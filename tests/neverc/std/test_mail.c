#include "neverc/std/net/mail.h"
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

    ASSERT_EQ(neverc_mail_parse_address("<user@example.com>", &addr), 0);
    ASSERT_STREQ(addr.address, "user@example.com");
    ASSERT_STREQ(addr.name, "");
    ASSERT_EQ(neverc_mail_parse_address("John.Doe <john@example.com>", &addr),
              0);
    ASSERT_STREQ(addr.name, "John.Doe");
    ASSERT_STREQ(addr.address, "john@example.com");
    ASSERT_EQ(neverc_mail_parse_address("user@127.0.0.1", &addr), 0);
    ASSERT_STREQ(addr.address, "user@127.0.0.1");
    ASSERT_EQ(neverc_mail_parse_address("user@exam ple.com", &addr), -1);
    ASSERT_EQ(neverc_mail_parse_address("undisclosed-recipients:;", &addr),
              -1);
    ASSERT_EQ(neverc_mail_parse_address("\"quoted\"@example.com", &addr), -1);

    ASSERT_EQ(neverc_mail_parse_address("\"Doe, John\" <john@example.com>", &addr), 0);
    ASSERT_STREQ(addr.address, "john@example.com");
    ASSERT_STREQ(addr.name, "Doe, John");

    ASSERT_EQ(neverc_mail_parse_address("user@x.com\r\nBcc: evil@x.com", &addr),
              -1);
    ASSERT_EQ(neverc_mail_parse_address("Name <user@x.com>\r\nBcc: evil", &addr),
              -1);
    ASSERT_EQ(neverc_mail_parse_address("John <a@b.com> <evil@x.com>", &addr),
              -1);
    ASSERT_EQ(neverc_mail_parse_address("John <a@b.com> trailing", &addr), -1);
    ASSERT_EQ(neverc_mail_parse_address("<>", &addr), -1);
    ASSERT_EQ(neverc_mail_parse_address("nodomain", &addr), -1);
    ASSERT_EQ(neverc_mail_parse_address("user@", &addr), -1);
    ASSERT_EQ(neverc_mail_parse_address("@example.com", &addr), -1);
    ASSERT_EQ(neverc_mail_parse_address("user@@example.com", &addr), -1);
    ASSERT_EQ(neverc_mail_parse_address("user@.example.com", &addr), -1);
    ASSERT_EQ(neverc_mail_parse_address("user@example.com.", &addr), -1);
    ASSERT_EQ(neverc_mail_parse_address("John Doe", &addr), -1);

    ASSERT_EQ(neverc_mail_parse_address("x@[garbage]", &addr), -1);
    ASSERT_EQ(neverc_mail_parse_address("x@[192.168.1.1]", &addr), 0);
    ASSERT_STREQ(addr.address, "x@[192.168.1.1]");
    ASSERT_EQ(neverc_mail_parse_address("x@[::1]", &addr), 0);
    ASSERT_STREQ(addr.address, "x@[::1]");
    ASSERT_EQ(neverc_mail_parse_address("x@[IPv6:::1]", &addr), 0);
    ASSERT_EQ(neverc_mail_parse_address("x@[IPv6:::ffff:127.0.0.1]", &addr),
              0);
    ASSERT_EQ(neverc_mail_parse_address("x@[IPv6:127.0.0.1]", &addr), -1);
    ASSERT_EQ(neverc_mail_parse_address("Bcc:hidden@x.com", &addr), -1);
    ASSERT_EQ(neverc_mail_parse_address("Bcc: hidden <user@x.com>", &addr),
              -1);
    ASSERT_EQ(neverc_mail_parse_address("Doe, John <j@x.com>", &addr), -1);
    ASSERT_EQ(neverc_mail_parse_address("user(comment)@x.com", &addr), -1);
    ASSERT_EQ(neverc_mail_parse_address("\"foo\"bar\" <j@x.com>", &addr), -1);
    ASSERT_EQ(neverc_mail_parse_address("\"Bcc: hidden\" <user@x.com>", &addr),
              0);
    ASSERT_STREQ(addr.name, "Bcc: hidden");
    ASSERT_STREQ(addr.address, "user@x.com");

    /* Quoted display-name may contain unquoted-looking angle mailboxes. */
    ASSERT_EQ(neverc_mail_parse_address("\"foo <bar>\" <user@x.com>", &addr),
              0);
    ASSERT_STREQ(addr.name, "foo <bar>");
    ASSERT_STREQ(addr.address, "user@x.com");
    ASSERT_EQ(neverc_mail_parse_address("\"foo\\\"bar\" <user@x.com>", &addr),
              0);
    ASSERT_STREQ(addr.name, "foo\"bar");
    ASSERT_STREQ(addr.address, "user@x.com");
    ASSERT_EQ(neverc_mail_parse_address("\"foo <bar>\"", &addr), -1);

    ASSERT_EQ(neverc_mail_parse_address(
                  "=?utf-8?q?=0D=0ABcc:_hidden?= <user@x.com>", &addr),
              -1);
    ASSERT_EQ(neverc_mail_parse_address(
                  "=?utf-8?b?DQpCY2M6IGhpZGRlbg==?= <user@x.com>", &addr),
              -1);
    ASSERT_EQ(neverc_mail_parse_address(
                  "=?utf-8?q?=0D=0ABcc:_x?=@x.com", &addr),
              -1);
    ASSERT_EQ(neverc_mail_parse_address(
                  "=?utf-8?q?Hello?= <user@x.com>", &addr),
              0);
    ASSERT_STREQ(addr.name, "=?utf-8?q?Hello?=");
    ASSERT_STREQ(addr.address, "user@x.com");
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

    n = neverc_mail_parse_address_list(
        "\"Doe, John\" <john@example.com>, other@example.com", addrs, 8);
    ASSERT_EQ(n, 2);
    ASSERT_STREQ(addrs[0].name, "Doe, John");
    ASSERT_STREQ(addrs[0].address, "john@example.com");
    ASSERT_STREQ(addrs[1].address, "other@example.com");

    ASSERT_EQ(neverc_mail_parse_address_list(
                  "ok@x.com, bad\r\nBcc: hidden@x.com", addrs, 8), -1);

    neverc_mail_address_t one[1];
    ASSERT_EQ(neverc_mail_parse_address_list("a@x.com, b@y.com", one, 1), -1);
    ASSERT_EQ(neverc_mail_parse_address_list("a@x.com", one, 1), 1);
    ASSERT_EQ(neverc_mail_parse_address_list("", one, 1), -1);
    ASSERT_EQ(neverc_mail_parse_address_list("   ", one, 1), -1);
    ASSERT_EQ(neverc_mail_parse_address_list("a@x.com,", one, 1), -1);
    ASSERT_EQ(neverc_mail_parse_address_list("Doe, John <j@x.com>", addrs, 8),
              -1);
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

    strcpy(addr.name, "Doe, John");
    strcpy(addr.address, "j@x.com");
    neverc_mail_format_address(&addr, buf, sizeof(buf));
    ASSERT_STREQ(buf, "\"Doe, John\" <j@x.com>");
    neverc_mail_address_t parsed[4];
    ASSERT_EQ(neverc_mail_parse_address_list(buf, parsed, 4), 1);

    strcpy(addr.name, "Eve");
    strcpy(addr.address, "eve@x.com\r\nBcc: hidden@x.com");
    ASSERT_EQ(neverc_mail_format_address(&addr, buf, sizeof(buf)), -1);

    strcpy(addr.name, "Bcc:hidden");
    strcpy(addr.address, "user@x.com");
    ASSERT_EQ(neverc_mail_format_address(&addr, buf, sizeof(buf)),
              (int)strlen("\"Bcc:hidden\" <user@x.com>"));
    ASSERT_STREQ(buf, "\"Bcc:hidden\" <user@x.com>");
    neverc_mail_address_t roundtrip;
    ASSERT_EQ(neverc_mail_parse_address(buf, &roundtrip), 0);
    ASSERT_STREQ(roundtrip.name, "Bcc:hidden");
    ASSERT_STREQ(roundtrip.address, "user@x.com");

    strcpy(addr.name, "foo <bar>");
    strcpy(addr.address, "user@x.com");
    ASSERT_EQ(neverc_mail_format_address(&addr, buf, sizeof(buf)),
              (int)strlen("\"foo <bar>\" <user@x.com>"));
    ASSERT_STREQ(buf, "\"foo <bar>\" <user@x.com>");
    ASSERT_EQ(neverc_mail_parse_address(buf, &roundtrip), 0);
    ASSERT_STREQ(roundtrip.name, "foo <bar>");
    ASSERT_STREQ(roundtrip.address, "user@x.com");

    strcpy(addr.name, "");
    strcpy(addr.address, "Bcc:hidden@x.com");
    ASSERT_EQ(neverc_mail_format_address(&addr, buf, sizeof(buf)), -1);

    strcpy(addr.name, "John");
    strcpy(addr.address, "john@example.com");
    ASSERT_EQ(neverc_mail_format_address(&addr, buf, 8), -1);
    strcpy(addr.name, "");
    strcpy(addr.address, "not-an-addr");
    ASSERT_EQ(neverc_mail_format_address(&addr, buf, sizeof(buf)), -1);

    strcpy(addr.name, "=?utf-8?q?=0D=0ABcc:_hidden?=");
    strcpy(addr.address, "user@x.com");
    ASSERT_EQ(neverc_mail_format_address(&addr, buf, sizeof(buf)), -1);
    strcpy(addr.name, "");
    strcpy(addr.address, "=?utf-8?q?=0D=0ABcc:_x?=@x.com");
    ASSERT_EQ(neverc_mail_format_address(&addr, buf, sizeof(buf)), -1);

    strcpy(addr.name, "=?utf-8?q?Hello?=");
    strcpy(addr.address, "user@x.com");
    ASSERT_EQ(neverc_mail_format_address(&addr, buf, sizeof(buf)),
              (int)strlen("=?utf-8?q?Hello?= <user@x.com>"));
    ASSERT_STREQ(buf, "=?utf-8?q?Hello?= <user@x.com>");
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

    ASSERT_EQ(neverc_mail_parse_message("", 0, &m), -1);
    const char *no_blank = "From: a@b.com\r\nTo: c@d.com";
    ASSERT_EQ(neverc_mail_parse_message(no_blank, strlen(no_blank), &m), -1);
    const char *no_blank_crlf = "From: a@b.com\r\n";
    ASSERT_EQ(neverc_mail_parse_message(no_blank_crlf, strlen(no_blank_crlf),
                                       &m),
              -1);

    const char *empty_name = ": empty-name\r\n\r\n";
    ASSERT_EQ(neverc_mail_parse_message(empty_name, strlen(empty_name), &m),
              -1);
    const char *ctl_value = "From: a\x01" "b\r\n\r\n";
    ASSERT_EQ(neverc_mail_parse_message(ctl_value, strlen(ctl_value), &m), -1);
    const char *no_colon = "NotAHeader\r\n\r\nbody";
    ASSERT_EQ(neverc_mail_parse_message(no_colon, strlen(no_colon), &m), -1);

    char long_key[160];
    memset(long_key, 'A', 128);
    memcpy(long_key + 128, ": v\r\n\r\n", 7);
    ASSERT_EQ(neverc_mail_parse_message(long_key, 135, &m), -1);

    char many[2048];
    size_t n = 0;
    for (int i = 0; i < NEVERC_MAIL_MAX_HEADERS + 1; i++) {
        int wrote = snprintf(many + n, sizeof(many) - n, "X%d: y\r\n", i);
        ASSERT_TRUE(wrote > 0);
        n += (size_t)wrote;
    }
    n += (size_t)snprintf(many + n, sizeof(many) - n, "\r\n");
    ASSERT_EQ(neverc_mail_parse_message(many, n, &m), -1);

    /* Bare CR is not a line break. Treating it as one smuggles Bcc. */
    const char *bare_cr = "From: user@x.com\rBcc: hidden@x.com\r\n\r\n";
    ASSERT_EQ(neverc_mail_parse_message(bare_cr, strlen(bare_cr), &m), -1);
    const char *bare_cr_fold = "From: user@x.com\r Bcc: hidden@x.com\r\n\r\n";
    ASSERT_EQ(neverc_mail_parse_message(bare_cr_fold, strlen(bare_cr_fold), &m),
              -1);

    const char *lf_only = "From: a@b.com\nTo: c@d.com\n\nbody";
    ASSERT_EQ(neverc_mail_parse_message(lf_only, strlen(lf_only), &m), 0);
    ASSERT_STREQ(neverc_mail_header_get(&m, "From"), "a@b.com");
    ASSERT_STREQ(neverc_mail_header_get(&m, "To"), "c@d.com");
    ASSERT_TRUE(m.body_len == 4);
    ASSERT_TRUE(memcmp(m.body, "body", 4) == 0);

    const char *folded =
        "Subject: Hello\r\n"
        " World\r\n"
        "\tAgain\r\n"
        "From: a@b.com\r\n"
        "\r\n"
        "ok";
    ASSERT_EQ(neverc_mail_parse_message(folded, strlen(folded), &m), 0);
    ASSERT_STREQ(neverc_mail_header_get(&m, "Subject"), "Hello World Again");
    ASSERT_STREQ(neverc_mail_header_get(&m, "From"), "a@b.com");
    ASSERT_TRUE(m.body_len == 2);
    ASSERT_TRUE(memcmp(m.body, "ok", 2) == 0);

    const char *fold_first =
        "Subject:\r\n"
        " Hello\r\n"
        "\r\n";
    ASSERT_EQ(neverc_mail_parse_message(fold_first, strlen(fold_first), &m), 0);
    ASSERT_STREQ(neverc_mail_header_get(&m, "Subject"), "Hello");

    /* Folded "Bcc:" is continuation of From, not a new field. */
    const char *fold_bcc =
        "From: user@x.com\r\n"
        " Bcc: hidden@x.com\r\n"
        "To: visible@x.com\r\n"
        "\r\n"
        "body";
    ASSERT_EQ(neverc_mail_parse_message(fold_bcc, strlen(fold_bcc), &m), 0);
    ASSERT_TRUE(neverc_mail_header_get(&m, "Bcc") == NULL);
    ASSERT_STREQ(neverc_mail_header_get(&m, "From"),
                 "user@x.com Bcc: hidden@x.com");
    ASSERT_STREQ(neverc_mail_header_get(&m, "To"), "visible@x.com");

    const char *fold_tab_bcc =
        "From: user@x.com\r\n"
        "\tBcc: hidden@x.com\r\n"
        "\r\n";
    ASSERT_EQ(neverc_mail_parse_message(fold_tab_bcc, strlen(fold_tab_bcc), &m),
              0);
    ASSERT_TRUE(neverc_mail_header_get(&m, "Bcc") == NULL);
    ASSERT_STREQ(neverc_mail_header_get(&m, "From"),
                 "user@x.com Bcc: hidden@x.com");

    /* A Bcc line that does not start with WSP is a real field, not a fold. */
    const char *real_bcc =
        "From: user@x.com\r\n"
        "Bcc: hidden@x.com\r\n"
        "\r\n";
    ASSERT_EQ(neverc_mail_parse_message(real_bcc, strlen(real_bcc), &m), 0);
    ASSERT_STREQ(neverc_mail_header_get(&m, "Bcc"), "hidden@x.com");

    /* After the blank line, "Bcc:" is body, not a header. */
    const char *bcc_in_body =
        "From: user@x.com\r\n"
        "\r\n"
        "Bcc: hidden@x.com\r\n";
    ASSERT_EQ(neverc_mail_parse_message(bcc_in_body, strlen(bcc_in_body), &m),
              0);
    ASSERT_TRUE(neverc_mail_header_get(&m, "Bcc") == NULL);
    ASSERT_TRUE(m.body_len >= 4 && memcmp(m.body, "Bcc:", 4) == 0);

    const char *tab_value = "Subject: Hello\tWorld\r\n\r\n";
    ASSERT_EQ(neverc_mail_parse_message(tab_value, strlen(tab_value), &m), 0);
    ASSERT_STREQ(neverc_mail_header_get(&m, "Subject"), "Hello\tWorld");

    const char *lead_ws = " From: user@x.com\r\n\r\n";
    ASSERT_EQ(neverc_mail_parse_message(lead_ws, strlen(lead_ws), &m), -1);

    const char *cr_before_colon = "From\rBcc: hidden@x.com\r\n\r\n";
    ASSERT_EQ(neverc_mail_parse_message(cr_before_colon, strlen(cr_before_colon),
                                       &m),
              -1);

    char nul_hdr[] = "From: a@b.com\0Bcc: hidden@x.com\r\n\r\n";
    ASSERT_EQ(neverc_mail_parse_message(nul_hdr, sizeof(nul_hdr) - 1, &m), -1);

    const char *ew_inject =
        "From: =?utf-8?q?=0D=0ABcc:_hidden@x.com?=\r\n"
        "\r\n";
    ASSERT_EQ(neverc_mail_parse_message(ew_inject, strlen(ew_inject), &m), -1);
}

static void test_parse_date(void) {
    printf("[parse date]\n");
    /* Mon, 02 Jan 2006 15:04:05 -0700 */
    long long t = neverc_mail_parse_date("Mon, 02 Jan 2006 15:04:05 -0700");
    ASSERT_TRUE(t == 1136239445LL);

    long long t2 = neverc_mail_parse_date("01 Jan 1970 00:00:00 +0000");
    ASSERT_EQ((int)t2, 0);

    ASSERT_TRUE(neverc_mail_parse_date("01 Jan 1970 25:00:00 +0000") == -1);
    ASSERT_TRUE(neverc_mail_parse_date("31 Feb 2024 00:00:00 +0000") == -1);

    /* RFC 5322: seconds are optional; zone is required. */
    ASSERT_TRUE(neverc_mail_parse_date("01 Jan 1970 00:00 +0000") == 0);
    ASSERT_TRUE(neverc_mail_parse_date("01 Jan 1970 00:00 -0700") == 25200LL);
    ASSERT_TRUE(neverc_mail_parse_date("01 Jan 1970 00:00:00") == -1);
    ASSERT_TRUE(neverc_mail_parse_date("02 January 2006 15:04:05 -0700") == -1);
    ASSERT_TRUE(neverc_mail_parse_date("02 Jan 2006 15:04:05 -0700 extra") ==
                -1);
    ASSERT_TRUE(neverc_mail_parse_date("01 Jan 70 00:00:00 +0000") == 0);
    ASSERT_TRUE(neverc_mail_parse_date("01 Jan 1970 00:00:00 EST") == 18000LL);
    ASSERT_TRUE(neverc_mail_parse_date("01 jan 1970 00:00:00 +0000") == 0);
    ASSERT_TRUE(neverc_mail_parse_date("01 JAN 1970 00:00:00 +0000") == 0);
    ASSERT_TRUE(neverc_mail_parse_date("mon, 01 Jan 1970 00:00:00 +0000") == 0);
    ASSERT_TRUE(neverc_mail_parse_date("01 Jan 1970 00:00:00 est") == 18000LL);
    ASSERT_TRUE(neverc_mail_parse_date(
                    "01 Jan 1970 00:00:00 -0700 (MST)") == 25200LL);
    ASSERT_TRUE(neverc_mail_parse_date("Xxx, 01 Jan 1970 00:00:00 +0000") ==
                -1);
    ASSERT_TRUE(neverc_mail_parse_date("") == -1);

    /* RFC 5322 FWS: CRLF must be followed by WSP. Bare CRLF is a new line. */
    ASSERT_TRUE(neverc_mail_parse_date("01 Jan 1970\r\n 00:00:00 +0000") == 0);
    ASSERT_TRUE(neverc_mail_parse_date("01 Jan 1970\n 00:00:00 +0000") == 0);
    ASSERT_TRUE(neverc_mail_parse_date("01 Jan 1970\r\n00:00:00 +0000") == -1);
    ASSERT_TRUE(neverc_mail_parse_date("01 Jan 1970\r 00:00:00 +0000") == -1);
    ASSERT_TRUE(neverc_mail_parse_date(
                    "01 Jan 1970 00:00:00 +0000\r\nBcc: hidden@x.com") == -1);
    ASSERT_TRUE(neverc_mail_parse_date(
                    "01 Jan 1970 00:00:00 +0000 (\r\nBcc: hidden)") == -1);
    ASSERT_TRUE(neverc_mail_parse_date(
                    "01 Jan 1970 00:00:00 +0000 (\r comment)") == -1);
    ASSERT_TRUE(neverc_mail_parse_date(
                    "01 Jan 1970 00:00:00 +0000 (\r\n comment)") == 0);
}

int main(void) {
    printf("=== NeverC net/mail Tests ===\n");
    test_parse_address();
    test_parse_address_list();
    test_format_address();
    test_parse_message();
    test_parse_date();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
