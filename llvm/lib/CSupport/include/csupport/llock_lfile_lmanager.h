#ifndef CSUPPORT_LLOCK_LFILE_LMANAGER_H
#define CSUPPORT_LLOCK_LFILE_LMANAGER_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

int csupport_lock_file_create(const char *path);
int csupport_lock_file_remove(const char *path);
int csupport_lock_file_exists(const char *path);

/* Get host identifier for lock file ownership.
   On macOS: uses gethostuuid (hardware UUID).
   On Linux/Unix: uses gethostname.
   Returns 0 on success, errno on failure. */
int csupport_get_host_id(char *buf, size_t buflen, size_t *out_len);

/* Check if a process is still running (by PID). Returns 1 if running. */
int csupport_process_alive(int pid);

#ifdef __cplusplus
}
#endif
#endif
