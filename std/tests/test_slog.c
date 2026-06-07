#include "neverc/std/log/slog.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define ASSERT_STR_EQ(expr, expected) do { \
    const char *_v = (expr); const char *_e = (expected); tests_run++; \
    if (_v && _e && strcmp(_v, _e) == 0) { tests_passed++; } \
    else { tests_failed++; \
           printf("  FAIL: %s = \"%s\", expected \"%s\" (line %d)\n", \
                  #expr, _v ? _v : "(null)", _e, __LINE__); } \
} while(0)

#define ASSERT_TRUE(expr) do { tests_run++; \
    if (expr) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL: %s (line %d)\n", #expr, __LINE__); } \
} while(0)

static void test_level_names(void) {
    printf("[level_names]\n");
    ASSERT_STR_EQ(neverc_slog_level_name(NEVERC_SLOG_DEBUG), "DEBUG");
    ASSERT_STR_EQ(neverc_slog_level_name(NEVERC_SLOG_INFO), "INFO");
    ASSERT_STR_EQ(neverc_slog_level_name(NEVERC_SLOG_WARN), "WARN");
    ASSERT_STR_EQ(neverc_slog_level_name(NEVERC_SLOG_ERROR), "ERROR");
}

static void test_attr_constructors(void) {
    printf("[attr_constructors]\n");
    neverc_slog_attr_t a;

    a = neverc_slog_string("key", "value");
    ASSERT_STR_EQ(a.key, "key");
    ASSERT_TRUE(a.kind == NEVERC_SLOG_ATTR_STRING);
    ASSERT_STR_EQ(a.val.s, "value");

    a = neverc_slog_int64("count", 42);
    ASSERT_STR_EQ(a.key, "count");
    ASSERT_TRUE(a.kind == NEVERC_SLOG_ATTR_INT64);
    ASSERT_TRUE(a.val.i == 42);

    a = neverc_slog_uint64("size", 1024);
    ASSERT_STR_EQ(a.key, "size");
    ASSERT_TRUE(a.kind == NEVERC_SLOG_ATTR_UINT64);
    ASSERT_TRUE(a.val.u == 1024);

    a = neverc_slog_float64("pi", 3.14);
    ASSERT_STR_EQ(a.key, "pi");
    ASSERT_TRUE(a.kind == NEVERC_SLOG_ATTR_FLOAT64);
    ASSERT_TRUE(a.val.f == 3.14);

    a = neverc_slog_bool("ok", 1);
    ASSERT_STR_EQ(a.key, "ok");
    ASSERT_TRUE(a.kind == NEVERC_SLOG_ATTR_BOOL);
    ASSERT_TRUE(a.val.b == 1);
}

static void test_handler_init(void) {
    printf("[handler_init]\n");
    neverc_slog_handler_t h;
    neverc_slog_init(&h, NULL, NEVERC_SLOG_INFO, NEVERC_SLOG_FORMAT_TEXT);
    ASSERT_TRUE(h.output == stderr);
    ASSERT_TRUE(h.level == NEVERC_SLOG_INFO);
    ASSERT_TRUE(h.format == NEVERC_SLOG_FORMAT_TEXT);
}

static void test_level_filtering(void) {
    printf("[level_filtering]\n");
    neverc_slog_handler_t h;
    FILE *f = fopen("/dev/null", "w");
    neverc_slog_init(&h, f, NEVERC_SLOG_WARN, NEVERC_SLOG_FORMAT_TEXT);

    neverc_slog_log(&h, NEVERC_SLOG_INFO, "should not appear", NULL, 0);
    neverc_slog_log(&h, NEVERC_SLOG_WARN, "should appear", NULL, 0);
    neverc_slog_log(&h, NEVERC_SLOG_ERROR, "should appear", NULL, 0);

    tests_run++;
    tests_passed++;
    fclose(f);
}

static void test_text_output(void) {
    printf("[text_output]\n");
    char buf[4096];
    FILE *f = fmemopen(buf, sizeof(buf), "w");
    neverc_slog_handler_t h;
    neverc_slog_init(&h, f, NEVERC_SLOG_DEBUG, NEVERC_SLOG_FORMAT_TEXT);

    neverc_slog_attr_t attrs[] = {
        neverc_slog_string("user", "alice"),
        neverc_slog_int64("age", 30)
    };
    neverc_slog_log(&h, NEVERC_SLOG_INFO, "hello", attrs, 2);
    fclose(f);

    ASSERT_TRUE(strstr(buf, "level=INFO") != NULL);
    ASSERT_TRUE(strstr(buf, "msg=\"hello\"") != NULL);
    ASSERT_TRUE(strstr(buf, "user=alice") != NULL);
    ASSERT_TRUE(strstr(buf, "age=30") != NULL);
}

static void test_json_output(void) {
    printf("[json_output]\n");
    char buf[4096];
    FILE *f = fmemopen(buf, sizeof(buf), "w");
    neverc_slog_handler_t h;
    neverc_slog_init(&h, f, NEVERC_SLOG_DEBUG, NEVERC_SLOG_FORMAT_JSON);

    neverc_slog_attr_t attrs[] = {
        neverc_slog_string("key", "val"),
        neverc_slog_bool("ok", 1)
    };
    neverc_slog_log(&h, NEVERC_SLOG_ERROR, "fail", attrs, 2);
    fclose(f);

    ASSERT_TRUE(strstr(buf, "\"level\":\"ERROR\"") != NULL);
    ASSERT_TRUE(strstr(buf, "\"msg\":\"fail\"") != NULL);
    ASSERT_TRUE(strstr(buf, "\"key\":\"val\"") != NULL);
    ASSERT_TRUE(strstr(buf, "\"ok\":true") != NULL);
}

static void test_default_handler(void) {
    printf("[default_handler]\n");
    neverc_slog_handler_t *def = neverc_slog_default();
    ASSERT_TRUE(def != NULL);

    neverc_slog_handler_t h;
    neverc_slog_init(&h, stderr, NEVERC_SLOG_WARN, NEVERC_SLOG_FORMAT_JSON);
    neverc_slog_set_default(&h);

    neverc_slog_handler_t *def2 = neverc_slog_default();
    ASSERT_TRUE(def2->level == NEVERC_SLOG_WARN);
    ASSERT_TRUE(def2->format == NEVERC_SLOG_FORMAT_JSON);
}

static void test_set_level(void) {
    printf("[set_level]\n");
    neverc_slog_handler_t h;
    neverc_slog_init(&h, NULL, NEVERC_SLOG_INFO, NEVERC_SLOG_FORMAT_TEXT);
    ASSERT_TRUE(h.level == NEVERC_SLOG_INFO);

    neverc_slog_set_level(&h, NEVERC_SLOG_DEBUG);
    ASSERT_TRUE(h.level == NEVERC_SLOG_DEBUG);
}

int main(void) {
    printf("=== NeverC log/slog Tests ===\n");
    test_level_names();
    test_attr_constructors();
    test_handler_init();
    test_level_filtering();
    test_text_output();
    test_json_output();
    test_default_handler();
    test_set_level();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
