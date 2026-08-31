//===-- AArch64CodeLayoutOpt.cpp - Code Layout Optimizations --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Backport of LLVM 22 PR #184434: code layout optimizations.
//
// This pass runs after instruction scheduling and aligns functions containing
// layout-sensitive instruction pairs (FCMP-FCSEL, CMP/CMN-CSEL) to reduce
// cacheline-straddle performance penalties and stabilize performance.
//
// Option -aarch64-code-layout-opt is a bitmask:
// Bit 0 (0x1): Enable FCMP-FCSEL code layout optimization
// Bit 1 (0x2): Enable CMP/CMN-CSEL code layout optimization
//
//===----------------------------------------------------------------------===//

#include "AArch64.h"
#include "AArch64InstrInfo.h"
#include "AArch64Subtarget.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

#define DEBUG_TYPE "aarch64-code-layout-opt"
#define AARCH64_CODE_LAYOUT_OPT_NAME "AArch64 Code Layout Optimization"

static cl::opt<unsigned> EnableCodeAlignment(
    "aarch64-code-layout-opt", cl::Hidden,
    cl::desc("Enable code alignment optimization for instruction pairs "
             "(bitmask: bit 0 = FCMP-FCSEL, bit 1 = CMP-CSEL)"),
    cl::init(0));

static cl::opt<unsigned> FunctionAlignBytes(
    "aarch64-code-layout-opt-align-functions", cl::Hidden,
    cl::desc("Function alignment in bytes for code layout optimization "
             "(must be a power of 2)"),
    cl::init(64), cl::callback([](const unsigned &Val) {
      if (!isPowerOf2_32(Val))
        report_fatal_error(
            "aarch64-code-layout-opt-align must be a power of 2");
    }));

namespace llvm {
unsigned getAArch64CodeLayoutFunctionAlignmentForTesting() {
  return ::FunctionAlignBytes.getValue();
}
} // namespace llvm

STATISTIC(NumFunctionsAligned,
          "Number of functions with aligned (to 64-bytes by default)");
STATISTIC(NumFcmpFcselPairsDetected,
          "Number of FCMP-FCSEL pairs detected for alignment");
STATISTIC(NumCmpCselPairsDetected,
          "Number of CMP/CMN-CSEL pairs detected for alignment");

namespace {

class AArch64CodeLayoutOpt : public MachineFunctionPass {
public:
  static char ID;
  AArch64CodeLayoutOpt() : MachineFunctionPass(ID) {}
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override {
    return AARCH64_CODE_LAYOUT_OPT_NAME;
  }

private:
  const AArch64InstrInfo *TII = nullptr;

  bool detectLayoutSensitivePattern(MachineBasicBlock *MBB);
  bool optimizeForCodeLayout(MachineFunction &MF);
};

} // end anonymous namespace

char AArch64CodeLayoutOpt::ID = 0;

INITIALIZE_PASS(AArch64CodeLayoutOpt, "aarch64-code-layout-opt",
                AARCH64_CODE_LAYOUT_OPT_NAME, false, false)

void AArch64CodeLayoutOpt::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  MachineFunctionPass::getAnalysisUsage(AU);
}

FunctionPass *llvm::createAArch64CodeLayoutOptPass() {
  return new AArch64CodeLayoutOpt();
}

static bool isFloatingPointCompare(unsigned Opc) {
  switch (Opc) {
  case AArch64::FCMPSrr:
  case AArch64::FCMPDrr:
  case AArch64::FCMPESrr:
  case AArch64::FCMPEDrr:
  case AArch64::FCMPHrr:
  case AArch64::FCMPEHrr:
    return true;
  default:
    return false;
  }
}

static bool isFloatingPointConditionalSelect(unsigned Opc) {
  switch (Opc) {
  case AArch64::FCSELSrrr:
  case AArch64::FCSELDrrr:
  case AArch64::FCSELHrrr:
    return true;
  default:
    return false;
  }
}

static bool isQualifyingIntCompare(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case AArch64::SUBSWrr:
  case AArch64::ADDSWrr:
    return MI.definesRegister(AArch64::WZR, /*TRI=*/nullptr);
  case AArch64::SUBSWri:
  case AArch64::ADDSWri:
    return MI.definesRegister(AArch64::WZR, /*TRI=*/nullptr) &&
           MI.getOperand(3).getImm() == 0 && MI.getOperand(2).getImm() <= 15;
  case AArch64::SUBSWrs:
  case AArch64::ADDSWrs:
    return MI.definesRegister(AArch64::WZR, /*TRI=*/nullptr) &&
           !AArch64InstrInfo::hasShiftedReg(MI);
  case AArch64::SUBSWrx:
    return MI.definesRegister(AArch64::WZR, /*TRI=*/nullptr) &&
           !AArch64InstrInfo::hasExtendedReg(MI);
  default:
    return false;
  }
}

bool AArch64CodeLayoutOpt::runOnMachineFunction(MachineFunction &MF) {
  if (!EnableCodeAlignment)
    return false;

  const Function &F = MF.getFunction();
  if (F.hasOptSize())
    return false;

  const auto *Subtarget = &MF.getSubtarget<AArch64Subtarget>();
  TII = Subtarget->getInstrInfo();

  const unsigned Mask = EnableCodeAlignment;
  if (!((Mask & 0x1) && Subtarget->hasFuseFCmpFCSel()) &&
      !((Mask & 0x2) && Subtarget->hasFuseCmpCSel()))
    return false;

  return optimizeForCodeLayout(MF);
}

bool AArch64CodeLayoutOpt::detectLayoutSensitivePattern(
    MachineBasicBlock *MBB) {
  auto Instrs = instructionsWithoutDebug(MBB->begin(), MBB->end());
  auto End = MBB->instr_end();

  if (EnableCodeAlignment & 0x1) {
    if (llvm::any_of(Instrs, [End](MachineInstr &MI) {
          if (!isFloatingPointCompare(MI.getOpcode()))
            return false;
          auto NextIt =
              skipDebugInstructionsForward(std::next(MI.getIterator()), End);
          return NextIt != End &&
                 isFloatingPointConditionalSelect(NextIt->getOpcode());
        })) {
      ++NumFcmpFcselPairsDetected;
      return true;
    }
  }

  if (EnableCodeAlignment & 0x2) {
    if (llvm::any_of(Instrs, [End](MachineInstr &MI) {
          if (!isQualifyingIntCompare(MI))
            return false;
          auto NextIt =
              skipDebugInstructionsForward(std::next(MI.getIterator()), End);
          return NextIt != End && NextIt->getOpcode() == AArch64::CSELWr;
        })) {
      ++NumCmpCselPairsDetected;
      return true;
    }
  }

  return false;
}

bool AArch64CodeLayoutOpt::optimizeForCodeLayout(MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << DEBUG_TYPE ": optimizeForCodeLayout: " << MF.getName()
                    << "\n");

  for (auto &MBB : MF) {
    if (!detectLayoutSensitivePattern(&MBB))
      continue;

    if (MF.getAlignment() >= Align(FunctionAlignBytes)) {
      LLVM_DEBUG(dbgs() << DEBUG_TYPE ": Function " << MF.getName()
                        << " already has sufficient alignment\n");
      return false;
    }

    MF.setAlignment(Align(FunctionAlignBytes));
    ++NumFunctionsAligned;
    LLVM_DEBUG(dbgs() << DEBUG_TYPE ": Set " << FunctionAlignBytes
                      << "-byte alignment for function " << MF.getName()
                      << "\n");
    return true;
  }

  return false;
}
