#include "Linker/ELF/DWARF.h"
#include "Linker/Core/Runtime/Allocator.h"
#include "Linker/ELF/InputSection.h"
#include "Linker/ELF/Symbols.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugPubTable.h"
#include "llvm/Object/ELFObjectFile.h"

using namespace llvm;
using namespace llvm::object;
using namespace linker;
using namespace linker::elf;

template <class ELFT> LinkerDwarfObj<ELFT>::LinkerDwarfObj(ObjFile<ELFT> *obj) {
  ArrayRef<typename ELFT::Shdr> objSections = obj->template getELFShdrs<ELFT>();
  assert(objSections.size() == obj->getSections().size());
  for (auto [i, sec] : llvm::enumerate(obj->getSections())) {
    if (!sec)
      continue;

    if (DwarfInputSection *m =
            StringSwitch<DwarfInputSection *>(sec->name)
                .Case(".debug_addr", &addrSection)
                .Case(".debug_gnu_pubnames", &gnuPubnamesSection)
                .Case(".debug_gnu_pubtypes", &gnuPubtypesSection)
                .Case(".debug_loclists", &loclistsSection)
                .Case(".debug_ranges", &rangesSection)
                .Case(".debug_rnglists", &rnglistsSection)
                .Case(".debug_str_offsets", &strOffsetsSection)
                .Case(".debug_line", &lineSection)
                .Default(nullptr)) {
      m->Data = toStringRef(sec->contentMaybeDecompress());
      m->sec = sec;
      continue;
    }

    if (sec->name == ".debug_abbrev")
      abbrevSection = toStringRef(sec->contentMaybeDecompress());
    else if (sec->name == ".debug_str")
      strSection = toStringRef(sec->contentMaybeDecompress());
    else if (sec->name == ".debug_line_str")
      lineStrSection = toStringRef(sec->contentMaybeDecompress());
    else if (sec->name == ".debug_info" &&
             !(objSections[i].sh_flags & ELF::SHF_GROUP)) {
      // DWARF v5 type units under COMDAT live in .debug_info too; skip them
      // for .gdb_index/diagnostics by filtering out SHF_GROUP sections.
      infoSection.Data = toStringRef(sec->contentMaybeDecompress());
      infoSection.sec = sec;
    }
  }
}

namespace {
template <class RelTy> struct DwarfRelocResolver {
  // S is the symbol value; addend is supplied by findAux.  For RELA the
  // addend comes from the relocation entry; for REL the specialisation
  // below pulls it from the relocated location instead.
  static uint64_t resolve(uint64_t /*type*/, uint64_t /*offset*/, uint64_t s,
                          uint64_t /*locData*/, int64_t addend) {
    return s + addend;
  }
};

template <class ELFT> struct DwarfRelocResolver<Elf_Rel_Impl<ELFT, false>> {
  static uint64_t resolve(uint64_t /*type*/, uint64_t /*offset*/, uint64_t s,
                          uint64_t locData, int64_t /*addend*/) {
    return s + locData;
  }
};
} // namespace

template <class ELFT>
template <class RelTy>
std::optional<RelocAddrEntry>
LinkerDwarfObj<ELFT>::findAux(const InputSectionBase &sec, uint64_t pos,
                              ArrayRef<RelTy> rels) const {
  auto it =
      partition_point(rels, [=](const RelTy &a) { return a.r_offset < pos; });
  if (it == rels.end() || it->r_offset != pos)
    return std::nullopt;
  const RelTy &rel = *it;

  const ObjFile<ELFT> *file = sec.getFile<ELFT>();
  uint32_t symIndex = rel.getSymbol();
  const typename ELFT::Sym &sym = file->template getELFSyms<ELFT>()[symIndex];
  uint32_t secIndex = file->getSectionIndex(sym);

  // Resolve symbols even if their defining section was discarded — needed
  // so .debug_ranges end-of-range entries don't terminate decoding early.
  Symbol &s = file->getRelocTargetSym(rel);
  uint64_t val = 0;
  if (auto *dr = dyn_cast<Defined>(&s))
    val = dr->value;

  DataRefImpl d;
  d.p = getAddend<ELFT>(rel);
  return RelocAddrEntry{secIndex, RelocationRef(d, nullptr),
                        val,      std::optional<object::RelocationRef>(),
                        0,        DwarfRelocResolver<RelTy>::resolve};
}

template <class ELFT>
std::optional<RelocAddrEntry>
LinkerDwarfObj<ELFT>::find(const llvm::DWARFSection &s, uint64_t pos) const {
  auto &sec = static_cast<const DwarfInputSection &>(s);
  const RelsOrRelas<ELFT> rels = sec.sec->template relsOrRelas<ELFT>();
  if (rels.areRelocsRel())
    return findAux(*sec.sec, pos, rels.rels);
  return findAux(*sec.sec, pos, rels.relas);
}

template class elf::LinkerDwarfObj<ELF64LE>;
template class elf::LinkerDwarfObj<ELF64BE>;
