#ifndef NEVERC_PLUGIN_OBJECT_ANDROIDKERNELRELEASEWRITERPREFLIGHT_H
#define NEVERC_PLUGIN_OBJECT_ANDROIDKERNELRELEASEWRITERPREFLIGHT_H

#include "neverc/Plugin/Host/ObjectGraph.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#include <vector>

namespace neverc::plugin {

/// Proves that one retained Android-release symbol can be represented by the
/// built-in ELF graph writer. \p FinalBinding is the binding after any planned
/// release demotion; the check is read-only and suitable before graph mutation.
llvm::Error verifyPortableAndroidKernelReleaseWriterSymbol(
    const PluginObjectSymbol &Symbol, NevercObjectSymbolBinding FinalBinding,
    llvm::StringRef Boundary);

/// Plans the exact native ELF extension bytes for FinalBinding after first
/// proving the symbol's current stable/native facts. The returned value is a
/// detached copy, so callers can validate every mutation before changing the
/// graph. Symbols without an extension retain an empty extension.
llvm::Expected<std::vector<uint8_t>>
planPortableAndroidKernelReleaseWriterSymbolExtension(
    const PluginObjectSymbol &Symbol, NevercObjectSymbolBinding FinalBinding,
    llvm::StringRef Boundary);

/// Proves that a retained section's native ELF extension carries no semantics
/// that the built-in graph writer would discard or rewrite differently.
llvm::Error verifyPortableAndroidKernelReleaseWriterSection(
    const PluginObjectSection &Section, llvm::StringRef Boundary);

/// Proves that a retained relocation has an exact AArch64 ELF spelling whose
/// native facts and target expression agree with the stable ObjectGraph.
llvm::Error verifyPortableAndroidKernelReleaseWriterRelocation(
    const PluginObjectGraph &Object, const PluginObjectRelocation &Relocation,
    llvm::StringRef Boundary);

/// Audits the exact NCSE v2 payload emitted by the built-in ELF reader for a
/// canonical native-image graph. Opaque native flags may survive here because
/// an unchanged graph is written by byte passthrough; the portable preflight
/// above remains stricter when a mutation makes the graph authoritative.
llvm::Error verifyCanonicalAndroidKernelReleaseReaderSection(
    const PluginObjectSection &Section, uint64_t ExpectedNativeIndex,
    llvm::StringRef Boundary);

/// Audits the exact NCRL v1 payload and its native-to-stable AArch64
/// projection for a canonical native-image graph.
llvm::Error verifyCanonicalAndroidKernelReleaseReaderRelocation(
    const PluginObjectGraph &Object,
    const PluginObjectRelocation &Relocation, llvm::StringRef Boundary);

/// Applies the same read-only proof to a graph that will be sent directly to
/// the built-in LLVM ELF writer with the Android release policy. This boundary
/// is intentionally not used by native-image passthrough or third-party
/// writers.
llvm::Error
verifyPortableAndroidKernelReleaseWriterGraph(const PluginObjectGraph &Object,
                                              llvm::StringRef Boundary);

} // namespace neverc::plugin

#endif // NEVERC_PLUGIN_OBJECT_ANDROIDKERNELRELEASEWRITERPREFLIGHT_H
