//===- RegAllocPriorityAdvisor.cpp - live ranges priority advisor ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementation of the default priority advisor and of the Analysis pass.
//
//===----------------------------------------------------------------------===//

#include "RegAllocPriorityAdvisor.h"
#include "RegAllocGreedy.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/VirtRegMap.h"
#include "llvm/IR/Module.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"

using namespace llvm;

static cl::opt<RegAllocPriorityAdvisorAnalysis::AdvisorMode> Mode(
    "regalloc-enable-priority-advisor", cl::Hidden,
    cl::init(RegAllocPriorityAdvisorAnalysis::AdvisorMode::Default),
    cl::desc("Enable regalloc advisor mode"),
    cl::values(clEnumValN(RegAllocPriorityAdvisorAnalysis::AdvisorMode::Default,
                          "default", "Default")));

char RegAllocPriorityAdvisorAnalysis::ID = 0;
INITIALIZE_PASS(RegAllocPriorityAdvisorAnalysis, "regalloc-priority",
                "Regalloc priority policy", false, true)

namespace {
class DefaultPriorityAdvisorAnalysis final
    : public RegAllocPriorityAdvisorAnalysis {
public:
  DefaultPriorityAdvisorAnalysis(bool NotAsRequested)
      : RegAllocPriorityAdvisorAnalysis(AdvisorMode::Default),
        NotAsRequested(NotAsRequested) {}

  // support for isa<> and dyn_cast.
  static bool classof(const RegAllocPriorityAdvisorAnalysis *R) {
    return R->getAdvisorMode() == AdvisorMode::Default;
  }

private:
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<SlotIndexes>();
    RegAllocPriorityAdvisorAnalysis::getAnalysisUsage(AU);
  }
  std::unique_ptr<RegAllocPriorityAdvisor>
  getAdvisor(const MachineFunction &MF, const RAGreedy &RA) override {
    return std::make_unique<DefaultPriorityAdvisor>(
        MF, RA, &getAnalysis<SlotIndexes>());
  }
  bool doInitialization(Module &M) override {
    if (NotAsRequested)
      M.getContext().emitError("Requested regalloc priority advisor analysis "
                               "could be created. Using default");
    return RegAllocPriorityAdvisorAnalysis::doInitialization(M);
  }
  const bool NotAsRequested;
};
} // namespace

template <> Pass *llvm::callDefaultCtor<RegAllocPriorityAdvisorAnalysis>() {
  (void)Mode;
  return new DefaultPriorityAdvisorAnalysis(/*NotAsRequested*/ false);
}

StringRef RegAllocPriorityAdvisorAnalysis::getPassName() const {
  return "Default Regalloc Priority Advisor";
}

RegAllocPriorityAdvisor::RegAllocPriorityAdvisor(const MachineFunction &MF,
                                                 const RAGreedy &RA,
                                                 SlotIndexes *const Indexes)
    : RA(RA), LIS(RA.getLiveIntervals()), VRM(RA.getVirtRegMap()),
      MRI(&VRM->getRegInfo()), TRI(MF.getSubtarget().getRegisterInfo()),
      RegClassInfo(RA.getRegClassInfo()), Indexes(Indexes),
      RegClassPriorityTrumpsGlobalness(
          RA.getRegClassPriorityTrumpsGlobalness()),
      ReverseLocalAssignment(RA.getReverseLocalAssignment()) {}
