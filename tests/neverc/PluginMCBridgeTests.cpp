#include "neverc/Plugin/Host/MCPluginBridge.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/TargetParser/Host.h"
#include "gtest/gtest.h"
#include <memory>
#include <optional>
#include <string>

using namespace llvm;
using namespace neverc::plugin;

namespace {

std::string errorText(Error ErrorValue) {
  return toString(std::move(ErrorValue)).str().str();
}

class MCTaskScope {
public:
  MCTaskScope()
      : Services("neverc-plugin-mc-bridge-tests",
                 LLVM_VERSION_MAJOR) {}

  bool initialize() {
    if (Error E = Services.interfaces().freeze()) {
      ADD_FAILURE() << errorText(std::move(E));
      return false;
    }
    auto CreatedPlan =
        makePluginActivationPlan(Services.registry(), {});
    if (!CreatedPlan) {
      ADD_FAILURE() << errorText(CreatedPlan.takeError());
      return false;
    }
    Plan.emplace(std::move(*CreatedPlan));
    auto CreatedSession = PluginSession::create(Services, *Plan);
    if (!CreatedSession) {
      ADD_FAILURE() << errorText(CreatedSession.takeError());
      return false;
    }
    Session = std::move(*CreatedSession);
    auto CreatedTask = Session->createTask(NEVERC_TASK_CODEGEN);
    if (!CreatedTask) {
      ADD_FAILURE() << errorText(CreatedTask.takeError());
      return false;
    }
    Task = std::move(*CreatedTask);
    return true;
  }

  ~MCTaskScope() {
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

PluginTargetSnapshot::NamedRecord schema() {
  PluginTargetSnapshot::NamedRecord Schema;
  Schema.CanonicalName = "test.mc-schema";
  Schema.Opcodes = {
      {10, 100, "test.old", 0},
      {11, 101, "test.new", 0},
  };
  Schema.SchemaRegisters = {
      {20, 5, "test.r5", 0},
  };
  return Schema;
}

std::unique_ptr<MCInst> instruction(uint32_t Opcode) {
  auto Result = std::make_unique<MCInst>();
  Result->setOpcode(Opcode);
  return Result;
}

NevercMCOperandValue registerOperand(NevercMCSchemaTokenHandle SchemaToken,
                                     uint32_t Register) {
  NevercMCOperandValue Value{};
  Value.Header = {sizeof(Value), NEVERC_MC_API_MAJOR,
                  NEVERC_MC_API_MINOR, 0};
  Value.Kind = NEVERC_MC_OPERAND_REGISTER;
  Value.SchemaToken = SchemaToken;
  Value.Payload.Register = Register;
  return Value;
}

NevercMCOperandValue immediateOperand(int64_t Immediate) {
  NevercMCOperandValue Value{};
  Value.Header = {sizeof(Value), NEVERC_MC_API_MAJOR,
                  NEVERC_MC_API_MINOR, 0};
  Value.Kind = NEVERC_MC_OPERAND_IMMEDIATE;
  Value.Payload.Immediate = Immediate;
  return Value;
}

TEST(PluginMCBridgeTest, TraversesStableSchemaInstructionsAndOperands) {
  MCTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  PluginMCUnit Unit;
  MCInst &Initial = Unit.append(instruction(100));
  Initial.addOperand(MCOperand::createReg(5));
  Initial.addOperand(MCOperand::createImm(7));
  auto Schema = schema();
  MCPluginBridge Bridge(Scope.task(), Unit, &Schema);
  const NevercMCAPI &API = Bridge.api();
  auto UnitHandle = Bridge.unit();
  ASSERT_TRUE(static_cast<bool>(UnitHandle));

  NevercMCInstHandle First{};
  ASSERT_EQ(API.GetFirstInstruction(
                API.Context, Scope.task().handle(), *UnitHandle, &First)
                .Code,
            NEVERC_STATUS_OK);
  NevercMCInstructionInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_MC_API_MAJOR,
                 NEVERC_MC_API_MINOR, 0};
  ASSERT_EQ(API.GetInstructionInfo(API.Context, Scope.task().handle(),
                                   First, &Info)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Info.Opcode, 10U);
  EXPECT_EQ(Info.OperandCount, 2U);

  NevercMCOperandHandle Operand{};
  ASSERT_EQ(API.GetInstructionOperand(
                API.Context, Scope.task().handle(), First, 0, &Operand)
                .Code,
            NEVERC_STATUS_OK);
  NevercMCOperandValue Value{};
  Value.Header = {sizeof(Value), NEVERC_MC_API_MAJOR,
                  NEVERC_MC_API_MINOR, 0};
  ASSERT_EQ(API.GetOperandValue(API.Context, Scope.task().handle(),
                                Operand, &Value)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Value.Kind, NEVERC_MC_OPERAND_REGISTER);
  EXPECT_EQ(Value.Payload.Register, 20U);
  EXPECT_TRUE(Value.SchemaToken.Owner == Info.SchemaToken.Owner &&
              Value.SchemaToken.Value == Info.SchemaToken.Value);
}

TEST(PluginMCBridgeTest, CommitsConstructedInstructionAtomically) {
  MCTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  PluginMCUnit Unit;
  Unit.append(instruction(100));
  auto Schema = schema();
  MCPluginBridge Bridge(Scope.task(), Unit, &Schema);
  const NevercMCAPI &API = Bridge.api();
  auto UnitHandle = Bridge.unit();
  ASSERT_TRUE(static_cast<bool>(UnitHandle));
  NevercMCSchemaTokenHandle SchemaToken{};
  ASSERT_EQ(API.GetSchemaToken(API.Context, Scope.task().handle(), *UnitHandle,
                               &SchemaToken)
                .Code,
            NEVERC_STATUS_OK);
  NevercMCInstHandle First{};
  ASSERT_EQ(API.GetFirstInstruction(
                API.Context, Scope.task().handle(), *UnitHandle, &First)
                .Code,
            NEVERC_STATUS_OK);
  NevercMCMutationHandle Mutation{};
  ASSERT_EQ(API.BeginMutation(API.Context, Scope.task().handle(),
                              *UnitHandle, &Mutation)
                .Code,
            NEVERC_STATUS_OK);
  NevercMCInstHandle Created{};
  ASSERT_EQ(API.CreateInstruction(API.Context, Scope.task().handle(),
                                  Mutation, SchemaToken, 11, &Created)
                .Code,
            NEVERC_STATUS_OK);
  NevercMCOperandValue Register = registerOperand(SchemaToken, 20);
  NevercMCOperandValue Immediate = immediateOperand(9);
  ASSERT_EQ(API.AppendOperand(API.Context, Scope.task().handle(),
                              Mutation, Created, &Register)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.AppendOperand(API.Context, Scope.task().handle(),
                              Mutation, Created, &Immediate)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.InsertInstructionBefore(
                API.Context, Scope.task().handle(), Mutation, First,
                Created)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.CommitMutation(API.Context, Scope.task().handle(),
                               Mutation)
                .Code,
            NEVERC_STATUS_OK);

  ASSERT_EQ(Unit.size(), 2U);
  ASSERT_NE(Unit.at(0), nullptr);
  EXPECT_EQ(Unit.at(0)->getOpcode(), 101U);
  EXPECT_EQ(Unit.at(0)->getOperand(0).getReg(), 5U);
  EXPECT_EQ(Unit.at(0)->getOperand(1).getImm(), 9);
}

TEST(PluginMCBridgeTest, AbandonAndFailedCommitRestoreOriginalUnit) {
  MCTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  PluginMCUnit Unit;
  Unit.append(instruction(100));
  auto Schema = schema();
  MCPluginBridge Bridge(Scope.task(), Unit, &Schema);
  const NevercMCAPI &API = Bridge.api();
  auto UnitHandle = Bridge.unit();
  ASSERT_TRUE(static_cast<bool>(UnitHandle));
  NevercMCSchemaTokenHandle SchemaToken{};
  ASSERT_EQ(API.GetSchemaToken(API.Context, Scope.task().handle(), *UnitHandle,
                               &SchemaToken)
                .Code,
            NEVERC_STATUS_OK);

  NevercMCMutationHandle Mutation{};
  ASSERT_EQ(API.BeginMutation(API.Context, Scope.task().handle(),
                              *UnitHandle, &Mutation)
                .Code,
            NEVERC_STATUS_OK);
  NevercMCInstHandle Detached{};
  ASSERT_EQ(API.CreateInstruction(API.Context, Scope.task().handle(),
                                  Mutation, SchemaToken, 11, &Detached)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(API.CommitMutation(API.Context, Scope.task().handle(),
                               Mutation)
                .Code,
            NEVERC_STATUS_VERIFICATION_FAILED);
  EXPECT_EQ(Unit.size(), 1U);
  NevercMCInstructionInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_MC_API_MAJOR,
                 NEVERC_MC_API_MINOR, 0};
  EXPECT_EQ(API.GetInstructionInfo(API.Context, Scope.task().handle(),
                                   Detached, &Info)
                .Code,
            NEVERC_STATUS_STALE_HANDLE);

  ASSERT_EQ(API.BeginMutation(API.Context, Scope.task().handle(),
                              *UnitHandle, &Mutation)
                .Code,
            NEVERC_STATUS_OK);
  NevercMCInstHandle First{};
  ASSERT_EQ(API.GetFirstInstruction(
                API.Context, Scope.task().handle(), *UnitHandle, &First)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.EraseInstruction(API.Context, Scope.task().handle(),
                                 Mutation, First)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(Unit.size(), 0U);
  EXPECT_EQ(API.AbandonMutation(API.Context, Scope.task().handle(),
                                Mutation)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Unit.size(), 1U);
}

TEST(PluginMCBridgeTest,
     ReadsConstructsAndEncodesInstructionBytes) {
  static const bool Initialized = [] {
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    return true;
  }();
  (void)Initialized;
  std::string LookupError;
  const std::string TripleName = sys::getDefaultTargetTriple();
  const Target *Target =
      TargetRegistry::lookupTarget(TripleName, LookupError);
  ASSERT_NE(Target, nullptr) << LookupError;
  std::unique_ptr<MCRegisterInfo> Registers(
      Target->createMCRegInfo(TripleName));
  std::unique_ptr<MCInstrInfo> Instructions(
      Target->createMCInstrInfo());
  std::unique_ptr<MCSubtargetInfo> Subtarget(
      Target->createMCSubtargetInfo(TripleName, "generic", ""));
  ASSERT_NE(Registers, nullptr);
  ASSERT_NE(Instructions, nullptr);
  ASSERT_NE(Subtarget, nullptr);
  MCTargetOptions Options;
  std::unique_ptr<MCAsmInfo> AsmInfo(
      Target->createMCAsmInfo(*Registers, TripleName, Options));
  ASSERT_NE(AsmInfo, nullptr);
  MCContext Context(Triple(TripleName), AsmInfo.get(),
                    Registers.get(), Subtarget.get());
  std::unique_ptr<MCCodeEmitter> Emitter(
      Target->createMCCodeEmitter(*Instructions, Context));
  ASSERT_NE(Emitter, nullptr);

  unsigned ReturnOpcode = 0;
  for (unsigned Opcode = 0;
       Opcode != Instructions->getNumOpcodes(); ++Opcode) {
    StringRef Name = Instructions->getName(Opcode);
    if (Name == "RET64" || Name == "RET") {
      ReturnOpcode = Opcode;
      break;
    }
  }
  ASSERT_NE(ReturnOpcode, 0U);
  // Take whatever register the return opcode itself declares. Naming one
  // architecture's link register only finds a register on that architecture,
  // and a return that encodes no register at all is equally valid here: the
  // test only needs the constructed instruction to match the existing one.
  unsigned ReturnRegister = 0;
  for (const MCOperandInfo &Operand :
       Instructions->get(ReturnOpcode).operands()) {
    if (Operand.RegClass < 0)
      continue;
    const MCRegisterClass &Class = Registers->getRegClass(Operand.RegClass);
    if (Class.getNumRegs() == 0)
      continue;
    ReturnRegister = Class.getRegister(0);
    break;
  }

  PluginTargetSnapshot::NamedRecord Schema;
  Schema.CanonicalName = "test.native-schema";
  Schema.Opcodes = {{10, ReturnOpcode, "return", 0}};
  if (ReturnRegister != 0)
    Schema.SchemaRegisters = {
        {20, ReturnRegister, "return-address", 0}};
  PluginMCUnit Unit;
  MCInst &Existing = Unit.append(instruction(ReturnOpcode));
  if (ReturnRegister != 0)
    Existing.addOperand(MCOperand::createReg(ReturnRegister));

  MCTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  MCPluginBridge Bridge(Scope.task(), Unit, &Schema);
  const NevercMCAPI &API = Bridge.api();
  auto UnitHandle = Bridge.unit();
  ASSERT_TRUE(static_cast<bool>(UnitHandle));
  NevercMCInstHandle ExistingHandle{};
  ASSERT_EQ(API.GetFirstInstruction(
                API.Context, Scope.task().handle(), *UnitHandle,
                &ExistingHandle)
                .Code,
            NEVERC_STATUS_OK);
  NevercMCInstructionInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_MC_API_MAJOR,
                 NEVERC_MC_API_MINOR, 0};
  ASSERT_EQ(API.GetInstructionInfo(
                API.Context, Scope.task().handle(), ExistingHandle, &Info)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(Info.Opcode, 10U);

  NevercMCMutationHandle Mutation{};
  ASSERT_EQ(API.BeginMutation(API.Context, Scope.task().handle(),
                              *UnitHandle, &Mutation)
                .Code,
            NEVERC_STATUS_OK);
  NevercMCInstHandle Created{};
  ASSERT_EQ(API.CreateInstruction(API.Context, Scope.task().handle(),
                                  Mutation, Info.SchemaToken, Info.Opcode,
                                  &Created)
                .Code,
            NEVERC_STATUS_OK);
  if (ReturnRegister != 0) {
    NevercMCOperandValue Register =
        registerOperand(Info.SchemaToken, 20);
    ASSERT_EQ(API.AppendOperand(API.Context, Scope.task().handle(),
                                Mutation, Created, &Register)
                  .Code,
              NEVERC_STATUS_OK);
  }
  ASSERT_EQ(API.AppendInstruction(
                API.Context, Scope.task().handle(), Mutation,
                *UnitHandle, Created)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.CommitMutation(API.Context, Scope.task().handle(),
                               Mutation)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(Unit.size(), 2U);

  SmallVector<char, 16> ExistingBytes;
  SmallVector<char, 16> CreatedBytes;
  SmallVector<MCFixup, 4> ExistingFixups;
  SmallVector<MCFixup, 4> CreatedFixups;
  Emitter->encodeInstruction(*Unit.at(0), ExistingBytes,
                             ExistingFixups, *Subtarget);
  Emitter->encodeInstruction(*Unit.at(1), CreatedBytes,
                             CreatedFixups, *Subtarget);
  EXPECT_FALSE(CreatedBytes.empty());
  EXPECT_EQ(CreatedBytes, ExistingBytes);
  EXPECT_TRUE(CreatedFixups.empty());
}

} // namespace
