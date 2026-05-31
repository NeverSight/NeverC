#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/mman.h>

#define EXPORT __attribute__((visibility("default")))

typedef int (*log_fn)(int, const char *, const char *, ...);
static log_fn s_log_print;

static void init_log(void) {
  if (s_log_print)
    return;
  void *h = dlopen("liblog.so", RTLD_LAZY);
  if (h)
    s_log_print = (log_fn)dlsym(h, "__android_log_print");
}

static void log_info(const char *msg) {
  init_log();
  if (s_log_print)
    s_log_print(4, "NeverCSO", "%s", msg);
}

EXPORT int nc_get_pid(void) {
  log_info("nc_get_pid called");
  return getpid();
}

EXPORT int nc_read_self_maps(char *buf, int buf_size) {
  log_info("nc_read_self_maps called");
  FILE *fp = fopen("/proc/self/maps", "r");
  if (!fp)
    return -1;

  int total = 0;
  char line[256];
  while (fgets(line, sizeof(line), fp) && total + (int)strlen(line) < buf_size) {
    int len = (int)strlen(line);
    memcpy(buf + total, line, len);
    total += len;
  }
  buf[total] = '\0';
  fclose(fp);
  return total;
}

EXPORT void *nc_alloc_rwx(int size) {
  log_info("nc_alloc_rwx called");
  void *p = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  return (p == MAP_FAILED) ? NULL : p;
}

EXPORT int nc_free_rwx(void *ptr, int size) {
  log_info("nc_free_rwx called");
  return munmap(ptr, size);
}

EXPORT void nc_xor_buffer(unsigned char *buf, int len, unsigned char key) {
  log_info("nc_xor_buffer called");
  for (int i = 0; i < len; ++i)
    buf[i] ^= key;
}

EXPORT const char *nc_version(void) { return "NeverC-SO 1.0"; }
