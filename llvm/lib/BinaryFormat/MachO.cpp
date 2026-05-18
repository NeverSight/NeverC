//===-- llvm/BinaryFormat/MachO.cpp - The MachO file format -----*- C++/-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/BinaryFormat/MachO.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

static Error unsupported(const char *Str, const Triple &T) {
  return createStringError(std::errc::invalid_argument,
                           "Unsupported triple for mach-o cpu %s: %s", Str,
                           T.str().c_str());
}

Expected<uint32_t> MachO::getCPUType(const Triple &T) {
  if (!T.isOSBinFormatMachO())
    return unsupported("type", T);
  if (T.isX86())
    return MachO::CPU_TYPE_X86_64;
  if (T.isAArch64())
    return MachO::CPU_TYPE_ARM64;
  return unsupported("type", T);
}

Expected<uint32_t> MachO::getCPUSubType(const Triple &T) {
  if (!T.isOSBinFormatMachO())
    return unsupported("subtype", T);
  if (T.isX86())
    return MachO::CPU_SUBTYPE_X86_64_ALL;
  if (T.isAArch64())
    return MachO::CPU_SUBTYPE_ARM64_ALL;
  return unsupported("subtype", T);
}
