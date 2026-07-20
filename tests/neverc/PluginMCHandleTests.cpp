#include "neverc/Plugin/Host/MCPluginBridge.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Error.h"
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

bool sameHandle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

class MCTaskScope {
public:
  MCTaskScope()
      : Services("neverc-plugin-mc-handle-tests", LLVM_VERSION_MAJOR) {}

  bool initialize() {
    if (Error E = Services.interfaces().freeze()) {
      ADD_FAILURE() << errorText(std::move(E));
      return false;
    }
    auto CreatedPlan = makePluginActivationPlan(Services.registry(), {});
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

PluginTargetSnapshot::NamedRecord schema(uint64_t Suffix = 1) {
  PluginTargetSnapshot::NamedRecord Schema;
  Schema.ID = {UINT64_C(0x4e43504d43534348), Suffix};
  Schema.TargetID = {UINT64_C(0x4e43505441524754), Suffix};
  Schema.CanonicalName = "test.mc-schema";
  Schema.Digest =
      "1111111111111111111111111111111111111111111111111111111111111111";
  Schema.Opcodes = {{10, 100, "test.opcode", 0}};
  Schema.SchemaRegisters = {{20, 5, "test.register", 0}};
  return Schema;
}

std::unique_ptr<MCInst> instruction(uint32_t Opcode) {
  auto Result = std::make_unique<MCInst>();
  Result->setOpcode(Opcode);
  return Result;
}

TEST(PluginMCHandleTest, PublishesTaskAndGenerationBoundSchemaToken) {
  MCTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  PluginMCUnit Unit;
  auto Schema = schema();
  MCPluginBridge Bridge(Scope.task(), Unit, &Schema);
  const NevercMCAPI &API = Bridge.api();
  auto UnitHandle = Bridge.unit();
  ASSERT_TRUE(static_cast<bool>(UnitHandle));

  NevercMCSchemaTokenHandle Token{};
  ASSERT_EQ(API.GetSchemaToken(API.Context, Scope.task().handle(), *UnitHandle,
                               &Token)
                .Code,
            NEVERC_STATUS_OK);
  NevercMCSchemaTokenInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_MC_API_MAJOR, NEVERC_MC_API_MINOR, 0};
  ASSERT_EQ(API.GetSchemaTokenInfo(API.Context, Scope.task().handle(), Token,
                                   &Info)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Info.SchemaID.High, Schema.ID.High);
  EXPECT_EQ(Info.SchemaID.Low, Schema.ID.Low);
  EXPECT_EQ(Info.TargetID.High, Schema.TargetID.High);
  EXPECT_EQ(Info.TargetID.Low, Schema.TargetID.Low);
  EXPECT_EQ(std::string(Info.Digest.Data, Info.Digest.Length), Schema.Digest);
  EXPECT_EQ(Info.UnitGeneration, 1U);
}

TEST(PluginMCHandleTest, CommitStalesUnitTokenAndAllChildHandles) {
  MCTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  PluginMCUnit Unit;
  auto Schema = schema();
  MCPluginBridge Bridge(Scope.task(), Unit, &Schema);
  const NevercMCAPI &API = Bridge.api();
  auto UnitHandle = Bridge.unit();
  ASSERT_TRUE(static_cast<bool>(UnitHandle));
  NevercMCSchemaTokenHandle Token{};
  ASSERT_EQ(API.GetSchemaToken(API.Context, Scope.task().handle(), *UnitHandle,
                               &Token)
                .Code,
            NEVERC_STATUS_OK);
  NevercMCMutationHandle Mutation{};
  ASSERT_EQ(API.BeginMutation(API.Context, Scope.task().handle(), *UnitHandle,
                              &Mutation)
                .Code,
            NEVERC_STATUS_OK);
  NevercMCInstHandle Instruction{};
  ASSERT_EQ(API.CreateInstruction(API.Context, Scope.task().handle(), Mutation,
                                  Token, 10, &Instruction)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.AppendInstruction(API.Context, Scope.task().handle(), Mutation,
                                  *UnitHandle, Instruction)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.CommitMutation(API.Context, Scope.task().handle(), Mutation)
                .Code,
            NEVERC_STATUS_OK);

  NevercMCInstructionInfo InstructionInfo{};
  InstructionInfo.Header = {sizeof(InstructionInfo), NEVERC_MC_API_MAJOR,
                            NEVERC_MC_API_MINOR, 0};
  EXPECT_EQ(API.GetInstructionInfo(API.Context, Scope.task().handle(),
                                   Instruction, &InstructionInfo)
                .Code,
            NEVERC_STATUS_STALE_HANDLE);
  NevercMCSchemaTokenInfo TokenInfo{};
  TokenInfo.Header = {sizeof(TokenInfo), NEVERC_MC_API_MAJOR,
                      NEVERC_MC_API_MINOR, 0};
  EXPECT_EQ(API.GetSchemaTokenInfo(API.Context, Scope.task().handle(), Token,
                                   &TokenInfo)
                .Code,
            NEVERC_STATUS_STALE_HANDLE);
  NevercMCMutationHandle StaleMutation{};
  EXPECT_EQ(API.BeginMutation(API.Context, Scope.task().handle(), *UnitHandle,
                              &StaleMutation)
                .Code,
            NEVERC_STATUS_STALE_HANDLE);

  auto NewUnitHandle = Bridge.unit();
  ASSERT_TRUE(static_cast<bool>(NewUnitHandle));
  EXPECT_FALSE(sameHandle(*NewUnitHandle, *UnitHandle));
  NevercMCSchemaTokenHandle NewToken{};
  ASSERT_EQ(API.GetSchemaToken(API.Context, Scope.task().handle(),
                               *NewUnitHandle, &NewToken)
                .Code,
            NEVERC_STATUS_OK);
  TokenInfo = {};
  TokenInfo.Header = {sizeof(TokenInfo), NEVERC_MC_API_MAJOR,
                      NEVERC_MC_API_MINOR, 0};
  ASSERT_EQ(API.GetSchemaTokenInfo(API.Context, Scope.task().handle(), NewToken,
                                   &TokenInfo)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(TokenInfo.UnitGeneration, 2U);
}

TEST(PluginMCHandleTest, RejectsSchemaTokenFromAnotherUnit) {
  MCTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  PluginMCUnit FirstUnit;
  PluginMCUnit SecondUnit;
  auto FirstSchema = schema(1);
  auto SecondSchema = schema(2);
  MCPluginBridge First(Scope.task(), FirstUnit, &FirstSchema);
  MCPluginBridge Second(Scope.task(), SecondUnit, &SecondSchema);
  const NevercMCAPI &FirstAPI = First.api();
  const NevercMCAPI &SecondAPI = Second.api();
  auto FirstUnitHandle = First.unit();
  auto SecondUnitHandle = Second.unit();
  ASSERT_TRUE(static_cast<bool>(FirstUnitHandle));
  ASSERT_TRUE(static_cast<bool>(SecondUnitHandle));
  NevercMCSchemaTokenHandle ForeignToken{};
  ASSERT_EQ(SecondAPI.GetSchemaToken(SecondAPI.Context, Scope.task().handle(),
                                    *SecondUnitHandle, &ForeignToken)
                .Code,
            NEVERC_STATUS_OK);
  NevercMCMutationHandle Mutation{};
  ASSERT_EQ(FirstAPI.BeginMutation(FirstAPI.Context, Scope.task().handle(),
                                   *FirstUnitHandle, &Mutation)
                .Code,
            NEVERC_STATUS_OK);
  NevercMCInstHandle Instruction{};
  EXPECT_EQ(FirstAPI.CreateInstruction(
                 FirstAPI.Context, Scope.task().handle(), Mutation,
                 ForeignToken, 10, &Instruction)
                .Code,
            NEVERC_STATUS_WRONG_SCOPE);
  EXPECT_EQ(FirstAPI.AbandonMutation(FirstAPI.Context, Scope.task().handle(),
                                    Mutation)
                .Code,
            NEVERC_STATUS_OK);
}

TEST(PluginMCHandleTest, ErasedHandleNeverRevivesAfterAbandon) {
  MCTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  PluginMCUnit Unit;
  Unit.append(instruction(100));
  auto Schema = schema();
  MCPluginBridge Bridge(Scope.task(), Unit, &Schema);
  const NevercMCAPI &API = Bridge.api();
  auto UnitHandle = Bridge.unit();
  ASSERT_TRUE(static_cast<bool>(UnitHandle));
  NevercMCInstHandle Instruction{};
  ASSERT_EQ(API.GetFirstInstruction(API.Context, Scope.task().handle(),
                                    *UnitHandle, &Instruction)
                .Code,
            NEVERC_STATUS_OK);
  NevercMCMutationHandle Mutation{};
  ASSERT_EQ(API.BeginMutation(API.Context, Scope.task().handle(), *UnitHandle,
                              &Mutation)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.EraseInstruction(API.Context, Scope.task().handle(), Mutation,
                                 Instruction)
                .Code,
            NEVERC_STATUS_OK);

  NevercMCInstructionInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_MC_API_MAJOR, NEVERC_MC_API_MINOR, 0};
  EXPECT_EQ(API.GetInstructionInfo(API.Context, Scope.task().handle(),
                                   Instruction, &Info)
                .Code,
            NEVERC_STATUS_STALE_HANDLE);
  ASSERT_EQ(API.AbandonMutation(API.Context, Scope.task().handle(), Mutation)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Unit.size(), 1U);
  EXPECT_EQ(API.GetInstructionInfo(API.Context, Scope.task().handle(),
                                   Instruction, &Info)
                .Code,
            NEVERC_STATUS_STALE_HANDLE);
  NevercMCInstHandle Reacquired{};
  ASSERT_EQ(API.GetFirstInstruction(API.Context, Scope.task().handle(),
                                    *UnitHandle, &Reacquired)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_FALSE(sameHandle(Instruction, Reacquired));
}

TEST(PluginMCHandleTest, TaskEndStalesSchemaToken) {
  MCTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  PluginMCUnit Unit;
  auto Schema = schema();
  MCPluginBridge Bridge(Scope.task(), Unit, &Schema);
  const NevercMCAPI &API = Bridge.api();
  auto UnitHandle = Bridge.unit();
  ASSERT_TRUE(static_cast<bool>(UnitHandle));
  NevercMCSchemaTokenHandle Token{};
  ASSERT_EQ(API.GetSchemaToken(API.Context, Scope.task().handle(), *UnitHandle,
                               &Token)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_FALSE(Scope.task().end());

  NevercMCSchemaTokenInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_MC_API_MAJOR, NEVERC_MC_API_MINOR, 0};
  EXPECT_EQ(API.GetSchemaTokenInfo(API.Context, Scope.task().handle(), Token,
                                   &Info)
                .Code,
            NEVERC_STATUS_STALE_HANDLE);
}

} // namespace
