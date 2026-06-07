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

#ifdef __neverc__
struct __neverc_std_tcp_t { char __tag; };
struct __neverc_std_udp_t { char __tag; };
struct __neverc_std_http_t { char __tag; };
struct __neverc_std_websocket_t { char __tag; };
struct __neverc_std_url_t { char __tag; };
struct __neverc_std_netip_t { char __tag; };
struct __neverc_std_mail_t { char __tag; };
struct __neverc_std_textproto_t { char __tag; };

struct __neverc_std_net_t {
    struct __neverc_std_tcp_t tcp;
    struct __neverc_std_udp_t udp;
    struct __neverc_std_http_t http;
    struct __neverc_std_websocket_t websocket;
    struct __neverc_std_url_t url;
    struct __neverc_std_netip_t netip;
    struct __neverc_std_mail_t mail;
    struct __neverc_std_textproto_t textproto;
};
extern struct __neverc_std_net_t __neverc_mod_net;
extern struct __neverc_std_net_t net;
#endif

#endif /* NEVERC_NET_H */
