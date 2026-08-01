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

#include <string.h>

/* External crypto functions from NeverC crypto module */
extern void neverc_sha256_init(void *ctx);
extern void neverc_sha256_update(void *ctx, const void *data, size_t len);
extern void neverc_sha256_final(void *ctx, uint8_t *out);
extern int neverc_hkdf_extract_sha256(const uint8_t *salt, size_t salt_len,
                                       const uint8_t *ikm, size_t ikm_len,
                                       uint8_t *prk);
extern int neverc_hkdf_expand_sha256(const uint8_t *prk, size_t prk_len,
                                      const uint8_t *info, size_t info_len,
                                      uint8_t *okm, size_t okm_len);

/* AES-128-GCM */
extern int neverc_gcm_seal(const uint8_t *key, size_t key_len,
                            const uint8_t *nonce, size_t nonce_len,
                            const uint8_t *plaintext, size_t pt_len,
                            const uint8_t *aad, size_t aad_len,
                            uint8_t *out, uint8_t *tag);
extern int neverc_gcm_open(const uint8_t *key, size_t key_len,
                            const uint8_t *nonce, size_t nonce_len,
                            const uint8_t *ciphertext, size_t ct_len,
                            const uint8_t *aad, size_t aad_len,
                            const uint8_t *tag,
                            uint8_t *out);

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

    return neverc_hkdf_expand_sha256(secret, secret_len, info, pos, out, out_len);
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
    if (neverc_hkdf_extract_sha256(salt, 20, dcid, dcid_len, initial_secret) != 0)
        return -1;

    /* Client Initial Secret */
    uint8_t client_secret[32];
    if (hkdf_expand_label(initial_secret, 32, "client in", 9,
                           NULL, 0, client_secret, 32) != 0)
        return -1;

    /* Server Initial Secret */
    uint8_t server_secret[32];
    if (hkdf_expand_label(initial_secret, 32, "server in", 9,
                           NULL, 0, server_secret, 32) != 0)
        return -1;

    /* Derive client keys */
    if (hkdf_expand_label(client_secret, 32, "quic key", 8,
                           NULL, 0, keys->client.key, 16) != 0) return -1;
    if (hkdf_expand_label(client_secret, 32, "quic iv", 7,
                           NULL, 0, keys->client.iv, 12) != 0) return -1;
    if (hkdf_expand_label(client_secret, 32, "quic hp", 7,
                           NULL, 0, keys->client.hp, 16) != 0) return -1;

    /* Derive server keys */
    if (hkdf_expand_label(server_secret, 32, "quic key", 8,
                           NULL, 0, keys->server.key, 16) != 0) return -1;
    if (hkdf_expand_label(server_secret, 32, "quic iv", 7,
                           NULL, 0, keys->server.iv, 12) != 0) return -1;
    if (hkdf_expand_label(server_secret, 32, "quic hp", 7,
                           NULL, 0, keys->server.hp, 16) != 0) return -1;

    /* Zero intermediate secrets */
    memset(initial_secret, 0, sizeof(initial_secret));
    memset(client_secret, 0, sizeof(client_secret));
    memset(server_secret, 0, sizeof(server_secret));

    return 0;
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
    uint8_t nonce[12];
    construct_nonce(keys->iv, pkt_number, nonce);

    /* out = ciphertext || tag (16 bytes) */
    uint8_t *tag = out + pt_len;
    return neverc_gcm_seal(keys->key, 16, nonce, 12,
                            plaintext, pt_len,
                            header, header_len,
                            out, tag);
}

int neverc_quic_decrypt_payload(const quic_keys_t *keys,
                                 uint64_t pkt_number,
                                 const uint8_t *header, size_t header_len,
                                 const uint8_t *ciphertext, size_t ct_len,
                                 uint8_t *out) {
    if (ct_len < 16) return -1;  /* Must have at least a tag */
    size_t pt_len = ct_len - 16;
    const uint8_t *tag = ciphertext + pt_len;

    uint8_t nonce[12];
    construct_nonce(keys->iv, pkt_number, nonce);

    return neverc_gcm_open(keys->key, 16, nonce, 12,
                            ciphertext, pt_len,
                            header, header_len,
                            tag, out);
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

/* Simple AES-128-ECB for header protection (single block) */
extern void neverc_aes128_encrypt_block(const uint8_t *key,
                                          const uint8_t *in, uint8_t *out);

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
    neverc_aes128_encrypt_block(hp_key, sample, mask);

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
    neverc_aes128_encrypt_block(hp_key, sample, mask);

    int is_long = (packet[0] & 0x80) != 0;
    packet[0] ^= is_long ? (mask[0] & 0x0f) : (mask[0] & 0x1f);
    uint8_t pn_len = (packet[0] & 0x03) + 1;
    if (pn_len > packet_len - pn_offset) return -1;
    for (int i = 0; i < pn_len; i++)
        packet[pn_offset + i] ^= mask[1 + i];
    return 0;
}
