#include "neverc/std/net/mail.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

static void trim(const char *s, size_t len, const char **out_start, size_t *out_len) {
    while (len > 0 && (*s == ' ' || *s == '\t')) { s++; len--; }
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t')) len--;
    *out_start = s;
    *out_len = len;
}

int neverc_mail_parse_address(const char *s, neverc_mail_address_t *out) {
    if (!s || !out) return -1;
    memset(out, 0, sizeof(*out));

    const char *start;
    size_t slen;
    trim(s, strlen(s), &start, &slen);
    if (slen == 0) return -1;

    const char *lt = NULL, *gt = NULL;
    for (size_t i = 0; i < slen; i++) {
        if (start[i] == '<') lt = start + i;
        if (start[i] == '>') gt = start + i;
    }

    if (lt && gt && gt > lt) {
        /* "Name <address>" format */
        size_t addr_len = (size_t)(gt - lt - 1);
        if (addr_len >= sizeof(out->address)) addr_len = sizeof(out->address) - 1;
        memcpy(out->address, lt + 1, addr_len);
        out->address[addr_len] = '\0';

        /* Name part */
        size_t name_len = (size_t)(lt - start);
        const char *nstart;
        size_t nlen;
        trim(start, name_len, &nstart, &nlen);
        /* Strip surrounding quotes */
        if (nlen >= 2 && nstart[0] == '"' && nstart[nlen-1] == '"') {
            nstart++; nlen -= 2;
        }
        if (nlen >= sizeof(out->name)) nlen = sizeof(out->name) - 1;
        memcpy(out->name, nstart, nlen);
        out->name[nlen] = '\0';
    } else {
        /* Plain address */
        if (slen >= sizeof(out->address)) slen = sizeof(out->address) - 1;
        memcpy(out->address, start, slen);
        out->address[slen] = '\0';
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
        const char *start = p;
        while (*p) {
            if (*p == '<') depth++;
            else if (*p == '>') depth--;
            else if (*p == ',' && depth == 0) break;
            p++;
        }

        char buf[512];
        size_t len = (size_t)(p - start);
        if (len >= sizeof(buf)) len = sizeof(buf) - 1;
        memcpy(buf, start, len);
        buf[len] = '\0';

        if (neverc_mail_parse_address(buf, &out[count]) == 0)
            count++;

        if (*p == ',') p++;
    }
    return count;
}

int neverc_mail_format_address(const neverc_mail_address_t *addr, char *buf, size_t cap) {
    if (!addr || !buf) return -1;
    if (addr->name[0])
        return snprintf(buf, cap, "%s <%s>", addr->name, addr->address);
    return snprintf(buf, cap, "%s", addr->address);
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

        /* Find colon */
        size_t colon = i;
        while (colon < len && data[colon] != ':' && data[colon] != '\n') colon++;
        if (colon >= len || data[colon] != ':') break;

        neverc_mail_header_t *h = &out->headers[out->header_count];
        size_t klen = colon - i;
        if (klen >= sizeof(h->key)) klen = sizeof(h->key) - 1;
        memcpy(h->key, data + i, klen);
        h->key[klen] = '\0';

        size_t vstart = colon + 1;
        while (vstart < len && (data[vstart] == ' ' || data[vstart] == '\t')) vstart++;

        /* Value may span multiple lines (continuation lines start with space/tab) */
        size_t vpos = 0;
        size_t pos = vstart;
        while (pos < len) {
            size_t line_end = pos;
            while (line_end < len && data[line_end] != '\r' && data[line_end] != '\n') line_end++;

            size_t line_len = line_end - pos;
            if (vpos + line_len + 1 < sizeof(h->value)) {
                if (vpos > 0 && pos != vstart) h->value[vpos++] = ' ';
                memcpy(h->value + vpos, data + pos, line_len);
                vpos += line_len;
            }

            if (line_end < len && data[line_end] == '\r') line_end++;
            if (line_end < len && data[line_end] == '\n') line_end++;

            /* Check for continuation */
            if (line_end < len && (data[line_end] == ' ' || data[line_end] == '\t')) {
                while (line_end < len && (data[line_end] == ' ' || data[line_end] == '\t')) line_end++;
                pos = line_end;
            } else {
                pos = line_end;
                break;
            }
        }
        h->value[vpos] = '\0';
        out->header_count++;
        i = pos;
    }

    out->body = data + i;
    out->body_len = len - i;
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

long long neverc_mail_parse_date(const char *s) {
    /* Simplified RFC 5322 date parser */
    if (!s) return -1;

    /* Skip day-of-week if present */
    const char *p = s;
    const char *comma = strchr(p, ',');
    if (comma) p = comma + 1;
    while (*p == ' ') p++;

    int day = 0, year = 0, hour = 0, min = 0, sec = 0;
    char month_str[16] = {0};

    if (sscanf(p, "%d %15s %d %d:%d:%d", &day, month_str, &year, &hour, &min, &sec) < 5)
        return -1;

    static const char *months[] = {
        "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"
    };
    int month = -1;
    for (int i = 0; i < 12; i++) {
        if (month_str[0] == months[i][0] && month_str[1] == months[i][1] && month_str[2] == months[i][2]) {
            month = i; break;
        }
    }
    if (month < 0) return -1;

    if (year < 100) year += (year < 50) ? 2000 : 1900;

    /* Days from epoch (simplified, no leap second) */
    static const int mdays[] = {0,31,59,90,120,151,181,212,243,273,304,334};
    long long days = (long long)(year - 1970) * 365 + mdays[month] + day - 1;
    /* Leap years */
    for (int y = 1970; y < year; y++) {
        if ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) days++;
    }
    if (month > 1 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) days++;

    return days * 86400 + (long long)hour * 3600 + (long long)min * 60 + sec;
}
