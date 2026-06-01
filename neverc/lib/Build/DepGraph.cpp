#include "neverc/Build/DepGraph.h"
#include "neverc/Build/Platform.h"

#include <algorithm>
#include <unordered_set>

namespace neverc {
namespace build {

bool DepGraph::build(const std::string &Target, const RuleDB &Rules,
                      bool AlwaysMake) {
  std::unordered_map<std::string, Color> Colors;
  return buildNode(Target, Rules, AlwaysMake, Colors);
}

bool DepGraph::buildNode(const std::string &Target, const RuleDB &Rules,
                          bool AlwaysMake,
                          std::unordered_map<std::string, Color> &Colors) {
  if (Colors[Target] == Black)
    return true;

  if (Colors[Target] == Gray) {
    CycleDetected = true;
    CycleMsg = "Circular dependency: " + Target;
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
      for (auto *RR : AllRules) {
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
    if (DepIt != Nodes.end() && DepIt->second.IsPhony)
      return true;

    int64_t DepTime = platform::getFileTimestamp(Dep);
    if (DepTime < 0)
      continue;
    if (DepTime > TargetTime)
      return true;
  }

  return false;
}

std::vector<std::vector<std::string>> DepGraph::topologicalLayers() const {
  std::unordered_map<std::string, int> InDegree;
  for (auto &[Name, N] : Nodes) {
    int Count = 0;
    for (auto &Dep : N.Dependencies) {
      if (Nodes.count(Dep))
        ++Count;
    }
    for (auto &Dep : N.OrderOnlyDeps) {
      if (Nodes.count(Dep))
        ++Count;
    }
    InDegree[Name] = Count;
  }

  std::vector<std::vector<std::string>> Layers;
  std::unordered_set<std::string> Processed;

  while (Processed.size() < Nodes.size()) {
    std::vector<std::string> Layer;
    for (auto &[Name, Degree] : InDegree) {
      if (Degree == 0 && !Processed.count(Name))
        Layer.push_back(Name);
    }

    if (Layer.empty())
      break;

    for (auto &Name : Layer) {
      Processed.insert(Name);
      for (auto &[Other, OtherNode] : Nodes) {
        if (Processed.count(Other))
          continue;
        for (auto &Dep : OtherNode.Dependencies) {
          if (Dep == Name)
            --InDegree[Other];
        }
        for (auto &Dep : OtherNode.OrderOnlyDeps) {
          if (Dep == Name)
            --InDegree[Other];
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
