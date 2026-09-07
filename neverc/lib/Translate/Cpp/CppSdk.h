#ifndef NEVERC_TRANSLATE_CPP_CPPSDK_H
#define NEVERC_TRANSLATE_CPP_CPPSDK_H

#include "../TranslateIR.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/JSON.h"

namespace neverc::translate {
inline constexpr const char *CppMathSDKID =
    "neverc-embedded-clang20.1.8-libcxx200100-macos15.5";
struct CppSdkContext {
  std::string DistributionID;
  std::string CatalogSHA256;
  std::string TargetTriple;
  std::vector<SDKDependency> ApprovedFiles;
};

/// The driver and internal frontend embed the same approved header bytes.
llvm::StringRef approvedCppSdkCatalog();
std::string approvedCppSdkCatalogSHA256();
bool validateCppMathTarget(llvm::StringRef Target, Diagnostics &Errors);

/// Approve only the immutable built-in distribution. No filesystem lookup or
/// caller-supplied descriptor can select different headers.
bool loadBuiltinCppSdk(llvm::StringRef Target, CppSdkContext &Result,
                       Diagnostics &Errors);
llvm::json::Object cppSdkRequestJSON(const CppSdkContext &Context);

/// Checks admitted root/path/hash identities against the embedded bytes.
/// SDK dependencies are distinct from owned source paths.
bool verifyCppSdkDependencies(const CppSdkContext &Context,
                              llvm::ArrayRef<SDKDependency> Dependencies,
                              Diagnostics &Errors);
bool verifyCppSdkMappings(const CppSdkContext &Context,
                          llvm::ArrayRef<MappingEvidence> Mappings,
                          Diagnostics &Errors);

} // namespace neverc::translate
#endif
