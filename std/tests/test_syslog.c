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

static void test_null_safety(void) {
    printf("[null safety]\n");
    ASSERT_TRUE(neverc_syslog_open(NULL, NEVERC_SYSLOG_USER, NEVERC_SYSLOG_DEBUG) != NULL);
    ASSERT_EQ(neverc_syslog_write(NULL, NEVERC_SYSLOG_INFO, "test"), -1);
}

int main(void) {
    printf("=== NeverC log/syslog Tests ===\n");
    test_open_close();
    test_write_levels();
    test_priority_filter();
    test_null_safety();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
