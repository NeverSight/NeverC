/* SPDX-License-Identifier: GPL-2.0 */
#include <nvkmod.h>
#include <nvk_netlink.h>
#include <nvk_process.h>
#include <nvk_mem.h>
#include <nvk_compat.h>

#define NEVERC_KRT_LOG_TAG "neverc_krt_netlink"
#include <nvk_log.h>

#define NEVERC_KRT_NL_PROTO  26

enum neverc_krt_nl_cmd {
	NEVERC_KRT_CMD_PING    = 1,
	NEVERC_KRT_CMD_VERSION = 2,
	NEVERC_KRT_CMD_PID     = 3,
	NEVERC_KRT_CMD_ECHO    = 4,
};

static struct neverc_krt_nl_sock nl_sock;

static void nl_handler(struct neverc_krt_nl_sock *ns, u32 pid,
		       u32 type, u32 seq,
		       const void *data, u32 len)
{
	switch (type) {
	case NEVERC_KRT_CMD_PING: {
		const char reply[] = "pong";
		neverc_krt_nl_reply(ns, pid, seq, reply, sizeof(reply));
		neverc_krt_log_dbg("ping from pid=%u\n", pid);
		break;
	}

	case NEVERC_KRT_CMD_VERSION: {
		const struct neverc_krt_kernel_info *ki = neverc_krt_kernel_version();
		struct {
			u32 major, minor, patch, android;
		} ver;
		ver.major = ki->major;
		ver.minor = ki->minor;
		ver.patch = ki->patch;
		ver.android = ki->android_version;
		neverc_krt_nl_reply(ns, pid, seq, &ver, sizeof(ver));
		break;
	}

	case NEVERC_KRT_CMD_PID: {
		int my_pid = neverc_krt_current_pid();
		neverc_krt_nl_reply(ns, pid, seq, &my_pid, sizeof(my_pid));
		break;
	}

	case NEVERC_KRT_CMD_ECHO: {
		if (data && len > 0)
			neverc_krt_nl_reply(ns, pid, seq, data, len);
		break;
	}

	default:
		neverc_krt_log_warn("unknown cmd=%u from pid=%u\n", type, pid);
		break;
	}
}

static int neverc_krt_netlink_init(void)
{
	int ret;

	ret = NEVERC_KRT_BOOTSTRAP();
	if (ret)
		return ret;

	neverc_krt_log_info("init on %s\n", NEVERC_KRT_KERNEL_STR);

	neverc_krt_mem_init();
	neverc_krt_process_init();
	neverc_krt_compat_init();

	ret = neverc_krt_nl_init();
	if (ret) {
		neverc_krt_log_err("netlink init failed: %d\n", ret);
		return ret;
	}

	ret = neverc_krt_nl_open(&nl_sock, NEVERC_KRT_NL_PROTO, nl_handler);
	if (ret) {
		neverc_krt_log_err("netlink open failed: %d\n", ret);
		return ret;
	}

	neverc_krt_log_info("listening on proto=%d\n", NEVERC_KRT_NL_PROTO);
	return 0;
}

static void neverc_krt_netlink_exit(void)
{
	neverc_krt_nl_close(&nl_sock);
	neverc_krt_log_info("unloaded\n");
}

module_init(neverc_krt_netlink_init);
module_exit(neverc_krt_netlink_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("NeverC");
MODULE_DESCRIPTION("NeverC netlink IPC demo");

NEVERC_KRT_DEFINE_MODULE("neverc_krt_netlink");
