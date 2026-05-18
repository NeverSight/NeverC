#include "neverc/Syntax/RunParser.h"
#include "neverc/Analyze/EnterExpressionEvaluationContext.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Core/TokenKinds.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Syntax/SyntaxParser.h"
#include "neverc/Tree/Core/TreeConsumer.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Stmt/Stmt.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>

using namespace neverc;

// ===----------------------------------------------------------------------===
// Stack trace & crash recovery helpers
// ===----------------------------------------------------------------------===

namespace {

class StackStateGuard
    : public llvm::CrashRecoveryContextCleanupBase<StackStateGuard,
                                                   const void> {
public:
  StackStateGuard(llvm::CrashRecoveryContext *Context, const void *Top)
      : llvm::CrashRecoveryContextCleanupBase<StackStateGuard, const void>(
            Context, Top) {}
  void recoverResources() override { llvm::RestorePrettyStackState(resource); }
};

class ParserStackTrace : public llvm::PrettyStackTraceEntry {
  const Parser &P;

public:
  ParserStackTrace(const Parser &p) : P(p) {}
  void print(llvm::raw_ostream &OS) const override;
};

void ParserStackTrace::print(llvm::raw_ostream &OS) const {
  const Token &Tok = P.getCurToken();
  if (Tok.is(tok::eof)) {
    OS << "<eof> parser at end of file\n";
    return;
  }

  if (Tok.getLocation().isInvalid()) {
    OS << "<unknown> parser at unknown location\n";
    return;
  }

  const PrepEngine &PP = P.getPrepEngine();
  Tok.getLocation().print(OS, PP.getSourceManager());
  if (Tok.isAnnotation()) {
    OS << ": at annotation token\n";
  } else {
    bool Invalid = false;
    const SourceManager &SM = P.getPrepEngine().getSourceManager();
    unsigned Length = Tok.getLength();
    const char *Spelling = SM.getCharacterData(Tok.getLocation(), &Invalid);
    if (Invalid) {
      OS << ": unknown current parser token\n";
      return;
    }
    OS << ": current parser token '" << llvm::StringRef(Spelling, Length)
       << "'\n";
  }
}

} // namespace

// ===----------------------------------------------------------------------===
// Top-level parsing entry point
// ===----------------------------------------------------------------------===

void neverc::RunParser(Sema &S, bool PrintStats) {
  if (PrintStats) {
    Decl::EnableStatistics();
    Stmt::EnableStatistics();
  }

  TreeConsumer *Consumer = &S.getTreeConsumer();

  std::unique_ptr<Parser> ParseOP(new Parser(S.getPrepEngine(), S));
  Parser &P = *ParseOP.get();

  llvm::CrashRecoveryContextCleanupRegistrar<const void, StackStateGuard>
      CleanupPrettyStack(llvm::SavePrettyStackState());
  ParserStackTrace CrashInfo(P);

  llvm::CrashRecoveryContextCleanupRegistrar<Parser> CleanupParser(
      ParseOP.get());

  PrepEngine &PP = S.getPrepEngine();
  PP.InitMainInput();

  bool HaveLexer = PP.getCurrentLexer();

  if (HaveLexer) {
    llvm::TimeTraceScope TimeScope("Frontend");
    P.Initialize();
    Parser::DeclGroupPtrTy ADecl;
    EnterExpressionEvaluationContext PotentiallyEvaluated(
        S, Sema::ExpressionEvaluationContext::PotentiallyEvaluated);

    for (bool AtEOF = P.ParseFirstTopLevelDecl(ADecl); !AtEOF;
         AtEOF = P.ParseTopLevelDecl(ADecl)) {
      if (ADecl && !Consumer->ProcessTopLevelDecl(ADecl.get()))
        return;
    }
  }

  for (Decl *D : S.WeakTopLevelDecls())
    Consumer->ProcessTopLevelDecl(DeclGroupRef(D));

  Consumer->ProcessTranslationUnit(S.getTreeContext());

  if (PrintStats) {
    llvm::errs() << "\nSTATISTICS:\n";
    S.getTreeContext().PrintStats();
    Decl::PrintStats();
    Stmt::PrintStats();
    Consumer->PrintStats();
  }
}
