//===- EHPersonalities.cpp - Compute EH-related information ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/EHPersonalities.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

/// See if the given exception handling personality function is one that we
/// understand.  If so, return a description of it; otherwise return Unknown.
EHPersonality llvm::classifyEHPersonality(const Value *Pers) {
  const GlobalValue *F =
      Pers ? dyn_cast<GlobalValue>(Pers->stripPointerCasts()) : nullptr;
  if (!F || !F->getValueType() || !F->getValueType()->isFunctionTy())
    return EHPersonality::Unknown;
  return StringSwitch<EHPersonality>(F->getName())
      .Case("__gcc_personality_v0", EHPersonality::GNU_C)
      .Case("__gcc_personality_seh0", EHPersonality::GNU_C)
      .Case("__C_specific_handler", EHPersonality::MSVC_TableSEH)
      .Default(EHPersonality::Unknown);
}

StringRef llvm::getEHPersonalityName(EHPersonality Pers) {
  switch (Pers) {
  case EHPersonality::GNU_C:
    return "__gcc_personality_v0";
  case EHPersonality::MSVC_TableSEH:
    return "__C_specific_handler";
  case EHPersonality::Unknown:
    llvm_unreachable("Unknown EHPersonality!");
  }

  llvm_unreachable("Invalid EHPersonality!");
}

DenseMap<BasicBlock *, ColorVector> llvm::colorEHFunclets(Function &F) {
  SmallVector<std::pair<BasicBlock *, BasicBlock *>, 16> Worklist;
  BasicBlock *EntryBlock = &F.getEntryBlock();
  DenseMap<BasicBlock *, ColorVector> BlockColors;

  // Build up the color map, which maps each block to its set of 'colors'.
  // For any block B the "colors" of B are the set of funclets F (possibly
  // including a root "funclet" representing the main function) such that
  // F will need to directly contain B or a copy of B (where the term "directly
  // contain" is used to distinguish from being "transitively contained" in
  // a nested funclet).
  //
  // Note: Despite not being a funclet in the truest sense, a catchswitch is
  // considered to belong to its own funclet for the purposes of coloring.

  DEBUG_WITH_TYPE("win-eh-prepare-coloring",
                  dbgs() << "\nColoring funclets for " << F.getName() << "\n");

  Worklist.push_back({EntryBlock, EntryBlock});

  while (!Worklist.empty()) {
    BasicBlock *Visiting;
    BasicBlock *Color;
    std::tie(Visiting, Color) = Worklist.pop_back_val();
    DEBUG_WITH_TYPE("win-eh-prepare-coloring",
                    dbgs() << "Visiting " << Visiting->getName() << ", "
                           << Color->getName() << "\n");
    Instruction *VisitingHead = Visiting->getFirstNonPHI();
    if (VisitingHead->isEHPad()) {
      // Mark this funclet head as a member of itself.
      Color = Visiting;
    }
    // Note that this is a member of the given color.
    ColorVector &Colors = BlockColors[Visiting];
    if (!is_contained(Colors, Color))
      Colors.push_back(Color);
    else
      continue;

    DEBUG_WITH_TYPE("win-eh-prepare-coloring",
                    dbgs() << "  Assigned color \'" << Color->getName()
                           << "\' to block \'" << Visiting->getName()
                           << "\'.\n");

    BasicBlock *SuccColor = Color;
    Instruction *Terminator = Visiting->getTerminator();
    if (auto *CatchRet = dyn_cast<CatchReturnInst>(Terminator)) {
      Value *ParentPad = CatchRet->getCatchSwitchParentPad();
      if (isa<ConstantTokenNone>(ParentPad))
        SuccColor = EntryBlock;
      else
        SuccColor = cast<Instruction>(ParentPad)->getParent();
    }

    for (BasicBlock *Succ : successors(Visiting))
      Worklist.push_back({Succ, SuccColor});
  }
  return BlockColors;
}
