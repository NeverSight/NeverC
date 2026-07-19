#include "neverc/Plugin/Host/MIRPluginBridge.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Host.h"
#include "gtest/gtest.h"
#include <memory>
#include <optional>
#include <string>

using namespace llvm;
using namespace neverc::plugin;

namespace {

std::string errorMessage(Error E) { return toString(std::move(E)).str().str(); }

class EmptyMIRTask {
public:
  EmptyMIRTask()
      : Services("neverc-plugin-mir-handle-tests", LLVM_VERSION_MAJOR) {}

  bool initialize() {
    if (Error E = Services.interfaces().freeze()) {
      ADD_FAILURE() << errorMessage(std::move(E));
      return false;
    }
    auto CreatedPlan = makePluginActivationPlan(Services.registry(), {});
    if (!CreatedPlan) {
      ADD_FAILURE() << errorMessage(CreatedPlan.takeError());
      return false;
    }
    Plan.emplace(std::move(*CreatedPlan));
    auto CreatedSession = PluginSession::create(Services, *Plan);
    if (!CreatedSession) {
      ADD_FAILURE() << errorMessage(CreatedSession.takeError());
      return false;
    }
    Session = std::move(*CreatedSession);
    auto CreatedTask = Session->createTask(NEVERC_TASK_CODEGEN);
    if (!CreatedTask) {
      ADD_FAILURE() << errorMessage(CreatedTask.takeError());
      return false;
    }
    Task = std::move(*CreatedTask);
    return true;
  }

  ~EmptyMIRTask() {
    if (Task)
      EXPECT_FALSE(Task->end());
    if (Session)
      EXPECT_FALSE(Session->end());
    Plan.reset();
    EXPECT_FALSE(Services.shutdown());
  }

  PluginTaskContext &task() { return *Task; }

private:
  PluginProcessServices Services;
  std::optional<PluginActivationPlan> Plan;
  std::unique_ptr<PluginSession> Session;
  std::unique_ptr<PluginTaskContext> Task;
};

class MachineFixture {
public:
  bool initialize() {
    static const bool Initialized = [] {
      InitializeNativeTarget();
      InitializeNativeTargetAsmPrinter();
      return true;
    }();
    (void)Initialized;

    std::string Error;
    std::string TripleName = sys::getDefaultTargetTriple();
    const Target *Target = TargetRegistry::lookupTarget(TripleName, Error);
    if (!Target) {
      ADD_FAILURE() << Error;
      return false;
    }
    TargetOptions Options;
    TargetMachineOwner.reset(Target->createTargetMachine(
        TripleName, "generic", "", Options, std::nullopt));
    if (!TargetMachineOwner) {
      ADD_FAILURE() << "failed to create native target machine";
      return false;
    }
    auto *TargetMachine =
        static_cast<LLVMTargetMachine *>(TargetMachineOwner.get());
    IRModule = std::make_unique<Module>("mir-handles", Context);
    IRModule->setTargetTriple(TripleName);
    IRModule->setDataLayout(TargetMachine->createDataLayout());
    FunctionType *Type = FunctionType::get(Type::getVoidTy(Context), false);
    IRFunction = Function::Create(Type, GlobalValue::ExternalLinkage,
                                  "function", *IRModule);
    MMI = std::make_unique<MachineModuleInfo>(TargetMachine);
    MF = &MMI->getOrCreateMachineFunction(*IRFunction);
    MBB = MF->CreateMachineBasicBlock();
    MF->push_back(MBB);
    const TargetInstrInfo *TII = MF->getSubtarget().getInstrInfo();
    MI = MF->CreateMachineInstr(TII->get(TargetOpcode::IMPLICIT_DEF),
                                DebugLoc());
    MBB->push_back(MI);
    MI->addOperand(*MF, MachineOperand::CreateImm(1));
    return true;
  }

  LLVMContext Context;
  std::unique_ptr<TargetMachine> TargetMachineOwner;
  std::unique_ptr<Module> IRModule;
  Function *IRFunction = nullptr;
  std::unique_ptr<MachineModuleInfo> MMI;
  MachineFunction *MF = nullptr;
  MachineBasicBlock *MBB = nullptr;
  MachineInstr *MI = nullptr;
};

TEST(PluginMIRHandleTest, ReusesHandlesAndSurvivesBlockRenumbering) {
  EmptyMIRTask Scope;
  MachineFixture Machine;
  ASSERT_TRUE(Scope.initialize());
  ASSERT_TRUE(Machine.initialize());
  MIRPluginBridge Bridge(Scope.task(), *Machine.MF, 17);

  auto Function = Bridge.machineFunction();
  auto Block = Bridge.wrapBasicBlock(*Machine.MBB);
  auto Instruction = Bridge.wrapInstruction(*Machine.MI);
  auto Operand = Bridge.wrapOperand(Machine.MI->getOperand(0));
  ASSERT_TRUE(static_cast<bool>(Function));
  ASSERT_TRUE(static_cast<bool>(Block));
  ASSERT_TRUE(static_cast<bool>(Instruction));
  ASSERT_TRUE(static_cast<bool>(Operand));
  EXPECT_EQ(Bridge.functionGeneration(), 17U);

  Machine.MF->RenumberBlocks();
  auto BlockAgain = Bridge.wrapBasicBlock(*Machine.MBB);
  ASSERT_TRUE(static_cast<bool>(BlockAgain));
  EXPECT_EQ(BlockAgain->Owner, Block->Owner);
  EXPECT_EQ(BlockAgain->Value, Block->Value);

  MachineBasicBlock *ResolvedBlock = nullptr;
  EXPECT_EQ(Bridge.resolveBasicBlock(*Block, &ResolvedBlock).Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(ResolvedBlock, Machine.MBB);
}

TEST(PluginMIRHandleTest, InvalidatesInstructionAndOperandTogether) {
  EmptyMIRTask Scope;
  MachineFixture Machine;
  ASSERT_TRUE(Scope.initialize());
  ASSERT_TRUE(Machine.initialize());
  MIRPluginBridge Bridge(Scope.task(), *Machine.MF);

  auto Instruction = Bridge.wrapInstruction(*Machine.MI);
  auto Operand = Bridge.wrapOperand(Machine.MI->getOperand(0));
  ASSERT_TRUE(static_cast<bool>(Instruction));
  ASSERT_TRUE(static_cast<bool>(Operand));
  ASSERT_EQ(Bridge.invalidateInstruction(*Machine.MI).Code, NEVERC_STATUS_OK);

  MachineInstr *ResolvedInstruction = nullptr;
  MachineOperand *ResolvedOperand = nullptr;
  EXPECT_NE(Bridge.resolveInstruction(*Instruction, &ResolvedInstruction).Code,
            NEVERC_STATUS_OK);
  EXPECT_NE(Bridge.resolveOperand(*Operand, &ResolvedOperand).Code,
            NEVERC_STATUS_OK);
}

TEST(PluginMIRHandleTest, RejectsHandlesFromAnotherCodeGenTask) {
  EmptyMIRTask FirstScope;
  EmptyMIRTask SecondScope;
  MachineFixture Machine;
  ASSERT_TRUE(FirstScope.initialize());
  ASSERT_TRUE(SecondScope.initialize());
  ASSERT_TRUE(Machine.initialize());
  MIRPluginBridge First(FirstScope.task(), *Machine.MF);
  MIRPluginBridge Second(SecondScope.task(), *Machine.MF);

  auto Handle = First.wrapBasicBlock(*Machine.MBB);
  ASSERT_TRUE(static_cast<bool>(Handle));
  MachineBasicBlock *Resolved = nullptr;
  EXPECT_NE(Second.resolveBasicBlock(*Handle, &Resolved).Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Resolved, nullptr);
}

} // namespace
