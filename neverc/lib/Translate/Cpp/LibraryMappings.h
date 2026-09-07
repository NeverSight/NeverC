#ifndef NEVERC_TRANSLATE_CPP_LIBRARYMAPPINGS_H
#define NEVERC_TRANSLATE_CPP_LIBRARYMAPPINGS_H

#include "../TranslateIR.h"
#include "llvm/ADT/ArrayRef.h"
#include <functional>

namespace neverc::translate {
inline constexpr const char *MathRuntimeFingerprintPolicy =
    "neverc.math.canonical-ir.v1";

struct RuntimeModuleData {
  std::string Name;
  std::string Bitcode;
};
struct RuntimeModuleCapability {
  std::string Name;
  std::string SHA256;
  std::string ImplementationIdentity;
  std::vector<std::string> DefinedSymbols;
  std::vector<std::string> RequiredSymbols;
};
struct MathRuntimeCapabilities {
  std::string ID;
  std::string Target;
  std::string HeaderRelativePath;
  std::string HeaderSHA256;
  std::vector<std::string> MappingIDs;
  std::vector<RuntimeModuleCapability> Modules;
};

/// Controlled providers are a test seam. Production uses the installed resource
/// header and BuiltinStd's embedded modules; no provider comes from user data.
struct RuntimeCapabilityProviders {
  std::function<bool(llvm::StringRef Path, std::string &Contents,
                     std::string &Reason)>
      ReadHeader;
  std::function<std::vector<RuntimeModuleData>(llvm::StringRef Target)> Modules;
};

bool inspectMathRuntime(llvm::StringRef Target,
                        llvm::StringRef NeverCResourceDirectory,
                        llvm::ArrayRef<std::string> RequiredMappingIDs,
                        bool BuiltinStdEnabled, MathRuntimeCapabilities &Result,
                        Diagnostics &Errors,
                        const RuntimeCapabilityProviders *Providers = nullptr);

/// Used for review/test evidence; this computes an identity, not approval.
bool mathRuntimeImplementationIdentity(llvm::StringRef Bitcode,
                                       std::string &Identity,
                                       Diagnostics &Errors);
llvm::StringRef approvedMathRuntimeHeaderSHA256();
llvm::StringRef approvedMathRuntimeIdentity(llvm::StringRef Target,
                                            llvm::StringRef Module);

} // namespace neverc::translate
#endif
