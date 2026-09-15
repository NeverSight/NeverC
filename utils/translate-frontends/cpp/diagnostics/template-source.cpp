// CI-only inspection of upstream Clang source metadata. This is not used by the
// embedded translator and does not define the translator's admission policy.
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/ASTUnit.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Signals.h"
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
    if (auto *Primary = D->getPrimaryTemplate()) {
      llvm::outs() << "  body pattern ";
      declaration(D->getTemplateInstantiationPattern());
      llvm::outs() << "\n";
      for (const auto *Redecl : Primary->redecls()) {
        const auto *Template = cast<FunctionTemplateDecl>(Redecl);
        const auto *Function = Template->getTemplatedDecl();
        llvm::outs() << "  primary redeclaration compatible="
                     << Template->isCompatibleWithDefinition() << " lexical=";
        declaration(Decl::castFromDeclContext(Function->getLexicalDeclContext()));
        llvm::outs() << " definition-lexical=";
        const auto *Definition = Function->getDefinition();
        declaration(Definition ? Decl::castFromDeclContext(Definition->getLexicalDeclContext()) : nullptr);
        llvm::outs() << "\n";
      }
    }
    return true;
  }
  bool VisitFriendDecl(FriendDecl *D) {
    const auto *Template = dyn_cast_or_null<ClassTemplateDecl>(D->getFriendDecl());
    if (!Template)
      return true;
    llvm::outs() << "friend class target=";
    declaration(Template);
    llvm::outs() << " owner=";
    declaration(Decl::castFromDeclContext(D->getDeclContext()));
    llvm::outs() << " target-lexical=";
    declaration(Decl::castFromDeclContext(Template->getLexicalDeclContext()));
    llvm::outs() << " record-previous=";
    declaration(Template->getTemplatedDecl()->getPreviousDecl());
    llvm::outs() << " template-previous=";
    declaration(Template->getPreviousDecl());
    llvm::outs() << " friend-kind=" << Template->getFriendObjectKind()
                 << " unsupported=" << D->isUnsupportedFriend()
                 << " parameter-lists=" << Template->getTemplatedDecl()->getNumTemplateParameterLists()
                 << "\n";
    qualifier(Template->getTemplatedDecl()->getQualifierLoc());
    return true;
  }
  bool VisitMaterializeTemporaryExpr(MaterializeTemporaryExpr *E) {
    if (E->getStorageDuration() != SD_Static)
      return true;
    llvm::outs() << "static temporary node=" << static_cast<const void *>(E)
                 << " type=" << E->getType().getAsString()
                 << " operand=" << E->getSubExpr()->getType().getAsString() << " owner=";
    declaration(E->getExtendingDecl());
    if (auto *Value = E->getOrCreateValue(false)) {
      llvm::outs() << " value=";
      Value->printPretty(llvm::outs(), Context, E->getType());
    }
    llvm::outs() << "\n";
    return true;
  }
  bool VisitVarDecl(VarDecl *D) {
    if (D->hasGlobalStorage() && !D->getDeclContext()->isDependentContext() &&
        D->hasInit() && (D->getType()->isReferenceType() || D->getType()->isPointerType() ||
                         D->getType()->isRecordType() || D->getType()->isArrayType())) {
      APValue Value;
      llvm::SmallVector<PartialDiagnosticAt, 8> Notes;
      const bool NativeConstant = D->hasConstantInitialization();
      bool Constant = D->getInit()->EvaluateAsInitializer(Value, Context, D, Notes, true);
      llvm::outs() << "constant binding " << D->getNameAsString()
                   << " native-constant=" << NativeConstant
                   << " success=" << Constant << " notes=" << Notes.size();
      if (Value.isLValue())
        llvm::outs() << " call=" << Value.getLValueCallIndex()
                     << " version=" << Value.getLValueVersion();
      if (Value.hasValue()) {
        llvm::outs() << " value=";
        Value.printPretty(llvm::outs(), Context, D->getType());
      }
      llvm::outs() << "\n";
    }
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
  bool VisitCXXNewExpr(CXXNewExpr *E) {
    llvm::outs() << "new array=" << E->isArray()
                 << " allocated=" << E->getAllocatedType().getAsString()
                 << " usual-sized-delete=" << E->doesUsualArrayDeleteWantSize()
                 << " nullable=" << E->shouldNullCheckAllocation() << "\n";
    if (auto Bound = E->getArraySize()) {
      llvm::outs() << "  bound " << (*Bound)->getStmtClassName()
                   << " type=" << (*Bound)->getType().getAsString()
                   << " constant=" << bool((*Bound)->getIntegerConstantExpr(Context)) << "\n";
    }
    if (const auto *Init = E->getInitializer()) {
      llvm::outs() << "  initializer " << Init->getStmtClassName()
                   << " type=" << Init->getType().getAsString() << "\n";
      if (const auto *List = dyn_cast<InitListExpr>(Init)) {
        if (List->isSyntacticForm() && List->getSemanticForm())
          List = List->getSemanticForm();
        llvm::outs() << "  array prefix=" << List->getNumInits() << " filler=";
        if (const auto *Filler = List->getArrayFiller())
          llvm::outs() << Filler->getStmtClassName() << " type=" << Filler->getType().getAsString();
        else
          llvm::outs() << "none";
        llvm::outs() << "\n";
      }
    }
    return true;
  }
};

// Controlled upstream experiment: make the compatible friend owner visible to
// FindInstantiatedDecl's existing lexical-context walk. Production source must
// retain the actual declaration context and repair that walk instead.
class FriendContextConsumer : public ASTConsumer {
public:
  void HandleCXXImplicitFunctionInstantiation(FunctionDecl *Function) override {
    const auto *Primary = Function->getPrimaryTemplate();
    if (!Primary || !Function->getLexicalDeclContext()->isFileContext())
      return;
    for (const auto *Redecl : Primary->redecls()) {
      const auto *Template = cast<FunctionTemplateDecl>(Redecl);
      if (!Template->isCompatibleWithDefinition())
        continue;
      auto *Owner = Template->getTemplatedDecl()->getLexicalDeclContext();
      if (Template->getFriendObjectKind() && isa<CXXRecordDecl>(Owner) &&
          !Owner->isDependentContext()) {
        llvm::outs() << "experiment: expose compatible friend owner for "
                     << Function->getNameAsString() << "\n";
        Function->setLexicalDeclContext(Owner);
        Function->setObjectOfFriendDecl();
      }
      break;
    }
  }
  void HandleTranslationUnit(ASTContext &Context) override {
    Context.getTranslationUnitDecl()->dump(llvm::outs());
    Inspect Visitor(Context);
    Visitor.TraverseDecl(Context.getTranslationUnitDecl());
  }
};

class FriendContextAction : public ASTFrontendAction {
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &,
                                                llvm::StringRef) override {
    return std::make_unique<FriendContextConsumer>();
  }
};

int main(int Argc, const char **Argv) {
  llvm::sys::PrintStackTraceOnErrorSignal(Argv[0]);
  struct Fixture {
    const char *Name;
    const char *Source;
  };
  const Fixture Sources[] = {
      {"runtime-array-initializers",
      "using Size=decltype(sizeof(0));struct Tag{};void*operator new[](Size,Tag)noexcept{return nullptr;}"
      "struct Temporary{Temporary(int=0){}~Temporary(){}};"
      "struct R{int value;R(const Temporary&t=Temporary()):value(0){}~R(){}"
      "static void*operator new[](Size)noexcept{return nullptr;}};"
      "R*plain(int n){return new R[n];}R*value(int n){return new R[n]();}"
      "R*braced(int n){return new R[n]{};}"
      "R*list(int n){return new R[n]{{},{}};}"
      "struct Aggregate{R field;};Aggregate*aggregate(int n){return new(Tag{})Aggregate[n]{};}"
      "int*scalar(int n){return new(Tag{})int[n];}int*zero(int n){return new(Tag{})int[n]();}"
      "int*prefix(int n){return new(Tag{})int[n]{1,2};}char*text(int n){return new(Tag{})char[n]{\"hi\"};}"
      "using Row=R[2];Row*nested(int n){return new R[n][2];}"
      "R*empty(){return new R[0];}R*known(){return new R[3]{};}"
      "struct Bound{long long n;operator long long()const{return n;}};"
      "R*converted(long long n){return new R[Bound{n}];}"},
      {"static-forward-reference-copy",
      "int n=3;struct R{int&r;};struct S{static const R a;static const R b;};const R S::a=S::b;const R S::b{n};int f(){return ++S::a.r;}"},
      {"static-prior-reference-copy",
      "int n=3;struct R{int&r;};struct S{static const R a;static const R b;};const R S::b{n};const R S::a=S::b;int f(){return ++S::a.r;}"},
      {"static-constexpr-reference-copy",
      "int n=3;struct R{int&r;};struct S{static const R a;static constexpr R b{n};};const R S::a=S::b;int f(){return ++S::a.r;}"},
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
      {"qualified-friend-class",
      "namespace N{template<class U>struct A;template<class V>struct A;}"
      "template<class T>class R{int n=3;template<class U>friend struct N::A;};"
      "namespace N{template<class U>struct A{static int get(const R<int>&r){return r.n;}};}"
      "int main(){R<int>r;return N::A<int>::get(r)-3;}"},
      {"static-temporary-values",
      "const int&r=3;int&&v=4;const int(&a)[2]={5,6};"
      "struct R{int n;int*p;constexpr R():n(7),p(&n){}};R&&s=R();"
      "const int&field=R().n;const char*text=\"cat\";"
      "int main(){return r+v+a[1]+s.n+field+text[0];}"},
      {"static-reference-fillers",
      "struct H;struct T{const H*owner;};struct H{const T&value=T{this};};"
      "const H values[2]{};"
      "int main(){if(&values[0].value==&values[1].value)return 1;"
      "return values[0].value.owner!=&values[0]||values[1].value.owner!=&values[1]?2:0;}"},
      {"static-reference-single-filler",
      "struct H;struct T{const H*owner;};struct H{const T&value=T{this};};"
      "const H(&values)[1]={};"
      "int main(){return values[0].value.owner!=&values[0]?2:0;}"},
      {"automatic-reference-fillers",
      "int live;struct T{T(){++live;}~T(){--live;}};"
      "struct H{const T&value=T{};};"
      "int main(){H values[2]{};return live!=2||&values[0].value==&values[1].value;}"},
      {"aggregate-member-cleanup",
      "int live;struct T{T(){++live;}~T(){--live;}};"
      "struct M{int seen;M(const T&argument=T{}):seen(live){}};"
      "struct A{M member;};"
      "int main(){A values[2]{};if(live)return 2;return values[1].member.seen!=2;}"},
  };
  bool Found = false;
  bool Failed = false;
  const bool FriendContext = Argc > 1 &&
      llvm::StringRef(Argv[1]) == "visible-copied-friend-context";
  for (const auto &Input : Sources) {
    if (Argc > 1 && (FriendContext ? llvm::StringRef("visible-copied-friend")
                                 : llvm::StringRef(Argv[1])) != Input.Name)
      continue;
    Found = true;
    if (Argc > 2 && llvm::StringRef(Argv[2]) == "--source-only") {
      llvm::outs() << Input.Source << "\n";
      return 0;
    }
    llvm::outs() << "fixture " << Input.Name << "\n";
    llvm::outs().flush();
    if (FriendContext) {
      Failed |= !tooling::runToolOnCodeWithArgs(
          std::make_unique<FriendContextAction>(), Input.Source,
          {"-std=c++17"}, "input.cpp");
      continue;
    }
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
