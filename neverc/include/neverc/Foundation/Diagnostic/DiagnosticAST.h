#ifndef NEVERC_FOUNDATION_DIAGNOSTICAST_H
#define NEVERC_FOUNDATION_DIAGNOSTICAST_H

#include "neverc/Foundation/Diagnostic/Diagnostic.h"

namespace neverc {
namespace diag {
enum {
#define DIAG(ENUM, FLAGS, DEFAULT_MAPPING, DESC, GROUP, NOWERROR,              \
             SHOWINSYSHEADER, SHOWINSYSMACRO, DEFERRABLE, CATEGORY)            \
  ENUM,
#define ASTSTART
#include "neverc/Foundation/DiagnosticASTKinds.td.h"
#undef DIAG
  NUM_BUILTIN_AST_DIAGNOSTICS
};
} // end namespace diag
} // end namespace neverc

#endif // NEVERC_FOUNDATION_DIAGNOSTICAST_H
