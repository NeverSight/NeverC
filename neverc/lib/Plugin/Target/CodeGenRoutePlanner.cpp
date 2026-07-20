#include "neverc/Plugin/Host/CodeGenRoutePlanner.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Twine.h"
#include <functional>
#include <map>
#include <set>

using namespace llvm;

namespace neverc::plugin {
namespace {

bool sameID(NevercInterfaceID Left, NevercInterfaceID Right) {
  return Left.High == Right.High && Left.Low == Right.Low;
}

bool nonzero(NevercInterfaceID ID) {
  return ID.High != 0 || ID.Low != 0;
}

Error routeError(const Twine &Message) {
  return createStringError(inconvertibleErrorCode(), Message);
}

bool validProductKind(NevercCodeGenProductKind Kind) {
  return (Kind >= NEVERC_CODEGEN_PRODUCT_IR &&
          Kind <= NEVERC_CODEGEN_PRODUCT_OBJECT_IMAGE) ||
         Kind >= NEVERC_CODEGEN_PRODUCT_CUSTOM;
}

bool dependencyOrderIsValid(
    ArrayRef<const PluginTargetSnapshot::CodeGenEdgeRecord *> Path) {
  std::set<std::pair<uint64_t, uint64_t>> Completed;
  for (const auto *Edge : Path) {
    for (NevercInterfaceID Dependency : Edge->Dependencies)
      if (!Completed.count({Dependency.High, Dependency.Low}))
        return false;
    Completed.insert({Edge->ID.High, Edge->ID.Low});
  }
  return true;
}

bool pathUsesProvider(
    ArrayRef<const PluginTargetSnapshot::CodeGenEdgeRecord *> Path,
    StringRef Provider) {
  return Provider.empty() ||
         llvm::any_of(Path, [&](const auto *Edge) {
           return Edge->ProviderID == Provider;
         });
}

} // namespace

Expected<PlannedCodeGenRoute> CodeGenRoutePlanner::plan(
    ArrayRef<PluginTargetSnapshot::CodeGenEdgeRecord> Edges,
    const CodeGenRouteRequest &Request) {
  if (!nonzero(Request.TargetID) ||
      !validProductKind(Request.InputKind) ||
      !validProductKind(Request.OutputKind) ||
      Request.InputKind == Request.OutputKind ||
      Request.CompatibilityKey.empty())
    return routeError("invalid codegen route request");

  std::vector<const PluginTargetSnapshot::CodeGenEdgeRecord *> Candidates;
  for (const auto &Edge : Edges) {
    if (!sameID(Edge.TargetID, Request.TargetID))
      continue;
    if (!validProductKind(Edge.InputKind) ||
        !validProductKind(Edge.OutputKind) ||
        Edge.InputKind == Edge.OutputKind || !nonzero(Edge.ID) ||
        !nonzero(Edge.ProductID))
      return routeError("invalid codegen edge '" + Edge.CanonicalName + "'");
    if (!Edge.CompatibilityKey.empty() &&
        Edge.CompatibilityKey != Request.CompatibilityKey)
      continue;
    if ((Edge.Flags & NEVERC_CODEGEN_EDGE_COARSE) != 0 &&
        !Edge.VerifyProduct)
      return routeError("codegen edge '" + Edge.CanonicalName +
                        "' is missing product verifier");
    if (Edge.InputKind < NEVERC_CODEGEN_PRODUCT_CUSTOM &&
        Edge.OutputKind < NEVERC_CODEGEN_PRODUCT_CUSTOM &&
        Edge.OutputKind > Edge.InputKind + 1 &&
        (Edge.Flags & (NEVERC_CODEGEN_EDGE_COARSE |
                       NEVERC_CODEGEN_EDGE_BUILTIN)) == 0)
      return routeError("codegen edge '" + Edge.CanonicalName +
                        "' skips mandatory products without a coarse "
                        "or built-in verifier boundary");
    Candidates.push_back(&Edge);
  }

  std::map<NevercCodeGenProductKind, unsigned> Visit;
  std::function<bool(NevercCodeGenProductKind)> HasCycle =
      [&](NevercCodeGenProductKind Kind) {
        unsigned &State = Visit[Kind];
        if (State == 1)
          return true;
        if (State == 2)
          return false;
        State = 1;
        for (const auto *Edge : Candidates)
          if (Edge->InputKind == Kind &&
              HasCycle(Edge->OutputKind))
            return true;
        State = 2;
        return false;
      };
  for (const auto *Edge : Candidates)
    if (HasCycle(Edge->InputKind))
      return routeError("codegen route graph contains a cycle");

  std::vector<std::vector<
      const PluginTargetSnapshot::CodeGenEdgeRecord *>>
      CompletePaths;
  std::vector<const PluginTargetSnapshot::CodeGenEdgeRecord *> Current;
  std::set<NevercCodeGenProductKind> ActiveKinds{Request.InputKind};
  std::function<void(NevercCodeGenProductKind)> Search =
      [&](NevercCodeGenProductKind Kind) {
        if (Kind == Request.OutputKind) {
          if (dependencyOrderIsValid(Current) &&
              pathUsesProvider(Current, Request.ForcedProvider))
            CompletePaths.push_back(Current);
          return;
        }
        for (const auto *Edge : Candidates) {
          if (Edge->InputKind != Kind ||
              ActiveKinds.count(Edge->OutputKind))
            continue;
          Current.push_back(Edge);
          ActiveKinds.insert(Edge->OutputKind);
          Search(Edge->OutputKind);
          ActiveKinds.erase(Edge->OutputKind);
          Current.pop_back();
          if (CompletePaths.size() > 1)
            return;
        }
      };
  Search(Request.InputKind);

  if (CompletePaths.empty()) {
    if (!Request.ForcedProvider.empty())
      return routeError("missing codegen edge for forced provider '" +
                        Request.ForcedProvider + "'");
    if (llvm::any_of(Edges, [&](const auto &Edge) {
          return sameID(Edge.TargetID, Request.TargetID) &&
                 !Edge.CompatibilityKey.empty() &&
                 Edge.CompatibilityKey != Request.CompatibilityKey;
        }))
      return routeError(
          "missing codegen edge with matching compatibility key");
    return routeError("missing codegen edge for requested product");
  }
  if (CompletePaths.size() != 1)
    return routeError("ambiguous codegen route: multiple complete paths");

  PlannedCodeGenRoute Result;
  Result.Edges = std::move(CompletePaths.front());
  Result.CompatibilityKey = Request.CompatibilityKey;
  return Result;
}

Expected<PlannedCodeGenRoute>
CodeGenRoutePlanner::plan(const PluginTargetSnapshot &Snapshot,
                          const CodeGenRouteRequest &Request) {
  return plan(Snapshot.codeGenEdges(), Request);
}

} // namespace neverc::plugin
