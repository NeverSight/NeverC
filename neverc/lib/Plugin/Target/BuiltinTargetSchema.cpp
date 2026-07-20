#include "neverc/Plugin/Host/BuiltinTargetSchema.h"
#include "neverc/Plugin/Host/TargetSchemaDigest.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include <system_error>

using namespace llvm;

namespace neverc::plugin {
namespace {

Error makeError(Twine Message) {
  return createStringError(std::make_error_code(std::errc::invalid_argument),
                           Message);
}

Expected<uint64_t> requireUint(const json::Object &Object, StringRef Key) {
  const json::Value *Value = Object.get(Key);
  uint64_t Out = 0;
  if (!Value || !Value->getAsUINT64(Out))
    return makeError("missing or invalid uint field '" + Key + "'");
  return Out;
}

Expected<int64_t> requireInt(const json::Object &Object, StringRef Key) {
  const json::Value *Value = Object.get(Key);
  int64_t Out = 0;
  if (!Value || !Value->getAsInteger(Out))
    return makeError("missing or invalid int field '" + Key + "'");
  return Out;
}

Expected<bool> requireBool(const json::Object &Object, StringRef Key) {
  const json::Value *Value = Object.get(Key);
  if (!Value)
    return makeError("missing bool field '" + Key + "'");
  int AsBool = Value->getAsBoolean();
  if (AsBool >= 0)
    return AsBool != 0;
  return makeError("missing or invalid bool field '" + Key + "'");
}

Expected<std::string> requireString(const json::Object &Object,
                                    StringRef Key) {
  const json::Value *Value = Object.get(Key);
  if (!Value)
    return makeError("missing string field '" + Key + "'");
  StringRef Text = Value->getAsString();
  if (Text.data() == nullptr && Text.empty()) {
    // Distinguish missing/non-string from legitimate empty string by kind.
    if (Value->getAsObject() || Value->getAsArray())
      return makeError("missing or invalid string field '" + Key + "'");
  }
  // Empty string is allowed; non-string types return empty StringRef with null.
  if (Text.data() == nullptr)
    return makeError("missing or invalid string field '" + Key + "'");
  return Text.str();
}

Expected<std::vector<uint32_t>>
requireU32Array(const json::Object &Object, StringRef Key) {
  const json::Value *Value = Object.get(Key);
  if (!Value || !Value->getAsArray())
    return makeError("missing or invalid array field '" + Key + "'");
  std::vector<uint32_t> Result;
  for (const json::Value &Entry : *Value->getAsArray()) {
    uint64_t Out = 0;
    if (!Entry.getAsUINT64(Out))
      return makeError("array '" + Key + "' contains non-uint entry");
    Result.push_back(static_cast<uint32_t>(Out));
  }
  return Result;
}

Expected<std::vector<std::string>>
requireStringArray(const json::Object &Object, StringRef Key) {
  const json::Value *Value = Object.get(Key);
  if (!Value || !Value->getAsArray())
    return makeError("missing or invalid array field '" + Key + "'");
  std::vector<std::string> Result;
  for (const json::Value &Entry : *Value->getAsArray()) {
    StringRef Text = Entry.getAsString();
    if (Text.data() == nullptr)
      return makeError("array '" + Key + "' contains non-string entry");
    Result.push_back(Text.str());
  }
  return Result;
}

Expected<BuiltinTargetRegister> parseRegister(const json::Object &Object) {
  BuiltinTargetRegister Register;
  auto StableID = requireUint(Object, "stable_id");
  if (!StableID)
    return StableID.takeError();
  Register.StableID = static_cast<uint32_t>(*StableID);
  auto BackendValue = requireUint(Object, "backend_value");
  if (!BackendValue)
    return BackendValue.takeError();
  Register.BackendValue = static_cast<uint32_t>(*BackendValue);
  auto Name = requireString(Object, "name");
  if (!Name)
    return Name.takeError();
  Register.CanonicalName = std::move(*Name);
  auto Encoding = requireUint(Object, "encoding");
  if (!Encoding)
    return Encoding.takeError();
  Register.Encoding = static_cast<uint32_t>(*Encoding);
  auto Dwarf = requireInt(Object, "dwarf");
  if (!Dwarf)
    return Dwarf.takeError();
  Register.DwarfNumber = static_cast<int32_t>(*Dwarf);
  auto EH = requireInt(Object, "eh");
  if (!EH)
    return EH.takeError();
  Register.EHNumber = static_cast<int32_t>(*EH);
  auto Size = requireUint(Object, "size_bits");
  if (!Size)
    return Size.takeError();
  Register.SizeInBits = static_cast<uint32_t>(*Size);
  auto Align = requireUint(Object, "align_bits");
  if (!Align)
    return Align.takeError();
  Register.AlignmentInBits = static_cast<uint32_t>(*Align);
  auto Aliases = requireU32Array(Object, "aliases");
  if (!Aliases)
    return Aliases.takeError();
  Register.Aliases = std::move(*Aliases);
  auto SubRegs = requireU32Array(Object, "subregs");
  if (!SubRegs)
    return SubRegs.takeError();
  Register.SubRegs = std::move(*SubRegs);
  auto SuperRegs = requireU32Array(Object, "superregs");
  if (!SuperRegs)
    return SuperRegs.takeError();
  Register.SuperRegs = std::move(*SuperRegs);
  auto SubRegIndices = requireU32Array(Object, "subreg_indices");
  if (!SubRegIndices)
    return SubRegIndices.takeError();
  Register.SubRegIndices = std::move(*SubRegIndices);
  auto RegClasses = requireU32Array(Object, "reg_classes");
  if (!RegClasses)
    return RegClasses.takeError();
  Register.RegClasses = std::move(*RegClasses);
  auto Flags = requireUint(Object, "flags");
  if (!Flags)
    return Flags.takeError();
  Register.Flags = *Flags;
  return Register;
}

Expected<BuiltinTargetInstruction>
parseInstruction(const json::Object &Object) {
  BuiltinTargetInstruction Instruction;
  auto StableID = requireUint(Object, "stable_id");
  if (!StableID)
    return StableID.takeError();
  Instruction.StableID = static_cast<uint32_t>(*StableID);
  auto BackendValue = requireUint(Object, "backend_value");
  if (!BackendValue)
    return BackendValue.takeError();
  Instruction.BackendValue = static_cast<uint32_t>(*BackendValue);
  auto Name = requireString(Object, "name");
  if (!Name)
    return Name.takeError();
  Instruction.CanonicalName = std::move(*Name);
  auto NumOperands = requireUint(Object, "num_operands");
  if (!NumOperands)
    return NumOperands.takeError();
  Instruction.NumOperands = static_cast<uint32_t>(*NumOperands);
  auto NumDefs = requireUint(Object, "num_defs");
  if (!NumDefs)
    return NumDefs.takeError();
  Instruction.NumDefs = static_cast<uint32_t>(*NumDefs);
  auto SchedClass = requireUint(Object, "sched_class");
  if (!SchedClass)
    return SchedClass.takeError();
  Instruction.SchedClass = static_cast<uint32_t>(*SchedClass);
  auto IsBranch = requireBool(Object, "is_branch");
  if (!IsBranch)
    return IsBranch.takeError();
  Instruction.IsBranch = *IsBranch;
  auto IsCall = requireBool(Object, "is_call");
  if (!IsCall)
    return IsCall.takeError();
  Instruction.IsCall = *IsCall;
  auto IsReturn = requireBool(Object, "is_return");
  if (!IsReturn)
    return IsReturn.takeError();
  Instruction.IsReturn = *IsReturn;
  auto IsTerminator = requireBool(Object, "is_terminator");
  if (!IsTerminator)
    return IsTerminator.takeError();
  Instruction.IsTerminator = *IsTerminator;
  auto HasSideEffects = requireBool(Object, "has_side_effects");
  if (!HasSideEffects)
    return HasSideEffects.takeError();
  Instruction.HasSideEffects = *HasSideEffects;
  auto ImplicitUses = requireU32Array(Object, "implicit_uses");
  if (!ImplicitUses)
    return ImplicitUses.takeError();
  Instruction.ImplicitUses = std::move(*ImplicitUses);
  auto ImplicitDefs = requireU32Array(Object, "implicit_defs");
  if (!ImplicitDefs)
    return ImplicitDefs.takeError();
  Instruction.ImplicitDefs = std::move(*ImplicitDefs);
  auto Flags = requireUint(Object, "flags");
  if (!Flags)
    return Flags.takeError();
  Instruction.Flags = *Flags;
  return Instruction;
}

Expected<BuiltinTargetFeature> parseFeature(const json::Object &Object) {
  BuiltinTargetFeature Feature;
  auto StableID = requireUint(Object, "stable_id");
  if (!StableID)
    return StableID.takeError();
  Feature.StableID = static_cast<uint32_t>(*StableID);
  auto BackendValue = requireUint(Object, "backend_value");
  if (!BackendValue)
    return BackendValue.takeError();
  Feature.BackendValue = static_cast<uint32_t>(*BackendValue);
  auto Key = requireString(Object, "key");
  if (!Key)
    return Key.takeError();
  Feature.Key = std::move(*Key);
  auto Description = requireString(Object, "description");
  if (!Description)
    return Description.takeError();
  Feature.Description = std::move(*Description);
  auto Default = requireBool(Object, "default");
  if (!Default)
    return Default.takeError();
  Feature.Default = *Default;
  auto Implies = requireStringArray(Object, "implies");
  if (!Implies)
    return Implies.takeError();
  Feature.Implies = std::move(*Implies);
  auto Conflicts = requireStringArray(Object, "conflicts");
  if (!Conflicts)
    return Conflicts.takeError();
  Feature.Conflicts = std::move(*Conflicts);
  auto Flags = requireUint(Object, "flags");
  if (!Flags)
    return Flags.takeError();
  Feature.Flags = *Flags;
  return Feature;
}

} // namespace

Expected<BuiltinTargetSchema>
loadBuiltinTargetSchema(StringRef SchemaRoot, StringRef Architecture) {
  SmallString<256> Path(SchemaRoot);
  sys::path::append(Path, (Architecture + ".json").str());
  auto Buffer = MemoryBuffer::getFile(Path);
  if (!Buffer)
    return createFileError(Path, Buffer.getError());

  auto Parsed = json::parse((*Buffer)->getBuffer());
  if (!Parsed)
    return Parsed.takeError();
  json::Object *Root = Parsed->getAsObject();
  if (!Root)
    return makeError("target schema root must be an object");

  BuiltinTargetSchema Schema;
  auto ArchitectureValue = requireString(*Root, "architecture");
  if (!ArchitectureValue)
    return ArchitectureValue.takeError();
  Schema.Architecture = std::move(*ArchitectureValue);
  if (Schema.Architecture != Architecture)
    return makeError("schema architecture field does not match requested name");

  auto Triple = requireString(*Root, "triple");
  if (!Triple)
    return Triple.takeError();
  Schema.Triple = std::move(*Triple);

  auto Producer = requireString(*Root, "producer_build_id");
  if (!Producer)
    return Producer.takeError();
  Schema.ProducerBuildID = std::move(*Producer);

  auto Digest = requireString(*Root, "digest");
  if (!Digest)
    return Digest.takeError();
  Schema.Digest = std::move(*Digest);

  auto Version = requireUint(*Root, "schema_version");
  if (!Version)
    return Version.takeError();
  Schema.SchemaVersion = static_cast<uint32_t>(*Version);

  const json::Value *RegistersValue = Root->get("registers");
  if (!RegistersValue || !RegistersValue->getAsArray())
    return makeError("missing registers array");
  for (const json::Value &Entry : *RegistersValue->getAsArray()) {
    const json::Object *Object = Entry.getAsObject();
    if (!Object)
      return makeError("register entry must be an object");
    auto Register = parseRegister(*Object);
    if (!Register)
      return Register.takeError();
    Schema.Registers.push_back(std::move(*Register));
  }

  const json::Value *InstructionsValue = Root->get("instructions");
  if (!InstructionsValue || !InstructionsValue->getAsArray())
    return makeError("missing instructions array");
  for (const json::Value &Entry : *InstructionsValue->getAsArray()) {
    const json::Object *Object = Entry.getAsObject();
    if (!Object)
      return makeError("instruction entry must be an object");
    auto Instruction = parseInstruction(*Object);
    if (!Instruction)
      return Instruction.takeError();
    Schema.Instructions.push_back(std::move(*Instruction));
  }

  const json::Value *FeaturesValue = Root->get("features");
  if (!FeaturesValue || !FeaturesValue->getAsArray())
    return makeError("missing features array");
  for (const json::Value &Entry : *FeaturesValue->getAsArray()) {
    const json::Object *Object = Entry.getAsObject();
    if (!Object)
      return makeError("feature entry must be an object");
    auto Feature = parseFeature(*Object);
    if (!Feature)
      return Feature.takeError();
    Schema.Features.push_back(std::move(*Feature));
  }

  std::string ExpectedDigest = computeTargetSchemaDigest(Schema);
  if (ExpectedDigest != Schema.Digest)
    return makeError("target schema digest mismatch for " + Architecture);

  return Schema;
}

const BuiltinTargetRegister *
findRegisterByBackendValue(const BuiltinTargetSchema &Schema,
                           uint32_t BackendValue) {
  for (const BuiltinTargetRegister &Register : Schema.Registers)
    if (Register.BackendValue == BackendValue)
      return &Register;
  return nullptr;
}

const BuiltinTargetInstruction *
findInstructionByBackendValue(const BuiltinTargetSchema &Schema,
                              uint32_t BackendValue) {
  for (const BuiltinTargetInstruction &Instruction : Schema.Instructions)
    if (Instruction.BackendValue == BackendValue)
      return &Instruction;
  return nullptr;
}

Error validateTargetSchemaToken(const BuiltinTargetSchema &Schema,
                                StringRef TokenDigest) {
  if (TokenDigest != Schema.Digest)
    return makeError("target schema token digest mismatch");
  return Error::success();
}

} // namespace neverc::plugin
