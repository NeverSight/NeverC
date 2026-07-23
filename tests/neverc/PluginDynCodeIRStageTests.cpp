// Unit tests for the split dyncode IR stage.  ZeroRelocPass was
// broken into DynCodePreparePass (early), StackifyPass (late) and the sealed
// DynCodeIRVerifier gate (dyncode.ir.final_verify), eliminating the run-count
// named-metadata sentinel.  These tests fix the entry/linkage/TLS/ctor/mutable
// global/computed-goto semantics on real modules and confirm the phase policies.
// Modules are built with the LLVM IR API (this LLVM tree ships no AsmParser).

#include "neverc/DynCode/IR/DynCodeIRVerifier.h"
#include "neverc/DynCode/IR/DynCodePreparePass.h"
#include "neverc/DynCode/IR/StackifyPass.h"

#include "DynCodePhaseRegistry.h"
#include "neverc/Plugin/Schema/PluginPhaseSchema.inc"

#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"

#include <memory>
#include <string>

using namespace llvm;
using namespace neverc::dyncode;

namespace {

// Captures error-severity diagnostics and fully handles them so the default
// LLVMContext behaviour (print to stderr / exit) never fires during the test.
struct CapturingDiagnosticHandler : public DiagnosticHandler {
  bool *SawError;
  std::string *Message;
  CapturingDiagnosticHandler(bool *E, std::string *M)
      : SawError(E), Message(M) {}
  bool handleDiagnostics(const DiagnosticInfo &DI) override {
    if (DI.getSeverity() == DS_Error) {
      *SawError = true;
      if (Message) {
        raw_string_ostream OS(*Message);
        DiagnosticPrinterRawOStream DP(OS);
        DI.print(DP);
      }
    }
    return true;
  }
};

class PluginDynCodeIRStageTest : public ::testing::Test {
protected:
  LLVMContext Context;
  ModuleAnalysisManager MAM;
  bool SawError = false;
  std::string Message;

  void SetUp() override {
    Context.setDiagnosticHandler(
        std::make_unique<CapturingDiagnosticHandler>(&SawError, &Message));
  }

  IntegerType *i32() { return Type::getInt32Ty(Context); }
  PointerType *ptr() { return PointerType::getUnqual(Context); }
  FunctionType *i32Fn() { return FunctionType::get(i32(), false); }

  // Creates `define i32 @<name>() { ret i32 <ret> }` and returns it.
  Function *addLeaf(Module &M, StringRef Name, int Ret) {
    Function *F =
        Function::Create(i32Fn(), GlobalValue::ExternalLinkage, Name, M);
    BasicBlock *BB = BasicBlock::Create(Context, "", F);
    IRBuilder<> B(BB);
    B.CreateRet(B.getInt32(Ret));
    return F;
  }

  static bool hasAlloca(Function &F) {
    for (BasicBlock &BB : F)
      for (Instruction &I : BB)
        if (isa<AllocaInst>(I))
          return true;
    return false;
  }
};

TEST_F(PluginDynCodeIRStageTest, PrepareChoosesEntryAndNormalisesLinkage) {
  Module M("t", Context);
  Function *Helper = addLeaf(M, "helper", 42);
  Function *Main =
      Function::Create(i32Fn(), GlobalValue::ExternalLinkage, "main", M);
  {
    BasicBlock *BB = BasicBlock::Create(Context, "", Main);
    IRBuilder<> B(BB);
    Value *R = B.CreateCall(Helper);
    B.CreateRet(R);
  }

  DynCodePreparePass("main").run(M, MAM);
  EXPECT_FALSE(SawError) << Message;

  EXPECT_TRUE(Main->hasExternalLinkage());
  EXPECT_TRUE(Main->isDSOLocal());
  EXPECT_TRUE(Helper->hasInternalLinkage());
}

TEST_F(PluginDynCodeIRStageTest, PrepareDemotesThreadLocalStorage) {
  Module M("t", Context);
  auto *G =
      new GlobalVariable(M, i32(), /*isConstant=*/false,
                         GlobalValue::ExternalLinkage, ConstantInt::get(i32(), 0),
                         "tls", nullptr, GlobalValue::GeneralDynamicTLSModel);
  ASSERT_TRUE(G->isThreadLocal());
  addLeaf(M, "main", 0);

  DynCodePreparePass("main").run(M, MAM);
  EXPECT_FALSE(SawError) << Message;

  GlobalVariable *TLS = M.getNamedGlobal("tls");
  ASSERT_NE(TLS, nullptr);
  EXPECT_FALSE(TLS->isThreadLocal());
}

TEST_F(PluginDynCodeIRStageTest, PrepareRejectsGlobalConstructors) {
  Module M("t", Context);
  Function *Ctor =
      Function::Create(FunctionType::get(Type::getVoidTy(Context), false),
                       GlobalValue::InternalLinkage, "ctor", M);
  {
    BasicBlock *BB = BasicBlock::Create(Context, "", Ctor);
    IRBuilder<> B(BB);
    B.CreateRetVoid();
  }
  addLeaf(M, "main", 0);

  StructType *ElemTy = StructType::get(i32(), ptr(), ptr());
  ArrayType *ArrTy = ArrayType::get(ElemTy, 1);
  Constant *Elem = ConstantStruct::get(
      ElemTy, {ConstantInt::get(i32(), 65535), Ctor,
               ConstantPointerNull::get(ptr())});
  Constant *Arr = ConstantArray::get(ArrTy, {Elem});
  new GlobalVariable(M, ArrTy, /*isConstant=*/false,
                     GlobalValue::AppendingLinkage, Arr, "llvm.global_ctors");

  DynCodePreparePass("main").run(M, MAM);
  EXPECT_TRUE(SawError);
}

TEST_F(PluginDynCodeIRStageTest, StackifyMovesMutableGlobalIntoEntryAlloca) {
  Module M("t", Context);
  auto *G = new GlobalVariable(M, i32(), /*isConstant=*/false,
                               GlobalValue::ExternalLinkage,
                               ConstantInt::get(i32(), 5), "g");
  Function *Main =
      Function::Create(i32Fn(), GlobalValue::ExternalLinkage, "main", M);
  {
    BasicBlock *BB = BasicBlock::Create(Context, "", Main);
    IRBuilder<> B(BB);
    Value *V = B.CreateLoad(i32(), G);
    B.CreateStore(B.getInt32(10), G);
    B.CreateRet(V);
  }

  // StackifyPass re-runs prepare internally, then stackifies.
  StackifyPass("main").run(M, MAM);
  EXPECT_FALSE(SawError) << Message;

  EXPECT_EQ(M.getNamedGlobal("g"), nullptr);
  EXPECT_TRUE(hasAlloca(*Main));
}

TEST_F(PluginDynCodeIRStageTest, VerifierRejectsBlockAddress) {
  Module M("t", Context);
  Function *Main =
      Function::Create(i32Fn(), GlobalValue::ExternalLinkage, "main", M);
  BasicBlock *Entry = BasicBlock::Create(Context, "entry", Main);
  BasicBlock *Lbl = BasicBlock::Create(Context, "lbl", Main);
  {
    IRBuilder<> B(Entry);
    B.CreateBr(Lbl);
    IRBuilder<> B2(Lbl);
    B2.CreateRet(B2.getInt32(0));
  }
  new GlobalVariable(M, ptr(), /*isConstant=*/false,
                     GlobalValue::ExternalLinkage, BlockAddress::get(Main, Lbl),
                     "ba");

  DynCodeIRVerifier().run(M, MAM);
  EXPECT_TRUE(SawError);
}

TEST_F(PluginDynCodeIRStageTest, VerifierAcceptsCleanModule) {
  Module M("t", Context);
  addLeaf(M, "main", 0);

  DynCodeIRVerifier().run(M, MAM);
  EXPECT_FALSE(SawError) << Message;
}

TEST_F(PluginDynCodeIRStageTest, FullIRStagePlacesEntryFirstAndStackifies) {
  Module M("t", Context);
  auto *G = new GlobalVariable(M, i32(), /*isConstant=*/false,
                               GlobalValue::ExternalLinkage,
                               ConstantInt::get(i32(), 0), "counter");
  addLeaf(M, "helper", 7); // defined before main to exercise placeEntryFirst
  Function *Main =
      Function::Create(i32Fn(), GlobalValue::ExternalLinkage, "main", M);
  {
    BasicBlock *BB = BasicBlock::Create(Context, "", Main);
    IRBuilder<> B(BB);
    Value *V = B.CreateLoad(i32(), G);
    Value *N = B.CreateAdd(V, B.getInt32(1));
    B.CreateStore(N, G);
    B.CreateRet(N);
  }

  DynCodePreparePass("main").run(M, MAM);
  EXPECT_FALSE(SawError) << Message;
  StackifyPass("main").run(M, MAM);
  EXPECT_FALSE(SawError) << Message;
  DynCodeIRVerifier().run(M, MAM);
  EXPECT_FALSE(SawError) << Message;

  EXPECT_EQ(M.getNamedGlobal("counter"), nullptr);
  EXPECT_EQ(&M.getFunctionList().front(), Main);
}

TEST(PluginDynCodeIRStagePhasePolicy, FinalVerifyIsSealedPrepareAndStackifyAreNot) {
  auto Registry = DynCodePhaseRegistry::create();
  if (!Registry) {
    consumeError(Registry.takeError());
    ADD_FAILURE() << "DynCodePhaseRegistry::create failed";
    return;
  }

  NevercInterfaceID Prepare{NEVERC_PHASE_DYNCODE_IR_PREPARE_HIGH,
                            NEVERC_PHASE_DYNCODE_IR_PREPARE_LOW};
  NevercInterfaceID Stackify{NEVERC_PHASE_DYNCODE_IR_STACKIFY_HIGH,
                             NEVERC_PHASE_DYNCODE_IR_STACKIFY_LOW};
  NevercInterfaceID FinalVerify{NEVERC_PHASE_DYNCODE_IR_FINAL_VERIFY_HIGH,
                                NEVERC_PHASE_DYNCODE_IR_FINAL_VERIFY_LOW};

  const DynCodePhaseDefinition *PrepDef = Registry->find(Prepare);
  const DynCodePhaseDefinition *StkDef = Registry->find(Stackify);
  const DynCodePhaseDefinition *FinalDef = Registry->find(FinalVerify);
  ASSERT_NE(PrepDef, nullptr);
  ASSERT_NE(StkDef, nullptr);
  ASSERT_NE(FinalDef, nullptr);

  EXPECT_FALSE(PrepDef->isSealedGate());
  EXPECT_FALSE(StkDef->isSealedGate());
  EXPECT_TRUE(FinalDef->isSealedGate());
}

} // namespace
