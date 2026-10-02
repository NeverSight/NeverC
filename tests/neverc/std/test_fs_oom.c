#include <stdio.h>
#include <stdlib.h>

static size_t allocation_count;
static size_t fail_at;

static int allocation_fails(void) {
    allocation_count++;
    return fail_at != 0 && allocation_count == fail_at;
}

static void *controlled_malloc(size_t size) {
    return allocation_fails() ? NULL : malloc(size);
}

static void *controlled_realloc(void *ptr, size_t size) {
    return allocation_fails() ? NULL : realloc(ptr, size);
}

#include "../../../std/src/unicode/utf8/utf8.c"
#include "../../../std/src/path/match.c"
#define malloc controlled_malloc
#define realloc controlled_realloc
#include "../../../std/src/io/fs/fs.c"
#undef malloc
#undef realloc

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

static void temp_file_path(char *buf, size_t cap, const char *name) {
#if defined(_WIN32)
    char dir[1024];
    DWORD n = GetTempPathA((DWORD)sizeof(dir), dir);
    if (n == 0 || n >= sizeof(dir))
        snprintf(dir, sizeof(dir), ".\\");
    snprintf(buf, cap, "%s%s", dir, name);
#else
    const char *dir = getenv("TMPDIR");
    if (!dir || !*dir) dir = "/tmp";
    snprintf(buf, cap, "%s/%s", dir, name);
#endif
}

int main(void) {
    reset_allocator(0);
    neverc_fs_dir_entry_t *entries = NULL;
    size_t count = 0;
    CHECK(neverc_fs_read_dir(".", &entries, &count) == 0);
    size_t dir_allocations = allocation_count;
    neverc_fs_free_entries(entries);

    for (size_t failure = 1; failure <= dir_allocations; failure++) {
        reset_allocator(failure);
        entries = (neverc_fs_dir_entry_t *)1;
        count = 99;
        CHECK(neverc_fs_read_dir(".", &entries, &count) == -1);
        CHECK(entries == NULL);
        CHECK(count == 0);
    }

    reset_allocator(0);
    char **matches = NULL;
    count = 0;
    CHECK(neverc_fs_glob(".", "*", &matches, &count) == 0);
    size_t glob_allocations = allocation_count;
    neverc_fs_free_matches(matches, count);

    for (size_t failure = 1; failure <= glob_allocations; failure++) {
        reset_allocator(failure);
        matches = (char **)1;
        count = 99;
        CHECK(neverc_fs_glob(".", "*", &matches, &count) == -1);
        CHECK(matches == NULL);
        CHECK(count == 0);
    }

    /* The size hint of a regular file is exact, so reading it must take one
     * size+1 allocation: EOF has to be found without doubling the buffer. */
    {
        enum { READ_FILE_SIZE = 4096 };
        char path[2048];
        temp_file_path(path, sizeof(path), "neverc_fs_oom_read.tmp");
        FILE *f = fopen(path, "wb");
        CHECK(f != NULL);
        for (int i = 0; i < READ_FILE_SIZE; i++)
            CHECK(fputc('a' + i % 26, f) != EOF);
        CHECK(fclose(f) == 0);

        uint8_t *data = NULL;
        size_t size = 0;
        reset_allocator(0);
        int rc = neverc_fs_read_file(path, &data, &size);
        size_t read_allocations = allocation_count;
        int content_ok = rc == 0 && size == READ_FILE_SIZE && data &&
                         data[0] == 'a' &&
                         data[READ_FILE_SIZE - 1] ==
                             'a' + (READ_FILE_SIZE - 1) % 26 &&
                         data[READ_FILE_SIZE] == 0;
        free(data);

        int failures_clear = 1;
        for (size_t failure = 1; failure <= read_allocations; failure++) {
            reset_allocator(failure);
            data = (uint8_t *)1;
            size = 99;
            rc = neverc_fs_read_file(path, &data, &size);
            if (rc != -1 || data != NULL || size != 0)
                failures_clear = 0;
        }
        remove(path);
        CHECK(content_ok);
        CHECK(failures_clear);
        CHECK(read_allocations == 1);
    }
    puts("passed");
    return 0;
}
