// CI-only inspection of upstream Clang source metadata. This is not used by the
// embedded translator and does not define the translator's admission policy.
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/ASTUnit.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/StringRef.h"
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
  bool VisitInitListExpr(InitListExpr *E) {
    llvm::outs() << "initializer semantic=" << E->isSemanticForm()
                 << " elements=" << E->getNumInits() << "\n";
    if (const auto *Written = E->getSyntacticForm()) {
      llvm::outs() << "  written initializer\n";
      Written->dump(llvm::outs(), Context);
    }
    if (const auto *Selected = E->getSemanticForm()) {
      llvm::outs() << "  selected initializer\n";
      Selected->dump(llvm::outs(), Context);
    }
    return true;
  }
};

int main(int Argc, const char **Argv) {
  struct Fixture {
    const char *Name;
    const char *Source;
  };
  const Fixture Sources[] = {
      {"nested-method",
      "template<class T>struct O{template<class U>struct I{T a;U b;int get()const;};};"
      "template<class X>template<class Y>int O<X>::I<Y>::get()const{return a+b;}"
      "int f(){O<int>::I<int>v{1,2};return v.get();}"},
      {"nested-static",
      "template<int N>struct O{template<int M>struct I{static int n;};};"
      "template<int X>template<int Y>int O<X>::I<Y>::n=X+Y;"
      "int*f(){return &O<2>::I<3>::n;}"},
      {"nested-pack",
      "template<int...N>struct O{template<int...M>struct I{static int n;};};"
      "template<int...X>template<int...Y>int O<X...>::I<Y...>::n=(0+...+X)+(0+...+Y);"
      "int*f(){return &O<1,2>::I<3,4>::n;}"},
      {"own-member-friend-function",
      "template<class T>struct O{template<class U>struct R{};};"
      "template<>template<class U>struct O<int>::R{"
      "U n=3;friend int get(const R&r){return r.n;}};"
      "int main(){O<int>::R<int>r;return get(r)-3;}"},
      {"own-member-friend-type",
      "class A;template<class T>struct O{template<class U>struct R{};};"
      "template<>template<class U>class O<int>::R{int n=3;friend U;};"
      "class A{public:static int get(const O<int>::R<A>&r){return r.n;}};"
      "int main(){O<int>::R<A>r;return A::get(r)-3;}"},
      {"visible-copied-friend",
      "template<class T>struct R{T n;"
      "template<class U>friend int get(const R&r,U n){return r.n+n;}};"
      "template<class U>int get(const R<int>&,U);"
      "int main(){R<int>r{1};auto p=&get<int>;return p(r,2);}"},
      {"callback-array-field",
      "template<int N>int get(){return N;}"
      "struct R{int(*p[2])();};"
      "int main(){R r{{get<2>,get<3>}};return r.p[1]();}"},
  };
  bool Found = false;
  bool Failed = false;
  for (const auto &Input : Sources) {
    if (Argc > 1 && llvm::StringRef(Argv[1]) != Input.Name)
      continue;
    Found = true;
    llvm::outs() << "fixture " << Input.Name << "\n";
    llvm::outs().flush();
    auto Unit = tooling::buildASTFromCodeWithArgs(Input.Source, {"-std=c++17"}, "input.cpp");
    if (!Unit) {
      Failed = true;
      continue;
    }
    Failed |= Unit->getDiagnostics().hasErrorOccurred();
    // Keep the recovered tree even when upstream Sema rejects a fixture.
    Unit->getASTContext().getTranslationUnitDecl()->dump(llvm::outs());
    Inspect Visitor(Unit->getASTContext());
    Visitor.TraverseDecl(Unit->getASTContext().getTranslationUnitDecl());
  }
  return !Found || Failed;
}
