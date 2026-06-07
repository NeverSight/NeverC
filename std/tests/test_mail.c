#include "neverc/net/mail.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define ASSERT_EQ(a, b) do { int _a=(a), _b=(b); tests_run++; \
    if (_a==_b) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL [%d]: %s = %d, expected %d\n", __LINE__, #a, _a, _b); } \
} while(0)

#define ASSERT_STREQ(a, b) do { tests_run++; \
    if (a && b && strcmp(a,b)==0) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL [%d]: got \"%s\", expected \"%s\"\n", __LINE__, a?a:"(null)", b); } \
} while(0)

#define ASSERT_TRUE(e) do { tests_run++; if(e){tests_passed++;}else{tests_failed++;printf("  FAIL [%d]: %s\n",__LINE__,#e);}}while(0)

static void test_parse_address(void) {
    printf("[parse address]\n");
    neverc_mail_address_t addr;

    ASSERT_EQ(neverc_mail_parse_address("user@example.com", &addr), 0);
    ASSERT_STREQ(addr.address, "user@example.com");
    ASSERT_STREQ(addr.name, "");

    ASSERT_EQ(neverc_mail_parse_address("John Doe <john@example.com>", &addr), 0);
    ASSERT_STREQ(addr.address, "john@example.com");
    ASSERT_STREQ(addr.name, "John Doe");

    ASSERT_EQ(neverc_mail_parse_address("\"Doe, John\" <john@example.com>", &addr), 0);
    ASSERT_STREQ(addr.address, "john@example.com");
    ASSERT_STREQ(addr.name, "Doe, John");
}

static void test_parse_address_list(void) {
    printf("[parse address list]\n");
    neverc_mail_address_t addrs[8];
    int n = neverc_mail_parse_address_list(
        "Alice <alice@x.com>, bob@y.com, Charlie <charlie@z.com>",
        addrs, 8);
    ASSERT_EQ(n, 3);
    ASSERT_STREQ(addrs[0].name, "Alice");
    ASSERT_STREQ(addrs[0].address, "alice@x.com");
    ASSERT_STREQ(addrs[1].address, "bob@y.com");
    ASSERT_STREQ(addrs[2].name, "Charlie");
}

static void test_format_address(void) {
    printf("[format address]\n");
    neverc_mail_address_t addr;
    char buf[256];

    strcpy(addr.name, "John");
    strcpy(addr.address, "john@example.com");
    neverc_mail_format_address(&addr, buf, sizeof(buf));
    ASSERT_STREQ(buf, "John <john@example.com>");

    addr.name[0] = '\0';
    neverc_mail_format_address(&addr, buf, sizeof(buf));
    ASSERT_STREQ(buf, "john@example.com");
}

static void test_parse_message(void) {
    printf("[parse message]\n");
    const char *msg =
        "From: sender@example.com\r\n"
        "To: receiver@example.com\r\n"
        "Subject: Test\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "Hello, World!\r\n";

    neverc_mail_message_t m;
    ASSERT_EQ(neverc_mail_parse_message(msg, strlen(msg), &m), 0);
    ASSERT_EQ(m.header_count, 4);
    ASSERT_STREQ(neverc_mail_header_get(&m, "From"), "sender@example.com");
    ASSERT_STREQ(neverc_mail_header_get(&m, "to"), "receiver@example.com");
    ASSERT_STREQ(neverc_mail_header_get(&m, "Subject"), "Test");
    ASSERT_STREQ(neverc_mail_header_get(&m, "content-type"), "text/plain");
    ASSERT_TRUE(m.body_len == 15);
    ASSERT_TRUE(memcmp(m.body, "Hello, World!\r\n", 15) == 0);
}

static void test_parse_date(void) {
    printf("[parse date]\n");
    /* Mon, 02 Jan 2006 15:04:05 -0700 */
    long long t = neverc_mail_parse_date("Mon, 02 Jan 2006 15:04:05 -0700");
    ASSERT_TRUE(t > 0);

    long long t2 = neverc_mail_parse_date("01 Jan 1970 00:00:00 +0000");
    ASSERT_EQ((int)t2, 0);
}

int main(void) {
    printf("=== NeverC net/mail Tests ===\n");
    test_parse_address();
    test_parse_address_list();
    test_format_address();
    test_parse_message();
    test_parse_date();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
