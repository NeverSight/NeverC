#include "CppSdk.h"
#include "../ArtifactWriter.h"
#include "../JSON.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SHA256.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <map>
#include <set>

namespace neverc::translate {
namespace {
using namespace llvm;
constexpr std::size_t MaxDescriptorBytes = 64 * 1024;
constexpr std::size_t MaxSDKFileBytes = 4 * 1024 * 1024;
constexpr char Catalog[] =
#include "CppSdkCatalog.inc"
    ;

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
      Code, File, "approved C++ SDK", Why,
      "Select the pinned external SDK descriptor for "
      "clang20.1.8-libcxx200100-macos15.5 and keep its files unchanged."));
  return false;
}
bool readFile(StringRef Path, std::size_t Limit, std::string &Out,
              std::string &Why) {
  auto Bytes = neverc::translate::readFile(Path, Limit);
  if (!Bytes) {
    Why = toString(Bytes.takeError()).str().str();
    return false;
  }
  Out = std::move(*Bytes);
  return true;
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
bool inside(StringRef Root, StringRef Path) {
  auto R = sys::path::begin(Root), RE = sys::path::end(Root);
  auto P = sys::path::begin(Path), PE = sys::path::end(Path);
  for (; R != RE; ++R, ++P)
    if (P == PE || *R != *P)
      return false;
  return true;
}
bool validHash(StringRef Hash) {
  return Hash.size() == 64 &&
         Hash.find_first_not_of("0123456789abcdef") == StringRef::npos;
}
const CppSdkRoot *findRoot(const CppSdkContext &C, StringRef Name) {
  for (const auto &Root : C.Roots)
    if (Root.Name == Name)
      return &Root;
  return nullptr;
}
bool approvedFile(const CppSdkContext &C, const SDKDependency &Input) {
  return std::any_of(C.ApprovedFiles.begin(), C.ApprovedFiles.end(),
                     [&](const SDKDependency &Approved) {
                       return Approved.Root == Input.Root &&
                              Approved.Path == Input.Path &&
                              Approved.SHA256 == Input.SHA256;
                     });
}
bool verifyFile(const CppSdkContext &C, const SDKDependency &Input,
                Diagnostics &D) {
  const auto *Root = findRoot(C, Input.Root);
  if (!Root || !relativePath(Input.Path) || !validHash(Input.SHA256))
    return error(D, Input.Path, "invalid SDK dependency identity", "TR0103");
  SmallString<256> Path(Root->AbsolutePath), Real;
  sys::path::append(Path, Input.Path);
  if (auto EC = sys::fs::real_path(Path, Real))
    return error(D, Input.Path, "SDK file is unavailable: " + EC.message());
  if (!inside(Root->AbsolutePath, Real) || Real != Path)
    return error(
        D, Input.Path,
        "SDK file is not the canonical declared root-relative identity");
  std::string Bytes, Why;
  if (!readFile(Real, MaxSDKFileBytes, Bytes, Why))
    return error(D, Input.Path, "cannot read SDK file: " + Why);
  if (digest(Bytes) != Input.SHA256)
    return error(
        D, Input.Path,
        "SDK file does not match the implementation-owned approved hash");
  return true;
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
      O->getString("platform_sdk_version") != "15.5")
    return error(D, "<sdk-catalog>", "incompatible compiled SDK catalog");
  C.DistributionID = CppMathSDKID;
  C.CatalogSHA256 = digest(Catalog);
  return catalogInputs(*O, "headers", C.ApprovedFiles, D) &&
         catalogInputs(*O, "metadata", Metadata, D);
}
bool boundedJSONDepth(StringRef Text) {
  unsigned Depth = 0;
  bool Quoted = false, Escaped = false;
  for (char Ch : Text) {
    if (Quoted) {
      if (Escaped)
        Escaped = false;
      else if (Ch == '\\')
        Escaped = true;
      else if (Ch == '"')
        Quoted = false;
    } else if (Ch == '"')
      Quoted = true;
    else if (Ch == '{' || Ch == '[') {
      if (++Depth > 16)
        return false;
    } else if ((Ch == '}' || Ch == ']') && Depth)
      --Depth;
  }
  return true;
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

bool loadCppSdk(llvm::StringRef Descriptor, llvm::StringRef Target,
                CppSdkContext &Result, Diagnostics &D) {
  Result = {};
  if (!validateCppMathTarget(Target, D))
    return false;
  CppSdkContext Parsed;
  std::vector<SDKDependency> Metadata;
  if (!parseCatalog(Parsed, Metadata, D))
    return false;
  Parsed.TargetTriple = llvm::Triple::normalize(Target);
  llvm::SmallString<256> Real;
  if (Descriptor.empty() || Descriptor.contains('\0') ||
      llvm::sys::fs::real_path(Descriptor, Real))
    return error(D, Descriptor,
                 "SDK descriptor is missing or cannot be resolved");
  Parsed.DescriptorPath = Real.str().str();
  std::string Bytes, Why;
  if (!readFile(Real, MaxDescriptorBytes, Bytes, Why))
    return error(D, Descriptor, "cannot read SDK descriptor: " + Why);
  if (!boundedJSONDepth(Bytes))
    return error(D, Descriptor, "SDK descriptor nesting limit exceeded");
  auto V = llvm::json::parse(Bytes);
  if (!V) {
    llvm::consumeError(V.takeError());
    return error(D, Descriptor, "invalid SDK descriptor JSON");
  }
  const auto *O = V->getAsObject();
  int64_t Version = 0;
  if (!O || O->size() != 4 || O->getString("schema") != "neverc.cpp.sdk" ||
      !O->getInteger("version", Version) || Version != 1 ||
      O->getString("distribution_id") != CppMathSDKID)
    return error(D, Descriptor,
                 "descriptor must select exactly "
                 "schema/version/distribution_id/roots for the approved SDK");
  const auto *Roots = O->getObject("roots");
  if (!Roots || Roots->size() != 3)
    return error(
        D, Descriptor,
        "descriptor needs exactly libcxx, resource, and platform roots");
  for (llvm::StringRef Name : {"libcxx", "resource", "platform"}) {
    llvm::StringRef Path = Roots->getString(Name);
    if (!Path.data() || Path.empty() || Path.contains('\0') ||
        Path.contains('\n') || Path.contains('\r'))
      return error(D, Descriptor, "invalid SDK root path for " + Name.str());
    llvm::SmallString<256> Absolute;
    if (llvm::sys::path::is_absolute(Path))
      Absolute = Path;
    else {
      Absolute = llvm::sys::path::parent_path(Parsed.DescriptorPath);
      llvm::sys::path::append(Absolute, Path);
    }
    if (llvm::sys::fs::real_path(Absolute, Real) ||
        !llvm::sys::fs::is_directory(Real))
      return error(D, Descriptor,
                   "SDK root is not an accessible directory: " + Name.str());
    for (const auto &Other : Parsed.Roots)
      if (inside(Other.AbsolutePath, Real) || inside(Real, Other.AbsolutePath))
        return error(D, Descriptor,
                     "SDK roots overlap and would make provenance ambiguous");
    Parsed.Roots.push_back({Name.str(), Real.str().str()});
  }
  Parsed.DescriptorSHA256 = digest(Bytes);
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
  llvm::json::Object Roots;
  for (const auto &Root : C.Roots)
    Roots[Root.Name] = jsonString(Root.AbsolutePath);
  return llvm::json::Object{{"distribution_id", jsonString(C.DistributionID)},
                            {"catalog_sha256", jsonString(C.CatalogSHA256)},
                            {"roots", std::move(Roots)}};
}

bool verifyCppSdkDependencies(const CppSdkContext &C,
                              llvm::ArrayRef<SDKDependency> Dependencies,
                              Diagnostics &D) {
  if (C.DistributionID != CppMathSDKID || C.CatalogSHA256 != digest(Catalog))
    return error(D, "<sdk>",
                 "SDK context was not approved by this implementation",
                 "TR0103");
  std::string Bytes, Why;
  if (!readFile(C.DescriptorPath, MaxDescriptorBytes, Bytes, Why) ||
      digest(Bytes) != C.DescriptorSHA256)
    return error(D, C.DescriptorPath,
                 "SDK descriptor changed during translation");
  // Reconstruct the immutable catalog instead of trusting mutable caller-owned
  // ApprovedFiles as authority for a helper-supplied dependency.
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
