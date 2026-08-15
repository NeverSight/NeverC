/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_PROFILE_H
#define NEVERC_KRT_PROFILE_H

#include <nvk_profile_ids.h>

/*
 * Pure profile-policy types.  Keep this header independent of Linux headers so
 * the exact-selection contract can be exercised by a normal host C compiler.
 */
enum neverc_krt_ftrace_callback_abi {
  NEVERC_KRT_FTRACE_ABI_UNSUPPORTED = 0,
  NEVERC_KRT_FTRACE_ABI_PT_REGS = NEVERC_KRT_PROFILE_FTRACE_CALLBACK_PT_REGS,
  NEVERC_KRT_FTRACE_ABI_FTRACE_REGS =
      NEVERC_KRT_PROFILE_FTRACE_CALLBACK_FTRACE_REGS,
};

enum neverc_krt_filldir_abi {
  NEVERC_KRT_FILLDIR_ABI_UNSUPPORTED = 0,
  NEVERC_KRT_FILLDIR_ABI_RETURNS_INT = NEVERC_KRT_PROFILE_FILLDIR_RETURNS_INT,
  NEVERC_KRT_FILLDIR_ABI_RETURNS_BOOL = NEVERC_KRT_PROFILE_FILLDIR_RETURNS_BOOL,
};

enum neverc_krt_kallsyms_iter_abi {
  NEVERC_KRT_KALLSYMS_ABI_UNSUPPORTED = 0,
  NEVERC_KRT_KALLSYMS_ABI_WITH_MODULE = NEVERC_KRT_PROFILE_KALLSYMS_WITH_MODULE,
  NEVERC_KRT_KALLSYMS_ABI_ADDRESS_ONLY =
      NEVERC_KRT_PROFILE_KALLSYMS_ADDRESS_ONLY,
};

enum neverc_krt_do_mmap_abi {
  NEVERC_KRT_DO_MMAP_ABI_UNSUPPORTED = 0,
  NEVERC_KRT_DO_MMAP_ABI_WITHOUT_VM_FLAGS =
      NEVERC_KRT_PROFILE_DO_MMAP_WITHOUT_VM_FLAGS,
  NEVERC_KRT_DO_MMAP_ABI_WITH_VM_FLAGS =
      NEVERC_KRT_PROFILE_DO_MMAP_WITH_VM_FLAGS,
};

struct neverc_krt_runtime_caps {
  enum neverc_krt_ftrace_callback_abi ftrace_callback_abi;
  enum neverc_krt_filldir_abi filldir_abi;
  enum neverc_krt_kallsyms_iter_abi kallsyms_iter_abi;
  enum neverc_krt_do_mmap_abi do_mmap_abi;
  unsigned char has_ftrace_registration_api;
};

struct neverc_krt_profile {
  unsigned int legacy_id;
  unsigned int linux_major;
  unsigned int linux_minor;
  unsigned int linux_patch;
  unsigned int android_release;
  unsigned int kmi_generation;
  unsigned int page_shift;
  const char *release_token;
  unsigned long kimage_vaddr;
  unsigned char kcfi_mode;
  struct neverc_krt_runtime_caps caps;
};

struct neverc_krt_observed_identity {
  unsigned int linux_major;
  unsigned int linux_minor;
  unsigned int linux_patch;
  unsigned int android_release;
  unsigned int kmi_generation;
  unsigned int page_shift;
  const char *release_token;
  unsigned long release_token_length;
  unsigned char has_android_identity;
};

/*
 * Exact means the numeric KMI identity matches: Linux patch, Android
 * generation, KMI, and page size.  The release token is deliberately not part
 * of Exact, so OEM / git / -dirty suffixes do not demote the match.
 * Compatible is available only to an explicitly selected build profile and
 * always requires the same Linux major.minor series and page size.
 * Compatible also requires the Android generation when the banner names
 * one; Linux patch, KMI, and release token are ignored.  Exact and Compatible
 * both activate the selected family's complete default runtime layout.  A
 * token-exact certificate may overlay that layout but is never an enable gate.
 * A dedicated compile family is not a leftover overlay.  Callers without
 * an explicit profile must continue to require a token-exact identity.
 */
enum neverc_krt_profile_match {
  NEVERC_KRT_PROFILE_MATCH_NONE = 0,
  NEVERC_KRT_PROFILE_MATCH_COMPATIBLE = 1,
  NEVERC_KRT_PROFILE_MATCH_EXACT = 2,
};

static inline int neverc_krt_profile_match_uses_family_layout(
    enum neverc_krt_profile_match match) {
  return match == NEVERC_KRT_PROFILE_MATCH_COMPATIBLE ||
         match == NEVERC_KRT_PROFILE_MATCH_EXACT;
}

static inline const struct neverc_krt_profile *
neverc_krt_find_profile_in_table(const struct neverc_krt_profile *profiles,
                                 unsigned long count, unsigned int legacy_id) {
  unsigned long i;

  for (i = 0; i < count; i++) {
    if (profiles[i].legacy_id == legacy_id)
      return &profiles[i];
  }
  return (const struct neverc_krt_profile *)0;
}
const struct neverc_krt_profile *
neverc_krt_find_profile(unsigned int legacy_id);
const struct neverc_krt_profile *neverc_krt_find_profile_by_identity(
    unsigned int linux_major, unsigned int linux_minor,
    unsigned int linux_patch, unsigned int android_release,
    unsigned int kmi_generation, unsigned int page_shift,
    const char *release_token, unsigned long release_token_length);
enum neverc_krt_profile_match
neverc_krt_match_profile(const struct neverc_krt_profile *profile,
                         const struct neverc_krt_observed_identity *identity);
int neverc_krt_parse_banner_identity(
    const char *banner, struct neverc_krt_observed_identity *identity);
/*
 * Build the loader vermagic string from a linux_banner.  The release
 * token is copied in full; the standard GKI feature flags are appended.
 * Returns 0, or -1/-2/-3 for bad arguments, an unparseable banner, or
 * an output buffer that cannot hold the complete string.
 */
int neverc_krt_format_vermagic_from_banner(const char *banner, char *out,
                                           unsigned long out_size);

/*
 * Certificate overlay identity.  A leftover Android/KMI record may share
 * series + Android + KMI + page with a later patch; overlay still requires
 * a byte-for-byte release token.
 */
struct neverc_krt_certificate_identity {
  unsigned int profile_id;
  unsigned int linux_major;
  unsigned int linux_minor;
  unsigned int android_release;
  unsigned int kmi_generation;
  unsigned int page_shift;
  const char *release_token;
  unsigned long release_token_length;
};

static inline int neverc_krt_release_token_bytes_equal(
    const char *expected, unsigned long expected_length, const char *observed,
    unsigned long observed_length) {
  unsigned long i;

  if (!expected || !observed || !expected_length || !observed_length ||
      expected_length != observed_length)
    return 0;
  for (i = 0; i < observed_length; i++) {
    if (expected[i] != observed[i])
      return 0;
  }
  return 1;
}

static inline int neverc_krt_certificate_identity_on_variant(
    const struct neverc_krt_certificate_identity *certificate,
    const struct neverc_krt_profile *profile,
    const struct neverc_krt_observed_identity *identity) {
  if (!certificate || !profile || !identity)
    return 0;
  return certificate->profile_id == profile->legacy_id &&
         certificate->linux_major == identity->linux_major &&
         certificate->linux_minor == identity->linux_minor &&
         certificate->android_release == identity->android_release &&
         certificate->kmi_generation == identity->kmi_generation &&
         certificate->page_shift == identity->page_shift;
}

static inline const struct neverc_krt_certificate_identity *
neverc_krt_select_certificate_identity(
    const struct neverc_krt_certificate_identity *certificates,
    unsigned long count, const struct neverc_krt_profile *profile,
    const struct neverc_krt_observed_identity *identity) {
  unsigned long i;

  if (!certificates || !profile || !identity)
    return (const struct neverc_krt_certificate_identity *)0;
  for (i = 0; i < count; i++) {
    const struct neverc_krt_certificate_identity *certificate =
        &certificates[i];

    if (!neverc_krt_certificate_identity_on_variant(certificate, profile,
                                                    identity))
      continue;
    if (neverc_krt_release_token_bytes_equal(
            certificate->release_token, certificate->release_token_length,
            identity->release_token, identity->release_token_length))
      return certificate;
  }
  return (const struct neverc_krt_certificate_identity *)0;
}

#endif /* NEVERC_KRT_PROFILE_H */
