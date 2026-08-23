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
#include <stdio.h>
#include <string.h>
#include <limits.h>

struct neverc_exec_cmd {
    char  *name;
    char **argv;
    int    argc;
    char  *dir;
    int    dir_invalid;
    char **env;
    int    env_count;
    int    env_invalid;
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
    if (!eq) return 0;
    if (eq != entry) return 1;
#if defined(NEVERC_PLATFORM_WINDOWS)
    /* Go dedupEnv: Windows per-drive cwd is "=C:=C:\path". */
    return strchr(entry + 1, '=') != NULL;
#else
    return 0;
#endif
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
    const char *base, *p, *slash;
    size_t n, i;
    char ext[8];
    if (!name || name[0] == '\0') return 0;
    slash = strrchr(name, '/');
    p = strrchr(name, '\\');
    if (p && (!slash || p > slash)) slash = p;
    base = slash ? slash + 1 : name;
    n = strlen(base);
    while (n > 0 && (base[n - 1] == ' ' || base[n - 1] == '.')) n--;
    /* Drive-relative "C:payload.bat" — the colon is the drive letter, not
     * an ADS separator. Skip it before the ADS strip so BatBadBut still
     * classifies the name (CVE-2024-24576). */
    if (n >= 2 &&
        ((base[0] >= 'A' && base[0] <= 'Z') ||
         (base[0] >= 'a' && base[0] <= 'z')) &&
        base[1] == ':') {
        base += 2;
        n -= 2;
    }
    /* NTFS ADS: file.bat::$DATA / file.bat:stream still invoke cmd.exe. */
    for (i = 0; i < n; i++) {
        if (base[i] == ':') {
            n = i;
            break;
        }
    }
    while (n > 0 && (base[n - 1] == ' ' || base[n - 1] == '.')) n--;
    /* Do not prefix-truncate: "neverc_long_name.bat" must still match.
     * Stemless ".bat" / ".cmd" still invoke cmd.exe (Go filepath.Ext). */
    if (n < 4) return 0;
    i = n;
    while (i > 0 && base[i - 1] != '.') i--;
    if (i == 0) return 0;
    if (n - i + 1 >= sizeof(ext)) return 0;
    ext[0] = '.';
    memcpy(ext + 1, base + i, n - i);
    ext[n - i + 1] = '\0';
    return exec_ascii_ieq(ext, ".bat") || exec_ascii_ieq(ext, ".cmd");
}

/* BatBadBut: CreateProcess of .bat/.cmd invokes cmd.exe, whose metacharacters
 * are not covered by CommandLineToArgvW quoting. Reject unsafe extra args. */
static int exec_batch_args_have_metachars(const neverc_exec_cmd_t *cmd) {
    int i;
    if (!cmd) return 1;
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

static int exec_batch_args_unsafe(const neverc_exec_cmd_t *cmd) {
    if (!cmd || !cmd->name) return 0;
    if (!exec_is_windows_batch_name(cmd->name) &&
        !(cmd->argv && cmd->argv[0] && exec_is_windows_batch_name(cmd->argv[0])))
        return 0;
    return exec_batch_args_have_metachars(cmd);
}

/* Go validateLookPath (CVE-2025-47906): "", ".", and ".." are not
 * executable names. filepath.Join(path_entry, ".") can collapse to a
 * PATH element that is itself a file; PATHEXT can turn that into a
 * sibling executable. */
static int exec_look_path_name_ok(const char *file) {
    if (!file || file[0] == '\0') return 0;
    if (file[0] == '.' && file[1] == '\0') return 0;
    if (file[0] == '.' && file[1] == '.' && file[2] == '\0') return 0;
    return 1;
}

/* Go dedupEnv: key is bytes before the first '='. Windows "=C:=C:\\path"
 * uses the second '=' so the key is "=C:". */
static int exec_env_key_span(const char *entry, size_t *klen) {
    const char *eq;
    if (!entry || !klen) return -1;
    eq = strchr(entry, '=');
    if (!eq) return -1;
    if (eq == entry) {
        eq = strchr(entry + 1, '=');
        if (!eq) return -1;
    }
    *klen = (size_t)(eq - entry);
    return 0;
}

static int exec_env_key_same(const char *a, size_t alen,
                             const char *b, size_t blen) {
    if (alen != blen) return 0;
#if defined(NEVERC_PLATFORM_WINDOWS)
    return _strnicmp(a, b, (int)alen) == 0;
#else
    return memcmp(a, b, alen) == 0;
#endif
}

static int exec_env_superseded(char **env, int count, int i) {
    size_t klen;
    int j;
    if (!env || !env[i] || exec_env_key_span(env[i], &klen) != 0)
        return 0;
    for (j = i + 1; j < count; j++) {
        size_t later;
        if (!env[j] || exec_env_key_span(env[j], &later) != 0)
            continue;
        if (exec_env_key_same(env[i], klen, env[j], later))
            return 1;
    }
    return 0;
}

static const char *exec_env_path(char **env, int env_count) {
    const char *found = NULL;
    int i;
    if (!env) return NULL;
    /* Go dedupEnvCase: duplicate keys keep the last value. */
    for (i = 0; i < env_count; i++) {
        if (!env[i]) continue;
#if defined(NEVERC_PLATFORM_WINDOWS)
        if (_strnicmp(env[i], "PATH=", 5) == 0)
            found = env[i] + 5;
#else
        if (strncmp(env[i], "PATH=", 5) == 0)
            found = env[i] + 5;
#endif
    }
    return found;
}

static char *exec_join_dir_file(const char *dir, size_t dlen,
                                const char *file, size_t flen,
                                int add_sep, char sep, const char *ext) {
    size_t elen = ext ? strlen(ext) : 0;
    size_t n, o;
    char *p;
    if (!dir || !file) return NULL;
    if (dlen > SIZE_MAX - 3 || flen > SIZE_MAX - 3 - dlen ||
        elen > SIZE_MAX - 3 - dlen - flen)
        return NULL;
    n = dlen + (add_sep ? 1 : 0) + flen + elen + 1;
    p = (char *)malloc(n);
    if (!p) return NULL;
    memcpy(p, dir, dlen);
    o = dlen;
    if (add_sep) p[o++] = sep;
    memcpy(p + o, file, flen);
    o += flen;
    if (elen) {
        memcpy(p + o, ext, elen);
        o += elen;
    }
    p[o] = '\0';
    return p;
}

static const char *exec_finish_look(char *buf, size_t cap, char *found) {
    size_t n;
    if (!found) return NULL;
    n = strlen(found);
    if (!buf || cap == 0 || n >= cap) {
        free(found);
        return NULL;
    }
    memcpy(buf, found, n + 1);
    free(found);
    return buf;
}

neverc_exec_cmd_t *neverc_exec_command(const char *name, const char **args, int argc) {
    if (!name || name[0] == '\0' || argc < 0 || argc == INT_MAX ||
        (argc > 0 && !args))
        return NULL;
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
    if (dir && !copy) {
        cmd->dir_invalid = 1;
        return;
    }
    free(cmd->dir);
    cmd->dir = copy;
    cmd->dir_invalid = 0;
}

void neverc_exec_cmd_set_env(neverc_exec_cmd_t *cmd, const char **env, int env_count) {
    if (!cmd) return;
    if (env_count < 0 || (env_count > 0 && !env) ||
        (size_t)env_count > SIZE_MAX / sizeof(char *) - 1) {
        cmd->env_invalid = 1;
        return;
    }
    for (int i = 0; i < env_count; i++) {
        if (!env[i] || !exec_env_entry_ok(env[i])) {
            cmd->env_invalid = 1;
            return;
        }
    }
    char **copy = exec_copy_strings(env, env_count);
    if (!copy) {
        cmd->env_invalid = 1;
        return;
    }
    exec_free_strings(cmd->env, cmd->env_count);
    cmd->env = copy;
    cmd->env_count = env_count;
    cmd->env_invalid = 0;
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
    if (cmd->env_invalid || cmd->dir_invalid) return -1;
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
        size_t item_length;
        if (exec_env_superseded(cmd->env, cmd->env_count, i))
            continue;
        item_length = strlen(cmd->env[i]);
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
        size_t item_length;
        if (exec_env_superseded(cmd->env, cmd->env_count, i))
            continue;
        item_length = strlen(cmd->env[i]);
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

static int exec_win_is_file(const char *path) {
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES &&
           (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

/* CRT getenv() is a stale snapshot vs SetEnvironmentVariableA / Win32
 * LookPath. Read PATHEXT the same way PATH is read. */
static const char *exec_win_pathext(char *buf, DWORD cap) {
    DWORD n;
    if (!buf || cap == 0)
        return ".COM;.EXE;.BAT;.CMD";
    n = GetEnvironmentVariableA("PATHEXT", buf, cap);
    if (n == 0 || n >= cap || !buf[0])
        return ".COM;.EXE;.BAT;.CMD";
    return buf;
}

/* Go hasExt: a '.' after the last volume/path separator. */
static int exec_win_has_ext(const char *file) {
    const char *dot;
    const char *sep = NULL;
    const char *p;
    if (!file) return 0;
    dot = strrchr(file, '.');
    if (!dot) return 0;
    for (p = file; *p; p++) {
        if (*p == ':' || *p == '\\' || *p == '/')
            sep = p;
    }
    return !sep || sep < dot;
}

/* Go findExecutable: accept the exact name only when it has an extension,
 * then each PATHEXT entry even when the name already has one
 * ("a.exe" → "a.exe.exe"). Extensionless PATH hits are ignored so a
 * file named "tool" cannot shadow "tool.exe" (Go lookpath_windows_test).
 * 0 = copied into buf, -2 = exists but does not fit, -1 = not found. */
static int exec_win_try_with_exe(const char *file, char *buf, DWORD cap) {
    size_t n;
    const char *exts;
    char pathext[4096];
    if (!file || !buf || cap == 0) return -1;
    if (exec_win_has_ext(file) && exec_win_is_file(file)) {
        n = strlen(file);
        if (n >= cap) return -2;
        memcpy(buf, file, n + 1);
        return 0;
    }
    exts = exec_win_pathext(pathext, (DWORD)sizeof(pathext));
    while (*exts) {
        const char *end = strchr(exts, ';');
        size_t elen = end ? (size_t)(end - exts) : strlen(exts);
        if (elen > 0) {
            size_t flen = strlen(file);
            int dotted = exts[0] == '.';
            size_t need = flen + (dotted ? 0U : 1U) + elen;
            if (need < cap) {
                int written = dotted
                    ? snprintf(buf, cap, "%s%.*s", file, (int)elen, exts)
                    : snprintf(buf, cap, "%s.%.*s", file, (int)elen, exts);
                if (written >= 0 && (DWORD)written < cap &&
                    exec_win_is_file(buf))
                    return 0;
            } else {
                char *full = (char *)malloc(need + 1U);
                int exists;
                if (!full) return -1;
                if (dotted)
                    (void)snprintf(full, need + 1U, "%s%.*s",
                                   file, (int)elen, exts);
                else
                    (void)snprintf(full, need + 1U, "%s.%.*s",
                                   file, (int)elen, exts);
                exists = exec_win_is_file(full);
                free(full);
                if (exists) return -2;
            }
        }
        if (!end)
            break;
        exts = end + 1;
    }
    return -1;
}

static int exec_win_is_drive_cwd(const char *dir, size_t n) {
    return n == 2 &&
           ((dir[0] >= 'A' && dir[0] <= 'Z') ||
            (dir[0] >= 'a' && dir[0] <= 'z')) &&
           dir[1] == ':';
}

static int exec_win_is_sep(char c) {
    return c == '\\' || c == '/';
}

static int exec_win_prefix_fold(const char *s, size_t slen,
                                const char *prefix, size_t plen) {
    size_t i;
    if (slen < plen) return 0;
    for (i = 0; i < plen; i++) {
        char a = s[i], b = prefix[i];
        if (exec_win_is_sep(b)) {
            if (!exec_win_is_sep(a)) return 0;
            continue;
        }
        if (a >= 'a' && a <= 'z') a = (char)(a - 32);
        if (b >= 'a' && b <= 'z') b = (char)(b - 32);
        if (a != b) return 0;
    }
    return 1;
}

static size_t exec_win_unc_end(const char *path, size_t n, size_t prefix) {
    int count = 0;
    size_t i;
    for (i = prefix; i < n; i++) {
        if (exec_win_is_sep(path[i])) {
            count++;
            if (count == 2) return i;
        }
    }
    return n;
}

/* Go validVolumeNameLen: a volume that contains ".." is not a volume. */
static size_t exec_win_valid_vol(const char *path, size_t n) {
    size_t i = 0;
    while (i < n) {
        size_t start = i;
        while (i < n && !exec_win_is_sep(path[i])) i++;
        if (i - start == 2 && path[start] == '.' && path[start + 1] == '.')
            return 0;
        if (i < n) i++;
    }
    return n;
}

/* Go filepath.VolumeNameLen + IsAbs. Drive-relative "C:foo", current-drive
 * "\foo", and ".\bin" are not absolute. UNC / \\?\ stay absolute after a
 * trailing `..` as long as `..` is not inside the volume (`\\i\..\c$`). */
static size_t exec_win_volume_len(const char *path, size_t n) {
    size_t i;
    if (n >= 2 && path[1] == ':') return 2;
    if (n == 0 || !exec_win_is_sep(path[0])) return 0;
    if (exec_win_prefix_fold(path, n, "\\\\.", 3) ||
        exec_win_prefix_fold(path, n, "\\\\?", 3) ||
        exec_win_prefix_fold(path, n, "\\??", 3)) {
        if (n == 3) return 3;
        if (n >= 7 && exec_win_prefix_fold(path + 4, n - 4, "UNC", 3))
            return exec_win_valid_vol(path, exec_win_unc_end(path, n, 8));
        for (i = 0; i < n - 4; i++) {
            if (exec_win_is_sep(path[4 + i]))
                return exec_win_valid_vol(path, 4 + i);
        }
        return exec_win_valid_vol(path, n);
    }
    if (n >= 2 && exec_win_is_sep(path[1]))
        return exec_win_valid_vol(path, exec_win_unc_end(path, n, 2));
    return 0;
}

static int exec_win_path_is_abs(const char *dir, size_t n) {
    size_t vol = exec_win_volume_len(dir, n);
    if (vol == 0) return 0;
    /* Go filepath.IsAbs / neverc_filepath_isabs: two separators, or
     * volume followed by a separator. `\??\C:` is not absolute. */
    if (n >= 2 && exec_win_is_sep(dir[0]) && exec_win_is_sep(dir[1]))
        return 1;
    return vol < n && exec_win_is_sep(dir[vol]);
}

static int exec_win_skip_path_dir(const char *dir, size_t dlen) {
    if (dlen == 0) return 1;
    return !exec_win_path_is_abs(dir, dlen);
}

/* Search PATH only. SearchPathA(NULL) also tries the application directory
 * and the current directory — cwd exe hijacking (Go 1.19+ LookPath). */
static const char *exec_look_in_win_path(const char *file, const char *path_env,
                                         char *buf, DWORD cap) {
    const char *p;
    if (!file || !exec_look_path_name_ok(file) || !buf || cap == 0 ||
        !path_env || path_env[0] == '\0')
        return NULL;
    p = path_env;
    for (;;) {
        /* Go filepath.SplitList: do not split on ';' inside quotes, then
         * drop every '"' so `PATH="C:\foo;bar";C:\Windows` stays two
         * entries. */
        const char *q = p;
        const char *dir;
        size_t dlen, i, o;
        int in_quote = 0;
        char entry[32768];
        while (*q) {
            if (*q == '"') in_quote = !in_quote;
            else if (*q == ';' && !in_quote) break;
            q++;
        }
        dlen = (size_t)(q - p);
        if (dlen >= sizeof(entry))
            dlen = sizeof(entry) - 1;
        o = 0;
        for (i = 0; i < dlen; i++) {
            if (p[i] != '"')
                entry[o++] = p[i];
        }
        entry[o] = '\0';
        dir = entry;
        dlen = o;
        if (dlen > 0 && dlen <= (size_t)INT_MAX &&
            !exec_win_skip_path_dir(dir, dlen)) {
            size_t flen = strlen(file);
            /* Go filepath.Join: a final ':' (C: / \\?\C:) does not get '\'. */
            int trailing = dir[dlen - 1] == '\\' || dir[dlen - 1] == '/' ||
                           dir[dlen - 1] == ':' ||
                           exec_win_is_drive_cwd(dir, dlen);
            int add_sep = !trailing;
            char *cand;
            /* Heap-join so a PATH entry that does not fit `buf` is still
             * stat'd. Skipping it would let a later short directory win.
             * Pathless names use lookExtensions (PATHEXT), not only .exe. */
            cand = exec_join_dir_file(dir, dlen, file, flen, add_sep, '\\',
                                      NULL);
            if (!cand) return NULL;
            {
                int rc = exec_win_try_with_exe(cand, buf, cap);
                free(cand);
                if (rc == 0) return buf;
                if (rc == -2) return NULL;
            }
        }
        if (*q == '\0') break;
        p = q + 1;
    }
    return NULL;
}

static int exec_win_join_dir_and_name(const char *dir, const char *name,
                                      char *buf, DWORD cap) {
    size_t nlen;
    size_t dlen;
    int n;
    if (!dir || !name || !buf || cap == 0) return -1;
    nlen = strlen(name);
    dlen = strlen(dir);
    if (nlen == 0) return -1;
    /* \\server\share\path */
    if (nlen > 2 && (name[0] == '\\' || name[0] == '/') &&
        (name[1] == '\\' || name[1] == '/')) {
        n = snprintf(buf, cap, "%s", name);
        return (n < 0 || (DWORD)n >= cap) ? -1 : n;
    }
    if (nlen > 1 && name[1] == ':') {
        if (nlen == 2) return -1;
        if (name[2] == '\\' || name[2] == '/') {
            n = snprintf(buf, cap, "%s", name);
            return (n < 0 || (DWORD)n >= cap) ? -1 : n;
        }
        /* C:foo — same-volume drive-relative (Go joinExeDirAndFName). */
        if (dlen >= 1 &&
            ((dir[0] | 32) == (name[0] | 32))) {
            n = exec_win_is_drive_cwd(dir, dlen)
                    ? snprintf(buf, cap, "%s%s", dir, name + 2)
                    : snprintf(buf, cap, "%s\\%s", dir, name + 2);
            return (n < 0 || (DWORD)n >= cap) ? -1 : n;
        }
        n = snprintf(buf, cap, "%s", name);
        return (n < 0 || (DWORD)n >= cap) ? -1 : n;
    }
    /* \foo — Dir's drive + absolute-on-drive path. Do not Join as Dir\foo. */
    if (name[0] == '\\' || name[0] == '/') {
        if (dlen >= 2 && dir[1] == ':') {
            n = snprintf(buf, cap, "%c:%s", dir[0], name);
            return (n < 0 || (DWORD)n >= cap) ? -1 : n;
        }
        n = snprintf(buf, cap, "%s", name);
        return (n < 0 || (DWORD)n >= cap) ? -1 : n;
    }
    n = exec_win_is_drive_cwd(dir, dlen)
            ? snprintf(buf, cap, "%s%s", dir, name)
            : snprintf(buf, cap, "%s\\%s", dir, name);
    return (n < 0 || (DWORD)n >= cap) ? -1 : n;
}

static const char *exec_windows_resolve_app(const neverc_exec_cmd_t *cmd,
                                            char *buf, DWORD cap) {
    const char *name;
    const char *path_env;
    char path_storage[32768];
    if (!cmd->name || cmd->name[0] == '\0' || !buf || cap == 0) return NULL;
    name = cmd->name;
    /* StartProcess joins argv0 against Dir (Go syscall.joinExeDirAndFName).
     * Pathless names still go through LookPath first. */
    if (cmd->dir && cmd->dir[0] != '\0' &&
        (strchr(name, '\\') || strchr(name, '/') ||
         (name[0] && name[1] == ':'))) {
        if (exec_win_join_dir_and_name(cmd->dir, name, buf, cap) < 0)
            return NULL;
        name = buf;
    }
    if (strchr(name, '\\') || strchr(name, '/') ||
        (name[0] && name[1] == ':'))
        return exec_win_try_with_exe(name, buf, cap) == 0 ? buf : NULL;
    path_env = exec_env_path(cmd->env, cmd->env_count);
    /* Custom Env without PATH must not fall back to the process PATH
     * (Go os/exec LookPath). CreateProcessA(NULL, ...) would also search. */
    if (cmd->env && (!path_env || path_env[0] == '\0'))
        return NULL;
    if (!cmd->env) {
        DWORD n = GetEnvironmentVariableA("PATH", path_storage,
                                          (DWORD)sizeof(path_storage));
        if (n == 0 || n >= sizeof(path_storage)) return NULL;
        path_env = path_storage;
    }
    return exec_look_in_win_path(name, path_env, buf, cap);
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
    if (!app) {
        free(environment);
        free(cmdline);
        goto setup_error;
    }
    if (exec_is_windows_batch_name(app) && exec_batch_args_have_metachars(cmd)) {
        free(environment);
        free(cmdline);
        goto setup_error;
    }

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
    if (!app) {
        free(environment);
        free(cmdline);
        goto setup_error;
    }
    if (exec_is_windows_batch_name(app) && exec_batch_args_have_metachars(cmd)) {
        free(environment);
        free(cmdline);
        goto setup_error;
    }

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
    char path_storage[32768];
    DWORD n;
    if (!file || !exec_look_path_name_ok(file) || !buf || cap == 0 ||
        cap > MAXDWORD)
        return NULL;
    if (strchr(file, '\\') || strchr(file, '/') ||
        (file[0] && file[1] == ':')) {
        size_t flen = strlen(file);
        /* Go LookPath: a relative / drive-relative / current-drive path is
         * ErrDot even when the file exists (".\\a.exe", "C:a.exe", "\\a.exe").
         * UNC with ".." in the volume is also not IsAbs. Absolute names
         * still get ".exe" when they have no extension (lookExtensions). */
        if (exec_win_skip_path_dir(file, flen) ||
            exec_win_try_with_exe(file, buf, (DWORD)cap) != 0)
            return NULL;
        return buf;
    }
    n = GetEnvironmentVariableA("PATH", path_storage, (DWORD)sizeof(path_storage));
    if (n == 0 || n >= sizeof(path_storage)) return NULL;
    return exec_look_in_win_path(file, path_storage, buf, (DWORD)cap);
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

/* Go 1.19+ LookPath ErrDot: empty PATH, cwd aliases (".", "./"),
 * relative dirs that Clean through `..`, and any other non-absolute
 * entry ("./bin", "bin") must not be a successful hit. */
static int exec_posix_skip_path_dir(const char *dir, size_t dlen) {
    if (dlen == 0) return 1;
    return dir[0] != '/';
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
    if (!file || !exec_look_path_name_ok(file) || !buf || cap == 0)
        return NULL;
    if (strchr(file, '/')) {
        /* Go LookPath("./tool") is ErrDot; only an absolute path is a hit. */
        if (file[0] != '/')
            return NULL;
        if (exec_is_exe_file(file)) {
            size_t flen = strlen(file);
            if (flen < cap) { memcpy(buf, file, flen + 1); return buf; }
        }
        return NULL;
    }
    /* PATH="" is no search list (Go SplitList("")). Empty, ".", and other
     * relative components are cwd hits (Go ErrDot) and are skipped. */
    if (!path_env || path_env[0] == '\0') return NULL;

    const char *p = path_env;
    size_t flen = strlen(file);
    for (;;) {
        const char *colon = strchr(p, ':');
        size_t dlen = colon ? (size_t)(colon - p) : strlen(p);
        if (!exec_posix_skip_path_dir(p, dlen) && dlen > 0) {
            char *cand = exec_join_dir_file(p, dlen, file, flen, 1, '/', NULL);
            if (!cand) return NULL;
            if (exec_is_exe_file(cand))
                return exec_finish_look(buf, cap, cand);
            free(cand);
        }
        if (!colon) break;
        p = colon + 1;
    }
    return NULL;
}

/* Go Cmd.Start looks up a pathless name in the parent, then chdir.
 * Searching PATH after chdir would run Dir/name when PATH contains ".". */
static int exec_posix_resolve_pathless(const neverc_exec_cmd_t *cmd,
                                       char *buf, size_t cap) {
    const char *path;
    char found[4096];
    char cwd[4096];
    const char *rel;
    int n;

    if (!cmd || !cmd->name || !buf || cap == 0)
        return -1;
    if (strchr(cmd->name, '/'))
        return 0;
    if (cmd->env)
        path = exec_env_path(cmd->env, cmd->env_count);
    else
        path = getenv("PATH");
    if (!exec_look_in_path(cmd->name, path, found, sizeof(found)))
        return -1;
    if (found[0] == '/') {
        size_t flen = strlen(found);
        if (flen >= cap) return -1;
        memcpy(buf, found, flen + 1);
        return 1;
    }
    if (!getcwd(cwd, sizeof(cwd)))
        return -1;
    rel = found;
    if (rel[0] == '.' && rel[1] == '/')
        rel += 2;
    n = snprintf(buf, cap, "%s/%s", cwd, rel);
    if (n < 0 || (size_t)n >= cap)
        return -1;
    return 1;
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

static char **exec_env_child_array(char **env, int count) {
    char **out;
    int i, n = 0;
    if (!env) return NULL;
    out = (char **)calloc((size_t)count + 1, sizeof(char *));
    if (!out) return NULL;
    for (i = 0; i < count; i++) {
        if (env[i] && !exec_env_superseded(env, count, i))
            out[n++] = env[i];
    }
    return out;
}

static void exec_posix_do_exec(neverc_exec_cmd_t *cmd) {
    if (cmd->env) {
        char **child_env = exec_env_child_array(cmd->env, cmd->env_count);
        if (!child_env) return;
        if (strchr(cmd->name, '/')) {
            execve(cmd->name, cmd->argv, child_env);
        } else {
            char resolved[4096];
            const char *path = exec_env_path(cmd->env, cmd->env_count);
            if (path &&
                exec_look_in_path(cmd->name, path, resolved, sizeof(resolved)))
                execve(resolved, cmd->argv, child_env);
        }
        free(child_env);
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
    size_t got = 0;
    while (got < sizeof(exec_err)) {
        ssize_t n = read(err_rd, (char *)&exec_err + got,
                         sizeof(exec_err) - got);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) {
            close(err_rd);
            return -1;
        }
        if (n == 0) break;
        got += (size_t)n;
    }
    close(err_rd);
    if (got == 0) return 0;
    return -1;
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
    char resolved[4096];
    char *saved_name;
    int resolved_rc;
    if (exec_prepare(cmd, capture_stdout || capture_stderr, out, st) != 0) return -1;

    saved_name = cmd->name;
    resolved_rc = exec_posix_resolve_pathless(cmd, resolved, sizeof(resolved));
    if (resolved_rc < 0) return -1;
    if (resolved_rc > 0) cmd->name = resolved;

    if (exec_pipe(err_pipe) < 0) {
        cmd->name = saved_name;
        return -1;
    }
    if (capture_stdout || capture_stderr) {
        if (exec_pipe(stdout_pipe) < 0) {
            close(err_pipe[0]); close(err_pipe[1]);
            cmd->name = saved_name;
            return -1;
        }
    }
    if (cmd->stdin_data && cmd->stdin_len > 0) {
        if (exec_pipe(stdin_pipe) < 0) {
            close(err_pipe[0]); close(err_pipe[1]);
            if (stdout_pipe[0] >= 0) { close(stdout_pipe[0]); close(stdout_pipe[1]); }
            cmd->name = saved_name;
            return -1;
        }
    }

    pid = fork();
    if (pid != 0)
        cmd->name = saved_name;
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
    char resolved[4096];
    char *saved_name;
    int resolved_rc;
    if (exec_prepare(cmd, 0, NULL, NULL) != 0) return -1;
    saved_name = cmd->name;
    resolved_rc = exec_posix_resolve_pathless(cmd, resolved, sizeof(resolved));
    if (resolved_rc < 0) return -1;
    if (resolved_rc > 0) cmd->name = resolved;
    if (exec_pipe(err_pipe) < 0) {
        cmd->name = saved_name;
        return -1;
    }
    if (cmd->stdin_data && cmd->stdin_len > 0) {
        if (exec_pipe(stdin_pipe) < 0) {
            close(err_pipe[0]); close(err_pipe[1]);
            cmd->name = saved_name;
            return -1;
        }
    }
    pid = fork();
    if (pid != 0)
        cmd->name = saved_name;
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
