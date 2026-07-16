/*===-- PluginPhaseSchema.h - NeverC stable phase IDs ----------*- C -*-===*/

#ifndef NEVERC_PLUGIN_PLUGINPHASESCHEMA_H
#define NEVERC_PLUGIN_PLUGINPHASESCHEMA_H

#include "neverc/Plugin/PluginCore.h"

typedef uint32_t NevercPhaseKind;
#define NEVERC_PHASE_KIND_TRANSITION UINT32_C(0)
#define NEVERC_PHASE_KIND_EVENT UINT32_C(1)

typedef uint32_t NevercPhaseGate;
#define NEVERC_PHASE_GATE_TRANSITION UINT32_C(0)
#define NEVERC_PHASE_GATE_SEALED_VERIFIER UINT32_C(1)
#define NEVERC_PHASE_GATE_SEALED_COMMIT UINT32_C(2)

typedef uint32_t NevercPhaseStability;
#define NEVERC_PHASE_STABILITY_STABLE UINT32_C(0)
#define NEVERC_PHASE_STABILITY_EXPERIMENTAL UINT32_C(1)

#include "neverc/Plugin/Schema/PluginPhaseSchema.inc"

#endif
