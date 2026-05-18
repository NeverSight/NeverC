/*===- TarWriter.c - tar archive creation (pure C) -------------*- C -*-===*/
#include "include/csupport/ltar_lwriter.h"
#include <string.h>
#include <stdio.h>

static void write_octal(char *dst, size_t dst_len, uint64_t val) {
  char buf[24];
  int pos = 0;
  if (val == 0) { buf[pos++] = '0'; }
  else {
    while (val > 0 && pos < 23) {
      buf[pos++] = '0' + (char)(val & 7);
      val >>= 3;
    }
  }
  size_t field_len = dst_len - 1;
  memset(dst, '0', field_len);
  dst[field_len] = '\0';
  for (int i = 0; i < pos && i < (int)field_len; i++)
    dst[field_len - 1 - i] = buf[i];
}

unsigned csupport_tar_checksum(const char header[512]) {
  unsigned sum = 0;
  for (int i = 0; i < 512; i++) {
    if (i >= 148 && i < 156)
      sum += ' ';
    else
      sum += (unsigned char)header[i];
  }
  return sum;
}

void csupport_tar_write_header(char header[512], const char *path,
                                size_t path_len, size_t file_size) {
  memset(header, 0, 512);

  size_t name_len = path_len < 100 ? path_len : 99;
  memcpy(header, path, name_len);

  write_octal(header + 100, 8, 0664);
  write_octal(header + 108, 8, 0);
  write_octal(header + 116, 8, 0);
  write_octal(header + 124, 12, (uint64_t)file_size);
  write_octal(header + 136, 12, 0);

  header[156] = '0';

  memcpy(header + 257, "ustar", 5);
  header[263] = '0';
  header[264] = '0';

  unsigned checksum = csupport_tar_checksum(header);
  snprintf(header + 148, 7, "%06o", checksum);
  header[154] = '\0';
  header[155] = ' ';
}

void csupport_tar_write_padding(char *buf, size_t file_size,
                                size_t *padding_size) {
  size_t remainder = file_size % 512;
  if (remainder == 0) {
    *padding_size = 0;
    return;
  }
  *padding_size = 512 - remainder;
  memset(buf, 0, *padding_size);
}

size_t csupport_tar_format_pax(char *buf, size_t buflen,
                               const char *key, size_t key_len,
                               const char *val, size_t val_len) {
  int content_len = (int)key_len + (int)val_len + 3;
  char lenbuf[16];
  int llen = snprintf(lenbuf, sizeof(lenbuf), "%d", content_len);
  int total = content_len + llen;
  int total2_len = snprintf(lenbuf, sizeof(lenbuf), "%d", total);
  total = content_len + total2_len;

  size_t written = (size_t)snprintf(buf, buflen, "%d %.*s=%.*s\n",
                                    total, (int)key_len, key,
                                    (int)val_len, val);
  return written < buflen ? written : 0;
}

int csupport_tar_split_path(const char *path, size_t path_len,
                            size_t *prefix_len, size_t *name_start) {
  if (path_len < 100) {
    *prefix_len = 0;
    *name_start = 0;
    return 1;
  }
  const int max_prefix = 137;
  size_t sep = (size_t)-1;
  size_t limit = (size_t)max_prefix + 1;
  if (limit > path_len) limit = path_len;
  for (size_t i = limit; i > 0; i--) {
    if (path[i - 1] == '/') { sep = i - 1; break; }
  }
  if (sep == (size_t)-1) return 0;
  if (path_len - sep - 1 >= 100) return 0;
  *prefix_len = sep;
  *name_start = sep + 1;
  return 1;
}

void csupport_tar_compute_checksum_buf(char *header, size_t hdr_len) {
  if (hdr_len < 512) return;
  memset(header + 148, ' ', 8);
  unsigned sum = 0;
  for (int i = 0; i < 512; i++)
    sum += (unsigned char)header[i];
  snprintf(header + 148, 7, "%06o", sum);
}
