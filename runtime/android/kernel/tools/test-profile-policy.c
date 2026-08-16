/* SPDX-License-Identifier: GPL-2.0 */
#include "nvk_profile.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

static void check_exact_lookup(void) {
  static const unsigned int supported[] = {
      510, 51013, 515, 51514, 601, 606, 612, 618,
  };
  static const unsigned int aliases[] = {51012, 51513};
  static const unsigned int alias_canonical[] = {510, 515};
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

  for (i = 0; i < sizeof(aliases) / sizeof(aliases[0]); i++) {
    const struct neverc_krt_profile *profile =
        neverc_krt_find_profile(aliases[i]);
    assert(profile != NULL);
    assert(profile->legacy_id == alias_canonical[i]);
  }

  for (i = 0; i < sizeof(unsupported) / sizeof(unsupported[0]); i++)
    assert(neverc_krt_find_profile(unsupported[i]) == NULL);
}

static void check_identity_lookup(void) {
  static const char release[] = "6.12.89-android16-6-maybe-dirty-4k";
  const struct neverc_krt_profile *profile;

  profile = neverc_krt_find_profile_by_identity(6, 12, 89, 16, 6, 12, release,
                                                sizeof(release) - 1);
  assert(profile != NULL && profile->legacy_id == 612);
  assert(neverc_krt_find_profile_by_identity(6, 13, 89, 16, 6, 12, release,
                                             sizeof(release) - 1) == NULL);
  assert(neverc_krt_find_profile_by_identity(6, 12, 90, 16, 6, 12, release,
                                             sizeof(release) - 1) == NULL);
  assert(neverc_krt_find_profile_by_identity(6, 12, 89, 17, 6, 12, release,
                                             sizeof(release) - 1) == NULL);
  assert(neverc_krt_find_profile_by_identity(6, 12, 89, 16, 7, 12, release,
                                             sizeof(release) - 1) == NULL);
  assert(neverc_krt_find_profile_by_identity(6, 12, 89, 16, 6, 14, release,
                                             sizeof(release) - 1) == NULL);
  assert(neverc_krt_find_profile_by_identity(
             6, 12, 89, 16, 6, 12, "6.12.89-android16-6-oem",
             sizeof("6.12.89-android16-6-oem") - 1) == NULL);
}

static void check_selected_profile_compatibility(void) {
  const struct neverc_krt_profile *profile = neverc_krt_find_profile(612);
  struct neverc_krt_observed_identity identity;

  assert(profile != NULL);
  assert(neverc_krt_parse_banner_identity(
             "Linux version 6.12.89-android16-6-maybe-dirty-4k SMP",
             &identity) == 0);
  identity.page_shift = 12;
  assert(neverc_krt_match_profile(profile, &identity) ==
         NEVERC_KRT_PROFILE_MATCH_EXACT);
  assert(neverc_krt_profile_identity_uses_family_layout(profile, &identity));
  assert(neverc_krt_parse_banner_identity(
             "Linux version 6.12.89-android16-6-oem SMP", &identity) == 0);
  identity.page_shift = 12;
  assert(neverc_krt_match_profile(profile, &identity) ==
         NEVERC_KRT_PROFILE_MATCH_EXACT);
  assert(neverc_krt_profile_identity_uses_family_layout(profile, &identity));

  /* A pinned 612 build must keep its family layout on a future patch/KMI. */
  assert(neverc_krt_parse_banner_identity(
             "Linux version 6.12.138-android16-99-oem-4k SMP", &identity) == 0);
  identity.page_shift = 12;
  assert(neverc_krt_match_profile(profile, &identity) ==
         NEVERC_KRT_PROFILE_MATCH_COMPATIBLE);
  assert(neverc_krt_profile_identity_uses_family_layout(profile, &identity));

  assert(neverc_krt_parse_banner_identity(
             "Linux version 6.12.38-android16-5-oem-4k SMP", &identity) == 0);
  identity.page_shift = 12;
  assert(neverc_krt_match_profile(profile, &identity) ==
         NEVERC_KRT_PROFILE_MATCH_COMPATIBLE);
  assert(neverc_krt_parse_banner_identity(
             "Linux version 6.12.50-android15-8-oem-4k SMP", &identity) == 0);
  identity.page_shift = 12;
  assert(neverc_krt_match_profile(profile, &identity) ==
         NEVERC_KRT_PROFILE_MATCH_NONE);

  identity.page_shift = 14;
  assert(neverc_krt_match_profile(profile, &identity) ==
         NEVERC_KRT_PROFILE_MATCH_NONE);

  identity.page_shift = 0;
  assert(neverc_krt_match_profile(profile, &identity) ==
         NEVERC_KRT_PROFILE_MATCH_NONE);

  assert(neverc_krt_parse_banner_identity(
             "Linux version 6.6.56-android15-8-oem-4k SMP", &identity) == 0);
  identity.page_shift = 12;
  assert(neverc_krt_match_profile(profile, &identity) ==
         NEVERC_KRT_PROFILE_MATCH_NONE);

  assert(neverc_krt_parse_banner_identity(
             "Linux version 6.12.38-oem-special SMP", &identity) == 0);
  assert(identity.has_android_identity == 0);
  identity.page_shift = 12;
  assert(neverc_krt_match_profile(profile, &identity) ==
         NEVERC_KRT_PROFILE_MATCH_COMPATIBLE);
  assert(neverc_krt_profile_identity_uses_family_layout(profile, &identity));

  profile = neverc_krt_find_profile(510);
  assert(profile != NULL);
  assert(neverc_krt_parse_banner_identity(
             "Linux version 5.10.205-android12-9-dirty SMP", &identity) == 0);
  identity.page_shift = 12;
  assert(neverc_krt_match_profile(profile, &identity) ==
         NEVERC_KRT_PROFILE_MATCH_COMPATIBLE);
  assert(neverc_krt_parse_banner_identity(
             "Linux version 5.10.223-android13-4-00011-ga33040a671e2-dirty SMP",
             &identity) == 0);
  identity.page_shift = 12;
  assert(neverc_krt_match_profile(profile, &identity) ==
         NEVERC_KRT_PROFILE_MATCH_NONE);

  profile = neverc_krt_find_profile(515);
  assert(profile != NULL);
  assert(neverc_krt_parse_banner_identity(
             "Linux version 5.15.153-android13-8-oem SMP", &identity) == 0);
  identity.page_shift = 12;
  assert(neverc_krt_match_profile(profile, &identity) ==
         NEVERC_KRT_PROFILE_MATCH_COMPATIBLE);
  assert(neverc_krt_parse_banner_identity(
             "Linux version 5.15.164-android14-11-maybe-dirty SMP",
             &identity) == 0);
  identity.page_shift = 12;
  assert(neverc_krt_match_profile(profile, &identity) ==
         NEVERC_KRT_PROFILE_MATCH_NONE);

  profile = neverc_krt_find_profile(51013);
  assert(profile != NULL);
  assert(neverc_krt_parse_banner_identity(
             "Linux version 5.10.223-android13-4-00011-ga33040a671e2-dirty SMP",
             &identity) == 0);
  identity.page_shift = 12;
  assert(neverc_krt_match_profile(profile, &identity) ==
         NEVERC_KRT_PROFILE_MATCH_EXACT);
  assert(neverc_krt_parse_banner_identity(
             "Linux version 5.10.223-android13-4-gdeadbeef SMP", &identity) ==
         0);
  identity.page_shift = 12;
  assert(neverc_krt_match_profile(profile, &identity) ==
         NEVERC_KRT_PROFILE_MATCH_EXACT);
  assert(neverc_krt_parse_banner_identity(
             "Linux version 5.10.200-android13-9-oem-special SMP", &identity) ==
         0);
  identity.page_shift = 12;
  assert(neverc_krt_match_profile(profile, &identity) ==
         NEVERC_KRT_PROFILE_MATCH_COMPATIBLE);
  assert(neverc_krt_parse_banner_identity(
             "Linux version 5.10.205-android12-9-dirty SMP", &identity) == 0);
  identity.page_shift = 12;
  assert(neverc_krt_match_profile(profile, &identity) ==
         NEVERC_KRT_PROFILE_MATCH_NONE);
  assert(neverc_krt_parse_banner_identity(
             "Linux version 5.10.223-oem-special SMP", &identity) == 0);
  assert(identity.has_android_identity == 0);
  identity.page_shift = 12;
  assert(neverc_krt_match_profile(profile, &identity) ==
         NEVERC_KRT_PROFILE_MATCH_COMPATIBLE);

  profile = neverc_krt_find_profile(51514);
  assert(profile != NULL);
  assert(neverc_krt_parse_banner_identity(
             "Linux version 5.15.164-android14-11-maybe-dirty SMP",
             &identity) == 0);
  identity.page_shift = 12;
  assert(neverc_krt_match_profile(profile, &identity) ==
         NEVERC_KRT_PROFILE_MATCH_EXACT);
  assert(neverc_krt_parse_banner_identity(
             "Linux version 5.15.100-android14-8-oem-special SMP", &identity) ==
         0);
  identity.page_shift = 12;
  assert(neverc_krt_match_profile(profile, &identity) ==
         NEVERC_KRT_PROFILE_MATCH_COMPATIBLE);
  assert(neverc_krt_parse_banner_identity(
             "Linux version 5.15.153-android13-8-oem SMP", &identity) == 0);
  identity.page_shift = 12;
  assert(neverc_krt_match_profile(profile, &identity) ==
         NEVERC_KRT_PROFILE_MATCH_NONE);
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
  assert(NEVERC_KRT_USER_PTMAP_BACKEND_UNSUPPORTED == 0);
  assert(!neverc_krt_profile_match_uses_family_layout(
      NEVERC_KRT_PROFILE_MATCH_NONE));
  assert(neverc_krt_profile_match_uses_family_layout(
      NEVERC_KRT_PROFILE_MATCH_COMPATIBLE));
  assert(neverc_krt_profile_match_uses_family_layout(
      NEVERC_KRT_PROFILE_MATCH_EXACT));
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
  assert(profile->caps.has_ftrace_registration_api == 0);
  assert(profile->caps.binder_filter_backend ==
         NEVERC_KRT_BINDER_FILTER_BACKEND_TRANSACTION);
  assert(profile->caps.vmalloc_visibility_backend ==
         NEVERC_KRT_VMALLOC_VIS_BACKEND_SEQ_OPERATIONS);
  assert(profile->caps.user_ptmap_backend ==
         NEVERC_KRT_USER_PTMAP_BACKEND_LEGACY_510);

  profile = neverc_krt_find_profile(51013);
  assert(profile->kcfi_mode == 0);
  assert(profile->caps.ftrace_callback_abi == NEVERC_KRT_FTRACE_ABI_PT_REGS);
  assert(profile->caps.filldir_abi == NEVERC_KRT_FILLDIR_ABI_RETURNS_INT);
  assert(profile->caps.has_ftrace_registration_api == 0);
  assert(profile->caps.user_ptmap_backend ==
         NEVERC_KRT_USER_PTMAP_BACKEND_LEGACY_510);

  profile = neverc_krt_find_profile(51514);
  assert(profile->kcfi_mode == 0);
  assert(profile->caps.ftrace_callback_abi ==
         NEVERC_KRT_FTRACE_ABI_FTRACE_REGS);
  assert(profile->caps.filldir_abi == NEVERC_KRT_FILLDIR_ABI_RETURNS_INT);
  assert(profile->caps.has_ftrace_registration_api == 0);
  assert(profile->caps.user_ptmap_backend ==
         NEVERC_KRT_USER_PTMAP_BACKEND_LEGACY_515);

  profile = neverc_krt_find_profile(601);
  assert(profile->kcfi_mode == 1);
  assert(profile->caps.filldir_abi == NEVERC_KRT_FILLDIR_ABI_RETURNS_BOOL);
  assert(profile->caps.kallsyms_iter_abi ==
         NEVERC_KRT_KALLSYMS_ABI_WITH_MODULE);
  assert(profile->caps.user_ptmap_backend ==
         NEVERC_KRT_USER_PTMAP_BACKEND_CLASSIC_601);

  profile = neverc_krt_find_profile(606);
  assert(profile->caps.kallsyms_iter_abi ==
         NEVERC_KRT_KALLSYMS_ABI_ADDRESS_ONLY);
  assert(profile->caps.do_mmap_abi == NEVERC_KRT_DO_MMAP_ABI_WITH_VM_FLAGS);
  assert(profile->caps.binder_filter_backend ==
         NEVERC_KRT_BINDER_FILTER_BACKEND_TRANSACTION);
  assert(profile->caps.vmalloc_visibility_backend ==
         NEVERC_KRT_VMALLOC_VIS_BACKEND_SEQ_OPERATIONS);
  assert(profile->caps.user_ptmap_backend ==
         NEVERC_KRT_USER_PTMAP_BACKEND_CLASSIC_606);

  profile = neverc_krt_find_profile(612);
  assert(profile->kcfi_mode == 2);
  assert(profile->caps.ftrace_callback_abi ==
         NEVERC_KRT_FTRACE_ABI_FTRACE_REGS);
  assert(profile->caps.has_ftrace_registration_api == 0);
  assert(profile->caps.binder_filter_backend ==
         NEVERC_KRT_BINDER_FILTER_BACKEND_TRANSACTION);
  assert(profile->caps.vmalloc_visibility_backend ==
         NEVERC_KRT_VMALLOC_VIS_BACKEND_NAMED_SHOW);
  assert(profile->caps.user_ptmap_backend ==
         NEVERC_KRT_USER_PTMAP_BACKEND_NORMALIZED_612_PLUS);

  profile = neverc_krt_find_profile(618);
  assert(profile->kcfi_mode == 2);
  assert(profile->caps.has_ftrace_registration_api == 0);
  assert(profile->caps.binder_filter_backend ==
         NEVERC_KRT_BINDER_FILTER_BACKEND_TRANSACTION);
  assert(profile->caps.vmalloc_visibility_backend ==
         NEVERC_KRT_VMALLOC_VIS_BACKEND_NAMED_SHOW);
  assert(profile->caps.user_ptmap_backend ==
         NEVERC_KRT_USER_PTMAP_BACKEND_NORMALIZED_612_PLUS);
}

static void check_banner_identity_parsing(void) {
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
  assert(identity.has_android_identity == 1);
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
                                          &identity) == 0);
  assert(identity.has_android_identity == 0);
  assert(neverc_krt_parse_banner_identity("Linux version 6.12.89-vendor16-6",
                                          &identity) == 0);
  assert(identity.has_android_identity == 0);
  assert(neverc_krt_parse_banner_identity(
             "Linux version 6.12.89-android16-6evil", &identity) == 0);
  assert(identity.has_android_identity == 0);
  assert(neverc_krt_parse_banner_identity("Linux version 6.12.89-andr",
                                          &identity) == 0);
  assert(identity.has_android_identity == 0);
  assert(neverc_krt_parse_banner_identity(
             "Linux version 4294967302.12.89-android16-6", &identity) != 0);
  assert(neverc_krt_parse_banner_identity(
             "Linux version 6.12.4294967385-android16-6", &identity) != 0);
  assert(neverc_krt_parse_banner_identity(
             "Linux version 6.12.89-android4294967312-6", &identity) != 0);
  assert(neverc_krt_parse_banner_identity(
             "Linux version 6.12.89-android16-4294967302", &identity) != 0);
}

static void check_vermagic_format_keeps_long_tokens(void) {
  static const char banner[] =
      "Linux version 5.10.223-android13-4-00011-ga33040a671e2-dirty "
      "(build-user@build-host)";
  static const char expected[] =
      "5.10.223-android13-4-00011-ga33040a671e2-dirty "
      "SMP preempt mod_unload modversions aarch64";
  char buf[128];
  char too_small[64];

  assert(neverc_krt_format_vermagic_from_banner(banner, buf, sizeof(buf)) == 0);
  assert(strcmp(buf, expected) == 0);
  assert(neverc_krt_format_vermagic_from_banner(banner, too_small,
                                                sizeof(too_small)) == -3);
  assert(neverc_krt_format_vermagic_from_banner(banner, buf, 0) == -1);
  assert(neverc_krt_format_vermagic_from_banner(NULL, buf, sizeof(buf)) == -1);
  assert(neverc_krt_format_vermagic_from_banner("not a banner", buf,
                                                sizeof(buf)) == -2);
}

static void check_certificate_select_is_token_exact(void) {
  static const char leftover_token[] =
      "6.12.38-android16-5-g8c67d4274c0a-ab14275539-4k";
  static const struct neverc_krt_certificate_identity leftover[] = {
      {
          .profile_id = 612,
          .linux_major = 6,
          .linux_minor = 12,
          .android_release = 16,
          .kmi_generation = 5,
          .page_shift = 12,
          .release_token = leftover_token,
          .release_token_length = sizeof(leftover_token) - 1,
      },
  };
  const struct neverc_krt_profile *profile = neverc_krt_find_profile(612);
  struct neverc_krt_observed_identity identity;

  assert(profile != NULL);
  assert(
      neverc_krt_parse_banner_identity(
          "Linux version 6.12.38-android16-5-g8c67d4274c0a-ab14275539-4k SMP",
          &identity) == 0);
  identity.page_shift = 12;
  assert(neverc_krt_select_certificate_identity(leftover, 1, profile,
                                                &identity) == &leftover[0]);

  assert(neverc_krt_parse_banner_identity(
             "Linux version 6.12.50-android16-5-oem-4k SMP", &identity) == 0);
  identity.page_shift = 12;
  assert(neverc_krt_select_certificate_identity(leftover, 1, profile,
                                                &identity) == NULL);

  assert(neverc_krt_parse_banner_identity(
             "Linux version 6.12.89-android16-6-maybe-dirty-4k SMP",
             &identity) == 0);
  identity.page_shift = 12;
  assert(neverc_krt_select_certificate_identity(leftover, 1, profile,
                                                &identity) == NULL);
}

int main(void) {
  check_exact_lookup();
  check_identity_lookup();
  check_selected_profile_compatibility();
  check_lookup_does_not_depend_on_order();
  check_fail_closed_enums();
  check_capability_contracts();
  check_banner_identity_parsing();
  check_vermagic_format_keeps_long_tokens();
  check_certificate_select_is_token_exact();
  return 0;
}
