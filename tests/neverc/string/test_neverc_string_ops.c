// NeverC builtin string mutation/utility/capacity ops
// RUN: %neverc -std=c23 %s -o %t && %t

int main(void) {
    /* ================================================================
     * Mutation: push_back, pop_back, insert, insert_char, erase,
     *           replace, clear, swap, resize, append
     * ================================================================ */

    /* push_back */
    {
        string s = "ab";
        s = s.push_back('c');
        if (s != "abc") return 1;
        s = s.push_back('d');
        if (s != "abcd") return 2;

        string e = "";
        e = e.push_back('x');
        if (e != "x") return 3;
    }

    /* pop_back */
    {
        string s = "abcd";
        s = s.pop_back();
        if (s != "abc") return 10;
        s = s.pop_back();
        if (s != "ab") return 11;

        string one = "z";
        one = one.pop_back();
        if (!neverc_string_empty(one)) return 12;

        string e = "";
        e = e.pop_back();
        if (!neverc_string_empty(e)) return 13;
    }

    /* insert (string) */
    {
        string s = "abef";
        s = s.insert(2, "cd");
        if (s != "abcdef") return 20;

        s = s.insert(0, ">>>");
        if (s != ">>>abcdef") return 21;

        s = s.insert(s.len, "<<<");
        if (s != ">>>abcdef<<<") return 22;

        string e = "";
        e = neverc_string_insert(e, 0, "hello");
        if (e != "hello") return 23;

        string noop = "abc";
        noop = noop.insert(1, "");
        if (noop != "abc") return 24;

        string past = "abc";
        past = past.insert(999, "d");
        if (past != "abcd") return 25;
    }

    /* insert_char */
    {
        string s = "ac";
        s = neverc_string_insert_char(s, 1, 3, 'b');
        if (s != "abbbc") return 30;

        s = neverc_string_insert_char(s, 0, 2, '_');
        if (s != "__abbbc") return 31;

        string z = neverc_string_insert_char("abc", 1, 0, 'x');
        if (z != "abc") return 32;
    }

    /* erase */
    {
        string s = "abcdef";
        s = s.erase(2, 2);
        if (s != "abef") return 40;

        s = s.erase(0, 1);
        if (s != "bef") return 41;

        string tail = "abcdef";
        tail = tail.erase(4, 999);
        if (tail != "abcd") return 42;

        string all = "xyz";
        all = all.erase(0, 3);
        if (!neverc_string_empty(all)) return 43;

        string past_end = "abc";
        past_end = past_end.erase(10, 2);
        if (past_end != "abc") return 44;

        string e = "";
        e = e.erase(0, 1);
        if (!neverc_string_empty(e)) return 45;
    }

    /* replace */
    {
        string s = "hello world";
        s = s.replace(6, 5, "neverc");
        if (s != "hello neverc") return 50;

        s = s.replace(0, 5, "hi");
        if (s != "hi neverc") return 51;

        string grow = "abc";
        grow = grow.replace(1, 1, "xxxxx");
        if (grow != "axxxxxc") return 52;

        string shrink = "abcdef";
        shrink = shrink.replace(1, 4, "z");
        if (shrink != "azf") return 53;

        string empty_rep = "abc";
        empty_rep = empty_rep.replace(1, 1, "");
        if (empty_rep != "ac") return 54;

        string past_end = "abc";
        past_end = past_end.replace(10, 2, "x");
        if (past_end != "abcx") return 55;
    }

    /* clear */
    {
        string s = "hello";
        s = s.clear();
        if (!neverc_string_empty(s)) return 60;
        if (s.cap != 0) return 61;
    }

    /* swap */
    {
        string a = "alpha";
        string b = "beta";
        a.swap(b);
        if (a != "beta") return 70;
        if (b != "alpha") return 71;

        string x = "";
        string y = "nonempty";
        x.swap(y);
        if (x != "nonempty") return 72;
        if (y != "") return 73;
    }

    /* resize (dotted form) */
    {
        string s = "abc";
        s = s.resize(5, '*');
        if (s != "abc**") return 80;
        if (s.size() != 5) return 81;

        s = s.resize(2, '!');
        if (s != "ab") return 82;

        s = s.resize(0, ' ');
        if (!neverc_string_empty(s)) return 83;
    }

    /* append (dotted form) */
    {
        string s = "hello";
        s = s.append(" world");
        if (s != "hello world") return 90;

        string e = "";
        e = e.append("start");
        if (e != "start") return 91;

        string x = "a";
        x = x.append("");
        if (x != "a") return 92;
    }

    /* ================================================================
     * Capacity: reserve, shrink_to_fit, max_size
     * ================================================================ */

    /* reserve + shrink_to_fit */
    {
        string s = "abc";
        s = s.reserve(100);
        if (s.cap < 101) return 100;
        if (s.len != 3) return 101;
        if (s != "abc") return 102;

        s = s.shrink_to_fit();
        if (s.cap != s.len + 1) return 103;
        if (s != "abc") return 104;
    }

    /* max_size */
    {
        string s = "test";
        __SIZE_TYPE__ ms = s.max_size();
        if (ms != (__SIZE_TYPE__)-2) return 110;
    }

    /* ================================================================
     * Utility: from_cstr, from_char, repeat, to_lower, to_upper,
     *          trim, ltrim, rtrim, from_int, from_uint, to_int, to_uint
     * ================================================================ */

    /* from_cstr / from_char */
    {
        const char *raw = "raw-cstr";
        string s = neverc_string_from_cstr(raw);
        if (s != "raw-cstr") return 120;
        if (s.cap == 0) return 121;

        string null_s = neverc_string_from_cstr((const char *)0);
        if (!neverc_string_empty(null_s)) return 122;

        string ch = neverc_string_from_char('Z');
        if (ch != "Z") return 123;
        if (ch.len != 1) return 124;
    }

    /* repeat */
    {
        string s = "ab".repeat(4);
        if (s != "abababab") return 130;
        if (s.len != 8) return 131;

        string zero = "x".repeat(0);
        if (!neverc_string_empty(zero)) return 132;

        string one = "hello".repeat(1);
        if (one != "hello") return 133;

        string single = neverc_string_from_char('*');
        single = single.repeat(5);
        if (single != "*****") return 134;
    }

    /* Single mainstream `to_lower` / `to_upper` spelling.  The
       Python-flavour `lower` / `upper` short aliases were dropped
       from the dotted-method table -- the bare verb is ambiguous
       between "is this lowercase?" and "produce a lowercased copy"
       on a value-typed surface, so we keep the explicit `to_*`
       form that matches the rest of this table (`to_base64`,
       `to_hex`, `to_utf16`, ...). */
    {
        string s = "Hello World 123!";
        string lo = s.to_lower();
        if (lo != "hello world 123!") return 140;
        string up = s.to_upper();
        if (up != "HELLO WORLD 123!") return 141;

        if ("MiXeD".to_lower() != "mixed") return 142;
        if ("MiXeD".to_upper() != "MIXED") return 143;

        string e = "";
        if (!neverc_string_empty(e.to_lower())) return 144;
        if (!neverc_string_empty(e.to_upper())) return 145;
    }

    /* trim / ltrim / rtrim */
    {
        string s = "  \t hello world \n ";
        if (s.trim() != "hello world") return 150;
        if (s.ltrim() != "hello world \n ") return 151;
        if (s.rtrim() != "  \t hello world") return 152;

        string all_ws = "   \t\n  ";
        if (!neverc_string_empty(all_ws.trim())) return 153;
        if (!neverc_string_empty(all_ws.ltrim())) return 154;
        if (!neverc_string_empty(all_ws.rtrim())) return 155;

        string no_ws = "compact";
        if (no_ws.trim() != "compact") return 156;

        string e = "";
        if (!neverc_string_empty(e.trim())) return 157;
    }

    /* from_int / from_uint */
    {
        if (neverc_string_from_int(0) != "0") return 160;
        if (neverc_string_from_int(42) != "42") return 161;
        if (neverc_string_from_int(-1) != "-1") return 162;
        if (neverc_string_from_int(-999) != "-999") return 163;
        if (neverc_string_from_int(1234567890) != "1234567890") return 164;

        if (neverc_string_from_uint(0) != "0") return 170;
        if (neverc_string_from_uint(42) != "42") return 171;
        if (neverc_string_from_uint(1234567890) != "1234567890") return 172;
    }

    /* to_int / to_uint */
    {
        if (neverc_string_to_int("0") != 0) return 180;
        if (neverc_string_to_int("42") != 42) return 181;
        if (neverc_string_to_int("-7") != -7) return 182;
        if (neverc_string_to_int("+99") != 99) return 183;
        if (neverc_string_to_int("") != 0) return 184;
        if (neverc_string_to_int("abc") != 0) return 185;
        if (neverc_string_to_int("123abc") != 123) return 186;

        if (neverc_string_to_uint("0") != 0) return 190;
        if (neverc_string_to_uint("42") != 42) return 191;
        if (neverc_string_to_uint("+99") != 99) return 192;
        if (neverc_string_to_uint("") != 0) return 193;
        if (neverc_string_to_uint("456xyz") != 456) return 194;
    }

    /* ================================================================
     * Search with position: find_from, rfind_to, *_of with positions
     * ================================================================ */

    /* find_from */
    {
        string s = "abcabc";
        if (s.find("abc") != 0) return 200;
        if (neverc_string_find_from(s, "abc", 1) != 3) return 201;
        if (neverc_string_find_from(s, "abc", 4) != NEVERC_STRING_NPOS) return 202;
        if (neverc_string_find_from(s, "abc", 99) != NEVERC_STRING_NPOS) return 203;
    }

    /* rfind_to */
    {
        string s = "abcabc";
        if (s.rfind("abc") != 3) return 210;
        if (neverc_string_rfind_to(s, "abc", 2) != 0) return 211;
        if (neverc_string_rfind_to(s, "abc", 0) != 0) return 212;
    }

    /* find_first_of_from / find_last_of_to */
    {
        string s = "abcdef";
        if (neverc_string_find_first_of_from(s, "cd", 0) != 2) return 220;
        if (neverc_string_find_first_of_from(s, "cd", 3) != 3) return 221;
        if (neverc_string_find_first_of_from(s, "cd", 4) != NEVERC_STRING_NPOS) return 222;

        if (neverc_string_find_last_of_to(s, "cd", 5) != 3) return 223;
        if (neverc_string_find_last_of_to(s, "cd", 2) != 2) return 224;
        if (neverc_string_find_last_of_to(s, "cd", 1) != NEVERC_STRING_NPOS) return 225;
    }

    /* find_first_not_of_from / find_last_not_of_to */
    {
        string s = "aaabbbccc";
        if (neverc_string_find_first_not_of_from(s, "a", 0) != 3) return 230;
        if (neverc_string_find_first_not_of_from(s, "ab", 0) != 6) return 231;
        if (neverc_string_find_first_not_of_from(s, "ab", 6) != 6) return 232;

        if (neverc_string_find_last_not_of_to(s, "c", 8) != 5) return 233;
        if (neverc_string_find_last_not_of_to(s, "bc", 8) != 2) return 234;
        if (neverc_string_find_last_not_of_to(s, "bc", 2) != 2) return 235;
    }

    /* ================================================================
     * Copy: neverc_string_copy, neverc_string_copy_from
     * ================================================================ */
    {
        char buf[16];
        for (int i = 0; i < 16; i++) buf[i] = 0;

        __SIZE_TYPE__ n = neverc_string_copy("hello", buf, 3);
        if (n != 3) return 240;
        if (buf[0] != 'h' || buf[1] != 'e' || buf[2] != 'l') return 241;

        for (int i = 0; i < 16; i++) buf[i] = 0;
        n = neverc_string_copy("hi", buf, 10);
        if (n != 2) return 242;
        if (buf[0] != 'h' || buf[1] != 'i') return 243;

        for (int i = 0; i < 16; i++) buf[i] = 0;
        n = neverc_string_copy_from("hello world", buf, 5, 6);
        if (n != 5) return 244;
        if (buf[0] != 'w' || buf[4] != 'd') return 245;

        n = neverc_string_copy_from("abc", buf, 10, 99);
        if (n != 0) return 246;

        n = neverc_string_copy("test", (char *)0, 4);
        if (n != 4) return 247;
    }

    /* ================================================================
     * Compare: compare_substr
     * ================================================================ */
    {
        string s = "hello world";
        if (s.compare(0, 5, "hello") != 0) return 250;
        if (s.compare(6, 5, "world") != 0) return 251;
        if (s.compare(0, 5, "hellp") >= 0) return 252;
        if (s.compare(0, 5, "hell") <= 0) return 253;
        if (s.compare(0, 3, "hel") != 0) return 254;
    }

    /* ================================================================
     * Edge cases: consecutive mutations, chain operations
     * ================================================================ */

    /* consecutive push_back + pop_back round trip */
    {
        string s = "";
        for (int i = 0; i < 10; i++)
            s = s.push_back((char)('a' + i));
        if (s != "abcdefghij") return 260;
        if (s.len != 10) return 261;

        for (int i = 0; i < 5; i++)
            s = s.pop_back();
        if (s != "abcde") return 262;
    }

    /* mixed mutations */
    {
        string s = "hello";
        s = s.erase(1, 3);
        if (s != "ho") return 270;
        s = s.insert(1, "ell");
        if (s != "hello") return 271;
        s = s.replace(0, 5, "world");
        if (s != "world") return 272;
        s = s.push_back('!');
        if (s != "world!") return 273;
        s = s.pop_back();
        if (s != "world") return 274;
    }

    /* repeated alloc/free pressure in a loop */
    {
        for (int i = 0; i < 200; i++) {
            string tmp = "loop";
            tmp = tmp.push_back((char)('0' + (i % 10)));
            tmp = tmp.append("!");
            if (tmp.len != 6) return 280;
        }
    }

    /* from_int round-trip */
    {
        for (__PTRDIFF_TYPE__ v = -50; v <= 50; v++) {
            string s = neverc_string_from_int(v);
            __PTRDIFF_TYPE__ back = neverc_string_to_int(s);
            if (back != v) return 290;
        }
    }

    /* assign to string built from its own substr */
    {
        string s = "hello world";
        s = s.substr(6);
        if (s != "world") return 300;
        s.assign(s.substr(1));
        if (s != "orld") return 301;
    }

    /* ================================================================
     * Dotted-call overload dispatch (2-arg find, rfind, *_of forms)
     * ================================================================ */

    /* s.find(needle, pos) -> neverc_string_find_from */
    {
        string s = "abcabc";
        if (s.find("abc", 1) != 3) return 310;
        if (s.find("abc", 4) != NEVERC_STRING_NPOS) return 311;
        if (s.find("a", 0) != 0) return 312;
    }

    /* s.rfind(needle, pos) -> neverc_string_rfind_to */
    {
        string s = "abcabc";
        if (s.rfind("abc", 2) != 0) return 320;
        if (s.rfind("abc", 5) != 3) return 321;
        if (s.rfind("abc", 0) != 0) return 322;
    }

    /* s.find_first_of(chars, pos) -> neverc_string_find_first_of_from */
    {
        string s = "abcdef";
        if (s.find_first_of("cd", 0) != 2) return 330;
        if (s.find_first_of("cd", 3) != 3) return 331;
        if (s.find_first_of("cd", 4) != NEVERC_STRING_NPOS) return 332;
    }

    /* s.find_last_of(chars, pos) -> neverc_string_find_last_of_to */
    {
        string s = "abcdef";
        if (s.find_last_of("cd", 5) != 3) return 340;
        if (s.find_last_of("cd", 2) != 2) return 341;
        if (s.find_last_of("cd", 1) != NEVERC_STRING_NPOS) return 342;
    }

    /* s.find_first_not_of(chars, pos) -> neverc_string_find_first_not_of_from */
    {
        string s = "aaabbb";
        if (s.find_first_not_of("a", 0) != 3) return 350;
        if (s.find_first_not_of("ab", 0) != NEVERC_STRING_NPOS) return 351;
        if (s.find_first_not_of("a", 3) != 3) return 352;
    }

    /* s.find_last_not_of(chars, pos) -> neverc_string_find_last_not_of_to */
    {
        string s = "aaabbb";
        if (s.find_last_not_of("b", 5) != 2) return 360;
        if (s.find_last_not_of("ab", 5) != NEVERC_STRING_NPOS) return 361;
        if (s.find_last_not_of("b", 2) != 2) return 362;
    }

    /* s.copy(buf, count, pos) -> neverc_string_copy_from */
    {
        char buf[16];
        for (int i = 0; i < 16; i++) buf[i] = 0;
        string s = "hello world";
        __SIZE_TYPE__ n = s.copy(buf, 5, 6);
        if (n != 5) return 370;
        if (buf[0] != 'w' || buf[4] != 'd') return 371;
    }

    /* s.insert(pos, count, ch) -> neverc_string_insert_char */
    {
        string s = "ac";
        s = s.insert(1, 3, 'b');
        if (s != "abbbc") return 380;
        s = s.insert(0, 2, '_');
        if (s != "__abbbc") return 381;
    }

    /* ================================================================
     * Dotted-call default-arg dispatch (1-arg erase, 1-arg resize)
     * ================================================================ */

    /* s.erase(pos) -> neverc_string_erase(s, pos, npos) — erase to end */
    {
        string s = "abcdef";
        s = s.erase(3);
        if (s != "abc") return 390;

        string t = "xyz";
        t = t.erase(0);
        if (!neverc_string_empty(t)) return 391;
    }

    /* s.resize(n) -> neverc_string_resize(s, n, '\0') — NUL fill */
    {
        string s = "abc";
        s = s.resize(6);
        if (s.len != 6) return 400;
        if (s.at(0) != 'a' || s.at(2) != 'c') return 401;
        if (s.at(3) != 0 || s.at(5) != 0) return 402;

        s = s.resize(2);
        if (s != "ab") return 403;
    }

    /* s.assign(value) — dotted form with receiver-by-pointer */
    {
        string s = "old value";
        s.assign("new value");
        if (s != "new value") return 410;

        string t = "hello" + " world";
        s.assign(t);
        if (s != "hello world") return 411;
        if (t != "hello world") return 412;
    }

    /* ================================================================
     * Ordering operators: <  >  <=  >=
     * ================================================================ */
    {
        string a = "abc";
        string b = "abd";
        string c = "abc";
        string d = "ab";

        if (!(a < b)) return 420;
        if (a > b) return 421;
        if (!(a <= c)) return 422;
        if (!(a >= c)) return 423;
        if (!(a > d)) return 424;
        if (a < d) return 425;
        if (!(d <= a)) return 426;
        if (d >= b) return 427;
    }

    printf("test_neverc_string_ops: ALL PASSED\n");
    return 0;
}
