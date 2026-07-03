/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_NETLINK_H
#define NEVERC_KRT_NETLINK_H

#include <linux/types.h>

#define NEVERC_KRT_NL_PROTO_BASE  25
#define NEVERC_KRT_NL_MSG_MAX     4096
#define NEVERC_KRT_NL_GRP_DEFAULT 1

struct neverc_krt_nl_msg_hdr {
	u32 type;
	u32 seq;
	u32 len;
	u32 pid;
};

#define NEVERC_KRT_NL_HDR_SIZE  sizeof(struct neverc_krt_nl_msg_hdr)

struct neverc_krt_nl_cfg {
	u32 groups;
	u32 flags;
	void (*input)(void *skb);
	/*
	 * Padding to cover the kernel's struct netlink_kernel_cfg tail:
	 *   5.10-6.6: cb_mutex(8) + bind(8) + unbind(8) + compare/release(8) = 32
	 *   6.12-6.18: bind(8) + unbind(8) + release(8)                      = 24
	 * Must be zeroed before passing to __netlink_kernel_create.
	 */
	unsigned char __pad[32];
};

int neverc_krt_nl_init(void);

struct neverc_krt_nl_sock {
	void *sock;
	int   proto;
	void (*handler)(struct neverc_krt_nl_sock *ns, u32 pid,
			u32 type, u32 seq,
			const void *data, u32 len);
};

#define NEVERC_KRT_NL_MAX_SOCKS 4

int neverc_krt_nl_open(struct neverc_krt_nl_sock *ns, int proto,
		       void (*handler)(struct neverc_krt_nl_sock *, u32, u32, u32,
				       const void *, u32));
void neverc_krt_nl_close(struct neverc_krt_nl_sock *ns);
int neverc_krt_nl_send(struct neverc_krt_nl_sock *ns, u32 pid,
		       u32 type, u32 seq,
		       const void *data, u32 len);
int neverc_krt_nl_reply(struct neverc_krt_nl_sock *ns, u32 pid,
			u32 seq, const void *data, u32 len);

void neverc_krt_nl_set_auth_key(u64 key);
int neverc_krt_nl_check_auth(u32 pid, u64 token);
void neverc_krt_nl_revoke_auth(void);

#endif /* NEVERC_KRT_NETLINK_H */
