#include "neverc/std/net/mail.h"
#include "neverc/std/net/netip.h"
#include <string.h>
#include <stdio.h>

static void trim(const char *s, size_t len, const char **out_start, size_t *out_len) {
    while (len > 0 && (*s == ' ' || *s == '\t')) { s++; len--; }
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t')) len--;
    *out_start = s;
    *out_len = len;
}

/* Offset of the first `lead` or `bound` byte in base[0,n), else n. `bound`
 * (the line terminator '\n') is located first so the `lead` search is capped at
 * that point — neither memchr over-scans past the earlier delimiter. This is an
 * exact, vectorized replacement for a `data[i] != lead && data[i] != bound`
 * byte-at-a-time loop. */
static size_t scan_first2(const char *base, size_t n, char lead, char bound) {
    const char *pb = (const char *)memchr(base, bound, n);
    size_t lim = pb ? (size_t)(pb - base) : n;
    const char *pl = (const char *)memchr(base, lead, lim);
    if (pl) return (size_t)(pl - base);
    return lim;
}

static int mail_field_has_ctl(const char *s, size_t len) {
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c == 0x7f) return 1;
    }
    return 0;
}

/* RFC 5322 field-name is 1*ftext (printable US-ASCII except ':'). */
static int mail_field_name_ok(const char *s, size_t len) {
    if (len == 0) return 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 33 || c > 126 || c == ':') return 0;
    }
    return 1;
}

static int mail_address_safe(const char *s) {
    if (!s || !s[0]) return 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p < 0x21 || *p == 0x7f || *p == '<' || *p == '>' ||
            *p == '"' || *p == '\\' || *p == ',' || *p == ';')
            return 0;
    }
    return 1;
}

/* RFC 5322 dot-atom: no empty, leading, trailing, or consecutive dots. */
static int mail_dot_atom_ok(const char *s, size_t n) {
    if (n == 0 || s[0] == '.' || s[n - 1] == '.') return 0;
    for (size_t i = 0; i + 1 < n; i++) {
        if (s[i] == '.' && s[i + 1] == '.') return 0;
    }
    return 1;
}

/* addr-spec is local-part "@" domain. Quoted local-parts are rejected
 * (mail_address_safe already forbids '"'). Domain-literals [ ... ] are ok. */
static int mail_addr_spec_ok(const char *s) {
    if (!mail_address_safe(s)) return 0;
    const char *at = strchr(s, '@');
    if (!at || at == s || !at[1] || strchr(at + 1, '@')) return 0;
    if (!mail_dot_atom_ok(s, (size_t)(at - s))) return 0;
    const char *dom = at + 1;
    if (dom[0] == '[') {
        size_t n = strlen(dom);
        if (n < 3 || dom[n - 1] != ']') return 0;
        char inner[128];
        size_t ilen = n - 2;
        if (ilen == 0 || ilen >= sizeof(inner)) return 0;
        memcpy(inner, dom + 1, ilen);
        inner[ilen] = '\0';
        const char *ip = inner;
        if (ilen >= 5) {
            char prefix[5];
            size_t i;
            for (i = 0; i < 5; i++) {
                char c = inner[i];
                if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
                prefix[i] = c;
            }
            if (memcmp(prefix, "ipv6:", 5) == 0)
                ip = inner + 5;
        }
        neverc_netip_addr_t addr;
        return neverc_netip_parse_addr(ip, &addr) == 0;
    }
    return mail_dot_atom_ok(dom, strlen(dom));
}

static int mail_put(char *buf, size_t cap, const char *fmt, const char *a,
                    const char *b) {
    int n = b ? snprintf(buf, cap, fmt, a, b) : snprintf(buf, cap, fmt, a);
    return (n > 0 && (size_t)n < cap) ? n : -1;
}

int neverc_mail_parse_address(const char *s, neverc_mail_address_t *out) {
    if (!s || !out) return -1;
    memset(out, 0, sizeof(*out));

    const char *start = NULL;
    size_t slen = 0;
    trim(s, strlen(s), &start, &slen);
    if (slen == 0 || mail_field_has_ctl(start, slen)) return -1;

    const char *lt = NULL, *gt = NULL;
    for (size_t i = 0; i < slen; i++) {
        if (start[i] == '<') {
            if (lt) return -1;
            lt = start + i;
        }
        if (start[i] == '>') {
            if (gt) return -1;
            gt = start + i;
        }
    }

    if (lt || gt) {
        if (!lt || !gt || gt < lt + 2) return -1;
        const char *after = gt + 1;
        while (after < start + slen && (*after == ' ' || *after == '\t'))
            after++;
        if (after != start + slen) return -1;

        size_t addr_len = (size_t)(gt - lt - 1);
        if (addr_len == 0 || addr_len >= sizeof(out->address)) return -1;
        memcpy(out->address, lt + 1, addr_len);
        out->address[addr_len] = '\0';
        if (!mail_addr_spec_ok(out->address)) return -1;

        size_t name_len = (size_t)(lt - start);
        const char *nstart;
        size_t nlen;
        trim(start, name_len, &nstart, &nlen);
        if (nlen >= 2 && nstart[0] == '"' && nstart[nlen-1] == '"') {
            nstart++; nlen -= 2;
        }
        if (mail_field_has_ctl(nstart, nlen)) return -1;
        if (nlen >= sizeof(out->name)) return -1;
        memcpy(out->name, nstart, nlen);
        out->name[nlen] = '\0';
    } else {
        if (slen >= sizeof(out->address)) return -1;
        memcpy(out->address, start, slen);
        out->address[slen] = '\0';
        if (!mail_addr_spec_ok(out->address)) return -1;
    }
    return 0;
}

int neverc_mail_parse_address_list(const char *s,
                                   neverc_mail_address_t *out, int max_out) {
    if (!s || !out || max_out <= 0) return -1;
    int count = 0;
    const char *p = s;

    while (*p && count < max_out) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        /* Find the end of this address (comma or end) */
        int depth = 0;
        int quoted = 0;
        int escaped = 0;
        const char *start = p;
        while (*p) {
            if (escaped) {
                escaped = 0;
            } else if (quoted && *p == '\\') {
                escaped = 1;
            } else if (*p == '"') {
                quoted = !quoted;
            } else if (!quoted && *p == '<') {
                depth++;
            } else if (!quoted && *p == '>' && depth > 0) {
                depth--;
            } else if (!quoted && *p == ',' && depth == 0) {
                break;
            }
            p++;
        }

        char buf[512] = {0};
        size_t len = (size_t)(p - start);
        if (len == 0 || len >= sizeof(buf)) return -1;
        memcpy(buf, start, len);
        buf[len] = '\0';

        if (neverc_mail_parse_address(buf, &out[count]) != 0)
            return -1;
        count++;

        if (*p == ',') {
            p++;
            while (*p == ' ' || *p == '\t') p++;
            /* Trailing comma is an empty mailbox (Go ParseAddressList). */
            if (!*p) return -1;
        }
    }
    while (*p == ' ' || *p == '\t') p++;
    /* A max_out cap must not silently drop remaining recipients. */
    if (*p) return -1;
    if (count == 0) return -1;
    return count;
}

int neverc_mail_format_address(const neverc_mail_address_t *addr, char *buf, size_t cap) {
    if (!addr || !buf || cap == 0 || !mail_addr_spec_ok(addr->address))
        return -1;
    if (mail_field_has_ctl(addr->name, strlen(addr->name))) return -1;
    if (!addr->name[0])
        return mail_put(buf, cap, "%s", addr->address, NULL);

    int need_quote = 0;
    for (const unsigned char *p = (const unsigned char *)addr->name; *p; p++) {
        if (*p < 33 || *p == 127 || *p == '"' || *p == '\\' ||
            *p == ',' || *p == '<' || *p == '>' || *p == '@' || *p == ';')
            need_quote = 1;
    }
    if (!need_quote)
        return mail_put(buf, cap, "%s <%s>", addr->name, addr->address);

    char quoted[sizeof(addr->name) * 2 + 3];
    size_t qi = 0;
    quoted[qi++] = '"';
    for (const char *p = addr->name; *p && qi + 2 < sizeof(quoted); p++) {
        if (*p == '"' || *p == '\\') quoted[qi++] = '\\';
        quoted[qi++] = *p;
    }
    quoted[qi++] = '"';
    quoted[qi] = '\0';
    return mail_put(buf, cap, "%s <%s>", quoted, addr->address);
}

int neverc_mail_parse_message(const char *data, size_t len, neverc_mail_message_t *out) {
    if (!data || !out) return -1;
    memset(out, 0, sizeof(*out));

    size_t i = 0;
    while (i < len && out->header_count < NEVERC_MAIL_MAX_HEADERS) {
        /* Empty line = end of headers */
        if (data[i] == '\r' && i+1 < len && data[i+1] == '\n') {
            out->body = data + i + 2;
            out->body_len = len - i - 2;
            return 0;
        }
        if (data[i] == '\n') {
            out->body = data + i + 1;
            out->body_len = len - i - 1;
            return 0;
        }

        /* Find colon (or the line's '\n', whichever comes first) */
        size_t colon = i + scan_first2(data + i, len - i, ':', '\n');
        if (colon >= len || data[colon] != ':') return -1;

        neverc_mail_header_t *h = &out->headers[out->header_count];
        size_t klen = colon - i;
        if (klen == 0 || klen >= sizeof(h->key) ||
            !mail_field_name_ok(data + i, klen))
            return -1;
        memcpy(h->key, data + i, klen);
        h->key[klen] = '\0';

        size_t vstart = colon + 1;
        while (vstart < len && (data[vstart] == ' ' || data[vstart] == '\t')) vstart++;

        /* Value may span multiple lines (continuation lines start with space/tab) */
        size_t vpos = 0;
        size_t pos = vstart;
        while (pos < len) {
            size_t line_end = pos + scan_first2(data + pos, len - pos, '\r', '\n');

            size_t line_len = line_end - pos;
            size_t need = line_len + ((vpos > 0 && pos != vstart) ? 1U : 0U);
            if (need >= sizeof(h->value) - vpos) return -1;
            if (vpos > 0 && pos != vstart) h->value[vpos++] = ' ';
            memcpy(h->value + vpos, data + pos, line_len);
            vpos += line_len;

            /* RFC 5322 line break is CRLF (LF alone is accepted). A bare CR
             * must not terminate the line: that splits "From: a\rBcc: b" into
             * two headers and smuggles a field. */
            if (line_end < len && data[line_end] == '\r') {
                if (line_end + 1 >= len || data[line_end + 1] != '\n')
                    return -1;
                line_end += 2;
            } else if (line_end < len && data[line_end] == '\n') {
                line_end++;
            }

            /* Check for continuation */
            if (line_end < len && (data[line_end] == ' ' || data[line_end] == '\t')) {
                while (line_end < len && (data[line_end] == ' ' || data[line_end] == '\t')) line_end++;
                pos = line_end;
            } else {
                pos = line_end;
                break;
            }
        }
        if (mail_field_has_ctl(h->value, vpos)) return -1;
        h->value[vpos] = '\0';
        out->header_count++;
        i = pos;
    }

    if (i < len) {
        if (data[i] == '\r' && i + 1 < len && data[i + 1] == '\n') {
            out->body = data + i + 2;
            out->body_len = len - i - 2;
            return 0;
        }
        if (data[i] == '\n') {
            out->body = data + i + 1;
            out->body_len = len - i - 1;
            return 0;
        }
        return -1;
    }

    out->body = data + i;
    out->body_len = 0;
    return 0;
}

const char *neverc_mail_header_get(const neverc_mail_message_t *msg, const char *key) {
    if (!msg || !key) return NULL;
    size_t klen = strlen(key);
    for (int i = 0; i < msg->header_count; i++) {
        if (strlen(msg->headers[i].key) == klen) {
            int match = 1;
            for (size_t j = 0; j < klen; j++) {
                char a = msg->headers[i].key[j], b = key[j];
                if (a >= 'A' && a <= 'Z') a += 32;
                if (b >= 'A' && b <= 'Z') b += 32;
                if (a != b) { match = 0; break; }
            }
            if (match) return msg->headers[i].value;
        }
    }
    return NULL;
}

static int mail_is_alpha(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

/* Nested RFC 5322 comments so a comma inside "(We, st)" is not a day-of-week. */
static int mail_skip_cfws(const char **pp) {
    const char *p = *pp;
    for (;;) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '(') break;
        int depth = 1;
        p++;
        while (*p && depth) {
            if (*p == '\\' && p[1]) {
                p += 2;
                continue;
            }
            if (*p == '(') depth++;
            else if (*p == ')') depth--;
            p++;
        }
        if (depth) return -1;
    }
    *pp = p;
    return 0;
}

static int mail_parse_uint(const char **pp, int min_digits, int max_digits,
                           int *out) {
    const char *p = *pp;
    if (*p < '0' || *p > '9') return -1;
    int v = 0, d = 0;
    while (*p >= '0' && *p <= '9') {
        if (d >= max_digits) return -1;
        v = v * 10 + (*p - '0');
        p++;
        d++;
    }
    if (d < min_digits) return -1;
    *out = v;
    *pp = p;
    return 0;
}

static int mail_month_index(const char *p) {
    static const char *months[] = {
        "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"
    };
    for (int i = 0; i < 12; i++) {
        if (p[0] == months[i][0] && p[1] == months[i][1] &&
            p[2] == months[i][2])
            return i;
    }
    return -1;
}

static int mail_dow_ok(const char *p) {
    static const char *dows[] = {
        "Mon","Tue","Wed","Thu","Fri","Sat","Sun"
    };
    for (int i = 0; i < 7; i++) {
        if (p[0] == dows[i][0] && p[1] == dows[i][1] && p[2] == dows[i][2])
            return 1;
    }
    return 0;
}

/* RFC 5322 obs-zone plus UT/UTC/GMT. Military 1-letter zones are -0000. */
static int mail_named_zone(const char *p, size_t n, int *offset) {
    static const struct { const char *name; int off; } zones[] = {
        {"UT", 0}, {"UTC", 0}, {"GMT", 0},
        {"EST", -5 * 3600}, {"EDT", -4 * 3600},
        {"CST", -6 * 3600}, {"CDT", -5 * 3600},
        {"MST", -7 * 3600}, {"MDT", -6 * 3600},
        {"PST", -8 * 3600}, {"PDT", -7 * 3600},
    };
    for (size_t i = 0; i < sizeof(zones) / sizeof(zones[0]); i++) {
        if (strlen(zones[i].name) == n && memcmp(p, zones[i].name, n) == 0) {
            *offset = zones[i].off;
            return 0;
        }
    }
    if (n == 1 && mail_is_alpha((unsigned char)p[0])) {
        *offset = 0;
        return 0;
    }
    return -1;
}

long long neverc_mail_parse_date(const char *s) {
    if (!s || !s[0]) return -1;

    const char *p = s;
    if (mail_skip_cfws(&p) != 0) return -1;

    /* Optional day-of-week "," — only if the token is 3 letters and a comma
     * follows. A leading month is left for the date production. */
    if (mail_is_alpha((unsigned char)p[0]) &&
        mail_is_alpha((unsigned char)p[1]) &&
        mail_is_alpha((unsigned char)p[2]) &&
        !mail_is_alpha((unsigned char)p[3])) {
        const char *q = p + 3;
        if (mail_skip_cfws(&q) != 0) return -1;
        if (*q == ',') {
            if (!mail_dow_ok(p)) return -1;
            p = q + 1;
            if (mail_skip_cfws(&p) != 0) return -1;
        }
    }

    int day = 0;
    if (mail_parse_uint(&p, 1, 2, &day) != 0) return -1;
    if (mail_skip_cfws(&p) != 0) return -1;

    /* Month is exactly 3 ALPHA (reject "January"). */
    if (!mail_is_alpha((unsigned char)p[0]) ||
        !mail_is_alpha((unsigned char)p[1]) ||
        !mail_is_alpha((unsigned char)p[2]) ||
        mail_is_alpha((unsigned char)p[3]))
        return -1;
    int month = mail_month_index(p);
    if (month < 0) return -1;
    p += 3;
    if (mail_skip_cfws(&p) != 0) return -1;

    int year = 0;
    const char *y0 = p;
    if (mail_parse_uint(&p, 2, 4, &year) != 0) return -1;
    int ydigits = (int)(p - y0);
    if (ydigits == 3) return -1;
    if (ydigits == 2) year += (year < 50) ? 2000 : 1900;
    if (year < 1 || year > 9999) return -1;
    if (mail_skip_cfws(&p) != 0) return -1;

    int hour = 0, min = 0, sec = 0;
    if (mail_parse_uint(&p, 1, 2, &hour) != 0 || *p != ':') return -1;
    p++;
    if (mail_parse_uint(&p, 1, 2, &min) != 0) return -1;
    if (*p == ':') {
        p++;
        if (mail_parse_uint(&p, 1, 2, &sec) != 0) return -1;
    }
    if (mail_skip_cfws(&p) != 0) return -1;

    /* zone is required (RFC 5322 time = time-of-day zone). */
    int zone_offset = 0;
    if (*p == '+' || *p == '-') {
        char sign = *p++;
        int zh = 0, zm = 0;
        if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9' ||
            p[2] < '0' || p[2] > '9' || p[3] < '0' || p[3] > '9')
            return -1;
        zh = (p[0] - '0') * 10 + (p[1] - '0');
        zm = (p[2] - '0') * 10 + (p[3] - '0');
        p += 4;
        if (zh > 23 || zm > 59) return -1;
        zone_offset = zh * 3600 + zm * 60;
        if (sign == '-') zone_offset = -zone_offset;
    } else {
        const char *z0 = p;
        while (mail_is_alpha((unsigned char)*p)) p++;
        if (mail_named_zone(z0, (size_t)(p - z0), &zone_offset) != 0)
            return -1;
    }

    if (mail_skip_cfws(&p) != 0 || *p) return -1;

    int leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    static const int month_days[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    int max_day = month_days[month] + (month == 1 && leap);
    if (day < 1 || day > max_day || hour < 0 || hour > 23 ||
        min < 0 || min > 59 || sec < 0 || sec > 60)
        return -1;

    /* Days since the Unix epoch via Howard Hinnant's O(1) days_from_civil (the
     * same closed form time.c uses). Replaces a naive O(year-1970) leap-day loop
     * that grew without bound for far-future dates. `month` is 0-based here, so
     * convert to the 1..12 the formula expects. */
    int m1 = month + 1;
    long long y = (long long)year - (m1 <= 2);
    long long era = (y >= 0 ? y : y - 399) / 400;
    long long yoe = y - era * 400;                                  /* [0, 399]    */
    long long doy = (153 * (m1 + (m1 > 2 ? -3 : 9)) + 2) / 5 + (day - 1); /* [0,365] */
    long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;          /* [0, 146096] */
    long long days = era * 146097 + doe - 719468;

    return days * 86400 + (long long)hour * 3600 +
           (long long)min * 60 + sec - zone_offset;
}
