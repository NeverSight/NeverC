#include "neverc/Foundation/AndroidKernelModuleReleaseNames.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/ErrorHandling.h"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include <vector>

using namespace llvm;

namespace neverc {

std::string formatReleaseName(ReleaseNameKind Kind, uint64_t Coordinate,
                              uint64_t Size) {
  (void)Size;
  switch (Kind) {
  case ReleaseNameKind::Function:
    return "fn_" + utohexstr(Coordinate);
  case ReleaseNameKind::Object:
    return "obj_" + utohexstr(Coordinate);
  case ReleaseNameKind::ExecutableLabel:
    return "code_" + utohexstr(Coordinate);
  case ReleaseNameKind::Other:
    return "sym_" + utohexstr(Coordinate);
  case ReleaseNameKind::Absolute:
    return "abs_" + utohexstr(Coordinate);
  }
  llvm_unreachable("invalid Android release name kind");
}

namespace {

enum class CoordinateDomain : uint8_t {
  Allocated,
  NonAllocated,
  Absolute,
};

using StructuralKey = std::tuple<uint8_t, uint64_t, uint64_t, uint8_t, uint32_t,
                                 uint32_t, uint64_t>;

struct PendingReleaseName {
  const ReleaseSymbolDescriptor *Symbol;
  StructuralKey Key;
};

StructuralKey structuralKey(const ReleaseSymbolDescriptor &Symbol,
                            CoordinateDomain Domain,
                            uint64_t FinalSectionOrdinal) {
  return {static_cast<uint8_t>(Domain),
          FinalSectionOrdinal,
          Symbol.Value,
          static_cast<uint8_t>(Symbol.Type),
          Symbol.BindingRank,
          Symbol.OtherValue,
          Symbol.Size};
}

ReleaseNameKind
deriveReleaseNameKind(const ReleaseSymbolDescriptor &Symbol,
                      const ReleaseSectionDescriptor *Section = nullptr) {
  using SymbolClass = AndroidKernelModuleSymbolPolicy::SymbolClass;
  if (Symbol.Class == SymbolClass::Absolute)
    return ReleaseNameKind::Absolute;
  switch (Symbol.Type) {
  case ReleaseSymbolType::Function:
    return ReleaseNameKind::Function;
  case ReleaseSymbolType::Object:
    return ReleaseNameKind::Object;
  case ReleaseSymbolType::NoType:
    return Section && Section->Allocated && Section->Executable
               ? ReleaseNameKind::ExecutableLabel
               : ReleaseNameKind::Other;
  case ReleaseSymbolType::Section:
  case ReleaseSymbolType::File:
  case ReleaseSymbolType::TLS:
  case ReleaseSymbolType::GNUIFunc:
  case ReleaseSymbolType::FormatExtension:
    return ReleaseNameKind::Other;
  }
  return ReleaseNameKind::Other;
}

Error validateReleaseSymbolType(const ReleaseSymbolDescriptor &Symbol) {
  using SymbolClass = AndroidKernelModuleSymbolPolicy::SymbolClass;
  switch (Symbol.Type) {
  case ReleaseSymbolType::NoType:
  case ReleaseSymbolType::Object:
  case ReleaseSymbolType::Function:
    return Error::success();
  case ReleaseSymbolType::Section:
    if (Symbol.Class != SymbolClass::Defined)
      return createStringError(
          inconvertibleErrorCode(),
          "Android release SECTION symbol is not a definition");
    return Error::success();
  case ReleaseSymbolType::File:
    if (!Symbol.OriginalName.empty())
      return createStringError(
          inconvertibleErrorCode(),
          "non-empty Android release FILE symbol is unsupported");
    if (Symbol.Class != SymbolClass::Absolute)
      return createStringError(inconvertibleErrorCode(),
                               "Android release FILE symbol is not absolute");
    return Error::success();
  case ReleaseSymbolType::TLS:
    return createStringError(inconvertibleErrorCode(),
                             "Android release TLS symbol is unsupported");
  case ReleaseSymbolType::GNUIFunc:
    return createStringError(inconvertibleErrorCode(),
                             "Android release GNU IFUNC symbol is unsupported");
  case ReleaseSymbolType::FormatExtension:
    return createStringError(
        inconvertibleErrorCode(),
        "Android release format-extension symbol is unsupported");
  }
  return createStringError(inconvertibleErrorCode(),
                           "invalid Android release symbol type");
}

using ReleaseSectionsByID =
    std::map<uint64_t, const ReleaseSectionDescriptor *>;

Expected<const ReleaseSectionDescriptor *>
validateReleaseSymbol(const ReleaseSymbolDescriptor &Symbol,
                      const ReleaseSectionsByID &SectionsByID) {
  using SymbolClass = AndroidKernelModuleSymbolPolicy::SymbolClass;
  if (Symbol.Class != SymbolClass::Defined &&
      Symbol.Class != SymbolClass::Absolute &&
      Symbol.Class != SymbolClass::Undefined)
    return createStringError(inconvertibleErrorCode(),
                             "unsupported Android release symbol class");
  if (Error Err = validateReleaseSymbolType(Symbol))
    return std::move(Err);

  if (Symbol.Class != SymbolClass::Defined)
    return static_cast<const ReleaseSectionDescriptor *>(nullptr);

  auto Section = SectionsByID.find(Symbol.SectionID);
  if (Section == SectionsByID.end())
    return createStringError(inconvertibleErrorCode(),
                             "Android release symbol references no section");
  const ReleaseSectionDescriptor *DefinedSection = Section->second;
  if (Symbol.Value > DefinedSection->Size)
    return createStringError(inconvertibleErrorCode(),
                             "Android release symbol is past section end");
  if (Symbol.Size > DefinedSection->Size - Symbol.Value)
    return createStringError(
        inconvertibleErrorCode(),
        "Android release symbol extent is past section end");
  if (!DefinedSection->Allocated && Symbol.Type == ReleaseSymbolType::Function)
    return createStringError(
        inconvertibleErrorCode(),
        "Android release function is in a non-allocated section");
  return DefinedSection;
}

} // namespace

Expected<SmallVector<ReleaseSectionLayout, 16>>
computeAndroidKernelReleaseSectionLayout(
    ArrayRef<ReleaseSectionDescriptor> Sections) {
  SmallVector<const ReleaseSectionDescriptor *, 16> Ordered;
  Ordered.reserve(Sections.size());
  std::set<uint64_t> SectionIDs;
  std::set<uint64_t> FinalOrdinals;
  for (const ReleaseSectionDescriptor &Section : Sections) {
    if (Section.Alignment != 0 &&
        (Section.Alignment & (Section.Alignment - 1)) != 0)
      return createStringError(
          inconvertibleErrorCode(),
          "Android release section alignment is not zero or a power of two");
    if (!SectionIDs.insert(Section.ID).second)
      return createStringError(inconvertibleErrorCode(),
                               "duplicate Android release section ID");
    if (!FinalOrdinals.insert(Section.FinalOrdinal).second)
      return createStringError(
          inconvertibleErrorCode(),
          "duplicate Android release final section ordinal");
    Ordered.push_back(&Section);
  }
  llvm::sort(Ordered, [](const ReleaseSectionDescriptor *LHS,
                         const ReleaseSectionDescriptor *RHS) {
    return LHS->FinalOrdinal < RHS->FinalOrdinal;
  });

  SmallVector<ReleaseSectionLayout, 16> Layout;
  uint64_t Cursor = 0;
  for (const ReleaseSectionDescriptor *Section : Ordered) {
    if (!Section->Allocated)
      continue;
    const uint64_t Alignment = std::max<uint64_t>(Section->Alignment, 1);
    if (const uint64_t Remainder = Cursor % Alignment) {
      const uint64_t Padding = Alignment - Remainder;
      if (Padding > std::numeric_limits<uint64_t>::max() - Cursor)
        return createStringError(inconvertibleErrorCode(),
                                 "Android release section alignment overflow");
      Cursor += Padding;
    }
    Layout.push_back({Section->ID, Section->FinalOrdinal, Cursor});
    const uint64_t Advance = std::max<uint64_t>(Section->Size, 1);
    if (Advance > std::numeric_limits<uint64_t>::max() - Cursor)
      return createStringError(inconvertibleErrorCode(),
                               "Android release section size overflow");
    Cursor += Advance;
  }
  return Layout;
}

Expected<SmallVector<ReleaseSymbolExchangeClass, 16>>
computeAndroidKernelReleaseNameExchangeClasses(
    ArrayRef<ReleaseSectionDescriptor> Sections,
    ArrayRef<ReleaseSymbolDescriptor> Symbols) {
  auto ValidatedLayout = computeAndroidKernelReleaseSectionLayout(Sections);
  if (!ValidatedLayout)
    return ValidatedLayout.takeError();

  using SymbolClass = AndroidKernelModuleSymbolPolicy::SymbolClass;
  ReleaseSectionsByID SectionsByID;
  for (const ReleaseSectionDescriptor &Section : Sections)
    SectionsByID.emplace(Section.ID, &Section);

  std::set<uint64_t> SeenSymbolIDs;
  std::map<StructuralKey, SmallVector<uint64_t, 4>> GeneratedClasses;
  SmallVector<ReleaseSymbolExchangeClass, 16> Classes;
  for (const ReleaseSymbolDescriptor &Symbol : Symbols) {
    if (!SeenSymbolIDs.insert(Symbol.ID).second)
      return createStringError(inconvertibleErrorCode(),
                               "duplicate Android release symbol ID");
    auto DefinedSection = validateReleaseSymbol(Symbol, SectionsByID);
    if (!DefinedSection)
      return DefinedSection.takeError();
    if (AndroidKernelModuleSymbolPolicy::hasExactReleaseName(
            Symbol.OriginalName, Symbol.Class,
            Symbol.Type == ReleaseSymbolType::Section, Symbol.PreserveName)) {
      ReleaseSymbolExchangeClass Exact;
      Exact.SymbolIDs.push_back(Symbol.ID);
      Classes.push_back(std::move(Exact));
      continue;
    }

    CoordinateDomain Domain;
    uint64_t FinalSectionOrdinal = 0;
    if (Symbol.Class == SymbolClass::Absolute) {
      Domain = CoordinateDomain::Absolute;
    } else if (Symbol.Class == SymbolClass::Defined) {
      Domain = (*DefinedSection)->Allocated ? CoordinateDomain::Allocated
                                            : CoordinateDomain::NonAllocated;
      FinalSectionOrdinal = (*DefinedSection)->FinalOrdinal;
    } else {
      return createStringError(
          inconvertibleErrorCode(),
          "unsupported Android release exchange-class symbol");
    }
    GeneratedClasses[structuralKey(Symbol, Domain, FinalSectionOrdinal)]
        .push_back(Symbol.ID);
  }

  for (auto &Entry : GeneratedClasses) {
    llvm::sort(Entry.second);
    ReleaseSymbolExchangeClass Generated;
    Generated.SymbolIDs = std::move(Entry.second);
    Classes.push_back(std::move(Generated));
  }
  llvm::sort(Classes, [](const ReleaseSymbolExchangeClass &LHS,
                         const ReleaseSymbolExchangeClass &RHS) {
    return LHS.SymbolIDs.front() < RHS.SymbolIDs.front();
  });
  return Classes;
}

Expected<SmallVector<ReleaseSymbolRename, 32>>
planAndroidKernelReleaseNames(ArrayRef<ReleaseSectionDescriptor> Sections,
                              ArrayRef<ReleaseSymbolDescriptor> Symbols) {
  auto Layout = computeAndroidKernelReleaseSectionLayout(Sections);
  if (!Layout)
    return Layout.takeError();

  std::set<uint64_t> SymbolIDs;
  for (const ReleaseSymbolDescriptor &Symbol : Symbols)
    if (!SymbolIDs.insert(Symbol.ID).second)
      return createStringError(inconvertibleErrorCode(),
                               "duplicate Android release symbol ID");

  ReleaseSectionsByID SectionsByID;
  for (const ReleaseSectionDescriptor &Section : Sections)
    SectionsByID.emplace(Section.ID, &Section);
  std::map<uint64_t, uint64_t> BasesByID;
  for (const ReleaseSectionLayout &Placement : *Layout)
    BasesByID.emplace(Placement.SectionID, Placement.Base);

  SmallVector<ReleaseSymbolRename, 32> Renames;
  Renames.reserve(Symbols.size());
  std::map<std::string, std::vector<PendingReleaseName>> CandidateGroups;
  std::set<std::string> ReservedNames;
  using SymbolClass = AndroidKernelModuleSymbolPolicy::SymbolClass;
  for (const ReleaseSymbolDescriptor &Symbol : Symbols) {
    auto ValidatedSection = validateReleaseSymbol(Symbol, SectionsByID);
    if (!ValidatedSection)
      return ValidatedSection.takeError();
    const ReleaseSectionDescriptor *DefinedSection = *ValidatedSection;

    if (AndroidKernelModuleSymbolPolicy::hasExactReleaseName(
            Symbol.OriginalName, Symbol.Class,
            Symbol.Type == ReleaseSymbolType::Section, Symbol.PreserveName)) {
      Renames.push_back({Symbol.ID, Symbol.OriginalName.str()});
      if (!Symbol.OriginalName.empty())
        ReservedNames.insert(Symbol.OriginalName.str());
      continue;
    }
    if (Symbol.Class == SymbolClass::Absolute) {
      std::string Candidate = formatReleaseName(ReleaseNameKind::Absolute,
                                                Symbol.Value, Symbol.Size);
      CandidateGroups[Candidate].push_back(
          {&Symbol, structuralKey(Symbol, CoordinateDomain::Absolute, 0)});
      continue;
    }
    if (!DefinedSection->Allocated) {
      std::string Candidate = "sym_S" +
                              utohexstr(DefinedSection->FinalOrdinal) + "_" +
                              utohexstr(Symbol.Value);
      CandidateGroups[Candidate].push_back(
          {&Symbol, structuralKey(Symbol, CoordinateDomain::NonAllocated,
                                  DefinedSection->FinalOrdinal)});
      continue;
    }
    auto Base = BasesByID.find(Symbol.SectionID);
    if (Base == BasesByID.end())
      return createStringError(
          inconvertibleErrorCode(),
          "Android release symbol section is not allocated");
    if (Symbol.Value > std::numeric_limits<uint64_t>::max() - Base->second)
      return createStringError(inconvertibleErrorCode(),
                               "Android release symbol address overflow");
    const ReleaseNameKind NameKind =
        deriveReleaseNameKind(Symbol, DefinedSection);
    std::string Candidate =
        formatReleaseName(NameKind, Base->second + Symbol.Value, Symbol.Size);
    CandidateGroups[Candidate].push_back(
        {&Symbol, structuralKey(Symbol, CoordinateDomain::Allocated,
                                DefinedSection->FinalOrdinal)});
  }

  for (auto &CandidateGroup : CandidateGroups) {
    const std::string &Candidate = CandidateGroup.first;
    std::vector<PendingReleaseName> &Group = CandidateGroup.second;
    llvm::sort(Group,
               [](const PendingReleaseName &LHS,
                  const PendingReleaseName &RHS) { return LHS.Key < RHS.Key; });

    uint64_t NextSuffix = 0;
    size_t AssignedInGroup = 0;
    size_t ClassBegin = 0;
    while (ClassBegin != Group.size()) {
      size_t ClassEnd = ClassBegin + 1;
      while (ClassEnd != Group.size() &&
             Group[ClassEnd].Key == Group[ClassBegin].Key)
        ++ClassEnd;

      SmallVector<const PendingReleaseName *, 4> ExactTieClass;
      for (size_t I = ClassBegin; I != ClassEnd; ++I)
        ExactTieClass.push_back(&Group[I]);
      llvm::sort(ExactTieClass, [](const PendingReleaseName *LHS,
                                   const PendingReleaseName *RHS) {
        return LHS->Symbol->ID < RHS->Symbol->ID;
      });

      for (const PendingReleaseName *Pending : ExactTieClass) {
        std::string OutputName = Candidate;
        if (NextSuffix != 0)
          OutputName += "_" + std::to_string(NextSuffix);
        if (ReservedNames.count(OutputName) != 0)
          return createStringError(
              inconvertibleErrorCode(),
              "generated Android release name collides with reserved name");
        Renames.push_back({Pending->Symbol->ID, std::move(OutputName)});
        ++AssignedInGroup;

        if (NextSuffix == std::numeric_limits<uint64_t>::max()) {
          if (AssignedInGroup != Group.size())
            return createStringError(inconvertibleErrorCode(),
                                     "Android release alias suffix overflow");
        } else {
          ++NextSuffix;
        }
      }
      ClassBegin = ClassEnd;
    }
  }

  llvm::sort(Renames, [](const ReleaseSymbolRename &LHS,
                         const ReleaseSymbolRename &RHS) {
    return LHS.SymbolID < RHS.SymbolID;
  });
  return Renames;
}

Error auditAndroidKernelReleaseNames(
    ArrayRef<ReleaseSectionDescriptor> Sections,
    ArrayRef<ReleaseSymbolDescriptor> Symbols,
    ArrayRef<ReleaseSymbolRename> ActualNames) {
  auto Expected = planAndroidKernelReleaseNames(Sections, Symbols);
  if (!Expected)
    return Expected.takeError();

  std::map<uint64_t, std::string> ExpectedByID;
  for (const ReleaseSymbolRename &Rename : *Expected)
    ExpectedByID.emplace(Rename.SymbolID, Rename.OutputName);
  std::map<uint64_t, std::string> ActualByID;
  for (const ReleaseSymbolRename &Rename : ActualNames)
    if (!ActualByID.emplace(Rename.SymbolID, Rename.OutputName).second)
      return createStringError(inconvertibleErrorCode(),
                               "duplicate audited Android release symbol ID");
  if (ActualByID.size() != ExpectedByID.size())
    return createStringError(inconvertibleErrorCode(),
                             "audited Android release plan has wrong size");
  for (const auto &Actual : ActualByID)
    if (ExpectedByID.count(Actual.first) == 0)
      return createStringError(inconvertibleErrorCode(),
                               "audited Android release plan has unknown ID");

  auto ExchangeClasses =
      computeAndroidKernelReleaseNameExchangeClasses(Sections, Symbols);
  if (!ExchangeClasses)
    return ExchangeClasses.takeError();
  for (const ReleaseSymbolExchangeClass &TieClass : *ExchangeClasses) {
    std::vector<std::string> ExpectedMultiset;
    std::vector<std::string> ActualMultiset;
    ExpectedMultiset.reserve(TieClass.SymbolIDs.size());
    ActualMultiset.reserve(TieClass.SymbolIDs.size());
    for (uint64_t SymbolID : TieClass.SymbolIDs) {
      ExpectedMultiset.push_back(ExpectedByID.find(SymbolID)->second);
      ActualMultiset.push_back(ActualByID.find(SymbolID)->second);
    }
    llvm::sort(ExpectedMultiset);
    llvm::sort(ActualMultiset);
    if (ActualMultiset != ExpectedMultiset)
      return createStringError(
          inconvertibleErrorCode(),
          "audited Android release exact-tie name multiset differs");
  }
  return Error::success();
}

} // namespace neverc
