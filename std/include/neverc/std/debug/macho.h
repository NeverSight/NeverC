#ifndef NEVERC_DEBUG_MACHO_H
#define NEVERC_DEBUG_MACHO_H

/*
 * Mach-O (macOS/iOS executable) format parser.
 * Supports thin 32-bit and 64-bit Mach-O binaries.
 * API modeled after Go's debug/macho package.
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===== Mach-O Constants ===== */

#define NEVERC_MH_MAGIC    0xFEEDFACE
#define NEVERC_MH_MAGIC_64 0xFEEDFACF
#define NEVERC_MH_CIGAM    0xCEFAEDFE
#define NEVERC_MH_CIGAM_64 0xCFFAEDFE
#define NEVERC_FAT_MAGIC   0xCAFEBABE
#define NEVERC_FAT_CIGAM   0xBEBAFECA

/* File types */
#define NEVERC_MH_OBJECT   1
#define NEVERC_MH_EXECUTE  2
#define NEVERC_MH_DYLIB    6
#define NEVERC_MH_DYLINKER 7
#define NEVERC_MH_BUNDLE   8
#define NEVERC_MH_DSYM     10

/* CPU types */
#define NEVERC_CPU_TYPE_X86    7
#define NEVERC_CPU_TYPE_X86_64 (NEVERC_CPU_TYPE_X86 | 0x01000000)
#define NEVERC_CPU_TYPE_ARM    12
#define NEVERC_CPU_TYPE_ARM64  (NEVERC_CPU_TYPE_ARM | 0x01000000)

/* Load command types */
#define NEVERC_LC_SEGMENT       0x01
#define NEVERC_LC_SYMTAB        0x02
#define NEVERC_LC_DYSYMTAB      0x0b
#define NEVERC_LC_LOAD_DYLIB    0x0c
#define NEVERC_LC_ID_DYLIB      0x0d
#define NEVERC_LC_SEGMENT_64    0x19
#define NEVERC_LC_UUID          0x1b
#define NEVERC_LC_RPATH         0x8000001c
#define NEVERC_LC_MAIN          0x80000028

/* Symbol N_TYPE masks */
#define NEVERC_N_STAB 0xe0
#define NEVERC_N_PEXT 0x10
#define NEVERC_N_TYPE 0x0e
#define NEVERC_N_EXT  0x01
#define NEVERC_N_UNDF 0x0
#define NEVERC_N_ABS  0x2
#define NEVERC_N_SECT 0xe
#define NEVERC_N_INDR 0xa

/* ===== Types ===== */

typedef struct {
    uint32_t magic;
    int32_t  cpu;
    int32_t  subcpu;
    uint32_t type;
    uint32_t ncmds;
    uint32_t sizeofcmds;
    uint32_t flags;
} neverc_macho_header_t;

typedef struct {
    char     name[17];
    char     segname[17];
    uint64_t addr;
    uint64_t size;
    uint32_t offset;
    uint32_t align;
    uint32_t reloff;
    uint32_t nreloc;
    uint32_t flags;
} neverc_macho_section_t;

typedef struct {
    char     name[17];
    uint64_t addr;
    uint64_t memsz;
    uint64_t offset;
    uint64_t filesz;
    uint32_t maxprot;
    uint32_t prot;
    uint32_t nsects;
    uint32_t flag;
} neverc_macho_segment_t;

typedef struct {
    char     name[256];
    uint8_t  type;
    uint8_t  sect;
    int16_t  desc;
    uint64_t value;
} neverc_macho_symbol_t;

typedef struct {
    char     name[256];
    uint32_t time;
    uint32_t current_version;
    uint32_t compat_version;
} neverc_macho_dylib_t;

typedef struct {
    neverc_macho_header_t header;
    int is_64bit;
    int is_swap;

    neverc_macho_section_t  *sections;
    uint32_t                 section_count;

    neverc_macho_segment_t  *segments;
    uint32_t                 segment_count;

    neverc_macho_dylib_t    *dylibs;
    uint32_t                 dylib_count;

    const uint8_t *data;
    size_t         data_len;
    int            owns_data;

    /* symtab load command offsets */
    uint32_t symoff, nsyms, stroff, strsize;
} neverc_macho_file_t;

/* ===== Functions ===== */

int neverc_macho_open(neverc_macho_file_t *f, const uint8_t *data, size_t len);
void neverc_macho_close(neverc_macho_file_t *f);
int neverc_macho_open_file(neverc_macho_file_t *f, const char *path);
int neverc_macho_is_valid(const uint8_t *data, size_t len);
int neverc_macho_is_fat(const uint8_t *data, size_t len);

const neverc_macho_section_t *neverc_macho_section(const neverc_macho_file_t *f,
                                                     const char *name);
const neverc_macho_segment_t *neverc_macho_segment(const neverc_macho_file_t *f,
                                                     const char *name);
int neverc_macho_section_data(const neverc_macho_file_t *f,
                               const neverc_macho_section_t *s,
                               uint8_t **out, size_t *out_len);

int neverc_macho_symbols(const neverc_macho_file_t *f,
                          neverc_macho_symbol_t **syms, int *count);

const char *neverc_macho_cpu_string(int32_t cpu);
const char *neverc_macho_type_string(uint32_t type);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
#include <neverc/std/_modules.h>
#endif

#endif /* NEVERC_DEBUG_MACHO_H */
