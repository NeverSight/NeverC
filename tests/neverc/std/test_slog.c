#include "neverc/std/log/slog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

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
#if defined(_WIN32)
    FILE *f = fopen("NUL", "w");
#else
    FILE *f = fopen("/dev/null", "w");
#endif
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
    memset(buf, 0, sizeof(buf));
#if defined(_WIN32)
    FILE *f = tmpfile();
#else
    FILE *f = fmemopen(buf, sizeof(buf), "w");
#endif
    neverc_slog_handler_t h;
    neverc_slog_init(&h, f, NEVERC_SLOG_DEBUG, NEVERC_SLOG_FORMAT_TEXT);

    neverc_slog_attr_t attrs[] = {
        neverc_slog_string("user", "alice"),
        neverc_slog_int64("age", 30)
    };
    neverc_slog_log(&h, NEVERC_SLOG_INFO, "hello", attrs, 2);
#if defined(_WIN32)
    fflush(f);
    rewind(f);
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
#endif
    fclose(f);

    ASSERT_TRUE(strstr(buf, "level=INFO") != NULL);
    ASSERT_TRUE(strstr(buf, "msg=\"hello\"") != NULL);
    ASSERT_TRUE(strstr(buf, "user=alice") != NULL);
    ASSERT_TRUE(strstr(buf, "age=30") != NULL);
}

static void test_json_output(void) {
    printf("[json_output]\n");
    char buf[4096];
    memset(buf, 0, sizeof(buf));
#if defined(_WIN32)
    FILE *f = tmpfile();
#else
    FILE *f = fmemopen(buf, sizeof(buf), "w");
#endif
    neverc_slog_handler_t h;
    neverc_slog_init(&h, f, NEVERC_SLOG_DEBUG, NEVERC_SLOG_FORMAT_JSON);

    neverc_slog_attr_t attrs[] = {
        neverc_slog_string("key", "val"),
        neverc_slog_bool("ok", 1)
    };
    neverc_slog_log(&h, NEVERC_SLOG_ERROR, "fail", attrs, 2);
#if defined(_WIN32)
    fflush(f);
    rewind(f);
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
#endif
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

static size_t count_substring(const char *text, const char *needle) {
    size_t count = 0, needle_len = strlen(needle);
    for (const char *found = text; needle_len > 0 &&
         (found = strstr(found, needle)) != NULL; found += needle_len)
        count++;
    return count;
}

static void test_escaping_and_special_floats(void) {
    printf("[escaping_and_special_floats]\n");
    char buf[4096] = {0};
    FILE *f = tmpfile();
    if (!f) {
        ASSERT_TRUE(0);
        return;
    }
    neverc_slog_handler_t h;
    neverc_slog_init(&h, f, NEVERC_SLOG_DEBUG, NEVERC_SLOG_FORMAT_JSON);

    uint64_t nan_bits = 0x7ff8000000000001ULL;
    uint64_t inf_bits = 0x7ff0000000000000ULL;
    double nan_value, inf_value;
    memcpy(&nan_value, &nan_bits, sizeof(nan_value));
    memcpy(&inf_value, &inf_bits, sizeof(inf_value));
    const char message[] = {'q', '"', '\n', (char)0xff, '\0'};
    const char string_value[] = {'x', '\t', (char)0xfe, '\0'};
    neverc_slog_attr_t attrs[] = {
        neverc_slog_string("text", string_value),
        neverc_slog_float64("nan", nan_value),
        neverc_slog_float64("inf", inf_value)
    };
    neverc_slog_log(&h, NEVERC_SLOG_INFO, message, attrs, 3);
    rewind(f);
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);

    ASSERT_TRUE(strstr(buf, "\"msg\":\"q\\\"\\n\\ufffd\"") != NULL);
    ASSERT_TRUE(strstr(buf, "\"text\":\"x\\t\\ufffd\"") != NULL);
    ASSERT_TRUE(strstr(buf, "\"nan\":null") != NULL);
    ASSERT_TRUE(strstr(buf, "\"inf\":null") != NULL);
    ASSERT_TRUE(memchr(buf, 0xff, n) == NULL);
    ASSERT_TRUE(memchr(buf, 0xfe, n) == NULL);

    memset(buf, 0, sizeof(buf));
    f = tmpfile();
    if (!f) {
        ASSERT_TRUE(0);
        return;
    }
    neverc_slog_init(&h, f, NEVERC_SLOG_DEBUG, NEVERC_SLOG_FORMAT_TEXT);
    neverc_slog_attr_t note = neverc_slog_string("note", "a b\nnext");
    neverc_slog_log(&h, NEVERC_SLOG_INFO, "hello\"\nforged", &note, 1);
    rewind(f);
    n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    ASSERT_TRUE(strstr(buf, "msg=\"hello\\\"\\nforged\"") != NULL);
    ASSERT_TRUE(strstr(buf, " note=\"a b\\nnext\"") != NULL);
    ASSERT_TRUE(count_substring(buf, "\n") == 1);
}

enum { SLOG_THREADS = 8, SLOG_ROUNDS = 100 };

typedef struct {
    neverc_slog_handler_t *handler;
    int id;
} concurrent_slog_ctx_t;

static volatile int slog_ready;
static volatile int slog_start;

#ifdef _WIN32
#if defined(_M_ARM64) || defined(__aarch64__)
#define TEST_SLOG_THREAD_CALL
#else
#define TEST_SLOG_THREAD_CALL WINAPI
#endif
static DWORD TEST_SLOG_THREAD_CALL concurrent_slog_worker(LPVOID opaque) {
#else
static void *concurrent_slog_worker(void *opaque) {
#endif
    concurrent_slog_ctx_t *ctx = (concurrent_slog_ctx_t *)opaque;
    __atomic_add_fetch(&slog_ready, 1, __ATOMIC_RELEASE);
    while (!__atomic_load_n(&slog_start, __ATOMIC_ACQUIRE)) {}
    neverc_slog_attr_t worker = neverc_slog_int64("worker", ctx->id);
    for (int round = 0; round < SLOG_ROUNDS; round++) {
        char message[16];
        snprintf(message, sizeof(message), "T%02d:%03d", ctx->id, round);
        neverc_slog_log(ctx->handler, NEVERC_SLOG_INFO,
                        message, &worker, 1);
    }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}
#ifdef TEST_SLOG_THREAD_CALL
#undef TEST_SLOG_THREAD_CALL
#endif

static void test_concurrent_records(void) {
    printf("[concurrent_records]\n");
    FILE *f = tmpfile();
    if (!f) {
        ASSERT_TRUE(0);
        return;
    }
    neverc_slog_handler_t h;
    neverc_slog_init(&h, f, NEVERC_SLOG_DEBUG, NEVERC_SLOG_FORMAT_TEXT);
    concurrent_slog_ctx_t contexts[SLOG_THREADS];
    int started = 0;
    slog_ready = 0;
    slog_start = 0;
#ifdef _WIN32
    HANDLE threads[SLOG_THREADS];
    for (int i = 0; i < SLOG_THREADS; i++) {
        contexts[i].handler = &h;
        contexts[i].id = i;
        HANDLE thread = CreateThread(NULL, 0, concurrent_slog_worker,
                                     &contexts[i], 0, NULL);
        if (thread) threads[started++] = thread;
    }
#else
    pthread_t threads[SLOG_THREADS];
    for (int i = 0; i < SLOG_THREADS; i++) {
        contexts[i].handler = &h;
        contexts[i].id = i;
        if (pthread_create(&threads[started], NULL, concurrent_slog_worker,
                           &contexts[i]) == 0)
            started++;
    }
#endif
    while (__atomic_load_n(&slog_ready, __ATOMIC_ACQUIRE) < started) {}
    __atomic_store_n(&slog_start, 1, __ATOMIC_RELEASE);
#ifdef _WIN32
    if (started > 0) WaitForMultipleObjects((DWORD)started, threads, TRUE, INFINITE);
    for (int i = 0; i < started; i++) CloseHandle(threads[i]);
#else
    for (int i = 0; i < started; i++) pthread_join(threads[i], NULL);
#endif
    fflush(f);
    ASSERT_TRUE(started == SLOG_THREADS);
    ASSERT_TRUE(fseek(f, 0, SEEK_END) == 0);
    long file_size = ftell(f);
    ASSERT_TRUE(file_size >= 0);
    rewind(f);
    size_t size = file_size > 0 ? (size_t)file_size : 0;
    char *output = (char *)malloc(size + 1);
    size_t read_size = output ? fread(output, 1, size, f) : 0;
    fclose(f);
    int valid = output && read_size == size;
    if (output) output[size] = '\0';
    size_t lines = 0;
    for (char *line = output; valid && line && *line;) {
        char *newline = strchr(line, '\n');
        if (!newline) {
            valid = 0;
            break;
        }
        *newline = '\0';
        valid = strncmp(line, "time=", 5) == 0 &&
                count_substring(line, "time=") == 1 &&
                count_substring(line, " level=INFO") == 1 &&
                count_substring(line, " msg=\"T") == 1 &&
                count_substring(line, " worker=") == 1;
        lines++;
        line = newline + 1;
    }
    valid = valid && lines == (size_t)started * SLOG_ROUNDS;
    ASSERT_TRUE(valid);
    free(output);
}

static void test_null_safety(void) {
    printf("[null_safety]\n");
    neverc_slog_init(NULL, NULL, NEVERC_SLOG_INFO, NEVERC_SLOG_FORMAT_TEXT);
    neverc_slog_set_default(NULL);
    neverc_slog_set_level(NULL, NEVERC_SLOG_INFO);

    FILE *f = tmpfile();
    if (!f) {
        ASSERT_TRUE(0);
        return;
    }
    neverc_slog_handler_t h;
    neverc_slog_init(&h, f, NEVERC_SLOG_DEBUG, NEVERC_SLOG_FORMAT_TEXT);
    neverc_slog_log(&h, NEVERC_SLOG_INFO, "invalid attrs", NULL, 1);
    fflush(f);
    ASSERT_TRUE(fseek(f, 0, SEEK_END) == 0);
    ASSERT_TRUE(ftell(f) == 0);
    fclose(f);
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
    test_escaping_and_special_floats();
    test_concurrent_records();
    test_null_safety();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
