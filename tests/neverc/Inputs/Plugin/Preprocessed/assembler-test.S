#define NEVERC_TEST_BYTE(value) .byte value

.text
.globl neverc_preprocessed_assembler_fixture
.p2align 2
neverc_preprocessed_assembler_fixture:
  NEVERC_TEST_BYTE(0)
