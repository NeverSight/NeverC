/*
 * Benchmark: new SwissTable maps vs the previous Robin-Hood implementation.
 *
 * The OLD table is reproduced verbatim (same wyhash, same load factor, same
 * backward-shift deletion) so the A/B isolates the data-structure change:
 * Robin-Hood open addressing vs SwissTable control-byte group probing.
 *
 * Cases: insert, lookup-hit, lookup-miss (SwissTable's strong suit), delete,
 * and a 80/20 mixed read/write workload, across several sizes.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

/* ============================================================
 * Shared wyhash (both tables hash keys identically)
 * ============================================================ */
static inline uint64_t b_read8(const uint8_t *p){ uint64_t v; memcpy(&v,p,8); return v; }
static inline uint64_t b_read4(const uint8_t *p){ uint32_t v; memcpy(&v,p,4); return (uint64_t)v; }
static inline uint64_t b_wymix(uint64_t a, uint64_t b){
#ifdef __SIZEOF_INT128__
    __uint128_t r=(__uint128_t)a*b; return (uint64_t)r ^ (uint64_t)(r>>64);
#else
    uint64_t ha=a>>32,la=(uint32_t)a,hb=b>>32,lb=(uint32_t)b;
    uint64_t rh=ha*hb,rl=la*lb,rm0=ha*lb,rm1=hb*la;
    uint64_t t=rl+(rm0<<32),c=(t<rl); uint64_t lo=t+(rm1<<32); c+=(lo<t);
    return lo ^ (rh+(rm0>>32)+(rm1>>32)+c);
#endif
}
#define WY0 0xa0761d6478bd642fULL
#define WY1 0xe7037ed1a0b428dbULL
#define WY2 0x8ebc6af09c88c6e3ULL
static uint64_t b_hash(const char *key){
    const uint8_t *p=(const uint8_t*)key;
    if(!p[0]) return WY0;
    if(!p[1]) return p[0]*WY1;
    if(!p[2]) return (((uint64_t)p[0]<<8)|p[1])*WY1 ^ WY0;
    if(!p[3]) return (((uint64_t)p[0]<<16)|((uint64_t)p[1]<<8)|p[2])*WY1 ^ WY0;
    size_t len=4+strlen(key+4); uint64_t seed=WY0,a,b;
    if(len<=16){
        a=(b_read4(p)<<32)|b_read4(p+((len>>3)<<2));
        b=(b_read4(p+len-4)<<32)|b_read4(p+len-4-((len>>3)<<2));
    } else if(len<=48){
        size_t i=0; for(;i+16<=len;i+=16) seed=b_wymix(b_read8(p+i)^WY1,b_read8(p+i+8)^seed);
        a=b_read8(p+len-16); b=b_read8(p+len-8);
    } else {
        uint64_t s1=seed,s2=seed; size_t i=0;
        for(;i+48<=len;i+=48){
            seed=b_wymix(b_read8(p+i)^WY0,b_read8(p+i+8)^seed);
            s1=b_wymix(b_read8(p+i+16)^WY1,b_read8(p+i+24)^s1);
            s2=b_wymix(b_read8(p+i+32)^WY2,b_read8(p+i+40)^s2);
        }
        seed^=s1^s2;
        for(;i+16<=len;i+=16) seed=b_wymix(b_read8(p+i)^WY1,b_read8(p+i+8)^seed);
        a=b_read8(p+len-16); b=b_read8(p+len-8);
    }
    return b_wymix(WY1^len, b_wymix(a^WY1, b^seed));
}

/* ============================================================
 * OLD: Robin-Hood open addressing (reproduced verbatim)
 * ============================================================ */
typedef struct { char *key; void *value; uint64_t hash; uint32_t key_len; } oh_entry_t;
typedef struct { oh_entry_t *e; size_t cap, len; } oh_map_t;
#define OH_EMPTY 0
static uint64_t oh_fix(uint64_t h){ return h?h:1; }
static size_t oh_pd(size_t slot, uint64_t hash, size_t cap){
    size_t nat=(size_t)(hash&(cap-1)); return (slot+cap-nat)&(cap-1);
}
static oh_map_t *oh_new(void){
    oh_map_t *m=calloc(1,sizeof(*m)); m->cap=16; m->e=calloc(m->cap,sizeof(oh_entry_t)); return m;
}
static void oh_free(oh_map_t *m){
    for(size_t i=0;i<m->cap;i++) if(m->e[i].hash!=OH_EMPTY) free(m->e[i].key);
    free(m->e); free(m);
}
static void oh_insert(oh_entry_t *e,size_t cap,char *key,void *val,uint64_t hash,uint32_t kl){
    size_t idx=(size_t)(hash&(cap-1)); oh_entry_t in={key,val,hash,kl};
    for(;;){
        if(e[idx].hash==OH_EMPTY){ e[idx]=in; return; }
        size_t ed=oh_pd(idx,e[idx].hash,cap), id=oh_pd(idx,in.hash,cap);
        if(id>ed){ oh_entry_t t=e[idx]; e[idx]=in; in=t; }
        idx=(idx+1)&(cap-1);
    }
}
static void oh_grow(oh_map_t *m){
    size_t nc=m->cap*2; oh_entry_t *ne=calloc(nc,sizeof(oh_entry_t));
    for(size_t i=0;i<m->cap;i++) if(m->e[i].hash!=OH_EMPTY)
        oh_insert(ne,nc,m->e[i].key,m->e[i].value,m->e[i].hash,m->e[i].key_len);
    free(m->e); m->e=ne; m->cap=nc;
}
static void oh_set(oh_map_t *m,const char *key,void *val){
    if(m->len*4>=m->cap*3) oh_grow(m);
    uint64_t h=oh_fix(b_hash(key)); uint32_t kl=(uint32_t)strlen(key);
    size_t idx=(size_t)(h&(m->cap-1));
    for(;;){
        if(m->e[idx].hash==OH_EMPTY) break;
        if(m->e[idx].hash==h && m->e[idx].key_len==kl && memcmp(m->e[idx].key,key,kl)==0){ m->e[idx].value=val; return; }
        size_t ed=oh_pd(idx,m->e[idx].hash,m->cap), id=oh_pd(idx,h,m->cap);
        if(id>ed) break;
        idx=(idx+1)&(m->cap-1);
    }
    char *dup=malloc(kl+1); memcpy(dup,key,kl+1);
    if(m->e[idx].hash==OH_EMPTY){ m->e[idx].key=dup; m->e[idx].value=val; m->e[idx].hash=h; m->e[idx].key_len=kl; }
    else { oh_entry_t d=m->e[idx]; m->e[idx].key=dup; m->e[idx].value=val; m->e[idx].hash=h; m->e[idx].key_len=kl;
           oh_insert(m->e,m->cap,d.key,d.value,d.hash,d.key_len); }
    m->len++;
}
static void *oh_get(oh_map_t *m,const char *key){
    uint64_t h=oh_fix(b_hash(key)); uint32_t kl=(uint32_t)strlen(key);
    size_t idx=(size_t)(h&(m->cap-1));
    for(size_t d=0;;d++,idx=(idx+1)&(m->cap-1)){
        if(m->e[idx].hash==OH_EMPTY) return NULL;
        if(oh_pd(idx,m->e[idx].hash,m->cap)<d) return NULL;
        if(m->e[idx].hash==h && m->e[idx].key_len==kl && memcmp(m->e[idx].key,key,kl)==0) return m->e[idx].value;
    }
}
static int oh_delete(oh_map_t *m,const char *key){
    uint64_t h=oh_fix(b_hash(key)); uint32_t kl=(uint32_t)strlen(key);
    size_t idx=(size_t)(h&(m->cap-1));
    for(size_t d=0;;d++,idx=(idx+1)&(m->cap-1)){
        if(m->e[idx].hash==OH_EMPTY) return -1;
        if(oh_pd(idx,m->e[idx].hash,m->cap)<d) return -1;
        if(m->e[idx].hash==h && m->e[idx].key_len==kl && memcmp(m->e[idx].key,key,kl)==0) break;
    }
    free(m->e[idx].key); m->e[idx].hash=OH_EMPTY; m->e[idx].key=NULL; m->e[idx].value=NULL; m->len--;
    size_t vac=idx, nx=(idx+1)&(m->cap-1);
    while(m->e[nx].hash!=OH_EMPTY){
        if(oh_pd(nx,m->e[nx].hash,m->cap)==0) break;
        m->e[vac]=m->e[nx]; m->e[nx].hash=OH_EMPTY; m->e[nx].key=NULL; m->e[nx].value=NULL;
        vac=nx; nx=(nx+1)&(m->cap-1);
    }
    return 0;
}

/* ============================================================
 * NEW: SwissTable via the library public API
 * ============================================================ */
#include "neverc/std/maps.h"

/* ============================================================ */
static double now_sec(void){ struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return ts.tv_sec+ts.tv_nsec*1e-9; }
static volatile void *vsink;
static volatile int isink;

static char **make_keys(int n){
    char **k=malloc((size_t)n*sizeof(char*));
    for(int i=0;i<n;i++){ char b[40]; snprintf(b,sizeof b,"key_%d_item_%x",i,i*2654435761u); k[i]=strdup(b); }
    return k;
}
static char **make_miss_keys(int n){
    char **k=malloc((size_t)n*sizeof(char*));
    for(int i=0;i<n;i++){ char b[40]; snprintf(b,sizeof b,"absent_%d_zzz_%x",i,i*40503u); k[i]=strdup(b); }
    return k;
}

int main(void){
    printf("=== maps: SwissTable (new) vs Robin-Hood (old) ===\n");
    int sizes[]={1000,10000,100000,1000000};
    int ns=sizeof(sizes)/sizeof(sizes[0]);

    for(int s=0;s<ns;s++){
        int n=sizes[s];
        char **keys=make_keys(n);
        char **miss=make_miss_keys(n);
        int reps = n<=10000?200: (n<=100000?20:3);

        printf("\n--- n=%d (reps=%d) ---\n", n, reps);
        printf("%-16s %10s %10s %8s\n","op","old(RH)","new(Swiss)","speedup");

        /* ---- insert ---- */
        double t0=now_sec();
        for(int r=0;r<reps;r++){ oh_map_t *m=oh_new(); for(int i=0;i<n;i++) oh_set(m,keys[i],(void*)(intptr_t)(i+1)); vsink=m; oh_free(m); }
        double t_old=now_sec()-t0;
        t0=now_sec();
        for(int r=0;r<reps;r++){ neverc_map_t *m=neverc_map_new(); for(int i=0;i<n;i++) neverc_map_set(m,keys[i],(void*)(intptr_t)(i+1)); vsink=m; neverc_map_free(m); }
        double t_new=now_sec()-t0;
        printf("%-16s %8.1fms %8.1fms %7.2fx\n","insert",t_old*1e3,t_new*1e3,t_old/t_new);

        /* Build one populated table of each for the read/delete cases. */
        oh_map_t *om=oh_new(); for(int i=0;i<n;i++) oh_set(om,keys[i],(void*)(intptr_t)(i+1));
        neverc_map_t *nm=neverc_map_new(); for(int i=0;i<n;i++) neverc_map_set(nm,keys[i],(void*)(intptr_t)(i+1));

        /* ---- lookup hit ---- */
        t0=now_sec(); for(int r=0;r<reps;r++) for(int i=0;i<n;i++) vsink=oh_get(om,keys[i]); t_old=now_sec()-t0;
        t0=now_sec(); for(int r=0;r<reps;r++) for(int i=0;i<n;i++) vsink=neverc_map_get(nm,keys[i]); t_new=now_sec()-t0;
        printf("%-16s %8.1fms %8.1fms %7.2fx\n","lookup-hit",t_old*1e3,t_new*1e3,t_old/t_new);

        /* ---- lookup miss ---- */
        t0=now_sec(); for(int r=0;r<reps;r++) for(int i=0;i<n;i++) vsink=oh_get(om,miss[i]); t_old=now_sec()-t0;
        t0=now_sec(); for(int r=0;r<reps;r++) for(int i=0;i<n;i++) vsink=neverc_map_get(nm,miss[i]); t_new=now_sec()-t0;
        printf("%-16s %8.1fms %8.1fms %7.2fx\n","lookup-miss",t_old*1e3,t_new*1e3,t_old/t_new);

        /* ---- 80/20 mixed (read-heavy) ---- */
        t0=now_sec();
        for(int r=0;r<reps;r++) for(int i=0;i<n;i++){ if((i&7)==0) oh_set(om,keys[i],(void*)(intptr_t)(i+2)); else vsink=oh_get(om,keys[i]); }
        t_old=now_sec()-t0;
        t0=now_sec();
        for(int r=0;r<reps;r++) for(int i=0;i<n;i++){ if((i&7)==0) neverc_map_set(nm,keys[i],(void*)(intptr_t)(i+2)); else vsink=neverc_map_get(nm,keys[i]); }
        t_new=now_sec()-t0;
        printf("%-16s %8.1fms %8.1fms %7.2fx\n","mixed-80/20",t_old*1e3,t_new*1e3,t_old/t_new);

        oh_free(om); neverc_map_free(nm);

        /* ---- delete (build + delete-all per rep) ---- */
        t0=now_sec();
        for(int r=0;r<reps;r++){ oh_map_t *m=oh_new(); for(int i=0;i<n;i++) oh_set(m,keys[i],(void*)(intptr_t)(i+1));
            for(int i=0;i<n;i++) isink=oh_delete(m,keys[i]); oh_free(m); }
        t_old=now_sec()-t0;
        t0=now_sec();
        for(int r=0;r<reps;r++){ neverc_map_t *m=neverc_map_new(); for(int i=0;i<n;i++) neverc_map_set(m,keys[i],(void*)(intptr_t)(i+1));
            for(int i=0;i<n;i++) isink=neverc_map_delete(m,keys[i]); neverc_map_free(m); }
        t_new=now_sec()-t0;
        printf("%-16s %8.1fms %8.1fms %7.2fx\n","delete-all",t_old*1e3,t_new*1e3,t_old/t_new);

        for(int i=0;i<n;i++){ free(keys[i]); free(miss[i]); }
        free(keys); free(miss);
    }
    printf("\n=== Done ===\n");
    return 0;
}
