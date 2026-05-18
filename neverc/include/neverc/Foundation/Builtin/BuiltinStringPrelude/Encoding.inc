R"prelude(

static __inline__ int __neverc_string_all_ascii(const char *data,
                                                __SIZE_TYPE__ len) {
  __SIZE_TYPE__ i = 0;
  __SIZE_TYPE__ hi_mask = (__SIZE_TYPE__)(-1) / (__SIZE_TYPE__)0xFF
                          * (__SIZE_TYPE__)0x80;
  for (; i + sizeof(__SIZE_TYPE__) <= len; i += sizeof(__SIZE_TYPE__)) {
    __SIZE_TYPE__ word;
    __builtin_memcpy(&word, data + i, sizeof(word));
    if (word & hi_mask)
      return 0;
  }
  for (; i < len; i++) {
    if ((unsigned char)data[i] > 0x7F)
      return 0;
  }
  return 1;
}

#ifndef NEVERC_STRING_UTF8_REPLACEMENT
#define NEVERC_STRING_UTF8_REPLACEMENT ((__UINT32_TYPE__)0xFFFD)
#endif

static __inline__ int __neverc_utf8_is_cont(unsigned char b) {
  return (b & 0xC0) == 0x80;
}

static __inline__ __SIZE_TYPE__
__neverc_utf8_decode(const char *p, __SIZE_TYPE__ avail,
                     __UINT32_TYPE__ *out_cp) {
  __UINT32_TYPE__ cp;
  unsigned char b0;
  if (avail == 0 || !p) {
    if (out_cp)
      *out_cp = NEVERC_STRING_UTF8_REPLACEMENT;
    return 0;
  }
  b0 = (unsigned char)p[0];
  if (b0 < 0x80) {
    if (out_cp)
      *out_cp = b0;
    return 1;
  }
  if (b0 < 0xC2) {

    if (out_cp)
      *out_cp = NEVERC_STRING_UTF8_REPLACEMENT;
    return 0;
  }
  if (b0 < 0xE0) {
    if (avail < 2 || !__neverc_utf8_is_cont((unsigned char)p[1])) {
      if (out_cp)
        *out_cp = NEVERC_STRING_UTF8_REPLACEMENT;
      return 0;
    }
    cp = ((__UINT32_TYPE__)(b0 & 0x1F) << 6) |
         (__UINT32_TYPE__)((unsigned char)p[1] & 0x3F);
    if (out_cp)
      *out_cp = cp;
    return 2;
  }
  if (b0 < 0xF0) {
    unsigned char b1, b2;
    if (avail < 3) {
      if (out_cp)
        *out_cp = NEVERC_STRING_UTF8_REPLACEMENT;
      return 0;
    }
    b1 = (unsigned char)p[1];
    b2 = (unsigned char)p[2];
    if (!__neverc_utf8_is_cont(b1) || !__neverc_utf8_is_cont(b2)) {
      if (out_cp)
        *out_cp = NEVERC_STRING_UTF8_REPLACEMENT;
      return 0;
    }

    if ((b0 == 0xE0 && b1 < 0xA0) || (b0 == 0xED && b1 >= 0xA0)) {
      if (out_cp)
        *out_cp = NEVERC_STRING_UTF8_REPLACEMENT;
      return 0;
    }
    cp = ((__UINT32_TYPE__)(b0 & 0x0F) << 12) |
         ((__UINT32_TYPE__)(b1 & 0x3F) << 6) |
         (__UINT32_TYPE__)(b2 & 0x3F);
    if (out_cp)
      *out_cp = cp;
    return 3;
  }
  if (b0 < 0xF5) {
    unsigned char b1, b2, b3;
    if (avail < 4) {
      if (out_cp)
        *out_cp = NEVERC_STRING_UTF8_REPLACEMENT;
      return 0;
    }
    b1 = (unsigned char)p[1];
    b2 = (unsigned char)p[2];
    b3 = (unsigned char)p[3];
    if (!__neverc_utf8_is_cont(b1) || !__neverc_utf8_is_cont(b2) ||
        !__neverc_utf8_is_cont(b3)) {
      if (out_cp)
        *out_cp = NEVERC_STRING_UTF8_REPLACEMENT;
      return 0;
    }

    if ((b0 == 0xF0 && b1 < 0x90) || (b0 == 0xF4 && b1 >= 0x90)) {
      if (out_cp)
        *out_cp = NEVERC_STRING_UTF8_REPLACEMENT;
      return 0;
    }
    cp = ((__UINT32_TYPE__)(b0 & 0x07) << 18) |
         ((__UINT32_TYPE__)(b1 & 0x3F) << 12) |
         ((__UINT32_TYPE__)(b2 & 0x3F) << 6) |
         (__UINT32_TYPE__)(b3 & 0x3F);
    if (out_cp)
      *out_cp = cp;
    return 4;
  }

  if (out_cp)
    *out_cp = NEVERC_STRING_UTF8_REPLACEMENT;
  return 0;
}

static __inline__ __SIZE_TYPE__
__neverc_utf8_encode(char *out, __UINT32_TYPE__ cp) {
  if (!out)
    return 0;
  if (cp < 0x80) {
    out[0] = (char)cp;
    return 1;
  }
  if (cp < 0x800) {
    out[0] = (char)(0xC0 | (cp >> 6));
    out[1] = (char)(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp < 0x10000) {
    if (cp >= 0xD800 && cp <= 0xDFFF)
      return 0;
    out[0] = (char)(0xE0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
  }
  if (cp <= 0x10FFFF) {
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
  }
  return 0;
}

static __inline__ __SIZE_TYPE__
__neverc_utf8_encoded_len(__UINT32_TYPE__ cp) {
  if (cp < 0x80)
    return 1;
  if (cp < 0x800)
    return 2;
  if (cp < 0x10000) {

    return 3;
  }
  if (cp <= 0x10FFFF)
    return 4;

  return 3;
}

static __SIZE_TYPE__ neverc_string_utf8_count(string s) {
  __SIZE_TYPE__ count = 0;
  if (!__neverc_string_invalid(s) && s.data) {
    if (s.len && __neverc_string_all_ascii(s.data, s.len)) {
      count = s.len;
    } else {
      __SIZE_TYPE__ i = 0;
      while (i < s.len) {
        __UINT32_TYPE__ cp;
        __SIZE_TYPE__ step = __neverc_utf8_decode(s.data + i, s.len - i, &cp);
        i += step ? step : 1;
        count++;
      }
    }
  }
  neverc_string_free(s);
  return count;
}

static int neverc_string_utf8_valid(string s) {
  int valid = 1;
  if (__neverc_string_invalid(s)) {
    valid = 0;
  } else if (s.data && s.len) {
    if (__neverc_string_all_ascii(s.data, s.len)) {
      valid = 1;
    } else {
      __SIZE_TYPE__ i = 0;
      while (i < s.len) {
        __UINT32_TYPE__ cp;
        __SIZE_TYPE__ step = __neverc_utf8_decode(s.data + i, s.len - i, &cp);
        if (step == 0) {
          valid = 0;
          break;
        }
        i += step;
      }
    }
  }
  neverc_string_free(s);
  return valid;
}

static __UINT32_TYPE__ neverc_string_utf8_at(string s, __SIZE_TYPE__ idx) {
  __UINT32_TYPE__ result = 0;
  if (!__neverc_string_invalid(s) && s.data) {
    __SIZE_TYPE__ i = 0;
    __SIZE_TYPE__ k = 0;
    while (i < s.len) {
      __UINT32_TYPE__ cp;
      __SIZE_TYPE__ step = __neverc_utf8_decode(s.data + i, s.len - i, &cp);
      if (k == idx) {
        result = cp;
        break;
      }
      k++;
      i += step ? step : 1;
    }
  }
  neverc_string_free(s);
  return result;
}

static __SIZE_TYPE__
neverc_string_utf8_byte_index(string s, __SIZE_TYPE__ cp_idx) {
  __SIZE_TYPE__ result = 0;
  if (!__neverc_string_invalid(s) && s.data) {
    __SIZE_TYPE__ i = 0;
    __SIZE_TYPE__ k = 0;
    while (i < s.len && k < cp_idx) {
      __UINT32_TYPE__ cp;
      __SIZE_TYPE__ step = __neverc_utf8_decode(s.data + i, s.len - i, &cp);
      i += step ? step : 1;
      k++;
    }
    result = i;
  }
  neverc_string_free(s);
  return result;
}

static string neverc_string_utf8_substr(string s,
                                             __SIZE_TYPE__ cp_start,
                                             __SIZE_TYPE__ cp_count) {
  if (__neverc_string_invalid(s) || s.len == 0)
    return __neverc_string_consume(s);

  __SIZE_TYPE__ byte_start = 0;
  __SIZE_TYPE__ k = 0;
  while (byte_start < s.len && k < cp_start) {
    __UINT32_TYPE__ cp;
    __SIZE_TYPE__ step =
        __neverc_utf8_decode(s.data + byte_start, s.len - byte_start, &cp);
    byte_start += step ? step : 1;
    k++;
  }
  if (byte_start >= s.len || cp_count == 0)
    return __neverc_string_consume(s);

  __SIZE_TYPE__ byte_end = byte_start;
  __SIZE_TYPE__ produced = 0;
  while (byte_end < s.len && produced < cp_count) {
    __UINT32_TYPE__ cp;
    __SIZE_TYPE__ step =
        __neverc_utf8_decode(s.data + byte_end, s.len - byte_end, &cp);
    byte_end += step ? step : 1;
    produced++;
  }
  return __neverc_string_borrow_copy(s, s.data + byte_start,
                                     byte_end - byte_start);
}

static string neverc_string_from_utf32_char(__UINT32_TYPE__ cp) {
  char buf[4];
  __SIZE_TYPE__ n = __neverc_utf8_encode(buf, cp);
  if (n == 0)
    return __neverc_string_empty();
  return __neverc_string_make_owned(buf, n);
}

static string neverc_string_from_utf16(const __UINT16_TYPE__ *data,
                                           __SIZE_TYPE__ len) {
  if (len == 0 || !data)
    return __neverc_string_empty();

  __SIZE_TYPE__ total = 0;
  __SIZE_TYPE__ i = 0;
  while (i < len) {
    __UINT32_TYPE__ cp;
    __UINT16_TYPE__ u = data[i];
    if (u >= 0xD800 && u <= 0xDBFF && i + 1 < len) {
      __UINT16_TYPE__ lo = data[i + 1];
      if (lo >= 0xDC00 && lo <= 0xDFFF) {
        cp = 0x10000 + (((__UINT32_TYPE__)(u - 0xD800)) << 10) +
             (__UINT32_TYPE__)(lo - 0xDC00);
        i += 2;
      } else {
        cp = NEVERC_STRING_UTF8_REPLACEMENT;
        i += 1;
      }
    } else if (u >= 0xD800 && u <= 0xDFFF) {
      cp = NEVERC_STRING_UTF8_REPLACEMENT;
      i += 1;
    } else {
      cp = u;
      i += 1;
    }
    __SIZE_TYPE__ enc = __neverc_utf8_encoded_len(cp);
    if (total > NEVERC_STRING_MAX_LEN - enc)
      return __neverc_string_empty();
    total += enc;
  }

  if (total == 0)
    return __neverc_string_empty();

  char *p = __neverc_string_alloc_payload(total);
  if (!p)
    return __neverc_string_empty();

  __SIZE_TYPE__ written = 0;
  i = 0;
  while (i < len) {
    __UINT32_TYPE__ cp;
    __UINT16_TYPE__ u = data[i];
    if (u >= 0xD800 && u <= 0xDBFF && i + 1 < len) {
      __UINT16_TYPE__ lo = data[i + 1];
      if (lo >= 0xDC00 && lo <= 0xDFFF) {
        cp = 0x10000 + (((__UINT32_TYPE__)(u - 0xD800)) << 10) +
             (__UINT32_TYPE__)(lo - 0xDC00);
        i += 2;
      } else {
        cp = NEVERC_STRING_UTF8_REPLACEMENT;
        i += 1;
      }
    } else if (u >= 0xD800 && u <= 0xDFFF) {
      cp = NEVERC_STRING_UTF8_REPLACEMENT;
      i += 1;
    } else {
      cp = u;
      i += 1;
    }
    __SIZE_TYPE__ enc = __neverc_utf8_encode(p + written, cp);
    if (enc == 0)
      enc = __neverc_utf8_encode(p + written, NEVERC_STRING_UTF8_REPLACEMENT);
    written += enc;
  }
  return __neverc_string_finalize_owned(p, written);
}

static string neverc_string_from_utf32(const __UINT32_TYPE__ *data,
                                           __SIZE_TYPE__ len) {
  if (len == 0 || !data)
    return __neverc_string_empty();

  __SIZE_TYPE__ total = 0;
  for (__SIZE_TYPE__ i = 0; i < len; i++) {
    __UINT32_TYPE__ cp = data[i];
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
      cp = NEVERC_STRING_UTF8_REPLACEMENT;
    __SIZE_TYPE__ enc = __neverc_utf8_encoded_len(cp);
    if (total > NEVERC_STRING_MAX_LEN - enc)
      return __neverc_string_empty();
    total += enc;
  }

  if (total == 0)
    return __neverc_string_empty();

  char *p = __neverc_string_alloc_payload(total);
  if (!p)
    return __neverc_string_empty();

  __SIZE_TYPE__ written = 0;
  for (__SIZE_TYPE__ i = 0; i < len; i++) {
    __UINT32_TYPE__ cp = data[i];
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
      cp = NEVERC_STRING_UTF8_REPLACEMENT;
    __SIZE_TYPE__ enc = __neverc_utf8_encode(p + written, cp);
    if (enc == 0)
      enc = __neverc_utf8_encode(p + written, NEVERC_STRING_UTF8_REPLACEMENT);
    written += enc;
  }
  return __neverc_string_finalize_owned(p, written);
}

static __SIZE_TYPE__ neverc_string_to_utf16(string s,
                                                 __UINT16_TYPE__ *out,
                                                 __SIZE_TYPE__ out_cap) {
  __SIZE_TYPE__ written = 0;
  __SIZE_TYPE__ required = 0;
  if (!__neverc_string_invalid(s) && s.data) {
    __SIZE_TYPE__ i = 0;
    while (i < s.len) {
      __UINT32_TYPE__ cp;
      __SIZE_TYPE__ step = __neverc_utf8_decode(s.data + i, s.len - i, &cp);
      i += step ? step : 1;
      __SIZE_TYPE__ need = (cp <= 0xFFFF) ? 1 : 2;
      required += need;
      if (out && written + need <= out_cap) {
        if (cp <= 0xFFFF) {
          out[written++] = (__UINT16_TYPE__)cp;
        } else {
          __UINT32_TYPE__ rebased = cp - 0x10000;
          out[written++] = (__UINT16_TYPE__)(0xD800 + (rebased >> 10));
          out[written++] = (__UINT16_TYPE__)(0xDC00 + (rebased & 0x3FF));
        }
      }
    }
  }
  neverc_string_free(s);
  return required;
}

static __SIZE_TYPE__ neverc_string_to_utf32(string s,
                                                 __UINT32_TYPE__ *out,
                                                 __SIZE_TYPE__ out_cap) {
  __SIZE_TYPE__ written = 0;
  __SIZE_TYPE__ required = 0;
  if (!__neverc_string_invalid(s) && s.data) {
    __SIZE_TYPE__ i = 0;
    while (i < s.len) {
      __UINT32_TYPE__ cp;
      __SIZE_TYPE__ step = __neverc_utf8_decode(s.data + i, s.len - i, &cp);
      i += step ? step : 1;
      required++;
      if (out && written < out_cap)
        out[written++] = cp;
    }
  }
  neverc_string_free(s);
  return required;
}

static __UINT16_TYPE__ *
neverc_string_to_utf16_owned(string s) {
  __UINT16_TYPE__ *out = (__UINT16_TYPE__ *)0;
  if (__neverc_string_invalid(s)) {
    neverc_string_free(s);
    return out;
  }

  __SIZE_TYPE__ units = 0;
  int overflow = 0;
  if (s.data) {
    __SIZE_TYPE__ i = 0;
    while (i < s.len) {
      __UINT32_TYPE__ cp;
      __SIZE_TYPE__ step = __neverc_utf8_decode(s.data + i, s.len - i, &cp);
      i += step ? step : 1;
      __SIZE_TYPE__ need = (cp <= 0xFFFF) ? 1 : 2;
      if (units > NEVERC_STRING_MAX_LEN - need) {
        overflow = 1;
        break;
      }
      units += need;
    }
  }
  if (overflow) {
    neverc_string_free(s);
    return (__UINT16_TYPE__ *)0;
  }

  if (units > (NEVERC_STRING_MAX_LEN / 2) - 1) {
    neverc_string_free(s);
    return (__UINT16_TYPE__ *)0;
  }
  __SIZE_TYPE__ byte_count = (units + 1) * 2;
  out = (__UINT16_TYPE__ *)NEVERC_STRING_ALLOC(byte_count);
  if (!out) {
    neverc_string_free(s);
    return out;
  }

  __SIZE_TYPE__ written = 0;
  if (s.data) {
    __SIZE_TYPE__ i = 0;
    while (i < s.len) {
      __UINT32_TYPE__ cp;
      __SIZE_TYPE__ step = __neverc_utf8_decode(s.data + i, s.len - i, &cp);
      i += step ? step : 1;
      if (cp <= 0xFFFF) {
        out[written++] = (__UINT16_TYPE__)cp;
      } else {
        __UINT32_TYPE__ rebased = cp - 0x10000;
        out[written++] = (__UINT16_TYPE__)(0xD800 + (rebased >> 10));
        out[written++] = (__UINT16_TYPE__)(0xDC00 + (rebased & 0x3FF));
      }
    }
  }
  out[written] = 0;
  neverc_string_free(s);
  return out;
}

static __UINT32_TYPE__ *
neverc_string_to_utf32_owned(string s) {
  __UINT32_TYPE__ *out = (__UINT32_TYPE__ *)0;
  if (__neverc_string_invalid(s)) {
    neverc_string_free(s);
    return out;
  }
  __SIZE_TYPE__ units = 0;
  if (s.data) {
    __SIZE_TYPE__ i = 0;
    while (i < s.len) {
      __UINT32_TYPE__ cp;
      __SIZE_TYPE__ step = __neverc_utf8_decode(s.data + i, s.len - i, &cp);
      i += step ? step : 1;
      if (units > NEVERC_STRING_MAX_LEN - 1) {
        neverc_string_free(s);
        return (__UINT32_TYPE__ *)0;
      }
      units += 1;
    }
  }

  if (units > (NEVERC_STRING_MAX_LEN / 4) - 1) {
    neverc_string_free(s);
    return (__UINT32_TYPE__ *)0;
  }
  __SIZE_TYPE__ byte_count = (units + 1) * 4;
  out = (__UINT32_TYPE__ *)NEVERC_STRING_ALLOC(byte_count);
  if (!out) {
    neverc_string_free(s);
    return out;
  }
  __SIZE_TYPE__ written = 0;
  if (s.data) {
    __SIZE_TYPE__ i = 0;
    while (i < s.len) {
      __UINT32_TYPE__ cp;
      __SIZE_TYPE__ step = __neverc_utf8_decode(s.data + i, s.len - i, &cp);
      i += step ? step : 1;
      out[written++] = cp;
    }
  }
  out[written] = 0;
  neverc_string_free(s);
  return out;
}

static __WCHAR_TYPE__ *neverc_string_w_str(string s) {
#if __SIZEOF_WCHAR_T__ == 2
  return (__WCHAR_TYPE__ *)neverc_string_to_utf16_owned(s);
#elif __SIZEOF_WCHAR_T__ == 4
  return (__WCHAR_TYPE__ *)neverc_string_to_utf32_owned(s);
#else
  neverc_string_free(s);
  return (__WCHAR_TYPE__ *)0;
#endif
}

static void __neverc_string_wfree_and_null(void **pp) {
  if (*pp) {
    NEVERC_STRING_FREE(*pp);
    *pp = (void *)0;
  }
}
#define neverc_string_wfree(p) __neverc_string_wfree_and_null((void *)&(p))

static void __neverc_wptr_cleanup(void *p) {
  __neverc_string_wfree_and_null((void **)p);
}

static int neverc_string_is_ascii(string s) {
  int ascii = 1;
  if (__neverc_string_invalid(s)) {
    ascii = 0;
  } else if (s.data && s.len) {
    ascii = __neverc_string_all_ascii(s.data, s.len);
  }
  neverc_string_free(s);
  return ascii;
}

static string neverc_string_from_latin1(const char *data,
                                            __SIZE_TYPE__ len) {
  if (len == 0 || !data)
    return __neverc_string_empty();

  if (__neverc_string_all_ascii(data, len))
    return __neverc_string_make_owned(data, len);

  __SIZE_TYPE__ total = 0;
  for (__SIZE_TYPE__ i = 0; i < len; i++) {
    __SIZE_TYPE__ need = ((unsigned char)data[i] < 0x80) ? 1 : 2;
    if (total > NEVERC_STRING_MAX_LEN - need)
      return __neverc_string_empty();
    total += need;
  }

  char *p = __neverc_string_alloc_payload(total);
  if (!p)
    return __neverc_string_empty();

  __SIZE_TYPE__ written = 0;
  for (__SIZE_TYPE__ i = 0; i < len; i++) {
    unsigned char b = (unsigned char)data[i];
    if (b < 0x80) {
      p[written++] = (char)b;
    } else {
      p[written++] = (char)(0xC0 | (b >> 6));
      p[written++] = (char)(0x80 | (b & 0x3F));
    }
  }
  return __neverc_string_finalize_owned(p, written);
}

static __SIZE_TYPE__ neverc_string_to_latin1(string s,
                                                  char *out,
                                                  __SIZE_TYPE__ out_cap) {
  __SIZE_TYPE__ written = 0;
  __SIZE_TYPE__ required = 0;
  if (!__neverc_string_invalid(s) && s.data) {
    __SIZE_TYPE__ i = 0;
    while (i < s.len) {
      __UINT32_TYPE__ cp;
      __SIZE_TYPE__ step = __neverc_utf8_decode(s.data + i, s.len - i, &cp);
      i += step ? step : 1;
      required++;
      if (out && written < out_cap)
        out[written++] = (cp <= 0xFF) ? (char)cp : '?';
    }
  }
  neverc_string_free(s);
  return required;
}

static __inline__ char __neverc_b64_encode_digit(unsigned x) {
  unsigned m = x & 63u;
  if (m < 26u) return (char)('A' + m);
  if (m < 52u) return (char)('a' + (m - 26u));
  if (m < 62u) return (char)('0' + (m - 52u));
  if (m == 62u) return '+';
  return '/';
}

static __inline__ unsigned char __neverc_b64_decode_digit(char c) {
  unsigned char uc = (unsigned char)c;
  if ((unsigned char)(uc - 'A') < 26u) return (unsigned char)(uc - 'A');
  if ((unsigned char)(uc - 'a') < 26u) return (unsigned char)(uc - 'a' + 26);
  if ((unsigned char)(uc - '0') < 10u) return (unsigned char)(uc - '0' + 52);
  if (c == '+') return 62;
  if (c == '/') return 63;
  return 0xFF;
}

static string neverc_string_to_base64(string s) {
  if (__neverc_string_invalid(s) || s.len == 0)
    return __neverc_string_consume(s);
  __SIZE_TYPE__ groups = (s.len + 2u) / 3u;
  if (groups > NEVERC_STRING_MAX_LEN / 4u)
    return __neverc_string_consume(s);
  __SIZE_TYPE__ total = groups * 4u;
  char *p = __neverc_string_alloc_payload(total);
  if (!p)
    return __neverc_string_consume(s);
  __SIZE_TYPE__ in = 0;
  __SIZE_TYPE__ out = 0;
  while (in + 3u <= s.len) {
    unsigned a = (unsigned char)s.data[in];
    unsigned b = (unsigned char)s.data[in + 1];
    unsigned c = (unsigned char)s.data[in + 2];
    p[out++] = __neverc_b64_encode_digit(a >> 2);
    p[out++] = __neverc_b64_encode_digit(((a & 0x03u) << 4) | (b >> 4));
    p[out++] = __neverc_b64_encode_digit(((b & 0x0Fu) << 2) | (c >> 6));
    p[out++] = __neverc_b64_encode_digit(c & 0x3Fu);
    in += 3u;
  }
  __SIZE_TYPE__ rem = s.len - in;
  if (rem == 1u) {
    unsigned a = (unsigned char)s.data[in];
    p[out++] = __neverc_b64_encode_digit(a >> 2);
    p[out++] = __neverc_b64_encode_digit((a & 0x03u) << 4);
    p[out++] = '=';
    p[out++] = '=';
  } else if (rem == 2u) {
    unsigned a = (unsigned char)s.data[in];
    unsigned b = (unsigned char)s.data[in + 1];
    p[out++] = __neverc_b64_encode_digit(a >> 2);
    p[out++] = __neverc_b64_encode_digit(((a & 0x03u) << 4) | (b >> 4));
    p[out++] = __neverc_b64_encode_digit((b & 0x0Fu) << 2);
    p[out++] = '=';
  }
  neverc_string_free(s);
  return __neverc_string_finalize_owned(p, total);
}

static string neverc_string_from_base64(string s) {
  if (__neverc_string_invalid(s) || s.len == 0)
    return __neverc_string_consume(s);
  if ((s.len % 4u) != 0u)
    return __neverc_string_consume(s);

  __SIZE_TYPE__ pad = 0;
  if (s.data[s.len - 1u] == '=') pad++;
  if (s.len >= 2u && s.data[s.len - 2u] == '=') pad++;

  if (pad > 2u)
    return __neverc_string_consume(s);

  __SIZE_TYPE__ groups = s.len / 4u;
  __SIZE_TYPE__ total = groups * 3u - pad;
  char *p = __neverc_string_alloc_payload(total);
  if (!p)
    return __neverc_string_consume(s);

  __SIZE_TYPE__ written = 0;
  for (__SIZE_TYPE__ g = 0; g < groups; g++) {
    char c0 = s.data[g * 4u + 0u];
    char c1 = s.data[g * 4u + 1u];
    char c2 = s.data[g * 4u + 2u];
    char c3 = s.data[g * 4u + 3u];
    int last = (g + 1u == groups);

    if (!last && (c2 == '=' || c3 == '=')) {
      NEVERC_STRING_FREE(p);
      return __neverc_string_consume(s);
    }
    if (last && c2 == '=' && c3 != '=') {
      NEVERC_STRING_FREE(p);
      return __neverc_string_consume(s);
    }

    if (c0 == '=' || c1 == '=') {
      NEVERC_STRING_FREE(p);
      return __neverc_string_consume(s);
    }
    unsigned char d0 = __neverc_b64_decode_digit(c0);
    unsigned char d1 = __neverc_b64_decode_digit(c1);
    unsigned char d2 = (last && c2 == '=') ? 0u
                                           : __neverc_b64_decode_digit(c2);
    unsigned char d3 = (last && c3 == '=') ? 0u
                                           : __neverc_b64_decode_digit(c3);
    if (d0 == 0xFFu || d1 == 0xFFu || d2 == 0xFFu || d3 == 0xFFu) {
      NEVERC_STRING_FREE(p);
      return __neverc_string_consume(s);
    }
    p[written++] = (char)((d0 << 2) | (d1 >> 4));
    if (!(last && c2 == '='))
      p[written++] = (char)(((d1 & 0x0Fu) << 4) | (d2 >> 2));
    if (!(last && c3 == '='))
      p[written++] = (char)(((d2 & 0x03u) << 6) | d3);
  }
  neverc_string_free(s);
  return __neverc_string_finalize_owned(p, written);
}

static __inline__ char __neverc_b64url_encode_digit(unsigned x) {
  unsigned m = x & 63u;
  if (m < 26u) return (char)('A' + m);
  if (m < 52u) return (char)('a' + (m - 26u));
  if (m < 62u) return (char)('0' + (m - 52u));
  if (m == 62u) return '-';
  return '_';
}

static __inline__ unsigned char __neverc_b64url_decode_digit(char c) {
  unsigned char uc = (unsigned char)c;
  if ((unsigned char)(uc - 'A') < 26u) return (unsigned char)(uc - 'A');
  if ((unsigned char)(uc - 'a') < 26u) return (unsigned char)(uc - 'a' + 26);
  if ((unsigned char)(uc - '0') < 10u) return (unsigned char)(uc - '0' + 52);
  if (c == '-') return 62;
  if (c == '_') return 63;
  return 0xFFu;
}

static string neverc_string_to_base64_url(string s) {
  if (__neverc_string_invalid(s) || s.len == 0)
    return __neverc_string_consume(s);
  __SIZE_TYPE__ groups = s.len / 3u;
  __SIZE_TYPE__ rem = s.len - groups * 3u;

  __SIZE_TYPE__ tail = (rem == 0u) ? 0u : (rem == 1u ? 2u : 3u);
  if (groups > (NEVERC_STRING_MAX_LEN - tail) / 4u)
    return __neverc_string_consume(s);
  __SIZE_TYPE__ total = groups * 4u + tail;
  char *p = __neverc_string_alloc_payload(total);
  if (!p)
    return __neverc_string_consume(s);
  __SIZE_TYPE__ in = 0;
  __SIZE_TYPE__ out = 0;
  while (in + 3u <= s.len) {
    unsigned a = (unsigned char)s.data[in];
    unsigned b = (unsigned char)s.data[in + 1];
    unsigned c = (unsigned char)s.data[in + 2];
    p[out++] = __neverc_b64url_encode_digit(a >> 2);
    p[out++] = __neverc_b64url_encode_digit(((a & 0x03u) << 4) | (b >> 4));
    p[out++] = __neverc_b64url_encode_digit(((b & 0x0Fu) << 2) | (c >> 6));
    p[out++] = __neverc_b64url_encode_digit(c & 0x3Fu);
    in += 3u;
  }
  if (rem == 1u) {
    unsigned a = (unsigned char)s.data[in];
    p[out++] = __neverc_b64url_encode_digit(a >> 2);
    p[out++] = __neverc_b64url_encode_digit((a & 0x03u) << 4);
  } else if (rem == 2u) {
    unsigned a = (unsigned char)s.data[in];
    unsigned b = (unsigned char)s.data[in + 1];
    p[out++] = __neverc_b64url_encode_digit(a >> 2);
    p[out++] = __neverc_b64url_encode_digit(((a & 0x03u) << 4) | (b >> 4));
    p[out++] = __neverc_b64url_encode_digit((b & 0x0Fu) << 2);
  }
  neverc_string_free(s);
  return __neverc_string_finalize_owned(p, total);
}

static string neverc_string_from_base64_url(string s) {
  if (__neverc_string_invalid(s) || s.len == 0)
    return __neverc_string_consume(s);

  __SIZE_TYPE__ effective = s.len;
  while (effective > 0u && s.data[effective - 1u] == '=')
    effective--;

  if (effective == 0u)
    return __neverc_string_consume(s);

  if ((effective % 4u) == 1u)
    return __neverc_string_consume(s);

  __SIZE_TYPE__ groups = effective / 4u;
  __SIZE_TYPE__ tail = effective - groups * 4u;
  __SIZE_TYPE__ total = groups * 3u +
                        (tail == 2u ? 1u : (tail == 3u ? 2u : 0u));

  char *p = __neverc_string_alloc_payload(total);
  if (!p)
    return __neverc_string_consume(s);

  __SIZE_TYPE__ written = 0;
  for (__SIZE_TYPE__ g = 0; g < groups; g++) {
    unsigned char d0 = __neverc_b64url_decode_digit(s.data[g * 4u + 0u]);
    unsigned char d1 = __neverc_b64url_decode_digit(s.data[g * 4u + 1u]);
    unsigned char d2 = __neverc_b64url_decode_digit(s.data[g * 4u + 2u]);
    unsigned char d3 = __neverc_b64url_decode_digit(s.data[g * 4u + 3u]);
    if (d0 == 0xFFu || d1 == 0xFFu || d2 == 0xFFu || d3 == 0xFFu) {
      NEVERC_STRING_FREE(p);
      return __neverc_string_consume(s);
    }
    p[written++] = (char)((d0 << 2) | (d1 >> 4));
    p[written++] = (char)(((d1 & 0x0Fu) << 4) | (d2 >> 2));
    p[written++] = (char)(((d2 & 0x03u) << 6) | d3);
  }
  if (tail == 2u) {
    unsigned char d0 = __neverc_b64url_decode_digit(s.data[groups * 4u + 0u]);
    unsigned char d1 = __neverc_b64url_decode_digit(s.data[groups * 4u + 1u]);
    if (d0 == 0xFFu || d1 == 0xFFu) {
      NEVERC_STRING_FREE(p);
      return __neverc_string_consume(s);
    }
    p[written++] = (char)((d0 << 2) | (d1 >> 4));
  } else if (tail == 3u) {
    unsigned char d0 = __neverc_b64url_decode_digit(s.data[groups * 4u + 0u]);
    unsigned char d1 = __neverc_b64url_decode_digit(s.data[groups * 4u + 1u]);
    unsigned char d2 = __neverc_b64url_decode_digit(s.data[groups * 4u + 2u]);
    if (d0 == 0xFFu || d1 == 0xFFu || d2 == 0xFFu) {
      NEVERC_STRING_FREE(p);
      return __neverc_string_consume(s);
    }
    p[written++] = (char)((d0 << 2) | (d1 >> 4));
    p[written++] = (char)(((d1 & 0x0Fu) << 4) | (d2 >> 2));
  }
  neverc_string_free(s);
  return __neverc_string_finalize_owned(p, written);
}

static __inline__ unsigned __neverc_string_hex_digit_value(char c) {
  unsigned char uc = (unsigned char)c;
  if ((unsigned char)(uc - '0') < 10u) return (unsigned)(uc - '0');
  if ((unsigned char)(uc - 'a') < 6u)  return (unsigned)(uc - 'a' + 10);
  if ((unsigned char)(uc - 'A') < 6u)  return (unsigned)(uc - 'A' + 10);
  return 16u;
}

static __inline__ char __neverc_string_hex_digit_char(unsigned x) {
  unsigned m = x & 0xFu;
  return m < 10u ? (char)('0' + m) : (char)('a' + (m - 10u));
}

static string neverc_string_to_hex(string s) {
  if (__neverc_string_invalid(s) || s.len == 0)
    return __neverc_string_consume(s);
  if (s.len > NEVERC_STRING_MAX_LEN / 2u)
    return __neverc_string_consume(s);
  __SIZE_TYPE__ total = s.len * 2u;
  char *p = __neverc_string_alloc_payload(total);
  if (!p)
    return __neverc_string_consume(s);
  for (__SIZE_TYPE__ i = 0; i < s.len; i++) {
    unsigned char b = (unsigned char)s.data[i];
    p[i * 2u + 0u] = __neverc_string_hex_digit_char(b >> 4);
    p[i * 2u + 1u] = __neverc_string_hex_digit_char(b & 0xFu);
  }
  neverc_string_free(s);
  return __neverc_string_finalize_owned(p, total);
}

static string neverc_string_from_hex(string s) {
  if (__neverc_string_invalid(s) || s.len == 0)
    return __neverc_string_consume(s);
  if ((s.len % 2u) != 0u)
    return __neverc_string_consume(s);
  __SIZE_TYPE__ total = s.len / 2u;
  char *p = __neverc_string_alloc_payload(total);
  if (!p)
    return __neverc_string_consume(s);
  for (__SIZE_TYPE__ i = 0; i < total; i++) {
    unsigned hi = __neverc_string_hex_digit_value(s.data[i * 2u + 0u]);
    unsigned lo = __neverc_string_hex_digit_value(s.data[i * 2u + 1u]);
    if (hi == 16u || lo == 16u) {
      NEVERC_STRING_FREE(p);
      return __neverc_string_consume(s);
    }
    p[i] = (char)((hi << 4) | lo);
  }
  neverc_string_free(s);
  return __neverc_string_finalize_owned(p, total);
}

static __inline__ int __neverc_url_unreserved(char c) {
  unsigned char uc = (unsigned char)c;
  return (unsigned char)(uc - 'A') < 26u || (unsigned char)(uc - 'a') < 26u ||
         (unsigned char)(uc - '0') < 10u ||
         c == '-' || c == '_' || c == '.' || c == '~';
}

static __inline__ char __neverc_string_hex_upper_char(unsigned x) {
  unsigned m = x & 0xFu;
  return m < 10u ? (char)('0' + m) : (char)('A' + (m - 10u));
}

static string neverc_string_url_encode(string s) {
  if (__neverc_string_invalid(s) || s.len == 0)
    return __neverc_string_consume(s);
  __SIZE_TYPE__ total = 0;
  for (__SIZE_TYPE__ i = 0; i < s.len; i++) {
    __SIZE_TYPE__ need = __neverc_url_unreserved(s.data[i]) ? 1u : 3u;
    if (total > NEVERC_STRING_MAX_LEN - need)
      return __neverc_string_consume(s);
    total += need;
  }
  char *p = __neverc_string_alloc_payload(total);
  if (!p)
    return __neverc_string_consume(s);
  __SIZE_TYPE__ written = 0;
  for (__SIZE_TYPE__ i = 0; i < s.len; i++) {
    unsigned char c = (unsigned char)s.data[i];
    if (__neverc_url_unreserved((char)c)) {
      p[written++] = (char)c;
    } else {
      p[written++] = '%';
      p[written++] = __neverc_string_hex_upper_char(c >> 4);
      p[written++] = __neverc_string_hex_upper_char(c & 0xFu);
    }
  }
  neverc_string_free(s);
  return __neverc_string_finalize_owned(p, total);
}

static string neverc_string_url_decode(string s) {
  if (__neverc_string_invalid(s) || s.len == 0)
    return __neverc_string_consume(s);
  char *p = __neverc_string_alloc_payload(s.len);
  if (!p)
    return __neverc_string_consume(s);
  __SIZE_TYPE__ written = 0;
  __SIZE_TYPE__ i = 0;
  while (i < s.len) {
    char c = s.data[i];
    if (c == '%') {
      if (i + 2u >= s.len) {
        NEVERC_STRING_FREE(p);
        return __neverc_string_consume(s);
      }
      unsigned hi = __neverc_string_hex_digit_value(s.data[i + 1u]);
      unsigned lo = __neverc_string_hex_digit_value(s.data[i + 2u]);
      if (hi == 16u || lo == 16u) {
        NEVERC_STRING_FREE(p);
        return __neverc_string_consume(s);
      }
      p[written++] = (char)((hi << 4) | lo);
      i += 3u;
    } else {
      p[written++] = c;
      i++;
    }
  }
  neverc_string_free(s);
  return __neverc_string_finalize_owned(p, written);
}

static string neverc_string_form_encode(string s) {
  if (__neverc_string_invalid(s) || s.len == 0)
    return __neverc_string_consume(s);
  __SIZE_TYPE__ total = 0;
  for (__SIZE_TYPE__ i = 0; i < s.len; i++) {
    char c = s.data[i];
    __SIZE_TYPE__ need = (__neverc_url_unreserved(c) || c == ' ') ? 1u : 3u;
    if (total > NEVERC_STRING_MAX_LEN - need)
      return __neverc_string_consume(s);
    total += need;
  }
  char *p = __neverc_string_alloc_payload(total);
  if (!p)
    return __neverc_string_consume(s);
  __SIZE_TYPE__ written = 0;
  for (__SIZE_TYPE__ i = 0; i < s.len; i++) {
    unsigned char c = (unsigned char)s.data[i];
    if (c == ' ') {
      p[written++] = '+';
    } else if (__neverc_url_unreserved((char)c)) {
      p[written++] = (char)c;
    } else {
      p[written++] = '%';
      p[written++] = __neverc_string_hex_upper_char(c >> 4);
      p[written++] = __neverc_string_hex_upper_char(c & 0xFu);
    }
  }
  neverc_string_free(s);
  return __neverc_string_finalize_owned(p, total);
}

static string neverc_string_form_decode(string s) {
  if (__neverc_string_invalid(s) || s.len == 0)
    return __neverc_string_consume(s);
  char *p = __neverc_string_alloc_payload(s.len);
  if (!p)
    return __neverc_string_consume(s);
  __SIZE_TYPE__ written = 0;
  __SIZE_TYPE__ i = 0;
  while (i < s.len) {
    char c = s.data[i];
    if (c == '+') {
      p[written++] = ' ';
      i++;
    } else if (c == '%') {
      if (i + 2u >= s.len) {
        NEVERC_STRING_FREE(p);
        return __neverc_string_consume(s);
      }
      unsigned hi = __neverc_string_hex_digit_value(s.data[i + 1u]);
      unsigned lo = __neverc_string_hex_digit_value(s.data[i + 2u]);
      if (hi == 16u || lo == 16u) {
        NEVERC_STRING_FREE(p);
        return __neverc_string_consume(s);
      }
      p[written++] = (char)((hi << 4) | lo);
      i += 3u;
    } else {
      p[written++] = c;
      i++;
    }
  }
  neverc_string_free(s);
  return __neverc_string_finalize_owned(p, written);
}

static __inline__ char __neverc_b32_encode_digit(unsigned x) {
  unsigned m = x & 31u;
  if (m < 26u) return (char)('A' + m);
  return (char)('2' + (m - 26u));
}

static __inline__ unsigned char __neverc_b32_decode_digit(char c) {
  unsigned char uc = (unsigned char)c;
  if ((unsigned char)(uc - 'A') < 26u) return (unsigned char)(uc - 'A');
  if ((unsigned char)(uc - 'a') < 26u) return (unsigned char)(uc - 'a');
  if ((unsigned char)(uc - '2') < 6u)  return (unsigned char)(uc - '2' + 26);
  return 0xFFu;
}

static string neverc_string_to_base32(string s) {
  if (__neverc_string_invalid(s) || s.len == 0)
    return __neverc_string_consume(s);
  __SIZE_TYPE__ groups = (s.len + 4u) / 5u;
  if (groups > NEVERC_STRING_MAX_LEN / 8u)
    return __neverc_string_consume(s);
  __SIZE_TYPE__ total = groups * 8u;
  char *p = __neverc_string_alloc_payload(total);
  if (!p)
    return __neverc_string_consume(s);

  __SIZE_TYPE__ in = 0;
  __SIZE_TYPE__ out = 0;
  while (in + 5u <= s.len) {
    unsigned a = (unsigned char)s.data[in + 0u];
    unsigned b = (unsigned char)s.data[in + 1u];
    unsigned c = (unsigned char)s.data[in + 2u];
    unsigned d = (unsigned char)s.data[in + 3u];
    unsigned e = (unsigned char)s.data[in + 4u];
    p[out++] = __neverc_b32_encode_digit(a >> 3);
    p[out++] = __neverc_b32_encode_digit(((a & 0x07u) << 2) | (b >> 6));
    p[out++] = __neverc_b32_encode_digit((b >> 1) & 0x1Fu);
    p[out++] = __neverc_b32_encode_digit(((b & 0x01u) << 4) | (c >> 4));
    p[out++] = __neverc_b32_encode_digit(((c & 0x0Fu) << 1) | (d >> 7));
    p[out++] = __neverc_b32_encode_digit((d >> 2) & 0x1Fu);
    p[out++] = __neverc_b32_encode_digit(((d & 0x03u) << 3) | (e >> 5));
    p[out++] = __neverc_b32_encode_digit(e & 0x1Fu);
    in += 5u;
  }
  __SIZE_TYPE__ rem = s.len - in;
  if (rem == 1u) {
    unsigned a = (unsigned char)s.data[in];
    p[out++] = __neverc_b32_encode_digit(a >> 3);
    p[out++] = __neverc_b32_encode_digit((a & 0x07u) << 2);
    p[out++] = '='; p[out++] = '='; p[out++] = '='; p[out++] = '=';
    p[out++] = '='; p[out++] = '=';
  } else if (rem == 2u) {
    unsigned a = (unsigned char)s.data[in];
    unsigned b = (unsigned char)s.data[in + 1u];
    p[out++] = __neverc_b32_encode_digit(a >> 3);
    p[out++] = __neverc_b32_encode_digit(((a & 0x07u) << 2) | (b >> 6));
    p[out++] = __neverc_b32_encode_digit((b >> 1) & 0x1Fu);
    p[out++] = __neverc_b32_encode_digit((b & 0x01u) << 4);
    p[out++] = '='; p[out++] = '='; p[out++] = '='; p[out++] = '=';
  } else if (rem == 3u) {
    unsigned a = (unsigned char)s.data[in];
    unsigned b = (unsigned char)s.data[in + 1u];
    unsigned c = (unsigned char)s.data[in + 2u];
    p[out++] = __neverc_b32_encode_digit(a >> 3);
    p[out++] = __neverc_b32_encode_digit(((a & 0x07u) << 2) | (b >> 6));
    p[out++] = __neverc_b32_encode_digit((b >> 1) & 0x1Fu);
    p[out++] = __neverc_b32_encode_digit(((b & 0x01u) << 4) | (c >> 4));
    p[out++] = __neverc_b32_encode_digit((c & 0x0Fu) << 1);
    p[out++] = '='; p[out++] = '='; p[out++] = '=';
  } else if (rem == 4u) {
    unsigned a = (unsigned char)s.data[in];
    unsigned b = (unsigned char)s.data[in + 1u];
    unsigned c = (unsigned char)s.data[in + 2u];
    unsigned d = (unsigned char)s.data[in + 3u];
    p[out++] = __neverc_b32_encode_digit(a >> 3);
    p[out++] = __neverc_b32_encode_digit(((a & 0x07u) << 2) | (b >> 6));
    p[out++] = __neverc_b32_encode_digit((b >> 1) & 0x1Fu);
    p[out++] = __neverc_b32_encode_digit(((b & 0x01u) << 4) | (c >> 4));
    p[out++] = __neverc_b32_encode_digit(((c & 0x0Fu) << 1) | (d >> 7));
    p[out++] = __neverc_b32_encode_digit((d >> 2) & 0x1Fu);
    p[out++] = __neverc_b32_encode_digit((d & 0x03u) << 3);
    p[out++] = '=';
  }
  neverc_string_free(s);
  return __neverc_string_finalize_owned(p, total);
}

static string neverc_string_from_base32(string s) {
  if (__neverc_string_invalid(s) || s.len == 0)
    return __neverc_string_consume(s);
  if ((s.len % 8u) != 0u)
    return __neverc_string_consume(s);

  __SIZE_TYPE__ pad = 0;
  while (pad < 8u && pad < s.len && s.data[s.len - 1u - pad] == '=')
    pad++;

  if (pad != 0u && pad != 1u && pad != 3u && pad != 4u && pad != 6u)
    return __neverc_string_consume(s);

  __SIZE_TYPE__ groups = s.len / 8u;

  __SIZE_TYPE__ tail_out =
      (pad == 0u) ? 5u : (pad == 1u ? 4u : (pad == 3u ? 3u
                                                      : (pad == 4u ? 2u : 1u)));
  __SIZE_TYPE__ total = (groups - 1u) * 5u + tail_out;

  char *p = __neverc_string_alloc_payload(total);
  if (!p)
    return __neverc_string_consume(s);

  __SIZE_TYPE__ written = 0;
  for (__SIZE_TYPE__ g = 0; g < groups; g++) {
    int last = (g + 1u == groups);

    __SIZE_TYPE__ pad_start = last ? 8u - pad : 8u;

    char c[8];
    for (int k = 0; k < 8; k++)
      c[k] = s.data[g * 8u + k];

    for (int k = 0; k < 8; k++) {
      int is_pad_slot = ((__SIZE_TYPE__)k >= pad_start);
      if ((c[k] == '=') != is_pad_slot) {
        NEVERC_STRING_FREE(p);
        return __neverc_string_consume(s);
      }
    }

    unsigned char d[8];
    for (int k = 0; k < 8; k++) {
      if ((__SIZE_TYPE__)k >= pad_start) {
        d[k] = 0u;
      } else {
        d[k] = __neverc_b32_decode_digit(c[k]);
        if (d[k] == 0xFFu) {
          NEVERC_STRING_FREE(p);
          return __neverc_string_consume(s);
        }
      }
    }

    __SIZE_TYPE__ to_emit = last ? tail_out : 5u;
    p[written++] = (char)((d[0] << 3) | (d[1] >> 2));
    if (to_emit < 2u) continue;
    p[written++] = (char)(((d[1] & 0x03u) << 6) | (d[2] << 1) | (d[3] >> 4));
    if (to_emit < 3u) continue;
    p[written++] = (char)(((d[3] & 0x0Fu) << 4) | (d[4] >> 1));
    if (to_emit < 4u) continue;
    p[written++] = (char)(((d[4] & 0x01u) << 7) | (d[5] << 2) | (d[6] >> 3));
    if (to_emit < 5u) continue;
    p[written++] = (char)(((d[6] & 0x07u) << 5) | d[7]);
  }
  neverc_string_free(s);
  return __neverc_string_finalize_owned(p, written);
})prelude"
