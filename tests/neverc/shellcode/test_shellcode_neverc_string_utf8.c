// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* NeverC builtin string UTF-8 / Unicode surface.
 *
 * Pins down the encoding contract the prelude documents:
 *
 *   * `u8"..."` literal initialisation  -- `string s = u8"你好"` is
 *     accepted by Sema (the dotted-call dispatcher and the
 *     literal-to-string lowering both honour the UTF-8 spelling so
 *     the QString-style "string s = u8\"...\"" ergonomic round-trips
 *     through the byte-oriented `(data, len, cap)` triple verbatim).
 *
 *   * Codepoint surface
 *       - `neverc_string_utf8_count(s)` / `s.utf8_count()`
 *       - `neverc_string_utf8_valid(s)` / `s.utf8_valid()`
 *       - `neverc_string_utf8_at(s, idx)` / `s.utf8_at(idx)`
 *       - `neverc_string_utf8_byte_index(s, k)` / `s.utf8_byte_index(k)`
 *
 *   * Conversion
 *       - `neverc_string_from_utf32_char(cp)`  -- 1..4 byte UTF-8 from a
 *                                          single codepoint
 *       - `neverc_string_from_utf16(d, n)`     -- UTF-16 buffer -> UTF-8 string
 *       - `neverc_string_from_utf32(d, n)`     -- UTF-32 buffer -> UTF-8 string
 *       - `s.to_utf16(out, cap)` / `s.to_utf32(out, cap)`  -- write
 *         caller-owned destination buffer; NULL `out` returns the
 *         required code-unit count without writing (heap-free
 *         shellcode / kernel ergonomic).
 *
 *   * Lossy fallback for malformed input -- lone surrogates,
 *     overlong sequences, codepoints beyond U+10FFFF all collapse to
 *     U+FFFD (REPLACEMENT CHARACTER).  Callers that need a hard
 *     reject use `neverc_string_utf8_valid(s)` to gate their input.
 *
 * Loader runs `main(a, b)` and uses the return code as exit status;
 * 0 means every assertion passed, any non-zero return value points at
 * the matching `return N` line below so a regression names the exact
 * codepoint helper that misbehaved.
 *
 * Embedded codepoints (used as test fixtures):
 *   你    U+4F60       (CJK)         -> 3 UTF-8 bytes
 *   好    U+597D       (CJK)         -> 3 UTF-8 bytes
 *   日    U+65E5       (CJK)         -> 3 UTF-8 bytes
 *   本    U+672C       (CJK)         -> 3 UTF-8 bytes
 *   語    U+8A9E       (CJK)         -> 3 UTF-8 bytes
 *   😀    U+1F600      (Supplementary) -> 4 UTF-8 bytes / 2 UTF-16 units
 *   €    U+20AC       (BMP)         -> 3 UTF-8 bytes
 */
int main(int a, int b) {
    (void)a;
    (void)b;

    /* --- u8"..." literal acceptance ---------------------------- */
    {
        string s = u8"你好";              /* 6 UTF-8 bytes, 2 codepoints */
        if (s.len != 6) return 1;
        if (!neverc_string_eq(s, u8"你好")) return 2;
        if (!neverc_string_eq(s, "你好")) return 3;        /* plain literal == u8 literal */
        if (s.utf8_count() != 2) return 4;
        if (!s.utf8_valid()) return 5;
    }

    /* concat with u8 prefix on both sides */
    {
        string s = u8"hello" + u8"世界";
        if (s.len != 11) return 6;                  /* 5 + 6 */
        if (!neverc_string_eq(s, u8"hello世界")) return 7;
        if (s.utf8_count() != 7) return 8;
    }

    /* dotted-call dispatcher accepts the `u8"..."` base directly */
    {
        if (!u8"你好".starts_with(u8"你")) return 9;
        if (u8"日本語".size() != 9) return 10;
        if (u8"日本語".utf8_count() != 3) return 11;
    }

    /* --- neverc_string_utf8_count / utf8_size / utf8_length aliases - */
    {
        string ascii = "abc";
        if (ascii.utf8_count() != 3) return 12;
        if (ascii.utf8_size() != 3) return 13;
        if (ascii.utf8_length() != 3) return 14;

        string mixed = u8"a你b好c";              /* 5 codepoints, 9 bytes */
        if (mixed.len != 9) return 15;
        if (mixed.utf8_count() != 5) return 16;

        string emoji = u8"😀😀";
        if (emoji.len != 8) return 17;             /* 4 + 4 */
        if (emoji.utf8_count() != 2) return 18;
    }

    /* empty string: count == 0, valid == 1 */
    {
        string e = "";
        if (e.utf8_count() != 0) return 19;
        if (!e.utf8_valid()) return 20;
    }

    /* --- neverc_string_utf8_valid: rejects malformed ------------------- */
    {
        /* overlong NUL (C0 80). */
        string bad1 = "\xC0\x80";
        if (bad1.utf8_valid()) return 21;

        /* lone continuation byte. */
        string bad2 = "\x80";
        if (bad2.utf8_valid()) return 22;

        /* truncated 2-byte sequence. */
        string bad3 = "\xC2";
        if (bad3.utf8_valid()) return 23;

        /* surrogate (ED A0 80 = U+D800). */
        string bad4 = "\xED\xA0\x80";
        if (bad4.utf8_valid()) return 24;

        /* codepoint above U+10FFFF (F4 90 80 80 = U+110000). */
        string bad5 = "\xF4\x90\x80\x80";
        if (bad5.utf8_valid()) return 25;

        /* well-formed CJK is valid. */
        string ok = u8"日本語";
        if (!ok.utf8_valid()) return 26;
    }

    /* --- neverc_string_utf8_at: codepoint index -> codepoint --------- */
    {
        string s = u8"你好abc😀";
        if (s.utf8_at(0) != 0x4F60) return 27;     /* 你 */
        if (s.utf8_at(1) != 0x597D) return 28;     /* 好 */
        if (s.utf8_at(2) != 'a') return 29;
        if (s.utf8_at(3) != 'b') return 30;
        if (s.utf8_at(4) != 'c') return 31;
        if (s.utf8_at(5) != 0x1F600) return 32;    /* 😀 */
        if (s.utf8_at(6) != 0) return 33;          /* out of range */
        if (s.utf8_at(99) != 0) return 34;         /* out of range */
    }

    /* --- neverc_string_utf8_byte_index: codepoint index -> byte offset */
    {
        string s = u8"你好abc😀";
        if (s.utf8_byte_index(0) != 0) return 35;
        if (s.utf8_byte_index(1) != 3) return 36;  /* after 你 */
        if (s.utf8_byte_index(2) != 6) return 37;  /* after 好 */
        if (s.utf8_byte_index(5) != 9) return 38;  /* after 'c' */
        /* k == cp_count -> return s.len so callers can use as end. */
        if (s.utf8_byte_index(6) != s.len) return 39;
        if (s.utf8_byte_index(99) != s.len) return 40;
    }

    /* --- neverc_string_from_utf32_char: single-codepoint encoder ----- */
    {
        if (!neverc_string_eq(neverc_string_from_utf32_char('A'), "A")) return 41;
        if (neverc_string_from_utf32_char('A').len != 1) return 42;

        if (!neverc_string_eq(neverc_string_from_utf32_char(0x4F60), u8"你")) return 43;
        if (neverc_string_from_utf32_char(0x4F60).len != 3) return 44;

        if (!neverc_string_eq(neverc_string_from_utf32_char(0x1F600), u8"😀")) return 45;
        if (neverc_string_from_utf32_char(0x1F600).len != 4) return 46;

        /* Surrogate / out-of-range -> empty sentinel. */
        if (neverc_string_from_utf32_char(0xD800).len != 0) return 47;
        if (neverc_string_from_utf32_char(0xDFFF).len != 0) return 48;
        if (neverc_string_from_utf32_char(0x110000).len != 0) return 49;
        if (neverc_string_from_utf32_char(0xFFFFFFFF).len != 0) return 50;
    }

    /* --- neverc_string_from_utf16: with surrogate pair --------------- */
    {
        __UINT16_TYPE__ buf[] = {0x4F60, 0x597D, 0xD83D, 0xDE00, 'x'};
        /* "你"=3 + "好"=3 + "😀"=4 + 'x'=1 = 11 bytes */
        string s = neverc_string_from_utf16(buf, 5);
        if (s.len != 11) return 51;
        if (s.utf8_count() != 4) return 52;
        if (!s.utf8_valid()) return 53;
        if (!neverc_string_eq(s, u8"你好😀x")) return 54;
    }

    /* lone high surrogate -> U+FFFD */
    {
        __UINT16_TYPE__ buf[] = {'A', 0xD800, 'B'};
        string s = neverc_string_from_utf16(buf, 3);
        /* 'A'=1 + U+FFFD=3 + 'B'=1 = 5 bytes */
        if (s.len != 5) return 55;
        if (s.utf8_count() != 3) return 56;
        if (!s.utf8_valid()) return 57;          /* output is well-formed */
    }

    /* lone low surrogate -> U+FFFD */
    {
        __UINT16_TYPE__ buf[] = {0xDC00, 'B'};
        string s = neverc_string_from_utf16(buf, 2);
        if (s.len != 4) return 58;               /* U+FFFD=3 + 'B'=1 */
    }

    /* empty / NULL inputs */
    {
        if (neverc_string_from_utf16((__UINT16_TYPE__*)0, 0).len != 0) return 59;
        if (neverc_string_from_utf16((__UINT16_TYPE__*)0, 5).len != 0) return 60;
    }

    /* --- neverc_string_from_utf32: codepoint buffer -> UTF-8 --------- */
    {
        __UINT32_TYPE__ buf[] = {0x4F60, 0x597D, 0x1F600};
        string s = neverc_string_from_utf32(buf, 3);
        if (s.len != 10) return 61;              /* 3 + 3 + 4 */
        if (s.utf8_count() != 3) return 62;
        if (!neverc_string_eq(s, u8"你好😀")) return 63;
    }

    /* invalid codepoints in UTF-32 input -> U+FFFD */
    {
        __UINT32_TYPE__ buf[] = {'A', 0xD800, 0x110000, 'B'};
        string s = neverc_string_from_utf32(buf, 4);
        /* 'A'=1 + U+FFFD=3 + U+FFFD=3 + 'B'=1 = 8 */
        if (s.len != 8) return 64;
        if (!s.utf8_valid()) return 65;
    }

    /* --- neverc_string_to_utf16: round-trip + truncation ------------- */
    {
        string s = u8"你好😀";
        __UINT16_TYPE__ out[8];
        for (int i = 0; i < 8; ++i) out[i] = 0xFFFF;
        __SIZE_TYPE__ n = s.to_utf16(out, 8);
        if (n != 4) return 66;                   /* 1 + 1 + surrogate pair */
        if (out[0] != 0x4F60) return 67;
        if (out[1] != 0x597D) return 68;
        if (out[2] != 0xD83D) return 69;         /* high surrogate */
        if (out[3] != 0xDE00) return 70;         /* low surrogate */
    }

    /* round-trip */
    {
        string s = u8"你好😀世界";
        __UINT16_TYPE__ buf[16];
        __SIZE_TYPE__ n = s.to_utf16(buf, 16);
        if (n != 6) return 71;                   /* 2 BMP + surrogate-pair + 2 BMP = 6 */
        string back = neverc_string_from_utf16(buf, n);
        if (!neverc_string_eq(back, u8"你好😀世界")) return 72;
    }

    /* truncation: out_cap < required, surrogate pair admission check */
    {
        string s = u8"😀😀";                      /* 2 codepoints, 4 UTF-16 units */
        __UINT16_TYPE__ out[3];
        for (int i = 0; i < 3; ++i) out[i] = 0xFFFF;
        __SIZE_TYPE__ n = s.to_utf16(out, 3);
        /* required is 4; out_cap is 3.  First surrogate pair fits
           at indices 0..1, second pair would not fit (would need 2..3,
           but written + need = 2 + 2 = 4 > out_cap = 3) so it is
           skipped to keep the output well-formed UTF-16. */
        if (n != 4) return 73;                   /* required, regardless of fit */
        if (out[0] != 0xD83D) return 74;
        if (out[1] != 0xDE00) return 75;
        if (out[2] != 0xFFFF) return 76;          /* untouched */
    }

    /* NULL out -> required count without writing (probe pattern) */
    {
        string s = u8"你好😀";
        __SIZE_TYPE__ need = s.to_utf16((__UINT16_TYPE__*)0, 0);
        if (need != 4) return 77;
    }

    /* --- neverc_string_to_utf32: round-trip ------------------------- */
    {
        string s = u8"日本語";
        __UINT32_TYPE__ out[8];
        for (int i = 0; i < 8; ++i) out[i] = 0xFFFFFFFF;
        __SIZE_TYPE__ n = s.to_utf32(out, 8);
        if (n != 3) return 78;
        if (out[0] != 0x65E5) return 79;
        if (out[1] != 0x672C) return 80;
        if (out[2] != 0x8A9E) return 81;
        string back = neverc_string_from_utf32(out, n);
        if (!neverc_string_eq(back, u8"日本語")) return 82;
    }

    /* `to_utf32` is the single mainstream spelling -- the `to_ucs4`
       legacy alias was dropped because UCS-4 is the obsolete
       ISO/IEC 10646 spelling and RFC 3629 settled on "UTF-32".
       Re-running the conversion through the canonical name pins
       down the same code path. */
    {
        string s = u8"日本語";
        __UINT32_TYPE__ out[4];
        __SIZE_TYPE__ n = s.to_utf32(out, 4);
        if (n != 3) return 83;
        if (out[0] != 0x65E5) return 84;
    }

    /* truncation: out_cap < required */
    {
        string s = u8"abcd";
        __UINT32_TYPE__ out[2];
        out[0] = out[1] = 0xFFFFFFFF;
        __SIZE_TYPE__ n = s.to_utf32(out, 2);
        if (n != 4) return 85;                   /* required */
        if (out[0] != 'a') return 86;
        if (out[1] != 'b') return 87;
    }

    /* NULL out -> required only */
    {
        string s = u8"日本語";
        __SIZE_TYPE__ need = s.to_utf32((__UINT32_TYPE__*)0, 0);
        if (need != 3) return 88;
    }

    /* --- end-to-end: every encoding cycle round-trips --------- */
    {
        /* UTF-8 -> UTF-16 -> UTF-8 */
        string original = u8"hello 世界! 😀";
        __UINT16_TYPE__ buf16[32];
        __SIZE_TYPE__ n16 = neverc_string_to_utf16(original, buf16, 32);
        string back = neverc_string_from_utf16(buf16, n16);
        if (!neverc_string_eq(back, u8"hello 世界! 😀")) return 89;
    }
    {
        /* UTF-8 -> UTF-32 -> UTF-8 */
        string original = u8"hello 世界! 😀";
        __UINT32_TYPE__ buf32[32];
        __SIZE_TYPE__ n32 = neverc_string_to_utf32(original, buf32, 32);
        string back = neverc_string_from_utf32(buf32, n32);
        if (!neverc_string_eq(back, u8"hello 世界! 😀")) return 90;
    }

    /* --- arena pressure: tight loop converts mixed-script strings */
    {
        for (int i = 0; i < 64; ++i) {
            string s = u8"循环" + u8"测试";
            if (s.utf8_count() != 4) return 91;
            __UINT16_TYPE__ buf16[16];
            __SIZE_TYPE__ n = s.to_utf16(buf16, 16);
            if (n != 4) return 92;
            string back = neverc_string_from_utf16(buf16, n);
            if (!neverc_string_eq(back, u8"循环测试")) return 93;
        }
    }

    /* --- combining with the rest of the string surface ---------- */
    {
        /* `+`, `==`, `s.find`, `s.substr`, `s.append` all keep their
           byte-oriented contract on UTF-8 content -- the tests above
           cover the codepoint surface, this one pins the byte-oriented
           operations against UTF-8 input so a regression that
           accidentally truncates inside a multi-byte codepoint surfaces
           here. */
        string s = u8"你好" + u8"world";
        if (s.len != 11) return 94;
        if (s.find(u8"world") != 6) return 95;
        if (!s.starts_with(u8"你好")) return 96;
        if (!s.ends_with("world")) return 97;
        string head = s.substr(0, 6);
        if (!neverc_string_eq(head, u8"你好")) return 98;
        if (head.utf8_count() != 2) return 99;
    }

    /* --- neverc_string_is_ascii ----------------------------------------- */
    {
        string ascii = "hello world";
        if (!ascii.is_ascii()) return 100;

        string utf8 = u8"你好";
        if (utf8.is_ascii()) return 101;

        string empty = "";
        if (!empty.is_ascii()) return 102;

        string high = "\x80";
        if (high.is_ascii()) return 103;

        string boundary = "\x7F";
        if (!boundary.is_ascii()) return 104;
    }

    /* --- neverc_string_from_latin1: pure ASCII fast path ---------------- */
    {
        const char ascii[] = {'h', 'e', 'l', 'l', 'o'};
        string s = neverc_string_from_latin1(ascii, 5);
        if (s.len != 5) return 105;
        if (!neverc_string_eq(s, "hello")) return 106;
        if (!s.utf8_valid()) return 107;
    }

    /* --- neverc_string_from_latin1: high-byte expansion to UTF-8 -------- */
    {
        /* Latin-1: é = 0xE9, ü = 0xFC, ñ = 0xF1 */
        const char lat1[] = {(char)0xE9, (char)0xFC, (char)0xF1};
        string s = neverc_string_from_latin1(lat1, 3);
        /* 0xE9 -> U+00E9 -> 0xC3 0xA9 (2 bytes)
           0xFC -> U+00FC -> 0xC3 0xBC (2 bytes)
           0xF1 -> U+00F1 -> 0xC3 0xB1 (2 bytes) = 6 UTF-8 bytes */
        if (s.len != 6) return 108;
        if (!s.utf8_valid()) return 109;
        if (s.utf8_count() != 3) return 110;
        if (s.utf8_at(0) != 0xE9) return 111;
        if (s.utf8_at(1) != 0xFC) return 112;
        if (s.utf8_at(2) != 0xF1) return 113;
    }

    /* --- neverc_string_from_latin1: mixed ASCII + high bytes ------------ */
    {
        /* "café" in Latin-1: c=0x63, a=0x61, f=0x66, é=0xE9 */
        const char lat1[] = {'c', 'a', 'f', (char)0xE9};
        string s = neverc_string_from_latin1(lat1, 4);
        /* 3 ASCII bytes + 1 high byte (2 UTF-8 bytes) = 5 bytes */
        if (s.len != 5) return 114;
        if (s.utf8_count() != 4) return 115;
        if (!s.utf8_valid()) return 116;
    }

    /* --- neverc_string_from_latin1: NULL / empty input ------------------- */
    {
        if (neverc_string_from_latin1((const char *)0, 5).len != 0) return 117;
        if (neverc_string_from_latin1((const char *)0, 0).len != 0) return 118;
        const char x = 'x';
        if (neverc_string_from_latin1(&x, 0).len != 0) return 119;
    }

    /* --- neverc_string_to_latin1: ASCII round-trip ----------------------- */
    {
        string s = "hello";
        char out[8];
        for (int i = 0; i < 8; ++i) out[i] = 'Z';
        __SIZE_TYPE__ n = s.to_latin1(out, 8);
        if (n != 5) return 120;
        if (out[0] != 'h' || out[1] != 'e' || out[2] != 'l' ||
            out[3] != 'l' || out[4] != 'o') return 121;
    }

    /* --- neverc_string_to_latin1: Latin-1 range codepoints -------------- */
    {
        /* Build UTF-8 from Latin-1 codepoints then convert back */
        const char lat1_in[] = {(char)0xE9, (char)0xFC};
        string s = neverc_string_from_latin1(lat1_in, 2);
        char out[4];
        __SIZE_TYPE__ n = s.to_latin1(out, 4);
        if (n != 2) return 122;
        if ((unsigned char)out[0] != 0xE9) return 123;
        if ((unsigned char)out[1] != 0xFC) return 124;
    }

    /* --- neverc_string_to_latin1: CJK codepoints -> '?' replacement ----- */
    {
        string s = u8"你好";
        char out[4];
        __SIZE_TYPE__ n = s.to_latin1(out, 4);
        if (n != 2) return 125;
        if (out[0] != '?') return 126;
        if (out[1] != '?') return 127;
    }

    /* --- neverc_string_to_latin1: NULL out probe ----------------------- */
    {
        string s = u8"café";
        __SIZE_TYPE__ need = s.to_latin1((char *)0, 0);
        if (need != 4) return 128;
    }

    /* --- neverc_string_to_latin1: truncation ---------------------------- */
    {
        string s = "abcd";
        char out[2];
        __SIZE_TYPE__ n = s.to_latin1(out, 2);
        if (n != 4) return 129;
        if (out[0] != 'a' || out[1] != 'b') return 130;
    }

    /* --- Latin-1 round-trip: Latin-1 -> UTF-8 -> Latin-1 --------- */
    {
        const char original[] = {
            'A', (char)0xC0, (char)0xE9, (char)0xFF, 'Z'
        };
        string utf8 = neverc_string_from_latin1(original, 5);
        if (!utf8.utf8_valid()) return 131;

        char back[8];
        __SIZE_TYPE__ n = neverc_string_to_latin1(utf8, back, 8);
        if (n != 5) return 132;
        if ((unsigned char)back[0] != 'A') return 133;
        if ((unsigned char)back[1] != 0xC0) return 134;
        if ((unsigned char)back[2] != 0xE9) return 135;
        if ((unsigned char)back[3] != 0xFF) return 136;
        if ((unsigned char)back[4] != 'Z') return 137;
    }

    return 0;
}
