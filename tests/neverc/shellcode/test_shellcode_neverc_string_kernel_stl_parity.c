// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* NeverC builtin string std::string-parity tour, ring-0 mirror.
 *
 * Compile-only guard for `-mshellcode-context=kernel`: every helper the
 * user-mode `test_shellcode_neverc_string_stl_parity.c` exercises must
 * also lower into the smaller kernel arena (4 KB) without leaving a
 * libc / mem* / heap extern that KernelImportPass would route through
 * `__neverc_kern_resolve`.  The body is a near-clone of the user-mode
 * tour, scaled down so every loop fits the tighter arena, and trades
 * loader-driven exit-code asserts for `seed + N` returns the kernel
 * harness can sanity-check separately.
 *
 * The "Why this duplicates the user test" answer is the same as every
 * other `_kernel_<tag>.c` file in this suite: the user-mode test uses
 * `int main(int a, int b)` and runs through the JIT loader, while the
 * kernel-mode test uses `int shellcode_entry(int seed)` and is driven
 * exclusively by `-mshellcode-context=kernel` codegen.  The two are
 * not mergable into one file because the entry-symbol contract (and
 * the StringRuntimePass arena size) differ across the two pipelines.
 */

static __SIZE_TYPE__ count_chars(string s, char ch) {
    __SIZE_TYPE__ n = 0;
    for (__SIZE_TYPE__ i = 0; i < s.len; ++i)
        if (s.data[i] == ch)
            ++n;
    return n;
}

int shellcode_entry(int seed) {
    /* construction */
    {
        /* See user-mode mirror: `string s = "literal"` is OWNED at
           function scope (Sema's retain pass clones the literal
           bytes).  The assertion shape mirrors the user-mode test so
           the kernel path enforces the same invariant. */
        string owned_from_literal = "ring0";
        string owned = "ring0" + " kernel";
        if (!neverc_string_eq(owned_from_literal, "ring0"))
            return seed + 1;
        if (!neverc_string_eq(owned, "ring0 kernel"))
            return seed + 2;
        if (owned_from_literal.cap != owned_from_literal.len + 1)
            return seed + 3;
        if (owned.cap == 0)
            return seed + 4;

        string from_cstr = neverc_string_from_cstr("kn");
        if (!neverc_string_eq(from_cstr, "kn"))
            return seed + 5;
        string from_char = neverc_string_from_char('!');
        if (!neverc_string_eq(from_char, "!"))
            return seed + 6;
        string view = neverc_string_view("alphabet", 5);
        if (!neverc_string_eq(view, "alpha"))
            return seed + 7;
        if (view.cap != 0)
            return seed + 100;
        string fill = neverc_string_repeat(neverc_string_from_char('z'), 3);
        if (!neverc_string_eq(fill, "zzz"))
            return seed + 8;
    }

    /* sizing + element access */
    {
        string s = "head" + "tail";
        if (s.size() != 8 || s.length() != 8 || s.empty())
            return seed + 9;
        if (s.front() != 'h' || s.back() != 'l')
            return seed + 10;
        if (s.at(0) != 'h' || s.at(99) != 0)
            return seed + 11;
        if (count_chars(s, 'l') != 1)
            return seed + 12;
    }

    /* comparison + (pos, count, other) compare */
    {
        string a = "alpha" + "";
        if (a.compare("alpha") != 0)
            return seed + 13;
        if (a.compare(0, 3, "alp") != 0)
            return seed + 14;
        if (!(a < "beta"))
            return seed + 15;
    }

    /* search: covers default + (needle, pos) overload + find_*_of +
       single-character overloads dispatched through the type-aware
       table (`s.find('x')` etc.) */
    {
        string h = "alpha-beta" + "-gamma";
        if (h.find("beta") != 6)
            return seed + 16;
        if (h.find("a", 1) != 4)
            return seed + 17;
        if (h.rfind("a", 5) != 4)
            return seed + 18;
        if (h.find_first_of("xyz-") != 5)
            return seed + 19;
        if (h.find_last_of("xyz-") != 10)
            return seed + 20;
        /* find_last_of("a-", 7) walks 7 -> 0; first hit at h[5]='-'. */
        if (h.find_last_of("a-", 7) != 5)
            return seed + 21;
        /* find_last_not_of("amg-") rightmost char outside is h[8]='t'. */
        if (h.find_last_not_of("amg-") != 8)
            return seed + 22;
        if (!h.contains("beta"))
            return seed + 23;
        if (!h.starts_with("alpha"))
            return seed + 24;
        if (!h.ends_with("gamma"))
            return seed + 25;

        /* Single-character overloads -- char literal type is `int`, so
           the type-aware overload table reaches the `_char` helpers. */
        if (h.find('b') != 6)
            return seed + 26;
        if (h.rfind('a') != 15)
            return seed + 27;
        if (h.find('a', 1) != 4)
            return seed + 28;
        if (h.find_first_of('-') != 5)
            return seed + 29;
        if (h.find_last_of('a', 7) != 4)
            return seed + 30;
        if (!h.contains('-'))
            return seed + 31;
        if (h.contains('z'))
            return seed + 32;
        if (!h.starts_with('a'))
            return seed + 33;
        if (!h.ends_with('a'))
            return seed + 34;
        /* Negated single-char overloads.  h[0]='a' skip, h[1]='l'
           hits at 1 for `find_first_not_of('a')`; h[15]='a' skip,
           h[14]='m' hits at 14 for `find_last_not_of('a')`. */
        if (h.find_first_not_of('a') != 1)
            return seed + 35;
        if (h.find_last_not_of('a') != 14)
            return seed + 36;
    }

    /* substr / copy + (out, count, pos) overload */
    {
        string s = "head" + "tail";
        if (!neverc_string_eq(s.substr(4), "tail"))
            return seed + 37;
        if (!neverc_string_empty(s.substr(99)))
            return seed + 38;
        char buf[8];
        for (int i = 0; i < 8; ++i)
            buf[i] = '?';
        string src = "01234567" + "";
        __SIZE_TYPE__ n = src.copy(buf, 4);
        if (n != 4 || buf[0] != '0' || buf[3] != '3')
            return seed + 39;
        for (int i = 0; i < 8; ++i)
            buf[i] = '?';
        string src2 = "01234567" + "";
        n = src2.copy(buf, 3, 5);
        if (n != 3 || buf[0] != '5' || buf[2] != '7')
            return seed + 40;
    }

    /* mutation: append / += / push_back / pop_back / insert /
       insert(pos, count, ch) / erase / replace / resize / char-fill
       overloads of append / replace / assign */
    {
        string s = "kn" + "";
        s = s.append(" arena");
        s += "!";
        if (!neverc_string_eq(s, "kn arena!"))
            return seed + 41;
        s = s.push_back('?');
        s = s.pop_back();
        if (!neverc_string_eq(s, "kn arena!"))
            return seed + 42;
        /* (pos, count, ch) insert -> neverc_string_insert_char. */
        s = s.insert(2, 2, '*');
        if (!neverc_string_eq(s, "kn** arena!"))
            return seed + 43;
        s = s.erase(2, 2);
        if (!neverc_string_eq(s, "kn arena!"))
            return seed + 44;
        s = s.replace(3, 5, "zone");
        if (!neverc_string_eq(s, "kn zone!"))
            return seed + 45;
        /* (pos, count, count2, ch) replace -> neverc_string_replace_char. */
        s = s.replace(3, 4, 3, 'X');
        if (!neverc_string_eq(s, "kn XXX!"))
            return seed + 46;
        s = s.resize(2);
        if (!neverc_string_eq(s, "kn"))
            return seed + 47;
        s = s.resize(4, '?');
        if (!neverc_string_eq(s, "kn??"))
            return seed + 48;
        /* (count, ch) append -> neverc_string_append_char. */
        s = s.append(2, '!');
        if (!neverc_string_eq(s, "kn??!!"))
            return seed + 49;
        s = neverc_string_clear(s);
        if (!s.empty())
            return seed + 50;

        string left = "k" + "n";
        string right = "n" + "k";
        left.swap(right);
        if (!neverc_string_eq(left, "nk") || !neverc_string_eq(right, "kn"))
            return seed + 51;

        string assigned;
        assigned.assign("kn-arena");
        if (!neverc_string_eq(assigned, "kn-arena"))
            return seed + 52;
        /* (count, ch) assign -> neverc_string_assign_char (Receiver kind). */
        assigned.assign(4, 'k');
        if (!neverc_string_eq(assigned, "kkkk"))
            return seed + 53;
    }

    /* capacity (pointer-receiver dispatch) */
    {
        string s = "kn" + "kn";
        if (s.capacity() < s.len)
            return seed + 54;
        s = s.reserve(48);
        if (s.capacity() < 48)
            return seed + 55;
        if (s.capacity() != s.cap - 1)
            return seed + 56;
        s = s.shrink_to_fit();
        if (s.capacity() != s.len)
            return seed + 57;
    }

    /* utility / conversions */
    {
        if (!neverc_string_eq("KeRnEl".to_lower(), "kernel"))
            return seed + 58;
        if (!neverc_string_eq("KeRnEl".to_upper(), "KERNEL"))
            return seed + 59;
        if (!neverc_string_eq(" \t hi \n".trim(), "hi"))
            return seed + 60;
        if (!neverc_string_eq(neverc_string_repeat("kn", 3), "knknkn"))
            return seed + 61;
        if (neverc_string_to_int("-64") != -64)
            return seed + 62;
    }

    /* extended utilities: hash / reverse / count */
    {
        string a = "ring0" + "";
        string b = "ring0" + "";
        if (a.hash() != b.hash())
            return seed + 63;
        if (("" + "").hash() != 0xcbf29ce484222325ULL)
            return seed + 64;
        if (!neverc_string_eq(("knkn" + "").reverse(), "nknk"))
            return seed + 65;
        if (("kkkk" + "").count('k') != 4)
            return seed + 66;
        if (("knknkn" + "").count("kn") != 3)
            return seed + 67;
    }

    /* formatting / padding / replace_all / base conversion */
    {
        if (!neverc_string_eq(("kn" + "").pad_left(5, '0'), "000kn"))
            return seed + 68;
        if (!neverc_string_eq(("kn" + "").pad_right(5, '.'), "kn..."))
            return seed + 69;
        if (!neverc_string_eq(("a-b-c" + "").replace_all("-", "::"), "a::b::c"))
            return seed + 70;
        if (!neverc_string_eq(neverc_string_from_uint_base(0xfeULL, 16), "fe"))
            return seed + 71;
        if (!neverc_string_eq(neverc_string_from_int_base(-15, 16), "-f"))
            return seed + 72;
    }

    /* memory safety: tighter loop count for the 4 KB arena */
    {
        for (int i = 0; i < 32; ++i) {
            string owned = "k" + "n";
            owned = owned;
            if (!neverc_string_eq(owned, "kn"))
                return seed + 73;
        }
        string s = "alpha" + "beta";
        s = neverc_string_view(s.data + 1, 4);
        if (!neverc_string_eq(s, "lpha"))
            return seed + 74;

        string chain =
            ("  KeRnEl  " + "").trim().to_lower().replace(0, 6, "ARENA");
        if (!neverc_string_eq(chain, "ARENA"))
            return seed + 75;
    }

    /* forged / oversized handles short-circuit safely */
    {
        string forged = {(const char *)0, 1, 0};
        if (forged.front() != 0)
            return seed + 76;
        string oversized = {(const char *)0, NEVERC_STRING_NPOS, 0};
        if (oversized.front() != 0)
            return seed + 77;
    }

    return seed;
}
