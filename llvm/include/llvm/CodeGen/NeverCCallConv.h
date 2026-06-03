//===- llvm/CodeGen/NeverCCallConv.h - NeverC custom calling conv -*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the data format and parser for CallingConv::NeverC_Custom,
// the data-driven custom calling convention used by NeverC.
//
// Instead of baking register-assignment rules into tablegen, the per-function
// register layout is described as a small string stored in the
// "neverc-callconv" function attribute and parsed here. Each backend turns the
// (target-agnostic) register *names* into physical registers in its own
// CCAssignFn. This keeps the format identical across targets and lets external
// tools (NeverC plugins, -mllvm passes, frontend attributes) all produce the
// same data without ever regenerating any .inc file.
//
// Format (case-insensitive, whitespace tolerant):
//
//   "gpr:rcx,rdx,r8,r9; xmm:xmm0,xmm1; ret_gpr:rax; ret_xmm:xmm0"
//
//   gpr     / arg_gpr  : ordered integer/pointer argument registers
//   xmm     / arg_xmm  : ordered float/vector argument registers
//   ret_gpr / ret      : ordered integer/pointer return registers
//   ret_xmm            : ordered float/vector return registers
//
// Any segment may be omitted. Unknown segments are ignored. Register names are
// target-specific identifiers (e.g. "rax"/"x0"); the backend validates them.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_NEVERCCALLCONV_H
#define LLVM_CODEGEN_NEVERCCALLCONV_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

namespace llvm {
namespace neverc {

/// Name of the function (and call-site) string attribute carrying the spec.
inline constexpr char CallConvAttrName[] = "neverc-callconv";

/// Segment keys recognized in a spec string (matched case-insensitively).
/// Producers (plugins, frontend, -mllvm passes) and the parser share these so
/// the format has a single source of truth.
namespace Keys {
inline constexpr char Args[] = "args";      ///< positional per-argument layout
inline constexpr char ArgGPR[] = "gpr";     ///< integer/pointer argument regs
inline constexpr char ArgGPRAlt[] = "arg_gpr";
inline constexpr char ArgXMM[] = "xmm";     ///< float/vector argument regs
inline constexpr char ArgXMMAlt[] = "arg_xmm";
inline constexpr char ArgFP[] = "fpr";      ///< float/vector args (target-neutral alias)
inline constexpr char RetGPR[] = "ret_gpr"; ///< integer/pointer return regs
inline constexpr char RetGPRAlt[] = "ret";
inline constexpr char RetXMM[] = "ret_xmm"; ///< float/vector return regs
inline constexpr char RetFP[] = "ret_fpr";  ///< float/vector returns (alias)
inline constexpr char CalleeSaved[] = "csr"; ///< callee-saved register set
} // namespace Keys

/// Token used as a value inside the "args" list to force stack passing.
inline constexpr char StackToken[] = "stack";
inline constexpr char StackTokenAlt[] = "mem";

/// True if \p Tok is the stack keyword (case-insensitive).
inline bool isStackToken(StringRef Tok) {
  return Tok.equals_insensitive(StackToken) ||
         Tok.equals_insensitive(StackTokenAlt);
}

/// Parsed register layout for CallingConv::NeverC_Custom. The StringRefs point
/// into the original spec string, so it must outlive this struct (it does: the
/// spec is owned either by the LLVMContext attribute or by the caller).
struct CustomCCSpec {
  /// Positional layout: one token per argument, in order. Each token is a
  /// register name or the stack keyword ("stack"/"mem"). When non-empty this
  /// takes precedence over the ArgGPR/ArgXMM pools and is the only way to force
  /// a specific argument onto the stack regardless of free registers.
  SmallVector<StringRef, 8> Args;

  /// Pool mode: argument registers consumed in order; values that don't fit
  /// spill to the stack. Integer/pointer args draw from ArgGPR, fp/vector from
  /// ArgXMM.
  SmallVector<StringRef, 8> ArgGPR;
  SmallVector<StringRef, 8> ArgXMM;
  SmallVector<StringRef, 8> RetGPR;
  SmallVector<StringRef, 8> RetXMM;

  /// Optional callee-saved register set (overrides the target's standard CSR
  /// for this convention). Empty = use the standard callee-saved set. Applies
  /// to both the callee's prologue/epilogue and the caller's preserved mask.
  SmallVector<StringRef, 8> CalleeSaved;

  bool hasPositionalArgs() const { return !Args.empty(); }
  bool hasArgRegs() const { return !ArgGPR.empty() || !ArgXMM.empty(); }
  bool hasAnyArgs() const { return hasPositionalArgs() || hasArgRegs(); }
  bool hasRetRegs() const { return !RetGPR.empty() || !RetXMM.empty(); }
  bool empty() const { return !hasAnyArgs() && !hasRetRegs(); }
};

/// Parse \p Spec into \p Out. Tolerant: malformed/unknown pieces are skipped
/// rather than reported, so a partially valid spec still does something useful.
inline void parseCustomCCSpec(StringRef Spec, CustomCCSpec &Out) {
  SmallVector<StringRef, 8> Segments;
  Spec.split(Segments, ';', /*MaxSplit=*/-1, /*KeepEmpty=*/false);
  for (StringRef Seg : Segments) {
    Seg = Seg.trim();
    if (Seg.empty())
      continue;

    StringRef Key, List;
    std::tie(Key, List) = Seg.split(':');
    Key = Key.trim();

    SmallVectorImpl<StringRef> *Dst = nullptr;
    if (Key.equals_insensitive(Keys::Args))
      Dst = &Out.Args;
    else if (Key.equals_insensitive(Keys::ArgGPR) ||
             Key.equals_insensitive(Keys::ArgGPRAlt))
      Dst = &Out.ArgGPR;
    else if (Key.equals_insensitive(Keys::ArgXMM) ||
             Key.equals_insensitive(Keys::ArgXMMAlt) ||
             Key.equals_insensitive(Keys::ArgFP))
      Dst = &Out.ArgXMM;
    else if (Key.equals_insensitive(Keys::RetGPR) ||
             Key.equals_insensitive(Keys::RetGPRAlt))
      Dst = &Out.RetGPR;
    else if (Key.equals_insensitive(Keys::RetXMM) ||
             Key.equals_insensitive(Keys::RetFP))
      Dst = &Out.RetXMM;
    else if (Key.equals_insensitive(Keys::CalleeSaved))
      Dst = &Out.CalleeSaved;
    else
      continue; // Unknown segment: ignore.

    SmallVector<StringRef, 8> Items;
    List.split(Items, ',', /*MaxSplit=*/-1, /*KeepEmpty=*/false);
    for (StringRef It : Items) {
      It = It.trim();
      if (!It.empty())
        Dst->push_back(It);
    }
  }
}

} // namespace neverc
} // namespace llvm

#endif // LLVM_CODEGEN_NEVERCCALLCONV_H
