#ifndef NEVERC_FRONTEND_TEXTDIAGNOSTIC_H
#define NEVERC_FRONTEND_TEXTDIAGNOSTIC_H

#include "neverc/Compiler/DiagnosticRenderer.h"

namespace neverc {

class TextDiagnostic : public DiagnosticRenderer {
  llvm::raw_ostream &OS;

public:
  TextDiagnostic(llvm::raw_ostream &OS, const LangOptions &LangOpts,
                 DiagnosticOptions *DiagOpts);

  ~TextDiagnostic() override;

  static void printDiagnosticLevel(llvm::raw_ostream &OS,
                                   DiagnosticsEngine::Level Level,
                                   bool ShowColors);

  static void printDiagnosticMessage(llvm::raw_ostream &OS, bool IsSupplemental,
                                     llvm::StringRef Message,
                                     unsigned CurrentColumn, unsigned Columns,
                                     bool ShowColors);

protected:
  void emitDiagnosticMessage(FullSourceLoc Loc, PresumedLoc PLoc,
                             DiagnosticsEngine::Level Level,
                             llvm::StringRef Message,
                             llvm::ArrayRef<CharSourceRange> Ranges,
                             DiagOrStoredDiag D) override;

  void emitDiagnosticLoc(FullSourceLoc Loc, PresumedLoc PLoc,
                         DiagnosticsEngine::Level Level,
                         llvm::ArrayRef<CharSourceRange> Ranges) override;

  void emitCodeContext(FullSourceLoc Loc, DiagnosticsEngine::Level Level,
                       llvm::SmallVectorImpl<CharSourceRange> &Ranges,
                       llvm::ArrayRef<FixItHint> Hints) override {
    emitSnippetAndCaret(Loc, Level, Ranges, Hints);
  }

  void emitIncludeLocation(FullSourceLoc Loc, PresumedLoc PLoc) override;

private:
  void emitFilename(llvm::StringRef Filename, const SourceManager &SM);

  void emitSnippetAndCaret(FullSourceLoc Loc, DiagnosticsEngine::Level Level,
                           llvm::SmallVectorImpl<CharSourceRange> &Ranges,
                           llvm::ArrayRef<FixItHint> Hints);

  void emitSnippet(llvm::StringRef SourceLine, unsigned MaxLineNoDisplayWidth,
                   unsigned LineNo);

  void emitParseableFixits(llvm::ArrayRef<FixItHint> Hints,
                           const SourceManager &SM);
};

} // end namespace neverc

#endif
