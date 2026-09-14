// CI-only inspection of upstream Clang source metadata. This is not used by the
// embedded translator and does not define the translator's admission policy.
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/ASTUnit.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;

class Inspect : public RecursiveASTVisitor<Inspect> {
  ASTContext &Context;

  void declaration(const Decl *D) {
    if (!D) {
      llvm::outs() << "none";
      return;
    }
    llvm::outs() << D->getDeclKindName();
    if (const auto *N = dyn_cast<NamedDecl>(D))
      llvm::outs() << " " << N->getQualifiedNameAsString();
    if (const auto *R = dyn_cast<CXXRecordDecl>(D)) {
      llvm::outs() << " dependent=" << R->isDependentContext();
      if (const auto *S = dyn_cast<ClassTemplateSpecializationDecl>(R)) {
        llvm::outs() << " args=";
        for (const auto &A : S->getTemplateArgs().asArray()) {
          A.print(Context.getPrintingPolicy(), llvm::outs(), true);
          llvm::outs() << ";";
        }
      }
    }
  }
  void qualifier(NestedNameSpecifierLoc Q) {
    for (; Q; Q = Q.getPrefix()) {
      auto T = Q.getTypeLoc();
      if (!T)
        continue;
      llvm::outs() << "  qualifier " << T.getType().getAsString()
                   << " canonical=" << T.getType().getCanonicalType().getAsString()
                   << " dependent=" << T.getType()->isDependentType()
                   << " instantiation-dependent="
                   << T.getType()->isInstantiationDependentType() << "\n";
    }
  }

public:
  explicit Inspect(ASTContext &C) : Context(C) {}
  bool shouldVisitTemplateInstantiations() const { return true; }
  bool VisitFunctionDecl(FunctionDecl *D) {
    if (D->isImplicit())
      return true;
    llvm::outs() << "function ";
    declaration(D);
    llvm::outs() << " dependent=" << D->isDependentContext()
                 << " templated-kind=" << D->getTemplatedKind() << " parent=";
    declaration(Decl::castFromDeclContext(D->getDeclContext()));
    llvm::outs() << "\n";
    qualifier(D->getQualifierLoc());
    return true;
  }
  bool VisitVarDecl(VarDecl *D) {
    if (!D->isStaticDataMember())
      return true;
    llvm::outs() << "static ";
    declaration(D);
    llvm::outs() << " parent=";
    declaration(Decl::castFromDeclContext(D->getDeclContext()));
    llvm::outs() << "\n";
    qualifier(D->getQualifierLoc());
    return true;
  }
  bool VisitSubstNonTypeTemplateParmExpr(SubstNonTypeTemplateParmExpr *E) {
    llvm::outs() << "  substitution index=" << E->getIndex() << " associated=";
    declaration(E->getAssociatedDecl());
    llvm::outs() << " replacement=";
    E->getReplacement()->printPretty(llvm::outs(), nullptr, Context.getPrintingPolicy());
    llvm::outs() << "\n";
    return true;
  }
};

int main() {
  const char *Sources[] = {
      "template<class T>struct O{template<class U>struct I{T a;U b;int get()const;};};"
      "template<class X>template<class Y>int O<X>::I<Y>::get()const{return a+b;}"
      "int f(){O<int>::I<int>v{1,2};return v.get();}",
      "template<int N>struct O{template<int M>struct I{static int n;};};"
      "template<int X>template<int Y>int O<X>::I<Y>::n=X+Y;"
      "int*f(){return &O<2>::I<3>::n;}",
      "template<int...N>struct O{template<int...M>struct I{static int n;};};"
      "template<int...X>template<int...Y>int O<X...>::I<Y...>::n=(0+...+X)+(0+...+Y);"
      "int*f(){return &O<1,2>::I<3,4>::n;}",
  };
  for (unsigned I = 0; I < 3; ++I) {
    llvm::outs() << "fixture " << I << "\n";
    auto Unit = tooling::buildASTFromCodeWithArgs(Sources[I], {"-std=c++17"}, "input.cpp");
    if (!Unit || Unit->getDiagnostics().hasErrorOccurred())
      return 1;
    Inspect Visitor(Unit->getASTContext());
    Visitor.TraverseDecl(Unit->getASTContext().getTranslationUnitDecl());
  }
}
