#ifndef NEVERC_ANALYZE_SEMANAMELOOKUPKINDS_H
#define NEVERC_ANALYZE_SEMANAMELOOKUPKINDS_H

namespace neverc {

enum ResolveNameKind {
  ResolveOrdinary = 0,
  ResolveTag,
  ResolveLabel,
  ResolveMember,
  ResolveRedeclWithLinkage,
  ResolveAny
};

enum RedeclarationKind {
  NotForRedeclaration = 0,
  ForVisibleRedeclaration,
  ForExternalRedeclaration
};

} // namespace neverc

#endif // NEVERC_ANALYZE_SEMANAMELOOKUPKINDS_H
