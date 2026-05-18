#ifndef _NULLPTR_T
#define _NULLPTR_T

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
typedef typeof(nullptr) nullptr_t;
#endif

#endif
