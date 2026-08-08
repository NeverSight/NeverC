#ifndef NEVERC_PLUGIN_LINK_ANDROIDKERNELPROFILECONTRACTVERIFIER_H
#define NEVERC_PLUGIN_LINK_ANDROIDKERNELPROFILECONTRACTVERIFIER_H

#include "neverc/Plugin/Host/ObjectGraph.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <cstdint>

namespace neverc::plugin {

/// Read the one native Android-kernel contract carried by a typed object graph
/// or serialized ELF image.  Missing, malformed, or internally inconsistent
/// records are errors; the returned value is still opaque to the plugin layer.
llvm::Expected<uint64_t>
readAndroidKernelProfileContract(const PluginObjectGraph &Object,
                                 llvm::StringRef Context);
llvm::Expected<uint64_t>
readAndroidKernelProfileContract(llvm::ArrayRef<uint8_t> Image,
                                 llvm::StringRef Context);

/// Require every input to carry the same contract and return that contract.
llvm::Expected<uint64_t> requireMatchingAndroidKernelProfileContracts(
    llvm::ArrayRef<PluginObjectGraph *> Objects, llvm::StringRef Boundary);
llvm::Expected<uint64_t> requireMatchingAndroidKernelProfileContracts(
    llvm::ArrayRef<llvm::StringRef> Images, llvm::StringRef Boundary);

/// Require an intermediate partial-link output to preserve its validated input
/// contract exactly so it remains eligible for a later checked link.
llvm::Error requireAndroidKernelProfileContract(
    const PluginObjectGraph &Object, uint64_t Expected,
    llvm::StringRef Boundary);
llvm::Error requireAndroidKernelProfileContract(llvm::ArrayRef<uint8_t> Image,
                                                uint64_t Expected,
                                                llvm::StringRef Boundary);

/// Remove the intermediate profile-contract section and all symbols defined in
/// it before publishing a final `.ko`.  A retained section may not reference
/// anything being removed.
llvm::Error stripAndroidKernelProfileContract(PluginObjectGraph &Object,
                                              llvm::StringRef Boundary);

/// Host-owned final-output invariant: object phases may not reintroduce the
/// profile-contract section or symbol after finalization.
llvm::Error forbidAndroidKernelProfileContract(const PluginObjectGraph &Object,
                                               llvm::StringRef Boundary);
llvm::Error forbidAndroidKernelProfileContract(llvm::ArrayRef<uint8_t> Image,
                                               llvm::StringRef Boundary);

} // namespace neverc::plugin

#endif // NEVERC_PLUGIN_LINK_ANDROIDKERNELPROFILECONTRACTVERIFIER_H
