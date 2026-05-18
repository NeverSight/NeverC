// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* NeverC builtin string boundary / degenerate-input coverage in ring-0
 * shellcode mode.
 *
 * Compile-only mirror of `test_shellcode_neverc_string_edge.c`: every
 * boundary path the prelude takes (clamp / NULL / empty / npos
 * short-circuit) must lower into the same StringRuntimePass-rewritten
 * arena on the smaller 4 KB kernel budget.  If a prelude branch grew a
 * fresh malloc/free dependency, KernelImportPass would surface it as
 * an unresolved kernel resolver call.
 *
 * The body never reaches the loader -- the test only asserts that the
 * shellcode pipeline accepts every helper.
 */
int shellcode_entry(int seed) {
    /* substr / erase clamp. */
    {
        string s = "abcdef" + "";
        if (!neverc_string_eq(s.substr(0, NEVERC_STRING_NPOS), "abcdef"))
            return seed + 1;
    }
    {
        string s = "abcdef" + "";
        if (!neverc_string_empty(s.substr(100, 5)))
            return seed + 2;
    }
    {
        string s = "abcdef" + "";
        if (!neverc_string_eq(s.erase(2, NEVERC_STRING_NPOS), "ab"))
            return seed + 3;
    }

    /* find / rfind empty needle. */
    {
        string s = "abcdef" + "";
        if (s.find("") != 0)
            return seed + 4;
        if (s.find("", 3) != 3)
            return seed + 5;
        if (s.rfind("") != 6)
            return seed + 6;
    }

    /* find_*_of empty-set parity. */
    {
        string s = "abcdef" + "";
        if (s.find_first_of("") != NEVERC_STRING_NPOS)
            return seed + 7;
        if (s.find_first_not_of("") != 0)
            return seed + 8;
        if (s.find_last_not_of("") != 5)
            return seed + 9;
    }

    /* repeat / resize / replace / insert clamps. */
    {
        string s = "abc" + "";
        if (!neverc_string_empty(s.repeat(0)))
            return seed + 10;
    }
    {
        if (!neverc_string_empty(neverc_string_repeat("", 5)))
            return seed + 11;
    }
    {
        string s = "abcd" + "";
        if (!neverc_string_empty(s.resize(0, '!')))
            return seed + 12;
    }
    {
        string s = "abc" + "";
        s = s.replace(0, NEVERC_STRING_NPOS, "X");
        if (!neverc_string_eq(s, "X"))
            return seed + 13;
    }
    {
        string s = "abcd" + "";
        s = s.insert(100, "X");
        if (!neverc_string_eq(s, "abcdX"))
            return seed + 14;
    }

    /* compare boundary. */
    {
        if (neverc_string_compare("", "") != 0)
            return seed + 15;
        if (neverc_string_compare("x", "") <= 0)
            return seed + 16;
    }
    {
        string s = "abcdef" + "";
        if (s.compare(0, NEVERC_STRING_NPOS, "abcdef") != 0)
            return seed + 17;
    }

    /* int / uint conversion edges. */
    {
        if (!neverc_string_eq(neverc_string_from_int(0), "0"))
            return seed + 18;
        if (!neverc_string_eq(neverc_string_from_int(-1), "-1"))
            return seed + 19;
        if (neverc_string_to_int("") != 0)
            return seed + 20;
        if (neverc_string_to_int("12abc") != 12)
            return seed + 21;
        if (neverc_string_to_uint("12abc") != 12)
            return seed + 22;
    }

    /* Factory NULL / zero-length safety. */
    {
        string nullc = neverc_string_from_cstr((const char *)0);
        if (!neverc_string_empty(nullc))
            return seed + 23;
    }
    {
        string nullv = neverc_string_view((const char *)0, 0);
        if (!neverc_string_empty(nullv))
            return seed + 24;
    }

    /* copy(NULL, n, pos) probe shape. */
    {
        if (neverc_string_copy_from("abcdef", (char *)0, 4, 1) != 4)
            return seed + 25;
        if (neverc_string_copy_from("abc", (char *)0, 100, 1) != 2)
            return seed + 26;
    }

    /* trim degenerate inputs. */
    {
        if (!neverc_string_empty(neverc_string_trim("")))
            return seed + 27;
        if (!neverc_string_empty(neverc_string_ltrim("    ")))
            return seed + 28;
        if (!neverc_string_eq(neverc_string_trim("nopad"), "nopad"))
            return seed + 29;
    }

    /* clone of empty owned. */
    {
        string e = neverc_string_clear("data");
        string c = neverc_string_clone(e);
        if (!neverc_string_empty(c))
            return seed + 30;
    }

    /* push_back / pop_back at boundary. */
    {
        string s = neverc_string_clear("data");
        s = s.push_back('A');
        if (!neverc_string_eq(s, "A"))
            return seed + 31;
        s = s.pop_back();
        if (!neverc_string_empty(s))
            return seed + 32;
        s = s.pop_back();
        if (!neverc_string_empty(s))
            return seed + 33;
    }

    return seed;
}
