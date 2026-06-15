#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/sysctl.h>
#include <mach/mach.h>

static void print_uname_info(void) {
  struct utsname uts;
  if (uname(&uts) == 0) {
    printf("  sysname  = %s\n", uts.sysname);
    printf("  release  = %s\n", uts.release);
    printf("  version  = %s\n", uts.version);
    printf("  machine  = %s\n", uts.machine);
    printf("  nodename = %s\n", uts.nodename);
  }
}

static void print_sysctl_info(void) {
  char model[128] = {0};
  size_t len = sizeof(model);
  if (sysctlbyname("hw.model", model, &len, NULL, 0) == 0)
    printf("  model    = %s\n", model);

  int ncpu = 0;
  len = sizeof(ncpu);
  if (sysctlbyname("hw.ncpu", &ncpu, &len, NULL, 0) == 0)
    printf("  ncpu     = %d\n", ncpu);

  uint64_t memsize = 0;
  len = sizeof(memsize);
  if (sysctlbyname("hw.memsize", &memsize, &len, NULL, 0) == 0)
    printf("  memsize  = %llu MB\n", memsize / (1024 * 1024));

  int pagesize = 0;
  len = sizeof(pagesize);
  if (sysctlbyname("hw.pagesize", &pagesize, &len, NULL, 0) == 0)
    printf("  pagesize = %d\n", pagesize);
}

static void print_mach_info(void) {
  mach_port_t host = mach_host_self();

  host_basic_info_data_t hbi;
  mach_msg_type_number_t count = HOST_BASIC_INFO_COUNT;
  if (host_info(host, HOST_BASIC_INFO, (host_info_t)&hbi,
                &count) == KERN_SUCCESS) {
    printf("  max_cpus    = %d\n", hbi.max_cpus);
    printf("  avail_cpus  = %d\n", hbi.avail_cpus);
    printf("  cpu_type    = %d\n", hbi.cpu_type);
    printf("  cpu_subtype = %d\n", hbi.cpu_subtype);
  }

  struct task_basic_info tbi;
  count = TASK_BASIC_INFO_COUNT;
  if (task_info(mach_task_self(), TASK_BASIC_INFO, (task_info_t)&tbi,
                &count) == KERN_SUCCESS) {
    printf("  resident    = %lu KB\n",
           (unsigned long)(tbi.resident_size / 1024));
    printf("  virtual     = %lu KB\n",
           (unsigned long)(tbi.virtual_size / 1024));
  }
}

int main(int argc, char *argv[]) {
  printf("NeverC macOS Application\n");
  printf("========================\n");

  printf("\nKernel info (uname):\n");
  print_uname_info();

  printf("\nHardware info (sysctl):\n");
  print_sysctl_info();

  printf("\nProcess info:\n");
  printf("  pid      = %d\n", getpid());
  printf("  ppid     = %d\n", getppid());
  printf("  uid      = %u\n", getuid());
  printf("  euid     = %u\n", geteuid());
  printf("  argc     = %d\n", argc);
  printf("  argv[0]  = %s\n", argv[0]);

  printf("\nMach info:\n");
  print_mach_info();

  printf("\nBuild info:\n");
  printf("  sizeof(void*) = %zu\n", sizeof(void *));
#if defined(__aarch64__) || defined(__arm64__)
  printf("  arch          = arm64 (Apple Silicon)\n");
#elif defined(__x86_64__)
  printf("  arch          = x86_64 (Intel)\n");
#else
  printf("  arch          = unknown\n");
#endif

  printf("\nDone.\n");
  return 0;
}
