//===- WindowsMachineFlag.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Object/WindowsMachineFlag.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

COFF::MachineTypes llvm::getMachineType(StringRef S) {
  return StringSwitch<COFF::MachineTypes>(S.lower())
      .Cases("x64", "amd64", COFF::IMAGE_FILE_MACHINE_AMD64)
      .Case("arm64", COFF::IMAGE_FILE_MACHINE_ARM64)
      .Default(COFF::IMAGE_FILE_MACHINE_UNKNOWN);
}

StringRef llvm::machineToStr(COFF::MachineTypes MT) {
  switch (MT) {
  case COFF::IMAGE_FILE_MACHINE_ARM64:
    return "arm64";
  case COFF::IMAGE_FILE_MACHINE_AMD64:
    return "x64";
  default:
    llvm_unreachable("unknown machine type");
  }
}
