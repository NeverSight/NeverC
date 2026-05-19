// RUN: %neverc -std=c23 %s -o %t && %t
/* test_neverc_string_namespace.c -- namespace isolation contract.
 *
 * Pins down the design promise the user asked for:
 *
 *   "[the public spellings] should only be reachable through `s.<m>(...)`
 *    or `s == t`; otherwise plain C may have a function with the same
 *    name, or another library a user links in may have one -- unless
 *    we add a `neverc_` prefix."
 *
 * Concretely, this file:
 *
 *   1. Defines its own user-facing `string_eq`, `string_view`,
 *      `string_to_base64`, `string_url_encode`, `string_clone`,
 *      `string_compare`, `string_find`, `string_to_lower` and
 *      `STRING_NPOS` AT FILE SCOPE.  None of them must shadow or
 *      collide with the prelude's builtin string runtime; the prelude
 *      now only plants `neverc_string_*` and `NEVERC_STRING_NPOS`,
 *      which leaves the bare spellings free for ordinary C code.
 *
 *   2. Calls each user function (returning a sentinel value) and
 *      verifies the returned value is the sentinel, NOT the prelude's
 *      result.  If the prelude leaked an unprefixed symbol the call
 *      would either fail to link or return the prelude's answer.
 *
 *   3. Exercises the user-facing surface (operator + dotted call,
 *      including `s.eq(t)`) on the same argument set, proving the
 *      prelude is still reachable through the documented spellings.
 *      The Java-style `.equals(t)` alias was dropped from the
 *      dotted-method table -- std::string and QString both reach
 *      equality through `operator==` only, so the dotted surface
 *      stays one-name-one-meaning.
 *
 *   4. Round-trips a Chinese / supplementary-plane payload through
 *      `printf("%s", s.c_str())` so the test also covers the
 *      "qstring s1 = u8\"你好\"" CJK output ergonomic the user
 *      mentioned (output checked via the test suite stdout grep).
 *
 *   5. Spot-checks the typed `neverc_string_*` direct call still
 *      works -- `neverc_string_eq(a, b)` should agree with `a == b`.
 */

/* ---- (1) User-defined functions that previously would have collided
 *           with the prelude.  Now safe -- the prelude only ships
 *           `neverc_string_eq` etc., not `string_eq`. */

/* User-defined `string_eq` with a wholly different signature and
   semantics from the prelude's `neverc_string_eq(string, string)`.
   If the prelude leaked a bare `string_eq` symbol, this definition
   would produce a "redefinition with different signature" or
   "redefinition with different type" diagnostic at compile time. */
static int string_eq(int a, int b) { return (a + b) | 0x80; }

static const char *string_view(int a, int b) {
    /* Wildly different signature from `neverc_string_view`'s
       (const char *, size_t) -- compile would fail if the prelude
       leaked the unprefixed name. */
    (void)a; (void)b;
    return "user-defined string_view sentinel";
}

static int string_to_base64(int n) {
    /* Different return type from `neverc_string_to_base64`. */
    return n + 1;
}

static int string_to_base32(int n) {
    /* Same shape sanity check for the Base32 (RFC 4648 §6) helper.
       Pinning down `string_to_base32` / `string_from_base32` here
       proves the prelude does not plant unprefixed `string_to_*` or
       `string_from_*` symbols even after the codec family expanded
       to cover TOTP / Google Authenticator (RFC 6238 §3). */
    return n * 5 + 11;
}

static int string_from_base32(int n) {
    return n * 7 - 13;
}

static int string_url_encode(int n) {
    return n * 3;
}

static int string_clone(int n) {
    return n - 1;
}

static int string_compare(int a, int b) {
    return a + b * 2;
}

static int string_find(int haystack, int needle) {
    return haystack ^ needle;
}

static int string_to_lower(int c) {
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

/* The ASCII case-insensitive `_ic` family inherits the same namespace
   contract as the rest of the prelude: only `neverc_string_*_ic` is
   shipped, so an ordinary C translation unit can keep defining its
   own `string_eq_ic` / `string_compare_ic` / etc. without collision. */
static int string_eq_ic(int a, int b) { return (a + b) | 0x40; }
static int string_compare_ic(int a, int b) { return a - b * 7; }
static int string_find_ic(int h, int n) { return h ^ n ^ 0x55; }
static int string_contains_ic(int h, int n) { return (h & n) | 1; }
static int string_starts_with_ic(int s, int p) { return s + p + 100; }
static int string_ends_with_ic(int s, int p) { return s * p + 1; }

/* User's own STRING_NPOS macro -- we deliberately removed the
   prelude's `STRING_NPOS` alias, only `NEVERC_STRING_NPOS` remains. */
#define STRING_NPOS 0xC0DEDEADBEEFu

int main(void) {
    int r = 0;

    /* ---- (2) Verify user functions are reachable + correct. */
    if (string_eq(2, 3) != ((2 + 3) | 0x80)) r = 1;
    if (string_view(7, 9) == (const char *)0) r = 1;
    if (string_view(0, 0)[0] != 'u') r = 1;
    if (string_to_base64(41) != 42) r = 1;
    if (string_to_base32(3) != 26) r = 1;
    if (string_from_base32(4) != 15) r = 1;
    if (string_url_encode(7) != 21) r = 1;
    if (string_clone(10) != 9) r = 1;
    if (string_compare(3, 4) != 11) r = 1;
    if (string_find(0xF0, 0x0F) != 0xFF) r = 1;
    if (string_to_lower('Q') != 'q') r = 1;
    if (string_to_lower('q') != 'q') r = 1;
    if (string_to_lower('!') != '!') r = 1;
    if (STRING_NPOS != 0xC0DEDEADBEEFu) r = 1;

    /* IC family namespace contract: bare names stay the user's. */
    if (string_eq_ic(2, 3) != ((2 + 3) | 0x40)) r = 1;
    if (string_compare_ic(10, 1) != 10 - 7) r = 1;
    if (string_find_ic(0xF0, 0x0F) != (0xF0 ^ 0x0F ^ 0x55)) r = 1;
    if (string_contains_ic(0x0F, 0x0F) != ((0x0F & 0x0F) | 1)) r = 1;
    if (string_starts_with_ic(2, 3) != 105) r = 1;
    if (string_ends_with_ic(2, 3) != 7) r = 1;

    /* ---- (3) Prelude is still reachable through the documented surface. */

    string s = "Hello";
    string t = "Hello";
    string u = "World";

    /* operator==/!= */
    if (!(s == t)) r = 1;
    if (s == u) r = 1;
    if (s != t) r = 1;
    if (!(s != u)) r = 1;

    /* dotted equality (same helper as operator==; avoids unprefixed C names). */
    if (!s.eq(t)) r = 1;
    if (s.eq(u)) r = 1;

    /* Dotted IC family reaches the prelude even with the bare
       string_*_ic spellings shadowed at file scope above. */
    if (!s.eq_ic("HELLO")) r = 1;
    if (s.eq_ic("WORLD")) r = 1;
    if (s.compare_ic("HELLO") != 0) r = 1;
    if (s.find_ic("LL") != 2) r = 1;
    if (!s.contains_ic("EL")) r = 1;
    if (!s.starts_with_ic("HE")) r = 1;
    if (!s.ends_with_ic("LO")) r = 1;

    /* dotted methods */
    if (s.len() != 5) r = 1;
    if (!s.starts_with("Hel")) r = 1;
    if (!s.ends_with("llo")) r = 1;
    if (s.find("ll") != 2) r = 1;
    if (s.find("xx") != NEVERC_STRING_NPOS) r = 1;
    if (s.compare("Hello") != 0) r = 1;
    if (s.compare("World") >= 0) r = 1;
    if (u.compare("Hello") <= 0) r = 1;

    /* `s[i]` subscript -- std::string-style read-only access lowered to
       `neverc_string_at(s, i)` by Sema's `ActOnArraySubscriptExpr` rewrite.
       Plain `char buf[N]; buf[i]` still reaches the ordinary array
       subscript path because the buffer's type is `char[N]`, not the
       builtin `string` record; only base expressions whose type IS
       the NeverC string take the rewrite.  Out-of-range index returns
       0, matching `s.at(i)`.  A chained `s.substr(...)[0]` test
       exercises the temporary-receiver retain path so a leak in the
       rewrite would surface under `leaks --atExit`. */
    if (s[0] != 'H') r = 1;
    if (s[4] != 'o') r = 1;
    if (s[99] != 0) r = 1;
    if (s.substr(0, 3)[0] != 'H') r = 1;
    {
        char buf[8] = "abcdef";
        if (buf[0] != 'a' || buf[3] != 'd') r = 1;
    }

    /* `s + ch` / `ch + s` / `s += ch` -- std::string-parity char-shaped
       operators.  The dispatcher in `tryNeverCStringBinaryOpRewrite`
       wraps the integer operand through `neverc_string_from_char` so the
       cat helper sees two strings and the value-typed consume contract
       releases each side exactly once.  No leak under leaks --atExit
       even on chained `"x" + 'y' + 'z'` shapes. */
    {
        string sa = "hi";
        sa += '!';
        if (sa != "hi!") r = 1;
        if (sa.len != 3) r = 1;
        string sb = sa + '?';
        if (sb != "hi!?") r = 1;
        string sc = '<' + sa;
        if (sc != "<hi!") r = 1;
        string sd = "x" + 'y' + 'z';
        if (sd != "xyz") r = 1;
        string se = "(" + sa + ")";
        if (se != "(hi!)") r = 1;
    }
    /* Plain int arithmetic / pointer arithmetic must keep working --
       only `string OP integer` triggers the char-promote path. */
    {
        int x = 5 + 3;
        if (x != 8) r = 1;
        const char *p = "hello";
        p = p + 1;
        if (p[0] != 'e') r = 1;
    }

    /* `to_lower` / `to_upper` round-trip preserves byte payload */
    if ("MIXED-Case 123".to_lower() != "mixed-case 123") r = 1;
    if ("MIXED-Case 123".to_upper() != "MIXED-CASE 123") r = 1;

    /* Substr / clone pair -- verifies the dot-call clone yields a
       fresh owned buffer (different pointer) but identical bytes. */
    string head = s.substr(0, 3);
    if (head != "Hel") r = 1;

    string copy = s.clone();
    if (copy != s) r = 1;
    /* Clone result is owned by us; the operator!=
       comparison above already released it, the local
       `copy` going out of scope releases the second one. */

    /* Chained dotted call on a string literal */
    if ("foobar".to_base64() != "Zm9vYmFy") r = 1;
    if ("Zm9vYmFy".from_base64() != "foobar") r = 1;
    /* Base32 reaches the prelude through `s.to_base32()` even with
       `string_to_base32` / `string_from_base32` shadowed at file
       scope above; pins down the namespace contract for the
       newly-added codec. */
    if ("foobar".to_base32() != "MZXW6YTBOI======") r = 1;
    if ("MZXW6YTBOI======".from_base32() != "foobar") r = 1;
    if ("hello world".url_encode() != "hello%20world") r = 1;

    /* ---- (4) CJK round-trip + stdout for the test suite grep. */

    string greet = u8"你好";
    /* 6 bytes (UTF-8), 2 codepoints (CJK). */
    if (greet.len != 6) r = 1;
    if (greet.utf8_count() != 2) r = 1;
    if (greet.utf8_at(0) != 0x4F60) r = 1;

    string mixed = u8"Hello, 世界 😀!";
    /* H-e-l-l-o-,-space (7 ASCII) + 世(3) + 界(3) + space(1) + 😀(4) + !(1)
       = 19 bytes; codepoint count = 7 + 1 + 1 + 1 + 1 + 1 = 12. */
    if (mixed.len != 19) r = 1;
    if (mixed.utf8_count() != 12) r = 1;
    if (!mixed.utf8_valid()) r = 1;

    /* Print via .c_str() -- the prelude pins the borrow lifetime, the
       fprintf reads bytes that are still owned by `mixed`. */
    printf("greet = %s\n", greet.c_str());
    printf("mixed = %s\n", mixed.c_str());

    /* Also verify that a UTF-32 -> UTF-8 round trip preserves the CJK
       payload through the `neverc_` prefixed factory. */
    {
        __UINT32_TYPE__ ucs4[] = {0x4F60u, 0x597Du};
        string back = neverc_string_from_utf32(ucs4, 2);
        if (back != u8"你好") r = 1;
    }

    /* ---- (5) Direct prefixed call agrees with operator / s.eq (escape hatch when mixing C glue). */
    if (neverc_string_eq(s, t) != 1) r = 1;
    if (neverc_string_eq(s, u) != 0) r = 1;
    if (neverc_string_compare(s, t) != 0) r = 1;

    printf("test_neverc_string_namespace: %s\n",
           r == 0 ? "ALL PASSED" : "FAILED");
    return r;
}
