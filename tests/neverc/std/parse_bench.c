/*
 * parse_bench.c — A/B benchmark for parse/scan-class optimizations:
 *   1. multipart boundary search: naive O(n*m) memcmp-loop vs shared strsearch
 *      engine (memchr + Boyer-Moore-Horspool).
 *   2. textproto header lookup: canonicalize-both-sides-with-malloc vs
 *      allocation-free on-the-fly canonical compare.
 *   3. csv unquoted-field copy: byte-at-a-time vs memchr + memcpy.
 *
 * Build (from repo root):
 *   build-neverc/bin/neverc -Istd/include -Istd/src -O2 -fno-builtin-std \
 *     -o /tmp/parse_bench tests/neverc/std/parse_bench.c -lm
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "bytes/strsearch.h"   /* the real shared engine used by the new code */

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/* ===================================================================== *
 * 1. Boundary search (multipart)
 * ===================================================================== */
__attribute__((noinline))
static size_t old_find(const uint8_t *d, size_t len, const uint8_t *m, size_t ml) {
    if (len < ml) return SIZE_MAX;
    for (size_t i = 0; i <= len - ml; i++)
        if (memcmp(d + i, m, ml) == 0) return i;
    return SIZE_MAX;
}

static size_t scan_old(const uint8_t *d, size_t len, const uint8_t *m, size_t ml) {
    size_t pos = 0, count = 0, off;
    while ((off = old_find(d + pos, len - pos, m, ml)) != SIZE_MAX) {
        count++; pos += off + ml;
    }
    return count;
}

static size_t scan_new(const uint8_t *d, size_t len, const uint8_t *m, size_t ml) {
    nci_ss_finder_t f;
    nci_ss_finder_init(&f, m, ml);
    size_t pos = 0, count = 0, off;
    while ((off = nci_ss_finder_next(&f, d + pos, len - pos)) != SIZE_MAX) {
        count++; pos += off + ml;
    }
    return count;
}

static void bench_boundary(void) {
    printf("=== multipart boundary search: old O(n*m) vs new (memchr+BMH) ===\n");
    printf("%-26s  %12s  %12s  %8s  %s\n", "scenario", "old (ms)", "new (ms)", "speedup", "match");

    const char *marker = "------NeverCFormBoundary8f3a2b1c9d";
    size_t ml = strlen(marker);
    size_t parts = 16;
    size_t gap = 512 * 1024;            /* big body between boundaries */
    size_t len = parts * (gap + ml) + gap;
    uint8_t *buf = malloc(len);

    /* scenario A: filler has NO '-' (typical upload body) */
    memset(buf, 'a', len);
    size_t p = 0;
    for (size_t k = 0; k < parts; k++) { p += gap; memcpy(buf + p, marker, ml); p += ml; }

    double t0 = now_ms(); size_t c1 = scan_old(buf, len, (const uint8_t*)marker, ml);
    double t1 = now_ms(); size_t c2 = scan_new(buf, len, (const uint8_t*)marker, ml);
    double t2 = now_ms();
    printf("%-26s  %12.2f  %12.2f  %7.2fx  %s\n", "no '-' in body",
           t1 - t0, t2 - t1, (t1 - t0)/(t2 - t1), c1 == c2 ? "OK" : "MISMATCH");

    /* scenario B: filler is full of '-' (adversarial for memchr-first-byte) */
    memset(buf, '-', len);
    p = 0;
    for (size_t k = 0; k < parts; k++) { p += gap; memcpy(buf + p, marker, ml); p += ml; }
    t0 = now_ms(); c1 = scan_old(buf, len, (const uint8_t*)marker, ml);
    t1 = now_ms(); c2 = scan_new(buf, len, (const uint8_t*)marker, ml);
    t2 = now_ms();
    printf("%-26s  %12.2f  %12.2f  %7.2fx  %s\n", "body full of '-'",
           t1 - t0, t2 - t1, (t1 - t0)/(t2 - t1), c1 == c2 ? "OK" : "MISMATCH");
    free(buf);
}

/* ===================================================================== *
 * 2. textproto header lookup
 * ===================================================================== */
static int nc_toupper(int c){return (c>='a'&&c<='z')?c-32:c;}
static int nc_tolower(int c){return (c>='A'&&c<='Z')?c+32:c;}

static char *canonical_key(const char *key){
    size_t len=strlen(key); char*out=malloc(len+1); int upper=1;
    for(size_t i=0;i<len;i++){
        if(key[i]=='-'){out[i]='-';upper=1;}
        else if(upper){out[i]=(char)nc_toupper((unsigned char)key[i]);upper=0;}
        else out[i]=(char)nc_tolower((unsigned char)key[i]);
    }
    out[len]='\0'; return out;
}
__attribute__((noinline))
static int old_cmp(const char*a,const char*b){
    char*ca=canonical_key(a),*cb=canonical_key(b);
    int r=strcmp(ca,cb); free(ca); free(cb); return r;
}
__attribute__((noinline))
static int new_eq(const char*canonical,const char*key){
    int upper=1; size_t i=0;
    for(;key[i];i++){char kc=key[i],c;
        if(kc=='-'){c='-';upper=1;}
        else if(upper){c=(char)nc_toupper((unsigned char)kc);upper=0;}
        else c=(char)nc_tolower((unsigned char)kc);
        if(canonical[i]!=c)return 0;}
    return canonical[i]=='\0';
}

static void bench_textproto(void) {
    printf("\n=== textproto header lookup: old (2x malloc/cmp) vs new (alloc-free) ===\n");
    const char *keys[] = {"Content-Type","Content-Length","Host","User-Agent",
        "Accept","Accept-Encoding","Connection","Cache-Control","Cookie","Date",
        "Etag","Server","Set-Cookie","Vary","X-Forwarded-For"};
    int nk = (int)(sizeof(keys)/sizeof(keys[0]));
    const char *lookups[] = {"content-type","CONTENT-LENGTH","x-forwarded-for",
        "set-cookie","accept-encoding","missing-header"};
    int nl = (int)(sizeof(lookups)/sizeof(lookups[0]));
    int reps = 300000;

    volatile int sink = 0;
    double t0 = now_ms();
    for (int r=0;r<reps;r++) for(int l=0;l<nl;l++) for(int i=0;i<nk;i++)
        if(old_cmp(keys[i],lookups[l])==0){sink+=i;break;}
    double t1 = now_ms();
    for (int r=0;r<reps;r++) for(int l=0;l<nl;l++) for(int i=0;i<nk;i++)
        if(new_eq(keys[i],lookups[l])){sink+=i;break;}
    double t2 = now_ms();
    printf("%-26s  %12.2f  %12.2f  %7.2fx\n", "15-header table lookups",
           t1 - t0, t2 - t1, (t1 - t0)/(t2 - t1));
    (void)sink;
}

/* ===================================================================== *
 * 3. csv unquoted field copy
 * ===================================================================== */
__attribute__((noinline))
static size_t csv_old(char *w, size_t wcap, const char *line, size_t len, char delim) {
    size_t wpos=0,i=0;
    while(i<len){
        while(i<len&&line[i]!=delim){ if(wpos>=wcap)return wpos; w[wpos++]=line[i++]; }
        if(wpos>=wcap)return wpos; w[wpos++]='\0';
        if(i<len&&line[i]==delim)i++;
    }
    return wpos;
}
__attribute__((noinline))
static size_t csv_new(char *w, size_t wcap, const char *line, size_t len, char delim) {
    size_t wpos=0,i=0;
    while(i<len){
        const char*s=line+i; size_t rem=len-i;
        const char*d=(const char*)memchr(s,delim,rem);
        size_t fl=d?(size_t)(d-s):rem;
        if(fl>wcap-wpos)return wpos;
        memcpy(w+wpos,s,fl); wpos+=fl; i+=fl;
        if(wpos>=wcap)return wpos; w[wpos++]='\0';
        if(i<len&&line[i]==delim)i++;
    }
    return wpos;
}

static void bench_csv(void) {
    printf("\n=== csv unquoted-field copy: old (byte loop) vs new (memchr+memcpy) ===\n");
    /* a row with several long unquoted fields */
    char line[4096]; size_t pos=0;
    for(int f=0;f<8;f++){ int n=400+f*30; for(int k=0;k<n;k++)line[pos++]=('A'+(k%26)); if(f<7)line[pos++]=','; }
    size_t len=pos;
    char work[8192];
    int reps=2000000;
    volatile size_t sink=0;
    double t0=now_ms(); for(int r=0;r<reps;r++) sink+=csv_old(work,sizeof(work),line,len,',');
    double t1=now_ms(); for(int r=0;r<reps;r++) sink+=csv_new(work,sizeof(work),line,len,',');
    double t2=now_ms();
    printf("%-26s  %12.2f  %12.2f  %7.2fx\n", "8 long fields",
           t1-t0, t2-t1, (t1-t0)/(t2-t1));
    (void)sink;
}

int main(void) {
    bench_boundary();
    bench_textproto();
    bench_csv();
    printf("\nBenchmark complete.\n");
    return 0;
}
