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
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Host.h"
#include "gtest/gtest.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace llvm;
using namespace neverc::plugin;

namespace {

std::string errorMessage(Error E) { return toString(std::move(E)).str().str(); }

class MIRTaskScope {
public:
  MIRTaskScope()
      : Services("neverc-plugin-mir-core-tests", LLVM_VERSION_MAJOR) {}

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

  ~MIRTaskScope() {
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

class MIRCoreFixture {
public:
  bool initialize() {
    static const bool Initialized = [] {
      InitializeNativeTarget();
      InitializeNativeTargetAsmPrinter();
      return true;
    }();
    (void)Initialized;
    std::string Error;
    TripleName = sys::getDefaultTargetTriple();
    const Target *Target = TargetRegistry::lookupTarget(TripleName, Error);
    if (!Target) {
      ADD_FAILURE() << Error;
      return false;
    }
    TargetOptions Options;
    TargetMachineOwner.reset(Target->createTargetMachine(
        TripleName, "generic", "", Options, std::nullopt));
    if (!TargetMachineOwner)
      return false;
    auto *TargetMachine =
        static_cast<LLVMTargetMachine *>(TargetMachineOwner.get());
    IRModule = std::make_unique<Module>("mir-core", Context);
    IRModule->setTargetTriple(TripleName);
    IRModule->setDataLayout(TargetMachine->createDataLayout());
    FunctionType *Type = FunctionType::get(Type::getVoidTy(Context), false);
    IRFunction = Function::Create(Type, GlobalValue::ExternalLinkage,
                                  "function", *IRModule);
    IRBlock = BasicBlock::Create(Context, "entry", IRFunction);
    ReturnInst::Create(Context, IRBlock);
    Global =
        new GlobalVariable(*IRModule, Type::getInt32Ty(Context), false,
                           GlobalValue::ExternalLinkage, nullptr, "global");
    BlockRef = BlockAddress::get(IRFunction, IRBlock);
    Metadata = MDNode::get(Context, {});

    MMI = std::make_unique<MachineModuleInfo>(TargetMachine);
    MF = &MMI->getOrCreateMachineFunction(*IRFunction);
    First = MF->CreateMachineBasicBlock(IRBlock);
    Second = MF->CreateMachineBasicBlock();
    MF->push_back(First);
    MF->push_back(Second);
    First->addSuccessor(Second, BranchProbability::getOne());
    First->addLiveIn(MCRegister(1));

    const TargetInstrInfo *TII = MF->getSubtarget().getInstrInfo();
    FirstMI = MF->CreateMachineInstr(TII->get(TargetOpcode::IMPLICIT_DEF),
                                     DebugLoc());
    SecondMI = MF->CreateMachineInstr(TII->get(TargetOpcode::COPY), DebugLoc());
    First->push_back(FirstMI);
    Second->push_back(SecondMI);
    FirstMI->setFlag(MachineInstr::FrameSetup);
    addEveryOperand();
    SecondMI->addOperand(*MF, MachineOperand::CreateImm(2));
    return true;
  }

  void addEveryOperand() {
    ConstantInt *Integer = ConstantInt::get(Type::getInt128Ty(Context), 0x1234);
    ConstantFP *Floating =
        cast<ConstantFP>(ConstantFP::get(Type::getDoubleTy(Context), 1.5));
    const TargetRegisterInfo *TRI = MF->getSubtarget().getRegisterInfo();
    RegisterMask.assign(MachineOperand::getRegMaskSize(TRI->getNumRegs()),
                        ~UINT32_C(0));
    ShuffleMask = {0, 1, -1};
    MCSymbolValue = MMI->getContext().createTempSymbol("mir_core");

    FirstMI->addOperand(*MF, MachineOperand::CreateReg(MCRegister(1), false));
    FirstMI->addOperand(*MF, MachineOperand::CreateImm(7));
    FirstMI->addOperand(*MF, MachineOperand::CreateCImm(Integer));
    FirstMI->addOperand(*MF, MachineOperand::CreateFPImm(Floating));
    FirstMI->addOperand(*MF, MachineOperand::CreateMBB(Second));
    FirstMI->addOperand(*MF, MachineOperand::CreateFI(-1));
    FirstMI->addOperand(*MF, MachineOperand::CreateCPI(3, 4));
    FirstMI->addOperand(*MF, MachineOperand::CreateTargetIndex(5, 6));
    FirstMI->addOperand(*MF, MachineOperand::CreateJTI(7));
    FirstMI->addOperand(*MF, MachineOperand::CreateES("external"));
    FirstMI->addOperand(*MF, MachineOperand::CreateGA(Global, 8));
    FirstMI->addOperand(*MF, MachineOperand::CreateBA(BlockRef, 9));
    FirstMI->addOperand(*MF,
                        MachineOperand::CreateRegMask(RegisterMask.data()));
    FirstMI->addOperand(*MF,
                        MachineOperand::CreateRegLiveOut(RegisterMask.data()));
    FirstMI->addOperand(*MF, MachineOperand::CreateMetadata(Metadata));
    FirstMI->addOperand(*MF, MachineOperand::CreateMCSymbol(MCSymbolValue));
    FirstMI->addOperand(*MF, MachineOperand::CreateCFIIndex(10));
    FirstMI->addOperand(
        *MF, MachineOperand::CreateIntrinsicID(Intrinsic::not_intrinsic));
    FirstMI->addOperand(*MF, MachineOperand::CreatePredicate(11));
    FirstMI->addOperand(*MF, MachineOperand::CreateShuffleMask(ShuffleMask));
    FirstMI->addOperand(*MF, MachineOperand::CreateDbgInstrRef(12, 13));
  }

  LLVMContext Context;
  std::string TripleName;
  std::unique_ptr<TargetMachine> TargetMachineOwner;
  std::unique_ptr<Module> IRModule;
  Function *IRFunction = nullptr;
  BasicBlock *IRBlock = nullptr;
  GlobalVariable *Global = nullptr;
  BlockAddress *BlockRef = nullptr;
  MDNode *Metadata = nullptr;
  std::unique_ptr<MachineModuleInfo> MMI;
  MachineFunction *MF = nullptr;
  MachineBasicBlock *First = nullptr;
  MachineBasicBlock *Second = nullptr;
  MachineInstr *FirstMI = nullptr;
  MachineInstr *SecondMI = nullptr;
  MCSymbol *MCSymbolValue = nullptr;
  std::vector<uint32_t> RegisterMask;
  std::vector<int> ShuffleMask;
};

struct CoreTestState {
  MIRTaskScope Scope;
  MIRCoreFixture Machine;

  bool initialize() { return Scope.initialize() && Machine.initialize(); }
};

TEST(PluginMIRCoreTest, TraversesBlocksInstructionsCFGAndLiveIns) {
  CoreTestState State;
  ASSERT_TRUE(State.initialize());
  MIRPluginBridge Bridge(State.Scope.task(), *State.Machine.MF, 23);
  const NevercMIRAPI &API = Bridge.api();
  auto Function = Bridge.machineFunction();
  ASSERT_TRUE(static_cast<bool>(Function));

  uint64_t Count = 0;
  EXPECT_EQ(API.GetBasicBlockCount(API.Context, State.Scope.task().handle(),
                                   *Function, &Count)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Count, 2U);
  NevercMachineBasicBlockHandle First{};
  NevercMachineBasicBlockHandle Last{};
  ASSERT_EQ(API.GetFirstBasicBlock(API.Context, State.Scope.task().handle(),
                                   *Function, &First)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.GetLastBasicBlock(API.Context, State.Scope.task().handle(),
                                  *Function, &Last)
                .Code,
            NEVERC_STATUS_OK);
  NevercMachineBasicBlockHandle Next{};
  EXPECT_EQ(API.GetNextBasicBlock(API.Context, State.Scope.task().handle(),
                                  First, &Next)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Next.Value, Last.Value);
  NevercMachineBasicBlockHandle Previous{};
  EXPECT_EQ(API.GetPreviousBasicBlock(API.Context, State.Scope.task().handle(),
                                      Last, &Previous)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Previous.Value, First.Value);

  NevercMachineBasicBlockHandle Blocks[2]{};
  EXPECT_EQ(API.CollectBasicBlocks(API.Context, State.Scope.task().handle(),
                                   *Function, Blocks, 2, &Count)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Count, 2U);

  uint64_t Successors = 0;
  ASSERT_EQ(API.GetSuccessorCount(API.Context, State.Scope.task().handle(),
                                  First, &Successors)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(Successors, 1U);
  NevercMIRCFGEdge Edge{};
  ASSERT_EQ(API.GetSuccessor(API.Context, State.Scope.task().handle(), First, 0,
                             &Edge)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Edge.Block.Value, Last.Value);
  EXPECT_EQ(Edge.ProbabilityNumerator, Edge.ProbabilityDenominator);

  uint64_t LiveIns = 0;
  ASSERT_EQ(API.GetLiveInCount(API.Context, State.Scope.task().handle(), First,
                               &LiveIns)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(LiveIns, 1U);
  NevercMIRLiveIn LiveIn{};
  ASSERT_EQ(
      API.GetLiveIn(API.Context, State.Scope.task().handle(), First, 0, &LiveIn)
          .Code,
      NEVERC_STATUS_OK);
  EXPECT_EQ(LiveIn.Register, 1U);
}

TEST(PluginMIRCoreTest, ReportsInstructionAndEveryOperandKind) {
  CoreTestState State;
  ASSERT_TRUE(State.initialize());
  MIRPluginBridge Bridge(State.Scope.task(), *State.Machine.MF);
  const NevercMIRAPI &API = Bridge.api();
  auto Instruction = Bridge.wrapInstruction(*State.Machine.FirstMI);
  ASSERT_TRUE(static_cast<bool>(Instruction));

  NevercMIRInstructionInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR, 0};
  ASSERT_EQ(API.GetInstructionInfo(API.Context, State.Scope.task().handle(),
                                   *Instruction, &Info)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Info.StableOpcode, NEVERC_MIR_GENERIC_OPCODE_IMPLICIT_DEF);
  EXPECT_EQ(Info.RequiresTargetSchema, NEVERC_FALSE);
  EXPECT_NE(Info.Flags & NEVERC_MIR_INSTR_FLAG_FRAME_SETUP, 0U);
  EXPECT_EQ(Info.OperandCount, NEVERC_MIR_OPERAND_COUNT);

  for (uint64_t I = 0; I != NEVERC_MIR_OPERAND_COUNT; ++I) {
    NevercMachineOperandHandle Operand{};
    ASSERT_EQ(API.GetInstructionOperand(API.Context,
                                        State.Scope.task().handle(),
                                        *Instruction, I, &Operand)
                  .Code,
              NEVERC_STATUS_OK)
        << I;
    NevercMIROperandValue Value{};
    Value.Header = {sizeof(Value), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR,
                    0};
    ASSERT_EQ(API.GetOperandValue(API.Context, State.Scope.task().handle(),
                                  Operand, &Value)
                  .Code,
              NEVERC_STATUS_OK)
        << I;
    EXPECT_EQ(Value.Kind, UINT32_C(0x52000001) + I) << I;
  }

  NevercMIRDebugLocation Location{};
  Location.Header = {sizeof(Location), NEVERC_MIR_API_MAJOR,
                     NEVERC_MIR_API_MINOR, 0};
  EXPECT_EQ(API.GetInstructionDebugLocation(API.Context,
                                            State.Scope.task().handle(),
                                            *Instruction, &Location)
                .Code,
            NEVERC_STATUS_NOT_FOUND);
}

TEST(PluginMIRCoreTest, RequiresMutationLeaseForOperandSetters) {
  CoreTestState State;
  ASSERT_TRUE(State.initialize());
  MIRPluginBridge Bridge(State.Scope.task(), *State.Machine.MF);
  const NevercMIRAPI &API = Bridge.api();
  auto Function = Bridge.machineFunction();
  auto Instruction = Bridge.wrapInstruction(*State.Machine.FirstMI);
  ASSERT_TRUE(static_cast<bool>(Function));
  ASSERT_TRUE(static_cast<bool>(Instruction));
  NevercMachineOperandHandle Operand{};
  ASSERT_EQ(API.GetInstructionOperand(API.Context, State.Scope.task().handle(),
                                      *Instruction, 1, &Operand)
                .Code,
            NEVERC_STATUS_OK);
  NevercMIROperandValue Value{};
  Value.Header = {sizeof(Value), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR, 0};
  ASSERT_EQ(API.GetOperandValue(API.Context, State.Scope.task().handle(),
                                Operand, &Value)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(Value.Kind, NEVERC_MIR_OPERAND_IMMEDIATE);
  Value.Payload.Immediate = 99;

  EXPECT_EQ(API.SetOperandValue(API.Context, State.Scope.task().handle(), {},
                                Operand, &Value)
                .Code,
            NEVERC_STATUS_POLICY_VIOLATION);
  NevercMIRMutationHandle Mutation{};
  ASSERT_EQ(API.BeginMutation(API.Context, State.Scope.task().handle(),
                              *Function, &Mutation)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.SetOperandValue(API.Context, State.Scope.task().handle(),
                                Mutation, Operand, &Value)
                .Code,
            NEVERC_STATUS_OK);

  Value = {};
  Value.Header = {sizeof(Value), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR, 0};
  ASSERT_EQ(API.GetOperandValue(API.Context, State.Scope.task().handle(),
                                Operand, &Value)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Value.Payload.Immediate, 99);
  ASSERT_EQ(
      API.AbortMutation(API.Context, State.Scope.task().handle(), Mutation)
          .Code,
      NEVERC_STATUS_OK);

  Value = {};
  Value.Header = {sizeof(Value), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR, 0};
  ASSERT_EQ(API.GetOperandValue(API.Context, State.Scope.task().handle(),
                                Operand, &Value)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Value.Payload.Immediate, 7);
}

} // namespace
