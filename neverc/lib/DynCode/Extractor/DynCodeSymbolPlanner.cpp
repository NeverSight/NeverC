// The symbol/relocation half of the format-agnostic
// extractor.
//
// planSymbols() maps every defined symbol that landed in a selected code
// fragment to its output offset; planRelocations() turns every relocation
// whose site is in the image into a typed worklist entry (disposition left
// Pending; applied later) and records the surviving external
// references as runtime-contract candidates.  Neither writes any bytes.

#include "Extractor/ExtractorCommon.h"
#include "Extractor/ObjectGraphExtractor.h"
#include "neverc/Plugin/Schema/PluginObjectSchema.inc"
#include <string>

using namespace llvm;

namespace neverc {
namespace dyncode {

const PlannedFragment *
ObjectGraphExtractor::fragmentContaining(uint64_t SectionID,
                                         uint64_t OffsetInSection) const {
  for (const PlannedFragment &F : Fragments) {
    if (F.SectionID != SectionID)
      continue;
    uint64_t Begin = F.StartInSection;
    uint64_t End = Begin + F.Size;
    if (OffsetInSection >= Begin && OffsetInSection < End)
      return &F;
    // A zero-length fragment (rare) still owns exactly its start offset.
    if (F.Size == 0 && OffsetInSection == Begin)
      return &F;
  }
  return nullptr;
}

llvm::Error ObjectGraphExtractor::planSymbols() {
  for (const plugin::PluginObjectSymbol &Sym : Graph.symbols()) {
    if (Sym.Definition != NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED)
      continue;
    if (Sym.Name.empty())
      continue;
    if (Sym.Type == NEVERC_OBJECT_SYMBOL_TYPE_SECTION ||
        Sym.Type == NEVERC_OBJECT_SYMBOL_TYPE_FILE)
      continue;
    const PlannedFragment *Frag = fragmentContaining(Sym.SectionID, Sym.Value);
    if (!Frag)
      continue; // symbol lives in a discarded section

    uint64_t Offset = Frag->OutputOffset + (Sym.Value - Frag->StartInSection);
    DynCodeSymbolMapping Mapping;
    Mapping.Name = Sym.Name;
    Mapping.OutputOffset = Offset;
    Mapping.IsEntry = (&Sym == EntrySymbol);
    if (llvm::Error E = Plan.addSymbolMapping(Mapping).takeError())
      return E;
    SymOffsetByID[Sym.ID] = Offset;
  }
  return Error::success();
}

llvm::Error ObjectGraphExtractor::planRelocations() {
  for (const plugin::PluginObjectRelocation &Reloc : Graph.relocations()) {
    const PlannedFragment *Site =
        fragmentContaining(Reloc.SectionID, Reloc.Offset);
    if (!Site)
      continue; // relocation not inside extracted code

    // ObjectGraph relocation Width is in bits; the plan worklist uses bytes.
    if (Reloc.Width == 0 || (Reloc.Width % 8) != 0 || (Reloc.Width / 8) > 8) {
      if (llvm::Error E = Report.addRecord(
              {26, "builtin.object_graph_extractor",
               "relocation.unsupported_width", std::to_string(Reloc.Width)}))
        return E;
      continue;
    }
    uint32_t ByteWidth = Reloc.Width / 8;

    uint64_t SiteOffset =
        Site->OutputOffset + (Reloc.Offset - Site->StartInSection);

    DynCodeRelocationEntry Entry;
    Entry.SiteOffset = SiteOffset;
    Entry.Addend = Reloc.Addend;
    Entry.Width = ByteWidth;
    Entry.IsPCRelative = Reloc.IsPCRelative;
    Entry.Kind = Reloc.Kind;
    Entry.Disposition = DynCodeRelocDisposition::Pending;

    bool Resolved = false;
    if (Reloc.TargetKind == NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL &&
        Reloc.TargetSymbolID != 0) {
      auto It = SymOffsetByID.find(Reloc.TargetSymbolID);
      if (It != SymOffsetByID.end()) {
        Entry.TargetOffset = It->second;
        Resolved = true;
      } else if (const plugin::PluginObjectSymbol *Target =
                     Graph.findSymbol(Reloc.TargetSymbolID)) {
        if (Target->Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED) {
          // An external reference the extractor cannot resolve in the image; it
          // must later be eliminated, resolved or turned into a runtime
          // contract, or the final verifier fails.
          DynCodeExternalContract Contract;
          Contract.Symbol = Target->Name;
          Contract.Disposition = DynCodeExternalDisposition::Unresolved;
          if (llvm::Error E = Plan.addExternalContract(Contract).takeError())
            return E;
          ++RemainingExternalCount;
          if (llvm::Error E = Report.addRecord(
                  {23, "builtin.object_graph_extractor", "external.unresolved",
                   Target->Name}))
            return E;
        }
      }
    } else if (Reloc.TargetKind == NEVERC_OBJECT_RELOCATION_TARGET_SECTION &&
               Reloc.TargetSectionID != 0) {
      const PlannedFragment *TargetFrag =
          fragmentContaining(Reloc.TargetSectionID, Reloc.TargetValue);
      if (TargetFrag) {
        Entry.TargetOffset = TargetFrag->OutputOffset +
                             (Reloc.TargetValue - TargetFrag->StartInSection);
        Resolved = true;
      }
    }
    (void)Resolved;

    if (llvm::Error E = Plan.addRelocation(Entry).takeError())
      return E;
  }
  return Error::success();
}

} // namespace dyncode
} // namespace neverc
