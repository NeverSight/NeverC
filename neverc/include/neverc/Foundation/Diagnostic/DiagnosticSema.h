#ifndef NEVERC_BASIC_DIAGNOSTICSEMA_H
#define NEVERC_BASIC_DIAGNOSTICSEMA_H

#include "neverc/Foundation/Diagnostic/Diagnostic.h"

namespace neverc {
namespace diag {
enum {
#define DIAG(ENUM, FLAGS, DEFAULT_MAPPING, DESC, GROUP, NOWERROR,              \
             SHOWINSYSHEADER, SHOWINSYSMACRO, DEFERRABLE, CATEGORY)            \
  ENUM,
#define SEMASTART
#include "neverc/Foundation/DiagnosticSemaKinds.td.h"
#undef DIAG
  NUM_BUILTIN_SEMA_DIAGNOSTICS
};
} // end namespace diag
} // end namespace neverc

#endif // NEVERC_BASIC_DIAGNOSTICSEMA_H
