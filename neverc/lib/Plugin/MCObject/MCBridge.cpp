#include "neverc/Plugin/Host/MCPluginBridge.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Error.h"
#include <cstddef>

using namespace llvm;

namespace neverc::plugin {
namespace {

NevercStatus mcStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

MCPluginBridge *bridge(void *Context, NevercTaskHandle Task,
                       NevercStatus &Status) {
  if (!Context) {
    Status = mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return nullptr;
  }
  auto *Bridge = static_cast<MCPluginBridge *>(Context);
  NevercTaskHandle Expected = Bridge->taskHandle();
  if (Expected.Owner != Task.Owner || Expected.Value != Task.Value) {
    Status = mcStatus(NEVERC_STATUS_WRONG_SCOPE);
    return nullptr;
  }
  Status = neverc_status_ok();
  return Bridge;
}

template <typename T> bool validOutput(const T *Output) {
  return Output && Output->Header.StructSize >= sizeof(T) &&
         Output->Header.Major == NEVERC_MC_API_MAJOR &&
         Output->Header.Minor <= NEVERC_MC_API_MINOR;
}

template <typename T>
NevercStatus writeHandle(Expected<T> Handle, T *Output) {
  if (!Output) {
    if (!Handle)
      consumeError(Handle.takeError());
    return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  }
  *Output = {};
  if (!Handle) {
    consumeError(Handle.takeError());
    return mcStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *Output = *Handle;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL BeginMutation(
    void *Context, NevercTaskHandle Task, NevercMCUnitHandle Unit,
    NevercMCMutationHandle *OutMutation) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  PluginMCUnit *Resolved = nullptr;
  Status = Bridge->resolveUnit(Unit, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return writeHandle(Bridge->beginMutation(), OutMutation);
}

NevercStatus NEVERC_CALL CommitMutation(
    void *Context, NevercTaskHandle Task,
    NevercMCMutationHandle Mutation) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  return Bridge ? Bridge->commitMutation(Mutation) : Status;
}

NevercStatus NEVERC_CALL AbandonMutation(
    void *Context, NevercTaskHandle Task,
    NevercMCMutationHandle Mutation) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  return Bridge ? Bridge->abandonMutation(Mutation) : Status;
}

NevercStatus NEVERC_CALL GetFirstInstruction(
    void *Context, NevercTaskHandle Task, NevercMCUnitHandle Unit,
    NevercMCInstHandle *OutInstruction) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge || !OutInstruction)
    return Bridge ? mcStatus(NEVERC_STATUS_INVALID_ARGUMENT) : Status;
  *OutInstruction = {};
  PluginMCUnit *Resolved = nullptr;
  Status = Bridge->resolveUnit(Unit, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Resolved->instructions().empty())
    return mcStatus(NEVERC_STATUS_NOT_FOUND);
  return writeHandle(
      Bridge->wrapInstruction(*Resolved->instructions().front()),
      OutInstruction);
}

NevercStatus NEVERC_CALL GetNextInstruction(
    void *Context, NevercTaskHandle Task,
    NevercMCInstHandle Instruction,
    NevercMCInstHandle *OutInstruction) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge || !OutInstruction)
    return Bridge ? mcStatus(NEVERC_STATUS_INVALID_ARGUMENT) : Status;
  *OutInstruction = {};
  MCInst *Resolved = nullptr;
  Status = Bridge->resolveInstruction(Instruction, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PluginMCUnit *Unit = nullptr;
  auto UnitHandle = Bridge->unit();
  if (!UnitHandle) {
    consumeError(UnitHandle.takeError());
    return mcStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  Status = Bridge->resolveUnit(*UnitHandle, &Unit);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  for (auto It = Unit->instructions().begin();
       It != Unit->instructions().end(); ++It) {
    if (It->get() != Resolved)
      continue;
    ++It;
    if (It == Unit->instructions().end())
      return mcStatus(NEVERC_STATUS_NOT_FOUND);
    return writeHandle(Bridge->wrapInstruction(**It),
                       OutInstruction);
  }
  for (auto &Section : Unit->sections()) {
    for (auto &Fragment : Section->Fragments) {
      for (auto It = Fragment->Instructions.begin();
           It != Fragment->Instructions.end(); ++It) {
        if (It->get() != Resolved)
          continue;
        ++It;
        if (It == Fragment->Instructions.end())
          return mcStatus(NEVERC_STATUS_NOT_FOUND);
        return writeHandle(Bridge->wrapInstruction(**It),
                           OutInstruction);
      }
    }
  }
  return mcStatus(NEVERC_STATUS_STALE_HANDLE);
}

NevercStatus NEVERC_CALL GetInstructionInfo(
    void *Context, NevercTaskHandle Task,
    NevercMCInstHandle Instruction, NevercMCInstructionInfo *OutInfo) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  if (!validOutput(OutInfo))
    return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  MCInst *Resolved = nullptr;
  Status = Bridge->resolveInstruction(Instruction, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto Opcode = Bridge->stableOpcode(Resolved->getOpcode());
  if (!Opcode) {
    consumeError(Opcode.takeError());
    return mcStatus(NEVERC_STATUS_NOT_FOUND);
  }
  auto SchemaToken = Bridge->schemaToken();
  if (!SchemaToken) {
    consumeError(SchemaToken.takeError());
    return mcStatus(NEVERC_STATUS_NOT_FOUND);
  }
  OutInfo->SchemaToken = *SchemaToken;
  OutInfo->Opcode = *Opcode;
  OutInfo->Flags = Resolved->getFlags();
  OutInfo->OperandCount = Resolved->getNumOperands();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL GetInstructionOperand(
    void *Context, NevercTaskHandle Task,
    NevercMCInstHandle Instruction, uint64_t Index,
    NevercMCOperandHandle *OutOperand) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  MCInst *Resolved = nullptr;
  Status = Bridge->resolveInstruction(Instruction, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Index >= Resolved->getNumOperands())
    return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return writeHandle(Bridge->wrapOperand(*Resolved, Index),
                     OutOperand);
}

NevercStatus NEVERC_CALL GetOperandValue(
    void *Context, NevercTaskHandle Task, NevercMCOperandHandle Operand,
    NevercMCOperandValue *OutValue) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  if (!validOutput(OutValue))
    return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  MCPluginBridge::OperandReference *Reference = nullptr;
  Status = Bridge->resolveOperand(Operand, &Reference);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Reference->Index >= Reference->Instruction->getNumOperands())
    return mcStatus(NEVERC_STATUS_STALE_HANDLE);
  const MCOperand &Value =
      Reference->Instruction->getOperand(Reference->Index);
  OutValue->Reserved = 0;
  OutValue->SchemaToken = {};
  if (Value.isReg()) {
    auto Register = Bridge->stableRegister(Value.getReg());
    if (!Register) {
      consumeError(Register.takeError());
      return mcStatus(NEVERC_STATUS_NOT_FOUND);
    }
    auto SchemaToken = Bridge->schemaToken();
    if (!SchemaToken) {
      consumeError(SchemaToken.takeError());
      return mcStatus(NEVERC_STATUS_NOT_FOUND);
    }
    OutValue->Kind = NEVERC_MC_OPERAND_REGISTER;
    OutValue->SchemaToken = *SchemaToken;
    OutValue->Payload.Register = *Register;
  } else if (Value.isImm()) {
    OutValue->Kind = NEVERC_MC_OPERAND_IMMEDIATE;
    OutValue->Payload.Immediate = Value.getImm();
  } else if (Value.isSFPImm()) {
    OutValue->Kind = NEVERC_MC_OPERAND_SINGLE_FLOAT;
    OutValue->Payload.SingleFloatBits = Value.getSFPImm();
  } else if (Value.isDFPImm()) {
    OutValue->Kind = NEVERC_MC_OPERAND_DOUBLE_FLOAT;
    OutValue->Payload.DoubleFloatBits = Value.getDFPImm();
  } else if (Value.isExpr()) {
    return mcStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  } else if (Value.isInst()) {
    OutValue->Kind = NEVERC_MC_OPERAND_INSTRUCTION;
    auto Instruction =
        Bridge->wrapInstruction(*const_cast<MCInst *>(Value.getInst()));
    if (!Instruction) {
      consumeError(Instruction.takeError());
      return mcStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    OutValue->Payload.Instruction = *Instruction;
  } else {
    OutValue->Kind = NEVERC_MC_OPERAND_INVALID;
    return mcStatus(NEVERC_STATUS_NOT_FOUND);
  }
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL CreateInstruction(
    void *Context, NevercTaskHandle Task,
    NevercMCMutationHandle Mutation,
    NevercMCSchemaTokenHandle SchemaToken, uint32_t Opcode,
    NevercMCInstHandle *OutInstruction) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  Status = Bridge->checkSchemaToken(SchemaToken);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return writeHandle(
      Bridge->createInstruction(Mutation, SchemaToken, Opcode),
      OutInstruction);
}

NevercStatus NEVERC_CALL AppendOperand(
    void *Context, NevercTaskHandle Task,
    NevercMCMutationHandle Mutation, NevercMCInstHandle Instruction,
    const NevercMCOperandValue *Value) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  if (!Value || Value->Header.StructSize < sizeof(*Value) ||
      Value->Header.Major != NEVERC_MC_API_MAJOR ||
      Value->Header.Minor > NEVERC_MC_API_MINOR)
    return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return Bridge->appendOperand(Mutation, Instruction, *Value);
}

NevercStatus NEVERC_CALL InsertInstructionBefore(
    void *Context, NevercTaskHandle Task,
    NevercMCMutationHandle Mutation, NevercMCInstHandle Position,
    NevercMCInstHandle Instruction) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  return Bridge ? Bridge->insertInstructionBefore(
                      Mutation, Position, Instruction)
                : Status;
}

NevercStatus NEVERC_CALL AppendInstruction(
    void *Context, NevercTaskHandle Task,
    NevercMCMutationHandle Mutation, NevercMCUnitHandle Unit,
    NevercMCInstHandle Instruction) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  return Bridge ? Bridge->appendInstruction(Mutation, Unit,
                                             Instruction)
                : Status;
}

NevercStatus NEVERC_CALL ReplaceInstruction(
    void *Context, NevercTaskHandle Task,
    NevercMCMutationHandle Mutation, NevercMCInstHandle Instruction,
    NevercMCInstHandle Replacement) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  return Bridge ? Bridge->replaceInstruction(
                      Mutation, Instruction, Replacement)
                : Status;
}

NevercStatus NEVERC_CALL EraseInstruction(
    void *Context, NevercTaskHandle Task,
    NevercMCMutationHandle Mutation, NevercMCInstHandle Instruction) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  return Bridge ? Bridge->eraseInstruction(Mutation, Instruction)
                : Status;
}

} // namespace

MCPluginBridge::MCPluginBridge(
    PluginTaskContext &TaskValue, PluginMCUnit &UnitValue,
    const PluginTargetSnapshot::NamedRecord *SchemaValue,
    bool AllowMutationValue)
    : Task(TaskValue), Unit(UnitValue), Schema(SchemaValue),
      MutationAllowed(AllowMutationValue) {
  if (Schema) {
    Unit.setTargetIdentity(Schema->TargetID, Schema->Digest);
    for (const auto &Value : Schema->Opcodes) {
      StableToBackendOpcodes[Value.StableID] = Value.BackendValue;
      BackendToStableOpcodes[Value.BackendValue] = Value.StableID;
    }
    for (const auto &Value : Schema->SchemaRegisters) {
      StableToBackendRegisters[Value.StableID] = Value.BackendValue;
      BackendToStableRegisters[Value.BackendValue] = Value.StableID;
    }
  }
  API.Header = {sizeof(API), NEVERC_MC_API_MAJOR,
                NEVERC_MC_API_MINOR, 0};
  initializeMCSchemaAPI(API, *this);
  initializeMCBuilderAPI(API, *this);
  initializeMCExpressionAPI(API, *this);
  API.RegisterSchema = nullptr;
  API.RegisterEncoder = nullptr;
  API.RegisterDecoder = nullptr;
  API.RegisterAsmBackend = nullptr;
  API.BeginMutation = BeginMutation;
  API.CommitMutation = CommitMutation;
  API.AbandonMutation = AbandonMutation;
  API.GetFirstInstruction = GetFirstInstruction;
  API.GetNextInstruction = GetNextInstruction;
  API.GetInstructionInfo = GetInstructionInfo;
  API.GetInstructionOperand = GetInstructionOperand;
  API.GetOperandValue = GetOperandValue;
  API.CreateInstruction = CreateInstruction;
  API.AppendOperand = AppendOperand;
  API.InsertInstructionBefore = InsertInstructionBefore;
  API.AppendInstruction = AppendInstruction;
  API.ReplaceInstruction = ReplaceInstruction;
  API.EraseInstruction = EraseInstruction;
}

MCPluginBridge::~MCPluginBridge() {
  if (!neverc_handle_is_null(MutationHandle)) {
    rollbackMutation();
    (void)Task.handles().release(MutationHandle,
                                 PluginMCMutationHandleKind);
  }
  advanceUnitGeneration();
}

Expected<uint32_t>
MCPluginBridge::stableOpcode(uint32_t Backend) const {
  if (BackendToStableOpcodes.empty())
    return Backend;
  auto It = BackendToStableOpcodes.find(Backend);
  if (It == BackendToStableOpcodes.end())
    return createStringError(inconvertibleErrorCode(),
                             "unknown MC backend opcode");
  return It->second;
}

Expected<uint32_t>
MCPluginBridge::backendOpcode(uint32_t Stable) const {
  if (StableToBackendOpcodes.empty())
    return Stable;
  auto It = StableToBackendOpcodes.find(Stable);
  if (It == StableToBackendOpcodes.end())
    return createStringError(inconvertibleErrorCode(),
                             "unknown stable MC opcode");
  return It->second;
}

Expected<uint32_t>
MCPluginBridge::stableRegister(uint32_t Backend) const {
  if (BackendToStableRegisters.empty())
    return Backend;
  auto It = BackendToStableRegisters.find(Backend);
  if (It == BackendToStableRegisters.end())
    return createStringError(inconvertibleErrorCode(),
                             "unknown MC backend register");
  return It->second;
}

Expected<uint32_t>
MCPluginBridge::backendRegister(uint32_t Stable) const {
  if (StableToBackendRegisters.empty())
    return Stable;
  auto It = StableToBackendRegisters.find(Stable);
  if (It == StableToBackendRegisters.end())
    return createStringError(inconvertibleErrorCode(),
                             "unknown stable MC register");
  return It->second;
}

bool MCPluginBridge::containsInstruction(
    const MCInst *Instruction) const {
  const auto Contains = [Instruction](const auto &Values) {
    return llvm::any_of(Values, [Instruction](const auto &Value) {
      return Value.get() == Instruction;
    });
  };
  return Unit.contains(Instruction) || Contains(Detached);
}

Expected<NevercMCInstHandle> MCPluginBridge::createInstruction(
    NevercMCMutationHandle Mutation,
    NevercMCSchemaTokenHandle SchemaToken, uint32_t Opcode) {
  NevercStatus Status = checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return createStringError(inconvertibleErrorCode(),
                             "invalid MC mutation handle");
  Status = checkSchemaToken(SchemaToken);
  if (Status.Code != NEVERC_STATUS_OK)
    return createStringError(inconvertibleErrorCode(),
                             "invalid MC target schema token");
  auto Backend = backendOpcode(Opcode);
  if (!Backend)
    return Backend.takeError();
  auto Instruction = std::make_unique<MCInst>();
  Instruction->setOpcode(*Backend);
  MCInst *Raw = Instruction.get();
  Detached.push_back(std::move(Instruction));
  Created.push_back(Raw);
  return wrapInstruction(*Raw);
}

NevercStatus MCPluginBridge::appendOperand(
    NevercMCMutationHandle Mutation, NevercMCInstHandle Instruction,
    const NevercMCOperandValue &Value) {
  NevercStatus Status = checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Value.Reserved != 0)
    return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  MCInst *Resolved = nullptr;
  Status = resolveInstruction(Instruction, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  MCOperand Operand;
  switch (Value.Kind) {
  case NEVERC_MC_OPERAND_REGISTER: {
    Status = checkSchemaToken(Value.SchemaToken);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    auto Register = backendRegister(Value.Payload.Register);
    if (!Register) {
      consumeError(Register.takeError());
      return mcStatus(NEVERC_STATUS_NOT_FOUND);
    }
    Operand = MCOperand::createReg(*Register);
    break;
  }
  case NEVERC_MC_OPERAND_IMMEDIATE:
    if (!neverc_handle_is_null(Value.SchemaToken))
      return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    Operand = MCOperand::createImm(Value.Payload.Immediate);
    break;
  case NEVERC_MC_OPERAND_SINGLE_FLOAT:
    if (!neverc_handle_is_null(Value.SchemaToken))
      return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    Operand = MCOperand::createSFPImm(Value.Payload.SingleFloatBits);
    break;
  case NEVERC_MC_OPERAND_DOUBLE_FLOAT:
    if (!neverc_handle_is_null(Value.SchemaToken))
      return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    Operand = MCOperand::createDFPImm(Value.Payload.DoubleFloatBits);
    break;
  case NEVERC_MC_OPERAND_EXPRESSION: {
    PluginMCExpression *Expression = nullptr;
    Status = resolveExpression(Value.Payload.Expression, &Expression);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (Expression->Kind == NEVERC_MC_EXPRESSION_TARGET_VARIANT) {
      Status = checkSchemaToken(Value.SchemaToken);
      if (Status.Code != NEVERC_STATUS_OK)
        return Status;
    } else if (!neverc_handle_is_null(Value.SchemaToken)) {
      return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    }
    return mcStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  }
  case NEVERC_MC_OPERAND_INSTRUCTION: {
    if (!neverc_handle_is_null(Value.SchemaToken))
      return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    MCInst *Nested = nullptr;
    Status =
        resolveInstruction(Value.Payload.Instruction, &Nested);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    Operand = MCOperand::createInst(Nested);
    break;
  }
  default:
    return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  }
  const unsigned PreviousCount = Resolved->getNumOperands();
  Resolved->addOperand(Operand);
  UndoActions.push_back([Resolved, PreviousCount] {
    if (Resolved->getNumOperands() > PreviousCount)
      Resolved->erase(Resolved->begin() + PreviousCount);
  });
  return neverc_status_ok();
}

NevercStatus MCPluginBridge::insertInstructionBefore(
    NevercMCMutationHandle Mutation, NevercMCInstHandle Position,
    NevercMCInstHandle Instruction) {
  NevercStatus Status = checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  MCInst *PositionValue = nullptr;
  MCInst *InstructionValue = nullptr;
  Status = resolveInstruction(Position, &PositionValue);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = resolveInstruction(Instruction, &InstructionValue);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PluginMCUnit::InstructionStorage *Storage = nullptr;
  if (Unit.contains(PositionValue)) {
    auto It = llvm::find_if(
        Unit.instructions(), [PositionValue](const auto &Value) {
          return Value.get() == PositionValue;
        });
    if (It != Unit.instructions().end())
      Storage = &Unit.instructions();
    if (!Storage)
      for (auto &Section : Unit.sections())
        for (auto &Fragment : Section->Fragments)
          if (llvm::any_of(
                  Fragment->Instructions,
                  [PositionValue](const auto &Value) {
                    return Value.get() == PositionValue;
                  }))
            Storage = &Fragment->Instructions;
  }
  auto InstructionIt =
      llvm::find_if(Detached, [InstructionValue](const auto &Value) {
        return Value.get() == InstructionValue;
      });
  if (!Storage || InstructionIt == Detached.end())
    return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto PositionIt =
      llvm::find_if(*Storage, [PositionValue](const auto &Value) {
        return Value.get() == PositionValue;
      });
  Storage->splice(PositionIt, Detached, InstructionIt);
  UndoActions.push_back([this, Storage, InstructionValue] {
    auto It = llvm::find_if(
        *Storage, [InstructionValue](const auto &Value) {
          return Value.get() == InstructionValue;
        });
    if (It != Storage->end())
      Detached.splice(Detached.end(), *Storage, It);
  });
  return neverc_status_ok();
}

NevercStatus MCPluginBridge::appendInstruction(
    NevercMCMutationHandle Mutation, NevercMCUnitHandle UnitValue,
    NevercMCInstHandle Instruction) {
  NevercStatus Status = checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PluginMCUnit *ResolvedUnit = nullptr;
  Status = resolveUnit(UnitValue, &ResolvedUnit);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  MCInst *InstructionValue = nullptr;
  Status = resolveInstruction(Instruction, &InstructionValue);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto It = llvm::find_if(
      Detached, [InstructionValue](const auto &Value) {
        return Value.get() == InstructionValue;
      });
  if (It == Detached.end())
    return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  ResolvedUnit->instructions().splice(
      ResolvedUnit->instructions().end(), Detached, It);
  UndoActions.push_back([this, InstructionValue] {
    auto Current = llvm::find_if(
        Unit.instructions(), [InstructionValue](const auto &Value) {
          return Value.get() == InstructionValue;
        });
    if (Current != Unit.instructions().end())
      Detached.splice(Detached.end(), Unit.instructions(), Current);
  });
  return neverc_status_ok();
}

NevercStatus MCPluginBridge::replaceInstruction(
    NevercMCMutationHandle Mutation, NevercMCInstHandle Instruction,
    NevercMCInstHandle Replacement) {
  NevercStatus Status = checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  MCInst *Old = nullptr;
  MCInst *New = nullptr;
  Status = resolveInstruction(Instruction, &Old);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = resolveInstruction(Replacement, &New);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PluginMCUnit::InstructionStorage *Storage = nullptr;
  if (llvm::any_of(Unit.instructions(), [Old](const auto &Value) {
        return Value.get() == Old;
      }))
    Storage = &Unit.instructions();
  if (!Storage)
    for (auto &Section : Unit.sections())
      for (auto &Fragment : Section->Fragments)
        if (llvm::any_of(Fragment->Instructions,
                         [Old](const auto &Value) {
                           return Value.get() == Old;
                         }))
          Storage = &Fragment->Instructions;
  auto NewIt = llvm::find_if(Detached, [New](const auto &Value) {
    return Value.get() == New;
  });
  if (!Storage || NewIt == Detached.end())
    return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto OldIt = llvm::find_if(*Storage, [Old](const auto &Value) {
    return Value.get() == Old;
  });
  auto Next = std::next(OldIt);
  MCInst *NextValue =
      Next == Storage->end() ? nullptr : Next->get();
  Removed.splice(Removed.end(), *Storage, OldIt);
  auto InsertAt = NextValue
                      ? llvm::find_if(*Storage,
                                      [NextValue](const auto &Value) {
                                        return Value.get() == NextValue;
                                      })
                      : Storage->end();
  Storage->splice(InsertAt, Detached, NewIt);
  UndoActions.push_back([this, Storage, Old, New, NextValue] {
    auto NewIt = llvm::find_if(*Storage, [New](const auto &Value) {
      return Value.get() == New;
    });
    if (NewIt != Storage->end())
      Detached.splice(Detached.end(), *Storage, NewIt);
    auto OldIt = llvm::find_if(Removed, [Old](const auto &Value) {
      return Value.get() == Old;
    });
    if (OldIt == Removed.end())
      return;
    auto Position =
        NextValue
            ? llvm::find_if(*Storage, [NextValue](const auto &Value) {
                    return Value.get() == NextValue;
                  })
            : Storage->end();
    Storage->splice(Position, Removed, OldIt);
  });
  invalidateInstruction(Old);
  return neverc_status_ok();
}

NevercStatus MCPluginBridge::eraseInstruction(
    NevercMCMutationHandle Mutation, NevercMCInstHandle Instruction) {
  NevercStatus Status = checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  MCInst *Value = nullptr;
  Status = resolveInstruction(Instruction, &Value);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PluginMCUnit::InstructionStorage *Storage = nullptr;
  if (llvm::any_of(Unit.instructions(), [Value](const auto &Entry) {
        return Entry.get() == Value;
      }))
    Storage = &Unit.instructions();
  if (!Storage)
    for (auto &Section : Unit.sections())
      for (auto &Fragment : Section->Fragments)
        if (llvm::any_of(Fragment->Instructions,
                         [Value](const auto &Entry) {
                           return Entry.get() == Value;
                         }))
          Storage = &Fragment->Instructions;
  if (!Storage)
    return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto It = llvm::find_if(*Storage, [Value](const auto &Entry) {
    return Entry.get() == Value;
  });
  auto Next = std::next(It);
  MCInst *NextValue =
      Next == Storage->end() ? nullptr : Next->get();
  Removed.splice(Removed.end(), *Storage, It);
  UndoActions.push_back([this, Storage, Value, NextValue] {
    auto RemovedIt = llvm::find_if(Removed, [Value](const auto &Entry) {
      return Entry.get() == Value;
    });
    if (RemovedIt == Removed.end())
      return;
    auto Position =
        NextValue
            ? llvm::find_if(*Storage, [NextValue](const auto &Entry) {
                    return Entry.get() == NextValue;
                  })
            : Storage->end();
    Storage->splice(Position, Removed, RemovedIt);
  });
  invalidateInstruction(Value);
  return neverc_status_ok();
}

} // namespace neverc::plugin
