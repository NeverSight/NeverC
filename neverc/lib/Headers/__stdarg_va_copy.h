#ifndef va_copy
#define va_copy(dest, src) __builtin_va_copy(dest, src)
#endif
