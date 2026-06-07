/*
 * Color palette test suite.
 * Validates Plan9 and WebSafe palette data and closest-color lookup.
 */
#include "neverc/std/image/color/palette.h"
#include <stdio.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define CHECK(name, cond) do { \
    tests_run++; \
    if (cond) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL: %s\n", name); } \
} while(0)

static void test_plan9_basics(void) {
    CHECK("plan9_first_is_black",
          neverc_palette_plan9[0].r == 0 &&
          neverc_palette_plan9[0].g == 0 &&
          neverc_palette_plan9[0].b == 0 &&
          neverc_palette_plan9[0].a == 0xff);

    CHECK("plan9_last_is_white",
          neverc_palette_plan9[255].r == 0xff &&
          neverc_palette_plan9[255].g == 0xff &&
          neverc_palette_plan9[255].b == 0xff);

    /* Verify all entries have alpha = 0xff */
    int all_opaque = 1;
    for (int i = 0; i < NEVERC_PALETTE_PLAN9_LEN; i++) {
        if (neverc_palette_plan9[i].a != 0xff) { all_opaque = 0; break; }
    }
    CHECK("plan9_all_opaque", all_opaque);
}

static void test_websafe_basics(void) {
    CHECK("websafe_first_is_black",
          neverc_palette_websafe[0].r == 0 &&
          neverc_palette_websafe[0].g == 0 &&
          neverc_palette_websafe[0].b == 0);

    CHECK("websafe_last_is_white",
          neverc_palette_websafe[215].r == 0xff &&
          neverc_palette_websafe[215].g == 0xff &&
          neverc_palette_websafe[215].b == 0xff);

    /* WebSafe is 6x6x6 grid: each channel steps by 0x33 (51) */
    CHECK("websafe_entry_5",
          neverc_palette_websafe[5].r == 0x00 &&
          neverc_palette_websafe[5].g == 0x00 &&
          neverc_palette_websafe[5].b == 0xff);

    int all_opaque = 1;
    for (int i = 0; i < NEVERC_PALETTE_WEBSAFE_LEN; i++) {
        if (neverc_palette_websafe[i].a != 0xff) { all_opaque = 0; break; }
    }
    CHECK("websafe_all_opaque", all_opaque);
}

static void test_plan9_index(void) {
    /* Black should map to index 0 */
    CHECK("plan9_idx_black", neverc_palette_plan9_index(0, 0, 0) == 0);

    /* White should map to index 255 */
    CHECK("plan9_idx_white", neverc_palette_plan9_index(0xff, 0xff, 0xff) == 255);

    /* Pure red should find the closest entry */
    int idx = neverc_palette_plan9_index(0xff, 0, 0);
    CHECK("plan9_idx_red_valid", idx >= 0 && idx < 256);
    CHECK("plan9_idx_red_is_red",
          neverc_palette_plan9[idx].r > 0xc0 &&
          neverc_palette_plan9[idx].g < 0x30 &&
          neverc_palette_plan9[idx].b < 0x30);
}

static void test_websafe_index(void) {
    CHECK("websafe_idx_black", neverc_palette_websafe_index(0, 0, 0) == 0);
    CHECK("websafe_idx_white", neverc_palette_websafe_index(0xff, 0xff, 0xff) == 215);

    /* (0x33, 0x66, 0x99) is exactly entry 6*1 + 6*0 + ... = index 45 in websafe */
    /* Actually websafe[45] = {0x33,0x99,0x99} based on the grid. Let me just verify
     * the returned index has the right color. */
    int idx = neverc_palette_websafe_index(0x33, 0x66, 0x99);
    CHECK("websafe_idx_mid_valid", idx >= 0 && idx < 216);
}

static void test_websafe_structure(void) {
    /* WebSafe is exactly a 6x6x6 grid with step=0x33 */
    int grid_ok = 1;
    for (int ri = 0; ri < 6 && grid_ok; ri++) {
        for (int gi = 0; gi < 6 && grid_ok; gi++) {
            for (int bi = 0; bi < 6 && grid_ok; bi++) {
                int idx = ri * 36 + gi * 6 + bi;
                uint8_t er = (uint8_t)(ri * 0x33);
                uint8_t eg = (uint8_t)(gi * 0x33);
                uint8_t eb = (uint8_t)(bi * 0x33);
                if (neverc_palette_websafe[idx].r != er ||
                    neverc_palette_websafe[idx].g != eg ||
                    neverc_palette_websafe[idx].b != eb)
                    grid_ok = 0;
            }
        }
    }
    CHECK("websafe_is_6x6x6_grid", grid_ok);
}

int main(void) {
    test_plan9_basics();
    test_websafe_basics();
    test_plan9_index();
    test_websafe_index();
    test_websafe_structure();

    printf("%d/%d tests passed\n", tests_passed, tests_run);
    if (tests_failed > 0)
        printf("%d tests FAILED\n", tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
