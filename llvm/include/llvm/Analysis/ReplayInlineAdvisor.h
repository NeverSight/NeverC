//===- ReplayInlineAdvisor.h - Replay Inline Advisor interface -*- C++ --*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Minimal stub: replay inlining support has been removed in NeverC. Only the
// data structures required by the InlineAdvisor API are kept so downstream
// headers can still build.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_ANALYSIS_REPLAYINLINEADVISOR_H
#define LLVM_ANALYSIS_REPLAYINLINEADVISOR_H

#include "llvm/ADT/StringRef.h"

namespace llvm {

struct CallSiteFormat {
  enum class Format : int {
    Line,
    LineColumn,
    LineDiscriminator,
    LineColumnDiscriminator
  };
  Format OutputFormat = Format::LineColumnDiscriminator;
};

/// Replay Inliner Setup (stub; replay is not supported).
struct ReplayInlinerSettings {
  enum class Scope : int { Function, Module };
  enum class Fallback : int { Original, AlwaysInline, NeverInline };

  StringRef ReplayFile;
  Scope ReplayScope = Scope::Function;
  Fallback ReplayFallback = Fallback::Original;
  CallSiteFormat ReplayFormat;
};

} // namespace llvm
#endif // LLVM_ANALYSIS_REPLAYINLINEADVISOR_H
