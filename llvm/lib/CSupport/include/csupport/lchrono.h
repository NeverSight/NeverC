#ifndef CSUPPORT_LCHRONO_H
#define CSUPPORT_LCHRONO_H
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Fill struct tm from epoch seconds (local time). */
void csupport_get_local_tm(int64_t epoch_sec, struct tm *out);

/* Determine whether mmap should be used for a memory buffer.
   Returns 1 = yes, 0 = no, -1 = need file size (caller must stat). */
int csupport_should_use_mmap(size_t file_size, size_t map_size, int64_t offset,
                             int requires_null_term, int page_size,
                             int is_volatile);

#ifdef __cplusplus
}
#endif
#endif
