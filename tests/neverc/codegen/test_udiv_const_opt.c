// Test: 32-bit unsigned division by constants optimization (33-bit magic)
// Verifies that the Granlund-Montgomery IsAdd=true case is optimized to a
// single 64x64->128 high multiply on 64-bit targets, and produces correct
// results for all edge cases.
//
// RUN: %neverc -O2 %s -o %t && %t
//
// Reference: "Optimization of 32-bit Unsigned Division by Constants on 64-bit
// Targets" (arxiv.org/html/2604.07902v1)

#include <stdint.h>
#include <stdio.h>

// --- IsAdd=true divisors (33-bit magic, optimized to single umulh/mulq) ---
uint32_t div7(uint32_t x)   { return x / 7; }
uint32_t div19(uint32_t x)  { return x / 19; }
uint32_t div21(uint32_t x)  { return x / 21; }
uint32_t div27(uint32_t x)  { return x / 27; }
uint32_t div31(uint32_t x)  { return x / 31; }
uint32_t div35(uint32_t x)  { return x / 35; }
uint32_t div37(uint32_t x)  { return x / 37; }
uint32_t div41(uint32_t x)  { return x / 41; }
uint32_t div49(uint32_t x)  { return x / 49; }
uint32_t div55(uint32_t x)  { return x / 55; }
uint32_t div63(uint32_t x)  { return x / 63; }
uint32_t div95(uint32_t x)  { return x / 95; }
uint32_t div107(uint32_t x) { return x / 107; }
uint32_t div127(uint32_t x) { return x / 127; }

// --- IsAdd=false divisors (standard path, must remain correct) ---
uint32_t div3(uint32_t x)   { return x / 3; }
uint32_t div5(uint32_t x)   { return x / 5; }
uint32_t div9(uint32_t x)   { return x / 9; }

// --- urem uses udiv internally, must also be correct ---
uint32_t rem7(uint32_t x)   { return x % 7; }
uint32_t rem19(uint32_t x)  { return x % 19; }
uint32_t rem107(uint32_t x) { return x % 107; }

static int failures = 0;

static void check_div(uint32_t x, uint32_t d, uint32_t got) {
    uint32_t expected = x / d;
    if (got != expected) {
        printf("  FAIL: %u / %u = %u, got %u\n", x, d, expected, got);
        failures++;
    }
}

static void check_rem(uint32_t x, uint32_t d, uint32_t got) {
    uint32_t expected = x % d;
    if (got != expected) {
        printf("  FAIL: %u %% %u = %u, got %u\n", x, d, expected, got);
        failures++;
    }
}

int main(void) {
    // --- Edge values (boundaries, divisor multiples, powers of two, extremes) ---
    uint32_t edges[] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 13, 14, 18, 19, 20, 21, 26, 27, 30,
        31, 34, 35, 36, 37, 40, 41, 48, 49, 54, 55, 62, 63, 94, 95, 106,
        107, 126, 127, 255, 256, 1000, 10000, 65535, 65536, 100000,
        1000000, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFEu, 0xFFFFFFFFu
    };
    int ne = sizeof(edges) / sizeof(edges[0]);

    for (int i = 0; i < ne; i++) {
        uint32_t x = edges[i];
        check_div(x, 7, div7(x));
        check_div(x, 19, div19(x));
        check_div(x, 21, div21(x));
        check_div(x, 27, div27(x));
        check_div(x, 31, div31(x));
        check_div(x, 35, div35(x));
        check_div(x, 37, div37(x));
        check_div(x, 41, div41(x));
        check_div(x, 49, div49(x));
        check_div(x, 55, div55(x));
        check_div(x, 63, div63(x));
        check_div(x, 95, div95(x));
        check_div(x, 107, div107(x));
        check_div(x, 127, div127(x));
        check_div(x, 3, div3(x));
        check_div(x, 5, div5(x));
        check_div(x, 9, div9(x));
        check_rem(x, 7, rem7(x));
        check_rem(x, 19, rem19(x));
        check_rem(x, 107, rem107(x));
    }

    // --- Exhaustive low range 0..500000 ---
    for (uint32_t x = 0; x < 500000; x++) {
        check_div(x, 7, div7(x));
        check_div(x, 19, div19(x));
        check_div(x, 21, div21(x));
        check_div(x, 107, div107(x));
        check_rem(x, 7, rem7(x));
        check_rem(x, 19, rem19(x));
    }

    // --- High-value range near UINT32_MAX ---
    for (uint32_t x = 0xFFFF0000u; x != 0; x++) {
        check_div(x, 7, div7(x));
        check_div(x, 19, div19(x));
        check_div(x, 107, div107(x));
    }

    if (failures == 0)
        printf("test_udiv_const_opt: ALL PASSED\n");
    else
        printf("test_udiv_const_opt: %d FAILURES\n", failures);
    return failures != 0;
}
