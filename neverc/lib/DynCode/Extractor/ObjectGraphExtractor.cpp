// Volume 6 task 11: the format-agnostic dyncode extractor orchestrator.
//
// run() validates the ObjectGraph, resolves the single entry from the request
// policy, plans sections/symbols/relocations, assembles the candidate image and
// verifies the plan.  verifyDynCodeExtractionPlan() is the shared verifier that
// both the builtin path and any plugin replacement of object.graph ->
// extraction.plan must satisfy.

#include "Extractor/ObjectGraphExtractor.h"
#include "Extractor/ExtractorCommon.h"
#include "neverc/Plugin/Schema/PluginObjectSchema.inc"
#include "llvm/Support/Error.h"
#include <vector>

using namespace llvm;

namespace neverc {
namespace dyncode {

llvm::Error ObjectGraphExtractor::validateGraph() {
  if (llvm::Error E = plugin::verifyPluginObjectGraph(Graph))
    return E;
  if (!Graph.hasLayoutProof())
    return createStringError(inconvertibleErrorCode(),
                             "dyncode extraction: object graph is not verified "
                             "(no layout proof); a candidate image cannot be "
                             "extracted from an unverified graph");
  if (Graph.layoutProof()->GraphGeneration != Graph.generation())
    return createStringError(inconvertibleErrorCode(),
                             "dyncode extraction: object graph layout proof is "
                             "stale (proof generation %llu != graph generation "
                             "%llu)",
                             (unsigned long long)Graph.layoutProof()->GraphGeneration,
                             (unsigned long long)Graph.generation());
  return Error::success();
}

llvm::Error ObjectGraphExtractor::resolveEntry() {
  std::vector<const plugin::PluginObjectSymbol *> Candidates;
  for (const plugin::PluginObjectSymbol &Sym : Graph.symbols()) {
    if (Sym.Definition != NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED)
      continue;
    if (!isDynCodeEntryCandidate(Sym.Name, Opts.EntrySymbol))
      continue;
    const plugin::PluginObjectSection *Sec = Graph.findSection(Sym.SectionID);
    if (!Sec || !isDynCodeCodeSection(*Sec, Opts.Target))
      continue;
    Candidates.push_back(&Sym);
  }

  if (Candidates.empty()) {
    if (!Opts.EntrySymbol.empty())
      return createStringError(
          inconvertibleErrorCode(),
          "dyncode extraction: entry symbol '%s' is not a defined function in "
          "a code section",
          Opts.EntrySymbol.c_str());
    return createStringError(
        inconvertibleErrorCode(),
        "dyncode extraction: no entry symbol; define one of {%s} or pass "
        "-fdyncode-entry=<symbol> (the extractor never falls back to the first "
        "function)",
        defaultEntryNameList().c_str());
  }
  if (Candidates.size() != 1)
    return createStringError(
        inconvertibleErrorCode(),
        "dyncode extraction: ambiguous entry (%zu candidates); pass "
        "-fdyncode-entry=<symbol> to choose one",
        Candidates.size());

  EntrySymbol = Candidates.front();
  EntrySectionID = EntrySymbol->SectionID;
  EntryValueInSection = EntrySymbol->Value;
  return Error::success();
}

llvm::Error ObjectGraphExtractor::assembleImage(DynCodeImage &Image) {
  if (Opts.MaxLength)
    Image.setBudget(Opts.MaxLength);

  static const std::vector<uint8_t> ZeroPage(4096, 0);
  auto appendZeros = [&](uint64_t Count) -> llvm::Error {
    while (Count > 0) {
      uint64_t Chunk = std::min<uint64_t>(Count, ZeroPage.size());
      if (llvm::Error E =
              Image.append(ArrayRef<uint8_t>(ZeroPage.data(), Chunk)))
        return E;
      Count -= Chunk;
    }
    return Error::success();
  };

  for (const PlannedFragment &Frag : Fragments) {
    if (Frag.OutputOffset > Image.size())
      if (llvm::Error E = appendZeros(Frag.OutputOffset - Image.size()))
        return E;
    if (!Frag.Bytes.empty())
      if (llvm::Error E = Image.append(Frag.Bytes))
        return E;
    if (Frag.Bytes.size() < Frag.Size)
      if (llvm::Error E = appendZeros(Frag.Size - Frag.Bytes.size()))
        return E;
  }

  Image.setEntry(EntryOutputOffset, EntrySymbol ? EntrySymbol->Name : "");
  if (llvm::Error E = Image.setOutputAlignment(Opts.Align ? Opts.Align : 1))
    return E;
  return Error::success();
}

llvm::Expected<ObjectGraphExtractionResult> ObjectGraphExtractor::run() {
  if (llvm::Error E = validateGraph())
    return std::move(E);
  if (llvm::Error E = resolveEntry())
    return std::move(E);
  if (llvm::Error E = planSections())
    return std::move(E);
  if (llvm::Error E = planSymbols())
    return std::move(E);
  if (llvm::Error E = planRelocations())
    return std::move(E);

  DynCodeImage Image;
  if (llvm::Error E = assembleImage(Image))
    return std::move(E);

  DynCodeEntryPolicy Policy = Opts.EntrySymbol.empty()
                                  ? DynCodeEntryPolicy::CandidateList
                                  : DynCodeEntryPolicy::Explicit;
  if (llvm::Error E = Plan.setEntry(Policy,
                                    EntrySymbol ? EntrySymbol->Name : "",
                                    EntryOutputOffset))
    return std::move(E);

  DynCodeReportSummary Summary;
  Summary.SelectedSectionCount = SelectedCount;
  Summary.RejectedSectionCount = RejectedCount;
  Summary.RuntimeContractCount = RuntimeContractCount;
  Summary.RemainingExternalCount = RemainingExternalCount;
  Summary.ImageSize = Image.size();
  Summary.Alignment = Image.outputAlignment();
  Summary.EntryOffset = EntryOutputOffset;
  Summary.EntrySymbol = EntrySymbol ? EntrySymbol->Name : "";
  Summary.OutputDigest = Image.digest();
  if (llvm::Error E = Report.setSummary(Summary))
    return std::move(E);

  if (llvm::Error E = verifyDynCodeExtractionPlan(Plan, Image, Opts))
    return std::move(E);

  ObjectGraphExtractionResult Result;
  Result.Plan = std::move(Plan);
  Result.Image = std::move(Image);
  Result.Report = std::move(Report);
  return Result;
}

llvm::Error verifyDynCodeExtractionPlan(const DynCodeExtractionPlan &Plan,
                                        const DynCodeImage &Image,
                                        const DynCodeOptions &Opts) {
  (void)Opts;
  if (!Plan.hasEntry())
    return createStringError(inconvertibleErrorCode(),
                             "dyncode extraction plan has no entry");
  if (Image.size() == 0)
    return createStringError(inconvertibleErrorCode(),
                             "dyncode extraction produced an empty image");
  uint64_t EntryOff = Plan.entryOffset();
  // dyncode requires the entry at output offset 0 (entry-at-zero).
  if (EntryOff != 0)
    return createStringError(
        inconvertibleErrorCode(),
        "dyncode extraction: entry offset %llu is not 0 (entry-at-zero "
        "violated)",
        (unsigned long long)EntryOff);
  if (EntryOff >= Image.size())
    return createStringError(inconvertibleErrorCode(),
                             "dyncode extraction: entry offset past image end");

  bool SawEntryMapping = false;
  for (const DynCodeSymbolMapping &M : Plan.symbolMappings()) {
    if (M.OutputOffset > Image.size())
      return createStringError(
          inconvertibleErrorCode(),
          "dyncode extraction: symbol '%s' maps past the image end",
          M.Name.c_str());
    if (M.IsEntry) {
      SawEntryMapping = true;
      if (M.OutputOffset != EntryOff)
        return createStringError(
            inconvertibleErrorCode(),
            "dyncode extraction: entry symbol '%s' maps to %llu but the plan "
            "entry offset is %llu",
            M.Name.c_str(), (unsigned long long)M.OutputOffset,
            (unsigned long long)EntryOff);
    }
  }
  if (!SawEntryMapping)
    return createStringError(
        inconvertibleErrorCode(),
        "dyncode extraction: entry symbol has no output mapping");
  return Error::success();
}

} // namespace dyncode
} // namespace neverc
