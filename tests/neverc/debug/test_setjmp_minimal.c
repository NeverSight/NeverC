#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>

static jmp_buf env;

__attribute__((noinline))
static void do_longjmp(int val) {
    longjmp(env, val);
}

int main(void) {
    printf("jmp_buf size: %zu\n", sizeof(jmp_buf));
    printf("test 1: basic setjmp...\n");

    volatile int stage = 0;
    int val = setjmp(env);
    if (val == 0) {
        stage = 1;
        printf("  setjmp returned 0, calling longjmp(42)...\n");
        do_longjmp(42);
        fprintf(stderr, "FAIL: should not reach here\n");
        return 1;
    } else {
        printf("  longjmp returned %d (expected 42)\n", val);
        if (val != 42) { fprintf(stderr, "FAIL: wrong value\n"); return 1; }
        if (stage != 1) { fprintf(stderr, "FAIL: volatile not preserved\n"); return 1; }
    }

    printf("test_setjmp_minimal: ALL PASSED\n");
    return 0;
}
