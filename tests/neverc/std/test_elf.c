/*
 * ELF parser test suite.
 * Tests with synthesized minimal ELF64 (little-endian, x86-64) binary data.
 */
#include "neverc/std/debug/elf.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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
    test_elf_self_binary();

    printf("\n%d/%d tests passed", tests_passed, tests_run);
    if (tests_failed > 0)
        printf(", %d FAILED", tests_failed);
    printf("\n");
    return tests_failed > 0 ? 1 : 0;
}
