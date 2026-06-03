//=== AArch64CallingConvention.h - AArch64 CC entry points ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the entry points for AArch64 calling convention analysis.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AARCH64_AARCH64CALLINGCONVENTION_H
#define LLVM_LIB_TARGET_AARCH64_AARCH64CALLINGCONVENTION_H

#include "llvm/CodeGen/CallingConvLower.h"

namespace llvm {
bool CC_AArch64_AAPCS(unsigned ValNo, MVT ValVT, MVT LocVT,
                      CCValAssign::LocInfo LocInfo, ISD::ArgFlagsTy ArgFlags,
                      CCState &State);
bool CC_AArch64_DarwinPCS_VarArg(unsigned ValNo, MVT ValVT, MVT LocVT,
                                 CCValAssign::LocInfo LocInfo,
                                 ISD::ArgFlagsTy ArgFlags, CCState &State);
bool CC_AArch64_DarwinPCS(unsigned ValNo, MVT ValVT, MVT LocVT,
                          CCValAssign::LocInfo LocInfo,
                          ISD::ArgFlagsTy ArgFlags, CCState &State);
bool CC_AArch64_Win64PCS(unsigned ValNo, MVT ValVT, MVT LocVT,
                         CCValAssign::LocInfo LocInfo, ISD::ArgFlagsTy ArgFlags,
                         CCState &State);
bool CC_AArch64_Win64_VarArg(unsigned ValNo, MVT ValVT, MVT LocVT,
                             CCValAssign::LocInfo LocInfo,
                             ISD::ArgFlagsTy ArgFlags, CCState &State);
bool CC_AArch64_Win64_CFGuard_Check(unsigned ValNo, MVT ValVT, MVT LocVT,
                                    CCValAssign::LocInfo LocInfo,
                                    ISD::ArgFlagsTy ArgFlags, CCState &State);
bool RetCC_AArch64_AAPCS(unsigned ValNo, MVT ValVT, MVT LocVT,
                         CCValAssign::LocInfo LocInfo, ISD::ArgFlagsTy ArgFlags,
                         CCState &State);

/// Data-driven custom calling convention (CallingConv::NeverC_Custom). Assigns
/// argument/return registers from the per-function "neverc-callconv" spec (see
/// llvm/CodeGen/NeverCCallConv.h) instead of any tablegen rule.
bool CC_AArch64_NeverC(unsigned ValNo, MVT ValVT, MVT LocVT,
                       CCValAssign::LocInfo LocInfo, ISD::ArgFlagsTy ArgFlags,
                       CCState &State);
bool RetCC_AArch64_NeverC(unsigned ValNo, MVT ValVT, MVT LocVT,
                          CCValAssign::LocInfo LocInfo, ISD::ArgFlagsTy ArgFlags,
                          CCState &State);

/// Translate a NeverC spec register name to the AArch64 physical register used
/// for the custom callee-saved ("csr") set: GPR names (x0..x28) map to the X
/// register, vector names (v0..v31) map to the full 128-bit Q register so the
/// callee's save/restore and the caller's preserved mask agree at every width.
/// Returns an invalid register for unknown names. Shared with AArch64RegisterInfo
/// and AArch64ISelLowering.
MCRegister neverCParseA64Reg(StringRef Name);
} // namespace llvm

#endif
