//===- X87ControlTests.cpp - Native x87 control-word effects
//----------------===//
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
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "gtest/gtest.h"

#include <memory>
#include <optional>
#include <set>
#include <string>

using namespace llvm;

TEST(X86X87Control, MemoryAndWaitingEffectsAreDistinct) {
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
    const auto Name = Instructions->getName(I);
    if (Name != "FNSTCW16m" && Name != "FLDCW16m")
      continue;
    SCOPED_TRACE(Name.str());
    ++Matches;
    const bool Load = Name == "FLDCW16m";
    const auto &Instruction = Instructions->get(I);
    EXPECT_TRUE(Instruction.hasUnmodeledSideEffects());
    EXPECT_EQ(Instruction.mayRaiseFPException(), Load);
    EXPECT_EQ(Instruction.mayLoad(), Load);
    EXPECT_EQ(Instruction.mayStore(), !Load);
    std::set<std::string> Uses, Defs;
    for (auto Register : Instruction.implicit_uses())
      Uses.insert(Registers->getName(Register));
    for (auto Register : Instruction.implicit_defs())
      Defs.insert(Registers->getName(Register));
    EXPECT_EQ(Uses, (Load ? std::set<std::string>{"FPCW", "FPSW"}
                          : std::set<std::string>{"FPCW"}));
    EXPECT_EQ(Defs, (Load ? std::set<std::string>{"FPCW", "FPSW"}
                          : std::set<std::string>{"FPSW"}));
  }
  EXPECT_EQ(Matches, 2U);
  LLVMContext Context;
  Module M("x87-control", Context);
  for (auto ID : {Intrinsic::x86_fnstcw, Intrinsic::x86_fldcw}) {
    auto *F = Intrinsic::getDeclaration(&M, ID);
    EXPECT_TRUE(F->getReturnType()->isVoidTy());
    ASSERT_EQ(F->arg_size(), 1U);
    EXPECT_TRUE(F->getArg(0)->getType()->isPointerTy());
    EXPECT_FALSE(F->onlyReadsMemory());
    EXPECT_FALSE(F->onlyWritesMemory());
    EXPECT_FALSE(F->hasFnAttribute(Attribute::WillReturn));
    EXPECT_FALSE(F->hasFnAttribute(Attribute::Speculatable));
  }
}

TEST(X86X87Control, StatusResetAndWaitRetainArchitecturalDependencies) {
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
    const auto Name = Instructions->getName(I);
    if (Name != "FNSTSW16r" && Name != "FNSTSWm" && Name != "FNCLEX" &&
        Name != "FNINIT" && Name != "WAIT")
      continue;
    SCOPED_TRACE(Name.str());
    ++Matches;
    const auto &Instruction = Instructions->get(I);
    EXPECT_TRUE(Instruction.hasUnmodeledSideEffects());
    EXPECT_EQ(Instruction.mayRaiseFPException(), Name == "WAIT");
    EXPECT_FALSE(Instruction.mayLoad());
    EXPECT_EQ(Instruction.mayStore(), Name == "FNSTSWm");
    std::set<std::string> Uses, Defs, ExpectedUses, ExpectedDefs;
    for (auto Register : Instruction.implicit_uses())
      Uses.insert(Registers->getName(Register));
    for (auto Register : Instruction.implicit_defs())
      Defs.insert(Registers->getName(Register));
    if (Name != "FNINIT")
      ExpectedUses.insert("FPSW");
    if (Name == "WAIT")
      ExpectedUses.insert("FPCW");
    ExpectedDefs.insert("FPSW");
    if (Name == "FNSTSW16r")
      ExpectedDefs.insert("AX");
    if (Name == "FNINIT") {
      ExpectedDefs.insert("FPCW");
      for (const char *Bank : {"FP", "ST", "MM"})
        for (unsigned J = 0; J < 8; ++J)
          ExpectedDefs.insert(Bank + std::to_string(J));
    }
    EXPECT_EQ(Uses, ExpectedUses);
    EXPECT_EQ(Defs, ExpectedDefs);
  }
  EXPECT_EQ(Matches, 5U);
  LLVMContext Context;
  Module M("x87-status", Context);
  for (auto ID :
       {Intrinsic::x86_fnstsw, Intrinsic::x86_fnstsw_mem, Intrinsic::x86_fnclex,
        Intrinsic::x86_fninit, Intrinsic::x86_fwait}) {
    auto *F = Intrinsic::getDeclaration(&M, ID);
    EXPECT_EQ(F->getReturnType()->isIntegerTy(16), ID == Intrinsic::x86_fnstsw);
    EXPECT_EQ(F->arg_size(), ID == Intrinsic::x86_fnstsw_mem ? 1U : 0U);
    EXPECT_FALSE(F->onlyReadsMemory());
    EXPECT_FALSE(F->onlyWritesMemory());
    EXPECT_FALSE(F->hasFnAttribute(Attribute::WillReturn));
    EXPECT_FALSE(F->hasFnAttribute(Attribute::Speculatable));
  }
}
