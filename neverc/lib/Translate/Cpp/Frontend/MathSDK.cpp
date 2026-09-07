#include "Frontend.h"
#include "BuiltinCppSdkData.h"
#include "clang/AST/Expr.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Index/USRGeneration.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include <set>

using namespace clang;
namespace nct {

static bool safeRelative(llvm::StringRef P) {
  if (P.empty() || P.starts_with("/") || P.ends_with("/") || P.contains('\\') ||
      P.contains(':') || P.contains('\0') || P.contains('\n') ||
      P.contains('\r'))
    return false;
  while (!P.empty()) {
    auto Part = P.split('/');
    if (Part.first.empty() || Part.first == "." || Part.first == "..")
      return false;
    P = Part.second;
  }
  return true;
}

static std::string virtualPath(llvm::StringRef Path) {
  llvm::SmallString<256> Result(llvm::sys::path::convert_to_slash(Path));
  llvm::sys::path::remove_dots(Result, true, llvm::sys::path::Style::posix);
  return Result.str().str();
}

static std::string virtualRoot() {
#ifdef _WIN32
  return "Z:/__neverc_cpp_builtin_sdk__/v1";
#else
  return "/__neverc_cpp_builtin_sdk__/v1";
#endif
}

bool State::configureSDK(const json::Object &SDK) {
  auto fail = [&](llvm::StringRef Why) {
    diagnose("TR0203", "C++ SDK", Why,
             "Use the approved embedded C++ SDK in a compatible NeverC build; "
             "external SDK roots are not supported.");
    return false;
  };
  if (digest(neverc_cpp_sdk::CatalogJSON) != neverc_cpp_sdk::CatalogSHA256)
    return fail("The embedded SDK catalog bytes do not match their identity.");
  auto Parsed = json::parse(neverc_cpp_sdk::CatalogJSON);
  if (!Parsed) {
    llvm::consumeError(Parsed.takeError());
    return fail("The frontend's embedded SDK catalog is invalid.");
  }
  const auto *Catalog = Parsed->getAsObject();
  auto Distribution = SDK.getString("distribution_id"),
       Hash = SDK.getString("catalog_sha256");
  if (!Catalog || SDK.size() != 2 || !Distribution || !Hash ||
      Catalog->getString("distribution_id") != Distribution ||
      *Hash != neverc_cpp_sdk::CatalogSHA256)
    return fail("SDK request identity does not match the embedded approved "
                "catalog, or contains an external SDK override.");
  SDKDistribution = Distribution->str();
  SDKCatalogHash = Hash->str();
  const auto Virtual = virtualRoot();
  const auto Owned = virtualPath(Root);
  const auto OwnedPrefix =
      llvm::StringRef(Owned).ends_with("/") ? Owned : Owned + "/";
  if (Owned == Virtual || llvm::StringRef(Owned).starts_with(Virtual + "/") ||
      llvm::StringRef(Virtual).starts_with(OwnedPrefix) ||
      llvm::sys::fs::exists(Virtual))
    return fail("The reserved embedded SDK root overlaps owned source or a "
                "physical filesystem entry.");
  for (auto Name : {"libcxx", "resource", "platform"})
    SDKRoots.emplace(Name, Virtual + "/" + Name);

  using Key = std::pair<std::string, std::string>;
  std::map<Key, std::pair<std::string, bool>> Expected;
  for (auto Name : {"headers", "metadata"}) {
    const auto *Entries = Catalog->getArray(Name);
    if (!Entries || Entries->empty())
      return fail("The embedded catalog has no approved headers or metadata.");
    const bool Metadata = llvm::StringRef(Name) == "metadata";
    for (const auto &Entry : *Entries) {
      const auto *F = Entry.getAsObject();
      if (!F)
        return fail("The embedded catalog contains an invalid file entry.");
      auto R = F->getString("root"), P = F->getString("path"),
           H = F->getString("sha256");
      if (!R || !P || !H || !SDKRoots.count(R->str()) || !safeRelative(*P) ||
          H->size() != 64 ||
          !Expected.emplace(Key(R->str(), P->str()),
                            std::make_pair(H->str(), Metadata)).second)
        return fail("The embedded catalog contains invalid or duplicate files.");
      if (!Metadata)
        SDKHeaders.emplace(Key(R->str(), P->str()), H->str());
    }
  }
  if (Expected.size() != neverc_cpp_sdk::FileCount)
    return fail("The embedded SDK file inventory does not match its catalog.");
  for (const auto &File : neverc_cpp_sdk::Files) {
    auto Entry = Expected.find({File.Root, File.Path});
    if (Entry == Expected.end() || File.Size > 4 * 1024 * 1024 ||
        Entry->second.first != File.SHA256 ||
        Entry->second.second != File.Metadata ||
        digest(llvm::StringRef(File.Contents, File.Size)) != File.SHA256)
      return fail("Embedded SDK bytes or file identities differ from the "
                  "approved catalog.");
    Expected.erase(Entry);
  }
  return Expected.empty();
}

llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> State::createFileSystem() {
  auto fail = [&]() -> llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> {
    diagnose("TR0203", "frontend filesystem",
             "Cannot create the isolated source and embedded SDK filesystem.",
             "Use readable owned sources and a compatible NeverC build.");
    return nullptr;
  };
  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> Physical(
      llvm::vfs::createPhysicalFileSystem().release());
  if (Physical->setCurrentWorkingDirectory(WorkingDirectory))
    return fail();
  if (!math())
    return Physical;
  auto Memory = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  if (Memory->setCurrentWorkingDirectory(WorkingDirectory))
    return fail();
  SDKVirtualFiles.clear();
  for (const auto &File : neverc_cpp_sdk::Files) {
    const auto Path = SDKRoots.at(File.Root) + "/" + File.Path;
    if (!Memory->addFile(Path, 0, llvm::MemoryBuffer::getMemBufferCopy(
                                    llvm::StringRef(File.Contents, File.Size),
                                    Path)))
      return fail();
    auto Status = Memory->status(Path);
    if (!Status)
      return fail();
    SDKVirtualFiles.emplace(Path, Status->getUniqueID());
  }
  auto Overlay = llvm::makeIntrusiveRefCnt<llvm::vfs::OverlayFileSystem>(Physical);
  Overlay->pushOverlay(Memory);
  return Overlay;
}

std::optional<SDKFile> State::sdkFile(llvm::StringRef Path) const {
  if (!math() || Path.empty() || Path.starts_with("<"))
    return std::nullopt;
  auto Name = virtualPath(Path);
  for (const auto &Root : SDKRoots) {
    auto Prefix = Root.second + "/";
    if (!llvm::StringRef(Name).starts_with(Prefix))
      continue;
    auto Relative = llvm::StringRef(Name).drop_front(Prefix.size()).str();
    auto Entry = SDKHeaders.find({Root.first, Relative});
    if (Entry == SDKHeaders.end())
      return std::nullopt;
    return SDKFile{Root.first, Relative, Entry->second};
  }
  return std::nullopt;
}

std::optional<SDKFile> State::sdkFile(FileEntryRef File) const {
  auto Path = virtualPath(File.getName());
  auto Identity = SDKVirtualFiles.find(Path);
  if (Identity == SDKVirtualFiles.end() ||
      Identity->second != File.getUniqueID())
    return std::nullopt;
  return sdkFile(Path);
}

std::optional<SDKFile> State::sdkFile(const SourceManager &SM,
                                      SourceLocation L) const {
  auto File = SM.getFileEntryRefForID(SM.getFileID(SM.getSpellingLoc(L)));
  return File ? sdkFile(*File) : std::nullopt;
}

bool State::consumeSDKFile(const SourceManager &SM, FileID ID) {
  auto Entry = sdkFile(SM, SM.getLocForStartOfFile(ID));
  bool Invalid = false;
  auto Bytes = SM.getBufferData(ID, &Invalid);
  if (!Entry || Invalid || digest(Bytes) != Entry->SHA256) {
    diagnose("TR0203", "C++ SDK header",
             "Consumed SDK header bytes or filesystem identity differ from "
             "the embedded approved catalog.",
             "Use the immutable SDK embedded in a compatible NeverC build.");
    return false;
  }
  SDKDependencies[{Entry->Root, Entry->Path}] = Entry->SHA256;
  return true;
}

void State::addSDKMetadata() {
  Module["fp_contract"] = "cpp.math.binary64.masked.v1";
  Module["sdk_distribution_id"] = SDKDistribution;
  Module["sdk_catalog_sha256"] = SDKCatalogHash;
  json::Array Deps;
  for (const auto &Entry : SDKDependencies)
    Deps.push_back(json::Object{{"root", Entry.first.first},
                                {"path", Entry.first.second},
                                {"sha256", Entry.second}});
  Module["sdk_dependencies"] = std::move(Deps);
}

json::Object Adapter::floatingLiteral(const llvm::APFloat &V,
                                      SourceLocation L) {
  if (&V.getSemantics() != &llvm::APFloat::IEEEdouble()) {
    reject(L, "floating literal",
           "Only IEEE binary64 literal semantics are admitted.");
    throw Failure{};
  }
  auto Bits = llvm::utohexstr(V.bitcastToAPInt().getZExtValue(), true);
  Bits.insert(Bits.begin(), 16 - Bits.size(), '0');
  return json::Object{
      {"kind", "literal"}, {"type", "double"}, {"bits", Bits}, {"loc", loc(L)}};
}

static bool standardNamespace(const DeclContext *C) {
  while (const auto *N = dyn_cast_or_null<NamespaceDecl>(C)) {
    if (N->isStdNamespace())
      return true;
    if (!N->isInline())
      return false;
    C = N->getParent();
  }
  return false;
}

std::string Adapter::mapping(const CallExpr *Call) {
  if (!S.math())
    return {};
  const auto *F = Call->getDirectCallee();
  if (!F || F->getNumParams() != 1 || Call->getNumArgs() != 1 ||
      F->isVariadic() ||
      F->getTemplatedKind() != FunctionDecl::TK_NonTemplate ||
      !F->isExternC() ||
      !F->getReturnType()->isSpecificBuiltinType(BuiltinType::Double) ||
      !F->getParamDecl(0)->getType()->isSpecificBuiltinType(
          BuiltinType::Double))
    return {};
  auto Spelling = F->getName();
  if (Spelling != "fabs" && Spelling != "floor")
    return {};
  const auto *Reference =
      dyn_cast<DeclRefExpr>(Call->getCallee()->IgnoreParenImpCasts());
  if (!Reference || !Reference->getQualifier() ||
      !Reference->getQualifier()->getAsNamespace() ||
      !Reference->getQualifier()->getAsNamespace()->isStdNamespace())
    return {};
  const auto *Shadow = dyn_cast<UsingShadowDecl>(Reference->getFoundDecl());
  if (!Shadow || !standardNamespace(Shadow->getDeclContext()))
    return {};
  const auto *Introducer = Shadow->getIntroducer();
  if (Introducer->getLocation().isMacroID())
    return {};
  auto Import = S.sdkFile(Sources, Introducer->getLocation());
  if (!Import || Import->Root != "libcxx" || Import->Path != "cmath" ||
      !S.SDKDependencies.count({Import->Root, Import->Path}))
    return {};
  const NamedDecl *Target = Shadow->getTargetDecl();
  while (const auto *Next = dyn_cast<UsingShadowDecl>(Target))
    Target = Next->getTargetDecl();
  if (Target->getCanonicalDecl() != F->getCanonicalDecl())
    return {};
  std::optional<SDKFile> Origin;
  SourceLocation OriginLocation;
  for (const auto *Redeclaration : F->redecls()) {
    if (Redeclaration->isImplicit())
      continue;
    if (Redeclaration->getLocation().isMacroID())
      return {};
    auto File = S.sdkFile(Sources, Redeclaration->getLocation());
    if (!File || File->Root != "platform" ||
        File->Path != "usr/include/math.h" ||
        !S.SDKDependencies.count({File->Root, File->Path}))
      return {};
    Origin = File;
    OriginLocation = Redeclaration->getLocation();
  }
  if (!Origin)
    return {};
  if (Sources.getSpellingLineNumber(OriginLocation) !=
          (Spelling == "fabs" ? 423u : 466u) ||
      Sources.getSpellingColumnNumber(OriginLocation) != 15u)
    return {};
  llvm::SmallString<128> USR;
  if (index::generateUSRForDecl(F->getCanonicalDecl(), USR))
    return {};
  auto DeclarationID =
      digest(S.SDKDistribution + "\n" + Origin->Root + "\n" + Origin->Path +
             "\n" + USR.str().str() + "\ndouble(double)");
  auto ID = "cpp.math." + Spelling.str() + ".f64.v1";
  auto L = Sources.getSpellingLoc(OriginLocation);
  MappedFunctions[ID] = json::Object{
      {"id", ID},
      {"declaration_id", DeclarationID},
      {"result", "double"},
      {"parameters", json::Array{"double"}},
      {"origin", json::Object{{"root", Origin->Root},
                              {"path", Origin->Path},
                              {"sha256", Origin->SHA256},
                              {"line", Sources.getSpellingLineNumber(L)},
                              {"column", Sources.getSpellingColumnNumber(L)}}}};
  return ID;
}
} // namespace nct
