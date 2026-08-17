#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t allocation_count;
static size_t fail_at;

static int allocation_fails(void) {
    allocation_count++;
    return fail_at != 0 && allocation_count == fail_at;
}

static void *controlled_malloc(size_t size) {
    return allocation_fails() ? NULL : malloc(size);
}

static void *controlled_calloc(size_t count, size_t size) {
    return allocation_fails() ? NULL : calloc(count, size);
}

static void *controlled_realloc(void *ptr, size_t size) {
    return allocation_fails() ? NULL : realloc(ptr, size);
}

static char *controlled_strdup(const char *s) {
    if (allocation_fails()) return NULL;
    size_t length = strlen(s);
    char *copy = (char *)malloc(length + 1);
    if (copy) memcpy(copy, s, length + 1);
    return copy;
}

#define malloc controlled_malloc
#define calloc controlled_calloc
#define realloc controlled_realloc
#define strdup controlled_strdup
#include "../../../std/src/os/exec/exec.c"
#undef malloc
#undef calloc
#undef realloc
#undef strdup

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",              \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static void reset_allocator(size_t failure) {
    allocation_count = 0;
    fail_at = failure;
}

int main(void) {
    reset_allocator(0);
    void *probe = controlled_malloc(1);
    CHECK(probe != NULL);
    free(probe);

    const char *arguments[] = {"one", "two"};
    reset_allocator(0);
    neverc_exec_cmd_t *cmd = neverc_exec_command("echo", arguments, 2);
    CHECK(cmd != NULL);
    size_t command_allocations = allocation_count;
    neverc_exec_cmd_free(cmd);
    for (size_t failure = 1; failure <= command_allocations; failure++) {
        reset_allocator(failure);
        cmd = neverc_exec_command("echo", arguments, 2);
        CHECK(cmd == NULL);
    }

    reset_allocator(0);
    cmd = neverc_exec_command("echo", NULL, 0);
    CHECK(cmd != NULL);
    neverc_exec_cmd_set_dir(cmd, "/tmp");
    CHECK(cmd->dir != NULL && strcmp(cmd->dir, "/tmp") == 0);
    reset_allocator(1);
    neverc_exec_cmd_set_dir(cmd, "/new-dir");
    CHECK(cmd->dir != NULL && strcmp(cmd->dir, "/tmp") == 0);
    CHECK(neverc_exec_cmd_run(cmd, NULL) == -1);
    neverc_exec_cmd_free(cmd);

    reset_allocator(0);
    cmd = neverc_exec_command("echo", NULL, 0);
    CHECK(cmd != NULL);
    reset_allocator(1);
    neverc_exec_cmd_set_dir(cmd, "/tmp");
    CHECK(cmd->dir == NULL);
    CHECK(neverc_exec_cmd_run(cmd, NULL) == -1);
    neverc_exec_cmd_free(cmd);

    const char *old_environment[] = {"OLD=1"};
    const char *new_environment[] = {"NEW=1", "SECOND=2"};
    for (size_t failure = 1; failure <= 3; failure++) {
        reset_allocator(0);
        cmd = neverc_exec_command("echo", NULL, 0);
        CHECK(cmd != NULL);
        neverc_exec_cmd_set_env(cmd, old_environment, 1);
        CHECK(cmd->env != NULL && strcmp(cmd->env[0], "OLD=1") == 0);
        reset_allocator(failure);
        neverc_exec_cmd_set_env(cmd, new_environment, 2);
        CHECK(cmd->env != NULL && cmd->env_count == 1);
        CHECK(strcmp(cmd->env[0], "OLD=1") == 0);
        neverc_exec_cmd_free(cmd);
    }

    const char *invalid_environment[] = {"NOEQUALS"};
    reset_allocator(0);
    cmd = neverc_exec_command("echo", NULL, 0);
    CHECK(cmd != NULL);
    neverc_exec_cmd_set_env(cmd, invalid_environment, 1);
    CHECK(cmd->env == NULL);
    neverc_exec_cmd_free(cmd);

    const char *empty_name[] = {"=VALUE"};
    reset_allocator(0);
    cmd = neverc_exec_command("echo", NULL, 0);
    CHECK(cmd != NULL);
    neverc_exec_cmd_set_env(cmd, empty_name, 1);
    CHECK(cmd->env == NULL);
    neverc_exec_cmd_free(cmd);

#if !defined(_WIN32)
    const char *large_output_args[] = {
        "-c", "dd if=/dev/zero bs=8192 count=1 2>/dev/null"
    };
    reset_allocator(0);
    cmd = neverc_exec_command("/bin/sh", large_output_args, 2);
    CHECK(cmd != NULL);
    for (size_t failure = 1; failure <= 2; failure++) {
        neverc_exec_output_t output = {0};
        reset_allocator(failure);
        CHECK(neverc_exec_cmd_output(cmd, &output, NULL) == -1);
        CHECK(output.data == NULL && output.len == 0 && output.cap == 0);
    }
    neverc_exec_cmd_free(cmd);
#endif

    puts("passed");
    return 0;
}
