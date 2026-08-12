//===- AndroidKernelReleasePublisher.h - Atomic release output --*- C++ -*-===//
//
// Part of the NeverC Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef NEVERC_FOUNDATION_ANDROIDKERNELRELEASEPUBLISHER_H
#define NEVERC_FOUNDATION_ANDROIDKERNELRELEASEPUBLISHER_H

#include "neverc/Foundation/AndroidKernelBuildState.h"
#include "neverc/Foundation/AndroidKernelReleaseSymbolMap.h"
#include "neverc/Foundation/Core/OutputBundleTransaction.h"
#include "neverc/Foundation/Core/OutputCoordinator.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

namespace neverc {

/// Publishes one final image together with its release map and explicit build
/// state. When \p Map is null, the transaction removes any stale map.
llvm::Expected<OutputBundleSummary>
publishAndroidKernelReleaseOutput(
    OutputCoordinator &Coordinator, llvm::StringRef ImagePath,
    llvm::ArrayRef<uint8_t> Image,
    const AndroidKernelReleaseSymbolMap *Map,
    const AndroidKernelBuildState &BuildState,
    OutputLeaseOwner LeaseOwner = {},
    OutputBundleSummary *FinalSummary = nullptr,
    OutputCoordinator::CancellationCheck IsCancelled = {});

/// Removes an Android-kernel image and every adjacent release/build-state
/// sidecar under the same cross-process publication lock used by publish.
llvm::Expected<OutputBundleSummary>
cleanAndroidKernelReleaseOutput(
    OutputCoordinator &Coordinator, llvm::StringRef ImagePath,
    OutputLeaseOwner LeaseOwner = {},
    OutputBundleSummary *FinalSummary = nullptr,
    OutputCoordinator::CancellationCheck IsCancelled = {});

} // namespace neverc

#endif // NEVERC_FOUNDATION_ANDROIDKERNELRELEASEPUBLISHER_H
