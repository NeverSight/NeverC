#include "neverc/std/crypto/x509.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    int version;
    uint8_t serial[20];
    int serial_len;
    int sig_algorithm;
    neverc_x509_name_t issuer;
    neverc_x509_name_t subject;
    neverc_x509_time_t not_before;
    neverc_x509_time_t not_after;
    int key_algorithm;
    int public_key_curve;
    uint8_t *public_key;
    size_t public_key_len;
    const uint8_t *raw_tbs;
    size_t raw_tbs_len;
    const uint8_t *signature;
    size_t signature_len;
    const uint8_t *raw_issuer;
    size_t raw_issuer_len;
    const uint8_t *raw_subject;
    size_t raw_subject_len;
    int is_ca;
    int basic_constraints_valid;
    int max_path_len;
    uint16_t key_usage;
    int key_usage_present;
    uint32_t ext_key_usage;
    int ext_key_usage_present;
    int has_unhandled_critical_extension;
    char **dns_names;
    size_t dns_name_count;
    neverc_x509_ip_address_t *ip_addresses;
    size_t ip_address_count;
    const uint8_t *raw;
    size_t raw_len;
} v3389_x509_cert_t;

#if UINTPTR_MAX == UINT64_MAX
_Static_assert(sizeof(v3389_x509_cert_t) == 2520,
               "v3389 64-bit X509 certificate size");
#elif UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(v3389_x509_cert_t) == 2456,
               "v3389 32-bit X509 certificate size");
#endif

#define ABI_TYPE_EQ(current, legacy)                                     \
    _Static_assert(sizeof(current) == sizeof(legacy), "v3389 size ABI"); \
    _Static_assert(_Alignof(current) == _Alignof(legacy),                \
                   "v3389 alignment ABI")
#define ABI_FIELD_EQ(current, legacy, field)                             \
    _Static_assert(offsetof(current, field) == offsetof(legacy, field),  \
                   "v3389 field offset ABI")

ABI_TYPE_EQ(neverc_x509_cert_t, v3389_x509_cert_t);
ABI_FIELD_EQ(neverc_x509_cert_t, v3389_x509_cert_t, version);
ABI_FIELD_EQ(neverc_x509_cert_t, v3389_x509_cert_t, serial);
ABI_FIELD_EQ(neverc_x509_cert_t, v3389_x509_cert_t, serial_len);
ABI_FIELD_EQ(neverc_x509_cert_t, v3389_x509_cert_t, sig_algorithm);
ABI_FIELD_EQ(neverc_x509_cert_t, v3389_x509_cert_t, issuer);
ABI_FIELD_EQ(neverc_x509_cert_t, v3389_x509_cert_t, subject);
ABI_FIELD_EQ(neverc_x509_cert_t, v3389_x509_cert_t, not_before);
ABI_FIELD_EQ(neverc_x509_cert_t, v3389_x509_cert_t, not_after);
ABI_FIELD_EQ(neverc_x509_cert_t, v3389_x509_cert_t, key_algorithm);
ABI_FIELD_EQ(neverc_x509_cert_t, v3389_x509_cert_t, public_key_curve);
ABI_FIELD_EQ(neverc_x509_cert_t, v3389_x509_cert_t, public_key);
ABI_FIELD_EQ(neverc_x509_cert_t, v3389_x509_cert_t, public_key_len);
ABI_FIELD_EQ(neverc_x509_cert_t, v3389_x509_cert_t, raw_tbs);
ABI_FIELD_EQ(neverc_x509_cert_t, v3389_x509_cert_t, raw_tbs_len);
ABI_FIELD_EQ(neverc_x509_cert_t, v3389_x509_cert_t, signature);
ABI_FIELD_EQ(neverc_x509_cert_t, v3389_x509_cert_t, signature_len);
ABI_FIELD_EQ(neverc_x509_cert_t, v3389_x509_cert_t, raw_issuer);
ABI_FIELD_EQ(neverc_x509_cert_t, v3389_x509_cert_t, raw_issuer_len);
ABI_FIELD_EQ(neverc_x509_cert_t, v3389_x509_cert_t, raw_subject);
ABI_FIELD_EQ(neverc_x509_cert_t, v3389_x509_cert_t, raw_subject_len);
ABI_FIELD_EQ(neverc_x509_cert_t, v3389_x509_cert_t, is_ca);
ABI_FIELD_EQ(neverc_x509_cert_t, v3389_x509_cert_t,
             basic_constraints_valid);
ABI_FIELD_EQ(neverc_x509_cert_t, v3389_x509_cert_t, max_path_len);
ABI_FIELD_EQ(neverc_x509_cert_t, v3389_x509_cert_t, key_usage);
ABI_FIELD_EQ(neverc_x509_cert_t, v3389_x509_cert_t, key_usage_present);
ABI_FIELD_EQ(neverc_x509_cert_t, v3389_x509_cert_t, ext_key_usage);
ABI_FIELD_EQ(neverc_x509_cert_t, v3389_x509_cert_t,
             ext_key_usage_present);
ABI_FIELD_EQ(neverc_x509_cert_t, v3389_x509_cert_t,
             has_unhandled_critical_extension);
ABI_FIELD_EQ(neverc_x509_cert_t, v3389_x509_cert_t, dns_names);
ABI_FIELD_EQ(neverc_x509_cert_t, v3389_x509_cert_t, dns_name_count);
ABI_FIELD_EQ(neverc_x509_cert_t, v3389_x509_cert_t, ip_addresses);
ABI_FIELD_EQ(neverc_x509_cert_t, v3389_x509_cert_t, ip_address_count);
ABI_FIELD_EQ(neverc_x509_cert_t, v3389_x509_cert_t, raw);
ABI_FIELD_EQ(neverc_x509_cert_t, v3389_x509_cert_t, raw_len);

/* Minimal certificate-shaped DER used only to exercise private, on-demand
 * extraction. It contains permitted DNS example.com, permitted IPv4
 * 10.0.0.0/8, and excluded DNS evil.example.com. */
static const uint8_t constrained_certificate_shell[] = {
    0x30, 0x67, 0x30, 0x52, 0x02, 0x01, 0x01,
    0x30, 0x00, 0x30, 0x00, 0x30, 0x00, 0x30, 0x00, 0x30, 0x00,
    0xa3, 0x43, 0x30, 0x41, 0x30, 0x3f,
    0x06, 0x03, 0x55, 0x1d, 0x1e, 0x01, 0x01, 0xff, 0x04, 0x35,
    0x30, 0x33,
    0xa0, 0x1b,
    0x30, 0x0d, 0x82, 0x0b,
    'e', 'x', 'a', 'm', 'p', 'l', 'e', '.', 'c', 'o', 'm',
    0x30, 0x0a, 0x87, 0x08,
    0x0a, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00,
    0xa1, 0x14, 0x30, 0x12, 0x82, 0x10,
    'e', 'v', 'i', 'l', '.', 'e', 'x', 'a', 'm', 'p', 'l', 'e', '.',
    'c', 'o', 'm',
    0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d,
    0x01, 0x01, 0x0b, 0x05, 0x00, 0x03, 0x02, 0x00, 0x00
};

enum { CANARY_SIZE = 32 };

typedef struct {
    uint8_t before[CANARY_SIZE];
    union {
        neverc_x509_cert_t now;
        v3389_x509_cert_t old;
    } value;
    uint8_t after[CANARY_SIZE];
} guarded_cert_t;

static int canaries_ok(const guarded_cert_t *guarded) {
    for (size_t i = 0; i < CANARY_SIZE; ++i) {
        if (guarded->before[i] != 0xa5 || guarded->after[i] != 0x5a)
            return 0;
    }
    return 1;
}

int main(void) {
    guarded_cert_t guarded;
    memset(&guarded, 0, sizeof(guarded));
    memset(guarded.before, 0xa5, sizeof(guarded.before));
    memset(guarded.after, 0x5a, sizeof(guarded.after));

    guarded.value.now.raw = constrained_certificate_shell;
    guarded.value.now.raw_len = sizeof(constrained_certificate_shell);
    if (neverc_x509_has_name_constraints(&guarded.value.now) != 1 ||
        !canaries_ok(&guarded))
        return 1;

    guarded.value.now.raw = NULL;
    guarded.value.now.raw_len = 0;
    if (neverc_x509_has_name_constraints(&guarded.value.now) != 0 ||
        neverc_x509_has_name_constraints(NULL) != 0 ||
        !canaries_ok(&guarded))
        return 1;

    static const uint8_t malformed[] = {0x30, 0x00};
    guarded.value.now.raw = malformed;
    guarded.value.now.raw_len = sizeof(malformed);
    if (neverc_x509_has_name_constraints(&guarded.value.now) != -1 ||
        !canaries_ok(&guarded))
        return 1;

    puts("passed");
    return 0;
}
