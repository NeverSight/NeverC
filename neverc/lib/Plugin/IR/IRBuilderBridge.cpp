#include "IRBuilderPluginBridge.h"
#include "neverc/Plugin/Host/IRPluginBridge.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/NoFolder.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <limits>
#include <new>
#include <string>

using namespace llvm;

namespace neverc::plugin {

struct IRBuilderPluginBridge::Mutation {
  enum class State { Active, Committed, Aborted };

  NevercIRMutationScope Scope = NEVERC_IR_MUTATION_SCOPE_MODULE;
  Function *FunctionScope = nullptr;
  BasicBlock *LoopHeader = nullptr;
  std::vector<BasicBlock *> LoopBlocks;
  State CurrentState = State::Active;
  unsigned BuilderCount = 0;
  std::vector<WeakTrackingVH> CreatedValues;
  std::vector<WeakTrackingVH> PhiIncomingEdits;
};

struct IRBuilderPluginBridge::Builder {
  Builder(Mutation &MutationValue, LLVMContext &Context)
      : Lease(&MutationValue),
        Value(std::make_unique<IRBuilder<NoFolder>>(Context)) {}

  Mutation *Lease;
  std::unique_ptr<IRBuilder<NoFolder>> Value;
};

namespace {

template <typename T> T *asBridge(void *Context) {
  return static_cast<T *>(Context);
}

Instruction::BinaryOps binaryOpcode(NevercIROpcode Opcode) {
  switch (Opcode) {
  case NEVERC_IR_OPCODE_ADD:
    return Instruction::Add;
  case NEVERC_IR_OPCODE_F_ADD:
    return Instruction::FAdd;
  case NEVERC_IR_OPCODE_SUB:
    return Instruction::Sub;
  case NEVERC_IR_OPCODE_F_SUB:
    return Instruction::FSub;
  case NEVERC_IR_OPCODE_MUL:
    return Instruction::Mul;
  case NEVERC_IR_OPCODE_F_MUL:
    return Instruction::FMul;
  case NEVERC_IR_OPCODE_U_DIV:
    return Instruction::UDiv;
  case NEVERC_IR_OPCODE_S_DIV:
    return Instruction::SDiv;
  case NEVERC_IR_OPCODE_F_DIV:
    return Instruction::FDiv;
  case NEVERC_IR_OPCODE_U_REM:
    return Instruction::URem;
  case NEVERC_IR_OPCODE_S_REM:
    return Instruction::SRem;
  case NEVERC_IR_OPCODE_F_REM:
    return Instruction::FRem;
  case NEVERC_IR_OPCODE_SHL:
    return Instruction::Shl;
  case NEVERC_IR_OPCODE_L_SHR:
    return Instruction::LShr;
  case NEVERC_IR_OPCODE_A_SHR:
    return Instruction::AShr;
  case NEVERC_IR_OPCODE_AND:
    return Instruction::And;
  case NEVERC_IR_OPCODE_OR:
    return Instruction::Or;
  case NEVERC_IR_OPCODE_XOR:
    return Instruction::Xor;
  default:
    return Instruction::BinaryOpsEnd;
  }
}

bool isFloatingBinary(Instruction::BinaryOps Opcode) {
  return Opcode == Instruction::FAdd || Opcode == Instruction::FSub ||
         Opcode == Instruction::FMul || Opcode == Instruction::FDiv ||
         Opcode == Instruction::FRem;
}

} // namespace

Expected<std::unique_ptr<IRBuilderPluginBridge>>
IRBuilderPluginBridge::create(IRPluginBridge &Bridge) {
  auto Result =
      std::unique_ptr<IRBuilderPluginBridge>(new IRBuilderPluginBridge(Bridge));
  if (Error E = Result->initialize())
    return std::move(E);
  return std::move(Result);
}

IRBuilderPluginBridge::IRBuilderPluginBridge(IRPluginBridge &Bridge)
    : Bridge(Bridge) {}

IRBuilderPluginBridge::~IRBuilderPluginBridge() {
  for (auto It = BuilderHandles.rbegin(); It != BuilderHandles.rend(); ++It)
    (void)Bridge.Task.handles().release(*It, PluginIRBuilderHandleKind);
  for (auto It = MutationHandles.rbegin(); It != MutationHandles.rend(); ++It) {
    Mutation *Value = nullptr;
    if (resolveMutation(*It, &Value).Code == NEVERC_STATUS_OK &&
        Value->CurrentState == Mutation::State::Active)
      rollback(*Value);
    (void)Bridge.Task.handles().release(*It, PluginIRMutationHandleKind);
  }
}

bool IRBuilderPluginBridge::hasActiveMutation() const {
  for (NevercIRMutationHandle Handle : MutationHandles) {
    Mutation *Value = nullptr;
    if (resolveMutation(Handle, &Value).Code == NEVERC_STATUS_OK &&
        Value->CurrentState == Mutation::State::Active)
      return true;
  }
  return false;
}

Error IRBuilderPluginBridge::initialize() {
  API.Header = {sizeof(API), NEVERC_IR_BUILDER_API_MAJOR,
                NEVERC_IR_BUILDER_API_MINOR, 0};
  API.Context = this;
  API.BeginMutation = &IRBuilderPluginBridge::beginMutation;
  API.CommitMutation = &IRBuilderPluginBridge::commitMutation;
  API.AbortMutation = &IRBuilderPluginBridge::abortMutation;
  API.DestroyMutation = &IRBuilderPluginBridge::destroyMutation;
  API.CreateBuilder = &IRBuilderPluginBridge::createBuilder;
  API.DestroyBuilder = &IRBuilderPluginBridge::destroyBuilder;
  API.SetInsertBlock = &IRBuilderPluginBridge::setInsertBlock;
  API.SetInsertBefore = &IRBuilderPluginBridge::setInsertBefore;
  API.SetDebugLocation = &IRBuilderPluginBridge::setDebugLocation;
  API.SetFastMathFlags = &IRBuilderPluginBridge::setFastMathFlags;
  API.BuildBinary = &IRBuilderPluginBridge::buildBinary;
  API.BuildUnary = &IRBuilderPluginBridge::buildUnary;
  API.BuildCompare = &IRBuilderPluginBridge::buildCompare;
  API.BuildCast = &IRBuilderPluginBridge::buildCast;
  API.BuildSelect = &IRBuilderPluginBridge::buildSelect;
  API.BuildAlloca = &IRBuilderPluginBridge::buildAlloca;
  API.BuildLoad = &IRBuilderPluginBridge::buildLoad;
  API.BuildStore = &IRBuilderPluginBridge::buildStore;
  API.BuildGetElementPtr = &IRBuilderPluginBridge::buildGetElementPtr;
  API.BuildCall = &IRBuilderPluginBridge::buildCall;
  API.BuildPhi = &IRBuilderPluginBridge::buildPhi;
  API.AddPhiIncoming = &IRBuilderPluginBridge::addPhiIncoming;
  API.BuildBranch = &IRBuilderPluginBridge::buildBranch;
  API.BuildConditionalBranch =
      &IRBuilderPluginBridge::buildConditionalBranch;
  API.BuildUnreachable = &IRBuilderPluginBridge::buildUnreachable;
  API.BuildReturn = &IRBuilderPluginBridge::buildReturn;
  API.BuildReturnVoid = &IRBuilderPluginBridge::buildReturnVoid;
  API.CreateFunction = &IRBuilderPluginBridge::createFunction;
  API.CreateBasicBlock = &IRBuilderPluginBridge::createBasicBlock;
  return Error::success();
}

bool IRBuilderPluginBridge::validTask(NevercTaskHandle Task) const {
  NevercTaskHandle Expected = Bridge.taskHandle();
  return Task.Owner == Expected.Owner && Task.Value == Expected.Value;
}

NevercStatus
IRBuilderPluginBridge::resolveMutation(NevercIRMutationHandle Handle,
                                       Mutation **OutMutation) const {
  NevercStatus MutationStatus = Bridge.checkMutationAllowed();
  if (MutationStatus.Code != NEVERC_STATUS_OK)
    return MutationStatus;
  if (OutMutation == nullptr)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                                      "mutation output is null");
  void *Payload = nullptr;
  NevercStatus Status = Bridge.Task.handles().resolve(
      Handle, PluginIRMutationHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  *OutMutation = static_cast<Mutation *>(Payload);
  return IRPluginBridge::successStatus();
}

NevercStatus
IRBuilderPluginBridge::resolveBuilder(NevercIRBuilderHandle Handle,
                                      Builder **OutBuilder) const {
  NevercStatus MutationStatus = Bridge.checkMutationAllowed();
  if (MutationStatus.Code != NEVERC_STATUS_OK)
    return MutationStatus;
  if (OutBuilder == nullptr)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                                      "builder output is null");
  void *Payload = nullptr;
  NevercStatus Status = Bridge.Task.handles().resolve(
      Handle, PluginIRBuilderHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  *OutBuilder = static_cast<Builder *>(Payload);
  return IRPluginBridge::successStatus();
}

NevercStatus
IRBuilderPluginBridge::validateBuilder(Builder &BuilderValue) const {
  if (BuilderValue.Lease == nullptr ||
      BuilderValue.Lease->CurrentState != Mutation::State::Active)
    return IRPluginBridge::makeStatus(
        NEVERC_STATUS_INVALID_STATE,
        "IR builder mutation lease is no longer active");
  if (BuilderValue.Value->GetInsertBlock() == nullptr)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_STATE,
                                      "IR builder has no insertion point");
  return IRPluginBridge::successStatus();
}

NevercStatus IRBuilderPluginBridge::publishInstruction(
    Builder &BuilderValue, Instruction &InstructionValue,
    NevercIRValueHandle *OutInstruction) {
  if (OutInstruction == nullptr) {
    InstructionValue.eraseFromParent();
    return IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                                      "instruction output is null");
  }
  auto Wrapped = Bridge.wrapValue(InstructionValue);
  if (!Wrapped) {
    InstructionValue.eraseFromParent();
    return IRPluginBridge::makeStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED,
                                      "failed to allocate IR value handle");
  }
  BuilderValue.Lease->CreatedValues.emplace_back(&InstructionValue);
  *OutInstruction = *Wrapped;
  return IRPluginBridge::successStatus();
}

bool IRBuilderPluginBridge::blockInScope(const Mutation &MutationValue,
                                         const BasicBlock &Block) const {
  if (MutationValue.Scope == NEVERC_IR_MUTATION_SCOPE_MODULE)
    return Block.getModule() == &Bridge.module();
  if (MutationValue.Scope == NEVERC_IR_MUTATION_SCOPE_LOOP)
    return std::find(MutationValue.LoopBlocks.begin(),
                     MutationValue.LoopBlocks.end(),
                     &Block) != MutationValue.LoopBlocks.end();
  return MutationValue.FunctionScope != nullptr &&
         Block.getParent() == MutationValue.FunctionScope;
}

void IRBuilderPluginBridge::rollback(Mutation &MutationValue) {
  for (auto It = MutationValue.PhiIncomingEdits.rbegin();
       It != MutationValue.PhiIncomingEdits.rend(); ++It) {
    auto *Phi = dyn_cast_or_null<PHINode>(static_cast<Value *>(*It));
    if (Phi != nullptr && Phi->getNumIncomingValues() != 0)
      Phi->removeIncomingValue(Phi->getNumIncomingValues() - 1, false);
  }
  MutationValue.PhiIncomingEdits.clear();
  for (auto It = MutationValue.CreatedValues.rbegin();
       It != MutationValue.CreatedValues.rend(); ++It) {
    Value *Created = *It;
    if (Created == nullptr)
      continue;
    auto HandleIt = Bridge.ValueHandles.find(Created);
    if (HandleIt != Bridge.ValueHandles.end()) {
      (void)Bridge.Task.handles().release(HandleIt->second,
                                          PluginIRValueHandleKind);
      Bridge.ValueHandles.erase(HandleIt);
    }
    if (auto *InstructionValue = dyn_cast<Instruction>(Created)) {
      if (InstructionValue->getParent() != nullptr)
        InstructionValue->eraseFromParent();
    } else if (auto *Block = dyn_cast<BasicBlock>(Created)) {
      if (Block->getParent() != nullptr)
        Block->eraseFromParent();
    } else if (auto *Global = dyn_cast<GlobalValue>(Created)) {
      if (Global->getParent() != nullptr)
        Global->eraseFromParent();
    }
  }
  MutationValue.CreatedValues.clear();
  MutationValue.CurrentState = Mutation::State::Aborted;
}

void IRBuilderPluginBridge::forgetMutationHandle(
    NevercIRMutationHandle Handle) {
  MutationHandles.erase(
      std::remove_if(MutationHandles.begin(), MutationHandles.end(),
                     [Handle](NevercIRMutationHandle Candidate) {
                       return Candidate.Value == Handle.Value;
                     }),
      MutationHandles.end());
}

void IRBuilderPluginBridge::forgetBuilderHandle(NevercIRBuilderHandle Handle) {
  BuilderHandles.erase(
      std::remove_if(BuilderHandles.begin(), BuilderHandles.end(),
                     [Handle](NevercIRBuilderHandle Candidate) {
                       return Candidate.Value == Handle.Value;
                     }),
      BuilderHandles.end());
}

NevercStatus NEVERC_CALL IRBuilderPluginBridge::beginMutation(
    void *Context, NevercTaskHandle Task, NevercIRMutationScope Scope,
    NevercIRValueHandle ScopeRoot, NevercIRMutationHandle *OutMutation) {
  auto *Self = asBridge<IRBuilderPluginBridge>(Context);
  if (Self == nullptr || OutMutation == nullptr)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                                      "invalid mutation arguments");
  if (!Self->validTask(Task))
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_SCOPE,
                                      "task handle does not own IR bridge");
  NevercStatus MutationStatus = Self->Bridge.checkMutationAllowed();
  if (MutationStatus.Code != NEVERC_STATUS_OK)
    return MutationStatus;
  *OutMutation = NevercIRMutationHandle{};

  for (NevercIRMutationHandle Handle : Self->MutationHandles) {
    Mutation *Existing = nullptr;
    if (Self->resolveMutation(Handle, &Existing).Code == NEVERC_STATUS_OK &&
        Existing->CurrentState == Mutation::State::Active)
      return IRPluginBridge::makeStatus(
          NEVERC_STATUS_BUSY, "another IR mutation lease is already active");
  }

  auto *Value = new (std::nothrow) Mutation();
  if (Value == nullptr)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED,
                                      "failed to allocate IR mutation lease");
  Value->Scope = Scope;

  if (Scope == NEVERC_IR_MUTATION_SCOPE_FUNCTION) {
    llvm::Value *Resolved = nullptr;
    NevercStatus Status = Self->Bridge.resolveValue(ScopeRoot, &Resolved);
    if (Status.Code != NEVERC_STATUS_OK) {
      delete Value;
      return Status;
    }
    Value->FunctionScope = dyn_cast<Function>(Resolved);
    if (Value->FunctionScope == nullptr) {
      delete Value;
      return IRPluginBridge::makeStatus(
          NEVERC_STATUS_WRONG_TYPE,
          "function mutation scope root is not an LLVM function");
    }
  } else if (Scope == NEVERC_IR_MUTATION_SCOPE_LOOP) {
    llvm::Value *Resolved = nullptr;
    NevercStatus Status = Self->Bridge.resolveValue(ScopeRoot, &Resolved);
    if (Status.Code != NEVERC_STATUS_OK) {
      delete Value;
      return Status;
    }
    Value->LoopHeader = dyn_cast<BasicBlock>(Resolved);
    if (Value->LoopHeader == nullptr ||
        Value->LoopHeader->getParent() == nullptr) {
      delete Value;
      return IRPluginBridge::makeStatus(
          NEVERC_STATUS_WRONG_TYPE,
          "loop mutation scope root is not an attached basic block");
    }
    Value->FunctionScope = Value->LoopHeader->getParent();
    DominatorTree Dominators(*Value->FunctionScope);
    LoopInfo Loops(Dominators);
    Loop *ResolvedLoop = Loops.getLoopFor(Value->LoopHeader);
    if (ResolvedLoop == nullptr ||
        ResolvedLoop->getHeader() != Value->LoopHeader) {
      delete Value;
      return IRPluginBridge::makeStatus(
          NEVERC_STATUS_WRONG_SCOPE,
          "loop mutation scope root is not a natural loop header");
    }
    Value->LoopBlocks.assign(ResolvedLoop->block_begin(),
                             ResolvedLoop->block_end());
  } else if (Scope != NEVERC_IR_MUTATION_SCOPE_MODULE) {
    delete Value;
    return IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                                      "unknown IR mutation scope");
  }

  auto Created = Self->Bridge.Task.handles().create(
      PluginIRMutationHandleKind, Value,
      [](void *Payload) { delete static_cast<Mutation *>(Payload); });
  if (!Created) {
    delete Value;
    consumeError(Created.takeError());
    return IRPluginBridge::makeStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED,
                                      "failed to allocate mutation handle");
  }
  *OutMutation = *Created;
  Self->MutationHandles.push_back(*OutMutation);
  return IRPluginBridge::successStatus();
}

NevercStatus NEVERC_CALL IRBuilderPluginBridge::commitMutation(
    void *Context, NevercTaskHandle Task, NevercIRMutationHandle MutationHandle) {
  auto *Self = asBridge<IRBuilderPluginBridge>(Context);
  if (Self == nullptr)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                                      "IR builder context is null");
  if (!Self->validTask(Task))
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_SCOPE,
                                      "task handle does not own IR bridge");
  Mutation *Value = nullptr;
  NevercStatus Status = Self->resolveMutation(MutationHandle, &Value);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Value->CurrentState != Mutation::State::Active)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_STATE,
                                      "IR mutation lease is not active");

  std::string VerificationMessage;
  raw_string_ostream Stream(VerificationMessage);
  bool Broken =
      Value->Scope == NEVERC_IR_MUTATION_SCOPE_MODULE
          ? verifyModule(Self->Bridge.module(), &Stream)
          : verifyFunction(*Value->FunctionScope, &Stream);
  Stream.flush();
  if (Broken) {
    Self->rollback(*Value);
    return IRPluginBridge::makeStatus(NEVERC_STATUS_VERIFICATION_FAILED,
                                      VerificationMessage);
  }
  Value->CurrentState = Mutation::State::Committed;
  return IRPluginBridge::successStatus();
}

NevercStatus NEVERC_CALL IRBuilderPluginBridge::abortMutation(
    void *Context, NevercTaskHandle Task, NevercIRMutationHandle MutationHandle) {
  auto *Self = asBridge<IRBuilderPluginBridge>(Context);
  if (Self == nullptr)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                                      "IR builder context is null");
  if (!Self->validTask(Task))
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_SCOPE,
                                      "task handle does not own IR bridge");
  Mutation *Value = nullptr;
  NevercStatus Status = Self->resolveMutation(MutationHandle, &Value);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Value->CurrentState != Mutation::State::Active)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_STATE,
                                      "IR mutation lease is not active");
  Self->rollback(*Value);
  return IRPluginBridge::successStatus();
}

NevercStatus NEVERC_CALL IRBuilderPluginBridge::destroyMutation(
    void *Context, NevercTaskHandle Task, NevercIRMutationHandle MutationHandle) {
  auto *Self = asBridge<IRBuilderPluginBridge>(Context);
  if (Self == nullptr)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                                      "IR builder context is null");
  if (!Self->validTask(Task))
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_SCOPE,
                                      "task handle does not own IR bridge");
  Mutation *Value = nullptr;
  NevercStatus Status = Self->resolveMutation(MutationHandle, &Value);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Value->BuilderCount != 0)
    return IRPluginBridge::makeStatus(
        NEVERC_STATUS_BUSY, "IR mutation still owns live builders");
  if (Value->CurrentState == Mutation::State::Active)
    Self->rollback(*Value);
  Status = Self->Bridge.Task.handles().release(
      MutationHandle, PluginIRMutationHandleKind);
  if (Status.Code == NEVERC_STATUS_OK)
    Self->forgetMutationHandle(MutationHandle);
  return Status;
}

NevercStatus NEVERC_CALL IRBuilderPluginBridge::createBuilder(
    void *Context, NevercTaskHandle Task, NevercIRMutationHandle MutationHandle,
    NevercIRBuilderHandle *OutBuilder) {
  auto *Self = asBridge<IRBuilderPluginBridge>(Context);
  if (Self == nullptr || OutBuilder == nullptr)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                                      "invalid builder arguments");
  if (!Self->validTask(Task))
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_SCOPE,
                                      "task handle does not own IR bridge");
  *OutBuilder = NevercIRBuilderHandle{};
  Mutation *Lease = nullptr;
  NevercStatus Status = Self->resolveMutation(MutationHandle, &Lease);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Lease->CurrentState != Mutation::State::Active)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_STATE,
                                      "IR mutation lease is not active");

  auto *Value = new (std::nothrow) Builder(*Lease, Self->Bridge.context());
  if (Value == nullptr)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED,
                                      "failed to allocate IR builder");
  auto Created = Self->Bridge.Task.handles().create(
      PluginIRBuilderHandleKind, Value,
      [](void *Payload) { delete static_cast<Builder *>(Payload); });
  if (!Created) {
    delete Value;
    consumeError(Created.takeError());
    return IRPluginBridge::makeStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED,
                                      "failed to allocate builder handle");
  }
  ++Lease->BuilderCount;
  *OutBuilder = *Created;
  Self->BuilderHandles.push_back(*OutBuilder);
  return IRPluginBridge::successStatus();
}

NevercStatus NEVERC_CALL IRBuilderPluginBridge::destroyBuilder(
    void *Context, NevercTaskHandle Task, NevercIRBuilderHandle BuilderHandle) {
  auto *Self = asBridge<IRBuilderPluginBridge>(Context);
  if (Self == nullptr)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                                      "IR builder context is null");
  if (!Self->validTask(Task))
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_SCOPE,
                                      "task handle does not own IR bridge");
  Builder *Value = nullptr;
  NevercStatus Status = Self->resolveBuilder(BuilderHandle, &Value);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Value->Lease != nullptr && Value->Lease->BuilderCount != 0)
    --Value->Lease->BuilderCount;
  Status = Self->Bridge.Task.handles().release(BuilderHandle,
                                               PluginIRBuilderHandleKind);
  if (Status.Code == NEVERC_STATUS_OK)
    Self->forgetBuilderHandle(BuilderHandle);
  return Status;
}

NevercStatus NEVERC_CALL IRBuilderPluginBridge::setInsertBlock(
    void *Context, NevercTaskHandle Task, NevercIRBuilderHandle BuilderHandle,
    NevercIRValueHandle BlockHandle) {
  auto *Self = asBridge<IRBuilderPluginBridge>(Context);
  if (Self == nullptr)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                                      "IR builder context is null");
  if (!Self->validTask(Task))
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_SCOPE,
                                      "task handle does not own IR bridge");
  Builder *Value = nullptr;
  NevercStatus Status = Self->resolveBuilder(BuilderHandle, &Value);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Value->Lease->CurrentState != Mutation::State::Active)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_STATE,
                                      "IR mutation lease is not active");
  llvm::Value *Resolved = nullptr;
  Status = Self->Bridge.resolveValue(BlockHandle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Block = dyn_cast<BasicBlock>(Resolved);
  if (Block == nullptr)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_TYPE,
                                      "insertion point is not a basic block");
  if (!Self->blockInScope(*Value->Lease, *Block))
    return IRPluginBridge::makeStatus(
        NEVERC_STATUS_WRONG_SCOPE,
        "insertion block lies outside the mutation scope");
  Value->Value->SetInsertPoint(Block);
  return IRPluginBridge::successStatus();
}

NevercStatus NEVERC_CALL IRBuilderPluginBridge::setInsertBefore(
    void *Context, NevercTaskHandle Task, NevercIRBuilderHandle BuilderHandle,
    NevercIRValueHandle InstructionHandle) {
  auto *Self = asBridge<IRBuilderPluginBridge>(Context);
  if (Self == nullptr)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                                      "IR builder context is null");
  if (!Self->validTask(Task))
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_SCOPE,
                                      "task handle does not own IR bridge");
  Builder *Value = nullptr;
  NevercStatus Status = Self->resolveBuilder(BuilderHandle, &Value);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Value->Lease->CurrentState != Mutation::State::Active)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_STATE,
                                      "IR mutation lease is not active");
  llvm::Value *Resolved = nullptr;
  Status = Self->Bridge.resolveValue(InstructionHandle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *InstructionValue = dyn_cast<Instruction>(Resolved);
  if (InstructionValue == nullptr || InstructionValue->getParent() == nullptr)
    return IRPluginBridge::makeStatus(
        NEVERC_STATUS_WRONG_TYPE,
        "insertion point is not an attached LLVM instruction");
  if (!Self->blockInScope(*Value->Lease, *InstructionValue->getParent()))
    return IRPluginBridge::makeStatus(
        NEVERC_STATUS_WRONG_SCOPE,
        "insertion instruction lies outside the mutation scope");
  Value->Value->SetInsertPoint(InstructionValue);
  return IRPluginBridge::successStatus();
}

NevercStatus NEVERC_CALL IRBuilderPluginBridge::setDebugLocation(
    void *Context, NevercTaskHandle Task, NevercIRBuilderHandle BuilderHandle,
    NevercIRMetadataHandle LocationHandle) {
  auto *Self = asBridge<IRBuilderPluginBridge>(Context);
  if (Self == nullptr)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                                      "IR builder context is null");
  if (!Self->validTask(Task))
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_SCOPE,
                                      "task handle does not own IR bridge");
  Builder *Value = nullptr;
  NevercStatus Status = Self->resolveBuilder(BuilderHandle, &Value);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Value->Lease->CurrentState != Mutation::State::Active)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_STATE,
                                      "IR mutation lease is not active");
  Metadata *Resolved = nullptr;
  Status = Self->Bridge.resolveMetadata(LocationHandle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Location = dyn_cast<DILocation>(Resolved);
  if (Location == nullptr)
    return IRPluginBridge::makeStatus(
        NEVERC_STATUS_WRONG_TYPE,
        "builder debug location is not DILocation metadata");
  Value->Value->SetCurrentDebugLocation(DebugLoc(Location));
  return IRPluginBridge::successStatus();
}

NevercStatus NEVERC_CALL IRBuilderPluginBridge::setFastMathFlags(
    void *Context, NevercTaskHandle Task, NevercIRBuilderHandle BuilderHandle,
    NevercIRFastMathFlags Flags) {
  auto *Self = asBridge<IRBuilderPluginBridge>(Context);
  if (Self == nullptr)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                                      "IR builder context is null");
  if (!Self->validTask(Task))
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_SCOPE,
                                      "task handle does not own IR bridge");
  constexpr NevercIRFastMathFlags KnownFlags =
      NEVERC_IR_FAST_MATH_ALLOW_REASSOC | NEVERC_IR_FAST_MATH_NO_NANS |
      NEVERC_IR_FAST_MATH_NO_INFS | NEVERC_IR_FAST_MATH_NO_SIGNED_ZEROS |
      NEVERC_IR_FAST_MATH_ALLOW_RECIPROCAL |
      NEVERC_IR_FAST_MATH_ALLOW_CONTRACT | NEVERC_IR_FAST_MATH_APPROX_FUNC;
  if ((Flags & ~KnownFlags) != 0)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                                      "unknown fast-math flag");
  Builder *Value = nullptr;
  NevercStatus Status = Self->resolveBuilder(BuilderHandle, &Value);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Value->Lease->CurrentState != Mutation::State::Active)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_STATE,
                                      "IR mutation lease is not active");
  FastMathFlags LLVMFlags;
  LLVMFlags.setAllowReassoc((Flags & NEVERC_IR_FAST_MATH_ALLOW_REASSOC) != 0);
  LLVMFlags.setNoNaNs((Flags & NEVERC_IR_FAST_MATH_NO_NANS) != 0);
  LLVMFlags.setNoInfs((Flags & NEVERC_IR_FAST_MATH_NO_INFS) != 0);
  LLVMFlags.setNoSignedZeros(
      (Flags & NEVERC_IR_FAST_MATH_NO_SIGNED_ZEROS) != 0);
  LLVMFlags.setAllowReciprocal(
      (Flags & NEVERC_IR_FAST_MATH_ALLOW_RECIPROCAL) != 0);
  LLVMFlags.setAllowContract(
      (Flags & NEVERC_IR_FAST_MATH_ALLOW_CONTRACT) != 0);
  LLVMFlags.setApproxFunc((Flags & NEVERC_IR_FAST_MATH_APPROX_FUNC) != 0);
  Value->Value->setFastMathFlags(LLVMFlags);
  return IRPluginBridge::successStatus();
}

NevercStatus NEVERC_CALL IRBuilderPluginBridge::buildBinary(
    void *Context, NevercTaskHandle Task, NevercIRBuilderHandle BuilderHandle,
    NevercIROpcode Opcode, NevercIRValueHandle LeftHandle,
    NevercIRValueHandle RightHandle, NevercStringView Name,
    NevercIRValueHandle *OutInstruction) {
  auto *Self = asBridge<IRBuilderPluginBridge>(Context);
  if (Self == nullptr || OutInstruction == nullptr)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                                      "invalid binary builder arguments");
  if (!Self->validTask(Task))
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_SCOPE,
                                      "task handle does not own IR bridge");
  *OutInstruction = NevercIRValueHandle{};
  Builder *BuilderValue = nullptr;
  NevercStatus Status = Self->resolveBuilder(BuilderHandle, &BuilderValue);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = Self->validateBuilder(*BuilderValue);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  llvm::Value *Left = nullptr;
  llvm::Value *Right = nullptr;
  if ((Status = Self->Bridge.resolveValue(LeftHandle, &Left)).Code !=
      NEVERC_STATUS_OK)
    return Status;
  if ((Status = Self->Bridge.resolveValue(RightHandle, &Right)).Code !=
      NEVERC_STATUS_OK)
    return Status;
  Instruction::BinaryOps LLVMOpcode = binaryOpcode(Opcode);
  if (LLVMOpcode == Instruction::BinaryOpsEnd)
    return IRPluginBridge::makeStatus(
        NEVERC_STATUS_INVALID_ARGUMENT,
        "opcode is not supported by the binary builder");
  if (Left->getType() != Right->getType())
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_TYPE,
                                      "binary operands have different types");
  Type *OperandType = Left->getType();
  if (isFloatingBinary(LLVMOpcode) ? !OperandType->isFPOrFPVectorTy()
                                   : !OperandType->isIntOrIntVectorTy())
    return IRPluginBridge::makeStatus(
        NEVERC_STATUS_WRONG_TYPE,
        "binary operand type is incompatible with the opcode");
  StringRef NameValue;
  Status = IRPluginBridge::viewToString(Name, &NameValue);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Instruction *Created =
      cast<Instruction>(BuilderValue->Value->CreateBinOp(
          LLVMOpcode, Left, Right, NameValue));
  auto Wrapped = Self->Bridge.wrapValue(*Created);
  if (!Wrapped) {
    Created->eraseFromParent();
    return IRPluginBridge::makeStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED,
                                      "failed to allocate IR value handle");
  }
  BuilderValue->Lease->CreatedValues.emplace_back(Created);
  *OutInstruction = *Wrapped;
  return IRPluginBridge::successStatus();
}

NevercStatus NEVERC_CALL IRBuilderPluginBridge::buildUnary(
    void *Context, NevercTaskHandle Task, NevercIRBuilderHandle BuilderHandle,
    NevercIROpcode Opcode, NevercIRValueHandle OperandHandle,
    NevercStringView Name, NevercIRValueHandle *OutInstruction) {
  auto *Self = asBridge<IRBuilderPluginBridge>(Context);
  if (Self == nullptr || OutInstruction == nullptr)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                                      "invalid unary builder arguments");
  if (!Self->validTask(Task))
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_SCOPE,
                                      "task handle does not own IR bridge");
  *OutInstruction = {};
  Builder *BuilderValue = nullptr;
  NevercStatus Status = Self->resolveBuilder(BuilderHandle, &BuilderValue);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if ((Status = Self->validateBuilder(*BuilderValue)).Code != NEVERC_STATUS_OK)
    return Status;
  Value *Operand = nullptr;
  if ((Status = Self->Bridge.resolveValue(OperandHandle, &Operand)).Code !=
      NEVERC_STATUS_OK)
    return Status;
  StringRef NameValue;
  if ((Status = IRPluginBridge::viewToString(Name, &NameValue)).Code !=
      NEVERC_STATUS_OK)
    return Status;

  Instruction *Created = nullptr;
  if (Opcode == NEVERC_IR_OPCODE_F_NEG) {
    if (!Operand->getType()->isFPOrFPVectorTy())
      return IRPluginBridge::makeStatus(
          NEVERC_STATUS_WRONG_TYPE,
          "fneg operand must have floating-point type");
    Created =
        cast<Instruction>(BuilderValue->Value->CreateFNeg(Operand, NameValue));
  } else if (Opcode == NEVERC_IR_OPCODE_FREEZE) {
    if (!Operand->getType()->isFirstClassType())
      return IRPluginBridge::makeStatus(
          NEVERC_STATUS_WRONG_TYPE,
          "freeze operand must have first-class type");
    Created = cast<Instruction>(
        BuilderValue->Value->CreateFreeze(Operand, NameValue));
  } else {
    return IRPluginBridge::makeStatus(
        NEVERC_STATUS_INVALID_ARGUMENT,
        "opcode is not supported by the unary builder");
  }
  return Self->publishInstruction(*BuilderValue, *Created, OutInstruction);
}

NevercStatus NEVERC_CALL IRBuilderPluginBridge::buildCompare(
    void *Context, NevercTaskHandle Task, NevercIRBuilderHandle BuilderHandle,
    NevercIRPredicate Predicate, NevercIRValueHandle LeftHandle,
    NevercIRValueHandle RightHandle, NevercStringView Name,
    NevercIRValueHandle *OutInstruction) {
  auto *Self = asBridge<IRBuilderPluginBridge>(Context);
  if (Self == nullptr || OutInstruction == nullptr)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                                      "invalid compare builder arguments");
  if (!Self->validTask(Task))
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_SCOPE,
                                      "task handle does not own IR bridge");
  *OutInstruction = {};
  Builder *BuilderValue = nullptr;
  NevercStatus Status = Self->resolveBuilder(BuilderHandle, &BuilderValue);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if ((Status = Self->validateBuilder(*BuilderValue)).Code != NEVERC_STATUS_OK)
    return Status;
  Value *Left = nullptr;
  Value *Right = nullptr;
  if ((Status = Self->Bridge.resolveValue(LeftHandle, &Left)).Code !=
          NEVERC_STATUS_OK ||
      (Status = Self->Bridge.resolveValue(RightHandle, &Right)).Code !=
          NEVERC_STATUS_OK)
    return Status;
  if (Left->getType() != Right->getType())
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_TYPE,
                                      "compare operands have different types");
  if ((Predicate & UINT32_C(0xffffff00)) != UINT32_C(0x44000000) ||
      (Predicate & UINT32_C(0xff)) == 0 ||
      (Predicate & UINT32_C(0xff)) > 26)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                                      "unknown comparison predicate");
  uint32_t PredicateIndex = Predicate & UINT32_C(0xff);
  auto LLVMPredicate = static_cast<CmpInst::Predicate>(
      PredicateIndex <= 16 ? PredicateIndex - 1
                           : 32 + (PredicateIndex - 17));
  if (CmpInst::isFPPredicate(LLVMPredicate)
          ? !Left->getType()->isFPOrFPVectorTy()
          : !Left->getType()->isIntOrIntVectorTy() &&
                !Left->getType()->isPtrOrPtrVectorTy())
    return IRPluginBridge::makeStatus(
        NEVERC_STATUS_WRONG_TYPE,
        "compare operand type is incompatible with the predicate");
  StringRef NameValue;
  if ((Status = IRPluginBridge::viewToString(Name, &NameValue)).Code !=
      NEVERC_STATUS_OK)
    return Status;
  Instruction *Created = cast<Instruction>(
      BuilderValue->Value->CreateCmp(LLVMPredicate, Left, Right, NameValue));
  return Self->publishInstruction(*BuilderValue, *Created, OutInstruction);
}

NevercStatus NEVERC_CALL IRBuilderPluginBridge::buildCast(
    void *Context, NevercTaskHandle Task, NevercIRBuilderHandle BuilderHandle,
    NevercIROpcode Opcode, NevercIRValueHandle OperandHandle,
    NevercIRTypeHandle DestinationTypeHandle, NevercStringView Name,
    NevercIRValueHandle *OutInstruction) {
  auto *Self = asBridge<IRBuilderPluginBridge>(Context);
  if (Self == nullptr || OutInstruction == nullptr)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                                      "invalid cast builder arguments");
  if (!Self->validTask(Task))
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_SCOPE,
                                      "task handle does not own IR bridge");
  *OutInstruction = {};
  Builder *BuilderValue = nullptr;
  NevercStatus Status = Self->resolveBuilder(BuilderHandle, &BuilderValue);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if ((Status = Self->validateBuilder(*BuilderValue)).Code != NEVERC_STATUS_OK)
    return Status;
  Value *Operand = nullptr;
  Type *DestinationType = nullptr;
  if ((Status = Self->Bridge.resolveValue(OperandHandle, &Operand)).Code !=
          NEVERC_STATUS_OK ||
      (Status = Self->Bridge.resolveType(DestinationTypeHandle,
                                         &DestinationType))
              .Code != NEVERC_STATUS_OK)
    return Status;
  Instruction::CastOps LLVMOpcode;
  switch (Opcode) {
  case NEVERC_IR_OPCODE_TRUNC:
    LLVMOpcode = Instruction::Trunc;
    break;
  case NEVERC_IR_OPCODE_Z_EXT:
    LLVMOpcode = Instruction::ZExt;
    break;
  case NEVERC_IR_OPCODE_S_EXT:
    LLVMOpcode = Instruction::SExt;
    break;
  case NEVERC_IR_OPCODE_FP_TO_UI:
    LLVMOpcode = Instruction::FPToUI;
    break;
  case NEVERC_IR_OPCODE_FP_TO_SI:
    LLVMOpcode = Instruction::FPToSI;
    break;
  case NEVERC_IR_OPCODE_UI_TO_FP:
    LLVMOpcode = Instruction::UIToFP;
    break;
  case NEVERC_IR_OPCODE_SI_TO_FP:
    LLVMOpcode = Instruction::SIToFP;
    break;
  case NEVERC_IR_OPCODE_FP_TRUNC:
    LLVMOpcode = Instruction::FPTrunc;
    break;
  case NEVERC_IR_OPCODE_FP_EXT:
    LLVMOpcode = Instruction::FPExt;
    break;
  case NEVERC_IR_OPCODE_PTR_TO_INT:
    LLVMOpcode = Instruction::PtrToInt;
    break;
  case NEVERC_IR_OPCODE_INT_TO_PTR:
    LLVMOpcode = Instruction::IntToPtr;
    break;
  case NEVERC_IR_OPCODE_BIT_CAST:
    LLVMOpcode = Instruction::BitCast;
    break;
  case NEVERC_IR_OPCODE_ADDR_SPACE_CAST:
    LLVMOpcode = Instruction::AddrSpaceCast;
    break;
  default:
    return IRPluginBridge::makeStatus(
        NEVERC_STATUS_INVALID_ARGUMENT,
        "opcode is not supported by the cast builder");
  }
  if (!CastInst::castIsValid(LLVMOpcode, Operand, DestinationType))
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_TYPE,
                                      "invalid LLVM cast");
  StringRef NameValue;
  if ((Status = IRPluginBridge::viewToString(Name, &NameValue)).Code !=
      NEVERC_STATUS_OK)
    return Status;
  Instruction *Created = cast<Instruction>(BuilderValue->Value->CreateCast(
      LLVMOpcode, Operand, DestinationType, NameValue));
  return Self->publishInstruction(*BuilderValue, *Created, OutInstruction);
}

NevercStatus NEVERC_CALL IRBuilderPluginBridge::buildSelect(
    void *Context, NevercTaskHandle Task, NevercIRBuilderHandle BuilderHandle,
    NevercIRValueHandle ConditionHandle, NevercIRValueHandle TrueHandle,
    NevercIRValueHandle FalseHandle, NevercStringView Name,
    NevercIRValueHandle *OutInstruction) {
  auto *Self = asBridge<IRBuilderPluginBridge>(Context);
  if (Self == nullptr || OutInstruction == nullptr)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                                      "invalid select builder arguments");
  if (!Self->validTask(Task))
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_SCOPE,
                                      "task handle does not own IR bridge");
  *OutInstruction = {};
  Builder *BuilderValue = nullptr;
  NevercStatus Status = Self->resolveBuilder(BuilderHandle, &BuilderValue);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if ((Status = Self->validateBuilder(*BuilderValue)).Code != NEVERC_STATUS_OK)
    return Status;
  Value *Condition = nullptr;
  Value *TrueValue = nullptr;
  Value *FalseValue = nullptr;
  if ((Status = Self->Bridge.resolveValue(ConditionHandle, &Condition)).Code !=
          NEVERC_STATUS_OK ||
      (Status = Self->Bridge.resolveValue(TrueHandle, &TrueValue)).Code !=
          NEVERC_STATUS_OK ||
      (Status = Self->Bridge.resolveValue(FalseHandle, &FalseValue)).Code !=
          NEVERC_STATUS_OK)
    return Status;
  if (!Condition->getType()->isIntOrIntVectorTy(1) ||
      TrueValue->getType() != FalseValue->getType())
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_TYPE,
                                      "invalid select operand types");
  StringRef NameValue;
  if ((Status = IRPluginBridge::viewToString(Name, &NameValue)).Code !=
      NEVERC_STATUS_OK)
    return Status;
  Instruction *Created = cast<Instruction>(
      BuilderValue->Value->CreateSelect(Condition, TrueValue, FalseValue,
                                        NameValue));
  return Self->publishInstruction(*BuilderValue, *Created, OutInstruction);
}

NevercStatus NEVERC_CALL IRBuilderPluginBridge::buildAlloca(
    void *Context, NevercTaskHandle Task, NevercIRBuilderHandle BuilderHandle,
    NevercIRTypeHandle AllocatedTypeHandle, uint32_t AddressSpace,
    NevercIRValueHandle ArraySizeHandle, NevercStringView Name,
    NevercIRValueHandle *OutInstruction) {
  auto *Self = asBridge<IRBuilderPluginBridge>(Context);
  if (Self == nullptr || OutInstruction == nullptr)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                                      "invalid alloca builder arguments");
  if (!Self->validTask(Task))
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_SCOPE,
                                      "task handle does not own IR bridge");
  *OutInstruction = {};
  Builder *BuilderValue = nullptr;
  NevercStatus Status = Self->resolveBuilder(BuilderHandle, &BuilderValue);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if ((Status = Self->validateBuilder(*BuilderValue)).Code != NEVERC_STATUS_OK)
    return Status;
  Type *AllocatedType = nullptr;
  if ((Status = Self->Bridge.resolveType(AllocatedTypeHandle, &AllocatedType))
          .Code != NEVERC_STATUS_OK)
    return Status;
  if (!AllocatedType->isSized())
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_TYPE,
                                      "alloca type must be sized");
  Value *ArraySize = nullptr;
  if (neverc_handle_is_null(ArraySizeHandle) == NEVERC_FALSE) {
    if ((Status = Self->Bridge.resolveValue(ArraySizeHandle, &ArraySize)).Code !=
        NEVERC_STATUS_OK)
      return Status;
    if (!ArraySize->getType()->isIntegerTy())
      return IRPluginBridge::makeStatus(
          NEVERC_STATUS_WRONG_TYPE,
          "alloca array size must have integer type");
  }
  StringRef NameValue;
  if ((Status = IRPluginBridge::viewToString(Name, &NameValue)).Code !=
      NEVERC_STATUS_OK)
    return Status;
  Instruction *Created = BuilderValue->Value->CreateAlloca(
      AllocatedType, AddressSpace, ArraySize, NameValue);
  return Self->publishInstruction(*BuilderValue, *Created, OutInstruction);
}

NevercStatus NEVERC_CALL IRBuilderPluginBridge::buildLoad(
    void *Context, NevercTaskHandle Task, NevercIRBuilderHandle BuilderHandle,
    NevercIRTypeHandle LoadedTypeHandle, NevercIRValueHandle PointerHandle,
    NevercStringView Name, NevercIRValueHandle *OutInstruction) {
  auto *Self = asBridge<IRBuilderPluginBridge>(Context);
  if (Self == nullptr || OutInstruction == nullptr)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                                      "invalid load builder arguments");
  if (!Self->validTask(Task))
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_SCOPE,
                                      "task handle does not own IR bridge");
  *OutInstruction = {};
  Builder *BuilderValue = nullptr;
  NevercStatus Status = Self->resolveBuilder(BuilderHandle, &BuilderValue);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if ((Status = Self->validateBuilder(*BuilderValue)).Code != NEVERC_STATUS_OK)
    return Status;
  Type *LoadedType = nullptr;
  Value *Pointer = nullptr;
  if ((Status = Self->Bridge.resolveType(LoadedTypeHandle, &LoadedType)).Code !=
          NEVERC_STATUS_OK ||
      (Status = Self->Bridge.resolveValue(PointerHandle, &Pointer)).Code !=
          NEVERC_STATUS_OK)
    return Status;
  if (!LoadedType->isFirstClassType() || !Pointer->getType()->isPointerTy())
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_TYPE,
                                      "invalid load type or pointer");
  StringRef NameValue;
  if ((Status = IRPluginBridge::viewToString(Name, &NameValue)).Code !=
      NEVERC_STATUS_OK)
    return Status;
  Instruction *Created =
      BuilderValue->Value->CreateLoad(LoadedType, Pointer, NameValue);
  return Self->publishInstruction(*BuilderValue, *Created, OutInstruction);
}

NevercStatus NEVERC_CALL IRBuilderPluginBridge::buildStore(
    void *Context, NevercTaskHandle Task, NevercIRBuilderHandle BuilderHandle,
    NevercIRValueHandle StoredValueHandle, NevercIRValueHandle PointerHandle,
    NevercIRValueHandle *OutInstruction) {
  auto *Self = asBridge<IRBuilderPluginBridge>(Context);
  if (Self == nullptr || OutInstruction == nullptr)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                                      "invalid store builder arguments");
  if (!Self->validTask(Task))
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_SCOPE,
                                      "task handle does not own IR bridge");
  *OutInstruction = {};
  Builder *BuilderValue = nullptr;
  NevercStatus Status = Self->resolveBuilder(BuilderHandle, &BuilderValue);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if ((Status = Self->validateBuilder(*BuilderValue)).Code != NEVERC_STATUS_OK)
    return Status;
  Value *StoredValue = nullptr;
  Value *Pointer = nullptr;
  if ((Status = Self->Bridge.resolveValue(StoredValueHandle, &StoredValue))
          .Code != NEVERC_STATUS_OK ||
      (Status = Self->Bridge.resolveValue(PointerHandle, &Pointer)).Code !=
          NEVERC_STATUS_OK)
    return Status;
  if (!StoredValue->getType()->isFirstClassType() ||
      !Pointer->getType()->isPointerTy())
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_TYPE,
                                      "invalid store value or pointer");
  Instruction *Created =
      BuilderValue->Value->CreateStore(StoredValue, Pointer);
  return Self->publishInstruction(*BuilderValue, *Created, OutInstruction);
}

NevercStatus NEVERC_CALL IRBuilderPluginBridge::buildGetElementPtr(
    void *Context, NevercTaskHandle Task, NevercIRBuilderHandle BuilderHandle,
    NevercIRTypeHandle SourceTypeHandle, NevercIRValueHandle PointerHandle,
    const NevercIRValueHandle *Indices, uint64_t IndexCount,
    NevercStringView Name, NevercIRValueHandle *OutInstruction) {
  auto *Self = asBridge<IRBuilderPluginBridge>(Context);
  if (Self == nullptr || OutInstruction == nullptr ||
      (Indices == nullptr && IndexCount != 0) ||
      IndexCount > std::numeric_limits<unsigned>::max())
    return IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                                      "invalid GEP builder arguments");
  if (!Self->validTask(Task))
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_SCOPE,
                                      "task handle does not own IR bridge");
  *OutInstruction = {};
  Builder *BuilderValue = nullptr;
  NevercStatus Status = Self->resolveBuilder(BuilderHandle, &BuilderValue);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if ((Status = Self->validateBuilder(*BuilderValue)).Code != NEVERC_STATUS_OK)
    return Status;
  Type *SourceType = nullptr;
  Value *Pointer = nullptr;
  if ((Status = Self->Bridge.resolveType(SourceTypeHandle, &SourceType)).Code !=
          NEVERC_STATUS_OK ||
      (Status = Self->Bridge.resolveValue(PointerHandle, &Pointer)).Code !=
          NEVERC_STATUS_OK)
    return Status;
  if (!SourceType->isSized() || !Pointer->getType()->isPtrOrPtrVectorTy())
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_TYPE,
                                      "invalid GEP source type or pointer");
  SmallVector<Value *, 8> LLVMIndices;
  LLVMIndices.reserve(static_cast<size_t>(IndexCount));
  for (uint64_t I = 0; I != IndexCount; ++I) {
    Value *Index = nullptr;
    if ((Status = Self->Bridge.resolveValue(Indices[I], &Index)).Code !=
        NEVERC_STATUS_OK)
      return Status;
    if (!Index->getType()->isIntOrIntVectorTy())
      return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_TYPE,
                                        "GEP index must have integer type");
    LLVMIndices.push_back(Index);
  }
  StringRef NameValue;
  if ((Status = IRPluginBridge::viewToString(Name, &NameValue)).Code !=
      NEVERC_STATUS_OK)
    return Status;
  Instruction *Created = cast<Instruction>(BuilderValue->Value->CreateGEP(
      SourceType, Pointer, LLVMIndices, NameValue));
  return Self->publishInstruction(*BuilderValue, *Created, OutInstruction);
}

NevercStatus NEVERC_CALL IRBuilderPluginBridge::buildCall(
    void *Context, NevercTaskHandle Task, NevercIRBuilderHandle BuilderHandle,
    NevercIRTypeHandle FunctionTypeHandle, NevercIRValueHandle CalleeHandle,
    const NevercIRValueHandle *Arguments, uint64_t ArgumentCount,
    NevercStringView Name, NevercIRValueHandle *OutInstruction) {
  auto *Self = asBridge<IRBuilderPluginBridge>(Context);
  if (Self == nullptr || OutInstruction == nullptr ||
      (Arguments == nullptr && ArgumentCount != 0) ||
      ArgumentCount > std::numeric_limits<unsigned>::max())
    return IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                                      "invalid call builder arguments");
  if (!Self->validTask(Task))
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_SCOPE,
                                      "task handle does not own IR bridge");
  *OutInstruction = {};
  Builder *BuilderValue = nullptr;
  NevercStatus Status = Self->resolveBuilder(BuilderHandle, &BuilderValue);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if ((Status = Self->validateBuilder(*BuilderValue)).Code != NEVERC_STATUS_OK)
    return Status;
  Type *ResolvedType = nullptr;
  Value *Callee = nullptr;
  if ((Status = Self->Bridge.resolveType(FunctionTypeHandle, &ResolvedType))
          .Code != NEVERC_STATUS_OK ||
      (Status = Self->Bridge.resolveValue(CalleeHandle, &Callee)).Code !=
          NEVERC_STATUS_OK)
    return Status;
  auto *Signature = dyn_cast<FunctionType>(ResolvedType);
  if (Signature == nullptr || !Callee->getType()->isPointerTy())
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_TYPE,
                                      "invalid call signature or callee");
  if ((!Signature->isVarArg() &&
       ArgumentCount != Signature->getNumParams()) ||
      (Signature->isVarArg() &&
       ArgumentCount < Signature->getNumParams()))
    return IRPluginBridge::makeStatus(
        NEVERC_STATUS_INVALID_ARGUMENT,
        "call argument count does not match the signature");
  SmallVector<Value *, 8> LLVMArguments;
  LLVMArguments.reserve(static_cast<size_t>(ArgumentCount));
  for (uint64_t I = 0; I != ArgumentCount; ++I) {
    Value *Argument = nullptr;
    if ((Status = Self->Bridge.resolveValue(Arguments[I], &Argument)).Code !=
        NEVERC_STATUS_OK)
      return Status;
    if (I < Signature->getNumParams() &&
        Argument->getType() != Signature->getParamType(I))
      return IRPluginBridge::makeStatus(
          NEVERC_STATUS_WRONG_TYPE,
          "call argument type does not match the signature");
    LLVMArguments.push_back(Argument);
  }
  StringRef NameValue;
  if ((Status = IRPluginBridge::viewToString(Name, &NameValue)).Code !=
      NEVERC_STATUS_OK)
    return Status;
  Instruction *Created = BuilderValue->Value->CreateCall(
      Signature, Callee, LLVMArguments, NameValue);
  return Self->publishInstruction(*BuilderValue, *Created, OutInstruction);
}

NevercStatus NEVERC_CALL IRBuilderPluginBridge::buildPhi(
    void *Context, NevercTaskHandle Task, NevercIRBuilderHandle BuilderHandle,
    NevercIRTypeHandle TypeHandle, uint32_t ReservedIncomingCount,
    NevercStringView Name, NevercIRValueHandle *OutInstruction) {
  auto *Self = asBridge<IRBuilderPluginBridge>(Context);
  if (Self == nullptr || OutInstruction == nullptr)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                                      "invalid phi builder arguments");
  if (!Self->validTask(Task))
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_SCOPE,
                                      "task handle does not own IR bridge");
  *OutInstruction = {};
  Builder *BuilderValue = nullptr;
  NevercStatus Status = Self->resolveBuilder(BuilderHandle, &BuilderValue);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if ((Status = Self->validateBuilder(*BuilderValue)).Code != NEVERC_STATUS_OK)
    return Status;
  Type *PhiType = nullptr;
  if ((Status = Self->Bridge.resolveType(TypeHandle, &PhiType)).Code !=
      NEVERC_STATUS_OK)
    return Status;
  if (!PhiType->isFirstClassType())
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_TYPE,
                                      "phi type must be first-class");
  StringRef NameValue;
  if ((Status = IRPluginBridge::viewToString(Name, &NameValue)).Code !=
      NEVERC_STATUS_OK)
    return Status;
  Instruction *Created = BuilderValue->Value->CreatePHI(
      PhiType, ReservedIncomingCount, NameValue);
  return Self->publishInstruction(*BuilderValue, *Created, OutInstruction);
}

NevercStatus NEVERC_CALL IRBuilderPluginBridge::addPhiIncoming(
    void *Context, NevercTaskHandle Task,
    NevercIRMutationHandle MutationHandle, NevercIRValueHandle PhiHandle,
    NevercIRValueHandle IncomingValueHandle,
    NevercIRValueHandle IncomingBlockHandle) {
  auto *Self = asBridge<IRBuilderPluginBridge>(Context);
  if (Self == nullptr)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                                      "IR builder context is null");
  if (!Self->validTask(Task))
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_SCOPE,
                                      "task handle does not own IR bridge");
  Mutation *Lease = nullptr;
  NevercStatus Status = Self->resolveMutation(MutationHandle, &Lease);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Lease->CurrentState != Mutation::State::Active)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_STATE,
                                      "IR mutation lease is not active");
  Value *ResolvedPhi = nullptr;
  Value *IncomingValue = nullptr;
  Value *ResolvedBlock = nullptr;
  if ((Status = Self->Bridge.resolveValue(PhiHandle, &ResolvedPhi)).Code !=
          NEVERC_STATUS_OK ||
      (Status =
           Self->Bridge.resolveValue(IncomingValueHandle, &IncomingValue))
              .Code != NEVERC_STATUS_OK ||
      (Status =
           Self->Bridge.resolveValue(IncomingBlockHandle, &ResolvedBlock))
              .Code != NEVERC_STATUS_OK)
    return Status;
  auto *Phi = dyn_cast<PHINode>(ResolvedPhi);
  auto *Block = dyn_cast<BasicBlock>(ResolvedBlock);
  if (Phi == nullptr || Block == nullptr ||
      Phi->getType() != IncomingValue->getType())
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_TYPE,
                                      "invalid phi incoming edge");
  if (Phi->getParent() == nullptr ||
      !Self->blockInScope(*Lease, *Phi->getParent()) ||
      Phi->getFunction() != Block->getParent())
    return IRPluginBridge::makeStatus(
        NEVERC_STATUS_WRONG_SCOPE,
        "phi or incoming block lies outside the mutation function");
  Phi->addIncoming(IncomingValue, Block);
  Lease->PhiIncomingEdits.emplace_back(Phi);
  return IRPluginBridge::successStatus();
}

NevercStatus NEVERC_CALL IRBuilderPluginBridge::buildBranch(
    void *Context, NevercTaskHandle Task, NevercIRBuilderHandle BuilderHandle,
    NevercIRValueHandle DestinationHandle,
    NevercIRValueHandle *OutInstruction) {
  auto *Self = asBridge<IRBuilderPluginBridge>(Context);
  if (Self == nullptr || OutInstruction == nullptr)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                                      "invalid branch builder arguments");
  if (!Self->validTask(Task))
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_SCOPE,
                                      "task handle does not own IR bridge");
  *OutInstruction = {};
  Builder *BuilderValue = nullptr;
  NevercStatus Status = Self->resolveBuilder(BuilderHandle, &BuilderValue);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if ((Status = Self->validateBuilder(*BuilderValue)).Code != NEVERC_STATUS_OK)
    return Status;
  Value *Resolved = nullptr;
  if ((Status = Self->Bridge.resolveValue(DestinationHandle, &Resolved)).Code !=
      NEVERC_STATUS_OK)
    return Status;
  auto *Destination = dyn_cast<BasicBlock>(Resolved);
  BasicBlock *InsertionBlock = BuilderValue->Value->GetInsertBlock();
  if (Destination == nullptr || Destination->getParent() == nullptr)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_TYPE,
                                      "branch destination is not a block");
  if (InsertionBlock->getParent() != Destination->getParent())
    return IRPluginBridge::makeStatus(
        NEVERC_STATUS_WRONG_SCOPE,
        "branch destination belongs to another function");
  Instruction *Created = BuilderValue->Value->CreateBr(Destination);
  return Self->publishInstruction(*BuilderValue, *Created, OutInstruction);
}

NevercStatus NEVERC_CALL IRBuilderPluginBridge::buildConditionalBranch(
    void *Context, NevercTaskHandle Task, NevercIRBuilderHandle BuilderHandle,
    NevercIRValueHandle ConditionHandle,
    NevercIRValueHandle TrueDestinationHandle,
    NevercIRValueHandle FalseDestinationHandle,
    NevercIRValueHandle *OutInstruction) {
  auto *Self = asBridge<IRBuilderPluginBridge>(Context);
  if (Self == nullptr || OutInstruction == nullptr)
    return IRPluginBridge::makeStatus(
        NEVERC_STATUS_INVALID_ARGUMENT,
        "invalid conditional branch builder arguments");
  if (!Self->validTask(Task))
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_SCOPE,
                                      "task handle does not own IR bridge");
  *OutInstruction = {};
  Builder *BuilderValue = nullptr;
  NevercStatus Status = Self->resolveBuilder(BuilderHandle, &BuilderValue);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if ((Status = Self->validateBuilder(*BuilderValue)).Code != NEVERC_STATUS_OK)
    return Status;
  Value *Condition = nullptr;
  Value *ResolvedTrue = nullptr;
  Value *ResolvedFalse = nullptr;
  if ((Status = Self->Bridge.resolveValue(ConditionHandle, &Condition)).Code !=
          NEVERC_STATUS_OK ||
      (Status =
           Self->Bridge.resolveValue(TrueDestinationHandle, &ResolvedTrue))
              .Code != NEVERC_STATUS_OK ||
      (Status =
           Self->Bridge.resolveValue(FalseDestinationHandle, &ResolvedFalse))
              .Code != NEVERC_STATUS_OK)
    return Status;
  auto *TrueDestination = dyn_cast<BasicBlock>(ResolvedTrue);
  auto *FalseDestination = dyn_cast<BasicBlock>(ResolvedFalse);
  BasicBlock *InsertionBlock = BuilderValue->Value->GetInsertBlock();
  if (!Condition->getType()->isIntegerTy(1) || TrueDestination == nullptr ||
      FalseDestination == nullptr)
    return IRPluginBridge::makeStatus(
        NEVERC_STATUS_WRONG_TYPE,
        "conditional branch requires i1 and block operands");
  if (TrueDestination->getParent() != InsertionBlock->getParent() ||
      FalseDestination->getParent() != InsertionBlock->getParent())
    return IRPluginBridge::makeStatus(
        NEVERC_STATUS_WRONG_SCOPE,
        "branch destination belongs to another function");
  Instruction *Created = BuilderValue->Value->CreateCondBr(
      Condition, TrueDestination, FalseDestination);
  return Self->publishInstruction(*BuilderValue, *Created, OutInstruction);
}

NevercStatus NEVERC_CALL IRBuilderPluginBridge::buildUnreachable(
    void *Context, NevercTaskHandle Task, NevercIRBuilderHandle BuilderHandle,
    NevercIRValueHandle *OutInstruction) {
  auto *Self = asBridge<IRBuilderPluginBridge>(Context);
  if (Self == nullptr || OutInstruction == nullptr)
    return IRPluginBridge::makeStatus(
        NEVERC_STATUS_INVALID_ARGUMENT,
        "invalid unreachable builder arguments");
  if (!Self->validTask(Task))
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_SCOPE,
                                      "task handle does not own IR bridge");
  *OutInstruction = {};
  Builder *BuilderValue = nullptr;
  NevercStatus Status = Self->resolveBuilder(BuilderHandle, &BuilderValue);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if ((Status = Self->validateBuilder(*BuilderValue)).Code != NEVERC_STATUS_OK)
    return Status;
  Instruction *Created = BuilderValue->Value->CreateUnreachable();
  return Self->publishInstruction(*BuilderValue, *Created, OutInstruction);
}

NevercStatus NEVERC_CALL IRBuilderPluginBridge::buildReturn(
    void *Context, NevercTaskHandle Task, NevercIRBuilderHandle BuilderHandle,
    NevercIRValueHandle ReturnHandle, NevercIRValueHandle *OutInstruction) {
  auto *Self = asBridge<IRBuilderPluginBridge>(Context);
  if (Self == nullptr || OutInstruction == nullptr)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                                      "invalid return builder arguments");
  if (!Self->validTask(Task))
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_SCOPE,
                                      "task handle does not own IR bridge");
  *OutInstruction = NevercIRValueHandle{};
  Builder *BuilderValue = nullptr;
  NevercStatus Status = Self->resolveBuilder(BuilderHandle, &BuilderValue);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = Self->validateBuilder(*BuilderValue);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  llvm::Value *ReturnValue = nullptr;
  Status = Self->Bridge.resolveValue(ReturnHandle, &ReturnValue);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Function *Parent =
      BuilderValue->Value->GetInsertBlock()->getParent();
  if (Parent == nullptr ||
      Parent->getReturnType() != ReturnValue->getType())
    return IRPluginBridge::makeStatus(
        NEVERC_STATUS_WRONG_TYPE,
        "return value type does not match the enclosing function");
  Instruction *Created = BuilderValue->Value->CreateRet(ReturnValue);
  auto Wrapped = Self->Bridge.wrapValue(*Created);
  if (!Wrapped) {
    Created->eraseFromParent();
    return IRPluginBridge::makeStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED,
                                      "failed to allocate IR value handle");
  }
  BuilderValue->Lease->CreatedValues.emplace_back(Created);
  *OutInstruction = *Wrapped;
  return IRPluginBridge::successStatus();
}

NevercStatus NEVERC_CALL IRBuilderPluginBridge::buildReturnVoid(
    void *Context, NevercTaskHandle Task, NevercIRBuilderHandle BuilderHandle,
    NevercIRValueHandle *OutInstruction) {
  auto *Self = asBridge<IRBuilderPluginBridge>(Context);
  if (Self == nullptr || OutInstruction == nullptr)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                                      "invalid return builder arguments");
  if (!Self->validTask(Task))
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_SCOPE,
                                      "task handle does not own IR bridge");
  *OutInstruction = NevercIRValueHandle{};
  Builder *BuilderValue = nullptr;
  NevercStatus Status = Self->resolveBuilder(BuilderHandle, &BuilderValue);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = Self->validateBuilder(*BuilderValue);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Function *Parent =
      BuilderValue->Value->GetInsertBlock()->getParent();
  if (Parent == nullptr || !Parent->getReturnType()->isVoidTy())
    return IRPluginBridge::makeStatus(
        NEVERC_STATUS_WRONG_TYPE,
        "void return requires an enclosing void function");
  Instruction *Created = BuilderValue->Value->CreateRetVoid();
  auto Wrapped = Self->Bridge.wrapValue(*Created);
  if (!Wrapped) {
    Created->eraseFromParent();
    return IRPluginBridge::makeStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED,
                                      "failed to allocate IR value handle");
  }
  BuilderValue->Lease->CreatedValues.emplace_back(Created);
  *OutInstruction = *Wrapped;
  return IRPluginBridge::successStatus();
}

NevercStatus NEVERC_CALL IRBuilderPluginBridge::createFunction(
    void *Context, NevercTaskHandle Task,
    NevercIRMutationHandle MutationHandle,
    NevercIRTypeHandle FunctionTypeHandle, NevercStringView Name,
    NevercIRValueHandle *OutFunction) {
  auto *Self = asBridge<IRBuilderPluginBridge>(Context);
  if (Self == nullptr || OutFunction == nullptr)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                                      "invalid function creation arguments");
  if (!Self->validTask(Task))
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_SCOPE,
                                      "task handle does not own IR bridge");
  *OutFunction = {};
  Mutation *Lease = nullptr;
  NevercStatus Status = Self->resolveMutation(MutationHandle, &Lease);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Lease->CurrentState != Mutation::State::Active)
    return IRPluginBridge::makeStatus(
        NEVERC_STATUS_INVALID_STATE,
        "IR function creation requires an active mutation");
  if (Lease->Scope != NEVERC_IR_MUTATION_SCOPE_MODULE)
    return IRPluginBridge::makeStatus(
        NEVERC_STATUS_WRONG_SCOPE,
        "IR function creation requires a module mutation");
  Type *ResolvedType = nullptr;
  Status = Self->Bridge.resolveType(FunctionTypeHandle, &ResolvedType);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *FunctionTypeValue = dyn_cast<FunctionType>(ResolvedType);
  if (FunctionTypeValue == nullptr)
    return IRPluginBridge::makeStatus(
        NEVERC_STATUS_WRONG_TYPE,
        "IR function creation requires a function type");
  StringRef NameValue;
  Status = IRPluginBridge::viewToString(Name, &NameValue);
  if (Status.Code != NEVERC_STATUS_OK || NameValue.empty())
    return Status.Code == NEVERC_STATUS_OK
               ? IRPluginBridge::makeStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                                            "IR function name is empty")
               : Status;
  if (Self->Bridge.module().getNamedValue(NameValue) != nullptr)
    return IRPluginBridge::makeStatus(NEVERC_STATUS_DUPLICATE_ID,
                                      "IR function name already exists");

  Function *Created =
      Function::Create(FunctionTypeValue, GlobalValue::ExternalLinkage,
                       NameValue, Self->Bridge.module());
  auto Wrapped = Self->Bridge.wrapValue(*Created);
  if (!Wrapped) {
    Created->eraseFromParent();
    return IRPluginBridge::makeStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED,
                                      "failed to allocate IR function handle");
  }
  Lease->CreatedValues.emplace_back(Created);
  *OutFunction = *Wrapped;
  return IRPluginBridge::successStatus();
}

NevercStatus NEVERC_CALL IRBuilderPluginBridge::createBasicBlock(
    void *Context, NevercTaskHandle Task,
    NevercIRMutationHandle MutationHandle,
    NevercIRValueHandle FunctionHandle, NevercStringView Name,
    NevercIRValueHandle *OutBlock) {
  auto *Self = asBridge<IRBuilderPluginBridge>(Context);
  if (Self == nullptr || OutBlock == nullptr)
    return IRPluginBridge::makeStatus(
        NEVERC_STATUS_INVALID_ARGUMENT,
        "invalid basic block creation arguments");
  if (!Self->validTask(Task))
    return IRPluginBridge::makeStatus(NEVERC_STATUS_WRONG_SCOPE,
                                      "task handle does not own IR bridge");
  *OutBlock = {};
  Mutation *Lease = nullptr;
  NevercStatus Status = Self->resolveMutation(MutationHandle, &Lease);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Lease->CurrentState != Mutation::State::Active)
    return IRPluginBridge::makeStatus(
        NEVERC_STATUS_INVALID_STATE,
        "IR basic block creation requires an active mutation");
  llvm::Value *ResolvedFunction = nullptr;
  Status = Self->Bridge.resolveValue(FunctionHandle, &ResolvedFunction);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *FunctionValue = dyn_cast<Function>(ResolvedFunction);
  if (FunctionValue == nullptr ||
      FunctionValue->getParent() != &Self->Bridge.module())
    return IRPluginBridge::makeStatus(
        NEVERC_STATUS_WRONG_SCOPE,
        "IR basic block function belongs to another module");
  if (Lease->Scope != NEVERC_IR_MUTATION_SCOPE_MODULE &&
      Lease->FunctionScope != FunctionValue)
    return IRPluginBridge::makeStatus(
        NEVERC_STATUS_WRONG_SCOPE,
        "IR basic block function is outside the mutation scope");
  StringRef NameValue;
  Status = IRPluginBridge::viewToString(Name, &NameValue);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  BasicBlock *Created =
      BasicBlock::Create(Self->Bridge.context(), NameValue, FunctionValue);
  auto Wrapped = Self->Bridge.wrapValue(*Created);
  if (!Wrapped) {
    Created->eraseFromParent();
    return IRPluginBridge::makeStatus(
        NEVERC_STATUS_RESOURCE_EXHAUSTED,
        "failed to allocate IR basic block handle");
  }
  Lease->CreatedValues.emplace_back(Created);
  *OutBlock = *Wrapped;
  return IRPluginBridge::successStatus();
}

} // namespace neverc::plugin
