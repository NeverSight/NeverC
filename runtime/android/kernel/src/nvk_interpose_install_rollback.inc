/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Shared transactional rollback policy for interpose batch installation.
 *
 * This statement fragment is included by the context and low-level batch
 * implementations and by the host fault-injection fixture.  The includer
 * supplies the NVK_IB_* operations.  Successful items are disabled as one
 * set before any teardown starts, then removed in reverse installation order
 * so stacked targets unwind as a LIFO chain.  Bounded chunks keep kernel stack
 * use fixed.
 */
int nvk_ib_i;
int nvk_ib_next = NVK_IB_COUNT;
int nvk_ib_indices[64];

for (nvk_ib_i = NVK_IB_COUNT - 1; nvk_ib_i >= 0; nvk_ib_i--) {
	if (NVK_IB_IS_SUCCESS(nvk_ib_i))
		NVK_IB_DISABLE(nvk_ib_i);
}
NVK_IB_QUIESCE();

while (nvk_ib_next > 0) {
	int nvk_ib_n = 0;

	while (nvk_ib_next > 0 && nvk_ib_n < 64) {
		nvk_ib_next--;
		if (NVK_IB_IS_SUCCESS(nvk_ib_next))
			nvk_ib_indices[nvk_ib_n++] = nvk_ib_next;
	}
	if (!nvk_ib_n)
		continue;

	NVK_IB_REMOVE_CHUNK(nvk_ib_indices, nvk_ib_n);
	for (nvk_ib_i = 0; nvk_ib_i < nvk_ib_n; nvk_ib_i++) {
		int nvk_ib_index = nvk_ib_indices[nvk_ib_i];

		if (NVK_IB_IS_ACTIVE(nvk_ib_index)) {
			NVK_IB_SET_RESULT(nvk_ib_index, NVK_IB_E_PATCH);
			continue;
		}
		NVK_IB_CLEAR_ORIG(nvk_ib_index);
		NVK_IB_SET_RESULT(nvk_ib_index, NVK_IB_E_ROLLBACK);
	}
}
