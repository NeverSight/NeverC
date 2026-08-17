#include "neverc/std/net/interface.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}

static void check_true(const char *name, int cond) {
    tests_run++;
    if (cond) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s\n", name); }
}

/* ===== Interfaces listing ===== */

static void test_interfaces(void) {
    printf("[interfaces]\n");
    neverc_net_interface_list_t list;
    check_int("list interfaces", neverc_net_interfaces(&list), 0);
    check_true("has interfaces", list.count > 0);

    /* Should have at least loopback */
    int has_lo = 0;
    for (int i = 0; i < list.count; i++) {
        if (list.ifaces[i].flags & NEVERC_NET_FLAG_LOOPBACK)
            has_lo = 1;
    }
    check_true("has loopback", has_lo);

    /* Print for visibility */
    for (int i = 0; i < list.count; i++) {
        printf("  [%d] %s idx=%d mtu=%d flags=0x%x hw=%s addrs=%d\n",
               i, list.ifaces[i].name, list.ifaces[i].index,
               list.ifaces[i].mtu, list.ifaces[i].flags,
               list.ifaces[i].hw_addr[0] ? list.ifaces[i].hw_addr : "none",
               list.ifaces[i].naddrs);
        for (int j = 0; j < list.ifaces[i].naddrs; j++) {
            printf("    addr: %s/%d\n",
                   list.ifaces[i].addrs[j].addr,
                   list.ifaces[i].addrs[j].prefix_len);
        }
    }

    int saw_link_local = 0;
    int link_local_zoned = 1;
    for (int i = 0; i < list.count; i++) {
        for (int j = 0; j < list.ifaces[i].naddrs; j++) {
            const char *a = list.ifaces[i].addrs[j].addr;
            if (strncmp(a, "fe80:", 5) == 0) {
                saw_link_local = 1;
                if (!strchr(a, '%'))
                    link_local_zoned = 0;
            }
        }
    }
    if (saw_link_local)
        check_true("link-local addrs include zone", link_local_zoned);
}

/* ===== InterfaceByName ===== */

static void test_interface_by_name(void) {
    printf("[interface_by_name]\n");
    neverc_net_interface_t iface;

    int rc = -1;
#if defined(_WIN32)
    /* Windows reports friendly names such as "Loopback Pseudo-Interface 1". */
    neverc_net_interface_list_t list;
    if (neverc_net_interfaces(&list) == 0) {
        for (int i = 0; i < list.count; i++) {
            if (list.ifaces[i].flags & NEVERC_NET_FLAG_LOOPBACK) {
                rc = neverc_net_interface_by_name(list.ifaces[i].name, &iface);
                if (rc == 0)
                    break;
            }
        }
    }
#else
    /* lo0 on macOS, lo on Linux */
    rc = neverc_net_interface_by_name("lo0", &iface);
    if (rc != 0)
        rc = neverc_net_interface_by_name("lo", &iface);
#endif
    check_int("find loopback by name", rc, 0);
    if (rc == 0) {
        check_true("loopback flag set", (iface.flags & NEVERC_NET_FLAG_LOOPBACK) != 0);
        check_true("loopback up", (iface.flags & NEVERC_NET_FLAG_UP) != 0);
    }

    /* Non-existent */
    check_int("nonexistent iface", neverc_net_interface_by_name("xyz999", &iface), -1);
    check_int("null name", neverc_net_interface_by_name(NULL, &iface), -1);
    check_int("empty name", neverc_net_interface_by_name("", &iface), -1);
    check_int("null list", neverc_net_interfaces(NULL), -1);
}

/* ===== InterfaceByIndex ===== */

static void test_interface_by_index(void) {
    printf("[interface_by_index]\n");

    /* First get all interfaces, then look up the first one by index */
    neverc_net_interface_list_t list;
    if (neverc_net_interfaces(&list) == 0 && list.count > 0) {
        neverc_net_interface_t found;
        int idx = list.ifaces[0].index;
        check_int("find by index", neverc_net_interface_by_index(idx, &found), 0);
        check_true("same name", strcmp(found.name, list.ifaces[0].name) == 0);
    }

    neverc_net_interface_t notfound;
    check_int("invalid index", neverc_net_interface_by_index(99999, &notfound), -1);
    check_int("index zero rejected", neverc_net_interface_by_index(0, &notfound),
              -1);
    check_int("negative index rejected",
              neverc_net_interface_by_index(-1, &notfound), -1);
}

/* ===== InterfaceAddrs ===== */

static void test_interface_addrs(void) {
    printf("[interface_addrs]\n");
    neverc_net_ifaddr_t addrs[128];
    int count = neverc_net_interface_addrs(addrs, 128);
    check_true("has addrs", count > 0);

    /* Should have at least 127.0.0.1 or ::1 */
    int has_loopback_addr = 0;
    for (int i = 0; i < count; i++) {
        if (strcmp(addrs[i].addr, "127.0.0.1") == 0 ||
            strcmp(addrs[i].addr, "::1") == 0)
            has_loopback_addr = 1;
    }
    check_true("has loopback addr", has_loopback_addr);
}

int main(void) {
    printf("=== NeverC net/interface tests ===\n");

    test_interfaces();
    test_interface_by_name();
    test_interface_by_index();
    test_interface_addrs();

    printf("\n%d tests, %d passed, %d failed\n",
           tests_run, tests_passed, tests_failed);

    if (tests_failed == 0)
        printf("ALL PASSED (%d tests passed)\n", tests_passed);

    return tests_failed;
}
