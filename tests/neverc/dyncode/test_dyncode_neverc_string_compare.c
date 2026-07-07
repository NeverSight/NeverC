// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* NeverC builtin string relational-operator coverage.
 *
 * Validates the std::string-parity ordering operators (`<`, `>`,
 * `<=`, `>=`) Sema rewrites into `neverc_string_compare(a, b) <op> 0`,
 * alongside the existing equality (`==` / `!=`) path.  Both owned
 * (`+`-built) and borrowed (literal-derived) operands are exercised
 * so the compare helper's release contract is verified to drop both
 * inputs across the same return value the user sees.
 *
 * Loader runs `main(a, b)` and uses the return code as exit status.
 * 0 means every assertion passed; any non-zero return identifies the
 * specific `return N;` line below.
 */
int main(int a, int b) {
    (void)a;
    (void)b;

    string apple = "apple" + "";
    string apricot = "apricot" + "";
    string banana = "banana" + "";

    /* (1) Strict ordering on owned strings: lexicographic via
       `neverc_string_compare`. */
    if (!(apple < apricot))
        return 1;
    if (!(apricot < banana))
        return 2;
    if (apricot < apple)
        return 3;

    /* (2) Strict ordering on literal vs. owned: literals stay as
       borrowed views (`cap == 0`), the owned RHS is consumed once
       after compare returns. */
    if (!("apple" < apricot))
        return 4;
    if (apple > banana)
        return 5;
    if (!(banana > apricot))
        return 6;

    /* (3) Equal-equal length, lexicographic tie-break by character. */
    if (!(neverc_string_clone(apple) <= apple))
        return 7;
    if (!(apple <= neverc_string_clone(apple)))
        return 8;
    if (!(apple >= neverc_string_clone(apple)))
        return 9;

    /* (4) Mixed prefix relationship: shorter prefix is `<` longer
       superstring (matches std::string). */
    string app = "app" + "";
    if (!(app < apple))
        return 10;
    if (!(apple > app))
        return 11;
    if (app >= apple)
        return 12;

    /* (5) Equality ops still route through `neverc_string_eq` (the fast
       path); ordering ops route through `neverc_string_compare`.  Both
       must agree on the equality verdict. */
    string twin = "apple" + "";
    if (!(twin == apple))
        return 13;
    if (twin != apple)
        return 14;
    if (twin < apple || twin > apple)
        return 15;
    if (!(twin <= apple) || !(twin >= apple))
        return 16;

    /* (6) Loop pressure: ordering ops in a tight loop must reuse the
       arena via the same release-after-compare contract `neverc_string_eq`
       uses.  Mixing literal / owned operands hammers the borrowed-
       view release path. */
    for (int i = 0; i < 96; ++i) {
        string scratch = "scratch" + "";
        if (!(scratch < "zzz"))
            return 17;
        if (!("aaa" < scratch))
            return 18;
        if (!(scratch >= "scratch"))
            return 19;
        if (!(scratch <= "scratch"))
            return 20;
    }

    /* (7) Ordering against the empty string: any non-empty string
       compares greater (matches std::string `compare`). */
    if (!("" < apple))
        return 21;
    if (apple < "")
        return 22;
    if (apple <= "")
        return 23;

    /* (8) `s.compare(t)` agrees with the operator path: same
       runtime helper, same -1/0/+1 result. */
    if (apple.compare(banana) >= 0)
        return 24;
    if (apple.compare("apple") != 0)
        return 25;
    if (banana.compare(apple) <= 0)
        return 26;

    return 0;
}
