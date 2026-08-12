//===- RecursiveMakeEnvironment.cpp - Recursive make variables ------------===//
//
// Part of the NeverC Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "neverc/Build/RecursiveMakeEnvironment.h"

#include "neverc/Build/BuildConstants.h"
#include "neverc/Build/StringUtils.h"
#include "neverc/Build/VariableEnv.h"

#include <algorithm>

namespace neverc {
namespace build {
namespace {

std::string quoteRecipeArgument(llvm::StringRef Argument) {
  std::string Result;
  Result.reserve(Argument.size() + 2);
#if defined(_WIN32)
  Result.push_back('"');
  Result.append(Argument.begin(), Argument.end());
  Result.push_back('"');
#else
  Result.push_back('\'');
  for (char Character : Argument) {
    if (Character == '\'')
      Result += "'\\''";
    else
      Result.push_back(Character);
  }
  Result.push_back('\'');
#endif
  return Result;
}

} // namespace

void RecursiveMakeEnvironment::apply(
    VariableEnv &Env, const RecursiveMakeEnvironmentConfig &Config) {
  for (const auto &[Name, Value] : Config.CommandLineVariables)
    Env.setCommandLineVar(Name, Value);

  Env.set(constants::VarMakefileList.str(), Config.MakefilePath.str(),
          AssignMode::Simple, VariableEnv::Origin::Default);
  Env.set(constants::VarCurdir.str(), Config.CurrentDirectory.str(),
          AssignMode::Simple, VariableEnv::Origin::Default);

  std::string NeverCCommand = quoteRecipeArgument(Config.MakeExecutable);
  if (!Config.PrependArg.empty()) {
    NeverCCommand += ' ';
    NeverCCommand += quoteRecipeArgument(Config.PrependArg);
  }
  Env.set(constants::VarNeverCMakeExecutable.str(), NeverCCommand,
          AssignMode::Simple, VariableEnv::Origin::Default);

  std::string RecursiveMake = NeverCCommand + " make";
  for (size_t Index = 0; Index != Config.CommandLineVariables.size();
       ++Index) {
    const auto &Variable = Config.CommandLineVariables[Index];
    const bool Superseded = std::any_of(
        Config.CommandLineVariables.begin() + Index + 1,
        Config.CommandLineVariables.end(),
        [&](const auto &Later) { return Later.first == Variable.first; });
    if (Superseded)
      continue;
    RecursiveMake += ' ';
    RecursiveMake +=
        quoteRecipeArgument(Variable.first + "=" + Variable.second);
  }
  Env.set(constants::VarMake.str(), std::move(RecursiveMake),
          AssignMode::Simple, VariableEnv::Origin::Default);
  Env.set(constants::VarMakeVersion.str(), constants::MakeVersionValue.str(),
          AssignMode::Simple, VariableEnv::Origin::Default);

  std::string Flags;
  if (Config.DryRun)
    Flags += 'n';
  if (Config.KeepGoing)
    Flags += 'k';
  if (Config.Silent)
    Flags += 's';
  if (Config.AlwaysMake)
    Flags += 'B';
  if (Config.Jobs > 1)
    Flags += " -j" + std::to_string(Config.Jobs);
  for (const auto &[Name, Value] : Config.CommandLineVariables)
    Flags += " " + Name + "=" + Value;
  Env.set(constants::VarMakeFlags.str(), Flags, AssignMode::Simple,
          VariableEnv::Origin::Default);

  if (!Config.Targets.empty())
    Env.set(constants::VarMakeCmdGoals.str(), joinWords(Config.Targets),
            AssignMode::Simple, VariableEnv::Origin::Default);
}

} // namespace build
} // namespace neverc
