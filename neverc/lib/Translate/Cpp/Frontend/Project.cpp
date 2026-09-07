#include "Frontend.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Index/USRGeneration.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include <algorithm>
#include <cstdlib>

using namespace clang;
namespace nct {

std::string State::relativePath(llvm::StringRef Path) const {
  if (Path.empty() || Path.starts_with("<"))
    return {};
  auto Cached = PathCache.find(Path.str());
  if (Cached != PathCache.end())
    return Cached->second;
  llvm::SmallString<256> Real;
  if (llvm::sys::fs::real_path(Path, Real))
    return {};
  const auto Canonical = llvm::sys::path::convert_to_slash(Real);
  auto Prefix = llvm::StringRef(Root).ends_with("/") ? Root : Root + "/";
  auto Relative = llvm::StringRef(Canonical).starts_with(Prefix)
                      ? llvm::StringRef(Canonical).drop_front(Prefix.size()).str()
                      : "";
  if (Relative.find_first_of("\\:\r\n") != std::string::npos)
    Relative.clear();
  PathCache.emplace(Path.str(), Relative);
  return Relative;
}

std::string State::sourcePath(const SourceManager &SM, SourceLocation L) const {
  L = SM.getExpansionLoc(L);
  if (L.isInvalid())
    return {};
  return relativePath(SM.getFilename(L));
}

bool State::owns(const SourceManager &SM, SourceLocation L) const {
  return project() ? !sourcePath(SM, L).empty()
                   : SM.isWrittenInMainFile(SM.getExpansionLoc(L));
}

bool isolateProjectEnvironment() {
  // The internal frontend runs in a separate, single-request NeverC process.
  // Environment changes are local to it and precede Clang's driver and threads.
  const char *Variables[] = {"CPATH",
                             "C_INCLUDE_PATH",
                             "CPLUS_INCLUDE_PATH",
                             "OBJC_INCLUDE_PATH",
                             "OBJCPLUS_INCLUDE_PATH",
                             "SDKROOT",
                             "MACOSX_DEPLOYMENT_TARGET",
                             "IPHONEOS_DEPLOYMENT_TARGET",
                             "TVOS_DEPLOYMENT_TARGET",
                             "WATCHOS_DEPLOYMENT_TARGET",
                             "CLANG_CONFIG_FILE_SYSTEM_DIR",
                             "CLANG_CONFIG_FILE_USER_DIR",
                             "CCC_OVERRIDE_OPTIONS",
                             "GCC_EXEC_PREFIX",
                             "COMPILER_PATH",
                             "LIBRARY_PATH",
                             "INCLUDE"};
  for (const auto *Name : Variables) {
#ifdef _WIN32
    if (_putenv_s(Name, "") != 0)
      return false;
#else
    if (unsetenv(Name) != 0)
      return false;
#endif
  }
  return true;
}

static const FunctionDecl *containingFunction(const Decl *D) {
  for (const DeclContext *C = D->getDeclContext(); C; C = C->getParent())
    if (const auto *F = dyn_cast<FunctionDecl>(C))
      return F;
  return nullptr;
}

std::string Adapter::ownerTU(const NamedDecl *D) const {
  if (const auto *F = containingFunction(D))
    return F->getFormalLinkage() == Linkage::Internal ? S.Relative : "";
  if (const auto *Field = dyn_cast<FieldDecl>(D))
    return ownerTU(Field->getParent());
  return D->getFormalLinkage() == Linkage::Internal || !D->hasLinkage()
             ? S.Relative
             : "";
}

std::string Adapter::identity(const NamedDecl *D) {
  if (S.project()) {
    if (const auto *F = dyn_cast<FieldDecl>(D))
      return identity(F->getParent()) + ":field:" + F->getNameAsString();
    if (const auto *P = dyn_cast<ParmVarDecl>(D)) {
      const auto *F = dyn_cast<FunctionDecl>(P->getDeclContext());
      if (F) {
        unsigned Index = 0;
        for (const auto *Candidate : F->parameters()) {
          if (Candidate == P)
            break;
          ++Index;
        }
        return identity(F) + ":parameter:" + std::to_string(Index);
      }
    }
    if (const auto *F = containingFunction(D)) {
      auto L = Sources.getExpansionLoc(D->getLocation());
      return identity(F) + ":local:" + S.sourcePath(Sources, L) + ":" +
             std::to_string(Sources.getFileOffset(L));
    }
  }
  llvm::SmallString<128> USR;
  if (index::generateUSRForDecl(D->getCanonicalDecl(), USR)) {
    reject(D->getLocation(), D->getDeclKindName(),
           "No stable semantic declaration identity.");
    throw Failure{};
  }
  auto Identity = USR.str().str();
  if (S.project()) {
    auto Owner = ownerTU(D);
    if (!Owner.empty())
      Identity = Owner + ":" + S.sourcePath(Sources, D->getLocation()) + ":" +
                 Identity;
  } else if (D->getFormalLinkage() == Linkage::Internal || !D->hasLinkage()) {
    Identity = S.Relative + ":" + Identity;
  }
  return Identity;
}

class BindingEvidence : public RecursiveASTVisitor<BindingEvidence> {
  Adapter &A;
  std::string Bytes;
  void add(llvm::StringRef Value) {
    Bytes += std::to_string(Value.size()) + ":" + Value.str() + ";";
  }

public:
  explicit BindingEvidence(Adapter &A) : A(A) {}
  bool VisitNamedDecl(NamedDecl *D) {
    if (D->isImplicit())
      return true;
    add(D->getDeclKindName());
    add(A.identity(D));
    if (const auto *V = dyn_cast<ValueDecl>(D)) {
      if (const auto *F = dyn_cast<FunctionDecl>(D))
        add(A.type(F->getReturnType(), D->getLocation(), true));
      else
        add(A.type(V->getType(), D->getLocation()));
    }
    return true;
  }
  bool VisitStmt(Stmt *S) {
    add(S->getStmtClassName());
    if (const auto *E = dyn_cast<Expr>(S)) {
      const auto *C = dyn_cast<ImplicitCastExpr>(E);
      if ((!C || C->getCastKind() != CK_FunctionToPointerDecay) &&
          !E->getType()->isFunctionType())
        add(A.type(E->getType(), E->getExprLoc(), true));
    }
    return true;
  }
  bool VisitDeclRefExpr(DeclRefExpr *E) {
    add(A.identity(E->getDecl()));
    return true;
  }
  bool VisitMemberExpr(MemberExpr *E) {
    add(A.identity(E->getMemberDecl()));
    return true;
  }
  bool VisitCallExpr(CallExpr *E) {
    if (const auto *F = E->getDirectCallee())
      add(A.identity(F));
    return true;
  }
  bool VisitIntegerLiteral(IntegerLiteral *E) {
    llvm::SmallString<32> Text;
    E->getValue().toString(Text, 10, !E->getType()->isUnsignedIntegerType());
    add(Text);
    return true;
  }
  bool VisitCXXBoolLiteralExpr(CXXBoolLiteralExpr *E) {
    add(E->getValue() ? "true" : "false");
    return true;
  }
  bool VisitFloatingLiteral(FloatingLiteral *E) {
    llvm::SmallString<32> Bits;
    E->getValue().bitcastToAPInt().toStringUnsigned(Bits, 16);
    add(Bits);
    return true;
  }
  bool VisitBinaryOperator(BinaryOperator *E) {
    add(E->getOpcodeStr());
    return true;
  }
  bool VisitUnaryOperator(UnaryOperator *E) {
    add(UnaryOperator::getOpcodeStr(E->getOpcode()));
    add(E->isPostfix() ? "post" : "pre");
    return true;
  }
  bool VisitCastExpr(CastExpr *E) {
    add(E->getCastKindName());
    return true;
  }
  std::string finish() const { return digest(Bytes); }
};

json::Object Adapter::evidence(const NamedDecl *D, llvm::StringRef Kind) {
  auto Range = Lexer::makeFileCharRange(
      CharSourceRange::getTokenRange(D->getSourceRange()), Sources,
      Context.getLangOpts());
  if (Range.isInvalid() || Sources.getFileID(Range.getBegin()) !=
                               Sources.getFileID(Range.getEnd())) {
    reject(D->getLocation(), "ODR evidence",
           "Definition spans unsupported macro/file boundaries.");
    throw Failure{};
  }
  auto FID = Sources.getFileID(Range.getBegin());
  auto Begin = Sources.getFileOffset(Range.getBegin());
  auto End = Sources.getFileOffset(Range.getEnd());
  std::string Tokens;
  for (const auto &T : S.ExpandedTokens[FID.getHashValue()])
    if (T.Offset >= Begin && T.Offset < End)
      Tokens += T.Bytes;
  if (Tokens.empty()) {
    reject(D->getLocation(), "ODR evidence",
           "Definition has no captured expanded token evidence.");
    throw Failure{};
  }
  BindingEvidence Bindings(*this);
  Bindings.TraverseDecl(const_cast<NamedDecl *>(D));
  bool Inline = false;
  if (const auto *F = dyn_cast<FunctionDecl>(D))
    Inline = F->isInlined();
  if (const auto *G = dyn_cast<VarDecl>(D))
    Inline = G->isInline();
  return json::Object{{"kind", Kind.str()},
                      {"name", name(D)},
                      {"semantic_id", digest(identity(D))},
                      {"owner_tu", ownerTU(D)},
                      {"inline", Inline},
                      {"origin", loc(D->getLocation())},
                      {"tokens_sha256", digest(Tokens)},
                      {"bindings_sha256", Bindings.finish()}};
}

void Adapter::addProjectMetadata() {
  json::Array FunctionData, GlobalData, ODR;
  std::vector<FunctionDecl *> FunctionOrder;
  for (auto &P : FunctionDeclarations)
    FunctionOrder.push_back(P.second);
  std::sort(FunctionOrder.begin(), FunctionOrder.end(),
            [&](const auto *L, const auto *R) { return name(L) < name(R); });
  for (const auto *F : FunctionOrder) {
    json::Array Params;
    auto Prefix = "nct_f" + digest(name(F)).substr(0, 12) + "_p";
    unsigned Index = 0;
    for (const auto *P : F->parameters())
      Params.push_back(
          json::Object{{"name", Prefix + std::to_string(++Index)},
                       {"type", type(P->getType(), P->getLocation())},
                       {"loc", loc(P->getLocation())}});
    FunctionData.push_back(json::Object{
        {"name", name(F)},
        {"semantic_id", digest(identity(F))},
        {"result", type(F->getReturnType(), F->getLocation(), true)},
        {"params", std::move(Params)},
        {"internal", F->getFormalLinkage() == Linkage::Internal},
        {"c_export",
         F->isExternC() && F->getFormalLinkage() != Linkage::Internal},
        {"inline", F->isInlined()},
        {"loc", loc(F->getLocation())}});
  }
  std::vector<VarDecl *> GlobalOrder;
  for (auto &P : GlobalDeclarations)
    GlobalOrder.push_back(P.second);
  std::sort(GlobalOrder.begin(), GlobalOrder.end(),
            [&](const auto *L, const auto *R) { return name(L) < name(R); });
  for (const auto *G : GlobalOrder)
    GlobalData.push_back(
        json::Object{{"name", name(G)},
                     {"semantic_id", digest(identity(G))},
                     {"type", type(G->getType(), G->getLocation())},
                     {"internal", G->getFormalLinkage() == Linkage::Internal},
                     {"loc", loc(G->getLocation())}});
  for (const auto *R : Records)
    ODR.push_back(evidence(R, "record"));
  for (const auto *F : Functions)
    ODR.push_back(evidence(F, "function"));
  for (const auto *G : Globals)
    ODR.push_back(evidence(G, "global"));
  S.Module["project_schema"] = 1;
  S.Module["translation_unit"] = S.Relative;
  S.Module["configuration_id"] = S.ConfigurationID;
  S.Module["function_declarations"] = std::move(FunctionData);
  S.Module["global_declarations"] = std::move(GlobalData);
  S.Module["odr"] = std::move(ODR);
}
} // namespace nct
