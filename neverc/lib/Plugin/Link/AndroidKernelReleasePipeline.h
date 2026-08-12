//===- AndroidKernelReleasePipeline.h - Typed release finalization -*- C++
//-*-===//
//
// Part of the NeverC Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef NEVERC_LIB_PLUGIN_LINK_ANDROIDKERNELRELEASEPIPELINE_H
#define NEVERC_LIB_PLUGIN_LINK_ANDROIDKERNELRELEASEPIPELINE_H

#include "AndroidKernelModuleFinalizer.h"
#include "AndroidKernelReleaseInputVerifier.h"
#include "ObjectMergeProvider.h"
#include "neverc/Foundation/Core/OutputCoordinator.h"
#include "neverc/Plugin/Host/ObjectPhaseHooks.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#include <cstdint>
#include <memory>
#include <optional>

namespace neverc::plugin {

enum class AndroidKernelReleaseAuthority : uint8_t {
  NativeAttested,
  GraphAuthoritative,
};

enum class AndroidKernelReleaseSymbolMapSource : uint8_t {
  None,
  NativeMerger,
  GraphFinalizer,
};

struct AndroidKernelReleasePipelineResult {
  AndroidKernelReleaseAuthority Authority =
      AndroidKernelReleaseAuthority::GraphAuthoritative;
  AndroidKernelReleaseSymbolMapSource SymbolMapSource =
      AndroidKernelReleaseSymbolMapSource::None;
  std::optional<AndroidKernelReleaseSymbolMap> SymbolMap;
  std::shared_ptr<const AndroidKernelReleaseBoundOutputContract>
      BoundNativeOutput;
  ObjectPhaseSemanticValidators Validators;

  bool usesNativeImage() const {
    return Authority == AndroidKernelReleaseAuthority::NativeAttested;
  }
};

/// Finalizes a release graph, chooses its sole authoritative byte route, and
/// binds validators and one symbol-map producer to that route.
llvm::Expected<AndroidKernelReleasePipelineResult>
finalizeAndroidKernelReleasePipeline(
    ObjectMergeResult &Merged, bool ProviderBuiltin,
    const AndroidKernelReleaseInputContract &InputContract,
    AndroidKernelModuleFinalizationPolicy Policy);

/// Creates publication mechanics for an already finalized release result.
ObjectPublicationHooks createAndroidKernelReleasePublicationHooks(
    OutputCoordinator &Coordinator, llvm::StringRef OutputPath,
    bool PublishesReleaseMap,
    std::optional<AndroidKernelReleaseSymbolMap> SymbolMap,
    OutputLeaseOwner LeaseOwner,
    OutputCoordinator::CancellationCheck IsCancelled);

} // namespace neverc::plugin

#endif // NEVERC_LIB_PLUGIN_LINK_ANDROIDKERNELRELEASEPIPELINE_H
