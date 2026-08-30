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

static size_t position_restore_count;
static size_t position_save_count;
static size_t fseek_count;
static size_t fsetpos_count;
static size_t fail_position_save_at;
static size_t fail_position_restore_at;

static int position_restore_fails(void) {
    position_restore_count++;
    return fail_position_restore_at != 0 &&
           position_restore_count == fail_position_restore_at;
}

static int controlled_fseek(FILE *stream, long offset, int origin) {
    fseek_count++;
    return position_restore_fails() ? -1 : fseek(stream, offset, origin);
}

static int controlled_fgetpos(FILE *stream, fpos_t *position) {
    position_save_count++;
    if (fail_position_save_at != 0 &&
        position_save_count == fail_position_save_at)
        return -1;
    return fgetpos(stream, position);
}

static int controlled_fsetpos(FILE *stream, const fpos_t *position) {
    fsetpos_count++;
    return position_restore_fails() ? -1 : fsetpos(stream, position);
}

#define malloc controlled_malloc
#define realloc controlled_realloc
#define fseek controlled_fseek
#define fgetpos controlled_fgetpos
#define fsetpos controlled_fsetpos
#include "../../../std/src/fmt/fmt.c"
#undef malloc
#undef realloc
#undef fseek
#undef fgetpos
#undef fsetpos

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

static void reset_position_restore(size_t failure) {
    position_restore_count = 0;
    fseek_count = 0;
    fsetpos_count = 0;
    fail_position_restore_at = failure;
}

static void reset_position_save(size_t failure) {
    position_save_count = 0;
    fail_position_save_at = failure;
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
        reset_allocator(1);
        CHECK(neverc_fmt_fscanf(input, "%f", &value) == 0);
        CHECK(allocation_count == 1);
        CHECK(value == 7.0);
        CHECK(neverc_fmt_fscanf(input, "%d", &suffix) == 1);
        CHECK(suffix == 9);
        CHECK(fclose(input) == 0);
    }

    {
        static const unsigned char bytes[] = {'A', 0xC2, 0xA0, 'Z'};
        struct {
            char text[3];
            unsigned char canary;
        } output = {{'Q', 'Q', '\0'}, 0xA5};
        FILE *input = tmpfile();
        CHECK(input != NULL);
        CHECK(fwrite(bytes, 1, sizeof(bytes), input) == sizeof(bytes));
        rewind(input);
        reset_allocator(0);
        reset_position_save(1);
        reset_position_restore(0);
        CHECK(neverc_fmt_fscanf(input, "%2s", output.text) == 0);
        CHECK(position_save_count == 1);
        CHECK(fseek_count == 0);
        CHECK(fsetpos_count == 0);
        CHECK(output.text[0] == 'Q');
        CHECK(output.text[1] == 'Q');
        CHECK(output.canary == 0xA5);
        CHECK(fclose(input) == 0);
    }

    {
        static const unsigned char bytes[] = {'A', 0xC2, 0xA0, 'Z'};
        struct {
            char text[3];
            unsigned char canary;
        } output = {{'Q', 'Q', '\0'}, 0xA5};
        FILE *input = tmpfile();
        CHECK(input != NULL);
        CHECK(fwrite(bytes, 1, sizeof(bytes), input) == sizeof(bytes));
        rewind(input);
        reset_allocator(0);
        reset_position_save(0);
        reset_position_restore(1);
        CHECK(neverc_fmt_fscanf(input, "%2s", output.text) == 0);
        CHECK(position_save_count == 1);
        CHECK(position_restore_count == 1);
        CHECK(fseek_count == 0);
        CHECK(fsetpos_count == 1);
        CHECK(output.text[0] == 'Q');
        CHECK(output.text[1] == 'Q');
        CHECK(output.canary == 0xA5);
        CHECK(fclose(input) == 0);
    }
    puts("passed");
    return 0;
}
