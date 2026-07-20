#include "../../neverc/lib/Plugin/MIR/MIRModuleArtifact.h"
#include "neverc/Plugin/Host/IRToMIRProvider.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
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
#include <string>

using namespace llvm;
using namespace neverc::plugin;

namespace {

constexpr NevercTargetID TestTargetID{UINT64_C(0x7400), UINT64_C(1)};
constexpr char TestSchemaDigest[] =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

std::string errorText(Error ErrorValue) {
  return toString(std::move(ErrorValue)).str().str();
}

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

TEST(PluginIRToMIRProviderTest,
     ReplacementUsesPipelineOwnedMMIAndSkipsBuiltin) {
  LLVMContext Context;
  Module M("ir-to-mir-replacement", Context);
  Function *F = addVoidFunction(M, "replacement");
  std::unique_ptr<LLVMTargetMachine> Machine = createTargetMachine(M);
  ASSERT_NE(Machine, nullptr);

  MachineModuleInfoWrapperPass MMI(Machine.get());
  MMI.doInitialization(M);
  MIRModuleCoveragePolicy Coverage;
  IRToMIRExecutionRequest Request;
  Request.Module = &M;
  Request.TargetMachine = Machine.get();
  Request.PipelineMMI = &MMI;
  Request.TargetID = TestTargetID;
  Request.CompatibilityKey = "test-target-key";
  Request.SchemaDigest = TestSchemaDigest;
  Request.Coverage = &Coverage;
  Request.HasFinalIRProof = true;
  Request.RunMachineVerifier = false;

  unsigned ReplacementCalls = 0;
  unsigned BuiltinCalls = 0;
  auto Result = IRToMIRProviderRuntime::execute(
      Request,
      [&](MIRModuleArtifact &Artifact) -> Error {
        ++ReplacementCalls;
        Artifact.getOrCreateMachineFunction(*F);
        return Error::success();
      },
      [&]() -> Expected<std::unique_ptr<MIRModuleArtifact>> {
        ++BuiltinCalls;
        return createStringError(inconvertibleErrorCode(),
                                 "builtin must not execute");
      });

  ASSERT_TRUE(static_cast<bool>(Result))
      << errorText(Result.takeError());
  EXPECT_EQ(ReplacementCalls, 1U);
  EXPECT_EQ(BuiltinCalls, 0U);
  EXPECT_EQ(&(*Result)->machineModuleInfo(), &MMI.getMMI());
  Result->reset();
  MMI.doFinalization(M);
}

TEST(PluginIRToMIRProviderTest,
     ReplacementMissingMachineFunctionFailsWithoutBuiltinFallback) {
  LLVMContext Context;
  Module M("ir-to-mir-incomplete", Context);
  addVoidFunction(M, "missing_machine_function");
  std::unique_ptr<LLVMTargetMachine> Machine = createTargetMachine(M);
  ASSERT_NE(Machine, nullptr);

  MachineModuleInfoWrapperPass MMI(Machine.get());
  MMI.doInitialization(M);
  MIRModuleCoveragePolicy Coverage;
  IRToMIRExecutionRequest Request;
  Request.Module = &M;
  Request.TargetMachine = Machine.get();
  Request.PipelineMMI = &MMI;
  Request.TargetID = TestTargetID;
  Request.CompatibilityKey = "test-target-key";
  Request.SchemaDigest = TestSchemaDigest;
  Request.Coverage = &Coverage;
  Request.HasFinalIRProof = true;
  Request.RunMachineVerifier = false;

  unsigned ReplacementCalls = 0;
  unsigned BuiltinCalls = 0;
  auto Result = IRToMIRProviderRuntime::execute(
      Request,
      [&](MIRModuleArtifact &) -> Error {
        ++ReplacementCalls;
        return Error::success();
      },
      [&]() -> Expected<std::unique_ptr<MIRModuleArtifact>> {
        ++BuiltinCalls;
        return createStringError(inconvertibleErrorCode(),
                                 "builtin must not execute");
      });

  ASSERT_FALSE(static_cast<bool>(Result));
  const std::string Message = errorText(Result.takeError());
  EXPECT_NE(Message.find("MIR provider omitted function"), std::string::npos)
      << Message;
  EXPECT_EQ(ReplacementCalls, 1U);
  EXPECT_EQ(BuiltinCalls, 0U);
  MMI.doFinalization(M);
}

} // namespace
