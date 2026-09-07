#ifndef NEVERC_TRANSLATE_DIAGNOSTICS_H
#define NEVERC_TRANSLATE_DIAGNOSTICS_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/JSON.h"
#include <cstdint>
#include <string>
#include <vector>

namespace neverc::translate {
struct SourceLocation {
  std::string File;
  uint32_t Line = 1;
  uint32_t Column = 1;
};
struct Diagnostic {
  std::string Code;
  SourceLocation Location;
  std::string Construct;
  std::string Reason;
  std::string Guidance;
};
using Diagnostics = std::vector<Diagnostic>;

llvm::json::Object diagnosticJSON(const Diagnostic &D);
void printDiagnostics(const Diagnostics &D);
Diagnostic driverDiagnostic(llvm::StringRef Code, llvm::StringRef Source,
                            llvm::StringRef Construct, llvm::StringRef Reason,
                            llvm::StringRef Guidance);
} // namespace neverc::translate
#endif
