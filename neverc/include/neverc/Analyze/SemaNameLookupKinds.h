#ifndef NEVERC_SEMA_SEMANAMELOOKUPKINDS_H
#define NEVERC_SEMA_SEMANAMELOOKUPKINDS_H

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

#endif // NEVERC_SEMA_SEMANAMELOOKUPKINDS_H
