#ifndef NEVERC_PLUGIN_LINK_LINKMUTATION_H
#define NEVERC_PLUGIN_LINK_LINKMUTATION_H

#include "LinkGraph.h"
#include "llvm/Support/Error.h"
#include <memory>
#include <string>
#include <vector>

namespace neverc::plugin {

enum class LinkMutationKind : uint8_t {
  InputStructure,
  SymbolResolution,
  ResolutionOutcome,
  Liveness,
  LivenessOutcome,
  Folding,
  Synthetic,
  AtomContent,
  LayoutConstraint,
  Relocation,
  Image,
};

NevercLinkState earliestInvalidatedLinkState(LinkMutationKind Kind);
NevercLinkState predecessorLinkState(NevercLinkState State);

class LinkMutation {
public:
  LinkMutation(const PluginLinkGraph &Graph, std::string Capability);

  PluginLinkGraph &graph() { return *Working; }
  const PluginLinkGraph &graph() const { return *Working; }
  bool changed() const { return Changed; }
  NevercLinkState earliestInvalidatedState() const {
    return EarliestInvalidatedState;
  }
  const std::string &capability() const { return Capability; }

  PluginLinkSection &addSection(PluginLinkSection Value);
  llvm::Error replaceSection(uint64_t ID, PluginLinkSection Value);
  llvm::Error eraseSection(uint64_t ID);

  PluginLinkAtom &addAtom(PluginLinkAtom Value);
  llvm::Error replaceAtom(uint64_t ID, PluginLinkAtom Value);
  llvm::Error eraseAtom(uint64_t ID);

  PluginLinkSymbol &addSymbol(PluginLinkSymbol Value);
  llvm::Error replaceSymbol(uint64_t ID, PluginLinkSymbol Value);
  llvm::Error eraseSymbol(uint64_t ID);

  PluginLinkEdge &addEdge(PluginLinkEdge Value);
  llvm::Error replaceEdge(uint64_t ID, PluginLinkEdge Value);
  llvm::Error eraseEdge(uint64_t ID);

  PluginLinkSynthetic &addSynthetic(PluginLinkSynthetic Value);
  llvm::Error replaceSynthetic(uint64_t ID, PluginLinkSynthetic Value);
  llvm::Error eraseSynthetic(uint64_t ID);

  PluginLinkConstraint &addConstraint(PluginLinkConstraint Value);
  llvm::Error replaceConstraint(uint64_t ID, PluginLinkConstraint Value);
  llvm::Error eraseConstraint(uint64_t ID);

  llvm::Error rebindSymbol(uint64_t SymbolID, uint64_t AtomID);
  llvm::Error setSymbolResolution(
      uint64_t SymbolID, NevercLinkSymbolBinding Binding,
      NevercLinkSymbolVisibility Visibility,
      NevercLinkSymbolDefinition Definition, bool Prevailing,
      bool Exported);
  llvm::Error retargetEdge(uint64_t EdgeID, uint64_t SymbolID,
                           uint64_t AtomID);
  llvm::Error setSymbolRoot(uint64_t SymbolID, bool Root);
  llvm::Error setAtomLive(uint64_t AtomID, bool Live);
  llvm::Error setFoldLeader(uint64_t AtomID, uint64_t LeaderID);
  llvm::Error replaceAtomContent(uint64_t AtomID,
                                 std::vector<uint8_t> Content,
                                 uint64_t ZeroFillSize);

private:
  void mark(LinkMutationKind Kind);

  std::unique_ptr<PluginLinkGraph> Working;
  std::string Capability;
  NevercLinkState EarliestInvalidatedState =
      NEVERC_LINK_STATE_IMAGE_EMITTED;
  bool Changed = false;
};

void initializeLinkMutationAPI(NevercLinkAPI &API,
                               LinkGraphPluginBridge &Bridge);

} // namespace neverc::plugin

#endif
