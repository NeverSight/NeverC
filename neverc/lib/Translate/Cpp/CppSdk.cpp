#include "CppSdk.h"
#include "../JSON.h"
#include "BuiltinCppSdkData.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SHA256.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <set>

namespace neverc::translate {
namespace {
using namespace llvm;
constexpr auto &Catalog = neverc_cpp_sdk::CatalogJSON;

std::string digest(StringRef Bytes) {
  SHA256 Hash;
  Hash.update(Bytes);
  std::string Out;
  constexpr char Hex[] = "0123456789abcdef";
  for (uint8_t Byte : Hash.final()) {
    Out += Hex[Byte >> 4];
    Out += Hex[Byte & 15];
  }
  return Out;
}
bool error(Diagnostics &D, StringRef File, StringRef Why,
           StringRef Code = "TR0101") {
  D.push_back(driverDiagnostic(
      Code, File, "built-in C++ SDK", Why,
      "Use the matching NeverC executable and its built-in C++ frontend."));
  return false;
}
bool rootName(StringRef Name) {
  return Name == "libcxx" || Name == "resource" || Name == "platform";
}
bool relativePath(StringRef Path) {
  if (Path.empty() || Path.contains('\0') || Path.contains('\\') ||
      Path.contains(':') || Path.contains('\n') || Path.contains('\r') ||
      sys::path::is_absolute(Path))
    return false;
  SmallVector<StringRef, 16> Parts;
  Path.split(Parts, '/', -1, true);
  for (StringRef Part : Parts)
    if (Part.empty() || Part == "." || Part == "..")
      return false;
  return true;
}
bool validHash(StringRef Hash) {
  return Hash.size() == 64 &&
         Hash.find_first_not_of("0123456789abcdef") == StringRef::npos;
}
bool approvedFile(const CppSdkContext &C, const SDKDependency &Input) {
  return std::any_of(C.ApprovedFiles.begin(), C.ApprovedFiles.end(),
                     [&](const SDKDependency &Approved) {
                       return Approved.Root == Input.Root &&
                              Approved.Path == Input.Path &&
                              Approved.SHA256 == Input.SHA256;
                     });
}
bool verifyFile(const CppSdkContext &, const SDKDependency &Input,
                Diagnostics &D) {
  for (const auto &File : neverc_cpp_sdk::Files) {
    if (Input.Root != File.Root || Input.Path != File.Path)
      continue;
    if (Input.SHA256 != File.SHA256 ||
        digest(StringRef(File.Contents, File.Size)) != Input.SHA256)
      return error(D, Input.Path,
                   "embedded SDK bytes do not match the catalog");
    return true;
  }
  return error(D, Input.Path, "file is not part of the embedded SDK", "TR0103");
}
bool catalogInputs(const json::Object &O, StringRef Key,
                   std::vector<SDKDependency> &Inputs, Diagnostics &D) {
  const auto *A = O.getArray(Key);
  if (!A)
    return error(D, "<sdk-catalog>", "invalid compiled SDK catalog array");
  std::set<std::pair<std::string, std::string>> Seen;
  for (const auto &V : *A) {
    const auto *F = V.getAsObject();
    if (!F)
      return error(D, "<sdk-catalog>", "invalid compiled SDK catalog file");
    auto Root = F->getString("root"), Path = F->getString("path"),
         Hash = F->getString("sha256");
    if (!rootName(Root) || !relativePath(Path) || !validHash(Hash) ||
        !Seen.insert({Root.str(), Path.str()}).second)
      return error(D, "<sdk-catalog>",
                   "invalid or duplicate compiled SDK identity");
    Inputs.push_back({Root.str(), Path.str(), Hash.str()});
  }
  return true;
}
bool parseCatalog(CppSdkContext &C, std::vector<SDKDependency> &Metadata,
                  Diagnostics &D) {
  auto V = json::parse(Catalog);
  if (!V) {
    consumeError(V.takeError());
    return error(D, "<sdk-catalog>", "cannot parse compiled SDK catalog");
  }
  const auto *O = V->getAsObject();
  int64_t Version = 0, Libcxx = 0;
  if (!O || O->getString("schema") != "neverc.cpp.sdk.catalog" ||
      !O->getInteger("version", Version) || Version != 1 ||
      O->getString("distribution_id") != CppMathSDKID ||
      O->getString("clang_version") != "20.1.8" ||
      !O->getInteger("libcxx_version", Libcxx) || Libcxx != 200100 ||
      O->getString("platform_sdk_version") != "15.5" ||
      digest(Catalog) != neverc_cpp_sdk::CatalogSHA256)
    return error(D, "<sdk-catalog>", "incompatible compiled SDK catalog");
  C.DistributionID = CppMathSDKID;
  C.CatalogSHA256 = digest(Catalog);
  return catalogInputs(*O, "headers", C.ApprovedFiles, D) &&
         catalogInputs(*O, "metadata", Metadata, D);
}
} // namespace

llvm::StringRef approvedCppSdkCatalog() { return Catalog; }
std::string approvedCppSdkCatalogSHA256() { return digest(Catalog); }

bool validateCppMathTarget(llvm::StringRef Target, Diagnostics &D) {
  llvm::Triple T(llvm::Triple::normalize(Target));
  llvm::VersionTuple Deployment;
  if ((T.getArch() != llvm::Triple::x86_64 &&
       T.getArch() != llvm::Triple::aarch64) ||
      T.getVendor() != llvm::Triple::Apple || !T.isMacOSX() ||
      !T.isLittleEndian() || !T.getEnvironmentName().empty() ||
      !T.getMacOSXVersion(Deployment) || Deployment.getMajor() != 15 ||
      Deployment.getMinor() != 0 || Deployment.getSubminor() != 0) {
    D.push_back(driverDiagnostic(
        "TR0204", "<target>", "math target",
        "cpp-math-v1 requires a tested macOS 15.0 target (x86_64 or arm64)",
        "Use --target x86_64-apple-macosx15.0.0 or --target "
        "arm64-apple-macosx15.0.0."));
    return false;
  }
  return true;
}

bool loadBuiltinCppSdk(llvm::StringRef Target, CppSdkContext &Result,
                       Diagnostics &D) {
  Result = {};
  if (!validateCppMathTarget(Target, D))
    return false;
  CppSdkContext Parsed;
  std::vector<SDKDependency> Metadata;
  if (!parseCatalog(Parsed, Metadata, D))
    return false;
  if (Parsed.ApprovedFiles.size() + Metadata.size() !=
      neverc_cpp_sdk::FileCount)
    return error(D, "<sdk-catalog>", "embedded SDK inventory is inconsistent");
  Parsed.TargetTriple = llvm::Triple::normalize(Target);
  for (const auto &Input : Parsed.ApprovedFiles)
    if (!verifyFile(Parsed, Input, D))
      return false;
  for (const auto &Input : Metadata)
    if (!verifyFile(Parsed, Input, D))
      return false;
  Result = std::move(Parsed);
  return true;
}

llvm::json::Object cppSdkRequestJSON(const CppSdkContext &C) {
  return llvm::json::Object{{"distribution_id", jsonString(C.DistributionID)},
                            {"catalog_sha256", jsonString(C.CatalogSHA256)}};
}

bool verifyCppSdkDependencies(const CppSdkContext &C,
                              llvm::ArrayRef<SDKDependency> Dependencies,
                              Diagnostics &D) {
  if (C.DistributionID != CppMathSDKID || C.CatalogSHA256 != digest(Catalog))
    return error(D, "<sdk>",
                 "SDK context was not approved by this implementation",
                 "TR0103");
  if (!validateCppMathTarget(C.TargetTriple, D))
    return false;
  // Reconstruct immutable authority instead of trusting caller-owned lists.
  CppSdkContext Approved;
  std::vector<SDKDependency> Metadata;
  if (!parseCatalog(Approved, Metadata, D))
    return false;
  std::set<std::pair<std::string, std::string>> Seen;
  for (const auto &Input : Dependencies) {
    if (!approvedFile(Approved, Input) ||
        !Seen.insert({Input.Root, Input.Path}).second)
      return error(D, Input.Path, "unapproved or duplicate SDK dependency",
                   "TR0103");
    if (!verifyFile(C, Input, D))
      return false;
  }
  for (const auto &Input : Metadata)
    if (!verifyFile(C, Input, D))
      return false;
  return true;
}

bool verifyCppSdkMappings(const CppSdkContext &C,
                          llvm::ArrayRef<MappingEvidence> Mappings,
                          Diagnostics &D) {
  CppSdkContext Approved;
  std::vector<SDKDependency> Metadata;
  if (C.DistributionID != CppMathSDKID || C.CatalogSHA256 != digest(Catalog) ||
      !parseCatalog(Approved, Metadata, D))
    return error(D, "<sdk>", "mapping has no approved SDK context", "TR0103");
  if (!validateCppMathTarget(C.TargetTriple, D))
    return false;
  std::set<std::string> Seen;
  for (const auto &M : Mappings) {
    const auto *Spec = findMappingSpec(M.ID);
    const bool Fabs = M.ID == "cpp.math.fabs.f64.v1";
    const std::string USR = Fabs ? "c:@F@fabs" : "c:@F@floor";
    const std::string ExpectedID =
        digest(std::string(CppMathSDKID) + "\nplatform\nusr/include/math.h\n" +
               USR + "\ndouble(double)");
    SDKDependency Origin{M.Origin.Root, M.Origin.Path, M.Origin.SHA256};
    if (!Spec || !Seen.insert(M.ID).second || M.DeclarationID != ExpectedID ||
        M.Result.Kind != TypeKind::Double || !M.Result.RecordID.empty() ||
        M.Parameters.size() != 1 || M.Parameters[0].Kind != TypeKind::Double ||
        !M.Parameters[0].RecordID.empty() || Origin.Root != "platform" ||
        Origin.Path != "usr/include/math.h" ||
        !approvedFile(Approved, Origin) ||
        M.Origin.Line != (Fabs ? 423u : 466u) || M.Origin.Column != 15u)
      return error(D, M.Origin.Path,
                   "mapping declaration/signature/origin does not match the "
                   "approved library operation",
                   "TR0103");
    if (!verifyFile(C, Origin, D))
      return false;
  }
  return true;
}

} // namespace neverc::translate
