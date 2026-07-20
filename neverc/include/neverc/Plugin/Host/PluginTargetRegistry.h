#ifndef NEVERC_PLUGIN_HOST_PLUGINTARGETREGISTRY_H
#define NEVERC_PLUGIN_HOST_PLUGINTARGETREGISTRY_H

#include "neverc/Plugin/Host/BuiltinTargetProvider.h"
#include "neverc/Plugin/Host/PluginTargetDescriptor.h"
#include "neverc/Plugin/PluginMC.h"
#include "neverc/Plugin/PluginObject.h"
#include "neverc/Plugin/PluginTarget.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace neverc::plugin {

class PluginModule;
class PluginProcessServices;

struct PluginTargetRegistrationView {
  llvm::StringRef PluginID;
  std::shared_ptr<const PluginModule> Owner;
  llvm::ArrayRef<NevercTargetDescriptor> Targets;
  llvm::ArrayRef<NevercTargetABIDescriptor> ABIs;
  llvm::ArrayRef<NevercCallingConventionDescriptor> CallingConventions;
  llvm::ArrayRef<NevercMCSchemaDescriptor> MCSchemas;
  llvm::ArrayRef<NevercObjectFormatDescriptor> ObjectFormats;
  llvm::ArrayRef<NevercCodeGenEdgeDescriptor> CodeGenEdges;
};

struct PluginTargetOptionLayer {
  std::string Triple;
  std::string CPU;
  std::string Features;
  std::string ObjectFormat;
  NevercTargetRelocationModel RelocationModel = 0;
  NevercTargetCodeModel CodeModel = 0;
  NevercTargetExecutionLevel ExecutionLevel = 0;
};

struct PluginTargetRequest : PluginTargetOptionLayer {
  PluginTargetOptionLayer Configuration;
  PluginTargetOptionLayer Platform;
};

class PluginTargetSnapshot {
public:
  struct MCSchemaValueRecord {
    uint32_t StableID = 0;
    uint32_t BackendValue = 0;
    std::string CanonicalName;
    uint64_t Flags = 0;
  };

  struct TripleMatcher {
    std::string Architecture;
    std::string Vendor;
    std::string OperatingSystem;
    std::string Environment;
    uint32_t Priority = 0;
  };

  struct TargetRecord {
    std::string PluginID;
    std::shared_ptr<const PluginModule> Owner;
    NevercTargetID ID{};
    std::string CanonicalName;
    std::vector<std::string> Aliases;
    std::vector<TripleMatcher> Matchers;
    NevercTargetABIID DefaultABI{};
    NevercCallingConventionID DefaultCallingConvention{};
    NevercInterfaceID MCSchemaID{};
    NevercInterfaceID DefaultObjectFormatID{};
    VerifiedTargetMachineDescriptor Machine;
    std::vector<VerifiedTargetMacro> Macros;
    std::vector<VerifiedTargetBuiltin> Builtins;
    std::vector<VerifiedTargetRegister> Registers;
    std::vector<VerifiedTargetConstraint> Constraints;
    std::string Clobbers;
    uint64_t Flags = 0;
    NevercTargetValidateCPUFn ValidateCPU = nullptr;
    NevercTargetCanonicalizeCPUFn CanonicalizeCPU = nullptr;
    NevercTargetListCPUsFn ListCPUs = nullptr;
    NevercTargetResolveFeaturesFn ResolveFeatures = nullptr;
    NevercCreateTargetMachineFn CreateTargetMachine = nullptr;
    NevercDestroyTargetMachineFn DestroyTargetMachine = nullptr;
    void *TargetUserData = nullptr;
  };

  struct NamedRecord {
    std::string PluginID;
    std::shared_ptr<const PluginModule> Owner;
    NevercInterfaceID ID{};
    NevercTargetID TargetID{};
    std::string CanonicalName;
    std::string Digest;
    std::vector<NevercInterfaceID> Dependencies;
    NevercClassifyABIFunctionFn ClassifyFunction = nullptr;
    NevercTargetVAArgDescriptor VAArg{};
    std::vector<std::string> CalleeSavedRegisters;
    uint32_t LLVMCallingConvention = 0;
    NevercPlanCallingConventionFn PlanCallingConvention = nullptr;
    std::vector<MCSchemaValueRecord> Opcodes;
    std::vector<MCSchemaValueRecord> SchemaRegisters;
    std::vector<MCSchemaValueRecord> OperandKinds;
    std::vector<MCSchemaValueRecord> Relocations;
    std::vector<MCSchemaValueRecord> Variants;
    uint64_t Flags = 0;
    void *CallbackUserData = nullptr;
  };

  struct ObjectFormatRecord {
    std::string PluginID;
    std::shared_ptr<const PluginModule> Owner;
    NevercObjectFormatID ID{};
    std::string CanonicalName;
    std::vector<std::string> Aliases;
    std::vector<NevercTargetID> SupportedTargets;
    std::string DefaultExtension;
    uint64_t Flags = 0;
    NevercObjectProbeFn Probe = nullptr;
    NevercObjectReaderFn Reader = nullptr;
    NevercObjectWriterFn Writer = nullptr;
    void *CallbackUserData = nullptr;
  };

  struct CodeGenEdgeRecord : NamedRecord {
    NevercCodeGenProductKind InputKind = 0;
    NevercCodeGenProductKind OutputKind = 0;
    NevercInterfaceID ProductID{};
    std::string CompatibilityKey;
    std::string ProviderID;
    NevercCoarseCodeGenLowerFn CoarseLower = nullptr;
    NevercVerifyCodeGenProductFn VerifyProduct = nullptr;
  };

  size_t targetCount() const { return Targets.size(); }
  size_t abiCount() const { return ABIs.size(); }
  size_t callingConventionCount() const { return CallingConventions.size(); }
  size_t mcSchemaCount() const { return MCSchemas.size(); }
  size_t objectFormatCount() const { return ObjectFormats.size(); }
  size_t codeGenEdgeCount() const { return CodeGenEdges.size(); }
  size_t builtinTargetCount() const;

  const TargetRecord *findTarget(NevercTargetID ID) const;
  const NamedRecord *findABI(NevercTargetABIID ID) const;
  const NamedRecord *
  findCallingConvention(NevercCallingConventionID ID) const;
  const NamedRecord *findMCSchema(NevercInterfaceID ID) const;
  const ObjectFormatRecord *
  findObjectFormat(NevercObjectFormatID ID) const;
  const TargetRecord *matchTarget(llvm::StringRef Selector) const;
  llvm::ArrayRef<BuiltinTargetRoute> builtinTargets() const;
  const BuiltinTargetRoute *
  matchBuiltinTarget(llvm::StringRef Selector) const;
  llvm::ArrayRef<TargetRecord> targets() const { return Targets; }
  llvm::ArrayRef<CodeGenEdgeRecord> codeGenEdges() const {
    return CodeGenEdges;
  }
  llvm::ArrayRef<ObjectFormatRecord> objectFormats() const {
    return ObjectFormats;
  }
  const TargetRecord *selectedTarget() const { return SelectedTarget; }
  const OwnedTargetKey *targetKey() const { return SelectedKey.get(); }
  llvm::ArrayRef<const CodeGenEdgeRecord *> route() const { return Route; }

private:
  std::vector<TargetRecord> Targets;
  std::vector<NamedRecord> ABIs;
  std::vector<NamedRecord> CallingConventions;
  std::vector<NamedRecord> MCSchemas;
  std::vector<ObjectFormatRecord> ObjectFormats;
  std::vector<CodeGenEdgeRecord> CodeGenEdges;
  const TargetRecord *SelectedTarget = nullptr;
  std::unique_ptr<OwnedTargetKey> SelectedKey;
  std::vector<const CodeGenEdgeRecord *> Route;

  friend class PluginTargetRegistry;
};

class PluginTargetRegistry {
public:
  static llvm::Expected<std::shared_ptr<const PluginTargetSnapshot>>
  freeze(llvm::ArrayRef<PluginTargetRegistrationView> Registrations,
         const PluginTargetRequest &Request);
  static llvm::Expected<std::shared_ptr<const PluginTargetSnapshot>>
  freeze(llvm::ArrayRef<std::shared_ptr<const PluginModule>> Modules,
         const PluginTargetRequest &Request);
};

llvm::Error registerPluginTargetInterfaces(PluginProcessServices &Services);
std::shared_ptr<const PluginTargetSnapshot>
findPluginTargetSnapshot(PluginProcessServices &Services,
                         NevercSessionHandle Session);

} // namespace neverc::plugin

#endif
