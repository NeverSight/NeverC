/*===- ConvertUTFWrapper.c - UTF conversion helpers (pure C) ----*- C -*-===*/
#include "include/csupport/lconvert_lu_lt_lf_lwrapper.h"
#include <string.h>
#include <stdint.h>

#if !defined(_WIN32) && !defined(__APPLE__)
#include <iconv.h>
#endif

int csupport_is_valid_utf8(const char *data, size_t len) {
  const uint8_t *p = (const uint8_t *)data;
  const uint8_t *end = p + len;
  while (p < end) {
    uint8_t c = *p;
    int extra;
    uint32_t cp, min_cp;
    if (c < 0x80) { p++; continue; }
    else if ((c & 0xE0) == 0xC0) { extra = 1; min_cp = 0x80; cp = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { extra = 2; min_cp = 0x800; cp = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { extra = 3; min_cp = 0x10000; cp = c & 0x07; }
    else return 0;
    p++;
    for (int i = 0; i < extra; i++) {
      if (p >= end || (*p & 0xC0) != 0x80) return 0;
      cp = (cp << 6) | (*p & 0x3F);
      p++;
    }
    if (cp < min_cp || cp > 0x10FFFF) return 0;
    if (cp >= 0xD800 && cp <= 0xDFFF) return 0;
  }
  return 1;
}

int csupport_utf8_char_length(uint8_t first_byte) {
  if (first_byte < 0x80) return 1;
  if ((first_byte & 0xE0) == 0xC0) return 2;
  if ((first_byte & 0xF0) == 0xE0) return 3;
  if ((first_byte & 0xF8) == 0xF0) return 4;
  return 0;
}

uint32_t csupport_decode_utf8(const unsigned char **pos,
                              const unsigned char *end) {
  unsigned char c = **pos;
  (*pos)++;
  if (c < 0x80) return c;
  int extra;
  uint32_t cp, min_cp;
  if ((c & 0xE0) == 0xC0) { extra = 1; min_cp = 0x80; cp = c & 0x1F; }
  else if ((c & 0xF0) == 0xE0) { extra = 2; min_cp = 0x800; cp = c & 0x0F; }
  else if ((c & 0xF8) == 0xF0) { extra = 3; min_cp = 0x10000; cp = c & 0x07; }
  else return 0xFFFD;
  for (int i = 0; i < extra; i++) {
    if (*pos >= end || ((**pos) & 0xC0) != 0x80) return 0xFFFD;
    cp = (cp << 6) | ((**pos) & 0x3F);
    (*pos)++;
  }
  if (cp < min_cp || cp > 0x10FFFF) return 0xFFFD;
  if (cp >= 0xD800 && cp <= 0xDFFF) return 0xFFFD;
  return cp;
}

static int encode_utf8(uint32_t cp, char *buf) {
  if (cp < 0x80) {
    buf[0] = (char)(cp & 0x7F);
    return 1;
  } else if (cp < 0x800) {
    buf[0] = (char)(0xC0 | ((cp >> 6) & 0x1F));
    buf[1] = (char)(0x80 | (cp & 0x3F));
    return 2;
  } else if (cp < 0x10000) {
    buf[0] = (char)(0xE0 | ((cp >> 12) & 0x0F));
    buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    buf[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
  } else if (cp <= 0x10FFFF) {
    buf[0] = (char)(0xF0 | ((cp >> 18) & 0x07));
    buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    buf[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
  }
  return 0;
}

size_t csupport_fix_utf8(const char *input, size_t input_len,
                         char *output, size_t output_cap) {
  const unsigned char *p = (const unsigned char *)input;
  const unsigned char *end = p + input_len;
  size_t out = 0;
  while (p < end) {
    uint32_t cp = csupport_decode_utf8(&p, end);
    char buf[4];
    int n = encode_utf8(cp, buf);
    if (n > 0) {
      if (output && out + (size_t)n <= output_cap)
        memcpy(output + out, buf, (size_t)n);
      out += (size_t)n;
    }
  }
  return out;
}

int csupport_has_gbk(const char *data, size_t len) {
  const unsigned char *p = (const unsigned char *)data;
  size_t i = 0;
  while (i < len) {
    if (p[i] <= 0x7f) {
      i++;
    } else {
      if (i == len - 1) return 0;
      if (p[i] >= 0x81 && p[i] <= 0xfe &&
          p[i + 1] >= 0x40 && p[i + 1] <= 0xfe && p[i + 1] != 0xf7) {
        i += 2;
      } else {
        return 0;
      }
    }
  }
  return 1;
}

int csupport_has_utf16_bom(const char *data, size_t len) {
  if (len < 2) return 0;
  unsigned char a = (unsigned char)data[0];
  unsigned char b = (unsigned char)data[1];
  return (a == 0xff && b == 0xfe) || (a == 0xfe && b == 0xff);
}

int csupport_has_utf8_bom(const char *data, size_t len) {
  if (len < 3) return 0;
  return (unsigned char)data[0] == 0xef &&
         (unsigned char)data[1] == 0xbb &&
         (unsigned char)data[2] == 0xbf;
}

void csupport_utf16_byteswap(uint16_t *data, size_t count) {
  for (size_t i = 0; i < count; i++) {
    data[i] = (uint16_t)((data[i] >> 8) | (data[i] << 8));
  }
}

int csupport_convert_gbk_to_utf8(const char *gbk, size_t gbk_len,
                                  char *utf8, size_t utf8_cap,
                                  size_t *utf8_written) {
#if defined(__APPLE__)
  size_t n = gbk_len < utf8_cap ? gbk_len : utf8_cap;
  if (utf8 && n > 0) memcpy(utf8, gbk, n);
  if (utf8_written) *utf8_written = gbk_len;
  return 1;
#elif defined(_WIN32)
  (void)gbk; (void)gbk_len; (void)utf8; (void)utf8_cap; (void)utf8_written;
  return 0;
#else
  iconv_t cd = iconv_open("UTF-8", "GBK");
  if (cd == (iconv_t)-1) {
    cd = iconv_open("UTF-8", "GB18030");
    if (cd == (iconv_t)-1) return 0;
  }
  char *in_ptr = (char *)gbk;
  size_t in_left = gbk_len;
  char *out_ptr = utf8;
  size_t out_left = utf8_cap;
  size_t ret = iconv(cd, &in_ptr, &in_left, &out_ptr, &out_left);
  iconv_close(cd);
  if (ret == (size_t)-1 || in_left != 0) return 0;
  if (utf8_written) *utf8_written = utf8_cap - out_left;
  return 1;
#endif
}
