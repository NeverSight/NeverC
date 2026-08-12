//===- RecursiveMakeEnvironment.h - Recursive make variables ---*- C++ -*-===//
//
// Part of the NeverC Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef NEVERC_BUILD_RECURSIVEMAKEENVIRONMENT_H
#define NEVERC_BUILD_RECURSIVEMAKEENVIRONMENT_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"

#include <string>
#include <utility>

namespace neverc {
namespace build {

class VariableEnv;

struct RecursiveMakeEnvironmentConfig {
  llvm::StringRef MakefilePath;
  llvm::StringRef MakeExecutable;
  llvm::StringRef CurrentDirectory;
  llvm::StringRef PrependArg;
  llvm::ArrayRef<std::pair<std::string, std::string>> CommandLineVariables;
  llvm::ArrayRef<std::string> Targets;
  unsigned Jobs = 1;
  bool DryRun = false;
  bool KeepGoing = false;
  bool Silent = false;
  bool AlwaysMake = false;
};

class RecursiveMakeEnvironment {
public:
  static void apply(VariableEnv &Env,
                    const RecursiveMakeEnvironmentConfig &Config);
};

} // namespace build
} // namespace neverc

#endif // NEVERC_BUILD_RECURSIVEMAKEENVIRONMENT_H
