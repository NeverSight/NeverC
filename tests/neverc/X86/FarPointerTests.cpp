//===- FarPointerTests.cpp - Native far-pointer effects ------------------===//
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

TEST(X86FarPointer, SegmentDefinitionsAndDescriptorEffectsAreRetained) {
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
  const auto *Registers = TM->getMCRegisterInfo();
  unsigned Matches = 0;
  for (unsigned I = 0; I < Instructions->getNumOpcodes(); ++I) {
    const auto Name = Instructions->getName(I);
    if (!(Name.starts_with("LFS") || Name.starts_with("LGS") ||
          Name.starts_with("LSS")) ||
        !Name.ends_with("rm"))
      continue;
    SCOPED_TRACE(Name.str());
    ++Matches;
    const auto &Inst = Instructions->get(I);
    EXPECT_EQ(Inst.getNumDefs(), 1U);
    EXPECT_TRUE(Inst.hasUnmodeledSideEffects());
    EXPECT_TRUE(Inst.mayLoad());
    EXPECT_TRUE(Inst.mayStore());
    std::set<std::string> Defs;
    for (auto Reg : Inst.implicit_defs())
      Defs.insert(Registers->getName(Reg));
    EXPECT_EQ(Defs, (std::set<std::string>{Name.substr(1, 2).str()}));
  }
  EXPECT_EQ(Matches, 9U);
  LLVMContext C;
  Module M("far-pointer", C);
  for (auto ID :
       {Intrinsic::x86_lds16, Intrinsic::x86_lds32, Intrinsic::x86_les16,
        Intrinsic::x86_les32, Intrinsic::x86_lfs16, Intrinsic::x86_lfs32,
        Intrinsic::x86_lfs64, Intrinsic::x86_lgs16, Intrinsic::x86_lgs32,
        Intrinsic::x86_lgs64, Intrinsic::x86_lss16, Intrinsic::x86_lss32,
        Intrinsic::x86_lss64}) {
    auto *F = Intrinsic::getDeclaration(&M, ID);
    EXPECT_TRUE(F->getReturnType()->isIntegerTy());
    ASSERT_EQ(F->arg_size(), 1U);
    EXPECT_TRUE(F->getArg(0)->getType()->isPointerTy());
    EXPECT_FALSE(F->onlyReadsMemory());
    EXPECT_FALSE(F->onlyWritesMemory());
    EXPECT_FALSE(F->hasFnAttribute(Attribute::WillReturn));
    EXPECT_FALSE(F->hasFnAttribute(Attribute::Speculatable));
  }
}
