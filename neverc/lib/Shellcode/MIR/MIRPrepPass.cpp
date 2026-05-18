#include "neverc/Shellcode/MIR/MIRPrepPass.h"
#include "ExtractorCommon.h"
#include "MIRRewriteRegistry.h"
#include "neverc/Shellcode/Pipeline/Diagnostics.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Type.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

namespace neverc {
namespace shellcode {
namespace {

constexpr llvm::StringLiteral kDiagnosticPrefix = Diagnostics::MIRPrefix;

bool isShellcodeStripPseudo(unsigned Opc) {
  switch (Opc) {
#define NEVERC_MIR_STRIP_PSEUDO(name, category) case TargetOpcode::name:
#include "neverc/Shellcode/Tables/MIRStripPseudoOpcodes.def"
#include "neverc/Shellcode/Tables/UserExtra_MIRStripPseudoOpcodes.def"
#undef NEVERC_MIR_STRIP_PSEUDO
    return true;
  default:
    return false;
  }
}

bool isSEHPseudoByMnemonic(const MachineInstr &MI) {
  unsigned Opc = MI.getOpcode();
  const MachineFunction *MF = MI.getParent()->getParent();
  if (!MF)
    return false;
  const TargetInstrInfo *TII = MF->getSubtarget().getInstrInfo();
  if (!TII)
    return false;
  StringRef Name = TII->getName(Opc);
  return Name.starts_with("SEH_");
}

std::string describeConstantForDiag(const Constant *C) {
  if (!C)
    return "<null>";
  std::string S;
  raw_string_ostream OS(S);
  if (auto *CI = dyn_cast<ConstantInt>(C)) {
    SmallString<40> HexStr;
    CI->getValue().toString(HexStr, /*Radix=*/16, /*Signed=*/false);
    OS << "i" << CI->getType()->getIntegerBitWidth() << " 0x" << HexStr;
  } else if (auto *CFP = dyn_cast<ConstantFP>(C)) {
    OS << "fp";
    if (CFP->getType()->isFloatTy())
      OS << "32";
    else if (CFP->getType()->isDoubleTy())
      OS << "64";
    else if (CFP->getType()->isHalfTy())
      OS << "16";
    else
      OS << "?";
  } else if (auto *CDS = dyn_cast<ConstantDataSequential>(C)) {
    OS << "seq<" << CDS->getNumElements() << " x " << *CDS->getElementType()
       << ">";
  } else if (isa<ConstantAggregateZero>(C)) {
    OS << "zeroinitializer";
  } else if (auto *CV = dyn_cast<ConstantVector>(C)) {
    OS << "vec<" << CV->getType()->getNumElements() << " x "
       << *CV->getType()->getElementType() << ">";
  } else if (auto *CA = dyn_cast<ConstantArray>(C)) {
    OS << "arr<" << CA->getType()->getNumElements() << " x "
       << *CA->getType()->getElementType() << ">";
  } else if (isa<ConstantStruct>(C)) {
    OS << "struct";
  } else {
    OS << "constant(" << *C->getType() << ")";
  }
  OS.flush();
  return S;
}

bool hasFeatureToken(StringRef Features, StringRef Tok) {
  size_t Pos = Features.find(Tok);
  while (Pos != StringRef::npos) {
    bool LeftOK = (Pos == 0 || Features[Pos - 1] == ',');
    size_t End = Pos + Tok.size();
    bool RightOK = (End == Features.size() || Features[End] == ',');
    if (LeftOK && RightOK)
      return true;
    Pos = Features.find(Tok, Pos + 1);
  }
  return false;
}

bool functionHasGeneralRegsOnly(const MachineFunction &MF) {
  const Triple &TT = MF.getTarget().getTargetTriple();
  if (TT.getArch() != Triple::aarch64)
    return false;
  const Function &F = MF.getFunction();
  if (!F.hasFnAttribute("target-features"))
    return false;
  StringRef Features = F.getFnAttribute("target-features").getValueAsString();
  return hasFeatureToken(Features, "+general-regs-only") ||
         hasFeatureToken(Features, "-fp-armv8");
}

bool looksLikeInlineAsmTemplate(StringRef Name) {
  if (Name == "syscall")
    return true;
  if (Name.starts_with("svc "))
    return true;
  if (Name.contains(' ') || Name.contains('#') || Name.contains('\t') ||
      Name.contains('\n'))
    return true;
  return false;
}

unsigned auditExternalReferences(MachineFunction &MF,
                                 const TargetDesc &Target) {
  StringSet<> Reported;
  StringRef FnName = MF.getName();
  ExternalSymbolHintContext HintContext;
  HintContext.FunctionHasGeneralRegsOnly = functionHasGeneralRegsOnly(MF);
  for (const MachineBasicBlock &MBB : MF) {
    for (const MachineInstr &MI : MBB) {
      for (const MachineOperand &MO : MI.operands()) {
        StringRef Name;
        const char *Kind = "";
        if (MO.isGlobal()) {
          const GlobalValue *GV = MO.getGlobal();
          if (!GV || !GV->isDeclaration())
            continue;
          if (isa<Function>(GV))
            Kind = "external function";
          else if (isa<GlobalVariable>(GV))
            Kind = "external global";
          else
            Kind = "external value";
          Name = GV->getName();
        } else if (MO.isSymbol()) {
          Name = MO.getSymbolName();
          Kind = "external asm symbol";
        } else {
          continue;
        }
        if (Name.empty() || isShellcodeInternalRuntimeName(Name) ||
            looksLikeInlineAsmTemplate(Name))
          continue;
        if (!Reported.insert(Name).second)
          continue;
        std::string Hint = getExternalSymbolHint(Name, Target, HintContext);
        errs() << kDiagnosticPrefix << "function '" << FnName
               << "' still references " << Kind << " '" << Name << "'";
        if (!Hint.empty())
          errs() << " -- " << Hint;
        errs() << "\n";
      }
    }
  }
  return Reported.size();
}

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
#include "neverc/Shellcode/Tables/MIRRewritePatterns.def"
#include "neverc/Shellcode/Tables/UserExtra_MIRRewritePatterns.def"
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

unsigned auditConstantPool(MachineFunction &MF) {
  MachineConstantPool *CP = MF.getConstantPool();
  if (!CP)
    return 0;
  const auto &Entries = CP->getConstants();
  if (Entries.empty())
    return 0;

  SmallSet<unsigned, 8> LiveCPIs;
  for (const MachineBasicBlock &MBB : MF) {
    for (const MachineInstr &MI : MBB) {
      for (const MachineOperand &MO : MI.operands()) {
        if (MO.isCPI())
          LiveCPIs.insert(MO.getIndex());
      }
    }
  }
  if (LiveCPIs.empty())
    return 0;

  StringRef FnName = MF.getName();
  for (unsigned Idx : LiveCPIs) {
    if (Idx >= Entries.size())
      continue;
    const auto &E = Entries[Idx];
    const Constant *C =
        E.isMachineConstantPoolEntry() ? nullptr : E.Val.ConstVal;
    errs() << kDiagnosticPrefix << "function '" << FnName
           << "' still references constant pool entry #" << Idx << " ("
           << describeConstantForDiag(C) << ", align=" << E.getAlign().value()
           << "); Data2TextPass should have stackified or inlined it."
           << " Falling through to the extractor, which will reject the "
              "resulting data section.\n";
  }
  return LiveCPIs.size();
}

class MIRPrepPass final : public MachineFunctionPass {
public:
  static char ID;

  explicit MIRPrepPass(const ShellcodeOptions &Opts)
      : MachineFunctionPass(ID), Enabled(Opts.Enabled), Target(Opts.Target) {}

  StringRef getPassName() const override { return "NeverC Shellcode MIR Prep"; }

  bool runOnMachineFunction(MachineFunction &MF) override {
    if (!Enabled)
      return false;

    const bool CheckSEH = MF.getTarget().getTargetTriple().isOSWindows();

    bool Changed = false;
    for (MachineBasicBlock &MBB : MF) {
      for (auto It = MBB.begin(), End = MBB.end(); It != End;) {
        MachineInstr &MI = *It++;
        if (isShellcodeStripPseudo(MI.getOpcode()) ||
            (CheckSEH && isSEHPseudoByMnemonic(MI))) {
          MI.eraseFromParent();
          Changed = true;
        }
      }
    }

    Changed |= runMIRRewrites(MF);

    (void)auditConstantPool(MF);

    (void)auditExternalReferences(MF, Target);

    return Changed;
  }

private:
  bool Enabled = false;
  TargetDesc Target;
};

} // namespace

char MIRPrepPass::ID = 0;

FunctionPass *createShellcodeMIRPrepPass(const ShellcodeOptions &Opts) {
  return new MIRPrepPass(Opts);
}

} // namespace shellcode
} // namespace neverc
