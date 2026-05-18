// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* NeverC builtin string boundary / degenerate-input coverage.
 *
 * Single source of truth for the prelude's clamp / NULL / empty / npos
 * short-circuit paths.  Other tests exercise the happy path and at most
 * one edge per helper -- this file pins down the expected behaviour at
 * every boundary so a regression in any clamp or "out-of-range -> empty"
 * branch fails one named check instead of leaking into a tangential
 * lifetime / search test.
 *
 * Loader runs `main(a, b)` and uses the return code as exit status.
 * 0 means every assertion passed; any non-zero return identifies the
 * specific `return N;` line below.
 */
int main(int a, int b) {
    (void)a;
    (void)b;

    /* substr / erase clamp and out-of-range. */
    {
        string s = "abcdef" + "";
        if (!neverc_string_eq(s.substr(0, NEVERC_STRING_NPOS), "abcdef"))
            return 1;
    }
    {
        string s = "abcdef" + "";
        if (!neverc_string_empty(s.substr(6, 0)))   /* pos == len -> empty */
            return 2;
    }
    {
        string s = "abcdef" + "";
        if (!neverc_string_empty(s.substr(100, 5))) /* pos > len -> empty */
            return 3;
    }
    {
        string s = "abcdef" + "";
        if (!neverc_string_eq(s.erase(0), ""))      /* erase whole tail */
            return 4;
    }
    {
        string s = "abcdef" + "";
        if (!neverc_string_eq(s.erase(6, 5), "abcdef")) /* erase past end */
            return 5;
    }
    {
        string s = "abcdef" + "";
        if (!neverc_string_eq(s.erase(2, NEVERC_STRING_NPOS), "ab"))
            return 6;
    }

    /* find / rfind on empty needle: matches std::string semantics. */
    {
        string s = "abcdef" + "";
        if (s.find("") != 0)                 /* empty matches at pos 0 */
            return 7;
        if (s.find("", 3) != 3)              /* empty matches at requested pos */
            return 8;
        if (s.find("", 100) != NEVERC_STRING_NPOS)  /* but not past end */
            return 9;
    }
    {
        string s = "abcdef" + "";
        if (s.rfind("") != 6)                /* rfind empty -> len */
            return 10;
    }
    {
        string s = "abcdef" + "";
        if (s.find("missing") != NEVERC_STRING_NPOS)
            return 11;
        if (s.rfind("missing") != NEVERC_STRING_NPOS)
            return 12;
    }

    /* find_*_of / find_*_not_of empty-set semantics. */
    {
        string s = "abcdef" + "";
        if (s.find_first_of("") != NEVERC_STRING_NPOS)
            return 13;
        if (s.find_last_of("") != NEVERC_STRING_NPOS)
            return 14;
        if (s.find_first_not_of("") != 0)
            return 15;
        if (s.find_last_not_of("") != 5)     /* last index in "abcdef" */
            return 16;
    }

    /* repeat / resize / replace boundary clamps. */
    {
        string s = "abc" + "";
        if (!neverc_string_empty(s.repeat(0)))      /* count == 0 -> empty */
            return 17;
    }
    {
        if (!neverc_string_empty(neverc_string_repeat("", 5)))  /* empty source -> empty */
            return 18;
    }
    {
        string s = "abcd" + "";
        if (!neverc_string_empty(s.resize(0, '!'))) /* resize to 0 -> empty */
            return 19;
    }
    {
        string s = "abc" + "";
        s = s.replace(0, NEVERC_STRING_NPOS, "X"); /* count clamped to len */
        if (!neverc_string_eq(s, "X"))
            return 20;
    }
    {
        string s = "abc" + "";
        s = s.replace(100, 5, "?");          /* pos > len clamped to len */
        if (!neverc_string_eq(s, "abc?"))
            return 21;
    }
    {
        string s = "abcd" + "";
        s = s.insert(100, "X");              /* pos > len clamped to len */
        if (!neverc_string_eq(s, "abcdX"))
            return 22;
    }

    /* compare boundary cases. */
    {
        if (neverc_string_compare("", "") != 0)
            return 23;
        if (neverc_string_compare("", "x") >= 0)
            return 24;
        if (neverc_string_compare("x", "") <= 0)
            return 25;
    }
    {
        string s = "abcdef" + "";
        if (s.compare(100, 3, "abc") >= 0)   /* pos > len -> empty prefix */
            return 26;
        if (s.compare(0, NEVERC_STRING_NPOS, "abcdef") != 0)
            return 27;
    }

    /* int / uint conversion edges. */
    {
        if (!neverc_string_eq(neverc_string_from_int(0), "0"))
            return 28;
        if (!neverc_string_eq(neverc_string_from_int(-1), "-1"))
            return 29;
        if (!neverc_string_eq(neverc_string_from_uint(0), "0"))
            return 30;
        if (neverc_string_to_int("") != 0)          /* empty parses to 0 */
            return 31;
        if (neverc_string_to_int("abc") != 0)       /* non-digit parses to 0 */
            return 32;
        if (neverc_string_to_int("12abc") != 12)    /* stops at first non-digit */
            return 33;
        if (neverc_string_to_uint("") != 0)
            return 34;
        if (neverc_string_to_uint("abc") != 0)
            return 35;
        if (neverc_string_to_uint("12abc") != 12)
            return 36;
    }

    /* Factory NULL / zero-length safety. */
    {
        string nullc = neverc_string_from_cstr((const char *)0);
        if (!neverc_string_empty(nullc))
            return 37;
    }
    {
        string nullv = neverc_string_view((const char *)0, 0);
        if (!neverc_string_empty(nullv))
            return 38;
    }
    {
        string emptyv = neverc_string_view("ignored", 0);
        if (!neverc_string_empty(emptyv))
            return 39;
    }

    /* copy(NULL, n, pos) is the std::string-style "tell me how many bytes
       you would have written" probe shape; the prelude consumes the input
       and returns the clipped count. */
    {
        if (neverc_string_copy_from("abcdef", (char *)0, 4, 1) != 4)
            return 40;
        if (neverc_string_copy_from("abc", (char *)0, 100, 1) != 2) /* clipped */
            return 41;
    }

    /* trim / ltrim / rtrim degenerate inputs. */
    {
        if (!neverc_string_empty(neverc_string_trim("")))
            return 42;
        if (!neverc_string_empty(neverc_string_ltrim("    ")))
            return 43;
        if (!neverc_string_empty(neverc_string_rtrim("\t\n\r")))
            return 44;
        if (!neverc_string_eq(neverc_string_trim("nopad"), "nopad"))
            return 45;
    }

    /* clone of an empty owned string keeps lifetime balanced. */
    {
        string e = neverc_string_clear("data");
        if (!neverc_string_empty(e))
            return 46;
        string c = neverc_string_clone(e);
        if (!neverc_string_empty(c))
            return 47;
    }

    /* push_back / pop_back boundary at empty + at one. */
    {
        string s = neverc_string_clear("data");
        s = s.push_back('A');
        if (!neverc_string_eq(s, "A"))
            return 48;
        s = s.pop_back();
        if (!neverc_string_empty(s))
            return 49;
        s = s.pop_back();                    /* pop empty stays empty */
        if (!neverc_string_empty(s))
            return 50;
    }

    /* Oversized handle (`len` saturates `__SIZE_TYPE__`) must short-
       circuit through `__neverc_string_invalid` in every prelude
       helper that would otherwise read or copy `s.len` bytes.  We
       construct a synthetic borrowed view with `len = NEVERC_STRING_NPOS`
       (== `(size_t)-1`, beyond `NEVERC_STRING_MAX_LEN`) and feed it
       to one helper per family: search / compare / mutator /
       accessor.  Each helper must release the input cleanly and
       fall back to its documented "no-op / NPOS / 0" result -- a
       regression that strips the invalid-check would surface as
       either a hang (loop walks `SIZE_MAX` bytes) or a
       segmentation fault (`s.data[s.len-1]` reads gigabytes past
       any real allocation). */
    {
        string oversized = {(const char *)1, NEVERC_STRING_NPOS, 0};
        if (neverc_string_find(oversized, "needle") != NEVERC_STRING_NPOS)
            return 51;
        oversized = (string){(const char *)1, NEVERC_STRING_NPOS, 0};
        if (neverc_string_contains(oversized, "needle"))
            return 52;
        oversized = (string){(const char *)1, NEVERC_STRING_NPOS, 0};
        if (neverc_string_compare(oversized, "real") == 0)
            return 53;  /* len mismatch -> non-zero verdict */
        oversized = (string){(const char *)1, NEVERC_STRING_NPOS, 0};
        if (neverc_string_eq(oversized, "real"))
            return 54;  /* invalid -> never equal */
        oversized = (string){(const char *)1, NEVERC_STRING_NPOS, 0};
        if (oversized.front() != 0)
            return 55;  /* invalid -> 0 sentinel */
        oversized = (string){(const char *)1, NEVERC_STRING_NPOS, 0};
        if (oversized.back() != 0)
            return 56;
        oversized = (string){(const char *)1, NEVERC_STRING_NPOS, 0};
        string popped = neverc_string_pop_back(oversized);
        if (!neverc_string_empty(popped))
            return 57;
        oversized = (string){(const char *)1, NEVERC_STRING_NPOS, 0};
        string pushed = neverc_string_push_back(oversized, '?');
        /* Retain sanitises the oversized handle to empty; push_back on
           empty legitimately allocates a 1-char "?" buffer. */
        if (!neverc_string_eq(pushed, "?"))
            return 58;
        oversized = (string){(const char *)1, NEVERC_STRING_NPOS, 0};
        string upper = neverc_string_to_upper(oversized);
        if (!neverc_string_empty(upper))
            return 59;
        oversized = (string){(const char *)1, NEVERC_STRING_NPOS, 0};
        if (neverc_string_to_int(oversized) != 0)
            return 60;
    }

    return 0;
}
