#ifndef NEVERC_PLUGIN_HOST_BUILTINTARGETPROVIDER_H
#define NEVERC_PLUGIN_HOST_BUILTINTARGETPROVIDER_H

#include "neverc/Plugin/PluginTarget.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

namespace llvm {
class Module;
class Target;
class TargetMachine;
} // namespace llvm

namespace neverc {
class TargetInfo;

namespace plugin {

enum class BuiltinObjectFormat {
  ELF,
  COFF,
  MachO,
};

enum class BuiltinTargetABIKind {
  X86_64SysV,
  X86_64Win64,
  AArch64AAPCS,
  AArch64DarwinPCS,
  AArch64Win64,
};

struct BuiltinTargetRoute {
  llvm::StringRef CanonicalName;
  llvm::StringRef CanonicalTriple;
  llvm::StringRef DefaultCPU;
  NevercTargetID TargetID{};
  NevercTargetABIID ABIID{};
  NevercInterfaceID MCSchemaID{};
  NevercInterfaceID ObjectFormatID{};
  BuiltinTargetABIKind ABI = BuiltinTargetABIKind::X86_64SysV;
  BuiltinObjectFormat ObjectFormat = BuiltinObjectFormat::ELF;
  bool SupportsSource = false;
  bool SupportsAssembly = false;
  bool SupportsObject = false;
  bool SupportsMCDecode = false;
};

/// Returns the complete, stable inventory of built-in NeverC target routes.
llvm::ArrayRef<BuiltinTargetRoute> builtinTargetRoutes();

/// Matches aliases and versioned triples while preserving platform ABI
/// boundaries (for example macOS versus iOS, and Linux versus Android).
const BuiltinTargetRoute *findBuiltinTargetRoute(llvm::StringRef Triple);

/// Read-only adapter over LLVM's process-global target registry.
llvm::Expected<const llvm::Target *>
lookupBuiltinLLVMTarget(const BuiltinTargetRoute &Route);

/// Selects the existing NeverC ABI lowering for a built-in TargetInfo.
const BuiltinTargetRoute *
selectBuiltinNeverCTargetRoute(const neverc::TargetInfo &Target);

/// Cross-validates the frontend/module selection against the LLVM target
/// machine before code generation writes output.
llvm::Error validateBuiltinTargetPipeline(
    const BuiltinTargetRoute &Route, const llvm::Module &Module,
    const llvm::TargetMachine &Machine, llvm::StringRef RequestedCPU,
    llvm::StringRef RequestedFeatures);

} // namespace plugin
} // namespace neverc

#endif
