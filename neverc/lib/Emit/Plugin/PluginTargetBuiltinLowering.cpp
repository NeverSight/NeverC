#include "../Core/FunctionEmitter.h"
#include "../Core/ModuleEmitter.h"
#include "neverc/Plugin/Host/IRPluginBridge.h"
#include "neverc/Plugin/Host/PluginTargetInfo.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"

using namespace llvm;

namespace neverc::Emit {

Value *FunctionEmitter::genPluginTargetBuiltinExpr(
    unsigned BuiltinID, const CallExpr *E) {
  const auto *Target = getTarget().getPluginTargetInfo();
  if (!Target || !Target->task())
    return nullptr;
  const plugin::VerifiedTargetBuiltin *Builtin =
      Target->getPluginBuiltin(BuiltinID);
  if (!Builtin || !Builtin->Lower)
    return nullptr;

  unsigned ICEArguments = 0;
  TreeContext::GetBuiltinTypeError TypeError;
  getContext().GetBuiltinType(BuiltinID, TypeError, &ICEArguments);
  if (TypeError != TreeContext::GE_None)
    return nullptr;

  SmallVector<Value *, 8> Arguments;
  Arguments.reserve(E->getNumArgs());
  for (unsigned I = 0; I != E->getNumArgs(); ++I)
    Arguments.push_back(genScalarOrConstFoldImmArg(ICEArguments, I, E));

  auto BridgeOrError =
      plugin::IRPluginBridge::borrow(*Target->task(), ME.getModule());
  if (!BridgeOrError) {
    consumeError(BridgeOrError.takeError());
    return nullptr;
  }
  std::unique_ptr<plugin::IRPluginBridge> Bridge =
      std::move(*BridgeOrError);
  const NevercIRBuilderAPI &API = Bridge->builderAPI();
  const NevercTaskHandle Task = Target->task()->handle();

  auto FunctionHandle = Bridge->wrapValue(*CurFn);
  if (!FunctionHandle) {
    consumeError(FunctionHandle.takeError());
    return nullptr;
  }
  NevercIRMutationHandle Mutation{};
  if (API.BeginMutation(API.Context, Task,
                        NEVERC_IR_MUTATION_SCOPE_FUNCTION,
                        *FunctionHandle, &Mutation)
          .Code != NEVERC_STATUS_OK)
    return nullptr;

  NevercIRBuilderHandle BuilderHandle{};
  bool BuilderCreated = false;
  bool MutationCommitted = false;
  auto Cleanup = make_scope_exit([&] {
    if (BuilderCreated)
      (void)API.DestroyBuilder(API.Context, Task, BuilderHandle);
    if (!MutationCommitted)
      (void)API.AbortMutation(API.Context, Task, Mutation);
    (void)API.DestroyMutation(API.Context, Task, Mutation);
  });

  if (API.CreateBuilder(API.Context, Task, Mutation, &BuilderHandle)
          .Code != NEVERC_STATUS_OK)
    return nullptr;
  BuilderCreated = true;

  BasicBlock *InsertBlock = Builder.GetInsertBlock();
  if (!InsertBlock)
    return nullptr;
  auto InsertBlockHandle = Bridge->wrapValue(*InsertBlock);
  if (!InsertBlockHandle) {
    consumeError(InsertBlockHandle.takeError());
    return nullptr;
  }
  if (Builder.GetInsertPoint() == InsertBlock->end()) {
    if (API.SetInsertBlock(API.Context, Task, BuilderHandle,
                           *InsertBlockHandle)
            .Code != NEVERC_STATUS_OK)
      return nullptr;
  } else {
    auto InsertBeforeHandle =
        Bridge->wrapValue(*Builder.GetInsertPoint());
    if (!InsertBeforeHandle) {
      consumeError(InsertBeforeHandle.takeError());
      return nullptr;
    }
    if (API.SetInsertBefore(API.Context, Task, BuilderHandle,
                            *InsertBeforeHandle)
            .Code != NEVERC_STATUS_OK)
      return nullptr;
  }

  SmallVector<NevercIRValueHandle, 8> ArgumentHandles;
  ArgumentHandles.reserve(Arguments.size());
  for (Value *Argument : Arguments) {
    auto Handle = Bridge->wrapValue(*Argument);
    if (!Handle) {
      consumeError(Handle.takeError());
      return nullptr;
    }
    ArgumentHandles.push_back(*Handle);
  }

  llvm::Type *ExpectedType = convertType(E->getType());
  auto ResultType = Bridge->wrapType(*ExpectedType);
  if (!ResultType) {
    consumeError(ResultType.takeError());
    return nullptr;
  }

  NevercTargetBuiltinLoweringInvocation Invocation{};
  Invocation.Header = {sizeof(Invocation), NEVERC_TARGET_API_MAJOR,
                       NEVERC_TARGET_API_MINOR, 0};
  Invocation.Task = Task;
  Invocation.BuiltinName = {Builtin->Name.data(), Builtin->Name.size()};
  Invocation.BuiltinIndex =
      static_cast<uint32_t>(BuiltinID - Builtin::FirstTSBuiltin);
  Invocation.Core = &Bridge->coreAPI();
  Invocation.Builder = &API;
  Invocation.Mutation = Mutation;
  Invocation.IRBuilder = BuilderHandle;
  Invocation.ResultType = *ResultType;
  Invocation.Arguments = ArgumentHandles.data();
  Invocation.ArgumentCount = ArgumentHandles.size();

  NevercIRValueHandle ResultHandle{};
  auto Status = Target->task()->invokeCallback(
      Target->record().PluginID, "LowerTargetBuiltin", [&] {
        return Builtin->Lower(Target->record().TargetUserData,
                              &Invocation, &ResultHandle);
      });
  if (!Status) {
    consumeError(Status.takeError());
    return nullptr;
  }
  if (Status->Code != NEVERC_STATUS_OK)
    return nullptr;

  Value *Result = nullptr;
  if (Bridge->resolveValue(ResultHandle, &Result).Code !=
          NEVERC_STATUS_OK ||
      !Result || Result->getType() != ExpectedType)
    return nullptr;

  if (Bridge->commitInProgressFunctionMutation(Mutation).Code !=
      NEVERC_STATUS_OK)
    return nullptr;
  MutationCommitted = true;
  return Result;
}

} // namespace neverc::Emit
