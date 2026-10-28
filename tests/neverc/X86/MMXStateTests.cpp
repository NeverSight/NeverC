// Native FEMMS must describe every MMX/x87 payload it invalidates.
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicsX86.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "gtest/gtest.h"

#include <memory>
#include <set>
#include <string>

using namespace llvm;

TEST(X86MMXState, FnopRetainsWaitingAndStatusDependencies) {
  LLVMInitializeX86TargetInfo();
  LLVMInitializeX86Target();
  LLVMInitializeX86TargetMC();
  const char *Triple = "x86_64-unknown-linux-gnu";
  std::string Error;
  const auto *Target = TargetRegistry::lookupTarget(Triple, Error);
  ASSERT_NE(Target, nullptr) << Error;
  std::unique_ptr<TargetMachine> TM(
      Target->createTargetMachine(Triple, "generic", "", {}, std::nullopt));
  ASSERT_NE(TM, nullptr);
  const auto *Instructions = TM->getMCInstrInfo();
  const auto *Registers = TM->getMCRegisterInfo();
  unsigned Matches = 0;
  for (unsigned I = 0; I < Instructions->getNumOpcodes(); ++I) {
    if (Instructions->getName(I) != "FNOP")
      continue;
    ++Matches;
    const auto &Instruction = Instructions->get(I);
    EXPECT_TRUE(Instruction.hasUnmodeledSideEffects());
    EXPECT_TRUE(Instruction.mayRaiseFPException());
    EXPECT_TRUE(Instruction.mayLoad());
    EXPECT_TRUE(Instruction.mayStore());
    std::set<std::string> Uses, Defs;
    for (auto Register : Instruction.implicit_uses())
      Uses.insert(Registers->getName(Register));
    for (auto Register : Instruction.implicit_defs())
      Defs.insert(Registers->getName(Register));
    EXPECT_EQ(Uses, (std::set<std::string>{"FPCW", "FPSW"}));
    EXPECT_EQ(Defs, (std::set<std::string>{"FPSW"}));
  }
  EXPECT_EQ(Matches, 1U);
  LLVMContext Context;
  Module M("fnop-state", Context);
  auto *F = Intrinsic::getDeclaration(&M, Intrinsic::x86_fnop);
  EXPECT_TRUE(F->getReturnType()->isVoidTy());
  EXPECT_TRUE(F->arg_empty());
  EXPECT_FALSE(F->onlyReadsMemory());
  EXPECT_FALSE(F->onlyWritesMemory());
  EXPECT_FALSE(F->doesNotReturn());
  EXPECT_FALSE(F->hasFnAttribute(Attribute::WillReturn));
}

TEST(X86MMXState, FemmsInvalidatesTheCompleteRegisterBank) {
  LLVMInitializeX86TargetInfo();
  LLVMInitializeX86Target();
  LLVMInitializeX86TargetMC();
  // The pruned target registry exposes x64; full-fork codegen tests cover
  // i386 separately.
  {
    const char *Triple = "x86_64-unknown-linux-gnu";
    SCOPED_TRACE(Triple);
    std::string Error;
    const auto *Target = TargetRegistry::lookupTarget(Triple, Error);
    ASSERT_NE(Target, nullptr) << Error;
    std::unique_ptr<TargetMachine> TM(Target->createTargetMachine(
        Triple, "generic", "+3dnow", {}, std::nullopt));
    ASSERT_NE(TM, nullptr);
    const auto *Instructions = TM->getMCInstrInfo();
    const auto *Registers = TM->getMCRegisterInfo();
    unsigned Matches = 0;
    for (unsigned I = 0; I < Instructions->getNumOpcodes(); ++I) {
      if (Instructions->getName(I) != "FEMMS")
        continue;
      ++Matches;
      const auto &Instruction = Instructions->get(I);
      EXPECT_TRUE(Instruction.hasUnmodeledSideEffects());
      EXPECT_TRUE(Instruction.mayLoad());
      EXPECT_TRUE(Instruction.mayStore());
      std::set<std::string> Actual, Expected;
      for (auto Register : Instruction.implicit_defs())
        Actual.insert(Registers->getName(Register));
      for (unsigned Slot = 0; Slot != 8; ++Slot) {
        Expected.insert("MM" + std::to_string(Slot));
        Expected.insert("ST" + std::to_string(Slot));
      }
      EXPECT_EQ(Actual, Expected);
    }
    EXPECT_EQ(Matches, 1u);
  }
  LLVMContext Context;
  Module M("femms-state", Context);
  auto *F = Intrinsic::getDeclaration(&M, Intrinsic::x86_mmx_femms);
  EXPECT_TRUE(F->getReturnType()->isVoidTy());
  EXPECT_TRUE(F->arg_empty());
  EXPECT_FALSE(F->onlyReadsMemory());
  EXPECT_FALSE(F->onlyWritesMemory());
  EXPECT_FALSE(F->doesNotReturn());
}
