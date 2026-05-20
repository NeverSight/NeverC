#ifndef NEVERC_COMPILER_TEXTDIAGNOSTICBUFFER_H
#define NEVERC_COMPILER_TEXTDIAGNOSTICBUFFER_H

#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace neverc {

class TextDiagnosticBuffer : public DiagnosticConsumer {
public:
  using DiagList = std::vector<std::pair<SourceLocation, std::string>>;
  using iterator = DiagList::iterator;
  using const_iterator = DiagList::const_iterator;

private:
  DiagList Errors, Warnings, Remarks, Notes;

  std::vector<std::pair<DiagnosticsEngine::Level, size_t>> All;

public:
  const_iterator err_begin() const { return Errors.begin(); }
  const_iterator err_end() const { return Errors.end(); }

  const_iterator warn_begin() const { return Warnings.begin(); }
  const_iterator warn_end() const { return Warnings.end(); }

  const_iterator remark_begin() const { return Remarks.begin(); }
  const_iterator remark_end() const { return Remarks.end(); }

  const_iterator note_begin() const { return Notes.begin(); }
  const_iterator note_end() const { return Notes.end(); }

  void ProcessDiagnostic(DiagnosticsEngine::Level DiagLevel,
                         const Diagnostic &Info) override;

  void FlushDiagnostics(DiagnosticsEngine &Diags) const;
};

} // namespace neverc

#endif // NEVERC_COMPILER_TEXTDIAGNOSTICBUFFER_H
