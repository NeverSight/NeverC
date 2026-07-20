//===-- TargetSchemaGen.cpp - Emit LOCKSTEP target schemas -------*- C++ -*-===//

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegister.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/TargetParser/Triple.h"
#include <array>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using namespace llvm;

namespace {

cl::opt<std::string> OutputDir(
    "output-dir",
    cl::desc("Directory that receives <arch>.json schemas"),
    cl::value_desc("dir"), cl::Required);

cl::opt<std::string> OnlyArch(
    "arch",
    cl::desc("Generate a single architecture (x86_64 or aarch64)"),
    cl::value_desc("name"), cl::init(""));

constexpr uint32_t RegisterBase = 0x61000000u;
constexpr uint32_t InstructionBase = 0x62000000u;
constexpr uint32_t FeatureBase = 0x63000000u;

struct RegisterRecord {
  uint32_t StableID = 0;
  uint32_t BackendValue = 0;
  std::string Name;
  uint32_t Encoding = 0;
  int32_t Dwarf = -1;
  int32_t EH = -1;
  uint32_t SizeBits = 0;
  uint32_t AlignBits = 0;
  std::vector<uint32_t> Aliases;
  std::vector<uint32_t> SubRegs;
  std::vector<uint32_t> SuperRegs;
  std::vector<uint32_t> SubRegIndices;
  std::vector<uint32_t> RegClasses;
};

struct InstructionRecord {
  uint32_t StableID = 0;
  uint32_t BackendValue = 0;
  std::string Name;
  uint32_t NumOperands = 0;
  uint32_t NumDefs = 0;
  uint32_t SchedClass = 0;
  bool IsBranch = false;
  bool IsCall = false;
  bool IsReturn = false;
  bool IsTerminator = false;
  bool HasSideEffects = false;
  std::vector<uint32_t> ImplicitUses;
  std::vector<uint32_t> ImplicitDefs;
};

struct FeatureRecord {
  uint32_t StableID = 0;
  uint32_t BackendValue = 0;
  std::string Key;
  std::string Description;
  bool Default = false;
  std::vector<std::string> Implies;
  std::vector<std::string> Conflicts;
};

struct SchemaDraft {
  std::string Architecture;
  std::string Triple;
  std::string ProducerBuildID;
  uint32_t SchemaVersion = 1;
  std::vector<RegisterRecord> Registers;
  std::vector<InstructionRecord> Instructions;
  std::vector<FeatureRecord> Features;
};

void appendU32(raw_ostream &OS, uint32_t Value) { OS << Value << '\n'; }
void appendBool(raw_ostream &OS, bool Value) {
  OS << (Value ? '1' : '0') << '\n';
}
void appendString(raw_ostream &OS, StringRef Value) {
  OS << Value.size() << ':' << Value << '\n';
}
void appendU32List(raw_ostream &OS, ArrayRef<uint32_t> Values) {
  appendU32(OS, static_cast<uint32_t>(Values.size()));
  for (uint32_t Value : Values)
    appendU32(OS, Value);
}
void appendStringList(raw_ostream &OS, ArrayRef<std::string> Values) {
  appendU32(OS, static_cast<uint32_t>(Values.size()));
  for (const std::string &Value : Values)
    appendString(OS, Value);
}

std::string computeDigest(const SchemaDraft &Schema) {
  std::string Buffer;
  raw_string_ostream OS(Buffer);
  appendString(OS, Schema.Architecture);
  appendString(OS, Schema.Triple);
  appendString(OS, Schema.ProducerBuildID);
  appendU32(OS, Schema.SchemaVersion);
  appendU32(OS, static_cast<uint32_t>(Schema.Registers.size()));
  for (const RegisterRecord &Register : Schema.Registers) {
    appendU32(OS, Register.StableID);
    appendU32(OS, Register.BackendValue);
    appendString(OS, Register.Name);
    appendU32(OS, Register.Encoding);
    OS << Register.Dwarf << '\n';
    OS << Register.EH << '\n';
    appendU32(OS, Register.SizeBits);
    appendU32(OS, Register.AlignBits);
    appendU32List(OS, Register.Aliases);
    appendU32List(OS, Register.SubRegs);
    appendU32List(OS, Register.SuperRegs);
    appendU32List(OS, Register.SubRegIndices);
    appendU32List(OS, Register.RegClasses);
    OS << 0 << '\n';
  }
  appendU32(OS, static_cast<uint32_t>(Schema.Instructions.size()));
  for (const InstructionRecord &Instruction : Schema.Instructions) {
    appendU32(OS, Instruction.StableID);
    appendU32(OS, Instruction.BackendValue);
    appendString(OS, Instruction.Name);
    appendU32(OS, Instruction.NumOperands);
    appendU32(OS, Instruction.NumDefs);
    appendU32(OS, Instruction.SchedClass);
    appendBool(OS, Instruction.IsBranch);
    appendBool(OS, Instruction.IsCall);
    appendBool(OS, Instruction.IsReturn);
    appendBool(OS, Instruction.IsTerminator);
    appendBool(OS, Instruction.HasSideEffects);
    appendU32List(OS, Instruction.ImplicitUses);
    appendU32List(OS, Instruction.ImplicitDefs);
    OS << 0 << '\n';
  }
  appendU32(OS, static_cast<uint32_t>(Schema.Features.size()));
  for (const FeatureRecord &Feature : Schema.Features) {
    appendU32(OS, Feature.StableID);
    appendU32(OS, Feature.BackendValue);
    appendString(OS, Feature.Key);
    appendString(OS, Feature.Description);
    appendBool(OS, Feature.Default);
    appendStringList(OS, Feature.Implies);
    appendStringList(OS, Feature.Conflicts);
    OS << 0 << '\n';
  }
  OS.flush();
  std::array<uint8_t, 32> Hash =
      SHA256::hash(arrayRefFromStringRef(Buffer));
  std::string Hex;
  Hex.resize(64);
  static constexpr char Digits[] = "0123456789abcdef";
  for (size_t I = 0; I != Hash.size(); ++I) {
    Hex[I * 2] = Digits[Hash[I] >> 4];
    Hex[I * 2 + 1] = Digits[Hash[I] & 0xF];
  }
  return Hex;
}

std::string sanitizeText(StringRef Text) {
  std::string Out;
  Out.reserve(Text.size());
  for (unsigned char Ch : Text) {
    if (Ch == 0)
      break;
    if (Ch < 0x20 || Ch > 0x7E)
      Out.push_back('?');
    else
      Out.push_back(static_cast<char>(Ch));
  }
  return Out;
}

void writeEscaped(raw_ostream &OS, StringRef Text) {
  OS << '"';
  for (unsigned char Ch : Text) {
    switch (Ch) {
    case '"':
      OS << "\\\"";
      break;
    case '\\':
      OS << "\\\\";
      break;
    case '\n':
      OS << "\\n";
      break;
    case '\r':
      OS << "\\r";
      break;
    case '\t':
      OS << "\\t";
      break;
    default:
      if (Ch < 0x20)
        OS << format("\\u%04x", Ch);
      else
        OS << static_cast<char>(Ch);
      break;
    }
  }
  OS << '"';
}

void writeU32Array(raw_ostream &OS, ArrayRef<uint32_t> Values) {
  OS << '[';
  for (size_t I = 0; I != Values.size(); ++I) {
    if (I)
      OS << ',';
    OS << Values[I];
  }
  OS << ']';
}

void writeStringArray(raw_ostream &OS, ArrayRef<std::string> Values) {
  OS << '[';
  for (size_t I = 0; I != Values.size(); ++I) {
    if (I)
      OS << ',';
    writeEscaped(OS, Values[I]);
  }
  OS << ']';
}

Expected<SchemaDraft> collectSchema(StringRef Architecture, StringRef Triple) {
  std::string Error;
  const Target *TheTarget = TargetRegistry::lookupTarget(Triple.str(), Error);
  if (!TheTarget)
    return createStringError(std::make_error_code(std::errc::invalid_argument),
                             Error);

  std::unique_ptr<MCRegisterInfo> MRI(TheTarget->createMCRegInfo(Triple));
  std::unique_ptr<MCInstrInfo> MII(TheTarget->createMCInstrInfo());
  std::unique_ptr<MCSubtargetInfo> STI(
      TheTarget->createMCSubtargetInfo(Triple, "generic", ""));
  if (!MRI || !MII || !STI)
    return createStringError(std::make_error_code(std::errc::invalid_argument),
                             "failed to create MC tables for " +
                                 Triple.str());

  SchemaDraft Schema;
  Schema.Architecture = Architecture.str();
  Schema.Triple = Triple.str();
  Schema.ProducerBuildID =
      (Twine(LLVM_VERSION_MAJOR) + "." + Twine(LLVM_VERSION_MINOR) + "." +
       Twine(LLVM_VERSION_PATCH) + "+" + LLVM_VERSION_STRING)
          .str();

  uint32_t RegisterIndex = 0;
  for (unsigned Reg = 1, E = MRI->getNumRegs(); Reg != E; ++Reg) {
    RegisterRecord Record;
    Record.StableID = RegisterBase + RegisterIndex;
    Record.BackendValue = Reg;
    Record.Name = sanitizeText(MRI->getName(Reg));
    Record.Encoding = MRI->getEncodingValue(MCRegister(Reg));
    Record.Dwarf = MRI->getDwarfRegNum(MCRegister(Reg), false);
    Record.EH = MRI->getDwarfRegNum(MCRegister(Reg), true);
    for (MCRegAliasIterator Alias(MCRegister(Reg), MRI.get(), false);
         Alias.isValid(); ++Alias)
      Record.Aliases.push_back((*Alias).id());
    for (MCSubRegIterator Sub(MCRegister(Reg), MRI.get(), false); Sub.isValid();
         ++Sub)
      Record.SubRegs.push_back(*Sub);
    for (MCSuperRegIterator Super(MCRegister(Reg), MRI.get(), false);
         Super.isValid(); ++Super)
      Record.SuperRegs.push_back(*Super);
    for (MCSubRegIndexIterator Index(MCRegister(Reg), MRI.get());
         Index.isValid(); ++Index)
      Record.SubRegIndices.push_back(Index.getSubRegIndex());
    for (unsigned Class = 0, ClassEnd = MRI->getNumRegClasses();
         Class != ClassEnd; ++Class) {
      const MCRegisterClass &RC = MRI->getRegClass(Class);
      if (!RC.contains(MCRegister(Reg)))
        continue;
      Record.RegClasses.push_back(Class);
      if (Record.SizeBits == 0) {
        Record.SizeBits = RC.getSizeInBits();
        Record.AlignBits = RC.getSizeInBits();
      }
    }
    Schema.Registers.push_back(std::move(Record));
    ++RegisterIndex;
  }

  for (unsigned Opcode = 0, E = MII->getNumOpcodes(); Opcode != E; ++Opcode) {
    const MCInstrDesc &Desc = MII->get(Opcode);
    InstructionRecord Record;
    Record.StableID = InstructionBase + Opcode;
    Record.BackendValue = Opcode;
    Record.Name = sanitizeText(MII->getName(Opcode));
    Record.NumOperands = Desc.getNumOperands();
    Record.NumDefs = Desc.getNumDefs();
    Record.SchedClass = Desc.getSchedClass();
    Record.IsBranch = Desc.isBranch();
    Record.IsCall = Desc.isCall();
    Record.IsReturn = Desc.isReturn();
    Record.IsTerminator = Desc.isTerminator();
    Record.HasSideEffects = Desc.hasUnmodeledSideEffects();
    for (MCPhysReg Implicit : Desc.implicit_uses())
      Record.ImplicitUses.push_back(Implicit);
    for (MCPhysReg Implicit : Desc.implicit_defs())
      Record.ImplicitDefs.push_back(Implicit);
    Schema.Instructions.push_back(std::move(Record));
  }

  ArrayRef<SubtargetFeatureKV> Features = STI->getAllProcessorFeatures();
  for (unsigned I = 0, E = Features.size(); I != E; ++I) {
    FeatureRecord Record;
    Record.StableID = FeatureBase + I;
    Record.BackendValue = Features[I].Value;
    Record.Key = sanitizeText(Features[I].Key);
    Record.Description =
        sanitizeText(Features[I].Desc ? Features[I].Desc : "");
    Record.Default = STI->getFeatureBits().test(Features[I].Value);
    const FeatureBitset Implied = Features[I].Implies.getAsBitset();
    for (const SubtargetFeatureKV &Candidate : Features)
      if (Implied.test(Candidate.Value))
        Record.Implies.push_back(sanitizeText(Candidate.Key));
    Schema.Features.push_back(std::move(Record));
  }

  return Schema;
}

Error writeSchema(const SchemaDraft &Schema, StringRef OutputDirectory) {
  if (std::error_code EC = sys::fs::create_directories(OutputDirectory))
    return createFileError(OutputDirectory, EC);

  SmallString<256> Path(OutputDirectory);
  sys::path::append(Path, Schema.Architecture + ".json");
  std::error_code EC;
  raw_fd_ostream OS(Path, EC, sys::fs::OF_Text);
  if (EC)
    return createFileError(Path, EC);

  const std::string Digest = computeDigest(Schema);
  OS << '{';
  OS << "\"architecture\":";
  writeEscaped(OS, Schema.Architecture);
  OS << ",\"triple\":";
  writeEscaped(OS, Schema.Triple);
  OS << ",\"producer_build_id\":";
  writeEscaped(OS, Schema.ProducerBuildID);
  OS << ",\"schema_version\":" << Schema.SchemaVersion;
  OS << ",\"digest\":";
  writeEscaped(OS, Digest);
  OS << ",\"registers\":[";
  for (size_t I = 0; I != Schema.Registers.size(); ++I) {
    const RegisterRecord &Register = Schema.Registers[I];
    if (I)
      OS << ',';
    OS << '{';
    OS << "\"stable_id\":" << Register.StableID;
    OS << ",\"backend_value\":" << Register.BackendValue;
    OS << ",\"name\":";
    writeEscaped(OS, Register.Name);
    OS << ",\"encoding\":" << Register.Encoding;
    OS << ",\"dwarf\":" << Register.Dwarf;
    OS << ",\"eh\":" << Register.EH;
    OS << ",\"size_bits\":" << Register.SizeBits;
    OS << ",\"align_bits\":" << Register.AlignBits;
    OS << ",\"aliases\":";
    writeU32Array(OS, Register.Aliases);
    OS << ",\"subregs\":";
    writeU32Array(OS, Register.SubRegs);
    OS << ",\"superregs\":";
    writeU32Array(OS, Register.SuperRegs);
    OS << ",\"subreg_indices\":";
    writeU32Array(OS, Register.SubRegIndices);
    OS << ",\"reg_classes\":";
    writeU32Array(OS, Register.RegClasses);
    OS << ",\"flags\":0}";
  }
  OS << "],\"instructions\":[";
  for (size_t I = 0; I != Schema.Instructions.size(); ++I) {
    const InstructionRecord &Instruction = Schema.Instructions[I];
    if (I)
      OS << ',';
    OS << '{';
    OS << "\"stable_id\":" << Instruction.StableID;
    OS << ",\"backend_value\":" << Instruction.BackendValue;
    OS << ",\"name\":";
    writeEscaped(OS, Instruction.Name);
    OS << ",\"num_operands\":" << Instruction.NumOperands;
    OS << ",\"num_defs\":" << Instruction.NumDefs;
    OS << ",\"sched_class\":" << Instruction.SchedClass;
    OS << ",\"is_branch\":" << (Instruction.IsBranch ? "true" : "false");
    OS << ",\"is_call\":" << (Instruction.IsCall ? "true" : "false");
    OS << ",\"is_return\":" << (Instruction.IsReturn ? "true" : "false");
    OS << ",\"is_terminator\":"
       << (Instruction.IsTerminator ? "true" : "false");
    OS << ",\"has_side_effects\":"
       << (Instruction.HasSideEffects ? "true" : "false");
    OS << ",\"implicit_uses\":";
    writeU32Array(OS, Instruction.ImplicitUses);
    OS << ",\"implicit_defs\":";
    writeU32Array(OS, Instruction.ImplicitDefs);
    OS << ",\"flags\":0}";
  }
  OS << "],\"features\":[";
  for (size_t I = 0; I != Schema.Features.size(); ++I) {
    const FeatureRecord &Feature = Schema.Features[I];
    if (I)
      OS << ',';
    OS << '{';
    OS << "\"stable_id\":" << Feature.StableID;
    OS << ",\"backend_value\":" << Feature.BackendValue;
    OS << ",\"key\":";
    writeEscaped(OS, Feature.Key);
    OS << ",\"description\":";
    writeEscaped(OS, Feature.Description);
    OS << ",\"default\":" << (Feature.Default ? "true" : "false");
    OS << ",\"implies\":";
    writeStringArray(OS, Feature.Implies);
    OS << ",\"conflicts\":";
    writeStringArray(OS, Feature.Conflicts);
    OS << ",\"flags\":0}";
  }
  OS << "]}\n";
  return Error::success();
}

} // namespace

int main(int Argc, char **Argv) {
  cl::ParseCommandLineOptions(Argc, Argv, "NeverC LOCKSTEP target schema gen\n");

  InitializeAllTargetInfos();
  InitializeAllTargetMCs();

  struct TargetSpec {
    const char *Arch;
    const char *Triple;
  };
  const TargetSpec Specs[] = {
      {"x86_64", "x86_64-unknown-linux-gnu"},
      {"aarch64", "aarch64-unknown-linux-gnu"},
  };

  for (const TargetSpec &Spec : Specs) {
    if (!OnlyArch.empty() && OnlyArch != Spec.Arch)
      continue;
    auto Schema = collectSchema(Spec.Arch, Spec.Triple);
    if (!Schema) {
      errs() << toString(Schema.takeError()) << '\n';
      return 1;
    }
    if (Error E = writeSchema(*Schema, OutputDir)) {
      errs() << toString(std::move(E)) << '\n';
      return 1;
    }
    outs() << "wrote " << Spec.Arch << ".json (" << Schema->Registers.size()
           << " regs, " << Schema->Instructions.size() << " opcodes, "
           << Schema->Features.size() << " features)\n";
  }
  return 0;
}
