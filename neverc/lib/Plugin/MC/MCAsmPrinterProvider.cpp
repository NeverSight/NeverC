#include "neverc/Plugin/Host/MCAsmPrinterProvider.h"
#include "neverc/Plugin/Host/MCPluginBridge.h"
#include "neverc/Plugin/Host/MCUnit.h"
#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <limits>
#include <memory>

using namespace llvm;

namespace neverc::plugin {
namespace {

Error printerError(const Twine &Message) {
  return createStringError(errc::invalid_argument, Message);
}

bool sameID(NevercInterfaceID Left, NevercInterfaceID Right) {
  return Left.High == Right.High && Left.Low == Right.Low;
}

void printBytes(ArrayRef<uint8_t> Bytes, raw_ostream &Output) {
  for (size_t Offset = 0; Offset < Bytes.size(); Offset += 16) {
    Output << "\t.byte\t";
    const size_t End = std::min(Offset + 16, Bytes.size());
    for (size_t I = Offset; I != End; ++I) {
      if (I != Offset)
        Output << ", ";
      Output << "0x" << format_hex_no_prefix(Bytes[I], 2);
    }
    Output << "\n";
  }
}

void printSectionDirective(StringRef Name, raw_ostream &Output) {
  if (Name == ".text" || Name == ".data" || Name == ".bss")
    Output << "\t" << Name << "\n";
  else
    Output << "\t.section\t" << Name << "\n";
}

} // namespace

AssemblyOutputBuilder::AssemblyOutputBuilder(uint64_t MaximumBytesValue)
    : MaximumBytes(MaximumBytesValue) {}

Error AssemblyOutputBuilder::write(StringRef Text) {
  if (Closed)
    return printerError("assembly output builder is closed");
  if (Text.size() > MaximumBytes ||
      Staging.size() > MaximumBytes - Text.size())
    return printerError("assembly output exceeds its configured limit");
  Staging.append(Text.data(), Text.size());
  return Error::success();
}

void AssemblyOutputBuilder::rollback() {
  Staging.clear();
  Closed = true;
}

Expected<AssemblyOutputArtifact>
AssemblyOutputBuilder::finish(NevercTargetID Target,
                              StringRef SchemaDigest,
                              uint64_t UnitGeneration) {
  if (Closed)
    return printerError("assembly output builder is closed");
  Closed = true;
  AssemblyOutputArtifact Result;
  Result.Text = std::move(Staging);
  Result.TargetID = Target;
  Result.TargetSchemaDigest = SchemaDigest.str();
  Result.UnitGeneration = UnitGeneration;
  Result.Finished = true;
  if (Error E = Result.verify())
    return std::move(E);
  return Result;
}

Expected<AssemblyOutputArtifact>
MCAsmPrinterProviderRuntime::execute(
    const AssemblyPrintExecutionRequest &Request,
    ReplacementProvider Replacement, BuiltinProvider Builtin) {
  if (!Request.Task || !Request.Snapshot || !Request.Unit ||
      Request.MaximumOutputBytes == 0 ||
      Request.MaximumOutputBytes >
          static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
    return printerError("invalid assembly print execution request");

  const auto *Target =
      Request.Snapshot->findTarget(Request.Unit->targetID());
  if (!Target)
    return printerError("assembly printer target is not registered");
  const auto *Schema =
      Request.Snapshot->findMCSchema(Target->MCSchemaID);
  if (!Schema || !sameID(Schema->TargetID, Target->ID) ||
      Request.Unit->targetSchemaDigest() != Schema->Digest)
    return printerError("assembly printer input has a foreign MC schema");
  if (Error E = verifyPluginMCUnit(*Request.Unit, Schema))
    return joinErrors(printerError("assembly print input verification failed"),
                      std::move(E));

  AssemblyOutputBuilder Output(Request.MaximumOutputBytes);
  MCPluginBridge Bridge(*Request.Task, *Request.Unit, Schema,
                        /*AllowMutation=*/false);
  if (Replacement) {
    if (Error E = Replacement(Bridge, Output)) {
      Output.rollback();
      return joinErrors(
          printerError("replacement assembly printer failed"),
          std::move(E));
    }
  } else {
    if (!Builtin)
      return printerError("assembly print route has no provider");
    auto Text = Builtin();
    if (!Text)
      return Text.takeError();
    if (Error E = Output.write(*Text)) {
      Output.rollback();
      return E;
    }
  }

  return Output.finish(Target->ID, Schema->Digest,
                       Bridge.unitGeneration());
}

Error BuiltinLLVMAsmPrinter::print(
    const Target &Target, const Triple &TargetTriple, StringRef CPU,
    StringRef Features, const PluginMCUnit &Unit, raw_ostream &Output) {
  MCTargetOptions Options;
  std::unique_ptr<MCRegisterInfo> MRI(
      Target.createMCRegInfo(TargetTriple.str()));
  std::unique_ptr<MCAsmInfo> MAI(
      MRI ? Target.createMCAsmInfo(*MRI, TargetTriple.str(), Options)
          : nullptr);
  std::unique_ptr<MCSubtargetInfo> STI(
      Target.createMCSubtargetInfo(TargetTriple.str(), CPU, Features));
  std::unique_ptr<MCInstrInfo> MCII(Target.createMCInstrInfo());
  std::unique_ptr<MCInstPrinter> Printer(
      MAI && MCII && MRI
          ? Target.createMCInstPrinter(TargetTriple, 0, *MAI, *MCII, *MRI)
          : nullptr);
  if (!MRI || !MAI || !STI || !MCII || !Printer)
    return printerError(
        "target does not provide the required assembly printer components");

  auto PrintInstruction = [&](const MCInst &Instruction) {
    Output << "\t";
    Printer->printInst(&Instruction, 0, "", *STI, Output);
    Output << "\n";
  };

  if (!Unit.instructions().empty()) {
    Output << "\t.text\n";
    for (const auto &Instruction : Unit.instructions())
      PrintInstruction(*Instruction);
  }

  for (const auto &Section : Unit.sections()) {
    printSectionDirective(Section->Name, Output);
    if (Section->Alignment > 1)
      Output << "\t.balign\t" << Section->Alignment << "\n";
    for (const auto &Symbol : Unit.symbols()) {
      if (Symbol->Section != Section.get())
        continue;
      if (Symbol->Binding == NEVERC_MC_SYMBOL_BINDING_GLOBAL)
        Output << "\t.globl\t" << Symbol->Name << "\n";
      else if (Symbol->Binding == NEVERC_MC_SYMBOL_BINDING_WEAK)
        Output << "\t.weak\t" << Symbol->Name << "\n";
      if (Symbol->Visibility == NEVERC_MC_SYMBOL_VISIBILITY_HIDDEN)
        Output << "\t.hidden\t" << Symbol->Name << "\n";
      if (Symbol->Value == 0)
        Output << Symbol->Name << ":\n";
    }
    for (const auto &Fragment : Section->Fragments) {
      if (!Fragment->Fixups.empty())
        return printerError(
            "builtin assembly printer cannot materialize detached fixups");
      if (Fragment->ExplicitOffset != NEVERC_MC_AUTOMATIC_OFFSET)
        Output << "\t.org\t" << Fragment->ExplicitOffset << "\n";
      if (Fragment->Alignment > 1)
        Output << "\t.balign\t" << Fragment->Alignment << "\n";
      printBytes(Fragment->Contents, Output);
      for (const auto &Instruction : Fragment->Instructions)
        PrintInstruction(*Instruction);
    }
  }
  return Error::success();
}

} // namespace neverc::plugin
