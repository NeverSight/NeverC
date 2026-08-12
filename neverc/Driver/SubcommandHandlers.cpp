//===--- SubcommandHandlers.cpp - Standalone subcommand dispatch ----------===//
//
// Part of the NeverC Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SubcommandHandlers.h"

#include "neverc/Build/AndroidKernelBuildCommands.h"
#include "neverc/Build/BuildDriver.h"
#include "neverc/Run/RunDriver.h"
#include "neverc/Runtime/RuntimeManager.h"
#include "neverc/Update/UpdateManager.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/ErrorHandling.h"

using llvm::ArrayRef;
using llvm::StringRef;

namespace neverc {
namespace driver {
namespace {

enum class SubcommandKind {
  None,

  // Private commands used by a detached helper or generated build recipes.
  ApplyUpdate,
  AndroidKernelBuild,

  // User-facing commands.
  Update,
  Run,
  Build,
  Runtime,
};

SubcommandKind classifySubcommand(StringRef Command) {
  if (build::classifyAndroidKernelBuildCommand(Command) !=
      build::AndroidKernelBuildCommandKind::None)
    return SubcommandKind::AndroidKernelBuild;

  return llvm::StringSwitch<SubcommandKind>(Command)
      .Case("__neverc_apply_update", SubcommandKind::ApplyUpdate)
      .Cases("update", "upgrade", SubcommandKind::Update)
      .Case("run", SubcommandKind::Run)
      .Cases("build", "make", SubcommandKind::Build)
      .Case("runtime", SubcommandKind::Runtime)
      .Default(SubcommandKind::None);
}

} // namespace

std::optional<int> dispatchSubcommand(ArrayRef<const char *> Args,
                                      const SubcommandContext &Context) {
  if (Args.size() < 2 || Args[1] == nullptr)
    return std::nullopt;

  const SubcommandKind Kind = classifySubcommand(Args[1]);
  if (Kind == SubcommandKind::None)
    return std::nullopt;

  llvm::SmallVector<const char *, 16> CommandArgs(Args.begin() + 1, Args.end());
  const int CommandArgc = static_cast<int>(CommandArgs.size());
  const char *Argv0 = Args.front();

  switch (Kind) {
  case SubcommandKind::ApplyUpdate:
    return update::runUpdateHelper(CommandArgc, CommandArgs.data(), Argv0);
  case SubcommandKind::AndroidKernelBuild:
    return build::dispatchAndroidKernelBuildCommand(Args);
  case SubcommandKind::Update:
    return update::runUpdate(CommandArgc, CommandArgs.data(), Argv0);
  case SubcommandKind::Run:
    return run::runCommand(CommandArgc, CommandArgs.data(),
                           Context.ExecutablePath, Context.PrependArg);
  case SubcommandKind::Build:
    return build::runBuild(CommandArgc, CommandArgs.data(), Argv0,
                           Context.PrependArg);
  case SubcommandKind::Runtime:
    return runtime::runRuntime(CommandArgc, CommandArgs.data(), Argv0);
  case SubcommandKind::None:
    llvm_unreachable("unhandled NeverC subcommand");
  }

  llvm_unreachable("unknown NeverC subcommand");
}

} // namespace driver
} // namespace neverc
