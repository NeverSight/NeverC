//===- MakefileInterpreter.h - Evaluate parsed makefiles -------*- C++ -*-===//
//
// Part of the NeverC Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef NEVERC_BUILD_MAKEFILEINTERPRETER_H
#define NEVERC_BUILD_MAKEFILEINTERPRETER_H

namespace neverc {
namespace build {

class FunctionRegistry;
struct MakefileAST;
class RuleDB;
class VariableEnv;

/// Applies makefile statements to a variable environment and rule database.
class MakefileInterpreter {
public:
  MakefileInterpreter(VariableEnv &Env, RuleDB &Rules,
                      FunctionRegistry &Functions);

  void process(MakefileAST &AST);

private:
  VariableEnv &Env;
  RuleDB &Rules;
  FunctionRegistry &Functions;
};

} // namespace build
} // namespace neverc

#endif // NEVERC_BUILD_MAKEFILEINTERPRETER_H
