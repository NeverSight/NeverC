// Volume 6 task 7: the remaining builtin dyncode IR passes are plain, explicit
// typed transforms with no hidden per-run state.  CompilerRtPass no longer keys
// off a CompilerRtStampAnalysis cache entry (it is idempotent on re-entry) and
// Data2TextPass takes an explicit pre/post phase instead of a named-metadata
// sentinel.  These tests exercise that on real modules and check the IR
// transform phases are distinct, replaceable (non-sealed) transitions.
// Modules are built with the LLVM IR API (this LLVM tree ships no AsmParser).

#include "neverc/DynCode/IR/CompilerRtPass.h"
#include "neverc/DynCode/IR/Data2TextPass.h"
#include "neverc/DynCode/Pipeline/TargetDesc.h"

#include "DynCodePhaseRegistry.h"
#include "neverc/Plugin/Schema/PluginPhaseSchema.inc"

#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Error.h"
#include "gtest/gtest.h"

#include <set>

using namespace llvm;
using namespace neverc::dyncode;

namespace {

int countWideUDiv(Function &F) {
  int N = 0;
  for (BasicBlock &BB : F)
    for (Instruction &I : BB)
      if (auto *BO = dyn_cast<BinaryOperator>(&I))
        if (BO->getOpcode() == Instruction::UDiv &&
            BO->getType()->isIntegerTy(128))
          ++N;
  return N;
}

StoreInst *firstStore(Function &F) {
  for (BasicBlock &BB : F)
    for (Instruction &I : BB)
      if (auto *SI = dyn_cast<StoreInst>(&I))
        return SI;
  return nullptr;
}

TargetDesc linuxX86User() {
  TargetDesc T;
  T.OS = DynCodeOS::Linux;
  T.Arch = DynCodeArch::X86_64;
  T.Format = ObjectFormat::ELF;
  T.Level = ExecutionLevel::User;
  return T;
}

TEST(PluginDynCodeIRProviderTest, CompilerRtLowersWideDivAndIsIdempotent) {
  LLVMContext Context;
  Module M("t", Context);
  auto *I128 = Type::getInt128Ty(Context);
  auto *FT = FunctionType::get(I128, {I128, I128}, false);
  Function *Main =
      Function::Create(FT, GlobalValue::ExternalLinkage, "main", M);
  {
    BasicBlock *BB = BasicBlock::Create(Context, "", Main);
    IRBuilder<> B(BB);
    B.CreateRet(B.CreateUDiv(Main->getArg(0), Main->getArg(1)));
  }
  ASSERT_EQ(countWideUDiv(*Main), 1);

  ModuleAnalysisManager MAM;
  TargetDesc T = linuxX86User();

  CompilerRtPass(T).run(M, MAM);
  EXPECT_EQ(countWideUDiv(*Main), 0); // lowered to a runtime call
  size_t FnCount = M.size();

  // A second run must be idempotent: the wide divide stays lowered and no new
  // helper functions are synthesised (there is no stamp deciding to skip).
  CompilerRtPass(T).run(M, MAM);
  EXPECT_EQ(countWideUDiv(*Main), 0);
  EXPECT_EQ(M.size(), FnCount);
}

// A single-byte constant store is devectorized by marking it volatile; only the
// post phase runs that step.  This is a clean observable of the explicit
// pre/post phase input replacing the old named-metadata sentinel.
static Function *makeByteStoreMain(LLVMContext &Context, Module &M) {
  M.setDataLayout("e-m:e-i64:64-f80:128-n8:16:32:64-S128");
  auto *I8 = Type::getInt8Ty(Context);
  auto *PtrTy = PointerType::getUnqual(Context);
  auto *FT = FunctionType::get(Type::getVoidTy(Context), {PtrTy}, false);
  Function *Main =
      Function::Create(FT, GlobalValue::ExternalLinkage, "main", M);
  BasicBlock *BB = BasicBlock::Create(Context, "", Main);
  IRBuilder<> B(BB);
  B.CreateStore(ConstantInt::get(I8, 5), Main->getArg(0)); // non-volatile
  B.CreateRetVoid();
  return Main;
}

TEST(PluginDynCodeIRProviderTest, Data2TextPreLeavesByteStoreUnchanged) {
  LLVMContext Context;
  Module M("t", Context);
  Function *Main = makeByteStoreMain(Context, M);

  ModuleAnalysisManager MAM;
  Data2TextPass(/*IsLate=*/false).run(M, MAM);
  StoreInst *SI = firstStore(*Main);
  ASSERT_NE(SI, nullptr);
  EXPECT_FALSE(SI->isVolatile()); // pre phase does not devectorize
}

TEST(PluginDynCodeIRProviderTest, Data2TextPostDevectorizesWithoutSentinel) {
  LLVMContext Context;
  Module M("t", Context);
  Function *Main = makeByteStoreMain(Context, M);

  ModuleAnalysisManager MAM;
  // Post phase runs standalone (no prior pre-run set a sentinel) and still does
  // its extra devectorization work.
  Data2TextPass(/*IsLate=*/true).run(M, MAM);
  StoreInst *SI = firstStore(*Main);
  ASSERT_NE(SI, nullptr);
  EXPECT_TRUE(SI->isVolatile());
}

TEST(PluginDynCodeIRProviderTest, Data2TextEliminatesConstantGlobal) {
  LLVMContext Context;
  Module M("t", Context);
  M.setDataLayout("e-m:e-i64:64-f80:128-n8:16:32:64-S128");
  auto *I32 = Type::getInt32Ty(Context);
  auto *G = new GlobalVariable(M, I32, /*isConstant=*/true,
                               GlobalValue::InternalLinkage,
                               ConstantInt::get(I32, 42), "c");
  auto *FT = FunctionType::get(I32, false);
  Function *Main =
      Function::Create(FT, GlobalValue::ExternalLinkage, "main", M);
  {
    BasicBlock *BB = BasicBlock::Create(Context, "", Main);
    IRBuilder<> B(BB);
    B.CreateRet(B.CreateLoad(I32, G));
  }

  ModuleAnalysisManager MAM;
  // Both phases inline scalar constant globals; the pre phase suffices.
  Data2TextPass(/*IsLate=*/false).run(M, MAM);
  EXPECT_EQ(M.getNamedGlobal("c"), nullptr);
}

TEST(PluginDynCodeIRProviderTest, IRTransformPhasesAreDistinctReplaceable) {
  auto Registry = DynCodePhaseRegistry::create();
  if (!Registry) {
    consumeError(Registry.takeError());
    ADD_FAILURE() << "DynCodePhaseRegistry::create failed";
    return;
  }

  const NevercInterfaceID Phases[] = {
      {NEVERC_PHASE_DYNCODE_IR_MEM_INTRIN_PRE_HIGH,
       NEVERC_PHASE_DYNCODE_IR_MEM_INTRIN_PRE_LOW},
      {NEVERC_PHASE_DYNCODE_IR_MEM_INTRIN_POST_HEAP_HIGH,
       NEVERC_PHASE_DYNCODE_IR_MEM_INTRIN_POST_HEAP_LOW},
      {NEVERC_PHASE_DYNCODE_IR_COMPILER_RT_PRE_HIGH,
       NEVERC_PHASE_DYNCODE_IR_COMPILER_RT_PRE_LOW},
      {NEVERC_PHASE_DYNCODE_IR_COMPILER_RT_POST_HIGH,
       NEVERC_PHASE_DYNCODE_IR_COMPILER_RT_POST_LOW},
      {NEVERC_PHASE_DYNCODE_IR_COMPILER_RT_FINAL_HIGH,
       NEVERC_PHASE_DYNCODE_IR_COMPILER_RT_FINAL_LOW},
      {NEVERC_PHASE_DYNCODE_IR_DATA_TO_TEXT_PRE_HIGH,
       NEVERC_PHASE_DYNCODE_IR_DATA_TO_TEXT_PRE_LOW},
      {NEVERC_PHASE_DYNCODE_IR_DATA_TO_TEXT_POST_HIGH,
       NEVERC_PHASE_DYNCODE_IR_DATA_TO_TEXT_POST_LOW},
  };

  std::set<std::pair<uint64_t, uint64_t>> Seen;
  for (const NevercInterfaceID &ID : Phases) {
    const DynCodePhaseDefinition *Def = Registry->find(ID);
    ASSERT_NE(Def, nullptr);
    EXPECT_FALSE(Def->isSealedGate());
    // Each pre/post/final variant is its own phase with a distinct ID.
    EXPECT_TRUE(Seen.insert({ID.High, ID.Low}).second);
  }
}

} // namespace
