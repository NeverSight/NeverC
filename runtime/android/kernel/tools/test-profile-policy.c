/* SPDX-License-Identifier: GPL-2.0 */
#include "nvk_profile.h"

#include <assert.h>
#include <stddef.h>

static void check_exact_lookup(void) {
  static const unsigned int supported[] = {
      510, 515, 601, 606, 612, 618,
  };
  static const unsigned int unsupported[] = {
      0, 509, 511, 613, 620, 700,
  };
  size_t i;

  for (i = 0; i < sizeof(supported) / sizeof(supported[0]); i++) {
    const struct neverc_krt_profile *profile =
        neverc_krt_find_profile(supported[i]);
    assert(profile != NULL);
    assert(profile->legacy_id == supported[i]);
  }

  for (i = 0; i < sizeof(unsupported) / sizeof(unsupported[0]); i++)
    assert(neverc_krt_find_profile(unsupported[i]) == NULL);
}

static void check_identity_lookup(void) {
  static const char release[] =
      "6.12.89-android16-6-maybe-dirty-4k";
  const struct neverc_krt_profile *profile;

  profile = neverc_krt_find_profile_by_identity(
      6, 12, 89, 16, 6, 12, release, sizeof(release) - 1);
  assert(profile != NULL && profile->legacy_id == 612);
  assert(neverc_krt_find_profile_by_identity(
             6, 13, 89, 16, 6, 12, release, sizeof(release) - 1) == NULL);
  assert(neverc_krt_find_profile_by_identity(
             6, 12, 90, 16, 6, 12, release, sizeof(release) - 1) == NULL);
  assert(neverc_krt_find_profile_by_identity(
             6, 12, 89, 17, 6, 12, release, sizeof(release) - 1) == NULL);
  assert(neverc_krt_find_profile_by_identity(
             6, 12, 89, 16, 7, 12, release, sizeof(release) - 1) == NULL);
  assert(neverc_krt_find_profile_by_identity(
             6, 12, 89, 16, 6, 14, release, sizeof(release) - 1) == NULL);
  assert(neverc_krt_find_profile_by_identity(
             6, 12, 89, 16, 6, 12,
             "6.12.89-android16-6-oem",
             sizeof("6.12.89-android16-6-oem") - 1) == NULL);
}

static void check_lookup_does_not_depend_on_order(void) {
  static const struct neverc_krt_profile shuffled[] = {
      {.legacy_id = 618},
      {.legacy_id = 510},
      {.legacy_id = 606},
  };
  const struct neverc_krt_profile *profile;

  profile = neverc_krt_find_profile_in_table(
      shuffled, sizeof(shuffled) / sizeof(shuffled[0]), 510);
  assert(profile == &shuffled[1]);
  assert(neverc_krt_find_profile_in_table(
             shuffled, sizeof(shuffled) / sizeof(shuffled[0]), 612) == NULL);
}

static void check_fail_closed_enums(void) {
  assert(NEVERC_KRT_FTRACE_ABI_UNSUPPORTED == 0);
  assert(NEVERC_KRT_FILLDIR_ABI_UNSUPPORTED == 0);
  assert(NEVERC_KRT_KALLSYMS_ABI_UNSUPPORTED == 0);
  assert(NEVERC_KRT_DO_MMAP_ABI_UNSUPPORTED == 0);
}

static void check_capability_contracts(void) {
  const struct neverc_krt_profile *profile;

  profile = neverc_krt_find_profile(510);
  assert(profile->kcfi_mode == 0);
  assert(profile->caps.ftrace_callback_abi == NEVERC_KRT_FTRACE_ABI_PT_REGS);
  assert(profile->caps.filldir_abi == NEVERC_KRT_FILLDIR_ABI_RETURNS_INT);
  assert(profile->caps.kallsyms_iter_abi ==
         NEVERC_KRT_KALLSYMS_ABI_WITH_MODULE);
  assert(profile->caps.do_mmap_abi == NEVERC_KRT_DO_MMAP_ABI_WITHOUT_VM_FLAGS);

  profile = neverc_krt_find_profile(601);
  assert(profile->kcfi_mode == 1);
  assert(profile->caps.filldir_abi == NEVERC_KRT_FILLDIR_ABI_RETURNS_BOOL);
  assert(profile->caps.kallsyms_iter_abi ==
         NEVERC_KRT_KALLSYMS_ABI_WITH_MODULE);

  profile = neverc_krt_find_profile(606);
  assert(profile->caps.kallsyms_iter_abi ==
         NEVERC_KRT_KALLSYMS_ABI_ADDRESS_ONLY);
  assert(profile->caps.do_mmap_abi == NEVERC_KRT_DO_MMAP_ABI_WITH_VM_FLAGS);

  profile = neverc_krt_find_profile(612);
  assert(profile->kcfi_mode == 2);
  assert(profile->caps.ftrace_callback_abi ==
         NEVERC_KRT_FTRACE_ABI_FTRACE_REGS);
  assert(profile->caps.has_ftrace_registration_api == 1);

  profile = neverc_krt_find_profile(618);
  assert(profile->kcfi_mode == 2);
  assert(profile->caps.has_ftrace_registration_api == 0);
}

static void check_banner_identity_is_strict(void) {
  struct neverc_krt_observed_identity identity;
  const struct neverc_krt_profile *profile;

  assert(neverc_krt_parse_banner_identity(
             "Linux version 6.12.89-android16-6-maybe-dirty-4k SMP",
             &identity) == 0);
  assert(identity.linux_major == 6);
  assert(identity.linux_minor == 12);
  assert(identity.linux_patch == 89);
  assert(identity.android_release == 16);
  assert(identity.kmi_generation == 6);
  profile = neverc_krt_find_profile_by_identity(
      identity.linux_major, identity.linux_minor, identity.linux_patch,
      identity.android_release, identity.kmi_generation, 12,
      identity.release_token, identity.release_token_length);
  assert(profile != NULL && profile->legacy_id == 612);

  assert(neverc_krt_parse_banner_identity(
             "Linux version 6.13.1-android16-6-vendor", &identity) == 0);
  assert(neverc_krt_find_profile_by_identity(
             identity.linux_major, identity.linux_minor, identity.linux_patch,
             identity.android_release, identity.kmi_generation, 12,
             identity.release_token, identity.release_token_length) == NULL);
  assert(neverc_krt_parse_banner_identity("Linux version 6.12.89-android16",
                                          &identity) != 0);
  assert(neverc_krt_parse_banner_identity("Linux version 6.12.89-vendor16-6",
                                          &identity) != 0);
  assert(neverc_krt_parse_banner_identity(
             "Linux version 6.12.89-android16-6evil", &identity) != 0);
  assert(neverc_krt_parse_banner_identity("Linux version 6.12.89-andr",
                                          &identity) != 0);
  assert(neverc_krt_parse_banner_identity(
             "Linux version 4294967302.12.89-android16-6", &identity) != 0);
  assert(neverc_krt_parse_banner_identity(
             "Linux version 6.12.4294967385-android16-6", &identity) != 0);
  assert(neverc_krt_parse_banner_identity(
             "Linux version 6.12.89-android4294967312-6", &identity) != 0);
  assert(neverc_krt_parse_banner_identity(
             "Linux version 6.12.89-android16-4294967302", &identity) != 0);
}

int main(void) {
  check_exact_lookup();
  check_identity_lookup();
  check_lookup_does_not_depend_on_order();
  check_fail_closed_enums();
  check_capability_contracts();
  check_banner_identity_is_strict();
  return 0;
}
