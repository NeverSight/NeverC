// RUN: %neverc -std=c23 %s -o %t && %t
// test_neverc_string_wide_literal.c -- wide-literal string init + UTF-16 owned
//
// Pins down two related additions to the NeverC builtin `string`
// surface:
//
//   1. `string s = L"..."` / `string s = u"..."` / `string s = U"..."`
//      now compile and produce the same UTF-8 byte payload as the
//      `u8"..."` spelling.  Sema folds the source code-unit stream into
//      a UTF-8 byte sequence at compile time, so the runtime stays the
//      same byte-oriented `(data, len, cap)` triple — there is no
//      `neverc_string_from_utf16` call at runtime.  Wide / UTF-16 /
//      UTF-32 source bytes that are not representable in Unicode
//      (lone surrogates, codepoints above U+10FFFF) collapse to the
//      U+FFFD REPLACEMENT CHARACTER, matching the lossy contract of
//      `neverc_string_from_utf16`.
//
//   2. `s.to_utf16_owned()` returns a freshly allocated, NUL-terminated
//      `__UINT16_TYPE__ *` callers hand straight to Win32 `*W` APIs
//      (CreateFileW, MessageBoxW, ...).  Pair every non-NULL return
//      with `neverc_string_wfree(buf)` to release; the buffer is sized
//      `(unit_count + 1) * 2` bytes and routes through
//      `NEVERC_STRING_ALLOC` so shellcode mode reuses the arena
//      without a separate whitelist entry.
//
// Loader runs `main` and prints `ALL PASSED` on success; any failure
// returns a non-zero distinct number naming the assertion that
// regressed so a CI failure points at the exact line.
//
// Embedded codepoints (used as test fixtures):
//   你    U+4F60    -> 3 UTF-8 bytes / 1 UTF-16 unit
//   好    U+597D    -> 3 UTF-8 bytes / 1 UTF-16 unit
//   😀    U+1F600   -> 4 UTF-8 bytes / 2 UTF-16 units (D83D DE00)

#include <stdio.h>
#include <stddef.h>   /* wchar_t typedef -- required for s.w_str() */

int main(void) {
    /* --- 1. ASCII payload: L / u / U / u8 must produce identical bytes -- */
    {
        string a = L"GetPathW";
        string b = u"GetPathW";
        string c = U"GetPathW";
        string d = u8"GetPathW";
        string e = "GetPathW";

        if (a.len != 8) return 1;
        if (b.len != 8) return 2;
        if (c.len != 8) return 3;
        if (d.len != 8) return 4;
        if (e.len != 8) return 5;

        if (!neverc_string_eq(a, "GetPathW")) return 6;
        if (!neverc_string_eq(b, "GetPathW")) return 7;
        if (!neverc_string_eq(c, "GetPathW")) return 8;
        if (a != b) return 9;
        if (b != c) return 10;
        if (c != d) return 11;
        if (d != e) return 12;
    }

    /* --- 2. CJK BMP codepoints round-trip through every wide spelling --- */
    {
        string lw   = L"你好";   /* wide */
        string s16  = u"你好";   /* UTF-16 */
        string s32  = U"你好";   /* UTF-32 */
        string sref = u8"你好";  /* UTF-8 reference */

        /* 你 = 3 UTF-8 bytes, 好 = 3 UTF-8 bytes -> 6 bytes each */
        if (lw.len   != 6) return 13;
        if (s16.len  != 6) return 14;
        if (s32.len  != 6) return 15;
        if (sref.len != 6) return 16;

        if (!neverc_string_eq(lw,  u8"你好")) return 17;
        if (!neverc_string_eq(s16, u8"你好")) return 18;
        if (!neverc_string_eq(s32, u8"你好")) return 19;

        if (lw  != sref) return 20;
        if (s16 != sref) return 21;
        if (s32 != sref) return 22;

        /* dotted UTF-8 surface keeps reporting codepoint counts */
        if (lw.utf8_count()  != 2) return 23;
        if (s16.utf8_count() != 2) return 24;
        if (s32.utf8_count() != 2) return 25;
    }

    /* --- 3. Supplementary plane (emoji) -- surrogate-pair walk --------- */
    {
        /* 😀 = U+1F600 -> 4 UTF-8 bytes / 2 UTF-16 surrogate units */
        string lw  = L"😀";
        string s16 = u"😀";
        string s32 = U"😀";

        if (lw.len  != 4) return 26;
        if (s16.len != 4) return 27;
        if (s32.len != 4) return 28;

        if (!neverc_string_eq(lw,  u8"😀")) return 29;
        if (!neverc_string_eq(s16, u8"😀")) return 30;
        if (!neverc_string_eq(s32, u8"😀")) return 31;

        if (lw.utf8_count()  != 1) return 32;
        if (s16.utf8_count() != 1) return 33;
        if (s32.utf8_count() != 1) return 34;
    }

    /* --- 4. Mixed BMP + supplementary plane in a single literal -------- */
    {
        string s = L"你好😀x";
        /* 3 + 3 + 4 + 1 = 11 bytes */
        if (s.len != 11) return 35;
        if (!neverc_string_eq(s, u8"你好😀x")) return 36;
        if (s.utf8_count() != 4) return 37;
        if (!s.utf8_valid()) return 38;
    }

    /* Lone-surrogate fallback is not exercised at literal granularity
       because the C23 lexer rejects `\uD800` / `\uDC00` universal
       character names inside string literals.  The defensive U+FFFD
       fallback path inside `foldNeverCStringWideLiteralToUtf8` is
       still useful (it mirrors the runtime `from_utf16` lossy
       contract) but is unreachable from compile-time literals; the
       runtime side is covered by `test_neverc_string_utf8.c`. */

    /* --- 6. Wide-literal concat keeps semantics --------------------- */
    {
        string s = L"hello" + L"world";
        if (s.len != 10) return 43;
        if (!neverc_string_eq(s, "helloworld")) return 44;
    }
    {
        /* Cross-prefix concat: any combination must produce UTF-8 bytes. */
        string s = L"你好" + u8"world";
        if (s.len != 11) return 45;
        if (!neverc_string_eq(s, u8"你好world")) return 46;
    }

    /* --- 7. Empty wide literal --------------------------------------- */
    {
        string s = L"";
        if (s.len != 0) return 47;
        if (!neverc_string_eq(s, "")) return 48;
        if (!s.empty()) return 49;
    }

    /* --- 8. Dotted-surface methods on a wide-literal initialised string */
    {
        if (!L"OpenProcess".starts_with("Open")) return 50;
        if (!u"GetProcAddress".ends_with("Address")) return 51;
        if (L"CreateFileW".len() != 11) return 52;
        if (u"日本語".utf8_count() != 3) return 53;
    }

    /* === to_utf16_owned + neverc_string_wfree =========================== */

    /* --- 9. ASCII payload: round-trip through to_utf16_owned ----------- */
    {
        string s = L"GetPathW";
        __UINT16_TYPE__ *w = s.to_utf16_owned();
        if (!w) return 60;

        const char *expected = "GetPathW";
        for (int i = 0; i < 8; ++i) {
            if (w[i] != (__UINT16_TYPE__)(unsigned char)expected[i])
                return 61;
        }
        if (w[8] != 0) return 62;          /* NUL terminator */

        /* Verify against the caller-owned `to_utf16` path. */
        __UINT16_TYPE__ ref[16];
        string s2 = "GetPathW";
        __SIZE_TYPE__ n = s2.to_utf16(ref, 16);
        if (n != 8) return 63;
        for (int i = 0; i < 8; ++i)
            if (w[i] != ref[i]) return 64;

        neverc_string_wfree(w);
    }

    /* --- 10. CJK round-trip: BMP -> single-unit UTF-16 ----------------- */
    {
        string s = u8"你好";
        __UINT16_TYPE__ *w = s.to_utf16_owned();
        if (!w) return 65;

        if (w[0] != 0x4F60) return 66;     /* 你 */
        if (w[1] != 0x597D) return 67;     /* 好 */
        if (w[2] != 0)      return 68;     /* NUL */

        neverc_string_wfree(w);
    }

    /* --- 11. Emoji round-trip: surrogate pair encoding ----------------- */
    {
        string s = u8"😀";
        __UINT16_TYPE__ *w = s.to_utf16_owned();
        if (!w) return 69;

        if (w[0] != 0xD83D) return 70;     /* high surrogate */
        if (w[1] != 0xDE00) return 71;     /* low surrogate  */
        if (w[2] != 0)      return 72;     /* NUL */

        neverc_string_wfree(w);
    }

    /* --- 12. Mixed BMP + emoji + ASCII --------------------------------- */
    {
        string s = u8"hi你好😀!";
        __UINT16_TYPE__ *w = s.to_utf16_owned();
        if (!w) return 73;

        /* Expected: h(1) i(1) 你(1) 好(1) high(1) low(1) !(1) NUL = 7 + 1 */
        if (w[0] != 'h') return 74;
        if (w[1] != 'i') return 75;
        if (w[2] != 0x4F60) return 76;
        if (w[3] != 0x597D) return 77;
        if (w[4] != 0xD83D) return 78;
        if (w[5] != 0xDE00) return 79;
        if (w[6] != '!') return 80;
        if (w[7] != 0) return 81;          /* NUL */

        neverc_string_wfree(w);
    }

    /* --- 13. Empty string -> { 0 } single-NUL buffer ------------------- */
    {
        string s = "";
        __UINT16_TYPE__ *w = s.to_utf16_owned();
        if (!w) return 82;
        if (w[0] != 0) return 83;
        neverc_string_wfree(w);
    }

    /* --- 14. neverc_string_wfree(NULL) is a no-op ---------------------- */
    {
        __UINT16_TYPE__ *null_ptr = (__UINT16_TYPE__ *)0;
        neverc_string_wfree(null_ptr);
        /* Reaching here without a crash is the assertion. */
    }

    /* --- 15. Composes with the rest of the surface ------------------- */
    {
        /* Build a wide string by chaining mutating ops, then convert. */
        string s = L"hello";
        s = s + L" world";
        if (s.len != 11) return 84;

        __UINT16_TYPE__ *w = s.to_utf16_owned();
        if (!w) return 85;
        const char *expected = "hello world";
        for (int i = 0; i < 11; ++i) {
            if (w[i] != (__UINT16_TYPE__)(unsigned char)expected[i])
                return 86;
        }
        if (w[11] != 0) return 87;
        neverc_string_wfree(w);
    }

    /* --- 16. Chained dotted-call: substr -> to_utf16_owned ------------- */
    {
        string s = L"prefix:GetPathW";
        __UINT16_TYPE__ *w = s.substr(7).to_utf16_owned();
        if (!w) return 88;
        const char *expected = "GetPathW";
        for (int i = 0; i < 8; ++i)
            if (w[i] != (__UINT16_TYPE__)expected[i]) return 89;
        if (w[8] != 0) return 90;
        neverc_string_wfree(w);
    }

    /* --- 17. Probe-shape parity: required count from to_utf16(NULL) == */
    /*       buffer length from to_utf16_owned                            */
    {
        string a = u8"hello 世界! 😀";
        __SIZE_TYPE__ need = neverc_string_to_utf16(neverc_string_clone(a),
                                                    (__UINT16_TYPE__ *)0, 0);
        __UINT16_TYPE__ *w = a.to_utf16_owned();
        if (!w) return 91;
        /* Walk to the trailing NUL and count units; should match `need`. */
        __SIZE_TYPE__ count = 0;
        while (w[count]) count++;
        if (count != need) return 92;
        neverc_string_wfree(w);
    }

    /* === to_utf32_owned: fixed-width codepoint surface ================== */

    /* --- 18. ASCII -> UTF-32 round-trip -------------------------------- */
    {
        string s = "GetPathW";
        __UINT32_TYPE__ *u = s.to_utf32_owned();
        if (!u) return 100;
        const char *expected = "GetPathW";
        for (int i = 0; i < 8; ++i)
            if (u[i] != (__UINT32_TYPE__)(unsigned char)expected[i])
                return 101;
        if (u[8] != 0) return 102;
        neverc_string_wfree(u);
    }

    /* --- 19. CJK + emoji: each codepoint takes exactly 1 UTF-32 unit --- */
    {
        string s = u8"你好😀";
        __UINT32_TYPE__ *u = s.to_utf32_owned();
        if (!u) return 103;
        if (u[0] != 0x4F60u)  return 104;  /* 你 */
        if (u[1] != 0x597Du)  return 105;  /* 好 */
        if (u[2] != 0x1F600u) return 106;  /* 😀 — no surrogate split */
        if (u[3] != 0)        return 107;  /* NUL */
        neverc_string_wfree(u);
    }

    /* --- 20. Empty string -> single-NUL UTF-32 buffer ------------------ */
    {
        string s = "";
        __UINT32_TYPE__ *u = s.to_utf32_owned();
        if (!u) return 108;
        if (u[0] != 0) return 109;
        neverc_string_wfree(u);
    }

    /* === w_str: platform-adaptive wide string =========================== */

    /* --- 21. ASCII payload: round-trip through w_str ------------------- */
    {
        string s = L"GetPathW";
        wchar_t *w = s.w_str();
        if (!w) return 110;
        const wchar_t expected[] = L"GetPathW";
        for (int i = 0; i < 8; ++i)
            if (w[i] != expected[i]) return 111;
        if (w[8] != 0) return 112;          /* NUL terminator */
        neverc_string_wfree(w);
    }

    /* --- 22. CJK round-trip: encoding adapts to platform wchar_t ------- */
    {
        string s = u8"你好";
        wchar_t *w = s.w_str();
        if (!w) return 113;
        if (w[0] != (wchar_t)0x4F60) return 114;
        if (w[1] != (wchar_t)0x597D) return 115;
        if (w[2] != 0) return 116;
        neverc_string_wfree(w);
    }

    /* --- 23. Emoji: 16-bit wchar_t emits surrogate pair, 32-bit emits  */
    /*       a single codepoint.  Probe `__SIZEOF_WCHAR_T__` to pick the */
    /*       right invariant rather than hard-coding either layout.      */
    {
        string s = u8"😀";
        wchar_t *w = s.w_str();
        if (!w) return 117;
#if __SIZEOF_WCHAR_T__ == 2
        if ((__UINT16_TYPE__)w[0] != 0xD83Du) return 118;
        if ((__UINT16_TYPE__)w[1] != 0xDE00u) return 119;
        if (w[2] != 0) return 120;
#elif __SIZEOF_WCHAR_T__ == 4
        if ((__UINT32_TYPE__)w[0] != 0x1F600u) return 118;
        if (w[1] != 0) return 119;
#endif
        neverc_string_wfree(w);
    }

    /* --- 24. wfree absorbs every pointer type via void * --------------- */
    /*        (uint16, uint32, wchar_t).  The free helper takes void *,   */
    /*        so any object pointer implicitly converts and is released   */
    /*        through the same allocator hook.                            */
    {
        __UINT16_TYPE__ *w16 = u8"abc".to_utf16_owned();
        __UINT32_TYPE__ *w32 = u8"abc".to_utf32_owned();
        wchar_t        *ww  = u8"abc".w_str();
        if (!w16 || !w32 || !ww) return 121;
        neverc_string_wfree(w16);
        neverc_string_wfree(w32);
        neverc_string_wfree(ww);
    }

    /* === data() now returns void *, sharing storage with c_str() ======= */

    /* --- 25. data() and c_str() alias the same backing buffer ---------- */
    {
        string s = "hello world";
        const char *via_cstr = s.c_str();
        const char *via_data = (const char *)s.data();
        if (via_cstr != via_data) return 130;
        if (via_data[6] != 'w') return 131;
        if (via_data[10] != 'd') return 132;
        if (via_data[11] != 0) return 133;     /* NUL terminator */
    }

    /* --- 26. data() falls back to "" sentinel on the empty receiver --- */
    {
        string s = "";
        const char *p = (const char *)s.data();
        if (!p) return 134;
        if (p[0] != 0) return 135;
    }

    /* --- 27. data() pointer feeds straight into a void *-shaped sink -- */
    /*        without an explicit cast (this is the whole point of the   */
    /*        void * return type).                                       */
    {
        string s = "abcdef";
        char dst[7];
        __builtin_memcpy(dst, s.data(), 6);
        dst[6] = 0;
        if (!neverc_string_eq(neverc_string_view(dst, 6), "abcdef"))
            return 136;
    }

    printf("test_neverc_string_wide_literal: ALL PASSED\n");
    return 0;
}
