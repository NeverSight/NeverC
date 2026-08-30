#ifndef NEVERC_DEBUG_ELF_H
#define NEVERC_DEBUG_ELF_H

/*
 * ELF (Executable and Linkable Format) parser.
 * Supports 32-bit and 64-bit ELF files on all platforms.
 * API modeled after Go's debug/elf package.
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===== ELF Constants ===== */

/* ELF magic */
#define NEVERC_ELF_MAGIC "\177ELF"

/* EI_CLASS */
#define NEVERC_ELFCLASSNONE 0
#define NEVERC_ELFCLASS32   1
#define NEVERC_ELFCLASS64   2

/* EI_DATA */
#define NEVERC_ELFDATANONE 0
#define NEVERC_ELFDATA2LSB 1
#define NEVERC_ELFDATA2MSB 2

/* e_type */
#define NEVERC_ET_NONE 0
#define NEVERC_ET_REL  1
#define NEVERC_ET_EXEC 2
#define NEVERC_ET_DYN  3
#define NEVERC_ET_CORE 4

/* e_machine */
#define NEVERC_EM_NONE    0
#define NEVERC_EM_386     3
#define NEVERC_EM_ARM     40
#define NEVERC_EM_X86_64  62
#define NEVERC_EM_AARCH64 183
#define NEVERC_EM_RISCV   243
#define NEVERC_EM_MIPS    8
#define NEVERC_EM_PPC     20
#define NEVERC_EM_PPC64   21

/* p_type */
#define NEVERC_PT_NULL    0
#define NEVERC_PT_LOAD    1
#define NEVERC_PT_DYNAMIC 2
#define NEVERC_PT_INTERP  3
#define NEVERC_PT_NOTE    4
#define NEVERC_PT_SHLIB   5
#define NEVERC_PT_PHDR    6
#define NEVERC_PT_TLS     7

/* sh_type */
#define NEVERC_SHT_NULL     0
#define NEVERC_SHT_PROGBITS 1
#define NEVERC_SHT_SYMTAB   2
#define NEVERC_SHT_STRTAB   3
#define NEVERC_SHT_RELA     4
#define NEVERC_SHT_HASH     5
#define NEVERC_SHT_DYNAMIC  6
#define NEVERC_SHT_NOTE     7
#define NEVERC_SHT_NOBITS   8
#define NEVERC_SHT_REL      9
#define NEVERC_SHT_DYNSYM   11

/* sh_flags */
#define NEVERC_SHF_WRITE     0x1
#define NEVERC_SHF_ALLOC     0x2
#define NEVERC_SHF_EXECINSTR 0x4

/* Symbol binding (ELF32_ST_BIND / ELF64_ST_BIND) */
#define NEVERC_STB_LOCAL  0
#define NEVERC_STB_GLOBAL 1
#define NEVERC_STB_WEAK   2

/* Symbol type (ELF32_ST_TYPE / ELF64_ST_TYPE) */
#define NEVERC_STT_NOTYPE  0
#define NEVERC_STT_OBJECT  1
#define NEVERC_STT_FUNC    2
#define NEVERC_STT_SECTION 3
#define NEVERC_STT_FILE    4
#define NEVERC_STT_COMMON  5
#define NEVERC_STT_TLS     6

/* Symbol visibility */
#define NEVERC_STV_DEFAULT   0
#define NEVERC_STV_INTERNAL  1
#define NEVERC_STV_HIDDEN    2
#define NEVERC_STV_PROTECTED 3

/* Special section indices */
#define NEVERC_SHN_UNDEF  0
#define NEVERC_SHN_LORESERVE 0xFF00
#define NEVERC_SHN_ABS    0xFFF1
#define NEVERC_SHN_COMMON 0xFFF2
#define NEVERC_SHN_XINDEX 0xFFFF

/* e_phnum sentinel: real count is sh_info of section 0 */
#define NEVERC_PN_XNUM 0xFFFF

/* ===== Types ===== */

typedef struct {
    uint8_t  ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint8_t  class_;
    uint8_t  data;
    uint8_t  osabi;
    uint8_t  abi_version;
} neverc_elf_file_header_t;

typedef struct {
    char     name[256];
    uint32_t type;
    uint64_t flags;
    uint64_t addr;
    uint64_t offset;
    uint64_t size;
    uint32_t link;
    uint32_t info;
    uint64_t addralign;
    uint64_t entsize;
    uint32_t name_idx;
} neverc_elf_section_t;

typedef struct {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
} neverc_elf_prog_t;

typedef struct {
    char     name[256];
    uint64_t value;
    uint64_t size;
    uint8_t  bind;
    uint8_t  type;
    uint8_t  visibility;
    uint16_t section;
} neverc_elf_symbol_t;

typedef struct {
    neverc_elf_file_header_t header;

    neverc_elf_section_t *sections;
    uint32_t              section_count;

    neverc_elf_prog_t *progs;
    uint32_t           prog_count;

    const uint8_t *data;
    size_t         data_len;
    int            owns_data;
} neverc_elf_file_t;

/* ===== Functions ===== */

/* Parse ELF from memory buffer. Returns 0 on success, -1 on error. */
int neverc_elf_open(neverc_elf_file_t *f, const uint8_t *data, size_t len);

/* Free resources. */
void neverc_elf_close(neverc_elf_file_t *f);

/* Open ELF file from path. Returns 0 on success, -1 on error. */
int neverc_elf_open_file(neverc_elf_file_t *f, const char *path);

/* Find a section by name. Returns NULL if not found. */
const neverc_elf_section_t *neverc_elf_section(const neverc_elf_file_t *f,
                                                const char *name);

/* Find a section by type. Returns NULL if not found. */
const neverc_elf_section_t *neverc_elf_section_by_type(const neverc_elf_file_t *f,
                                                        uint32_t type);

/* Read section data. Caller must free *out. Returns 0 on success. */
int neverc_elf_section_data(const neverc_elf_file_t *f,
                             const neverc_elf_section_t *s,
                             uint8_t **out, size_t *out_len);

/* Get symbol table. Caller must free *syms. Returns symbol count, or -1 on error. */
int neverc_elf_symbols(const neverc_elf_file_t *f,
                        neverc_elf_symbol_t **syms, int *count);

/* Get dynamic symbols. Caller must free *syms. */
int neverc_elf_dynamic_symbols(const neverc_elf_file_t *f,
                                neverc_elf_symbol_t **syms, int *count);

/* Get imported libraries. Returns count, fills names array (max_names entries).
   Each name points into the file's string tables — do not free individually. */
int neverc_elf_imported_libraries(const neverc_elf_file_t *f,
                                   char **names, int max_names);

/* Check if it's a valid ELF. */
int neverc_elf_is_valid(const uint8_t *data, size_t len);

/* Machine name string (e.g. "x86-64", "AArch64"). */
const char *neverc_elf_machine_string(uint16_t machine);

/* Type name string (e.g. "EXEC", "DYN"). */
const char *neverc_elf_type_string(uint16_t type);

#ifdef __cplusplus
}
#endif

/* ===== Std Module Dot-Syntax Support ===== */
#ifdef __neverc__
#include <neverc/std/debug.h>
#endif

#endif /* NEVERC_DEBUG_ELF_H */
