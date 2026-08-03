#include "neverc/std/log.h"
#include <stdio.h>
#include <string.h>

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

int main(void) {
    printf("=== NeverC Log Module Tests ===\n\n");
    test_basic_output();
    test_prefix();
    test_date_time();
    test_printf();
    test_msg_prefix();
    test_microseconds_and_accessors();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
