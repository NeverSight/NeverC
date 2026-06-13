/*
 * poly1305_bench.c — A/B: portable 32-bit (5x26) vs donna-64 (3x44/42).
 * Cross-validates that both produce identical tags over many random inputs,
 * then times throughput.
 *
 * Build:  cc -O2 -o poly1305_bench poly1305_bench.c
 * Run:    ./poly1305_bench
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

static uint64_t u64le(const uint8_t *p){return (uint64_t)p[0]|((uint64_t)p[1]<<8)|((uint64_t)p[2]<<16)|((uint64_t)p[3]<<24)|((uint64_t)p[4]<<32)|((uint64_t)p[5]<<40)|((uint64_t)p[6]<<48)|((uint64_t)p[7]<<56);}
static uint32_t u32le(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}

/* ── OLD: 32-bit 5x26 ─────────────────────────────────────────────────── */
static void old_auth(uint8_t tag[16], const uint8_t *msg, size_t msg_len, const uint8_t key[32]){
    uint32_t r0=u32le(key+0)&0x0FFFFFFF,r1=u32le(key+4)&0x0FFFFFFC,r2=u32le(key+8)&0x0FFFFFFC,r3=u32le(key+12)&0x0FFFFFFC;
    uint32_t s0=u32le(key+16),s1=u32le(key+20),s2=u32le(key+24),s3=u32le(key+28);
    uint32_t h0=0,h1=0,h2=0,h3=0,h4=0;
    uint32_t rr0=r0&0x3ffffff,rr1=((r0>>26)|(r1<<6))&0x3ffffff,rr2=((r1>>20)|(r2<<12))&0x3ffffff,rr3=((r2>>14)|(r3<<18))&0x3ffffff,rr4=(r3>>8)&0x3ffffff;
    uint32_t s1_5=rr1*5,s2_5=rr2*5,s3_5=rr3*5,s4_5=rr4*5;
    size_t off=0;
    while(off<msg_len){
        uint8_t block[17]; size_t blen=msg_len-off; if(blen>16)blen=16;
        memcpy(block,msg+off,blen); block[blen]=1; memset(block+blen+1,0,16-blen);
        uint32_t t0=u32le(block),t1=u32le(block+4),t2=u32le(block+8),t3=u32le(block+12);
        h0+=t0&0x3ffffff; h1+=((t0>>26)|(t1<<6))&0x3ffffff; h2+=((t1>>20)|(t2<<12))&0x3ffffff;
        h3+=((t2>>14)|(t3<<18))&0x3ffffff; h4+=(t3>>8); if(blen==16)h4+=(1<<24);
        uint64_t d0=(uint64_t)h0*rr0+(uint64_t)h1*s4_5+(uint64_t)h2*s3_5+(uint64_t)h3*s2_5+(uint64_t)h4*s1_5;
        uint64_t d1=(uint64_t)h0*rr1+(uint64_t)h1*rr0+(uint64_t)h2*s4_5+(uint64_t)h3*s3_5+(uint64_t)h4*s2_5;
        uint64_t d2=(uint64_t)h0*rr2+(uint64_t)h1*rr1+(uint64_t)h2*rr0+(uint64_t)h3*s4_5+(uint64_t)h4*s3_5;
        uint64_t d3=(uint64_t)h0*rr3+(uint64_t)h1*rr2+(uint64_t)h2*rr1+(uint64_t)h3*rr0+(uint64_t)h4*s4_5;
        uint64_t d4=(uint64_t)h0*rr4+(uint64_t)h1*rr3+(uint64_t)h2*rr2+(uint64_t)h3*rr1+(uint64_t)h4*rr0;
        uint32_t c;
        c=(uint32_t)(d0>>26);h0=(uint32_t)d0&0x3ffffff;d1+=c; c=(uint32_t)(d1>>26);h1=(uint32_t)d1&0x3ffffff;d2+=c;
        c=(uint32_t)(d2>>26);h2=(uint32_t)d2&0x3ffffff;d3+=c; c=(uint32_t)(d3>>26);h3=(uint32_t)d3&0x3ffffff;d4+=c;
        c=(uint32_t)(d4>>26);h4=(uint32_t)d4&0x3ffffff;h0+=c*5; c=h0>>26;h0&=0x3ffffff;h1+=c;
        off+=blen;
    }
    uint32_t c=h1>>26;h1&=0x3ffffff;h2+=c; c=h2>>26;h2&=0x3ffffff;h3+=c; c=h3>>26;h3&=0x3ffffff;h4+=c;
    c=h4>>26;h4&=0x3ffffff;h0+=c*5; c=h0>>26;h0&=0x3ffffff;h1+=c;
    uint32_t g0=h0+5;c=g0>>26;g0&=0x3ffffff; uint32_t g1=h1+c;c=g1>>26;g1&=0x3ffffff;
    uint32_t g2=h2+c;c=g2>>26;g2&=0x3ffffff; uint32_t g3=h3+c;c=g3>>26;g3&=0x3ffffff; uint32_t g4=h4+c-(1<<26);
    uint32_t mask=(g4>>31)-1; g0&=mask;g1&=mask;g2&=mask;g3&=mask;g4&=mask; mask=~mask;
    h0=(h0&mask)|g0;h1=(h1&mask)|g1;h2=(h2&mask)|g2;h3=(h3&mask)|g3;h4=(h4&mask)|g4;
    uint32_t f0=h0|(h1<<26),f1=(h1>>6)|(h2<<20),f2=(h2>>12)|(h3<<14),f3=(h3>>18)|(h4<<8);
    uint64_t t=(uint64_t)f0+s0;f0=(uint32_t)t;t>>=32; t+=(uint64_t)f1+s1;f1=(uint32_t)t;t>>=32;
    t+=(uint64_t)f2+s2;f2=(uint32_t)t;t>>=32; t+=(uint64_t)f3+s3;f3=(uint32_t)t;
    tag[0]=(uint8_t)f0;tag[1]=(uint8_t)(f0>>8);tag[2]=(uint8_t)(f0>>16);tag[3]=(uint8_t)(f0>>24);
    tag[4]=(uint8_t)f1;tag[5]=(uint8_t)(f1>>8);tag[6]=(uint8_t)(f1>>16);tag[7]=(uint8_t)(f1>>24);
    tag[8]=(uint8_t)f2;tag[9]=(uint8_t)(f2>>8);tag[10]=(uint8_t)(f2>>16);tag[11]=(uint8_t)(f2>>24);
    tag[12]=(uint8_t)f3;tag[13]=(uint8_t)(f3>>8);tag[14]=(uint8_t)(f3>>16);tag[15]=(uint8_t)(f3>>24);
}

/* ── NEW: donna-64 ────────────────────────────────────────────────────── */
typedef unsigned __int128 u128;
static void new_auth(uint8_t tag[16], const uint8_t *msg, size_t msg_len, const uint8_t key[32]){
    uint64_t t0=u64le(key+0),t1=u64le(key+8);
    uint64_t r0=(t0)&0xffc0fffffffULL, r1=((t0>>44)|(t1<<20))&0xfffffc0ffffULL, r2=((t1>>24))&0x00ffffffc0fULL;
    uint64_t s1=r1*(5<<2), s2=r2*(5<<2);
    uint64_t h0=0,h1=0,h2=0,c;
    size_t off=0;
    while(off<msg_len){
        size_t blen=msg_len-off; uint64_t m0,m1,hibit;
        if(blen>=16){blen=16;m0=u64le(msg+off);m1=u64le(msg+off+8);hibit=(uint64_t)1<<40;}
        else{uint8_t b[16];memset(b,0,16);memcpy(b,msg+off,blen);b[blen]=1;m0=u64le(b);m1=u64le(b+8);hibit=0;}
        h0+=(m0)&0xfffffffffffULL; h1+=((m0>>44)|(m1<<20))&0xfffffffffffULL; h2+=(((m1>>24))&0x3ffffffffffULL)|hibit;
        u128 d,d0,d1,d2;
        d0=(u128)h0*r0; d=(u128)h1*s2; d0+=d; d=(u128)h2*s1; d0+=d;
        d1=(u128)h0*r1; d=(u128)h1*r0; d1+=d; d=(u128)h2*s2; d1+=d;
        d2=(u128)h0*r2; d=(u128)h1*r1; d2+=d; d=(u128)h2*r0; d2+=d;
        c=(uint64_t)(d0>>44);h0=(uint64_t)d0&0xfffffffffffULL; d1+=c;c=(uint64_t)(d1>>44);h1=(uint64_t)d1&0xfffffffffffULL;
        d2+=c;c=(uint64_t)(d2>>42);h2=(uint64_t)d2&0x3ffffffffffULL; h0+=c*5;c=(h0>>44);h0&=0xfffffffffffULL;h1+=c;
        off+=blen;
    }
    c=(h1>>44);h1&=0xfffffffffffULL;h2+=c; c=(h2>>42);h2&=0x3ffffffffffULL;h0+=c*5; c=(h0>>44);h0&=0xfffffffffffULL;h1+=c;
    c=(h1>>44);h1&=0xfffffffffffULL;h2+=c; c=(h2>>42);h2&=0x3ffffffffffULL;h0+=c*5; c=(h0>>44);h0&=0xfffffffffffULL;h1+=c;
    uint64_t g0=h0+5;c=(g0>>44);g0&=0xfffffffffffULL; uint64_t g1=h1+c;c=(g1>>44);g1&=0xfffffffffffULL;
    uint64_t g2=h2+c-((uint64_t)1<<42);
    c=(g2>>63)-1; g0&=c;g1&=c;g2&=c; c=~c; h0=(h0&c)|g0;h1=(h1&c)|g1;h2=(h2&c)|g2;
    t0=u64le(key+16);t1=u64le(key+24);
    h0+=((t0)&0xfffffffffffULL);c=(h0>>44);h0&=0xfffffffffffULL;
    h1+=(((t0>>44)|(t1<<20))&0xfffffffffffULL)+c;c=(h1>>44);h1&=0xfffffffffffULL;
    h2+=(((t1>>24))&0x3ffffffffffULL)+c; h2&=0x3ffffffffffULL;
    h0=(h0)|(h1<<44); h1=(h1>>20)|(h2<<24);
    for(int i=0;i<8;i++)tag[i]=(uint8_t)(h0>>(8*i));
    for(int i=0;i<8;i++)tag[8+i]=(uint8_t)(h1>>(8*i));
}

static uint64_t rng=0x9e3779b97f4a7c15ULL;
static uint8_t rb(void){rng^=rng<<13;rng^=rng>>7;rng^=rng<<17;return (uint8_t)(rng>>24);}
static double now_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e3+t.tv_nsec/1e6;}

int main(void){
    /* cross-validate over many lengths */
    int mism=0;
    uint8_t key[32], msg[600], a[16], b[16];
    for(int trial=0;trial<20000;trial++){
        for(int i=0;i<32;i++)key[i]=rb();
        size_t len=(size_t)(rng%600); for(size_t i=0;i<len;i++)msg[i]=rb();
        old_auth(a,msg,len,key); new_auth(b,msg,len,key);
        if(memcmp(a,b,16)!=0){mism++; if(mism<=3)printf("  MISMATCH len=%zu\n",len);}
    }
    printf("cross-validate 20000 random inputs: %s\n", mism?"MISMATCH":"ALL IDENTICAL");

    /* throughput */
    size_t N=1<<20; uint8_t *buf=malloc(N); for(size_t i=0;i<N;i++)buf[i]=rb();
    for(int i=0;i<32;i++)key[i]=rb();
    int it=300;
    double t0=now_ms(); for(int k=0;k<it;k++) old_auth(a,buf,N,key); double t1=now_ms();
    double t2=now_ms(); for(int k=0;k<it;k++) new_auth(b,buf,N,key); double t3=now_ms();
    double oms=(t1-t0)/it, nms=(t3-t2)/it;
    printf("\n1 MiB auth:  32-bit %6.3f ms (%.2f GB/s)   donna-64 %6.3f ms (%.2f GB/s)   %.2fx\n",
        oms, N/1e9/(oms/1e3), nms, N/1e9/(nms/1e3), oms/nms);
    free(buf);
    return mism?1:0;
}
