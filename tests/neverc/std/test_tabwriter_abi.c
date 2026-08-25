#include "neverc/std/text/tabwriter.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    char   *out_buf;
    size_t  out_len;
    size_t  out_cap;

    int     minwidth;
    int     tabwidth;
    int     padding;
    char    padchar;
    unsigned flags;

    char    buf[64 * 1024];
    size_t  buf_len;
    int     buf_carved;

    neverc_tabwriter_cell_t cells[256];
    int     ncells;

    int     col_widths[256];
    int     ncols;

    int     lines_start[4096];
    int     lines_ncells[4096];
    int     nlines;
} v3389_tabwriter_t;

#if UINTPTR_MAX == UINT64_MAX
_Static_assert(sizeof(neverc_tabwriter_t) == 102472,
               "v3389.1.4 64-bit tabwriter size");
#elif UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(neverc_tabwriter_t) == 102452,
               "v3389.1.4 32-bit tabwriter size");
#endif

#define ABI_TYPE_EQ(current, legacy)                                      \
    _Static_assert(sizeof(current) == sizeof(legacy),                     \
                   "v3389.1.4 tabwriter size ABI");                      \
    _Static_assert(_Alignof(current) == _Alignof(legacy),                 \
                   "v3389.1.4 tabwriter alignment ABI")
#define ABI_FIELD_EQ(field)                                                \
    _Static_assert(offsetof(neverc_tabwriter_t, field) ==                 \
                       offsetof(v3389_tabwriter_t, field),                \
                   "v3389.1.4 tabwriter field offset ABI")

ABI_TYPE_EQ(neverc_tabwriter_t, v3389_tabwriter_t);
ABI_FIELD_EQ(out_buf);
ABI_FIELD_EQ(out_len);
ABI_FIELD_EQ(out_cap);
ABI_FIELD_EQ(minwidth);
ABI_FIELD_EQ(tabwidth);
ABI_FIELD_EQ(padding);
ABI_FIELD_EQ(padchar);
ABI_FIELD_EQ(flags);
ABI_FIELD_EQ(buf);
ABI_FIELD_EQ(buf_len);
ABI_FIELD_EQ(buf_carved);
ABI_FIELD_EQ(cells);
ABI_FIELD_EQ(ncells);
ABI_FIELD_EQ(col_widths);
ABI_FIELD_EQ(ncols);
ABI_FIELD_EQ(lines_start);
ABI_FIELD_EQ(lines_ncells);
ABI_FIELD_EQ(nlines);

#undef ABI_FIELD_EQ
#undef ABI_TYPE_EQ

typedef struct {
    uint64_t before;
    neverc_tabwriter_t writer;
    uint64_t after;
} guarded_writer_t;

static const uint64_t BEFORE_CANARY = UINT64_C(0x41b1c2d3e4f50617);
static const uint64_t AFTER_CANARY = UINT64_C(0x8293a4b5c6d7e8f9);

static int canaries_valid(const guarded_writer_t *guarded) {
    return guarded->before == BEFORE_CANARY &&
           guarded->after == AFTER_CANARY;
}

static int test_overflow_cells_are_private(void) {
    guarded_writer_t guarded;
    memset(&guarded, 0, sizeof(guarded));
    guarded.before = BEFORE_CANARY;
    guarded.after = AFTER_CANARY;
    neverc_tabwriter_init(&guarded.writer, 1, 0, 1, ' ', 0);

    char input[100 * 6];
    for (size_t i = 0; i < 100; i++)
        memcpy(input + i * 6, "a\tb\tc\n", 6);
    neverc_tabwriter_write(&guarded.writer, input, sizeof(input));
    neverc_tabwriter_flush(&guarded.writer);

    size_t output_len = 0;
    const char *output = neverc_tabwriter_output(
        &guarded.writer, &output_len);
    int valid = output != NULL && output_len == sizeof(input);
    for (size_t i = 0; valid && i < 100; i++)
        valid = memcmp(output + i * 6, "a b c\n", 6) == 0;
    valid = valid && canaries_valid(&guarded);

    neverc_tabwriter_reset(&guarded.writer);
    return valid && canaries_valid(&guarded);
}

static int test_sticky_error_stays_in_bounds(void) {
    guarded_writer_t guarded;
    memset(&guarded, 0, sizeof(guarded));
    guarded.before = BEFORE_CANARY;
    guarded.after = AFTER_CANARY;
    neverc_tabwriter_init(&guarded.writer, 1, 0, 0, ' ', 0);

    char tabs[NEVERC_TABWRITER_MAX_CELLS + 1];
    memset(tabs, '\t', sizeof(tabs));
    neverc_tabwriter_write(&guarded.writer, tabs, sizeof(tabs));
    neverc_tabwriter_flush(&guarded.writer);

    size_t output_len = 123;
    int valid = neverc_tabwriter_output(&guarded.writer, &output_len) == NULL &&
                output_len == 0 && canaries_valid(&guarded);

    /* The sticky failure path and recovery must not write past the released
     * object even though all added state lives outside that object. */
    neverc_tabwriter_write(&guarded.writer, "ignored", 7);
    neverc_tabwriter_flush(&guarded.writer);
    valid = valid && canaries_valid(&guarded);
    neverc_tabwriter_reset(&guarded.writer);
    return valid && canaries_valid(&guarded);
}

int main(void) {
    int failures = 0;
    if (!test_overflow_cells_are_private()) {
        fputs("tabwriter overflow-cell ABI regression failed\n", stderr);
        failures++;
    }
    if (!test_sticky_error_stays_in_bounds()) {
        fputs("tabwriter sticky-error ABI regression failed\n", stderr);
        failures++;
    }
    if (failures == 0) puts("passed");
    return failures == 0 ? 0 : 1;
}
