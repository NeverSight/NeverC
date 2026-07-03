/* SPDX-License-Identifier: GPL-2.0 */
#include <nvk.h>
#include <linux/module.h>

typedef void *(*neverc_krt_netlink_create_fn)(void *net, int unit,
					      void *module,
					      struct neverc_krt_nl_cfg *cfg);
typedef void  (*neverc_krt_netlink_release_fn)(void *sock);
typedef void *(*neverc_krt___alloc_skb_fn)(unsigned int size, u32 gfp,
					   int flags, int node);
typedef void  (*neverc_krt_kfree_skb_fn)(void *skb);
typedef unsigned char *(*neverc_krt_skb_put_fn)(void *skb, unsigned int len);
typedef void *(*neverc_krt_nlmsg_put_fn)(void *skb, u32 portid, u32 seq,
					 int type, int payload, int flags);
typedef int   (*neverc_krt_netlink_unicast_fn)(void *ssk, void *skb,
					       u32 portid, int nonblock);
typedef int   (*neverc_krt_netlink_broadcast_fn)(void *ssk, void *skb,
						 u32 portid, u32 group,
						 u32 allocation);
static neverc_krt_netlink_create_fn     _neverc_krt_nl_create;
static neverc_krt_netlink_release_fn    _neverc_krt_nl_release;
static neverc_krt___alloc_skb_fn        _neverc_krt_nl___alloc_skb;
static neverc_krt_kfree_skb_fn          _neverc_krt_nl_kfree_skb;
static neverc_krt_skb_put_fn            _neverc_krt_nl_skb_put;
static neverc_krt_nlmsg_put_fn          _neverc_krt_nl_nlmsg_put;
static neverc_krt_netlink_unicast_fn    _neverc_krt_nl_unicast;
static neverc_krt_netlink_broadcast_fn  _neverc_krt_nl_broadcast;
static void                            **_neverc_krt_nl_init_net;
static int                              _neverc_krt_nl_inited;
static unsigned long                    _neverc_krt_skb_data_off;

static void *_neverc_krt_nl_alloc_skb_compat(unsigned int size, u32 gfp)
{
	if (_neverc_krt_nl___alloc_skb)
		return _neverc_krt_nl___alloc_skb(size, gfp, 0, -1);
	return (void *)0;
}

static void *_neverc_krt_nl_nlmsg_put_compat(void *skb, u32 portid, u32 seq,
					     int type, int payload, int flags)
{
	if (_neverc_krt_nl_nlmsg_put)
		return _neverc_krt_nl_nlmsg_put(skb, portid, seq, type,
						payload, flags);
	if (!_neverc_krt_nl_skb_put) return (void *)0;
	unsigned int aligned = (16 + payload + 3) & ~3U;
	unsigned char *p = _neverc_krt_nl_skb_put(skb, aligned);
	if (!p) return (void *)0;
	u32 *hdr = (u32 *)p;
	hdr[0] = 16 + payload;
	hdr[1] = (u32)((u16)type | ((u16)flags << 16));
	hdr[2] = seq;
	hdr[3] = portid;
	if (aligned > 16U + (unsigned)payload) {
		unsigned char *pad = p + 16 + payload;
		unsigned int n = aligned - 16 - payload;
		unsigned int j;
		for (j = 0; j < n; j++) pad[j] = 0;
	}
	return (void *)p;
}

static u64 _neverc_krt_nl_auth_key;
static u32 _neverc_krt_nl_auth_pid;
static int _neverc_krt_nl_auth_ok;

static struct neverc_krt_nl_sock *_neverc_krt_nl_socks[NEVERC_KRT_NL_MAX_SOCKS];
static int                        _neverc_krt_nl_sock_count;

int neverc_krt_nl_init(void)
{
	if (_neverc_krt_nl_inited) return 0;

	_neverc_krt_nl_create =
		(neverc_krt_netlink_create_fn)NEVERC_KRT_LOOKUP("__netlink_kernel_create");

	_neverc_krt_nl_release =
		(neverc_krt_netlink_release_fn)NEVERC_KRT_LOOKUP("netlink_kernel_release");
	/*
	 * alloc_skb / nlmsg_new are always inline in GKI 5.10–6.18.
	 * __alloc_skb is the only real export.
	 */
	_neverc_krt_nl___alloc_skb =
		(neverc_krt___alloc_skb_fn)NEVERC_KRT_LOOKUP("__alloc_skb");
	/*
	 * kfree_skb was removed from ksymtab in 6.12+.
	 * consume_skb is exported across all GKI 5.10–6.18.
	 */
	_neverc_krt_nl_kfree_skb =
		(neverc_krt_kfree_skb_fn)NEVERC_KRT_LOOKUP("consume_skb");
	if (!_neverc_krt_nl_kfree_skb)
		_neverc_krt_nl_kfree_skb =
			(neverc_krt_kfree_skb_fn)NEVERC_KRT_LOOKUP("kfree_skb");
	_neverc_krt_nl_skb_put =
		(neverc_krt_skb_put_fn)NEVERC_KRT_LOOKUP("skb_put");
	/*
	 * __nlmsg_put is the real export in all GKI 5.10–6.18.
	 * nlmsg_put is always an inline wrapper.
	 */
	_neverc_krt_nl_nlmsg_put =
		(neverc_krt_nlmsg_put_fn)NEVERC_KRT_LOOKUP("__nlmsg_put");
	_neverc_krt_nl_unicast =
		(neverc_krt_netlink_unicast_fn)NEVERC_KRT_LOOKUP("netlink_unicast");
	_neverc_krt_nl_broadcast =
		(neverc_krt_netlink_broadcast_fn)NEVERC_KRT_LOOKUP("netlink_broadcast");
	/*
	 * nlmsg_data / nlmsg_hdr are always inline in GKI 5.10–6.18.
	 * We handle them with direct pointer arithmetic in the dispatch
	 * and send paths, so no lookup is needed.
	 */
	_neverc_krt_nl_init_net =
		(void **)NEVERC_KRT_LOOKUP("init_net");

	if (!_neverc_krt_nl_create || !_neverc_krt_nl_release)
		return -1;

	/*
	 * nlmsg_hdr() is always inline (returns skb->data).
	 * Probe offsetof(struct sk_buff, data) once via alloc + skb_put.
	 */
	if (_neverc_krt_nl___alloc_skb && _neverc_krt_nl_skb_put) {
		void *probe_skb = _neverc_krt_nl_alloc_skb_compat(128,
								  0x14000C0U);
		if (probe_skb) {
			unsigned char *put_ptr =
				_neverc_krt_nl_skb_put(probe_skb, 8);
			if (put_ptr) {
				unsigned long target = (unsigned long)put_ptr;
				unsigned long off;
				for (off = 64; off < 336; off += 8) {
					unsigned long v;
					if (neverc_krt_mem_read(&v,
							(char *)probe_skb + off,
							8))
						continue;
					if (v == target) {
						_neverc_krt_skb_data_off = off;
						break;
					}
				}
			}
			if (_neverc_krt_nl_kfree_skb)
				_neverc_krt_nl_kfree_skb(probe_skb);
		}
	}

	_neverc_krt_nl_inited = 1;
	return 0;
}

static struct neverc_krt_nl_sock *_neverc_krt_nl_find_by_proto(int proto)
{
	int i;
	for (i = 0; i < _neverc_krt_nl_sock_count; i++) {
		if (_neverc_krt_nl_socks[i] && _neverc_krt_nl_socks[i]->proto == proto)
			return _neverc_krt_nl_socks[i];
	}
	return _neverc_krt_nl_sock_count > 0 ? _neverc_krt_nl_socks[0] : (void *)0;
}

static void _neverc_krt_nl_dispatch(void *skb)
{
	struct neverc_krt_nl_sock *ns = (void *)0;
	int i;
	for (i = 0; i < _neverc_krt_nl_sock_count; i++) {
		if (_neverc_krt_nl_socks[i] && _neverc_krt_nl_socks[i]->sock) {
			ns = _neverc_krt_nl_socks[i];
			break;
		}
	}
	void *nlh = (void *)0;
	unsigned char *data;
	u32 pid, type, seq, payload_len;
	u32 *hdr;

	if (!ns || !ns->handler || !skb)
		return;

	if (_neverc_krt_skb_data_off) {
		unsigned long data_ptr;
		if (!neverc_krt_mem_read(&data_ptr,
				(char *)skb + _neverc_krt_skb_data_off, 8))
			nlh = (void *)data_ptr;
	} else {
		/*
		 * Fallback when the skb probe failed at init: scan pointer
		 * slots for a kernel address whose first u32 looks like
		 * a plausible nlmsg_len.
		 */
		unsigned long off;
		for (off = 96; off < 336; off += 8) {
			unsigned long v;
			if (neverc_krt_mem_read(&v, (char *)skb + off, 8))
				continue;
			if (v < 0xFFFF000000000000UL) continue;
			u32 maybe_len;
			if (neverc_krt_mem_read(&maybe_len, (void *)v, 4))
				continue;
			if (maybe_len >= 16 &&
			    maybe_len <= NEVERC_KRT_NL_MSG_MAX) {
				nlh = (void *)v;
				break;
			}
		}
	}

	if (!nlh) return;

	hdr = (u32 *)nlh;
	u32 nlmsg_len  = hdr[0];
	if (nlmsg_len < 16 || nlmsg_len > NEVERC_KRT_NL_MSG_MAX)
		return;
	u32 nlmsg_type = (u16)hdr[1];
	u32 nlmsg_seq  = hdr[2];
	u32 nlmsg_pid  = hdr[3];

	payload_len = nlmsg_len - 16;

	data = (unsigned char *)nlh + 16;

	ns->handler(ns, nlmsg_pid, nlmsg_type, nlmsg_seq,
		    data, payload_len);
}

int neverc_krt_nl_open(struct neverc_krt_nl_sock *ns, int proto,
		       void (*handler)(struct neverc_krt_nl_sock *, u32, u32, u32,
				       const void *, u32))
{
	struct neverc_krt_nl_cfg cfg;
	unsigned char *p = (unsigned char *)&cfg;
	unsigned long i;

	if (!_neverc_krt_nl_inited) return -1;
	if (!_neverc_krt_nl_init_net) return -2;

	for (i = 0; i < sizeof(cfg); i++)
		p[i] = 0;

	cfg.input = _neverc_krt_nl_dispatch;

	ns->proto = proto;
	ns->handler = handler;

	ns->sock = _neverc_krt_nl_create(*_neverc_krt_nl_init_net, proto,
				       THIS_MODULE, &cfg);
	if (!ns->sock)
		return -3;

	if (_neverc_krt_nl_sock_count < NEVERC_KRT_NL_MAX_SOCKS)
		_neverc_krt_nl_socks[_neverc_krt_nl_sock_count++] = ns;

	return 0;
}

void neverc_krt_nl_close(struct neverc_krt_nl_sock *ns)
{
	int i;
	if (!ns || !ns->sock) return;
	_neverc_krt_nl_release(ns->sock);
	ns->sock = (void *)0;

	for (i = 0; i < _neverc_krt_nl_sock_count; i++) {
		if (_neverc_krt_nl_socks[i] == ns) {
			_neverc_krt_nl_socks[i] =
				_neverc_krt_nl_socks[--_neverc_krt_nl_sock_count];
			break;
		}
	}
	(void)0;
}

int neverc_krt_nl_send(struct neverc_krt_nl_sock *ns, u32 pid,
		       u32 type, u32 seq,
		       const void *data, u32 len)
{
	void *skb, *nlh;
	u32 total;

	if (!ns || !ns->sock) return -1;
	if (!_neverc_krt_nl___alloc_skb ||
	    (!_neverc_krt_nl_nlmsg_put && !_neverc_krt_nl_skb_put) ||
	    !_neverc_krt_nl_unicast)
		return -2;

	total = 16 + ((len + 3) & ~3U);
	skb = _neverc_krt_nl_alloc_skb_compat(total, 0x14000C0U /* GFP_KERNEL */);
	if (!skb) return -3;

	nlh = _neverc_krt_nl_nlmsg_put_compat(skb, 0, seq, type, len, 0);
	if (!nlh) {
		if (_neverc_krt_nl_kfree_skb) _neverc_krt_nl_kfree_skb(skb);
		return -4;
	}

	if (data && len > 0) {
		unsigned char *dst = (unsigned char *)nlh + 16;
		const unsigned char *src = (const unsigned char *)data;
		u32 i;
		for (i = 0; i < len; i++)
			dst[i] = src[i];
	}

	return _neverc_krt_nl_unicast(ns->sock, skb, pid, 0);
}

int neverc_krt_nl_reply(struct neverc_krt_nl_sock *ns, u32 pid,
			u32 seq, const void *data, u32 len)
{
	return neverc_krt_nl_send(ns, pid, 0, seq, data, len);
}

void neverc_krt_nl_set_auth_key(u64 key)
{
	_neverc_krt_nl_auth_key = key;
}

int neverc_krt_nl_check_auth(u32 pid, u64 token)
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

void neverc_krt_nl_revoke_auth(void)
{
	_neverc_krt_nl_auth_pid = 0;
	_neverc_krt_nl_auth_ok = 0;
}

