#include "neverc/std/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_bool(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}

static void check_contains(const char *name, const char *haystack,
                            const char *needle) {
    tests_run++;
    if (strstr(haystack, needle)) tests_passed++;
    else {
        tests_failed++;
        printf("  FAIL: %s: \"%s\" does not contain \"%s\"\n",
               name, haystack, needle);
    }
}

static void test_basic_output(void) {
    printf("[basic output]\n");
    char buf[512];
    FILE *tmp = tmpfile();
    neverc_log_logger_t l;
    neverc_log_init(&l, tmp, "", 0);

    neverc_log_println(&l, "hello log");
    fflush(tmp);
    rewind(tmp);
    size_t n = fread(buf, 1, sizeof(buf) - 1, tmp);
    buf[n] = '\0';
    fclose(tmp);

    check_contains("basic msg", buf, "hello log");
    check_contains("has newline", buf, "\n");
}

static void test_prefix(void) {
    printf("[prefix]\n");
    char buf[512];
    FILE *tmp = tmpfile();
    neverc_log_logger_t l;
    neverc_log_init(&l, tmp, "[INFO] ", 0);

    neverc_log_println(&l, "test message");
    fflush(tmp);
    rewind(tmp);
    size_t n = fread(buf, 1, sizeof(buf) - 1, tmp);
    buf[n] = '\0';
    fclose(tmp);

    check_contains("prefix present", buf, "[INFO] test message");
}

static void test_date_time(void) {
    printf("[date/time]\n");
    char buf[512];
    FILE *tmp = tmpfile();
    neverc_log_logger_t l;
    neverc_log_init(&l, tmp, "", NEVERC_LOG_LDATE | NEVERC_LOG_LTIME);

    neverc_log_println(&l, "timed");
    fflush(tmp);
    rewind(tmp);
    size_t n = fread(buf, 1, sizeof(buf) - 1, tmp);
    buf[n] = '\0';
    fclose(tmp);

    check_bool("has slash (date)", strchr(buf, '/') != NULL, 1);
    check_bool("has colon (time)", strchr(buf, ':') != NULL, 1);
    check_contains("has msg", buf, "timed");
}

static void test_printf(void) {
    printf("[printf]\n");
    char buf[512];
    FILE *tmp = tmpfile();
    neverc_log_logger_t l;
    neverc_log_init(&l, tmp, "PRE: ", 0);

    neverc_log_printf(&l, "count=%d name=%s", 42, "Alice");
    fflush(tmp);
    rewind(tmp);
    size_t n = fread(buf, 1, sizeof(buf) - 1, tmp);
    buf[n] = '\0';
    fclose(tmp);

    check_contains("printf prefix", buf, "PRE: ");
    check_contains("printf content", buf, "count=42 name=Alice");
}

static void test_msg_prefix(void) {
    printf("[msg prefix]\n");
    char buf[512];
    FILE *tmp = tmpfile();
    neverc_log_logger_t l;
    neverc_log_init(&l, tmp, "[WARN] ", NEVERC_LOG_LDATE | NEVERC_LOG_LMSGPREFIX);

    neverc_log_println(&l, "warning!");
    fflush(tmp);
    rewind(tmp);
    size_t n = fread(buf, 1, sizeof(buf) - 1, tmp);
    buf[n] = '\0';
    fclose(tmp);

    check_contains("msg prefix after date", buf, "[WARN] warning!");
}

static void test_microseconds_and_accessors(void) {
    printf("[microseconds/accessors]\n");
    char buf[512];
    FILE *tmp = tmpfile();
    neverc_log_logger_t l;
    neverc_log_init(&l, tmp, "MICRO ", NEVERC_LOG_LMICRO);

    check_bool("flags accessor",
               neverc_log_flags(&l) == NEVERC_LOG_LMICRO, 1);
    check_bool("prefix accessor",
               strcmp(neverc_log_prefix(&l), "MICRO ") == 0, 1);
    check_bool("writer accessor", neverc_log_writer(&l) == tmp, 1);

    neverc_log_println(&l, "timed");
    fflush(tmp);
    rewind(tmp);
    size_t n = fread(buf, 1, sizeof(buf) - 1, tmp);
    buf[n] = '\0';
    fclose(tmp);

    const char *dot = strchr(buf, '.');
    check_bool("microseconds include time", strchr(buf, ':') != NULL, 1);
    check_bool("microseconds include fraction", dot != NULL, 1);
    int six_digits = dot != NULL;
    for (int i = 1; i <= 6 && six_digits; i++)
        six_digits = dot[i] >= '0' && dot[i] <= '9';
    check_bool("microseconds have six digits", six_digits, 1);
}

static void test_null_safety(void) {
    printf("[null safety]\n");

    neverc_log_init(NULL, NULL, NULL, 0);
    neverc_log_set_output(NULL, NULL);
    neverc_log_set_prefix(NULL, NULL);
    neverc_log_set_flags(NULL, 0);
    neverc_log_print(NULL, NULL);
    neverc_log_printf(NULL, NULL);
    neverc_log_println(NULL, NULL);
    check_bool("null logger flags are zero", neverc_log_flags(NULL) == 0, 1);
    check_bool("null logger prefix is null", neverc_log_prefix(NULL) == NULL, 1);
    check_bool("null logger writer is stderr",
               neverc_log_writer(NULL) == stderr, 1);

    neverc_log_logger_t logger;
    neverc_log_init(&logger, NULL, NULL, 0);
    check_bool("null init output uses stderr",
               neverc_log_writer(&logger) == stderr, 1);
    neverc_log_set_output(&logger, NULL);
    check_bool("null set output uses stderr",
               neverc_log_writer(&logger) == stderr, 1);
    neverc_log_print(&logger, NULL);
    neverc_log_printf(&logger, NULL);
    neverc_log_println(&logger, NULL);
}

enum { CONCURRENT_THREADS = 8, CONCURRENT_ROUNDS = 100,
       CONCURRENT_PREFIX_LEN = 4096 };

typedef struct {
    neverc_log_logger_t *logger;
    int id;
} concurrent_log_ctx_t;

static volatile int concurrent_ready;
static volatile int concurrent_start;

#ifdef _WIN32
#if defined(_M_ARM64) || defined(__aarch64__)
#define TEST_LOG_THREAD_CALL
#else
#define TEST_LOG_THREAD_CALL WINAPI
#endif
static DWORD TEST_LOG_THREAD_CALL concurrent_log_worker(LPVOID opaque) {
#else
static void *concurrent_log_worker(void *opaque) {
#endif
    concurrent_log_ctx_t *ctx = (concurrent_log_ctx_t *)opaque;
    __atomic_add_fetch(&concurrent_ready, 1, __ATOMIC_RELEASE);
    while (!__atomic_load_n(&concurrent_start, __ATOMIC_ACQUIRE)) {}
    for (int round = 0; round < CONCURRENT_ROUNDS; round++)
        neverc_log_printf(ctx->logger, "T%02d:%03d\n", ctx->id, round);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}
#ifdef TEST_LOG_THREAD_CALL
#undef TEST_LOG_THREAD_CALL
#endif

static void test_concurrent_entries(void) {
    printf("[concurrent entries]\n");

    FILE *tmp = tmpfile();
    if (!tmp) {
        check_bool("concurrent output file opens", 0, 1);
        return;
    }
    char prefix[CONCURRENT_PREFIX_LEN + 1];
    memset(prefix, 'P', CONCURRENT_PREFIX_LEN);
    prefix[CONCURRENT_PREFIX_LEN] = '\0';
    neverc_log_logger_t logger;
    neverc_log_init(&logger, tmp, prefix, 0);

    concurrent_ready = 0;
    concurrent_start = 0;
    concurrent_log_ctx_t contexts[CONCURRENT_THREADS];
    int started = 0;
#ifdef _WIN32
    HANDLE threads[CONCURRENT_THREADS];
    for (int i = 0; i < CONCURRENT_THREADS; i++) {
        contexts[i].logger = &logger;
        contexts[i].id = i;
        HANDLE thread = CreateThread(NULL, 0, concurrent_log_worker,
                                     &contexts[i], 0, NULL);
        if (thread) threads[started++] = thread;
    }
#else
    pthread_t threads[CONCURRENT_THREADS];
    for (int i = 0; i < CONCURRENT_THREADS; i++) {
        contexts[i].logger = &logger;
        contexts[i].id = i;
        if (pthread_create(&threads[started], NULL, concurrent_log_worker,
                           &contexts[i]) == 0)
            started++;
    }
#endif
    while (__atomic_load_n(&concurrent_ready, __ATOMIC_ACQUIRE) < started) {}
    __atomic_store_n(&concurrent_start, 1, __ATOMIC_RELEASE);
#ifdef _WIN32
    if (started > 0) WaitForMultipleObjects((DWORD)started, threads, TRUE, INFINITE);
    for (int i = 0; i < started; i++) CloseHandle(threads[i]);
#else
    for (int i = 0; i < started; i++) pthread_join(threads[i], NULL);
#endif

    fflush(tmp);
    check_bool("all concurrent workers start", started, CONCURRENT_THREADS);
    check_bool("concurrent output seek succeeds", fseek(tmp, 0, SEEK_END) == 0, 1);
    long file_size = ftell(tmp);
    check_bool("concurrent output size is valid", file_size >= 0, 1);
    rewind(tmp);
    size_t size = file_size > 0 ? (size_t)file_size : 0;
    char *output = (char *)malloc(size ? size : 1);
    size_t read_size = output ? fread(output, 1, size, tmp) : 0;

    const size_t message_len = 8;
    const size_t record_len = CONCURRENT_PREFIX_LEN + message_len;
    size_t expected_size = (size_t)started * CONCURRENT_ROUNDS * record_len;
    int valid = output && read_size == size && size == expected_size;
    for (size_t pos = 0; valid && pos < size; pos += record_len) {
        for (size_t i = 0; i < CONCURRENT_PREFIX_LEN; i++)
            if (output[pos + i] != 'P') valid = 0;
        const char *message = output + pos + CONCURRENT_PREFIX_LEN;
        valid = valid && message[0] == 'T' &&
                message[1] >= '0' && message[1] <= '9' &&
                message[2] >= '0' && message[2] <= '9' &&
                message[3] == ':' &&
                message[4] >= '0' && message[4] <= '9' &&
                message[5] >= '0' && message[5] <= '9' &&
                message[6] >= '0' && message[6] <= '9' &&
                message[7] == '\n';
    }
    check_bool("concurrent records remain atomic", valid, 1);
    free(output);
    fclose(tmp);
}

int main(void) {
    printf("=== NeverC Log Module Tests ===\n\n");
    test_basic_output();
    test_prefix();
    test_date_time();
    test_printf();
    test_msg_prefix();
    test_microseconds_and_accessors();
    test_concurrent_entries();
    test_null_safety();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
