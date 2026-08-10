#ifndef NEVERC_FOUNDATION_ANDROIDKERNELMODULERELOCATIONPOLICY_H
#define NEVERC_FOUNDATION_ANDROIDKERNELMODULERELOCATIONPOLICY_H

#include "llvm/BinaryFormat/ELF.h"

#include <cstdint>
#include <optional>

namespace neverc::AndroidKernelModuleRelocationPolicy {

/// Number of bytes the Linux AArch64 module loader reads and writes at a
/// static RELA relocation site. Unsupported relocation types return nullopt.
///
/// This mirrors the cases in Linux arch/arm64/kernel/module.c,
/// apply_relocate_add(): reloc_data writes 16/32/64-bit data, while every
/// reloc_insn_movw/reloc_insn_imm/reloc_insn_adrp case accesses one 32-bit
/// instruction. R_AARCH64_NONE deliberately has a zero-byte span.
inline std::optional<uint8_t> writeWidth(uint32_t Type) {
  using namespace llvm::ELF;
  switch (Type) {
  case R_AARCH64_NONE:
    return 0;
  case R_AARCH64_ABS16:
  case R_AARCH64_PREL16:
    return 2;
  case R_AARCH64_ABS32:
  case R_AARCH64_PREL32:
    return 4;
  case R_AARCH64_ABS64:
  case R_AARCH64_PREL64:
    return 8;

  case R_AARCH64_MOVW_UABS_G0:
  case R_AARCH64_MOVW_UABS_G0_NC:
  case R_AARCH64_MOVW_UABS_G1:
  case R_AARCH64_MOVW_UABS_G1_NC:
  case R_AARCH64_MOVW_UABS_G2:
  case R_AARCH64_MOVW_UABS_G2_NC:
  case R_AARCH64_MOVW_UABS_G3:
  case R_AARCH64_MOVW_SABS_G0:
  case R_AARCH64_MOVW_SABS_G1:
  case R_AARCH64_MOVW_SABS_G2:
  case R_AARCH64_MOVW_PREL_G0:
  case R_AARCH64_MOVW_PREL_G0_NC:
  case R_AARCH64_MOVW_PREL_G1:
  case R_AARCH64_MOVW_PREL_G1_NC:
  case R_AARCH64_MOVW_PREL_G2:
  case R_AARCH64_MOVW_PREL_G2_NC:
  case R_AARCH64_MOVW_PREL_G3:
  case R_AARCH64_LD_PREL_LO19:
  case R_AARCH64_ADR_PREL_LO21:
  case R_AARCH64_ADR_PREL_PG_HI21:
  case R_AARCH64_ADR_PREL_PG_HI21_NC:
  case R_AARCH64_ADD_ABS_LO12_NC:
  case R_AARCH64_LDST8_ABS_LO12_NC:
  case R_AARCH64_LDST16_ABS_LO12_NC:
  case R_AARCH64_LDST32_ABS_LO12_NC:
  case R_AARCH64_LDST64_ABS_LO12_NC:
  case R_AARCH64_LDST128_ABS_LO12_NC:
  case R_AARCH64_TSTBR14:
  case R_AARCH64_CONDBR19:
  case R_AARCH64_JUMP26:
  case R_AARCH64_CALL26:
    return 4;
  default:
    return std::nullopt;
  }
}

} // namespace neverc::AndroidKernelModuleRelocationPolicy

#endif // NEVERC_FOUNDATION_ANDROIDKERNELMODULERELOCATIONPOLICY_H
