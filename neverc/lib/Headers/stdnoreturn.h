#ifndef __STDNORETURN_H
#define __STDNORETURN_H

#define noreturn _Noreturn
#define __noreturn_is_defined 1

#if (defined(__STDC_VERSION__) && __STDC_VERSION__ > 201710L) &&               \
    !defined(_NEVERC_DISABLE_CRT_DEPRECATION_WARNINGS)
/* The noreturn macro is deprecated in C23. We do not mark it as such because
   including the header file in C23 is also deprecated and we do not want to
   issue a confusing diagnostic for code which includes <stdnoreturn.h>
   followed by code that writes [[noreturn]]. The issue with such code is not
   with the attribute, or the use of 'noreturn', but the inclusion of the
   header. */
#endif

#endif /* __STDNORETURN_H */
