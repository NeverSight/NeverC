//===------ PGOOptions.h -- PGO option tunables ----------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// Define option tunables for PGO.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_PGOOPTIONS_H
#define LLVM_SUPPORT_PGOOPTIONS_H

#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Error.h"
#include <assert.h>

namespace llvm {

namespace vfs {
class FileSystem;
} // namespace vfs

/// A struct capturing PGO tunables.
struct PGOOptions {
  enum PGOAction { NoAction, IRInstr, IRUse, SampleUse };
  enum CSPGOAction { NoCSAction, CSIRInstr, CSIRUse };
  inline PGOOptions(StringRef ProfileFile, StringRef CSProfileGenFile,
                    StringRef MemoryProfile,
                    IntrusiveRefCntPtr<vfs::FileSystem> FS,
                    PGOAction Action = NoAction,
                    CSPGOAction CSAction = NoCSAction,
                    bool DebugInfoForProfiling = false,
                    bool PseudoProbeForProfiling = false,
                    bool AtomicCounterUpdate = false)
      : ProfileFile(ProfileFile), CSProfileGenFile(CSProfileGenFile),
        MemoryProfile(MemoryProfile), Action(Action), CSAction(CSAction),
        DebugInfoForProfiling(
            DebugInfoForProfiling ||
            (Action == SampleUse && !PseudoProbeForProfiling)),
        PseudoProbeForProfiling(PseudoProbeForProfiling),
        AtomicCounterUpdate(AtomicCounterUpdate), FS(FS) {
    assert(this->CSAction == NoCSAction ||
           (this->Action != IRInstr && this->Action != SampleUse));
    assert(this->CSAction != CSIRInstr || !this->CSProfileGenFile.empty());
    assert(this->CSAction != CSIRUse || this->Action == IRUse);
    assert(this->MemoryProfile.empty() || this->Action != PGOOptions::IRInstr);
    assert(this->Action != NoAction || this->CSAction != NoCSAction ||
           !this->MemoryProfile.empty() || this->DebugInfoForProfiling ||
           this->PseudoProbeForProfiling);
    assert(this->FS || !(this->Action == IRUse || this->CSAction == CSIRUse ||
                         !this->MemoryProfile.empty()));
  }
  PGOOptions(const PGOOptions &) = default;
  ~PGOOptions() = default;
  PGOOptions &operator=(const PGOOptions &) = default;

  SmallString<256> ProfileFile;
  SmallString<256> CSProfileGenFile;
  SmallString<256> MemoryProfile;
  PGOAction Action;
  CSPGOAction CSAction;
  bool DebugInfoForProfiling;
  bool PseudoProbeForProfiling;
  bool AtomicCounterUpdate;
  IntrusiveRefCntPtr<vfs::FileSystem> FS;
};
} // namespace llvm

#endif
