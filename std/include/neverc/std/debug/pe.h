#ifndef NEVERC_DEBUG_PE_H
#define NEVERC_DEBUG_PE_H

/*
 * PE (Portable Executable) format parser.
 * Supports PE32 and PE32+ (64-bit) files.
 * API modeled after Go's debug/pe package.
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===== PE Constants ===== */

#define NEVERC_PE_SIGNATURE 0x00004550  /* "PE\0\0" */

/* Machine types */
#define NEVERC_IMAGE_FILE_MACHINE_UNKNOWN   0x0
#define NEVERC_IMAGE_FILE_MACHINE_I386      0x14c
#define NEVERC_IMAGE_FILE_MACHINE_AMD64     0x8664
#define NEVERC_IMAGE_FILE_MACHINE_ARM       0x1c0
#define NEVERC_IMAGE_FILE_MACHINE_ARMNT     0x1c4
#define NEVERC_IMAGE_FILE_MACHINE_ARM64     0xaa64

/* Characteristics */
#define NEVERC_IMAGE_FILE_EXECUTABLE_IMAGE    0x0002
#define NEVERC_IMAGE_FILE_LARGE_ADDRESS_AWARE 0x0020
#define NEVERC_IMAGE_FILE_DLL                 0x2000

/* Optional header magic */
#define NEVERC_PE32_MAGIC  0x10b
#define NEVERC_PE32P_MAGIC 0x20b  /* PE32+ (64-bit) */

/* Section characteristics */
#define NEVERC_IMAGE_SCN_CNT_CODE               0x00000020
#define NEVERC_IMAGE_SCN_CNT_INITIALIZED_DATA   0x00000040
#define NEVERC_IMAGE_SCN_CNT_UNINITIALIZED_DATA 0x00000080
#define NEVERC_IMAGE_SCN_MEM_EXECUTE            0x20000000
#define NEVERC_IMAGE_SCN_MEM_READ               0x40000000
#define NEVERC_IMAGE_SCN_MEM_WRITE              0x80000000

/* Data directory indices (matching Go debug/pe IMAGE_DIRECTORY_ENTRY_*) */
#define NEVERC_IMAGE_DIRECTORY_ENTRY_EXPORT         0
#define NEVERC_IMAGE_DIRECTORY_ENTRY_IMPORT         1
#define NEVERC_IMAGE_DIRECTORY_ENTRY_RESOURCE       2
#define NEVERC_IMAGE_DIRECTORY_ENTRY_EXCEPTION      3
#define NEVERC_IMAGE_DIRECTORY_ENTRY_SECURITY       4  /* file offset, not RVA */
#define NEVERC_IMAGE_DIRECTORY_ENTRY_BASERELOC      5
#define NEVERC_IMAGE_DIRECTORY_ENTRY_DEBUG          6
#define NEVERC_IMAGE_DIRECTORY_ENTRY_ARCHITECTURE   7
#define NEVERC_IMAGE_DIRECTORY_ENTRY_GLOBALPTR      8
#define NEVERC_IMAGE_DIRECTORY_ENTRY_TLS            9
#define NEVERC_IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG    10
#define NEVERC_IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT   11
#define NEVERC_IMAGE_DIRECTORY_ENTRY_IAT            12
#define NEVERC_IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT   13
#define NEVERC_IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR 14

/* Symbol storage class */
#define NEVERC_IMAGE_SYM_CLASS_EXTERNAL 2
#define NEVERC_IMAGE_SYM_CLASS_STATIC   3
#define NEVERC_IMAGE_SYM_CLASS_FILE     103

/* ===== Types ===== */

typedef struct {
    uint16_t machine;
    uint16_t number_of_sections;
    uint32_t time_date_stamp;
    uint32_t pointer_to_symbol_table;
    uint32_t number_of_symbols;
    uint16_t size_of_optional_header;
    uint16_t characteristics;
} neverc_pe_file_header_t;

typedef struct {
    uint32_t virtual_address;
    uint32_t size;
} neverc_pe_data_directory_t;

typedef struct {
    uint16_t magic;
    uint64_t image_base;
    uint32_t section_alignment;
    uint32_t file_alignment;
    uint64_t size_of_image;
    uint64_t size_of_headers;
    uint32_t subsystem;
    uint32_t number_of_rva_and_sizes;
    neverc_pe_data_directory_t data_directory[16];
} neverc_pe_optional_header_t;

typedef struct {
    char     name[9]; /* Raw COFF name; use neverc_pe_section_name(). */
    uint32_t virtual_size;
    uint32_t virtual_address;
    uint32_t size_of_raw_data;
    uint32_t pointer_to_raw_data;
    uint32_t pointer_to_relocations;
    uint32_t number_of_relocations;
    uint32_t characteristics;
} neverc_pe_section_t;

typedef struct {
    char     name[256];
    uint32_t value;
    int16_t  section_number;
    uint16_t type;
    uint8_t  storage_class;
} neverc_pe_symbol_t;

typedef struct {
    neverc_pe_file_header_t     file_header;
    neverc_pe_optional_header_t optional_header;
    int                         is_64bit;

    neverc_pe_section_t *sections;
    uint32_t             section_count;

    const uint8_t *data;
    size_t         data_len;
    int            owns_data;
} neverc_pe_file_t;

/* ===== Functions ===== */

int neverc_pe_open(neverc_pe_file_t *f, const uint8_t *data, size_t len);
void neverc_pe_close(neverc_pe_file_t *f);
int neverc_pe_open_file(neverc_pe_file_t *f, const char *path);
int neverc_pe_is_valid(const uint8_t *data, size_t len);

const neverc_pe_section_t *neverc_pe_section(const neverc_pe_file_t *f,
                                              const char *name);
/* Returns the full COFF section name. The returned string is borrowed from
 * the file and remains valid until neverc_pe_close(). */
const char *neverc_pe_section_name(const neverc_pe_file_t *f,
                                   const neverc_pe_section_t *s);
int neverc_pe_section_data(const neverc_pe_file_t *f,
                            const neverc_pe_section_t *s,
                            uint8_t **out, size_t *out_len);

int neverc_pe_symbols(const neverc_pe_file_t *f,
                       neverc_pe_symbol_t **syms, int *count);
/* Import names are borrowed NUL-terminated strings backed by f->data.
 * Returns the number written, or -1 for malformed input/invalid arguments. */
int neverc_pe_imported_symbols(const neverc_pe_file_t *f,
                                char **names, int max_names);
int neverc_pe_imported_libraries(const neverc_pe_file_t *f,
                                  char **names, int max_names);

const char *neverc_pe_machine_string(uint16_t machine);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
#include <neverc/std/debug.h>
#endif

#endif /* NEVERC_DEBUG_PE_H */
