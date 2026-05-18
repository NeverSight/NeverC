//===-- X86MCTargetDesc.cpp - X86 Target Descriptions ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides X86 specific target descriptions.
//
//===----------------------------------------------------------------------===//

#include "X86MCTargetDesc.h"
#include "TargetInfo/X86TargetInfo.h"
#include "X86ATTInstPrinter.h"
#include "X86BaseInfo.h"
#include "X86IntelInstPrinter.h"
#include "X86MCAsmInfo.h"
#include "X86TargetStreamer.h"
#include "llvm/ADT/APInt.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MachineLocation.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

#define GET_REGINFO_MC_DESC
#include "X86GenRegisterInfo.inc"

#include "X86DarwinSdkConflictUndef.h"
#define GET_INSTRINFO_MC_DESC
#define GET_INSTRINFO_MC_HELPERS
#define ENABLE_INSTR_PREDICATE_VERIFIER
#include "X86GenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "X86GenSubtargetInfo.inc"

std::string X86_MC::ParseX86Triple(const Triple &TT) {
  (void)TT;
  return "+64bit-mode,+sse2";
}

unsigned X86_MC::getDwarfRegFlavour(const Triple &TT, bool isEH) {
  (void)TT;
  (void)isEH;
  return DWARFFlavour::X86_64;
}

bool X86_MC::hasLockPrefix(const MCInst &MI) {
  return MI.getFlags() & X86::IP_HAS_LOCK;
}

static bool isMemOperand(const MCInst &MI, unsigned Op, unsigned RegClassID) {
  const MCOperand &Base = MI.getOperand(Op + X86::AddrBaseReg);
  const MCOperand &Index = MI.getOperand(Op + X86::AddrIndexReg);
  const MCRegisterClass &RC = X86MCRegisterClasses[RegClassID];

  return (Base.isReg() && Base.getReg() != 0 && RC.contains(Base.getReg())) ||
         (Index.isReg() && Index.getReg() != 0 && RC.contains(Index.getReg()));
}

bool X86_MC::is32BitMemOperand(const MCInst &MI, unsigned Op) {
  const MCOperand &Base = MI.getOperand(Op + X86::AddrBaseReg);
  const MCOperand &Index = MI.getOperand(Op + X86::AddrIndexReg);
  if (Base.isReg() && Base.getReg() == X86::EIP) {
    assert(Index.isReg() && Index.getReg() == 0 && "Invalid eip-based address");
    return true;
  }
  if (Index.isReg() && Index.getReg() == X86::EIZ)
    return true;
  return isMemOperand(MI, Op, X86::GR32RegClassID);
}

#ifndef NDEBUG
bool X86_MC::is64BitMemOperand(const MCInst &MI, unsigned Op) {
  return isMemOperand(MI, Op, X86::GR64RegClassID);
}
#endif

bool X86_MC::needsAddressSizeOverride(const MCInst &MI,
                                      const MCSubtargetInfo &STI,
                                      int MemoryOperand, uint64_t TSFlags) {
  uint64_t AdSize = TSFlags & X86II::AdSizeMask;
  if (AdSize == X86II::AdSize32)
    return true;
  uint64_t Form = TSFlags & X86II::FormMask;
  switch (Form) {
  default:
    break;
  case X86II::RawFrmDstSrc: {
    unsigned siReg = MI.getOperand(1).getReg();
    assert(((siReg == X86::ESI && MI.getOperand(0).getReg() == X86::EDI) ||
            (siReg == X86::RSI && MI.getOperand(0).getReg() == X86::RDI)) &&
           "SI and DI register sizes do not match");
    return siReg == X86::ESI;
  }
  case X86II::RawFrmSrc:
    return MI.getOperand(0).getReg() == X86::ESI;
  case X86II::RawFrmDst:
    return MI.getOperand(0).getReg() == X86::EDI;
  }

  // Determine where the memory operand starts, if present.
  if (MemoryOperand < 0)
    return false;

  return is32BitMemOperand(MI, MemoryOperand);
}

void X86_MC::initLLVMToSEHAndCVRegMapping(MCRegisterInfo *MRI) {
  for (unsigned Reg = X86::NoRegister + 1; Reg < X86::NUM_TARGET_REGS; ++Reg) {
    unsigned SEH = MRI->getEncodingValue(Reg);
    MRI->mapLLVMRegToSEHReg(Reg, SEH);
  }
}

MCSubtargetInfo *X86_MC::createX86MCSubtargetInfo(const Triple &TT,
                                                  StringRef CPU, StringRef FS) {
  std::string ArchFS = X86_MC::ParseX86Triple(TT);
  assert(!ArchFS.empty() && "Failed to parse X86 triple");
  if (!FS.empty())
    ArchFS = (Twine(ArchFS) + "," + FS).str();

  if (CPU.empty())
    CPU = "generic";

  size_t posNoEVEX512 = FS.rfind("-evex512");
  // Make sure we won't be cheated by "-avx512fp16".
  size_t posNoAVX512F =
      FS.ends_with("-avx512f") ? FS.size() - 8 : FS.rfind("-avx512f,");
  size_t posEVEX512 = FS.rfind("+evex512");
  size_t posAVX512F = FS.rfind("+avx512"); // Any AVX512XXX will enable AVX512F.

  if (posAVX512F != StringRef::npos &&
      (posNoAVX512F == StringRef::npos || posNoAVX512F < posAVX512F))
    if (posEVEX512 == StringRef::npos && posNoEVEX512 == StringRef::npos)
      ArchFS += ",+evex512";

  return createX86MCSubtargetInfoImpl(TT, CPU, /*TuneCPU*/ CPU, ArchFS);
}

static MCInstrInfo *createX86MCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitX86MCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createX86MCRegisterInfo(const Triple &TT) {
  unsigned RA = X86::RIP; // Should have dwarf #16.

  MCRegisterInfo *X = new MCRegisterInfo();
  InitX86MCRegisterInfo(X, RA, X86_MC::getDwarfRegFlavour(TT, false),
                        X86_MC::getDwarfRegFlavour(TT, true), RA);
  X86_MC::initLLVMToSEHAndCVRegMapping(X);
  return X;
}

static MCAsmInfo *createX86MCAsmInfo(const MCRegisterInfo &MRI,
                                     const Triple &TheTriple,
                                     const MCTargetOptions &Options) {
  MCAsmInfo *MAI;
  if (TheTriple.isOSBinFormatMachO()) {
    MAI = new X86_64MCAsmInfoDarwin(TheTriple);
  } else if (TheTriple.isOSBinFormatELF()) {
    // Force the use of an ELF container.
    MAI = new X86ELFMCAsmInfo(TheTriple);
  } else if (TheTriple.isWindowsMSVCEnvironment()) {
    if (Options.getAssemblyLanguage().equals_insensitive("masm"))
      MAI = new X86MCAsmInfoMicrosoftMASM(TheTriple);
    else
      MAI = new X86MCAsmInfoMicrosoft(TheTriple);
  } else if (TheTriple.isOSCygMing() ||
             TheTriple.isWindowsItaniumEnvironment()) {
    MAI = new X86MCAsmInfoGNUCOFF(TheTriple);
  } else if (TheTriple.isUEFI()) {
    MAI = new X86MCAsmInfoGNUCOFF(TheTriple);
  } else {
    // The default is ELF.
    MAI = new X86ELFMCAsmInfo(TheTriple);
  }

  constexpr int stackGrowth = -8;
  MCCFIInstruction Inst = MCCFIInstruction::cfiDefCfa(
      nullptr, MRI.getDwarfRegNum(X86::RSP, true), -stackGrowth);
  MAI->addInitialFrameState(Inst);

  // Add return address to move list
  MCCFIInstruction Inst2 = MCCFIInstruction::createOffset(
      nullptr, MRI.getDwarfRegNum(X86::RIP, true), stackGrowth);
  MAI->addInitialFrameState(Inst2);

  return MAI;
}

static MCInstPrinter *createX86MCInstPrinter(const Triple &T,
                                             unsigned SyntaxVariant,
                                             const MCAsmInfo &MAI,
                                             const MCInstrInfo &MII,
                                             const MCRegisterInfo &MRI) {
  if (SyntaxVariant == 0)
    return new X86ATTInstPrinter(MAI, MII, MRI);
  if (SyntaxVariant == 1)
    return new X86IntelInstPrinter(MAI, MII, MRI);
  return nullptr;
}

namespace llvm {
namespace X86_MC {

class X86MCInstrAnalysis : public MCInstrAnalysis {
  X86MCInstrAnalysis(const X86MCInstrAnalysis &) = delete;
  X86MCInstrAnalysis &operator=(const X86MCInstrAnalysis &) = delete;
  virtual ~X86MCInstrAnalysis() = default;

public:
  X86MCInstrAnalysis(const MCInstrInfo *MCII) : MCInstrAnalysis(MCII) {}

#define GET_STIPREDICATE_DECLS_FOR_MC_ANALYSIS
#include "X86GenSubtargetInfo.inc"

  bool clearsSuperRegisters(const MCRegisterInfo &MRI, const MCInst &Inst,
                            APInt &Mask) const override;
  std::vector<std::pair<uint64_t, uint64_t>>
  findPltEntries(uint64_t PltSectionVA, ArrayRef<uint8_t> PltContents,
                 const Triple &TargetTriple) const override;

  bool evaluateBranch(const MCInst &Inst, uint64_t Addr, uint64_t Size,
                      uint64_t &Target) const override;
  std::optional<uint64_t>
  evaluateMemoryOperandAddress(const MCInst &Inst, const MCSubtargetInfo *STI,
                               uint64_t Addr, uint64_t Size) const override;
  std::optional<uint64_t>
  getMemoryOperandRelocationOffset(const MCInst &Inst,
                                   uint64_t Size) const override;
};

#define GET_STIPREDICATE_DEFS_FOR_MC_ANALYSIS
#include "X86GenSubtargetInfo.inc"

bool X86MCInstrAnalysis::clearsSuperRegisters(const MCRegisterInfo &MRI,
                                              const MCInst &Inst,
                                              APInt &Mask) const {
  const MCInstrDesc &Desc = Info->get(Inst.getOpcode());
  unsigned NumDefs = Desc.getNumDefs();
  unsigned NumImplicitDefs = Desc.implicit_defs().size();
  assert(Mask.getBitWidth() == NumDefs + NumImplicitDefs &&
         "Unexpected number of bits in the mask!");

  bool HasVEX = (Desc.TSFlags & X86II::EncodingMask) == X86II::VEX;
  bool HasEVEX = (Desc.TSFlags & X86II::EncodingMask) == X86II::EVEX;
  bool HasXOP = (Desc.TSFlags & X86II::EncodingMask) == X86II::XOP;

  const MCRegisterClass &GR32RC = MRI.getRegClass(X86::GR32RegClassID);
  const MCRegisterClass &VR128XRC = MRI.getRegClass(X86::VR128XRegClassID);
  const MCRegisterClass &VR256XRC = MRI.getRegClass(X86::VR256XRegClassID);

  auto ClearsSuperReg = [=](unsigned RegID) {
    // On X86-64, a general purpose integer register is viewed as a 64-bit
    // register internal to the processor.
    // An update to the lower 32 bits of a 64 bit integer register is
    // architecturally defined to zero extend the upper 32 bits.
    if (GR32RC.contains(RegID))
      return true;

    // Early exit if this instruction has no vex/evex/xop prefix.
    if (!HasEVEX && !HasVEX && !HasXOP)
      return false;

    // All VEX and EVEX encoded instructions are defined to zero the high bits
    // of the destination register up to VLMAX (i.e. the maximum vector register
    // width pertaining to the instruction).
    // We assume the same behavior for XOP instructions too.
    return VR128XRC.contains(RegID) || VR256XRC.contains(RegID);
  };

  Mask.clearAllBits();
  for (unsigned I = 0, E = NumDefs; I < E; ++I) {
    const MCOperand &Op = Inst.getOperand(I);
    if (ClearsSuperReg(Op.getReg()))
      Mask.setBit(I);
  }

  for (unsigned I = 0, E = NumImplicitDefs; I < E; ++I) {
    const MCPhysReg Reg = Desc.implicit_defs()[I];
    if (ClearsSuperReg(Reg))
      Mask.setBit(NumDefs + I);
  }

  return Mask.getBoolValue();
}

static std::vector<std::pair<uint64_t, uint64_t>>
findX86_64PltEntries(uint64_t PltSectionVA, ArrayRef<uint8_t> PltContents) {
  // Do a lightweight parsing of PLT entries.
  std::vector<std::pair<uint64_t, uint64_t>> Result;
  for (uint64_t Byte = 0, End = PltContents.size(); Byte + 6 < End;) {
    // Recognize a jmp.
    if (PltContents[Byte] == 0xff && PltContents[Byte + 1] == 0x25) {
      // The jmp instruction at the beginning of each PLT entry jumps to the
      // address of the next instruction plus the immediate.
      uint32_t Imm = support::endian::read32le(PltContents.data() + Byte + 2);
      Result.push_back(
          std::make_pair(PltSectionVA + Byte, PltSectionVA + Byte + 6 + Imm));
      Byte += 6;
    } else
      Byte++;
  }
  return Result;
}

std::vector<std::pair<uint64_t, uint64_t>>
X86MCInstrAnalysis::findPltEntries(uint64_t PltSectionVA,
                                   ArrayRef<uint8_t> PltContents,
                                   const Triple &TargetTriple) const {
  if (TargetTriple.getArch() == Triple::x86_64)
    return findX86_64PltEntries(PltSectionVA, PltContents);
  return {};
}

bool X86MCInstrAnalysis::evaluateBranch(const MCInst &Inst, uint64_t Addr,
                                        uint64_t Size, uint64_t &Target) const {
  if (Inst.getNumOperands() == 0 ||
      Info->get(Inst.getOpcode()).operands()[0].OperandType !=
          MCOI::OPERAND_PCREL)
    return false;
  Target = Addr + Size + Inst.getOperand(0).getImm();
  return true;
}

std::optional<uint64_t> X86MCInstrAnalysis::evaluateMemoryOperandAddress(
    const MCInst &Inst, const MCSubtargetInfo *STI, uint64_t Addr,
    uint64_t Size) const {
  const MCInstrDesc &MCID = Info->get(Inst.getOpcode());
  int MemOpStart = X86II::getMemoryOperandNo(MCID.TSFlags);
  if (MemOpStart == -1)
    return std::nullopt;
  MemOpStart += X86II::getOperandBias(MCID);

  const MCOperand &SegReg = Inst.getOperand(MemOpStart + X86::AddrSegmentReg);
  const MCOperand &BaseReg = Inst.getOperand(MemOpStart + X86::AddrBaseReg);
  const MCOperand &IndexReg = Inst.getOperand(MemOpStart + X86::AddrIndexReg);
  const MCOperand &ScaleAmt = Inst.getOperand(MemOpStart + X86::AddrScaleAmt);
  const MCOperand &Disp = Inst.getOperand(MemOpStart + X86::AddrDisp);
  if (SegReg.getReg() != 0 || IndexReg.getReg() != 0 ||
      ScaleAmt.getImm() != 1 || !Disp.isImm())
    return std::nullopt;

  // RIP-relative addressing.
  if (BaseReg.getReg() == X86::RIP)
    return Addr + Size + Disp.getImm();

  return std::nullopt;
}

std::optional<uint64_t>
X86MCInstrAnalysis::getMemoryOperandRelocationOffset(const MCInst &Inst,
                                                     uint64_t Size) const {
  if (Inst.getOpcode() != X86::LEA64r)
    return std::nullopt;
  const MCInstrDesc &MCID = Info->get(Inst.getOpcode());
  int MemOpStart = X86II::getMemoryOperandNo(MCID.TSFlags);
  if (MemOpStart == -1)
    return std::nullopt;
  MemOpStart += X86II::getOperandBias(MCID);
  const MCOperand &SegReg = Inst.getOperand(MemOpStart + X86::AddrSegmentReg);
  const MCOperand &BaseReg = Inst.getOperand(MemOpStart + X86::AddrBaseReg);
  const MCOperand &IndexReg = Inst.getOperand(MemOpStart + X86::AddrIndexReg);
  const MCOperand &ScaleAmt = Inst.getOperand(MemOpStart + X86::AddrScaleAmt);
  const MCOperand &Disp = Inst.getOperand(MemOpStart + X86::AddrDisp);
  // Must be a simple rip-relative address.
  if (BaseReg.getReg() != X86::RIP || SegReg.getReg() != 0 ||
      IndexReg.getReg() != 0 || ScaleAmt.getImm() != 1 || !Disp.isImm())
    return std::nullopt;
  // rip-relative ModR/M immediate is 32 bits.
  assert(Size > 4 && "invalid instruction size for rip-relative lea");
  return Size - 4;
}

} // end of namespace X86_MC

} // end of namespace llvm

static MCInstrAnalysis *createX86MCInstrAnalysis(const MCInstrInfo *Info) {
  return new X86_MC::X86MCInstrAnalysis(Info);
}

// Force static initialization.
extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeX86TargetMC() {
  for (Target *T : {&getTheX86_64Target()}) {
    // Register the MC asm info.
    RegisterMCAsmInfoFn X(*T, createX86MCAsmInfo);

    // Register the MC instruction info.
    TargetRegistry::RegisterMCInstrInfo(*T, createX86MCInstrInfo);

    // Register the MC register info.
    TargetRegistry::RegisterMCRegInfo(*T, createX86MCRegisterInfo);

    // Register the MC subtarget info.
    TargetRegistry::RegisterMCSubtargetInfo(*T,
                                            X86_MC::createX86MCSubtargetInfo);

    // Register the MC instruction analyzer.
    TargetRegistry::RegisterMCInstrAnalysis(*T, createX86MCInstrAnalysis);

    // Register the code emitter.
    TargetRegistry::RegisterMCCodeEmitter(*T, createX86MCCodeEmitter);

    // Register the obj target streamer.
    TargetRegistry::RegisterObjectTargetStreamer(*T,
                                                 createX86ObjectTargetStreamer);

    // Register the asm target streamer.
    TargetRegistry::RegisterAsmTargetStreamer(*T, createX86AsmTargetStreamer);

    // Register the null streamer.
    TargetRegistry::RegisterNullTargetStreamer(*T, createX86NullTargetStreamer);

    TargetRegistry::RegisterCOFFStreamer(*T, createX86WinCOFFStreamer);

    // Register the MCInstPrinter.
    TargetRegistry::RegisterMCInstPrinter(*T, createX86MCInstPrinter);
  }

  // Register the asm backend.
  TargetRegistry::RegisterMCAsmBackend(getTheX86_64Target(),
                                       createX86_64AsmBackend);
}

MCRegister llvm::getX86SubSuperRegister(MCRegister Reg, unsigned Size,
                                        bool High) {
#define DEFAULT_NOREG                                                          \
  default:                                                                     \
    return X86::NoRegister;
#define SUB_SUPER(R1, R2, R3, R4, R)                                           \
  case X86::R1:                                                                \
  case X86::R2:                                                                \
  case X86::R3:                                                                \
  case X86::R4:                                                                \
    return X86::R;
#define A_SUB_SUPER(R)                                                         \
  case X86::AH:                                                                \
    SUB_SUPER(AL, AX, EAX, RAX, R)
#define D_SUB_SUPER(R)                                                         \
  case X86::DH:                                                                \
    SUB_SUPER(DL, DX, EDX, RDX, R)
#define C_SUB_SUPER(R)                                                         \
  case X86::CH:                                                                \
    SUB_SUPER(CL, CX, ECX, RCX, R)
#define B_SUB_SUPER(R)                                                         \
  case X86::BH:                                                                \
    SUB_SUPER(BL, BX, EBX, RBX, R)
#define SI_SUB_SUPER(R) SUB_SUPER(SIL, SI, ESI, RSI, R)
#define DI_SUB_SUPER(R) SUB_SUPER(DIL, DI, EDI, RDI, R)
#define BP_SUB_SUPER(R) SUB_SUPER(BPL, BP, EBP, RBP, R)
#define SP_SUB_SUPER(R) SUB_SUPER(SPL, SP, ESP, RSP, R)
#define NO_SUB_SUPER(NO, REG)                                                  \
  SUB_SUPER(R##NO##B, R##NO##W, R##NO##D, R##NO, REG)
#define NO_SUB_SUPER_B(NO) NO_SUB_SUPER(NO, R##NO##B)
#define NO_SUB_SUPER_W(NO) NO_SUB_SUPER(NO, R##NO##W)
#define NO_SUB_SUPER_D(NO) NO_SUB_SUPER(NO, R##NO##D)
#define NO_SUB_SUPER_Q(NO) NO_SUB_SUPER(NO, R##NO)
  switch (Size) {
  default:
    llvm_unreachable("illegal register size");
  case 8:
    if (High) {
      switch (Reg.id()) {
        DEFAULT_NOREG
        A_SUB_SUPER(AH)
        D_SUB_SUPER(DH)
        C_SUB_SUPER(CH)
        B_SUB_SUPER(BH)
      }
    } else {
      switch (Reg.id()) {
        DEFAULT_NOREG
        A_SUB_SUPER(AL)
        D_SUB_SUPER(DL)
        C_SUB_SUPER(CL)
        B_SUB_SUPER(BL)
        SI_SUB_SUPER(SIL)
        DI_SUB_SUPER(DIL)
        BP_SUB_SUPER(BPL)
        SP_SUB_SUPER(SPL)
        NO_SUB_SUPER_B(8)
        NO_SUB_SUPER_B(9)
        NO_SUB_SUPER_B(10)
        NO_SUB_SUPER_B(11)
        NO_SUB_SUPER_B(12)
        NO_SUB_SUPER_B(13)
        NO_SUB_SUPER_B(14)
        NO_SUB_SUPER_B(15)
        NO_SUB_SUPER_B(16)
        NO_SUB_SUPER_B(17)
        NO_SUB_SUPER_B(18)
        NO_SUB_SUPER_B(19)
        NO_SUB_SUPER_B(20)
        NO_SUB_SUPER_B(21)
        NO_SUB_SUPER_B(22)
        NO_SUB_SUPER_B(23)
        NO_SUB_SUPER_B(24)
        NO_SUB_SUPER_B(25)
        NO_SUB_SUPER_B(26)
        NO_SUB_SUPER_B(27)
        NO_SUB_SUPER_B(28)
        NO_SUB_SUPER_B(29)
        NO_SUB_SUPER_B(30)
        NO_SUB_SUPER_B(31)
      }
    }
  case 16:
    switch (Reg.id()) {
      DEFAULT_NOREG
      A_SUB_SUPER(AX)
      D_SUB_SUPER(DX)
      C_SUB_SUPER(CX)
      B_SUB_SUPER(BX)
      SI_SUB_SUPER(SI)
      DI_SUB_SUPER(DI)
      BP_SUB_SUPER(BP)
      SP_SUB_SUPER(SP)
      NO_SUB_SUPER_W(8)
      NO_SUB_SUPER_W(9)
      NO_SUB_SUPER_W(10)
      NO_SUB_SUPER_W(11)
      NO_SUB_SUPER_W(12)
      NO_SUB_SUPER_W(13)
      NO_SUB_SUPER_W(14)
      NO_SUB_SUPER_W(15)
      NO_SUB_SUPER_W(16)
      NO_SUB_SUPER_W(17)
      NO_SUB_SUPER_W(18)
      NO_SUB_SUPER_W(19)
      NO_SUB_SUPER_W(20)
      NO_SUB_SUPER_W(21)
      NO_SUB_SUPER_W(22)
      NO_SUB_SUPER_W(23)
      NO_SUB_SUPER_W(24)
      NO_SUB_SUPER_W(25)
      NO_SUB_SUPER_W(26)
      NO_SUB_SUPER_W(27)
      NO_SUB_SUPER_W(28)
      NO_SUB_SUPER_W(29)
      NO_SUB_SUPER_W(30)
      NO_SUB_SUPER_W(31)
    }
  case 32:
    switch (Reg.id()) {
      DEFAULT_NOREG
      A_SUB_SUPER(EAX)
      D_SUB_SUPER(EDX)
      C_SUB_SUPER(ECX)
      B_SUB_SUPER(EBX)
      SI_SUB_SUPER(ESI)
      DI_SUB_SUPER(EDI)
      BP_SUB_SUPER(EBP)
      SP_SUB_SUPER(ESP)
      NO_SUB_SUPER_D(8)
      NO_SUB_SUPER_D(9)
      NO_SUB_SUPER_D(10)
      NO_SUB_SUPER_D(11)
      NO_SUB_SUPER_D(12)
      NO_SUB_SUPER_D(13)
      NO_SUB_SUPER_D(14)
      NO_SUB_SUPER_D(15)
      NO_SUB_SUPER_D(16)
      NO_SUB_SUPER_D(17)
      NO_SUB_SUPER_D(18)
      NO_SUB_SUPER_D(19)
      NO_SUB_SUPER_D(20)
      NO_SUB_SUPER_D(21)
      NO_SUB_SUPER_D(22)
      NO_SUB_SUPER_D(23)
      NO_SUB_SUPER_D(24)
      NO_SUB_SUPER_D(25)
      NO_SUB_SUPER_D(26)
      NO_SUB_SUPER_D(27)
      NO_SUB_SUPER_D(28)
      NO_SUB_SUPER_D(29)
      NO_SUB_SUPER_D(30)
      NO_SUB_SUPER_D(31)
    }
  case 64:
    switch (Reg.id()) {
      DEFAULT_NOREG
      A_SUB_SUPER(RAX)
      D_SUB_SUPER(RDX)
      C_SUB_SUPER(RCX)
      B_SUB_SUPER(RBX)
      SI_SUB_SUPER(RSI)
      DI_SUB_SUPER(RDI)
      BP_SUB_SUPER(RBP)
      SP_SUB_SUPER(RSP)
      NO_SUB_SUPER_Q(8)
      NO_SUB_SUPER_Q(9)
      NO_SUB_SUPER_Q(10)
      NO_SUB_SUPER_Q(11)
      NO_SUB_SUPER_Q(12)
      NO_SUB_SUPER_Q(13)
      NO_SUB_SUPER_Q(14)
      NO_SUB_SUPER_Q(15)
      NO_SUB_SUPER_Q(16)
      NO_SUB_SUPER_Q(17)
      NO_SUB_SUPER_Q(18)
      NO_SUB_SUPER_Q(19)
      NO_SUB_SUPER_Q(20)
      NO_SUB_SUPER_Q(21)
      NO_SUB_SUPER_Q(22)
      NO_SUB_SUPER_Q(23)
      NO_SUB_SUPER_Q(24)
      NO_SUB_SUPER_Q(25)
      NO_SUB_SUPER_Q(26)
      NO_SUB_SUPER_Q(27)
      NO_SUB_SUPER_Q(28)
      NO_SUB_SUPER_Q(29)
      NO_SUB_SUPER_Q(30)
      NO_SUB_SUPER_Q(31)
    }
  }
}
