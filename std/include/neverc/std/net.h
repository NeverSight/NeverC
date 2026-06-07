#ifndef NEVERC_NET_H
#define NEVERC_NET_H

/*
 * NeverC net — umbrella header for network submodules.
 */

#include "net/url.h"
#include "net/netip.h"
#include "net/mail.h"
#include "net/textproto.h"

#ifdef __neverc__
struct __neverc_std_url_t { char __tag; };
struct __neverc_std_netip_t { char __tag; };
struct __neverc_std_mail_t { char __tag; };
struct __neverc_std_textproto_t { char __tag; };

struct __neverc_std_net_t {
    struct __neverc_std_url_t url;
    struct __neverc_std_netip_t netip;
    struct __neverc_std_mail_t mail;
    struct __neverc_std_textproto_t textproto;
};
extern struct __neverc_std_net_t __neverc_mod_net;
extern struct __neverc_std_net_t net;
#endif

#endif /* NEVERC_NET_H */
