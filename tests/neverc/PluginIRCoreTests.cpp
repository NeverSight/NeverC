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
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalIFunc.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Error.h"
#include <array>
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

class IRCoreScope {
public:
  IRCoreScope()
      : Services("neverc-plugin-ir-core-tests", LLVM_VERSION_MAJOR) {}

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
    auto CreatedBridge = IRPluginBridge::create(*Task, "ir-core");
    if (!CreatedBridge) {
      ADD_FAILURE() << errorMessage(CreatedBridge.takeError());
      return false;
    }
    Bridge = std::move(*CreatedBridge);
    return true;
  }

  ~IRCoreScope() {
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

NevercIRPropertyValue propertyValue() {
  NevercIRPropertyValue Value{};
  Value.Header.StructSize = sizeof(Value);
  Value.Header.Major = NEVERC_IR_CORE_API_MAJOR;
  Value.Header.Minor = NEVERC_IR_CORE_API_MINOR;
  return Value;
}

TEST(PluginIRCoreTest, ReadsWritesModuleAndEnumeratesEveryGlobalFamily) {
  IRCoreScope Scope;
  ASSERT_TRUE(Scope.initialize());
  IRPluginBridge &Bridge = Scope.bridge();

  EXPECT_EQ(Bridge.setModuleIdentifier("renamed").Code, NEVERC_STATUS_OK);
  EXPECT_EQ(Bridge.setModuleTargetTriple("x86_64-unknown-linux-gnu").Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Bridge.setModuleDataLayout("e-p:64:64-i64:64-n8:16:32:64").Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Bridge.setModuleInlineAssembly(".byte 0x90").Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Bridge.getModuleIdentifier(), "renamed");
  EXPECT_EQ(Bridge.getModuleTargetTriple(), "x86_64-unknown-linux-gnu");
  EXPECT_EQ(Bridge.getModuleDataLayout(),
            "e-p:64:64-i64:64-n8:16:32:64");
  EXPECT_EQ(Bridge.getModuleInlineAssembly(), ".byte 0x90\n");

  Type *I32 = Type::getInt32Ty(Bridge.context());
  FunctionType *FnTy = FunctionType::get(I32, false);
  Function *Fn = Function::Create(FnTy, GlobalValue::ExternalLinkage, "fn",
                                  Bridge.module());
  auto *Global = new GlobalVariable(
      Bridge.module(), I32, false, GlobalValue::ExternalLinkage,
      ConstantInt::get(I32, 1), "global");
  GlobalAlias::create(I32, 0, GlobalValue::ExternalLinkage, "alias", Global,
                      &Bridge.module());
  GlobalIFunc::create(I32, 0, GlobalValue::ExternalLinkage, "ifunc", Fn,
                      &Bridge.module());

  const std::array<std::pair<NevercIRValueCollection, uint64_t>, 4>
      Collections = {{
          {NEVERC_IR_COLLECTION_MODULE_FUNCTIONS, 1},
          {NEVERC_IR_COLLECTION_MODULE_GLOBALS, 1},
          {NEVERC_IR_COLLECTION_MODULE_ALIASES, 1},
          {NEVERC_IR_COLLECTION_MODULE_I_FUNCS, 1},
      }};
  for (const auto &[Collection, ExpectedCount] : Collections) {
    NevercIRValueCursor Cursor{};
    ASSERT_EQ(Bridge.beginValueCursor(Bridge.moduleHandle(), Collection,
                                      &Cursor)
                  .Code,
              NEVERC_STATUS_OK);
    std::array<NevercIRValueHandle, 2> Values{};
    uint64_t Count = 0;
    EXPECT_EQ(Bridge.collectValueCursor(&Cursor, Values, &Count).Code,
              NEVERC_STATUS_OK);
    EXPECT_EQ(Count, ExpectedCount);
    EXPECT_EQ(Bridge.collectValueCursor(&Cursor, Values, &Count).Code,
              NEVERC_STATUS_OK);
    EXPECT_EQ(Count, 0U);
  }
}

TEST(PluginIRCoreTest, TraversesFunctionsCFGOperandsUsesAndPhiIncomingEdges) {
  IRCoreScope Scope;
  ASSERT_TRUE(Scope.initialize());
  IRPluginBridge &Bridge = Scope.bridge();

  Type *I32 = Type::getInt32Ty(Bridge.context());
  FunctionType *FnTy = FunctionType::get(I32, {I32}, false);
  Function *Fn = Function::Create(FnTy, GlobalValue::ExternalLinkage, "work",
                                  Bridge.module());
  Argument *Arg = Fn->getArg(0);
  Arg->setName("input");
  BasicBlock *Entry = BasicBlock::Create(Bridge.context(), "entry", Fn);
  BasicBlock *Merge = BasicBlock::Create(Bridge.context(), "merge", Fn);
  auto *Add = BinaryOperator::CreateAdd(
      Arg, ConstantInt::get(I32, 1), "sum", Entry);
  BranchInst::Create(Merge, Entry);
  auto *Phi = PHINode::Create(I32, 1, "joined", Merge);
  Phi->addIncoming(Add, Entry);
  ReturnInst *Ret = ReturnInst::Create(Bridge.context(), Phi, Merge);

  auto FnHandle = Bridge.wrapValue(*Fn);
  auto ArgHandle = Bridge.wrapValue(*Arg);
  auto EntryHandle = Bridge.wrapValue(*Entry);
  auto MergeHandle = Bridge.wrapValue(*Merge);
  auto AddHandle = Bridge.wrapValue(*Add);
  auto PhiHandle = Bridge.wrapValue(*Phi);
  auto RetHandle = Bridge.wrapValue(*Ret);
  ASSERT_TRUE(static_cast<bool>(FnHandle));
  ASSERT_TRUE(static_cast<bool>(ArgHandle));
  ASSERT_TRUE(static_cast<bool>(EntryHandle));
  ASSERT_TRUE(static_cast<bool>(MergeHandle));
  ASSERT_TRUE(static_cast<bool>(AddHandle));
  ASSERT_TRUE(static_cast<bool>(PhiHandle));
  ASSERT_TRUE(static_cast<bool>(RetHandle));

  auto Name = Bridge.getValueName(*AddHandle);
  ASSERT_TRUE(static_cast<bool>(Name));
  EXPECT_EQ(*Name, "sum");
  EXPECT_EQ(Bridge.setValueName(*AddHandle, "total").Code, NEVERC_STATUS_OK);
  EXPECT_EQ(Add->getName(), "total");

  auto TypeHandle = Bridge.getValueType(*AddHandle);
  ASSERT_TRUE(static_cast<bool>(TypeHandle));
  Type *ResolvedType = nullptr;
  EXPECT_EQ(Bridge.resolveType(*TypeHandle, &ResolvedType).Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(ResolvedType, I32);

  uint64_t OperandCount = 0;
  EXPECT_EQ(Bridge.getOperandCount(*AddHandle, &OperandCount).Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(OperandCount, 2U);
  auto FirstOperand = Bridge.getOperand(*AddHandle, 0);
  ASSERT_TRUE(static_cast<bool>(FirstOperand));
  EXPECT_EQ(FirstOperand->Owner, ArgHandle->Owner);
  EXPECT_EQ(FirstOperand->Value, ArgHandle->Value);

  auto Two = Bridge.wrapValue(*ConstantInt::get(I32, 2));
  ASSERT_TRUE(static_cast<bool>(Two));
  EXPECT_EQ(Bridge.setOperand(*AddHandle, 1, *Two).Code, NEVERC_STATUS_OK);
  EXPECT_EQ(cast<ConstantInt>(Add->getOperand(1))->getZExtValue(), 2U);

  uint64_t UseCount = 0;
  EXPECT_EQ(Bridge.getValueUseCount(*PhiHandle, &UseCount).Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(UseCount, 1U);
  NevercIRUseInfo Use{};
  Use.Header.StructSize = sizeof(Use);
  EXPECT_EQ(Bridge.getValueUse(*PhiHandle, 0, &Use).Code, NEVERC_STATUS_OK);
  EXPECT_EQ(Use.User.Owner, RetHandle->Owner);
  EXPECT_EQ(Use.User.Value, RetHandle->Value);
  EXPECT_EQ(Use.OperandIndex, 0U);

  uint64_t IncomingCount = 0;
  EXPECT_EQ(Bridge.getPHIIncomingCount(*PhiHandle, &IncomingCount).Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(IncomingCount, 1U);
  NevercIRPhiIncoming Incoming{};
  Incoming.Header.StructSize = sizeof(Incoming);
  EXPECT_EQ(Bridge.getPHIIncoming(*PhiHandle, 0, &Incoming).Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Incoming.Value.Value, AddHandle->Value);
  EXPECT_EQ(Incoming.Block.Value, EntryHandle->Value);

  uint64_t Predecessors = 0;
  uint64_t Successors = 0;
  EXPECT_EQ(Bridge.getPredecessorCount(*MergeHandle, &Predecessors).Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Bridge.getSuccessorCount(*EntryHandle, &Successors).Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Predecessors, 1U);
  EXPECT_EQ(Successors, 1U);
  EXPECT_EQ(Bridge.getPredecessor(*MergeHandle, 0)->Value,
            EntryHandle->Value);
  EXPECT_EQ(Bridge.getSuccessor(*EntryHandle, 0)->Value,
            MergeHandle->Value);
  EXPECT_EQ(Bridge.getTerminator(*MergeHandle)->Value, RetHandle->Value);

  NevercIRValueCursor Cursor{};
  ASSERT_EQ(Bridge.beginValueCursor(*FnHandle,
                                    NEVERC_IR_COLLECTION_FUNCTION_ARGUMENTS,
                                    &Cursor)
                .Code,
            NEVERC_STATUS_OK);
  std::array<NevercIRValueHandle, 4> Values{};
  uint64_t Count = 0;
  EXPECT_EQ(Bridge.collectValueCursor(&Cursor, Values, &Count).Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Count, 1U);
  EXPECT_EQ(Values[0].Value, ArgHandle->Value);
}

TEST(PluginIRCoreTest, ReadsWritesGlobalAndFunctionState) {
  IRCoreScope Scope;
  ASSERT_TRUE(Scope.initialize());
  IRPluginBridge &Bridge = Scope.bridge();

  Type *I8 = Type::getInt8Ty(Bridge.context());
  FunctionType *FnTy = FunctionType::get(I8, false);
  Function *Fn = Function::Create(FnTy, GlobalValue::ExternalLinkage, "fn",
                                  Bridge.module());
  Function *Personality = Function::Create(
      FunctionType::get(Type::getInt32Ty(Bridge.context()), true),
      GlobalValue::ExternalLinkage, "personality", Bridge.module());
  auto *Global = new GlobalVariable(
      Bridge.module(), I8, false, GlobalValue::ExternalLinkage,
      ConstantInt::get(I8, 0), "global");
  auto FnHandle = Bridge.wrapValue(*Fn);
  auto PersonalityHandle = Bridge.wrapValue(*Personality);
  auto GlobalHandle = Bridge.wrapValue(*Global);
  ASSERT_TRUE(static_cast<bool>(FnHandle));
  ASSERT_TRUE(static_cast<bool>(PersonalityHandle));
  ASSERT_TRUE(static_cast<bool>(GlobalHandle));

  EXPECT_EQ(Bridge.setGlobalLinkage(*GlobalHandle,
                                    NEVERC_IR_LINKAGE_INTERNAL)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Bridge.setGlobalVisibility(*GlobalHandle,
                                       NEVERC_IR_VISIBILITY_HIDDEN)
                .Code,
            NEVERC_STATUS_INVALID_STATE);
  EXPECT_EQ(Bridge.setGlobalLinkage(*GlobalHandle,
                                    NEVERC_IR_LINKAGE_EXTERNAL)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Bridge.setGlobalVisibility(*GlobalHandle,
                                       NEVERC_IR_VISIBILITY_HIDDEN)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Bridge.setGlobalSection(*GlobalHandle, ".plugin").Code,
            NEVERC_STATUS_OK);
  auto Comdat = Bridge.getOrInsertComdat("plugin.group");
  ASSERT_TRUE(static_cast<bool>(Comdat));
  EXPECT_EQ(Bridge.setGlobalComdat(*GlobalHandle, *Comdat).Code,
            NEVERC_STATUS_OK);
  NevercIRLinkage Linkage = NEVERC_IR_LINKAGE_UNKNOWN;
  NevercIRVisibility Visibility = NEVERC_IR_VISIBILITY_UNKNOWN;
  EXPECT_EQ(Bridge.getGlobalLinkage(*GlobalHandle, &Linkage).Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Bridge.getGlobalVisibility(*GlobalHandle, &Visibility).Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Linkage, NEVERC_IR_LINKAGE_EXTERNAL);
  EXPECT_EQ(Visibility, NEVERC_IR_VISIBILITY_HIDDEN);
  EXPECT_EQ(*Bridge.getGlobalSection(*GlobalHandle), ".plugin");
  EXPECT_EQ(Bridge.getGlobalComdat(*GlobalHandle)->Value, Comdat->Value);

  EXPECT_EQ(Bridge.setFunctionCallingConvention(
                *FnHandle, NEVERC_IR_CALLING_CONVENTION_FAST)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Bridge.setFunctionPersonality(*FnHandle, *PersonalityHandle).Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Bridge.setFunctionGC(*FnHandle, "plugin-gc").Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Bridge.setFunctionSection(*FnHandle, ".text.plugin").Code,
            NEVERC_STATUS_OK);
  NevercIRCallingConvention CallingConvention =
      NEVERC_IR_CALLING_CONVENTION_UNKNOWN;
  EXPECT_EQ(Bridge.getFunctionCallingConvention(*FnHandle,
                                                 &CallingConvention)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(CallingConvention, NEVERC_IR_CALLING_CONVENTION_FAST);
  EXPECT_EQ(Bridge.getFunctionPersonality(*FnHandle)->Value,
            PersonalityHandle->Value);
  EXPECT_EQ(*Bridge.getFunctionGC(*FnHandle), "plugin-gc");
  EXPECT_EQ(*Bridge.getFunctionSection(*FnHandle), ".text.plugin");
}

TEST(PluginIRCoreTest, AppliesSchemaCheckedInstructionProperties) {
  IRCoreScope Scope;
  ASSERT_TRUE(Scope.initialize());
  IRPluginBridge &Bridge = Scope.bridge();

  Type *I32 = Type::getInt32Ty(Bridge.context());
  Function *Fn = Function::Create(
      FunctionType::get(I32, false), GlobalValue::ExternalLinkage, "flags",
      Bridge.module());
  BasicBlock *Block = BasicBlock::Create(Bridge.context(), "entry", Fn);
  auto *Add = BinaryOperator::CreateAdd(ConstantInt::get(I32, 1),
                                        ConstantInt::get(I32, 2), "sum",
                                        Block);
  auto *Cmp = new ICmpInst(*Block, CmpInst::ICMP_EQ, Add,
                           ConstantInt::get(I32, 3), "cmp");
  auto *GEP = GetElementPtrInst::Create(
      I32, ConstantPointerNull::get(PointerType::get(Bridge.context(), 0)),
      ConstantInt::get(I32, 1), "gep", Block);
  ReturnInst::Create(Bridge.context(), Add, Block);
  auto FnHandle = Bridge.wrapValue(*Fn);
  auto AddHandle = Bridge.wrapValue(*Add);
  auto CmpHandle = Bridge.wrapValue(*Cmp);
  auto GEPHandle = Bridge.wrapValue(*GEP);
  ASSERT_TRUE(static_cast<bool>(FnHandle));
  ASSERT_TRUE(static_cast<bool>(AddHandle));
  ASSERT_TRUE(static_cast<bool>(CmpHandle));
  ASSERT_TRUE(static_cast<bool>(GEPHandle));

  NevercIROpcode Opcode = NEVERC_IR_OPCODE_UNKNOWN;
  EXPECT_EQ(Bridge.getInstructionOpcode(*AddHandle, &Opcode).Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Opcode, NEVERC_IR_OPCODE_ADD);

  NevercIRPropertyValue Value = propertyValue();
  Value.Kind = NEVERC_IR_PROPERTY_VALUE_BOOL;
  Value.UnsignedValue = NEVERC_TRUE;
  EXPECT_EQ(Bridge.setInstructionProperty(*AddHandle, NEVERC_IR_PROPERTY_NSW,
                                          Value)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_TRUE(Add->hasNoSignedWrap());

  Value = propertyValue();
  EXPECT_EQ(Bridge.getInstructionProperty(*AddHandle, NEVERC_IR_PROPERTY_NSW,
                                          &Value)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Value.Kind, NEVERC_IR_PROPERTY_VALUE_BOOL);
  EXPECT_EQ(Value.UnsignedValue, NEVERC_TRUE);

  Value = propertyValue();
  Value.Kind = NEVERC_IR_PROPERTY_VALUE_ENUM;
  Value.UnsignedValue = NEVERC_IR_PREDICATE_ICMP_SGT;
  EXPECT_EQ(Bridge.setInstructionProperty(
                *CmpHandle, NEVERC_IR_PROPERTY_PREDICATE, Value)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Cmp->getPredicate(), CmpInst::ICMP_SGT);

  Value = propertyValue();
  Value.Kind = NEVERC_IR_PROPERTY_VALUE_BOOL;
  Value.UnsignedValue = NEVERC_TRUE;
  EXPECT_EQ(Bridge.setInstructionProperty(
                *GEPHandle, NEVERC_IR_PROPERTY_NUSW, Value)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_TRUE(GEP->getNoWrapFlags().hasNoUnsignedSignedWrap());
  EXPECT_EQ(Bridge.setInstructionProperty(
                *GEPHandle, NEVERC_IR_PROPERTY_NUW, Value)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_TRUE(GEP->hasNoUnsignedWrap());
  EXPECT_EQ(Bridge.setInstructionProperty(
                *GEPHandle, NEVERC_IR_PROPERTY_NSW, Value)
                .Code,
            NEVERC_STATUS_WRONG_TYPE);

  Value = propertyValue();
  Value.Kind = NEVERC_IR_PROPERTY_VALUE_BOOL;
  Value.UnsignedValue = NEVERC_TRUE;
  EXPECT_EQ(Bridge.setInstructionProperty(
                *AddHandle, NEVERC_IR_PROPERTY_VOLATILE, Value)
                .Code,
            NEVERC_STATUS_WRONG_TYPE);
  EXPECT_EQ(Bridge.getInstructionProperty(
                *FnHandle, NEVERC_IR_PROPERTY_NSW, &Value)
                .Code,
            NEVERC_STATUS_WRONG_TYPE);
}

TEST(PluginIRCoreTest, MutationInvalidatesCursorsAndUsedValuesCannotErase) {
  IRCoreScope Scope;
  ASSERT_TRUE(Scope.initialize());
  IRPluginBridge &Bridge = Scope.bridge();

  Type *I32 = Type::getInt32Ty(Bridge.context());
  Function *Fn = Function::Create(
      FunctionType::get(I32, false), GlobalValue::ExternalLinkage, "cursor",
      Bridge.module());
  BasicBlock *Block = BasicBlock::Create(Bridge.context(), "entry", Fn);
  auto *Add = BinaryOperator::CreateAdd(ConstantInt::get(I32, 1),
                                        ConstantInt::get(I32, 2), "sum",
                                        Block);
  ReturnInst::Create(Bridge.context(), Add, Block);
  auto FnHandle = Bridge.wrapValue(*Fn);
  auto AddHandle = Bridge.wrapValue(*Add);
  ASSERT_TRUE(static_cast<bool>(FnHandle));
  ASSERT_TRUE(static_cast<bool>(AddHandle));

  NevercIRValueCursor Cursor{};
  ASSERT_EQ(Bridge.beginValueCursor(*FnHandle,
                                    NEVERC_IR_COLLECTION_FUNCTION_BLOCKS,
                                    &Cursor)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Bridge.setValueName(*FnHandle, "renamed").Code,
            NEVERC_STATUS_OK);
  std::array<NevercIRValueHandle, 1> Values{};
  uint64_t Count = 0;
  EXPECT_EQ(Bridge.collectValueCursor(&Cursor, Values, &Count).Code,
            NEVERC_STATUS_STALE_HANDLE);
  EXPECT_EQ(Bridge.eraseValue(*AddHandle).Code, NEVERC_STATUS_INVALID_STATE);
}

TEST(PluginIRCoreTest, PublishesTraversalThroughThePureCAPITable) {
  IRCoreScope Scope;
  ASSERT_TRUE(Scope.initialize());
  IRPluginBridge &Bridge = Scope.bridge();
  const NevercIRCoreAPI &API = Bridge.coreAPI();

  ASSERT_NE(API.SetModuleIdentifier, nullptr);
  ASSERT_NE(API.BeginValueCursor, nullptr);
  ASSERT_NE(API.CollectValueCursor, nullptr);
  const char Identifier[] = "c-api-module";
  EXPECT_EQ(API.SetModuleIdentifier(
                API.Context, Scope.taskHandle(),
                NevercStringView{Identifier, sizeof(Identifier) - 1})
                .Code,
            NEVERC_STATUS_OK);
  NevercStringView ReadIdentifier{};
  EXPECT_EQ(API.GetModuleIdentifier(API.Context, Scope.taskHandle(),
                                    &ReadIdentifier)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(StringRef(ReadIdentifier.Data, ReadIdentifier.Length),
            "c-api-module");

  Type *I32 = Type::getInt32Ty(Bridge.context());
  Function::Create(FunctionType::get(I32, false),
                   GlobalValue::ExternalLinkage, "listed", Bridge.module());
  NevercIRValueCursor Cursor{};
  EXPECT_EQ(API.BeginValueCursor(
                API.Context, Scope.taskHandle(), Bridge.moduleHandle(),
                NEVERC_IR_COLLECTION_MODULE_FUNCTIONS, &Cursor)
                .Code,
            NEVERC_STATUS_OK);
  NevercIRValueHandle Value{};
  uint64_t Count = 0;
  EXPECT_EQ(API.CollectValueCursor(API.Context, Scope.taskHandle(), &Cursor,
                                   &Value, 1, &Count)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Count, 1U);
}

TEST(PluginIRCoreTest,
     RoundTripsModuleTargetLayoutAndInlineAssemblyThroughTheCAPI) {
  IRCoreScope Scope;
  ASSERT_TRUE(Scope.initialize());
  const NevercIRCoreAPI &API = Scope.bridge().coreAPI();

  auto SetString = [&](auto Setter, StringRef Value) {
    return Setter(API.Context, Scope.taskHandle(),
                  NevercStringView{Value.data(), Value.size()});
  };
  auto ExpectString = [&](auto Getter, StringRef Expected) {
    NevercStringView Value{};
    ASSERT_EQ(Getter(API.Context, Scope.taskHandle(), &Value).Code,
              NEVERC_STATUS_OK);
    EXPECT_EQ(StringRef(Value.Data, Value.Length), Expected);
  };

  ASSERT_EQ(SetString(API.SetModuleTargetTriple, "x86_64-pc-linux-gnu").Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(SetString(API.SetModuleDataLayout,
                      "e-m:e-p:64:64-i64:64-n8:16:32:64-S128")
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(SetString(API.SetModuleInlineAssembly, ".globl plugin_symbol\n")
                .Code,
            NEVERC_STATUS_OK);

  ExpectString(API.GetModuleTargetTriple, "x86_64-pc-linux-gnu");
  ExpectString(API.GetModuleDataLayout,
               "e-m:e-p:64:64-i64:64-n8:16:32:64-S128");
  ExpectString(API.GetModuleInlineAssembly, ".globl plugin_symbol\n");

  EXPECT_EQ(SetString(API.SetModuleDataLayout, "not-a-data-layout").Code,
            NEVERC_STATUS_INVALID_ARGUMENT);
  ExpectString(API.GetModuleDataLayout,
               "e-m:e-p:64:64-i64:64-n8:16:32:64-S128");
}

} // namespace
