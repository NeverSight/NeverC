#include "ComdatSelectionProvider.h"
#include "ResolutionVerifier.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <map>
#include <tuple>

using namespace llvm;

namespace neverc::plugin {
namespace {

Error comdatError(const Twine &Message) {
  return createStringError(errc::invalid_argument,
                           "link COMDAT selection: " + Message);
}

uint64_t groupSize(const PluginLinkGraph &Graph, uint64_t ComdatID) {
  uint64_t Size = 0;
  for (const PluginLinkAtom &Atom : Graph.atoms())
    if (Atom.ComdatID == ComdatID) {
      const uint64_t AtomSize =
          Atom.Content.size() + Atom.ZeroFillSize;
      if (Size > UINT64_MAX - AtomSize)
        return UINT64_MAX;
      Size += AtomSize;
    }
  return Size;
}

std::string groupContent(const PluginLinkGraph &Graph,
                         uint64_t ComdatID) {
  std::vector<std::string> Atoms;
  for (const PluginLinkAtom &Atom : Graph.atoms()) {
    if (Atom.ComdatID != ComdatID)
      continue;
    std::string Encoded;
    raw_string_ostream OS(Encoded);
    OS << Atom.Name.size() << ":" << Atom.Name << ":"
       << Atom.Content.size() << ":";
    for (uint8_t Byte : Atom.Content)
      OS << format_hex_no_prefix(Byte, 2);
    OS << ":" << Atom.ZeroFillSize;
    OS.flush();
    Atoms.push_back(std::move(Encoded));
  }
  llvm::sort(Atoms);
  std::string Result;
  raw_string_ostream OS(Result);
  for (StringRef Atom : Atoms)
    OS << Atom.size() << ":" << Atom;
  OS.flush();
  return Result;
}

uint64_t inputOrdinal(const PluginLinkGraph &Graph,
                      const PluginLinkComdat &Comdat) {
  const PluginLinkInput *Input =
      Graph.findInput(Comdat.Origin.InputID);
  return Input ? Input->Ordinal : 0;
}

uint64_t symbolComdat(const PluginLinkGraph &Graph,
                      const PluginLinkSymbol &Symbol) {
  const PluginLinkAtom *Atom = Graph.findAtom(Symbol.AtomID);
  return Atom ? Atom->ComdatID : 0;
}

} // namespace

Expected<std::vector<LinkComdatSelectionRecord>>
selectLinkComdats(PluginLinkGraph &Graph) {
  if (Graph.state() < NEVERC_LINK_STATE_SYMBOLS_RESOLVED)
    return comdatError("symbols are not resolved");
  if (Graph.state() > NEVERC_LINK_STATE_COMDAT_SELECTED)
    return comdatError(
        "later phases must be invalidated before COMDAT selection");

  std::map<std::string, std::vector<PluginLinkComdat *>> Groups;
  for (PluginLinkComdat &Comdat : Graph.comdats())
    Groups[Comdat.Name].push_back(&Comdat);

  std::vector<LinkComdatSelectionRecord> Records;
  Records.reserve(Groups.size());
  for (auto &[Name, Candidates] : Groups) {
    LinkComdatSelectionRecord Record;
    Record.Name = Name;
    for (const PluginLinkComdat *Candidate : Candidates)
      Record.CandidateComdatIDs.push_back(Candidate->ID);
    if (Candidates.empty())
      continue;
    Record.Rule = Candidates.front()->Selection;
    if (llvm::any_of(Candidates, [&](const PluginLinkComdat *Value) {
          return Value->Selection != Record.Rule;
        }))
      return comdatError("selection rules disagree for '" + Name + "'");
    if (Record.Rule == NEVERC_LINK_COMDAT_NO_DUPLICATES &&
        Candidates.size() != 1)
      return comdatError("duplicate no-duplicates group '" + Name + "'");

    PluginLinkComdat *Selected = Candidates.front();
    if (Record.Rule == NEVERC_LINK_COMDAT_EXACT_MATCH) {
      const std::string Expected =
          groupContent(Graph, Selected->ID);
      if (llvm::any_of(Candidates, [&](const PluginLinkComdat *Value) {
            return groupContent(Graph, Value->ID) != Expected;
          }))
        return comdatError("exact-match group differs for '" + Name + "'");
    } else if (Record.Rule == NEVERC_LINK_COMDAT_SAME_SIZE) {
      const uint64_t Expected = groupSize(Graph, Selected->ID);
      if (llvm::any_of(Candidates, [&](const PluginLinkComdat *Value) {
            return groupSize(Graph, Value->ID) != Expected;
          }))
        return comdatError("same-size group differs for '" + Name + "'");
    } else if (Record.Rule == NEVERC_LINK_COMDAT_LARGEST) {
      Selected = *std::max_element(
          Candidates.begin(), Candidates.end(),
          [&](const PluginLinkComdat *Left,
              const PluginLinkComdat *Right) {
            return std::tuple<uint64_t, uint64_t>{
                       groupSize(Graph, Left->ID), Left->ID} <
                   std::tuple<uint64_t, uint64_t>{
                       groupSize(Graph, Right->ID), Right->ID};
          });
    } else if (Record.Rule == NEVERC_LINK_COMDAT_NEWEST) {
      Selected = *std::max_element(
          Candidates.begin(), Candidates.end(),
          [&](const PluginLinkComdat *Left,
              const PluginLinkComdat *Right) {
            return std::tuple<uint64_t, uint64_t>{
                       inputOrdinal(Graph, *Left), Left->ID} <
                   std::tuple<uint64_t, uint64_t>{
                       inputOrdinal(Graph, *Right), Right->ID};
          });
    } else if (Record.Rule != NEVERC_LINK_COMDAT_ANY &&
               Record.Rule != NEVERC_LINK_COMDAT_NO_DUPLICATES) {
      return comdatError("unknown selection rule for '" + Name + "'");
    }

    Record.SelectedComdatID = Selected->ID;
    for (PluginLinkComdat *Candidate : Candidates)
      Candidate->SelectedID = Selected->ID;

    std::map<std::pair<std::string, std::string>,
             std::vector<PluginLinkSymbol *>>
        Symbols;
    for (PluginLinkSymbol &Symbol : Graph.symbols())
      if (llvm::any_of(Candidates, [&](const PluginLinkComdat *Value) {
            return symbolComdat(Graph, Symbol) == Value->ID;
          }))
        Symbols[{Symbol.Name, Symbol.Version}].push_back(&Symbol);
    for (auto &[SymbolName, Values] : Symbols) {
      PluginLinkSymbol *Winner = nullptr;
      for (PluginLinkSymbol *Value : Values) {
        const bool IsWinner =
            symbolComdat(Graph, *Value) == Selected->ID;
        Value->IsPrevailing = IsWinner && !Winner;
        if (Value->IsPrevailing)
          Winner = Value;
      }
      if (!Winner)
        continue;
      for (PluginLinkEdge &Edge : Graph.edges())
        if (llvm::any_of(Values, [&](const PluginLinkSymbol *Value) {
              return Edge.TargetSymbolID == Value->ID;
            }))
          Edge.TargetSymbolID = Winner->ID;
    }
    Records.push_back(std::move(Record));
  }

  Graph.advanceGeneration();
  Graph.setState(NEVERC_LINK_STATE_COMDAT_SELECTED);
  if (Error E = verifyLinkComdatSelection(Graph))
    return std::move(E);
  return Records;
}

} // namespace neverc::plugin
