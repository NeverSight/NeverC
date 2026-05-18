/*===- Signals.c - Signal handling (pure C) ----------------------*- C -*-===*/
#include "include/csupport/lsignals.h"
#include "llvm/Config/llvm-config.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#ifdef __APPLE__
#include <sys/resource.h>
#endif
#ifdef _WIN32
#include <io.h>
#else
#include <sys/ioctl.h>
#endif

static void (*g_interrupt_func)(void) = NULL;

void csupport_set_interrupt_function(void (*fn)(void)) {
  g_interrupt_func = fn;
}

void csupport_run_interrupt_handlers(void) {
  if (g_interrupt_func)
    g_interrupt_func();
}

#ifdef LLVM_ON_UNIX
#include <unistd.h>
#include <execinfo.h>
#if defined(HAVE_LINK_H)
#include <link.h>
#include <elf.h>
#endif

int csupport_backtrace(void **buffer, int size) {
#if defined(__APPLE__) || defined(__linux__)
  return backtrace(buffer, size);
#else
  (void)buffer; (void)size;
  return 0;
#endif
}

void csupport_print_stack_trace_fd(int fd) {
  void *trace[128];
  int n = csupport_backtrace(trace, 128);
  if (n > 0) {
#if defined(__APPLE__) || defined(__linux__)
    backtrace_symbols_fd(trace, n, fd);
#else
    for (int i = 0; i < n; i++) {
      char buf[32];
      int len = snprintf(buf, sizeof(buf), "  [%d] %p\n", i, trace[i]);
      if (len > 0) write(fd, buf, (size_t)len);
    }
#endif
  }
}

void csupport_disable_system_dialogs_on_crash(void) {
  /* On Unix, nothing to do - crash reports are handled by signals */
}

int csupport_register_signal_handler(int signo, csupport_signal_handler_t handler,
                                     void *cookie) {
  (void)handler; (void)cookie;
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = SIG_DFL;
  sigemptyset(&sa.sa_mask);
  if (sigaction(signo, &sa, NULL) < 0)
    return -1;
  return 0;
}

#else

int csupport_backtrace(void **buffer, int size) {
  (void)buffer; (void)size;
  return 0;
}

void csupport_print_stack_trace_fd(int fd) {
  (void)fd;
}

void csupport_disable_system_dialogs_on_crash(void) {}

int csupport_register_signal_handler(int signo, csupport_signal_handler_t handler,
                                     void *cookie) {
  (void)signo; (void)handler; (void)cookie;
  return -1;
}

#endif

int csupport_format_ptr_hex(char *buf, size_t buflen, const void *ptr) {
  return snprintf(buf, buflen, "0x%0*" PRIxPTR,
                  (int)(2 * sizeof(void *)), (uintptr_t)ptr);
}

void csupport_set_thread_background_priority(void) {
#ifdef __APPLE__
  setpriority(PRIO_DARWIN_THREAD, 0, PRIO_DARWIN_BG);
#endif
}

int csupport_has_thread_background_priority(void) {
#ifdef __APPLE__
  return getpriority(PRIO_DARWIN_THREAD, 0) == 1;
#else
  return 0;
#endif
}

int csupport_is_interactive_session(void) {
#if defined(_WIN32)
  return _isatty(_fileno(stdin));
#else
  return isatty(STDIN_FILENO);
#endif
}

int csupport_get_terminal_width(void) {
#if defined(_WIN32)
  return 80;
#elif defined(TIOCGWINSZ)
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
    return ws.ws_col;
  return 80;
#else
  return 80;
#endif
}

int csupport_signal_name(int signo, char *buf, size_t buflen) {
  if (!buf || buflen == 0) return 0;
  const char *name = 0;
  switch (signo) {
#ifdef SIGABRT
  case SIGABRT: name = "SIGABRT"; break;
#endif
#ifdef SIGBUS
  case SIGBUS: name = "SIGBUS"; break;
#endif
#ifdef SIGFPE
  case SIGFPE: name = "SIGFPE"; break;
#endif
#ifdef SIGILL
  case SIGILL: name = "SIGILL"; break;
#endif
#ifdef SIGSEGV
  case SIGSEGV: name = "SIGSEGV"; break;
#endif
#ifdef SIGTRAP
  case SIGTRAP: name = "SIGTRAP"; break;
#endif
#ifdef SIGPIPE
  case SIGPIPE: name = "SIGPIPE"; break;
#endif
  default: break;
  }
  if (name) {
    size_t len = strlen(name);
    if (len >= buflen) len = buflen - 1;
    memcpy(buf, name, len);
    buf[len] = '\0';
    return (int)len;
  }
  return snprintf(buf, buflen, "signal %d", signo);
}

int csupport_is_crash_signal(int signo) {
#ifdef SIGABRT
  if (signo == SIGABRT) return 1;
#endif
#ifdef SIGBUS
  if (signo == SIGBUS) return 1;
#endif
#ifdef SIGFPE
  if (signo == SIGFPE) return 1;
#endif
#ifdef SIGILL
  if (signo == SIGILL) return 1;
#endif
#ifdef SIGSEGV
  if (signo == SIGSEGV) return 1;
#endif
#ifdef SIGTRAP
  if (signo == SIGTRAP) return 1;
#endif
  return 0;
}

void csupport_block_signals(void) {
#ifdef LLVM_ON_UNIX
  sigset_t set;
  sigfillset(&set);
  sigprocmask(SIG_BLOCK, &set, 0);
#endif
}

void csupport_unblock_signals(void) {
#ifdef LLVM_ON_UNIX
  sigset_t set;
  sigfillset(&set);
  sigprocmask(SIG_UNBLOCK, &set, 0);
#endif
}

int csupport_format_crash_backtrace(char *buf, size_t buflen,
                                     void **addrs, int depth) {
  if (!buf || buflen == 0 || !addrs || depth <= 0) return 0;
  int pos = 0;
  for (int i = 0; i < depth && (size_t)pos < buflen - 1; i++) {
    int n = snprintf(buf + pos, buflen - (size_t)pos,
                     "#%d %p\n", i, addrs[i]);
    if (n > 0) pos += n;
  }
  return pos;
}

int csupport_write_crash_log_msg(char *buf, size_t buflen, const char *prefix,
                                  const char *msg) {
  if (!buf || buflen == 0) return 0;
  int n = snprintf(buf, buflen, "%s%s\n", prefix ? prefix : "", msg ? msg : "");
  return (n > 0 && (size_t)n < buflen) ? n : (int)(buflen - 1);
}

int csupport_format_frame_address(char *buf, size_t cap, int frame_no,
                                    const void *addr) {
  if (!buf || cap == 0) return 0;
  return snprintf(buf, cap, "#%-2d %p", frame_no, addr);
}

int csupport_format_frame_full(char *buf, size_t cap, int frame_no,
                                 const void *addr, const char *fname,
                                 const char *symbol, unsigned offset) {
  if (!buf || cap == 0) return 0;
  if (symbol && fname)
    return snprintf(buf, cap, "#%-2d %p %s (%s+0x%x)", frame_no, addr,
                    fname, symbol, offset);
  if (fname)
    return snprintf(buf, cap, "#%-2d %p %s", frame_no, addr, fname);
  return snprintf(buf, cap, "#%-2d %p", frame_no, addr);
}

size_t csupport_format_ptr_hex_full(char *buf, size_t cap, const void *ptr) {
  if (!buf || cap == 0) return 0;
  size_t n = (size_t)snprintf(buf, cap, "0x%0*" PRIxPTR,
                              (int)(2 * sizeof(void *)), (uintptr_t)ptr);
  return n < cap ? n : cap - 1;
}

/* ===================================================================== */
/* Session 13: Signal-safe file removal linked list (pure C with atomics) */
/* ===================================================================== */

#ifdef LLVM_ON_UNIX
#include <sys/stat.h>
#include <stdlib.h>

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_ATOMICS__)
#include <stdatomic.h>
#define ATOMIC_PTR(T) _Atomic(T)
#define ATOMIC_LOAD(p) atomic_load(p)
#define ATOMIC_STORE(p, v) atomic_store(p, v)
#define ATOMIC_EXCHANGE(p, v) atomic_exchange(p, v)
#define ATOMIC_CAS(p, expected, desired) \
  atomic_compare_exchange_strong(p, expected, desired)
#else
#define ATOMIC_PTR(T) T volatile
#define ATOMIC_LOAD(p) __atomic_load_n(p, __ATOMIC_SEQ_CST)
#define ATOMIC_STORE(p, v) __atomic_store_n(p, v, __ATOMIC_SEQ_CST)
#define ATOMIC_EXCHANGE(p, v) __atomic_exchange_n(p, v, __ATOMIC_SEQ_CST)
#define ATOMIC_CAS(p, expected, desired) \
  __atomic_compare_exchange_n(p, expected, desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)
#endif

typedef struct csupport_file_node {
  ATOMIC_PTR(char *) filename;
  ATOMIC_PTR(struct csupport_file_node *) next;
} csupport_file_node_t;

static ATOMIC_PTR(csupport_file_node_t *) g_files_to_remove = NULL;

void csupport_file_remove_list_insert(const char *filename) {
  if (!filename) return;
  csupport_file_node_t *node =
      (csupport_file_node_t *)calloc(1, sizeof(csupport_file_node_t));
  if (!node) return;
  ATOMIC_STORE(&node->filename, strdup(filename));
  ATOMIC_STORE(&node->next, (csupport_file_node_t *)NULL);

  ATOMIC_PTR(csupport_file_node_t *) *insert_point = &g_files_to_remove;
  csupport_file_node_t *old = NULL;
  while (!ATOMIC_CAS(insert_point, &old, node)) {
    insert_point = &old->next;
    old = NULL;
  }
}

void csupport_file_remove_list_erase(const char *filename) {
  if (!filename) return;
  csupport_file_node_t *cur = ATOMIC_LOAD(&g_files_to_remove);
  for (; cur; cur = ATOMIC_LOAD(&cur->next)) {
    char *old_fn = ATOMIC_LOAD(&cur->filename);
    if (old_fn && strcmp(old_fn, filename) == 0) {
      old_fn = ATOMIC_EXCHANGE(&cur->filename, (char *)NULL);
      if (old_fn) free(old_fn);
    }
  }
}

void csupport_file_remove_list_remove_all(void) {
  csupport_file_node_t *head =
      ATOMIC_EXCHANGE(&g_files_to_remove, (csupport_file_node_t *)NULL);

  for (csupport_file_node_t *cur = head; cur;
       cur = ATOMIC_LOAD(&cur->next)) {
    char *path = ATOMIC_EXCHANGE(&cur->filename, (char *)NULL);
    if (path) {
      struct stat buf;
      if (stat(path, &buf) == 0 && S_ISREG(buf.st_mode))
        unlink(path);
      ATOMIC_EXCHANGE(&cur->filename, path);
    }
  }

  ATOMIC_EXCHANGE(&g_files_to_remove, head);
}

void csupport_file_remove_list_cleanup(void) {
  csupport_file_node_t *head =
      ATOMIC_EXCHANGE(&g_files_to_remove, (csupport_file_node_t *)NULL);
  while (head) {
    csupport_file_node_t *next = ATOMIC_LOAD(&head->next);
    char *fn = ATOMIC_EXCHANGE(&head->filename, (char *)NULL);
    if (fn) free(fn);
    free(head);
    head = next;
  }
}

/* ===================================================================== */
/* Session 13: Signal handler registration (pure C sigaction)            */
/* ===================================================================== */

static unsigned g_num_registered = 0;
static struct {
  struct sigaction sa;
  int signo;
} g_registered_signals[CSUPPORT_MAX_REGISTERED_SIGNALS];

int csupport_register_signal_handlers(const int *kill_sigs, int num_kill,
                                      const int *int_sigs, int num_int,
                                      const int *info_sigs, int num_info,
                                      int pipe_sig,
                                      csupport_sig_handler_fn kill_handler,
                                      csupport_sig_handler_fn info_handler) {
  if (g_num_registered != 0) return (int)g_num_registered;

  csupport_setup_sig_alt_stack();

  unsigned idx = 0;
  struct sigaction new_act;

  for (int i = 0; i < num_int && idx < CSUPPORT_MAX_REGISTERED_SIGNALS; i++) {
    memset(&new_act, 0, sizeof(new_act));
    new_act.sa_handler = kill_handler;
    new_act.sa_flags = SA_NODEFER | SA_RESETHAND | SA_ONSTACK;
    sigemptyset(&new_act.sa_mask);
    sigaction(int_sigs[i], &new_act, &g_registered_signals[idx].sa);
    g_registered_signals[idx].signo = int_sigs[i];
    idx++;
  }

  for (int i = 0; i < num_kill && idx < CSUPPORT_MAX_REGISTERED_SIGNALS; i++) {
    memset(&new_act, 0, sizeof(new_act));
    new_act.sa_handler = kill_handler;
    new_act.sa_flags = SA_NODEFER | SA_RESETHAND | SA_ONSTACK;
    sigemptyset(&new_act.sa_mask);
    sigaction(kill_sigs[i], &new_act, &g_registered_signals[idx].sa);
    g_registered_signals[idx].signo = kill_sigs[i];
    idx++;
  }

  if (pipe_sig > 0 && idx < CSUPPORT_MAX_REGISTERED_SIGNALS) {
    memset(&new_act, 0, sizeof(new_act));
    new_act.sa_handler = kill_handler;
    new_act.sa_flags = SA_NODEFER | SA_RESETHAND | SA_ONSTACK;
    sigemptyset(&new_act.sa_mask);
    sigaction(pipe_sig, &new_act, &g_registered_signals[idx].sa);
    g_registered_signals[idx].signo = pipe_sig;
    idx++;
  }

  for (int i = 0; i < num_info && idx < CSUPPORT_MAX_REGISTERED_SIGNALS; i++) {
    memset(&new_act, 0, sizeof(new_act));
    new_act.sa_handler = info_handler;
    new_act.sa_flags = SA_ONSTACK;
    sigemptyset(&new_act.sa_mask);
    sigaction(info_sigs[i], &new_act, &g_registered_signals[idx].sa);
    g_registered_signals[idx].signo = info_sigs[i];
    idx++;
  }

  g_num_registered = idx;
  return (int)idx;
}

void csupport_unregister_signal_handlers(void) {
  for (unsigned i = 0; i < g_num_registered; i++) {
    sigaction(g_registered_signals[i].signo,
              &g_registered_signals[i].sa, NULL);
  }
  g_num_registered = 0;
}

extern void csupport_create_sig_alt_stack(void);
void csupport_setup_sig_alt_stack(void) {
  csupport_create_sig_alt_stack();
}

/* Atomic function pointer storage for interrupt/info/pipe handlers */
static ATOMIC_PTR(void (*)(void)) g_atomic_interrupt_fn = NULL;
static ATOMIC_PTR(void (*)(void)) g_atomic_info_fn = NULL;
static ATOMIC_PTR(void (*)(void)) g_atomic_pipe_fn = NULL;

void csupport_set_atomic_interrupt_fn(void (*fn)(void)) {
  ATOMIC_EXCHANGE(&g_atomic_interrupt_fn, fn);
}

void *csupport_get_atomic_interrupt_fn(void) {
  return (void *)(uintptr_t)ATOMIC_LOAD(&g_atomic_interrupt_fn);
}

void csupport_set_atomic_info_fn(void (*fn)(void)) {
  ATOMIC_EXCHANGE(&g_atomic_info_fn, fn);
}

void *csupport_get_atomic_info_fn(void) {
  return (void *)(uintptr_t)ATOMIC_LOAD(&g_atomic_info_fn);
}

void csupport_set_atomic_pipe_fn(void (*fn)(void)) {
  ATOMIC_EXCHANGE(&g_atomic_pipe_fn, fn);
}

void *csupport_get_atomic_pipe_fn(void) {
  return (void *)(uintptr_t)ATOMIC_LOAD(&g_atomic_pipe_fn);
}

void *csupport_exchange_atomic_pipe_fn(void (*fn)(void)) {
  return (void *)(uintptr_t)ATOMIC_EXCHANGE(&g_atomic_pipe_fn, fn);
}

/* --- Session 14: _Unwind_Backtrace-based stack unwinding (glibc only) --- */
#if defined(HAVE__UNWIND_BACKTRACE) && defined(__GLIBC__)
#include <unwind.h>

typedef struct {
  void **stack_trace;
  int max_entries;
  int entries;
} csupport_unwind_ctx_t;

static _Unwind_Reason_Code csupport_unwind_cb(_Unwind_Context *ctx, void *arg) {
  csupport_unwind_ctx_t *data = (csupport_unwind_ctx_t *)arg;
  void *ip = (void *)_Unwind_GetIP(ctx);
  if (!ip) return _URC_END_OF_STACK;
  if (data->entries >= 0)
    data->stack_trace[data->entries] = ip;
  if (++data->entries == data->max_entries)
    return _URC_END_OF_STACK;
  return _URC_NO_REASON;
}

int csupport_unwind_backtrace(void **stack_trace, int max_entries) {
  if (max_entries < 0) return 0;
  csupport_unwind_ctx_t ctx;
  ctx.stack_trace = stack_trace;
  ctx.max_entries = max_entries;
  ctx.entries = -1;
  _Unwind_Backtrace(csupport_unwind_cb, &ctx);
  return ctx.entries > 0 ? ctx.entries : 0;
}
#else
int csupport_unwind_backtrace(void **stack_trace, int max_entries) {
  (void)stack_trace; (void)max_entries;
  return 0;
}
#endif

/* --- Session 14: dladdr column width calculation --- */
#if HAVE_DLFCN_H && HAVE_DLADDR
#include <dlfcn.h>

int csupport_dladdr_max_width(void **stack_trace, int depth) {
  int width = 0;
  for (int i = 0; i < depth; ++i) {
    Dl_info dli;
    dladdr(stack_trace[i], &dli);
    const char *slash = strrchr(dli.dli_fname, '/');
    int nw = slash ? (int)strlen(slash) - 1 : (int)strlen(dli.dli_fname);
    if (nw > width) width = nw;
  }
  return width;
}
#else
int csupport_dladdr_max_width(void **stack_trace, int depth) {
  (void)stack_trace; (void)depth;
  return 0;
}
#endif

/* --- Session 14: ELF Build ID finder + mode string (pure C) --- */
#if defined(HAVE_LINK_H)
#ifndef PT_NOTE
#define PT_NOTE 4
#endif
#ifndef PF_R
#define PF_R 4
#define PF_W 2
#define PF_X 1
#endif

static uintptr_t csupport_align4(uintptr_t v) { return (v + 3) & ~(uintptr_t)3; }

const uint8_t *csupport_find_elf_build_id(uintptr_t dlpi_addr,
                                          const void *phdrs, int phnum,
                                          size_t phent_size,
                                          size_t *out_len) {
  if (!phdrs || !out_len) return NULL;
  *out_len = 0;
  for (int i = 0; i < phnum; i++) {
    const ElfW(Phdr) *phdr =
        (const ElfW(Phdr) *)((const char *)phdrs + (size_t)i * phent_size);
    if (phdr->p_type != PT_NOTE)
      continue;
    const uint8_t *notes = (const uint8_t *)(dlpi_addr + phdr->p_vaddr);
    size_t notes_sz = phdr->p_memsz;
    while (notes_sz > 12) {
      uint32_t name_sz = *(const uint32_t *)notes;
      uint32_t desc_sz = *(const uint32_t *)(notes + 4);
      uint32_t type    = *(const uint32_t *)(notes + 8);
      notes += 12; notes_sz -= 12;

      uintptr_t cur = (uintptr_t)notes;
      uint32_t padded_name = (uint32_t)(csupport_align4(cur + name_sz) - cur);
      if (padded_name >= notes_sz) break;
      const uint8_t *name_data = notes;
      notes += padded_name; notes_sz -= padded_name;

      cur = (uintptr_t)notes;
      uint32_t padded_desc = (uint32_t)(csupport_align4(cur + desc_sz) - cur);
      if (padded_desc > notes_sz) break;
      const uint8_t *desc_data = notes;
      notes += padded_desc; notes_sz -= padded_desc;

      if (type == 3 && name_sz >= 3 &&
          name_data[0] == 'G' && name_data[1] == 'N' && name_data[2] == 'U') {
        *out_len = desc_sz;
        return desc_data;
      }
    }
  }
  return NULL;
}

void csupport_mode_str_from_flags(uint32_t flags, char out[4]) {
  char *p = out;
  if (flags & PF_R) *p++ = 'r';
  if (flags & PF_W) *p++ = 'w';
  if (flags & PF_X) *p++ = 'x';
  *p = '\0';
}
#else
const uint8_t *csupport_find_elf_build_id(uintptr_t dlpi_addr,
                                          const void *phdrs, int phnum,
                                          size_t phent_size,
                                          size_t *out_len) {
  (void)dlpi_addr; (void)phdrs; (void)phnum; (void)phent_size;
  if (out_len) *out_len = 0;
  return NULL;
}
void csupport_mode_str_from_flags(uint32_t flags, char out[4]) {
  (void)flags;
  out[0] = '\0';
}
#endif

#if defined(HAVE_LINK_H) &&                                                \
    (defined(__linux__) || defined(__FreeBSD__) ||                        \
     defined(__FreeBSD_kernel__) || defined(__NetBSD__))
typedef struct {
  void **stack_trace;
  int depth;
  int first;
  const char **modules;
  intptr_t *offsets;
  const char *main_exec_name;
} csupport_dl_iterate_data_t;

static int csupport_dl_iterate_cb(struct dl_phdr_info *info, size_t size,
                                  void *arg) {
  (void)size;
  csupport_dl_iterate_data_t *data = (csupport_dl_iterate_data_t *)arg;
  const char *name = data->first ? data->main_exec_name : info->dlpi_name;
  data->first = 0;

  for (int i = 0; i < info->dlpi_phnum; i++) {
    const ElfW(Phdr) *phdr = &info->dlpi_phdr[i];
    if (phdr->p_type != PT_LOAD)
      continue;
    intptr_t beg = (intptr_t)info->dlpi_addr + (intptr_t)phdr->p_vaddr;
    intptr_t end = beg + (intptr_t)phdr->p_memsz;
    for (int j = 0; j < data->depth; j++) {
      if (data->modules[j])
        continue;
      intptr_t addr = (intptr_t)data->stack_trace[j];
      if (beg <= addr && addr < end) {
        data->modules[j] = name;
        data->offsets[j] = addr - (intptr_t)info->dlpi_addr;
      }
    }
  }
  return 0;
}

int csupport_find_modules_offsets_elf(void **stack_trace, int depth,
                                      const char **modules, intptr_t *offsets,
                                      const char *main_executable_name) {
  csupport_dl_iterate_data_t data;
  data.stack_trace = stack_trace;
  data.depth = depth;
  data.first = 1;
  data.modules = modules;
  data.offsets = offsets;
  data.main_exec_name = main_executable_name;
  dl_iterate_phdr(csupport_dl_iterate_cb, &data);
  return 1;
}
#endif

/* ===================================================================== */
/* Session 15: Signal callback system (replaces C++ CallbackAndCookie)    */
/* ===================================================================== */

#define CSUPPORT_MAX_SIGNAL_CALLBACKS 8

enum csupport_cb_status {
  CSUPPORT_CB_EMPTY = 0,
  CSUPPORT_CB_INITIALIZING = 1,
  CSUPPORT_CB_INITIALIZED = 2,
  CSUPPORT_CB_EXECUTING = 3
};

static struct {
  csupport_signal_callback_fn callback;
  void *cookie;
  int flag; /* atomic, stores csupport_cb_status values */
} g_signal_callbacks[CSUPPORT_MAX_SIGNAL_CALLBACKS];

void csupport_run_signal_callbacks(void) {
  for (int i = 0; i < CSUPPORT_MAX_SIGNAL_CALLBACKS; i++) {
    int expected = CSUPPORT_CB_INITIALIZED;
    int desired = CSUPPORT_CB_EXECUTING;
    if (!__atomic_compare_exchange_n(&g_signal_callbacks[i].flag, &expected,
                                     desired, 0, __ATOMIC_SEQ_CST,
                                     __ATOMIC_SEQ_CST))
      continue;
    g_signal_callbacks[i].callback(g_signal_callbacks[i].cookie);
    g_signal_callbacks[i].callback = NULL;
    g_signal_callbacks[i].cookie = NULL;
    __atomic_store_n(&g_signal_callbacks[i].flag, CSUPPORT_CB_EMPTY,
                     __ATOMIC_SEQ_CST);
  }
}

void csupport_insert_signal_callback(csupport_signal_callback_fn fn,
                                     void *cookie) {
  for (int i = 0; i < CSUPPORT_MAX_SIGNAL_CALLBACKS; i++) {
    int expected = CSUPPORT_CB_EMPTY;
    int desired = CSUPPORT_CB_INITIALIZING;
    if (!__atomic_compare_exchange_n(&g_signal_callbacks[i].flag, &expected,
                                     desired, 0, __ATOMIC_SEQ_CST,
                                     __ATOMIC_SEQ_CST))
      continue;
    g_signal_callbacks[i].callback = fn;
    g_signal_callbacks[i].cookie = cookie;
    __atomic_store_n(&g_signal_callbacks[i].flag, CSUPPORT_CB_INITIALIZED,
                     __ATOMIC_SEQ_CST);
    return;
  }
}

/* ===================================================================== */
/* Session 15: Unix signal handler core (replaces Signals.inc C++ logic) */
/* ===================================================================== */

static const int g_int_sigs[] = {SIGHUP, SIGINT, SIGTERM, SIGUSR2};
static const int g_kill_sigs[] = {
    SIGILL, SIGTRAP, SIGABRT, SIGFPE, SIGBUS, SIGSEGV, SIGQUIT
#ifdef SIGSYS
    , SIGSYS
#endif
#ifdef SIGXCPU
    , SIGXCPU
#endif
#ifdef SIGXFSZ
    , SIGXFSZ
#endif
#ifdef SIGEMT
    , SIGEMT
#endif
};
static const int g_info_sigs[] = {
    SIGUSR1
#ifdef SIGINFO
    , SIGINFO
#endif
};

static int sig_in_set(int sig, const int *set, int count) {
  for (int i = 0; i < count; i++)
    if (set[i] == sig) return 1;
  return 0;
}

int csupport_is_int_signal(int sig) {
  return sig_in_set(sig, g_int_sigs,
                    (int)(sizeof(g_int_sigs) / sizeof(g_int_sigs[0])));
}

int csupport_is_kill_signal(int sig) {
  return sig_in_set(sig, g_kill_sigs,
                    (int)(sizeof(g_kill_sigs) / sizeof(g_kill_sigs[0])));
}

int csupport_is_info_signal(int sig) {
  return sig_in_set(sig, g_info_sigs,
                    (int)(sizeof(g_info_sigs) / sizeof(g_info_sigs[0])));
}

const int *csupport_get_kill_sigs(int *out_count) {
  if (out_count)
    *out_count = (int)(sizeof(g_kill_sigs) / sizeof(g_kill_sigs[0]));
  return g_kill_sigs;
}

const int *csupport_get_int_sigs(int *out_count) {
  if (out_count)
    *out_count = (int)(sizeof(g_int_sigs) / sizeof(g_int_sigs[0]));
  return g_int_sigs;
}

const int *csupport_get_info_sigs(int *out_count) {
  if (out_count)
    *out_count = (int)(sizeof(g_info_sigs) / sizeof(g_info_sigs[0]));
  return g_info_sigs;
}

void csupport_unix_signal_handler(int sig) {
  csupport_unregister_signal_handlers();

  sigset_t mask;
  sigfillset(&mask);
  sigprocmask(SIG_UNBLOCK, &mask, NULL);

  csupport_file_remove_list_remove_all();

  if (sig == SIGPIPE) {
    typedef void (*fn_t)(void);
    fn_t old_pipe = (fn_t)csupport_exchange_atomic_pipe_fn(NULL);
    if (old_pipe) { old_pipe(); return; }
  }

  int is_int = csupport_is_int_signal(sig);
  if (is_int) {
    typedef void (*fn_t)(void);
    fn_t old_int = (fn_t)csupport_get_atomic_interrupt_fn();
    if (old_int) {
      csupport_set_atomic_interrupt_fn(NULL);
      old_int();
      return;
    }
  }

  if (sig == SIGPIPE || is_int) {
    raise(sig);
    return;
  }

  csupport_run_signal_callbacks();

#ifdef __s390__
  if (sig == SIGILL || sig == SIGFPE || sig == SIGTRAP)
    raise(sig);
#endif
}

void csupport_unix_info_signal_handler(int sig) {
  (void)sig;
  int saved_errno = errno;
  typedef void (*fn_t)(void);
  fn_t fn = (fn_t)csupport_get_atomic_info_fn();
  if (fn) fn();
  errno = saved_errno;
}

void csupport_unix_cleanup_on_signal(int sig) {
  if (csupport_is_info_signal(sig)) {
    csupport_unix_info_signal_handler(sig);
    return;
  }
  csupport_file_remove_list_remove_all();
  if (csupport_is_int_signal(sig) || sig == SIGPIPE)
    return;
  csupport_run_signal_callbacks();
}

#include <pthread.h>

static pthread_mutex_t g_register_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_all_handlers_registered = 0;

void csupport_unix_register_all_handlers(void) {
  pthread_mutex_lock(&g_register_mutex);
  if (g_all_handlers_registered) {
    pthread_mutex_unlock(&g_register_mutex);
    return;
  }
  int pipe_sig = csupport_get_atomic_pipe_fn() ? SIGPIPE : 0;
  int nk, ni, nf;
  const int *ks = csupport_get_kill_sigs(&nk);
  const int *is2 = csupport_get_int_sigs(&ni);
  const int *fs = csupport_get_info_sigs(&nf);
  csupport_register_signal_handlers(ks, nk, is2, ni, fs, nf, pipe_sig,
                                    csupport_unix_signal_handler,
                                    csupport_unix_info_signal_handler);
  g_all_handlers_registered = 1;
  pthread_mutex_unlock(&g_register_mutex);
}

void csupport_unix_unregister_all_handlers(void) {
  csupport_unregister_signal_handlers();
  g_all_handlers_registered = 0;
}

/* ===================================================================== */
/* Session 15: PrintStackTrace dladdr loop + DSOMarkupPrinter (pure C)   */
/* ===================================================================== */

#if HAVE_DLFCN_H && HAVE_DLADDR

void csupport_print_stack_trace_dladdr(void *ctx, csupport_write_fn_t write_fn,
                                       void **stack_trace, int depth,
                                       char *(*demangle_fn)(const char *)) {
  int width = csupport_dladdr_max_width(stack_trace, depth);

  for (int i = 0; i < depth; ++i) {
    Dl_info dlinfo;
    dladdr(stack_trace[i], &dlinfo);

    char buf[512];
    int n;

    n = snprintf(buf, sizeof(buf), "%-2d", i);
    if (n > 0) write_fn(ctx, buf, (size_t)n);

    const char *name = strrchr(dlinfo.dli_fname, '/');
    n = snprintf(buf, sizeof(buf), " %-*s", width,
                 name ? name + 1 : dlinfo.dli_fname);
    if (n > 0) write_fn(ctx, buf, (size_t)n);

    n = snprintf(buf, sizeof(buf), " %#0*lx",
                 (int)(sizeof(void *) * 2) + 2,
                 (unsigned long)(uintptr_t)stack_trace[i]);
    if (n > 0) write_fn(ctx, buf, (size_t)n);

    if (dlinfo.dli_sname != NULL) {
      write_fn(ctx, " ", 1);
      if (demangle_fn) {
        char *d = demangle_fn(dlinfo.dli_sname);
        if (d) {
          write_fn(ctx, d, strlen(d));
          free(d);
        } else {
          write_fn(ctx, dlinfo.dli_sname, strlen(dlinfo.dli_sname));
        }
      } else {
        write_fn(ctx, dlinfo.dli_sname, strlen(dlinfo.dli_sname));
      }

      ptrdiff_t offset = (const char *)stack_trace[i] -
                          (const char *)dlinfo.dli_saddr;
      n = snprintf(buf, sizeof(buf), " + %td", offset);
      if (n > 0) write_fn(ctx, buf, (size_t)n);
    }
    write_fn(ctx, "\n", 1);
  }
}

#else

void csupport_print_stack_trace_dladdr(void *ctx, csupport_write_fn_t write_fn,
                                       void **stack_trace, int depth,
                                       char *(*demangle_fn)(const char *)) {
  (void)ctx; (void)write_fn; (void)stack_trace; (void)depth;
  (void)demangle_fn;
}

#endif /* HAVE_DLFCN_H && HAVE_DLADDR */

#if defined(HAVE_LINK_H) && ENABLE_BACKTRACES &&                            \
    (defined(__linux__) || defined(__FreeBSD__) ||                           \
     defined(__FreeBSD_kernel__) || defined(__NetBSD__))

typedef struct {
  void *ctx;
  csupport_write_fn_t write_fn;
  const char *main_executable_name;
  size_t module_count;
  int is_first;
} csupport_dso_markup_ctx_t;

static int csupport_dso_markup_cb(struct dl_phdr_info *info, size_t size,
                                  void *arg) {
  (void)size;
  csupport_dso_markup_ctx_t *data = (csupport_dso_markup_ctx_t *)arg;
  size_t bid_len = 0;
  const uint8_t *bid = csupport_find_elf_build_id(
      info->dlpi_addr, info->dlpi_phdr, info->dlpi_phnum,
      sizeof(info->dlpi_phdr[0]), &bid_len);
  if (!bid || bid_len == 0)
    return 0;

  char buf[1024];
  const char *name = data->is_first ? data->main_executable_name
                                    : info->dlpi_name;
  int n = snprintf(buf, sizeof(buf), "{{{module:%zu:%s:elf:",
                   data->module_count, name);
  if (n > 0) data->write_fn(data->ctx, buf, (size_t)n);

  for (size_t i = 0; i < bid_len; i++) {
    n = snprintf(buf, sizeof(buf), "%02x", bid[i]);
    if (n > 0) data->write_fn(data->ctx, buf, (size_t)n);
  }
  data->write_fn(data->ctx, "}}}\n", 4);

  for (int i = 0; i < info->dlpi_phnum; i++) {
    const ElfW(Phdr) *phdr = &info->dlpi_phdr[i];
    if (phdr->p_type != PT_LOAD)
      continue;
    uintptr_t start_addr = info->dlpi_addr + phdr->p_vaddr;
    uintptr_t mod_rel_addr = phdr->p_vaddr;
    char mode_str[4];
    csupport_mode_str_from_flags(phdr->p_flags, mode_str);
    n = snprintf(buf, sizeof(buf),
                 "{{{mmap:%#016lx:%#lx:load:%zu:%s:%#016lx}}}\n",
                 (unsigned long)start_addr, (unsigned long)phdr->p_memsz,
                 data->module_count, mode_str, (unsigned long)mod_rel_addr);
    if (n > 0) data->write_fn(data->ctx, buf, (size_t)n);
  }
  data->is_first = 0;
  data->module_count++;
  return 0;
}

void csupport_print_dso_markup_all(void *ctx, csupport_write_fn_t write_fn,
                                   const char *main_executable_name) {
  csupport_dso_markup_ctx_t data;
  data.ctx = ctx;
  data.write_fn = write_fn;
  data.main_executable_name = main_executable_name;
  data.module_count = 0;
  data.is_first = 1;

  write_fn(ctx, "{{{reset}}}\n", 12);
  dl_iterate_phdr(csupport_dso_markup_cb, &data);
}

#else

void csupport_print_dso_markup_all(void *ctx, csupport_write_fn_t write_fn,
                                   const char *main_executable_name) {
  (void)ctx; (void)write_fn; (void)main_executable_name;
}

#endif /* HAVE_LINK_H && ENABLE_BACKTRACES && linux/FreeBSD */

#else /* !LLVM_ON_UNIX (Windows stubs) */

void csupport_file_remove_list_insert(const char *filename) { (void)filename; }
void csupport_file_remove_list_erase(const char *filename) { (void)filename; }
void csupport_file_remove_list_remove_all(void) {}
void csupport_file_remove_list_cleanup(void) {}

int csupport_register_signal_handlers(const int *kill_sigs, int num_kill,
                                      const int *int_sigs, int num_int,
                                      const int *info_sigs, int num_info,
                                      int pipe_sig,
                                      csupport_sig_handler_fn kill_handler,
                                      csupport_sig_handler_fn info_handler) {
  (void)kill_sigs; (void)num_kill; (void)int_sigs; (void)num_int;
  (void)info_sigs; (void)num_info; (void)pipe_sig;
  (void)kill_handler; (void)info_handler;
  return 0;
}

void csupport_unregister_signal_handlers(void) {}
void csupport_setup_sig_alt_stack(void) {}

void csupport_set_atomic_interrupt_fn(void (*fn)(void)) { (void)fn; }
void *csupport_get_atomic_interrupt_fn(void) { return NULL; }
void csupport_set_atomic_info_fn(void (*fn)(void)) { (void)fn; }
void *csupport_get_atomic_info_fn(void) { return NULL; }
void csupport_set_atomic_pipe_fn(void (*fn)(void)) { (void)fn; }
void *csupport_get_atomic_pipe_fn(void) { return NULL; }
void *csupport_exchange_atomic_pipe_fn(void (*fn)(void)) { (void)fn; return NULL; }

void csupport_run_signal_callbacks(void) {}
void csupport_insert_signal_callback(csupport_signal_callback_fn fn, void *cookie) {
  (void)fn; (void)cookie;
}
int csupport_is_int_signal(int sig) { (void)sig; return 0; }
int csupport_is_kill_signal(int sig) { (void)sig; return 0; }
int csupport_is_info_signal(int sig) { (void)sig; return 0; }
const int *csupport_get_kill_sigs(int *out_count) { if (out_count) *out_count = 0; return NULL; }
const int *csupport_get_int_sigs(int *out_count) { if (out_count) *out_count = 0; return NULL; }
const int *csupport_get_info_sigs(int *out_count) { if (out_count) *out_count = 0; return NULL; }
void csupport_unix_signal_handler(int sig) { (void)sig; }
void csupport_unix_info_signal_handler(int sig) { (void)sig; }
void csupport_unix_cleanup_on_signal(int sig) { (void)sig; }
void csupport_unix_register_all_handlers(void) {}
void csupport_unix_unregister_all_handlers(void) {}
void csupport_print_stack_trace_dladdr(void *ctx, csupport_write_fn_t write_fn,
                                       void **stack_trace, int depth,
                                       char *(*demangle_fn)(const char *)) {
  (void)ctx; (void)write_fn; (void)stack_trace; (void)depth; (void)demangle_fn;
}
void csupport_print_dso_markup_all(void *ctx, csupport_write_fn_t write_fn,
                                   const char *main_executable_name) {
  (void)ctx; (void)write_fn; (void)main_executable_name;
}

#endif
