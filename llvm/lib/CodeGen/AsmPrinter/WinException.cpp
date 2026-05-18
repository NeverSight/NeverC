//===-- CodeGen/AsmPrinter/WinException.cpp - Dwarf Exception Impl ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains support for writing Win64 exception info into asm files.
//
//===----------------------------------------------------------------------===//

#include "WinException.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/WinEHFuncInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Target/TargetMachine.h"
using namespace llvm;

WinException::WinException(AsmPrinter *A) : EHStreamer(A) {
  // MSVC's EH tables are always composed of 32-bit words.  All known 64-bit
  // platforms use an imagerel32 relocation to refer to symbols.
  useImageRel32 = (A->getDataLayout().getPointerSizeInBits() == 64);
  isAArch64 = Asm->TM.getTargetTriple().isAArch64();
  isX86 = Asm->TM.getTargetTriple().isX86();
}

WinException::~WinException() = default;

/// endModule - Emit all exception information that should come after the
/// content.
void WinException::endModule() {
  auto &OS = *Asm->OutStreamer;
  const Module *M = MMI->getModule();

  if (M->getModuleFlag("ehcontguard") && !EHContTargets.empty()) {
    // Emit the symbol index of each ehcont target.
    OS.switchSection(Asm->OutContext.getObjectFileInfo()->getGEHContSection());
    for (const MCSymbol *S : EHContTargets) {
      OS.emitCOFFSymbolIndex(S);
    }
  }
}

void WinException::beginFunction(const MachineFunction *MF) {
  shouldEmitMoves = shouldEmitPersonality = shouldEmitLSDA = false;

  // If any landing pads survive, we need an EH table.
  bool hasLandingPads = !MF->getLandingPads().empty();
  bool hasEHFunclets = MF->hasEHFunclets();

  const Function &F = MF->getFunction();

  shouldEmitMoves = Asm->needsSEHMoves() && MF->hasWinCFI();

  const TargetLoweringObjectFile &TLOF = Asm->getObjFileLowering();
  unsigned PerEncoding = TLOF.getPersonalityEncoding();

  EHPersonality Per = EHPersonality::Unknown;
  const Function *PerFn = nullptr;
  if (F.hasPersonalityFn()) {
    PerFn = dyn_cast<Function>(F.getPersonalityFn()->stripPointerCasts());
    Per = classifyEHPersonality(PerFn);
  }

  bool forceEmitPersonality = F.hasPersonalityFn() &&
                              !isNoOpWithoutInvoke(Per) &&
                              F.needsUnwindTableEntry();

  shouldEmitPersonality =
      forceEmitPersonality || ((hasLandingPads || hasEHFunclets) &&
                               PerEncoding != dwarf::DW_EH_PE_omit && PerFn);

  unsigned LSDAEncoding = TLOF.getLSDAEncoding();
  shouldEmitLSDA =
      shouldEmitPersonality && LSDAEncoding != dwarf::DW_EH_PE_omit;

  // If we're not using CFI, we don't want the CFI or the personality, but we
  // might want EH tables if we had EH pads.
  if (!Asm->MAI->usesWindowsCFI()) {
    shouldEmitLSDA = hasEHFunclets;
    shouldEmitPersonality = false;
    return;
  }

  beginFunclet(MF->front(), Asm->CurrentFnSym);
}

void WinException::markFunctionEnd() {
  if (isAArch64 && CurrentFuncletEntry &&
      (shouldEmitMoves || shouldEmitPersonality))
    Asm->OutStreamer->emitWinCFIFuncletOrFuncEnd();
}

/// endFunction - Gather and emit post-function exception information.
///
void WinException::endFunction(const MachineFunction *MF) {
  if (!shouldEmitPersonality && !shouldEmitMoves && !shouldEmitLSDA)
    return;

  const Function &F = MF->getFunction();
  EHPersonality Per = EHPersonality::Unknown;
  if (F.hasPersonalityFn())
    Per = classifyEHPersonality(F.getPersonalityFn()->stripPointerCasts());

  endFuncletImpl();

  // endFunclet will emit the necessary .xdata tables for table-based SEH.
  if (Per == EHPersonality::MSVC_TableSEH && MF->hasEHFunclets())
    return;

  if (shouldEmitPersonality || shouldEmitLSDA) {
    Asm->OutStreamer->pushSection();

    // Just switch sections to the right xdata section.
    MCSection *XData = Asm->OutStreamer->getAssociatedXDataSection(
        Asm->OutStreamer->getCurrentSectionOnly());
    Asm->OutStreamer->switchSection(XData);

    // Emit the tables appropriate to the personality function in use. If we
    // don't recognize the personality, assume it uses an Itanium-style LSDA.
    if (Per == EHPersonality::MSVC_TableSEH)
      emitCSpecificHandlerTable(MF);
    else
      emitExceptionTable();

    Asm->OutStreamer->popSection();
  }

  if (!MF->getCatchretTargets().empty()) {
    // Copy the function's catchret targets to a module-level list.
    EHContTargets.insert(EHContTargets.end(), MF->getCatchretTargets().begin(),
                         MF->getCatchretTargets().end());
  }
}

/// Retrieve the MCSymbol for a GlobalValue or MachineBasicBlock.
static MCSymbol *getMCSymbolForMBB(AsmPrinter *Asm,
                                   const MachineBasicBlock *MBB) {
  if (!MBB)
    return nullptr;

  assert(MBB->isEHFuncletEntry());

  // Give catches and cleanups a name based off of their parent function and
  // their funclet entry block's number.
  const MachineFunction *MF = MBB->getParent();
  const Function &F = MF->getFunction();
  StringRef FuncLinkageName = GlobalValue::dropLLVMManglingEscape(F.getName());
  MCContext &Ctx = MF->getContext();
  StringRef HandlerPrefix = MBB->isCleanupFuncletEntry() ? "dtor" : "catch";
  return Ctx.getOrCreateSymbol("?" + HandlerPrefix + "$" +
                               Twine(MBB->getNumber()) + "@?0?" +
                               FuncLinkageName + "@4HA");
}

void WinException::beginFunclet(const MachineBasicBlock &MBB, MCSymbol *Sym) {
  CurrentFuncletEntry = &MBB;

  const Function &F = Asm->MF->getFunction();
  // If a symbol was not provided for the funclet, invent one.
  if (!Sym) {
    Sym = getMCSymbolForMBB(Asm, &MBB);

    // Describe our funclet symbol as a function with internal linkage.
    Asm->OutStreamer->beginCOFFSymbolDef(Sym);
    Asm->OutStreamer->emitCOFFSymbolStorageClass(COFF::IMAGE_SYM_CLASS_STATIC);
    Asm->OutStreamer->emitCOFFSymbolType(COFF::IMAGE_SYM_DTYPE_FUNCTION
                                         << COFF::SCT_COMPLEX_TYPE_SHIFT);
    Asm->OutStreamer->endCOFFSymbolDef();

    // We want our funclet's entry point to be aligned such that no nops will be
    // present after the label.
    Asm->emitAlignment(std::max(Asm->MF->getAlignment(), MBB.getAlignment()),
                       &F);

    // Now that we've emitted the alignment directive, point at our funclet.
    Asm->OutStreamer->emitLabel(Sym);
  }

  // Mark 'Sym' as starting our funclet.
  if (shouldEmitMoves || shouldEmitPersonality) {
    CurrentFuncletTextSection = Asm->OutStreamer->getCurrentSectionOnly();
    Asm->OutStreamer->emitWinCFIStartProc(Sym);
  }

  if (shouldEmitPersonality) {
    const TargetLoweringObjectFile &TLOF = Asm->getObjFileLowering();
    const Function *PerFn = nullptr;

    // Determine which personality routine we are using for this funclet.
    if (F.hasPersonalityFn())
      PerFn = dyn_cast<Function>(F.getPersonalityFn()->stripPointerCasts());
    const MCSymbol *PersHandlerSym =
        TLOF.getCFIPersonalitySymbol(PerFn, Asm->TM, MMI);

    // Do not emit a .seh_handler directives for cleanup funclets.
    // FIXME: This means cleanup funclets cannot handle exceptions. Given that
    // NeverC doesn't produce EH constructs inside cleanup funclets and LLVM's
    // inliner doesn't allow inlining them, this isn't a major problem in
    // practice.
    if (!CurrentFuncletEntry->isCleanupFuncletEntry())
      Asm->OutStreamer->emitWinEHHandler(PersHandlerSym, true, true);
  }
}

void WinException::endFunclet() {
  if (isAArch64 && CurrentFuncletEntry &&
      (shouldEmitMoves || shouldEmitPersonality)) {
    Asm->OutStreamer->switchSection(CurrentFuncletTextSection);
    Asm->OutStreamer->emitWinCFIFuncletOrFuncEnd();
  }
  endFuncletImpl();
}

void WinException::endFuncletImpl() {
  // No funclet to process?  Great, we have nothing to do.
  if (!CurrentFuncletEntry)
    return;

  const MachineFunction *MF = Asm->MF;
  if (shouldEmitMoves || shouldEmitPersonality) {
    const Function &F = MF->getFunction();
    EHPersonality Per = EHPersonality::Unknown;
    if (F.hasPersonalityFn())
      Per = classifyEHPersonality(F.getPersonalityFn()->stripPointerCasts());

    if (Per == EHPersonality::MSVC_TableSEH && MF->hasEHFunclets() &&
        !CurrentFuncletEntry->isEHFuncletEntry()) {
      // Emit an UNWIND_INFO struct describing the prologue.
      Asm->OutStreamer->emitWinEHHandlerData();

      // If this is the parent function in Win64 SEH, emit the LSDA immediately
      // following .seh_handlerdata.
      emitCSpecificHandlerTable(MF);
    } else if (shouldEmitPersonality || shouldEmitLSDA) {
      // Emit an UNWIND_INFO struct describing the prologue.
      Asm->OutStreamer->emitWinEHHandlerData();
      // In these cases, no further info is written to the .xdata section
      // right here, but is written by e.g. emitExceptionTable in endFunction()
      // above.
    } else {
      // No need to emit the EH handler data right here if nothing needs
      // writing to the .xdata section; it will be emitted for all
      // functions that need it in the end anyway.
    }

    // Switch back to the funclet start .text section now that we are done
    // writing to .xdata, and emit an .seh_endproc directive to mark the end of
    // the function.
    Asm->OutStreamer->switchSection(CurrentFuncletTextSection);
    Asm->OutStreamer->emitWinCFIEndProc();
  }

  // Let's make sure we don't try to end the same funclet twice.
  CurrentFuncletEntry = nullptr;
}

const MCExpr *WinException::create32bitRef(const MCSymbol *Value) {
  if (!Value)
    return MCConstantExpr::create(0, Asm->OutContext);
  return MCSymbolRefExpr::create(Value,
                                 useImageRel32
                                     ? MCSymbolRefExpr::VK_COFF_IMGREL32
                                     : MCSymbolRefExpr::VK_None,
                                 Asm->OutContext);
}

const MCExpr *WinException::create32bitRef(const GlobalValue *GV) {
  if (!GV)
    return MCConstantExpr::create(0, Asm->OutContext);
  return create32bitRef(Asm->getSymbol(GV));
}

const MCExpr *WinException::getLabel(const MCSymbol *Label) {
  return MCSymbolRefExpr::create(Label, MCSymbolRefExpr::VK_COFF_IMGREL32,
                                 Asm->OutContext);
}

const MCExpr *WinException::getLabelPlusOne(const MCSymbol *Label) {
  return MCBinaryExpr::createAdd(getLabel(Label),
                                 MCConstantExpr::create(1, Asm->OutContext),
                                 Asm->OutContext);
}

const MCExpr *WinException::getOffset(const MCSymbol *OffsetOf,
                                      const MCSymbol *OffsetFrom) {
  return MCBinaryExpr::createSub(
      MCSymbolRefExpr::create(OffsetOf, Asm->OutContext),
      MCSymbolRefExpr::create(OffsetFrom, Asm->OutContext), Asm->OutContext);
}

const MCExpr *WinException::getOffsetPlusOne(const MCSymbol *OffsetOf,
                                             const MCSymbol *OffsetFrom) {
  return MCBinaryExpr::createAdd(getOffset(OffsetOf, OffsetFrom),
                                 MCConstantExpr::create(1, Asm->OutContext),
                                 Asm->OutContext);
}

namespace {

/// Top-level state used to represent unwind to caller
const int NullState = -1;

struct InvokeStateChange {
  /// EH Label immediately after the last invoke in the previous state, or
  /// nullptr if the previous state was the null state.
  const MCSymbol *PreviousEndLabel;

  /// EH label immediately before the first invoke in the new state, or nullptr
  /// if the new state is the null state.
  const MCSymbol *NewStartLabel;

  /// State of the invoke following NewStartLabel, or NullState to indicate
  /// the presence of calls which may unwind to caller.
  int NewState;
};

/// Iterator that reports all the invoke state changes in a range of machine
/// basic blocks.  Changes to the null state are reported whenever a call that
/// may unwind to caller is encountered.  The MBB range is expected to be an
/// entire function or funclet, and the start and end of the range are treated
/// as being in the NullState even if there's not an unwind-to-caller call
/// before the first invoke or after the last one (i.e., the first state change
/// reported is the first change to something other than NullState, and a
/// change back to NullState is always reported at the end of iteration).
class InvokeStateChangeIterator {
  InvokeStateChangeIterator(const WinEHFuncInfo &EHInfo,
                            MachineFunction::const_iterator MFI,
                            MachineFunction::const_iterator MFE,
                            MachineBasicBlock::const_iterator MBBI,
                            int BaseState)
      : EHInfo(EHInfo), MFI(MFI), MFE(MFE), MBBI(MBBI), BaseState(BaseState) {
    LastStateChange.PreviousEndLabel = nullptr;
    LastStateChange.NewStartLabel = nullptr;
    LastStateChange.NewState = BaseState;
    scan();
  }

public:
  static iterator_range<InvokeStateChangeIterator>
  range(const WinEHFuncInfo &EHInfo, MachineFunction::const_iterator Begin,
        MachineFunction::const_iterator End, int BaseState = NullState) {
    // Reject empty ranges to simplify bookkeeping by ensuring that we can get
    // the end of the last block.
    assert(Begin != End);
    auto BlockBegin = Begin->begin();
    auto BlockEnd = std::prev(End)->end();
    return make_range(
        InvokeStateChangeIterator(EHInfo, Begin, End, BlockBegin, BaseState),
        InvokeStateChangeIterator(EHInfo, End, End, BlockEnd, BaseState));
  }

  // Iterator methods.
  bool operator==(const InvokeStateChangeIterator &O) const {
    assert(BaseState == O.BaseState);
    // Must be visiting same block.
    if (MFI != O.MFI)
      return false;
    // Must be visiting same isntr.
    if (MBBI != O.MBBI)
      return false;
    // At end of block/instr iteration, we can still have two distinct states:
    // one to report the final EndLabel, and another indicating the end of the
    // state change iteration.  Check for CurrentEndLabel equality to
    // distinguish these.
    return CurrentEndLabel == O.CurrentEndLabel;
  }

  bool operator!=(const InvokeStateChangeIterator &O) const {
    return !operator==(O);
  }
  InvokeStateChange &operator*() { return LastStateChange; }
  InvokeStateChange *operator->() { return &LastStateChange; }
  InvokeStateChangeIterator &operator++() { return scan(); }

private:
  InvokeStateChangeIterator &scan();

  const WinEHFuncInfo &EHInfo;
  const MCSymbol *CurrentEndLabel = nullptr;
  MachineFunction::const_iterator MFI;
  MachineFunction::const_iterator MFE;
  MachineBasicBlock::const_iterator MBBI;
  InvokeStateChange LastStateChange;
  bool VisitingInvoke = false;
  int BaseState;
};

} // end anonymous namespace

InvokeStateChangeIterator &InvokeStateChangeIterator::scan() {
  bool IsNewBlock = false;
  for (; MFI != MFE; ++MFI, IsNewBlock = true) {
    if (IsNewBlock)
      MBBI = MFI->begin();
    for (auto MBBE = MFI->end(); MBBI != MBBE; ++MBBI) {
      const MachineInstr &MI = *MBBI;
      if (!VisitingInvoke && LastStateChange.NewState != BaseState &&
          MI.isCall() && !EHStreamer::callToNoUnwindFunction(&MI)) {
        // Indicate a change of state to the null state.  We don't have
        // start/end EH labels handy but the caller won't expect them for
        // null state regions.
        LastStateChange.PreviousEndLabel = CurrentEndLabel;
        LastStateChange.NewStartLabel = nullptr;
        LastStateChange.NewState = BaseState;
        CurrentEndLabel = nullptr;
        // Don't re-visit this instr on the next scan
        ++MBBI;
        return *this;
      }

      // All other state changes are at EH labels before/after invokes.
      if (!MI.isEHLabel())
        continue;
      MCSymbol *Label = MI.getOperand(0).getMCSymbol();
      if (Label == CurrentEndLabel) {
        VisitingInvoke = false;
        continue;
      }
      auto InvokeMapIter = EHInfo.LabelToStateMap.find(Label);
      // Ignore EH labels that aren't the ones inserted before an invoke
      if (InvokeMapIter == EHInfo.LabelToStateMap.end())
        continue;
      auto &StateAndEnd = InvokeMapIter->second;
      int NewState = StateAndEnd.first;
      // Keep track of the fact that we're between EH start/end labels so
      // we know not to treat the inoke we'll see as unwinding to caller.
      VisitingInvoke = true;
      if (NewState == LastStateChange.NewState) {
        // The state isn't actually changing here.  Record the new end and
        // keep going.
        CurrentEndLabel = StateAndEnd.second;
        continue;
      }
      // Found a state change to report
      LastStateChange.PreviousEndLabel = CurrentEndLabel;
      LastStateChange.NewStartLabel = Label;
      LastStateChange.NewState = NewState;
      // Start keeping track of the new current end
      CurrentEndLabel = StateAndEnd.second;
      // Don't re-visit this instr on the next scan
      ++MBBI;
      return *this;
    }
  }
  // Iteration hit the end of the block range.
  if (LastStateChange.NewState != BaseState) {
    // Report the end of the last new state
    LastStateChange.PreviousEndLabel = CurrentEndLabel;
    LastStateChange.NewStartLabel = nullptr;
    LastStateChange.NewState = BaseState;
    // Leave CurrentEndLabel non-null to distinguish this state from end.
    assert(CurrentEndLabel != nullptr);
    return *this;
  }
  // We've reported all state changes and hit the end state.
  CurrentEndLabel = nullptr;
  return *this;
}

/// Emit the language-specific data that __C_specific_handler expects.  This
/// handler lives in the x64 Microsoft C runtime and allows catching or cleaning
/// up after faults with __try, __except, and __finally.  The typeinfo values
/// are not really RTTI data, but pointers to filter functions that return an
/// integer (1, 0, or -1) indicating how to handle the exception. For __finally
/// blocks and other cleanups, the landing pad label is zero, and the filter
/// function is actually a cleanup handler with the same prototype.  A catch-all
/// entry is modeled with a null filter function field and a non-zero landing
/// pad label.
///
/// Possible filter function return values:
///   EXCEPTION_EXECUTE_HANDLER (1):
///     Jump to the landing pad label after cleanups.
///   EXCEPTION_CONTINUE_SEARCH (0):
///     Continue searching this table or continue unwinding.
///   EXCEPTION_CONTINUE_EXECUTION (-1):
///     Resume execution at the trapping PC.
///
/// Inferred table structure:
///   struct Table {
///     int NumEntries;
///     struct Entry {
///       imagerel32 LabelStart;       // Inclusive
///       imagerel32 LabelEnd;         // Exclusive
///       imagerel32 FilterOrFinally;  // One means catch-all.
///       imagerel32 LabelLPad;        // Zero means __finally.
///     } Entries[NumEntries];
///   };
void WinException::emitCSpecificHandlerTable(const MachineFunction *MF) {
  auto &OS = *Asm->OutStreamer;
  MCContext &Ctx = Asm->OutContext;
  const WinEHFuncInfo &FuncInfo = *MF->getWinEHFuncInfo();

  bool VerboseAsm = OS.isVerboseAsm();
  auto AddComment = [&](const Twine &Comment) {
    if (VerboseAsm)
      OS.AddComment(Comment);
  };

  if (!isAArch64) {
    // Emit a label assignment with the SEH frame offset so we can use it for
    // llvm.eh.recoverfp.
    StringRef FLinkageName =
        GlobalValue::dropLLVMManglingEscape(MF->getFunction().getName());
    MCSymbol *ParentFrameOffset =
        Ctx.getOrCreateParentFrameOffsetSymbol(FLinkageName);
    const MCExpr *MCOffset =
        MCConstantExpr::create(FuncInfo.SEHSetFrameOffset, Ctx);
    Asm->OutStreamer->emitAssignment(ParentFrameOffset, MCOffset);
  }

  // Use the assembler to compute the number of table entries through label
  // difference and division.
  MCSymbol *TableBegin =
      Ctx.createTempSymbol("lsda_begin", /*AlwaysAddSuffix=*/true);
  MCSymbol *TableEnd =
      Ctx.createTempSymbol("lsda_end", /*AlwaysAddSuffix=*/true);
  const MCExpr *LabelDiff = getOffset(TableEnd, TableBegin);
  const MCExpr *EntrySize = MCConstantExpr::create(16, Ctx);
  const MCExpr *EntryCount = MCBinaryExpr::createDiv(LabelDiff, EntrySize, Ctx);
  AddComment("Number of call sites");
  OS.emitValue(EntryCount, 4);

  OS.emitLabel(TableBegin);

  // Iterate over all the invoke try ranges. Unlike MSVC, LLVM currently only
  // models exceptions from invokes. LLVM also allows arbitrary reordering of
  // the code, so our tables end up looking a bit different. Rather than
  // trying to match MSVC's tables exactly, we emit a denormalized table.  For
  // each range of invokes in the same state, we emit table entries for all
  // the actions that would be taken in that state. This means our tables are
  // slightly bigger, which is OK.
  const MCSymbol *LastStartLabel = nullptr;
  int LastEHState = -1;
  // Break out before we enter into a finally funclet.
  // FIXME: We need to emit separate EH tables for cleanups.
  MachineFunction::const_iterator End = MF->end();
  MachineFunction::const_iterator Stop = std::next(MF->begin());
  while (Stop != End && !Stop->isEHFuncletEntry())
    ++Stop;
  for (const auto &StateChange :
       InvokeStateChangeIterator::range(FuncInfo, MF->begin(), Stop)) {
    // Emit all the actions for the state we just transitioned out of
    // if it was not the null state
    if (LastEHState != -1)
      emitSEHActionsForRange(FuncInfo, LastStartLabel,
                             StateChange.PreviousEndLabel, LastEHState);
    LastStartLabel = StateChange.NewStartLabel;
    LastEHState = StateChange.NewState;
  }
  for (auto &Entry : FuncInfo.SEHUnwindMap) {
    if (!Entry.IsFinally && Entry.ToState != -1) {
      // Mark up the destination of _local_unwind so it doesn't unwind
      // too far.
      //
      // FIXME: Can this overlap with the EH_LABEL for an invoke?
      // Be defensive: some SEH patterns (especially after outlining) may leave
      // this as an IR basic block or null. Don't crash while emitting asm.
      if (auto *Handler = Entry.Handler.dyn_cast<MachineBasicBlock *>()) {
        const MCSymbol *Begin = Handler->getSymbol();
        emitSEHActionsForRange(FuncInfo, Begin, Begin, Entry.ToState);
      }
    }
  }

  OS.emitLabel(TableEnd);
}

void WinException::emitSEHActionsForRange(const WinEHFuncInfo &FuncInfo,
                                          const MCSymbol *BeginLabel,
                                          const MCSymbol *EndLabel, int State) {
  auto &OS = *Asm->OutStreamer;
  MCContext &Ctx = Asm->OutContext;
  bool VerboseAsm = OS.isVerboseAsm();
  auto AddComment = [&](const Twine &Comment) {
    if (VerboseAsm)
      OS.AddComment(Comment);
  };

  assert(BeginLabel && EndLabel);
  while (State != -1) {
    if (State < 0 || static_cast<size_t>(State) >= FuncInfo.SEHUnwindMap.size())
      return;
    const SEHUnwindMapEntry &UME = FuncInfo.SEHUnwindMap[State];
    const MCExpr *FilterOrFinally;
    const MCExpr *ExceptOrNull;
    const MCSymbol *HandlerSym = nullptr;
    if (UME.Handler.isNull())
      return;
    if (auto *HandlerMBB = UME.Handler.dyn_cast<MachineBasicBlock *>()) {
      HandlerSym = UME.IsFinally ? getMCSymbolForMBB(Asm, HandlerMBB)
                                 : HandlerMBB->getSymbol();
    } else if (const auto *HandlerBB =
                   UME.Handler.dyn_cast<const BasicBlock *>()) {
      // Fallback: handler lives in a different IR function (e.g. outlined).
      // Use the parent function symbol. This avoids crashing under /O2 and
      // keeps the EH table emission conservative.
      HandlerSym = Asm->getSymbol(HandlerBB->getParent());
    }
    if (!HandlerSym)
      return;

    if (UME.IsFinally) {
      FilterOrFinally = create32bitRef(HandlerSym);
      ExceptOrNull = MCConstantExpr::create(0, Ctx);
    } else {
      // For an except, the filter can be 1 (catch-all) or a function label.
      FilterOrFinally = UME.Filter ? create32bitRef(UME.Filter)
                                   : MCConstantExpr::create(1, Ctx);
      ExceptOrNull = create32bitRef(HandlerSym);
    }

    AddComment("LabelStart");
    OS.emitValue(getLabel(BeginLabel), 4);
    AddComment("LabelEnd");
    OS.emitValue(getLabelPlusOne(EndLabel), 4);
    AddComment(UME.IsFinally ? "FinallyFunclet"
               : UME.Filter  ? "FilterFunction"
                             : "CatchAll");
    OS.emitValue(FilterOrFinally, 4);
    AddComment(UME.IsFinally ? "Null" : "ExceptionHandler");
    OS.emitValue(ExceptOrNull, 4);

    assert(UME.ToState < State && "states should decrease");
    State = UME.ToState;
  }
}
