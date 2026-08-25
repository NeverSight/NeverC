#ifndef NEVERC_CRYPTO_TLS_H
#define NEVERC_CRYPTO_TLS_H

/*
 * NeverC crypto/tls — experimental TLS 1.3 (RFC 8446) components.
 *
 * The key schedule and record layer verify supported peer CertificateVerify
 * signatures, build certificate chains from custom or system roots, and sign
 * with P-256 keys. Initial-handshake mTLS, KeyUpdate, and stateful TLS 1.3
 * PSK-DHE session resumption are implemented. Post-handshake authentication
 * and independent security validation remain future hardening work. The
 * interoperable transport is enabled by default; define
 * NEVERC_TLS_DISABLE_TRANSPORT to compile entry points fail closed.
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

/* Server-side client certificate policy. */
#define NEVERC_TLS_CLIENT_AUTH_NONE               0
#define NEVERC_TLS_CLIENT_AUTH_REQUIRE_AND_VERIFY 1

/* Create a new TLS config and return one owned reference.
 * Default: TLS 1.3, X25519, AES-128-GCM-SHA256. */
neverc_tls_config_t *neverc_tls_config_new(void);

/* Release the caller's reference. Do not use cfg or pointers borrowed from it
 * after this call, and do not release the same reference concurrently with an
 * API call that is still using it. Successful connections/listeners retain
 * their own reference. */
void neverc_tls_config_free(neverc_tls_config_t *cfg);

/* A config becomes immutable when first passed to a dial, connection, or
 * listener function. Finish all certificate, root, ALPN, SNI, verification,
 * and client-auth setup first. Later int-returning setters fail with -1 and
 * void setters leave the frozen config unchanged. This makes one configured
 * instance safe to reuse concurrently without configuration data races. */

/* Load a local certificate and P-256 private key from PEM files.
 * SEC1 "EC PRIVATE KEY" and unencrypted PKCS#8 "PRIVATE KEY" are accepted.
 * Returns 0 on success. */
int neverc_tls_config_load_cert(neverc_tls_config_t *cfg,
                                 const char *cert_pem_path,
                                 const char *key_pem_path);

/* Load a local certificate and SEC1 or unencrypted PKCS#8 P-256 key from PEM
 * strings. Returns 0 on success. */
int neverc_tls_config_load_cert_mem(neverc_tls_config_t *cfg,
                                     const char *cert_pem,
                                     const char *key_pem);

/* Add one or more trusted root certificates from a PEM file/buffer.
 * Each call must add at least one new valid CERTIFICATE block. */
int neverc_tls_config_add_root_certificates(
    neverc_tls_config_t *cfg, const char *pem_path);
int neverc_tls_config_add_root_certificates_mem(
    neverc_tls_config_t *cfg, const char *pem, size_t pem_len);

/* Set ALPN protocols (e.g., "h2", "http/1.1"). */
void neverc_tls_config_set_alpn(neverc_tls_config_t *cfg,
                                 const char **protocols, int count);

/* Skip certificate verification (insecure, for testing only). */
void neverc_tls_config_insecure_skip_verify(neverc_tls_config_t *cfg);

/* Set server name for SNI (client only). tls_dial infers from addr when
 * this is empty, but does not persist the inferred name on cfg. */
void neverc_tls_config_set_server_name(neverc_tls_config_t *cfg,
                                        const char *name);

/* Current configured SNI. Empty/NULL means tls_dial should infer. The result
 * is borrowed from cfg: before first use, callers must synchronize this getter
 * with set_server_name, and the next successful setter immediately invalidates
 * the old result. After cfg is frozen, the result remains stable only while the
 * caller keeps its cfg reference alive. */
const char *neverc_tls_config_server_name(const neverc_tls_config_t *cfg);

/* Set the server-side client certificate policy. REQUIRE_AND_VERIFY sends a
 * TLS 1.3 CertificateRequest and validates the client certificate against the
 * roots added to this config with config_add_root_certificates*. */
int neverc_tls_config_set_client_auth(neverc_tls_config_t *cfg, int mode);

/* --- TLS Connection --- */
typedef struct neverc_tls_conn neverc_tls_conn_t;

/* Dial and complete a verified TLS 1.3 client handshake. */
neverc_tls_conn_t *neverc_tls_dial(const char *addr,
                                    neverc_tls_config_t *cfg,
                                    const char **errp);

/* Wrap TCP and complete a TLS 1.3 server handshake. */
neverc_tls_conn_t *neverc_tls_server(neverc_tcp_conn_t *tcp,
                                      neverc_tls_config_t *cfg,
                                      const char **errp);

/* Wrap TCP and complete a verified TLS 1.3 client handshake. */
neverc_tls_conn_t *neverc_tls_client(neverc_tcp_conn_t *tcp,
                                      neverc_tls_config_t *cfg,
                                      const char **errp);

/* As above, but interrupt the verified handshake when ctx is cancelled or its
 * deadline expires. The returned TLS connection borrows tcp; whether the
 * handshake succeeds or fails, the caller remains responsible for tcp. */
neverc_tls_conn_t *neverc_tls_client_context(
    neverc_tcp_conn_t *tcp, neverc_tls_config_t *cfg,
    neverc_context_t *ctx, const char **errp);

/* Reactor-driven server handshake. server_begin validates configuration and
 * creates a TLS connection without waiting for network I/O. Repeatedly call
 * handshake_step when the socket is readable/writable, updating poller
 * interest from its result. COMPLETE is returned only after peer Finished has
 * been authenticated and all queued handshake records have been written. */
#define NEVERC_TLS_HANDSHAKE_COMPLETE   0
#define NEVERC_TLS_HANDSHAKE_WANT_READ  1
#define NEVERC_TLS_HANDSHAKE_WANT_WRITE 2
#define NEVERC_TLS_HANDSHAKE_ERROR     (-1)

neverc_tls_conn_t *neverc_tls_server_begin(
    neverc_tcp_conn_t *tcp, neverc_tls_config_t *cfg, const char **errp);
int neverc_tls_handshake_step(neverc_tls_conn_t *conn, const char **errp);

typedef enum {
    NEVERC_TLS_IO_OK = 0,
    NEVERC_TLS_IO_WANT_READ,
    NEVERC_TLS_IO_WANT_WRITE,
    NEVERC_TLS_IO_EOF,
    NEVERC_TLS_IO_ERROR,
} neverc_tls_io_status_t;

typedef struct {
    neverc_tls_io_status_t status;
    size_t transferred;
} neverc_tls_io_result_t;

/* Non-blocking application record I/O for reactor-owned connections. A write
 * result may report WANT_WRITE with transferred > 0: those plaintext bytes
 * were accepted and must not be submitted again, while the generated record
 * remains queued for the next writable event. */
neverc_tls_io_result_t neverc_tls_try_read(
    neverc_tls_conn_t *conn, void *buffer, size_t capacity);
neverc_tls_io_result_t neverc_tls_try_write(
    neverc_tls_conn_t *conn, const void *data, size_t length);
neverc_tls_io_result_t neverc_tls_flush(neverc_tls_conn_t *conn);
neverc_tls_io_result_t neverc_tls_try_close_notify(
    neverc_tls_conn_t *conn);

/* Change ownership between a poller and a blocking protocol loop after the
 * handshake. Disabling reactor mode requires an empty encrypted write queue.
 * Preloaded application bytes are returned before any bytes already buffered
 * by the record layer; this preserves data read ahead by the old owner. */
int neverc_tls_set_reactor_mode(neverc_tls_conn_t *conn, int enabled);
int neverc_tls_preload_application_data(
    neverc_tls_conn_t *conn, const void *data, size_t length);

/* Read decrypted data. Returns bytes read, 0 on close, -1 on error.
 * Concurrent reads are serialized; one reader may run alongside writers. */
int neverc_tls_read(neverc_tls_conn_t *conn, void *buf, size_t buflen);

/* As above, interrupted by context cancellation or deadline expiry. */
int neverc_tls_read_context(neverc_tls_conn_t *conn, neverc_context_t *ctx,
                            void *buf, size_t buflen);

/* Write data (encrypted before sending). Returns bytes written or -1.
 * Concurrent writes and key updates are serialized per connection. */
int neverc_tls_write(neverc_tls_conn_t *conn, const void *data, size_t len);

/* As above, with one context budget across all generated TLS records. */
int neverc_tls_write_context(neverc_tls_conn_t *conn, neverc_context_t *ctx,
                             const void *data, size_t len);

/* Interrupt one transport direction. shutdown_read may be used to wake a
 * concurrent blocked reader; shutdown_write waits for an active writer. */
int neverc_tls_shutdown_read(neverc_tls_conn_t *conn);
int neverc_tls_shutdown_write(neverc_tls_conn_t *conn);

/* Rotate the TLS 1.3 application write keys. If request_peer_update is 1,
 * request that the peer rotate its write keys too; 0 only rotates this side.
 * Returns 0 on success or -1 for invalid state/input or an I/O failure. */
int neverc_tls_key_update(neverc_tls_conn_t *conn, int request_peer_update);

/* Close TLS connection (sends close_notify alert). The caller must first
 * ensure that no read, write, or key-update operation is still running. */
void neverc_tls_close(neverc_tls_conn_t *conn);

/* Get the negotiated ALPN protocol (or NULL). */
const char *neverc_tls_alpn(neverc_tls_conn_t *conn);

/* Get the SNI server name sent by the client (or NULL). On a client
 * connection this is the configured outbound SNI value; on a server
 * connection it is the validated host_name received in ClientHello. */
const char *neverc_tls_server_name(neverc_tls_conn_t *conn);

/* Get the negotiated cipher suite. */
uint16_t neverc_tls_cipher_suite(neverc_tls_conn_t *conn);

/* Get the peer's certificate (DER-encoded, caller must NOT free). */
const uint8_t *neverc_tls_peer_certificate(neverc_tls_conn_t *conn,
                                            size_t *out_len);

#if defined(NEVERC_TLS_TESTING)
int neverc_tls_test_config_set_handshake_fragment_size(
    neverc_tls_config_t *cfg, size_t fragment_size);
int neverc_tls_test_handshake_reassembly(void);
int neverc_tls_test_did_resume(neverc_tls_conn_t *conn);
int neverc_tls_test_corrupt_client_session(
    neverc_tls_config_t *cfg);
int neverc_tls_test_expire_client_session(
    neverc_tls_config_t *cfg);
int neverc_tls_test_expire_server_sessions(
    neverc_tls_config_t *cfg);
int neverc_tls_test_set_client_session_alpn(
    neverc_tls_config_t *cfg, const char *alpn);
int neverc_tls_test_resize_client_session_ticket(
    neverc_tls_config_t *cfg, size_t ticket_len);
#endif

/* Verify a DER server certificate through intermediates to the roots configured
 * on config, or to the platform roots when no custom roots were added.
 * config must contain a non-empty server name. If moment is NULL, current UTC
 * is used. This function always verifies and ignores insecure_skip_verify. */
int neverc_tls_verify_server_certificate_chain(
    const neverc_tls_config_t *config,
    const uint8_t *leaf_der,
    size_t leaf_der_len,
    const neverc_x509_cert_pool_t *intermediates,
    const neverc_x509_time_t *moment);

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

#if defined(NEVERC_TLS_TESTING)
/* Fuzz arbitrary record fragmentation through the production handshake
 * reassembler. Returns 0 after fully consuming or rejecting the input. */
int neverc_tls_test_fuzz_handshake_reassembly(
    const uint8_t *data, size_t data_len);
#endif

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

/* Listen for TLS 1.3 connections. */
neverc_tls_listener_t *neverc_tls_listen(const char *addr,
                                          neverc_tls_config_t *cfg,
                                          const char **errp);

/* Accept and complete a TLS 1.3 server handshake. */
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
