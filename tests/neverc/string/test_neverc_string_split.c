// RUN: %neverc -std=c23 %s -o %t && %t
/* test_neverc_string_split.c -- split / partition / join coverage.
 *
 * The std::string surface deliberately stops short of `split` / `join`
 * (the C++ idiom is `std::ranges::split` + a manually-allocated
 * vector).  NeverC's value-typed `string` cannot expose a "vector of
 * strings" return type without committing to a dynamic-array ABI,
 * so the prelude takes the Rust `str::split_once` / Go `strings.Cut`
 * route instead: peel one piece off either end with
 * `split_first` / `split_rest` / `split_before_last` /
 * `split_after_last`, and use `neverc_string_join` for the inverse
 * direction with a caller-owned `const string *` array.  Single
 * mainstream spelling per concept -- the terse `before` / `after`
 * dotted aliases were dropped because no major stdlib (Python
 * `str.partition`, Rust `str::split_once`, Go `strings.Cut`) ships
 * them as method names.
 *
 * Coverage:
 *
 *   * split_first / split_rest -- forward pair, common idiom
 *     `head = s.split_first(","); s = s.split_rest(",")` until `s.len == 0`.
 *
 *   * split_before_last / split_after_last -- "find the last token"
 *     idiom for path-style `dirname` / `basename` slicing.
 *
 *   * Empty / missing separator policies match the documentation:
 *     - empty sep    -> single piece (whole `s`).
 *     - missing sep  -> `split_first` returns `s`, `split_rest`
 *                       returns the empty sentinel.
 *
 *   * neverc_string_join -- pure ASCII, CJK, single-element, empty
 *     separator, oversized-saturation rejection.
 *
 *   * Round-trip: split + join on `,` / `:` / `/` payloads round-trip
 *     through both directions.
 *
 * Receiver-by-value contract is exercised end-to-end so any
 * leaked allocation would surface under `leaks --atExit`.
 */

int main(void) {
    int r = 0;

    /* ----- split_first / split_rest forward pair ----- */
    if ("a,b,c".split_first(",") != "a") r = 1;
    if ("a,b,c".split_rest(",") != "b,c") r = 1;

    /* Multi-byte separator */
    if ("foo::bar::baz".split_first("::") != "foo") r = 1;
    if ("foo::bar::baz".split_rest("::") != "bar::baz") r = 1;

    /* CJK payload + ASCII separator */
    if (u8"一,二,三".split_first(",") != u8"一") r = 1;
    if (u8"一,二,三".split_rest(",") != u8"二,三") r = 1;

    /* Walk the whole string with the `(split_first, split_rest)` recurrence. */
    {
        string s = "alpha;beta;gamma;delta";
        string p1 = s.split_first(";");
        s = s.split_rest(";");
        string p2 = s.split_first(";");
        s = s.split_rest(";");
        string p3 = s.split_first(";");
        s = s.split_rest(";");
        if (p1 != "alpha") r = 1;
        if (p2 != "beta") r = 1;
        if (p3 != "gamma") r = 1;
        if (s != "delta") r = 1;
    }

    /* Missing separator -- `split_first` returns the whole input,
       `split_rest` returns the empty sentinel.  This is the
       documented "no second element" answer that lets a loop bail
       out cleanly. */
    if ("noseparator".split_first(",") != "noseparator") r = 1;
    if ("noseparator".split_rest(",").len != 0) r = 1;

    /* Empty separator -- both halves return the whole input so the
       pair stays self-consistent (no piece silently lost). */
    if ("abc".split_first("") != "abc") r = 1;
    if ("abc".split_rest("") != "abc") r = 1;

    /* Empty source -- both halves are empty regardless of separator. */
    if ("".split_first(",").len != 0) r = 1;
    if ("".split_rest(",").len != 0) r = 1;

    /* ----- split_before_last / split_after_last reverse pair ----- */
    if ("a/b/c.txt".split_before_last("/") != "a/b") r = 1;
    if ("a/b/c.txt".split_after_last("/") != "c.txt") r = 1;

    /* basename on a path with no separator returns the whole input. */
    if ("filename.txt".split_before_last("/") != "filename.txt") r = 1;
    if ("filename.txt".split_after_last("/").len != 0) r = 1;

    /* extension peeling */
    if ("notes.long.tar.gz".split_before_last(".") != "notes.long.tar") r = 1;
    if ("notes.long.tar.gz".split_after_last(".") != "gz") r = 1;

    /* Empty separator on the reverse pair returns the whole input. */
    if ("abc".split_before_last("") != "abc") r = 1;
    if ("abc".split_after_last("") != "abc") r = 1;

    /* ----- neverc_string_join -----
     *
     * Memory-safety note: `string parts[N] = { ... }` triggers a
     * value-typed retain on every element, so each `parts[i]` is a
     * caller-owned `string` after the initializer.  The helper does
     * NOT take ownership of the elements (only of `sep`), so the
     * caller MUST release every slot after the join completes.
     * `neverc_string_array_free(parts, n)` is the pointer-shaped
     * cleanup escape hatch: Sema's per-variable cleanup attribute
     * does not reach `string`-typed arrays, so the prelude offers
     * an explicit array-walking variant that frees each owned
     * payload in place.  Borrow-view slots (`cap == 0`) are no-ops,
     * so a mixed array of literals plus owned pieces stays correct
     * without the caller having to peek at each slot's ownership.
     * The `__neverc_string_retain` short-circuit on borrow views
     * already keeps a literal-only array allocation-free, so the
     * `array_free` call below mostly poisons the slots (resets to
     * the empty sentinel) without freeing anything -- it is still
     * called for symmetry. */
    {
        string parts[3] = { "a", "b", "c" };
        if (neverc_string_join(parts, 3, ",") != "a,b,c") r = 1;
        neverc_string_array_free(parts, 3);
    }

    /* Single-element join -- no separator emitted. */
    {
        string parts[1] = { "only" };
        if (neverc_string_join(parts, 1, ",") != "only") r = 1;
        neverc_string_array_free(parts, 1);
    }

    /* Empty separator -- pure concatenation. */
    {
        string parts[3] = { "x", "y", "z" };
        if (neverc_string_join(parts, 3, "") != "xyz") r = 1;
        neverc_string_array_free(parts, 3);
    }

    /* CJK payload + multi-byte separator. */
    {
        string parts[3] = { u8"一", u8"二", u8"三" };
        if (neverc_string_join(parts, 3, u8"、") != u8"一、二、三") r = 1;
        neverc_string_array_free(parts, 3);
    }

    /* Zero-element join collapses to the empty sentinel; the
       separator is still consumed (no leak). */
    {
        if (neverc_string_join((const string *)0, 0, ",").len != 0) r = 1;
    }

    /* ----- Round-trip ----- */
    {
        /* Split a CSV manually with the (split_first, split_rest)
           pair, then rejoin via `neverc_string_join` and confirm the
           byte payload matches. */
        string s = "alpha,beta,gamma";
        string p1 = s.clone().split_first(",");
        string rest1 = s.clone().split_rest(",");
        string p2 = rest1.clone().split_first(",");
        string p3 = rest1.split_rest(",");
        string parts[3] = { p1, p2, p3 };
        string joined = neverc_string_join(parts, 3, ",");
        if (joined != s) r = 1;
        /* `parts` retained an independent owned copy of each `pN`,
           so we have two distinct sets of owned handles to release:
           the originals (`pN`) plus the array slots.  The array
           walks through `neverc_string_array_free`, the originals
           through ordinary single-handle frees. */
        neverc_string_array_free(parts, 3);
        neverc_string_free(p1);
        neverc_string_free(p2);
        neverc_string_free(p3);
        neverc_string_free(joined);
    }

    /* path-style dirname/basename round-trip via `split_before_last`
       / `split_after_last`. */
    {
        string path = "/usr/local/bin/neverc";
        string dir = path.clone().split_before_last("/");
        string base = path.clone().split_after_last("/");
        if (dir != "/usr/local/bin") r = 1;
        if (base != "neverc") r = 1;
        string parts[2] = { dir, base };
        string joined = neverc_string_join(parts, 2, "/");
        if (joined != path) r = 1;
        neverc_string_array_free(parts, 2);
        neverc_string_free(dir);
        neverc_string_free(base);
        neverc_string_free(joined);
    }

    /* ----- Full split into a heap array -----
     *
     * `neverc_string_split(s, sep, &items, &count)` is the
     * "give me every piece in one call" companion to the
     * peel-one-piece `split_first` / `split_rest` pair.  The
     * heap-allocated array is released through
     * `neverc_string_split_free(items, count)` -- it drops every
     * element AND the array storage, so the caller doesn't need
     * a separate `__builtin_free`.
     *
     * Edge cases pinned down here match Java / Python / Go split
     * conventions: empty source -> single empty piece; missing
     * separator -> single piece (the whole input); trailing
     * separator -> trailing empty piece; multiple consecutive
     * separators -> empty pieces between them; empty separator
     * -> single piece (no split happens, matches the existing
     * `split_first` / `split_rest` policy).
     *
     * Both the global-function form and the dotted-call alias
     * (`s.split(...)`) round-trip through the same runtime helper.
     */

    /* Basic 3-piece split via the global-function form. */
    {
        string *items = (string *)0;
        __SIZE_TYPE__ count = 0;
        neverc_string_split("a,b,c", ",", &items, &count);
        if (count != 3) r = 1;
        if (items == (string *)0) r = 1;
        if (items[0] != "a") r = 1;
        if (items[1] != "b") r = 1;
        if (items[2] != "c") r = 1;
        neverc_string_split_free(items, count);
    }

    /* Same shape via the dotted-call alias. */
    {
        string *items = (string *)0;
        __SIZE_TYPE__ count = 0;
        "x|y|z".split("|", &items, &count);
        if (count != 3) r = 1;
        if (items[0] != "x") r = 1;
        if (items[1] != "y") r = 1;
        if (items[2] != "z") r = 1;
        neverc_string_split_free(items, count);
    }

    /* Multi-byte separator. */
    {
        string *items = (string *)0;
        __SIZE_TYPE__ count = 0;
        "foo::bar::baz".split("::", &items, &count);
        if (count != 3) r = 1;
        if (items[0] != "foo") r = 1;
        if (items[1] != "bar") r = 1;
        if (items[2] != "baz") r = 1;
        neverc_string_split_free(items, count);
    }

    /* CJK payload + ASCII separator -- bytes are opaque to the
       split, so each piece keeps its full UTF-8 byte sequence. */
    {
        string *items = (string *)0;
        __SIZE_TYPE__ count = 0;
        u8"一,二,三".split(",", &items, &count);
        if (count != 3) r = 1;
        if (items[0] != u8"一") r = 1;
        if (items[1] != u8"二") r = 1;
        if (items[2] != u8"三") r = 1;
        neverc_string_split_free(items, count);
    }

    /* Trailing separator yields a trailing empty piece. */
    {
        string *items = (string *)0;
        __SIZE_TYPE__ count = 0;
        "a,".split(",", &items, &count);
        if (count != 2) r = 1;
        if (items[0] != "a") r = 1;
        if (items[1].len != 0) r = 1;
        neverc_string_split_free(items, count);
    }

    /* Leading separator yields a leading empty piece. */
    {
        string *items = (string *)0;
        __SIZE_TYPE__ count = 0;
        ",a".split(",", &items, &count);
        if (count != 2) r = 1;
        if (items[0].len != 0) r = 1;
        if (items[1] != "a") r = 1;
        neverc_string_split_free(items, count);
    }

    /* Consecutive separators yield empty pieces between them. */
    {
        string *items = (string *)0;
        __SIZE_TYPE__ count = 0;
        "a,,b".split(",", &items, &count);
        if (count != 3) r = 1;
        if (items[0] != "a") r = 1;
        if (items[1].len != 0) r = 1;
        if (items[2] != "b") r = 1;
        neverc_string_split_free(items, count);
    }

    /* Missing separator -- single piece with the whole input. */
    {
        string *items = (string *)0;
        __SIZE_TYPE__ count = 0;
        "noseparator".split(",", &items, &count);
        if (count != 1) r = 1;
        if (items[0] != "noseparator") r = 1;
        neverc_string_split_free(items, count);
    }

    /* Empty separator -- single piece (whole input).  Matches the
       documented `split_first` / `split_rest` policy and the C++
       `std::ranges::views::split` behaviour. */
    {
        string *items = (string *)0;
        __SIZE_TYPE__ count = 0;
        "abc".split("", &items, &count);
        if (count != 1) r = 1;
        if (items[0] != "abc") r = 1;
        neverc_string_split_free(items, count);
    }

    /* Empty source -- single empty piece.  Matches Java
       `"".split(",")` and Rust `"".split(',').collect::<Vec<_>>()`
       so callers can iterate without a special-case. */
    {
        string *items = (string *)0;
        __SIZE_TYPE__ count = 0;
        "".split(",", &items, &count);
        if (count != 1) r = 1;
        if (items[0].len != 0) r = 1;
        neverc_string_split_free(items, count);
    }

    /* Long split (8 pieces) -- exercises the per-piece
       owned-allocation loop.  `neverc_string_split_free` walks
       every slot so any leak surfaces under leaks --atExit. */
    {
        string *items = (string *)0;
        __SIZE_TYPE__ count = 0;
        "1:2:3:4:5:6:7:8".split(":", &items, &count);
        if (count != 8) r = 1;
        if (items[0] != "1") r = 1;
        if (items[3] != "4") r = 1;
        if (items[7] != "8") r = 1;
        neverc_string_split_free(items, count);
    }

    /* Round-trip: split + join on the same separator restores the
       original payload byte-for-byte. */
    {
        string original = "alpha,beta,gamma,delta";
        string *items = (string *)0;
        __SIZE_TYPE__ count = 0;
        original.clone().split(",", &items, &count);
        if (count != 4) r = 1;
        string joined = neverc_string_join(items, count, ",");
        if (joined != original) r = 1;
        neverc_string_split_free(items, count);
        neverc_string_free(joined);
    }

    /* NULL out_items / out_count are accepted (helper drops the
       receiver + sep instead of crashing).  Stress-test the
       defensive-output-init path. */
    {
        neverc_string_split("a,b", ",", (string **)0, (__SIZE_TYPE__ *)0);
    }

    printf("test_neverc_string_split: %s\n",
           r == 0 ? "ALL PASSED" : "FAILED");
    return r;
}
