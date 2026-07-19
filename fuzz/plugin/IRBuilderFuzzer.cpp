#include "PluginFrontendFuzzSupport.h"
#include "neverc/Plugin/Host/IRPluginBridge.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/Error.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>

using namespace llvm;
using namespace neverc;
using namespace neverc::fuzz;
using namespace neverc::plugin;

namespace {

NevercStringView takeName(ByteCursor &Input) {
  ArrayRef<uint8_t> Bytes = Input.takeBytes(32);
  return {reinterpret_cast<const char *>(Bytes.data()), Bytes.size()};
}

void consume(Error E) {
  if (E)
    consumeError(std::move(E));
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  ByteCursor Input(Data, Size);
  auto CreatedTask =
      pluginFuzzRuntime().session().createTask(NEVERC_TASK_TRANSLATION_UNIT);
  if (!CreatedTask) {
    consumeError(CreatedTask.takeError());
    return 0;
  }
  std::unique_ptr<PluginTaskContext> Task = std::move(*CreatedTask);
  auto CreatedBridge = IRPluginBridge::create(*Task, "ir-builder-fuzz");
  if (!CreatedBridge) {
    consumeError(CreatedBridge.takeError());
    consume(Task->end());
    return 0;
  }
  std::unique_ptr<IRPluginBridge> Bridge = std::move(*CreatedBridge);
  const NevercIRBuilderAPI &BuilderAPI = Bridge->builderAPI();
  const NevercIRCoreAPI &Core = Bridge->coreAPI();
  NevercTaskHandle TaskHandle = Task->handle();

  Type *I32 = Type::getInt32Ty(Bridge->context());
  Function *FunctionValue = Function::Create(
      FunctionType::get(I32, false), GlobalValue::ExternalLinkage, "fuzz",
      Bridge->module());
  BasicBlock *Entry =
      BasicBlock::Create(Bridge->context(), "entry", FunctionValue);
  ReturnInst *Return =
      ReturnInst::Create(Bridge->context(), ConstantInt::get(I32, 0), Entry);
  auto FunctionHandle = Bridge->wrapValue(*FunctionValue);
  auto EntryHandle = Bridge->wrapValue(*Entry);
  auto ReturnHandle = Bridge->wrapValue(*Return);
  auto I32Handle = Bridge->wrapType(*I32);
  auto Left = Bridge->wrapValue(*ConstantInt::get(I32, Input.takeU32()));
  auto Right = Bridge->wrapValue(*ConstantInt::get(I32, Input.takeU32()));
  if (!FunctionHandle || !EntryHandle || !ReturnHandle || !I32Handle || !Left ||
      !Right) {
    Bridge.reset();
    consume(Task->end());
    return 0;
  }

  NevercIRMutationHandle Mutation{};
  if (BuilderAPI
          .BeginMutation(BuilderAPI.Context, TaskHandle,
                         NEVERC_IR_MUTATION_SCOPE_FUNCTION, *FunctionHandle,
                         &Mutation)
          .Code != NEVERC_STATUS_OK) {
    Bridge.reset();
    consume(Task->end());
    return 0;
  }
  NevercIRBuilderHandle Builder{};
  if (BuilderAPI
          .CreateBuilder(BuilderAPI.Context, TaskHandle, Mutation, &Builder)
          .Code != NEVERC_STATUS_OK) {
    (void)BuilderAPI.AbortMutation(BuilderAPI.Context, TaskHandle, Mutation);
    (void)BuilderAPI.DestroyMutation(BuilderAPI.Context, TaskHandle, Mutation);
    Bridge.reset();
    consume(Task->end());
    return 0;
  }
  (void)BuilderAPI.SetInsertBefore(BuilderAPI.Context, TaskHandle, Builder,
                                   *ReturnHandle);

  NevercIRValueHandle LastCreated{};
  unsigned OperationCount = std::min<unsigned>(Input.takeByte(), 32U);
  for (unsigned Index = 0; Index != OperationCount; ++Index) {
    NevercTaskHandle OperationTask = chooseTaskHandle(Input, TaskHandle);
    NevercIRValueHandle A =
        (Input.takeByte() & 1U) ? *Left : arbitraryHandle(Input);
    NevercIRValueHandle B =
        (Input.takeByte() & 1U) ? *Right : arbitraryHandle(Input);
    NevercIRValueHandle Result{};
    switch (Input.takeByte() % 5U) {
    case 0:
      (void)BuilderAPI.BuildBinary(
          BuilderAPI.Context, OperationTask, Builder, Input.takeU32(), A, B,
          takeName(Input), &Result);
      break;
    case 1:
      (void)BuilderAPI.BuildUnary(BuilderAPI.Context, OperationTask, Builder,
                                  Input.takeU32(), A, takeName(Input), &Result);
      break;
    case 2:
      (void)BuilderAPI.BuildCompare(
          BuilderAPI.Context, OperationTask, Builder, Input.takeU32(), A, B,
          takeName(Input), &Result);
      break;
    case 3:
      (void)BuilderAPI.BuildCast(
          BuilderAPI.Context, OperationTask, Builder, Input.takeU32(), A,
          (Input.takeByte() & 1U) ? *I32Handle : arbitraryHandle(Input),
          takeName(Input), &Result);
      break;
    default:
      (void)BuilderAPI.SetFastMathFlags(BuilderAPI.Context, OperationTask,
                                        Builder, Input.takeU64());
      break;
    }
    if (!neverc_handle_is_null(Result))
      LastCreated = Result;
  }

  const bool Commit = (Input.takeByte() & 1U) != 0;
  NevercStatus Final =
      Commit ? BuilderAPI.CommitMutation(BuilderAPI.Context, TaskHandle,
                                          Mutation)
             : BuilderAPI.AbortMutation(BuilderAPI.Context, TaskHandle,
                                        Mutation);
  if (Final.Code == NEVERC_STATUS_OK && Commit &&
      verifyFunction(*FunctionValue))
    std::abort();
  if (!Commit && !neverc_handle_is_null(LastCreated)) {
    NevercIRTypeHandle Type{};
    NevercStatus Stale =
        Core.GetValueType(Core.Context, TaskHandle, LastCreated, &Type);
    if (Stale.Code != NEVERC_STATUS_STALE_HANDLE)
      std::abort();
  }

  (void)BuilderAPI.DestroyBuilder(BuilderAPI.Context, TaskHandle, Builder);
  (void)BuilderAPI.DestroyMutation(BuilderAPI.Context, TaskHandle, Mutation);
  Bridge.reset();
  consume(Task->end());
  return 0;
}
