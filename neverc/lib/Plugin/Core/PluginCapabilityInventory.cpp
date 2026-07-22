//===- PluginCapabilityInventory.cpp - compiled-in capability dump -------===//
//
// Emits the host's compiled-in plugin capability inventory as JSON. The data
// comes from the generated phase-schema macros that are compiled into this
// binary, so the output is a real runtime reflection of what the host was
// built with, not a re-read of the JSON source. No user plugins are loaded.
//
//===----------------------------------------------------------------------===//

#include "neverc/Plugin/Host/PluginCapabilityInventory.h"

#include "neverc/Plugin/PluginPhaseSchema.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

namespace neverc::plugin {
namespace {

// A single compiled-in phase, materialized from the schema macros.
struct PhaseRow {
  const char *Symbol;
  const char *Name;
  const char *Domain;
  const char *Verifier;
  uint32_t Kind;
  uint32_t Gate;
  uint32_t Stability;
  uint32_t Observer;
  uint64_t Policy;
  uint64_t IdHigh;
  uint64_t IdLow;
  uint64_t InHigh;
  uint64_t InLow;
  uint64_t OutHigh;
  uint64_t OutLow;
  bool BuiltinFallback;
};

std::string hexID(uint64_t Value) {
  char Buffer[19];
  std::snprintf(Buffer, sizeof(Buffer), "0x%016llx",
                static_cast<unsigned long long>(Value));
  return std::string(Buffer);
}

const char *kindName(uint32_t Kind) {
  return Kind == NEVERC_PHASE_KIND_EVENT ? "event" : "transition";
}

const char *gateName(uint32_t Gate) {
  switch (Gate) {
  case NEVERC_PHASE_GATE_SEALED_VERIFIER:
    return "sealed_verifier";
  case NEVERC_PHASE_GATE_SEALED_COMMIT:
    return "sealed_commit";
  default:
    return "transition";
  }
}

const char *stabilityName(uint32_t Stability) {
  return Stability == NEVERC_PHASE_STABILITY_EXPERIMENTAL ? "experimental"
                                                          : "stable";
}

void emitPolicy(json::OStream &J, uint64_t Policy) {
  J.attributeArray("policy", [&] {
    // Emitted in ascending bit order to match the JSON schema source.
    if (Policy & NEVERC_PHASE_OBSERVABLE)
      J.value("OBSERVABLE");
    if (Policy & NEVERC_PHASE_INTERCEPTABLE)
      J.value("INTERCEPTABLE");
    if (Policy & NEVERC_PHASE_REPLACEABLE)
      J.value("REPLACEABLE");
    if (Policy & NEVERC_PHASE_SKIPPABLE_WITH_PROOF)
      J.value("SKIPPABLE_WITH_PROOF");
    if (Policy & NEVERC_PHASE_SEALED_HOST_GATE)
      J.value("SEALED_HOST_GATE");
  });
}

void emitObserverPoints(json::OStream &J, uint32_t Observer) {
  J.attributeArray("observer_points", [&] {
    if (Observer & NEVERC_OBSERVER_BEFORE)
      J.value("BEFORE");
    if (Observer & NEVERC_OBSERVER_AFTER)
      J.value("AFTER");
    if (Observer & NEVERC_OBSERVER_AFTER_COMMIT)
      J.value("AFTER_COMMIT");
  });
}

void emitID(json::OStream &J, StringRef Key, uint64_t High, uint64_t Low) {
  J.attributeArray(Key, [&] {
    J.value(hexID(High));
    J.value(hexID(Low));
  });
}

std::vector<PhaseRow> collectPhases() {
  std::vector<PhaseRow> Rows;
#define NEVERC_INVENTORY_ROW(Symbol)                                           \
  Rows.push_back(PhaseRow{                                                     \
      #Symbol, NEVERC_PHASE_##Symbol##_NAME, NEVERC_PHASE_##Symbol##_DOMAIN,   \
      NEVERC_PHASE_##Symbol##_VERIFIER, NEVERC_PHASE_##Symbol##_KIND,          \
      NEVERC_PHASE_##Symbol##_GATE, NEVERC_PHASE_##Symbol##_STABILITY,         \
      NEVERC_PHASE_##Symbol##_OBSERVER_POINTS, NEVERC_PHASE_##Symbol##_POLICY, \
      NEVERC_PHASE_##Symbol##_HIGH, NEVERC_PHASE_##Symbol##_LOW,               \
      NEVERC_PHASE_##Symbol##_INPUT_HIGH, NEVERC_PHASE_##Symbol##_INPUT_LOW,   \
      NEVERC_PHASE_##Symbol##_OUTPUT_HIGH, NEVERC_PHASE_##Symbol##_OUTPUT_LOW, \
      NEVERC_PHASE_##Symbol##_BUILTIN_FALLBACK == NEVERC_TRUE});
  NEVERC_FOR_EACH_BUILTIN_PHASE(NEVERC_INVENTORY_ROW)
#undef NEVERC_INVENTORY_ROW
  return Rows;
}

} // namespace

void emitCapabilityInventoryJSON(raw_ostream &OS) {
  const std::vector<PhaseRow> Rows = collectPhases();

  // Aggregate module (domain) and artifact views without loading any plugin.
  std::map<std::string, unsigned> ModulePhaseCount;
  std::map<std::string, unsigned> ModuleDefaultProviders;
  std::set<std::pair<uint64_t, uint64_t>> Artifacts;
  std::set<std::string> Verifiers;
  unsigned DefaultProviders = 0;
  for (const PhaseRow &Row : Rows) {
    ModulePhaseCount[Row.Domain]++;
    if (Row.BuiltinFallback) {
      ModuleDefaultProviders[Row.Domain]++;
      ++DefaultProviders;
    }
    if (Row.InHigh || Row.InLow)
      Artifacts.insert({Row.InHigh, Row.InLow});
    if (Row.OutHigh || Row.OutLow)
      Artifacts.insert({Row.OutHigh, Row.OutLow});
    Verifiers.insert(Row.Verifier);
  }

  json::OStream J(OS, 2);
  J.object([&] {
    J.attributeObject("abi", [&] {
      J.attribute("major", static_cast<int64_t>(NEVERC_PLUGIN_ABI_MAJOR));
      J.attribute("minor", static_cast<int64_t>(NEVERC_PLUGIN_ABI_MINOR));
    });
    J.attribute("phase_count",
                static_cast<int64_t>(NEVERC_BUILTIN_PHASE_COUNT));
    J.attribute("extension_family_count",
                static_cast<int64_t>(NEVERC_EXTENSION_FAMILY_COUNT));
    J.attribute("default_provider_count",
                static_cast<int64_t>(DefaultProviders));

    J.attributeArray("modules", [&] {
      for (const auto &Module : ModulePhaseCount) {
        J.object([&] {
          J.attribute("name", Module.first);
          J.attribute("phase_count", static_cast<int64_t>(Module.second));
          J.attribute(
              "default_providers",
              static_cast<int64_t>(ModuleDefaultProviders[Module.first]));
        });
      }
    });

    J.attributeArray("artifacts", [&] {
      for (const auto &Artifact : Artifacts)
        J.array([&] {
          J.value(hexID(Artifact.first));
          J.value(hexID(Artifact.second));
        });
    });

    J.attributeArray("verifiers", [&] {
      for (const std::string &Verifier : Verifiers)
        J.value(Verifier);
    });

    J.attributeArray("phases", [&] {
      for (const PhaseRow &Row : Rows) {
        J.object([&] {
          J.attribute("name", Row.Name);
          J.attribute("symbol", Row.Symbol);
          J.attribute("domain", Row.Domain);
          J.attribute("kind", kindName(Row.Kind));
          J.attribute("verifier", Row.Verifier);
          emitID(J, "id", Row.IdHigh, Row.IdLow);
          emitID(J, "input_artifact", Row.InHigh, Row.InLow);
          emitID(J, "output_artifact", Row.OutHigh, Row.OutLow);
          emitPolicy(J, Row.Policy);
          emitObserverPoints(J, Row.Observer);
          J.attribute("gate", gateName(Row.Gate));
          J.attribute("stability", stabilityName(Row.Stability));
          J.attribute("builtin_fallback", Row.BuiltinFallback);
          J.attribute("has_default_provider", Row.BuiltinFallback);
        });
      }
    });
  });
  OS << "\n";
}

} // namespace neverc::plugin
