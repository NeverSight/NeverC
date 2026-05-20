#ifndef NEVERC_FOUNDATION_DIAGNOSTICLEX_H
#define NEVERC_FOUNDATION_DIAGNOSTICLEX_H

#include "neverc/Foundation/Diagnostic/Diagnostic.h"

namespace neverc {
namespace diag {
enum {
#define DIAG(ENUM, FLAGS, DEFAULT_MAPPING, DESC, GROUP, NOWERROR,              \
             SHOWINSYSHEADER, SHOWINSYSMACRO, DEFERRABLE, CATEGORY)            \
  ENUM,
#define LEXSTART
#include "neverc/Foundation/DiagnosticLexKinds.td.h"
#undef DIAG
  NUM_BUILTIN_LEX_DIAGNOSTICS
};
} // end namespace diag
} // end namespace neverc

#endif // NEVERC_FOUNDATION_DIAGNOSTICLEX_H
