#include "neverc/Plugin/Host/IRPluginBridge.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "gtest/gtest.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace llvm;
using namespace neverc::plugin;

namespace {

std::string errorMessage(Error E) {
  auto Message = toString(std::move(E));
  return Message.str().str();
}

class EmptyIRTask {
public:
  EmptyIRTask()
      : Services("neverc-plugin-ir-handle-tests", LLVM_VERSION_MAJOR) {}

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
    return true;
  }

  ~EmptyIRTask() {
    for (auto It = SiblingTasks.rbegin(); It != SiblingTasks.rend(); ++It)
      EXPECT_FALSE((*It)->end());
    if (OtherTask)
      EXPECT_FALSE(OtherTask->end());
    if (Task)
      EXPECT_FALSE(Task->end());
    if (OtherSession)
      EXPECT_FALSE(OtherSession->end());
    if (Session)
      EXPECT_FALSE(Session->end());
    OtherPlan.reset();
    Plan.reset();
    EXPECT_FALSE(Services.shutdown());
  }

  PluginTaskContext &task() { return *Task; }

  PluginTaskContext *createSiblingTask() {
    return createTask(NEVERC_TASK_TRANSLATION_UNIT);
  }

  PluginTaskContext *createTask(
      NevercTaskKind Kind, PluginTaskContext *Parent = nullptr) {
    auto Created = Session->createTask(Kind, Parent);
    if (!Created) {
      ADD_FAILURE() << errorMessage(Created.takeError());
      return nullptr;
    }
    SiblingTasks.push_back(std::move(*Created));
    return SiblingTasks.back().get();
  }

  PluginTaskContext *createOtherSessionTask() {
    auto CreatedPlan = makePluginActivationPlan(Services.registry(), {});
    if (!CreatedPlan) {
      ADD_FAILURE() << errorMessage(CreatedPlan.takeError());
      return nullptr;
    }
    OtherPlan.emplace(std::move(*CreatedPlan));
    auto CreatedSession = PluginSession::create(Services, *OtherPlan);
    if (!CreatedSession) {
      ADD_FAILURE() << errorMessage(CreatedSession.takeError());
      return nullptr;
    }
    OtherSession = std::move(*CreatedSession);
    auto CreatedTask =
        OtherSession->createTask(NEVERC_TASK_TRANSLATION_UNIT);
    if (!CreatedTask) {
      ADD_FAILURE() << errorMessage(CreatedTask.takeError());
      return nullptr;
    }
    OtherTask = std::move(*CreatedTask);
    return OtherTask.get();
  }

private:
  PluginProcessServices Services;
  std::optional<PluginActivationPlan> Plan;
  std::optional<PluginActivationPlan> OtherPlan;
  std::unique_ptr<PluginSession> Session;
  std::unique_ptr<PluginSession> OtherSession;
  std::unique_ptr<PluginTaskContext> Task;
  std::unique_ptr<PluginTaskContext> OtherTask;
  std::vector<std::unique_ptr<PluginTaskContext>> SiblingTasks;
};

TEST(PluginIRHandleTest, ReusesTheLiveHandleForTheSameModule) {
  EmptyIRTask Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Created = IRPluginBridge::create(Scope.task(), "same-module");
  ASSERT_TRUE(static_cast<bool>(Created))
      << errorMessage(Created.takeError());
  std::unique_ptr<IRPluginBridge> Bridge = std::move(*Created);

  auto WrappedAgain = Bridge->wrapModule(Bridge->module());
  ASSERT_TRUE(static_cast<bool>(WrappedAgain))
      << errorMessage(WrappedAgain.takeError());
  EXPECT_EQ(WrappedAgain->Owner, Bridge->moduleHandle().Owner);
  EXPECT_EQ(WrappedAgain->Value, Bridge->moduleHandle().Value);

  llvm::Module *Resolved = nullptr;
  EXPECT_EQ(
      Bridge->resolveModule(*WrappedAgain, &Resolved).Code,
      NEVERC_STATUS_OK);
  EXPECT_EQ(Resolved, &Bridge->module());

  auto WrappedContext = Bridge->wrapContext(Bridge->context());
  ASSERT_TRUE(static_cast<bool>(WrappedContext));
  EXPECT_EQ(WrappedContext->Owner, Bridge->contextHandle().Owner);
  EXPECT_EQ(WrappedContext->Value, Bridge->contextHandle().Value);
  LLVMContext *ResolvedContext = nullptr;
  EXPECT_EQ(
      Bridge->resolveContext(*WrappedContext, &ResolvedContext).Code,
      NEVERC_STATUS_OK);
  EXPECT_EQ(ResolvedContext, &Bridge->context());
}

TEST(PluginIRHandleTest, RejectsWrongTypeTaskAndSession) {
  EmptyIRTask FirstScope;
  ASSERT_TRUE(FirstScope.initialize());
  PluginTaskContext *SiblingTask = FirstScope.createSiblingTask();
  PluginTaskContext *OtherTask = FirstScope.createOtherSessionTask();
  ASSERT_NE(SiblingTask, nullptr);
  ASSERT_NE(OtherTask, nullptr);

  auto FirstCreated =
      IRPluginBridge::create(FirstScope.task(), "first-module");
  auto SiblingCreated =
      IRPluginBridge::create(*SiblingTask, "sibling-module");
  auto OtherCreated =
      IRPluginBridge::create(*OtherTask, "other-module");
  ASSERT_TRUE(static_cast<bool>(FirstCreated));
  ASSERT_TRUE(static_cast<bool>(SiblingCreated));
  ASSERT_TRUE(static_cast<bool>(OtherCreated));
  std::unique_ptr<IRPluginBridge> First = std::move(*FirstCreated);
  std::unique_ptr<IRPluginBridge> Sibling = std::move(*SiblingCreated);
  std::unique_ptr<IRPluginBridge> Other = std::move(*OtherCreated);

  llvm::Module *Resolved = nullptr;
  EXPECT_EQ(
      First->resolveModule(First->contextHandle(), &Resolved).Code,
      NEVERC_STATUS_WRONG_TYPE);
  EXPECT_EQ(
      Sibling->resolveModule(First->moduleHandle(), &Resolved).Code,
      NEVERC_STATUS_WRONG_SCOPE);
  EXPECT_EQ(
      Other->resolveModule(First->moduleHandle(), &Resolved).Code,
      NEVERC_STATUS_WRONG_SESSION);
}

TEST(PluginIRHandleTest, ReusesValueHandlesAndPublishesStableKinds) {
  EmptyIRTask Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Created = IRPluginBridge::create(Scope.task(), "value-kinds");
  ASSERT_TRUE(static_cast<bool>(Created));
  std::unique_ptr<IRPluginBridge> Bridge = std::move(*Created);

  FunctionType *FunctionTy =
      FunctionType::get(Type::getVoidTy(Bridge->context()), false);
  Function *Fn = Function::Create(
      FunctionTy, GlobalValue::ExternalLinkage, "entry", Bridge->module());
  auto First = Bridge->wrapValue(*Fn);
  auto Second = Bridge->wrapValue(*Fn);
  ASSERT_TRUE(static_cast<bool>(First));
  ASSERT_TRUE(static_cast<bool>(Second));
  EXPECT_EQ(First->Owner, Second->Owner);
  EXPECT_EQ(First->Value, Second->Value);

  NevercIRValueKind Kind = NEVERC_IR_VALUE_UNKNOWN;
  EXPECT_EQ(Bridge->getValueKind(*First, &Kind).Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Kind, NEVERC_IR_VALUE_FUNCTION);
}

TEST(PluginIRHandleTest, ErasingAValueImmediatelyStalesItsHandle) {
  EmptyIRTask Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Created = IRPluginBridge::create(Scope.task(), "erase-value");
  ASSERT_TRUE(static_cast<bool>(Created));
  std::unique_ptr<IRPluginBridge> Bridge = std::move(*Created);

  FunctionType *FunctionTy =
      FunctionType::get(Type::getVoidTy(Bridge->context()), false);
  Function *Fn = Function::Create(
      FunctionTy, GlobalValue::ExternalLinkage, "entry", Bridge->module());
  BasicBlock *Block =
      BasicBlock::Create(Bridge->context(), "body", Fn);
  ReturnInst *Return = ReturnInst::Create(Bridge->context(), Block);
  auto Handle = Bridge->wrapValue(*Return);
  ASSERT_TRUE(static_cast<bool>(Handle));
  auto MetadataHandle = Bridge->getValueAsMetadata(*Handle);
  ASSERT_TRUE(static_cast<bool>(MetadataHandle));

  EXPECT_EQ(Bridge->eraseValue(*Handle).Code, NEVERC_STATUS_OK);
  llvm::Value *Resolved = nullptr;
  EXPECT_EQ(Bridge->resolveValue(*Handle, &Resolved).Code,
            NEVERC_STATUS_STALE_HANDLE);
  EXPECT_EQ(Resolved, nullptr);
  llvm::Metadata *ResolvedMetadata = nullptr;
  EXPECT_EQ(
      Bridge->resolveMetadata(*MetadataHandle, &ResolvedMetadata).Code,
      NEVERC_STATUS_STALE_HANDLE);
  EXPECT_EQ(ResolvedMetadata, nullptr);
}

TEST(PluginIRHandleTest, RAUWKeepsTheOriginalHandleBoundUntilErase) {
  EmptyIRTask Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Created = IRPluginBridge::create(Scope.task(), "rauw-value");
  ASSERT_TRUE(static_cast<bool>(Created));
  std::unique_ptr<IRPluginBridge> Bridge = std::move(*Created);

  Type *I32 = Type::getInt32Ty(Bridge->context());
  FunctionType *FunctionTy = FunctionType::get(I32, false);
  Function *Fn = Function::Create(
      FunctionTy, GlobalValue::ExternalLinkage, "entry", Bridge->module());
  BasicBlock *Block =
      BasicBlock::Create(Bridge->context(), "body", Fn);
  auto *Original = BinaryOperator::CreateAdd(
      ConstantInt::get(I32, 1), ConstantInt::get(I32, 2), "original",
      Block);
  ReturnInst *Return = ReturnInst::Create(
      Bridge->context(), Original, Block);
  Constant *Replacement = ConstantInt::get(I32, 42);
  auto OriginalHandle = Bridge->wrapValue(*Original);
  auto ReplacementHandle = Bridge->wrapValue(*Replacement);
  ASSERT_TRUE(static_cast<bool>(OriginalHandle));
  ASSERT_TRUE(static_cast<bool>(ReplacementHandle));
  auto MetadataHandle = Bridge->getValueAsMetadata(*OriginalHandle);
  ASSERT_TRUE(static_cast<bool>(MetadataHandle));

  EXPECT_EQ(Bridge
                ->replaceAllUsesWith(*OriginalHandle, *ReplacementHandle)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Return->getReturnValue(), Replacement);
  llvm::Value *Resolved = nullptr;
  EXPECT_EQ(Bridge->resolveValue(*OriginalHandle, &Resolved).Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Resolved, Original);
  llvm::Metadata *ResolvedMetadata = nullptr;
  EXPECT_EQ(
      Bridge->resolveMetadata(*MetadataHandle, &ResolvedMetadata).Code,
      NEVERC_STATUS_STALE_HANDLE);
  EXPECT_EQ(Bridge->eraseValue(*OriginalHandle).Code, NEVERC_STATUS_OK);
  EXPECT_EQ(Bridge->resolveValue(*OriginalHandle, &Resolved).Code,
            NEVERC_STATUS_STALE_HANDLE);
}

TEST(PluginIRHandleTest, TypeHandlesAreContextCheckedAndCanonical) {
  EmptyIRTask Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Created = IRPluginBridge::create(Scope.task(), "type-handles");
  ASSERT_TRUE(static_cast<bool>(Created));
  std::unique_ptr<IRPluginBridge> Bridge = std::move(*Created);

  Type *I64 = Type::getInt64Ty(Bridge->context());
  auto First = Bridge->wrapType(*I64);
  auto Second = Bridge->wrapType(*I64);
  ASSERT_TRUE(static_cast<bool>(First));
  ASSERT_TRUE(static_cast<bool>(Second));
  EXPECT_EQ(First->Owner, Second->Owner);
  EXPECT_EQ(First->Value, Second->Value);

  Type *Resolved = nullptr;
  EXPECT_EQ(Bridge->resolveType(*First, &Resolved).Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Resolved, I64);
  EXPECT_EQ(Bridge->resolveType(Bridge->moduleHandle(), &Resolved).Code,
            NEVERC_STATUS_WRONG_TYPE);

  LLVMContext ForeignContext;
  auto Foreign = Bridge->wrapType(*Type::getInt64Ty(ForeignContext));
  EXPECT_FALSE(static_cast<bool>(Foreign));
  consumeError(Foreign.takeError());
}

TEST(PluginIRHandleTest, ModuleTeardownInvalidatesEveryChildHandle) {
  EmptyIRTask Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Created = IRPluginBridge::create(Scope.task(), "module-teardown");
  ASSERT_TRUE(static_cast<bool>(Created));
  std::unique_ptr<IRPluginBridge> Bridge = std::move(*Created);

  Type *I8 = Type::getInt8Ty(Bridge->context());
  FunctionType *FunctionTy =
      FunctionType::get(Type::getVoidTy(Bridge->context()), false);
  Function *Fn = Function::Create(
      FunctionTy, GlobalValue::ExternalLinkage, "entry", Bridge->module());
  auto TypeHandle = Bridge->wrapType(*I8);
  auto ValueHandle = Bridge->wrapValue(*Fn);
  auto MetadataHandle = Bridge->getMetadataString("module metadata");
  ASSERT_TRUE(static_cast<bool>(MetadataHandle));
  auto MetadataNode = Bridge->getMetadataNode({*MetadataHandle}, false);
  ASSERT_TRUE(static_cast<bool>(MetadataNode));
  auto NamedMetadata = Bridge->getOrInsertNamedMetadata("neverc.teardown");
  ASSERT_TRUE(static_cast<bool>(NamedMetadata));
  ASSERT_EQ(
      Bridge->appendNamedMetadata(*NamedMetadata, *MetadataNode).Code,
      NEVERC_STATUS_OK);
  auto AttributeHandle = Bridge->createEnumAttribute("nounwind");
  ASSERT_TRUE(static_cast<bool>(TypeHandle));
  ASSERT_TRUE(static_cast<bool>(ValueHandle));
  ASSERT_TRUE(static_cast<bool>(AttributeHandle));
  NevercIRContextHandle ContextHandle = Bridge->contextHandle();
  NevercIRModuleHandle ModuleHandle = Bridge->moduleHandle();

  Bridge.reset();
  void *Resolved = nullptr;
  EXPECT_EQ(Scope.task()
                .handles()
                .resolve(*ValueHandle, PluginIRValueHandleKind, &Resolved)
                .Code,
            NEVERC_STATUS_STALE_HANDLE);
  EXPECT_EQ(Scope.task()
                .handles()
                .resolve(*TypeHandle, PluginIRTypeHandleKind, &Resolved)
                .Code,
            NEVERC_STATUS_STALE_HANDLE);
  EXPECT_EQ(Scope.task()
                .handles()
                .resolve(*MetadataHandle, PluginIRMetadataHandleKind,
                         &Resolved)
                .Code,
            NEVERC_STATUS_STALE_HANDLE);
  EXPECT_EQ(Scope.task()
                .handles()
                .resolve(*NamedMetadata,
                         PluginIRNamedMetadataHandleKind, &Resolved)
                .Code,
            NEVERC_STATUS_STALE_HANDLE);
  EXPECT_EQ(Scope.task()
                .handles()
                .resolve(*AttributeHandle, PluginIRAttributeHandleKind,
                         &Resolved)
                .Code,
            NEVERC_STATUS_STALE_HANDLE);
  EXPECT_EQ(Scope.task()
                .handles()
                .resolve(ModuleHandle, PluginIRModuleHandleKind, &Resolved)
                .Code,
            NEVERC_STATUS_STALE_HANDLE);
  EXPECT_EQ(Scope.task()
                .handles()
                .resolve(ContextHandle, PluginIRContextHandleKind, &Resolved)
                .Code,
            NEVERC_STATUS_STALE_HANDLE);
}

TEST(PluginIRHandleTest, ImportedLTOModuleGetsIndependentOwnership) {
  EmptyIRTask Scope;
  ASSERT_TRUE(Scope.initialize());
  PluginTaskContext *LTOTask =
      Scope.createTask(NEVERC_TASK_LTO, &Scope.task());
  ASSERT_NE(LTOTask, nullptr);

  auto SourceCreated =
      IRPluginBridge::create(Scope.task(), "source-module");
  ASSERT_TRUE(static_cast<bool>(SourceCreated));
  std::unique_ptr<IRPluginBridge> Source = std::move(*SourceCreated);
  FunctionType *FunctionTy =
      FunctionType::get(Type::getVoidTy(Source->context()), false);
  Function::Create(FunctionTy, GlobalValue::ExternalLinkage, "entry",
                   Source->module());

  auto ImportedContext = std::make_unique<LLVMContext>();
  auto ImportedModule =
      std::make_unique<Module>("lto-import", *ImportedContext);
  FunctionType *ImportedFunctionTy =
      FunctionType::get(Type::getVoidTy(*ImportedContext), false);
  Function::Create(ImportedFunctionTy, GlobalValue::ExternalLinkage, "entry",
                   *ImportedModule);
  auto ImportedCreated = IRPluginBridge::adopt(
      *LTOTask, std::move(ImportedContext), std::move(ImportedModule));
  ASSERT_TRUE(static_cast<bool>(ImportedCreated))
      << errorMessage(ImportedCreated.takeError());
  std::unique_ptr<IRPluginBridge> Imported = std::move(*ImportedCreated);

  EXPECT_NE(Source->moduleHandle().Owner, Imported->moduleHandle().Owner);
  EXPECT_NE(&Source->module(), &Imported->module());
  EXPECT_NE(&Source->context(), &Imported->context());
  Module *Resolved = nullptr;
  EXPECT_EQ(
      Imported->resolveModule(Source->moduleHandle(), &Resolved).Code,
      NEVERC_STATUS_WRONG_SCOPE);
  EXPECT_EQ(
      Source->resolveModule(Source->moduleHandle(), &Resolved).Code,
      NEVERC_STATUS_OK);
  EXPECT_EQ(Resolved, &Source->module());
}

} // namespace
