// RUN: %neverc --target=aarch64-linux-android -fsyntax-only %s
/* Minimal source for -fsyntax-only against aarch64-linux-android* (no libc). */
typedef unsigned long size_t;
void *malloc(size_t);
int x;
