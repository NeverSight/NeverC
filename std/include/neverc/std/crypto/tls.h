#ifndef NEVERC_CRYPTO_TLS_H
#define NEVERC_CRYPTO_TLS_H

/*
 * NeverC crypto/tls — experimental TLS 1.3 (RFC 8446) components.
 *
 * The key schedule and record layer now verify supported peer
 * CertificateVerify signatures, but certificate-chain/system-trust validation
 * and server-side signing are incomplete. Connection and listener entry points
 * therefore remain fail closed.
 *
 * Go-style API:
 *   // Client
 *   neverc_tls_conn_t *conn = neverc_tls_dial("example.com:443", NULL);
 *   neverc_tls_write(conn, data, len);
 *   n = neverc_tls_read(conn, buf, buflen);
 *   neverc_tls_close(conn);
 *
 *   // Server
 *   neverc_tls_config_t *cfg = neverc_tls_config_new();
 *   neverc_tls_config_load_cert(cfg, "cert.pem", "key.pem");
 *   neverc_tls_listener_t *ln = neverc_tls_listen(":443", cfg);
 *   neverc_tls_conn_t *conn = neverc_tls_accept(ln, NULL);
 *
 * Cross-platform: POSIX + WinSock.
 */

#include <stddef.h>
#include <stdint.h>
#include "neverc/std/crypto/x509.h"
#include "neverc/std/net/tcp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- TLS Version --- */
#define NEVERC_TLS_VERSION_13  0x0304

/* --- Cipher Suites --- */
#define NEVERC_TLS_AES_128_GCM_SHA256       0x1301
#define NEVERC_TLS_AES_256_GCM_SHA384       0x1302
#define NEVERC_TLS_CHACHA20_POLY1305_SHA256  0x1303

/* --- Key Exchange Groups --- */
#define NEVERC_TLS_GROUP_X25519    0x001D
#define NEVERC_TLS_GROUP_SECP256R1 0x0017
#define NEVERC_TLS_GROUP_SECP384R1 0x0018

/* --- Signature Schemes --- */
#define NEVERC_TLS_SIGNATURE_ECDSA_SECP256R1_SHA256 0x0403
#define NEVERC_TLS_SIGNATURE_RSA_PSS_RSAE_SHA256    0x0804
#define NEVERC_TLS_SIGNATURE_RSA_PSS_RSAE_SHA384    0x0805
#define NEVERC_TLS_SIGNATURE_RSA_PSS_RSAE_SHA512    0x0806
#define NEVERC_TLS_SIGNATURE_ED25519                0x0807

/* --- TLS Configuration --- */
typedef struct neverc_tls_config neverc_tls_config_t;

/* Create a new TLS config. Default: TLS 1.3, X25519, AES-128-GCM-SHA256. */
neverc_tls_config_t *neverc_tls_config_new(void);

/* Free a TLS config. */
void neverc_tls_config_free(neverc_tls_config_t *cfg);

/* Load certificate and private key from PEM files (for server). Returns 0 on success. */
int neverc_tls_config_load_cert(neverc_tls_config_t *cfg,
                                 const char *cert_pem_path,
                                 const char *key_pem_path);

/* Load certificate and key from PEM strings (for server). Returns 0 on success. */
int neverc_tls_config_load_cert_mem(neverc_tls_config_t *cfg,
                                     const char *cert_pem,
                                     const char *key_pem);

/* Set ALPN protocols (e.g., "h2", "http/1.1"). */
void neverc_tls_config_set_alpn(neverc_tls_config_t *cfg,
                                 const char **protocols, int count);

/* Skip certificate verification (insecure, for testing only). */
void neverc_tls_config_insecure_skip_verify(neverc_tls_config_t *cfg);

/* Set server name for SNI (client only, auto-set by tls_dial). */
void neverc_tls_config_set_server_name(neverc_tls_config_t *cfg,
                                        const char *name);

/* --- TLS Connection --- */
typedef struct neverc_tls_conn neverc_tls_conn_t;

/* Returns NULL with an unavailable error until verification is complete. */
neverc_tls_conn_t *neverc_tls_dial(const char *addr,
                                    neverc_tls_config_t *cfg,
                                    const char **errp);

/* Returns NULL with an unavailable error until verification is complete. */
neverc_tls_conn_t *neverc_tls_server(neverc_tcp_conn_t *tcp,
                                      neverc_tls_config_t *cfg,
                                      const char **errp);

/* Returns NULL with an unavailable error until verification is complete. */
neverc_tls_conn_t *neverc_tls_client(neverc_tcp_conn_t *tcp,
                                      neverc_tls_config_t *cfg,
                                      const char **errp);

/* Read decrypted data. Returns bytes read, 0 on close, -1 on error. */
int neverc_tls_read(neverc_tls_conn_t *conn, void *buf, size_t buflen);

/* Write data (encrypted before sending). Returns bytes written or -1. */
int neverc_tls_write(neverc_tls_conn_t *conn, const void *data, size_t len);

/* Close TLS connection (sends close_notify alert). */
void neverc_tls_close(neverc_tls_conn_t *conn);

/* Get the negotiated ALPN protocol (or NULL). */
const char *neverc_tls_alpn(neverc_tls_conn_t *conn);

/* Get the negotiated cipher suite. */
uint16_t neverc_tls_cipher_suite(neverc_tls_conn_t *conn);

/* Get the peer's certificate (DER-encoded, caller must NOT free). */
const uint8_t *neverc_tls_peer_certificate(neverc_tls_conn_t *conn,
                                            size_t *out_len);

/* Verify a TLS 1.3 CertificateVerify signature over a SHA-256 or SHA-384
 * transcript. The signature scheme's hash is independent of the transcript
 * hash selected by the cipher suite.
 * from_server is nonzero for the server context and zero for the client.
 * Returns 0 on success and -1 on malformed input, mismatch, or an unsupported
 * certificate/signature-scheme combination. */
int neverc_tls_verify_certificate_verify(
    const neverc_x509_cert_t *certificate,
    uint16_t signature_scheme,
    int from_server,
    const uint8_t *transcript_hash,
    size_t transcript_hash_len,
    const uint8_t *signature,
    size_t signature_len);

/* Sign a TLS 1.3 CertificateVerify message with the private key loaded in
 * config. The current implementation supports ECDSA P-256/SHA-256 and writes
 * a DER-encoded ECDSA signature. Returns 0 on success. */
int neverc_tls_sign_certificate_verify(
    const neverc_tls_config_t *config,
    int from_server,
    const uint8_t *transcript_hash,
    size_t transcript_hash_len,
    uint16_t *signature_scheme,
    uint8_t *signature,
    size_t signature_capacity,
    size_t *signature_len);

/* --- TLS Listener (for HTTPS server) --- */
typedef struct neverc_tls_listener neverc_tls_listener_t;

/* Returns NULL with an unavailable error until verification is complete. */
neverc_tls_listener_t *neverc_tls_listen(const char *addr,
                                          neverc_tls_config_t *cfg,
                                          const char **errp);

/* Returns NULL with an unavailable error until verification is complete. */
neverc_tls_conn_t *neverc_tls_accept(neverc_tls_listener_t *ln,
                                      const char **errp);

/* Close the listener. */
void neverc_tls_listener_close(neverc_tls_listener_t *ln);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
#include <neverc/std/crypto.h>
#endif

#endif /* NEVERC_CRYPTO_TLS_H */
