// RUN: %neverc -c %s -o %t.o
// Helper for LTO test (separate TU)

int helper_add(int a, int b) {
    return a + b;
}

int helper_mul(int a, int b) {
    return a * b;
}

int helper_square(int x) {
    return x * x;
}
