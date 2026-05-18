// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* NeverC builtin string in kernel-context shellcode mode: search + resize.
 *
 * Compile-only guard for `-mshellcode-context=kernel`: ensures the
 * std::string-parity additions (`find_first_of` / `find_last_of` /
 * `find_first_not_of` / `find_last_not_of`, `resize`) compile, link,
 * and survive the kernel-mode pipeline (smaller arena, kernel
 * resolver gating).  The body never reaches the loader -- the test
 * only asserts that the shellcode pipeline accepts every helper.
 *
 * Index map for "kernel-string":
 *   k(0) e(1) r(2) n(3) e(4) l(5) -(6) s(7) t(8) r(9) i(10) n(11) g(12)
 */
int shellcode_entry(int seed) {
    string s = "kernel-string";

    if (s.find_first_of("-x") != 6)            /* '-' at index 6 */
        return seed + 1;
    if (s.find_first_of("zZ") != NEVERC_STRING_NPOS)
        return seed + 2;
    if (s.find_last_of("knr") != 11)           /* last 'n' at index 11 */
        return seed + 3;
    if (s.find_first_not_of("kernel") != 6)    /* '-' is the first non-{kernel} */
        return seed + 4;
    if (s.find_last_not_of("g-") != 11)        /* 'n' at 11 (12 is 'g') */
        return seed + 5;

    /* Empty-set parity. */
    if (s.find_first_of("") != NEVERC_STRING_NPOS)
        return seed + 6;
    if (s.find_first_not_of("") != 0)
        return seed + 7;
    if (s.find_last_of("") != NEVERC_STRING_NPOS)
        return seed + 8;
    if (s.find_last_not_of("") != 12)
        return seed + 9;

    /* resize + arena reuse under the tighter kernel arena (4 KB). */
    string r = "kk";
    r = r.resize(8, '*');
    if (!neverc_string_eq(r, "kk******"))
        return seed + 10;
    r = r.resize(2, '?');
    if (!neverc_string_eq(r, "kk"))
        return seed + 11;
    r = r.resize(0, '!');
    if (!neverc_string_empty(r))
        return seed + 12;

    for (int i = 0; i < 32; i++) {
        string loop = "kk";
        loop = loop.resize(6, '+');
        if (!neverc_string_eq(loop, "kk++++"))
            return seed + 13;
    }

    return seed;
}
