/*===- LockFileManager.c - Lock file operations (pure C) ------*- C -*-===*/
#include "include/csupport/llock_lfile_lmanager.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#ifndef _WIN32
#include <unistd.h>
#include <fcntl.h>
#else
#include <process.h>
#endif

int csupport_lock_file_create(const char *path) {
#ifndef _WIN32
  int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
  if (fd < 0) return errno == EEXIST ? 0 : -1;
  char buf[256];
  int n = snprintf(buf, sizeof(buf), "%d", (int)getpid());
  if (n > 0) {
    ssize_t w = write(fd, buf, (size_t)n);
    (void)w;
  }
  close(fd);
  return 1;
#else
  FILE *f = fopen(path, "wx");
  if (!f) return 0;
  fprintf(f, "%d", (int)_getpid());
  fclose(f);
  return 1;
#endif
}

int csupport_lock_file_remove(const char *path) {
  return remove(path) == 0;
}

int csupport_lock_file_exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0;
}

#ifdef __APPLE__
#include <uuid/uuid.h>
#endif
#ifndef _WIN32
#include <signal.h>
#endif

int csupport_get_host_id(char *buf, size_t buflen, size_t *out_len) {
  if (!buf || buflen == 0) return -1;
#if defined(__APPLE__)
  struct timespec wait = {1, 0};
  uuid_t uuid;
  if (gethostuuid(uuid, &wait) != 0) return errno;
  uuid_string_t ustr;
  uuid_unparse(uuid, ustr);
  size_t len = strlen(ustr);
  if (len >= buflen) len = buflen - 1;
  memcpy(buf, ustr, len);
  buf[len] = 0;
  if (out_len) *out_len = len;
#elif defined(_WIN32)
  memcpy(buf, "localhost", 9 < buflen ? 9 : buflen - 1);
  buf[9 < buflen ? 9 : buflen - 1] = 0;
  if (out_len) *out_len = 9 < buflen ? 9 : buflen - 1;
#else
  char hostname[256];
  hostname[0] = 0;
  gethostname(hostname, 255);
  hostname[255] = 0;
  size_t len = strlen(hostname);
  if (len >= buflen) len = buflen - 1;
  memcpy(buf, hostname, len);
  buf[len] = 0;
  if (out_len) *out_len = len;
#endif
  return 0;
}

int csupport_process_alive(int pid) {
#ifndef _WIN32
  return kill(pid, 0) == 0 || errno != ESRCH;
#else
  (void)pid;
  return 1;
#endif
}
