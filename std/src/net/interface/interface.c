#include "neverc/std/net/interface.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <iphlpapi.h>
  #include <windows.h>
  #pragma comment(lib, "iphlpapi.lib")
  #pragma comment(lib, "ws2_32.lib")
#else
  #include <sys/socket.h>
  #include <sys/types.h>
  #include <sys/ioctl.h>
  #include <net/if.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <ifaddrs.h>
  #include <unistd.h>
  #if defined(__linux__) || defined(__ANDROID__)
    #include <linux/if_packet.h>
  #elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || \
        defined(__NetBSD__)
    #include <net/if_dl.h>
  #endif
#endif

static void format_mac(const unsigned char *hw, int hwlen,
                        char *out, size_t outlen) {
    if (hwlen >= 6) {
        snprintf(out, outlen, "%02x:%02x:%02x:%02x:%02x:%02x",
                 hw[0], hw[1], hw[2], hw[3], hw[4], hw[5]);
    } else {
        out[0] = '\0';
    }
}

#ifdef _WIN32

int neverc_net_interfaces(neverc_net_interface_list_t *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    ULONG buflen = 15000;
    PIP_ADAPTER_ADDRESSES addrs = NULL;
    ULONG flags = GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_SKIP_ANYCAST |
                  GAA_FLAG_SKIP_MULTICAST;

    for (int tries = 0; tries < 3; tries++) {
        addrs = (PIP_ADAPTER_ADDRESSES)malloc(buflen);
        if (!addrs) return -1;
        ULONG rc = GetAdaptersAddresses(AF_UNSPEC, flags, NULL, addrs, &buflen);
        if (rc == NO_ERROR) break;
        free(addrs);
        addrs = NULL;
        if (rc != ERROR_BUFFER_OVERFLOW) return -1;
    }
    if (!addrs) return -1;

    for (PIP_ADAPTER_ADDRESSES a = addrs;
         a && out->count < NEVERC_NET_MAX_INTERFACES;
         a = a->Next) {
        neverc_net_interface_t *iface = &out->ifaces[out->count];
        iface->index = (int)a->IfIndex;
        iface->mtu = (int)a->Mtu;

        if (WideCharToMultiByte(CP_UTF8, 0, a->FriendlyName, -1,
                                 iface->name, (int)sizeof(iface->name),
                                 NULL, NULL) <= 0)
            iface->name[0] = '\0';
        iface->name[sizeof(iface->name) - 1] = '\0';

        if (a->PhysicalAddressLength >= 6)
            format_mac(a->PhysicalAddress, (int)a->PhysicalAddressLength,
                        iface->hw_addr, sizeof(iface->hw_addr));

        if (a->OperStatus == IfOperStatusUp) iface->flags |= NEVERC_NET_FLAG_UP;
        if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) iface->flags |= NEVERC_NET_FLAG_LOOPBACK;
        if (a->Flags & IP_ADAPTER_NO_MULTICAST) { /* nothing */ }
        else iface->flags |= NEVERC_NET_FLAG_MULTICAST;

        for (PIP_ADAPTER_UNICAST_ADDRESS ua = a->FirstUnicastAddress;
             ua && iface->naddrs < NEVERC_NET_MAX_IF_ADDRS;
             ua = ua->Next) {
            char buf[64] = {0};
            struct sockaddr *sa = ua->Address.lpSockaddr;
            if (sa->sa_family == AF_INET) {
                struct sockaddr_in *in = (struct sockaddr_in *)sa;
                inet_ntop(AF_INET, &in->sin_addr, buf, sizeof(buf));
                iface->addrs[iface->naddrs].prefix_len =
                    (int)ua->OnLinkPrefixLength;
            } else if (sa->sa_family == AF_INET6) {
                struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)sa;
                inet_ntop(AF_INET6, &in6->sin6_addr, buf, sizeof(buf));
                iface->addrs[iface->naddrs].prefix_len =
                    (int)ua->OnLinkPrefixLength;
            }
            if (buf[0]) {
                strncpy(iface->addrs[iface->naddrs].addr, buf, 63);
                iface->naddrs++;
            }
        }
        out->count++;
    }

    free(addrs);
    return 0;
}

#else /* POSIX */

int neverc_net_interfaces(neverc_net_interface_list_t *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    struct ifaddrs *ifap, *ifa;
    if (getifaddrs(&ifap) != 0) return -1;

    /* First pass: collect unique interface names */
    for (ifa = ifap; ifa && out->count < NEVERC_NET_MAX_INTERFACES;
         ifa = ifa->ifa_next) {
        if (!ifa->ifa_name) continue;

        int found = -1;
        for (int i = 0; i < out->count; i++) {
            if (strcmp(out->ifaces[i].name, ifa->ifa_name) == 0) {
                found = i;
                break;
            }
        }

        neverc_net_interface_t *iface;
        if (found >= 0) {
            iface = &out->ifaces[found];
        } else {
            iface = &out->ifaces[out->count];
            strncpy(iface->name, ifa->ifa_name, sizeof(iface->name) - 1);
            iface->index = (int)if_nametoindex(ifa->ifa_name);

            if (ifa->ifa_flags & IFF_UP)        iface->flags |= NEVERC_NET_FLAG_UP;
            if (ifa->ifa_flags & IFF_BROADCAST)  iface->flags |= NEVERC_NET_FLAG_BROADCAST;
            if (ifa->ifa_flags & IFF_LOOPBACK)   iface->flags |= NEVERC_NET_FLAG_LOOPBACK;
            if (ifa->ifa_flags & IFF_POINTOPOINT) iface->flags |= NEVERC_NET_FLAG_POINTTOPOINT;
#ifdef IFF_MULTICAST
            if (ifa->ifa_flags & IFF_MULTICAST)  iface->flags |= NEVERC_NET_FLAG_MULTICAST;
#endif
#ifdef IFF_RUNNING
            if (ifa->ifa_flags & IFF_RUNNING)    iface->flags |= NEVERC_NET_FLAG_RUNNING;
#endif

            /* MTU */
#if !defined(__APPLE__) || !defined(TARGET_OS_IPHONE)
            int sock = socket(AF_INET, SOCK_DGRAM, 0);
            if (sock >= 0) {
                struct ifreq ifr;
                memset(&ifr, 0, sizeof(ifr));
                strncpy(ifr.ifr_name, ifa->ifa_name, IFNAMSIZ - 1);
                if (ioctl(sock, SIOCGIFMTU, &ifr) == 0)
                    iface->mtu = ifr.ifr_mtu;
                close(sock);
            }
#endif
            out->count++;
        }

        if (!ifa->ifa_addr) continue;

        /* MAC address */
#if defined(__linux__) || defined(__ANDROID__)
        if (ifa->ifa_addr->sa_family == AF_PACKET) {
            struct sockaddr_ll *sll = (struct sockaddr_ll *)ifa->ifa_addr;
            if (sll->sll_halen >= 6)
                format_mac(sll->sll_addr, (int)sll->sll_halen,
                            iface->hw_addr, sizeof(iface->hw_addr));
        }
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || \
      defined(__NetBSD__)
        if (ifa->ifa_addr->sa_family == AF_LINK) {
            struct sockaddr_dl *sdl = (struct sockaddr_dl *)ifa->ifa_addr;
            if (sdl->sdl_alen >= 6)
                format_mac((unsigned char *)LLADDR(sdl), (int)sdl->sdl_alen,
                            iface->hw_addr, sizeof(iface->hw_addr));
        }
#endif

        /* IP addresses */
        if (ifa->ifa_addr->sa_family == AF_INET &&
            iface->naddrs < NEVERC_NET_MAX_IF_ADDRS) {
            struct sockaddr_in *in = (struct sockaddr_in *)ifa->ifa_addr;
            char buf[64];
            inet_ntop(AF_INET, &in->sin_addr, buf, sizeof(buf));
            strncpy(iface->addrs[iface->naddrs].addr, buf, 63);

            /* Calculate prefix from netmask */
            if (ifa->ifa_netmask && ifa->ifa_netmask->sa_family == AF_INET) {
                struct sockaddr_in *nm = (struct sockaddr_in *)ifa->ifa_netmask;
                uint32_t mask = ntohl(nm->sin_addr.s_addr);
                int bits = 0;
                while (mask & 0x80000000u) { bits++; mask <<= 1; }
                iface->addrs[iface->naddrs].prefix_len = bits;
            } else {
                iface->addrs[iface->naddrs].prefix_len = -1;
            }
            iface->naddrs++;
        } else if (ifa->ifa_addr->sa_family == AF_INET6 &&
                   iface->naddrs < NEVERC_NET_MAX_IF_ADDRS) {
            struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)ifa->ifa_addr;
            char buf[64];
            inet_ntop(AF_INET6, &in6->sin6_addr, buf, sizeof(buf));
            strncpy(iface->addrs[iface->naddrs].addr, buf, 63);

            if (ifa->ifa_netmask && ifa->ifa_netmask->sa_family == AF_INET6) {
                struct sockaddr_in6 *nm6 =
                    (struct sockaddr_in6 *)ifa->ifa_netmask;
                int bits = 0;
                for (int j = 0; j < 16; j++) {
                    uint8_t b = nm6->sin6_addr.s6_addr[j];
                    while (b & 0x80) { bits++; b <<= 1; }
                    if (b != 0) break;
                }
                iface->addrs[iface->naddrs].prefix_len = bits;
            } else {
                iface->addrs[iface->naddrs].prefix_len = -1;
            }
            iface->naddrs++;
        }
    }

    freeifaddrs(ifap);
    return 0;
}

#endif /* _WIN32 / POSIX */

int neverc_net_interface_by_name(const char *name,
                                  neverc_net_interface_t *out) {
    if (!name || !out) return -1;

    neverc_net_interface_list_t list;
    if (neverc_net_interfaces(&list) != 0) return -1;

    for (int i = 0; i < list.count; i++) {
        if (strcmp(list.ifaces[i].name, name) == 0) {
            *out = list.ifaces[i];
            return 0;
        }
    }
    return -1;
}

int neverc_net_interface_by_index(int index, neverc_net_interface_t *out) {
    if (!out) return -1;

    neverc_net_interface_list_t list;
    if (neverc_net_interfaces(&list) != 0) return -1;

    for (int i = 0; i < list.count; i++) {
        if (list.ifaces[i].index == index) {
            *out = list.ifaces[i];
            return 0;
        }
    }
    return -1;
}

int neverc_net_interface_addrs(neverc_net_ifaddr_t *addrs, int max_addrs) {
    if (!addrs || max_addrs <= 0) return -1;

    neverc_net_interface_list_t list;
    if (neverc_net_interfaces(&list) != 0) return -1;

    int count = 0;
    for (int i = 0; i < list.count && count < max_addrs; i++) {
        for (int j = 0; j < list.ifaces[i].naddrs && count < max_addrs; j++) {
            addrs[count] = list.ifaces[i].addrs[j];
            count++;
        }
    }
    return count;
}
