#ifndef NEVERC_UNICODE_H
#define NEVERC_UNICODE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_UNICODE_MAX_RUNE      0x10FFFF
#define NEVERC_UNICODE_REPLACEMENT   0xFFFD
#define NEVERC_UNICODE_MAX_ASCII     0x7F
#define NEVERC_UNICODE_MAX_LATIN1    0xFF

int neverc_unicode_is_upper(uint32_t r);
int neverc_unicode_is_lower(uint32_t r);
int neverc_unicode_is_letter(uint32_t r);
int neverc_unicode_is_digit(uint32_t r);
int neverc_unicode_is_space(uint32_t r);
int neverc_unicode_is_punct(uint32_t r);
int neverc_unicode_is_control(uint32_t r);
int neverc_unicode_is_graphic(uint32_t r);
int neverc_unicode_is_print(uint32_t r);

uint32_t neverc_unicode_to_upper(uint32_t r);
uint32_t neverc_unicode_to_lower(uint32_t r);

#ifdef __cplusplus
}
#endif

#endif /* NEVERC_UNICODE_H */
