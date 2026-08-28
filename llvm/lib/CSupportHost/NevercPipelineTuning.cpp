//===- NevercPipelineTuning.cpp - NeverC option capture -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/NevercPipelineTuning.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

#define LLVM_NEVERC_PIPELINE_TUNING_OPTION(Type, Field, Option, Default,       \
                                           Spelling, Description)              \
  static cl::opt<Type> Option(Spelling, cl::init(Default), cl::Hidden,         \
                              cl::desc(Description));
#include "llvm/Support/NevercPipelineTuning.def"
#undef LLVM_NEVERC_PIPELINE_TUNING_OPTION

void llvm::detail::anchorNevercPipelineTuningOptions() {
  // The strong reference from cl::initCommonOptions is the behavior: it makes
  // a static linker extract this archive member, whose cl::opt constructors
  // register the options before main.  Keep the hook value-free so parsing
  // does not turn command-line globals into LLVMContext defaults.
}

NevercPipelineTuningOptions llvm::captureNevercPipelineTuningOptions() {
  NevercPipelineTuningOptions Captured;
#define LLVM_NEVERC_PIPELINE_TUNING_OPTION(Type, Field, Option, Default,       \
                                           Spelling, Description)              \
  Captured.Field = Option;
#include "llvm/Support/NevercPipelineTuning.def"
#undef LLVM_NEVERC_PIPELINE_TUNING_OPTION
  return Captured;
}

NevercPipelineTuningOptions llvm::overlayOccurredNevercPipelineTuningOptions(
    NevercPipelineTuningOptions Base) {
#define LLVM_NEVERC_PIPELINE_TUNING_OPTION(Type, Field, Option, Default,       \
                                           Spelling, Description)              \
  if (Option.getNumOccurrences() != 0)                                         \
    Base.Field = Option;
#include "llvm/Support/NevercPipelineTuning.def"
#undef LLVM_NEVERC_PIPELINE_TUNING_OPTION
  return Base;
}
