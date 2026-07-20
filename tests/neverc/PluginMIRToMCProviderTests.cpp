#include "../../neverc/lib/Plugin/MIR/MIRModuleArtifact.h"
#include "neverc/Plugin/Host/MCPluginBridge.h"
#include "neverc/Plugin/Host/MIRToMCProvider.h"
#include "neverc/Plugin/Host/MachineEmissionBridge.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Error.h"
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

constexpr NevercTargetID TestTargetID{UINT64_C(0x7500), UINT64_C(1)};
constexpr NevercInterfaceID TestSchemaID{UINT64_C(0x7501), UINT64_C(1)};
constexpr char TestSchemaDigest[] =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
constexpr char ForeignSchemaDigest[] =
    "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789";

NevercStringView view(const char *Text) {
  return {Text, static_cast<uint64_t>(std::char_traits<char>::length(Text))};
}

std::string errorText(Error ErrorValue) {
  return toString(std::move(ErrorValue)).str().str();
}

class CodeGenTaskScope {
public:
  CodeGenTaskScope()
      : Services("neverc-plugin-mir-to-mc-provider-tests",
                 LLVM_VERSION_MAJOR) {}

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

  ~CodeGenTaskScope() {
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

std::unique_ptr<LLVMTargetMachine> createTargetMachine(Module &M) {
  static const bool Initialized = [] {
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    return true;
  }();
  (void)Initialized;

  const std::string TripleName = sys::getDefaultTargetTriple();
  std::string LookupError;
  const Target *Target =
      TargetRegistry::lookupTarget(TripleName, LookupError);
  if (!Target)
    return nullptr;
  TargetOptions Options;
  std::unique_ptr<TargetMachine> Machine(Target->createTargetMachine(
      TripleName, "generic", "", Options, std::nullopt,
      CodeGenOptLevel::None));
  if (!Machine)
    return nullptr;
  M.setTargetTriple(TripleName);
  M.setDataLayout(Machine->createDataLayout());
  return std::unique_ptr<LLVMTargetMachine>(
      static_cast<LLVMTargetMachine *>(Machine.release()));
}

Function *addVoidFunction(Module &M, StringRef Name) {
  Function *F = Function::Create(
      FunctionType::get(Type::getVoidTy(M.getContext()), false),
      GlobalValue::ExternalLinkage, Name, M);
  BasicBlock *Entry = BasicBlock::Create(M.getContext(), "entry", F);
  IRBuilder<> Builder(Entry);
  Builder.CreateRetVoid();
  return F;
}

Expected<std::shared_ptr<const PluginTargetSnapshot>> createSnapshot() {
  NevercTargetDescriptor Target{};
  Target.Header = {sizeof(Target), NEVERC_TARGET_API_MAJOR,
                   NEVERC_TARGET_API_MINOR, 0};
  Target.TargetID = TestTargetID;
  Target.CanonicalName = view("test.mir-to-mc");
  Target.MCSchemaID = TestSchemaID;
  Target.Machine.Header = {sizeof(Target.Machine), NEVERC_TARGET_API_MAJOR,
                           NEVERC_TARGET_API_MINOR, 0};
  Target.Machine.RawTriple = view("test-unknown-none-none");
  Target.Machine.Architecture = view("test");
  Target.Machine.DataLayout = view("e-p:64:64-i64:64-n32:64-S128");
  Target.Machine.DefaultCPU = view("generic");
  Target.Machine.SchemaDigest = view(TestSchemaDigest);
  Target.Machine.SupportedRelocationModels =
      NEVERC_TARGET_RELOCATION_MASK_STATIC;
  Target.Machine.SupportedCodeModels = NEVERC_TARGET_CODE_MODEL_MASK_SMALL;
  Target.Machine.DefaultRelocationModel = NEVERC_TARGET_RELOCATION_STATIC;
  Target.Machine.DefaultCodeModel = NEVERC_TARGET_CODE_MODEL_SMALL;
  Target.Machine.ExceptionModel = NEVERC_TARGET_EXCEPTION_NONE;
  Target.Machine.UnwindModel = NEVERC_TARGET_UNWIND_NONE;
  Target.Machine.Endianness = NEVERC_TARGET_ENDIAN_LITTLE;
  Target.Machine.PointerWidth = 64;
  Target.Machine.IntWidth = 32;
  Target.Machine.LongWidth = 64;
  Target.Machine.LongLongWidth = 64;
  Target.Machine.StackAlignment = 128;
  Target.Machine.MaximumAtomicWidth = 64;
  Target.Machine.MaximumVectorAlignment = 128;
  Target.Machine.BuiltinVaListKind = NEVERC_TARGET_VA_LIST_VOID_POINTER;
  Target.Machine.ExecutionLevels = NEVERC_TARGET_EXECUTION_USER;
  Target.Machine.DefaultExecutionLevel = NEVERC_TARGET_EXECUTION_USER;
  Target.Machine.TLSSupported = NEVERC_TRUE;

  NevercMCSchemaValueDescriptor Opcode{};
  Opcode.Header = {sizeof(Opcode), NEVERC_MC_API_MAJOR,
                   NEVERC_MC_API_MINOR, 0};
  Opcode.StableID = 10;
  Opcode.BackendValue = 100;
  Opcode.CanonicalName = view("test.opcode");

  NevercMCSchemaDescriptor Schema{};
  Schema.Header = {sizeof(Schema), NEVERC_MC_API_MAJOR,
                   NEVERC_MC_API_MINOR, 0};
  Schema.SchemaID = TestSchemaID;
  Schema.TargetID = TestTargetID;
  Schema.CanonicalName = view("test.mc");
  Schema.Digest = view(TestSchemaDigest);
  Schema.Opcodes = {&Opcode, 1, sizeof(Opcode)};

  PluginTargetRegistrationView Registration;
  Registration.PluginID = "org.neverc.test.mir-to-mc";
  Registration.Targets = ArrayRef<NevercTargetDescriptor>(Target);
  Registration.MCSchemas = ArrayRef<NevercMCSchemaDescriptor>(Schema);
  return PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(Registration),
      PluginTargetRequest{});
}

Error appendTestInstruction(MachineEmissionBridge &Bridge,
                            PluginTaskContext &Task) {
  auto Unit = Bridge.unitHandle();
  if (!Unit)
    return Unit.takeError();
  const NevercMCAPI &API = Bridge.api();
  NevercMCSchemaTokenHandle SchemaToken{};
  NevercStatus Status =
      API.GetSchemaToken(API.Context, Task.handle(), *Unit, &SchemaToken);
  if (Status.Code != NEVERC_STATUS_OK)
    return createStringError(inconvertibleErrorCode(),
                             "failed to acquire MC schema token");
  NevercMCMutationHandle Mutation{};
  Status = API.BeginMutation(API.Context, Task.handle(), *Unit, &Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return createStringError(inconvertibleErrorCode(),
                             "failed to begin MC mutation");
  NevercMCInstHandle Instruction{};
  Status = API.CreateInstruction(API.Context, Task.handle(), Mutation,
                                 SchemaToken, 10, &Instruction);
  if (Status.Code != NEVERC_STATUS_OK)
    return createStringError(inconvertibleErrorCode(),
                             "failed to create MC instruction");
  Status = API.AppendInstruction(API.Context, Task.handle(), Mutation, *Unit,
                                 Instruction);
  if (Status.Code != NEVERC_STATUS_OK)
    return createStringError(inconvertibleErrorCode(),
                             "failed to append MC instruction");
  Status = API.CommitMutation(API.Context, Task.handle(), Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return createStringError(inconvertibleErrorCode(),
                             "failed to commit MC mutation");
  return Error::success();
}

TEST(PluginMIRToMCProviderTest,
     ReplacementEmitsTaskLocalMCAndSkipsBuiltin) {
  CodeGenTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Snapshot = createSnapshot();
  if (!Snapshot) {
    ADD_FAILURE() << errorText(Snapshot.takeError());
    return;
  }

  LLVMContext Context;
  Module M("mir-to-mc-replacement", Context);
  Function *F = addVoidFunction(M, "replacement");
  std::unique_ptr<LLVMTargetMachine> Machine = createTargetMachine(M);
  ASSERT_NE(Machine, nullptr);
  auto MIR = MIRModuleArtifact::createOwned(
      M, *Machine, TestTargetID, "test-target-key", TestSchemaDigest);
  if (!MIR) {
    ADD_FAILURE() << errorText(MIR.takeError());
    return;
  }
  (*MIR)->getOrCreateMachineFunction(*F);

  MIRToMCExecutionRequest Request;
  Request.Task = &Scope.task();
  Request.MIR = MIR->get();
  Request.Snapshot = Snapshot->get();
  Request.HasFinalMIRProof = true;
  Request.RunMachineVerifier = false;
  unsigned ReplacementCalls = 0;
  unsigned BuiltinCalls = 0;
  auto Result = MIRToMCProviderRuntime::execute(
      Request,
      [&](MachineEmissionBridge &Bridge) -> Error {
        ++ReplacementCalls;
        return appendTestInstruction(Bridge, Scope.task());
      },
      [&]() -> Expected<std::unique_ptr<PluginMCUnit>> {
        ++BuiltinCalls;
        return createStringError(inconvertibleErrorCode(),
                                 "builtin must not execute");
      });

  if (!Result) {
    ADD_FAILURE() << errorText(Result.takeError());
    return;
  }
  EXPECT_EQ(ReplacementCalls, 1U);
  EXPECT_EQ(BuiltinCalls, 0U);
  ASSERT_EQ((*Result)->size(), 1U);
  ASSERT_NE((*Result)->at(0), nullptr);
  EXPECT_EQ((*Result)->at(0)->getOpcode(), 100U);
}

TEST(PluginMIRToMCProviderTest,
     ForeignSchemaIsRejectedBeforeReplacementOrBuiltinRuns) {
  CodeGenTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Snapshot = createSnapshot();
  if (!Snapshot) {
    ADD_FAILURE() << errorText(Snapshot.takeError());
    return;
  }

  LLVMContext Context;
  Module M("mir-to-mc-foreign-schema", Context);
  Function *F = addVoidFunction(M, "foreign_schema");
  std::unique_ptr<LLVMTargetMachine> Machine = createTargetMachine(M);
  ASSERT_NE(Machine, nullptr);
  auto MIR = MIRModuleArtifact::createOwned(
      M, *Machine, TestTargetID, "test-target-key", ForeignSchemaDigest);
  if (!MIR) {
    ADD_FAILURE() << errorText(MIR.takeError());
    return;
  }
  (*MIR)->getOrCreateMachineFunction(*F);

  MIRToMCExecutionRequest Request;
  Request.Task = &Scope.task();
  Request.MIR = MIR->get();
  Request.Snapshot = Snapshot->get();
  Request.HasFinalMIRProof = true;
  Request.RunMachineVerifier = false;
  unsigned ReplacementCalls = 0;
  unsigned BuiltinCalls = 0;
  auto Result = MIRToMCProviderRuntime::execute(
      Request,
      [&](MachineEmissionBridge &Bridge) -> Error {
        ++ReplacementCalls;
        return appendTestInstruction(Bridge, Scope.task());
      },
      [&]() -> Expected<std::unique_ptr<PluginMCUnit>> {
        ++BuiltinCalls;
        return createStringError(inconvertibleErrorCode(),
                                 "builtin must not execute");
      });

  ASSERT_FALSE(static_cast<bool>(Result));
  const std::string Message = errorText(Result.takeError());
  EXPECT_NE(Message.find("schema"), std::string::npos) << Message;
  EXPECT_EQ(ReplacementCalls, 0U);
  EXPECT_EQ(BuiltinCalls, 0U);
}

TEST(PluginMIRToMCProviderTest,
     EmptyReplacementProductFailsWithoutBuiltinFallback) {
  CodeGenTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Snapshot = createSnapshot();
  if (!Snapshot) {
    ADD_FAILURE() << errorText(Snapshot.takeError());
    return;
  }

  LLVMContext Context;
  Module M("mir-to-mc-empty", Context);
  Function *F = addVoidFunction(M, "empty_product");
  std::unique_ptr<LLVMTargetMachine> Machine = createTargetMachine(M);
  ASSERT_NE(Machine, nullptr);
  auto MIR = MIRModuleArtifact::createOwned(
      M, *Machine, TestTargetID, "test-target-key", TestSchemaDigest);
  if (!MIR) {
    ADD_FAILURE() << errorText(MIR.takeError());
    return;
  }
  (*MIR)->getOrCreateMachineFunction(*F);

  MIRToMCExecutionRequest Request;
  Request.Task = &Scope.task();
  Request.MIR = MIR->get();
  Request.Snapshot = Snapshot->get();
  Request.HasFinalMIRProof = true;
  Request.RunMachineVerifier = false;
  unsigned ReplacementCalls = 0;
  unsigned BuiltinCalls = 0;
  auto Result = MIRToMCProviderRuntime::execute(
      Request,
      [&](MachineEmissionBridge &) -> Error {
        ++ReplacementCalls;
        return Error::success();
      },
      [&]() -> Expected<std::unique_ptr<PluginMCUnit>> {
        ++BuiltinCalls;
        return createStringError(inconvertibleErrorCode(),
                                 "builtin must not execute");
      });

  ASSERT_FALSE(static_cast<bool>(Result));
  const std::string Message = errorText(Result.takeError());
  EXPECT_NE(Message.find("emitted no instructions"), std::string::npos)
      << Message;
  EXPECT_EQ(ReplacementCalls, 1U);
  EXPECT_EQ(BuiltinCalls, 0U);
}

} // namespace
