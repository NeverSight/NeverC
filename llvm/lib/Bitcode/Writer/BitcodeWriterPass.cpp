//===- BitcodeWriterPass.cpp - Bitcode writing pass -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// BitcodeWriterPass implementation.
//
//===----------------------------------------------------------------------===//

#include "llvm/Bitcode/BitcodeWriterPass.h"
#include "llvm/Analysis/ModuleSummaryAnalysis.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/PassManager.h"
using namespace llvm;

PreservedAnalyses BitcodeWriterPass::run(Module &M, ModuleAnalysisManager &AM) {
  // RemoveDIs: there's no bitcode representation of the DPValue debug-info,
  // convert to dbg.values before writing out.
  bool IsNewDbgInfoFormat = M.IsNewDbgInfoFormat;
  if (IsNewDbgInfoFormat)
    M.convertFromNewDbgValues();

  const ModuleSummaryIndex *Index =
      EmitSummaryIndex ? &(AM.getResult<ModuleSummaryIndexAnalysis>(M))
                       : nullptr;
  WriteBitcodeToFile(M, OS, ShouldPreserveUseListOrder, Index, EmitModuleHash);

  if (IsNewDbgInfoFormat)
    M.convertToNewDbgValues();

  return PreservedAnalyses::all();
}
