//===- llvm/Support/ScaledNumber.h - Support for scaled numbers -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains functions (and a class) useful for working with scaled
// numbers -- in particular, pairs of integers where one represents digits and
// another represents a scale.  The functions are helpers and live in the
// namespace ScaledNumbers.  The class ScaledNumber is useful for modelling
// certain cost metrics that need simple, integer-like semantics that are easy
// to reason about.
//
// These might remind you of soft-floats.  If you want one of those, you're in
// the wrong place.  Look at include/llvm/ADT/APFloat.h instead.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_SCALEDNUMBER_H
#define LLVM_SUPPORT_SCALEDNUMBER_H

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/MathExtras.h"
#include <limits>
#include <stdint.h>
#include <utility>

#include "csupport/lscaled_lnumber.h"

#ifndef BRIDGE_MIN
#define BRIDGE_MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

namespace llvm {
namespace ScaledNumbers {

template <class T> struct ScaledResult {
  T first;
  int16_t second;
  ScaledResult() = default;
  ScaledResult(T f, int16_t s) : first(f), second(s) {}
  template <class U>
  ScaledResult(const ScaledResult<U> &O)
      : first(static_cast<T>(O.first)), second(O.second) {}
};

/// Maximum scale; same as APFloat for easy debug printing.
const int32_t MaxScale = 16383;

/// Maximum scale; same as APFloat for easy debug printing.
const int32_t MinScale = -16382;

/// Get the width of a number.
template <class DigitsT> inline int getWidth() { return sizeof(DigitsT) * 8; }

/// Conditionally round up a scaled number.
///
/// Given \c Digits and \c Scale, round up iff \c ShouldRound is \c true.
/// Always returns \c Scale unless there's an overflow, in which case it
/// returns \c 1+Scale.
///
/// \pre adding 1 to \c Scale will not overflow INT16_MAX.
template <class DigitsT>
inline ScaledResult<DigitsT> getRounded(DigitsT Digits, int16_t Scale,
                                        bool ShouldRound) {
  static_assert(!std::numeric_limits<DigitsT>::is_signed, "expected unsigned");

  if (ShouldRound)
    if (!++Digits)
      return {DigitsT(1) << (getWidth<DigitsT>() - 1), int16_t(Scale + 1)};
  return {Digits, Scale};
}

/// Convenience helper for 32-bit rounding.
inline ScaledResult<uint32_t> getRounded32(uint32_t Digits, int16_t Scale,
                                           bool ShouldRound) {
  return getRounded(Digits, Scale, ShouldRound);
}

/// Convenience helper for 64-bit rounding.
inline ScaledResult<uint64_t> getRounded64(uint64_t Digits, int16_t Scale,
                                           bool ShouldRound) {
  return getRounded(Digits, Scale, ShouldRound);
}

/// Adjust a 64-bit scaled number down to the appropriate width.
///
/// \pre Adding 64 to \c Scale will not overflow INT16_MAX.
template <class DigitsT>
inline ScaledResult<DigitsT> getAdjusted(uint64_t Digits, int16_t Scale = 0) {
  static_assert(!std::numeric_limits<DigitsT>::is_signed, "expected unsigned");

  const int Width = getWidth<DigitsT>();
  if (Width == 64 || Digits <= std::numeric_limits<DigitsT>::max())
    return {DigitsT(Digits), Scale};

  int Shift = llvm::bit_width(Digits) - Width;
  return getRounded<DigitsT>(Digits >> Shift, Scale + Shift,
                             Digits & (UINT64_C(1) << (Shift - 1)));
}

/// Convenience helper for adjusting to 32 bits.
inline ScaledResult<uint32_t> getAdjusted32(uint64_t Digits,
                                            int16_t Scale = 0) {
  return getAdjusted<uint32_t>(Digits, Scale);
}

/// Convenience helper for adjusting to 64 bits.
inline ScaledResult<uint64_t> getAdjusted64(uint64_t Digits,
                                            int16_t Scale = 0) {
  return getAdjusted<uint64_t>(Digits, Scale);
}

extern "C" int csupport_scaled_compare(uint64_t l, uint64_t r, int scale_diff);

/// Multiply two 64-bit integers to create a 64-bit scaled number.
///
/// Implemented with four 64-bit integer multiplies.
ScaledResult<uint64_t> multiply64(uint64_t LHS, uint64_t RHS);

/// Multiply two 32-bit integers to create a 32-bit scaled number.
///
/// Implemented with one 64-bit integer multiply.
template <class DigitsT>
inline ScaledResult<DigitsT> getProduct(DigitsT LHS, DigitsT RHS) {
  static_assert(!std::numeric_limits<DigitsT>::is_signed, "expected unsigned");

  if (getWidth<DigitsT>() <= 32 || (LHS <= UINT32_MAX && RHS <= UINT32_MAX))
    return getAdjusted<DigitsT>(uint64_t(LHS) * RHS);

  return multiply64(LHS, RHS);
}

/// Convenience helper for 32-bit product.
inline ScaledResult<uint32_t> getProduct32(uint32_t LHS, uint32_t RHS) {
  return getProduct(LHS, RHS);
}

/// Convenience helper for 64-bit product.
inline ScaledResult<uint64_t> getProduct64(uint64_t LHS, uint64_t RHS) {
  return getProduct(LHS, RHS);
}

/// Divide two 64-bit integers to create a 64-bit scaled number.
///
/// Implemented with long division.
///
/// \pre \c Dividend and \c Divisor are non-zero.
ScaledResult<uint64_t> divide64(uint64_t Dividend, uint64_t Divisor);

/// Divide two 32-bit integers to create a 32-bit scaled number.
///
/// Implemented with one 64-bit integer divide/remainder pair.
///
/// \pre \c Dividend and \c Divisor are non-zero.
ScaledResult<uint32_t> divide32(uint32_t Dividend, uint32_t Divisor);

/// Divide two 32-bit numbers to create a 32-bit scaled number.
///
/// Implemented with one 64-bit integer divide/remainder pair.
///
/// Returns \c (DigitsT_MAX, MaxScale) for divide-by-zero (0 for 0/0).
template <class DigitsT>
ScaledResult<DigitsT> getQuotient(DigitsT Dividend, DigitsT Divisor) {
  static_assert(!std::numeric_limits<DigitsT>::is_signed, "expected unsigned");
  static_assert(sizeof(DigitsT) == 4 || sizeof(DigitsT) == 8,
                "expected 32-bit or 64-bit digits");

  if (!Dividend)
    return {0, 0};
  if (!Divisor)
    return {std::numeric_limits<DigitsT>::max(), MaxScale};

  if (getWidth<DigitsT>() == 64)
    return divide64(Dividend, Divisor);
  return divide32(Dividend, Divisor);
}

/// Convenience helper for 32-bit quotient.
inline ScaledResult<uint32_t> getQuotient32(uint32_t Dividend,
                                            uint32_t Divisor) {
  return getQuotient(Dividend, Divisor);
}

/// Convenience helper for 64-bit quotient.
inline ScaledResult<uint64_t> getQuotient64(uint64_t Dividend,
                                            uint64_t Divisor) {
  return getQuotient(Dividend, Divisor);
}

/// Implementation of getLg() and friends.
///
/// Returns the rounded lg of \c Digits*2^Scale and an int specifying whether
/// this was rounded up (1), down (-1), or exact (0).
///
/// Returns \c INT32_MIN when \c Digits is zero.
struct LgResult {
  int32_t first;
  int second;
};

template <class DigitsT>
inline LgResult getLgImpl(DigitsT Digits, int16_t Scale) {
  static_assert(!std::numeric_limits<DigitsT>::is_signed, "expected unsigned");

  if (!Digits)
    return {INT32_MIN, 0};

  static_assert(sizeof(Digits) <= sizeof(uint64_t));
  int32_t LocalFloor = llvm::Log2_64(Digits);

  int32_t Floor = Scale + LocalFloor;
  if (Digits == UINT64_C(1) << LocalFloor)
    return {Floor, 0};

  assert(LocalFloor >= 1);
  bool Round = Digits & UINT64_C(1) << (LocalFloor - 1);
  return {Floor + (int)Round, Round ? 1 : -1};
}

/// Get the lg (rounded) of a scaled number.
///
/// Get the lg of \c Digits*2^Scale.
///
/// Returns \c INT32_MIN when \c Digits is zero.
template <class DigitsT> int32_t getLg(DigitsT Digits, int16_t Scale) {
  return getLgImpl(Digits, Scale).first;
}

/// Get the lg floor of a scaled number.
///
/// Get the floor of the lg of \c Digits*2^Scale.
///
/// Returns \c INT32_MIN when \c Digits is zero.
template <class DigitsT> int32_t getLgFloor(DigitsT Digits, int16_t Scale) {
  auto Lg = getLgImpl(Digits, Scale);
  return Lg.first - (Lg.second > 0);
}

/// Get the lg ceiling of a scaled number.
///
/// Get the ceiling of the lg of \c Digits*2^Scale.
///
/// Returns \c INT32_MIN when \c Digits is zero.
template <class DigitsT> int32_t getLgCeiling(DigitsT Digits, int16_t Scale) {
  auto Lg = getLgImpl(Digits, Scale);
  return Lg.first + (Lg.second < 0);
}

/// Implementation for comparing scaled numbers.
///
/// Compare two 64-bit numbers with different scales.  Given that the scale of
/// \c L is higher than that of \c R by \c ScaleDiff, compare them.  Return -1,
/// 1, and 0 for less than, greater than, and equal, respectively.
///
/// \pre 0 <= ScaleDiff < 64.
inline int compareImpl(uint64_t L, uint64_t R, int ScaleDiff) {
  return csupport_scaled_compare(L, R, ScaleDiff);
}

/// Compare two scaled numbers.
///
/// Compare two scaled numbers.  Returns 0 for equal, -1 for less than, and 1
/// for greater than.
template <class DigitsT>
int compare(DigitsT LDigits, int16_t LScale, DigitsT RDigits, int16_t RScale) {
  static_assert(!std::numeric_limits<DigitsT>::is_signed, "expected unsigned");

  // Check for zero.
  if (!LDigits)
    return RDigits ? -1 : 0;
  if (!RDigits)
    return 1;

  // Check for the scale.  Use getLgFloor to be sure that the scale difference
  // is always lower than 64.
  int32_t lgL = getLgFloor(LDigits, LScale), lgR = getLgFloor(RDigits, RScale);
  if (lgL != lgR)
    return lgL < lgR ? -1 : 1;

  // Compare digits.
  if (LScale < RScale)
    return compareImpl(LDigits, RDigits, RScale - LScale);

  return -compareImpl(RDigits, LDigits, LScale - RScale);
}

/// Match scales of two numbers.
///
/// Given two scaled numbers, match up their scales.  Change the digits and
/// scales in place.  Shift the digits as necessary to form equivalent numbers,
/// losing precision only when necessary.
///
/// If the output value of \c LDigits (\c RDigits) is \c 0, the output value of
/// \c LScale (\c RScale) is unspecified.
///
/// As a convenience, returns the matching scale.  If the output value of one
/// number is zero, returns the scale of the other.  If both are zero, which
/// scale is returned is unspecified.
template <class DigitsT>
int16_t matchScales(DigitsT &LDigits, int16_t &LScale, DigitsT &RDigits,
                    int16_t &RScale) {
  static_assert(!std::numeric_limits<DigitsT>::is_signed, "expected unsigned");

  if (LScale < RScale)
    // Swap arguments.
    return matchScales(RDigits, RScale, LDigits, LScale);
  if (!LDigits)
    return RScale;
  if (!RDigits || LScale == RScale)
    return LScale;

  // Now LScale > RScale.  Get the difference.
  int32_t ScaleDiff = int32_t(LScale) - RScale;
  if (ScaleDiff >= 2 * getWidth<DigitsT>()) {
    // Don't bother shifting.  RDigits will get zero-ed out anyway.
    RDigits = 0;
    return LScale;
  }

  // Shift LDigits left as much as possible, then shift RDigits right.
  int32_t clzL = (int32_t)llvm::countl_zero(LDigits);
  int32_t ShiftL = clzL < ScaleDiff ? clzL : ScaleDiff;
  assert(ShiftL < getWidth<DigitsT>() && "can't shift more than width");

  int32_t ShiftR = ScaleDiff - ShiftL;
  if (ShiftR >= getWidth<DigitsT>()) {
    // Don't bother shifting.  RDigits will get zero-ed out anyway.
    RDigits = 0;
    return LScale;
  }

  LDigits <<= ShiftL;
  RDigits >>= ShiftR;

  LScale -= ShiftL;
  RScale += ShiftR;
  assert(LScale == RScale && "scales should match");
  return LScale;
}

/// Get the sum of two scaled numbers.
///
/// Get the sum of two scaled numbers with as much precision as possible.
///
/// \pre Adding 1 to \c LScale (or \c RScale) will not overflow INT16_MAX.
template <class DigitsT>
ScaledResult<DigitsT> getSum(DigitsT LDigits, int16_t LScale, DigitsT RDigits,
                             int16_t RScale) {
  static_assert(!std::numeric_limits<DigitsT>::is_signed, "expected unsigned");

  assert(LScale < INT16_MAX && "scale too large");
  assert(RScale < INT16_MAX && "scale too large");

  int16_t Scale = matchScales(LDigits, LScale, RDigits, RScale);

  DigitsT Sum = LDigits + RDigits;
  if (Sum >= RDigits)
    return {Sum, Scale};

  DigitsT HighBit = DigitsT(1) << (getWidth<DigitsT>() - 1);
  return {DigitsT(HighBit | Sum >> 1), int16_t(Scale + 1)};
}

/// Convenience helper for 32-bit sum.
inline ScaledResult<uint32_t> getSum32(uint32_t LDigits, int16_t LScale,
                                       uint32_t RDigits, int16_t RScale) {
  return getSum(LDigits, LScale, RDigits, RScale);
}

/// Convenience helper for 64-bit sum.
inline ScaledResult<uint64_t> getSum64(uint64_t LDigits, int16_t LScale,
                                       uint64_t RDigits, int16_t RScale) {
  return getSum(LDigits, LScale, RDigits, RScale);
}

/// Get the difference of two scaled numbers.
///
/// Get LHS minus RHS with as much precision as possible.
///
/// Returns \c (0, 0) if the RHS is larger than the LHS.
template <class DigitsT>
ScaledResult<DigitsT> getDifference(DigitsT LDigits, int16_t LScale,
                                    DigitsT RDigits, int16_t RScale) {
  static_assert(!std::numeric_limits<DigitsT>::is_signed, "expected unsigned");

  const DigitsT SavedRDigits = RDigits;
  const int16_t SavedRScale = RScale;
  matchScales(LDigits, LScale, RDigits, RScale);

  if (LDigits <= RDigits)
    return {0, 0};
  if (RDigits || !SavedRDigits)
    return {DigitsT(LDigits - RDigits), LScale};

  const auto RLgFloor = getLgFloor(SavedRDigits, SavedRScale);
  if (!compare(LDigits, LScale, DigitsT(1), RLgFloor + getWidth<DigitsT>()))
    return {std::numeric_limits<DigitsT>::max(), int16_t(RLgFloor)};

  return {LDigits, LScale};
}

/// Convenience helper for 32-bit difference.
inline ScaledResult<uint32_t> getDifference32(uint32_t LDigits, int16_t LScale,
                                              uint32_t RDigits,
                                              int16_t RScale) {
  return getDifference(LDigits, LScale, RDigits, RScale);
}

/// Convenience helper for 64-bit difference.
inline ScaledResult<uint64_t> getDifference64(uint64_t LDigits, int16_t LScale,
                                              uint64_t RDigits,
                                              int16_t RScale) {
  return getDifference(LDigits, LScale, RDigits, RScale);
}

} // end namespace ScaledNumbers
} // end namespace llvm

namespace llvm {

class raw_ostream;
class ScaledNumberBase {
public:
  static constexpr int DefaultPrecision = 10;

  static void dump(uint64_t D, int16_t E, int Width);
  static raw_ostream &print(raw_ostream &OS, uint64_t D, int16_t E, int Width,
                            unsigned Precision);
  static SmallString<64> toString(uint64_t D, int16_t E, int Width,
                                  unsigned Precision);
  static int countLeadingZeros32(uint32_t N) { return llvm::countl_zero(N); }
  static int countLeadingZeros64(uint64_t N) { return llvm::countl_zero(N); }
  static uint64_t getHalf(uint64_t N) { return (N >> 1) + (N & 1); }

  struct SplitSignedResult {
    uint64_t first;
    bool second;
  };
  static SplitSignedResult splitSigned(int64_t N) {
    if (N >= 0)
      return {uint64_t(N), false};
    uint64_t Unsigned = N == INT64_MIN ? UINT64_C(1) << 63 : uint64_t(-N);
    return {Unsigned, true};
  }
  static int64_t joinSigned(uint64_t U, bool IsNeg) {
    if (U > uint64_t(INT64_MAX))
      return IsNeg ? INT64_MIN : INT64_MAX;
    return IsNeg ? -int64_t(U) : int64_t(U);
  }
};

/// Simple representation of a scaled number.
///
/// ScaledNumber is a number represented by digits and a scale.  It uses simple
/// saturation arithmetic and every operation is well-defined for every value.
/// It's somewhat similar in behaviour to a soft-float, but is *not* a
/// replacement for one.  If you're doing numerics, look at \a APFloat instead.
/// Nevertheless, we've found these semantics useful for modelling certain cost
/// metrics.
///
/// The number is split into a signed scale and unsigned digits.  The number
/// represented is \c getDigits()*2^getScale().  In this way, the digits are
/// much like the mantissa in the x87 long double, but there is no canonical
/// form so the same number can be represented by many bit representations.
///
/// ScaledNumber is templated on the underlying integer type for digits, which
/// is expected to be unsigned.
///
/// Unlike APFloat, ScaledNumber does not model architecture floating point
/// behaviour -- while this might make it a little faster and easier to reason
/// about, it certainly makes it more dangerous for general numerics.
///
/// ScaledNumber is totally ordered.  However, there is no canonical form, so
/// there are multiple representations of most scalars.  E.g.:
///
///     ScaledNumber(8u, 0) == ScaledNumber(4u, 1)
///     ScaledNumber(4u, 1) == ScaledNumber(2u, 2)
///     ScaledNumber(2u, 2) == ScaledNumber(1u, 3)
///
/// ScaledNumber implements most arithmetic operations.  Precision is kept
/// where possible.  Uses simple saturation arithmetic, so that operations
/// saturate to 0.0 or getLargest() rather than under or overflowing.  It has
/// some extra arithmetic for unit inversion.  0.0/0.0 is defined to be 0.0.
/// Any other division by 0.0 is defined to be getLargest().
///
/// As a convenience for modifying the exponent, left and right shifting are
/// both implemented, and both interpret negative shifts as positive shifts in
/// the opposite direction.
///
/// Scales are limited to the range accepted by x87 long double.  This makes
/// it trivial to add functionality to convert to APFloat (this is already
/// relied on for the implementation of printing).
///
/// Possible (and conflicting) future directions:
///
///  1. Turn this into a wrapper around \a APFloat.
///  2. Share the algorithm implementations with \a APFloat.
///  3. Allow \a ScaledNumber to represent a signed number.
template <class DigitsT> class ScaledNumber : ScaledNumberBase {
public:
  static_assert(!std::numeric_limits<DigitsT>::is_signed,
                "only unsigned floats supported");

  typedef DigitsT DigitsType;

private:
  typedef std::numeric_limits<DigitsType> DigitsLimits;

  static constexpr int Width = sizeof(DigitsType) * 8;
  static_assert(Width <= 64, "invalid integer width for digits");

private:
  DigitsType Digits = 0;
  int16_t Scale = 0;

public:
  ScaledNumber() = default;

  constexpr ScaledNumber(DigitsType Digits, int16_t Scale)
      : Digits(Digits), Scale(Scale) {}

private:
  ScaledNumber(const ScaledNumbers::ScaledResult<DigitsT> &X)
      : Digits(X.first), Scale(X.second) {}

public:
  static ScaledNumber getZero() { return ScaledNumber(0, 0); }
  static ScaledNumber getOne() { return ScaledNumber(1, 0); }
  static ScaledNumber getLargest() {
    return ScaledNumber(DigitsLimits::max(), ScaledNumbers::MaxScale);
  }
  static ScaledNumber get(uint64_t N) { return adjustToWidth(N, 0); }
  static ScaledNumber getInverse(uint64_t N) { return get(N).invert(); }
  static ScaledNumber getFraction(DigitsType N, DigitsType D) {
    return getQuotient(N, D);
  }

  int16_t getScale() const { return Scale; }
  DigitsType getDigits() const { return Digits; }

  /// Convert to the given integer type.
  ///
  /// Convert to \c IntT using simple saturating arithmetic, truncating if
  /// necessary.
  template <class IntT> IntT toInt() const;

  bool isZero() const { return !Digits; }
  bool isLargest() const { return *this == getLargest(); }
  bool isOne() const {
    if (Scale > 0 || Scale <= -Width)
      return false;
    return Digits == DigitsType(1) << -Scale;
  }

  /// The log base 2, rounded.
  ///
  /// Get the lg of the scalar.  lg 0 is defined to be INT32_MIN.
  int32_t lg() const { return ScaledNumbers::getLg(Digits, Scale); }

  /// The log base 2, rounded towards INT32_MIN.
  ///
  /// Get the lg floor.  lg 0 is defined to be INT32_MIN.
  int32_t lgFloor() const { return ScaledNumbers::getLgFloor(Digits, Scale); }

  /// The log base 2, rounded towards INT32_MAX.
  ///
  /// Get the lg ceiling.  lg 0 is defined to be INT32_MIN.
  int32_t lgCeiling() const {
    return ScaledNumbers::getLgCeiling(Digits, Scale);
  }

  bool operator==(const ScaledNumber &X) const { return compare(X) == 0; }
  bool operator<(const ScaledNumber &X) const { return compare(X) < 0; }
  bool operator!=(const ScaledNumber &X) const { return compare(X) != 0; }
  bool operator>(const ScaledNumber &X) const { return compare(X) > 0; }
  bool operator<=(const ScaledNumber &X) const { return compare(X) <= 0; }
  bool operator>=(const ScaledNumber &X) const { return compare(X) >= 0; }

  bool operator!() const { return isZero(); }

  /// Convert to a decimal representation in a string.
  ///
  /// Convert to a string.  Uses scientific notation for very large/small
  /// numbers.  Scientific notation is used roughly for numbers outside of the
  /// range 2^-64 through 2^64.
  ///
  /// \c Precision indicates the number of decimal digits of precision to use;
  /// 0 requests the maximum available.
  ///
  /// As a special case to make debugging easier, if the number is small enough
  /// to convert without scientific notation and has more than \c Precision
  /// digits before the decimal place, it's printed accurately to the first
  /// digit past zero.  E.g., assuming 10 digits of precision:
  ///
  ///     98765432198.7654... => 98765432198.8
  ///      8765432198.7654... =>  8765432198.8
  ///       765432198.7654... =>   765432198.8
  ///        65432198.7654... =>    65432198.77
  ///         5432198.7654... =>     5432198.765
  SmallString<64> toString(unsigned Precision = DefaultPrecision) {
    return ScaledNumberBase::toString(Digits, Scale, Width, Precision);
  }

  /// Print a decimal representation.
  ///
  /// Print a string.  See toString for documentation.
  raw_ostream &print(raw_ostream &OS,
                     unsigned Precision = DefaultPrecision) const {
    return ScaledNumberBase::print(OS, Digits, Scale, Width, Precision);
  }
  void dump() const { return ScaledNumberBase::dump(Digits, Scale, Width); }

  ScaledNumber &operator+=(const ScaledNumber &X) {
    auto R = ScaledNumbers::getSum(Digits, Scale, X.Digits, X.Scale);
    Digits = R.first;
    Scale = R.second;
    if (Scale > ScaledNumbers::MaxScale)
      *this = getLargest();
    return *this;
  }
  ScaledNumber &operator-=(const ScaledNumber &X) {
    auto R = ScaledNumbers::getDifference(Digits, Scale, X.Digits, X.Scale);
    Digits = R.first;
    Scale = R.second;
    return *this;
  }
  ScaledNumber &operator*=(const ScaledNumber &X);
  ScaledNumber &operator/=(const ScaledNumber &X);
  ScaledNumber &operator<<=(int16_t Shift) {
    shiftLeft(Shift);
    return *this;
  }
  ScaledNumber &operator>>=(int16_t Shift) {
    shiftRight(Shift);
    return *this;
  }

private:
  void shiftLeft(int32_t Shift);
  void shiftRight(int32_t Shift);

  /// Adjust two floats to have matching exponents.
  ///
  /// Adjust \c this and \c X to have matching exponents.  Returns the new \c X
  /// by value.  Does nothing if \a isZero() for either.
  ///
  /// The value that compares smaller will lose precision, and possibly become
  /// \a isZero().
  ScaledNumber matchScales(ScaledNumber X) {
    ScaledNumbers::matchScales(Digits, Scale, X.Digits, X.Scale);
    return X;
  }

public:
  /// Scale a large number accurately.
  ///
  /// Scale N (multiply it by this).  Uses full precision multiplication, even
  /// if Width is smaller than 64, so information is not lost.
  uint64_t scale(uint64_t N) const;
  uint64_t scaleByInverse(uint64_t N) const {
    // TODO: implement directly, rather than relying on inverse.  Inverse is
    // expensive.
    return inverse().scale(N);
  }
  int64_t scale(int64_t N) const {
    auto Unsigned = splitSigned(N);
    return joinSigned(scale(Unsigned.first), Unsigned.second);
  }
  int64_t scaleByInverse(int64_t N) const {
    auto Unsigned = splitSigned(N);
    return joinSigned(scaleByInverse(Unsigned.first), Unsigned.second);
  }

  int compare(const ScaledNumber &X) const {
    return ScaledNumbers::compare(Digits, Scale, X.Digits, X.Scale);
  }
  int compareTo(uint64_t N) const {
    return ScaledNumbers::compare<uint64_t>(Digits, Scale, N, 0);
  }
  int compareTo(int64_t N) const { return N < 0 ? 1 : compareTo(uint64_t(N)); }

  ScaledNumber &invert() { return *this = ScaledNumber::get(1) / *this; }
  ScaledNumber inverse() const { return ScaledNumber(*this).invert(); }

private:
  static ScaledNumber getProduct(DigitsType LHS, DigitsType RHS) {
    return ScaledNumbers::getProduct(LHS, RHS);
  }
  static ScaledNumber getQuotient(DigitsType Dividend, DigitsType Divisor) {
    return ScaledNumbers::getQuotient(Dividend, Divisor);
  }

  static int countLeadingZerosWidth(DigitsType Digits) {
    if (Width == 64)
      return countLeadingZeros64(Digits);
    if (Width == 32)
      return countLeadingZeros32(Digits);
    return countLeadingZeros32(Digits) + Width - 32;
  }

  /// Adjust a number to width, rounding up if necessary.
  ///
  /// Should only be called for \c Shift close to zero.
  ///
  /// \pre Shift >= MinScale && Shift + 64 <= MaxScale.
  static ScaledNumber adjustToWidth(uint64_t N, int32_t Shift) {
    assert(Shift >= ScaledNumbers::MinScale && "Shift should be close to 0");
    assert(Shift <= ScaledNumbers::MaxScale - 64 &&
           "Shift should be close to 0");
    auto Adjusted = ScaledNumbers::getAdjusted<DigitsT>(N, Shift);
    return Adjusted;
  }

  static ScaledNumber getRounded(ScaledNumber P, bool Round) {
    // Saturate.
    if (P.isLargest())
      return P;

    return ScaledNumbers::getRounded(P.Digits, P.Scale, Round);
  }
};

#define SCALED_NUMBER_BOP(op, base)                                            \
  template <class DigitsT>                                                     \
  ScaledNumber<DigitsT> operator op(const ScaledNumber<DigitsT> &L,            \
                                    const ScaledNumber<DigitsT> &R) {          \
    return ScaledNumber<DigitsT>(L) base R;                                    \
  }
SCALED_NUMBER_BOP(+, +=)
SCALED_NUMBER_BOP(-, -=)
SCALED_NUMBER_BOP(*, *=)
SCALED_NUMBER_BOP(/, /=)
#undef SCALED_NUMBER_BOP

template <class DigitsT>
ScaledNumber<DigitsT> operator<<(const ScaledNumber<DigitsT> &L,
                                 int16_t Shift) {
  return ScaledNumber<DigitsT>(L) <<= Shift;
}

template <class DigitsT>
ScaledNumber<DigitsT> operator>>(const ScaledNumber<DigitsT> &L,
                                 int16_t Shift) {
  return ScaledNumber<DigitsT>(L) >>= Shift;
}

template <class DigitsT>
raw_ostream &operator<<(raw_ostream &OS, const ScaledNumber<DigitsT> &X) {
  return X.print(OS, 10);
}

#define SCALED_NUMBER_COMPARE_TO_TYPE(op, T1, T2)                              \
  template <class DigitsT>                                                     \
  bool operator op(const ScaledNumber<DigitsT> &L, T1 R) {                     \
    return L.compareTo(T2(R)) op 0;                                            \
  }                                                                            \
  template <class DigitsT>                                                     \
  bool operator op(T1 L, const ScaledNumber<DigitsT> &R) {                     \
    return 0 op R.compareTo(T2(L));                                            \
  }
#define SCALED_NUMBER_COMPARE_TO(op)                                           \
  SCALED_NUMBER_COMPARE_TO_TYPE(op, uint64_t, uint64_t)                        \
  SCALED_NUMBER_COMPARE_TO_TYPE(op, uint32_t, uint64_t)                        \
  SCALED_NUMBER_COMPARE_TO_TYPE(op, int64_t, int64_t)                          \
  SCALED_NUMBER_COMPARE_TO_TYPE(op, int32_t, int64_t)
SCALED_NUMBER_COMPARE_TO(<)
SCALED_NUMBER_COMPARE_TO(>)
SCALED_NUMBER_COMPARE_TO(==)
SCALED_NUMBER_COMPARE_TO(!=)
SCALED_NUMBER_COMPARE_TO(<=)
SCALED_NUMBER_COMPARE_TO(>=)
#undef SCALED_NUMBER_COMPARE_TO
#undef SCALED_NUMBER_COMPARE_TO_TYPE

template <class DigitsT>
uint64_t ScaledNumber<DigitsT>::scale(uint64_t N) const {
  if (Width == 64 || N <= DigitsLimits::max())
    return (get(N) * *this).template toInt<uint64_t>();

  // Defer to the 64-bit version.
  return ScaledNumber<uint64_t>(Digits, Scale).scale(N);
}

template <class DigitsT>
template <class IntT>
IntT ScaledNumber<DigitsT>::toInt() const {
  typedef std::numeric_limits<IntT> Limits;
  if (*this < 1)
    return 0;
  if (*this >= Limits::max())
    return Limits::max();

  IntT N = Digits;
  if (Scale > 0) {
    assert(size_t(Scale) < sizeof(IntT) * 8);
    return N << Scale;
  }
  if (Scale < 0) {
    assert(size_t(-Scale) < sizeof(IntT) * 8);
    return N >> -Scale;
  }
  return N;
}

template <class DigitsT>
ScaledNumber<DigitsT> &
ScaledNumber<DigitsT>::operator*=(const ScaledNumber &X) {
  if (isZero())
    return *this;
  if (X.isZero())
    return *this = X;

  // Save the exponents.
  int32_t Scales = int32_t(Scale) + int32_t(X.Scale);

  // Get the raw product.
  *this = getProduct(Digits, X.Digits);

  // Combine with exponents.
  return *this <<= Scales;
}
template <class DigitsT>
ScaledNumber<DigitsT> &
ScaledNumber<DigitsT>::operator/=(const ScaledNumber &X) {
  if (isZero())
    return *this;
  if (X.isZero())
    return *this = getLargest();

  // Save the exponents.
  int32_t Scales = int32_t(Scale) - int32_t(X.Scale);

  // Get the raw quotient.
  *this = getQuotient(Digits, X.Digits);

  // Combine with exponents.
  return *this <<= Scales;
}
template <class DigitsT> void ScaledNumber<DigitsT>::shiftLeft(int32_t Shift) {
  if (!Shift || isZero())
    return;
  assert(Shift != INT32_MIN);
  if (Shift < 0) {
    shiftRight(-Shift);
    return;
  }

  // Shift as much as we can in the exponent.
  int32_t maxShiftUp = ScaledNumbers::MaxScale - Scale;
  int32_t ScaleShift = Shift < maxShiftUp ? Shift : maxShiftUp;
  Scale += ScaleShift;
  if (ScaleShift == Shift)
    return;

  // Check this late, since it's rare.
  if (isLargest())
    return;

  // Shift the digits themselves.
  Shift -= ScaleShift;
  if (Shift > countLeadingZerosWidth(Digits)) {
    // Saturate.
    *this = getLargest();
    return;
  }

  Digits <<= Shift;
}

template <class DigitsT> void ScaledNumber<DigitsT>::shiftRight(int32_t Shift) {
  if (!Shift || isZero())
    return;
  assert(Shift != INT32_MIN);
  if (Shift < 0) {
    shiftLeft(-Shift);
    return;
  }

  // Shift as much as we can in the exponent.
  int32_t maxShiftDown = Scale - ScaledNumbers::MinScale;
  int32_t ScaleShift = Shift < maxShiftDown ? Shift : maxShiftDown;
  Scale -= ScaleShift;
  if (ScaleShift == Shift)
    return;

  // Shift the digits themselves.
  Shift -= ScaleShift;
  if (Shift >= Width) {
    // Saturate.
    *this = getZero();
    return;
  }

  Digits >>= Shift;
}

} // end namespace llvm

// === Inline implementations (moved from cpp_bridge.cpp) ===

namespace llvm {

inline ScaledNumbers::ScaledResult<uint64_t>
ScaledNumbers::multiply64(uint64_t LHS, uint64_t RHS) {
  csupport_scaled_pair_t r = csupport_scaled_multiply64(LHS, RHS);
  return {r.value, r.exponent};
}

inline ScaledNumbers::ScaledResult<uint32_t>
ScaledNumbers::divide32(uint32_t Dividend, uint32_t Divisor) {
  csupport_scaled_pair_t r = csupport_scaled_divide32(Dividend, Divisor);
  return {(uint32_t)(r.value), r.exponent};
}

inline ScaledNumbers::ScaledResult<uint64_t>
ScaledNumbers::divide64(uint64_t Dividend, uint64_t Divisor) {
  csupport_scaled_pair_t r = csupport_scaled_divide64(Dividend, Divisor);
  return {r.value, r.exponent};
}

inline static SmallString<64> toStringAPFloat(uint64_t D, int E,
                                              unsigned Precision) {
  assert(E >= ScaledNumbers::MinScale);
  assert(E <= ScaledNumbers::MaxScale);

  int LeadingZeros = ScaledNumberBase::countLeadingZeros64(D);
  int NewE = BRIDGE_MIN(ScaledNumbers::MaxScale, E + 63 - LeadingZeros);
  int Shift = 63 - (NewE - E);
  assert(Shift <= LeadingZeros);
  assert(Shift == LeadingZeros || NewE == ScaledNumbers::MaxScale);
  assert(Shift >= 0 && Shift < 64 && "undefined behavior");
  D <<= Shift;
  E = NewE;

  unsigned AdjustedE = E + 16383;
  if (!(D >> 63)) {
    assert(E == ScaledNumbers::MaxScale);
    AdjustedE = 0;
  }

  uint64_t RawBits[2] = {D, AdjustedE};
  APFloat Float(APFloat::x87DoubleExtended(), APInt(80, RawBits));
  SmallVector<char, 24> Chars;
  Float.toString(Chars, Precision, 0);
  return SmallString<64>(StringRef(Chars.data(), Chars.size()));
}

inline SmallString<64> ScaledNumberBase::toString(uint64_t D, int16_t E,
                                                  int Width,
                                                  unsigned Precision) {
  if (!D)
    return SmallString<64>("0.0");

  uint64_t Above0 = 0, Below0 = 0, Extra = 0;
  int ExtraShift = 0;
  if (E == 0) {
    Above0 = D;
  } else if (E > 0) {
    if (int Shift = BRIDGE_MIN(int16_t(countLeadingZeros64(D)), E)) {
      D <<= Shift;
      E -= Shift;
      if (!E)
        Above0 = D;
    }
  } else if (E > -64) {
    Above0 = D >> -E;
    Below0 = D << (64 + E);
  } else if (E == -64) {
    Below0 = D;
  } else if (E > -120) {
    Below0 = D >> (-E - 64);
    Extra = D << (128 + E);
    ExtraShift = -64 - E;
  }

  if (!Above0 && !Below0)
    return toStringAPFloat(D, E, Precision);

  char buf[256];
  size_t n = csupport_scaled_format_digits(Above0, Below0, Extra, ExtraShift,
                                           Precision, Width, buf, sizeof(buf));
  return SmallString<64>(StringRef(buf, n));
}

inline raw_ostream &ScaledNumberBase::print(raw_ostream &OS, uint64_t D,
                                            int16_t E, int Width,
                                            unsigned Precision) {
  return OS << toString(D, E, Width, Precision);
}

inline void ScaledNumberBase::dump(uint64_t D, int16_t E, int Width) {
  print(dbgs(), D, E, Width, 0)
      << "[" << Width << ":" << D << "*2^" << E << "]";
}

} // namespace llvm

#endif // LLVM_SUPPORT_SCALEDNUMBER_H
