#ifndef _WIN32
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

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

#define malloc controlled_malloc
#define realloc controlled_realloc
#include "../../../std/src/fmt/fmt.c"
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

static FILE *open_fmt_oom_input_pipe(const unsigned char *data, size_t len) {
    int fds[2];
    size_t offset = 0;
    FILE *input;
    if (!data || len > 512U) return NULL;
#ifdef _WIN32
    if (_pipe(fds, 512, _O_BINARY) != 0) return NULL;
    while (offset < len) {
        int written = _write(fds[1], data + offset,
                             (unsigned int)(len - offset));
        if (written <= 0) {
            _close(fds[0]);
            _close(fds[1]);
            return NULL;
        }
        offset += (size_t)written;
    }
    _close(fds[1]);
    input = _fdopen(fds[0], "rb");
    if (!input) _close(fds[0]);
#else
    if (pipe(fds) != 0) return NULL;
    while (offset < len) {
        ssize_t written = write(fds[1], data + offset, len - offset);
        if (written <= 0) {
            close(fds[0]);
            close(fds[1]);
            return NULL;
        }
        offset += (size_t)written;
    }
    close(fds[1]);
    input = fdopen(fds[0], "rb");
    if (!input) close(fds[0]);
#endif
    return input;
}

int main(void) {
    reset_allocator(1);
    CHECK(neverc_fmt_sprintf("value") == NULL);

    char literal[300];
    memset(literal, 'x', sizeof(literal) - 1);
    literal[sizeof(literal) - 1] = '\0';
    reset_allocator(2);
    CHECK(neverc_fmt_sprintf(literal) == NULL);

    reset_allocator(2);
    CHECK(neverc_fmt_sprintf("%300s", "x") == NULL);

    {
        char wide_input[301];
        int value = 77;
        wide_input[0] = '1';
        memset(wide_input + 1, 'x', sizeof(wide_input) - 2U);
        wide_input[sizeof(wide_input) - 1U] = '\0';
        reset_allocator(1);
        CHECK(neverc_fmt_sscanf(wide_input, "%300d", &value) == 0);
        CHECK(allocation_count == 1);
        CHECK(value == 77);
    }

    {
        unsigned char float_input[163];
        FILE *input;
        double value = 7.0;
        int suffix = 0;
        memset(float_input, '0', 160);
        float_input[160] = '1';
        float_input[161] = ' ';
        float_input[162] = '9';
        input = open_fmt_oom_input_pipe(float_input, sizeof(float_input));
        CHECK(input != NULL);
        CHECK(ftell(input) < 0);
        reset_allocator(1);
        CHECK(neverc_fmt_fscanf(input, "%f", &value) == 0);
        CHECK(allocation_count == 1);
        CHECK(value == 7.0);
        CHECK(neverc_fmt_fscanf(input, "%d", &suffix) == 1);
        CHECK(suffix == 9);
        CHECK(fclose(input) == 0);
    }
    puts("passed");
    return 0;
}
