// Volume 6 task 11: the section half of the format-agnostic extractor.
//
// planSections() selects the code sections out of the ObjectGraph, splits each
// into function-granular fragments (so the entry function can be moved to
// output offset 0), lays them out with per-fragment alignment and records the
// discarded sections (forbidden data / TLS / unwind / debug / non-code) in both
// the plan and the report.  It writes no bytes.

#include "Extractor/ExtractorCommon.h"
#include "Extractor/ObjectGraphExtractor.h"
#include "neverc/Plugin/Schema/PluginObjectSchema.inc"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>

using namespace llvm;

namespace neverc {
namespace dyncode {

bool isDynCodeCodeSection(const plugin::PluginObjectSection &Section,
                          const TargetDesc &Target) {
  if (Section.Kind == NEVERC_OBJECT_SECTION_KIND_TEXT)
    return true;
  if (Section.Flags & NEVERC_OBJECT_SECTION_EXECUTABLE)
    return true;
  // Name-based fallback keeps parity with the old per-format readers for
  // objects whose section kind is generic but whose name is a known .text form.
  return isTextSection(Target, Section.Name);
}

namespace {

std::string discardReason(const plugin::PluginObjectSection &Section,
                          const TargetDesc &Target) {
  if (isForbiddenDataSection(Target, Section.Name))
    return "forbidden-data";
  switch (Section.Kind) {
  case NEVERC_OBJECT_SECTION_KIND_DATA:
  case NEVERC_OBJECT_SECTION_KIND_READ_ONLY_DATA:
    return "data";
  case NEVERC_OBJECT_SECTION_KIND_ZERO_FILL:
    return "zero-fill";
  case NEVERC_OBJECT_SECTION_KIND_TLS_DATA:
  case NEVERC_OBJECT_SECTION_KIND_TLS_ZERO_FILL:
    return "tls";
  case NEVERC_OBJECT_SECTION_KIND_DEBUG:
    return "debug";
  case NEVERC_OBJECT_SECTION_KIND_UNWIND:
    return "unwind";
  default:
    break;
  }
  return "non-code";
}

} // namespace

llvm::Error ObjectGraphExtractor::planSections() {
  // Map each code section to its sorted defined function symbol boundaries.
  struct Boundary {
    uint64_t Value;
    const plugin::PluginObjectSymbol *Sym;
  };

  // Collect the discovery-ordered code sections and their boundaries.
  struct CodeSection {
    const plugin::PluginObjectSection *Section;
    std::vector<Boundary> Boundaries; // sorted, unique by value
  };
  std::vector<CodeSection> CodeSections;

  for (const plugin::PluginObjectSection &Section : Graph.sections()) {
    if (!isDynCodeCodeSection(Section, Opts.Target)) {
      DynCodeSectionFragment Frag;
      Frag.SourceName = Section.Name;
      Frag.SourceKind = Section.Kind;
      Frag.Disposition = DynCodeSectionDisposition::Discarded;
      Frag.Reason = discardReason(Section, Opts.Target);
      if (llvm::Error E = Plan.addSectionFragment(Frag).takeError())
        return E;
      ++RejectedCount;
      if (llvm::Error E = Report.addRecord(
              {24, "builtin.object_graph_extractor", "section.discarded",
               (Twine(Section.Name) + ":" + Frag.Reason).str()}))
        return E;
      continue;
    }
    CodeSections.push_back({&Section, {}});
  }

  if (CodeSections.empty())
    return createStringError(inconvertibleErrorCode(),
                             "dyncode extraction: object graph has no code "
                             "section to extract");

  // Gather function boundaries for each code section.
  for (const plugin::PluginObjectSymbol &Sym : Graph.symbols()) {
    if (Sym.Definition != NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED)
      continue;
    if (Sym.Type != NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION)
      continue;
    for (CodeSection &CS : CodeSections) {
      if (CS.Section->ID != Sym.SectionID)
        continue;
      CS.Boundaries.push_back({Sym.Value, &Sym});
      break;
    }
  }
  // The entry symbol is always a boundary even if it is not typed FUNCTION.
  if (EntrySymbol) {
    for (CodeSection &CS : CodeSections) {
      if (CS.Section->ID != EntrySectionID)
        continue;
      if (llvm::none_of(CS.Boundaries, [&](const Boundary &B) {
            return B.Value == EntryValueInSection;
          }))
        CS.Boundaries.push_back({EntryValueInSection, EntrySymbol});
      break;
    }
  }

  // Build the function-granular fragment list (still in section discovery
  // order); the entry fragment is pulled to the front afterwards.
  std::vector<PlannedFragment> Discovered;
  for (CodeSection &CS : CodeSections) {
    const plugin::PluginObjectSection &Section = *CS.Section;
    uint64_t Extent = Section.Data.size();
    llvm::sort(CS.Boundaries, [](const Boundary &A, const Boundary &B) {
      return A.Value < B.Value;
    });
    CS.Boundaries.erase(
        std::unique(CS.Boundaries.begin(), CS.Boundaries.end(),
                    [](const Boundary &A, const Boundary &B) {
                      return A.Value == B.Value;
                    }),
        CS.Boundaries.end());

    // Region start offsets: a leading region for bytes before the first
    // function symbol (alignment 1), then one per boundary carrying that
    // symbol's own alignment.  The whole-section alignment is deliberately not
    // reused per function: it only constrained the section's placement in the
    // source object, and reapplying it after the entry function is moved to the
    // front would inject spurious inter-function padding.
    struct Region {
      uint64_t Start;
      uint64_t Align;
    };
    std::vector<Region> Starts;
    if (CS.Boundaries.empty() || CS.Boundaries.front().Value > 0)
      Starts.push_back({0, 1});
    for (const Boundary &B : CS.Boundaries)
      if (B.Value <= Extent)
        Starts.push_back(
            {B.Value, (B.Sym && B.Sym->Alignment > 1) ? B.Sym->Alignment : 1});

    for (size_t I = 0; I < Starts.size(); ++I) {
      uint64_t Start = Starts[I].Start;
      uint64_t End = (I + 1 < Starts.size()) ? Starts[I + 1].Start : Extent;
      if (End < Start)
        End = Start;
      PlannedFragment Frag;
      Frag.SectionID = Section.ID;
      Frag.SectionName = Section.Name;
      Frag.StartInSection = Start;
      Frag.Size = End - Start;
      Frag.Alignment = Starts[I].Align;
      Frag.IsEntry = (EntrySymbol && Section.ID == EntrySectionID &&
                      Start == EntryValueInSection);
      uint64_t Avail = Extent > Start ? Extent - Start : 0;
      uint64_t Copy = std::min(Frag.Size, Avail);
      Frag.Bytes = ArrayRef<uint8_t>(Section.Data).slice(Start, Copy);
      Discovered.push_back(Frag);
    }
  }

  // Entry-first ordering: the entry fragment, then the rest in discovery order.
  Fragments.clear();
  if (EntrySymbol) {
    auto It = llvm::find_if(Discovered,
                            [](const PlannedFragment &F) { return F.IsEntry; });
    if (It == Discovered.end())
      return createStringError(inconvertibleErrorCode(),
                               "dyncode extraction: entry symbol '%s' is not "
                               "inside a selected code fragment",
                               Opts.EntrySymbol.c_str());
    Fragments.push_back(*It);
    for (const PlannedFragment &F : Discovered)
      if (!F.IsEntry)
        Fragments.push_back(F);
  } else {
    Fragments = Discovered;
  }

  // Lay out with per-fragment alignment and record selected fragments.
  uint64_t Offset = 0;
  for (PlannedFragment &Frag : Fragments) {
    if (Frag.Alignment > 1) {
      uint64_t Mask = Frag.Alignment - 1;
      Offset = (Offset + Mask) & ~Mask;
    }
    Frag.OutputOffset = Offset;
    if (Frag.IsEntry)
      EntryOutputOffset = Offset;

    DynCodeSectionFragment PlanFrag;
    PlanFrag.SourceName = Frag.SectionName;
    PlanFrag.SourceKind = NEVERC_OBJECT_SECTION_KIND_TEXT;
    PlanFrag.Disposition = DynCodeSectionDisposition::Selected;
    PlanFrag.OutputOffset = Frag.OutputOffset;
    PlanFrag.OutputSize = Frag.Size;
    PlanFrag.Alignment = Frag.Alignment;
    if (llvm::Error E = Plan.addSectionFragment(PlanFrag).takeError())
      return E;
    ++SelectedCount;

    Offset += Frag.Size;
  }

  return Error::success();
}

} // namespace dyncode
} // namespace neverc
