#ifndef NEVERC_PLUGIN_HOST_NATIVEELFSECTIONFACTS_H
#define NEVERC_PLUGIN_HOST_NATIVEELFSECTIONFACTS_H

#include "neverc/Plugin/Host/ObjectSectionRole.h"
#include "neverc/Plugin/PluginObject.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/ELF.h"

#include <cstdint>

namespace neverc::plugin {

/// Stable ObjectGraph facts produced by BuiltinLLVMObjectReader for one ELF
/// section. Canonical-release validation reuses this projection so its native
/// provenance audit cannot drift from the reader as new native section types
/// are admitted.
struct NativeELFSectionProjection {
  NevercObjectSectionKind Kind = NEVERC_OBJECT_SECTION_KIND_FORMAT_EXTENSION;
  NevercObjectSectionFlags Flags = 0;
};

inline bool isELFDebugSectionName(llvm::StringRef Name) {
  // ELFObjectFile::isDebugSection recognizes .gdb_index in addition to the
  // portable name families shared by the other object adapters.
  return isDebugSectionName(BuiltinObjectFormat::ELF, Name) ||
         Name == ".gdb_index";
}

inline NativeELFSectionProjection
projectNativeELFSection(llvm::StringRef Name, uint64_t Type, uint64_t Flags) {
  const bool Allocated = (Flags & llvm::ELF::SHF_ALLOC) != 0;
  const bool Writable = (Flags & llvm::ELF::SHF_WRITE) != 0;
  const bool Executable = (Flags & llvm::ELF::SHF_EXECINSTR) != 0;
  const bool TLS = (Flags & llvm::ELF::SHF_TLS) != 0;
  const bool ZeroFill = Type == llvm::ELF::SHT_NOBITS;

  NativeELFSectionProjection Result;
  if (isELFDebugSectionName(Name))
    Result.Kind = NEVERC_OBJECT_SECTION_KIND_DEBUG;
  else if (isUnwindSectionName(BuiltinObjectFormat::ELF, Name))
    Result.Kind = NEVERC_OBJECT_SECTION_KIND_UNWIND;
  else if (TLS)
    Result.Kind = ZeroFill ? NEVERC_OBJECT_SECTION_KIND_TLS_ZERO_FILL
                           : NEVERC_OBJECT_SECTION_KIND_TLS_DATA;
  else if (Executable)
    Result.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  else if (ZeroFill)
    Result.Kind = NEVERC_OBJECT_SECTION_KIND_ZERO_FILL;
  else {
    // These are the exact ELFObjectFile predicates used by sectionKind():
    // isData(), then isBerkeleyData(), then isBerkeleyText(). Their order is
    // observable for allocated SHT_PROGBITS read-only sections.
    const bool IsData = Type == llvm::ELF::SHT_PROGBITS && Allocated;
    const bool IsBerkeleyText = Allocated && !Writable;
    const bool IsBerkeleyData =
        !IsBerkeleyText && Type != llvm::ELF::SHT_NOBITS && Allocated;
    if (IsData || IsBerkeleyData)
      Result.Kind = NEVERC_OBJECT_SECTION_KIND_DATA;
    else if (IsBerkeleyText)
      Result.Kind = NEVERC_OBJECT_SECTION_KIND_READ_ONLY_DATA;
  }

  if (Allocated)
    Result.Flags |= NEVERC_OBJECT_SECTION_ALLOCATED;
  if (Writable)
    Result.Flags |= NEVERC_OBJECT_SECTION_WRITABLE;
  if (Executable)
    Result.Flags |= NEVERC_OBJECT_SECTION_EXECUTABLE;
  if ((Flags & llvm::ELF::SHF_MERGE) != 0)
    Result.Flags |= NEVERC_OBJECT_SECTION_MERGEABLE;
  if ((Flags & llvm::ELF::SHF_STRINGS) != 0)
    Result.Flags |= NEVERC_OBJECT_SECTION_STRINGS;
  if (TLS)
    Result.Flags |= NEVERC_OBJECT_SECTION_WRITABLE | NEVERC_OBJECT_SECTION_TLS;
  if (Result.Kind == NEVERC_OBJECT_SECTION_KIND_DEBUG)
    Result.Flags |= NEVERC_OBJECT_SECTION_DEBUG;
  if (Result.Kind == NEVERC_OBJECT_SECTION_KIND_UNWIND)
    Result.Flags |= NEVERC_OBJECT_SECTION_UNWIND;
  return Result;
}

} // namespace neverc::plugin

#endif // NEVERC_PLUGIN_HOST_NATIVEELFSECTIONFACTS_H
