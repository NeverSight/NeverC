#include "PluginFrontendFuzzSupport.h"
#include "neverc/Plugin/Host/MIRPluginBridge.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Host.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>

using namespace llvm;
using namespace neverc;
using namespace neverc::fuzz;
using namespace neverc::plugin;

namespace {

void consume(Error E) {
  if (E)
    consumeError(std::move(E));
}

NevercMIRInstructionOpcode opcode(ByteCursor &Input) {
  NevercMIRInstructionOpcode Opcode{};
  Opcode.StableOpcode = Input.takeU32();
  Opcode.TargetOpcode = Input.takeU32();
  Opcode.RequiresTargetSchema =
      (Input.takeByte() & 1U) ? NEVERC_TRUE : NEVERC_FALSE;
  return Opcode;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  static const bool Initialized = [] {
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    return true;
  }();
  (void)Initialized;
  ByteCursor Input(Data, Size);

  auto CreatedTask =
      pluginFuzzRuntime().session().createTask(NEVERC_TASK_CODEGEN);
  if (!CreatedTask) {
    consumeError(CreatedTask.takeError());
    return 0;
  }
  std::unique_ptr<PluginTaskContext> Task = std::move(*CreatedTask);

  std::string Error;
  std::string TripleName = sys::getDefaultTargetTriple();
  const Target *Target = TargetRegistry::lookupTarget(TripleName, Error);
  if (!Target) {
    consume(Task->end());
    return 0;
  }
  TargetOptions Options;
  std::unique_ptr<TargetMachine> TargetMachineOwner(
      Target->createTargetMachine(TripleName, "generic", "", Options,
                                  std::nullopt));
  if (!TargetMachineOwner) {
    consume(Task->end());
    return 0;
  }
  auto *LLVMTarget = static_cast<LLVMTargetMachine *>(TargetMachineOwner.get());
  LLVMContext Context;
  Module IRModule("mir-builder-fuzz", Context);
  IRModule.setTargetTriple(TripleName);
  IRModule.setDataLayout(LLVMTarget->createDataLayout());
  Function *IRFunction =
      Function::Create(FunctionType::get(Type::getVoidTy(Context), false),
                       GlobalValue::ExternalLinkage, "fuzz", IRModule);
  BasicBlock *IRBlock = BasicBlock::Create(Context, "entry", IRFunction);
  ReturnInst::Create(Context, IRBlock);
  MachineModuleInfo MMI(LLVMTarget);
  MachineFunction &MF = MMI.getOrCreateMachineFunction(*IRFunction);
  MachineBasicBlock *First = MF.CreateMachineBasicBlock(IRBlock);
  MachineBasicBlock *Second = MF.CreateMachineBasicBlock();
  MF.push_back(First);
  MF.push_back(Second);

  auto Bridge = std::make_unique<MIRPluginBridge>(*Task, MF);
  const NevercMIRAPI &API = Bridge->api();
  NevercTaskHandle TaskHandle = Task->handle();
  auto FunctionHandle = Bridge->machineFunction();
  auto FirstHandle = Bridge->wrapBasicBlock(*First);
  auto SecondHandle = Bridge->wrapBasicBlock(*Second);
  if (!FunctionHandle || !FirstHandle || !SecondHandle) {
    Bridge.reset();
    consume(Task->end());
    return 0;
  }

  NevercMIRMutationHandle Mutation{};
  if (API.BeginMutation(API.Context, TaskHandle, *FunctionHandle, &Mutation)
          .Code != NEVERC_STATUS_OK) {
    Bridge.reset();
    consume(Task->end());
    return 0;
  }

  NevercMachineBasicBlockHandle CreatedBlock{};
  NevercMachineInstrHandle CreatedInstruction{};
  NevercMachineOperandHandle CreatedOperand{};
  unsigned OperationCount = std::min<unsigned>(Input.takeByte(), 32U);
  for (unsigned Index = 0; Index != OperationCount; ++Index) {
    NevercTaskHandle OperationTask = chooseTaskHandle(Input, TaskHandle);
    switch (Input.takeByte() % 8U) {
    case 0:
      (void)API.CreateBasicBlock(
          API.Context, OperationTask, Mutation,
          (Input.takeByte() & 1U) ? *SecondHandle : arbitraryHandle(Input),
          &CreatedBlock);
      break;
    case 1:
      (void)API.CreateInstruction(
          API.Context, OperationTask, Mutation,
          (Input.takeByte() & 1U) ? *FirstHandle : arbitraryHandle(Input), {},
          opcode(Input), &CreatedInstruction);
      break;
    case 2: {
      NevercMIROperandValue Value{};
      Value.Header = {std::min<uint32_t>(sizeof(Value), Input.takeU32()),
                      NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR, 0};
      Value.Kind = Input.takeU32();
      Value.TargetFlags = Input.takeU32();
      Value.Payload.Immediate = static_cast<int64_t>(Input.takeU64());
      (void)API.AppendOperand(
          API.Context, OperationTask, Mutation,
          (Input.takeByte() & 1U) ? CreatedInstruction
                                  : arbitraryHandle(Input),
          &Value, &CreatedOperand);
      break;
    }
    case 3:
      (void)API.SetInstructionFlags(
          API.Context, OperationTask, Mutation,
          (Input.takeByte() & 1U) ? CreatedInstruction
                                  : arbitraryHandle(Input),
          Input.takeU64());
      break;
    case 4:
      (void)API.AddCFGEdge(
          API.Context, OperationTask, Mutation,
          (Input.takeByte() & 1U) ? *FirstHandle : arbitraryHandle(Input),
          (Input.takeByte() & 1U) ? *SecondHandle : arbitraryHandle(Input),
          Input.takeU32(), Input.takeU32());
      break;
    case 5:
      (void)API.MoveBasicBlock(
          API.Context, OperationTask, Mutation,
          (Input.takeByte() & 1U) ? CreatedBlock : arbitraryHandle(Input),
          (Input.takeByte() & 1U) ? *SecondHandle : arbitraryHandle(Input));
      break;
    case 6:
      (void)API.MoveInstruction(
          API.Context, OperationTask, Mutation,
          (Input.takeByte() & 1U) ? CreatedInstruction
                                  : arbitraryHandle(Input),
          (Input.takeByte() & 1U) ? *SecondHandle : arbitraryHandle(Input), {});
      break;
    default: {
      int32_t FrameIndex = 0;
      (void)API.CreateStackObject(API.Context, OperationTask, Mutation,
                                  Input.takeU64(), Input.takeU64(),
                                  (Input.takeByte() & 1U) ? NEVERC_TRUE
                                                          : NEVERC_FALSE,
                                  Input.takeU32(), &FrameIndex);
      break;
    }
    }
  }

  bool Commit = (Input.takeByte() & 1U) != 0;
  NevercStatus Final =
      Commit ? API.CommitMutation(API.Context, TaskHandle, Mutation)
             : API.AbortMutation(API.Context, TaskHandle, Mutation);
  if (Final.Code == NEVERC_STATUS_OK && !Commit) {
    if (!neverc_handle_is_null(CreatedBlock)) {
      int64_t Number = 0;
      if (API.GetBasicBlockNumber(API.Context, TaskHandle, CreatedBlock,
                                  &Number)
              .Code != NEVERC_STATUS_STALE_HANDLE)
        std::abort();
    }
    if (!neverc_handle_is_null(CreatedInstruction)) {
      NevercMIRInstructionInfo Info{};
      Info.Header = {sizeof(Info), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR,
                     0};
      if (API.GetInstructionInfo(API.Context, TaskHandle, CreatedInstruction,
                                 &Info)
              .Code != NEVERC_STATUS_STALE_HANDLE)
        std::abort();
    }
    if (!neverc_handle_is_null(CreatedOperand)) {
      NevercMIROperandValue Value{};
      Value.Header = {sizeof(Value), NEVERC_MIR_API_MAJOR,
                      NEVERC_MIR_API_MINOR, 0};
      if (API.GetOperandValue(API.Context, TaskHandle, CreatedOperand, &Value)
              .Code != NEVERC_STATUS_STALE_HANDLE)
        std::abort();
    }
  }
  (void)API.EndMutation(API.Context, TaskHandle, Mutation);
  Bridge.reset();
  consume(Task->end());
  return 0;
}
