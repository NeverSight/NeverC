#ifndef NEVERC_FOUNDATION_ANDROIDKERNELMODULESYMBOLPOLICY_H
#define NEVERC_FOUNDATION_ANDROIDKERNELMODULESYMBOLPOLICY_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace neverc::AndroidKernelModuleSymbolPolicy {

/// Linux reserves this OS-specific section index for unresolved livepatch
/// references.  Their names are consumed later by livepatch rather than by the
/// ordinary module import resolver.
inline constexpr uint16_t LivePatchSectionIndex = 0xff20;
inline constexpr uint64_t LivePatchRelocationSectionFlag = 0x00100000;

inline bool isLivePatchSectionName(llvm::StringRef Name) {
  return Name.starts_with(".klp.");
}

/// A livepatch module is identified authoritatively by its NUL-separated
/// `.modinfo` entry, even when it has no livepatch relocation sections yet.
/// Treat any value for the reserved key as the marker; the kernel only needs
/// the key to classify the artifact.
inline bool containsLivePatchModInfo(llvm::ArrayRef<uint8_t> Bytes) {
  for (size_t Offset = 0; Offset < Bytes.size();) {
    size_t End = Offset;
    while (End < Bytes.size() && Bytes[End] != 0)
      ++End;
    llvm::StringRef Entry(reinterpret_cast<const char *>(Bytes.data() + Offset),
                          End - Offset);
    if (Entry.starts_with("livepatch="))
      return true;
    Offset = End < Bytes.size() ? End + 1 : End;
  }
  return false;
}

/// The name-relevant ELF definition classes.  Keeping raw SHN_* constants out
/// of this interface lets the typed plugin ObjectGraph and the native ELF
/// merger consume exactly the same release policy.
enum class SymbolClass : uint8_t {
  Defined,
  Absolute,
  Undefined,
  Common,
  LivePatch,
};

/// Sections that must stay distinct through the Android kernel relocatable
/// link. `.plt` and `.init.plt` are loader/architecture structures, but their
/// ordinary symbol names are still eligible for structural release renaming.
inline constexpr std::array<llvm::StringLiteral, 7> MergePreservedSections = {
    ".modinfo",
    "__versions",
    ".codetag.alloc_tags",
    ".gnu.linkonce.this_module",
    ".plt",
    ".init.plt",
    ".text.ftrace_trampoline",
};

inline constexpr std::array<llvm::StringLiteral, 5>
    SymbolNamePreservedSections = {
        ".modinfo",   ".text.ftrace_trampoline", ".gnu.linkonce.this_module",
        "__versions", ".codetag.alloc_tags",
};

inline constexpr std::array<llvm::StringLiteral, 6> PreservedSymbolNames = {
    "init_module",      "cleanup_module",       "__cfi_check",
    "__cfi_check_fail", "__cfi_jt_init_module", "__cfi_jt_cleanup_module",
};

inline bool preservesSectionFromMerging(llvm::StringRef Name) {
  for (llvm::StringRef Preserved : MergePreservedSections)
    if (Name == Preserved)
      return true;
  return false;
}

/// Sections whose symbol names participate in Android/Linux module-loader ABI
/// or in the explicit release compatibility contract.  Section names
/// themselves are never rewritten; names of symbols defined in these sections
/// stay exact as well.
inline bool preservesSymbolNamesInSection(llvm::StringRef Name) {
  for (llvm::StringRef Preserved : SymbolNamePreservedSections)
    if (Name == Preserved)
      return true;
  return false;
}

/// Names looked up directly by supported Android kernels, or explicitly kept
/// as module entry/CFI ABI.  Android's pre-KCFI CFI loader also looks up both
/// __cfi_jt_* objects through module kallsyms before calling init/exit.
inline bool preservesSymbolName(llvm::StringRef Name) {
  for (llvm::StringRef Preserved : PreservedSymbolNames)
    if (Name == Preserved)
      return true;
  return Name.starts_with("__typeid__") || Name.starts_with("__kcfi_typeid_");
}

/// Whether one retained release symbol owns its original spelling exactly.
/// This is shared by the planner and the independent native replay so the
/// boundary between singleton names and exchangeable generated-name classes
/// cannot drift as new loader/CFI spellings are added.
inline bool hasExactReleaseName(llvm::StringRef Name, SymbolClass Class,
                                bool IsSectionSymbol, bool PreserveName) {
  return Class == SymbolClass::Undefined || IsSectionSymbol || PreserveName ||
         Name.empty() || preservesSymbolName(Name);
}

} // namespace neverc::AndroidKernelModuleSymbolPolicy

#endif // NEVERC_FOUNDATION_ANDROIDKERNELMODULESYMBOLPOLICY_H
