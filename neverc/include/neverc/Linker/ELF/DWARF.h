#ifndef LINKER_ELF_DWARF_H
#define LINKER_ELF_DWARF_H

#include "Linker/ELF/InputFiles.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/Object/ELF.h"
#include <optional>

namespace linker::elf {

class InputSection;

struct DwarfInputSection final : public llvm::DWARFSection {
  InputSectionBase *sec = nullptr;
};

template <class ELFT> class LinkerDwarfObj final : public llvm::DWARFObject {
public:
  explicit LinkerDwarfObj(ObjFile<ELFT> *obj);

  void forEachInfoSections(
      llvm::function_ref<void(const llvm::DWARFSection &)> f) const override {
    f(infoSection);
  }

  InputSection *getInfoSection() const {
    return cast<InputSection>(infoSection.sec);
  }

  const llvm::DWARFSection &getLoclistsSection() const override {
    return loclistsSection;
  }

  const llvm::DWARFSection &getRangesSection() const override {
    return rangesSection;
  }

  const llvm::DWARFSection &getRnglistsSection() const override {
    return rnglistsSection;
  }

  const llvm::DWARFSection &getStrOffsetsSection() const override {
    return strOffsetsSection;
  }

  const llvm::DWARFSection &getLineSection() const override {
    return lineSection;
  }

  const llvm::DWARFSection &getAddrSection() const override {
    return addrSection;
  }

  const DwarfInputSection &getGnuPubnamesSection() const override {
    return gnuPubnamesSection;
  }

  const DwarfInputSection &getGnuPubtypesSection() const override {
    return gnuPubtypesSection;
  }

  StringRef getFileName() const override { return ""; }
  StringRef getAbbrevSection() const override { return abbrevSection; }
  StringRef getStrSection() const override { return strSection; }
  StringRef getLineStrSection() const override { return lineStrSection; }

  bool isLittleEndian() const override {
    return ELFT::TargetEndianness == llvm::endianness::little;
  }

  std::optional<llvm::RelocAddrEntry> find(const llvm::DWARFSection &sec,
                                           uint64_t pos) const override;

private:
  template <class RelTy>
  std::optional<llvm::RelocAddrEntry> findAux(const InputSectionBase &sec,
                                              uint64_t pos,
                                              ArrayRef<RelTy> rels) const;

  DwarfInputSection gnuPubnamesSection;
  DwarfInputSection gnuPubtypesSection;
  DwarfInputSection infoSection;
  DwarfInputSection loclistsSection;
  DwarfInputSection rangesSection;
  DwarfInputSection rnglistsSection;
  DwarfInputSection strOffsetsSection;
  DwarfInputSection lineSection;
  DwarfInputSection addrSection;
  StringRef abbrevSection;
  StringRef strSection;
  StringRef lineStrSection;
};

} // namespace linker::elf

#endif
