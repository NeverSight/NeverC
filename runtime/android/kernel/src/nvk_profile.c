/* SPDX-License-Identifier: GPL-2.0 */
#include "nvk_profile.h"
#include "nvk_profile_table.inc"

const struct neverc_krt_profile *
neverc_krt_find_profile(unsigned int legacy_id) {
  return neverc_krt_find_profile_in_table(_neverc_krt_profiles,
                                          NEVERC_KRT_PROFILE_COUNT, legacy_id);
}

const struct neverc_krt_profile *neverc_krt_find_profile_by_identity(
    unsigned int linux_major, unsigned int linux_minor, unsigned int linux_patch,
    unsigned int android_release, unsigned int kmi_generation,
    unsigned int page_shift, const char *release_token,
    unsigned long release_token_length) {
  unsigned long i;

  if (!release_token || !release_token_length)
    return (const struct neverc_krt_profile *)0;
  for (i = 0; i < NEVERC_KRT_PROFILE_COUNT; i++) {
    const struct neverc_krt_profile *profile = &_neverc_krt_profiles[i];
    unsigned long release_index = 0;

    if (profile->linux_major == linux_major &&
        profile->linux_minor == linux_minor &&
        profile->linux_patch == linux_patch &&
        profile->android_release == android_release &&
        profile->kmi_generation == kmi_generation &&
        profile->page_shift == page_shift) {
      while (release_index < release_token_length &&
             profile->release_token[release_index] ==
                 release_token[release_index])
        release_index++;
      if (release_index == release_token_length &&
          profile->release_token[release_index] == '\0')
        return profile;
    }
  }
  return (const struct neverc_krt_profile *)0;
}

static int neverc_krt_parse_uint(const char **cursor, unsigned int *value) {
  const char *p = *cursor;
  unsigned int parsed = 0;

  if (*p < '0' || *p > '9')
    return -1;
  while (*p >= '0' && *p <= '9') {
    unsigned int digit = (unsigned int)(*p - '0');

    if (parsed > (~0U - digit) / 10U)
      return -1;
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
  if (neverc_krt_consume_literal(&p, "-android"))
    return -1;
  if (neverc_krt_parse_uint(&p, &parsed.android_release) || *p++ != '-' ||
      neverc_krt_parse_uint(&p, &parsed.kmi_generation))
    return -1;
  if (*p != '\0' && *p != '-' && *p != ' ')
    return -1;
  while (*p && *p != ' ')
    p++;
  parsed.release_token = release_token;
  parsed.release_token_length = (unsigned long)(p - release_token);
  *identity = parsed;
  return 0;
}
