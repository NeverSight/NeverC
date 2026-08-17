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

int main(void) {
    printf("=== NeverC log/syslog Tests ===\n");
    test_open_close();
    test_write_levels();
    test_priority_filter();
    test_pri_and_facilities();
    test_long_tag();
    test_null_safety();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
