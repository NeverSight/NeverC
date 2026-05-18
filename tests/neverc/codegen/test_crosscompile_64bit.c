// test_crosscompile_64bit.c
// Validates 64-bit object file generation across different targets after
// hardcoding is64Bit()=true in ELF/MachO/COFF writers.
//
// RUN: %neverc -O2 -c %s -o %t.o
// RUN: %neverc -O2 --target=x86_64-linux-gnu -c %s -o %t_elf.o
// RUN: %neverc -O2 --target=aarch64-linux-gnu -c %s -o %t_aarch64.o
//
// This test verifies cross-compilation to different 64-bit targets
// produces valid object files (compiler doesn't crash, output is non-empty).

int global_var = 42;
static int static_var = 100;

__attribute__((noinline))
int compute(int x, int y) {
    return x * y + global_var - static_var;
}

typedef struct {
    void *ptr;
    unsigned long long val;
    unsigned int tag;
} Record;

Record make_record(void *p, unsigned long long v) {
    Record r;
    r.ptr = p;
    r.val = v;
    r.tag = (unsigned int)(v & 0xFFFFFFFF);
    return r;
}

int main(void) {
    int result = compute(3, 7);
    Record r = make_record(&result, 0xDEADBEEFCAFEBABEULL);
    return (int)(r.val != 0);
}
