/* HeapArenaPass __builtin_calloc test: verify builtin variant is rewritten.
 * Expected: fn(0, 0) == 0  →  exit code 0
 */
int main(int a, int b) {
    int *buf = (int *)__builtin_calloc(4, sizeof(int));
    if (!buf)
        return 99;
    int sum = 0;
    for (int i = 0; i < 4; ++i)
        sum += buf[i];
    __builtin_free(buf);
    return sum;
}
