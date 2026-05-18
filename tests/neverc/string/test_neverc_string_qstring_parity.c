// RUN: %neverc -std=c23 %s -o %t && %t
/* test_neverc_string_qstring_parity.c -- "QString + std::string" combined demo.
 *
 * End-to-end acceptance covering every requirement raised during the
 * BuiltinString review.  Each failing check sets r = 1.
 *
 *   (1) `string s = u8"..."` constructs directly; `.c_str()` feeds printf
 *       for CJK output with no extra encoding step.
 *   (2) `s.format("...%S...", arg)` passes UTF-8 bytes through transparently;
 *       CJK / emoji land in the result as-is.  The by-value consume contract
 *       on `%S` args is covered by Sema's implicit retain copy.
 *   (3) All major codecs exposed via dotted call: UTF-16 / UTF-32 / Latin-1 /
 *       Base64 (RFC 4648) / Hex / URL (RFC 3986) / Form / HTML (OWASP) /
 *       JSON (RFC 8259) / CSV (RFC 4180); each pair does a CJK round-trip.
 *   (4) Long dotted-call chains across codecs (CJK -> JSON -> base64 -> URL
 *       -> reverse) still restore byte-perfect; macOS leaks --atExit also
 *       confirms no owned temporary leaks.
 *   (5) Public symbols are prefixed with `neverc_string_`; users can freely
 *       declare same-named `string_eq` / `string_view` / `string_to_base64`
 *       without collision -- enforced at the language level.
 *   (6) `s == t` / `s.eq(t)` / `neverc_string_eq(s, t)` all have identical
 *       semantics; lexicographic comparison aligns with std::string byte order.
 *   (7) Short name `STRING_NPOS` reserved for users; prelude only exposes
 *       `NEVERC_STRING_NPOS`.
 */

/* ---- (5) User-defined same-named functions / macros -- must compile,
   must not be shadowed by the prelude. */

static int string_eq(int a, int b) { return (a + b) | 0x80; }
static int string_to_base64(int n) { return n - 1; }
static int string_url_encode(int n) { return n + 100; }
static int string_to_hex(int n) { return n * 16; }
static int string_format(int n) { return n + 7; }

#define STRING_NPOS 0xC0DEDEADBEEFu

int main(void) {
    int r = 0;

    /* ===== (1) UTF-8 literals + printf CJK output ===== */
    string greet = u8"你好，世界";
    /* 5 codepoints, all 3-byte CJK = 15 bytes */
    if (greet.len != 15) r = 1;
    if (greet.utf8_count() != 5) r = 1;
    if (!greet.is_utf8()) r = 1;
    if (greet.is_ascii()) r = 1;
    if (greet.utf8_at(0) != 0x4F60u) r = 1;        /* 你 */
    if (greet.utf8_at(4) != 0x754Cu) r = 1;        /* 界 */
    /* Feed printf directly -- terminal renders CJK via UTF-8 */
    printf("greet  = %s\n", greet.c_str());

    /* Literals also support dotted calls (string literal -> borrowed view).
       Note: `.len` (field access) only works on `string` variables; literals
       must go through the `.len()` method entry so Sema promotes char[] to
       string. */
    if (u8"你好".len() != 6) r = 1;
    if (u8"你好".utf8_count() != 2) r = 1;
    if (u8"你好" != u8"你好") r = 1;

    /* ===== (2) `s.format(...)` CJK + mixed arguments ===== */
    string name = u8"小明";
    /* %S takes a NeverC string value; Sema auto-retains lvalues so the
       caller does not need .clone().  The fmt receiver is a literal,
       so its free is a no-op. */
    string greeting = "Hello, %S! lucky=%d level=%lx".format(name, 42, 0xABCDuL);
    if (!greeting.contains(u8"小明")) r = 1;
    if (!greeting.contains("42")) r = 1;
    if (!greeting.contains("abcd")) r = 1;
    printf("greet2 = %s\n", greeting.c_str());

    /* %s takes const char *, CJK works via byte passthrough */
    string g2 = "[%s] count=%d".format(u8"中文标签".c_str(), 7);
    if (!g2.contains(u8"中文标签")) r = 1;
    if (!g2.contains("count=7")) r = 1;

    /* ===== (3) Encoding round-trip: all major codecs ===== */

    /* UTF-16 */
    {
        __UINT16_TYPE__ buf16[16];
        __SIZE_TYPE__ n = u8"你好😀".to_utf16(buf16, 16);
        /* 你(BMP, 1 unit) + 好(BMP, 1 unit) + 😀(astral, 2 units) = 4 */
        if (n != 4) r = 1;
        if (buf16[2] != 0xD83Du || buf16[3] != 0xDE00u) r = 1;
        string back = neverc_string_from_utf16(buf16, n);
        if (back != u8"你好😀") r = 1;
    }

    /* UTF-32 */
    {
        __UINT32_TYPE__ buf32[8];
        __SIZE_TYPE__ n = u8"你好😀".to_utf32(buf32, 8);
        if (n != 3) r = 1;
        if (buf32[2] != 0x1F600u) r = 1;
        string back = neverc_string_from_utf32(buf32, n);
        if (back != u8"你好😀") r = 1;
    }

    /* Latin-1 (ISO-8859-1) */
    {
        char latin1[4] = { 'c', 'a', 'f', (char)0xE9 };  /* "café" Latin-1 */
        string utf8 = neverc_string_from_latin1(latin1, 4);
        if (utf8 != u8"café") r = 1;
        char back[8];
        __SIZE_TYPE__ n = u8"café".to_latin1(back, 8);
        if (n != 4) r = 1;
        if ((unsigned char)back[3] != 0xE9u) r = 1;
    }

    /* Base64 (RFC 4648 §4) */
    if (u8"你好".to_base64() != "5L2g5aW9") r = 1;
    if ("5L2g5aW9".from_base64() != u8"你好") r = 1;
    if (u8"中文 + 表情 😀".to_base64().from_base64() != u8"中文 + 表情 😀") r = 1;

    /* Hex */
    if (u8"你".to_hex() != "e4bda0") r = 1;
    if ("e4bda0".from_hex() != u8"你") r = 1;
    if (u8"中文".to_hex().from_hex() != u8"中文") r = 1;

    /* URL percent-encoding (RFC 3986) */
    if (u8"中".url_encode() != "%E4%B8%AD") r = 1;
    if ("%E4%B8%AD".url_decode() != u8"中") r = 1;
    if ("hello world".url_encode() != "hello%20world") r = 1;

    /* Form encoding (application/x-www-form-urlencoded) */
    if ("hello world".form_encode() != "hello+world") r = 1;
    if ("a+b".form_encode() != "a%2Bb") r = 1;
    if (u8"中=值".form_encode() != "%E4%B8%AD%3D%E5%80%BC") r = 1;

    /* HTML entity escape (OWASP rule 1/2 subset) */
    if ("<a>X&Y</a>".html_escape() != "&lt;a&gt;X&amp;Y&lt;/a&gt;") r = 1;
    if (u8"<你好>".html_escape().html_unescape() != u8"<你好>") r = 1;

    /* JSON string-literal escape (RFC 8259 §7) */
    if ("\"hi\"".json_escape() != "\\\"hi\\\"") r = 1;
    if (u8"中文 \"q\"\n".json_escape().json_unescape() != u8"中文 \"q\"\n") r = 1;

    /* CSV field escape (RFC 4180 §2) */
    if ("a\"b".csv_escape() != "\"a\"\"b\"") r = 1;
    if (u8"\"中文\",cell".csv_escape().csv_unescape() != u8"\"中文\",cell") r = 1;

    /* ===== (4) Cross-codec long-chain round-trip + no intermediate leaks ===== */
    {
        string source = u8"配置: { \"key\": \"中文 + 表情 😀\" }";
        string round = source.clone()
                             .json_escape()
                             .to_base64()
                             .url_encode()
                             .url_decode()
                             .from_base64()
                             .json_unescape();
        if (round != source) r = 1;
        printf("round  = %s\n", round.c_str());
    }

    /* Chained trim + mutation + slice -- matches std::string idiom */
    {
        string out = u8"  Hello, 世界!  ".trim()
                                         .to_lower()
                                         .replace_all(",", "::")
                                         .substr(0, 5);
        if (out != "hello") r = 1;
    }

    /* ===== (5) User same-named functions/macros still resolve to user defs,
       not shadowed by the prelude ===== */
    if (string_eq(2, 3) != ((2 + 3) | 0x80)) r = 1;       /* user's, not prelude */
    if (string_to_base64(43) != 42) r = 1;
    if (string_url_encode(7) != 107) r = 1;
    if (string_to_hex(8) != 128) r = 1;
    if (string_format(3) != 10) r = 1;
    if (STRING_NPOS != 0xC0DEDEADBEEFu) r = 1;
    /* Prelude uses `NEVERC_STRING_NPOS`; short name STRING_NPOS is for users */
    if (greet.find(u8"不存在") != NEVERC_STRING_NPOS) r = 1;
    if (NEVERC_STRING_NPOS == STRING_NPOS) r = 1;          /* two distinct symbols */

    /* ===== (6) Three entry points (operator / dotted / neverc_*) are
       equivalent.  `s.equals(t)` Java-style alias was removed from the
       dotted-method table (std::string / Qt only compare via operator==),
       so the three main entries are `==` / `.eq()` / `neverc_string_eq()`. ===== */
    {
        string a = u8"你好";
        string b = u8"你好";
        string c = u8"再见";
        if (!(a == b))                       r = 1;
        if (!a.eq(b))                        r = 1;
        if (a == c)                          r = 1;
        if (a != b)                          r = 1;
        if (!(a != c))                       r = 1;
        /* Byte order: 0xE4... < 0xE5... -> a < c. */
        if (!(a < c))                        r = 1;
        if (c <= a)                          r = 1;
        if (a.compare(b) != 0)               r = 1;
        if (a.compare(c) >= 0)               r = 1;
        if (neverc_string_eq(a, b) != 1)     r = 1;
        if (neverc_string_eq(a, c) != 0)     r = 1;
        if (neverc_string_compare(a, b) != 0) r = 1;
    }

    /* ASCII case-insensitive entry (single canonical spelling -- _ic suffix;
       boost-flavour i-prefix removed from BuiltinStringMethodNames.def). */
    if (!u8"Hello".eq_ic("HELLO")) r = 1;
    if (u8"Hello".eq_ic("WORLD")) r = 1;

    /* ===== (7) STRING_NPOS short-name isolation -- already verified in (5),
       pinned again here. ===== */
    {
        /* Prelude only uses NEVERC_STRING_NPOS; our short-name macro is not
           overwritten. */
        unsigned long long u = STRING_NPOS;
        if (u != 0xC0DEDEADBEEFuLL) r = 1;
    }

    /* ===== (8) std::string-style mutation end-to-end with CJK input.
       NeverC `string` is a value type: `s.append(t)` returns a new owned
       value (unlike std::string which mutates in place).  `s += t` is
       rewritten by Sema to `s = __neverc_string_cat(s, t)`, updating
       in place; to use `append` explicitly write `s = s.append(t)`. ===== */
    {
        string s = u8"中文";
        s = s.append(u8"编辑");
        if (s != u8"中文编辑") r = 1;
        s += u8"!";
        if (s != u8"中文编辑!") r = 1;
        /* `s += '?'` goes through the char-promote path; '?' is one ASCII
           byte and does not break the preceding UTF-8 sequence. */
        s += '?';
        if (s != u8"中文编辑!?") r = 1;
        /* insert operates at byte position -- CJK chars are 3 bytes each,
           inserting at a codepoint boundary (byte 6) keeps valid UTF-8. */
        if (u8"中文测试".insert(6, u8" -> ") != u8"中文 -> 测试") r = 1;
        /* replace_all is transparent to CJK payload when matching on ASCII
           delimiters -- the replacement itself can be UTF-8. */
        if (u8"中,文,编,辑".replace_all(",", u8"·") != u8"中·文·编·辑") r = 1;
    }

    /* split + join array round-trip (counterpart to Python str.split /
       Qt QString::split / Go strings.Split / Java String.split).
       `neverc_string_split` slices the receiver into a heap-array;
       the caller gets `(items, count)` and releases via
       `neverc_string_split_free` (elements + array).
       leaks --atExit must report 0 throughout. */
    {
        string *items = (string *)0;
        __SIZE_TYPE__ count = 0;
        u8"你好,世界,😀".split(",", &items, &count);
        if (count != 3) r = 1;
        if (items[0] != u8"你好") r = 1;
        if (items[1] != u8"世界") r = 1;
        if (items[2] != u8"😀") r = 1;
        /* Join back to the original -- byte-perfect. */
        string joined = neverc_string_join(items, count, ",");
        if (joined != u8"你好,世界,😀") r = 1;
        neverc_string_free(joined);
        neverc_string_split_free(items, count);
    }

    printf("test_neverc_string_qstring_parity: %s\n",
           r == 0 ? "ALL PASSED" : "FAILED");
    return r;
}
