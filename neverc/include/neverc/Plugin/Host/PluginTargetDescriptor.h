#ifndef NEVERC_PLUGIN_HOST_PLUGINTARGETDESCRIPTOR_H
#define NEVERC_PLUGIN_HOST_PLUGINTARGETDESCRIPTOR_H

#include "neverc/Plugin/PluginTarget.h"
#include "llvm/Support/Error.h"
#include <initializer_list>
#include <string>
#include <vector>

namespace neverc::plugin {

class OwnedTargetKey {
public:
  NevercTargetKey view() const;

private:
  NevercTargetID TargetID{};
  std::string RawTriple;
  std::string Architecture;
  std::string Vendor;
  std::string OperatingSystem;
  std::string Environment;
  std::string CPU;
  std::string TuneCPU;
  std::vector<std::string> Features;
  NevercTargetABIID ABIID{};
  NevercCallingConventionID CallingConventionID{};
  NevercInterfaceID ObjectFormatID{};
  NevercTargetRelocationModel RelocationModel = 0;
  NevercTargetCodeModel CodeModel = 0;
  NevercTargetExecutionLevel ExecutionLevel = 0;
  uint32_t PointerWidth = 0;
  NevercTargetEndianness Endianness = 0;
  std::string SchemaDigest;
  mutable std::vector<NevercStringView> FeatureViews;

  friend class TargetKeyBuilder;
};

class TargetKeyBuilder {
public:
  TargetKeyBuilder &setTargetID(NevercTargetID ID);
  TargetKeyBuilder &setTriple(std::string RawTriple,
                              std::string Architecture,
                              std::string Vendor,
                              std::string OperatingSystem,
                              std::string Environment);
  TargetKeyBuilder &setCPU(std::string CPU, std::string TuneCPU);
  TargetKeyBuilder &setFeatures(std::vector<std::string> Features);
  TargetKeyBuilder &
  setFeatures(std::initializer_list<std::string> Features);
  TargetKeyBuilder &setABI(NevercTargetABIID ID);
  TargetKeyBuilder &
  setCallingConvention(NevercCallingConventionID ID);
  TargetKeyBuilder &setObjectFormat(NevercInterfaceID ID);
  TargetKeyBuilder &
  setCodeGeneration(NevercTargetRelocationModel RelocationModel,
                    NevercTargetCodeModel CodeModel);
  TargetKeyBuilder &setExecution(NevercTargetExecutionLevel Level,
                                 uint32_t PointerWidth,
                                 NevercTargetEndianness Endianness);
  TargetKeyBuilder &setSchemaDigest(std::string Digest);

  llvm::Expected<OwnedTargetKey> build() const;

private:
  OwnedTargetKey Key;
};

struct VerifiedTargetFeature {
  std::string Name;
  std::vector<std::string> Implies;
  std::vector<std::string> Conflicts;
  bool EnabledByDefault = false;
};

struct VerifiedTargetAddressSpace {
  uint32_t AddressSpace = 0;
  uint32_t PointerWidth = 0;
  uint32_t ABIAlignment = 0;
  uint32_t PreferredAlignment = 0;
  uint64_t Flags = 0;
};

struct VerifiedTargetMacro {
  std::string Name;
  std::string Value;
  bool Undefine = false;
};

struct VerifiedTargetBuiltin {
  std::string Name;
  std::string TypeEncoding;
  std::string Attributes;
  std::string RequiredFeatures;
  std::string HeaderName;
  uint32_t Languages = 0;
};

struct VerifiedTargetRegister {
  std::string Name;
  std::vector<std::string> Aliases;
};

struct VerifiedTargetConstraint {
  std::string Spelling;
  uint64_t Flags = 0;
  int32_t ImmediateMinimum = 0;
  int32_t ImmediateMaximum = 0;
  std::string ConvertedConstraint;
};

struct VerifiedTargetMachineDescriptor {
  std::string RawTriple;
  std::string Architecture;
  std::string Vendor;
  std::string OperatingSystem;
  std::string Environment;
  std::string DataLayout;
  std::string DefaultCPU;
  std::string TuneCPU;
  std::string GlobalLabelPrefix;
  std::string SchemaDigest;
  std::vector<std::string> CPUs;
  std::vector<VerifiedTargetFeature> Features;
  std::vector<NevercTargetABIID> ABIs;
  std::vector<NevercCallingConventionID> CallingConventions;
  std::vector<NevercInterfaceID> ObjectFormats;
  std::vector<VerifiedTargetAddressSpace> AddressSpaces;
  uint64_t SupportedRelocationModels = 0;
  uint64_t SupportedCodeModels = 0;
  NevercTargetRelocationModel DefaultRelocationModel = 0;
  NevercTargetCodeModel DefaultCodeModel = 0;
  NevercTargetExceptionModel ExceptionModel = 0;
  NevercTargetUnwindModel UnwindModel = 0;
  NevercTargetEndianness Endianness = 0;
  uint32_t PointerWidth = 0;
  uint32_t IntWidth = 0;
  uint32_t LongWidth = 0;
  uint32_t LongLongWidth = 0;
  uint32_t StackAlignment = 0;
  uint32_t MaximumAtomicWidth = 0;
  uint32_t MaximumVectorAlignment = 0;
  NevercTargetBuiltinVaListKind BuiltinVaListKind = 0;
  NevercTargetExecutionLevel ExecutionLevels = 0;
  NevercTargetExecutionLevel DefaultExecutionLevel = 0;
  bool TLSSupported = false;
};

llvm::Expected<VerifiedTargetMachineDescriptor>
verifyTargetMachineDescriptor(const NevercTargetMachineDescriptor &Descriptor);

} // namespace neverc::plugin

#endif
