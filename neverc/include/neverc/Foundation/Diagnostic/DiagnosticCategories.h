#ifndef NEVERC_FOUNDATION_DIAGNOSTICCATEGORIES_H
#define NEVERC_FOUNDATION_DIAGNOSTICCATEGORIES_H

namespace neverc {
namespace diag {
enum {
#define GET_CATEGORY_TABLE
#define CATEGORY(X, ENUM) ENUM,
#include "neverc/Foundation/DiagnosticGroups.td.h"
#undef CATEGORY
#undef GET_CATEGORY_TABLE
  DiagCat_NUM_CATEGORIES
};

enum class Group {
#define DIAG_ENTRY(GroupName, FlagNameOffset, Members, SubGroups, Docs)        \
  GroupName,
#include "neverc/Foundation/DiagnosticGroups.td.h"
#undef CATEGORY
#undef DIAG_ENTRY
};
} // end namespace diag
} // end namespace neverc

#endif
