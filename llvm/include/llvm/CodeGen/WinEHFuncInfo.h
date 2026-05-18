//===- llvm/CodeGen/WinEHFuncInfo.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Data structures and associated state for Windows exception handling schemes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_WINEHFUNCINFO_H
#define LLVM_CODEGEN_WINEHFUNCINFO_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SmallVector.h"
#include <cstdint>
#include <limits>
#include <utility>

namespace llvm {

class BasicBlock;
class FuncletPadInst;
class Function;
class Instruction;
class InvokeInst;
class MachineBasicBlock;
class MCSymbol;

using MBBOrBasicBlock = PointerUnion<const BasicBlock *, MachineBasicBlock *>;

/// SEH unwind map entry — supports SEH filters.
struct SEHUnwindMapEntry {
  /// If unwinding continues through this handler, transition to the handler at
  /// this state. This indexes into SEHUnwindMap.
  int ToState = -1;

  bool IsFinally = false;

  /// Holds the filter expression function.
  const Function *Filter = nullptr;

  /// Holds the __except or __finally basic block.
  MBBOrBasicBlock Handler;
};

struct WinEHFuncInfo {
  DenseMap<const Instruction *, int> EHPadStateMap;
  DenseMap<const FuncletPadInst *, int> FuncletBaseStateMap;
  DenseMap<const InvokeInst *, int> InvokeStateMap;
  DenseMap<MCSymbol *, std::pair<int, MCSymbol *>> LabelToStateMap;
  DenseMap<const BasicBlock *, int> BlockToStateMap;
  SmallVector<SEHUnwindMapEntry, 4> SEHUnwindMap;
  int UnwindHelpFrameIdx = std::numeric_limits<int>::max();
  int PSPSymFrameIdx = std::numeric_limits<int>::max();

  void addIPToStateRange(const InvokeInst *II, MCSymbol *InvokeBegin,
                         MCSymbol *InvokeEnd);

  void addIPToStateRange(int State, MCSymbol *InvokeBegin, MCSymbol *InvokeEnd);

  int SEHSetFrameOffset = std::numeric_limits<int>::max();

  WinEHFuncInfo();
};

void calculateSEHStateNumbers(const Function *ParentFn,
                              WinEHFuncInfo &FuncInfo);

void calculateSEHStateForWinEH(const BasicBlock *BB, int State,
                               WinEHFuncInfo &FuncInfo);

} // end namespace llvm

#endif // LLVM_CODEGEN_WINEHFUNCINFO_H
