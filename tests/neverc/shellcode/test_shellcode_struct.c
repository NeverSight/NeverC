// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* Stack-local struct with a compound literal initializer.  Proves the
 * pipeline is happy with aggregate types — they stackify naturally and
 * never touch __const / __data.
 *
 * Called with (5, 9):
 *   v = {5, 9, 5 * 9}
 *   v.x * v.y + v.z = 5 * 9 + 45 = 90
 * Exit code: 90.
 */
struct Vec3 { int x; int y; int z; };

static int dot(struct Vec3 v) {
    return v.x * v.y + v.z;
}

int main(int a, int b) {
    struct Vec3 v = {a, b, a * b};
    return dot(v);
}
