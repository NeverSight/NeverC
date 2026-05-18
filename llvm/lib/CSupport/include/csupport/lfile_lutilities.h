#ifndef CSUPPORT_LFILE_LUTILITIES_H
#define CSUPPORT_LFILE_LUTILITIES_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

int csupport_files_differ(const char *path_a, const char *path_b);
int csupport_atomic_file_write(const char *path, const char *data, size_t len);

int csupport_is_number_char(char c);
const char *csupport_backup_number(const char *pos, const char *first_char);
const char *csupport_end_of_number(const char *pos);
int csupport_diff_numbers_tol(const char **f1p, const char **f2p,
                              const char *f1_end, const char *f2_end,
                              double abs_tolerance, double rel_tolerance);

/* Extended version: returns -1 if not a number, 0 if within tolerance,
   1 if out of tolerance. Outputs parsed values via out_v1/out_v2. */
int csupport_diff_numbers_tol_ex(const char **f1p, const char **f2p,
                                 const char *f1_end, const char *f2_end,
                                 double abs_tolerance, double rel_tolerance,
                                 double *out_v1, double *out_v2);

#ifdef __cplusplus
}
#endif
#endif
