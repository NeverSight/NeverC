#include "neverc/Plugin/Host/MIRPluginBridge.h"
#include "neverc/Plugin/Host/PluginDiagnostics.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/CodeGen/LowLevelType.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCSymbol.h"
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

class MIRBuilderTaskScope {
public:
  MIRBuilderTaskScope()
      : Services("neverc-plugin-mir-builder-tests", LLVM_VERSION_MAJOR) {}

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

  ~MIRBuilderTaskScope() {
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

class MIRBuilderMachine {
public:
  bool initialize(unsigned BlockCount) {
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
    IRModule = std::make_unique<Module>("mir-builder", Context);
    IRModule->setTargetTriple(TripleName);
    IRModule->setDataLayout(TargetMachine->createDataLayout());
    FunctionType *Type = FunctionType::get(Type::getVoidTy(Context), false);
    IRFunction = Function::Create(Type, GlobalValue::ExternalLinkage,
                                  "function", *IRModule);
    IRBlock = BasicBlock::Create(Context, "entry", IRFunction);
    ReturnInst::Create(Context, IRBlock);

    MMI = std::make_unique<MachineModuleInfo>(TargetMachine);
    MF = &MMI->getOrCreateMachineFunction(*IRFunction);
    for (unsigned I = 0; I != BlockCount; ++I) {
      MachineBasicBlock *Block =
          MF->CreateMachineBasicBlock(I == 0 ? IRBlock : nullptr);
      MF->push_back(Block);
      Blocks.push_back(Block);
    }
    return true;
  }

  LLVMContext Context;
  std::string TripleName;
  std::unique_ptr<TargetMachine> TargetMachineOwner;
  std::unique_ptr<Module> IRModule;
  Function *IRFunction = nullptr;
  BasicBlock *IRBlock = nullptr;
  std::unique_ptr<MachineModuleInfo> MMI;
  MachineFunction *MF = nullptr;
  std::vector<MachineBasicBlock *> Blocks;
};

struct BuilderTestState {
  MIRBuilderTaskScope Scope;
  MIRBuilderMachine Machine;

  bool initialize(unsigned BlockCount) {
    return Scope.initialize() && Machine.initialize(BlockCount);
  }
};

NevercMIRInstructionOpcode genericOpcode(NevercMIRGenericOpcode Opcode) {
  NevercMIRInstructionOpcode Result{};
  Result.StableOpcode = Opcode;
  return Result;
}

TEST(PluginMIRBuilderTest, ExposesCompleteBuilderAndTransactionAPI) {
  BuilderTestState State;
  ASSERT_TRUE(State.initialize(0));
  MIRPluginBridge Bridge(State.Scope.task(), *State.Machine.MF);
  const NevercMIRAPI &API = Bridge.api();

  EXPECT_NE(API.CommitMutation, nullptr);
  EXPECT_NE(API.AbortMutation, nullptr);
  EXPECT_NE(API.CreateBasicBlock, nullptr);
  EXPECT_NE(API.MoveBasicBlock, nullptr);
  EXPECT_NE(API.EraseBasicBlock, nullptr);
  EXPECT_NE(API.CreateInstruction, nullptr);
  EXPECT_NE(API.MoveInstruction, nullptr);
  EXPECT_NE(API.EraseInstruction, nullptr);
  EXPECT_NE(API.SetInstructionFlags, nullptr);
  EXPECT_NE(API.AppendOperand, nullptr);
  EXPECT_NE(API.AddCFGEdge, nullptr);
  EXPECT_NE(API.RemoveCFGEdge, nullptr);
}

TEST(PluginMIRBuilderTest, AbortRestoresBlocksInstructionsOperandsAndCFG) {
  BuilderTestState State;
  ASSERT_TRUE(State.initialize(2));
  using Property = MachineFunctionProperties::Property;
  bool WasSSA = State.Machine.MF->getProperties().hasProperty(Property::IsSSA);
  bool TrackedLiveness =
      State.Machine.MF->getProperties().hasProperty(Property::TracksLiveness);
  MIRPluginBridge Bridge(State.Scope.task(), *State.Machine.MF);
  const NevercMIRAPI &API = Bridge.api();
  auto Function = Bridge.machineFunction();
  auto First = Bridge.wrapBasicBlock(*State.Machine.Blocks[0]);
  auto Second = Bridge.wrapBasicBlock(*State.Machine.Blocks[1]);
  ASSERT_TRUE(Function && First && Second);

  NevercMIRMutationHandle Mutation{};
  ASSERT_EQ(API.BeginMutation(API.Context, State.Scope.task().handle(),
                              *Function, &Mutation)
                .Code,
            NEVERC_STATUS_OK);

  NevercMachineBasicBlockHandle CreatedBlock{};
  ASSERT_EQ(API.CreateBasicBlock(API.Context, State.Scope.task().handle(),
                                 Mutation, *Second, &CreatedBlock)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.MoveBasicBlock(API.Context, State.Scope.task().handle(),
                               Mutation, CreatedBlock, {})
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.AddCFGEdge(API.Context, State.Scope.task().handle(), Mutation,
                           *First, CreatedBlock, 1, 1)
                .Code,
            NEVERC_STATUS_OK);

  NevercMachineInstrHandle CreatedInstruction{};
  ASSERT_EQ(API.CreateInstruction(API.Context, State.Scope.task().handle(),
                                  Mutation, CreatedBlock, {},
                                  genericOpcode(NEVERC_MIR_GENERIC_OPCODE_KILL),
                                  &CreatedInstruction)
                .Code,
            NEVERC_STATUS_OK);
  NevercMIROperandValue Immediate{};
  Immediate.Header = {sizeof(Immediate), NEVERC_MIR_API_MAJOR,
                      NEVERC_MIR_API_MINOR, 0};
  Immediate.Kind = NEVERC_MIR_OPERAND_IMMEDIATE;
  Immediate.Payload.Immediate = 7;
  NevercMachineOperandHandle CreatedOperand{};
  ASSERT_EQ(API.AppendOperand(API.Context, State.Scope.task().handle(),
                              Mutation, CreatedInstruction, &Immediate,
                              &CreatedOperand)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.MoveInstruction(API.Context, State.Scope.task().handle(),
                                Mutation, CreatedInstruction, *Second, {})
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.RemoveCFGEdge(API.Context, State.Scope.task().handle(),
                              Mutation, *First, CreatedBlock)
                .Code,
            NEVERC_STATUS_OK);

  ASSERT_EQ(
      API.AbortMutation(API.Context, State.Scope.task().handle(), Mutation)
          .Code,
      NEVERC_STATUS_OK);
  EXPECT_EQ(State.Machine.MF->size(), 2U);
  EXPECT_EQ(&State.Machine.MF->front(), State.Machine.Blocks[0]);
  EXPECT_EQ(&State.Machine.MF->back(), State.Machine.Blocks[1]);
  EXPECT_TRUE(State.Machine.Blocks[0]->succ_empty());
  EXPECT_TRUE(State.Machine.Blocks[0]->empty());
  EXPECT_TRUE(State.Machine.Blocks[1]->empty());
  EXPECT_EQ(State.Machine.MF->getProperties().hasProperty(Property::IsSSA),
            WasSSA);
  EXPECT_EQ(
      State.Machine.MF->getProperties().hasProperty(Property::TracksLiveness),
      TrackedLiveness);

  int64_t BlockNumber = 0;
  EXPECT_EQ(API.GetBasicBlockNumber(API.Context, State.Scope.task().handle(),
                                    CreatedBlock, &BlockNumber)
                .Code,
            NEVERC_STATUS_STALE_HANDLE);
  NevercMIRInstructionInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR, 0};
  EXPECT_EQ(API.GetInstructionInfo(API.Context, State.Scope.task().handle(),
                                   CreatedInstruction, &Info)
                .Code,
            NEVERC_STATUS_STALE_HANDLE);
  NevercMIROperandValue Value{};
  Value.Header = {sizeof(Value), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR, 0};
  EXPECT_EQ(API.GetOperandValue(API.Context, State.Scope.task().handle(),
                                CreatedOperand, &Value)
                .Code,
            NEVERC_STATUS_STALE_HANDLE);
}

TEST(PluginMIRBuilderTest, EndMutationCommitsErasesAndStalesHandles) {
  BuilderTestState State;
  ASSERT_TRUE(State.initialize(2));
  MachineFunction &MF = *State.Machine.MF;
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  MachineInstr *Instruction =
      MF.CreateMachineInstr(TII->get(TargetOpcode::KILL), DebugLoc());
  State.Machine.Blocks[0]->push_back(Instruction);

  MIRPluginBridge Bridge(State.Scope.task(), MF);
  const NevercMIRAPI &API = Bridge.api();
  auto Function = Bridge.machineFunction();
  auto First = Bridge.wrapBasicBlock(*State.Machine.Blocks[0]);
  auto WrappedInstruction = Bridge.wrapInstruction(*Instruction);
  ASSERT_TRUE(Function && First && WrappedInstruction);

  NevercMIRMutationHandle Mutation{};
  ASSERT_EQ(API.BeginMutation(API.Context, State.Scope.task().handle(),
                              *Function, &Mutation)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.EraseInstruction(API.Context, State.Scope.task().handle(),
                                 Mutation, *WrappedInstruction)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.EraseBasicBlock(API.Context, State.Scope.task().handle(),
                                Mutation, *First)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(
      API.EndMutation(API.Context, State.Scope.task().handle(), Mutation).Code,
      NEVERC_STATUS_OK);

  EXPECT_EQ(MF.size(), 1U);
  uint64_t Count = 0;
  EXPECT_EQ(API.GetInstructionCount(API.Context, State.Scope.task().handle(),
                                    *First, &Count)
                .Code,
            NEVERC_STATUS_STALE_HANDLE);
  NevercMIRInstructionInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR, 0};
  EXPECT_EQ(API.GetInstructionInfo(API.Context, State.Scope.task().handle(),
                                   *WrappedInstruction, &Info)
                .Code,
            NEVERC_STATUS_STALE_HANDLE);
}

TEST(PluginMIRBuilderTest, CommitVerifierRejectsAndRollsBackInvalidMIR) {
  BuilderTestState State;
  ASSERT_TRUE(State.initialize(0));
  MachineFunction &MF = *State.Machine.MF;
  ASSERT_TRUE(MF.verify(nullptr, "baseline", false));

  MIRPluginBridge Bridge(State.Scope.task(), MF);
  const NevercMIRAPI &API = Bridge.api();
  auto Function = Bridge.machineFunction();
  ASSERT_TRUE(static_cast<bool>(Function));
  NevercMIRMutationHandle Mutation{};
  ASSERT_EQ(API.BeginMutation(API.Context, State.Scope.task().handle(),
                              *Function, &Mutation)
                .Code,
            NEVERC_STATUS_OK);
  NevercMachineBasicBlockHandle Block{};
  ASSERT_EQ(API.CreateBasicBlock(API.Context, State.Scope.task().handle(),
                                 Mutation, {}, &Block)
                .Code,
            NEVERC_STATUS_OK);
  NevercMachineBasicBlockHandle SecondBlock{};
  ASSERT_EQ(API.CreateBasicBlock(API.Context, State.Scope.task().handle(),
                                 Mutation, {}, &SecondBlock)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.AddCFGEdge(API.Context, State.Scope.task().handle(), Mutation,
                           SecondBlock, Block, 1, 1)
                .Code,
            NEVERC_STATUS_OK);

  EXPECT_EQ(
      API.CommitMutation(API.Context, State.Scope.task().handle(), Mutation)
          .Code,
      NEVERC_STATUS_VERIFICATION_FAILED);
  std::vector<PluginDiagnosticRecord> Diagnostics =
      State.Scope.task().session().diagnostics().takeSorted();
  ASSERT_EQ(Diagnostics.size(), 1U);
  EXPECT_EQ(Diagnostics[0].Severity, NEVERC_DIAGNOSTIC_ERROR);
  EXPECT_EQ(Diagnostics[0].PluginID, "neverc.host.mir");
  EXPECT_EQ(Diagnostics[0].PhaseID, "mir.commit");
  EXPECT_NE(Diagnostics[0].Message.find("verification failed"),
            std::string::npos);
  EXPECT_NE(Diagnostics[0].Message.find("bb.1"), std::string::npos);
  EXPECT_TRUE(MF.empty());
  int64_t BlockNumber = 0;
  EXPECT_EQ(API.GetBasicBlockNumber(API.Context, State.Scope.task().handle(),
                                    Block, &BlockNumber)
                .Code,
            NEVERC_STATUS_STALE_HANDLE);
}

TEST(PluginMIRBuilderTest, CommitPreflightSafelyRejectsMalformedOperands) {
  BuilderTestState State;
  ASSERT_TRUE(State.initialize(0));
  MachineFunction &MF = *State.Machine.MF;
  MIRPluginBridge Bridge(State.Scope.task(), MF, 1, false,
                         "org.neverc.test.mir-builder");
  const NevercMIRAPI &API = Bridge.api();
  auto Function = Bridge.machineFunction();
  ASSERT_TRUE(static_cast<bool>(Function));
  NevercMIRMutationHandle Mutation{};
  ASSERT_EQ(API.BeginMutation(API.Context, State.Scope.task().handle(),
                              *Function, &Mutation)
                .Code,
            NEVERC_STATUS_OK);
  NevercMachineBasicBlockHandle Block{};
  ASSERT_EQ(API.CreateBasicBlock(API.Context, State.Scope.task().handle(),
                                 Mutation, {}, &Block)
                .Code,
            NEVERC_STATUS_OK);
  NevercMachineInstrHandle Instruction{};
  ASSERT_EQ(API.CreateInstruction(API.Context, State.Scope.task().handle(),
                                  Mutation, Block, {},
                                  genericOpcode(NEVERC_MIR_GENERIC_OPCODE_COPY),
                                  &Instruction)
                .Code,
            NEVERC_STATUS_OK);

  EXPECT_EQ(
      API.CommitMutation(API.Context, State.Scope.task().handle(), Mutation)
          .Code,
      NEVERC_STATUS_VERIFICATION_FAILED);
  EXPECT_TRUE(MF.empty());
  std::vector<PluginDiagnosticRecord> Diagnostics =
      State.Scope.task().session().diagnostics().takeSorted();
  ASSERT_EQ(Diagnostics.size(), 1U);
  EXPECT_EQ(Diagnostics[0].PluginID, "org.neverc.test.mir-builder");
  EXPECT_NE(Diagnostics[0].Message.find("too few operands"), std::string::npos);
  EXPECT_NE(Diagnostics[0].Message.find("COPY"), std::string::npos);

  Mutation = {};
  Block = {};
  Instruction = {};
  ASSERT_EQ(API.BeginMutation(API.Context, State.Scope.task().handle(),
                              *Function, &Mutation)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.CreateBasicBlock(API.Context, State.Scope.task().handle(),
                                 Mutation, {}, &Block)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.CreateInstruction(API.Context, State.Scope.task().handle(),
                                  Mutation, Block, {},
                                  genericOpcode(NEVERC_MIR_GENERIC_OPCODE_COPY),
                                  &Instruction)
                .Code,
            NEVERC_STATUS_OK);
  NevercMIROperandValue Immediate{};
  Immediate.Header = {sizeof(Immediate), NEVERC_MIR_API_MAJOR,
                      NEVERC_MIR_API_MINOR, 0};
  Immediate.Kind = NEVERC_MIR_OPERAND_IMMEDIATE;
  NevercMachineOperandHandle Operand{};
  ASSERT_EQ(API.AppendOperand(API.Context, State.Scope.task().handle(),
                              Mutation, Instruction, &Immediate, &Operand)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.AppendOperand(API.Context, State.Scope.task().handle(),
                              Mutation, Instruction, &Immediate, &Operand)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(
      API.CommitMutation(API.Context, State.Scope.task().handle(), Mutation)
          .Code,
      NEVERC_STATUS_VERIFICATION_FAILED);
  EXPECT_TRUE(MF.empty());
  Diagnostics = State.Scope.task().session().diagnostics().takeSorted();
  ASSERT_EQ(Diagnostics.size(), 1U);
  EXPECT_NE(Diagnostics[0].Message.find("definition is not a register"),
            std::string::npos);
}

TEST(PluginMIRBuilderTest, CommitVerifierAcceptsValidGenericMIR) {
  BuilderTestState State;
  ASSERT_TRUE(State.initialize(0));
  MachineFunction &MF = *State.Machine.MF;
  using Property = MachineFunctionProperties::Property;
  ASSERT_TRUE(MF.getProperties().hasProperty(Property::IsSSA));
  ASSERT_TRUE(MF.getProperties().hasProperty(Property::TracksLiveness));
  Register SourceRegister =
      MF.getRegInfo().createGenericVirtualRegister(LLT::scalar(32));
  Register DestinationRegister =
      MF.getRegInfo().createGenericVirtualRegister(LLT::scalar(32));

  MIRPluginBridge Bridge(State.Scope.task(), MF, 41);
  const NevercMIRAPI &API = Bridge.api();
  auto Function = Bridge.machineFunction();
  ASSERT_TRUE(static_cast<bool>(Function));
  NevercMIRMutationHandle Mutation{};
  ASSERT_EQ(API.BeginMutation(API.Context, State.Scope.task().handle(),
                              *Function, &Mutation)
                .Code,
            NEVERC_STATUS_OK);
  NevercMachineBasicBlockHandle Block{};
  ASSERT_EQ(API.CreateBasicBlock(API.Context, State.Scope.task().handle(),
                                 Mutation, {}, &Block)
                .Code,
            NEVERC_STATUS_OK);
  NevercMachineInstrHandle Instruction{};
  ASSERT_EQ(API.CreateInstruction(
                   API.Context, State.Scope.task().handle(), Mutation, Block,
                   {}, genericOpcode(NEVERC_MIR_GENERIC_OPCODE_G_IMPLICIT_DEF),
                   &Instruction)
                .Code,
            NEVERC_STATUS_OK);
  NevercMIROperandValue Definition{};
  Definition.Header = {sizeof(Definition), NEVERC_MIR_API_MAJOR,
                       NEVERC_MIR_API_MINOR, 0};
  Definition.Kind = NEVERC_MIR_OPERAND_REGISTER;
  Definition.Payload.Register.Number = SourceRegister.id();
  Definition.Payload.Register.Flags = NEVERC_MIR_REG_FLAG_DEF;
  Definition.Payload.Register.IsPhysical = NEVERC_FALSE;
  Definition.Payload.Register.RequiresTargetSchema = NEVERC_FALSE;
  NevercMachineOperandHandle Operand{};
  ASSERT_EQ(API.AppendOperand(API.Context, State.Scope.task().handle(),
                              Mutation, Instruction, &Definition, &Operand)
                .Code,
            NEVERC_STATUS_OK);
  NevercMachineInstrHandle Copy{};
  ASSERT_EQ(API.CreateInstruction(
                   API.Context, State.Scope.task().handle(), Mutation, Block,
                   {}, genericOpcode(NEVERC_MIR_GENERIC_OPCODE_COPY), &Copy)
                .Code,
            NEVERC_STATUS_OK);
  Definition.Payload.Register.Number = DestinationRegister.id();
  ASSERT_EQ(API.AppendOperand(API.Context, State.Scope.task().handle(),
                              Mutation, Copy, &Definition, &Operand)
                .Code,
            NEVERC_STATUS_OK);
  Definition.Payload.Register.Number = SourceRegister.id();
  Definition.Payload.Register.Flags = 0;
  ASSERT_EQ(API.AppendOperand(API.Context, State.Scope.task().handle(),
                              Mutation, Copy, &Definition, &Operand)
                .Code,
            NEVERC_STATUS_OK);

  ASSERT_EQ(
      API.CommitMutation(API.Context, State.Scope.task().handle(), Mutation)
          .Code,
      NEVERC_STATUS_OK);
  ASSERT_EQ(MF.size(), 1U);
  EXPECT_EQ(MF.front().size(), 2U);
  EXPECT_FALSE(MF.getProperties().hasProperty(Property::IsSSA));
  EXPECT_FALSE(MF.getProperties().hasProperty(Property::TracksLiveness));
  uint64_t Generation = 0;
  ASSERT_EQ(API.GetMachineFunctionGeneration(API.Context,
                                             State.Scope.task().handle(),
                                             *Function, &Generation)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Generation, 42U);
}

TEST(PluginMIRBuilderTest, AppendsEveryPublicOperandKind) {
  BuilderTestState State;
  ASSERT_TRUE(State.initialize(2));
  MachineFunction &MF = *State.Machine.MF;
  LLVMContext &Context = State.Machine.Context;
  auto *Global = new GlobalVariable(
      *State.Machine.IRModule, Type::getInt32Ty(Context), false,
      GlobalValue::ExternalLinkage, nullptr, "mir_builder_global");
  BlockAddress *BlockReference =
      BlockAddress::get(State.Machine.IRFunction, State.Machine.IRBlock);
  MDNode *Metadata = MDNode::get(Context, {});
  ConstantInt *Integer = ConstantInt::get(Type::getInt128Ty(Context), 0x1234);
  ConstantFP *Floating =
      cast<ConstantFP>(ConstantFP::get(Type::getDoubleTy(Context), 1.5));
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  std::vector<uint32_t> RegisterMask(
      MachineOperand::getRegMaskSize(TRI->getNumRegs()), ~UINT32_C(0));
  std::vector<int> ShuffleMask = {0, 1, -1};
  MCSymbol *Symbol =
      State.Machine.MMI->getContext().createTempSymbol("mir_builder");
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  MachineInstr *Source =
      MF.CreateMachineInstr(TII->get(TargetOpcode::IMPLICIT_DEF), DebugLoc());
  MachineInstr *Target =
      MF.CreateMachineInstr(TII->get(TargetOpcode::KILL), DebugLoc());
  State.Machine.Blocks[0]->push_back(Source);
  State.Machine.Blocks[1]->push_back(Target);
  Source->addOperand(MF, MachineOperand::CreateReg(MCRegister(1), false));
  Source->addOperand(MF, MachineOperand::CreateImm(7));
  Source->addOperand(MF, MachineOperand::CreateCImm(Integer));
  Source->addOperand(MF, MachineOperand::CreateFPImm(Floating));
  Source->addOperand(MF, MachineOperand::CreateMBB(State.Machine.Blocks[1]));
  Source->addOperand(MF, MachineOperand::CreateFI(-1));
  Source->addOperand(MF, MachineOperand::CreateCPI(3, 4));
  Source->addOperand(MF, MachineOperand::CreateTargetIndex(5, 6));
  Source->addOperand(MF, MachineOperand::CreateJTI(7));
  Source->addOperand(MF, MachineOperand::CreateES("external"));
  Source->addOperand(MF, MachineOperand::CreateGA(Global, 8));
  Source->addOperand(MF, MachineOperand::CreateBA(BlockReference, 9));
  Source->addOperand(MF, MachineOperand::CreateRegMask(RegisterMask.data()));
  Source->addOperand(MF, MachineOperand::CreateRegLiveOut(RegisterMask.data()));
  Source->addOperand(MF, MachineOperand::CreateMetadata(Metadata));
  Source->addOperand(MF, MachineOperand::CreateMCSymbol(Symbol));
  Source->addOperand(MF, MachineOperand::CreateCFIIndex(10));
  Source->addOperand(
      MF, MachineOperand::CreateIntrinsicID(Intrinsic::not_intrinsic));
  Source->addOperand(MF, MachineOperand::CreatePredicate(11));
  Source->addOperand(MF, MachineOperand::CreateShuffleMask(ShuffleMask));
  Source->addOperand(MF, MachineOperand::CreateDbgInstrRef(12, 13));

  MIRPluginBridge Bridge(State.Scope.task(), MF, 1, true);
  const NevercMIRAPI &API = Bridge.api();
  auto Function = Bridge.machineFunction();
  auto WrappedSource = Bridge.wrapInstruction(*Source);
  auto WrappedTarget = Bridge.wrapInstruction(*Target);
  ASSERT_TRUE(Function && WrappedSource && WrappedTarget);
  NevercMIRMutationHandle Mutation{};
  ASSERT_EQ(API.BeginMutation(API.Context, State.Scope.task().handle(),
                              *Function, &Mutation)
                .Code,
            NEVERC_STATUS_OK);

  for (uint64_t I = 0; I != NEVERC_MIR_OPERAND_COUNT; ++I) {
    NevercMachineOperandHandle SourceOperand{};
    ASSERT_EQ(API.GetInstructionOperand(API.Context,
                                        State.Scope.task().handle(),
                                        *WrappedSource, I, &SourceOperand)
                  .Code,
              NEVERC_STATUS_OK)
        << I;
    NevercMIROperandValue Value{};
    Value.Header = {sizeof(Value), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR,
                    0};
    ASSERT_EQ(API.GetOperandValue(API.Context, State.Scope.task().handle(),
                                  SourceOperand, &Value)
                  .Code,
              NEVERC_STATUS_OK)
        << I;
    NevercMachineOperandHandle Appended{};
    ASSERT_EQ(API.AppendOperand(API.Context, State.Scope.task().handle(),
                                Mutation, *WrappedTarget, &Value, &Appended)
                  .Code,
              NEVERC_STATUS_OK)
        << I;
    Value = {};
    Value.Header = {sizeof(Value), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR,
                    0};
    ASSERT_EQ(API.GetOperandValue(API.Context, State.Scope.task().handle(),
                                  Appended, &Value)
                  .Code,
              NEVERC_STATUS_OK)
        << I;
    EXPECT_EQ(Value.Kind, UINT32_C(0x52000001) + I) << I;
  }
  EXPECT_EQ(Target->getNumOperands(), NEVERC_MIR_OPERAND_COUNT);
  ASSERT_EQ(
      API.AbortMutation(API.Context, State.Scope.task().handle(), Mutation)
          .Code,
      NEVERC_STATUS_OK);
  EXPECT_EQ(Target->getNumOperands(), 0U);
}

TEST(PluginMIRBuilderTest, OperandHandlesSurviveGrowthAndSetterAbort) {
  BuilderTestState State;
  ASSERT_TRUE(State.initialize(1));
  MachineFunction &MF = *State.Machine.MF;
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  MachineInstr *Instruction =
      MF.CreateMachineInstr(TII->get(TargetOpcode::KILL), DebugLoc());
  State.Machine.Blocks[0]->push_back(Instruction);
  Instruction->addOperand(MF, MachineOperand::CreateImm(7));

  MIRPluginBridge Bridge(State.Scope.task(), MF);
  const NevercMIRAPI &API = Bridge.api();
  auto Function = Bridge.machineFunction();
  auto WrappedInstruction = Bridge.wrapInstruction(*Instruction);
  ASSERT_TRUE(Function && WrappedInstruction);
  NevercMachineOperandHandle OriginalOperand{};
  ASSERT_EQ(API.GetInstructionOperand(API.Context, State.Scope.task().handle(),
                                      *WrappedInstruction, 0, &OriginalOperand)
                .Code,
            NEVERC_STATUS_OK);

  NevercMIRMutationHandle Mutation{};
  ASSERT_EQ(API.BeginMutation(API.Context, State.Scope.task().handle(),
                              *Function, &Mutation)
                .Code,
            NEVERC_STATUS_OK);
  NevercMIROperandValue Immediate{};
  Immediate.Header = {sizeof(Immediate), NEVERC_MIR_API_MAJOR,
                      NEVERC_MIR_API_MINOR, 0};
  Immediate.Kind = NEVERC_MIR_OPERAND_IMMEDIATE;
  for (int64_t I = 0; I != 64; ++I) {
    Immediate.Payload.Immediate = I;
    NevercMachineOperandHandle Appended{};
    ASSERT_EQ(API.AppendOperand(API.Context, State.Scope.task().handle(),
                                Mutation, *WrappedInstruction, &Immediate,
                                &Appended)
                  .Code,
              NEVERC_STATUS_OK);
  }

  NevercMIROperandValue OriginalValue{};
  OriginalValue.Header = {sizeof(OriginalValue), NEVERC_MIR_API_MAJOR,
                          NEVERC_MIR_API_MINOR, 0};
  ASSERT_EQ(API.GetOperandValue(API.Context, State.Scope.task().handle(),
                                OriginalOperand, &OriginalValue)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(OriginalValue.Payload.Immediate, 7);
  OriginalValue.Payload.Immediate = 99;
  ASSERT_EQ(API.SetOperandValue(API.Context, State.Scope.task().handle(),
                                Mutation, OriginalOperand, &OriginalValue)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(
      API.AbortMutation(API.Context, State.Scope.task().handle(), Mutation)
          .Code,
      NEVERC_STATUS_OK);

  EXPECT_EQ(Instruction->getNumOperands(), 1U);
  OriginalValue = {};
  OriginalValue.Header = {sizeof(OriginalValue), NEVERC_MIR_API_MAJOR,
                          NEVERC_MIR_API_MINOR, 0};
  ASSERT_EQ(API.GetOperandValue(API.Context, State.Scope.task().handle(),
                                OriginalOperand, &OriginalValue)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(OriginalValue.Payload.Immediate, 7);
}

TEST(PluginMIRBuilderTest, RequiresNegotiationForTargetOpcodes) {
  BuilderTestState State;
  ASSERT_TRUE(State.initialize(1));
  MIRPluginBridge Bridge(State.Scope.task(), *State.Machine.MF);
  const NevercMIRAPI &API = Bridge.api();
  auto Function = Bridge.machineFunction();
  auto Block = Bridge.wrapBasicBlock(*State.Machine.Blocks[0]);
  ASSERT_TRUE(Function && Block);

  NevercMIRMutationHandle Mutation{};
  ASSERT_EQ(API.BeginMutation(API.Context, State.Scope.task().handle(),
                              *Function, &Mutation)
                .Code,
            NEVERC_STATUS_OK);
  NevercMIRInstructionOpcode TargetOpcode{};
  TargetOpcode.TargetOpcode = llvm::TargetOpcode::COPY;
  TargetOpcode.RequiresTargetSchema = NEVERC_TRUE;
  NevercMachineInstrHandle Instruction{};
  EXPECT_EQ(API.CreateInstruction(API.Context, State.Scope.task().handle(),
                                  Mutation, *Block, {}, TargetOpcode,
                                  &Instruction)
                .Code,
            NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  EXPECT_EQ(
      API.AbortMutation(API.Context, State.Scope.task().handle(), Mutation)
          .Code,
      NEVERC_STATUS_OK);

  MIRPluginBridge EnabledBridge(State.Scope.task(), *State.Machine.MF, 1, true);
  const NevercMIRAPI &EnabledAPI = EnabledBridge.api();
  auto EnabledFunction = EnabledBridge.machineFunction();
  auto EnabledBlock = EnabledBridge.wrapBasicBlock(*State.Machine.Blocks[0]);
  ASSERT_TRUE(EnabledFunction && EnabledBlock);
  Mutation = {};
  ASSERT_EQ(EnabledAPI
                .BeginMutation(EnabledAPI.Context, State.Scope.task().handle(),
                               *EnabledFunction, &Mutation)
                .Code,
            NEVERC_STATUS_OK);
  const TargetInstrInfo *TII = State.Machine.MF->getSubtarget().getInstrInfo();
  unsigned TargetSpecificOpcode = 0;
  for (unsigned I = 0; I != TII->getNumOpcodes(); ++I) {
    if (!isTargetSpecificOpcode(I))
      continue;
    TargetSpecificOpcode = I;
    break;
  }
  ASSERT_NE(TargetSpecificOpcode, 0U);
  TargetOpcode.TargetOpcode = TargetSpecificOpcode;
  ASSERT_EQ(EnabledAPI
                .CreateInstruction(
                    EnabledAPI.Context, State.Scope.task().handle(), Mutation,
                    *EnabledBlock, {}, TargetOpcode, &Instruction)
                .Code,
            NEVERC_STATUS_OK);
  TargetOpcode.TargetOpcode = UINT32_MAX;
  EXPECT_EQ(EnabledAPI
                .CreateInstruction(
                    EnabledAPI.Context, State.Scope.task().handle(), Mutation,
                    *EnabledBlock, {}, TargetOpcode, &Instruction)
                .Code,
            NEVERC_STATUS_NOT_FOUND);
  EXPECT_EQ(EnabledAPI
                .AbortMutation(EnabledAPI.Context, State.Scope.task().handle(),
                               Mutation)
                .Code,
            NEVERC_STATUS_OK);
}

} // namespace
