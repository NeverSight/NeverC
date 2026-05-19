#include "Linker/ELF/SyntheticSections.h"
#include "Linker/Core/Runtime/Session.h"
#include "Linker/Core/Support/Dwarf.h"
#include "Linker/Core/Support/Strings.h"
#include "Linker/ELF/Config.h"
#include "Linker/ELF/DWARF.h"
#include "Linker/ELF/EhFrame.h"
#include "Linker/ELF/Emit.h"
#include "Linker/ELF/InputFiles.h"
#include "Linker/ELF/LinkerScript.h"
#include "Linker/ELF/OutputSections.h"
#include "Linker/ELF/SymbolTable.h"
#include "Linker/ELF/Symbols.h"
#include "Linker/ELF/Target.h"
#include "Linker/ELF/Thunks.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetOperations.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugPubTable.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/Parallel.h"
#include "llvm/Support/TimeProfiler.h"
#include <cstdlib>

using namespace llvm;
using namespace llvm::dwarf;
using namespace llvm::ELF;
using namespace llvm::object;
using namespace llvm::support;
using namespace linker;
using namespace linker::elf;

using llvm::support::endian::read32le;
using llvm::support::endian::write32le;
using llvm::support::endian::write64le;

constexpr size_t MergeNoTailSection::numShards;

namespace {
uint64_t readUint(uint8_t *buf) { return read64(buf); }
void writeUint(uint8_t *buf, uint64_t val) { write64(buf, val); }
} // namespace

// ===----------------------------------------------------------------------===
// Helper functions
// ===----------------------------------------------------------------------===

InputSection *elf::createInterpSection() {
  // StringSaver guarantees that the returned string ends with '\0'.
  StringRef s = saver().save(config->dynamicLinker);
  ArrayRef<uint8_t> contents = {(const uint8_t *)s.data(), s.size() + 1};

  return make<InputSection>(nullptr, SHF_ALLOC, SHT_PROGBITS, 1, contents,
                            ".interp");
}

Defined *elf::addSyntheticLocal(StringRef name, uint8_t type, uint64_t value,
                                uint64_t size, InputSectionBase &section) {
  Defined *s = makeDefined(section.file, name, STB_LOCAL, STV_DEFAULT, type,
                           value, size, &section);
  if (in.symTab)
    in.symTab->addSymbol(s);

  return s;
}

namespace {
size_t getHashSize() {
  switch (config->buildId) {
  case BuildIdStyle::Fast:
    return 8;
  case BuildIdStyle::Md5:
  case BuildIdStyle::Uuid:
    return 16;
  case BuildIdStyle::Sha1:
    return 20;
  case BuildIdStyle::Hexstring:
    return config->buildIdVector.size();
  default:
    llvm_unreachable("unknown BuildIdStyle");
  }
}
} // namespace

// This class represents a linker-synthesized .note.gnu.property section.
//
// In x86 and AArch64, object files may contain feature flags indicating the
// features that they have used. The flags are stored in a .note.gnu.property
// section.
//
// The linker reads the sections from input files and merges them by computing
// AND of the flags. The result is written as a new .note.gnu.property section.
//
// If the flag is zero (which indicates that the intersection of the feature
// sets is empty, or some input files didn't have .note.gnu.property sections),
// we don't create this section.
GnuPropertySection::GnuPropertySection()
    : SyntheticSection(llvm::ELF::SHF_ALLOC, llvm::ELF::SHT_NOTE,
                       config->wordsize, ".note.gnu.property") {}

void GnuPropertySection::writeTo(uint8_t *buf) {
  uint32_t featureAndType = config->emachine == EM_AARCH64
                                ? GNU_PROPERTY_AARCH64_FEATURE_1_AND
                                : GNU_PROPERTY_X86_FEATURE_1_AND;

  write32(buf, 4);                          // Name size
  write32(buf + 4, 16);                     // Content size
  write32(buf + 8, NT_GNU_PROPERTY_TYPE_0); // Type
  memcpy(buf + 12, "GNU", 4);               // Name string
  write32(buf + 16, featureAndType);        // Feature type
  write32(buf + 20, 4);                     // Feature size
  write32(buf + 24, config->andFeatures);   // Feature flags
  write32(buf + 28, 0);                     // Padding
}

size_t GnuPropertySection::getSize() const { return 32; }

FormIdSection::FormIdSection()
    : SyntheticSection(SHF_ALLOC, SHT_NOTE, 4, ".note.gnu.build-id"),
      hashSize(getHashSize()) {}

void FormIdSection::writeTo(uint8_t *buf) {
  write32(buf, 4);                   // Name size
  write32(buf + 4, hashSize);        // Content size
  write32(buf + 8, NT_GNU_BUILD_ID); // Type
  memcpy(buf + 12, "GNU", 4);        // Name string
  hashBuf = buf + 16;
}

void FormIdSection::writeBuildId(ArrayRef<uint8_t> buf) {
  assert(buf.size() == hashSize);
  memcpy(hashBuf, buf.data(), hashSize);
}

BssSection::BssSection(StringRef name, uint64_t size, uint32_t alignment)
    : SyntheticSection(SHF_ALLOC | SHF_WRITE, SHT_NOBITS, alignment, name) {
  this->bss = true;
  this->size = size;
}

// ===----------------------------------------------------------------------===
// EhFrameSection
// ===----------------------------------------------------------------------===

EhFrameSection::EhFrameSection()
    : SyntheticSection(SHF_ALLOC, SHT_PROGBITS, 1, ".eh_frame") {}

// Search for an existing CIE record or create a new one.
// CIE records from input object files are uniquified by their contents
// and where their relocations point to.
template <class ELFT, class RelTy>
CieRecord *EhFrameSection::addCie(EhSectionPiece &cie, ArrayRef<RelTy> rels) {
  Symbol *personality = nullptr;
  unsigned firstRelI = cie.firstRelocation;
  if (firstRelI != (unsigned)-1)
    personality =
        &cie.sec->template getFile<ELFT>()->getRelocTargetSym(rels[firstRelI]);

  // Search for an existing CIE by CIE contents/relocation target pair.
  CieRecord *&rec = cieMap[{cie.data(), personality}];

  // If not found, create a new one.
  if (!rec) {
    rec = make<CieRecord>();
    rec->cie = &cie;
    cieRecords.push_back(rec);
  }
  return rec;
}

// There is one FDE per function. Returns a non-null pointer to the function
// symbol if the given FDE points to a live function.
template <class ELFT, class RelTy>
Defined *EhFrameSection::isFdeLive(EhSectionPiece &fde, ArrayRef<RelTy> rels) {
  auto *sec = cast<EhInputSection>(fde.sec);
  unsigned firstRelI = fde.firstRelocation;

  // FDEs should always reference a function, but ill-formed inputs can
  // strand them with no first relocation; drop those entries so they
  // don't pollute the merged .eh_frame.
  if (firstRelI == (unsigned)-1)
    return nullptr;

  const RelTy &rel = rels[firstRelI];
  Symbol &b = sec->template getFile<ELFT>()->getRelocTargetSym(rel);

  // FDEs for garbage-collected or merged-by-ICF sections, or sections in
  // another partition, are dead.
  if (auto *d = dyn_cast<Defined>(&b))
    if (!d->folded && d->section && d->section->partition == partition)
      return d;
  return nullptr;
}

// .eh_frame is a sequence of CIE or FDE records. In general, there
// is one CIE record per input object file which is followed by
// a list of FDEs. This function searches an existing CIE or create a new
// one and associates FDEs to the CIE.
template <class ELFT, class RelTy>
void EhFrameSection::addRecords(EhInputSection *sec, ArrayRef<RelTy> rels) {
  offsetToCie.clear();
  for (EhSectionPiece &cie : sec->cies)
    offsetToCie[cie.inputOff] = addCie<ELFT>(cie, rels);
  for (EhSectionPiece &fde : sec->fdes) {
    uint32_t id = endian::read32<ELFT::TargetEndianness>(fde.data().data() + 4);
    CieRecord *rec = offsetToCie[fde.inputOff + 4 - id];
    if (!rec)
      fatal(toString(sec) + ": invalid CIE reference");

    if (!isFdeLive<ELFT>(fde, rels))
      continue;
    rec->fdes.push_back(&fde);
    numFdes++;
  }
}

template <class ELFT> void EhFrameSection::addSectionAux(EhInputSection *sec) {
  if (!sec->isLive())
    return;
  const RelsOrRelas<ELFT> rels = sec->template relsOrRelas<ELFT>();
  if (rels.areRelocsRel())
    addRecords<ELFT>(sec, rels.rels);
  else
    addRecords<ELFT>(sec, rels.relas);
}

// Used by ICF<ELFT>::handleLSDA(). This function is very similar to
// EhFrameSection::addRecords().
template <class ELFT, class RelTy>
void EhFrameSection::iterateFDEWithLSDAAux(
    EhInputSection &sec, ArrayRef<RelTy> rels, DenseSet<size_t> &ciesWithLSDA,
    llvm::function_ref<void(InputSection &)> fn) {
  for (EhSectionPiece &cie : sec.cies)
    if (hasLSDA(cie))
      ciesWithLSDA.insert(cie.inputOff);
  for (EhSectionPiece &fde : sec.fdes) {
    uint32_t id = endian::read32<ELFT::TargetEndianness>(fde.data().data() + 4);
    if (!ciesWithLSDA.contains(fde.inputOff + 4 - id))
      continue;

    // The CIE has a LSDA argument. Call fn with d's section.
    if (Defined *d = isFdeLive<ELFT>(fde, rels))
      if (auto *s = dyn_cast_or_null<InputSection>(d->section))
        fn(*s);
  }
}

template <class ELFT>
void EhFrameSection::iterateFDEWithLSDA(
    llvm::function_ref<void(InputSection &)> fn) {
  DenseSet<size_t> ciesWithLSDA;
  for (EhInputSection *sec : sections) {
    ciesWithLSDA.clear();
    const RelsOrRelas<ELFT> rels = sec->template relsOrRelas<ELFT>();
    if (rels.areRelocsRel())
      iterateFDEWithLSDAAux<ELFT>(*sec, rels.rels, ciesWithLSDA, fn);
    else
      iterateFDEWithLSDAAux<ELFT>(*sec, rels.relas, ciesWithLSDA, fn);
  }
}

namespace {
void writeCieFde(uint8_t *buf, ArrayRef<uint8_t> d) {
  memcpy(buf, d.data(), d.size());
  // Fix the size field. -4 since size does not include the size field itself.
  write32(buf, d.size() - 4);
}
} // namespace

void EhFrameSection::finalizeContents() {
  assert(!this->size); // Not finalized.

  switch (config->ekind) {
  case ELFNoneKind:
    llvm_unreachable("invalid ekind");
  case ELF64LEKind:
    for (EhInputSection *sec : sections)
      addSectionAux<ELF64LE>(sec);
    break;
  case ELF64BEKind:
    for (EhInputSection *sec : sections)
      addSectionAux<ELF64BE>(sec);
    break;
  }

  size_t off = 0;
  for (CieRecord *rec : cieRecords) {
    rec->cie->outputOff = off;
    off += rec->cie->size;

    for (EhSectionPiece *fde : rec->fdes) {
      fde->outputOff = off;
      off += fde->size;
    }
  }

  // The LSB standard does not allow a .eh_frame section with zero
  // Call Frame Information records. glibc unwind-dw2-fde.c
  // classify_object_over_fdes expects there is a CIE record length 0 as a
  // terminator. Thus we add one unconditionally.
  off += 4;

  this->size = off;
}

// Returns data for .eh_frame_hdr. .eh_frame_hdr is a binary search table
// to get an FDE from an address to which FDE is applied. This function
// returns a list of such pairs.
SmallVector<EhFrameSection::FdeData, 0> EhFrameSection::getFdeData() const {
  uint8_t *buf = Out::bufferStart + getParent()->offset + outSecOff;
  SmallVector<FdeData, 0> ret;

  uint64_t va = getPartition().ehFrameHdr->getVA();
  for (CieRecord *rec : cieRecords) {
    uint8_t enc = getFdeEncoding(rec->cie);
    for (EhSectionPiece *fde : rec->fdes) {
      uint64_t pc = getFdePc(buf, fde->outputOff, enc);
      uint64_t fdeVA = getParent()->addr + fde->outputOff;
      if (!isInt<32>(pc - va))
        fatal(toString(fde->sec) + ": PC offset is too large: 0x" +
              Twine::utohexstr(pc - va));
      ret.push_back({uint32_t(pc - va), uint32_t(fdeVA - va)});
    }
  }

  // Sort the FDE list by their PC and uniqueify. Usually there is only
  // one FDE for a PC (i.e. function), but if ICF merges two functions
  // into one, there can be more than one FDEs pointing to the address.
  auto less = [](const FdeData &a, const FdeData &b) {
    return a.pcRel < b.pcRel;
  };
  llvm::stable_sort(ret, less);
  auto eq = [](const FdeData &a, const FdeData &b) {
    return a.pcRel == b.pcRel;
  };
  ret.erase(std::unique(ret.begin(), ret.end(), eq), ret.end());

  return ret;
}

namespace {
uint64_t readFdeAddr(uint8_t *buf, int size) {
  switch (size) {
  case DW_EH_PE_udata2:
    return read16(buf);
  case DW_EH_PE_sdata2:
    return (int16_t)read16(buf);
  case DW_EH_PE_udata4:
    return read32(buf);
  case DW_EH_PE_sdata4:
    return (int32_t)read32(buf);
  case DW_EH_PE_udata8:
  case DW_EH_PE_sdata8:
    return read64(buf);
  case DW_EH_PE_absptr:
    return readUint(buf);
  }
  fatal("unknown FDE size encoding");
}
} // namespace

// Returns the VA to which a given FDE (on a mmap'ed buffer) is applied to.
// We need it to create .eh_frame_hdr section.
uint64_t EhFrameSection::getFdePc(uint8_t *buf, size_t fdeOff,
                                  uint8_t enc) const {
  // The starting address to which this FDE applies is
  // stored at FDE + 8 byte. And this offset is within
  // the .eh_frame section.
  size_t off = fdeOff + 8;
  uint64_t addr = readFdeAddr(buf + off, enc & 0xf);
  if ((enc & 0x70) == DW_EH_PE_absptr)
    return addr;
  if ((enc & 0x70) == DW_EH_PE_pcrel)
    return addr + getParent()->addr + off + outSecOff;
  fatal("unknown FDE size relative encoding");
}

void EhFrameSection::writeTo(uint8_t *buf) {
  // Write CIE and FDE records.
  for (CieRecord *rec : cieRecords) {
    size_t cieOffset = rec->cie->outputOff;
    writeCieFde(buf + cieOffset, rec->cie->data());

    for (EhSectionPiece *fde : rec->fdes) {
      size_t off = fde->outputOff;
      writeCieFde(buf + off, fde->data());

      // FDE's second word should have the offset to an associated CIE.
      // Write it.
      write32(buf + off + 4, off + 4 - cieOffset);
    }
  }

  // Apply relocations. .eh_frame section contents are not contiguous
  // in the output buffer, but relocateAlloc() still works because
  // getOffset() takes care of discontiguous section pieces.
  for (EhInputSection *s : sections)
    target->relocateAlloc(*s, buf);

  if (getPartition().ehFrameHdr && getPartition().ehFrameHdr->getParent())
    getPartition().ehFrameHdr->write();
}

// ===----------------------------------------------------------------------===
// GotSection / GotPltSection / IgotPltSection
// ===----------------------------------------------------------------------===

GotSection::GotSection()
    : SyntheticSection(SHF_ALLOC | SHF_WRITE, SHT_PROGBITS,
                       target->gotEntrySize, ".got") {
  numEntries = target->gotHeaderEntriesNum;
}

void GotSection::addConstant(const Relocation &r) { relocations.push_back(r); }
void GotSection::addEntry(Symbol &sym) {
  assert(sym.auxIdx == symAux.size() - 1);
  symAux.back().gotIdx = numEntries++;
}

bool GotSection::addTlsDescEntry(Symbol &sym) {
  assert(sym.auxIdx == symAux.size() - 1);
  symAux.back().tlsDescIdx = numEntries;
  numEntries += 2;
  return true;
}

bool GotSection::addDynTlsEntry(Symbol &sym) {
  assert(sym.auxIdx == symAux.size() - 1);
  symAux.back().tlsGdIdx = numEntries;
  // Global Dynamic TLS entries take two GOT slots.
  numEntries += 2;
  return true;
}

// Reserves TLS entries for a TLS module ID and a TLS block offset.
// In total it takes two GOT slots.
bool GotSection::addTlsIndex() {
  if (tlsIndexOff != uint32_t(-1))
    return false;
  tlsIndexOff = numEntries * config->wordsize;
  numEntries += 2;
  return true;
}

uint32_t GotSection::getTlsDescOffset(const Symbol &sym) const {
  return sym.getTlsDescIdx() * config->wordsize;
}

uint64_t GotSection::getTlsDescAddr(const Symbol &sym) const {
  return getVA() + getTlsDescOffset(sym);
}

uint64_t GotSection::getGlobalDynAddr(const Symbol &b) const {
  return this->getVA() + b.getTlsGdIdx() * config->wordsize;
}

uint64_t GotSection::getGlobalDynOffset(const Symbol &b) const {
  return b.getTlsGdIdx() * config->wordsize;
}

void GotSection::finalizeContents() { size = numEntries * config->wordsize; }

bool GotSection::isNeeded() const {
  // Needed if the GOT symbol is used or the number of entries is more than just
  // the header. A GOT with just the header may not be needed.
  return hasGotOffRel || numEntries > target->gotHeaderEntriesNum;
}

void GotSection::writeTo(uint8_t *buf) {
  if (size == 0)
    return;
  target->writeGotHeader(buf);
  target->relocateAlloc(*this, buf);
}

GotPltSection::GotPltSection()
    : SyntheticSection(SHF_ALLOC | SHF_WRITE, SHT_PROGBITS, config->wordsize,
                       ".got.plt") {}

void GotPltSection::addEntry(Symbol &sym) {
  assert(sym.auxIdx == symAux.size() - 1 &&
         symAux.back().pltIdx == entries.size());
  entries.push_back(&sym);
}

size_t GotPltSection::getSize() const {
  return (target->gotPltHeaderEntriesNum + entries.size()) *
         target->gotEntrySize;
}

void GotPltSection::writeTo(uint8_t *buf) {
  target->writeGotPltHeader(buf);
  buf += target->gotPltHeaderEntriesNum * target->gotEntrySize;
  for (const Symbol *b : entries) {
    target->writeGotPlt(buf, *b);
    buf += target->gotEntrySize;
  }
}

bool GotPltSection::isNeeded() const {
  // We need to emit GOTPLT even if it's empty if there's a relocation relative
  // to it.
  return !entries.empty() || hasGotPltOffRel;
}

IgotPltSection::IgotPltSection()
    : SyntheticSection(SHF_ALLOC | SHF_WRITE, SHT_PROGBITS,
                       target->gotEntrySize, ".got.plt") {}

void IgotPltSection::addEntry(Symbol &sym) {
  assert(symAux.back().pltIdx == entries.size());
  entries.push_back(&sym);
}

size_t IgotPltSection::getSize() const {
  return entries.size() * target->gotEntrySize;
}

void IgotPltSection::writeTo(uint8_t *buf) {
  for (const Symbol *b : entries) {
    target->writeIgotPlt(buf, *b);
    buf += target->gotEntrySize;
  }
}

StringTableSection::StringTableSection(StringRef name, bool dynamic)
    : SyntheticSection(dynamic ? (uint64_t)SHF_ALLOC : 0, SHT_STRTAB, 1, name),
      dynamic(dynamic) {
  // ELF string tables start with a NUL byte.
  strings.push_back("");
  stringMap.try_emplace(CachedHashStringRef(""), 0);
  size = 1;
}

// Adds a string to the string table. If `hashIt` is true we hash and check for
// duplicates. It is optional because the name of global symbols are already
// uniqued and hashing them again has a big cost for a small value: uniquing
// them with some other string that happens to be the same.
unsigned StringTableSection::addString(StringRef s, bool hashIt) {
  if (hashIt) {
    auto r = stringMap.try_emplace(CachedHashStringRef(s), size);
    if (!r.second)
      return r.first->second;
  }
  if (s.empty())
    return 0;
  unsigned ret = this->size;
  this->size = this->size + s.size() + 1;
  strings.push_back(s);
  return ret;
}

void StringTableSection::writeTo(uint8_t *buf) {
  for (StringRef s : strings) {
    memcpy(buf, s.data(), s.size());
    buf[s.size()] = '\0';
    buf += s.size() + 1;
  }
}

// Returns the number of entries in .gnu.version_d: the number of
// non-VER_NDX_LOCAL-non-VER_NDX_GLOBAL definitions, plus 1.
// Note that we don't support vd_cnt > 1 yet.
namespace {
unsigned getVerDefNum() { return namedVersionDefs().size() + 1; }
} // namespace

// ===----------------------------------------------------------------------===
// DynamicSection
// ===----------------------------------------------------------------------===

template <class ELFT>
DynamicSection<ELFT>::DynamicSection()
    : SyntheticSection(SHF_ALLOC | SHF_WRITE, SHT_DYNAMIC, config->wordsize,
                       ".dynamic") {
  this->entsize = 16;

  // .dynamic section is not writable when -z rodynamic is passed.
  if (config->zRodynamic)
    this->flags = SHF_ALLOC;
}

// The output section .rela.dyn may include these synthetic sections:
//
// - part.relaDyn
// - in.relaIplt: this is included if in.relaIplt is named .rela.dyn
// - in.relaPlt: this is included if a linker script places .rela.plt inside
//   .rela.dyn
//
// DT_RELASZ is the total size of the included sections.
namespace {
uint64_t addRelaSz(const RelocationBaseSection &relaDyn) {
  size_t size = relaDyn.getSize();
  if (in.relaIplt->getParent() == relaDyn.getParent())
    size += in.relaIplt->getSize();
  if (in.relaPlt->getParent() == relaDyn.getParent())
    size += in.relaPlt->getSize();
  return size;
}
} // namespace

// A Linker script may assign the RELA relocation sections to the same
// output section. When this occurs we cannot just use the OutputSection
// Size. Moreover the [DT_JMPREL, DT_JMPREL + DT_PLTRELSZ) is permitted to
// overlap with the [DT_RELA, DT_RELA + DT_RELASZ).
namespace {
uint64_t addPltRelSz() {
  size_t size = in.relaPlt->getSize();
  if (in.relaIplt->getParent() == in.relaPlt->getParent() &&
      in.relaIplt->name == in.relaPlt->name)
    size += in.relaIplt->getSize();
  return size;
}
} // namespace

// Add remaining entries to complete .dynamic contents.
template <class ELFT>
std::vector<std::pair<int32_t, uint64_t>>
DynamicSection<ELFT>::computeContents() {
  elf::Partition &part = getPartition();
  bool isMain = part.name.empty();
  std::vector<std::pair<int32_t, uint64_t>> entries;

  auto addInt = [&](int32_t tag, uint64_t val) {
    entries.emplace_back(tag, val);
  };
  auto addInSec = [&](int32_t tag, const InputSection &sec) {
    entries.emplace_back(tag, sec.getVA());
  };

  for (StringRef s : config->filterList)
    addInt(DT_FILTER, part.dynStrTab->addString(s));
  for (StringRef s : config->auxiliaryList)
    addInt(DT_AUXILIARY, part.dynStrTab->addString(s));

  if (!config->rpath.empty())
    addInt(config->enableNewDtags ? DT_RUNPATH : DT_RPATH,
           part.dynStrTab->addString(config->rpath));

  for (SharedFile *file : ctx.sharedFiles)
    if (file->isNeeded)
      addInt(DT_NEEDED, part.dynStrTab->addString(file->soName));

  if (isMain) {
    if (!config->soName.empty())
      addInt(DT_SONAME, part.dynStrTab->addString(config->soName));
  } else {
    if (!config->soName.empty())
      addInt(DT_NEEDED, part.dynStrTab->addString(config->soName));
    addInt(DT_SONAME, part.dynStrTab->addString(part.name));
  }

  // Set DT_FLAGS and DT_FLAGS_1.
  uint32_t dtFlags = 0;
  uint32_t dtFlags1 = 0;
  if (config->bsymbolic == BsymbolicKind::All)
    dtFlags |= DF_SYMBOLIC;
  if (config->zGlobal)
    dtFlags1 |= DF_1_GLOBAL;
  if (config->zInitfirst)
    dtFlags1 |= DF_1_INITFIRST;
  if (config->zInterpose)
    dtFlags1 |= DF_1_INTERPOSE;
  if (config->zNodefaultlib)
    dtFlags1 |= DF_1_NODEFLIB;
  if (config->zNodelete)
    dtFlags1 |= DF_1_NODELETE;
  if (config->zNodlopen)
    dtFlags1 |= DF_1_NOOPEN;
  if (config->pie)
    dtFlags1 |= DF_1_PIE;
  if (config->zNow) {
    dtFlags |= DF_BIND_NOW;
    dtFlags1 |= DF_1_NOW;
  }
  if (config->zOrigin) {
    dtFlags |= DF_ORIGIN;
    dtFlags1 |= DF_1_ORIGIN;
  }
  if (!config->zText)
    dtFlags |= DF_TEXTREL;
  if (ctx.hasTlsIe && config->shared)
    dtFlags |= DF_STATIC_TLS;

  if (dtFlags)
    addInt(DT_FLAGS, dtFlags);
  if (dtFlags1)
    addInt(DT_FLAGS_1, dtFlags1);

  // DT_DEBUG is a pointer to debug information used by debuggers at runtime. We
  // need it for each process, so we don't write it for DSOs. The loader writes
  // the pointer into this entry.
  //
  // DT_DEBUG is the only .dynamic entry that needs to be written to. Some
  // systems provide other means to give the debugger this information. Such
  // systems may choose make .dynamic read-only. If -z rodynamic is passed,
  // don't write DT_DEBUG.
  if (!config->shared && !config->zRodynamic)
    addInt(DT_DEBUG, 0);

  if (part.relaDyn->isNeeded() ||
      (in.relaIplt->isNeeded() &&
       part.relaDyn->getParent() == in.relaIplt->getParent())) {
    addInSec(part.relaDyn->dynamicTag, *part.relaDyn);
    entries.emplace_back(part.relaDyn->sizeDynamicTag,
                         addRelaSz(*part.relaDyn));

    bool isRela = config->isRela;
    addInt(isRela ? DT_RELAENT : DT_RELENT,
           isRela ? sizeof(Elf_Rela) : sizeof(Elf_Rel));

    {
      size_t numRelativeRels = part.relaDyn->getRelativeRelocCount();
      if (config->zCombreloc && numRelativeRels)
        addInt(isRela ? DT_RELACOUNT : DT_RELCOUNT, numRelativeRels);
    }
  }
  if (part.relrDyn && part.relrDyn->getParent() &&
      !part.relrDyn->relocs.empty()) {
    addInSec(config->useAndroidRelrTags ? DT_ANDROID_RELR : DT_RELR,
             *part.relrDyn);
    addInt(config->useAndroidRelrTags ? DT_ANDROID_RELRSZ : DT_RELRSZ,
           part.relrDyn->getParent()->size);
    addInt(config->useAndroidRelrTags ? DT_ANDROID_RELRENT : DT_RELRENT,
           sizeof(Elf_Relr));
  }
  // .rel[a].plt section usually consists of two parts, containing plt and
  // iplt relocations. It is possible to have only iplt relocations in the
  // output. In that case relaPlt is empty and have zero offset, the same offset
  // as relaIplt has. And we still want to emit proper dynamic tags for that
  // case, so here we always use relaPlt as marker for the beginning of
  // .rel[a].plt section.
  if (isMain && (in.relaPlt->isNeeded() || in.relaIplt->isNeeded())) {
    addInSec(DT_JMPREL, *in.relaPlt);
    entries.emplace_back(DT_PLTRELSZ, addPltRelSz());
    if (config->emachine == EM_AARCH64) {
      if (llvm::find_if(in.relaPlt->relocs, [](const DynamicReloc &r) {
            return r.type == target->pltRel &&
                   r.sym->stOther & STO_AARCH64_VARIANT_PCS;
          }) != in.relaPlt->relocs.end())
        addInt(DT_AARCH64_VARIANT_PCS, 0);
    }
    addInSec(DT_PLTGOT, *in.gotPlt);
    addInt(DT_PLTREL, config->isRela ? DT_RELA : DT_REL);
  }

  if (config->emachine == EM_AARCH64) {
    if (config->andFeatures & GNU_PROPERTY_AARCH64_FEATURE_1_BTI)
      addInt(DT_AARCH64_BTI_PLT, 0);
    if (config->zPacPlt)
      addInt(DT_AARCH64_PAC_PLT, 0);

    if (config->androidMemtagMode != ELF::NT_MEMTAG_LEVEL_NONE) {
      addInt(DT_AARCH64_MEMTAG_MODE,
             config->androidMemtagMode == NT_MEMTAG_LEVEL_ASYNC);
      addInt(DT_AARCH64_MEMTAG_HEAP, config->androidMemtagHeap);
      addInt(DT_AARCH64_MEMTAG_STACK, config->androidMemtagStack);
      if (mainPart->memtagDescriptors->isNeeded()) {
        addInSec(DT_AARCH64_MEMTAG_GLOBALS, *mainPart->memtagDescriptors);
        addInt(DT_AARCH64_MEMTAG_GLOBALSSZ,
               mainPart->memtagDescriptors->getSize());
      }
    }
  }

  addInSec(DT_SYMTAB, *part.dynSymTab);
  addInt(DT_SYMENT, sizeof(Elf_Sym));
  addInSec(DT_STRTAB, *part.dynStrTab);
  addInt(DT_STRSZ, part.dynStrTab->getSize());
  if (!config->zText)
    addInt(DT_TEXTREL, 0);
  if (part.gnuHashTab && part.gnuHashTab->getParent())
    addInSec(DT_GNU_HASH, *part.gnuHashTab);
  if (part.hashTab && part.hashTab->getParent())
    addInSec(DT_HASH, *part.hashTab);

  if (isMain) {
    if (Out::preinitArray) {
      addInt(DT_PREINIT_ARRAY, Out::preinitArray->addr);
      addInt(DT_PREINIT_ARRAYSZ, Out::preinitArray->size);
    }
    if (Out::initArray) {
      addInt(DT_INIT_ARRAY, Out::initArray->addr);
      addInt(DT_INIT_ARRAYSZ, Out::initArray->size);
    }
    if (Out::finiArray) {
      addInt(DT_FINI_ARRAY, Out::finiArray->addr);
      addInt(DT_FINI_ARRAYSZ, Out::finiArray->size);
    }

    if (Symbol *b = symtab.find(config->init))
      if (b->isDefined())
        addInt(DT_INIT, b->getVA());
    if (Symbol *b = symtab.find(config->fini))
      if (b->isDefined())
        addInt(DT_FINI, b->getVA());
  }

  if (part.verSym && part.verSym->isNeeded())
    addInSec(DT_VERSYM, *part.verSym);
  if (part.verDef && part.verDef->isLive()) {
    addInSec(DT_VERDEF, *part.verDef);
    addInt(DT_VERDEFNUM, getVerDefNum());
  }
  if (part.verNeed && part.verNeed->isNeeded()) {
    addInSec(DT_VERNEED, *part.verNeed);
    unsigned needNum = 0;
    for (SharedFile *f : ctx.sharedFiles)
      if (!f->vernauxs.empty())
        ++needNum;
    addInt(DT_VERNEEDNUM, needNum);
  }

  addInt(DT_NULL, 0);
  return entries;
}

template <class ELFT> void DynamicSection<ELFT>::finalizeContents() {
  if (OutputSection *sec = getPartition().dynStrTab->getParent())
    getParent()->link = sec->sectionIndex;
  this->size = computeContents().size() * this->entsize;
}

template <class ELFT> void DynamicSection<ELFT>::writeTo(uint8_t *buf) {
  auto *p = reinterpret_cast<Elf_Dyn *>(buf);

  for (std::pair<int32_t, uint64_t> kv : computeContents()) {
    p->d_tag = kv.first;
    p->d_un.d_val = kv.second;
    ++p;
  }
}

uint64_t DynamicReloc::getOffset() const {
  return inputSec->getVA(offsetInSec);
}

int64_t DynamicReloc::computeAddend() const {
  switch (kind) {
  case AddendOnly:
    assert(sym == nullptr);
    return addend;
  case AgainstSymbol:
    assert(sym != nullptr);
    return addend;
  case AddendOnlyWithTargetVA:
  case AgainstSymbolWithTargetVA: {
    uint64_t ca = InputSection::getRelocTargetVA(inputSec->file, type, addend,
                                                 getOffset(), *sym, expr);
    return ca;
  }
  }
  llvm_unreachable("Unknown DynamicReloc::Kind enum");
}

uint32_t DynamicReloc::getSymIndex(SymbolTableBaseSection *symTab) const {
  if (!needsDynSymIndex())
    return 0;

  size_t index = symTab->getSymbolIndex(sym);
  assert((index != 0 || (type != target->gotRel && type != target->pltRel) ||
          !mainPart->dynSymTab->getParent()) &&
         "GOT or PLT relocation must refer to symbol in dynamic symbol table");
  return index;
}

// ===----------------------------------------------------------------------===
// Relocation sections
// ===----------------------------------------------------------------------===

RelocationBaseSection::RelocationBaseSection(StringRef name, uint32_t type,
                                             int32_t dynamicTag,
                                             int32_t sizeDynamicTag,
                                             bool combreloc,
                                             unsigned concurrency)
    : SyntheticSection(SHF_ALLOC, type, config->wordsize, name),
      dynamicTag(dynamicTag), sizeDynamicTag(sizeDynamicTag),
      relocsVec(concurrency), combreloc(combreloc) {}

void RelocationBaseSection::addSymbolReloc(
    RelType dynType, InputSectionBase &isec, uint64_t offsetInSec, Symbol &sym,
    int64_t addend, std::optional<RelType> addendRelType) {
  addReloc(DynamicReloc::AgainstSymbol, dynType, isec, offsetInSec, sym, addend,
           R_ADDEND, addendRelType ? *addendRelType : target->noneRel);
}

void RelocationBaseSection::addAddendOnlyRelocIfNonPreemptible(
    RelType dynType, GotSection &sec, uint64_t offsetInSec, Symbol &sym,
    RelType addendRelType) {
  // No need to write an addend to the section for preemptible symbols.
  if (sym.isPreemptible)
    addReloc({dynType, &sec, offsetInSec, DynamicReloc::AgainstSymbol, sym, 0,
              R_ABS});
  else
    addReloc(DynamicReloc::AddendOnlyWithTargetVA, dynType, sec, offsetInSec,
             sym, 0, R_ABS, addendRelType);
}

void RelocationBaseSection::mergeRels() {
  size_t newSize = relocs.size();
  for (const auto &v : relocsVec)
    newSize += v.size();
  relocs.reserve(newSize);
  for (const auto &v : relocsVec)
    llvm::append_range(relocs, v);
  relocsVec.clear();
}

void RelocationBaseSection::partitionRels() {
  if (!combreloc)
    return;
  const RelType relativeRel = target->relativeRel;
  numRelativeRelocs =
      llvm::partition(relocs, [=](auto &r) { return r.type == relativeRel; }) -
      relocs.begin();
}

void RelocationBaseSection::finalizeContents() {
  SymbolTableBaseSection *symTab = getPartition().dynSymTab.get();

  // When linking glibc statically, .rel{,a}.plt contains R_*_IRELATIVE
  // relocations due to IFUNC (e.g. strcpy). sh_link will be set to 0 in that
  // case.
  if (symTab && symTab->getParent())
    getParent()->link = symTab->getParent()->sectionIndex;
  else
    getParent()->link = 0;

  if (in.relaPlt.get() == this && in.gotPlt->getParent()) {
    getParent()->flags |= ELF::SHF_INFO_LINK;
    getParent()->info = in.gotPlt->getParent()->sectionIndex;
  }
  if (in.relaIplt.get() == this && in.igotPlt->getParent()) {
    getParent()->flags |= ELF::SHF_INFO_LINK;
    getParent()->info = in.igotPlt->getParent()->sectionIndex;
  }
}

void DynamicReloc::computeRaw(SymbolTableBaseSection *symtab) {
  r_offset = getOffset();
  r_sym = getSymIndex(symtab);
  addend = computeAddend();
  kind = AddendOnly; // Catch errors
}

void RelocationBaseSection::computeRels() {
  SymbolTableBaseSection *symTab = getPartition().dynSymTab.get();
  parallelForEach(relocs,
                  [symTab](DynamicReloc &rel) { rel.computeRaw(symTab); });
  // Sort by (!IsRelative,SymIndex,r_offset). DT_REL[A]COUNT requires us to
  // place R_*_RELATIVE first. SymIndex is to improve locality, while r_offset
  // is to make results easier to read.
  if (combreloc) {
    auto nonRelative = relocs.begin() + numRelativeRelocs;
    parallelSort(relocs.begin(), nonRelative,
                 [&](auto &a, auto &b) { return a.r_offset < b.r_offset; });
    // Non-relative relocations are few, so don't bother with parallelSort.
    llvm::sort(nonRelative, relocs.end(), [&](auto &a, auto &b) {
      return std::tie(a.r_sym, a.r_offset) < std::tie(b.r_sym, b.r_offset);
    });
  }
}

template <class ELFT>
RelocationSection<ELFT>::RelocationSection(StringRef name, bool combreloc,
                                           unsigned concurrency)
    : RelocationBaseSection(name, config->isRela ? SHT_RELA : SHT_REL,
                            config->isRela ? DT_RELA : DT_REL,
                            config->isRela ? DT_RELASZ : DT_RELSZ, combreloc,
                            concurrency) {
  this->entsize = config->isRela ? sizeof(Elf_Rela) : sizeof(Elf_Rel);
}

template <class ELFT> void RelocationSection<ELFT>::writeTo(uint8_t *buf) {
  computeRels();
  for (const DynamicReloc &rel : relocs) {
    auto *p = reinterpret_cast<Elf_Rela *>(buf);
    p->r_offset = rel.r_offset;
    p->setSymbolAndType(rel.r_sym, rel.type);
    if (config->isRela)
      p->r_addend = rel.addend;
    buf += config->isRela ? sizeof(Elf_Rela) : sizeof(Elf_Rel);
  }
}

RelrBaseSection::RelrBaseSection(unsigned concurrency)
    : SyntheticSection(SHF_ALLOC,
                       config->useAndroidRelrTags ? SHT_ANDROID_RELR : SHT_RELR,
                       config->wordsize, ".relr.dyn"),
      relocsVec(concurrency) {}

void RelrBaseSection::mergeRels() {
  size_t newSize = relocs.size();
  for (const auto &v : relocsVec)
    newSize += v.size();
  relocs.reserve(newSize);
  for (const auto &v : relocsVec)
    llvm::append_range(relocs, v);
  relocsVec.clear();
}

template <class ELFT>
AndroidPackedRelocationSection<ELFT>::AndroidPackedRelocationSection(
    StringRef name, unsigned concurrency)
    : RelocationBaseSection(
          name, config->isRela ? SHT_ANDROID_RELA : SHT_ANDROID_REL,
          config->isRela ? DT_ANDROID_RELA : DT_ANDROID_REL,
          config->isRela ? DT_ANDROID_RELASZ : DT_ANDROID_RELSZ,
          /*combreloc=*/false, concurrency) {
  this->entsize = 1;
}

template <class ELFT>
bool AndroidPackedRelocationSection<ELFT>::updateAllocSize() {
  // This function computes the contents of an Android-format packed relocation
  // section.
  //
  // This format compresses relocations by using relocation groups to factor out
  // fields that are common between relocations and storing deltas from previous
  // relocations in SLEB128 format (which has a short representation for small
  // numbers). A good example of a relocation type with common fields is
  // R_*_RELATIVE, which is normally used to represent function pointers in
  // vtables. In the REL format, each relative relocation has the same r_info
  // field, and is only different from other relative relocations in terms of
  // the r_offset field. By sorting relocations by offset, grouping them by
  // r_info and representing each relocation with only the delta from the
  // previous offset, each 8-byte relocation can be compressed to as little as 1
  // byte (or less with run-length encoding). This relocation packer was able to
  // reduce the size of the relocation section in an Android Chromium DSO from
  // 2,911,184 bytes to 174,693 bytes, or 6% of the original size.
  //
  // A relocation section consists of a header containing the literal bytes
  // 'APS2' followed by a sequence of SLEB128-encoded integers. The first two
  // elements are the total number of relocations in the section and an initial
  // r_offset value. The remaining elements define a sequence of relocation
  // groups. Each relocation group starts with a header consisting of the
  // following elements:
  //
  // - the number of relocations in the relocation group
  // - flags for the relocation group
  // - (if RELOCATION_GROUPED_BY_OFFSET_DELTA_FLAG is set) the r_offset delta
  //   for each relocation in the group.
  // - (if RELOCATION_GROUPED_BY_INFO_FLAG is set) the value of the r_info
  //   field for each relocation in the group.
  // - (if RELOCATION_GROUP_HAS_ADDEND_FLAG and
  //   RELOCATION_GROUPED_BY_ADDEND_FLAG are set) the r_addend delta for
  //   each relocation in the group.
  //
  // Following the relocation group header are descriptions of each of the
  // relocations in the group. They consist of the following elements:
  //
  // - (if RELOCATION_GROUPED_BY_OFFSET_DELTA_FLAG is not set) the r_offset
  //   delta for this relocation.
  // - (if RELOCATION_GROUPED_BY_INFO_FLAG is not set) the value of the r_info
  //   field for this relocation.
  // - (if RELOCATION_GROUP_HAS_ADDEND_FLAG is set and
  //   RELOCATION_GROUPED_BY_ADDEND_FLAG is not set) the r_addend delta for
  //   this relocation.

  size_t oldSize = relocData.size();

  relocData = {'A', 'P', 'S', '2'};
  raw_svector_ostream os(relocData);
  auto add = [&](int64_t v) { encodeSLEB128(v, os); };

  // The format header includes the number of relocations and the initial
  // offset (we set this to zero because the first relocation group will
  // perform the initial adjustment).
  add(relocs.size());
  add(0);

  std::vector<Elf_Rela> relatives, nonRelatives;

  for (const DynamicReloc &rel : relocs) {
    Elf_Rela r;
    r.r_offset = rel.getOffset();
    r.setSymbolAndType(rel.getSymIndex(getPartition().dynSymTab.get()),
                       rel.type);
    r.r_addend = config->isRela ? rel.computeAddend() : 0;

    if (r.getType() == target->relativeRel)
      relatives.push_back(r);
    else
      nonRelatives.push_back(r);
  }

  llvm::sort(relatives, [](const Elf_Rel &a, const Elf_Rel &b) {
    return a.r_offset < b.r_offset;
  });

  // Try to find groups of relative relocations which are spaced one word
  // apart from one another. These generally correspond to vtable entries. The
  // format allows these groups to be encoded using a sort of run-length
  // encoding, but each group will cost 7 bytes in addition to the offset from
  // the previous group, so it is only profitable to do this for groups of
  // size 8 or larger.
  std::vector<Elf_Rela> ungroupedRelatives;
  std::vector<std::vector<Elf_Rela>> relativeGroups;
  for (auto i = relatives.begin(), e = relatives.end(); i != e;) {
    std::vector<Elf_Rela> group;
    do {
      group.push_back(*i++);
    } while (i != e && (i - 1)->r_offset + config->wordsize == i->r_offset);

    if (group.size() < 8)
      ungroupedRelatives.insert(ungroupedRelatives.end(), group.begin(),
                                group.end());
    else
      relativeGroups.emplace_back(std::move(group));
  }

  // For non-relative relocations, we would like to:
  //   1. Have relocations with the same symbol offset to be consecutive, so
  //      that the runtime linker can speed-up symbol lookup by implementing an
  //      1-entry cache.
  //   2. Group relocations by r_info to reduce the size of the relocation
  //      section.
  // Since the symbol offset is the high bits in r_info, sorting by r_info
  // allows us to do both.
  //
  // For Rela, we also want to sort by r_addend when r_info is the same. This
  // enables us to group by r_addend as well.
  llvm::sort(nonRelatives, [](const Elf_Rela &a, const Elf_Rela &b) {
    if (a.r_info != b.r_info)
      return a.r_info < b.r_info;
    if (a.r_addend != b.r_addend)
      return a.r_addend < b.r_addend;
    return a.r_offset < b.r_offset;
  });

  // Group relocations with the same r_info. Note that each group emits a group
  // header and that may make the relocation section larger. It is hard to
  // estimate the size of a group header as the encoded size of that varies
  // based on r_info. However, we can approximate this trade-off by the number
  // of values encoded. Each group header contains 3 values, and each relocation
  // in a group encodes one less value, as compared to when it is not grouped.
  // Therefore, we only group relocations if there are 3 or more of them with
  // the same r_info.
  //
  // For Rela, the addend for most non-relative relocations is zero, and thus we
  // can usually get a smaller relocation section if we group relocations with 0
  // addend as well.
  std::vector<Elf_Rela> ungroupedNonRelatives;
  std::vector<std::vector<Elf_Rela>> nonRelativeGroups;
  for (auto i = nonRelatives.begin(), e = nonRelatives.end(); i != e;) {
    auto j = i + 1;
    while (j != e && i->r_info == j->r_info &&
           (!config->isRela || i->r_addend == j->r_addend))
      ++j;
    if (j - i < 3 || (config->isRela && i->r_addend != 0))
      ungroupedNonRelatives.insert(ungroupedNonRelatives.end(), i, j);
    else
      nonRelativeGroups.emplace_back(i, j);
    i = j;
  }

  // Sort ungrouped relocations by offset to minimize the encoded length.
  llvm::sort(ungroupedNonRelatives, [](const Elf_Rela &a, const Elf_Rela &b) {
    return a.r_offset < b.r_offset;
  });

  unsigned hasAddendIfRela =
      config->isRela ? RELOCATION_GROUP_HAS_ADDEND_FLAG : 0;

  uint64_t offset = 0;
  uint64_t addend = 0;

  // Emit the run-length encoding for the groups of adjacent relative
  // relocations. Each group is represented using two groups in the packed
  // format. The first is used to set the current offset to the start of the
  // group (and also encodes the first relocation), and the second encodes the
  // remaining relocations.
  for (std::vector<Elf_Rela> &g : relativeGroups) {
    // The first relocation in the group.
    add(1);
    add(RELOCATION_GROUPED_BY_OFFSET_DELTA_FLAG |
        RELOCATION_GROUPED_BY_INFO_FLAG | hasAddendIfRela);
    add(g[0].r_offset - offset);
    add(target->relativeRel);
    if (config->isRela) {
      add(g[0].r_addend - addend);
      addend = g[0].r_addend;
    }

    // The remaining relocations.
    add(g.size() - 1);
    add(RELOCATION_GROUPED_BY_OFFSET_DELTA_FLAG |
        RELOCATION_GROUPED_BY_INFO_FLAG | hasAddendIfRela);
    add(config->wordsize);
    add(target->relativeRel);
    if (config->isRela) {
      for (const auto &i : llvm::drop_begin(g)) {
        add(i.r_addend - addend);
        addend = i.r_addend;
      }
    }

    offset = g.back().r_offset;
  }

  // Now the ungrouped relatives.
  if (!ungroupedRelatives.empty()) {
    add(ungroupedRelatives.size());
    add(RELOCATION_GROUPED_BY_INFO_FLAG | hasAddendIfRela);
    add(target->relativeRel);
    for (Elf_Rela &r : ungroupedRelatives) {
      add(r.r_offset - offset);
      offset = r.r_offset;
      if (config->isRela) {
        add(r.r_addend - addend);
        addend = r.r_addend;
      }
    }
  }

  // Grouped non-relatives.
  for (ArrayRef<Elf_Rela> g : nonRelativeGroups) {
    add(g.size());
    add(RELOCATION_GROUPED_BY_INFO_FLAG);
    add(g[0].r_info);
    for (const Elf_Rela &r : g) {
      add(r.r_offset - offset);
      offset = r.r_offset;
    }
    addend = 0;
  }

  // Finally the ungrouped non-relative relocations.
  if (!ungroupedNonRelatives.empty()) {
    add(ungroupedNonRelatives.size());
    add(hasAddendIfRela);
    for (Elf_Rela &r : ungroupedNonRelatives) {
      add(r.r_offset - offset);
      offset = r.r_offset;
      add(r.r_info);
      if (config->isRela) {
        add(r.r_addend - addend);
        addend = r.r_addend;
      }
    }
  }

  // Don't allow the section to shrink; otherwise the size of the section can
  // oscillate infinitely.
  if (relocData.size() < oldSize)
    relocData.append(oldSize - relocData.size(), 0);

  // Returns whether the section size changed. We need to keep recomputing both
  // section layout and the contents of this section until the size converges
  // because changing this section's size can affect section layout, which in
  // turn can affect the sizes of the LEB-encoded integers stored in this
  // section.
  return relocData.size() != oldSize;
}

template <class ELFT>
RelrSection<ELFT>::RelrSection(unsigned concurrency)
    : RelrBaseSection(concurrency) {
  this->entsize = config->wordsize;
}

template <class ELFT> bool RelrSection<ELFT>::updateAllocSize() {
  // This function computes the contents of an SHT_RELR packed relocation
  // section.
  //
  // Proposal for adding SHT_RELR sections to generic-abi is here:
  //   https://groups.google.com/forum/#!topic/generic-abi/bX460iggiKg
  //
  // The encoded sequence of Elf64_Relr entries in a SHT_RELR section looks
  // like [ AAAAAAAA BBBBBBB1 BBBBBBB1 ... AAAAAAAA BBBBBB1 ... ]
  //
  // i.e. start with an address, followed by any number of bitmaps. The address
  // entry encodes 1 relocation. The subsequent bitmap entries encode up to 63
  // relocations each, at subsequent offsets following the last address entry.
  //
  // The bitmap entries must have 1 in the least significant bit. The assumption
  // here is that an address cannot have 1 in lsb. Odd addresses are not
  // supported.
  //
  // Excluding the least significant bit in the bitmap, each non-zero bit in
  // the bitmap represents a relocation to be applied to a corresponding machine
  // word that follows the base address word. The second least significant bit
  // represents the machine word immediately following the initial address, and
  // each bit that follows represents the next word, in linear order. As such,
  // a single bitmap can encode up to 31 relocations in a 32-bit object, and
  // 63 relocations in a 64-bit object.
  //
  // This encoding has a couple of interesting properties:
  // 1. Looking at any entry, it is clear whether it's an address or a bitmap:
  //    even means address, odd means bitmap.
  // 2. Just a simple list of addresses is a valid encoding.

  size_t oldSize = relrRelocs.size();
  relrRelocs.clear();

  // Same as Config->Wordsize but faster because this is a compile-time
  // constant.
  const size_t wordsize = sizeof(typename ELFT::uint);

  // Number of bits to use for the relocation offsets bitmap.
  // Must be either 63 or 31.
  const size_t nBits = wordsize * 8 - 1;

  // Get offsets for all relative relocations and sort them.
  std::unique_ptr<uint64_t[]> offsets(new uint64_t[relocs.size()]);
  for (auto [i, r] : llvm::enumerate(relocs))
    offsets[i] = r.getOffset();
  llvm::sort(offsets.get(), offsets.get() + relocs.size());

  // For each leading relocation, find following ones that can be folded
  // as a bitmap and fold them.
  for (size_t i = 0, e = relocs.size(); i != e;) {
    // Add a leading relocation.
    relrRelocs.push_back(Elf_Relr(offsets[i]));
    uint64_t base = offsets[i] + wordsize;
    ++i;

    // Find foldable relocations to construct bitmaps.
    for (;;) {
      uint64_t bitmap = 0;
      for (; i != e; ++i) {
        uint64_t d = offsets[i] - base;
        if (d >= nBits * wordsize || d % wordsize)
          break;
        bitmap |= uint64_t(1) << (d / wordsize);
      }
      if (!bitmap)
        break;
      relrRelocs.push_back(Elf_Relr((bitmap << 1) | 1));
      base += nBits * wordsize;
    }
  }

  // Don't allow the section to shrink; otherwise the size of the section can
  // oscillate infinitely. Trailing 1s do not decode to more relocations.
  if (relrRelocs.size() < oldSize) {
    log(".relr.dyn needs " + Twine(oldSize - relrRelocs.size()) +
        " padding word(s)");
    relrRelocs.resize(oldSize, Elf_Relr(1));
  }

  return relrRelocs.size() != oldSize;
}

// ===----------------------------------------------------------------------===
// Symbol table & hash table sections
// ===----------------------------------------------------------------------===

SymbolTableBaseSection::SymbolTableBaseSection(StringTableSection &strTabSec)
    : SyntheticSection(strTabSec.isDynamic() ? (uint64_t)SHF_ALLOC : 0,
                       strTabSec.isDynamic() ? SHT_DYNSYM : SHT_SYMTAB,
                       config->wordsize,
                       strTabSec.isDynamic() ? ".dynsym" : ".symtab"),
      strTabSec(strTabSec) {}

void SymbolTableBaseSection::finalizeContents() {
  if (OutputSection *sec = strTabSec.getParent())
    getParent()->link = sec->sectionIndex;

  if (this->type != SHT_DYNSYM) {
    sortSymTabSymbols();
    return;
  }

  // If it is a .dynsym, there should be no local symbols, but we need
  // to do a few things for the dynamic linker.

  // Section's Info field has the index of the first non-local symbol.
  // Because the first symbol entry is a null entry, 1 is the first.
  getParent()->info = 1;

  if (getPartition().gnuHashTab) {
    // NB: It also sorts Symbols to meet the GNU hash table requirements.
    getPartition().gnuHashTab->addSymbols(symbols);
  }

  // Only the main partition's dynsym indexes are stored in the symbols
  // themselves. All other partitions use a lookup table.
  if (this == mainPart->dynSymTab.get()) {
    size_t i = 0;
    for (const SymbolTableEntry &s : symbols)
      s.sym->dynsymIndex = ++i;
  }
}

// The ELF spec requires that all local symbols precede global symbols, so we
// sort symbol entries in this function. (For .dynsym, we don't do that because
// symbols for dynamic linking are inherently all globals.)
//
// Aside from above, we put local symbols in groups starting with the STT_FILE
// symbol. That is convenient for purpose of identifying where are local symbols
// coming from.
void SymbolTableBaseSection::sortSymTabSymbols() {
  // Move all local symbols before global symbols.
  auto e = std::stable_partition(
      symbols.begin(), symbols.end(),
      [](const SymbolTableEntry &s) { return s.sym->isLocal(); });
  size_t numLocals = e - symbols.begin();
  getParent()->info = numLocals + 1;

  // We want to group the local symbols by file. For that we rebuild the local
  // part of the symbols vector. We do not need to care about the STT_FILE
  // symbols, they are already naturally placed first in each group. That
  // happens because STT_FILE is always the first symbol in the object and hence
  // precede all other local symbols we add for a file.
  MapVector<InputFile *, SmallVector<SymbolTableEntry, 0>> arr;
  for (const SymbolTableEntry &s : llvm::make_range(symbols.begin(), e))
    arr[s.sym->file].push_back(s);

  auto i = symbols.begin();
  for (auto &p : arr)
    for (SymbolTableEntry &entry : p.second)
      *i++ = entry;
}

void SymbolTableBaseSection::addSymbol(Symbol *b) {
  // Adding a local symbol to a .dynsym is a bug.
  assert(this->type != SHT_DYNSYM || !b->isLocal());
  symbols.push_back({b, strTabSec.addString(b->getName(), false)});
}

size_t SymbolTableBaseSection::getSymbolIndex(Symbol *sym) {
  if (this == mainPart->dynSymTab.get())
    return sym->dynsymIndex;

  // Initializes symbol lookup tables lazily. This is used only for -r,
  // --emit-relocs and dynsyms in partitions other than the main one.
  llvm::call_once(onceFlag, [&] {
    symbolIndexMap.reserve(symbols.size());
    size_t i = 0;
    for (const SymbolTableEntry &e : symbols) {
      if (e.sym->type == STT_SECTION)
        sectionIndexMap[e.sym->getOutputSection()] = ++i;
      else
        symbolIndexMap[e.sym] = ++i;
    }
  });

  // Section symbols are mapped based on their output sections
  // to maintain their semantics.
  if (sym->type == STT_SECTION)
    return sectionIndexMap.lookup(sym->getOutputSection());
  return symbolIndexMap.lookup(sym);
}

template <class ELFT>
SymbolTableSection<ELFT>::SymbolTableSection(StringTableSection &strTabSec)
    : SymbolTableBaseSection(strTabSec) {
  this->entsize = sizeof(Elf_Sym);
}

namespace {
BssSection *getCommonSec(Symbol *sym) { return nullptr; }
} // namespace

namespace {
uint32_t getSymSectionIndex(Symbol *sym) {
  assert(!(sym->hasFlag(NEEDS_COPY) && sym->isObject()));
  if (!isa<Defined>(sym) || sym->hasFlag(NEEDS_COPY))
    return SHN_UNDEF;
  if (const OutputSection *os = sym->getOutputSection())
    return os->sectionIndex >= SHN_LORESERVE ? (uint32_t)SHN_XINDEX
                                             : os->sectionIndex;
  return SHN_ABS;
}
} // namespace

// Write the internal symbol table contents to the output symbol table.
template <class ELFT> void SymbolTableSection<ELFT>::writeTo(uint8_t *buf) {
  // The first entry is a null entry as per the ELF spec.
  buf += sizeof(Elf_Sym);

  auto *eSym = reinterpret_cast<Elf_Sym *>(buf);

  for (SymbolTableEntry &ent : symbols) {
    Symbol *sym = ent.sym;
    bool isDefinedHere = type == SHT_SYMTAB || sym->partition == partition;

    // Set st_name, st_info and st_other.
    eSym->st_name = ent.strTabOffset;
    eSym->setBindingAndType(sym->binding, sym->type);
    eSym->st_other = sym->stOther;

    if (BssSection *commonSec = getCommonSec(sym)) {
      // When -r is specified, a COMMON symbol is not allocated. Its st_shndx
      // holds SHN_COMMON and st_value holds the alignment.
      eSym->st_shndx = SHN_COMMON;
      eSym->st_value = commonSec->addralign;
      eSym->st_size = cast<Defined>(sym)->size;
    } else {
      const uint32_t shndx = getSymSectionIndex(sym);
      if (isDefinedHere) {
        eSym->st_shndx = shndx;
        eSym->st_value = sym->getVA();
        // Copy symbol size if it is a defined symbol. st_size is not
        // significant for undefined symbols, so whether copying it or not is up
        // to us if that's the case. We'll leave it as zero because by not
        // setting a value, we can get the exact same outputs for two sets of
        // input files that differ only in undefined symbol size in DSOs.
        eSym->st_size = shndx != SHN_UNDEF ? cast<Defined>(sym)->size : 0;
      } else {
        eSym->st_shndx = 0;
        eSym->st_value = 0;
        eSym->st_size = 0;
      }
    }

    ++eSym;
  }
}

SymtabShndxSection::SymtabShndxSection()
    : SyntheticSection(0, SHT_SYMTAB_SHNDX, 4, ".symtab_shndx") {
  this->entsize = 4;
}

void SymtabShndxSection::writeTo(uint8_t *buf) {
  // We write an array of 32 bit values, where each value has 1:1 association
  // with an entry in .symtab. If the corresponding entry contains SHN_XINDEX,
  // we need to write actual index, otherwise, we must write SHN_UNDEF(0).
  buf += 4; // Ignore .symtab[0] entry.
  for (const SymbolTableEntry &entry : in.symTab->getSymbols()) {
    if (!getCommonSec(entry.sym) && getSymSectionIndex(entry.sym) == SHN_XINDEX)
      write32(buf, entry.sym->getOutputSection()->sectionIndex);
    buf += 4;
  }
}

bool SymtabShndxSection::isNeeded() const {
  // SHT_SYMTAB can hold symbols with section indices values up to
  // SHN_LORESERVE. If we need more, we want to use extension SHT_SYMTAB_SHNDX
  // section. Problem is that we reveal the final section indices a bit too
  // late, and we do not know them here. For simplicity, we just always create
  // a .symtab_shndx section when the amount of output sections is huge.
  size_t size = 0;
  for (SectionCommand *cmd : script->sectionCommands)
    if (isa<OutputDesc>(cmd))
      ++size;
  return size >= SHN_LORESERVE;
}

void SymtabShndxSection::finalizeContents() {
  getParent()->link = in.symTab->getParent()->sectionIndex;
}

size_t SymtabShndxSection::getSize() const {
  return in.symTab->getNumSymbols() * 4;
}

// .hash and .gnu.hash sections contain on-disk hash tables that map
// symbol names to their dynamic symbol table indices. Their purpose
// is to help the dynamic linker resolve symbols quickly. If ELF files
// don't have them, the dynamic linker has to do linear search on all
// dynamic symbols, which makes programs slower. Therefore, a .hash
// section is added to a DSO by default.
//
// The Unix semantics of resolving dynamic symbols is somewhat expensive.
// Each ELF file has a list of DSOs that the ELF file depends on and a
// list of dynamic symbols that need to be resolved from any of the
// DSOs. That means resolving all dynamic symbols takes O(m)*O(n)
// where m is the number of DSOs and n is the number of dynamic
// symbols. For modern large programs, both m and n are large.  So
// making each step faster by using hash tables substantially
// improves time to load programs.
//
// (Note that this is not the only way to design the shared library.
// For instance, the Windows DLL takes a different approach. On
// Windows, each dynamic symbol has a name of DLL from which the symbol
// has to be resolved. That makes the cost of symbol resolution O(n).
// This disables some hacky techniques you can use on Unix such as
// LD_PRELOAD, but this is arguably better semantics than the Unix ones.)
//
// Due to historical reasons, we have two different hash tables, .hash
// and .gnu.hash. They are for the same purpose, and .gnu.hash is a new
// and better version of .hash. .hash is just an on-disk hash table, but
// .gnu.hash has a bloom filter in addition to a hash table to skip
// DSOs very quickly. If you are sure that your dynamic linker knows
// about .gnu.hash, you want to specify --hash-style=gnu. Otherwise, a
// safe bet is to specify --hash-style=both for backward compatibility.
GnuHashTableSection::GnuHashTableSection()
    : SyntheticSection(SHF_ALLOC, SHT_GNU_HASH, config->wordsize, ".gnu.hash") {
}

void GnuHashTableSection::finalizeContents() {
  if (OutputSection *sec = getPartition().dynSymTab->getParent())
    getParent()->link = sec->sectionIndex;

  // Computes bloom filter size in word size. We want to allocate 12
  // bits for each symbol. It must be a power of two.
  if (symbols.empty()) {
    maskWords = 1;
  } else {
    uint64_t numBits = symbols.size() * 12;
    maskWords = NextPowerOf2(numBits / (config->wordsize * 8));
  }

  size = 16;                            // Header
  size += config->wordsize * maskWords; // Bloom filter
  size += nBuckets * 4;                 // Hash buckets
  size += symbols.size() * 4;           // Hash values
}

void GnuHashTableSection::writeTo(uint8_t *buf) {
  // Write a header.
  write32(buf, nBuckets);
  write32(buf + 4, getPartition().dynSymTab->getNumSymbols() - symbols.size());
  write32(buf + 8, maskWords);
  write32(buf + 12, Shift2);
  buf += 16;

  // Write the 2-bit bloom filter.
  const unsigned c = 64;
  for (const Entry &sym : symbols) {
    // When C = 64, we choose a word with bits [6:...] and set 1 to two bits in
    // the word using bits [0:5] and [26:31].
    size_t i = (sym.hash / c) & (maskWords - 1);
    uint64_t val = readUint(buf + i * config->wordsize);
    val |= uint64_t(1) << (sym.hash % c);
    val |= uint64_t(1) << ((sym.hash >> Shift2) % c);
    writeUint(buf + i * config->wordsize, val);
  }
  buf += config->wordsize * maskWords;

  // Write the hash table.
  uint32_t *buckets = reinterpret_cast<uint32_t *>(buf);
  uint32_t oldBucket = -1;
  uint32_t *values = buckets + nBuckets;
  for (auto i = symbols.begin(), e = symbols.end(); i != e; ++i) {
    // Write a hash value. It represents a sequence of chains that share the
    // same hash modulo value. The last element of each chain is terminated by
    // LSB 1.
    uint32_t hash = i->hash;
    bool isLastInChain = (i + 1) == e || i->bucketIdx != (i + 1)->bucketIdx;
    hash = isLastInChain ? hash | 1 : hash & ~1;
    write32(values++, hash);

    if (i->bucketIdx == oldBucket)
      continue;
    // Write a hash bucket. Hash buckets contain indices in the following hash
    // value table.
    write32(buckets + i->bucketIdx,
            getPartition().dynSymTab->getSymbolIndex(i->sym));
    oldBucket = i->bucketIdx;
  }
}

// Add symbols to this symbol hash table. Note that this function
// destructively sort a given vector -- which is needed because
// GNU-style hash table places some sorting requirements.
void GnuHashTableSection::addSymbols(SmallVectorImpl<SymbolTableEntry> &v) {
  // We cannot use 'auto' for Mid because GCC 6.1 cannot deduce
  // its type correctly.
  auto mid =
      std::stable_partition(v.begin(), v.end(), [&](const SymbolTableEntry &s) {
        return !s.sym->isDefined() || s.sym->partition != partition;
      });

  // We chose load factor 4 for the on-disk hash table. For each hash
  // collision, the dynamic linker will compare a uint32_t hash value.
  // Since the integer comparison is quite fast, we believe we can
  // make the load factor even larger. 4 is just a conservative choice.
  //
  // Note that we don't want to create a zero-sized hash table because
  // Android loader as of 2018 doesn't like a .gnu.hash containing such
  // table. If that's the case, we create a hash table with one unused
  // dummy slot.
  nBuckets = std::max<size_t>((v.end() - mid) / 4, 1);

  if (mid == v.end())
    return;

  for (SymbolTableEntry &ent : llvm::make_range(mid, v.end())) {
    Symbol *b = ent.sym;
    uint32_t hash = hashGnu(b->getName());
    uint32_t bucketIdx = hash % nBuckets;
    symbols.push_back({b, ent.strTabOffset, hash, bucketIdx});
  }

  llvm::sort(symbols, [](const Entry &l, const Entry &r) {
    return std::tie(l.bucketIdx, l.strTabOffset) <
           std::tie(r.bucketIdx, r.strTabOffset);
  });

  v.erase(mid, v.end());
  for (const Entry &ent : symbols)
    v.push_back({ent.sym, ent.strTabOffset});
}

HashTableSection::HashTableSection()
    : SyntheticSection(SHF_ALLOC, SHT_HASH, 4, ".hash") {
  this->entsize = 4;
}

void HashTableSection::finalizeContents() {
  SymbolTableBaseSection *symTab = getPartition().dynSymTab.get();

  if (OutputSection *sec = symTab->getParent())
    getParent()->link = sec->sectionIndex;

  unsigned numEntries = 2;               // nbucket and nchain.
  numEntries += symTab->getNumSymbols(); // The chain entries.

  // Create as many buckets as there are symbols.
  numEntries += symTab->getNumSymbols();
  this->size = numEntries * 4;
}

void HashTableSection::writeTo(uint8_t *buf) {
  SymbolTableBaseSection *symTab = getPartition().dynSymTab.get();
  unsigned numSymbols = symTab->getNumSymbols();

  uint32_t *p = reinterpret_cast<uint32_t *>(buf);
  write32(p++, numSymbols); // nbucket
  write32(p++, numSymbols); // nchain

  uint32_t *buckets = p;
  uint32_t *chains = p + numSymbols;

  for (const SymbolTableEntry &s : symTab->getSymbols()) {
    Symbol *sym = s.sym;
    StringRef name = sym->getName();
    unsigned i = sym->dynsymIndex;
    uint32_t hash = hashSysV(name) % numSymbols;
    chains[i] = buckets[hash];
    write32(buckets + hash, i);
  }
}

PltSection::PltSection()
    : SyntheticSection(SHF_ALLOC | SHF_EXECINSTR, SHT_PROGBITS, 16, ".plt"),
      headerSize(target->pltHeaderSize) {

  // On x86_64 when IBT is enabled, this section contains the second PLT
  // (lazy symbol resolvers).
  if (config->emachine == EM_X86_64 &&
      (config->andFeatures & GNU_PROPERTY_X86_FEATURE_1_IBT))
    name = ".plt.sec";
}

void PltSection::writeTo(uint8_t *buf) {
  // At beginning of PLT, we have code to call the dynamic
  // linker to resolve dynsyms at runtime. Write such code.
  target->writePltHeader(buf);
  size_t off = headerSize;

  for (const Symbol *sym : entries) {
    target->writePlt(buf + off, *sym, getVA() + off);
    off += target->pltEntrySize;
  }
}

void PltSection::addEntry(Symbol &sym) {
  assert(sym.auxIdx == symAux.size() - 1);
  symAux.back().pltIdx = entries.size();
  entries.push_back(&sym);
}

size_t PltSection::getSize() const {
  return headerSize + entries.size() * target->pltEntrySize;
}

bool PltSection::isNeeded() const {
  // For -z retpolineplt, .iplt needs the .plt header.
  return !entries.empty() || (config->zRetpolineplt && in.iplt->isNeeded());
}

// Used by AArch64 to add mapping symbols in the PLT section, which aid
// disassembly.
void PltSection::addSymbols() {
  target->addPltHeaderSymbols(*this);

  size_t off = headerSize;
  for (size_t i = 0; i < entries.size(); ++i) {
    target->addPltSymbols(*this, off);
    off += target->pltEntrySize;
  }
}

IpltSection::IpltSection()
    : SyntheticSection(SHF_ALLOC | SHF_EXECINSTR, SHT_PROGBITS, 16, ".iplt") {}

void IpltSection::writeTo(uint8_t *buf) {
  uint32_t off = 0;
  for (const Symbol *sym : entries) {
    target->writeIplt(buf + off, *sym, getVA() + off);
    off += target->ipltEntrySize;
  }
}

size_t IpltSection::getSize() const {
  return entries.size() * target->ipltEntrySize;
}

void IpltSection::addEntry(Symbol &sym) {
  assert(sym.auxIdx == symAux.size() - 1);
  symAux.back().pltIdx = entries.size();
  entries.push_back(&sym);
}

// AArch64 uses mapping symbols to aid disassembly.
void IpltSection::addSymbols() {
  size_t off = 0;
  for (size_t i = 0, e = entries.size(); i != e; ++i) {
    target->addPltSymbols(*this, off);
    off += target->pltEntrySize;
  }
}

// This is an x86-only extra PLT section and used only when a security
// enhancement feature called CET is enabled. In this comment, I'll explain what
// the feature is and why we have two PLT sections if CET is enabled.
//
// So, what does CET do? CET introduces a new restriction to indirect jump
// instructions. CET works this way. Assume that CET is enabled. Then, if you
// execute an indirect jump instruction, the processor verifies that a special
// "landing pad" instruction (which is actually a repurposed NOP instruction and
// now called "endbr32" or "endbr64") is at the jump target. If the jump target
// does not start with that instruction, the processor raises an exception
// instead of continuing executing code.
//
// If CET is enabled, the compiler emits endbr to all locations where indirect
// jumps may jump to.
//
// This mechanism makes it extremely hard to transfer the control to a middle of
// a function that is not supporsed to be a indirect jump target, preventing
// certain types of attacks such as ROP or JOP.
//
// Note that the processors in the market as of 2019 don't actually support the
// feature. Only the spec is available at the moment.
//
// Now, I'll explain why we have this extra PLT section for CET.
//
// Since you can indirectly jump to a PLT entry, we have to make PLT entries
// start with endbr. The problem is there's no extra space for endbr (which is 4
// bytes long), as the PLT entry is only 16 bytes long and all bytes are already
// used.
//
// In order to deal with the issue, we split a PLT entry into two PLT entries.
// Remember that each PLT entry contains code to jump to an address read from
// .got.plt AND code to resolve a dynamic symbol lazily. With the 2-PLT scheme,
// the former code is written to .plt.sec, and the latter code is written to
// .plt.
//
// Lazy symbol resolution in the 2-PLT scheme works in the usual way, except
// that the regular .plt is now called .plt.sec and .plt is repurposed to
// contain only code for lazy symbol resolution.
//
// In other words, this is how the 2-PLT scheme works. Application code is
// supposed to jump to .plt.sec to call an external function. Each .plt.sec
// entry contains code to read an address from a corresponding .got.plt entry
// and jump to that address. Addresses in .got.plt initially point to .plt, so
// when an application calls an external function for the first time, the
// control is transferred to a function that resolves a symbol name from
// external shared object files. That function then rewrites a .got.plt entry
// with a resolved address, so that the subsequent function calls directly jump
// to a desired location from .plt.sec.
//
// There is an open question as to whether the 2-PLT scheme was desirable or
// not. We could have simply extended the PLT entry size to 32-bytes to
// accommodate endbr, and that scheme would have been much simpler than the
// 2-PLT scheme. One reason to split PLT was, by doing that, we could keep hot
// code (.plt.sec) from cold code (.plt). But as far as I know no one proved
// that the optimization actually makes a difference.
//
// That said, the 2-PLT scheme is a part of the ABI, debuggers and other tools
// depend on it, so we implement the ABI.
IBTPltSection::IBTPltSection()
    : SyntheticSection(SHF_ALLOC | SHF_EXECINSTR, SHT_PROGBITS, 16, ".plt") {}

void IBTPltSection::writeTo(uint8_t *buf) {
  target->writeIBTPlt(buf, in.plt->getNumEntries());
}

size_t IBTPltSection::getSize() const {
  // 16 is the header size of .plt.
  return 16 + in.plt->getNumEntries() * target->pltEntrySize;
}

bool IBTPltSection::isNeeded() const { return in.plt->getNumEntries() > 0; }

RelroPaddingSection::RelroPaddingSection()
    : SyntheticSection(SHF_ALLOC | SHF_WRITE, SHT_NOBITS, 1, ".relro_padding") {
}

// The string hash function for .gdb_index.
namespace {
uint32_t computeGdbHash(StringRef s) {
  uint32_t h = 0;
  for (uint8_t c : s)
    h = h * 67 + toLower(c) - 113;
  return h;
}
} // namespace

GdbIndexSection::GdbIndexSection()
    : SyntheticSection(0, SHT_PROGBITS, 1, ".gdb_index") {}

// Returns the desired size of an on-disk hash table for a .gdb_index section.
// There's a tradeoff between size and collision rate. We aim 75% utilization.
size_t GdbIndexSection::computeSymtabSize() const {
  return std::max<size_t>(NextPowerOf2(symbols.size() * 4 / 3), 1024);
}

namespace {
SmallVector<GdbIndexSection::CuEntry, 0> readCuList(DWARFContext &dwarf) {
  SmallVector<GdbIndexSection::CuEntry, 0> ret;
  for (std::unique_ptr<DWARFUnit> &cu : dwarf.compile_units())
    ret.push_back({cu->getOffset(), cu->getLength() + 4});
  return ret;
}

SmallVector<GdbIndexSection::AddressEntry, 0>
readAddressAreas(DWARFContext &dwarf, InputSection *sec) {
  SmallVector<GdbIndexSection::AddressEntry, 0> ret;

  uint32_t cuIdx = 0;
  for (std::unique_ptr<DWARFUnit> &cu : dwarf.compile_units()) {
    if (Error e = cu->tryExtractDIEsIfNeeded(false)) {
      warn(toString(sec) + ": " + toString(std::move(e)));
      return {};
    }
    Expected<DWARFAddressRangesVector> ranges = cu->collectAddressRanges();
    if (!ranges) {
      warn(toString(sec) + ": " + toString(ranges.takeError()));
      return {};
    }

    ArrayRef<InputSectionBase *> sections = sec->file->getSections();
    for (DWARFAddressRange &r : *ranges) {
      if (r.SectionIndex == -1ULL)
        continue;
      // Range list with zero size has no effect.
      InputSectionBase *s = sections[r.SectionIndex];
      if (s && s != &InputSection::discarded && s->isLive())
        if (r.LowPC != r.HighPC)
          ret.push_back({cast<InputSection>(s), r.LowPC, r.HighPC, cuIdx});
    }
    ++cuIdx;
  }

  return ret;
}

template <class ELFT>
SmallVector<GdbIndexSection::NameAttrEntry, 0>
readPubNamesAndTypes(const LinkerDwarfObj<ELFT> &obj,
                     const SmallVectorImpl<GdbIndexSection::CuEntry> &cus) {
  const DwarfInputSection &pubNames = obj.getGnuPubnamesSection();
  const DwarfInputSection &pubTypes = obj.getGnuPubtypesSection();

  SmallVector<GdbIndexSection::NameAttrEntry, 0> ret;
  for (const DwarfInputSection *pub : {&pubNames, &pubTypes}) {
    DWARFDataExtractor data(obj, *pub, config->isLE, config->wordsize);
    DWARFDebugPubTable table;
    table.extract(data, /*GnuStyle=*/true, [&](Error e) {
      warn(toString(pub->sec) + ": " + toString(std::move(e)));
    });
    for (const DWARFDebugPubTable::Set &set : table.getData()) {
      // The value written into the constant pool is kind << 24 | cuIndex. As we
      // don't know how many compilation units precede this object to compute
      // cuIndex, we compute (kind << 24 | cuIndexInThisObject) instead, and add
      // the number of preceding compilation units later.
      uint32_t i = llvm::partition_point(cus,
                                         [&](GdbIndexSection::CuEntry cu) {
                                           return cu.cuOffset < set.Offset;
                                         }) -
                   cus.begin();
      for (const DWARFDebugPubTable::Entry &ent : set.Entries)
        ret.push_back({{ent.Name, computeGdbHash(ent.Name)},
                       (ent.Descriptor.toBits() << 24) | i});
    }
  }
  return ret;
}
} // namespace

// Create a list of symbols from a given list of symbol names and types
// by uniquifying them by name.
namespace {
std::pair<SmallVector<GdbIndexSection::GdbSymbol, 0>, size_t> createSymbols(
    ArrayRef<SmallVector<GdbIndexSection::NameAttrEntry, 0>> nameAttrs,
    const SmallVector<GdbIndexSection::GdbChunk, 0> &chunks) {
  using GdbSymbol = GdbIndexSection::GdbSymbol;
  using NameAttrEntry = GdbIndexSection::NameAttrEntry;

  // For each chunk, compute the number of compilation units preceding it.
  uint32_t cuIdx = 0;
  std::unique_ptr<uint32_t[]> cuIdxs(new uint32_t[chunks.size()]);
  for (uint32_t i = 0, e = chunks.size(); i != e; ++i) {
    cuIdxs[i] = cuIdx;
    cuIdx += chunks[i].compilationUnits.size();
  }

  // The number of symbols we will handle in this function is of the order
  // of millions for very large executables, so we use multi-threading to
  // speed it up.
  constexpr size_t numShards = 32;
  const size_t concurrency =
      llvm::bit_floor(std::min<size_t>(config->threadCount, numShards));

  // A sharded map to uniquify symbols by name.
  auto map =
      std::make_unique<DenseMap<CachedHashStringRef, size_t>[]>(numShards);
  size_t shift = 32 - llvm::countr_zero(numShards);

  // Instantiate GdbSymbols while uniqufying them by name.
  auto symbols = std::make_unique<SmallVector<GdbSymbol, 0>[]>(numShards);

  parallelFor(0, concurrency, [&](size_t threadId) {
    uint32_t i = 0;
    for (ArrayRef<NameAttrEntry> entries : nameAttrs) {
      for (const NameAttrEntry &ent : entries) {
        size_t shardId = ent.name.hash() >> shift;
        if ((shardId & (concurrency - 1)) != threadId)
          continue;

        uint32_t v = ent.cuIndexAndAttrs + cuIdxs[i];
        size_t &idx = map[shardId][ent.name];
        if (idx) {
          symbols[shardId][idx - 1].cuVector.push_back(v);
          continue;
        }

        idx = symbols[shardId].size() + 1;
        symbols[shardId].push_back({ent.name, {v}, 0, 0});
      }
      ++i;
    }
  });

  size_t numSymbols = 0;
  for (ArrayRef<GdbSymbol> v : ArrayRef(symbols.get(), numShards))
    numSymbols += v.size();

  // The return type is a flattened vector, so we'll copy each vector
  // contents to Ret.
  SmallVector<GdbSymbol, 0> ret;
  ret.reserve(numSymbols);
  for (SmallVector<GdbSymbol, 0> &vec :
       MutableArrayRef(symbols.get(), numShards))
    for (GdbSymbol &sym : vec)
      ret.push_back(std::move(sym));

  // CU vectors and symbol names are adjacent in the output file.
  // We can compute their offsets in the output file now.
  size_t off = 0;
  for (GdbSymbol &sym : ret) {
    sym.cuVectorOff = off;
    off += (sym.cuVector.size() + 1) * 4;
  }
  for (GdbSymbol &sym : ret) {
    sym.nameOff = off;
    off += sym.name.size() + 1;
  }
  // If off overflows, the last symbol's nameOff likely overflows.
  if (!isUInt<32>(off))
    errorOrWarn("--gdb-index: constant pool size (" + Twine(off) +
                ") exceeds UINT32_MAX");

  return {ret, off};
}
} // namespace

// Returns a newly-created .gdb_index section.
template <class ELFT> GdbIndexSection *GdbIndexSection::create() {
  llvm::TimeTraceScope timeScope("Create gdb index");

  // Collect InputFiles with .debug_info.  See LinkerDwarfObj's constructor;
  // future lightweight parsing should parallelise the isec->data()
  // decompression path.
  SetVector<InputFile *> files;
  for (InputSectionBase *s : ctx.inputSections) {
    InputSection *isec = dyn_cast<InputSection>(s);
    if (!isec)
      continue;
    // .debug_gnu_pub{names,types} are useless in executables.
    // They are present in input object files solely for creating
    // a .gdb_index. So we can remove them from the output.
    if (s->name == ".debug_gnu_pubnames" || s->name == ".debug_gnu_pubtypes")
      s->markDead();
    else if (isec->name == ".debug_info")
      files.insert(isec->file);
  }
  // Drop .rel[a].debug_gnu_pub{names,types} for --emit-relocs.
  llvm::erase_if(ctx.inputSections, [](InputSectionBase *s) {
    if (auto *isec = dyn_cast<InputSection>(s))
      if (InputSectionBase *rel = isec->getRelocatedSection())
        return !rel->isLive();
    return !s->isLive();
  });

  SmallVector<GdbChunk, 0> chunks(files.size());
  SmallVector<SmallVector<NameAttrEntry, 0>, 0> nameAttrs(files.size());

  parallelFor(0, files.size(), [&](size_t i) {
    // To keep memory usage low, we don't want to keep cached DWARFContext, so
    // avoid getDwarf() here.
    ObjFile<ELFT> *file = cast<ObjFile<ELFT>>(files[i]);
    DWARFContext dwarf(std::make_unique<LinkerDwarfObj<ELFT>>(file));
    auto &dobj = static_cast<const LinkerDwarfObj<ELFT> &>(dwarf.getDWARFObj());

    // If the are multiple compile units .debug_info (very rare ld -r --unique),
    // this only picks the last one. Other address ranges are lost.
    chunks[i].sec = dobj.getInfoSection();
    chunks[i].compilationUnits = readCuList(dwarf);
    chunks[i].addressAreas = readAddressAreas(dwarf, chunks[i].sec);
    nameAttrs[i] = readPubNamesAndTypes<ELFT>(dobj, chunks[i].compilationUnits);
  });

  auto *ret = make<GdbIndexSection>();
  ret->chunks = std::move(chunks);
  std::tie(ret->symbols, ret->size) = createSymbols(nameAttrs, ret->chunks);

  // Count the areas other than the constant pool.
  ret->size += sizeof(GdbIndexHeader) + ret->computeSymtabSize() * 8;
  for (GdbChunk &chunk : ret->chunks)
    ret->size +=
        chunk.compilationUnits.size() * 16 + chunk.addressAreas.size() * 20;

  return ret;
}

void GdbIndexSection::writeTo(uint8_t *buf) {
  // Write the header.
  auto *hdr = reinterpret_cast<GdbIndexHeader *>(buf);
  uint8_t *start = buf;
  hdr->version = 7;
  buf += sizeof(*hdr);

  // Write the CU list.
  hdr->cuListOff = buf - start;
  for (GdbChunk &chunk : chunks) {
    for (CuEntry &cu : chunk.compilationUnits) {
      write64le(buf, chunk.sec->outSecOff + cu.cuOffset);
      write64le(buf + 8, cu.cuLength);
      buf += 16;
    }
  }

  // Write the address area.
  hdr->cuTypesOff = buf - start;
  hdr->addressAreaOff = buf - start;
  uint32_t cuOff = 0;
  for (GdbChunk &chunk : chunks) {
    for (AddressEntry &e : chunk.addressAreas) {
      // In the case of ICF there may be duplicate address range entries.
      const uint64_t baseAddr = e.section->repl->getVA(0);
      write64le(buf, baseAddr + e.lowAddress);
      write64le(buf + 8, baseAddr + e.highAddress);
      write32le(buf + 16, e.cuIndex + cuOff);
      buf += 20;
    }
    cuOff += chunk.compilationUnits.size();
  }

  // Write the on-disk open-addressing hash table containing symbols.
  hdr->symtabOff = buf - start;
  size_t symtabSize = computeSymtabSize();
  uint32_t mask = symtabSize - 1;

  for (GdbSymbol &sym : symbols) {
    uint32_t h = sym.name.hash();
    uint32_t i = h & mask;
    uint32_t step = ((h * 17) & mask) | 1;

    while (read32le(buf + i * 8))
      i = (i + step) & mask;

    write32le(buf + i * 8, sym.nameOff);
    write32le(buf + i * 8 + 4, sym.cuVectorOff);
  }

  buf += symtabSize * 8;

  // Write the string pool.
  hdr->constantPoolOff = buf - start;
  parallelForEach(symbols, [&](GdbSymbol &sym) {
    memcpy(buf + sym.nameOff, sym.name.data(), sym.name.size());
  });

  // Write the CU vectors.
  for (GdbSymbol &sym : symbols) {
    write32le(buf, sym.cuVector.size());
    buf += 4;
    for (uint32_t val : sym.cuVector) {
      write32le(buf, val);
      buf += 4;
    }
  }
}

bool GdbIndexSection::isNeeded() const { return !chunks.empty(); }

EhFrameHeader::EhFrameHeader()
    : SyntheticSection(SHF_ALLOC, SHT_PROGBITS, 4, ".eh_frame_hdr") {}

void EhFrameHeader::writeTo(uint8_t *buf) {
  // Unlike most sections, the EhFrameHeader section is written while writing
  // another section, namely EhFrameSection, which calls the write() function
  // below from its writeTo() function. This is necessary because the contents
  // of EhFrameHeader depend on the relocated contents of EhFrameSection and we
  // don't know which order the sections will be written in.
}

// .eh_frame_hdr contains a binary search table of pointers to FDEs.
// Each entry of the search table consists of two values,
// the starting PC from where FDEs covers, and the FDE's address.
// It is sorted by PC.
void EhFrameHeader::write() {
  uint8_t *buf = Out::bufferStart + getParent()->offset + outSecOff;
  using FdeData = EhFrameSection::FdeData;
  SmallVector<FdeData, 0> fdes = getPartition().ehFrame->getFdeData();

  buf[0] = 1;
  buf[1] = DW_EH_PE_pcrel | DW_EH_PE_sdata4;
  buf[2] = DW_EH_PE_udata4;
  buf[3] = DW_EH_PE_datarel | DW_EH_PE_sdata4;
  write32(buf + 4,
          getPartition().ehFrame->getParent()->addr - this->getVA() - 4);
  write32(buf + 8, fdes.size());
  buf += 12;

  for (FdeData &fde : fdes) {
    write32(buf, fde.pcRel);
    write32(buf + 4, fde.fdeVARel);
    buf += 8;
  }
}

size_t EhFrameHeader::getSize() const {
  // .eh_frame_hdr has a 12 bytes header followed by an array of FDEs.
  return 12 + getPartition().ehFrame->numFdes * 8;
}

bool EhFrameHeader::isNeeded() const {
  return isLive() && getPartition().ehFrame->isNeeded();
}

VersionDefinitionSection::VersionDefinitionSection()
    : SyntheticSection(SHF_ALLOC, SHT_GNU_verdef, sizeof(uint32_t),
                       ".gnu.version_d") {}

StringRef VersionDefinitionSection::getFileDefName() {
  if (!getPartition().name.empty())
    return getPartition().name;
  if (!config->soName.empty())
    return config->soName;
  return config->outputFile;
}

void VersionDefinitionSection::finalizeContents() {
  fileDefNameOff = getPartition().dynStrTab->addString(getFileDefName());
  for (const VersionDefinition &v : namedVersionDefs())
    verDefNameOffs.push_back(getPartition().dynStrTab->addString(v.name));

  if (OutputSection *sec = getPartition().dynStrTab->getParent())
    getParent()->link = sec->sectionIndex;

  // sh_info should be set to the number of definitions. This fact is missed in
  // documentation, but confirmed by binutils community:
  // https://sourceware.org/ml/binutils/2014-11/msg00355.html
  getParent()->info = getVerDefNum();
}

void VersionDefinitionSection::writeOne(uint8_t *buf, uint32_t index,
                                        StringRef name, size_t nameOff) {
  uint16_t flags = index == 1 ? VER_FLG_BASE : 0;

  // Write a verdef.
  write16(buf, 1);                  // vd_version
  write16(buf + 2, flags);          // vd_flags
  write16(buf + 4, index);          // vd_ndx
  write16(buf + 6, 1);              // vd_cnt
  write32(buf + 8, hashSysV(name)); // vd_hash
  write32(buf + 12, 20);            // vd_aux
  write32(buf + 16, 28);            // vd_next

  // Write a veraux.
  write32(buf + 20, nameOff); // vda_name
  write32(buf + 24, 0);       // vda_next
}

void VersionDefinitionSection::writeTo(uint8_t *buf) {
  writeOne(buf, 1, getFileDefName(), fileDefNameOff);

  auto nameOffIt = verDefNameOffs.begin();
  for (const VersionDefinition &v : namedVersionDefs()) {
    buf += EntrySize;
    writeOne(buf, v.id, v.name, *nameOffIt++);
  }

  // Need to terminate the last version definition.
  write32(buf + 16, 0); // vd_next
}

size_t VersionDefinitionSection::getSize() const {
  return EntrySize * getVerDefNum();
}

// .gnu.version is a table where each entry is 2 byte long.
VersionTableSection::VersionTableSection()
    : SyntheticSection(SHF_ALLOC, SHT_GNU_versym, sizeof(uint16_t),
                       ".gnu.version") {
  this->entsize = 2;
}

void VersionTableSection::finalizeContents() {
  // At the moment of june 2016 GNU docs does not mention that sh_link field
  // should be set, but Sun docs do. Also readelf relies on this field.
  getParent()->link = getPartition().dynSymTab->getParent()->sectionIndex;
}

size_t VersionTableSection::getSize() const {
  return (getPartition().dynSymTab->getSymbols().size() + 1) * 2;
}

void VersionTableSection::writeTo(uint8_t *buf) {
  buf += 2;
  for (const SymbolTableEntry &s : getPartition().dynSymTab->getSymbols()) {
    // For an unextracted lazy symbol (undefined weak), it must have been
    // converted to Undefined and have VER_NDX_GLOBAL version here.
    assert(!s.sym->isLazy());
    write16(buf, s.sym->versionId);
    buf += 2;
  }
}

bool VersionTableSection::isNeeded() const {
  return isLive() &&
         (getPartition().verDef || getPartition().verNeed->isNeeded());
}

void elf::addVerneed(Symbol *ss) {
  auto &file = cast<SharedFile>(*ss->file);
  if (ss->versionId == VER_NDX_GLOBAL)
    return;

  if (file.vernauxs.empty())
    file.vernauxs.resize(file.verdefs.size());

  // Select a version identifier for the vernaux data structure, if we haven't
  // already allocated one. The verdef identifiers cover the range
  // [1..getVerDefNum()]; this causes the vernaux identifiers to start from
  // getVerDefNum()+1.
  if (file.vernauxs[ss->versionId] == 0)
    file.vernauxs[ss->versionId] = ++SharedFile::vernauxNum + getVerDefNum();

  ss->versionId = file.vernauxs[ss->versionId];
}

template <class ELFT>
VersionNeedSection<ELFT>::VersionNeedSection()
    : SyntheticSection(SHF_ALLOC, SHT_GNU_verneed, sizeof(uint32_t),
                       ".gnu.version_r") {}

template <class ELFT> void VersionNeedSection<ELFT>::finalizeContents() {
  for (SharedFile *f : ctx.sharedFiles) {
    if (f->vernauxs.empty())
      continue;
    verneeds.emplace_back();
    Verneed &vn = verneeds.back();
    vn.nameStrTab = getPartition().dynStrTab->addString(f->soName);
    bool isLibc = config->relrGlibc && f->soName.starts_with("libc.so.");
    bool isGlibc2 = false;
    for (unsigned i = 0; i != f->vernauxs.size(); ++i) {
      if (f->vernauxs[i] == 0)
        continue;
      auto *verdef =
          reinterpret_cast<const typename ELFT::Verdef *>(f->verdefs[i]);
      StringRef ver(f->getStringTable().data() + verdef->getAux()->vda_name);
      if (isLibc && ver.starts_with("GLIBC_2."))
        isGlibc2 = true;
      vn.vernauxs.push_back({verdef->vd_hash, f->vernauxs[i],
                             getPartition().dynStrTab->addString(ver)});
    }
    if (isGlibc2) {
      const char *ver = "GLIBC_ABI_DT_RELR";
      vn.vernauxs.push_back({hashSysV(ver),
                             ++SharedFile::vernauxNum + getVerDefNum(),
                             getPartition().dynStrTab->addString(ver)});
    }
  }

  if (OutputSection *sec = getPartition().dynStrTab->getParent())
    getParent()->link = sec->sectionIndex;
  getParent()->info = verneeds.size();
}

template <class ELFT> void VersionNeedSection<ELFT>::writeTo(uint8_t *buf) {
  // The Elf_Verneeds need to appear first, followed by the Elf_Vernauxs.
  auto *verneed = reinterpret_cast<Elf_Verneed *>(buf);
  auto *vernaux = reinterpret_cast<Elf_Vernaux *>(verneed + verneeds.size());

  for (auto &vn : verneeds) {
    // Create an Elf_Verneed for this DSO.
    verneed->vn_version = 1;
    verneed->vn_cnt = vn.vernauxs.size();
    verneed->vn_file = vn.nameStrTab;
    verneed->vn_aux =
        reinterpret_cast<char *>(vernaux) - reinterpret_cast<char *>(verneed);
    verneed->vn_next = sizeof(Elf_Verneed);
    ++verneed;

    // Create the Elf_Vernauxs for this Elf_Verneed.
    for (auto &vna : vn.vernauxs) {
      vernaux->vna_hash = vna.hash;
      vernaux->vna_flags = 0;
      vernaux->vna_other = vna.verneedIndex;
      vernaux->vna_name = vna.nameStrTab;
      vernaux->vna_next = sizeof(Elf_Vernaux);
      ++vernaux;
    }

    vernaux[-1].vna_next = 0;
  }
  verneed[-1].vn_next = 0;
}

template <class ELFT> size_t VersionNeedSection<ELFT>::getSize() const {
  return verneeds.size() * sizeof(Elf_Verneed) +
         SharedFile::vernauxNum * sizeof(Elf_Vernaux);
}

template <class ELFT> bool VersionNeedSection<ELFT>::isNeeded() const {
  return isLive() && SharedFile::vernauxNum != 0;
}

void MergeSyntheticSection::addSection(MergeInputSection *ms) {
  ms->parent = this;
  sections.push_back(ms);
  assert(addralign == ms->addralign || !(ms->flags & SHF_STRINGS));
  addralign = std::max(addralign, ms->addralign);
}

MergeTailSection::MergeTailSection(StringRef name, uint32_t type,
                                   uint64_t flags, uint32_t alignment)
    : MergeSyntheticSection(name, type, flags, alignment),
      builder(StringTableBuilder::RAW, llvm::Align(alignment)) {}

size_t MergeTailSection::getSize() const { return builder.getSize(); }

void MergeTailSection::writeTo(uint8_t *buf) { builder.write(buf); }

void MergeTailSection::finalizeContents() {
  // Add all string pieces to the string table builder to create section
  // contents.
  for (MergeInputSection *sec : sections)
    for (size_t i = 0, e = sec->pieces.size(); i != e; ++i)
      if (sec->pieces[i].live)
        builder.add(sec->getData(i));

  // Fix the string table content. After this, the contents will never change.
  builder.finalize();

  // finalize() fixed tail-optimized strings, so we can now get
  // offsets of strings. Get an offset for each string and save it
  // to a corresponding SectionPiece for easy access.
  for (MergeInputSection *sec : sections)
    for (size_t i = 0, e = sec->pieces.size(); i != e; ++i)
      if (sec->pieces[i].live)
        sec->pieces[i].outputOff = builder.getOffset(sec->getData(i));
}

void MergeNoTailSection::writeTo(uint8_t *buf) {
  parallelFor(0, numShards,
              [&](size_t i) { shards[i].write(buf + shardOffsets[i]); });
}

// This function is very hot (i.e. it can take several seconds to finish)
// because sometimes the number of inputs is in an order of magnitude of
// millions. So, we use multi-threading.
//
// For any strings S and T, we know S is not mergeable with T if S's hash
// value is different from T's. If that's the case, we can safely put S and
// T into different string builders without worrying about merge misses.
// We do it in parallel.
void MergeNoTailSection::finalizeContents() {
  // Initializes string table builders.
  for (size_t i = 0; i < numShards; ++i)
    shards.emplace_back(StringTableBuilder::RAW, llvm::Align(addralign));

  // Concurrency level. Must be a power of 2 to avoid expensive modulo
  // operations in the following tight loop.
  const size_t concurrency =
      llvm::bit_floor(std::min<size_t>(config->threadCount, numShards));

  // Add section pieces to the builders.
  parallelFor(0, concurrency, [&](size_t threadId) {
    for (MergeInputSection *sec : sections) {
      for (size_t i = 0, e = sec->pieces.size(); i != e; ++i) {
        if (!sec->pieces[i].live)
          continue;
        size_t shardId = getShardId(sec->pieces[i].hash);
        if ((shardId & (concurrency - 1)) == threadId)
          sec->pieces[i].outputOff = shards[shardId].add(sec->getData(i));
      }
    }
  });

  size_t off = 0;
  for (size_t i = 0; i < numShards; ++i) {
    shards[i].finalizeInOrder();
    if (shards[i].getSize() > 0)
      off = alignToPowerOf2(off, addralign);
    shardOffsets[i] = off;
    off += shards[i].getSize();
  }
  size = off;

  // So far, section pieces have offsets from beginning of shards, but
  // we want offsets from beginning of the whole section. Fix them.
  parallelForEach(sections, [&](MergeInputSection *sec) {
    for (size_t i = 0, e = sec->pieces.size(); i != e; ++i)
      if (sec->pieces[i].live)
        sec->pieces[i].outputOff +=
            shardOffsets[getShardId(sec->pieces[i].hash)];
  });
}

template <class ELFT> void elf::splitSections() {
  llvm::TimeTraceScope timeScope("Split sections");
  // splitIntoPieces needs to be called on each MergeInputSection
  // before calling finalizeContents().
  parallelForEach(ctx.objectFiles, [](ELFFileBase *file) {
    for (InputSectionBase *sec : file->getSections()) {
      if (!sec)
        continue;
      if (auto *s = dyn_cast<MergeInputSection>(sec))
        s->splitIntoPieces();
      else if (auto *eh = dyn_cast<EhInputSection>(sec))
        eh->split<ELFT>();
    }
  });
}

void elf::combineEhSections() {
  llvm::TimeTraceScope timeScope("Combine EH sections");
  for (EhInputSection *sec : ctx.ehInputSections) {
    EhFrameSection &eh = *sec->getPartition().ehFrame;
    sec->parent = &eh;
    eh.addralign = std::max(eh.addralign, sec->addralign);
    eh.sections.push_back(sec);
    llvm::append_range(eh.dependentSections, sec->dependentSections);
  }
}

ThunkSection::ThunkSection(OutputSection *os, uint64_t off)
    : SyntheticSection(SHF_ALLOC | SHF_EXECINSTR, SHT_PROGBITS, 4,
                       ".text.thunk") {
  this->parent = os;
  this->outSecOff = off;
}

size_t ThunkSection::getSize() const {
  if (roundUpSizeForErrata)
    return alignTo(size, 4096);
  return size;
}

void ThunkSection::addThunk(Thunk *t) {
  thunks.push_back(t);
  t->addSymbols(*this);
}

void ThunkSection::writeTo(uint8_t *buf) {
  for (Thunk *t : thunks)
    t->writeTo(buf + t->offset);
}

InputSection *ThunkSection::getTargetInputSection() const {
  if (thunks.empty())
    return nullptr;
  const Thunk *t = thunks.front();
  return t->getTargetInputSection();
}

bool ThunkSection::assignOffsets() {
  uint64_t off = 0;
  for (Thunk *t : thunks) {
    off = alignToPowerOf2(off, t->alignment);
    t->setOffset(off);
    uint32_t size = t->size();
    t->getThunkTargetSym()->size = size;
    off += size;
  }
  bool changed = off != size;
  size = off;
  return changed;
}

namespace {
uint8_t getAbiVersion() { return 0; }
} // namespace

template <typename ELFT> void elf::writeEhdr(uint8_t *buf, Partition &part) {
  memcpy(buf, "\177ELF", 4);

  auto *eHdr = reinterpret_cast<typename ELFT::Ehdr *>(buf);
  eHdr->e_ident[EI_CLASS] = ELFCLASS64;
  eHdr->e_ident[EI_DATA] = config->isLE ? ELFDATA2LSB : ELFDATA2MSB;
  eHdr->e_ident[EI_VERSION] = EV_CURRENT;
  eHdr->e_ident[EI_OSABI] = config->osabi;
  eHdr->e_ident[EI_ABIVERSION] = getAbiVersion();
  eHdr->e_machine = config->emachine;
  eHdr->e_version = EV_CURRENT;
  eHdr->e_flags = config->eflags;
  eHdr->e_ehsize = sizeof(typename ELFT::Ehdr);
  eHdr->e_phnum = part.phdrs.size();
  eHdr->e_shentsize = sizeof(typename ELFT::Shdr);

  eHdr->e_phoff = sizeof(typename ELFT::Ehdr);
  eHdr->e_phentsize = sizeof(typename ELFT::Phdr);
}

template <typename ELFT> void elf::writePhdrs(uint8_t *buf, Partition &part) {
  // Write the program header table.
  auto *hBuf = reinterpret_cast<typename ELFT::Phdr *>(buf);
  for (PhdrEntry *p : part.phdrs) {
    hBuf->p_type = p->p_type;
    hBuf->p_flags = p->p_flags;
    hBuf->p_offset = p->p_offset;
    hBuf->p_vaddr = p->p_vaddr;
    hBuf->p_paddr = p->p_paddr;
    hBuf->p_filesz = p->p_filesz;
    hBuf->p_memsz = p->p_memsz;
    hBuf->p_align = p->p_align;
    ++hBuf;
  }
}

template <typename ELFT>
PartitionElfHeaderSection<ELFT>::PartitionElfHeaderSection()
    : SyntheticSection(SHF_ALLOC, SHT_LLVM_PART_EHDR, 1, "") {}

template <typename ELFT>
size_t PartitionElfHeaderSection<ELFT>::getSize() const {
  return sizeof(typename ELFT::Ehdr);
}

template <typename ELFT>
void PartitionElfHeaderSection<ELFT>::writeTo(uint8_t *buf) {
  writeEhdr<ELFT>(buf, getPartition());

  // Loadable partitions are always ET_DYN.
  auto *eHdr = reinterpret_cast<typename ELFT::Ehdr *>(buf);
  eHdr->e_type = ET_DYN;
}

template <typename ELFT>
PartitionProgramHeadersSection<ELFT>::PartitionProgramHeadersSection()
    : SyntheticSection(SHF_ALLOC, SHT_LLVM_PART_PHDR, 1, ".phdrs") {}

template <typename ELFT>
size_t PartitionProgramHeadersSection<ELFT>::getSize() const {
  return sizeof(typename ELFT::Phdr) * getPartition().phdrs.size();
}

template <typename ELFT>
void PartitionProgramHeadersSection<ELFT>::writeTo(uint8_t *buf) {
  writePhdrs<ELFT>(buf, getPartition());
}

PartitionIndexSection::PartitionIndexSection()
    : SyntheticSection(SHF_ALLOC, SHT_PROGBITS, 4, ".rodata") {}

size_t PartitionIndexSection::getSize() const {
  return 12 * (partitions.size() - 1);
}

void PartitionIndexSection::finalizeContents() {
  for (size_t i = 1; i != partitions.size(); ++i)
    partitions[i].nameStrTab =
        mainPart->dynStrTab->addString(partitions[i].name);
}

void PartitionIndexSection::writeTo(uint8_t *buf) {
  uint64_t va = getVA();
  for (size_t i = 1; i != partitions.size(); ++i) {
    write32(buf, mainPart->dynStrTab->getVA() + partitions[i].nameStrTab - va);
    write32(buf + 4, partitions[i].elfHeader->getVA() - (va + 4));

    SyntheticSection *next = i == partitions.size() - 1
                                 ? in.partEnd.get()
                                 : partitions[i + 1].elfHeader.get();
    write32(buf + 8, next->getVA() - partitions[i].elfHeader->getVA());

    va += 12;
    buf += 12;
  }
}

void InStruct::reset() {
  attributes.reset();
  bss.reset();
  bssRelRo.reset();
  got.reset();
  gotPlt.reset();
  igotPlt.reset();
  relroPadding.reset();
  partEnd.reset();
  partIndex.reset();
  plt.reset();
  iplt.reset();
  ibtPlt.reset();
  relaPlt.reset();
  relaIplt.reset();
  shStrTab.reset();
  strTab.reset();
  symTab.reset();
  symTabShndx.reset();
}

constexpr char kMemtagAndroidNoteName[] = "Android";
void MemtagAndroidNote::writeTo(uint8_t *buf) {
  static_assert(
      sizeof(kMemtagAndroidNoteName) == 8,
      "Android 11 & 12 have an ABI that the note name is 8 bytes long. Keep it "
      "that way for backwards compatibility.");

  write32(buf, sizeof(kMemtagAndroidNoteName));
  write32(buf + 4, sizeof(uint32_t));
  write32(buf + 8, ELF::NT_ANDROID_TYPE_MEMTAG);
  memcpy(buf + 12, kMemtagAndroidNoteName, sizeof(kMemtagAndroidNoteName));
  buf += 12 + alignTo(sizeof(kMemtagAndroidNoteName), 4);

  uint32_t value = 0;
  value |= config->androidMemtagMode;
  if (config->androidMemtagHeap)
    value |= ELF::NT_MEMTAG_HEAP;
  // Note, MTE stack is an ABI break. Attempting to run an MTE stack-enabled
  // binary on Android 11 or 12 will result in a checkfail in the loader.
  if (config->androidMemtagStack)
    value |= ELF::NT_MEMTAG_STACK;
  write32(buf, value); // note value
}

size_t MemtagAndroidNote::getSize() const {
  return sizeof(llvm::ELF::Elf64_Nhdr) +
         /*namesz=*/alignTo(sizeof(kMemtagAndroidNoteName), 4) +
         /*descsz=*/sizeof(uint32_t);
}

void PackageMetadataNote::writeTo(uint8_t *buf) {
  write32(buf, 4);
  write32(buf + 4, config->packageMetadata.size() + 1);
  write32(buf + 8, FDO_PACKAGING_METADATA);
  memcpy(buf + 12, "FDO", 4);
  memcpy(buf + 16, config->packageMetadata.data(),
         config->packageMetadata.size());
}

size_t PackageMetadataNote::getSize() const {
  return sizeof(llvm::ELF::Elf64_Nhdr) + 4 +
         alignTo(config->packageMetadata.size() + 1, 4);
}

// Helper function, return the size of the ULEB128 for 'v', optionally writing
// it to `*(buf + offset)` if `buf` is non-null.
namespace {
size_t computeOrWriteULEB128(uint64_t v, uint8_t *buf, size_t offset) {
  if (buf)
    return encodeULEB128(v, buf + offset);
  return getULEB128Size(v);
}
} // namespace

// https://github.com/ARM-software/abi-aa/blob/main/memtagabielf64/memtagabielf64.rst#83encoding-of-sht_aarch64_memtag_globals_dynamic
constexpr uint64_t kMemtagStepSizeBits = 3;
constexpr uint64_t kMemtagGranuleSize = 16;
namespace {
size_t createMemtagDescriptors(const SmallVector<const Symbol *, 0> &symbols,
                               uint8_t *buf = nullptr) {
  size_t sectionSize = 0;
  uint64_t lastGlobalEnd = 0;

  for (const Symbol *sym : symbols) {
    if (!includeInSymtab(*sym))
      continue;
    const uint64_t addr = sym->getVA();
    const uint64_t size = sym->getSize();

    if (addr <= kMemtagGranuleSize && buf != nullptr)
      errorOrWarn("address of the tagged symbol \"" + sym->getName() +
                  "\" falls in the ELF header. This is indicative of a "
                  "compiler/linker bug");
    if (addr % kMemtagGranuleSize != 0)
      errorOrWarn("address of the tagged symbol \"" + sym->getName() +
                  "\" at 0x" + Twine::utohexstr(addr) +
                  "\" is not granule (16-byte) aligned");
    if (size == 0)
      errorOrWarn("size of the tagged symbol \"" + sym->getName() +
                  "\" is not allowed to be zero");
    if (size % kMemtagGranuleSize != 0)
      errorOrWarn("size of the tagged symbol \"" + sym->getName() +
                  "\" (size 0x" + Twine::utohexstr(size) +
                  ") is not granule (16-byte) aligned");

    const uint64_t sizeToEncode = size / kMemtagGranuleSize;
    const uint64_t stepToEncode = ((addr - lastGlobalEnd) / kMemtagGranuleSize)
                                  << kMemtagStepSizeBits;
    if (sizeToEncode < (1 << kMemtagStepSizeBits)) {
      sectionSize +=
          computeOrWriteULEB128(stepToEncode | sizeToEncode, buf, sectionSize);
    } else {
      sectionSize += computeOrWriteULEB128(stepToEncode, buf, sectionSize);
      sectionSize += computeOrWriteULEB128(sizeToEncode - 1, buf, sectionSize);
    }
    lastGlobalEnd = addr + size;
  }

  return sectionSize;
}
} // namespace

bool MemtagDescriptors::updateAllocSize() {
  size_t oldSize = getSize();
  std::stable_sort(symbols.begin(), symbols.end(),
                   [](const Symbol *s1, const Symbol *s2) {
                     return s1->getVA() < s2->getVA();
                   });
  return oldSize != getSize();
}

void MemtagDescriptors::writeTo(uint8_t *buf) {
  createMemtagDescriptors(symbols, buf);
}

size_t MemtagDescriptors::getSize() const {
  return createMemtagDescriptors(symbols);
}

InStruct elf::in;

std::vector<Partition> elf::partitions;
Partition *elf::mainPart;

template GdbIndexSection *GdbIndexSection::create<ELF64LE>();
template GdbIndexSection *GdbIndexSection::create<ELF64BE>();

template void elf::splitSections<ELF64LE>();
template void elf::splitSections<ELF64BE>();

template void EhFrameSection::iterateFDEWithLSDA<ELF64LE>(
    function_ref<void(InputSection &)>);
template void EhFrameSection::iterateFDEWithLSDA<ELF64BE>(
    function_ref<void(InputSection &)>);

template class elf::DynamicSection<ELF64LE>;
template class elf::DynamicSection<ELF64BE>;

template class elf::RelocationSection<ELF64LE>;
template class elf::RelocationSection<ELF64BE>;

template class elf::AndroidPackedRelocationSection<ELF64LE>;
template class elf::AndroidPackedRelocationSection<ELF64BE>;

template class elf::RelrSection<ELF64LE>;
template class elf::RelrSection<ELF64BE>;

template class elf::SymbolTableSection<ELF64LE>;
template class elf::SymbolTableSection<ELF64BE>;

template class elf::VersionNeedSection<ELF64LE>;
template class elf::VersionNeedSection<ELF64BE>;

template void elf::writeEhdr<ELF64LE>(uint8_t *Buf, Partition &Part);
template void elf::writeEhdr<ELF64BE>(uint8_t *Buf, Partition &Part);

template void elf::writePhdrs<ELF64LE>(uint8_t *Buf, Partition &Part);
template void elf::writePhdrs<ELF64BE>(uint8_t *Buf, Partition &Part);

template class elf::PartitionElfHeaderSection<ELF64LE>;
template class elf::PartitionElfHeaderSection<ELF64BE>;

template class elf::PartitionProgramHeadersSection<ELF64LE>;
template class elf::PartitionProgramHeadersSection<ELF64BE>;
