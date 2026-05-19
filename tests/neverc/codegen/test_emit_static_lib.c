// In-process static archive emission test
// RUN: %neverc -DSTATIC_LIB_MEMBER -c %s -o %t.member.o
// RUN: %neverc --emit-static-lib %t.member.o -o %t.print.a -### 2>&1 | grep -F "(in-process archive)"
// RUN: env PATH=/no-such-neverc-archive-tool %neverc --emit-static-lib %t.member.o -o %t.a
// RUN: test -s %t.a
// RUN: %neverc %s %t.a -o %t
// RUN: %t

#ifdef STATIC_LIB_MEMBER

int neverc_static_lib_base(void) {
  return 40;
}

int neverc_static_lib_delta(void) {
  return 2;
}

#else

extern int neverc_static_lib_base(void);
extern int neverc_static_lib_delta(void);

int main(void) {
  return neverc_static_lib_base() + neverc_static_lib_delta() == 42 ? 0 : 1;
}

#endif
