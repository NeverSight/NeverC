#include "neverc/Build/DepGraph.h"
#include "neverc/Build/BuildConstants.h"
#include "neverc/Build/Platform.h"

#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/Twine.h"

#include <algorithm>

namespace neverc {
namespace build {

bool DepGraph::build(const std::string &Target, const RuleDB &Rules,
                      bool AlwaysMake) {
  llvm::StringMap<Color> Colors;
  return buildNode(Target, Rules, AlwaysMake, Colors);
}

bool DepGraph::buildNode(const std::string &Target, const RuleDB &Rules,
                          bool AlwaysMake,
                          llvm::StringMap<Color> &Colors) {
  if (Colors[Target] == Black)
    return true;

  if (Colors[Target] == Gray) {
    CycleDetected = true;
    CycleMsg = ("Circular dependency: " + llvm::Twine(Target)).str();
    return false;
  }

  Colors[Target] = Gray;

  // Populate the node. Use a block scope so the reference is not held
  // across recursive calls (insertions into Nodes can rehash and
  // invalidate references).
  {
    Node &N = Nodes[Target];
    N.Name = Target;

    auto AllRules = Rules.findAllRules(Target);
    N.IsPhony = Rules.isPhony(Target);

    if (!AllRules.empty()) {
      N.Rule = AllRules[0];
      for (auto *RR : AllRules) {
        if (!RR->Recipes.empty())
          N.Rule = RR;
      }
      // Collect prereqs from the recipe rule first so $< refers to the
      // correct first prerequisite (e.g. pattern rule prereq before
      // prereq-only explicit rules).
      if (N.Rule) {
        for (auto &Dep : N.Rule->Prerequisites)
          N.Dependencies.push_back(Dep);
        for (auto &Dep : N.Rule->OrderOnlyPrereqs)
          N.OrderOnlyDeps.push_back(Dep);
      }
      for (auto *RR : AllRules) {
        if (RR == N.Rule)
          continue;
        for (auto &Dep : RR->Prerequisites) {
          if (std::find(N.Dependencies.begin(), N.Dependencies.end(), Dep) ==
              N.Dependencies.end())
            N.Dependencies.push_back(Dep);
        }
        for (auto &Dep : RR->OrderOnlyPrereqs) {
          if (std::find(N.OrderOnlyDeps.begin(), N.OrderOnlyDeps.end(), Dep) ==
              N.OrderOnlyDeps.end())
            N.OrderOnlyDeps.push_back(Dep);
        }
      }
    }
  }

  // Copy dependency lists before recursion — recursive buildNode calls
  // insert new entries into Nodes, which may rehash the map.
  std::vector<std::string> Deps = Nodes[Target].Dependencies;
  std::vector<std::string> OODeps = Nodes[Target].OrderOnlyDeps;

  for (auto &Dep : Deps) {
    if (!buildNode(Dep, Rules, AlwaysMake, Colors))
      return false;
  }
  for (auto &Dep : OODeps) {
    if (!buildNode(Dep, Rules, AlwaysMake, Colors))
      return false;
  }

  // Re-lookup after recursion since the map may have been rehashed.
  Nodes[Target].NeedsBuild = needsRebuild(Nodes[Target]) || AlwaysMake;

  Colors[Target] = Black;
  return true;
}

bool DepGraph::needsRebuild(const Node &N) const {
  if (N.IsPhony)
    return true;

  if (!N.Rule)
    return false;

  int64_t TargetTime = platform::getFileTimestamp(N.Name);
  if (TargetTime < 0)
    return true;

  for (auto &Dep : N.Dependencies) {
    auto DepIt = Nodes.find(Dep);
    if (DepIt != Nodes.end()) {
      if (DepIt->second.IsPhony)
        return true;
      if (DepIt->second.NeedsBuild)
        return true;
    }

    int64_t DepTime = platform::getFileTimestamp(Dep);
    if (DepTime < 0)
      continue;
    if (DepTime > TargetTime)
      return true;
  }

  return false;
}

std::vector<std::vector<std::string>> DepGraph::topologicalLayers() const {
  llvm::StringMap<int> InDegree;
  for (auto &Entry : Nodes) {
    int Count = 0;
    for (auto &Dep : Entry.second.Dependencies) {
      if (Nodes.count(Dep))
        ++Count;
    }
    for (auto &Dep : Entry.second.OrderOnlyDeps) {
      if (Nodes.count(Dep))
        ++Count;
    }
    InDegree[Entry.first()] = Count;
  }

  std::vector<std::vector<std::string>> Layers;
  llvm::StringSet<> Processed;

  while (Processed.size() < Nodes.size()) {
    std::vector<std::string> Layer;
    for (auto &Entry : InDegree) {
      if (Entry.second == 0 && !Processed.count(Entry.first()))
        Layer.push_back(Entry.first().str());
    }

    if (Layer.empty())
      break;

    for (auto &Name : Layer) {
      Processed.insert(Name);
      for (auto &NodeEntry : Nodes) {
        if (Processed.count(NodeEntry.first()))
          continue;
        for (auto &Dep : NodeEntry.second.Dependencies) {
          if (Dep == Name)
            --InDegree[NodeEntry.first()];
        }
        for (auto &Dep : NodeEntry.second.OrderOnlyDeps) {
          if (Dep == Name)
            --InDegree[NodeEntry.first()];
        }
      }
    }
    Layers.push_back(std::move(Layer));
  }

  return Layers;
}

DepGraph::Node *DepGraph::getNode(const std::string &Name) {
  auto It = Nodes.find(Name);
  return It != Nodes.end() ? &It->second : nullptr;
}

const DepGraph::Node *DepGraph::getNode(const std::string &Name) const {
  auto It = Nodes.find(Name);
  return It != Nodes.end() ? &It->second : nullptr;
}

bool DepGraph::hasNode(const std::string &Name) const {
  return Nodes.count(Name) > 0;
}

} // namespace build
} // namespace neverc
