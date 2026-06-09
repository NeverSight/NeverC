//===-- llvm/IR/GEPNoWrapFlags.h - GEP no-wrap flags ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Backport of LLVM 20 GEPNoWrapFlags for nuw GEP alias analysis.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_GEPNOWRAPFLAGS_H
#define LLVM_IR_GEPNOWRAPFLAGS_H

#include <cassert>

namespace llvm {

/// Represents flags for the getelementptr instruction/expression.
///  * inbounds (implies nusw)
///  * nusw (no unsigned signed wrap)
///  * nuw (no unsigned wrap)
class GEPNoWrapFlags {
  enum : unsigned {
    InBoundsFlag = (1 << 0),
    NUSWFlag = (1 << 1),
    NUWFlag = (1 << 2),
  };

  unsigned Flags;
  GEPNoWrapFlags(unsigned Flags) : Flags(Flags) {
    assert((!isInBounds() || hasNoUnsignedSignedWrap()) &&
           "inbounds implies nusw");
  }

public:
  GEPNoWrapFlags() : Flags(0) {}

  // For backward compatibility: plain boolean as InBounds.
  GEPNoWrapFlags(bool IsInBounds)
      : Flags(IsInBounds ? (InBoundsFlag | NUSWFlag) : 0) {}

  static GEPNoWrapFlags none() { return GEPNoWrapFlags(); }

  static GEPNoWrapFlags all() {
    return GEPNoWrapFlags(InBoundsFlag | NUSWFlag | NUWFlag);
  }

  static GEPNoWrapFlags inBounds() {
    return GEPNoWrapFlags(InBoundsFlag | NUSWFlag);
  }

  static GEPNoWrapFlags noUnsignedSignedWrap() {
    return GEPNoWrapFlags(NUSWFlag);
  }

  static GEPNoWrapFlags noUnsignedWrap() { return GEPNoWrapFlags(NUWFlag); }

  static GEPNoWrapFlags fromRaw(unsigned Flags) {
    return GEPNoWrapFlags(Flags);
  }
  unsigned getRaw() const { return Flags; }

  bool isInBounds() const { return Flags & InBoundsFlag; }
  bool hasNoUnsignedSignedWrap() const { return Flags & NUSWFlag; }
  bool hasNoUnsignedWrap() const { return Flags & NUWFlag; }

  GEPNoWrapFlags withoutInBounds() const {
    return GEPNoWrapFlags(Flags & ~InBoundsFlag);
  }
  GEPNoWrapFlags withoutNoUnsignedSignedWrap() const {
    return GEPNoWrapFlags(Flags & ~(InBoundsFlag | NUSWFlag));
  }
  GEPNoWrapFlags withoutNoUnsignedWrap() const {
    return GEPNoWrapFlags(Flags & ~NUWFlag);
  }

  GEPNoWrapFlags operator&(GEPNoWrapFlags Other) const {
    return GEPNoWrapFlags(Flags & Other.Flags);
  }
  GEPNoWrapFlags &operator&=(GEPNoWrapFlags Other) {
    Flags &= Other.Flags;
    return *this;
  }
  GEPNoWrapFlags operator|(GEPNoWrapFlags Other) const {
    return GEPNoWrapFlags(Flags | Other.Flags);
  }
  GEPNoWrapFlags &operator|=(GEPNoWrapFlags Other) {
    Flags |= Other.Flags;
    return *this;
  }

  bool operator==(GEPNoWrapFlags Other) const { return Flags == Other.Flags; }
  bool operator!=(GEPNoWrapFlags Other) const { return Flags != Other.Flags; }
};

} // end namespace llvm

#endif // LLVM_IR_GEPNOWRAPFLAGS_H
