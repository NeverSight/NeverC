#include "neverc/DynCode/Extractor/DynCodeExtractionPlan.h"
#include "llvm/Support/Errc.h"

using namespace llvm;

namespace neverc {
namespace dyncode {
namespace {

Error planError(const Twine &Message) {
  return createStringError(errc::invalid_argument,
                           "dyncode extraction plan: " + Message);
}

/// Checked ``a + b`` for output-range math; fails instead of wrapping.
Expected<uint64_t> checkedAdd(uint64_t A, uint64_t B, const Twine &What) {
  if (A > UINT64_MAX - B)
    return planError(What + " overflows a 64-bit output offset");
  return A + B;
}

/// True when [AOff, AOff+ASize) and [BOff, BOff+BSize) share any byte.  Both
/// ranges are assumed non-overflowing (validated by the caller).
bool rangesOverlap(uint64_t AOff, uint64_t ASize, uint64_t BOff,
                   uint64_t BSize) {
  if (ASize == 0 || BSize == 0)
    return false;
  return AOff < BOff + BSize && BOff < AOff + ASize;
}

} // namespace

void DynCodeExtractionPlan::rebuild() {
  ++Generation;
  Sections.clear();
  Symbols.clear();
  Relocations.clear();
  Externals.clear();
  EntryKind = DynCodeEntryPolicy::CandidateList;
  EntrySym.clear();
  EntryOff = 0;
  HasEntry = false;
}

Expected<DynCodeHandle>
DynCodeExtractionPlan::addSectionFragment(DynCodeSectionFragment Fragment) {
  if (Fragment.Alignment == 0 || (Fragment.Alignment & (Fragment.Alignment - 1)))
    return planError("section fragment alignment must be a power of two");
  auto End = checkedAdd(Fragment.OutputOffset, Fragment.OutputSize,
                        "section fragment output range");
  if (!End)
    return End.takeError();
  if (Fragment.Disposition == DynCodeSectionDisposition::Selected) {
    for (const DynCodeSectionFragment &Existing : Sections) {
      if (Existing.Disposition != DynCodeSectionDisposition::Selected)
        continue;
      if (rangesOverlap(Fragment.OutputOffset, Fragment.OutputSize,
                        Existing.OutputOffset, Existing.OutputSize))
        return planError("selected section fragment '" + Fragment.SourceName +
                         "' overlaps '" + Existing.SourceName + "'");
    }
  }
  uint64_t Index = Sections.size();
  Sections.push_back(std::move(Fragment));
  return makeHandle(DynCodeHandleKind::SectionFragment, Index);
}

Expected<DynCodeHandle>
DynCodeExtractionPlan::addSymbolMapping(DynCodeSymbolMapping Mapping) {
  if (Mapping.Name.empty())
    return planError("symbol mapping requires a non-empty name");
  for (const DynCodeSymbolMapping &Existing : Symbols) {
    if (Existing.Name == Mapping.Name)
      return planError("duplicate symbol mapping for '" + Mapping.Name + "'");
    if (Existing.OutputOffset == Mapping.OutputOffset && Existing.IsEntry &&
        Mapping.IsEntry)
      return planError("multiple entry symbols map to the same offset");
  }
  if (Mapping.IsEntry) {
    for (const DynCodeSymbolMapping &Existing : Symbols)
      if (Existing.IsEntry)
        return planError("plan already has an entry symbol '" + Existing.Name +
                         "'");
  }
  uint64_t Index = Symbols.size();
  Symbols.push_back(std::move(Mapping));
  return makeHandle(DynCodeHandleKind::SymbolMapping, Index);
}

Expected<DynCodeHandle>
DynCodeExtractionPlan::addRelocation(DynCodeRelocationEntry Relocation) {
  if (Relocation.Width == 0 || Relocation.Width > 8)
    return planError("relocation width must be in 1..8 bytes");
  auto End = checkedAdd(Relocation.SiteOffset, Relocation.Width,
                        "relocation site range");
  if (!End)
    return End.takeError();
  uint64_t Index = Relocations.size();
  Relocations.push_back(std::move(Relocation));
  return makeHandle(DynCodeHandleKind::Relocation, Index);
}

Expected<DynCodeHandle>
DynCodeExtractionPlan::addExternalContract(DynCodeExternalContract Contract) {
  if (Contract.Symbol.empty())
    return planError("external contract requires a symbol name");
  for (const DynCodeExternalContract &Existing : Externals)
    if (Existing.Symbol == Contract.Symbol)
      return planError("duplicate external contract for '" + Contract.Symbol +
                       "'");
  uint64_t Index = Externals.size();
  Externals.push_back(std::move(Contract));
  return makeHandle(DynCodeHandleKind::ExternalContract, Index);
}

Error DynCodeExtractionPlan::setEntry(DynCodeEntryPolicy Policy,
                                      StringRef Symbol, uint64_t Offset) {
  if (Policy == DynCodeEntryPolicy::Explicit && Symbol.empty())
    return planError("explicit entry policy requires a symbol");
  EntryKind = Policy;
  EntrySym = Symbol.str();
  EntryOff = Offset;
  HasEntry = true;
  return Error::success();
}

Expected<uint64_t>
DynCodeExtractionPlan::resolve(DynCodeHandle Handle,
                              DynCodeHandleKind Expected) const {
  if (Handle.isNull() || Handle.Kind != Expected)
    return planError("handle has the wrong kind");
  if (Handle.Generation != Generation)
    return planError("stale handle from an earlier plan generation");
  uint64_t Count = 0;
  switch (Expected) {
  case DynCodeHandleKind::SectionFragment:
    Count = Sections.size();
    break;
  case DynCodeHandleKind::SymbolMapping:
    Count = Symbols.size();
    break;
  case DynCodeHandleKind::Relocation:
    Count = Relocations.size();
    break;
  case DynCodeHandleKind::ExternalContract:
    Count = Externals.size();
    break;
  default:
    return planError("handle kind is not owned by the plan");
  }
  if (Handle.Index >= Count)
    return planError("handle index is out of range");
  return Handle.Index;
}

const DynCodeSectionFragment *
DynCodeExtractionPlan::lookupSectionFragment(DynCodeHandle Handle) const {
  auto Index = resolve(Handle, DynCodeHandleKind::SectionFragment);
  if (!Index) {
    consumeError(Index.takeError());
    return nullptr;
  }
  return &Sections[*Index];
}

const DynCodeSymbolMapping *
DynCodeExtractionPlan::lookupSymbolMapping(DynCodeHandle Handle) const {
  auto Index = resolve(Handle, DynCodeHandleKind::SymbolMapping);
  if (!Index) {
    consumeError(Index.takeError());
    return nullptr;
  }
  return &Symbols[*Index];
}

const DynCodeRelocationEntry *
DynCodeExtractionPlan::lookupRelocation(DynCodeHandle Handle) const {
  auto Index = resolve(Handle, DynCodeHandleKind::Relocation);
  if (!Index) {
    consumeError(Index.takeError());
    return nullptr;
  }
  return &Relocations[*Index];
}

const DynCodeExternalContract *
DynCodeExtractionPlan::lookupExternalContract(DynCodeHandle Handle) const {
  auto Index = resolve(Handle, DynCodeHandleKind::ExternalContract);
  if (!Index) {
    consumeError(Index.takeError());
    return nullptr;
  }
  return &Externals[*Index];
}

} // namespace dyncode
} // namespace neverc
