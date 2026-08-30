#ifndef NEVERC_HEADER_PROBE
#error "NEVERC_HEADER_PROBE must select one isolated header contract"
#endif

#if NEVERC_HEADER_PROBE == 1
#include <neverc/std/_modules.h>
#include <neverc/std/time.h>
neverc_time_t neverc_header_contract_probe(void) {
    return time_mod.add(std.time.now(), 0);
}
#elif NEVERC_HEADER_PROBE == 2
#include <neverc/std/debug/elf.h>
int neverc_header_contract_probe(const uint8_t *data, size_t len) {
    return debug.elf.is_valid(data, len);
}
#elif NEVERC_HEADER_PROBE == 3
#include <neverc/std/debug/pe.h>
int neverc_header_contract_probe(const uint8_t *data, size_t len) {
    return debug.pe.is_valid(data, len);
}
#elif NEVERC_HEADER_PROBE == 4
#include <neverc/std/debug/macho.h>
int neverc_header_contract_probe(const uint8_t *data, size_t len) {
    return debug.macho.is_valid(data, len);
}
#elif NEVERC_HEADER_PROBE == 5
#include <neverc/std/debug/dwarf.h>
const char *neverc_header_contract_probe(uint16_t tag) {
    return debug.dwarf.tag_string(tag);
}
#elif NEVERC_HEADER_PROBE == 6
#include <neverc/std/net/tcp.h>
uintptr_t neverc_header_contract_probe(neverc_tcp_conn_t *conn) {
    return net.tcp.conn_handle(conn);
}
#elif NEVERC_HEADER_PROBE == 7
#include <neverc/std/net/udp.h>
uintptr_t neverc_header_contract_probe(neverc_udp_conn_t *conn) {
    return net.udp.conn_handle(conn);
}
#elif NEVERC_HEADER_PROBE == 8
#include <neverc/std/net/http.h>
const char *neverc_header_contract_probe(int status) {
    return net.http.status_text(status);
}
#elif NEVERC_HEADER_PROBE == 9
#include <neverc/std/net/http3.h>
neverc_http3_client_config_t neverc_header_contract_probe(void) {
    return net.http3.client_config_default();
}
#elif NEVERC_HEADER_PROBE == 10
#include <neverc/std/net/quic.h>
neverc_quic_config_t neverc_header_contract_probe(void) {
    return net.quic.config_default();
}
#else
#error "unknown NEVERC_HEADER_PROBE"
#endif
