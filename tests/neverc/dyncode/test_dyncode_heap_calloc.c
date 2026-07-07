/* HeapArenaPass calloc test: zero-initialized allocation.
 * Expected: fn(0, 0) == 0  →  exit code 0
 */
int main(int a, int b) {
    void *calloc(unsigned long, unsigned long);
    void free(void *);

    int *buf = (int *)calloc(8, sizeof(int));
    if (!buf)
        return 99;
    int sum = 0;
    for (int i = 0; i < 8; ++i)
        sum += buf[i];
    free(buf);
    return sum;
}
