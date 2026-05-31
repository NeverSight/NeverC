#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/mman.h>
#include <signal.h>
#include <errno.h>

static volatile int g_signal_caught = 0;

static void signal_handler(int sig) {
  g_signal_caught = sig;
}

static void *thread_func(void *arg) {
  int id = *(int *)arg;
  printf("  [thread %d] started (tid concept)\n", id);

  volatile unsigned long sum = 0;
  for (unsigned long i = 0; i < 1000000; ++i)
    sum += i;
  printf("  [thread %d] sum = %lu\n", id, sum);

  return (void *)(uintptr_t)sum;
}

static void demo_threads(void) {
  printf("--- pthreads ---\n");

  enum { NUM_THREADS = 4 };
  pthread_t threads[NUM_THREADS];
  int ids[NUM_THREADS];

  for (int i = 0; i < NUM_THREADS; ++i) {
    ids[i] = i;
    if (pthread_create(&threads[i], NULL, thread_func, &ids[i]) != 0) {
      perror("pthread_create");
      return;
    }
  }

  for (int i = 0; i < NUM_THREADS; ++i) {
    void *retval;
    pthread_join(threads[i], &retval);
    printf("  [main] thread %d returned %lu\n", i, (unsigned long)(uintptr_t)retval);
  }
}

static void demo_mmap(void) {
  printf("--- mmap ---\n");

  size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
  printf("  page size = %zu\n", page_size);

  void *addr = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (addr == MAP_FAILED) {
    perror("mmap");
    return;
  }

  memset(addr, 0xAB, page_size);
  unsigned char *bytes = (unsigned char *)addr;
  printf("  mmap'd %zu bytes at %p, first byte = 0x%02X\n",
         page_size, addr, bytes[0]);

  if (munmap(addr, page_size) != 0)
    perror("munmap");
  else
    printf("  munmap OK\n");
}

static void demo_pipe(void) {
  printf("--- pipe ---\n");

  int fds[2];
  if (pipe(fds) != 0) {
    perror("pipe");
    return;
  }

  const char *msg = "Hello via pipe!";
  ssize_t written = write(fds[1], msg, strlen(msg));
  close(fds[1]);

  char buf[64];
  ssize_t nread = read(fds[0], buf, sizeof(buf) - 1);
  close(fds[0]);

  if (nread > 0) {
    buf[nread] = '\0';
    printf("  wrote %zd bytes, read back: \"%s\"\n", written, buf);
  }
}

static void demo_signal(void) {
  printf("--- signals ---\n");

  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = signal_handler;
  sigaction(SIGUSR1, &sa, NULL);

  raise(SIGUSR1);
  printf("  caught signal %d (SIGUSR1=%d) -> %s\n",
         g_signal_caught, SIGUSR1,
         g_signal_caught == SIGUSR1 ? "OK" : "FAIL");
}

int main(void) {
  printf("NeverC Linux POSIX demo\n\n");

  demo_threads();
  printf("\n");
  demo_mmap();
  printf("\n");
  demo_pipe();
  printf("\n");
  demo_signal();

  printf("\nAll demos completed.\n");
  return 0;
}
