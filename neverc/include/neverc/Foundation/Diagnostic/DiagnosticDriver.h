#ifndef NEVERC_FOUNDATION_DIAGNOSTICDRIVER_H
#define NEVERC_FOUNDATION_DIAGNOSTICDRIVER_H

#include "neverc/Foundation/Diagnostic/Diagnostic.h"

namespace neverc {
namespace diag {
enum {
#define DIAG(ENUM, FLAGS, DEFAULT_MAPPING, DESC, GROUP, NOWERROR,              \
             SHOWINSYSHEADER, SHOWINSYSMACRO, DEFERRABLE, CATEGORY)            \
  ENUM,
#define DRIVERSTART
#include "neverc/Foundation/DiagnosticDriverKinds.td.h"
#undef DIAG
  NUM_BUILTIN_DRIVER_DIAGNOSTICS
};
} // end namespace diag
} // end namespace neverc

#endif // NEVERC_FOUNDATION_DIAGNOSTICDRIVER_H
