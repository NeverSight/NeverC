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

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include <cstdint>
#include <limits>

namespace llvm {
namespace neverc {

/// Name of the function (and call-site) string attribute carrying the spec.
inline constexpr char CallConvAttrName[] = "neverc-callconv";

/// Name of the immutable, host-validated per-function calling-convention plan.
inline constexpr char CallConvPlanAttrName[] = "neverc-cc-plan-v1";

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

enum class CCPlanLocationKind : uint8_t {
  Register,
  Stack,
};

struct CCPlanLocation {
  CCPlanLocationKind Kind = CCPlanLocationKind::Register;
  uint32_t ValueIndex = 0;
  uint32_t PieceOffset = 0;
  uint32_t Size = 0;
  uint32_t Alignment = 0;
  uint32_t RegisterNumber = 0;
  uint32_t StackOffset = 0;
  uint64_t Flags = 0;
};

struct CustomCCPlan {
  StringRef SchemaDigest;
  uint64_t TargetIDHigh = 0;
  uint64_t TargetIDLow = 0;
  uint64_t CallingConventionIDHigh = 0;
  uint64_t CallingConventionIDLow = 0;
  SmallVector<CCPlanLocation, 8> Returns;
  SmallVector<CCPlanLocation, 8> Arguments;
  SmallVector<uint32_t, 16> CalleeSaved;
  uint32_t StackAlignment = 0;

  const CCPlanLocation *findReturn(unsigned ValueIndex) const {
    for (const CCPlanLocation &Location : Returns)
      if (Location.ValueIndex == ValueIndex)
        return &Location;
    return nullptr;
  }

  const CCPlanLocation *findArgument(unsigned ValueIndex) const {
    for (const CCPlanLocation &Location : Arguments)
      if (Location.ValueIndex == ValueIndex)
        return &Location;
    return nullptr;
  }
};

inline bool parseCCPlanUnsigned(StringRef Text, uint64_t &Value) {
  Text = Text.trim();
  return !Text.empty() && !Text.getAsInteger(10, Value);
}

inline bool parseCCPlanID(StringRef Text, uint64_t &High,
                          uint64_t &Low) {
  auto [HighText, LowText] = Text.split(':');
  return !LowText.empty() &&
         LowText.find(':') == StringRef::npos &&
         parseCCPlanUnsigned(HighText, High) &&
         parseCCPlanUnsigned(LowText, Low);
}

inline bool parseCCPlanLocations(
    StringRef Text, SmallVectorImpl<CCPlanLocation> &Out) {
  if (Text.empty())
    return true;
  SmallVector<StringRef, 8> Records;
  Text.split(Records, '|', /*MaxSplit=*/-1, /*KeepEmpty=*/true);
  for (StringRef Record : Records) {
    SmallVector<StringRef, 8> Fields;
    Record.split(Fields, ',', /*MaxSplit=*/-1, /*KeepEmpty=*/true);
    if (Fields.size() != 8)
      return false;
    CCPlanLocation Location;
    if (Fields[0] == "r")
      Location.Kind = CCPlanLocationKind::Register;
    else if (Fields[0] == "s")
      Location.Kind = CCPlanLocationKind::Stack;
    else
      return false;

    uint64_t Values[7] = {};
    for (unsigned I = 0; I != 7; ++I)
      if (!parseCCPlanUnsigned(Fields[I + 1], Values[I]))
        return false;
    for (unsigned I = 0; I != 6; ++I)
      if (Values[I] > std::numeric_limits<uint32_t>::max())
        return false;
    Location.ValueIndex = static_cast<uint32_t>(Values[0]);
    Location.PieceOffset = static_cast<uint32_t>(Values[1]);
    Location.Size = static_cast<uint32_t>(Values[2]);
    Location.Alignment = static_cast<uint32_t>(Values[3]);
    Location.RegisterNumber = static_cast<uint32_t>(Values[4]);
    Location.StackOffset = static_cast<uint32_t>(Values[5]);
    Location.Flags = Values[6];
    if (Location.Size == 0 || Location.Alignment == 0 ||
        (Location.Alignment & (Location.Alignment - 1)) != 0 ||
        (Location.Flags & ~UINT64_C(3)) != 0 ||
        ((Location.Flags & UINT64_C(2)) != 0 &&
         (Location.Flags & UINT64_C(1)) == 0))
      return false;
    if (Location.Kind == CCPlanLocationKind::Register &&
        (Location.RegisterNumber == 0 || Location.StackOffset != 0))
      return false;
    if (Location.Kind == CCPlanLocationKind::Stack &&
        Location.RegisterNumber != 0)
      return false;
    Out.push_back(Location);
  }
  return true;
}

/// Parse the immutable plan emitted by NeverC's pre-codegen materializer.
/// This parser is deliberately strict: malformed or unknown fields are not
/// interpreted as a partial calling convention.
inline bool parseCustomCCPlan(StringRef Text, CustomCCPlan &Out) {
  Out = CustomCCPlan();
  SmallVector<StringRef, 12> Segments;
  Text.split(Segments, ';', /*MaxSplit=*/-1, /*KeepEmpty=*/true);
  if (Segments.empty() || Segments.front() != "neverc-cc-plan-v1")
    return false;

  bool SawSchema = false;
  bool SawTarget = false;
  bool SawCC = false;
  bool SawStack = false;
  bool SawReturns = false;
  bool SawArguments = false;
  bool SawCalleeSaved = false;
  for (StringRef Segment : ArrayRef(Segments).drop_front()) {
    StringRef Key;
    StringRef Value;
    std::tie(Key, Value) = Segment.split('=');
    if (Key.empty() || Segment.find('=') == StringRef::npos)
      return false;
    if (Key == "schema") {
      if (SawSchema || Value.empty())
        return false;
      Out.SchemaDigest = Value;
      SawSchema = true;
    } else if (Key == "target") {
      if (SawTarget ||
          !parseCCPlanID(Value, Out.TargetIDHigh, Out.TargetIDLow))
        return false;
      SawTarget = true;
    } else if (Key == "cc") {
      if (SawCC ||
          !parseCCPlanID(Value, Out.CallingConventionIDHigh,
                         Out.CallingConventionIDLow))
        return false;
      SawCC = true;
    } else if (Key == "stack") {
      uint64_t StackAlignment = 0;
      if (SawStack ||
          !parseCCPlanUnsigned(Value, StackAlignment) ||
          StackAlignment == 0 ||
          (StackAlignment & (StackAlignment - 1)) != 0 ||
          StackAlignment >
              std::numeric_limits<uint32_t>::max())
        return false;
      Out.StackAlignment = static_cast<uint32_t>(StackAlignment);
      SawStack = true;
    } else if (Key == "returns") {
      if (SawReturns || !parseCCPlanLocations(Value, Out.Returns))
        return false;
      SawReturns = true;
    } else if (Key == "arguments") {
      if (SawArguments ||
          !parseCCPlanLocations(Value, Out.Arguments))
        return false;
      SawArguments = true;
    } else if (Key == "callee-saved") {
      if (SawCalleeSaved)
        return false;
      SawCalleeSaved = true;
      if (Value.empty())
        continue;
      SmallVector<StringRef, 16> Registers;
      Value.split(Registers, ',', /*MaxSplit=*/-1,
                  /*KeepEmpty=*/true);
      for (StringRef RegisterText : Registers) {
        uint64_t Register = 0;
        if (!parseCCPlanUnsigned(RegisterText, Register) ||
            Register == 0 ||
            Register > std::numeric_limits<uint32_t>::max())
          return false;
        Out.CalleeSaved.push_back(static_cast<uint32_t>(Register));
      }
    } else {
      return false;
    }
  }
  return SawSchema && SawTarget && SawCC && SawStack && SawReturns &&
         SawArguments && SawCalleeSaved;
}

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
