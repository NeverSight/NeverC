// RUN: %neverc -std=c23 %s -o %t && %t
/* test_neverc_string_webcodec.c -- web/data-shaped escape contracts.
 *
 * Pins down the round-trip + strictness contract for the three
 * `escape`/`unescape` pairs added in BuiltinStringPrelude/WebCodec.inc:
 *
 *   * HTML entity escape (5-char OWASP rule #1/#2 subset) + a
 *     tolerant decoder that passes unknown `&...;` runs through
 *     verbatim.
 *
 *   * RFC 8259 §7 JSON string-literal escape + a strict decoder
 *     (surrogate-pair aware on `\uXXXX`).
 *
 *   * RFC 4180 §2 CSV field escape (always-quote convention) + a
 *     strict round-trip decoder.
 *
 * Coverage:
 *   * Empty input round-trip on every helper -> empty sentinel.
 *   * Per-special-char rewrite verified individually.
 *   * UTF-8 / CJK / supplementary-plane payload pass-through.
 *   * Strict-decode rejection on every documented failure shape.
 *   * Forged handles (NULL data with positive len) collapse to the
 *     empty sentinel without touching the NULL pointer.
 *   * Cross-codec chaining (HTML escape -> URL encode, JSON escape
 *     -> base64) keeps the single-ownership contract and round-trips
 *     back through the inverse path.
 *
 * Combined with the macOS `leaks --atExit` gate in run_tests.sh,
 * this file proves no helper leaks an owned `string` on either the
 * happy path or the strict-decode failure tail.
 */

int main(void) {
    int r = 0;

    /* ===== HTML entity escape ===== */

    /* Empty round-trip */
    if ("".html_escape().len != 0) r = 1;
    if ("".html_unescape().len != 0) r = 1;

    /* 5-char rewrite each on its own */
    if ("&".html_escape() != "&amp;") r = 1;
    if ("<".html_escape() != "&lt;") r = 1;
    if (">".html_escape() != "&gt;") r = 1;
    if ("\"".html_escape() != "&quot;") r = 1;
    if ("'".html_escape() != "&#39;") r = 1;

    /* Mixed run + exact byte sequence */
    if ("a&b<c>d\"e'f".html_escape() != "a&amp;b&lt;c&gt;d&quot;e&#39;f")
        r = 1;
    /* CJK passes through unchanged (high-bit UTF-8 has no entity in
       the OWASP subset). */
    if (u8"你好&".html_escape() != u8"你好&amp;") r = 1;

    /* Round-trip on the 5-char subset */
    if ("a&b<c>d\"e'f".html_escape().html_unescape() != "a&b<c>d\"e'f") r = 1;
    if (u8"<a>你好</a>".html_escape().html_unescape() != u8"<a>你好</a>") r = 1;

    /* Named entities the unescape decoder also accepts -- core set:
       the 5 OWASP rule-#1/#2 entities the encoder writes, plus the
       four typography/legal-text friendlies every server-side
       templating engine accepts by default. */
    if ("&apos;".html_unescape() != "'") r = 1;
    if ("&nbsp;".html_unescape() != u8"\u00A0") r = 1;
    if ("&copy;".html_unescape() != u8"©") r = 1;
    if ("&reg;".html_unescape() != u8"®") r = 1;

    /* Named entities OUTSIDE the core set fall through verbatim.
       Producers that need them MUST emit numeric entities
       (`&#x2122;`, `&#x2026;`), which the decoder still handles --
       see the supplementary-plane assertions below. */
    if ("&trade;".html_unescape() != "&trade;") r = 1;
    if ("&hellip;".html_unescape() != "&hellip;") r = 1;
    if ("&#x2122;".html_unescape() != u8"™") r = 1;
    if ("&#x2026;".html_unescape() != u8"…") r = 1;

    /* Numeric entities -- decimal */
    if ("&#65;".html_unescape() != "A") r = 1;
    if ("&#90;".html_unescape() != "Z") r = 1;
    if ("&#19990;".html_unescape() != u8"世") r = 1;
    /* Numeric entities -- lowercase + uppercase hex */
    if ("&#x41;".html_unescape() != "A") r = 1;
    if ("&#X41;".html_unescape() != "A") r = 1;
    if ("&#x4e16;".html_unescape() != u8"世") r = 1;
    /* Supplementary plane via numeric entity */
    if ("&#x1F600;".html_unescape() != u8"😀") r = 1;
    /* Surrogate-range numeric entity -> U+FFFD lossy fallback */
    if ("&#xD800;".html_unescape() != u8"\uFFFD") r = 1;

    /* Tolerant fallback: unknown / malformed entities pass through */
    if ("&unknown;".html_unescape() != "&unknown;") r = 1;
    if ("a & b".html_unescape() != "a & b") r = 1;
    if ("&;".html_unescape() != "&;") r = 1;
    /* Long unterminated `&` past the per-token cap -- emit the leading
       `&` and continue scanning the rest. */
    if ("&abcdefghijklmnop".html_unescape() != "&abcdefghijklmnop") r = 1;
    /* Mixed chunked input with one valid entity inside a noise tail. */
    if ("hello &amp; goodbye".html_unescape() != "hello & goodbye") r = 1;

    /* Bad numeric entities pass through (not malformed enough to drop
       the whole payload -- mirror browsers' "be tolerant" policy). */
    if ("&#abc;".html_unescape() != "&#abc;") r = 1;
    if ("&#xZZ;".html_unescape() != "&#xZZ;") r = 1;
    /* Out-of-range numeric (beyond U+10FFFF) passes through */
    if ("&#x110000;".html_unescape() != "&#x110000;") r = 1;

    /* ===== JSON string-literal escape ===== */

    /* Empty + ASCII pass-through (no chars require escape) */
    if ("".json_escape().len != 0) r = 1;
    if ("hello world".json_escape() != "hello world") r = 1;
    /* The 7 short escapes */
    if ("\"".json_escape() != "\\\"") r = 1;
    if ("\\".json_escape() != "\\\\") r = 1;
    if ("\b".json_escape() != "\\b") r = 1;
    if ("\f".json_escape() != "\\f") r = 1;
    if ("\n".json_escape() != "\\n") r = 1;
    if ("\r".json_escape() != "\\r") r = 1;
    if ("\t".json_escape() != "\\t") r = 1;
    /* Control char without short form -> \u00XX uppercase hex */
    {
        char buf[2] = { 0x01, 0 };
        string s = neverc_string_view(buf, 1);
        if (s.json_escape() != "\\u0001") r = 1;
    }
    {
        char buf[2] = { 0x1F, 0 };
        string s = neverc_string_view(buf, 1);
        if (s.json_escape() != "\\u001F") r = 1;
    }
    /* `/` is intentionally NOT escaped on encode */
    if ("a/b".json_escape() != "a/b") r = 1;
    /* High-bit UTF-8 passes through (RFC 8259 allows raw multi-byte
       UTF-8 in JSON strings when the transport is UTF-8) */
    if (u8"你好".json_escape() != u8"你好") r = 1;

    /* Round-trip */
    if ("\"\\\b\f\n\r\thello".json_escape().json_unescape()
        != "\"\\\b\f\n\r\thello") r = 1;
    if (u8"中文 + emoji 😀".json_escape().json_unescape()
        != u8"中文 + emoji 😀") r = 1;

    /* Strict unescape: 7 short forms */
    if ("\\\"".json_unescape() != "\"") r = 1;
    if ("\\\\".json_unescape() != "\\") r = 1;
    if ("\\/".json_unescape() != "/") r = 1;
    if ("\\b".json_unescape() != "\b") r = 1;
    if ("\\f".json_unescape() != "\f") r = 1;
    if ("\\n".json_unescape() != "\n") r = 1;
    if ("\\r".json_unescape() != "\r") r = 1;
    if ("\\t".json_unescape() != "\t") r = 1;
    /* `\uXXXX` BMP */
    if ("\\u0041".json_unescape() != "A") r = 1;
    if ("\\u4F60\\u597D".json_unescape() != u8"你好") r = 1;
    /* Surrogate pair -> astral codepoint */
    if ("\\uD83D\\uDE00".json_unescape() != u8"😀") r = 1;
    /* Lone surrogate -> replacement codepoint (lossy mirror of
       from_utf16's contract) */
    if ("\\uD800".json_unescape() != u8"\uFFFD") r = 1;
    /* Mismatched low surrogate (high not followed by low) */
    if ("\\uD83Dx".json_unescape() != u8"\uFFFDx") r = 1;
    /* Mixed valid + invalid in a payload */
    if ("\\\"hi\\\"".json_unescape() != "\"hi\"") r = 1;

    /* Strict-mode rejection on every documented failure shape ->
       empty sentinel.  This is the leak-free tail every helper has
       to honour: failures take the receiver buffer with them. */
    if ("\\".json_unescape().len != 0) r = 1;          /* lone trailing */
    if ("\\q".json_unescape().len != 0) r = 1;         /* unknown escape */
    if ("\\u".json_unescape().len != 0) r = 1;         /* truncated */
    if ("\\u12".json_unescape().len != 0) r = 1;       /* truncated */
    if ("\\u123".json_unescape().len != 0) r = 1;      /* truncated */
    if ("\\u123g".json_unescape().len != 0) r = 1;     /* non-hex */
    if ("hi\\".json_unescape().len != 0) r = 1;        /* trailing in tail */

    /* ===== CSV field escape ===== */

    /* Empty input -> empty sentinel (consistent with every other
       byte-shaped escape; callers who want the canonical wrapped
       empty CSV field write `"\"\""` directly). */
    if ("".csv_escape().len != 0) r = 1;
    /* Plain alphanumerics still get wrapped (always-quote policy) */
    if ("hello".csv_escape() != "\"hello\"") r = 1;
    /* Internal `"` doubled */
    if ("a\"b".csv_escape() != "\"a\"\"b\"") r = 1;
    if ("\"\"".csv_escape() != "\"\"\"\"\"\"") r = 1;
    /* Comma + newline pass through (the wrapping `"` already
       neutralises them in any CSV consumer). */
    if ("a,b".csv_escape() != "\"a,b\"") r = 1;
    if ("a\nb".csv_escape() != "\"a\nb\"") r = 1;
    /* CJK passes through */
    if (u8"你好".csv_escape() != u8"\"你好\"") r = 1;

    /* Strict unescape */
    if ("\"\"".csv_unescape().len != 0) r = 1;        /* canonical empty */
    if ("\"hello\"".csv_unescape() != "hello") r = 1;
    if ("\"a\"\"b\"".csv_unescape() != "a\"b") r = 1;
    if ("\"a,b\"".csv_unescape() != "a,b") r = 1;
    if ("\"a\nb\"".csv_unescape() != "a\nb") r = 1;
    if (u8"\"你好\"".csv_unescape() != u8"你好") r = 1;

    /* Strict-mode rejection -> empty sentinel */
    if ("hello".csv_unescape().len != 0) r = 1;       /* missing wrappers */
    if ("\"hello".csv_unescape().len != 0) r = 1;     /* missing trailing */
    if ("hello\"".csv_unescape().len != 0) r = 1;     /* missing leading */
    if ("\"a\"b\"".csv_unescape().len != 0) r = 1;    /* lone interior `"` */

    /* Round-trip on rich payload */
    if ("hello".csv_escape().csv_unescape() != "hello") r = 1;
    if ("a\"b,c\nd".csv_escape().csv_unescape() != "a\"b,c\nd") r = 1;
    if (u8"\"中文\" with quotes".csv_escape().csv_unescape()
        != u8"\"中文\" with quotes") r = 1;

    /* ===== URL-safe Base64 (RFC 4648 §5, no-padding emit) =====
       The encoding JWT (RFC 7519), JWS / JWE (RFC 7515 / 7516),
       OAuth 2.0 PKCE (RFC 7636 §4.2), JOSE, and protobuf web
       encoding all settle on; differs from the standard alphabet
       in two glyphs (`+ -> -`, `/ -> _`) and in not emitting `=`
       padding.  Decoder accepts both forms (with or without
       trailing `=`) and rejects every non-alphabet byte
       (including the standard-alphabet `+/`). */

    /* Empty round-trip. */
    if ("".to_base64_url().len != 0) r = 1;
    if ("".from_base64_url().len != 0) r = 1;

    /* RFC 4648 §10 test-vector staircase -- f / fo / foo / foob /
       fooba / foobar pin down the per-residue (0/1/2-byte tail)
       shapes; the no-padding emit is the contract clients depend
       on for JWT compactness. */
    if ("f".to_base64_url() != "Zg") r = 1;
    if ("fo".to_base64_url() != "Zm8") r = 1;
    if ("foo".to_base64_url() != "Zm9v") r = 1;
    if ("foob".to_base64_url() != "Zm9vYg") r = 1;
    if ("fooba".to_base64_url() != "Zm9vYmE") r = 1;
    if ("foobar".to_base64_url() != "Zm9vYmFy") r = 1;
    if ("Zg".from_base64_url() != "f") r = 1;
    if ("Zm8".from_base64_url() != "fo") r = 1;
    if ("Zm9v".from_base64_url() != "foo") r = 1;
    if ("Zm9vYg".from_base64_url() != "foob") r = 1;
    if ("Zm9vYmE".from_base64_url() != "fooba") r = 1;
    if ("Zm9vYmFy".from_base64_url() != "foobar") r = 1;

    /* Alphabet-substitution pin-down: a payload whose bits land on
       indices 62/63 must produce `-_` in the url-safe alphabet
       where the standard one would emit `+/`.  `\xFB\xFF\xBF` is
       chosen because all four 6-bit groups of its 24 bits hit
       those high indices, so the entire 4-char output toggles
       under the alphabet swap byte-for-byte. */
    {
        char triplet[3];
        triplet[0] = (char)0xFB;
        triplet[1] = (char)0xFF;
        triplet[2] = (char)0xBF;
        string s = neverc_string_view(triplet, 3);
        if (s.clone().to_base64() != "+/+/") r = 1;
        if (s.clone().to_base64_url() != "-_-_") r = 1;
        string back = "-_-_".from_base64_url();
        if (back.len != 3 ||
            (unsigned char)back.c_str()[0] != 0xFBu ||
            (unsigned char)back.c_str()[1] != 0xFFu ||
            (unsigned char)back.c_str()[2] != 0xBFu) r = 1;
        neverc_string_free(s);
    }

    /* Padding tolerance on decode: producers that still emit the
       standard `=` padding interoperate with our no-padding
       emit.  Pure-padding inputs strip down to the empty
       sentinel rather than tripping the malformed-tail guard. */
    if ("Zg==".from_base64_url() != "f") r = 1;
    if ("Zm8=".from_base64_url() != "fo") r = 1;
    if ("Zm9vYmE=".from_base64_url() != "fooba") r = 1;
    if ("=".from_base64_url().len != 0) r = 1;
    if ("==".from_base64_url().len != 0) r = 1;
    if ("===".from_base64_url().len != 0) r = 1;

    /* CJK + UTF-8 supplementary plane round-trip via UTF-8 bytes. */
    if (u8"你好".to_base64_url() != "5L2g5aW9") r = 1;
    if ("5L2g5aW9".from_base64_url() != u8"你好") r = 1;
    if (u8"中文 + emoji 😀".to_base64_url().from_base64_url()
        != u8"中文 + emoji 😀") r = 1;

    /* Cross-alphabet rejection: a standard-base64 payload
       containing `+` / `/` is NOT a valid url-safe input.  And a
       url-safe payload containing `-` / `_` is NOT a valid
       standard input.  Each rejected path collapses to the empty
       sentinel without partial decode. */
    if ("+/+/".from_base64_url().len != 0) r = 1;
    if ("Pj4+".from_base64_url().len != 0) r = 1;
    if ("-_-_".from_base64().len != 0) r = 1;

    /* Strict-mode rejection on every documented failure shape:
       1-char tail (impossible group), interior `=`, whitespace,
       any non-alphabet byte. */
    if ("Z".from_base64_url().len != 0) r = 1;
    if ("Z===".from_base64_url().len != 0) r = 1;
    if ("Zg=A".from_base64_url().len != 0) r = 1;
    if ("Zm 9".from_base64_url().len != 0) r = 1;
    if ("Z@9v".from_base64_url().len != 0) r = 1;

    /* JWT header round-trip ergonomic -- `{"alg":"HS256"}` is the
       canonical 16-byte JOSE header; the url-safe encoding has no
       `+/=` glyphs so it embeds cleanly into a `xxx.yyy.zzz`
       token without further escaping.  Locks the no-padding
       emit's interop with every JWT library in the wild. */
    if ("{\"alg\":\"HS256\"}".to_base64_url() !=
            "eyJhbGciOiJIUzI1NiJ9") r = 1;
    if ("eyJhbGciOiJIUzI1NiJ9".from_base64_url() !=
            "{\"alg\":\"HS256\"}") r = 1;

    /* ===== Forged handles (must not crash) ===== */
    {
        string forged = neverc_string_view((const char *)0, 99);
        if (forged.len != 0) r = 1;
        if (forged.clone().html_escape().len != 0) r = 1;
        if (forged.clone().html_unescape().len != 0) r = 1;
        if (forged.clone().json_escape().len != 0) r = 1;
        if (forged.clone().json_unescape().len != 0) r = 1;
        if (forged.clone().csv_escape().len != 0) r = 1;
        if (forged.clone().csv_unescape().len != 0) r = 1;
        /* Same forged-handle drain on the url-safe base64 pair:
           encoder collapses to empty without dereferencing the
           NULL data pointer; decoder collapses on the same
           grounds.  Pairs with the leaks --atExit gate to verify
           the consume contract holds even when the input never
           had bytes to consume. */
        if (forged.clone().to_base64_url().len != 0) r = 1;
        if (forged.clone().from_base64_url().len != 0) r = 1;
        /* The original `forged` view is also released (the dotted
           call chain consumed clones; this last consume is the
           legal single-owner release of the borrow handle).
           Borrowed views are no-ops on neverc_string_free, so we
           drop the local explicitly to satisfy any audit pass that
           wants every name reachable through one release. */
        neverc_string_free(forged);
    }

    /* ===== Cross-codec chaining (no leak across the chain) ===== */
    {
        /* HTML-escape then URL-encode the result -> safe for both
           HTML and URL contexts.  Round-trip back proves both
           transforms are byte-perfect inverses. */
        string round = "<a>X&Y</a>".html_escape().url_encode()
                                   .url_decode().html_unescape();
        if (round != "<a>X&Y</a>") r = 1;
    }
    {
        /* JSON-escape then base64-encode -> safe to embed in a JSON
           string transmitted through a base64 channel (matches
           every "config blob inside a JSON envelope inside an
           HTTP header" pipeline). */
        string round = u8"中文\nwith newline".json_escape().to_base64()
                                              .from_base64().json_unescape();
        if (round != u8"中文\nwith newline") r = 1;
    }
    {
        /* CSV-escape then HTML-escape (web view of CSV cell) -> the
           wrapping `"` becomes `&quot;`, the interior `""` becomes
           `&quot;&quot;`. */
        string cell = "a\"b".csv_escape().html_escape();
        if (cell != "&quot;a&quot;&quot;b&quot;") r = 1;
        if (cell.html_unescape().csv_unescape() != "a\"b") r = 1;
    }
    {
        /* JSON-escape then url-safe Base64 -- mirrors the JWT
           payload shape (a JSON object carried inside a
           `header.payload.signature` token where each segment is
           url-safe base64 of its bytes).  Round-trip through the
           inverse pair recovers the CJK + control-char payload
           byte-for-byte. */
        string round = u8"中文\nwith newline".json_escape()
                                              .to_base64_url()
                                              .from_base64_url()
                                              .json_unescape();
        if (round != u8"中文\nwith newline") r = 1;
    }

    printf("test_neverc_string_webcodec: %s\n",
           r == 0 ? "ALL PASSED" : "FAILED");
    return r;
}
