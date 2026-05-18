#ifndef CSUPPORT_LTAR_LWRITER_H
#define CSUPPORT_LTAR_LWRITER_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

void csupport_tar_write_header(char header[512], const char *path,
                               size_t path_len, size_t file_size);

unsigned csupport_tar_checksum(const char header[512]);

void csupport_tar_write_padding(char *buf, size_t file_size,
                                size_t *padding_size);

/* Format a PAX extended attribute "LEN key=val\n". Returns bytes written. */
size_t csupport_tar_format_pax(char *buf, size_t buflen, const char *key,
                               size_t key_len, const char *val, size_t val_len);

/* Split path for ustar header. Returns 1 if path fits, 0 if PAX needed. */
int csupport_tar_split_path(const char *path, size_t path_len,
                            size_t *prefix_len, size_t *name_start);

/* Compute and write checksum into a 512-byte tar header in-place. */
void csupport_tar_compute_checksum_buf(char *header, size_t hdr_len);

#ifdef __cplusplus
}
#endif
#endif
