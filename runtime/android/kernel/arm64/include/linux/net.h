/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_NET_H
#define _NEVERC_KRT_LINUX_NET_H

#include <linux/types.h>

struct socket; /* opaque */

#define SOCK_STREAM 1
#define SOCK_DGRAM  2
#define SOCK_RAW    3

int sock_create(int family, int type, int protocol, struct socket **res);
int sock_create_kern(struct net *net, int family, int type, int protocol,
		     struct socket **res);
void sock_release(struct socket *sock);
int kernel_connect(struct socket *sock, void *addr, int addrlen, int flags);
int kernel_bind(struct socket *sock, void *addr, int addrlen);
int kernel_listen(struct socket *sock, int backlog);
int kernel_sendmsg(struct socket *sock, void *msg, void *vec,
		   size_t num, size_t size);
int kernel_recvmsg(struct socket *sock, void *msg, void *vec,
		   size_t num, size_t size, int flags);

#endif
