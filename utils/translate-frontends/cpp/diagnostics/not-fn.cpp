// Upstream native AST evidence only; this grants no private frontend admission.
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace clang;

class NotFnVisitor : public RecursiveASTVisitor<NotFnVisitor> {
  ASTContext &Context;
  std::set<const FunctionDecl *> Functions;
  std::set<const CXXRecordDecl *> Records;
  unsigned Factories = 0, Calls = 0;
  unsigned ActiveClosure = 0;
  bool Overflow = false;

  bool sdk(const Decl *D) const {
    if (!D)
      return false;
    const auto &SM = Context.getSourceManager();
    const auto Path = SM.getFilename(SM.getSpellingLoc(D->getLocation()));
    return Path.contains("/neverc/lib/Translate/Cpp/SDK/libcxx/");
  }
  void record(const CXXRecordDecl *R, unsigned Depth = 0) {
    R = R ? R->getDefinition() : nullptr;
    if (!R || R->isDependentContext() || !sdk(R) ||
        !Records.insert(R->getCanonicalDecl()).second)
      return;
    if (Depth > 16 || Records.size() > 64) {
      Overflow = true;
      return;
    }
    auto &OS = llvm::outs();
    const auto &Layout = Context.getASTRecordLayout(R);
    OS << "NOT-FN-LAYOUT " << R->getQualifiedNameAsString()
       << " size=" << Layout.getSize().getQuantity()
       << " align=" << Layout.getAlignment().getQuantity()
       << " standard-layout=" << R->isStandardLayout()
       << " trivial-copy=" << R->isTriviallyCopyable() << "\n";
    R->dump(OS);
    for (const auto &Base : R->bases()) {
      const auto *B = Base.getType()->getAsCXXRecordDecl();
      OS << "BASE " << Base.getType().getAsString() << "\n";
      record(B, Depth + 1);
    }
    unsigned I = 0;
    for (const auto *Field : R->fields()) {
      OS << "FIELD " << Field->getNameAsString() << " "
         << Field->getType().getAsString() << " offset-bits="
         << Layout.getFieldOffset(I++) << "\n";
      record(Field->getType()->getAsCXXRecordDecl(), Depth + 1);
    }
  }
  void function(const FunctionDecl *F) {
    if (!F || !sdk(F) || !Functions.insert(F->getCanonicalDecl()).second)
      return;
    if (Functions.size() > 128) {
      Overflow = true;
      return;
    }
    auto &OS = llvm::outs();
    OS << "NOT-FN-SELECTED " << F->getQualifiedNameAsString() << "\n";
    F->dump(OS);
    const auto *D = F->getDefinition();
    if (!D)
      return;
    ++ActiveClosure;
    if (const auto *C = dyn_cast<CXXConstructorDecl>(D)) {
      for (const auto *Init : C->inits()) {
        OS << "CONSTRUCTOR-INITIALIZER\n";
        Init->getInit()->dump(OS, Context);
        TraverseStmt(const_cast<Expr *>(Init->getInit()));
      }
    }
    if (const auto *Body = D->getBody())
      TraverseStmt(const_cast<Stmt *>(Body));
    --ActiveClosure;
  }
public:
  explicit NotFnVisitor(ASTContext &Context) : Context(Context) {}
  bool VisitCallExpr(CallExpr *Call) {
    const auto *F = Call->getDirectCallee();
    if (!F || !sdk(F))
      return true;
    bool Root = F->getIdentifier() && F->getName() == "not_fn";
    if (Root) {
      ++Factories;
      llvm::outs() << "NOT-FN-FACTORY\n";
      record(Call->getType()->getAsCXXRecordDecl());
    }
    if (const auto *M = dyn_cast<CXXMethodDecl>(F))
      if (M->getOverloadedOperator() == OO_Call &&
          M->getParent()->getIdentifier() &&
          M->getParent()->getName() == "__perfect_forward_impl") {
        Root = true;
        ++Calls;
        llvm::outs() << "NOT-FN-CALL cv=" << M->getMethodQualifiers().getAsString()
                     << " ref=" << unsigned(M->getRefQualifier()) << "\n";
      }
    // Header declarations contain unrelated nondependent calls. Only an
    // actual adapter root and its selected body closure belong in this probe.
    if (Root || ActiveClosure)
      function(F);
    return true;
  }
  bool VisitCXXConstructExpr(CXXConstructExpr *C) {
    const auto *R = C->getType()->getAsCXXRecordDecl();
    if (ActiveClosure || (R && R->getIdentifier() && R->getName() == "__not_fn_t"))
      function(C->getConstructor());
    return true;
  }
  bool VisitCXXInheritedCtorInitExpr(CXXInheritedCtorInitExpr *C) {
    if (ActiveClosure) {
      llvm::outs() << "NOT-FN-INHERITED-CONSTRUCTOR\n";
      function(C->getConstructor());
    }
    return true;
  }
  void finish() {
    llvm::outs() << "NOT-FN-COUNTS factories=" << Factories << " calls=" << Calls
                 << " functions=" << Functions.size() << " records=" << Records.size()
                 << " overflow=" << Overflow << "\n";
  }
};

class NotFnConsumer : public ASTConsumer {
public:
  void HandleTranslationUnit(ASTContext &Context) override {
    NotFnVisitor Visitor(Context);
    Visitor.TraverseDecl(Context.getTranslationUnitDecl());
    Visitor.finish();
  }
};
class NotFnAction : public ASTFrontendAction {
public:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &, llvm::StringRef) override {
    return std::make_unique<NotFnConsumer>();
  }
};
int main(int argc, char **argv) {
  if (argc != 3)
    return 2;
  auto Source = llvm::MemoryBuffer::getFile(argv[2]);
  if (!Source)
    return 3;
  const std::string SDK = std::string(argv[1]) + "/neverc/lib/Translate/Cpp/SDK/";
  std::vector<std::string> Arguments{
      "--no-default-config", "-std=c++17", "-stdlib=libc++", "-nostdinc++",
      "-resource-dir", SDK + "resource", "-isystem", SDK + "libcxx",
      "-D_LIBCPP_REMOVE_TRANSITIVE_INCLUDES", "-D_NEVERC_CPP_CORE_V2_SDK",
      "-D_LIBCPP___ITERATOR_ISTREAM_ITERATOR_H", "-D_LIBCPP___ITERATOR_ISTREAMBUF_ITERATOR_H",
      "-D_LIBCPP___ITERATOR_OSTREAM_ITERATOR_H", "-D_LIBCPP___ITERATOR_OSTREAMBUF_ITERATOR_H",
      "-D_LIBCPP_DISABLE_VISIBILITY_ANNOTATIONS"};
  return tooling::runToolOnCodeWithArgs(std::make_unique<NotFnAction>(),
      (*Source)->getBuffer(), Arguments, argv[2], "/usr/bin/clang++-20") ? 0 : 1;
}
