// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* NeverC builtin string in shellcode mode: search + resize coverage.
 *
 * Targets the std::string-parity additions (`find_first_of` /
 * `find_last_of` / `find_first_not_of` / `find_last_not_of`, `resize`)
 * so the shellcode prelude, arena, and Roster wiring stay intact
 * across builds.  The loader runs `main(a, b)` and uses the return
 * code as the exit status; 0 means every assertion passed.
 */
int main(int a, int b) {
    (void)a;
    (void)b;

    string s = "shellcode";

    /* find_first_of / find_last_of */
    if (s.find_first_of("lc") != 3)        /* 'l' at index 3 */
        return 1;
    if (s.find_first_of("xyz") != NEVERC_STRING_NPOS)
        return 2;
    if (s.find_last_of("lc") != 5)         /* 'c' at index 5 */
        return 3;
    if (s.find_last_of("z") != NEVERC_STRING_NPOS)
        return 4;

    /* find_first_not_of / find_last_not_of */
    if (s.find_first_not_of("she") != 3)   /* 'l' at index 3 */
        return 5;
    if (s.find_first_not_of("shellcode") != NEVERC_STRING_NPOS)
        return 6;
    if (s.find_last_not_of("oe") != 7)     /* 'd' at index 7 */
        return 7;
    if (s.find_last_not_of("shellcode") != NEVERC_STRING_NPOS)
        return 8;

    /* empty-set semantics line up with std::string. */
    if (s.find_first_of("") != NEVERC_STRING_NPOS)
        return 9;
    if (s.find_first_not_of("") != 0)
        return 10;
    if (s.find_last_of("") != NEVERC_STRING_NPOS)
        return 11;
    if (s.find_last_not_of("") != 8)       /* last index in "shellcode" */
        return 12;

    /* Free-standing call form hammered in a loop so the arena must
       reuse its free-list rather than grow forever. */
    for (int i = 0; i < 64; i++) {
        if (neverc_string_find_first_of("abc xyz abc", "z") != 6)
            return 13;
        if (neverc_string_find_last_of("abc xyz abc", "a") != 8)
            return 14;
        if (neverc_string_find_first_not_of("abc xyz abc", "abc ") != 4)
            return 15;
        if (neverc_string_find_last_not_of("abc xyz abc", "abc") != 7)
            return 16;
    }

    /* resize: extend with fill, shrink, then collapse to empty. */
    string r = "ab";
    r = r.resize(5, '_');
    if (!neverc_string_eq(r, "ab___"))
        return 17;
    if (r.size() != 5)
        return 18;
    r = r.resize(2, '!');
    if (!neverc_string_eq(r, "ab"))
        return 19;
    if (r.size() != 2)
        return 20;
    r = r.resize(0, ' ');
    if (!neverc_string_empty(r))
        return 21;

    /* Growth from a freshly-owned string (`+` produces a new owned
       buffer), then resize beyond the original capacity.  Catches any
       regression where resize accidentally aliases the input pointer. */
    string g = "x" + "y";
    g = g.resize(8, '*');
    if (!neverc_string_eq(g, "xy******"))
        return 22;
    g = g.resize(1, '?');
    if (!neverc_string_eq(g, "x"))
        return 23;

    /* Pressure test the kernel-shaped path on the larger user arena:
       repeated resize + free should reuse arena blocks instead of
       running out of room. */
    for (int i = 0; i < 96; i++) {
        string loop = "lo";
        loop = loop.resize(6, '+');
        if (!neverc_string_eq(loop, "lo++++"))
            return 24;
    }

    return 0;
}
