#include "SymbolResolutionProvider.h"
#include "ResolutionVerifier.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Errc.h"
#include <algorithm>
#include <map>
#include <set>
#include <tuple>

using namespace llvm;

namespace neverc::plugin {
namespace {

Error resolutionError(const Twine &Message) {
  return createStringError(errc::invalid_argument,
                           "link symbol resolution: " + Message);
}

struct SymbolKey {
  std::string Name;
  std::string Version;
  uint64_t LocalScope = 0;

  bool operator<(const SymbolKey &Other) const {
    return std::tie(Name, Version, LocalScope) <
           std::tie(Other.Name, Other.Version, Other.LocalScope);
  }
};

SymbolKey key(const PluginLinkSymbol &Symbol) {
  SymbolKey Result{Symbol.Name, Symbol.Version, 0};
  if (Symbol.Binding == NEVERC_LINK_SYMBOL_BINDING_LOCAL)
    Result.LocalScope =
        Symbol.Origin.ObjectGraph.Owner != 0
            ? Symbol.Origin.ObjectGraph.Owner
            : (Symbol.Origin.InputID != 0 ? Symbol.Origin.InputID
                                          : Symbol.ID);
  return Result;
}

LinkSymbolResolutionReason reason(const PluginLinkSymbol &Symbol) {
  if (Symbol.Definition == NEVERC_LINK_SYMBOL_UNDEFINED)
    return LinkSymbolResolutionReason::Undefined;
  if (Symbol.Definition == NEVERC_LINK_SYMBOL_SHARED)
    return LinkSymbolResolutionReason::SharedDefinition;
  if (Symbol.Definition == NEVERC_LINK_SYMBOL_COMMON ||
      Symbol.Binding == NEVERC_LINK_SYMBOL_BINDING_COMMON)
    return LinkSymbolResolutionReason::CommonDefinition;
  if (Symbol.Binding == NEVERC_LINK_SYMBOL_BINDING_WEAK)
    return LinkSymbolResolutionReason::WeakDefinition;
  return LinkSymbolResolutionReason::StrongDefinition;
}

unsigned rank(LinkSymbolResolutionReason Reason) {
  switch (Reason) {
  case LinkSymbolResolutionReason::StrongDefinition:
  case LinkSymbolResolutionReason::ComdatCandidate:
    return 0;
  case LinkSymbolResolutionReason::CommonDefinition:
    return 1;
  case LinkSymbolResolutionReason::WeakDefinition:
    return 2;
  case LinkSymbolResolutionReason::SharedDefinition:
    return 3;
  case LinkSymbolResolutionReason::Undefined:
    return 4;
  }
  return 4;
}

uint64_t comdatID(const PluginLinkGraph &Graph,
                  const PluginLinkSymbol &Symbol) {
  if (const PluginLinkAtom *Atom = Graph.findAtom(Symbol.AtomID))
    return Atom->ComdatID;
  return 0;
}

bool sameComdatGroup(const PluginLinkGraph &Graph,
                     ArrayRef<PluginLinkSymbol *> Symbols) {
  StringRef Name;
  for (const PluginLinkSymbol *Symbol : Symbols) {
    const uint64_t ID = comdatID(Graph, *Symbol);
    const PluginLinkComdat *Comdat = Graph.findComdat(ID);
    if (!Comdat || Comdat->Name.empty())
      return false;
    if (Name.empty())
      Name = Comdat->Name;
    else if (Name != Comdat->Name)
      return false;
  }
  return !Name.empty();
}

} // namespace

Expected<std::vector<LinkSymbolResolutionRecord>>
resolveLinkSymbols(PluginLinkGraph &Graph,
                   const SymbolResolutionOptions &Options) {
  if (Graph.state() < NEVERC_LINK_STATE_INPUTS_READ)
    return resolutionError("input graph is not ready");
  if (Graph.state() > NEVERC_LINK_STATE_SYMBOLS_RESOLVED)
    return resolutionError(
        "later link phases must be invalidated before symbol resolution");

  std::map<SymbolKey, std::vector<PluginLinkSymbol *>> Groups;
  for (PluginLinkSymbol &Symbol : Graph.symbols()) {
    Symbol.IsPrevailing = false;
    Groups[key(Symbol)].push_back(&Symbol);
  }

  std::set<std::string> ExplicitExports(
      Options.ExportedSymbols.begin(), Options.ExportedSymbols.end());
  std::vector<LinkSymbolResolutionRecord> Records;
  Records.reserve(Groups.size());
  for (auto &[Key, Candidates] : Groups) {
    LinkSymbolResolutionRecord Record;
    Record.Name = Key.Name;
    Record.Version = Key.Version;
    for (PluginLinkSymbol *Candidate : Candidates)
      Record.Candidates.push_back(
          {Candidate->ID, reason(*Candidate), false});

    std::vector<PluginLinkSymbol *> Definitions;
    llvm::copy_if(Candidates, std::back_inserter(Definitions),
                  [](const PluginLinkSymbol *Symbol) {
                    return Symbol->Definition !=
                           NEVERC_LINK_SYMBOL_UNDEFINED;
                  });
    PluginLinkSymbol *Selected = nullptr;
    if (!Definitions.empty()) {
      llvm::stable_sort(
          Definitions,
          [](const PluginLinkSymbol *Left,
             const PluginLinkSymbol *Right) {
            const unsigned LeftRank = rank(reason(*Left));
            const unsigned RightRank = rank(reason(*Right));
            if (LeftRank != RightRank)
              return LeftRank < RightRank;
            if (LeftRank == 1 &&
                std::tie(Left->Size, Left->Value) !=
                    std::tie(Right->Size, Right->Value))
              return std::tie(Left->Size, Left->Value) >
                     std::tie(Right->Size, Right->Value);
            return Left->ID < Right->ID;
          });
      const unsigned BestRank = rank(reason(*Definitions.front()));
      std::vector<PluginLinkSymbol *> Best;
      llvm::copy_if(Definitions, std::back_inserter(Best),
                    [&](const PluginLinkSymbol *Symbol) {
                      return rank(reason(*Symbol)) == BestRank;
                    });
      if (BestRank == 0 && Best.size() > 1 &&
          Key.LocalScope == 0 && !sameComdatGroup(Graph, Best))
        return resolutionError("duplicate strong definition of '" +
                               Key.Name + "'");
      Selected = Definitions.front();
      Record.Reason =
          Best.size() > 1 && sameComdatGroup(Graph, Best)
              ? LinkSymbolResolutionReason::ComdatCandidate
              : reason(*Selected);
      Record.SelectedSymbolID = Selected->ID;
      Selected->IsPrevailing = true;
      if (ExplicitExports.count(Key.Name) != 0)
        Selected->IsExported = true;
      if (Selected->IsExported || Key.Name == Options.EntrySymbol) {
        Selected->IsRoot = true;
        if (PluginLinkAtom *Atom = Graph.findAtom(Selected->AtomID))
          Atom->Flags |= NEVERC_LINK_ATOM_ROOT |
                         NEVERC_LINK_ATOM_LIVE;
      }
      for (LinkSymbolCandidateRecord &Candidate :
           Record.Candidates)
        Candidate.Selected =
            Candidate.SymbolID == Selected->ID;
      for (PluginLinkEdge &Edge : Graph.edges())
        if (llvm::any_of(
                Candidates, [&](const PluginLinkSymbol *Candidate) {
                  return Edge.TargetSymbolID == Candidate->ID;
                }))
          Edge.TargetSymbolID = Selected->ID;
    } else {
      Record.Reason = LinkSymbolResolutionReason::Undefined;
      const bool WeakOnly =
          llvm::all_of(Candidates, [](const PluginLinkSymbol *Symbol) {
            return Symbol->Binding ==
                   NEVERC_LINK_SYMBOL_BINDING_WEAK;
          });
      if (!Options.AllowUndefined && !Options.Relocatable &&
          !WeakOnly)
        return resolutionError("undefined symbol '" + Key.Name + "'");
    }
    Records.push_back(std::move(Record));
  }

  Graph.advanceGeneration();
  Graph.setState(NEVERC_LINK_STATE_SYMBOLS_RESOLVED);
  if (Error E = verifyLinkSymbolResolution(Graph, Options))
    return std::move(E);
  return Records;
}

} // namespace neverc::plugin
