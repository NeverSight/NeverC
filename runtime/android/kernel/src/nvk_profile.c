/* SPDX-License-Identifier: GPL-2.0 */
#include "nvk_profile.h"
#include "nvk_profile_table.inc"

const struct neverc_krt_profile *
neverc_krt_find_profile(unsigned int legacy_id) {
  return neverc_krt_find_profile_in_table(
      _neverc_krt_profiles, NEVERC_KRT_PROFILE_COUNT,
      neverc_krt_canonical_legacy_id(legacy_id));
}

static int neverc_krt_release_token_equal(
    const char *expected, const char *observed,
    unsigned long observed_length) {
  unsigned long i;

  if (!expected || !observed || !observed_length)
    return 0;
  for (i = 0; i < observed_length; i++) {
    if (expected[i] == '\0' || expected[i] != observed[i])
      return 0;
  }
  return expected[observed_length] == '\0';
}

enum neverc_krt_profile_match neverc_krt_match_profile(
    const struct neverc_krt_profile *profile,
    const struct neverc_krt_observed_identity *identity) {
  if (!profile || !identity ||
      profile->linux_major != identity->linux_major ||
      profile->linux_minor != identity->linux_minor ||
      !identity->page_shift || profile->page_shift != identity->page_shift)
    return NEVERC_KRT_PROFILE_MATCH_NONE;

  if (identity->has_android_identity &&
      profile->linux_patch == identity->linux_patch &&
      profile->android_release == identity->android_release &&
      profile->kmi_generation == identity->kmi_generation &&
      neverc_krt_release_token_equal(profile->release_token,
                                     identity->release_token,
                                     identity->release_token_length))
    return NEVERC_KRT_PROFILE_MATCH_EXACT;

  return NEVERC_KRT_PROFILE_MATCH_COMPATIBLE;
}

const struct neverc_krt_profile *neverc_krt_find_profile_by_identity(
    unsigned int linux_major, unsigned int linux_minor, unsigned int linux_patch,
    unsigned int android_release, unsigned int kmi_generation,
    unsigned int page_shift, const char *release_token,
    unsigned long release_token_length) {
  unsigned long i;
  struct neverc_krt_observed_identity identity = {
      .linux_major = linux_major,
      .linux_minor = linux_minor,
      .linux_patch = linux_patch,
      .android_release = android_release,
      .kmi_generation = kmi_generation,
      .page_shift = page_shift,
      .release_token = release_token,
      .release_token_length = release_token_length,
      .has_android_identity = 1,
  };

  if (!release_token || !release_token_length)
    return (const struct neverc_krt_profile *)0;
  for (i = 0; i < NEVERC_KRT_PROFILE_COUNT; i++) {
    const struct neverc_krt_profile *profile = &_neverc_krt_profiles[i];

    if (neverc_krt_match_profile(profile, &identity) ==
        NEVERC_KRT_PROFILE_MATCH_EXACT)
      return profile;
  }
  return (const struct neverc_krt_profile *)0;
}

/* -1: no number, -2: overflow. */
static int neverc_krt_parse_uint(const char **cursor, unsigned int *value) {
  const char *p = *cursor;
  unsigned int parsed = 0;

  if (*p < '0' || *p > '9')
    return -1;
  while (*p >= '0' && *p <= '9') {
    unsigned int digit = (unsigned int)(*p - '0');

    if (parsed > (~0U - digit) / 10U)
      return -2;
    parsed = parsed * 10U + digit;
    p++;
  }
  *cursor = p;
  *value = parsed;
  return 0;
}

static int neverc_krt_consume_literal(const char **cursor,
                                      const char *literal) {
  const char *p = *cursor;

  while (*literal) {
    if (*p != *literal)
      return -1;
    p++;
    literal++;
  }
  *cursor = p;
  return 0;
}

int neverc_krt_parse_banner_identity(
    const char *banner, struct neverc_krt_observed_identity *identity) {
  const char *p = banner;
  const char *release_token;
  const char *release_end;
  struct neverc_krt_observed_identity parsed = {0};

  if (!banner || !identity)
    return -1;
  while (*p && (*p < '0' || *p > '9'))
    p++;
  release_token = p;
  if (neverc_krt_parse_uint(&p, &parsed.linux_major) || *p++ != '.' ||
      neverc_krt_parse_uint(&p, &parsed.linux_minor) || *p++ != '.' ||
      neverc_krt_parse_uint(&p, &parsed.linux_patch))
    return -1;

  release_end = p;
  while (*release_end && *release_end != ' ' && *release_end != '\n' &&
         *release_end != '\t')
    release_end++;
  parsed.release_token = release_token;
  parsed.release_token_length = (unsigned long)(release_end - release_token);

  /*
   * OEM kernels are not required to retain the Android/KMI suffix.  Preserve
   * a successfully parsed base Linux version for selected-profile
   * compatibility, while marking the richer identity only when the complete
   * `-androidN-KMI` prefix is well formed.  Numeric overflow is corruption,
   * not a vendor spelling variation, and remains a hard parse failure.
   */
  {
    const char *android = p;
    unsigned int android_release;
    unsigned int kmi_generation;
    int status;

    if (!neverc_krt_consume_literal(&android, "-android")) {
      status = neverc_krt_parse_uint(&android, &android_release);
      if (status == -2)
        return -1;
      if (!status && *android == '-') {
        android++;
        status = neverc_krt_parse_uint(&android, &kmi_generation);
        if (status == -2)
          return -1;
        if (!status && (*android == '\0' || *android == '-' ||
                        *android == ' ' || *android == '\n' ||
                        *android == '\t')) {
          parsed.android_release = android_release;
          parsed.kmi_generation = kmi_generation;
          parsed.has_android_identity = 1;
        }
      }
    }
  }
  *identity = parsed;
  return 0;
}

int neverc_krt_format_vermagic_from_banner(const char *banner, char *out,
                                           unsigned long out_size) {
  static const char *const flags[] = {
      "SMP", "preempt", "mod_unload", "modversions", "aarch64",
  };
  struct neverc_krt_observed_identity identity;
  unsigned long needed;
  unsigned long i;
  unsigned long pos;

  if (!banner || !out || !out_size)
    return -1;
  if (neverc_krt_parse_banner_identity(banner, &identity) ||
      !identity.release_token || !identity.release_token_length)
    return -2;

  needed = identity.release_token_length;
  for (i = 0; i < sizeof(flags) / sizeof(flags[0]); i++) {
    const char *flag = flags[i];
    unsigned long flag_length = 0;

    while (flag[flag_length])
      flag_length++;
    needed += 1UL + flag_length;
  }
  if (needed + 1UL > out_size)
    return -3;

  pos = 0;
  for (i = 0; i < identity.release_token_length; i++)
    out[pos++] = identity.release_token[i];
  for (i = 0; i < sizeof(flags) / sizeof(flags[0]); i++) {
    const char *flag = flags[i];

    out[pos++] = ' ';
    while (*flag)
      out[pos++] = *flag++;
  }
  out[pos] = '\0';
  return 0;
}
