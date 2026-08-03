/*
 * Arbitrary-precision decimal used for exact decimal<->binary float
 * conversion. Shared by parse_float.c (decimal->binary, the Eisel-Lemire
 * fallback) and format_float.c (binary->shortest/fixed decimal). Faithful
 * port of Go's strconv/decimal.go.
 *
 * All functions are static so each translation unit gets its own copy with no
 * link-time conflicts (same pattern as sort_impl.h / strsearch.h).
 */
#ifndef NEVERC_STRCONV_DECIMAL_H
#define NEVERC_STRCONV_DECIMAL_H

#include <stdint.h>

/* ---- IEEE-754 binary64 parameters ---- */
#define NC_MANT_BITS 52
#define NC_EXP_BITS  11
#define NC_EXP_BIAS  (-1023)

#define NC_DEC_CAP   800
#define NC_MAX_SHIFT 60

typedef struct {
    uint8_t d[NC_DEC_CAP];  /* big-endian decimal digits, '0'..'9' */
    int nd;                 /* number of digits in use */
    int dp;                 /* decimal point: digits to the left of it */
    int neg;
    int trunc;              /* nonzero digits were discarded below d[] */
} nc_decimal;

static const struct { int delta; const char *cutoff; } nc_leftcheats[] = {
    {0, ""}, {1, "5"}, {1, "25"}, {1, "125"}, {2, "625"}, {2, "3125"},
    {2, "15625"}, {3, "78125"}, {3, "390625"}, {3, "1953125"},
    {4, "9765625"}, {4, "48828125"}, {4, "244140625"}, {4, "1220703125"},
    {5, "6103515625"}, {5, "30517578125"}, {5, "152587890625"},
    {6, "762939453125"}, {6, "3814697265625"}, {6, "19073486328125"},
    {7, "95367431640625"}, {7, "476837158203125"}, {7, "2384185791015625"},
    {7, "11920928955078125"}, {8, "59604644775390625"},
    {8, "298023223876953125"}, {8, "1490116119384765625"},
    {9, "7450580596923828125"}, {9, "37252902984619140625"},
    {9, "186264514923095703125"}, {10, "931322574615478515625"},
    {10, "4656612873077392578125"}, {10, "23283064365386962890625"},
    {10, "116415321826934814453125"}, {11, "582076609134674072265625"},
    {11, "2910383045673370361328125"}, {11, "14551915228366851806640625"},
    {12, "72759576141834259033203125"},
    {12, "363797880709171295166015625"},
    {12, "1818989403545856475830078125"},
    {13, "9094947017729282379150390625"},
    {13, "45474735088646411895751953125"},
    {13, "227373675443232059478759765625"},
    {13, "1136868377216160297393798828125"},
    {14, "5684341886080801486968994140625"},
    {14, "28421709430404007434844970703125"},
    {14, "142108547152020037174224853515625"},
    {15, "710542735760100185871124267578125"},
    {15, "3552713678800500929355621337890625"},
    {15, "17763568394002504646778106689453125"},
    {16, "88817841970012523233890533447265625"},
    {16, "444089209850062616169452667236328125"},
    {16, "2220446049250313080847263336181640625"},
    {16, "11102230246251565404236316680908203125"},
    {17, "55511151231257827021181583404541015625"},
    {17, "277555756156289135105907917022705078125"},
    {17, "1387778780781445675529539585113525390625"},
    {18, "6938893903907228377647697925567626953125"},
    {18, "34694469519536141888238489627838134765625"},
    {18, "173472347597680709441192448139190673828125"},
    {19, "867361737988403547205962240695953369140625"},
};

static void nc_dec_trim(nc_decimal *a) {
    if (a->nd < 0) a->nd = 0;
    if (a->nd > NC_DEC_CAP) a->nd = NC_DEC_CAP;
    while (a->nd > 0 && a->d[a->nd - 1] == '0') a->nd--;
    if (a->nd == 0) a->dp = 0;
}

/* prefix of a->d (length a->nd) strictly less than C string s? */
static int nc_prefix_less(const nc_decimal *a, const char *s) {
    int i;
    for (i = 0; s[i] != '\0'; i++) {
        if (i >= a->nd) return 1;
        if (a->d[i] != (uint8_t)s[i]) return a->d[i] < (uint8_t)s[i];
    }
    return 0;
}

static void nc_left_shift(nc_decimal *a, unsigned k) {
    if (k > NC_MAX_SHIFT) return;
    int delta = nc_leftcheats[k].delta;
    if (nc_prefix_less(a, nc_leftcheats[k].cutoff)) delta--;

    int r = a->nd;
    int w = a->nd + delta;
    uint64_t n = 0;
    for (r--; r >= 0; r--) {
        n += (uint64_t)(a->d[r] - '0') << k;
        uint64_t quo = n / 10;
        uint64_t rem = n - 10 * quo;
        w--;
        if (w >= 0 && w < NC_DEC_CAP) a->d[w] = (uint8_t)(rem + '0');
        else if (rem != 0) a->trunc = 1;
        n = quo;
    }
    while (n > 0) {
        uint64_t quo = n / 10;
        uint64_t rem = n - 10 * quo;
        w--;
        if (w >= 0 && w < NC_DEC_CAP) a->d[w] = (uint8_t)(rem + '0');
        else if (rem != 0) a->trunc = 1;
        n = quo;
    }
    a->nd += delta;
    if (a->nd >= NC_DEC_CAP) a->nd = NC_DEC_CAP;
    a->dp += delta;
    nc_dec_trim(a);
}

static void nc_right_shift(nc_decimal *a, unsigned k) {
    int r = 0, w = 0;
    uint64_t n = 0;
    for (; (n >> k) == 0; r++) {
        if (r >= a->nd) {
            if (n == 0) { a->nd = 0; return; }
            while ((n >> k) == 0) { n = n * 10; r++; }
            break;
        }
        n = n * 10 + (uint64_t)(a->d[r] - '0');
    }
    a->dp -= r - 1;
    uint64_t mask = ((uint64_t)1 << k) - 1;
    for (; r < a->nd; r++) {
        uint64_t c = (uint64_t)(a->d[r] - '0');
        uint64_t dig = n >> k;
        n &= mask;
        a->d[w++] = (uint8_t)(dig + '0');
        n = n * 10 + c;
    }
    while (n > 0) {
        uint64_t dig = n >> k;
        n &= mask;
        if (w < NC_DEC_CAP) a->d[w++] = (uint8_t)(dig + '0');
        else if (dig > 0) a->trunc = 1;
        n = n * 10;
    }
    a->nd = w;
    nc_dec_trim(a);
}

/* Multiply (k>0) or divide (k<0) the decimal value by 2^|k|, exactly. */
static void nc_dec_shift(nc_decimal *a, int k) {
    if (a->nd == 0) return;
    if (k > 0) {
        while (k > NC_MAX_SHIFT) { nc_left_shift(a, NC_MAX_SHIFT); k -= NC_MAX_SHIFT; }
        nc_left_shift(a, (unsigned)k);
    } else if (k < 0) {
        while (k < -NC_MAX_SHIFT) { nc_right_shift(a, NC_MAX_SHIFT); k += NC_MAX_SHIFT; }
        nc_right_shift(a, (unsigned)(-k));
    }
}

static int nc_should_round_up(const nc_decimal *a, int nd) {
    if (nd < 0 || nd >= a->nd) return 0;
    if (a->d[nd] == '5' && nd + 1 == a->nd) {        /* exactly halfway */
        if (a->trunc) return 1;
        return nd > 0 && ((a->d[nd - 1] - '0') & 1) != 0;  /* round to even */
    }
    return a->d[nd] >= '5';
}

#endif /* NEVERC_STRCONV_DECIMAL_H */
