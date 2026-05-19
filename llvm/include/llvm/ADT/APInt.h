//===-- llvm/ADT/APInt.h - For Arbitrary Precision Integer -----*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a class to represent arbitrary precision
/// integral constant values and operations on them.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_APINT_H
#define LLVM_ADT_APINT_H

#include "csupport/lapint.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <assert.h>
#include <cmath>
#include <limits.h>
#include <optional>
#include <stdlib.h>
#include <string.h>
#include <utility>

namespace llvm {
class FoldingSetNodeID;
class StringRef;
class hash_code;
class raw_ostream;
struct Align;

template <typename T, typename Enable> struct DenseMapInfo;

class APInt;

inline APInt operator-(APInt);

//===----------------------------------------------------------------------===//
//                              APInt Class
//===----------------------------------------------------------------------===//

/// Class for arbitrary precision integers.
///
/// APInt is a functional replacement for common case unsigned integer type like
/// "unsigned", "unsigned long" or "uint64_t", but also allows non-byte-width
/// integer sizes and large integer value types such as 3-bits, 15-bits, or more
/// than 64-bits of precision. APInt provides a variety of arithmetic operators
/// and methods to manipulate integer values of any bit-width. It supports both
/// the typical integer arithmetic and comparison operations as well as bitwise
/// manipulation.
///
/// The class has several invariants worth noting:
///   * All bit, byte, and word positions are zero-based.
///   * Once the bit width is set, it doesn't change except by the Truncate,
///     SignExtend, or ZeroExtend operations.
///   * All binary operators must be on APInt instances of the same bit width.
///     Attempting to use these operators on instances with different bit
///     widths will yield an assertion.
///   * The value is stored canonically as an unsigned value. For operations
///     where it makes a difference, there are both signed and unsigned variants
///     of the operation. For example, sdiv and udiv. However, because the bit
///     widths must be the same, operations such as Mul and Add produce the same
///     results regardless of whether the values are interpreted as signed or
///     not.
///   * In general, the class tries to follow the style of computation that LLVM
///     uses in its IR. This simplifies its use for LLVM.
///   * APInt supports zero-bit-width values, but operations that require bits
///     are not defined on it (e.g. you cannot ask for the sign of a zero-bit
///     integer).  This means that operations like zero extension and logical
///     shifts are defined, but sign extension and ashr is not.  Zero bit values
///     compare and hash equal to themselves, and countLeadingZeros returns 0.
///
class [[nodiscard]] APInt {
public:
  typedef uint64_t WordType;

  /// This enum is used to hold the constants we needed for APInt.
  enum : unsigned {
    /// Byte size of a word.
    APINT_WORD_SIZE = sizeof(WordType),
    /// Bits in a word.
    APINT_BITS_PER_WORD = APINT_WORD_SIZE * CHAR_BIT
  };

  enum class Rounding {
    DOWN,
    TOWARD_ZERO,
    UP,
  };

  static constexpr WordType WORDTYPE_MAX = ~WordType(0);

  /// \name Constructors
  /// @{

  /// Create a new APInt of numBits width, initialized as val.
  ///
  /// If isSigned is true then val is treated as if it were a signed value
  /// (i.e. as an int64_t) and the appropriate sign extension to the bit width
  /// will be done. Otherwise, no sign extension occurs (high order bits beyond
  /// the range of val are zero filled).
  ///
  /// \param numBits the bit width of the constructed APInt
  /// \param val the initial value of the APInt
  /// \param isSigned how to treat signedness of val
  APInt(unsigned numBits, uint64_t val, bool isSigned = false)
      : BitWidth(numBits) {
    if (isSingleWord()) {
      U.VAL = val;
      clearUnusedBits();
    } else {
      initSlowCase(val, isSigned);
    }
  }

  /// Construct an APInt of numBits width, initialized as bigVal[].
  ///
  /// Note that bigVal.size() can be smaller or larger than the corresponding
  /// bit width but any extraneous bits will be dropped.
  ///
  /// \param numBits the bit width of the constructed APInt
  /// \param bigVal a sequence of words to form the initial value of the APInt
  APInt(unsigned numBits, ArrayRef<uint64_t> bigVal);

  /// Equivalent to APInt(numBits, ArrayRef<uint64_t>(bigVal, numWords)), but
  /// deprecated because this constructor is prone to ambiguity with the
  /// APInt(unsigned, uint64_t, bool) constructor.
  ///
  /// If this overload is ever deleted, care should be taken to prevent calls
  /// from being incorrectly captured by the APInt(unsigned, uint64_t, bool)
  /// constructor.
  APInt(unsigned numBits, unsigned numWords, const uint64_t bigVal[]);

  /// Construct an APInt from a string representation.
  ///
  /// This constructor interprets the string \p str in the given radix. The
  /// interpretation stops when the first character that is not suitable for the
  /// radix is encountered, or the end of the string. Acceptable radix values
  /// are 2, 8, 10, 16, and 36. It is an error for the value implied by the
  /// string to require more bits than numBits.
  ///
  /// \param numBits the bit width of the constructed APInt
  /// \param str the string to be interpreted
  /// \param radix the radix to use for the conversion
  APInt(unsigned numBits, ::llvm::StringRef str, uint8_t radix);

  /// Default constructor that creates an APInt with a 1-bit zero value.
  explicit APInt() { U.VAL = 0; }

  /// Copy Constructor.
  APInt(const APInt &that) : BitWidth(that.BitWidth) {
    if (isSingleWord())
      U.VAL = that.U.VAL;
    else
      initSlowCase(that);
  }

  /// Move Constructor.
  APInt(APInt &&that) : BitWidth(that.BitWidth) {
    memcpy(&U, &that.U, sizeof(U));
    that.BitWidth = 0;
  }

  /// Destructor.
  ~APInt() {
    if (needsCleanup())
      delete[] U.pVal;
  }

  /// @}
  /// \name Value Generators
  /// @{

  /// Get the '0' value for the specified bit-width.
  static APInt getZero(unsigned numBits) { return APInt(numBits, 0); }

  /// Return an APInt zero bits wide.
  static APInt getZeroWidth() { return getZero(0); }

  /// Gets maximum unsigned value of APInt for specific bit width.
  static APInt getMaxValue(unsigned numBits) { return getAllOnes(numBits); }

  /// Gets maximum signed value of APInt for a specific bit width.
  static APInt getSignedMaxValue(unsigned numBits) {
    APInt API = getAllOnes(numBits);
    API.clearBit(numBits - 1);
    return API;
  }

  /// Gets minimum unsigned value of APInt for a specific bit width.
  static APInt getMinValue(unsigned numBits) { return APInt(numBits, 0); }

  /// Gets minimum signed value of APInt for a specific bit width.
  static APInt getSignedMinValue(unsigned numBits) {
    APInt API(numBits, 0);
    API.setBit(numBits - 1);
    return API;
  }

  /// Get the SignMask for a specific bit width.
  ///
  /// This is just a wrapper function of getSignedMinValue(), and it helps code
  /// readability when we want to get a SignMask.
  static APInt getSignMask(unsigned BitWidth) {
    return getSignedMinValue(BitWidth);
  }

  /// Return an APInt of a specified width with all bits set.
  static APInt getAllOnes(unsigned numBits) {
    return APInt(numBits, WORDTYPE_MAX, true);
  }

  /// Return an APInt with exactly one bit set in the result.
  static APInt getOneBitSet(unsigned numBits, unsigned BitNo) {
    APInt Res(numBits, 0);
    Res.setBit(BitNo);
    return Res;
  }

  /// Get a value with a block of bits set.
  ///
  /// Constructs an APInt value that has a contiguous range of bits set. The
  /// bits from loBit (inclusive) to hiBit (exclusive) will be set. All other
  /// bits will be zero. For example, with parameters(32, 0, 16) you would get
  /// 0x0000FFFF. Please call getBitsSetWithWrap if \p loBit may be greater than
  /// \p hiBit.
  ///
  /// \param numBits the intended bit width of the result
  /// \param loBit the index of the lowest bit set.
  /// \param hiBit the index of the highest bit set.
  ///
  /// \returns An APInt value with the requested bits set.
  static APInt getBitsSet(unsigned numBits, unsigned loBit, unsigned hiBit) {
    APInt Res(numBits, 0);
    Res.setBits(loBit, hiBit);
    return Res;
  }

  /// Wrap version of getBitsSet.
  /// If \p hiBit is bigger than \p loBit, this is same with getBitsSet.
  /// If \p hiBit is not bigger than \p loBit, the set bits "wrap". For example,
  /// with parameters (32, 28, 4), you would get 0xF000000F.
  /// If \p hiBit is equal to \p loBit, you would get a result with all bits
  /// set.
  static APInt getBitsSetWithWrap(unsigned numBits, unsigned loBit,
                                  unsigned hiBit) {
    APInt Res(numBits, 0);
    Res.setBitsWithWrap(loBit, hiBit);
    return Res;
  }

  /// Constructs an APInt value that has a contiguous range of bits set. The
  /// bits from loBit (inclusive) to numBits (exclusive) will be set. All other
  /// bits will be zero. For example, with parameters(32, 12) you would get
  /// 0xFFFFF000.
  ///
  /// \param numBits the intended bit width of the result
  /// \param loBit the index of the lowest bit to set.
  ///
  /// \returns An APInt value with the requested bits set.
  static APInt getBitsSetFrom(unsigned numBits, unsigned loBit) {
    APInt Res(numBits, 0);
    Res.setBitsFrom(loBit);
    return Res;
  }

  /// Constructs an APInt value that has the top hiBitsSet bits set.
  ///
  /// \param numBits the bitwidth of the result
  /// \param hiBitsSet the number of high-order bits set in the result.
  static APInt getHighBitsSet(unsigned numBits, unsigned hiBitsSet) {
    APInt Res(numBits, 0);
    Res.setHighBits(hiBitsSet);
    return Res;
  }

  /// Constructs an APInt value that has the bottom loBitsSet bits set.
  ///
  /// \param numBits the bitwidth of the result
  /// \param loBitsSet the number of low-order bits set in the result.
  static APInt getLowBitsSet(unsigned numBits, unsigned loBitsSet) {
    APInt Res(numBits, 0);
    Res.setLowBits(loBitsSet);
    return Res;
  }

  /// Return a value containing V broadcasted over NewLen bits.
  static APInt getSplat(unsigned NewLen, const APInt &V);

  /// @}
  /// \name Value Tests
  /// @{

  /// Determine if this APInt just has one word to store value.
  ///
  /// \returns true if the number of bits <= 64, false otherwise.
  bool isSingleWord() const { return BitWidth <= APINT_BITS_PER_WORD; }

  /// Determine sign of this APInt.
  ///
  /// This tests the high bit of this APInt to determine if it is set.
  ///
  /// \returns true if this APInt is negative, false otherwise
  bool isNegative() const { return (*this)[BitWidth - 1]; }

  /// Determine if this APInt Value is non-negative (>= 0)
  ///
  /// This tests the high bit of the APInt to determine if it is unset.
  bool isNonNegative() const { return !isNegative(); }

  /// Determine if sign bit of this APInt is set.
  ///
  /// This tests the high bit of this APInt to determine if it is set.
  ///
  /// \returns true if this APInt has its sign bit set, false otherwise.
  bool isSignBitSet() const { return (*this)[BitWidth - 1]; }

  /// Determine if sign bit of this APInt is clear.
  ///
  /// This tests the high bit of this APInt to determine if it is clear.
  ///
  /// \returns true if this APInt has its sign bit clear, false otherwise.
  bool isSignBitClear() const { return !isSignBitSet(); }

  /// Determine if this APInt Value is positive.
  ///
  /// This tests if the value of this APInt is positive (> 0). Note
  /// that 0 is not a positive value.
  ///
  /// \returns true if this APInt is positive.
  bool isStrictlyPositive() const { return isNonNegative() && !isZero(); }

  /// Determine if this APInt Value is non-positive (<= 0).
  ///
  /// \returns true if this APInt is non-positive.
  bool isNonPositive() const { return !isStrictlyPositive(); }

  /// Determine if this APInt Value only has the specified bit set.
  ///
  /// \returns true if this APInt only has the specified bit set.
  bool isOneBitSet(unsigned BitNo) const {
    return (*this)[BitNo] && popcount() == 1;
  }

  /// Determine if all bits are set.  This is true for zero-width values.
  bool isAllOnes() const {
    if (BitWidth == 0)
      return true;
    if (isSingleWord())
      return U.VAL == WORDTYPE_MAX >> (APINT_BITS_PER_WORD - BitWidth);
    return countTrailingOnesSlowCase() == BitWidth;
  }

  /// Determine if this value is zero, i.e. all bits are clear.
  bool isZero() const {
    if (isSingleWord())
      return U.VAL == 0;
    return countLeadingZerosSlowCase() == BitWidth;
  }

  /// Determine if this is a value of 1.
  ///
  /// This checks to see if the value of this APInt is one.
  bool isOne() const {
    if (isSingleWord())
      return U.VAL == 1;
    return countLeadingZerosSlowCase() == BitWidth - 1;
  }

  /// Determine if this is the largest unsigned value.
  ///
  /// This checks to see if the value of this APInt is the maximum unsigned
  /// value for the APInt's bit width.
  bool isMaxValue() const { return isAllOnes(); }

  /// Determine if this is the largest signed value.
  ///
  /// This checks to see if the value of this APInt is the maximum signed
  /// value for the APInt's bit width.
  bool isMaxSignedValue() const {
    if (isSingleWord()) {
      assert(BitWidth && "zero width values not allowed");
      return U.VAL == ((WordType(1) << (BitWidth - 1)) - 1);
    }
    return !isNegative() && countTrailingOnesSlowCase() == BitWidth - 1;
  }

  /// Determine if this is the smallest unsigned value.
  ///
  /// This checks to see if the value of this APInt is the minimum unsigned
  /// value for the APInt's bit width.
  bool isMinValue() const { return isZero(); }

  /// Determine if this is the smallest signed value.
  ///
  /// This checks to see if the value of this APInt is the minimum signed
  /// value for the APInt's bit width.
  bool isMinSignedValue() const {
    if (isSingleWord()) {
      assert(BitWidth && "zero width values not allowed");
      return U.VAL == (WordType(1) << (BitWidth - 1));
    }
    return isNegative() && countTrailingZerosSlowCase() == BitWidth - 1;
  }

  /// Check if this APInt has an N-bits unsigned integer value.
  bool isIntN(unsigned N) const { return getActiveBits() <= N; }

  /// Check if this APInt has an N-bits signed integer value.
  bool isSignedIntN(unsigned N) const { return getSignificantBits() <= N; }

  /// Check if this APInt's value is a power of two greater than zero.
  ///
  /// \returns true if the argument APInt value is a power of two > 0.
  bool isPowerOf2() const {
    if (isSingleWord()) {
      assert(BitWidth && "zero width values not allowed");
      return isPowerOf2_64(U.VAL);
    }
    return countPopulationSlowCase() == 1;
  }

  /// Check if this APInt's negated value is a power of two greater than zero.
  bool isNegatedPowerOf2() const {
    assert(BitWidth && "zero width values not allowed");
    if (isNonNegative())
      return false;
    // NegatedPowerOf2 - shifted mask in the top bits.
    unsigned LO = countl_one();
    unsigned TZ = countr_zero();
    return (LO + TZ) == BitWidth;
  }

  /// Checks if this APInt -interpreted as an address- is aligned to the
  /// provided value.
  bool isAligned(Align A) const;

  /// Check if the APInt's value is returned by getSignMask.
  ///
  /// \returns true if this is the value returned by getSignMask.
  bool isSignMask() const { return isMinSignedValue(); }

  /// Convert APInt to a boolean value.
  ///
  /// This converts the APInt to a boolean value as a test against zero.
  bool getBoolValue() const { return !isZero(); }

  /// If this value is smaller than the specified limit, return it, otherwise
  /// return the limit value.  This causes the value to saturate to the limit.
  uint64_t getLimitedValue(uint64_t Limit = UINT64_MAX) const {
    return ugt(Limit) ? Limit : getZExtValue();
  }

  /// Check if the APInt consists of a repeated bit pattern.
  ///
  /// e.g. 0x01010101 satisfies isSplat(8).
  /// \param SplatSizeInBits The size of the pattern in bits. Must divide bit
  /// width without remainder.
  bool isSplat(unsigned SplatSizeInBits) const;

  /// \returns true if this APInt value is a sequence of \param numBits ones
  /// starting at the least significant bit with the remainder zero.
  bool isMask(unsigned numBits) const {
    assert(numBits != 0 && "numBits must be non-zero");
    assert(numBits <= BitWidth && "numBits out of range");
    if (isSingleWord())
      return U.VAL == (WORDTYPE_MAX >> (APINT_BITS_PER_WORD - numBits));
    unsigned Ones = countTrailingOnesSlowCase();
    return (numBits == Ones) &&
           ((Ones + countLeadingZerosSlowCase()) == BitWidth);
  }

  /// \returns true if this APInt is a non-empty sequence of ones starting at
  /// the least significant bit with the remainder zero.
  /// Ex. isMask(0x0000FFFFU) == true.
  bool isMask() const {
    if (isSingleWord())
      return isMask_64(U.VAL);
    unsigned Ones = countTrailingOnesSlowCase();
    return (Ones > 0) && ((Ones + countLeadingZerosSlowCase()) == BitWidth);
  }

  /// Return true if this APInt value contains a non-empty sequence of ones with
  /// the remainder zero.
  bool isShiftedMask() const {
    if (isSingleWord())
      return isShiftedMask_64(U.VAL);
    unsigned Ones = countPopulationSlowCase();
    unsigned LeadZ = countLeadingZerosSlowCase();
    return (Ones + LeadZ + countr_zero()) == BitWidth;
  }

  /// Return true if this APInt value contains a non-empty sequence of ones with
  /// the remainder zero. If true, \p MaskIdx will specify the index of the
  /// lowest set bit and \p MaskLen is updated to specify the length of the
  /// mask, else neither are updated.
  bool isShiftedMask(unsigned &MaskIdx, unsigned &MaskLen) const {
    if (isSingleWord())
      return isShiftedMask_64(U.VAL, MaskIdx, MaskLen);
    unsigned Ones = countPopulationSlowCase();
    unsigned LeadZ = countLeadingZerosSlowCase();
    unsigned TrailZ = countTrailingZerosSlowCase();
    if ((Ones + LeadZ + TrailZ) != BitWidth)
      return false;
    MaskLen = Ones;
    MaskIdx = TrailZ;
    return true;
  }

  /// Compute an APInt containing numBits highbits from this APInt.
  ///
  /// Get an APInt with the same BitWidth as this APInt, just zero mask the low
  /// bits and right shift to the least significant bit.
  ///
  /// \returns the high "numBits" bits of this APInt.
  APInt getHiBits(unsigned numBits) const;

  /// Compute an APInt containing numBits lowbits from this APInt.
  ///
  /// Get an APInt with the same BitWidth as this APInt, just zero mask the high
  /// bits.
  ///
  /// \returns the low "numBits" bits of this APInt.
  APInt getLoBits(unsigned numBits) const;

  /// Determine if two APInts have the same value, after zero-extending
  /// one of them (if needed!) to ensure that the bit-widths match.
  static bool isSameValue(const APInt &I1, const APInt &I2) {
    if (I1.getBitWidth() == I2.getBitWidth())
      return I1 == I2;

    if (I1.getBitWidth() > I2.getBitWidth())
      return I1 == I2.zext(I1.getBitWidth());

    return I1.zext(I2.getBitWidth()) == I2;
  }

  /// Overload to compute a hash_code for an APInt value.
  friend hash_code hash_value(const APInt &Arg);

  /// This function returns a pointer to the internal storage of the APInt.
  /// This is useful for writing out the APInt in binary form without any
  /// conversions.
  const uint64_t *getRawData() const {
    if (isSingleWord())
      return &U.VAL;
    return &U.pVal[0];
  }

  /// @}
  /// \name Unary Operators
  /// @{

  /// Postfix increment operator.  Increment *this by 1.
  ///
  /// \returns a new APInt value representing the original value of *this.
  APInt operator++(int) {
    APInt API(*this);
    ++(*this);
    return API;
  }

  /// Prefix increment operator.
  ///
  /// \returns *this incremented by one
  APInt &operator++();

  /// Postfix decrement operator. Decrement *this by 1.
  ///
  /// \returns a new APInt value representing the original value of *this.
  APInt operator--(int) {
    APInt API(*this);
    --(*this);
    return API;
  }

  /// Prefix decrement operator.
  ///
  /// \returns *this decremented by one.
  APInt &operator--();

  /// Logical negation operation on this APInt returns true if zero, like normal
  /// integers.
  bool operator!() const { return isZero(); }

  /// @}
  /// \name Assignment Operators
  /// @{

  /// Copy assignment operator.
  ///
  /// \returns *this after assignment of RHS.
  APInt &operator=(const APInt &RHS) {
    // The common case (both source or dest being inline) doesn't require
    // allocation or deallocation.
    if (isSingleWord() && RHS.isSingleWord()) {
      U.VAL = RHS.U.VAL;
      BitWidth = RHS.BitWidth;
      return *this;
    }

    assignSlowCase(RHS);
    return *this;
  }

  /// Move assignment operator.
  APInt &operator=(APInt &&that) {
#ifdef EXPENSIVE_CHECKS
    // Some std::shuffle implementations still do self-assignment.
    if (this == &that)
      return *this;
#endif
    assert(this != &that && "Self-move not supported");
    if (!isSingleWord())
      delete[] U.pVal;

    // Use memcpy so that type based alias analysis sees both VAL and pVal
    // as modified.
    memcpy(&U, &that.U, sizeof(U));

    BitWidth = that.BitWidth;
    that.BitWidth = 0;
    return *this;
  }

  /// Assignment operator.
  ///
  /// The RHS value is assigned to *this. If the significant bits in RHS exceed
  /// the bit width, the excess bits are truncated. If the bit width is larger
  /// than 64, the value is zero filled in the unspecified high order bits.
  ///
  /// \returns *this after assignment of RHS value.
  APInt &operator=(uint64_t RHS) {
    if (isSingleWord()) {
      U.VAL = RHS;
      return clearUnusedBits();
    }
    U.pVal[0] = RHS;
    memset(U.pVal + 1, 0, (getNumWords() - 1) * APINT_WORD_SIZE);
    return *this;
  }

  /// Bitwise AND assignment operator.
  ///
  /// Performs a bitwise AND operation on this APInt and RHS. The result is
  /// assigned to *this.
  ///
  /// \returns *this after ANDing with RHS.
  APInt &operator&=(const APInt &RHS) {
    assert(BitWidth == RHS.BitWidth && "Bit widths must be the same");
    if (isSingleWord())
      U.VAL &= RHS.U.VAL;
    else
      andAssignSlowCase(RHS);
    return *this;
  }

  /// Bitwise AND assignment operator.
  ///
  /// Performs a bitwise AND operation on this APInt and RHS. RHS is
  /// logically zero-extended or truncated to match the bit-width of
  /// the LHS.
  APInt &operator&=(uint64_t RHS) {
    if (isSingleWord()) {
      U.VAL &= RHS;
      return *this;
    }
    U.pVal[0] &= RHS;
    memset(U.pVal + 1, 0, (getNumWords() - 1) * APINT_WORD_SIZE);
    return *this;
  }

  /// Bitwise OR assignment operator.
  ///
  /// Performs a bitwise OR operation on this APInt and RHS. The result is
  /// assigned *this;
  ///
  /// \returns *this after ORing with RHS.
  APInt &operator|=(const APInt &RHS) {
    assert(BitWidth == RHS.BitWidth && "Bit widths must be the same");
    if (isSingleWord())
      U.VAL |= RHS.U.VAL;
    else
      orAssignSlowCase(RHS);
    return *this;
  }

  /// Bitwise OR assignment operator.
  ///
  /// Performs a bitwise OR operation on this APInt and RHS. RHS is
  /// logically zero-extended or truncated to match the bit-width of
  /// the LHS.
  APInt &operator|=(uint64_t RHS) {
    if (isSingleWord()) {
      U.VAL |= RHS;
      return clearUnusedBits();
    }
    U.pVal[0] |= RHS;
    return *this;
  }

  /// Bitwise XOR assignment operator.
  ///
  /// Performs a bitwise XOR operation on this APInt and RHS. The result is
  /// assigned to *this.
  ///
  /// \returns *this after XORing with RHS.
  APInt &operator^=(const APInt &RHS) {
    assert(BitWidth == RHS.BitWidth && "Bit widths must be the same");
    if (isSingleWord())
      U.VAL ^= RHS.U.VAL;
    else
      xorAssignSlowCase(RHS);
    return *this;
  }

  /// Bitwise XOR assignment operator.
  ///
  /// Performs a bitwise XOR operation on this APInt and RHS. RHS is
  /// logically zero-extended or truncated to match the bit-width of
  /// the LHS.
  APInt &operator^=(uint64_t RHS) {
    if (isSingleWord()) {
      U.VAL ^= RHS;
      return clearUnusedBits();
    }
    U.pVal[0] ^= RHS;
    return *this;
  }

  /// Multiplication assignment operator.
  ///
  /// Multiplies this APInt by RHS and assigns the result to *this.
  ///
  /// \returns *this
  APInt &operator*=(const APInt &RHS);
  APInt &operator*=(uint64_t RHS);

  /// Addition assignment operator.
  ///
  /// Adds RHS to *this and assigns the result to *this.
  ///
  /// \returns *this
  APInt &operator+=(const APInt &RHS);
  APInt &operator+=(uint64_t RHS);

  /// Subtraction assignment operator.
  ///
  /// Subtracts RHS from *this and assigns the result to *this.
  ///
  /// \returns *this
  APInt &operator-=(const APInt &RHS);
  APInt &operator-=(uint64_t RHS);

  /// Left-shift assignment function.
  ///
  /// Shifts *this left by shiftAmt and assigns the result to *this.
  ///
  /// \returns *this after shifting left by ShiftAmt
  APInt &operator<<=(unsigned ShiftAmt) {
    assert(ShiftAmt <= BitWidth && "Invalid shift amount");
    if (isSingleWord()) {
      if (ShiftAmt == BitWidth)
        U.VAL = 0;
      else
        U.VAL <<= ShiftAmt;
      return clearUnusedBits();
    }
    shlSlowCase(ShiftAmt);
    return *this;
  }

  /// Left-shift assignment function.
  ///
  /// Shifts *this left by shiftAmt and assigns the result to *this.
  ///
  /// \returns *this after shifting left by ShiftAmt
  APInt &operator<<=(const APInt &ShiftAmt);

  /// @}
  /// \name Binary Operators
  /// @{

  /// Multiplication operator.
  ///
  /// Multiplies this APInt by RHS and returns the result.
  APInt operator*(const APInt &RHS) const;

  /// Left logical shift operator.
  ///
  /// Shifts this APInt left by \p Bits and returns the result.
  APInt operator<<(unsigned Bits) const { return shl(Bits); }

  /// Left logical shift operator.
  ///
  /// Shifts this APInt left by \p Bits and returns the result.
  APInt operator<<(const APInt &Bits) const { return shl(Bits); }

  /// Arithmetic right-shift function.
  ///
  /// Arithmetic right-shift this APInt by shiftAmt.
  APInt ashr(unsigned ShiftAmt) const {
    APInt R(*this);
    R.ashrInPlace(ShiftAmt);
    return R;
  }

  /// Arithmetic right-shift this APInt by ShiftAmt in place.
  void ashrInPlace(unsigned ShiftAmt) {
    assert(ShiftAmt <= BitWidth && "Invalid shift amount");
    if (isSingleWord()) {
      int64_t SExtVAL = SignExtend64(U.VAL, BitWidth);
      if (ShiftAmt == BitWidth)
        U.VAL = SExtVAL >> (APINT_BITS_PER_WORD - 1); // Fill with sign bit.
      else
        U.VAL = SExtVAL >> ShiftAmt;
      clearUnusedBits();
      return;
    }
    ashrSlowCase(ShiftAmt);
  }

  /// Logical right-shift function.
  ///
  /// Logical right-shift this APInt by shiftAmt.
  APInt lshr(unsigned shiftAmt) const {
    APInt R(*this);
    R.lshrInPlace(shiftAmt);
    return R;
  }

  /// Logical right-shift this APInt by ShiftAmt in place.
  void lshrInPlace(unsigned ShiftAmt) {
    assert(ShiftAmt <= BitWidth && "Invalid shift amount");
    if (isSingleWord()) {
      if (ShiftAmt == BitWidth)
        U.VAL = 0;
      else
        U.VAL >>= ShiftAmt;
      return;
    }
    lshrSlowCase(ShiftAmt);
  }

  /// Left-shift function.
  ///
  /// Left-shift this APInt by shiftAmt.
  APInt shl(unsigned shiftAmt) const {
    APInt R(*this);
    R <<= shiftAmt;
    return R;
  }

  /// relative logical shift right
  APInt relativeLShr(int RelativeShift) const {
    return RelativeShift > 0 ? lshr(RelativeShift) : shl(-RelativeShift);
  }

  /// relative logical shift left
  APInt relativeLShl(int RelativeShift) const {
    return relativeLShr(-RelativeShift);
  }

  /// relative arithmetic shift right
  APInt relativeAShr(int RelativeShift) const {
    return RelativeShift > 0 ? ashr(RelativeShift) : shl(-RelativeShift);
  }

  /// relative arithmetic shift left
  APInt relativeAShl(int RelativeShift) const {
    return relativeAShr(-RelativeShift);
  }

  /// Rotate left by rotateAmt.
  APInt rotl(unsigned rotateAmt) const;

  /// Rotate right by rotateAmt.
  APInt rotr(unsigned rotateAmt) const;

  /// Arithmetic right-shift function.
  ///
  /// Arithmetic right-shift this APInt by shiftAmt.
  APInt ashr(const APInt &ShiftAmt) const {
    APInt R(*this);
    R.ashrInPlace(ShiftAmt);
    return R;
  }

  /// Arithmetic right-shift this APInt by shiftAmt in place.
  void ashrInPlace(const APInt &shiftAmt);

  /// Logical right-shift function.
  ///
  /// Logical right-shift this APInt by shiftAmt.
  APInt lshr(const APInt &ShiftAmt) const {
    APInt R(*this);
    R.lshrInPlace(ShiftAmt);
    return R;
  }

  /// Logical right-shift this APInt by ShiftAmt in place.
  void lshrInPlace(const APInt &ShiftAmt);

  /// Left-shift function.
  ///
  /// Left-shift this APInt by shiftAmt.
  APInt shl(const APInt &ShiftAmt) const {
    APInt R(*this);
    R <<= ShiftAmt;
    return R;
  }

  /// Rotate left by rotateAmt.
  APInt rotl(const APInt &rotateAmt) const;

  /// Rotate right by rotateAmt.
  APInt rotr(const APInt &rotateAmt) const;

  /// Concatenate the bits from "NewLSB" onto the bottom of *this.  This is
  /// equivalent to:
  ///   (this->zext(NewWidth) << NewLSB.getBitWidth()) | NewLSB.zext(NewWidth)
  APInt concat(const APInt &NewLSB) const {
    /// If the result will be small, then both the merged values are small.
    unsigned NewWidth = getBitWidth() + NewLSB.getBitWidth();
    if (NewWidth <= APINT_BITS_PER_WORD)
      return APInt(NewWidth, (U.VAL << NewLSB.getBitWidth()) | NewLSB.U.VAL);
    return concatSlowCase(NewLSB);
  }

  /// Unsigned division operation.
  ///
  /// Perform an unsigned divide operation on this APInt by RHS. Both this and
  /// RHS are treated as unsigned quantities for purposes of this division.
  ///
  /// \returns a new APInt value containing the division result, rounded towards
  /// zero.
  APInt udiv(const APInt &RHS) const;
  APInt udiv(uint64_t RHS) const;

  /// Signed division function for APInt.
  ///
  /// Signed divide this APInt by APInt RHS.
  ///
  /// The result is rounded towards zero.
  APInt sdiv(const APInt &RHS) const;
  APInt sdiv(int64_t RHS) const;

  /// Unsigned remainder operation.
  ///
  /// Perform an unsigned remainder operation on this APInt with RHS being the
  /// divisor. Both this and RHS are treated as unsigned quantities for purposes
  /// of this operation.
  ///
  /// \returns a new APInt value containing the remainder result
  APInt urem(const APInt &RHS) const;
  uint64_t urem(uint64_t RHS) const;

  /// Function for signed remainder operation.
  ///
  /// Signed remainder operation on APInt.
  ///
  /// Note that this is a true remainder operation and not a modulo operation
  /// because the sign follows the sign of the dividend which is *this.
  APInt srem(const APInt &RHS) const;
  int64_t srem(int64_t RHS) const;

  /// Dual division/remainder interface.
  ///
  /// Sometimes it is convenient to divide two APInt values and obtain both the
  /// quotient and remainder. This function does both operations in the same
  /// computation making it a little more efficient. The pair of input arguments
  /// may overlap with the pair of output arguments. It is safe to call
  /// udivrem(X, Y, X, Y), for example.
  static void udivrem(const APInt &LHS, const APInt &RHS, APInt &Quotient,
                      APInt &Remainder);
  static void udivrem(const APInt &LHS, uint64_t RHS, APInt &Quotient,
                      uint64_t &Remainder);

  static void sdivrem(const APInt &LHS, const APInt &RHS, APInt &Quotient,
                      APInt &Remainder);
  static void sdivrem(const APInt &LHS, int64_t RHS, APInt &Quotient,
                      int64_t &Remainder);

  // Operations that return overflow indicators.
  APInt sadd_ov(const APInt &RHS, bool &Overflow) const;
  APInt uadd_ov(const APInt &RHS, bool &Overflow) const;
  APInt ssub_ov(const APInt &RHS, bool &Overflow) const;
  APInt usub_ov(const APInt &RHS, bool &Overflow) const;
  APInt sdiv_ov(const APInt &RHS, bool &Overflow) const;
  APInt smul_ov(const APInt &RHS, bool &Overflow) const;
  APInt umul_ov(const APInt &RHS, bool &Overflow) const;
  APInt sshl_ov(const APInt &Amt, bool &Overflow) const;
  APInt sshl_ov(unsigned Amt, bool &Overflow) const;
  APInt ushl_ov(const APInt &Amt, bool &Overflow) const;
  APInt ushl_ov(unsigned Amt, bool &Overflow) const;

  // Operations that saturate
  APInt sadd_sat(const APInt &RHS) const;
  APInt uadd_sat(const APInt &RHS) const;
  APInt ssub_sat(const APInt &RHS) const;
  APInt usub_sat(const APInt &RHS) const;
  APInt smul_sat(const APInt &RHS) const;
  APInt umul_sat(const APInt &RHS) const;
  APInt sshl_sat(const APInt &RHS) const;
  APInt sshl_sat(unsigned RHS) const;
  APInt ushl_sat(const APInt &RHS) const;
  APInt ushl_sat(unsigned RHS) const;

  /// Array-indexing support.
  ///
  /// \returns the bit value at bitPosition
  bool operator[](unsigned bitPosition) const {
    assert(bitPosition < getBitWidth() && "Bit position out of bounds!");
    return (maskBit(bitPosition) & getWord(bitPosition)) != 0;
  }

  /// @}
  /// \name Comparison Operators
  /// @{

  /// Equality operator.
  ///
  /// Compares this APInt with RHS for the validity of the equality
  /// relationship.
  bool operator==(const APInt &RHS) const {
    assert(BitWidth == RHS.BitWidth && "Comparison requires equal bit widths");
    if (isSingleWord())
      return U.VAL == RHS.U.VAL;
    return equalSlowCase(RHS);
  }

  /// Equality operator.
  ///
  /// Compares this APInt with a uint64_t for the validity of the equality
  /// relationship.
  ///
  /// \returns true if *this == Val
  bool operator==(uint64_t Val) const {
    return (isSingleWord() || getActiveBits() <= 64) && getZExtValue() == Val;
  }

  /// Equality comparison.
  ///
  /// Compares this APInt with RHS for the validity of the equality
  /// relationship.
  ///
  /// \returns true if *this == Val
  bool eq(const APInt &RHS) const { return (*this) == RHS; }

  /// Inequality operator.
  ///
  /// Compares this APInt with RHS for the validity of the inequality
  /// relationship.
  ///
  /// \returns true if *this != Val
  bool operator!=(const APInt &RHS) const { return !((*this) == RHS); }

  /// Inequality operator.
  ///
  /// Compares this APInt with a uint64_t for the validity of the inequality
  /// relationship.
  ///
  /// \returns true if *this != Val
  bool operator!=(uint64_t Val) const { return !((*this) == Val); }

  /// Inequality comparison
  ///
  /// Compares this APInt with RHS for the validity of the inequality
  /// relationship.
  ///
  /// \returns true if *this != Val
  bool ne(const APInt &RHS) const { return !((*this) == RHS); }

  /// Unsigned less than comparison
  ///
  /// Regards both *this and RHS as unsigned quantities and compares them for
  /// the validity of the less-than relationship.
  ///
  /// \returns true if *this < RHS when both are considered unsigned.
  bool ult(const APInt &RHS) const { return compare(RHS) < 0; }

  /// Unsigned less than comparison
  ///
  /// Regards both *this as an unsigned quantity and compares it with RHS for
  /// the validity of the less-than relationship.
  ///
  /// \returns true if *this < RHS when considered unsigned.
  bool ult(uint64_t RHS) const {
    // Only need to check active bits if not a single word.
    return (isSingleWord() || getActiveBits() <= 64) && getZExtValue() < RHS;
  }

  /// Signed less than comparison
  ///
  /// Regards both *this and RHS as signed quantities and compares them for
  /// validity of the less-than relationship.
  ///
  /// \returns true if *this < RHS when both are considered signed.
  bool slt(const APInt &RHS) const { return compareSigned(RHS) < 0; }

  /// Signed less than comparison
  ///
  /// Regards both *this as a signed quantity and compares it with RHS for
  /// the validity of the less-than relationship.
  ///
  /// \returns true if *this < RHS when considered signed.
  bool slt(int64_t RHS) const {
    return (!isSingleWord() && getSignificantBits() > 64)
               ? isNegative()
               : getSExtValue() < RHS;
  }

  /// Unsigned less or equal comparison
  ///
  /// Regards both *this and RHS as unsigned quantities and compares them for
  /// validity of the less-or-equal relationship.
  ///
  /// \returns true if *this <= RHS when both are considered unsigned.
  bool ule(const APInt &RHS) const { return compare(RHS) <= 0; }

  /// Unsigned less or equal comparison
  ///
  /// Regards both *this as an unsigned quantity and compares it with RHS for
  /// the validity of the less-or-equal relationship.
  ///
  /// \returns true if *this <= RHS when considered unsigned.
  bool ule(uint64_t RHS) const { return !ugt(RHS); }

  /// Signed less or equal comparison
  ///
  /// Regards both *this and RHS as signed quantities and compares them for
  /// validity of the less-or-equal relationship.
  ///
  /// \returns true if *this <= RHS when both are considered signed.
  bool sle(const APInt &RHS) const { return compareSigned(RHS) <= 0; }

  /// Signed less or equal comparison
  ///
  /// Regards both *this as a signed quantity and compares it with RHS for the
  /// validity of the less-or-equal relationship.
  ///
  /// \returns true if *this <= RHS when considered signed.
  bool sle(uint64_t RHS) const { return !sgt(RHS); }

  /// Unsigned greater than comparison
  ///
  /// Regards both *this and RHS as unsigned quantities and compares them for
  /// the validity of the greater-than relationship.
  ///
  /// \returns true if *this > RHS when both are considered unsigned.
  bool ugt(const APInt &RHS) const { return !ule(RHS); }

  /// Unsigned greater than comparison
  ///
  /// Regards both *this as an unsigned quantity and compares it with RHS for
  /// the validity of the greater-than relationship.
  ///
  /// \returns true if *this > RHS when considered unsigned.
  bool ugt(uint64_t RHS) const {
    // Only need to check active bits if not a single word.
    return (!isSingleWord() && getActiveBits() > 64) || getZExtValue() > RHS;
  }

  /// Signed greater than comparison
  ///
  /// Regards both *this and RHS as signed quantities and compares them for the
  /// validity of the greater-than relationship.
  ///
  /// \returns true if *this > RHS when both are considered signed.
  bool sgt(const APInt &RHS) const { return !sle(RHS); }

  /// Signed greater than comparison
  ///
  /// Regards both *this as a signed quantity and compares it with RHS for
  /// the validity of the greater-than relationship.
  ///
  /// \returns true if *this > RHS when considered signed.
  bool sgt(int64_t RHS) const {
    return (!isSingleWord() && getSignificantBits() > 64)
               ? !isNegative()
               : getSExtValue() > RHS;
  }

  /// Unsigned greater or equal comparison
  ///
  /// Regards both *this and RHS as unsigned quantities and compares them for
  /// validity of the greater-or-equal relationship.
  ///
  /// \returns true if *this >= RHS when both are considered unsigned.
  bool uge(const APInt &RHS) const { return !ult(RHS); }

  /// Unsigned greater or equal comparison
  ///
  /// Regards both *this as an unsigned quantity and compares it with RHS for
  /// the validity of the greater-or-equal relationship.
  ///
  /// \returns true if *this >= RHS when considered unsigned.
  bool uge(uint64_t RHS) const { return !ult(RHS); }

  /// Signed greater or equal comparison
  ///
  /// Regards both *this and RHS as signed quantities and compares them for
  /// validity of the greater-or-equal relationship.
  ///
  /// \returns true if *this >= RHS when both are considered signed.
  bool sge(const APInt &RHS) const { return !slt(RHS); }

  /// Signed greater or equal comparison
  ///
  /// Regards both *this as a signed quantity and compares it with RHS for
  /// the validity of the greater-or-equal relationship.
  ///
  /// \returns true if *this >= RHS when considered signed.
  bool sge(int64_t RHS) const { return !slt(RHS); }

  /// This operation tests if there are any pairs of corresponding bits
  /// between this APInt and RHS that are both set.
  bool intersects(const APInt &RHS) const {
    assert(BitWidth == RHS.BitWidth && "Bit widths must be the same");
    if (isSingleWord())
      return (U.VAL & RHS.U.VAL) != 0;
    return intersectsSlowCase(RHS);
  }

  /// This operation checks that all bits set in this APInt are also set in RHS.
  bool isSubsetOf(const APInt &RHS) const {
    assert(BitWidth == RHS.BitWidth && "Bit widths must be the same");
    if (isSingleWord())
      return (U.VAL & ~RHS.U.VAL) == 0;
    return isSubsetOfSlowCase(RHS);
  }

  /// @}
  /// \name Resizing Operators
  /// @{

  /// Truncate to new width.
  ///
  /// Truncate the APInt to a specified width. It is an error to specify a width
  /// that is greater than the current width.
  APInt trunc(unsigned width) const;

  /// Truncate to new width with unsigned saturation.
  ///
  /// If the APInt, treated as unsigned integer, can be losslessly truncated to
  /// the new bitwidth, then return truncated APInt. Else, return max value.
  APInt truncUSat(unsigned width) const;

  /// Truncate to new width with signed saturation.
  ///
  /// If this APInt, treated as signed integer, can be losslessly truncated to
  /// the new bitwidth, then return truncated APInt. Else, return either
  /// signed min value if the APInt was negative, or signed max value.
  APInt truncSSat(unsigned width) const;

  /// Sign extend to a new width.
  ///
  /// This operation sign extends the APInt to a new width. If the high order
  /// bit is set, the fill on the left will be done with 1 bits, otherwise zero.
  /// It is an error to specify a width that is less than the
  /// current width.
  APInt sext(unsigned width) const;

  /// Zero extend to a new width.
  ///
  /// This operation zero extends the APInt to a new width. The high order bits
  /// are filled with 0 bits.  It is an error to specify a width that is less
  /// than the current width.
  APInt zext(unsigned width) const;

  /// Sign extend or truncate to width
  ///
  /// Make this APInt have the bit width given by \p width. The value is sign
  /// extended, truncated, or left alone to make it that width.
  APInt sextOrTrunc(unsigned width) const;

  /// Zero extend or truncate to width
  ///
  /// Make this APInt have the bit width given by \p width. The value is zero
  /// extended, truncated, or left alone to make it that width.
  APInt zextOrTrunc(unsigned width) const;

  /// @}
  /// \name Bit Manipulation Operators
  /// @{

  /// Set every bit to 1.
  void setAllBits() {
    if (isSingleWord())
      U.VAL = WORDTYPE_MAX;
    else
      // Set all the bits in all the words.
      memset(U.pVal, -1, getNumWords() * APINT_WORD_SIZE);
    // Clear the unused ones
    clearUnusedBits();
  }

  /// Set the given bit to 1 whose position is given as "bitPosition".
  void setBit(unsigned BitPosition) {
    assert(BitPosition < BitWidth && "BitPosition out of range");
    WordType Mask = maskBit(BitPosition);
    if (isSingleWord())
      U.VAL |= Mask;
    else
      U.pVal[whichWord(BitPosition)] |= Mask;
  }

  /// Set the sign bit to 1.
  void setSignBit() { setBit(BitWidth - 1); }

  /// Set a given bit to a given value.
  void setBitVal(unsigned BitPosition, bool BitValue) {
    if (BitValue)
      setBit(BitPosition);
    else
      clearBit(BitPosition);
  }

  /// Set the bits from loBit (inclusive) to hiBit (exclusive) to 1.
  /// This function handles "wrap" case when \p loBit >= \p hiBit, and calls
  /// setBits when \p loBit < \p hiBit.
  /// For \p loBit == \p hiBit wrap case, set every bit to 1.
  void setBitsWithWrap(unsigned loBit, unsigned hiBit) {
    assert(hiBit <= BitWidth && "hiBit out of range");
    assert(loBit <= BitWidth && "loBit out of range");
    if (loBit < hiBit) {
      setBits(loBit, hiBit);
      return;
    }
    setLowBits(hiBit);
    setHighBits(BitWidth - loBit);
  }

  /// Set the bits from loBit (inclusive) to hiBit (exclusive) to 1.
  /// This function handles case when \p loBit <= \p hiBit.
  void setBits(unsigned loBit, unsigned hiBit) {
    assert(hiBit <= BitWidth && "hiBit out of range");
    assert(loBit <= BitWidth && "loBit out of range");
    assert(loBit <= hiBit && "loBit greater than hiBit");
    if (loBit == hiBit)
      return;
    if (loBit < APINT_BITS_PER_WORD && hiBit <= APINT_BITS_PER_WORD) {
      uint64_t mask = WORDTYPE_MAX >> (APINT_BITS_PER_WORD - (hiBit - loBit));
      mask <<= loBit;
      if (isSingleWord())
        U.VAL |= mask;
      else
        U.pVal[0] |= mask;
    } else {
      setBitsSlowCase(loBit, hiBit);
    }
  }

  /// Set the top bits starting from loBit.
  void setBitsFrom(unsigned loBit) { return setBits(loBit, BitWidth); }

  /// Set the bottom loBits bits.
  void setLowBits(unsigned loBits) { return setBits(0, loBits); }

  /// Set the top hiBits bits.
  void setHighBits(unsigned hiBits) {
    return setBits(BitWidth - hiBits, BitWidth);
  }

  /// Set every bit to 0.
  void clearAllBits() {
    if (isSingleWord())
      U.VAL = 0;
    else
      memset(U.pVal, 0, getNumWords() * APINT_WORD_SIZE);
  }

  /// Set a given bit to 0.
  ///
  /// Set the given bit to 0 whose position is given as "bitPosition".
  void clearBit(unsigned BitPosition) {
    assert(BitPosition < BitWidth && "BitPosition out of range");
    WordType Mask = ~maskBit(BitPosition);
    if (isSingleWord())
      U.VAL &= Mask;
    else
      U.pVal[whichWord(BitPosition)] &= Mask;
  }

  /// Set bottom loBits bits to 0.
  void clearLowBits(unsigned loBits) {
    assert(loBits <= BitWidth && "More bits than bitwidth");
    APInt Keep = getHighBitsSet(BitWidth, BitWidth - loBits);
    *this &= Keep;
  }

  /// Set the sign bit to 0.
  void clearSignBit() { clearBit(BitWidth - 1); }

  /// Toggle every bit to its opposite value.
  void flipAllBits() {
    if (isSingleWord()) {
      U.VAL ^= WORDTYPE_MAX;
      clearUnusedBits();
    } else {
      flipAllBitsSlowCase();
    }
  }

  /// Toggles a given bit to its opposite value.
  ///
  /// Toggle a given bit to its opposite value whose position is given
  /// as "bitPosition".
  void flipBit(unsigned bitPosition);

  /// Negate this APInt in place.
  void negate() {
    flipAllBits();
    ++(*this);
  }

  /// Insert the bits from a smaller APInt starting at bitPosition.
  void insertBits(const APInt &SubBits, unsigned bitPosition);
  void insertBits(uint64_t SubBits, unsigned bitPosition, unsigned numBits);

  /// Return an APInt with the extracted bits [bitPosition,bitPosition+numBits).
  APInt extractBits(unsigned numBits, unsigned bitPosition) const;
  uint64_t extractBitsAsZExtValue(unsigned numBits, unsigned bitPosition) const;

  /// @}
  /// \name Value Characterization Functions
  /// @{

  /// Return the number of bits in the APInt.
  unsigned getBitWidth() const { return BitWidth; }

  /// Get the number of words.
  ///
  /// Here one word's bitwidth equals to that of uint64_t.
  ///
  /// \returns the number of words to hold the integer value of this APInt.
  unsigned getNumWords() const { return getNumWords(BitWidth); }

  /// Get the number of words.
  ///
  /// *NOTE* Here one word's bitwidth equals to that of uint64_t.
  ///
  /// \returns the number of words to hold the integer value with a given bit
  /// width.
  static unsigned getNumWords(unsigned BitWidth) {
    return ((uint64_t)BitWidth + APINT_BITS_PER_WORD - 1) / APINT_BITS_PER_WORD;
  }

  /// Compute the number of active bits in the value
  ///
  /// This function returns the number of active bits which is defined as the
  /// bit width minus the number of leading zeros. This is used in several
  /// computations to see how "wide" the value is.
  unsigned getActiveBits() const { return BitWidth - countl_zero(); }

  /// Compute the number of active words in the value of this APInt.
  ///
  /// This is used in conjunction with getActiveData to extract the raw value of
  /// the APInt.
  unsigned getActiveWords() const {
    unsigned numActiveBits = getActiveBits();
    return numActiveBits ? whichWord(numActiveBits - 1) + 1 : 1;
  }

  /// Get the minimum bit size for this signed APInt
  ///
  /// Computes the minimum bit width for this APInt while considering it to be a
  /// signed (and probably negative) value. If the value is not negative, this
  /// function returns the same value as getActiveBits()+1. Otherwise, it
  /// returns the smallest bit width that will retain the negative value. For
  /// example, -1 can be written as 0b1 or 0xFFFFFFFFFF. 0b1 is shorter and so
  /// for -1, this function will always return 1.
  unsigned getSignificantBits() const {
    return BitWidth - getNumSignBits() + 1;
  }

  /// Get zero extended value
  ///
  /// This method attempts to return the value of this APInt as a zero extended
  /// uint64_t. The bitwidth must be <= 64 or the value must fit within a
  /// uint64_t. Otherwise an assertion will result.
  uint64_t getZExtValue() const {
    if (isSingleWord())
      return U.VAL;
    assert(getActiveBits() <= 64 && "Too many bits for uint64_t");
    return U.pVal[0];
  }

  /// Get zero extended value if possible
  ///
  /// This method attempts to return the value of this APInt as a zero extended
  /// uint64_t. The bitwidth must be <= 64 or the value must fit within a
  /// uint64_t. Otherwise no value is returned.
  std::optional<uint64_t> tryZExtValue() const {
    return (getActiveBits() <= 64) ? std::optional<uint64_t>(getZExtValue())
                                   : std::nullopt;
  };

  /// Get sign extended value
  ///
  /// This method attempts to return the value of this APInt as a sign extended
  /// int64_t. The bit width must be <= 64 or the value must fit within an
  /// int64_t. Otherwise an assertion will result.
  int64_t getSExtValue() const {
    if (isSingleWord())
      return SignExtend64(U.VAL, BitWidth);
    assert(getSignificantBits() <= 64 && "Too many bits for int64_t");
    return int64_t(U.pVal[0]);
  }

  /// Get sign extended value if possible
  ///
  /// This method attempts to return the value of this APInt as a sign extended
  /// int64_t. The bitwidth must be <= 64 or the value must fit within an
  /// int64_t. Otherwise no value is returned.
  std::optional<int64_t> trySExtValue() const {
    return (getSignificantBits() <= 64) ? std::optional<int64_t>(getSExtValue())
                                        : std::nullopt;
  };

  /// Get bits required for string value.
  ///
  /// This method determines how many bits are required to hold the APInt
  /// equivalent of the string given by \p str.
  static unsigned getBitsNeeded(StringRef str, uint8_t radix);

  /// Get the bits that are sufficient to represent the string value. This may
  /// over estimate the amount of bits required, but it does not require
  /// parsing the value in the string.
  static unsigned getSufficientBitsNeeded(StringRef Str, uint8_t Radix);

  /// The APInt version of std::countl_zero.
  ///
  /// It counts the number of zeros from the most significant bit to the first
  /// one bit.
  ///
  /// \returns BitWidth if the value is zero, otherwise returns the number of
  ///   zeros from the most significant bit to the first one bits.
  unsigned countl_zero() const {
    if (isSingleWord()) {
      unsigned unusedBits = APINT_BITS_PER_WORD - BitWidth;
      return ::llvm::countl_zero(U.VAL) - unusedBits;
    }
    return countLeadingZerosSlowCase();
  }

  unsigned countLeadingZeros() const { return countl_zero(); }

  /// Count the number of leading one bits.
  ///
  /// This function is an APInt version of std::countl_one. It counts the number
  /// of ones from the most significant bit to the first zero bit.
  ///
  /// \returns 0 if the high order bit is not set, otherwise returns the number
  /// of 1 bits from the most significant to the least
  unsigned countl_one() const {
    if (isSingleWord()) {
      if (LLVM_UNLIKELY(BitWidth == 0))
        return 0;
      return ::llvm::countl_one(U.VAL << (APINT_BITS_PER_WORD - BitWidth));
    }
    return countLeadingOnesSlowCase();
  }

  unsigned countLeadingOnes() const { return countl_one(); }

  /// Computes the number of leading bits of this APInt that are equal to its
  /// sign bit.
  unsigned getNumSignBits() const {
    return isNegative() ? countl_one() : countl_zero();
  }

  /// Count the number of trailing zero bits.
  ///
  /// This function is an APInt version of std::countr_zero. It counts the
  /// number of zeros from the least significant bit to the first set bit.
  ///
  /// \returns BitWidth if the value is zero, otherwise returns the number of
  /// zeros from the least significant bit to the first one bit.
  unsigned countr_zero() const {
    if (isSingleWord()) {
      unsigned TrailingZeros = ::llvm::countr_zero(U.VAL);
      return (TrailingZeros > BitWidth ? BitWidth : TrailingZeros);
    }
    return countTrailingZerosSlowCase();
  }

  unsigned countTrailingZeros() const { return countr_zero(); }

  /// Count the number of trailing one bits.
  ///
  /// This function is an APInt version of std::countr_one. It counts the number
  /// of ones from the least significant bit to the first zero bit.
  ///
  /// \returns BitWidth if the value is all ones, otherwise returns the number
  /// of ones from the least significant bit to the first zero bit.
  unsigned countr_one() const {
    if (isSingleWord())
      return ::llvm::countr_one(U.VAL);
    return countTrailingOnesSlowCase();
  }

  unsigned countTrailingOnes() const { return countr_one(); }

  /// Count the number of bits set.
  ///
  /// This function is an APInt version of std::popcount. It counts the number
  /// of 1 bits in the APInt value.
  ///
  /// \returns 0 if the value is zero, otherwise returns the number of set bits.
  unsigned popcount() const {
    if (isSingleWord())
      return ::llvm::popcount(U.VAL);
    return countPopulationSlowCase();
  }

  /// @}
  /// \name Conversion Functions
  /// @{
  void print(raw_ostream &OS, bool isSigned) const;

  /// Converts an APInt to a string and append it to Str.  Str is commonly a
  /// SmallString. If Radix > 10, UpperCase determine the case of letter
  /// digits.
  void toString(SmallVectorImpl<char> &Str, unsigned Radix, bool Signed,
                bool formatAsCLiteral = false, bool UpperCase = true) const;

  /// Considers the APInt to be unsigned and converts it into a string in the
  /// radix given. The radix can be 2, 8, 10 16, or 36.
  void toStringUnsigned(SmallVectorImpl<char> &Str, unsigned Radix = 10) const {
    toString(Str, Radix, false, false);
  }

  /// Considers the APInt to be signed and converts it into a string in the
  /// radix given. The radix can be 2, 8, 10, 16, or 36.
  void toStringSigned(SmallVectorImpl<char> &Str, unsigned Radix = 10) const {
    toString(Str, Radix, true, false);
  }

  /// \returns a byte-swapped representation of this APInt Value.
  APInt byteSwap() const;

  /// \returns the value with the bit representation reversed of this APInt
  /// Value.
  APInt reverseBits() const;

  /// Converts this APInt to a double value.
  double roundToDouble(bool isSigned) const;

  /// Converts this unsigned APInt to a double value.
  double roundToDouble() const { return roundToDouble(false); }

  /// Converts this signed APInt to a double value.
  double signedRoundToDouble() const { return roundToDouble(true); }

  /// Converts APInt bits to a double
  ///
  /// The conversion does not do a translation from integer to double, it just
  /// re-interprets the bits as a double. Note that it is valid to do this on
  /// any bit width. Exactly 64 bits will be translated.
  double bitsToDouble() const { return ::llvm::bit_cast<double>(getWord(0)); }

  /// Converts APInt bits to a float
  ///
  /// The conversion does not do a translation from integer to float, it just
  /// re-interprets the bits as a float. Note that it is valid to do this on
  /// any bit width. Exactly 32 bits will be translated.
  float bitsToFloat() const {
    return ::llvm::bit_cast<float>(static_cast<uint32_t>(getWord(0)));
  }

  /// Converts a double to APInt bits.
  ///
  /// The conversion does not do a translation from double to integer, it just
  /// re-interprets the bits of the double.
  static APInt doubleToBits(double V) {
    return APInt(sizeof(double) * CHAR_BIT, ::llvm::bit_cast<uint64_t>(V));
  }

  /// Converts a float to APInt bits.
  ///
  /// The conversion does not do a translation from float to integer, it just
  /// re-interprets the bits of the float.
  static APInt floatToBits(float V) {
    return APInt(sizeof(float) * CHAR_BIT, ::llvm::bit_cast<uint32_t>(V));
  }

  /// @}
  /// \name Mathematics Operations
  /// @{

  /// \returns the floor log base 2 of this APInt.
  unsigned logBase2() const { return getActiveBits() - 1; }

  /// \returns the ceil log base 2 of this APInt.
  unsigned ceilLogBase2() const {
    APInt temp(*this);
    --temp;
    return temp.getActiveBits();
  }

  /// \returns the nearest log base 2 of this APInt. Ties round up.
  ///
  /// NOTE: When we have a BitWidth of 1, we define:
  ///
  ///   log2(0) = UINT32_MAX
  ///   log2(1) = 0
  ///
  /// to get around any mathematical concerns resulting from
  /// referencing 2 in a space where 2 does no exist.
  unsigned nearestLogBase2() const;

  /// \returns the log base 2 of this APInt if its an exact power of two, -1
  /// otherwise
  int32_t exactLogBase2() const {
    if (!isPowerOf2())
      return -1;
    return logBase2();
  }

  /// Compute the square root.
  APInt sqrt() const;

  /// Get the absolute value.  If *this is < 0 then return -(*this), otherwise
  /// *this.  Note that the "most negative" signed number (e.g. -128 for 8 bit
  /// wide APInt) is unchanged due to how negation works.
  APInt abs() const {
    if (isNegative())
      return -(*this);
    return *this;
  }

  /// \returns the multiplicative inverse for a given modulo.
  APInt multiplicativeInverse(const APInt &modulo) const;

  /// @}
  /// \name Building-block Operations for APInt and APFloat
  /// @{

  // These building block operations operate on a representation of arbitrary
  // precision, two's-complement, bignum integer values. They should be
  // sufficient to implement APInt and APFloat bignum requirements. Inputs are
  // generally a pointer to the base of an array of integer parts, representing
  // an unsigned bignum, and a count of how many parts there are.

  /// Sets the least significant part of a bignum to the input value, and zeroes
  /// out higher parts.
  static void tcSet(WordType *, WordType, unsigned);

  /// Assign one bignum to another.
  static void tcAssign(WordType *, const WordType *, unsigned);

  /// Returns true if a bignum is zero, false otherwise.
  static bool tcIsZero(const WordType *, unsigned);

  /// Extract the given bit of a bignum; returns 0 or 1.  Zero-based.
  static int tcExtractBit(const WordType *, unsigned bit);

  /// Copy the bit vector of width srcBITS from SRC, starting at bit srcLSB, to
  /// DST, of dstCOUNT parts, such that the bit srcLSB becomes the least
  /// significant bit of DST.  All high bits above srcBITS in DST are
  /// zero-filled.
  static void tcExtract(WordType *, unsigned dstCount, const WordType *,
                        unsigned srcBits, unsigned srcLSB);

  /// Set the given bit of a bignum.  Zero-based.
  static void tcSetBit(WordType *, unsigned bit);

  /// Clear the given bit of a bignum.  Zero-based.
  static void tcClearBit(WordType *, unsigned bit);

  /// Returns the bit number of the least or most significant set bit of a
  /// number.  If the input number has no bits set -1U is returned.
  static unsigned tcLSB(const WordType *, unsigned n);
  static unsigned tcMSB(const WordType *parts, unsigned n);

  /// Negate a bignum in-place.
  static void tcNegate(WordType *, unsigned);

  /// DST += RHS + CARRY where CARRY is zero or one.  Returns the carry flag.
  static WordType tcAdd(WordType *, const WordType *, WordType carry, unsigned);
  /// DST += RHS.  Returns the carry flag.
  static WordType tcAddPart(WordType *, WordType, unsigned);

  /// DST -= RHS + CARRY where CARRY is zero or one. Returns the carry flag.
  static WordType tcSubtract(WordType *, const WordType *, WordType carry,
                             unsigned);
  /// DST -= RHS.  Returns the carry flag.
  static WordType tcSubtractPart(WordType *, WordType, unsigned);

  /// DST += SRC * MULTIPLIER + PART   if add is true
  /// DST  = SRC * MULTIPLIER + PART   if add is false
  ///
  /// Requires 0 <= DSTPARTS <= SRCPARTS + 1.  If DST overlaps SRC they must
  /// start at the same point, i.e. DST == SRC.
  ///
  /// If DSTPARTS == SRC_PARTS + 1 no overflow occurs and zero is returned.
  /// Otherwise DST is filled with the least significant DSTPARTS parts of the
  /// result, and if all of the omitted higher parts were zero return zero,
  /// otherwise overflow occurred and return one.
  static int tcMultiplyPart(WordType *dst, const WordType *src,
                            WordType multiplier, WordType carry,
                            unsigned srcParts, unsigned dstParts, bool add);

  /// DST = LHS * RHS, where DST has the same width as the operands and is
  /// filled with the least significant parts of the result.  Returns one if
  /// overflow occurred, otherwise zero.  DST must be disjoint from both
  /// operands.
  static int tcMultiply(WordType *, const WordType *, const WordType *,
                        unsigned);

  /// DST = LHS * RHS, where DST has width the sum of the widths of the
  /// operands. No overflow occurs. DST must be disjoint from both operands.
  static void tcFullMultiply(WordType *, const WordType *, const WordType *,
                             unsigned, unsigned);

  /// If RHS is zero LHS and REMAINDER are left unchanged, return one.
  /// Otherwise set LHS to LHS / RHS with the fractional part discarded, set
  /// REMAINDER to the remainder, return zero.  i.e.
  ///
  ///  OLD_LHS = RHS * LHS + REMAINDER
  ///
  /// SCRATCH is a bignum of the same size as the operands and result for use by
  /// the routine; its contents need not be initialized and are destroyed.  LHS,
  /// REMAINDER and SCRATCH must be distinct.
  static int tcDivide(WordType *lhs, const WordType *rhs, WordType *remainder,
                      WordType *scratch, unsigned parts);

  /// Shift a bignum left Count bits. Shifted in bits are zero. There are no
  /// restrictions on Count.
  static void tcShiftLeft(WordType *, unsigned Words, unsigned Count);

  /// Shift a bignum right Count bits.  Shifted in bits are zero.  There are no
  /// restrictions on Count.
  static void tcShiftRight(WordType *, unsigned Words, unsigned Count);

  /// Comparison (unsigned) of two bignums.
  static int tcCompare(const WordType *, const WordType *, unsigned);

  /// Increment a bignum in-place.  Return the carry flag.
  static WordType tcIncrement(WordType *dst, unsigned parts) {
    return tcAddPart(dst, 1, parts);
  }

  /// Decrement a bignum in-place.  Return the borrow flag.
  static WordType tcDecrement(WordType *dst, unsigned parts) {
    return tcSubtractPart(dst, 1, parts);
  }

  /// Used to insert APInt objects, or objects that contain APInt objects, into
  ///  FoldingSets.
  void Profile(FoldingSetNodeID &id) const;

  /// debug method
  void dump() const;

  /// Returns whether this instance allocated memory.
  bool needsCleanup() const { return !isSingleWord(); }

private:
  /// This union is used to store the integer value. When the
  /// integer bit-width <= 64, it uses VAL, otherwise it uses pVal.
  union {
    uint64_t VAL;   ///< Used to store the <= 64 bits integer value.
    uint64_t *pVal; ///< Used to store the >64 bits integer value.
  } U;

  unsigned BitWidth = 1; ///< The number of bits in this APInt.

  friend struct DenseMapInfo<APInt, void>;
  friend class APSInt;

  /// This constructor is used only internally for speed of construction of
  /// temporaries. It is unsafe since it takes ownership of the pointer, so it
  /// is not public.
  APInt(uint64_t *val, unsigned bits) : BitWidth(bits) { U.pVal = val; }

  /// Determine which word a bit is in.
  ///
  /// \returns the word position for the specified bit position.
  static unsigned whichWord(unsigned bitPosition) {
    return bitPosition / APINT_BITS_PER_WORD;
  }

  /// Determine which bit in a word the specified bit position is in.
  static unsigned whichBit(unsigned bitPosition) {
    return bitPosition % APINT_BITS_PER_WORD;
  }

  /// Get a single bit mask.
  ///
  /// \returns a uint64_t with only bit at "whichBit(bitPosition)" set
  /// This method generates and returns a uint64_t (word) mask for a single
  /// bit at a specific bit position. This is used to mask the bit in the
  /// corresponding word.
  static uint64_t maskBit(unsigned bitPosition) {
    return 1ULL << whichBit(bitPosition);
  }

  /// Clear unused high order bits
  ///
  /// This method is used internally to clear the top "N" bits in the high order
  /// word that are not used by the APInt. This is needed after the most
  /// significant word is assigned a value to ensure that those bits are
  /// zero'd out.
  APInt &clearUnusedBits() {
    // Compute how many bits are used in the final word.
    unsigned WordBits = ((BitWidth - 1) % APINT_BITS_PER_WORD) + 1;

    // Mask out the high bits.
    uint64_t mask = WORDTYPE_MAX >> (APINT_BITS_PER_WORD - WordBits);
    if (LLVM_UNLIKELY(BitWidth == 0))
      mask = 0;

    if (isSingleWord())
      U.VAL &= mask;
    else
      U.pVal[getNumWords() - 1] &= mask;
    return *this;
  }

  /// Get the word corresponding to a bit position
  /// \returns the corresponding word for the specified bit position.
  uint64_t getWord(unsigned bitPosition) const {
    return isSingleWord() ? U.VAL : U.pVal[whichWord(bitPosition)];
  }

  static uint64_t *getClearedMemory(unsigned numWords) {
    return (uint64_t *)calloc(numWords, sizeof(uint64_t));
  }
  static uint64_t *getMemory(unsigned numWords) {
    return (uint64_t *)malloc(numWords * sizeof(uint64_t));
  }

  /// Utility method to change the bit width of this APInt to new bit width,
  /// allocating and/or deallocating as necessary. There is no guarantee on the
  /// value of any bits upon return. Caller should populate the bits after.
  void reallocate(unsigned NewBitWidth);

  /// Convert a char array into an APInt
  ///
  /// \param radix 2, 8, 10, 16, or 36
  /// Converts a string into a number.  The string must be non-empty
  /// and well-formed as a number of the given base. The bit-width
  /// must be sufficient to hold the result.
  ///
  /// This is used by the constructors that take string arguments.
  ///
  /// StringRef::getAsInteger is superficially similar but (1) does
  /// not assume that the string is well-formed and (2) grows the
  /// result to hold the input.
  void fromString(unsigned numBits, StringRef str, uint8_t radix);

  /// An internal division function for dividing APInts.
  ///
  /// This is used by the toString method to divide by the radix. It simply
  /// provides a more convenient form of divide for internal use since KnuthDiv
  /// has specific constraints on its inputs. If those constraints are not met
  /// then it provides a simpler form of divide.
  static void divide(const WordType *LHS, unsigned lhsWords,
                     const WordType *RHS, unsigned rhsWords, WordType *Quotient,
                     WordType *Remainder);

  /// out-of-line slow case for inline constructor
  void initSlowCase(uint64_t val, bool isSigned);

  /// shared code between two array constructors
  void initFromArray(ArrayRef<uint64_t> array);

  /// out-of-line slow case for inline copy constructor
  void initSlowCase(const APInt &that);

  /// out-of-line slow case for shl
  void shlSlowCase(unsigned ShiftAmt);

  /// out-of-line slow case for lshr.
  void lshrSlowCase(unsigned ShiftAmt);

  /// out-of-line slow case for ashr.
  void ashrSlowCase(unsigned ShiftAmt);

  /// out-of-line slow case for operator=
  void assignSlowCase(const APInt &RHS);

  /// out-of-line slow case for operator==
  bool equalSlowCase(const APInt &RHS) const LLVM_READONLY;

  /// out-of-line slow case for countLeadingZeros
  unsigned countLeadingZerosSlowCase() const LLVM_READONLY;

  /// out-of-line slow case for countLeadingOnes.
  unsigned countLeadingOnesSlowCase() const LLVM_READONLY;

  /// out-of-line slow case for countTrailingZeros.
  unsigned countTrailingZerosSlowCase() const LLVM_READONLY;

  /// out-of-line slow case for countTrailingOnes
  unsigned countTrailingOnesSlowCase() const LLVM_READONLY;

  /// out-of-line slow case for countPopulation
  unsigned countPopulationSlowCase() const LLVM_READONLY;

  /// out-of-line slow case for intersects.
  bool intersectsSlowCase(const APInt &RHS) const LLVM_READONLY;

  /// out-of-line slow case for isSubsetOf.
  bool isSubsetOfSlowCase(const APInt &RHS) const LLVM_READONLY;

  /// out-of-line slow case for setBits.
  void setBitsSlowCase(unsigned loBit, unsigned hiBit);

  /// out-of-line slow case for flipAllBits.
  void flipAllBitsSlowCase();

  /// out-of-line slow case for concat.
  APInt concatSlowCase(const APInt &NewLSB) const;

  /// out-of-line slow case for operator&=.
  void andAssignSlowCase(const APInt &RHS);

  /// out-of-line slow case for operator|=.
  void orAssignSlowCase(const APInt &RHS);

  /// out-of-line slow case for operator^=.
  void xorAssignSlowCase(const APInt &RHS);

  /// Unsigned comparison. Returns -1, 0, or 1 if this APInt is less than, equal
  /// to, or greater than RHS.
  int compare(const APInt &RHS) const LLVM_READONLY;

  /// Signed comparison. Returns -1, 0, or 1 if this APInt is less than, equal
  /// to, or greater than RHS.
  int compareSigned(const APInt &RHS) const LLVM_READONLY;

  /// @}
};

inline bool operator==(uint64_t V1, const APInt &V2) { return V2 == V1; }

inline bool operator!=(uint64_t V1, const APInt &V2) { return V2 != V1; }

/// Unary bitwise complement operator.
///
/// \returns an APInt that is the bitwise complement of \p v.
inline APInt operator~(APInt v) {
  v.flipAllBits();
  return v;
}

inline APInt operator&(APInt a, const APInt &b) {
  a &= b;
  return a;
}

inline APInt operator&(const APInt &a, APInt &&b) {
  b &= a;
  return std::move(b);
}

inline APInt operator&(APInt a, uint64_t RHS) {
  a &= RHS;
  return a;
}

inline APInt operator&(uint64_t LHS, APInt b) {
  b &= LHS;
  return b;
}

inline APInt operator|(APInt a, const APInt &b) {
  a |= b;
  return a;
}

inline APInt operator|(const APInt &a, APInt &&b) {
  b |= a;
  return std::move(b);
}

inline APInt operator|(APInt a, uint64_t RHS) {
  a |= RHS;
  return a;
}

inline APInt operator|(uint64_t LHS, APInt b) {
  b |= LHS;
  return b;
}

inline APInt operator^(APInt a, const APInt &b) {
  a ^= b;
  return a;
}

inline APInt operator^(const APInt &a, APInt &&b) {
  b ^= a;
  return std::move(b);
}

inline APInt operator^(APInt a, uint64_t RHS) {
  a ^= RHS;
  return a;
}

inline APInt operator^(uint64_t LHS, APInt b) {
  b ^= LHS;
  return b;
}

inline raw_ostream &operator<<(raw_ostream &OS, const APInt &I) {
  I.print(OS, true);
  return OS;
}

inline APInt operator-(APInt v) {
  v.negate();
  return v;
}

inline APInt operator+(APInt a, const APInt &b) {
  a += b;
  return a;
}

inline APInt operator+(const APInt &a, APInt &&b) {
  b += a;
  return std::move(b);
}

inline APInt operator+(APInt a, uint64_t RHS) {
  a += RHS;
  return a;
}

inline APInt operator+(uint64_t LHS, APInt b) {
  b += LHS;
  return b;
}

inline APInt operator-(APInt a, const APInt &b) {
  a -= b;
  return a;
}

inline APInt operator-(const APInt &a, APInt &&b) {
  b.negate();
  b += a;
  return std::move(b);
}

inline APInt operator-(APInt a, uint64_t RHS) {
  a -= RHS;
  return a;
}

inline APInt operator-(uint64_t LHS, APInt b) {
  b.negate();
  b += LHS;
  return b;
}

inline APInt operator*(APInt a, uint64_t RHS) {
  a *= RHS;
  return a;
}

inline APInt operator*(uint64_t LHS, APInt b) {
  b *= LHS;
  return b;
}

namespace APIntOps {

/// Determine the smaller of two APInts considered to be signed.
inline const APInt &smin(const APInt &A, const APInt &B) {
  return A.slt(B) ? A : B;
}

/// Determine the larger of two APInts considered to be signed.
inline const APInt &smax(const APInt &A, const APInt &B) {
  return A.sgt(B) ? A : B;
}

/// Determine the smaller of two APInts considered to be unsigned.
inline const APInt &umin(const APInt &A, const APInt &B) {
  return A.ult(B) ? A : B;
}

/// Determine the larger of two APInts considered to be unsigned.
inline const APInt &umax(const APInt &A, const APInt &B) {
  return A.ugt(B) ? A : B;
}

/// Compute GCD of two unsigned APInt values.
///
/// This function returns the greatest common divisor of the two APInt values
/// using Stein's algorithm.
///
/// \returns the greatest common divisor of A and B.
APInt GreatestCommonDivisor(APInt A, APInt B);

/// Converts the given APInt to a double value.
///
/// Treats the APInt as an unsigned value for conversion purposes.
inline double RoundAPIntToDouble(const APInt &APIVal) {
  return APIVal.roundToDouble();
}

/// Converts the given APInt to a double value.
///
/// Treats the APInt as a signed value for conversion purposes.
inline double RoundSignedAPIntToDouble(const APInt &APIVal) {
  return APIVal.signedRoundToDouble();
}

/// Converts the given APInt to a float value.
inline float RoundAPIntToFloat(const APInt &APIVal) {
  return float(RoundAPIntToDouble(APIVal));
}

/// Converts the given APInt to a float value.
///
/// Treats the APInt as a signed value for conversion purposes.
inline float RoundSignedAPIntToFloat(const APInt &APIVal) {
  return float(APIVal.signedRoundToDouble());
}

/// Converts the given double value into a APInt.
///
/// This function convert a double value to an APInt value.
APInt RoundDoubleToAPInt(double Double, unsigned width);

/// Converts a float value into a APInt.
///
/// Converts a float value into an APInt value.
inline APInt RoundFloatToAPInt(float Float, unsigned width) {
  return RoundDoubleToAPInt(double(Float), width);
}

/// Return A unsign-divided by B, rounded by the given rounding mode.
APInt RoundingUDiv(const APInt &A, const APInt &B, APInt::Rounding RM);

/// Return A sign-divided by B, rounded by the given rounding mode.
APInt RoundingSDiv(const APInt &A, const APInt &B, APInt::Rounding RM);

/// Let q(n) = An^2 + Bn + C, and BW = bit width of the value range
/// (e.g. 32 for i32).
/// This function finds the smallest number n, such that
/// (a) n >= 0 and q(n) = 0, or
/// (b) n >= 1 and q(n-1) and q(n), when evaluated in the set of all
///     integers, belong to two different intervals [Rk, Rk+R),
///     where R = 2^BW, and k is an integer.
/// The idea here is to find when q(n) "overflows" 2^BW, while at the
/// same time "allowing" subtraction. In unsigned modulo arithmetic a
/// subtraction (treated as addition of negated numbers) would always
/// count as an overflow, but here we want to allow values to decrease
/// and increase as long as they are within the same interval.
/// Specifically, adding of two negative numbers should not cause an
/// overflow (as long as the magnitude does not exceed the bit width).
/// On the other hand, given a positive number, adding a negative
/// number to it can give a negative result, which would cause the
/// value to go from [-2^BW, 0) to [0, 2^BW). In that sense, zero is
/// treated as a special case of an overflow.
///
/// This function returns std::nullopt if after finding k that minimizes the
/// positive solution to q(n) = kR, both solutions are contained between
/// two consecutive integers.
///
/// There are cases where q(n) > T, and q(n+1) < T (assuming evaluation
/// in arithmetic modulo 2^BW, and treating the values as signed) by the
/// virtue of *signed* overflow. This function will *not* find such an n,
/// however it may find a value of n satisfying the inequalities due to
/// an *unsigned* overflow (if the values are treated as unsigned).
/// To find a solution for a signed overflow, treat it as a problem of
/// finding an unsigned overflow with a range with of BW-1.
///
/// The returned value may have a different bit width from the input
/// coefficients.
/// Returns APInt() (bitwidth 0) if no solution exists.
APInt SolveQuadraticEquationWrap(APInt A, APInt B, APInt C,
                                 unsigned RangeWidth);

/// Compare two values, and if they are different, return the position of the
/// most significant bit that is different in the values.
/// Returns UINT_MAX if A == B.
unsigned GetMostSignificantDifferentBit(const APInt &A, const APInt &B);

/// Splat/Merge neighboring bits to widen/narrow the bitmask represented
/// by \param A to \param NewBitWidth bits.
///
/// MatchAnyBits: (Default)
/// e.g. ScaleBitMask(0b0101, 8) -> 0b00110011
/// e.g. ScaleBitMask(0b00011011, 4) -> 0b0111
///
/// MatchAllBits:
/// e.g. ScaleBitMask(0b0101, 8) -> 0b00110011
/// e.g. ScaleBitMask(0b00011011, 4) -> 0b0001
/// A.getBitwidth() or NewBitWidth must be a whole multiples of the other.
APInt ScaleBitMask(const APInt &A, unsigned NewBitWidth,
                   bool MatchAllBits = false);
} // namespace APIntOps

// See friend declaration above. This additional declaration is required in
// order to compile LLVM with IBM xlC compiler.
hash_code hash_value(const APInt &Arg);

/// StoreIntToMemory - Fills the StoreBytes bytes of memory starting from Dst
/// with the integer held in IntVal.
void StoreIntToMemory(const APInt &IntVal, uint8_t *Dst, unsigned StoreBytes);

/// LoadIntFromMemory - Loads the integer stored in the LoadBytes bytes starting
/// from Src into IntVal, which is assumed to be wide enough and to hold zero.
void LoadIntFromMemory(APInt &IntVal, const uint8_t *Src, unsigned LoadBytes);

/// Provide DenseMapInfo for APInt.
template <> struct DenseMapInfo<APInt, void> {
  static inline APInt getEmptyKey() {
    APInt V(nullptr, 0);
    V.U.VAL = ~0ULL;
    return V;
  }

  static inline APInt getTombstoneKey() {
    APInt V(nullptr, 0);
    V.U.VAL = ~1ULL;
    return V;
  }

  static unsigned getHashValue(const APInt &Key) {
    return (unsigned)(hash_value(Key));
  }

  static bool isEqual(const APInt &LHS, const APInt &RHS) {
    return LHS.getBitWidth() == RHS.getBitWidth() && LHS == RHS;
  }
};

// ---------------------------------------------------------------------------
// Inline APInt method definitions delegating to pure-C csupport functions.
// These eliminate the need for out-of-line definitions in cpp_bridge.cpp.
// ---------------------------------------------------------------------------

inline void APInt::andAssignSlowCase(const APInt &RHS) {
  csupport_apint_and_assign(U.pVal, RHS.U.pVal, getNumWords());
}
inline void APInt::orAssignSlowCase(const APInt &RHS) {
  csupport_apint_or_assign(U.pVal, RHS.U.pVal, getNumWords());
}
inline void APInt::xorAssignSlowCase(const APInt &RHS) {
  csupport_apint_xor_assign(U.pVal, RHS.U.pVal, getNumWords());
}
inline unsigned APInt::countLeadingZerosSlowCase() const {
  return csupport_apint_count_leading_zeros(U.pVal, getNumWords(), BitWidth);
}
inline unsigned APInt::countLeadingOnesSlowCase() const {
  return csupport_apint_count_leading_ones(U.pVal, getNumWords(), BitWidth);
}
inline unsigned APInt::countTrailingZerosSlowCase() const {
  return csupport_apint_count_trailing_zeros(U.pVal, getNumWords(), BitWidth);
}
inline unsigned APInt::countTrailingOnesSlowCase() const {
  return csupport_apint_count_trailing_ones(U.pVal, getNumWords());
}
inline unsigned APInt::countPopulationSlowCase() const {
  return csupport_apint_popcount(U.pVal, getNumWords());
}
inline bool APInt::intersectsSlowCase(const APInt &RHS) const {
  return csupport_apint_intersects(U.pVal, RHS.U.pVal, getNumWords());
}
inline bool APInt::isSubsetOfSlowCase(const APInt &RHS) const {
  return csupport_apint_is_subset_of(U.pVal, RHS.U.pVal, getNumWords());
}
inline void APInt::setBitsSlowCase(unsigned loBit, unsigned hiBit) {
  csupport_apint_set_bits(U.pVal, loBit, hiBit);
}
inline void APInt::flipAllBitsSlowCase() {
  csupport_apint_flip_all(U.pVal, getNumWords());
  clearUnusedBits();
}
inline bool APInt::equalSlowCase(const APInt &RHS) const {
  int r;
  csupport_apint_equal_slow(U.pVal, RHS.U.pVal, getNumWords(), &r);
  return r;
}
inline void APInt::ashrSlowCase(unsigned ShiftAmt) {
  csupport_apint_ashr(U.pVal, getNumWords(), BitWidth, ShiftAmt);
  clearUnusedBits();
}
inline void APInt::lshrSlowCase(unsigned ShiftAmt) {
  csupport_apint_lshr_slow(U.pVal, U.pVal, getNumWords(), ShiftAmt, BitWidth);
}
inline void APInt::shlSlowCase(unsigned ShiftAmt) {
  csupport_apint_shl_slow(U.pVal, U.pVal, getNumWords(), ShiftAmt, BitWidth);
}
inline void APInt::divide(const WordType *LHS, unsigned lhsWords,
                          const WordType *RHS, unsigned rhsWords,
                          WordType *Quotient, WordType *Remainder) {
  csupport_apint_divide(LHS, lhsWords, RHS, rhsWords, Quotient, Remainder);
}

// tc* bignum operations — all delegate directly to C implementations
inline void APInt::tcSet(WordType *dst, WordType part, unsigned parts) {
  csupport_apint_tc_set(dst, part, parts);
}
inline void APInt::tcAssign(WordType *dst, const WordType *src,
                            unsigned parts) {
  csupport_apint_tc_assign(dst, src, parts);
}
inline bool APInt::tcIsZero(const WordType *src, unsigned parts) {
  return csupport_apint_tc_is_zero(src, parts);
}
inline int APInt::tcExtractBit(const WordType *parts, unsigned bit) {
  return csupport_apint_tc_extract_bit(parts, bit);
}
inline void APInt::tcSetBit(WordType *parts, unsigned bit) {
  csupport_apint_tc_set_bit(parts, bit);
}
inline void APInt::tcClearBit(WordType *parts, unsigned bit) {
  csupport_apint_tc_clear_bit(parts, bit);
}
inline unsigned APInt::tcLSB(const WordType *parts, unsigned n) {
  return csupport_apint_tc_lsb(parts, n);
}
inline unsigned APInt::tcMSB(const WordType *parts, unsigned n) {
  return csupport_apint_tc_msb(parts, n);
}
inline void APInt::tcExtract(WordType *dst, unsigned dstCount,
                             const WordType *src, unsigned srcBits,
                             unsigned srcLSB) {
  csupport_apint_tc_extract(dst, dstCount, src, srcBits, srcLSB);
}
inline APInt::WordType APInt::tcAdd(WordType *dst, const WordType *rhs,
                                    WordType c, unsigned parts) {
  return csupport_apint_tc_add(dst, rhs, c, parts);
}
inline APInt::WordType APInt::tcAddPart(WordType *dst, WordType src,
                                        unsigned parts) {
  return csupport_apint_tc_add_part(dst, src, parts);
}
inline APInt::WordType APInt::tcSubtract(WordType *dst, const WordType *rhs,
                                         WordType c, unsigned parts) {
  return csupport_apint_tc_subtract(dst, rhs, c, parts);
}
inline APInt::WordType APInt::tcSubtractPart(WordType *dst, WordType src,
                                             unsigned parts) {
  return csupport_apint_tc_subtract_part(dst, src, parts);
}
inline void APInt::tcNegate(WordType *dst, unsigned parts) {
  csupport_apint_tc_negate(dst, parts);
}
inline int APInt::tcMultiplyPart(WordType *dst, const WordType *src,
                                 WordType multiplier, WordType carry,
                                 unsigned srcParts, unsigned dstParts,
                                 bool add) {
  return csupport_apint_tc_multiply_part(dst, src, multiplier, carry, srcParts,
                                         dstParts, add ? 1 : 0);
}
inline int APInt::tcMultiply(WordType *dst, const WordType *lhs,
                             const WordType *rhs, unsigned parts) {
  return csupport_apint_tc_multiply(dst, lhs, rhs, parts);
}
inline void APInt::tcFullMultiply(WordType *dst, const WordType *lhs,
                                  const WordType *rhs, unsigned lhsParts,
                                  unsigned rhsParts) {
  csupport_apint_tc_full_multiply(dst, lhs, rhs, lhsParts, rhsParts);
}
inline int APInt::tcDivide(WordType *lhs, const WordType *rhs,
                           WordType *remainder, WordType *srhs,
                           unsigned parts) {
  return csupport_apint_tc_divide(lhs, rhs, remainder, srhs, parts);
}
inline void APInt::tcShiftLeft(WordType *Dst, unsigned Words, unsigned Count) {
  csupport_apint_shift_left(Dst, Dst, Words, Count);
}
inline void APInt::tcShiftRight(WordType *Dst, unsigned Words, unsigned Count) {
  csupport_apint_shift_right_logical(Dst, Dst, Words, Count);
}
inline int APInt::tcCompare(const WordType *lhs, const WordType *rhs,
                            unsigned parts) {
  return csupport_apint_tc_compare(lhs, rhs, parts);
}

// ---------------------------------------------------------------------------
// Inline APInt method definitions: init / assign / reallocate
// ---------------------------------------------------------------------------

inline void APInt::initSlowCase(uint64_t val, bool isSigned) {
  U.pVal = getClearedMemory(getNumWords());
  U.pVal[0] = val;
  if (isSigned && int64_t(val) < 0)
    for (unsigned i = 1; i < getNumWords(); ++i)
      U.pVal[i] = WORDTYPE_MAX;
  clearUnusedBits();
}

inline void APInt::initSlowCase(const APInt &that) {
  U.pVal = getMemory(getNumWords());
  memcpy(U.pVal, that.U.pVal, getNumWords() * APINT_WORD_SIZE);
}

inline void APInt::initFromArray(ArrayRef<uint64_t> bigVal) {
  assert(bigVal.data() && "Null pointer detected!");
  if (isSingleWord())
    U.VAL = bigVal[0];
  else {
    U.pVal = getClearedMemory(getNumWords());
    unsigned words =
        bigVal.size() < getNumWords() ? bigVal.size() : getNumWords();
    memcpy(U.pVal, bigVal.data(), words * APINT_WORD_SIZE);
  }
  clearUnusedBits();
}

inline APInt::APInt(unsigned numBits, ArrayRef<uint64_t> bigVal)
    : BitWidth(numBits) {
  initFromArray(bigVal);
}

inline APInt::APInt(unsigned numBits, unsigned numWords,
                    const uint64_t bigVal[])
    : BitWidth(numBits) {
  initFromArray(ArrayRef(bigVal, numWords));
}

inline APInt::APInt(unsigned numbits, ::llvm::StringRef Str, uint8_t radix)
    : BitWidth(numbits) {
  fromString(numbits, Str, radix);
}

inline void APInt::reallocate(unsigned NewBitWidth) {
  if (getNumWords() == getNumWords(NewBitWidth)) {
    BitWidth = NewBitWidth;
    return;
  }
  if (!isSingleWord())
    free(U.pVal);
  BitWidth = NewBitWidth;
  if (!isSingleWord())
    U.pVal = getMemory(getNumWords());
}

inline void APInt::assignSlowCase(const APInt &RHS) {
  if (this == &RHS)
    return;
  reallocate(RHS.getBitWidth());
  if (isSingleWord())
    U.VAL = RHS.U.VAL;
  else
    memcpy(U.pVal, RHS.U.pVal, getNumWords() * APINT_WORD_SIZE);
}

// ---------------------------------------------------------------------------
// Arithmetic operators
// ---------------------------------------------------------------------------

inline APInt &APInt::operator++() {
  if (isSingleWord())
    ++U.VAL;
  else
    tcIncrement(U.pVal, getNumWords());
  return clearUnusedBits();
}

inline APInt &APInt::operator--() {
  if (isSingleWord())
    --U.VAL;
  else
    tcDecrement(U.pVal, getNumWords());
  return clearUnusedBits();
}

inline APInt &APInt::operator+=(const APInt &RHS) {
  assert(BitWidth == RHS.BitWidth && "Bit widths must be the same");
  if (isSingleWord())
    U.VAL += RHS.U.VAL;
  else
    tcAdd(U.pVal, RHS.U.pVal, 0, getNumWords());
  return clearUnusedBits();
}

inline APInt &APInt::operator+=(uint64_t RHS) {
  if (isSingleWord())
    U.VAL += RHS;
  else
    tcAddPart(U.pVal, RHS, getNumWords());
  return clearUnusedBits();
}

inline APInt &APInt::operator-=(const APInt &RHS) {
  assert(BitWidth == RHS.BitWidth && "Bit widths must be the same");
  if (isSingleWord())
    U.VAL -= RHS.U.VAL;
  else
    tcSubtract(U.pVal, RHS.U.pVal, 0, getNumWords());
  return clearUnusedBits();
}

inline APInt &APInt::operator-=(uint64_t RHS) {
  if (isSingleWord())
    U.VAL -= RHS;
  else
    tcSubtractPart(U.pVal, RHS, getNumWords());
  return clearUnusedBits();
}

inline APInt APInt::operator*(const APInt &RHS) const {
  assert(BitWidth == RHS.BitWidth && "Bit widths must be the same");
  if (isSingleWord())
    return APInt(BitWidth, U.VAL * RHS.U.VAL);
  APInt Result(getMemory(getNumWords()), getBitWidth());
  tcMultiply(Result.U.pVal, U.pVal, RHS.U.pVal, getNumWords());
  Result.clearUnusedBits();
  return Result;
}

inline APInt &APInt::operator*=(const APInt &RHS) {
  *this = *this * RHS;
  return *this;
}

inline APInt &APInt::operator*=(uint64_t RHS) {
  if (isSingleWord()) {
    U.VAL *= RHS;
  } else {
    unsigned NumWords = getNumWords();
    tcMultiplyPart(U.pVal, U.pVal, RHS, 0, NumWords, NumWords, false);
  }
  return clearUnusedBits();
}

// ---------------------------------------------------------------------------
// Comparison
// ---------------------------------------------------------------------------

inline int APInt::compare(const APInt &RHS) const {
  assert(BitWidth == RHS.BitWidth && "Bit widths must be same for comparison");
  if (isSingleWord())
    return U.VAL < RHS.U.VAL ? -1 : U.VAL > RHS.U.VAL;
  return tcCompare(U.pVal, RHS.U.pVal, getNumWords());
}

inline int APInt::compareSigned(const APInt &RHS) const {
  assert(BitWidth == RHS.BitWidth && "Bit widths must be same for comparison");
  if (isSingleWord()) {
    int64_t lhsSext = SignExtend64(U.VAL, BitWidth);
    int64_t rhsSext = SignExtend64(RHS.U.VAL, BitWidth);
    return lhsSext < rhsSext ? -1 : lhsSext > rhsSext;
  }
  bool lhsNeg = isNegative();
  bool rhsNeg = RHS.isNegative();
  if (lhsNeg != rhsNeg)
    return lhsNeg ? -1 : 1;
  return tcCompare(U.pVal, RHS.U.pVal, getNumWords());
}

// ---------------------------------------------------------------------------
// Bit manipulation
// ---------------------------------------------------------------------------

inline APInt APInt::concatSlowCase(const APInt &NewLSB) const {
  unsigned NewWidth = getBitWidth() + NewLSB.getBitWidth();
  APInt Result(NewWidth, 0);
  unsigned nw = Result.getNumWords();
  uint64_t buf[64];
  assert(nw <= 64);
  csupport_apint_concat(buf, nw, getRawData(), getBitWidth(),
                        NewLSB.getRawData(), NewLSB.getBitWidth());
  uint64_t *dst = Result.isSingleWord() ? &Result.U.VAL : Result.U.pVal;
  memcpy(dst, buf, nw * sizeof(uint64_t));
  return Result;
}

inline void APInt::flipBit(unsigned bitPosition) {
  assert(bitPosition < BitWidth && "Out of the bit-width range!");
  setBitVal(bitPosition, !(*this)[bitPosition]);
}

inline void APInt::insertBits(const APInt &subBits, unsigned bitPosition) {
  unsigned subBitWidth = subBits.getBitWidth();
  assert((subBitWidth + bitPosition) <= BitWidth && "Illegal bit insertion");
  if (subBitWidth == 0)
    return;
  if (subBitWidth == BitWidth) {
    *this = subBits;
    return;
  }
  if (isSingleWord()) {
    uint64_t mask = WORDTYPE_MAX >> (APINT_BITS_PER_WORD - subBitWidth);
    U.VAL &= ~(mask << bitPosition);
    U.VAL |= (subBits.U.VAL << bitPosition);
    return;
  }
  csupport_apint_insert_bits(U.pVal, getNumWords(), subBits.getRawData(),
                             subBits.getNumWords(), subBitWidth, bitPosition);
}

inline void APInt::insertBits(uint64_t subBits, unsigned bitPosition,
                              unsigned numBits) {
  uint64_t maskBits = maskTrailingOnes<uint64_t>(numBits);
  subBits &= maskBits;
  if (isSingleWord()) {
    U.VAL &= ~(maskBits << bitPosition);
    U.VAL |= subBits << bitPosition;
    return;
  }
  csupport_apint_insert_bits64(U.pVal, getNumWords(), subBits, bitPosition,
                               numBits);
}

inline APInt APInt::extractBits(unsigned numBits, unsigned bitPosition) const {
  assert(bitPosition < BitWidth && (numBits + bitPosition) <= BitWidth &&
         "Illegal bit extraction");
  if (isSingleWord())
    return APInt(numBits, U.VAL >> bitPosition);
  unsigned loBit = whichBit(bitPosition);
  unsigned loWord = whichWord(bitPosition);
  unsigned hiWord = whichWord(bitPosition + numBits - 1);
  if (loWord == hiWord)
    return APInt(numBits, U.pVal[loWord] >> loBit);
  if (loBit == 0)
    return APInt(numBits, ArrayRef(U.pVal + loWord, 1 + hiWord - loWord));
  APInt Result(numBits, 0);
  uint64_t *DestPtr = Result.isSingleWord() ? &Result.U.VAL : Result.U.pVal;
  csupport_apint_extract_bits(U.pVal, getNumWords(), DestPtr,
                              Result.getNumWords(), numBits, bitPosition);
  return Result.clearUnusedBits();
}

inline uint64_t APInt::extractBitsAsZExtValue(unsigned numBits,
                                              unsigned bitPosition) const {
  assert(bitPosition < BitWidth && (numBits + bitPosition) <= BitWidth &&
         "Illegal bit extraction");
  assert(numBits <= 64 && "Illegal bit extraction");
  if (isSingleWord()) {
    uint64_t maskBits = maskTrailingOnes<uint64_t>(numBits);
    return (U.VAL >> bitPosition) & maskBits;
  }
  return csupport_apint_extract_bits_zext(U.pVal, getNumWords(), numBits,
                                          bitPosition);
}

inline bool APInt::isSplat(unsigned SplatSizeInBits) const {
  assert(getBitWidth() % SplatSizeInBits == 0 &&
         "SplatSizeInBits must divide width!");
  return *this == rotl(SplatSizeInBits);
}

inline APInt APInt::getHiBits(unsigned numBits) const {
  return this->lshr(BitWidth - numBits);
}

inline APInt APInt::getLoBits(unsigned numBits) const {
  APInt Result(getLowBitsSet(BitWidth, numBits));
  Result &= *this;
  return Result;
}

inline APInt APInt::getSplat(unsigned NewLen, const APInt &V) {
  assert(NewLen >= V.getBitWidth() && "Can't splat to smaller bit width!");
  APInt Val = V.zext(NewLen);
  for (unsigned I = V.getBitWidth(); I < NewLen; I <<= 1)
    Val |= Val << I;
  return Val;
}

inline APInt APInt::byteSwap() const {
  assert(BitWidth >= 16 && BitWidth % 8 == 0 && "Cannot byteswap!");
  if (BitWidth == 16)
    return APInt(BitWidth, ::llvm::byteswap<uint16_t>(U.VAL));
  if (BitWidth == 32)
    return APInt(BitWidth, ::llvm::byteswap<uint32_t>(U.VAL));
  if (BitWidth <= 64) {
    uint64_t Tmp1 = ::llvm::byteswap<uint64_t>(U.VAL);
    Tmp1 >>= (64 - BitWidth);
    return APInt(BitWidth, Tmp1);
  }
  APInt Result(BitWidth, 0);
  csupport_apint_byte_swap(Result.U.pVal, U.pVal, BitWidth);
  return Result;
}

inline APInt APInt::reverseBits() const {
  switch (BitWidth) {
  case 64:
    return APInt(BitWidth, ::llvm::reverseBits<uint64_t>(U.VAL));
  case 32:
    return APInt(BitWidth, ::llvm::reverseBits<uint32_t>(U.VAL));
  case 16:
    return APInt(BitWidth, ::llvm::reverseBits<uint16_t>(U.VAL));
  case 8:
    return APInt(BitWidth, ::llvm::reverseBits<uint8_t>(U.VAL));
  case 0:
    return *this;
  default:
    break;
  }
  if (isSingleWord()) {
    uint64_t r = ::llvm::reverseBits<uint64_t>(U.VAL);
    r >>= (64 - BitWidth);
    return APInt(BitWidth, r);
  }
  APInt Result(BitWidth, 0);
  csupport_apint_reverse_bits(Result.U.pVal, U.pVal, BitWidth);
  return Result;
}

// ---------------------------------------------------------------------------
// Conversion / extension / truncation
// ---------------------------------------------------------------------------

inline double APInt::roundToDouble(bool isSigned) const {
  if (isSingleWord() || getActiveBits() <= APINT_BITS_PER_WORD) {
    if (isSigned && BitWidth <= APINT_BITS_PER_WORD) {
      int64_t sext = SignExtend64(getWord(0), BitWidth);
      return double(sext);
    } else
      return double(getWord(0));
  }
  return csupport_apint_round_to_double(U.pVal, getNumWords(), BitWidth,
                                        isSigned);
}

inline APInt APInt::trunc(unsigned width) const {
  assert(width <= BitWidth && "Invalid APInt Truncate request");
  if (width <= APINT_BITS_PER_WORD)
    return APInt(width, getRawData()[0]);
  if (width == BitWidth)
    return *this;
  APInt Result(getMemory(getNumWords(width)), width);
  csupport_apint_trunc_words(Result.U.pVal, U.pVal, Result.getNumWords(),
                             width);
  return Result;
}

inline APInt APInt::truncUSat(unsigned width) const {
  assert(width <= BitWidth && "Invalid APInt Truncate request");
  if (isIntN(width))
    return trunc(width);
  return APInt::getMaxValue(width);
}

inline APInt APInt::truncSSat(unsigned width) const {
  assert(width <= BitWidth && "Invalid APInt Truncate request");
  if (isSignedIntN(width))
    return trunc(width);
  return isNegative() ? APInt::getSignedMinValue(width)
                      : APInt::getSignedMaxValue(width);
}

inline APInt APInt::sext(unsigned Width) const {
  assert(Width >= BitWidth && "Invalid APInt SignExtend request");
  if (Width <= APINT_BITS_PER_WORD)
    return APInt(Width, SignExtend64(U.VAL, BitWidth));
  if (Width == BitWidth)
    return *this;
  APInt Result(getMemory(getNumWords(Width)), Width);
  csupport_apint_sext_words(Result.U.pVal, Result.getNumWords(), getRawData(),
                            getNumWords(), BitWidth);
  Result.clearUnusedBits();
  return Result;
}

inline APInt APInt::zext(unsigned width) const {
  assert(width >= BitWidth && "Invalid APInt ZeroExtend request");
  if (width <= APINT_BITS_PER_WORD)
    return APInt(width, U.VAL);
  if (width == BitWidth)
    return *this;
  APInt Result(getMemory(getNumWords(width)), width);
  memcpy(Result.U.pVal, getRawData(), getNumWords() * APINT_WORD_SIZE);
  memset(Result.U.pVal + getNumWords(), 0,
         (Result.getNumWords() - getNumWords()) * APINT_WORD_SIZE);
  return Result;
}

inline APInt APInt::zextOrTrunc(unsigned width) const {
  if (BitWidth < width)
    return zext(width);
  if (BitWidth > width)
    return trunc(width);
  return *this;
}

inline APInt APInt::sextOrTrunc(unsigned width) const {
  if (BitWidth < width)
    return sext(width);
  if (BitWidth > width)
    return trunc(width);
  return *this;
}

// ---------------------------------------------------------------------------
// Shifts / rotations
// ---------------------------------------------------------------------------

inline void APInt::ashrInPlace(const APInt &shiftAmt) {
  ashrInPlace((unsigned)shiftAmt.getLimitedValue(BitWidth));
}

inline void APInt::lshrInPlace(const APInt &shiftAmt) {
  lshrInPlace((unsigned)shiftAmt.getLimitedValue(BitWidth));
}

inline APInt &APInt::operator<<=(const APInt &shiftAmt) {
  *this <<= (unsigned)shiftAmt.getLimitedValue(BitWidth);
  return *this;
}

namespace detail {
inline unsigned rotateModulo(unsigned BitWidth, const APInt &rotateAmt) {
  if (LLVM_UNLIKELY(BitWidth == 0))
    return 0;
  unsigned rotBitWidth = rotateAmt.getBitWidth();
  APInt rot = rotateAmt;
  if (rotBitWidth < BitWidth)
    rot = rotateAmt.zext(BitWidth);
  rot = rot.urem(APInt(rot.getBitWidth(), BitWidth));
  return rot.getLimitedValue(BitWidth);
}
} // namespace detail

inline APInt APInt::rotl(const APInt &rotateAmt) const {
  return rotl(detail::rotateModulo(BitWidth, rotateAmt));
}

inline APInt APInt::rotl(unsigned rotateAmt) const {
  if (LLVM_UNLIKELY(BitWidth == 0))
    return *this;
  rotateAmt %= BitWidth;
  if (rotateAmt == 0)
    return *this;
  if (isSingleWord())
    return shl(rotateAmt) | lshr(BitWidth - rotateAmt);
  APInt Result(BitWidth, 0);
  csupport_apint_rotl(Result.U.pVal, U.pVal, getNumWords(), BitWidth,
                      rotateAmt);
  return Result;
}

inline APInt APInt::rotr(const APInt &rotateAmt) const {
  return rotr(detail::rotateModulo(BitWidth, rotateAmt));
}

inline APInt APInt::rotr(unsigned rotateAmt) const {
  if (BitWidth == 0)
    return *this;
  rotateAmt %= BitWidth;
  if (rotateAmt == 0)
    return *this;
  if (isSingleWord())
    return lshr(rotateAmt) | shl(BitWidth - rotateAmt);
  APInt Result(BitWidth, 0);
  csupport_apint_rotr(Result.U.pVal, U.pVal, getNumWords(), BitWidth,
                      rotateAmt);
  return Result;
}

// ---------------------------------------------------------------------------
// Division / remainder
// ---------------------------------------------------------------------------

inline APInt APInt::udiv(const APInt &RHS) const {
  assert(BitWidth == RHS.BitWidth && "Bit widths must be the same");
  if (isSingleWord()) {
    assert(RHS.U.VAL != 0 && "Divide by zero?");
    return APInt(BitWidth, U.VAL / RHS.U.VAL);
  }
  unsigned lhsWords = getNumWords(getActiveBits());
  unsigned rhsBits = RHS.getActiveBits();
  unsigned rhsWords = getNumWords(rhsBits);
  assert(rhsWords && "Divided by zero???");
  if (!lhsWords)
    return APInt(BitWidth, 0);
  if (rhsBits == 1)
    return *this;
  if (lhsWords < rhsWords || this->ult(RHS))
    return APInt(BitWidth, 0);
  if (*this == RHS)
    return APInt(BitWidth, 1);
  if (lhsWords == 1)
    return APInt(BitWidth, this->U.pVal[0] / RHS.U.pVal[0]);
  APInt Quotient(BitWidth, 0);
  divide(U.pVal, lhsWords, RHS.U.pVal, rhsWords, Quotient.U.pVal, nullptr);
  return Quotient;
}

inline APInt APInt::udiv(uint64_t RHS) const {
  assert(RHS != 0 && "Divide by zero?");
  if (isSingleWord())
    return APInt(BitWidth, U.VAL / RHS);
  unsigned lhsWords = getNumWords(getActiveBits());
  if (!lhsWords)
    return APInt(BitWidth, 0);
  if (RHS == 1)
    return *this;
  if (this->ult(RHS))
    return APInt(BitWidth, 0);
  if (*this == RHS)
    return APInt(BitWidth, 1);
  if (lhsWords == 1)
    return APInt(BitWidth, this->U.pVal[0] / RHS);
  APInt Quotient(BitWidth, 0);
  divide(U.pVal, lhsWords, &RHS, 1, Quotient.U.pVal, nullptr);
  return Quotient;
}

inline APInt APInt::sdiv(const APInt &RHS) const {
  if (isNegative()) {
    if (RHS.isNegative())
      return (-(*this)).udiv(-RHS);
    return -((-(*this)).udiv(RHS));
  }
  if (RHS.isNegative())
    return -(this->udiv(-RHS));
  return this->udiv(RHS);
}

inline APInt APInt::sdiv(int64_t RHS) const {
  if (isNegative()) {
    if (RHS < 0)
      return (-(*this)).udiv(-RHS);
    return -((-(*this)).udiv(RHS));
  }
  if (RHS < 0)
    return -(this->udiv(-RHS));
  return this->udiv(RHS);
}

inline APInt APInt::urem(const APInt &RHS) const {
  assert(BitWidth == RHS.BitWidth && "Bit widths must be the same");
  if (isSingleWord()) {
    assert(RHS.U.VAL != 0 && "Remainder by zero?");
    return APInt(BitWidth, U.VAL % RHS.U.VAL);
  }
  unsigned lhsWords = getNumWords(getActiveBits());
  unsigned rhsBits = RHS.getActiveBits();
  unsigned rhsWords = getNumWords(rhsBits);
  assert(rhsWords && "Performing remainder operation by zero ???");
  if (lhsWords == 0)
    return APInt(BitWidth, 0);
  if (rhsBits == 1)
    return APInt(BitWidth, 0);
  if (lhsWords < rhsWords || this->ult(RHS))
    return *this;
  if (*this == RHS)
    return APInt(BitWidth, 0);
  if (lhsWords == 1)
    return APInt(BitWidth, U.pVal[0] % RHS.U.pVal[0]);
  APInt Remainder(BitWidth, 0);
  divide(U.pVal, lhsWords, RHS.U.pVal, rhsWords, nullptr, Remainder.U.pVal);
  return Remainder;
}

inline uint64_t APInt::urem(uint64_t RHS) const {
  assert(RHS != 0 && "Remainder by zero?");
  if (isSingleWord())
    return U.VAL % RHS;
  unsigned lhsWords = getNumWords(getActiveBits());
  if (lhsWords == 0)
    return 0;
  if (RHS == 1)
    return 0;
  if (this->ult(RHS))
    return getZExtValue();
  if (*this == RHS)
    return 0;
  if (lhsWords == 1)
    return U.pVal[0] % RHS;
  uint64_t Remainder;
  divide(U.pVal, lhsWords, &RHS, 1, nullptr, &Remainder);
  return Remainder;
}

inline APInt APInt::srem(const APInt &RHS) const {
  if (isNegative()) {
    if (RHS.isNegative())
      return -((-(*this)).urem(-RHS));
    return -((-(*this)).urem(RHS));
  }
  if (RHS.isNegative())
    return this->urem(-RHS);
  return this->urem(RHS);
}

inline int64_t APInt::srem(int64_t RHS) const {
  if (isNegative()) {
    if (RHS < 0)
      return -((-(*this)).urem(-RHS));
    return -((-(*this)).urem(RHS));
  }
  if (RHS < 0)
    return this->urem(-RHS);
  return this->urem(RHS);
}

inline void APInt::udivrem(const APInt &LHS, const APInt &RHS, APInt &Quotient,
                           APInt &Remainder) {
  assert(LHS.BitWidth == RHS.BitWidth && "Bit widths must be the same");
  unsigned BitWidth = LHS.BitWidth;
  if (LHS.isSingleWord()) {
    assert(RHS.U.VAL != 0 && "Divide by zero?");
    uint64_t QuotVal = LHS.U.VAL / RHS.U.VAL;
    uint64_t RemVal = LHS.U.VAL % RHS.U.VAL;
    Quotient = APInt(BitWidth, QuotVal);
    Remainder = APInt(BitWidth, RemVal);
    return;
  }
  unsigned lhsWords = getNumWords(LHS.getActiveBits());
  unsigned rhsBits = RHS.getActiveBits();
  unsigned rhsWords = getNumWords(rhsBits);
  assert(rhsWords && "Performing divrem operation by zero ???");
  if (lhsWords == 0) {
    Quotient = APInt(BitWidth, 0);
    Remainder = APInt(BitWidth, 0);
    return;
  }
  if (rhsBits == 1) {
    Quotient = LHS;
    Remainder = APInt(BitWidth, 0);
  }
  if (lhsWords < rhsWords || LHS.ult(RHS)) {
    Remainder = LHS;
    Quotient = APInt(BitWidth, 0);
    return;
  }
  if (LHS == RHS) {
    Quotient = APInt(BitWidth, 1);
    Remainder = APInt(BitWidth, 0);
    return;
  }
  Quotient.reallocate(BitWidth);
  Remainder.reallocate(BitWidth);
  if (lhsWords == 1) {
    uint64_t lhsValue = LHS.U.pVal[0];
    uint64_t rhsValue = RHS.U.pVal[0];
    Quotient = lhsValue / rhsValue;
    Remainder = lhsValue % rhsValue;
    return;
  }
  divide(LHS.U.pVal, lhsWords, RHS.U.pVal, rhsWords, Quotient.U.pVal,
         Remainder.U.pVal);
  memset(Quotient.U.pVal + lhsWords, 0,
         (getNumWords(BitWidth) - lhsWords) * APINT_WORD_SIZE);
  memset(Remainder.U.pVal + rhsWords, 0,
         (getNumWords(BitWidth) - rhsWords) * APINT_WORD_SIZE);
}

inline void APInt::udivrem(const APInt &LHS, uint64_t RHS, APInt &Quotient,
                           uint64_t &Remainder) {
  assert(RHS != 0 && "Divide by zero?");
  unsigned BitWidth = LHS.BitWidth;
  if (LHS.isSingleWord()) {
    uint64_t QuotVal = LHS.U.VAL / RHS;
    Remainder = LHS.U.VAL % RHS;
    Quotient = APInt(BitWidth, QuotVal);
    return;
  }
  unsigned lhsWords = getNumWords(LHS.getActiveBits());
  if (lhsWords == 0) {
    Quotient = APInt(BitWidth, 0);
    Remainder = 0;
    return;
  }
  if (RHS == 1) {
    Quotient = LHS;
    Remainder = 0;
    return;
  }
  if (LHS.ult(RHS)) {
    Remainder = LHS.getZExtValue();
    Quotient = APInt(BitWidth, 0);
    return;
  }
  if (LHS == RHS) {
    Quotient = APInt(BitWidth, 1);
    Remainder = 0;
    return;
  }
  Quotient.reallocate(BitWidth);
  if (lhsWords == 1) {
    uint64_t lhsValue = LHS.U.pVal[0];
    Quotient = lhsValue / RHS;
    Remainder = lhsValue % RHS;
    return;
  }
  divide(LHS.U.pVal, lhsWords, &RHS, 1, Quotient.U.pVal, &Remainder);
  memset(Quotient.U.pVal + lhsWords, 0,
         (getNumWords(BitWidth) - lhsWords) * APINT_WORD_SIZE);
}

inline void APInt::sdivrem(const APInt &LHS, const APInt &RHS, APInt &Quotient,
                           APInt &Remainder) {
  if (LHS.isNegative()) {
    if (RHS.isNegative())
      APInt::udivrem(-LHS, -RHS, Quotient, Remainder);
    else {
      APInt::udivrem(-LHS, RHS, Quotient, Remainder);
      Quotient.negate();
    }
    Remainder.negate();
  } else if (RHS.isNegative()) {
    APInt::udivrem(LHS, -RHS, Quotient, Remainder);
    Quotient.negate();
  } else {
    APInt::udivrem(LHS, RHS, Quotient, Remainder);
  }
}

inline void APInt::sdivrem(const APInt &LHS, int64_t RHS, APInt &Quotient,
                           int64_t &Remainder) {
  uint64_t R = Remainder;
  if (LHS.isNegative()) {
    if (RHS < 0)
      APInt::udivrem(-LHS, -RHS, Quotient, R);
    else {
      APInt::udivrem(-LHS, RHS, Quotient, R);
      Quotient.negate();
    }
    R = -R;
  } else if (RHS < 0) {
    APInt::udivrem(LHS, -RHS, Quotient, R);
    Quotient.negate();
  } else {
    APInt::udivrem(LHS, RHS, Quotient, R);
  }
  Remainder = R;
}

// ---------------------------------------------------------------------------
// Overflow-checking arithmetic
// ---------------------------------------------------------------------------

inline APInt APInt::sadd_ov(const APInt &RHS, bool &Overflow) const {
  APInt Res = *this + RHS;
  Overflow = isNonNegative() == RHS.isNonNegative() &&
             Res.isNonNegative() != isNonNegative();
  return Res;
}

inline APInt APInt::uadd_ov(const APInt &RHS, bool &Overflow) const {
  APInt Res = *this + RHS;
  Overflow = Res.ult(RHS);
  return Res;
}

inline APInt APInt::ssub_ov(const APInt &RHS, bool &Overflow) const {
  APInt Res = *this - RHS;
  Overflow = isNonNegative() != RHS.isNonNegative() &&
             Res.isNonNegative() != isNonNegative();
  return Res;
}

inline APInt APInt::usub_ov(const APInt &RHS, bool &Overflow) const {
  APInt Res = *this - RHS;
  Overflow = Res.ugt(*this);
  return Res;
}

inline APInt APInt::sdiv_ov(const APInt &RHS, bool &Overflow) const {
  Overflow = isMinSignedValue() && RHS.isAllOnes();
  return sdiv(RHS);
}

inline APInt APInt::smul_ov(const APInt &RHS, bool &Overflow) const {
  APInt Res = *this * RHS;
  if (RHS != 0)
    Overflow =
        Res.sdiv(RHS) != *this || (isMinSignedValue() && RHS.isAllOnes());
  else
    Overflow = false;
  return Res;
}

inline APInt APInt::umul_ov(const APInt &RHS, bool &Overflow) const {
  if (countl_zero() + RHS.countl_zero() + 2 <= BitWidth) {
    Overflow = true;
    return *this * RHS;
  }
  APInt Res = lshr(1) * RHS;
  Overflow = Res.isNegative();
  Res <<= 1;
  if ((*this)[0]) {
    Res += RHS;
    if (Res.ult(RHS))
      Overflow = true;
  }
  return Res;
}

inline APInt APInt::sshl_ov(const APInt &ShAmt, bool &Overflow) const {
  return sshl_ov(ShAmt.getLimitedValue(getBitWidth()), Overflow);
}

inline APInt APInt::sshl_ov(unsigned ShAmt, bool &Overflow) const {
  Overflow = ShAmt >= getBitWidth();
  if (Overflow)
    return APInt(BitWidth, 0);
  if (isNonNegative())
    Overflow = ShAmt >= countl_zero();
  else
    Overflow = ShAmt >= countl_one();
  return *this << ShAmt;
}

inline APInt APInt::ushl_ov(const APInt &ShAmt, bool &Overflow) const {
  return ushl_ov(ShAmt.getLimitedValue(getBitWidth()), Overflow);
}

inline APInt APInt::ushl_ov(unsigned ShAmt, bool &Overflow) const {
  Overflow = ShAmt >= getBitWidth();
  if (Overflow)
    return APInt(BitWidth, 0);
  Overflow = ShAmt > countl_zero();
  return *this << ShAmt;
}

// ---------------------------------------------------------------------------
// Saturating arithmetic
// ---------------------------------------------------------------------------

inline APInt APInt::sadd_sat(const APInt &RHS) const {
  bool Overflow;
  APInt Res = sadd_ov(RHS, Overflow);
  if (!Overflow)
    return Res;
  return isNegative() ? APInt::getSignedMinValue(BitWidth)
                      : APInt::getSignedMaxValue(BitWidth);
}

inline APInt APInt::uadd_sat(const APInt &RHS) const {
  bool Overflow;
  APInt Res = uadd_ov(RHS, Overflow);
  if (!Overflow)
    return Res;
  return APInt::getMaxValue(BitWidth);
}

inline APInt APInt::ssub_sat(const APInt &RHS) const {
  bool Overflow;
  APInt Res = ssub_ov(RHS, Overflow);
  if (!Overflow)
    return Res;
  return isNegative() ? APInt::getSignedMinValue(BitWidth)
                      : APInt::getSignedMaxValue(BitWidth);
}

inline APInt APInt::usub_sat(const APInt &RHS) const {
  bool Overflow;
  APInt Res = usub_ov(RHS, Overflow);
  if (!Overflow)
    return Res;
  return APInt(BitWidth, 0);
}

inline APInt APInt::smul_sat(const APInt &RHS) const {
  bool Overflow;
  APInt Res = smul_ov(RHS, Overflow);
  if (!Overflow)
    return Res;
  bool ResIsNegative = isNegative() ^ RHS.isNegative();
  return ResIsNegative ? APInt::getSignedMinValue(BitWidth)
                       : APInt::getSignedMaxValue(BitWidth);
}

inline APInt APInt::umul_sat(const APInt &RHS) const {
  bool Overflow;
  APInt Res = umul_ov(RHS, Overflow);
  if (!Overflow)
    return Res;
  return APInt::getMaxValue(BitWidth);
}

inline APInt APInt::sshl_sat(const APInt &RHS) const {
  return sshl_sat(RHS.getLimitedValue(getBitWidth()));
}

inline APInt APInt::sshl_sat(unsigned RHS) const {
  bool Overflow;
  APInt Res = sshl_ov(RHS, Overflow);
  if (!Overflow)
    return Res;
  return isNegative() ? APInt::getSignedMinValue(BitWidth)
                      : APInt::getSignedMaxValue(BitWidth);
}

inline APInt APInt::ushl_sat(const APInt &RHS) const {
  return ushl_sat(RHS.getLimitedValue(getBitWidth()));
}

inline APInt APInt::ushl_sat(unsigned RHS) const {
  bool Overflow;
  APInt Res = ushl_ov(RHS, Overflow);
  if (!Overflow)
    return Res;
  return APInt::getMaxValue(BitWidth);
}

// ---------------------------------------------------------------------------
// String conversion
// ---------------------------------------------------------------------------

inline unsigned APInt::getSufficientBitsNeeded(StringRef Str, uint8_t Radix) {
  return csupport_apint_sufficient_bits(Str.data(), Str.size(), Radix);
}

inline unsigned APInt::getBitsNeeded(StringRef str, uint8_t radix) {
  unsigned sufficient = getSufficientBitsNeeded(str, radix);
  if (radix == 2 || radix == 8 || radix == 16)
    return sufficient;
  size_t slen = str.size();
  StringRef::iterator p = str.begin();
  unsigned isNegative = *p == '-';
  if (*p == '-' || *p == '+') {
    p++;
    slen--;
    assert(slen && "String is only a sign, needs a value.");
  }
  APInt tmp(sufficient, StringRef(p, slen), radix);
  unsigned log = tmp.logBase2();
  if (log == (unsigned)-1) {
    return isNegative + 1;
  } else if (isNegative && tmp.isPowerOf2()) {
    return isNegative + log;
  } else {
    return isNegative + log + 1;
  }
}

inline void APInt::fromString(unsigned numbits, StringRef str, uint8_t radix) {
  assert(!str.empty() && "Invalid string length");
  assert(
      (radix == 10 || radix == 8 || radix == 16 || radix == 2 || radix == 36) &&
      "Radix should be 2, 8, 10, 16, or 36!");
  if (isSingleWord())
    U.VAL = 0;
  else
    U.pVal = getClearedMemory(getNumWords());
  uint64_t *dst = isSingleWord() ? &U.VAL : U.pVal;
  csupport_apint_from_string(dst, numbits, str.data(), str.size(), radix);
  clearUnusedBits();
}

inline void APInt::toString(SmallVectorImpl<char> &Str, unsigned Radix,
                            bool Signed, bool formatAsCLiteral,
                            bool UpperCase) const {
  assert(
      (Radix == 10 || Radix == 8 || Radix == 16 || Radix == 2 || Radix == 36) &&
      "Radix should be 2, 8, 10, 16, or 36!");
  const char *Prefix = "";
  if (formatAsCLiteral) {
    switch (Radix) {
    case 2:
      Prefix = "0b";
      break;
    case 8:
      Prefix = "0";
      break;
    case 10:
      break;
    case 16:
      Prefix = "0x";
      break;
    default:
      llvm_unreachable("Invalid radix!");
    }
  }
  if (isZero()) {
    while (*Prefix)
      Str.push_back(*Prefix++);
    Str.push_back('0');
    return;
  }
  static const char BothDigits[] = "0123456789abcdefghijklmnopqrstuvwxyz"
                                   "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  const char *Digits = BothDigits + (UpperCase ? 36 : 0);
  if (isSingleWord()) {
    char Buffer[65];
    char *BufPtr = Buffer + 65;
    uint64_t N;
    if (!Signed) {
      N = getZExtValue();
    } else {
      int64_t I = getSExtValue();
      if (I >= 0) {
        N = I;
      } else {
        Str.push_back('-');
        N = -(uint64_t)I;
      }
    }
    while (*Prefix)
      Str.push_back(*Prefix++);
    while (N) {
      *--BufPtr = Digits[N % Radix];
      N /= Radix;
    }
    Str.append(BufPtr, Buffer + 65);
    return;
  }
  char buf[4096];
  size_t len = 0;
  csupport_apint_to_string(getRawData(), BitWidth, Signed ? 1 : 0, Radix, buf,
                           sizeof(buf), &len);
  size_t start = 0;
  if (len > 0 && buf[0] == '-') {
    Str.push_back('-');
    start = 1;
  }
  while (*Prefix)
    Str.push_back(*Prefix++);
  if (!UpperCase) {
    for (size_t i = start; i < len; i++)
      Str.push_back(buf[i] >= 'A' && buf[i] <= 'Z' ? buf[i] + 32 : buf[i]);
  } else {
    Str.append(buf + start, buf + len);
  }
}

// ---------------------------------------------------------------------------
// Misc: nearestLogBase2, sqrt, multiplicativeInverse
// ---------------------------------------------------------------------------

inline unsigned APInt::nearestLogBase2() const {
  if (BitWidth == 1)
    return U.VAL - 1;
  if (isZero())
    return UINT32_MAX;
  unsigned lg = logBase2();
  return lg + unsigned((*this)[lg - 1]);
}

inline APInt APInt::sqrt() const {
  unsigned magnitude = getActiveBits();
  if (magnitude <= 5) {
    static const uint8_t results[32] = {
        /*     0 */ 0,
        /*  1- 2 */ 1, 1,
        /*  3- 6 */ 2, 2, 2, 2,
        /*  7-12 */ 3, 3, 3, 3, 3, 3,
        /* 13-20 */ 4, 4, 4, 4, 4, 4, 4, 4,
        /* 21-30 */ 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
        /*    31 */ 6};
    return APInt(BitWidth, results[(isSingleWord() ? U.VAL : U.pVal[0])]);
  }
  if (magnitude < 52) {
    return APInt(
        BitWidth,
        uint64_t(std::round(std::sqrt(double(isSingleWord() ? U.VAL : U.pVal[0])))));
  }
  unsigned nbits = BitWidth, i = 4;
  APInt testy(BitWidth, 16);
  APInt x_old(BitWidth, 1);
  APInt x_new(BitWidth, 0);
  APInt two(BitWidth, 2);
  for (;; i += 2, testy = testy.shl(2))
    if (i >= nbits || this->ule(testy)) {
      x_old = x_old.shl(i / 2);
      break;
    }
  for (;;) {
    x_new = (this->udiv(x_old) + x_old).udiv(two);
    if (x_old.ule(x_new))
      break;
    x_old = x_new;
  }
  APInt square(x_old * x_old);
  APInt nextSquare((x_old + 1) * (x_old + 1));
  if (this->ult(square))
    return x_old;
  assert(this->ule(nextSquare) && "Error in APInt::sqrt computation");
  APInt midpoint((nextSquare - square).udiv(two));
  APInt offset(*this - square);
  if (offset.ult(midpoint))
    return x_old;
  return x_old + 1;
}

inline APInt APInt::multiplicativeInverse(const APInt &modulo) const {
  assert(ult(modulo) && "This APInt must be smaller than the modulo");
  APInt r[2] = {modulo, *this};
  APInt t[2] = {APInt(BitWidth, 0), APInt(BitWidth, 1)};
  APInt q(BitWidth, 0);
  unsigned i;
  for (i = 0; r[i ^ 1] != 0; i ^= 1) {
    udivrem(r[i], r[i ^ 1], q, r[i]);
    t[i] -= t[i ^ 1] * q;
  }
  if (r[i] != 1)
    return APInt(BitWidth, 0);
  if (t[i].isNegative())
    t[i] += modulo;
  return t[i];
}

// ---------------------------------------------------------------------------
// Out-of-line inline definitions (moved from cpp_bridge.cpp)
// ---------------------------------------------------------------------------

inline void APInt::Profile(FoldingSetNodeID &ID) const {
  ID.AddInteger(BitWidth);
  if (isSingleWord()) {
    ID.AddInteger(U.VAL);
    return;
  }
  unsigned NumWords = getNumWords();
  for (unsigned i = 0; i < NumWords; ++i)
    ID.AddInteger(U.pVal[i]);
}

inline bool APInt::isAligned(Align A) const {
  if (isZero())
    return true;
  const unsigned TrailingZeroes = countr_zero();
  const unsigned MinimumTrailingZeroes = Log2(A);
  return TrailingZeroes >= MinimumTrailingZeroes;
}

inline hash_code hash_value(const APInt &Arg) {
  if (Arg.isSingleWord())
    return hash_combine(Arg.BitWidth, Arg.U.VAL);
  return hash_combine(
      Arg.BitWidth,
      hash_combine_range(Arg.U.pVal, Arg.U.pVal + Arg.getNumWords()));
}

inline void APInt::print(raw_ostream &OS, bool isSigned) const {
  SmallString<40> S;
  this->toString(S, 10, isSigned, false);
  OS << S;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD inline void APInt::dump() const {
  SmallString<40> S, U;
  this->toStringUnsigned(U);
  this->toStringSigned(S);
  errs() << "APInt(" << BitWidth << "b, " << U << "u " << S << "s)\n";
}
#endif

namespace APIntOps {

inline APInt GreatestCommonDivisor(APInt A, APInt B) {
  if (A == B)
    return A;
  if (!A)
    return B;
  if (!B)
    return A;
  if (A.isSingleWord()) {
    uint64_t a = A.getZExtValue(), b = B.getZExtValue();
    while (b) {
      uint64_t t = b;
      b = a % b;
      a = t;
    }
    return APInt(A.getBitWidth(), a);
  }
  unsigned nw = A.getNumWords();
  uint64_t buf[64];
  assert(nw <= 64);
  csupport_apint_gcd(buf, A.getRawData(), B.getRawData(), nw, A.getBitWidth());
  return APInt(A.getBitWidth(), ArrayRef(buf, nw));
}

inline APInt RoundDoubleToAPInt(double Double, unsigned width) {
  uint64_t I = bit_cast<uint64_t>(Double);
  bool isNeg = I >> 63;
  int64_t exp = ((I >> 52) & 0x7ff) - 1023;
  if (exp < 0)
    return APInt(width, 0u);
  uint64_t mantissa = (I & (~0ULL >> 12)) | 1ULL << 52;
  if (exp < 52)
    return isNeg ? -APInt(width, mantissa >> (52 - exp))
                 : APInt(width, mantissa >> (52 - exp));
  if (width <= exp - 52)
    return APInt(width, 0);
  APInt Tmp(width, mantissa);
  Tmp <<= (unsigned)exp - 52;
  return isNeg ? -Tmp : Tmp;
}

inline APInt RoundingUDiv(const APInt &A, const APInt &B, APInt::Rounding RM) {
  switch (RM) {
  case APInt::Rounding::DOWN:
  case APInt::Rounding::TOWARD_ZERO:
    return A.udiv(B);
  case APInt::Rounding::UP: {
    APInt Quo, Rem;
    APInt::udivrem(A, B, Quo, Rem);
    if (Rem.isZero())
      return Quo;
    return Quo + 1;
  }
  }
  llvm_unreachable("Unknown APInt::Rounding enum");
}

inline APInt RoundingSDiv(const APInt &A, const APInt &B, APInt::Rounding RM) {
  switch (RM) {
  case APInt::Rounding::DOWN:
  case APInt::Rounding::UP: {
    APInt Quo, Rem;
    APInt::sdivrem(A, B, Quo, Rem);
    if (Rem.isZero())
      return Quo;
    if (RM == APInt::Rounding::DOWN) {
      if (Rem.isNegative() != B.isNegative())
        return Quo - 1;
      return Quo;
    }
    if (Rem.isNegative() != B.isNegative())
      return Quo;
    return Quo + 1;
  }
  case APInt::Rounding::TOWARD_ZERO:
    return A.sdiv(B);
  }
  llvm_unreachable("Unknown APInt::Rounding enum");
}

inline unsigned GetMostSignificantDifferentBit(const APInt &A, const APInt &B) {
  assert(A.getBitWidth() == B.getBitWidth() && "Must have the same bitwidth");
  if (A == B)
    return UINT_MAX;
  return A.getBitWidth() - ((A ^ B).countl_zero() + 1);
}

inline APInt ScaleBitMask(const APInt &A, unsigned NewBitWidth,
                          bool MatchAllBits) {
  unsigned OldBitWidth = A.getBitWidth();
  assert((((OldBitWidth % NewBitWidth) == 0) ||
          ((NewBitWidth % OldBitWidth) == 0)) &&
         "One size should be a multiple of the other one. "
         "Can't do fractional scaling.");
  if (OldBitWidth == NewBitWidth)
    return A;
  APInt NewA = APInt::getZero(NewBitWidth);
  if (A.isZero())
    return NewA;
  if (NewBitWidth > OldBitWidth) {
    unsigned Scale = NewBitWidth / OldBitWidth;
    for (unsigned i = 0; i != OldBitWidth; ++i)
      if (A[i])
        NewA.setBits(i * Scale, (i + 1) * Scale);
  } else {
    unsigned Scale = OldBitWidth / NewBitWidth;
    for (unsigned i = 0; i != NewBitWidth; ++i) {
      if (MatchAllBits) {
        if (A.extractBits(Scale, i * Scale).isAllOnes())
          NewA.setBit(i);
      } else {
        if (!A.extractBits(Scale, i * Scale).isZero())
          NewA.setBit(i);
      }
    }
  }
  return NewA;
}

inline APInt SolveQuadraticEquationWrap(APInt A, APInt B, APInt C,
                                        unsigned RangeWidth) {
  unsigned CoeffWidth = A.getBitWidth();
  assert(CoeffWidth == B.getBitWidth() && CoeffWidth == C.getBitWidth());
  assert(RangeWidth <= CoeffWidth &&
         "Value range width should be less than coefficient width");
  assert(RangeWidth > 1 && "Value range bit width should be > 1");
  if (C.sextOrTrunc(RangeWidth).isZero())
    return APInt(CoeffWidth, 0);
  CoeffWidth *= 3;
  A = A.sext(CoeffWidth);
  B = B.sext(CoeffWidth);
  C = C.sext(CoeffWidth);
  if (A.isNegative()) {
    A.negate();
    B.negate();
    C.negate();
  }
  APInt R = APInt::getOneBitSet(CoeffWidth, RangeWidth);
  APInt TwoA = 2 * A;
  APInt SqrB = B * B;
  bool PickLow;
  auto RoundUp = [](const APInt &V, const APInt &A) -> APInt {
    assert(A.isStrictlyPositive());
    APInt T = V.abs().urem(A);
    if (T.isZero())
      return V;
    return V.isNegative() ? V + T : V + (A - T);
  };
  if (B.isNonNegative()) {
    C = C.srem(R);
    if (C.isStrictlyPositive())
      C -= R;
    PickLow = false;
  } else {
    APInt LowkR = C - SqrB.udiv(2 * TwoA);
    LowkR = RoundUp(LowkR, R);
    if (C.sgt(LowkR)) {
      C -= -RoundUp(-C, R);
      PickLow = true;
    } else {
      C -= LowkR;
      PickLow = false;
    }
  }
  APInt D = SqrB - 4 * A * C;
  assert(D.isNonNegative() && "Negative discriminant");
  APInt SQ = D.sqrt();
  APInt Q = SQ * SQ;
  bool InexactSQ = Q != D;
  if (Q.sgt(D))
    SQ -= 1;
  APInt X;
  APInt Rem;
  if (PickLow)
    APInt::sdivrem(-B - (SQ + InexactSQ), TwoA, X, Rem);
  else
    APInt::sdivrem(-B + SQ, TwoA, X, Rem);
  assert(X.isNonNegative() && "Solution should be non-negative");
  if (!InexactSQ && Rem.isZero())
    return X;
  assert((SQ * SQ).sle(D) && "SQ = |_sqrt(D)_|, so SQ*SQ <= D");
  APInt VX = (A * X + B) * X + C;
  APInt VY = VX + TwoA * X + A + B;
  bool SignChange =
      VX.isNegative() != VY.isNegative() || VX.isZero() != VY.isZero();
  if (!SignChange)
    return APInt();
  X += 1;
  return X;
}

} // namespace APIntOps

inline void StoreIntToMemory(const APInt &IntVal, uint8_t *Dst,
                             unsigned StoreBytes) {
  assert((IntVal.getBitWidth() + 7) / 8 >= StoreBytes && "Integer too small!");
  const uint8_t *Src = (const uint8_t *)IntVal.getRawData();
  if (sys::IsLittleEndianHost) {
    memcpy(Dst, Src, StoreBytes);
  } else {
    while (StoreBytes > sizeof(uint64_t)) {
      StoreBytes -= sizeof(uint64_t);
      memcpy(Dst + StoreBytes, Src, sizeof(uint64_t));
      Src += sizeof(uint64_t);
    }
    memcpy(Dst, Src + sizeof(uint64_t) - StoreBytes, StoreBytes);
  }
}

inline void LoadIntFromMemory(APInt &IntVal, const uint8_t *Src,
                              unsigned LoadBytes) {
  assert((IntVal.getBitWidth() + 7) / 8 >= LoadBytes && "Integer too small!");
  uint8_t *Dst = const_cast<uint8_t *>(
      reinterpret_cast<const uint8_t *>(IntVal.getRawData()));
  if (sys::IsLittleEndianHost)
    memcpy(Dst, Src, LoadBytes);
  else {
    while (LoadBytes > sizeof(uint64_t)) {
      LoadBytes -= sizeof(uint64_t);
      memcpy(Dst, Src + LoadBytes, sizeof(uint64_t));
      Dst += sizeof(uint64_t);
    }
    memcpy(Dst + sizeof(uint64_t) - LoadBytes, Src, LoadBytes);
  }
}

namespace {
inline unsigned stringRefAutoSenseRadixForConsume(StringRef &Str) {
  const char *p = Str.data();
  size_t n = Str.size();
  unsigned r = csupport_auto_sense_radix(&p, &n);
  Str = StringRef(p, n);
  return r;
}
} // namespace

inline bool StringRef::consumeInteger(unsigned Radix, APInt &Result) {
  StringRef Str = *this;

  if (Radix == 0)
    Radix = stringRefAutoSenseRadixForConsume(Str);

  assert(Radix > 1 && Radix <= 36);

  if (Str.empty())
    return true;

  while (!Str.empty() && Str.front() == '0')
    Str = Str.substr(1);

  if (Str.empty()) {
    Result = APInt(64, 0);
    *this = Str;
    return false;
  }

  unsigned Log2Radix = 0;
  while ((1U << Log2Radix) < Radix)
    Log2Radix++;
  bool IsPowerOf2Radix = ((1U << Log2Radix) == Radix);

  unsigned BitWidth = Log2Radix * Str.size();
  if (BitWidth < Result.getBitWidth())
    BitWidth = Result.getBitWidth();
  else if (BitWidth > Result.getBitWidth())
    Result = Result.zext(BitWidth);

  APInt RadixAP, CharAP;
  if (!IsPowerOf2Radix) {
    RadixAP = APInt(BitWidth, Radix);
    CharAP = APInt(BitWidth, 0);
  }

  Result = 0;
  while (!Str.empty()) {
    unsigned CharVal;
    if (Str[0] >= '0' && Str[0] <= '9')
      CharVal = Str[0] - '0';
    else if (Str[0] >= 'a' && Str[0] <= 'z')
      CharVal = Str[0] - 'a' + 10;
    else if (Str[0] >= 'A' && Str[0] <= 'Z')
      CharVal = Str[0] - 'A' + 10;
    else
      break;

    if (CharVal >= Radix)
      break;

    if (IsPowerOf2Radix) {
      Result <<= Log2Radix;
      Result |= CharVal;
    } else {
      Result *= RadixAP;
      CharAP = CharVal;
      Result += CharAP;
    }

    Str = Str.substr(1);
  }

  if (size() == Str.size())
    return true;

  *this = Str;
  return false;
}

inline bool StringRef::getAsInteger(unsigned Radix, APInt &Result) const {
  StringRef Str = *this;
  if (Str.consumeInteger(Radix, Result))
    return true;

  return !Str.empty();
}

} // namespace llvm

#endif
