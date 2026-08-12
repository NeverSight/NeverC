//===- AndroidKernelReleaseBoundary.h - Typed diagnostic boundaries -*- C++
//-*-===//
//
// Part of the NeverC Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef NEVERC_LIB_PLUGIN_LINK_ANDROIDKERNELRELEASEBOUNDARY_H
#define NEVERC_LIB_PLUGIN_LINK_ANDROIDKERNELRELEASEBOUNDARY_H

#include "llvm/ADT/StringRef.h"

#include <cstdint>

namespace neverc::plugin {

enum class ReleaseBoundary : uint8_t {
  ObjectMergeProvider,
  FinalObjectMergeProviderOutput,
  FinalAuthoritativePreHookGraph,
  FinalPreWriteOutput,
  GraphImmutableIdentityContract,
  FinalTrustedPrePostWriteImage,
  FinalPostWriteOutput,
  ImageImmutableIdentityContract,
  FinalBoundNativeOutputContract,
  FinalReleaseInputContract,
  FinalReleaseSymbolMap,
};

inline llvm::StringRef releaseBoundaryText(ReleaseBoundary Boundary) {
  switch (Boundary) {
  case ReleaseBoundary::ObjectMergeProvider:
    return "Android kernel ObjectMergeProvider boundary";
  case ReleaseBoundary::FinalObjectMergeProviderOutput:
    return "final Android kernel ObjectMergeProvider output";
  case ReleaseBoundary::FinalAuthoritativePreHookGraph:
    return "final Android kernel authoritative pre-hook graph";
  case ReleaseBoundary::FinalPreWriteOutput:
    return "final Android kernel pre-write output";
  case ReleaseBoundary::GraphImmutableIdentityContract:
    return "final Android kernel graph immutable identity contract";
  case ReleaseBoundary::FinalTrustedPrePostWriteImage:
    return "final Android kernel trusted pre-post-write image";
  case ReleaseBoundary::FinalPostWriteOutput:
    return "final Android kernel post-write output";
  case ReleaseBoundary::ImageImmutableIdentityContract:
    return "final Android kernel image immutable identity contract";
  case ReleaseBoundary::FinalBoundNativeOutputContract:
    return "final Android kernel bound native output contract";
  case ReleaseBoundary::FinalReleaseInputContract:
    return "final Android kernel release input contract";
  case ReleaseBoundary::FinalReleaseSymbolMap:
    return "final Android kernel release symbol map";
  }
  return "Android kernel release boundary";
}

} // namespace neverc::plugin

#endif // NEVERC_LIB_PLUGIN_LINK_ANDROIDKERNELRELEASEBOUNDARY_H
