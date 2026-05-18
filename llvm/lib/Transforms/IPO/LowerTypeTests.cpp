//===- LowerTypeTests.cpp - Lower type tests pass -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// C code never generates type metadata or type.test intrinsics (those are used
// for CFI on C++ virtual calls). Provide no-op stubs so the LTO pipeline can
// include the pass without pulling in the 2400-line implementation.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/LowerTypeTests.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"

using namespace llvm;

PreservedAnalyses LowerTypeTestsPass::run(Module &M, ModuleAnalysisManager &) {
  return PreservedAnalyses::all();
}

namespace llvm {
namespace lowertypetests {

bool BitSetInfo::containsGlobalOffset(uint64_t) const { return false; }
void BitSetInfo::print(raw_ostream &) const {}

BitSetInfo BitSetBuilder::build() {
  BitSetInfo BSI;
  BSI.ByteOffset = 0;
  BSI.BitSize = 0;
  BSI.AlignLog2 = 0;
  return BSI;
}

void GlobalLayoutBuilder::addFragment(const std::set<uint64_t> &) {}

void ByteArrayBuilder::allocate(const std::set<uint64_t> &, uint64_t,
                                uint64_t &AllocByteOffset, uint8_t &AllocMask) {
  AllocByteOffset = 0;
  AllocMask = 0;
}

bool isJumpTableCanonical(Function *) { return false; }

} // namespace lowertypetests
} // namespace llvm
