#ifndef NEVERC_COMPILER_DIAGNOSTICRENDERER_H
#define NEVERC_COMPILER_DIAGNOSTICRENDERER_H

#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "neverc/Foundation/Diagnostic/DiagnosticOptions.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/StringRef.h"

namespace neverc {

class LangOptions;

using DiagOrStoredDiag =
    llvm::PointerUnion<const Diagnostic *, const StoredDiagnostic *>;

class DiagnosticRenderer {
protected:
  const LangOptions &LangOpts;
  llvm::IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts;

  SourceLocation LastLoc;

  SourceLocation LastIncludeLoc;

  DiagnosticsEngine::Level LastLevel = DiagnosticsEngine::Ignored;

  DiagnosticRenderer(const LangOptions &LangOpts, DiagnosticOptions *DiagOpts);

  virtual ~DiagnosticRenderer();

  virtual void emitDiagnosticMessage(FullSourceLoc Loc, PresumedLoc PLoc,
                                     DiagnosticsEngine::Level Level,
                                     llvm::StringRef Message,
                                     llvm::ArrayRef<CharSourceRange> Ranges,
                                     DiagOrStoredDiag Info) = 0;

  virtual void emitDiagnosticLoc(FullSourceLoc Loc, PresumedLoc PLoc,
                                 DiagnosticsEngine::Level Level,
                                 llvm::ArrayRef<CharSourceRange> Ranges) = 0;

  virtual void emitCodeContext(FullSourceLoc Loc,
                               DiagnosticsEngine::Level Level,
                               llvm::SmallVectorImpl<CharSourceRange> &Ranges,
                               llvm::ArrayRef<FixItHint> Hints) = 0;

  virtual void emitIncludeLocation(FullSourceLoc Loc, PresumedLoc PLoc) = 0;

  virtual void beginDiagnostic(DiagOrStoredDiag D,
                               DiagnosticsEngine::Level Level) {}
  virtual void endDiagnostic(DiagOrStoredDiag D,
                             DiagnosticsEngine::Level Level) {}

private:
  void emitBasicNote(llvm::StringRef Message);
  void emitIncludeStack(FullSourceLoc Loc, PresumedLoc PLoc,
                        DiagnosticsEngine::Level Level);
  void emitIncludeStackRecursively(FullSourceLoc Loc);
  void emitCaret(FullSourceLoc Loc, DiagnosticsEngine::Level Level,
                 llvm::ArrayRef<CharSourceRange> Ranges,
                 llvm::ArrayRef<FixItHint> Hints);
  void emitSingleMacroExpansion(FullSourceLoc Loc,
                                DiagnosticsEngine::Level Level,
                                llvm::ArrayRef<CharSourceRange> Ranges);
  void emitMacroExpansions(FullSourceLoc Loc, DiagnosticsEngine::Level Level,
                           llvm::ArrayRef<CharSourceRange> Ranges,
                           llvm::ArrayRef<FixItHint> Hints);

public:
  void emitDiagnostic(FullSourceLoc Loc, DiagnosticsEngine::Level Level,
                      llvm::StringRef Message,
                      llvm::ArrayRef<CharSourceRange> Ranges,
                      llvm::ArrayRef<FixItHint> FixItHints,
                      DiagOrStoredDiag D = (Diagnostic *)nullptr);

  void emitStoredDiagnostic(StoredDiagnostic &Diag);
};

class DiagnosticNoteRenderer : public DiagnosticRenderer {
public:
  DiagnosticNoteRenderer(const LangOptions &LangOpts,
                         DiagnosticOptions *DiagOpts)
      : DiagnosticRenderer(LangOpts, DiagOpts) {}

  ~DiagnosticNoteRenderer() override;

  void emitIncludeLocation(FullSourceLoc Loc, PresumedLoc PLoc) override;

  virtual void emitNote(FullSourceLoc Loc, llvm::StringRef Message) = 0;
};

} // namespace neverc

#endif // NEVERC_COMPILER_DIAGNOSTICRENDERER_H
