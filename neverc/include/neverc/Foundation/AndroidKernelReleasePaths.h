//===- AndroidKernelReleasePaths.h - Adjacent release files -----*- C++ -*-===//
//
// Part of the NeverC Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef NEVERC_FOUNDATION_ANDROIDKERNELRELEASEPATHS_H
#define NEVERC_FOUNDATION_ANDROIDKERNELRELEASEPATHS_H

#include "llvm/ADT/StringRef.h"

#include <string>

namespace neverc {

inline constexpr llvm::StringLiteral AndroidKernelBuildFlagsFilename =
    ".nvk-build-flags";
inline constexpr llvm::StringLiteral AndroidKernelBuildExtraFilename =
    ".nvk-build-extra";
inline constexpr llvm::StringLiteral AndroidKernelBuildIntegrityFilename =
    ".nvk-build-integrity";
inline constexpr llvm::StringLiteral AndroidKernelLegacyReleaseFilename =
    ".nvk-release-bundle";

std::string androidKernelAdjacentOutputPath(llvm::StringRef ImagePath,
                                            llvm::StringRef Filename);

std::string formatAndroidKernelBuildIntegrity(
    llvm::StringRef ImageDigest, llvm::StringRef BuildIDDigest,
    llvm::StringRef BuildExtraDigest);

} // namespace neverc

#endif // NEVERC_FOUNDATION_ANDROIDKERNELRELEASEPATHS_H
