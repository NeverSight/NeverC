/*
 * gif_bench.c — A/B benchmark for the GIF LZW decode reconstruction method.
 *
 * old: per-entry malloc + memcpy of the fully expanded byte string for every
 *      dictionary code (O(sum of entry lengths) work + thousands of allocs).
 * new: fixed prefix/suffix chains + a reconstruction stack (zero per-entry
 *      allocation), matching the library's gif.c decoder.
 *
 * Both decode the same valid code stream (produced by the fixed encoder) and
 * must yield identical pixels.
 *
 * Build (from repo root):
 *   build-neverc/bin/neverc -O2 -fno-builtin-std -o /tmp/gif_bench \
 *     tests/neverc/std/gif_bench.c -lm
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#define LZW_MAX_CODE 4096
typedef struct { int prefix, suffix, child, next; } lzw_entry_t;

static int g_codes[1<<22]; static int g_widths[1<<22]; static int g_n;
static int g_min_cs;

/* Fixed encoder (child/sibling list) — only used to generate a valid stream. */
static void encode(const uint8_t *data, size_t len, int min_code_size) {
    int clear=1<<min_code_size, eoi=clear+1, next=eoi+1, cs=min_code_size+1;
    lzw_entry_t *t = calloc(LZW_MAX_CODE,sizeof(lzw_entry_t));
    for(int i=0;i<LZW_MAX_CODE;i++){t[i].prefix=-1;t[i].suffix=-1;t[i].child=-1;t[i].next=-1;}
    g_n=0; g_min_cs=min_code_size;
    #define E(c) do{ g_codes[g_n]=(c); g_widths[g_n]=cs; g_n++; }while(0)
    E(clear);
    if(len){ int cur=data[0];
        for(size_t i=1;i<len;i++){int px=data[i],f=-1;
            for(int e=t[cur].child;e!=-1;e=t[e].next)if(t[e].suffix==px){f=e;break;}
            if(f>=0)cur=f; else{ E(cur);
                if(next<LZW_MAX_CODE){t[next].prefix=cur;t[next].suffix=px;t[next].child=-1;t[next].next=t[cur].child;t[cur].child=next;next++;}
                if(next>(1<<cs)&&cs<12)cs++;
                if(next>=LZW_MAX_CODE-1){E(clear);for(int j=0;j<LZW_MAX_CODE;j++){t[j].prefix=-1;t[j].suffix=-1;t[j].child=-1;t[j].next=-1;}next=eoi+1;cs=min_code_size+1;}
                cur=px; } }
        E(cur); }
    E(eoi);
    #undef E
    free(t);
}

/* OLD decode: string table with per-entry malloc/memcpy. */
__attribute__((noinline))
static size_t dec_old(uint8_t *out, size_t cap) {
    int clear=1<<g_min_cs, eoi=clear+1; int next=eoi+1; int prev=-1;
    uint8_t *S[4096]; int SL[4096];
    for(int i=0;i<4096;i++){S[i]=NULL;SL[i]=0;}
    for(int i=0;i<clear;i++){S[i]=malloc(1);S[i][0]=(uint8_t)i;SL[i]=1;}
    size_t o=0;
    for(int k=0;k<g_n;k++){int code=g_codes[k];
        if(code==eoi)break;
        if(code==clear){for(int i=eoi+1;i<4096;i++){free(S[i]);S[i]=NULL;SL[i]=0;}next=eoi+1;prev=-1;continue;}
        uint8_t *os;int ol;
        if(code<next&&S[code]){os=S[code];ol=SL[code];}
        else if(code==next&&prev>=0&&S[prev]){ol=SL[prev]+1;os=malloc(ol);memcpy(os,S[prev],SL[prev]);os[ol-1]=S[prev][0];
            if(next<4096){S[next]=os;SL[next]=ol;next++;}
            for(int i=0;i<ol&&o<cap;i++)out[o++]=os[i];prev=code;continue;}
        else break;
        for(int i=0;i<ol&&o<cap;i++)out[o++]=os[i];
        if(prev>=0&&S[prev]&&next<4096){int nl=SL[prev]+1;S[next]=malloc(nl);memcpy(S[next],S[prev],SL[prev]);S[next][nl-1]=os[0];SL[next]=nl;next++;}
        prev=code;
    }
    for(int i=0;i<4096;i++)free(S[i]);
    return o;
}

/* NEW decode: prefix/suffix chains + stack (matches gif.c). */
__attribute__((noinline))
static size_t dec_new(uint8_t *out, size_t cap) {
    int clear=1<<g_min_cs, eoi=clear+1; int next=eoi+1; int prev=-1;
    uint16_t prefix[4096]; uint8_t suffix[4096]; uint8_t st[4096]; uint8_t fb=0;
    for(int i=0;i<clear;i++){prefix[i]=0;suffix[i]=(uint8_t)i;}
    size_t o=0;
    for(int k=0;k<g_n;k++){int code=g_codes[k];
        if(code==eoi)break;
        if(code==clear){next=eoi+1;prev=-1;continue;}
        int sp=0,c;
        if(code<next)c=code; else if(code==next&&prev>=0){st[sp++]=fb;c=prev;} else break;
        while(c>=clear){st[sp++]=suffix[c];c=prefix[c];}
        st[sp++]=(uint8_t)c; fb=(uint8_t)c;
        for(int i=sp-1;i>=0&&o<cap;i--)out[o++]=st[i];
        if(prev>=0&&next<4096){prefix[next]=(uint16_t)prev;suffix[next]=fb;next++;}
        prev=code;
    }
    return o;
}

static double now_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000.0+t.tv_nsec/1e6;}

int main(void){
    int w=512,h=512,ps=256; size_t n=(size_t)w*h;
    uint8_t *img=malloc(n);
    for(int y=0;y<h;y++)for(int x=0;x<w;x++)img[y*w+x]=(uint8_t)((x*7+y*13+(x/3)*(y/5))%ps);
    encode(img,n,8);

    uint8_t *o1=malloc(n+16),*o2=malloc(n+16);
    int reps=400;
    double t0=now_ms(); size_t a=0; for(int r=0;r<reps;r++)a=dec_old(o1,n+16);
    double t1=now_ms(); size_t b=0; for(int r=0;r<reps;r++)b=dec_new(o2,n+16);
    double t2=now_ms();
    int match=(a==b)&&(a==n)&&memcmp(o1,o2,a)==0&&memcmp(o1,img,a)==0;

    printf("=== GIF LZW decode: old (string table, per-entry malloc) vs new (prefix chain) ===\n");
    printf("image %dx%d pal=%d, %d codes, %d reps\n", w, h, ps, g_n, reps);
    printf("old   %10.2f ms (%.4f ms/decode)\n", t1-t0, (t1-t0)/reps);
    printf("new   %10.2f ms (%.4f ms/decode)\n", t2-t1, (t2-t1)/reps);
    printf("speedup %8.2fx   %s\n", (t1-t0)/(t2-t1), match?"OK (exact)":"MISMATCH");
    return match?0:1;
}
