/*
 * NeverC os/exec tests.
 * Tests process execution, output capture, LookPath.
 */
#include "neverc/std/os/exec.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#if !defined(_WIN32)
#include <unistd.h>
#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>
#else
#include <windows.h>
#endif

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

enum { BIDIRECTIONAL_BYTES = 256 * 1024 };

#define ASSERT_INT_EQ(expr, expected) do { \
    int _v = (int)(expr); int _e = (int)(expected); tests_run++; \
    if (_v == _e) { tests_passed++; } \
    else { tests_failed++; \
           printf("  FAIL: %s = %d, expected %d (line %d)\n", #expr, _v, _e, __LINE__); } \
} while(0)

#define ASSERT_TRUE(expr) do { tests_run++; \
    if (expr) tests_passed++; \
    else { tests_failed++; printf("  FAIL: %s (line %d)\n", #expr, __LINE__); } \
} while(0)

static void *local_memmem(const void *haystack, size_t haystacklen,
                          const void *needle, size_t needlelen) {
    if (needlelen == 0) return (void *)haystack;
    if (haystacklen < needlelen) return NULL;
    const unsigned char *h = (const unsigned char *)haystack;
    for (size_t i = 0; i <= haystacklen - needlelen; i++) {
        if (memcmp(h + i, needle, needlelen) == 0)
            return (void *)(h + i);
    }
    return NULL;
}
#define memmem local_memmem

static void test_command_echo(void) {
    printf("[echo]\n");
#if defined(_WIN32)
    const char *args[] = {"/C", "echo hello world"};
    neverc_exec_cmd_t *cmd = neverc_exec_command("cmd.exe", args, 2);
#else
    const char *args[] = {"hello", "world"};
    neverc_exec_cmd_t *cmd = neverc_exec_command("echo", args, 2);
#endif
    ASSERT_TRUE(cmd != NULL);

    neverc_exec_output_t out = {0};
    neverc_exec_exit_status_t st = {0};
    ASSERT_INT_EQ(neverc_exec_cmd_output(cmd, &out, &st), 0);
    ASSERT_INT_EQ(st.exit_code, 0);
    ASSERT_TRUE(out.len > 0);

    ASSERT_TRUE(memmem(out.data, out.len, "hello world", 11) != NULL);

    neverc_exec_output_free(&out);
    neverc_exec_cmd_free(cmd);
}

static void test_command_false(void) {
    printf("[false_exit_code]\n");
#if defined(_WIN32)
    const char *args[] = {"/C", "exit /B 1"};
    neverc_exec_cmd_t *cmd = neverc_exec_command("cmd.exe", args, 2);
#else
    neverc_exec_cmd_t *cmd = neverc_exec_command("false", NULL, 0);
#endif
    ASSERT_TRUE(cmd != NULL);

    neverc_exec_exit_status_t st = {0};
    ASSERT_INT_EQ(neverc_exec_cmd_run(cmd, &st), 0);
    ASSERT_TRUE(st.exit_code != 0);

    neverc_exec_cmd_free(cmd);
}

static void test_command_true(void) {
    printf("[true_exit_code]\n");
#if defined(_WIN32)
    const char *args[] = {"/C", "exit /B 0"};
    neverc_exec_cmd_t *cmd = neverc_exec_command("cmd.exe", args, 2);
#else
    neverc_exec_cmd_t *cmd = neverc_exec_command("true", NULL, 0);
#endif
    ASSERT_TRUE(cmd != NULL);

    neverc_exec_exit_status_t st = {0};
    ASSERT_INT_EQ(neverc_exec_cmd_run(cmd, &st), 0);
    ASSERT_INT_EQ(st.exit_code, 0);

    neverc_exec_cmd_free(cmd);
}

#if !defined(_WIN32)
static void test_command_stdin(void) {
    printf("[stdin_pipe]\n");
    const char *args[] = {"-c", "cat"};
    neverc_exec_cmd_t *cmd = neverc_exec_command("/bin/sh", args, 2);
    ASSERT_TRUE(cmd != NULL);

    neverc_exec_cmd_set_stdin(cmd, "input data", 10);

    neverc_exec_output_t out = {0};
    neverc_exec_exit_status_t st = {0};
    ASSERT_INT_EQ(neverc_exec_cmd_output(cmd, &out, &st), 0);
    ASSERT_INT_EQ(st.exit_code, 0);
    ASSERT_INT_EQ((int)out.len, 10);
    ASSERT_TRUE(memcmp(out.data, "input data", 10) == 0);

    neverc_exec_output_free(&out);
    neverc_exec_cmd_free(cmd);
}
#endif

static int run_bidirectional_child(void) {
    unsigned char block[4096];
    memset(block, 'O', sizeof(block));
    size_t remaining = BIDIRECTIONAL_BYTES;
    while (remaining > 0) {
        size_t chunk = remaining < sizeof(block) ? remaining : sizeof(block);
        if (fwrite(block, 1, chunk, stdout) != chunk)
            return 1;
        remaining -= chunk;
    }
    if (fflush(stdout) != 0)
        return 1;

    size_t input_length = 0;
    for (;;) {
        size_t count = fread(block, 1, sizeof(block), stdin);
        input_length += count;
        if (count < sizeof(block)) {
            if (ferror(stdin))
                return 1;
            break;
        }
    }
    return printf("\nstdin=%zu\n", input_length) < 0 ? 1 : 0;
}

static int run_stdin_eof_child(void) {
    unsigned char byte;
    if (fread(&byte, 1, 1, stdin) != 0)
        return 42;
    return feof(stdin) ? 0 : 43;
}

static void test_empty_stdin_case(const char *executable, int use_start) {
    neverc_exec_exit_status_t st = {0};
    int run_rc = -1;
#if defined(_WIN32)
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    HANDLE read_handle = NULL, write_handle = NULL;
    HANDLE saved_stdin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD written = 0;
    int setup_ok =
        saved_stdin != NULL && saved_stdin != INVALID_HANDLE_VALUE &&
        CreatePipe(&read_handle, &write_handle, &sa, 0) &&
        WriteFile(write_handle, "X", 1, &written, NULL) && written == 1;
    ASSERT_TRUE(setup_ok);
    if (!setup_ok) {
        if (read_handle) CloseHandle(read_handle);
        if (write_handle) CloseHandle(write_handle);
        return;
    }
    CloseHandle(write_handle);
    write_handle = NULL;
    int redirected = SetStdHandle(STD_INPUT_HANDLE, read_handle) != 0;
    ASSERT_TRUE(redirected);
    if (!redirected) {
        CloseHandle(read_handle);
        return;
    }
#else
    int saved_stdin = dup(STDIN_FILENO);
    int inherited_input[2] = {-1, -1};
    int setup_ok = saved_stdin >= 0 && pipe(inherited_input) == 0;
    ASSERT_TRUE(setup_ok);
    if (!setup_ok) {
        if (saved_stdin >= 0) close(saved_stdin);
        if (inherited_input[0] >= 0) close(inherited_input[0]);
        if (inherited_input[1] >= 0) close(inherited_input[1]);
        return;
    }
    ASSERT_INT_EQ((int)write(inherited_input[1], "X", 1), 1);
    close(inherited_input[1]);
    inherited_input[1] = -1;
    int redirected = dup2(inherited_input[0], STDIN_FILENO);
    ASSERT_INT_EQ(redirected, STDIN_FILENO);
    if (redirected != STDIN_FILENO) {
        close(inherited_input[0]);
        close(saved_stdin);
        return;
    }
    close(inherited_input[0]);
    inherited_input[0] = -1;
#endif

    const char *args[] = {"--stdin-eof-child"};
    neverc_exec_cmd_t *cmd = neverc_exec_command(executable, args, 1);
    ASSERT_TRUE(cmd != NULL);
    if (cmd) {
        neverc_exec_cmd_set_stdin(cmd, "", 0);
        if (use_start) {
            run_rc = neverc_exec_cmd_start(cmd);
            if (run_rc == 0)
                run_rc = neverc_exec_cmd_wait(cmd, &st);
        } else {
            run_rc = neverc_exec_cmd_run(cmd, &st);
        }
        neverc_exec_cmd_free(cmd);
    }

#if defined(_WIN32)
    ASSERT_TRUE(SetStdHandle(STD_INPUT_HANDLE, saved_stdin) != 0);
    CloseHandle(read_handle);
#else
    ASSERT_INT_EQ(dup2(saved_stdin, STDIN_FILENO), STDIN_FILENO);
    close(saved_stdin);
#endif
    ASSERT_INT_EQ(run_rc, 0);
    ASSERT_INT_EQ(st.exit_code, 0);
}

static void test_empty_stdin_is_eof(const char *executable) {
    printf("[empty_stdin_is_eof]\n");
    test_empty_stdin_case(executable, 0);
    test_empty_stdin_case(executable, 1);
}

static void test_bidirectional_pipes(const char *executable) {
    printf("[bidirectional_pipes]\n");
    const char *args[] = {"--bidirectional-child"};
    neverc_exec_cmd_t *cmd =
        neverc_exec_command(executable, args, 1);
    ASSERT_TRUE(cmd != NULL);
    if (!cmd) return;

    unsigned char *input = (unsigned char *)malloc(BIDIRECTIONAL_BYTES);
    ASSERT_TRUE(input != NULL);
    if (!input) {
        neverc_exec_cmd_free(cmd);
        return;
    }
    memset(input, 'I', BIDIRECTIONAL_BYTES);
    neverc_exec_cmd_set_stdin(cmd, input, BIDIRECTIONAL_BYTES);

    neverc_exec_output_t out = {0};
    neverc_exec_exit_status_t st = {0};
    ASSERT_INT_EQ(neverc_exec_cmd_output(cmd, &out, &st), 0);
    ASSERT_INT_EQ(st.exit_code, 0);
    ASSERT_TRUE(out.len >= BIDIRECTIONAL_BYTES);
    ASSERT_TRUE(memmem(out.data, out.len,
                       "stdin=262144", 12) != NULL);

    neverc_exec_output_free(&out);
    neverc_exec_cmd_free(cmd);
    free(input);
}

static void test_look_path(void) {
    printf("[look_path]\n");
    char buf[4096];

#if defined(_WIN32)
    const char *p = neverc_exec_look_path("cmd.exe", buf, sizeof(buf));
#else
    const char *p = neverc_exec_look_path("ls", buf, sizeof(buf));
#endif
    ASSERT_TRUE(p != NULL);
    ASSERT_TRUE(strlen(p) > 0);

#if defined(_WIN32)
    /* Go LookPath: a path-qualified name without an ext still tries .exe. */
    {
        char sysdir[MAX_PATH];
        char notepad[MAX_PATH];
        UINT n = GetSystemDirectoryA(sysdir, (UINT)sizeof(sysdir));
        ASSERT_TRUE(n > 0 && n < sizeof(sysdir));
        snprintf(notepad, sizeof(notepad), "%s\\notepad", sysdir);
        p = neverc_exec_look_path(notepad, buf, sizeof(buf));
        ASSERT_TRUE(p != NULL);
        ASSERT_TRUE(strlen(p) >= 4);
        ASSERT_TRUE(p[strlen(p) - 4] == '.' &&
                    (p[strlen(p) - 3] == 'e' || p[strlen(p) - 3] == 'E') &&
                    (p[strlen(p) - 2] == 'x' || p[strlen(p) - 2] == 'X') &&
                    (p[strlen(p) - 1] == 'e' || p[strlen(p) - 1] == 'E'));
        p = neverc_exec_look_path(".\\notepad", buf, sizeof(buf));
        ASSERT_TRUE(p == NULL);
    }
    /* Go lookExtensions walks PATHEXT, not only .exe. */
    {
        char dir[MAX_PATH], bat[MAX_PATH], absdir[MAX_PATH];
        FILE *pf;
        DWORD n;
        char old_path[32768];
        GetCurrentDirectoryA((DWORD)sizeof(absdir), absdir);
        snprintf(dir, sizeof(dir), "%s\\neverc_pathext_dir", absdir);
        snprintf(bat, sizeof(bat), "%s\\neverc_pathext_only.bat", dir);
        CreateDirectoryA(dir, NULL);
        pf = fopen(bat, "wb");
        ASSERT_TRUE(pf != NULL);
        if (pf) {
            fputs("@echo off\r\n", pf);
            fclose(pf);
        }
        n = GetEnvironmentVariableA("PATH", old_path, sizeof(old_path));
        SetEnvironmentVariableA("PATH", dir);
        SetEnvironmentVariableA("PATHEXT", ".BAT;.CMD;.EXE");
        p = neverc_exec_look_path("neverc_pathext_only", buf, sizeof(buf));
        ASSERT_TRUE(p != NULL);
        ASSERT_TRUE(strstr(p, ".bat") != NULL || strstr(p, ".BAT") != NULL);
        if (n > 0 && n < sizeof(old_path))
            SetEnvironmentVariableA("PATH", old_path);
        else
            SetEnvironmentVariableA("PATH", NULL);
        SetEnvironmentVariableA("PATHEXT", NULL);
        DeleteFileA(bat);
        RemoveDirectoryA(dir);
    }
    /* Go findExecutable: LookPath("a.exe") finds a.exe.exe. */
    {
        char dir[MAX_PATH], exe[MAX_PATH], absdir[MAX_PATH];
        FILE *pf;
        DWORD n;
        char old_path[32768];
        GetCurrentDirectoryA((DWORD)sizeof(absdir), absdir);
        snprintf(dir, sizeof(dir), "%s\\neverc_dbl_ext_dir", absdir);
        snprintf(exe, sizeof(exe), "%s\\a.exe.exe", dir);
        CreateDirectoryA(dir, NULL);
        pf = fopen(exe, "wb");
        ASSERT_TRUE(pf != NULL);
        if (pf) {
            fputs("MZ", pf);
            fclose(pf);
        }
        n = GetEnvironmentVariableA("PATH", old_path, sizeof(old_path));
        SetEnvironmentVariableA("PATH", dir);
        /* Lowercase PATHEXT so the joined name is a.exe.exe; strstr is
         * case-sensitive even though Win32 lookup is not. */
        SetEnvironmentVariableA("PATHEXT", ".exe;.bat");
        p = neverc_exec_look_path("a.exe", buf, sizeof(buf));
        ASSERT_TRUE(p != NULL);
        ASSERT_TRUE(p && (strstr(p, "a.exe.exe") != NULL ||
                          strstr(p, "a.exe.EXE") != NULL));
        if (n > 0 && n < sizeof(old_path))
            SetEnvironmentVariableA("PATH", old_path);
        else
            SetEnvironmentVariableA("PATH", NULL);
        SetEnvironmentVariableA("PATHEXT", NULL);
        DeleteFileA(exe);
        RemoveDirectoryA(dir);
    }
    /* A too-long first PATH hit must not fall through to a later directory. */
    {
        char dir[MAX_PATH], exe[MAX_PATH], absdir[MAX_PATH], windir[MAX_PATH];
        char pathbuf[MAX_PATH * 2], look_buf[16];
        FILE *pf;
        DWORD n;
        size_t prefix;
        char old_path[32768];
        GetCurrentDirectoryA((DWORD)sizeof(absdir), absdir);
        GetWindowsDirectoryA(windir, (UINT)sizeof(windir));
        snprintf(dir, sizeof(dir), "%s\\nclpl_", absdir);
        prefix = strlen(dir);
        memset(dir + prefix, 'a', 80);
        dir[prefix + 80] = '\0';
        snprintf(exe, sizeof(exe), "%s\\notepad.exe", dir);
        CreateDirectoryA(dir, NULL);
        pf = fopen(exe, "wb");
        ASSERT_TRUE(pf != NULL);
        if (pf) {
            fputs("MZ", pf);
            fclose(pf);
        }
        n = GetEnvironmentVariableA("PATH", old_path, sizeof(old_path));
        snprintf(pathbuf, sizeof(pathbuf), "%s;%s\\System32", dir, windir);
        SetEnvironmentVariableA("PATH", pathbuf);
        p = neverc_exec_look_path("notepad", look_buf, sizeof(look_buf));
        ASSERT_TRUE(p == NULL);
        if (n > 0 && n < sizeof(old_path))
            SetEnvironmentVariableA("PATH", old_path);
        else
            SetEnvironmentVariableA("PATH", NULL);
        DeleteFileA(exe);
        RemoveDirectoryA(dir);
    }
#endif

    p = neverc_exec_look_path("this_command_does_not_exist_xyz", buf, sizeof(buf));
    ASSERT_TRUE(p == NULL);
    ASSERT_TRUE(neverc_exec_look_path("", buf, sizeof(buf)) == NULL);
    ASSERT_TRUE(neverc_exec_look_path(".", buf, sizeof(buf)) == NULL);
    ASSERT_TRUE(neverc_exec_look_path("..", buf, sizeof(buf)) == NULL);

#if defined(_WIN32)
    {
        char planted[MAX_PATH];
        FILE *pf;
        snprintf(planted, sizeof(planted), "neverc_cwd_only_hijack.exe");
        pf = fopen(planted, "wb");
        ASSERT_TRUE(pf != NULL);
        if (pf) {
            fputs("MZ", pf);
            fclose(pf);
        }
        p = neverc_exec_look_path("neverc_cwd_only_hijack.exe", buf, sizeof(buf));
        ASSERT_TRUE(p == NULL);
        {
            char old_path[32768];
            DWORD n = GetEnvironmentVariableA("PATH", old_path, sizeof(old_path));
            SetEnvironmentVariableA("PATH", ".\\");
            p = neverc_exec_look_path("neverc_cwd_only_hijack.exe", buf, sizeof(buf));
            ASSERT_TRUE(p == NULL);
            SetEnvironmentVariableA("PATH", "./");
            p = neverc_exec_look_path("neverc_cwd_only_hijack.exe", buf, sizeof(buf));
            ASSERT_TRUE(p == NULL);
            SetEnvironmentVariableA("PATH", "./.");
            p = neverc_exec_look_path("neverc_cwd_only_hijack.exe", buf, sizeof(buf));
            ASSERT_TRUE(p == NULL);
            SetEnvironmentVariableA("PATH", "C:");
            p = neverc_exec_look_path("neverc_cwd_only_hijack.exe", buf, sizeof(buf));
            ASSERT_TRUE(p == NULL);
            /* Win32 treats ". " as "." (cwd). Must not be a PATH hit. */
            SetEnvironmentVariableA("PATH", ". ");
            p = neverc_exec_look_path("neverc_cwd_only_hijack.exe", buf, sizeof(buf));
            ASSERT_TRUE(p == NULL);
            SetEnvironmentVariableA("PATH", ".  ");
            p = neverc_exec_look_path("neverc_cwd_only_hijack.exe", buf, sizeof(buf));
            ASSERT_TRUE(p == NULL);
            SetEnvironmentVariableA("PATH", "\". \"");
            p = neverc_exec_look_path("neverc_cwd_only_hijack.exe", buf, sizeof(buf));
            ASSERT_TRUE(p == NULL);
            /* Go LookPath: explicit relative / drive-relative names are ErrDot. */
            p = neverc_exec_look_path(".\\neverc_cwd_only_hijack.exe", buf, sizeof(buf));
            ASSERT_TRUE(p == NULL);
            p = neverc_exec_look_path("./neverc_cwd_only_hijack.exe", buf, sizeof(buf));
            ASSERT_TRUE(p == NULL);
            p = neverc_exec_look_path("..\\neverc_cwd_only_hijack.exe", buf, sizeof(buf));
            ASSERT_TRUE(p == NULL);
            {
                char drive_rel[64];
                char cwd[MAX_PATH];
                DWORD clen = GetCurrentDirectoryA(sizeof(cwd), cwd);
                if (clen > 0 && clen < sizeof(cwd) &&
                    ((cwd[0] >= 'A' && cwd[0] <= 'Z') ||
                     (cwd[0] >= 'a' && cwd[0] <= 'z')) && cwd[1] == ':')
                    snprintf(drive_rel, sizeof(drive_rel),
                             "%c:neverc_cwd_only_hijack.exe", cwd[0]);
                else
                    snprintf(drive_rel, sizeof(drive_rel),
                             "C:neverc_cwd_only_hijack.exe");
                p = neverc_exec_look_path(drive_rel, buf, sizeof(buf));
                ASSERT_TRUE(p == NULL);
            }
            p = neverc_exec_look_path("\\neverc_cwd_only_hijack.exe", buf, sizeof(buf));
            ASSERT_TRUE(p == NULL);
            if (n > 0 && n < sizeof(old_path))
                SetEnvironmentVariableA("PATH", old_path);
            else
                SetEnvironmentVariableA("PATH", NULL);
        }
        DeleteFileA(planted);
    }
    {
        char old_path[32768];
        FILE *pf;
        DWORD n = GetEnvironmentVariableA("PATH", old_path, sizeof(old_path));
        CreateDirectoryA("neverc_lp_dir", NULL);
        pf = fopen("neverc_lp_dir\\neverc_noext_hijack", "wb");
        ASSERT_TRUE(pf != NULL);
        if (pf) {
            fputs("MZ", pf);
            fclose(pf);
        }
        SetEnvironmentVariableA("PATH", "neverc_lp_dir");
        p = neverc_exec_look_path("neverc_noext_hijack", buf, sizeof(buf));
        ASSERT_TRUE(p == NULL);
        DeleteFileA("neverc_lp_dir\\neverc_noext_hijack");
        RemoveDirectoryA("neverc_lp_dir");

        /* Absolute PATH: extensionless "tool" must not shadow tool.exe. */
        {
            char evil_dir[MAX_PATH], good_dir[MAX_PATH];
            char abs_evil[MAX_PATH], abs_good[MAX_PATH];
            char path_both[MAX_PATH * 2 + 8];
            CreateDirectoryA("neverc_extless_evil", NULL);
            CreateDirectoryA("neverc_extless_good", NULL);
            GetFullPathNameA("neverc_extless_evil", sizeof(abs_evil),
                             abs_evil, NULL);
            GetFullPathNameA("neverc_extless_good", sizeof(abs_good),
                             abs_good, NULL);
            snprintf(evil_dir, sizeof(evil_dir), "%s\\tool", abs_evil);
            snprintf(good_dir, sizeof(good_dir), "%s\\tool.exe", abs_good);
            {
                char sysdir[MAX_PATH], src[MAX_PATH];
                UINT sn = GetSystemDirectoryA(sysdir, (UINT)sizeof(sysdir));
                ASSERT_TRUE(sn > 0 && sn < sizeof(sysdir));
                snprintf(src, sizeof(src), "%s\\cmd.exe", sysdir);
                ASSERT_TRUE(CopyFileA(src, evil_dir, FALSE));
                ASSERT_TRUE(CopyFileA(src, good_dir, FALSE));
            }
            SetEnvironmentVariableA("PATH", abs_evil);
            p = neverc_exec_look_path("tool", buf, sizeof(buf));
            ASSERT_TRUE(p == NULL);
            snprintf(path_both, sizeof(path_both), "%s;%s", abs_evil, abs_good);
            SetEnvironmentVariableA("PATH", path_both);
            p = neverc_exec_look_path("tool", buf, sizeof(buf));
            ASSERT_TRUE(p != NULL);
            /* Default PATHEXT is ".EXE"; strstr is case-sensitive. */
            ASSERT_TRUE(p && (strstr(p, "tool.exe") != NULL ||
                              strstr(p, "tool.EXE") != NULL));
            DeleteFileA(evil_dir);
            DeleteFileA(good_dir);
            RemoveDirectoryA("neverc_extless_evil");
            RemoveDirectoryA("neverc_extless_good");
        }

        /* Relative PATH with a real component is still cwd-relative (ErrDot). */
        CreateDirectoryA("neverc_lp_rel", NULL);
        pf = fopen("neverc_lp_rel\\neverc_rel_hijack.exe", "wb");
        ASSERT_TRUE(pf != NULL);
        if (pf) {
            fputs("MZ", pf);
            fclose(pf);
        }
        SetEnvironmentVariableA("PATH", "neverc_lp_rel");
        p = neverc_exec_look_path("neverc_rel_hijack.exe", buf, sizeof(buf));
        ASSERT_TRUE(p == NULL);
        SetEnvironmentVariableA("PATH", ".\\neverc_lp_rel");
        p = neverc_exec_look_path("neverc_rel_hijack.exe", buf, sizeof(buf));
        ASSERT_TRUE(p == NULL);
        SetEnvironmentVariableA("PATH", "./neverc_lp_rel");
        p = neverc_exec_look_path("neverc_rel_hijack.exe", buf, sizeof(buf));
        ASSERT_TRUE(p == NULL);
        {
            neverc_exec_cmd_t *rel_cmd =
                neverc_exec_command("neverc_rel_hijack.exe", NULL, 0);
            neverc_exec_exit_status_t rel_st = {0};
            ASSERT_TRUE(rel_cmd != NULL);
            ASSERT_INT_EQ(neverc_exec_cmd_run(rel_cmd, &rel_st), -1);
            neverc_exec_cmd_free(rel_cmd);
        }
        DeleteFileA("neverc_lp_rel\\neverc_rel_hijack.exe");
        RemoveDirectoryA("neverc_lp_rel");

        pf = fopen("neverc_dotdot_hijack.exe", "wb");
        ASSERT_TRUE(pf != NULL);
        if (pf) {
            fputs("MZ", pf);
            fclose(pf);
        }
        CreateDirectoryA("neverc_path_dd", NULL);
        SetEnvironmentVariableA("PATH", "neverc_path_dd\\..");
        p = neverc_exec_look_path("neverc_dotdot_hijack.exe", buf, sizeof(buf));
        ASSERT_TRUE(p == NULL);
        SetEnvironmentVariableA("PATH", "neverc_path_dd\\.. ");
        p = neverc_exec_look_path("neverc_dotdot_hijack.exe", buf, sizeof(buf));
        ASSERT_TRUE(p == NULL);
        DeleteFileA("neverc_dotdot_hijack.exe");
        RemoveDirectoryA("neverc_path_dd");

        /* Go filepath.SplitList: quoted PATH entries may contain ';'. */
        CreateDirectoryA("neverc_lp;dir", NULL);
        pf = fopen("neverc_lp;dir\\neverc_semi_hit.exe", "wb");
        ASSERT_TRUE(pf != NULL);
        if (pf) {
            fputs("MZ", pf);
            fclose(pf);
        }
        SetEnvironmentVariableA("PATH", "\"neverc_lp;dir\"");
        p = neverc_exec_look_path("neverc_semi_hit.exe", buf, sizeof(buf));
        ASSERT_TRUE(p == NULL);
        {
            char abs_semi[MAX_PATH];
            DWORD alen = GetFullPathNameA("neverc_lp;dir", sizeof(abs_semi),
                                          abs_semi, NULL);
            if (alen > 0 && alen < sizeof(abs_semi)) {
                char quoted[MAX_PATH + 8];
                snprintf(quoted, sizeof(quoted), "\"%s\"", abs_semi);
                SetEnvironmentVariableA("PATH", quoted);
                p = neverc_exec_look_path("neverc_semi_hit.exe", buf,
                                          sizeof(buf));
                ASSERT_TRUE(p != NULL);
            }
        }
        DeleteFileA("neverc_lp;dir\\neverc_semi_hit.exe");
        RemoveDirectoryA("neverc_lp;dir");

        /* Go validVolumeNameLen: `..` inside a UNC volume is not IsAbs. */
        p = neverc_exec_look_path("\\\\i\\..\\c$\\neverc_missing.exe",
                                  buf, sizeof(buf));
        ASSERT_TRUE(p == NULL);

        /* Go filepath.Join keeps a final ':' so \\?\C:name is not C:\name. */
        SetEnvironmentVariableA("PATH", "\\\\?\\C:");
        p = neverc_exec_look_path("Windows\\System32\\cmd.exe", buf,
                                  sizeof(buf));
        ASSERT_TRUE(p == NULL);
        p = neverc_exec_look_path("cmd.exe", buf, sizeof(buf));
        if (p) {
            ASSERT_TRUE(_strnicmp(p, "\\\\?\\C:\\", 7) != 0);
        }
        /* `\??\C:` is not IsAbs; LookPath skips non-absolute PATH entries. */
        SetEnvironmentVariableA("PATH", "\\??\\C:");
        p = neverc_exec_look_path("cmd.exe", buf, sizeof(buf));
        ASSERT_TRUE(p == NULL);
        if (n > 0 && n < sizeof(old_path))
            SetEnvironmentVariableA("PATH", old_path);
        else
            SetEnvironmentVariableA("PATH", NULL);

        /* Explicit path + existing PE, no PATHEXT suffix (STD_TEST -o). */
        {
            char sysdir[MAX_PATH], src[MAX_PATH], dest[MAX_PATH];
            const char *true_args[] = {"/C", "exit /B 0"};
            neverc_exec_cmd_t *extless;
            neverc_exec_exit_status_t st = {0};
            UINT sn = GetSystemDirectoryA(sysdir, (UINT)sizeof(sysdir));
            ASSERT_TRUE(sn > 0 && sn < sizeof(sysdir));
            snprintf(src, sizeof(src), "%s\\cmd.exe", sysdir);
            ASSERT_TRUE(GetFullPathNameA("neverc_extless_argv0",
                                         (DWORD)sizeof(dest), dest,
                                         NULL) > 0);
            DeleteFileA(dest);
            ASSERT_TRUE(CopyFileA(src, dest, FALSE));
            extless = neverc_exec_command(dest, true_args, 2);
            ASSERT_TRUE(extless != NULL);
            ASSERT_INT_EQ(neverc_exec_cmd_run(extless, &st), 0);
            ASSERT_INT_EQ(st.exit_code, 0);
            neverc_exec_cmd_free(extless);
            DeleteFileA(dest);
        }

        /* Go lookExtensions: PATHEXT beats an extensionless sibling. */
        {
            char sysdir[MAX_PATH], src[MAX_PATH];
            char hijack[MAX_PATH], hijack_exe[MAX_PATH];
            const char *true_args[] = {"/C", "exit /B 0"};
            neverc_exec_cmd_t *cmd;
            neverc_exec_exit_status_t st = {0};
            FILE *hf;
            UINT sn = GetSystemDirectoryA(sysdir, (UINT)sizeof(sysdir));
            ASSERT_TRUE(sn > 0 && sn < sizeof(sysdir));
            snprintf(src, sizeof(src), "%s\\cmd.exe", sysdir);
            ASSERT_TRUE(GetFullPathNameA("neverc_pathext_wins",
                                         (DWORD)sizeof(hijack), hijack,
                                         NULL) > 0);
            snprintf(hijack_exe, sizeof(hijack_exe), "%s.exe", hijack);
            DeleteFileA(hijack);
            DeleteFileA(hijack_exe);
            hf = fopen(hijack, "wb");
            ASSERT_TRUE(hf != NULL);
            if (hf) {
                fputs("MZ", hf);
                fclose(hf);
            }
            ASSERT_TRUE(CopyFileA(src, hijack_exe, FALSE));
            cmd = neverc_exec_command(hijack, true_args, 2);
            ASSERT_TRUE(cmd != NULL);
            ASSERT_INT_EQ(neverc_exec_cmd_run(cmd, &st), 0);
            ASSERT_INT_EQ(st.exit_code, 0);
            neverc_exec_cmd_free(cmd);
            DeleteFileA(hijack);
            DeleteFileA(hijack_exe);
        }
    }
#endif

#if defined(_WIN32)
    {
        const char *empty_path[] = { "PATH=" };
        neverc_exec_cmd_t *empty_cmd = neverc_exec_command("cmd.exe", NULL, 0);
        neverc_exec_exit_status_t empty_st = {0};
        ASSERT_TRUE(empty_cmd != NULL);
        neverc_exec_cmd_set_env(empty_cmd, empty_path, 1);
        ASSERT_INT_EQ(neverc_exec_cmd_run(empty_cmd, &empty_st), -1);
        neverc_exec_cmd_free(empty_cmd);
    }
#endif

#if !defined(_WIN32)
    p = neverc_exec_look_path("bin", buf, sizeof(buf));
    if (p) {
        struct stat st;
        ASSERT_TRUE(stat(p, &st) == 0 && S_ISREG(st.st_mode));
    } else {
        tests_run++;
        tests_passed++;
    }
    ASSERT_TRUE(neverc_exec_look_path("/usr/bin", buf, sizeof(buf)) == NULL);

    const char *old_path = getenv("PATH");
    char *saved = old_path ? strdup(old_path) : NULL;
    char cwd[1024], script[1200], name[64];
    ASSERT_TRUE(getcwd(cwd, sizeof(cwd)) != NULL);
    snprintf(name, sizeof(name), "neverc_lookpath_%d", (int)getpid());
    snprintf(script, sizeof(script), "%s/%s", cwd, name);
    FILE *sf = fopen(script, "w");
    ASSERT_TRUE(sf != NULL);
    if (sf) {
        fputs("#!/bin/sh\nexit 0\n", sf);
        fclose(sf);
    }
    chmod(script, 0755);
    setenv("PATH", "", 1);
    p = neverc_exec_look_path(name, buf, sizeof(buf));
    ASSERT_TRUE(p == NULL);
    {
        const char *empty_path[] = { "PATH=" };
        neverc_exec_cmd_t *empty_cmd = neverc_exec_command(name, NULL, 0);
        neverc_exec_exit_status_t empty_st = {0};
        ASSERT_TRUE(empty_cmd != NULL);
        neverc_exec_cmd_set_env(empty_cmd, empty_path, 1);
        ASSERT_INT_EQ(neverc_exec_cmd_run(empty_cmd, &empty_st), -1);
        neverc_exec_cmd_free(empty_cmd);
    }
    mkdir("neverc_path_dd", 0755);
    setenv("PATH", "neverc_path_dd/..", 1);
    p = neverc_exec_look_path(name, buf, sizeof(buf));
    ASSERT_TRUE(p == NULL);
    rmdir("neverc_path_dd");
    setenv("PATH", ":", 1);
    p = neverc_exec_look_path(name, buf, sizeof(buf));
    ASSERT_TRUE(p == NULL);
    setenv("PATH", ".", 1);
    p = neverc_exec_look_path(name, buf, sizeof(buf));
    ASSERT_TRUE(p == NULL);
    setenv("PATH", "./", 1);
    p = neverc_exec_look_path(name, buf, sizeof(buf));
    ASSERT_TRUE(p == NULL);
    setenv("PATH", ".//", 1);
    p = neverc_exec_look_path(name, buf, sizeof(buf));
    ASSERT_TRUE(p == NULL);
    setenv("PATH", "./.", 1);
    p = neverc_exec_look_path(name, buf, sizeof(buf));
    ASSERT_TRUE(p == NULL);
    /* Go LookPath("./name") is ErrDot even when the file is in cwd. */
    {
        char rel[80];
        snprintf(rel, sizeof(rel), "./%s", name);
        p = neverc_exec_look_path(rel, buf, sizeof(buf));
        ASSERT_TRUE(p == NULL);
        snprintf(rel, sizeof(rel), "../%s", name);
        p = neverc_exec_look_path(rel, buf, sizeof(buf));
        ASSERT_TRUE(p == NULL);
    }
    mkdir("neverc_lp_rel", 0755);
    {
        char relbin[1280];
        snprintf(relbin, sizeof(relbin), "neverc_lp_rel/%s", name);
        FILE *rf = fopen(relbin, "w");
        ASSERT_TRUE(rf != NULL);
        if (rf) {
            fputs("#!/bin/sh\nexit 0\n", rf);
            fclose(rf);
        }
        chmod(relbin, 0755);
        setenv("PATH", "neverc_lp_rel", 1);
        p = neverc_exec_look_path(name, buf, sizeof(buf));
        ASSERT_TRUE(p == NULL);
        setenv("PATH", "./neverc_lp_rel", 1);
        p = neverc_exec_look_path(name, buf, sizeof(buf));
        ASSERT_TRUE(p == NULL);
        {
            neverc_exec_cmd_t *rel_cmd = neverc_exec_command(name, NULL, 0);
            neverc_exec_exit_status_t rel_st = {0};
            ASSERT_TRUE(rel_cmd != NULL);
            ASSERT_INT_EQ(neverc_exec_cmd_run(rel_cmd, &rel_st), -1);
            neverc_exec_cmd_free(rel_cmd);
        }
        unlink(relbin);
    }
    rmdir("neverc_lp_rel");
    /* Absolute path with a separator is still a hit (not ErrDot). */
    p = neverc_exec_look_path(script, buf, sizeof(buf));
    ASSERT_TRUE(p != NULL);
    /* CVE-2025-47906: a PATH element that is itself a file must not
     * make LookPath(".") / ".." resolve to that file. */
    setenv("PATH", script, 1);
    p = neverc_exec_look_path(".", buf, sizeof(buf));
    ASSERT_TRUE(p == NULL);
    p = neverc_exec_look_path("..", buf, sizeof(buf));
    ASSERT_TRUE(p == NULL);
    setenv("PATH", "./", 1);
    {
        neverc_exec_cmd_t *cwd_cmd = neverc_exec_command(name, NULL, 0);
        neverc_exec_exit_status_t cwd_st = {0};
        ASSERT_TRUE(cwd_cmd != NULL);
        ASSERT_INT_EQ(neverc_exec_cmd_run(cwd_cmd, &cwd_st), -1);
        neverc_exec_cmd_free(cwd_cmd);
    }
    setenv("PATH", ":/usr/bin:", 1);
    p = neverc_exec_look_path(name, buf, sizeof(buf));
    ASSERT_TRUE(p == NULL);
    unlink(script);
    if (saved) {
        setenv("PATH", saved, 1);
        free(saved);
    } else {
        unsetenv("PATH");
    }
#endif
}

static void test_combined_output(void) {
    printf("[combined_output]\n");
#if defined(_WIN32)
    const char *args[] = {"/C", "echo stdout && echo stderr 1>&2"};
    neverc_exec_cmd_t *cmd = neverc_exec_command("cmd.exe", args, 2);
#else
    const char *args[] = {"-c", "echo stdout; echo stderr >&2"};
    neverc_exec_cmd_t *cmd = neverc_exec_command("/bin/sh", args, 2);
#endif
    ASSERT_TRUE(cmd != NULL);

    neverc_exec_output_t out = {0};
    neverc_exec_exit_status_t st = {0};
    ASSERT_INT_EQ(neverc_exec_cmd_combined_output(cmd, &out, &st), 0);
    ASSERT_INT_EQ(st.exit_code, 0);
    ASSERT_TRUE(out.len > 0);

    ASSERT_TRUE(memmem(out.data, out.len, "stdout", 6) != NULL);
    ASSERT_TRUE(memmem(out.data, out.len, "stderr", 6) != NULL);

    neverc_exec_output_free(&out);
    neverc_exec_cmd_free(cmd);
}

#if !defined(_WIN32)
static int parse_int_output(const neverc_exec_output_t *out) {
    char tmp[32];
    size_t n = out->len < sizeof(tmp) - 1 ? out->len : sizeof(tmp) - 1;
    memcpy(tmp, out->data, n);
    tmp[n] = '\0';
    return atoi(tmp);
}

static int run_print_blocked_usr1(void) {
    sigset_t cur, empty;
    sigemptyset(&empty);
    if (sigprocmask(SIG_BLOCK, &empty, &cur) != 0) return 1;
    return printf("%d\n", sigismember(&cur, SIGUSR1)) < 0 ? 1 : 0;
}

static void test_unread_stdin_is_not_failure(void) {
    printf("[unread_stdin]\n");
    const char *args[] = {"-c", "exit 0"};
    neverc_exec_cmd_t *cmd = neverc_exec_command("/bin/sh", args, 2);
    ASSERT_TRUE(cmd != NULL);
    if (!cmd) return;

    size_t n = 256 * 1024;
    char *data = (char *)malloc(n);
    ASSERT_TRUE(data != NULL);
    if (!data) {
        neverc_exec_cmd_free(cmd);
        return;
    }
    memset(data, 'x', n);
    neverc_exec_cmd_set_stdin(cmd, data, n);

    neverc_exec_exit_status_t st = {0};
    ASSERT_INT_EQ(neverc_exec_cmd_run(cmd, &st), 0);
    ASSERT_INT_EQ(st.exit_code, 0);
    ASSERT_INT_EQ(st.signaled, 0);

    neverc_exec_cmd_free(cmd);
    free(data);
}

static void test_signaled_status(void) {
    printf("[signaled_status]\n");
    const char *args[] = {"-c", "kill -s USR1 $$"};
    neverc_exec_cmd_t *cmd = neverc_exec_command("/bin/sh", args, 2);
    ASSERT_TRUE(cmd != NULL);
    if (!cmd) return;

    neverc_exec_exit_status_t st = {0};
    ASSERT_INT_EQ(neverc_exec_cmd_run(cmd, &st), 0);
    ASSERT_INT_EQ(st.signaled, 1);
    ASSERT_INT_EQ(st.signal_num, SIGUSR1);
    ASSERT_INT_EQ(st.exit_code, -1);
    neverc_exec_cmd_free(cmd);
}

static void test_exec_resets_signal_mask(const char *executable) {
    printf("[exec_signal_mask]\n");
    sigset_t block, old;
    sigemptyset(&block);
    sigaddset(&block, SIGUSR1);
    ASSERT_INT_EQ(sigprocmask(SIG_BLOCK, &block, &old), 0);

    const char *args[] = {"--print-blocked-usr1"};
    neverc_exec_cmd_t *cmd = neverc_exec_command(executable, args, 1);
    ASSERT_TRUE(cmd != NULL);
    if (!cmd) {
        sigprocmask(SIG_SETMASK, &old, NULL);
        return;
    }

    neverc_exec_output_t out = {0};
    neverc_exec_exit_status_t st = {0};
    ASSERT_INT_EQ(neverc_exec_cmd_output(cmd, &out, &st), 0);
    ASSERT_INT_EQ(st.exit_code, 0);
    ASSERT_INT_EQ(parse_int_output(&out), 0);

    neverc_exec_output_free(&out);
    neverc_exec_cmd_free(cmd);
    sigprocmask(SIG_SETMASK, &old, NULL);
}

static void test_env_still_searches_path(void) {
    printf("[env_path_search]\n");
    const char *env[] = { "PATH=/usr/bin:/bin", "HOME=/" };
    neverc_exec_cmd_t *cmd = neverc_exec_command("true", NULL, 0);
    ASSERT_TRUE(cmd != NULL);
    neverc_exec_cmd_set_env(cmd, env, 2);
    neverc_exec_exit_status_t st = {0};
    ASSERT_INT_EQ(neverc_exec_cmd_run(cmd, &st), 0);
    ASSERT_INT_EQ(st.exit_code, 0);
    neverc_exec_cmd_free(cmd);
}

static void test_env_without_path_does_not_use_parent_path(void) {
    printf("[env_without_path]\n");
    const char *env[] = { "HOME=/" };
    neverc_exec_cmd_t *cmd = neverc_exec_command("true", NULL, 0);
    ASSERT_TRUE(cmd != NULL);
    neverc_exec_cmd_set_env(cmd, env, 1);
    neverc_exec_exit_status_t st = {0};
    ASSERT_INT_EQ(neverc_exec_cmd_run(cmd, &st), -1);
    neverc_exec_cmd_free(cmd);
}

static void test_set_dir(void) {
    printf("[set_dir]\n");
    const char *args[] = {"-c", "pwd"};
    neverc_exec_cmd_t *cmd = neverc_exec_command("/bin/sh", args, 2);
    ASSERT_TRUE(cmd != NULL);
    neverc_exec_cmd_set_dir(cmd, "/tmp");

    neverc_exec_output_t out = {0};
    neverc_exec_exit_status_t st = {0};
    ASSERT_INT_EQ(neverc_exec_cmd_output(cmd, &out, &st), 0);
    ASSERT_INT_EQ(st.exit_code, 0);

    char *outstr = (char *)malloc(out.len + 1);
    memcpy(outstr, out.data, out.len);
    outstr[out.len] = '\0';
    while (out.len > 0 && (outstr[out.len - 1] == '\n' || outstr[out.len - 1] == '\r'))
        outstr[--out.len] = '\0';

    tests_run++;
    if (strcmp(outstr, "/tmp") == 0 || strcmp(outstr, "/private/tmp") == 0)
        tests_passed++;
    else {
        tests_failed++;
        printf("  FAIL: pwd = '%s', expected '/tmp' or '/private/tmp'\n", outstr);
    }

    free(outstr);
    neverc_exec_output_free(&out);
    neverc_exec_cmd_free(cmd);
}

static void test_set_dir_does_not_research_path(void) {
    printf("[set_dir_pathless_lookpath]\n");
    char gooddir[256], evildir[256], goodbin[300], evilbin[300], pathenv[320];
    FILE *f;
    neverc_exec_cmd_t *cmd;
    neverc_exec_output_t out = {0};
    neverc_exec_exit_status_t st = {0};
    const char *env[1];
    int pid = (int)getpid();

    snprintf(gooddir, sizeof(gooddir), "/tmp/neverc_exec_good_%d", pid);
    snprintf(evildir, sizeof(evildir), "/tmp/neverc_exec_evil_%d", pid);
    mkdir(gooddir, 0700);
    mkdir(evildir, 0700);
    snprintf(goodbin, sizeof(goodbin), "%s/nctool", gooddir);
    snprintf(evilbin, sizeof(evilbin), "%s/nctool", evildir);

    f = fopen(goodbin, "w");
    ASSERT_TRUE(f != NULL);
    if (f) {
        fputs("#!/bin/sh\necho GOOD\n", f);
        fclose(f);
    }
    chmod(goodbin, 0755);
    f = fopen(evilbin, "w");
    ASSERT_TRUE(f != NULL);
    if (f) {
        fputs("#!/bin/sh\necho EVIL\n", f);
        fclose(f);
    }
    chmod(evilbin, 0755);

    snprintf(pathenv, sizeof(pathenv), "PATH=%s", gooddir);
    env[0] = pathenv;
    cmd = neverc_exec_command("nctool", NULL, 0);
    ASSERT_TRUE(cmd != NULL);
    neverc_exec_cmd_set_dir(cmd, evildir);
    neverc_exec_cmd_set_env(cmd, env, 1);
    ASSERT_INT_EQ(neverc_exec_cmd_output(cmd, &out, &st), 0);
    ASSERT_INT_EQ(st.exit_code, 0);
    ASSERT_TRUE(memmem(out.data, out.len, "GOOD", 4) != NULL);
    ASSERT_TRUE(memmem(out.data, out.len, "EVIL", 4) == NULL);

    neverc_exec_output_free(&out);
    neverc_exec_cmd_free(cmd);
    unlink(goodbin);
    unlink(evilbin);
    rmdir(gooddir);
    rmdir(evildir);
}

static void test_env_path_last_wins(void) {
    printf("[env_path_last_wins]\n");
    char gooddir[256], evildir[256], goodbin[300], evilbin[300];
    char evil_env[320], good_env[320];
    FILE *f;
    neverc_exec_cmd_t *cmd;
    neverc_exec_output_t out = {0};
    neverc_exec_exit_status_t st = {0};
    const char *env[4];
    int pid = (int)getpid();

    snprintf(gooddir, sizeof(gooddir), "/tmp/neverc_exec_last_good_%d", pid);
    snprintf(evildir, sizeof(evildir), "/tmp/neverc_exec_last_evil_%d", pid);
    mkdir(gooddir, 0700);
    mkdir(evildir, 0700);
    snprintf(goodbin, sizeof(goodbin), "%s/nctool", gooddir);
    snprintf(evilbin, sizeof(evilbin), "%s/nctool", evildir);

    f = fopen(goodbin, "w");
    ASSERT_TRUE(f != NULL);
    if (f) {
        fputs("#!/bin/sh\necho GOOD\necho MARK=$NEVERC_MARK\n", f);
        fclose(f);
    }
    chmod(goodbin, 0755);
    f = fopen(evilbin, "w");
    ASSERT_TRUE(f != NULL);
    if (f) {
        fputs("#!/bin/sh\necho EVIL\n", f);
        fclose(f);
    }
    chmod(evilbin, 0755);

    snprintf(evil_env, sizeof(evil_env), "PATH=%s", evildir);
    snprintf(good_env, sizeof(good_env), "PATH=%s", gooddir);
    env[0] = evil_env;
    env[1] = good_env;
    env[2] = "NEVERC_MARK=evil";
    env[3] = "NEVERC_MARK=good";
    cmd = neverc_exec_command("nctool", NULL, 0);
    ASSERT_TRUE(cmd != NULL);
    neverc_exec_cmd_set_env(cmd, env, 4);
    ASSERT_INT_EQ(neverc_exec_cmd_output(cmd, &out, &st), 0);
    ASSERT_INT_EQ(st.exit_code, 0);
    ASSERT_TRUE(memmem(out.data, out.len, "GOOD", 4) != NULL);
    ASSERT_TRUE(memmem(out.data, out.len, "EVIL", 4) == NULL);
    ASSERT_TRUE(memmem(out.data, out.len, "MARK=good", 9) != NULL);
    ASSERT_TRUE(memmem(out.data, out.len, "MARK=evil", 9) == NULL);

    neverc_exec_output_free(&out);
    neverc_exec_cmd_free(cmd);
    unlink(goodbin);
    unlink(evilbin);
    rmdir(gooddir);
    rmdir(evildir);
}

static void test_look_path_overflow_does_not_skip(void) {
    printf("[look_path_overflow]\n");
    char longdir[256], shortdir[256], longbin[420], shortbin[300];
    char pathbuf[700], tiny[64], big[512];
    const char *old = getenv("PATH");
    char *oldcopy = old ? strdup(old) : NULL;
    FILE *f;
    const char *p;
    int pid = (int)getpid();
    size_t n;

    snprintf(shortdir, sizeof(shortdir), "/tmp/nclps_%d", pid);
    snprintf(longdir, sizeof(longdir), "/tmp/nclpl_%d_", pid);
    n = strlen(longdir);
    memset(longdir + n, 'a', 160);
    longdir[n + 160] = '\0';
    mkdir(longdir, 0700);
    mkdir(shortdir, 0700);
    snprintf(longbin, sizeof(longbin), "%s/nclook", longdir);
    snprintf(shortbin, sizeof(shortbin), "%s/nclook", shortdir);

    f = fopen(longbin, "w");
    ASSERT_TRUE(f != NULL);
    if (f) {
        fputs("#!/bin/sh\necho LONG\n", f);
        fclose(f);
    }
    chmod(longbin, 0755);
    f = fopen(shortbin, "w");
    ASSERT_TRUE(f != NULL);
    if (f) {
        fputs("#!/bin/sh\necho SHORT\n", f);
        fclose(f);
    }
    chmod(shortbin, 0755);

    snprintf(pathbuf, sizeof(pathbuf), "%s:%s", longdir, shortdir);
    ASSERT_INT_EQ(setenv("PATH", pathbuf, 1), 0);

    p = neverc_exec_look_path("nclook", tiny, sizeof(tiny));
    ASSERT_TRUE(p == NULL);

    p = neverc_exec_look_path("nclook", big, sizeof(big));
    ASSERT_TRUE(p != NULL);
    ASSERT_TRUE(p && strstr(p, longdir) != NULL);

    unlink(longbin);
    p = neverc_exec_look_path("nclook", tiny, sizeof(tiny));
    ASSERT_TRUE(p != NULL);
    ASSERT_TRUE(p && strstr(p, shortdir) != NULL);

    if (oldcopy) {
        setenv("PATH", oldcopy, 1);
        free(oldcopy);
    } else {
        unsetenv("PATH");
    }
    unlink(shortbin);
    rmdir(longdir);
    rmdir(shortdir);
}
#endif

static void test_argv_quoting(const char *executable) {
    printf("[argv_quoting]\n");
    const char *args[] = {"--print-argv", "foo\"bar", "a b", "$(whoami)", ";id", "a|b"};
    neverc_exec_cmd_t *cmd = neverc_exec_command(executable, args, 6);
    ASSERT_TRUE(cmd != NULL);
    if (!cmd) return;

    neverc_exec_output_t out = {0};
    neverc_exec_exit_status_t st = {0};
    ASSERT_INT_EQ(neverc_exec_cmd_output(cmd, &out, &st), 0);
    ASSERT_INT_EQ(st.exit_code, 0);
    ASSERT_TRUE(memmem(out.data, out.len, "foo\"bar", 7) != NULL);
    ASSERT_TRUE(memmem(out.data, out.len, "a b", 3) != NULL);
    ASSERT_TRUE(memmem(out.data, out.len, "$(whoami)", 9) != NULL);
    ASSERT_TRUE(memmem(out.data, out.len, ";id", 3) != NULL);
    ASSERT_TRUE(memmem(out.data, out.len, "a|b", 3) != NULL);

    neverc_exec_output_free(&out);
    neverc_exec_cmd_free(cmd);
}

static int run_pause_child(void) {
#if defined(_WIN32)
    Sleep(INFINITE);
#else
    pause();
#endif
    return 0;
}

#if !defined(_WIN32)
static int run_check_extra_fd(int fd) {
    int open_now = fcntl(fd, F_GETFD) >= 0;
    return printf("%d\n", open_now) < 0 ? 1 : 0;
}
#endif

static void test_missing_executable(void) {
    printf("[missing_executable]\n");
    neverc_exec_cmd_t *cmd =
        neverc_exec_command("neverc_no_such_cmd_xyz_12345", NULL, 0);
    ASSERT_TRUE(cmd != NULL);
    neverc_exec_exit_status_t st = {0};
    ASSERT_INT_EQ(neverc_exec_cmd_run(cmd, &st), -1);
    ASSERT_INT_EQ(neverc_exec_cmd_start(cmd), -1);
    neverc_exec_cmd_free(cmd);
}

static void test_empty_env_pathless_name_fails(void) {
    printf("[empty_env_pathless]\n");
#if defined(_WIN32)
    const char *args[] = {"/C", "exit /B 0"};
    neverc_exec_cmd_t *cmd = neverc_exec_command("cmd.exe", args, 2);
#else
    neverc_exec_cmd_t *cmd = neverc_exec_command("true", NULL, 0);
#endif
    ASSERT_TRUE(cmd != NULL);
    neverc_exec_cmd_set_env(cmd, NULL, 0);
    neverc_exec_exit_status_t st = {0};
    ASSERT_INT_EQ(neverc_exec_cmd_run(cmd, &st), -1);
    neverc_exec_cmd_free(cmd);

    char buf[4096];
#if defined(_WIN32)
    const char *p = neverc_exec_look_path("cmd.exe", buf, sizeof(buf));
#else
    const char *p = neverc_exec_look_path("true", buf, sizeof(buf));
#endif
    ASSERT_TRUE(p != NULL);
    if (!p) return;
#if defined(_WIN32)
    cmd = neverc_exec_command(p, args, 2);
#else
    cmd = neverc_exec_command(p, NULL, 0);
#endif
    ASSERT_TRUE(cmd != NULL);
    neverc_exec_cmd_set_env(cmd, NULL, 0);
    ASSERT_INT_EQ(neverc_exec_cmd_run(cmd, &st), 0);
    ASSERT_INT_EQ(st.exit_code, 0);
    neverc_exec_cmd_free(cmd);
}

static void test_invalid_env_rejected(void) {
    printf("[invalid_env]\n");
#if defined(_WIN32)
    const char *args[] = {"/C", "exit /B 0"};
    neverc_exec_cmd_t *cmd = neverc_exec_command("cmd.exe", args, 2);
#else
    neverc_exec_cmd_t *cmd = neverc_exec_command("true", NULL, 0);
#endif
    ASSERT_TRUE(cmd != NULL);
    const char *bad[] = {"NOT_AN_ASSIGNMENT"};
    neverc_exec_cmd_set_env(cmd, bad, 1);
    neverc_exec_exit_status_t st = {0};
    ASSERT_INT_EQ(neverc_exec_cmd_run(cmd, &st), -1);
    neverc_exec_cmd_free(cmd);

#if defined(_WIN32)
    cmd = neverc_exec_command("cmd.exe", args, 2);
    ASSERT_TRUE(cmd != NULL);
    {
        char drive_cwd[64];
        snprintf(drive_cwd, sizeof(drive_cwd), "=C:=C:\\Windows");
        const char *win_env[] = {drive_cwd, "PATH=C:\\Windows\\System32"};
        neverc_exec_cmd_set_env(cmd, win_env, 2);
        st.exit_code = -1;
        ASSERT_INT_EQ(neverc_exec_cmd_run(cmd, &st), 0);
        ASSERT_INT_EQ(st.exit_code, 0);
    }
    neverc_exec_cmd_free(cmd);

    {
        const char *slash_args[] = {"/C", "exit /B 0"};
        cmd = neverc_exec_command("C:System32\\cmd.exe", slash_args, 2);
        ASSERT_TRUE(cmd != NULL);
        neverc_exec_cmd_set_dir(cmd, "C:\\Windows");
        st.exit_code = -1;
        ASSERT_INT_EQ(neverc_exec_cmd_run(cmd, &st), 0);
        ASSERT_INT_EQ(st.exit_code, 0);
        neverc_exec_cmd_free(cmd);
    }

    /* Go joinExeDirAndFName: a relative Dir's first letter is not a volume.
     * Dir=neverc_nvol_dir + N:payload.exe must not become dir\payload.exe. */
    {
        char absdir[MAX_PATH], dir[MAX_PATH], payload[MAX_PATH];
        char sysdir[MAX_PATH], src[MAX_PATH];
        UINT n;
        const char *hijack_args[] = {"N:payload.exe", NULL};
        GetCurrentDirectoryA((DWORD)sizeof(absdir), absdir);
        snprintf(dir, sizeof(dir), "%s\\neverc_nvol_dir", absdir);
        snprintf(payload, sizeof(payload), "%s\\payload.exe", dir);
        n = GetSystemDirectoryA(sysdir, (UINT)sizeof(sysdir));
        ASSERT_TRUE(n > 0 && n < sizeof(sysdir));
        snprintf(src, sizeof(src), "%s\\cmd.exe", sysdir);
        CreateDirectoryA(dir, NULL);
        ASSERT_TRUE(CopyFileA(src, payload, FALSE));
        cmd = neverc_exec_command("N:payload.exe", hijack_args, 1);
        ASSERT_TRUE(cmd != NULL);
        neverc_exec_cmd_set_dir(cmd, "neverc_nvol_dir");
        st.exit_code = -1;
        ASSERT_INT_EQ(neverc_exec_cmd_run(cmd, &st), -1);
        neverc_exec_cmd_free(cmd);
        DeleteFileA(payload);
        RemoveDirectoryA(dir);
    }
#endif
}

static void test_batch_args_rejected(void) {
    printf("[batch_args_rejected]\n");
    char script[1200];
#if defined(_WIN32)
    char tmp[MAX_PATH];
    DWORD n = GetTempPathA((DWORD)sizeof(tmp), tmp);
    ASSERT_TRUE(n > 0 && n < sizeof(tmp));
    snprintf(script, sizeof(script), "%sneverc_probe_%lu.bat", tmp,
             (unsigned long)GetCurrentProcessId());
#else
    char cwd[1024];
    ASSERT_TRUE(getcwd(cwd, sizeof(cwd)) != NULL);
    snprintf(script, sizeof(script), "%s/neverc_probe_%d.bat", cwd, (int)getpid());
#endif
    FILE *sf = fopen(script, "w");
    ASSERT_TRUE(sf != NULL);
    if (sf) {
#if defined(_WIN32)
        fputs("@echo off\r\nexit /B 0\r\n", sf);
#else
        fputs("#!/bin/sh\nexit 0\n", sf);
#endif
        fclose(sf);
    }
#if !defined(_WIN32)
    chmod(script, 0755);
#endif
    const char *safe[] = {"ok"};
    neverc_exec_cmd_t *cmd = neverc_exec_command(script, safe, 1);
    neverc_exec_exit_status_t st = {0};
    ASSERT_INT_EQ(neverc_exec_cmd_run(cmd, &st), 0);
    ASSERT_INT_EQ(st.exit_code, 0);
    neverc_exec_cmd_free(cmd);

    const char *unsafe[] = {"&calc"};
    cmd = neverc_exec_command(script, unsafe, 1);
    ASSERT_INT_EQ(neverc_exec_cmd_run(cmd, &st), -1);
    neverc_exec_cmd_free(cmd);

    /* Windows treats trailing spaces/dots as part of the extension, so
     * "probe.bat." / "probe.cmd " must still be rejected. */
    cmd = neverc_exec_command("probe.bat.", unsafe, 1);
    ASSERT_INT_EQ(neverc_exec_cmd_run(cmd, &st), -1);
    neverc_exec_cmd_free(cmd);
    cmd = neverc_exec_command("probe.cmd ", unsafe, 1);
    ASSERT_INT_EQ(neverc_exec_cmd_run(cmd, &st), -1);
    neverc_exec_cmd_free(cmd);

    /* NTFS ADS suffixes must still classify as batch (BatBadBut). */
    {
        char ads[1200];
#if defined(_WIN32)
        snprintf(ads, sizeof(ads), "%s::$DATA", script);
#else
        snprintf(ads, sizeof(ads), "%s::$DATA", script);
        sf = fopen(ads, "w");
        ASSERT_TRUE(sf != NULL);
        if (sf) {
            fputs("#!/bin/sh\nexit 0\n", sf);
            fclose(sf);
        }
        chmod(ads, 0755);
#endif
        cmd = neverc_exec_command(ads, unsafe, 1);
        ASSERT_INT_EQ(neverc_exec_cmd_run(cmd, &st), -1);
        neverc_exec_cmd_free(cmd);
        cmd = neverc_exec_command("probe.bat:stream", unsafe, 1);
        ASSERT_INT_EQ(neverc_exec_cmd_run(cmd, &st), -1);
        neverc_exec_cmd_free(cmd);
        cmd = neverc_exec_command("C:payload.bat", unsafe, 1);
        ASSERT_INT_EQ(neverc_exec_cmd_run(cmd, &st), -1);
        neverc_exec_cmd_free(cmd);
        cmd = neverc_exec_command("c:payload.cmd:stream", unsafe, 1);
        ASSERT_INT_EQ(neverc_exec_cmd_run(cmd, &st), -1);
        neverc_exec_cmd_free(cmd);
        cmd = neverc_exec_command(".bat", unsafe, 1);
        ASSERT_INT_EQ(neverc_exec_cmd_run(cmd, &st), -1);
        neverc_exec_cmd_free(cmd);
        cmd = neverc_exec_command(".cmd", unsafe, 1);
        ASSERT_INT_EQ(neverc_exec_cmd_run(cmd, &st), -1);
        neverc_exec_cmd_free(cmd);
        cmd = neverc_exec_command("/tmp/.bat", unsafe, 1);
        ASSERT_INT_EQ(neverc_exec_cmd_run(cmd, &st), -1);
        neverc_exec_cmd_free(cmd);
#if !defined(_WIN32)
        unlink(ads);
#endif
    }

    /* Names longer than the old 15-byte prefix copy must still be
     * classified as batch (BatBadBut). */
    {
        char long_script[1200];
#if defined(_WIN32)
        snprintf(long_script, sizeof(long_script),
                 "%sneverc_long_batch_name.bat", tmp);
#else
        snprintf(long_script, sizeof(long_script),
                 "%s/neverc_long_batch_name.bat", cwd);
#endif
        sf = fopen(long_script, "w");
        ASSERT_TRUE(sf != NULL);
        if (sf) {
#if defined(_WIN32)
            fputs("@echo off\r\nexit /B 0\r\n", sf);
#else
            fputs("#!/bin/sh\nexit 0\n", sf);
#endif
            fclose(sf);
        }
#if !defined(_WIN32)
        chmod(long_script, 0755);
#endif
        cmd = neverc_exec_command(long_script, safe, 1);
        ASSERT_INT_EQ(neverc_exec_cmd_run(cmd, &st), 0);
        neverc_exec_cmd_free(cmd);
        cmd = neverc_exec_command(long_script, unsafe, 1);
        ASSERT_INT_EQ(neverc_exec_cmd_run(cmd, &st), -1);
        neverc_exec_cmd_free(cmd);
        cmd = neverc_exec_command("neverc_long_batch_name.bat", unsafe, 1);
        ASSERT_INT_EQ(neverc_exec_cmd_run(cmd, &st), -1);
        neverc_exec_cmd_free(cmd);
#if defined(_WIN32)
        DeleteFileA(long_script);
#else
        unlink(long_script);
#endif
    }
#if defined(_WIN32)
    DeleteFileA(script);
#else
    unlink(script);
#endif
}

static void test_start_kill_wait(const char *executable) {
    printf("[start_kill_wait]\n");
    ASSERT_INT_EQ(neverc_exec_cmd_kill(NULL, 15), -1);
    ASSERT_INT_EQ(neverc_exec_cmd_wait(NULL, NULL), -1);
    ASSERT_INT_EQ(neverc_exec_cmd_pid(NULL), -1);

#if defined(_WIN32)
    const char *true_args[] = {"/C", "exit /B 0"};
    neverc_exec_cmd_t *idle = neverc_exec_command("cmd.exe", true_args, 2);
#else
    neverc_exec_cmd_t *idle = neverc_exec_command("true", NULL, 0);
#endif
    ASSERT_TRUE(idle != NULL);
    ASSERT_INT_EQ(neverc_exec_cmd_kill(idle, 15), -1);
    ASSERT_INT_EQ(neverc_exec_cmd_wait(idle, NULL), -1);
    ASSERT_INT_EQ(neverc_exec_cmd_pid(idle), -1);
    neverc_exec_cmd_free(idle);

#if !defined(_WIN32)
    void (*old_term)(int) = signal(SIGTERM, SIG_IGN);
#endif
    const char *args[] = {"--pause"};
    neverc_exec_cmd_t *cmd = neverc_exec_command(executable, args, 1);
    ASSERT_TRUE(cmd != NULL);
    ASSERT_INT_EQ(neverc_exec_cmd_start(cmd), 0);
    ASSERT_TRUE(neverc_exec_cmd_pid(cmd) > 0);
    ASSERT_INT_EQ(neverc_exec_cmd_start(cmd), -1);
#if defined(_WIN32)
    ASSERT_INT_EQ(neverc_exec_cmd_kill(cmd, 15), 0);
#else
    ASSERT_INT_EQ(neverc_exec_cmd_kill(cmd, SIGTERM), 0);
#endif
    neverc_exec_exit_status_t st = {0};
    ASSERT_INT_EQ(neverc_exec_cmd_wait(cmd, &st), 0);
#if defined(_WIN32)
    ASSERT_TRUE(st.exit_code != 0 || st.signaled);
#else
    ASSERT_INT_EQ(st.signaled, 1);
    ASSERT_INT_EQ(st.signal_num, SIGTERM);
#endif
    ASSERT_INT_EQ(neverc_exec_cmd_pid(cmd), -1);
    neverc_exec_cmd_free(cmd);
#if !defined(_WIN32)
    signal(SIGTERM, old_term);
#endif
}

#if !defined(_WIN32)
static void test_child_cloexec(const char *executable) {
    printf("[child_cloexec]\n");
    int fds[2];
    ASSERT_INT_EQ(pipe(fds), 0);
    char fdstr[16];
    snprintf(fdstr, sizeof(fdstr), "%d", fds[0]);
    const char *args[] = {"--check-extra-fd", fdstr};
    neverc_exec_cmd_t *cmd = neverc_exec_command(executable, args, 2);
    ASSERT_TRUE(cmd != NULL);
    neverc_exec_output_t out = {0};
    neverc_exec_exit_status_t st = {0};
    ASSERT_INT_EQ(neverc_exec_cmd_output(cmd, &out, &st), 0);
    ASSERT_INT_EQ(st.exit_code, 0);
    ASSERT_TRUE(out.len >= 1 && out.data[0] == '0');
    neverc_exec_output_free(&out);
    neverc_exec_cmd_free(cmd);
    close(fds[0]);
    close(fds[1]);
}
#endif

int main(int argc, char **argv) {
    if (argc == 2 &&
        strcmp(argv[1], "--bidirectional-child") == 0)
        return run_bidirectional_child();
    if (argc == 2 && strcmp(argv[1], "--pause") == 0)
        return run_pause_child();
    if (argc == 2 && strcmp(argv[1], "--stdin-eof-child") == 0)
        return run_stdin_eof_child();
#if !defined(_WIN32)
    if (argc == 2 && strcmp(argv[1], "--print-blocked-usr1") == 0)
        return run_print_blocked_usr1();
    if (argc == 3 && strcmp(argv[1], "--check-extra-fd") == 0)
        return run_check_extra_fd(atoi(argv[2]));
#endif
    if (argc >= 2 && strcmp(argv[1], "--print-argv") == 0) {
        int i;
        for (i = 2; i < argc; i++) {
            if (printf("%s\n", argv[i]) < 0)
                return 1;
        }
        return 0;
    }

    printf("=== NeverC os/exec Tests ===\n");
    ASSERT_TRUE(neverc_exec_command(NULL, NULL, 0) == NULL);
    ASSERT_TRUE(neverc_exec_command("echo", NULL, -1) == NULL);
    ASSERT_INT_EQ(neverc_exec_cmd_run(NULL, NULL), -1);
    test_command_echo();
    test_command_false();
    test_command_true();
#if !defined(_WIN32)
    test_command_stdin();
#endif
    test_bidirectional_pipes(argv[0]);
    test_empty_stdin_is_eof(argv[0]);
    test_argv_quoting(argv[0]);
    test_look_path();
    test_combined_output();
    test_missing_executable();
    test_empty_env_pathless_name_fails();
    test_invalid_env_rejected();
    test_batch_args_rejected();
    test_start_kill_wait(argv[0]);
#if !defined(_WIN32)
    test_child_cloexec(argv[0]);
    test_unread_stdin_is_not_failure();
    test_signaled_status();
    test_exec_resets_signal_mask(argv[0]);
    test_env_still_searches_path();
    test_env_without_path_does_not_use_parent_path();
    test_set_dir();
    test_set_dir_does_not_research_path();
    test_env_path_last_wins();
    test_look_path_overflow_does_not_skip();
#endif
    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
