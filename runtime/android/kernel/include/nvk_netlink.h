/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVK_NETLINK_H
#define NVK_NETLINK_H

#include <linux/types.h>
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

static nvk_netlink_create_fn     _nvk_nl_create;
static nvk_netlink_release_fn    _nvk_nl_release;
static nvk_alloc_skb_fn          _nvk_nl_alloc_skb;
static nvk_kfree_skb_fn          _nvk_nl_kfree_skb;
static nvk_skb_put_fn            _nvk_nl_skb_put;
static nvk_nlmsg_put_fn          _nvk_nl_nlmsg_put;
static nvk_netlink_unicast_fn    _nvk_nl_unicast;
static nvk_netlink_broadcast_fn  _nvk_nl_broadcast;
static nvk_nlmsg_data_fn         _nvk_nl_nlmsg_data;
static nvk_nlmsg_hdr_fn          _nvk_nl_nlmsg_hdr;
static void                     **_nvk_nl_init_net;
static int                        _nvk_nl_inited;

static int nvk_nl_init(void)
{
	if (_nvk_nl_inited) return 0;

	_nvk_nl_create =
		(nvk_netlink_create_fn)NVK_LOOKUP("__netlink_kernel_create");
	if (!_nvk_nl_create)
		_nvk_nl_create =
			(nvk_netlink_create_fn)NVK_LOOKUP("netlink_kernel_create");

	_nvk_nl_release =
		(nvk_netlink_release_fn)NVK_LOOKUP("netlink_kernel_release");
	_nvk_nl_alloc_skb =
		(nvk_alloc_skb_fn)NVK_LOOKUP("nlmsg_new");
	if (!_nvk_nl_alloc_skb)
		_nvk_nl_alloc_skb =
			(nvk_alloc_skb_fn)NVK_LOOKUP("alloc_skb");
	_nvk_nl_kfree_skb =
		(nvk_kfree_skb_fn)NVK_LOOKUP("kfree_skb");
	if (!_nvk_nl_kfree_skb)
		_nvk_nl_kfree_skb =
			(nvk_kfree_skb_fn)NVK_LOOKUP("consume_skb");
	_nvk_nl_skb_put =
		(nvk_skb_put_fn)NVK_LOOKUP("skb_put");
	_nvk_nl_nlmsg_put =
		(nvk_nlmsg_put_fn)NVK_LOOKUP("nlmsg_put");
	_nvk_nl_unicast =
		(nvk_netlink_unicast_fn)NVK_LOOKUP("netlink_unicast");
	_nvk_nl_broadcast =
		(nvk_netlink_broadcast_fn)NVK_LOOKUP("netlink_broadcast");
	_nvk_nl_nlmsg_data =
		(nvk_nlmsg_data_fn)NVK_LOOKUP("nlmsg_data");
	_nvk_nl_nlmsg_hdr =
		(nvk_nlmsg_hdr_fn)NVK_LOOKUP("nlmsg_hdr");
	_nvk_nl_init_net =
		(void **)NVK_LOOKUP("init_net");

	if (!_nvk_nl_create || !_nvk_nl_release)
		return -1;

	_nvk_nl_inited = 1;
	return 0;
}

struct nvk_nl_sock {
	void *sock;
	int   proto;
	void (*handler)(struct nvk_nl_sock *ns, u32 pid,
			u32 type, u32 seq,
			const void *data, u32 len);
};

#define NVK_NL_MAX_SOCKS 4

static void _nvk_nl_dispatch(void *skb);
static struct nvk_nl_sock *_nvk_nl_socks[NVK_NL_MAX_SOCKS];
static int _nvk_nl_sock_count;

static struct nvk_nl_sock *_nvk_nl_find_by_proto(int proto)
{
	int i;
	for (i = 0; i < _nvk_nl_sock_count; i++) {
		if (_nvk_nl_socks[i] && _nvk_nl_socks[i]->proto == proto)
			return _nvk_nl_socks[i];
	}
	return _nvk_nl_sock_count > 0 ? _nvk_nl_socks[0] : (void *)0;
}

static void _nvk_nl_dispatch(void *skb)
{
	struct nvk_nl_sock *ns = (void *)0;
	int i;
	for (i = 0; i < _nvk_nl_sock_count; i++) {
		if (_nvk_nl_socks[i] && _nvk_nl_socks[i]->sock) {
			ns = _nvk_nl_socks[i];
			break;
		}
	}
	void *nlh;
	unsigned char *data;
	u32 pid, type, seq, payload_len;
	u32 *hdr;

	if (!ns || !ns->handler || !skb)
		return;

	if (_nvk_nl_nlmsg_hdr)
		nlh = _nvk_nl_nlmsg_hdr(skb);
	else
		return;

	if (!nlh) return;

	hdr = (u32 *)nlh;
	u32 nlmsg_len  = hdr[0];
	u32 nlmsg_type = (u16)hdr[1];
	u32 nlmsg_flags = (u16)(hdr[1] >> 16);
	u32 nlmsg_seq  = hdr[2];
	u32 nlmsg_pid  = hdr[3];

	(void)nlmsg_flags;

	payload_len = nlmsg_len > 16 ? nlmsg_len - 16 : 0;

	if (_nvk_nl_nlmsg_data)
		data = (unsigned char *)_nvk_nl_nlmsg_data(nlh);
	else
		data = (unsigned char *)nlh + 16;

	ns->handler(ns, nlmsg_pid, nlmsg_type, nlmsg_seq,
		    data, payload_len);
}

static int nvk_nl_open(struct nvk_nl_sock *ns, int proto,
		       void (*handler)(struct nvk_nl_sock *, u32, u32, u32,
				       const void *, u32))
{
	struct nvk_nl_cfg cfg;
	unsigned char *p = (unsigned char *)&cfg;
	unsigned long i;

	if (!_nvk_nl_inited) return -1;
	if (!_nvk_nl_init_net) return -2;

	for (i = 0; i < sizeof(cfg); i++)
		p[i] = 0;

	cfg.input = _nvk_nl_dispatch;

	ns->proto = proto;
	ns->handler = handler;

	ns->sock = _nvk_nl_create(*_nvk_nl_init_net, proto, &cfg);
	if (!ns->sock)
		return -3;

	if (_nvk_nl_sock_count < NVK_NL_MAX_SOCKS)
		_nvk_nl_socks[_nvk_nl_sock_count++] = ns;

	return 0;
}

static void nvk_nl_close(struct nvk_nl_sock *ns)
{
	int i;
	if (!ns || !ns->sock) return;
	_nvk_nl_release(ns->sock);
	ns->sock = (void *)0;

	for (i = 0; i < _nvk_nl_sock_count; i++) {
		if (_nvk_nl_socks[i] == ns) {
			_nvk_nl_socks[i] =
				_nvk_nl_socks[--_nvk_nl_sock_count];
			break;
		}
	}
	(void)0;
}

static int nvk_nl_send(struct nvk_nl_sock *ns, u32 pid,
		       u32 type, u32 seq,
		       const void *data, u32 len)
{
	void *skb, *nlh;
	u32 total;

	if (!ns || !ns->sock) return -1;
	if (!_nvk_nl_alloc_skb || !_nvk_nl_nlmsg_put || !_nvk_nl_unicast)
		return -2;

	total = 16 + ((len + 3) & ~3U);
	skb = _nvk_nl_alloc_skb(total, 0x14000C0U /* GFP_KERNEL */);
	if (!skb) return -3;

	nlh = _nvk_nl_nlmsg_put(skb, 0, seq, type, len, 0);
	if (!nlh) {
		_nvk_nl_kfree_skb(skb);
		return -4;
	}

	if (data && len > 0) {
		void *payload;
		if (_nvk_nl_nlmsg_data)
			payload = _nvk_nl_nlmsg_data(nlh);
		else
			payload = (void *)((unsigned long)nlh + 16);
		const unsigned char *src = (const unsigned char *)data;
		unsigned char *dst = (unsigned char *)payload;
		u32 i;
		for (i = 0; i < len; i++)
			dst[i] = src[i];
	}

	return _nvk_nl_unicast(ns->sock, skb, pid, 0);
}

static int nvk_nl_reply(struct nvk_nl_sock *ns, u32 pid,
			u32 seq, const void *data, u32 len)
{
	return nvk_nl_send(ns, pid, 0, seq, data, len);
}


static u64 _nvk_nl_auth_key;
static u32 _nvk_nl_auth_pid;
static int _nvk_nl_auth_ok;

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
