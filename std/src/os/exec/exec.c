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
    int    started;
#if defined(NEVERC_PLATFORM_WINDOWS)
    void          *process;
    void          *thread;
    void          *stdin_thread;
    void          *stdin_writer;
    unsigned long  pid;
#else
    int    pid;
    int    stdin_writer;
#endif
};

static void exec_free_strings(char **values, int count) {
    if (!values) return;
    for (int i = 0; i < count; i++) free(values[i]);
    free(values);
}

static int exec_env_entry_ok(const char *entry) {
    const char *eq;
    if (!entry || entry[0] == '\0') return 0;
    eq = strchr(entry, '=');
    return eq && eq != entry;
}

static char **exec_copy_strings(const char **values, int count) {
    if (count < 0 || (count > 0 && !values) ||
        (size_t)count > SIZE_MAX / sizeof(char *) - 1) return NULL;
    char **copy = (char **)calloc((size_t)count + 1, sizeof(char *));
    if (!copy) return NULL;
    for (int i = 0; i < count; i++) {
        if (!values[i] || !exec_env_entry_ok(values[i])) {
            exec_free_strings(copy, i);
            return NULL;
        }
        copy[i] = strdup(values[i]);
        if (!copy[i]) { exec_free_strings(copy, i); return NULL; }
    }
    return copy;
}

static int exec_ascii_ieq(const char *a, const char *b) {
    unsigned char ca, cb;
    if (!a || !b) return 0;
    while (*a || *b) {
        ca = (unsigned char)*a++;
        cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
    }
    return 1;
}

static int exec_is_windows_batch_name(const char *name) {
    const char *base, *dot, *p, *slash;
    char trimmed[16];
    size_t n, i;
    if (!name || name[0] == '\0') return 0;
    slash = strrchr(name, '/');
    p = strrchr(name, '\\');
    if (p && (!slash || p > slash)) slash = p;
    base = slash ? slash + 1 : name;
    n = strlen(base);
    while (n > 0 && (base[n - 1] == ' ' || base[n - 1] == '.')) n--;
    if (n >= sizeof(trimmed)) n = sizeof(trimmed) - 1;
    memcpy(trimmed, base, n);
    trimmed[n] = '\0';
    dot = strrchr(trimmed, '.');
    if (!dot || dot == trimmed) return 0;
    return exec_ascii_ieq(dot, ".bat") || exec_ascii_ieq(dot, ".cmd");
}

/* BatBadBut: CreateProcess of .bat/.cmd invokes cmd.exe, whose metacharacters
 * are not covered by CommandLineToArgvW quoting. Reject unsafe extra args. */
static int exec_batch_args_unsafe(const neverc_exec_cmd_t *cmd) {
    int i;
    if (!cmd || !cmd->name) return 0;
    if (!exec_is_windows_batch_name(cmd->name) &&
        !(cmd->argv && cmd->argv[0] && exec_is_windows_batch_name(cmd->argv[0])))
        return 0;
    for (i = 1; i < cmd->argc; i++) {
        const char *a = cmd->argv[i];
        if (!a) return 1;
        for (; *a; a++) {
            unsigned char c = (unsigned char)*a;
            if (c < 0x20 || strchr("&|<>^%!\"()", (int)c))
                return 1;
        }
    }
    return 0;
}

static const char *exec_env_path(char **env, int env_count) {
    int i;
    if (!env) return NULL;
    for (i = 0; i < env_count; i++) {
        if (!env[i]) continue;
#if defined(NEVERC_PLATFORM_WINDOWS)
        if (_strnicmp(env[i], "PATH=", 5) == 0)
            return env[i] + 5;
#else
        if (strncmp(env[i], "PATH=", 5) == 0)
            return env[i] + 5;
#endif
    }
    return NULL;
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
    if (cmd->started) {
        neverc_exec_cmd_kill(cmd, 9);
        neverc_exec_cmd_wait(cmd, NULL);
    }
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
    if (!cmd || !cmd->name || !cmd->argv || cmd->started ||
        (capture_stdout && !out)) return -1;
    if (exec_batch_args_unsafe(cmd)) return -1;
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

static int exec_windows_env_has_key(char **env, int count, const char *key) {
    size_t klen = strlen(key);
    int i;
    for (i = 0; i < count; i++) {
        if (env[i] && _strnicmp(env[i], key, (int)klen) == 0 && env[i][klen] == '=')
            return 1;
    }
    return 0;
}

static char *exec_windows_environment(const neverc_exec_cmd_t *cmd) {
    char systemroot[MAX_PATH + 12];
    int extra = 0;
    size_t extra_len = 0;
    size_t length;
    int i;
    char *block;
    size_t position;
    if (!cmd->env) return NULL;

    systemroot[0] = '\0';
    if (!exec_windows_env_has_key(cmd->env, cmd->env_count, "SYSTEMROOT")) {
        DWORD n = GetEnvironmentVariableA("SYSTEMROOT", systemroot + 11,
                                          (DWORD)sizeof(systemroot) - 11);
        if (n > 0 && n < sizeof(systemroot) - 11) {
            memcpy(systemroot, "SYSTEMROOT=", 11);
            extra = 1;
            extra_len = 11 + (size_t)n;
        }
    }

    length = (cmd->env_count == 0 && !extra) ? 2 : 1;
    if (extra) {
        if (extra_len > SIZE_MAX - length - 1) return NULL;
        length += extra_len + 1;
    }
    for (i = 0; i < cmd->env_count; i++) {
        size_t item_length = strlen(cmd->env[i]);
        if (item_length > SIZE_MAX - length - 1) return NULL;
        length += item_length + 1;
    }
    block = (char *)malloc(length);
    if (!block) return NULL;
    position = 0;
    if (extra) {
        memcpy(block + position, systemroot, extra_len + 1);
        position += extra_len + 1;
    }
    for (i = 0; i < cmd->env_count; i++) {
        size_t item_length = strlen(cmd->env[i]);
        memcpy(block + position, cmd->env[i], item_length + 1);
        position += item_length + 1;
    }
    block[position] = '\0';
    if (position == 0) block[1] = '\0';
    return block;
}

static char *exec_windows_cmdline(const neverc_exec_cmd_t *cmd) {
    size_t cmdlen = 0;
    char *cmdline;
    size_t pos = 0;
    int i;
    for (i = 0; i < cmd->argc; i++) {
        size_t argument_length = strlen(cmd->argv[i]);
        if (argument_length > (SIZE_MAX / 2) - 4) return NULL;
        size_t quoted = argument_length * 2 + 3;
        if (quoted > SIZE_MAX - cmdlen - 1) return NULL;
        cmdlen += quoted;
    }
    cmdline = (char *)malloc(cmdlen + 1);
    if (!cmdline) return NULL;
    for (i = 0; i < cmd->argc; i++) {
        const char *arg = cmd->argv[i];
        int needs_quote = (arg[0] == '\0');
        const char *p;
        if (i > 0) cmdline[pos++] = ' ';
        for (p = arg; *p && !needs_quote; p++) {
            unsigned char c = (unsigned char)*p;
            if (c <= 0x20 || c == '"' || c == '&' || c == '|' ||
                c == '<' || c == '>' || c == '^')
                needs_quote = 1;
        }
        if (!needs_quote) {
            size_t alen = strlen(arg);
            memcpy(cmdline + pos, arg, alen);
            pos += alen;
            continue;
        }
        cmdline[pos++] = '"';
        {
            size_t slashes = 0;
            for (p = arg; *p; p++) {
                if (*p == '\\') {
                    slashes++;
                } else if (*p == '"') {
                    size_t extra;
                    for (extra = 0; extra < slashes; extra++)
                        cmdline[pos++] = '\\';
                    cmdline[pos++] = '\\';
                    slashes = 0;
                } else {
                    slashes = 0;
                }
                cmdline[pos++] = *p;
            }
            {
                size_t extra;
                for (extra = 0; extra < slashes; extra++)
                    cmdline[pos++] = '\\';
            }
        }
        cmdline[pos++] = '"';
    }
    cmdline[pos] = '\0';
    return cmdline;
}

static const char *exec_windows_resolve_app(const neverc_exec_cmd_t *cmd,
                                            char *buf, DWORD cap) {
    const char *path_env;
    DWORD len;
    if (!cmd->name || cmd->name[0] == '\0') return NULL;
    if (strchr(cmd->name, '\\') || strchr(cmd->name, '/') ||
        (cmd->name[0] && cmd->name[1] == ':'))
        return cmd->name;
    path_env = exec_env_path(cmd->env, cmd->env_count);
    len = SearchPathA(path_env, cmd->name, ".exe", cap, buf, NULL);
    return (len > 0 && len < cap) ? buf : NULL;
}

typedef struct {
    HANDLE pipe;
    const unsigned char *data;
    size_t length;
    int failed;
} exec_windows_stdin_writer_t;

static DWORD WINAPI exec_windows_write_stdin(LPVOID argument) {
    exec_windows_stdin_writer_t *writer =
        (exec_windows_stdin_writer_t *)argument;
    size_t offset = 0;
    while (offset < writer->length) {
        size_t remaining = writer->length - offset;
        DWORD chunk = remaining > 65536U ? 65536U : (DWORD)remaining;
        DWORD written = 0;
        if (!WriteFile(writer->pipe, writer->data + offset,
                       chunk, &written, NULL)) {
            DWORD err = GetLastError();
            if (err != ERROR_BROKEN_PIPE && err != ERROR_NO_DATA &&
                err != ERROR_PIPE_NOT_CONNECTED)
                writer->failed = 1;
            break;
        }
        if (written == 0) break;
        offset += written;
    }
    CloseHandle(writer->pipe);
    writer->pipe = NULL;
    return 0;
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

    /* CommandLineToArgvW quoting (Go syscall.EscapeArg): double slashes
     * before embedded quotes and before the closing quote. */
    char *cmdline = exec_windows_cmdline(cmd);
    if (!cmdline) goto setup_error;

    char *environment = exec_windows_environment(cmd);
    if (cmd->env && !environment) { free(cmdline); goto setup_error; }

    char resolved[32768];
    const char *app = exec_windows_resolve_app(cmd, resolved,
                                               (DWORD)sizeof(resolved));

    STARTUPINFOA si = {sizeof(si)};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hStdoutWr ? hStdoutWr : GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = (capture_stderr && hStdoutWr) ? hStdoutWr : GetStdHandle(STD_ERROR_HANDLE);
    si.hStdInput = hStdinRd ? hStdinRd : GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi = {0};
    BOOL ok = CreateProcessA(app, cmdline, NULL, NULL, TRUE, 0,
                             environment, cmd->dir, &si, &pi);
    free(environment);
    free(cmdline);
    if (!ok) {
        goto setup_error;
    }

    if (hStdoutWr) CloseHandle(hStdoutWr);
    if (hStdinRd) CloseHandle(hStdinRd);

    int run_error = 0;
    HANDLE hStdinThread = NULL;
    exec_windows_stdin_writer_t stdin_writer = {
        hStdinWr, (const unsigned char *)cmd->stdin_data,
        cmd->stdin_len, 0
    };
    if (hStdinWr && cmd->stdin_data) {
        hStdinThread = CreateThread(
            NULL, 0, exec_windows_write_stdin, &stdin_writer, 0, NULL);
        if (!hStdinThread) {
            run_error = 1;
            CloseHandle(hStdinWr);
        } else {
            hStdinWr = NULL; /* The writer thread owns the pipe handle. */
        }
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

    /* Wait for the process before joining the stdin writer. Waiting for the
     * writer first deadlocks when the child stays alive without draining
     * stdin (pipe buffer fills, writer blocks, parent never reaps). */
    if (WaitForSingleObject(pi.hProcess, INFINITE) != WAIT_OBJECT_0) run_error = 1;
    if (hStdinThread) {
        if (WaitForSingleObject(hStdinThread, INFINITE) != WAIT_OBJECT_0 ||
            stdin_writer.failed)
            run_error = 1;
        CloseHandle(hStdinThread);
    }
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

int neverc_exec_cmd_start(neverc_exec_cmd_t *cmd) {
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    HANDLE hStdinRd = NULL, hStdinWr = NULL;
    char *cmdline;
    char *environment;
    char resolved[32768];
    const char *app;
    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi = {0};
    exec_windows_stdin_writer_t *writer = NULL;
    if (exec_prepare(cmd, 0, NULL, NULL) != 0) return -1;

    if (cmd->stdin_data && cmd->stdin_len > 0) {
        if (!CreatePipe(&hStdinRd, &hStdinWr, &sa, 0) ||
            !SetHandleInformation(hStdinWr, HANDLE_FLAG_INHERIT, 0))
            goto setup_error;
    }

    cmdline = exec_windows_cmdline(cmd);
    if (!cmdline) goto setup_error;
    environment = exec_windows_environment(cmd);
    if (cmd->env && !environment) { free(cmdline); goto setup_error; }
    app = exec_windows_resolve_app(cmd, resolved, (DWORD)sizeof(resolved));

    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    si.hStdInput = hStdinRd ? hStdinRd : GetStdHandle(STD_INPUT_HANDLE);

    if (!CreateProcessA(app, cmdline, NULL, NULL, TRUE, 0,
                        environment, cmd->dir, &si, &pi)) {
        free(environment);
        free(cmdline);
        goto setup_error;
    }
    free(environment);
    free(cmdline);
    if (hStdinRd) { CloseHandle(hStdinRd); hStdinRd = NULL; }

    if (hStdinWr && cmd->stdin_data) {
        writer = (exec_windows_stdin_writer_t *)calloc(1, sizeof(*writer));
        if (!writer) {
            CloseHandle(hStdinWr);
            TerminateProcess(pi.hProcess, 1);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return -1;
        }
        writer->pipe = hStdinWr;
        writer->data = (const unsigned char *)cmd->stdin_data;
        writer->length = cmd->stdin_len;
        cmd->stdin_thread = CreateThread(
            NULL, 0, exec_windows_write_stdin, writer, 0, NULL);
        if (!cmd->stdin_thread) {
            CloseHandle(hStdinWr);
            free(writer);
            TerminateProcess(pi.hProcess, 1);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return -1;
        }
        cmd->stdin_writer = writer;
        hStdinWr = NULL;
    }

    cmd->process = pi.hProcess;
    cmd->thread = pi.hThread;
    cmd->pid = pi.dwProcessId;
    cmd->started = 1;
    return 0;

setup_error:
    if (hStdinRd) CloseHandle(hStdinRd);
    if (hStdinWr) CloseHandle(hStdinWr);
    return -1;
}

int neverc_exec_cmd_wait(neverc_exec_cmd_t *cmd, neverc_exec_exit_status_t *st) {
    DWORD exitCode = (DWORD)-1;
    int run_error = 0;
    if (!cmd || !cmd->started || !cmd->process) return -1;
    if (WaitForSingleObject((HANDLE)cmd->process, INFINITE) != WAIT_OBJECT_0)
        run_error = 1;
    if (cmd->stdin_thread) {
        exec_windows_stdin_writer_t *writer =
            (exec_windows_stdin_writer_t *)cmd->stdin_writer;
        if (WaitForSingleObject((HANDLE)cmd->stdin_thread, INFINITE) != WAIT_OBJECT_0 ||
            (writer && writer->failed))
            run_error = 1;
        CloseHandle((HANDLE)cmd->stdin_thread);
        cmd->stdin_thread = NULL;
        free(writer);
        cmd->stdin_writer = NULL;
    }
    if (!GetExitCodeProcess((HANDLE)cmd->process, &exitCode)) run_error = 1;
    if (st) {
        st->exit_code = (int)exitCode;
        st->signaled = 0;
        st->signal_num = 0;
    }
    CloseHandle((HANDLE)cmd->process);
    if (cmd->thread) CloseHandle((HANDLE)cmd->thread);
    cmd->process = NULL;
    cmd->thread = NULL;
    cmd->pid = 0;
    cmd->started = 0;
    return run_error ? -1 : 0;
}

int neverc_exec_cmd_kill(neverc_exec_cmd_t *cmd, int signum) {
    if (!cmd || !cmd->started || !cmd->process) return -1;
    if (signum == 0)
        return WaitForSingleObject((HANDLE)cmd->process, 0) == WAIT_TIMEOUT ? 0 : -1;
    if (signum != 9 && signum != 15 && signum != 2)
        return -1;
    return TerminateProcess((HANDLE)cmd->process, 1) ? 0 : -1;
}

int neverc_exec_cmd_pid(const neverc_exec_cmd_t *cmd) {
    if (!cmd || !cmd->started || cmd->pid == 0) return -1;
    if (cmd->pid > (unsigned long)INT_MAX) return -1;
    return (int)cmd->pid;
}

const char *neverc_exec_look_path(const char *file, char *buf, size_t cap) {
    if (!file || file[0] == '\0' || !buf || cap == 0 || cap > MAXDWORD)
        return NULL;
    DWORD len = SearchPathA(NULL, file, ".exe", (DWORD)cap, buf, NULL);
    return len > 0 && len < cap ? buf : NULL;
}

#else /* POSIX */

#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

static int exec_is_exe_file(const char *path) {
    if (access(path, X_OK) != 0) return 0;
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int exec_set_cloexec(int fd) {
#ifdef FD_CLOEXEC
    return fcntl(fd, F_SETFD, FD_CLOEXEC);
#else
    (void)fd;
    return 0;
#endif
}

static int exec_pipe(int fds[2]) {
    if (pipe(fds) < 0) return -1;
    if (exec_set_cloexec(fds[0]) != 0 || exec_set_cloexec(fds[1]) != 0) {
        close(fds[0]);
        close(fds[1]);
        return -1;
    }
    return 0;
}

static const char *exec_look_in_path(const char *file, const char *path_env,
                                     char *buf, size_t cap) {
    if (!file || file[0] == '\0' || !buf || cap == 0) return NULL;
    if (strchr(file, '/')) {
        if (exec_is_exe_file(file)) {
            size_t flen = strlen(file);
            if (flen < cap) { memcpy(buf, file, flen + 1); return buf; }
        }
        return NULL;
    }
    if (!path_env) return NULL;

    const char *p = path_env;
    size_t flen = strlen(file);
    for (;;) {
        const char *colon = strchr(p, ':');
        size_t dlen = colon ? (size_t)(colon - p) : strlen(p);
        if (dlen == 0) {
            if (cap >= 3 && flen <= cap - 3) {
                buf[0] = '.';
                buf[1] = '/';
                memcpy(buf + 2, file, flen + 1);
                if (exec_is_exe_file(buf)) return buf;
            }
        } else if (cap >= 2 && dlen <= cap - 2 && flen <= cap - 2 - dlen) {
            memcpy(buf, p, dlen);
            buf[dlen] = '/';
            memcpy(buf + dlen + 1, file, flen + 1);
            if (exec_is_exe_file(buf)) return buf;
        }
        if (!colon) break;
        p = colon + 1;
    }
    return NULL;
}

static void exec_posix_reset_signals(void) {
    int sig;
#ifndef NSIG
#define EXEC_NSIG 32
#else
#define EXEC_NSIG NSIG
#endif
    for (sig = 1; sig < EXEC_NSIG; sig++) {
        struct sigaction sa;
        if (sig == SIGKILL || sig == SIGSTOP) continue;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = SIG_DFL;
        sigemptyset(&sa.sa_mask);
        (void)sigaction(sig, &sa, NULL);
    }
#undef EXEC_NSIG
}

static void exec_mark_cloexec_from(int minfd) {
#ifdef FD_CLOEXEC
    long maxfd = sysconf(_SC_OPEN_MAX);
    int fd;
    if (maxfd < (long)minfd) return;
    if (maxfd > 65536) maxfd = 65536;
    for (fd = minfd; fd < (int)maxfd; fd++)
        (void)fcntl(fd, F_SETFD, FD_CLOEXEC);
#else
    (void)minfd;
#endif
}

static void exec_posix_do_exec(neverc_exec_cmd_t *cmd) {
    if (cmd->env) {
        if (strchr(cmd->name, '/')) {
            execve(cmd->name, cmd->argv, cmd->env);
        } else {
            char resolved[4096];
            const char *path = exec_env_path(cmd->env, cmd->env_count);
            if (!path) path = getenv("PATH");
            if (exec_look_in_path(cmd->name, path, resolved, sizeof(resolved)))
                execve(resolved, cmd->argv, cmd->env);
        }
    } else {
        execvp(cmd->name, cmd->argv);
    }
}

static void exec_posix_child(neverc_exec_cmd_t *cmd,
                             int stdout_wr, int capture_stderr,
                             int stdin_rd, int err_wr) {
    int err;
    sigset_t empty;
    exec_posix_reset_signals();
    sigemptyset(&empty);
    sigprocmask(SIG_SETMASK, &empty, NULL);
    if (cmd->dir) {
        if (chdir(cmd->dir) < 0) goto exec_fail;
    }
    if (stdout_wr >= 0) {
        if (dup2(stdout_wr, STDOUT_FILENO) < 0) goto exec_fail;
        if (capture_stderr && dup2(stdout_wr, STDERR_FILENO) < 0) goto exec_fail;
    }
    if (stdin_rd >= 0) {
        if (dup2(stdin_rd, STDIN_FILENO) < 0) goto exec_fail;
    }
    exec_mark_cloexec_from(3);
    exec_posix_do_exec(cmd);
exec_fail:
    err = errno ? errno : ENOENT;
    if (err_wr >= 0) {
        ssize_t n;
        do {
            n = write(err_wr, &err, sizeof(err));
        } while (n < 0 && errno == EINTR);
    }
    _exit(127);
}

static int exec_waitpid_retry(pid_t pid, int *wstatus) {
    pid_t waited;
    do { waited = waitpid(pid, wstatus, 0); } while (waited < 0 && errno == EINTR);
    return waited < 0 ? -1 : 0;
}

static void exec_fill_wait_status(int wstatus, neverc_exec_exit_status_t *st) {
    if (!st) return;
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

static int exec_reap_writer(pid_t stdin_writer) {
    int writer_status = 0;
    if (stdin_writer <= 0) return 0;
    if (exec_waitpid_retry(stdin_writer, &writer_status) != 0) return -1;
    if (WIFSIGNALED(writer_status) && WTERMSIG(writer_status) == SIGPIPE)
        return 0;
    if (!WIFEXITED(writer_status) || WEXITSTATUS(writer_status) != 0)
        return -1;
    return 0;
}

static pid_t exec_fork_stdin_writer(neverc_exec_cmd_t *cmd, int stdin_wr,
                                    int stdout_rd) {
    pid_t stdin_writer = fork();
    if (stdin_writer == 0) {
        if (stdout_rd >= 0) close(stdout_rd);
        signal(SIGPIPE, SIG_IGN);
        size_t offset = 0;
        while (offset < cmd->stdin_len) {
            size_t remaining = cmd->stdin_len - offset;
            size_t chunk =
                remaining > (size_t)INT_MAX ?
                (size_t)INT_MAX : remaining;
            ssize_t written = write(
                stdin_wr,
                (const char *)cmd->stdin_data + offset, chunk);
            if (written < 0 && errno == EINTR)
                continue;
            if (written < 0 && errno == EPIPE) {
                close(stdin_wr);
                _exit(0);
            }
            if (written <= 0) {
                close(stdin_wr);
                _exit(125);
            }
            offset += (size_t)written;
        }
        close(stdin_wr);
        _exit(0);
    }
    return stdin_writer;
}

static int exec_read_exec_error(int err_rd) {
    int exec_err = 0;
    ssize_t n;
    do {
        n = read(err_rd, &exec_err, sizeof(exec_err));
    } while (n < 0 && errno == EINTR);
    close(err_rd);
    if (n == (ssize_t)sizeof(exec_err)) return -1;
    if (n < 0) return -1;
    return 0;
}

static int exec_run_posix(neverc_exec_cmd_t *cmd, int capture_stdout, int capture_stderr,
                          neverc_exec_output_t *out, neverc_exec_exit_status_t *st) {
    int stdout_pipe[2] = {-1, -1};
    int stdin_pipe[2] = {-1, -1};
    int err_pipe[2] = {-1, -1};
    pid_t pid;
    pid_t stdin_writer = -1;
    int run_error = 0;
    int wstatus = 0;
    if (exec_prepare(cmd, capture_stdout || capture_stderr, out, st) != 0) return -1;

    if (exec_pipe(err_pipe) < 0) return -1;
    if (capture_stdout || capture_stderr) {
        if (exec_pipe(stdout_pipe) < 0) {
            close(err_pipe[0]); close(err_pipe[1]);
            return -1;
        }
    }
    if (cmd->stdin_data && cmd->stdin_len > 0) {
        if (exec_pipe(stdin_pipe) < 0) {
            close(err_pipe[0]); close(err_pipe[1]);
            if (stdout_pipe[0] >= 0) { close(stdout_pipe[0]); close(stdout_pipe[1]); }
            return -1;
        }
    }

    pid = fork();
    if (pid < 0) {
        close(err_pipe[0]); close(err_pipe[1]);
        if (stdout_pipe[0] >= 0) { close(stdout_pipe[0]); close(stdout_pipe[1]); }
        if (stdin_pipe[0] >= 0) { close(stdin_pipe[0]); close(stdin_pipe[1]); }
        return -1;
    }

    if (pid == 0) {
        close(err_pipe[0]);
        if (stdout_pipe[0] >= 0) close(stdout_pipe[0]);
        if (stdin_pipe[1] >= 0) close(stdin_pipe[1]);
        exec_posix_child(cmd, stdout_pipe[1], capture_stderr,
                         stdin_pipe[0], err_pipe[1]);
    }

    close(err_pipe[1]);
    err_pipe[1] = -1;
    if (stdout_pipe[1] >= 0) { close(stdout_pipe[1]); stdout_pipe[1] = -1; }
    if (stdin_pipe[0] >= 0) { close(stdin_pipe[0]); stdin_pipe[0] = -1; }

    if (stdin_pipe[1] >= 0 && cmd->stdin_data) {
        stdin_writer = exec_fork_stdin_writer(cmd, stdin_pipe[1], stdout_pipe[0]);
        if (stdin_writer < 0) run_error = 1;
        close(stdin_pipe[1]);
        stdin_pipe[1] = -1;
    }

    if (exec_read_exec_error(err_pipe[0]) != 0) run_error = 1;
    err_pipe[0] = -1;

    if (stdout_pipe[0] >= 0 && out && !run_error) {
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
    }
    if (stdout_pipe[0] >= 0) {
        close(stdout_pipe[0]);
        stdout_pipe[0] = -1;
    }

    if (exec_waitpid_retry(pid, &wstatus) != 0) run_error = 1;
    if (exec_reap_writer(stdin_writer) != 0) run_error = 1;
    if (st && !run_error)
        exec_fill_wait_status(wstatus, st);
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

int neverc_exec_cmd_start(neverc_exec_cmd_t *cmd) {
    int stdin_pipe[2] = {-1, -1};
    int err_pipe[2] = {-1, -1};
    pid_t pid;
    pid_t stdin_writer = -1;
    if (exec_prepare(cmd, 0, NULL, NULL) != 0) return -1;
    if (exec_pipe(err_pipe) < 0) return -1;
    if (cmd->stdin_data && cmd->stdin_len > 0) {
        if (exec_pipe(stdin_pipe) < 0) {
            close(err_pipe[0]); close(err_pipe[1]);
            return -1;
        }
    }
    pid = fork();
    if (pid < 0) {
        close(err_pipe[0]); close(err_pipe[1]);
        if (stdin_pipe[0] >= 0) { close(stdin_pipe[0]); close(stdin_pipe[1]); }
        return -1;
    }
    if (pid == 0) {
        close(err_pipe[0]);
        if (stdin_pipe[1] >= 0) close(stdin_pipe[1]);
        exec_posix_child(cmd, -1, 0, stdin_pipe[0], err_pipe[1]);
    }
    close(err_pipe[1]);
    if (stdin_pipe[0] >= 0) { close(stdin_pipe[0]); stdin_pipe[0] = -1; }
    if (stdin_pipe[1] >= 0 && cmd->stdin_data) {
        stdin_writer = exec_fork_stdin_writer(cmd, stdin_pipe[1], -1);
        close(stdin_pipe[1]);
        stdin_pipe[1] = -1;
        if (stdin_writer < 0) {
            close(err_pipe[0]);
            kill(pid, SIGKILL);
            exec_waitpid_retry(pid, NULL);
            return -1;
        }
    }
    if (exec_read_exec_error(err_pipe[0]) != 0) {
        if (stdin_writer > 0) {
            kill(stdin_writer, SIGKILL);
            exec_reap_writer(stdin_writer);
        }
        exec_waitpid_retry(pid, NULL);
        return -1;
    }
    cmd->pid = (int)pid;
    cmd->stdin_writer = (int)stdin_writer;
    cmd->started = 1;
    return 0;
}

int neverc_exec_cmd_wait(neverc_exec_cmd_t *cmd, neverc_exec_exit_status_t *st) {
    int wstatus = 0;
    int rc;
    if (!cmd || !cmd->started || cmd->pid <= 0) return -1;
    rc = exec_waitpid_retry((pid_t)cmd->pid, &wstatus);
    if (exec_reap_writer((pid_t)cmd->stdin_writer) != 0) rc = -1;
    if (rc == 0) exec_fill_wait_status(wstatus, st);
    cmd->pid = 0;
    cmd->stdin_writer = 0;
    cmd->started = 0;
    return rc;
}

int neverc_exec_cmd_kill(neverc_exec_cmd_t *cmd, int signum) {
    if (!cmd || !cmd->started || cmd->pid <= 0) return -1;
    return kill((pid_t)cmd->pid, signum) == 0 ? 0 : -1;
}

int neverc_exec_cmd_pid(const neverc_exec_cmd_t *cmd) {
    if (!cmd || !cmd->started || cmd->pid <= 0) return -1;
    return cmd->pid;
}

const char *neverc_exec_look_path(const char *file, char *buf, size_t cap) {
    return exec_look_in_path(file, getenv("PATH"), buf, cap);
}

#endif /* POSIX */
