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

#define NC_TEMPLATE_MALLOC controlled_malloc
#define NC_TEMPLATE_CALLOC controlled_calloc
#define NC_TEMPLATE_REALLOC controlled_realloc
#include "../../../std/src/text/template/template.c"

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
    static const char source[] =
        "begin {{.Name}} {{if .Show}}yes {{range .Items}}item{{end}}"
        "{{else}}no{{end}} finish";

    reset_allocator(0);
    const char *err = NULL;
    neverc_template_t *tmpl = neverc_template_parse(source, &err);
    CHECK(tmpl != NULL);
    CHECK(err == NULL);
    size_t parse_allocations = allocation_count;
    neverc_template_free(tmpl);

    for (size_t i = 1; i <= parse_allocations; i++) {
        reset_allocator(i);
        err = NULL;
        tmpl = neverc_template_parse(source, &err);
        CHECK(tmpl == NULL);
        CHECK(err != NULL);
    }

    neverc_template_data_t data;
    neverc_template_data_init(&data);
    reset_allocator(1);
    neverc_template_data_set(&data, "Name", "ignored");
    CHECK(data.nvars == 0);
    CHECK(data.vars == NULL);

    char long_value[513];
    memset(long_value, 'x', sizeof(long_value) - 1U);
    long_value[sizeof(long_value) - 1U] = '\0';
    reset_allocator(0);
    neverc_template_data_set(&data, "Name", long_value);
    neverc_template_data_set(&data, "Show", "true");
    neverc_template_data_set(&data, "Items", "present");
    CHECK(data.nvars == 3);

    reset_allocator(0);
    tmpl = neverc_template_parse(source, &err);
    CHECK(tmpl != NULL);
    reset_allocator(0);
    size_t outlen = 0;
    char *result = neverc_template_execute(tmpl, &data, &outlen);
    CHECK(result != NULL);
    CHECK(outlen > sizeof(long_value));
    size_t execute_allocations = allocation_count;
    free(result);

    for (size_t i = 1; i <= execute_allocations; i++) {
        reset_allocator(i);
        outlen = 99;
        result = neverc_template_execute(tmpl, &data, &outlen);
        CHECK(result == NULL);
        CHECK(outlen == 0);
    }

    neverc_template_free(tmpl);
    neverc_template_data_free(&data);
    puts("passed");
    return 0;
}
