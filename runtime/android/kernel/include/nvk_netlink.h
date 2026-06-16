/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_NETLINK_H
#define NEVERC_KRT_NETLINK_H

#include <linux/types.h>
#include <nvk_rt.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <linux/string.h>
#include <nvk_mem.h>

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
};

typedef void *(*neverc_krt_netlink_create_fn)(void *net, int unit,
				       struct neverc_krt_nl_cfg *cfg);
typedef void  (*neverc_krt_netlink_release_fn)(void *sock);
typedef void *(*neverc_krt_alloc_skb_fn)(unsigned int size, u32 gfp);
typedef void  (*neverc_krt_kfree_skb_fn)(void *skb);
typedef unsigned char *(*neverc_krt_skb_put_fn)(void *skb, unsigned int len);
typedef void *(*neverc_krt_nlmsg_put_fn)(void *skb, u32 portid, u32 seq,
				  int type, int payload, int flags);
typedef int   (*neverc_krt_netlink_unicast_fn)(void *ssk, void *skb,
					u32 portid, int nonblock);
typedef int   (*neverc_krt_netlink_broadcast_fn)(void *ssk, void *skb,
					  u32 portid, u32 group,
					  u32 allocation);
typedef void *(*neverc_krt_nlmsg_data_fn)(void *nlh);
typedef void *(*neverc_krt_nlmsg_hdr_fn)(void *skb);

NEVERC_KRT_RT_VAR neverc_krt_netlink_create_fn     _neverc_krt_nl_create;
NEVERC_KRT_RT_VAR neverc_krt_netlink_release_fn    _neverc_krt_nl_release;
NEVERC_KRT_RT_VAR neverc_krt_alloc_skb_fn          _neverc_krt_nl_alloc_skb;
NEVERC_KRT_RT_VAR neverc_krt_kfree_skb_fn          _neverc_krt_nl_kfree_skb;
NEVERC_KRT_RT_VAR neverc_krt_skb_put_fn            _neverc_krt_nl_skb_put;
NEVERC_KRT_RT_VAR neverc_krt_nlmsg_put_fn          _neverc_krt_nl_nlmsg_put;
NEVERC_KRT_RT_VAR neverc_krt_netlink_unicast_fn    _neverc_krt_nl_unicast;
NEVERC_KRT_RT_VAR neverc_krt_netlink_broadcast_fn  _neverc_krt_nl_broadcast;
NEVERC_KRT_RT_VAR neverc_krt_nlmsg_data_fn         _neverc_krt_nl_nlmsg_data;
NEVERC_KRT_RT_VAR neverc_krt_nlmsg_hdr_fn          _neverc_krt_nl_nlmsg_hdr;
NEVERC_KRT_RT_VAR void                     **_neverc_krt_nl_init_net;
NEVERC_KRT_RT_VAR int                        _neverc_krt_nl_inited;

int neverc_krt_nl_init(void);


struct neverc_krt_nl_sock {
	void *sock;
	int   proto;
	void (*handler)(struct neverc_krt_nl_sock *ns, u32 pid,
			u32 type, u32 seq,
			const void *data, u32 len);
};

#define NEVERC_KRT_NL_MAX_SOCKS 4

NEVERC_KRT_RT_VAR void _neverc_krt_nl_dispatch(void *skb);
NEVERC_KRT_RT_VAR struct neverc_krt_nl_sock *_neverc_krt_nl_socks[NEVERC_KRT_NL_MAX_SOCKS];
NEVERC_KRT_RT_VAR int _neverc_krt_nl_sock_count;

struct neverc_krt_nl_sock *_neverc_krt_nl_find_by_proto(int proto);


void _neverc_krt_nl_dispatch(void *skb);


int neverc_krt_nl_open(struct neverc_krt_nl_sock *ns, int proto,
		       void (*handler)(struct neverc_krt_nl_sock *, u32, u32, u32,
				       const void *, u32));


void neverc_krt_nl_close(struct neverc_krt_nl_sock *ns);


int neverc_krt_nl_send(struct neverc_krt_nl_sock *ns, u32 pid,
		       u32 type, u32 seq,
		       const void *data, u32 len);


int neverc_krt_nl_reply(struct neverc_krt_nl_sock *ns, u32 pid,
			u32 seq, const void *data, u32 len);



NEVERC_KRT_RT_VAR u64 _neverc_krt_nl_auth_key;
NEVERC_KRT_RT_VAR u32 _neverc_krt_nl_auth_pid;
NEVERC_KRT_RT_VAR int _neverc_krt_nl_auth_ok;

static __always_inline void neverc_krt_nl_set_auth_key(u64 key)
{
	_neverc_krt_nl_auth_key = key;
}

static __always_inline int neverc_krt_nl_check_auth(u32 pid, u64 token)
{
	if (_neverc_krt_nl_auth_key == 0)
		return 1;
	if (token == _neverc_krt_nl_auth_key) {
		_neverc_krt_nl_auth_pid = pid;
		_neverc_krt_nl_auth_ok = 1;
		return 1;
	}
	if (_neverc_krt_nl_auth_ok && pid == _neverc_krt_nl_auth_pid)
		return 1;
	return 0;
}

static __always_inline void neverc_krt_nl_revoke_auth(void)
{
	_neverc_krt_nl_auth_pid = 0;
	_neverc_krt_nl_auth_ok = 0;
}

#endif /* NEVERC_KRT_NETLINK_H */
