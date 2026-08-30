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
#include <neverc/std/net/_module.h>
#endif

#endif /* NEVERC_NET_H */
