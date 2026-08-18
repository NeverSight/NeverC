#include "neverc/std/net/netip.h"
#include <string.h>

/*
 * Hand-written integer/hex formatting for the string paths. Replaces the
 * previous per-component snprintf calls (which dominated runtime: format-string
 * parsing + locale handling per call, and one call per IPv6 group/separator).
 */

static inline int fmt_u32_dec(char *p, uint32_t v) {
    char tmp[10];
    int n = 0;
    do { tmp[n++] = (char)('0' + (v % 10)); v /= 10; } while (v);
    for (int i = 0; i < n; i++) p[i] = tmp[n - 1 - i];
    return n;
}

static inline int fmt_u16_hex(char *p, uint16_t v) {
    static const char hexd[] = "0123456789abcdef";
    char tmp[4];
    int n = 0;
    do { tmp[n++] = hexd[v & 0xf]; v >>= 4; } while (v);
    for (int i = 0; i < n; i++) p[i] = tmp[n - 1 - i];
    return n;
}

/* IPv4-mapped IPv6 (::ffff:0:0/96), matching Go netip.Addr.Is4In6. */
static int addr_is_4in6(const neverc_netip_addr_t *addr) {
    if (addr->is_v4) return 0;
    for (int i = 0; i < 10; i++)
        if (addr->addr[i] != 0) return 0;
    return addr->addr[10] == 0xff && addr->addr[11] == 0xff;
}

static int append_zone(const neverc_netip_addr_t *addr, char *out, int pos) {
    if (!addr->zone[0]) return pos;
    out[pos++] = '%';
    size_t zl = strlen(addr->zone);
    memcpy(out + pos, addr->zone, zl);
    return pos + (int)zl;
}

/*
 * Format an address into out (caller guarantees >= 110 bytes of room).
 * Returns the number of bytes written (no NUL terminator). IPv4-mapped
 * IPv6 uses Go netip form "::ffff:a.b.c.d" rather than hex groups.
 */
static int format_addr_raw(const neverc_netip_addr_t *addr, char *out) {
    if (addr->is_v4) {
        int pos = 0;
        pos += fmt_u32_dec(out + pos, addr->addr[12]); out[pos++] = '.';
        pos += fmt_u32_dec(out + pos, addr->addr[13]); out[pos++] = '.';
        pos += fmt_u32_dec(out + pos, addr->addr[14]); out[pos++] = '.';
        pos += fmt_u32_dec(out + pos, addr->addr[15]);
        return pos;
    }

    if (addr_is_4in6(addr)) {
        int pos = 0;
        memcpy(out, "::ffff:", 7);
        pos = 7;
        pos += fmt_u32_dec(out + pos, addr->addr[12]); out[pos++] = '.';
        pos += fmt_u32_dec(out + pos, addr->addr[13]); out[pos++] = '.';
        pos += fmt_u32_dec(out + pos, addr->addr[14]); out[pos++] = '.';
        pos += fmt_u32_dec(out + pos, addr->addr[15]);
        return append_zone(addr, out, pos);
    }

    uint16_t groups[8];
    for (int i = 0; i < 8; i++)
        groups[i] = (uint16_t)((addr->addr[i*2] << 8) | addr->addr[i*2+1]);

    int best_start = -1, best_len = 0, cur_start = -1, cur_len = 0;
    for (int i = 0; i < 8; i++) {
        if (groups[i] == 0) {
            if (cur_start < 0) cur_start = i;
            cur_len++;
        } else {
            if (cur_len > best_len && cur_len >= 2) { best_start = cur_start; best_len = cur_len; }
            cur_start = -1; cur_len = 0;
        }
    }
    if (cur_len > best_len && cur_len >= 2) { best_start = cur_start; best_len = cur_len; }

    int pos = 0;
    for (int i = 0; i < 8; i++) {
        if (i == best_start) {
            out[pos++] = ':';
            out[pos++] = ':';
            i += best_len - 1;
            continue;
        }
        if (i > 0 && i != best_start + best_len) out[pos++] = ':';
        pos += fmt_u16_hex(out + pos, groups[i]);
    }
    return append_zone(addr, out, pos);
}

/*
 * Copy a freshly formatted string into the caller buffer with snprintf-style
 * truncation semantics: write at most cap-1 bytes plus a NUL, and return the
 * full length that would have been written.
 */
static int emit_str(char *buf, size_t cap, const char *src, int len) {
    if (cap > 0) {
        size_t n = (size_t)len < cap ? (size_t)len : cap - 1;
        memcpy(buf, src, n);
        buf[n] = '\0';
    }
    return len;
}

static int parse_decimal(const char *s, int len, unsigned *out) {
    if (len <= 0 || len > 5) return -1;
    unsigned v = 0;
    for (int i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '9') return -1;
        v = v * 10 + (unsigned)(s[i] - '0');
    }
    *out = v;
    return 0;
}

static int parse_hex16(const char *s, int len, uint16_t *out) {
    if (len <= 0 || len > 4) return -1;
    uint16_t v = 0;
    for (int i = 0; i < len; i++) {
        char c = s[i];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return -1;
        v = (uint16_t)(v * 16 + d);
    }
    *out = v;
    return 0;
}

static int parse_ipv4(const char *s, size_t len, uint8_t out[4]) {
    int octet = 0, start = 0;
    for (int i = 0; i <= (int)len && octet < 4; i++) {
        if (i == (int)len || s[i] == '.') {
            unsigned v;
            int octet_len = i - start;
            if (octet_len > 1 && s[start] == '0')
                return -1;
            if (parse_decimal(s + start, octet_len, &v) != 0 || v > 255)
                return -1;
            out[octet++] = (uint8_t)v;
            start = i + 1;
        }
    }
    return (octet == 4 && start == (int)len + 1) ? 0 : -1;
}

int neverc_netip_parse_addr(const char *s, neverc_netip_addr_t *out) {
    if (!s || !out) return -1;
    memset(out, 0, sizeof(*out));

    size_t slen = strlen(s);
    if (slen == 0 || slen > 255) return -1;

    /* Try IPv4 */
    if (slen <= 15) {
        uint8_t v4[4];
        if (parse_ipv4(s, slen, v4) == 0) {
            out->is_v4 = 1;
            out->valid = 1;
            out->addr[10] = 0xff;
            out->addr[11] = 0xff;
            out->addr[12] = v4[0];
            out->addr[13] = v4[1];
            out->addr[14] = v4[2];
            out->addr[15] = v4[3];
            return 0;
        }
    }

    /* IPv6 */
    const char *zone_start = NULL;
    size_t iplen = slen;
    for (size_t i = 0; i < slen; i++) {
        if (s[i] == '%') {
            zone_start = s + i + 1;
            iplen = i;
            break;
        }
    }
    /* Go netip.ParseAddr: IPv4 cannot carry a zone (the '.' branch
     * never splits on '%'). `1.2.3.4%eth0` must not parse as IPv6. */
    if (zone_start) {
        uint8_t v4z[4];
        if (parse_ipv4(s, iplen, v4z) == 0)
            return -1;
    }

    uint16_t groups[8] = {0};
    int ngroups = 0, dcolon_pos = -1;
    int pos = 0;

    if (iplen >= 2 && s[0] == ':' && s[1] == ':') {
        dcolon_pos = 0;
        pos = 2;
        if ((int)iplen == 2) goto done_parse;
    }

    while (pos < (int)iplen) {
        /* Check for embedded IPv4 */
        int has_dot = 0;
        for (int j = pos; j < (int)iplen; j++) {
            if (s[j] == '.') { has_dot = 1; break; }
            if (s[j] == ':') break;
        }
        if (has_dot) {
            int end = (int)iplen;
            uint8_t v4[4];
            if (parse_ipv4(s + pos, (size_t)(end - pos), v4) != 0) return -1;
            if (ngroups > 6) return -1;
            groups[ngroups++] = (uint16_t)((v4[0] << 8) | v4[1]);
            groups[ngroups++] = (uint16_t)((v4[2] << 8) | v4[3]);
            break;
        }

        int gstart = pos;
        while (pos < (int)iplen && s[pos] != ':') pos++;
        int glen = pos - gstart;
        if (glen <= 0 || glen > 4 || ngroups >= 8) return -1;
        if (parse_hex16(s + gstart, glen, &groups[ngroups]) != 0) return -1;
        ngroups++;

        if (pos >= (int)iplen) break;
        /* Skip the colon */
        pos++;
        /* Check for double colon */
        if (pos < (int)iplen && s[pos] == ':') {
            if (dcolon_pos >= 0) return -1;
            dcolon_pos = ngroups;
            pos++;
            if (pos >= (int)iplen) break;
        } else if (pos >= (int)iplen) {
            return -1;
        }
    }

done_parse:
    if (dcolon_pos >= 0) {
        int missing = 8 - ngroups;
        if (missing <= 0) return -1;
        uint16_t expanded[8] = {0};
        for (int i = 0; i < dcolon_pos; i++) expanded[i] = groups[i];
        for (int i = dcolon_pos; i < ngroups; i++) expanded[i + missing] = groups[i];
        memcpy(groups, expanded, sizeof(groups));
    } else if (ngroups != 8) {
        return -1;
    }

    if (zone_start) {
        size_t zlen = slen - (size_t)(zone_start - s);
        if (zlen == 0 || zlen >= sizeof(out->zone)) return -1;
        for (size_t zi = 0; zi < zlen; zi++) {
            unsigned char c = (unsigned char)zone_start[zi];
            /* Zone IDs are interface names or decimal indices. CTL in a
             * zone interpolates into URL/log lines as header injection. */
            if (c <= 0x20 || c == 0x7f || c == ']' || c == '/' || c == '@')
                return -1;
        }
        memcpy(out->zone, zone_start, zlen);
        out->zone[zlen] = '\0';
    }
    for (int i = 0; i < 8; i++) {
        out->addr[i*2]   = (uint8_t)(groups[i] >> 8);
        out->addr[i*2+1] = (uint8_t)(groups[i] & 0xff);
    }
    out->is_v4 = 0;
    out->valid = 1;
    return 0;
}

int neverc_netip_addr_from4(uint8_t a, uint8_t b, uint8_t c, uint8_t d, neverc_netip_addr_t *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    out->is_v4 = 1; out->valid = 1;
    out->addr[10] = 0xff; out->addr[11] = 0xff;
    out->addr[12] = a; out->addr[13] = b; out->addr[14] = c; out->addr[15] = d;
    return 0;
}

int neverc_netip_addr_from16(const uint8_t addr[16], neverc_netip_addr_t *out) {
    if (!out || !addr) return -1;
    memset(out, 0, sizeof(*out));
    memcpy(out->addr, addr, 16);
    out->valid = 1;
    return 0;
}

int neverc_netip_addr_is4in6(const neverc_netip_addr_t *addr) {
    return addr && addr->valid && addr_is_4in6(addr);
}

int neverc_netip_addr_unmap(const neverc_netip_addr_t *addr,
                            neverc_netip_addr_t *out) {
    if (!addr || !out || !addr->valid) return -1;
    *out = *addr;
    if (addr_is_4in6(addr)) {
        out->is_v4 = 1;
        out->zone[0] = '\0';
    }
    return 0;
}

int neverc_netip_addr_string(const neverc_netip_addr_t *addr, char *buf, size_t cap) {
    if (!addr || !addr->valid || (cap > 0 && !buf)) return -1;
    char tmp[120];
    int len = format_addr_raw(addr, tmp);
    return emit_str(buf, cap, tmp, len);
}

int neverc_netip_parse_addrport(const char *s, neverc_netip_addrport_t *out) {
    if (!s || !out) return -1;
    memset(out, 0, sizeof(*out));
    size_t slen = strlen(s);

    const char *colon = NULL;
    if (s[0] == '[') {
        const char *rb = strrchr(s, ']');
        if (!rb || rb[1] != ':') return -1;
        char addrbuf[256];
        size_t alen = (size_t)(rb - s - 1);
        if (alen == 0 || alen >= sizeof(addrbuf)) return -1;
        memcpy(addrbuf, s+1, alen);
        addrbuf[alen] = '\0';
        if (neverc_netip_parse_addr(addrbuf, &out->addr) != 0) return -1;
        /* Go netip.ParseAddrPort: brackets are valid only for IPv6. */
        if (out->addr.is_v4) return -1;
        colon = rb + 1;
    } else {
        /* IPv4: find last colon */
        colon = strrchr(s, ':');
        if (!colon) return -1;
        char addrbuf[256];
        size_t alen = (size_t)(colon - s);
        if (alen == 0 || alen >= sizeof(addrbuf)) return -1;
        memcpy(addrbuf, s, alen);
        addrbuf[alen] = '\0';
        if (neverc_netip_parse_addr(addrbuf, &out->addr) != 0) return -1;
        if (!out->addr.is_v4) return -1;
    }

    const char *port_str = colon + 1;
    size_t plen = slen - (size_t)(port_str - s);
    if (plen == 0 || plen > 10) return -1;
    unsigned port = 0;
    for (size_t i = 0; i < plen; i++) {
        if (port_str[i] < '0' || port_str[i] > '9') return -1;
        if (port > 65535u / 10u ||
            (port == 65535u / 10u && (unsigned)(port_str[i] - '0') > 65535u % 10u))
            return -1;
        port = port * 10u + (unsigned)(port_str[i] - '0');
    }
    if (port > 65535u) return -1;
    out->port = (uint16_t)port;
    return 0;
}

int neverc_netip_addrport_string(const neverc_netip_addrport_t *ap, char *buf, size_t cap) {
    if (!ap || !ap->addr.valid || (cap > 0 && !buf)) return -1;
    char tmp[144];
    int pos = 0;
    if (ap->addr.is_v4) {
        pos += format_addr_raw(&ap->addr, tmp + pos);
        tmp[pos++] = ':';
        pos += fmt_u32_dec(tmp + pos, ap->port);
    } else {
        tmp[pos++] = '[';
        pos += format_addr_raw(&ap->addr, tmp + pos);
        tmp[pos++] = ']';
        tmp[pos++] = ':';
        pos += fmt_u32_dec(tmp + pos, ap->port);
    }
    return emit_str(buf, cap, tmp, pos);
}

int neverc_netip_parse_prefix(const char *s, neverc_netip_prefix_t *out) {
    if (!s || !out) return -1;
    memset(out, 0, sizeof(*out));
    const char *slash = strchr(s, '/');
    if (!slash) return -1;
    char addrbuf[256];
    size_t alen = (size_t)(slash - s);
    if (alen >= sizeof(addrbuf)) return -1;
    memcpy(addrbuf, s, alen);
    addrbuf[alen] = '\0';
    neverc_netip_addr_t addr;
    if (neverc_netip_parse_addr(addrbuf, &addr) != 0) return -1;
    /* Go netip.ParsePrefix rejects IPv6 zones (go.dev/issue/51899). */
    if (!addr.is_v4 && addr.zone[0]) return -1;

    const char *bits_str = slash + 1;
    size_t blen = strlen(bits_str);
    /* strconv.Atoi allows leading zeros; Go ParsePrefix does not.
     * Cap length before the int cast so a huge digit string cannot
     * truncate to a small prefix (CIDR overflow). */
    if (blen == 0 || blen > 5) return -1;
    if (blen > 1 && (bits_str[0] < '1' || bits_str[0] > '9')) return -1;
    unsigned bits;
    if (parse_decimal(bits_str, (int)blen, &bits) != 0) return -1;
    int maxbits = addr.is_v4 ? 32 : 128;
    if ((int)bits > maxbits) return -1;
    out->addr = addr;
    out->bits = (uint8_t)bits;
    out->valid = 1;
    return 0;
}

int neverc_netip_prefix_string(const neverc_netip_prefix_t *pfx, char *buf, size_t cap) {
    if (!pfx || !pfx->valid || (cap > 0 && !buf)) return -1;
    char tmp[144];
    int pos = format_addr_raw(&pfx->addr, tmp);
    tmp[pos++] = '/';
    pos += fmt_u32_dec(tmp + pos, pfx->bits);
    return emit_str(buf, cap, tmp, pos);
}

int neverc_netip_addr_is_valid(const neverc_netip_addr_t *addr) { return addr && addr->valid; }
int neverc_netip_addr_is4(const neverc_netip_addr_t *addr)     { return addr && addr->valid && addr->is_v4; }
int neverc_netip_addr_is6(const neverc_netip_addr_t *addr)     { return addr && addr->valid && !addr->is_v4; }

int neverc_netip_addr_bit_len(const neverc_netip_addr_t *addr) {
    if (!addr || !addr->valid) return 0;
    return addr->is_v4 ? 32 : 128;
}

static int addr_v4_octets(const neverc_netip_addr_t *addr, uint8_t out[4]) {
    if (addr->is_v4) {
        memcpy(out, addr->addr + 12, 4);
        return 1;
    }
    for (int i = 0; i < 10; i++)
        if (addr->addr[i] != 0) return 0;
    if (addr->addr[10] != 0xff || addr->addr[11] != 0xff)
        return 0;
    memcpy(out, addr->addr + 12, 4);
    return 1;
}

int neverc_netip_addr_is_loopback(const neverc_netip_addr_t *addr) {
    if (!addr || !addr->valid) return 0;
    uint8_t v4[4];
    if (addr_v4_octets(addr, v4)) return v4[0] == 127;
    /* ::1 */
    for (int i = 0; i < 15; i++) if (addr->addr[i] != 0) return 0;
    return addr->addr[15] == 1;
}

int neverc_netip_addr_is_multicast(const neverc_netip_addr_t *addr) {
    if (!addr || !addr->valid) return 0;
    uint8_t v4[4];
    if (addr_v4_octets(addr, v4)) return (v4[0] & 0xf0) == 0xe0;
    return addr->addr[0] == 0xff;
}

int neverc_netip_addr_is_private(const neverc_netip_addr_t *addr) {
    if (!addr || !addr->valid) return 0;
    uint8_t v4[4];
    if (addr_v4_octets(addr, v4)) {
        uint8_t a = v4[0], b = v4[1];
        return a == 10 || (a == 172 && (b & 0xf0) == 16) || (a == 192 && b == 168);
    }
    return (addr->addr[0] & 0xfe) == 0xfc;
}

int neverc_netip_addr_is_link_local_unicast(const neverc_netip_addr_t *addr) {
    if (!addr || !addr->valid) return 0;
    uint8_t v4[4];
    if (addr_v4_octets(addr, v4)) return v4[0] == 169 && v4[1] == 254;
    return addr->addr[0] == 0xfe && (addr->addr[1] & 0xc0) == 0x80;
}

int neverc_netip_addr_is_link_local_multicast(const neverc_netip_addr_t *addr) {
    if (!addr || !addr->valid) return 0;
    uint8_t v4[4];
    if (addr_v4_octets(addr, v4))
        return v4[0] == 224 && v4[1] == 0 && v4[2] == 0;
    return addr->addr[0] == 0xff && (addr->addr[1] & 0x0f) == 0x02;
}

int neverc_netip_addr_is_global_unicast(const neverc_netip_addr_t *addr) {
    if (!addr || !addr->valid) return 0;
    uint8_t v4[4];
    if (addr_v4_octets(addr, v4)) {
        /* Go: IPv4 unspecified and limited broadcast are not global.
         * IPv4-mapped ::ffff:0.0.0.0 is unspecified after Unmap. */
        if ((v4[0] == 0 && v4[1] == 0 && v4[2] == 0 && v4[3] == 0) ||
            (v4[0] == 255 && v4[1] == 255 && v4[2] == 255 && v4[3] == 255))
            return 0;
    }
    return !neverc_netip_addr_is_loopback(addr) &&
           !neverc_netip_addr_is_multicast(addr) &&
           !neverc_netip_addr_is_link_local_unicast(addr) &&
           !neverc_netip_addr_is_private(addr) &&
           !neverc_netip_addr_is_unspecified(addr);
}

int neverc_netip_addr_is_unspecified(const neverc_netip_addr_t *addr) {
    if (!addr || !addr->valid) return 0;
    /* Go netip.Addr.IsUnspecified: only 0.0.0.0 and ::, not ::ffff:0.0.0.0. */
    if (addr->is_v4)
        return addr->addr[12] == 0 && addr->addr[13] == 0 &&
               addr->addr[14] == 0 && addr->addr[15] == 0;
    if (addr_is_4in6(addr))
        return 0;
    for (int i = 0; i < 16; i++) if (addr->addr[i] != 0) return 0;
    return 1;
}

int neverc_netip_addr_is_internal(const neverc_netip_addr_t *addr) {
    neverc_netip_addr_t unmapped;
    if (!addr || !addr->valid) return 1;
    if (neverc_netip_addr_unmap(addr, &unmapped) != 0) return 1;
    if (neverc_netip_addr_is_loopback(&unmapped) ||
        neverc_netip_addr_is_private(&unmapped) ||
        neverc_netip_addr_is_link_local_unicast(&unmapped) ||
        neverc_netip_addr_is_multicast(&unmapped) ||
        neverc_netip_addr_is_unspecified(&unmapped))
        return 1;
    uint8_t v4[4];
    if (neverc_netip_addr_as4(&unmapped, v4) == 4 &&
        v4[0] == 255 && v4[1] == 255 && v4[2] == 255 && v4[3] == 255)
        return 1;
    return 0;
}

int neverc_netip_addr_compare(const neverc_netip_addr_t *a, const neverc_netip_addr_t *b) {
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    if (a->is_v4 != b->is_v4) return a->is_v4 ? -1 : 1;
    int start = a->is_v4 ? 12 : 0;
    int cmp = memcmp(a->addr + start, b->addr + start, a->is_v4 ? 4 : 16);
    if (cmp != 0) return cmp;
    return strcmp(a->zone, b->zone);
}

int neverc_netip_addr_equal(const neverc_netip_addr_t *a, const neverc_netip_addr_t *b) {
    return neverc_netip_addr_compare(a, b) == 0;
}

int neverc_netip_prefix_contains(const neverc_netip_prefix_t *pfx, const neverc_netip_addr_t *addr) {
    if (!pfx || !addr || !pfx->valid || !addr->valid) return 0;
    /* Go netip.Prefix.Contains: zones never match, and IPv4-mapped IPv6
     * does not match an IPv4 prefix (families must be identical). */
    if (addr->zone[0]) return 0;
    if (pfx->addr.is_v4 != addr->is_v4) return 0;

    int start = pfx->addr.is_v4 ? 12 : 0;
    int bytes = pfx->addr.is_v4 ? 4 : 16;
    int bits = pfx->bits;

    for (int i = 0; i < bytes; i++) {
        if (bits >= 8) {
            if (pfx->addr.addr[start+i] != addr->addr[start+i]) return 0;
            bits -= 8;
        } else if (bits > 0) {
            uint8_t mask = (uint8_t)(0xff << (8 - bits));
            if ((pfx->addr.addr[start+i] & mask) != (addr->addr[start+i] & mask)) return 0;
            bits = 0;
        }
    }
    return 1;
}

int neverc_netip_prefix_masked(const neverc_netip_prefix_t *pfx, neverc_netip_addr_t *out) {
    if (!pfx || !out || !pfx->valid) return -1;
    *out = pfx->addr;
    int start = out->is_v4 ? 12 : 0;
    int bytes = out->is_v4 ? 4 : 16;
    int bits = pfx->bits;
    for (int i = 0; i < bytes; i++) {
        if (bits >= 8) { bits -= 8; continue; }
        uint8_t mask = bits > 0 ? (uint8_t)(0xff << (8 - bits)) : 0;
        out->addr[start+i] &= mask;
        bits = 0;
    }
    return 0;
}

int neverc_netip_prefix_bits(const neverc_netip_prefix_t *pfx) {
    return (pfx && pfx->valid) ? pfx->bits : -1;
}

void neverc_netip_addr_ipv4_unspecified(neverc_netip_addr_t *out) {
    neverc_netip_addr_from4(0, 0, 0, 0, out);
}

void neverc_netip_addr_ipv6_unspecified(neverc_netip_addr_t *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->valid = 1;
}

void neverc_netip_addr_ipv6_loopback(neverc_netip_addr_t *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->addr[15] = 1;
    out->valid = 1;
}

int neverc_netip_addr_as4(const neverc_netip_addr_t *addr, uint8_t out[4]) {
    if (!addr || !addr->valid || !addr->is_v4 || !out) return -1;
    memcpy(out, addr->addr + 12, 4);
    return 4;
}

int neverc_netip_addr_as16(const neverc_netip_addr_t *addr, uint8_t out[16]) {
    if (!addr || !addr->valid || !out) return -1;
    memcpy(out, addr->addr, 16);
    return 16;
}
