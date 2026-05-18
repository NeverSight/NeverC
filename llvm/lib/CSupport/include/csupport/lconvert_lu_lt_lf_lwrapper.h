#ifndef CSUPPORT_LCONVERT_LU_LT_LF_LWRAPPER_H
#define CSUPPORT_LCONVERT_LU_LT_LF_LWRAPPER_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

int csupport_is_valid_utf8(const char *data, size_t len);
int csupport_utf8_char_length(uint8_t first_byte);

/* Decode one UTF-8 codepoint from *pos (up to end).
   Returns codepoint (0xFFFD on error), advances *pos. */
uint32_t csupport_decode_utf8(const unsigned char **pos,
                              const unsigned char *end);

/* Fix invalid UTF-8: decode leniently, re-encode strictly.
   Invalid bytes become U+FFFD. Returns output bytes needed.
   If output is NULL, just counts. */
size_t csupport_fix_utf8(const char *input, size_t input_len, char *output,
                         size_t output_cap);

/* Check if data is valid GBK encoding. Returns 1 if valid GBK. */
int csupport_has_gbk(const char *data, size_t len);

/* Check for UTF-16 byte order mark in first 2 bytes. */
int csupport_has_utf16_bom(const char *data, size_t len);

/* Check for UTF-8 byte order mark (EF BB BF). */
int csupport_has_utf8_bom(const char *data, size_t len);

/* Byteswap UTF-16 data in place (each 2-byte unit). */
void csupport_utf16_byteswap(uint16_t *data, size_t count);

/* Convert GBK-encoded data to UTF-8.
   Returns 1 on success, 0 on failure. Uses iconv on Unix, WinAPI on Windows.
   utf8_written receives the number of bytes written to utf8 buffer. */
int csupport_convert_gbk_to_utf8(const char *gbk, size_t gbk_len, char *utf8,
                                 size_t utf8_cap, size_t *utf8_written);

#ifdef __cplusplus
}
#endif
#endif
