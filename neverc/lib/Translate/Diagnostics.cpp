#include "Diagnostics.h"
#include "JSON.h"
#include "llvm/Support/raw_ostream.h"

namespace neverc::translate {
llvm::json::Object diagnosticJSON(const Diagnostic &D) {
  return llvm::json::Object{{"code", jsonString(D.Code)},
                            {"file", jsonString(D.Location.File)},
                            {"line", D.Location.Line},
                            {"column", D.Location.Column},
                            {"construct", jsonString(D.Construct)},
                            {"reason", jsonString(D.Reason)},
                            {"guidance", jsonString(D.Guidance)}};
}
void printDiagnostics(const Diagnostics &D) {
  for (const auto &Item : D)
    llvm::errs() << Item.Location.File << ':' << Item.Location.Line << ':'
                 << Item.Location.Column << ": error [" << Item.Code << "] "
                 << Item.Construct << ": " << Item.Reason << "\n  "
                 << Item.Guidance << '\n';
}
Diagnostic driverDiagnostic(llvm::StringRef Code, llvm::StringRef Source,
                            llvm::StringRef Construct, llvm::StringRef Reason,
                            llvm::StringRef Guidance) {
  return {Code.str(),
          {Source.str(), 1, 1},
          Construct.str(),
          Reason.str(),
          Guidance.str()};
}
} // namespace neverc::translate
