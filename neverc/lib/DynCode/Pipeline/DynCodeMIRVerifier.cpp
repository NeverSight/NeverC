// Volume 6 task 9: the sealed dyncode final-MIR verifier gate.
//
// This is the read-only half of the old monolithic MIRPrepPass.  It runs at the
// Final machine-pipeline hook (immediately before AsmPrinter) as the
// neverc.dyncode.mir.final_verify SEALED_HOST_GATE: no plugin may register a
// Provider/Interceptor for it or SKIP it.  It never mutates the MIR.
//
// Post-transform invariant: after the neverc.dyncode.mir.prepare transform (or a
// plugin replacement of it), no dyncode/SEH strip pseudo may survive.  If one
// does -- e.g. a wrong replacement provider left it behind -- the gate emits a
// hard error and compilation fails before any object bytes are written.  The
// external-reference and constant-pool audits stay diagnostic (the downstream
// extractor remains the authority on data sections), preserving existing
// behaviour for clean dyncode.

#include "neverc/DynCode/MIR/MIRPrepPass.h"
#include "Extractor/ExtractorCommon.h"
#include "MIR/MIRPseudoClassify.h"
#include "neverc/DynCode/Pipeline/Diagnostics.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

namespace neverc {
namespace dyncode {
namespace {

constexpr llvm::StringLiteral kDiagnosticPrefix = Diagnostics::MIRPrefix;

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
        if (Name.empty() || isDynCodeInternalRuntimeName(Name) ||
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

/// Returns the number of forbidden strip pseudos that survived the transform.
/// Every hit is a hard violation of the post-transform invariant.
unsigned countForbiddenPseudos(MachineFunction &MF, LLVMContext &Ctx) {
  const bool CheckSEH = MF.getTarget().getTargetTriple().isOSWindows();
  unsigned Count = 0;
  for (const MachineBasicBlock &MBB : MF) {
    for (const MachineInstr &MI : MBB) {
      const bool Strip = isDynCodeStripPseudo(MI.getOpcode());
      const bool SEH = CheckSEH && isSEHPseudoByMnemonic(MI);
      if (!Strip && !SEH)
        continue;
      ++Count;
      const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
      StringRef OpName = TII ? TII->getName(MI.getOpcode()) : StringRef();
      std::string Msg;
      raw_string_ostream OS(Msg);
      OS << kDiagnosticPrefix << "final MIR verifier: function '" << MF.getName()
         << "' still contains forbidden " << (SEH ? "SEH" : "dyncode")
         << " pseudo '" << OpName
         << "'; the mir.prepare transform (or its replacement) must remove it "
            "before emission";
      OS.flush();
      Ctx.emitError(Msg);
    }
  }
  return Count;
}

/// Sealed gate for neverc.dyncode.mir.final_verify.  Never mutates MIR.
class DynCodeMIRVerifierPass final : public MachineFunctionPass {
public:
  static char ID;

  explicit DynCodeMIRVerifierPass(const DynCodeOptions &Opts)
      : MachineFunctionPass(ID), Enabled(Opts.Enabled), Target(Opts.Target) {}

  StringRef getPassName() const override {
    return "NeverC DynCode MIR Final Verify";
  }

  void getAnalysisUsage(AnalysisUsage &Usage) const override {
    Usage.setPreservesAll();
    MachineFunctionPass::getAnalysisUsage(Usage);
  }

  bool runOnMachineFunction(MachineFunction &MF) override {
    if (!Enabled)
      return false;

    // Hard gate: no strip/SEH pseudo may survive into the emitted object.
    (void)countForbiddenPseudos(MF, MF.getFunction().getContext());

    // Diagnostic audits: the downstream extractor remains the authority that
    // rejects leftover external references and data sections.
    (void)auditConstantPool(MF);
    (void)auditExternalReferences(MF, Target);

    return false;
  }

private:
  bool Enabled = false;
  TargetDesc Target;
};

} // namespace

char DynCodeMIRVerifierPass::ID = 0;

FunctionPass *createDynCodeMIRVerifierPass(const DynCodeOptions &Opts) {
  return new DynCodeMIRVerifierPass(Opts);
}

} // namespace dyncode
} // namespace neverc
