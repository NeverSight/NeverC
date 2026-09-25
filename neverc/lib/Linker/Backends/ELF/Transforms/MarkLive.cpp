#include "Linker/ELF/MarkLive.h"
#include "Linker/Core/Runtime/LinkerParallel.h"
#include "Linker/Core/Runtime/Session.h"
#include "Linker/Core/Support/Strings.h"
#include "Linker/ELF/ELFContextAccess.h"
#include "Linker/ELF/InputFiles.h"
#include "Linker/ELF/InputSection.h"
#include "Linker/ELF/LinkerScript.h"
#include "Linker/ELF/SymbolTable.h"
#include "Linker/ELF/Symbols.h"
#include "Linker/ELF/SyntheticSections.h"
#include "Linker/ELF/Target.h"
#include "llvm/Object/ELF.h"
#include "llvm/Support/TimeProfiler.h"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <vector>
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif

using namespace llvm;
using namespace llvm::ELF;
using namespace llvm::object;
using namespace llvm::support::endian;
using namespace linker;
using namespace linker::elf;

// ===----------------------------------------------------------------------===
// Garbage collection
// ===----------------------------------------------------------------------===

namespace {
// Atomically set a section's partition to the main partition and return the
// previous value. Sections are claimed exactly once this way, so concurrent
// markers never scan the same section twice.
uint8_t claimMainPartition(InputSectionBase *sec) {
#if defined(_MSC_VER) && !defined(__clang__)
  return static_cast<uint8_t>(_InterlockedExchange8(
      reinterpret_cast<volatile char *>(&sec->partition), 1));
#else
  return __atomic_exchange_n(&sec->partition, uint8_t(1), __ATOMIC_RELAXED);
#endif
}

uint8_t loadPartition(const InputSectionBase *sec) {
#if defined(_MSC_VER) && !defined(__clang__)
  return *reinterpret_cast<const volatile uint8_t *>(&sec->partition);
#else
  return __atomic_load_n(&sec->partition, __ATOMIC_RELAXED);
#endif
}

// Atomically set SectionPiece::live. The bit shares a word with the piece
// hash, which is immutable during garbage collection.
void setPieceLiveAtomic(SectionPiece &piece) {
  static const uint32_t liveMask = [] {
    SectionPiece probe(0, 0, true);
    uint32_t word;
    std::memcpy(&word,
                reinterpret_cast<const char *>(&probe) + sizeof(probe.inputOff),
                sizeof(word));
    return word;
  }();
  auto *word = reinterpret_cast<uint32_t *>(reinterpret_cast<char *>(&piece) +
                                            sizeof(piece.inputOff));
#if defined(_MSC_VER) && !defined(__clang__)
  if (*reinterpret_cast<volatile uint32_t *>(word) & liveMask)
    return;
  _InterlockedOr(reinterpret_cast<volatile long *>(word),
                 static_cast<long>(liveMask));
#else
  if (__atomic_load_n(word, __ATOMIC_RELAXED) & liveMask)
    return;
  __atomic_fetch_or(word, liveMask, __ATOMIC_RELAXED);
#endif
}

// Symbol::used is a bitfield. Find its byte and mask once, from zeroed symbol
// storage (the way SymbolTable::insert initializes symbols), so concurrent
// markers can set it with an atomic OR that preserves the other bits.
struct UsedBit {
  size_t offset;
  uint8_t mask;
};
const UsedBit &getUsedBit() {
  static const UsedBit bit = [] {
    alignas(SymbolUnion) unsigned char storage[sizeof(SymbolUnion)] = {};
    reinterpret_cast<Symbol *>(storage)->used = true;
    for (size_t i = 0; i != sizeof(Symbol); ++i)
      if (storage[i])
        return UsedBit{i, storage[i]};
    llvm_unreachable("Symbol::used has no storage");
  }();
  return bit;
}

void setUsedAtomic(Symbol &sym) {
  const UsedBit &bit = getUsedBit();
  auto *byte = reinterpret_cast<uint8_t *>(&sym) + bit.offset;
#if defined(_MSC_VER) && !defined(__clang__)
  if (*reinterpret_cast<volatile uint8_t *>(byte) & bit.mask)
    return;
  _InterlockedOr8(reinterpret_cast<volatile char *>(byte),
                  static_cast<char>(bit.mask));
#else
  if (__atomic_load_n(byte, __ATOMIC_RELAXED) & bit.mask)
    return;
  __atomic_fetch_or(byte, bit.mask, __ATOMIC_RELAXED);
#endif
}

size_t getParallelWorklistThreshold() {
  unsigned threads = parallelThreadCount();
  size_t threshold = static_cast<size_t>(threads) * 2048;
  return std::clamp<size_t>(threshold, 4096, 65536);
}

template <class ELFT> class MarkLive {
public:
  MarkLive(unsigned partition) : partition(partition) {}

  void run();
  void moveToMain();

private:
  void enqueue(InputSectionBase *sec, uint64_t offset);
  void markSymbol(Symbol *sym);
  void mark();
  // Per-task marking state. SharedFile::isNeeded is a plain bool that other
  // code reads, so tasks record needed files and the flag is written between
  // rounds, when no task runs; such references are rare.
  struct ParallelMarkTask {
    SmallVector<InputSection *, 0> stack;
    SmallVector<SharedFile *, 0> neededFiles;
  };
  void markMainPartitionParallel();
  bool collectMainRootsParallel();
  template <class RelTy>
  void scanEhFrameParallel(EhInputSection &eh, ArrayRef<RelTy> rels,
                           ParallelMarkTask &task);
  void scanParallel(InputSectionBase &sec, ParallelMarkTask &task) const;
  static void enqueueParallel(InputSectionBase *sec, uint64_t offset,
                              SmallVectorImpl<InputSection *> &stack);

  template <class RelTy>
  void resolveReloc(InputSectionBase &sec, RelTy &rel, bool fromFDE);

  template <class RelTy>
  void scanEhFrameSection(EhInputSection &eh, ArrayRef<RelTy> rels);

  // The index of the partition that we are currently processing.
  unsigned partition;

  // A list of sections to visit.
  SmallVector<InputSection *, 0> queue;

  // There are normally few input sections whose names are valid C
  // identifiers, so we just store a SmallVector instead of a multimap.
  DenseMap<StringRef, SmallVector<InputSectionBase *, 0>> cNamedSections;
};
} // namespace

namespace {
template <class ELFT>
uint64_t getAddend(InputSectionBase &sec, const typename ELFT::Rel &rel) {
  return target->getImplicitAddend(sec.content().begin() + rel.r_offset,
                                   rel.getType());
}

template <class ELFT>
uint64_t getAddend(InputSectionBase &sec, const typename ELFT::Rela &rel) {
  return rel.r_addend;
}
} // namespace

template <class ELFT>
template <class RelTy>
void MarkLive<ELFT>::resolveReloc(InputSectionBase &sec, RelTy &rel,
                                  bool fromFDE) {
  Symbol &sym = sec.getFile<ELFT>()->getRelocTargetSym(rel);

  // If a symbol is referenced in a live section, it is used.
  sym.used = true;

  if (auto *d = dyn_cast<Defined>(&sym)) {
    auto *relSec = dyn_cast_or_null<InputSectionBase>(d->section);
    if (!relSec)
      return;

    uint64_t offset = d->value;
    if (d->isSection())
      offset += getAddend<ELFT>(sec, rel);

    // fromFDE being true means this is referenced by a FDE in a .eh_frame
    // piece. The relocation points to the described function or to a LSDA. We
    // only need to keep the LSDA live, so ignore anything that points to
    // executable sections. If the LSDA is in a section group or has the
    // SHF_LINK_ORDER flag, we ignore the relocation as well because (a) if the
    // associated text section is live, the LSDA will be retained due to section
    // group/SHF_LINK_ORDER rules (b) if the associated text section should be
    // discarded, marking the LSDA will unnecessarily retain the text section.
    if (!(fromFDE && ((relSec->flags & (SHF_EXECINSTR | SHF_LINK_ORDER)) ||
                      relSec->nextInSectionGroup)))
      enqueue(relSec, offset);
    return;
  }

  if (auto *ss = dyn_cast<SharedSymbol>(&sym))
    if (!ss->isWeak())
      cast<SharedFile>(ss->file)->isNeeded = true;

  for (InputSectionBase *sec : cNamedSections.lookup(sym.getName()))
    enqueue(sec, 0);
}

// The .eh_frame section is an unfortunate special case.
// The section is divided in CIEs and FDEs and the relocations it can have are
// * CIEs can refer to a personality function.
// * FDEs can refer to a LSDA
// * FDEs refer to the function they contain information about
// The last kind of relocation cannot keep the referred section alive, or they
// would keep everything alive in a common object file. In fact, each FDE is
// alive if the section it refers to is alive.
// To keep things simple, in here we just ignore the last relocation kind. The
// other two keep the referred section alive.
//
// A possible improvement would be to fully process .eh_frame in the middle of
// the gc pass. With that we would be able to also gc some sections holding
// LSDAs and personality functions if we found that they were unused.
template <class ELFT>
template <class RelTy>
void MarkLive<ELFT>::scanEhFrameSection(EhInputSection &eh,
                                        ArrayRef<RelTy> rels) {
  for (const EhSectionPiece &cie : eh.cies)
    if (cie.firstRelocation != unsigned(-1))
      resolveReloc(eh, rels[cie.firstRelocation], false);
  for (const EhSectionPiece &fde : eh.fdes) {
    size_t firstRelI = fde.firstRelocation;
    if (firstRelI == (unsigned)-1)
      continue;
    uint64_t pieceEnd = fde.inputOff + fde.size;
    for (size_t j = firstRelI, end2 = rels.size();
         j < end2 && rels[j].r_offset < pieceEnd; ++j)
      resolveReloc(eh, rels[j], true);
  }
}

// Some sections are used directly by the loader, so they should never be
// garbage-collected. This function returns true if a given section is such
// section.
namespace {
bool isReserved(InputSectionBase *sec) {
  switch (sec->type) {
  case SHT_FINI_ARRAY:
  case SHT_INIT_ARRAY:
  case SHT_PREINIT_ARRAY:
    return true;
  case SHT_NOTE:
    // SHT_NOTE sections in a group are subject to garbage collection.
    return !sec->nextInSectionGroup;
  default:
    // Support SHT_PROGBITS .init_array (https://golang.org/issue/50295) and
    // .init_array.N (https://github.com/rust-lang/rust/issues/92181) for a
    // while.
    return sec->reservedName;
  }
}
} // namespace

template <class ELFT>
void MarkLive<ELFT>::enqueue(InputSectionBase *sec, uint64_t offset) {
  // Usually, a whole section is marked as live or dead, but in mergeable
  // (splittable) sections, each piece of data has independent liveness bit.
  // So we explicitly tell it which offset is in use.
  if (auto *ms = dyn_cast<MergeInputSection>(sec))
    ms->getSectionPiece(offset).live = true;

  // Set Sec->Partition to the meet (i.e. the "minimum") of Partition and
  // Sec->Partition in the following lattice: 1 < other < 0. If Sec->Partition
  // doesn't change, we don't need to do anything.
  if (sec->partition == 1 || sec->partition == partition)
    return;
  sec->partition = sec->partition ? 1 : partition;

  // Add input section to the queue.
  if (InputSection *s = dyn_cast<InputSection>(sec))
    queue.push_back(s);
}

template <class ELFT> void MarkLive<ELFT>::markSymbol(Symbol *sym) {
  if (auto *d = dyn_cast_or_null<Defined>(sym))
    if (auto *isec = dyn_cast_or_null<InputSectionBase>(d->section))
      enqueue(isec, d->value);
}

// This is the main function of the garbage collector.
// Starting from GC-root sections, this function visits all reachable
// sections to set their "Live" bits.
template <class ELFT>
template <class RelTy>
void MarkLive<ELFT>::scanEhFrameParallel(EhInputSection &eh,
                                         ArrayRef<RelTy> rels,
                                         ParallelMarkTask &task) {
  // Mirrors scanEhFrameSection()/resolveReloc() for roots. No C-named section
  // is registered yet while .eh_frame roots are scanned.
  auto visit = [&](const RelTy &rel, bool fromFDE) {
    Symbol &sym = eh.getFile<ELFT>()->getRelocTargetSym(rel);
    setUsedAtomic(sym);
    if (auto *d = dyn_cast<Defined>(&sym)) {
      auto *relSec = dyn_cast_or_null<InputSectionBase>(d->section);
      if (!relSec)
        return;
      uint64_t offset = d->value;
      if (d->isSection())
        offset += getAddend<ELFT>(eh, rel);
      if (!(fromFDE && ((relSec->flags & (SHF_EXECINSTR | SHF_LINK_ORDER)) ||
                        relSec->nextInSectionGroup)))
        enqueueParallel(relSec, offset, task.stack);
      return;
    }
    if (auto *ss = dyn_cast<SharedSymbol>(&sym))
      if (!ss->isWeak()) {
        auto *file = cast<SharedFile>(ss->file);
        if (!file->isNeeded)
          task.neededFiles.push_back(file);
      }
  };
  for (const EhSectionPiece &cie : eh.cies)
    if (cie.firstRelocation != unsigned(-1))
      visit(rels[cie.firstRelocation], false);
  for (const EhSectionPiece &fde : eh.fdes) {
    size_t firstRelI = fde.firstRelocation;
    if (firstRelI == (unsigned)-1)
      continue;
    uint64_t pieceEnd = fde.inputOff + fde.size;
    for (size_t j = firstRelI, end2 = rels.size();
         j < end2 && rels[j].r_offset < pieceEnd; ++j)
      visit(rels[j], true);
  }
}

// Collect the main partition's GC roots with the workers, with the same
// result as the sequential code in run(). The three root groups (dynamic
// symbols, .eh_frame references, section properties) are gathered one after
// another, because a section's liveness at the end of one group decides
// whether the next group enqueues it; each group is parallel internally.
// Returns false, having done nothing, when a sequential ordering dependency
// inside the section group makes the parallel scan unsafe.
template <class ELFT> bool MarkLive<ELFT>::collectMainRootsParallel() {
  ArrayRef<InputSectionBase *> sections = elfState().inputSections;
  // A dependent (SHF_LINK_ORDER) section that is also SHF_GNU_RETAIN is either
  // scanned or merely marked live depending on its index relative to its
  // parent, so keep the sequential scan for such inputs.
  std::atomic<bool> retainedDependent{false};
  parallelFor(0, sections.size(), [&](size_t i) {
    for (InputSection *isec : sections[i]->dependentSections)
      if (isec->flags & SHF_GNU_RETAIN)
        retainedDependent.store(true, std::memory_order_relaxed);
  });
  if (retainedDependent.load())
    return false;

  const size_t tasks = size_t(parallelThreadCount()) * 4;
  std::vector<ParallelMarkTask> states(tasks);
  auto forChunks = [&](size_t count, auto &&body) {
    const size_t chunk = (count + tasks - 1) / tasks;
    parallelFor(0, tasks, [&](size_t t) {
      const size_t begin = t * chunk, end = std::min(count, begin + chunk);
      for (size_t i = begin; i < end; ++i)
        body(i, states[t]);
    });
  };

  // Externally visible symbols may be interposed at runtime.
  ArrayRef<Symbol *> symbols = symtab.getSymbols();
  forChunks(symbols.size(), [&](size_t i, ParallelMarkTask &task) {
    Symbol *sym = symbols[i];
    if (sym->includeInDynsym() && sym->partition == partition)
      if (auto *d = dyn_cast<Defined>(sym))
        if (auto *isec = dyn_cast_or_null<InputSectionBase>(d->section))
          enqueueParallel(isec, d->value, task.stack);
  });

  markSymbol(symtab.find(config->entry));
  markSymbol(symtab.find(config->init));
  markSymbol(symtab.find(config->fini));
  for (StringRef s : config->undefined)
    markSymbol(symtab.find(s));
  for (StringRef s : script->referencedSymbols)
    markSymbol(symtab.find(s));

  ArrayRef<EhInputSection *> ehSections = elfState().ehInputSections;
  forChunks(ehSections.size(), [&](size_t i, ParallelMarkTask &task) {
    EhInputSection *eh = ehSections[i];
    const RelsOrRelas<ELFT> rels = eh->template relsOrRelas<ELFT>();
    if (rels.areRelocsRel())
      scanEhFrameParallel(*eh, rels.rels, task);
    else if (rels.relas.size())
      scanEhFrameParallel(*eh, rels.relas, task);
  });

  // Section properties. Names for __start_/__stop_ lookups are recorded per
  // chunk and registered below in section order.
  std::vector<SmallVector<InputSectionBase *, 0>> cNamed(tasks);
  const size_t chunk = (sections.size() + tasks - 1) / tasks;
  parallelFor(0, tasks, [&](size_t t) {
    ParallelMarkTask &task = states[t];
    const size_t begin = t * chunk;
    const size_t end = std::min(sections.size(), begin + chunk);
    for (size_t i = begin; i < end; ++i) {
      InputSectionBase *sec = sections[i];
      if (sec->flags & SHF_GNU_RETAIN) {
        enqueueParallel(sec, 0, task.stack);
        continue;
      }
      if (sec->flags & SHF_LINK_ORDER)
        continue;
      // See run() for why non-SHF_ALLOC sections are retained unscanned.
      if (!(sec->flags & SHF_ALLOC)) {
        bool isRel = sec->type == SHT_REL || sec->type == SHT_RELA;
        if (!isRel && !sec->nextInSectionGroup) {
          claimMainPartition(sec);
          for (InputSection *isec : sec->dependentSections)
            claimMainPartition(isec);
        }
      }
      if (isReserved(sec)) {
        enqueueParallel(sec, 0, task.stack);
      } else if (sec->cIdentifierName &&
                 (!config->zStartStopGC || sec->name.starts_with("__libc_"))) {
        cNamed[t].push_back(sec);
      }
    }
  });
  for (SmallVector<InputSectionBase *, 0> &list : cNamed)
    for (InputSectionBase *sec : list) {
      cNamedSections[saver().save("__start_" + sec->name)].push_back(sec);
      cNamedSections[saver().save("__stop_" + sec->name)].push_back(sec);
    }

  for (ParallelMarkTask &task : states) {
    queue.append(task.stack.begin(), task.stack.end());
    for (SharedFile *file : task.neededFiles)
      file->isNeeded = true;
  }
  return true;
}

template <class ELFT> void MarkLive<ELFT>::run() {
  if (partition == 1 && parallelEnabled() && script->keptSections.empty() &&
      collectMainRootsParallel()) {
    mark();
    return;
  }

  // Add GC root symbols.

  // Preserve externally-visible symbols if the symbols defined by this
  // file can interpose other ELF file's symbols at runtime.
  for (Symbol *sym : symtab.getSymbols())
    if (sym->includeInDynsym() && sym->partition == partition)
      markSymbol(sym);

  // If this isn't the main partition, that's all that we need to preserve.
  if (partition != 1) {
    mark();
    return;
  }

  markSymbol(symtab.find(config->entry));
  markSymbol(symtab.find(config->init));
  markSymbol(symtab.find(config->fini));
  for (StringRef s : config->undefined)
    markSymbol(symtab.find(s));
  for (StringRef s : script->referencedSymbols)
    markSymbol(symtab.find(s));

  // Mark .eh_frame sections as live because there are usually no relocations
  // that point to .eh_frames. Otherwise, the garbage collector would drop
  // all of them. We also want to preserve personality routines and LSDA
  // referenced by .eh_frame sections, so we scan them for that here.
  for (EhInputSection *eh : elfState().ehInputSections) {
    const RelsOrRelas<ELFT> rels = eh->template relsOrRelas<ELFT>();
    if (rels.areRelocsRel())
      scanEhFrameSection(*eh, rels.rels);
    else if (rels.relas.size())
      scanEhFrameSection(*eh, rels.relas);
  }
  for (InputSectionBase *sec : elfState().inputSections) {
    if (sec->flags & SHF_GNU_RETAIN) {
      enqueue(sec, 0);
      continue;
    }
    if (sec->flags & SHF_LINK_ORDER)
      continue;

    // Usually, non-SHF_ALLOC sections are not removed even if they are
    // unreachable through relocations because reachability is not a good signal
    // whether they are garbage or not (e.g. there is usually no section
    // referring to a .comment section, but we want to keep it.) When a
    // non-SHF_ALLOC section is retained, we also retain sections dependent on
    // it.
    //
    // Note on SHF_LINK_ORDER: Such sections contain metadata and they
    // have a reverse dependency on the InputSection they are linked with.
    // We are able to garbage collect them.
    //
    // Note on SHF_REL{,A}: Such sections reach here only when -r
    // or --emit-reloc were given. And they are subject of garbage
    // collection because, if we remove a text section, we also
    // remove its relocation section.
    //
    // Note on nextInSectionGroup: The ELF spec says that group sections are
    // included or omitted as a unit. We take the interpretation that:
    //
    // - Group members (nextInSectionGroup != nullptr) are subject to garbage
    //   collection.
    // - Groups members are retained or discarded as a unit.
    if (!(sec->flags & SHF_ALLOC)) {
      bool isRel = sec->type == SHT_REL || sec->type == SHT_RELA;
      if (!isRel && !sec->nextInSectionGroup) {
        sec->markLive();
        for (InputSection *isec : sec->dependentSections)
          isec->markLive();
      }
    }

    // Preserve special sections and those which are specified in linker
    // script KEEP command.
    if (isReserved(sec) || script->shouldKeep(sec)) {
      enqueue(sec, 0);
    } else if (sec->cIdentifierName &&
               (!config->zStartStopGC || sec->name.starts_with("__libc_"))) {
      // As a workaround for glibc libc.a before 2.34
      // (https://sourceware.org/PR27492), retain __libc_atexit and similar
      // sections regardless of zStartStopGC.
      cNamedSections[saver().save("__start_" + sec->name)].push_back(sec);
      cNamedSections[saver().save("__stop_" + sec->name)].push_back(sec);
    }
  }

  mark();
}

template <class ELFT>
void MarkLive<ELFT>::enqueueParallel(InputSectionBase *sec, uint64_t offset,
                                     SmallVectorImpl<InputSection *> &stack) {
  if (auto *ms = dyn_cast<MergeInputSection>(sec))
    setPieceLiveAtomic(ms->getSectionPiece(offset));
  // Cheap pre-check; the exchange below decides ownership.
  if (loadPartition(sec) == 1 || claimMainPartition(sec) == 1)
    return;
  if (InputSection *s = dyn_cast<InputSection>(sec))
    stack.push_back(s);
}

template <class ELFT>
void MarkLive<ELFT>::scanParallel(InputSectionBase &sec,
                                  ParallelMarkTask &task) const {
  SmallVectorImpl<InputSection *> &stack = task.stack;
  auto visit = [&](const auto &rel) {
    Symbol &sym = sec.getFile<ELFT>()->getRelocTargetSym(rel);
    setUsedAtomic(sym);
    if (auto *d = dyn_cast<Defined>(&sym)) {
      if (auto *relSec = dyn_cast_or_null<InputSectionBase>(d->section)) {
        uint64_t offset = d->value;
        if (d->isSection())
          offset += getAddend<ELFT>(sec, rel);
        enqueueParallel(relSec, offset, stack);
      }
      return;
    }
    if (auto *ss = dyn_cast<SharedSymbol>(&sym))
      if (!ss->isWeak()) {
        auto *file = cast<SharedFile>(ss->file);
        if (!file->isNeeded)
          task.neededFiles.push_back(file);
      }
    if (!cNamedSections.empty()) {
      auto it = cNamedSections.find(sym.getName());
      if (it != cNamedSections.end())
        for (InputSectionBase *cSec : it->second)
          enqueueParallel(cSec, 0, stack);
    }
  };
  const RelsOrRelas<ELFT> rels = sec.template relsOrRelas<ELFT>();
  for (const typename ELFT::Rel &rel : rels.rels)
    visit(rel);
  for (const typename ELFT::Rela &rel : rels.relas)
    visit(rel);
  for (InputSectionBase *isec : sec.dependentSections)
    enqueueParallel(isec, 0, stack);
  if (sec.nextInSectionGroup)
    enqueueParallel(sec.nextInSectionGroup, 0, stack);
}

// Mark everything reachable from the queued main-partition roots using all
// workers. Reachability does not depend on visiting order, so the result is
// identical to the sequential walk. Each worker walks depth-first from its own
// stack and, while other workers are idle, hands half of its stack to a shared
// pool, so one root that reaches most of the program is still shared out
// without fork/join rounds. The walk ends when every started worker is idle
// and the pool is empty; a worker the pool starts later finds nothing to do.
template <class ELFT> void MarkLive<ELFT>::markMainPartitionParallel() {
  constexpr size_t seedChunk = 64;
  constexpr size_t shareThreshold = 32;
  struct WorkPool {
    std::mutex mutex;
    std::condition_variable wake;
    std::vector<SmallVector<InputSection *, 0>> chunks;
    unsigned started = 0;
    unsigned idle = 0;
    bool done = false;
    std::atomic<unsigned> idleHint{0};
  } pool;
  for (size_t i = 0; i < queue.size(); i += seedChunk)
    pool.chunks.emplace_back(queue.begin() + i,
                             queue.begin() +
                                 std::min(queue.size(), i + seedChunk));
  queue.clear();

  const size_t workers = parallelThreadCount();
  std::vector<ParallelMarkTask> states(workers);
  parallelFor(0, workers, [&](size_t w) {
    ParallelMarkTask &task = states[w];
    SmallVectorImpl<InputSection *> &stack = task.stack;
    {
      std::lock_guard<std::mutex> lock(pool.mutex);
      if (pool.done)
        return;
      ++pool.started;
    }
    for (;;) {
      if (stack.empty()) {
        std::unique_lock<std::mutex> lock(pool.mutex);
        ++pool.idle;
        pool.idleHint.fetch_add(1, std::memory_order_relaxed);
        pool.wake.wait(lock, [&] {
          return !pool.chunks.empty() || pool.done || pool.idle == pool.started;
        });
        if (pool.chunks.empty()) {
          pool.done = true;
          pool.wake.notify_all();
          return;
        }
        --pool.idle;
        pool.idleHint.fetch_sub(1, std::memory_order_relaxed);
        stack.assign(pool.chunks.back().begin(), pool.chunks.back().end());
        pool.chunks.pop_back();
        continue;
      }
      scanParallel(*stack.pop_back_val(), task);
      if (stack.size() >= shareThreshold &&
          pool.idleHint.load(std::memory_order_relaxed) != 0) {
        // Give away the older half: it is closer to the roots and so tends
        // to lead to more work.
        const size_t half = stack.size() / 2;
        SmallVector<InputSection *, 0> shared(stack.begin(),
                                              stack.begin() + half);
        stack.erase(stack.begin(), stack.begin() + half);
        std::lock_guard<std::mutex> lock(pool.mutex);
        pool.chunks.push_back(std::move(shared));
        pool.wake.notify_one();
      }
    }
  });
  for (ParallelMarkTask &task : states)
    for (SharedFile *file : task.neededFiles)
      file->isNeeded = true;
}

template <class ELFT> void MarkLive<ELFT>::mark() {
  if (partition == 1 && parallelEnabled() && !queue.empty()) {
    markMainPartitionParallel();
    return;
  }
  const size_t parallelThreshold = getParallelWorklistThreshold();
  const bool canParallelize = parallelEnabled();
  // Mark all reachable sections.
  while (!queue.empty()) {
    if (canParallelize && queue.size() >= parallelThreshold) {
      struct PendingReloc {
        Symbol *sym = nullptr;
        InputSectionBase *target = nullptr;
        uint64_t targetOffset = 0;
      };
      struct PendingRefs {
        SmallVector<PendingReloc, 0> relocs;
        SmallVector<InputSectionBase *, 0> dependentSections;
        InputSectionBase *nextInSectionGroup = nullptr;
      };

      SmallVector<InputSection *, 0> batch;
      size_t batchLimit = std::min(queue.size(), parallelThreshold * 8);
      batch.reserve(batchLimit);
      for (size_t i = 0; i < batchLimit && !queue.empty(); ++i)
        batch.push_back(queue.pop_back_val());

      std::vector<PendingRefs> pending(batch.size());
      parallelFor(0, batch.size(), [&](size_t i) {
        InputSectionBase &sec = *batch[i];
        PendingRefs &refs = pending[i];
        const RelsOrRelas<ELFT> rels = sec.template relsOrRelas<ELFT>();
        refs.relocs.reserve(rels.rels.size() + rels.relas.size());
        auto collectReloc = [&](const auto &rel) {
          Symbol &sym = sec.getFile<ELFT>()->getRelocTargetSym(rel);
          PendingReloc pendingReloc;
          pendingReloc.sym = &sym;
          if (auto *d = dyn_cast<Defined>(&sym)) {
            if (auto *relSec = dyn_cast_or_null<InputSectionBase>(d->section)) {
              uint64_t offset = d->value;
              if (d->isSection())
                offset += getAddend<ELFT>(sec, rel);
              pendingReloc.target = relSec;
              pendingReloc.targetOffset = offset;
            }
          }
          refs.relocs.push_back(pendingReloc);
        };
        for (const typename ELFT::Rel &rel : rels.rels)
          collectReloc(rel);
        for (const typename ELFT::Rela &rel : rels.relas)
          collectReloc(rel);
        refs.dependentSections.append(sec.dependentSections.begin(),
                                      sec.dependentSections.end());
        refs.nextInSectionGroup = sec.nextInSectionGroup;
      });

      for (size_t i = 0; i < batch.size(); ++i) {
        for (const PendingReloc &pendingReloc : pending[i].relocs) {
          Symbol *sym = pendingReloc.sym;
          sym->used = true;
          if (isa<Defined>(sym)) {
            if (pendingReloc.target)
              enqueue(pendingReloc.target, pendingReloc.targetOffset);
            continue;
          }
          if (auto *ss = dyn_cast<SharedSymbol>(sym))
            if (!ss->isWeak())
              cast<SharedFile>(ss->file)->isNeeded = true;
          for (InputSectionBase *cSec : cNamedSections.lookup(sym->getName()))
            enqueue(cSec, 0);
        }

        for (InputSectionBase *isec : pending[i].dependentSections)
          enqueue(isec, 0);
        if (pending[i].nextInSectionGroup)
          enqueue(pending[i].nextInSectionGroup, 0);
      }
      continue;
    }

    InputSectionBase &sec = *queue.pop_back_val();

    const RelsOrRelas<ELFT> rels = sec.template relsOrRelas<ELFT>();
    for (const typename ELFT::Rel &rel : rels.rels)
      resolveReloc(sec, rel, false);
    for (const typename ELFT::Rela &rel : rels.relas)
      resolveReloc(sec, rel, false);

    for (InputSectionBase *isec : sec.dependentSections)
      enqueue(isec, 0);

    // Mark the next group member.
    if (sec.nextInSectionGroup)
      enqueue(sec.nextInSectionGroup, 0);
  }
}

// Move the sections for some symbols to the main partition, specifically ifuncs
// (because they can result in an IRELATIVE being added to the main partition's
// GOT, which means that the ifunc must be available when the main partition is
// loaded) and TLS symbols (because we only know how to correctly process TLS
// relocations for the main partition).
//
// We also need to move sections whose names are C identifiers that are referred
// to from __start_/__stop_ symbols because there will only be one set of
// symbols for the whole program.
template <class ELFT> void MarkLive<ELFT>::moveToMain() {
  for (ELFFileBase *file : elfState().objectFiles)
    for (Symbol *s : file->getSymbols())
      if (auto *d = dyn_cast<Defined>(s))
        if ((d->type == STT_GNU_IFUNC || d->type == STT_TLS) && d->section &&
            d->section->isLive())
          markSymbol(s);

  for (InputSectionBase *sec : elfState().inputSections) {
    if (!sec->isLive() || !sec->cIdentifierName)
      continue;
    if (symtab.find(("__start_" + sec->name).str()) ||
        symtab.find(("__stop_" + sec->name).str()))
      enqueue(sec, 0);
  }

  mark();
}

// Before calling this function, Live bits are off for all
// input sections. This function make some or all of them on
// so that they are emitted to the output file.
template <class ELFT> void elf::markLive() {
  llvm::TimeTraceScope timeScope("markLive");
  // If --gc-sections is not given, retain all input sections.
  if (!config->gcSections) {
    // If a DSO defines a symbol referenced in a regular object, it is needed.
    for (Symbol *sym : symtab.getSymbols())
      if (auto *s = dyn_cast<SharedSymbol>(sym))
        if (s->isUsedInRegularObj && !s->isWeak())
          cast<SharedFile>(s->file)->isNeeded = true;
    return;
  }

  for (InputSectionBase *sec : elfState().inputSections)
    sec->markDead();

  // Follow the graph to mark all live sections.
  for (unsigned curPart = 1; curPart <= partitions.size(); ++curPart)
    MarkLive<ELFT>(curPart).run();

  // If we have multiple partitions, some sections need to live in the main
  // partition even if they were allocated to a loadable partition. Move them
  // there now.
  if (partitions.size() != 1)
    MarkLive<ELFT>(1).moveToMain();

  // Report garbage-collected sections.
  if (config->printGcSections)
    for (InputSectionBase *sec : elfState().inputSections)
      if (!sec->isLive())
        message("removing unused section " + toString(sec));
}

template void elf::markLive<ELF64LE>();
template void elf::markLive<ELF64BE>();
