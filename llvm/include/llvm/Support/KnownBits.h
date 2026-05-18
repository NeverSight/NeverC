//===- llvm/Support/KnownBits.h - Stores known zeros/ones -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a class for representing known zeros and ones used by
// computeKnownBits.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_KNOWNBITS_H
#define LLVM_SUPPORT_KNOWNBITS_H

#include "llvm/ADT/APInt.h"
#include "llvm/Support/Debug.h"
#include <optional>

namespace llvm {

// Struct for tracking the known zeros and ones of a value.
struct KnownBits {
  APInt Zero;
  APInt One;

private:
  // Internal constructor for creating a KnownBits from two APInts.
  KnownBits(APInt Zero, APInt One)
      : Zero(std::move(Zero)), One(std::move(One)) {}

public:
  // Default construct Zero and One.
  KnownBits() = default;

  /// Create a known bits object of BitWidth bits initialized to unknown.
  KnownBits(unsigned BitWidth) : Zero(BitWidth, 0), One(BitWidth, 0) {}

  /// Get the bit width of this value.
  unsigned getBitWidth() const {
    assert(Zero.getBitWidth() == One.getBitWidth() &&
           "Zero and One should have the same width!");
    return Zero.getBitWidth();
  }

  /// Returns true if there is conflicting information.
  bool hasConflict() const { return Zero.intersects(One); }

  /// Returns true if we know the value of all bits.
  bool isConstant() const {
    assert(!hasConflict() && "KnownBits conflict!");
    return Zero.popcount() + One.popcount() == getBitWidth();
  }

  /// Returns the value when all bits have a known value. This just returns One
  /// with a protective assertion.
  const APInt &getConstant() const {
    assert(isConstant() && "Can only get value when all bits are known");
    return One;
  }

  /// Returns true if we don't know any bits.
  bool isUnknown() const { return Zero.isZero() && One.isZero(); }

  /// Resets the known state of all bits.
  void resetAll() {
    Zero.clearAllBits();
    One.clearAllBits();
  }

  /// Returns true if value is all zero.
  bool isZero() const {
    assert(!hasConflict() && "KnownBits conflict!");
    return Zero.isAllOnes();
  }

  /// Returns true if value is all one bits.
  bool isAllOnes() const {
    assert(!hasConflict() && "KnownBits conflict!");
    return One.isAllOnes();
  }

  /// Make all bits known to be zero and discard any previous information.
  void setAllZero() {
    Zero.setAllBits();
    One.clearAllBits();
  }

  /// Make all bits known to be one and discard any previous information.
  void setAllOnes() {
    Zero.clearAllBits();
    One.setAllBits();
  }

  /// Returns true if this value is known to be negative.
  bool isNegative() const { return One.isSignBitSet(); }

  /// Returns true if this value is known to be non-negative.
  bool isNonNegative() const { return Zero.isSignBitSet(); }

  /// Returns true if this value is known to be non-zero.
  bool isNonZero() const { return !One.isZero(); }

  /// Returns true if this value is known to be positive.
  bool isStrictlyPositive() const {
    return Zero.isSignBitSet() && !One.isZero();
  }

  /// Make this value negative.
  void makeNegative() { One.setSignBit(); }

  /// Make this value non-negative.
  void makeNonNegative() { Zero.setSignBit(); }

  /// Return the minimal unsigned value possible given these KnownBits.
  APInt getMinValue() const {
    // Assume that all bits that aren't known-ones are zeros.
    return One;
  }

  /// Return the minimal signed value possible given these KnownBits.
  APInt getSignedMinValue() const {
    // Assume that all bits that aren't known-ones are zeros.
    APInt Min = One;
    // Sign bit is unknown.
    if (Zero.isSignBitClear())
      Min.setSignBit();
    return Min;
  }

  /// Return the maximal unsigned value possible given these KnownBits.
  APInt getMaxValue() const {
    // Assume that all bits that aren't known-zeros are ones.
    return ~Zero;
  }

  /// Return the maximal signed value possible given these KnownBits.
  APInt getSignedMaxValue() const {
    // Assume that all bits that aren't known-zeros are ones.
    APInt Max = ~Zero;
    // Sign bit is unknown.
    if (One.isSignBitClear())
      Max.clearSignBit();
    return Max;
  }

  /// Return known bits for a truncation of the value we're tracking.
  KnownBits trunc(unsigned BitWidth) const {
    return KnownBits(Zero.trunc(BitWidth), One.trunc(BitWidth));
  }

  /// Return known bits for an "any" extension of the value we're tracking,
  /// where we don't know anything about the extended bits.
  KnownBits anyext(unsigned BitWidth) const {
    return KnownBits(Zero.zext(BitWidth), One.zext(BitWidth));
  }

  /// Return known bits for a zero extension of the value we're tracking.
  KnownBits zext(unsigned BitWidth) const {
    unsigned OldBitWidth = getBitWidth();
    APInt NewZero = Zero.zext(BitWidth);
    NewZero.setBitsFrom(OldBitWidth);
    return KnownBits(NewZero, One.zext(BitWidth));
  }

  /// Return known bits for a sign extension of the value we're tracking.
  KnownBits sext(unsigned BitWidth) const {
    return KnownBits(Zero.sext(BitWidth), One.sext(BitWidth));
  }

  /// Return known bits for an "any" extension or truncation of the value we're
  /// tracking.
  KnownBits anyextOrTrunc(unsigned BitWidth) const {
    if (BitWidth > getBitWidth())
      return anyext(BitWidth);
    if (BitWidth < getBitWidth())
      return trunc(BitWidth);
    return *this;
  }

  /// Return known bits for a zero extension or truncation of the value we're
  /// tracking.
  KnownBits zextOrTrunc(unsigned BitWidth) const {
    if (BitWidth > getBitWidth())
      return zext(BitWidth);
    if (BitWidth < getBitWidth())
      return trunc(BitWidth);
    return *this;
  }

  /// Return known bits for a sign extension or truncation of the value we're
  /// tracking.
  KnownBits sextOrTrunc(unsigned BitWidth) const {
    if (BitWidth > getBitWidth())
      return sext(BitWidth);
    if (BitWidth < getBitWidth())
      return trunc(BitWidth);
    return *this;
  }

  /// Return known bits for a in-register sign extension of the value we're
  /// tracking.
  KnownBits sextInReg(unsigned SrcBitWidth) const;

  /// Insert the bits from a smaller known bits starting at bitPosition.
  void insertBits(const KnownBits &SubBits, unsigned BitPosition) {
    Zero.insertBits(SubBits.Zero, BitPosition);
    One.insertBits(SubBits.One, BitPosition);
  }

  /// Return a subset of the known bits from [bitPosition,bitPosition+numBits).
  KnownBits extractBits(unsigned NumBits, unsigned BitPosition) const {
    return KnownBits(Zero.extractBits(NumBits, BitPosition),
                     One.extractBits(NumBits, BitPosition));
  }

  /// Concatenate the bits from \p Lo onto the bottom of *this.  This is
  /// equivalent to:
  ///   (this->zext(NewWidth) << Lo.getBitWidth()) | Lo.zext(NewWidth)
  KnownBits concat(const KnownBits &Lo) const {
    return KnownBits(Zero.concat(Lo.Zero), One.concat(Lo.One));
  }

  /// Return KnownBits based on this, but updated given that the underlying
  /// value is known to be greater than or equal to Val.
  KnownBits makeGE(const APInt &Val) const;

  /// Returns the minimum number of trailing zero bits.
  unsigned countMinTrailingZeros() const { return Zero.countr_one(); }

  /// Returns the minimum number of trailing one bits.
  unsigned countMinTrailingOnes() const { return One.countr_one(); }

  /// Returns the minimum number of leading zero bits.
  unsigned countMinLeadingZeros() const { return Zero.countl_one(); }

  /// Returns the minimum number of leading one bits.
  unsigned countMinLeadingOnes() const { return One.countl_one(); }

  /// Returns the number of times the sign bit is replicated into the other
  /// bits.
  unsigned countMinSignBits() const {
    if (isNonNegative())
      return countMinLeadingZeros();
    if (isNegative())
      return countMinLeadingOnes();
    // Every value has at least 1 sign bit.
    return 1;
  }

  /// Returns the maximum number of bits needed to represent all possible
  /// signed values with these known bits. This is the inverse of the minimum
  /// number of known sign bits. Examples for bitwidth 5:
  /// 110?? --> 4
  /// 0000? --> 2
  unsigned countMaxSignificantBits() const {
    return getBitWidth() - countMinSignBits() + 1;
  }

  /// Returns the maximum number of trailing zero bits possible.
  unsigned countMaxTrailingZeros() const { return One.countr_zero(); }

  /// Returns the maximum number of trailing one bits possible.
  unsigned countMaxTrailingOnes() const { return Zero.countr_zero(); }

  /// Returns the maximum number of leading zero bits possible.
  unsigned countMaxLeadingZeros() const { return One.countl_zero(); }

  /// Returns the maximum number of leading one bits possible.
  unsigned countMaxLeadingOnes() const { return Zero.countl_zero(); }

  /// Returns the number of bits known to be one.
  unsigned countMinPopulation() const { return One.popcount(); }

  /// Returns the maximum number of bits that could be one.
  unsigned countMaxPopulation() const {
    return getBitWidth() - Zero.popcount();
  }

  /// Returns the maximum number of bits needed to represent all possible
  /// unsigned values with these known bits. This is the inverse of the
  /// minimum number of leading zeros.
  unsigned countMaxActiveBits() const {
    return getBitWidth() - countMinLeadingZeros();
  }

  /// Create known bits from a known constant.
  static KnownBits makeConstant(const APInt &C) { return KnownBits(~C, C); }

  /// Returns KnownBits information that is known to be true for both this and
  /// RHS.
  ///
  /// When an operation is known to return one of its operands, this can be used
  /// to combine information about the known bits of the operands to get the
  /// information that must be true about the result.
  KnownBits intersectWith(const KnownBits &RHS) const {
    return KnownBits(Zero & RHS.Zero, One & RHS.One);
  }

  /// Returns KnownBits information that is known to be true for either this or
  /// RHS or both.
  ///
  /// This can be used to combine different sources of information about the
  /// known bits of a single value, e.g. information about the low bits and the
  /// high bits of the result of a multiplication.
  KnownBits unionWith(const KnownBits &RHS) const {
    return KnownBits(Zero | RHS.Zero, One | RHS.One);
  }

  /// Compute known bits common to LHS and RHS.
  LLVM_DEPRECATED("use intersectWith instead", "intersectWith")
  static KnownBits commonBits(const KnownBits &LHS, const KnownBits &RHS) {
    return LHS.intersectWith(RHS);
  }

  /// Return true if LHS and RHS have no common bits set.
  static bool haveNoCommonBitsSet(const KnownBits &LHS, const KnownBits &RHS) {
    return (LHS.Zero | RHS.Zero).isAllOnes();
  }

  /// Compute known bits resulting from adding LHS, RHS and a 1-bit Carry.
  static KnownBits computeForAddCarry(const KnownBits &LHS,
                                      const KnownBits &RHS,
                                      const KnownBits &Carry);

  /// Compute known bits resulting from adding LHS and RHS.
  static KnownBits computeForAddSub(bool Add, bool NSW, const KnownBits &LHS,
                                    KnownBits RHS);

  /// Compute known bits results from subtracting RHS from LHS with 1-bit
  /// Borrow.
  static KnownBits computeForSubBorrow(const KnownBits &LHS, KnownBits RHS,
                                       const KnownBits &Borrow);

  /// Compute knownbits resulting from llvm.sadd.sat(LHS, RHS)
  static KnownBits sadd_sat(const KnownBits &LHS, const KnownBits &RHS);

  /// Compute knownbits resulting from llvm.uadd.sat(LHS, RHS)
  static KnownBits uadd_sat(const KnownBits &LHS, const KnownBits &RHS);

  /// Compute knownbits resulting from llvm.ssub.sat(LHS, RHS)
  static KnownBits ssub_sat(const KnownBits &LHS, const KnownBits &RHS);

  /// Compute knownbits resulting from llvm.usub.sat(LHS, RHS)
  static KnownBits usub_sat(const KnownBits &LHS, const KnownBits &RHS);

  /// Compute known bits resulting from multiplying LHS and RHS.
  static KnownBits mul(const KnownBits &LHS, const KnownBits &RHS,
                       bool NoUndefSelfMultiply = false);

  /// Compute known bits from sign-extended multiply-hi.
  static KnownBits mulhs(const KnownBits &LHS, const KnownBits &RHS);

  /// Compute known bits from zero-extended multiply-hi.
  static KnownBits mulhu(const KnownBits &LHS, const KnownBits &RHS);

  /// Compute known bits for sdiv(LHS, RHS).
  static KnownBits sdiv(const KnownBits &LHS, const KnownBits &RHS,
                        bool Exact = false);

  /// Compute known bits for udiv(LHS, RHS).
  static KnownBits udiv(const KnownBits &LHS, const KnownBits &RHS,
                        bool Exact = false);

  /// Compute known bits for urem(LHS, RHS).
  static KnownBits urem(const KnownBits &LHS, const KnownBits &RHS);

  /// Compute known bits for srem(LHS, RHS).
  static KnownBits srem(const KnownBits &LHS, const KnownBits &RHS);

  /// Compute known bits for umax(LHS, RHS).
  static KnownBits umax(const KnownBits &LHS, const KnownBits &RHS);

  /// Compute known bits for umin(LHS, RHS).
  static KnownBits umin(const KnownBits &LHS, const KnownBits &RHS);

  /// Compute known bits for smax(LHS, RHS).
  static KnownBits smax(const KnownBits &LHS, const KnownBits &RHS);

  /// Compute known bits for smin(LHS, RHS).
  static KnownBits smin(const KnownBits &LHS, const KnownBits &RHS);

  /// Compute known bits for shl(LHS, RHS).
  /// NOTE: RHS (shift amount) bitwidth doesn't need to be the same as LHS.
  static KnownBits shl(const KnownBits &LHS, const KnownBits &RHS,
                       bool NUW = false, bool NSW = false,
                       bool ShAmtNonZero = false);

  /// Compute known bits for lshr(LHS, RHS).
  /// NOTE: RHS (shift amount) bitwidth doesn't need to be the same as LHS.
  static KnownBits lshr(const KnownBits &LHS, const KnownBits &RHS,
                        bool ShAmtNonZero = false);

  /// Compute known bits for ashr(LHS, RHS).
  /// NOTE: RHS (shift amount) bitwidth doesn't need to be the same as LHS.
  static KnownBits ashr(const KnownBits &LHS, const KnownBits &RHS,
                        bool ShAmtNonZero = false);

  /// Comparison results: -1 = unknown, 0 = false, 1 = true
  static int eq(const KnownBits &LHS, const KnownBits &RHS);
  static int ne(const KnownBits &LHS, const KnownBits &RHS);
  static int ugt(const KnownBits &LHS, const KnownBits &RHS);
  static int uge(const KnownBits &LHS, const KnownBits &RHS);
  static int ult(const KnownBits &LHS, const KnownBits &RHS);
  static int ule(const KnownBits &LHS, const KnownBits &RHS);
  static int sgt(const KnownBits &LHS, const KnownBits &RHS);
  static int sge(const KnownBits &LHS, const KnownBits &RHS);
  static int slt(const KnownBits &LHS, const KnownBits &RHS);
  static int sle(const KnownBits &LHS, const KnownBits &RHS);

  /// Update known bits based on ANDing with RHS.
  KnownBits &operator&=(const KnownBits &RHS);

  /// Update known bits based on ORing with RHS.
  KnownBits &operator|=(const KnownBits &RHS);

  /// Update known bits based on XORing with RHS.
  KnownBits &operator^=(const KnownBits &RHS);

  /// Compute known bits for the absolute value.
  KnownBits abs(bool IntMinIsPoison = false) const;

  KnownBits byteSwap() const {
    return KnownBits(Zero.byteSwap(), One.byteSwap());
  }

  KnownBits reverseBits() const {
    return KnownBits(Zero.reverseBits(), One.reverseBits());
  }

  /// Compute known bits for X & -X, which has only the lowest bit set of X set.
  /// The name comes from the X86 BMI instruction
  KnownBits blsi() const;

  /// Compute known bits for X ^ (X - 1), which has all bits up to and including
  /// the lowest set bit of X set. The name comes from the X86 BMI instruction.
  KnownBits blsmsk() const;

  bool operator==(const KnownBits &Other) const {
    return Zero == Other.Zero && One == Other.One;
  }

  bool operator!=(const KnownBits &Other) const { return !(*this == Other); }

  void print(raw_ostream &OS) const;
  void dump() const;

private:
  // Internal helper for getting the initial KnownBits for an `srem` or `urem`
  // operation with the low-bits set.
  static KnownBits remGetLowBits(const KnownBits &LHS, const KnownBits &RHS);
};

inline KnownBits operator&(KnownBits LHS, const KnownBits &RHS) {
  LHS &= RHS;
  return LHS;
}

inline KnownBits operator&(const KnownBits &LHS, KnownBits &&RHS) {
  RHS &= LHS;
  return std::move(RHS);
}

inline KnownBits operator|(KnownBits LHS, const KnownBits &RHS) {
  LHS |= RHS;
  return LHS;
}

inline KnownBits operator|(const KnownBits &LHS, KnownBits &&RHS) {
  RHS |= LHS;
  return std::move(RHS);
}

inline KnownBits operator^(KnownBits LHS, const KnownBits &RHS) {
  LHS ^= RHS;
  return LHS;
}

inline KnownBits operator^(const KnownBits &LHS, KnownBits &&RHS) {
  RHS ^= LHS;
  return std::move(RHS);
}

inline raw_ostream &operator<<(raw_ostream &OS, const KnownBits &Known) {
  Known.print(OS);
  return OS;
}

// ---------------------------------------------------------------------------
// Inline KnownBits method definitions
// ---------------------------------------------------------------------------

namespace detail {
inline KnownBits computeForAddCarryImpl(const KnownBits &LHS,
                                        const KnownBits &RHS, bool CarryZero,
                                        bool CarryOne) {
  assert(!(CarryZero && CarryOne) &&
         "Carry can't be zero and one at the same time");
  APInt PossibleSumZero = LHS.getMaxValue() + RHS.getMaxValue() + !CarryZero;
  APInt PossibleSumOne = LHS.getMinValue() + RHS.getMinValue() + CarryOne;
  APInt CarryKnownZero = ~(PossibleSumZero ^ LHS.Zero ^ RHS.Zero);
  APInt CarryKnownOne = PossibleSumOne ^ LHS.One ^ RHS.One;
  APInt LHSKnownUnion = LHS.Zero | LHS.One;
  APInt RHSKnownUnion = RHS.Zero | RHS.One;
  APInt CarryKnownUnion = CarryKnownZero | CarryKnownOne;
  APInt Known = LHSKnownUnion & RHSKnownUnion & CarryKnownUnion;
  assert((PossibleSumZero & Known) == (PossibleSumOne & Known) &&
         "known bits of sum differ");
  KnownBits KnownOut;
  KnownOut.Zero = ~PossibleSumZero & Known;
  KnownOut.One = PossibleSumOne & Known;
  return KnownOut;
}

inline unsigned getMaxShiftAmountImpl(const APInt &MaxValue,
                                      unsigned BitWidth) {
  if (isPowerOf2_32(BitWidth))
    return MaxValue.extractBitsAsZExtValue(Log2_32(BitWidth), 0);
  return MaxValue.getLimitedValue(BitWidth - 1);
}

inline KnownBits divComputeLowBitImpl(KnownBits Known, const KnownBits &LHS,
                                      const KnownBits &RHS, bool Exact) {
  if (!Exact)
    return Known;
  if (LHS.One[0])
    Known.One.setBit(0);
  int MinTZ =
      (int)LHS.countMinTrailingZeros() - (int)RHS.countMaxTrailingZeros();
  int MaxTZ =
      (int)LHS.countMaxTrailingZeros() - (int)RHS.countMinTrailingZeros();
  if (MinTZ >= 0) {
    Known.Zero.setLowBits(MinTZ);
    if (MinTZ == MaxTZ)
      Known.One.setBit(MinTZ);
  } else if (MaxTZ < 0) {
    Known.setAllZero();
  }
  if (Known.hasConflict())
    Known.setAllZero();
  return Known;
}
} // namespace detail

inline KnownBits KnownBits::computeForAddCarry(const KnownBits &LHS,
                                               const KnownBits &RHS,
                                               const KnownBits &Carry) {
  assert(Carry.getBitWidth() == 1 && "Carry must be 1-bit");
  return detail::computeForAddCarryImpl(LHS, RHS, Carry.Zero.getBoolValue(),
                                        Carry.One.getBoolValue());
}

inline KnownBits KnownBits::computeForAddSub(bool Add, bool NSW,
                                             const KnownBits &LHS,
                                             KnownBits RHS) {
  KnownBits KnownOut;
  if (Add) {
    KnownOut = detail::computeForAddCarryImpl(LHS, RHS, true, false);
  } else {
    {
      APInt swptmp = RHS.Zero;
      RHS.Zero = RHS.One;
      RHS.One = swptmp;
    }
    KnownOut = detail::computeForAddCarryImpl(LHS, RHS, false, true);
  }
  if (!KnownOut.isNegative() && !KnownOut.isNonNegative()) {
    if (NSW) {
      if (LHS.isNonNegative() && RHS.isNonNegative())
        KnownOut.makeNonNegative();
      else if (LHS.isNegative() && RHS.isNegative())
        KnownOut.makeNegative();
    }
  }
  return KnownOut;
}

inline KnownBits KnownBits::computeForSubBorrow(const KnownBits &LHS,
                                                KnownBits RHS,
                                                const KnownBits &Borrow) {
  assert(Borrow.getBitWidth() == 1 && "Borrow must be 1-bit");
  {
    APInt swptmp = RHS.Zero;
    RHS.Zero = RHS.One;
    RHS.One = swptmp;
  }
  return detail::computeForAddCarryImpl(LHS, RHS, Borrow.One.getBoolValue(),
                                        Borrow.Zero.getBoolValue());
}

inline KnownBits KnownBits::sextInReg(unsigned SrcBitWidth) const {
  unsigned BitWidth = getBitWidth();
  assert(0 < SrcBitWidth && SrcBitWidth <= BitWidth &&
         "Illegal sext-in-register");
  if (SrcBitWidth == BitWidth)
    return *this;
  unsigned ExtBits = BitWidth - SrcBitWidth;
  KnownBits Result;
  Result.One = One << ExtBits;
  Result.Zero = Zero << ExtBits;
  Result.One.ashrInPlace(ExtBits);
  Result.Zero.ashrInPlace(ExtBits);
  return Result;
}

inline KnownBits KnownBits::makeGE(const APInt &Val) const {
  unsigned N = (Zero | Val).countl_one();
  APInt MaskedVal(Val);
  MaskedVal.clearLowBits(getBitWidth() - N);
  return KnownBits(Zero, One | MaskedVal);
}

inline KnownBits KnownBits::umax(const KnownBits &LHS, const KnownBits &RHS) {
  if (LHS.getMinValue().uge(RHS.getMaxValue()))
    return LHS;
  if (RHS.getMinValue().uge(LHS.getMaxValue()))
    return RHS;
  KnownBits L = LHS.makeGE(RHS.getMinValue());
  KnownBits R = RHS.makeGE(LHS.getMinValue());
  return L.intersectWith(R);
}

inline KnownBits KnownBits::umin(const KnownBits &LHS, const KnownBits &RHS) {
  auto Flip = [](const KnownBits &Val) { return KnownBits(Val.One, Val.Zero); };
  return Flip(umax(Flip(LHS), Flip(RHS)));
}

inline KnownBits KnownBits::smax(const KnownBits &LHS, const KnownBits &RHS) {
  auto Flip = [](const KnownBits &Val) {
    unsigned SignBitPosition = Val.getBitWidth() - 1;
    APInt Z = Val.Zero, O = Val.One;
    Z.setBitVal(SignBitPosition, Val.One[SignBitPosition]);
    O.setBitVal(SignBitPosition, Val.Zero[SignBitPosition]);
    return KnownBits(Z, O);
  };
  return Flip(umax(Flip(LHS), Flip(RHS)));
}

inline KnownBits KnownBits::smin(const KnownBits &LHS, const KnownBits &RHS) {
  auto Flip = [](const KnownBits &Val) {
    unsigned SignBitPosition = Val.getBitWidth() - 1;
    APInt Z = Val.One, O = Val.Zero;
    Z.setBitVal(SignBitPosition, Val.Zero[SignBitPosition]);
    O.setBitVal(SignBitPosition, Val.One[SignBitPosition]);
    return KnownBits(Z, O);
  };
  return Flip(umax(Flip(LHS), Flip(RHS)));
}

inline KnownBits KnownBits::shl(const KnownBits &LHS, const KnownBits &RHS,
                                bool NUW, bool NSW, bool ShAmtNonZero) {
  unsigned BitWidth = LHS.getBitWidth();
  auto ShiftByConst = [&](const KnownBits &L, unsigned ShiftAmt) {
    KnownBits Known;
    bool ShiftedOutZero, ShiftedOutOne;
    Known.Zero = L.Zero.ushl_ov(ShiftAmt, ShiftedOutZero);
    Known.Zero.setLowBits(ShiftAmt);
    Known.One = L.One.ushl_ov(ShiftAmt, ShiftedOutOne);
    if (NSW) {
      if (NUW && ShiftAmt != 0)
        ShiftedOutZero = true;
      if (ShiftedOutZero)
        Known.makeNonNegative();
      else if (ShiftedOutOne)
        Known.makeNegative();
    }
    return Known;
  };
  KnownBits Known(BitWidth);
  unsigned MinShiftAmount = RHS.getMinValue().getLimitedValue(BitWidth);
  if (MinShiftAmount == 0 && ShAmtNonZero)
    MinShiftAmount = 1;
  if (LHS.isUnknown()) {
    Known.Zero.setLowBits(MinShiftAmount);
    if (NUW && NSW && MinShiftAmount != 0)
      Known.makeNonNegative();
    return Known;
  }
  APInt MaxValue = RHS.getMaxValue();
  unsigned MaxShiftAmount = detail::getMaxShiftAmountImpl(MaxValue, BitWidth);
  if (NUW && NSW)
    MaxShiftAmount = std::min(MaxShiftAmount, LHS.countMaxLeadingZeros() - 1);
  if (NUW)
    MaxShiftAmount = std::min(MaxShiftAmount, LHS.countMaxLeadingZeros());
  if (NSW)
    MaxShiftAmount = std::min(
        MaxShiftAmount,
        std::max(LHS.countMaxLeadingZeros(), LHS.countMaxLeadingOnes()) - 1);
  if (MinShiftAmount == 0 && MaxShiftAmount == BitWidth - 1 &&
      isPowerOf2_32(BitWidth)) {
    Known.Zero.setLowBits(LHS.countMinTrailingZeros());
    if (LHS.isAllOnes())
      Known.One.setSignBit();
    if (NSW) {
      if (LHS.isNonNegative())
        Known.makeNonNegative();
      if (LHS.isNegative())
        Known.makeNegative();
    }
    return Known;
  }
  unsigned ShiftAmtZeroMask = RHS.Zero.zextOrTrunc(32).getZExtValue();
  unsigned ShiftAmtOneMask = RHS.One.zextOrTrunc(32).getZExtValue();
  Known.Zero.setAllBits();
  Known.One.setAllBits();
  for (unsigned ShiftAmt = MinShiftAmount; ShiftAmt <= MaxShiftAmount;
       ++ShiftAmt) {
    if ((ShiftAmtZeroMask & ShiftAmt) != 0 ||
        (ShiftAmtOneMask | ShiftAmt) != ShiftAmt)
      continue;
    Known = Known.intersectWith(ShiftByConst(LHS, ShiftAmt));
    if (Known.isUnknown())
      break;
  }
  if (Known.hasConflict())
    Known.setAllZero();
  return Known;
}

inline KnownBits KnownBits::lshr(const KnownBits &LHS, const KnownBits &RHS,
                                 bool ShAmtNonZero) {
  unsigned BitWidth = LHS.getBitWidth();
  auto ShiftByConst = [&](const KnownBits &L, unsigned ShiftAmt) {
    KnownBits Known = L;
    Known.Zero.lshrInPlace(ShiftAmt);
    Known.One.lshrInPlace(ShiftAmt);
    Known.Zero.setHighBits(ShiftAmt);
    return Known;
  };
  KnownBits Known(BitWidth);
  unsigned MinShiftAmount = RHS.getMinValue().getLimitedValue(BitWidth);
  if (MinShiftAmount == 0 && ShAmtNonZero)
    MinShiftAmount = 1;
  if (LHS.isUnknown()) {
    Known.Zero.setHighBits(MinShiftAmount);
    return Known;
  }
  APInt MaxValue = RHS.getMaxValue();
  unsigned MaxShiftAmount = detail::getMaxShiftAmountImpl(MaxValue, BitWidth);
  unsigned ShiftAmtZeroMask = RHS.Zero.zextOrTrunc(32).getZExtValue();
  unsigned ShiftAmtOneMask = RHS.One.zextOrTrunc(32).getZExtValue();
  Known.Zero.setAllBits();
  Known.One.setAllBits();
  for (unsigned ShiftAmt = MinShiftAmount; ShiftAmt <= MaxShiftAmount;
       ++ShiftAmt) {
    if ((ShiftAmtZeroMask & ShiftAmt) != 0 ||
        (ShiftAmtOneMask | ShiftAmt) != ShiftAmt)
      continue;
    Known = Known.intersectWith(ShiftByConst(LHS, ShiftAmt));
    if (Known.isUnknown())
      break;
  }
  if (Known.hasConflict())
    Known.setAllZero();
  return Known;
}

inline KnownBits KnownBits::ashr(const KnownBits &LHS, const KnownBits &RHS,
                                 bool ShAmtNonZero) {
  unsigned BitWidth = LHS.getBitWidth();
  auto ShiftByConst = [&](const KnownBits &L, unsigned ShiftAmt) {
    KnownBits Known = L;
    Known.Zero.ashrInPlace(ShiftAmt);
    Known.One.ashrInPlace(ShiftAmt);
    return Known;
  };
  KnownBits Known(BitWidth);
  unsigned MinShiftAmount = RHS.getMinValue().getLimitedValue(BitWidth);
  if (MinShiftAmount == 0 && ShAmtNonZero)
    MinShiftAmount = 1;
  if (LHS.isUnknown()) {
    if (MinShiftAmount == BitWidth) {
      Known.setAllZero();
      return Known;
    }
    return Known;
  }
  APInt MaxValue = RHS.getMaxValue();
  unsigned MaxShiftAmount = detail::getMaxShiftAmountImpl(MaxValue, BitWidth);
  unsigned ShiftAmtZeroMask = RHS.Zero.zextOrTrunc(32).getZExtValue();
  unsigned ShiftAmtOneMask = RHS.One.zextOrTrunc(32).getZExtValue();
  Known.Zero.setAllBits();
  Known.One.setAllBits();
  for (unsigned ShiftAmt = MinShiftAmount; ShiftAmt <= MaxShiftAmount;
       ++ShiftAmt) {
    if ((ShiftAmtZeroMask & ShiftAmt) != 0 ||
        (ShiftAmtOneMask | ShiftAmt) != ShiftAmt)
      continue;
    Known = Known.intersectWith(ShiftByConst(LHS, ShiftAmt));
    if (Known.isUnknown())
      break;
  }
  if (Known.hasConflict())
    Known.setAllZero();
  return Known;
}

inline int KnownBits::eq(const KnownBits &LHS, const KnownBits &RHS) {
  if (LHS.isConstant() && RHS.isConstant())
    return LHS.getConstant() == RHS.getConstant() ? 1 : 0;
  if (LHS.One.intersects(RHS.Zero) || RHS.One.intersects(LHS.Zero))
    return 0;
  return -1;
}
inline int KnownBits::ne(const KnownBits &LHS, const KnownBits &RHS) {
  int r = eq(LHS, RHS);
  return r < 0 ? -1 : !r;
}
inline int KnownBits::ugt(const KnownBits &LHS, const KnownBits &RHS) {
  if (LHS.getMaxValue().ule(RHS.getMinValue()))
    return 0;
  if (LHS.getMinValue().ugt(RHS.getMaxValue()))
    return 1;
  return -1;
}
inline int KnownBits::uge(const KnownBits &LHS, const KnownBits &RHS) {
  int r = ugt(RHS, LHS);
  return r < 0 ? -1 : !r;
}
inline int KnownBits::ult(const KnownBits &LHS, const KnownBits &RHS) {
  return ugt(RHS, LHS);
}
inline int KnownBits::ule(const KnownBits &LHS, const KnownBits &RHS) {
  return uge(RHS, LHS);
}
inline int KnownBits::sgt(const KnownBits &LHS, const KnownBits &RHS) {
  if (LHS.getSignedMaxValue().sle(RHS.getSignedMinValue()))
    return 0;
  if (LHS.getSignedMinValue().sgt(RHS.getSignedMaxValue()))
    return 1;
  return -1;
}
inline int KnownBits::sge(const KnownBits &LHS, const KnownBits &RHS) {
  int r = sgt(RHS, LHS);
  return r < 0 ? -1 : !r;
}
inline int KnownBits::slt(const KnownBits &LHS, const KnownBits &RHS) {
  return sgt(RHS, LHS);
}
inline int KnownBits::sle(const KnownBits &LHS, const KnownBits &RHS) {
  return sge(RHS, LHS);
}

inline KnownBits KnownBits::abs(bool IntMinIsPoison) const {
  if (isNonNegative())
    return *this;
  KnownBits KnownAbs(getBitWidth());
  if (isNegative()) {
    KnownBits Tmp = *this;
    if (IntMinIsPoison && (Zero.popcount() + 2) == getBitWidth())
      Tmp.One.setBit(countMinTrailingZeros());
    KnownAbs =
        computeForAddSub(false, IntMinIsPoison,
                         KnownBits::makeConstant(APInt(getBitWidth(), 0)), Tmp);
    if (IntMinIsPoison && Tmp.countMinPopulation() == 1 &&
        Tmp.countMaxPopulation() != 1) {
      Tmp.One.clearSignBit();
      Tmp.Zero.setSignBit();
      KnownAbs.One.setBits(getBitWidth() - Tmp.countMinLeadingZeros(),
                           getBitWidth() - 1);
    }
  } else {
    unsigned MaxTZ = countMaxTrailingZeros();
    unsigned MinTZ = countMinTrailingZeros();
    KnownAbs.Zero.setLowBits(MinTZ);
    if (MaxTZ == MinTZ && MaxTZ < getBitWidth())
      KnownAbs.One.setBit(MaxTZ);
    if (IntMinIsPoison || (!One.isZero() && !One.isMinSignedValue())) {
      KnownAbs.One.clearSignBit();
      KnownAbs.Zero.setSignBit();
    }
  }
  assert(!KnownAbs.hasConflict() && "Bad Output");
  return KnownAbs;
}

namespace detail {
inline KnownBits computeForSatAddSubImpl(bool Add, bool Signed,
                                         const KnownBits &LHS,
                                         const KnownBits &RHS) {
  assert(!LHS.hasConflict() && !RHS.hasConflict() && "Bad inputs");
  KnownBits Res = KnownBits::computeForAddSub(Add, false, LHS, RHS);
  unsigned BitWidth = Res.getBitWidth();
  auto SignBitKnown = [&](const KnownBits &K) {
    return K.Zero[BitWidth - 1] || K.One[BitWidth - 1];
  };
  int Overflow = -1;
  if (Signed) {
    if (SignBitKnown(LHS) && SignBitKnown(RHS) && SignBitKnown(Res)) {
      if (Add)
        Overflow = (LHS.isNonNegative() == RHS.isNonNegative() &&
                    Res.isNonNegative() != LHS.isNonNegative())
                       ? 1
                       : 0;
      else
        Overflow = (LHS.isNonNegative() != RHS.isNonNegative() &&
                    Res.isNonNegative() != LHS.isNonNegative())
                       ? 1
                       : 0;
    }
  } else if (Add) {
    bool Of;
    (void)LHS.getMaxValue().uadd_ov(RHS.getMaxValue(), Of);
    if (!Of) {
      Overflow = 0;
    } else {
      (void)LHS.getMinValue().uadd_ov(RHS.getMinValue(), Of);
      if (Of)
        Overflow = 1;
    }
  } else {
    bool Of;
    (void)LHS.getMinValue().usub_ov(RHS.getMaxValue(), Of);
    if (!Of) {
      Overflow = 0;
    } else {
      (void)LHS.getMaxValue().usub_ov(RHS.getMinValue(), Of);
      if (Of)
        Overflow = 1;
    }
  }
  if (Signed) {
    if (Add) {
      if (LHS.isNonNegative() && RHS.isNonNegative()) {
        Res.One.clearSignBit();
        Res.Zero.setSignBit();
      }
      if (LHS.isNegative() && RHS.isNegative()) {
        Res.One.setSignBit();
        Res.Zero.clearSignBit();
      }
    } else {
      if (LHS.isNegative() && RHS.isNonNegative()) {
        Res.One.setSignBit();
        Res.Zero.clearSignBit();
      } else if (LHS.isNonNegative() && RHS.isNegative()) {
        Res.One.clearSignBit();
        Res.Zero.setSignBit();
      }
    }
  } else {
    unsigned LeadingKnown;
    if (Add)
      LeadingKnown =
          std::max(LHS.countMinLeadingOnes(), RHS.countMinLeadingOnes());
    else
      LeadingKnown =
          std::max(LHS.countMinLeadingZeros(), RHS.countMinLeadingOnes());
    APInt Mask = APInt::getHighBitsSet(BitWidth, LeadingKnown);
    if (Add) {
      Res.One |= Mask;
      Res.Zero &= ~Mask;
    } else {
      Res.Zero |= Mask;
      Res.One &= ~Mask;
    }
  }
  if (Overflow >= 0) {
    if (Overflow == 0) {
      assert(!Res.hasConflict());
      return Res;
    }
    APInt C;
    if (Signed) {
      assert(SignBitKnown(LHS));
      C = LHS.isNegative() ? APInt::getSignedMinValue(BitWidth)
                           : APInt::getSignedMaxValue(BitWidth);
    } else if (Add) {
      C = APInt::getMaxValue(BitWidth);
    } else {
      C = APInt::getMinValue(BitWidth);
    }
    Res.One = C;
    Res.Zero = ~C;
    assert(!Res.hasConflict());
    return Res;
  }
  if (Signed) {
    Res.Zero.clearLowBits(BitWidth - 1);
    Res.One.clearLowBits(BitWidth - 1);
  } else if (Add) {
    Res.Zero.clearAllBits();
  } else {
    Res.One.clearAllBits();
  }
  assert(!Res.hasConflict());
  return Res;
}
} // namespace detail

inline KnownBits KnownBits::sadd_sat(const KnownBits &LHS,
                                     const KnownBits &RHS) {
  return detail::computeForSatAddSubImpl(true, true, LHS, RHS);
}
inline KnownBits KnownBits::ssub_sat(const KnownBits &LHS,
                                     const KnownBits &RHS) {
  return detail::computeForSatAddSubImpl(false, true, LHS, RHS);
}
inline KnownBits KnownBits::uadd_sat(const KnownBits &LHS,
                                     const KnownBits &RHS) {
  return detail::computeForSatAddSubImpl(true, false, LHS, RHS);
}
inline KnownBits KnownBits::usub_sat(const KnownBits &LHS,
                                     const KnownBits &RHS) {
  return detail::computeForSatAddSubImpl(false, false, LHS, RHS);
}

inline KnownBits KnownBits::mul(const KnownBits &LHS, const KnownBits &RHS,
                                bool NoUndefSelfMultiply) {
  unsigned BitWidth = LHS.getBitWidth();
  assert(BitWidth == RHS.getBitWidth() && !LHS.hasConflict() &&
         !RHS.hasConflict());
  assert((!NoUndefSelfMultiply || LHS == RHS));
  APInt UMaxLHS = LHS.getMaxValue(), UMaxRHS = RHS.getMaxValue();
  bool HasOverflow;
  APInt UMaxResult = UMaxLHS.umul_ov(UMaxRHS, HasOverflow);
  unsigned LeadZ = HasOverflow ? 0 : UMaxResult.countl_zero();
  const APInt &Bottom0 = LHS.One, &Bottom1 = RHS.One;
  unsigned TrailBitsKnown0 = (LHS.Zero | LHS.One).countr_one();
  unsigned TrailBitsKnown1 = (RHS.Zero | RHS.One).countr_one();
  unsigned TrailZero0 = LHS.countMinTrailingZeros();
  unsigned TrailZero1 = RHS.countMinTrailingZeros();
  unsigned TrailZ = TrailZero0 + TrailZero1;
  unsigned SmallestOperand =
      std::min(TrailBitsKnown0 - TrailZero0, TrailBitsKnown1 - TrailZero1);
  unsigned ResultBitsKnown = std::min(SmallestOperand + TrailZ, BitWidth);
  APInt BottomKnown =
      Bottom0.getLoBits(TrailBitsKnown0) * Bottom1.getLoBits(TrailBitsKnown1);
  KnownBits Res(BitWidth);
  Res.Zero.setHighBits(LeadZ);
  Res.Zero |= (~BottomKnown).getLoBits(ResultBitsKnown);
  Res.One = BottomKnown.getLoBits(ResultBitsKnown);
  if (NoUndefSelfMultiply && BitWidth > 1) {
    assert(Res.One[1] == 0);
    Res.Zero.setBit(1);
  }
  return Res;
}

inline KnownBits KnownBits::mulhs(const KnownBits &LHS, const KnownBits &RHS) {
  unsigned BitWidth = LHS.getBitWidth();
  assert(BitWidth == RHS.getBitWidth() && !LHS.hasConflict() &&
         !RHS.hasConflict());
  return mul(LHS.sext(2 * BitWidth), RHS.sext(2 * BitWidth))
      .extractBits(BitWidth, BitWidth);
}

inline KnownBits KnownBits::mulhu(const KnownBits &LHS, const KnownBits &RHS) {
  unsigned BitWidth = LHS.getBitWidth();
  assert(BitWidth == RHS.getBitWidth() && !LHS.hasConflict() &&
         !RHS.hasConflict());
  return mul(LHS.zext(2 * BitWidth), RHS.zext(2 * BitWidth))
      .extractBits(BitWidth, BitWidth);
}

inline KnownBits KnownBits::sdiv(const KnownBits &LHS, const KnownBits &RHS,
                                 bool Exact) {
  if (LHS.isNonNegative() && RHS.isNonNegative())
    return udiv(LHS, RHS, Exact);
  unsigned BitWidth = LHS.getBitWidth();
  assert(!LHS.hasConflict() && !RHS.hasConflict());
  KnownBits Known(BitWidth);
  if (LHS.isZero() || RHS.isZero()) {
    Known.setAllZero();
    return Known;
  }
  APInt Res;
  bool HasRes = false;
  if (LHS.isNegative() && RHS.isNegative()) {
    APInt Denom = RHS.getSignedMaxValue(), Num = LHS.getSignedMinValue();
    Res = (Num.isMinSignedValue() && Denom.isAllOnes())
              ? APInt::getSignedMaxValue(BitWidth)
              : Num.sdiv(Denom);
    HasRes = true;
  } else if (LHS.isNegative() && RHS.isNonNegative()) {
    if (Exact || (-LHS.getSignedMaxValue()).uge(RHS.getSignedMaxValue())) {
      APInt Denom = RHS.getSignedMinValue(), Num = LHS.getSignedMinValue();
      Res = Denom.isZero() ? Num : Num.sdiv(Denom);
      HasRes = true;
    }
  } else if (LHS.isStrictlyPositive() && RHS.isNegative()) {
    if (Exact || LHS.getSignedMinValue().uge(-RHS.getSignedMinValue())) {
      Res = LHS.getSignedMaxValue().sdiv(RHS.getSignedMaxValue());
      HasRes = true;
    }
  }
  if (HasRes) {
    if (Res.isNonNegative())
      Known.Zero.setHighBits(Res.countLeadingZeros());
    else
      Known.One.setHighBits(Res.countLeadingOnes());
  }
  Known = detail::divComputeLowBitImpl(Known, LHS, RHS, Exact);
  assert(!Known.hasConflict());
  return Known;
}

inline KnownBits KnownBits::udiv(const KnownBits &LHS, const KnownBits &RHS,
                                 bool Exact) {
  unsigned BitWidth = LHS.getBitWidth();
  assert(!LHS.hasConflict() && !RHS.hasConflict());
  KnownBits Known(BitWidth);
  if (LHS.isZero() || RHS.isZero()) {
    Known.setAllZero();
    return Known;
  }
  APInt MinDenom = RHS.getMinValue(), MaxNum = LHS.getMaxValue();
  APInt MaxRes = MinDenom.isZero() ? MaxNum : MaxNum.udiv(MinDenom);
  Known.Zero.setHighBits(MaxRes.countLeadingZeros());
  Known = detail::divComputeLowBitImpl(Known, LHS, RHS, Exact);
  assert(!Known.hasConflict());
  return Known;
}

inline KnownBits KnownBits::remGetLowBits(const KnownBits &LHS,
                                          const KnownBits &RHS) {
  unsigned BitWidth = LHS.getBitWidth();
  if (!RHS.isZero() && RHS.Zero[0]) {
    unsigned RHSZeros = RHS.countMinTrailingZeros();
    APInt Mask = APInt::getLowBitsSet(BitWidth, RHSZeros);
    return KnownBits(LHS.Zero & Mask, LHS.One & Mask);
  }
  return KnownBits(BitWidth);
}

inline KnownBits KnownBits::urem(const KnownBits &LHS, const KnownBits &RHS) {
  assert(!LHS.hasConflict() && !RHS.hasConflict());
  KnownBits Known = remGetLowBits(LHS, RHS);
  if (RHS.isConstant() && RHS.getConstant().isPowerOf2()) {
    Known.Zero |= ~(RHS.getConstant() - 1);
    return Known;
  }
  uint32_t Leaders =
      std::max(LHS.countMinLeadingZeros(), RHS.countMinLeadingZeros());
  Known.Zero.setHighBits(Leaders);
  return Known;
}

inline KnownBits KnownBits::srem(const KnownBits &LHS, const KnownBits &RHS) {
  assert(!LHS.hasConflict() && !RHS.hasConflict());
  KnownBits Known = remGetLowBits(LHS, RHS);
  if (RHS.isConstant() && RHS.getConstant().isPowerOf2()) {
    APInt LowBits = RHS.getConstant() - 1;
    if (LHS.isNonNegative() || LowBits.isSubsetOf(LHS.Zero))
      Known.Zero |= ~LowBits;
    if (LHS.isNegative() && LowBits.intersects(LHS.One))
      Known.One |= ~LowBits;
    return Known;
  }
  Known.Zero.setHighBits(LHS.countMinLeadingZeros());
  return Known;
}

inline KnownBits &KnownBits::operator&=(const KnownBits &RHS) {
  Zero |= RHS.Zero;
  One &= RHS.One;
  return *this;
}

inline KnownBits &KnownBits::operator|=(const KnownBits &RHS) {
  Zero &= RHS.Zero;
  One |= RHS.One;
  return *this;
}

inline KnownBits &KnownBits::operator^=(const KnownBits &RHS) {
  APInt Z = (Zero & RHS.Zero) | (One & RHS.One);
  One = (Zero & RHS.One) | (One & RHS.Zero);
  Zero = Z;
  return *this;
}

inline KnownBits KnownBits::blsi() const {
  unsigned BitWidth = getBitWidth();
  KnownBits Known(Zero, APInt(BitWidth, 0));
  unsigned Max = countMaxTrailingZeros();
  Known.Zero.setBitsFrom(std::min(Max + 1, BitWidth));
  unsigned Min = countMinTrailingZeros();
  if (Max == Min && Max < BitWidth)
    Known.One.setBit(Max);
  return Known;
}

inline KnownBits KnownBits::blsmsk() const {
  unsigned BitWidth = getBitWidth();
  KnownBits Known(BitWidth);
  unsigned Max = countMaxTrailingZeros();
  Known.Zero.setBitsFrom(std::min(Max + 1, BitWidth));
  unsigned Min = countMinTrailingZeros();
  Known.One.setLowBits(std::min(Min + 1, BitWidth));
  return Known;
}

inline void KnownBits::print(raw_ostream &OS) const {
  unsigned BitWidth = getBitWidth();
  for (unsigned I = 0; I < BitWidth; ++I) {
    unsigned Bit = BitWidth - 1 - I;
    if (Zero[Bit] && One[Bit])
      OS << "!";
    else if (Zero[Bit])
      OS << "0";
    else if (One[Bit])
      OS << "1";
    else
      OS << "?";
  }
}

inline void KnownBits::dump() const {
  print(dbgs());
  dbgs() << "\n";
}

} // end namespace llvm

#endif
