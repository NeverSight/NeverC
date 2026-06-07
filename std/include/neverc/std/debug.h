#ifndef NEVERC_DEBUG_H
#define NEVERC_DEBUG_H

#include "debug/elf.h"
#include "debug/pe.h"
#include "debug/macho.h"
#include "debug/dwarf.h"
#include "debug/plan9obj.h"

#ifdef __neverc__

struct __neverc_std_elf_t { char __tag; };
struct __neverc_std_pe_t { char __tag; };
struct __neverc_std_macho_t { char __tag; };
struct __neverc_std_dwarf_t { char __tag; };
struct __neverc_std_plan9obj_t { char __tag; };

struct __neverc_std_debug_t {
    struct __neverc_std_elf_t elf;
    struct __neverc_std_pe_t pe;
    struct __neverc_std_macho_t macho;
    struct __neverc_std_dwarf_t dwarf;
    struct __neverc_std_plan9obj_t plan9obj;
};

extern struct __neverc_std_debug_t __neverc_mod_debug;
extern struct __neverc_std_debug_t debug;
#endif /* __neverc__ */

#endif /* NEVERC_DEBUG_H */
