// RUN: %neverc -std=c23 %s -o %t && %t
/* test_neverc_string_ic.c -- ASCII case-insensitive surface.
 *
 * Pins down the boost::algorithm::iequals / Java equalsIgnoreCase /
 * Go strings.EqualFold (ASCII) parity layer the user gets via the
 * dotted `_ic` family.  Single mainstream spelling per concept --
 * the `_ic` suffix (matches the prelude function names):
 *
 *   s.eq_ic(t)
 *   s.compare_ic(t)
 *   s.find_ic(t)
 *   s.contains_ic(t)
 *   s.starts_with_ic(t)
 *   s.ends_with_ic(t)
 *
 * The boost-flavour `i`-prefix family (`iequals`, `icompare`,
 * `ifind`, `icontains`, `istarts_with`, `iends_with`) and the
 * Java `equals_ic` alias were dropped to keep one obvious dotted
 * name per concept; downstream code that still wants the boost
 * spelling can wrap it in a one-liner.
 *
 * Coverage:
 *
 *   1. ASCII fold contract: 'A'..'Z' fold to 'a'..'z', everything else
 *      passes through unchanged (digits, punctuation, control bytes).
 *
 *   2. Non-ASCII passthrough: bytes >= 0x80 in a UTF-8 multibyte
 *      sequence keep their original value, so a CJK payload (`u8"你好"`)
 *      still byte-equates against itself but does NOT fold any of its
 *      bytes through the ASCII map.  This is the same contract every
 *      mainstream ASCII-only iequals helper uses.
 *
 *   3. Lifetime: every owned temporary (clone(), substr(), etc.) is
 *      consumed by the helper exactly once.  Combined with the
 *      `leaks --atExit` gate further down in run_tests.sh, any
 *      double-free / leak in the new helpers would surface as a
 *      hard test failure.
 *
 *   4. Forged-handle short circuit: a `(len > 0, data == NULL)` view
 *      reaches `__neverc_string_invalid` and the helper bails out
 *      with the documented sentinel (0 / NPOS) without dereferencing
 *      the NULL pointer.
 *
 *   5. Direct `neverc_string_*_ic(...)` invocation -- the explicit
 *      prefix path users opt into when they want to keep the prelude's
 *      helper reachable from non-dotted code (e.g. callbacks, function
 *      pointers).  Verifies the dotted spelling and the prefixed
 *      spelling produce the same answer.
 */

int main(void) {
    int r = 0;

    /* ===== (1) Equality ===== */

    /* Equal under fold, not under byte equality. */
    if (!"Hello".eq_ic("hello")) r = 1;
    if (!"HELLO".eq_ic("hello")) r = 1;
    if (!"HeLLo".eq_ic("hElLo")) r = 1;
    if (!"foo".eq_ic("FOO")) r = 1;

    /* Not equal even under fold (different lengths). */
    if ("hello".eq_ic("hell")) r = 1;
    if ("hell".eq_ic("hello")) r = 1;

    /* Not equal under fold (different content). */
    if ("Hello".eq_ic("World")) r = 1;

    /* Empty / empty -> equal */
    if (!"".eq_ic("")) r = 1;
    if ("".eq_ic("a")) r = 1;
    if ("a".eq_ic("")) r = 1;

    /* Bytes that are NOT in A..Z must NOT fold.  Specifically the
       byte difference between '@' (0x40) and '`' (0x60) is the same
       0x20 the A..Z fold uses, and a buggy "subtract 0x20 unconditionally"
       implementation would equate them.  We require strict ASCII-only
       fold, so these MUST stay unequal. */
    if ("@".eq_ic("`")) r = 1;
    if ("[".eq_ic("{")) r = 1;
    if ("]".eq_ic("}")) r = 1;

    /* Digits / punctuation pass through unchanged. */
    if (!"abc 123!".eq_ic("ABC 123!")) r = 1;
    if ("abc 123!".eq_ic("abc 124!")) r = 1;

    /* ===== (2) 3-way compare ===== */

    if ("abc".compare_ic("ABC") != 0) r = 1;
    if ("abc".compare_ic("abd") != -1) r = 1;
    if ("abd".compare_ic("ABC") != 1) r = 1;
    if ("ab".compare_ic("ABCD") != -1) r = 1;
    if ("ABCD".compare_ic("ab") != 1) r = 1;
    if ("".compare_ic("hi") != -1) r = 1;
    if ("hi".compare_ic("") != 1) r = 1;
    if ("".compare_ic("") != 0) r = 1;

    /* ===== (3) find / contains ===== */

    if ("Hello, World".find_ic("world") != 7) r = 1;
    if ("Hello, World".find_ic("WORLD") != 7) r = 1;
    if ("Hello, World".find_ic("xyz") != NEVERC_STRING_NPOS) r = 1;
    if (!"Hello, World".contains_ic("world")) r = 1;
    if ("Hello, World".contains_ic("planet")) r = 1;

    /* Empty needle matches at offset 0 (std::string parity). */
    if ("Hello".find_ic("") != 0) r = 1;
    if (!"Hello".contains_ic("")) r = 1;

    /* Needle longer than haystack -> NPOS. */
    if ("Hi".find_ic("Hello") != NEVERC_STRING_NPOS) r = 1;

    /* Empty haystack with non-empty needle -> NPOS. */
    if ("".find_ic("hi") != NEVERC_STRING_NPOS) r = 1;

    /* Empty haystack with empty needle -> 0 (std::string parity). */
    if ("".find_ic("") != 0) r = 1;

    /* ===== (4) starts_with / ends_with ===== */

    if (!"Hello, World".starts_with_ic("HELLO")) r = 1;
    if (!"Hello, World".starts_with_ic("Hello")) r = 1;
    if ("Hello, World".starts_with_ic("World")) r = 1;
    if ("Hello, World".starts_with_ic("Hello, World!")) r = 1;
    if (!"Hello, World".starts_with_ic("")) r = 1;

    if (!"Hello, World".ends_with_ic("WORLD")) r = 1;
    if (!"Hello, World".ends_with_ic("World")) r = 1;
    if ("Hello, World".ends_with_ic("Hello")) r = 1;
    if ("Hello, World".ends_with_ic("xx World")) r = 1;
    if (!"Hello, World".ends_with_ic("")) r = 1;

    /* ===== (5) Non-ASCII passthrough ===== */

    /* Same UTF-8 byte payload -> equal under fold (ASCII fold leaves
       the multibyte tail alone, so byte equality wins). */
    if (!u8"你好".eq_ic(u8"你好")) r = 1;
    if (u8"你好".eq_ic(u8"再见")) r = 1;

    /* Mixed ASCII + CJK -- only ASCII halves fold, the CJK tail must
       still byte-equate. */
    if (!u8"Hello 你好".eq_ic(u8"HELLO 你好")) r = 1;
    if (u8"Hello 你好".eq_ic(u8"HELLO 再见")) r = 1;

    /* find_ic on a CJK-tail haystack still finds the ASCII-folded
       prefix at the right offset. */
    if (u8"Hello 你好".find_ic("HELLO") != 0) r = 1;

    /* ===== (6) HTTP-style header dispatch (the canonical use case) ===== */

    string ct = "Content-Type";
    if (!ct.eq_ic("content-type")) r = 1;
    if (!ct.eq_ic("CONTENT-TYPE")) r = 1;
    if (!ct.starts_with_ic("CONTENT-")) r = 1;
    if (!ct.ends_with_ic("-TYPE")) r = 1;

    string ext = "image.PNG";
    if (!ext.ends_with_ic(".png")) r = 1;
    if (!ext.contains_ic("image")) r = 1;

    /* ===== (7) Lifetime: chained dotted calls on owned temporaries ===== */

    /* substr() returns owned; eq_ic must consume both sides exactly
       once.  The leaks --atExit gate downstream pins this. */
    if (!"Hello, World".substr(0, 5).eq_ic("HELLO")) r = 1;
    if ("Hello, World".substr(0, 5).eq_ic("WORLD")) r = 1;
    if ("Hello, World".substr(7).find_ic("WORLD") != 0) r = 1;
    if (!"Hello, World".substr(0, 5).clone().starts_with_ic("HEL")) r = 1;
    if (!"Hello, World".clone().clone().eq_ic("hello, world")) r = 1;

    /* compare_ic on chained owned temporaries.
       "HELP" vs "hello" under ASCII fold: 'h'=='h', 'e'=='e', 'l'=='l',
       then 'p' (0x70) > 'l' (0x6C), so the helper returns +1 (HELP > hello).
       Pin both halves of the spectrum so the assertion catches a sign-
       flipped or saturating-to-zero bug in the helper. */
    if ("Hello".clone().compare_ic("hello") != 0) r = 1;
    if ("HELP".clone().compare_ic("hello") <= 0) r = 1;
    if ("apple".clone().compare_ic("BANANA") >= 0) r = 1;

    /* ===== (8) Direct `neverc_` prefixed call -- the escape hatch. ===== */

    {
        string a = "Accept-Encoding";
        string b = "accept-encoding";
        if (neverc_string_eq_ic(a, b) != 1) r = 1;
    }
    {
        string a = "Accept-Encoding";
        string b = "User-Agent";
        if (neverc_string_eq_ic(a, b) != 0) r = 1;
    }
    {
        string a = "abc";
        string b = "ABD";
        if (neverc_string_compare_ic(a, b) >= 0) r = 1;
    }
    {
        string s = "abcDEFghi";
        string n = "DEF";
        if (neverc_string_find_ic(s, n) != 3) r = 1;
    }
    {
        string s = "abcDEFghi";
        string n = "xyz";
        if (neverc_string_contains_ic(s, n) != 0) r = 1;
    }

    /* ===== (9) Forged handle short-circuit ===== */

    /* A `(len > 0, data == NULL)` view is rejected by
       `__neverc_string_invalid`; the helpers MUST collapse to the
       documented sentinel without dereferencing data.  Construct via
       `neverc_string_view` because string literals always have a real
       data pointer. */
    {
        string forged = neverc_string_view((const char *)0, 5);
        string real = "Hello";
        /* forged != "anything" under fold, no crash */
        if (neverc_string_eq_ic(forged, real) != 0) r = 1;
    }
    {
        string forged = neverc_string_view((const char *)0, 3);
        string real = "abc";
        /* compare_ic uses len-only fallback when invalid -- still no
           crash, returns -1/0/+1 by length comparison alone. */
        int c = neverc_string_compare_ic(forged, real);
        /* Both have non-zero len; len comparison says equal length =>
           result == 0; but since one is invalid we just need "no crash". */
        if (c < -1 || c > 1) r = 1;
    }
    {
        string forged = neverc_string_view((const char *)0, 5);
        string needle = "x";
        if (neverc_string_find_ic(forged, needle) != NEVERC_STRING_NPOS)
            r = 1;
    }

    /* ===== (10) Cross-spelling agreement ===== */

    {
        string a = "Hello";
        string b = "hello";
        int dotted = a.clone().eq_ic(b.clone());
        int prefixed = neverc_string_eq_ic(a.clone(), b.clone());
        if (dotted != prefixed) r = 1;
        if (!dotted) r = 1;
        /* Drop the originals -- the .clone() copies above were each
           consumed by their respective helper. */
    }

    extern int printf(const char *, ...);
    printf("test_neverc_string_ic: %s\n", r == 0 ? "ALL PASSED" : "FAILED");
    return r;
}
