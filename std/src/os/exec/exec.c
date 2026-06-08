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

neverc_exec_cmd_t *neverc_exec_command(const char *name, const char **args, int argc) {
    neverc_exec_cmd_t *cmd = (neverc_exec_cmd_t *)calloc(1, sizeof(*cmd));
    if (!cmd) return NULL;
    cmd->name = strdup(name);
    cmd->argc = argc + 1;
    cmd->argv = (char **)calloc((size_t)(cmd->argc + 1), sizeof(char *));
    cmd->argv[0] = strdup(name);
    for (int i = 0; i < argc; i++)
        cmd->argv[i + 1] = strdup(args[i]);
    cmd->argv[cmd->argc] = NULL;
    return cmd;
}

void neverc_exec_cmd_set_dir(neverc_exec_cmd_t *cmd, const char *dir) {
    free(cmd->dir);
    cmd->dir = dir ? strdup(dir) : NULL;
}

void neverc_exec_cmd_set_env(neverc_exec_cmd_t *cmd, const char **env, int env_count) {
    if (cmd->env) {
        for (int i = 0; i < cmd->env_count; i++) free(cmd->env[i]);
        free(cmd->env);
    }
    cmd->env = (char **)calloc((size_t)(env_count + 1), sizeof(char *));
    cmd->env_count = env_count;
    for (int i = 0; i < env_count; i++)
        cmd->env[i] = strdup(env[i]);
    cmd->env[env_count] = NULL;
}

void neverc_exec_cmd_set_stdin(neverc_exec_cmd_t *cmd, const void *data, size_t len) {
    cmd->stdin_data = data;
    cmd->stdin_len = len;
}

void neverc_exec_cmd_free(neverc_exec_cmd_t *cmd) {
    if (!cmd) return;
    free(cmd->name);
    if (cmd->argv) {
        for (int i = 0; i < cmd->argc; i++) free(cmd->argv[i]);
        free(cmd->argv);
    }
    free(cmd->dir);
    if (cmd->env) {
        for (int i = 0; i < cmd->env_count; i++) free(cmd->env[i]);
        free(cmd->env);
    }
    free(cmd);
}

void neverc_exec_output_free(neverc_exec_output_t *out) {
    if (out) { free(out->data); out->data = NULL; out->len = 0; out->cap = 0; }
}

#if defined(NEVERC_PLATFORM_WINDOWS)

#include <windows.h>

static int exec_run_windows(neverc_exec_cmd_t *cmd, int capture_stdout, int capture_stderr,
                            neverc_exec_output_t *out, neverc_exec_exit_status_t *st) {
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    HANDLE hStdoutRd = NULL, hStdoutWr = NULL;
    HANDLE hStdinRd = NULL, hStdinWr = NULL;

    if (capture_stdout || capture_stderr) {
        CreatePipe(&hStdoutRd, &hStdoutWr, &sa, 0);
        SetHandleInformation(hStdoutRd, HANDLE_FLAG_INHERIT, 0);
    }
    if (cmd->stdin_data && cmd->stdin_len > 0) {
        CreatePipe(&hStdinRd, &hStdinWr, &sa, 0);
        SetHandleInformation(hStdinWr, HANDLE_FLAG_INHERIT, 0);
    }

    /* Build command line – only quote arguments that contain spaces or
       special characters so that cmd.exe /C and similar switches are passed
       unquoted and recognised correctly. */
    size_t cmdlen = 0;
    for (int i = 0; i < cmd->argc; i++)
        cmdlen += strlen(cmd->argv[i]) + 3;
    char *cmdline = (char *)malloc(cmdlen + 1);
    cmdline[0] = '\0';
    for (int i = 0; i < cmd->argc; i++) {
        if (i > 0) strcat(cmdline, " ");
        int needs_quote = 0;
        for (const char *p = cmd->argv[i]; *p; p++) {
            if (*p == ' ' || *p == '\t' || *p == '"' || *p == '&' ||
                *p == '|' || *p == '<' || *p == '>' || *p == '^') {
                needs_quote = 1;
                break;
            }
        }
        if (needs_quote || cmd->argv[i][0] == '\0') {
            strcat(cmdline, "\"");
            strcat(cmdline, cmd->argv[i]);
            strcat(cmdline, "\"");
        } else {
            strcat(cmdline, cmd->argv[i]);
        }
    }

    STARTUPINFOA si = {sizeof(si)};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hStdoutWr ? hStdoutWr : GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = (capture_stderr && hStdoutWr) ? hStdoutWr : GetStdHandle(STD_ERROR_HANDLE);
    si.hStdInput = hStdinRd ? hStdinRd : GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi = {0};
    BOOL ok = CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0,
                             cmd->env ? cmd->env : NULL, cmd->dir, &si, &pi);
    free(cmdline);
    if (!ok) {
        if (hStdoutRd) { CloseHandle(hStdoutRd); CloseHandle(hStdoutWr); }
        if (hStdinRd) { CloseHandle(hStdinRd); CloseHandle(hStdinWr); }
        return -1;
    }

    if (hStdoutWr) CloseHandle(hStdoutWr);
    if (hStdinRd) CloseHandle(hStdinRd);

    if (hStdinWr && cmd->stdin_data) {
        DWORD written;
        WriteFile(hStdinWr, cmd->stdin_data, (DWORD)cmd->stdin_len, &written, NULL);
        CloseHandle(hStdinWr);
    }

    if (hStdoutRd && out) {
        out->cap = 4096; out->len = 0;
        out->data = (unsigned char *)malloc(out->cap);
        DWORD n;
        while (ReadFile(hStdoutRd, out->data + out->len, (DWORD)(out->cap - out->len), &n, NULL) && n > 0) {
            out->len += n;
            if (out->len >= out->cap) {
                out->cap *= 2;
                out->data = (unsigned char *)realloc(out->data, out->cap);
            }
        }
        CloseHandle(hStdoutRd);
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    if (st) { st->exit_code = (int)exitCode; st->signaled = 0; st->signal_num = 0; }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return 0;
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
    DWORD len = SearchPathA(NULL, file, ".exe", (DWORD)cap, buf, NULL);
    return len > 0 ? buf : NULL;
}

#else /* POSIX */

#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>

static int exec_run_posix(neverc_exec_cmd_t *cmd, int capture_stdout, int capture_stderr,
                          neverc_exec_output_t *out, neverc_exec_exit_status_t *st) {
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
            dup2(stdout_pipe[1], STDOUT_FILENO);
            if (capture_stderr) dup2(stdout_pipe[1], STDERR_FILENO);
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
        }
        if (stdin_pipe[0] >= 0) {
            dup2(stdin_pipe[0], STDIN_FILENO);
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

    if (stdin_pipe[1] >= 0 && cmd->stdin_data) {
        ssize_t off = 0;
        while ((size_t)off < cmd->stdin_len) {
            ssize_t n = write(stdin_pipe[1], (const char *)cmd->stdin_data + off, cmd->stdin_len - (size_t)off);
            if (n <= 0) break;
            off += n;
        }
        close(stdin_pipe[1]);
    }

    if (stdout_pipe[0] >= 0 && out) {
        out->cap = 4096; out->len = 0;
        out->data = (unsigned char *)malloc(out->cap);
        for (;;) {
            if (out->len >= out->cap) {
                out->cap *= 2;
                out->data = (unsigned char *)realloc(out->data, out->cap);
            }
            ssize_t n = read(stdout_pipe[0], out->data + out->len, out->cap - out->len);
            if (n <= 0) break;
            out->len += (size_t)n;
        }
        close(stdout_pipe[0]);
    }

    int wstatus = 0;
    waitpid(pid, &wstatus, 0);
    if (st) {
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
