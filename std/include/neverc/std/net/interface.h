#ifndef NEVERC_NET_INTERFACE_H
#define NEVERC_NET_INTERFACE_H

/*
 * NeverC net/interface — network interface enumeration.
 * Mirrors Go's net.Interfaces, net.InterfaceByName, etc.
 * Cross-platform: POSIX (Linux/macOS/iOS/Android) + Windows.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_NET_MAX_INTERFACES 64
#define NEVERC_NET_MAX_IF_ADDRS   16

/* Interface flags (mirrors Go net.Flags) */
#define NEVERC_NET_FLAG_UP            (1 << 0)
#define NEVERC_NET_FLAG_BROADCAST     (1 << 1)
#define NEVERC_NET_FLAG_LOOPBACK      (1 << 2)
#define NEVERC_NET_FLAG_POINTTOPOINT  (1 << 3)
#define NEVERC_NET_FLAG_MULTICAST     (1 << 4)
#define NEVERC_NET_FLAG_RUNNING       (1 << 5)

typedef struct {
    char    addr[64];      /* IP address string */
    int     prefix_len;    /* CIDR prefix length (-1 if unknown) */
} neverc_net_ifaddr_t;

typedef struct {
    int                  index;
    int                  mtu;
    char                 name[64];
    char                 hw_addr[24];    /* MAC address "aa:bb:cc:dd:ee:ff" */
    uint32_t             flags;

    neverc_net_ifaddr_t  addrs[NEVERC_NET_MAX_IF_ADDRS];
    int                  naddrs;
} neverc_net_interface_t;

typedef struct {
    neverc_net_interface_t ifaces[NEVERC_NET_MAX_INTERFACES];
    int                    count;
} neverc_net_interface_list_t;

/* List all network interfaces (like Go net.Interfaces).
 * Returns 0 on success. */
int neverc_net_interfaces(neverc_net_interface_list_t *out);

/* Find interface by name (like Go net.InterfaceByName).
 * Returns 0 on success, -1 if not found. */
int neverc_net_interface_by_name(const char *name,
                                  neverc_net_interface_t *out);

/* Find interface by index (like Go net.InterfaceByIndex).
 * Returns 0 on success, -1 if not found. */
int neverc_net_interface_by_index(int index,
                                   neverc_net_interface_t *out);

/* List all interface addresses (like Go net.InterfaceAddrs).
 * Returns address count, fills addrs array. */
int neverc_net_interface_addrs(neverc_net_ifaddr_t *addrs, int max_addrs);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
#include <neverc/std/net.h>
#endif

#endif /* NEVERC_NET_INTERFACE_H */
