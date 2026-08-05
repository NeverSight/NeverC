//===--- ArgumentHandlers.h - Early driver argument handling ----*- C++ -*-===//
//
// Part of the NeverC Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef NEVERC_DRIVER_ARGUMENTHANDLERS_H
#define NEVERC_DRIVER_ARGUMENTHANDLERS_H

#include "llvm/ADT/ArrayRef.h"

#include <optional>

namespace llvm {
class raw_ostream;
}

namespace neverc {
namespace driver {

struct EarlyArgumentResult {
  std::optional<int> ExitCode;
  bool CanonicalPrefixes = true;
};

/// Handle options that must run before diagnostics and compilation are set up.
EarlyArgumentResult
processEarlyDriverArguments(llvm::ArrayRef<const char *> Args,
                            llvm::raw_ostream &Out, llvm::raw_ostream &Err,
                            bool StandardOutputIsDisplayed);

} // namespace driver
} // namespace neverc

#endif // NEVERC_DRIVER_ARGUMENTHANDLERS_H
