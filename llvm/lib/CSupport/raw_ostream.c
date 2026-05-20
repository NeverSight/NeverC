/*===- raw_ostream.c - Output stream (pure C) -------------------*- C -*-===*/
#include "include/csupport/raw_ostream.h"
#include "include/csupport/lprocess.h"
#include "include/csupport/ostream.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include <errno.h>

int csupport_write_fd(int fd, const char *data, size_t len) {
  while (len > 0) {
#ifdef _WIN32
    unsigned int chunk = len > 0x7FFFFFFFu ? 0x7FFFFFFFu : (unsigned int)len;
    int n = _write(fd, data, chunk);
#else
    ssize_t n = write(fd, data, len);
#endif
    if (n < 0) {
      if (errno == EINTR) continue;
      return -1;
    }
    data += n;
    len -= (size_t)n;
  }
  return 0;
}

size_t csupport_format_u64_to_hex(char *buf, size_t buflen, uint64_t val,
                                  int lower) {
  const char *digits = lower ? "0123456789abcdef" : "0123456789ABCDEF";
  char tmp[17];
  int pos = 0;
  if (val == 0) { tmp[pos++] = '0'; }
  else { while (val) { tmp[pos++] = digits[val & 0xf]; val >>= 4; } }
  if ((size_t)pos >= buflen) return 0;
  for (int i = 0; i < pos; i++) buf[i] = tmp[pos - 1 - i];
  buf[pos] = '\0';
  return (size_t)pos;
}

size_t csupport_ostream_copy_to_buffer_small(const char *src, size_t len,
                                              char *dst) {
  switch (len) {
  case 4: dst[3] = src[3]; __attribute__((fallthrough));
  case 3: dst[2] = src[2]; __attribute__((fallthrough));
  case 2: dst[1] = src[1]; __attribute__((fallthrough));
  case 1: dst[0] = src[0]; __attribute__((fallthrough));
  case 0: break;
  default: memcpy(dst, src, len); break;
  }
  return len;
}

size_t csupport_ostream_write_padding(char *buf, size_t buflen,
                                       char pad_char, unsigned count) {
  if (count == 0) return 0;
  unsigned to_write = count < (unsigned)buflen ? count : (unsigned)buflen;
  memset(buf, pad_char, to_write);
  return to_write;
}

size_t csupport_format_u64_decimal(char *buf, size_t buflen, uint64_t val,
                                    unsigned min_digits) {
  char tmp[21];
  int pos = 0;
  if (val == 0) { tmp[pos++] = '0'; }
  else { while (val) { tmp[pos++] = '0' + (char)(val % 10); val /= 10; } }
  while ((unsigned)pos < min_digits && pos < 20)
    tmp[pos++] = '0';
  if ((size_t)pos >= buflen) { if (buflen > 0) buf[0] = '\0'; return 0; }
  for (int i = 0; i < pos; i++) buf[i] = tmp[pos - 1 - i];
  buf[pos] = '\0';
  return (size_t)pos;
}

size_t csupport_format_i64_decimal(char *buf, size_t buflen, int64_t val,
                                    unsigned min_digits) {
  if (val < 0) {
    if (buflen < 2) { if (buflen > 0) buf[0] = '\0'; return 0; }
    buf[0] = '-';
    size_t n = csupport_format_u64_decimal(buf + 1, buflen - 1,
                                            -(uint64_t)val, min_digits);
    return n > 0 ? n + 1 : 0;
  }
  return csupport_format_u64_decimal(buf, buflen, (uint64_t)val, min_digits);
}

unsigned csupport_hex_dump_offset_width(uint64_t max_offset) {
  if (max_offset == 0) return 4;
  unsigned power = 0;
  uint64_t v = max_offset;
  while (v) { ++power; v >>= 1; }
  unsigned nibbles = (power + 3) / 4;
  return nibbles < 4 ? 4 : nibbles;
}

size_t csupport_format_hex_dump_line(char *buf, size_t buflen,
                                      const uint8_t *data, size_t data_len,
                                      uint64_t offset, unsigned offset_width,
                                      unsigned num_per_line,
                                      unsigned byte_group_size,
                                      int upper_case, int show_ascii) {
  const char *digits = upper_case ? "0123456789ABCDEF" : "0123456789abcdef";
  size_t pos = 0;
  size_t line_bytes = data_len < num_per_line ? data_len : num_per_line;

  if (offset_width > 0) {
    char hex[17];
    size_t hlen = csupport_format_u64_to_hex(hex, sizeof(hex), offset,
                                              !upper_case);
    unsigned pad = offset_width > (unsigned)hlen ? offset_width - (unsigned)hlen : 0;
    for (unsigned i = 0; i < pad && pos < buflen; i++)
      buf[pos++] = '0';
    for (size_t i = 0; i < hlen && pos < buflen; i++)
      buf[pos++] = hex[i];
    if (pos + 1 < buflen) { buf[pos++] = ':'; buf[pos++] = ' '; }
  }

  size_t hex_start = pos;
  for (size_t i = 0; i < line_bytes; i++) {
    if (i > 0 && byte_group_size > 0 && (i % byte_group_size) == 0 && pos < buflen)
      buf[pos++] = ' ';
    if (pos + 1 < buflen) {
      buf[pos++] = digits[(data[i] >> 4) & 0xF];
      buf[pos++] = digits[data[i] & 0xF];
    }
  }

  if (show_ascii) {
    unsigned num_groups = byte_group_size > 0
        ? (num_per_line + byte_group_size - 1) / byte_group_size
        : 0;
    unsigned block_width = num_per_line * 2 + (num_groups > 0 ? num_groups - 1 : 0);
    size_t hex_chars = pos - hex_start;
    unsigned padding = block_width > (unsigned)hex_chars
        ? block_width - (unsigned)hex_chars + 2 : 2;
    for (unsigned i = 0; i < padding && pos < buflen; i++)
      buf[pos++] = ' ';
    if (pos < buflen) buf[pos++] = '|';
    for (size_t i = 0; i < line_bytes && pos < buflen; i++) {
      buf[pos++] = (data[i] >= 32 && data[i] < 127) ? (char)data[i] : '.';
    }
    if (pos < buflen) buf[pos++] = '|';
  }

  if (pos < buflen) buf[pos] = '\0';
  return pos;
}

size_t csupport_format_justified(char *buf, size_t buflen,
                                   const char *str, size_t str_len,
                                   unsigned width, int justify) {
  size_t pos = 0;
  unsigned left_pad = 0, right_pad = 0;
  if (width > str_len) {
    unsigned diff = width - (unsigned)str_len;
    switch (justify) {
    case 0: break;
    case 1: right_pad = diff; break;
    case 2: left_pad = diff; break;
    case 3: left_pad = diff / 2; right_pad = diff - left_pad; break;
    }
  }
  for (unsigned i = 0; i < left_pad && pos < buflen; i++)
    buf[pos++] = ' ';
  for (size_t i = 0; i < str_len && pos < buflen; i++)
    buf[pos++] = str[i];
  for (unsigned i = 0; i < right_pad && pos < buflen; i++)
    buf[pos++] = ' ';
  if (pos < buflen) buf[pos] = '\0';
  return pos;
}

size_t csupport_format_number_decimal(char *buf, size_t buflen,
                                       int64_t val, unsigned width) {
  char tmp[32];
  size_t n = csupport_format_i64_decimal(tmp, sizeof(tmp), val, 0);
  size_t pos = 0;
  if (width > n) {
    unsigned pad = width - (unsigned)n;
    for (unsigned i = 0; i < pad && pos < buflen; i++)
      buf[pos++] = ' ';
  }
  for (size_t i = 0; i < n && pos < buflen; i++)
    buf[pos++] = tmp[i];
  if (pos < buflen) buf[pos] = '\0';
  return pos;
}

size_t csupport_format_number_hex(char *buf, size_t buflen,
                                    uint64_t val, unsigned width,
                                    int upper, int prefix) {
  char tmp[32];
  size_t pos = 0;
  if (prefix && pos + 1 < buflen) {
    buf[pos++] = '0';
    buf[pos++] = upper ? 'X' : 'x';
  }
  size_t hlen = csupport_format_u64_to_hex(tmp, sizeof(tmp), val, !upper);
  unsigned total = (unsigned)hlen + (prefix ? 2 : 0);
  if (width > total) {
    unsigned pad = width - total;
    for (unsigned i = 0; i < pad && pos < buflen; i++)
      buf[pos++] = '0';
  }
  for (size_t i = 0; i < hlen && pos < buflen; i++)
    buf[pos++] = tmp[i];
  if (pos < buflen) buf[pos] = '\0';
  return pos;
}

float csupport_bp_log_cost(unsigned x, unsigned y,
                             const float *log2_cache, unsigned cache_size) {
  float lx = (x + 1 < cache_size) ? log2_cache[x + 1] : log2f((float)(x + 1));
  float ly = (y + 1 < cache_size) ? log2_cache[y + 1] : log2f((float)(y + 1));
  return -((float)x * lx + (float)y * ly);
}

float csupport_bp_move_gain(const unsigned *utility_nodes,
                              unsigned num_un,
                              const float *cached_gain_lr,
                              const float *cached_gain_rl,
                              unsigned *sig_stride,
                              int from_left_to_right) {
  float gain = 0.0f;
  (void)sig_stride;
  for (unsigned i = 0; i < num_un; i++) {
    unsigned idx = utility_nodes[i];
    gain += from_left_to_right ? cached_gain_lr[idx] : cached_gain_rl[idx];
  }
  return gain;
}

int csupport_bp_move_node(unsigned *bucket,
                            const unsigned *utility_nodes, unsigned num_un,
                            unsigned *left_counts, unsigned *right_counts,
                            int *cached_gain_valid, unsigned sig_stride,
                            unsigned left_bucket, unsigned right_bucket,
                            float skip_probability, uint32_t rng_val) {
  float rand_f = (float)(rng_val >> 8) * (1.0f / 16777216.0f);
  if (rand_f <= skip_probability)
    return 0;
  int from_left = (*bucket == left_bucket);
  *bucket = from_left ? right_bucket : left_bucket;
  (void)sig_stride;
  for (unsigned i = 0; i < num_un; i++) {
    unsigned idx = utility_nodes[i];
    if (from_left) {
      left_counts[idx]--;
      right_counts[idx]++;
    } else {
      left_counts[idx]++;
      right_counts[idx]--;
    }
    cached_gain_valid[idx] = 0;
  }
  return 1;
}

size_t csupport_bp_leaf_count(unsigned left_idx,
                              unsigned right_idx,
                              unsigned start_idx,
                              unsigned end_idx) {
  unsigned leaf_count = 0;
  if (left_idx == 0 && right_idx == 0)
    return (end_idx > start_idx) ? end_idx - start_idx : 1;
  return leaf_count;
}
