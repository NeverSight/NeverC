#ifndef NEVERC_LIB_DYNCODE_PLUGIN_DYNCODEPROOF_H
#define NEVERC_LIB_DYNCODE_PLUGIN_DYNCODEPROOF_H

// Host-issued proof for a dyncode phase output (Volume 6 task 5).
//
// Every dyncode phase output the host verifier accepts is bound to the exact
// pipeline value generation, artifact type, request digest and provider route
// that produced it.  A proof is only ever created by the host verifier; plugins
// receive an opaque NevercProofHandle they cannot forge.  Re-running a phase, a
// byte edit that bumps the value generation, or a proof minted for a different
// task/route all fail to match, so stale or forged proofs are rejected.

#include "neverc/Plugin/PluginCore.h"

#include <array>
#include <cstdint>

namespace neverc {
namespace dyncode {

class DynCodePipelineValue;

struct DynCodePhaseProof {
  const DynCodePipelineValue *Value = nullptr;
  uint64_t ValueGeneration = 0;
  NevercInterfaceID OutputArtifact{};
  NevercInterfaceID Phase{};
  std::array<uint8_t, 32> RouteDigest{};
  std::array<uint8_t, 32> RequestDigest{};
};

} // namespace dyncode
} // namespace neverc

#endif
