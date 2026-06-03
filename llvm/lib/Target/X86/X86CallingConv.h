//=== X86CallingConv.h - X86 Custom Calling Convention Routines -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the custom routines for the X86 Calling Convention that
// aren't done by tablegen.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_X86CALLINGCONV_H
#define LLVM_LIB_TARGET_X86_X86CALLINGCONV_H

#include "MCTargetDesc/X86MCTargetDesc.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/IR/CallingConv.h"

namespace llvm {

bool RetCC_X86(unsigned ValNo, MVT ValVT, MVT LocVT,
               CCValAssign::LocInfo LocInfo, ISD::ArgFlagsTy ArgFlags,
               CCState &State);

bool CC_X86(unsigned ValNo, MVT ValVT, MVT LocVT, CCValAssign::LocInfo LocInfo,
            ISD::ArgFlagsTy ArgFlags, CCState &State);

/// Data-driven custom calling convention (CallingConv::NeverC_Custom).
///
/// These are drop-in replacements for CC_X86 / RetCC_X86: for every other
/// calling convention they simply delegate to the tablegen entry points, but
/// for NeverC_Custom they assign argument/return registers according to the
/// per-function "neverc-callconv" spec (see llvm/CodeGen/NeverCCallConv.h).
/// This is how external NeverC plugins can set arbitrary register layouts
/// without touching tablegen / X86GenCallingConv.inc.
bool CC_X86_NeverC(unsigned ValNo, MVT ValVT, MVT LocVT,
                   CCValAssign::LocInfo LocInfo, ISD::ArgFlagsTy ArgFlags,
                   CCState &State);

bool RetCC_X86_NeverC(unsigned ValNo, MVT ValVT, MVT LocVT,
                      CCValAssign::LocInfo LocInfo, ISD::ArgFlagsTy ArgFlags,
                      CCState &State);

/// Translate a NeverC spec register name (e.g. "rbx", "xmm0") to an X86
/// physical register; returns an invalid register for unknown names. Shared
/// with X86RegisterInfo for the custom callee-saved ("csr") set.
MCRegister neverCParseX86Reg(StringRef Name);

} // namespace llvm

#endif
