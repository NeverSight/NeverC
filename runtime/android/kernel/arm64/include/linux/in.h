/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_IN_H
#define _NVK_LINUX_IN_H

#include <linux/types.h>
#include <linux/socket.h>
#include <asm/byteorder.h>

struct in_addr { __be32 s_addr; };

struct sockaddr_in {
	unsigned short sin_family;
	__be16 sin_port;
	struct in_addr sin_addr;
	unsigned char __pad[8];
};

#define IPPROTO_IP   0
#define IPPROTO_ICMP 1
#define IPPROTO_TCP  6
#define IPPROTO_UDP  17
#define IPPROTO_RAW  255

#define INADDR_ANY       ((unsigned long)0x00000000)
#define INADDR_LOOPBACK  0x7f000001
#define INADDR_BROADCAST ((unsigned long)0xffffffff)

#endif
