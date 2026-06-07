#ifndef NEVERC_ARCHIVE_H
#define NEVERC_ARCHIVE_H

/*
 * NeverC archive — umbrella header for archive submodules.
 */

#include "archive/tar.h"
#include "archive/zip.h"

#ifdef __neverc__
struct __neverc_std_tar_t { char __tag; };
struct __neverc_std_zip_t { char __tag; };

struct __neverc_std_archive_t {
    struct __neverc_std_tar_t tar;
    struct __neverc_std_zip_t zip;
};
extern struct __neverc_std_archive_t __neverc_mod_archive;
extern struct __neverc_std_archive_t archive;
#endif

#endif /* NEVERC_ARCHIVE_H */
