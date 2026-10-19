// Native upstream AST evidence only. This probe grants no NeverC admission.
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/ExprCXX.h"
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

class PairVisitor : public RecursiveASTVisitor<PairVisitor> {
  ASTContext &Context;
  std::set<const CXXConstructorDecl *> Seen;
  unsigned Public = 0, Private = 0;
public:
  explicit PairVisitor(ASTContext &Context) : Context(Context) {}
  void constructor(const CXXConstructorDecl *C) {
    if (!C || !C->getParent()->getIdentifier() ||
        C->getParent()->getName() != "pair" ||
        (C->getNumParams() != 3 && C->getNumParams() != 5) ||
        !C->getTemplateSpecializationArgs())
      return;
    const auto *Tag = C->getParamDecl(0)->getType()->getAsCXXRecordDecl();
    if (!Tag || !Tag->getIdentifier() || Tag->getName() != "piecewise_construct_t" ||
        !Seen.insert(C->getCanonicalDecl()).second)
      return;
    auto &OS = llvm::outs();
    if (C->getNumParams() == 3) {
      OS << "PAIR-PIECEWISE-PUBLIC\n";
      ++Public;
    } else {
      OS << "PAIR-PIECEWISE-PRIVATE\n";
      ++Private;
    }
    const auto &SM = Context.getSourceManager();
    OS << "location: " << C->getLocation().printToString(SM) << "\n";
    OS << "spelling: " << SM.getSpellingLoc(C->getLocation()).printToString(SM) << "\n";
    OS << "access: " << unsigned(C->getAccess()) << ", delegating: "
       << C->isDelegatingConstructor() << ", parameters: " << C->getNumParams() << "\n";
    const auto *Prototype = C->getType()->getAs<FunctionProtoType>();
    OS << "exception-spec: " << (Prototype ? int(Prototype->getExceptionSpecType()) : -1) << "\n";
    for (unsigned I = 0; I < C->getNumParams(); ++I)
      OS << "parameter " << I << ": " << C->getParamDecl(I)->getType().getAsString() << "\n";
    const auto *D = dyn_cast_or_null<CXXConstructorDecl>(C->getDefinition());
    OS << "has definition: " << bool(D) << "\n";
    if (D) {
      D->dump(OS);
      for (const auto *Init : D->inits()) {
        OS << "INITIALIZER delegating=" << Init->isDelegatingInitializer()
           << " member=" << Init->isMemberInitializer() << "\n";
        if (const auto *Member = Init->getAnyMember())
          OS << "member-name: " << Member->getNameAsString() << "\n";
        Init->getInit()->dump(OS, Context);
      }
    }
  }
  bool VisitCXXConstructExpr(CXXConstructExpr *E) {
    constructor(E->getConstructor());
    return true;
  }
  bool VisitCXXConstructorDecl(CXXConstructorDecl *C) {
    constructor(C);
    return true;
  }
  void finish() {
    llvm::outs() << "PAIR-PIECEWISE-COUNTS public=" << Public << " private=" << Private << "\n";
  }
};

class PairConsumer : public ASTConsumer {
public:
  void HandleTranslationUnit(ASTContext &Context) override {
    PairVisitor Visitor(Context);
    Visitor.TraverseDecl(Context.getTranslationUnitDecl());
    Visitor.finish();
  }
};
class PairAction : public ASTFrontendAction {
public:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &, llvm::StringRef) override {
    return std::make_unique<PairConsumer>();
  }
};
int main(int argc, char **argv) {
  if (argc != 3)
    return 2;
  auto Source = llvm::MemoryBuffer::getFile(argv[2]);
  if (!Source) {
    llvm::errs() << Source.getError().message() << "\n";
    return 3;
  }
  const std::string SDK = std::string(argv[1]) + "/neverc/lib/Translate/Cpp/SDK/";
  std::vector<std::string> Arguments{
      "--no-default-config", "-std=c++17", "-stdlib=libc++", "-nostdinc++",
      "-resource-dir", SDK + "resource", "-isystem", SDK + "libcxx",
      "-D_LIBCPP_REMOVE_TRANSITIVE_INCLUDES", "-D_NEVERC_CPP_CORE_V2_SDK",
      "-D_LIBCPP___ITERATOR_ISTREAM_ITERATOR_H", "-D_LIBCPP___ITERATOR_ISTREAMBUF_ITERATOR_H",
      "-D_LIBCPP___ITERATOR_OSTREAM_ITERATOR_H", "-D_LIBCPP___ITERATOR_OSTREAMBUF_ITERATOR_H",
      "-D_LIBCPP_DISABLE_VISIBILITY_ANNOTATIONS"};
  return tooling::runToolOnCodeWithArgs(std::make_unique<PairAction>(),
      (*Source)->getBuffer(), Arguments, argv[2], "/usr/bin/clang++-20") ? 0 : 1;
}
