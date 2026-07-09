//===- MergerCommon.h - Internal helpers for object mergers ----===//
//
// Helpers shared across the per-format mergers.  Kept private to the
// `neverc/lib/Merge/` translation unit so the public API in
// `Merge/Merger.h` stays minimal.
//
//===-----------------------------------------------------------===//

#ifndef NEVERC_LIB_MERGE_COMMON_MERGERCOMMON_H
#define NEVERC_LIB_MERGE_COMMON_MERGERCOMMON_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ErrorHandling.h"

#include <csetjmp>

namespace neverc::merge::detail {

/// Canonical output section name for the ELF `mergeSections` mode: per-symbol
/// sections (`.text.foo`, `.bss.bar`, ...) collapse to their umbrella section
/// so symbol names stop leaking through section names (Android kernel modules).
///
/// This is the *single* source of truth for that renaming.  The merger applies
/// it while laying out sections (ELF/MergerELF.cpp) and the independent verifier
/// applies it to predict where a symbol should land (Verify/MergerVerify.cpp); keeping
/// them as two hand-synced copies risked one drifting from the other and either
/// false-rejecting a good merge or masking a real offset bug.  When
/// \p MergeSections is false, or the name is empty, or it is in \p Preserved,
/// the name is returned unchanged.
inline llvm::StringRef
canonicalELFSectionName(llvm::StringRef Name, bool MergeSections,
                        llvm::ArrayRef<llvm::StringRef> Preserved) {
  if (!MergeSections || Name.empty())
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

/// True if \p Key is one of the two values an \c llvm::DenseMap with an integer
/// key reserves for its empty / tombstone sentinels (~0 and ~0 - 1).  Section
/// indices, symbol indices and relocation targets read from a hostile or
/// malformed object can be exactly these, and calling \c DenseMap::find() /
/// \c lookup() with a reserved key is undefined behavior: it can alias an empty
/// bucket and hand back an uninitialized "value", which the caller then uses as
/// an array index — an out-of-bounds read (the merge fuzzer hit this as a BUS in
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
/// alternative — abort() — is strictly worse, and (b) malformed-input paths
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
