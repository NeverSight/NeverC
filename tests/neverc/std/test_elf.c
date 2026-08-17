/*
 * ELF parser test suite.
 * Tests with synthesized minimal ELF64 (little-endian, x86-64) binary data.
 */
#include "neverc/std/debug/elf.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <stdint.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define CHECK(name, cond) do { \
    tests_run++; \
    if (cond) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL: %s\n", name); } \
} while(0)

/*
 * Build a minimal ELF64 LE binary in memory:
 *   ELF header (64 bytes)
 *   Section header string table (.shstrtab) at offset 64
 *   Section headers at offset 64+shstrtab_size (3 entries: NULL + .text + .shstrtab)
 *   .text section data at offset after section headers
 */
static uint8_t *build_minimal_elf64(size_t *out_len) {
    /* .shstrtab content: "\0.text\0.shstrtab\0" */
    const char shstrtab[] = "\0.text\0.shstrtab\0";
    size_t shstrtab_len = sizeof(shstrtab) - 1;  /* 17 bytes */

    /* .text content */
    uint8_t text_data[] = {0xCC, 0xC3, 0x90, 0xEB, 0xFE}; /* int3; ret; nop; jmp $ */
    size_t text_len = sizeof(text_data);

    size_t ehdr_sz = 64;
    size_t shdr_sz = 64;  /* each section header */
    size_t shstrtab_off = ehdr_sz;
    size_t text_off = shstrtab_off + shstrtab_len;
    /* Align text_off to 16 */
    text_off = (text_off + 15) & ~(size_t)15;
    size_t shdr_off = text_off + text_len;
    shdr_off = (shdr_off + 7) & ~(size_t)7;
    size_t total = shdr_off + 3 * shdr_sz;

    uint8_t *buf = (uint8_t *)calloc(total, 1);
    if (!buf) return NULL;

    /* ELF header */
    buf[0] = 0x7f; buf[1] = 'E'; buf[2] = 'L'; buf[3] = 'F';
    buf[4] = 2;  /* ELFCLASS64 */
    buf[5] = 1;  /* ELFDATA2LSB */
    buf[6] = 1;  /* EV_CURRENT */
    buf[7] = 0;  /* ELFOSABI_NONE */

    /* e_type = ET_EXEC (2) */
    buf[16] = 2; buf[17] = 0;
    /* e_machine = EM_X86_64 (62) */
    buf[18] = 62; buf[19] = 0;
    /* e_version = 1 */
    buf[20] = 1;
    /* e_entry = 0x401000 */
    buf[24] = 0x00; buf[25] = 0x10; buf[26] = 0x40;
    /* e_phoff = 0 (no program headers) */
    /* e_shoff */
    buf[40] = (uint8_t)(shdr_off);
    buf[41] = (uint8_t)(shdr_off >> 8);
    buf[42] = (uint8_t)(shdr_off >> 16);
    buf[43] = (uint8_t)(shdr_off >> 24);
    /* e_ehsize = 64 */
    buf[52] = 64;
    /* e_shentsize = 64 */
    buf[58] = 64;
    /* e_shnum = 3 */
    buf[60] = 3;
    /* e_shstrndx = 2 */
    buf[62] = 2;

    /* .shstrtab content */
    memcpy(buf + shstrtab_off, shstrtab, shstrtab_len);

    /* .text content */
    memcpy(buf + text_off, text_data, text_len);

    /* Section headers */
    uint8_t *sh = buf + shdr_off;

    /* [0] SHT_NULL — all zeros (already calloc'd) */

    /* [1] .text */
    uint8_t *sh1 = sh + shdr_sz;
    /* sh_name = 1 (offset of ".text" in shstrtab) */
    sh1[0] = 1;
    /* sh_type = SHT_PROGBITS (1) */
    sh1[4] = 1;
    /* sh_flags = SHF_ALLOC | SHF_EXECINSTR (6) */
    sh1[8] = 6;
    /* sh_addr = 0x401000 */
    sh1[16] = 0x00; sh1[17] = 0x10; sh1[18] = 0x40;
    /* sh_offset */
    sh1[24] = (uint8_t)(text_off);
    sh1[25] = (uint8_t)(text_off >> 8);
    /* sh_size */
    sh1[32] = (uint8_t)text_len;
    /* sh_addralign = 16 */
    sh1[48] = 16;

    /* [2] .shstrtab */
    uint8_t *sh2 = sh + 2 * shdr_sz;
    /* sh_name = 7 (offset of ".shstrtab" in shstrtab) */
    sh2[0] = 7;
    /* sh_type = SHT_STRTAB (3) */
    sh2[4] = 3;
    /* sh_offset */
    sh2[24] = (uint8_t)(shstrtab_off);
    sh2[25] = (uint8_t)(shstrtab_off >> 8);
    /* sh_size */
    sh2[32] = (uint8_t)shstrtab_len;

    *out_len = total;
    return buf;
}

static void test_elf64_parse(void) {
    size_t len;
    uint8_t *data = build_minimal_elf64(&len);
    CHECK("build_elf64_not_null", data != NULL);
    if (!data) return;

    CHECK("is_valid", neverc_elf_is_valid(data, len));

    neverc_elf_file_t f;
    int rc = neverc_elf_open(&f, data, len);
    CHECK("open_success", rc == 0);

    CHECK("class_64", f.header.class_ == NEVERC_ELFCLASS64);
    CHECK("data_lsb", f.header.data == NEVERC_ELFDATA2LSB);
    CHECK("type_exec", f.header.type == NEVERC_ET_EXEC);
    CHECK("machine_x86_64", f.header.machine == NEVERC_EM_X86_64);
    CHECK("entry_point", f.header.entry == 0x401000);
    CHECK("section_count_3", f.section_count == 3);

    const neverc_elf_section_t *text = neverc_elf_section(&f, ".text");
    CHECK("text_found", text != NULL);
    if (text) {
        CHECK("text_type_progbits", text->type == NEVERC_SHT_PROGBITS);
        CHECK("text_size_5", text->size == 5);
        CHECK("text_flags_exec", (text->flags & NEVERC_SHF_EXECINSTR) != 0);

        uint8_t *sec_data = NULL;
        size_t sec_len = 0;
        rc = neverc_elf_section_data(&f, text, &sec_data, &sec_len);
        CHECK("text_data_read", rc == 0 && sec_data != NULL);
        if (sec_data) {
            CHECK("text_data_byte0_int3", sec_data[0] == 0xCC);
            CHECK("text_data_byte1_ret", sec_data[1] == 0xC3);
            free(sec_data);
        }
    }

    const neverc_elf_section_t *shstrtab = neverc_elf_section(&f, ".shstrtab");
    CHECK("shstrtab_found", shstrtab != NULL);
    if (shstrtab) {
        CHECK("shstrtab_type", shstrtab->type == NEVERC_SHT_STRTAB);
    }

    const neverc_elf_section_t *missing = neverc_elf_section(&f, ".bss");
    CHECK("bss_not_found", missing == NULL);

    const neverc_elf_section_t *by_type = neverc_elf_section_by_type(&f, NEVERC_SHT_STRTAB);
    CHECK("find_by_type", by_type != NULL);

    CHECK("machine_string", strcmp(neverc_elf_machine_string(NEVERC_EM_X86_64), "x86-64") == 0);
    CHECK("type_string_exec", strcmp(neverc_elf_type_string(NEVERC_ET_EXEC), "EXEC") == 0);
    CHECK("type_string_dyn", strcmp(neverc_elf_type_string(NEVERC_ET_DYN), "DYN") == 0);

    neverc_elf_close(&f);
    free(data);
}

static void test_elf_invalid(void) {
    uint8_t garbage[] = {0x00, 0x01, 0x02, 0x03};
    CHECK("invalid_too_short", !neverc_elf_is_valid(garbage, sizeof(garbage)));

    neverc_elf_file_t f;
    CHECK("open_fails_garbage", neverc_elf_open(&f, garbage, sizeof(garbage)) < 0);
    CHECK("open_fails_null", neverc_elf_open(&f, NULL, 0) < 0);
    CHECK("is_valid_rejects_null",
          !neverc_elf_is_valid(NULL, 16));
    CHECK("open_rejects_null_output",
          neverc_elf_open(NULL, garbage, sizeof(garbage)) < 0);

    uint8_t bad_encoding[64] = {0};
    bad_encoding[0] = 0x7f;
    bad_encoding[1] = 'E';
    bad_encoding[2] = 'L';
    bad_encoding[3] = 'F';
    bad_encoding[4] = NEVERC_ELFCLASS64;
    bad_encoding[5] = 3;
    bad_encoding[6] = 1;
    CHECK("unknown data encoding rejected",
          neverc_elf_open(&f, bad_encoding, sizeof(bad_encoding)) < 0);

    uint8_t elf32[52] = {0};
    elf32[0] = 0x7f;
    elf32[1] = 'E';
    elf32[2] = 'L';
    elf32[3] = 'F';
    elf32[4] = NEVERC_ELFCLASS32;
    elf32[5] = NEVERC_ELFDATA2LSB;
    elf32[6] = 1;
    elf32[16] = NEVERC_ET_REL;
    elf32[20] = 1;
    elf32[40] = 52;
    CHECK("minimal ELF32 header accepted",
          neverc_elf_open(&f, elf32, sizeof(elf32)) == 0);
    neverc_elf_close(&f);

    uint8_t *out = (uint8_t *)1;
    size_t out_len = 123;
    CHECK("section data validates outputs",
          neverc_elf_section_data(&f, NULL, NULL, &out_len) < 0);
    CHECK("empty section data succeeds",
          neverc_elf_section_data(&f, NULL, &out, &out_len) == 0 &&
          out == NULL && out_len == 0);
}

static void test_elf_symbol_count_overflow(void) {
    uint8_t placeholder = 0;
    neverc_elf_section_t sections[2];
    memset(sections, 0, sizeof(sections));
    sections[0].type = NEVERC_SHT_SYMTAB;
    sections[0].link = 1;
    sections[0].size = ((uint64_t)INT_MAX + 2) * 24;
    sections[0].entsize = 24;
    sections[1].type = NEVERC_SHT_STRTAB;
    sections[1].size = 1;

    neverc_elf_file_t f;
    memset(&f, 0, sizeof(f));
    f.data = &placeholder;
    f.data_len = SIZE_MAX;
    f.header.class_ = NEVERC_ELFCLASS64;
    f.header.data = NEVERC_ELFDATA2LSB;
    f.sections = sections;
    f.section_count = 2;

    neverc_elf_symbol_t *symbols = (neverc_elf_symbol_t *)1;
    int count = 123;
    CHECK("oversized symbol count rejected",
          neverc_elf_symbols(&f, &symbols, &count) < 0);
    CHECK("symbol outputs cleared",
          symbols == NULL && count == 0);
}

static void test_elf_malformed_tables(void) {
    size_t len = 0;
    uint8_t *data = build_minimal_elf64(&len);
    CHECK("build malformed ELF fixtures", data != NULL);
    if (!data) return;

    neverc_elf_file_t f;
    uint8_t saved_shstrndx = data[62];
    data[62] = 99;
    CHECK("reject shstrndx past section table",
          neverc_elf_open(&f, data, len) < 0);
    data[62] = saved_shstrndx;

    uint8_t *sh2 = NULL;
    /* Section headers start at e_shoff (ELF64, offset 40). */
    size_t shdr_off = (size_t)data[40] | ((size_t)data[41] << 8) |
                      ((size_t)data[42] << 16) | ((size_t)data[43] << 24);
    sh2 = data + shdr_off + 2 * 64;
    uint8_t saved_off = sh2[24];
    sh2[24] = 0xFF;
    sh2[25] = 0xFF;
    CHECK("reject shstrtab section past EOF",
          neverc_elf_open(&f, data, len) < 0);
    sh2[24] = saved_off;
    sh2[25] = 0;

    uint8_t saved_type = sh2[4];
    sh2[4] = NEVERC_SHT_NOBITS;
    CHECK("reject SHT_NOBITS as section name table",
          neverc_elf_open(&f, data, len) < 0);
    sh2[4] = saved_type;

    uint8_t *sh1 = data + shdr_off + 64;
    uint8_t saved_name = sh1[0];
    sh1[0] = 0xFF;
    CHECK("reject section name index past string table",
          neverc_elf_open(&f, data, len) < 0);
    sh1[0] = saved_name;

    /* Overwrite the whole .shstrtab so no name has a terminating NUL. */
    uint8_t saved_shstr[17];
    memcpy(saved_shstr, data + 64, sizeof(saved_shstr));
    memset(data + 64, 'A', sizeof(saved_shstr));
    CHECK("reject unterminated section name",
          neverc_elf_open(&f, data, len) < 0);
    memcpy(data + 64, saved_shstr, sizeof(saved_shstr));

    /* Keep in-range NULs for each name, but leave a trailing non-NUL so the
     * string table itself is not terminated. */
    uint8_t saved_sh_size = sh2[32];
    uint8_t saved_pad = data[64 + 17];
    sh2[32] = 18;
    data[64 + 17] = 'X';
    CHECK("reject string table without a terminating NUL",
          neverc_elf_open(&f, data, len) < 0);
    sh2[32] = saved_sh_size;
    data[64 + 17] = saved_pad;

    data[40] = 0; data[41] = 0; data[42] = 0; data[43] = 0;
    CHECK("reject shnum without a section header offset",
          neverc_elf_open(&f, data, len) < 0);
    data[40] = (uint8_t)shdr_off;
    data[41] = (uint8_t)(shdr_off >> 8);
    data[42] = (uint8_t)(shdr_off >> 16);
    data[43] = (uint8_t)(shdr_off >> 24);

    data[54] = 56; /* e_phentsize */
    data[56] = 1;  /* e_phnum = 1, e_phoff stays 0 */
    CHECK("reject phnum without a program header offset",
          neverc_elf_open(&f, data, len) < 0);
    data[54] = 0;
    data[56] = 0;

    /* e_shstrndx = 0 must not treat section 0's sh_size as a string table. */
    data[62] = 0;
    CHECK("SHN_UNDEF shstrndx opens without names",
          neverc_elf_open(&f, data, len) == 0);
    CHECK("SHN_UNDEF leaves .text unresolved",
          neverc_elf_section(&f, ".text") == NULL);
    neverc_elf_close(&f);
    data[62] = saved_shstrndx;

    uint8_t truncated[32];
    memcpy(truncated, data, sizeof(truncated));
    truncated[4] = NEVERC_ELFCLASS64;
    truncated[5] = NEVERC_ELFDATA2LSB;
    truncated[6] = 1;
    CHECK("reject truncated ELF64 header",
          neverc_elf_open(&f, truncated, sizeof(truncated)) < 0);

    free(data);
}

static void put16be(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v;
}
static void put32be(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static void put64le(uint8_t *p, uint64_t v) {
    int i;
    for (i = 0; i < 8; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}

static void test_elf_extended_and_truncated(void) {
    size_t len = 0;
    uint8_t *data = build_minimal_elf64(&len);
    CHECK("build extended ELF fixture", data != NULL);
    if (!data) return;

    neverc_elf_file_t f;
    size_t shdr_off = (size_t)data[40] | ((size_t)data[41] << 8) |
                      ((size_t)data[42] << 16) | ((size_t)data[43] << 24);

    /* e_shnum == 0 with a real table: count lives in section 0 sh_size. */
    data[60] = 0; data[61] = 0;
    data[shdr_off + 32] = 3;
    CHECK("extended e_shnum from section 0",
          neverc_elf_open(&f, data, len) == 0);
    CHECK("extended numbering keeps three sections",
          f.section_count == 3 && neverc_elf_section(&f, ".text") != NULL);
    {
        uint8_t *sec_data = (uint8_t *)1;
        size_t sec_len = 99;
        CHECK("SHT_NULL section 0 has no file bytes",
              neverc_elf_section_data(&f, &f.sections[0], &sec_data,
                                      &sec_len) == 0 &&
                  sec_data == NULL && sec_len == 0);
    }
    neverc_elf_close(&f);
    data[60] = 3;
    data[shdr_off + 32] = 0;

    data[62] = 0xFF; data[63] = 0xFF;
    data[shdr_off + 40] = 2; /* sh_link holds the real shstrndx */
    CHECK("SHN_XINDEX resolved from section 0",
          neverc_elf_open(&f, data, len) == 0);
    CHECK("SHN_XINDEX still names .text",
          neverc_elf_section(&f, ".text") != NULL);
    neverc_elf_close(&f);
    data[62] = 2; data[63] = 0;
    data[shdr_off + 40] = 0;

    /* PN_XNUM with no section table cannot be resolved. */
    data[56] = 0xFF; data[57] = 0xFF;
    data[40] = 0; data[41] = 0; data[42] = 0; data[43] = 0;
    data[60] = 0; data[61] = 0;
    CHECK("PN_XNUM without section 0 rejected",
          neverc_elf_open(&f, data, len) < 0);
    data[56] = 0; data[57] = 0;
    data[40] = (uint8_t)shdr_off;
    data[41] = (uint8_t)(shdr_off >> 8);
    data[42] = (uint8_t)(shdr_off >> 16);
    data[43] = (uint8_t)(shdr_off >> 24);
    data[60] = 3;

    data[58] = 8; /* e_shentsize too small for Elf64_Shdr */
    CHECK("reject undersized section header entries",
          neverc_elf_open(&f, data, len) < 0);
    data[58] = 64;

    uint8_t *sh1 = data + shdr_off + 64;
    uint8_t saved_size = sh1[32];
    sh1[32] = 0xFF; sh1[33] = 0xFF;
    CHECK("reject truncated non-NOBITS section",
          neverc_elf_open(&f, data, len) < 0);
    sh1[32] = saved_size; sh1[33] = 0;

    uint8_t saved_off[16];
    memcpy(saved_off, sh1 + 24, 16);
    put64le(sh1 + 24, UINT64_MAX - 4);
    put64le(sh1 + 32, 16);
    CHECK("reject sh_offset plus size wrap",
          neverc_elf_open(&f, data, len) < 0);
    memcpy(sh1 + 24, saved_off, 16);

    data[60] = 0; data[61] = 0;
    data[shdr_off + 32] = 3;
    data[shdr_off + 4] = NEVERC_SHT_PROGBITS;
    CHECK("reject extended numbering when section 0 is not SHT_NULL",
          neverc_elf_open(&f, data, len) < 0);
    data[shdr_off + 4] = 0;
    data[60] = 3;
    data[shdr_off + 32] = 0;

    free(data);
}

static uint8_t *build_minimal_elf32_be(size_t *out_len) {
    const char shstrtab[] = "\0.text\0.shstrtab\0";
    size_t shstrtab_len = sizeof(shstrtab) - 1;
    size_t ehdr = 52, shdr = 40;
    size_t shstrtab_off = ehdr;
    size_t shdr_off = shstrtab_off + shstrtab_len;
    size_t total = shdr_off + 3 * shdr;
    uint8_t *buf = (uint8_t *)calloc(total, 1);
    if (!buf) return NULL;

    buf[0] = 0x7f; buf[1] = 'E'; buf[2] = 'L'; buf[3] = 'F';
    buf[4] = NEVERC_ELFCLASS32;
    buf[5] = NEVERC_ELFDATA2MSB;
    buf[6] = 1;
    put16be(buf + 16, NEVERC_ET_REL);
    put16be(buf + 18, NEVERC_EM_PPC);
    put32be(buf + 20, 1);
    put32be(buf + 32, (uint32_t)shdr_off);
    put16be(buf + 40, 52);
    put16be(buf + 46, 40);
    put16be(buf + 48, 3);
    put16be(buf + 50, 2);
    memcpy(buf + shstrtab_off, shstrtab, shstrtab_len);

    uint8_t *sh1 = buf + shdr_off + shdr;
    put32be(sh1 + 0, 1);
    put32be(sh1 + 4, NEVERC_SHT_NOBITS);
    put32be(sh1 + 20, 16);

    uint8_t *sh2 = buf + shdr_off + 2 * shdr;
    put32be(sh2 + 0, 7);
    put32be(sh2 + 4, NEVERC_SHT_STRTAB);
    put32be(sh2 + 16, (uint32_t)shstrtab_off);
    put32be(sh2 + 20, (uint32_t)shstrtab_len);

    *out_len = total;
    return buf;
}

static void test_elf32_be_and_entsize(void) {
    size_t len = 0;
    uint8_t *data = build_minimal_elf32_be(&len);
    CHECK("build ELF32 BE fixture", data != NULL);
    if (!data) return;

    neverc_elf_file_t f;
    CHECK("open ELF32 big-endian", neverc_elf_open(&f, data, len) == 0);
    CHECK("ELF32 BE class and encoding",
          f.header.class_ == NEVERC_ELFCLASS32 &&
              f.header.data == NEVERC_ELFDATA2MSB);
    CHECK("ELF32 BE machine", f.header.machine == NEVERC_EM_PPC);
    CHECK("ELF32 BE .text is NOBITS",
          neverc_elf_section(&f, ".text") != NULL &&
              neverc_elf_section(&f, ".text")->type == NEVERC_SHT_NOBITS);
    neverc_elf_close(&f);
    free(data);

    uint8_t buf[49];
    memset(buf, 0, sizeof(buf));
    neverc_elf_section_t sections[2];
    memset(sections, 0, sizeof(sections));
    sections[0].type = NEVERC_SHT_SYMTAB;
    sections[0].link = 1;
    sections[0].size = 48;
    sections[0].entsize = 0;
    sections[1].type = NEVERC_SHT_STRTAB;
    sections[1].offset = 48;
    sections[1].size = 1;

    memset(&f, 0, sizeof(f));
    f.data = buf;
    f.data_len = sizeof(buf);
    f.header.class_ = NEVERC_ELFCLASS64;
    f.header.data = NEVERC_ELFDATA2LSB;
    f.sections = sections;
    f.section_count = 2;

    neverc_elf_symbol_t *symbols = (neverc_elf_symbol_t *)1;
    int count = 99;
    CHECK("zero entsize uses class symbol size",
          neverc_elf_symbols(&f, &symbols, &count) == 0 &&
              count == 1 && symbols != NULL);
    free(symbols);
}

static void test_elf_tiny_malformed(void) {
    neverc_elf_file_t f;

    uint8_t ident16[16] = {
        0x7f, 'E', 'L', 'F', NEVERC_ELFCLASS64, NEVERC_ELFDATA2LSB, 1
    };
    CHECK("reject 16-byte ELF64 ident",
          neverc_elf_open(&f, ident16, sizeof(ident16)) < 0);

    uint8_t elf32_trunc[51] = {0};
    elf32_trunc[0] = 0x7f;
    elf32_trunc[1] = 'E';
    elf32_trunc[2] = 'L';
    elf32_trunc[3] = 'F';
    elf32_trunc[4] = NEVERC_ELFCLASS32;
    elf32_trunc[5] = NEVERC_ELFDATA2LSB;
    elf32_trunc[6] = 1;
    CHECK("reject truncated ELF32 header",
          neverc_elf_open(&f, elf32_trunc, sizeof(elf32_trunc)) < 0);

    uint8_t phentsz[64];
    memset(phentsz, 0, sizeof(phentsz));
    phentsz[0] = 0x7f;
    phentsz[1] = 'E';
    phentsz[2] = 'L';
    phentsz[3] = 'F';
    phentsz[4] = NEVERC_ELFCLASS64;
    phentsz[5] = NEVERC_ELFDATA2LSB;
    phentsz[6] = 1;
    phentsz[20] = 1;
    phentsz[32] = 64;
    phentsz[52] = 64;
    phentsz[56] = 1; /* e_phnum=1, e_phentsize=0 */
    CHECK("reject zero program header entry size",
          neverc_elf_open(&f, phentsz, sizeof(phentsz)) < 0);

    uint8_t trunc_phdr[74];
    memset(trunc_phdr, 0, sizeof(trunc_phdr));
    trunc_phdr[0] = 0x7f;
    trunc_phdr[1] = 'E';
    trunc_phdr[2] = 'L';
    trunc_phdr[3] = 'F';
    trunc_phdr[4] = NEVERC_ELFCLASS64;
    trunc_phdr[5] = NEVERC_ELFDATA2LSB;
    trunc_phdr[6] = 1;
    trunc_phdr[20] = 1;
    trunc_phdr[32] = 64;
    trunc_phdr[52] = 64;
    trunc_phdr[54] = 56;
    trunc_phdr[56] = 1;
    CHECK("reject truncated program header table",
          neverc_elf_open(&f, trunc_phdr, sizeof(trunc_phdr)) < 0);

    uint8_t phdr_wrap[64 + 56];
    memset(phdr_wrap, 0, sizeof(phdr_wrap));
    phdr_wrap[0] = 0x7f;
    phdr_wrap[1] = 'E';
    phdr_wrap[2] = 'L';
    phdr_wrap[3] = 'F';
    phdr_wrap[4] = NEVERC_ELFCLASS64;
    phdr_wrap[5] = NEVERC_ELFDATA2LSB;
    phdr_wrap[6] = 1;
    phdr_wrap[20] = 1;
    phdr_wrap[32] = 64;
    phdr_wrap[52] = 64;
    phdr_wrap[54] = 56;
    phdr_wrap[56] = 1;
    put64le(phdr_wrap + 64 + 8, UINT64_MAX - 4);
    put64le(phdr_wrap + 64 + 32, 16);
    CHECK("reject program header offset plus filesz wrap",
          neverc_elf_open(&f, phdr_wrap, sizeof(phdr_wrap)) < 0);

    uint8_t dynbuf[32];
    memset(dynbuf, 0, sizeof(dynbuf));
    dynbuf[0] = 1;  /* DT_NEEDED */
    dynbuf[8] = 99; /* string offset past dynstr */
    dynbuf[31] = 0;
    neverc_elf_section_t secs[2];
    memset(secs, 0, sizeof(secs));
    secs[0].type = NEVERC_SHT_DYNAMIC;
    secs[0].size = 32;
    secs[0].entsize = 16;
    secs[0].link = 1;
    secs[1].type = NEVERC_SHT_STRTAB;
    secs[1].offset = 31;
    secs[1].size = 1;
    memset(&f, 0, sizeof(f));
    f.data = dynbuf;
    f.data_len = sizeof(dynbuf);
    f.header.class_ = NEVERC_ELFCLASS64;
    f.header.data = NEVERC_ELFDATA2LSB;
    f.sections = secs;
    f.section_count = 2;
    char *names[1] = {(char *)1};
    CHECK("reject DT_NEEDED past string table",
          neverc_elf_imported_libraries(&f, names, 1) < 0);
}

static void test_elf_self_binary(void) {
    /*
     * On macOS we're running a Mach-O binary, not ELF.
     * On Linux this would parse the test binary itself.
     * This test validates the open_file path at least doesn't crash.
     */
    neverc_elf_file_t f;
    int rc = neverc_elf_open_file(&f, "/nonexistent/path");
    CHECK("open_nonexistent_fails", rc < 0);
}

int main(void) {
    printf("=== NeverC debug/elf Tests ===\n\n");

    test_elf64_parse();
    test_elf_invalid();
    test_elf_symbol_count_overflow();
    test_elf_malformed_tables();
    test_elf_extended_and_truncated();
    test_elf32_be_and_entsize();
    test_elf_tiny_malformed();
    test_elf_self_binary();

    printf("\n%d/%d tests passed", tests_passed, tests_run);
    if (tests_failed > 0)
        printf(", %d FAILED", tests_failed);
    printf("\n");
    return tests_failed > 0 ? 1 : 0;
}
