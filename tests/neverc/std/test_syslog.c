#include "neverc/std/log/syslog.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define ASSERT_TRUE(e) do{tests_run++;if(e){tests_passed++;}else{tests_failed++;printf("  FAIL [%d]: %s\n",__LINE__,#e);}}while(0)
#define ASSERT_EQ(a,b) do{int _a=(a),_b=(b);tests_run++;if(_a==_b){tests_passed++;}else{tests_failed++;printf("  FAIL [%d]: %s=%d, want %d\n",__LINE__,#a,_a,_b);}}while(0)

static void test_open_close(void) {
    printf("[open/close]\n");
    neverc_syslog_t *log = neverc_syslog_open("neverc_test",
                                               NEVERC_SYSLOG_USER,
                                               NEVERC_SYSLOG_DEBUG);
    ASSERT_TRUE(log != NULL);
    neverc_syslog_close(log);
}

static void test_write_levels(void) {
    printf("[write levels]\n");
    neverc_syslog_t *log = neverc_syslog_open("neverc_test",
                                               NEVERC_SYSLOG_USER,
                                               NEVERC_SYSLOG_DEBUG);
    ASSERT_TRUE(log != NULL);

    ASSERT_EQ(neverc_syslog_info(log, "test info message"), 0);
    ASSERT_EQ(neverc_syslog_debug(log, "test debug message"), 0);
    ASSERT_EQ(neverc_syslog_warning(log, "test warning"), 0);
    ASSERT_EQ(neverc_syslog_err(log, "test error"), 0);
    ASSERT_EQ(neverc_syslog_notice(log, "test notice"), 0);

    neverc_syslog_close(log);
}

static void test_priority_filter(void) {
    printf("[priority filter]\n");
    neverc_syslog_t *log = neverc_syslog_open("neverc_test",
                                               NEVERC_SYSLOG_USER,
                                               NEVERC_SYSLOG_WARNING);
    ASSERT_TRUE(log != NULL);

    ASSERT_EQ(neverc_syslog_err(log, "should pass"), 0);
    ASSERT_EQ(neverc_syslog_warning(log, "should pass"), 0);
    /* INFO > WARNING priority number, so it should be filtered */
    ASSERT_EQ(neverc_syslog_info(log, "should be filtered"), -1);
    ASSERT_EQ(neverc_syslog_debug(log, "should be filtered"), -1);

    neverc_syslog_close(log);
}

static void test_pri_and_facilities(void) {
    printf("[PRI]\n");
    ASSERT_EQ(neverc_syslog_pri(NEVERC_SYSLOG_USER, NEVERC_SYSLOG_INFO),
              (1 << 3) | 6);
    ASSERT_EQ(neverc_syslog_pri(NEVERC_SYSLOG_LOCAL0, NEVERC_SYSLOG_DEBUG),
              (16 << 3) | 7);
    ASSERT_EQ(neverc_syslog_pri(NEVERC_SYSLOG_KERN, NEVERC_SYSLOG_EMERG), 0);
    ASSERT_EQ(neverc_syslog_pri(NEVERC_SYSLOG_AUTHPRIV, NEVERC_SYSLOG_ERR),
              (10 << 3) | 3);
    ASSERT_EQ(neverc_syslog_pri(NEVERC_SYSLOG_SYSLOG, NEVERC_SYSLOG_WARNING),
              (5 << 3) | 4);
    ASSERT_EQ(neverc_syslog_pri(NEVERC_SYSLOG_USER,
                                (neverc_syslog_priority_t)(-1)), -1);
    ASSERT_EQ(neverc_syslog_pri(NEVERC_SYSLOG_USER,
                                (neverc_syslog_priority_t)8), -1);
    ASSERT_EQ(neverc_syslog_pri(NEVERC_SYSLOG_LOCAL7, NEVERC_SYSLOG_DEBUG),
              (23 << 3) | 7);
    ASSERT_EQ(neverc_syslog_pri((neverc_syslog_facility_t)(24 << 3),
                                NEVERC_SYSLOG_INFO), -1);
    ASSERT_EQ(neverc_syslog_pri((neverc_syslog_facility_t)1,
                                NEVERC_SYSLOG_INFO), -1);
    ASSERT_EQ(neverc_syslog_pri((neverc_syslog_facility_t)(-8),
                                NEVERC_SYSLOG_INFO), -1);
    ASSERT_TRUE(neverc_syslog_open("x", (neverc_syslog_facility_t)1,
                                   NEVERC_SYSLOG_INFO) == NULL);
    ASSERT_TRUE(neverc_syslog_open("x", NEVERC_SYSLOG_USER,
                                   (neverc_syslog_priority_t)8) == NULL);
}

static void test_long_tag(void) {
    printf("[long tag]\n");
    char tag[200];
    memset(tag, 'a', sizeof(tag) - 1);
    tag[sizeof(tag) - 1] = '\0';
    neverc_syslog_t *log = neverc_syslog_open(tag, NEVERC_SYSLOG_DAEMON,
                                               NEVERC_SYSLOG_INFO);
    ASSERT_TRUE(log != NULL);
    ASSERT_EQ(neverc_syslog_info(log, "truncated tag still writes"), 0);
    ASSERT_EQ(neverc_syslog_debug(log, "filtered by min priority"), -1);
    neverc_syslog_close(log);
}

static void test_null_safety(void) {
    printf("[null safety]\n");
    neverc_syslog_t *log = neverc_syslog_open(NULL, NEVERC_SYSLOG_USER,
                                              NEVERC_SYSLOG_DEBUG);
    ASSERT_TRUE(log != NULL);
    ASSERT_EQ(neverc_syslog_write(NULL, NEVERC_SYSLOG_INFO, "test"), -1);
    ASSERT_EQ(neverc_syslog_write(log, (neverc_syslog_priority_t)(-1), "x"), -1);
    ASSERT_EQ(neverc_syslog_write(log, (neverc_syslog_priority_t)8, "x"), -1);
    neverc_syslog_close(log);
}

static void test_format_and_injection(void) {
    printf("[format/injection]\n");
    neverc_syslog_t *log = neverc_syslog_open("app\n<0>root",
                                               NEVERC_SYSLOG_USER,
                                               NEVERC_SYSLOG_DEBUG);
    ASSERT_TRUE(log != NULL);

    char buf[512];
    ASSERT_EQ(neverc_syslog_format(log, NEVERC_SYSLOG_INFO,
                                   "hello\n<13>forged", buf, sizeof(buf)), 0);
    ASSERT_TRUE(strncmp(buf, "<14>", 4) == 0);
    ASSERT_TRUE(strstr(buf, "[INFO]") == NULL);
    ASSERT_TRUE(strchr(buf, '\n') == NULL);
    ASSERT_TRUE(strchr(buf, '\r') == NULL);
    ASSERT_TRUE(strstr(buf, "hello <13>forged") != NULL);
    /* Tag "app\n<0>root" must not keep a second PRI or a ':' split. */
    {
        const char *colon = strchr(buf + 4, ':');
        int tag_ok = colon && colon[1] == ' ';
        const char *p;
        for (p = buf + 4; tag_ok && p < colon; p++) {
            if (*p == '<' || *p == '>' || *p == ':')
                tag_ok = 0;
        }
        ASSERT_TRUE(tag_ok);
    }
    ASSERT_EQ(neverc_syslog_info(log, "hello\n<13>forged"), 0);

    neverc_syslog_close(log);
    log = neverc_syslog_open("foo: bar<0>", NEVERC_SYSLOG_USER,
                             NEVERC_SYSLOG_DEBUG);
    ASSERT_TRUE(log != NULL);
    ASSERT_EQ(neverc_syslog_format(log, NEVERC_SYSLOG_INFO, "msg",
                                   buf, sizeof(buf)), 0);
    ASSERT_TRUE(strncmp(buf, "<14>", 4) == 0);
    ASSERT_TRUE(strstr(buf, "<0>") == NULL);
    {
        const char *colon = strchr(buf + 4, ':');
        ASSERT_TRUE(colon != NULL && colon[1] == ' ');
        ASSERT_TRUE(strcmp(colon + 2, "msg") == 0);
        ASSERT_TRUE(strchr(colon + 1, ':') == NULL);
    }
    neverc_syslog_close(log);

    log = neverc_syslog_open("my app", NEVERC_SYSLOG_USER,
                             NEVERC_SYSLOG_DEBUG);
    ASSERT_TRUE(log != NULL);
    ASSERT_EQ(neverc_syslog_format(log, NEVERC_SYSLOG_INFO, "hello",
                                   buf, sizeof(buf)), 0);
    ASSERT_TRUE(strncmp(buf, "<14>", 4) == 0);
    {
        const char *colon = strchr(buf + 4, ':');
        ASSERT_TRUE(colon != NULL && colon[1] == ' ');
        ASSERT_TRUE(strncmp(buf + 4, "my_app", (size_t)(colon - (buf + 4))) == 0);
        ASSERT_TRUE(strcmp(colon + 2, "hello") == 0);
    }
    neverc_syslog_close(log);

    log = neverc_syslog_open("ok", NEVERC_SYSLOG_USER, NEVERC_SYSLOG_DEBUG);
    ASSERT_TRUE(log != NULL);
    ASSERT_EQ(neverc_syslog_format(log, NEVERC_SYSLOG_INFO, "x",
                                   buf, 4), -1);

    /* Message is an operand of %s, not a format string. A leftover
     * syslog(pri, msg) / "%s: [%s] %s" registration would expand this. */
    ASSERT_EQ(neverc_syslog_format(log, NEVERC_SYSLOG_INFO, "100% done %s %n",
                                   buf, sizeof(buf)), 0);
    ASSERT_TRUE(strstr(buf, "100% done %s %n") != NULL);

    ASSERT_EQ(neverc_syslog_format(NULL, NEVERC_SYSLOG_INFO, "x",
                                   buf, sizeof(buf)), -1);
    ASSERT_EQ(neverc_syslog_format(log, NEVERC_SYSLOG_INFO, "x",
                                   NULL, 8), -1);
    neverc_syslog_close(log);
}

static void test_format_long_message_is_not_truncated(void) {
    printf("[format long message]\n");
    neverc_syslog_t *log = neverc_syslog_open(
        "long", NEVERC_SYSLOG_USER, NEVERC_SYSLOG_DEBUG);
    ASSERT_TRUE(log != NULL);
    if (!log) return;
    char message[3001];
    char formatted[4096];
    memset(message, 'x', sizeof(message) - 1U);
    message[sizeof(message) - 1U] = '\0';
    ASSERT_EQ(neverc_syslog_format(
                  log, NEVERC_SYSLOG_INFO, message,
                  formatted, sizeof(formatted)),
              0);
    const char *body = strstr(formatted, ": ");
    ASSERT_TRUE(body != NULL);
    if (body) {
        body += 2;
        ASSERT_TRUE(strlen(body) == sizeof(message) - 1U);
        ASSERT_TRUE(memcmp(body, message, sizeof(message)) == 0);
    }
    neverc_syslog_close(log);
}

int main(void) {
    printf("=== NeverC log/syslog Tests ===\n");
    test_open_close();
    test_write_levels();
    test_priority_filter();
    test_pri_and_facilities();
    test_long_tag();
    test_null_safety();
    test_format_and_injection();
    test_format_long_message_is_not_truncated();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
