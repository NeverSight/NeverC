#ifndef NEVERC_TRANSLATE_CPP_CPPSDK_H
#define NEVERC_TRANSLATE_CPP_CPPSDK_H

#include "../TranslateIR.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/JSON.h"

namespace neverc::translate {
inline constexpr const char *CppMathSDKID =
    "clang20.1.8-libcxx200100-macos15.5";

struct CppSdkRoot {
  std::string Name;
  std::string AbsolutePath;
};
struct CppSdkContext {
  std::string DistributionID;
  std::string CatalogSHA256;
  std::string DescriptorPath;
  std::string DescriptorSHA256;
  std::string TargetTriple;
  std::vector<CppSdkRoot> Roots;
  std::vector<SDKDependency> ApprovedFiles;
};

/// Both binaries compile the same implementation-owned catalog JSON bytes.
llvm::StringRef approvedCppSdkCatalog();
std::string approvedCppSdkCatalogSHA256();
bool validateCppMathTarget(llvm::StringRef Target, Diagnostics &Errors);

/// Descriptor roots locate already installed external files; descriptor hashes
/// cannot approve another SDK. Result is empty on failure.
bool loadCppSdk(llvm::StringRef Descriptor, llvm::StringRef Target,
                CppSdkContext &Result, Diagnostics &Errors);
llvm::json::Object cppSdkRequestJSON(const CppSdkContext &Context);

/// Checks admitted root/path/hash identities and current bytes, including the
/// descriptor snapshot. SDK dependencies are distinct from owned source paths.
bool verifyCppSdkDependencies(const CppSdkContext &Context,
                              llvm::ArrayRef<SDKDependency> Dependencies,
                              Diagnostics &Errors);
bool verifyCppSdkMappings(const CppSdkContext &Context,
                          llvm::ArrayRef<MappingEvidence> Mappings,
                          Diagnostics &Errors);

} // namespace neverc::translate
#endif
