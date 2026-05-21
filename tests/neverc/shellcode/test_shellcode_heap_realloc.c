/* HeapArenaPass realloc test: grow buffer and verify old data preserved.
 * Expected: fn(0, 0) == 15  →  exit code 15
 */
int main(int a, int b) {
    void *malloc(unsigned long);
    void *realloc(void *, unsigned long);
    void free(void *);

    int *buf = (int *)malloc(2 * sizeof(int));
    if (!buf)
        return 99;
    buf[0] = 5;
    buf[1] = 10;

    buf = (int *)realloc(buf, 4 * sizeof(int));
    if (!buf)
        return 98;

    int sum = buf[0] + buf[1];
    free(buf);
    return sum;
}
