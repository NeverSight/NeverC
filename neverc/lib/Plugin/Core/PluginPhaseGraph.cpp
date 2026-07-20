#include "neverc/Plugin/Host/PluginPhaseGraph.h"
#include "neverc/Plugin/PluginPhaseSchema.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Twine.h"
#include <set>
#include <unordered_map>
#include <utility>

using namespace llvm;

namespace neverc::plugin {
namespace {

Error phaseError(const Twine &Message) {
  return createStringError(inconvertibleErrorCode(), Message);
}

bool nonzero(NevercInterfaceID ID) { return ID.High != 0 || ID.Low != 0; }

bool canonicalPhaseName(StringRef Name) {
  if (Name.empty() || Name.size() > 255)
    return false;
  for (char C : Name)
    if (!((C >= 'a' && C <= 'z') || (C >= '0' && C <= '9') || C == '.' ||
          C == '_' || C == '-'))
      return false;
  return Name.front() != '.' && Name.back() != '.' && !Name.contains("..");
}

size_t findPhaseIndex(ArrayRef<PluginPhaseDefinition> Phases,
                      NevercInterfaceID ID) {
  for (size_t I = 0; I != Phases.size(); ++I)
    if (samePluginInterfaceID(Phases[I].ID, ID))
      return I;
  return Phases.size();
}

PluginPhaseDefinition
builtinPhase(const char *Name, const char *Domain, const char *Verifier,
             uint64_t High, uint64_t Low, uint64_t InputHigh, uint64_t InputLow,
             uint64_t OutputHigh, uint64_t OutputLow, NevercPhasePolicy Policy,
             NevercObserverPoint ObserverPoints, NevercPhaseGate Gate,
             NevercPhaseStability Stability, NevercBool HasBuiltinFallback) {
  PluginPhaseDefinition Phase;
  Phase.ID = {High, Low};
  Phase.CanonicalName = Name;
  Phase.Domain = Domain;
  Phase.Verifier = Verifier;
  Phase.InputArtifact = {InputHigh, InputLow};
  Phase.OutputArtifact = {OutputHigh, OutputLow};
  Phase.Policy = Policy;
  Phase.ObserverPoints = ObserverPoints;
  Phase.Gate = static_cast<PluginPhaseGateKind>(Gate);
  Phase.Stability = static_cast<PluginPhaseStability>(Stability);
  Phase.HasBuiltinFallback = HasBuiltinFallback == NEVERC_TRUE;
  return Phase;
}

} // namespace

bool samePluginInterfaceID(NevercInterfaceID Left, NevercInterfaceID Right) {
  return Left.High == Right.High && Left.Low == Right.Low;
}

Error PluginPhaseGraph::addPhase(PluginPhaseDefinition Phase) {
  if (Finalized)
    return phaseError("cannot add a phase after graph finalization");
  if (!nonzero(Phase.ID))
    return phaseError("phase ID must be nonzero");
  if (!canonicalPhaseName(Phase.CanonicalName))
    return phaseError("phase has a non-canonical name");
  if (!canonicalPhaseName(Phase.Domain))
    return phaseError("phase has a non-canonical domain");
  if (!canonicalPhaseName(Phase.Verifier))
    return phaseError("phase has a non-canonical verifier");
  if (find(Phase.ID))
    return phaseError("duplicate phase ID");
  if (find(Phase.CanonicalName))
    return phaseError("duplicate phase canonical name");
  Phases.push_back(std::move(Phase));
  return Error::success();
}

Error PluginPhaseGraph::addEdge(NevercInterfaceID Before,
                                NevercInterfaceID After,
                                bool RequireCompatibleArtifacts) {
  if (Finalized)
    return phaseError("cannot add a phase edge after graph finalization");
  if (!nonzero(Before) || !nonzero(After))
    return phaseError("phase edge contains a null ID");
  if (samePluginInterfaceID(Before, After))
    return phaseError("phase graph contains a self edge");
  if (llvm::any_of(Edges, [&](const Edge &Existing) {
        return samePluginInterfaceID(Existing.Before, Before) &&
               samePluginInterfaceID(Existing.After, After);
      }))
    return Error::success();
  Edges.push_back({Before, After, RequireCompatibleArtifacts});
  return Error::success();
}

Error PluginPhaseGraph::finalize() {
  if (Finalized)
    return Error::success();
  constexpr NevercPhasePolicy KnownPolicy =
      NEVERC_PHASE_OBSERVABLE | NEVERC_PHASE_INTERCEPTABLE |
      NEVERC_PHASE_REPLACEABLE | NEVERC_PHASE_SKIPPABLE_WITH_PROOF |
      NEVERC_PHASE_SEALED_HOST_GATE;
  constexpr NevercObserverPoint KnownObserverPoints =
      NEVERC_OBSERVER_BEFORE | NEVERC_OBSERVER_AFTER |
      NEVERC_OBSERVER_AFTER_COMMIT;

  for (const PluginPhaseDefinition &Phase : Phases) {
    if (Phase.Gate != PluginPhaseGateKind::Transition &&
        Phase.Gate != PluginPhaseGateKind::SealedVerifier &&
        Phase.Gate != PluginPhaseGateKind::SealedCommit)
      return phaseError("phase '" + Phase.CanonicalName +
                        "' has an invalid gate kind");
    if (Phase.Stability != PluginPhaseStability::Stable &&
        Phase.Stability != PluginPhaseStability::Experimental)
      return phaseError("phase '" + Phase.CanonicalName +
                        "' has invalid stability");
    if ((Phase.Policy & ~KnownPolicy) != 0)
      return phaseError("phase '" + Phase.CanonicalName +
                        "' has unknown policy flags");
    if ((Phase.ObserverPoints & ~KnownObserverPoints) != 0)
      return phaseError("phase '" + Phase.CanonicalName +
                        "' has unknown observer points");
    if (Phase.ObserverPoints != 0 &&
        (Phase.Policy & NEVERC_PHASE_OBSERVABLE) == 0)
      return phaseError("phase '" + Phase.CanonicalName +
                        "' exposes observers without OBSERVABLE");
    bool Sealed = (Phase.Policy & NEVERC_PHASE_SEALED_HOST_GATE) != 0;
    if (Sealed &&
        (Phase.Policy & (NEVERC_PHASE_INTERCEPTABLE | NEVERC_PHASE_REPLACEABLE |
                         NEVERC_PHASE_SKIPPABLE_WITH_PROOF)) != 0)
      return phaseError("sealed phase '" + Phase.CanonicalName +
                        "' has replaceable policy");
    if (Sealed == (Phase.Gate == PluginPhaseGateKind::Transition))
      return phaseError("phase '" + Phase.CanonicalName +
                        "' gate kind disagrees with policy");
    if ((Phase.ObserverPoints & NEVERC_OBSERVER_AFTER_COMMIT) != 0 &&
        Phase.Gate != PluginPhaseGateKind::SealedCommit)
      return phaseError("phase '" + Phase.CanonicalName +
                        "' exposes after-commit outside a sealed commit");
    if ((Phase.Policy & NEVERC_PHASE_REPLACEABLE) != 0 &&
        !Phase.HasBuiltinFallback)
      return phaseError("replaceable phase '" + Phase.CanonicalName +
                        "' has no builtin fallback");
    if (!nonzero(Phase.InputArtifact) || !nonzero(Phase.OutputArtifact))
      return phaseError("phase '" + Phase.CanonicalName +
                        "' has null artifact types");
  }

  std::vector<std::vector<size_t>> Successors(Phases.size());
  std::vector<size_t> InDegree(Phases.size(), 0);
  for (const Edge &EdgeValue : Edges) {
    size_t Before = findPhaseIndex(Phases, EdgeValue.Before);
    size_t After = findPhaseIndex(Phases, EdgeValue.After);
    if (Before == Phases.size() || After == Phases.size())
      return phaseError("phase edge references an unknown phase");
    if (EdgeValue.RequireCompatibleArtifacts &&
        !samePluginInterfaceID(Phases[Before].OutputArtifact,
                               Phases[After].InputArtifact))
      return phaseError("phase edge from '" + Phases[Before].CanonicalName +
                        "' to '" + Phases[After].CanonicalName +
                        "' has incompatible artifact types");
    Successors[Before].push_back(After);
    ++InDegree[After];
  }

  std::set<size_t> Ready;
  for (size_t I = 0; I != InDegree.size(); ++I)
    if (InDegree[I] == 0)
      Ready.insert(I);
  Order.clear();
  while (!Ready.empty()) {
    size_t Current = *Ready.begin();
    Ready.erase(Ready.begin());
    Order.push_back(Current);
    for (size_t Next : Successors[Current])
      if (--InDegree[Next] == 0)
        Ready.insert(Next);
  }
  if (Order.size() != Phases.size()) {
    std::string Message = "phase graph cycle includes:";
    for (size_t I = 0; I != InDegree.size(); ++I)
      if (InDegree[I] != 0)
        Message += " " + Phases[I].CanonicalName;
    Order.clear();
    return phaseError(Message);
  }
  Finalized = true;
  return Error::success();
}

const PluginPhaseDefinition *
PluginPhaseGraph::find(NevercInterfaceID ID) const {
  size_t Index = findPhaseIndex(Phases, ID);
  return Index == Phases.size() ? nullptr : &Phases[Index];
}

const PluginPhaseDefinition *
PluginPhaseGraph::find(StringRef CanonicalName) const {
  auto It = llvm::find_if(Phases, [&](const PluginPhaseDefinition &Phase) {
    return Phase.CanonicalName == CanonicalName;
  });
  return It == Phases.end() ? nullptr : &*It;
}

Expected<PluginPhaseGraph> PluginPhaseGraph::createBuiltinDriverGraph() {
  PluginPhaseGraph Graph;
#define NEVERC_BUILD_BUILTIN_PHASE(Symbol)                                     \
  builtinPhase(                                                                \
      NEVERC_PHASE_##Symbol##_NAME, NEVERC_PHASE_##Symbol##_DOMAIN,            \
      NEVERC_PHASE_##Symbol##_VERIFIER, NEVERC_PHASE_##Symbol##_HIGH,          \
      NEVERC_PHASE_##Symbol##_LOW, NEVERC_PHASE_##Symbol##_INPUT_HIGH,         \
      NEVERC_PHASE_##Symbol##_INPUT_LOW, NEVERC_PHASE_##Symbol##_OUTPUT_HIGH,  \
      NEVERC_PHASE_##Symbol##_OUTPUT_LOW, NEVERC_PHASE_##Symbol##_POLICY,      \
      NEVERC_PHASE_##Symbol##_OBSERVER_POINTS, NEVERC_PHASE_##Symbol##_GATE,   \
      NEVERC_PHASE_##Symbol##_STABILITY,                                       \
      NEVERC_PHASE_##Symbol##_BUILTIN_FALLBACK),
  const PluginPhaseDefinition Builtins[] = {
      NEVERC_FOR_EACH_BUILTIN_DRIVER_PHASE(NEVERC_BUILD_BUILTIN_PHASE)};
#undef NEVERC_BUILD_BUILTIN_PHASE
  for (const PluginPhaseDefinition &Phase : Builtins)
    if (Error E = Graph.addPhase(Phase))
      return std::move(E);
  for (size_t I = 1; I != std::size(Builtins); ++I)
    if (Error E = Graph.addEdge(Builtins[I - 1].ID, Builtins[I].ID))
      return std::move(E);
  if (Error E = Graph.finalize())
    return std::move(E);
  return Graph;
}

Expected<PluginPhaseGraph> PluginPhaseGraph::createBuiltinSourceGraph() {
  PluginPhaseGraph Graph;
#define NEVERC_BUILD_BUILTIN_PHASE(Symbol)                                     \
  builtinPhase(                                                                \
      NEVERC_PHASE_##Symbol##_NAME, NEVERC_PHASE_##Symbol##_DOMAIN,            \
      NEVERC_PHASE_##Symbol##_VERIFIER, NEVERC_PHASE_##Symbol##_HIGH,          \
      NEVERC_PHASE_##Symbol##_LOW, NEVERC_PHASE_##Symbol##_INPUT_HIGH,         \
      NEVERC_PHASE_##Symbol##_INPUT_LOW, NEVERC_PHASE_##Symbol##_OUTPUT_HIGH,  \
      NEVERC_PHASE_##Symbol##_OUTPUT_LOW, NEVERC_PHASE_##Symbol##_POLICY,      \
      NEVERC_PHASE_##Symbol##_OBSERVER_POINTS, NEVERC_PHASE_##Symbol##_GATE,   \
      NEVERC_PHASE_##Symbol##_STABILITY,                                       \
      NEVERC_PHASE_##Symbol##_BUILTIN_FALLBACK),
  const PluginPhaseDefinition Builtins[] = {
      NEVERC_FOR_EACH_BUILTIN_SOURCE_PHASE(NEVERC_BUILD_BUILTIN_PHASE)
          NEVERC_FOR_EACH_BUILTIN_PREP_PHASE(NEVERC_BUILD_BUILTIN_PHASE)
              NEVERC_BUILD_BUILTIN_PHASE(SYNTAX_PARSE)
                  NEVERC_BUILD_BUILTIN_PHASE(SEMA_ANALYZE)
                  NEVERC_BUILD_BUILTIN_PHASE(SYNTAX_EXTENSION_DECLARATION)
                  NEVERC_BUILD_BUILTIN_PHASE(SYNTAX_EXTENSION_STATEMENT)
                      NEVERC_BUILD_BUILTIN_PHASE(SYNTAX_EXTENSION_EXPRESSION)
                          NEVERC_BUILD_BUILTIN_PHASE(SYNTAX_EXTENSION_TYPE_NAME)
                              NEVERC_BUILD_BUILTIN_PHASE(
                                  SYNTAX_EXTENSION_ATTRIBUTE)
                                  NEVERC_BUILD_BUILTIN_PHASE(
                                      SYNTAX_EXTENSION_KEYWORD)
                                      NEVERC_BUILD_BUILTIN_PHASE(
                                          SEMA_EXTENSION_EXPRESSION)
                                          NEVERC_BUILD_BUILTIN_PHASE(
                                              SEMA_EXTENSION_STATEMENT)
                                              NEVERC_BUILD_BUILTIN_PHASE(
                                                  SEMA_EXTENSION_DECLARATION)
                                                  NEVERC_BUILD_BUILTIN_PHASE(
                                                      SEMA_EXTENSION_TYPE)
                                                      NEVERC_BUILD_BUILTIN_PHASE(
                                                          SEMA_EXTENSION_LOOKUP)
                                                          NEVERC_BUILD_BUILTIN_PHASE(
                                                              SEMA_EXTENSION_CONVERSION)};
#undef NEVERC_BUILD_BUILTIN_PHASE
  for (const PluginPhaseDefinition &Phase : Builtins)
    if (Error E = Graph.addPhase(Phase))
      return std::move(E);
  for (size_t I = 1; I != NEVERC_BUILTIN_SOURCE_PHASE_COUNT; ++I)
    if (Error E = Graph.addEdge(Builtins[I - 1].ID, Builtins[I].ID, true))
      return std::move(E);
  if (Error E = Graph.addEdge(
          {NEVERC_PHASE_SYNTAX_PARSE_HIGH, NEVERC_PHASE_SYNTAX_PARSE_LOW},
          {NEVERC_PHASE_SEMA_ANALYZE_HIGH, NEVERC_PHASE_SEMA_ANALYZE_LOW},
          true))
    return std::move(E);
  if (Error E = Graph.finalize())
    return std::move(E);
  return Graph;
}

Expected<PluginPhaseGraph> PluginPhaseGraph::createBuiltinIRGraph() {
  PluginPhaseGraph Graph;
#define NEVERC_BUILD_BUILTIN_PHASE(Symbol)                                     \
  builtinPhase(                                                                \
      NEVERC_PHASE_##Symbol##_NAME, NEVERC_PHASE_##Symbol##_DOMAIN,            \
      NEVERC_PHASE_##Symbol##_VERIFIER, NEVERC_PHASE_##Symbol##_HIGH,          \
      NEVERC_PHASE_##Symbol##_LOW, NEVERC_PHASE_##Symbol##_INPUT_HIGH,         \
      NEVERC_PHASE_##Symbol##_INPUT_LOW, NEVERC_PHASE_##Symbol##_OUTPUT_HIGH,  \
      NEVERC_PHASE_##Symbol##_OUTPUT_LOW, NEVERC_PHASE_##Symbol##_POLICY,      \
      NEVERC_PHASE_##Symbol##_OBSERVER_POINTS, NEVERC_PHASE_##Symbol##_GATE,   \
      NEVERC_PHASE_##Symbol##_STABILITY,                                       \
      NEVERC_PHASE_##Symbol##_BUILTIN_FALLBACK),
  const PluginPhaseDefinition Builtins[] = {
      NEVERC_FOR_EACH_BUILTIN_IR_PHASE(NEVERC_BUILD_BUILTIN_PHASE)};
#undef NEVERC_BUILD_BUILTIN_PHASE
  for (const PluginPhaseDefinition &Phase : Builtins)
    if (Error E = Graph.addPhase(Phase))
      return std::move(E);

  const NevercInterfaceID Order[] = {
      {NEVERC_PHASE_IR_GENERATE_HIGH, NEVERC_PHASE_IR_GENERATE_LOW},
      {NEVERC_PHASE_IR_PASS_PRE_OPT_HIGH, NEVERC_PHASE_IR_PASS_PRE_OPT_LOW},
      {NEVERC_PHASE_IR_PASS_PIPELINE_START_HIGH,
       NEVERC_PHASE_IR_PASS_PIPELINE_START_LOW},
      {NEVERC_PHASE_IR_OPTIMIZE_HIGH, NEVERC_PHASE_IR_OPTIMIZE_LOW},
      {NEVERC_PHASE_IR_PASS_OPTIMIZER_LAST_HIGH,
       NEVERC_PHASE_IR_PASS_OPTIMIZER_LAST_LOW},
      {NEVERC_PHASE_IR_PASS_POST_OPT_HIGH,
       NEVERC_PHASE_IR_PASS_POST_OPT_LOW},
      {NEVERC_PHASE_IR_PASS_PRE_CODEGEN_HIGH,
       NEVERC_PHASE_IR_PASS_PRE_CODEGEN_LOW},
      {NEVERC_PHASE_IR_FINAL_VERIFY_HIGH,
       NEVERC_PHASE_IR_FINAL_VERIFY_LOW}};
  for (size_t I = 1; I != std::size(Order); ++I)
    if (Error E = Graph.addEdge(Order[I - 1], Order[I], true))
      return std::move(E);
  if (Error E = Graph.finalize())
    return std::move(E);
  return Graph;
}

Expected<PluginPhaseGraph> PluginPhaseGraph::createBuiltinCodeGenGraph() {
  PluginPhaseGraph Graph;
#define NEVERC_BUILD_BUILTIN_PHASE(Symbol)                                    \
  builtinPhase(                                                               \
      NEVERC_PHASE_##Symbol##_NAME, NEVERC_PHASE_##Symbol##_DOMAIN,           \
      NEVERC_PHASE_##Symbol##_VERIFIER, NEVERC_PHASE_##Symbol##_HIGH,         \
      NEVERC_PHASE_##Symbol##_LOW, NEVERC_PHASE_##Symbol##_INPUT_HIGH,        \
      NEVERC_PHASE_##Symbol##_INPUT_LOW, NEVERC_PHASE_##Symbol##_OUTPUT_HIGH, \
      NEVERC_PHASE_##Symbol##_OUTPUT_LOW, NEVERC_PHASE_##Symbol##_POLICY,     \
      NEVERC_PHASE_##Symbol##_OBSERVER_POINTS, NEVERC_PHASE_##Symbol##_GATE,  \
      NEVERC_PHASE_##Symbol##_STABILITY,                                      \
      NEVERC_PHASE_##Symbol##_BUILTIN_FALLBACK),
  const PluginPhaseDefinition Builtins[] = {
      NEVERC_FOR_EACH_BUILTIN_CODEGEN_PHASE(NEVERC_BUILD_BUILTIN_PHASE)};
#undef NEVERC_BUILD_BUILTIN_PHASE
  for (const PluginPhaseDefinition &Phase : Builtins)
    if (Error E = Graph.addPhase(Phase))
      return std::move(E);
  if (Error E = Graph.addEdge(
          {NEVERC_PHASE_CODEGEN_IR_TO_MIR_HIGH,
           NEVERC_PHASE_CODEGEN_IR_TO_MIR_LOW},
          {NEVERC_PHASE_CODEGEN_MIR_TO_MC_HIGH,
           NEVERC_PHASE_CODEGEN_MIR_TO_MC_LOW},
          true))
    return std::move(E);
  if (Error E = Graph.finalize())
    return std::move(E);
  return Graph;
}

Expected<PluginPhaseGraph> PluginPhaseGraph::createBuiltinMCGraph() {
  PluginPhaseGraph Graph;
#define NEVERC_BUILD_BUILTIN_PHASE(Symbol)                                    \
  builtinPhase(                                                               \
      NEVERC_PHASE_##Symbol##_NAME, NEVERC_PHASE_##Symbol##_DOMAIN,           \
      NEVERC_PHASE_##Symbol##_VERIFIER, NEVERC_PHASE_##Symbol##_HIGH,         \
      NEVERC_PHASE_##Symbol##_LOW, NEVERC_PHASE_##Symbol##_INPUT_HIGH,        \
      NEVERC_PHASE_##Symbol##_INPUT_LOW, NEVERC_PHASE_##Symbol##_OUTPUT_HIGH, \
      NEVERC_PHASE_##Symbol##_OUTPUT_LOW, NEVERC_PHASE_##Symbol##_POLICY,     \
      NEVERC_PHASE_##Symbol##_OBSERVER_POINTS, NEVERC_PHASE_##Symbol##_GATE,  \
      NEVERC_PHASE_##Symbol##_STABILITY,                                      \
      NEVERC_PHASE_##Symbol##_BUILTIN_FALLBACK),
  const PluginPhaseDefinition Builtins[] = {
      NEVERC_FOR_EACH_BUILTIN_MC_PHASE(NEVERC_BUILD_BUILTIN_PHASE)};
#undef NEVERC_BUILD_BUILTIN_PHASE
  for (const PluginPhaseDefinition &Phase : Builtins)
    if (Error E = Graph.addPhase(Phase))
      return std::move(E);
  if (Error E = Graph.finalize())
    return std::move(E);
  return Graph;
}

Expected<PluginPhaseGraph> PluginPhaseGraph::createBuiltinObjectGraph() {
  PluginPhaseGraph Graph;
#define NEVERC_BUILD_BUILTIN_PHASE(Symbol)                                    \
  builtinPhase(                                                               \
      NEVERC_PHASE_##Symbol##_NAME, NEVERC_PHASE_##Symbol##_DOMAIN,           \
      NEVERC_PHASE_##Symbol##_VERIFIER, NEVERC_PHASE_##Symbol##_HIGH,         \
      NEVERC_PHASE_##Symbol##_LOW, NEVERC_PHASE_##Symbol##_INPUT_HIGH,        \
      NEVERC_PHASE_##Symbol##_INPUT_LOW, NEVERC_PHASE_##Symbol##_OUTPUT_HIGH, \
      NEVERC_PHASE_##Symbol##_OUTPUT_LOW, NEVERC_PHASE_##Symbol##_POLICY,     \
      NEVERC_PHASE_##Symbol##_OBSERVER_POINTS, NEVERC_PHASE_##Symbol##_GATE,  \
      NEVERC_PHASE_##Symbol##_STABILITY,                                      \
      NEVERC_PHASE_##Symbol##_BUILTIN_FALLBACK),
  const PluginPhaseDefinition Builtins[] = {
      NEVERC_FOR_EACH_BUILTIN_OBJECT_PHASE(NEVERC_BUILD_BUILTIN_PHASE)};
#undef NEVERC_BUILD_BUILTIN_PHASE
  for (const PluginPhaseDefinition &Phase : Builtins)
    if (Error E = Graph.addPhase(Phase))
      return std::move(E);

  const NevercInterfaceID Ordered[] = {
      {NEVERC_PHASE_OBJECT_PROBE_HIGH, NEVERC_PHASE_OBJECT_PROBE_LOW},
      {NEVERC_PHASE_OBJECT_READ_HIGH, NEVERC_PHASE_OBJECT_READ_LOW},
      {NEVERC_PHASE_OBJECT_PRE_WRITE_HIGH,
       NEVERC_PHASE_OBJECT_PRE_WRITE_LOW},
      {NEVERC_PHASE_OBJECT_POST_LAYOUT_HIGH,
       NEVERC_PHASE_OBJECT_POST_LAYOUT_LOW},
      {NEVERC_PHASE_OBJECT_WRITE_HIGH, NEVERC_PHASE_OBJECT_WRITE_LOW},
      {NEVERC_PHASE_OBJECT_POST_WRITE_HIGH,
       NEVERC_PHASE_OBJECT_POST_WRITE_LOW},
      {NEVERC_PHASE_OBJECT_FINAL_VERIFY_HIGH,
       NEVERC_PHASE_OBJECT_FINAL_VERIFY_LOW},
      {NEVERC_PHASE_OBJECT_COMMIT_HIGH, NEVERC_PHASE_OBJECT_COMMIT_LOW},
  };
  for (size_t Index = 1; Index != std::size(Ordered); ++Index)
    if (Error E = Graph.addEdge(
            Ordered[Index - 1], Ordered[Index],
            Index >= 2))
      return std::move(E);
  if (Error E = Graph.finalize())
    return std::move(E);
  return Graph;
}

Expected<PluginPhaseGraph> PluginPhaseGraph::createBuiltinLinkGraph() {
  PluginPhaseGraph Graph;
#define NEVERC_BUILD_BUILTIN_PHASE(Symbol)                                    \
  builtinPhase(                                                               \
      NEVERC_PHASE_##Symbol##_NAME, NEVERC_PHASE_##Symbol##_DOMAIN,           \
      NEVERC_PHASE_##Symbol##_VERIFIER, NEVERC_PHASE_##Symbol##_HIGH,         \
      NEVERC_PHASE_##Symbol##_LOW, NEVERC_PHASE_##Symbol##_INPUT_HIGH,        \
      NEVERC_PHASE_##Symbol##_INPUT_LOW, NEVERC_PHASE_##Symbol##_OUTPUT_HIGH, \
      NEVERC_PHASE_##Symbol##_OUTPUT_LOW, NEVERC_PHASE_##Symbol##_POLICY,     \
      NEVERC_PHASE_##Symbol##_OBSERVER_POINTS, NEVERC_PHASE_##Symbol##_GATE,  \
      NEVERC_PHASE_##Symbol##_STABILITY,                                      \
      NEVERC_PHASE_##Symbol##_BUILTIN_FALLBACK),
  const PluginPhaseDefinition Builtins[] = {
      NEVERC_FOR_EACH_BUILTIN_LINK_PHASE(NEVERC_BUILD_BUILTIN_PHASE)};
#undef NEVERC_BUILD_BUILTIN_PHASE
  for (const PluginPhaseDefinition &Phase : Builtins)
    if (Error E = Graph.addPhase(Phase))
      return std::move(E);

  const NevercInterfaceID Transitions[] = {
      {NEVERC_PHASE_LINK_INPUT_PROBE_HIGH,
       NEVERC_PHASE_LINK_INPUT_PROBE_LOW},
      {NEVERC_PHASE_LINK_READ_INPUTS_HIGH,
       NEVERC_PHASE_LINK_READ_INPUTS_LOW},
      {NEVERC_PHASE_LINK_LTO_RESOLVE_HIGH,
       NEVERC_PHASE_LINK_LTO_RESOLVE_LOW},
      {NEVERC_PHASE_LINK_LTO_GENERATE_HIGH,
       NEVERC_PHASE_LINK_LTO_GENERATE_LOW},
      {NEVERC_PHASE_LINK_RESOLVE_SYMBOLS_HIGH,
       NEVERC_PHASE_LINK_RESOLVE_SYMBOLS_LOW},
      {NEVERC_PHASE_LINK_SELECT_COMDAT_HIGH,
       NEVERC_PHASE_LINK_SELECT_COMDAT_LOW},
      {NEVERC_PHASE_LINK_GC_HIGH, NEVERC_PHASE_LINK_GC_LOW},
      {NEVERC_PHASE_LINK_ICF_HIGH, NEVERC_PHASE_LINK_ICF_LOW},
      {NEVERC_PHASE_LINK_SYNTHESIZE_HIGH,
       NEVERC_PHASE_LINK_SYNTHESIZE_LOW},
      {NEVERC_PHASE_LINK_RELAX_THUNKS_HIGH,
       NEVERC_PHASE_LINK_RELAX_THUNKS_LOW},
      {NEVERC_PHASE_LINK_LAYOUT_HIGH, NEVERC_PHASE_LINK_LAYOUT_LOW},
      {NEVERC_PHASE_LINK_RELOCATE_HIGH, NEVERC_PHASE_LINK_RELOCATE_LOW},
      {NEVERC_PHASE_LINK_EMIT_IMAGE_HIGH,
       NEVERC_PHASE_LINK_EMIT_IMAGE_LOW},
  };
  for (size_t Index = 1; Index != std::size(Transitions); ++Index)
    if (Error E =
            Graph.addEdge(Transitions[Index - 1], Transitions[Index], true))
      return std::move(E);

  if (Error E = Graph.addEdge(
          {NEVERC_PHASE_LINK_READ_INPUTS_HIGH,
           NEVERC_PHASE_LINK_READ_INPUTS_LOW},
          {NEVERC_PHASE_LINK_OBJECT_MERGE_HIGH,
           NEVERC_PHASE_LINK_OBJECT_MERGE_LOW},
          true))
    return std::move(E);
  if (Error E = Graph.addEdge(
          {NEVERC_PHASE_LINK_EMIT_IMAGE_HIGH,
           NEVERC_PHASE_LINK_EMIT_IMAGE_LOW},
          {NEVERC_PHASE_LINK_POST_EMIT_HIGH,
           NEVERC_PHASE_LINK_POST_EMIT_LOW},
          true))
    return std::move(E);
  if (Error E = Graph.addEdge(
          {NEVERC_PHASE_LINK_FULL_HIGH, NEVERC_PHASE_LINK_FULL_LOW},
          {NEVERC_PHASE_LINK_POST_EMIT_HIGH,
           NEVERC_PHASE_LINK_POST_EMIT_LOW},
          true))
    return std::move(E);

  const NevercInterfaceID Gates[] = {
      {NEVERC_PHASE_LINK_POST_EMIT_HIGH,
       NEVERC_PHASE_LINK_POST_EMIT_LOW},
      {NEVERC_PHASE_LINK_IMAGE_VERIFY_HIGH,
       NEVERC_PHASE_LINK_IMAGE_VERIFY_LOW},
      {NEVERC_PHASE_LINK_SIDE_OUTPUTS_VERIFY_HIGH,
       NEVERC_PHASE_LINK_SIDE_OUTPUTS_VERIFY_LOW},
      {NEVERC_PHASE_LINK_COMMIT_HIGH, NEVERC_PHASE_LINK_COMMIT_LOW},
      {NEVERC_PHASE_LINK_AFTER_COMMIT_HIGH,
       NEVERC_PHASE_LINK_AFTER_COMMIT_LOW},
  };
  for (size_t Index = 1; Index != std::size(Gates); ++Index)
    if (Error E = Graph.addEdge(Gates[Index - 1], Gates[Index], true))
      return std::move(E);
  if (Error E = Graph.finalize())
    return std::move(E);
  return Graph;
}

} // namespace neverc::plugin
