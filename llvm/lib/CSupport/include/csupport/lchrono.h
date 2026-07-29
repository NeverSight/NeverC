#ifndef CSUPPORT_LCHRONO_H
#define CSUPPORT_LCHRONO_H
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Fill struct tm from epoch seconds. */
void csupport_get_local_tm(int64_t epoch_sec, struct tm *out);
void csupport_get_utc_tm(int64_t epoch_sec, struct tm *out);

/* Rewrite the sub-second extensions strftime does not know -- %L for
   milliseconds, %f for microseconds, %N for nanoseconds -- into the digits
   they stand for, leaving the rest of the format for strftime to read.  The
   format is the caller's, so its length is not bounded here.
   Buffer filler: see the contract on csupport_obuf_t in csupport/buffer.h. */
size_t csupport_expand_chrono_format(const char *style, size_t style_len,
                                     int64_t frac_ms, int64_t frac_us,
                                     int64_t frac_ns, char *out,
                                     size_t out_cap);

/* Determine whether mmap should be used for a memory buffer.
   Returns 1 = yes, 0 = no, -1 = need file size (caller must stat). */
int csupport_should_use_mmap(size_t file_size, size_t map_size, int64_t offset,
                             int requires_null_term, int page_size,
                             int is_volatile);

#ifdef __cplusplus
}
#endif
#endif
