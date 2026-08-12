//===--- SubcommandHandlers.h - Standalone subcommand dispatch --*- C++ -*-===//
//
// Part of the NeverC Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef NEVERC_DRIVER_SUBCOMMANDHANDLERS_H
#define NEVERC_DRIVER_SUBCOMMANDHANDLERS_H

#include "llvm/ADT/ArrayRef.h"

#include <optional>

namespace neverc {
namespace driver {

struct SubcommandContext {
  const char *ExecutablePath = nullptr;
  const char *PrependArg = nullptr;
};

/// Dispatch a standalone public or private subcommand. Returns nullopt when
/// \p Args describe a normal compiler-driver invocation.
std::optional<int> dispatchSubcommand(llvm::ArrayRef<const char *> Args,
                                      const SubcommandContext &Context);

} // namespace driver
} // namespace neverc

#endif // NEVERC_DRIVER_SUBCOMMANDHANDLERS_H
