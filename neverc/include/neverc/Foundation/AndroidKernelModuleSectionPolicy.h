#ifndef NEVERC_FOUNDATION_ANDROIDKERNELMODULESECTIONPOLICY_H
#define NEVERC_FOUNDATION_ANDROIDKERNELMODULESECTIONPOLICY_H

#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/ELF.h"

#include <cstdint>

namespace neverc::AndroidKernelModuleSectionPolicy {

/// Section-index metadata whose semantics the canonical Android release
/// artifact cannot represent.  These inputs must be rejected rather than
/// copied or silently discarded.
inline constexpr bool rejectsReleaseInputType(uint32_t Type) {
  using namespace llvm::ELF;
  return Type == SHT_GROUP || Type == SHT_SYMTAB_SHNDX;
}

/// Index-independent metadata consumed or replaced while producing one
/// canonical release object. Non-leading SHT_NULL entries are inactive slots
/// and are dropped; the selected symbol table, relocation tables, and LLVM
/// index metadata are rebuilt or discarded as appropriate.
///
/// SHT_STRTAB is deliberately absent: a string table is metadata only when its
/// section index is exactly e_shstrndx or the selected SHT_SYMTAB's sh_link.
/// The canonical release writer cannot represent any additional string table,
/// so callers must identify those two indices first and reject every other
/// SHT_STRTAB instead of silently treating it as regenerated metadata.
inline constexpr bool regeneratesReleaseInputType(uint32_t Type) {
  using namespace llvm::ELF;
  return Type == SHT_NULL || Type == SHT_SYMTAB || Type == SHT_RELA ||
         Type == SHT_REL || Type == SHT_LLVM_ADDRSIG ||
         Type == SHT_LLVM_CALL_GRAPH_PROFILE;
}

/// Metadata kinds the release producer itself may serialize.  Their exact
/// cardinality, placement, and header shape are audited by the verifier.
inline constexpr bool isCanonicalReleaseOutputMetadataType(uint32_t Type) {
  using namespace llvm::ELF;
  return Type == SHT_NULL || Type == SHT_SYMTAB || Type == SHT_STRTAB ||
         Type == SHT_RELA;
}

/// Types that cannot occur at a given index in producer-canonical release
/// output.  SHT_NULL is valid only for the mandatory leading section.
inline constexpr bool rejectsReleaseOutputTypeAtIndex(uint32_t Type,
                                                      bool IsSectionZero) {
  using namespace llvm::ELF;
  if (IsSectionZero)
    return Type != SHT_NULL;
  return Type == SHT_NULL || Type == SHT_GROUP || Type == SHT_SYMTAB_SHNDX ||
         Type == SHT_REL || Type == SHT_LLVM_ADDRSIG ||
         Type == SHT_LLVM_CALL_GRAPH_PROFILE;
}

/// The fixed metadata suffix emitted for a finalized Android kernel module.
/// Content sections precede this suffix; its only variable-length run is one
/// SHT_RELA section per relocation-bearing content section.
enum class CanonicalMetadataKind : uint8_t {
  Null,
  Symtab,
  Strtab,
  Rela,
  Shstrtab,
};

struct CanonicalMetadataHeaderShape {
  uint32_t Type;
  uint64_t Flags;
  uint64_t Alignment;
  uint64_t Entsize;
};

inline constexpr CanonicalMetadataHeaderShape
canonicalMetadataHeaderShape(CanonicalMetadataKind Kind) {
  using namespace llvm::ELF;
  switch (Kind) {
  case CanonicalMetadataKind::Null:
    return {SHT_NULL, 0, 0, 0};
  case CanonicalMetadataKind::Symtab:
    return {SHT_SYMTAB, 0, 8, sizeof(Elf64_Sym)};
  case CanonicalMetadataKind::Strtab:
  case CanonicalMetadataKind::Shstrtab:
    return {SHT_STRTAB, 0, 1, 0};
  case CanonicalMetadataKind::Rela:
    return {SHT_RELA, 0, 8, sizeof(Elf64_Rela)};
  }
  return {SHT_NULL, 0, 0, 0};
}

/// Initialize a producer header from the same shape table consumed by the
/// independent verifier.  Link and Info are semantic indices supplied by the
/// caller; every other metadata header field is canonical and zero-initialized.
template <typename SectionHeaderT>
inline void initializeCanonicalMetadataHeader(SectionHeaderT &Header,
                                              CanonicalMetadataKind Kind,
                                              uint32_t NameOffset = 0,
                                              uint32_t Link = 0,
                                              uint32_t Info = 0) {
  Header = SectionHeaderT{};
  const CanonicalMetadataHeaderShape Shape = canonicalMetadataHeaderShape(Kind);
  Header.sh_name = NameOffset;
  Header.sh_type = Shape.Type;
  Header.sh_flags = Shape.Flags;
  Header.sh_addralign = Shape.Alignment;
  Header.sh_entsize = Shape.Entsize;
  Header.sh_link = Link;
  Header.sh_info = Info;
}

/// A format-neutral view used to audit exact metadata shape without sharing
/// either the producer's section representation or the verifier's raw parser.
struct CanonicalSectionShapeView {
  llvm::StringRef Name;
  uint32_t NameOffset = 0;
  uint32_t Type = 0;
  uint64_t Flags = 0;
  uint64_t Size = 0;
  uint64_t Offset = 0;
  uint64_t Address = 0;
  uint64_t Alignment = 0;
  uint64_t Entsize = 0;
  uint32_t Link = 0;
  uint32_t Info = 0;
  bool Compressed = false;
};

inline bool matchesCanonicalMetadataShape(
    CanonicalMetadataKind Kind, const CanonicalSectionShapeView &Section,
    uint32_t ExpectedLink = 0, uint32_t ExpectedInfo = 0,
    llvm::StringRef RelocationTargetName = {}) {
  const CanonicalMetadataHeaderShape Expected =
      canonicalMetadataHeaderShape(Kind);
  if (Section.Type != Expected.Type || Section.Flags != Expected.Flags ||
      Section.Address != 0 || Section.Alignment != Expected.Alignment ||
      Section.Entsize != Expected.Entsize || Section.Link != ExpectedLink ||
      Section.Info != ExpectedInfo || Section.Compressed)
    return false;

  switch (Kind) {
  case CanonicalMetadataKind::Null:
    return Section.Name.empty() && Section.NameOffset == 0 &&
           Section.Size == 0 && Section.Offset == 0 && ExpectedLink == 0 &&
           ExpectedInfo == 0;
  case CanonicalMetadataKind::Symtab:
    return Section.Name == ".symtab" && Section.Size != 0 &&
           Section.Size % sizeof(llvm::ELF::Elf64_Sym) == 0;
  case CanonicalMetadataKind::Strtab:
    return Section.Name == ".strtab" && Section.Size != 0;
  case CanonicalMetadataKind::Rela:
    return Section.Name.starts_with(".rela") &&
           Section.Name.drop_front(sizeof(".rela") - 1) ==
               RelocationTargetName &&
           Section.Size != 0 &&
           Section.Size % sizeof(llvm::ELF::Elf64_Rela) == 0;
  case CanonicalMetadataKind::Shstrtab:
    return Section.Name == ".shstrtab" && Section.Size != 0;
  }
  return false;
}

} // namespace neverc::AndroidKernelModuleSectionPolicy

#endif // NEVERC_FOUNDATION_ANDROIDKERNELMODULESECTIONPOLICY_H
