//===- AndroidKernelBuildState.h - Published build identity -----*- C++ -*-===//
//
// Part of the NeverC Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef NEVERC_FOUNDATION_ANDROIDKERNELBUILDSTATE_H
#define NEVERC_FOUNDATION_ANDROIDKERNELBUILDSTATE_H

#include <optional>
#include <string>

namespace neverc {

/// Optional build identity published atomically beside an Android kernel image.
struct AndroidKernelBuildState {
  std::optional<std::string> BuildID;
  std::optional<std::string> BuildExtra;
};

/// Captures the build-state environment exported by Android example builds.
AndroidKernelBuildState androidKernelBuildStateFromEnvironment();

} // namespace neverc

#endif // NEVERC_FOUNDATION_ANDROIDKERNELBUILDSTATE_H
