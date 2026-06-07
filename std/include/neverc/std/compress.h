#ifndef NEVERC_COMPRESS_H
#define NEVERC_COMPRESS_H

/*
 * NeverC compress — umbrella header for compression submodules.
 */

#include "compress/lzw.h"
#include "compress/flate.h"
#include "compress/gzip.h"
#include "compress/zlib.h"
#include "compress/bzip2.h"

#ifdef __neverc__
struct __neverc_std_lzw_t { char __tag; };
struct __neverc_std_flate_t { char __tag; };
struct __neverc_std_gzip_t { char __tag; };
struct __neverc_std_zlib_t { char __tag; };
struct __neverc_std_bzip2_t { char __tag; };

struct __neverc_std_compress_t {
    struct __neverc_std_lzw_t lzw;
    struct __neverc_std_flate_t flate;
    struct __neverc_std_gzip_t gzip;
    struct __neverc_std_zlib_t zlib;
    struct __neverc_std_bzip2_t bzip2;
};
extern struct __neverc_std_compress_t __neverc_mod_compress;
extern struct __neverc_std_compress_t compress;
#endif

#endif
