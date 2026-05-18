#ifndef __STDALIGN_H
#define __STDALIGN_H

#if defined(__STDC_VERSION__) && __STDC_VERSION__ < 202311L
#define alignas _Alignas
#define alignof _Alignof
#define __alignas_is_defined 1
#define __alignof_is_defined 1
#endif

#endif /* __STDALIGN_H */
