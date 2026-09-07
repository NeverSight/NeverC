#include "Frontend.h"
#include "SDKCatalog.h"
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

bool State::configureSDK(const json::Object &SDK) {
  auto fail = [&](llvm::StringRef Why) {
    diagnose("TR0203", "C++ SDK", Why,
             "Install the exact approved C++ SDK and configure its three "
             "canonical roots.");
    return false;
  };
  auto Parsed = json::parse(NeverCCppSDKCatalog);
  if (!Parsed) {
    llvm::consumeError(Parsed.takeError());
    return fail("The helper's compiled SDK catalog is invalid.");
  }
  const auto *Catalog = Parsed->getAsObject();
  auto Distribution = SDK.getString("distribution_id"),
       Hash = SDK.getString("catalog_sha256");
  const auto *Roots = SDK.getObject("roots");
  if (!Catalog || !Distribution || !Hash || !Roots || Roots->size() != 3 ||
      Catalog->getString("distribution_id") != Distribution ||
      *Hash != NeverCCppSDKCatalogHash)
    return fail("SDK request identity does not match the helper's compiled "
                "approved catalog.");
  SDKDistribution = Distribution->str();
  SDKCatalogHash = Hash->str();
  for (auto Name : {"libcxx", "resource", "platform"}) {
    auto Path = Roots->getString(Name);
    llvm::SmallString<256> Real;
    if (!Path || Path->contains('\0') || !llvm::sys::path::is_absolute(*Path) ||
        llvm::sys::fs::real_path(*Path, Real) ||
        !llvm::sys::fs::is_directory(Real) || Real != *Path)
      return fail("SDK roots must be canonical absolute existing directories.");
    auto P = Real.str().str();
    if (P == Root || llvm::StringRef(P).starts_with(Root + "/") ||
        llvm::StringRef(Root).starts_with(P + "/"))
      return fail(
          "SDK roots and the owned project root must not contain each other.");
    for (const auto &Existing : SDKRoots)
      if (P == Existing.second ||
          llvm::StringRef(P).starts_with(Existing.second + "/") ||
          llvm::StringRef(Existing.second).starts_with(P + "/"))
        return fail("SDK roots must be disjoint.");
    SDKRoots.emplace(Name, std::move(P));
  }
  const auto *Headers = Catalog->getArray("headers");
  if (!Headers || Headers->empty())
    return fail("Compiled catalog has no approved headers.");
  for (const auto &Entry : *Headers) {
    const auto *H = Entry.getAsObject();
    if (!H)
      return fail("Compiled catalog contains an invalid header entry.");
    auto R = H->getString("root"), P = H->getString("path"),
         Hsh = H->getString("sha256");
    if (!R || !P || !Hsh || !SDKRoots.count(R->str()) || !safeRelative(*P) ||
        Hsh->size() != 64 ||
        !SDKHeaders.emplace(std::make_pair(R->str(), P->str()), Hsh->str())
             .second)
      return fail(
          "Compiled catalog contains an invalid or duplicate header entry.");
  }
  // A missing header can change __has_include branches without an include
  // event.
  for (const auto &Entry : SDKHeaders) {
    auto Path = SDKRoots.at(Entry.first.first) + "/" + Entry.first.second;
    auto Identity = sdkFile(Path);
    auto Buffer = llvm::MemoryBuffer::getFile(Path);
    if (!Identity || Identity->Root != Entry.first.first ||
        Identity->Path != Entry.first.second || !Buffer ||
        digest((*Buffer)->getBuffer()) != Entry.second)
      return fail("Approved SDK distribution has a missing, relocated or "
                  "changed header.");
  }
  if (const auto *Metadata = Catalog->getArray("metadata")) {
    for (const auto &Entry : *Metadata) {
      const auto *M = Entry.getAsObject();
      if (!M)
        return fail("Compiled catalog contains invalid SDK metadata.");
      auto R = M->getString("root"), P = M->getString("path"),
           Hsh = M->getString("sha256");
      if (!R || !P || !Hsh || !SDKRoots.count(R->str()) || !safeRelative(*P))
        return fail("Invalid SDK metadata identity.");
      auto Buffer =
          llvm::MemoryBuffer::getFile(SDKRoots.at(R->str()) + "/" + P->str());
      if (!Buffer || digest((*Buffer)->getBuffer()) != *Hsh)
        return fail(
            "SDK metadata bytes differ from the approved distribution.");
    }
  }
  return true;
}

std::optional<SDKFile> State::sdkFile(llvm::StringRef Path) const {
  if (!math() || Path.empty() || Path.starts_with("<"))
    return std::nullopt;
  llvm::SmallString<256> Real;
  if (llvm::sys::fs::real_path(Path, Real))
    return std::nullopt;
  for (const auto &Root : SDKRoots) {
    auto Prefix = Root.second + "/";
    if (!Real.str().starts_with(Prefix))
      continue;
    auto Relative = Real.str().drop_front(Prefix.size()).str();
    auto Entry = SDKHeaders.find({Root.first, Relative});
    if (Entry == SDKHeaders.end())
      return std::nullopt;
    return SDKFile{Root.first, Relative, Entry->second};
  }
  return std::nullopt;
}

std::optional<SDKFile> State::sdkFile(const SourceManager &SM,
                                      SourceLocation L) const {
  return sdkFile(SM.getFilename(SM.getSpellingLoc(L)));
}

bool State::consumeSDKFile(const SourceManager &SM, FileID ID) {
  auto Entry = sdkFile(SM, SM.getLocForStartOfFile(ID));
  bool Invalid = false;
  auto Bytes = SM.getBufferData(ID, &Invalid);
  if (!Entry || Invalid || digest(Bytes) != Entry->SHA256) {
    diagnose("TR0203", "C++ SDK header",
             "Consumed SDK header bytes or identity differ from the compiled "
             "approved catalog.",
             "Restore the approved SDK distribution and regenerate the "
             "translation.");
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
