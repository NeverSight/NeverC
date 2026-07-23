// Volume 6 task 9: the builtin dyncode MIR transform provider.
//
// This is the mutating half of the old monolithic MIRPrepPass.  It runs at the
// PreEmit machine-pipeline hook as the default provider for the
// neverc.dyncode.mir.prepare transition (OBSERVABLE | INTERCEPTABLE |
// REPLACEABLE): it strips dyncode/SEH pseudos and applies the target-specific
// constant-pool rewrites that keep the emitted code position-independent.  The
// pass instance only holds the immutable "enabled" bit captured from the frozen
// DynCodeOptions; it never consults process-global current options.  The audit
// and reject logic moved to the sealed DynCodeMIRVerifier gate (Final hook).

#include "neverc/DynCode/MIR/MIRPrepPass.h"
#include "MIR/MIRPseudoClassify.h"
#include "MIR/MIRRewriteRegistry.h"
#include "neverc/DynCode/Pipeline/Diagnostics.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

namespace neverc {
namespace dyncode {
namespace {

constexpr llvm::StringLiteral kDiagnosticPrefix = Diagnostics::MIRPrefix;

bool encodeAArch64FmovImm32(const APInt &Bits, uint8_t &OutImm8) {
  uint32_t B = static_cast<uint32_t>(Bits.getZExtValue());
  uint32_t Sign = (B >> 24) & 0x80;
  uint32_t ExpBiased = (B >> 23) & 0xff;
  uint32_t Mantissa = B & 0x7fffff;
  if ((Mantissa & 0x7ffff) != 0)
    return false;
  if (ExpBiased < 124 || ExpBiased > 131)
    return false;
  OutImm8 = static_cast<uint8_t>(Sign | ((ExpBiased & 7) << 4) |
                                 ((Mantissa >> 19) & 0xF));
  return true;
}

bool encodeAArch64FmovImm64(const APInt &Bits, uint8_t &OutImm8) {
  uint64_t B = Bits.getZExtValue();
  uint32_t Sign = static_cast<uint32_t>((B >> 56) & 0x80);
  uint32_t ExpBiased = static_cast<uint32_t>((B >> 52) & 0x7ff);
  uint64_t Mantissa = B & 0xfffffffffffffULL;
  if ((Mantissa & 0xffffffffffffULL) != 0)
    return false;
  if (ExpBiased < 1020 || ExpBiased > 1027)
    return false;
  OutImm8 = static_cast<uint8_t>(Sign | ((ExpBiased & 7) << 4) |
                                 static_cast<uint32_t>((Mantissa >> 48) & 0xF));
  return true;
}

bool isPhysRegLiveAfter(MachineInstr &MI, MCRegister Reg,
                        const TargetRegisterInfo &TRI) {
  MachineBasicBlock &MBB = *MI.getParent();
  LivePhysRegs LPR(TRI);
  LPR.addLiveOuts(MBB);
  for (auto It = MBB.rbegin(); It != MBB.rend(); ++It) {
    if (&*It == &MI)
      break;
    LPR.stepBackward(*It);
  }
  return LPR.contains(Reg);
}

bool isPositiveZeroFP(const ConstantFP &CFP) {
  return CFP.getValueAPF().bitcastToAPInt().isZero();
}

unsigned tryRewriteAArch64CPIAsFmovImm(MachineFunction &MF) {
  if (MF.getTarget().getTargetTriple().getArch() != Triple::aarch64)
    return 0;
  MachineConstantPool *CP = MF.getConstantPool();
  if (!CP)
    return 0;
  const auto &Entries = CP->getConstants();
  if (Entries.empty())
    return 0;
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  if (!TII || !TRI)
    return 0;

  unsigned LDRSuiOpc =
      lookupMIRRewriteOpcode(*TII, "AArch64CPIAsFmovImm", "LDRSui");
  unsigned LDRDuiOpc =
      lookupMIRRewriteOpcode(*TII, "AArch64CPIAsFmovImm", "LDRDui");
  unsigned FMOVSiOpc =
      lookupMIRRewriteOpcode(*TII, "AArch64CPIAsFmovImm", "FMOVSi");
  unsigned FMOVDiOpc =
      lookupMIRRewriteOpcode(*TII, "AArch64CPIAsFmovImm", "FMOVDi");
  unsigned ADRPOpc =
      lookupMIRRewriteOpcode(*TII, "AArch64CPIAsFmovImm", "ADRP");
  if (!LDRSuiOpc || !LDRDuiOpc || !FMOVSiOpc || !FMOVDiOpc || !ADRPOpc)
    return 0;

  unsigned Rewrote = 0;
  SmallVector<MachineInstr *, 8> EraseList;

  for (MachineBasicBlock &MBB : MF) {
    for (auto It = MBB.begin(), End = MBB.end(); It != End; /*++It inside*/) {
      MachineInstr &MI = *It++;
      unsigned Opc = MI.getOpcode();
      bool IsS = (Opc == LDRSuiOpc);
      bool IsD = (Opc == LDRDuiOpc);
      if (!IsS && !IsD)
        continue;

      int CPIIdx = -1;
      for (const MachineOperand &MO : MI.operands()) {
        if (MO.isCPI()) {
          CPIIdx = MO.getIndex();
          break;
        }
      }
      if (CPIIdx < 0 || static_cast<unsigned>(CPIIdx) >= Entries.size())
        continue;
      const auto &E = Entries[CPIIdx];
      if (E.isMachineConstantPoolEntry())
        continue;
      auto *CFP = dyn_cast_or_null<ConstantFP>(E.Val.ConstVal);
      if (!CFP)
        continue;

      APInt Bits = CFP->getValueAPF().bitcastToAPInt();
      uint8_t Imm8 = 0;
      bool Enc = IsS ? encodeAArch64FmovImm32(Bits, Imm8)
                     : encodeAArch64FmovImm64(Bits, Imm8);
      if (!Enc)
        continue;

      if (MI.getNumOperands() < 2 || !MI.getOperand(0).isReg() ||
          !MI.getOperand(1).isReg())
        continue;
      Register Dst = MI.getOperand(0).getReg();
      Register Base = MI.getOperand(1).getReg();
      if (!Dst.isPhysical() || !Base.isPhysical())
        continue;

      MachineInstr *ADRP = MI.getPrevNode();
      if (!ADRP || ADRP->getOpcode() != ADRPOpc || ADRP->getNumOperands() < 1 ||
          !ADRP->getOperand(0).isReg() || ADRP->getOperand(0).getReg() != Base)
        continue;

      if (isPhysRegLiveAfter(MI, MCRegister(Base.asMCReg()), *TRI))
        continue;

      BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(IsS ? FMOVSiOpc : FMOVDiOpc),
              Dst)
          .addImm(Imm8);

      EraseList.push_back(&MI);
      EraseList.push_back(ADRP);
      ++Rewrote;
    }
  }

  for (MachineInstr *MI : EraseList)
    MI->eraseFromParent();

  return Rewrote;
}

unsigned tryRewriteX86ZeroFPCPIAsXorps(MachineFunction &MF) {
  if (MF.getTarget().getTargetTriple().getArch() != Triple::x86_64)
    return 0;
  MachineConstantPool *CP = MF.getConstantPool();
  if (!CP)
    return 0;
  const auto &Entries = CP->getConstants();
  if (Entries.empty())
    return 0;
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  if (!TII)
    return 0;

  unsigned MOVSSrm =
      lookupMIRRewriteOpcode(*TII, "X86ZeroFPCPIAsXorps", "MOVSSrm");
  unsigned MOVSDrm =
      lookupMIRRewriteOpcode(*TII, "X86ZeroFPCPIAsXorps", "MOVSDrm");
  unsigned FsFLD0SS =
      lookupMIRRewriteOpcode(*TII, "X86ZeroFPCPIAsXorps", "FsFLD0SS");
  unsigned FsFLD0SD =
      lookupMIRRewriteOpcode(*TII, "X86ZeroFPCPIAsXorps", "FsFLD0SD");
  if ((!MOVSSrm && !MOVSDrm) || (!FsFLD0SS && !FsFLD0SD))
    return 0;

  unsigned Rewrote = 0;
  SmallVector<MachineInstr *, 8> EraseList;

  for (MachineBasicBlock &MBB : MF) {
    for (auto It = MBB.begin(), End = MBB.end(); It != End;) {
      MachineInstr &MI = *It++;
      unsigned Opc = MI.getOpcode();
      bool IsSS = (MOVSSrm && Opc == MOVSSrm);
      bool IsSD = (MOVSDrm && Opc == MOVSDrm);
      if (!IsSS && !IsSD)
        continue;
      if (IsSS && !FsFLD0SS)
        continue;
      if (IsSD && !FsFLD0SD)
        continue;

      int CPIIdx = -1;
      for (const MachineOperand &MO : MI.operands()) {
        if (MO.isCPI()) {
          CPIIdx = MO.getIndex();
          break;
        }
      }
      if (CPIIdx < 0 || static_cast<unsigned>(CPIIdx) >= Entries.size())
        continue;
      const auto &E = Entries[CPIIdx];
      if (E.isMachineConstantPoolEntry())
        continue;
      auto *CFP = dyn_cast_or_null<ConstantFP>(E.Val.ConstVal);
      if (!CFP || !isPositiveZeroFP(*CFP))
        continue;

      if (MI.getNumOperands() < 1 || !MI.getOperand(0).isReg())
        continue;
      Register Dst = MI.getOperand(0).getReg();
      if (!Dst.isPhysical())
        continue;

      BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(IsSS ? FsFLD0SS : FsFLD0SD),
              Dst);
      EraseList.push_back(&MI);
      ++Rewrote;
    }
  }

  for (MachineInstr *MI : EraseList)
    MI->eraseFromParent();
  return Rewrote;
}

struct MIRRewritePattern {
  StringRef Name;
  Triple::ArchType Arch;
  unsigned (*Apply)(MachineFunction &MF);
};

#define NEVERC_MIR_REWRITE_ARCH_any Triple::UnknownArch
#define NEVERC_MIR_REWRITE_ARCH_aarch64 Triple::aarch64
#define NEVERC_MIR_REWRITE_ARCH_x86_64 Triple::x86_64
constexpr MIRRewritePattern kMIRRewritePatterns[] = {
#define NEVERC_MIR_REWRITE_PATTERN(id, display, arch, function)                \
  {display, NEVERC_MIR_REWRITE_ARCH_##arch, function},
#include "neverc/DynCode/Tables/MIRRewritePatterns.def"
#include "neverc/DynCode/Tables/UserExtra_MIRRewritePatterns.def"
#undef NEVERC_MIR_REWRITE_PATTERN
};
#undef NEVERC_MIR_REWRITE_ARCH_any
#undef NEVERC_MIR_REWRITE_ARCH_aarch64
#undef NEVERC_MIR_REWRITE_ARCH_x86_64

bool runMIRRewrites(MachineFunction &MF) {
  bool Changed = false;
  Triple::ArchType Arch = MF.getTarget().getTargetTriple().getArch();
  for (const MIRRewritePattern &P : kMIRRewritePatterns) {
    if (P.Arch != Triple::UnknownArch && P.Arch != Arch)
      continue;
    unsigned N = P.Apply(MF);
    if (!N)
      continue;
    Changed = true;
    errs() << kDiagnosticPrefix << "pattern '" << P.Name << "' rewrote " << N
           << " site(s) in '" << MF.getName()
           << "' (MIR-level safety net after Data2TextPass).\n";
  }
  return Changed;
}

/// Builtin provider for neverc.dyncode.mir.prepare: strips dyncode/SEH pseudos
/// and applies the target constant-pool rewrites.  Holds only the immutable
/// "enabled" bit; a replacement provider can substitute this whole transform.
class DynCodeMIRTransformPass final : public MachineFunctionPass {
public:
  static char ID;

  explicit DynCodeMIRTransformPass(const DynCodeOptions &Opts)
      : MachineFunctionPass(ID), Enabled(Opts.Enabled) {}

  StringRef getPassName() const override {
    return "NeverC DynCode MIR Transform";
  }

  bool runOnMachineFunction(MachineFunction &MF) override {
    if (!Enabled)
      return false;

    const bool CheckSEH = MF.getTarget().getTargetTriple().isOSWindows();

    bool Changed = false;
    for (MachineBasicBlock &MBB : MF) {
      for (auto It = MBB.begin(), End = MBB.end(); It != End;) {
        MachineInstr &MI = *It++;
        if (isDynCodeStripPseudo(MI.getOpcode()) ||
            (CheckSEH && isSEHPseudoByMnemonic(MI))) {
          MI.eraseFromParent();
          Changed = true;
        }
      }
    }

    Changed |= runMIRRewrites(MF);
    return Changed;
  }

private:
  bool Enabled = false;
};

} // namespace

char DynCodeMIRTransformPass::ID = 0;

FunctionPass *createDynCodeMIRTransformPass(const DynCodeOptions &Opts) {
  return new DynCodeMIRTransformPass(Opts);
}

} // namespace dyncode
} // namespace neverc
