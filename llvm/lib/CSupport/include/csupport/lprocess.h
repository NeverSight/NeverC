#ifndef CSUPPORT_LPROCESS_H
#define CSUPPORT_LPROCESS_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

unsigned csupport_get_page_size(void);
int csupport_get_process_id(void);
const char *csupport_get_env(const char *name);
int csupport_safely_close_fd(int fd);

int csupport_color_needs_flush(void);
const char *csupport_output_color(char code, int bold, int bg);
const char *csupport_output_bold(int bg);
const char *csupport_output_reverse(void);
const char *csupport_reset_color(void);
int csupport_fd_has_colors(int fd);

int csupport_fd_write(int fd, const char *ptr, size_t size);
uint64_t csupport_fd_seek(int fd, uint64_t off);
size_t csupport_fd_preferred_buffer_size(int fd, int is_displayed);
int csupport_fd_is_regular_file(int fd);
int64_t csupport_fd_tell(int fd);
int csupport_stdout_fileno(void);
int csupport_stderr_fileno(void);
int csupport_fd_open(const char *filename, size_t filename_len, int create_disp,
                     int access, int flags, int *err_out);
void csupport_change_stdout_mode(int flags);
int csupport_fd_write_console(int fd, const char *ptr, size_t size);

size_t csupport_get_malloc_usage(void);
void csupport_get_time_usage(int64_t *elapsed_ns, int64_t *user_ns,
                             int64_t *sys_ns);
void csupport_prevent_core_files(void);
int csupport_fixup_std_fds(void);
int csupport_fd_is_displayed(int fd);
unsigned csupport_fd_columns(int fd);
int csupport_fd_has_terminal_colors(int fd);
unsigned csupport_get_random_number(void);
void csupport_use_ansi_escape_codes(int enable);
void csupport_exit_no_cleanup(int retcode);
int csupport_fd_lock(int fd);
int csupport_fd_try_lock_for(int fd, int64_t timeout_ms);

extern int csupport_core_files_prevented;

#ifdef __cplusplus
}
#endif
#endif
