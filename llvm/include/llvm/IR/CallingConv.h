//===- llvm/CallingConv.h - LLVM Calling Conventions ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines LLVM's set of calling conventions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_CALLINGCONV_H
#define LLVM_IR_CALLINGCONV_H

namespace llvm {

/// CallingConv Namespace - This namespace contains an enum with a value for
/// the well-known calling conventions.
///
namespace CallingConv {

/// LLVM IR allows to use arbitrary numbers as calling convention identifiers.
using ID = unsigned;

/// A set of enums which specify the assigned numeric values for known llvm
/// calling conventions.
/// LLVM Calling Convention Representation
enum {
  /// The default llvm calling convention, compatible with C. This convention
  /// is the only one that supports varargs calls. As with typical C calling
  /// conventions, the callee/caller have to tolerate certain amounts of
  /// prototype mismatch.
  C = 0,

  // Generic LLVM calling conventions. None of these support varargs calls,
  // and all assume that the caller and callee prototype exactly match.

  /// Attempts to make calls as fast as possible (e.g. by passing things in
  /// registers).
  Fast = 8,

  /// Attempts to make code in the caller as efficient as possible under the
  /// assumption that the call is not commonly executed. As such, these calls
  /// often preserve all registers so that the call does not break any live
  /// ranges in the caller side.
  Cold = 9,

  /// Used for dynamic register based calls (e.g. stackmap and patchpoint
  /// intrinsics).
  AnyReg = 13,

  /// Used for runtime calls that preserves most registers.
  PreserveMost = 14,

  /// Used for runtime calls that preserves (almost) all registers.
  PreserveAll = 15,

  /// Attemps to make calls as fast as possible while guaranteeing that tail
  /// call optimization can always be performed.
  Tail = 18,

  /// Special calling convention on Windows for calling the Control Guard
  /// Check ICall funtion. The function takes exactly one argument (address of
  /// the target function) passed in the first argument register, and has no
  /// return value. All register values are preserved.
  CFGuard_Check = 19,

  /// This is the start of the target-specific calling conventions, e.g.
  /// fastcall and thiscall on X86.
  FirstTargetCC = 64,

  /// stdcall is mostly used by the Win32 API. It is basically the same as the
  /// C convention with the difference in that the callee is responsible for
  /// popping the arguments from the stack.
  X86_StdCall = 64,

  /// 'fast' analog of X86_StdCall. Passes first two arguments in ECX:EDX
  /// registers, others - via stack. Callee is responsible for stack cleaning.
  X86_FastCall = 65,

  /// Similar to X86_StdCall. Passes first argument in ECX, others via stack.
  /// Callee is responsible for stack cleaning. MSVC uses this by default for
  /// methods in its ABI.
  X86_ThisCall = 70,

  /// The C convention as specified in the x86-64 supplement to the System V
  /// ABI, used on most non-Windows systems.
  X86_64_SysV = 78,

  /// The C convention as implemented on Windows/x86-64 and AArch64. It
  /// differs from the more common \c X86_64_SysV convention in a number of
  /// ways, most notably in that XMM registers used to pass arguments are
  /// shadowed by GPRs, and vice versa. On AArch64, this is identical to the
  /// normal C (AAPCS) calling convention for normal functions, but floats are
  /// passed in integer registers to variadic functions.
  Win64 = 79,

  /// MSVC calling convention that passes vectors and vector aggregates in SSE
  /// registers.
  X86_VectorCall = 80,

  /// x86 hardware interrupt context. Callee may take one or two parameters,
  /// where the 1st represents a pointer to hardware context frame and the 2nd
  /// represents hardware error code, the presence of the later depends on the
  /// interrupt vector taken. Valid for both 32- and 64-bit subtargets.
  X86_INTR = 83,

  /// Register calling convention used for parameters transfer optimization
  X86_RegCall = 92,

  /// Used between AArch64 Advanced SIMD functions
  AArch64_VectorCall = 97,

  /// Used between AArch64 SVE functions
  AArch64_SVE_VectorCall = 98,

  /// Preserve X0-X13, X19-X29, SP, Z0-Z31, P0-P15.
  AArch64_SME_ABI_Support_Routines_PreserveMost_From_X0 = 102,

  /// Preserve X2-X15, X19-X29, SP, Z0-Z31, P0-P15.
  AArch64_SME_ABI_Support_Routines_PreserveMost_From_X2 = 103,

  /// The highest possible ID. Must be some 2^k - 1.
  MaxID = 1023
};

} // end namespace CallingConv

} // end namespace llvm

#endif // LLVM_IR_CALLINGCONV_H
