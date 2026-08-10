#ifndef NEVERC_FOUNDATION_ANDROIDKERNELMODULERELEASENAMES_H
#define NEVERC_FOUNDATION_ANDROIDKERNELMODULERELEASENAMES_H

#include "neverc/Foundation/AndroidKernelModuleSymbolPolicy.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#include <cstdint>
#include <string>

namespace neverc {

enum class ReleaseNameKind : uint8_t {
  Function,
  Object,
  ExecutableLabel,
  Other,
  Absolute,
};

enum class ReleaseSymbolType : uint8_t {
  NoType,
  Object,
  Function,
  Section,
  File,
  TLS,
  GNUIFunc,
  FormatExtension,
};

struct ReleaseSectionDescriptor {
  uint64_t ID;
  uint64_t FinalOrdinal;
  uint64_t Alignment;
  uint64_t Size;
  bool Allocated;
  bool Executable;
};

struct ReleaseSectionLayout {
  uint64_t SectionID;
  uint64_t FinalOrdinal;
  uint64_t Base;
};

struct ReleaseSymbolDescriptor {
  uint64_t ID;
  llvm::StringRef OriginalName;
  AndroidKernelModuleSymbolPolicy::SymbolClass Class;
  ReleaseSymbolType Type;
  uint64_t SectionID;
  uint64_t Value;
  uint64_t Size;
  uint32_t BindingRank;
  /// Complete output-observable symbol-other value. Native ELF adapters pass
  /// the full st_other byte, including architecture-specific high bits.
  uint32_t OtherValue;
  bool PreserveName;
};

struct ReleaseSymbolRename {
  uint64_t SymbolID;
  std::string OutputName;
};

/// One ownership class for serialized release names. Exact names form
/// singleton classes; generated names may exchange only within a class whose
/// members have the complete same observable structural key.
struct ReleaseSymbolExchangeClass {
  llvm::SmallVector<uint64_t, 4> SymbolIDs;
};

/// Formats the unsuffixed, IDA-inspired structural spelling for an ordinary
/// release symbol. NeverC deliberately avoids IDA's reserved `sub_` and `loc_`
/// dummy-name prefixes because IDA escapes user symbols that use them. Size is
/// deliberately ignored: an ELF object size is not an IDA data type.
std::string formatReleaseName(ReleaseNameKind Kind, uint64_t Coordinate,
                              uint64_t Size);

/// Returns true when Name has the strict spelling emitted by the canonical
/// Android kernel release-name planner.  This checks grammar only; use
/// auditAndroidKernelReleaseNames to prove the structural assignment.
inline bool hasCanonicalReleaseNameShape(llvm::StringRef Name) {
  auto IsUpperHex = [](llvm::StringRef Value) {
    if (Value.empty() || (Value.size() > 1 && Value.front() == '0'))
      return false;
    return llvm::all_of(Value, [](char C) {
      return (C >= '0' && C <= '9') || (C >= 'A' && C <= 'F');
    });
  };
  auto IsDecimalSuffix = [](llvm::StringRef Value) {
    if (Value.empty() || Value.front() == '0')
      return false;
    return llvm::all_of(Value, [](char C) { return C >= '0' && C <= '9'; });
  };
  auto IsCoordinate = [&](llvm::StringRef Value) {
    const auto [Coordinate, Suffix] = Value.split('_');
    if (!IsUpperHex(Coordinate))
      return false;
    if (!Value.contains('_'))
      return true;
    return !Suffix.contains('_') && IsDecimalSuffix(Suffix);
  };

  for (llvm::StringRef Prefix :
       {llvm::StringRef("fn_"), llvm::StringRef("obj_"),
        llvm::StringRef("code_"), llvm::StringRef("abs_")})
    if (Name.consume_front(Prefix))
      return IsCoordinate(Name);

  if (!Name.consume_front("sym_"))
    return false;
  if (!Name.consume_front("S"))
    return IsCoordinate(Name);

  const auto [SectionOrdinal, Rest] = Name.split('_');
  if (!Name.contains('_') || !IsUpperHex(SectionOrdinal))
    return false;
  return IsCoordinate(Rest);
}

llvm::Expected<llvm::SmallVector<ReleaseSectionLayout, 16>>
computeAndroidKernelReleaseSectionLayout(
    llvm::ArrayRef<ReleaseSectionDescriptor> Sections);

llvm::Expected<llvm::SmallVector<ReleaseSymbolRename, 32>>
planAndroidKernelReleaseNames(llvm::ArrayRef<ReleaseSectionDescriptor> Sections,
                              llvm::ArrayRef<ReleaseSymbolDescriptor> Symbols);

/// Returns the planner's canonical name-ownership classes. This is the single
/// source used by both plan auditing and input-aware native replay; consumers
/// must not reimplement the exact-name predicate or structural tie key. Like
/// the planner, this public entry point validates every section and symbol
/// before exact-name classification; callers need no hidden prevalidation.
llvm::Expected<llvm::SmallVector<ReleaseSymbolExchangeClass, 16>>
computeAndroidKernelReleaseNameExchangeClasses(
    llvm::ArrayRef<ReleaseSectionDescriptor> Sections,
    llvm::ArrayRef<ReleaseSymbolDescriptor> Symbols);

/// Audits an output plan by structural equivalence class. Producer-local IDs
/// associate records only; symbols tied on the complete observable key may
/// exchange their allocated names, but their class-owned multiset must match.
llvm::Error auditAndroidKernelReleaseNames(
    llvm::ArrayRef<ReleaseSectionDescriptor> Sections,
    llvm::ArrayRef<ReleaseSymbolDescriptor> Symbols,
    llvm::ArrayRef<ReleaseSymbolRename> ActualNames);

} // namespace neverc

#endif // NEVERC_FOUNDATION_ANDROIDKERNELMODULERELEASENAMES_H
