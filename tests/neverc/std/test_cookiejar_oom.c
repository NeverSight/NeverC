#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t allocation_count;
static size_t successful_allocations;
static size_t free_count;
static size_t fail_at;

static int allocation_fails(void) {
    allocation_count++;
    return fail_at != 0 && allocation_count == fail_at;
}

static void *controlled_malloc(size_t size) {
    if (allocation_fails()) return NULL;
    void *result = malloc(size);
    if (result) successful_allocations++;
    return result;
}

static void *controlled_calloc(size_t count, size_t size) {
    if (allocation_fails()) return NULL;
    void *result = calloc(count, size);
    if (result) successful_allocations++;
    return result;
}

static void controlled_free(void *ptr) {
    if (ptr) free_count++;
    free(ptr);
}

#define malloc controlled_malloc
#define calloc controlled_calloc
#define free controlled_free
#include "../../../std/src/net/http/cookiejar/cookiejar.c"
#undef malloc
#undef calloc
#undef free

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",             \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static void reset_allocator(size_t failure) {
    allocation_count = 0;
    successful_allocations = 0;
    free_count = 0;
    fail_at = failure;
}

static neverc_cookiejar_t *new_jar(void) {
    reset_allocator(0);
    return neverc_cookiejar_new();
}

static const neverc_cookiejar_entry_t old_cookie = {
    .name = "token",
    .value = "old",
    .domain = "example.com",
    .path = "/",
};

int main(void) {
    neverc_cookiejar_t *jar = new_jar();
    CHECK(jar != NULL);
    reset_allocator(0);
    neverc_cookiejar_set_cookies(
        jar, "https://example.com/", &old_cookie, 1);
    size_t insert_allocations = allocation_count;
    CHECK(insert_allocations > 0);
    CHECK(neverc_cookiejar_count(jar) == 1);
    neverc_cookiejar_free(jar);

    for (size_t failure = 1; failure <= insert_allocations; failure++) {
        jar = new_jar();
        CHECK(jar != NULL);
        reset_allocator(failure);
        neverc_cookiejar_set_cookies(
            jar, "https://example.com/", &old_cookie, 1);
        CHECK(neverc_cookiejar_count(jar) == 0);
        CHECK(free_count == successful_allocations);
        reset_allocator(0);
        neverc_cookiejar_free(jar);
    }

    jar = new_jar();
    CHECK(jar != NULL);
    reset_allocator(0);
    neverc_cookiejar_set_cookies(
        jar, "https://example.com/", &old_cookie, 1);
    neverc_cookiejar_entry_t new_cookie = old_cookie;
    new_cookie.value = "new";
    reset_allocator(0);
    neverc_cookiejar_set_cookies(
        jar, "https://example.com/", &new_cookie, 1);
    size_t update_allocations = allocation_count;
    CHECK(update_allocations > 0);
    neverc_cookiejar_free(jar);

    for (size_t failure = 1; failure <= update_allocations; failure++) {
        jar = new_jar();
        CHECK(jar != NULL);
        reset_allocator(0);
        neverc_cookiejar_set_cookies(
            jar, "https://example.com/", &old_cookie, 1);
        reset_allocator(failure);
        neverc_cookiejar_set_cookies(
            jar, "https://example.com/", &new_cookie, 1);
        CHECK(neverc_cookiejar_count(jar) == 1);
        neverc_cookiejar_entry_t out[1];
        CHECK(neverc_cookiejar_cookies(
                  jar, "https://example.com/", out, 1) == 1);
        CHECK(out[0].value != NULL);
        CHECK(strcmp(out[0].value, "old") == 0);
        CHECK(free_count == successful_allocations);
        reset_allocator(0);
        neverc_cookiejar_free(jar);
    }

    jar = new_jar();
    CHECK(jar != NULL);
    neverc_cookiejar_entry_t invalid_cookie = old_cookie;
    invalid_cookie.value = NULL;
    reset_allocator(0);
    neverc_cookiejar_set_cookies(
        jar, "https://example.com/", &invalid_cookie, 1);
    CHECK(neverc_cookiejar_count(jar) == 0);
    neverc_cookiejar_free(jar);

    puts("passed");
    return 0;
}
