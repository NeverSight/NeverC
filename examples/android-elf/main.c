#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <math.h>

static void print_device_info(void) {
  struct utsname uts;
  if (uname(&uts) == 0) {
    printf("  sysname  = %s\n", uts.sysname);
    printf("  release  = %s\n", uts.release);
    printf("  version  = %s\n", uts.version);
    printf("  machine  = %s\n", uts.machine);
    printf("  nodename = %s\n", uts.nodename);
  }
}

static void check_root_status(void) {
  uid_t uid = getuid();
  uid_t euid = geteuid();
  printf("\nPrivilege info:\n");
  printf("  uid  = %u\n", uid);
  printf("  euid = %u\n", euid);
  printf("  root = %s\n", (euid == 0) ? "YES" : "NO");

  const char *paths[] = {
      "/system/bin/su", "/system/xbin/su", "/sbin/su", "/data/local/tmp"};
  printf("\nPath checks:\n");
  for (int i = 0; i < 4; ++i) {
    struct stat st;
    int exists = (stat(paths[i], &st) == 0);
    printf("  %-24s %s\n", paths[i], exists ? "EXISTS" : "not found");
  }
}

static void demo_dlopen(void) {
  printf("\nDynamic loading (dlopen):\n");

  void *handle = dlopen("liblog.so", RTLD_LAZY);
  if (handle) {
    typedef int (*log_fn)(int, const char *, const char *, ...);
    log_fn android_log = (log_fn)dlsym(handle, "__android_log_print");
    if (android_log) {
      printf("  __android_log_print found at %p\n", (void *)android_log);
      android_log(4 /* ANDROID_LOG_INFO */, "NeverC",
                  "Hello from NeverC ELF binary!");
    } else {
      printf("  __android_log_print not found: %s\n", dlerror());
    }
    dlclose(handle);
  } else {
    printf("  liblog.so: %s\n", dlerror());
  }
}

static void demo_proc_info(void) {
  printf("\nProcess info:\n");
  printf("  pid  = %d\n", getpid());
  printf("  ppid = %d\n", getppid());

  char buf[256];
  ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (len > 0) {
    buf[len] = '\0';
    printf("  exe  = %s\n", buf);
  }

  FILE *fp = fopen("/proc/self/maps", "r");
  if (fp) {
    printf("\nMemory maps (first 5 entries):\n");
    int count = 0;
    while (fgets(buf, sizeof(buf), fp) && count < 5) {
      printf("  %s", buf);
      count++;
    }
    fclose(fp);
  }
}

int main(int argc, char *argv[]) {
  printf("NeverC Android ELF — arm64 native binary\n");
  printf("==========================================\n");

  printf("\nDevice info:\n");
  print_device_info();

  printf("\nBuild info:\n");
  printf("  sizeof(void*) = %zu\n", sizeof(void *));
#if defined(__aarch64__)
  printf("  arch          = aarch64 (arm64-v8a)\n");
#else
  printf("  arch          = unknown\n");
#endif
  printf("  argc          = %d\n", argc);
  printf("  sqrt(2)       = %.10f\n", sqrt(2.0));

  check_root_status();
  demo_dlopen();
  demo_proc_info();

  printf("\nDone.\n");
  return 0;
}
