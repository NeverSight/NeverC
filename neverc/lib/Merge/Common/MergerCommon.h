//===- MergerCommon.h - Internal helpers for object mergers ----===//
//
// Helpers shared across the per-format mergers.  Kept private to the
// `neverc/lib/Merge/` translation unit so the public API in
// `Merge/Merger.h` stays minimal.
//
//===-----------------------------------------------------------===//

#ifndef NEVERC_LIB_MERGE_COMMON_MERGERCOMMON_H
#define NEVERC_LIB_MERGE_COMMON_MERGERCOMMON_H

#include "neverc/Foundation/AndroidKernelProfileContract.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Support/ErrorHandling.h"

#include <csetjmp>

namespace neverc::merge::detail {

/// NeverC intermediate-object tooling metadata.  Present on compiler-produced
/// `.o` files for profile/KCFI equality checks; the final Android `.ko` merge
/// must drop it so the delivered module has no NeverC-branded section.
inline bool isAndroidKernelProfileContractSection(llvm::StringRef Name) {
  return Name == neverc::AndroidKernelProfileContract::NativeSection;
}

inline bool isAndroidKernelProfileContractSymbol(llvm::StringRef Name) {
  return Name == neverc::AndroidKernelProfileContract::NativeSymbol;
}

/// Section-name predicates shared by the ELF merger and its independent
/// verifier.  Keeping the discard policy here prevents the producer from
/// silently growing a broader release policy than the verifier models.
inline bool isELFDebugSection(llvm::StringRef Name) {
  return Name == ".debug" || Name.starts_with(".debug_") ||
         Name.starts_with(".zdebug_");
}

/// Non-loader metadata intentionally omitted from a release Android module.
/// This list is deliberately narrow: module ABI sections such as
/// `.codetag.alloc_tags`, `__versions`, and `.gnu.linkonce.this_module` are
/// not cosmetic and must never be classified here.
inline bool
isAndroidKernelReleaseDiscardableSection(llvm::StringRef Name) {
  return Name == ".comment";
}

/// Canonical output section name for the ELF `mergeSections` mode: per-symbol
/// sections (`.text.foo`, `.bss.bar`, ...) collapse to their umbrella section
/// so symbol names stop leaking through section names (Android kernel modules).
///
/// This is the *single* source of truth for that renaming.  The merger applies
/// it while laying out sections (ELF/MergerELF.cpp) and the independent verifier
/// applies it to predict where a symbol should land (Verify/MergerVerify.cpp); keeping
/// them as two hand-synced copies risked one drifting from the other and either
/// false-rejecting a good merge or masking a real offset bug.  When
/// \p MergeSections is false, the name is empty, it is in \p Preserved, or the
/// section has SHF_MERGE semantics, the name is returned unchanged.  Mergeable
/// constant/string sections (for example `.rodata.str1.1`) must retain their
/// distinct names and flags; folding them onto ordinary `.rodata` would create
/// duplicate output section names because those flag sets are incompatible.
inline llvm::StringRef
canonicalELFSectionName(llvm::StringRef Name, uint64_t Flags,
                        bool MergeSections, bool AndroidKernelModule,
                        llvm::ArrayRef<llvm::StringRef> Preserved) {
  // scripts/module.lds.S routes the compiler-facing `alloc_tags` input
  // section into the loader-facing `.codetag.alloc_tags` output section.  The
  // in-process `-r` merger is the final linker for NeverC modules, so it must
  // perform the same mapping.  This is deliberately independent of generic
  // per-symbol folding: it is an Android module ABI contract.
  if (AndroidKernelModule && Name == "alloc_tags")
    return ".codetag.alloc_tags";
  if (!MergeSections || Name.empty() || (Flags & llvm::ELF::SHF_MERGE))
    return Name;
  for (llvm::StringRef P : Preserved)
    if (Name == P)
      return Name;
  if (Name.starts_with(".text."))
    return ".text";
  if (Name.starts_with(".bss."))
    return ".bss";
  if (Name.starts_with(".data."))
    return ".data";
  if (Name.starts_with(".rodata."))
    return ".rodata";
  return Name;
}

/// Whether a section holds the call graph profile, which no merge can carry
/// with it.
///
/// Each of its entries names two functions and the weight of the edge between
/// them, and on COFF and Mach-O the two functions are written as raw symbol
/// *table indices* (WinCOFFObjectWriter.cpp, MachObjectWriter.cpp) where ELF
/// writes relocations.  A merge builds one symbol table out of all its inputs,
/// so every such index comes out of it naming whatever symbol now sits at that
/// position: the section stays well-formed and describes a different call
/// graph than it went in with, and the linker orders the image by it.  The
/// original numbering is gone by the time the bytes are copied, so nothing
/// downstream can repair them.  Parallel codegen compounds it -- the profile
/// belongs to the module rather than to any symbol in it, so the merge is
/// handed one copy per partition.
///
/// Dropping it is therefore the only answer that cannot be wrong, and it is
/// the one ELF already gives: MergerELF.cpp skips SHT_LLVM_CALL_GRAPH_PROFILE
/// outright, on the separate grounds that it is linker metadata `-r` does not
/// carry.  ELF can say this by section type; COFF and Mach-O have only the
/// name codegen gave the section, so they ask here.
inline bool isCOFFCallGraphProfileSection(llvm::StringRef Name) {
  return Name == ".llvm.call-graph-profile";
}

inline bool isMachOCallGraphProfileSection(llvm::StringRef SegmentName,
                                           llvm::StringRef SectionName) {
  return SegmentName == "__LLVM" && SectionName == "__cg_profile";
}

/// Whether the bytes a Mach-O relocation of type \p Type covers on \p CpuType
/// are a word of their own, so that adjusting the relocation can add a delta
/// to the whole field.
///
/// The Mach-O merger rewrites section-relative relocation sites in place, and
/// "not PC-relative" used to stand in for "addresses a word of its own".  It
/// does not: ARM64_RELOC_PAGEOFF12 is not PC-relative either, and its field is
/// the twelve-bit immediate inside an `add` or a load -- bits 10 through 21 of
/// the instruction word, with the destination and source registers below it
/// and the opcode above.  Adding a delta to the word writes through all of
/// that: a small delta lands in the register fields and changes which
/// registers the instruction reads, a larger one reaches the immediate at the
/// wrong bit position.  The object stays well-formed and the self-check skips
/// relocation sites, so neither notices.  Only the pointer forms hold a word.
///
/// The object-graph layer states the same fact in
/// `Plugin/Host/NativeRelocationFacts.h`, which cannot be included here: this
/// library is deliberately free of the plugin ABI so the linker backends and
/// the parallel-codegen pipeline can share it.  The two have to agree on every
/// type, and MergeTests.cpp checks that they do.
inline bool machOFieldIsWholeWord(uint32_t CpuType, uint8_t Type) {
  namespace MO = llvm::MachO;
  if (CpuType == MO::CPU_TYPE_ARM64)
    switch (Type) {
    case MO::ARM64_RELOC_UNSIGNED:
    case MO::ARM64_RELOC_SUBTRACTOR:
    case MO::ARM64_RELOC_POINTER_TO_GOT:
    case MO::ARM64_RELOC_ADDEND:
      return true;
    default:
      return false;
    }
  // Every x86_64 relocation covers a displacement or immediate that occupies
  // whole bytes of its own, even where those bytes sit inside an instruction.
  return CpuType == MO::CPU_TYPE_X86_64;
}

/// True if \p Key is one of the two values an \c llvm::DenseMap with an integer
/// key reserves for its empty / tombstone sentinels (~0 and ~0 - 1).  Section
/// indices, symbol indices and relocation targets read from a hostile or
/// malformed object can be exactly these, and calling \c DenseMap::find() /
/// \c lookup() with a reserved key is undefined behavior: it can alias an empty
/// bucket and hand back an uninitialized "value", which the caller then uses as
/// an array index -- an out-of-bounds read (the merge fuzzer hit this as a BUS in
/// the COFF verifier's symbol-by-index lookup).  Every lookup whose key
/// originates in untrusted file bytes must treat a reserved key as "absent".
template <typename T> inline bool isReservedDenseKey(T Key) {
  return Key == ~T(0) || Key == T(~T(0) - 1);
}

/// Deduplicating string table: each unique payload is appended once and
/// callers receive a 4-byte offset that they can splat into ELF/MachO/COFF
/// `*_strx` fields.  Index 0 is reserved as the empty string per the ELF
/// strtab convention.
struct DedupStrTab {
  llvm::SmallVector<char, 0> Data;
  llvm::StringMap<uint32_t> Index;

  DedupStrTab() : Data{'\0'} {}

  uint32_t add(llvm::StringRef S) {
    if (S.empty())
      return 0;
    auto [It, Inserted] = Index.try_emplace(S, 0);
    if (!Inserted)
      return It->second;
    uint32_t Off = Data.size();
    Data.append(S.begin(), S.end());
    Data.push_back('\0');
    It->second = Off;
    return Off;
  }
};

/// Run a merge callable under a fatal-error recovery guard.
///
/// LLVM's MachO and COFF object parsers call report_fatal_error() → abort()
/// on some malformed-input paths instead of returning Error through Expected<>.
/// The canonical example is getStruct() in MachOObjectFile.cpp (whose FIXME
/// reads "Replace all uses of this function with getStructOrErr").
///
/// This wrapper installs a temporary fatal-error handler that longjmp's back
/// here so the merge returns false (= caller falls back to serial codegen /
/// a real linker) instead of killing the process.  If another handler is
/// already installed (e.g. the compiler frontend's directLLVMErrorHandler
/// during !parallelSafe compilation), we skip installation and rely on the
/// caller's CrashRecoveryContext for crash isolation.
///
/// Caution: longjmp across C++ stack frames with non-trivial destructors is
/// technically UB.  In practice it only leaks memory (the objects are not
/// destructed), which is acceptable because (a) report_fatal_error's default
/// alternative -- abort() -- is strictly worse, and (b) malformed-input paths
/// that trigger this are rare.
template <typename F>
inline bool runMergeSafely(F &&Fn) {
  if (ErrorHandler != nullptr)
    return Fn();

  static thread_local std::jmp_buf FatalBuf;
  llvm::install_fatal_error_handler(
      +[](void *, const char *, bool) { std::longjmp(FatalBuf, 1); },
      nullptr);
  if (setjmp(FatalBuf) != 0) {
    llvm::remove_fatal_error_handler();
    return false;
  }
  bool Result = Fn();
  llvm::remove_fatal_error_handler();
  return Result;
}

} // namespace neverc::merge::detail

#endif // NEVERC_LIB_MERGE_COMMON_MERGERCOMMON_H
