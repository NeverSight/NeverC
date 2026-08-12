//===- AndroidKernelReleaseWriterPolicy.h - Shared release policy -*- C++
//-*-===//
//
// Part of the NeverC Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef NEVERC_LIB_PLUGIN_ANDROIDKERNELRELEASEWRITERPOLICY_H
#define NEVERC_LIB_PLUGIN_ANDROIDKERNELRELEASEWRITERPOLICY_H

#include "neverc/Plugin/Host/ObjectGraph.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#include <cstdint>
#include <vector>

namespace neverc::plugin {

/// Proves that one retained Android-release symbol can be represented by the
/// built-in ELF graph writer. \p FinalBinding is the binding after any planned
/// release demotion; the check is read-only and suitable before graph mutation.
llvm::Error verifyPortableAndroidKernelReleaseWriterSymbol(
    const PluginObjectSymbol &Symbol, NevercObjectSymbolBinding FinalBinding,
    llvm::StringRef Boundary);

/// Plans detached native ELF extension bytes for \p FinalBinding after proving
/// the symbol's current stable/native facts.
llvm::Expected<std::vector<uint8_t>>
planPortableAndroidKernelReleaseWriterSymbolExtension(
    const PluginObjectSymbol &Symbol, NevercObjectSymbolBinding FinalBinding,
    llvm::StringRef Boundary);

/// Proves that retained graph entities preserve all native facts required by
/// the built-in portable Android release writer.
llvm::Error verifyPortableAndroidKernelReleaseWriterSection(
    const PluginObjectSection &Section, llvm::StringRef Boundary);
llvm::Error verifyPortableAndroidKernelReleaseWriterRelocation(
    const PluginObjectGraph &Object, const PluginObjectRelocation &Relocation,
    llvm::StringRef Boundary);
llvm::Error
verifyPortableAndroidKernelReleaseWriterGraph(const PluginObjectGraph &Object,
                                              llvm::StringRef Boundary);

/// Audits canonical native-reader provenance used by unchanged byte
/// passthrough. These checks do not grant portable-writer authority.
llvm::Error verifyCanonicalAndroidKernelReleaseReaderSection(
    const PluginObjectSection &Section, uint64_t ExpectedNativeIndex,
    llvm::StringRef Boundary);
llvm::Error verifyCanonicalAndroidKernelReleaseReaderRelocation(
    const PluginObjectGraph &Object, const PluginObjectRelocation &Relocation,
    llvm::StringRef Boundary);

} // namespace neverc::plugin

#endif // NEVERC_LIB_PLUGIN_ANDROIDKERNELRELEASEWRITERPOLICY_H
