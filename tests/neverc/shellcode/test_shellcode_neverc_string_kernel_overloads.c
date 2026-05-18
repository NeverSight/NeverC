// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* NeverC builtin string std::string-parity arity overloads in ring-0
 * shellcode mode.
 *
 * Compile-only guard for `-mshellcode-context=kernel`: every row in
 * `BuiltinStringMethodOverloads.def` plus the SizeAllOnes default-arg
 * rows in `BuiltinStringMethodDefaults.def` must dispatch to the same
 * runtime helper as user mode.  If the dispatcher swaps an overload
 * name or drops the default-argument synthesis, KernelImportPass would
 * surface a previously-claimed extern as an unresolved kernel resolver
 * call -- so the compile failure here would name the wrong helper.
 */
int shellcode_entry(int seed) {
    string s = "abc xyz abc" + "";

    /* find / find(needle, pos) */
    if (s.find("abc") != 0)
        return seed + 1;
    if (s.find("abc", 3) != 8)
        return seed + 2;

    /* rfind / rfind(needle, pos) */
    if (s.rfind("abc") != 8)
        return seed + 3;
    if (s.rfind("abc", 4) != 0)
        return seed + 4;

    /* find_first_of / find_first_of(chars, pos) */
    if (s.find_first_of("xyz") != 4)
        return seed + 5;
    if (s.find_first_of("xyz", 5) != 5)
        return seed + 6;

    /* find_last_of / find_last_of(chars, pos) */
    if (s.find_last_of("xyz") != 6)
        return seed + 7;
    if (s.find_last_of("xyz", 4) != 4)
        return seed + 8;

    /* find_first_not_of / find_first_not_of(chars, pos) */
    if (s.find_first_not_of("abc ") != 4)
        return seed + 9;
    if (s.find_first_not_of("xyz", 4) != 7)
        return seed + 10;

    /* find_last_not_of / find_last_not_of(chars, pos) */
    if (s.find_last_not_of("abc") != 7)
        return seed + 11;
    if (s.find_last_not_of(" ", 7) != 6)
        return seed + 12;

    /* substr(pos) / substr(pos, len) -- SizeAllOnes default. */
    if (!neverc_string_eq(s.substr(8), "abc"))
        return seed + 13;
    if (!neverc_string_eq(s.substr(0, 3), "abc"))
        return seed + 14;

    /* erase(pos) / erase(pos, count) -- SizeAllOnes default. */
    {
        string e = "abcdef" + "";
        e = e.erase(2);
        if (!neverc_string_eq(e, "ab"))
            return seed + 15;
    }
    {
        string e = "abcdef" + "";
        e = e.erase(2, 2);
        if (!neverc_string_eq(e, "abef"))
            return seed + 16;
    }

    /* compare(other) / compare(pos, count, other) */
    if (s.compare("abc xyz abc") != 0)
        return seed + 17;
    if (s.compare(0, 3, "abc") != 0)
        return seed + 18;

    /* insert(pos, str) / insert(pos, count, ch) */
    {
        string ins = "abc" + "";
        ins = ins.insert(1, "X");
        if (!neverc_string_eq(ins, "aXbc"))
            return seed + 19;
    }
    {
        string ins = "abc" + "";
        ins = ins.insert(1, 3, '!');
        if (!neverc_string_eq(ins, "a!!!bc"))
            return seed + 20;
    }

    /* resize(n) / resize(n, fill): the 1-arg form synthesises `'\0'`
       through the CharZero default-arg row.  Compile-only mirror of
       the user-mode test; the dispatcher must not desync between
       contexts or KernelImportPass would surface `neverc_string_resize` as
       unresolved when the default argument silently dropped. */
    {
        string r = "ab" + "";
        r = r.resize(4);
        if (r.size() != 4)
            return seed + 100;
        if (r.front() != 'a' || r.at(2) != 0 || r.at(3) != 0)
            return seed + 101;
    }
    {
        string r = "abc" + "";
        r = r.resize(5, '?');
        if (!neverc_string_eq(r, "abc??"))
            return seed + 102;
    }

    /* copy(buf, count) / copy(buf, count, pos) */
    {
        char buf[16] = {0};
        if (s.copy(buf, 3) != 3)
            return seed + 21;
    }
    {
        char buf[16] = {0};
        if (s.copy(buf, 3, 4) != 3)
            return seed + 22;
    }

    /* Free-standing call forms must dispatch identically. */
    if (neverc_string_find_from("abc xyz abc", "abc", 1) != 8)
        return seed + 23;
    if (neverc_string_rfind_to("abc xyz abc", "abc", 4) != 0)
        return seed + 24;
    if (neverc_string_compare_substr("abc xyz", 0, 3, "abc") != 0)
        return seed + 25;
    if (!neverc_string_eq(neverc_string_insert_char("abc", 1, 2, '?'), "a??bc"))
        return seed + 26;

    return seed;
}
