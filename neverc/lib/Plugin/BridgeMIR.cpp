#include "BridgeCastHelpers.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"

using namespace llvm;

namespace neverc {
namespace plugin {

static uint8_t machineOperandKindOf(const MachineOperand &Op) {
  switch (Op.getType()) {
  case MachineOperand::MO_Register:
    return NEVERC_MIR_OP_REG;
  case MachineOperand::MO_Immediate:
    return NEVERC_MIR_OP_IMM;
  case MachineOperand::MO_CImmediate:
    return NEVERC_MIR_OP_IMM;
  case MachineOperand::MO_FPImmediate:
    return NEVERC_MIR_OP_FPIMM;
  case MachineOperand::MO_MachineBasicBlock:
    return NEVERC_MIR_OP_MBB;
  case MachineOperand::MO_FrameIndex:
    return NEVERC_MIR_OP_FRAMEIDX;
  case MachineOperand::MO_GlobalAddress:
    return NEVERC_MIR_OP_GLOBAL;
  case MachineOperand::MO_ExternalSymbol:
    return NEVERC_MIR_OP_EXTSYM;
  case MachineOperand::MO_Metadata:
    return NEVERC_MIR_OP_METADATA;
  case MachineOperand::MO_RegisterMask:
  case MachineOperand::MO_RegisterLiveOut:
    return NEVERC_MIR_OP_REGMASK;
  case MachineOperand::MO_BlockAddress:
    return NEVERC_MIR_OP_BLOCKADDR;
  default:
    return NEVERC_MIR_OP_OTHER;
  }
}

// ===----------------------------------------------------------------------===
//  MIR ops
// ===----------------------------------------------------------------------===

static NevercMachineBBRef bridgeMFuncGetFirstBB(NevercMachineFuncRef MF) {
  if (LLVM_UNLIKELY(!MF))
    return nullptr;
  auto *Func = unwrapMF(MF);
  if (LLVM_UNLIKELY(Func->empty()))
    return nullptr;
  return wrapMBB(&Func->front());
}

static NevercMachineBBRef bridgeMFuncGetNextBB(NevercMachineBBRef MBB) {
  if (LLVM_UNLIKELY(!MBB))
    return nullptr;
  auto *Block = unwrapMBB(MBB);
  if (LLVM_UNLIKELY(!Block->getParent()))
    return nullptr;
  auto It = std::next(Block->getIterator());
  if (It == Block->getParent()->end())
    return nullptr;
  return wrapMBB(&*It);
}

static NevercMachineInstrRef bridgeMBBGetFirstInst(NevercMachineBBRef MBB) {
  if (LLVM_UNLIKELY(!MBB))
    return nullptr;
  auto *Block = unwrapMBB(MBB);
  if (LLVM_UNLIKELY(Block->empty()))
    return nullptr;
  return wrapMI(&Block->front());
}

static NevercMachineInstrRef bridgeMBBGetNextInst(NevercMachineInstrRef MI) {
  if (LLVM_UNLIKELY(!MI))
    return nullptr;
  auto *Inst = unwrapMI(MI);
  if (LLVM_UNLIKELY(!Inst->getParent()))
    return nullptr;
  auto It = std::next(Inst->getIterator());
  if (It == Inst->getParent()->end())
    return nullptr;
  return wrapMI(&*It);
}

static unsigned bridgeMInstGetOpcode(NevercMachineInstrRef MI) {
  if (LLVM_UNLIKELY(!MI))
    return 0;
  return unwrapMI(MI)->getOpcode();
}

static unsigned bridgeMInstGetNumOperands(NevercMachineInstrRef MI) {
  if (LLVM_UNLIKELY(!MI))
    return 0;
  return unwrapMI(MI)->getNumOperands();
}

static void bridgeMInstEraseFromParent(NevercMachineInstrRef MI) {
  if (LLVM_UNLIKELY(!MI))
    return;
  unwrapMI(MI)->eraseFromParent();
}

// ===----------------------------------------------------------------------===
//  MIR name & operand access
// ===----------------------------------------------------------------------===

static const char *bridgeMFuncGetName(NevercMachineFuncRef MF) {
  if (LLVM_UNLIKELY(!MF))
    return "";
  return nameStr(unwrapMF(MF)->getName().data());
}

static int64_t bridgeMInstGetOperandImm(NevercMachineInstrRef MI,
                                        unsigned Idx) {
  if (LLVM_UNLIKELY(!MI))
    return 0;
  auto *Inst = unwrapMI(MI);
  if (Idx >= Inst->getNumOperands() || !Inst->getOperand(Idx).isImm())
    return 0;
  return Inst->getOperand(Idx).getImm();
}

static int bridgeMInstGetOperandIsReg(NevercMachineInstrRef MI, unsigned Idx) {
  if (LLVM_UNLIKELY(!MI))
    return 0;
  auto *Inst = unwrapMI(MI);
  if (Idx >= Inst->getNumOperands())
    return 0;
  return Inst->getOperand(Idx).isReg();
}

static unsigned bridgeMInstGetOperandReg(NevercMachineInstrRef MI,
                                         unsigned Idx) {
  if (LLVM_UNLIKELY(!MI))
    return 0;
  auto *Inst = unwrapMI(MI);
  if (Idx >= Inst->getNumOperands() || !Inst->getOperand(Idx).isReg())
    return 0;
  return Inst->getOperand(Idx).getReg().id();
}

static int bridgeMInstGetOperandIsImm(NevercMachineInstrRef MI, unsigned Idx) {
  if (LLVM_UNLIKELY(!MI))
    return 0;
  auto *Inst = unwrapMI(MI);
  if (Idx >= Inst->getNumOperands())
    return 0;
  return Inst->getOperand(Idx).isImm();
}

static unsigned bridgeMInstCollectOperandKinds(NevercMachineInstrRef MI,
                                               uint8_t *OutKinds) {
  if (LLVM_UNLIKELY(!MI || !OutKinds))
    return 0;
  auto *Inst = unwrapMI(MI);
  unsigned N = Inst->getNumOperands();
  for (unsigned I = 0; I < N; ++I)
    OutKinds[I] = machineOperandKindOf(Inst->getOperand(I));
  return N;
}

// ===----------------------------------------------------------------------===
//  MBB navigation
// ===----------------------------------------------------------------------===

static unsigned bridgeMBBGetNumber(NevercMachineBBRef MBB) {
  if (LLVM_UNLIKELY(!MBB))
    return 0;
  return unwrapMBB(MBB)->getNumber();
}

static unsigned bridgeMBBGetSuccCount(NevercMachineBBRef MBB) {
  if (LLVM_UNLIKELY(!MBB))
    return 0;
  return unwrapMBB(MBB)->succ_size();
}

static unsigned bridgeMBBGetPredCount(NevercMachineBBRef MBB) {
  if (LLVM_UNLIKELY(!MBB))
    return 0;
  return unwrapMBB(MBB)->pred_size();
}

// ===----------------------------------------------------------------------===
//  MIR BasicBlock navigation by index
// ===----------------------------------------------------------------------===

static NevercMachineBBRef bridgeMBBGetSuccessor(NevercMachineBBRef MBB,
                                                unsigned Idx) {
  if (LLVM_UNLIKELY(!MBB))
    return nullptr;
  auto *Block = unwrapMBB(MBB);
  if (Idx >= Block->succ_size())
    return nullptr;
  auto It = Block->succ_begin();
  std::advance(It, Idx);
  return wrapMBB(*It);
}

static NevercMachineBBRef bridgeMBBGetPredecessor(NevercMachineBBRef MBB,
                                                  unsigned Idx) {
  if (LLVM_UNLIKELY(!MBB))
    return nullptr;
  auto *Block = unwrapMBB(MBB);
  if (Idx >= Block->pred_size())
    return nullptr;
  auto It = Block->pred_begin();
  std::advance(It, Idx);
  return wrapMBB(*It);
}

static void bridgeMFuncCollectBBs(NevercMachineFuncRef MF,
                                  NevercMachineBBRef *Out) {
  if (LLVM_UNLIKELY(!MF || !Out))
    return;
  unsigned Idx = 0;
  for (auto &MBB : *unwrapMF(MF))
    Out[Idx++] = wrapMBB(&MBB);
}

static void bridgeMBBCollectInstructions(NevercMachineBBRef MBB,
                                         NevercMachineInstrRef *Out) {
  if (LLVM_UNLIKELY(!MBB || !Out))
    return;
  unsigned Idx = 0;
  for (auto &MI : *unwrapMBB(MBB))
    Out[Idx++] = wrapMI(&MI);
}

// ===----------------------------------------------------------------------===
//  MIR extended navigation
// ===----------------------------------------------------------------------===

static NevercMachineInstrRef bridgeMBBGetLastInst(NevercMachineBBRef MBB) {
  if (LLVM_UNLIKELY(!MBB))
    return nullptr;
  auto *BB = unwrapMBB(MBB);
  if (LLVM_UNLIKELY(BB->empty()))
    return nullptr;
  return wrapMI(&BB->back());
}

static NevercMachineInstrRef bridgeMBBGetPrevInst(NevercMachineInstrRef MI) {
  if (LLVM_UNLIKELY(!MI))
    return nullptr;
  auto *Inst = unwrapMI(MI);
  auto *MBB = Inst->getParent();
  if (LLVM_UNLIKELY(!MBB))
    return nullptr;
  auto It = Inst->getIterator();
  if (LLVM_UNLIKELY(It == MBB->begin()))
    return nullptr;
  return wrapMI(&*std::prev(It));
}

static NevercMachineBBRef bridgeMFuncGetLastBB(NevercMachineFuncRef MF) {
  if (LLVM_UNLIKELY(!MF))
    return nullptr;
  auto *Func = unwrapMF(MF);
  if (LLVM_UNLIKELY(Func->empty()))
    return nullptr;
  return wrapMBB(&Func->back());
}

static NevercMachineBBRef bridgeMFuncGetPrevBB(NevercMachineBBRef MBB) {
  if (LLVM_UNLIKELY(!MBB))
    return nullptr;
  auto *BB = unwrapMBB(MBB);
  auto *MF = BB->getParent();
  if (LLVM_UNLIKELY(!MF))
    return nullptr;
  auto It = BB->getIterator();
  if (It == MF->begin())
    return nullptr;
  return wrapMBB(&*std::prev(It));
}

// ===----------------------------------------------------------------------===
//  MIR operand mutation
// ===----------------------------------------------------------------------===

static void bridgeMInstSetOperandReg(NevercMachineInstrRef MI, unsigned Idx,
                                     unsigned Reg) {
  if (LLVM_UNLIKELY(!MI))
    return;
  auto *Inst = unwrapMI(MI);
  if (Idx < Inst->getNumOperands() && Inst->getOperand(Idx).isReg())
    Inst->getOperand(Idx).setReg(Register(Reg));
}

static void bridgeMInstSetOperandImm(NevercMachineInstrRef MI, unsigned Idx,
                                     int64_t Val) {
  if (LLVM_UNLIKELY(!MI))
    return;
  auto *Inst = unwrapMI(MI);
  if (Idx < Inst->getNumOperands() && Inst->getOperand(Idx).isImm())
    Inst->getOperand(Idx).setImm(Val);
}

// ===----------------------------------------------------------------------===
//  MIR instruction flags & properties
// ===----------------------------------------------------------------------===

static unsigned bridgeMInstGetFlags(NevercMachineInstrRef MI) {
  if (LLVM_UNLIKELY(!MI))
    return 0;
  return unwrapMI(MI)->getFlags();
}

static void bridgeMInstSetFlags(NevercMachineInstrRef MI, unsigned Flags) {
  if (LLVM_UNLIKELY(!MI))
    return;
  unwrapMI(MI)->setFlags(Flags);
}

static int bridgeMInstIsBranch(NevercMachineInstrRef MI) {
  if (LLVM_UNLIKELY(!MI))
    return 0;
  return unwrapMI(MI)->isBranch();
}

static int bridgeMInstIsCall(NevercMachineInstrRef MI) {
  if (LLVM_UNLIKELY(!MI))
    return 0;
  return unwrapMI(MI)->isCall();
}

static int bridgeMInstIsReturn(NevercMachineInstrRef MI) {
  if (LLVM_UNLIKELY(!MI))
    return 0;
  return unwrapMI(MI)->isReturn();
}

static int bridgeMInstIsTerminator(NevercMachineInstrRef MI) {
  if (LLVM_UNLIKELY(!MI))
    return 0;
  return unwrapMI(MI)->isTerminator();
}

static int bridgeMInstIsMoveImmediate(NevercMachineInstrRef MI) {
  if (LLVM_UNLIKELY(!MI))
    return 0;
  return unwrapMI(MI)->isMoveImmediate();
}

static int bridgeMInstHasDelaySlot(NevercMachineInstrRef MI) {
  if (LLVM_UNLIKELY(!MI))
    return 0;
  return unwrapMI(MI)->hasDelaySlot();
}

static const char *bridgeMInstGetDesc(NevercMachineInstrRef MI) {
  if (LLVM_UNLIKELY(!MI))
    return "";
  auto *Inst = unwrapMI(MI);
  auto *MBB = Inst->getParent();
  if (LLVM_UNLIKELY(!MBB))
    return "";
  auto *MF = MBB->getParent();
  if (LLVM_UNLIKELY(!MF))
    return "";
  const auto *TII = MF->getSubtarget().getInstrInfo();
  if (LLVM_UNLIKELY(!TII))
    return "";
  return TII->getName(Inst->getOpcode()).data();
}

// ===----------------------------------------------------------------------===
//  MIR register queries
// ===----------------------------------------------------------------------===

static int bridgeMInstOperandIsVirtReg(NevercMachineInstrRef MI, unsigned Idx) {
  if (LLVM_UNLIKELY(!MI))
    return 0;
  auto *Inst = unwrapMI(MI);
  if (Idx >= Inst->getNumOperands() || !Inst->getOperand(Idx).isReg())
    return 0;
  return Inst->getOperand(Idx).getReg().isVirtual();
}

static int bridgeMInstOperandIsPhysReg(NevercMachineInstrRef MI, unsigned Idx) {
  if (LLVM_UNLIKELY(!MI))
    return 0;
  auto *Inst = unwrapMI(MI);
  if (Idx >= Inst->getNumOperands() || !Inst->getOperand(Idx).isReg())
    return 0;
  return Inst->getOperand(Idx).getReg().isPhysical();
}

static int bridgeMInstOperandIsDef(NevercMachineInstrRef MI, unsigned Idx) {
  if (LLVM_UNLIKELY(!MI))
    return 0;
  auto *Inst = unwrapMI(MI);
  if (Idx >= Inst->getNumOperands() || !Inst->getOperand(Idx).isReg())
    return 0;
  return Inst->getOperand(Idx).isDef();
}

static int bridgeMInstOperandIsUse(NevercMachineInstrRef MI, unsigned Idx) {
  if (LLVM_UNLIKELY(!MI))
    return 0;
  auto *Inst = unwrapMI(MI);
  if (Idx >= Inst->getNumOperands() || !Inst->getOperand(Idx).isReg())
    return 0;
  return Inst->getOperand(Idx).isUse();
}

// ===----------------------------------------------------------------------===
//  MIR instruction movement & counting
// ===----------------------------------------------------------------------===

static void bridgeMInstMoveBefore(NevercMachineInstrRef MI,
                                  NevercMachineInstrRef Before) {
  if (LLVM_UNLIKELY(!MI || !Before))
    return;
  auto *Inst = unwrapMI(MI);
  auto *BeforeInst = unwrapMI(Before);
  auto *DstMBB = BeforeInst->getParent();
  auto *SrcMBB = Inst->getParent();
  if (DstMBB && SrcMBB)
    DstMBB->splice(BeforeInst->getIterator(), SrcMBB, Inst->getIterator());
}

static unsigned bridgeMBBGetInstCount(NevercMachineBBRef MBB) {
  if (LLVM_UNLIKELY(!MBB))
    return 0;
  return static_cast<unsigned>(unwrapMBB(MBB)->size());
}

static unsigned bridgeMFuncGetBBCount(NevercMachineFuncRef MF) {
  if (LLVM_UNLIKELY(!MF))
    return 0;
  return static_cast<unsigned>(unwrapMF(MF)->size());
}

// ===----------------------------------------------------------------------===
//  MIR callback iteration
//  Same pattern as the IR ForEach family.  One vtable call replaces N
//  MFuncGetNextBB / MBBGetNextInst vtable calls.
// ===----------------------------------------------------------------------===

static void bridgeMFuncForEachBB(
    NevercMachineFuncRef MF,
    int (*Fn)(NevercMachineBBRef MBB, void *Ctx), void *Ctx) {
  if (LLVM_UNLIKELY(!MF || !Fn))
    return;
  for (auto &MBB : *unwrapMF(MF))
    if (Fn(wrapMBB(&MBB), Ctx) != 0)
      return;
}

static void bridgeMBBForEachInst(
    NevercMachineBBRef MBB,
    int (*Fn)(NevercMachineInstrRef MI, void *Ctx), void *Ctx) {
  if (LLVM_UNLIKELY(!MBB || !Fn))
    return;
  for (auto &MI : *unwrapMBB(MBB))
    if (Fn(wrapMI(&MI), Ctx) != 0)
      return;
}

static NevercMachineBBRef *
bridgeArenaCollectMBBs(NevercArenaRef Arena, NevercMachineFuncRef MF,
                       unsigned *OutCount) {
  if (OutCount)
    *OutCount = 0;
  if (LLVM_UNLIKELY(!Arena || !MF || !OutCount))
    return nullptr;
  size_t RawCount = unwrapMF(MF)->size();
  if (LLVM_UNLIKELY(RawCount == 0 || RawCount > UINT_MAX ||
                    RawCount > SIZE_MAX / sizeof(NevercMachineBBRef)))
    return nullptr;
  auto *Buf = static_cast<NevercMachineBBRef *>(
      unwrapArena(Arena)->Alloc.Allocate(
          RawCount * sizeof(NevercMachineBBRef),
          alignof(NevercMachineBBRef)));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  unsigned Idx = 0;
  for (auto &MBB : *unwrapMF(MF))
    Buf[Idx++] = wrapMBB(&MBB);
  *OutCount = Idx;
  return Buf;
}

void populateMIRBridge(NevercHostAPI &API) {
  API.MFuncGetFirstBB = bridgeMFuncGetFirstBB;
  API.MFuncGetNextBB = bridgeMFuncGetNextBB;
  API.MBBGetFirstInst = bridgeMBBGetFirstInst;
  API.MBBGetNextInst = bridgeMBBGetNextInst;
  API.MInstGetOpcode = bridgeMInstGetOpcode;
  API.MInstGetNumOperands = bridgeMInstGetNumOperands;
  API.MInstEraseFromParent = bridgeMInstEraseFromParent;

  API.MFuncGetName = bridgeMFuncGetName;
  API.MInstGetOperandImm = bridgeMInstGetOperandImm;
  API.MInstGetOperandIsReg = bridgeMInstGetOperandIsReg;
  API.MInstGetOperandReg = bridgeMInstGetOperandReg;
  API.MInstGetOperandIsImm = bridgeMInstGetOperandIsImm;

  API.MBBGetNumber = bridgeMBBGetNumber;
  API.MBBGetSuccCount = bridgeMBBGetSuccCount;
  API.MBBGetPredCount = bridgeMBBGetPredCount;
  API.MBBGetSuccessor = bridgeMBBGetSuccessor;
  API.MBBGetPredecessor = bridgeMBBGetPredecessor;

  API.MBBGetLastInst = bridgeMBBGetLastInst;
  API.MBBGetPrevInst = bridgeMBBGetPrevInst;
  API.MFuncGetLastBB = bridgeMFuncGetLastBB;
  API.MFuncGetPrevBB = bridgeMFuncGetPrevBB;

  API.MInstSetOperandReg = bridgeMInstSetOperandReg;
  API.MInstSetOperandImm = bridgeMInstSetOperandImm;
  API.MInstGetFlags = bridgeMInstGetFlags;
  API.MInstSetFlags = bridgeMInstSetFlags;
  API.MInstIsBranch = bridgeMInstIsBranch;
  API.MInstIsCall = bridgeMInstIsCall;
  API.MInstIsReturn = bridgeMInstIsReturn;
  API.MInstIsTerminator = bridgeMInstIsTerminator;
  API.MInstIsMoveImmediate = bridgeMInstIsMoveImmediate;
  API.MInstHasDelaySlot = bridgeMInstHasDelaySlot;
  API.MInstGetDesc = bridgeMInstGetDesc;

  API.MInstOperandIsVirtReg = bridgeMInstOperandIsVirtReg;
  API.MInstOperandIsPhysReg = bridgeMInstOperandIsPhysReg;
  API.MInstOperandIsDef = bridgeMInstOperandIsDef;
  API.MInstOperandIsUse = bridgeMInstOperandIsUse;

  API.MInstMoveBefore = bridgeMInstMoveBefore;
  API.MBBGetInstCount = bridgeMBBGetInstCount;
  API.MFuncGetBBCount = bridgeMFuncGetBBCount;

  API.MFuncCollectBBs = bridgeMFuncCollectBBs;
  API.MBBCollectInstructions = bridgeMBBCollectInstructions;
  API.MInstCollectOperandKinds = bridgeMInstCollectOperandKinds;

  API.MFuncForEachBB = bridgeMFuncForEachBB;
  API.MBBForEachInst = bridgeMBBForEachInst;

  API.ArenaCollectMBBs = bridgeArenaCollectMBBs;
}

} // namespace plugin
} // namespace neverc
