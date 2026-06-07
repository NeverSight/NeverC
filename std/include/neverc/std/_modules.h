#ifndef NEVERC_STD_MODULES_H
#define NEVERC_STD_MODULES_H

/*
 * Root `std` namespace for NeverC dot-syntax: std.math.sqrt(x)
 *
 * Only available in .nc files (guarded by __neverc__).
 * The Sema layer recognizes __neverc_std_root_t and synthesizes module
 * member access — no real struct members are needed.
 */

#ifdef __neverc__

struct __neverc_std_root_t { char __tag; };
extern struct __neverc_std_root_t std;

#endif /* __neverc__ */

#endif /* NEVERC_STD_MODULES_H */
