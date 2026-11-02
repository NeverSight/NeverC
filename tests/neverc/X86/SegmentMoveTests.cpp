//===- SegmentMoveTests.cpp - Native selector effects -------------------===//
//
// NeverC Compiler
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Function.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsX86.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "gtest/gtest.h"

#include <memory>
#include <optional>
#include <set>
#include <string>

using namespace llvm;

TEST(X86SegmentMove, ExplicitDefinitionsMemoryAndArithmeticFlags) {
  LLVMInitializeX86TargetInfo();
  LLVMInitializeX86Target();
  LLVMInitializeX86TargetMC();
  std::string Error;
  const char *Triple = "x86_64-unknown-linux-gnu";
  const auto *T = TargetRegistry::lookupTarget(Triple, Error);
  ASSERT_NE(T, nullptr) << Error;
  std::unique_ptr<TargetMachine> TM(
      T->createTargetMachine(Triple, "generic", "", {}, std::nullopt));
  const auto *Instructions = TM->getMCInstrInfo();
  const std::set<std::string> Names = {
      "MOV16rs",           "MOV32rs",          "MOV64rs", "MOV16ms",
      "MOV16sr",           "MOV32sr",          "MOV64sr", "MOV16sm",
      "WRITE_SEGMENT_REG", "WRITE_SEGMENT_MEM"};
  unsigned Matches = 0;
  for (unsigned I = 0; I < Instructions->getNumOpcodes(); ++I) {
    const auto Name = Instructions->getName(I);
    if (!Names.count(Name.str()))
      continue;
    SCOPED_TRACE(Name.str());
    ++Matches;
    const auto &Inst = Instructions->get(I);
    const bool Pseudo = Name.starts_with("WRITE_");
    const bool Write = Pseudo || Name.ends_with("sr") || Name.ends_with("sm");
    EXPECT_EQ(Inst.getNumDefs(), (Pseudo || Name == "MOV16ms") ? 0U : 1U);
    EXPECT_TRUE(Inst.hasUnmodeledSideEffects());
    EXPECT_EQ(Inst.mayLoad(), Write);
    EXPECT_EQ(Inst.mayStore(), Write || Name == "MOV16ms");
    EXPECT_TRUE(Inst.implicit_defs().empty());
    EXPECT_TRUE(Inst.implicit_uses().empty());
    EXPECT_EQ(Inst.usesCustomInsertionHook(), Pseudo);
  }
  EXPECT_EQ(Matches, Names.size());
}

TEST(X86SegmentMove, IntrinsicsRetainImmediateSelectorAndUnknownEffects) {
  LLVMContext C;
  Module M("segments", C);
  for (auto ID : {Intrinsic::x86_readseg, Intrinsic::x86_readseg_mem,
                  Intrinsic::x86_writeseg, Intrinsic::x86_writeseg_mem}) {
    auto *F = Intrinsic::getDeclaration(&M, ID);
    SCOPED_TRACE(F->getName().str());
    EXPECT_EQ(F->arg_size(), ID == Intrinsic::x86_readseg ? 1U : 2U);
    EXPECT_TRUE(F->getArg(0)->getType()->isIntegerTy(32));
    EXPECT_TRUE(F->hasParamAttribute(0, Attribute::ImmArg));
    if (ID == Intrinsic::x86_readseg)
      EXPECT_TRUE(F->getReturnType()->isIntegerTy(16));
    else {
      EXPECT_TRUE(F->getReturnType()->isVoidTy());
      if (ID == Intrinsic::x86_writeseg)
        EXPECT_TRUE(F->getArg(1)->getType()->isIntegerTy(16));
      else
        EXPECT_TRUE(F->getArg(1)->getType()->isPointerTy());
    }
    EXPECT_FALSE(F->onlyReadsMemory());
    EXPECT_FALSE(F->onlyWritesMemory());
    EXPECT_FALSE(F->hasFnAttribute(Attribute::WillReturn));
    EXPECT_FALSE(F->hasFnAttribute(Attribute::Speculatable));
  }
}
