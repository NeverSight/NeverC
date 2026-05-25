R"prelude(
static int neverc_string_compare(string a, string b) {
  int result = 0;
  if (a.data == b.data && a.len == b.len) {

  } else if (NEVERC_UNLIKELY(__neverc_string_invalid(a) || __neverc_string_invalid(b))) {
    result = (a.len > b.len) - (a.len < b.len);
  } else {
    __SIZE_TYPE__ common = a.len < b.len ? a.len : b.len;
    if (common > 0) {
      int cmp = __builtin_memcmp(a.data, b.data, common);
      result = (cmp > 0) - (cmp < 0);
    }
    if (result == 0 && a.len != b.len)
      result = (a.len > b.len) - (a.len < b.len);
  }
  __neverc_string_release2(a, b);
  return result;
}

static int neverc_string_eq(string a, string b) {
  int equal;
  if (a.len != b.len) {
    equal = 0;
  } else if (a.data == b.data) {
    equal = (a.len == 0 || !__neverc_string_invalid(a)) ? 1 : 0;
  } else if (a.len == 0) {
    equal = 1;
  } else if (NEVERC_UNLIKELY(__neverc_string_invalid(a) || __neverc_string_invalid(b))) {
    equal = 0;
  } else {
    equal = __builtin_memcmp(a.data, b.data, a.len) == 0;
  }
  __neverc_string_release2(a, b);
  return equal;
}

static __attribute__((always_inline)) int
__neverc_string_decrypt_equals(string s, const char *enc,
                               __SIZE_TYPE__ len, __SIZE_TYPE__ key) {
  int equal;
  if (s.len != len) {
    equal = 0;
  } else if (len == 0) {
    equal = (s.len == 0) ? 1 : 0;
  } else if (NEVERC_UNLIKELY(__neverc_string_invalid(s))) {
    equal = 0;
  } else {
    volatile __SIZE_TYPE__ vkey = key;
    equal = __neverc_decrypt_match(s.data, 0, enc, 0, len, vkey);
  }
  neverc_string_free(s);
  return equal;
}

static int neverc_string_compare_substr(string a, __SIZE_TYPE__ pos,
                                             __SIZE_TYPE__ count, string b) {
  int result = 0;
  if (NEVERC_UNLIKELY(__neverc_string_invalid(a) || __neverc_string_invalid(b))) {
    result = (a.len > b.len) - (a.len < b.len);
  } else {
    if (pos > a.len)
      pos = a.len;
    __SIZE_TYPE__ remaining = a.len - pos;
    __SIZE_TYPE__ alen = count < remaining ? count : remaining;
    __SIZE_TYPE__ common = alen < b.len ? alen : b.len;
    if (common > 0) {
      int cmp = __builtin_memcmp(a.data + pos, b.data, common);
      result = (cmp > 0) - (cmp < 0);
    }
    if (result == 0 && alen != b.len)
      result = (alen > b.len) - (alen < b.len);
  }
  __neverc_string_release2(a, b);
  return result;
}

static int neverc_string_compare_substr2(string a, __SIZE_TYPE__ pos1,
                                              __SIZE_TYPE__ count1, string b,
                                              __SIZE_TYPE__ pos2,
                                              __SIZE_TYPE__ count2) {
  int result = 0;
  if (NEVERC_UNLIKELY(__neverc_string_invalid(a) || __neverc_string_invalid(b))) {
    result = (a.len > b.len) - (a.len < b.len);
  } else {
    if (pos1 > a.len)
      pos1 = a.len;
    if (pos2 > b.len)
      pos2 = b.len;
    __SIZE_TYPE__ a_remaining = a.len - pos1;
    __SIZE_TYPE__ b_remaining = b.len - pos2;
    __SIZE_TYPE__ alen = count1 < a_remaining ? count1 : a_remaining;
    __SIZE_TYPE__ blen = count2 < b_remaining ? count2 : b_remaining;
    __SIZE_TYPE__ common = alen < blen ? alen : blen;
    if (common > 0) {
      int cmp = __builtin_memcmp(a.data + pos1, b.data + pos2, common);
      result = (cmp > 0) - (cmp < 0);
    }
    if (result == 0 && alen != blen)
      result = (alen > blen) - (alen < blen);
  }
  __neverc_string_release2(a, b);
  return result;
}

static __inline__ int neverc_string_contains_char(string s, char ch) {
  int found = 0;
  if (NEVERC_LIKELY(!__neverc_string_invalid(s) && s.data && s.len))
    found = __builtin_memchr(s.data, (unsigned char)ch, s.len) != 0;
  neverc_string_free(s);
  return found;
}

static __inline__ int neverc_string_starts_with_char(string s, char ch) {
  int matched = 0;
  if (NEVERC_LIKELY(!__neverc_string_invalid(s) && s.data && s.len))
    matched = s.data[0] == ch;
  neverc_string_free(s);
  return matched;
}

static __inline__ int neverc_string_ends_with_char(string s, char ch) {
  int matched = 0;
  if (NEVERC_LIKELY(!__neverc_string_invalid(s) && s.data && s.len))
    matched = s.data[s.len - 1] == ch;
  neverc_string_free(s);
  return matched;
}

static __attribute__((always_inline)) int
__neverc_string_decrypt_compare(string s, const char *enc,
                                __SIZE_TYPE__ len, __SIZE_TYPE__ key) {
  int result = 0;
  if (NEVERC_UNLIKELY(__neverc_string_invalid(s))) {
    result = (s.len > len) - (s.len < len);
  } else {
    volatile __SIZE_TYPE__ vkey = key;
    __SIZE_TYPE__ common = s.len < len ? s.len : len;
    for (__SIZE_TYPE__ i = 0; i < common; i++) {
      unsigned char sb = (unsigned char)s.data[i];
      unsigned char db = (unsigned char)NEVERC_STRING_DECRYPT_BYTE(enc[i], vkey, i);
      if (sb != db) {
        result = (sb > db) - (sb < db);
        goto done;
      }
    }
    if (s.len != len)
      result = (s.len > len) - (s.len < len);
  }
done:
  neverc_string_free(s);
  return result;
}

static __attribute__((always_inline)) int
__neverc_string_decrypt_equals_ic(string s, const char *enc,
                                  __SIZE_TYPE__ len, __SIZE_TYPE__ key) {
  int equal;
  if (s.len != len) {
    equal = 0;
  } else if (len == 0) {
    equal = (s.len == 0) ? 1 : 0;
  } else if (NEVERC_UNLIKELY(__neverc_string_invalid(s))) {
    equal = 0;
  } else {
    volatile __SIZE_TYPE__ vkey = key;
    equal = __neverc_decrypt_match_ic(s.data, 0, enc, 0, len, vkey);
  }
  neverc_string_free(s);
  return equal;
}

static int neverc_string_eq_ic(string a, string b) {
  int equal;
  if (a.len != b.len) {
    equal = 0;
  } else if (a.data == b.data) {
    equal = (a.len == 0 || !__neverc_string_invalid(a)) ? 1 : 0;
  } else if (a.len == 0) {
    equal = 1;
  } else if (NEVERC_UNLIKELY(__neverc_string_invalid(a) || __neverc_string_invalid(b))) {
    equal = 0;
  } else {
    equal = 1;
    for (__SIZE_TYPE__ i = 0; i < a.len; i++) {
      unsigned char ab = (unsigned char)a.data[i];
      unsigned char bb = (unsigned char)b.data[i];
      if (ab == bb)
        continue;
      if (__neverc_ascii_lower(ab) != __neverc_ascii_lower(bb)) {
        equal = 0;
        break;
      }
    }
  }
  __neverc_string_release2(a, b);
  return equal;
}

static int neverc_string_compare_ic(string a, string b) {
  int result = 0;
  if (a.data == b.data && a.len == b.len) {
    goto done;
  }
  if (NEVERC_UNLIKELY(__neverc_string_invalid(a) || __neverc_string_invalid(b))) {
    result = (a.len > b.len) - (a.len < b.len);
    goto done;
  }
  {
    __SIZE_TYPE__ common = a.len < b.len ? a.len : b.len;
    for (__SIZE_TYPE__ i = 0; i < common; i++) {
      unsigned char ab = (unsigned char)a.data[i];
      unsigned char bb = (unsigned char)b.data[i];
      if (ab == bb)
        continue;
      unsigned char ac = __neverc_ascii_lower(ab);
      unsigned char bc = __neverc_ascii_lower(bb);
      if (ac != bc) {
        result = (ac > bc) - (ac < bc);
        goto done;
      }
    }
  }
  if (a.len != b.len)
    result = (a.len > b.len) - (a.len < b.len);

done:
  __neverc_string_release2(a, b);
  return result;
})prelude"
