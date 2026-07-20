#include "neverc/Plugin/Host/MCPluginBridge.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "llvm/Support/Error.h"
#include <limits>
#include <new>

using namespace llvm;

namespace neverc::plugin {
namespace {

NevercStatus mcStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

bool sameHandle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

template <typename Value>
Expected<NevercHandle> wrapTracked(
    PluginTaskContext &Task, PluginHandleKind Kind, Value &Object,
    std::vector<std::pair<NevercHandle, Value *>> &Handles) {
  for (const auto &Entry : Handles)
    if (Entry.second == &Object)
      return Entry.first;
  auto Handle = Task.handles().create(Kind, &Object);
  if (!Handle)
    return Handle.takeError();
  Handles.push_back({*Handle, &Object});
  return *Handle;
}

template <typename Value, typename Contains>
NevercStatus resolveTracked(
    const PluginTaskContext &Task, PluginHandleKind Kind,
    NevercHandle Handle,
    const std::vector<std::pair<NevercHandle, Value *>> &Handles,
    Contains ContainsValue, Value **OutValue) {
  if (!OutValue)
    return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutValue = nullptr;
  void *Payload = nullptr;
  NevercStatus Status = Task.handles().resolve(Handle, Kind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *ValuePointer = static_cast<Value *>(Payload);
  if (!ContainsValue(ValuePointer))
    return mcStatus(NEVERC_STATUS_STALE_HANDLE);
  for (const auto &Entry : Handles)
    if (Entry.second == ValuePointer &&
        sameHandle(Entry.first, Handle)) {
      *OutValue = ValuePointer;
      return neverc_status_ok();
    }
  return mcStatus(NEVERC_STATUS_WRONG_SCOPE);
}

template <typename Value>
void invalidateTracked(
    PluginTaskContext &Task, PluginHandleKind Kind, Value *Object,
    std::vector<std::pair<NevercHandle, Value *>> &Handles) {
  for (auto It = Handles.begin(); It != Handles.end(); ++It) {
    if (It->second != Object)
      continue;
    (void)Task.handles().release(It->first, Kind);
    Handles.erase(It);
    break;
  }
}

template <typename Value>
void releaseTracked(
    PluginTaskContext &Task, PluginHandleKind Kind,
    std::vector<std::pair<NevercHandle, Value *>> &Handles) {
  for (const auto &Entry : Handles)
    (void)Task.handles().release(Entry.first, Kind);
  Handles.clear();
}

} // namespace

Expected<NevercMCUnitHandle> MCPluginBridge::unit() {
  if (!neverc_handle_is_null(UnitHandle))
    return UnitHandle;
  auto Handle = Task.handles().create(PluginMCUnitHandleKind, &Unit);
  if (!Handle)
    return Handle.takeError();
  UnitHandle = *Handle;
  return UnitHandle;
}

Expected<NevercMCSchemaTokenHandle> MCPluginBridge::schemaToken() {
  if (!Schema)
    return createStringError(inconvertibleErrorCode(),
                             "MC target schema is unavailable");
  if (!neverc_handle_is_null(SchemaTokenHandle))
    return SchemaTokenHandle;
  auto Handle =
      Task.handles().create(PluginMCSchemaTokenHandleKind, this);
  if (!Handle)
    return Handle.takeError();
  SchemaTokenHandle = *Handle;
  return SchemaTokenHandle;
}

NevercStatus MCPluginBridge::checkSchemaToken(
    NevercMCSchemaTokenHandle Token) const {
  void *Payload = nullptr;
  NevercStatus Status = Task.handles().resolve(
      Token, PluginMCSchemaTokenHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Payload != this || !sameHandle(Token, SchemaTokenHandle))
    return mcStatus(NEVERC_STATUS_WRONG_SCOPE);
  return neverc_status_ok();
}

Expected<NevercMCInstHandle>
MCPluginBridge::wrapInstruction(MCInst &Instruction) {
  for (const auto &Entry : InstructionHandles)
    if (Entry.second == &Instruction)
      return Entry.first;
  auto Handle = Task.handles().create(PluginMCInstructionHandleKind,
                                      &Instruction);
  if (!Handle)
    return Handle.takeError();
  InstructionHandles.push_back({*Handle, &Instruction});
  return *Handle;
}

Expected<NevercMCOperandHandle>
MCPluginBridge::wrapOperand(MCInst &Instruction, uint64_t Index) {
  auto *Reference =
      new (std::nothrow) OperandReference{&Instruction, Index};
  if (!Reference)
    return createStringError(inconvertibleErrorCode(),
                             "failed to allocate MC operand reference");
  auto Handle = Task.handles().create(
      PluginMCOperandHandleKind, Reference,
      [](void *Value) { delete static_cast<OperandReference *>(Value); });
  if (!Handle) {
    delete Reference;
    return Handle.takeError();
  }
  BorrowedOperandHandles.push_back(*Handle);
  return *Handle;
}

Expected<NevercMCExprHandle>
MCPluginBridge::wrapExpression(PluginMCExpression &Expression) {
  return wrapTracked(Task, PluginMCExpressionHandleKind, Expression,
                     ExpressionHandles);
}

Expected<NevercMCSectionHandle>
MCPluginBridge::wrapSection(PluginMCSection &Section) {
  return wrapTracked(Task, PluginMCSectionHandleKind, Section,
                     SectionHandles);
}

Expected<NevercMCSymbolHandle>
MCPluginBridge::wrapSymbol(PluginMCSymbol &Symbol) {
  return wrapTracked(Task, PluginMCSymbolHandleKind, Symbol,
                     SymbolHandles);
}

Expected<NevercMCFragmentHandle>
MCPluginBridge::wrapFragment(PluginMCFragment &Fragment) {
  return wrapTracked(Task, PluginMCFragmentHandleKind, Fragment,
                     FragmentHandles);
}

Expected<NevercMCFixupHandle>
MCPluginBridge::wrapFixup(PluginMCFixup &Fixup) {
  return wrapTracked(Task, PluginMCFixupHandleKind, Fixup,
                     FixupHandles);
}

NevercStatus MCPluginBridge::resolveUnit(
    NevercMCUnitHandle Handle, PluginMCUnit **OutUnit) const {
  if (!OutUnit)
    return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutUnit = nullptr;
  void *Payload = nullptr;
  NevercStatus Status =
      Task.handles().resolve(Handle, PluginMCUnitHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Payload != &Unit || !sameHandle(Handle, UnitHandle))
    return mcStatus(NEVERC_STATUS_WRONG_SCOPE);
  *OutUnit = &Unit;
  return neverc_status_ok();
}

NevercStatus MCPluginBridge::resolveInstruction(
    NevercMCInstHandle Handle, MCInst **OutInstruction) const {
  if (!OutInstruction)
    return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutInstruction = nullptr;
  void *Payload = nullptr;
  NevercStatus Status = Task.handles().resolve(
      Handle, PluginMCInstructionHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Instruction = static_cast<MCInst *>(Payload);
  if (!containsInstruction(Instruction))
    return mcStatus(NEVERC_STATUS_STALE_HANDLE);
  bool Known = false;
  for (const auto &Entry : InstructionHandles)
    if (Entry.second == Instruction && sameHandle(Entry.first, Handle)) {
      Known = true;
      break;
    }
  if (!Known)
    return mcStatus(NEVERC_STATUS_WRONG_SCOPE);
  *OutInstruction = Instruction;
  return neverc_status_ok();
}

NevercStatus MCPluginBridge::resolveOperand(
    NevercMCOperandHandle Handle, OperandReference **OutOperand) const {
  if (!OutOperand)
    return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutOperand = nullptr;
  void *Payload = nullptr;
  NevercStatus Status = Task.handles().resolve(
      Handle, PluginMCOperandHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Reference = static_cast<OperandReference *>(Payload);
  if (!containsInstruction(Reference->Instruction))
    return mcStatus(NEVERC_STATUS_STALE_HANDLE);
  *OutOperand = Reference;
  return neverc_status_ok();
}

NevercStatus MCPluginBridge::resolveExpression(
    NevercMCExprHandle Handle,
    PluginMCExpression **OutExpression) const {
  return resolveTracked(
      Task, PluginMCExpressionHandleKind, Handle, ExpressionHandles,
      [this](const PluginMCExpression *Expression) {
        return Unit.contains(Expression);
      },
      OutExpression);
}

NevercStatus MCPluginBridge::resolveSection(
    NevercMCSectionHandle Handle, PluginMCSection **OutSection) const {
  return resolveTracked(
      Task, PluginMCSectionHandleKind, Handle, SectionHandles,
      [this](const PluginMCSection *Section) {
        return Unit.contains(Section);
      },
      OutSection);
}

NevercStatus MCPluginBridge::resolveSymbol(
    NevercMCSymbolHandle Handle, PluginMCSymbol **OutSymbol) const {
  return resolveTracked(
      Task, PluginMCSymbolHandleKind, Handle, SymbolHandles,
      [this](const PluginMCSymbol *Symbol) {
        return Unit.contains(Symbol);
      },
      OutSymbol);
}

NevercStatus MCPluginBridge::resolveFragment(
    NevercMCFragmentHandle Handle,
    PluginMCFragment **OutFragment) const {
  return resolveTracked(
      Task, PluginMCFragmentHandleKind, Handle, FragmentHandles,
      [this](const PluginMCFragment *Fragment) {
        return Unit.contains(Fragment);
      },
      OutFragment);
}

NevercStatus MCPluginBridge::resolveFixup(
    NevercMCFixupHandle Handle, PluginMCFixup **OutFixup) const {
  return resolveTracked(
      Task, PluginMCFixupHandleKind, Handle, FixupHandles,
      [this](const PluginMCFixup *Fixup) {
        return Unit.contains(Fixup);
      },
      OutFixup);
}

void MCPluginBridge::finishBorrowedHandles() {
  for (NevercMCOperandHandle Handle : BorrowedOperandHandles)
    (void)Task.handles().release(Handle, PluginMCOperandHandleKind);
  BorrowedOperandHandles.clear();
}

void MCPluginBridge::invalidateInstruction(MCInst *Instruction) {
  for (auto It = BorrowedOperandHandles.begin();
       It != BorrowedOperandHandles.end();) {
    void *Payload = nullptr;
    NevercStatus Status = Task.handles().resolve(
        *It, PluginMCOperandHandleKind, &Payload);
    auto *Reference =
        Status.Code == NEVERC_STATUS_OK
            ? static_cast<OperandReference *>(Payload)
            : nullptr;
    if (Reference && Reference->Instruction == Instruction) {
      (void)Task.handles().release(*It, PluginMCOperandHandleKind);
      It = BorrowedOperandHandles.erase(It);
    } else {
      ++It;
    }
  }
  for (auto It = InstructionHandles.begin();
       It != InstructionHandles.end(); ++It) {
    if (It->second != Instruction)
      continue;
    (void)Task.handles().release(It->first,
                                 PluginMCInstructionHandleKind);
    InstructionHandles.erase(It);
    break;
  }
}

void MCPluginBridge::invalidateSection(PluginMCSection *Section) {
  invalidateTracked(Task, PluginMCSectionHandleKind, Section,
                    SectionHandles);
}

void MCPluginBridge::invalidateSymbol(PluginMCSymbol *Symbol) {
  invalidateTracked(Task, PluginMCSymbolHandleKind, Symbol,
                    SymbolHandles);
}

void MCPluginBridge::invalidateExpression(
    PluginMCExpression *Expression) {
  invalidateTracked(Task, PluginMCExpressionHandleKind, Expression,
                    ExpressionHandles);
}

void MCPluginBridge::invalidateFragment(PluginMCFragment *Fragment) {
  invalidateTracked(Task, PluginMCFragmentHandleKind, Fragment,
                    FragmentHandles);
}

void MCPluginBridge::invalidateFixup(PluginMCFixup *Fixup) {
  invalidateTracked(Task, PluginMCFixupHandleKind, Fixup,
                    FixupHandles);
}

void MCPluginBridge::advanceUnitGeneration() {
  finishBorrowedHandles();
  releaseTracked(Task, PluginMCInstructionHandleKind,
                 InstructionHandles);
  releaseTracked(Task, PluginMCSectionHandleKind, SectionHandles);
  releaseTracked(Task, PluginMCSymbolHandleKind, SymbolHandles);
  releaseTracked(Task, PluginMCExpressionHandleKind,
                 ExpressionHandles);
  releaseTracked(Task, PluginMCFragmentHandleKind, FragmentHandles);
  releaseTracked(Task, PluginMCFixupHandleKind, FixupHandles);
  if (!neverc_handle_is_null(SchemaTokenHandle)) {
    (void)Task.handles().release(SchemaTokenHandle,
                                 PluginMCSchemaTokenHandleKind);
    SchemaTokenHandle = {};
  }
  if (!neverc_handle_is_null(UnitHandle)) {
    (void)Task.handles().release(UnitHandle, PluginMCUnitHandleKind);
    UnitHandle = {};
  }
  if (UnitGeneration == std::numeric_limits<uint64_t>::max())
    UnitGeneration = 1;
  else
    ++UnitGeneration;
}

} // namespace neverc::plugin
