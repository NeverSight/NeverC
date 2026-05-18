#include "neverc/Compiler/TextDiagnosticBuffer.h"
#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"

using namespace neverc;

__attribute__((hot)) void
TextDiagnosticBuffer::ProcessDiagnostic(DiagnosticsEngine::Level Level,
                                        const Diagnostic &Info) {
  DiagnosticConsumer::ProcessDiagnostic(Level, Info);

  llvm::SmallString<100> Buf;
  Info.FormatDiagnostic(Buf);
  auto Loc = Info.getLocation();
  std::string Msg(Buf.str());

  using DL = DiagnosticsEngine;
  if (LLVM_LIKELY(Level == DL::Warning)) {
    All.emplace_back(Level, Warnings.size());
    Warnings.emplace_back(Loc, std::move(Msg));
  } else if (Level == DL::Error || Level == DL::Fatal) {
    All.emplace_back(Level, Errors.size());
    Errors.emplace_back(Loc, std::move(Msg));
  } else if (Level == DL::Note) {
    All.emplace_back(Level, Notes.size());
    Notes.emplace_back(Loc, std::move(Msg));
  } else if (Level == DL::Remark) {
    All.emplace_back(Level, Remarks.size());
    Remarks.emplace_back(Loc, std::move(Msg));
  } else {
    llvm_unreachable("Diagnostic not handled during diagnostic buffering!");
  }
}

void TextDiagnosticBuffer::FlushDiagnostics(DiagnosticsEngine &Diags) const {
  for (const auto &I : All) {
    auto Diag = Diags.Report(Diags.getCustomDiagID(I.first, "%0"));
    using DL = DiagnosticsEngine;
    const auto &Ref = [&]() -> const std::string & {
      switch (I.first) {
      case DL::Warning:
        return Warnings[I.second].second;
      case DL::Error:
      case DL::Fatal:
        return Errors[I.second].second;
      case DL::Note:
        return Notes[I.second].second;
      case DL::Remark:
        return Remarks[I.second].second;
      default:
        llvm_unreachable("Diagnostic not handled during flushing!");
      }
    }();
    Diag << Ref;
  }
}
