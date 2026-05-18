/*===- Chrono.c - Time utilities (pure C) ------------------------*- C -*-===*/
#include "include/csupport/lchrono.h"
#include <time.h>

void csupport_get_local_tm(int64_t epoch_sec, struct tm *out) {
  time_t t = (time_t)epoch_sec;
#ifdef _WIN32
  localtime_s(out, &t);
#else
  localtime_r(&t, out);
#endif
}

int csupport_should_use_mmap(size_t file_size, size_t map_size,
                              int64_t offset, int requires_null_term,
                              int page_size, int is_volatile) {
  if (is_volatile && requires_null_term)
    return 0;
  if (map_size < 4u * 4096u || map_size < (unsigned)page_size)
    return 0;
  if (!requires_null_term)
    return 1;
  if (file_size == (size_t)-1)
    return -1;
  size_t end = (size_t)offset + map_size;
  if (end != file_size)
    return 0;
  if ((file_size & ((size_t)page_size - 1)) == 0)
    return 0;
  return 1;
}
