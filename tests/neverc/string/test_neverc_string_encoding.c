// RUN: %neverc -std=c23 %s -o %t && %t
/* test_neverc_string_encoding.c -- builtin string encoding round-trips.
 *
 * Reference for the "ergonomic surface" of the NeverC builtin string
 * type.  Every operation goes through one of:
 *
 *   * dotted method call         -- `s.to_base64()`, `s.utf8_count()`,
 *                                   `"foo".url_encode()` (string
 *                                   literals are already `string`
 *                                   values, so dotted calls land on
 *                                   them directly).
 *   * operator                   -- `s == t`, `s + t`, `s += t`.
 *   * explicit `neverc_` prefix  -- `neverc_string_view(d, n)`,
 *                                   `neverc_string_from_utf16(d, n)`,
 *                                   `neverc_string_from_latin1(d, n)`.
 *
 * The bare `string_eq` / `string_to_base64` / `string_view` spellings
 * are deliberately NOT visible: an ordinary C translation unit can keep
 * declaring `int string_eq(int, int);` of its own without colliding
 * with the prelude.  `test_neverc_string_namespace.c` pins that down
 * with an actual user-defined `string_eq` / `string_view` /
 * `STRING_NPOS` at file scope.
 *
 * Coverage:
 *
 *   * `u8"..."` literals + plain narrow literals -- both round-trip
 *     through the value type byte-for-byte, so `string s = u8"你好"`
 *     and `string t = "你好"` are interchangeable.  This is the Qt
 *     QString ergonomic the user asked for ("qstring s1 = u8\"你好\"").
 *
 *   * Codepoint surface (`s.utf8_count()`, `s.utf8_at()`,
 *     `s.utf8_valid()`, `s.utf8_byte_index()`).
 *
 *   * UTF-16 / UTF-32 / Latin-1 round trips through caller-owned
 *     destination buffers.
 *
 *   * Base64 (RFC 4648 §4 standard alphabet, `=` padding).
 *
 *   * Hex (lowercase encode, case-insensitive decode).
 *
 *   * URL percent-encoding (RFC 3986 unreserved set + uppercase
 *     `%XX` escapes).
 *
 *   * application/x-www-form-urlencoded form encoding (' ' -> '+',
 *     '+' -> '%2B').
 *
 * Every helper consumes the receiver and returns a fresh owned string,
 * so the entire test runs end-to-end without a manual `s.free()` --
 * the `free` calls are emitted by the dispatcher around each by-value
 * `string` temporary.
 */

int main(void) {
    int r = 0;

    /* ===== UTF-8 literal acceptance + codepoint surface ===== */
    string s_zh = u8"你好";
    if (s_zh.len != 6) r = 1;
    if (s_zh.utf8_count() != 2) r = 1;
    if (s_zh.utf8_at(0) != 0x4F60) r = 1;
    if (s_zh.utf8_at(1) != 0x597D) r = 1;
    if (!s_zh.utf8_valid()) r = 1;
    if (s_zh.is_ascii()) r = 1;

    /* Plain narrow literal == u8 literal at the byte level */
    string s_plain = "你好";
    if (s_plain.len != 6) r = 1;
    if (s_plain != s_zh) r = 1;

    /* Multi-codepoint mix: ASCII + CJK + supplementary plane */
    string s_mix = u8"a你好😀b";
    /* a (1) + 你 (3) + 好 (3) + 😀 (4) + b (1) = 12 bytes, 5 codepoints */
    if (s_mix.len != 12) r = 1;
    if (s_mix.utf8_count() != 5) r = 1;
    if (s_mix.utf8_at(0) != 'a') r = 1;
    if (s_mix.utf8_at(1) != 0x4F60) r = 1;
    if (s_mix.utf8_at(2) != 0x597D) r = 1;
    if (s_mix.utf8_at(3) != 0x1F600) r = 1;
    if (s_mix.utf8_at(4) != 'b') r = 1;
    if (!s_mix.utf8_valid()) r = 1;

    /* Byte index for codepoint slicing */
    if (s_zh.utf8_byte_index(0) != 0) r = 1;
    if (s_zh.utf8_byte_index(1) != 3) r = 1;
    if (s_zh.utf8_byte_index(2) != 6) r = 1;

    /* Codepoint-aware substring -- the QString::mid / Python str[a:b]
       analogue.  Walks the source by codepoint units instead of byte
       units, so a CJK / supplementary-plane payload no longer needs
       the caller to convert through utf8_byte_index() first. */
    if (u8"a你好😀b".utf8_substr(1, 2) != u8"你好") r = 1;
    if (u8"a你好😀b".utf8_substr(0, 1) != "a") r = 1;
    if (u8"a你好😀b".utf8_substr(3, 1) != u8"😀") r = 1;
    if (u8"a你好😀b".utf8_substr(4) != "b") r = 1;
    /* 1-arg form -> "to end" via the SizeAllOnes default-arg row. */
    if (u8"a你好😀b".utf8_substr(2) != u8"好😀b") r = 1;
    /* Out-of-range start clips to empty -- matches std::string::substr's
       lossy clipping when `pos >= size()`. */
    if (u8"a你好😀b".utf8_substr(99).len != 0) r = 1;
    /* Zero-count -> empty regardless of start. */
    if (u8"a你好😀b".utf8_substr(0, 0).len != 0) r = 1;
    /* Codepoint-aware slicing keeps a single mainstream spelling --
       the JS-style `utf8_slice` and the legacy `code_point_substr`
       alias rows were dropped from the dotted-method table; the same
       logical operation lives behind `utf8_substr`. */
    if (u8"a你好😀b".utf8_substr(1, 2) != u8"你好") r = 1;
    if (u8"a你好😀b".utf8_substr(3, 1) != u8"😀") r = 1;
    /* Pure ASCII payload is just substr in disguise -- byte and
       codepoint indices coincide so the two surfaces agree. */
    if ("hello world".utf8_substr(6, 5) != "world") r = 1;

    /* Lone continuation byte (0x80) is malformed UTF-8.  The factory
       call uses the explicit `neverc_` prefix because a binary buffer
       is not a string literal we can dotted-call into. */
    string s_bad = neverc_string_view("\x80", 1);
    if (s_bad.utf8_valid()) r = 1;
    /* utf8_count counts one tally per malformed byte (Python parity) */
    if (neverc_string_view("\x80", 1).utf8_count() != 1) r = 1;

    /* ===== UTF-16 round trip ===== */
    {
        __UINT16_TYPE__ buf16[16];
        __SIZE_TYPE__ n = "Hi 世界!".to_utf16(buf16, 16);
        /* H i sp 世 界 ! */
        if (n != 6) r = 1;
        if (buf16[0] != 'H' || buf16[1] != 'i' || buf16[2] != ' ') r = 1;
        if (buf16[3] != 0x4E16 || buf16[4] != 0x754C) r = 1;
        if (buf16[5] != '!') r = 1;
        /* Probe: NULL out -> required length without writing */
        if ("Hi 世界!".to_utf16((__UINT16_TYPE__ *)0, 0) != 6) r = 1;
        /* Round trip back to UTF-8 */
        string back = neverc_string_from_utf16(buf16, n);
        if (back != "Hi 世界!") r = 1;
    }

    /* Supplementary plane -> surrogate pair */
    {
        __UINT16_TYPE__ buf16[8];
        __SIZE_TYPE__ n = "😀".to_utf16(buf16, 8);
        if (n != 2) r = 1;
        if (buf16[0] != 0xD83D || buf16[1] != 0xDE00) r = 1;
        string back = neverc_string_from_utf16(buf16, n);
        if (back != "😀") r = 1;
    }

    /* Lone high surrogate -> U+FFFD on UTF-16 -> UTF-8 conversion */
    {
        __UINT16_TYPE__ buf16[1];
        buf16[0] = 0xD800;
        string s = neverc_string_from_utf16(buf16, 1);
        if (s.utf8_at(0) != 0xFFFD) r = 1;
    }

    /* ===== UTF-32 round trip ===== */
    {
        __UINT32_TYPE__ buf32[8];
        __SIZE_TYPE__ n = "a😀b".to_utf32(buf32, 8);
        if (n != 3) r = 1;
        if (buf32[0] != 'a' || buf32[1] != 0x1F600 || buf32[2] != 'b') r = 1;
        string back = neverc_string_from_utf32(buf32, n);
        if (back != "a😀b") r = 1;
    }

    /* Out-of-range codepoint in UTF-32 -> U+FFFD */
    {
        __UINT32_TYPE__ buf32[1];
        buf32[0] = 0x110000;
        string s = neverc_string_from_utf32(buf32, 1);
        if (s.utf8_at(0) != 0xFFFD) r = 1;
    }

    /* Single codepoint -> 1..4 byte UTF-8 */
    {
        if (neverc_string_from_utf32_char(0x41) != "A") r = 1;
        if (neverc_string_from_utf32_char(0x4F60) != u8"你") r = 1;
        if (neverc_string_from_utf32_char(0x1F600) != u8"😀") r = 1;
        /* Surrogate / out-of-range -> empty sentinel */
        if (neverc_string_from_utf32_char(0xD800).len != 0) r = 1;
        if (neverc_string_from_utf32_char(0x110000).len != 0) r = 1;
    }

    /* ===== Latin-1 round trip ===== */
    {
        /* "café" in Latin-1 is 4 bytes: 'c' 'a' 'f' 0xE9 */
        char latin1[4];
        latin1[0] = 'c'; latin1[1] = 'a'; latin1[2] = 'f';
        latin1[3] = (char)0xE9;
        string utf8 = neverc_string_from_latin1(latin1, 4);
        /* c a f + 0xC3 0xA9 */
        if (utf8.len != 5) r = 1;
        if (utf8.utf8_at(3) != 0xE9) r = 1;
        char back[8];
        __SIZE_TYPE__ n = utf8.to_latin1(back, 8);
        if (n != 4) r = 1;
        if (back[0] != 'c' || back[1] != 'a' || back[2] != 'f') r = 1;
        if ((unsigned char)back[3] != 0xE9) r = 1;

        /* Codepoint > 0x100 -> '?' on toLatin1 fallback */
        char back2[8];
        __SIZE_TYPE__ n2 = "中".to_latin1(back2, 8);
        if (n2 != 1) r = 1;
        if (back2[0] != '?') r = 1;
    }

    /* ===== Base64 (RFC 4648 §4 standard test vectors) ===== */
    if ("".to_base64() != "") r = 1;
    if ("f".to_base64() != "Zg==") r = 1;
    if ("fo".to_base64() != "Zm8=") r = 1;
    if ("foo".to_base64() != "Zm9v") r = 1;
    if ("foob".to_base64() != "Zm9vYg==") r = 1;
    if ("fooba".to_base64() != "Zm9vYmE=") r = 1;
    if ("foobar".to_base64() != "Zm9vYmFy") r = 1;
    /* Round trip */
    if ("foobar".to_base64().from_base64() != "foobar") r = 1;
    /* Single mainstream Qt-style spelling -- the PHP-flavour
       `base64_encode` / `base64_decode` aliases were dropped from
       the dotted-method table; users get exactly one obvious
       spelling per concept. */
    /* CJK round-trip -- bytes are opaque to base64 */
    if (u8"你好".to_base64().from_base64() != u8"你好") r = 1;
    /* Strict rejection */
    /* not multiple of 4 */
    if ("Zm9vYmF".from_base64().len != 0) r = 1;
    /* bad char */
    if ("Zm9vYmF!".from_base64().len != 0) r = 1;
    /* = mid-stream */
    if ("Zm==Zg==".from_base64().len != 0) r = 1;
    /* 3 trailing = */
    if ("Z===".from_base64().len != 0) r = 1;
    /* leading = */
    if ("=AAA".from_base64().len != 0) r = 1;

    /* ===== Base32 (RFC 4648 §6 / §10 standard test vectors) =====
       The Base32 alphabet (A-Z + 2-7, 5 bits per char, 8 chars per
       5 source bytes) is the encoding TOTP / Google Authenticator
       (RFC 6238 §3) and DNS-SD instance-name labels (RFC 6763
       §4.1.3) use.  These are the §10 reference vectors so a
       conformant decoder must agree byte-for-byte. */
    if ("".to_base32() != "") r = 1;
    if ("f".to_base32() != "MY======") r = 1;
    if ("fo".to_base32() != "MZXQ====") r = 1;
    if ("foo".to_base32() != "MZXW6===") r = 1;
    if ("foob".to_base32() != "MZXW6YQ=") r = 1;
    if ("fooba".to_base32() != "MZXW6YTB") r = 1;
    if ("foobar".to_base32() != "MZXW6YTBOI======") r = 1;
    /* Round trip */
    if ("foobar".to_base32().from_base32() != "foobar") r = 1;
    /* CJK round trip -- bytes are opaque to base32 */
    if (u8"你好".to_base32().from_base32() != u8"你好") r = 1;
    /* Encoder is upper-case; decoder accepts both cases (the
       case-folding tolerance every TOTP / DNS-SD reader follows). */
    if ("mzxw6ytboi======".from_base32() != "foobar") r = 1;
    if ("MzXw6YtBoI======".from_base32() != "foobar") r = 1;
    /* Multi-group round trip with the last group carrying padding.
       16 source bytes split as 3 groups (5+5+5 bytes -> 8+8+8 chars
       with no pad) plus a 1-byte tail (-> 8 chars with pad=6). */
    {
        string sixteen = "abcdefghijklmnop";
        string b32 = sixteen.to_base32();
        if (b32.len != 32) r = 1;
        if (b32.from_base32() != "abcdefghijklmnop") r = 1;
    }
    /* Strict rejection contract */
    /* not multiple of 8 */
    if ("MY=====".from_base32().len != 0) r = 1;
    /* legal pad counts are exactly {0, 1, 3, 4, 6}; 2/5/7 forbidden.
       Use a plain 8-char tail with the disallowed pad to isolate
       the rule (mid-stream `=` would otherwise mask it). */
    if ("MFRGGZ==".from_base32().len != 0) r = 1; /* pad=2 forbidden */
    if ("MFR=====".from_base32().len != 0) r = 1; /* pad=5 forbidden */
    if ("M=======".from_base32().len != 0) r = 1; /* pad=7 forbidden */
    /* non-last group MUST NOT contain `=` -- 16-char input where the
       first 8 chars carry pad is rejected even though the trailing
       group is well-formed. */
    if ("MFRG====AAAAAAAA".from_base32().len != 0) r = 1;
    if ("MFRGG===AAAAAAAA".from_base32().len != 0) r = 1;
    /* non-alphabet byte */
    if ("MZXW6YTBO!======".from_base32().len != 0) r = 1;
    /* digit `1` is intentionally not in the Base32 alphabet (avoids
       O/0, I/1 ambiguity that the alphabet was designed to dodge). */
    if ("MZXW6Y1B".from_base32().len != 0) r = 1;
    /* mid-stream `=` in the last group: last group's pad=1 leaves
       slot 0..6 as alphabet, so an `=` at slot 2 is forbidden. */
    if ("MZ==MZX=".from_base32().len != 0) r = 1;
    /* leading `=` in any group is never legal (slot 0/1 are never
       pad slots regardless of pad count). */
    if ("=ZXW6YTB".from_base32().len != 0) r = 1;
    /* `to_base32` / `from_base32` is the single mainstream spelling
       (Qt's QByteArray has no Base32, cppcodec ships
       `base32_rfc4648` exactly here) -- the PHP-flavour
       `base32_encode` / `base32_decode` aliases were dropped to
       keep one obvious dotted name per concept. */

    /* ===== Hex ===== */
    if ("".to_hex() != "") r = 1;
    if ("abc".to_hex() != "616263") r = 1;
    if ("\x00\x01\xff".to_hex() != "0001ff") r = 1;
    if ("616263".from_hex() != "abc") r = 1;
    /* Case-insensitive decode */
    if ("DeAdBe".from_hex() != "\xDE\xAD\xBE") r = 1;
    /* Strict rejection */
    /* odd length */
    if ("a".from_hex().len != 0) r = 1;
    /* bad digit */
    if ("zz".from_hex().len != 0) r = 1;
    /* Single mainstream `to_hex` / `from_hex` Qt-style spelling --
       the Python-flavour shorthand `hex` was dropped because the
       bare verb is ambiguous on a dotted call (encode? decode?
       lowercase? uppercase?). */
    /* Round trip on CJK */
    if (u8"中文".to_hex().from_hex() != u8"中文") r = 1;

    /* ===== URL percent-encoding (RFC 3986) ===== */
    if ("".url_encode() != "") r = 1;
    if ("abc-_.~".url_encode() != "abc-_.~") r = 1;
    if ("hello world".url_encode() != "hello%20world") r = 1;
    /* CJK -> percent-encoded UTF-8 bytes */
    if (u8"中".url_encode() != "%E4%B8%AD") r = 1;
    if ("name=value&x=1".url_encode() != "name%3Dvalue%26x%3D1") r = 1;
    if ("hello%20world".url_decode() != "hello world") r = 1;
    if ("%E4%B8%AD".url_decode() != u8"中") r = 1;
    /* Lowercase hex on decode also accepted */
    if ("%e4%b8%ad".url_decode() != u8"中") r = 1;
    /* Strict rejection -- truncated / bad / trailing % */
    if ("%".url_decode().len != 0) r = 1;
    if ("%2".url_decode().len != 0) r = 1;
    if ("%2g".url_decode().len != 0) r = 1;
    if ("ab%".url_decode().len != 0) r = 1;
    /* Single mainstream `url_encode` / `url_decode` spelling -- the
       academic `percent_encode` / `percent_decode` synonyms were
       dropped from the dotted-method table.  RFC 3986 calls it
       "percent-encoding" but every mainstream stdlib (JS
       `encodeURIComponent`, Python `urllib.parse.quote`, Java
       `URLEncoder.encode`) ships it under "URL". */
    /* Round trip on a richer payload */
    if (u8"Hello, 世界! &?=#".url_encode().url_decode() != u8"Hello, 世界! &?=#")
        r = 1;

    /* ===== HTML form (application/x-www-form-urlencoded) ===== */
    /* SPACE -> '+', literal '+' -> '%2B' */
    if ("hello world".form_encode() != "hello+world") r = 1;
    if ("a+b".form_encode() != "a%2Bb") r = 1;
    if ("name=val&x=1 2".form_encode() != "name%3Dval%26x%3D1+2") r = 1;
    if (u8"中".form_encode() != "%E4%B8%AD") r = 1;
    /* SPACE round trip via form */
    if ("hello+world".form_decode() != "hello world") r = 1;
    if ("a%2Bb".form_decode() != "a+b") r = 1;
    if ("name%3Dval%26x%3D1+2".form_decode() != "name=val&x=1 2") r = 1;
    /* Strict rejection on malformed escape (same contract as url_decode) */
    if ("%".form_decode().len != 0) r = 1;
    if ("%2".form_decode().len != 0) r = 1;
    if ("%2g".form_decode().len != 0) r = 1;
    /* form_encode and url_encode are NOT interchangeable.  The form
       flavour writes '+', the RFC 3986 flavour writes '%20', so the
       two outputs MUST differ on any input that contains a SPACE. */
    if ("a b".form_encode() == "a b".url_encode()) r = 1;
    /* Cross-decoding intentionally does the wrong thing -- piping a
       form-encoded payload through `url_decode` keeps the '+' glyph
       intact (no SPACE substitution), so the round trip MUST fail. */
    if ("hello+world".url_decode() == "hello world") r = 1;
    /* `form_encode` / `form_decode` is the single mainstream spelling
       for `application/x-www-form-urlencoded`; the mouthful
       `urlencoded_*` alias rows were dropped from the dotted-method
       table. */
    if ("a b".form_encode() != "a+b") r = 1;
    if ("a+b".form_decode() != "a b") r = 1;
    /* Round trip on rich CJK payload */
    if (u8"Hello, 世界! &?=# +space".form_encode().form_decode()
        != u8"Hello, 世界! &?=# +space")
        r = 1;

    /* ===== Chained encodings: percent-encoded base64 of CJK ===== */
    {
        string blob = u8"中文混合 mixed";
        string b64 = blob.clone().to_base64();
        /* Base64 output is pure ASCII, so url_encode shrinks to a
           no-op except for the trailing `=` padding. */
        string urlsafe = b64.clone().url_encode();
        string back_b64 = urlsafe.url_decode();
        if (back_b64 != b64) r = 1;
        string back = back_b64.from_base64();
        if (back != u8"中文混合 mixed") r = 1;
    }

    /* ===== UTF-8 byte tampering survives the contract ===== */
    {
        /* Forged handle: positive len with NULL data must short-
           circuit through the empty sentinel without crashing. */
        string forged = neverc_string_view((const char *)0, 5);
        if (forged.len != 0) r = 1;
        if (forged.to_base64().len != 0) r = 1;
        if (forged.url_encode().len != 0) r = 1;
        if (forged.to_hex().len != 0) r = 1;
    }

    printf("test_neverc_string_encoding: %s\n",
           r == 0 ? "ALL PASSED" : "FAILED");
    return r;
}
