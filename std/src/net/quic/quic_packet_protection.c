/*
 * QUIC Packet Protection (RFC 9001 §5)
 *
 * QUIC packets are protected using AEAD (AES-128-GCM or ChaCha20-Poly1305)
 * plus header protection (AES-ECB or ChaCha20).
 *
 * Initial packets use keys derived from the client's Destination Connection ID
 * using HKDF (this provides protection against injection but NOT secrecy).
 *
 * Key derivation for Initial packets (RFC 9001 §5.2):
 *   initial_salt = 0x38762cf7f55934b34d179ae6a4c80cadccbb7f0a (QUIC v1)
 *   initial_secret = HKDF-Extract(initial_salt, client_dst_connection_id)
 *   client_initial_secret = HKDF-Expand-Label(initial_secret, "client in", "", 32)
 *   server_initial_secret = HKDF-Expand-Label(initial_secret, "server in", "", 32)
 *
 *   key = HKDF-Expand-Label(secret, "quic key", "", 16)
 *   iv  = HKDF-Expand-Label(secret, "quic iv", "", 12)
 *   hp  = HKDF-Expand-Label(secret, "quic hp", "", 16)
 */

#include "_quic_internal.h"

#include "neverc/std/_platform.h"
#include "neverc/std/crypto/aes.h"
#include "neverc/std/crypto/gcm.h"
#include "neverc/std/crypto/hkdf.h"

#include <string.h>

/* ======================================================================
 * QUIC v1 Initial Salt (RFC 9001 §5.2)
 * ====================================================================== */

static const uint8_t QUIC_V1_INITIAL_SALT[20] = {
    0x38, 0x76, 0x2c, 0xf7, 0xf5, 0x59, 0x34, 0xb3, 0x4d, 0x17,
    0x9a, 0xe6, 0xa4, 0xc8, 0x0c, 0xad, 0xcc, 0xbb, 0x7f, 0x0a
};

/* QUIC v2 salt (RFC 9369) */
static const uint8_t QUIC_V2_INITIAL_SALT[20] = {
    0x0d, 0xed, 0xe3, 0xde, 0xf7, 0x00, 0xa6, 0xdb, 0x81, 0x93,
    0x81, 0xbe, 0x6e, 0x26, 0x9d, 0xcb, 0xf9, 0xbd, 0x2e, 0xd9
};

/* ======================================================================
 * HKDF-Expand-Label (TLS 1.3 style, RFC 8446 §7.1)
 *
 * HKDF-Expand-Label(Secret, Label, Context, Length) =
 *   HKDF-Expand(Secret, HkdfLabel, Length)
 *
 * struct HkdfLabel {
 *   uint16 length;
 *   opaque label<7..255> = "tls13 " + Label;
 *   opaque context<0..255> = Context;
 * };
 * ====================================================================== */

static int hkdf_expand_label(const uint8_t *secret, size_t secret_len,
                              const char *label, size_t label_len,
                              const uint8_t *context, size_t context_len,
                              uint8_t *out, size_t out_len) {
    if (!secret || !label || !out || secret_len != 32 ||
        out_len > UINT16_MAX || label_len > 249 || context_len > 255 ||
        (!context && context_len != 0))
        return -1;
    /* Build HkdfLabel */
    uint8_t info[512];
    size_t pos = 0;

    /* uint16 length */
    info[pos++] = (uint8_t)(out_len >> 8);
    info[pos++] = (uint8_t)(out_len & 0xFF);

    /* opaque label: "tls13 " + label */
    size_t full_label_len = 6 + label_len;
    info[pos++] = (uint8_t)full_label_len;
    memcpy(info + pos, "tls13 ", 6);
    pos += 6;
    memcpy(info + pos, label, label_len);
    pos += label_len;

    /* opaque context */
    info[pos++] = (uint8_t)context_len;
    if (context_len > 0) {
        memcpy(info + pos, context, context_len);
        pos += context_len;
    }

    return neverc_hkdf_expand_sha256(out, out_len, secret, info, pos);
}

/* ======================================================================
 * Derive Initial Keys
 * ====================================================================== */

int neverc_quic_derive_initial_keys(const uint8_t *dcid, size_t dcid_len,
                                     uint32_t version,
                                     quic_initial_keys_t *keys) {
    const uint8_t *salt;
    if (version == 0x6b3343cf) /* QUIC v2 */
        salt = QUIC_V2_INITIAL_SALT;
    else
        salt = QUIC_V1_INITIAL_SALT;

    /* initial_secret = HKDF-Extract(salt, dcid) */
    uint8_t initial_secret[32];
    if (!dcid || dcid_len == 0 || dcid_len > QUIC_MAX_CID_LEN || !keys ||
        neverc_hkdf_extract_sha256(initial_secret, salt, 20,
                                   dcid, dcid_len) != 0)
        return -1;

    /* Client Initial Secret */
    uint8_t client_secret[32];
    if (hkdf_expand_label(initial_secret, 32, "client in", 9,
                           NULL, 0, client_secret, 32) != 0)
        goto failed;

    /* Server Initial Secret */
    uint8_t server_secret[32];
    if (hkdf_expand_label(initial_secret, 32, "server in", 9,
                           NULL, 0, server_secret, 32) != 0)
        goto failed;

    /* Derive client keys */
    if (hkdf_expand_label(client_secret, 32, "quic key", 8,
                           NULL, 0, keys->client.key, 16) != 0) goto failed;
    if (hkdf_expand_label(client_secret, 32, "quic iv", 7,
                           NULL, 0, keys->client.iv, 12) != 0) goto failed;
    if (hkdf_expand_label(client_secret, 32, "quic hp", 7,
                           NULL, 0, keys->client.hp, 16) != 0) goto failed;

    /* Derive server keys */
    if (hkdf_expand_label(server_secret, 32, "quic key", 8,
                           NULL, 0, keys->server.key, 16) != 0) goto failed;
    if (hkdf_expand_label(server_secret, 32, "quic iv", 7,
                           NULL, 0, keys->server.iv, 12) != 0) goto failed;
    if (hkdf_expand_label(server_secret, 32, "quic hp", 7,
                           NULL, 0, keys->server.hp, 16) != 0) goto failed;

    /* Zero intermediate secrets */
    neverc_platform_secure_zero(initial_secret, sizeof(initial_secret));
    neverc_platform_secure_zero(client_secret, sizeof(client_secret));
    neverc_platform_secure_zero(server_secret, sizeof(server_secret));

    return 0;

failed:
    neverc_platform_secure_zero(initial_secret, sizeof(initial_secret));
    neverc_platform_secure_zero(client_secret, sizeof(client_secret));
    neverc_platform_secure_zero(server_secret, sizeof(server_secret));
    neverc_platform_secure_zero(keys, sizeof(*keys));
    return -1;
}

/* ======================================================================
 * Packet Number Nonce Construction
 *
 * nonce = iv XOR (0-padded packet number)
 * ====================================================================== */

static void construct_nonce(const uint8_t *iv, uint64_t pkt_number,
                             uint8_t *nonce) {
    memcpy(nonce, iv, 12);
    /* XOR packet number into last 8 bytes of IV */
    for (int i = 0; i < 8; i++) {
        nonce[11 - i] ^= (uint8_t)(pkt_number >> (8 * i));
    }
}

/* ======================================================================
 * AEAD Encrypt/Decrypt
 * ====================================================================== */

int neverc_quic_encrypt_payload(const quic_keys_t *keys,
                                 uint64_t pkt_number,
                                 const uint8_t *header, size_t header_len,
                                 const uint8_t *plaintext, size_t pt_len,
                                 uint8_t *out) {
    if (!keys || (!plaintext && pt_len != 0) || !header || !out) return -1;
    uint8_t nonce[12];
    construct_nonce(keys->iv, pkt_number, nonce);
    neverc_gcm_ctx context;
    if (neverc_gcm_init(&context, keys->key, 16) != 0) return -1;
    uint8_t *tag = out + pt_len;
    int result = neverc_gcm_seal(&context, nonce, plaintext, pt_len,
                                 header, header_len, out, tag);
    neverc_platform_secure_zero(&context, sizeof(context));
    neverc_platform_secure_zero(nonce, sizeof(nonce));
    return result;
}

int neverc_quic_decrypt_payload(const quic_keys_t *keys,
                                 uint64_t pkt_number,
                                 const uint8_t *header, size_t header_len,
                                 const uint8_t *ciphertext, size_t ct_len,
                                 uint8_t *out) {
    if (!keys || !header || !ciphertext || !out || ct_len < 16)
        return -1;
    size_t pt_len = ct_len - 16;
    const uint8_t *tag = ciphertext + pt_len;

    uint8_t nonce[12];
    construct_nonce(keys->iv, pkt_number, nonce);

    neverc_gcm_ctx context;
    if (neverc_gcm_init(&context, keys->key, 16) != 0) return -1;
    int result = neverc_gcm_open(&context, nonce, ciphertext, pt_len,
                                 header, header_len, tag, out);
    neverc_platform_secure_zero(&context, sizeof(context));
    neverc_platform_secure_zero(nonce, sizeof(nonce));
    return result;
}

/* ======================================================================
 * Header Protection (RFC 9001 §5.4)
 *
 * Header protection masks the packet number length and packet number bytes
 * using a sample from the ciphertext:
 *   sample = ciphertext[pn_offset+4 .. pn_offset+4+16]
 *   mask = AES-ECB(hp_key, sample)[0..5]
 *
 * For long headers: first byte mask = mask[0] & 0x0f
 * For short headers: first byte mask = mask[0] & 0x1f
 * ====================================================================== */

int neverc_quic_apply_header_protection(const uint8_t *hp_key,
                                          uint8_t *packet, size_t packet_len,
                                          size_t pn_offset) {
    if (!hp_key || !packet || pn_offset > packet_len ||
        packet_len - pn_offset < 20)
        return -1;

    uint8_t pn_len = (packet[0] & 0x03) + 1;
    if (pn_len > packet_len - pn_offset) return -1;

    /* Sample 16 bytes starting at pn_offset + 4 */
    const uint8_t *sample = packet + pn_offset + 4;

    /* Generate mask via AES-ECB */
    uint8_t mask[16];
    neverc_aes_ctx_t aes;
    if (neverc_aes_init(&aes, hp_key, 16) != 0) return -1;
    neverc_aes_encrypt_block(&aes, mask, sample);

    /* Apply mask to first byte */
    int is_long = (packet[0] & 0x80) != 0;
    if (is_long)
        packet[0] ^= (mask[0] & 0x0F);
    else
        packet[0] ^= (mask[0] & 0x1F);

    /* Apply mask to packet number bytes (1-4 bytes) */
    for (int i = 0; i < pn_len; i++) {
        packet[pn_offset + i] ^= mask[1 + i];
    }
    neverc_platform_secure_zero(&aes, sizeof(aes));
    neverc_platform_secure_zero(mask, sizeof(mask));
    return 0;
}

int neverc_quic_remove_header_protection(const uint8_t *hp_key,
                                           uint8_t *packet, size_t packet_len,
                                           size_t pn_offset) {
    if (!hp_key || !packet || pn_offset > packet_len ||
        packet_len - pn_offset < 20)
        return -1;

    const uint8_t *sample = packet + pn_offset + 4;
    uint8_t mask[16];
    neverc_aes_ctx_t aes;
    if (neverc_aes_init(&aes, hp_key, 16) != 0) return -1;
    neverc_aes_encrypt_block(&aes, mask, sample);

    int is_long = (packet[0] & 0x80) != 0;
    packet[0] ^= is_long ? (mask[0] & 0x0f) : (mask[0] & 0x1f);
    uint8_t pn_len = (packet[0] & 0x03) + 1;
    if (pn_len > packet_len - pn_offset) return -1;
    for (int i = 0; i < pn_len; i++)
        packet[pn_offset + i] ^= mask[1 + i];
    neverc_platform_secure_zero(&aes, sizeof(aes));
    neverc_platform_secure_zero(mask, sizeof(mask));
    return 0;
}
