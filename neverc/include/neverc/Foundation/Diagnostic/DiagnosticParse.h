#ifndef NEVERC_FOUNDATION_DIAGNOSTICPARSE_H
#define NEVERC_FOUNDATION_DIAGNOSTICPARSE_H

#include "neverc/Foundation/Diagnostic/Diagnostic.h"

namespace neverc {
namespace diag {
enum {
#define DIAG(ENUM, FLAGS, DEFAULT_MAPPING, DESC, GROUP, NOWERROR,              \
             SHOWINSYSHEADER, SHOWINSYSMACRO, DEFERRABLE, CATEGORY)            \
  ENUM,
#define PARSESTART
#include "neverc/Foundation/DiagnosticParseKinds.td.h"
#undef DIAG
  NUM_BUILTIN_PARSE_DIAGNOSTICS
};
} // end namespace diag
} // end namespace neverc

#endif // NEVERC_FOUNDATION_DIAGNOSTICPARSE_H
