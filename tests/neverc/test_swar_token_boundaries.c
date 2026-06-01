// Regression test for the SWAR per-byte zero-detector bug.
//
// The classic word-at-a-time "has zero byte" formula
//     (V - 0x0101..) & ~V & 0x8080..
// is only valid as a whole-word boolean. Used as a per-byte mask it
// borrow-propagates a false positive into the byte *after* a matched (zero)
// byte. The lexer's no-SIMD (SWAR) scanners used it per byte, so any construct
// where a scanned char is immediately followed by (that char +/- 1) was
// mis-tokenized. Only affected builds without SIMD intrinsics (e.g. MSVC).
//
//   - whitespace skip: ' '(0x20) then '!'(0x21)  =>  "a  != b" became "a = b"
//   - identifier:      '_'(0x5F) then '^'(0x5E)  =>  "x_^y"    became one ident
//   - pp-number:       '.'(0x2E) then '/'(0x2F)  =>  "1./2"    became one number
//
// See SourceScannerSIMDHelpers.inc / SourceScannerTokens.cpp (carry-free
// per-byte detector). Each construct below would fail to COMPILE if the bug
// regressed, so a clean build + correct values guards it.
#include <stdio.h>

int main(void) {
    int fails = 0;
    int a = 1, b = 2;

    /* whitespace run before '!=' (' ' is 0x20, '!' is 0x21) */
    if ((a != b)   != 1) fails |= 1;    /* 1 space  */
    if ((a  != b)  != 1) fails |= 2;    /* 2 spaces */
    if ((a   != b) != 1) fails |= 4;    /* 3 spaces */
    if ((a	!= b)  != 1) fails |= 8;    /* tab      */

    /* identifier ending in '_' immediately followed by '^' (0x5F then 0x5E) */
    { int x_ = 5, y = 3;       if ((x_^y) != 6) fails |= 16; }
    { int abcdef_ = 5, g = 3;  if ((abcdef_^g) != 6) fails |= 32; }

    /* pp-number with '.' immediately followed by '/' (0x2E then 0x2F) */
    if (1./2 != 0.5) fails |= 64;
    if (10./4 != 2.5) fails |= 128;

    if (fails == 0)
        printf("test_swar_token_boundaries: ALL PASSED\n");
    else
        printf("test_swar_token_boundaries: FAILED mask=%d\n", fails);
    return fails;
}
