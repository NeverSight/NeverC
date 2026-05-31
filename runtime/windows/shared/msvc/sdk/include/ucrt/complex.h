//
// complex.h
//
//      Minimal C99 <complex.h> for neverc on Windows targets.
//
// The bundled UCRT subset ships no <complex.h>, and the real MSVC UCRT header
// models complex numbers with the _Dcomplex struct because MSVC has no native
// _Complex support.  neverc's Clang-based frontend *does* support the C99
// _Complex types, so this header provides the standard macros plus the part-
// extraction / conjugate routines as inline builtins (no UCRT dependency).
// The transcendental complex routines are declared for source compatibility
// and resolve against the math runtime only if actually referenced.
//
#pragma once
#ifndef _NEVERC_COMPLEX_H
#define _NEVERC_COMPLEX_H

#define complex    _Complex
#define _Complex_I __builtin_complex(0.0f, 1.0f)
#define I          _Complex_I

#define __NEVERC_CMPLX                                                          \
  static __inline__ __attribute__((__always_inline__, __unused__))

__NEVERC_CMPLX double creal(double _Complex __z) { return __real__ __z; }
__NEVERC_CMPLX double cimag(double _Complex __z) { return __imag__ __z; }
__NEVERC_CMPLX float crealf(float _Complex __z) { return __real__ __z; }
__NEVERC_CMPLX float cimagf(float _Complex __z) { return __imag__ __z; }
__NEVERC_CMPLX long double creall(long double _Complex __z) {
  return __real__ __z;
}
__NEVERC_CMPLX long double cimagl(long double _Complex __z) {
  return __imag__ __z;
}

__NEVERC_CMPLX double _Complex conj(double _Complex __z) {
  return __builtin_conj(__z);
}
__NEVERC_CMPLX float _Complex conjf(float _Complex __z) {
  return __builtin_conjf(__z);
}
__NEVERC_CMPLX long double _Complex conjl(long double _Complex __z) {
  return __builtin_conjl(__z);
}

#undef __NEVERC_CMPLX

/* Transcendental complex math — declared only; provided by the math runtime
 * when referenced.  Kept minimal but complete enough for typical C99 code. */
double _Complex cacos(double _Complex);
double _Complex casin(double _Complex);
double _Complex catan(double _Complex);
double _Complex ccos(double _Complex);
double _Complex csin(double _Complex);
double _Complex ctan(double _Complex);
double _Complex cexp(double _Complex);
double _Complex clog(double _Complex);
double cabs(double _Complex);
double _Complex cpow(double _Complex, double _Complex);
double _Complex csqrt(double _Complex);
double carg(double _Complex);
double _Complex cproj(double _Complex);

float _Complex cacosf(float _Complex);
float _Complex casinf(float _Complex);
float _Complex catanf(float _Complex);
float _Complex ccosf(float _Complex);
float _Complex csinf(float _Complex);
float _Complex ctanf(float _Complex);
float _Complex cexpf(float _Complex);
float _Complex clogf(float _Complex);
float cabsf(float _Complex);
float _Complex cpowf(float _Complex, float _Complex);
float _Complex csqrtf(float _Complex);
float cargf(float _Complex);
float _Complex cprojf(float _Complex);

#endif /* _NEVERC_COMPLEX_H */
