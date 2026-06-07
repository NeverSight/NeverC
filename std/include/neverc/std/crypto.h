#ifndef NEVERC_CRYPTO_H
#define NEVERC_CRYPTO_H

#ifdef __neverc__

struct __neverc_std_sha256_t { char __tag; };
struct __neverc_std_sha1_t { char __tag; };
struct __neverc_std_sha512_t { char __tag; };
struct __neverc_std_sha384_t { char __tag; };
struct __neverc_std_sha224_t { char __tag; };
struct __neverc_std_sha3_t { char __tag; };
struct __neverc_std_sha512_224_t { char __tag; };
struct __neverc_std_sha512_256_t { char __tag; };
struct __neverc_std_md5_t { char __tag; };
struct __neverc_std_aes_t { char __tag; };
struct __neverc_std_des_t { char __tag; };
struct __neverc_std_rc4_t { char __tag; };
struct __neverc_std_chacha20_t { char __tag; };
struct __neverc_std_poly1305_t { char __tag; };
struct __neverc_std_chacha20poly1305_t { char __tag; };
struct __neverc_std_gcm_t { char __tag; };
struct __neverc_std_cipher_t { char __tag; };
struct __neverc_std_hmac_t { char __tag; };
struct __neverc_std_subtle_t { char __tag; };
struct __neverc_std_hkdf_t { char __tag; };
struct __neverc_std_pbkdf2_t { char __tag; };
struct __neverc_std_x509_t { char __tag; };
struct __neverc_std_crypto_rand_t { char __tag; };
struct __neverc_std_elliptic_t { char __tag; };
struct __neverc_std_rsa_t { char __tag; };
struct __neverc_std_ecdsa_t { char __tag; };
struct __neverc_std_dsa_t { char __tag; };
struct __neverc_std_ed25519_t { char __tag; };
struct __neverc_std_ecdh_t { char __tag; };

struct __neverc_std_crypto_t {
    struct __neverc_std_sha256_t sha256;
    struct __neverc_std_sha1_t sha1;
    struct __neverc_std_sha512_t sha512;
    struct __neverc_std_sha384_t sha384;
    struct __neverc_std_sha224_t sha224;
    struct __neverc_std_sha3_t sha3;
    struct __neverc_std_sha512_224_t sha512_224;
    struct __neverc_std_sha512_256_t sha512_256;
    struct __neverc_std_md5_t md5;
    struct __neverc_std_aes_t aes;
    struct __neverc_std_des_t des;
    struct __neverc_std_rc4_t rc4;
    struct __neverc_std_chacha20_t chacha20;
    struct __neverc_std_poly1305_t poly1305;
    struct __neverc_std_chacha20poly1305_t chacha20poly1305;
    struct __neverc_std_gcm_t gcm;
    struct __neverc_std_cipher_t cipher;
    struct __neverc_std_hmac_t hmac;
    struct __neverc_std_subtle_t subtle;
    struct __neverc_std_hkdf_t hkdf;
    struct __neverc_std_pbkdf2_t pbkdf2;
    struct __neverc_std_x509_t x509;
    struct __neverc_std_crypto_rand_t rand;
    struct __neverc_std_elliptic_t elliptic;
    struct __neverc_std_rsa_t rsa;
    struct __neverc_std_ecdsa_t ecdsa;
    struct __neverc_std_dsa_t dsa;
    struct __neverc_std_ed25519_t ed25519;
    struct __neverc_std_ecdh_t ecdh;
};

extern struct __neverc_std_crypto_t __neverc_mod_crypto;
extern struct __neverc_std_crypto_t crypto;
#endif /* __neverc__ */

#endif /* NEVERC_CRYPTO_H */
