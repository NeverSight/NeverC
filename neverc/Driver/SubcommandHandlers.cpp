//===--- SubcommandHandlers.cpp - Standalone subcommand dispatch ----------===//
//
// Part of the NeverC Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SubcommandHandlers.h"

#include "neverc/Build/BuildDriver.h"
#include "neverc/Foundation/AndroidKernelReleaseSymbolMap.h"
#include "neverc/Foundation/Core/OutputTransaction.h"
#include "neverc/Run/RunDriver.h"
#include "neverc/Runtime/RuntimeManager.h"
#include "neverc/Update/UpdateManager.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

#include <string>

using llvm::ArrayRef;
using llvm::StringRef;

namespace neverc {
namespace driver {
namespace {

enum class SubcommandKind {
  None,

  // Private commands used by a detached helper or generated build recipes.
  ApplyUpdate,
  CleanAndroidKernelOutput,
  AndroidKernelOutputIntegrity,

  // User-facing commands.
  Update,
  Run,
  Build,
  Runtime,
};

SubcommandKind classifySubcommand(StringRef Command) {
  return llvm::StringSwitch<SubcommandKind>(Command)
      .Case("__neverc_apply_update", SubcommandKind::ApplyUpdate)
      .Case("__neverc_clean_android_kernel_output",
            SubcommandKind::CleanAndroidKernelOutput)
      .Case("__neverc_android_kernel_output_integrity",
            SubcommandKind::AndroidKernelOutputIntegrity)
      .Cases("update", "upgrade", SubcommandKind::Update)
      .Case("run", SubcommandKind::Run)
      .Cases("build", "make", SubcommandKind::Build)
      .Case("runtime", SubcommandKind::Runtime)
      .Default(SubcommandKind::None);
}

int runCleanAndroidKernelOutput(ArrayRef<const char *> Args) {
  if (Args.size() != 3) {
    llvm::errs() << "usage: " << Args.front()
                 << " __neverc_clean_android_kernel_output <module.ko>\n";
    return 1;
  }

  OutputCoordinator Coordinator;
  OutputBundleSummary FinalSummary;
  auto Cleaned = cleanAndroidKernelReleaseOutput(
      Coordinator, StringRef(Args[2]), /*LeaseOwner=*/{}, &FinalSummary);
  if (!Cleaned) {
    std::string Message = llvm::toString(Cleaned.takeError()).str().str();
    if (FinalSummary.Flags & OutputPublished)
      Message += "; output cleanup completed before this failure";
    if (FinalSummary.Flags & OutputDurabilityUnconfirmed)
      Message += "; output-directory durability is unconfirmed";
    if (FinalSummary.Flags & OutputRecoveryRequired) {
      Message += "; output recovery is required";
      if (!FinalSummary.JournalPath.empty())
        Message += " (journal: " + FinalSummary.JournalPath + ")";
    }
    llvm::errs() << Message << '\n';
    return 1;
  }

  if (Cleaned->Flags & OutputDurabilityUnconfirmed)
    llvm::errs() << "warning: Android kernel output cleanup completed, but "
                    "output-directory durability could not be confirmed\n";
  return 0;
}

int runAndroidKernelOutputIntegrity(ArrayRef<const char *> Args) {
  if (Args.size() != 3) {
    llvm::errs() << "usage: " << Args.front()
                 << " __neverc_android_kernel_output_integrity <module.ko>\n";
    return 1;
  }

  auto Integrity = currentAndroidKernelBuildIntegrity(StringRef(Args[2]));
  if (!Integrity) {
    llvm::errs() << llvm::toString(Integrity.takeError()) << '\n';
    return 1;
  }
  llvm::outs() << *Integrity << '\n';
  return 0;
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
  case SubcommandKind::CleanAndroidKernelOutput:
    return runCleanAndroidKernelOutput(Args);
  case SubcommandKind::AndroidKernelOutputIntegrity:
    return runAndroidKernelOutputIntegrity(Args);
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
