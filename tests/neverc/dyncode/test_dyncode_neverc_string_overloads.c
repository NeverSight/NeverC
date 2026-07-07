// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* NeverC builtin string std::string-parity arity overloads.
 *
 * Hits every row in `BuiltinStringMethodOverloads.def` plus the
 * default-argument rows in `BuiltinStringMethodDefaults.def` so a
 * dispatcher regression that re-routes one of the arity-aware paths
 * is caught here.  Also exercises the free-standing helper spellings
 * that share the runtime backend (`neverc_string_find_from`, `neverc_string_rfind_to`,
 * `neverc_string_compare_substr`, `neverc_string_copy_from`, `neverc_string_insert_char`)
 * to keep the public API surface in lockstep with the dotted form.
 *
 * Loader runs `main(a, b)` and uses the return code as exit status;
 * 0 means every assertion passed.
 */
int main(int a, int b) {
    (void)a;
    (void)b;

    string s = "abc xyz abc" + "";

    /* find / find(needle, pos) */
    if (s.find("abc") != 0)
        return 1;
    if (s.find("abc", 3) != 8)
        return 2;
    if (s.find("missing") != NEVERC_STRING_NPOS)
        return 3;
    if (s.find("missing", 100) != NEVERC_STRING_NPOS)
        return 4;

    /* rfind / rfind(needle, pos) */
    if (s.rfind("abc") != 8)
        return 5;
    if (s.rfind("abc", 4) != 0)
        return 6;
    if (s.rfind("nope") != NEVERC_STRING_NPOS)
        return 7;

    /* find_first_of / find_first_of(chars, pos) */
    if (s.find_first_of("xyz") != 4)
        return 8;
    if (s.find_first_of("xyz", 5) != 5)
        return 9;
    if (s.find_first_of("?") != NEVERC_STRING_NPOS)
        return 10;

    /* find_last_of / find_last_of(chars, pos) */
    if (s.find_last_of("xyz") != 6)
        return 11;
    if (s.find_last_of("xyz", 4) != 4)
        return 12;
    if (s.find_last_of("?") != NEVERC_STRING_NPOS)
        return 13;

    /* find_first_not_of / find_first_not_of(chars, pos) */
    if (s.find_first_not_of("abc ") != 4)
        return 14;
    if (s.find_first_not_of("xyz", 4) != 7)
        return 15;

    /* find_last_not_of / find_last_not_of(chars, pos) */
    if (s.find_last_not_of("abc") != 7)
        return 16;
    if (s.find_last_not_of("abc xyz") != NEVERC_STRING_NPOS)
        return 17;
    if (s.find_last_not_of(" ", 7) != 6)
        return 18;

    /* substr(pos) / substr(pos, len): the 1-arg form goes through the
       SizeAllOnes default-arg row in `BuiltinStringMethodDefaults.def`. */
    if (!neverc_string_eq(s.substr(8), "abc"))
        return 19;
    if (!neverc_string_eq(s.substr(0, 3), "abc"))
        return 20;
    if (!neverc_string_eq(s.substr(3, 1), " "))
        return 21;

    /* erase(pos) / erase(pos, count): same SizeAllOnes default path. */
    {
        string e = "abcdef" + "";
        e = e.erase(2);
        if (!neverc_string_eq(e, "ab"))
            return 22;
    }
    {
        string e = "abcdef" + "";
        e = e.erase(2, 2);
        if (!neverc_string_eq(e, "abef"))
            return 23;
    }
    {
        string e = "abcdef" + "";
        e = e.erase(0, 0);
        if (!neverc_string_eq(e, "abcdef"))
            return 24;
    }

    /* compare(other) / compare(pos, count, other): the 3-arg form
       routes to `neverc_string_compare_substr`. */
    if (s.compare("abc xyz abc") != 0)
        return 25;
    if (s.compare("abc xyz abd") >= 0)
        return 26;
    if (s.compare(0, 3, "abc") != 0)
        return 27;
    if (s.compare(4, 3, "xyz") != 0)
        return 28;
    if (s.compare(8, 3, "abd") >= 0)
        return 29;

    /* insert(pos, str) / insert(pos, count, ch): the 3-arg form
       routes to `neverc_string_insert_char`. */
    {
        string ins = "abc" + "";
        ins = ins.insert(1, "X");
        if (!neverc_string_eq(ins, "aXbc"))
            return 30;
    }
    {
        string ins = "abc" + "";
        ins = ins.insert(1, 3, '!');
        if (!neverc_string_eq(ins, "a!!!bc"))
            return 31;
    }
    {
        string ins = "abc" + "";
        ins = ins.insert(0, 1, '*');
        if (!neverc_string_eq(ins, "*abc"))
            return 32;
    }
    {
        string ins = "abc" + "";
        ins = ins.insert(3, 2, '_');
        if (!neverc_string_eq(ins, "abc__"))
            return 33;
    }

    /* resize(n) / resize(n, fill): the 1-arg form goes through the
       CharZero default-arg row in `BuiltinStringMethodDefaults.def`,
       which appends `'\0'` so the runtime helper sees the full
       `(s, n, fill)` signature.  Matches `std::string::resize(n)`'s
       value-initialised-`char` default fill (NUL bytes). */
    {
        string r = "ab" + "";
        r = r.resize(5);
        /* size grew to 5; the 3 trailing bytes are NUL fill. */
        if (r.size() != 5)
            return 100;
        if (r.front() != 'a' || r.at(1) != 'b')
            return 101;
        if (r.at(2) != 0 || r.at(3) != 0 || r.at(4) != 0)
            return 102;
    }
    {
        string r = "abcdef" + "";
        r = r.resize(3);
        /* shrinking does not look at fill; truncate to "abc". */
        if (!neverc_string_eq(r, "abc"))
            return 103;
    }
    {
        string r = "abc" + "";
        r = r.resize(5, '?');
        /* explicit fill stays untouched by the default-arg path. */
        if (!neverc_string_eq(r, "abc??"))
            return 104;
    }

    /* copy(buf, count) / copy(buf, count, pos): the 3-arg form
       routes to `neverc_string_copy_from`. */
    {
        char buf[16] = {0};
        if (s.copy(buf, 3) != 3)
            return 34;
        if (buf[0] != 'a' || buf[1] != 'b' || buf[2] != 'c')
            return 35;
    }
    {
        char buf[16] = {0};
        if (s.copy(buf, 3, 4) != 3)
            return 36;
        if (buf[0] != 'x' || buf[1] != 'y' || buf[2] != 'z')
            return 37;
    }

    /* Free-standing call forms must still dispatch identically. */
    if (neverc_string_find_from("abc xyz abc", "abc", 1) != 8)
        return 38;
    if (neverc_string_rfind_to("abc xyz abc", "abc", 4) != 0)
        return 39;
    if (neverc_string_find_first_of_from("abc xyz abc", "xyz", 5) != 5)
        return 40;
    if (neverc_string_find_last_of_to("abc xyz abc", "xyz", 4) != 4)
        return 41;
    if (neverc_string_find_first_not_of_from("abc xyz abc", "abc", 0) != 3)
        return 42;
    if (neverc_string_find_last_not_of_to("abc xyz abc", "abc", 7) != 7)
        return 43;
    if (neverc_string_compare_substr("abc xyz", 0, 3, "abc") != 0)
        return 44;
    if (neverc_string_copy_from("abcdefgh", (char *)0, 4, 2) != 4)
        return 45;
    if (!neverc_string_eq(neverc_string_insert_char("abc", 1, 2, '?'), "a??bc"))
        return 46;

    /* Pressure: arity-aware paths in a tight loop must reuse arena. */
    for (int i = 0; i < 64; ++i) {
        if ("hammer".substr(0, 3) != "ham")
            return 47;
        if ("hammer".find("mer", 1) != 3)
            return 48;
        if ("aabb".compare(2, 2, "bb") != 0)
            return 49;
    }

    return 0;
}
