#include "neverc/Plugin/Host/MIRPluginBridge.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/BasicBlock.h"
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

using namespace llvm;
using namespace neverc::plugin;

namespace {

std::string errorMessage(Error E) { return toString(std::move(E)).str().str(); }

class MIRStateTaskScope {
public:
  MIRStateTaskScope()
      : Services("neverc-plugin-mir-state-tests", LLVM_VERSION_MAJOR) {}

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

  ~MIRStateTaskScope() {
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

class MIRStateMachine {
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
    IRModule = std::make_unique<Module>("mir-state", Context);
    IRModule->setTargetTriple(TripleName);
    IRModule->setDataLayout(TargetMachine->createDataLayout());
    FunctionType *Type = FunctionType::get(Type::getVoidTy(Context), false);
    IRFunction = Function::Create(Type, GlobalValue::ExternalLinkage,
                                  "function", *IRModule);
    IRBlock = BasicBlock::Create(Context, "entry", IRFunction);
    ReturnInst::Create(Context, IRBlock);

    MMI = std::make_unique<MachineModuleInfo>(TargetMachine);
    MF = &MMI->getOrCreateMachineFunction(*IRFunction);
    Block = MF->CreateMachineBasicBlock(IRBlock);
    MF->push_back(Block);
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
  MachineBasicBlock *Block = nullptr;
};

struct MIRStateTestState {
  MIRStateTaskScope Scope;
  MIRStateMachine Machine;

  bool initialize() { return Scope.initialize() && Machine.initialize(); }
};

TEST(PluginMIRStateTest,
     GenericVirtualRegisterCreationIsObservableAndTransactionallyReversible) {
  MIRStateTestState State;
  ASSERT_TRUE(State.initialize());
  MIRPluginBridge Bridge(State.Scope.task(), *State.Machine.MF);
  const NevercMIRAPI &API = Bridge.api();
  ASSERT_NE(API.CreateVirtualRegister, nullptr);
  ASSERT_NE(API.GetRegisterInfo, nullptr);

  auto Function = Bridge.machineFunction();
  ASSERT_TRUE(static_cast<bool>(Function));
  NevercMIRMutationHandle Mutation{};
  ASSERT_EQ(API.BeginMutation(API.Context, State.Scope.task().handle(),
                              *Function, &Mutation)
                .Code,
            NEVERC_STATUS_OK);

  NevercMIRVirtualRegisterDesc Desc{};
  Desc.Header = {sizeof(Desc), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR, 0};
  Desc.AssignmentKind = NEVERC_MIR_REG_ASSIGNMENT_GENERIC;
  Desc.Type.Kind = NEVERC_MIR_LLT_SCALAR;
  Desc.Type.ScalarSizeInBits = 32;
  uint32_t Register = 0;
  ASSERT_EQ(API.CreateVirtualRegister(API.Context, State.Scope.task().handle(),
                                      Mutation, &Desc, &Register)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_NE(Register, 0U);

  NevercMIRRegisterInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR, 0};
  ASSERT_EQ(API.GetRegisterInfo(API.Context, State.Scope.task().handle(),
                                *Function, Register, &Info)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Info.AssignmentKind, NEVERC_MIR_REG_ASSIGNMENT_GENERIC);
  EXPECT_EQ(Info.Type.Kind, NEVERC_MIR_LLT_SCALAR);
  EXPECT_EQ(Info.Type.ScalarSizeInBits, 32U);

  ASSERT_EQ(
      API.AbortMutation(API.Context, State.Scope.task().handle(), Mutation)
          .Code,
      NEVERC_STATUS_OK);
  Info.Header = {sizeof(Info), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR, 0};
  EXPECT_EQ(API.GetRegisterInfo(API.Context, State.Scope.task().handle(),
                                *Function, Register, &Info)
                .Code,
            NEVERC_STATUS_INVALID_ARGUMENT);
}

TEST(PluginMIRStateTest,
     RegisterDefUseQueriesAndReplacementFollowTransactionalOperands) {
  MIRStateTestState State;
  ASSERT_TRUE(State.initialize());
  MIRPluginBridge Bridge(State.Scope.task(), *State.Machine.MF);
  const NevercMIRAPI &API = Bridge.api();
  auto Function = Bridge.machineFunction();
  auto Block = Bridge.wrapBasicBlock(*State.Machine.Block);
  ASSERT_TRUE(Function && Block);
  NevercMIRMutationHandle Mutation{};
  ASSERT_EQ(API.BeginMutation(API.Context, State.Scope.task().handle(),
                              *Function, &Mutation)
                .Code,
            NEVERC_STATUS_OK);

  NevercMIRVirtualRegisterDesc Desc{};
  Desc.Header = {sizeof(Desc), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR, 0};
  Desc.AssignmentKind = NEVERC_MIR_REG_ASSIGNMENT_GENERIC;
  Desc.Type.Kind = NEVERC_MIR_LLT_SCALAR;
  Desc.Type.ScalarSizeInBits = 32;
  uint32_t From = 0;
  uint32_t To = 0;
  ASSERT_EQ(API.CreateVirtualRegister(API.Context, State.Scope.task().handle(),
                                      Mutation, &Desc, &From)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.CreateVirtualRegister(API.Context, State.Scope.task().handle(),
                                      Mutation, &Desc, &To)
                .Code,
            NEVERC_STATUS_OK);

  NevercMIRInstructionOpcode Opcode{};
  Opcode.StableOpcode = NEVERC_MIR_GENERIC_OPCODE_KILL;
  NevercMachineInstrHandle Instruction{};
  ASSERT_EQ(API.CreateInstruction(API.Context, State.Scope.task().handle(),
                                  Mutation, *Block, {}, Opcode, &Instruction)
                .Code,
            NEVERC_STATUS_OK);
  NevercMIROperandValue RegisterOperand{};
  RegisterOperand.Header = {sizeof(RegisterOperand), NEVERC_MIR_API_MAJOR,
                            NEVERC_MIR_API_MINOR, 0};
  RegisterOperand.Kind = NEVERC_MIR_OPERAND_REGISTER;
  RegisterOperand.Payload.Register.Number = From;
  RegisterOperand.Payload.Register.Flags = NEVERC_MIR_REG_FLAG_DEF;
  NevercMachineOperandHandle Operand{};
  ASSERT_EQ(API.AppendOperand(API.Context, State.Scope.task().handle(), Mutation,
                              Instruction, &RegisterOperand, &Operand)
                .Code,
            NEVERC_STATUS_OK);

  uint64_t Count = 0;
  ASSERT_EQ(API.GetRegisterDefCount(API.Context, State.Scope.task().handle(),
                                    *Function, From, &Count)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Count, 1U);
  NevercMachineOperandHandle Queried{};
  ASSERT_EQ(API.GetRegisterDef(API.Context, State.Scope.task().handle(),
                               *Function, From, 0, &Queried)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Queried.Owner, Operand.Owner);
  EXPECT_EQ(Queried.Value, Operand.Value);

  ASSERT_EQ(API.ReplaceRegister(API.Context, State.Scope.task().handle(),
                                Mutation, From, To)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.GetRegisterDefCount(API.Context, State.Scope.task().handle(),
                                    *Function, From, &Count)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Count, 0U);
  ASSERT_EQ(API.GetRegisterDefCount(API.Context, State.Scope.task().handle(),
                                    *Function, To, &Count)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Count, 1U);

  Desc.Type.ScalarSizeInBits = 64;
  ASSERT_EQ(API.SetVirtualRegisterAssignment(
                    API.Context, State.Scope.task().handle(), Mutation, To,
                    &Desc)
                .Code,
            NEVERC_STATUS_OK);
  NevercMIRRegisterInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR, 0};
  ASSERT_EQ(API.GetRegisterInfo(API.Context, State.Scope.task().handle(),
                                *Function, To, &Info)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Info.Type.ScalarSizeInBits, 64U);

  ASSERT_EQ(
      API.AbortMutation(API.Context, State.Scope.task().handle(), Mutation)
          .Code,
      NEVERC_STATUS_OK);
}

TEST(PluginMIRStateTest,
     FrameObjectsCanBeCreatedUpdatedQueriedAndTransactionallyReverted) {
  MIRStateTestState State;
  ASSERT_TRUE(State.initialize());
  MIRPluginBridge Bridge(State.Scope.task(), *State.Machine.MF);
  const NevercMIRAPI &API = Bridge.api();
  ASSERT_NE(API.GetFrameObjectCount, nullptr);
  ASSERT_NE(API.GetFrameObject, nullptr);
  ASSERT_NE(API.GetFrameObjectByIndex, nullptr);
  ASSERT_NE(API.CreateStackObject, nullptr);
  ASSERT_NE(API.CreateFixedStackObject, nullptr);
  ASSERT_NE(API.CreateVariableSizedStackObject, nullptr);
  ASSERT_NE(API.SetFrameObjectSize, nullptr);
  ASSERT_NE(API.SetFrameObjectAlignment, nullptr);
  ASSERT_NE(API.SetFrameObjectOffset, nullptr);

  auto Function = Bridge.machineFunction();
  ASSERT_TRUE(static_cast<bool>(Function));
  NevercMIRMutationHandle Mutation{};
  ASSERT_EQ(API.BeginMutation(API.Context, State.Scope.task().handle(),
                              *Function, &Mutation)
                .Code,
            NEVERC_STATUS_OK);

  int32_t Fixed = 0;
  ASSERT_EQ(API.CreateFixedStackObject(
                    API.Context, State.Scope.task().handle(), Mutation, 8, -16,
                    NEVERC_TRUE, NEVERC_FALSE, &Fixed)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_LT(Fixed, 0);
  int32_t Regular = -1;
  ASSERT_EQ(API.CreateStackObject(API.Context, State.Scope.task().handle(),
                                  Mutation, 16, 8, NEVERC_FALSE, 0, &Regular)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_GE(Regular, 0);
  int32_t Variable = -1;
  ASSERT_EQ(API.CreateVariableSizedStackObject(
                    API.Context, State.Scope.task().handle(), Mutation, 16,
                    &Variable)
                .Code,
            NEVERC_STATUS_OK);

  uint64_t Count = 0;
  ASSERT_EQ(API.GetFrameObjectCount(API.Context, State.Scope.task().handle(),
                                    *Function, &Count)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Count, 3U);

  NevercMIRFrameObjectInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR, 0};
  ASSERT_EQ(API.GetFrameObjectByIndex(API.Context, State.Scope.task().handle(),
                                      *Function, Fixed, &Info)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_NE(Info.Flags & NEVERC_MIR_FRAME_FIXED, 0U);
  EXPECT_NE(Info.Flags & NEVERC_MIR_FRAME_IMMUTABLE, 0U);
  EXPECT_EQ(Info.Offset, -16);

  ASSERT_EQ(API.SetFrameObjectSize(API.Context, State.Scope.task().handle(),
                                   Mutation, Regular, 24)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.SetFrameObjectAlignment(API.Context, State.Scope.task().handle(),
                                        Mutation, Regular, 16)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.SetFrameObjectOffset(API.Context, State.Scope.task().handle(),
                                     Mutation, Regular, 32)
                .Code,
            NEVERC_STATUS_OK);
  Info.Header = {sizeof(Info), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR, 0};
  ASSERT_EQ(API.GetFrameObjectByIndex(API.Context, State.Scope.task().handle(),
                                      *Function, Regular, &Info)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Info.Size, 24);
  EXPECT_EQ(Info.Alignment, 16U);
  EXPECT_EQ(Info.Offset, 32);

  Info.Header = {sizeof(Info), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR, 0};
  ASSERT_EQ(API.GetFrameObjectByIndex(API.Context, State.Scope.task().handle(),
                                      *Function, Variable, &Info)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_NE(Info.Flags & NEVERC_MIR_FRAME_VARIABLE_SIZED, 0U);

  ASSERT_EQ(
      API.AbortMutation(API.Context, State.Scope.task().handle(), Mutation)
          .Code,
      NEVERC_STATUS_OK);
  ASSERT_EQ(API.GetFrameObjectCount(API.Context, State.Scope.task().handle(),
                                    *Function, &Count)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Count, 0U);
}

TEST(PluginMIRStateTest,
     CalleeSavedStateUsesTargetSchemaAndRollsBackWithItsFrameMutation) {
  MIRStateTestState State;
  ASSERT_TRUE(State.initialize());
  MIRPluginBridge Bridge(State.Scope.task(), *State.Machine.MF, 1,
                         "test-schema", "test-schema");
  const NevercMIRAPI &API = Bridge.api();
  ASSERT_NE(API.GetCalleeSavedCount, nullptr);
  ASSERT_NE(API.GetCalleeSaved, nullptr);
  ASSERT_NE(API.SetCalleeSaved, nullptr);

  auto Function = Bridge.machineFunction();
  ASSERT_TRUE(static_cast<bool>(Function));
  NevercMIRMutationHandle Mutation{};
  ASSERT_EQ(API.BeginMutation(API.Context, State.Scope.task().handle(),
                              *Function, &Mutation)
                .Code,
            NEVERC_STATUS_OK);
  int32_t FrameIndex = -1;
  ASSERT_EQ(API.CreateStackObject(API.Context, State.Scope.task().handle(),
                                  Mutation, 8, 8, NEVERC_TRUE, 0, &FrameIndex)
                .Code,
            NEVERC_STATUS_OK);

  NevercMIRCalleeSavedInfo Entry{};
  Entry.Register = 1;
  Entry.FrameIndex = FrameIndex;
  Entry.IsRestored = NEVERC_TRUE;
  ASSERT_EQ(API.SetCalleeSaved(API.Context, State.Scope.task().handle(),
                               Mutation, &Entry, 1)
                .Code,
            NEVERC_STATUS_OK);

  uint64_t Count = 0;
  ASSERT_EQ(API.GetCalleeSavedCount(API.Context, State.Scope.task().handle(),
                                    *Function, &Count)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(Count, 1U);
  NevercMIRCalleeSavedInfo Actual{};
  ASSERT_EQ(API.GetCalleeSaved(API.Context, State.Scope.task().handle(),
                               *Function, 0, &Actual)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Actual.Register, Entry.Register);
  EXPECT_EQ(Actual.FrameIndex, Entry.FrameIndex);
  EXPECT_EQ(Actual.IsSpilledToRegister, NEVERC_FALSE);
  EXPECT_EQ(Actual.IsRestored, NEVERC_TRUE);

  ASSERT_EQ(
      API.AbortMutation(API.Context, State.Scope.task().handle(), Mutation)
          .Code,
      NEVERC_STATUS_OK);
  ASSERT_EQ(API.GetCalleeSavedCount(API.Context, State.Scope.task().handle(),
                                    *Function, &Count)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Count, 0U);
}

TEST(PluginMIRStateTest,
     FunctionAndBlockLiveInsCanBeAddedRemovedAndQueried) {
  MIRStateTestState State;
  ASSERT_TRUE(State.initialize());
  MIRPluginBridge Bridge(State.Scope.task(), *State.Machine.MF, 1,
                         "test-schema", "test-schema");
  const NevercMIRAPI &API = Bridge.api();
  auto Function = Bridge.machineFunction();
  auto Block = Bridge.wrapBasicBlock(*State.Machine.Block);
  ASSERT_TRUE(Function && Block);
  NevercMIRMutationHandle Mutation{};
  ASSERT_EQ(API.BeginMutation(API.Context, State.Scope.task().handle(),
                              *Function, &Mutation)
                .Code,
            NEVERC_STATUS_OK);

  ASSERT_EQ(API.AddFunctionLiveIn(API.Context, State.Scope.task().handle(),
                                  Mutation, 1, 0)
                .Code,
            NEVERC_STATUS_OK);
  uint64_t Count = 0;
  ASSERT_EQ(API.GetFunctionLiveInCount(
                    API.Context, State.Scope.task().handle(), *Function, &Count)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(Count, 1U);
  NevercMIRFunctionLiveIn FunctionLiveIn{};
  ASSERT_EQ(API.GetFunctionLiveIn(API.Context, State.Scope.task().handle(),
                                  *Function, 0, &FunctionLiveIn)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(FunctionLiveIn.PhysicalRegister, 1U);
  EXPECT_EQ(FunctionLiveIn.VirtualRegister, 0U);
  ASSERT_EQ(API.RemoveFunctionLiveIn(API.Context, State.Scope.task().handle(),
                                     Mutation, 1)
                .Code,
            NEVERC_STATUS_OK);

  ASSERT_EQ(API.AddBasicBlockLiveIn(API.Context, State.Scope.task().handle(),
                                    Mutation, *Block, 1, 1)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.GetLiveInCount(API.Context, State.Scope.task().handle(), *Block,
                               &Count)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Count, 1U);
  ASSERT_EQ(API.RemoveBasicBlockLiveIn(
                    API.Context, State.Scope.task().handle(), Mutation, *Block,
                    1, 1)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.GetLiveInCount(API.Context, State.Scope.task().handle(), *Block,
                               &Count)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Count, 0U);

  ASSERT_EQ(
      API.AbortMutation(API.Context, State.Scope.task().handle(), Mutation)
          .Code,
      NEVERC_STATUS_OK);
}

TEST(PluginMIRStateTest,
     ConstantPoolEntriesRoundTripAndAbortRestoresTheOriginalPool) {
  MIRStateTestState State;
  ASSERT_TRUE(State.initialize());
  MIRPluginBridge Bridge(State.Scope.task(), *State.Machine.MF);
  const NevercMIRAPI &API = Bridge.api();
  ASSERT_NE(API.GetConstantPoolCount, nullptr);
  ASSERT_NE(API.GetConstantPoolEntry, nullptr);
  ASSERT_NE(API.CreateConstantPoolEntry, nullptr);
  ASSERT_NE(API.RemoveConstantPoolEntry, nullptr);

  auto Function = Bridge.machineFunction();
  ASSERT_TRUE(static_cast<bool>(Function));
  NevercMIRMutationHandle Mutation{};
  ASSERT_EQ(API.BeginMutation(API.Context, State.Scope.task().handle(),
                              *Function, &Mutation)
                .Code,
            NEVERC_STATUS_OK);

  uint64_t Word = UINT64_C(0x12345678);
  NevercMIRConstantPoolEntryDesc Desc{};
  Desc.Header = {sizeof(Desc), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR, 0};
  Desc.Kind = NEVERC_MIR_CONSTANT_INTEGER;
  Desc.Alignment = 4;
  Desc.Value = {&Word, 1, 32, 0};
  uint32_t Index = UINT32_MAX;
  ASSERT_EQ(API.CreateConstantPoolEntry(
                    API.Context, State.Scope.task().handle(), Mutation, &Desc,
                    &Index)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Index, 0U);

  uint64_t Count = 0;
  ASSERT_EQ(API.GetConstantPoolCount(API.Context, State.Scope.task().handle(),
                                     *Function, &Count)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(Count, 1U);
  NevercMIRConstantPoolEntryInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR, 0};
  ASSERT_EQ(API.GetConstantPoolEntry(API.Context, State.Scope.task().handle(),
                                     *Function, Index, &Info)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Info.Kind, NEVERC_MIR_CONSTANT_INTEGER);
  EXPECT_EQ(Info.Alignment, 4U);
  EXPECT_EQ(Info.Size, 4U);
  ASSERT_EQ(Info.Value.Count, 1U);
  ASSERT_NE(Info.Value.Data, nullptr);
  EXPECT_EQ(Info.Value.BitWidth, 32U);
  EXPECT_EQ(Info.Value.Data[0], Word);

  Desc.Alignment = 16;
  uint32_t ReusedIndex = UINT32_MAX;
  ASSERT_EQ(API.CreateConstantPoolEntry(
                    API.Context, State.Scope.task().handle(), Mutation, &Desc,
                    &ReusedIndex)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(ReusedIndex, Index);
  Info.Header = {sizeof(Info), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR, 0};
  ASSERT_EQ(API.GetConstantPoolEntry(API.Context, State.Scope.task().handle(),
                                     *Function, Index, &Info)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Info.Alignment, 16U);

  ASSERT_EQ(API.RemoveConstantPoolEntry(
                    API.Context, State.Scope.task().handle(), Mutation, Index)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.GetConstantPoolCount(API.Context, State.Scope.task().handle(),
                                     *Function, &Count)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Count, 0U);

  ASSERT_EQ(
      API.AbortMutation(API.Context, State.Scope.task().handle(), Mutation)
          .Code,
      NEVERC_STATUS_OK);
  ASSERT_EQ(API.GetConstantPoolCount(API.Context, State.Scope.task().handle(),
                                     *Function, &Count)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Count, 0U);
}

TEST(PluginMIRStateTest, FloatingConstantPoolEntriesPreserveExactBits) {
  MIRStateTestState State;
  ASSERT_TRUE(State.initialize());
  MIRPluginBridge Bridge(State.Scope.task(), *State.Machine.MF);
  const NevercMIRAPI &API = Bridge.api();
  auto Function = Bridge.machineFunction();
  ASSERT_TRUE(static_cast<bool>(Function));
  NevercMIRMutationHandle Mutation{};
  ASSERT_EQ(API.BeginMutation(API.Context, State.Scope.task().handle(),
                              *Function, &Mutation)
                .Code,
            NEVERC_STATUS_OK);

  uint64_t Bits = UINT64_C(0x3ff8000000000000);
  NevercMIRConstantPoolEntryDesc Desc{};
  Desc.Header = {sizeof(Desc), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR, 0};
  Desc.Kind = NEVERC_MIR_CONSTANT_FLOAT;
  Desc.Alignment = 8;
  Desc.Value = {&Bits, 1, 64, NEVERC_MIR_FLOAT_SEMANTICS_IEEE_DOUBLE};
  uint32_t Index = UINT32_MAX;
  ASSERT_EQ(API.CreateConstantPoolEntry(
                    API.Context, State.Scope.task().handle(), Mutation, &Desc,
                    &Index)
                .Code,
            NEVERC_STATUS_OK);

  NevercMIRConstantPoolEntryInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR, 0};
  ASSERT_EQ(API.GetConstantPoolEntry(API.Context, State.Scope.task().handle(),
                                     *Function, Index, &Info)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Info.Kind, NEVERC_MIR_CONSTANT_FLOAT);
  EXPECT_EQ(Info.Value.BitWidth, 64U);
  EXPECT_EQ(Info.Value.Semantics, NEVERC_MIR_FLOAT_SEMANTICS_IEEE_DOUBLE);
  ASSERT_EQ(Info.Value.Count, 1U);
  EXPECT_EQ(Info.Value.Data[0], Bits);

  ASSERT_EQ(
      API.AbortMutation(API.Context, State.Scope.task().handle(), Mutation)
          .Code,
      NEVERC_STATUS_OK);
}

TEST(PluginMIRStateTest,
     JumpTablesExposeDestinationsSupportDeletionAndRollbackAtomically) {
  MIRStateTestState State;
  ASSERT_TRUE(State.initialize());
  MIRPluginBridge Bridge(State.Scope.task(), *State.Machine.MF);
  const NevercMIRAPI &API = Bridge.api();
  ASSERT_NE(API.GetJumpTableCount, nullptr);
  ASSERT_NE(API.GetJumpTable, nullptr);
  ASSERT_NE(API.GetJumpTableDestination, nullptr);
  ASSERT_NE(API.CreateJumpTable, nullptr);
  ASSERT_NE(API.RemoveJumpTable, nullptr);

  auto Function = Bridge.machineFunction();
  auto Block = Bridge.wrapBasicBlock(*State.Machine.Block);
  ASSERT_TRUE(Function && Block);
  NevercMIRMutationHandle Mutation{};
  ASSERT_EQ(API.BeginMutation(API.Context, State.Scope.task().handle(),
                              *Function, &Mutation)
                .Code,
            NEVERC_STATUS_OK);

  NevercMachineBasicBlockHandle Destinations[] = {*Block};
  uint32_t Index = UINT32_MAX;
  ASSERT_EQ(API.CreateJumpTable(
                    API.Context, State.Scope.task().handle(), Mutation,
                    NEVERC_MIR_JT_BLOCK_ADDRESS, Destinations, 1, &Index)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Index, 0U);

  uint64_t Count = 0;
  ASSERT_EQ(API.GetJumpTableCount(API.Context, State.Scope.task().handle(),
                                  *Function, &Count)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(Count, 1U);
  NevercMIRJumpTableInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR, 0};
  ASSERT_EQ(API.GetJumpTable(API.Context, State.Scope.task().handle(),
                             *Function, Index, &Info)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Info.EntryKind, NEVERC_MIR_JT_BLOCK_ADDRESS);
  EXPECT_EQ(Info.DestinationCount, 1U);
  EXPECT_EQ(Info.IsDeleted, NEVERC_FALSE);

  NevercMachineBasicBlockHandle Destination{};
  ASSERT_EQ(API.GetJumpTableDestination(
                    API.Context, State.Scope.task().handle(), *Function, Index,
                    0, &Destination)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Destination.Owner, Block->Owner);
  EXPECT_EQ(Destination.Value, Block->Value);

  ASSERT_EQ(API.RemoveJumpTable(API.Context, State.Scope.task().handle(),
                                Mutation, Index)
                .Code,
            NEVERC_STATUS_OK);
  Info.Header = {sizeof(Info), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR, 0};
  ASSERT_EQ(API.GetJumpTable(API.Context, State.Scope.task().handle(),
                             *Function, Index, &Info)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Info.IsDeleted, NEVERC_TRUE);
  EXPECT_EQ(Info.DestinationCount, 0U);

  ASSERT_EQ(
      API.AbortMutation(API.Context, State.Scope.task().handle(), Mutation)
          .Code,
      NEVERC_STATUS_OK);
  ASSERT_EQ(API.GetJumpTableCount(API.Context, State.Scope.task().handle(),
                                  *Function, &Count)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Count, 0U);
}

TEST(PluginMIRStateTest,
     MemoryOperandsRoundTripThroughInstructionsAndAbortStalesNewHandles) {
  MIRStateTestState State;
  ASSERT_TRUE(State.initialize());
  MIRPluginBridge Bridge(State.Scope.task(), *State.Machine.MF);
  const NevercMIRAPI &API = Bridge.api();
  ASSERT_NE(API.GetInstructionMemoryOperand, nullptr);
  ASSERT_NE(API.GetMemoryOperandInfo, nullptr);
  ASSERT_NE(API.CreateMemoryOperand, nullptr);
  ASSERT_NE(API.AddInstructionMemoryOperand, nullptr);
  ASSERT_NE(API.RemoveInstructionMemoryOperand, nullptr);

  auto Function = Bridge.machineFunction();
  auto Block = Bridge.wrapBasicBlock(*State.Machine.Block);
  ASSERT_TRUE(Function && Block);
  NevercMIRMutationHandle Mutation{};
  ASSERT_EQ(API.BeginMutation(API.Context, State.Scope.task().handle(),
                              *Function, &Mutation)
                .Code,
            NEVERC_STATUS_OK);
  NevercMIRInstructionOpcode Opcode{};
  Opcode.StableOpcode = NEVERC_MIR_GENERIC_OPCODE_KILL;
  NevercMachineInstrHandle Instruction{};
  ASSERT_EQ(API.CreateInstruction(API.Context, State.Scope.task().handle(),
                                  Mutation, *Block, {}, Opcode, &Instruction)
                .Code,
            NEVERC_STATUS_OK);

  NevercMIRMemoryOperandDesc Desc{};
  Desc.Header = {sizeof(Desc), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR, 0};
  Desc.Flags = NEVERC_MIR_MEMORY_LOAD | NEVERC_MIR_MEMORY_VOLATILE;
  Desc.Size = 4;
  Desc.BaseAlignment = 4;
  Desc.Pointer.Kind = NEVERC_MIR_MEMORY_POINTER_UNKNOWN;
  Desc.Pointer.AddressSpace = 0;
  Desc.Pointer.Offset = 8;
  Desc.SuccessOrdering = NEVERC_MIR_ATOMIC_NOT_ATOMIC;
  Desc.FailureOrdering = NEVERC_MIR_ATOMIC_NOT_ATOMIC;
  NevercMachineMemOperandHandle MemoryOperand{};
  ASSERT_EQ(API.CreateMemoryOperand(API.Context, State.Scope.task().handle(),
                                    Mutation, &Desc, &MemoryOperand)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.AddInstructionMemoryOperand(
                    API.Context, State.Scope.task().handle(), Mutation,
                    Instruction, MemoryOperand)
                .Code,
            NEVERC_STATUS_OK);

  NevercMachineMemOperandHandle FromInstruction{};
  ASSERT_EQ(API.GetInstructionMemoryOperand(
                    API.Context, State.Scope.task().handle(), Instruction, 0,
                    &FromInstruction)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(FromInstruction.Owner, MemoryOperand.Owner);
  EXPECT_EQ(FromInstruction.Value, MemoryOperand.Value);

  NevercMIRMemoryOperandInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR, 0};
  ASSERT_EQ(API.GetMemoryOperandInfo(API.Context, State.Scope.task().handle(),
                                     MemoryOperand, &Info)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Info.Flags, Desc.Flags);
  EXPECT_EQ(Info.Size, Desc.Size);
  EXPECT_EQ(Info.BaseAlignment, Desc.BaseAlignment);
  EXPECT_EQ(Info.Alignment, 4U);
  EXPECT_EQ(Info.Pointer.Kind, NEVERC_MIR_MEMORY_POINTER_UNKNOWN);
  EXPECT_EQ(Info.Pointer.Offset, 8);

  ASSERT_EQ(API.RemoveInstructionMemoryOperand(
                    API.Context, State.Scope.task().handle(), Mutation,
                    Instruction, 0)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(API.GetInstructionMemoryOperand(
                    API.Context, State.Scope.task().handle(), Instruction, 0,
                    &FromInstruction)
                .Code,
            NEVERC_STATUS_NOT_FOUND);

  ASSERT_EQ(
      API.AbortMutation(API.Context, State.Scope.task().handle(), Mutation)
          .Code,
      NEVERC_STATUS_OK);
  Info.Header = {sizeof(Info), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR, 0};
  EXPECT_EQ(API.GetMemoryOperandInfo(API.Context, State.Scope.task().handle(),
                                     MemoryOperand, &Info)
                .Code,
            NEVERC_STATUS_STALE_HANDLE);
}

TEST(PluginMIRStateTest,
     AtomicFixedStackMemoryPreservesScopePointerAndAliasMetadata) {
  MIRStateTestState State;
  ASSERT_TRUE(State.initialize());
  MIRPluginBridge Bridge(State.Scope.task(), *State.Machine.MF);
  const NevercMIRAPI &API = Bridge.api();
  auto Function = Bridge.machineFunction();
  ASSERT_TRUE(static_cast<bool>(Function));
  NevercMIRMutationHandle Mutation{};
  ASSERT_EQ(API.BeginMutation(API.Context, State.Scope.task().handle(),
                              *Function, &Mutation)
                .Code,
            NEVERC_STATUS_OK);
  int32_t FrameIndex = 0;
  ASSERT_EQ(API.CreateFixedStackObject(
                    API.Context, State.Scope.task().handle(), Mutation, 8, -8,
                    NEVERC_FALSE, NEVERC_FALSE, &FrameIndex)
                .Code,
            NEVERC_STATUS_OK);

  MDNode *TBAA = MDNode::get(State.Machine.Context, {});
  auto TBAAHandle =
      Bridge.wrapReference(TBAA, NEVERC_MIR_OPERAND_METADATA);
  ASSERT_TRUE(static_cast<bool>(TBAAHandle));
  constexpr char Scope[] = "singlethread";
  NevercMIRMemoryOperandDesc Desc{};
  Desc.Header = {sizeof(Desc), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR, 0};
  Desc.Flags = NEVERC_MIR_MEMORY_LOAD;
  Desc.Size = 8;
  Desc.BaseAlignment = 8;
  Desc.Pointer.Kind = NEVERC_MIR_MEMORY_POINTER_FIXED_STACK;
  Desc.Pointer.FrameIndex = FrameIndex;
  Desc.Pointer.Offset = 3;
  Desc.SuccessOrdering = NEVERC_MIR_ATOMIC_ACQUIRE;
  Desc.FailureOrdering = NEVERC_MIR_ATOMIC_NOT_ATOMIC;
  Desc.SynchronizationScope = {Scope, sizeof(Scope) - 1};
  Desc.TBAA = *TBAAHandle;
  NevercMachineMemOperandHandle MemoryOperand{};
  ASSERT_EQ(API.CreateMemoryOperand(API.Context, State.Scope.task().handle(),
                                    Mutation, &Desc, &MemoryOperand)
                .Code,
            NEVERC_STATUS_OK);

  NevercMIRMemoryOperandInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR, 0};
  ASSERT_EQ(API.GetMemoryOperandInfo(API.Context, State.Scope.task().handle(),
                                     MemoryOperand, &Info)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Info.SuccessOrdering, NEVERC_MIR_ATOMIC_ACQUIRE);
  EXPECT_EQ(Info.FailureOrdering, NEVERC_MIR_ATOMIC_NOT_ATOMIC);
  EXPECT_EQ(StringRef(Info.SynchronizationScope.Data,
                      Info.SynchronizationScope.Length),
            Scope);
  EXPECT_EQ(Info.Pointer.Kind, NEVERC_MIR_MEMORY_POINTER_FIXED_STACK);
  EXPECT_EQ(Info.Pointer.FrameIndex, FrameIndex);
  EXPECT_EQ(Info.Pointer.Offset, 3);
  EXPECT_EQ(Info.TBAA.Owner, TBAAHandle->Owner);
  EXPECT_EQ(Info.TBAA.Value, TBAAHandle->Value);

  ASSERT_EQ(
      API.AbortMutation(API.Context, State.Scope.task().handle(), Mutation)
          .Code,
      NEVERC_STATUS_OK);
}

TEST(PluginMIRStateTest,
     MachinePropertiesRequireProofAndRollbackTransactionally) {
  MIRStateTestState State;
  ASSERT_TRUE(State.initialize());
  MIRPluginBridge Bridge(State.Scope.task(), *State.Machine.MF);
  const NevercMIRAPI &API = Bridge.api();
  auto Function = Bridge.machineFunction();
  ASSERT_TRUE(static_cast<bool>(Function));

  NevercBool Original = NEVERC_FALSE;
  ASSERT_EQ(API.GetMachineProperty(
                    API.Context, State.Scope.task().handle(), *Function,
                    NEVERC_MIR_PROPERTY_NO_PH_IS, &Original)
                .Code,
            NEVERC_STATUS_OK);

  NevercMIRMutationHandle Mutation{};
  ASSERT_EQ(API.BeginMutation(API.Context, State.Scope.task().handle(),
                              *Function, &Mutation)
                .Code,
            NEVERC_STATUS_OK);
  NevercMIRPropertyProof Proof{};
  Proof.Header = {sizeof(Proof), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR, 0};
  Proof.Property = NEVERC_MIR_PROPERTY_NO_PH_IS;
  Proof.Kind = NEVERC_MIR_PROPERTY_PROOF_STRUCTURAL_CHECK;
  Proof.Value = NEVERC_TRUE;
  ASSERT_EQ(API.SetMachinePropertyWithProof(
                    API.Context, State.Scope.task().handle(), Mutation, &Proof)
                .Code,
            NEVERC_STATUS_OK);
  NevercBool Value = NEVERC_FALSE;
  ASSERT_EQ(API.GetMachineProperty(
                    API.Context, State.Scope.task().handle(), *Function,
                    NEVERC_MIR_PROPERTY_NO_PH_IS, &Value)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Value, NEVERC_TRUE);
  ASSERT_EQ(
      API.AbortMutation(API.Context, State.Scope.task().handle(), Mutation)
          .Code,
      NEVERC_STATUS_OK);
  ASSERT_EQ(API.GetMachineProperty(
                    API.Context, State.Scope.task().handle(), *Function,
                    NEVERC_MIR_PROPERTY_NO_PH_IS, &Value)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Value, Original);

  ASSERT_EQ(API.BeginMutation(API.Context, State.Scope.task().handle(),
                              *Function, &Mutation)
                .Code,
            NEVERC_STATUS_OK);
  Proof.Kind = NEVERC_MIR_PROPERTY_PROOF_INVALIDATION;
  Proof.Value = NEVERC_FALSE;
  ASSERT_EQ(API.SetMachinePropertyWithProof(
                    API.Context, State.Scope.task().handle(), Mutation, &Proof)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(
      API.CommitMutation(API.Context, State.Scope.task().handle(), Mutation)
          .Code,
      NEVERC_STATUS_OK);
  ASSERT_EQ(API.GetMachineProperty(
                    API.Context, State.Scope.task().handle(), *Function,
                    NEVERC_MIR_PROPERTY_NO_PH_IS, &Value)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Value, NEVERC_FALSE);
}

} // namespace
