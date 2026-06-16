/* SPDX-License-Identifier: GPL-2.0 */
/* nvk_netlink.c — implementations extracted from nvk_netlink.h. */
#include <nvk.h>

int nvk_nl_init(void)
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

struct nvk_nl_sock *_nvk_nl_find_by_proto(int proto)
{
	int i;
	for (i = 0; i < _nvk_nl_sock_count; i++) {
		if (_nvk_nl_socks[i] && _nvk_nl_socks[i]->proto == proto)
			return _nvk_nl_socks[i];
	}
	return _nvk_nl_sock_count > 0 ? _nvk_nl_socks[0] : (void *)0;
}

void _nvk_nl_dispatch(void *skb)
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
	if (nlmsg_len < 16 || nlmsg_len > NVK_NL_MSG_MAX)
		return;
	u32 nlmsg_type = (u16)hdr[1];
	u32 nlmsg_seq  = hdr[2];
	u32 nlmsg_pid  = hdr[3];

	payload_len = nlmsg_len - 16;

	if (_nvk_nl_nlmsg_data)
		data = (unsigned char *)_nvk_nl_nlmsg_data(nlh);
	else
		data = (unsigned char *)nlh + 16;

	ns->handler(ns, nlmsg_pid, nlmsg_type, nlmsg_seq,
		    data, payload_len);
}

int nvk_nl_open(struct nvk_nl_sock *ns, int proto,
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

void nvk_nl_close(struct nvk_nl_sock *ns)
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

int nvk_nl_send(struct nvk_nl_sock *ns, u32 pid,
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

int nvk_nl_reply(struct nvk_nl_sock *ns, u32 pid,
			u32 seq, const void *data, u32 len)
{
	return nvk_nl_send(ns, pid, 0, seq, data, len);
}

