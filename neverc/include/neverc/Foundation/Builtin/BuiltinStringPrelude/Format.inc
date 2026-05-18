R"prelude(

#ifndef NEVERC_STRING_FORMAT_INT_BUF
#define NEVERC_STRING_FORMAT_INT_BUF 24
#endif

#ifndef NEVERC_STRING_FORMAT_MAX_FIELD
#define NEVERC_STRING_FORMAT_MAX_FIELD ((__SIZE_TYPE__)65536)
#endif

static __inline__ __SIZE_TYPE__
__neverc_string_format_u64(char *out, unsigned long long value) {
  if (value == 0) {
    out[0] = '0';
    return 1;
  }
  char tmp[20];
  __SIZE_TYPE__ n = 0;
  while (value) {
    tmp[n++] = (char)('0' + (value % 10ULL));
    value /= 10ULL;
  }
  for (__SIZE_TYPE__ i = 0; i < n; i++)
    out[i] = tmp[n - 1 - i];
  return n;
}

static __inline__ __SIZE_TYPE__
__neverc_string_format_i64(char *out, long long value) {
  unsigned long long u;
  __SIZE_TYPE__ pos = 0;
  if (value < 0) {
    out[pos++] = '-';
    u = (unsigned long long)0 - (unsigned long long)value;
  } else {
    u = (unsigned long long)value;
  }
  pos += __neverc_string_format_u64(out + pos, u);
  return pos;
}

static __inline__ __SIZE_TYPE__
__neverc_string_format_x64(char *out, unsigned long long value) {
  if (value == 0) {
    out[0] = '0';
    return 1;
  }
  char tmp[16];
  __SIZE_TYPE__ n = 0;
  while (value) {
    unsigned d = (unsigned)(value & 0xFu);
    tmp[n++] = d < 10 ? (char)('0' + d) : (char)('a' + (d - 10));
    value >>= 4;
  }
  for (__SIZE_TYPE__ i = 0; i < n; i++)
    out[i] = tmp[n - 1 - i];
  return n;
}

static __inline__ __SIZE_TYPE__
__neverc_string_format_cstr_len(const char *s) {
  return s ? (__SIZE_TYPE__)__builtin_strlen(s) : 0;
}

struct __neverc_format_spec {

  int left_align;

  int zero_pad;

  __SIZE_TYPE__ width;

  int has_precision;
  __SIZE_TYPE__ precision;

  int len_mod;

  char spec;

  __SIZE_TYPE__ end_pos;
};

static __inline__ __SIZE_TYPE__
__neverc_string_format_max(__SIZE_TYPE__ a, __SIZE_TYPE__ b) {
  return a > b ? a : b;
}

static __inline__ __SIZE_TYPE__
__neverc_string_format_pad_count(__SIZE_TYPE__ width,
                                 __SIZE_TYPE__ rendered_len) {
  return width > rendered_len ? width - rendered_len : 0;
}

static __inline__ void
__neverc_string_format_parse_spec(const char *data, __SIZE_TYPE__ start,
                                  __SIZE_TYPE__ avail,
                                  struct __neverc_format_spec *out) {
  out->left_align = 0;
  out->zero_pad = 0;
  out->width = 0;
  out->has_precision = 0;
  out->precision = 0;
  out->len_mod = 0;
  out->spec = 0;
  out->end_pos = start;

  __SIZE_TYPE__ pos = start;
  if (pos >= avail)
    return;

  while (pos < avail && (data[pos] == '-' || data[pos] == '0')) {
    if (data[pos] == '-')
      out->left_align = 1;
    else
      out->zero_pad = 1;
    pos++;
  }
  if (pos >= avail)
    return;

  while (pos < avail && (unsigned char)((unsigned char)data[pos] - '0') < 10u) {
    __SIZE_TYPE__ d = (__SIZE_TYPE__)(data[pos] - '0');
    if (out->width > (NEVERC_STRING_FORMAT_MAX_FIELD - d) / 10u)
      return;
    out->width = out->width * 10u + d;
    pos++;
  }
  if (pos >= avail)
    return;

  if (data[pos] == '.') {
    pos++;
    out->has_precision = 1;
    while (pos < avail && (unsigned char)((unsigned char)data[pos] - '0') < 10u) {
      __SIZE_TYPE__ d = (__SIZE_TYPE__)(data[pos] - '0');
      if (out->precision > (NEVERC_STRING_FORMAT_MAX_FIELD - d) / 10u)
        return;
      out->precision = out->precision * 10u + d;
      pos++;
    }
    if (pos >= avail)
      return;
  }

  if (data[pos] == 'l') {
    out->len_mod = 1;
    pos++;
    if (pos < avail && data[pos] == 'l') {
      out->len_mod = 2;
      pos++;
    }
  }
  if (pos >= avail)
    return;

  char c = data[pos];
  switch (c) {
  case '%':
  case 'd':
  case 'i':
  case 'u':
  case 'x':
  case 's':
  case 'S':
  case 'c':
  case 'p':
    out->spec = c;
    out->end_pos = pos + 1;
    break;
  default:

    break;
  }
}

static __inline__ void
__neverc_string_format_release_args(string fmt, __builtin_va_list ap) {
  for (__SIZE_TYPE__ i = 0; i < fmt.len; ) {
    if (fmt.data[i] != '%' || i + 1 >= fmt.len) {
      i++;
      continue;
    }
    struct __neverc_format_spec sp;
    __neverc_string_format_parse_spec(fmt.data, i + 1, fmt.len, &sp);
    if (sp.spec == 0) {

      i++;
      continue;
    }
    switch (sp.spec) {
    case '%':
      break;
    case 'c':
      (void)__builtin_va_arg(ap, int);
      break;
    case 'd':
    case 'i':
      if (sp.len_mod == 0)      (void)__builtin_va_arg(ap, int);
      else if (sp.len_mod == 1) (void)__builtin_va_arg(ap, long);
      else                      (void)__builtin_va_arg(ap, long long);
      break;
    case 'u':
    case 'x':
      if (sp.len_mod == 0)      (void)__builtin_va_arg(ap, unsigned int);
      else if (sp.len_mod == 1) (void)__builtin_va_arg(ap, unsigned long);
      else                      (void)__builtin_va_arg(ap, unsigned long long);
      break;
    case 's':
      (void)__builtin_va_arg(ap, const char *);
      break;
    case 'S': {
      string s = __builtin_va_arg(ap, string);
      neverc_string_free(s);
      break;
    }
    case 'p':
      (void)__builtin_va_arg(ap, void *);
      break;
    default:
      break;
    }
    i = sp.end_pos;
  }
}

static string neverc_string_format(string fmt, ...) {
  if (__neverc_string_invalid(fmt) || fmt.len == 0)
    return __neverc_string_consume(fmt);

  __SIZE_TYPE__ total = 0;
  __builtin_va_list ap;
  __builtin_va_start(ap, fmt);
  for (__SIZE_TYPE__ i = 0; i < fmt.len; ) {
    if (fmt.data[i] != '%' || i + 1 >= fmt.len) {
      total++;
      i++;
      if (total > NEVERC_STRING_MAX_LEN)
        goto budget_overflow;
      continue;
    }
    struct __neverc_format_spec sp;
    __neverc_string_format_parse_spec(fmt.data, i + 1, fmt.len, &sp);
    if (sp.spec == 0) {

      total++;
      i++;
      if (total > NEVERC_STRING_MAX_LEN)
        goto budget_overflow;
      continue;
    }
    char buf[NEVERC_STRING_FORMAT_INT_BUF];
    __SIZE_TYPE__ rendered = 0;
    switch (sp.spec) {
    case '%':
      rendered = 1;
      break;
    case 'c':
      (void)__builtin_va_arg(ap, int);
      rendered = 1;
      break;
    case 'd':
    case 'i': {
      long long v;
      if (sp.len_mod == 0)      v = __builtin_va_arg(ap, int);
      else if (sp.len_mod == 1) v = __builtin_va_arg(ap, long);
      else                      v = __builtin_va_arg(ap, long long);
      rendered = __neverc_string_format_i64(buf, v);
      break;
    }
    case 'u': {
      unsigned long long u;
      if (sp.len_mod == 0)      u = __builtin_va_arg(ap, unsigned int);
      else if (sp.len_mod == 1) u = __builtin_va_arg(ap, unsigned long);
      else                      u = __builtin_va_arg(ap, unsigned long long);
      rendered = __neverc_string_format_u64(buf, u);
      break;
    }
    case 'x': {
      unsigned long long u;
      if (sp.len_mod == 0)      u = __builtin_va_arg(ap, unsigned int);
      else if (sp.len_mod == 1) u = __builtin_va_arg(ap, unsigned long);
      else                      u = __builtin_va_arg(ap, unsigned long long);
      rendered = __neverc_string_format_x64(buf, u);
      break;
    }
    case 's': {
      const char *str = __builtin_va_arg(ap, const char *);
      rendered = str ? __neverc_string_format_cstr_len(str) : 6u;
      if (sp.has_precision && sp.precision < rendered)
        rendered = sp.precision;
      break;
    }
    case 'S': {

      string s = __builtin_va_arg(ap, string);
      rendered = __neverc_string_invalid(s) ? 0u : s.len;
      if (sp.has_precision && sp.precision < rendered)
        rendered = sp.precision;
      break;
    }
    case 'p': {
      void *ptr = __builtin_va_arg(ap, void *);
      unsigned long long u = (unsigned long long)(__SIZE_TYPE__)ptr;
      __SIZE_TYPE__ hex_n = __neverc_string_format_x64(buf, u);
      rendered = 2u + hex_n;
      break;
    }
    default:

      rendered = 0;
      break;
    }
    __SIZE_TYPE__ field_len = __neverc_string_format_max(sp.width, rendered);
    if (total > NEVERC_STRING_MAX_LEN - field_len)
      goto budget_overflow;
    total += field_len;
    i = sp.end_pos;
  }
  __builtin_va_end(ap);

  if (0) {
  budget_overflow:

    __builtin_va_end(ap);
    __builtin_va_list ap_rel;
    __builtin_va_start(ap_rel, fmt);
    __neverc_string_format_release_args(fmt, ap_rel);
    __builtin_va_end(ap_rel);
    return __neverc_string_consume(fmt);
  }

  if (total == 0)
    return __neverc_string_consume(fmt);

  char *p = __neverc_string_alloc_payload(total);
  if (!p) {

    __builtin_va_list ap_rel;
    __builtin_va_start(ap_rel, fmt);
    __neverc_string_format_release_args(fmt, ap_rel);
    __builtin_va_end(ap_rel);
    return __neverc_string_consume(fmt);
  }

  __SIZE_TYPE__ written = 0;
  __builtin_va_list ap2;
  __builtin_va_start(ap2, fmt);
  for (__SIZE_TYPE__ i = 0; i < fmt.len; ) {
    if (fmt.data[i] != '%' || i + 1 >= fmt.len) {
      if (written < total) p[written++] = fmt.data[i];
      i++;
      continue;
    }
    struct __neverc_format_spec sp;
    __neverc_string_format_parse_spec(fmt.data, i + 1, fmt.len, &sp);
    if (sp.spec == 0) {

      if (written < total) p[written++] = '%';
      i++;
      continue;
    }
    char buf[NEVERC_STRING_FORMAT_INT_BUF];
    switch (sp.spec) {
    case '%': {
      __SIZE_TYPE__ pad = __neverc_string_format_pad_count(sp.width, 1u);
      if (!sp.left_align && pad) {
        __builtin_memset(p + written, ' ', pad); written += pad;
      }
      p[written++] = '%';
      if (sp.left_align && pad) {
        __builtin_memset(p + written, ' ', pad); written += pad;
      }
      break;
    }
    case 'c': {
      int c = __builtin_va_arg(ap2, int);
      __SIZE_TYPE__ pad = __neverc_string_format_pad_count(sp.width, 1u);
      if (!sp.left_align && pad) {
        __builtin_memset(p + written, ' ', pad); written += pad;
      }
      p[written++] = (char)c;
      if (sp.left_align && pad) {
        __builtin_memset(p + written, ' ', pad); written += pad;
      }
      break;
    }
    case 'd':
    case 'i': {
      long long v;
      if (sp.len_mod == 0)      v = __builtin_va_arg(ap2, int);
      else if (sp.len_mod == 1) v = __builtin_va_arg(ap2, long);
      else                      v = __builtin_va_arg(ap2, long long);
      __SIZE_TYPE__ n = __neverc_string_format_i64(buf, v);
      __SIZE_TYPE__ pad = __neverc_string_format_pad_count(sp.width, n);
      int has_sign = (n > 0 && buf[0] == '-');
      if (sp.left_align) {
        __builtin_memcpy(p + written, buf, n); written += n;
        if (pad) { __builtin_memset(p + written, ' ', pad); written += pad; }
      } else if (sp.zero_pad) {
        if (has_sign) p[written++] = '-';
        if (pad) { __builtin_memset(p + written, '0', pad); written += pad; }
        __SIZE_TYPE__ off = has_sign ? 1u : 0u;
        __builtin_memcpy(p + written, buf + off, n - off); written += n - off;
      } else {
        if (pad) { __builtin_memset(p + written, ' ', pad); written += pad; }
        __builtin_memcpy(p + written, buf, n); written += n;
      }
      break;
    }
    case 'u':
    case 'x': {
      unsigned long long u;
      if (sp.len_mod == 0)      u = __builtin_va_arg(ap2, unsigned int);
      else if (sp.len_mod == 1) u = __builtin_va_arg(ap2, unsigned long);
      else                      u = __builtin_va_arg(ap2, unsigned long long);
      __SIZE_TYPE__ n = (sp.spec == 'x') ? __neverc_string_format_x64(buf, u)
                                         : __neverc_string_format_u64(buf, u);
      __SIZE_TYPE__ pad = __neverc_string_format_pad_count(sp.width, n);
      char fill = (!sp.left_align && sp.zero_pad) ? '0' : ' ';
      if (!sp.left_align && pad) {
        __builtin_memset(p + written, fill, pad); written += pad;
      }
      __builtin_memcpy(p + written, buf, n); written += n;
      if (sp.left_align && pad) {
        __builtin_memset(p + written, ' ', pad); written += pad;
      }
      break;
    }
    case 's': {
      const char *str = __builtin_va_arg(ap2, const char *);
      const char nullstr[6] = {'(', 'n', 'u', 'l', 'l', ')'};
      const char *src;
      __SIZE_TYPE__ src_len;
      if (!str) {
        src = nullstr;
        src_len = 6u;
      } else {
        src = str;
        src_len = __neverc_string_format_cstr_len(str);
      }
      if (sp.has_precision && sp.precision < src_len)
        src_len = sp.precision;
      __SIZE_TYPE__ pad =
          __neverc_string_format_pad_count(sp.width, src_len);
      if (!sp.left_align && pad) {
        __builtin_memset(p + written, ' ', pad); written += pad;
      }
      if (src_len) {
        __builtin_memcpy(p + written, src, src_len); written += src_len;
      }
      if (sp.left_align && pad) {
        __builtin_memset(p + written, ' ', pad); written += pad;
      }
      break;
    }
    case 'S': {
      string s = __builtin_va_arg(ap2, string);
      __SIZE_TYPE__ src_len = __neverc_string_invalid(s) ? 0u : s.len;
      if (sp.has_precision && sp.precision < src_len)
        src_len = sp.precision;
      __SIZE_TYPE__ pad =
          __neverc_string_format_pad_count(sp.width, src_len);
      if (!sp.left_align && pad) {
        __builtin_memset(p + written, ' ', pad); written += pad;
      }
      if (s.data && src_len) {
        __builtin_memcpy(p + written, s.data, src_len); written += src_len;
      }
      if (sp.left_align && pad) {
        __builtin_memset(p + written, ' ', pad); written += pad;
      }
      neverc_string_free(s);
      break;
    }
    case 'p': {
      void *ptr = __builtin_va_arg(ap2, void *);
      unsigned long long u = (unsigned long long)(__SIZE_TYPE__)ptr;
      __SIZE_TYPE__ hex_n = __neverc_string_format_x64(buf, u);
      __SIZE_TYPE__ rendered = 2u + hex_n;
      __SIZE_TYPE__ pad =
          __neverc_string_format_pad_count(sp.width, rendered);
      if (sp.left_align) {
        p[written++] = '0'; p[written++] = 'x';
        __builtin_memcpy(p + written, buf, hex_n); written += hex_n;
        if (pad) { __builtin_memset(p + written, ' ', pad); written += pad; }
      } else if (sp.zero_pad) {
        p[written++] = '0'; p[written++] = 'x';
        if (pad) { __builtin_memset(p + written, '0', pad); written += pad; }
        __builtin_memcpy(p + written, buf, hex_n); written += hex_n;
      } else {
        if (pad) { __builtin_memset(p + written, ' ', pad); written += pad; }
        p[written++] = '0'; p[written++] = 'x';
        __builtin_memcpy(p + written, buf, hex_n); written += hex_n;
      }
      break;
    }
    default:
      break;
    }
    i = sp.end_pos;
  }
  __builtin_va_end(ap2);

  string result = __neverc_string_finalize_owned(p, written);
  neverc_string_free(fmt);
  return result;
})prelude"
