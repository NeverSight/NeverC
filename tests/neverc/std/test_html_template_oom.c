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

#define malloc controlled_malloc
#define calloc controlled_calloc
#define realloc controlled_realloc
/* Do not include html.c here: it defines the same static html_esc_extra table
 * as template.c. Provide unescape via the hooked allocator so entity-prefix
 * checks still exercise malloc failure. */
char *neverc_html_unescape_string(const char *s, size_t *outlen) {
    size_t n;
    char *out;

    if (!s) {
        if (outlen) {
            *outlen = 0;
        }
        return NULL;
    }
    n = strlen(s);
    out = malloc(n + 1);
    if (!out) {
        if (outlen) {
            *outlen = 0;
        }
        return NULL;
    }
    memcpy(out, s, n + 1);
    if (outlen) {
        *outlen = n;
    }
    return out;
}
#include "../../../std/src/html/template/template.c"
#undef malloc
#undef calloc
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

int main(void) {
    static const char source[] =
        "<p>{{.Name}}</p>{{if .Show}}yes{{else}}no{{end}}"
        "{{range .Items}}item{{end}}";

    reset_allocator(0);
    neverc_html_template_t *t = neverc_html_template_parse(source);
    CHECK(t != NULL);
    size_t parse_allocations = allocation_count;
    neverc_html_template_free(t);

    for (size_t i = 1; i <= parse_allocations; i++) {
        reset_allocator(i);
        t = neverc_html_template_parse(source);
        CHECK(t == NULL);
    }

    neverc_html_template_data_t data;
    reset_allocator(0);
    neverc_html_template_data_init(&data);
    neverc_html_template_data_set(&data, "Name", "<unsafe>");
    neverc_html_template_data_set(&data, "Show", "true");
    neverc_html_template_data_set(&data, "Items", "present");
    CHECK(data.count == 3);

    reset_allocator(0);
    t = neverc_html_template_parse(source);
    CHECK(t != NULL);
    reset_allocator(0);
    char *out = neverc_html_template_execute(t, &data);
    CHECK(out != NULL);
    size_t execute_allocations = allocation_count;
    free(out);

    for (size_t i = 1; i <= execute_allocations; i++) {
        reset_allocator(i);
        out = neverc_html_template_execute(t, &data);
        CHECK(out == NULL);
    }

    neverc_html_template_free(t);
    neverc_html_template_data_free(&data);
    puts("passed");
    return 0;
}
