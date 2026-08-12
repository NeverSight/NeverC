//===- AndroidKernelBuildCommands.cpp - Android build helpers -------------===//
//
// Part of the NeverC Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "neverc/Build/AndroidKernelBuildCommands.h"

#include "neverc/Foundation/AndroidKernelReleasePublisher.h"
#include "neverc/Foundation/AndroidKernelReleaseSymbolMap.h"
#include "neverc/Foundation/Core/OutputTransaction.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

#include <string>

namespace neverc {
namespace build {
namespace {

int runCleanAndroidKernelOutput(llvm::ArrayRef<const char *> Args) {
  if (Args.size() != 3) {
    llvm::errs() << "usage: " << Args.front()
                 << " __neverc_clean_android_kernel_output <module.ko>\n";
    return 1;
  }

  OutputCoordinator Coordinator;
  OutputBundleSummary FinalSummary;
  auto Cleaned = cleanAndroidKernelReleaseOutput(
      Coordinator, llvm::StringRef(Args[2]), /*LeaseOwner=*/{}, &FinalSummary);
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

int runAndroidKernelOutputIntegrity(llvm::ArrayRef<const char *> Args) {
  if (Args.size() != 3) {
    llvm::errs() << "usage: " << Args.front()
                 << " __neverc_android_kernel_output_integrity <module.ko>\n";
    return 1;
  }

  auto Integrity =
      currentAndroidKernelBuildIntegrity(llvm::StringRef(Args[2]));
  if (!Integrity) {
    llvm::errs() << llvm::toString(Integrity.takeError()) << '\n';
    return 1;
  }
  llvm::outs() << *Integrity << '\n';
  return 0;
}

} // namespace

AndroidKernelBuildCommandKind
classifyAndroidKernelBuildCommand(llvm::StringRef Command) {
  return llvm::StringSwitch<AndroidKernelBuildCommandKind>(Command)
      .Case("__neverc_clean_android_kernel_output",
            AndroidKernelBuildCommandKind::CleanOutput)
      .Case("__neverc_android_kernel_output_integrity",
            AndroidKernelBuildCommandKind::OutputIntegrity)
      .Default(AndroidKernelBuildCommandKind::None);
}

std::optional<int>
dispatchAndroidKernelBuildCommand(llvm::ArrayRef<const char *> Args) {
  if (Args.size() < 2 || Args[1] == nullptr)
    return std::nullopt;

  switch (classifyAndroidKernelBuildCommand(Args[1])) {
  case AndroidKernelBuildCommandKind::CleanOutput:
    return runCleanAndroidKernelOutput(Args);
  case AndroidKernelBuildCommandKind::OutputIntegrity:
    return runAndroidKernelOutputIntegrity(Args);
  case AndroidKernelBuildCommandKind::None:
    return std::nullopt;
  }

  llvm_unreachable("unknown Android kernel build command");
}

} // namespace build
} // namespace neverc
