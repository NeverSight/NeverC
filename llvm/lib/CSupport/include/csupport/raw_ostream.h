#ifndef CSUPPORT_RAW_OSTREAM_H
#define CSUPPORT_RAW_OSTREAM_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

int csupport_write_fd(int fd, const char *data, size_t len);
size_t csupport_format_u64_to_hex(char *buf, size_t buflen, uint64_t val,
                                  int lower);

size_t csupport_ostream_copy_to_buffer_small(const char *src, size_t len,
                                             char *dst);

size_t csupport_ostream_write_padding(char *buf, size_t buflen, char pad_char,
                                      unsigned count);

size_t csupport_format_u64_decimal(char *buf, size_t buflen, uint64_t val,
                                   unsigned min_digits);
size_t csupport_format_i64_decimal(char *buf, size_t buflen, int64_t val,
                                   unsigned min_digits);

size_t csupport_format_hex_dump_line(char *buf, size_t buflen,
                                     const uint8_t *data, size_t data_len,
                                     uint64_t offset, unsigned offset_width,
                                     unsigned num_per_line,
                                     unsigned byte_group_size, int upper_case,
                                     int show_ascii);

unsigned csupport_hex_dump_offset_width(uint64_t max_offset);

size_t csupport_format_justified(char *buf, size_t buflen, const char *str,
                                 size_t str_len, unsigned width, int justify);

size_t csupport_format_number_decimal(char *buf, size_t buflen, int64_t val,
                                      unsigned width);
size_t csupport_format_number_hex(char *buf, size_t buflen, uint64_t val,
                                  unsigned width, int upper, int prefix);

float csupport_bp_log_cost(unsigned x, unsigned y, const float *log2_cache,
                           unsigned cache_size);
float csupport_bp_move_gain(const unsigned *utility_nodes, unsigned num_un,
                            const float *cached_gain_lr,
                            const float *cached_gain_rl, unsigned *sig_stride,
                            int from_left_to_right);
int csupport_bp_move_node(unsigned *bucket, const unsigned *utility_nodes,
                          unsigned num_un, unsigned *left_counts,
                          unsigned *right_counts, int *cached_gain_valid,
                          unsigned sig_stride, unsigned left_bucket,
                          unsigned right_bucket, float skip_probability,
                          uint32_t rng_val);

size_t csupport_bp_leaf_count(unsigned left_idx, unsigned right_idx,
                              unsigned start_idx, unsigned end_idx);

#ifdef __cplusplus
}
#endif
#endif
