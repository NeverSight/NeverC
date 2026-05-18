#ifndef LINKER_ELF_RELOCATIONS_H
#define LINKER_ELF_RELOCATIONS_H

#include "Linker/Core/Support/LlvmAliases.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include <vector>

namespace linker::elf {
class Symbol;
class InputSection;
class InputSectionBase;
class OutputSection;
class SectionBase;

// Represents a relocation type, such as R_X86_64_PC32 or R_AARCH64_CALL26.
using RelType = uint32_t;
using JumpModType = uint32_t;

// List of target-independent relocation types. Relocations read
// from files are converted to these types so that the main code
// doesn't have to know about architecture-specific details.
enum RelExpr {
  R_ABS,
  R_ADDEND,
  R_DTPREL,
  R_GOT,
  R_GOT_OFF,
  R_GOT_PC,
  R_GOTONLY_PC,
  R_GOTPLTONLY_PC,
  R_GOTPLT,
  R_GOTPLTREL,
  R_GOTREL,
  R_NONE,
  R_PC,
  R_PLT,
  R_PLT_PC,
  R_PLT_GOTPLT,
  R_RELAX_HINT,
  R_RELAX_GOT_PC,
  R_RELAX_GOT_PC_NOPIC,
  R_RELAX_TLS_GD_TO_IE,
  R_RELAX_TLS_GD_TO_IE_ABS,
  R_RELAX_TLS_GD_TO_IE_GOT_OFF,
  R_RELAX_TLS_GD_TO_IE_GOTPLT,
  R_RELAX_TLS_GD_TO_LE,
  R_RELAX_TLS_GD_TO_LE_NEG,
  R_RELAX_TLS_IE_TO_LE,
  R_RELAX_TLS_LD_TO_LE,
  R_RELAX_TLS_LD_TO_LE_ABS,
  R_SIZE,
  R_TPREL,
  R_TPREL_NEG,
  R_TLSDESC,
  R_TLSDESC_CALL,
  R_TLSDESC_PC,
  R_TLSDESC_GOTPLT,
  R_TLSGD_GOT,
  R_TLSGD_GOTPLT,
  R_TLSGD_PC,
  R_TLSIE_HINT,
  R_TLSLD_GOT,
  R_TLSLD_GOTPLT,
  R_TLSLD_GOT_OFF,
  R_TLSLD_HINT,
  R_TLSLD_PC,

  // The following is abstract relocation types used for only one target.
  //
  // Even though RelExpr is intended to be a target-neutral representation
  // of a relocation type, there are some relocations whose semantics are
  // unique to a target. Such relocation are marked with R_<TARGET_NAME>.
  R_AARCH64_GOT_PAGE_PC,
  R_AARCH64_GOT_PAGE,
  R_AARCH64_PAGE_PC,
  R_AARCH64_RELAX_TLS_GD_TO_IE_PAGE_PC,
  R_AARCH64_TLSDESC_PAGE,
};

// Architecture-neutral representation of relocation.
struct Relocation {
  RelExpr expr;
  RelType type;
  uint64_t offset;
  int64_t addend;
  Symbol *sym;
};

// Manipulate jump instructions with these modifiers.  These are used to relax
// jump instruction opcodes at basic block boundaries and are particularly
// useful when basic block sections are enabled.
struct JumpInstrMod {
  uint64_t offset;
  JumpModType original;
  unsigned size;
};

// This function writes undefined symbol diagnostics to an internal buffer.
// Call reportUndefinedSymbols() after calling scanRelocations() to emit
// the diagnostics.
template <class ELFT> void scanRelocations();
void reportUndefinedSymbols();
void postScanRelocations();
void addGotEntry(Symbol &sym);

class ThunkSection;
class Thunk;
class InputSectionDescription;

class ThunkCreator {
public:
  // Return true if Thunks have been added to OutputSections
  bool createThunks(uint32_t pass, ArrayRef<OutputSection *> outputSections);

private:
  void mergeThunks(ArrayRef<OutputSection *> outputSections);

  ThunkSection *getISDThunkSec(OutputSection *os, InputSection *isec,
                               InputSectionDescription *isd,
                               const Relocation &rel, uint64_t src);

  ThunkSection *getISThunkSec(InputSection *isec);

  void createInitialThunkSections(ArrayRef<OutputSection *> outputSections);

  std::pair<Thunk *, bool> getThunk(InputSection *isec, Relocation &rel,
                                    uint64_t src);

  ThunkSection *addThunkSection(OutputSection *os, InputSectionDescription *,
                                uint64_t off);

  bool normalizeExistingThunk(Relocation &rel, uint64_t src);

  // Record all the available Thunks for a (Symbol, addend) pair, where Symbol
  // is represented as a (section, offset) pair. There may be multiple
  // relocations sharing the same (section, offset + addend) pair. We may revert
  // a relocation back to its original non-Thunk target, and restore the
  // original addend, so we cannot fold offset + addend. A nested pair is used
  // because DenseMapInfo is not specialized for std::tuple.
  llvm::DenseMap<std::pair<std::pair<SectionBase *, uint64_t>, int64_t>,
                 std::vector<Thunk *>>
      thunkedSymbolsBySectionAndAddend;
  llvm::DenseMap<std::pair<Symbol *, int64_t>, std::vector<Thunk *>>
      thunkedSymbols;

  // Find a Thunk from the Thunks symbol definition, we can use this to find
  // the Thunk from a relocation to the Thunks symbol definition.
  llvm::DenseMap<Symbol *, Thunk *> thunks;

  // Track InputSections that have an inline ThunkSection placed in front
  // an inline ThunkSection may have control fall through to the section below
  // so we need to make sure that there is only one of them.
  // An inline ThunkSection is placed immediately before its target.
  llvm::DenseMap<InputSection *, ThunkSection *> thunkedSections;

  // The number of completed passes of createThunks this permits us
  // to do one time initialization on Pass 0 and put a limit on the
  // number of times it can be called to prevent infinite loops.
  uint32_t pass = 0;
};

// Return a int64_t to make sure we get the sign extension out of the way as
// early as possible.
template <class ELFT>
static inline int64_t getAddend(const typename ELFT::Rel &rel) {
  return 0;
}
template <class ELFT>
static inline int64_t getAddend(const typename ELFT::Rela &rel) {
  return rel.r_addend;
}

template <typename RelTy>
ArrayRef<RelTy> sortRels(ArrayRef<RelTy> rels, SmallVector<RelTy, 0> &storage) {
  auto cmp = [](const RelTy &a, const RelTy &b) {
    return a.r_offset < b.r_offset;
  };
  if (!llvm::is_sorted(rels, cmp)) {
    storage.assign(rels.begin(), rels.end());
    llvm::stable_sort(storage, cmp);
    rels = storage;
  }
  return rels;
}

// Returns true if Expr refers a GOT entry. Note that this function returns
// false for TLS variables even though they need GOT, because TLS variables uses
// GOT differently than the regular variables.
bool needsGot(RelExpr expr);
} // namespace linker::elf

#endif
