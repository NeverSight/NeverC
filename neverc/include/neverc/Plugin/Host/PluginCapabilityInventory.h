#ifndef NEVERC_PLUGIN_HOST_PLUGINCAPABILITYINVENTORY_H
#define NEVERC_PLUGIN_HOST_PLUGINCAPABILITYINVENTORY_H

namespace llvm {
class raw_ostream;
} // namespace llvm

namespace neverc::plugin {

/// Emit this host's compiled-in plugin capability inventory as JSON to \p OS.
///
/// The output is derived entirely from the phase schema that is baked into this
/// binary (via the generated Schema/PluginPhaseSchema.inc macros), so it is a
/// genuine runtime view of what the host offers: its modules, phases, phase
/// policies, gates, verifiers, artifacts and default (builtin fallback)
/// providers. It loads no user plugins and constructs no Session/Task state,
/// which makes it safe to call from a read-only `--print-plugin-capabilities`
/// query before any compilation is set up.
///
/// CI cross-checks parse this output and compare it against the static sources
/// of truth (PhaseSchema.json, the coverage manifest and the SDK/ABI
/// manifests). A mismatch means the shipped binary drifted from its schema —
/// for example a regenerated schema that was never recompiled.
void emitCapabilityInventoryJSON(llvm::raw_ostream &OS);

} // namespace neverc::plugin

#endif
