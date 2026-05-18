// RUN: %neverc -x c -fms-extensions -target x86_64-pc-windows-msvc -fsyntax-only %s
// RUN: %neverc -x c -fms-extensions -target x86_64-pc-windows-msvc -c %s -o %t.seh.obj
// RUN: test -s %t.seh.obj

int f(void) {
  __try {
    return 0;
  } __except (1) {
    return 1;
  }
}

int g(void) {
  __try {
    __leave;
    return 2;
  } __finally {
    (void)0;
  }
  return 3;
}
