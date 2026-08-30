//===- ExtendedSectionNumbering.h - ELF section-count encoding -*- C++ -*-===//

#ifndef NEVERC_MERGE_ELF_EXTENDEDSECTIONNUMBERING_H
#define NEVERC_MERGE_ELF_EXTENDEDSECTIONNUMBERING_H

#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Object/ELFTypes.h"

#include <cstdint>
#include <limits>

namespace neverc::merge::detail {

/// Encode the gABI section-count and section-name-table escapes. ELFT's packed
/// fields perform the target-endian conversion, so the same operation is valid
/// for ELF32/ELF64 and little/big-endian objects.
template <typename ELFT>
bool encodeELFSectionHeaderNumbers(typename ELFT::Ehdr &Header,
                                   typename ELFT::Shdr &NullSection,
                                   uint64_t SectionCount,
                                   uint64_t SectionStringTableIndex) {
  if (SectionCount == 0 || SectionStringTableIndex >= SectionCount ||
      SectionStringTableIndex > std::numeric_limits<uint32_t>::max() ||
      (!ELFT::Is64Bits && SectionCount > std::numeric_limits<uint32_t>::max()))
    return false;

  const bool ExtendedCount = SectionCount >= llvm::ELF::SHN_LORESERVE;
  Header.e_shnum = ExtendedCount ? 0 : static_cast<uint16_t>(SectionCount);
  NullSection.sh_size = ExtendedCount ? SectionCount : 0;

  const bool ExtendedNames =
      SectionStringTableIndex >= llvm::ELF::SHN_LORESERVE;
  Header.e_shstrndx = ExtendedNames
                          ? llvm::ELF::SHN_XINDEX
                          : static_cast<uint16_t>(SectionStringTableIndex);
  NullSection.sh_link =
      ExtendedNames ? static_cast<uint32_t>(SectionStringTableIndex) : 0;
  return true;
}

} // namespace neverc::merge::detail

#endif // NEVERC_MERGE_ELF_EXTENDEDSECTIONNUMBERING_H
