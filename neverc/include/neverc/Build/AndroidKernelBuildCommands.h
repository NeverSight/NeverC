//===- AndroidKernelBuildCommands.h - Android build helpers ----*- C++ -*-===//
//
// Part of the NeverC Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef NEVERC_BUILD_ANDROIDKERNELBUILDCOMMANDS_H
#define NEVERC_BUILD_ANDROIDKERNELBUILDCOMMANDS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"

#include <optional>

namespace neverc {
namespace build {

enum class AndroidKernelBuildCommandKind {
  None,
  CleanOutput,
  OutputIntegrity,
};

AndroidKernelBuildCommandKind
classifyAndroidKernelBuildCommand(llvm::StringRef Command);

/// Dispatch a private command used by generated Android kernel build recipes.
/// Returns nullopt when \p Args do not name an Android kernel build command.
std::optional<int>
dispatchAndroidKernelBuildCommand(llvm::ArrayRef<const char *> Args);

} // namespace build
} // namespace neverc

#endif // NEVERC_BUILD_ANDROIDKERNELBUILDCOMMANDS_H
