#include "neverc/Plugin/Host/IRPluginBridge.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "gtest/gtest.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/Error.h"
#include <memory>
#include <optional>
#include <string>

using namespace llvm;
using namespace neverc::plugin;

namespace {

std::string errorMessage(Error E) {
  auto Message = toString(std::move(E));
  return Message.str().str();
}

class IRBuilderScope {
public:
  IRBuilderScope()
      : Services("neverc-plugin-ir-builder-tests", LLVM_VERSION_MAJOR) {}

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
    auto CreatedTask =
        Session->createTask(NEVERC_TASK_TRANSLATION_UNIT);
    if (!CreatedTask) {
      ADD_FAILURE() << errorMessage(CreatedTask.takeError());
      return false;
    }
    Task = std::move(*CreatedTask);
    auto CreatedBridge = IRPluginBridge::create(*Task, "ir-builder");
    if (!CreatedBridge) {
      ADD_FAILURE() << errorMessage(CreatedBridge.takeError());
      return false;
    }
    Bridge = std::move(*CreatedBridge);
    return true;
  }

  ~IRBuilderScope() {
    Bridge.reset();
    if (Task)
      EXPECT_FALSE(Task->end());
    if (Session)
      EXPECT_FALSE(Session->end());
    Plan.reset();
    EXPECT_FALSE(Services.shutdown());
  }

  IRPluginBridge &bridge() { return *Bridge; }
  NevercTaskHandle taskHandle() const { return Task->handle(); }

private:
  PluginProcessServices Services;
  std::optional<PluginActivationPlan> Plan;
  std::unique_ptr<PluginSession> Session;
  std::unique_ptr<PluginTaskContext> Task;
  std::unique_ptr<IRPluginBridge> Bridge;
};

TEST(PluginIRBuilderTest,
     FunctionMutationBuildsArithmeticAndCommitsVerifiedIRThroughCAPI) {
  IRBuilderScope Scope;
  ASSERT_TRUE(Scope.initialize());
  IRPluginBridge &Bridge = Scope.bridge();
  const NevercIRBuilderAPI &API = Bridge.builderAPI();
  ASSERT_NE(API.BeginMutation, nullptr);
  ASSERT_NE(API.CommitMutation, nullptr);
  ASSERT_NE(API.AbortMutation, nullptr);
  ASSERT_NE(API.DestroyMutation, nullptr);
  ASSERT_NE(API.CreateBuilder, nullptr);
  ASSERT_NE(API.DestroyBuilder, nullptr);
  ASSERT_NE(API.SetInsertBlock, nullptr);
  ASSERT_NE(API.SetInsertBefore, nullptr);
  ASSERT_NE(API.SetDebugLocation, nullptr);
  ASSERT_NE(API.SetFastMathFlags, nullptr);
  ASSERT_NE(API.BuildBinary, nullptr);
  ASSERT_NE(API.BuildUnary, nullptr);
  ASSERT_NE(API.BuildCompare, nullptr);
  ASSERT_NE(API.BuildCast, nullptr);
  ASSERT_NE(API.BuildSelect, nullptr);
  ASSERT_NE(API.BuildAlloca, nullptr);
  ASSERT_NE(API.BuildLoad, nullptr);
  ASSERT_NE(API.BuildStore, nullptr);
  ASSERT_NE(API.BuildGetElementPtr, nullptr);
  ASSERT_NE(API.BuildCall, nullptr);
  ASSERT_NE(API.BuildPhi, nullptr);
  ASSERT_NE(API.AddPhiIncoming, nullptr);
  ASSERT_NE(API.BuildBranch, nullptr);
  ASSERT_NE(API.BuildConditionalBranch, nullptr);
  ASSERT_NE(API.BuildUnreachable, nullptr);
  ASSERT_NE(API.BuildReturn, nullptr);
  ASSERT_NE(API.BuildReturnVoid, nullptr);
  ASSERT_NE(API.CreateFunction, nullptr);
  ASSERT_NE(API.CreateBasicBlock, nullptr);

  const NevercIRCoreAPI &Core = Bridge.coreAPI();
  NevercIRModuleHandle Module{};
  NevercIRTypeHandle I32{};
  NevercIRTypeHandle FunctionType{};
  NevercIRValueHandle FnHandle{};
  NevercIRValueHandle EntryHandle{};
  NevercIRValueHandle Forty{};
  NevercIRValueHandle Two{};
  ASSERT_EQ(Core.GetModule(Core.Context, Scope.taskHandle(), &Module).Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(
      Core.GetIntegerType(Core.Context, Scope.taskHandle(), 32, &I32).Code,
      NEVERC_STATUS_OK);
  ASSERT_EQ(Core.GetFunctionType(Core.Context, Scope.taskHandle(), I32, nullptr,
                                 0, NEVERC_FALSE, &FunctionType)
                .Code,
            NEVERC_STATUS_OK);
  NevercIRMutationHandle Mutation{};
  ASSERT_EQ(API.BeginMutation(
                API.Context, Scope.taskHandle(),
                NEVERC_IR_MUTATION_SCOPE_MODULE, Module, &Mutation)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.CreateFunction(API.Context, Scope.taskHandle(), Mutation,
                               FunctionType, NevercStringView{"answer", 6},
                               &FnHandle)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.CreateBasicBlock(API.Context, Scope.taskHandle(), Mutation,
                                 FnHandle, NevercStringView{"entry", 5},
                                 &EntryHandle)
                .Code,
            NEVERC_STATUS_OK);
  uint64_t FortyWord = 40;
  uint64_t TwoWord = 2;
  ASSERT_EQ(Core.CreateIntegerConstant(Core.Context, Scope.taskHandle(), I32,
                                       &FortyWord, 1, &Forty)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(Core.CreateIntegerConstant(Core.Context, Scope.taskHandle(), I32,
                                       &TwoWord, 1, &Two)
                .Code,
            NEVERC_STATUS_OK);
  NevercIRBuilderHandle Builder{};
  ASSERT_EQ(API.CreateBuilder(API.Context, Scope.taskHandle(), Mutation,
                              &Builder)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.SetInsertBlock(API.Context, Scope.taskHandle(), Builder,
                               EntryHandle)
                .Code,
            NEVERC_STATUS_OK);

  NevercIRValueHandle Sum{};
  const char SumName[] = "sum";
  ASSERT_EQ(API.BuildBinary(
                API.Context, Scope.taskHandle(), Builder, NEVERC_IR_OPCODE_ADD,
                Forty, Two,
                NevercStringView{SumName, sizeof(SumName) - 1}, &Sum)
                .Code,
            NEVERC_STATUS_OK);
  NevercIRValueHandle Return{};
  ASSERT_EQ(API.BuildReturn(API.Context, Scope.taskHandle(), Builder, Sum,
                            &Return)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(API.CommitMutation(API.Context, Scope.taskHandle(), Mutation).Code,
            NEVERC_STATUS_OK);
  EXPECT_FALSE(verifyModule(Bridge.module(), &errs()));

  Value *ResolvedSum = nullptr;
  ASSERT_EQ(Bridge.resolveValue(Sum, &ResolvedSum).Code, NEVERC_STATUS_OK);
  auto *Add = dyn_cast<BinaryOperator>(ResolvedSum);
  ASSERT_NE(Add, nullptr);
  EXPECT_EQ(Add->getOpcode(), Instruction::Add);
  EXPECT_EQ(Add->getName(), "sum");

  EXPECT_EQ(API.DestroyBuilder(API.Context, Scope.taskHandle(), Builder).Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(API.DestroyMutation(API.Context, Scope.taskHandle(), Mutation).Code,
            NEVERC_STATUS_OK);
}

TEST(PluginIRBuilderTest,
     AbortedMutationRollsBackCreatedInstructionsAndStalesTheirHandles) {
  IRBuilderScope Scope;
  ASSERT_TRUE(Scope.initialize());
  IRPluginBridge &Bridge = Scope.bridge();
  const NevercIRBuilderAPI &API = Bridge.builderAPI();

  Type *I32 = Type::getInt32Ty(Bridge.context());
  Function *Fn = Function::Create(
      FunctionType::get(I32, false), GlobalValue::ExternalLinkage, "aborted",
      Bridge.module());
  BasicBlock *Entry = BasicBlock::Create(Bridge.context(), "entry", Fn);
  auto FnHandle = Bridge.wrapValue(*Fn);
  auto EntryHandle = Bridge.wrapValue(*Entry);
  auto One = Bridge.wrapValue(*ConstantInt::get(I32, 1));
  ASSERT_TRUE(static_cast<bool>(FnHandle));
  ASSERT_TRUE(static_cast<bool>(EntryHandle));
  ASSERT_TRUE(static_cast<bool>(One));

  NevercIRMutationHandle Mutation{};
  ASSERT_EQ(API.BeginMutation(
                API.Context, Scope.taskHandle(),
                NEVERC_IR_MUTATION_SCOPE_FUNCTION, *FnHandle, &Mutation)
                .Code,
            NEVERC_STATUS_OK);
  NevercIRBuilderHandle Builder{};
  ASSERT_EQ(API.CreateBuilder(API.Context, Scope.taskHandle(), Mutation,
                              &Builder)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.SetInsertBlock(API.Context, Scope.taskHandle(), Builder,
                               *EntryHandle)
                .Code,
            NEVERC_STATUS_OK);

  NevercIRValueHandle Created{};
  ASSERT_EQ(API.BuildBinary(
                API.Context, Scope.taskHandle(), Builder, NEVERC_IR_OPCODE_ADD,
                *One, *One, NevercStringView{"temporary", 9}, &Created)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_FALSE(Entry->empty());

  ASSERT_EQ(API.AbortMutation(API.Context, Scope.taskHandle(), Mutation).Code,
            NEVERC_STATUS_OK);
  EXPECT_TRUE(Entry->empty());
  Value *Resolved = nullptr;
  EXPECT_EQ(Bridge.resolveValue(Created, &Resolved).Code,
            NEVERC_STATUS_STALE_HANDLE);

  NevercIRValueHandle Return{};
  EXPECT_EQ(API.BuildReturn(API.Context, Scope.taskHandle(), Builder, *One,
                            &Return)
                .Code,
            NEVERC_STATUS_INVALID_STATE);
  EXPECT_EQ(API.DestroyBuilder(API.Context, Scope.taskHandle(), Builder).Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(API.DestroyMutation(API.Context, Scope.taskHandle(), Mutation).Code,
            NEVERC_STATUS_OK);
}

TEST(PluginIRBuilderTest,
     BuildsControlFlowMemoryCallAndPhiThroughThePublicBuilderTable) {
  IRBuilderScope Scope;
  ASSERT_TRUE(Scope.initialize());
  IRPluginBridge &Bridge = Scope.bridge();
  const NevercIRBuilderAPI &API = Bridge.builderAPI();

  Type *I32 = Type::getInt32Ty(Bridge.context());
  FunctionType *CalleeType = FunctionType::get(I32, {I32}, false);
  Function *Callee =
      Function::Create(CalleeType, GlobalValue::ExternalLinkage, "callee",
                       Bridge.module());
  Function *Fn = Function::Create(
      FunctionType::get(I32, false), GlobalValue::ExternalLinkage, "families",
      Bridge.module());
  BasicBlock *Entry = BasicBlock::Create(Bridge.context(), "entry", Fn);
  BasicBlock *Then = BasicBlock::Create(Bridge.context(), "then", Fn);
  BasicBlock *Else = BasicBlock::Create(Bridge.context(), "else", Fn);
  BasicBlock *Merge = BasicBlock::Create(Bridge.context(), "merge", Fn);

  auto I32Handle = Bridge.wrapType(*I32);
  auto CalleeTypeHandle = Bridge.wrapType(*CalleeType);
  auto CalleeHandle = Bridge.wrapValue(*Callee);
  auto FnHandle = Bridge.wrapValue(*Fn);
  auto EntryHandle = Bridge.wrapValue(*Entry);
  auto ThenHandle = Bridge.wrapValue(*Then);
  auto ElseHandle = Bridge.wrapValue(*Else);
  auto MergeHandle = Bridge.wrapValue(*Merge);
  auto Forty = Bridge.wrapValue(*ConstantInt::get(I32, 40));
  auto Two = Bridge.wrapValue(*ConstantInt::get(I32, 2));
  ASSERT_TRUE(static_cast<bool>(I32Handle));
  ASSERT_TRUE(static_cast<bool>(CalleeTypeHandle));
  ASSERT_TRUE(static_cast<bool>(CalleeHandle));
  ASSERT_TRUE(static_cast<bool>(FnHandle));
  ASSERT_TRUE(static_cast<bool>(EntryHandle));
  ASSERT_TRUE(static_cast<bool>(ThenHandle));
  ASSERT_TRUE(static_cast<bool>(ElseHandle));
  ASSERT_TRUE(static_cast<bool>(MergeHandle));
  ASSERT_TRUE(static_cast<bool>(Forty));
  ASSERT_TRUE(static_cast<bool>(Two));

  NevercIRMutationHandle Mutation{};
  ASSERT_EQ(API.BeginMutation(
                API.Context, Scope.taskHandle(),
                NEVERC_IR_MUTATION_SCOPE_FUNCTION, *FnHandle, &Mutation)
                .Code,
            NEVERC_STATUS_OK);
  NevercIRBuilderHandle Builder{};
  ASSERT_EQ(API.CreateBuilder(API.Context, Scope.taskHandle(), Mutation,
                              &Builder)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.SetInsertBlock(API.Context, Scope.taskHandle(), Builder,
                               *EntryHandle)
                .Code,
            NEVERC_STATUS_OK);

  NevercIRValueHandle Slot{};
  ASSERT_EQ(API.BuildAlloca(API.Context, Scope.taskHandle(), Builder,
                            *I32Handle, 0, NevercIRValueHandle{},
                            NevercStringView{"slot", 4}, &Slot)
                .Code,
            NEVERC_STATUS_OK);
  NevercIRValueHandle Store{};
  ASSERT_EQ(API.BuildStore(API.Context, Scope.taskHandle(), Builder, *Forty,
                           Slot, &Store)
                .Code,
            NEVERC_STATUS_OK);
  NevercIRValueHandle Loaded{};
  ASSERT_EQ(API.BuildLoad(API.Context, Scope.taskHandle(), Builder, *I32Handle,
                          Slot, NevercStringView{"loaded", 6}, &Loaded)
                .Code,
            NEVERC_STATUS_OK);
  NevercIRValueHandle Condition{};
  ASSERT_EQ(API.BuildCompare(
                API.Context, Scope.taskHandle(), Builder,
                NEVERC_IR_PREDICATE_ICMP_EQ, Loaded, *Forty,
                NevercStringView{"matches", 7}, &Condition)
                .Code,
            NEVERC_STATUS_OK);
  NevercIRValueHandle EntryBranch{};
  ASSERT_EQ(API.BuildConditionalBranch(
                API.Context, Scope.taskHandle(), Builder, Condition,
                *ThenHandle, *ElseHandle, &EntryBranch)
                .Code,
            NEVERC_STATUS_OK);

  ASSERT_EQ(API.SetInsertBlock(API.Context, Scope.taskHandle(), Builder,
                               *ThenHandle)
                .Code,
            NEVERC_STATUS_OK);
  NevercIRValueHandle Arguments[] = {Loaded};
  NevercIRValueHandle Called{};
  ASSERT_EQ(API.BuildCall(
                API.Context, Scope.taskHandle(), Builder, *CalleeTypeHandle,
                *CalleeHandle, Arguments, 1, NevercStringView{"called", 6},
                &Called)
                .Code,
            NEVERC_STATUS_OK);
  NevercIRValueHandle ThenBranch{};
  ASSERT_EQ(API.BuildBranch(API.Context, Scope.taskHandle(), Builder,
                            *MergeHandle, &ThenBranch)
                .Code,
            NEVERC_STATUS_OK);

  ASSERT_EQ(API.SetInsertBlock(API.Context, Scope.taskHandle(), Builder,
                               *ElseHandle)
                .Code,
            NEVERC_STATUS_OK);
  NevercIRValueHandle Added{};
  ASSERT_EQ(API.BuildBinary(API.Context, Scope.taskHandle(), Builder,
                            NEVERC_IR_OPCODE_ADD, Loaded, *Two,
                            NevercStringView{"added", 5}, &Added)
                .Code,
            NEVERC_STATUS_OK);
  NevercIRValueHandle ElseBranch{};
  ASSERT_EQ(API.BuildBranch(API.Context, Scope.taskHandle(), Builder,
                            *MergeHandle, &ElseBranch)
                .Code,
            NEVERC_STATUS_OK);

  ASSERT_EQ(API.SetInsertBlock(API.Context, Scope.taskHandle(), Builder,
                               *MergeHandle)
                .Code,
            NEVERC_STATUS_OK);
  NevercIRValueHandle Phi{};
  ASSERT_EQ(API.BuildPhi(API.Context, Scope.taskHandle(), Builder, *I32Handle,
                         2, NevercStringView{"result", 6}, &Phi)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.AddPhiIncoming(API.Context, Scope.taskHandle(), Mutation, Phi,
                               Called, *ThenHandle)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.AddPhiIncoming(API.Context, Scope.taskHandle(), Mutation, Phi,
                               Added, *ElseHandle)
                .Code,
            NEVERC_STATUS_OK);
  NevercIRValueHandle Return{};
  ASSERT_EQ(API.BuildReturn(API.Context, Scope.taskHandle(), Builder, Phi,
                            &Return)
                .Code,
            NEVERC_STATUS_OK);

  EXPECT_EQ(API.CommitMutation(API.Context, Scope.taskHandle(), Mutation).Code,
            NEVERC_STATUS_OK);
  EXPECT_FALSE(verifyFunction(*Fn, &errs()));
  EXPECT_EQ(std::distance(inst_begin(Fn), inst_end(Fn)), 11);
  EXPECT_EQ(API.DestroyBuilder(API.Context, Scope.taskHandle(), Builder).Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(API.DestroyMutation(API.Context, Scope.taskHandle(), Mutation).Code,
            NEVERC_STATUS_OK);
}

TEST(PluginIRBuilderTest,
     FailedVerificationAutomaticallyRollsBackTheMutation) {
  IRBuilderScope Scope;
  ASSERT_TRUE(Scope.initialize());
  IRPluginBridge &Bridge = Scope.bridge();
  const NevercIRBuilderAPI &API = Bridge.builderAPI();

  Type *I32 = Type::getInt32Ty(Bridge.context());
  Function *Fn = Function::Create(
      FunctionType::get(I32, false), GlobalValue::ExternalLinkage, "broken",
      Bridge.module());
  BasicBlock *Entry = BasicBlock::Create(Bridge.context(), "entry", Fn);
  auto FnHandle = Bridge.wrapValue(*Fn);
  auto EntryHandle = Bridge.wrapValue(*Entry);
  auto One = Bridge.wrapValue(*ConstantInt::get(I32, 1));
  ASSERT_TRUE(static_cast<bool>(FnHandle));
  ASSERT_TRUE(static_cast<bool>(EntryHandle));
  ASSERT_TRUE(static_cast<bool>(One));

  NevercIRMutationHandle Mutation{};
  ASSERT_EQ(API.BeginMutation(
                API.Context, Scope.taskHandle(),
                NEVERC_IR_MUTATION_SCOPE_FUNCTION, *FnHandle, &Mutation)
                .Code,
            NEVERC_STATUS_OK);
  NevercIRBuilderHandle Builder{};
  ASSERT_EQ(API.CreateBuilder(API.Context, Scope.taskHandle(), Mutation,
                              &Builder)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.SetInsertBlock(API.Context, Scope.taskHandle(), Builder,
                               *EntryHandle)
                .Code,
            NEVERC_STATUS_OK);
  NevercIRValueHandle Created{};
  ASSERT_EQ(API.BuildBinary(
                API.Context, Scope.taskHandle(), Builder, NEVERC_IR_OPCODE_ADD,
                *One, *One, NevercStringView{}, &Created)
                .Code,
            NEVERC_STATUS_OK);

  EXPECT_EQ(API.CommitMutation(API.Context, Scope.taskHandle(), Mutation).Code,
            NEVERC_STATUS_VERIFICATION_FAILED);
  EXPECT_TRUE(Entry->empty());
  Value *Resolved = nullptr;
  EXPECT_EQ(Bridge.resolveValue(Created, &Resolved).Code,
            NEVERC_STATUS_STALE_HANDLE);
  EXPECT_EQ(API.DestroyBuilder(API.Context, Scope.taskHandle(), Builder).Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(API.DestroyMutation(API.Context, Scope.taskHandle(), Mutation).Code,
            NEVERC_STATUS_OK);
}

TEST(PluginIRBuilderTest, FunctionMutationRejectsInsertionIntoAnotherFunction) {
  IRBuilderScope Scope;
  ASSERT_TRUE(Scope.initialize());
  IRPluginBridge &Bridge = Scope.bridge();
  const NevercIRBuilderAPI &API = Bridge.builderAPI();

  FunctionType *Signature = FunctionType::get(Type::getVoidTy(Bridge.context()),
                                               false);
  Function *First = Function::Create(Signature, GlobalValue::ExternalLinkage,
                                     "first", Bridge.module());
  Function *Second = Function::Create(Signature, GlobalValue::ExternalLinkage,
                                      "second", Bridge.module());
  BasicBlock *FirstEntry =
      BasicBlock::Create(Bridge.context(), "entry", First);
  BasicBlock *SecondEntry =
      BasicBlock::Create(Bridge.context(), "entry", Second);
  BranchInst::Create(SecondEntry, SecondEntry);
  auto FirstHandle = Bridge.wrapValue(*First);
  auto FirstEntryHandle = Bridge.wrapValue(*FirstEntry);
  auto SecondEntryHandle = Bridge.wrapValue(*SecondEntry);
  ASSERT_TRUE(static_cast<bool>(FirstHandle));
  ASSERT_TRUE(static_cast<bool>(FirstEntryHandle));
  ASSERT_TRUE(static_cast<bool>(SecondEntryHandle));

  NevercIRMutationHandle Mutation{};
  ASSERT_EQ(API.BeginMutation(
                API.Context, Scope.taskHandle(),
                NEVERC_IR_MUTATION_SCOPE_FUNCTION, *FirstHandle, &Mutation)
                .Code,
            NEVERC_STATUS_OK);
  NevercIRBuilderHandle Builder{};
  ASSERT_EQ(API.CreateBuilder(API.Context, Scope.taskHandle(), Mutation,
                              &Builder)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(API.SetInsertBlock(API.Context, Scope.taskHandle(), Builder,
                               *SecondEntryHandle)
                .Code,
            NEVERC_STATUS_WRONG_SCOPE);
  ASSERT_EQ(API.SetInsertBlock(API.Context, Scope.taskHandle(), Builder,
                               *FirstEntryHandle)
                .Code,
            NEVERC_STATUS_OK);
  NevercIRValueHandle Return{};
  ASSERT_EQ(API.BuildReturnVoid(API.Context, Scope.taskHandle(), Builder,
                                &Return)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(API.CommitMutation(API.Context, Scope.taskHandle(), Mutation).Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(API.DestroyBuilder(API.Context, Scope.taskHandle(), Builder).Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(API.DestroyMutation(API.Context, Scope.taskHandle(), Mutation).Code,
            NEVERC_STATUS_OK);
}

TEST(PluginIRBuilderTest, RejectsTaskHandleFromAnotherSessionOwner) {
  IRBuilderScope Scope;
  ASSERT_TRUE(Scope.initialize());
  IRPluginBridge &Bridge = Scope.bridge();
  const NevercIRBuilderAPI &API = Bridge.builderAPI();

  Function *Fn = Function::Create(
      FunctionType::get(Type::getVoidTy(Bridge.context()), false),
      GlobalValue::ExternalLinkage, "wrong_owner", Bridge.module());
  auto FnHandle = Bridge.wrapValue(*Fn);
  ASSERT_TRUE(static_cast<bool>(FnHandle));

  NevercTaskHandle WrongOwner = Scope.taskHandle();
  ++WrongOwner.Owner;
  NevercIRMutationHandle Mutation{};
  EXPECT_EQ(API.BeginMutation(API.Context, WrongOwner,
                              NEVERC_IR_MUTATION_SCOPE_FUNCTION, *FnHandle,
                              &Mutation)
                .Code,
            NEVERC_STATUS_WRONG_SCOPE);
}

TEST(PluginIRBuilderTest, LoopMutationAcceptsOnlyBlocksInTheNaturalLoop) {
  IRBuilderScope Scope;
  ASSERT_TRUE(Scope.initialize());
  IRPluginBridge &Bridge = Scope.bridge();
  const NevercIRBuilderAPI &API = Bridge.builderAPI();

  Function *Fn = Function::Create(
      FunctionType::get(Type::getVoidTy(Bridge.context()), false),
      GlobalValue::ExternalLinkage, "loop", Bridge.module());
  BasicBlock *Entry = BasicBlock::Create(Bridge.context(), "entry", Fn);
  BasicBlock *Header = BasicBlock::Create(Bridge.context(), "header", Fn);
  BasicBlock *Body = BasicBlock::Create(Bridge.context(), "body", Fn);
  BranchInst::Create(Header, Entry);
  BranchInst::Create(Body, Header);
  BranchInst::Create(Header, Body);
  ASSERT_FALSE(verifyFunction(*Fn, &errs()));
  auto HeaderHandle = Bridge.wrapValue(*Header);
  auto BodyHandle = Bridge.wrapValue(*Body);
  auto EntryHandle = Bridge.wrapValue(*Entry);
  ASSERT_TRUE(static_cast<bool>(HeaderHandle));
  ASSERT_TRUE(static_cast<bool>(BodyHandle));
  ASSERT_TRUE(static_cast<bool>(EntryHandle));

  NevercIRMutationHandle Mutation{};
  ASSERT_EQ(API.BeginMutation(API.Context, Scope.taskHandle(),
                              NEVERC_IR_MUTATION_SCOPE_LOOP, *HeaderHandle,
                              &Mutation)
                .Code,
            NEVERC_STATUS_OK);
  NevercIRBuilderHandle Builder{};
  ASSERT_EQ(API.CreateBuilder(API.Context, Scope.taskHandle(), Mutation,
                              &Builder)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(API.SetInsertBlock(API.Context, Scope.taskHandle(), Builder,
                               *EntryHandle)
                .Code,
            NEVERC_STATUS_WRONG_SCOPE);
  EXPECT_EQ(API.SetInsertBlock(API.Context, Scope.taskHandle(), Builder,
                               *BodyHandle)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(API.AbortMutation(API.Context, Scope.taskHandle(), Mutation).Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(API.DestroyBuilder(API.Context, Scope.taskHandle(), Builder).Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(API.DestroyMutation(API.Context, Scope.taskHandle(), Mutation).Code,
            NEVERC_STATUS_OK);
}

} // namespace
