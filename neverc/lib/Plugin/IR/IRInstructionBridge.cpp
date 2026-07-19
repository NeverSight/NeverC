#include "neverc/Plugin/Host/IRPluginBridge.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

namespace neverc::plugin {
namespace {

NevercStatus instructionStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

NevercIROpcode encodeOpcode(unsigned Opcode) {
  switch (Opcode) {
#define NEVERC_IR_SCHEMA_INTERNAL_OPCODE(Internal, Symbol, ID)                \
  case Instruction::Internal:                                                 \
    return ID;
#include "neverc/Plugin/Schema/PluginIRSchema.inc"
#undef NEVERC_IR_SCHEMA_INTERNAL_OPCODE
  default:
    return NEVERC_IR_OPCODE_UNKNOWN;
  }
}

Expected<CmpInst::Predicate> decodePredicate(NevercIRPredicate Predicate) {
  switch (Predicate) {
#define NEVERC_IR_SCHEMA_INTERNAL_PREDICATE(Internal, Symbol, ID)             \
  case ID:                                                                    \
    return CmpInst::Internal;
#include "neverc/Plugin/Schema/PluginIRSchema.inc"
#undef NEVERC_IR_SCHEMA_INTERNAL_PREDICATE
  default:
    return createStringError(inconvertibleErrorCode(),
                             "unknown IR predicate");
  }
}

NevercIRPredicate encodePredicate(CmpInst::Predicate Predicate) {
  switch (Predicate) {
#define NEVERC_IR_SCHEMA_INTERNAL_PREDICATE(Internal, Symbol, ID)             \
  case CmpInst::Internal:                                                     \
    return ID;
#include "neverc/Plugin/Schema/PluginIRSchema.inc"
#undef NEVERC_IR_SCHEMA_INTERNAL_PREDICATE
  default:
    return NEVERC_IR_PREDICATE_UNKNOWN;
  }
}

Expected<CallingConv::ID>
decodeCallingConvention(NevercIRCallingConvention CallingConvention) {
  switch (CallingConvention) {
#define NEVERC_IR_SCHEMA_INTERNAL_CALLING_CONVENTION(Internal, Symbol, ID)    \
  case ID:                                                                    \
    return CallingConv::Internal;
#include "neverc/Plugin/Schema/PluginIRSchema.inc"
#undef NEVERC_IR_SCHEMA_INTERNAL_CALLING_CONVENTION
  default:
    return createStringError(inconvertibleErrorCode(),
                             "unknown IR calling convention");
  }
}

NevercIRCallingConvention
encodeCallingConvention(CallingConv::ID CallingConvention) {
  switch (CallingConvention) {
#define NEVERC_IR_SCHEMA_INTERNAL_CALLING_CONVENTION(Internal, Symbol, ID)    \
  case CallingConv::Internal:                                                 \
    return ID;
#include "neverc/Plugin/Schema/PluginIRSchema.inc"
#undef NEVERC_IR_SCHEMA_INTERNAL_CALLING_CONVENTION
  default:
    return NEVERC_IR_CALLING_CONVENTION_UNKNOWN;
  }
}

Expected<AtomicOrdering> decodeAtomicOrdering(NevercIRAtomicOrdering Ordering) {
  switch (Ordering) {
  case NEVERC_IR_ATOMIC_ORDERING_NOT_ATOMIC:
    return AtomicOrdering::NotAtomic;
  case NEVERC_IR_ATOMIC_ORDERING_UNORDERED:
    return AtomicOrdering::Unordered;
  case NEVERC_IR_ATOMIC_ORDERING_MONOTONIC:
    return AtomicOrdering::Monotonic;
  case NEVERC_IR_ATOMIC_ORDERING_ACQUIRE:
    return AtomicOrdering::Acquire;
  case NEVERC_IR_ATOMIC_ORDERING_RELEASE:
    return AtomicOrdering::Release;
  case NEVERC_IR_ATOMIC_ORDERING_ACQUIRE_RELEASE:
    return AtomicOrdering::AcquireRelease;
  case NEVERC_IR_ATOMIC_ORDERING_SEQUENTIALLY_CONSISTENT:
    return AtomicOrdering::SequentiallyConsistent;
  default:
    return createStringError(inconvertibleErrorCode(),
                             "unknown atomic ordering");
  }
}

NevercIRAtomicOrdering encodeAtomicOrdering(AtomicOrdering Ordering) {
  switch (Ordering) {
  case AtomicOrdering::NotAtomic:
    return NEVERC_IR_ATOMIC_ORDERING_NOT_ATOMIC;
  case AtomicOrdering::Unordered:
    return NEVERC_IR_ATOMIC_ORDERING_UNORDERED;
  case AtomicOrdering::Monotonic:
    return NEVERC_IR_ATOMIC_ORDERING_MONOTONIC;
  case AtomicOrdering::Acquire:
    return NEVERC_IR_ATOMIC_ORDERING_ACQUIRE;
  case AtomicOrdering::Release:
    return NEVERC_IR_ATOMIC_ORDERING_RELEASE;
  case AtomicOrdering::AcquireRelease:
    return NEVERC_IR_ATOMIC_ORDERING_ACQUIRE_RELEASE;
  case AtomicOrdering::SequentiallyConsistent:
    return NEVERC_IR_ATOMIC_ORDERING_SEQUENTIALLY_CONSISTENT;
  }
  llvm_unreachable("all atomic orderings are covered");
}

NevercIRFastMathFlags encodeFastMathFlags(const FastMathFlags &Flags) {
  NevercIRFastMathFlags Result = 0;
  if (Flags.allowReassoc())
    Result |= NEVERC_IR_FAST_MATH_ALLOW_REASSOC;
  if (Flags.noNaNs())
    Result |= NEVERC_IR_FAST_MATH_NO_NANS;
  if (Flags.noInfs())
    Result |= NEVERC_IR_FAST_MATH_NO_INFS;
  if (Flags.noSignedZeros())
    Result |= NEVERC_IR_FAST_MATH_NO_SIGNED_ZEROS;
  if (Flags.allowReciprocal())
    Result |= NEVERC_IR_FAST_MATH_ALLOW_RECIPROCAL;
  if (Flags.allowContract())
    Result |= NEVERC_IR_FAST_MATH_ALLOW_CONTRACT;
  if (Flags.approxFunc())
    Result |= NEVERC_IR_FAST_MATH_APPROX_FUNC;
  return Result;
}

FastMathFlags decodeFastMathFlags(NevercIRFastMathFlags Bits) {
  FastMathFlags Flags;
  Flags.setAllowReassoc(Bits & NEVERC_IR_FAST_MATH_ALLOW_REASSOC);
  Flags.setNoNaNs(Bits & NEVERC_IR_FAST_MATH_NO_NANS);
  Flags.setNoInfs(Bits & NEVERC_IR_FAST_MATH_NO_INFS);
  Flags.setNoSignedZeros(Bits & NEVERC_IR_FAST_MATH_NO_SIGNED_ZEROS);
  Flags.setAllowReciprocal(Bits & NEVERC_IR_FAST_MATH_ALLOW_RECIPROCAL);
  Flags.setAllowContract(Bits & NEVERC_IR_FAST_MATH_ALLOW_CONTRACT);
  Flags.setApproxFunc(Bits & NEVERC_IR_FAST_MATH_APPROX_FUNC);
  return Flags;
}

bool readVolatile(Instruction &InstructionValue, bool *OutValue) {
  if (auto *Load = dyn_cast<LoadInst>(&InstructionValue))
    *OutValue = Load->isVolatile();
  else if (auto *Store = dyn_cast<StoreInst>(&InstructionValue))
    *OutValue = Store->isVolatile();
  else if (auto *RMW = dyn_cast<AtomicRMWInst>(&InstructionValue))
    *OutValue = RMW->isVolatile();
  else if (auto *CmpXchg = dyn_cast<AtomicCmpXchgInst>(&InstructionValue))
    *OutValue = CmpXchg->isVolatile();
  else
    return false;
  return true;
}

bool writeVolatile(Instruction &InstructionValue, bool Value) {
  if (auto *Load = dyn_cast<LoadInst>(&InstructionValue))
    Load->setVolatile(Value);
  else if (auto *Store = dyn_cast<StoreInst>(&InstructionValue))
    Store->setVolatile(Value);
  else if (auto *RMW = dyn_cast<AtomicRMWInst>(&InstructionValue))
    RMW->setVolatile(Value);
  else if (auto *CmpXchg = dyn_cast<AtomicCmpXchgInst>(&InstructionValue))
    CmpXchg->setVolatile(Value);
  else
    return false;
  return true;
}

MaybeAlign readAlignment(Instruction &InstructionValue) {
  if (auto *Alloca = dyn_cast<AllocaInst>(&InstructionValue))
    return Alloca->getAlign();
  if (auto *Load = dyn_cast<LoadInst>(&InstructionValue))
    return Load->getAlign();
  if (auto *Store = dyn_cast<StoreInst>(&InstructionValue))
    return Store->getAlign();
  if (auto *RMW = dyn_cast<AtomicRMWInst>(&InstructionValue))
    return RMW->getAlign();
  if (auto *CmpXchg = dyn_cast<AtomicCmpXchgInst>(&InstructionValue))
    return CmpXchg->getAlign();
  return std::nullopt;
}

bool writeAlignment(Instruction &InstructionValue, Align Alignment) {
  if (auto *Alloca = dyn_cast<AllocaInst>(&InstructionValue))
    Alloca->setAlignment(Alignment);
  else if (auto *Load = dyn_cast<LoadInst>(&InstructionValue))
    Load->setAlignment(Alignment);
  else if (auto *Store = dyn_cast<StoreInst>(&InstructionValue))
    Store->setAlignment(Alignment);
  else if (auto *RMW = dyn_cast<AtomicRMWInst>(&InstructionValue))
    RMW->setAlignment(Alignment);
  else if (auto *CmpXchg = dyn_cast<AtomicCmpXchgInst>(&InstructionValue))
    CmpXchg->setAlignment(Alignment);
  else
    return false;
  return true;
}

std::optional<AtomicOrdering>
readAtomicOrdering(Instruction &InstructionValue) {
  if (auto *Load = dyn_cast<LoadInst>(&InstructionValue))
    return Load->getOrdering();
  if (auto *Store = dyn_cast<StoreInst>(&InstructionValue))
    return Store->getOrdering();
  if (auto *Fence = dyn_cast<FenceInst>(&InstructionValue))
    return Fence->getOrdering();
  if (auto *RMW = dyn_cast<AtomicRMWInst>(&InstructionValue))
    return RMW->getOrdering();
  return std::nullopt;
}

bool writeAtomicOrdering(Instruction &InstructionValue,
                         AtomicOrdering Ordering) {
  if (auto *Load = dyn_cast<LoadInst>(&InstructionValue)) {
    if (Ordering == AtomicOrdering::Release ||
        Ordering == AtomicOrdering::AcquireRelease)
      return false;
    Load->setOrdering(Ordering);
  } else if (auto *Store = dyn_cast<StoreInst>(&InstructionValue)) {
    if (Ordering == AtomicOrdering::Acquire ||
        Ordering == AtomicOrdering::AcquireRelease)
      return false;
    Store->setOrdering(Ordering);
  } else if (auto *Fence = dyn_cast<FenceInst>(&InstructionValue)) {
    if (Ordering == AtomicOrdering::NotAtomic ||
        Ordering == AtomicOrdering::Unordered ||
        Ordering == AtomicOrdering::Monotonic)
      return false;
    Fence->setOrdering(Ordering);
  } else if (auto *RMW = dyn_cast<AtomicRMWInst>(&InstructionValue)) {
    if (Ordering == AtomicOrdering::NotAtomic ||
        Ordering == AtomicOrdering::Unordered)
      return false;
    RMW->setOrdering(Ordering);
  } else {
    return false;
  }
  return true;
}

std::optional<SyncScope::ID>
readSyncScope(Instruction &InstructionValue) {
  if (auto *Load = dyn_cast<LoadInst>(&InstructionValue))
    return Load->getSyncScopeID();
  if (auto *Store = dyn_cast<StoreInst>(&InstructionValue))
    return Store->getSyncScopeID();
  if (auto *Fence = dyn_cast<FenceInst>(&InstructionValue))
    return Fence->getSyncScopeID();
  if (auto *RMW = dyn_cast<AtomicRMWInst>(&InstructionValue))
    return RMW->getSyncScopeID();
  if (auto *CmpXchg = dyn_cast<AtomicCmpXchgInst>(&InstructionValue))
    return CmpXchg->getSyncScopeID();
  return std::nullopt;
}

bool writeSyncScope(Instruction &InstructionValue, SyncScope::ID Scope) {
  if (auto *Load = dyn_cast<LoadInst>(&InstructionValue))
    Load->setSyncScopeID(Scope);
  else if (auto *Store = dyn_cast<StoreInst>(&InstructionValue))
    Store->setSyncScopeID(Scope);
  else if (auto *Fence = dyn_cast<FenceInst>(&InstructionValue))
    Fence->setSyncScopeID(Scope);
  else if (auto *RMW = dyn_cast<AtomicRMWInst>(&InstructionValue))
    RMW->setSyncScopeID(Scope);
  else if (auto *CmpXchg = dyn_cast<AtomicCmpXchgInst>(&InstructionValue))
    CmpXchg->setSyncScopeID(Scope);
  else
    return false;
  return true;
}

bool requireKind(const NevercIRPropertyValue &Value,
                 NevercIRPropertyValueKind Kind) {
  return Value.Header.StructSize >= sizeof(NevercIRPropertyValue) &&
         Value.Kind == Kind;
}

void initializeProperty(NevercIRPropertyValue *Value,
                        NevercIRPropertyValueKind Kind) {
  *Value = {};
  Value->Header = {sizeof(*Value), NEVERC_IR_CORE_API_MAJOR,
                   NEVERC_IR_CORE_API_MINOR, 0};
  Value->Kind = Kind;
}

} // namespace

NevercStatus IRPluginBridge::getInstructionOpcode(
    NevercIRValueHandle Handle, NevercIROpcode *OutOpcode) const {
  if (!OutOpcode)
    return instructionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutOpcode = NEVERC_IR_OPCODE_UNKNOWN;
  Value *Resolved = nullptr;
  NevercStatus Status = resolveValue(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *InstructionValue = dyn_cast<Instruction>(Resolved);
  if (!InstructionValue)
    return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
  *OutOpcode = encodeOpcode(InstructionValue->getOpcode());
  return *OutOpcode == NEVERC_IR_OPCODE_UNKNOWN
             ? instructionStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE)
             : neverc_status_ok();
}

NevercStatus IRPluginBridge::getInstructionProperty(
    NevercIRValueHandle Handle, NevercIRPropertyID Property,
    NevercIRPropertyValue *OutValue) {
  if (!OutValue ||
      OutValue->Header.StructSize < sizeof(NevercIRPropertyValue))
    return instructionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Value *Resolved = nullptr;
  NevercStatus Status = resolveValue(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *InstructionValue = dyn_cast<Instruction>(Resolved);
  if (!InstructionValue)
    return instructionStatus(NEVERC_STATUS_WRONG_TYPE);

  switch (Property) {
  case NEVERC_IR_PROPERTY_NAME:
    if (InstructionValue->getType()->isVoidTy())
      return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
    initializeProperty(OutValue, NEVERC_IR_PROPERTY_VALUE_STRING);
    OutValue->StringValue = {InstructionValue->getName().data(),
                            InstructionValue->getName().size()};
    return neverc_status_ok();
  case NEVERC_IR_PROPERTY_FAST_MATH_FLAGS:
    if (!isa<FPMathOperator>(InstructionValue))
      return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
    initializeProperty(OutValue, NEVERC_IR_PROPERTY_VALUE_FLAGS);
    OutValue->UnsignedValue =
        encodeFastMathFlags(InstructionValue->getFastMathFlags());
    return neverc_status_ok();
  case NEVERC_IR_PROPERTY_NUW:
  case NEVERC_IR_PROPERTY_NSW:
  case NEVERC_IR_PROPERTY_NUSW: {
    bool Flag = false;
    if (isa<OverflowingBinaryOperator>(InstructionValue)) {
      if (Property == NEVERC_IR_PROPERTY_NUSW)
        return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
      Flag = Property == NEVERC_IR_PROPERTY_NUW
                 ? InstructionValue->hasNoUnsignedWrap()
                 : InstructionValue->hasNoSignedWrap();
    } else if (auto *GEP = dyn_cast<GetElementPtrInst>(InstructionValue)) {
      if (Property == NEVERC_IR_PROPERTY_NSW)
        return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
      Flag = Property == NEVERC_IR_PROPERTY_NUW
                 ? GEP->hasNoUnsignedWrap()
                 : GEP->getNoWrapFlags().hasNoUnsignedSignedWrap();
    } else {
      return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
    }
    initializeProperty(OutValue, NEVERC_IR_PROPERTY_VALUE_BOOL);
    OutValue->UnsignedValue = Flag ? NEVERC_TRUE : NEVERC_FALSE;
    return neverc_status_ok();
  }
  case NEVERC_IR_PROPERTY_EXACT:
    if (!isa<PossiblyExactOperator>(InstructionValue))
      return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
    initializeProperty(OutValue, NEVERC_IR_PROPERTY_VALUE_BOOL);
    OutValue->UnsignedValue =
        InstructionValue->isExact() ? NEVERC_TRUE : NEVERC_FALSE;
    return neverc_status_ok();
  case NEVERC_IR_PROPERTY_DISJOINT: {
    auto *Disjoint = dyn_cast<PossiblyDisjointInst>(InstructionValue);
    if (!Disjoint)
      return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
    initializeProperty(OutValue, NEVERC_IR_PROPERTY_VALUE_BOOL);
    OutValue->UnsignedValue =
        Disjoint->isDisjoint() ? NEVERC_TRUE : NEVERC_FALSE;
    return neverc_status_ok();
  }
  case NEVERC_IR_PROPERTY_VOLATILE: {
    bool Volatile = false;
    if (!readVolatile(*InstructionValue, &Volatile))
      return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
    initializeProperty(OutValue, NEVERC_IR_PROPERTY_VALUE_BOOL);
    OutValue->UnsignedValue = Volatile ? NEVERC_TRUE : NEVERC_FALSE;
    return neverc_status_ok();
  }
  case NEVERC_IR_PROPERTY_ALIGNMENT: {
    MaybeAlign Alignment = readAlignment(*InstructionValue);
    if (!Alignment)
      return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
    initializeProperty(OutValue, NEVERC_IR_PROPERTY_VALUE_UINT);
    OutValue->UnsignedValue = Alignment->value();
    return neverc_status_ok();
  }
  case NEVERC_IR_PROPERTY_ATOMIC_ORDERING: {
    auto Ordering = readAtomicOrdering(*InstructionValue);
    if (!Ordering)
      return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
    initializeProperty(OutValue, NEVERC_IR_PROPERTY_VALUE_ENUM);
    OutValue->UnsignedValue = encodeAtomicOrdering(*Ordering);
    return neverc_status_ok();
  }
  case NEVERC_IR_PROPERTY_SYNC_SCOPE: {
    auto Scope = readSyncScope(*InstructionValue);
    if (!Scope)
      return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
    SmallVector<StringRef, 8> Names;
    Context->getSyncScopeNames(Names);
    if (*Scope >= Names.size())
      return instructionStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
    initializeProperty(OutValue, NEVERC_IR_PROPERTY_VALUE_STRING);
    OutValue->StringValue = {Names[*Scope].data(), Names[*Scope].size()};
    return neverc_status_ok();
  }
  case NEVERC_IR_PROPERTY_PREDICATE: {
    auto *Comparison = dyn_cast<CmpInst>(InstructionValue);
    if (!Comparison)
      return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
    initializeProperty(OutValue, NEVERC_IR_PROPERTY_VALUE_ENUM);
    OutValue->UnsignedValue = encodePredicate(Comparison->getPredicate());
    return neverc_status_ok();
  }
  case NEVERC_IR_PROPERTY_CALLING_CONVENTION: {
    auto *Call = dyn_cast<CallBase>(InstructionValue);
    if (!Call)
      return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
    initializeProperty(OutValue, NEVERC_IR_PROPERTY_VALUE_ENUM);
    OutValue->UnsignedValue =
        encodeCallingConvention(Call->getCallingConv());
    return neverc_status_ok();
  }
  case NEVERC_IR_PROPERTY_TAIL_CALL_KIND: {
    auto *Call = dyn_cast<CallInst>(InstructionValue);
    if (!Call)
      return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
    initializeProperty(OutValue, NEVERC_IR_PROPERTY_VALUE_ENUM);
    switch (Call->getTailCallKind()) {
    case CallInst::TCK_None:
      OutValue->UnsignedValue = NEVERC_IR_TAIL_CALL_NONE;
      break;
    case CallInst::TCK_Tail:
      OutValue->UnsignedValue = NEVERC_IR_TAIL_CALL_TAIL;
      break;
    case CallInst::TCK_MustTail:
      OutValue->UnsignedValue = NEVERC_IR_TAIL_CALL_MUST_TAIL;
      break;
    case CallInst::TCK_NoTail:
      OutValue->UnsignedValue = NEVERC_IR_TAIL_CALL_NO_TAIL;
      break;
    }
    return neverc_status_ok();
  }
  case NEVERC_IR_PROPERTY_WEAK: {
    auto *CmpXchg = dyn_cast<AtomicCmpXchgInst>(InstructionValue);
    if (!CmpXchg)
      return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
    initializeProperty(OutValue, NEVERC_IR_PROPERTY_VALUE_BOOL);
    OutValue->UnsignedValue =
        CmpXchg->isWeak() ? NEVERC_TRUE : NEVERC_FALSE;
    return neverc_status_ok();
  }
  case NEVERC_IR_PROPERTY_SUCCESS_ORDERING:
  case NEVERC_IR_PROPERTY_FAILURE_ORDERING: {
    auto *CmpXchg = dyn_cast<AtomicCmpXchgInst>(InstructionValue);
    if (!CmpXchg)
      return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
    initializeProperty(OutValue, NEVERC_IR_PROPERTY_VALUE_ENUM);
    OutValue->UnsignedValue = encodeAtomicOrdering(
        Property == NEVERC_IR_PROPERTY_SUCCESS_ORDERING
            ? CmpXchg->getSuccessOrdering()
            : CmpXchg->getFailureOrdering());
    return neverc_status_ok();
  }
  case NEVERC_IR_PROPERTY_INBOUNDS: {
    auto *GEP = dyn_cast<GetElementPtrInst>(InstructionValue);
    if (!GEP)
      return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
    initializeProperty(OutValue, NEVERC_IR_PROPERTY_VALUE_BOOL);
    OutValue->UnsignedValue =
        GEP->isInBounds() ? NEVERC_TRUE : NEVERC_FALSE;
    return neverc_status_ok();
  }
  case NEVERC_IR_PROPERTY_SOURCE_ELEMENT_TYPE: {
    auto *GEP = dyn_cast<GetElementPtrInst>(InstructionValue);
    if (!GEP)
      return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
    auto Type = wrapType(*GEP->getSourceElementType());
    if (!Type) {
      consumeError(Type.takeError());
      return instructionStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    initializeProperty(OutValue, NEVERC_IR_PROPERTY_VALUE_TYPE);
    OutValue->TypeValue = *Type;
    return neverc_status_ok();
  }
  case NEVERC_IR_PROPERTY_ALLOCATED_TYPE: {
    auto *Alloca = dyn_cast<AllocaInst>(InstructionValue);
    if (!Alloca)
      return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
    auto Type = wrapType(*Alloca->getAllocatedType());
    if (!Type) {
      consumeError(Type.takeError());
      return instructionStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    initializeProperty(OutValue, NEVERC_IR_PROPERTY_VALUE_TYPE);
    OutValue->TypeValue = *Type;
    return neverc_status_ok();
  }
  case NEVERC_IR_PROPERTY_CLEANUP: {
    auto *LandingPad = dyn_cast<LandingPadInst>(InstructionValue);
    if (!LandingPad)
      return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
    initializeProperty(OutValue, NEVERC_IR_PROPERTY_VALUE_BOOL);
    OutValue->UnsignedValue =
        LandingPad->isCleanup() ? NEVERC_TRUE : NEVERC_FALSE;
    return neverc_status_ok();
  }
  case NEVERC_IR_PROPERTY_INDICES:
  case NEVERC_IR_PROPERTY_ATTRIBUTES:
    return instructionStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  default:
    return instructionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  }
}

NevercStatus IRPluginBridge::setInstructionProperty(
    NevercIRValueHandle Handle, NevercIRPropertyID Property,
    const NevercIRPropertyValue &Value) {
  NevercStatus Status = checkMutationAllowed();
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  llvm::Value *Resolved = nullptr;
  Status = resolveValue(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *InstructionValue = dyn_cast<Instruction>(Resolved);
  if (!InstructionValue)
    return instructionStatus(NEVERC_STATUS_WRONG_TYPE);

  switch (Property) {
  case NEVERC_IR_PROPERTY_NAME:
    if (!requireKind(Value, NEVERC_IR_PROPERTY_VALUE_STRING) ||
        (Value.StringValue.Length != 0 && !Value.StringValue.Data))
      return instructionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return setValueName(
        Handle, StringRef(Value.StringValue.Data ? Value.StringValue.Data : "",
                          static_cast<size_t>(Value.StringValue.Length)));
  case NEVERC_IR_PROPERTY_FAST_MATH_FLAGS:
    if (!requireKind(Value, NEVERC_IR_PROPERTY_VALUE_FLAGS))
      return instructionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    if (!isa<FPMathOperator>(InstructionValue))
      return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
    InstructionValue->setFastMathFlags(
        decodeFastMathFlags(Value.UnsignedValue));
    break;
  case NEVERC_IR_PROPERTY_NUW:
  case NEVERC_IR_PROPERTY_NSW:
  case NEVERC_IR_PROPERTY_NUSW: {
    if (!requireKind(Value, NEVERC_IR_PROPERTY_VALUE_BOOL) ||
        Value.UnsignedValue > NEVERC_TRUE)
      return instructionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    bool Enabled = Value.UnsignedValue == NEVERC_TRUE;
    if (isa<OverflowingBinaryOperator>(InstructionValue)) {
      if (Property == NEVERC_IR_PROPERTY_NUSW)
        return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
      if (Property == NEVERC_IR_PROPERTY_NUW)
        InstructionValue->setHasNoUnsignedWrap(Enabled);
      else
        InstructionValue->setHasNoSignedWrap(Enabled);
    } else if (auto *GEP = dyn_cast<GetElementPtrInst>(InstructionValue)) {
      if (Property == NEVERC_IR_PROPERTY_NSW)
        return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
      if (Property == NEVERC_IR_PROPERTY_NUW) {
        GEP->setHasNoUnsignedWrap(Enabled);
      } else {
        GEPNoWrapFlags Flags = GEP->getNoWrapFlags();
        GEP->setNoWrapFlags(
            Enabled ? Flags | GEPNoWrapFlags::noUnsignedSignedWrap()
                    : Flags.withoutNoUnsignedSignedWrap());
      }
    } else {
      return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
    }
    break;
  }
  case NEVERC_IR_PROPERTY_EXACT:
    if (!requireKind(Value, NEVERC_IR_PROPERTY_VALUE_BOOL) ||
        Value.UnsignedValue > NEVERC_TRUE)
      return instructionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    if (!isa<PossiblyExactOperator>(InstructionValue))
      return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
    InstructionValue->setIsExact(Value.UnsignedValue == NEVERC_TRUE);
    break;
  case NEVERC_IR_PROPERTY_DISJOINT: {
    if (!requireKind(Value, NEVERC_IR_PROPERTY_VALUE_BOOL) ||
        Value.UnsignedValue > NEVERC_TRUE)
      return instructionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    auto *Disjoint = dyn_cast<PossiblyDisjointInst>(InstructionValue);
    if (!Disjoint)
      return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
    Disjoint->setIsDisjoint(Value.UnsignedValue == NEVERC_TRUE);
    break;
  }
  case NEVERC_IR_PROPERTY_VOLATILE:
    if (!requireKind(Value, NEVERC_IR_PROPERTY_VALUE_BOOL) ||
        Value.UnsignedValue > NEVERC_TRUE)
      return instructionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    if (!writeVolatile(*InstructionValue,
                       Value.UnsignedValue == NEVERC_TRUE))
      return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
    break;
  case NEVERC_IR_PROPERTY_ALIGNMENT:
    if (!requireKind(Value, NEVERC_IR_PROPERTY_VALUE_UINT) ||
        !isPowerOf2_64(Value.UnsignedValue) ||
        Value.UnsignedValue > Value::MaximumAlignment)
      return instructionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    if (!writeAlignment(*InstructionValue, Align(Value.UnsignedValue)))
      return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
    break;
  case NEVERC_IR_PROPERTY_ATOMIC_ORDERING: {
    if (!requireKind(Value, NEVERC_IR_PROPERTY_VALUE_ENUM))
      return instructionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    auto Ordering = decodeAtomicOrdering(Value.UnsignedValue);
    if (!Ordering) {
      consumeError(Ordering.takeError());
      return instructionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    }
    if (!writeAtomicOrdering(*InstructionValue, *Ordering))
      return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
    break;
  }
  case NEVERC_IR_PROPERTY_SYNC_SCOPE: {
    if (!requireKind(Value, NEVERC_IR_PROPERTY_VALUE_STRING) ||
        (Value.StringValue.Length != 0 && !Value.StringValue.Data))
      return instructionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    StringRef ScopeName(Value.StringValue.Data ? Value.StringValue.Data : "",
                        static_cast<size_t>(Value.StringValue.Length));
    if (!writeSyncScope(*InstructionValue,
                        Context->getOrInsertSyncScopeID(ScopeName)))
      return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
    break;
  }
  case NEVERC_IR_PROPERTY_PREDICATE: {
    if (!requireKind(Value, NEVERC_IR_PROPERTY_VALUE_ENUM))
      return instructionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    auto *Comparison = dyn_cast<CmpInst>(InstructionValue);
    if (!Comparison)
      return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
    auto Predicate = decodePredicate(Value.UnsignedValue);
    if (!Predicate) {
      consumeError(Predicate.takeError());
      return instructionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    }
    if (CmpInst::isIntPredicate(*Predicate) !=
        isa<ICmpInst>(Comparison))
      return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
    Comparison->setPredicate(*Predicate);
    break;
  }
  case NEVERC_IR_PROPERTY_CALLING_CONVENTION: {
    if (!requireKind(Value, NEVERC_IR_PROPERTY_VALUE_ENUM))
      return instructionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    auto *Call = dyn_cast<CallBase>(InstructionValue);
    if (!Call)
      return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
    auto CallingConvention =
        decodeCallingConvention(Value.UnsignedValue);
    if (!CallingConvention) {
      consumeError(CallingConvention.takeError());
      return instructionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    }
    Call->setCallingConv(*CallingConvention);
    break;
  }
  case NEVERC_IR_PROPERTY_TAIL_CALL_KIND: {
    if (!requireKind(Value, NEVERC_IR_PROPERTY_VALUE_ENUM))
      return instructionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    auto *Call = dyn_cast<CallInst>(InstructionValue);
    if (!Call)
      return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
    switch (Value.UnsignedValue) {
    case NEVERC_IR_TAIL_CALL_NONE:
      Call->setTailCallKind(CallInst::TCK_None);
      break;
    case NEVERC_IR_TAIL_CALL_TAIL:
      Call->setTailCallKind(CallInst::TCK_Tail);
      break;
    case NEVERC_IR_TAIL_CALL_MUST_TAIL:
      Call->setTailCallKind(CallInst::TCK_MustTail);
      break;
    case NEVERC_IR_TAIL_CALL_NO_TAIL:
      Call->setTailCallKind(CallInst::TCK_NoTail);
      break;
    default:
      return instructionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    }
    break;
  }
  case NEVERC_IR_PROPERTY_WEAK: {
    if (!requireKind(Value, NEVERC_IR_PROPERTY_VALUE_BOOL) ||
        Value.UnsignedValue > NEVERC_TRUE)
      return instructionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    auto *CmpXchg = dyn_cast<AtomicCmpXchgInst>(InstructionValue);
    if (!CmpXchg)
      return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
    CmpXchg->setWeak(Value.UnsignedValue == NEVERC_TRUE);
    break;
  }
  case NEVERC_IR_PROPERTY_SUCCESS_ORDERING:
  case NEVERC_IR_PROPERTY_FAILURE_ORDERING: {
    if (!requireKind(Value, NEVERC_IR_PROPERTY_VALUE_ENUM))
      return instructionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    auto *CmpXchg = dyn_cast<AtomicCmpXchgInst>(InstructionValue);
    if (!CmpXchg)
      return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
    auto Ordering = decodeAtomicOrdering(Value.UnsignedValue);
    if (!Ordering) {
      consumeError(Ordering.takeError());
      return instructionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    }
    if (Property == NEVERC_IR_PROPERTY_FAILURE_ORDERING &&
        (*Ordering == AtomicOrdering::Release ||
         *Ordering == AtomicOrdering::AcquireRelease))
      return instructionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    if (Property == NEVERC_IR_PROPERTY_SUCCESS_ORDERING)
      CmpXchg->setSuccessOrdering(*Ordering);
    else
      CmpXchg->setFailureOrdering(*Ordering);
    break;
  }
  case NEVERC_IR_PROPERTY_INBOUNDS: {
    if (!requireKind(Value, NEVERC_IR_PROPERTY_VALUE_BOOL) ||
        Value.UnsignedValue > NEVERC_TRUE)
      return instructionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    auto *GEP = dyn_cast<GetElementPtrInst>(InstructionValue);
    if (!GEP)
      return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
    GEP->setIsInBounds(Value.UnsignedValue == NEVERC_TRUE);
    break;
  }
  case NEVERC_IR_PROPERTY_SOURCE_ELEMENT_TYPE:
  case NEVERC_IR_PROPERTY_ALLOCATED_TYPE: {
    if (!requireKind(Value, NEVERC_IR_PROPERTY_VALUE_TYPE))
      return instructionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    Type *ResolvedType = nullptr;
    Status = resolveType(Value.TypeValue, &ResolvedType);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (Property == NEVERC_IR_PROPERTY_SOURCE_ELEMENT_TYPE) {
      auto *GEP = dyn_cast<GetElementPtrInst>(InstructionValue);
      if (!GEP)
        return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
      GEP->setSourceElementType(ResolvedType);
    } else {
      auto *Alloca = dyn_cast<AllocaInst>(InstructionValue);
      if (!Alloca)
        return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
      if (!ResolvedType->isSized())
        return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
      Alloca->setAllocatedType(ResolvedType);
    }
    break;
  }
  case NEVERC_IR_PROPERTY_CLEANUP: {
    if (!requireKind(Value, NEVERC_IR_PROPERTY_VALUE_BOOL) ||
        Value.UnsignedValue > NEVERC_TRUE)
      return instructionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    auto *LandingPad = dyn_cast<LandingPadInst>(InstructionValue);
    if (!LandingPad)
      return instructionStatus(NEVERC_STATUS_WRONG_TYPE);
    LandingPad->setCleanup(Value.UnsignedValue == NEVERC_TRUE);
    break;
  }
  case NEVERC_IR_PROPERTY_INDICES:
  case NEVERC_IR_PROPERTY_ATTRIBUTES:
    return instructionStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  default:
    return instructionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  }

  noteMutation();
  return neverc_status_ok();
}

} // namespace neverc::plugin
