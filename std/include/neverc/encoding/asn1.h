#ifndef NEVERC_ENCODING_ASN1_H
#define NEVERC_ENCODING_ASN1_H

/*
 * NeverC encoding/asn1 — ASN.1 DER encoding/decoding (mirrors Go encoding/asn1).
 *
 * Supports basic types: INTEGER, BOOLEAN, OCTET STRING, BIT STRING,
 * NULL, OID, SEQUENCE, SET, UTF8String, PrintableString, IA5String.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ASN.1 tag classes */
#define NEVERC_ASN1_UNIVERSAL   0x00
#define NEVERC_ASN1_APPLICATION 0x40
#define NEVERC_ASN1_CONTEXT     0x80
#define NEVERC_ASN1_PRIVATE     0xC0

/* Universal tags */
#define NEVERC_ASN1_BOOLEAN         1
#define NEVERC_ASN1_INTEGER         2
#define NEVERC_ASN1_BIT_STRING      3
#define NEVERC_ASN1_OCTET_STRING    4
#define NEVERC_ASN1_NULL            5
#define NEVERC_ASN1_OID             6
#define NEVERC_ASN1_UTF8_STRING     12
#define NEVERC_ASN1_SEQUENCE        16
#define NEVERC_ASN1_SET             17
#define NEVERC_ASN1_PRINTABLE_STR   19
#define NEVERC_ASN1_IA5_STRING      22

#define NEVERC_ASN1_CONSTRUCTED     0x20

typedef struct {
    int       tag_class;
    int       tag_number;
    int       constructed;
    const uint8_t *value;
    size_t    value_len;
    const uint8_t *full;
    size_t    full_len;
} neverc_asn1_element_t;

/* Decode one TLV element from DER */
int neverc_asn1_decode_element(const uint8_t *data, size_t len,
                               neverc_asn1_element_t *elem);

/* Decode integer (returns bytes needed; sets val for small integers) */
int neverc_asn1_decode_int64(const neverc_asn1_element_t *elem, int64_t *val);
int neverc_asn1_decode_bool(const neverc_asn1_element_t *elem, int *val);

/* Decode OID to dot-notation string (malloc'd) */
char *neverc_asn1_decode_oid(const neverc_asn1_element_t *elem);

/* Encode helpers (write DER into buffer, return bytes written) */
int neverc_asn1_encode_int64(uint8_t *buf, size_t cap, int64_t val);
int neverc_asn1_encode_bool(uint8_t *buf, size_t cap, int val);
int neverc_asn1_encode_octet_string(uint8_t *buf, size_t cap,
                                    const uint8_t *data, size_t len);
int neverc_asn1_encode_null(uint8_t *buf, size_t cap);
int neverc_asn1_encode_length(uint8_t *buf, size_t cap, size_t length);
int neverc_asn1_encode_tag(uint8_t *buf, size_t cap, int tag_class,
                           int constructed, int tag_number);

#ifdef __cplusplus
}
#endif

#endif /* NEVERC_ENCODING_ASN1_H */
