//===-- AArch64TargetInfo.cpp - AArch64 Target Implementation
//-----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TargetInfo/AArch64TargetInfo.h"
#include "llvm/MC/TargetRegistry.h"

using namespace llvm;
Target &llvm::getTheAArch64leTarget() {
  static Target TheAArch64leTarget;
  return TheAArch64leTarget;
}
Target &llvm::getTheARM64Target() {
  static Target TheARM64Target;
  return TheARM64Target;
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeAArch64TargetInfo() {
  TargetRegistry::RegisterTarget(
      getTheARM64Target(), "arm64", "ARM64 (little endian)", "AArch64",
      [](Triple::ArchType) { return false; }, true);

  RegisterTarget<Triple::aarch64, /*HasJIT=*/true> Z(
      getTheAArch64leTarget(), "aarch64", "AArch64 (little endian)", "AArch64");
}
