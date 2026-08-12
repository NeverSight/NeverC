//===- AndroidKernelReleasePaths.cpp - Adjacent release files -------------===//
//
// Part of the NeverC Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "neverc/Foundation/AndroidKernelReleasePaths.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Path.h"

namespace neverc {

std::string androidKernelAdjacentOutputPath(llvm::StringRef ImagePath,
                                            llvm::StringRef Filename) {
  llvm::SmallString<256> Path(llvm::sys::path::parent_path(ImagePath));
  llvm::sys::path::append(Path, Filename);
  return Path.str().str();
}

std::string formatAndroidKernelBuildIntegrity(
    llvm::StringRef ImageDigest, llvm::StringRef BuildIDDigest,
    llvm::StringRef BuildExtraDigest) {
  return "IMAGE_SHA256=" + ImageDigest.str() +
         " BUILD_ID_SHA256=" + BuildIDDigest.str() +
         " BUILD_EXTRA_SHA256=" + BuildExtraDigest.str();
}

} // namespace neverc
