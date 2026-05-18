//===- MCMachObjectTargetWriter.cpp - Mach-O Target Writer Subclass -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCMachObjectWriter.h"

using namespace llvm;

MCMachObjectTargetWriter::MCMachObjectTargetWriter(uint32_t CPUType_,
                                                   uint32_t CPUSubtype_)
    : CPUType(CPUType_), CPUSubtype(CPUSubtype_) {}

MCMachObjectTargetWriter::~MCMachObjectTargetWriter() = default;
