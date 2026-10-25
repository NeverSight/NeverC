// Native transaction instructions must retain their immediate and effects.
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicsX86.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"

#include <string>

using namespace llvm;

TEST(X86Transaction, AbortRequiresAnImmediateAndRetainsUnknownEffects) {
  LLVMContext Context;
  Module M("transaction-immediate", Context);
  IRBuilder<> Builder(Context);
  auto *Type =
      FunctionType::get(Builder.getVoidTy(), {Builder.getInt8Ty()}, false);
  auto *F = Function::Create(Type, GlobalValue::ExternalLinkage, "abort", M);
  Builder.SetInsertPoint(BasicBlock::Create(Context, "entry", F));
  auto *Declaration = Intrinsic::getDeclaration(&M, Intrinsic::x86_xabort);
  EXPECT_TRUE(Declaration->hasParamAttribute(0, Attribute::ImmArg));
  EXPECT_FALSE(Declaration->onlyReadsMemory());
  EXPECT_FALSE(Declaration->onlyWritesMemory());
  EXPECT_FALSE(Declaration->doesNotReturn());
  auto *Call = Builder.CreateCall(Declaration, {F->getArg(0)});
  Builder.CreateRetVoid();
  std::string Error;
  raw_string_ostream Stream(Error);
  EXPECT_TRUE(verifyModule(M, &Stream));
  EXPECT_NE(Error.find("immarg operand has non-immediate parameter"),
            std::string::npos)
      << Error;
  for (unsigned Byte : {0u, 127u, 128u, 255u}) {
    Call->setArgOperand(0, Builder.getInt8(Byte));
    EXPECT_FALSE(verifyModule(M, &errs()));
  }
}
