#include "neverc/Compiler/FrontendAction.h"
#include "neverc/Compiler/CompilerInstance.h"
#include "neverc/Compiler/Utils.h"
#include "neverc/Foundation/Builtin/BuiltinString.h"
#include "neverc/Foundation/Builtin/Builtins.h"
#include "neverc/Foundation/Core/Stack.h"
#include "neverc/Foundation/Diagnostic/DiagnosticOptions.h"
#include "neverc/Foundation/LangOpts/LangStandard.h"
#include "neverc/Scan/IncludeResolver.h"
#include "neverc/Scan/LiteralParser.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Scan/SourceScanner.h"
#include "neverc/Syntax/RunParser.h"
#include "neverc/Tree/Core/TreeConsumer.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Decl/DeclGroup.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/BuryPointer.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>
using namespace neverc;

// ===----------------------------------------------------------------------===
// FrontendAction
// ===----------------------------------------------------------------------===

FrontendAction::FrontendAction() : Instance(nullptr) {}

FrontendAction::~FrontendAction() {}

void FrontendAction::setCurrentInput(const FrontendInputFile &CurrentInput) {
  this->CurrentInput = CurrentInput;
}

std::unique_ptr<TreeConsumer>
FrontendAction::CreateWrappedConsumer(CompilerInstance &CI,
                                      llvm::StringRef InFile) {
  return CreateTreeConsumer(CI, InFile);
}

namespace {
SourceLocation readOriginalFileName(CompilerInstance &CI,
                                    std::string &InputFile) {
  auto &SourceMgr = CI.getSourceManager();
  auto MainFileID = SourceMgr.getMainFileID();

  auto MainFileBuf = SourceMgr.getBufferOrNone(MainFileID);
  if (!MainFileBuf)
    return SourceLocation();

  std::unique_ptr<SourceScanner> RawLexer(
      new SourceScanner(MainFileID, *MainFileBuf, SourceMgr, CI.getLangOpts()));

  // If the first line has the syntax of
  //
  // # NUM "FILENAME"
  //
  // we use FILENAME as the input file name.
  Token T;
  if (RawLexer->LexFromRawLexer(T) || T.getKind() != tok::hash)
    return SourceLocation();
  if (RawLexer->LexFromRawLexer(T) || T.isAtStartOfLine() ||
      T.getKind() != tok::numeric_constant)
    return SourceLocation();

  RawLexer->LexFromRawLexer(T);
  if (T.isAtStartOfLine() || T.getKind() != tok::string_literal)
    return SourceLocation();

  StringLiteralParser Literal(T, CI.getPrepEngine());
  if (Literal.hadError)
    return SourceLocation();
  RawLexer->LexFromRawLexer(T);
  if (T.isNot(tok::eof) && !T.isAtStartOfLine())
    return SourceLocation();
  InputFile = Literal.getString().str();

  return T.getLocation();
}

} // namespace

bool FrontendAction::BeginSourceFile(CompilerInstance &CI,
                                     const FrontendInputFile &RealInput) {
  FrontendInputFile Input(RealInput);
  assert(!Instance && "Already processing a source file!");
  assert(!Input.isEmpty() && "Unexpected empty filename!");
  setCurrentInput(Input);
  setCompilerInstance(&CI);

  bool HasBegunSourceFile = false;

  // If we fail, reset state since the client will not end up calling the
  // matching EndSourceFile(). All paths that return true should release this.
  auto FailureCleanup = llvm::make_scope_exit([&]() {
    if (HasBegunSourceFile)
      CI.getDiagnosticClient().EndSourceFile();
    CI.setTreeConsumer(nullptr);
    CI.clearOutputFiles(/*EraseFiles=*/true);
    setCurrentInput(FrontendInputFile());
    setCompilerInstance(nullptr);
  });

  if (!BeginInvocation(CI))
    return false;

  if (!CI.hasFileManager()) {
    if (!CI.createFileManager()) {
      return false;
    }
  }
  if (!CI.hasSourceManager()) {
    CI.createSourceManager(CI.getFileManager());
  }

  // IR files bypass the rest of initialization.
  if (Input.getKind().getLanguage() == Language::LLVM_IR) {
    assert(hasIRSupport() && "This action does not have IR file support!");

    // Inform the diagnostic client we are processing a source file.
    CI.getDiagnosticClient().BeginSourceFile(CI.getLangOpts(), nullptr);
    HasBegunSourceFile = true;

    if (!BeginSourceFileAction(CI))
      return false;

    if (!CI.InitializeSourceManager(CurrentInput))
      return false;

    FailureCleanup.release();
    return true;
  }

  CI.createPrepEngine();

  // Inform the diagnostic client we are processing a source file.
  CI.getDiagnosticClient().BeginSourceFile(CI.getLangOpts(),
                                           &CI.getPrepEngine());
  HasBegunSourceFile = true;

  if (!CI.InitializeSourceManager(Input))
    return false;

  if (CI.hasPrepEngine() && !usesPreprocessorOnly()) {
    PrepEngine &PP = CI.getPrepEngine();
    if (PP.getLangOpts().BuiltinString) {
      std::string NewPredefines = PP.getPredefines();
      bool HasAllocOverride =
          NewPredefines.find("NEVERC_STRING_ALLOC") != std::string::npos ||
          NewPredefines.find("NEVERC_STRING_FREE") != std::string::npos;
      bool UseFullPrelude =
          HasAllocOverride ||
          BuiltinString::getEmbeddedStringBitcode().empty();
      if (UseFullPrelude) {
        NewPredefines += BuiltinString::getBuiltinStringPrelude().str();
      } else {
        NewPredefines += BuiltinString::getBuiltinStringThinHeader().str();
      }
      PP.setPredefines(std::move(NewPredefines));
    }
  }

  if (!BeginSourceFileAction(CI))
    return false;

  // Create the AST context and consumer unless this is a preprocessor only
  // action.
  if (!usesPreprocessorOnly()) {
    CI.createTreeContext();

    // For preprocessed files, check if the first line specifies the original
    // source file name with a linemarker.
    std::string PresumedInputFile = std::string(getCurrentFileOrBufferName());
    if (Input.isPreprocessed())
      readOriginalFileName(CI, PresumedInputFile);

    std::unique_ptr<TreeConsumer> Consumer =
        CreateWrappedConsumer(CI, PresumedInputFile);
    if (!Consumer)
      return false;

    CI.getTreeContext().setTreeMutationListener(
        Consumer->GetTreeMutationListener());

    CI.setTreeConsumer(std::move(Consumer));
    if (!CI.hasTreeConsumer())
      return false;
  }

  {
    PrepEngine &PP = CI.getPrepEngine();
    PP.getBuiltinInfo().initializeBuiltins(PP.getIdentifierTable(),
                                           PP.getLangOpts());
  }

  FailureCleanup.release();
  return true;
}

llvm::Error FrontendAction::Execute() {
  CompilerInstance &CI = getCompilerInstance();

  if (CI.hasFrontendTimer()) {
    llvm::TimeRegion Timer(CI.getFrontendTimer());
    ExecuteAction();
  } else
    ExecuteAction();

  return llvm::Error::success();
}

void FrontendAction::EndSourceFile() {
  CompilerInstance &CI = getCompilerInstance();

  // Inform the diagnostic client we are done with this source file.
  CI.getDiagnosticClient().EndSourceFile();

  // Inform the preprocessor we are done.
  if (CI.hasPrepEngine())
    CI.getPrepEngine().FiniMainInput();

  // Finalize the action.
  EndSourceFileAction();

  // Sema references the ast consumer, so reset sema first.
  //
  bool DisableFree = CI.getFrontendOpts().DisableFree;
  if (DisableFree) {
    CI.resetAndLeakSema();
    CI.resetAndLeakTreeContext();
    llvm::BuryPointer(CI.takeTreeConsumer().get());
  } else {
    CI.setSema(nullptr);
    CI.setTreeContext(nullptr);
    CI.setTreeConsumer(nullptr);
  }

  if (CI.getFrontendOpts().ShowStats) {
    llvm::errs() << "\nSTATISTICS FOR '" << getCurrentFileOrBufferName()
                 << "':\n";
    CI.getPrepEngine().DumpStats();
    CI.getPrepEngine().getIdentifierTable().PrintStats();
    CI.getPrepEngine().getIncludeResolver().PrintStats();
    CI.getSourceManager().PrintStats();
    llvm::errs() << "\n";
  }

  // Cleanup the output streams, and erase the output files if instructed by the
  // FrontendAction.
  CI.clearOutputFiles(/*EraseFiles=*/shouldEraseOutputFiles());

  // The resources are owned by AST when the current file is AST.
  // So we reset the resources here to avoid users accessing it
  setCompilerInstance(nullptr);
  setCurrentInput(FrontendInputFile());
}

bool FrontendAction::shouldEraseOutputFiles() {
  return getCompilerInstance().getDiagnostics().hasErrorOccurred();
}

void ASTFrontendAction::ExecuteAction() {
  CompilerInstance &CI = getCompilerInstance();
  if (!CI.hasPrepEngine())
    return;
  // This is a fallback: If the client forgets to invoke this, we mark the
  // current stack as the bottom. Though not optimal, this could help prevent
  // stack overflow during deep recursion.
  neverc::noteBottomOfStack();

  if (!CI.hasSema())
    CI.createSema();

  RunParser(CI.getSema(), CI.getFrontendOpts().ShowStats);
}

std::unique_ptr<TreeConsumer>
PreprocessorFrontendAction::CreateTreeConsumer(CompilerInstance &CI,
                                               llvm::StringRef InFile) {
  llvm_unreachable("Invalid CreateTreeConsumer on preprocessor action!");
}
