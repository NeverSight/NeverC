#ifndef NEVERC_NET_H
#define NEVERC_NET_H

/*
 * NeverC net — umbrella header for network submodules.
 *
 * Dot-syntax: net.tcp.listen(":8080"), net.http.listen_and_serve(":8080", NULL)
 */

#include "net/tcp.h"
#include "net/udp.h"
#include "net/http.h"
#include "net/websocket.h"
#include "net/url.h"
#include "net/netip.h"
#include "net/mail.h"
#include "net/textproto.h"
#include "net/resolve.h"
#include "net/interface.h"
#include "net/http/http2.h"
#include "net/grpc.h"
#include "net/http3.h"
#include "net/quic.h"
#include "net/rpc.h"
#include "net/smtp.h"
#include "net/http/httptest.h"
#include "net/http/httputil.h"
#include "net/http/cookiejar.h"

#ifdef __neverc__
/* Windows SDK (combaseapi.h) defines `#define interface struct` for COM.
   Undefine it so we can use `interface` as a struct field name. */
#ifdef interface
#undef interface
#endif
struct __neverc_std_tcp_t { char __tag; };
struct __neverc_std_udp_t { char __tag; };
struct __neverc_std_http_t { char __tag; };
/* The field is named websocket, while its marker follows the neverc_ws_ ABI
 * prefix used by the public functions. */
struct __neverc_std_ws_t { char __tag; };
struct __neverc_std_url_t { char __tag; };
struct __neverc_std_netip_t { char __tag; };
struct __neverc_std_mail_t { char __tag; };
struct __neverc_std_textproto_t { char __tag; };
struct __neverc_std_resolve_t { char __tag; };
struct __neverc_std_interface_t { char __tag; };
/* Marker name must match neverc_h2_* so net.http2.server_create()
 * resolves to neverc_h2_server_create, not neverc_http2_server_create. */
struct __neverc_std_h2_t { char __tag; };
struct __neverc_std_grpc_t { char __tag; };
struct __neverc_std_http3_t { char __tag; };
struct __neverc_std_quic_t { char __tag; };
struct __neverc_std_rpc_t { char __tag; };
struct __neverc_std_smtp_t { char __tag; };
struct __neverc_std_httptest_t { char __tag; };
struct __neverc_std_httputil_t { char __tag; };
struct __neverc_std_cookiejar_t { char __tag; };

struct __neverc_std_net_t {
    struct __neverc_std_tcp_t tcp;
    struct __neverc_std_udp_t udp;
    struct __neverc_std_http_t http;
    struct __neverc_std_h2_t http2;
    struct __neverc_std_grpc_t grpc;
    struct __neverc_std_http3_t http3;
    struct __neverc_std_quic_t quic;
    struct __neverc_std_rpc_t rpc;
    struct __neverc_std_ws_t websocket;
    struct __neverc_std_url_t url;
    struct __neverc_std_netip_t netip;
    struct __neverc_std_mail_t mail;
    struct __neverc_std_textproto_t textproto;
    struct __neverc_std_resolve_t resolve;
    struct __neverc_std_interface_t interface;
    struct __neverc_std_smtp_t smtp;
    struct __neverc_std_httptest_t httptest;
    struct __neverc_std_httputil_t httputil;
    struct __neverc_std_cookiejar_t cookiejar;
};
extern struct __neverc_std_net_t __neverc_mod_net;
extern struct __neverc_std_net_t net;
#endif

#endif /* NEVERC_NET_H */
