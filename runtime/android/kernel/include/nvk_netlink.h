/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVK_NETLINK_H
#define NVK_NETLINK_H

#include <linux/types.h>
#include <nvk_rt.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <linux/string.h>
#include <nvk_mem.h>

#define NVK_NL_PROTO_BASE  25
#define NVK_NL_MSG_MAX     4096
#define NVK_NL_GRP_DEFAULT 1

struct nvk_nl_msg_hdr {
	u32 type;
	u32 seq;
	u32 len;
	u32 pid;
};

#define NVK_NL_HDR_SIZE  sizeof(struct nvk_nl_msg_hdr)

struct nvk_nl_cfg {
	u32 groups;
	u32 flags;
	void (*input)(void *skb);
};

typedef void *(*nvk_netlink_create_fn)(void *net, int unit,
				       struct nvk_nl_cfg *cfg);
typedef void  (*nvk_netlink_release_fn)(void *sock);
typedef void *(*nvk_alloc_skb_fn)(unsigned int size, u32 gfp);
typedef void  (*nvk_kfree_skb_fn)(void *skb);
typedef unsigned char *(*nvk_skb_put_fn)(void *skb, unsigned int len);
typedef void *(*nvk_nlmsg_put_fn)(void *skb, u32 portid, u32 seq,
				  int type, int payload, int flags);
typedef int   (*nvk_netlink_unicast_fn)(void *ssk, void *skb,
					u32 portid, int nonblock);
typedef int   (*nvk_netlink_broadcast_fn)(void *ssk, void *skb,
					  u32 portid, u32 group,
					  u32 allocation);
typedef void *(*nvk_nlmsg_data_fn)(void *nlh);
typedef void *(*nvk_nlmsg_hdr_fn)(void *skb);

NVK_RT_VAR nvk_netlink_create_fn     _nvk_nl_create;
NVK_RT_VAR nvk_netlink_release_fn    _nvk_nl_release;
NVK_RT_VAR nvk_alloc_skb_fn          _nvk_nl_alloc_skb;
NVK_RT_VAR nvk_kfree_skb_fn          _nvk_nl_kfree_skb;
NVK_RT_VAR nvk_skb_put_fn            _nvk_nl_skb_put;
NVK_RT_VAR nvk_nlmsg_put_fn          _nvk_nl_nlmsg_put;
NVK_RT_VAR nvk_netlink_unicast_fn    _nvk_nl_unicast;
NVK_RT_VAR nvk_netlink_broadcast_fn  _nvk_nl_broadcast;
NVK_RT_VAR nvk_nlmsg_data_fn         _nvk_nl_nlmsg_data;
NVK_RT_VAR nvk_nlmsg_hdr_fn          _nvk_nl_nlmsg_hdr;
NVK_RT_VAR void                     **_nvk_nl_init_net;
NVK_RT_VAR int                        _nvk_nl_inited;

int nvk_nl_init(void);


struct nvk_nl_sock {
	void *sock;
	int   proto;
	void (*handler)(struct nvk_nl_sock *ns, u32 pid,
			u32 type, u32 seq,
			const void *data, u32 len);
};

#define NVK_NL_MAX_SOCKS 4

NVK_RT_VAR void _nvk_nl_dispatch(void *skb);
NVK_RT_VAR struct nvk_nl_sock *_nvk_nl_socks[NVK_NL_MAX_SOCKS];
NVK_RT_VAR int _nvk_nl_sock_count;

struct nvk_nl_sock *_nvk_nl_find_by_proto(int proto);


void _nvk_nl_dispatch(void *skb);


int nvk_nl_open(struct nvk_nl_sock *ns, int proto,
		       void (*handler)(struct nvk_nl_sock *, u32, u32, u32,
				       const void *, u32));


void nvk_nl_close(struct nvk_nl_sock *ns);


int nvk_nl_send(struct nvk_nl_sock *ns, u32 pid,
		       u32 type, u32 seq,
		       const void *data, u32 len);


int nvk_nl_reply(struct nvk_nl_sock *ns, u32 pid,
			u32 seq, const void *data, u32 len);



NVK_RT_VAR u64 _nvk_nl_auth_key;
NVK_RT_VAR u32 _nvk_nl_auth_pid;
NVK_RT_VAR int _nvk_nl_auth_ok;

static __always_inline void nvk_nl_set_auth_key(u64 key)
{
	_nvk_nl_auth_key = key;
}

static __always_inline int nvk_nl_check_auth(u32 pid, u64 token)
{
	if (_nvk_nl_auth_key == 0)
		return 1;
	if (token == _nvk_nl_auth_key) {
		_nvk_nl_auth_pid = pid;
		_nvk_nl_auth_ok = 1;
		return 1;
	}
	if (_nvk_nl_auth_ok && pid == _nvk_nl_auth_pid)
		return 1;
	return 0;
}

static __always_inline void nvk_nl_revoke_auth(void)
{
	_nvk_nl_auth_pid = 0;
	_nvk_nl_auth_ok = 0;
}

#endif /* NVK_NETLINK_H */
