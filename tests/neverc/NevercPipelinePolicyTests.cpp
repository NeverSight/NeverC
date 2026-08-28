//===- NevercPipelinePolicyTests.cpp - Policy consumer contracts ----------===//

#include "Backend/ParallelCodeGenMergeInternal.h"

#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/Transforms/IPO/ModuleInliner.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include "llvm/Transforms/Scalar/LoopUnrollPass.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace llvm {

// ScalarEvolution already grants this ABI test helper friendship. Pin the
// first private-field offsets as well as the constructor surface: the removed
// threshold member fit inside existing padding, so sizeof alone could not
// detect that layout regression.
class ScalarEvolutionsTest {
public:
  static constexpr std::size_t hasGuardsOffset() {
    return offsetof(ScalarEvolution, HasGuards);
  }
  static constexpr std::size_t targetLibraryInfoOffset() {
    return offsetof(ScalarEvolution, TLI);
  }
};

} // namespace llvm

using namespace llvm;

// These public value types cross the lockstep LLVM C++ boundary. Keep the
// pre-request-policy layouts source-compatible: request-local SCEV state lives
// behind LLVMContext's existing pImpl instead.
static_assert(std::is_empty_v<ScalarEvolutionAnalysis>);
static_assert(sizeof(PipelineTuningOptions) == 36);
static_assert(
    !std::is_constructible_v<ScalarEvolution, Function &, TargetLibraryInfo &,
                             AssumptionCache &, DominatorTree &, LoopInfo &,
                             std::optional<unsigned>>);
static_assert(ScalarEvolutionsTest::hasGuardsOffset() == sizeof(void *));
static_assert(ScalarEvolutionsTest::targetLibraryInfoOffset() ==
              2 * sizeof(void *));

namespace {

std::unique_ptr<TargetMachine> createNativeTargetMachine() {
  static const bool Initialized = [] {
    return !InitializeNativeTarget() && !InitializeNativeTargetAsmPrinter();
  }();
  if (!Initialized)
    return nullptr;

  const std::string TripleName = sys::getDefaultTargetTriple();
  std::string Error;
  const Target *TheTarget = TargetRegistry::lookupTarget(TripleName, Error);
  if (!TheTarget)
    return nullptr;

  TargetOptions Options;
  Options.MCOptions.MCIncrementalLinkerCompatible = false;
  return std::unique_ptr<TargetMachine>(
      TheTarget->createTargetMachine(TripleName, "generic", "", Options,
                                     std::nullopt, CodeGenOptLevel::Default));
}

BasicBlock *appendRuntimeLoop(Function &F, BasicBlock *Preheader,
                              Function &SideEffect, Value *Limit,
                              StringRef Name) {
  LLVMContext &Context = F.getContext();
  Type *I32 = Type::getInt32Ty(Context);
  BasicBlock *Header =
      BasicBlock::Create(Context, (Name + ".header").str(), &F);
  BasicBlock *Exit = BasicBlock::Create(Context, (Name + ".exit").str(), &F);

  IRBuilder<> Builder(Preheader);
  Builder.CreateBr(Header);
  Builder.SetInsertPoint(Header);
  PHINode *Iteration = Builder.CreatePHI(I32, 2, (Name + ".iv").str());
  Iteration->addIncoming(ConstantInt::get(I32, 0), Preheader);
  Builder.CreateCall(&SideEffect, {Iteration});
  Value *Next = Builder.CreateAdd(Iteration, ConstantInt::get(I32, 1), "next");
  Value *Continue = Builder.CreateICmpULT(Next, Limit, "continue");
  Builder.CreateCondBr(Continue, Header, Exit);
  Iteration->addIncoming(Next, Header);
  return Exit;
}

struct ModuleInlinerFixture {
  std::unique_ptr<Module> M;
  Function *Caller = nullptr;
};

ModuleInlinerFixture createModuleInlinerFixture(LLVMContext &Context,
                                                TargetMachine &Machine) {
  auto M = std::make_unique<Module>("neverc-module-inliner-loop-cap", Context);
  M->setTargetTriple(Machine.getTargetTriple().str());
  M->setDataLayout(Machine.createDataLayout());

  Type *Void = Type::getVoidTy(Context);
  Type *I32 = Type::getInt32Ty(Context);
  FunctionType *UnaryTy = FunctionType::get(Void, {I32}, false);
  Function *SideEffect = Function::Create(UnaryTy, GlobalValue::ExternalLinkage,
                                          "neverc_policy_side_effect", *M);

  auto AddLoopLeaf = [&](StringRef Name, bool AlwaysInline) {
    Function *Leaf =
        Function::Create(UnaryTy, GlobalValue::InternalLinkage, Name, *M);
    if (AlwaysInline)
      Leaf->addFnAttr(Attribute::AlwaysInline);
    BasicBlock *Entry = BasicBlock::Create(Context, "entry", Leaf);
    BasicBlock *Exit = appendRuntimeLoop(*Leaf, Entry, *SideEffect,
                                         Leaf->getArg(0), "leaf.loop");
    IRBuilder<>(Exit).CreateRetVoid();
    return Leaf;
  };

  Function *OrdinaryLeaf = AddLoopLeaf("neverc_loop_leaf", false);
  Function *MandatoryLeaf = AddLoopLeaf("neverc_mandatory_loop_leaf", true);
  Function *Caller = Function::Create(UnaryTy, GlobalValue::ExternalLinkage,
                                      "neverc_loop_dense_caller", *M);
  BasicBlock *Entry = BasicBlock::Create(Context, "entry", Caller);
  BasicBlock *Exit = appendRuntimeLoop(*Caller, Entry, *SideEffect,
                                       Caller->getArg(0), "caller.loop");
  IRBuilder<> Builder(Exit);
  Builder.CreateCall(OrdinaryLeaf, {Caller->getArg(0)});
  Builder.CreateCall(MandatoryLeaf, {Caller->getArg(0)});
  Builder.CreateRetVoid();

  return {std::move(M), Caller};
}

void runModuleInliner(Module &M, TargetMachine &Machine) {
  LoopAnalysisManager LAM;
  FunctionAnalysisManager FAM;
  CGSCCAnalysisManager CGAM;
  ModuleAnalysisManager MAM;
  PassInstrumentationCallbacks PIC;
  PassBuilder PB(&Machine, PipelineTuningOptions(), std::nullopt, &PIC);
  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  ModulePassManager MPM;
  MPM.addPass(RequireAnalysisPass<ProfileSummaryAnalysis, Module>());
  MPM.addPass(ModuleInlinerPass(getInlineParams(/*Threshold=*/100000),
                                InliningAdvisorMode::Default,
                                ThinOrFullLTOPhase::FullLTOPostLink));
  MPM.run(M, MAM);
}

unsigned countDirectCallsTo(const Function &Caller, StringRef CalleeName) {
  unsigned Count = 0;
  for (const Instruction &I : instructions(Caller))
    if (const auto *CB = dyn_cast<CallBase>(&I))
      if (const Function *Callee = CB->getCalledFunction())
        Count += Callee->getName() == CalleeName;
  return Count;
}

std::unique_ptr<Module> createFullUnrollFixture(LLVMContext &Context,
                                                TargetMachine &Machine) {
  auto M = std::make_unique<Module>("neverc-full-unroll-loop-cap", Context);
  M->setTargetTriple(Machine.getTargetTriple().str());
  M->setDataLayout(Machine.createDataLayout());

  Type *Void = Type::getVoidTy(Context);
  Type *I32 = Type::getInt32Ty(Context);
  FunctionType *UnaryTy = FunctionType::get(Void, {I32}, false);
  Function *SideEffect = Function::Create(UnaryTy, GlobalValue::ExternalLinkage,
                                          "neverc_unroll_side_effect", *M);
  Function *F = Function::Create(UnaryTy, GlobalValue::ExternalLinkage,
                                 "neverc_two_loop_function", *M);
  Value *TripCount = ConstantInt::get(I32, 4);
  BasicBlock *Entry = BasicBlock::Create(Context, "entry", F);
  BasicBlock *AfterFirst =
      appendRuntimeLoop(*F, Entry, *SideEffect, TripCount, "first");
  BasicBlock *AfterSecond =
      appendRuntimeLoop(*F, AfterFirst, *SideEffect, TripCount, "second");
  IRBuilder<>(AfterSecond).CreateRetVoid();
  return M;
}

void runFullUnroll(Module &M, TargetMachine &Machine) {
  LoopAnalysisManager LAM;
  FunctionAnalysisManager FAM;
  CGSCCAnalysisManager CGAM;
  ModuleAnalysisManager MAM;
  PassInstrumentationCallbacks PIC;
  PassBuilder PB(&Machine, PipelineTuningOptions(), std::nullopt, &PIC);
  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  FunctionPassManager FPM;
  FPM.addPass(createFunctionToLoopPassAdaptor(
      LoopFullUnrollPass(/*OptLevel=*/2, /*OnlyWhenForced=*/false,
                         /*ForgetSCEV=*/false)));
  ModulePassManager MPM;
  MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
  MPM.run(M, MAM);
}

unsigned countBackEdges(const Function &F) {
  SmallVector<std::pair<const BasicBlock *, const BasicBlock *>, 8> BackEdges;
  FindFunctionBackedges(F, BackEdges);
  return BackEdges.size();
}

} // namespace

TEST(NevercPipelinePolicyTest,
     ModuleInlinerHonorsTaskLocalLoopCapAndMandatoryExemption) {
  std::unique_ptr<TargetMachine> Machine = createNativeTargetMachine();
  ASSERT_NE(Machine, nullptr);

  LLVMContext CappedContext;
  NevercPipelineTuningOptions CappedTuning;
  CappedTuning.InlineMaxCallerLoops = 1;
  CappedContext.setNevercPipelineTuningOptions(CappedTuning);
  ModuleInlinerFixture Capped =
      createModuleInlinerFixture(CappedContext, *Machine);
  ASSERT_FALSE(verifyModule(*Capped.M, &errs()));
  runModuleInliner(*Capped.M, *Machine);
  EXPECT_EQ(countDirectCallsTo(*Capped.Caller, "neverc_loop_leaf"), 1u)
      << "the flat module inliner bypassed the request-local loop-density cap";
  EXPECT_EQ(countDirectCallsTo(*Capped.Caller, "neverc_mandatory_loop_leaf"),
            0u)
      << "the loop-density cap must not block mandatory always-inline calls";
  EXPECT_NE(Capped.M->getFunction("neverc_loop_leaf"), nullptr);

  LLVMContext UncappedContext;
  NevercPipelineTuningOptions UncappedTuning;
  UncappedTuning.InlineMaxCallerLoops = 0;
  UncappedContext.setNevercPipelineTuningOptions(UncappedTuning);
  ModuleInlinerFixture Uncapped =
      createModuleInlinerFixture(UncappedContext, *Machine);
  ASSERT_FALSE(verifyModule(*Uncapped.M, &errs()));
  runModuleInliner(*Uncapped.M, *Machine);
  EXPECT_EQ(countDirectCallsTo(*Uncapped.Caller, "neverc_loop_leaf"), 0u)
      << "the control arm must prove the advisor would otherwise inline";
  EXPECT_EQ(Uncapped.M->getFunction("neverc_loop_leaf"), nullptr);
}

TEST(NevercPipelinePolicyTest,
     FullUnrollPassConsumesTaskLocalPerFunctionLoopCap) {
  std::unique_ptr<TargetMachine> Machine = createNativeTargetMachine();
  ASSERT_NE(Machine, nullptr);

  LLVMContext CappedContext;
  NevercPipelineTuningOptions CappedTuning;
  CappedTuning.FullUnrollMaxLoopsPerFunction = 1;
  CappedContext.setNevercPipelineTuningOptions(CappedTuning);
  std::unique_ptr<Module> Capped =
      createFullUnrollFixture(CappedContext, *Machine);
  ASSERT_FALSE(verifyModule(*Capped, &errs()));
  runFullUnroll(*Capped, *Machine);
  EXPECT_EQ(countBackEdges(*Capped->getFunction("neverc_two_loop_function")),
            2u);

  LLVMContext UncappedContext;
  NevercPipelineTuningOptions UncappedTuning;
  UncappedTuning.FullUnrollMaxLoopsPerFunction = 0;
  UncappedContext.setNevercPipelineTuningOptions(UncappedTuning);
  std::unique_ptr<Module> Uncapped =
      createFullUnrollFixture(UncappedContext, *Machine);
  ASSERT_FALSE(verifyModule(*Uncapped, &errs()));
  runFullUnroll(*Uncapped, *Machine);
  EXPECT_EQ(countBackEdges(*Uncapped->getFunction("neverc_two_loop_function")),
            0u)
      << "the control arm must prove both constant-trip loops are unrollable";
}

TEST(NevercPipelinePolicyTest,
     ScalarEvolutionKeepsAmbientABIAndContextLocalOverride) {
  std::unique_ptr<TargetMachine> Machine = createNativeTargetMachine();
  ASSERT_NE(Machine, nullptr);

  const unsigned SavedThreshold = getScevHugeExprThreshold();
  auto RestoreThreshold =
      make_scope_exit([&] { setScevHugeExprThreshold(SavedThreshold); });

  LLVMContext Context;
  Module M("neverc-scev-context-policy", Context);
  M.setTargetTriple(Machine->getTargetTriple().str());
  M.setDataLayout(Machine->createDataLayout());
  Function *F = Function::Create(
      FunctionType::get(Type::getVoidTy(Context), false),
      GlobalValue::ExternalLinkage, "neverc_scev_context_policy", M);
  IRBuilder<>(BasicBlock::Create(Context, "entry", F)).CreateRetVoid();

  LoopAnalysisManager LAM;
  FunctionAnalysisManager FAM;
  CGSCCAnalysisManager CGAM;
  ModuleAnalysisManager MAM;
  PassBuilder PB(Machine.get());
  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  setScevHugeExprThreshold(211);
  ScalarEvolution &SE = FAM.getResult<ScalarEvolutionAnalysis>(*F);
  EXPECT_EQ(SE.getHugeExpressionThreshold(), 211u);

  Context.setNevercSCEVHugeExpressionThreshold(73);
  setScevHugeExprThreshold(307);
  EXPECT_EQ(SE.getHugeExpressionThreshold(), 73u)
      << "an in-flight request must not reread the ambient option";

  Context.setNevercSCEVHugeExpressionThreshold(std::nullopt);
  EXPECT_EQ(SE.getHugeExpressionThreshold(), 307u)
      << "an ordinary context preserves the legacy ambient behavior";
}

TEST(NevercPipelinePolicyTest,
     SingularParallelWrappersPreserveContextSCEVOverrideForZeroSentinel) {
  std::unique_ptr<TargetMachine> Machine = createNativeTargetMachine();
  ASSERT_NE(Machine, nullptr);

  const unsigned SavedThreshold = getScevHugeExprThreshold();
  auto RestoreThreshold =
      make_scope_exit([&] { setScevHugeExprThreshold(SavedThreshold); });
  setScevHugeExprThreshold(307);

  neverc::ParallelCodeGenTuning Tuning;
  Tuning.SCEVHugeExprThreshold = 0;
  auto ExpectPreservedAfterDecline = [&](bool Optimize) {
    LLVMContext Context;
    Context.setNevercSCEVHugeExpressionThreshold(73);
    Module M("singular-pcg-context-scev", Context);
    M.setTargetTriple(Machine->getTargetTriple().str());
    M.setDataLayout(Machine->createDataLayout());
    SmallVector<char, 0> Output;
    raw_svector_ostream Stream(Output);
    const bool Succeeded =
        Optimize
            ? neverc::runParallelOptAndCodeGenWithTuning(
                  M, *Machine, neverc::ParallelCodeGenOutputs{Stream},
                  /*OptLevel=*/2, Tuning)
            : neverc::runParallelCodeGenWithTuning(
                  M, *Machine, neverc::ParallelCodeGenOutputs{Stream}, Tuning);
    EXPECT_FALSE(Succeeded);
    ASSERT_TRUE(Context.getNevercSCEVHugeExpressionThreshold().has_value());
    EXPECT_EQ(*Context.getNevercSCEVHugeExpressionThreshold(), 73u);
  };

  ExpectPreservedAfterDecline(/*Optimize=*/false);
  ExpectPreservedAfterDecline(/*Optimize=*/true);
}

TEST(NevercPipelinePolicyTest,
     LegacyParallelWrappersCaptureParsedPipelineOverridesBeforeDecline) {
  std::unique_ptr<TargetMachine> Machine = createNativeTargetMachine();
  ASSERT_NE(Machine, nullptr);

  auto &Options = cl::getRegisteredOptions();
  auto It = Options.find("neverc-full-unroll-max-loops-per-function");
  ASSERT_NE(It, Options.end());
  cl::Option *Option = It->second;
  std::function<void()> Restore = Option->createStateRestorer();
  auto Restorer = make_scope_exit([&] { Restore(); });
  Option->reset();
  ASSERT_FALSE(Option->addOccurrence(/*Pos=*/1, Option->ArgStr, "37"));

  auto ExpectCapturedAfterDecline = [&](bool Optimize) {
    LLVMContext Context;
    Module M("legacy-pipeline-policy-capture", Context);
    M.setTargetTriple(Machine->getTargetTriple().str());
    M.setDataLayout(Machine->createDataLayout());
    SmallVector<char, 0> Output;
    raw_svector_ostream Stream(Output);
    const bool Succeeded =
        Optimize
            ? neverc::runParallelOptAndCodeGen(
                  M, *Machine, neverc::ParallelCodeGenOutputs{Stream},
                  /*NumPartitions=*/{}, /*OptLevel=*/{})
            : neverc::runParallelCodeGen(M, *Machine,
                                         neverc::ParallelCodeGenOutputs{Stream},
                                         /*NumPartitions=*/{});
    EXPECT_FALSE(Succeeded);
    EXPECT_EQ(
        Context.getNevercPipelineTuningOptions().FullUnrollMaxLoopsPerFunction,
        37u);
  };

  ExpectCapturedAfterDecline(/*Optimize=*/false);
  ExpectCapturedAfterDecline(/*Optimize=*/true);
}
