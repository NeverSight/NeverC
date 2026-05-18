#ifndef CSUPPORT_LSIGNALS_H
#define CSUPPORT_LSIGNALS_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

void csupport_set_interrupt_function(void (*fn)(void));
void csupport_run_interrupt_handlers(void);

typedef void (*csupport_signal_handler_t)(void *cookie);

int csupport_register_signal_handler(int signo,
                                     csupport_signal_handler_t handler,
                                     void *cookie);

void csupport_print_stack_trace_fd(int fd);

int csupport_backtrace(void **buffer, int size);

void csupport_disable_system_dialogs_on_crash(void);

int csupport_format_ptr_hex(char *buf, size_t buflen, const void *ptr);
void csupport_set_thread_background_priority(void);
int csupport_has_thread_background_priority(void);

int csupport_is_interactive_session(void);
int csupport_get_terminal_width(void);

int csupport_signal_name(int signo, char *buf, size_t buflen);
int csupport_is_crash_signal(int signo);
void csupport_block_signals(void);
void csupport_unblock_signals(void);

/* Format a backtrace as "#N addr\n" lines into buf. Returns bytes written. */
int csupport_format_crash_backtrace(char *buf, size_t buflen, void **addrs,
                                    int depth);
/* Format a crash log message with optional prefix. */
int csupport_write_crash_log_msg(char *buf, size_t buflen, const char *prefix,
                                 const char *msg);

/* --- Session 13: signal-safe file removal linked list (pure C with atomics)
 * --- */

/* Insert filename into the signal-safe removal list. NOT signal-safe. */
void csupport_file_remove_list_insert(const char *filename);

/* Erase filename from the removal list (marks it empty). NOT signal-safe. */
void csupport_file_remove_list_erase(const char *filename);

/* Signal-safe: unlink all regular files in the removal list. */
void csupport_file_remove_list_remove_all(void);

/* Cleanup: free the entire removal list. NOT signal-safe. */
void csupport_file_remove_list_cleanup(void);

/* --- Session 13: signal handler registration (pure C sigaction) --- */

/* Max number of signals we can register handlers for */
#define CSUPPORT_MAX_REGISTERED_SIGNALS 32

typedef void (*csupport_sig_handler_fn)(int signo);

/* Register signal handlers for the given signals.
 * kill_handler is installed for kill/interrupt signals
 * (SA_NODEFER|SA_RESETHAND). info_handler is installed for info signals
 * (SIGUSR1, SIGINFO). Returns number of signals registered, or -1 on error. */
int csupport_register_signal_handlers(const int *kill_sigs, int num_kill,
                                      const int *int_sigs, int num_int,
                                      const int *info_sigs, int num_info,
                                      int pipe_sig,
                                      csupport_sig_handler_fn kill_handler,
                                      csupport_sig_handler_fn info_handler);

/* Restore all registered signal handlers to their original state. */
void csupport_unregister_signal_handlers(void);

/* Setup a signal alternate stack (for handling stack overflow). */
void csupport_setup_sig_alt_stack(void);

/* Atomically set/get function pointers for interrupt, info, pipe handlers. */
void csupport_set_atomic_interrupt_fn(void (*fn)(void));
void *csupport_get_atomic_interrupt_fn(void);

void csupport_set_atomic_info_fn(void (*fn)(void));
void *csupport_get_atomic_info_fn(void);

void csupport_set_atomic_pipe_fn(void (*fn)(void));
void *csupport_get_atomic_pipe_fn(void);
void *csupport_exchange_atomic_pipe_fn(void (*fn)(void));

/* --- Session 14: ELF module/offset scan moved from Unix/Signals.inc --- */
#if defined(HAVE_LINK_H)
int csupport_find_modules_offsets_elf(void **stack_trace, int depth,
                                      const char **modules, intptr_t *offsets,
                                      const char *main_executable_name);
#endif

/* _Unwind_Backtrace-based stack unwinding (glibc only, replaces C++ lambda). */
int csupport_unwind_backtrace(void **stack_trace, int max_entries);

/* Compute max module-name column width from dladdr results.
 * Returns width suitable for printf field width. */
int csupport_dladdr_max_width(void **stack_trace, int depth);

/* ELF NT_GNU_BUILD_ID finder. Scans PT_NOTE segments of a DSO described by
 * phdr_addr/phdr_count/phdr_entry_size/dlpi_addr.
 * Returns pointer+length of Build ID, or NULL if not found. */
const uint8_t *csupport_find_elf_build_id(uintptr_t dlpi_addr,
                                          const void *phdrs, int phnum,
                                          size_t phent_size, size_t *out_len);

/* Convert ELF p_flags to "rwx" permission string (null-terminated, max 4
 * bytes). */
void csupport_mode_str_from_flags(uint32_t flags, char out[4]);

/* --- Session 15: Signal callback system (replaces C++ CallbackAndCookie) ---
 */

typedef void (*csupport_signal_callback_fn)(void *cookie);

/* Signal-safe: run all registered signal callbacks (atomic CAS guard). */
void csupport_run_signal_callbacks(void);

/* Signal-safe: register a callback+cookie pair. */
void csupport_insert_signal_callback(csupport_signal_callback_fn fn,
                                     void *cookie);

/* --- Session 15: Unix signal handler core (replaces C++ Signals.inc logic) ---
 */

/* Query signal set membership. */
int csupport_is_int_signal(int sig);
int csupport_is_kill_signal(int sig);
int csupport_is_info_signal(int sig);

/* Get signal arrays (for use by RegisterHandlers). */
const int *csupport_get_kill_sigs(int *out_count);
const int *csupport_get_int_sigs(int *out_count);
const int *csupport_get_info_sigs(int *out_count);

/* Core signal handler: unregister, unblock, remove files, dispatch.
 * Installed as the SA handler for kill/interrupt/pipe signals. */
void csupport_unix_signal_handler(int sig);

/* Info signal handler: save/restore errno, call info fn. */
void csupport_unix_info_signal_handler(int sig);

/* CleanupOnSignal: called from crash recovery context. */
void csupport_unix_cleanup_on_signal(int sig);

/* Thread-safe one-time registration of all signal handlers. */
void csupport_unix_register_all_handlers(void);

/* Unregister handlers + reset flag. */
void csupport_unix_unregister_all_handlers(void);

/* --- Session 15: PrintStackTrace dladdr loop + DSOMarkupPrinter (write
 * callback) --- */

typedef void (*csupport_write_fn_t)(void *ctx, const char *data, size_t len);

/* Print stack trace using dladdr, calling write_fn for output.
 * demangle_fn is optional; if non-NULL, called to demangle symbol names. */
void csupport_print_stack_trace_dladdr(void *ctx, csupport_write_fn_t write_fn,
                                       void **stack_trace, int depth,
                                       char *(*demangle_fn)(const char *));

/* Print DSO markup for all loaded modules (ELF dl_iterate_phdr).
 * main_executable_name is used for the first module. */
void csupport_print_dso_markup_all(void *ctx, csupport_write_fn_t write_fn,
                                   const char *main_executable_name);

#ifdef __cplusplus
}
#endif
#endif
