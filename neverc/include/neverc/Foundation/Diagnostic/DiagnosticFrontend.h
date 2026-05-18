#ifndef NEVERC_BASIC_DIAGNOSTICFRONTEND_H
#define NEVERC_BASIC_DIAGNOSTICFRONTEND_H

#include "neverc/Foundation/Diagnostic/Diagnostic.h"

namespace neverc {
namespace diag {
enum {
#define DIAG(ENUM, FLAGS, DEFAULT_MAPPING, DESC, GROUP, NOWERROR,              \
             SHOWINSYSHEADER, SHOWINSYSMACRO, DEFERRABLE, CATEGORY)            \
  ENUM,
#define FRONTENDSTART
#include "neverc/Foundation/DiagnosticFrontendKinds.td.h"
#undef DIAG
  NUM_BUILTIN_FRONTEND_DIAGNOSTICS
};
} // end namespace diag
} // end namespace neverc

#endif // NEVERC_BASIC_DIAGNOSTICFRONTEND_H
