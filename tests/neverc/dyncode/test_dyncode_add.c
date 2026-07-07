// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* Pure computation: int add(int a, int b) { return a + b; }
 * Expected: fn(3, 4) == 7  →  exit code 7
 */
int main(int a, int b) {
    return a + b;
}
