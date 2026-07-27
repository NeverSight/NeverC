#include "neverc/Plugin/Host/MCAsmPrinterProvider.h"
#include "neverc/Plugin/Host/AssemblySymbolName.h"
#include "neverc/Plugin/Host/MCPluginBridge.h"
#include "neverc/Plugin/Host/MCUnit.h"
#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <limits>
#include <memory>
#include <string>
#include <vector>

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

// What the printer writes goes straight back into the assembler, so a section
// has to come back out of that trip with the nature it went in with. Naming it
// and nothing else leaves that to the assembler, and each format fills an
// unstated flag set in differently: ELF derives one from the name, so a
// section not called ".text..." loses SHF_ALLOC and SHF_EXECINSTR; COFF
// settles on initialised read/write data, turning executable code into a
// writable data section; and Mach-O without "pure_instructions" yields a
// section that no longer reads back as text. The shorthands -- .text, .data,
// .bss -- have the same problem, each naming a section and choosing its
// attributes together, so every section states its own flags instead.
void printSectionDirective(const PluginMCSection &Section, const Triple &Target,
                           raw_ostream &Output) {
  const std::string Name = assemblyName(Section.Name);
  const bool Executable = (Section.Flags & NEVERC_MC_SECTION_EXECUTABLE) != 0;
  const bool Writable = (Section.Flags & NEVERC_MC_SECTION_WRITABLE) != 0;

  // Mach-O names a section by segment and section both, and ".section __foo"
  // alone is a syntax error there. The segment is not in the unit, so it comes
  // from what the section holds, the same way the object writer picks one.
  if (Target.isOSBinFormatMachO()) {
    Output << "\t.section\t" << (Executable ? "__TEXT" : "__DATA") << ','
           << Name;
    if (Executable)
      Output << ",regular,pure_instructions";
    Output << "\n";
    return;
  }
  if (Target.isOSBinFormatELF()) {
    std::string Flags;
    if ((Section.Flags & NEVERC_MC_SECTION_ALLOCATED) != 0)
      Flags.push_back('a');
    if (Writable)
      Flags.push_back('w');
    if (Executable)
      Flags.push_back('x');
    // No "M" for a mergeable section: the flag needs an entry size beside it,
    // the unit carries none, and the assembler rejects it on its own.
    Output << "\t.section\t" << Name << ",\"" << Flags << "\",@progbits\n";
    return;
  }
  // COFF states content and protection in one string. A fragment always has
  // contents, so this is always the initialised spelling -- 'b' would conflict
  // with 'd' and is what an uninitialised section would use.
  Output << "\t.section\t" << Name << ",\""
         << (Executable ? (Writable ? "xw" : "xr") : (Writable ? "dw" : "dr"))
         << "\"\n";
}

// ".weak" and ".hidden" are ELF spellings. Mach-O distinguishes a weak
// definition from a weak reference and marks hidden with ".private_extern";
// COFF takes ".weak" but has no hidden at all, and an unknown directive stops
// the assembler rather than being ignored.
void printSymbolAttributes(const PluginMCSymbol &Symbol, const Triple &Target,
                           raw_ostream &Output) {
  const std::string Name = assemblyName(Symbol.Name);
  if (Symbol.Binding == NEVERC_MC_SYMBOL_BINDING_GLOBAL)
    Output << "\t.globl\t" << Name << "\n";
  else if (Symbol.Binding == NEVERC_MC_SYMBOL_BINDING_WEAK) {
    if (Target.isOSBinFormatMachO())
      Output << (Symbol.Definition == NEVERC_MC_SYMBOL_DEFINITION_UNDEFINED
                     ? "\t.weak_reference\t"
                     : "\t.weak_definition\t")
             << Name << "\n";
    else
      Output << "\t.weak\t" << Name << "\n";
  }
  if (Symbol.Visibility == NEVERC_MC_SYMBOL_VISIBILITY_HIDDEN) {
    if (Target.isOSBinFormatMachO())
      Output << "\t.private_extern\t" << Name << "\n";
    else if (Target.isOSBinFormatELF())
      Output << "\t.hidden\t" << Name << "\n";
  }
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

  // An opcode states how many operands it takes, and the printer generated for
  // it reads exactly that many by index -- so one built with fewer runs off the
  // end of the operand list. Nothing upstream rules that out: the MC verifier
  // checks that an opcode and its registers belong to the target schema but not
  // how many operands accompany it, and a plugin builds an instruction by
  // creating it with an opcode and then appending operands one at a time, so
  // stopping early leaves an instruction that is well-formed by every rule
  // stated so far. Refusing it keeps a plugin's half-built instruction from
  // becoming a read past the end of memory the host owns.
  auto PrintInstruction = [&](const MCInst &Instruction) -> Error {
    const unsigned Opcode = Instruction.getOpcode();
    if (Opcode >= MCII->getNumOpcodes())
      return printerError("builtin assembly printer received opcode " +
                          Twine(Opcode) + ", which this target does not define");
    const MCInstrDesc &Description = MCII->get(Opcode);
    if (Instruction.getNumOperands() < Description.getNumOperands())
      return printerError(
          "builtin assembly printer received opcode " + Twine(Opcode) +
          " with " + Twine(Instruction.getNumOperands()) +
          " operands, but it takes " + Twine(Description.getNumOperands()));
    Output << "\t";
    Printer->printInst(&Instruction, 0, "", *STI, Output);
    Output << "\n";
    return Error::success();
  };

  if (!Unit.instructions().empty()) {
    Output << "\t.text\n";
    for (const auto &Instruction : Unit.instructions())
      if (Error E = PrintInstruction(*Instruction))
        return E;
  }

  // Attributes are written before any section rather than beside the label
  // they describe. A directive naming a symbol does not care where the label
  // lands, and a symbol belonging to no section -- an undefined weak reference
  // is the ordinary case -- is never reached by the per-section loop below, so
  // writing them there dropped its binding and turned the weak reference into
  // a strong one. Unresolved, the weak one is zero and the strong one is a
  // link error.
  for (const auto &Symbol : Unit.symbols()) {
    if (!expressibleName(Symbol->Name) ||
        isPrivateLabelName(Symbol->Name, TargetTriple))
      return printerError(
          "builtin assembly printer cannot write the name of a symbol");
    // The only definitions this can write are a label inside a section and no
    // definition at all. An absolute symbol is created by ".set" and a common
    // one by ".comm"; writing just the binding for either leaves it undefined,
    // taking the value or the size it was defined with with it.
    const bool Expressible =
        Symbol->Definition == NEVERC_MC_SYMBOL_DEFINITION_UNDEFINED ||
        (Symbol->Definition == NEVERC_MC_SYMBOL_DEFINITION_SECTION &&
         Symbol->Section != nullptr);
    if (!Expressible)
      return printerError(
          "builtin assembly printer cannot write the definition of symbol '" +
          Symbol->Name + "'");
    printSymbolAttributes(*Symbol, TargetTriple, Output);
  }

  for (const auto &Section : Unit.sections()) {
    if (!expressibleName(Section->Name))
      return printerError(
          "builtin assembly printer cannot write the name of a section");
    printSectionDirective(*Section, TargetTriple, Output);
    if (Section->Alignment > 1)
      Output << "\t.balign\t" << Section->Alignment << "\n";

    std::vector<const PluginMCSymbol *> Defined;
    for (const auto &Symbol : Unit.symbols())
      if (Symbol->Section == Section.get())
        Defined.push_back(Symbol.get());
    llvm::sort(Defined, [](const PluginMCSymbol *Left,
                           const PluginMCSymbol *Right) {
      if (Left->Value != Right->Value)
        return Left->Value < Right->Value;
      return Left->Name < Right->Name;
    });

    // A symbol becomes a label placed at its value, which means splitting a
    // fragment's bytes around it. Writing every label up front instead only
    // landed the ones at offset zero, and the rest were dropped without a
    // word -- the definition simply left the printer as an undefined symbol.
    size_t NextSymbol = 0;
    uint64_t Offset = 0;
    bool OffsetKnown = true;
    for (const auto &Fragment : Section->Fragments) {
      if (!Fragment->Fixups.empty())
        return printerError(
            "builtin assembly printer cannot materialize detached fixups");
      if (Fragment->ExplicitOffset != NEVERC_MC_AUTOMATIC_OFFSET) {
        Output << "\t.org\t" << Fragment->ExplicitOffset << "\n";
        Offset = Fragment->ExplicitOffset;
        OffsetKnown = true;
      }
      if (Fragment->Alignment > 1) {
        Output << "\t.balign\t" << Fragment->Alignment << "\n";
        Offset = alignTo(Offset, Fragment->Alignment);
      }
      const ArrayRef<uint8_t> Contents(Fragment->Contents);
      size_t Written = 0;
      while (NextSymbol != Defined.size() &&
             Defined[NextSymbol]->Value <= Offset + Contents.size()) {
        const PluginMCSymbol &Symbol = *Defined[NextSymbol];
        // Instructions are handed to the assembler as text, so how many bytes
        // they take is its answer to give, not one this can count.
        if (!OffsetKnown || Symbol.Value < Offset)
          return printerError(
              "builtin assembly printer cannot place symbol '" + Symbol.Name +
              "' at its offset");
        const size_t Cut = static_cast<size_t>(Symbol.Value - Offset);
        printBytes(Contents.slice(Written, Cut - Written), Output);
        Written = Cut;
        Output << assemblyName(Symbol.Name) << ":\n";
        ++NextSymbol;
      }
      printBytes(Contents.drop_front(Written), Output);
      Offset += Contents.size();
      for (const auto &Instruction : Fragment->Instructions)
        if (Error E = PrintInstruction(*Instruction))
          return E;
      if (!Fragment->Instructions.empty())
        OffsetKnown = false;
    }
    if (NextSymbol != Defined.size())
      return printerError("builtin assembly printer cannot place symbol '" +
                          Defined[NextSymbol]->Name +
                          "' past the end of its section");
  }
  return Error::success();
}

} // namespace neverc::plugin
