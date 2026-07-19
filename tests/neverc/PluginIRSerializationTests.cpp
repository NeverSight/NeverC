#include "IRModuleArtifact.h"
#include "neverc/Plugin/Host/IRPluginBridge.h"
#include "neverc/Plugin/Host/PluginArtifactRegistry.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

using namespace neverc::plugin;

namespace {

std::string errorMessage(llvm::Error Error) {
  return llvm::toString(std::move(Error)).str().str();
}

struct SerializationEnvironment {
  PluginProcessServices Process;
  std::optional<PluginActivationPlan> Plan;

  SerializationEnvironment()
      : Process("neverc-plugin-ir-serialization-tests", LLVM_VERSION_MAJOR) {
    if (llvm::Error Error = Process.interfaces().freeze()) {
      ADD_FAILURE() << errorMessage(std::move(Error));
      return;
    }
    auto PlanOrErr = makePluginActivationPlan(Process.registry(), {});
    if (!PlanOrErr) {
      ADD_FAILURE() << errorMessage(PlanOrErr.takeError());
      return;
    }
    Plan.emplace(std::move(*PlanOrErr));
  }

  ~SerializationEnvironment() {
    Plan.reset();
    EXPECT_FALSE(Process.shutdown());
  }
};

struct SerializationRuntime {
  SerializationEnvironment &Environment;
  std::unique_ptr<PluginSession> Session;
  std::unique_ptr<PluginTaskContext> Task;
  std::unique_ptr<IRPluginBridge> Bridge;

  SerializationRuntime(SerializationEnvironment &Environment,
                       llvm::StringRef Identifier)
      : Environment(Environment) {
    if (!Environment.Plan)
      return;
    auto SessionOrErr =
        PluginSession::create(Environment.Process, *Environment.Plan);
    if (!SessionOrErr) {
      ADD_FAILURE() << errorMessage(SessionOrErr.takeError());
      return;
    }
    Session = std::move(*SessionOrErr);

    auto TaskOrErr = Session->createTask(NEVERC_TASK_TRANSLATION_UNIT);
    if (!TaskOrErr) {
      ADD_FAILURE() << errorMessage(TaskOrErr.takeError());
      return;
    }
    Task = std::move(*TaskOrErr);

    auto BridgeOrErr = IRPluginBridge::create(*Task, Identifier);
    if (!BridgeOrErr) {
      ADD_FAILURE() << errorMessage(BridgeOrErr.takeError());
      return;
    }
    Bridge = std::move(*BridgeOrErr);
  }

  ~SerializationRuntime() {
    Bridge.reset();
    if (Task)
      EXPECT_FALSE(Task->end());
    if (Session)
      EXPECT_FALSE(Session->end());
  }
};

std::vector<uint8_t> serializeBitcode(llvm::StringRef Identifier,
                                      llvm::StringRef Triple,
                                      llvm::StringRef DataLayout) {
  llvm::LLVMContext Context;
  llvm::Module Module(Identifier, Context);
  Module.setTargetTriple(Triple);
  Module.setDataLayout(DataLayout);
  llvm::FunctionType *Type =
      llvm::FunctionType::get(llvm::Type::getInt32Ty(Context), false);
  llvm::Function *Function = llvm::Function::Create(
      Type, llvm::GlobalValue::ExternalLinkage, "answer", Module);
  llvm::BasicBlock *Entry =
      llvm::BasicBlock::Create(Context, "entry", Function);
  llvm::IRBuilder<> Builder(Entry);
  Builder.CreateRet(llvm::ConstantInt::get(llvm::Type::getInt32Ty(Context), 42));

  llvm::SmallVector<char, 0> Storage;
  llvm::raw_svector_ostream Stream(Storage);
  llvm::WriteBitcodeToFile(Module, Stream);
  return std::vector<uint8_t>(Storage.begin(), Storage.end());
}

NevercByteView bytesOf(const std::vector<uint8_t> &Bytes) {
  NevercByteView View{};
  View.Data = Bytes.data();
  View.Length = Bytes.size();
  return View;
}

std::unique_ptr<IRModuleArtifact>
makeModuleArtifact(std::shared_ptr<IRPluginBridge> Bridge) {
  auto Artifact = std::make_unique<IRModuleArtifact>();
  Artifact->Bridge = std::move(Bridge);
  Artifact->Product = standardIRModuleProductID();
  Artifact->TargetTriple = Artifact->Bridge->module().getTargetTriple();
  Artifact->DataLayout = Artifact->Bridge->module().getDataLayoutStr();
  Artifact->Generation = Artifact->Bridge->mutationGeneration();
  Artifact->DependencyDigest.fill(0xA5);
  Artifact->HasDependencyDigest = true;
  return Artifact;
}

} // namespace

TEST(PluginIRSerializationTest,
     BitcodeReplacementIsAtomicAndInvalidatesOldModuleHandles) {
  SerializationEnvironment Environment;
  SerializationRuntime Runtime(Environment, "serialization");
  ASSERT_NE(Runtime.Bridge, nullptr);
  constexpr llvm::StringLiteral Triple("x86_64-unknown-linux-gnu");
  constexpr llvm::StringLiteral Layout("e-p:64:64");
  Runtime.Bridge->module().setTargetTriple(Triple);
  Runtime.Bridge->module().setDataLayout(Layout);

  llvm::FunctionType *AnswerType = llvm::FunctionType::get(
      llvm::Type::getInt32Ty(Runtime.Bridge->context()), false);
  llvm::Function *Answer = llvm::Function::Create(
      AnswerType, llvm::GlobalValue::ExternalLinkage, "answer",
      Runtime.Bridge->module());
  llvm::BasicBlock *Entry =
      llvm::BasicBlock::Create(Runtime.Bridge->context(), "entry", Answer);
  llvm::IRBuilder<> AnswerBuilder(Entry);
  AnswerBuilder.CreateRet(llvm::ConstantInt::get(
      llvm::Type::getInt32Ty(Runtime.Bridge->context()), 42));

  auto BufferOrErr =
      Runtime.Bridge->exportModule(NEVERC_IR_SERIALIZATION_BITCODE);
  ASSERT_TRUE(static_cast<bool>(BufferOrErr));
  NevercByteView ExportedView{};
  ASSERT_EQ(Runtime.Bridge
                ->getSerializedBufferView(*BufferOrErr, &ExportedView)
                .Code,
            NEVERC_STATUS_OK);
  std::vector<uint8_t> Bitcode(
      ExportedView.Data, ExportedView.Data + ExportedView.Length);
  ASSERT_EQ(Runtime.Bridge->releaseSerializedBuffer(*BufferOrErr).Code,
            NEVERC_STATUS_OK);

  llvm::FunctionType *Type = llvm::FunctionType::get(
      llvm::Type::getVoidTy(Runtime.Bridge->context()), false);
  llvm::Function *Transient = llvm::Function::Create(
      Type, llvm::GlobalValue::ExternalLinkage, "transient",
      Runtime.Bridge->module());
  auto TransientHandle = Runtime.Bridge->wrapValue(*Transient);
  ASSERT_TRUE(static_cast<bool>(TransientHandle));

  EXPECT_EQ(Runtime.Bridge
                ->importModule(NEVERC_IR_SERIALIZATION_BITCODE,
                               bytesOf(Bitcode))
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Runtime.Bridge->module().getModuleIdentifier(), "serialization");
  EXPECT_NE(Runtime.Bridge->module().getFunction("answer"), nullptr);
  EXPECT_EQ(Runtime.Bridge->module().getFunction("transient"), nullptr);

  llvm::Value *Resolved = nullptr;
  EXPECT_EQ(Runtime.Bridge->resolveValue(*TransientHandle, &Resolved).Code,
            NEVERC_STATUS_STALE_HANDLE);
  EXPECT_EQ(Resolved, nullptr);
}

TEST(PluginIRSerializationTest,
     RejectsMalformedAndTargetIncompatibleBitcodeWithoutReplacingModule) {
  SerializationEnvironment Environment;
  SerializationRuntime Runtime(Environment, "original");
  ASSERT_NE(Runtime.Bridge, nullptr);
  constexpr llvm::StringLiteral Triple("x86_64-unknown-linux-gnu");
  constexpr llvm::StringLiteral Layout("e-p:64:64");
  Runtime.Bridge->module().setTargetTriple(Triple);
  Runtime.Bridge->module().setDataLayout(Layout);

  const std::vector<uint8_t> Malformed = {0x42, 0x43, 0xC0};
  EXPECT_EQ(Runtime.Bridge
                ->importModule(NEVERC_IR_SERIALIZATION_BITCODE,
                               bytesOf(Malformed))
                .Code,
            NEVERC_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(Runtime.Bridge->module().getModuleIdentifier(), "original");

  std::vector<uint8_t> WrongTriple = serializeBitcode(
      "wrong-triple", "aarch64-unknown-linux-gnu", Layout);
  EXPECT_EQ(Runtime.Bridge
                ->importModule(NEVERC_IR_SERIALIZATION_BITCODE,
                               bytesOf(WrongTriple))
                .Code,
            NEVERC_STATUS_WRONG_SCOPE);
  EXPECT_EQ(Runtime.Bridge->module().getModuleIdentifier(), "original");

  std::vector<uint8_t> WrongLayout =
      serializeBitcode("wrong-layout", Triple, "e-p:32:32");
  EXPECT_EQ(Runtime.Bridge
                ->importModule(NEVERC_IR_SERIALIZATION_BITCODE,
                               bytesOf(WrongLayout))
                .Code,
            NEVERC_STATUS_WRONG_SCOPE);
  EXPECT_EQ(Runtime.Bridge->module().getModuleIdentifier(), "original");
}

TEST(PluginIRSerializationTest,
     ExportedBuffersAreTaskScopedAndExplicitlyReleased) {
  SerializationEnvironment Environment;
  SerializationRuntime Owner(Environment, "owner");
  SerializationRuntime Other(Environment, "other");
  ASSERT_NE(Owner.Bridge, nullptr);
  ASSERT_NE(Other.Bridge, nullptr);

  auto BufferOrErr =
      Owner.Bridge->exportModule(NEVERC_IR_SERIALIZATION_BITCODE);
  ASSERT_TRUE(static_cast<bool>(BufferOrErr));
  NevercIRSerializedBufferHandle Buffer = *BufferOrErr;

  NevercByteView View{};
  EXPECT_EQ(Owner.Bridge->getSerializedBufferView(Buffer, &View).Code,
            NEVERC_STATUS_OK);
  EXPECT_NE(View.Data, nullptr);
  EXPECT_GT(View.Length, 0u);
  EXPECT_EQ(Other.Bridge->getSerializedBufferView(Buffer, &View).Code,
            NEVERC_STATUS_WRONG_SESSION);

  EXPECT_EQ(Owner.Bridge->releaseSerializedBuffer(Buffer).Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Owner.Bridge->getSerializedBufferView(Buffer, &View).Code,
            NEVERC_STATUS_STALE_HANDLE);
}

TEST(PluginIRSerializationTest, ReportsUnavailableTextImportCapability) {
  SerializationEnvironment Environment;
  SerializationRuntime Runtime(Environment, "text-import");
  ASSERT_NE(Runtime.Bridge, nullptr);
  static constexpr char Text[] = "define i32 @answer() { ret i32 42 }\n";
  NevercByteView View{};
  View.Data = reinterpret_cast<const uint8_t *>(Text);
  View.Length = sizeof(Text) - 1;
  EXPECT_EQ(Runtime.Bridge->importModule(NEVERC_IR_SERIALIZATION_TEXT, View).Code,
            NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
}

TEST(PluginIRSerializationTest,
     ModuleArtifactReplacementIsVerifiedAndAtomicallyOwned) {
  SerializationEnvironment Environment;
  SerializationRuntime Runtime(Environment, "artifact");
  ASSERT_NE(Runtime.Bridge, nullptr);
  Runtime.Bridge->module().setTargetTriple("x86_64-unknown-linux-gnu");
  Runtime.Bridge->module().setDataLayout("e-p:64:64-i128:128");

  PluginArtifactRegistry Artifacts;
  ASSERT_FALSE(registerIRModuleArtifactType(Artifacts));
  ASSERT_FALSE(Artifacts.freeze());
  PluginArtifactSlot Slot(Artifacts.find(irModuleArtifactID()));

  std::shared_ptr<IRPluginBridge> FirstBridge(std::move(Runtime.Bridge));
  std::weak_ptr<IRPluginBridge> FirstLifetime = FirstBridge;
  auto First = makeModuleArtifact(FirstBridge);
  auto FirstTransaction = PluginArtifactTransaction::create(
      Artifacts, irModuleArtifactID(), First.get());
  ASSERT_TRUE(static_cast<bool>(FirstTransaction));
  (void)First.release();
  ASSERT_FALSE((*FirstTransaction)->commit(Slot));
  EXPECT_EQ(Slot.generation(), 1u);

  auto Invalid = makeModuleArtifact(FirstBridge);
  Invalid->TargetTriple = "aarch64-unknown-linux-gnu";
  auto InvalidTransaction = PluginArtifactTransaction::create(
      Artifacts, irModuleArtifactID(), Invalid.get());
  ASSERT_TRUE(static_cast<bool>(InvalidTransaction));
  (void)Invalid.release();
  llvm::Error InvalidCommit = (*InvalidTransaction)->commit(Slot);
  EXPECT_TRUE(static_cast<bool>(InvalidCommit));
  llvm::consumeError(std::move(InvalidCommit));
  (*InvalidTransaction)->abort();
  EXPECT_EQ(Slot.generation(), 1u);

  auto SecondBridgeOrErr =
      IRPluginBridge::create(*Runtime.Task, "artifact-replacement");
  ASSERT_TRUE(static_cast<bool>(SecondBridgeOrErr));
  std::shared_ptr<IRPluginBridge> SecondBridge(
      std::move(*SecondBridgeOrErr));
  SecondBridge->module().setTargetTriple("x86_64-unknown-linux-gnu");
  SecondBridge->module().setDataLayout("e-p:64:64-i128:128");
  auto Second = makeModuleArtifact(SecondBridge);
  auto SecondTransaction = PluginArtifactTransaction::create(
      Artifacts, irModuleArtifactID(), Second.get());
  ASSERT_TRUE(static_cast<bool>(SecondTransaction));
  (void)Second.release();

  FirstBridge.reset();
  EXPECT_FALSE(FirstLifetime.expired());
  ASSERT_FALSE((*SecondTransaction)->commit(Slot));
  EXPECT_EQ(Slot.generation(), 2u);
  EXPECT_TRUE(FirstLifetime.expired());
}
