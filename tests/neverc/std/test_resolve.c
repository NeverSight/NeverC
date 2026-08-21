#include "neverc/std/net/resolve.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifndef _WIN32
#include <pthread.h>
#include <unistd.h>
#endif

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}

static void check_str(const char *name, const char *got, const char *expected) {
    tests_run++;
    if (got && expected && strcmp(got, expected) == 0) tests_passed++;
    else {
        tests_failed++;
        printf("  FAIL: %s: got \"%s\", expected \"%s\"\n", name,
               got ? got : "NULL", expected ? expected : "NULL");
    }
}

static void check_true(const char *name, int cond) {
    tests_run++;
    if (cond) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s\n", name); }
}

/* ===== SplitHostPort ===== */

static void test_split_host_port(void) {
    printf("[split_host_port]\n");
    char host[128], port[32];

    check_int("split host:port", neverc_net_split_host_port("127.0.0.1:8080", host, sizeof(host), port, sizeof(port)), 0);
    check_str("host", host, "127.0.0.1");
    check_str("port", port, "8080");

    check_int("split localhost:80", neverc_net_split_host_port("localhost:80", host, sizeof(host), port, sizeof(port)), 0);
    check_str("host localhost", host, "localhost");
    check_str("port 80", port, "80");

    check_int("split [::1]:443", neverc_net_split_host_port("[::1]:443", host, sizeof(host), port, sizeof(port)), 0);
    check_str("ipv6 host", host, "::1");
    check_str("ipv6 port", port, "443");

    check_int("split [2001:db8::1]:8080", neverc_net_split_host_port("[2001:db8::1]:8080", host, sizeof(host), port, sizeof(port)), 0);
    check_str("ipv6 full host", host, "2001:db8::1");
    check_str("ipv6 full port", port, "8080");

    check_int("split [fe80::1%eth0]:80",
              neverc_net_split_host_port("[fe80::1%eth0]:80", host,
                                         sizeof(host), port, sizeof(port)), 0);
    check_str("ipv6 zone host", host, "fe80::1%eth0");
    check_str("ipv6 zone port", port, "80");

    /* Error cases */
    check_int("no port", neverc_net_split_host_port("localhost", host, sizeof(host), port, sizeof(port)), -1);
    check_int("null input", neverc_net_split_host_port(NULL, host, sizeof(host), port, sizeof(port)), -1);

    /* Just :port */
    check_int("split :8080", neverc_net_split_host_port(":8080", host, sizeof(host), port, sizeof(port)), 0);
    check_str("empty host", host, "");
    check_str("port only", port, "8080");

    char tiny_host[4];
    char tiny_port[16];
    check_int("host overflow rejected",
              neverc_net_split_host_port("192.168.1.1:80", tiny_host,
                                         sizeof(tiny_host), tiny_port,
                                         sizeof(tiny_port)), -1);
    check_int("non-numeric port rejected",
              neverc_net_split_host_port("localhost:abc", host, sizeof(host),
                                         port, sizeof(port)), -1);
    check_int("port overflow rejected",
              neverc_net_split_host_port("localhost:65536", host, sizeof(host),
                                         port, sizeof(port)), -1);
    check_int("ipv6 missing port rejected",
              neverc_net_split_host_port("[::1]", host, sizeof(host),
                                         port, sizeof(port)), -1);
    check_int("ipv6 empty port rejected",
              neverc_net_split_host_port("[::1]:", host, sizeof(host),
                                         port, sizeof(port)), -1);
    check_int("ipv6 port overflow rejected",
              neverc_net_split_host_port("[::1]:65536", host, sizeof(host),
                                         port, sizeof(port)), -1);
    check_int("signed port rejected",
              neverc_net_split_host_port("localhost:+80", host, sizeof(host),
                                         port, sizeof(port)), -1);
    check_int("empty hostport rejected",
              neverc_net_split_host_port("", host, sizeof(host),
                                         port, sizeof(port)), -1);
    check_int("empty ipv6 zone rejected",
              neverc_net_split_host_port("[fe80::1%]:80", host, sizeof(host),
                                         port, sizeof(port)), -1);
    check_int("split rejects CTL host",
              neverc_net_split_host_port("host\nname:80", host, sizeof(host),
                                         port, sizeof(port)), -1);
    check_int("split ipv4-mapped",
              neverc_net_split_host_port("[::ffff:127.0.0.1]:80", host,
                                         sizeof(host), port, sizeof(port)),
              0);
    check_str("ipv4-mapped host", host, "::ffff:127.0.0.1");
    check_str("ipv4-mapped port", port, "80");

    /* Go net.SplitHostPort does not treat SP (0x20) as CTL (0x00-0x1F, 0x7F).
     * Windows IPv6 zones such as "Ethernet 2" must round-trip. */
    check_int("split allows space in host",
              neverc_net_split_host_port("host name:80", host, sizeof(host),
                                         port, sizeof(port)), 0);
    check_str("space host", host, "host name");
    check_str("space port", port, "80");
    check_int("split allows space in ipv6 zone",
              neverc_net_split_host_port("[fe80::1%Ethernet 2]:80", host,
                                         sizeof(host), port, sizeof(port)),
              0);
    check_str("space zone host", host, "fe80::1%Ethernet 2");
    check_str("space zone port", port, "80");
    check_int("split still rejects tab host",
              neverc_net_split_host_port("host\tname:80", host, sizeof(host),
                                         port, sizeof(port)), -1);
    check_int("split still rejects CR host",
              neverc_net_split_host_port("host\rname:80", host, sizeof(host),
                                         port, sizeof(port)), -1);
    check_int("split still rejects DEL host",
              neverc_net_split_host_port("host\x7fname:80", host, sizeof(host),
                                         port, sizeof(port)), -1);
    check_int("split rejects extra brackets",
              neverc_net_split_host_port("foo[bar]:80", host, sizeof(host),
                                         port, sizeof(port)), -1);
    check_int("split rejects stray close bracket",
              neverc_net_split_host_port("]:80", host, sizeof(host),
                                         port, sizeof(port)), -1);
}

/* ===== JoinHostPort ===== */

static void test_join_host_port(void) {
    printf("[join_host_port]\n");
    char buf[128];

    check_true("join ipv4", neverc_net_join_host_port("127.0.0.1", "8080", buf, sizeof(buf)) > 0);
    check_str("ipv4 result", buf, "127.0.0.1:8080");

    check_true("join hostname", neverc_net_join_host_port("localhost", "80", buf, sizeof(buf)) > 0);
    check_str("hostname result", buf, "localhost:80");

    check_true("join ipv6", neverc_net_join_host_port("::1", "443", buf, sizeof(buf)) > 0);
    check_str("ipv6 result", buf, "[::1]:443");

    check_true("join ipv6 full", neverc_net_join_host_port("2001:db8::1", "8080", buf, sizeof(buf)) > 0);
    check_str("ipv6 full result", buf, "[2001:db8::1]:8080");

    check_true("join ipv6 zone",
               neverc_net_join_host_port("fe80::1%eth0", "80", buf,
                                         sizeof(buf)) > 0);
    check_str("ipv6 zone result", buf, "[fe80::1%eth0]:80");

    check_int("join rejects empty port",
              neverc_net_join_host_port("localhost", "", buf, sizeof(buf)),
              -1);
    check_int("join rejects overflow port",
              neverc_net_join_host_port("localhost", "65536", buf,
                                        sizeof(buf)), -1);
    check_int("join rejects signed port",
              neverc_net_join_host_port("localhost", "+80", buf, sizeof(buf)),
              -1);
    check_int("join rejects CTL host",
              neverc_net_join_host_port("host\nname", "80", buf, sizeof(buf)),
              -1);
    check_true("join ipv4-mapped",
               neverc_net_join_host_port("::ffff:127.0.0.1", "80", buf,
                                         sizeof(buf)) > 0);
    check_str("ipv4-mapped join", buf, "[::ffff:127.0.0.1]:80");
    check_true("join windows ipv6 zone with space",
               neverc_net_join_host_port("fe80::1%Ethernet 2", "80", buf,
                                         sizeof(buf)) > 0);
    check_str("windows zone join", buf, "[fe80::1%Ethernet 2]:80");
    check_int("join still rejects tab host",
              neverc_net_join_host_port("host\tname", "80", buf, sizeof(buf)),
              -1);
}

static void test_addr_internal(void) {
    printf("[addr_internal]\n");
    neverc_net_addrs_t addrs;

    check_int("loopback ipv4 internal",
              neverc_net_addr_is_internal("127.0.0.1"), 1);
    check_int("loopback ipv6 internal",
              neverc_net_addr_is_internal("::1"), 1);
    check_int("mapped loopback internal",
              neverc_net_addr_is_internal("::ffff:127.0.0.1"), 1);
    check_int("private ipv4 internal",
              neverc_net_addr_is_internal("10.0.0.1"), 1);
    check_int("link-local internal",
              neverc_net_addr_is_internal("169.254.1.1"), 1);
    check_int("localhost name internal",
              neverc_net_addr_is_internal("localhost"), 1);
    check_int("localhost suffix internal",
              neverc_net_addr_is_internal("foo.localhost"), 1);
    check_int("public ipv4 not internal",
              neverc_net_addr_is_internal("8.8.8.8"), 0);
    check_int("mapped public not internal",
              neverc_net_addr_is_internal("::ffff:8.8.8.8"), 0);
    check_int("empty addr fail-closed",
              neverc_net_addr_is_internal(""), 1);
    check_int("garbage addr fail-closed",
              neverc_net_addr_is_internal("not-an-ip"), 1);

    memset(&addrs, 0, sizeof(addrs));
    check_int("empty addrs fail-closed",
              neverc_net_addrs_any_internal(&addrs), 1);
    strcpy(addrs.addrs[0], "8.8.8.8");
    addrs.count = 1;
    check_int("public only not internal",
              neverc_net_addrs_any_internal(&addrs), 0);
    strcpy(addrs.addrs[1], "::ffff:127.0.0.1");
    addrs.count = 2;
    check_int("mapped loopback in set",
              neverc_net_addrs_any_internal(&addrs), 1);
}

/* ===== LookupHost ===== */

static void test_lookup_host(void) {
    printf("[lookup_host]\n");
    neverc_net_addrs_t addrs;

    memset(&addrs, 0x41, sizeof(addrs));
    addrs.count = 7;
    check_int("empty host rejected", neverc_net_lookup_host("", &addrs), -1);
    check_int("empty host clears leftover addrs", addrs.count, 0);
    addrs.count = 7;
    check_int("null host rejected", neverc_net_lookup_host(NULL, &addrs), -1);
    check_int("null host clears leftover addrs", addrs.count, 0);

    /* Resolve localhost — should always work */
    int rc = neverc_net_lookup_host("localhost", &addrs);
    check_int("lookup localhost", rc, 0);
    check_true("localhost has addrs", addrs.count > 0);

    if (addrs.count > 0) {
        int found_127 = 0;
        int found_v6_lo = 0;
        for (int i = 0; i < addrs.count; i++) {
            if (strcmp(addrs.addrs[i], "127.0.0.1") == 0) found_127 = 1;
            if (strcmp(addrs.addrs[i], "::1") == 0) found_v6_lo = 1;
        }
        check_true("localhost resolves to 127.0.0.1 or ::1", found_127 || found_v6_lo);
    }
}

/* ===== LookupIP with network filter ===== */

static void test_lookup_ip(void) {
    printf("[lookup_ip]\n");
    neverc_net_addrs_t addrs;

    addrs.count = 7;
    check_int("empty ip host rejected",
              neverc_net_lookup_ip("ip4", "", &addrs), -1);
    check_int("empty ip host clears leftover", addrs.count, 0);
    addrs.count = 7;
    check_int("null ip host rejected",
              neverc_net_lookup_ip("ip4", NULL, &addrs), -1);
    check_int("null ip host clears leftover", addrs.count, 0);

    int rc = neverc_net_lookup_ip("ip4", "localhost", &addrs);
    check_int("lookup ip4 localhost", rc, 0);
    if (addrs.count > 0) {
        /* All results should be IPv4 (no colons) */
        int all_v4 = 1;
        for (int i = 0; i < addrs.count; i++)
            if (strchr(addrs.addrs[i], ':')) all_v4 = 0;
        check_true("ip4 filter works", all_v4);
    }

    check_int("unknown ip network rejected",
              neverc_net_lookup_ip("tcp", "localhost", &addrs), -1);
    check_int("invalid ip network rejected",
              neverc_net_lookup_ip("ipx", "localhost", &addrs), -1);

    /* inet_ntop drops sin6_scope_id; a zoned literal must keep the zone. */
    const char *zoned_hosts[] = {
        "fe80::1%lo0", "fe80::1%lo", "fe80::1%1", NULL
    };
    int kept_zone = 0;
    int tried_zoned = 0;
    for (int i = 0; zoned_hosts[i]; i++) {
        neverc_net_addrs_t zoned;
        if (neverc_net_lookup_ip("ip6", zoned_hosts[i], &zoned) != 0)
            continue;
        tried_zoned = 1;
        for (int j = 0; j < zoned.count; j++) {
            if (strncmp(zoned.addrs[j], "fe80:", 5) == 0 &&
                strchr(zoned.addrs[j], '%'))
                kept_zone = 1;
        }
        if (kept_zone) break;
    }
    if (tried_zoned)
        check_true("lookup_ip keeps ipv6 zone", kept_zone);

    neverc_net_addrs_t bad;
    check_int("lookup_ip unknown iface rejected",
              neverc_net_lookup_ip("ip6", "fe80::1%no_such_iface_zzz", &bad),
              -1);
    check_int("lookup_ip empty zone rejected",
              neverc_net_lookup_ip("ip6", "fe80::1%", &bad), -1);
    check_int("lookup_ip zero zone rejected",
              neverc_net_lookup_ip("ip6", "fe80::1%0", &bad), -1);
    check_int("lookup_ip ipv4 zone rejected",
              neverc_net_lookup_ip("ip4", "127.0.0.1%1", &bad), -1);
    check_int("lookup_ip invalid utf8 rejected",
              neverc_net_lookup_ip("ip", "\xff\xfe.example", &bad), -1);

    neverc_net_addrs_t leftover;
    leftover.count = 9;
    strcpy(leftover.addrs[0], "8.8.8.8");
    check_int("lookup_host CTL name rejected",
              neverc_net_lookup_host("foo\nbar.example", &leftover), -1);
    check_int("lookup_host CTL clears leftover", leftover.count, 0);
    leftover.count = 9;
    check_int("lookup_ip CR name rejected",
              neverc_net_lookup_ip("ip", "foo\rbar.example", &leftover), -1);
    check_int("lookup_ip CR clears leftover", leftover.count, 0);
    leftover.count = 9;
    check_int("lookup_ip TAB name rejected",
              neverc_net_lookup_ip("ip4", "foo\tbar.example", &leftover), -1);
    check_int("lookup_ip TAB clears leftover", leftover.count, 0);
    leftover.count = 9;
    check_int("lookup_addr CTL rejected",
              neverc_net_lookup_addr("127.0.0.1\n", &leftover), -1);
    check_int("lookup_addr CTL clears leftover", leftover.count, 0);
    check_int("lookup_port CTL service rejected",
              neverc_net_lookup_port("tcp", "http\n"), -1);
    neverc_net_mx_list_t mxctl;
    mxctl.count = 4;
    check_int("lookup_mx CTL name rejected",
              neverc_net_lookup_mx("example.com\n", &mxctl), -1);
    check_int("lookup_mx CTL clears leftover", mxctl.count, 0);
    neverc_net_txt_list_t txtctl;
    txtctl.count = 4;
    check_int("lookup_txt CTL name rejected",
              neverc_net_lookup_txt("example.com\r", &txtctl), -1);
    check_int("lookup_txt CTL clears leftover", txtctl.count, 0);
    neverc_net_srv_list_t srvctl;
    srvctl.count = 4;
    check_int("lookup_srv CTL service rejected",
              neverc_net_lookup_srv("http\n", "tcp", "example.com", &srvctl),
              -1);
    check_int("lookup_srv CTL service clears leftover", srvctl.count, 0);
    srvctl.count = 4;
    check_int("lookup_srv CTL proto rejected",
              neverc_net_lookup_srv("http", "tcp\n", "example.com", &srvctl),
              -1);
    check_int("lookup_srv CTL proto clears leftover", srvctl.count, 0);
    char cname_ctl[64];
    memset(cname_ctl, 'A', sizeof(cname_ctl) - 1);
    cname_ctl[sizeof(cname_ctl) - 1] = '\0';
    check_int("lookup_cname CTL name rejected",
              neverc_net_lookup_cname("localhost\n", cname_ctl,
                                      sizeof(cname_ctl)), -1);
    check_true("lookup_cname CTL clears leftover", cname_ctl[0] == '\0');

    /* Dual-stack / mapped literals must print as IPv4 for ACL matching. */
    neverc_net_addrs_t mapped;
    check_int("lookup_ip mapped literal",
              neverc_net_lookup_ip("ip", "::ffff:127.0.0.1", &mapped), 0);
    if (mapped.count > 0) {
        int unmapped = 0;
        int has_ffff = 0;
        for (int i = 0; i < mapped.count; i++) {
            if (strcmp(mapped.addrs[i], "127.0.0.1") == 0) unmapped = 1;
            if (strstr(mapped.addrs[i], "ffff")) has_ffff = 1;
        }
        check_true("lookup_ip unmaps ipv4-mapped", unmapped);
        check_true("lookup_ip mapped has no ffff", !has_ffff);
    }

    neverc_net_addrs_t rev4, revm;
    int r4 = neverc_net_lookup_addr("127.0.0.1", &rev4);
    int rm = neverc_net_lookup_addr("::ffff:127.0.0.1", &revm);
    if (r4 == 0 && rm == 0) {
        check_str("lookup_addr mapped matches ipv4",
                  revm.addrs[0], rev4.addrs[0]);
    } else if (r4 == 0) {
        check_int("lookup_addr mapped should succeed", rm, 0);
    }
}

/* ===== LookupPort ===== */

static void test_lookup_port(void) {
    printf("[lookup_port]\n");

    check_int("http port", neverc_net_lookup_port("tcp", "http"), 80);
    check_int("https port", neverc_net_lookup_port("tcp", "https"), 443);
    check_int("numeric port", neverc_net_lookup_port("tcp", "8080"), 8080);
    check_int("port zero", neverc_net_lookup_port("tcp", "0"), 0);
    check_int("ssh port", neverc_net_lookup_port("tcp", "ssh"), 22);
    check_int("dns port", neverc_net_lookup_port("udp", "domain"), 53);
    check_int("empty service rejected", neverc_net_lookup_port("tcp", ""), -1);
    check_int("null service rejected", neverc_net_lookup_port("tcp", NULL), -1);
    check_int("overflow service rejected",
              neverc_net_lookup_port("tcp", "65536"), -1);
    check_int("signed numeric rejected",
              neverc_net_lookup_port("tcp", "+80"), -1);
    check_int("whitespace numeric rejected",
              neverc_net_lookup_port("tcp", " 80"), -1);
    check_int("unknown port network rejected",
              neverc_net_lookup_port("sctp", "http"), -1);
    check_int("unknown network rejects numeric port",
              neverc_net_lookup_port("bogus", "8080"), -1);

    neverc_net_addrs_t rev;
    rev.count = 4;
    check_int("empty reverse addr rejected",
              neverc_net_lookup_addr("", &rev), -1);
    check_int("empty reverse clears leftover", rev.count, 0);
    rev.count = 4;
    check_int("null reverse addr rejected",
              neverc_net_lookup_addr(NULL, &rev), -1);
    check_int("null reverse clears leftover", rev.count, 0);
    check_int("zoned ipv6 unknown iface rejected",
              neverc_net_lookup_addr("fe80::1%no_such_iface_zzz", &rev), -1);
    check_int("zoned ipv6 empty zone rejected",
              neverc_net_lookup_addr("fe80::1%", &rev), -1);
    check_int("ipv4 with zone rejected",
              neverc_net_lookup_addr("127.0.0.1%1", &rev), -1);
    check_int("zoned ipv6 zero zone rejected",
              neverc_net_lookup_addr("fe80::1%0", &rev), -1);
    neverc_net_mx_list_t mx;
    mx.count = 3;
    check_int("empty mx name rejected", neverc_net_lookup_mx("", &mx), -1);
    check_int("empty mx clears leftover", mx.count, 0);
    /* MX/NS/SRV name expansion is bounded to that RR's RDATA so a short
     * rdlen cannot start dn_expand in the next record, and trailing bytes
     * after the name are rejected. */
    neverc_net_txt_list_t txt;
    txt.count = 3;
    check_int("empty txt name rejected", neverc_net_lookup_txt("", &txt), -1);
    check_int("empty txt clears leftover", txt.count, 0);
    neverc_net_ns_list_t ns;
    ns.count = 3;
    check_int("empty ns name rejected", neverc_net_lookup_ns("", &ns), -1);
    check_int("empty ns clears leftover", ns.count, 0);
    neverc_net_srv_list_t empty_srv;
    empty_srv.count = 3;
    check_int("empty srv name rejected",
              neverc_net_lookup_srv("http", "tcp", "", &empty_srv), -1);
    check_int("empty srv clears leftover", empty_srv.count, 0);

    neverc_net_srv_list_t srv;
    char long_name[600];
    memset(long_name, 'a', 599);
    long_name[599] = '\0';
    srv.count = 99;
    check_int("srv qname overflow",
              neverc_net_lookup_srv("xmpp-client", "tcp", long_name, &srv), -1);
    check_int("srv overflow clears leftover", srv.count, 0);
}

/* ===== LookupCNAME ===== */

static void test_lookup_cname(void) {
    printf("[lookup_cname]\n");
    char buf[256];

    memset(buf, 'A', sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    check_int("empty cname rejected",
              neverc_net_lookup_cname("", buf, sizeof(buf)), -1);
    check_true("empty cname clears leftover", buf[0] == '\0');
    memset(buf, 'A', sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    check_int("null cname rejected",
              neverc_net_lookup_cname(NULL, buf, sizeof(buf)), -1);
    check_true("null cname clears leftover", buf[0] == '\0');

    int rc = neverc_net_lookup_cname("localhost", buf, sizeof(buf));
    check_int("cname localhost", rc, 0);
    check_true("cname has value", buf[0] != '\0');
    check_int("cname invalid utf8 rejected",
              neverc_net_lookup_cname("\xff\xfe", buf, sizeof(buf)), -1);
}

/* ===== Pipe ===== */

typedef struct {
    neverc_net_pipe_t *pipe;
    char recv_buf[256];
    int recv_len;
} pipe_thread_arg_t;

#ifndef _WIN32
static void *pipe_reader_thread(void *arg) {
    pipe_thread_arg_t *a = (pipe_thread_arg_t *)arg;
    a->recv_len = neverc_net_pipe_read(a->pipe, a->recv_buf,
                                         sizeof(a->recv_buf) - 1);
    if (a->recv_len > 0)
        a->recv_buf[a->recv_len] = '\0';
    return NULL;
}
#endif

static void test_pipe(void) {
    printf("[pipe]\n");

    neverc_net_pipe_t *end1, *end2;
    check_int("pipe create", neverc_net_pipe(&end1, &end2), 0);

    char byte = 0;
    check_int("pipe rejects oversized write",
              neverc_net_pipe_write(end1, &byte, (size_t)INT_MAX + 1), -1);
    check_int("pipe rejects oversized read",
              neverc_net_pipe_read(end1, &byte, (size_t)INT_MAX + 1), -1);
    check_int("pipe zero-length read",
              neverc_net_pipe_read(end1, &byte, 0), 0);
    check_int("pipe zero-length write",
              neverc_net_pipe_write(end1, &byte, 0), 0);

    /* Test basic write+read */
    const char *msg = "Hello, Pipe!";
    int wn = neverc_net_pipe_write(end1, msg, strlen(msg));
    check_int("pipe write", wn, (int)strlen(msg));

    char buf[256] = {0};
    int rn = neverc_net_pipe_read(end2, buf, sizeof(buf));
    check_int("pipe read len", rn, (int)strlen(msg));
    check_str("pipe read data", buf, msg);

    /* Test reverse direction */
    const char *reply = "Reply!";
    wn = neverc_net_pipe_write(end2, reply, strlen(reply));
    check_int("pipe reverse write", wn, (int)strlen(reply));

    memset(buf, 0, sizeof(buf));
    rn = neverc_net_pipe_read(end1, buf, sizeof(buf));
    check_int("pipe reverse read len", rn, (int)strlen(reply));
    check_str("pipe reverse read data", buf, reply);

#ifndef _WIN32
    /* Test concurrent read/write with threads */
    pipe_thread_arg_t targ;
    targ.pipe = end2;
    targ.recv_len = 0;
    memset(targ.recv_buf, 0, sizeof(targ.recv_buf));

    pthread_t th;
    pthread_create(&th, NULL, pipe_reader_thread, &targ);

    const char *concurrent_msg = "Concurrent!";
    neverc_net_pipe_write(end1, concurrent_msg, strlen(concurrent_msg));

    pthread_join(th, NULL);
    check_int("concurrent read len", targ.recv_len, (int)strlen(concurrent_msg));
    check_str("concurrent read data", targ.recv_buf, concurrent_msg);
#endif

    /* Test close + EOF */
    neverc_net_pipe_close(end1);
    rn = neverc_net_pipe_read(end2, buf, sizeof(buf));
    check_int("read after close = EOF", rn, 0);

    neverc_net_pipe_close(end2);
}

#ifndef _WIN32
static void *pipe_eof_waiter(void *arg) {
    neverc_net_pipe_t *p = (neverc_net_pipe_t *)arg;
    char buf[8];
    int n = neverc_net_pipe_read(p, buf, sizeof(buf));
    return (void *)(intptr_t)n;
}

static void test_pipe_close_wakes_all(void) {
    printf("[pipe_close_wakes_all]\n");
    neverc_net_pipe_t *end1, *end2;
    check_int("wake-all pipe create", neverc_net_pipe(&end1, &end2), 0);

    pthread_t t1, t2;
    pthread_create(&t1, NULL, pipe_eof_waiter, end2);
    pthread_create(&t2, NULL, pipe_eof_waiter, end2);
    usleep(50000);
    neverc_net_pipe_close(end1);

    void *r1 = (void *)(intptr_t)-2, *r2 = (void *)(intptr_t)-2;
    pthread_join(t1, &r1);
    pthread_join(t2, &r2);
    check_int("first waiter eof", (int)(intptr_t)r1, 0);
    check_int("second waiter eof", (int)(intptr_t)r2, 0);
    neverc_net_pipe_close(end2);
}
#endif

/* ===== Large pipe transfer ===== */

#ifndef _WIN32
typedef struct {
    neverc_net_pipe_t *p;
    char *buf;
    int total;
    int target_size;
} large_pipe_arg_t;

static void *large_reader_thread(void *arg) {
    large_pipe_arg_t *a = (large_pipe_arg_t *)arg;
    while (a->total < a->target_size) {
        int n = neverc_net_pipe_read(a->p, a->buf + a->total,
                                      (size_t)(a->target_size - a->total));
        if (n <= 0) break;
        a->total += n;
    }
    return NULL;
}
#endif

static void test_pipe_large(void) {
    printf("[pipe_large]\n");

    neverc_net_pipe_t *end1, *end2;
    check_int("large pipe create", neverc_net_pipe(&end1, &end2), 0);

    char big[4096];
    memset(big, 'X', sizeof(big));

#ifndef _WIN32
    char *big_recv = (char *)calloc(1, sizeof(big) + 1);

    pthread_t th;
    large_pipe_arg_t la = {end2, big_recv, 0, (int)sizeof(big)};

    pthread_create(&th, NULL, large_reader_thread, &la);

    int total_written = 0;
    while (total_written < (int)sizeof(big)) {
        int chunk = 512;
        if (total_written + chunk > (int)sizeof(big))
            chunk = (int)sizeof(big) - total_written;
        int n = neverc_net_pipe_write(end1, big + total_written, (size_t)chunk);
        if (n <= 0) break;
        total_written += n;
    }

    pthread_join(th, NULL);
    check_int("large write total", total_written, (int)sizeof(big));
    check_int("large read total", la.total, (int)sizeof(big));

    int match = (memcmp(big, big_recv, sizeof(big)) == 0);
    check_true("large data match", match);

    free(big_recv);
#endif

    neverc_net_pipe_close(end1);
    neverc_net_pipe_close(end2);
}

int main(void) {
    printf("=== NeverC net/resolve tests ===\n");

    test_split_host_port();
    test_join_host_port();
    test_addr_internal();
    test_lookup_host();
    test_lookup_ip();
    test_lookup_port();
    test_lookup_cname();
    test_pipe();
#ifndef _WIN32
    test_pipe_close_wakes_all();
#endif
    test_pipe_large();

    printf("\n%d tests, %d passed, %d failed\n",
           tests_run, tests_passed, tests_failed);

    if (tests_failed == 0)
        printf("ALL PASSED (%d tests passed)\n", tests_passed);
    if (tests_failed == 0) puts("passed");

    return tests_failed;
}
