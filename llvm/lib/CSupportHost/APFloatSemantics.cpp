//===- APFloatSemantics.cpp - Canonical APFloat semantics -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// Owns the C++ fltSemantics objects whose addresses identify APFloat layouts.
/// Keeping one definition in LLVMSupport preserves that identity across
/// translation units and shared-library/plugin boundaries without treating
/// the differently typed C representation as a C++ object.
///
//===----------------------------------------------------------------------===//

#include "llvm/ADT/APFloat.h"

namespace llvm {
namespace {

constexpr fltSemantics IEEEHalfSemantics = {15, -14, 11, 16};
constexpr fltSemantics BFloatSemantics = {127, -126, 8, 16};
constexpr fltSemantics IEEESingleSemantics = {127, -126, 24, 32};
constexpr fltSemantics IEEEDoubleSemantics = {1023, -1022, 53, 64};
constexpr fltSemantics IEEEQuadSemantics = {16383, -16382, 113, 128};
constexpr fltSemantics PPCDoubleDoubleSemantics = {-1, 0, 0, 128};
constexpr fltSemantics Float8E5M2Semantics = {15, -14, 3, 8};
constexpr fltSemantics Float8E5M2FNUZSemantics = {
    15, -15, 3, 8, fltNonfiniteBehavior::NanOnly, fltNanEncoding::NegativeZero};
constexpr fltSemantics Float8E4M3FNSemantics = {
    8, -6, 4, 8, fltNonfiniteBehavior::NanOnly, fltNanEncoding::AllOnes};
constexpr fltSemantics Float8E4M3FNUZSemantics = {
    7, -7, 4, 8, fltNonfiniteBehavior::NanOnly, fltNanEncoding::NegativeZero};
constexpr fltSemantics Float8E4M3B11FNUZSemantics = {
    4, -10, 4, 8, fltNonfiniteBehavior::NanOnly, fltNanEncoding::NegativeZero};
constexpr fltSemantics FloatTF32Semantics = {127, -126, 11, 19};
constexpr fltSemantics X87DoubleExtendedSemantics = {16383, -16382, 64, 80};
constexpr fltSemantics BogusSemantics = {0, 0, 0, 0};
constexpr fltSemantics PPCDoubleDoubleLegacySemantics = {1023, -1022 + 53,
                                                         53 + 53, 128};

} // namespace

const fltSemantics &APFloatBase::IEEEhalf() { return IEEEHalfSemantics; }
const fltSemantics &APFloatBase::BFloat() { return BFloatSemantics; }
const fltSemantics &APFloatBase::IEEEsingle() { return IEEESingleSemantics; }
const fltSemantics &APFloatBase::IEEEdouble() { return IEEEDoubleSemantics; }
const fltSemantics &APFloatBase::IEEEquad() { return IEEEQuadSemantics; }
const fltSemantics &APFloatBase::PPCDoubleDouble() {
  return PPCDoubleDoubleSemantics;
}
const fltSemantics &APFloatBase::Float8E5M2() { return Float8E5M2Semantics; }
const fltSemantics &APFloatBase::Float8E5M2FNUZ() {
  return Float8E5M2FNUZSemantics;
}
const fltSemantics &APFloatBase::Float8E4M3FN() {
  return Float8E4M3FNSemantics;
}
const fltSemantics &APFloatBase::Float8E4M3FNUZ() {
  return Float8E4M3FNUZSemantics;
}
const fltSemantics &APFloatBase::Float8E4M3B11FNUZ() {
  return Float8E4M3B11FNUZSemantics;
}
const fltSemantics &APFloatBase::FloatTF32() { return FloatTF32Semantics; }
const fltSemantics &APFloatBase::x87DoubleExtended() {
  return X87DoubleExtendedSemantics;
}
const fltSemantics &APFloatBase::Bogus() { return BogusSemantics; }

namespace detail {

const fltSemantics &ppcDoubleDoubleLegacySemantics() {
  return PPCDoubleDoubleLegacySemantics;
}

} // namespace detail
} // namespace llvm
