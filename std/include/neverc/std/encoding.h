#ifndef NEVERC_ENCODING_H
#define NEVERC_ENCODING_H

#ifdef __neverc__

struct __neverc_std_hex_t { char __tag; };
struct __neverc_std_base64_t { char __tag; };
struct __neverc_std_base32_t { char __tag; };
struct __neverc_std_ascii85_t { char __tag; };
struct __neverc_std_binary_t { char __tag; };
struct __neverc_std_pem_t { char __tag; };
struct __neverc_std_csv_t { char __tag; };
struct __neverc_std_json_t { char __tag; };
struct __neverc_std_xml_t { char __tag; };
struct __neverc_std_asn1_t { char __tag; };

struct __neverc_std_encoding_t {
    struct __neverc_std_hex_t hex;
    struct __neverc_std_base64_t base64;
    struct __neverc_std_base32_t base32;
    struct __neverc_std_ascii85_t ascii85;
    struct __neverc_std_binary_t binary;
    struct __neverc_std_pem_t pem;
    struct __neverc_std_csv_t csv;
    struct __neverc_std_json_t json;
    struct __neverc_std_xml_t xml;
    struct __neverc_std_asn1_t asn1;
};

extern struct __neverc_std_encoding_t __neverc_mod_encoding;
extern struct __neverc_std_encoding_t encoding;
#endif /* __neverc__ */

#endif /* NEVERC_ENCODING_H */
