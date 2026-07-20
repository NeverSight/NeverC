#include "ResolutionVerifier.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Errc.h"
#include <map>
#include <set>
#include <tuple>

using namespace llvm;

namespace neverc::plugin {
namespace {

Error resolutionError(const Twine &Message) {
  return createStringError(errc::invalid_argument,
                           "link resolution verification: " + Message);
}

struct SymbolKey {
  std::string Name;
  std::string Version;

  bool operator<(const SymbolKey &Other) const {
    return std::tie(Name, Version) <
           std::tie(Other.Name, Other.Version);
  }
};

uint64_t symbolComdat(const PluginLinkGraph &Graph,
                      const PluginLinkSymbol &Symbol) {
  const PluginLinkAtom *Atom = Graph.findAtom(Symbol.AtomID);
  return Atom ? Atom->ComdatID : 0;
}

} // namespace

Error verifyLinkSymbolResolution(
    const PluginLinkGraph &Graph,
    const SymbolResolutionOptions &Options) {
  if (Graph.state() < NEVERC_LINK_STATE_SYMBOLS_RESOLVED)
    return resolutionError("graph has no symbol-resolution state");
  if (Error E = verifyPluginLinkGraph(Graph))
    return E;

  std::map<SymbolKey, std::vector<const PluginLinkSymbol *>> Groups;
  for (const PluginLinkSymbol &Symbol : Graph.symbols())
    if (Symbol.Binding != NEVERC_LINK_SYMBOL_BINDING_LOCAL)
      Groups[{Symbol.Name, Symbol.Version}].push_back(&Symbol);

  for (const auto &[Key, Candidates] : Groups) {
    const PluginLinkSymbol *Prevailing = nullptr;
    bool HasDefinition = false;
    bool WeakUndefinedOnly = true;
    for (const PluginLinkSymbol *Candidate : Candidates) {
      HasDefinition |= Candidate->Definition !=
                       NEVERC_LINK_SYMBOL_UNDEFINED;
      if (Candidate->Definition == NEVERC_LINK_SYMBOL_UNDEFINED &&
          Candidate->Binding != NEVERC_LINK_SYMBOL_BINDING_WEAK)
        WeakUndefinedOnly = false;
      if (!Candidate->IsPrevailing)
        continue;
      if (Candidate->Definition == NEVERC_LINK_SYMBOL_UNDEFINED)
        return resolutionError("undefined symbol is prevailing: '" +
                               Key.Name + "'");
      if (Prevailing)
        return resolutionError("multiple prevailing symbols for '" +
                               Key.Name + "'");
      Prevailing = Candidate;
    }
    if (HasDefinition && !Prevailing)
      return resolutionError("definition set has no prevailing symbol for '" +
                             Key.Name + "'");
    if (!HasDefinition && !Options.AllowUndefined &&
        !Options.Relocatable && !WeakUndefinedOnly)
      return resolutionError("unresolved symbol '" + Key.Name + "'");
    if (Prevailing) {
      for (const PluginLinkEdge &Edge : Graph.edges())
        if (llvm::any_of(
                Candidates, [&](const PluginLinkSymbol *Candidate) {
                  return Edge.TargetSymbolID == Candidate->ID;
                }) &&
            Edge.TargetSymbolID != Prevailing->ID)
          return resolutionError(
              "edge still targets a non-prevailing definition of '" +
              Key.Name + "'");
    }
  }

  if (!Options.EntrySymbol.empty()) {
    auto It = Groups.find({Options.EntrySymbol, {}});
    if (It == Groups.end())
      return resolutionError("entry symbol is missing: '" +
                             Options.EntrySymbol + "'");
    auto EntryIt =
        llvm::find_if(It->second, [](const PluginLinkSymbol *Symbol) {
          return Symbol->IsPrevailing;
        });
    const PluginLinkSymbol *Entry =
        EntryIt == It->second.end() ? nullptr : *EntryIt;
    if (!Entry || !Entry->IsRoot)
      return resolutionError(
          "entry symbol is not a prevailing root: '" +
          Options.EntrySymbol + "'");
  }
  for (StringRef Export : Options.ExportedSymbols) {
    auto It = Groups.find({Export.str(), {}});
    if (It == Groups.end() ||
        llvm::none_of(It->second,
                      [](const PluginLinkSymbol *Symbol) {
                        return Symbol->IsPrevailing &&
                               Symbol->IsExported &&
                               Symbol->IsRoot;
                      }))
      return resolutionError(
          "export is not a prevailing root: '" + Export + "'");
  }
  return Error::success();
}

Error verifyLinkComdatSelection(const PluginLinkGraph &Graph) {
  if (Graph.state() < NEVERC_LINK_STATE_COMDAT_SELECTED)
    return resolutionError("graph has no COMDAT-selection state");
  if (Error E = verifyPluginLinkGraph(Graph))
    return E;

  std::map<std::string, std::vector<const PluginLinkComdat *>> Groups;
  for (const PluginLinkComdat &Comdat : Graph.comdats())
    Groups[Comdat.Name].push_back(&Comdat);
  for (const auto &[Name, Candidates] : Groups) {
    uint64_t SelectedID = 0;
    std::set<uint64_t> IDs;
    for (const PluginLinkComdat *Candidate : Candidates)
      IDs.insert(Candidate->ID);
    for (const PluginLinkComdat *Candidate : Candidates) {
      if (Candidate->SelectedID == 0 ||
          !IDs.count(Candidate->SelectedID))
        return resolutionError(
            "COMDAT selection is missing or foreign for '" + Name + "'");
      if (SelectedID == 0)
        SelectedID = Candidate->SelectedID;
      else if (SelectedID != Candidate->SelectedID)
        return resolutionError(
            "COMDAT candidates disagree on selected group for '" +
            Name + "'");
    }

    std::map<SymbolKey, unsigned> PrevailingByName;
    for (const PluginLinkSymbol &Symbol : Graph.symbols()) {
      const uint64_t ComdatID = symbolComdat(Graph, Symbol);
      if (!IDs.count(ComdatID) || !Symbol.IsPrevailing)
        continue;
      if (ComdatID != SelectedID)
        return resolutionError(
            "losing COMDAT owns a prevailing symbol for '" + Name + "'");
      if (++PrevailingByName[{Symbol.Name, Symbol.Version}] > 1)
        return resolutionError(
            "selected COMDAT has duplicate prevailing symbol '" +
            Symbol.Name + "'");
    }
  }
  return Error::success();
}

} // namespace neverc::plugin
