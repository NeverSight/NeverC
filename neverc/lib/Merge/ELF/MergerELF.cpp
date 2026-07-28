//===- MergerELF.cpp - ELF64LE relocatable merge (ported from LLD -r) --===//
//
// Relocatable object merger for ELF64LE.  The algorithms here are ported
// from the LLD ELF backend's `-r` (partial link) code paths:
//
//   Section merging   ← Layout/OutputSections.cpp  (canMergeToProgbits,
//                        commitSection, section (name,type,flags) grouping)
//   Symbol resolution ← Symbols/Symbols.cpp        (GLOBAL>WEAK>UNDEF,
//                        SHN_COMMON preserved, binding kept as-is in -r)
//   Relocation remap  ← Emit/ElfImageEmitter.cpp   (copyRelocs path,
//                        no scanRelocations in -r, just index remap)
//   Output writing    ← Emit/ElfImageEmitter.cpp   (ET_REL, addr=0,
//                        no PHDR, section symbols via addSectionSymbols)
//   .eh_frame         ← kept as regular section in -r (LLD skips
//                        ehFrame synthesis when config->relocatable)
//
// C++ specific logic (COMDAT groups, SHT_GROUP, .gnu.linkonce.*,
// template instantiation dedup) is intentionally removed — neverc is
// a pure-C compiler.  .eh_frame is preserved as a normal section so
// that C unwinding / stack traces work correctly.
//
//===------------------------------------------------------------------===//

#include "Common/DwarfRebase.h"
#include "Common/MergerCommon.h"
#include "neverc/Merge/Merger.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Support/Error.h"

#include <cstdint>
#include <cstring>

using namespace llvm;

namespace neverc::merge {

namespace {

// ---------------------------------------------------------------------------
// Section merge compatibility — ported from LLD OutputSections.cpp
// ---------------------------------------------------------------------------

// LLD allows merging several section types into SHT_PROGBITS when they
// end up in the same output section.  This mirrors `canMergeToProgbits()`
// in Layout/OutputSections.cpp.
bool canMergeToProgbits(uint32_t type, uint16_t machine) {
  using namespace llvm::ELF;
  return type == SHT_NOBITS || type == SHT_PROGBITS || type == SHT_INIT_ARRAY ||
         type == SHT_PREINIT_ARRAY || type == SHT_FINI_ARRAY ||
         type == SHT_NOTE ||
         (type == SHT_X86_64_UNWIND && machine == EM_X86_64);
}

// Two input sections can share an output section when:
//   1) Same name
//   2) Compatible types (both progbits-mergeable, or identical type)
//   3) Same flag set (modulo SHF_GROUP which we strip for pure C)
//
// Ported from the section matching logic in LLD's LinkerScript.cpp
// (addSection / getOutputSectionName) and OutputSections.cpp.
bool sectionsCompatible(uint32_t typeA, uint64_t flagsA, uint32_t typeB,
                        uint64_t flagsB, uint16_t machine) {
  if (typeA == typeB)
    return flagsA == flagsB;
  if (canMergeToProgbits(typeA, machine) && canMergeToProgbits(typeB, machine))
    return flagsA == flagsB;
  return false;
}

uint64_t clampAlign(uint64_t A) {
  constexpr uint64_t MaxAlign = uint64_t(1) << 20;
  if (A <= 1)
    return 1;
  if ((A & (A - 1)) != 0)
    return 1;
  return std::min(A, MaxAlign);
}

// ---------------------------------------------------------------------------
// Symbol resolution priority — ported from LLD Symbols/Symbols.cpp
// ---------------------------------------------------------------------------

// In -r mode LLD preserves symbol bindings as-is but still resolves
// duplicates: a defined GLOBAL beats a WEAK which beats an UNDEF.
// This priority mirrors Symbol::resolve() for the relocatable case.
enum SymPriority : uint8_t {
  PRI_UNDEF = 0,
  PRI_COMMON = 1,
  PRI_WEAK_DEF = 2,
  PRI_GLOBAL_DEF = 3,
};

template <typename SymT> SymPriority getSymPriority(const SymT &S) {
  using namespace llvm::ELF;
  bool isDefined = S.st_shndx != SHN_UNDEF;
  bool isCommon = S.st_shndx == SHN_COMMON;
  bool isWeak = S.getBinding() == STB_WEAK;

  if (!isDefined && !isCommon)
    return PRI_UNDEF;
  if (isCommon)
    return PRI_COMMON;
  if (isWeak)
    return PRI_WEAK_DEF;
  return PRI_GLOBAL_DEF;
}

// ---------------------------------------------------------------------------
// Core merger implementation
// ---------------------------------------------------------------------------

template <typename BufT>
bool mergeELF64LEImpl(ArrayRef<BufT> Buffers, raw_pwrite_stream &OS,
                      const Options &Opts) {
  using namespace llvm::object;
  using namespace llvm::ELF;
  using ELFT = ELF64LE;
  using Ehdr = typename ELFT::Ehdr;
  using Shdr = typename ELFT::Shdr;
  using Sym = typename ELFT::Sym;
  using Rela = typename ELFT::Rela;

  detail::DedupStrTab ShStrTab, SymStrTab;

  struct RelocEntry {
    Rela Entry;
    unsigned PartIdx;
  };

  struct MergedSection {
    std::string Name;
    Shdr Template;
    SmallVector<char, 0> Data;
    SmallVector<RelocEntry, 0> Relocs;
    uint64_t VirtualSize = 0;
    // SHF_LINK_ORDER bookkeeping.  Such a section (e.g. ftrace's
    // __patchable_function_entries, sh_link → its code section) requires a
    // valid sh_link in the output.  After -r merge every same-named input
    // collapses into one output section, so we record the *canonical* name of
    // each contributor's sh_link target and, after layout, remap sh_link to the
    // merged target section.  A single sh_link cannot name two targets, so when
    // contributors disagree (e.g. per-function .text.foo/.text.bar that are
    // NOT folded because mergeSections is off) we refuse rather than emit a
    // section whose ordering/dependency points at only one of them.
    bool HasLinkOrder = false;
    bool LinkTargetConsistent = true;
    std::string LinkTargetName;
  };
  SmallVector<MergedSection, 32> MergedSections;
  StringMap<SmallVector<unsigned, 2>> SectionIndex;

  uint16_t Machine = 0;
  uint32_t EFlags = 0;
  unsigned char OSABI = 0, ABIVer = 0;
  bool HaveArch = false;

  // Section merge: group by (name, compatible_type, flags).
  // Ported from LLD LinkerScript::addSection + OutputSections.cpp.
  auto findOrCreateSection = [&](StringRef Name, const Shdr &S) -> unsigned {
    auto &Candidates = SectionIndex[Name];
    uint64_t Flags = S.sh_flags & ~(uint64_t)SHF_GROUP;
    for (unsigned idx : Candidates)
      if (sectionsCompatible(MergedSections[idx].Template.sh_type,
                             MergedSections[idx].Template.sh_flags, S.sh_type,
                             Flags, Machine))
        return idx;
    MergedSection MS;
    MS.Name = Name.str();
    MS.Template = S;
    MS.Template.sh_flags = Flags;
    MS.Template.sh_addralign = clampAlign(MS.Template.sh_addralign);
    // sh_link/sh_info are regenerated for the metadata sections that use them
    // (.symtab→.strtab, .rela.*→.symtab/target) during output, so a merged
    // content section starts at 0.  SHF_LINK_ORDER sections (e.g.
    // __patchable_function_entries, .ARM.exidx) carry an sh_link to their
    // associated code section; after -r merge every such input collapses into
    // one output section, so the per-input link is meaningless and sh_link=0
    // (SHN_UNDEF) is the spec-legal "ordered, no required predecessor" value —
    // harmless for a single merged section.  neverc's kernel modules don't emit
    // these today (no default -fpatchable-function-entry; verified on the
    // sample .ko), so link remapping is intentionally omitted until a real
    // consumer exists to exercise it.
    MS.Template.sh_link = 0;
    MS.Template.sh_info = 0;
    unsigned NewIdx = MergedSections.size();
    MergedSections.push_back(std::move(MS));
    Candidates.push_back(NewIdx);
    return NewIdx;
  };

  struct PerPartition {
    DenseMap<unsigned, unsigned> SecMap;
    DenseMap<unsigned, unsigned> SymMap;
    DenseMap<unsigned, uint64_t> SecOff;
  };
  SmallVector<PerPartition, 8> Maps;
  SmallVector<PartitionDwarf, 8> PartDwarfs;
  PartDwarfs.resize(Buffers.size());

  // Symbol table: locals first, then globals (ELF convention).
  // In -r mode LLD does NOT recompute bindings (ElfImageEmitter.cpp:1829).
  SmallVector<Sym, 64> LocalSyms, GlobalSyms;
  LocalSyms.push_back({});
  memset(&LocalSyms[0], 0, sizeof(Sym));

  // Global symbol dedup: ported from LLD SymbolTable::insert +
  // Symbol::resolve.  In -r mode, GLOBAL>WEAK>UNDEF priority, and
  // SHN_COMMON is preserved (not converted to BSS).
  struct GlobalDedup {
    unsigned SlotIdx; // index into GlobalSyms
    SymPriority Pri;
    // A "strong" definition is a defined STB_GLOBAL (not WEAK, not COMMON, not
    // STB_GNU_UNIQUE).  Two of them for one name is an ODR violation; tracked
    // here so the second can be refused rather than silently dropped.
    bool Strong;
  };
  StringMap<GlobalDedup> GlobalMap;

  // Sum of all input bytes.  A relocatable merge only writes bytes copied from
  // an input section's on-disk contents (each copied once) plus a little
  // alignment padding, so the merged on-disk image cannot legitimately exceed
  // this.  SHT_NOBITS sections are the lone exception: they declare an sh_size
  // backed by *no* file bytes (a 64-byte section header can claim a 2^64 size),
  // and when such a section is folded into a PROGBITS output it must be
  // materialized as real zero bytes.  A garbage/hostile sh_size then drives an
  // unbounded SmallVector resize — the merge fuzzer hit a 7.6 EB allocation
  // (sh_size spelling "\1__mod_i") -> ASan allocation-size-too-big abort.  Cap
  // the cumulative materialized zero-fill at the total input size and refuse
  // beyond it: the parallel-codegen path never promotes NOBITS->PROGBITS so it
  // never trips, and the general -r path safely falls back to a real linker.
  uint64_t TotalInputBytes = 0;
  for (const auto &B : Buffers)
    TotalInputBytes += B.size();
  uint64_t MaterializedZeroFill = 0;

  for (unsigned p = 0; p < Buffers.size(); ++p) {
    if (Buffers[p].empty())
      continue;
    Maps.resize(p + 1);
    auto &PM = Maps[p];
    PM.SecMap[0] = 0;

    auto Buf = MemoryBufferRef(StringRef(Buffers[p].data(), Buffers[p].size()),
                               "part");
    auto ObjOrErr = ELFObjectFile<ELFT>::create(Buf);
    if (!ObjOrErr) {
      consumeError(ObjOrErr.takeError());
      return false;
    }
    const auto &EF = ObjOrErr->getELFFile();
    const Ehdr &Hdr = EF.getHeader();
    if (!HaveArch) {
      HaveArch = true;
      Machine = Hdr.e_machine;
      EFlags = Hdr.e_flags;
      OSABI = Hdr.e_ident[EI_OSABI];
      ABIVer = Hdr.e_ident[EI_ABIVERSION];
    } else if (Hdr.e_machine != Machine) {
      // Every input must target the same architecture.  Two ELF64LE objects of
      // different e_machine (e.g. x86-64 and AArch64) both parse cleanly as
      // ELF64LE here yet describe incompatible code; concatenating them would
      // emit one valid ELF header (the first input's e_machine) over a body
      // whose later partitions are the wrong ISA — a "loads then executes
      // garbage" miscompile no symbol/offset/content anchor can see, because
      // each partition's bytes still match its own input; only the architecture
      // is wrong.  The parallel-codegen path can never hit this (all partitions
      // share one TargetMachine); the general `-r` driver path over arbitrary
      // user objects can, and a real linker (`ld -r`) refuses it too.  Refuse
      // rather than emit a cross-ISA object.
      errs() << "neverc: relocatable merge: input has e_machine "
             << Hdr.e_machine << " but an earlier input had " << Machine
             << " (mixed architectures); refusing to merge\n";
      return false;
    }

    auto SecsOrErr = EF.sections();
    if (!SecsOrErr) {
      consumeError(SecsOrErr.takeError());
      return false;
    }
    ArrayRef<Shdr> Secs = *SecsOrErr;

    const Shdr *SymTabHdr = nullptr;
    StringRef SymStr;
    for (const Shdr &S : Secs) {
      if (S.sh_type == SHT_SYMTAB) {
        SymTabHdr = &S;
        if (auto R = EF.getStringTableForSymtab(S))
          SymStr = *R;
        else
          // Leave SymStr empty (every symbol name becomes "") rather than
          // letting the Expected's error go unchecked — an unconsumed llvm
          // Error aborts the process in assertions builds.  A malformed symbol
          // string table only reaches here on a hostile/garbage -r input; the
          // parallel-codegen path always produces a well-formed one.
          consumeError(R.takeError());
        break;
      }
    }

    // Section indices that some SHT_RELA/SHT_REL targets via sh_info.  Used
    // only to make the SHT_NOTE dedup below reloc-safe: a relocated note must
    // never be folded onto an earlier byte-identical copy, because Phase 3
    // would then append this note's relocations onto the front copy as well,
    // double-applying them (or, if the two notes carry relocations to different
    // symbols, silently keeping only the front's).  A note with relocations
    // therefore takes the normal concatenating path, where Phase 3 places its
    // relocations against its own SecOff like any other section.  Real notes
    // (.note.gnu.property, build-id, .note.GNU-stack) carry no relocations, so
    // this only ever changes the exotic relocated-note case.
    DenseSet<unsigned> SectionHasRelocTarget;
    for (const Shdr &RS : Secs)
      if (RS.sh_type == SHT_RELA || RS.sh_type == SHT_REL)
        // sh_info (the relocated target section index) is read straight from the
        // input, so a hostile/fuzzed object can set it to DenseMap's reserved
        // empty (~0u) or tombstone (~0u-1) key — inserting which asserts under
        // LLVM_ENABLE_ASSERTIONS (the merge fuzzer's recurring "Empty/Tombstone
        // value shouldn't be inserted" abort) and is UB otherwise.  A real reloc
        // target is always a valid section index, and the sole consumer
        // (count(i) with i in [1, Secs.size())) can never match an out-of-range
        // value, so bounding it here is behavior-preserving and also excludes
        // both reserved keys.
        if (RS.sh_info < Secs.size())
          SectionHasRelocTarget.insert(RS.sh_info);

    // ----- Phase 1: Merge sections -----
    // Skip metadata sections that are regenerated in the output.
    // SHT_GROUP is skipped because neverc is pure C — no COMDAT.
    // SHT_LLVM_ADDRSIG / SHT_LLVM_CALL_GRAPH_PROFILE are linker
    // metadata that don't survive -r.
    for (unsigned i = 1; i < Secs.size(); ++i) {
      const Shdr &S = Secs[i];
      // A COMDAT/section group (SHT_GROUP) implies dedup semantics this pure-C
      // -r merge does not implement.  Pure C post-LTO never emits one, so its
      // presence means an upstream assumption is violated; refuse rather than
      // silently drop the group and risk merging members that should have been
      // deduplicated into a single copy.
      if (S.sh_type == SHT_GROUP) {
        errs() << "neverc: relocatable merge does not support SHT_GROUP "
                  "(COMDAT) sections; refusing to merge\n";
        return false;
      }
      if (S.sh_type == SHT_SYMTAB || S.sh_type == SHT_STRTAB ||
          S.sh_type == SHT_RELA || S.sh_type == SHT_REL ||
          S.sh_type == SHT_LLVM_ADDRSIG ||
          S.sh_type == SHT_LLVM_CALL_GRAPH_PROFILE)
        continue;

      // A malformed sh_name (offset outside the section-header string table)
      // makes getSectionName return an Expected error.  The prior
      // `NameOrErr ? *NameOrErr : ""` consulted the value but never *consumed*
      // the error, so in an assertions / ABI-breaking-checks build the
      // Expected's destructor aborted the process ("Expected<T> must be checked
      // before access or destruction") at the next scope exit — the same
      // unchecked-Expected crash class the merge fuzzer found in the COFF path.
      // Consume the error and refuse: a processable section we cannot name
      // cannot be routed to the right merged section.  A valid object never
      // errors here, so this never false-rejects.
      auto NameOrErr = EF.getSectionName(S);
      if (!NameOrErr) {
        consumeError(NameOrErr.takeError());
        return false;
      }
      StringRef SecName = *NameOrErr;

      // SHF_LINK_ORDER sections (e.g. __patchable_function_entries emitted by
      // -fpatchable-function-entry for ftrace) carry an sh_link to an
      // associated code section.  The link target's *canonical* name is read
      // here, before SecName itself is canonicalized, and remapped to the
      // merged target section after layout (see the LinkTargetName handling
      // below and the sh_link remap pass before output).  Folding several such
      // inputs whose targets canonicalize to one section (the ftrace .ko case:
      // every .text.foo → .text) is correct; contributors that disagree are
      // refused at output time because one sh_link cannot name two targets.
      StringRef LinkTargetName;
      if (S.sh_flags & SHF_LINK_ORDER) {
        if (S.sh_link != 0 && S.sh_link < Secs.size()) {
          if (auto LN = EF.getSectionName(Secs[S.sh_link]))
            LinkTargetName = detail::canonicalELFSectionName(
                *LN, Secs[S.sh_link].sh_flags, Opts.mergeSections,
                Opts.preservedSections);
          else
            consumeError(LN.takeError());
        }
      }

      // Canonicalize per-symbol sections (.text.foo -> .text, ...) via the
      // single shared helper the verifier also uses, so the two never drift.
      SecName = detail::canonicalELFSectionName(SecName, S.sh_flags,
                                                Opts.mergeSections,
                                                Opts.preservedSections);

      if (Opts.dropDebugInfo &&
          (SecName.starts_with(".debug_") || SecName == ".debug" ||
           SecName.starts_with(".zdebug_")))
        continue;

      // SHT_NOTE dedup.  When parallel codegen splits one module every
      // partition re-emits byte-identical notes (e.g. .note.gnu.property for
      // BTI/PAC/CET), so collapsing them to a single copy is both correct and
      // tidy.  But this merger is *also* the linker's general `-r` path
      // (ElfDriver/MachODriver) over arbitrary, possibly heterogeneous user
      // objects, where two same-named notes can legitimately carry *different*
      // bytes (distinct GNU-property feature sets, build-ids, ...).  Dropping
      // the later one there silently loses data the verifier cannot catch (it
      // excludes NOTE sections from its content anchor).  So dedup only when
      // the incoming note is byte-identical to the copy already merged;
      // otherwise fall through to the normal concatenating path.  A
      // byte-identical note starts at the first copy's offset 0, so deduped
      // symbols into it need no offset shift.  A note that is itself a
      // relocation target is never deduped (SectionHasRelocTarget guard): see
      // its definition above for why double-applied relocations would result.
      if (S.sh_type == SHT_NOTE && p > 0 && !SectionHasRelocTarget.count(i)) {
        auto CIt = SectionIndex.find(SecName);
        if (CIt != SectionIndex.end() && !CIt->second.empty()) {
          unsigned FrontIdx = CIt->second.front();
          const MergedSection &Front = MergedSections[FrontIdx];
          if (Front.Template.sh_type == SHT_NOTE) {
            auto D = EF.getSectionContents(S);
            if (!D) {
              consumeError(D.takeError());
              return false;
            }
            ArrayRef<uint8_t> NB = *D;
            if (NB.size() == Front.Data.size() &&
                (NB.empty() ||
                 memcmp(NB.data(), Front.Data.data(), NB.size()) == 0)) {
              PM.SecMap[i] = FrontIdx + 1;
              continue;
            }
          }
        }
      }

      Shdr SCopy = S;
      SCopy.sh_flags &= ~(uint64_t)SHF_GROUP;
      unsigned MIdx = findOrCreateSection(SecName, SCopy);
      auto &MS = MergedSections[MIdx];

      // Record each SHF_LINK_ORDER contributor's (canonical) link target.  All
      // contributors to one merged section must agree, or a single output
      // sh_link cannot represent them; the disagreement is caught at output.
      if (S.sh_flags & SHF_LINK_ORDER) {
        if (!MS.HasLinkOrder) {
          MS.HasLinkOrder = true;
          MS.LinkTargetName = LinkTargetName.str();
        } else if (MS.LinkTargetName != LinkTargetName) {
          MS.LinkTargetConsistent = false;
        }
      }

      // Track max alignment (LLD: OutputSection::commitSection).
      {
        uint64_t SafeAlign = clampAlign(S.sh_addralign);
        if (SafeAlign > MS.Template.sh_addralign)
          MS.Template.sh_addralign = SafeAlign;
      }

      // When types differ but are compatible (canMergeToProgbits),
      // promote to SHT_PROGBITS (LLD behavior).
      if (MS.Template.sh_type != S.sh_type &&
          canMergeToProgbits(MS.Template.sh_type, Machine) &&
          canMergeToProgbits(S.sh_type, Machine))
        MS.Template.sh_type = SHT_PROGBITS;

      uint64_t Align = MS.Template.sh_addralign;
      uint64_t PartOffset;
      // Lay out by the *merged* section's type, not this input's.  Once any
      // PROGBITS-compatible input joins a NOBITS group the whole output section
      // is promoted to PROGBITS (above), and every NOBITS contribution must
      // then be materialized as zero bytes so all partitions measure their
      // offset against one continuous byte stream.  Keying off the input type
      // instead let a NOBITS run and a PROGBITS run both restart at offset 0 —
      // aliasing their symbols and dropping the NOBITS reserve from the output.
      if (MS.Template.sh_type == SHT_NOBITS) {
        // Pure NOBITS so far (this input is NOBITS too): no on-disk bytes.
        PartOffset = MS.VirtualSize;
        if (Align > 1)
          PartOffset += (Align - (PartOffset % Align)) % Align;
        MS.VirtualSize = PartOffset + S.sh_size;
      } else {
        // Merged section is (or just became) PROGBITS.  Materialize any
        // NOBITS-only space accumulated before the promotion as zero bytes so
        // the running offset stays continuous across the type change.  The
        // accumulated VirtualSize is attacker-controlled (it comes from a
        // NOBITS sh_size), so bound the zero-fill against the input size before
        // resizing (see MaterializedZeroFill above).
        if (MS.Data.size() < MS.VirtualSize) {
          uint64_t Fill = MS.VirtualSize - MS.Data.size();
          if (Fill > TotalInputBytes - MaterializedZeroFill) {
            errs() << "neverc: relocatable merge: materializing a NOBITS "
                      "section as "
                   << MS.VirtualSize
                   << " zero bytes exceeds the total input size; refusing to "
                      "merge\n";
            return false;
          }
          MaterializedZeroFill += Fill;
          MS.Data.resize(MS.VirtualSize, 0);
        }
        PartOffset = MS.Data.size();
        if (Align > 1) {
          uint64_t Padding = (Align - (PartOffset % Align)) % Align;
          MS.Data.resize(MS.Data.size() + Padding, 0);
          PartOffset = MS.Data.size();
        }
        if (S.sh_type == SHT_NOBITS) {
          // A NOBITS input folded into a PROGBITS output occupies real zero
          // bytes (it can no longer ride the pure-virtual-size path).  sh_size
          // is read straight from the input header and backed by no file bytes,
          // so bound it before the resize exactly as the accumulated fill above
          // (the merge fuzzer drove this very resize to a 7.6 EB allocation).
          if (S.sh_size > TotalInputBytes - MaterializedZeroFill) {
            errs() << "neverc: relocatable merge: materializing a NOBITS "
                      "section of "
                   << S.sh_size
                   << " zero bytes exceeds the total input size; refusing to "
                      "merge\n";
            return false;
          }
          MaterializedZeroFill += S.sh_size;
          MS.Data.resize(MS.Data.size() + S.sh_size, 0);
        } else {
          auto D = EF.getSectionContents(S);
          if (!D) {
            // We have already committed this input's PartOffset.  Silently
            // skipping its bytes would shift every later section and alias this
            // section's symbols onto the next one's data — the same class of
            // wrong-address corruption as a collapsed offset.  Refuse instead.
            consumeError(D.takeError());
            return false;
          }
          MS.Data.append(D->begin(), D->end());
        }
        MS.VirtualSize = MS.Data.size();
      }

      PM.SecMap[i] = MIdx + 1;
      PM.SecOff[i] = PartOffset;

      // DWARF sections address each other with plain integers rather than
      // relocations, so note where this partition landed in each of them; the
      // offsets are rewritten once every partition has been appended.
      // The size is what was actually appended rather than the value in the
      // section header, so a header that overstates its contents cannot make
      // the slice run into the next partition's bytes.  A zerofill section
      // appends nothing and its PartOffset indexes VirtualSize instead, hence
      // the guard.
      PartDwarfs[p].record(SecName, MIdx, PartOffset,
                           MS.Data.size() > PartOffset
                               ? MS.Data.size() - PartOffset
                               : 0);
    }

    // ----- Phase 2: Merge symbols -----
    // Ported from LLD finalizeSections → addSymbol.
    // Locals are concatenated; globals are deduped by name with
    // GLOBAL>WEAK>UNDEF priority (Symbol::resolve for -r).
    // SHN_COMMON symbols are preserved (LLD: getCommonSec returns
    // nullptr for non-relocated common symbols).
    if (SymTabHdr) {
      auto SymsOrErr = EF.symbols(SymTabHdr);
      if (!SymsOrErr) {
        consumeError(SymsOrErr.takeError());
        return false;
      }
      ArrayRef<Sym> Syms = *SymsOrErr;

      for (unsigned i = 1; i < Syms.size(); ++i) {
        Sym OutS = Syms[i];

        // Remap section index and adjust value.
        if (OutS.st_shndx < SHN_LORESERVE) {
          auto It = PM.SecMap.find(OutS.st_shndx);
          if (It != PM.SecMap.end()) {
            unsigned origShndx = OutS.st_shndx;
            OutS.st_shndx = It->second;
            if (It->second != 0) {
              auto OffIt = PM.SecOff.find(origShndx);
              if (OffIt != PM.SecOff.end())
                OutS.st_value += OffIt->second;
            }
          } else {
            OutS.st_shndx = 0;
          }
        }
        // SHN_COMMON: preserved as-is in -r mode (LLD behavior).

        // strnlen-bound the name to the string table extent.  llvm's
        // getStringTableForSymtab guarantees a trailing NUL (it errors
        // otherwise), so for well-formed input this is identical to the implicit
        // strlen; the explicit bound keeps every merger's symbol-name read
        // uniformly safe (matching the MachO merger and the verifier) and immune
        // to any future reader that stops validating the terminator.
        StringRef Name;
        if (OutS.st_name < SymStr.size())
          Name = StringRef(SymStr.data() + OutS.st_name,
                           strnlen(SymStr.data() + OutS.st_name,
                                   SymStr.size() - OutS.st_name));
        OutS.st_name = SymStrTab.add(Name);

        if (Syms[i].getBinding() == STB_LOCAL) {
          PM.SymMap[i] = LocalSyms.size();
          LocalSyms.push_back(OutS);
        } else {
          // Global symbol dedup: same-name globals are resolved by
          // priority (defined GLOBAL > WEAK > COMMON > UNDEF).
          // Ported from LLD SymbolTable::insert / Symbol::resolve.
          SymPriority Pri = getSymPriority(Syms[i]);
          // A strong definition is specifically a defined STB_GLOBAL: WEAK and
          // COMMON resolve by priority (legal), and STB_GNU_UNIQUE (a C++
          // vague-linkage symbol this pure-C merger never emits) is excluded so
          // it is never mistaken for an ODR clash.
          bool Strong =
              Pri == PRI_GLOBAL_DEF && Syms[i].getBinding() == STB_GLOBAL;
          auto [It, Inserted] = GlobalMap.try_emplace(
              Name, GlobalDedup{(unsigned)GlobalSyms.size(), Pri, Strong});

          if (Inserted) {
            unsigned Slot = GlobalSyms.size();
            GlobalSyms.push_back(OutS);
            // Record slot; will add FirstGlobal offset after the loop.
            PM.SymMap[i] = Slot | 0x80000000u;
          } else {
            if (Strong && It->second.Strong) {
              // Two strong (STB_GLOBAL, defined-in-section) definitions of one
              // name is an ODR violation.  A real link errors ("multiple
              // definition"); LLD -r keeps both and defers the error.  This
              // priority dedup would instead silently keep the first and drop
              // the second — the "loads fine, wrong symbol wins" divergence the
              // merger refuses elsewhere.  The parallel-codegen path can never
              // produce it (each global is defined in exactly one partition;
              // .__pcg locals carry unique names), so only a malformed -r input
              // reaches here; refuse rather than silently pick one.
              errs() << "neverc: relocatable merge: multiple strong definitions "
                        "of symbol '"
                     << Name << "'; refusing to merge\n";
              return false;
            }
            unsigned Slot = It->second.SlotIdx;
            if (Pri > It->second.Pri) {
              GlobalSyms[Slot] = OutS;
              It->second.Pri = Pri;
              It->second.Strong = Strong;
            }
            PM.SymMap[i] = Slot | 0x80000000u;
          }
        }
      }
    }

    // ----- Phase 3: Collect relocations -----
    // In -r mode LLD skips scanRelocations() entirely
    // (ElfImageEmitter.cpp:1773) and just copies relocs with remapped
    // indices (copyRelocs path).
    for (unsigned i = 1; i < Secs.size(); ++i) {
      if (Secs[i].sh_type != SHT_RELA && Secs[i].sh_type != SHT_REL)
        continue;
      // sh_info names the relocated section and is attacker-controlled; a value
      // equal to a DenseMap reserved key makes find() undefined, so skip it (it
      // cannot name a real section anyway).
      if (detail::isReservedDenseKey((unsigned)Secs[i].sh_info))
        continue;
      auto TargetIt = PM.SecMap.find(Secs[i].sh_info);
      if (TargetIt == PM.SecMap.end() || TargetIt->second == 0)
        continue;
      unsigned targetMIdx = TargetIt->second - 1;
      uint64_t dataOff = 0;
      auto OffIt = PM.SecOff.find(Secs[i].sh_info);
      if (OffIt != PM.SecOff.end())
        dataOff = OffIt->second;

      if (Secs[i].sh_type == SHT_RELA) {
        auto R = EF.relas(Secs[i]);
        if (!R) {
          // Silently dropping a relocation section would leave its
          // cross-references unrelocated in the output — a miscompile that
          // loads fine and then reads/jumps to the wrong place.  Refuse.
          consumeError(R.takeError());
          return false;
        }
        for (const Rela &Re : *R) {
          Rela Adjusted = Re;
          Adjusted.r_offset += dataOff;
          MergedSections[targetMIdx].Relocs.push_back({Adjusted, p});
        }
      } else {
        // SHT_REL (implicit-addend form).  A faithful REL->RELA conversion must
        // recover each relocation's implicit addend from the bytes it applies
        // to — architecture- and type-specific arithmetic.  The 64-bit ELF
        // targets neverc emits all use RELA, so this branch is unreachable in
        // practice; the prior code set r_addend=0, which silently dropped any
        // non-zero implicit addend, and the verifier checks reloc *offsets*,
        // not addend values, so it could not catch the resulting wrong value.
        // Refuse the merge instead so a hypothetical REL input falls back to
        // the proven path (serial codegen) or errors loudly, never miscompiles.
        return false;
      }
    }
  }

  // Every partition has now been appended, so each one's DWARF sections have
  // a known home in the merged output.  Re-point the offsets the units carry
  // for each other; until this runs, every partition after the first reads the
  // first one's abbreviation and string tables.
  if (!rebaseMergedDwarf(
          PartDwarfs,
          [&](unsigned Idx) {
            return Idx < MergedSections.size()
                       ? MutableArrayRef<char>(MergedSections[Idx].Data)
                       : MutableArrayRef<char>();
          },
          ELFT::TargetEndianness == llvm::endianness::little))
    return false;

  // ----- Finalize global symbol indices -----
  // During the loop, global SymMap entries store (slot | 0x80000000).
  // Now that FirstGlobal is known, resolve to actual indices.
  unsigned FirstGlobal = LocalSyms.size();
  for (auto &PM : Maps)
    for (auto &[K, V] : PM.SymMap)
      if (V & 0x80000000u)
        V = FirstGlobal + (V & 0x7FFFFFFFu);

  // Remap relocation symbol indices.
  for (auto &MS : MergedSections) {
    for (auto &RE : MS.Relocs) {
      if (RE.PartIdx >= Maps.size())
        continue;
      unsigned origSym = RE.Entry.getSymbol();
      unsigned newSym = 0;
      // origSym comes from r_info and is attacker-controlled; a reserved
      // DenseMap key would make find() undefined.  Leave newSym = 0 (the
      // undefined-symbol slot) for such a relocation.
      if (!detail::isReservedDenseKey(origSym)) {
        auto It = Maps[RE.PartIdx].SymMap.find(origSym);
        if (It != Maps[RE.PartIdx].SymMap.end())
          newSym = It->second;
      }
      RE.Entry.setSymbolAndType(newSym, RE.Entry.getType());
    }
  }

  // ----- Demote __pcg symbols to local (matching MachO merger) -----
  // Symbols externalized only for cross-partition references (with the
  // .__pcg<hash> suffix) are an implementation detail of parallel codegen
  // and must not bloat the final binary's symbol table.  Convert defined
  // __pcg globals to STB_LOCAL and reorder the symbol table accordingly.
  {
    DenseSet<unsigned> DemoteSlots;
    for (unsigned i = 0; i < GlobalSyms.size(); ++i) {
      if (GlobalSyms[i].st_shndx == ELF::SHN_UNDEF ||
          GlobalSyms[i].st_shndx == ELF::SHN_COMMON)
        continue;
      StringRef Name(SymStrTab.Data.data() + GlobalSyms[i].st_name);
      if (Name.contains(PcgSymbolMarker)) {
        GlobalSyms[i].setBinding(ELF::STB_LOCAL);
        DemoteSlots.insert(i);
      }
    }
    if (!DemoteSlots.empty()) {
      SmallVector<Sym, 64> NewLocals, NewGlobals;
      DenseMap<unsigned, unsigned> ReorderMap;
      // Identity-map existing locals.
      for (unsigned i = 0; i < LocalSyms.size(); ++i)
        ReorderMap[i] = i;
      NewLocals = std::move(LocalSyms);
      // Append demoted globals into the local section.
      for (unsigned i = 0; i < GlobalSyms.size(); ++i) {
        unsigned OldIdx = FirstGlobal + i;
        if (DemoteSlots.count(i)) {
          ReorderMap[OldIdx] = NewLocals.size();
          NewLocals.push_back(GlobalSyms[i]);
        }
      }
      unsigned NewFirstGlobal = NewLocals.size();
      for (unsigned i = 0; i < GlobalSyms.size(); ++i) {
        unsigned OldIdx = FirstGlobal + i;
        if (!DemoteSlots.count(i)) {
          ReorderMap[OldIdx] = NewFirstGlobal + NewGlobals.size();
          NewGlobals.push_back(GlobalSyms[i]);
        }
      }
      LocalSyms = std::move(NewLocals);
      GlobalSyms = std::move(NewGlobals);
      FirstGlobal = NewFirstGlobal;
      // Remap relocation symbol indices to match the new ordering.
      for (auto &MS : MergedSections)
        for (auto &RE : MS.Relocs) {
          unsigned OldSym = RE.Entry.getSymbol();
          auto It = ReorderMap.find(OldSym);
          if (It != ReorderMap.end())
            RE.Entry.setSymbolAndType(It->second, RE.Entry.getType());
        }
    }
  }

  // ----- Build output sections -----
  SmallVector<Sym, 64> FinalSyms;
  FinalSyms.append(LocalSyms.begin(), LocalSyms.end());
  FinalSyms.append(GlobalSyms.begin(), GlobalSyms.end());

  struct OutSection {
    Shdr Hdr;
    SmallVector<char, 0> Data;
    uint64_t VirtualSize = 0;
  };
  SmallVector<OutSection, 32> OutSections;
  OutSections.push_back({});
  memset(&OutSections[0].Hdr, 0, sizeof(Shdr));

  for (auto &MS : MergedSections) {
    OutSection Out;
    Out.Hdr = MS.Template;
    Out.Hdr.sh_name = ShStrTab.add(MS.Name);
    Out.Data = std::move(MS.Data);
    Out.VirtualSize = MS.VirtualSize;
    OutSections.push_back(std::move(Out));
  }

  // Remap SHF_LINK_ORDER sh_link to the merged target section now that every
  // content section exists.  Output section index == merged section index + 1
  // (slot 0 is the null section).  A consistent, present target yields a valid
  // sh_link (the very invariant verifyMerge audits); anything else is refused
  // so the caller falls back rather than emitting a section whose ordering
  // dependency is dropped or points at the wrong code.
  for (unsigned m = 0; m < MergedSections.size(); ++m) {
    const auto &MS = MergedSections[m];
    if (!MS.HasLinkOrder)
      continue;
    if (!MS.LinkTargetConsistent) {
      errs() << "neverc: relocatable merge: SHF_LINK_ORDER section '" << MS.Name
             << "' has contributors with differing link targets; refusing\n";
      return false;
    }
    auto It = SectionIndex.find(MS.LinkTargetName);
    if (MS.LinkTargetName.empty() || It == SectionIndex.end() ||
        It->second.empty()) {
      errs() << "neverc: relocatable merge: SHF_LINK_ORDER section '" << MS.Name
             << "' link target '" << MS.LinkTargetName
             << "' is absent from the merged output; refusing\n";
      return false;
    }
    OutSections[m + 1].Hdr.sh_link = It->second.front() + 1;
  }

  unsigned SymTabIdx = OutSections.size();
  {
    OutSection S;
    memset(&S.Hdr, 0, sizeof(Shdr));
    S.Hdr.sh_type = SHT_SYMTAB;
    S.Hdr.sh_entsize = sizeof(Sym);
    S.Hdr.sh_addralign = 8;
    S.Hdr.sh_info = FirstGlobal;
    S.Hdr.sh_name = ShStrTab.add(".symtab");
    S.Data.assign(reinterpret_cast<const char *>(FinalSyms.data()),
                  reinterpret_cast<const char *>(FinalSyms.data()) +
                      FinalSyms.size() * sizeof(Sym));
    OutSections.push_back(std::move(S));
  }

  unsigned StrTabIdx = OutSections.size();
  {
    OutSection S;
    memset(&S.Hdr, 0, sizeof(Shdr));
    S.Hdr.sh_type = SHT_STRTAB;
    S.Hdr.sh_addralign = 1;
    S.Hdr.sh_name = ShStrTab.add(".strtab");
    S.Data.assign(SymStrTab.Data.begin(), SymStrTab.Data.end());
    OutSections.push_back(std::move(S));
  }
  OutSections[SymTabIdx].Hdr.sh_link = StrTabIdx;

  // Emit .rela sections for each merged section with relocations.
  for (unsigned m = 0; m < MergedSections.size(); ++m) {
    auto &MS = MergedSections[m];
    if (MS.Relocs.empty())
      continue;
    OutSection S;
    memset(&S.Hdr, 0, sizeof(Shdr));
    S.Hdr.sh_type = SHT_RELA;
    S.Hdr.sh_entsize = sizeof(Rela);
    S.Hdr.sh_addralign = 8;
    S.Hdr.sh_link = SymTabIdx;
    S.Hdr.sh_info = m + 1;
    S.Hdr.sh_name = ShStrTab.add((".rela" + MS.Name).c_str());
    SmallVector<Rela, 0> FlatRelas;
    for (auto &RE : MS.Relocs)
      FlatRelas.push_back(RE.Entry);
    S.Data.assign(reinterpret_cast<const char *>(FlatRelas.data()),
                  reinterpret_cast<const char *>(FlatRelas.data()) +
                      FlatRelas.size() * sizeof(Rela));
    OutSections.push_back(std::move(S));
  }

  unsigned ShStrTabIdx = OutSections.size();
  {
    uint32_t nameOff = ShStrTab.add(".shstrtab");
    OutSection S;
    memset(&S.Hdr, 0, sizeof(Shdr));
    S.Hdr.sh_type = SHT_STRTAB;
    S.Hdr.sh_addralign = 1;
    S.Hdr.sh_name = nameOff;
    S.Data.assign(ShStrTab.Data.begin(), ShStrTab.Data.end());
    OutSections.push_back(std::move(S));
  }

  // e_shnum is a 16-bit field; a section count >= SHN_LORESERVE requires the
  // SHN_XINDEX escape (e_shnum=0, real count in section[0].sh_size) which this
  // -r merge does not emit.  Writing the count straight into the 16-bit field
  // would silently truncate and produce a corrupt object — the same
  // valid-looking-but-wrong failure class the verifier exists to stop, but at a
  // layer the verifier (which trusts the parsed header) cannot see.  Refuse
  // instead so the caller falls back to serial codegen / a real linker.  Only
  // reachable on a single link of tens of thousands of per-function sections
  // (FunctionSections); the kernel-module mergeSections path folds to a handful.
  if (OutSections.size() >= ELF::SHN_LORESERVE) {
    errs() << "neverc: relocatable merge produced " << OutSections.size()
           << " sections, exceeding the ELF e_shnum limit ("
           << ELF::SHN_LORESERVE << "); refusing to emit a truncated object\n";
    return false;
  }

  // ----- Layout -----
  // All section addresses are 0 in -r mode (ElfImageEmitter.cpp:1552).
  uint64_t Off = sizeof(Ehdr);
  for (unsigned i = 1; i < OutSections.size(); ++i) {
    uint64_t Align = clampAlign(OutSections[i].Hdr.sh_addralign);
    Off = (Off + Align - 1) & ~(Align - 1);
    OutSections[i].Hdr.sh_offset = Off;
    if (OutSections[i].Hdr.sh_type == SHT_NOBITS) {
      OutSections[i].Hdr.sh_size = OutSections[i].VirtualSize;
    } else {
      OutSections[i].Hdr.sh_size = OutSections[i].Data.size();
      Off += OutSections[i].Data.size();
    }
  }
  Off = (Off + 7) & ~(uint64_t)7;
  uint64_t ShOff = Off;

  // ----- Write output -----
  // Ported from LLD ElfImageEmitter::writeHeader / writeResult.
  SmallVector<char, 0> OutBuf;
  OutBuf.resize(ShOff + OutSections.size() * sizeof(Shdr), 0);

  Ehdr *H = reinterpret_cast<Ehdr *>(OutBuf.data());
  memset(H, 0, sizeof(Ehdr));
  memcpy(H->e_ident, ElfMagic, 4);
  H->e_ident[EI_CLASS] = ELFCLASS64;
  H->e_ident[EI_DATA] = ELFDATA2LSB;
  H->e_ident[EI_VERSION] = EV_CURRENT;
  H->e_ident[EI_OSABI] = OSABI;
  H->e_ident[EI_ABIVERSION] = ABIVer;
  H->e_type = ET_REL;
  H->e_machine = Machine;
  H->e_version = EV_CURRENT;
  H->e_ehsize = sizeof(Ehdr);
  H->e_shentsize = sizeof(Shdr);
  H->e_shoff = ShOff;
  H->e_shnum = OutSections.size();
  H->e_shstrndx = ShStrTabIdx;
  H->e_flags = EFlags;
  // No e_phoff / e_phentsize / e_phnum — ET_REL has no program headers
  // (LLD: ElfImageEmitter.cpp:2625 skips these when config->relocatable).

  for (unsigned i = 1; i < OutSections.size(); ++i)
    if (!OutSections[i].Data.empty())
      memcpy(OutBuf.data() + OutSections[i].Hdr.sh_offset,
             OutSections[i].Data.data(), OutSections[i].Data.size());

  for (unsigned i = 0; i < OutSections.size(); ++i)
    memcpy(OutBuf.data() + ShOff + i * sizeof(Shdr), &OutSections[i].Hdr,
           sizeof(Shdr));

  // Self-verify before committing: an independent re-parse content-anchors
  // every uniquely-named defined symbol back to its input bytes.  On any
  // divergence we refuse to write the object and return false, so the caller
  // falls back to the proven path (serial codegen) or errors loudly rather
  // than emitting a valid-looking but semantically wrong .o.
  if (Opts.verify) {
    SmallVector<StringRef, 8> Views;
    Views.reserve(Buffers.size());
    for (const auto &B : Buffers)
      Views.push_back(StringRef(B.data(), B.size()));
    std::string VErr;
    if (!verifyMerge(ArrayRef<StringRef>(Views), ArrayRef<char>(OutBuf),
                     Format::ELF64LE, Opts, &VErr)) {
      errs() << "neverc: relocatable merge self-check failed: " << VErr << "\n";
      return false;
    }
  }

  OS.write(OutBuf.data(), OutBuf.size());
  return true;
}

} // anonymous namespace

bool mergeELF64LEObjects(ArrayRef<SmallVector<char, 0>> Buffers,
                         raw_pwrite_stream &OS, const Options &Opts) {
  return detail::runMergeSafely(
      [&]() { return mergeELF64LEImpl(Buffers, OS, Opts); });
}

bool mergeELF64LEObjects(ArrayRef<StringRef> Buffers, raw_pwrite_stream &OS,
                         const Options &Opts) {
  return detail::runMergeSafely(
      [&]() { return mergeELF64LEImpl(Buffers, OS, Opts); });
}

} // namespace neverc::merge
