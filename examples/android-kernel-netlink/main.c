/* SPDX-License-Identifier: GPL-2.0 */
#include <nvkmod.h>
#include <nvk_netlink.h>
#include <nvk_process.h>
#include <nvk_compat.h>

#define NVK_LOG_TAG "nvk_netlink"
#include <nvk_log.h>

#define NVK_NL_PROTO  26

enum nvk_nl_cmd {
	NVK_CMD_PING    = 1,
	NVK_CMD_VERSION = 2,
	NVK_CMD_PID     = 3,
	NVK_CMD_ECHO    = 4,
};

static struct nvk_nl_sock nl_sock;

static void nl_handler(struct nvk_nl_sock *ns, u32 pid,
		       u32 type, u32 seq,
		       const void *data, u32 len)
{
	switch (type) {
	case NVK_CMD_PING: {
		const char reply[] = "pong";
		nvk_nl_reply(ns, pid, seq, reply, sizeof(reply));
		nvk_log_dbg("ping from pid=%u\n", pid);
		break;
	}

	case NVK_CMD_VERSION: {
		const struct nvk_kernel_info *ki = nvk_kernel_version();
		struct {
			u32 major, minor, patch, android;
		} ver;
		ver.major = ki->major;
		ver.minor = ki->minor;
		ver.patch = ki->patch;
		ver.android = ki->android_version;
		nvk_nl_reply(ns, pid, seq, &ver, sizeof(ver));
		break;
	}

	case NVK_CMD_PID: {
		int my_pid = nvk_current_pid();
		nvk_nl_reply(ns, pid, seq, &my_pid, sizeof(my_pid));
		break;
	}

	case NVK_CMD_ECHO: {
		if (data && len > 0)
			nvk_nl_reply(ns, pid, seq, data, len);
		break;
	}

	default:
		nvk_log_warn("unknown cmd=%u from pid=%u\n", type, pid);
		break;
	}
}

static int nvk_netlink_init(void)
{
	int ret;

	ret = NVK_BOOTSTRAP();
	if (ret)
		return ret;

	nvk_log_info("init on %s\n", NVK_KERNEL_STR);

	nvk_mem_init();
	nvk_process_init();
	nvk_compat_init();

	ret = nvk_nl_init();
	if (ret) {
		nvk_log_err("netlink init failed: %d\n", ret);
		return ret;
	}

	ret = nvk_nl_open(&nl_sock, NVK_NL_PROTO, nl_handler);
	if (ret) {
		nvk_log_err("netlink open failed: %d\n", ret);
		return ret;
	}

	nvk_log_info("listening on proto=%d\n", NVK_NL_PROTO);
	return 0;
}

static void nvk_netlink_exit(void)
{
	nvk_nl_close(&nl_sock);
	nvk_log_info("unloaded\n");
}

module_init(nvk_netlink_init);
module_exit(nvk_netlink_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("NeverC");
MODULE_DESCRIPTION("NeverC netlink IPC demo");

NVK_DEFINE_MODULE("nvk_netlink");
