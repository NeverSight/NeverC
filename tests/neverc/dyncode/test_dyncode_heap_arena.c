/* HeapArenaPass basic test: malloc + fill + free, return sum.
 * Expected: fn(0, 0) == 10  →  exit code 10
 */
int main(int a, int b) {
    void *malloc(unsigned long);
    void free(void *);

    int *buf = (int *)malloc(4 * sizeof(int));
    if (!buf)
        return 99;
    buf[0] = 1;
    buf[1] = 2;
    buf[2] = 3;
    buf[3] = 4;
    int sum = buf[0] + buf[1] + buf[2] + buf[3];
    free(buf);
    return sum;
}
