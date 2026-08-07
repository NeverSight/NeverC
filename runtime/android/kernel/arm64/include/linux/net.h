/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_NET_H
#define _NEVERC_KRT_LINUX_NET_H

#include <linux/types.h>
#include <nvkmod_version.h>

struct socket; /* opaque */
struct net;    /* opaque */

#define SOCK_STREAM 1
#define SOCK_DGRAM  2
#define SOCK_RAW    3

#ifdef NEVERC_KRT_NON_KMI_API
int sock_create(int family, int type, int protocol, struct socket **res);
#endif
int sock_create_kern(struct net *net, int family, int type, int protocol,
		     struct socket **res);
void sock_release(struct socket *sock);
int kernel_connect(struct socket *sock, void *addr, int addrlen, int flags);
int kernel_bind(struct socket *sock, void *addr, int addrlen);
#if NEVERC_KRT_LINUX_AT_LEAST(6, 1, 0) || defined(NEVERC_KRT_NON_KMI_API)
int kernel_listen(struct socket *sock, int backlog);
#endif
int kernel_sendmsg(struct socket *sock, void *msg, void *vec,
		   size_t num, size_t size);
int kernel_recvmsg(struct socket *sock, void *msg, void *vec,
		   size_t num, size_t size, int flags);

#endif
