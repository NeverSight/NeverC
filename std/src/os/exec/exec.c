/*
 * NeverC os/exec — process execution.
 * Mirrors Go os/exec package.
 *
 * POSIX: fork() + execvp() with pipe-based I/O capture.
 * Windows: CreateProcess() with redirected handles.
 */

#include "neverc/std/os/exec.h"
#include "neverc/std/_platform.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

struct neverc_exec_cmd {
    char  *name;
    char **argv;
    int    argc;
    char  *dir;
    char **env;
    int    env_count;
    const void *stdin_data;
    size_t      stdin_len;
};

static void exec_free_strings(char **values, int count) {
    if (!values) return;
    for (int i = 0; i < count; i++) free(values[i]);
    free(values);
}

static char **exec_copy_strings(const char **values, int count) {
    if (count < 0 || (count > 0 && !values) ||
        (size_t)count > SIZE_MAX / sizeof(char *) - 1) return NULL;
    char **copy = (char **)calloc((size_t)count + 1, sizeof(char *));
    if (!copy) return NULL;
    for (int i = 0; i < count; i++) {
        if (!values[i]) { exec_free_strings(copy, i); return NULL; }
        copy[i] = strdup(values[i]);
        if (!copy[i]) { exec_free_strings(copy, i); return NULL; }
    }
    return copy;
}

neverc_exec_cmd_t *neverc_exec_command(const char *name, const char **args, int argc) {
    if (!name || argc < 0 || argc == INT_MAX || (argc > 0 && !args)) return NULL;
    neverc_exec_cmd_t *cmd = (neverc_exec_cmd_t *)calloc(1, sizeof(*cmd));
    if (!cmd) return NULL;
    cmd->name = strdup(name);
    if (!cmd->name) { neverc_exec_cmd_free(cmd); return NULL; }
    cmd->argc = argc + 1;
    if ((size_t)cmd->argc > SIZE_MAX / sizeof(char *) - 1) {
        neverc_exec_cmd_free(cmd);
        return NULL;
    }
    cmd->argv = (char **)calloc((size_t)(cmd->argc + 1), sizeof(char *));
    if (!cmd->argv) { neverc_exec_cmd_free(cmd); return NULL; }
    cmd->argv[0] = strdup(name);
    if (!cmd->argv[0]) { neverc_exec_cmd_free(cmd); return NULL; }
    for (int i = 0; i < argc; i++) {
        if (!args[i]) { neverc_exec_cmd_free(cmd); return NULL; }
        cmd->argv[i + 1] = strdup(args[i]);
        if (!cmd->argv[i + 1]) { neverc_exec_cmd_free(cmd); return NULL; }
    }
    cmd->argv[cmd->argc] = NULL;
    return cmd;
}

void neverc_exec_cmd_set_dir(neverc_exec_cmd_t *cmd, const char *dir) {
    if (!cmd) return;
    char *copy = dir ? strdup(dir) : NULL;
    if (dir && !copy) return;
    free(cmd->dir);
    cmd->dir = copy;
}

void neverc_exec_cmd_set_env(neverc_exec_cmd_t *cmd, const char **env, int env_count) {
    if (!cmd) return;
    char **copy = exec_copy_strings(env, env_count);
    if (!copy) return;
    exec_free_strings(cmd->env, cmd->env_count);
    cmd->env = copy;
    cmd->env_count = env_count;
}

void neverc_exec_cmd_set_stdin(neverc_exec_cmd_t *cmd, const void *data, size_t len) {
    if (!cmd || (!data && len != 0)) return;
    cmd->stdin_data = data;
    cmd->stdin_len = len;
}

void neverc_exec_cmd_free(neverc_exec_cmd_t *cmd) {
    if (!cmd) return;
    free(cmd->name);
    exec_free_strings(cmd->argv, cmd->argc);
    free(cmd->dir);
    exec_free_strings(cmd->env, cmd->env_count);
    free(cmd);
}

void neverc_exec_output_free(neverc_exec_output_t *out) {
    if (out) { free(out->data); out->data = NULL; out->len = 0; out->cap = 0; }
}

static int exec_prepare(neverc_exec_cmd_t *cmd, int capture_stdout,
                        neverc_exec_output_t *out,
                        neverc_exec_exit_status_t *status) {
    if (!cmd || !cmd->name || !cmd->argv ||
        (capture_stdout && !out)) return -1;
    if (out) {
        if (out->data || out->len != 0 || out->cap != 0) return -1;
        out->data = NULL;
        out->len = 0;
        out->cap = 0;
    }
    if (status) {
        status->exit_code = -1;
        status->signaled = 0;
        status->signal_num = 0;
    }
    return 0;
}

static int exec_output_reserve(neverc_exec_output_t *out, size_t needed) {
    if (needed <= out->cap) return 0;
    size_t capacity = out->cap ? out->cap : 4096;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2) { capacity = needed; break; }
        capacity *= 2;
    }
    unsigned char *grown = (unsigned char *)realloc(out->data, capacity);
    if (!grown) return -1;
    out->data = grown;
    out->cap = capacity;
    return 0;
}

#if defined(NEVERC_PLATFORM_WINDOWS)

#include <windows.h>

static char *exec_windows_environment(const neverc_exec_cmd_t *cmd) {
    if (!cmd->env) return NULL;
    size_t length = cmd->env_count == 0 ? 2 : 1;
    for (int i = 0; i < cmd->env_count; i++) {
        size_t item_length = strlen(cmd->env[i]);
        if (item_length > SIZE_MAX - length - 1) return NULL;
        length += item_length + 1;
    }
    char *block = (char *)malloc(length);
    if (!block) return NULL;
    size_t position = 0;
    for (int i = 0; i < cmd->env_count; i++) {
        size_t item_length = strlen(cmd->env[i]);
        memcpy(block + position, cmd->env[i], item_length + 1);
        position += item_length + 1;
    }
    block[position] = '\0';
    if (position == 0) block[1] = '\0';
    return block;
}

static int exec_run_windows(neverc_exec_cmd_t *cmd, int capture_stdout, int capture_stderr,
                            neverc_exec_output_t *out, neverc_exec_exit_status_t *st) {
    if (exec_prepare(cmd, capture_stdout || capture_stderr, out, st) != 0) return -1;
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    HANDLE hStdoutRd = NULL, hStdoutWr = NULL;
    HANDLE hStdinRd = NULL, hStdinWr = NULL;

    if (capture_stdout || capture_stderr) {
        if (!CreatePipe(&hStdoutRd, &hStdoutWr, &sa, 0) ||
            !SetHandleInformation(hStdoutRd, HANDLE_FLAG_INHERIT, 0)) goto setup_error;
    }
    if (cmd->stdin_data && cmd->stdin_len > 0) {
        if (!CreatePipe(&hStdinRd, &hStdinWr, &sa, 0) ||
            !SetHandleInformation(hStdinWr, HANDLE_FLAG_INHERIT, 0)) goto setup_error;
    }

    /* Build command line – only quote arguments that contain spaces or
       special characters so that cmd.exe /C and similar switches are passed
       unquoted and recognised correctly. */
    size_t cmdlen = 0;
    for (int i = 0; i < cmd->argc; i++) {
        size_t argument_length = strlen(cmd->argv[i]);
        if (argument_length > SIZE_MAX - cmdlen - 3) goto setup_error;
        cmdlen += argument_length + 3;
    }
    char *cmdline = (char *)malloc(cmdlen + 1);
    if (!cmdline) goto setup_error;
    /* Append with a running offset instead of strcat: each strcat re-scanned the
     * whole accumulated string, making the build O(total_len^2). memcpy at the
     * tracked end is O(total_len) and writes byte-identical output. The +3/arg
     * reservation (leading space + two quotes) bounds every write below. */
    size_t pos = 0;
    for (int i = 0; i < cmd->argc; i++) {
        if (i > 0) cmdline[pos++] = ' ';
        int needs_quote = 0;
        for (const char *p = cmd->argv[i]; *p; p++) {
            if (*p == ' ' || *p == '\t' || *p == '"' || *p == '&' ||
                *p == '|' || *p == '<' || *p == '>' || *p == '^') {
                needs_quote = 1;
                break;
            }
        }
        size_t alen = strlen(cmd->argv[i]);
        if (needs_quote || cmd->argv[i][0] == '\0') {
            cmdline[pos++] = '"';
            memcpy(cmdline + pos, cmd->argv[i], alen); pos += alen;
            cmdline[pos++] = '"';
        } else {
            memcpy(cmdline + pos, cmd->argv[i], alen); pos += alen;
        }
    }
    cmdline[pos] = '\0';

    char *environment = exec_windows_environment(cmd);
    if (cmd->env && !environment) { free(cmdline); goto setup_error; }

    STARTUPINFOA si = {sizeof(si)};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hStdoutWr ? hStdoutWr : GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = (capture_stderr && hStdoutWr) ? hStdoutWr : GetStdHandle(STD_ERROR_HANDLE);
    si.hStdInput = hStdinRd ? hStdinRd : GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi = {0};
    BOOL ok = CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0,
                             environment, cmd->dir, &si, &pi);
    free(environment);
    free(cmdline);
    if (!ok) {
        goto setup_error;
    }

    if (hStdoutWr) CloseHandle(hStdoutWr);
    if (hStdinRd) CloseHandle(hStdinRd);

    int run_error = 0;
    if (hStdinWr && cmd->stdin_data) {
        size_t offset = 0;
        while (offset < cmd->stdin_len) {
            size_t remaining = cmd->stdin_len - offset;
            DWORD chunk = remaining > MAXDWORD ? MAXDWORD : (DWORD)remaining;
            DWORD written = 0;
            if (!WriteFile(hStdinWr, (const char *)cmd->stdin_data + offset,
                           chunk, &written, NULL) || written == 0) {
                run_error = 1;
                break;
            }
            offset += written;
        }
        CloseHandle(hStdinWr);
        hStdinWr = NULL;
    }

    if (hStdoutRd && out) {
        if (exec_output_reserve(out, 1) != 0) {
            run_error = 1;
        } else for (;;) {
            if (out->len == SIZE_MAX ||
                exec_output_reserve(out, out->len + 1) != 0) {
                run_error = 1;
                break;
            }
            size_t available = out->cap - out->len;
            DWORD chunk = available > MAXDWORD ? MAXDWORD : (DWORD)available;
            DWORD n = 0;
            if (!ReadFile(hStdoutRd, out->data + out->len, chunk, &n, NULL)) {
                if (GetLastError() != ERROR_BROKEN_PIPE) run_error = 1;
                break;
            }
            if (n == 0) break;
            out->len += n;
        }
        CloseHandle(hStdoutRd);
        hStdoutRd = NULL;
    }

    if (WaitForSingleObject(pi.hProcess, INFINITE) != WAIT_OBJECT_0) run_error = 1;
    DWORD exitCode = (DWORD)-1;
    if (!GetExitCodeProcess(pi.hProcess, &exitCode)) run_error = 1;
    if (st) { st->exit_code = (int)exitCode; st->signaled = 0; st->signal_num = 0; }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (run_error) {
        if (out) neverc_exec_output_free(out);
        return -1;
    }
    return 0;

setup_error:
    if (hStdoutRd) CloseHandle(hStdoutRd);
    if (hStdoutWr) CloseHandle(hStdoutWr);
    if (hStdinRd) CloseHandle(hStdinRd);
    if (hStdinWr) CloseHandle(hStdinWr);
    return -1;
}

int neverc_exec_cmd_run(neverc_exec_cmd_t *cmd, neverc_exec_exit_status_t *st) {
    return exec_run_windows(cmd, 0, 0, NULL, st);
}
int neverc_exec_cmd_output(neverc_exec_cmd_t *cmd, neverc_exec_output_t *out, neverc_exec_exit_status_t *st) {
    return exec_run_windows(cmd, 1, 0, out, st);
}
int neverc_exec_cmd_combined_output(neverc_exec_cmd_t *cmd, neverc_exec_output_t *out, neverc_exec_exit_status_t *st) {
    return exec_run_windows(cmd, 1, 1, out, st);
}

const char *neverc_exec_look_path(const char *file, char *buf, size_t cap) {
    if (!file || !buf || cap == 0 || cap > MAXDWORD) return NULL;
    DWORD len = SearchPathA(NULL, file, ".exe", (DWORD)cap, buf, NULL);
    return len > 0 && len < cap ? buf : NULL;
}

#else /* POSIX */

#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>

static int exec_run_posix(neverc_exec_cmd_t *cmd, int capture_stdout, int capture_stderr,
                          neverc_exec_output_t *out, neverc_exec_exit_status_t *st) {
    if (exec_prepare(cmd, capture_stdout || capture_stderr, out, st) != 0) return -1;
    int stdout_pipe[2] = {-1, -1};
    int stdin_pipe[2] = {-1, -1};

    if (capture_stdout || capture_stderr) {
        if (pipe(stdout_pipe) < 0) return -1;
    }
    if (cmd->stdin_data && cmd->stdin_len > 0) {
        if (pipe(stdin_pipe) < 0) {
            if (stdout_pipe[0] >= 0) { close(stdout_pipe[0]); close(stdout_pipe[1]); }
            return -1;
        }
    }

    pid_t pid = fork();
    if (pid < 0) {
        if (stdout_pipe[0] >= 0) { close(stdout_pipe[0]); close(stdout_pipe[1]); }
        if (stdin_pipe[0] >= 0) { close(stdin_pipe[0]); close(stdin_pipe[1]); }
        return -1;
    }

    if (pid == 0) {
        /* Child */
        if (cmd->dir) {
            if (chdir(cmd->dir) < 0) _exit(127);
        }

        if (stdout_pipe[1] >= 0) {
            if (dup2(stdout_pipe[1], STDOUT_FILENO) < 0) _exit(127);
            if (capture_stderr && dup2(stdout_pipe[1], STDERR_FILENO) < 0) _exit(127);
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
        }
        if (stdin_pipe[0] >= 0) {
            if (dup2(stdin_pipe[0], STDIN_FILENO) < 0) _exit(127);
            close(stdin_pipe[0]);
            close(stdin_pipe[1]);
        }

        if (cmd->env)
            execve(cmd->name, cmd->argv, cmd->env);
        else
            execvp(cmd->name, cmd->argv);
        _exit(127);
    }

    /* Parent */
    if (stdout_pipe[1] >= 0) close(stdout_pipe[1]);
    if (stdin_pipe[0] >= 0) close(stdin_pipe[0]);

    int run_error = 0;
    if (stdin_pipe[1] >= 0 && cmd->stdin_data) {
        size_t off = 0;
        while (off < cmd->stdin_len) {
            ssize_t n = write(stdin_pipe[1], (const char *)cmd->stdin_data + off,
                              cmd->stdin_len - off);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) { run_error = 1; break; }
            off += (size_t)n;
        }
        close(stdin_pipe[1]);
        stdin_pipe[1] = -1;
    }

    if (stdout_pipe[0] >= 0 && out) {
        if (exec_output_reserve(out, 1) != 0) {
            run_error = 1;
        } else {
            for (;;) {
                if (out->len == SIZE_MAX ||
                    exec_output_reserve(out, out->len + 1) != 0) {
                    run_error = 1;
                    break;
                }
                ssize_t n = read(stdout_pipe[0], out->data + out->len,
                                 out->cap - out->len);
                if (n < 0 && errno == EINTR) continue;
                if (n < 0) run_error = 1;
                if (n <= 0) break;
                out->len += (size_t)n;
            }
        }
        close(stdout_pipe[0]);
        stdout_pipe[0] = -1;
    }

    int wstatus = 0;
    pid_t waited;
    do { waited = waitpid(pid, &wstatus, 0); } while (waited < 0 && errno == EINTR);
    if (waited < 0) run_error = 1;
    if (st && waited > 0) {
        if (WIFEXITED(wstatus)) {
            st->exit_code = WEXITSTATUS(wstatus);
            st->signaled = 0;
            st->signal_num = 0;
        } else if (WIFSIGNALED(wstatus)) {
            st->exit_code = -1;
            st->signaled = 1;
            st->signal_num = WTERMSIG(wstatus);
        }
    }
    if (run_error) {
        if (out) neverc_exec_output_free(out);
        return -1;
    }
    return 0;
}

int neverc_exec_cmd_run(neverc_exec_cmd_t *cmd, neverc_exec_exit_status_t *st) {
    return exec_run_posix(cmd, 0, 0, NULL, st);
}
int neverc_exec_cmd_output(neverc_exec_cmd_t *cmd, neverc_exec_output_t *out, neverc_exec_exit_status_t *st) {
    return exec_run_posix(cmd, 1, 0, out, st);
}
int neverc_exec_cmd_combined_output(neverc_exec_cmd_t *cmd, neverc_exec_output_t *out, neverc_exec_exit_status_t *st) {
    return exec_run_posix(cmd, 1, 1, out, st);
}

const char *neverc_exec_look_path(const char *file, char *buf, size_t cap) {
    if (!file || !buf || cap == 0) return NULL;
    if (strchr(file, '/')) {
        if (access(file, X_OK) == 0) {
            size_t flen = strlen(file);
            if (flen < cap) { memcpy(buf, file, flen + 1); return buf; }
        }
        return NULL;
    }
    const char *path_env = getenv("PATH");
    if (!path_env) return NULL;

    char path_copy[8192];
    size_t plen = strlen(path_env);
    if (plen >= sizeof(path_copy)) return NULL;
    memcpy(path_copy, path_env, plen + 1);

    char *saveptr = NULL;
    char *dir = strtok_r(path_copy, ":", &saveptr);
    while (dir) {
        size_t dlen = strlen(dir);
        size_t flen = strlen(file);
        if (dlen + 1 + flen + 1 <= cap) {
            memcpy(buf, dir, dlen);
            buf[dlen] = '/';
            memcpy(buf + dlen + 1, file, flen + 1);
            if (access(buf, X_OK) == 0) return buf;
        }
        dir = strtok_r(NULL, ":", &saveptr);
    }
    return NULL;
}

#endif /* POSIX */
