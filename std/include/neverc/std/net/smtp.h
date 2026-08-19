#ifndef NEVERC_NET_SMTP_H
#define NEVERC_NET_SMTP_H

/*
 * NeverC net/smtp — SMTP client (mirrors Go net/smtp, RFC 5321).
 *
 * Features:
 *   - SMTP connection and EHLO/HELO
 *   - RFC 3207 STARTTLS (fail-closed; mirrors Go smtp.Client.StartTLS)
 *   - PLAIN/LOGIN authentication
 *   - MAIL FROM / RCPT TO / DATA
 *   - Convenience: SendMail()
 *
 * Cross-platform: POSIX + WinSock.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct neverc_smtp_client neverc_smtp_client_t;
struct neverc_tls_config;

/* Auth method */
typedef enum {
    NEVERC_SMTP_AUTH_NONE  = 0,
    NEVERC_SMTP_AUTH_PLAIN = 1,
    NEVERC_SMTP_AUTH_LOGIN = 2,
} neverc_smtp_auth_method_t;

/* --- Connection --- */

/* Connect to an SMTP server. addr is "host:port" or "[IPv6]:port"
 * (Go net.SplitHostPort, e.g. "smtp.gmail.com:587"). The host without
 * brackets is kept as the TLS ServerName. Returns NULL on error; *errp is set. */
neverc_smtp_client_t *neverc_smtp_dial(const char *addr, const char **errp);

/* Close the SMTP connection. Sends QUIT if connected. */
void neverc_smtp_close(neverc_smtp_client_t *c);

/* --- Protocol --- */

/* Send EHLO (or HELO fallback). Called automatically by other methods
 * if not called explicitly. Returns 0 on success. */
int neverc_smtp_hello(neverc_smtp_client_t *c, const char *local_name);

/* RFC 3207 STARTTLS. cfg may be NULL (SNI is taken from the dial address).
 * A 220 followed by a handshake failure fails closed: the connection is
 * marked unusable and AUTH is not attempted on leftover plaintext.
 * Returns 0 on success. */
int neverc_smtp_starttls(neverc_smtp_client_t *c,
                         struct neverc_tls_config *cfg);

/* Authenticate with the server. The SASL mechanism must have been advertised
 * in EHLO; AUTH is not attempted after HELO-only, an unadvertised method, or
 * when EHLO advertised STARTTLS and the connection has not been upgraded.
 * Returns 0 on success. */
int neverc_smtp_auth(neverc_smtp_client_t *c,
                      neverc_smtp_auth_method_t method,
                      const char *username,
                      const char *password);

/* Start a mail transaction. Returns 0 on success. */
int neverc_smtp_mail(neverc_smtp_client_t *c, const char *from);

/* Add a recipient. Can be called multiple times. Returns 0 on success. */
int neverc_smtp_rcpt(neverc_smtp_client_t *c, const char *to);

/* Start the DATA phase. After this, write the message body with
 * neverc_smtp_write_data, then call neverc_smtp_data_close. */
int neverc_smtp_data(neverc_smtp_client_t *c);

/* Write message data (can be called multiple times). */
int neverc_smtp_write_data(neverc_smtp_client_t *c,
                             const void *data, size_t len);

/* End the DATA phase (sends ".\r\n"). Returns 0 on success. */
int neverc_smtp_data_close(neverc_smtp_client_t *c);

/* Send RSET (reset transaction). */
int neverc_smtp_reset(neverc_smtp_client_t *c);

/* Send NOOP. */
int neverc_smtp_noop(neverc_smtp_client_t *c);

/* Send QUIT. */
int neverc_smtp_quit(neverc_smtp_client_t *c);

/* Get the last server response message. */
const char *neverc_smtp_last_response(neverc_smtp_client_t *c);

/* --- Convenience --- */

/* Send an email in one call (like Go smtp.SendMail).
 * addr: "host:port", auth can be NONE for unauthenticated.
 * If EHLO advertises STARTTLS, the TLS upgrade is required and fail-closed
 * before AUTH or DATA. msg should be a complete RFC 822 message.
 * Returns 0 on success. */
int neverc_smtp_send_mail(const char *addr,
                            neverc_smtp_auth_method_t auth_method,
                            const char *username,
                            const char *password,
                            const char *from,
                            const char **to, int nto,
                            const void *msg, size_t msg_len,
                            const char **errp);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
#include <neverc/std/net.h>
#endif

#endif /* NEVERC_NET_SMTP_H */
