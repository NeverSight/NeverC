// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* NeverC builtin string in shellcode mode.
 *
 * Exercises owned string operations (`+`, `+=`, assignment copy, `substr`),
 * std::string-like mutation helpers, and utility helpers so the shellcode
 * pipeline must replace the builtin runtime's default malloc/free path with
 * its local arena instead of leaving heap externs behind.
 */
int main(int a, int b) {
    (void)a;
    (void)b;

    string prefix = "shell";
    string joined = prefix + "code";
    if (!neverc_string_eq(joined, "shellcode"))
        return 1;
    if (joined != "shellcode" || joined == "shell")
        return 2;

    string tail = joined.substr(5);
    if (!neverc_string_eq(tail, "code"))
        return 3;

    string grow = "a";
    grow += "b";
    grow += grow;
    if (!neverc_string_eq(grow, "abab"))
        return 4;

    string copied_by_init = grow;
    if (!neverc_string_eq(copied_by_init, "abab"))
        return 5;
    if (copied_by_init.data == grow.data)
        return 13;
    if (!neverc_string_eq(grow, "abab"))
        return 6;

    string copied_by_assign;
    copied_by_assign = grow;
    if (!neverc_string_eq(copied_by_assign, "abab"))
        return 7;
    if (copied_by_assign.data == grow.data)
        return 14;
    if (!neverc_string_eq(grow, "abab"))
        return 8;

    string cloned = neverc_string_clone(grow);
    if (!neverc_string_eq(cloned, "abab"))
        return 9;
    if (cloned.data == grow.data)
        return 15;
    string dotted_clone = grow.clone();
    if (!neverc_string_eq(dotted_clone, "abab"))
        return 10;
    if (dotted_clone.data == grow.data)
        return 16;

    const char *joined_cstr = neverc_string_cstr(joined);
    if (joined_cstr[0] != 's' || joined_cstr[9] != 0)
        return 11;
    const char *joined_data = joined.data();
    if (joined_data[4] != 'l' || joined_data[9] != 0)
        return 23;
    for (int i = 0; i < 2048; ++i) {
        const char *literal_cstr = neverc_string_cstr("literal cstr");
        if (literal_cstr[8] != 'c')
            return 26;
    }
    if (joined.find("code") != 5 || !joined.ends_with("code"))
        return 12;
    if (joined.rfind("c") != 5 || joined.rfind("missing") != NEVERC_STRING_NPOS)
        return 27;
    if (neverc_string_rfind("abcabc", "abc") != 3)
        return 28;
    if (joined.size() != 9 || joined.length() != 9 || joined.empty())
        return 20;
    if (!joined.starts_with("shell") || !joined.contains("llco") ||
        joined.compare("shellcode") != 0)
        return 21;
    if (joined.front() != 's' || joined.back() != 'e' || joined.at(5) != 'c')
        return 22;

    string mut = "shell";
    mut = mut.append("code");
    mut = mut.push_back('!');
    if (!neverc_string_eq(mut, "shellcode!"))
        return 29;
    mut = mut.pop_back();
    mut = mut.insert(5, "-");
    if (!neverc_string_eq(mut, "shell-code"))
        return 30;
    mut = mut.erase(5, 1);
    mut = mut.replace(5, 4, "CODE");
    if (!neverc_string_eq(mut, "shellCODE"))
        return 31;
    if (!neverc_string_empty(neverc_string_clear(mut)))
        return 32;

    if (!neverc_string_eq(neverc_string_repeat("ha", 3), "hahaha"))
        return 33;
    if (!neverc_string_eq("MiXeD".to_lower(), "mixed"))
        return 34;
    if (!neverc_string_eq("MiXeD".to_upper(), "MIXED"))
        return 35;
    if (!neverc_string_eq(" \t trim me \n".trim(), "trim me"))
        return 36;
    if (!neverc_string_eq(" \t left".ltrim(), "left"))
        return 37;
    if (!neverc_string_eq("right \r\n".rtrim(), "right"))
        return 38;
    if (!neverc_string_eq(neverc_string_from_cstr("from cstr"), "from cstr"))
        return 39;
    if (!neverc_string_eq(neverc_string_from_char('x'), "x"))
        return 40;
    if (!neverc_string_eq(neverc_string_from_int(-42), "-42"))
        return 41;
    if (!neverc_string_eq(neverc_string_from_uint(123), "123"))
        return 42;
    if (neverc_string_to_int("-42") != -42)
        return 43;
    if (neverc_string_to_uint("+123") != 123)
        return 44;

    string left = "left";
    string right = "right";
    neverc_string_swap(&left, &right);
    if (!neverc_string_eq(left, "right") || !neverc_string_eq(right, "left"))
        return 45;

    {
        string forged;
        forged.data = (const char *)1;
        forged.len = 1;
        forged.cap = 1;
        if (forged.len != 1)
            return 19;
    }
    if (!neverc_string_eq(joined, "shellcode"))
        return 18;

    const char *freed_block = 0;
    {
        string first = "reuse " + "block";
        freed_block = first.data;
    }
    string second = "reuse " + "block";
    if (second.data != freed_block)
        return 24;
    if (!neverc_string_eq(second, "reuse block"))
        return 25;

    for (int i = 0; i < 192; ++i) {
        string loop = "arena";
        loop += " reuse";
        if (!neverc_string_eq(loop, "arena reuse"))
            return 17;
    }

    return 0;
}
