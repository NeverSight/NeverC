// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* Pure computation: recursive Fibonacci.
 * Expected: fib(10) == 55  →  exit code 55
 */
static int fib(int n) {
    if (n <= 1)
        return n;
    return fib(n - 1) + fib(n - 2);
}

int main(int n, int unused) {
    return fib(n);
}
