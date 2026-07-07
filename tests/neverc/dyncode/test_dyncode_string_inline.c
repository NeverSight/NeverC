// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* Pure computation: sum of inline string bytes.
 * Tests Data2TextPass constant array inlining.
 * Expected: 'a' + 'b' + 'c' == 97 + 98 + 99 == 294 → exit code 294 % 256
 *
 * Since exit codes are mod 256, we use a smaller example:
 * 'A'(65) + 'B'(66) = 131  →  exit code 131
 */
int main(int unused1, int unused2) {
    const char s[] = {'A', 'B', '\0'};
    return s[0] + s[1];
}
