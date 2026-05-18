// RUN: %neverc -c %s -o %t.hello.o
// RUN: test -s %t.hello.o
// RUN: %neverc -B %neverc_bindir %t.hello.o -o %t.hello
// RUN: test -s %t.hello
// RUN: %neverc -flto -c %s -o %t.hello.lto.o
// RUN: test -s %t.hello.lto.o
// RUN: %neverc -B %neverc_bindir -flto %t.hello.lto.o -o %t.hello.lto
// RUN: test -s %t.hello.lto

int add(int a, int b) { return a + b; }
int main(void) { return add(1, 2); }
