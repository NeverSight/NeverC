// Pointer intrinsics must not invent alignment or discard native RMW effects.
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicsX86.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "gtest/gtest.h"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

using namespace llvm;

TEST(X86AtomicMemory, UnqualifiedPointersKeepByteAlignmentAndRMWEffects) {
  LLVMInitializeX86TargetInfo();
  LLVMInitializeX86Target();
  LLVMInitializeX86TargetMC();
  const char *Triple = "x86_64-unknown-linux-gnu";
  std::string Error;
  const auto *Target = TargetRegistry::lookupTarget(Triple, Error);
  ASSERT_NE(Target, nullptr) << Error;
  std::unique_ptr<TargetMachine> TM(Target->createTargetMachine(
      Triple, "generic", "+raoint,+cmpccxadd", {}, std::nullopt));
  ASSERT_NE(TM, nullptr);
  LLVMContext Context;
  Module M("atomic-pointer-alignment", Context);
  M.setTargetTriple(Triple);
  M.setDataLayout(TM->createDataLayout());
  IRBuilder<> B(Context);
  auto *Type = FunctionType::get(B.getVoidTy(), {B.getPtrTy()}, false);
  auto *F = Function::Create(Type, GlobalValue::ExternalLinkage, "probe", M);
  B.SetInsertPoint(BasicBlock::Create(Context, "entry", F));
  auto *Return = B.CreateRetVoid();
  B.SetInsertPoint(Return);
  MachineModuleInfo MMI(static_cast<LLVMTargetMachine *>(TM.get()));
  auto &MF = MMI.getOrCreateMachineFunction(*F);
  const auto *Lowering = MF.getSubtarget().getTargetLowering();
  ASSERT_NE(Lowering, nullptr);

  using ID = Intrinsic::ID;
  std::vector<std::pair<ID, unsigned>> Cases = {{Intrinsic::x86_aadd32, 32},
                                                {Intrinsic::x86_aand32, 32},
                                                {Intrinsic::x86_aor32, 32},
                                                {Intrinsic::x86_axor32, 32}};
  Cases.insert(Cases.end(), {{Intrinsic::x86_aadd64, 64},
                             {Intrinsic::x86_aand64, 64},
                             {Intrinsic::x86_aor64, 64},
                             {Intrinsic::x86_axor64, 64},
                             {Intrinsic::x86_cmpccxadd32, 32},
                             {Intrinsic::x86_cmpccxadd64, 64}});
  for (unsigned Bits : {8u, 16u, 32u, 64u}) {
    for (ID I : {Intrinsic::x86_atomic_add_cc, Intrinsic::x86_atomic_sub_cc,
                 Intrinsic::x86_atomic_or_cc, Intrinsic::x86_atomic_and_cc,
                 Intrinsic::x86_atomic_xor_cc})
      Cases.emplace_back(I, Bits);
    if (Bits != 8)
      for (ID I : {Intrinsic::x86_atomic_bts, Intrinsic::x86_atomic_btc,
                   Intrinsic::x86_atomic_btr, Intrinsic::x86_atomic_bts_rm,
                   Intrinsic::x86_atomic_btc_rm, Intrinsic::x86_atomic_btr_rm})
        Cases.emplace_back(I, Bits);
  }
  for (auto [I, Bits] : Cases) {
    SCOPED_TRACE(Intrinsic::getBaseName(I).str());
    SCOPED_TRACE(Bits);
    SmallVector<llvm::Type *, 1> Overloads;
    if (Intrinsic::isOverloaded(I))
      Overloads.push_back(B.getIntNTy(Bits));
    auto *Declaration = Intrinsic::getDeclaration(&M, I, Overloads);
    SmallVector<Value *, 4> Args;
    for (auto *Parameter : Declaration->getFunctionType()->params())
      Args.push_back(Parameter->isPointerTy()
                         ? static_cast<Value *>(F->getArg(0))
                         : ConstantInt::get(Parameter, 3));
    auto *Call = B.CreateCall(Declaration, Args);
    ASSERT_FALSE(Call->getParamAlign(0));
    TargetLowering::IntrinsicInfo Info;
    ASSERT_TRUE(Lowering->getTgtMemIntrinsic(Info, *Call, MF, I));
    ASSERT_TRUE(Info.align);
    EXPECT_EQ(Info.align->value(), 1u);
    EXPECT_EQ(Info.memVT.getFixedSizeInBits(), Bits);
    EXPECT_EQ(dyn_cast_if_present<const Value *>(Info.ptrVal), F->getArg(0));
    EXPECT_EQ(Info.offset, 0);
    EXPECT_EQ(Info.flags, MachineMemOperand::MOLoad |
                              MachineMemOperand::MOStore |
                              MachineMemOperand::MOVolatile);
  }
}
