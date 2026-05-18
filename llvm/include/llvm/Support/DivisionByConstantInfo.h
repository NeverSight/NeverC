//===- llvm/Support/DivisionByConstantInfo.h ---------------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// This file implements support for optimizing divisions by a constant
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_DIVISIONBYCONSTANTINFO_H
#define LLVM_SUPPORT_DIVISIONBYCONSTANTINFO_H

#include "llvm/ADT/APInt.h"

namespace llvm {

/// Magic data for optimising signed division by a constant.
struct SignedDivisionByConstantInfo {
  static SignedDivisionByConstantInfo get(const APInt &D);
  APInt Magic;          ///< magic number
  unsigned ShiftAmount; ///< shift amount
};

/// Magic data for optimising unsigned division by a constant.
struct UnsignedDivisionByConstantInfo {
  static UnsignedDivisionByConstantInfo
  get(const APInt &D, unsigned LeadingZeros = 0,
      bool AllowEvenDivisorOptimization = true);
  APInt Magic;        ///< magic number
  bool IsAdd;         ///< add indicator
  unsigned PostShift; ///< post-shift amount
  unsigned PreShift;  ///< pre-shift amount
};

inline SignedDivisionByConstantInfo
SignedDivisionByConstantInfo::get(const APInt &D) {
  assert(!D.isZero() && "Precondition violation.");
  assert(D.getBitWidth() >= 3 && "Does not work at smaller bitwidths.");
  APInt Delta;
  APInt SignedMin = APInt::getSignedMinValue(D.getBitWidth());
  struct SignedDivisionByConstantInfo Retval;
  APInt AD = D.abs();
  APInt T = SignedMin + (D.lshr(D.getBitWidth() - 1));
  APInt ANC = T - 1 - T.urem(AD);
  unsigned P = D.getBitWidth() - 1;
  APInt Q1, R1, Q2, R2;
  APInt::udivrem(SignedMin, ANC, Q1, R1);
  APInt::udivrem(SignedMin, AD, Q2, R2);
  do {
    P = P + 1;
    Q1 <<= 1;
    R1 <<= 1;
    if (R1.uge(ANC)) {
      ++Q1;
      R1 -= ANC;
    }
    Q2 <<= 1;
    R2 <<= 1;
    if (R2.uge(AD)) {
      ++Q2;
      R2 -= AD;
    }
    Delta = AD;
    Delta -= R2;
  } while (Q1.ult(Delta) || (Q1 == Delta && R1.isZero()));
  Retval.Magic = Q2;
  ++Retval.Magic;
  if (D.isNegative())
    Retval.Magic.negate();
  Retval.ShiftAmount = P - D.getBitWidth();
  return Retval;
}

inline UnsignedDivisionByConstantInfo
UnsignedDivisionByConstantInfo::get(const APInt &D, unsigned LeadingZeros,
                                    bool AllowEvenDivisorOptimization) {
  assert(!D.isZero() && !D.isOne() && "Precondition violation.");
  assert(D.getBitWidth() > 1 && "Does not work at smaller bitwidths.");
  APInt Delta;
  struct UnsignedDivisionByConstantInfo Retval;
  Retval.IsAdd = false;
  APInt AllOnes = APInt::getAllOnes(D.getBitWidth()).lshr(LeadingZeros);
  APInt SignedMin = APInt::getSignedMinValue(D.getBitWidth());
  APInt SignedMax = APInt::getSignedMaxValue(D.getBitWidth());
  APInt NC = AllOnes - (AllOnes + 1 - D).urem(D);
  assert(NC.urem(D) == D - 1 && "Unexpected NC value");
  unsigned P = D.getBitWidth() - 1;
  APInt Q1, R1, Q2, R2;
  APInt::udivrem(SignedMin, NC, Q1, R1);
  APInt::udivrem(SignedMax, D, Q2, R2);
  do {
    P = P + 1;
    if (R1.uge(NC - R1)) {
      Q1 <<= 1;
      ++Q1;
      R1 <<= 1;
      R1 -= NC;
    } else {
      Q1 <<= 1;
      R1 <<= 1;
    }
    if ((R2 + 1).uge(D - R2)) {
      if (Q2.uge(SignedMax))
        Retval.IsAdd = true;
      Q2 <<= 1;
      ++Q2;
      R2 <<= 1;
      ++R2;
      R2 -= D;
    } else {
      if (Q2.uge(SignedMin))
        Retval.IsAdd = true;
      Q2 <<= 1;
      R2 <<= 1;
      ++R2;
    }
    Delta = D;
    --Delta;
    Delta -= R2;
  } while (P < D.getBitWidth() * 2 &&
           (Q1.ult(Delta) || (Q1 == Delta && R1.isZero())));
  if (Retval.IsAdd && !D[0] && AllowEvenDivisorOptimization) {
    unsigned PreShift = D.countr_zero();
    APInt ShiftedD = D.lshr(PreShift);
    Retval =
        UnsignedDivisionByConstantInfo::get(ShiftedD, LeadingZeros + PreShift);
    assert(Retval.IsAdd == 0 && Retval.PreShift == 0);
    Retval.PreShift = PreShift;
    return Retval;
  }
  Retval.Magic = Q2;
  ++Retval.Magic;
  Retval.PostShift = P - D.getBitWidth();
  if (Retval.IsAdd) {
    assert(Retval.PostShift > 0 && "Unexpected shift");
    Retval.PostShift -= 1;
  }
  Retval.PreShift = 0;
  return Retval;
}

} // namespace llvm

#endif
