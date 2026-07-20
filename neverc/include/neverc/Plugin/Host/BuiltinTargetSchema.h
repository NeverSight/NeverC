#ifndef NEVERC_PLUGIN_HOST_BUILTINTARGETSCHEMA_H
#define NEVERC_PLUGIN_HOST_BUILTINTARGETSCHEMA_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <string>
#include <vector>

namespace neverc::plugin {

struct BuiltinTargetRegister {
  uint32_t StableID = 0;
  uint32_t BackendValue = 0;
  std::string CanonicalName;
  uint32_t Encoding = 0;
  int32_t DwarfNumber = -1;
  int32_t EHNumber = -1;
  uint32_t SizeInBits = 0;
  uint32_t AlignmentInBits = 0;
  std::vector<uint32_t> Aliases;
  std::vector<uint32_t> SubRegs;
  std::vector<uint32_t> SuperRegs;
  std::vector<uint32_t> SubRegIndices;
  std::vector<uint32_t> RegClasses;
  uint64_t Flags = 0;
};

struct BuiltinTargetInstruction {
  uint32_t StableID = 0;
  uint32_t BackendValue = 0;
  std::string CanonicalName;
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
  uint64_t Flags = 0;
};

struct BuiltinTargetFeature {
  uint32_t StableID = 0;
  uint32_t BackendValue = 0;
  std::string Key;
  std::string Description;
  bool Default = false;
  std::vector<std::string> Implies;
  std::vector<std::string> Conflicts;
  uint64_t Flags = 0;
};

struct BuiltinTargetSchema {
  std::string Architecture;
  std::string Triple;
  std::string ProducerBuildID;
  std::string Digest;
  uint32_t SchemaVersion = 1;
  std::vector<BuiltinTargetRegister> Registers;
  std::vector<BuiltinTargetInstruction> Instructions;
  std::vector<BuiltinTargetFeature> Features;
};

llvm::Expected<BuiltinTargetSchema>
loadBuiltinTargetSchema(llvm::StringRef SchemaRoot,
                        llvm::StringRef Architecture);

const BuiltinTargetRegister *
findRegisterByBackendValue(const BuiltinTargetSchema &Schema,
                           uint32_t BackendValue);

const BuiltinTargetInstruction *
findInstructionByBackendValue(const BuiltinTargetSchema &Schema,
                              uint32_t BackendValue);

llvm::Error validateTargetSchemaToken(const BuiltinTargetSchema &Schema,
                                      llvm::StringRef TokenDigest);

} // namespace neverc::plugin

#endif
