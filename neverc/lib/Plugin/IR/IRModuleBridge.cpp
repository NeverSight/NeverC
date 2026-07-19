#include "neverc/Plugin/Host/IRPluginBridge.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/Comdat.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalIFunc.h"
#include "llvm/IR/GlobalObject.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Error.h"
#include <cstddef>
#include <limits>

using namespace llvm;

namespace neverc::plugin {
namespace {

NevercStatus moduleStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

bool sameHandle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

template <typename Range>
NevercStatus collectRange(IRPluginBridge &Bridge, Range &&Values,
                          NevercIRValueCursor *Cursor,
                          MutableArrayRef<NevercIRValueHandle> OutValues,
                          uint64_t *OutCount) {
  uint64_t Position = 0;
  uint64_t Written = 0;
  for (auto &Value : Values) {
    if (Position++ < Cursor->Position)
      continue;
    if (Written == OutValues.size())
      break;
    auto Handle = Bridge.wrapValue(Value);
    if (!Handle) {
      consumeError(Handle.takeError());
      return moduleStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    OutValues[static_cast<size_t>(Written++)] = *Handle;
  }
  Cursor->Position += Written;
  *OutCount = Written;
  return neverc_status_ok();
}

Expected<GlobalValue::LinkageTypes>
decodeLinkage(NevercIRLinkage Linkage) {
  switch (Linkage) {
#define NEVERC_IR_SCHEMA_INTERNAL_LINKAGE(Internal, Symbol, ID)               \
  case ID:                                                                    \
    return GlobalValue::Internal;
#include "neverc/Plugin/Schema/PluginIRSchema.inc"
#undef NEVERC_IR_SCHEMA_INTERNAL_LINKAGE
  default:
    return createStringError(inconvertibleErrorCode(), "unknown IR linkage");
  }
}

NevercIRLinkage encodeLinkage(GlobalValue::LinkageTypes Linkage) {
  switch (Linkage) {
#define NEVERC_IR_SCHEMA_INTERNAL_LINKAGE(Internal, Symbol, ID)               \
  case GlobalValue::Internal:                                                 \
    return ID;
#include "neverc/Plugin/Schema/PluginIRSchema.inc"
#undef NEVERC_IR_SCHEMA_INTERNAL_LINKAGE
  }
  return NEVERC_IR_LINKAGE_UNKNOWN;
}

Expected<GlobalValue::VisibilityTypes>
decodeVisibility(NevercIRVisibility Visibility) {
  switch (Visibility) {
#define NEVERC_IR_SCHEMA_INTERNAL_VISIBILITY(Internal, Symbol, ID)            \
  case ID:                                                                    \
    return GlobalValue::Internal;
#include "neverc/Plugin/Schema/PluginIRSchema.inc"
#undef NEVERC_IR_SCHEMA_INTERNAL_VISIBILITY
  default:
    return createStringError(inconvertibleErrorCode(),
                             "unknown IR visibility");
  }
}

NevercIRVisibility encodeVisibility(GlobalValue::VisibilityTypes Visibility) {
  switch (Visibility) {
#define NEVERC_IR_SCHEMA_INTERNAL_VISIBILITY(Internal, Symbol, ID)            \
  case GlobalValue::Internal:                                                 \
    return ID;
#include "neverc/Plugin/Schema/PluginIRSchema.inc"
#undef NEVERC_IR_SCHEMA_INTERNAL_VISIBILITY
  }
  return NEVERC_IR_VISIBILITY_UNKNOWN;
}

} // namespace

StringRef IRPluginBridge::getModuleIdentifier() const {
  return Module->getModuleIdentifier();
}

NevercStatus IRPluginBridge::setModuleIdentifier(StringRef Identifier) {
  NevercStatus Status = checkMutationAllowed();
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Identifier.empty())
    return moduleStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Module->setModuleIdentifier(Identifier);
  noteMutation();
  return neverc_status_ok();
}

StringRef IRPluginBridge::getModuleTargetTriple() const {
  return Module->getTargetTriple();
}

NevercStatus IRPluginBridge::setModuleTargetTriple(StringRef Triple) {
  NevercStatus Status = checkMutationAllowed();
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Module->setTargetTriple(Triple);
  noteMutation();
  return neverc_status_ok();
}

StringRef IRPluginBridge::getModuleDataLayout() const {
  return Module->getDataLayoutStr();
}

NevercStatus IRPluginBridge::setModuleDataLayout(StringRef DataLayoutText) {
  NevercStatus Status = checkMutationAllowed();
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Expected<DataLayout> Parsed = DataLayout::parse(DataLayoutText);
  if (!Parsed) {
    consumeError(Parsed.takeError());
    return moduleStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  }
  Module->setDataLayout(*Parsed);
  noteMutation();
  return neverc_status_ok();
}

StringRef IRPluginBridge::getModuleInlineAssembly() const {
  return Module->getModuleInlineAsm();
}

NevercStatus IRPluginBridge::setModuleInlineAssembly(StringRef Assembly) {
  NevercStatus Status = checkMutationAllowed();
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Module->setModuleInlineAsm(Assembly);
  noteMutation();
  return neverc_status_ok();
}

NevercStatus IRPluginBridge::beginValueCursor(
    NevercHandle Container, NevercIRValueCollection Collection,
    NevercIRValueCursor *OutCursor) const {
  if (!OutCursor)
    return moduleStatus(NEVERC_STATUS_INVALID_ARGUMENT);

  switch (Collection) {
  case NEVERC_IR_COLLECTION_MODULE_FUNCTIONS:
  case NEVERC_IR_COLLECTION_MODULE_GLOBALS:
  case NEVERC_IR_COLLECTION_MODULE_ALIASES:
  case NEVERC_IR_COLLECTION_MODULE_I_FUNCS: {
    llvm::Module *Resolved = nullptr;
    NevercStatus Status = resolveModule(Container, &Resolved);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    break;
  }
  case NEVERC_IR_COLLECTION_FUNCTION_ARGUMENTS:
  case NEVERC_IR_COLLECTION_FUNCTION_BLOCKS: {
    Value *Resolved = nullptr;
    NevercStatus Status = resolveValue(Container, &Resolved);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (!isa<Function>(Resolved))
      return moduleStatus(NEVERC_STATUS_WRONG_TYPE);
    break;
  }
  case NEVERC_IR_COLLECTION_BLOCK_INSTRUCTIONS: {
    Value *Resolved = nullptr;
    NevercStatus Status = resolveValue(Container, &Resolved);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (!isa<BasicBlock>(Resolved))
      return moduleStatus(NEVERC_STATUS_WRONG_TYPE);
    break;
  }
  default:
    return moduleStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  }

  *OutCursor = {};
  OutCursor->Header = {sizeof(*OutCursor), NEVERC_IR_CORE_API_MAJOR,
                       NEVERC_IR_CORE_API_MINOR, 0};
  OutCursor->Container = Container;
  OutCursor->MutationGeneration = MutationGeneration;
  OutCursor->Collection = Collection;
  return neverc_status_ok();
}

NevercStatus IRPluginBridge::collectValueCursor(
    NevercIRValueCursor *Cursor,
    MutableArrayRef<NevercIRValueHandle> OutValues, uint64_t *OutCount) {
  if (!Cursor || !OutCount ||
      Cursor->Header.StructSize < sizeof(NevercIRValueCursor) ||
      Cursor->Header.Major != NEVERC_IR_CORE_API_MAJOR)
    return moduleStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutCount = 0;
  if (Cursor->MutationGeneration != MutationGeneration)
    return moduleStatus(NEVERC_STATUS_STALE_HANDLE);

  switch (Cursor->Collection) {
  case NEVERC_IR_COLLECTION_MODULE_FUNCTIONS: {
    llvm::Module *Resolved = nullptr;
    NevercStatus Status = resolveModule(Cursor->Container, &Resolved);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    return collectRange(*this, Resolved->functions(), Cursor, OutValues,
                        OutCount);
  }
  case NEVERC_IR_COLLECTION_MODULE_GLOBALS: {
    llvm::Module *Resolved = nullptr;
    NevercStatus Status = resolveModule(Cursor->Container, &Resolved);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    return collectRange(*this, Resolved->globals(), Cursor, OutValues,
                        OutCount);
  }
  case NEVERC_IR_COLLECTION_MODULE_ALIASES: {
    llvm::Module *Resolved = nullptr;
    NevercStatus Status = resolveModule(Cursor->Container, &Resolved);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    return collectRange(*this, Resolved->aliases(), Cursor, OutValues,
                        OutCount);
  }
  case NEVERC_IR_COLLECTION_MODULE_I_FUNCS: {
    llvm::Module *Resolved = nullptr;
    NevercStatus Status = resolveModule(Cursor->Container, &Resolved);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    return collectRange(*this, Resolved->ifuncs(), Cursor, OutValues,
                        OutCount);
  }
  case NEVERC_IR_COLLECTION_FUNCTION_ARGUMENTS:
  case NEVERC_IR_COLLECTION_FUNCTION_BLOCKS: {
    Value *Resolved = nullptr;
    NevercStatus Status = resolveValue(Cursor->Container, &Resolved);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    auto *FunctionValue = dyn_cast<Function>(Resolved);
    if (!FunctionValue)
      return moduleStatus(NEVERC_STATUS_WRONG_TYPE);
    if (Cursor->Collection == NEVERC_IR_COLLECTION_FUNCTION_ARGUMENTS)
      return collectRange(*this, FunctionValue->args(), Cursor, OutValues,
                          OutCount);
    return collectRange(*this, *FunctionValue, Cursor, OutValues, OutCount);
  }
  case NEVERC_IR_COLLECTION_BLOCK_INSTRUCTIONS: {
    Value *Resolved = nullptr;
    NevercStatus Status = resolveValue(Cursor->Container, &Resolved);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    auto *Block = dyn_cast<BasicBlock>(Resolved);
    if (!Block)
      return moduleStatus(NEVERC_STATUS_WRONG_TYPE);
    return collectRange(*this, *Block, Cursor, OutValues, OutCount);
  }
  default:
    return moduleStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  }
}

NevercStatus IRPluginBridge::getGlobalLinkage(
    NevercIRValueHandle Handle, NevercIRLinkage *OutLinkage) const {
  if (!OutLinkage)
    return moduleStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutLinkage = NEVERC_IR_LINKAGE_UNKNOWN;
  Value *Resolved = nullptr;
  NevercStatus Status = resolveValue(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Global = dyn_cast<GlobalValue>(Resolved);
  if (!Global)
    return moduleStatus(NEVERC_STATUS_WRONG_TYPE);
  *OutLinkage = encodeLinkage(Global->getLinkage());
  return *OutLinkage == NEVERC_IR_LINKAGE_UNKNOWN
             ? moduleStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE)
             : neverc_status_ok();
}

NevercStatus IRPluginBridge::setGlobalLinkage(
    NevercIRValueHandle Handle, NevercIRLinkage Linkage) {
  NevercStatus Status = checkMutationAllowed();
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Value *Resolved = nullptr;
  Status = resolveValue(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Global = dyn_cast<GlobalValue>(Resolved);
  if (!Global)
    return moduleStatus(NEVERC_STATUS_WRONG_TYPE);
  auto Internal = decodeLinkage(Linkage);
  if (!Internal) {
    consumeError(Internal.takeError());
    return moduleStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  }
  Global->setLinkage(*Internal);
  noteMutation();
  return neverc_status_ok();
}

NevercStatus IRPluginBridge::getGlobalVisibility(
    NevercIRValueHandle Handle,
    NevercIRVisibility *OutVisibility) const {
  if (!OutVisibility)
    return moduleStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutVisibility = NEVERC_IR_VISIBILITY_UNKNOWN;
  Value *Resolved = nullptr;
  NevercStatus Status = resolveValue(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Global = dyn_cast<GlobalValue>(Resolved);
  if (!Global)
    return moduleStatus(NEVERC_STATUS_WRONG_TYPE);
  *OutVisibility = encodeVisibility(Global->getVisibility());
  return *OutVisibility == NEVERC_IR_VISIBILITY_UNKNOWN
             ? moduleStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE)
             : neverc_status_ok();
}

NevercStatus IRPluginBridge::setGlobalVisibility(
    NevercIRValueHandle Handle, NevercIRVisibility Visibility) {
  NevercStatus Status = checkMutationAllowed();
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Value *Resolved = nullptr;
  Status = resolveValue(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Global = dyn_cast<GlobalValue>(Resolved);
  if (!Global)
    return moduleStatus(NEVERC_STATUS_WRONG_TYPE);
  auto Internal = decodeVisibility(Visibility);
  if (!Internal) {
    consumeError(Internal.takeError());
    return moduleStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  }
  if (Global->hasLocalLinkage() &&
      *Internal != GlobalValue::DefaultVisibility)
    return moduleStatus(NEVERC_STATUS_INVALID_STATE);
  Global->setVisibility(*Internal);
  noteMutation();
  return neverc_status_ok();
}

Expected<StringRef>
IRPluginBridge::getGlobalSection(NevercIRValueHandle Handle) const {
  Value *Resolved = nullptr;
  NevercStatus Status = resolveValue(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return createStringError(inconvertibleErrorCode(),
                             "invalid global value handle");
  auto *Global = dyn_cast<GlobalObject>(Resolved);
  if (!Global)
    return createStringError(inconvertibleErrorCode(),
                             "IR value is not a global object");
  return Global->getSection();
}

NevercStatus IRPluginBridge::setGlobalSection(
    NevercIRValueHandle Handle, StringRef Section) {
  NevercStatus Status = checkMutationAllowed();
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Value *Resolved = nullptr;
  Status = resolveValue(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Global = dyn_cast<GlobalObject>(Resolved);
  if (!Global)
    return moduleStatus(NEVERC_STATUS_WRONG_TYPE);
  Global->setSection(Section);
  noteMutation();
  return neverc_status_ok();
}

Expected<NevercIRComdatHandle>
IRPluginBridge::wrapComdat(Comdat &Value) {
  auto Existing = ComdatHandles.find(&Value);
  if (Existing != ComdatHandles.end())
    return Existing->second;
  auto Created = Task.handles().create(PluginIRComdatHandleKind, &Value);
  if (!Created)
    return Created.takeError();
  ComdatHandles.emplace(&Value, *Created);
  return *Created;
}

NevercStatus
IRPluginBridge::resolveComdat(NevercIRComdatHandle Handle,
                              Comdat **OutComdat) const {
  if (!OutComdat)
    return moduleStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutComdat = nullptr;
  void *Payload = nullptr;
  NevercStatus Status =
      Task.handles().resolve(Handle, PluginIRComdatHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Resolved = static_cast<Comdat *>(Payload);
  auto Existing = ComdatHandles.find(Resolved);
  if (Existing == ComdatHandles.end() ||
      !sameHandle(Existing->second, Handle))
    return moduleStatus(NEVERC_STATUS_WRONG_SCOPE);
  *OutComdat = Resolved;
  return neverc_status_ok();
}

Expected<NevercIRComdatHandle>
IRPluginBridge::getOrInsertComdat(StringRef Name) {
  if (checkMutationAllowed().Code != NEVERC_STATUS_OK)
    return createStringError(inconvertibleErrorCode(),
                             "IR mutation is unavailable in this context");
  if (Name.empty())
    return createStringError(inconvertibleErrorCode(),
                             "COMDAT name must not be empty");
  Comdat *Value = Module->getOrInsertComdat(Name);
  auto Handle = wrapComdat(*Value);
  if (Handle)
    noteMutation();
  return Handle;
}

Expected<NevercIRComdatHandle>
IRPluginBridge::getGlobalComdat(NevercIRValueHandle Handle) {
  Value *Resolved = nullptr;
  NevercStatus Status = resolveValue(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return createStringError(inconvertibleErrorCode(),
                             "invalid global value handle");
  auto *Global = dyn_cast<GlobalObject>(Resolved);
  if (!Global)
    return createStringError(inconvertibleErrorCode(),
                             "IR value is not a global object");
  if (!Global->hasComdat())
    return NevercIRComdatHandle{};
  return wrapComdat(*Global->getComdat());
}

NevercStatus IRPluginBridge::setGlobalComdat(
    NevercIRValueHandle Handle, NevercIRComdatHandle ComdatHandle) {
  NevercStatus Status = checkMutationAllowed();
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Value *Resolved = nullptr;
  Status = resolveValue(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Global = dyn_cast<GlobalObject>(Resolved);
  if (!Global)
    return moduleStatus(NEVERC_STATUS_WRONG_TYPE);
  if (neverc_handle_is_null(ComdatHandle) == NEVERC_TRUE) {
    Global->setComdat(nullptr);
  } else {
    Comdat *ResolvedComdat = nullptr;
    Status = resolveComdat(ComdatHandle, &ResolvedComdat);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    Global->setComdat(ResolvedComdat);
  }
  noteMutation();
  return neverc_status_ok();
}

void IRPluginBridge::noteMutation() {
  if (MutationGeneration == std::numeric_limits<uint64_t>::max())
    MutationGeneration = 1;
  else
    ++MutationGeneration;
}

void IRPluginBridge::enterReadOnly() {
  ReadOnlyDepth.fetch_add(1, std::memory_order_acq_rel);
}

void IRPluginBridge::leaveReadOnly() {
  ReadOnlyDepth.fetch_sub(1, std::memory_order_acq_rel);
}

NevercStatus IRPluginBridge::checkMutationAllowed() const {
  return ReadOnlyDepth.load(std::memory_order_acquire) == 0
             ? neverc_status_ok()
             : moduleStatus(NEVERC_STATUS_POLICY_VIOLATION);
}

} // namespace neverc::plugin
