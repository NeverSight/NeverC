//===- WindowsMachineFlag.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Functions for implementing the /machine: flag.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_WINDOWSMACHINEFLAG_H
#define LLVM_OBJECT_WINDOWSMACHINEFLAG_H

namespace llvm {

class StringRef;
namespace COFF {
enum MachineTypes : unsigned;
}

// Returns a user-readable string for ARM64, AMD64.
StringRef machineToStr(COFF::MachineTypes MT);

// Maps /machine: arguments to a MachineTypes value.
// Only returns ARM64, AMD64, or IMAGE_FILE_MACHINE_UNKNOWN.
COFF::MachineTypes getMachineType(StringRef S);

} // namespace llvm

#endif
