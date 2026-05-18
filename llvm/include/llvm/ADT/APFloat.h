//===- llvm/ADT/APFloat.h - Arbitrary Precision Floating Point ---*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares a class to represent arbitrary precision floating point
/// values and provide a variety of arithmetic operations on them.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_APFLOAT_H
#define LLVM_ADT_APFLOAT_H

#include "csupport/la_lp_lfloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/FloatingPointMode.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include <memory>

#define APFLOAT_DISPATCH_ON_SEMANTICS(METHOD_CALL)                             \
  do {                                                                         \
    if (usesLayout<IEEEFloat>(getSemantics()))                                 \
      return U.IEEE.METHOD_CALL;                                               \
    if (usesLayout<DoubleAPFloat>(getSemantics()))                             \
      return U.Double.METHOD_CALL;                                             \
    llvm_unreachable("Unexpected semantics");                                  \
  } while (false)

namespace llvm {

struct fltSemantics;
class APSInt;
class StringRef;
class APFloat;
class raw_ostream;

template <typename T> class Expected;
template <typename T> class SmallVectorImpl;

/// Enum that represents what fraction of the LSB truncated bits of an fp number
/// represent.
///
/// This essentially combines the roles of guard and sticky bits.
enum lostFraction { // Example of truncated bits:
  lfExactlyZero,    // 000000
  lfLessThanHalf,   // 0xxxxx  x's not all zero
  lfExactlyHalf,    // 100000
  lfMoreThanHalf    // 1xxxxx  x's not all zero
};

/// A self-contained host- and target-independent arbitrary-precision
/// floating-point software implementation.
///
/// APFloat uses bignum integer arithmetic as provided by static functions in
/// the APInt class.  The library will work with bignum integers whose parts are
/// any unsigned type at least 16 bits wide, but 64 bits is recommended.
///
/// Written for clarity rather than speed, in particular with a view to use in
/// the front-end of a cross compiler so that target arithmetic can be correctly
/// performed on the host.  Performance should nonetheless be reasonable,
/// particularly for its intended use.  It may be useful as a base
/// implementation for a run-time library during development of a faster
/// target-specific one.
///
/// All 5 rounding modes in the IEEE-754R draft are handled correctly for all
/// implemented operations.  Currently implemented operations are add, subtract,
/// multiply, divide, fused-multiply-add, conversion-to-float,
/// conversion-to-integer and conversion-from-integer.  New rounding modes
/// (e.g. away from zero) can be added with three or four lines of code.
///
/// Four formats are built-in: IEEE single precision, double precision,
/// quadruple precision, and x87 80-bit extended double (when operating with
/// full extended precision).  Adding a new format that obeys IEEE semantics
/// only requires adding two lines of code: a declaration and definition of the
/// format.
///
/// All operations return the status of that operation as an exception bit-mask,
/// so multiple operations can be done consecutively with their results or-ed
/// together.  The returned status can be useful for compiler diagnostics; e.g.,
/// inexact, underflow and overflow can be easily diagnosed on constant folding,
/// and compiler optimizers can determine what exceptions would be raised by
/// folding operations and optimize, or perhaps not optimize, accordingly.
///
/// At present, underflow tininess is detected after rounding; it should be
/// straight forward to add support for the before-rounding case too.
///
/// The library reads hexadecimal floating point numbers as per C99, and
/// correctly rounds if necessary according to the specified rounding mode.
/// Syntax is required to have been validated by the caller.  It also converts
/// floating point numbers to hexadecimal text as per the C99 %a and %A
/// conversions.  The output precision (or alternatively the natural minimal
/// precision) can be specified; if the requested precision is less than the
/// natural precision the output is correctly rounded for the specified rounding
/// mode.
///
/// It also reads decimal floating point numbers and correctly rounds according
/// to the specified rounding mode.
///
/// Conversion to decimal text is not currently implemented.
///
/// Non-zero finite numbers are represented internally as a sign bit, a 16-bit
/// signed exponent, and the significand as an array of integer parts.  After
/// normalization of a number of precision P the exponent is within the range of
/// the format, and if the number is not denormal the P-th bit of the
/// significand is set as an explicit integer bit.  For denormals the most
/// significant bit is shifted right so that the exponent is maintained at the
/// format's minimum, so that the smallest denormal has just the least
/// significant bit of the significand set.  The sign of zeroes and infinities
/// is significant; the exponent and significand of such numbers is not stored,
/// but has a known implicit (deterministic) value: 0 for the significands, 0
/// for zero exponent, all 1 bits for infinity exponent.  For NaNs the sign and
/// significand are deterministic, although not really meaningful, and preserved
/// in non-conversion operations.  The exponent is implicitly all 1 bits.
///
/// APFloat does not provide any exception handling beyond default exception
/// handling. We represent Signaling NaNs via IEEE-754R 2008 6.2.1 should clause
/// by encoding Signaling NaNs with the first bit of its trailing significand as
/// 0.
///
/// TODO
/// ====
///
/// Some features that may or may not be worth adding:
///
/// Binary to decimal conversion (hard).
///
/// Optional ability to detect underflow tininess before rounding.
///
/// New formats: x87 in single and double precision mode (IEEE apart from
/// extended exponent range) (hard).
///
/// New operations: sqrt, IEEE remainder, C90 fmod, nexttoward.
///

// This is the common type definitions shared by APFloat and its internal
// implementation classes. This struct should not define any non-static data
// members.
struct APFloatBase {
  typedef APInt::WordType integerPart;
  static constexpr unsigned integerPartWidth = APInt::APINT_BITS_PER_WORD;

  /// A signed type to represent a floating point numbers unbiased exponent.
  typedef int32_t ExponentType;

  /// \name Floating Point Semantics.
  /// @{
  enum Semantics {
    S_IEEEhalf,
    S_BFloat,
    S_IEEEsingle,
    S_IEEEdouble,
    S_IEEEquad,
    S_PPCDoubleDouble,
    // 8-bit floating point number following IEEE-754 conventions with bit
    // layout S1E5M2 as described in https://arxiv.org/abs/2209.05433.
    S_Float8E5M2,
    // 8-bit floating point number mostly following IEEE-754 conventions
    // and bit layout S1E5M2 described in https://arxiv.org/abs/2206.02915,
    // with expanded range and with no infinity or signed zero.
    // NaN is represented as negative zero. (FN -> Finite, UZ -> unsigned zero).
    // This format's exponent bias is 16, instead of the 15 (2 ** (5 - 1) - 1)
    // that IEEE precedent would imply.
    S_Float8E5M2FNUZ,
    // 8-bit floating point number mostly following IEEE-754 conventions with
    // bit layout S1E4M3 as described in https://arxiv.org/abs/2209.05433.
    // Unlike IEEE-754 types, there are no infinity values, and NaN is
    // represented with the exponent and mantissa bits set to all 1s.
    S_Float8E4M3FN,
    // 8-bit floating point number mostly following IEEE-754 conventions
    // and bit layout S1E4M3 described in https://arxiv.org/abs/2206.02915,
    // with expanded range and with no infinity or signed zero.
    // NaN is represented as negative zero. (FN -> Finite, UZ -> unsigned zero).
    // This format's exponent bias is 8, instead of the 7 (2 ** (4 - 1) - 1)
    // that IEEE precedent would imply.
    S_Float8E4M3FNUZ,
    // 8-bit floating point number mostly following IEEE-754 conventions
    // and bit layout S1E4M3 with expanded range and with no infinity or signed
    // zero.
    // NaN is represented as negative zero. (FN -> Finite, UZ -> unsigned zero).
    // This format's exponent bias is 11, instead of the 7 (2 ** (4 - 1) - 1)
    // that IEEE precedent would imply.
    S_Float8E4M3B11FNUZ,
    // Floating point number that occupies 32 bits or less of storage, providing
    // improved range compared to half (16-bit) formats, at (potentially)
    // greater throughput than single precision (32-bit) formats.
    S_FloatTF32,

    S_x87DoubleExtended,
    S_MaxSemantics = S_x87DoubleExtended,
  };

  static const llvm::fltSemantics &EnumToSemantics(Semantics S) {
    switch (S) {
    case S_IEEEhalf:
      return IEEEhalf();
    case S_BFloat:
      return BFloat();
    case S_IEEEsingle:
      return IEEEsingle();
    case S_IEEEdouble:
      return IEEEdouble();
    case S_IEEEquad:
      return IEEEquad();
    case S_PPCDoubleDouble:
      return PPCDoubleDouble();
    case S_Float8E5M2:
      return Float8E5M2();
    case S_Float8E5M2FNUZ:
      return Float8E5M2FNUZ();
    case S_Float8E4M3FN:
      return Float8E4M3FN();
    case S_Float8E4M3FNUZ:
      return Float8E4M3FNUZ();
    case S_Float8E4M3B11FNUZ:
      return Float8E4M3B11FNUZ();
    case S_FloatTF32:
      return FloatTF32();
    case S_x87DoubleExtended:
      return x87DoubleExtended();
    }
    llvm_unreachable("Unrecognised floating semantics");
  }
  static Semantics SemanticsToEnum(const llvm::fltSemantics &Sem) {
    if (&Sem == &IEEEhalf())
      return S_IEEEhalf;
    if (&Sem == &BFloat())
      return S_BFloat;
    if (&Sem == &IEEEsingle())
      return S_IEEEsingle;
    if (&Sem == &IEEEdouble())
      return S_IEEEdouble;
    if (&Sem == &IEEEquad())
      return S_IEEEquad;
    if (&Sem == &PPCDoubleDouble())
      return S_PPCDoubleDouble;
    if (&Sem == &Float8E5M2())
      return S_Float8E5M2;
    if (&Sem == &Float8E5M2FNUZ())
      return S_Float8E5M2FNUZ;
    if (&Sem == &Float8E4M3FN())
      return S_Float8E4M3FN;
    if (&Sem == &Float8E4M3FNUZ())
      return S_Float8E4M3FNUZ;
    if (&Sem == &Float8E4M3B11FNUZ())
      return S_Float8E4M3B11FNUZ;
    if (&Sem == &FloatTF32())
      return S_FloatTF32;
    if (&Sem == &x87DoubleExtended())
      return S_x87DoubleExtended;
    llvm_unreachable("Unknown floating semantics");
  }

  static const fltSemantics &IEEEhalf() {
    return reinterpret_cast<const fltSemantics &>(csupport_sem_ieee_half);
  }
  static const fltSemantics &BFloat() {
    return reinterpret_cast<const fltSemantics &>(csupport_sem_bfloat);
  }
  static const fltSemantics &IEEEsingle() {
    return reinterpret_cast<const fltSemantics &>(csupport_sem_ieee_single);
  }
  static const fltSemantics &IEEEdouble() {
    return reinterpret_cast<const fltSemantics &>(csupport_sem_ieee_double);
  }
  static const fltSemantics &IEEEquad() {
    return reinterpret_cast<const fltSemantics &>(csupport_sem_ieee_quad);
  }
  static const fltSemantics &PPCDoubleDouble() {
    return reinterpret_cast<const fltSemantics &>(
        csupport_sem_ppc_double_double);
  }
  static const fltSemantics &Float8E5M2() {
    return reinterpret_cast<const fltSemantics &>(csupport_sem_float8_e5m2);
  }
  static const fltSemantics &Float8E5M2FNUZ() {
    return reinterpret_cast<const fltSemantics &>(csupport_sem_float8_e5m2fnuz);
  }
  static const fltSemantics &Float8E4M3FN() {
    return reinterpret_cast<const fltSemantics &>(csupport_sem_float8_e4m3fn);
  }
  static const fltSemantics &Float8E4M3FNUZ() {
    return reinterpret_cast<const fltSemantics &>(csupport_sem_float8_e4m3fnuz);
  }
  static const fltSemantics &Float8E4M3B11FNUZ() {
    return reinterpret_cast<const fltSemantics &>(
        csupport_sem_float8_e4m3b11fnuz);
  }
  static const fltSemantics &FloatTF32() {
    return reinterpret_cast<const fltSemantics &>(csupport_sem_float_tf32);
  }
  static const fltSemantics &x87DoubleExtended() {
    return reinterpret_cast<const fltSemantics &>(
        csupport_sem_x87_double_extended);
  }
  static const fltSemantics &Bogus() {
    return reinterpret_cast<const fltSemantics &>(csupport_sem_bogus);
  }

  /// @}

  /// IEEE-754R 5.11: Floating Point Comparison Relations.
  enum cmpResult { cmpLessThan, cmpEqual, cmpGreaterThan, cmpUnordered };

  /// IEEE-754R 4.3: Rounding-direction attributes.
  using roundingMode = llvm::RoundingMode;

  static constexpr roundingMode rmNearestTiesToEven =
      RoundingMode::NearestTiesToEven;
  static constexpr roundingMode rmTowardPositive = RoundingMode::TowardPositive;
  static constexpr roundingMode rmTowardNegative = RoundingMode::TowardNegative;
  static constexpr roundingMode rmTowardZero = RoundingMode::TowardZero;
  static constexpr roundingMode rmNearestTiesToAway =
      RoundingMode::NearestTiesToAway;

  /// IEEE-754R 7: Default exception handling.
  ///
  /// opUnderflow or opOverflow are always returned or-ed with opInexact.
  ///
  /// APFloat models this behavior specified by IEEE-754:
  ///   "For operations producing results in floating-point format, the default
  ///    result of an operation that signals the invalid operation exception
  ///    shall be a quiet NaN."
  enum opStatus {
    opOK = 0x00,
    opInvalidOp = 0x01,
    opDivByZero = 0x02,
    opOverflow = 0x04,
    opUnderflow = 0x08,
    opInexact = 0x10
  };

  /// Category of internally-represented number.
  enum fltCategory { fcInfinity, fcNaN, fcNormal, fcZero };

  /// Convenience enum used to construct an uninitialized APFloat.
  enum uninitializedTag { uninitialized };

  /// Enumeration of \c ilogb error results.
  enum IlogbErrorKinds {
    IEK_Zero = INT_MIN + 1,
    IEK_NaN = INT_MIN,
    IEK_Inf = INT_MAX
  };

  static unsigned int semanticsPrecision(const fltSemantics &);
  static ExponentType semanticsMinExponent(const fltSemantics &);
  static ExponentType semanticsMaxExponent(const fltSemantics &);
  static unsigned int semanticsSizeInBits(const fltSemantics &);
  static unsigned int semanticsIntSizeInBits(const fltSemantics &, bool);
  static bool isRepresentableAsNormalIn(const fltSemantics &Src,
                                        const fltSemantics &Dst);
  static unsigned getSizeInBits(const fltSemantics &Sem);
};

enum class fltNonfiniteBehavior {
  IEEE754,
  NanOnly,
};

enum class fltNanEncoding {
  IEEE,
  AllOnes,
  NegativeZero,
};

struct fltSemantics {
  APFloatBase::ExponentType maxExponent;
  APFloatBase::ExponentType minExponent;
  unsigned int precision;
  unsigned int sizeInBits;
  fltNonfiniteBehavior nonFiniteBehavior = fltNonfiniteBehavior::IEEE754;
  fltNanEncoding nanEncoding = fltNanEncoding::IEEE;
  bool isRepresentableBy(const fltSemantics &S) const {
    return maxExponent <= S.maxExponent && minExponent >= S.minExponent &&
           precision <= S.precision;
  }
};

inline unsigned int APFloatBase::semanticsPrecision(const fltSemantics &s) {
  return s.precision;
}
inline APFloatBase::ExponentType
APFloatBase::semanticsMinExponent(const fltSemantics &s) {
  return s.minExponent;
}
inline APFloatBase::ExponentType
APFloatBase::semanticsMaxExponent(const fltSemantics &s) {
  return s.maxExponent;
}
inline unsigned int APFloatBase::semanticsSizeInBits(const fltSemantics &s) {
  return s.sizeInBits;
}
inline unsigned int APFloatBase::semanticsIntSizeInBits(const fltSemantics &s,
                                                        bool isSigned) {
  unsigned MinBitWidth = semanticsMaxExponent(s) + 1;
  if (isSigned)
    ++MinBitWidth;
  return MinBitWidth;
}
inline bool APFloatBase::isRepresentableAsNormalIn(const fltSemantics &Src,
                                                   const fltSemantics &Dst) {
  if (Src.maxExponent >= Dst.maxExponent || Src.minExponent <= Dst.minExponent)
    return false;
  return Dst.precision >= Src.precision;
}
inline unsigned APFloatBase::getSizeInBits(const fltSemantics &Sem) {
  return Sem.sizeInBits;
}

namespace detail {

class IEEEFloat final : public APFloatBase {
public:
  /// \name Constructors
  /// @{

  IEEEFloat(const fltSemantics &); // Default construct to +0.0
  IEEEFloat(const fltSemantics &, integerPart);
  IEEEFloat(const fltSemantics &, uninitializedTag);
  IEEEFloat(const fltSemantics &, const APInt &);
  explicit IEEEFloat(double d);
  explicit IEEEFloat(float f);
  IEEEFloat(const IEEEFloat &);
  IEEEFloat(IEEEFloat &&);
  ~IEEEFloat();

  /// @}

  /// Returns whether this instance allocated memory.
  bool needsCleanup() const { return partCount() > 1; }

  /// \name Convenience "constructors"
  /// @{

  /// @}

  /// \name Arithmetic
  /// @{

  opStatus add(const IEEEFloat &, roundingMode);
  opStatus subtract(const IEEEFloat &, roundingMode);
  opStatus multiply(const IEEEFloat &, roundingMode);
  opStatus divide(const IEEEFloat &, roundingMode);
  /// IEEE remainder.
  opStatus remainder(const IEEEFloat &);
  /// C fmod, or llvm frem.
  opStatus mod(const IEEEFloat &);
  opStatus fusedMultiplyAdd(const IEEEFloat &, const IEEEFloat &, roundingMode);
  opStatus roundToIntegral(roundingMode);
  /// IEEE-754R 5.3.1: nextUp/nextDown.
  opStatus next(bool nextDown);

  /// @}

  /// \name Sign operations.
  /// @{

  void changeSign();

  /// @}

  /// \name Conversions
  /// @{

  opStatus convert(const fltSemantics &, roundingMode, bool *);
  opStatus convertToInteger(MutableArrayRef<integerPart>, unsigned int, bool,
                            roundingMode, bool *) const;
  opStatus convertFromAPInt(const APInt &, bool, roundingMode);
  opStatus convertFromSignExtendedInteger(const integerPart *, unsigned int,
                                          bool, roundingMode);
  opStatus convertFromZeroExtendedInteger(const integerPart *, unsigned int,
                                          bool, roundingMode);
  Expected<opStatus> convertFromString(StringRef, roundingMode);
  APInt bitcastToAPInt() const;
  double convertToDouble() const;
  float convertToFloat() const;

  /// @}

  /// The definition of equality is not straightforward for floating point, so
  /// we won't use operator==.  Use one of the following, or write whatever it
  /// is you really mean.
  bool operator==(const IEEEFloat &) const = delete;

  /// IEEE comparison with another floating point number (NaNs compare
  /// unordered, 0==-0).
  cmpResult compare(const IEEEFloat &) const;

  /// Bitwise comparison for equality (QNaNs compare equal, 0!=-0).
  bool bitwiseIsEqual(const IEEEFloat &) const;

  /// Write out a hexadecimal representation of the floating point value to DST,
  /// which must be of sufficient size, in the C99 form [-]0xh.hhhhp[+-]d.
  /// Return the number of characters written, excluding the terminating NUL.
  unsigned int convertToHexString(char *dst, unsigned int hexDigits,
                                  bool upperCase, roundingMode) const;

  /// \name IEEE-754R 5.7.2 General operations.
  /// @{

  /// IEEE-754R isSignMinus: Returns true if and only if the current value is
  /// negative.
  ///
  /// This applies to zeros and NaNs as well.
  bool isNegative() const { return sign; }

  /// IEEE-754R isNormal: Returns true if and only if the current value is
  /// normal.
  ///
  /// This implies that the current value of the float is not zero, subnormal,
  /// infinite, or NaN following the definition of normality from IEEE-754R.
  bool isNormal() const { return !isDenormal() && isFiniteNonZero(); }

  /// Returns true if and only if the current value is zero, subnormal, or
  /// normal.
  ///
  /// This means that the value is not infinite or NaN.
  bool isFinite() const { return !isNaN() && !isInfinity(); }

  /// Returns true if and only if the float is plus or minus zero.
  bool isZero() const { return category == fcZero; }

  /// IEEE-754R isSubnormal(): Returns true if and only if the float is a
  /// denormal.
  bool isDenormal() const;

  /// IEEE-754R isInfinite(): Returns true if and only if the float is infinity.
  bool isInfinity() const { return category == fcInfinity; }

  /// Returns true if and only if the float is a quiet or signaling NaN.
  bool isNaN() const { return category == fcNaN; }

  /// Returns true if and only if the float is a signaling NaN.
  bool isSignaling() const;

  /// @}

  /// \name Simple Queries
  /// @{

  fltCategory getCategory() const { return category; }
  const fltSemantics &getSemantics() const { return *semantics; }
  bool isNonZero() const { return category != fcZero; }
  bool isFiniteNonZero() const { return isFinite() && !isZero(); }
  bool isPosZero() const { return isZero() && !isNegative(); }
  bool isNegZero() const { return isZero() && isNegative(); }

  /// Returns true if and only if the number has the smallest possible non-zero
  /// magnitude in the current semantics.
  bool isSmallest() const;

  /// Returns true if this is the smallest (by magnitude) normalized finite
  /// number in the given semantics.
  bool isSmallestNormalized() const;

  /// Returns true if and only if the number has the largest possible finite
  /// magnitude in the current semantics.
  bool isLargest() const;

  /// Returns true if and only if the number is an exact integer.
  bool isInteger() const;

  /// @}

  IEEEFloat &operator=(const IEEEFloat &);
  IEEEFloat &operator=(IEEEFloat &&);

  /// Overload to compute a hash code for an APFloat value.
  ///
  /// Note that the use of hash codes for floating point values is in general
  /// frought with peril. Equality is hard to define for these values. For
  /// example, should negative and positive zero hash to different codes? Are
  /// they equal or not? This hash value implementation specifically
  /// emphasizes producing different codes for different inputs in order to
  /// be used in canonicalization and memoization. As such, equality is
  /// bitwiseIsEqual, and 0 != -0.
  friend hash_code hash_value(const IEEEFloat &Arg);

  /// Converts this value into a decimal string.
  ///
  /// \param FormatPrecision The maximum number of digits of
  ///   precision to output.  If there are fewer digits available,
  ///   zero padding will not be used unless the value is
  ///   integral and small enough to be expressed in
  ///   FormatPrecision digits.  0 means to use the natural
  ///   precision of the number.
  /// \param FormatMaxPadding The maximum number of zeros to
  ///   consider inserting before falling back to scientific
  ///   notation.  0 means to always use scientific notation.
  ///
  /// \param TruncateZero Indicate whether to remove the trailing zero in
  ///   fraction part or not. Also setting this parameter to false forcing
  ///   producing of output more similar to default printf behavior.
  ///   Specifically the lower e is used as exponent delimiter and exponent
  ///   always contains no less than two digits.
  ///
  /// Number       Precision    MaxPadding      Result
  /// ------       ---------    ----------      ------
  /// 1.01E+4              5             2       10100
  /// 1.01E+4              4             2       1.01E+4
  /// 1.01E+4              5             1       1.01E+4
  /// 1.01E-2              5             2       0.0101
  /// 1.01E-2              4             2       0.0101
  /// 1.01E-2              4             1       1.01E-2
  void toString(SmallVectorImpl<char> &Str, unsigned FormatPrecision = 0,
                unsigned FormatMaxPadding = 3, bool TruncateZero = true) const;

  /// If this value has an exact multiplicative inverse, store it in inv and
  /// return true.
  bool getExactInverse(APFloat *inv) const;

  // If this is an exact power of two, return the exponent while ignoring the
  // sign bit. If it's not an exact power of 2, return INT_MIN
  LLVM_READONLY
  int getExactLog2Abs() const;

  // If this is an exact power of two, return the exponent. If it's not an exact
  // power of 2, return INT_MIN
  LLVM_READONLY
  int getExactLog2() const {
    return isNegative() ? INT_MIN : getExactLog2Abs();
  }

  /// Returns the exponent of the internal representation of the APFloat.
  ///
  /// Because the radix of APFloat is 2, this is equivalent to floor(log2(x)).
  /// For special APFloat values, this returns special error codes:
  ///
  ///   NaN -> \c IEK_NaN
  ///   0   -> \c IEK_Zero
  ///   Inf -> \c IEK_Inf
  ///
  friend int ilogb(const IEEEFloat &Arg);

  /// Returns: X * 2^Exp for integral exponents.
  friend IEEEFloat scalbn(IEEEFloat X, int Exp, roundingMode);

  friend IEEEFloat frexp(const IEEEFloat &X, int &Exp, roundingMode);

  /// \name Special value setters.
  /// @{

  void makeLargest(bool Neg = false);
  void makeSmallest(bool Neg = false);
  void makeNaN(bool SNaN = false, bool Neg = false,
               const APInt *fill = nullptr);
  void makeInf(bool Neg = false);
  void makeZero(bool Neg = false);
  void makeQuiet();

  /// Returns the smallest (by magnitude) normalized finite number in the given
  /// semantics.
  ///
  /// \param Negative - True iff the number should be negative
  void makeSmallestNormalized(bool Negative = false);

  /// @}

  cmpResult compareAbsoluteValue(const IEEEFloat &) const;

private:
  /// \name Simple Queries
  /// @{

  integerPart *significandParts();
  const integerPart *significandParts() const;
  unsigned int partCount() const;

  /// @}

  /// \name Significand operations.
  /// @{

  integerPart addSignificand(const IEEEFloat &);
  integerPart subtractSignificand(const IEEEFloat &, integerPart);
  lostFraction addOrSubtractSignificand(const IEEEFloat &, bool subtract);
  lostFraction multiplySignificand(const IEEEFloat &, IEEEFloat);
  lostFraction multiplySignificand(const IEEEFloat &);
  lostFraction divideSignificand(const IEEEFloat &);
  void incrementSignificand();
  void initialize(const fltSemantics *);
  void shiftSignificandLeft(unsigned int);
  lostFraction shiftSignificandRight(unsigned int);
  unsigned int significandLSB() const;
  unsigned int significandMSB() const;
  void zeroSignificand();
  /// Return true if the significand excluding the integral bit is all ones.
  bool isSignificandAllOnes() const;
  bool isSignificandAllOnesExceptLSB() const;
  /// Return true if the significand excluding the integral bit is all zeros.
  bool isSignificandAllZeros() const;
  bool isSignificandAllZerosExceptMSB() const;

  /// @}

  /// \name Arithmetic on special values.
  /// @{

  opStatus addOrSubtractSpecials(const IEEEFloat &, bool subtract);
  opStatus divideSpecials(const IEEEFloat &);
  opStatus multiplySpecials(const IEEEFloat &);
  opStatus modSpecials(const IEEEFloat &);
  opStatus remainderSpecials(const IEEEFloat &);

  /// @}

  /// \name Miscellany
  /// @{

  bool convertFromStringSpecials(StringRef str);
  opStatus normalize(roundingMode, lostFraction);
  opStatus addOrSubtract(const IEEEFloat &, roundingMode, bool subtract);
  opStatus handleOverflow(roundingMode);
  bool roundAwayFromZero(roundingMode, lostFraction, unsigned int) const;
  opStatus convertToSignExtendedInteger(MutableArrayRef<integerPart>,
                                        unsigned int, bool, roundingMode,
                                        bool *) const;
  opStatus convertFromUnsignedParts(const integerPart *, unsigned int,
                                    roundingMode);
  Expected<opStatus> convertFromHexadecimalString(StringRef, roundingMode);
  Expected<opStatus> convertFromDecimalString(StringRef, roundingMode);
  char *convertNormalToHexString(char *, unsigned int, bool,
                                 roundingMode) const;
  opStatus roundSignificandWithExponent(const integerPart *, unsigned int, int,
                                        roundingMode);
  ExponentType exponentNaN() const;
  ExponentType exponentInf() const;
  ExponentType exponentZero() const;

  /// @}

  template <const fltSemantics &S> APInt convertIEEEFloatToAPInt() const;
  APInt convertHalfAPFloatToAPInt() const;
  APInt convertBFloatAPFloatToAPInt() const;
  APInt convertFloatAPFloatToAPInt() const;
  APInt convertDoubleAPFloatToAPInt() const;
  APInt convertQuadrupleAPFloatToAPInt() const;
  APInt convertF80LongDoubleAPFloatToAPInt() const;
  APInt convertPPCDoubleDoubleAPFloatToAPInt() const;
  APInt convertFloat8E5M2APFloatToAPInt() const;
  APInt convertFloat8E5M2FNUZAPFloatToAPInt() const;
  APInt convertFloat8E4M3FNAPFloatToAPInt() const;
  APInt convertFloat8E4M3FNUZAPFloatToAPInt() const;
  APInt convertFloat8E4M3B11FNUZAPFloatToAPInt() const;
  APInt convertFloatTF32APFloatToAPInt() const;
  void initFromAPInt(const fltSemantics *Sem, const APInt &api);
  template <const fltSemantics &S> void initFromIEEEAPInt(const APInt &api);
  void initFromHalfAPInt(const APInt &api);
  void initFromBFloatAPInt(const APInt &api);
  void initFromFloatAPInt(const APInt &api);
  void initFromDoubleAPInt(const APInt &api);
  void initFromQuadrupleAPInt(const APInt &api);
  void initFromF80LongDoubleAPInt(const APInt &api);
  void initFromPPCDoubleDoubleAPInt(const APInt &api);
  void initFromFloat8E5M2APInt(const APInt &api);
  void initFromFloat8E5M2FNUZAPInt(const APInt &api);
  void initFromFloat8E4M3FNAPInt(const APInt &api);
  void initFromFloat8E4M3FNUZAPInt(const APInt &api);
  void initFromFloat8E4M3B11FNUZAPInt(const APInt &api);
  void initFromFloatTF32APInt(const APInt &api);

  void assign(const IEEEFloat &);
  void copySignificand(const IEEEFloat &);
  void freeSignificand();

  /// Note: this must be the first data member.
  /// The semantics that this value obeys.
  const fltSemantics *semantics;

  /// A binary fraction with an explicit integer bit.
  ///
  /// The significand must be at least one bit wider than the target precision.
  union Significand {
    integerPart part;
    integerPart *parts;
  } significand;

  /// The signed unbiased exponent of the value.
  ExponentType exponent;

  /// What kind of floating point number this is.
  ///
  /// Only 2 bits are required, but VisualStudio incorrectly sign extends it.
  /// Using the extra bit keeps it from failing under VisualStudio.
  fltCategory category : 3;

  /// Sign bit of the number.
  unsigned int sign : 1;
};

inline int ilogb(const IEEEFloat &Arg) {
  if (Arg.isNaN())
    return IEEEFloat::IEK_NaN;
  if (Arg.isZero())
    return IEEEFloat::IEK_Zero;
  if (Arg.isInfinity())
    return IEEEFloat::IEK_Inf;
  if (!Arg.isDenormal())
    return Arg.exponent;

  IEEEFloat Normalized(Arg);
  int SignificandBits = Arg.getSemantics().precision - 1;

  Normalized.exponent += SignificandBits;
  Normalized.normalize(IEEEFloat::rmNearestTiesToEven, lfExactlyZero);
  return Normalized.exponent - SignificandBits;
}

inline IEEEFloat scalbn(IEEEFloat X, int Exp,
                        IEEEFloat::roundingMode RoundingMode) {
  auto MaxExp = X.getSemantics().maxExponent;
  auto MinExp = X.getSemantics().minExponent;

  int SignificandBits = X.getSemantics().precision - 1;
  int MaxIncrement = MaxExp - (MinExp - SignificandBits) + 1;

  int Clamped = Exp;
  if (Clamped > MaxIncrement)
    Clamped = MaxIncrement;
  if (Clamped < -MaxIncrement - 1)
    Clamped = -MaxIncrement - 1;
  X.exponent += Clamped;
  X.normalize(RoundingMode, lfExactlyZero);
  if (X.isNaN())
    X.makeQuiet();
  return X;
}

inline IEEEFloat frexp(const IEEEFloat &Val, int &Exp,
                       IEEEFloat::roundingMode RM) {
  Exp = ilogb(Val);

  if (Exp == IEEEFloat::IEK_NaN) {
    IEEEFloat Quiet(Val);
    Quiet.makeQuiet();
    return Quiet;
  }

  if (Exp == IEEEFloat::IEK_Inf)
    return Val;

  Exp = Exp == IEEEFloat::IEK_Zero ? 0 : Exp + 1;
  return scalbn(Val, -Exp, RM);
}

inline unsigned int IEEEFloat::partCount() const {
  return ((semantics->precision + 1) + integerPartWidth - 1) / integerPartWidth;
}

inline IEEEFloat::integerPart *IEEEFloat::significandParts() {
  if (partCount() > 1)
    return significand.parts;
  return &significand.part;
}

inline const IEEEFloat::integerPart *IEEEFloat::significandParts() const {
  if (partCount() > 1)
    return significand.parts;
  return &significand.part;
}

inline void IEEEFloat::zeroSignificand() {
  APInt::tcSet(significandParts(), 0, partCount());
}

inline unsigned int IEEEFloat::significandMSB() const {
  return APInt::tcMSB(significandParts(), partCount());
}

inline unsigned int IEEEFloat::significandLSB() const {
  return APInt::tcLSB(significandParts(), partCount());
}

inline void IEEEFloat::incrementSignificand() {
  integerPart carry = APInt::tcIncrement(significandParts(), partCount());
  assert(carry == 0);
  (void)carry;
}

inline IEEEFloat::integerPart IEEEFloat::addSignificand(const IEEEFloat &rhs) {
  assert(semantics == rhs.semantics && exponent == rhs.exponent);
  return APInt::tcAdd(significandParts(), rhs.significandParts(), 0,
                      partCount());
}

inline IEEEFloat::integerPart
IEEEFloat::subtractSignificand(const IEEEFloat &rhs, integerPart borrow) {
  assert(semantics == rhs.semantics && exponent == rhs.exponent);
  return APInt::tcSubtract(significandParts(), rhs.significandParts(), borrow,
                           partCount());
}

inline void IEEEFloat::initialize(const fltSemantics *ourSemantics) {
  semantics = ourSemantics;
  unsigned int count = partCount();
  if (count > 1)
    significand.parts = (integerPart *)malloc(count * sizeof(integerPart));
}

inline void IEEEFloat::freeSignificand() {
  if (needsCleanup())
    free(significand.parts);
}

inline void IEEEFloat::assign(const IEEEFloat &rhs) {
  assert(semantics == rhs.semantics);
  sign = rhs.sign;
  category = rhs.category;
  exponent = rhs.exponent;
  if (isFiniteNonZero() || category == fcNaN)
    copySignificand(rhs);
}

inline void IEEEFloat::copySignificand(const IEEEFloat &rhs) {
  assert(isFiniteNonZero() || category == fcNaN);
  assert(rhs.partCount() >= partCount());
  APInt::tcAssign(significandParts(), rhs.significandParts(), partCount());
}

inline void IEEEFloat::makeNaN(bool SNaN, bool Negative, const APInt *fill) {
  category = fcNaN;
  sign = Negative;
  exponent = exponentNaN();
  int s = sign;
  csupport_apfloat_make_nan(
      significandParts(), partCount(), semantics->precision, SNaN, &s,
      fill ? fill->getRawData() : nullptr, fill ? fill->getNumWords() : 0,
      (int)semantics->nonFiniteBehavior, (int)semantics->nanEncoding,
      semantics == &APFloatBase::x87DoubleExtended());
  sign = s;
}

inline IEEEFloat &IEEEFloat::operator=(const IEEEFloat &rhs) {
  if (this != &rhs) {
    if (semantics != rhs.semantics) {
      freeSignificand();
      initialize(rhs.semantics);
    }
    assign(rhs);
  }
  return *this;
}

inline IEEEFloat &IEEEFloat::operator=(IEEEFloat &&rhs) {
  freeSignificand();
  semantics = rhs.semantics;
  significand = rhs.significand;
  exponent = rhs.exponent;
  category = rhs.category;
  sign = rhs.sign;
  rhs.semantics = &APFloatBase::Bogus();
  return *this;
}

inline bool IEEEFloat::isDenormal() const {
  return isFiniteNonZero() && (exponent == semantics->minExponent) &&
         (APInt::tcExtractBit(significandParts(), semantics->precision - 1) ==
          0);
}

inline bool IEEEFloat::isSmallest() const {
  return isFiniteNonZero() && exponent == semantics->minExponent &&
         significandMSB() == 0;
}

inline bool IEEEFloat::isSmallestNormalized() const {
  return getCategory() == fcNormal && exponent == semantics->minExponent &&
         isSignificandAllZerosExceptMSB();
}

inline bool IEEEFloat::isSignificandAllOnes() const {
  return csupport_apfloat_is_significand_all_ones(significandParts(),
                                                  semantics->precision);
}

inline bool IEEEFloat::isSignificandAllOnesExceptLSB() const {
  return csupport_apfloat_is_significand_all_ones_except_lsb(
      significandParts(), semantics->precision);
}

inline bool IEEEFloat::isSignificandAllZeros() const {
  return csupport_apfloat_is_significand_all_zeros(significandParts(),
                                                   semantics->precision);
}

inline bool IEEEFloat::isSignificandAllZerosExceptMSB() const {
  return csupport_apfloat_is_significand_all_zeros_except_msb(
      significandParts(), semantics->precision);
}

inline bool IEEEFloat::isLargest() const {
  if (semantics->nonFiniteBehavior == fltNonfiniteBehavior::NanOnly &&
      semantics->nanEncoding == fltNanEncoding::AllOnes)
    return isFiniteNonZero() && exponent == semantics->maxExponent &&
           isSignificandAllOnesExceptLSB();
  return isFiniteNonZero() && exponent == semantics->maxExponent &&
         isSignificandAllOnes();
}

inline bool IEEEFloat::isInteger() const {
  if (!isFinite())
    return false;
  IEEEFloat truncated = *this;
  truncated.roundToIntegral(rmTowardZero);
  return compare(truncated) == cmpEqual;
}

inline bool IEEEFloat::bitwiseIsEqual(const IEEEFloat &rhs) const {
  if (this == &rhs)
    return true;
  if (semantics != rhs.semantics || category != rhs.category ||
      sign != rhs.sign)
    return false;
  if (category == fcZero || category == fcInfinity)
    return true;
  if (isFiniteNonZero() && exponent != rhs.exponent)
    return false;
  return memcmp(significandParts(), rhs.significandParts(),
                partCount() * sizeof(integerPart)) == 0;
}

inline IEEEFloat::IEEEFloat(const fltSemantics &ourSemantics,
                            integerPart value) {
  initialize(&ourSemantics);
  sign = 0;
  category = fcNormal;
  zeroSignificand();
  exponent = ourSemantics.precision - 1;
  significandParts()[0] = value;
  normalize(rmNearestTiesToEven, lfExactlyZero);
}

inline IEEEFloat::IEEEFloat(const fltSemantics &ourSemantics) {
  initialize(&ourSemantics);
  makeZero(false);
}

inline IEEEFloat::IEEEFloat(const fltSemantics &ourSemantics, uninitializedTag)
    : IEEEFloat(ourSemantics) {}

inline IEEEFloat::IEEEFloat(const IEEEFloat &rhs) {
  initialize(rhs.semantics);
  assign(rhs);
}

inline IEEEFloat::IEEEFloat(IEEEFloat &&rhs) : semantics(&Bogus()) {
  *this = static_cast<IEEEFloat &&>(rhs);
}

inline IEEEFloat::~IEEEFloat() { freeSignificand(); }

inline IEEEFloat::ExponentType IEEEFloat::exponentNaN() const {
  return csupport_flt_exponent_nan(
      reinterpret_cast<const csupport_flt_semantics_t *>(semantics));
}

inline IEEEFloat::ExponentType IEEEFloat::exponentInf() const {
  return csupport_flt_exponent_inf(
      reinterpret_cast<const csupport_flt_semantics_t *>(semantics));
}

inline IEEEFloat::ExponentType IEEEFloat::exponentZero() const {
  return csupport_flt_exponent_zero(
      reinterpret_cast<const csupport_flt_semantics_t *>(semantics));
}

inline void IEEEFloat::makeLargest(bool Negative) {
  category = fcNormal;
  sign = Negative;
  exponent = semantics->maxExponent;
  csupport_apfloat_make_largest(
      significandParts(), partCount(), semantics->precision,
      (int)semantics->nonFiniteBehavior, (int)semantics->nanEncoding);
}

inline void IEEEFloat::makeSmallest(bool Negative) {
  category = fcNormal;
  sign = Negative;
  exponent = semantics->minExponent;
  csupport_apfloat_make_smallest(significandParts(), partCount());
}

inline void IEEEFloat::makeSmallestNormalized(bool Negative) {
  category = fcNormal;
  sign = Negative;
  exponent = semantics->minExponent;
  csupport_apfloat_make_smallest_normalized(significandParts(), partCount(),
                                            semantics->precision);
}

inline void IEEEFloat::makeInf(bool Negative) {
  if (semantics->nonFiniteBehavior == fltNonfiniteBehavior::NanOnly) {
    makeNaN(false, Negative);
    return;
  }
  category = fcInfinity;
  sign = Negative;
  exponent = exponentInf();
  APInt::tcSet(significandParts(), 0, partCount());
}

inline void IEEEFloat::makeZero(bool Negative) {
  category = fcZero;
  sign = Negative;
  if (semantics->nanEncoding == fltNanEncoding::NegativeZero)
    sign = false;
  exponent = exponentZero();
  APInt::tcSet(significandParts(), 0, partCount());
}

inline void IEEEFloat::makeQuiet() {
  assert(isNaN());
  if (semantics->nonFiniteBehavior != fltNonfiniteBehavior::NanOnly)
    APInt::tcSetBit(significandParts(), semantics->precision - 2);
}

inline void IEEEFloat::changeSign() {
  if (semantics->nanEncoding == fltNanEncoding::NegativeZero &&
      (isZero() || isNaN()))
    return;
  sign = !sign;
}

inline bool IEEEFloat::isSignaling() const {
  if (!isNaN())
    return false;
  if (semantics->nonFiniteBehavior == fltNonfiniteBehavior::NanOnly)
    return false;
  return !APInt::tcExtractBit(significandParts(), semantics->precision - 2);
}

inline IEEEFloat::cmpResult
IEEEFloat::compareAbsoluteValue(const IEEEFloat &rhs) const {
  assert(semantics == rhs.semantics);
  assert(isFiniteNonZero() && rhs.isFiniteNonZero());
  int compare = exponent - rhs.exponent;
  if (compare == 0)
    compare = APInt::tcCompare(significandParts(), rhs.significandParts(),
                               partCount());
  if (compare > 0)
    return cmpGreaterThan;
  if (compare < 0)
    return cmpLessThan;
  return cmpEqual;
}

inline IEEEFloat::cmpResult IEEEFloat::compare(const IEEEFloat &rhs) const {
  assert(semantics == rhs.semantics);
  int r = csupport_apfloat_compare_categories((int)category, (int)rhs.category,
                                              sign, rhs.sign);
  if (r == -2)
    return cmpUnordered;
  if (r == -1)
    return cmpLessThan;
  if (r == 1)
    return cmpGreaterThan;
  if (r == 0)
    return cmpEqual;
  if (r != 2)
    llvm_unreachable("Unexpected csupport_apfloat_compare_categories result");

  if (sign != rhs.sign)
    return sign ? cmpLessThan : cmpGreaterThan;
  cmpResult result = compareAbsoluteValue(rhs);
  if (sign) {
    if (result == cmpLessThan)
      result = cmpGreaterThan;
    else if (result == cmpGreaterThan)
      result = cmpLessThan;
  }
  return result;
}

inline lostFraction IEEEFloat::shiftSignificandRight(unsigned int bits) {
  assert((ExponentType)(exponent + bits) >= exponent);
  exponent += bits;
  lostFraction lf = (lostFraction)csupport_apfloat_lost_fraction_truncation(
      significandParts(), partCount(), bits);
  APInt::tcShiftRight(significandParts(), partCount(), bits);
  return lf;
}

inline void IEEEFloat::shiftSignificandLeft(unsigned int bits) {
  assert(bits < semantics->precision);
  if (bits) {
    APInt::tcShiftLeft(significandParts(), partCount(), bits);
    exponent -= bits;
    assert(!APInt::tcIsZero(significandParts(), partCount()));
  }
}

inline IEEEFloat::opStatus IEEEFloat::add(const IEEEFloat &rhs,
                                          roundingMode rounding_mode) {
  return addOrSubtract(rhs, rounding_mode, false);
}

inline IEEEFloat::opStatus IEEEFloat::subtract(const IEEEFloat &rhs,
                                               roundingMode rounding_mode) {
  return addOrSubtract(rhs, rounding_mode, true);
}

inline bool IEEEFloat::roundAwayFromZero(roundingMode rounding_mode,
                                         lostFraction lost_fraction,
                                         unsigned int bit) const {
  return csupport_apfloat_round_away_from_zero(
      (int)rounding_mode, (int)lost_fraction, (int)category, sign,
      significandParts(), bit);
}

inline int IEEEFloat::getExactLog2Abs() const {
  return csupport_apfloat_get_exact_log2_abs(
      significandParts(), partCount(), semantics->precision, exponent,
      semantics->minExponent, isFiniteNonZero(), isZero());
}

inline IEEEFloat::opStatus
IEEEFloat::handleOverflow(roundingMode rounding_mode) {
  if (rounding_mode == rmNearestTiesToEven ||
      rounding_mode == rmNearestTiesToAway ||
      (rounding_mode == rmTowardPositive && !sign) ||
      (rounding_mode == rmTowardNegative && sign)) {
    if (semantics->nonFiniteBehavior == fltNonfiniteBehavior::NanOnly)
      makeNaN(false, sign);
    else
      category = fcInfinity;
    return (opStatus)(opOverflow | opInexact);
  }
  category = fcNormal;
  exponent = semantics->maxExponent;
  csupport_apfloat_tc_set_least_significant_bits(
      significandParts(), partCount(), semantics->precision);
  if (semantics->nonFiniteBehavior == fltNonfiniteBehavior::NanOnly &&
      semantics->nanEncoding == fltNanEncoding::AllOnes)
    APInt::tcClearBit(significandParts(), 0);
  return opInexact;
}

inline IEEEFloat::opStatus IEEEFloat::normalize(roundingMode rounding_mode,
                                                lostFraction lost_fraction) {
  if (!isFiniteNonZero())
    return opOK;
  int cat = (int)category, s = sign, exp = exponent;
  int nfb = (int)semantics->nonFiniteBehavior;
  int ne = (int)semantics->nanEncoding;
  int result = csupport_apfloat_normalize(
      significandParts(), partCount(), &exp, &cat, &s, (int)rounding_mode,
      (int)lost_fraction, semantics->precision, semantics->maxExponent,
      semantics->minExponent, nfb, ne);
  exponent = exp;
  category = (fltCategory)cat;
  sign = s;
  if (cat == fcNaN && nfb == (int)fltNonfiniteBehavior::NanOnly)
    makeNaN(false, sign);
  return (opStatus)result;
}

inline char *
IEEEFloat::convertNormalToHexString(char *dst, unsigned int hexDigits,
                                    bool upperCase,
                                    roundingMode rounding_mode) const {
  unsigned out_len;
  csupport_apfloat_convert_normal_to_hex(
      dst, significandParts(), partCount(), semantics->precision, exponent,
      sign, significandLSB(), hexDigits, upperCase, (int)rounding_mode,
      (int)category, &out_len);
  return dst + out_len;
}

inline unsigned int
IEEEFloat::convertToHexString(char *dst, unsigned int hexDigits, bool upperCase,
                              roundingMode rounding_mode) const {
  char *p = dst;
  if (sign)
    *dst++ = '-';

  switch (category) {
  case fcInfinity:
    memcpy(dst, upperCase ? "INFINITY" : "infinity", 8);
    dst += 8;
    break;

  case fcNaN:
    memcpy(dst, upperCase ? "NAN" : "nan", 3);
    dst += 3;
    break;

  case fcZero:
    *dst++ = '0';
    *dst++ = upperCase ? 'X' : 'x';
    *dst++ = '0';
    if (hexDigits > 1) {
      *dst++ = '.';
      memset(dst, '0', hexDigits - 1);
      dst += hexDigits - 1;
    }
    *dst++ = upperCase ? 'P' : 'p';
    *dst++ = '0';
    break;

  case fcNormal:
    dst = convertNormalToHexString(dst, hexDigits, upperCase, rounding_mode);
    break;
  }

  *dst = 0;
  return (unsigned int)(dst - p);
}

inline hash_code hash_value(const IEEEFloat &Arg) {
  if (!Arg.isFiniteNonZero())
    return hash_combine((uint8_t)Arg.getCategory(),
                        // NaN has no sign, fix it at zero.
                        Arg.isNaN() ? (uint8_t)0 : (uint8_t)Arg.isNegative(),
                        Arg.getSemantics().precision);

  // Normal floats need their exponent and significand hashed.
  return hash_combine(
      (uint8_t)Arg.getCategory(), (uint8_t)Arg.isNegative(),
      Arg.getSemantics().precision, Arg.exponent,
      hash_combine_range(Arg.significandParts(),
                         Arg.significandParts() + Arg.partCount()));
}

inline APInt IEEEFloat::convertHalfAPFloatToAPInt() const {
  return bitcastToAPInt();
}
inline APInt IEEEFloat::convertBFloatAPFloatToAPInt() const {
  return bitcastToAPInt();
}
inline APInt IEEEFloat::convertFloatAPFloatToAPInt() const {
  return bitcastToAPInt();
}
inline APInt IEEEFloat::convertDoubleAPFloatToAPInt() const {
  return bitcastToAPInt();
}
inline APInt IEEEFloat::convertQuadrupleAPFloatToAPInt() const {
  return bitcastToAPInt();
}
inline APInt IEEEFloat::convertFloat8E5M2APFloatToAPInt() const {
  return bitcastToAPInt();
}
inline APInt IEEEFloat::convertFloat8E5M2FNUZAPFloatToAPInt() const {
  return bitcastToAPInt();
}
inline APInt IEEEFloat::convertFloat8E4M3FNAPFloatToAPInt() const {
  return bitcastToAPInt();
}
inline APInt IEEEFloat::convertFloat8E4M3FNUZAPFloatToAPInt() const {
  return bitcastToAPInt();
}
inline APInt IEEEFloat::convertFloat8E4M3B11FNUZAPFloatToAPInt() const {
  return bitcastToAPInt();
}
inline APInt IEEEFloat::convertFloatTF32APFloatToAPInt() const {
  return bitcastToAPInt();
}

inline APInt IEEEFloat::convertPPCDoubleDoubleAPFloatToAPInt() const {
  assert(semantics ==
         (const fltSemantics *)&csupport_sem_ppc_double_double_legacy);
  assert(partCount() == 2);

  uint64_t words[2];
  opStatus fs;
  bool losesInfo;

  fltSemantics extendedSemantics = *semantics;
  extendedSemantics.minExponent = APFloatBase::IEEEdouble().minExponent;
  IEEEFloat extended(*this);
  fs = extended.convert(extendedSemantics, rmNearestTiesToEven, &losesInfo);
  assert(fs == opOK && !losesInfo);
  (void)fs;

  IEEEFloat u(extended);
  fs = u.convert(APFloatBase::IEEEdouble(), rmNearestTiesToEven, &losesInfo);
  assert(fs == opOK || fs == opInexact);
  (void)fs;
  words[0] = *u.convertDoubleAPFloatToAPInt().getRawData();

  if (u.isFiniteNonZero() && losesInfo) {
    fs = u.convert(extendedSemantics, rmNearestTiesToEven, &losesInfo);
    assert(fs == opOK && !losesInfo);
    (void)fs;

    IEEEFloat v(extended);
    v.subtract(u, rmNearestTiesToEven);
    fs = v.convert(APFloatBase::IEEEdouble(), rmNearestTiesToEven, &losesInfo);
    assert(fs == opOK && !losesInfo);
    (void)fs;
    words[1] = *v.convertDoubleAPFloatToAPInt().getRawData();
  } else {
    words[1] = 0;
  }

  return APInt(128, words);
}

inline void IEEEFloat::initFromPPCDoubleDoubleAPInt(const APInt &api) {
  uint64_t i1 = api.getRawData()[0];
  uint64_t i2 = api.getRawData()[1];
  opStatus fs;
  bool losesInfo;

  const fltSemantics &LegacySem = *reinterpret_cast<const fltSemantics *>(
      &csupport_sem_ppc_double_double_legacy);

  initFromDoubleAPInt(APInt(64, i1));
  fs = convert(LegacySem, rmNearestTiesToEven, &losesInfo);
  assert(fs == opOK && !losesInfo);
  (void)fs;

  if (isFiniteNonZero()) {
    IEEEFloat v(APFloatBase::IEEEdouble(), APInt(64, i2));
    fs = v.convert(LegacySem, rmNearestTiesToEven, &losesInfo);
    assert(fs == opOK && !losesInfo);
    (void)fs;

    add(v, rmNearestTiesToEven);
  }
}

namespace {
constexpr unsigned MaxPowerOfFiveParts =
    2U +
    ((((16383U + 113U - 1U) * 815U) / (351U * APFloatBase::integerPartWidth)));

struct DecimalInfo {
  const char *firstSigDigit;
  const char *lastSigDigit;
  int exponent;
  int normalizedExponent;
};

inline Error interpretDecimal(StringRef::iterator Begin,
                              StringRef::iterator End, DecimalInfo *D) {
  csupport_decimal_info_t CD;
  int err = csupport_apfloat_interpret_decimal(Begin, End, &CD);
  if (err)
    return createStringError(inconvertibleErrorCode(),
                             csupport_apfloat_error_msg(err));
  D->firstSigDigit = CD.first_sig_digit;
  D->lastSigDigit = CD.last_sig_digit;
  D->exponent = CD.exponent;
  D->normalizedExponent = CD.normalized_exponent;
  return Error::success();
}
} // namespace

inline IEEEFloat::opStatus
IEEEFloat::roundSignificandWithExponent(const integerPart *decSigParts,
                                        unsigned sigPartCount, int exp,
                                        roundingMode rounding_mode) {
  unsigned int parts, pow5PartCount;
  fltSemantics calcSemantics = {32767, -32767, 0, 0};
  integerPart pow5Parts[MaxPowerOfFiveParts];
  bool isNearest;

  isNearest = (rounding_mode == rmNearestTiesToEven ||
               rounding_mode == rmNearestTiesToAway);

  parts = csupport_flt_part_count_for_bits(semantics->precision + 11);

  pow5PartCount = csupport_apfloat_power_of5(pow5Parts, exp >= 0 ? exp : -exp);

  for (;; parts *= 2) {
    opStatus sigStatus, powStatus;
    unsigned int excessPrecision, truncatedBits;

    calcSemantics.precision = parts * integerPartWidth - 1;
    excessPrecision = calcSemantics.precision - semantics->precision;
    truncatedBits = excessPrecision;

    IEEEFloat decSig(calcSemantics, uninitialized);
    decSig.makeZero(sign);
    IEEEFloat pow5(calcSemantics);

    sigStatus = decSig.convertFromUnsignedParts(decSigParts, sigPartCount,
                                                rmNearestTiesToEven);
    powStatus = pow5.convertFromUnsignedParts(pow5Parts, pow5PartCount,
                                              rmNearestTiesToEven);
    decSig.exponent += exp;

    lostFraction calcLostFraction;
    integerPart HUerr, HUdistance;
    unsigned int powHUerr;

    if (exp >= 0) {
      calcLostFraction = decSig.multiplySignificand(pow5);
      powHUerr = powStatus != opOK;
    } else {
      calcLostFraction = decSig.divideSignificand(pow5);
      if (decSig.exponent < semantics->minExponent) {
        excessPrecision += (semantics->minExponent - decSig.exponent);
        truncatedBits = excessPrecision;
        if (excessPrecision > calcSemantics.precision)
          excessPrecision = calcSemantics.precision;
      }
      powHUerr =
          (powStatus == opOK && calcLostFraction == lfExactlyZero) ? 0 : 2;
    }

    assert(APInt::tcExtractBit(decSig.significandParts(),
                               calcSemantics.precision - 1) == 1);

    HUerr = csupport_apfloat_huerr_bound(calcLostFraction != lfExactlyZero,
                                         sigStatus != opOK, powHUerr);
    HUdistance = 2 * csupport_apfloat_ulps_from_boundary(
                         decSig.significandParts(), excessPrecision, isNearest);

    if (HUdistance >= HUerr) {
      APInt::tcExtract(
          significandParts(), partCount(), decSig.significandParts(),
          calcSemantics.precision - excessPrecision, excessPrecision);
      exponent = (decSig.exponent + semantics->precision -
                  (calcSemantics.precision - excessPrecision));
      calcLostFraction =
          (lostFraction)csupport_apfloat_lost_fraction_truncation(
              decSig.significandParts(), decSig.partCount(), truncatedBits);
      return normalize(rounding_mode, calcLostFraction);
    }
  }
}

inline Expected<IEEEFloat::opStatus>
IEEEFloat::convertFromHexadecimalString(StringRef s,
                                        roundingMode rounding_mode) {
  category = fcNormal;
  zeroSignificand();
  exponent = 0;
  int exp_out = 0, cat_out = 0, lf_out = 0;
  int err = csupport_apfloat_convert_from_hex_core(
      s.begin(), s.end(), significandParts(), partCount(), integerPartWidth,
      semantics->precision, &exp_out, &cat_out, &lf_out);
  (void)cat_out;
  if (err)
    return createStringError(inconvertibleErrorCode(),
                             csupport_apfloat_error_msg(err));
  exponent = exp_out;
  return normalize(rounding_mode, (lostFraction)lf_out);
}

inline Expected<IEEEFloat::opStatus>
IEEEFloat::convertFromDecimalString(StringRef str, roundingMode rounding_mode) {
  DecimalInfo D;
  opStatus fs;

  StringRef::iterator p = str.begin();
  if (Error Err = interpretDecimal(p, str.end(), &D))
    return std::move(Err);

  if (D.firstSigDigit == str.end() ||
      (unsigned)(*D.firstSigDigit - '0') >= 10U) {
    category = fcZero;
    fs = opOK;
    if (semantics->nanEncoding == fltNanEncoding::NegativeZero)
      sign = false;
  } else if (D.normalizedExponent - 1 > INT_MAX / 42039) {
    fs = handleOverflow(rounding_mode);
  } else if (D.normalizedExponent - 1 < INT_MIN / 42039 ||
             (D.normalizedExponent + 1) * 28738 <=
                 8651 * (semantics->minExponent - (int)semantics->precision)) {
    category = fcNormal;
    zeroSignificand();
    fs = normalize(rounding_mode, lfLessThanHalf);
  } else if ((D.normalizedExponent - 1) * 42039 >=
             12655 * semantics->maxExponent) {
    fs = handleOverflow(rounding_mode);
  } else {
    uint64_t *decSignificand = 0;
    unsigned partCount = 0;
    int cerr = csupport_apfloat_convert_from_decimal_core(
        D.firstSigDigit, D.lastSigDigit, D.normalizedExponent, D.exponent,
        &decSignificand, &partCount);
    if (cerr == CSUPPORT_APFLOAT_ERR_INVALID_SIG) {
      free(decSignificand);
      return createStringError(inconvertibleErrorCode(),
                               "Invalid character in significand");
    }
    if (cerr < 0) {
      free(decSignificand);
      return createStringError(inconvertibleErrorCode(),
                               "Memory allocation failed");
    }
    category = fcNormal;
    fs = roundSignificandWithExponent((integerPart *)(decSignificand),
                                      partCount, D.exponent, rounding_mode);
    free(decSignificand);
  }

  return fs;
}

inline bool IEEEFloat::convertFromStringSpecials(StringRef str) {
  int cat = 0, neg = 0, sig = 0;
  unsigned radix = 0;
  const char *payload_begin = 0;
  size_t payload_len = 0;
  if (!csupport_apfloat_parse_special_string(str.data(), str.size(), &cat, &neg,
                                             &sig, &radix, &payload_begin,
                                             &payload_len))
    return false;

  if (cat == 1) {
    makeInf(neg);
    return true;
  }

  if (payload_begin == 0 && payload_len == 0) {
    makeNaN(sig, neg);
    return true;
  }

  APInt Payload;
  if (!StringRef(payload_begin, payload_len).getAsInteger(radix, Payload)) {
    makeNaN(sig, neg, &Payload);
    return true;
  }
  return false;
}

inline Expected<IEEEFloat::opStatus>
IEEEFloat::convertFromString(StringRef str, roundingMode rounding_mode) {
  if (str.empty())
    return createStringError(inconvertibleErrorCode(), "Invalid string length");

  if (convertFromStringSpecials(str))
    return opOK;

  StringRef::iterator p = str.begin();
  size_t slen = str.size();
  sign = *p == '-' ? 1 : 0;
  if (*p == '-' || *p == '+') {
    p++;
    slen--;
    if (!slen)
      return createStringError(inconvertibleErrorCode(),
                               "String has no digits");
  }

  if (slen >= 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
    if (slen == 2)
      return createStringError(inconvertibleErrorCode(), "Invalid string");
    return convertFromHexadecimalString(StringRef(p + 2, slen - 2),
                                        rounding_mode);
  }

  return convertFromDecimalString(StringRef(p, slen), rounding_mode);
}

inline APInt IEEEFloat::convertF80LongDoubleAPFloatToAPInt() const {
  assert(semantics == &APFloatBase::x87DoubleExtended());
  assert(partCount() == 2);
  uint64_t words[2];
  csupport_apfloat_convert_f80_to_words((int)category, isFiniteNonZero(),
                                        exponent, sign, significandParts(),
                                        words);
  return APInt(80, words);
}

inline APInt IEEEFloat::bitcastToAPInt() const {
  if (semantics == &APFloatBase::x87DoubleExtended())
    return convertF80LongDoubleAPFloatToAPInt();
  if (semantics == (const fltSemantics *)&csupport_sem_ppc_double_double_legacy)
    return convertPPCDoubleDoubleAPFloatToAPInt();
  unsigned numWords = csupport_flt_part_count_for_bits(semantics->sizeInBits);
  uint64_t words[2] = {};
  csupport_apfloat_convert_ieee_to_words(
      (int)category, isFiniteNonZero(), exponent, sign, significandParts(),
      semantics->precision, semantics->sizeInBits, semantics->minExponent,
      (int)semantics->nonFiniteBehavior, (int)semantics->nanEncoding, words,
      numWords);
  return APInt(semantics->sizeInBits, ArrayRef(words, numWords));
}

inline float IEEEFloat::convertToFloat() const {
  assert(semantics == &APFloatBase::IEEEsingle() &&
         "Float semantics are not IEEEsingle");
  return bitcastToAPInt().bitsToFloat();
}

inline double IEEEFloat::convertToDouble() const {
  assert(semantics == &APFloatBase::IEEEdouble() &&
         "Float semantics are not IEEEdouble");
  return bitcastToAPInt().bitsToDouble();
}

inline void IEEEFloat::initFromF80LongDoubleAPInt(const APInt &api) {
  initialize(&APFloatBase::x87DoubleExtended());
  assert(partCount() == 2);
  int s = 0, exp = 0;
  uint64_t mysig = 0;
  int cat = csupport_apfloat_unpack_f80(api.getRawData(), &s, &exp, &mysig);
  sign = s;
  switch (cat) {
  case CSUPPORT_APFLOAT_CAT_ZERO:
    makeZero(sign);
    return;
  case CSUPPORT_APFLOAT_CAT_INF:
    makeInf(sign);
    return;
  case CSUPPORT_APFLOAT_CAT_NAN:
    category = fcNaN;
    exponent = exponentNaN();
    significandParts()[0] = mysig;
    significandParts()[1] = 0;
    return;
  default:
    category = fcNormal;
    exponent = exp;
    significandParts()[0] = mysig;
    significandParts()[1] = 0;
    return;
  }
}

inline void IEEEFloat::initFromAPInt(const fltSemantics *Sem,
                                     const APInt &api) {
  assert(api.getBitWidth() == Sem->sizeInBits);
  if (Sem == &APFloatBase::x87DoubleExtended())
    return initFromF80LongDoubleAPInt(api);
  if (Sem == (const fltSemantics *)&csupport_sem_ppc_double_double_legacy)
    return initFromPPCDoubleDoubleAPInt(api);
  uint64_t sig_buf[8];
  int s = 0, exp = 0;
  unsigned stored_sig_parts =
      csupport_flt_part_count_for_bits(Sem->precision - 1);
  int cat = csupport_apfloat_unpack_ieee(
      api.getRawData(), api.getNumWords(), Sem->precision, Sem->sizeInBits,
      Sem->minExponent, (int)Sem->nonFiniteBehavior, (int)Sem->nanEncoding, &s,
      &exp, sig_buf, 8);
  initialize(Sem);
  assert(partCount() == stored_sig_parts);
  sign = s;
  switch (cat) {
  case CSUPPORT_APFLOAT_CAT_ZERO:
    makeZero(sign);
    return;
  case CSUPPORT_APFLOAT_CAT_INF:
    makeInf(sign);
    return;
  case CSUPPORT_APFLOAT_CAT_NAN:
    category = fcNaN;
    exponent = exp;
    memcpy(significandParts(), sig_buf, stored_sig_parts * sizeof(integerPart));
    return;
  case CSUPPORT_APFLOAT_CAT_NORMAL:
  case CSUPPORT_APFLOAT_CAT_DENORM:
    category = fcNormal;
    exponent = exp;
    memcpy(significandParts(), sig_buf, stored_sig_parts * sizeof(integerPart));
    return;
  }
}

inline void IEEEFloat::initFromQuadrupleAPInt(const APInt &api) {
  initFromAPInt(&IEEEquad(), api);
}
inline void IEEEFloat::initFromDoubleAPInt(const APInt &api) {
  initFromAPInt(&IEEEdouble(), api);
}
inline void IEEEFloat::initFromFloatAPInt(const APInt &api) {
  initFromAPInt(&IEEEsingle(), api);
}
inline void IEEEFloat::initFromBFloatAPInt(const APInt &api) {
  initFromAPInt(&BFloat(), api);
}
inline void IEEEFloat::initFromHalfAPInt(const APInt &api) {
  initFromAPInt(&IEEEhalf(), api);
}
inline void IEEEFloat::initFromFloat8E5M2APInt(const APInt &api) {
  initFromAPInt(&Float8E5M2(), api);
}
inline void IEEEFloat::initFromFloat8E5M2FNUZAPInt(const APInt &api) {
  initFromAPInt(&Float8E5M2FNUZ(), api);
}
inline void IEEEFloat::initFromFloat8E4M3FNAPInt(const APInt &api) {
  initFromAPInt(&Float8E4M3FN(), api);
}
inline void IEEEFloat::initFromFloat8E4M3FNUZAPInt(const APInt &api) {
  initFromAPInt(&Float8E4M3FNUZ(), api);
}
inline void IEEEFloat::initFromFloat8E4M3B11FNUZAPInt(const APInt &api) {
  initFromAPInt(&Float8E4M3B11FNUZ(), api);
}
inline void IEEEFloat::initFromFloatTF32APInt(const APInt &api) {
  initFromAPInt(&FloatTF32(), api);
}

inline IEEEFloat::IEEEFloat(const fltSemantics &Sem, const APInt &API) {
  initFromAPInt(&Sem, API);
}

inline IEEEFloat::IEEEFloat(float f) {
  initFromAPInt(&IEEEsingle(), APInt::floatToBits(f));
}

inline IEEEFloat::IEEEFloat(double d) {
  initFromAPInt(&IEEEdouble(), APInt::doubleToBits(d));
}

inline IEEEFloat::opStatus
IEEEFloat::addOrSubtractSpecials(const IEEEFloat &rhs, bool subtract) {
  int out_cat = (int)category, out_sign = sign;
  int r = csupport_apfloat_add_or_subtract_specials(
      (int)category, (int)rhs.category, subtract, sign, rhs.sign, &out_cat,
      &out_sign);
  if (r == 0) {
    if (out_cat != (int)category)
      category = (fltCategory)out_cat;
    sign = out_sign;
    if (category == fcNaN) {
      if (isSignaling()) {
        makeQuiet();
        return opInvalidOp;
      }
      return rhs.isSignaling() ? opInvalidOp : opOK;
    }
    return opOK;
  }
  if (r == -2) {
    makeNaN();
    return opInvalidOp;
  }
  if (r == -5)
    return opDivByZero;
  assign(rhs);
  sign = out_sign;
  if (isSignaling()) {
    makeQuiet();
    return opInvalidOp;
  }
  return rhs.isSignaling() ? opInvalidOp : opOK;
}

inline IEEEFloat::opStatus IEEEFloat::multiplySpecials(const IEEEFloat &rhs) {
  int out_cat = (int)category;
  int r = csupport_apfloat_multiply_specials((int)category, (int)rhs.category,
                                             &out_cat);
  if (r == 0) {
    if (out_cat != (int)category)
      category = (fltCategory)out_cat;
    return opOK;
  }
  if (r == -2) {
    makeNaN();
    return opInvalidOp;
  }
  if (category != fcNaN) {
    assign(rhs);
    sign = false;
  }
  sign ^= rhs.sign;
  if (isSignaling()) {
    makeQuiet();
    return opInvalidOp;
  }
  return rhs.isSignaling() ? opInvalidOp : opOK;
}

inline IEEEFloat::opStatus IEEEFloat::divideSpecials(const IEEEFloat &rhs) {
  int out_cat = (int)category;
  int r = csupport_apfloat_divide_specials((int)category, (int)rhs.category,
                                           &out_cat);
  if (r == 0) {
    if (out_cat != (int)category)
      category = (fltCategory)out_cat;
    return opOK;
  }
  if (r == -2) {
    makeNaN();
    return opInvalidOp;
  }
  if (r == -4) {
    if (semantics->nonFiniteBehavior == fltNonfiniteBehavior::NanOnly)
      makeNaN(false, sign);
    else
      category = fcInfinity;
    return opDivByZero;
  }
  if (category != fcNaN) {
    assign(rhs);
    sign = false;
  }
  sign ^= rhs.sign;
  if (isSignaling()) {
    makeQuiet();
    return opInvalidOp;
  }
  return rhs.isSignaling() ? opInvalidOp : opOK;
}

inline IEEEFloat::opStatus IEEEFloat::modSpecials(const IEEEFloat &rhs) {
  int out_cat = 0;
  int r =
      csupport_apfloat_mod_specials((int)category, (int)rhs.category, &out_cat);
  if (r == 0)
    return opOK;
  if (r == -2) {
    makeNaN();
    return opInvalidOp;
  }
  if (r == -5)
    return opOK;
  if (category != fcNaN)
    assign(rhs);
  if (isSignaling()) {
    makeQuiet();
    return opInvalidOp;
  }
  return rhs.isSignaling() ? opInvalidOp : opOK;
}

inline IEEEFloat::opStatus IEEEFloat::remainderSpecials(const IEEEFloat &rhs) {
  int out_cat = 0;
  int r = csupport_apfloat_remainder_specials((int)category, (int)rhs.category,
                                              &out_cat);
  if (r == 0)
    return opOK;
  if (r == -2) {
    makeNaN();
    return opInvalidOp;
  }
  if (r == -5)
    return opDivByZero;
  if (category != fcNaN)
    assign(rhs);
  if (isSignaling()) {
    makeQuiet();
    return opInvalidOp;
  }
  return rhs.isSignaling() ? opInvalidOp : opOK;
}

inline IEEEFloat::opStatus IEEEFloat::remainder(const IEEEFloat &rhs) {
  opStatus fs;
  unsigned int origSign = sign;

  fs = remainderSpecials(rhs);
  if (fs != opDivByZero)
    return fs;

  fs = opOK;

  IEEEFloat P2 = rhs;
  if (P2.add(rhs, rmNearestTiesToEven) == opOK) {
    fs = mod(P2);
    assert(fs == opOK);
  }

  IEEEFloat P = rhs;
  P.sign = false;
  sign = false;

  bool losesInfo;
  fltSemantics extendedSemantics = *semantics;
  extendedSemantics.maxExponent++;
  extendedSemantics.minExponent--;
  extendedSemantics.precision += 2;

  IEEEFloat VEx = *this;
  fs = VEx.convert(extendedSemantics, rmNearestTiesToEven, &losesInfo);
  assert(fs == opOK && !losesInfo);
  IEEEFloat PEx = P;
  fs = PEx.convert(extendedSemantics, rmNearestTiesToEven, &losesInfo);
  assert(fs == opOK && !losesInfo);

  fs = VEx.add(VEx, rmNearestTiesToEven);
  assert(fs == opOK);

  if (VEx.compare(PEx) == cmpGreaterThan) {
    fs = subtract(P, rmNearestTiesToEven);
    assert(fs == opOK);

    fs = VEx.subtract(PEx, rmNearestTiesToEven);
    assert(fs == opOK);
    fs = VEx.subtract(PEx, rmNearestTiesToEven);
    assert(fs == opOK);

    cmpResult result = VEx.compare(PEx);
    if (result == cmpGreaterThan || result == cmpEqual) {
      fs = subtract(P, rmNearestTiesToEven);
      assert(fs == opOK);
    }
  }

  if (isZero()) {
    sign = origSign;
    if (semantics->nanEncoding == fltNanEncoding::NegativeZero)
      sign = false;
  } else
    sign ^= origSign;
  return fs;
}

inline IEEEFloat::opStatus IEEEFloat::mod(const IEEEFloat &rhs) {
  opStatus fs;
  fs = modSpecials(rhs);
  unsigned int origSign = sign;

  while (isFiniteNonZero() && rhs.isFiniteNonZero() &&
         compareAbsoluteValue(rhs) != cmpLessThan) {
    int Exp = ilogb(*this) - ilogb(rhs);
    IEEEFloat V = scalbn(rhs, Exp, rmNearestTiesToEven);
    if (V.isNaN() || compareAbsoluteValue(V) == cmpLessThan)
      V = scalbn(rhs, Exp - 1, rmNearestTiesToEven);
    V.sign = sign;

    fs = subtract(V, rmNearestTiesToEven);
    assert(fs == opOK);
  }
  if (isZero()) {
    sign = origSign;
    if (semantics->nanEncoding == fltNanEncoding::NegativeZero)
      sign = false;
  }
  return fs;
}

inline IEEEFloat::opStatus
IEEEFloat::fusedMultiplyAdd(const IEEEFloat &multiplicand,
                            const IEEEFloat &addend,
                            roundingMode rounding_mode) {
  opStatus fs;

  sign ^= multiplicand.sign;

  if (isFiniteNonZero() && multiplicand.isFiniteNonZero() &&
      addend.isFinite()) {
    lostFraction lost_fraction;

    lost_fraction = multiplySignificand(multiplicand, addend);
    fs = normalize(rounding_mode, lost_fraction);
    if (lost_fraction != lfExactlyZero)
      fs = (opStatus)(fs | opInexact);

    if (category == fcZero && !(fs & opUnderflow) && sign != addend.sign) {
      sign = (rounding_mode == rmTowardNegative);
      if (semantics->nanEncoding == fltNanEncoding::NegativeZero)
        sign = false;
    }
  } else {
    fs = multiplySpecials(multiplicand);

    if (fs == opOK)
      fs = addOrSubtract(addend, rounding_mode, false);
  }

  return fs;
}

inline IEEEFloat::opStatus
IEEEFloat::roundToIntegral(roundingMode rounding_mode) {
  opStatus fs;

  if (isInfinity())
    return opOK;

  if (isNaN()) {
    if (isSignaling()) {
      makeQuiet();
      return opInvalidOp;
    }
    return opOK;
  }

  if (isZero())
    return opOK;

  if (exponent + 1 >= (int)semanticsPrecision(*semantics))
    return opOK;

  APInt IntegerConstant(NextPowerOf2(semanticsPrecision(*semantics)), 1);
  IntegerConstant <<= semanticsPrecision(*semantics) - 1;
  IEEEFloat MagicConstant(*semantics);
  fs = MagicConstant.convertFromAPInt(IntegerConstant, false,
                                      rmNearestTiesToEven);
  assert(fs == opOK);
  MagicConstant.sign = sign;

  bool inputSign = isNegative();

  fs = add(MagicConstant, rounding_mode);

  subtract(MagicConstant, rounding_mode);

  if (inputSign != isNegative())
    changeSign();

  return fs;
}

inline IEEEFloat::opStatus IEEEFloat::next(bool nextDown) {
  if (nextDown)
    changeSign();

  opStatus result = opOK;

  switch (category) {
  case fcInfinity:
    if (!isNegative())
      break;
    makeLargest(true);
    break;
  case fcNaN:
    if (isSignaling()) {
      result = opInvalidOp;
      makeNaN(false, isNegative(), 0);
    }
    break;
  case fcZero:
    makeSmallest(false);
    break;
  case fcNormal:
    if (isSmallest() && isNegative()) {
      APInt::tcSet(significandParts(), 0, partCount());
      category = fcZero;
      exponent = 0;
      if (semantics->nanEncoding == fltNanEncoding::NegativeZero)
        sign = false;
      break;
    }

    if (isLargest() && !isNegative()) {
      if (semantics->nonFiniteBehavior == fltNonfiniteBehavior::NanOnly) {
        makeNaN();
        break;
      }
      APInt::tcSet(significandParts(), 0, partCount());
      category = fcInfinity;
      exponent = semantics->maxExponent + 1;
      break;
    }

    if (isNegative()) {
      bool WillCrossBinadeBoundary =
          exponent != semantics->minExponent && isSignificandAllZeros();

      integerPart *Parts = significandParts();
      APInt::tcDecrement(Parts, partCount());

      if (WillCrossBinadeBoundary) {
        APInt::tcSetBit(Parts, semantics->precision - 1);
        exponent--;
      }
    } else {
      bool WillCrossBinadeBoundary = !isDenormal() && isSignificandAllOnes();

      if (WillCrossBinadeBoundary) {
        integerPart *Parts = significandParts();
        APInt::tcSet(Parts, 0, partCount());
        APInt::tcSetBit(Parts, semantics->precision - 1);
        assert(exponent != semantics->maxExponent &&
               "We can not increment an exponent beyond the maxExponent allowed"
               " by the given floating point semantics.");
        exponent++;
      } else {
        incrementSignificand();
      }
    }
    break;
  }

  if (nextDown)
    changeSign();

  return result;
}

inline IEEEFloat::opStatus
IEEEFloat::addOrSubtract(const IEEEFloat &rhs, roundingMode rm, bool subtract) {
  opStatus fs = addOrSubtractSpecials(rhs, subtract);
  if (fs == opDivByZero) {
    lostFraction lf = addOrSubtractSignificand(rhs, subtract);
    fs = normalize(rm, lf);
    assert(category != fcZero || lf == lfExactlyZero);
  }
  if (category == fcZero) {
    if (rhs.category != fcZero || (sign == rhs.sign) == subtract)
      sign = (rm == rmTowardNegative);
    if (semantics->nanEncoding == fltNanEncoding::NegativeZero)
      sign = false;
  }
  return fs;
}

inline IEEEFloat::opStatus IEEEFloat::multiply(const IEEEFloat &rhs,
                                               roundingMode rm) {
  sign ^= rhs.sign;
  opStatus fs = multiplySpecials(rhs);
  if (isZero() && semantics->nanEncoding == fltNanEncoding::NegativeZero)
    sign = false;
  if (isFiniteNonZero()) {
    lostFraction lf = multiplySignificand(rhs);
    fs = normalize(rm, lf);
    if (lf != lfExactlyZero)
      fs = (opStatus)(fs | opInexact);
  }
  return fs;
}

inline IEEEFloat::opStatus IEEEFloat::divide(const IEEEFloat &rhs,
                                             roundingMode rm) {
  sign ^= rhs.sign;
  opStatus fs = divideSpecials(rhs);
  if (isZero() && semantics->nanEncoding == fltNanEncoding::NegativeZero)
    sign = false;
  if (isFiniteNonZero()) {
    lostFraction lf = divideSignificand(rhs);
    fs = normalize(rm, lf);
    if (lf != lfExactlyZero)
      fs = (opStatus)(fs | opInexact);
  }
  return fs;
}

namespace {
inline void append(SmallVectorImpl<char> &Buffer, StringRef Str) {
  Buffer.append(Str.begin(), Str.end());
}

inline void AdjustToPrecision(APInt &significand, int &exp,
                              unsigned FormatPrecision) {
  unsigned bits = significand.getActiveBits();

  unsigned bitsRequired = (FormatPrecision * 196 + 58) / 59;

  if (bits <= bitsRequired)
    return;

  unsigned tensRemovable = (bits - bitsRequired) * 59 / 196;
  if (!tensRemovable)
    return;

  exp += tensRemovable;

  APInt divisor(significand.getBitWidth(), 1);
  APInt powten(significand.getBitWidth(), 10);
  while (true) {
    if (tensRemovable & 1)
      divisor *= powten;
    tensRemovable >>= 1;
    if (!tensRemovable)
      break;
    powten *= powten;
  }

  significand = significand.udiv(divisor);

  significand = significand.trunc(significand.getActiveBits());
}

inline void AdjustToPrecision(SmallVectorImpl<char> &buffer, int &exp,
                              unsigned FormatPrecision) {
  unsigned N = buffer.size();
  if (N <= FormatPrecision)
    return;
  csupport_apfloat_adjust_precision_buffer(buffer.data(), &N, &exp,
                                           FormatPrecision);
  buffer.truncate(N);
}
} // namespace

inline void IEEEFloat::toString(SmallVectorImpl<char> &Str,
                                unsigned FormatPrecision,
                                unsigned FormatMaxPadding,
                                bool TruncateZero) const {
  switch (category) {
  case fcInfinity:
    if (isNegative())
      return append(Str, "-Inf");
    else
      return append(Str, "+Inf");

  case fcNaN:
    return append(Str, "NaN");

  case fcZero:
    if (isNegative())
      Str.push_back('-');

    if (!FormatMaxPadding) {
      if (TruncateZero)
        append(Str, "0.0E+0");
      else {
        append(Str, "0.0");
        if (FormatPrecision > 1)
          Str.append(FormatPrecision - 1, '0');
        append(Str, "e+00");
      }
    } else
      Str.push_back('0');
    return;

  case fcNormal:
    break;
  }

  if (isNegative())
    Str.push_back('-');

  int exp = exponent - ((int)semantics->precision - 1);
  APInt significand(
      semantics->precision,
      ArrayRef(significandParts(),
               csupport_flt_part_count_for_bits(semantics->precision)));

  if (!FormatPrecision) {
    FormatPrecision = 2 + semantics->precision * 59 / 196;
  }

  int trailingZeros = significand.countr_zero();
  exp += trailingZeros;
  significand.lshrInPlace(trailingZeros);

  if (exp == 0) {
  } else if (exp > 0) {
    significand = significand.zext(semantics->precision + exp);
    significand <<= exp;
    exp = 0;
  } else {
    int texp = -exp;

    unsigned precision = semantics->precision + (137 * texp + 136) / 59;

    significand = significand.zext(precision);
    APInt five_to_the_i(precision, 5);
    while (true) {
      if (texp & 1)
        significand *= five_to_the_i;

      texp >>= 1;
      if (!texp)
        break;
      five_to_the_i *= five_to_the_i;
    }
  }

  AdjustToPrecision(significand, exp, FormatPrecision);

  SmallVector<char, 256> buffer;

  unsigned precision = significand.getBitWidth();
  if (precision < 4) {
    precision = 4;
    significand = significand.zext(precision);
  }
  APInt ten(precision, 10);
  APInt digit(precision, 0);

  bool inTrail = true;
  while (significand != 0) {
    APInt::udivrem(significand, ten, significand, digit);

    unsigned d = digit.getZExtValue();

    if (inTrail && !d)
      exp++;
    else {
      buffer.push_back((char)('0' + d));
      inTrail = false;
    }
  }

  assert(!buffer.empty() && "no characters in buffer!");

  AdjustToPrecision(buffer, exp, FormatPrecision);

  unsigned NDigits = buffer.size();

  char fmtbuf[512];
  unsigned fmtlen;
  csupport_apfloat_format_to_string(buffer.data(), NDigits, exp,
                                    FormatPrecision, FormatMaxPadding,
                                    TruncateZero, 0, fmtbuf, &fmtlen);
  Str.append(fmtbuf, fmtbuf + fmtlen);
}

// This mode implements more precise float in terms of two APFloats.
// The interface and layout is designed for arbitrary underlying semantics,
// though currently only PPCDoubleDouble semantics are supported, whose
// corresponding underlying semantics are IEEEdouble.
class DoubleAPFloat final : public APFloatBase {
  // Note: this must be the first data member.
  const fltSemantics *Semantics;
  std::unique_ptr<APFloat[]> Floats;

  opStatus addImpl(const APFloat &a, const APFloat &aa, const APFloat &c,
                   const APFloat &cc, roundingMode RM);

  opStatus addWithSpecial(const DoubleAPFloat &LHS, const DoubleAPFloat &RHS,
                          DoubleAPFloat &Out, roundingMode RM);

public:
  DoubleAPFloat(const fltSemantics &S);
  DoubleAPFloat(const fltSemantics &S, uninitializedTag);
  DoubleAPFloat(const fltSemantics &S, integerPart);
  DoubleAPFloat(const fltSemantics &S, const APInt &I);
  DoubleAPFloat(const fltSemantics &S, APFloat &&First, APFloat &&Second);
  DoubleAPFloat(const DoubleAPFloat &RHS);
  DoubleAPFloat(DoubleAPFloat &&RHS);

  DoubleAPFloat &operator=(const DoubleAPFloat &RHS);
  inline DoubleAPFloat &operator=(DoubleAPFloat &&RHS);

  bool needsCleanup() const { return Floats != nullptr; }

  inline APFloat &getFirst();
  inline const APFloat &getFirst() const;
  inline APFloat &getSecond();
  inline const APFloat &getSecond() const;

  opStatus add(const DoubleAPFloat &RHS, roundingMode RM);
  opStatus subtract(const DoubleAPFloat &RHS, roundingMode RM);
  opStatus multiply(const DoubleAPFloat &RHS, roundingMode RM);
  opStatus divide(const DoubleAPFloat &RHS, roundingMode RM);
  opStatus remainder(const DoubleAPFloat &RHS);
  opStatus mod(const DoubleAPFloat &RHS);
  opStatus fusedMultiplyAdd(const DoubleAPFloat &Multiplicand,
                            const DoubleAPFloat &Addend, roundingMode RM);
  opStatus roundToIntegral(roundingMode RM);
  void changeSign();
  cmpResult compareAbsoluteValue(const DoubleAPFloat &RHS) const;

  fltCategory getCategory() const;
  bool isNegative() const;

  void makeInf(bool Neg);
  void makeZero(bool Neg);
  void makeLargest(bool Neg);
  void makeSmallest(bool Neg);
  void makeSmallestNormalized(bool Neg);
  void makeNaN(bool SNaN, bool Neg, const APInt *fill);

  cmpResult compare(const DoubleAPFloat &RHS) const;
  bool bitwiseIsEqual(const DoubleAPFloat &RHS) const;
  APInt bitcastToAPInt() const;
  Expected<opStatus> convertFromString(StringRef, roundingMode);
  opStatus next(bool nextDown);

  opStatus convertToInteger(MutableArrayRef<integerPart> Input,
                            unsigned int Width, bool IsSigned, roundingMode RM,
                            bool *IsExact) const;
  opStatus convertFromAPInt(const APInt &Input, bool IsSigned, roundingMode RM);
  opStatus convertFromSignExtendedInteger(const integerPart *Input,
                                          unsigned int InputSize, bool IsSigned,
                                          roundingMode RM);
  opStatus convertFromZeroExtendedInteger(const integerPart *Input,
                                          unsigned int InputSize, bool IsSigned,
                                          roundingMode RM);
  unsigned int convertToHexString(char *DST, unsigned int HexDigits,
                                  bool UpperCase, roundingMode RM) const;

  bool isDenormal() const;
  bool isSmallest() const;
  bool isSmallestNormalized() const;
  bool isLargest() const;
  bool isInteger() const;

  void toString(SmallVectorImpl<char> &Str, unsigned FormatPrecision,
                unsigned FormatMaxPadding, bool TruncateZero = true) const;

  bool getExactInverse(APFloat *inv) const;

  LLVM_READONLY
  int getExactLog2() const;
  LLVM_READONLY
  int getExactLog2Abs() const;

  friend DoubleAPFloat scalbn(const DoubleAPFloat &X, int Exp, roundingMode);
  friend DoubleAPFloat frexp(const DoubleAPFloat &X, int &Exp, roundingMode);
  friend hash_code hash_value(const DoubleAPFloat &Arg);
};

DoubleAPFloat scalbn(const DoubleAPFloat &Arg, int Exp,
                     IEEEFloat::roundingMode RM);
DoubleAPFloat frexp(const DoubleAPFloat &X, int &Exp, IEEEFloat::roundingMode);

} // namespace detail

// This is a interface class that is currently forwarding functionalities from
// detail::IEEEFloat.
class APFloat : public APFloatBase {
  typedef detail::IEEEFloat IEEEFloat;
  typedef detail::DoubleAPFloat DoubleAPFloat;

  static_assert(std::is_standard_layout<IEEEFloat>::value);

  union Storage {
    const fltSemantics *semantics;
    IEEEFloat IEEE;
    DoubleAPFloat Double;

    explicit Storage(IEEEFloat F, const fltSemantics &S);
    explicit Storage(DoubleAPFloat F, const fltSemantics &S)
        : Double(std::move(F)) {
      assert(&S == &PPCDoubleDouble());
    }

    template <typename... ArgTypes>
    Storage(const fltSemantics &Semantics, ArgTypes &&...Args) {
      if (usesLayout<IEEEFloat>(Semantics)) {
        new (&IEEE) IEEEFloat(Semantics, std::forward<ArgTypes>(Args)...);
        return;
      }
      if (usesLayout<DoubleAPFloat>(Semantics)) {
        new (&Double) DoubleAPFloat(Semantics, std::forward<ArgTypes>(Args)...);
        return;
      }
      llvm_unreachable("Unexpected semantics");
    }

    ~Storage() {
      if (usesLayout<IEEEFloat>(*semantics)) {
        IEEE.~IEEEFloat();
        return;
      }
      if (usesLayout<DoubleAPFloat>(*semantics)) {
        Double.~DoubleAPFloat();
        return;
      }
      llvm_unreachable("Unexpected semantics");
    }

    Storage(const Storage &RHS) {
      if (usesLayout<IEEEFloat>(*RHS.semantics)) {
        new (this) IEEEFloat(RHS.IEEE);
        return;
      }
      if (usesLayout<DoubleAPFloat>(*RHS.semantics)) {
        new (this) DoubleAPFloat(RHS.Double);
        return;
      }
      llvm_unreachable("Unexpected semantics");
    }

    Storage(Storage &&RHS) {
      if (usesLayout<IEEEFloat>(*RHS.semantics)) {
        new (this) IEEEFloat(std::move(RHS.IEEE));
        return;
      }
      if (usesLayout<DoubleAPFloat>(*RHS.semantics)) {
        new (this) DoubleAPFloat(std::move(RHS.Double));
        return;
      }
      llvm_unreachable("Unexpected semantics");
    }

    Storage &operator=(const Storage &RHS) {
      if (usesLayout<IEEEFloat>(*semantics) &&
          usesLayout<IEEEFloat>(*RHS.semantics)) {
        IEEE = RHS.IEEE;
      } else if (usesLayout<DoubleAPFloat>(*semantics) &&
                 usesLayout<DoubleAPFloat>(*RHS.semantics)) {
        Double = RHS.Double;
      } else if (this != &RHS) {
        this->~Storage();
        new (this) Storage(RHS);
      }
      return *this;
    }

    Storage &operator=(Storage &&RHS) {
      if (usesLayout<IEEEFloat>(*semantics) &&
          usesLayout<IEEEFloat>(*RHS.semantics)) {
        IEEE = std::move(RHS.IEEE);
      } else if (usesLayout<DoubleAPFloat>(*semantics) &&
                 usesLayout<DoubleAPFloat>(*RHS.semantics)) {
        Double = std::move(RHS.Double);
      } else if (this != &RHS) {
        this->~Storage();
        new (this) Storage(std::move(RHS));
      }
      return *this;
    }
  } U;

  template <typename T> static bool usesLayout(const fltSemantics &Semantics) {
    static_assert(std::is_same<T, IEEEFloat>::value ||
                  std::is_same<T, DoubleAPFloat>::value);
    if (std::is_same<T, DoubleAPFloat>::value) {
      return &Semantics == &PPCDoubleDouble();
    }
    return &Semantics != &PPCDoubleDouble();
  }

  IEEEFloat &getIEEE() {
    if (usesLayout<IEEEFloat>(*U.semantics))
      return U.IEEE;
    if (usesLayout<DoubleAPFloat>(*U.semantics))
      return U.Double.getFirst().U.IEEE;
    llvm_unreachable("Unexpected semantics");
  }

  const IEEEFloat &getIEEE() const {
    if (usesLayout<IEEEFloat>(*U.semantics))
      return U.IEEE;
    if (usesLayout<DoubleAPFloat>(*U.semantics))
      return U.Double.getFirst().U.IEEE;
    llvm_unreachable("Unexpected semantics");
  }

  void makeZero(bool Neg) { APFLOAT_DISPATCH_ON_SEMANTICS(makeZero(Neg)); }

  void makeInf(bool Neg) { APFLOAT_DISPATCH_ON_SEMANTICS(makeInf(Neg)); }

  void makeNaN(bool SNaN, bool Neg, const APInt *fill) {
    APFLOAT_DISPATCH_ON_SEMANTICS(makeNaN(SNaN, Neg, fill));
  }

  void makeLargest(bool Neg) {
    APFLOAT_DISPATCH_ON_SEMANTICS(makeLargest(Neg));
  }

  void makeSmallest(bool Neg) {
    APFLOAT_DISPATCH_ON_SEMANTICS(makeSmallest(Neg));
  }

  void makeSmallestNormalized(bool Neg) {
    APFLOAT_DISPATCH_ON_SEMANTICS(makeSmallestNormalized(Neg));
  }

  explicit APFloat(IEEEFloat F, const fltSemantics &S) : U(std::move(F), S) {}
  explicit APFloat(DoubleAPFloat F, const fltSemantics &S)
      : U(std::move(F), S) {}

  cmpResult compareAbsoluteValue(const APFloat &RHS) const {
    assert(&getSemantics() == &RHS.getSemantics() &&
           "Should only compare APFloats with the same semantics");
    if (usesLayout<IEEEFloat>(getSemantics()))
      return U.IEEE.compareAbsoluteValue(RHS.U.IEEE);
    if (usesLayout<DoubleAPFloat>(getSemantics()))
      return U.Double.compareAbsoluteValue(RHS.U.Double);
    llvm_unreachable("Unexpected semantics");
  }

public:
  APFloat(const fltSemantics &Semantics) : U(Semantics) {}
  APFloat(const fltSemantics &Semantics, StringRef S);
  APFloat(const fltSemantics &Semantics, integerPart I) : U(Semantics, I) {}
  template <typename T,
            typename = std::enable_if_t<std::is_floating_point<T>::value>>
  APFloat(const fltSemantics &Semantics, T V) = delete;
  // TODO: Remove this constructor. This isn't faster than the first one.
  APFloat(const fltSemantics &Semantics, uninitializedTag)
      : U(Semantics, uninitialized) {}
  APFloat(const fltSemantics &Semantics, const APInt &I) : U(Semantics, I) {}
  explicit APFloat(double d) : U(IEEEFloat(d), IEEEdouble()) {}
  explicit APFloat(float f) : U(IEEEFloat(f), IEEEsingle()) {}
  APFloat(const APFloat &RHS) = default;
  APFloat(APFloat &&RHS) = default;

  ~APFloat() = default;

  bool needsCleanup() const { APFLOAT_DISPATCH_ON_SEMANTICS(needsCleanup()); }

  /// Factory for Positive and Negative Zero.
  ///
  /// \param Negative True iff the number should be negative.
  static APFloat getZero(const fltSemantics &Sem, bool Negative = false) {
    APFloat Val(Sem, uninitialized);
    Val.makeZero(Negative);
    return Val;
  }

  /// Factory for Positive and Negative Infinity.
  ///
  /// \param Negative True iff the number should be negative.
  static APFloat getInf(const fltSemantics &Sem, bool Negative = false) {
    APFloat Val(Sem, uninitialized);
    Val.makeInf(Negative);
    return Val;
  }

  /// Factory for NaN values.
  ///
  /// \param Negative - True iff the NaN generated should be negative.
  /// \param payload - The unspecified fill bits for creating the NaN, 0 by
  /// default.  The value is truncated as necessary.
  static APFloat getNaN(const fltSemantics &Sem, bool Negative = false,
                        uint64_t payload = 0) {
    if (payload) {
      APInt intPayload(64, payload);
      return getQNaN(Sem, Negative, &intPayload);
    } else {
      return getQNaN(Sem, Negative, nullptr);
    }
  }

  /// Factory for QNaN values.
  static APFloat getQNaN(const fltSemantics &Sem, bool Negative = false,
                         const APInt *payload = nullptr) {
    APFloat Val(Sem, uninitialized);
    Val.makeNaN(false, Negative, payload);
    return Val;
  }

  /// Factory for SNaN values.
  static APFloat getSNaN(const fltSemantics &Sem, bool Negative = false,
                         const APInt *payload = nullptr) {
    APFloat Val(Sem, uninitialized);
    Val.makeNaN(true, Negative, payload);
    return Val;
  }

  /// Returns the largest finite number in the given semantics.
  ///
  /// \param Negative - True iff the number should be negative
  static APFloat getLargest(const fltSemantics &Sem, bool Negative = false) {
    APFloat Val(Sem, uninitialized);
    Val.makeLargest(Negative);
    return Val;
  }

  /// Returns the smallest (by magnitude) finite number in the given semantics.
  /// Might be denormalized, which implies a relative loss of precision.
  ///
  /// \param Negative - True iff the number should be negative
  static APFloat getSmallest(const fltSemantics &Sem, bool Negative = false) {
    APFloat Val(Sem, uninitialized);
    Val.makeSmallest(Negative);
    return Val;
  }

  /// Returns the smallest (by magnitude) normalized finite number in the given
  /// semantics.
  ///
  /// \param Negative - True iff the number should be negative
  static APFloat getSmallestNormalized(const fltSemantics &Sem,
                                       bool Negative = false) {
    APFloat Val(Sem, uninitialized);
    Val.makeSmallestNormalized(Negative);
    return Val;
  }

  /// Returns a float which is bitcasted from an all one value int.
  ///
  /// \param Semantics - type float semantics
  static APFloat getAllOnesValue(const fltSemantics &Semantics);

  /// Used to insert APFloat objects, or objects that contain APFloat objects,
  /// into FoldingSets.
  void Profile(FoldingSetNodeID &NID) const;

  opStatus add(const APFloat &RHS, roundingMode RM) {
    assert(&getSemantics() == &RHS.getSemantics() &&
           "Should only call on two APFloats with the same semantics");
    if (usesLayout<IEEEFloat>(getSemantics()))
      return U.IEEE.add(RHS.U.IEEE, RM);
    if (usesLayout<DoubleAPFloat>(getSemantics()))
      return U.Double.add(RHS.U.Double, RM);
    llvm_unreachable("Unexpected semantics");
  }
  opStatus subtract(const APFloat &RHS, roundingMode RM) {
    assert(&getSemantics() == &RHS.getSemantics() &&
           "Should only call on two APFloats with the same semantics");
    if (usesLayout<IEEEFloat>(getSemantics()))
      return U.IEEE.subtract(RHS.U.IEEE, RM);
    if (usesLayout<DoubleAPFloat>(getSemantics()))
      return U.Double.subtract(RHS.U.Double, RM);
    llvm_unreachable("Unexpected semantics");
  }
  opStatus multiply(const APFloat &RHS, roundingMode RM) {
    assert(&getSemantics() == &RHS.getSemantics() &&
           "Should only call on two APFloats with the same semantics");
    if (usesLayout<IEEEFloat>(getSemantics()))
      return U.IEEE.multiply(RHS.U.IEEE, RM);
    if (usesLayout<DoubleAPFloat>(getSemantics()))
      return U.Double.multiply(RHS.U.Double, RM);
    llvm_unreachable("Unexpected semantics");
  }
  opStatus divide(const APFloat &RHS, roundingMode RM) {
    assert(&getSemantics() == &RHS.getSemantics() &&
           "Should only call on two APFloats with the same semantics");
    if (usesLayout<IEEEFloat>(getSemantics()))
      return U.IEEE.divide(RHS.U.IEEE, RM);
    if (usesLayout<DoubleAPFloat>(getSemantics()))
      return U.Double.divide(RHS.U.Double, RM);
    llvm_unreachable("Unexpected semantics");
  }
  opStatus remainder(const APFloat &RHS) {
    assert(&getSemantics() == &RHS.getSemantics() &&
           "Should only call on two APFloats with the same semantics");
    if (usesLayout<IEEEFloat>(getSemantics()))
      return U.IEEE.remainder(RHS.U.IEEE);
    if (usesLayout<DoubleAPFloat>(getSemantics()))
      return U.Double.remainder(RHS.U.Double);
    llvm_unreachable("Unexpected semantics");
  }
  opStatus mod(const APFloat &RHS) {
    assert(&getSemantics() == &RHS.getSemantics() &&
           "Should only call on two APFloats with the same semantics");
    if (usesLayout<IEEEFloat>(getSemantics()))
      return U.IEEE.mod(RHS.U.IEEE);
    if (usesLayout<DoubleAPFloat>(getSemantics()))
      return U.Double.mod(RHS.U.Double);
    llvm_unreachable("Unexpected semantics");
  }
  opStatus fusedMultiplyAdd(const APFloat &Multiplicand, const APFloat &Addend,
                            roundingMode RM) {
    assert(&getSemantics() == &Multiplicand.getSemantics() &&
           "Should only call on APFloats with the same semantics");
    assert(&getSemantics() == &Addend.getSemantics() &&
           "Should only call on APFloats with the same semantics");
    if (usesLayout<IEEEFloat>(getSemantics()))
      return U.IEEE.fusedMultiplyAdd(Multiplicand.U.IEEE, Addend.U.IEEE, RM);
    if (usesLayout<DoubleAPFloat>(getSemantics()))
      return U.Double.fusedMultiplyAdd(Multiplicand.U.Double, Addend.U.Double,
                                       RM);
    llvm_unreachable("Unexpected semantics");
  }
  opStatus roundToIntegral(roundingMode RM) {
    APFLOAT_DISPATCH_ON_SEMANTICS(roundToIntegral(RM));
  }

  // TODO: bool parameters are not readable and a source of bugs.
  // Do something.
  opStatus next(bool nextDown) {
    APFLOAT_DISPATCH_ON_SEMANTICS(next(nextDown));
  }

  /// Negate an APFloat.
  APFloat operator-() const {
    APFloat Result(*this);
    Result.changeSign();
    return Result;
  }

  /// Add two APFloats, rounding ties to the nearest even.
  /// No error checking.
  APFloat operator+(const APFloat &RHS) const {
    APFloat Result(*this);
    (void)Result.add(RHS, rmNearestTiesToEven);
    return Result;
  }

  /// Subtract two APFloats, rounding ties to the nearest even.
  /// No error checking.
  APFloat operator-(const APFloat &RHS) const {
    APFloat Result(*this);
    (void)Result.subtract(RHS, rmNearestTiesToEven);
    return Result;
  }

  /// Multiply two APFloats, rounding ties to the nearest even.
  /// No error checking.
  APFloat operator*(const APFloat &RHS) const {
    APFloat Result(*this);
    (void)Result.multiply(RHS, rmNearestTiesToEven);
    return Result;
  }

  /// Divide the first APFloat by the second, rounding ties to the nearest even.
  /// No error checking.
  APFloat operator/(const APFloat &RHS) const {
    APFloat Result(*this);
    (void)Result.divide(RHS, rmNearestTiesToEven);
    return Result;
  }

  void changeSign() { APFLOAT_DISPATCH_ON_SEMANTICS(changeSign()); }
  void clearSign() {
    if (isNegative())
      changeSign();
  }
  void copySign(const APFloat &RHS) {
    if (isNegative() != RHS.isNegative())
      changeSign();
  }

  /// A static helper to produce a copy of an APFloat value with its sign
  /// copied from some other APFloat.
  static APFloat copySign(APFloat Value, const APFloat &Sign) {
    Value.copySign(Sign);
    return Value;
  }

  /// Assuming this is an IEEE-754 NaN value, quiet its signaling bit.
  /// This preserves the sign and payload bits.
  APFloat makeQuiet() const {
    APFloat Result(*this);
    Result.getIEEE().makeQuiet();
    return Result;
  }

  opStatus convert(const fltSemantics &ToSemantics, roundingMode RM,
                   bool *losesInfo);
  opStatus convertToInteger(MutableArrayRef<integerPart> Input,
                            unsigned int Width, bool IsSigned, roundingMode RM,
                            bool *IsExact) const {
    APFLOAT_DISPATCH_ON_SEMANTICS(
        convertToInteger(Input, Width, IsSigned, RM, IsExact));
  }
  opStatus convertToInteger(APSInt &Result, roundingMode RM,
                            bool *IsExact) const;
  opStatus convertFromAPInt(const APInt &Input, bool IsSigned,
                            roundingMode RM) {
    APFLOAT_DISPATCH_ON_SEMANTICS(convertFromAPInt(Input, IsSigned, RM));
  }
  opStatus convertFromSignExtendedInteger(const integerPart *Input,
                                          unsigned int InputSize, bool IsSigned,
                                          roundingMode RM) {
    APFLOAT_DISPATCH_ON_SEMANTICS(
        convertFromSignExtendedInteger(Input, InputSize, IsSigned, RM));
  }
  opStatus convertFromZeroExtendedInteger(const integerPart *Input,
                                          unsigned int InputSize, bool IsSigned,
                                          roundingMode RM) {
    APFLOAT_DISPATCH_ON_SEMANTICS(
        convertFromZeroExtendedInteger(Input, InputSize, IsSigned, RM));
  }
  Expected<opStatus> convertFromString(StringRef Str, roundingMode RM) {
    APFLOAT_DISPATCH_ON_SEMANTICS(convertFromString(Str, RM));
  }
  APInt bitcastToAPInt() const {
    APFLOAT_DISPATCH_ON_SEMANTICS(bitcastToAPInt());
  }

  /// Converts this APFloat to host double value.
  ///
  /// \pre The APFloat must be built using semantics, that can be represented by
  /// the host double type without loss of precision. It can be IEEEdouble and
  /// shorter semantics, like IEEEsingle and others.
  double convertToDouble() const;

  /// Converts this APFloat to host float value.
  ///
  /// \pre The APFloat must be built using semantics, that can be represented by
  /// the host float type without loss of precision. It can be IEEEsingle and
  /// shorter semantics, like IEEEhalf.
  float convertToFloat() const;

  bool operator==(const APFloat &RHS) const { return compare(RHS) == cmpEqual; }

  bool operator!=(const APFloat &RHS) const { return compare(RHS) != cmpEqual; }

  bool operator<(const APFloat &RHS) const {
    return compare(RHS) == cmpLessThan;
  }

  bool operator>(const APFloat &RHS) const {
    return compare(RHS) == cmpGreaterThan;
  }

  bool operator<=(const APFloat &RHS) const {
    cmpResult Res = compare(RHS);
    return Res == cmpLessThan || Res == cmpEqual;
  }

  bool operator>=(const APFloat &RHS) const {
    cmpResult Res = compare(RHS);
    return Res == cmpGreaterThan || Res == cmpEqual;
  }

  cmpResult compare(const APFloat &RHS) const {
    assert(&getSemantics() == &RHS.getSemantics() &&
           "Should only compare APFloats with the same semantics");
    if (usesLayout<IEEEFloat>(getSemantics()))
      return U.IEEE.compare(RHS.U.IEEE);
    if (usesLayout<DoubleAPFloat>(getSemantics()))
      return U.Double.compare(RHS.U.Double);
    llvm_unreachable("Unexpected semantics");
  }

  bool bitwiseIsEqual(const APFloat &RHS) const {
    if (&getSemantics() != &RHS.getSemantics())
      return false;
    if (usesLayout<IEEEFloat>(getSemantics()))
      return U.IEEE.bitwiseIsEqual(RHS.U.IEEE);
    if (usesLayout<DoubleAPFloat>(getSemantics()))
      return U.Double.bitwiseIsEqual(RHS.U.Double);
    llvm_unreachable("Unexpected semantics");
  }

  /// We don't rely on operator== working on double values, as
  /// it returns true for things that are clearly not equal, like -0.0 and 0.0.
  /// As such, this method can be used to do an exact bit-for-bit comparison of
  /// two floating point values.
  ///
  /// We leave the version with the double argument here because it's just so
  /// convenient to write "2.0" and the like.  Without this function we'd
  /// have to duplicate its logic everywhere it's called.
  bool isExactlyValue(double V) const {
    bool ignored;
    APFloat Tmp(V);
    Tmp.convert(getSemantics(), APFloat::rmNearestTiesToEven, &ignored);
    return bitwiseIsEqual(Tmp);
  }

  unsigned int convertToHexString(char *DST, unsigned int HexDigits,
                                  bool UpperCase, roundingMode RM) const {
    APFLOAT_DISPATCH_ON_SEMANTICS(
        convertToHexString(DST, HexDigits, UpperCase, RM));
  }

  bool isZero() const { return getCategory() == fcZero; }
  bool isInfinity() const { return getCategory() == fcInfinity; }
  bool isNaN() const { return getCategory() == fcNaN; }

  bool isNegative() const { return getIEEE().isNegative(); }
  bool isDenormal() const { APFLOAT_DISPATCH_ON_SEMANTICS(isDenormal()); }
  bool isSignaling() const { return getIEEE().isSignaling(); }

  bool isNormal() const { return !isDenormal() && isFiniteNonZero(); }
  bool isFinite() const { return !isNaN() && !isInfinity(); }

  fltCategory getCategory() const { return getIEEE().getCategory(); }
  const fltSemantics &getSemantics() const { return *U.semantics; }
  bool isNonZero() const { return !isZero(); }
  bool isFiniteNonZero() const { return isFinite() && !isZero(); }
  bool isPosZero() const { return isZero() && !isNegative(); }
  bool isNegZero() const { return isZero() && isNegative(); }
  bool isPosInfinity() const { return isInfinity() && !isNegative(); }
  bool isNegInfinity() const { return isInfinity() && isNegative(); }
  bool isSmallest() const { APFLOAT_DISPATCH_ON_SEMANTICS(isSmallest()); }
  bool isLargest() const { APFLOAT_DISPATCH_ON_SEMANTICS(isLargest()); }
  bool isInteger() const { APFLOAT_DISPATCH_ON_SEMANTICS(isInteger()); }
  bool isIEEE() const { return usesLayout<IEEEFloat>(getSemantics()); }

  bool isSmallestNormalized() const {
    APFLOAT_DISPATCH_ON_SEMANTICS(isSmallestNormalized());
  }

  /// Return the FPClassTest which will return true for the value.
  FPClassTest classify() const;

  APFloat &operator=(const APFloat &RHS) = default;
  APFloat &operator=(APFloat &&RHS) = default;

  void toString(SmallVectorImpl<char> &Str, unsigned FormatPrecision = 0,
                unsigned FormatMaxPadding = 3, bool TruncateZero = true) const {
    APFLOAT_DISPATCH_ON_SEMANTICS(
        toString(Str, FormatPrecision, FormatMaxPadding, TruncateZero));
  }

  void print(raw_ostream &) const;
  void dump() const;

  bool getExactInverse(APFloat *inv) const {
    APFLOAT_DISPATCH_ON_SEMANTICS(getExactInverse(inv));
  }

  LLVM_READONLY
  int getExactLog2Abs() const {
    APFLOAT_DISPATCH_ON_SEMANTICS(getExactLog2Abs());
  }

  LLVM_READONLY
  int getExactLog2() const { APFLOAT_DISPATCH_ON_SEMANTICS(getExactLog2()); }

  friend hash_code hash_value(const APFloat &Arg);
  friend int ilogb(const APFloat &Arg) { return ilogb(Arg.getIEEE()); }
  friend APFloat scalbn(APFloat X, int Exp, roundingMode RM);
  friend APFloat frexp(const APFloat &X, int &Exp, roundingMode RM);
  friend IEEEFloat;
  friend DoubleAPFloat;
};

/// See friend declarations above.
///
/// These additional declarations are required in order to compile LLVM with IBM
/// xlC compiler.
hash_code hash_value(const APFloat &Arg);

namespace detail {
inline bool IEEEFloat::getExactInverse(APFloat *inv) const {
  if (!isFiniteNonZero())
    return false;

  if (significandLSB() != semantics->precision - 1)
    return false;

  IEEEFloat reciprocal(*semantics, 1ULL);
  if (reciprocal.divide(*this, rmNearestTiesToEven) != opOK)
    return false;

  if (reciprocal.isDenormal())
    return false;

  assert(reciprocal.isFiniteNonZero() &&
         reciprocal.significandLSB() == reciprocal.semantics->precision - 1);

  if (inv)
    *inv = APFloat(reciprocal, *semantics);

  return true;
}

inline bool DoubleAPFloat::getExactInverse(APFloat *inv) const {
  assert(Semantics == &APFloatBase::PPCDoubleDouble() &&
         "Unexpected Semantics");
  const fltSemantics &LegacySem = *reinterpret_cast<const fltSemantics *>(
      &csupport_sem_ppc_double_double_legacy);
  APFloat Tmp(LegacySem, bitcastToAPInt());
  if (!inv)
    return Tmp.getExactInverse(nullptr);
  APFloat Inv(LegacySem);
  bool Ret = Tmp.getExactInverse(&Inv);
  *inv = APFloat(APFloatBase::PPCDoubleDouble(), Inv.bitcastToAPInt());
  return Ret;
}

inline Expected<IEEEFloat::opStatus>
DoubleAPFloat::convertFromString(StringRef S, roundingMode RM) {
  assert(Semantics == &APFloatBase::PPCDoubleDouble() &&
         "Unexpected Semantics");
  const fltSemantics &LegacySem = *reinterpret_cast<const fltSemantics *>(
      &csupport_sem_ppc_double_double_legacy);
  APFloat Tmp(LegacySem);
  auto Ret = Tmp.convertFromString(S, RM);
  *this = DoubleAPFloat(APFloatBase::PPCDoubleDouble(), Tmp.bitcastToAPInt());
  return Ret;
}

inline APFloat::opStatus DoubleAPFloat::next(bool nextDown) {
  assert(Semantics == &APFloatBase::PPCDoubleDouble() &&
         "Unexpected Semantics");
  const fltSemantics &LegacySem = *reinterpret_cast<const fltSemantics *>(
      &csupport_sem_ppc_double_double_legacy);
  APFloat Tmp(LegacySem, bitcastToAPInt());
  auto Ret = Tmp.next(nextDown);
  *this = DoubleAPFloat(APFloatBase::PPCDoubleDouble(), Tmp.bitcastToAPInt());
  return Ret;
}

inline APFloat::opStatus
DoubleAPFloat::convertToInteger(MutableArrayRef<integerPart> Input,
                                unsigned int Width, bool IsSigned,
                                roundingMode RM, bool *IsExact) const {
  assert(Semantics == &APFloatBase::PPCDoubleDouble() &&
         "Unexpected Semantics");
  const fltSemantics &LegacySem = *reinterpret_cast<const fltSemantics *>(
      &csupport_sem_ppc_double_double_legacy);
  return APFloat(LegacySem, bitcastToAPInt())
      .convertToInteger(Input, Width, IsSigned, RM, IsExact);
}

inline APFloat::opStatus DoubleAPFloat::convertFromAPInt(const APInt &Input,
                                                         bool IsSigned,
                                                         roundingMode RM) {
  assert(Semantics == &APFloatBase::PPCDoubleDouble() &&
         "Unexpected Semantics");
  const fltSemantics &LegacySem = *reinterpret_cast<const fltSemantics *>(
      &csupport_sem_ppc_double_double_legacy);
  APFloat Tmp(LegacySem);
  auto Ret = Tmp.convertFromAPInt(Input, IsSigned, RM);
  *this = DoubleAPFloat(APFloatBase::PPCDoubleDouble(), Tmp.bitcastToAPInt());
  return Ret;
}

inline APFloat::opStatus
DoubleAPFloat::convertFromSignExtendedInteger(const integerPart *Input,
                                              unsigned int InputSize,
                                              bool IsSigned, roundingMode RM) {
  assert(Semantics == &APFloatBase::PPCDoubleDouble() &&
         "Unexpected Semantics");
  const fltSemantics &LegacySem = *reinterpret_cast<const fltSemantics *>(
      &csupport_sem_ppc_double_double_legacy);
  APFloat Tmp(LegacySem);
  auto Ret = Tmp.convertFromSignExtendedInteger(Input, InputSize, IsSigned, RM);
  *this = DoubleAPFloat(APFloatBase::PPCDoubleDouble(), Tmp.bitcastToAPInt());
  return Ret;
}

inline APFloat::opStatus
DoubleAPFloat::convertFromZeroExtendedInteger(const integerPart *Input,
                                              unsigned int InputSize,
                                              bool IsSigned, roundingMode RM) {
  assert(Semantics == &APFloatBase::PPCDoubleDouble() &&
         "Unexpected Semantics");
  const fltSemantics &LegacySem = *reinterpret_cast<const fltSemantics *>(
      &csupport_sem_ppc_double_double_legacy);
  APFloat Tmp(LegacySem);
  auto Ret = Tmp.convertFromZeroExtendedInteger(Input, InputSize, IsSigned, RM);
  *this = DoubleAPFloat(APFloatBase::PPCDoubleDouble(), Tmp.bitcastToAPInt());
  return Ret;
}

inline unsigned int DoubleAPFloat::convertToHexString(char *DST,
                                                      unsigned int HexDigits,
                                                      bool UpperCase,
                                                      roundingMode RM) const {
  assert(Semantics == &APFloatBase::PPCDoubleDouble() &&
         "Unexpected Semantics");
  const fltSemantics &LegacySem = *reinterpret_cast<const fltSemantics *>(
      &csupport_sem_ppc_double_double_legacy);
  return APFloat(LegacySem, bitcastToAPInt())
      .convertToHexString(DST, HexDigits, UpperCase, RM);
}

inline bool DoubleAPFloat::isDenormal() const {
  return getCategory() == fcNormal &&
         (Floats[0].isDenormal() || Floats[1].isDenormal() ||
          Floats[0] != Floats[0] + Floats[1]);
}

inline bool DoubleAPFloat::isSmallest() const {
  if (getCategory() != fcNormal)
    return false;
  DoubleAPFloat Tmp(*this);
  Tmp.makeSmallest(isNegative());
  return Tmp.compare(*this) == cmpEqual;
}

inline bool DoubleAPFloat::isSmallestNormalized() const {
  if (getCategory() != fcNormal)
    return false;
  DoubleAPFloat Tmp(*this);
  Tmp.makeSmallestNormalized(isNegative());
  return Tmp.compare(*this) == cmpEqual;
}

inline bool DoubleAPFloat::isLargest() const {
  if (getCategory() != fcNormal)
    return false;
  DoubleAPFloat Tmp(*this);
  Tmp.makeLargest(isNegative());
  return Tmp.compare(*this) == cmpEqual;
}

inline bool DoubleAPFloat::isInteger() const {
  assert(Semantics == &APFloatBase::PPCDoubleDouble() &&
         "Unexpected Semantics");
  return Floats[0].isInteger() && Floats[1].isInteger();
}

inline void DoubleAPFloat::toString(SmallVectorImpl<char> &Str,
                                    unsigned FormatPrecision,
                                    unsigned FormatMaxPadding,
                                    bool TruncateZero) const {
  assert(Semantics == &APFloatBase::PPCDoubleDouble() &&
         "Unexpected Semantics");
  const fltSemantics &LegacySem = *reinterpret_cast<const fltSemantics *>(
      &csupport_sem_ppc_double_double_legacy);
  APFloat(LegacySem, bitcastToAPInt())
      .toString(Str, FormatPrecision, FormatMaxPadding, TruncateZero);
}

inline hash_code hash_value(const DoubleAPFloat &Arg) {
  if (Arg.Floats)
    return hash_combine(hash_value(Arg.Floats[0]), hash_value(Arg.Floats[1]));
  return hash_combine(Arg.Semantics);
}
} // namespace detail

inline hash_code hash_value(const APFloat &Arg) {
  if (APFloat::usesLayout<detail::IEEEFloat>(Arg.getSemantics()))
    return hash_value(Arg.U.IEEE);
  if (APFloat::usesLayout<detail::DoubleAPFloat>(Arg.getSemantics()))
    return hash_value(Arg.U.Double);
  llvm_unreachable("Unexpected semantics");
}

inline APFloat scalbn(APFloat X, int Exp, APFloat::roundingMode RM) {
  if (APFloat::usesLayout<detail::IEEEFloat>(X.getSemantics()))
    return APFloat(scalbn(X.U.IEEE, Exp, RM), X.getSemantics());
  if (APFloat::usesLayout<detail::DoubleAPFloat>(X.getSemantics()))
    return APFloat(scalbn(X.U.Double, Exp, RM), X.getSemantics());
  llvm_unreachable("Unexpected semantics");
}

/// Equivalent of C standard library function.
///
/// While the C standard says Exp is an unspecified value for infinity and nan,
/// this returns INT_MAX for infinities, and INT_MIN for NaNs.
inline APFloat frexp(const APFloat &X, int &Exp, APFloat::roundingMode RM) {
  if (APFloat::usesLayout<detail::IEEEFloat>(X.getSemantics()))
    return APFloat(frexp(X.U.IEEE, Exp, RM), X.getSemantics());
  if (APFloat::usesLayout<detail::DoubleAPFloat>(X.getSemantics()))
    return APFloat(frexp(X.U.Double, Exp, RM), X.getSemantics());
  llvm_unreachable("Unexpected semantics");
}

namespace detail {
inline DoubleAPFloat scalbn(const DoubleAPFloat &Arg, int Exp,
                            IEEEFloat::roundingMode RM) {
  assert(Arg.Semantics == &APFloatBase::PPCDoubleDouble() &&
         "Unexpected Semantics");
  return DoubleAPFloat(APFloatBase::PPCDoubleDouble(),
                       llvm::scalbn(Arg.Floats[0], Exp, RM),
                       llvm::scalbn(Arg.Floats[1], Exp, RM));
}

inline DoubleAPFloat frexp(const DoubleAPFloat &Arg, int &Exp,
                           IEEEFloat::roundingMode RM) {
  assert(Arg.Semantics == &APFloatBase::PPCDoubleDouble() &&
         "Unexpected Semantics");
  APFloat First = llvm::frexp(Arg.Floats[0], Exp, RM);
  APFloat Second = Arg.Floats[1];
  if (Arg.getCategory() == APFloat::fcNormal)
    Second = llvm::scalbn(Second, -Exp, RM);
  return DoubleAPFloat(APFloatBase::PPCDoubleDouble(), std::move(First),
                       std::move(Second));
}
} // namespace detail

/// Returns the absolute value of the argument.
inline APFloat abs(APFloat X) {
  X.clearSign();
  return X;
}

/// Returns the negated value of the argument.
inline APFloat neg(APFloat X) {
  X.changeSign();
  return X;
}

/// Implements IEEE minNum semantics. Returns the smaller of the 2 arguments if
/// both are not NaN. If either argument is a NaN, returns the other argument.
LLVM_READONLY
inline APFloat minnum(const APFloat &A, const APFloat &B) {
  if (A.isNaN())
    return B;
  if (B.isNaN())
    return A;
  return B < A ? B : A;
}

/// Implements IEEE maxNum semantics. Returns the larger of the 2 arguments if
/// both are not NaN. If either argument is a NaN, returns the other argument.
LLVM_READONLY
inline APFloat maxnum(const APFloat &A, const APFloat &B) {
  if (A.isNaN())
    return B;
  if (B.isNaN())
    return A;
  return A < B ? B : A;
}

/// Implements IEEE 754-2018 minimum semantics. Returns the smaller of 2
/// arguments, propagating NaNs and treating -0 as less than +0.
LLVM_READONLY
inline APFloat minimum(const APFloat &A, const APFloat &B) {
  if (A.isNaN())
    return A;
  if (B.isNaN())
    return B;
  if (A.isZero() && B.isZero() && (A.isNegative() != B.isNegative()))
    return A.isNegative() ? A : B;
  return B < A ? B : A;
}

/// Implements IEEE 754-2018 maximum semantics. Returns the larger of 2
/// arguments, propagating NaNs and treating -0 as less than +0.
LLVM_READONLY
inline APFloat maximum(const APFloat &A, const APFloat &B) {
  if (A.isNaN())
    return A;
  if (B.isNaN())
    return B;
  if (A.isZero() && B.isZero() && (A.isNegative() != B.isNegative()))
    return A.isNegative() ? B : A;
  return A < B ? B : A;
}

// We want the following functions to be available in the header for inlining.
// We cannot define them inline in the class definition of `DoubleAPFloat`
// because doing so would instantiate `std::unique_ptr<APFloat[]>` before
// `APFloat` is defined, and that would be undefined behavior.
namespace detail {

inline DoubleAPFloat &DoubleAPFloat::operator=(DoubleAPFloat &&RHS) {
  if (this != &RHS) {
    this->~DoubleAPFloat();
    new (this) DoubleAPFloat(std::move(RHS));
  }
  return *this;
}

inline APFloat &DoubleAPFloat::getFirst() { return Floats[0]; }
inline const APFloat &DoubleAPFloat::getFirst() const { return Floats[0]; }
inline APFloat &DoubleAPFloat::getSecond() { return Floats[1]; }
inline const APFloat &DoubleAPFloat::getSecond() const { return Floats[1]; }

inline APFloat::opStatus
DoubleAPFloat::fusedMultiplyAdd(const DoubleAPFloat &Multiplicand,
                                const DoubleAPFloat &Addend,
                                APFloat::roundingMode RM) {
  assert(Semantics == &APFloatBase::PPCDoubleDouble() &&
         "Unexpected Semantics");
  const fltSemantics &LegacySem = *reinterpret_cast<const fltSemantics *>(
      &csupport_sem_ppc_double_double_legacy);
  APFloat Tmp(LegacySem, bitcastToAPInt());
  auto Ret =
      Tmp.fusedMultiplyAdd(APFloat(LegacySem, Multiplicand.bitcastToAPInt()),
                           APFloat(LegacySem, Addend.bitcastToAPInt()), RM);
  *this = DoubleAPFloat(APFloatBase::PPCDoubleDouble(), Tmp.bitcastToAPInt());
  return Ret;
}

inline APFloat::opStatus
DoubleAPFloat::roundToIntegral(APFloat::roundingMode RM) {
  assert(Semantics == &APFloatBase::PPCDoubleDouble() &&
         "Unexpected Semantics");
  const fltSemantics &LegacySem = *reinterpret_cast<const fltSemantics *>(
      &csupport_sem_ppc_double_double_legacy);
  APFloat Tmp(LegacySem, bitcastToAPInt());
  auto Ret = Tmp.roundToIntegral(RM);
  *this = DoubleAPFloat(APFloatBase::PPCDoubleDouble(), Tmp.bitcastToAPInt());
  return Ret;
}

inline void DoubleAPFloat::changeSign() {
  Floats[0].changeSign();
  Floats[1].changeSign();
}

inline APFloat::cmpResult
DoubleAPFloat::compareAbsoluteValue(const DoubleAPFloat &RHS) const {
  auto Result = Floats[0].compareAbsoluteValue(RHS.Floats[0]);
  if (Result != cmpEqual)
    return Result;
  Result = Floats[1].compareAbsoluteValue(RHS.Floats[1]);
  if (Result == cmpLessThan || Result == cmpGreaterThan) {
    auto Against = Floats[0].isNegative() ^ Floats[1].isNegative();
    auto RHSAgainst = RHS.Floats[0].isNegative() ^ RHS.Floats[1].isNegative();
    if (Against && !RHSAgainst)
      return cmpLessThan;
    if (!Against && RHSAgainst)
      return cmpGreaterThan;
    if (!Against && !RHSAgainst)
      return Result;
    if (Against && RHSAgainst)
      return (cmpResult)(cmpLessThan + cmpGreaterThan - Result);
  }
  return Result;
}

inline APFloat::fltCategory DoubleAPFloat::getCategory() const {
  return Floats[0].getCategory();
}

inline bool DoubleAPFloat::isNegative() const { return Floats[0].isNegative(); }

inline void DoubleAPFloat::makeInf(bool Neg) {
  Floats[0].makeInf(Neg);
  Floats[1].makeZero(false);
}

inline void DoubleAPFloat::makeZero(bool Neg) {
  Floats[0].makeZero(Neg);
  Floats[1].makeZero(false);
}

inline void DoubleAPFloat::makeLargest(bool Neg) {
  assert(Semantics == &APFloatBase::PPCDoubleDouble() &&
         "Unexpected Semantics");
  Floats[0] =
      APFloat(APFloatBase::IEEEdouble(), APInt(64, 0x7fefffffffffffffull));
  Floats[1] =
      APFloat(APFloatBase::IEEEdouble(), APInt(64, 0x7c8ffffffffffffeull));
  if (Neg)
    changeSign();
}

inline void DoubleAPFloat::makeSmallest(bool Neg) {
  assert(Semantics == &APFloatBase::PPCDoubleDouble() &&
         "Unexpected Semantics");
  Floats[0].makeSmallest(Neg);
  Floats[1].makeZero(false);
}

inline void DoubleAPFloat::makeSmallestNormalized(bool Neg) {
  assert(Semantics == &APFloatBase::PPCDoubleDouble() &&
         "Unexpected Semantics");
  Floats[0] =
      APFloat(APFloatBase::IEEEdouble(), APInt(64, 0x0360000000000000ull));
  if (Neg)
    Floats[0].changeSign();
  Floats[1].makeZero(false);
}

inline void DoubleAPFloat::makeNaN(bool SNaN, bool Neg, const APInt *fill) {
  Floats[0].makeNaN(SNaN, Neg, fill);
  Floats[1].makeZero(false);
}

inline APFloat::cmpResult
DoubleAPFloat::compare(const DoubleAPFloat &RHS) const {
  auto Result = Floats[0].compare(RHS.Floats[0]);
  if (Result == APFloat::cmpEqual)
    return Floats[1].compare(RHS.Floats[1]);
  return Result;
}

inline bool DoubleAPFloat::bitwiseIsEqual(const DoubleAPFloat &RHS) const {
  return Floats[0].bitwiseIsEqual(RHS.Floats[0]) &&
         Floats[1].bitwiseIsEqual(RHS.Floats[1]);
}

inline APInt DoubleAPFloat::bitcastToAPInt() const {
  assert(Semantics == &APFloatBase::PPCDoubleDouble() &&
         "Unexpected Semantics");
  uint64_t Data[] = {
      Floats[0].bitcastToAPInt().getRawData()[0],
      Floats[1].bitcastToAPInt().getRawData()[0],
  };
  return APInt(128, 2, Data);
}

// ---------------------------------------------------------------------------
// Inline IEEEFloat method definitions delegating to pure-C csupport functions.
// ---------------------------------------------------------------------------

// isSignificandAll*, multiplySignificand(simple), divideSignificand,
// addOrSubtractSignificand, roundAwayFromZero, getExactLog2Abs:
// moved to inline block above (after IEEEFloat class body)

inline lostFraction IEEEFloat::addOrSubtractSignificand(const IEEEFloat &rhs,
                                                        bool subtract) {
  assert(semantics == rhs.semantics);
  int s = sign ? 1 : 0;
  int rhs_s = rhs.sign ? 1 : 0;
  int lf = csupport_apfloat_add_or_subtract_significand(
      significandParts(), partCount(), &exponent, &s, rhs.significandParts(),
      rhs.exponent, rhs_s, subtract ? 1 : 0);
  sign = (unsigned)s;
  return (lostFraction)lf;
}

inline lostFraction IEEEFloat::multiplySignificand(const IEEEFloat &rhs) {
  exponent += rhs.exponent;
  int exp = exponent;
  int lf = csupport_apfloat_multiply_significand_simple(
      significandParts(), rhs.significandParts(), partCount(),
      semantics->precision, &exp);
  exponent = exp;
  return (lostFraction)lf;
}

inline lostFraction IEEEFloat::multiplySignificand(const IEEEFloat &rhs,
                                                   IEEEFloat addend) {
  int s = sign ? 1 : 0;
  int lf = csupport_apfloat_multiply_significand_fma(
      reinterpret_cast<const csupport_flt_semantics_t *>(semantics),
      significandParts(), partCount(), &exponent, &s, rhs.significandParts(),
      rhs.exponent, addend.significandParts(), addend.partCount(),
      addend.exponent, (int)addend.category, addend.sign ? 1 : 0,
      addend.isNonZero() ? 1 : 0);
  sign = (unsigned)s;
  return (lostFraction)lf;
}

inline lostFraction IEEEFloat::divideSignificand(const IEEEFloat &rhs) {
  assert(semantics == rhs.semantics);
  exponent -= rhs.exponent;
  int exp_val = exponent;
  lostFraction lf = (lostFraction)csupport_apfloat_divide_significand(
      significandParts(), rhs.significandParts(), partCount(),
      semantics->precision, &exp_val);
  exponent = exp_val;
  return lf;
}

inline IEEEFloat::opStatus IEEEFloat::convert(const fltSemantics &toSemantics_,
                                              roundingMode rounding_mode,
                                              bool *losesInfo) {
  int cat = (int)category;
  int sgn = sign ? 1 : 0;
  int loses = 0;
  int fs = csupport_apfloat_convert_semantics(
      reinterpret_cast<const csupport_flt_semantics_t *>(semantics),
      reinterpret_cast<const csupport_flt_semantics_t *>(&toSemantics_),
      semantics == &APFloatBase::x87DoubleExtended(),
      &toSemantics_ == &APFloatBase::x87DoubleExtended(), &significand.part,
      &significand.parts, partCount(), &exponent, &cat, &sgn,
      (int)rounding_mode, isSignaling() ? 1 : 0, isFiniteNonZero() ? 1 : 0,
      &loses);
  semantics = &toSemantics_;
  category = (fltCategory)cat;
  sign = (unsigned)sgn;
  *losesInfo = loses != 0;
  return (opStatus)fs;
}

// ---------------------------------------------------------------------------
// IEEEFloat integer conversion methods (inlined from cpp_bridge.cpp)
// ---------------------------------------------------------------------------

static_assert(APFloatBase::integerPartWidth % 4 == 0,
              "Part width must be divisible by 4!");

inline IEEEFloat::opStatus IEEEFloat::convertToSignExtendedInteger(
    MutableArrayRef<integerPart> parts, unsigned int width, bool isSigned,
    roundingMode rounding_mode, bool *isExact) const {
  unsigned dstPartsCount = csupport_flt_part_count_for_bits(width);
  assert(dstPartsCount <= parts.size() && "Integer too big");
  int exact = 0;
  int r = csupport_apfloat_convert_to_sign_ext_int(
      significandParts(), partCount(), semantics->precision, exponent,
      (int)category, sign, isSigned, (int)rounding_mode,
      (uint64_t *)(parts.data()), dstPartsCount, width, &exact);
  *isExact = exact;
  if (r < 0)
    return opInvalidOp;
  if (r == 0)
    return opOK;
  return opInexact;
}

inline IEEEFloat::opStatus
IEEEFloat::convertToInteger(MutableArrayRef<integerPart> parts,
                            unsigned int width, bool isSigned,
                            roundingMode rounding_mode, bool *isExact) const {
  opStatus fs;
  fs = convertToSignExtendedInteger(parts, width, isSigned, rounding_mode,
                                    isExact);
  if (fs == opInvalidOp) {
    unsigned int bits, dstPartsCount;
    dstPartsCount = csupport_flt_part_count_for_bits(width);
    assert(dstPartsCount <= parts.size() && "Integer too big");
    if (category == fcNaN)
      bits = 0;
    else if (sign)
      bits = isSigned;
    else
      bits = width - isSigned;
    csupport_apfloat_tc_set_least_significant_bits(parts.data(), dstPartsCount,
                                                   bits);
    if (sign && isSigned)
      APInt::tcShiftLeft(parts.data(), dstPartsCount, width - 1);
  }
  return fs;
}

inline IEEEFloat::opStatus IEEEFloat::convertFromUnsignedParts(
    const integerPart *src, unsigned int srcCount, roundingMode rounding_mode) {
  unsigned int omsb, precision, dstCount;
  integerPart *dst;
  lostFraction lost_fraction;
  category = fcNormal;
  omsb = APInt::tcMSB(src, srcCount) + 1;
  dst = significandParts();
  dstCount = partCount();
  precision = semantics->precision;
  if (precision <= omsb) {
    exponent = omsb - 1;
    lost_fraction = (lostFraction)csupport_apfloat_lost_fraction_truncation(
        src, srcCount, omsb - precision);
    APInt::tcExtract(dst, dstCount, src, precision, omsb - precision);
  } else {
    exponent = precision - 1;
    lost_fraction = lfExactlyZero;
    APInt::tcExtract(dst, dstCount, src, omsb, 0);
  }
  return normalize(rounding_mode, lost_fraction);
}

inline IEEEFloat::opStatus
IEEEFloat::convertFromAPInt(const APInt &Val, bool isSigned,
                            roundingMode rounding_mode) {
  unsigned int partCount = Val.getNumWords();
  APInt api = Val;
  sign = false;
  if (isSigned && api.isNegative()) {
    sign = true;
    api = -api;
  }
  return convertFromUnsignedParts(api.getRawData(), partCount, rounding_mode);
}

inline IEEEFloat::opStatus
IEEEFloat::convertFromSignExtendedInteger(const integerPart *src,
                                          unsigned int srcCount, bool isSigned,
                                          roundingMode rounding_mode) {
  opStatus status;
  if (isSigned && APInt::tcExtractBit(src, srcCount * integerPartWidth - 1)) {
    integerPart *copy;
    sign = true;
    copy = (integerPart *)malloc((srcCount) * sizeof(integerPart));
    APInt::tcAssign(copy, src, srcCount);
    APInt::tcNegate(copy, srcCount);
    status = convertFromUnsignedParts(copy, srcCount, rounding_mode);
    free(copy);
  } else {
    sign = false;
    status = convertFromUnsignedParts(src, srcCount, rounding_mode);
  }
  return status;
}

inline IEEEFloat::opStatus
IEEEFloat::convertFromZeroExtendedInteger(const integerPart *parts,
                                          unsigned int width, bool isSigned,
                                          roundingMode rounding_mode) {
  unsigned int partCount = csupport_flt_part_count_for_bits(width);
  APInt api = APInt(width, ArrayRef(parts, partCount));
  sign = false;
  if (isSigned && APInt::tcExtractBit(parts, width - 1)) {
    sign = true;
    api = -api;
  }
  return convertFromUnsignedParts(api.getRawData(), partCount, rounding_mode);
}

// ---------------------------------------------------------------------------
// DoubleAPFloat constructors and arithmetic (inlined from cpp_bridge.cpp)
// ---------------------------------------------------------------------------

inline DoubleAPFloat::DoubleAPFloat(const fltSemantics &S)
    : Semantics(&S),
      Floats(new APFloat[2]{APFloat(APFloatBase::IEEEdouble()),
                            APFloat(APFloatBase::IEEEdouble())}) {
  assert(Semantics == &APFloatBase::PPCDoubleDouble());
}

inline DoubleAPFloat::DoubleAPFloat(const fltSemantics &S, uninitializedTag)
    : Semantics(&S), Floats(new APFloat[2]{
                         APFloat(APFloatBase::IEEEdouble(), uninitialized),
                         APFloat(APFloatBase::IEEEdouble(), uninitialized)}) {
  assert(Semantics == &APFloatBase::PPCDoubleDouble());
}

inline DoubleAPFloat::DoubleAPFloat(const fltSemantics &S, integerPart I)
    : Semantics(&S),
      Floats(new APFloat[2]{APFloat(APFloatBase::IEEEdouble(), I),
                            APFloat(APFloatBase::IEEEdouble())}) {
  assert(Semantics == &APFloatBase::PPCDoubleDouble());
}

inline DoubleAPFloat::DoubleAPFloat(const fltSemantics &S, const APInt &I)
    : Semantics(&S),
      Floats(new APFloat[2]{
          APFloat(APFloatBase::IEEEdouble(), APInt(64, I.getRawData()[0])),
          APFloat(APFloatBase::IEEEdouble(), APInt(64, I.getRawData()[1]))}) {
  assert(Semantics == &APFloatBase::PPCDoubleDouble());
}

inline DoubleAPFloat::DoubleAPFloat(const fltSemantics &S, APFloat &&First,
                                    APFloat &&Second)
    : Semantics(&S),
      Floats(new APFloat[2]{std::move(First), std::move(Second)}) {
  assert(Semantics == &APFloatBase::PPCDoubleDouble());
  assert(&Floats[0].getSemantics() == &APFloatBase::IEEEdouble());
  assert(&Floats[1].getSemantics() == &APFloatBase::IEEEdouble());
}

inline DoubleAPFloat::DoubleAPFloat(const DoubleAPFloat &RHS)
    : Semantics(RHS.Semantics),
      Floats(RHS.Floats ? new APFloat[2]{APFloat(RHS.Floats[0]),
                                         APFloat(RHS.Floats[1])}
                        : nullptr) {
  assert(Semantics == &APFloatBase::PPCDoubleDouble());
}

inline DoubleAPFloat::DoubleAPFloat(DoubleAPFloat &&RHS)
    : Semantics(RHS.Semantics), Floats(std::move(RHS.Floats)) {
  RHS.Semantics = &APFloatBase::Bogus();
  assert(Semantics == &APFloatBase::PPCDoubleDouble());
}

inline DoubleAPFloat &DoubleAPFloat::operator=(const DoubleAPFloat &RHS) {
  if (Semantics == RHS.Semantics && RHS.Floats) {
    Floats[0] = RHS.Floats[0];
    Floats[1] = RHS.Floats[1];
  } else if (this != &RHS) {
    this->~DoubleAPFloat();
    new (this) DoubleAPFloat(RHS);
  }
  return *this;
}

inline APFloat::opStatus
DoubleAPFloat::addImpl(const APFloat &a, const APFloat &aa, const APFloat &c,
                       const APFloat &cc, roundingMode RM) {
  int Status = opOK;
  APFloat z = a;
  Status |= z.add(c, RM);
  if (!z.isFinite()) {
    if (!z.isInfinity()) {
      Floats[0] = z;
      Floats[1].makeZero(/* Neg = */ false);
      return (opStatus)Status;
    }
    Status = opOK;
    auto AComparedToC = a.compareAbsoluteValue(c);
    z = cc;
    Status |= z.add(aa, RM);
    if (AComparedToC == APFloat::cmpGreaterThan) {
      Status |= z.add(c, RM);
      Status |= z.add(a, RM);
    } else {
      Status |= z.add(a, RM);
      Status |= z.add(c, RM);
    }
    if (!z.isFinite()) {
      Floats[0] = z;
      Floats[1].makeZero(/* Neg = */ false);
      return (opStatus)Status;
    }
    Floats[0] = z;
    APFloat zz = aa;
    Status |= zz.add(cc, RM);
    if (AComparedToC == APFloat::cmpGreaterThan) {
      // Floats[1] = a - z + c + zz;
      Floats[1] = a;
      Status |= Floats[1].subtract(z, RM);
      Status |= Floats[1].add(c, RM);
      Status |= Floats[1].add(zz, RM);
    } else {
      // Floats[1] = c - z + a + zz;
      Floats[1] = c;
      Status |= Floats[1].subtract(z, RM);
      Status |= Floats[1].add(a, RM);
      Status |= Floats[1].add(zz, RM);
    }
  } else {
    // q = a - z;
    APFloat q = a;
    Status |= q.subtract(z, RM);

    // zz = q + c + (a - (q + z)) + aa + cc;
    // Compute a - (q + z) as -((q + z) - a) to avoid temporary copies.
    auto zz = q;
    Status |= zz.add(c, RM);
    Status |= q.add(z, RM);
    Status |= q.subtract(a, RM);
    q.changeSign();
    Status |= zz.add(q, RM);
    Status |= zz.add(aa, RM);
    Status |= zz.add(cc, RM);
    if (zz.isZero() && !zz.isNegative()) {
      Floats[0] = z;
      Floats[1].makeZero(/* Neg = */ false);
      return opOK;
    }
    Floats[0] = z;
    Status |= Floats[0].add(zz, RM);
    if (!Floats[0].isFinite()) {
      Floats[1].makeZero(/* Neg = */ false);
      return (opStatus)Status;
    }
    Floats[1] = z;
    Status |= Floats[1].subtract(Floats[0], RM);
    Status |= Floats[1].add(zz, RM);
  }
  return (opStatus)Status;
}

inline APFloat::opStatus DoubleAPFloat::addWithSpecial(const DoubleAPFloat &LHS,
                                                       const DoubleAPFloat &RHS,
                                                       DoubleAPFloat &Out,
                                                       roundingMode RM) {
  if (LHS.getCategory() == fcNaN) {
    Out = LHS;
    return opOK;
  }
  if (RHS.getCategory() == fcNaN) {
    Out = RHS;
    return opOK;
  }
  if (LHS.getCategory() == fcZero) {
    Out = RHS;
    return opOK;
  }
  if (RHS.getCategory() == fcZero) {
    Out = LHS;
    return opOK;
  }
  if (LHS.getCategory() == fcInfinity && RHS.getCategory() == fcInfinity &&
      LHS.isNegative() != RHS.isNegative()) {
    Out.makeNaN(false, Out.isNegative(), nullptr);
    return opInvalidOp;
  }
  if (LHS.getCategory() == fcInfinity) {
    Out = LHS;
    return opOK;
  }
  if (RHS.getCategory() == fcInfinity) {
    Out = RHS;
    return opOK;
  }
  assert(LHS.getCategory() == fcNormal && RHS.getCategory() == fcNormal);

  APFloat A(LHS.Floats[0]), AA(LHS.Floats[1]), C(RHS.Floats[0]),
      CC(RHS.Floats[1]);
  assert(&A.getSemantics() == &APFloatBase::IEEEdouble());
  assert(&AA.getSemantics() == &APFloatBase::IEEEdouble());
  assert(&C.getSemantics() == &APFloatBase::IEEEdouble());
  assert(&CC.getSemantics() == &APFloatBase::IEEEdouble());
  assert(&Out.Floats[0].getSemantics() == &APFloatBase::IEEEdouble());
  assert(&Out.Floats[1].getSemantics() == &APFloatBase::IEEEdouble());
  return Out.addImpl(A, AA, C, CC, RM);
}

inline APFloat::opStatus DoubleAPFloat::add(const DoubleAPFloat &RHS,
                                            roundingMode RM) {
  return addWithSpecial(*this, RHS, *this, RM);
}

inline APFloat::opStatus DoubleAPFloat::subtract(const DoubleAPFloat &RHS,
                                                 roundingMode RM) {
  changeSign();
  auto Ret = add(RHS, RM);
  changeSign();
  return Ret;
}

inline APFloat::opStatus DoubleAPFloat::multiply(const DoubleAPFloat &RHS,
                                                 APFloat::roundingMode RM) {
  const auto &LHS = *this;
  auto &Out = *this;
  if (LHS.getCategory() == fcNaN) {
    Out = LHS;
    return opOK;
  }
  if (RHS.getCategory() == fcNaN) {
    Out = RHS;
    return opOK;
  }
  if ((LHS.getCategory() == fcZero && RHS.getCategory() == fcInfinity) ||
      (LHS.getCategory() == fcInfinity && RHS.getCategory() == fcZero)) {
    Out.makeNaN(false, false, nullptr);
    return opOK;
  }
  if (LHS.getCategory() == fcZero || LHS.getCategory() == fcInfinity) {
    Out = LHS;
    return opOK;
  }
  if (RHS.getCategory() == fcZero || RHS.getCategory() == fcInfinity) {
    Out = RHS;
    return opOK;
  }
  assert(LHS.getCategory() == fcNormal && RHS.getCategory() == fcNormal &&
         "Special cases not handled exhaustively");

  int Status = opOK;
  APFloat A = Floats[0], B = Floats[1], C = RHS.Floats[0], D = RHS.Floats[1];
  // t = a * c
  APFloat T = A;
  Status |= T.multiply(C, RM);
  if (!T.isFiniteNonZero()) {
    Floats[0] = T;
    Floats[1].makeZero(/* Neg = */ false);
    return (opStatus)Status;
  }

  // tau = fmsub(a, c, t), that is -fmadd(-a, c, t).
  APFloat Tau = A;
  T.changeSign();
  Status |= Tau.fusedMultiplyAdd(C, T, RM);
  T.changeSign();
  {
    // v = a * d
    APFloat V = A;
    Status |= V.multiply(D, RM);
    // w = b * c
    APFloat W = B;
    Status |= W.multiply(C, RM);
    Status |= V.add(W, RM);
    // tau += v + w
    Status |= Tau.add(V, RM);
  }
  // u = t + tau
  APFloat U = T;
  Status |= U.add(Tau, RM);

  Floats[0] = U;
  if (!U.isFinite()) {
    Floats[1].makeZero(/* Neg = */ false);
  } else {
    // Floats[1] = (t - u) + tau
    Status |= T.subtract(U, RM);
    Status |= T.add(Tau, RM);
    Floats[1] = T;
  }
  return (opStatus)Status;
}

inline APFloat::opStatus DoubleAPFloat::divide(const DoubleAPFloat &RHS,
                                               APFloat::roundingMode RM) {
  assert(Semantics == &APFloatBase::PPCDoubleDouble() &&
         "Unexpected Semantics");
  const fltSemantics &LegacySem = *reinterpret_cast<const fltSemantics *>(
      &csupport_sem_ppc_double_double_legacy);
  APFloat Tmp(LegacySem, bitcastToAPInt());
  auto Ret = Tmp.divide(APFloat(LegacySem, RHS.bitcastToAPInt()), RM);
  *this = DoubleAPFloat(APFloatBase::PPCDoubleDouble(), Tmp.bitcastToAPInt());
  return Ret;
}

inline APFloat::opStatus DoubleAPFloat::remainder(const DoubleAPFloat &RHS) {
  assert(Semantics == &APFloatBase::PPCDoubleDouble() &&
         "Unexpected Semantics");
  const fltSemantics &LegacySem = *reinterpret_cast<const fltSemantics *>(
      &csupport_sem_ppc_double_double_legacy);
  APFloat Tmp(LegacySem, bitcastToAPInt());
  auto Ret = Tmp.remainder(APFloat(LegacySem, RHS.bitcastToAPInt()));
  *this = DoubleAPFloat(APFloatBase::PPCDoubleDouble(), Tmp.bitcastToAPInt());
  return Ret;
}

inline APFloat::opStatus DoubleAPFloat::mod(const DoubleAPFloat &RHS) {
  assert(Semantics == &APFloatBase::PPCDoubleDouble() &&
         "Unexpected Semantics");
  const fltSemantics &LegacySem = *reinterpret_cast<const fltSemantics *>(
      &csupport_sem_ppc_double_double_legacy);
  APFloat Tmp(LegacySem, bitcastToAPInt());
  auto Ret = Tmp.mod(APFloat(LegacySem, RHS.bitcastToAPInt()));
  *this = DoubleAPFloat(APFloatBase::PPCDoubleDouble(), Tmp.bitcastToAPInt());
  return Ret;
}

inline int DoubleAPFloat::getExactLog2() const { return INT_MIN; }

inline int DoubleAPFloat::getExactLog2Abs() const { return INT_MIN; }

} // namespace detail

// ---------------------------------------------------------------------------
// APFloat methods (inlined from cpp_bridge.cpp)
// ---------------------------------------------------------------------------

inline APFloat::Storage::Storage(IEEEFloat F, const fltSemantics &Semantics) {
  if (usesLayout<IEEEFloat>(Semantics)) {
    new (&IEEE) IEEEFloat(std::move(F));
    return;
  }
  if (usesLayout<DoubleAPFloat>(Semantics)) {
    const fltSemantics &S = F.getSemantics();
    new (&Double) DoubleAPFloat(Semantics, APFloat(std::move(F), S),
                                APFloat(APFloatBase::IEEEdouble()));
    return;
  }
  llvm_unreachable("Unexpected semantics");
}

inline APFloat::APFloat(const fltSemantics &Semantics, StringRef S)
    : APFloat(Semantics) {
  auto StatusOrErr = convertFromString(S, rmNearestTiesToEven);
  assert(StatusOrErr && "Invalid floating point representation");
  consumeError(StatusOrErr.takeError());
}

inline FPClassTest APFloat::classify() const {
  if (isZero())
    return isNegative() ? fcNegZero : fcPosZero;
  if (isNormal())
    return isNegative() ? fcNegNormal : fcPosNormal;
  if (isDenormal())
    return isNegative() ? fcNegSubnormal : fcPosSubnormal;
  if (isInfinity())
    return isNegative() ? fcNegInf : fcPosInf;
  assert(isNaN() && "Other class of FP constant");
  return isSignaling() ? fcSNan : fcQNan;
}

inline APFloat::opStatus APFloat::convert(const fltSemantics &ToSemantics,
                                          roundingMode RM, bool *losesInfo) {
  if (&getSemantics() == &ToSemantics) {
    *losesInfo = false;
    return opOK;
  }
  if (usesLayout<IEEEFloat>(getSemantics()) &&
      usesLayout<IEEEFloat>(ToSemantics))
    return U.IEEE.convert(ToSemantics, RM, losesInfo);
  if (usesLayout<IEEEFloat>(getSemantics()) &&
      usesLayout<DoubleAPFloat>(ToSemantics)) {
    assert(&ToSemantics == &PPCDoubleDouble());
    const fltSemantics &LegacySem = *reinterpret_cast<const fltSemantics *>(
        &csupport_sem_ppc_double_double_legacy);
    auto Ret = U.IEEE.convert(LegacySem, RM, losesInfo);
    *this = APFloat(ToSemantics, U.IEEE.bitcastToAPInt());
    return Ret;
  }
  if (usesLayout<DoubleAPFloat>(getSemantics()) &&
      usesLayout<IEEEFloat>(ToSemantics)) {
    auto Ret = getIEEE().convert(ToSemantics, RM, losesInfo);
    *this = APFloat(std::move(getIEEE()), ToSemantics);
    return Ret;
  }
  llvm_unreachable("Unexpected semantics");
}

inline APFloat APFloat::getAllOnesValue(const fltSemantics &Semantics) {
  return APFloat(Semantics, APInt::getAllOnes(Semantics.sizeInBits));
}

inline void APFloat::print(raw_ostream &OS) const {
  SmallVector<char, 16> Buffer;
  toString(Buffer);
  OS << Buffer << "\n";
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD inline void APFloat::dump() const { print(dbgs()); }
#endif

inline void APFloat::Profile(FoldingSetNodeID &NID) const {
  NID.Add(bitcastToAPInt());
}

inline APFloat::opStatus APFloat::convertToInteger(APSInt &result,
                                                   roundingMode rounding_mode,
                                                   bool *isExact) const {
  unsigned bitWidth = result.getBitWidth();
  SmallVector<uint64_t, 4> parts(result.getNumWords());
  opStatus status = convertToInteger(parts, bitWidth, result.isSigned(),
                                     rounding_mode, isExact);
  result = APInt(bitWidth, parts);
  return status;
}

inline double APFloat::convertToDouble() const {
  if (&getSemantics() == (const llvm::fltSemantics *)&APFloatBase::IEEEdouble())
    return getIEEE().convertToDouble();
  assert(getSemantics().isRepresentableBy(APFloatBase::IEEEdouble()) &&
         "Float semantics is not representable by IEEEdouble");
  APFloat Temp = *this;
  bool LosesInfo;
  opStatus St =
      Temp.convert(APFloatBase::IEEEdouble(), rmNearestTiesToEven, &LosesInfo);
  assert(!(St & opInexact) && !LosesInfo && "Unexpected imprecision");
  (void)St;
  return Temp.getIEEE().convertToDouble();
}

inline float APFloat::convertToFloat() const {
  if (&getSemantics() == (const llvm::fltSemantics *)&APFloatBase::IEEEsingle())
    return getIEEE().convertToFloat();
  assert(getSemantics().isRepresentableBy(APFloatBase::IEEEsingle()) &&
         "Float semantics is not representable by IEEEsingle");
  APFloat Temp = *this;
  bool LosesInfo;
  opStatus St =
      Temp.convert(APFloatBase::IEEEsingle(), rmNearestTiesToEven, &LosesInfo);
  assert(!(St & opInexact) && !LosesInfo && "Unexpected imprecision");
  (void)St;
  return Temp.getIEEE().convertToFloat();
}

inline bool StringRef::getAsDouble(double &Result, bool AllowInexact) const {
  APFloat F(0.0);
  auto StatusOrErr = F.convertFromString(*this, APFloat::rmNearestTiesToEven);
  if (errorToBool(StatusOrErr.takeError()))
    return true;

  APFloat::opStatus Status = *StatusOrErr;
  if (Status != APFloat::opOK) {
    if (!AllowInexact || !(Status & APFloat::opInexact))
      return true;
  }

  Result = F.convertToDouble();
  return false;
}

} // namespace llvm

#undef APFLOAT_DISPATCH_ON_SEMANTICS
#endif // LLVM_ADT_APFLOAT_H
