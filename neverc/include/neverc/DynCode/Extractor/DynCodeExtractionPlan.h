#ifndef NEVERC_DYNCODE_EXTRACTOR_DYNCODEEXTRACTIONPLAN_H
#define NEVERC_DYNCODE_EXTRACTOR_DYNCODEEXTRACTIONPLAN_H

// The typed dyncode extraction plan.
//
// The plan is the format-agnostic description of what the extractor will pull
// out of a verified ObjectGraph: which section fragments become code, where each
// symbol lands, how each relocation is disposed, which external references
// survive as runtime contracts, and the single entry.  Every entity is addressed
// by a typed generation handle so a plan replacement (or a rebuild of a table)
// makes previously handed-out handles stale instead of silently aliasing a new
// entity.  No bytes are written here -- the plan is pure metadata that the
// ObjectGraph extractor and relocation executor consume.

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <string>
#include <vector>

namespace neverc {
namespace dyncode {

/// Kind tag carried by every typed plan/image/report handle.  Resolving a handle
/// of the wrong kind is a structured error, never a silent reinterpretation.
enum class DynCodeHandleKind : uint32_t {
  Invalid = 0,
  SectionFragment = 1,
  SymbolMapping = 2,
  Relocation = 3,
  ExternalContract = 4,
  Image = 5,
  Report = 6,
};

/// A typed generation handle.  ``Generation`` is the owning table's generation
/// at creation time; if the table is rebuilt/replaced the generation advances
/// and this handle becomes stale.  A null handle has Kind == Invalid.
struct DynCodeHandle {
  DynCodeHandleKind Kind = DynCodeHandleKind::Invalid;
  uint32_t Generation = 0;
  uint64_t Index = 0;

  bool isNull() const { return Kind == DynCodeHandleKind::Invalid; }
  bool operator==(const DynCodeHandle &Other) const {
    return Kind == Other.Kind && Generation == Other.Generation &&
           Index == Other.Index;
  }
};

enum class DynCodeSectionDisposition : uint32_t { Selected = 1, Discarded = 2 };

enum class DynCodeRelocDisposition : uint32_t {
  Applied = 1,
  RuntimeContract = 2,
  Rejected = 3,
  Pending = 4,
};

enum class DynCodeExternalDisposition : uint32_t {
  Eliminated = 1,
  ResolvedInternal = 2,
  RuntimeContract = 3,
  Unresolved = 4,
};

enum class DynCodeEntryPolicy : uint32_t {
  Explicit = 1,
  CandidateList = 2,
  AtZero = 3,
};

struct DynCodeSectionFragment {
  std::string SourceName;
  uint32_t SourceKind = 0;
  DynCodeSectionDisposition Disposition = DynCodeSectionDisposition::Selected;
  uint64_t OutputOffset = 0;
  uint64_t OutputSize = 0;
  uint64_t Alignment = 1;
  std::string Reason;
};

struct DynCodeSymbolMapping {
  std::string Name;
  uint64_t OutputOffset = 0;
  bool IsEntry = false;
};

struct DynCodeRelocationEntry {
  uint64_t SiteOffset = 0;
  uint64_t TargetOffset = 0;
  int64_t Addend = 0;
  uint32_t Width = 0;
  bool IsPCRelative = false;
  uint32_t Kind = 0;
  DynCodeRelocDisposition Disposition = DynCodeRelocDisposition::Pending;
  std::string RuntimeContract;
};

struct DynCodeExternalContract {
  std::string Symbol;
  DynCodeExternalDisposition Disposition =
      DynCodeExternalDisposition::Unresolved;
  uint32_t ImportKind = 0;
  std::string ResolverContract;
  std::string ProviderID;
};

/// The typed extraction plan.  All add* methods validate their inputs (checked
/// arithmetic, no overlapping output ranges, a single entry) and return a typed
/// handle.  ``rebuild`` bumps the generation so a replacement provider's plan
/// invalidates handles a previous provider handed out.
class DynCodeExtractionPlan {
public:
  DynCodeExtractionPlan() = default;

  uint32_t generation() const { return Generation; }

  /// Invalidates every previously issued handle by advancing the generation and
  /// clearing all tables.  Used when a plan replacement provider rebuilds the
  /// plan from scratch.
  void rebuild();

  llvm::Expected<DynCodeHandle>
  addSectionFragment(DynCodeSectionFragment Fragment);
  llvm::Expected<DynCodeHandle> addSymbolMapping(DynCodeSymbolMapping Mapping);
  llvm::Expected<DynCodeHandle>
  addRelocation(DynCodeRelocationEntry Relocation);
  llvm::Expected<DynCodeHandle>
  addExternalContract(DynCodeExternalContract Contract);

  llvm::Error setEntry(DynCodeEntryPolicy Policy, llvm::StringRef Symbol,
                       uint64_t Offset);

  llvm::ArrayRef<DynCodeSectionFragment> sectionFragments() const {
    return Sections;
  }
  llvm::ArrayRef<DynCodeSymbolMapping> symbolMappings() const {
    return Symbols;
  }
  llvm::ArrayRef<DynCodeRelocationEntry> relocations() const {
    return Relocations;
  }
  llvm::ArrayRef<DynCodeExternalContract> externalContracts() const {
    return Externals;
  }

  DynCodeEntryPolicy entryPolicy() const { return EntryKind; }
  llvm::StringRef entrySymbol() const { return EntrySym; }
  uint64_t entryOffset() const { return EntryOff; }
  bool hasEntry() const { return HasEntry; }

  /// Resolves a handle to its table index, checking kind and generation.
  llvm::Expected<uint64_t> resolve(DynCodeHandle Handle,
                                   DynCodeHandleKind Expected) const;

  const DynCodeSectionFragment *
  lookupSectionFragment(DynCodeHandle Handle) const;
  const DynCodeSymbolMapping *lookupSymbolMapping(DynCodeHandle Handle) const;
  const DynCodeRelocationEntry *lookupRelocation(DynCodeHandle Handle) const;
  const DynCodeExternalContract *
  lookupExternalContract(DynCodeHandle Handle) const;

private:
  DynCodeHandle makeHandle(DynCodeHandleKind Kind, uint64_t Index) const {
    return DynCodeHandle{Kind, Generation, Index};
  }

  uint32_t Generation = 1;
  std::vector<DynCodeSectionFragment> Sections;
  std::vector<DynCodeSymbolMapping> Symbols;
  std::vector<DynCodeRelocationEntry> Relocations;
  std::vector<DynCodeExternalContract> Externals;
  DynCodeEntryPolicy EntryKind = DynCodeEntryPolicy::CandidateList;
  std::string EntrySym;
  uint64_t EntryOff = 0;
  bool HasEntry = false;
};

} // namespace dyncode
} // namespace neverc

#endif // NEVERC_DYNCODE_EXTRACTOR_DYNCODEEXTRACTIONPLAN_H
