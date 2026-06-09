#include "neverc/std/math/rand.h"
#include "neverc/std/math.h"

/*
 * xoshiro256** PRNG — fast, high quality, period 2^256-1.
 * Based on https://prng.di.unimi.it/xoshiro256starstar.c
 * by David Blackman and Sebastiano Vigna (public domain).
 */

static uint64_t s[4] = {
    0x180ec6d33cfd0abaULL, 0xd5a61266f0c9392cULL,
    0xa9582618e03fc9aaULL, 0x39abdc4529b1661cULL
};

static inline uint64_t rotl(uint64_t x, int k) {
    return (x << k) | (x >> ((64 - k) & 63));
}

static uint64_t next(void) {
    uint64_t result = rotl(s[1] * 5, 7) * 9;
    uint64_t t = s[1] << 17;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rotl(s[3], 45);
    return result;
}

static uint64_t splitmix64(uint64_t *state) {
    uint64_t z = (*state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

void neverc_rand_seed(uint64_t seed) {
    uint64_t sm = seed;
    s[0] = splitmix64(&sm);
    s[1] = splitmix64(&sm);
    s[2] = splitmix64(&sm);
    s[3] = splitmix64(&sm);
}

uint64_t neverc_rand_uint64(void) { return next(); }

uint32_t neverc_rand_uint32(void) { return (uint32_t)(next() >> 32); }

int32_t neverc_rand_int32(void) { return (int32_t)(next() >> 33); }

int64_t neverc_rand_int63(void) { return (int64_t)(next() >> 1); }

int64_t neverc_rand_intn(int64_t n) {
    if (n <= 0) return 0;
    uint64_t un = (uint64_t)n;
    if ((un & (un - 1)) == 0)
        return (int64_t)((next() >> 1) & (un - 1));
    uint64_t max = (uint64_t)((1ULL << 63) - 1 - (1ULL << 63) % un);
    uint64_t v = next() >> 1;
    while (v > max) v = next() >> 1;
    return (int64_t)(v % un);
}

int32_t neverc_rand_int32n(int32_t n) {
    if (n <= 0) return 0;
    return (int32_t)neverc_rand_intn((int64_t)n);
}

uint32_t neverc_rand_uint32n(uint32_t n) {
    if (n == 0) return 0;
    return (uint32_t)(neverc_rand_uint64() % (uint64_t)n);
}

uint64_t neverc_rand_uint64n(uint64_t n) {
    if (n == 0) return 0;
    uint64_t threshold = -n % n;
    for (;;) {
        uint64_t v = next();
        if (v >= threshold) return v % n;
    }
}

int64_t neverc_rand_int63n(int64_t n) {
    if (n <= 0) return 0;
    return (int64_t)neverc_rand_uint64n((uint64_t)n);
}

void neverc_rand_read(void *buf, size_t len) {
    uint8_t *p = (uint8_t *)buf;
    while (len >= 8) {
        uint64_t v = next();
        for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (i * 8));
        p += 8; len -= 8;
    }
    if (len > 0) {
        uint64_t v = next();
        for (size_t i = 0; i < len; i++) p[i] = (uint8_t)(v >> (i * 8));
    }
}

double neverc_rand_float64(void) {
    return (double)(next() >> 11) * 0x1.0p-53;
}

float neverc_rand_float32(void) {
    return (float)(next() >> 40) * 0x1.0p-24f;
}

/*
 * Ziggurat method for normal distribution.
 * "The Ziggurat Method for Generating Random Variables"
 * (Marsaglia & Tsang, 2000)
 */
static const uint32_t kn[128] = {
    0x76ad2212, 0x0, 0x600f1b53, 0x6ce447a6, 0x725b46a2,
    0x7560051d, 0x774921eb, 0x789a25bd, 0x799045c3, 0x7a4bce5d,
    0x7adf629f, 0x7b5682a6, 0x7bb8a8c6, 0x7c0ae722, 0x7c50cce7,
    0x7c8cec5b, 0x7cc12cd6, 0x7ceefed2, 0x7d177e0b, 0x7d3b8883,
    0x7d5bce6c, 0x7d78dd64, 0x7d932886, 0x7dab0e57, 0x7dc0dd30,
    0x7dd4d688, 0x7de73185, 0x7df81cea, 0x7e07c0a3, 0x7e163efa,
    0x7e23b587, 0x7e303dfd, 0x7e3beec2, 0x7e46db77, 0x7e51155d,
    0x7e5aabb3, 0x7e63abf7, 0x7e6c222c, 0x7e741906, 0x7e7b9a18,
    0x7e82adfa, 0x7e895c63, 0x7e8fac4b, 0x7e95a3fb, 0x7e9b4924,
    0x7ea0a0ef, 0x7ea5b00d, 0x7eaa7ac3, 0x7eaf04f3, 0x7eb3522a,
    0x7eb765a5, 0x7ebb4259, 0x7ebeeafd, 0x7ec2620a, 0x7ec5a9c4,
    0x7ec8c441, 0x7ecbb365, 0x7ece78ed, 0x7ed11671, 0x7ed38d62,
    0x7ed5df12, 0x7ed80cb4, 0x7eda175c, 0x7edc0005, 0x7eddc78e,
    0x7edf6ebf, 0x7ee0f647, 0x7ee25ebe, 0x7ee3a8a9, 0x7ee4d473,
    0x7ee5e276, 0x7ee6d2f5, 0x7ee7a620, 0x7ee85c10, 0x7ee8f4cd,
    0x7ee97047, 0x7ee9ce59, 0x7eea0eca, 0x7eea3147, 0x7eea3568,
    0x7eea1aab, 0x7ee9e071, 0x7ee98602, 0x7ee90a88, 0x7ee86d08,
    0x7ee7ac6a, 0x7ee6c769, 0x7ee5bc9c, 0x7ee48a67, 0x7ee32efc,
    0x7ee1a857, 0x7edff42f, 0x7ede0ffa, 0x7edbf8d9, 0x7ed9ab94,
    0x7ed7248d, 0x7ed45fae, 0x7ed1585c, 0x7ece095f, 0x7eca6ccb,
    0x7ec67be2, 0x7ec22eee, 0x7ebd7d1a, 0x7eb85c35, 0x7eb2c075,
    0x7eac9c20, 0x7ea5df27, 0x7e9e769f, 0x7e964c16, 0x7e8d44ba,
    0x7e834033, 0x7e781728, 0x7e6b9933, 0x7e5d8a1a, 0x7e4d9ded,
    0x7e3b737a, 0x7e268c2f, 0x7e0e3ff5, 0x7df1aa5d, 0x7dcf8c72,
    0x7da61a1e, 0x7d72a0fb, 0x7d30e097, 0x7cd9b4ab, 0x7c600f1a,
    0x7ba90bdc, 0x7a722176, 0x77d664e5,
};
static const float wn[128] = {
    1.7290405e-09f, 1.2680929e-10f, 1.6897518e-10f, 1.9862688e-10f,
    2.2232431e-10f, 2.4244937e-10f, 2.601613e-10f, 2.7611988e-10f,
    2.9073963e-10f, 3.042997e-10f, 3.1699796e-10f, 3.289802e-10f,
    3.4035738e-10f, 3.5121603e-10f, 3.616251e-10f, 3.7164058e-10f,
    3.8130857e-10f, 3.9066758e-10f, 3.9975012e-10f, 4.08584e-10f,
    4.1719309e-10f, 4.2559822e-10f, 4.338176e-10f, 4.418672e-10f,
    4.497613e-10f, 4.5751258e-10f, 4.651324e-10f, 4.7263105e-10f,
    4.8001775e-10f, 4.87301e-10f, 4.944885e-10f, 5.015873e-10f,
    5.0860405e-10f, 5.155446e-10f, 5.2241467e-10f, 5.2921934e-10f,
    5.359635e-10f, 5.426517e-10f, 5.4928817e-10f, 5.5587696e-10f,
    5.624219e-10f, 5.6892646e-10f, 5.753941e-10f, 5.818282e-10f,
    5.882317e-10f, 5.946077e-10f, 6.00959e-10f, 6.072884e-10f,
    6.135985e-10f, 6.19892e-10f, 6.2617134e-10f, 6.3243905e-10f,
    6.386974e-10f, 6.449488e-10f, 6.511956e-10f, 6.5744005e-10f,
    6.6368433e-10f, 6.699307e-10f, 6.7618144e-10f, 6.824387e-10f,
    6.8870465e-10f, 6.949815e-10f, 7.012715e-10f, 7.075768e-10f,
    7.1389966e-10f, 7.202424e-10f, 7.266073e-10f, 7.329966e-10f,
    7.394128e-10f, 7.4585826e-10f, 7.5233547e-10f, 7.58847e-10f,
    7.653954e-10f, 7.719835e-10f, 7.7861395e-10f, 7.852897e-10f,
    7.920138e-10f, 7.987892e-10f, 8.0561924e-10f, 8.125073e-10f,
    8.194569e-10f, 8.2647167e-10f, 8.3355556e-10f, 8.407127e-10f,
    8.479473e-10f, 8.55264e-10f, 8.6266755e-10f, 8.7016316e-10f,
    8.777562e-10f, 8.8545243e-10f, 8.932582e-10f, 9.0117996e-10f,
    9.09225e-10f, 9.174008e-10f, 9.2571584e-10f, 9.341788e-10f,
    9.427997e-10f, 9.515889e-10f, 9.605579e-10f, 9.697193e-10f,
    9.790869e-10f, 9.88676e-10f, 9.985036e-10f, 1.0085882e-09f,
    1.0189509e-09f, 1.0296151e-09f, 1.0406069e-09f, 1.0519566e-09f,
    1.063698e-09f, 1.0758702e-09f, 1.0885183e-09f, 1.1016947e-09f,
    1.1154611e-09f, 1.1298902e-09f, 1.1450696e-09f, 1.1611052e-09f,
    1.1781276e-09f, 1.1962995e-09f, 1.2158287e-09f, 1.2369856e-09f,
    1.2601323e-09f, 1.2857697e-09f, 1.3146202e-09f, 1.347784e-09f,
    1.3870636e-09f, 1.4357403e-09f, 1.5008659e-09f, 1.6030948e-09f,
};
static const float fn_norm[128] = {
    1, 0.9635997f, 0.9362827f, 0.9130436f, 0.89228165f, 0.87324303f,
    0.8555006f, 0.8387836f, 0.8229072f, 0.8077383f, 0.793177f,
    0.7791461f, 0.7655842f, 0.7524416f, 0.73967725f, 0.7272569f,
    0.7151515f, 0.7033361f, 0.69178915f, 0.68049186f, 0.6694277f,
    0.658582f, 0.6479418f, 0.63749546f, 0.6272325f, 0.6171434f,
    0.6072195f, 0.5974532f, 0.58783704f, 0.5783647f, 0.56903f,
    0.5598274f, 0.5507518f, 0.54179835f, 0.5329627f, 0.52424055f,
    0.5156282f, 0.50712204f, 0.49871865f, 0.49041483f, 0.48220766f,
    0.4740943f, 0.46607214f, 0.4581387f, 0.45029163f, 0.44252872f,
    0.43484783f, 0.427247f, 0.41972435f, 0.41227803f, 0.40490642f,
    0.39760786f, 0.3903808f, 0.3832238f, 0.37613547f, 0.36911446f,
    0.3621595f, 0.35526937f, 0.34844297f, 0.34167916f, 0.33497685f,
    0.3283351f, 0.3217529f, 0.3152294f, 0.30876362f, 0.30235484f,
    0.29600215f, 0.28970486f, 0.2834622f, 0.2772735f, 0.27113807f,
    0.2650553f, 0.25902456f, 0.2530453f, 0.24711695f, 0.241239f,
    0.23541094f, 0.22963232f, 0.2239027f, 0.21822165f, 0.21258877f,
    0.20700371f, 0.20146611f, 0.19597565f, 0.19053204f, 0.18513499f,
    0.17978427f, 0.17447963f, 0.1692209f, 0.16400786f, 0.15884037f,
    0.15371831f, 0.14864157f, 0.14361008f, 0.13862377f, 0.13368265f,
    0.12878671f, 0.12393598f, 0.119130544f, 0.11437051f, 0.10965602f,
    0.104987256f, 0.10036444f, 0.095787846f, 0.0912578f, 0.08677467f,
    0.0823389f, 0.077950984f, 0.073611505f, 0.06932112f, 0.06508058f,
    0.06089077f, 0.056752663f, 0.0526674f, 0.048636295f, 0.044660863f,
    0.040742867f, 0.03688439f, 0.033087887f, 0.029356318f,
    0.025693292f, 0.022103304f, 0.018592102f, 0.015167298f,
    0.011839478f, 0.008624485f, 0.005548995f, 0.0026696292f,
};

static const double rn_const = 3.442619855899;

double neverc_rand_norm_float64(void) {
    for (;;) {
        uint64_t u = next();
        int32_t j = (int32_t)u;
        unsigned i = (unsigned)(u >> 32) & 0x7F;
        double x = (double)j * (double)wn[i];
        uint32_t aj = j < 0 ? (uint32_t)(-(uint32_t)j) : (uint32_t)j;
        if (aj < kn[i]) return x;
        if (i == 0) {
            double rx, ry;
            do {
                rx = -neverc_math_log(neverc_rand_float64()) * (1.0 / rn_const);
                ry = -neverc_math_log(neverc_rand_float64());
            } while (ry + ry < rx * rx);
            return j > 0 ? rn_const + rx : -rn_const - rx;
        }
        if (fn_norm[i] + (float)neverc_rand_float64() * (fn_norm[i-1] - fn_norm[i])
                < (float)neverc_math_exp(-0.5 * x * x))
            return x;
    }
}

/*
 * Ziggurat method for exponential distribution.
 */
static const uint32_t ke[256] = {
    0xe290a139, 0x0, 0x9beadebc, 0xc377ac71, 0xd4ddb990,
    0xde893fb8, 0xe4a8e87c, 0xe8dff16a, 0xebf2deab, 0xee49a6e8,
    0xf0204efd, 0xf19bdb8e, 0xf2d458bb, 0xf3da104b, 0xf4b86d78,
    0xf577ad8a, 0xf61de83d, 0xf6afb784, 0xf730a573, 0xf7a37651,
    0xf80a5bb6, 0xf867189d, 0xf8bb1b4f, 0xf9079062, 0xf94d70ca,
    0xf98d8c7d, 0xf9c8928a, 0xf9ff175b, 0xfa319996, 0xfa6085f8,
    0xfa8c3a62, 0xfab5084e, 0xfadb36c8, 0xfaff0410, 0xfb20a6ea,
    0xfb404fb4, 0xfb5e2951, 0xfb7a59e9, 0xfb95038c, 0xfbae44ba,
    0xfbc638d8, 0xfbdcf892, 0xfbf29a30, 0xfc0731df, 0xfc1ad1ed,
    0xfc2d8b02, 0xfc3f6c4d, 0xfc5083ac, 0xfc60ddd1, 0xfc708662,
    0xfc7f8810, 0xfc8decb4, 0xfc9bbd62, 0xfca9027c, 0xfcb5c3c3,
    0xfcc20864, 0xfccdd70a, 0xfcd935e3, 0xfce42ab0, 0xfceebace,
    0xfcf8eb3b, 0xfd02c0a0, 0xfd0c3f59, 0xfd156b7b, 0xfd1e48d6,
    0xfd26daff, 0xfd2f2552, 0xfd372af7, 0xfd3eeee5, 0xfd4673e7,
    0xfd4dbc9e, 0xfd54cb85, 0xfd5ba2f2, 0xfd62451b, 0xfd68b415,
    0xfd6ef1da, 0xfd750047, 0xfd7ae120, 0xfd809612, 0xfd8620b4,
    0xfd8b8285, 0xfd90bcf5, 0xfd95d15e, 0xfd9ac10b, 0xfd9f8d36,
    0xfda43708, 0xfda8bf9e, 0xfdad2806, 0xfdb17141, 0xfdb59c46,
    0xfdb9a9fd, 0xfdbd9b46, 0xfdc170f6, 0xfdc52bd8, 0xfdc8ccac,
    0xfdcc542d, 0xfdcfc30b, 0xfdd319ef, 0xfdd6597a, 0xfdd98245,
    0xfddc94e5, 0xfddf91e6, 0xfde279ce, 0xfde54d1f, 0xfde80c52,
    0xfdeab7de, 0xfded5034, 0xfdefd5be, 0xfdf248e3, 0xfdf4aa06,
    0xfdf6f984, 0xfdf937b6, 0xfdfb64f4, 0xfdfd818d, 0xfdff8dd0,
    0xfe018a08, 0xfe03767a, 0xfe05536c, 0xfe07211c, 0xfe08dfc9,
    0xfe0a8fab, 0xfe0c30fb, 0xfe0dc3ec, 0xfe0f48b1, 0xfe10bf76,
    0xfe122869, 0xfe1383b4, 0xfe14d17c, 0xfe1611e7, 0xfe174516,
    0xfe186b2a, 0xfe19843e, 0xfe1a9070, 0xfe1b8fd6, 0xfe1c8289,
    0xfe1d689b, 0xfe1e4220, 0xfe1f0f26, 0xfe1fcfbc, 0xfe2083ed,
    0xfe212bc3, 0xfe21c745, 0xfe225678, 0xfe22d95f, 0xfe234ffb,
    0xfe23ba4a, 0xfe241849, 0xfe2469f2, 0xfe24af3c, 0xfe24e81e,
    0xfe25148b, 0xfe253474, 0xfe2547c7, 0xfe254e70, 0xfe25485a,
    0xfe25356a, 0xfe251586, 0xfe24e88f, 0xfe24ae64, 0xfe2466e1,
    0xfe2411df, 0xfe23af34, 0xfe233eb4, 0xfe22c02c, 0xfe22336b,
    0xfe219838, 0xfe20ee58, 0xfe20358c, 0xfe1f6d92, 0xfe1e9621,
    0xfe1daef0, 0xfe1cb7ac, 0xfe1bb002, 0xfe1a9798, 0xfe196e0d,
    0xfe1832fd, 0xfe16e5fe, 0xfe15869d, 0xfe141464, 0xfe128ed3,
    0xfe10f565, 0xfe0f478c, 0xfe0d84b1, 0xfe0bac36, 0xfe09bd73,
    0xfe07b7b5, 0xfe059a40, 0xfe03644c, 0xfe011504, 0xfdfeab88,
    0xfdfc26e9, 0xfdf98629, 0xfdf6c83b, 0xfdf3ec01, 0xfdf0f04a,
    0xfdedd3d1, 0xfdea953d, 0xfde7331e, 0xfde3abe9, 0xfddffdfb,
    0xfddc2791, 0xfdd826cd, 0xfdd3f9a8, 0xfdcf9dfc, 0xfdcb1176,
    0xfdc65198, 0xfdc15bb3, 0xfdbc2ce2, 0xfdb6c206, 0xfdb117be,
    0xfdab2a63, 0xfda4f5fd, 0xfd9e7640, 0xfd97a67a, 0xfd908192,
    0xfd8901f2, 0xfd812182, 0xfd78d98e, 0xfd7022bb, 0xfd66f4ed,
    0xfd5d4732, 0xfd530f9c, 0xfd48432b, 0xfd3cd59a, 0xfd30b936,
    0xfd23dea4, 0xfd16349e, 0xfd07a7a3, 0xfcf8219b, 0xfce7895b,
    0xfcd5c220, 0xfcc2aadb, 0xfcae1d5e, 0xfc97ed4e, 0xfc7fe6d4,
    0xfc65ccf3, 0xfc495762, 0xfc2a2fc8, 0xfc07ee19, 0xfbe213c1,
    0xfbb8051a, 0xfb890078, 0xfb5411a5, 0xfb180005, 0xfad33482,
    0xfa839276, 0xfa263b32, 0xf9b72d1c, 0xf930a1a2, 0xf889f023,
    0xf7b577d2, 0xf69c650c, 0xf51530f0, 0xf2cb0e3c, 0xeeefb15d,
    0xe6da6ecf,
};
static const float we[256] = {
    2.0249555e-09f, 1.486674e-11f, 2.4409617e-11f, 3.1968806e-11f,
    3.844677e-11f, 4.4228204e-11f, 4.9516443e-11f, 5.443359e-11f,
    5.905944e-11f, 6.344942e-11f, 6.7643814e-11f, 7.1672945e-11f,
    7.556032e-11f, 7.932458e-11f, 8.298079e-11f, 8.654132e-11f,
    9.0016515e-11f, 9.3415074e-11f, 9.674443e-11f, 1.0001099e-10f,
    1.03220314e-10f, 1.06377254e-10f, 1.09486115e-10f, 1.1255068e-10f,
    1.1557435e-10f, 1.1856015e-10f, 1.2151083e-10f, 1.2442886e-10f,
    1.2731648e-10f, 1.3017575e-10f, 1.3300853e-10f, 1.3581657e-10f,
    1.3860142e-10f, 1.4136457e-10f, 1.4410738e-10f, 1.4683108e-10f,
    1.4953687e-10f, 1.5222583e-10f, 1.54899e-10f, 1.5755733e-10f,
    1.6020171e-10f, 1.6283301e-10f, 1.6545203e-10f, 1.6805951e-10f,
    1.7065617e-10f, 1.732427e-10f, 1.7581973e-10f, 1.7838787e-10f,
    1.8094774e-10f, 1.8349985e-10f, 1.8604476e-10f, 1.8858298e-10f,
    1.9111498e-10f, 1.9364126e-10f, 1.9616223e-10f, 1.9867835e-10f,
    2.0119004e-10f, 2.0369768e-10f, 2.0620168e-10f, 2.087024e-10f,
    2.1120022e-10f, 2.136955e-10f, 2.1618855e-10f, 2.1867974e-10f,
    2.2116936e-10f, 2.2365775e-10f, 2.261452e-10f, 2.2863202e-10f,
    2.311185e-10f, 2.3360494e-10f, 2.360916e-10f, 2.3857874e-10f,
    2.4106667e-10f, 2.4355562e-10f, 2.4604588e-10f, 2.485377e-10f,
    2.5103128e-10f, 2.5352695e-10f, 2.560249e-10f, 2.585254e-10f,
    2.6102867e-10f, 2.6353494e-10f, 2.6604446e-10f, 2.6855745e-10f,
    2.7107416e-10f, 2.7359479e-10f, 2.761196e-10f, 2.7864877e-10f,
    2.8118255e-10f, 2.8372119e-10f, 2.8626485e-10f, 2.888138e-10f,
    2.9136826e-10f, 2.939284e-10f, 2.9649452e-10f, 2.9906677e-10f,
    3.016454e-10f, 3.0423064e-10f, 3.0682268e-10f, 3.0942177e-10f,
    3.1202813e-10f, 3.1464195e-10f, 3.1726352e-10f, 3.19893e-10f,
    3.2253064e-10f, 3.251767e-10f, 3.2783135e-10f, 3.3049485e-10f,
    3.3316744e-10f, 3.3584938e-10f, 3.3854083e-10f, 3.4124212e-10f,
    3.4395342e-10f, 3.46675e-10f, 3.4940711e-10f, 3.5215003e-10f,
    3.5490397e-10f, 3.5766917e-10f, 3.6044595e-10f, 3.6323455e-10f,
    3.660352e-10f, 3.6884823e-10f, 3.7167386e-10f, 3.745124e-10f,
    3.773641e-10f, 3.802293e-10f, 3.8310827e-10f, 3.860013e-10f,
    3.8890866e-10f, 3.918307e-10f, 3.9476775e-10f, 3.9772008e-10f,
    4.0068804e-10f, 4.0367196e-10f, 4.0667217e-10f, 4.09689e-10f,
    4.1272286e-10f, 4.1577405e-10f, 4.1884296e-10f, 4.2192994e-10f,
    4.250354e-10f, 4.281597e-10f, 4.313033e-10f, 4.3446652e-10f,
    4.3764986e-10f, 4.408537e-10f, 4.4407847e-10f, 4.4732465e-10f,
    4.5059267e-10f, 4.5388301e-10f, 4.571962e-10f, 4.6053267e-10f,
    4.6389292e-10f, 4.6727755e-10f, 4.70687e-10f, 4.741219e-10f,
    4.7758275e-10f, 4.810702e-10f, 4.845848e-10f, 4.8812715e-10f,
    4.9169796e-10f, 4.9529775e-10f, 4.989273e-10f, 5.0258725e-10f,
    5.0627835e-10f, 5.100013e-10f, 5.1375687e-10f, 5.1754584e-10f,
    5.21369e-10f, 5.2522725e-10f, 5.2912136e-10f, 5.330522e-10f,
    5.370208e-10f, 5.4102806e-10f, 5.45075e-10f, 5.491625e-10f,
    5.532918e-10f, 5.5746385e-10f, 5.616799e-10f, 5.6594107e-10f,
    5.7024857e-10f, 5.746037e-10f, 5.7900773e-10f, 5.834621e-10f,
    5.8796823e-10f, 5.925276e-10f, 5.971417e-10f, 6.018122e-10f,
    6.065408e-10f, 6.113292e-10f, 6.1617933e-10f, 6.2109295e-10f,
    6.260722e-10f, 6.3111916e-10f, 6.3623595e-10f, 6.4142497e-10f,
    6.4668854e-10f, 6.5202926e-10f, 6.5744976e-10f, 6.6295286e-10f,
    6.6854156e-10f, 6.742188e-10f, 6.79988e-10f, 6.858526e-10f,
    6.9181616e-10f, 6.978826e-10f, 7.04056e-10f, 7.103407e-10f,
    7.167412e-10f, 7.2326256e-10f, 7.2990985e-10f, 7.366886e-10f,
    7.4360473e-10f, 7.5066453e-10f, 7.5787476e-10f, 7.6524265e-10f,
    7.7277595e-10f, 7.80483e-10f, 7.883728e-10f, 7.9645507e-10f,
    8.047402e-10f, 8.1323964e-10f, 8.219657e-10f, 8.309319e-10f,
    8.401528e-10f, 8.496445e-10f, 8.594247e-10f, 8.6951274e-10f,
    8.799301e-10f, 8.9070046e-10f, 9.018503e-10f, 9.134092e-10f,
    9.254101e-10f, 9.378904e-10f, 9.508923e-10f, 9.644638e-10f,
    9.786603e-10f, 9.935448e-10f, 1.0091913e-09f, 1.025686e-09f,
    1.0431306e-09f, 1.0616465e-09f, 1.08138e-09f, 1.1025096e-09f,
    1.1252564e-09f, 1.1498986e-09f, 1.1767932e-09f, 1.206409e-09f,
    1.2393786e-09f, 1.276585e-09f, 1.3193139e-09f, 1.3695435e-09f,
    1.4305498e-09f, 1.508365e-09f, 1.6160854e-09f, 1.7921248e-09f,
};
static const float fe[256] = {
    1, 0.9381437f, 0.90046996f, 0.87170434f, 0.8477855f, 0.8269933f,
    0.8084217f, 0.7915276f, 0.77595687f, 0.7614634f, 0.7478686f,
    0.7350381f, 0.72286767f, 0.71127474f, 0.70019263f, 0.6895665f,
    0.67935055f, 0.6695063f, 0.66000086f, 0.65080583f, 0.6418967f,
    0.63325197f, 0.6248527f, 0.6166822f, 0.60872537f, 0.60096896f,
    0.5934009f, 0.58601034f, 0.5787874f, 0.57172304f, 0.5648092f,
    0.5580383f, 0.5514034f, 0.5448982f, 0.5385169f, 0.53225386f,
    0.5261042f, 0.52006316f, 0.5141264f, 0.50828975f, 0.5025495f,
    0.496902f, 0.49134386f, 0.485872f, 0.48048335f, 0.4751752f,
    0.46994483f, 0.46478975f, 0.45970762f, 0.45469615f, 0.44975325f,
    0.44487688f, 0.44006512f, 0.43531612f, 0.43062815f, 0.42599955f,
    0.42142874f, 0.4169142f, 0.41245446f, 0.40804818f, 0.403694f,
    0.3993907f, 0.39513698f, 0.39093173f, 0.38677382f, 0.38266218f,
    0.37859577f, 0.37457356f, 0.37059465f, 0.3666581f, 0.362763f,
    0.35890847f, 0.35509375f, 0.351318f, 0.3475805f, 0.34388044f,
    0.34021714f, 0.3365899f, 0.33299807f, 0.32944095f, 0.32591796f,
    0.3224285f, 0.3189719f, 0.31554767f, 0.31215525f, 0.30879408f,
    0.3054636f, 0.3021634f, 0.29889292f, 0.2956517f, 0.29243928f,
    0.28925523f, 0.28609908f, 0.28297043f, 0.27986884f, 0.27679393f,
    0.2737453f, 0.2707226f, 0.2677254f, 0.26475343f, 0.26180625f,
    0.25888354f, 0.25598502f, 0.2531103f, 0.25025907f, 0.24743107f,
    0.24462597f, 0.24184346f, 0.23908329f, 0.23634516f, 0.23362878f,
    0.23093392f, 0.2282603f, 0.22560766f, 0.22297576f, 0.22036438f,
    0.21777324f, 0.21520215f, 0.21265087f, 0.21011916f, 0.20760682f,
    0.20511365f, 0.20263945f, 0.20018397f, 0.19774707f, 0.19532852f,
    0.19292815f, 0.19054577f, 0.1881812f, 0.18583426f, 0.18350479f,
    0.1811926f, 0.17889754f, 0.17661946f, 0.17435817f, 0.17211354f,
    0.1698854f, 0.16767362f, 0.16547804f, 0.16329853f, 0.16113494f,
    0.15898713f, 0.15685499f, 0.15473837f, 0.15263714f, 0.15055119f,
    0.14848037f, 0.14642459f, 0.14438373f, 0.14235765f, 0.14034624f,
    0.13834943f, 0.13636707f, 0.13439907f, 0.13244532f, 0.13050574f,
    0.1285802f, 0.12666863f, 0.12477092f, 0.12288698f, 0.12101672f,
    0.119160056f, 0.1173169f, 0.115487166f, 0.11367077f, 0.11186763f,
    0.11007768f, 0.10830083f, 0.10653701f, 0.10478614f, 0.10304816f,
    0.101323f, 0.09961058f, 0.09791085f, 0.09622374f, 0.09454919f,
    0.09288713f, 0.091237515f, 0.08960028f, 0.087975375f, 0.08636274f,
    0.08476233f, 0.083174095f, 0.081597984f, 0.08003395f, 0.07848195f,
    0.076941945f, 0.07541389f, 0.07389775f, 0.072393484f, 0.07090106f,
    0.069420435f, 0.06795159f, 0.066494495f, 0.06504912f, 0.063615434f,
    0.062193416f, 0.060783047f, 0.059384305f, 0.057997175f,
    0.05662164f, 0.05525769f, 0.053905312f, 0.052564494f, 0.051235236f,
    0.049917534f, 0.048611384f, 0.047316793f, 0.046033762f, 0.0447623f,
    0.043502413f, 0.042254124f, 0.041017443f, 0.039792392f,
    0.038578995f, 0.037377283f, 0.036187284f, 0.035009038f,
    0.033842582f, 0.032687962f, 0.031545233f, 0.030414443f, 0.02929566f,
    0.02818895f, 0.027094385f, 0.026012046f, 0.024942026f, 0.023884421f,
    0.022839336f, 0.021806888f, 0.020787204f, 0.019780423f, 0.0187867f,
    0.0178062f, 0.016839107f, 0.015885621f, 0.014945968f, 0.014020392f,
    0.013109165f, 0.012212592f, 0.011331013f, 0.01046481f, 0.009614414f,
    0.008780315f, 0.007963077f, 0.0071633533f, 0.006381906f,
    0.0056196423f, 0.0048776558f, 0.004157295f, 0.0034602648f,
    0.0027887989f, 0.0021459677f, 0.0015362998f, 0.0009672693f,
    0.00045413437f,
};

static const double re_const = 7.69711747013104972;

double neverc_rand_exp_float64(void) {
    for (;;) {
        uint64_t u = next();
        uint32_t j = (uint32_t)u;
        uint8_t i = (uint8_t)(u >> 32);
        double x = (double)j * (double)we[i];
        if (j < ke[i]) return x;
        if (i == 0) return re_const - neverc_math_log(neverc_rand_float64());
        if (fe[i] + (float)neverc_rand_float64() * (fe[i-1] - fe[i])
                < (float)neverc_math_exp(-x))
            return x;
    }
}

void neverc_rand_perm(int n, int *out) {
    for (int i = 0; i < n; i++) out[i] = i;
    for (int i = n - 1; i > 0; i--) {
        int j = (int)neverc_rand_intn((int64_t)(i + 1));
        int tmp = out[i]; out[i] = out[j]; out[j] = tmp;
    }
}

void neverc_rand_shuffle(int n, void (*swap)(int i, int j)) {
    for (int i = n - 1; i > 0; i--) {
        int j = (int)neverc_rand_intn((int64_t)(i + 1));
        swap(i, j);
    }
}

int neverc_rand_int(void) {
    return (int)(next() >> (sizeof(int) == 8 ? 1 : 33));
}

unsigned int neverc_rand_uint(void) {
    return sizeof(unsigned int) == 8 ? (unsigned int)next()
                                     : (unsigned int)(next() >> 32);
}

int neverc_rand_intn_int(int n) {
    if (n <= 0) return 0;
    return (int)neverc_rand_intn((int64_t)n);
}

unsigned int neverc_rand_uintn(unsigned int n) {
    if (n == 0) return 0;
    return (unsigned int)(neverc_rand_uint64() % (uint64_t)n);
}

/*
 * Zipf distribution — rejection-inversion method.
 * (Hormann & Derflinger, 1996)
 */
static double zipf_h(const neverc_rand_zipf_t *z, double x) {
    return neverc_math_exp(z->oneminusQ * neverc_math_log(z->v + x)) * z->oneminusQinv;
}

static double zipf_hinv(const neverc_rand_zipf_t *z, double x) {
    return neverc_math_exp(z->oneminusQinv * neverc_math_log(z->oneminusQ * x)) - z->v;
}

int neverc_rand_zipf_init(neverc_rand_zipf_t *z, double s, double v, uint64_t imax) {
    if (s <= 1.0 || v < 1.0) return 0;
    z->imax = (double)imax;
    z->v = v;
    z->q = s;
    z->oneminusQ = 1.0 - s;
    z->oneminusQinv = 1.0 / z->oneminusQ;
    z->hxm = zipf_h(z, z->imax + 0.5);
    z->hx0minusHxm = zipf_h(z, 0.5) - neverc_math_exp(neverc_math_log(v) * (-s)) - z->hxm;
    z->s_param = 1.0 - zipf_hinv(z, zipf_h(z, 1.5) - neverc_math_exp(-s * neverc_math_log(v + 1.0)));
    return 1;
}

uint64_t neverc_rand_zipf_uint64(neverc_rand_zipf_t *z) {
    double k = 0.0;
    for (;;) {
        double r = neverc_rand_float64();
        double ur = z->hxm + r * z->hx0minusHxm;
        double x = zipf_hinv(z, ur);
        k = neverc_math_floor(x + 0.5);
        if (k - x <= z->s_param) break;
        if (ur >= zipf_h(z, k + 0.5) - neverc_math_exp(-neverc_math_log(k + z->v) * z->q))
            break;
    }
    return (uint64_t)k;
}
