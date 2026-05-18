// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* NeverC builtin string relational operators in ring-0 shellcode mode.
 *
 * Compile-only guard for `-mshellcode-context=kernel`: the Sema rewrite
 * for `<` / `>` / `<=` / `>=` (calls `neverc_string_compare(a, b)` and
 * compares the -1/0/+1 result against zero) must lower into the same
 * StringRuntimePass-rewritten allocator path as the rest of the
 * prelude, so the kernel arena handles the by-value compare just like
 * the user-mode path.  KernelImportPass otherwise would surface
 * `neverc_string_compare` as an unresolved kernel resolver call.
 */
int shellcode_entry(int seed) {
    string apple = "apple" + "";
    string apricot = "apricot" + "";
    string banana = "banana" + "";

    if (!(apple < apricot))
        return seed + 1;
    if (!(apricot < banana))
        return seed + 2;
    if (apricot < apple)
        return seed + 3;

    if (!("apple" < apricot))
        return seed + 4;
    if (apple > banana)
        return seed + 5;
    if (!(banana > apricot))
        return seed + 6;

    if (!(neverc_string_clone(apple) <= apple))
        return seed + 7;
    if (!(apple >= neverc_string_clone(apple)))
        return seed + 8;

    string app = "app" + "";
    if (!(app < apple))
        return seed + 9;
    if (!(apple > app))
        return seed + 10;

    string twin = "apple" + "";
    if (!(twin == apple))
        return seed + 11;
    if (twin != apple)
        return seed + 12;
    if (twin < apple || twin > apple)
        return seed + 13;
    if (!(twin <= apple) || !(twin >= apple))
        return seed + 14;

    /* Pressure: arena reuse on the smaller kernel budget. */
    for (int i = 0; i < 32; ++i) {
        string scratch = "scratch" + "";
        if (!(scratch < "zzz"))
            return seed + 15;
        if (!("aaa" < scratch))
            return seed + 16;
        if (!(scratch >= "scratch"))
            return seed + 17;
    }

    if (!("" < apple))
        return seed + 18;
    if (apple < "")
        return seed + 19;

    if (apple.compare(banana) >= 0)
        return seed + 20;
    if (apple.compare("apple") != 0)
        return seed + 21;

    return seed;
}
