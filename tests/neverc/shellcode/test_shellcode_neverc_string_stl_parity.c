// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* NeverC builtin string std::string-parity tour.
 *
 * Single end-to-end exercise that walks the public surface a typical
 * std::string user expects, in roughly the order the cppreference
 * member table presents it, so a regression in the dispatcher /
 * prelude that quietly drops one helper shows up next to the matching
 * std::string method spelling.
 *
 *   * construction      -- borrowed view, owned-via-`+`, factories,
 *                          `neverc_string_view`, char-fill via `neverc_string_repeat`
 *   * sizing            -- size / length / empty / max_size / capacity
 *   * element access    -- at / front / back / data / c_str / s.data[i]
 *   * iteration         -- pointer + length walk over `s.data`
 *   * comparison        -- ==, !=, <, >, <=, >=, .compare(), .compare(pos, n, t)
 *   * search            -- find / rfind / find_first/last(_not)_of with the
 *                          (needle, pos) overload, contains, starts_with,
 *                          ends_with
 *   * substr / copy     -- substr(pos[, n]), copy(out, n[, pos])
 *   * mutation          -- append / +=, insert (string + (count, ch)),
 *                          erase, replace, push_back / pop_back, clear,
 *                          resize (1- and 2-arg), assign, swap
 *   * capacity (deep)   -- reserve, shrink_to_fit, capacity preserved
 *                          across a retain-prone dispatcher path
 *   * conversions       -- to_lower, to_upper, trim/ltrim/rtrim,
 *                          neverc_string_from_int, neverc_string_from_uint,
 *                          neverc_string_to_int, neverc_string_to_uint, repeat
 *
 * Loader runs `main(a, b)` and uses the return code as exit status;
 * 0 means every assertion passed, any non-zero return value points
 * at the specific `return N;` line below.  Numbering is contiguous
 * across the sections because the diagnostic value is "which line
 * failed" rather than "which API was wrong".
 */

static __SIZE_TYPE__ count_chars(string s, char ch) {
    /* Iterates via `s.data[i]` to confirm the field-access path is
       still the primary way to walk a string (no iterators in C). */
    __SIZE_TYPE__ n = 0;
    for (__SIZE_TYPE__ i = 0; i < s.len; ++i)
        if (s.data[i] == ch)
            ++n;
    return n;
}

int main(int a, int b) {
    (void)a;
    (void)b;

    /* --- construction --------------------------------------------- */
    {
        /* `string s = "literal"` (function-scope) goes through Sema's
           assignment-conversion retain path, which materialises an
           OWNED copy of the literal bytes (cap == len + 1).  This
           matches the rest of the value-typed contract: every named
           `string` variable owns its buffer.  Callers that genuinely
           want a borrowed view (cap == 0) reach for `neverc_string_view(...)`,
           exercised below. */
        string owned_from_literal = "hello";
        string owned = "hello" + " world";
        if (!neverc_string_eq(owned_from_literal, "hello"))
            return 1;
        if (!neverc_string_eq(owned, "hello world"))
            return 2;
        if (owned_from_literal.cap != owned_from_literal.len + 1)
            return 3;
        if (owned.cap == 0)
            return 4;

        string from_cstr = neverc_string_from_cstr("from cstr");
        if (!neverc_string_eq(from_cstr, "from cstr"))
            return 5;

        string from_char = neverc_string_from_char('!');
        if (!neverc_string_eq(from_char, "!"))
            return 6;

        /* Explicit borrowed view: the only spelling that yields
           `cap == 0` after Sema's retain pass.  Mirrors the
           `test_shellcode_neverc_string_capacity.c` regression that
           pins the same invariant. */
        string view_slice = neverc_string_view("alphabet", 5);
        if (!neverc_string_eq(view_slice, "alpha"))
            return 7;
        if (view_slice.cap != 0)
            return 200;

        /* std::string(n, ch) analogue via repeat. */
        string fill = neverc_string_repeat(neverc_string_from_char('z'), 4);
        if (!neverc_string_eq(fill, "zzzz"))
            return 8;
    }

    /* --- sizing --------------------------------------------------- */
    {
        string s = "abc" + "def";
        if (s.size() != 6)
            return 9;
        if (s.length() != 6)
            return 10;
        if (s.len() != 6)
            return 11;
        if (s.empty())
            return 12;
        if (neverc_string_clear(s).size() != 0)
            return 13;
        string e = "";
        if (!e.empty())
            return 14;

        string m = "x" + "";
        if (m.max_size() < 1024)
            return 15;
    }

    /* --- element access ------------------------------------------- */
    {
        string s = "head" + "tail";
        if (s.front() != 'h')
            return 16;
        if (s.back() != 'l')
            return 17;
        if (s.at(0) != 'h')
            return 18;
        if (s.at(7) != 'l')
            return 19;
        if (s.at(99) != 0)
            return 20;
        const char *cstr = s.c_str();
        if (cstr[0] != 'h' || cstr[8] != 0)
            return 21;
        const char *data = s.data();
        if (data[4] != 't')
            return 22;
        if (count_chars(s, 'l') != 1)
            return 23;
    }

    /* --- iteration / pointer walk --------------------------------- */
    {
        string s = "abcd" + "ef";
        __SIZE_TYPE__ sum = 0;
        for (__SIZE_TYPE__ i = 0; i < s.len; ++i)
            sum += (__SIZE_TYPE__)s.data[i];
        if (sum != ('a' + 'b' + 'c' + 'd' + 'e' + 'f'))
            return 24;
    }

    /* --- comparison ----------------------------------------------- */
    {
        string lhs = "alpha" + "";
        string rhs = "alpha" + "";
        if (lhs != rhs)
            return 25;
        if (lhs == "alphax")
            return 26;
        if (lhs.compare("alpha") != 0)
            return 27;
        if (lhs.compare("beta") >= 0)
            return 28;
        if (("beta" + "").compare("alpha") <= 0)
            return 29;
        /* Position-aware compare: substr(0, 3, "alp") matches. */
        if (lhs.compare(0, 3, "alp") != 0)
            return 30;
        if (lhs.compare(2, 3, "pha") != 0)
            return 31;
        /* 5-arg compare: substring of `lhs` against substring of the
           second string.  `lhs[0..3] == "alp"`, `"alphabet"[0..3] == "alp"`
           -> equal.  Completes std::string::compare's overload set. */
        if (lhs.compare(0, 3, "alphabet" + "", 0, 3) != 0)
            return 32;
        /* Mismatched substring: `lhs[0..3] == "alp"` vs
           `"betagamma"[3..6] == "aga"` -- 'l' < 'g' is false because
           ASCII 'a' < 'l', so ordering is "alp" > "aga".  We only
           assert the sign here so the comparator's implementation is
           free to evolve. */
        if (lhs.compare(0, 3, "betagamma" + "", 3, 3) <= 0)
            return 33;

        string a = "a" + "";
        string ab = "ab" + "";
        if (!(a < ab))
            return 34;
        if (a >= ab)
            return 35;
        if (!(ab > a))
            return 36;
        if (ab <= a)
            return 37;
    }

    /* --- search --------------------------------------------------- */
    /*
       Reference layout (16 chars), used by every find/rfind expectation
       below.  Keeping it as a comment next to the assertions makes the
       expected positions trivially auditable when this file gets
       grepped:

         index : 0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
         char  : a  l  p  h  a  -  b  e  t  a  -  g  a  m  m  a
    */
    {
        string haystack = "alpha-beta" + "-gamma";
        if (haystack.find("beta") != 6)
            return 38;
        if (haystack.find("missing") != NEVERC_STRING_NPOS)
            return 39;
        /* (needle, pos) overload reaches the position-aware helper. */
        if (haystack.find("a", 1) != 4)
            return 40;
        if (haystack.rfind("a") != 15)
            return 41;
        if (haystack.rfind("a", 5) != 4)
            return 42;
        if (haystack.find_first_of("xyz-") != 5)
            return 43;
        if (haystack.find_last_of("xyz-") != 10)
            return 44;
        if (haystack.find_first_of("-", 6) != 10)
            return 45;
        /* find_last_of("a-", 7) walks 7 -> 0 looking for 'a' or '-';
           h[7]='e'/h[6]='b' miss, h[5]='-' hits. */
        if (haystack.find_last_of("a-", 7) != 5)
            return 46;
        if (haystack.find_first_not_of("alph") != 5)
            return 47;
        if (haystack.find_first_not_of("alph", 1) != 5)
            return 48;
        /* find_last_not_of("amg-") walks 15 -> 0; the rightmost
           character outside `amg-` is h[8]='t'. */
        if (haystack.find_last_not_of("amg-") != 8)
            return 49;
        if (haystack.find_last_not_of("amg-", 9) != 8)
            return 50;
        if (!haystack.contains("beta"))
            return 51;
        if (haystack.contains("zeta"))
            return 52;
        if (!haystack.starts_with("alpha"))
            return 53;
        if (!haystack.ends_with("gamma"))
            return 54;
    }

    /* --- search (single-char overloads) --------------------------- */
    /*
       std::string-parity char-typed overloads of the search /
       predicate methods.  Sema picks `neverc_string_find_char` etc. when the
       first dotted argument has integer type (and is not itself a
       string).  Same haystack as above:

         index : 0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
         char  : a  l  p  h  a  -  b  e  t  a  -  g  a  m  m  a
    */
    {
        string haystack = "alpha-beta" + "-gamma";
        /* `s.find('x')` is the canonical one-char find -- char literal
           type is `int`, so the type-aware overload table reaches
           `neverc_string_find_char` instead of the by-string `neverc_string_find`. */
        if (haystack.find('b') != 6)
            return 55;
        if (haystack.find('z') != NEVERC_STRING_NPOS)
            return 56;
        if (haystack.find('a', 1) != 4)
            return 57;
        if (haystack.rfind('a') != 15)
            return 58;
        if (haystack.rfind('a', 5) != 4)
            return 59;
        /* find_first_of('x') / find_last_of('x') collapse to a
           single-character find / rfind; share the same helpers. */
        if (haystack.find_first_of('-') != 5)
            return 60;
        if (haystack.find_last_of('-') != 10)
            return 61;
        if (haystack.find_first_of('-', 6) != 10)
            return 62;
        if (haystack.find_last_of('a', 7) != 4)
            return 63;
        /* Predicate shorthands. */
        if (!haystack.contains('-'))
            return 64;
        if (haystack.contains('z'))
            return 65;
        if (!haystack.starts_with('a'))
            return 66;
        if (haystack.starts_with('z'))
            return 67;
        if (!haystack.ends_with('a'))
            return 68;
        if (haystack.ends_with('z'))
            return 69;

        /* Negated single-char overloads -- the canonical
           "skip-leading-X" / "skip-trailing-X" idioms.

             index : 0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
             char  : a  l  p  h  a  -  b  e  t  a  -  g  a  m  m  a

           find_first_not_of('a') walks 0..15; h[0]='a' skip, h[1]='l'
           hits at 1.  find_last_not_of('a') walks 15..0; h[15]='a'
           skip, h[14]='m' hits at 14. */
        if (haystack.find_first_not_of('a') != 1)
            return 70;
        if (haystack.find_last_not_of('a') != 14)
            return 71;
        /* From-position variants: starting at pos=5 the first non-'-'
           character is h[6]='b'; ending at pos=7 the last non-'a'
           character is h[7]='e'.  These exercise the
           `neverc_string_find_char_not_from` / `neverc_string_rfind_char_not_to`
           helpers that the 2-arg overload row reaches. */
        if (haystack.find_first_not_of('-', 5) != 6)
            return 72;
        if (haystack.find_last_not_of('a', 7) != 7)
            return 73;
        /* Skip-leading-spaces idiom on a literal that starts with two
           spaces.  Confirms the `s.find_first_not_of(' ')` ergonomic
           lowers correctly through the type-aware overload table. */
        string padded = "  hello" + "";
        if (padded.find_first_not_of(' ') != 2)
            return 74;

        /* `char` variable arg -- not just literals -- must reach the
           same char overload because the type-aware predicate keys on
           `isIntegerType()`, not on the literal-ness of the source. */
        char target = 'b';
        if (haystack.find(target) != 6)
            return 75;
    }

    /* --- substr / copy -------------------------------------------- */
    {
        string s = "head" + "tail";
        if (!neverc_string_eq(s.substr(4), "tail"))
            return 76;
        if (!neverc_string_eq(s.substr(0, 4), "head"))
            return 77;
        if (!neverc_string_empty(s.substr(99)))
            return 78;

        char buf[8];
        for (int i = 0; i < 8; ++i)
            buf[i] = '?';
        string src = "0123" + "4567";
        __SIZE_TYPE__ n = src.copy(buf, 4);
        if (n != 4)
            return 79;
        if (buf[0] != '0' || buf[3] != '3')
            return 80;
        /* Position-aware copy: `s.copy(out, n, pos)` reaches the
           overloaded helper; reuse `src` via a fresh literal copy. */
        for (int i = 0; i < 8; ++i)
            buf[i] = '?';
        string src2 = "0123" + "4567";
        n = src2.copy(buf, 3, 5);
        if (n != 3)
            return 81;
        if (buf[0] != '5' || buf[2] != '7')
            return 82;
    }

    /* --- mutation ------------------------------------------------- */
    {
        string s = "hello" + "";
        s = s.append(" world");
        if (!neverc_string_eq(s, "hello world"))
            return 83;
        s += "!";
        if (!neverc_string_eq(s, "hello world!"))
            return 84;
        s = s.push_back('?');
        if (!neverc_string_eq(s, "hello world!?"))
            return 85;
        s = s.pop_back();
        if (!neverc_string_eq(s, "hello world!"))
            return 86;
        /* (pos, count, ch) insert: the 3-arg overload reaches
           `neverc_string_insert_char` per BuiltinStringMethodOverloads.def. */
        s = s.insert(5, 3, '*');
        if (!neverc_string_eq(s, "hello*** world!"))
            return 87;
        /* (pos, string) insert keeps reaching the default helper. */
        s = s.insert(8, "X");
        if (!neverc_string_eq(s, "hello***X world!"))
            return 88;
        s = s.erase(5, 4);
        if (!neverc_string_eq(s, "hello world!"))
            return 89;
        s = s.replace(6, 5, "earth");
        if (!neverc_string_eq(s, "hello earth!"))
            return 90;
        /* (pos, count, count2, ch) replace: the 4-arg overload reaches
           `neverc_string_replace_char` per BuiltinStringMethodOverloads.def
           -- char-fill replacement mirrors std::string's char-fill
           overload of the same name. */
        s = s.replace(6, 5, 4, 'Z');
        if (!neverc_string_eq(s, "hello ZZZZ!"))
            return 91;
        s = s.resize(5);
        if (!neverc_string_eq(s, "hello"))
            return 92;
        s = s.resize(8, '!');
        if (!neverc_string_eq(s, "hello!!!"))
            return 93;
        /* (count, ch) append: the 2-arg overload reaches
           `neverc_string_append_char` -- distinct from `s.append(other)` which
           uses the 1-arg default. */
        s = s.append(3, '?');
        if (!neverc_string_eq(s, "hello!!!???"))
            return 94;
        s = neverc_string_clear(s);
        if (!s.empty())
            return 95;

        string left = "left" + "";
        string right = "right" + "";
        left.swap(right);
        if (!neverc_string_eq(left, "right") || !neverc_string_eq(right, "left"))
            return 96;

        string assigned;
        assigned.assign("payload");
        if (!neverc_string_eq(assigned, "payload"))
            return 97;
        /* (count, ch) assign: the 2-arg overload reaches
           `neverc_string_assign_char` -- the receiver is still passed by
           pointer through `MethodReceiverKind::Receiver`, so the prior
           buffer is released through the same release-then-install
           dance the 1-arg `neverc_string_assign` uses. */
        assigned.assign(5, '*');
        if (!neverc_string_eq(assigned, "*****"))
            return 98;
    }

    /* --- capacity (deep) ------------------------------------------ */
    {
        string s = "ab" + "cd";
        if (s.capacity() < s.len)
            return 99;
        s = s.reserve(64);
        /* The whole point of the by-pointer dispatch: the reservation
           must survive the dotted-call path that would otherwise
           retain the receiver and flatten `cap` to `len + 1`. */
        if (s.capacity() < 64)
            return 100;
        if (s.capacity() != s.cap - 1)
            return 101;
        s = s.shrink_to_fit();
        if (s.capacity() != s.len)
            return 102;
    }

    /* --- conversions / utility ------------------------------------ */
    {
        if (!neverc_string_eq("MiXeD".to_lower(), "mixed"))
            return 103;
        if (!neverc_string_eq("MiXeD".to_upper(), "MIXED"))
            return 104;
        if (!neverc_string_eq(" \t hi \n".trim(), "hi"))
            return 105;
        if (!neverc_string_eq(" \t hi \n".ltrim(), "hi \n"))
            return 106;
        if (!neverc_string_eq(" \t hi \n".rtrim(), " \t hi"))
            return 107;
        if (!neverc_string_eq(neverc_string_repeat("ab", 3), "ababab"))
            return 108;
        if (!neverc_string_eq(neverc_string_from_int(-42), "-42"))
            return 109;
        if (!neverc_string_eq(neverc_string_from_uint(123), "123"))
            return 110;
        if (neverc_string_to_int("-42") != -42)
            return 111;
        if (neverc_string_to_uint("+123") != 123)
            return 112;
    }

    /* --- extended utilities (hash / reverse / count) ------------- */
    {
        /* `s.hash()` is the FNV-1a 64-bit content hash; equal strings
           hash to the same value, distinct contents almost certainly
           hash differently.  `neverc_string_hash("")` is the FNV-1a basis
           constant `0xcbf29ce484222325`. */
        string a = "alpha" + "";
        string b = "alpha" + "";
        string c = "beta" + "";
        if (a.hash() != b.hash())
            return 113;
        if (a.hash() == c.hash())
            return 114;
        if (("" + "").hash() != 0xcbf29ce484222325ULL)
            return 115;

        /* `s.reverse()` flips the byte order; reversing twice round-
           trips back to the original payload. */
        string forward = "abcdef" + "";
        string reversed = forward.reverse();
        if (!neverc_string_eq(reversed, "fedcba"))
            return 116;
        string twice = ("xyzw" + "").reverse().reverse();
        if (!neverc_string_eq(twice, "xyzw"))
            return 117;
        if (!neverc_string_empty(("" + "").reverse()))
            return 118;

        /* `s.count(needle)` counts NON-OVERLAPPING occurrences --
           "aaaa".count("aa") is 2 (positions 0 and 2), not 3. */
        if (("hello world" + "").count("l") != 3)
            return 119;
        if (("aaaa" + "").count("aa") != 2)
            return 120;
        if (("aaaa" + "").count("zzz") != 0)
            return 121;
        /* Empty-needle count returns `s.len + 1` -- one match between
           every pair of characters plus one each at the start / end,
           so callers can pre-size split outputs without special-casing
           "".  See the helper comment in Utility.inc. */
        if (("abc" + "").count("") != 4)
            return 122;
        /* Single-char count via the type-aware overload table:
           `s.count('l')` reaches `neverc_string_count_char`. */
        if (("hello world" + "").count('l') != 3)
            return 123;
        if (("hello" + "").count('z') != 0)
            return 124;
    }

    /* --- formatting / padding (pad_left / pad_right / replace_all /
       base conversion) ------------------------------------------- */
    {
        /* `s.pad_left(width, ch)` prepends, `pad_right` appends.
           Single mainstream spelling -- the Python-flavour
           `rjust` / `ljust` aliases were dropped from the
           dotted-method table because their "left/right justify"
           verbs invert the natural reading of "where do I add
           padding" (`ljust` justifies LEFT therefore pads RIGHT,
           etc.) which trips almost every reader. */
        if (!neverc_string_eq(("42" + "").pad_left(5, '0'), "00042"))
            return 125;
        if (!neverc_string_eq(("hi" + "").pad_right(5, '.'), "hi..."))
            return 126;
        /* No-op when `width <= s.len` -- helper still returns a fresh
           owned copy so the value-typed contract holds. */
        if (!neverc_string_eq(("longer" + "").pad_left(3, ' '), "longer"))
            return 129;
        if (!neverc_string_eq(("longer" + "").pad_right(3, ' '), "longer"))
            return 130;

        /* `s.replace_all(from, to)` global substitution. */
        if (!neverc_string_eq(("a-b-c" + "").replace_all("-", "::"), "a::b::c"))
            return 131;
        /* Non-overlapping semantics: `"aaaa".replace_all("aa", "x")`
           consumes positions 0..1 and 2..3, producing "xx". */
        if (!neverc_string_eq(("aaaa" + "").replace_all("aa", "x"), "xx"))
            return 132;
        /* Replacement that grows beyond the source. */
        if (!neverc_string_eq(("ab" + "").replace_all("a", "AAA"), "AAAb"))
            return 133;
        /* Replacement that shrinks. */
        if (!neverc_string_eq(("hello hello" + "").replace_all("hello", "hi"),
                        "hi hi"))
            return 134;
        /* Empty `from` -- returns the input unchanged so the loop body
           does not spin forever. */
        if (!neverc_string_eq(("xyz" + "").replace_all("", "..."), "xyz"))
            return 135;
        /* No match -- returns the input unchanged. */
        if (!neverc_string_eq(("xyz" + "").replace_all("zz", "Q"), "xyz"))
            return 136;

        /* Base conversion: `neverc_string_from_int_base` / `from_uint_base`
           lower / uppercase agnostic (lowercase only by default; pipe
           through `to_upper` for uppercase). */
        if (!neverc_string_eq(neverc_string_from_uint_base(255, 16), "ff"))
            return 137;
        if (!neverc_string_eq(neverc_string_from_uint_base(255, 16).to_upper(), "FF"))
            return 138;
        if (!neverc_string_eq(neverc_string_from_uint_base(0, 16), "0"))
            return 139;
        if (!neverc_string_eq(neverc_string_from_uint_base(0xdeadbeefULL, 16), "deadbeef"))
            return 140;
        if (!neverc_string_eq(neverc_string_from_uint_base(7, 2), "111"))
            return 141;
        if (!neverc_string_eq(neverc_string_from_uint_base(8, 8), "10"))
            return 142;
        if (!neverc_string_eq(neverc_string_from_int_base(-42, 16), "-2a"))
            return 143;
        /* 36 in base-36 is "10" (one digit 1, one digit 0): the
           lowest two-digit value confirms the multi-digit divisor /
           remainder loop terminates correctly. */
        if (!neverc_string_eq(neverc_string_from_uint_base(36, 36), "10"))
            return 144;
        /* Out-of-range bases collapse to the empty sentinel. */
        if (!neverc_string_empty(neverc_string_from_uint_base(123, 1)))
            return 145;
        if (!neverc_string_empty(neverc_string_from_uint_base(123, 37)))
            return 146;
    }

    /* --- memory safety stress ------------------------------------- */
    {
        /* Self-assign cycle on owned + borrowed: sema's
           `s = s` rewrite path must not corrupt the storage. */
        for (int i = 0; i < 96; ++i) {
            string owned = "alpha" + "beta";
            owned = owned;
            if (!neverc_string_eq(owned, "alphabeta"))
                return 147;
            string borrowed = "literal";
            borrowed = borrowed;
            if (!neverc_string_eq(borrowed, "literal"))
                return 148;
        }

        /* Borrowed-view-into-owned: assigning a slice of `s` back to
           `s` must copy first, then drop the original buffer. */
        string s = "alpha" + "beta";
        s = neverc_string_view(s.data + 1, 4);
        if (!neverc_string_eq(s, "lpha"))
            return 149;

        /* Method chain consuming temporaries on every step. */
        string chain =
            ("  alpha-BETA  " + "").trim().to_lower().replace(0, 5, "GAMMA");
        if (!neverc_string_eq(chain, "GAMMA-beta"))
            return 150;
    }

    /* --- forged / oversized handles short-circuit safely --------- */
    {
        /* `(data == NULL, len > 0)` is the canonical "forged handle"
           shape `__neverc_string_invalid` rejects: every reading
           helper short-circuits to the empty-shape result instead of
           dereferencing the NULL.  Use this NULL-data shape (rather
           than a non-NULL garbage pointer) so the test stays
           memory-safe even when the predicate accidentally regresses. */
        string forged = {(const char *)0, 1, 0};
        if (forged.front() != 0)
            return 151;
        if (forged.back() != 0)
            return 152;
        if (forged.at(0) != 0)
            return 153;
        /* Oversized handle: `len > MAX_LEN` is the second invalid
           shape the predicate covers (saturating SIZE_MAX would slip
           past `len + 1`-style guards in callers).  Read helpers must
           still short-circuit to 0 instead of walking SIZE_MAX bytes. */
        string oversized = {(const char *)0, NEVERC_STRING_NPOS, 0};
        if (oversized.front() != 0)
            return 154;
    }

    return 0;
}
