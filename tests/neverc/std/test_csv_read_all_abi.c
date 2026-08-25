#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static size_t allocation_calls;

void *csv_failing_malloc(size_t size) {
    (void)size;
    allocation_calls++;
    return NULL;
}

void *csv_failing_calloc(size_t count, size_t size) {
    (void)count;
    (void)size;
    allocation_calls++;
    return NULL;
}

void *csv_failing_realloc(void *pointer, size_t size) {
    (void)pointer;
    (void)size;
    allocation_calls++;
    return NULL;
}

#define malloc csv_failing_malloc
#define calloc csv_failing_calloc
#define realloc csv_failing_realloc
#include "../../../std/src/encoding/csv/csv.c"
#undef realloc
#undef calloc
#undef malloc

typedef int (*v3389_read_all_func_t)(
    const char *, size_t, const char ***, int **, int,
    char *, size_t, const neverc_csv_reader_opts_t *);

static v3389_read_all_func_t v3389_read_all = neverc_csv_read_all;

typedef struct {
    uint64_t before;
    const char *fields[NEVERC_CSV_MAX_FIELDS];
    uint64_t after;
} guarded_row_t;

typedef struct {
    uint64_t before;
    int value;
    uint64_t after;
} guarded_count_t;

typedef struct {
    uint64_t before;
    char bytes[128];
    uint64_t after;
} guarded_work_t;

#define BEFORE UINT64_C(0x1122334455667788)
#define AFTER  UINT64_C(0x8877665544332211)

static int guards_ok(const guarded_row_t *row0, const guarded_row_t *row1,
                     const guarded_count_t *count0,
                     const guarded_count_t *count1,
                     const guarded_work_t *work) {
    return row0->before == BEFORE && row0->after == AFTER &&
           row1->before == BEFORE && row1->after == AFTER &&
           count0->before == BEFORE && count0->after == AFTER &&
           count1->before == BEFORE && count1->after == AFTER &&
           work->before == BEFORE && work->after == AFTER;
}

int main(void) {
    guarded_row_t row0, row1;
    guarded_count_t count0 = {BEFORE, -1, AFTER};
    guarded_count_t count1 = {BEFORE, -1, AFTER};
    guarded_work_t work;
    memset(&row0, 0, sizeof(row0));
    memset(&row1, 0, sizeof(row1));
    memset(&work, 0, sizeof(work));
    row0.before = row1.before = work.before = BEFORE;
    row0.after = row1.after = work.after = AFTER;

    const char **records[2] = {row0.fields, row1.fields};
    int *counts[2] = {&count0.value, &count1.value};
    allocation_calls = 0;
    int result = v3389_read_all(
        "a,b\nc,d\n", 8, records, counts, 2,
        work.bytes, sizeof(work.bytes), NULL);
    if (result != 2 || count0.value != 2 || count1.value != 2 ||
        strcmp(row0.fields[0], "a") != 0 ||
        strcmp(row0.fields[1], "b") != 0 ||
        strcmp(row1.fields[0], "c") != 0 ||
        strcmp(row1.fields[1], "d") != 0 ||
        records[0] != row0.fields || records[1] != row1.fields ||
        counts[0] != &count0.value || counts[1] != &count1.value ||
        allocation_calls != 0 ||
        !guards_ok(&row0, &row1, &count0, &count1, &work))
        return 1;

    /* The additive contiguous-count API retains the current zero-allocation
     * behavior rather than overloading the released symbol. */
    int flat_counts[2] = {-1, -1};
    result = neverc_csv_read_all_into(
        "x,y\nz,w\n", 8, records, flat_counts, 2,
        work.bytes, sizeof(work.bytes), NULL);
    if (result != 2 || flat_counts[0] != 2 || flat_counts[1] != 2 ||
        allocation_calls != 0 ||
        !guards_ok(&row0, &row1, &count0, &count1, &work))
        return 1;

    /* A missing legacy count destination fails before parsing the row and
     * cannot turn the integer count into a pointer or cross caller canaries. */
    int *missing_count[1] = {NULL};
    const char **one_record[1] = {row0.fields};
    if (neverc_csv_read_all("q,r\n", 4, one_record, missing_count, 1,
                           work.bytes, sizeof(work.bytes), NULL) != -1 ||
        allocation_calls != 0 ||
        !guards_ok(&row0, &row1, &count0, &count1, &work))
        return 1;

    puts("passed");
    return 0;
}
