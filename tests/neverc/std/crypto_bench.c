/*
 * Benchmark: optimized crypto primitives vs the previous reference code.
 * Covers: AES block cipher (T-tables vs byte-oriented SubBytes/MixColumns)
 *         GHASH (Shoup 4-bit table vs bit-serial GF(2^128) multiply)
 *         AES-GCM end-to-end seal throughput (library public API).
 *
 * The "old" routines are the pre-optimization implementations, reproduced
 * verbatim and marked noinline so the compiler cannot hoist them out of the
 * timing loops — the same A/B methodology as algo_bench.c / sort_bench.c.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

/* ============================================================
 * OLD AES — byte-oriented reference (verbatim, noinline)
 * ============================================================ */
static const uint8_t S[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16,
};
static const uint32_t RC[10] = {0x01000000,0x02000000,0x04000000,0x08000000,0x10000000,0x20000000,0x40000000,0x80000000,0x1b000000,0x36000000};
static uint32_t subw(uint32_t w){return ((uint32_t)S[(w>>24)&0xff]<<24)|((uint32_t)S[(w>>16)&0xff]<<16)|((uint32_t)S[(w>>8)&0xff]<<8)|S[w&0xff];}
static uint32_t rotw(uint32_t w){return (w<<8)|(w>>24);}
static uint32_t gb(const uint8_t*p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static void pb(uint8_t*p,uint32_t v){p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}

typedef struct { uint32_t ek[60]; int nr; } old_aes_t;
static void old_aes_init(old_aes_t*c,const uint8_t*key,int kl){
    int nk=kl/4; c->nr=nk+6; uint32_t*ek=c->ek;
    for(int i=0;i<nk;i++) ek[i]=gb(key+4*i);
    int total=4*(c->nr+1);
    for(int i=nk;i<total;i++){uint32_t t=ek[i-1]; if(i%nk==0)t=subw(rotw(t))^RC[i/nk-1]; else if(nk>6&&i%nk==4)t=subw(t); ek[i]=ek[i-nk]^t;}
}
__attribute__((noinline))
static void old_aes_encrypt(const old_aes_t*c,uint8_t*dst,const uint8_t*src){
    #define XTIME(x) ((((x)<<1)^((((x)>>7)&1)*0x1b))&0xFF)
    uint32_t s0=gb(src)^c->ek[0],s1=gb(src+4)^c->ek[1],s2=gb(src+8)^c->ek[2],s3=gb(src+12)^c->ek[3];
    int nr=c->nr; const uint32_t*rk=c->ek+4;
    for(int r=1;r<nr;r++){
        uint8_t a[4][4];
        a[0][0]=S[(s0>>24)&0xff];a[0][1]=S[(s1>>24)&0xff];a[0][2]=S[(s2>>24)&0xff];a[0][3]=S[(s3>>24)&0xff];
        a[1][0]=S[(s1>>16)&0xff];a[1][1]=S[(s2>>16)&0xff];a[1][2]=S[(s3>>16)&0xff];a[1][3]=S[(s0>>16)&0xff];
        a[2][0]=S[(s2>>8)&0xff];a[2][1]=S[(s3>>8)&0xff];a[2][2]=S[(s0>>8)&0xff];a[2][3]=S[(s1>>8)&0xff];
        a[3][0]=S[s3&0xff];a[3][1]=S[s0&0xff];a[3][2]=S[s1&0xff];a[3][3]=S[s2&0xff];
        for(int col=0;col<4;col++){uint8_t b0=a[0][col],b1=a[1][col],b2=a[2][col],b3=a[3][col];
            a[0][col]=XTIME(b0)^XTIME(b1)^b1^b2^b3;a[1][col]=b0^XTIME(b1)^XTIME(b2)^b2^b3;a[2][col]=b0^b1^XTIME(b2)^XTIME(b3)^b3;a[3][col]=XTIME(b0)^b0^b1^b2^XTIME(b3);}
        s0=((uint32_t)a[0][0]<<24)|((uint32_t)a[1][0]<<16)|((uint32_t)a[2][0]<<8)|a[3][0];
        s1=((uint32_t)a[0][1]<<24)|((uint32_t)a[1][1]<<16)|((uint32_t)a[2][1]<<8)|a[3][1];
        s2=((uint32_t)a[0][2]<<24)|((uint32_t)a[1][2]<<16)|((uint32_t)a[2][2]<<8)|a[3][2];
        s3=((uint32_t)a[0][3]<<24)|((uint32_t)a[1][3]<<16)|((uint32_t)a[2][3]<<8)|a[3][3];
        s0^=rk[0];s1^=rk[1];s2^=rk[2];s3^=rk[3];rk+=4;
    }
    uint32_t t0=((uint32_t)S[(s0>>24)&0xff]<<24)|((uint32_t)S[(s1>>16)&0xff]<<16)|((uint32_t)S[(s2>>8)&0xff]<<8)|S[s3&0xff];
    uint32_t t1=((uint32_t)S[(s1>>24)&0xff]<<24)|((uint32_t)S[(s2>>16)&0xff]<<16)|((uint32_t)S[(s3>>8)&0xff]<<8)|S[s0&0xff];
    uint32_t t2=((uint32_t)S[(s2>>24)&0xff]<<24)|((uint32_t)S[(s3>>16)&0xff]<<16)|((uint32_t)S[(s0>>8)&0xff]<<8)|S[s1&0xff];
    uint32_t t3=((uint32_t)S[(s3>>24)&0xff]<<24)|((uint32_t)S[(s0>>16)&0xff]<<16)|((uint32_t)S[(s1>>8)&0xff]<<8)|S[s2&0xff];
    pb(dst,t0^rk[0]);pb(dst+4,t1^rk[1]);pb(dst+8,t2^rk[2]);pb(dst+12,t3^rk[3]);
    #undef XTIME
}

/* ============================================================
 * OLD GHASH — bit-serial GF(2^128) multiply (verbatim, noinline)
 * ============================================================ */
__attribute__((noinline))
static void old_ghash_mul(uint8_t result[16], const uint8_t x[16], const uint8_t h[16]) {
    uint8_t v[16]; memcpy(v, h, 16);
    uint8_t z[16]; memset(z, 0, 16);
    for (int i = 0; i < 128; i++) {
        if ((x[i / 8] >> (7 - (i % 8))) & 1)
            for (int j = 0; j < 16; j++) z[j] ^= v[j];
        int lsb = v[15] & 1;
        for (int j = 15; j > 0; j--) v[j] = (v[j] >> 1) | (v[j-1] << 7);
        v[0] >>= 1;
        if (lsb) v[0] ^= 0xE1;
    }
    memcpy(result, z, 16);
}

/* ============================================================
 * NEW GHASH — Shoup 4-bit table (mirrors gcm.c, for isolated A/B)
 * ============================================================ */
static uint64_t lbe(const uint8_t*p){return ((uint64_t)p[0]<<56)|((uint64_t)p[1]<<48)|((uint64_t)p[2]<<40)|((uint64_t)p[3]<<32)|((uint64_t)p[4]<<24)|((uint64_t)p[5]<<16)|((uint64_t)p[6]<<8)|p[7];}
static void sbe(uint8_t*p,uint64_t v){p[0]=(uint8_t)(v>>56);p[1]=(uint8_t)(v>>48);p[2]=(uint8_t)(v>>40);p[3]=(uint8_t)(v>>32);p[4]=(uint8_t)(v>>24);p[5]=(uint8_t)(v>>16);p[6]=(uint8_t)(v>>8);p[7]=(uint8_t)v;}
static void mx1(uint64_t*hi,uint64_t*lo){uint64_t h=*hi,l=*lo;uint64_t c=l&1;*lo=(l>>1)|(h<<63);*hi=(h>>1)^(c?0xE100000000000000ULL:0);}
typedef struct { uint64_t htab[16][2]; uint64_t rem4[16]; } ght_t;
static void new_ghash_init(ght_t*t,const uint8_t h[16]){
    for(int r=0;r<16;r++){uint64_t hh=0,ll=(uint64_t)r;mx1(&hh,&ll);mx1(&hh,&ll);mx1(&hh,&ll);mx1(&hh,&ll);t->rem4[r]=hh;}
    t->htab[0][0]=0;t->htab[0][1]=0;t->htab[8][0]=lbe(h);t->htab[8][1]=lbe(h+8);
    for(int j=8;j>1;j>>=1){uint64_t hh=t->htab[j][0],ll=t->htab[j][1];mx1(&hh,&ll);t->htab[j>>1][0]=hh;t->htab[j>>1][1]=ll;}
    for(int j=2;j<16;j<<=1)for(int k=1;k<j;k++){t->htab[j+k][0]=t->htab[j][0]^t->htab[k][0];t->htab[j+k][1]=t->htab[j][1]^t->htab[k][1];}
}
static void mx4(uint64_t*hi,uint64_t*lo,const uint64_t rem4[16]){uint64_t h=*hi,l=*lo;uint64_t rem=l&0xf;*lo=(l>>4)|(h<<60);*hi=(h>>4)^rem4[rem];}
__attribute__((noinline))
static void new_ghash_mul(const ght_t*t,uint8_t result[16],const uint8_t x[16]){
    uint64_t zh=0,zl=0;
    for(int b=15;b>=0;b--){unsigned by=x[b],lo=by&0xf,hi=by>>4;
        mx4(&zh,&zl,t->rem4);zh^=t->htab[lo][0];zl^=t->htab[lo][1];
        mx4(&zh,&zl,t->rem4);zh^=t->htab[hi][0];zl^=t->htab[hi][1];}
    sbe(result,zh);sbe(result+8,zl);
}

/* ============================================================
 * SHA-256 / SHA-1 / Keccak block functions: textbook vs unrolled
 * (self-contained A/B; the library uses the unrolled forms internally)
 * ============================================================ */
static uint32_t b32(const uint8_t*p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
#define HROTR(x,n) (((x)>>(n))|((x)<<(32-(n))))
#define HROTL(x,n) (((x)<<(n))|((x)>>(32-(n))))
#define HCH(x,y,z) (((x)&(y))^(~(x)&(z)))
#define HMAJ(x,y,z) (((x)&(y))^((x)&(z))^((y)&(z)))
#define HEP0(x) (HROTR(x,2)^HROTR(x,13)^HROTR(x,22))
#define HEP1(x) (HROTR(x,6)^HROTR(x,11)^HROTR(x,25))
#define HSIG0(x) (HROTR(x,7)^HROTR(x,18)^((x)>>3))
#define HSIG1(x) (HROTR(x,17)^HROTR(x,19)^((x)>>10))
static const uint32_t SK[64]={
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
__attribute__((noinline))
static void sha256_blk_old(uint32_t st[8],const uint8_t b[64]){
    uint32_t W[64]; for(int i=0;i<16;i++)W[i]=b32(b+4*i);
    for(int i=16;i<64;i++)W[i]=HSIG1(W[i-2])+W[i-7]+HSIG0(W[i-15])+W[i-16];
    uint32_t a=st[0],bb=st[1],c=st[2],d=st[3],e=st[4],f=st[5],g=st[6],h=st[7];
    for(int i=0;i<64;i++){uint32_t t1=h+HEP1(e)+HCH(e,f,g)+SK[i]+W[i],t2=HEP0(a)+HMAJ(a,bb,c);h=g;g=f;f=e;e=d+t1;d=c;c=bb;bb=a;a=t1+t2;}
    st[0]+=a;st[1]+=bb;st[2]+=c;st[3]+=d;st[4]+=e;st[5]+=f;st[6]+=g;st[7]+=h;
}
__attribute__((noinline))
static void sha256_blk_new(uint32_t st[8],const uint8_t blk[64]){
    uint32_t W[64]; for(int i=0;i<16;i++)W[i]=b32(blk+4*i);
    for(int i=16;i<64;i++)W[i]=HSIG1(W[i-2])+W[i-7]+HSIG0(W[i-15])+W[i-16];
    uint32_t a=st[0],b=st[1],c=st[2],d=st[3],e=st[4],f=st[5],g=st[6],h=st[7],t1,t2;
    #define RR(a,b,c,d,e,f,g,h,i) t1=(h)+HEP1(e)+HCH(e,f,g)+SK[i]+W[i];t2=HEP0(a)+HMAJ(a,b,c);(d)+=t1;(h)=t1+t2;
    for(int i=0;i<64;i+=8){RR(a,b,c,d,e,f,g,h,i+0)RR(h,a,b,c,d,e,f,g,i+1)RR(g,h,a,b,c,d,e,f,i+2)RR(f,g,h,a,b,c,d,e,i+3)RR(e,f,g,h,a,b,c,d,i+4)RR(d,e,f,g,h,a,b,c,i+5)RR(c,d,e,f,g,h,a,b,i+6)RR(b,c,d,e,f,g,h,a,i+7)}
    #undef RR
    st[0]+=a;st[1]+=b;st[2]+=c;st[3]+=d;st[4]+=e;st[5]+=f;st[6]+=g;st[7]+=h;
}
__attribute__((noinline))
static void sha1_blk_old(uint32_t st[5],const uint8_t b[64]){
    uint32_t W[80]; for(int i=0;i<16;i++)W[i]=b32(b+4*i);
    for(int i=16;i<80;i++)W[i]=HROTL(W[i-3]^W[i-8]^W[i-14]^W[i-16],1);
    uint32_t a=st[0],bb=st[1],c=st[2],d=st[3],e=st[4];
    for(int i=0;i<80;i++){uint32_t f,k;
        if(i<20){f=(bb&c)|(~bb&d);k=0x5A827999;}else if(i<40){f=bb^c^d;k=0x6ED9EBA1;}
        else if(i<60){f=(bb&c)|(bb&d)|(c&d);k=0x8F1BBCDC;}else{f=bb^c^d;k=0xCA62C1D6;}
        uint32_t t=HROTL(a,5)+f+e+k+W[i];e=d;d=c;c=HROTL(bb,30);bb=a;a=t;}
    st[0]+=a;st[1]+=bb;st[2]+=c;st[3]+=d;st[4]+=e;
}
__attribute__((noinline))
static void sha1_blk_new(uint32_t st[5],const uint8_t blk[64]){
    uint32_t W[80]; for(int i=0;i<16;i++)W[i]=b32(blk+4*i);
    for(int i=16;i<80;i++)W[i]=HROTL(W[i-3]^W[i-8]^W[i-14]^W[i-16],1);
    uint32_t a=st[0],b=st[1],c=st[2],d=st[3],e=st[4],t;
    #define P1(a,b,c,d,e,i) t=HROTL(a,5)+(((b)&(c))|(~(b)&(d)))+(e)+0x5A827999+W[i];(e)=t;(b)=HROTL(b,30);
    #define P2(a,b,c,d,e,i) t=HROTL(a,5)+((b)^(c)^(d))+(e)+0x6ED9EBA1+W[i];(e)=t;(b)=HROTL(b,30);
    #define P3(a,b,c,d,e,i) t=HROTL(a,5)+(((b)&(c))|((b)&(d))|((c)&(d)))+(e)+0x8F1BBCDC+W[i];(e)=t;(b)=HROTL(b,30);
    #define P4(a,b,c,d,e,i) t=HROTL(a,5)+((b)^(c)^(d))+(e)+0xCA62C1D6+W[i];(e)=t;(b)=HROTL(b,30);
    for(int i=0;i<20;i+=5){P1(a,b,c,d,e,i+0)P1(e,a,b,c,d,i+1)P1(d,e,a,b,c,i+2)P1(c,d,e,a,b,i+3)P1(b,c,d,e,a,i+4)}
    for(int i=20;i<40;i+=5){P2(a,b,c,d,e,i+0)P2(e,a,b,c,d,i+1)P2(d,e,a,b,c,i+2)P2(c,d,e,a,b,i+3)P2(b,c,d,e,a,i+4)}
    for(int i=40;i<60;i+=5){P3(a,b,c,d,e,i+0)P3(e,a,b,c,d,i+1)P3(d,e,a,b,c,i+2)P3(c,d,e,a,b,i+3)P3(b,c,d,e,a,i+4)}
    for(int i=60;i<80;i+=5){P4(a,b,c,d,e,i+0)P4(e,a,b,c,d,i+1)P4(d,e,a,b,c,i+2)P4(c,d,e,a,b,i+3)P4(b,c,d,e,a,i+4)}
    #undef P1
    #undef P2
    #undef P3
    #undef P4
    st[0]+=a;st[1]+=b;st[2]+=c;st[3]+=d;st[4]+=e;
}
#define KROT(x,n) (((x)<<(n))|((x)>>(64-(n))))
static const uint64_t KRC[24]={
0x0000000000000001ULL,0x0000000000008082ULL,0x800000000000808aULL,0x8000000080008000ULL,
0x000000000000808bULL,0x0000000080000001ULL,0x8000000080008081ULL,0x8000000000008009ULL,
0x000000000000008aULL,0x0000000000000088ULL,0x0000000080008009ULL,0x000000008000000aULL,
0x000000008000808bULL,0x800000000000008bULL,0x8000000000008089ULL,0x8000000000008003ULL,
0x8000000000008002ULL,0x8000000000000080ULL,0x000000000000800aULL,0x800000008000000aULL,
0x8000000080008081ULL,0x8000000000008080ULL,0x0000000080000001ULL,0x8000000080008008ULL};
__attribute__((noinline))
static void keccak_old(uint64_t st[25]){
    for(int r=0;r<24;r++){
        uint64_t bc[5]; for(int i=0;i<5;i++)bc[i]=st[i]^st[i+5]^st[i+10]^st[i+15]^st[i+20];
        for(int i=0;i<5;i++){uint64_t t=bc[(i+4)%5]^KROT(bc[(i+1)%5],1);for(int j=0;j<25;j+=5)st[j+i]^=t;}
        uint64_t tmp=st[1];
        static const int pl[24]={10,7,11,17,18,3,5,16,8,21,24,4,15,23,19,13,12,2,20,14,22,9,6,1};
        static const int rc[24]={1,3,6,10,15,21,28,36,45,55,2,14,27,41,56,8,25,43,62,18,39,61,20,44};
        for(int i=0;i<24;i++){int j=pl[i];uint64_t t2=st[j];st[j]=KROT(tmp,rc[i]);tmp=t2;}
        for(int j=0;j<25;j+=5){uint64_t t0=st[j],t1=st[j+1],t2=st[j+2],t3=st[j+3],t4=st[j+4];
            st[j]=t0^(~t1&t2);st[j+1]=t1^(~t2&t3);st[j+2]=t2^(~t3&t4);st[j+3]=t3^(~t4&t0);st[j+4]=t4^(~t0&t1);}
        st[0]^=KRC[r];
    }
}
__attribute__((noinline))
static void keccak_new(uint64_t a[25]){
    for(int r=0;r<24;r++){
        uint64_t c0=a[0]^a[5]^a[10]^a[15]^a[20],c1=a[1]^a[6]^a[11]^a[16]^a[21],c2=a[2]^a[7]^a[12]^a[17]^a[22],c3=a[3]^a[8]^a[13]^a[18]^a[23],c4=a[4]^a[9]^a[14]^a[19]^a[24];
        uint64_t d0=c4^KROT(c1,1),d1=c0^KROT(c2,1),d2=c1^KROT(c3,1),d3=c2^KROT(c4,1),d4=c3^KROT(c0,1);
        a[0]^=d0;a[5]^=d0;a[10]^=d0;a[15]^=d0;a[20]^=d0;a[1]^=d1;a[6]^=d1;a[11]^=d1;a[16]^=d1;a[21]^=d1;
        a[2]^=d2;a[7]^=d2;a[12]^=d2;a[17]^=d2;a[22]^=d2;a[3]^=d3;a[8]^=d3;a[13]^=d3;a[18]^=d3;a[23]^=d3;a[4]^=d4;a[9]^=d4;a[14]^=d4;a[19]^=d4;a[24]^=d4;
        uint64_t b0=a[0],b1=KROT(a[6],44),b2=KROT(a[12],43),b3=KROT(a[18],21),b4=KROT(a[24],14),b5=KROT(a[3],28),b6=KROT(a[9],20),b7=KROT(a[10],3),b8=KROT(a[16],45),b9=KROT(a[22],61),b10=KROT(a[1],1),b11=KROT(a[7],6),b12=KROT(a[13],25),b13=KROT(a[19],8),b14=KROT(a[20],18),b15=KROT(a[4],27),b16=KROT(a[5],36),b17=KROT(a[11],10),b18=KROT(a[17],15),b19=KROT(a[23],56),b20=KROT(a[2],62),b21=KROT(a[8],55),b22=KROT(a[14],39),b23=KROT(a[15],41),b24=KROT(a[21],2);
        a[0]=b0^(~b1&b2);a[1]=b1^(~b2&b3);a[2]=b2^(~b3&b4);a[3]=b3^(~b4&b0);a[4]=b4^(~b0&b1);
        a[5]=b5^(~b6&b7);a[6]=b6^(~b7&b8);a[7]=b7^(~b8&b9);a[8]=b8^(~b9&b5);a[9]=b9^(~b5&b6);
        a[10]=b10^(~b11&b12);a[11]=b11^(~b12&b13);a[12]=b12^(~b13&b14);a[13]=b13^(~b14&b10);a[14]=b14^(~b10&b11);
        a[15]=b15^(~b16&b17);a[16]=b16^(~b17&b18);a[17]=b17^(~b18&b19);a[18]=b18^(~b19&b15);a[19]=b19^(~b15&b16);
        a[20]=b20^(~b21&b22);a[21]=b21^(~b22&b23);a[22]=b22^(~b23&b24);a[23]=b23^(~b24&b20);a[24]=b24^(~b20&b21);
        a[0]^=KRC[r];
    }
}

/* ============================================================
 * NEW: library public API
 * ============================================================ */
#include "neverc/std/crypto/aes.h"
#include "neverc/std/crypto/gcm.h"

static double now_sec(void){struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);return ts.tv_sec+ts.tv_nsec*1e-9;}
static volatile uint8_t sink;

static void bench_aes(void){
    printf("\n=== AES block cipher: T-tables (new) vs byte-oriented (old) ===\n");
    printf("%-22s  %10s  %10s  %8s\n","case","old","new","speedup");
    size_t n=16384; uint8_t*buf=malloc(n),*out=malloc(n);
    srand(7); for(size_t i=0;i<n;i++)buf[i]=(uint8_t)rand();
    int klens[3]={16,24,32};
    for(int t=0;t<3;t++){
        int kl=klens[t]; uint8_t key[32]; for(int j=0;j<kl;j++)key[j]=(uint8_t)rand();
        old_aes_t oc; old_aes_init(&oc,key,kl);
        neverc_aes_ctx_t nc; neverc_aes_init(&nc,key,kl);
        int iters=6000;
        double t0=now_sec();
        for(int it=0;it<iters;it++){for(size_t i=0;i+16<=n;i+=16)old_aes_encrypt(&oc,out+i,buf+i);sink=out[0];}
        double to=now_sec()-t0;
        t0=now_sec();
        for(int it=0;it<iters;it++){for(size_t i=0;i+16<=n;i+=16)neverc_aes_encrypt_block(&nc,out+i,buf+i);sink=out[0];}
        double tn=now_sec()-t0;
        char label[32]; snprintf(label,sizeof(label),"AES-%d encrypt",kl*8);
        printf("%-22s  %8.1f ms  %8.1f ms  %6.2fx\n",label,to*1000,tn*1000,to/tn);
    }
    free(buf);free(out);
}

static void bench_ghash(void){
    printf("\n=== GHASH: Shoup 4-bit table (new) vs bit-serial (old) ===\n");
    printf("%-22s  %10s  %10s  %8s\n","case","old","new","speedup");
    size_t sizes[]={1024,16384,65536}; int ns=sizeof(sizes)/sizeof(sizes[0]);
    uint8_t h[16]; srand(11); for(int j=0;j<16;j++)h[j]=(uint8_t)rand();
    ght_t t; new_ghash_init(&t,h);
    for(int s=0;s<ns;s++){
        size_t n=sizes[s]; uint8_t*buf=malloc(n); for(size_t i=0;i<n;i++)buf[i]=(uint8_t)rand();
        int iters=(int)(50000000/(n+1)); if(iters<200)iters=200;
        uint8_t tag[16],blk[16];
        double t0=now_sec();
        for(int it=0;it<iters;it++){memset(tag,0,16);for(size_t i=0;i+16<=n;i+=16){for(int j=0;j<16;j++)blk[j]=tag[j]^buf[i+j];old_ghash_mul(tag,blk,h);}sink=tag[0];}
        double to=now_sec()-t0;
        t0=now_sec();
        for(int it=0;it<iters;it++){memset(tag,0,16);for(size_t i=0;i+16<=n;i+=16){for(int j=0;j<16;j++)blk[j]=tag[j]^buf[i+j];new_ghash_mul(&t,tag,blk);}sink=tag[0];}
        double tn=now_sec()-t0;
        char label[32]; snprintf(label,sizeof(label),"GHASH n=%zu",n);
        printf("%-22s  %8.1f ms  %8.1f ms  %6.2fx\n",label,to*1000,tn*1000,to/tn);
        free(buf);
    }
}

static void bench_gcm(void){
    printf("\n=== AES-128-GCM seal end-to-end (new, library API) ===\n");
    printf("%-22s  %12s\n","size","throughput");
    size_t sizes[]={64,1024,16384,65536}; int ns=sizeof(sizes)/sizeof(sizes[0]);
    uint8_t key[16]={0x2b,0x7e,0x15,0x16,0x28,0xae,0xd2,0xa6,0xab,0xf7,0x15,0x88,0x09,0xcf,0x4f,0x3c};
    uint8_t nonce[12]={0}; neverc_gcm_ctx ctx; neverc_gcm_init(&ctx,key,16);
    for(int s=0;s<ns;s++){
        size_t n=sizes[s]; uint8_t*pt=malloc(n),*ct=malloc(n),tag[16];
        for(size_t i=0;i<n;i++)pt[i]=(uint8_t)i;
        int iters=(int)(150000000/(n+1)); if(iters<200)iters=200;
        double t0=now_sec();
        for(int it=0;it<iters;it++){neverc_gcm_seal(&ctx,nonce,pt,n,NULL,0,ct,tag);sink=tag[0];}
        double tt=now_sec()-t0;
        double mb=(double)iters*(double)n/(1024.0*1024.0);
        char label[32]; snprintf(label,sizeof(label),"n=%zu",n);
        printf("%-22s  %8.1f MB/s\n",label,mb/tt);
        free(pt);free(ct);
    }
}

static void bench_hashes(void){
    printf("\n=== Hash block functions: unrolled (new) vs textbook (old) ===\n");
    printf("%-22s  %10s  %10s  %8s\n","case","old","new","speedup");
    uint8_t blk[64]; for(int i=0;i<64;i++)blk[i]=(uint8_t)(i*7+3);
    int iters=3000000;
    { uint32_t st[8]={1,2,3,4,5,6,7,8}; double t0=now_sec();
      for(int i=0;i<iters;i++){sha256_blk_old(st,blk);sink=(uint8_t)st[0];} double to=now_sec()-t0;
      uint32_t s2[8]={1,2,3,4,5,6,7,8}; t0=now_sec();
      for(int i=0;i<iters;i++){sha256_blk_new(s2,blk);sink=(uint8_t)s2[0];} double tn=now_sec()-t0;
      printf("%-22s  %8.1f ms  %8.1f ms  %6.2fx\n","SHA-256 block",to*1000,tn*1000,to/tn); }
    { uint32_t st[5]={1,2,3,4,5}; double t0=now_sec();
      for(int i=0;i<iters;i++){sha1_blk_old(st,blk);sink=(uint8_t)st[0];} double to=now_sec()-t0;
      uint32_t s2[5]={1,2,3,4,5}; t0=now_sec();
      for(int i=0;i<iters;i++){sha1_blk_new(s2,blk);sink=(uint8_t)s2[0];} double tn=now_sec()-t0;
      printf("%-22s  %8.1f ms  %8.1f ms  %6.2fx\n","SHA-1 block",to*1000,tn*1000,to/tn); }
    { uint64_t st[25]; for(int i=0;i<25;i++)st[i]=(uint64_t)i*0x9e3779b97f4a7c15ULL;
      int ki=2000000; double t0=now_sec();
      for(int i=0;i<ki;i++){keccak_old(st);sink=(uint8_t)st[0];} double to=now_sec()-t0;
      uint64_t s2[25]; for(int i=0;i<25;i++)s2[i]=(uint64_t)i*0x9e3779b97f4a7c15ULL; t0=now_sec();
      for(int i=0;i<ki;i++){keccak_new(s2);sink=(uint8_t)s2[0];} double tn=now_sec()-t0;
      printf("%-22s  %8.1f ms  %8.1f ms  %6.2fx\n","Keccak-f1600",to*1000,tn*1000,to/tn); }
}

int main(void){
    printf("=== std crypto optimization benchmarks ===\n");
    bench_aes();
    bench_ghash();
    bench_hashes();
    bench_gcm();
    printf("\n=== Done ===\n");
    return 0;
}
