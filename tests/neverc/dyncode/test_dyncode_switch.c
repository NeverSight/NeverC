// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* switch/case on a small integer: Clang usually lowers this via
 * conditional branches (no jump table for such a small range), so it
 * should stay relocation-free with nothing special from the user.
 *
 * arg a selects an operation on arg b:
 *   0 -> b + 10
 *   1 -> b * 3
 *   2 -> b - 7
 *   3 -> ~b & 0xff
 *   default -> 0xff
 *
 * Called with (2, 20) -> 20 - 7 = 13 -> exit code 13.
 */
int main(int a, int b) {
    switch (a) {
    case 0: return b + 10;
    case 1: return b * 3;
    case 2: return b - 7;
    case 3: return (~b) & 0xff;
    default: return 0xff;
    }
}
