#include "neverc/std/net/netip.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define ASSERT_TRUE(expr) do { tests_run++; \
    if (expr) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL [%d]: %s\n", __LINE__, #expr); } \
} while(0)

#define ASSERT_EQ(a, b) do { int _a=(a), _b=(b); tests_run++; \
    if (_a==_b) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL [%d]: %s = %d, expected %d\n", __LINE__, #a, _a, _b); } \
} while(0)

#define ASSERT_STREQ(a, b) do { tests_run++; \
    if (strcmp(a,b)==0) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL [%d]: got \"%s\", expected \"%s\"\n", __LINE__, a, b); } \
} while(0)

static void test_parse_ipv4(void) {
    printf("[parse IPv4]\n");
    neverc_netip_addr_t addr;
    ASSERT_EQ(neverc_netip_parse_addr("192.168.1.1", &addr), 0);
    ASSERT_TRUE(addr.is_v4);
    ASSERT_TRUE(addr.valid);
    char buf[64];
    neverc_netip_addr_string(&addr, buf, sizeof(buf));
    ASSERT_STREQ(buf, "192.168.1.1");

    ASSERT_EQ(neverc_netip_parse_addr("0.0.0.0", &addr), 0);
    neverc_netip_addr_string(&addr, buf, sizeof(buf));
    ASSERT_STREQ(buf, "0.0.0.0");

    ASSERT_EQ(neverc_netip_parse_addr("255.255.255.255", &addr), 0);
    neverc_netip_addr_string(&addr, buf, sizeof(buf));
    ASSERT_STREQ(buf, "255.255.255.255");

    ASSERT_EQ(neverc_netip_parse_addr("256.0.0.0", &addr), -1);
    ASSERT_EQ(neverc_netip_parse_addr("1.2.3", &addr), -1);
    ASSERT_EQ(neverc_netip_parse_addr("", &addr), -1);
    ASSERT_EQ(neverc_netip_parse_addr(NULL, &addr), -1);
    ASSERT_EQ(neverc_netip_parse_addr("192.168.1.1", NULL), -1);
    ASSERT_EQ(neverc_netip_parse_addr("127.0.0.01", &addr), -1);
    ASSERT_EQ(neverc_netip_parse_addr("01.2.3.4", &addr), -1);
    ASSERT_EQ(neverc_netip_parse_addr("192.168.1.1.", &addr), -1);
    ASSERT_EQ(neverc_netip_parse_addr("::ffff:192.168.001.1", &addr), -1);
}

static void test_parse_ipv6(void) {
    printf("[parse IPv6]\n");
    neverc_netip_addr_t addr;
    char buf[128];

    ASSERT_EQ(neverc_netip_parse_addr("::1", &addr), 0);
    ASSERT_TRUE(!addr.is_v4);
    neverc_netip_addr_string(&addr, buf, sizeof(buf));
    ASSERT_STREQ(buf, "::1");

    ASSERT_EQ(neverc_netip_parse_addr("::", &addr), 0);
    neverc_netip_addr_string(&addr, buf, sizeof(buf));
    ASSERT_STREQ(buf, "::");

    ASSERT_EQ(neverc_netip_parse_addr("2001:db8::1", &addr), 0);
    neverc_netip_addr_string(&addr, buf, sizeof(buf));
    ASSERT_STREQ(buf, "2001:db8::1");

    ASSERT_EQ(neverc_netip_parse_addr("fe80::1%eth0", &addr), 0);
    ASSERT_STREQ(addr.zone, "eth0");
    neverc_netip_addr_string(&addr, buf, sizeof(buf));
    ASSERT_STREQ(buf, "fe80::1%eth0");
    ASSERT_EQ(neverc_netip_parse_addr("fe80::1%Ethernet 2", &addr), 0);
    ASSERT_STREQ(addr.zone, "Ethernet 2");
    neverc_netip_addr_string(&addr, buf, sizeof(buf));
    ASSERT_STREQ(buf, "fe80::1%Ethernet 2");

    ASSERT_EQ(neverc_netip_parse_addr("::ffff:192.168.1.1", &addr), 0);
    ASSERT_TRUE(!addr.is_v4);
    neverc_netip_addr_string(&addr, buf, sizeof(buf));
    ASSERT_STREQ(buf, "::ffff:192.168.1.1");

    ASSERT_EQ(neverc_netip_parse_addr("::ffff:c0a8:101", &addr), 0);
    neverc_netip_addr_string(&addr, buf, sizeof(buf));
    ASSERT_STREQ(buf, "::ffff:192.168.1.1");

    ASSERT_EQ(neverc_netip_parse_addr("::ffff:192.168.1.1%eth0", &addr), 0);
    neverc_netip_addr_string(&addr, buf, sizeof(buf));
    ASSERT_STREQ(buf, "::ffff:192.168.1.1%eth0");
    ASSERT_TRUE(neverc_netip_addr_is4in6(&addr));
    ASSERT_TRUE(!neverc_netip_addr_is4(&addr));
    ASSERT_EQ(neverc_netip_addr_string(&addr, NULL, 0),
              (int)strlen("::ffff:192.168.1.1%eth0"));

    neverc_netip_addr_t unmapped;
    ASSERT_EQ(neverc_netip_addr_unmap(&addr, &unmapped), 0);
    ASSERT_TRUE(neverc_netip_addr_is4(&unmapped));
    ASSERT_TRUE(!neverc_netip_addr_is4in6(&unmapped));
    ASSERT_STREQ(unmapped.zone, "");
    neverc_netip_addr_string(&unmapped, buf, sizeof(buf));
    ASSERT_STREQ(buf, "192.168.1.1");
    uint8_t v4[4];
    ASSERT_EQ(neverc_netip_addr_as4(&unmapped, v4), 4);
    ASSERT_EQ(v4[0], 192); ASSERT_EQ(v4[1], 168);
    ASSERT_EQ(v4[2], 1); ASSERT_EQ(v4[3], 1);
    ASSERT_EQ(neverc_netip_addr_as4(&addr, v4), -1);

    neverc_netip_parse_addr("::1", &addr);
    ASSERT_TRUE(!neverc_netip_addr_is4in6(&addr));
    ASSERT_EQ(neverc_netip_addr_unmap(&addr, &unmapped), 0);
    ASSERT_TRUE(neverc_netip_addr_is6(&unmapped));
    ASSERT_TRUE(!neverc_netip_addr_is4in6(&unmapped));

    ASSERT_EQ(neverc_netip_parse_addr("ff02::1", &addr), 0);
    neverc_netip_addr_string(&addr, buf, sizeof(buf));
    ASSERT_STREQ(buf, "ff02::1");

    ASSERT_EQ(neverc_netip_parse_addr("::1:2:3:4:5:6:7:8", &addr), -1);
    ASSERT_EQ(neverc_netip_parse_addr("1:2:3:4:5:6:7:8::", &addr), -1);
    ASSERT_EQ(neverc_netip_parse_addr("1:2:3:4:5:6:7:8:", &addr), -1);
    ASSERT_EQ(neverc_netip_parse_addr("fe80::1%", &addr), -1);

    ASSERT_EQ(neverc_netip_parse_addr("192.0.2.1%eth0", &addr), -1);
    ASSERT_EQ(neverc_netip_parse_addr("1.2.3.4%x", &addr), -1);
    ASSERT_EQ(neverc_netip_parse_addr("fe80::1%\neth0", &addr), -1);
    ASSERT_EQ(neverc_netip_parse_addr("fe80::1%\x7f", &addr), -1);
    ASSERT_EQ(neverc_netip_parse_addr("fe80::1%]eth0", &addr), -1);
    ASSERT_EQ(neverc_netip_parse_addr("fe80::1%/eth0", &addr), -1);
    ASSERT_EQ(neverc_netip_parse_addr("fe80::1%@eth0", &addr), -1);

    ASSERT_EQ(neverc_netip_parse_addr("::ffff:0.0.0.0", &addr), 0);
    ASSERT_TRUE(neverc_netip_addr_is4in6(&addr));
    ASSERT_TRUE(!neverc_netip_addr_is_unspecified(&addr));
    ASSERT_TRUE(!neverc_netip_addr_is_global_unicast(&addr));
    ASSERT_TRUE(neverc_netip_addr_is_internal(&addr));

    char long_zone[80];
    memcpy(long_zone, "fe80::1%", 8);
    memset(long_zone + 8, 'z', 64);
    long_zone[72] = '\0';
    ASSERT_EQ(neverc_netip_parse_addr(long_zone, &addr), -1);
}

static void test_addr_from4(void) {
    printf("[addr_from4]\n");
    neverc_netip_addr_t addr;
    neverc_netip_addr_from4(10, 0, 0, 1, &addr);
    char buf[64];
    neverc_netip_addr_string(&addr, buf, sizeof(buf));
    ASSERT_STREQ(buf, "10.0.0.1");
    ASSERT_TRUE(neverc_netip_addr_is4(&addr));
}

static void test_properties(void) {
    printf("[properties]\n");
    neverc_netip_addr_t addr;

    neverc_netip_parse_addr("127.0.0.1", &addr);
    ASSERT_TRUE(neverc_netip_addr_is_loopback(&addr));
    ASSERT_TRUE(!neverc_netip_addr_is_multicast(&addr));
    ASSERT_TRUE(!neverc_netip_addr_is_private(&addr));

    neverc_netip_parse_addr("10.0.0.1", &addr);
    ASSERT_TRUE(neverc_netip_addr_is_private(&addr));
    ASSERT_TRUE(!neverc_netip_addr_is_global_unicast(&addr));

    neverc_netip_parse_addr("172.16.0.1", &addr);
    ASSERT_TRUE(neverc_netip_addr_is_private(&addr));

    neverc_netip_parse_addr("192.168.0.1", &addr);
    ASSERT_TRUE(neverc_netip_addr_is_private(&addr));

    neverc_netip_parse_addr("224.0.0.1", &addr);
    ASSERT_TRUE(neverc_netip_addr_is_multicast(&addr));

    neverc_netip_parse_addr("169.254.1.1", &addr);
    ASSERT_TRUE(neverc_netip_addr_is_link_local_unicast(&addr));

    neverc_netip_parse_addr("0.0.0.0", &addr);
    ASSERT_TRUE(neverc_netip_addr_is_unspecified(&addr));

    neverc_netip_parse_addr("8.8.8.8", &addr);
    ASSERT_TRUE(neverc_netip_addr_is_global_unicast(&addr));
    ASSERT_TRUE(!neverc_netip_addr_is_internal(&addr));

    neverc_netip_parse_addr("255.255.255.255", &addr);
    ASSERT_TRUE(!neverc_netip_addr_is_global_unicast(&addr));
    ASSERT_TRUE(neverc_netip_addr_is_internal(&addr));

    neverc_netip_parse_addr("::1", &addr);
    ASSERT_TRUE(neverc_netip_addr_is_loopback(&addr));
    ASSERT_EQ(neverc_netip_addr_bit_len(&addr), 128);

    neverc_netip_parse_addr("::ffff:127.0.0.1", &addr);
    ASSERT_TRUE(neverc_netip_addr_is_loopback(&addr));
    ASSERT_TRUE(!neverc_netip_addr_is_global_unicast(&addr));
    ASSERT_TRUE(neverc_netip_addr_is_internal(&addr));
    neverc_netip_parse_addr("::ffff:10.0.0.1", &addr);
    ASSERT_TRUE(neverc_netip_addr_is_private(&addr));
    ASSERT_TRUE(!neverc_netip_addr_is_global_unicast(&addr));

    neverc_netip_parse_addr("fe80::1", &addr);
    ASSERT_TRUE(neverc_netip_addr_is_link_local_unicast(&addr));

    neverc_netip_parse_addr("ff02::1", &addr);
    ASSERT_TRUE(neverc_netip_addr_is_multicast(&addr));
    ASSERT_TRUE(neverc_netip_addr_is_link_local_multicast(&addr));

    neverc_netip_parse_addr("fc00::1", &addr);
    ASSERT_TRUE(neverc_netip_addr_is_private(&addr));
    ASSERT_TRUE(!neverc_netip_addr_is_global_unicast(&addr));

    neverc_netip_parse_addr("::", &addr);
    ASSERT_TRUE(neverc_netip_addr_is_unspecified(&addr));
}

static void test_compare(void) {
    printf("[compare]\n");
    neverc_netip_addr_t a, b;
    neverc_netip_parse_addr("1.2.3.4", &a);
    neverc_netip_parse_addr("1.2.3.5", &b);
    ASSERT_TRUE(neverc_netip_addr_compare(&a, &b) < 0);
    ASSERT_TRUE(neverc_netip_addr_compare(&b, &a) > 0);
    ASSERT_TRUE(neverc_netip_addr_equal(&a, &a));
    ASSERT_TRUE(!neverc_netip_addr_equal(&a, &b));
    ASSERT_EQ(neverc_netip_addr_compare(NULL, NULL), 0);
    ASSERT_TRUE(neverc_netip_addr_compare(&a, NULL) > 0);
    ASSERT_TRUE(neverc_netip_addr_compare(NULL, &a) < 0);
    ASSERT_TRUE(!neverc_netip_addr_equal(&a, NULL));

    neverc_netip_parse_addr("fe80::1%eth0", &a);
    neverc_netip_parse_addr("fe80::1%eth1", &b);
    ASSERT_TRUE(!neverc_netip_addr_equal(&a, &b));
    ASSERT_TRUE(neverc_netip_addr_compare(&a, &b) != 0);
    ASSERT_TRUE(neverc_netip_addr_equal(&a, &a));
}

static void test_prefix(void) {
    printf("[prefix]\n");
    neverc_netip_prefix_t pfx;
    char buf[128];

    ASSERT_EQ(neverc_netip_parse_prefix("192.168.1.0/24", &pfx), 0);
    ASSERT_EQ(neverc_netip_prefix_bits(&pfx), 24);
    neverc_netip_prefix_string(&pfx, buf, sizeof(buf));
    ASSERT_STREQ(buf, "192.168.1.0/24");

    neverc_netip_addr_t addr;
    neverc_netip_parse_addr("192.168.1.100", &addr);
    ASSERT_TRUE(neverc_netip_prefix_contains(&pfx, &addr));

    neverc_netip_addr_t masked;
    ASSERT_EQ(neverc_netip_prefix_masked(&pfx, &masked), 0);
    neverc_netip_addr_string(&masked, buf, sizeof(buf));
    ASSERT_STREQ(buf, "192.168.1.0");

    neverc_netip_parse_addr("192.168.2.1", &addr);
    ASSERT_TRUE(!neverc_netip_prefix_contains(&pfx, &addr));

    ASSERT_EQ(neverc_netip_parse_prefix("10.0.0.0/8", &pfx), 0);
    neverc_netip_parse_addr("10.255.255.255", &addr);
    ASSERT_TRUE(neverc_netip_prefix_contains(&pfx, &addr));
    neverc_netip_parse_addr("11.0.0.0", &addr);
    ASSERT_TRUE(!neverc_netip_prefix_contains(&pfx, &addr));

    ASSERT_EQ(neverc_netip_parse_prefix("2001:db8::/32", &pfx), 0);
    neverc_netip_parse_addr("2001:db8::1", &addr);
    ASSERT_TRUE(neverc_netip_prefix_contains(&pfx, &addr));
    neverc_netip_parse_addr("2001:db9::1", &addr);
    ASSERT_TRUE(!neverc_netip_prefix_contains(&pfx, &addr));

    ASSERT_EQ(neverc_netip_parse_prefix("1.2.3.4/33", &pfx), -1);
    ASSERT_EQ(neverc_netip_parse_prefix("1.2.3.4/", &pfx), -1);
    ASSERT_EQ(neverc_netip_parse_prefix("2001:db8::/129", &pfx), -1);
    ASSERT_EQ(neverc_netip_parse_prefix("fe80::1%eth0/64", &pfx), -1);
    ASSERT_EQ(neverc_netip_parse_prefix("1.2.3.4/08", &pfx), -1);
    ASSERT_EQ(neverc_netip_parse_prefix("1.2.3.4/256", &pfx), -1);
    ASSERT_TRUE(!pfx.valid);
    ASSERT_EQ(neverc_netip_parse_prefix("1.2.3.4/65536", &pfx), -1);
    ASSERT_TRUE(!pfx.valid);
    ASSERT_EQ(neverc_netip_parse_prefix("1.2.3.4/99999", &pfx), -1);
    ASSERT_EQ(neverc_netip_parse_prefix("1.2.3.4/100000", &pfx), -1);
    ASSERT_TRUE(!pfx.valid);
    ASSERT_EQ(neverc_netip_parse_prefix("::/256", &pfx), -1);
    ASSERT_TRUE(!pfx.valid);
    ASSERT_EQ(neverc_netip_parse_prefix("10.0.0.0/0", &pfx), 0);

    ASSERT_EQ(neverc_netip_parse_prefix("2001:db8::/32", &pfx), 0);
    neverc_netip_parse_addr("2001:db8::1%eth0", &addr);
    ASSERT_TRUE(!neverc_netip_prefix_contains(&pfx, &addr));

    ASSERT_EQ(neverc_netip_parse_prefix("192.168.1.0/24", &pfx), 0);
    neverc_netip_parse_addr("::ffff:192.168.1.100", &addr);
    ASSERT_TRUE(!neverc_netip_prefix_contains(&pfx, &addr));
    neverc_netip_parse_addr("::ffff:192.168.2.1", &addr);
    ASSERT_TRUE(!neverc_netip_prefix_contains(&pfx, &addr));
    neverc_netip_parse_addr("192.168.1.100", &addr);
    ASSERT_TRUE(neverc_netip_prefix_contains(&pfx, &addr));

    ASSERT_EQ(neverc_netip_parse_prefix("::ffff:10.0.0.0/96", &pfx), 0);
    neverc_netip_parse_addr("10.0.0.1", &addr);
    ASSERT_TRUE(!neverc_netip_prefix_contains(&pfx, &addr));
    neverc_netip_parse_addr("::ffff:10.0.0.1", &addr);
    ASSERT_TRUE(neverc_netip_prefix_contains(&pfx, &addr));

    /* Go Prefix.Contains: /0 matches the whole family; host bits of an
     * uncanonical prefix are ignored; partial-byte masks use the high bits. */
    ASSERT_EQ(neverc_netip_parse_prefix("0.0.0.0/0", &pfx), 0);
    neverc_netip_parse_addr("8.8.8.8", &addr);
    ASSERT_TRUE(neverc_netip_prefix_contains(&pfx, &addr));
    neverc_netip_parse_addr("255.255.255.255", &addr);
    ASSERT_TRUE(neverc_netip_prefix_contains(&pfx, &addr));
    neverc_netip_parse_addr("::1", &addr);
    ASSERT_TRUE(!neverc_netip_prefix_contains(&pfx, &addr));

    ASSERT_EQ(neverc_netip_parse_prefix("192.168.1.0/24", &pfx), 0);
    neverc_netip_parse_addr("192.168.1.255", &addr);
    ASSERT_TRUE(neverc_netip_prefix_contains(&pfx, &addr));
    neverc_netip_parse_addr("192.168.1.0", &addr);
    ASSERT_TRUE(neverc_netip_prefix_contains(&pfx, &addr));

    ASSERT_EQ(neverc_netip_parse_prefix("192.168.1.100/24", &pfx), 0);
    neverc_netip_parse_addr("192.168.1.1", &addr);
    ASSERT_TRUE(neverc_netip_prefix_contains(&pfx, &addr));

    ASSERT_EQ(neverc_netip_parse_prefix("192.168.1.0/31", &pfx), 0);
    neverc_netip_parse_addr("192.168.1.0", &addr);
    ASSERT_TRUE(neverc_netip_prefix_contains(&pfx, &addr));
    neverc_netip_parse_addr("192.168.1.1", &addr);
    ASSERT_TRUE(neverc_netip_prefix_contains(&pfx, &addr));
    neverc_netip_parse_addr("192.168.1.2", &addr);
    ASSERT_TRUE(!neverc_netip_prefix_contains(&pfx, &addr));

    ASSERT_EQ(neverc_netip_parse_prefix("::/0", &pfx), 0);
    neverc_netip_parse_addr("::1", &addr);
    ASSERT_TRUE(neverc_netip_prefix_contains(&pfx, &addr));
    neverc_netip_parse_addr("2001:db8::1", &addr);
    ASSERT_TRUE(neverc_netip_prefix_contains(&pfx, &addr));
    neverc_netip_parse_addr("1.2.3.4", &addr);
    ASSERT_TRUE(!neverc_netip_prefix_contains(&pfx, &addr));

    ASSERT_EQ(neverc_netip_parse_prefix("::1/128", &pfx), 0);
    neverc_netip_parse_addr("::1", &addr);
    ASSERT_TRUE(neverc_netip_prefix_contains(&pfx, &addr));
    neverc_netip_parse_addr("::2", &addr);
    ASSERT_TRUE(!neverc_netip_prefix_contains(&pfx, &addr));

    ASSERT_EQ(neverc_netip_parse_prefix("2001:db8::/121", &pfx), 0);
    neverc_netip_parse_addr("2001:db8::1", &addr);
    ASSERT_TRUE(neverc_netip_prefix_contains(&pfx, &addr));
    neverc_netip_parse_addr("2001:db8::7f", &addr);
    ASSERT_TRUE(neverc_netip_prefix_contains(&pfx, &addr));
    neverc_netip_parse_addr("2001:db8::80", &addr);
    ASSERT_TRUE(!neverc_netip_prefix_contains(&pfx, &addr));
}

static void test_addrport(void) {
    printf("[addrport]\n");
    neverc_netip_addrport_t ap;
    char buf[128];

    ASSERT_EQ(neverc_netip_parse_addrport("192.168.1.1:8080", &ap), 0);
    ASSERT_EQ(ap.port, 8080);
    neverc_netip_addrport_string(&ap, buf, sizeof(buf));
    ASSERT_STREQ(buf, "192.168.1.1:8080");

    ASSERT_EQ(neverc_netip_parse_addrport("[::1]:443", &ap), 0);
    ASSERT_EQ(ap.port, 443);
    neverc_netip_addrport_string(&ap, buf, sizeof(buf));
    ASSERT_STREQ(buf, "[::1]:443");

    ASSERT_EQ(neverc_netip_parse_addrport("::1:8080", &ap), -1);
    ASSERT_EQ(neverc_netip_parse_addrport("192.168.1.1:65536", &ap), -1);
    ASSERT_EQ(neverc_netip_parse_addrport("[::1]:65536", &ap), -1);
    ASSERT_EQ(neverc_netip_parse_addrport("192.168.1.1:", &ap), -1);
    ASSERT_EQ(neverc_netip_parse_addrport("", &ap), -1);
    ASSERT_EQ(neverc_netip_parse_addrport(NULL, &ap), -1);
    ASSERT_EQ(neverc_netip_parse_addrport("[::1]", &ap), -1);
    ASSERT_EQ(neverc_netip_parse_addrport("192.168.1.1", &ap), -1);
    ASSERT_EQ(neverc_netip_parse_addrport("[192.168.1.1]:80", &ap), -1);
    ASSERT_EQ(neverc_netip_parse_addrport("[fe80::1%eth0]:80", &ap), 0);
    ASSERT_EQ(ap.port, 80);
    neverc_netip_addrport_string(&ap, buf, sizeof(buf));
    ASSERT_STREQ(buf, "[fe80::1%eth0]:80");
    ASSERT_EQ(neverc_netip_parse_addrport("[::ffff:192.168.1.1]:80", &ap), 0);
    neverc_netip_addrport_string(&ap, buf, sizeof(buf));
    ASSERT_STREQ(buf, "[::ffff:192.168.1.1]:80");
    ASSERT_EQ(neverc_netip_parse_addrport("::ffff:192.168.1.1:80", &ap), -1);
    ASSERT_EQ(neverc_netip_parse_addrport("192.168.1.1:00080", &ap), 0);
    ASSERT_EQ(ap.port, 80);
    ASSERT_EQ(neverc_netip_parse_addrport("[::1]:00080", &ap), 0);
    ASSERT_EQ(ap.port, 80);
    neverc_netip_addrport_t invalid;
    memset(&invalid, 0, sizeof(invalid));
    ASSERT_EQ(neverc_netip_addrport_string(&invalid, buf, sizeof(buf)), -1);
}

static void test_wellknown(void) {
    printf("[well-known addresses]\n");
    neverc_netip_addr_t addr;
    char buf[128];

    neverc_netip_addr_ipv4_unspecified(&addr);
    neverc_netip_addr_string(&addr, buf, sizeof(buf));
    ASSERT_STREQ(buf, "0.0.0.0");

    neverc_netip_addr_ipv6_loopback(&addr);
    neverc_netip_addr_string(&addr, buf, sizeof(buf));
    ASSERT_STREQ(buf, "::1");
    ASSERT_TRUE(neverc_netip_addr_is_loopback(&addr));
    ASSERT_TRUE(neverc_netip_addr_is6(&addr));
    ASSERT_TRUE(neverc_netip_addr_is_valid(&addr));

    neverc_netip_addr_ipv6_unspecified(&addr);
    neverc_netip_addr_string(&addr, buf, sizeof(buf));
    ASSERT_STREQ(buf, "::");
    ASSERT_TRUE(neverc_netip_addr_is6(&addr));
    ASSERT_TRUE(neverc_netip_addr_is_unspecified(&addr));
}

static void test_as_bytes(void) {
    printf("[as bytes]\n");
    neverc_netip_addr_t addr;
    neverc_netip_addr_from4(10, 20, 30, 40, &addr);
    uint8_t v4[4];
    ASSERT_EQ(neverc_netip_addr_as4(&addr, v4), 4);
    ASSERT_EQ(v4[0], 10); ASSERT_EQ(v4[1], 20); ASSERT_EQ(v4[2], 30); ASSERT_EQ(v4[3], 40);

    uint8_t v16[16];
    ASSERT_EQ(neverc_netip_addr_as16(&addr, v16), 16);
    ASSERT_EQ(v16[12], 10); ASSERT_EQ(v16[13], 20);

    uint8_t raw16[16] = {0};
    raw16[15] = 1;
    ASSERT_EQ(neverc_netip_addr_from16(raw16, &addr), 0);
    ASSERT_TRUE(neverc_netip_addr_is6(&addr));
    ASSERT_TRUE(neverc_netip_addr_is_loopback(&addr));
}

int main(void) {
    printf("=== NeverC net/netip Tests ===\n");
    test_parse_ipv4();
    test_parse_ipv6();
    test_addr_from4();
    test_properties();
    test_compare();
    test_prefix();
    test_addrport();
    test_wellknown();
    test_as_bytes();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
