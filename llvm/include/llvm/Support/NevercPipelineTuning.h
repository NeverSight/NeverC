//===- NevercPipelineTuning.h - Request-local NeverC policy -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_NEVERCPIPELINETUNING_H
#define LLVM_SUPPORT_NEVERCPIPELINETUNING_H

namespace llvm {

namespace NevercPipelineTuningDefaults {
#define LLVM_NEVERC_PIPELINE_TUNING_OPTION(Type, Field, Option, Default,       \
                                           Spelling, Description)              \
  inline constexpr Type Field = Default;
#include "llvm/Support/NevercPipelineTuning.def"
#undef LLVM_NEVERC_PIPELINE_TUNING_OPTION
} // namespace NevercPipelineTuningDefaults

namespace NevercPipelineTuningOptionSpelling {
#define LLVM_NEVERC_PIPELINE_TUNING_OPTION(Type, Field, Option, Default,       \
                                           Spelling, Description)              \
  inline constexpr char Field[] = Spelling;
#include "llvm/Support/NevercPipelineTuning.def"
#undef LLVM_NEVERC_PIPELINE_TUNING_OPTION
} // namespace NevercPipelineTuningOptionSpelling

/// NeverC-owned policy that affects the shape of default auto-LTO pipelines.
/// It contains values only: no callbacks, pointers, or process-global state.
struct NevercPipelineTuningOptions {
#define LLVM_NEVERC_PIPELINE_TUNING_OPTION(Type, Field, Option, Default,       \
                                           Spelling, Description)              \
  Type Field = NevercPipelineTuningDefaults::Field;
#include "llvm/Support/NevercPipelineTuning.def"
#undef LLVM_NEVERC_PIPELINE_TUNING_OPTION
};

namespace detail {
/// Keep the translation unit that owns NeverC's hidden command-line options in
/// statically linked opt-like clients.  CommandLine's common-option
/// initialization calls this before parsing; it does not read or publish any
/// option value.
void anchorNevercPipelineTuningOptions();
} // namespace detail

NevercPipelineTuningOptions captureNevercPipelineTuningOptions();

NevercPipelineTuningOptions
overlayOccurredNevercPipelineTuningOptions(NevercPipelineTuningOptions Base);

} // namespace llvm

#endif // LLVM_SUPPORT_NEVERCPIPELINETUNING_H
