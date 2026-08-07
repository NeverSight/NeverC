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
  NEVERC_KRT_FILLDIR_ABI_RETURNS_BOOL =
      NEVERC_KRT_PROFILE_FILLDIR_RETURNS_BOOL,
};

enum neverc_krt_kallsyms_iter_abi {
  NEVERC_KRT_KALLSYMS_ABI_UNSUPPORTED = 0,
  NEVERC_KRT_KALLSYMS_ABI_WITH_MODULE =
      NEVERC_KRT_PROFILE_KALLSYMS_WITH_MODULE,
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
  const char *release_token;
  unsigned long release_token_length;
};

static inline const struct neverc_krt_profile *neverc_krt_find_profile_in_table(
    const struct neverc_krt_profile *profiles, unsigned long count,
    unsigned int legacy_id) {
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
    unsigned int linux_major, unsigned int linux_minor, unsigned int linux_patch,
    unsigned int android_release, unsigned int kmi_generation,
    unsigned int page_shift, const char *release_token,
    unsigned long release_token_length);
int neverc_krt_parse_banner_identity(
    const char *banner, struct neverc_krt_observed_identity *identity);

#endif /* NEVERC_KRT_PROFILE_H */
