#ifndef NEVERC_DEBUG_H
#define NEVERC_DEBUG_H

#ifdef __neverc__

struct __neverc_std_elf_t { char __tag; };
struct __neverc_std_pe_t { char __tag; };
struct __neverc_std_macho_t { char __tag; };
struct __neverc_std_dwarf_t { char __tag; };

struct __neverc_std_debug_t {
    struct __neverc_std_elf_t elf;
    struct __neverc_std_pe_t pe;
    struct __neverc_std_macho_t macho;
    struct __neverc_std_dwarf_t dwarf;
};

extern struct __neverc_std_debug_t __neverc_mod_debug;
#endif /* __neverc__ */

#endif /* NEVERC_DEBUG_H */
