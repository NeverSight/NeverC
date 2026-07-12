/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_CRED_H
#define _NEVERC_KRT_LINUX_CRED_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <nvkmod_version.h>

/*
 * Opaque struct cred — layout varies across GKI versions.
 *
 *   Field          5.10    5.15    6.1     6.6     6.12    6.18
 *   ────────────────────────────────────────────────────────────
 *   usage          4 (at)  4 (at)  4 (at)  8 (al)  8 (al)  8 (al)
 *   uid..fsgid     32      32      32      32      32      32
 *   securebits     4       4       4       4       4       4
 *   cap_* (×5)     40      40      40      40      40      40
 *   ────────────────────────────────────────────────────────────
 *   at = atomic_t (4 bytes), al = atomic_long_t (8 bytes)
 *
 * The runtime probes uid offset via _neverc_krt_cred_uid_base()
 * which returns 4 (5.10–6.1) or 8 (6.6+).  All further field
 * offsets are computed relative to uid.
 *
 * CONFIG_DEBUG_CREDENTIALS is disabled in GKI builds, so the
 * subscribers/put_addr/magic debug fields are never present.
 */
struct cred;        /* opaque */
struct task_struct; /* opaque */

typedef struct {
	uid_t val;
} kuid_t;

typedef struct {
	gid_t val;
} kgid_t;

/*
 * Capability set — always 8 bytes on arm64.
 *   5.10–6.1: struct kernel_cap_struct { u32 cap[_KERNEL_CAPABILITY_U32S]; }
 *   6.6+:     struct { u64 val; }
 * Both are 8 bytes.  The runtime reads capability bits as raw u32
 * words at probed offsets (_neverc_krt_cred_cap_off), so this
 * compile-time type is only used for stack-local declarations.
 */
typedef struct {
	u32 cap[2];
} kernel_cap_t;

#define CAP_DAC_OVERRIDE   1
#define CAP_DAC_READ_SEARCH 2
#define CAP_SYS_PTRACE     19
#define CAP_SYS_ADMIN      21
#define CAP_SYS_MODULE     16
#define CAP_NET_ADMIN      12
#define CAP_NET_RAW        13

void *neverc_krt_prepare_creds(void);
int neverc_krt_commit_creds(void *cred);

static __always_inline struct cred *prepare_creds(void)
{
	return (struct cred *)neverc_krt_prepare_creds();
}

static __always_inline int commit_creds(struct cred *new_cred)
{
	return neverc_krt_commit_creds(new_cred);
}

/*
 * Cred helper export status (verified from System.map __ksymtab):
 *   prepare_creds  — exported 5.10–6.18
 *   commit_creds   — exported 5.10–6.18
 *   __put_cred     — exported 5.15–6.18
 *   override_creds — exported 5.15–6.12 (removed in 6.18)
 *   revert_creds   — exported 5.15–6.12 (removed in 6.18)
 *   abort_creds    — NEVER exported (5.10–6.18)
 *   put_cred       — NEVER exported (always inline)
 *
 * For cross-version portable credential work, use only
 * prepare_creds + commit_creds.  The runtime's neverc_krt_su_*
 * and neverc_krt_cred_* APIs handle all version differences internally
 * via NEVERC_KRT_LOOKUP.
 */
#if NEVERC_KRT_KERNEL >= 515
void __put_cred(struct cred *cred);
#define abort_creds(cred) __put_cred(cred)
#endif

#if defined(NEVERC_KRT_NON_KMI_API) && NEVERC_KRT_KERNEL >= 515 && \
	NEVERC_KRT_KERNEL < 618
const struct cred *override_creds(const struct cred *new_cred);
void revert_creds(const struct cred *old);
#endif

#define KUIDT_INIT(v) (kuid_t){ .val = (v) }
#define KGIDT_INIT(v) (kgid_t){ .val = (v) }
#define GLOBAL_ROOT_UID KUIDT_INIT(0)
#define GLOBAL_ROOT_GID KGIDT_INIT(0)

#endif /* _NEVERC_KRT_LINUX_CRED_H */
