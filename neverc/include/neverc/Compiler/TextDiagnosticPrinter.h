#ifndef NEVERC_FRONTEND_TEXTDIAGNOSTICPRINTER_H
#define NEVERC_FRONTEND_TEXTDIAGNOSTICPRINTER_H

#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/Support/Compiler.h"
#include <memory>

namespace neverc {
class DiagnosticOptions;
class LangOptions;
class TextDiagnostic;

class TextDiagnosticPrinter : public DiagnosticConsumer {
  llvm::raw_ostream &OS;
  llvm::IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts;

  std::unique_ptr<TextDiagnostic> TextDiag;

  std::string Prefix;

  LLVM_PREFERRED_TYPE(bool)
  unsigned OwnsOutputStream : 1;

public:
  TextDiagnosticPrinter(llvm::raw_ostream &os, DiagnosticOptions *diags,
                        bool OwnsOutputStream = false);
  ~TextDiagnosticPrinter() override;

  void setPrefix(std::string Value) { Prefix = std::move(Value); }

  void BeginSourceFile(const LangOptions &LO, const PrepEngine *PP) override;
  void EndSourceFile() override;
  void ProcessDiagnostic(DiagnosticsEngine::Level Level,
                         const Diagnostic &Info) override;
};

} // end namespace neverc

#endif
