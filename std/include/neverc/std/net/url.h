#ifndef NEVERC_NET_URL_H
#define NEVERC_NET_URL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char scheme[64];
    char user[128];
    char password[128];
    char host[256];
    char port[16];
    char path[1024];
    char raw_query[2048];
    char fragment[256];
} neverc_url_t;

int  neverc_url_parse(neverc_url_t *u, const char *raw_url);
int  neverc_url_string(const neverc_url_t *u, char *buf, size_t cap);
int  neverc_url_hostname(const neverc_url_t *u, char *buf, size_t cap);
int  neverc_url_request_uri(const neverc_url_t *u, char *buf, size_t cap);
int  neverc_url_is_abs(const neverc_url_t *u);

typedef struct {
    char keys[64][256];
    char vals[64][1024];
    int  count;
} neverc_url_values_t;

int  neverc_url_values_parse(neverc_url_values_t *v, const char *query);
const char *neverc_url_values_get(const neverc_url_values_t *v, const char *key);
void neverc_url_values_set(neverc_url_values_t *v, const char *key, const char *val);
int  neverc_url_values_encode(const neverc_url_values_t *v, char *buf, size_t cap);

int  neverc_url_path_escape(const char *s, char *buf, size_t cap);
int  neverc_url_path_unescape(const char *s, char *buf, size_t cap);
int  neverc_url_query_escape(const char *s, char *buf, size_t cap);
int  neverc_url_query_unescape(const char *s, char *buf, size_t cap);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/net.h>
#endif


#endif
