/*===- Chrono.c - Time utilities (pure C) ------------------------*- C -*-===*/
#include "include/csupport/lchrono.h"
#include "include/csupport/buffer.h"
#include <time.h>

void csupport_get_local_tm(int64_t epoch_sec, struct tm *out) {
  time_t t = (time_t)epoch_sec;
#ifdef _WIN32
  localtime_s(out, &t);
#else
  localtime_r(&t, out);
#endif
}

void csupport_get_utc_tm(int64_t epoch_sec, struct tm *out) {
  time_t t = (time_t)epoch_sec;
#ifdef _WIN32
  gmtime_s(out, &t);
#else
  gmtime_r(&t, out);
#endif
}

size_t csupport_expand_chrono_format(const char *style, size_t style_len,
                                     int64_t frac_ms, int64_t frac_us,
                                     int64_t frac_ns, char *out,
                                     size_t out_cap) {
  csupport_obuf_t expanded = csupport_obuf(out, out_cap);
  for (size_t i = 0; i < style_len; ++i) {
    if (style[i] != '%' || i + 1 >= style_len) {
      csupport_obuf_put(&expanded, style[i]);
      continue;
    }
    switch (style[i + 1]) {
    case 'L': /* Milliseconds, from Ruby. */
      csupport_obuf_printf(&expanded, "%.3lld", (long long)frac_ms);
      break;
    case 'f': /* Microseconds, from Python. */
      csupport_obuf_printf(&expanded, "%.6lld", (long long)frac_us);
      break;
    case 'N': /* Nanoseconds, from date(1). */
      csupport_obuf_printf(&expanded, "%.9lld", (long long)frac_ns);
      break;
    case '%':
      /* Passed through whole, so that "%%f" reads as an escaped percent
         followed by an f rather than as a percent followed by "%f". */
      csupport_obuf_write(&expanded, "%%", 2);
      break;
    default:
      /* A specifier this does not know is strftime's to interpret. */
      csupport_obuf_put(&expanded, style[i]);
      continue;
    }
    ++i;
  }
  return csupport_obuf_finish(&expanded);
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
