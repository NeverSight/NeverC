/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_SOCKET_H
#define _NVK_LINUX_SOCKET_H

#include <linux/types.h>

#define AF_UNSPEC    0
#define AF_UNIX      1
#define AF_INET      2
#define AF_INET6     10
#define AF_NETLINK   16
#define AF_PACKET    17

#define PF_UNSPEC  AF_UNSPEC
#define PF_UNIX    AF_UNIX
#define PF_INET    AF_INET
#define PF_INET6   AF_INET6
#define PF_NETLINK AF_NETLINK
#define PF_PACKET  AF_PACKET

#define SOL_SOCKET 1

struct sockaddr {
	unsigned short sa_family;
	char sa_data[14];
};

#endif
