#ifndef NEVERC_PLUGIN_HOST_TARGETSCHEMADIGEST_H
#define NEVERC_PLUGIN_HOST_TARGETSCHEMADIGEST_H

#include "llvm/ADT/StringRef.h"
#include <string>

namespace neverc::plugin {

struct BuiltinTargetSchema;

/// Compute the LOCKSTEP digest for a fully populated builtin target schema.
std::string computeTargetSchemaDigest(const BuiltinTargetSchema &Schema);

/// SHA-256 hex digest over an already-canonical JSON payload.
std::string digestCanonicalTargetSchemaJSON(llvm::StringRef CanonicalJSON);

} // namespace neverc::plugin

#endif
