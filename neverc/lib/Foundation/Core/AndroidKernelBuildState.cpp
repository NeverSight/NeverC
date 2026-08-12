//===- AndroidKernelBuildState.cpp - Published build identity -------------===//
//
// Part of the NeverC Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "neverc/Foundation/AndroidKernelBuildState.h"

#include "llvm/Support/Process.h"

namespace neverc {

AndroidKernelBuildState androidKernelBuildStateFromEnvironment() {
  AndroidKernelBuildState State;
  if (auto BuildID =
          llvm::sys::Process::GetEnv("NEVERC_ANDROID_KERNEL_BUILD_ID"))
    State.BuildID = BuildID->str().str();
  if (auto BuildExtra =
          llvm::sys::Process::GetEnv("NEVERC_ANDROID_KERNEL_BUILD_EXTRA"))
    State.BuildExtra = BuildExtra->str().str();
  return State;
}

} // namespace neverc
