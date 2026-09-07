// Feasibility data only: this is NOT the production frontend protocol.
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/Version.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Index/USRGeneration.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
namespace json = llvm::json;

static llvm::cl::OptionCategory Category("NeverC C++ feasibility prototype");

class Exporter : public RecursiveASTVisitor<Exporter> {
  ASTContext &Context;
  SourceManager &Sources;
  json::Array Namespaces, Records, Functions;

  bool owned(const Decl *D) const {
    return !D->isImplicit() && Sources.isWrittenInMainFile(D->getLocation());
  }

  json::Object location(SourceLocation L) const {
    auto P = Sources.getPresumedLoc(Sources.getExpansionLoc(L));
    if (!P.isValid())
      return {};
    // Basenames are sufficient only for these single-file prototype fixtures.
    return json::Object{{"file", llvm::sys::path::filename(P.getFilename()).str()},
            {"line", P.getLine()}, {"column", P.getColumn()}};
  }

  std::string identity(const Decl *D) const {
    llvm::SmallString<128> USR;
    if (index::generateUSRForDecl(D->getCanonicalDecl(), USR))
      return "unavailable";
    return USR.str().str();
  }

  json::Object type(QualType T) const {
    PrintingPolicy Policy(Context.getLangOpts());
    json::Object O{{"spelling", T.getAsString(Policy)},
                   {"canonical", T.getCanonicalType().getAsString(Policy)}};
    if (!T->isIncompleteType() && !T->isFunctionType() && !T->isVoidType()) {
      O["width_bits"] = Context.getTypeSize(T);
      O["alignment_bits"] = Context.getTypeAlign(T);
    }
    return O;
  }

  json::Object declaration(const NamedDecl *D) const {
    return json::Object{{"id", identity(D)}, {"name", D->getNameAsString()},
            {"qualified_name", D->getQualifiedNameAsString()},
            {"location", location(D->getLocation())}};
  }

  json::Object expression(const Expr *E) {
    json::Object O{{"kind", E->getStmtClassName()}, {"type", type(E->getType())},
                   {"location", location(E->getExprLoc())}};
    if (const auto *I = dyn_cast<IntegerLiteral>(E)) {
      llvm::SmallString<32> Text;
      I->getValue().toString(Text, 10, false);
      O["value"] = Text.str().str();
    } else if (const auto *B = dyn_cast<CXXBoolLiteralExpr>(E)) {
      O["value"] = B->getValue();
    } else if (const auto *R = dyn_cast<DeclRefExpr>(E)) {
      O["declaration"] = identity(R->getDecl());
    } else if (const auto *P = dyn_cast<ParenExpr>(E)) {
      O["operand"] = expression(P->getSubExpr());
    } else if (const auto *C = dyn_cast<CastExpr>(E)) {
      O["operation"] = C->getCastKindName();
      O["operand"] = expression(C->getSubExpr());
    } else if (const auto *B = dyn_cast<BinaryOperator>(E)) {
      O["operation"] = B->getOpcodeStr();
      O["left"] = expression(B->getLHS());
      O["right"] = expression(B->getRHS());
    } else if (const auto *U = dyn_cast<UnaryOperator>(E)) {
      O["operation"] = UnaryOperator::getOpcodeStr(U->getOpcode());
      O["postfix"] = U->isPostfix();
      O["operand"] = expression(U->getSubExpr());
    } else if (const auto *C = dyn_cast<CallExpr>(E)) {
      if (const auto *F = C->getDirectCallee())
        O["callee"] = identity(F);
      json::Array Args;
      for (const auto *A : C->arguments())
        Args.push_back(expression(A));
      O["arguments"] = std::move(Args);
    } else if (const auto *M = dyn_cast<MemberExpr>(E)) {
      O["member"] = identity(M->getMemberDecl());
      O["base"] = expression(M->getBase());
    } else if (const auto *I = dyn_cast<InitListExpr>(E)) {
      json::Array Elements;
      for (const auto *Init : I->inits())
        Elements.push_back(expression(Init));
      O["elements"] = std::move(Elements);
    }
    return O;
  }

  json::Object statement(const Stmt *S) {
    if (const auto *E = dyn_cast<Expr>(S))
      return expression(E);
    json::Object O{{"kind", S->getStmtClassName()},
                   {"location", location(S->getBeginLoc())}};
    if (const auto *C = dyn_cast<CompoundStmt>(S)) {
      json::Array Children;
      for (const auto *Child : C->body())
        Children.push_back(statement(Child));
      O["statements"] = std::move(Children);
    } else if (const auto *R = dyn_cast<ReturnStmt>(S)) {
      if (R->getRetValue())
        O["value"] = expression(R->getRetValue());
    } else if (const auto *DS = dyn_cast<DeclStmt>(S)) {
      json::Array Vars;
      for (const auto *D : DS->decls()) {
        if (const auto *V = dyn_cast<VarDecl>(D)) {
          auto Var = declaration(V);
          Var["type"] = type(V->getType());
          if (V->getInit())
            Var["initializer"] = expression(V->getInit());
          Vars.push_back(std::move(Var));
        }
      }
      O["variables"] = std::move(Vars);
    }
    return O;
  }

public:
  explicit Exporter(ASTContext &C) : Context(C), Sources(C.getSourceManager()) {}

  bool VisitNamespaceDecl(NamespaceDecl *D) {
    if (owned(D))
      Namespaces.push_back(declaration(D));
    return true;
  }

  bool VisitCXXRecordDecl(CXXRecordDecl *D) {
    if (!owned(D) || !D->isCompleteDefinition())
      return true;
    auto O = declaration(D);
    O["aggregate"] = D->isAggregate();
    O["type"] = type(Context.getRecordType(D));
    json::Array Fields;
    unsigned Index = 0;
    const auto &Layout = Context.getASTRecordLayout(D);
    for (const auto *F : D->fields()) {
      auto Field = declaration(F);
      Field["type"] = type(F->getType());
      Field["offset_bits"] = Layout.getFieldOffset(Index++);
      Fields.push_back(std::move(Field));
    }
    O["fields"] = std::move(Fields);
    Records.push_back(std::move(O));
    return true;
  }

  bool VisitFunctionDecl(FunctionDecl *D) {
    if (!owned(D) || !D->doesThisDeclarationHaveABody())
      return true;
    auto O = declaration(D);
    O["type"] = type(D->getType());
    O["result_type"] = type(D->getReturnType());
    O["c_linkage"] = D->isExternC();
    O["main"] = D->isMain();
    json::Array Parameters;
    for (const auto *P : D->parameters()) {
      auto Parameter = declaration(P);
      Parameter["type"] = type(P->getType());
      Parameters.push_back(std::move(Parameter));
    }
    O["parameters"] = std::move(Parameters);
    O["body"] = statement(D->getBody());
    Functions.push_back(std::move(O));
    return true;
  }

  json::Object finish() {
    const auto &Target = Context.getTargetInfo();
    return json::Object{{"format", "neverc-cpp-feasibility-only"}, {"version", 1},
            {"frontend", getClangFullVersion()},
            {"target", Target.getTriple().str()},
            {"int_width", Target.getIntWidth()},
            {"pointer_width", Target.getPointerWidth(LangAS::Default)},
            {"namespaces", std::move(Namespaces)},
            {"records", std::move(Records)},
            {"functions", std::move(Functions)}};
  }
};

class Consumer : public ASTConsumer {
public:
  void HandleTranslationUnit(ASTContext &Context) override {
    if (Context.getDiagnostics().hasErrorOccurred())
      return;
    Exporter E(Context);
    E.TraverseDecl(Context.getTranslationUnitDecl());
    llvm::outs() << llvm::formatv("{0:2}\n", json::Value(E.finish()));
  }
};

class Action : public ASTFrontendAction {
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &,
                                               llvm::StringRef) override {
    return std::make_unique<Consumer>();
  }
};

int main(int Argc, const char **Argv) {
  auto Options = tooling::CommonOptionsParser::create(Argc, Argv, Category,
                                                     llvm::cl::OneOrMore);
  if (!Options) {
    llvm::errs() << Options.takeError();
    return 1;
  }
  if (Options->getSourcePathList().size() != 1) {
    llvm::errs() << "prototype accepts exactly one source file\n";
    return 1;
  }
  tooling::ClangTool Tool(Options->getCompilations(), Options->getSourcePathList());
  return Tool.run(tooling::newFrontendActionFactory<Action>().get());
}
