//===- BranchProbability.h - Branch Probability Wrapper ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Definition of BranchProbability shared by IR and Machine Instructions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_BRANCHPROBABILITY_H
#define LLVM_SUPPORT_BRANCHPROBABILITY_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <assert.h>
#include <cinttypes>
#include <math.h>

extern "C" {
uint32_t csupport_branch_prob_normalize(uint32_t num, uint32_t denom,
                                        uint32_t target_denom);
uint32_t csupport_branch_prob_get64(uint64_t num, uint64_t denom,
                                    uint32_t target_denom);
uint64_t csupport_branch_prob_scale(uint64_t num, uint32_t n, uint32_t d);
uint64_t csupport_branch_prob_scale_inverse(uint64_t num, uint32_t n,
                                            uint32_t d);
}

namespace llvm {

// This class represents Branch Probability as a non-negative fraction that is
// no greater than 1. It uses a fixed-point-like implementation, in which the
// denominator is always a constant value (here we use 1<<31 for maximum
// precision).
class BranchProbability {
  // Numerator
  uint32_t N;

  // Denominator, which is a constant value.
  static constexpr uint32_t D = 1u << 31;
  static constexpr uint32_t UnknownN = UINT32_MAX;

  // Construct a BranchProbability with only numerator assuming the denominator
  // is 1<<31. For internal use only.
  explicit BranchProbability(uint32_t n) : N(n) {}

public:
  BranchProbability() : N(UnknownN) {}
  BranchProbability(uint32_t Numerator, uint32_t Denominator) {
    assert(Denominator > 0 && "Denominator cannot be 0!");
    assert(Numerator <= Denominator && "Probability cannot be bigger than 1!");
    N = csupport_branch_prob_normalize(Numerator, Denominator, D);
  }

  bool isZero() const { return N == 0; }
  bool isUnknown() const { return N == UnknownN; }

  static BranchProbability getZero() { return BranchProbability(0); }
  static BranchProbability getOne() { return BranchProbability(D); }
  static BranchProbability getUnknown() { return BranchProbability(UnknownN); }
  // Create a BranchProbability object with the given numerator and 1<<31
  // as denominator.
  static BranchProbability getRaw(uint32_t N) { return BranchProbability(N); }
  // Create a BranchProbability object from 64-bit integers.
  static BranchProbability getBranchProbability(uint64_t Numerator,
                                                uint64_t Denominator) {
    assert(Numerator <= Denominator && "Probability cannot be bigger than 1!");
    return BranchProbability(
        csupport_branch_prob_get64(Numerator, Denominator, D), D);
  }

  // Normalize given probabilties so that the sum of them becomes approximate
  // one.
  template <class ProbabilityIter>
  static void normalizeProbabilities(ProbabilityIter Begin,
                                     ProbabilityIter End);

  uint32_t getNumerator() const { return N; }
  static uint32_t getDenominator() { return D; }

  // Return (1 - Probability).
  BranchProbability getCompl() const { return BranchProbability(D - N); }

  inline raw_ostream &print(raw_ostream &OS) const {
    if (isUnknown())
      return OS << "?%";
    double Percent = rint(((double)N / D) * 100.0 * 100.0) / 100.0;
    return OS << format("0x%08" PRIx32 " / 0x%08" PRIx32 " = %.2f%%", N, D,
                        Percent);
  }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  LLVM_DUMP_METHOD inline void dump() const { print(dbgs()) << '\n'; }
#else
  void dump() const;
#endif

  /// Scale a large integer.  Guarantees full precision.  Returns the floor.
  uint64_t scale(uint64_t Num) const {
    return csupport_branch_prob_scale(Num, N, D);
  }

  /// Scale a large integer by the inverse.  Guarantees full precision.
  uint64_t scaleByInverse(uint64_t Num) const {
    return csupport_branch_prob_scale_inverse(Num, N, D);
  }

  BranchProbability &operator+=(BranchProbability RHS) {
    assert(N != UnknownN && RHS.N != UnknownN &&
           "Unknown probability cannot participate in arithmetics.");
    // Saturate the result in case of overflow.
    N = (uint64_t(N) + RHS.N > D) ? D : N + RHS.N;
    return *this;
  }

  BranchProbability &operator-=(BranchProbability RHS) {
    assert(N != UnknownN && RHS.N != UnknownN &&
           "Unknown probability cannot participate in arithmetics.");
    // Saturate the result in case of underflow.
    N = N < RHS.N ? 0 : N - RHS.N;
    return *this;
  }

  BranchProbability &operator*=(BranchProbability RHS) {
    assert(N != UnknownN && RHS.N != UnknownN &&
           "Unknown probability cannot participate in arithmetics.");
    N = (static_cast<uint64_t>(N) * RHS.N + D / 2) / D;
    return *this;
  }

  BranchProbability &operator*=(uint32_t RHS) {
    assert(N != UnknownN &&
           "Unknown probability cannot participate in arithmetics.");
    N = (uint64_t(N) * RHS > D) ? D : N * RHS;
    return *this;
  }

  BranchProbability &operator/=(BranchProbability RHS) {
    assert(N != UnknownN && RHS.N != UnknownN &&
           "Unknown probability cannot participate in arithmetics.");
    N = (static_cast<uint64_t>(N) * D + RHS.N / 2) / RHS.N;
    return *this;
  }

  BranchProbability &operator/=(uint32_t RHS) {
    assert(N != UnknownN &&
           "Unknown probability cannot participate in arithmetics.");
    assert(RHS > 0 && "The divider cannot be zero.");
    N /= RHS;
    return *this;
  }

  BranchProbability operator+(BranchProbability RHS) const {
    BranchProbability Prob(*this);
    Prob += RHS;
    return Prob;
  }

  BranchProbability operator-(BranchProbability RHS) const {
    BranchProbability Prob(*this);
    Prob -= RHS;
    return Prob;
  }

  BranchProbability operator*(BranchProbability RHS) const {
    BranchProbability Prob(*this);
    Prob *= RHS;
    return Prob;
  }

  BranchProbability operator*(uint32_t RHS) const {
    BranchProbability Prob(*this);
    Prob *= RHS;
    return Prob;
  }

  BranchProbability operator/(BranchProbability RHS) const {
    BranchProbability Prob(*this);
    Prob /= RHS;
    return Prob;
  }

  BranchProbability operator/(uint32_t RHS) const {
    BranchProbability Prob(*this);
    Prob /= RHS;
    return Prob;
  }

  bool operator==(BranchProbability RHS) const { return N == RHS.N; }
  bool operator!=(BranchProbability RHS) const { return !(*this == RHS); }

  bool operator<(BranchProbability RHS) const {
    assert(N != UnknownN && RHS.N != UnknownN &&
           "Unknown probability cannot participate in comparisons.");
    return N < RHS.N;
  }

  bool operator>(BranchProbability RHS) const {
    assert(N != UnknownN && RHS.N != UnknownN &&
           "Unknown probability cannot participate in comparisons.");
    return RHS < *this;
  }

  bool operator<=(BranchProbability RHS) const {
    assert(N != UnknownN && RHS.N != UnknownN &&
           "Unknown probability cannot participate in comparisons.");
    return !(RHS < *this);
  }

  bool operator>=(BranchProbability RHS) const {
    assert(N != UnknownN && RHS.N != UnknownN &&
           "Unknown probability cannot participate in comparisons.");
    return !(*this < RHS);
  }
};

inline raw_ostream &operator<<(raw_ostream &OS, BranchProbability Prob) {
  return Prob.print(OS);
}

template <class ProbabilityIter>
void BranchProbability::normalizeProbabilities(ProbabilityIter Begin,
                                               ProbabilityIter End) {
  if (Begin == End)
    return;

  unsigned UnknownProbCount = 0;
  uint64_t Sum = 0;
  for (auto I = Begin; I != End; ++I) {
    if (!I->isUnknown())
      Sum += I->N;
    else
      UnknownProbCount++;
  }

  if (UnknownProbCount > 0) {
    BranchProbability ProbForUnknown = BranchProbability::getZero();
    if (Sum < BranchProbability::getDenominator())
      ProbForUnknown = BranchProbability::getRaw(
          (BranchProbability::getDenominator() - Sum) / UnknownProbCount);

    for (auto I = Begin; I != End; ++I)
      if (I->isUnknown())
        *I = ProbForUnknown;

    if (Sum <= BranchProbability::getDenominator())
      return;
  }

  if (Sum == 0) {
    unsigned Count = 0;
    for (auto I = Begin; I != End; ++I)
      ++Count;
    BranchProbability BP(1, Count);
    for (auto I = Begin; I != End; ++I)
      *I = BP;
    return;
  }

  for (auto I = Begin; I != End; ++I)
    I->N = (I->N * uint64_t(D) + Sum / 2) / Sum;
}

} // namespace llvm

#endif
