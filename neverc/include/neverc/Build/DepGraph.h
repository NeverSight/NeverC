#ifndef NEVERC_BUILD_DEPGRAPH_H
#define NEVERC_BUILD_DEPGRAPH_H

#include "neverc/Build/RuleDB.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace neverc {
namespace build {

class DepGraph {
public:
  struct Node {
    std::string Name;
    const ResolvedRule *Rule = nullptr;
    std::vector<std::string> Dependencies;
    std::vector<std::string> OrderOnlyDeps;
    bool NeedsBuild = false;
    bool IsPhony = false;
    bool Built = false;
    bool Failed = false;
  };

  bool build(const std::string &Target, const RuleDB &Rules,
             bool AlwaysMake = false);

  bool hasCycle() const { return CycleDetected; }
  const std::string &cycleMessage() const { return CycleMsg; }

  std::vector<std::vector<std::string>> topologicalLayers() const;

  Node *getNode(const std::string &Name);
  const Node *getNode(const std::string &Name) const;
  bool hasNode(const std::string &Name) const;

  const std::unordered_map<std::string, Node> &nodes() const {
    return Nodes;
  }
  std::unordered_map<std::string, Node> &nodes() { return Nodes; }

private:
  enum Color { White, Gray, Black };

  bool buildNode(const std::string &Target, const RuleDB &Rules,
                  bool AlwaysMake,
                  std::unordered_map<std::string, Color> &Colors);

  bool needsRebuild(const Node &N) const;

  std::unordered_map<std::string, Node> Nodes;
  bool CycleDetected = false;
  std::string CycleMsg;
};

} // namespace build
} // namespace neverc

#endif // NEVERC_BUILD_DEPGRAPH_H
