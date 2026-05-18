#include "Linker/ELF/InputSection.h"
#include "Linker/Core/Runtime/Session.h"
#include "Linker/ELF/Config.h"
#include "Linker/ELF/InputFiles.h"
#include "Linker/ELF/OutputSections.h"
#include "Linker/ELF/Relocations.h"
#include "Linker/ELF/SymbolTable.h"
#include "Linker/ELF/Symbols.h"
#include "Linker/ELF/SyntheticSections.h"
#include "Linker/ELF/Target.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/xxhash.h"
#include <algorithm>
#include <mutex>
#include <optional>
#include <vector>

using namespace llvm;
using namespace llvm::ELF;
using namespace llvm::object;
using namespace llvm::support;
using namespace llvm::support::endian;
using namespace llvm::sys;
using namespace linker;
using namespace linker::elf;

// Returns a string to construct an error message.
// ===----------------------------------------------------------------------===
// Construction & initialization
// ===----------------------------------------------------------------------===

std::string linker::toString(const InputSectionBase *sec) {
  return (toString(sec->file) + ":(" + sec->name + ")").str();
}

namespace {
template <class ELFT>
ArrayRef<uint8_t> getSectionContents(ObjFile<ELFT> &file,
                                     const typename ELFT::Shdr &hdr) {
  if (hdr.sh_type == SHT_NOBITS)
    return ArrayRef<uint8_t>(nullptr, hdr.sh_size);
  return check(file.getObj().getSectionContents(hdr));
}
} // namespace

InputSectionBase::InputSectionBase(InputFile *file, uint64_t flags,
                                   uint32_t type, uint64_t entsize,
                                   uint32_t link, uint32_t info,
                                   uint32_t addralign, ArrayRef<uint8_t> data,
                                   StringRef name, Kind sectionKind)
    : SectionBase(sectionKind, name, flags, entsize, addralign, type, info,
                  link),
      file(file), content_(data.data()), size(data.size()) {
  // In order to reduce memory allocation, we assume that mergeable
  // sections are smaller than 4 GiB, which is not an unreasonable
  // assumption as of 2017.
  if (sectionKind == SectionBase::Merge && content().size() > UINT32_MAX)
    error(toString(this) + ": section too large");

  // The ELF spec states that a value of 0 means the section has
  // no alignment constraints.
  uint32_t v = std::max<uint32_t>(addralign, 1);
  if (!isPowerOf2_64(v))
    fatal(toString(this) + ": sh_addralign is not a power of 2");
  this->addralign = v;

  // If SHF_COMPRESSED is set, parse the header. The legacy .zdebug format is no
  // longer supported.
  if (flags & SHF_COMPRESSED)
    dispatchByFormat(parseCompressedHeader, );
}

// Drop SHF_GROUP bit unless we are producing a re-linkable object file.
// SHF_GROUP is a marker that a section belongs to some comdat group.
// That flag doesn't make sense in an executable.
namespace {
uint64_t getFlags(uint64_t flags) {
  flags &= ~(uint64_t)SHF_INFO_LINK;
  if (!config->relocatable)
    flags &= ~(uint64_t)SHF_GROUP;
  return flags;
}
} // namespace

template <class ELFT>
InputSectionBase::InputSectionBase(ObjFile<ELFT> &file,
                                   const typename ELFT::Shdr &hdr,
                                   StringRef name, Kind sectionKind)
    : InputSectionBase(&file, getFlags(hdr.sh_flags), hdr.sh_type,
                       hdr.sh_entsize, hdr.sh_link, hdr.sh_info,
                       hdr.sh_addralign, getSectionContents(file, hdr), name,
                       sectionKind) {
  // We reject object files having insanely large alignments even though
  // they are allowed by the spec. I think 4GB is a reasonable limitation.
  // We might want to relax this in the future.
  if (hdr.sh_addralign > UINT32_MAX)
    fatal(toString(&file) + ": section sh_addralign is too large");
}

size_t InputSectionBase::getSize() const {
  if (auto *s = dyn_cast<SyntheticSection>(this))
    return s->getSize();
  return size - bytesDropped;
}

namespace {
template <class ELFT>
void decompressAux(const InputSectionBase &sec, uint8_t *out, size_t size) {
  auto *hdr = reinterpret_cast<const typename ELFT::Chdr *>(sec.content_);
  auto compressed = ArrayRef<uint8_t>(sec.content_, sec.compressedSize)
                        .slice(sizeof(typename ELFT::Chdr));
  if (Error e = hdr->ch_type == ELFCOMPRESS_ZLIB
                    ? compression::zlib::decompress(compressed, out, size)
                    : compression::zstd::decompress(compressed, out, size))
    fatal(toString(&sec) +
          ": decompress failed: " + llvm::toString(std::move(e)));
}
} // namespace

void InputSectionBase::decompress() const {
  uint8_t *uncompressedBuf;
  {
    static std::mutex mu;
    std::lock_guard<std::mutex> lock(mu);
    uncompressedBuf = bAlloc().Allocate<uint8_t>(size);
  }

  dispatchByFormat(decompressAux, *this, uncompressedBuf, size);
  content_ = uncompressedBuf;
  compressed = false;
}

template <class ELFT> RelsOrRelas<ELFT> InputSectionBase::relsOrRelas() const {
  if (relSecIdx == 0)
    return {};
  RelsOrRelas<ELFT> ret;
  typename ELFT::Shdr shdr =
      cast<ELFFileBase>(file)->getELFShdrs<ELFT>()[relSecIdx];
  if (shdr.sh_type == SHT_REL) {
    ret.rels = ArrayRef(reinterpret_cast<const typename ELFT::Rel *>(
                            file->mb.getBufferStart() + shdr.sh_offset),
                        shdr.sh_size / sizeof(typename ELFT::Rel));
  } else {
    assert(shdr.sh_type == SHT_RELA);
    ret.relas = ArrayRef(reinterpret_cast<const typename ELFT::Rela *>(
                             file->mb.getBufferStart() + shdr.sh_offset),
                         shdr.sh_size / sizeof(typename ELFT::Rela));
  }
  return ret;
}

uint64_t SectionBase::getOffset(uint64_t offset) const {
  switch (kind()) {
  case Output: {
    auto *os = cast<OutputSection>(this);
    // For output sections we treat offset -1 as the end of the section.
    return offset == uint64_t(-1) ? os->size : offset;
  }
  case Regular:
  case Synthetic:
    return cast<InputSection>(this)->outSecOff + offset;
  case EHFrame: {
    // Two code paths may reach here. First, compiler-rt crtbegin.o and GCC
    // crtbeginT.o may reference the start of an empty .eh_frame to identify the
    // start of the output .eh_frame. Just return offset.
    //
    // Second, InputSection::copyRelocations on .eh_frame. Some pieces may be
    // discarded due to GC/ICF. We should compute the output section offset.
    const EhInputSection *es = cast<EhInputSection>(this);
    if (!es->content().empty())
      if (InputSection *isec = es->getParent())
        return isec->outSecOff + es->getParentOffset(offset);
    return offset;
  }
  case Merge:
    const MergeInputSection *ms = cast<MergeInputSection>(this);
    if (InputSection *isec = ms->getParent())
      return isec->outSecOff + ms->getParentOffset(offset);
    return ms->getParentOffset(offset);
  }
  llvm_unreachable("invalid section kind");
}

uint64_t SectionBase::getVA(uint64_t offset) const {
  const OutputSection *out = getOutputSection();
  return (out ? out->addr : 0) + getOffset(offset);
}

OutputSection *SectionBase::getOutputSection() {
  InputSection *sec;
  if (auto *isec = dyn_cast<InputSection>(this))
    sec = isec;
  else if (auto *ms = dyn_cast<MergeInputSection>(this))
    sec = ms->getParent();
  else if (auto *eh = dyn_cast<EhInputSection>(this))
    sec = eh->getParent();
  else
    return cast<OutputSection>(this);
  return sec ? sec->getParent() : nullptr;
}

// When a section is compressed, `rawData` consists with a header followed
// by zlib-compressed data. This function parses a header to initialize
// `uncompressedSize` member and remove the header from `rawData`.
template <typename ELFT> void InputSectionBase::parseCompressedHeader() {
  flags &= ~(uint64_t)SHF_COMPRESSED;

  // New-style header
  if (content().size() < sizeof(typename ELFT::Chdr)) {
    error(toString(this) + ": corrupted compressed section");
    return;
  }

  auto *hdr = reinterpret_cast<const typename ELFT::Chdr *>(content().data());
  if (hdr->ch_type == ELFCOMPRESS_ZLIB) {
    if (!compression::zlib::isAvailable())
      error(toString(this) + " is compressed with ELFCOMPRESS_ZLIB, but the "
                             "linker is not built with zlib support");
  } else if (hdr->ch_type == ELFCOMPRESS_ZSTD) {
    if (!compression::zstd::isAvailable())
      error(toString(this) + " is compressed with ELFCOMPRESS_ZSTD, but the "
                             "linker is not built with zstd support");
  } else {
    error(toString(this) + ": unsupported compression type (" +
          Twine(hdr->ch_type) + ")");
    return;
  }

  compressed = true;
  compressedSize = size;
  size = hdr->ch_size;
  addralign = std::max<uint32_t>(hdr->ch_addralign, 1);
}

InputSection *InputSectionBase::getLinkOrderDep() const {
  assert(flags & SHF_LINK_ORDER);
  if (!link)
    return nullptr;
  return cast<InputSection>(file->getSections()[link]);
}

// Find a symbol that encloses a given location.
Defined *InputSectionBase::getEnclosingSymbol(uint64_t offset,
                                              uint8_t type) const {
  for (Symbol *b : file->getSymbols())
    if (Defined *d = dyn_cast<Defined>(b))
      if (d->section == this && d->value <= offset &&
          offset < d->value + d->size && (type == 0 || type == d->type))
        return d;
  return nullptr;
}

// Returns an object file location string. Used to construct an error message.
std::string InputSectionBase::getLocation(uint64_t offset) const {
  std::string secAndOffset =
      (name + "+0x" + Twine::utohexstr(offset) + ")").str();

  // We don't have file for synthetic sections.
  if (file == nullptr)
    return (config->outputFile + ":(" + secAndOffset).str();

  std::string filename = toString(file);
  if (Defined *d = getEnclosingFunction(offset))
    return filename + ":(function " + toString(*d) + ": " + secAndOffset;

  return filename + ":(" + secAndOffset;
}

// This function is intended to be used for constructing an error message.
// The returned message looks like this:
//
//   foo.c:42 (/home/alice/possibly/very/long/path/foo.c:42)
//
//  Returns an empty string if there's no way to get line info.
std::string InputSectionBase::getSrcMsg(const Symbol &sym,
                                        uint64_t offset) const {
  return file->getSrcMsg(sym, *this, offset);
}

// Returns a filename string along with an optional section name. This
// function is intended to be used for constructing an error
// message. The returned message looks like this:
//
//   path/to/foo.o:(function bar)
//
// or
//
//   path/to/foo.o:(function bar) in archive path/to/bar.a
std::string InputSectionBase::getObjMsg(uint64_t off) const {
  std::string filename = std::string(file->getName());

  std::string archive;
  if (!file->archiveName.empty())
    archive = (" in archive " + file->archiveName).str();

  // Find a symbol that encloses a given location. getObjMsg may be called
  // before ObjFile::prepareSectionsAndLocals where local symbols are
  // initialized.
  if (Defined *d = getEnclosingSymbol(off))
    return filename + ":(" + toString(*d) + ")" + archive;

  // If there's no symbol, print out the offset in the section.
  return (filename + ":(" + name + "+0x" + utohexstr(off) + ")" + archive)
      .str();
}

InputSection InputSection::discarded(nullptr, 0, 0, 0, ArrayRef<uint8_t>(), "");

InputSection::InputSection(InputFile *f, uint64_t flags, uint32_t type,
                           uint32_t addralign, ArrayRef<uint8_t> data,
                           StringRef name, Kind k)
    : InputSectionBase(f, flags, type,
                       /*Entsize*/ 0, /*Link*/ 0, /*Info*/ 0, addralign, data,
                       name, k) {}

template <class ELFT>
InputSection::InputSection(ObjFile<ELFT> &f, const typename ELFT::Shdr &header,
                           StringRef name)
    : InputSectionBase(f, header, name, InputSectionBase::Regular) {}

// Copy SHT_GROUP section contents. Used only for the -r option.
template <class ELFT> void InputSection::copyShtGroup(uint8_t *buf) {
  // ELFT::Word is the 32-bit integral type in the target endianness.
  using u32 = typename ELFT::Word;
  ArrayRef<u32> from = getDataAs<u32>();
  auto *to = reinterpret_cast<u32 *>(buf);

  // The first entry is not a section number but a flag.
  *to++ = from[0];

  // Adjust section numbers because section numbers in an input object files are
  // different in the output. We also need to handle combined or discarded
  // members.
  ArrayRef<InputSectionBase *> sections = file->getSections();
  DenseSet<uint32_t> seen;
  for (uint32_t idx : from.slice(1)) {
    OutputSection *osec = sections[idx]->getOutputSection();
    if (osec && seen.insert(osec->sectionIndex).second)
      *to++ = osec->sectionIndex;
  }
}

InputSectionBase *InputSection::getRelocatedSection() const {
  if (!file || (type != SHT_RELA && type != SHT_REL))
    return nullptr;
  ArrayRef<InputSectionBase *> sections = file->getSections();
  return sections[info];
}

template <class ELFT, class RelTy>
void InputSection::copyRelocations(uint8_t *buf) {
  // x86/AArch64 only.
  struct MapRel {
    const ObjFile<ELFT> &file;
    Relocation operator()(const RelTy &rel) const {
      return Relocation{R_NONE, rel.getType(), rel.r_offset,
                        getAddend<ELFT>(rel), &file.getRelocTargetSym(rel)};
    }
  };
  using RawRels = ArrayRef<RelTy>;
  using MapRelIter = llvm::mapped_iterator<typename RawRels::iterator, MapRel>;
  auto mapRel = MapRel{*getFile<ELFT>()};
  RawRels rawRels = getDataAs<RelTy>();
  auto rels = llvm::make_range(MapRelIter(rawRels.begin(), mapRel),
                               MapRelIter(rawRels.end(), mapRel));
  copyRelocations<ELFT, RelTy>(buf, rels);
}

// This is used for -r and --emit-relocs. We can't use memcpy to copy
// relocations because we need to update symbol table offset and section index
// for each relocation. So we copy relocations one by one.
template <class ELFT, class RelTy, class RelIt>
void InputSection::copyRelocations(uint8_t *buf,
                                   llvm::iterator_range<RelIt> rels) {
  const TargetInfo &target = *elf::target;
  InputSectionBase *sec = getRelocatedSection();
  (void)sec->contentMaybeDecompress(); // uncompress if needed

  for (const Relocation &rel : rels) {
    RelType type = rel.type;
    const ObjFile<ELFT> *file = getFile<ELFT>();
    Symbol &sym = *rel.sym;

    auto *p = reinterpret_cast<typename ELFT::Rela *>(buf);
    buf += sizeof(RelTy);

    if (RelTy::IsRela)
      p->r_addend = rel.addend;

    // Output section VA is zero for -r, so r_offset is an offset within the
    // section, but for --emit-relocs it is a virtual address.
    p->r_offset = sec->getVA(rel.offset);
    p->setSymbolAndType(in.symTab->getSymbolIndex(&sym), type);

    if (sym.type == STT_SECTION) {
      // We combine multiple section symbols into only one per
      // section. This means we have to update the addend. That is
      // trivial for Elf_Rela, but for Elf_Rel we have to write to the
      // section data. We do that by adding to the Relocation vector.

      // .eh_frame is horribly special and can reference discarded sections. To
      // avoid having to parse and recreate .eh_frame, we just replace any
      // relocation in it pointing to discarded sections with R_*_NONE, which
      // hopefully creates a frame that is ignored at runtime. Also, don't warn
      // on .gcc_except_table and debug sections.
      //
      auto *d = dyn_cast<Defined>(&sym);
      if (!d) {
        if (!isDebugSection(*sec) && sec->name != ".eh_frame" &&
            sec->name != ".gcc_except_table" && sec->name != ".got2" &&
            sec->name != ".toc") {
          uint32_t secIdx = cast<Undefined>(sym).discardedSecIdx;
          Elf_Shdr_Impl<ELFT> sec = file->template getELFShdrs<ELFT>()[secIdx];
          warn("relocation refers to a discarded section: " +
               CHECK(file->getObj().getSectionName(sec), file) +
               "\n>>> referenced by " + getObjMsg(p->r_offset));
        }
        p->setSymbolAndType(0, 0);
        continue;
      }
      SectionBase *section = d->section;
      assert(section->isLive());

      int64_t addend = rel.addend;
      const uint8_t *bufLoc = sec->content().begin() + rel.offset;
      if (!RelTy::IsRela)
        addend = target.getImplicitAddend(bufLoc, type);

      if (RelTy::IsRela)
        p->r_addend = sym.getVA(addend) - section->getOutputSection()->addr;
      // For SHF_ALLOC sections relocated by REL, append a relocation to
      // sec->relocations so that relocateAlloc transitively called by
      // writeSections will update the implicit addend. Non-SHF_ALLOC sections
      // utilize relocateNonAlloc to process raw relocations and do not need
      // this sec->relocations change.
      // -r mode is handled by the merge library before the pipeline runs,
      // so config->relocatable is always false here.
    }
  }
}

// AArch64 ABI: branch to undefined weak resolves to next instruction (P+4).
namespace {
uint64_t getAArch64UndefinedRelativeWeakVA(uint64_t type, uint64_t p) {
  switch (type) {
  // Unresolved branch relocations to weak references resolve to next
  // instruction, this is 4 bytes on from P.
  case R_AARCH64_CALL26:
  case R_AARCH64_CONDBR19:
  case R_AARCH64_JUMP26:
  case R_AARCH64_TSTBR14:
    return p + 4;
  // Unresolved non branch pc-relative relocations
  case R_AARCH64_PREL16:
  case R_AARCH64_PREL32:
  case R_AARCH64_PREL64:
  case R_AARCH64_ADR_PREL_LO21:
  case R_AARCH64_LD_PREL_LO19:
  case R_AARCH64_PLT32:
    return p;
  }
  llvm_unreachable("AArch64 pc-relative relocation expected\n");
}

// A TLS symbol's virtual address is relative to the TLS segment. Add a
// target-specific adjustment to produce a thread-pointer-relative offset.
int64_t getTlsTpOffset(const Symbol &s) {
  // On targets that support TLSDESC, _TLS_MODULE_BASE_@tpoff = 0.
  if (&s == ElfSym::tlsModuleBase)
    return 0;

  // There are 2 TLS layouts. Among targets we support, x86 uses TLS Variant 2
  // while most others use Variant 1. At run time TP will be aligned to p_align.

  // Variant 1. TP will be followed by an optional gap (which is the size of 2
  // pointers on AArch64, 0 on other targets), followed by alignment
  // padding, then the static TLS blocks. The alignment padding is added so that
  // (TP + gap + padding) is congruent to p_vaddr modulo p_align.
  //
  // Variant 2. Static TLS blocks, followed by alignment padding are placed
  // before TP. The alignment padding is added so that (TP - padding -
  // p_memsz) is congruent to p_vaddr modulo p_align.
  // x86_64 (Variant 2), AArch64 (Variant 1) only.
  PhdrEntry *tls = Out::tlsPhdr;
  switch (config->emachine) {
  case EM_X86_64:
    return s.getVA(0) - tls->p_memsz -
           ((-tls->p_vaddr - tls->p_memsz) & (tls->p_align - 1));
  case EM_AARCH64:
    return s.getVA(0) + config->wordsize * 2 +
           ((tls->p_vaddr - config->wordsize * 2) & (tls->p_align - 1));
  default:
    llvm_unreachable("Unsupported architecture in this build");
  }
}
} // namespace

uint64_t InputSectionBase::getRelocTargetVA(const InputFile *file, RelType type,
                                            int64_t a, uint64_t p,
                                            const Symbol &sym, RelExpr expr) {
  switch (expr) {
  case R_ABS:
  case R_DTPREL:
  case R_RELAX_TLS_LD_TO_LE_ABS:
  case R_RELAX_GOT_PC_NOPIC:
    return sym.getVA(a);
  case R_ADDEND:
    return a;
  case R_RELAX_HINT:
    return 0;
  case R_GOT:
  case R_RELAX_TLS_GD_TO_IE_ABS:
    return sym.getGotVA() + a;
  case R_GOTONLY_PC:
    return in.got->getVA() + a - p;
  case R_GOTPLTONLY_PC:
    return in.gotPlt->getVA() + a - p;
  case R_GOTREL:
    return sym.getVA(a) - in.got->getVA();
  case R_GOTPLTREL:
    return sym.getVA(a) - in.gotPlt->getVA();
  case R_GOTPLT:
  case R_RELAX_TLS_GD_TO_IE_GOTPLT:
    return sym.getGotVA() + a - in.gotPlt->getVA();
  case R_TLSLD_GOT_OFF:
  case R_GOT_OFF:
  case R_RELAX_TLS_GD_TO_IE_GOT_OFF:
    return sym.getGotOffset() + a;
  case R_AARCH64_GOT_PAGE_PC:
  case R_AARCH64_RELAX_TLS_GD_TO_IE_PAGE_PC:
    return getAArch64Page(sym.getGotVA() + a) - getAArch64Page(p);
  case R_AARCH64_GOT_PAGE:
    return sym.getGotVA() + a - getAArch64Page(in.got->getVA());
  case R_GOT_PC:
  case R_RELAX_TLS_GD_TO_IE:
    return sym.getGotVA() + a - p;
  case R_AARCH64_PAGE_PC: {
    uint64_t val = sym.isUndefWeak() ? p + a : sym.getVA(a);
    return getAArch64Page(val) - getAArch64Page(p);
  }
  case R_PC: {
    uint64_t dest;
    if (sym.isUndefined()) {
      // Only AArch64 and x86. AArch64 resolves undefined weak branch to next
      // instr.
      if (config->emachine == EM_AARCH64)
        dest = getAArch64UndefinedRelativeWeakVA(type, p) + a;
      else
        dest = sym.getVA(a);
    } else {
      dest = sym.getVA(a);
    }
    return dest - p;
  }
  case R_PLT:
    return sym.getPltVA() + a;
  case R_PLT_PC:
    return sym.getPltVA() + a - p;
  case R_PLT_GOTPLT:
    return sym.getPltVA() + a - in.gotPlt->getVA();
  case R_RELAX_GOT_PC:
    return sym.getVA(a) - p;
  case R_RELAX_TLS_GD_TO_LE:
  case R_RELAX_TLS_IE_TO_LE:
  case R_RELAX_TLS_LD_TO_LE:
  case R_TPREL:
    // It is not very clear what to return if the symbol is undefined. With
    // --noinhibit-exec, even a non-weak undefined reference may reach here.
    // Just return A, which matches R_ABS, and the behavior of some dynamic
    // loaders.
    if (sym.isUndefined())
      return a;
    return getTlsTpOffset(sym) + a;
  case R_RELAX_TLS_GD_TO_LE_NEG:
  case R_TPREL_NEG:
    if (sym.isUndefined())
      return a;
    return -getTlsTpOffset(sym) + a;
  case R_SIZE:
    return sym.getSize() + a;
  case R_TLSDESC:
    return in.got->getTlsDescAddr(sym) + a;
  case R_TLSDESC_PC:
    return in.got->getTlsDescAddr(sym) + a - p;
  case R_TLSDESC_GOTPLT:
    return in.got->getTlsDescAddr(sym) + a - in.gotPlt->getVA();
  case R_AARCH64_TLSDESC_PAGE:
    return getAArch64Page(in.got->getTlsDescAddr(sym) + a) - getAArch64Page(p);
  case R_TLSGD_GOT:
    return in.got->getGlobalDynOffset(sym) + a;
  case R_TLSGD_GOTPLT:
    return in.got->getGlobalDynAddr(sym) + a - in.gotPlt->getVA();
  case R_TLSGD_PC:
    return in.got->getGlobalDynAddr(sym) + a - p;
  case R_TLSLD_GOTPLT:
    return in.got->getVA() + in.got->getTlsIndexOff() + a - in.gotPlt->getVA();
  case R_TLSLD_GOT:
    return in.got->getTlsIndexOff() + a;
  case R_TLSLD_PC:
    return in.got->getTlsIndexVA() + a - p;
  default:
    llvm_unreachable("Unsupported architecture in this build");
  }
}

// This function applies relocations to sections without SHF_ALLOC bit.
// Such sections are never mapped to memory at runtime. Debug sections are
// an example. Relocations in non-alloc sections are much easier to
// handle than in allocated sections because it will never need complex
// treatment such as GOT or PLT (because at runtime no one refers them).
// So, we handle relocations for non-alloc sections directly in this
// function as a performance optimization.
template <class ELFT, class RelTy>
void InputSection::relocateNonAlloc(uint8_t *buf, ArrayRef<RelTy> rels) {
  const unsigned bits = sizeof(typename ELFT::uint) * 8;
  const TargetInfo &target = *elf::target;
  const auto emachine = config->emachine;
  const bool isDebug = isDebugSection(*this);
  const bool isDebugLine = isDebug && name == ".debug_line";
  std::optional<uint64_t> tombstone;
  if (isDebug) {
    if (name == ".debug_loc" || name == ".debug_ranges")
      tombstone = 1;
    else if (name == ".debug_names")
      tombstone = UINT64_MAX; // tombstone value
    else
      tombstone = 0;
  }
  for (const auto &patAndValue : llvm::reverse(config->deadRelocInNonAlloc))
    if (patAndValue.first.match(this->name)) {
      tombstone = patAndValue.second;
      break;
    }

  for (size_t i = 0, relsSize = rels.size(); i != relsSize; ++i) {
    const RelTy &rel = rels[i];
    const RelType type = rel.getType();
    const uint64_t offset = rel.r_offset;
    uint8_t *bufLoc = buf + offset;
    int64_t addend = getAddend<ELFT>(rel);
    if (!RelTy::IsRela)
      addend += target.getImplicitAddend(bufLoc, type);

    Symbol &sym = getFile<ELFT>()->getRelocTargetSym(rel);
    RelExpr expr = target.getRelExpr(type, sym, bufLoc);
    if (expr == R_NONE)
      continue;
    auto *ds = dyn_cast<Defined>(&sym);

    if (tombstone && (expr == R_ABS || expr == R_DTPREL)) {
      // Resolve relocations in .debug_* referencing (discarded symbols or ICF
      // folded section symbols) to a tombstone value. Resolving to addend is
      // unsatisfactory because the result address range may collide with a
      // valid range of low address, or leave multiple CUs claiming ownership of
      // the same range of code, which may confuse consumers.
      //
      // To address the problems, we use -1 as a tombstone value for most
      // .debug_* sections. We have to ignore the addend because we don't want
      // to resolve an address attribute (which may have a non-zero addend) to
      // -1+addend (wrap around to a low address).
      //
      // R_DTPREL type relocations represent an offset into the dynamic thread
      // vector. The computed value is st_value plus a non-negative offset.
      // Negative values are invalid, so -1 can be used as the tombstone value.
      //
      // If the referenced symbol is discarded (made Undefined), or the
      // section defining the referenced symbol is garbage collected,
      // sym.getOutputSection() is nullptr. `ds->folded` catches the ICF folded
      // case. However, resolving a relocation in .debug_line to -1 would stop
      // debugger users from setting breakpoints on the folded-in function, so
      // exclude .debug_line.
      //
      // For pre-DWARF-v5 .debug_loc and .debug_ranges, -1 is a reserved value
      // (base address selection entry), use 1 (which is used by GNU ld for
      // .debug_ranges).
      //
      if (!sym.getOutputSection() || (ds && ds->folded && !isDebugLine)) {
        // If -z dead-reloc-in-nonalloc= is specified, respect it.
        uint64_t value = SignExtend64<bits>(*tombstone);
        // For a 32-bit local TU reference in .debug_names, X86_64::relocate
        // requires that the unsigned value for R_X86_64_32 is truncated to
        // 32-bit. Other 64-bit targets's don't discern signed/unsigned 32-bit
        // absolute relocations and do not need this change.
        if (emachine == EM_X86_64 && type == R_X86_64_32)
          value = static_cast<uint32_t>(value);
        target.relocateNoSym(bufLoc, type, value);
        continue;
      }
    }

    // R_ABS/R_DTPREL and some other relocations can be used from non-SHF_ALLOC
    // sections.
    if (LLVM_LIKELY(expr == R_ABS) || expr == R_DTPREL || expr == R_GOTPLTREL) {
      target.relocateNoSym(bufLoc, type, SignExtend64<bits>(sym.getVA(addend)));
      continue;
    }

    if (expr == R_SIZE) {
      target.relocateNoSym(bufLoc, type,
                           SignExtend64<bits>(sym.getSize() + addend));
      continue;
    }

    std::string msg = getLocation(offset) + ": has non-ABS relocation " +
                      toString(type) + " against symbol '" + toString(sym) +
                      "'";
    if (expr != R_PC) {
      errorOrWarn(msg);
      return;
    }

    // PC-relative relocation in a non-ALLOC section. Non-ALLOC sections are
    // not loaded at runtime so PC-relative doesn't make sense. GNU linkers
    // historically accept these, relocating as if at address 0. We do the
    // same for bug-compatibility.
    warn(msg);
    target.relocateNoSym(
        bufLoc, type,
        SignExtend64<bits>(sym.getVA(addend - offset - outSecOff)));
  }
}

template <class ELFT>
void InputSectionBase::relocate(uint8_t *buf, uint8_t *bufEnd) {
  if ((flags & SHF_EXECINSTR) && LLVM_UNLIKELY(getFile<ELFT>()->splitStack))
    adjustSplitStackFunctionPrologues<ELFT>(buf, bufEnd);

  if (flags & SHF_ALLOC) {
    target->relocateAlloc(*this, buf);
    return;
  }

  auto *sec = cast<InputSection>(this);
  // For a relocatable link, also call relocateNonAlloc() to rewrite applicable
  // locations with tombstone values.
  const RelsOrRelas<ELFT> rels = sec->template relsOrRelas<ELFT>();
  if (rels.areRelocsRel())
    sec->relocateNonAlloc<ELFT>(buf, rels.rels);
  else
    sec->relocateNonAlloc<ELFT>(buf, rels.relas);
}

// For each function-defining prologue, find any calls to __morestack,
// and replace them with calls to __morestack_non_split.
namespace {
void switchMorestackCallsToMorestackNonSplit(
    DenseSet<Defined *> &prologues,
    SmallVector<Relocation *, 0> &morestackCalls) {

  // If the target adjusted a function's prologue, all calls to
  // __morestack inside that function should be switched to
  // __morestack_non_split.
  Symbol *moreStackNonSplit = symtab.find("__morestack_non_split");
  if (!moreStackNonSplit) {
    error("mixing split-stack objects requires a definition of "
          "__morestack_non_split");
    return;
  }

  // Sort both collections to compare addresses efficiently.
  llvm::sort(morestackCalls, [](const Relocation *l, const Relocation *r) {
    return l->offset < r->offset;
  });
  std::vector<Defined *> functions(prologues.begin(), prologues.end());
  llvm::sort(functions, [](const Defined *l, const Defined *r) {
    return l->value < r->value;
  });

  auto it = morestackCalls.begin();
  for (Defined *f : functions) {
    // Find the first call to __morestack within the function.
    while (it != morestackCalls.end() && (*it)->offset < f->value)
      ++it;
    // Adjust all calls inside the function.
    while (it != morestackCalls.end() && (*it)->offset < f->value + f->size) {
      (*it)->sym = moreStackNonSplit;
      ++it;
    }
  }
}

bool enclosingPrologueAttempted(uint64_t offset,
                                const DenseSet<Defined *> &prologues) {
  for (Defined *f : prologues)
    if (f->value <= offset && offset < f->value + f->size)
      return true;
  return false;
}
} // namespace

// If a function compiled for split stack calls a function not
// compiled for split stack, then the caller needs its prologue
// adjusted to ensure that the called function will have enough stack
// available. Find those functions, and adjust their prologues.
template <class ELFT>
void InputSectionBase::adjustSplitStackFunctionPrologues(uint8_t *buf,
                                                         uint8_t *end) {
  DenseSet<Defined *> prologues;
  SmallVector<Relocation *, 0> morestackCalls;

  for (Relocation &rel : relocs()) {
    // Ignore calls into the split-stack api.
    if (rel.sym->getName().starts_with("__morestack")) {
      if (rel.sym->getName().equals("__morestack"))
        morestackCalls.push_back(&rel);
      continue;
    }

    // A relocation to non-function isn't relevant. Sometimes
    // __morestack is not marked as a function, so this check comes
    // after the name check.
    if (rel.sym->type != STT_FUNC)
      continue;

    // If the callee's-file was compiled with split stack, nothing to do.  In
    // this context, a "Defined" symbol is one "defined by the binary currently
    // being produced". So an "undefined" symbol might be provided by a shared
    // library. It is not possible to tell how such symbols were compiled, so be
    // conservative.
    if (Defined *d = dyn_cast<Defined>(rel.sym))
      if (InputSection *isec = cast_or_null<InputSection>(d->section))
        if (!isec || !isec->getFile<ELFT>() ||
            isec->getFile<ELFT>()->splitStack)
          continue;

    if (enclosingPrologueAttempted(rel.offset, prologues))
      continue;

    if (Defined *f = getEnclosingFunction(rel.offset)) {
      prologues.insert(f);
      if (target->adjustPrologueForCrossSplitStack(buf + f->value, end,
                                                   f->stOther))
        continue;
      if (!getFile<ELFT>()->someNoSplitStack)
        error(linker::toString(this) + ": " + f->getName() +
              " (with -fsplit-stack) calls " + rel.sym->getName() +
              " (without -fsplit-stack), but couldn't adjust its prologue");
    }
  }

  if (target->needsMoreStackNonSplit)
    switchMorestackCallsToMorestackNonSplit(prologues, morestackCalls);
}

template <class ELFT> void InputSection::writeTo(uint8_t *buf) {
  if (LLVM_UNLIKELY(type == SHT_NOBITS))
    return;
  // If -r or --emit-relocs is given, then an InputSection
  // may be a relocation section.
  if (LLVM_UNLIKELY(type == SHT_RELA)) {
    copyRelocations<ELFT, typename ELFT::Rela>(buf);
    return;
  }
  if (LLVM_UNLIKELY(type == SHT_REL)) {
    copyRelocations<ELFT, typename ELFT::Rel>(buf);
    return;
  }

  // If -r is given, we may have a SHT_GROUP section.
  if (LLVM_UNLIKELY(type == SHT_GROUP)) {
    copyShtGroup<ELFT>(buf);
    return;
  }

  // If this is a compressed section, uncompress section contents directly
  // to the buffer.
  if (compressed) {
    auto *hdr = reinterpret_cast<const typename ELFT::Chdr *>(content_);
    auto compressed = ArrayRef<uint8_t>(content_, compressedSize)
                          .slice(sizeof(typename ELFT::Chdr));
    size_t size = this->size;
    if (Error e = hdr->ch_type == ELFCOMPRESS_ZLIB
                      ? compression::zlib::decompress(compressed, buf, size)
                      : compression::zstd::decompress(compressed, buf, size))
      fatal(toString(this) +
            ": decompress failed: " + llvm::toString(std::move(e)));
    uint8_t *bufEnd = buf + size;
    relocate<ELFT>(buf, bufEnd);
    return;
  }

  // Copy section contents from source object file to output file
  // and then apply relocations.
  memcpy(buf, content().data(), content().size());
  relocate<ELFT>(buf, buf + content().size());
}

void InputSection::replace(InputSection *other) {
  addralign = std::max(addralign, other->addralign);

  // When a section is replaced with another section that was allocated to
  // another partition, the replacement section (and its associated sections)
  // need to be placed in the main partition so that both partitions will be
  // able to access it.
  if (partition != other->partition) {
    partition = 1;
    for (InputSection *isec : dependentSections)
      isec->partition = 1;
  }

  other->repl = repl;
  other->markDead();
}

template <class ELFT>
EhInputSection::EhInputSection(ObjFile<ELFT> &f,
                               const typename ELFT::Shdr &header,
                               StringRef name)
    : InputSectionBase(f, header, name, InputSectionBase::EHFrame) {}

SyntheticSection *EhInputSection::getParent() const {
  return cast_or_null<SyntheticSection>(parent);
}

// .eh_frame is a sequence of CIE or FDE records.
// This function splits an input section into records and returns them.
template <class ELFT> void EhInputSection::split() {
  const RelsOrRelas<ELFT> rels = relsOrRelas<ELFT>();
  // getReloc expects the relocations to be sorted by r_offset. See the comment
  // in scanRelocs.
  if (rels.areRelocsRel()) {
    SmallVector<typename ELFT::Rel, 0> storage;
    split<ELFT>(sortRels(rels.rels, storage));
  } else {
    SmallVector<typename ELFT::Rela, 0> storage;
    split<ELFT>(sortRels(rels.relas, storage));
  }
}

template <class ELFT, class RelTy>
void EhInputSection::split(ArrayRef<RelTy> rels) {
  ArrayRef<uint8_t> d = content();
  const char *msg = nullptr;
  unsigned relI = 0;
  while (!d.empty()) {
    if (d.size() < 4) {
      msg = "CIE/FDE too small";
      break;
    }
    uint64_t size = endian::read32<ELFT::TargetEndianness>(d.data());
    if (size == 0) // ZERO terminator
      break;
    uint32_t id = endian::read32<ELFT::TargetEndianness>(d.data() + 4);
    size += 4;
    if (LLVM_UNLIKELY(size > d.size())) {
      // If it is 0xFFFFFFFF, the next 8 bytes contain the size instead,
      // but we do not support that format yet.
      msg = size == UINT32_MAX + uint64_t(4)
                ? "CIE/FDE too large"
                : "CIE/FDE ends past the end of the section";
      break;
    }

    // Find the first relocation that points to [off,off+size). Relocations
    // have been sorted by r_offset.
    const uint64_t off = d.data() - content().data();
    while (relI != rels.size() && rels[relI].r_offset < off)
      ++relI;
    unsigned firstRel = -1;
    if (relI != rels.size() && rels[relI].r_offset < off + size)
      firstRel = relI;
    (id == 0 ? cies : fdes).emplace_back(off, this, size, firstRel);
    d = d.slice(size);
  }
  if (msg)
    errorOrWarn("corrupted .eh_frame: " + Twine(msg) + "\n>>> defined in " +
                getObjMsg(d.data() - content().data()));
}

// Return the offset in an output section for a given input offset.
uint64_t EhInputSection::getParentOffset(uint64_t offset) const {
  auto it = partition_point(
      fdes, [=](EhSectionPiece p) { return p.inputOff <= offset; });
  if (it == fdes.begin() || it[-1].inputOff + it[-1].size <= offset) {
    it = partition_point(
        cies, [=](EhSectionPiece p) { return p.inputOff <= offset; });
    if (it == cies.begin()) // invalid piece
      return offset;
  }
  if (it[-1].outputOff == -1) // invalid piece
    return offset - it[-1].inputOff;
  return it[-1].outputOff + (offset - it[-1].inputOff);
}

namespace {
size_t findNull(StringRef s, size_t entSize) {
  for (unsigned i = 0, n = s.size(); i != n; i += entSize) {
    const char *b = s.begin() + i;
    if (std::all_of(b, b + entSize, [](char c) { return c == 0; }))
      return i;
  }
  llvm_unreachable("");
}
} // namespace

// Split SHF_STRINGS section. Such section is a sequence of
// null-terminated strings.
void MergeInputSection::splitStrings(StringRef s, size_t entSize) {
  const bool live = !(flags & SHF_ALLOC) || !config->gcSections;
  const char *p = s.data(), *end = s.data() + s.size();
  if (!std::all_of(end - entSize, end, [](char c) { return c == 0; }))
    fatal(toString(this) + ": string is not null terminated");
  if (entSize == 1) {
    // Optimize the common case.
    do {
      size_t size = strlen(p);
      pieces.emplace_back(p - s.begin(), xxh3_64bits(StringRef(p, size)), live);
      p += size + 1;
    } while (p != end);
  } else {
    do {
      size_t size = findNull(StringRef(p, end - p), entSize);
      pieces.emplace_back(p - s.begin(), xxh3_64bits(StringRef(p, size)), live);
      p += size + entSize;
    } while (p != end);
  }
}

// Split non-SHF_STRINGS section. Such section is a sequence of
// fixed size records.
void MergeInputSection::splitNonStrings(ArrayRef<uint8_t> data,
                                        size_t entSize) {
  size_t size = data.size();
  assert((size % entSize) == 0);
  const bool live = !(flags & SHF_ALLOC) || !config->gcSections;

  pieces.resize_for_overwrite(size / entSize);
  for (size_t i = 0, j = 0; i != size; i += entSize, j++)
    pieces[j] = {i, (uint32_t)xxh3_64bits(data.slice(i, entSize)), live};
}

template <class ELFT>
MergeInputSection::MergeInputSection(ObjFile<ELFT> &f,
                                     const typename ELFT::Shdr &header,
                                     StringRef name)
    : InputSectionBase(f, header, name, InputSectionBase::Merge) {}

MergeInputSection::MergeInputSection(uint64_t flags, uint32_t type,
                                     uint64_t entsize, ArrayRef<uint8_t> data,
                                     StringRef name)
    : InputSectionBase(nullptr, flags, type, entsize, /*Link*/ 0, /*Info*/ 0,
                       /*Alignment*/ entsize, data, name, SectionBase::Merge) {}

// This function is called after we obtain a complete list of input sections
// that need to be linked. This is responsible to split section contents
// into small chunks for further processing.
//
// Note that this function is called from parallelForEach. This must be
// thread-safe (i.e. no memory allocation from the pools).
void MergeInputSection::splitIntoPieces() {
  assert(pieces.empty());

  if (flags & SHF_STRINGS)
    splitStrings(toStringRef(contentMaybeDecompress()), entsize);
  else
    splitNonStrings(contentMaybeDecompress(), entsize);
}

SectionPiece &MergeInputSection::getSectionPiece(uint64_t offset) {
  if (content().size() <= offset)
    fatal(toString(this) + ": offset is outside the section");
  return partition_point(
      pieces, [=](SectionPiece p) { return p.inputOff <= offset; })[-1];
}

// Return the offset in an output section for a given input offset.
uint64_t MergeInputSection::getParentOffset(uint64_t offset) const {
  const SectionPiece &piece = getSectionPiece(offset);
  return piece.outputOff + (offset - piece.inputOff);
}

template InputSection::InputSection(ObjFile<ELF64LE> &, const ELF64LE::Shdr &,
                                    StringRef);
template InputSection::InputSection(ObjFile<ELF64BE> &, const ELF64BE::Shdr &,
                                    StringRef);

template void InputSection::writeTo<ELF64LE>(uint8_t *);
template void InputSection::writeTo<ELF64BE>(uint8_t *);

template RelsOrRelas<ELF64LE> InputSectionBase::relsOrRelas<ELF64LE>() const;
template RelsOrRelas<ELF64BE> InputSectionBase::relsOrRelas<ELF64BE>() const;

template MergeInputSection::MergeInputSection(ObjFile<ELF64LE> &,
                                              const ELF64LE::Shdr &, StringRef);
template MergeInputSection::MergeInputSection(ObjFile<ELF64BE> &,
                                              const ELF64BE::Shdr &, StringRef);

template EhInputSection::EhInputSection(ObjFile<ELF64LE> &,
                                        const ELF64LE::Shdr &, StringRef);
template EhInputSection::EhInputSection(ObjFile<ELF64BE> &,
                                        const ELF64BE::Shdr &, StringRef);

template void EhInputSection::split<ELF64LE>();
template void EhInputSection::split<ELF64BE>();
