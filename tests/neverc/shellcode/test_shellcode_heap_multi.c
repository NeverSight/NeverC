/* HeapArenaPass multiple alloc/free: arena reuse after free.
 * Expected: fn(0, 0) == 42  →  exit code 42
 */
int main(int a, int b) {
    void *malloc(unsigned long);
    void free(void *);

    char *a1 = (char *)malloc(64);
    char *a2 = (char *)malloc(128);
    if (!a1 || !a2)
        return 99;

    a1[0] = 20;
    a2[0] = 22;
    int r = a1[0] + a2[0];

    free(a1);
    free(a2);

    char *a3 = (char *)malloc(64);
    if (!a3)
        return 98;
    a3[0] = r;
    int result = a3[0];
    free(a3);
    return result;
}
