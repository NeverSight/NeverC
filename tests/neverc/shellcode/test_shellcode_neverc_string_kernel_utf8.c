// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* NeverC builtin string UTF-8 / Unicode surface, ring-0 mirror.
 *
 * Compile-only guard for `-mshellcode-context=kernel`: every UTF-8 /
 * UTF-16 / UTF-32 helper the user-mode test exercises must also lower
 * into the smaller kernel arena (4 KB) without leaving a libc / mem*
 * heap extern that KernelImportPass would route through
 * `__neverc_kern_resolve`.  The body is a scaled-down copy of the
 * user-mode tour -- every loop and intermediate buffer fits inside the
 * tighter arena while still touching every public encoding helper at
 * least once so a regression in the dispatcher (or one of the
 * def-table consumers behind it) shows up next to the matching method
 * spelling rather than inside a tangential lifetime test.
 *
 * Why this duplicates the user test
 * =================================
 *
 * Same answer as every other `_kernel_<tag>.c` file in this suite: the
 * user-mode test uses `int main(int a, int b)` and runs through the
 * JIT loader, while the kernel-mode test uses
 * `int shellcode_entry(int seed)` and is driven exclusively by
 * `-mshellcode-context=kernel` codegen.  The two are not mergable into
 * one file because the entry-symbol contract (and the StringRuntimePass
 * arena size) differ across the two pipelines.
 */
int shellcode_entry(int seed) {
    /* u8"..." literal initialisation + dotted-call dispatch */
    {
        string s = u8"你好";
        if (s.len != 6) return seed + 1;
        if (s.utf8_count() != 2) return seed + 2;
        if (!s.utf8_valid()) return seed + 3;
        if (s.utf8_at(0) != 0x4F60) return seed + 4;
    }

    /* validation rejects malformed UTF-8 */
    {
        string ok = u8"日本";
        if (!ok.utf8_valid()) return seed + 5;
        string bad = "\xC0\x80";
        if (bad.utf8_valid()) return seed + 6;
    }

    /* codepoint -> byte index round-trip */
    {
        string s = u8"a你b";
        if (s.utf8_byte_index(0) != 0) return seed + 7;
        if (s.utf8_byte_index(1) != 1) return seed + 8;
        if (s.utf8_byte_index(2) != 4) return seed + 9;
        if (s.utf8_byte_index(3) != s.len) return seed + 10;
    }

    /* single-codepoint encoding */
    {
        if (neverc_string_from_utf32_char('Z').len != 1) return seed + 11;
        if (neverc_string_from_utf32_char(0x4F60).len != 3) return seed + 12;
        if (neverc_string_from_utf32_char(0x1F600).len != 4) return seed + 13;
        if (neverc_string_from_utf32_char(0xD800).len != 0) return seed + 14;
    }

    /* UTF-16 -> UTF-8 with surrogate pair (heap-free buffer) */
    {
        __UINT16_TYPE__ buf[3];
        buf[0] = 0x4F60;
        buf[1] = 0xD83D;
        buf[2] = 0xDE00;
        string s = neverc_string_from_utf16(buf, 3);
        if (s.len != 7) return seed + 15;
        if (s.utf8_count() != 2) return seed + 16;
        if (!s.utf8_valid()) return seed + 17;
    }

    /* lone surrogate falls back to U+FFFD */
    {
        __UINT16_TYPE__ buf[2];
        buf[0] = 0xD800;
        buf[1] = 'A';
        string s = neverc_string_from_utf16(buf, 2);
        if (s.len != 4) return seed + 18;
    }

    /* UTF-32 -> UTF-8 + invalid codepoint replacement */
    {
        __UINT32_TYPE__ buf[3];
        buf[0] = 0x4F60;
        buf[1] = 0xD800;
        buf[2] = 0x110000;
        string s = neverc_string_from_utf32(buf, 3);
        if (s.len != 9) return seed + 19;        /* 3 + 3 (FFFD) + 3 (FFFD) */
    }

    /* round-trip UTF-8 -> UTF-16 -> UTF-8 (small arena-friendly path) */
    {
        string s = u8"循环";                     /* 2 codepoints, 6 bytes */
        __UINT16_TYPE__ buf16[4];
        __SIZE_TYPE__ n = s.to_utf16(buf16, 4);
        if (n != 2) return seed + 20;
        string back = neverc_string_from_utf16(buf16, n);
        if (!neverc_string_eq(back, u8"循环")) return seed + 21;
    }

    /* round-trip UTF-8 -> UTF-32 -> UTF-8 */
    {
        string s = u8"测试";
        __UINT32_TYPE__ buf32[4];
        __SIZE_TYPE__ n = s.to_utf32(buf32, 4);
        if (n != 2) return seed + 22;
        if (buf32[0] != 0x6D4B) return seed + 23;
        if (buf32[1] != 0x8BD5) return seed + 24;
        string back = neverc_string_from_utf32(buf32, n);
        if (!neverc_string_eq(back, u8"测试")) return seed + 25;
    }

    /* truncation: out_cap < required keeps surrogate pairs intact */
    {
        string s = u8"😀";
        __UINT16_TYPE__ out[1];
        out[0] = 0xFFFF;
        __SIZE_TYPE__ n = s.to_utf16(out, 1);
        if (n != 2) return seed + 26;            /* required, even on truncation */
        if (out[0] != 0xFFFF) return seed + 27;  /* surrogate pair refused */
    }

    /* NULL out -> required count only */
    {
        string s = u8"日本語";
        __SIZE_TYPE__ need = s.to_utf32((__UINT32_TYPE__*)0, 0);
        if (need != 3) return seed + 28;
    }

    /* arena pressure: tight loop converts mixed-script strings.  A
       smaller iteration count than the user-mode mirror keeps every
       transient owned buffer inside the 4 KB kernel arena. */
    for (int i = 0; i < 16; ++i) {
        string s = u8"循" + u8"环";
        if (s.utf8_count() != 2) return seed + 29;
        __UINT16_TYPE__ buf16[8];
        __SIZE_TYPE__ n = s.to_utf16(buf16, 8);
        if (n != 2) return seed + 30;
    }

    /* byte-oriented operations stay correct on UTF-8 content */
    {
        string s = u8"你好" + u8"abc";
        if (s.len != 9) return seed + 31;
        if (s.find(u8"abc") != 6) return seed + 32;
        if (!s.starts_with(u8"你好")) return seed + 33;
    }

    /* ASCII predicate */
    {
        string ascii = "hello";
        if (!ascii.is_ascii()) return seed + 34;
        string utf8 = u8"你";
        if (utf8.is_ascii()) return seed + 35;
        string empty = "";
        if (!empty.is_ascii()) return seed + 36;
    }

    /* Latin-1 -> UTF-8 conversion */
    {
        const char lat1[] = {'A', (char)0xE9};
        string s = neverc_string_from_latin1(lat1, 2);
        if (s.len != 3) return seed + 37;
        if (!s.utf8_valid()) return seed + 38;
        if (s.utf8_at(0) != 'A') return seed + 39;
        if (s.utf8_at(1) != 0xE9) return seed + 40;
    }

    /* UTF-8 -> Latin-1 conversion (lossy for CJK) */
    {
        string s = u8"A\xC3\xA9";
        char out[4];
        __SIZE_TYPE__ n = s.to_latin1(out, 4);
        if (n != 2) return seed + 41;
        if (out[0] != 'A') return seed + 42;
        if ((unsigned char)out[1] != 0xE9) return seed + 43;
    }

    /* CJK -> Latin-1 lossy replacement */
    {
        string s = u8"你";
        char out[2];
        __SIZE_TYPE__ n = s.to_latin1(out, 2);
        if (n != 1) return seed + 44;
        if (out[0] != '?') return seed + 45;
    }

    return seed;
}
