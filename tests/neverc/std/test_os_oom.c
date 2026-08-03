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
    size_t len = strlen(s);
    char *copy = (char *)malloc(len + 1);
    if (copy) memcpy(copy, s, len + 1);
    return copy;
}

#define malloc controlled_malloc
#define calloc controlled_calloc
#define realloc controlled_realloc
#define strdup controlled_strdup
#include "../../../std/src/os/os.c"
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
    char long_value[601];
    memset(long_value, 'x', sizeof(long_value) - 1);
    long_value[sizeof(long_value) - 1] = '\0';
    CHECK(neverc_os_setenv("NEVERC_OS_OOM_VALUE", long_value) == 0);
    reset_allocator(0);
    char *expanded = neverc_os_expand_env("$NEVERC_OS_OOM_VALUE");
    CHECK(expanded != NULL);
    CHECK(strlen(expanded) == sizeof(long_value) - 1);
    size_t expand_allocations = allocation_count;
    free(expanded);
    for (size_t failure = 1; failure <= expand_allocations; failure++) {
        reset_allocator(failure);
        CHECK(neverc_os_expand_env("$NEVERC_OS_OOM_VALUE") == NULL);
    }
    neverc_os_unsetenv("NEVERC_OS_OOM_VALUE");

    reset_allocator(0);
    int env_count = 0;
    char **environment = neverc_os_environ(&env_count);
    CHECK(environment != NULL);
    size_t environ_allocations = allocation_count;
    for (int i = 0; i < env_count; i++) free(environment[i]);
    free(environment);

    for (size_t failure = 1; failure <= environ_allocations; failure++) {
        reset_allocator(failure);
        env_count = 99;
        environment = neverc_os_environ(&env_count);
        CHECK(environment == NULL);
        CHECK(env_count == 0);
    }

    reset_allocator(0);
    neverc_os_dir_entry_t *entries = NULL;
    size_t count = 0;
    CHECK(neverc_os_read_dir(".", &entries, &count) == 0);
    size_t dir_allocations = allocation_count;
    free(entries);
    for (size_t failure = 1; failure <= dir_allocations; failure++) {
        reset_allocator(failure);
        entries = (neverc_os_dir_entry_t *)1;
        count = 99;
        CHECK(neverc_os_read_dir(".", &entries, &count) == -1);
        CHECK(entries == NULL);
        CHECK(count == 0);
    }
    puts("passed");
    return 0;
}
