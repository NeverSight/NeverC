#ifndef NEVERC_OS_EXEC_H
#define NEVERC_OS_EXEC_H

/*
 * NeverC os/exec — process execution.
 * Mirrors Go os/exec package.
 * Cross-platform: POSIX (fork/exec) + Windows (CreateProcess).
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct neverc_exec_cmd neverc_exec_cmd_t;

typedef struct {
    int    exit_code;
    int    signaled;
    int    signal_num;
} neverc_exec_exit_status_t;

typedef struct {
    unsigned char *data;
    size_t         len;
    size_t         cap;
} neverc_exec_output_t;

neverc_exec_cmd_t *neverc_exec_command(const char *name, const char **args, int argc);
void               neverc_exec_cmd_set_dir(neverc_exec_cmd_t *cmd, const char *dir);
void               neverc_exec_cmd_set_env(neverc_exec_cmd_t *cmd, const char **env, int env_count);
void               neverc_exec_cmd_set_stdin(neverc_exec_cmd_t *cmd, const void *data, size_t len);

int  neverc_exec_cmd_run(neverc_exec_cmd_t *cmd, neverc_exec_exit_status_t *st);

int  neverc_exec_cmd_output(neverc_exec_cmd_t *cmd, neverc_exec_output_t *out,
                             neverc_exec_exit_status_t *st);

int  neverc_exec_cmd_combined_output(neverc_exec_cmd_t *cmd, neverc_exec_output_t *out,
                                      neverc_exec_exit_status_t *st);

void neverc_exec_cmd_free(neverc_exec_cmd_t *cmd);
void neverc_exec_output_free(neverc_exec_output_t *out);

const char *neverc_exec_look_path(const char *file, char *buf, size_t cap);

#ifdef __cplusplus
}
#endif

#endif
