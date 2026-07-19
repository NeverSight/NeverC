#include "MIRBridgeInternal.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/MC/MCSymbol.h"
#include <algorithm>
#include <limits>

using namespace llvm;

namespace neverc::plugin {
namespace {

#define NEVERC_MIR_BRIDGE_OR_RETURN()                                          \
  NevercStatus BridgeStatus;                                                   \
  MIRPluginBridge *Bridge = getMIRBridge(Context, Task, &BridgeStatus);        \
  if (!Bridge)                                                                 \
  return BridgeStatus

bool validOperandValue(const NevercMIROperandValue *Value) {
  return Value && Value->Header.StructSize >= sizeof(*Value) &&
         Value->Header.Major == NEVERC_MIR_API_MAJOR;
}

NevercMIRRegisterFlags registerFlags(const MachineOperand &Operand) {
  NevercMIRRegisterFlags Flags = 0;
  if (Operand.isDef())
    Flags |= NEVERC_MIR_REG_FLAG_DEF;
  if (Operand.isImplicit())
    Flags |= NEVERC_MIR_REG_FLAG_IMPLICIT;
  if (Operand.isKill())
    Flags |= NEVERC_MIR_REG_FLAG_KILL;
  if (Operand.isDead())
    Flags |= NEVERC_MIR_REG_FLAG_DEAD;
  if (Operand.isUndef())
    Flags |= NEVERC_MIR_REG_FLAG_UNDEF;
  if (Operand.isEarlyClobber())
    Flags |= NEVERC_MIR_REG_FLAG_EARLY_CLOBBER;
  if (Operand.isRenamable())
    Flags |= NEVERC_MIR_REG_FLAG_RENAMABLE;
  if (Operand.isInternalRead())
    Flags |= NEVERC_MIR_REG_FLAG_INTERNAL_READ;
  if (Operand.isDebug())
    Flags |= NEVERC_MIR_REG_FLAG_DEBUG;
  return Flags;
}

uint32_t floatSemantics(const ConstantFP &Constant) {
  Type *TypeValue = Constant.getType();
  if (TypeValue->isHalfTy())
    return NEVERC_MIR_FLOAT_SEMANTICS_IEEE_HALF;
  if (TypeValue->isBFloatTy())
    return NEVERC_MIR_FLOAT_SEMANTICS_BFLOAT;
  if (TypeValue->isFloatTy())
    return NEVERC_MIR_FLOAT_SEMANTICS_IEEE_SINGLE;
  if (TypeValue->isDoubleTy())
    return NEVERC_MIR_FLOAT_SEMANTICS_IEEE_DOUBLE;
  if (TypeValue->isX86_FP80Ty())
    return NEVERC_MIR_FLOAT_SEMANTICS_X87_DOUBLE_EXTENDED;
  if (TypeValue->isFP128Ty())
    return NEVERC_MIR_FLOAT_SEMANTICS_IEEE_QUAD;
  if (TypeValue->isPPC_FP128Ty())
    return NEVERC_MIR_FLOAT_SEMANTICS_PPC_DOUBLE_DOUBLE;
  return 0;
}

const fltSemantics *llvmFloatSemantics(uint32_t Semantics) {
  switch (Semantics) {
  case NEVERC_MIR_FLOAT_SEMANTICS_IEEE_HALF:
    return &APFloat::IEEEhalf();
  case NEVERC_MIR_FLOAT_SEMANTICS_BFLOAT:
    return &APFloat::BFloat();
  case NEVERC_MIR_FLOAT_SEMANTICS_IEEE_SINGLE:
    return &APFloat::IEEEsingle();
  case NEVERC_MIR_FLOAT_SEMANTICS_IEEE_DOUBLE:
    return &APFloat::IEEEdouble();
  case NEVERC_MIR_FLOAT_SEMANTICS_X87_DOUBLE_EXTENDED:
    return &APFloat::x87DoubleExtended();
  case NEVERC_MIR_FLOAT_SEMANTICS_IEEE_QUAD:
    return &APFloat::IEEEquad();
  case NEVERC_MIR_FLOAT_SEMANTICS_PPC_DOUBLE_DOUBLE:
    return &APFloat::PPCDoubleDouble();
  default:
    return nullptr;
  }
}

bool validWords(const NevercMIRWordView &Words) {
  if (Words.BitWidth == 0 || Words.Count != (Words.BitWidth + 63) / 64 ||
      Words.Count > std::numeric_limits<size_t>::max())
    return false;
  return Words.Data || Words.Count == 0;
}

NevercStatus writeReference(MIRPluginBridge &Bridge, const void *Reference,
                            NevercMIROperandKind Kind,
                            NevercMIRReferenceHandle *OutHandle) {
  auto Handle = Bridge.wrapReference(Reference, Kind);
  if (!Handle) {
    consumeError(Handle.takeError());
    return mirStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutHandle = *Handle;
  return neverc_status_ok();
}

NevercStatus getValue(MIRPluginBridge &Bridge, MachineOperand &Operand,
                      NevercMIROperandValue *OutValue) {
  if (!validOperandValue(OutValue))
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  NevercMIROperandValue Result{};
  Result.Header = {sizeof(Result), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR,
                   0};
  Result.Kind = stableMIROperandKind(Operand.getType());
  if (Result.Kind == 0)
    return mirStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  if (!Operand.isReg())
    Result.TargetFlags = Operand.getTargetFlags();

  switch (Operand.getType()) {
  case MachineOperand::MO_Register: {
    Register Reg = Operand.getReg();
    Result.Payload.Register.Number = Reg.id();
    Result.Payload.Register.SubRegister = Operand.getSubReg();
    Result.Payload.Register.Flags = registerFlags(Operand);
    Result.Payload.Register.IsPhysical =
        Reg.isPhysical() ? NEVERC_TRUE : NEVERC_FALSE;
    Result.Payload.Register.RequiresTargetSchema =
        Reg.isPhysical() ? NEVERC_TRUE : NEVERC_FALSE;
    break;
  }
  case MachineOperand::MO_Immediate:
    Result.Payload.Immediate = Operand.getImm();
    break;
  case MachineOperand::MO_CImmediate: {
    const APInt &Value = Operand.getCImm()->getValue();
    ArrayRef<uint64_t> Words(Value.getRawData(), Value.getNumWords());
    ArrayRef<uint64_t> Stored = Bridge.setScratchWords(Words);
    Result.Payload.Constant = {Stored.data(), Stored.size(),
                               Value.getBitWidth(), 0};
    break;
  }
  case MachineOperand::MO_FPImmediate: {
    APInt Value = Operand.getFPImm()->getValueAPF().bitcastToAPInt();
    ArrayRef<uint64_t> Words(Value.getRawData(), Value.getNumWords());
    ArrayRef<uint64_t> Stored = Bridge.setScratchWords(Words);
    Result.Payload.Constant = {Stored.data(), Stored.size(),
                               Value.getBitWidth(),
                               floatSemantics(*Operand.getFPImm())};
    break;
  }
  case MachineOperand::MO_MachineBasicBlock: {
    auto Handle = Bridge.wrapBasicBlock(*Operand.getMBB());
    if (!Handle) {
      consumeError(Handle.takeError());
      return mirStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    Result.Payload.BasicBlock = *Handle;
    break;
  }
  case MachineOperand::MO_FrameIndex:
  case MachineOperand::MO_JumpTableIndex:
    Result.Payload.IndexOffset = {Operand.getIndex(), 0, 0};
    break;
  case MachineOperand::MO_ConstantPoolIndex:
  case MachineOperand::MO_TargetIndex:
    Result.Payload.IndexOffset = {Operand.getIndex(), 0, Operand.getOffset()};
    break;
  case MachineOperand::MO_ExternalSymbol: {
    StringRef Symbol = Operand.getSymbolName();
    Result.Payload.SymbolOffset.Symbol = {Symbol.data(), Symbol.size()};
    Result.Payload.SymbolOffset.Offset = Operand.getOffset();
    break;
  }
  case MachineOperand::MO_GlobalAddress:
    if (NevercStatus Status =
            writeReference(Bridge, Operand.getGlobal(), Result.Kind,
                           &Result.Payload.ReferenceOffset.Reference);
        Status.Code != NEVERC_STATUS_OK)
      return Status;
    Result.Payload.ReferenceOffset.Offset = Operand.getOffset();
    break;
  case MachineOperand::MO_BlockAddress:
    if (NevercStatus Status =
            writeReference(Bridge, Operand.getBlockAddress(), Result.Kind,
                           &Result.Payload.ReferenceOffset.Reference);
        Status.Code != NEVERC_STATUS_OK)
      return Status;
    Result.Payload.ReferenceOffset.Offset = Operand.getOffset();
    break;
  case MachineOperand::MO_RegisterMask:
  case MachineOperand::MO_RegisterLiveOut: {
    const TargetRegisterInfo *TRI =
        Operand.getParent()->getMF()->getSubtarget().getRegisterInfo();
    uint64_t Count = MachineOperand::getRegMaskSize(TRI->getNumRegs());
    const uint32_t *Data =
        Operand.isRegMask() ? Operand.getRegMask() : Operand.getRegLiveOut();
    Result.Payload.RegisterMask = {Data, Count};
    break;
  }
  case MachineOperand::MO_Metadata:
    if (NevercStatus Status =
            writeReference(Bridge, Operand.getMetadata(), Result.Kind,
                           &Result.Payload.Reference);
        Status.Code != NEVERC_STATUS_OK)
      return Status;
    break;
  case MachineOperand::MO_MCSymbol:
    if (NevercStatus Status =
            writeReference(Bridge, Operand.getMCSymbol(), Result.Kind,
                           &Result.Payload.ReferenceOffset.Reference);
        Status.Code != NEVERC_STATUS_OK)
      return Status;
    Result.Payload.ReferenceOffset.Offset = Operand.getOffset();
    break;
  case MachineOperand::MO_CFIIndex:
    Result.Payload.UnsignedValue = Operand.getCFIIndex();
    break;
  case MachineOperand::MO_IntrinsicID:
    Result.Payload.UnsignedValue = Operand.getIntrinsicID();
    break;
  case MachineOperand::MO_Predicate:
    Result.Payload.UnsignedValue = Operand.getPredicate();
    break;
  case MachineOperand::MO_ShuffleMask: {
    ArrayRef<int> Mask = Operand.getShuffleMask();
    static_assert(sizeof(int) == sizeof(int32_t));
    Result.Payload.ShuffleMask = {
        reinterpret_cast<const int32_t *>(Mask.data()), Mask.size()};
    break;
  }
  case MachineOperand::MO_DbgInstrRef:
    Result.Payload.DebugInstructionReference = {Operand.getInstrRefInstrIndex(),
                                                Operand.getInstrRefOpIndex()};
    break;
  }
  *OutValue = Result;
  return neverc_status_ok();
}

NevercStatus setRegisterValue(MachineOperand &Operand,
                              const NevercMIRRegisterValue &Value) {
  constexpr NevercMIRRegisterFlags KnownFlags =
      NEVERC_MIR_REG_FLAG_DEF | NEVERC_MIR_REG_FLAG_IMPLICIT |
      NEVERC_MIR_REG_FLAG_KILL | NEVERC_MIR_REG_FLAG_DEAD |
      NEVERC_MIR_REG_FLAG_UNDEF | NEVERC_MIR_REG_FLAG_EARLY_CLOBBER |
      NEVERC_MIR_REG_FLAG_RENAMABLE | NEVERC_MIR_REG_FLAG_INTERNAL_READ |
      NEVERC_MIR_REG_FLAG_DEBUG;
  if ((Value.Flags & ~KnownFlags) != 0 ||
      (Value.IsPhysical != NEVERC_FALSE && Value.IsPhysical != NEVERC_TRUE) ||
      (Value.RequiresTargetSchema != NEVERC_FALSE &&
       Value.RequiresTargetSchema != NEVERC_TRUE))
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  if (Value.RequiresTargetSchema != Value.IsPhysical)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  bool IsDef = (Value.Flags & NEVERC_MIR_REG_FLAG_DEF) != 0;
  bool IsKill = (Value.Flags & NEVERC_MIR_REG_FLAG_KILL) != 0;
  bool IsDead = (Value.Flags & NEVERC_MIR_REG_FLAG_DEAD) != 0;
  bool IsEarlyClobber = (Value.Flags & NEVERC_MIR_REG_FLAG_EARLY_CLOBBER) != 0;
  bool IsDebug = (Value.Flags & NEVERC_MIR_REG_FLAG_DEBUG) != 0;
  bool IsRenamable = (Value.Flags & NEVERC_MIR_REG_FLAG_RENAMABLE) != 0;
  if ((IsDef && IsKill) || (!IsDef && IsDead) || (!IsDef && IsEarlyClobber) ||
      (IsDef && IsDebug) || Value.SubRegister > 0xfff)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Register NewRegister(Value.Number);
  if ((NewRegister.isPhysical() ? NEVERC_TRUE : NEVERC_FALSE) !=
      Value.IsPhysical)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  if (!NewRegister.isPhysical() && IsRenamable)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  if (NewRegister.isPhysical() && Operand.getReg().id() != NewRegister.id())
    return mirStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  Operand.setReg(NewRegister);
  Operand.setSubReg(Value.SubRegister);
  if (Operand.isDef() && !IsDef) {
    Operand.setIsDead(false);
    Operand.setIsEarlyClobber(false);
  } else if (Operand.isUse() && IsDef) {
    Operand.setIsKill(false);
    Operand.setIsDebug(false);
  }
  Operand.setIsDef(IsDef);
  Operand.setImplicit((Value.Flags & NEVERC_MIR_REG_FLAG_IMPLICIT) != 0);
  if (IsDef) {
    Operand.setIsDead(IsDead);
    Operand.setIsEarlyClobber(IsEarlyClobber);
  } else {
    Operand.setIsKill(IsKill);
    Operand.setIsDebug(IsDebug);
  }
  Operand.setIsUndef((Value.Flags & NEVERC_MIR_REG_FLAG_UNDEF) != 0);
  if (NewRegister.isPhysical())
    Operand.setIsRenamable(IsRenamable);
  Operand.setIsInternalRead((Value.Flags & NEVERC_MIR_REG_FLAG_INTERNAL_READ) !=
                            0);
  return neverc_status_ok();
}

NevercStatus setValue(MIRPluginBridge &Bridge, MachineOperand &Operand,
                      const NevercMIROperandValue &Value) {
  NevercMIROperandKind Expected = stableMIROperandKind(Operand.getType());
  if (Value.Kind != Expected)
    return mirStatus(NEVERC_STATUS_WRONG_TYPE);
  if (Operand.isReg()) {
    if (Value.TargetFlags != 0)
      return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    if (Value.Payload.Register.IsPhysical == NEVERC_TRUE &&
        !Bridge.targetSchemaEnabled())
      return mirStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
    return setRegisterValue(Operand, Value.Payload.Register);
  }
  if (Value.TargetFlags > 0xfff)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);

  switch (Operand.getType()) {
  case MachineOperand::MO_Register:
    llvm_unreachable("register handled above");
  case MachineOperand::MO_Immediate:
    Operand.setImm(Value.Payload.Immediate);
    break;
  case MachineOperand::MO_CImmediate: {
    const NevercMIRWordView &Words = Value.Payload.Constant;
    if (!validWords(Words) || Words.Semantics != 0)
      return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    APInt Integer(Words.BitWidth, ArrayRef<uint64_t>(Words.Data, Words.Count));
    Operand.setCImm(ConstantInt::get(
        Operand.getParent()->getMF()->getFunction().getContext(), Integer));
    break;
  }
  case MachineOperand::MO_FPImmediate: {
    const NevercMIRWordView &Words = Value.Payload.Constant;
    const fltSemantics *Semantics = llvmFloatSemantics(Words.Semantics);
    if (!validWords(Words) || !Semantics)
      return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    APInt Bits(Words.BitWidth, ArrayRef<uint64_t>(Words.Data, Words.Count));
    APFloat Float(*Semantics, Bits);
    Operand.setFPImm(ConstantFP::get(
        Operand.getParent()->getMF()->getFunction().getContext(), Float));
    break;
  }
  case MachineOperand::MO_MachineBasicBlock: {
    MachineBasicBlock *Block = nullptr;
    NevercStatus Status =
        Bridge.resolveBasicBlock(Value.Payload.BasicBlock, &Block);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    Operand.setMBB(Block);
    break;
  }
  case MachineOperand::MO_FrameIndex:
    if (Value.Payload.IndexOffset.Offset != 0)
      return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    Operand.setIndex(Value.Payload.IndexOffset.Index);
    break;
  case MachineOperand::MO_JumpTableIndex:
    if (Value.Payload.IndexOffset.Index < 0 ||
        Value.Payload.IndexOffset.Offset != 0)
      return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    Operand.setIndex(Value.Payload.IndexOffset.Index);
    break;
  case MachineOperand::MO_ConstantPoolIndex:
  case MachineOperand::MO_TargetIndex:
    if (Value.Payload.IndexOffset.Index < 0)
      return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    Operand.setIndex(Value.Payload.IndexOffset.Index);
    Operand.setOffset(Value.Payload.IndexOffset.Offset);
    break;
  case MachineOperand::MO_ExternalSymbol: {
    NevercStringView Symbol = Value.Payload.SymbolOffset.Symbol;
    if ((!Symbol.Data && Symbol.Length != 0) ||
        Symbol.Length > std::numeric_limits<size_t>::max())
      return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    StringRef Name(Symbol.Data ? Symbol.Data : "",
                   static_cast<size_t>(Symbol.Length));
    if (Name.contains('\0'))
      return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    Operand.ChangeToES(Bridge.ownString(Name), Value.TargetFlags);
    Operand.setOffset(Value.Payload.SymbolOffset.Offset);
    break;
  }
  case MachineOperand::MO_GlobalAddress: {
    const void *Reference = nullptr;
    NevercStatus Status = Bridge.resolveReference(
        Value.Payload.ReferenceOffset.Reference, Value.Kind, &Reference);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    Operand.ChangeToGA(static_cast<const GlobalValue *>(Reference),
                       Value.Payload.ReferenceOffset.Offset, Value.TargetFlags);
    break;
  }
  case MachineOperand::MO_BlockAddress: {
    const void *Reference = nullptr;
    NevercStatus Status = Bridge.resolveReference(
        Value.Payload.ReferenceOffset.Reference, Value.Kind, &Reference);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (Reference != Operand.getBlockAddress())
      return mirStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
    Operand.setOffset(Value.Payload.ReferenceOffset.Offset);
    break;
  }
  case MachineOperand::MO_RegisterMask: {
    NevercMIRRegisterMaskView Mask = Value.Payload.RegisterMask;
    if ((!Mask.Data && Mask.Count != 0) ||
        Mask.Count > std::numeric_limits<size_t>::max())
      return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    ArrayRef<uint32_t> Stored = Bridge.ownRegisterMask(
        ArrayRef<uint32_t>(Mask.Data, static_cast<size_t>(Mask.Count)));
    Operand.setRegMask(Stored.data());
    break;
  }
  case MachineOperand::MO_RegisterLiveOut: {
    NevercMIRRegisterMaskView Mask = Value.Payload.RegisterMask;
    const TargetRegisterInfo *TRI =
        Operand.getParent()->getMF()->getSubtarget().getRegisterInfo();
    size_t Count = MachineOperand::getRegMaskSize(TRI->getNumRegs());
    if (Mask.Count != Count || !Mask.Data ||
        !std::equal(Mask.Data, Mask.Data + Count, Operand.getRegLiveOut()))
      return mirStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
    break;
  }
  case MachineOperand::MO_Metadata: {
    const void *Reference = nullptr;
    NevercStatus Status = Bridge.resolveReference(Value.Payload.Reference,
                                                  Value.Kind, &Reference);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    Operand.setMetadata(static_cast<const MDNode *>(Reference));
    break;
  }
  case MachineOperand::MO_MCSymbol: {
    const void *Reference = nullptr;
    NevercStatus Status = Bridge.resolveReference(
        Value.Payload.ReferenceOffset.Reference, Value.Kind, &Reference);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    Operand.ChangeToMCSymbol(
        const_cast<MCSymbol *>(static_cast<const MCSymbol *>(Reference)),
        Value.TargetFlags);
    Operand.setOffset(Value.Payload.ReferenceOffset.Offset);
    break;
  }
  case MachineOperand::MO_CFIIndex:
    if (Value.Payload.UnsignedValue != Operand.getCFIIndex())
      return mirStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
    break;
  case MachineOperand::MO_IntrinsicID:
    Operand.setIntrinsicID(
        static_cast<Intrinsic::ID>(Value.Payload.UnsignedValue));
    break;
  case MachineOperand::MO_Predicate:
    Operand.setPredicate(Value.Payload.UnsignedValue);
    break;
  case MachineOperand::MO_ShuffleMask: {
    NevercMIRShuffleMaskView Mask = Value.Payload.ShuffleMask;
    ArrayRef<int> Existing = Operand.getShuffleMask();
    if (Mask.Count != Existing.size() || (Mask.Count != 0 && !Mask.Data) ||
        (Mask.Count != 0 &&
         !std::equal(Mask.Data, Mask.Data + Mask.Count, Existing.begin())))
      return mirStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
    break;
  }
  case MachineOperand::MO_DbgInstrRef:
    Operand.setInstrRefInstrIndex(
        Value.Payload.DebugInstructionReference.InstructionIndex);
    Operand.setInstrRefOpIndex(
        Value.Payload.DebugInstructionReference.OperandIndex);
    break;
  }
  if (!Operand.isReg() && Operand.getTargetFlags() != Value.TargetFlags)
    Operand.setTargetFlags(Value.TargetFlags);
  return neverc_status_ok();
}

} // namespace

Expected<MachineOperand> createMIROperand(MIRPluginBridge &Bridge,
                                          MachineFunction &Function,
                                          const NevercMIROperandValue &Value) {
  if (Value.TargetFlags > 0xfff)
    return createStringError(inconvertibleErrorCode(),
                             "MIR operand target flags are out of range");

  switch (Value.Kind) {
  case NEVERC_MIR_OPERAND_REGISTER: {
    const NevercMIRRegisterValue &RegisterValue = Value.Payload.Register;
    Register RegisterNumber(RegisterValue.Number);
    if (RegisterValue.IsPhysical == NEVERC_TRUE &&
        !Bridge.targetSchemaEnabled())
      return createStringError(inconvertibleErrorCode(),
                               "physical MIR registers require target schema");
    MachineOperand Result = MachineOperand::CreateReg(RegisterNumber, false);
    NevercStatus Status = setRegisterValue(Result, RegisterValue);
    if (Status.Code != NEVERC_STATUS_OK)
      return createStringError(inconvertibleErrorCode(),
                               "invalid MIR register operand");
    return Result;
  }
  case NEVERC_MIR_OPERAND_IMMEDIATE:
    return MachineOperand::CreateImm(Value.Payload.Immediate);
  case NEVERC_MIR_OPERAND_C_IMMEDIATE: {
    const NevercMIRWordView &Words = Value.Payload.Constant;
    if (!validWords(Words) || Words.Semantics != 0)
      return createStringError(inconvertibleErrorCode(),
                               "invalid MIR integer constant words");
    APInt Integer(Words.BitWidth, ArrayRef<uint64_t>(Words.Data, Words.Count));
    return MachineOperand::CreateCImm(
        ConstantInt::get(Function.getFunction().getContext(), Integer));
  }
  case NEVERC_MIR_OPERAND_FP_IMMEDIATE: {
    const NevercMIRWordView &Words = Value.Payload.Constant;
    const fltSemantics *Semantics = llvmFloatSemantics(Words.Semantics);
    if (!validWords(Words) || !Semantics)
      return createStringError(inconvertibleErrorCode(),
                               "invalid MIR floating constant words");
    APInt Bits(Words.BitWidth, ArrayRef<uint64_t>(Words.Data, Words.Count));
    APFloat Float(*Semantics, Bits);
    return MachineOperand::CreateFPImm(
        ConstantFP::get(Function.getFunction().getContext(), Float));
  }
  case NEVERC_MIR_OPERAND_MACHINE_BASIC_BLOCK: {
    MachineBasicBlock *Block = nullptr;
    NevercStatus Status =
        Bridge.resolveBasicBlock(Value.Payload.BasicBlock, &Block);
    if (Status.Code != NEVERC_STATUS_OK)
      return createStringError(inconvertibleErrorCode(),
                               "invalid MIR basic block operand");
    return MachineOperand::CreateMBB(Block, Value.TargetFlags);
  }
  case NEVERC_MIR_OPERAND_FRAME_INDEX: {
    if (Value.Payload.IndexOffset.Offset != 0)
      return createStringError(inconvertibleErrorCode(),
                               "frame index operand cannot have an offset");
    MachineOperand Result =
        MachineOperand::CreateFI(Value.Payload.IndexOffset.Index);
    Result.setTargetFlags(Value.TargetFlags);
    return Result;
  }
  case NEVERC_MIR_OPERAND_CONSTANT_POOL_INDEX: {
    if (Value.Payload.IndexOffset.Index < 0)
      return createStringError(inconvertibleErrorCode(),
                               "constant pool index cannot be negative");
    MachineOperand Result = MachineOperand::CreateCPI(
        static_cast<unsigned>(Value.Payload.IndexOffset.Index), 0,
        Value.TargetFlags);
    Result.setOffset(Value.Payload.IndexOffset.Offset);
    return Result;
  }
  case NEVERC_MIR_OPERAND_TARGET_INDEX:
    if (Value.Payload.IndexOffset.Index < 0)
      return createStringError(inconvertibleErrorCode(),
                               "target index cannot be negative");
    return MachineOperand::CreateTargetIndex(
        static_cast<unsigned>(Value.Payload.IndexOffset.Index),
        Value.Payload.IndexOffset.Offset, Value.TargetFlags);
  case NEVERC_MIR_OPERAND_JUMP_TABLE_INDEX: {
    if (Value.Payload.IndexOffset.Index < 0 ||
        Value.Payload.IndexOffset.Offset != 0)
      return createStringError(inconvertibleErrorCode(),
                               "invalid jump table index operand");
    return MachineOperand::CreateJTI(
        static_cast<unsigned>(Value.Payload.IndexOffset.Index),
        Value.TargetFlags);
  }
  case NEVERC_MIR_OPERAND_EXTERNAL_SYMBOL: {
    NevercStringView Symbol = Value.Payload.SymbolOffset.Symbol;
    if ((!Symbol.Data && Symbol.Length != 0) ||
        Symbol.Length > std::numeric_limits<size_t>::max())
      return createStringError(inconvertibleErrorCode(),
                               "invalid MIR external symbol");
    StringRef Name(Symbol.Data ? Symbol.Data : "",
                   static_cast<size_t>(Symbol.Length));
    if (Name.contains('\0'))
      return createStringError(inconvertibleErrorCode(),
                               "MIR external symbol contains a null byte");
    MachineOperand Result =
        MachineOperand::CreateES(Bridge.ownString(Name), Value.TargetFlags);
    Result.setOffset(Value.Payload.SymbolOffset.Offset);
    return Result;
  }
  case NEVERC_MIR_OPERAND_GLOBAL_ADDRESS: {
    const void *Reference = nullptr;
    NevercStatus Status = Bridge.resolveReference(
        Value.Payload.ReferenceOffset.Reference, Value.Kind, &Reference);
    if (Status.Code != NEVERC_STATUS_OK)
      return createStringError(inconvertibleErrorCode(),
                               "invalid MIR global reference");
    return MachineOperand::CreateGA(static_cast<const GlobalValue *>(Reference),
                                    Value.Payload.ReferenceOffset.Offset,
                                    Value.TargetFlags);
  }
  case NEVERC_MIR_OPERAND_BLOCK_ADDRESS: {
    const void *Reference = nullptr;
    NevercStatus Status = Bridge.resolveReference(
        Value.Payload.ReferenceOffset.Reference, Value.Kind, &Reference);
    if (Status.Code != NEVERC_STATUS_OK)
      return createStringError(inconvertibleErrorCode(),
                               "invalid MIR block address reference");
    return MachineOperand::CreateBA(
        static_cast<const BlockAddress *>(Reference),
        Value.Payload.ReferenceOffset.Offset, Value.TargetFlags);
  }
  case NEVERC_MIR_OPERAND_REGISTER_MASK:
  case NEVERC_MIR_OPERAND_REGISTER_LIVE_OUT: {
    NevercMIRRegisterMaskView Mask = Value.Payload.RegisterMask;
    const TargetRegisterInfo *TRI = Function.getSubtarget().getRegisterInfo();
    size_t ExpectedCount = MachineOperand::getRegMaskSize(TRI->getNumRegs());
    if (!Mask.Data || Mask.Count != ExpectedCount)
      return createStringError(inconvertibleErrorCode(),
                               "invalid MIR register mask");
    ArrayRef<uint32_t> Stored = Bridge.ownRegisterMask(
        ArrayRef<uint32_t>(Mask.Data, static_cast<size_t>(Mask.Count)));
    MachineOperand Result =
        Value.Kind == NEVERC_MIR_OPERAND_REGISTER_MASK
            ? MachineOperand::CreateRegMask(Stored.data())
            : MachineOperand::CreateRegLiveOut(Stored.data());
    Result.setTargetFlags(Value.TargetFlags);
    return Result;
  }
  case NEVERC_MIR_OPERAND_METADATA: {
    const void *Reference = nullptr;
    NevercStatus Status = Bridge.resolveReference(Value.Payload.Reference,
                                                  Value.Kind, &Reference);
    if (Status.Code != NEVERC_STATUS_OK)
      return createStringError(inconvertibleErrorCode(),
                               "invalid MIR metadata reference");
    MachineOperand Result =
        MachineOperand::CreateMetadata(static_cast<const MDNode *>(Reference));
    Result.setTargetFlags(Value.TargetFlags);
    return Result;
  }
  case NEVERC_MIR_OPERAND_MC_SYMBOL: {
    const void *Reference = nullptr;
    NevercStatus Status = Bridge.resolveReference(
        Value.Payload.ReferenceOffset.Reference, Value.Kind, &Reference);
    if (Status.Code != NEVERC_STATUS_OK)
      return createStringError(inconvertibleErrorCode(),
                               "invalid MIR MC symbol reference");
    MachineOperand Result = MachineOperand::CreateMCSymbol(
        const_cast<MCSymbol *>(static_cast<const MCSymbol *>(Reference)),
        Value.TargetFlags);
    Result.setOffset(Value.Payload.ReferenceOffset.Offset);
    return Result;
  }
  case NEVERC_MIR_OPERAND_CFI_INDEX: {
    MachineOperand Result =
        MachineOperand::CreateCFIIndex(Value.Payload.UnsignedValue);
    Result.setTargetFlags(Value.TargetFlags);
    return Result;
  }
  case NEVERC_MIR_OPERAND_INTRINSIC_ID: {
    MachineOperand Result = MachineOperand::CreateIntrinsicID(
        static_cast<Intrinsic::ID>(Value.Payload.UnsignedValue));
    Result.setTargetFlags(Value.TargetFlags);
    return Result;
  }
  case NEVERC_MIR_OPERAND_PREDICATE: {
    MachineOperand Result =
        MachineOperand::CreatePredicate(Value.Payload.UnsignedValue);
    Result.setTargetFlags(Value.TargetFlags);
    return Result;
  }
  case NEVERC_MIR_OPERAND_SHUFFLE_MASK: {
    NevercMIRShuffleMaskView Mask = Value.Payload.ShuffleMask;
    if ((Mask.Count != 0 && !Mask.Data) ||
        Mask.Count > std::numeric_limits<size_t>::max())
      return createStringError(inconvertibleErrorCode(),
                               "invalid MIR shuffle mask");
    static_assert(sizeof(int) == sizeof(int32_t));
    ArrayRef<int> Stored = Bridge.ownShuffleMask(
        ArrayRef<int>(reinterpret_cast<const int *>(Mask.Data),
                      static_cast<size_t>(Mask.Count)));
    MachineOperand Result = MachineOperand::CreateShuffleMask(Stored);
    Result.setTargetFlags(Value.TargetFlags);
    return Result;
  }
  case NEVERC_MIR_OPERAND_DBG_INSTR_REF: {
    MachineOperand Result = MachineOperand::CreateDbgInstrRef(
        Value.Payload.DebugInstructionReference.InstructionIndex,
        Value.Payload.DebugInstructionReference.OperandIndex);
    Result.setTargetFlags(Value.TargetFlags);
    return Result;
  }
  default:
    return createStringError(inconvertibleErrorCode(),
                             "unknown MIR operand kind");
  }
}

NevercStatus NEVERC_CALL getMIROperandValue(void *Context,
                                            NevercTaskHandle Task,
                                            NevercMachineOperandHandle Operand,
                                            NevercMIROperandValue *OutValue) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  MachineOperand *Resolved = nullptr;
  NevercStatus Status = Bridge->resolveOperand(Operand, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return getValue(*Bridge, *Resolved, OutValue);
}

NevercStatus NEVERC_CALL setMIROperandValue(
    void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
    NevercMachineOperandHandle Operand, const NevercMIROperandValue *Value) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!validOperandValue(Value))
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  NevercStatus Status = Bridge->checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return mirStatus(NEVERC_STATUS_POLICY_VIOLATION);
  MachineOperand *Resolved = nullptr;
  Status = Bridge->resolveOperand(Operand, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  MachineInstr *Instruction = Resolved->getParent();
  unsigned Index = Resolved->getOperandNo();
  MachineOperand Original = *Resolved;
  Original.clearParent();
  Status = setValue(*Bridge, *Resolved, *Value);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Bridge->addMutationUndo(
      [Instruction, Index, Original = std::move(Original)]() mutable {
        if (!Instruction || Index >= Instruction->getNumOperands())
          return;
        Instruction->removeOperand(Index);
        Instruction->insert(Instruction->operands_begin() + Index,
                            ArrayRef<MachineOperand>(&Original, 1));
      });
  Bridge->invalidateMutationProperties();
  Bridge->noteMutation();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL getMIROperandInstruction(
    void *Context, NevercTaskHandle Task, NevercMachineOperandHandle Operand,
    NevercMachineInstrHandle *OutInstruction) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!OutInstruction)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutInstruction = {};
  MachineOperand *Resolved = nullptr;
  NevercStatus Status = Bridge->resolveOperand(Operand, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto Handle = Bridge->wrapInstruction(*Resolved->getParent());
  if (!Handle) {
    consumeError(Handle.takeError());
    return mirStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutInstruction = *Handle;
  return neverc_status_ok();
}

#undef NEVERC_MIR_BRIDGE_OR_RETURN

} // namespace neverc::plugin
