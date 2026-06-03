#ifndef NEVERC_BUILD_DEPGRAPH_H
#define NEVERC_BUILD_DEPGRAPH_H

#include "neverc/Build/RuleDB.h"

#include "llvm/ADT/StringMap.h"

#include <string>
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

  const llvm::StringMap<Node> &nodes() const { return Nodes; }
  llvm::StringMap<Node> &nodes() { return Nodes; }

private:
  enum Color { White, Gray, Black };

  bool buildNode(const std::string &Target, const RuleDB &Rules,
                  bool AlwaysMake,
                  llvm::StringMap<Color> &Colors);

  bool needsRebuild(const Node &N) const;

  llvm::StringMap<Node> Nodes;
  bool CycleDetected = false;
  std::string CycleMsg;
};

} // namespace build
} // namespace neverc

#endif // NEVERC_BUILD_DEPGRAPH_H
