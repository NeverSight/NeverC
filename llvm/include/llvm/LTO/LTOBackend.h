//===-LTOBackend.h - LLVM Link Time Optimizer Backend ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the "backend" phase of LTO, i.e. it performs
// optimization and code generation on a loaded module. It is generally used
// internally by the LTO class but can also be used independently.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LTO_LTOBACKEND_H
#define LLVM_LTO_LTOBACKEND_H

#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Target/TargetOptions.h"

namespace llvm {

class BitcodeModule;
class Error;
class Module;
class Target;

namespace lto {

/// Runs middle-end LTO optimizations on \p Mod.
bool opt(const Config &Conf, TargetMachine *TM, unsigned Task, Module &Mod,
         ModuleSummaryIndex *ExportSummary);

/// Runs the LTO backend (optimization + code generation).
Error backend(const Config &C, AddStreamFn AddStream,
              unsigned ParallelCodeGenParallelismLevel, Module &M,
              ModuleSummaryIndex &CombinedIndex);

Error finalizeOptimizationRemarks(
    std::unique_ptr<ToolOutputFile> DiagOutputFile);
} // namespace lto
} // namespace llvm

#endif
