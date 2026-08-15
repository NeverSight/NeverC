/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Shared remove_ctx_many policy.
 *
 * This statement fragment is included by the production implementation and
 * the host fault-injection fixture.  The includer provides the NVK_RM_*
 * operations so the state machine is tested without compiling ARM64 stubs on
 * the host.
 */
int i;
int ret = NVK_RM_OK;
unsigned char restored[64];
unsigned char drained[64];
int n = count;
int off = 0;

if (!list || count <= 0)
	return NVK_RM_OK;

/* Disable the complete batch before waiting on any chunk.  Otherwise a
 * handler in item 65+ can remain enabled while chunk 0 waits for the whole
 * list, deadlocking teardown before that later item is ever disabled. */
for (i = 0; i < count; i++) {
	NVK_RM_CTX_TYPE *h = NVK_RM_CTX_AT(i);

	if (NVK_RM_IS_ACTIVE(h))
		NVK_RM_DISABLE(h);
}
NVK_RM_BARRIER();
NVK_RM_WAKE();

/* Restore/release in chunks of 64 to keep the bitmap bounded. */
while (off < count) {
	n = count - off;
	if (n > 64)
		n = 64;
	for (i = 0; i < n; i++)
		restored[i] = 0;
	for (i = 0; i < n; i++)
		drained[i] = 0;

	for (i = 0; i < n; i++) {
		NVK_RM_CTX_TYPE *h = NVK_RM_CTX_AT(off + i);

		if (!NVK_RM_IS_ACTIVE(h))
			continue;
		if (NVK_RM_RESTORE(h) == 0)
			restored[i] = 1;
		else {
			/* The target still branches through this retained context, but
			 * unload has begun: keep the bypass-to-trampoline path armed and
			 * do not admit new business handlers during the shared drain. */
			NVK_RM_DISABLE(h);
			ret = NVK_RM_E_PATCH;
		}
	}

	for (i = 0; i < n; i++) {
		NVK_RM_CTX_TYPE *h = NVK_RM_CTX_AT(off + i);

		if (!NVK_RM_IS_ACTIVE(h))
			continue;
		if (NVK_RM_DRAIN(h) == NVK_RM_OK)
			drained[i] = 1;
		else
			ret = NVK_RM_E_PATCH;
	}

	/* Guard ownership belongs to an in-flight handler/CALL replacement.  Do
	 * not clear slots at disable time: a pre-disable entrant can still use
	 * them for recursion suppression.  Exact drain makes this clear safe and
	 * idempotent for both restored and retained-failure contexts. */
	for (i = 0; i < n; i++) {
		NVK_RM_CTX_TYPE *h = NVK_RM_CTX_AT(off + i);

		if (NVK_RM_IS_ACTIVE(h) && drained[i])
			NVK_RM_CLEAR_GUARD(h);
	}

	for (i = 0; i < n; i++) {
		NVK_RM_CTX_TYPE *h = NVK_RM_CTX_AT(off + i);

		if (!h || !restored[i] || !drained[i])
			continue;
		NVK_RM_RELEASE(h);
	}
	off += n;
}
