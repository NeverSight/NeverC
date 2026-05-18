// RUN: %neverc --target=x86_64-windows-msvc -fsyntax-only %s
/*
 * NeverC Compiler Validation - Microsoft-style Inline Assembly
 *
 * Tests MSVC __asm syntax on x86_64-windows-msvc target.
 * This file is a syntax-only / compile-only test (cannot run without Windows).
 *
 *  1.  Basic __asm { } block
 *  2.  Register operations (mov, xor)
 *  3.  Arithmetic (add, sub, imul)
 *  4.  Single-line __asm
 *  5.  Labels and conditional jumps
 *  6.  Structure member access
 *  7.  Multiple __asm blocks in one function
 *  8.  Bitwise operations
 *  9.  Nested local variable references
 * 10.  _asm alias (alternative keyword)
 */

/* 1. Basic __asm block with nop */
void test_ms_basic(void) {
    __asm {
        nop
        nop
    }
}

/* 2. Register mov and xor */
void test_ms_register_ops(void) {
    int result;
    __asm {
        mov eax, 42
        mov result, eax
    }
    (void)result;
}

/* 3. Arithmetic — add, sub, imul */
void test_ms_arithmetic(void) {
    int a = 10, b = 20;
    int sum, diff, product;
    __asm {
        mov eax, a
        add eax, b
        mov sum, eax

        mov eax, b
        sub eax, a
        mov diff, eax

        mov eax, a
        imul eax, b
        mov product, eax
    }
    (void)sum;
    (void)diff;
    (void)product;
}

/* 4. Single-line __asm statements */
void test_ms_single_line(void) {
    __asm nop
    __asm nop
    __asm xor eax, eax
}

/* 5. Labels and conditional jump */
void test_ms_labels(void) {
    int val = 1;
    int out = 0;
    __asm {
        cmp val, 0
        je skip_set
        mov out, 42
    skip_set:
        nop
    }
    (void)out;
}

/* 6. Structure member access in MS asm */
struct Vec2 {
    int x;
    int y;
};

void test_ms_struct_access(void) {
    struct Vec2 v = { 100, 200 };
    int vx, vy;
    __asm {
        mov eax, v.x
        mov vx, eax
        mov eax, v.y
        mov vy, eax
    }
    (void)vx;
    (void)vy;
}

/* 7. Multiple __asm blocks in a single function */
void test_ms_multi_block(void) {
    int a = 5;
    __asm {
        mov eax, a
        add eax, 10
        mov a, eax
    }
    __asm {
        mov eax, a
        shl eax, 1
        mov a, eax
    }
    (void)a;
}

/* 8. Bitwise operations */
void test_ms_bitwise(void) {
    int mask = 0xFF00;
    int val = 0x1234;
    int result;
    __asm {
        mov eax, val
        and eax, mask
        mov result, eax
    }
    (void)result;
}

/* 9. Nested local variable references (loops, multiple vars) */
void test_ms_locals(void) {
    int x = 3, y = 7;
    int sum, xor_val;
    __asm {
        mov eax, x
        add eax, y
        mov sum, eax

        mov eax, x
        xor eax, y
        mov xor_val, eax
    }
    (void)sum;
    (void)xor_val;
}

/* 10. _asm alias keyword (alternate spelling) */
void test_ms_asm_alias(void) {
    _asm {
        nop
    }
    _asm nop
}

int main(void) {
    test_ms_basic();
    test_ms_register_ops();
    test_ms_arithmetic();
    test_ms_single_line();
    test_ms_labels();
    test_ms_struct_access();
    test_ms_multi_block();
    test_ms_bitwise();
    test_ms_locals();
    test_ms_asm_alias();
    return 0;
}
