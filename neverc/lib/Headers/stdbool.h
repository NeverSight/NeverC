#ifndef __STDBOOL_H
#define __STDBOOL_H

#define __bool_true_false_are_defined 1

#if defined(__STDC_VERSION__) && __STDC_VERSION__ > 201710L
/* C23: bool, true, false are keywords; macros here for backward compat only. */
#else
#define bool _Bool
#define true 1
#define false 0
#endif

#endif /* __STDBOOL_H */
