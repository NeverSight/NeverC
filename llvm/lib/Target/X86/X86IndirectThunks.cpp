//==- X86IndirectThunks.cpp - Construct indirect call/jump thunks for x86  --=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// Pass that injects an MI thunk that is used to lower indirect calls in a way
/// that prevents speculation on some x86 processors and can be used to mitigate
/// security vulnerabilities due to targeted speculative execution and side
/// channels such as CVE-2017-5715.
///
/// Currently supported thunks include:
/// - Retpoline -- A RET-implemented trampoline that lowers indirect calls
/// - LVI Thunk -- A CALL/JMP-implemented thunk that forces load serialization
///   before making an indirect call/jump
///
/// Note that the reason that this is implemented as a MachineFunctionPass and
/// not a ModulePass is that ModulePasses at this point in the LLVM X86 pipeline
/// serialize all transformations, which can consume lots of memory.
///
/// TODO(chandlerc): All of this code could use better comments and
/// documentation.
///
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "X86InstrBuilder.h"
#include "X86Subtarget.h"
#include "llvm/CodeGen/IndirectThunks.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

#define DEBUG_TYPE "x86-retpoline-thunks"

static const char RetpolineNamePrefix[] = "__llvm_retpoline_";
static const char R11RetpolineName[] = "__llvm_retpoline_r11";

static const char LVIThunkNamePrefix[] = "__llvm_lvi_thunk_";
static const char R11LVIThunkName[] = "__llvm_lvi_thunk_r11";

namespace {
struct RetpolineThunkInserter : ThunkInserter<RetpolineThunkInserter> {
  const char *getThunkPrefix() { return RetpolineNamePrefix; }
  bool mayUseThunk(const MachineFunction &MF, bool InsertedThunks) {
    if (InsertedThunks)
      return false;
    const auto &STI = MF.getSubtarget<X86Subtarget>();
    return (STI.useRetpolineIndirectCalls() ||
            STI.useRetpolineIndirectBranches()) &&
           !STI.useRetpolineExternalThunk();
  }
  bool insertThunks(MachineModuleInfo &MMI, MachineFunction &MF);
  void populateThunk(MachineFunction &MF);
};

struct LVIThunkInserter : ThunkInserter<LVIThunkInserter> {
  const char *getThunkPrefix() { return LVIThunkNamePrefix; }
  bool mayUseThunk(const MachineFunction &MF, bool InsertedThunks) {
    if (InsertedThunks)
      return false;
    return MF.getSubtarget<X86Subtarget>().useLVIControlFlowIntegrity();
  }
  bool insertThunks(MachineModuleInfo &MMI, MachineFunction &MF) {
    createThunkFunction(MMI, R11LVIThunkName);
    return true;
  }
  void populateThunk(MachineFunction &MF) {
    assert(MF.size() == 1);
    MachineBasicBlock *Entry = &MF.front();
    Entry->clear();

    // This code mitigates LVI by replacing each indirect call/jump with a
    // direct call/jump to a thunk that looks like:
    // ```
    // lfence
    // jmpq *%r11
    // ```
    // This ensures that if the value in register %r11 was loaded from memory,
    // then the value in %r11 is (architecturally) correct prior to the jump.
    const TargetInstrInfo *TII = MF.getSubtarget<X86Subtarget>().getInstrInfo();
    BuildMI(&MF.front(), DebugLoc(), TII->get(X86::LFENCE));
    BuildMI(&MF.front(), DebugLoc(), TII->get(X86::JMP64r)).addReg(X86::R11);
    MF.front().addLiveIn(X86::R11);
  }
};

class X86IndirectThunks : public MachineFunctionPass {
public:
  static char ID;

  X86IndirectThunks() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return "X86 Indirect Thunks"; }

  bool doInitialization(Module &M) override;
  bool runOnMachineFunction(MachineFunction &MF) override;

private:
  std::tuple<RetpolineThunkInserter, LVIThunkInserter> TIs;

  // FIXME: When LLVM moves to C++17, these can become folds
  template <typename... ThunkInserterT>
  static void initTIs(Module &M,
                      std::tuple<ThunkInserterT...> &ThunkInserters) {
    (void)std::initializer_list<int>{
        (std::get<ThunkInserterT>(ThunkInserters).init(M), 0)...};
  }
  template <typename... ThunkInserterT>
  static bool runTIs(MachineModuleInfo &MMI, MachineFunction &MF,
                     std::tuple<ThunkInserterT...> &ThunkInserters) {
    bool Modified = false;
    (void)std::initializer_list<int>{
        Modified |= std::get<ThunkInserterT>(ThunkInserters).run(MMI, MF)...};
    return Modified;
  }
};

} // end anonymous namespace

bool RetpolineThunkInserter::insertThunks(MachineModuleInfo &MMI,
                                          MachineFunction &MF) {
  createThunkFunction(MMI, R11RetpolineName);
  return true;
}

void RetpolineThunkInserter::populateThunk(MachineFunction &MF) {
  assert(MF.getName() == "__llvm_retpoline_r11");
  Register ThunkReg = X86::R11;

  const TargetInstrInfo *TII = MF.getSubtarget<X86Subtarget>().getInstrInfo();
  assert(MF.size() == 1);
  MachineBasicBlock *Entry = &MF.front();
  Entry->clear();

  MachineBasicBlock *CaptureSpec =
      MF.CreateMachineBasicBlock(Entry->getBasicBlock());
  MachineBasicBlock *CallTarget =
      MF.CreateMachineBasicBlock(Entry->getBasicBlock());
  MCSymbol *TargetSym = MF.getContext().createTempSymbol();
  MF.push_back(CaptureSpec);
  MF.push_back(CallTarget);

  Entry->addLiveIn(ThunkReg);
  BuildMI(Entry, DebugLoc(), TII->get(X86::CALL64pcrel32)).addSym(TargetSym);

  Entry->addSuccessor(CaptureSpec);

  BuildMI(CaptureSpec, DebugLoc(), TII->get(X86::PAUSE));
  BuildMI(CaptureSpec, DebugLoc(), TII->get(X86::LFENCE));
  BuildMI(CaptureSpec, DebugLoc(), TII->get(X86::JMP_1)).addMBB(CaptureSpec);
  CaptureSpec->setMachineBlockAddressTaken();
  CaptureSpec->addSuccessor(CaptureSpec);

  CallTarget->addLiveIn(ThunkReg);
  CallTarget->setMachineBlockAddressTaken();
  CallTarget->setAlignment(Align(16));

  addRegOffset(BuildMI(CallTarget, DebugLoc(), TII->get(X86::MOV64mr)),
               X86::RSP, false, 0)
      .addReg(ThunkReg);

  CallTarget->back().setPreInstrSymbol(MF, TargetSym);
  BuildMI(CallTarget, DebugLoc(), TII->get(X86::RET64));
}

FunctionPass *llvm::createX86IndirectThunksPass() {
  return new X86IndirectThunks();
}

char X86IndirectThunks::ID = 0;

bool X86IndirectThunks::doInitialization(Module &M) {
  initTIs(M, TIs);
  return false;
}

bool X86IndirectThunks::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << getPassName() << '\n');
  auto &MMI = getAnalysis<MachineModuleInfoWrapperPass>().getMMI();
  return runTIs(MMI, MF, TIs);
}
