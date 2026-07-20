#include "neverc/Plugin/Host/BuiltinTargetSchema.h"
#include "neverc/Plugin/Host/TargetSchemaDigest.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/raw_ostream.h"
#include <array>
#include <string>

using namespace llvm;

namespace neverc::plugin {
namespace {

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

std::string canonicalPayload(const BuiltinTargetSchema &Schema) {
  std::string Buffer;
  raw_string_ostream OS(Buffer);
  appendString(OS, Schema.Architecture);
  appendString(OS, Schema.Triple);
  appendString(OS, Schema.ProducerBuildID);
  appendU32(OS, Schema.SchemaVersion);

  appendU32(OS, static_cast<uint32_t>(Schema.Registers.size()));
  for (const BuiltinTargetRegister &Register : Schema.Registers) {
    appendU32(OS, Register.StableID);
    appendU32(OS, Register.BackendValue);
    appendString(OS, Register.CanonicalName);
    appendU32(OS, Register.Encoding);
    OS << Register.DwarfNumber << '\n';
    OS << Register.EHNumber << '\n';
    appendU32(OS, Register.SizeInBits);
    appendU32(OS, Register.AlignmentInBits);
    appendU32List(OS, Register.Aliases);
    appendU32List(OS, Register.SubRegs);
    appendU32List(OS, Register.SuperRegs);
    appendU32List(OS, Register.SubRegIndices);
    appendU32List(OS, Register.RegClasses);
    OS << Register.Flags << '\n';
  }

  appendU32(OS, static_cast<uint32_t>(Schema.Instructions.size()));
  for (const BuiltinTargetInstruction &Instruction : Schema.Instructions) {
    appendU32(OS, Instruction.StableID);
    appendU32(OS, Instruction.BackendValue);
    appendString(OS, Instruction.CanonicalName);
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
    OS << Instruction.Flags << '\n';
  }

  appendU32(OS, static_cast<uint32_t>(Schema.Features.size()));
  for (const BuiltinTargetFeature &Feature : Schema.Features) {
    appendU32(OS, Feature.StableID);
    appendU32(OS, Feature.BackendValue);
    appendString(OS, Feature.Key);
    appendString(OS, Feature.Description);
    appendBool(OS, Feature.Default);
    appendStringList(OS, Feature.Implies);
    appendStringList(OS, Feature.Conflicts);
    OS << Feature.Flags << '\n';
  }
  OS.flush();
  return Buffer;
}

} // namespace

std::string digestCanonicalTargetSchemaJSON(StringRef CanonicalJSON) {
  std::array<uint8_t, 32> Hash =
      SHA256::hash(arrayRefFromStringRef(CanonicalJSON));
  std::string Hex;
  Hex.resize(64);
  static constexpr char Digits[] = "0123456789abcdef";
  for (size_t I = 0; I != Hash.size(); ++I) {
    Hex[I * 2] = Digits[Hash[I] >> 4];
    Hex[I * 2 + 1] = Digits[Hash[I] & 0xF];
  }
  return Hex;
}

std::string computeTargetSchemaDigest(const BuiltinTargetSchema &Schema) {
  return digestCanonicalTargetSchemaJSON(canonicalPayload(Schema));
}

} // namespace neverc::plugin
