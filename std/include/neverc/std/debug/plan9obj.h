#ifndef NEVERC_DEBUG_PLAN9OBJ_H
#define NEVERC_DEBUG_PLAN9OBJ_H

/*
 * Plan 9 a.out object file parser.
 * API modeled after Go's debug/plan9obj package.
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Magic numbers */
#define NEVERC_PLAN9_MAGIC64    0x8000
#define NEVERC_PLAN9_MAGIC386   ((4*11+0)*11 + 7)            /* 491 */
#define NEVERC_PLAN9_MAGICAMD64 ((4*26+0)*26 + 7 + 0x8000)  /* 0x8000 + 2743 */
#define NEVERC_PLAN9_MAGICARM   ((4*20+0)*20 + 7)            /* 1607 */

/* ===== Types ===== */

typedef struct {
    uint32_t magic;
    uint32_t text;
    uint32_t data;
    uint32_t bss;
    uint32_t syms;
    uint32_t entry_lo;
    uint32_t spsz;
    uint32_t pcsz;
} neverc_plan9_prog_t;

typedef struct {
    char    *name;
    uint32_t size;
    uint64_t offset;
} neverc_plan9_section_t;

typedef struct {
    uint64_t value;
    char     type;
    char    *name;
} neverc_plan9_sym_t;

typedef struct {
    uint32_t magic;
    uint32_t bss;
    uint64_t entry;
    int      ptr_size;
    uint64_t load_address;
    uint64_t hdr_size;

    neverc_plan9_section_t *sections;
    int                     num_sections;

    neverc_plan9_sym_t *symbols;
    int                 num_symbols;

    uint8_t *data;
    size_t   data_len;
} neverc_plan9_file_t;

/* Open and parse a Plan 9 a.out binary from a file path. Returns 0 on success. */
int  neverc_plan9_open(neverc_plan9_file_t *f, const char *path);

/* Parse a Plan 9 a.out binary from memory buffer. Returns 0 on success. */
int  neverc_plan9_parse(neverc_plan9_file_t *f, const uint8_t *buf, size_t len);

/* Free all resources associated with a parsed file. */
void neverc_plan9_close(neverc_plan9_file_t *f);

/* Find a section by name. Returns NULL if not found. */
neverc_plan9_section_t *neverc_plan9_section(neverc_plan9_file_t *f, const char *name);

/* Read section data. Caller must provide buf of at least sect->size bytes.
 * Returns 0 on success, -1 on failure. */
int  neverc_plan9_section_data(neverc_plan9_file_t *f,
                                neverc_plan9_section_t *sect,
                                uint8_t *buf, size_t cap);

/* Parse symbol table, including Plan 9 z/Z file-path references.
 * Populates f->symbols / f->num_symbols and returns 0 on success. */
int  neverc_plan9_symbols(neverc_plan9_file_t *f);

/* Check if a magic number is valid Plan 9 a.out. */
int  neverc_plan9_valid_magic(uint32_t magic);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/debug.h>
#endif


#endif /* NEVERC_DEBUG_PLAN9OBJ_H */
