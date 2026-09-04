//===- ParallelCodeGenCacheTests.cpp - Aggregate cache transactions -------===//

#include "Backend/ParallelCodeGenMergeInternal.h"
#include "ProcessResourceBrokerInternal.h"
#include "neverc/Plugin/Host/PluginLLVMOptionSnapshot.h"

#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/xxhash.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Host.h"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace llvm;

namespace {

class ScopedEnvironmentVariable {
public:
  ScopedEnvironmentVariable(const char *Name, const char *Value) : Name(Name) {
    if (const char *Previous = ::getenv(Name)) {
      HadPrevious = true;
      PreviousValue = Previous;
    }
#ifdef _WIN32
    ::_putenv_s(Name, Value ? Value : "");
#else
    if (Value)
      ::setenv(Name, Value, 1);
    else
      ::unsetenv(Name);
#endif
  }

  ~ScopedEnvironmentVariable() {
#ifdef _WIN32
    ::_putenv_s(Name.c_str(), HadPrevious ? PreviousValue.c_str() : "");
#else
    if (HadPrevious)
      ::setenv(Name.c_str(), PreviousValue.c_str(), 1);
    else
      ::unsetenv(Name.c_str());
#endif
  }

private:
  std::string Name;
  std::string PreviousValue;
  bool HadPrevious = false;
};

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
  // COFF otherwise permits a wall-clock timestamp for incremental-linker
  // compatibility, which would invalidate the byte-for-byte oracle below.
  Options.MCOptions.MCIncrementalLinkerCompatible = false;
  return std::unique_ptr<TargetMachine>(
      TheTarget->createTargetMachine(TripleName, "generic", "", Options,
                                     std::nullopt, CodeGenOptLevel::Default));
}

std::unique_ptr<Module>
createLoopDenseModule(LLVMContext &Context, TargetMachine &Machine,
                      StringRef Identifier, bool AddUnloweredTypeTest = false) {
  auto M = std::make_unique<Module>(Identifier, Context);
  M->setTargetTriple(Machine.getTargetTriple().str());
  M->setDataLayout(Machine.createDataLayout());

  Type *I64 = Type::getInt64Ty(Context);
  FunctionType *FunctionTy = FunctionType::get(I64, {I64}, false);
  for (unsigned Index = 0; Index != 64; ++Index) {
    Function *F =
        Function::Create(FunctionTy, GlobalValue::ExternalLinkage,
                         (Twine("pcg_cache_loop_") + Twine(Index)).str(), *M);
    F->addFnAttr(Attribute::NoInline);
    Argument *Limit = F->getArg(0);
    Limit->setName("limit");

    BasicBlock *Entry = BasicBlock::Create(Context, "entry", F);
    BasicBlock *Loop = BasicBlock::Create(Context, "loop", F);
    BasicBlock *Exit = BasicBlock::Create(Context, "exit", F);
    IRBuilder<> Builder(Entry);
    AllocaInst *State = Builder.CreateAlloca(I64, nullptr, "state");
    StoreInst *InitialStore =
        Builder.CreateStore(ConstantInt::get(I64, Index + 1), State);
    InitialStore->setVolatile(true);
    Builder.CreateBr(Loop);

    Builder.SetInsertPoint(Loop);
    PHINode *Iteration = Builder.CreatePHI(I64, 2, "iteration");
    Iteration->addIncoming(ConstantInt::get(I64, 0), Entry);
    LoadInst *Old = Builder.CreateLoad(I64, State, "old");
    Old->setVolatile(true);
    Value *Mixed = Builder.CreateAdd(Old, Iteration, "mixed");
    StoreInst *LoopStore = Builder.CreateStore(Mixed, State);
    LoopStore->setVolatile(true);
    Value *Next =
        Builder.CreateAdd(Iteration, ConstantInt::get(I64, 1), "next");
    Value *Continue = Builder.CreateICmpULT(Next, Limit, "continue");
    Builder.CreateCondBr(Continue, Loop, Exit);
    Iteration->addIncoming(Next, Loop);

    Builder.SetInsertPoint(Exit);
    LoadInst *Result = Builder.CreateLoad(I64, State, "result");
    Result->setVolatile(true);
    Builder.CreateRet(Result);
  }

  if (AddUnloweredTypeTest) {
    Type *PointerTy = PointerType::getUnqual(Context);
    Function *Probe = Function::Create(
        FunctionType::get(Type::getInt1Ty(Context), {PointerTy}, false),
        GlobalValue::ExternalLinkage, "pcg_live_type_test", *M);
    IRBuilder<> Builder(BasicBlock::Create(Context, "entry", Probe));
    Function *TypeTest =
        Intrinsic::getDeclaration(M.get(), Intrinsic::type_test);
    Value *TypeID = MetadataAsValue::get(
        Context, MDString::get(Context, "neverc.test.pcg.type"));
    Value *Result = Builder.CreateCall(TypeTest, {Probe->getArg(0), TypeID});
    Builder.CreateRet(Result);
  }
  return M;
}

std::string moduleIRText(const Module &M) {
  std::string Text;
  raw_string_ostream Stream(Text);
  M.print(Stream, nullptr);
  Stream.flush();
  return Text;
}

void countAllObservations(neverc::ParallelCodeGenObservers &Observers,
                          std::atomic<unsigned> &Count) {
  auto Note = [&] { Count.fetch_add(1, std::memory_order_relaxed); };
  Observers.ObservePartitionExecutionOrder =
      [Note](neverc::ParallelCodeGenWorkerPhase, ArrayRef<unsigned>) {
        Note();
      };
  Observers.ObserveResourceWorkerGrant =
      [Note](neverc::ParallelCodeGenWorkerPhase, unsigned, unsigned) {
        Note();
      };
  Observers.ObserveResolvedFinalCodeGenPartitions = [Note](unsigned) {
    Note();
  };
  Observers.ObserveResolvedFinalCodeGenSCEVThreshold = [Note](unsigned) {
    Note();
  };
  Observers.ObserveFinalCodeGenPartitionPipelineTuning =
      [Note](unsigned, const NevercPipelineTuningOptions &) { Note(); };
  Observers.ObserveRetention =
      [Note](neverc::ParallelCodeGenRetentionPoint,
             neverc::ParallelCodeGenRetentionSnapshot) { Note(); };
}

class InjectUnloweredTypeTestPass
    : public PassInfoMixin<InjectUnloweredTypeTestPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
    LLVMContext &Context = M.getContext();
    Function *Probe = Function::Create(
        FunctionType::get(Type::getInt1Ty(Context),
                          {PointerType::getUnqual(Context)}, false),
        GlobalValue::ExternalLinkage, "pcg_late_type_test", M);
    IRBuilder<> Builder(BasicBlock::Create(Context, "entry", Probe));
    Function *TypeTest = Intrinsic::getDeclaration(&M, Intrinsic::type_test);
    Value *TypeID = MetadataAsValue::get(
        Context, MDString::get(Context, "neverc.test.pcg.late.type"));
    Value *Result = Builder.CreateCall(TypeTest, {Probe->getArg(0), TypeID});
    Builder.CreateRet(Result);
    return PreservedAnalyses::none();
  }
};

class LowerTypeTestForCodeGenPass
    : public PassInfoMixin<LowerTypeTestForCodeGenPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
    Function *TypeTest = M.getFunction("llvm.type.test");
    if (!TypeTest)
      return PreservedAnalyses::all();
    while (!TypeTest->use_empty()) {
      auto *Call = dyn_cast<CallBase>(*TypeTest->user_begin());
      if (!Call)
        return PreservedAnalyses::none();
      Call->replaceAllUsesWith(ConstantInt::getFalse(M.getContext()));
      Call->eraseFromParent();
    }
    return PreservedAnalyses::none();
  }
};

class ReportWholeModuleOptimizationErrorPass
    : public PassInfoMixin<ReportWholeModuleOptimizationErrorPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
    M.getContext().emitError("test whole-module optimization failure");
    return PreservedAnalyses::all();
  }
};

class CountFinalMachineFunctionsPass final : public MachineFunctionPass {
public:
  static char ID;

  explicit CountFinalMachineFunctionsPass(std::atomic<unsigned> &Runs)
      : MachineFunctionPass(ID), Runs(Runs) {}

private:
  bool runOnMachineFunction(MachineFunction &) override {
    Runs.fetch_add(1, std::memory_order_relaxed);
    return false;
  }

  std::atomic<unsigned> &Runs;
};

char CountFinalMachineFunctionsPass::ID = 0;

class CountFinalMachineFunctionsHooks final : public MachinePipelineHooks {
public:
  explicit CountFinalMachineFunctionsHooks(std::atomic<unsigned> &Runs)
      : Runs(Runs) {}

  void addPasses(TargetPassConfig &TPC,
                 MachinePipelineHookPoint Point) override {
    if (Point == MachinePipelineHookPoint::Final)
      TPC.addExternalPass(new CountFinalMachineFunctionsPass(Runs));
  }

private:
  std::atomic<unsigned> &Runs;
};

std::string functionNameForPartition(StringRef Prefix, unsigned Partition,
                                     unsigned NumPartitions) {
  for (unsigned Suffix = 0;; ++Suffix) {
    std::string Name = (Prefix + Twine(Partition) + "_" + Twine(Suffix)).str();
    if (xxh3_64bits(Name) % NumPartitions == Partition)
      return Name;
  }
}

struct ScheduledWorkEstimate {
  std::uint64_t InstructionWeight = 0;
  std::uint64_t LoopCount = 0;
};

std::vector<ScheduledWorkEstimate>
captureScheduledWork(Module &M, unsigned NumPartitions) {
  std::vector<ScheduledWorkEstimate> Work(NumPartitions);
  SmallVector<std::pair<const BasicBlock *, const BasicBlock *>, 8> BackEdges;
  for (Function &F : M) {
    if (F.isDeclaration())
      continue;
    ScheduledWorkEstimate &Estimate =
        Work[xxh3_64bits(F.getName()) % NumPartitions];
    for (BasicBlock &BB : F)
      Estimate.InstructionWeight += BB.size();
    BackEdges.clear();
    FindFunctionBackedges(F, BackEdges);
    Estimate.LoopCount += BackEdges.size();
  }
  return Work;
}

Function *addScheduledFunction(Module &M, StringRef Name, unsigned NumLoops,
                               unsigned NumStraightLineAdds) {
  LLVMContext &Context = M.getContext();
  Type *I64 = Type::getInt64Ty(Context);
  FunctionType *FunctionTy = FunctionType::get(I64, {I64}, false);
  Function *F =
      Function::Create(FunctionTy, GlobalValue::ExternalLinkage, Name, M);
  F->addFnAttr(Attribute::NoInline);
  Argument *Limit = F->getArg(0);
  Limit->setName("limit");

  BasicBlock *Current = BasicBlock::Create(Context, "entry", F);
  IRBuilder<> Builder(Current);
  Value *Accumulator = Limit;
  for (unsigned LoopIndex = 0; LoopIndex != NumLoops; ++LoopIndex) {
    BasicBlock *Loop = BasicBlock::Create(Context, "loop", F);
    BasicBlock *Next = BasicBlock::Create(Context, "next", F);
    Builder.CreateBr(Loop);

    Builder.SetInsertPoint(Loop);
    PHINode *Iteration = Builder.CreatePHI(I64, 2, "iteration");
    PHINode *State = Builder.CreatePHI(I64, 2, "state");
    Iteration->addIncoming(ConstantInt::get(I64, 0), Current);
    State->addIncoming(Accumulator, Current);
    Value *Mixed = Builder.CreateAdd(State, Iteration, "mixed");
    Value *Incremented =
        Builder.CreateAdd(Iteration, ConstantInt::get(I64, 1), "incremented");
    Value *Continue = Builder.CreateICmpULT(Incremented, Limit, "continue");
    Builder.CreateCondBr(Continue, Loop, Next);
    Iteration->addIncoming(Incremented, Loop);
    State->addIncoming(Mixed, Loop);

    Current = Next;
    Builder.SetInsertPoint(Current);
    Accumulator = Mixed;
  }

  for (unsigned I = 0; I != NumStraightLineAdds; ++I)
    Accumulator =
        Builder.CreateAdd(Accumulator, ConstantInt::get(I64, I + 1), "work");
  Builder.CreateRet(Accumulator);
  return F;
}

std::unique_ptr<Module> createScheduledModule(LLVMContext &Context,
                                              TargetMachine &Machine,
                                              StringRef Identifier) {
  auto M = std::make_unique<Module>(Identifier, Context);
  M->setTargetTriple(Machine.getTargetTriple().str());
  M->setDataLayout(Machine.createDataLayout());

  constexpr unsigned NumPartitions = 4;
  addScheduledFunction(
      *M, functionNameForPartition("pcg_schedule_", 0, NumPartitions),
      /*NumLoops=*/0, /*NumStraightLineAdds=*/1500);
  addScheduledFunction(
      *M, functionNameForPartition("pcg_schedule_", 1, NumPartitions),
      /*NumLoops=*/1, /*NumStraightLineAdds=*/0);
  addScheduledFunction(
      *M, functionNameForPartition("pcg_schedule_", 2, NumPartitions),
      /*NumLoops=*/1, /*NumStraightLineAdds=*/0);
  addScheduledFunction(
      *M, functionNameForPartition("pcg_schedule_", 3, NumPartitions),
      /*NumLoops=*/3, /*NumStraightLineAdds=*/0);
  return M;
}

std::unique_ptr<Module> createExecutionOrderInvariantModule(
    LLVMContext &Context, TargetMachine &Machine, StringRef Identifier) {
  auto M = std::make_unique<Module>(Identifier, Context);
  M->setTargetTriple(Machine.getTargetTriple().str());
  M->setDataLayout(Machine.createDataLayout());

  constexpr unsigned NumPartitions = 4;
  for (unsigned Partition = 0; Partition != NumPartitions; ++Partition)
    addScheduledFunction(*M,
                         functionNameForPartition("pcg_order_invariant_",
                                                  Partition, NumPartitions),
                         /*NumLoops=*/1,
                         /*NumStraightLineAdds=*/Partition == 3 ? 128 : 0);
  return M;
}

std::unique_ptr<Module> createBoundedInFlightModule(LLVMContext &Context,
                                                    TargetMachine &Machine,
                                                    StringRef Identifier) {
  auto M = std::make_unique<Module>(Identifier, Context);
  M->setTargetTriple(Machine.getTargetTriple().str());
  M->setDataLayout(Machine.createDataLayout());

  constexpr unsigned NumPartitions = 8;
  for (unsigned Partition = 0; Partition != NumPartitions; ++Partition)
    addScheduledFunction(*M,
                         functionNameForPartition("pcg_bounded_in_flight_",
                                                  Partition, NumPartitions),
                         /*NumLoops=*/1, /*NumStraightLineAdds=*/Partition * 3);
  return M;
}

std::unique_ptr<Module> createExactScheduleFormulaModule(LLVMContext &Context,
                                                         TargetMachine &Machine,
                                                         StringRef Identifier) {
  auto M = std::make_unique<Module>(Identifier, Context);
  M->setTargetTriple(Machine.getTargetTriple().str());
  M->setDataLayout(Machine.createDataLayout());

  constexpr unsigned NumPartitions = 2;
  // p0: weight 707, loops 7. p1: weight 901, loops 0. With divisors
  // 1000/10 the exact max is p0=.707, p1=.901, so p1 must run first.
  // Integer division would tie both at zero and normalized-signal addition
  // would rank p0 first; this fixture therefore distinguishes both mistakes.
  addScheduledFunction(
      *M, functionNameForPartition("pcg_formula_", 0, NumPartitions),
      /*NumLoops=*/7, /*NumStraightLineAdds=*/657);
  addScheduledFunction(
      *M, functionNameForPartition("pcg_formula_", 1, NumPartitions),
      /*NumLoops=*/0, /*NumStraightLineAdds=*/900);
  return M;
}

std::unique_ptr<Module> createCompactedScheduleModule(LLVMContext &Context,
                                                      TargetMachine &Machine,
                                                      StringRef Identifier) {
  auto M = std::make_unique<Module>(Identifier, Context);
  M->setTargetTriple(Machine.getTargetTriple().str());
  M->setDataLayout(Machine.createDataLayout());

  constexpr unsigned NumHashBins = 4;
  // Four functions justify four provisional bins, but only original bins 0
  // and 3 are populated. After compaction they must become final partitions
  // 0 and 1, with the heavier former bin 3 scheduled first.
  addScheduledFunction(
      *M, functionNameForPartition("pcg_compact_a_", 0, NumHashBins),
      /*NumLoops=*/0, /*NumStraightLineAdds=*/0);
  addScheduledFunction(
      *M, functionNameForPartition("pcg_compact_b_", 0, NumHashBins),
      /*NumLoops=*/0, /*NumStraightLineAdds=*/0);
  addScheduledFunction(
      *M, functionNameForPartition("pcg_compact_a_", 3, NumHashBins),
      /*NumLoops=*/1, /*NumStraightLineAdds=*/0);
  addScheduledFunction(
      *M, functionNameForPartition("pcg_compact_b_", 3, NumHashBins),
      /*NumLoops=*/0, /*NumStraightLineAdds=*/100);
  return M;
}

struct StoredArtifact {
  std::string Key;
  std::vector<char> Bytes;
  bool OutputWasReady = false;
};

class RecordingCache {
public:
  explicit RecordingCache(SmallVectorImpl<char> &Output) : Output(Output) {}

  neverc::PartitionCacheHooks hooks() {
    neverc::PartitionCacheHooks Hooks;
    Hooks.Lookup = [this](StringRef PipeTag, StringRef Bitcode,
                          std::string &KeyOut,
                          SmallVectorImpl<char> &Artifact) {
      (void)Bitcode;
      (void)Artifact;
      std::lock_guard<std::mutex> Lock(Mutex);
      LookupTags.push_back(PipeTag.str());
      KeyOut = "miss-" + std::to_string(NextKey++);
      return false;
    };
    Hooks.Store = [this](StringRef Key, ArrayRef<char> Artifact) {
      std::lock_guard<std::mutex> Lock(Mutex);
      Stores.push_back({Key.str(),
                        std::vector<char>(Artifact.begin(), Artifact.end()),
                        !Output.empty()});
    };
    return Hooks;
  }

  std::vector<std::string> lookupTags() const {
    std::lock_guard<std::mutex> Lock(Mutex);
    return LookupTags;
  }

  std::vector<StoredArtifact> stores() const {
    std::lock_guard<std::mutex> Lock(Mutex);
    return Stores;
  }

private:
  SmallVectorImpl<char> &Output;
  mutable std::mutex Mutex;
  unsigned NextKey = 0;
  std::vector<std::string> LookupTags;
  std::vector<StoredArtifact> Stores;
};

class ReplayCache {
public:
  neverc::PartitionCacheHooks hooks() {
    neverc::PartitionCacheHooks Hooks;
    Hooks.Lookup = [this](StringRef PipeTag, StringRef Bitcode,
                          std::string &KeyOut,
                          SmallVectorImpl<char> &Artifact) {
      std::string Key;
      Key.reserve(PipeTag.size() + 1 + Bitcode.size());
      Key.append(PipeTag.data(), PipeTag.size());
      Key.push_back('\0');
      Key.append(Bitcode.data(), Bitcode.size());
      KeyOut = Key;

      std::lock_guard<std::mutex> Lock(Mutex);
      ++Lookups;
      auto It = Artifacts.find(Key);
      if (It == Artifacts.end())
        return false;
      Artifact.append(It->second.begin(), It->second.end());
      ++Hits;
      return true;
    };
    Hooks.Store = [this](StringRef Key, ArrayRef<char> Artifact) {
      std::lock_guard<std::mutex> Lock(Mutex);
      Artifacts[Key.str()] =
          std::vector<char>(Artifact.begin(), Artifact.end());
      ++Stores;
    };
    return Hooks;
  }

  unsigned hitCount() const {
    std::lock_guard<std::mutex> Lock(Mutex);
    return Hits;
  }

  unsigned lookupCount() const {
    std::lock_guard<std::mutex> Lock(Mutex);
    return Lookups;
  }

  unsigned storeCount() const {
    std::lock_guard<std::mutex> Lock(Mutex);
    return Stores;
  }

private:
  mutable std::mutex Mutex;
  std::map<std::string, std::vector<char>> Artifacts;
  unsigned Lookups = 0;
  unsigned Hits = 0;
  unsigned Stores = 0;
};

void expectObject(StringRef Bytes) {
  auto Object = object::ObjectFile::createObjectFile(
      MemoryBufferRef(Bytes, "parallel-codegen-cache-output"));
  ASSERT_TRUE(static_cast<bool>(Object))
      << toString(Object.takeError()).str().str();
}

neverc::ParallelOptimizationHooks wholeModuleBarrier() {
  neverc::ParallelOptimizationHooks Hooks;
  Hooks.WholeModulePostOpt = [](ModulePassManager &) {};
  return Hooks;
}

class WholeModulePassLifetimeProbe
    : public PassInfoMixin<WholeModulePassLifetimeProbe> {
public:
  explicit WholeModulePassLifetimeProbe(std::shared_ptr<unsigned> Lifetime)
      : Lifetime(std::move(Lifetime)) {}

  PreservedAnalyses run(Module &, ModuleAnalysisManager &) {
    return PreservedAnalyses::all();
  }

private:
  std::shared_ptr<unsigned> Lifetime;
};

enum class SCEVProbePhase : unsigned {
  PartitionPreOpt = 0,
  WholeModulePostOpt = 1,
};

constexpr unsigned probePhaseIndex(SCEVProbePhase Phase) {
  return static_cast<unsigned>(Phase);
}

using PipelineTuningValues = std::array<std::int64_t, 5>;

PipelineTuningValues
pipelineTuningValues(const llvm::NevercPipelineTuningOptions &Tuning) {
  return {static_cast<std::int64_t>(Tuning.ModuleInlinerThreshold),
          static_cast<std::int64_t>(Tuning.AutoLTOInlineThreshold),
          static_cast<std::int64_t>(Tuning.InlinerLiteFSimpl),
          static_cast<std::int64_t>(Tuning.InlineMaxCallerLoops),
          static_cast<std::int64_t>(Tuning.FullUnrollMaxLoopsPerFunction)};
}

/// A deterministic two-session rendezvous for a fixed optimization phase.
/// Session B cannot leave until session A rereads the same SCEV instance it
/// queried before the rendezvous. Both participants then leave together, so a
/// fast failure in one PCG request cannot strand the other request.
class SCEVProbeRendezvous {
public:
  template <typename RereadFn>
  bool arrive(unsigned Session, SCEVProbePhase Phase, RereadFn &&Reread) {
    const unsigned PhaseIndex = probePhaseIndex(Phase);
    const unsigned SessionBit = 1u << Session;
    std::unique_lock<std::mutex> Lock(Mutex);
    PhaseState &State = Phases[PhaseIndex];
    if (Session > 1 || (State.ArrivedMask & SessionBit) != 0) {
      failLocked("duplicate or invalid SCEV rendezvous participant");
      return false;
    }

    State.ArrivedMask |= SessionBit;
    Condition.notify_all();
    if (!waitLocked(Lock, [&] {
          return Cancelled || State.ArrivedMask == BothSessionsMask;
        }))
      return false;
    if (Cancelled)
      return false;

    if (Session == 0) {
      Lock.unlock();
      Reread();
      Lock.lock();
      State.SessionAReread = true;
      Condition.notify_all();
    } else if (!waitLocked(Lock,
                           [&] { return Cancelled || State.SessionAReread; })) {
      return false;
    }
    if (Cancelled)
      return false;

    State.CompletedMask |= SessionBit;
    Condition.notify_all();
    if (!waitLocked(Lock, [&] {
          return Cancelled || State.CompletedMask == BothSessionsMask;
        }))
      return false;
    return !Cancelled;
  }

  void sessionFinished(unsigned Session) {
    std::lock_guard<std::mutex> Lock(Mutex);
    FinishedMask |= 1u << Session;
    for (const PhaseState &State : Phases) {
      if (State.CompletedMask != BothSessionsMask) {
        failLocked("a PCG session finished before both SCEV phases overlapped");
        break;
      }
    }
  }

  struct Snapshot {
    std::array<unsigned, 2> ArrivedMasks{};
    std::array<unsigned, 2> CompletedMasks{};
    std::array<bool, 2> SessionAReread{};
    unsigned FinishedMask = 0;
    bool Cancelled = false;
    std::string Failure;
  };

  Snapshot snapshot() const {
    std::lock_guard<std::mutex> Lock(Mutex);
    Snapshot Result;
    for (unsigned I = 0; I != Phases.size(); ++I) {
      Result.ArrivedMasks[I] = Phases[I].ArrivedMask;
      Result.CompletedMasks[I] = Phases[I].CompletedMask;
      Result.SessionAReread[I] = Phases[I].SessionAReread;
    }
    Result.FinishedMask = FinishedMask;
    Result.Cancelled = Cancelled;
    Result.Failure = Failure;
    return Result;
  }

private:
  static constexpr unsigned BothSessionsMask = 0b11;
  static constexpr std::chrono::seconds Watchdog{30};

  struct PhaseState {
    unsigned ArrivedMask = 0;
    unsigned CompletedMask = 0;
    bool SessionAReread = false;
  };

  template <typename Predicate>
  bool waitLocked(std::unique_lock<std::mutex> &Lock, Predicate &&Ready) {
    if (Condition.wait_until(Lock, std::chrono::steady_clock::now() + Watchdog,
                             std::forward<Predicate>(Ready)))
      return true;
    failLocked("timed out waiting for the other SCEV probe session");
    return false;
  }

  void failLocked(StringRef Message) {
    if (!Cancelled)
      Failure = Message.str();
    Cancelled = true;
    Condition.notify_all();
  }

  mutable std::mutex Mutex;
  std::condition_variable Condition;
  std::array<PhaseState, 2> Phases;
  unsigned FinishedMask = 0;
  bool Cancelled = false;
  std::string Failure;
};

class SessionSCEVProbeState;

class SCEVThresholdProbePass : public PassInfoMixin<SCEVThresholdProbePass> {
public:
  SCEVThresholdProbePass(SessionSCEVProbeState &State, SCEVProbePhase Phase,
                         unsigned Partition)
      : State(State), Phase(Phase), Partition(Partition) {}

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);

private:
  SessionSCEVProbeState &State;
  SCEVProbePhase Phase;
  unsigned Partition;
};

struct SessionSCEVProbeSnapshot {
  unsigned ConfiguredPartitionProbes = 0;
  std::vector<std::vector<unsigned>> PartitionValues;
  std::vector<unsigned> WholeModuleValues;
  std::array<std::vector<unsigned>, 2> SessionARereads;
  std::vector<std::vector<PipelineTuningValues>> PartitionPipelineValues;
  std::vector<PipelineTuningValues> WholeModulePipelineValues;
  std::array<std::vector<PipelineTuningValues>, 2> SessionAPipelineRereads;
  std::vector<unsigned> ResolvedFinalCodeGenPartitions;
  std::vector<PipelineTuningValues> FinalCodeGenPartitionPipelineValues;
};

class SessionSCEVProbeState {
public:
  SessionSCEVProbeState(unsigned Session, SCEVProbeRendezvous &Rendezvous)
      : Session(Session), Rendezvous(Rendezvous) {}

  unsigned allocatePartitionProbe() {
    return NextPartitionProbe.fetch_add(1, std::memory_order_relaxed);
  }

  bool observeAndClaim(SCEVProbePhase Phase, unsigned Partition,
                       unsigned Threshold,
                       PipelineTuningValues PipelineTuning) {
    std::lock_guard<std::mutex> Lock(Mutex);
    const unsigned PhaseIndex = probePhaseIndex(Phase);
    if (Phase == SCEVProbePhase::PartitionPreOpt) {
      if (PartitionValues.size() <= Partition) {
        PartitionValues.resize(Partition + 1);
        PartitionPipelineValues.resize(Partition + 1);
      }
      PartitionValues[Partition].push_back(Threshold);
      PartitionPipelineValues[Partition].push_back(PipelineTuning);
    } else {
      WholeModuleValues.push_back(Threshold);
      WholeModulePipelineValues.push_back(PipelineTuning);
    }
    if (RendezvousClaimed[PhaseIndex])
      return false;
    RendezvousClaimed[PhaseIndex] = true;
    return true;
  }

  void recordSessionAReread(SCEVProbePhase Phase, unsigned Threshold,
                            PipelineTuningValues PipelineTuning) {
    std::lock_guard<std::mutex> Lock(Mutex);
    SessionARereads[probePhaseIndex(Phase)].push_back(Threshold);
    SessionAPipelineRereads[probePhaseIndex(Phase)].push_back(PipelineTuning);
  }

  void recordResolvedFinalCodeGenPartitions(unsigned Partitions) {
    std::lock_guard<std::mutex> Lock(Mutex);
    ResolvedFinalCodeGenPartitions.push_back(Partitions);
  }

  void recordFinalCodeGenPartitionPipelineTuning(
      unsigned Partition, PipelineTuningValues PipelineTuning) {
    std::lock_guard<std::mutex> Lock(Mutex);
    if (FinalCodeGenPartitionPipelineValues.size() <= Partition)
      FinalCodeGenPartitionPipelineValues.resize(Partition + 1);
    FinalCodeGenPartitionPipelineValues[Partition] = PipelineTuning;
  }

  bool rendezvous(SCEVProbePhase Phase, const std::function<void()> &Reread) {
    return Rendezvous.arrive(Session, Phase, Reread);
  }

  SessionSCEVProbeSnapshot snapshot() const {
    std::lock_guard<std::mutex> Lock(Mutex);
    return {NextPartitionProbe.load(std::memory_order_relaxed),
            PartitionValues,
            WholeModuleValues,
            SessionARereads,
            PartitionPipelineValues,
            WholeModulePipelineValues,
            SessionAPipelineRereads,
            ResolvedFinalCodeGenPartitions,
            FinalCodeGenPartitionPipelineValues};
  }

private:
  const unsigned Session;
  SCEVProbeRendezvous &Rendezvous;
  std::atomic<unsigned> NextPartitionProbe{0};
  mutable std::mutex Mutex;
  std::array<bool, 2> RendezvousClaimed{};
  std::vector<std::vector<unsigned>> PartitionValues;
  std::vector<unsigned> WholeModuleValues;
  std::array<std::vector<unsigned>, 2> SessionARereads;
  std::vector<std::vector<PipelineTuningValues>> PartitionPipelineValues;
  std::vector<PipelineTuningValues> WholeModulePipelineValues;
  std::array<std::vector<PipelineTuningValues>, 2> SessionAPipelineRereads;
  std::vector<unsigned> ResolvedFinalCodeGenPartitions;
  std::vector<PipelineTuningValues> FinalCodeGenPartitionPipelineValues;
};

PreservedAnalyses SCEVThresholdProbePass::run(Function &F,
                                              FunctionAnalysisManager &FAM) {
  ScalarEvolution &SE = FAM.getResult<ScalarEvolutionAnalysis>(F);
  const unsigned Threshold = SE.getHugeExpressionThreshold();
  const PipelineTuningValues PipelineTuning =
      pipelineTuningValues(F.getContext().getNevercPipelineTuningOptions());
  const bool Claimed =
      State.observeAndClaim(Phase, Partition, Threshold, PipelineTuning);
  if (Claimed) {
    State.rendezvous(Phase, [&] {
      ScalarEvolution &SameSE = FAM.getResult<ScalarEvolutionAnalysis>(F);
      State.recordSessionAReread(
          Phase, SameSE.getHugeExpressionThreshold(),
          pipelineTuningValues(
              F.getContext().getNevercPipelineTuningOptions()));
    });
  }
  return PreservedAnalyses::all();
}

neverc::ParallelOptimizationHooks
overlapHooks(SessionSCEVProbeState &State,
             neverc::ParallelCodeGenObservers &Observers) {
  neverc::ParallelOptimizationHooks Hooks;
  Hooks.PreOpt = [&State](ModulePassManager &MPM) {
    const unsigned Partition = State.allocatePartitionProbe();
    MPM.addPass(createModuleToFunctionPassAdaptor(SCEVThresholdProbePass(
        State, SCEVProbePhase::PartitionPreOpt, Partition)));
  };
  Hooks.WholeModulePostOpt = [&State](ModulePassManager &MPM) {
    MPM.addPass(createModuleToFunctionPassAdaptor(SCEVThresholdProbePass(
        State, SCEVProbePhase::WholeModulePostOpt, /*Partition=*/0)));
  };
  Observers.ObserveResolvedFinalCodeGenPartitions =
      [&State](unsigned Partitions) {
        State.recordResolvedFinalCodeGenPartitions(Partitions);
      };
  Observers.ObserveFinalCodeGenPartitionPipelineTuning =
      [&State](unsigned Partition,
               const llvm::NevercPipelineTuningOptions &Tuning) {
        State.recordFinalCodeGenPartitionPipelineTuning(
            Partition, pipelineTuningValues(Tuning));
      };
  return Hooks;
}

neverc::ParallelCodeGenTuning overlapTuning(unsigned MaxPartitions,
                                            unsigned SCEVThreshold) {
  neverc::ParallelCodeGenTuning Tuning;
  Tuning.MinDefinedFunctions = 1;
  Tuning.MinInstructionWeight = 0;
  Tuning.MinLoopCount = 0;
  Tuning.OptInstructionsPerPartition = 1;
  Tuning.OptLoopsPerPartition = 0;
  Tuning.OptMaxPartitions = MaxPartitions;
  Tuning.CodeGenInstructionsPerPartition = 1;
  Tuning.CodeGenLoopsPerPartition = 0;
  Tuning.CodeGenMaxPartitions = MaxPartitions;
  Tuning.SCEVHugeExprThreshold = SCEVThreshold;
  Tuning.LoopsPerWorker = 1;
  Tuning.InstructionsPerWorker = 1;
  Tuning.MinWorkerThreads = MaxPartitions;
  return Tuning;
}

struct DirectPCGRunResult {
  bool Succeeded = false;
  bool SawError = false;
  std::string Failure;
  std::vector<char> Object;
  std::vector<std::string> LookupTags;
  std::vector<StoredArtifact> Stores;
};

struct ResourceGrantObservation {
  neverc::ParallelCodeGenWorkerPhase Phase;
  unsigned DesiredWorkers = 0;
  unsigned GrantedWorkers = 0;
};

DirectPCGRunResult runFreshDirectPCG(
    StringRef Identifier, const neverc::ParallelCodeGenTuning &Tuning,
    neverc::ParallelOptimizationHooks *Hooks, bool RecordCache = true,
    const neverc::ParallelCodeGenObservers *Observers = nullptr,
    const llvm::NevercPipelineTuningOptions *PipelineTuning = nullptr,
    bool AddUnloweredTypeTest = false, bool PreReportError = false) {
  DirectPCGRunResult Result;
  std::unique_ptr<TargetMachine> Machine = createNativeTargetMachine();
  if (!Machine) {
    Result.Failure = "native target machine is unavailable";
    return Result;
  }

  LLVMContext Context;
  Context.setDiagnosticHandlerCallBack(
      [](const DiagnosticInfo &Diagnostic, void *Opaque) {
        if (Diagnostic.getSeverity() == DS_Error)
          static_cast<DirectPCGRunResult *>(Opaque)->SawError = true;
      },
      &Result);
  if (PreReportError)
    Context.emitError("test pre-existing parallel codegen failure");
  std::unique_ptr<Module> M = createLoopDenseModule(
      Context, *Machine, Identifier, AddUnloweredTypeTest);
  SmallVector<char, 0> Output;
  raw_svector_ostream OutputStream(Output);
  RecordingCache Cache(Output);
  neverc::PartitionCacheHooks CacheHooks = Cache.hooks();
  if (PipelineTuning) {
    Result.Succeeded = neverc::runParallelOptAndCodeGenWithTunings(
        *M, *Machine, neverc::ParallelCodeGenOutputs{OutputStream},
        /*OptLevel=*/2, Tuning, *PipelineTuning,
        RecordCache ? &CacheHooks : nullptr, Hooks, Observers);
  } else {
    Result.Succeeded = neverc::runParallelOptAndCodeGenWithTuning(
        *M, *Machine, neverc::ParallelCodeGenOutputs{OutputStream},
        /*OptLevel=*/2, Tuning, RecordCache ? &CacheHooks : nullptr, Hooks,
        Observers);
  }
  Result.Object.assign(Output.begin(), Output.end());
  Result.LookupTags = Cache.lookupTags();
  Result.Stores = Cache.stores();
  if (!Result.Succeeded)
    Result.Failure = "runParallelOptAndCodeGenWithTuning declined or failed";
  return Result;
}

TEST(ParallelCodeGenCacheTest,
     RejectsTypeMetadataIntrinsicInjectedByPartitionPostOptHook) {
  const neverc::ParallelCodeGenTuning Tuning =
      overlapTuning(/*MaxPartitions=*/2, /*SCEVThreshold=*/41);
  neverc::ParallelOptimizationHooks Hooks;
  Hooks.PostOpt = [](ModulePassManager &MPM) {
    MPM.addPass(InjectUnloweredTypeTestPass());
  };

  DirectPCGRunResult Result =
      runFreshDirectPCG("partition-late-type-test", Tuning, &Hooks,
                        /*RecordCache=*/false);
  EXPECT_FALSE(Result.Succeeded);
  EXPECT_TRUE(Result.SawError);
  EXPECT_TRUE(Result.Object.empty());
}

TEST(ParallelCodeGenCacheTest,
     WholeModuleBarrierMayLegitimatelyLowerTypeMetadataIntrinsic) {
  const neverc::ParallelCodeGenTuning Tuning =
      overlapTuning(/*MaxPartitions=*/2, /*SCEVThreshold=*/41);
  neverc::ParallelOptimizationHooks Hooks;
  Hooks.WholeModulePostOpt = [](ModulePassManager &MPM) {
    MPM.addPass(LowerTypeTestForCodeGenPass());
  };

  DirectPCGRunResult Result = runFreshDirectPCG(
      "whole-module-lowers-type-test", Tuning, &Hooks,
      /*RecordCache=*/false, /*Observers=*/nullptr,
      /*PipelineTuning=*/nullptr, /*AddUnloweredTypeTest=*/true);
  ASSERT_TRUE(Result.Succeeded) << Result.Failure;
  EXPECT_FALSE(Result.SawError);
  EXPECT_FALSE(Result.Object.empty());
}

TEST(ParallelCodeGenCacheTest,
     WholeModuleDiagnosticDoesNotPublishObjectOrOptimizedIRCache) {
  const neverc::ParallelCodeGenTuning Tuning =
      overlapTuning(/*MaxPartitions=*/2, /*SCEVThreshold=*/41);
  neverc::ParallelOptimizationHooks Hooks;
  Hooks.WholeModulePostOpt = [](ModulePassManager &MPM) {
    MPM.addPass(ReportWholeModuleOptimizationErrorPass());
  };

  DirectPCGRunResult Result =
      runFreshDirectPCG("whole-module-reported-error", Tuning, &Hooks);
  EXPECT_FALSE(Result.Succeeded);
  EXPECT_TRUE(Result.SawError);
  EXPECT_TRUE(Result.Object.empty());
  EXPECT_TRUE(Result.Stores.empty());
}

TEST(ParallelCodeGenCacheTest,
     ExhaustedWholeModuleCodeGenRoutesReportErrorWithoutAborting) {
  std::unique_ptr<TargetMachine> Machine = createNativeTargetMachine();
  ASSERT_TRUE(Machine);

  LLVMContext Context;
  unsigned ErrorDiagnostics = 0;
  Context.setDiagnosticHandlerCallBack(
      [](const DiagnosticInfo &Diagnostic, void *Opaque) {
        if (Diagnostic.getSeverity() == DS_Error)
          ++*static_cast<unsigned *>(Opaque);
      },
      &ErrorDiagnostics);
  std::unique_ptr<Module> M = createLoopDenseModule(
      Context, *Machine, "whole-module-codegen-routes-exhausted");

  // Partition target machines are independently constructed. Reject only the
  // final sealed-module emission on the original target machine; the forced
  // object-merge failure below rejects the preceding parallel route.
  static_cast<LLVMTargetMachine &>(*Machine).setMachineEmissionFactory(
      [](LLVMTargetMachine &, PassManagerBase &, raw_pwrite_stream &,
         raw_pwrite_stream *, CodeGenFileType,
         MachineModuleInfoWrapperPass &) { return true; });
  ScopedEnvironmentVariable NoStrict("NEVERC_PCG_STRICT", nullptr);
  ScopedEnvironmentVariable ForceMergeFailure(
      "NEVERC_PCG_FORCE_MERGE_FAIL", "1");

  const neverc::ParallelCodeGenTuning Tuning =
      overlapTuning(/*MaxPartitions=*/2, /*SCEVThreshold=*/41);
  neverc::ParallelOptimizationHooks Hooks = wholeModuleBarrier();
  SmallVector<char, 0> Output;
  raw_svector_ostream OutputStream(Output);
  bool Succeeded = neverc::runParallelOptAndCodeGenWithTuning(
      *M, *Machine, neverc::ParallelCodeGenOutputs{OutputStream},
      /*OptLevel=*/2, Tuning, /*Cache=*/nullptr, &Hooks);

  EXPECT_FALSE(Succeeded);
  EXPECT_EQ(ErrorDiagnostics, 1u);
  EXPECT_TRUE(Output.empty());
}

TEST(ParallelCodeGenCacheTest,
     WholeModuleBarrierRunsStatefulMachineHooksExactlyOnce) {
  std::unique_ptr<TargetMachine> Machine = createNativeTargetMachine();
  ASSERT_TRUE(Machine);

  std::atomic<unsigned> MachineFunctionRuns{0};
  static_cast<LLVMTargetMachine &>(*Machine).setMachinePipelineHooks(
      std::make_shared<CountFinalMachineFunctionsHooks>(MachineFunctionRuns));
  LLVMContext Context;
  std::unique_ptr<Module> M = createLoopDenseModule(
      Context, *Machine, "whole-module-stateful-machine-hook");

  // Without the exactly-once guard, the final parallel route runs this hook
  // on every partition and the forced merge decline then replays it during
  // sealed-module serial codegen.
  ScopedEnvironmentVariable ForceMergeFailure(
      "NEVERC_PCG_FORCE_MERGE_FAIL", "1");
  const neverc::ParallelCodeGenTuning Tuning =
      overlapTuning(/*MaxPartitions=*/2, /*SCEVThreshold=*/41);
  neverc::ParallelOptimizationHooks Hooks = wholeModuleBarrier();
  SmallVector<char, 0> Output;
  raw_svector_ostream OutputStream(Output);
  ASSERT_TRUE(neverc::runParallelOptAndCodeGenWithTuning(
      *M, *Machine, neverc::ParallelCodeGenOutputs{OutputStream},
      /*OptLevel=*/2, Tuning, /*Cache=*/nullptr, &Hooks));

  EXPECT_EQ(MachineFunctionRuns.load(std::memory_order_relaxed), 64u);
  ASSERT_FALSE(Output.empty());
  expectObject(StringRef(Output.data(), Output.size()));
}

TEST(ParallelCodeGenCacheTest,
     PreExistingDiagnosticDoesNotRunHooksOrPublishObjectCache) {
  const neverc::ParallelCodeGenTuning Tuning =
      overlapTuning(/*MaxPartitions=*/2, /*SCEVThreshold=*/41);
  std::atomic<unsigned> PreOptRuns{0};
  neverc::ParallelOptimizationHooks Hooks;
  Hooks.PreOpt = [&](ModulePassManager &) {
    PreOptRuns.fetch_add(1, std::memory_order_relaxed);
  };

  DirectPCGRunResult Result = runFreshDirectPCG(
      "pre-existing-reported-error", Tuning, &Hooks,
      /*RecordCache=*/true, /*Observers=*/nullptr,
      /*PipelineTuning=*/nullptr, /*AddUnloweredTypeTest=*/false,
      /*PreReportError=*/true);
  EXPECT_FALSE(Result.Succeeded);
  EXPECT_TRUE(Result.SawError);
  EXPECT_EQ(PreOptRuns.load(std::memory_order_relaxed), 0u);
  EXPECT_TRUE(Result.Object.empty());
  EXPECT_TRUE(Result.Stores.empty());
}

TEST(ParallelCodeGenCacheTest,
     ProcessBudgetClampsPhysicalWorkersWithoutChangingObject) {
  ScopedEnvironmentVariable Threads("NEVERC_PCG_THREADS", "4");
  const neverc::ParallelCodeGenTuning Tuning =
      overlapTuning(/*MaxPartitions=*/4, /*SCEVThreshold=*/41);

  auto Run = [&](unsigned Tokens,
                 std::vector<ResourceGrantObservation> &Observations) {
    neverc::ProcessResourceBrokerConfig Config;
    Config.Enabled = true;
    Config.CpuTokens = Tokens;
    auto Broker = neverc::ProcessResourceBrokerTestAccess::create(Config);
    neverc::ScopedProcessResourceBrokerOverride Override(*Broker);
    neverc::ParallelCodeGenObservers Observers;
    Observers.ObserveResourceWorkerGrant =
        [&](neverc::ParallelCodeGenWorkerPhase Phase, unsigned Desired,
            unsigned Granted) {
          Observations.push_back({Phase, Desired, Granted});
        };
    neverc::ParallelOptimizationHooks Hooks = wholeModuleBarrier();
    DirectPCGRunResult Result =
        runFreshDirectPCG("resource-budget-identity", Tuning, &Hooks,
                          /*RecordCache=*/false, &Observers);
    const neverc::ProcessResourceBrokerSnapshot Snapshot =
        neverc::ProcessResourceBrokerTestAccess::snapshot(*Broker);
    EXPECT_EQ(Snapshot.HighWaterTokens, Tokens);
    EXPECT_EQ(Snapshot.ActiveTokens, 0U);
    EXPECT_EQ(Snapshot.ActiveSessions, 0U);
    return Result;
  };

  std::vector<ResourceGrantObservation> SerialObservations;
  DirectPCGRunResult Serial = Run(/*Tokens=*/1, SerialObservations);
  std::vector<ResourceGrantObservation> ParallelObservations;
  DirectPCGRunResult Parallel = Run(/*Tokens=*/2, ParallelObservations);
  std::vector<ResourceGrantObservation> FullObservations;
  DirectPCGRunResult Full = Run(/*Tokens=*/4, FullObservations);

  ASSERT_TRUE(Serial.Succeeded) << Serial.Failure;
  ASSERT_TRUE(Parallel.Succeeded) << Parallel.Failure;
  ASSERT_TRUE(Full.Succeeded) << Full.Failure;
  ASSERT_FALSE(Serial.Object.empty());
  EXPECT_EQ(Serial.Object, Parallel.Object);
  EXPECT_EQ(Serial.Object, Full.Object);
  const std::array ExpectedPhases = {
      neverc::ParallelCodeGenWorkerPhase::Prepare,
      neverc::ParallelCodeGenWorkerPhase::OptCodeGen,
      neverc::ParallelCodeGenWorkerPhase::Prepare,
      neverc::ParallelCodeGenWorkerPhase::CodeGen,
  };
  ASSERT_EQ(SerialObservations.size(), ExpectedPhases.size());
  ASSERT_EQ(ParallelObservations.size(), ExpectedPhases.size());
  ASSERT_EQ(FullObservations.size(), ExpectedPhases.size());
  for (std::size_t Index = 0; Index != ExpectedPhases.size(); ++Index) {
    EXPECT_EQ(SerialObservations[Index].Phase, ExpectedPhases[Index]);
    EXPECT_EQ(ParallelObservations[Index].Phase, ExpectedPhases[Index]);
    EXPECT_EQ(FullObservations[Index].Phase, ExpectedPhases[Index]);
  }
  for (const ResourceGrantObservation &Observation : SerialObservations) {
    EXPECT_EQ(Observation.DesiredWorkers, 4U);
    EXPECT_EQ(Observation.GrantedWorkers, 1U);
  }
  for (const ResourceGrantObservation &Observation : ParallelObservations) {
    EXPECT_EQ(Observation.DesiredWorkers, 4U);
    EXPECT_EQ(Observation.GrantedWorkers, 2U);
  }
  for (const ResourceGrantObservation &Observation : FullObservations) {
    EXPECT_EQ(Observation.DesiredWorkers, 4U);
    EXPECT_EQ(Observation.GrantedWorkers, 4U);
  }
}

void expectRequestTunings(
    const SessionSCEVProbeSnapshot &Snapshot, unsigned ExpectedPartitions,
    unsigned ExpectedThreshold,
    const llvm::NevercPipelineTuningOptions &ExpectedPipelineTuning,
    bool ExpectRereads) {
  const PipelineTuningValues ExpectedPipelineValues =
      pipelineTuningValues(ExpectedPipelineTuning);
  ASSERT_EQ(Snapshot.ConfiguredPartitionProbes, ExpectedPartitions);
  ASSERT_EQ(Snapshot.PartitionValues.size(), ExpectedPartitions);
  ASSERT_EQ(Snapshot.PartitionPipelineValues.size(), ExpectedPartitions);
  for (std::size_t PartitionIndex = 0;
       PartitionIndex != Snapshot.PartitionValues.size(); ++PartitionIndex) {
    const std::vector<unsigned> &Partition =
        Snapshot.PartitionValues[PartitionIndex];
    const std::vector<PipelineTuningValues> &PartitionPipeline =
        Snapshot.PartitionPipelineValues[PartitionIndex];
    ASSERT_FALSE(Partition.empty());
    for (unsigned Threshold : Partition)
      EXPECT_EQ(Threshold, ExpectedThreshold);
    ASSERT_EQ(PartitionPipeline.size(), Partition.size());
    for (const PipelineTuningValues &Values : PartitionPipeline)
      EXPECT_EQ(Values, ExpectedPipelineValues);
  }

  ASSERT_FALSE(Snapshot.WholeModuleValues.empty());
  for (unsigned Threshold : Snapshot.WholeModuleValues)
    EXPECT_EQ(Threshold, ExpectedThreshold);
  ASSERT_EQ(Snapshot.WholeModulePipelineValues.size(),
            Snapshot.WholeModuleValues.size());
  for (const PipelineTuningValues &Values : Snapshot.WholeModulePipelineValues)
    EXPECT_EQ(Values, ExpectedPipelineValues);

  for (unsigned Phase = 0; Phase != Snapshot.SessionARereads.size(); ++Phase) {
    if (ExpectRereads) {
      ASSERT_EQ(Snapshot.SessionARereads[Phase].size(), 1u);
      EXPECT_EQ(Snapshot.SessionARereads[Phase][0], ExpectedThreshold);
      ASSERT_EQ(Snapshot.SessionAPipelineRereads[Phase].size(), 1u);
      EXPECT_EQ(Snapshot.SessionAPipelineRereads[Phase][0],
                ExpectedPipelineValues);
    } else {
      EXPECT_TRUE(Snapshot.SessionARereads[Phase].empty());
      EXPECT_TRUE(Snapshot.SessionAPipelineRereads[Phase].empty());
    }
  }

  ASSERT_EQ(Snapshot.ResolvedFinalCodeGenPartitions.size(), 1u);
  EXPECT_EQ(Snapshot.ResolvedFinalCodeGenPartitions[0], ExpectedPartitions);
  ASSERT_EQ(Snapshot.FinalCodeGenPartitionPipelineValues.size(),
            ExpectedPartitions);
  for (const PipelineTuningValues &Values :
       Snapshot.FinalCodeGenPartitionPipelineValues)
    EXPECT_EQ(Values, ExpectedPipelineValues);
}

void expectTransactionalCache(const DirectPCGRunResult &Result,
                              unsigned ExpectedPartitions) {
  ASSERT_EQ(Result.LookupTags.size(), ExpectedPartitions);
  for (const std::string &Tag : Result.LookupTags)
    EXPECT_TRUE(StringRef(Tag).starts_with("p-opt-ir-v2;neverc-pcg-policy-v3"));

  ASSERT_EQ(Result.Stores.size(), ExpectedPartitions);
  for (const StoredArtifact &Store : Result.Stores) {
    EXPECT_TRUE(Store.OutputWasReady)
        << "optimized IR was committed before final object publication";
    EXPECT_FALSE(Store.Key.empty());
    ASSERT_FALSE(Store.Bytes.empty());

    LLVMContext ParseContext;
    auto Parsed = parseBitcodeFile(
        MemoryBufferRef(StringRef(Store.Bytes.data(), Store.Bytes.size()),
                        "task-local-scev-cache-entry"),
        ParseContext);
    ASSERT_TRUE(static_cast<bool>(Parsed))
        << toString(Parsed.takeError()).str().str();
  }
}

class SCEVThresholdCollector {
public:
  void record(unsigned Threshold) {
    std::lock_guard<std::mutex> Lock(Mutex);
    Values.push_back(Threshold);
  }

  std::vector<unsigned> snapshot() const {
    std::lock_guard<std::mutex> Lock(Mutex);
    return Values;
  }

private:
  mutable std::mutex Mutex;
  std::vector<unsigned> Values;
};

class SCEVThresholdCollectingPass
    : public PassInfoMixin<SCEVThresholdCollectingPass> {
public:
  explicit SCEVThresholdCollectingPass(SCEVThresholdCollector &Collector)
      : Collector(Collector) {}

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
    Collector.record(
        FAM.getResult<ScalarEvolutionAnalysis>(F).getHugeExpressionThreshold());
    return PreservedAnalyses::all();
  }

private:
  SCEVThresholdCollector &Collector;
};

class PipelineTuningCollector {
public:
  void record(const llvm::NevercPipelineTuningOptions &Tuning) {
    std::lock_guard<std::mutex> Lock(Mutex);
    Values.push_back(pipelineTuningValues(Tuning));
  }

  std::vector<PipelineTuningValues> snapshot() const {
    std::lock_guard<std::mutex> Lock(Mutex);
    return Values;
  }

private:
  mutable std::mutex Mutex;
  std::vector<PipelineTuningValues> Values;
};

class PipelineTuningCollectingModulePass
    : public PassInfoMixin<PipelineTuningCollectingModulePass> {
public:
  explicit PipelineTuningCollectingModulePass(
      PipelineTuningCollector &Collector)
      : Collector(Collector) {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
    Collector.record(M.getContext().getNevercPipelineTuningOptions());
    return PreservedAnalyses::all();
  }

private:
  PipelineTuningCollector &Collector;
};

} // namespace

TEST(ParallelCodeGenCacheTest,
     HeavyFirstScheduleUsesBothSignalsAndPartitionIdTieBreak) {
  std::unique_ptr<TargetMachine> Machine = createNativeTargetMachine();
  ASSERT_NE(Machine, nullptr);

  ScopedEnvironmentVariable Threads("NEVERC_PCG_THREADS", "1");
  LLVMContext Context;
  std::unique_ptr<Module> M =
      createScheduledModule(Context, *Machine, "heavy-first-schedule");
  const std::vector<ScheduledWorkEstimate> Work =
      captureScheduledWork(*M, /*NumPartitions=*/4);
  ASSERT_EQ(Work.size(), 4u);
  EXPECT_EQ(Work[0].InstructionWeight, 1501u);
  EXPECT_EQ(Work[0].LoopCount, 0u);
  EXPECT_EQ(Work[1].InstructionWeight, 8u);
  EXPECT_EQ(Work[1].LoopCount, 1u);
  EXPECT_EQ(Work[2].InstructionWeight, 8u);
  EXPECT_EQ(Work[2].LoopCount, 1u);
  EXPECT_EQ(Work[3].InstructionWeight, 22u);
  EXPECT_EQ(Work[3].LoopCount, 3u);
  SmallVector<char, 0> Output;
  raw_svector_ostream OutputStream(Output);

  neverc::ParallelCodeGenTuning Tuning;
  Tuning.MinDefinedFunctions = 1;
  Tuning.MinInstructionWeight = 0;
  Tuning.MinLoopCount = 0;
  Tuning.OptInstructionsPerPartition = 1000;
  Tuning.OptLoopsPerPartition = 1;
  Tuning.OptMaxPartitions = 4;

  std::vector<unsigned> ObservedOrder;
  neverc::ParallelCodeGenObservers Observers;
  Observers.ObservePartitionExecutionOrder =
      [&](neverc::ParallelCodeGenWorkerPhase Phase, ArrayRef<unsigned> Order) {
        switch (Phase) {
        case neverc::ParallelCodeGenWorkerPhase::Prepare:
          ADD_FAILURE() << "fused direct worker mislabeled as prepare";
          break;
        case neverc::ParallelCodeGenWorkerPhase::OptCodeGen:
          ObservedOrder.assign(Order.begin(), Order.end());
          break;
        case neverc::ParallelCodeGenWorkerPhase::CodeGen:
          ADD_FAILURE() << "unexpected codegen-only worker phase";
          break;
        }
      };

  ASSERT_TRUE(neverc::runParallelOptAndCodeGenWithTuning(
      *M, *Machine, neverc::ParallelCodeGenOutputs{OutputStream},
      /*OptLevel=*/2, Tuning, /*Cache=*/nullptr, /*Hooks=*/nullptr,
      &Observers));
  ASSERT_FALSE(Output.empty());
  expectObject(StringRef(Output.data(), Output.size()));

  EXPECT_EQ(ObservedOrder, (std::vector<unsigned>{3, 0, 1, 2}));
}

TEST(ParallelCodeGenCacheTest,
     BoundedInFlightCapsPreparedStateAtPhysicalWorkers) {
  std::unique_ptr<TargetMachine> Machine = createNativeTargetMachine();
  ASSERT_NE(Machine, nullptr);

  ScopedEnvironmentVariable Threads("NEVERC_PCG_THREADS", "2");
  ScopedEnvironmentVariable Reclaim("NEVERC_PCG_BENCH_EAGER_RECLAIM", "1");
  LLVMContext Context;
  std::unique_ptr<Module> M = createBoundedInFlightModule(
      Context, *Machine, "bounded-in-flight-direct-cold");
  SmallVector<char, 0> Output;
  raw_svector_ostream OutputStream(Output);

  neverc::ParallelCodeGenTuning Tuning;
  Tuning.MinDefinedFunctions = 1;
  Tuning.MinInstructionWeight = 0;
  Tuning.MinLoopCount = 0;
  Tuning.OptInstructionsPerPartition = 1;
  Tuning.OptLoopsPerPartition = 0;
  Tuning.OptMaxPartitions = 8;

  unsigned MaxLivePreparedPartitions = 0;
  unsigned PrepareGrantCount = 0;
  unsigned OptCodeGenGrantCount = 0;
  unsigned GrantedFusedWorkers = 0;
  std::optional<neverc::ParallelCodeGenRetentionSnapshot> AfterPrepareSnapshot;
  neverc::ParallelCodeGenObservers Observers;
  Observers.ObserveResourceWorkerGrant =
      [&](neverc::ParallelCodeGenWorkerPhase Phase, unsigned,
          unsigned Granted) {
        if (Phase == neverc::ParallelCodeGenWorkerPhase::Prepare) {
          ++PrepareGrantCount;
        } else if (Phase == neverc::ParallelCodeGenWorkerPhase::OptCodeGen) {
          ++OptCodeGenGrantCount;
          GrantedFusedWorkers = Granted;
        }
      };
  Observers.ObserveRetention =
      [&](neverc::ParallelCodeGenRetentionPoint Point,
          neverc::ParallelCodeGenRetentionSnapshot Snapshot) {
        MaxLivePreparedPartitions = std::max(
            MaxLivePreparedPartitions, Snapshot.MaxLivePreparedPartitions);
        if (Point == neverc::ParallelCodeGenRetentionPoint::AfterPrepare)
          AfterPrepareSnapshot = Snapshot;
      };

  ASSERT_TRUE(neverc::runParallelOptAndCodeGenWithTuning(
      *M, *Machine, neverc::ParallelCodeGenOutputs{OutputStream},
      /*OptLevel=*/2, Tuning,
      /*Cache=*/nullptr, /*Hooks=*/nullptr, &Observers));
  ASSERT_FALSE(Output.empty());
  expectObject(StringRef(Output.data(), Output.size()));
  EXPECT_EQ(PrepareGrantCount, 0u);
  EXPECT_EQ(OptCodeGenGrantCount, 1u);
  EXPECT_EQ(GrantedFusedWorkers, 2u);
  EXPECT_GT(MaxLivePreparedPartitions, 0u);
  EXPECT_LE(MaxLivePreparedPartitions, 2u);
  ASSERT_TRUE(AfterPrepareSnapshot.has_value());
  EXPECT_EQ(AfterPrepareSnapshot->LiveModules, 0u);
  EXPECT_EQ(AfterPrepareSnapshot->LiveContexts, 0u);
  EXPECT_EQ(AfterPrepareSnapshot->LiveTargetMachines, 0u);
}

TEST(ParallelCodeGenCacheTest,
     DirectPathReleasesPartitionStateBeforeObjectMerge) {
  const neverc::ParallelCodeGenTuning Tuning =
      overlapTuning(/*MaxPartitions=*/2, /*SCEVThreshold=*/41);
  using Record = std::pair<neverc::ParallelCodeGenRetentionPoint,
                           neverc::ParallelCodeGenRetentionSnapshot>;
  std::vector<Record> Records;
  neverc::ParallelCodeGenObservers Observers;
  Observers.ObserveRetention =
      [&](neverc::ParallelCodeGenRetentionPoint Point,
          neverc::ParallelCodeGenRetentionSnapshot Snapshot) {
        Records.emplace_back(Point, Snapshot);
      };

  DirectPCGRunResult Result =
      runFreshDirectPCG("direct-retention", Tuning, /*Hooks=*/nullptr,
                        /*RecordCache=*/true, &Observers);
  ASSERT_TRUE(Result.Succeeded) << Result.Failure;
  ASSERT_FALSE(Result.Object.empty());
  expectObject(StringRef(Result.Object.data(), Result.Object.size()));
  ASSERT_EQ(Result.LookupTags.size(), 2u);
  for (const std::string &Tag : Result.LookupTags)
    EXPECT_TRUE(StringRef(Tag).starts_with("p-opt-v2;neverc-pcg-policy-v3"));
  ASSERT_EQ(Result.Stores.size(), 2u);
  for (const StoredArtifact &Store : Result.Stores) {
    EXPECT_FALSE(Store.Key.empty());
    ASSERT_FALSE(Store.Bytes.empty());
    expectObject(StringRef(Store.Bytes.data(), Store.Bytes.size()));
  }

  auto At = [&](neverc::ParallelCodeGenRetentionPoint Point) {
    std::vector<neverc::ParallelCodeGenRetentionSnapshot> Matches;
    for (const Record &Entry : Records)
      if (Entry.first == Point)
        Matches.push_back(Entry.second);
    return Matches;
  };

  const auto AfterPrepare =
      At(neverc::ParallelCodeGenRetentionPoint::AfterPrepare);
  ASSERT_EQ(AfterPrepare.size(), 1u);
  EXPECT_GT(AfterPrepare[0].LiveModules, 0u);
  EXPECT_EQ(AfterPrepare[0].LiveModules, AfterPrepare[0].LiveContexts);
  EXPECT_EQ(AfterPrepare[0].LiveModules, AfterPrepare[0].LiveTargetMachines);
  EXPECT_EQ(AfterPrepare[0].FullBitcodeCapacityBytes, 0u);

  const auto AfterWork =
      At(neverc::ParallelCodeGenRetentionPoint::AfterPartitionWorkReclaim);
  ASSERT_EQ(AfterWork.size(), 1u);
  EXPECT_EQ(AfterWork[0].LiveModules, 0u);
  EXPECT_EQ(AfterWork[0].LiveContexts, 0u);
  EXPECT_EQ(AfterWork[0].LiveTargetMachines, 0u);

  const auto BeforeMerge =
      At(neverc::ParallelCodeGenRetentionPoint::BeforeObjectMerge);
  ASSERT_EQ(BeforeMerge.size(), 1u);
  EXPECT_EQ(BeforeMerge[0].LiveModules, 0u);
  EXPECT_EQ(BeforeMerge[0].LiveContexts, 0u);
  EXPECT_EQ(BeforeMerge[0].LiveTargetMachines, 0u);
  EXPECT_GT(BeforeMerge[0].ObjectBufferCapacityBytes, 0u);

  const auto Complete = At(neverc::ParallelCodeGenRetentionPoint::Complete);
  ASSERT_EQ(Complete.size(), 1u);
  EXPECT_EQ(Complete[0].FullBitcodeCapacityBytes, 0u);
  EXPECT_EQ(Complete[0].ObjectBufferCapacityBytes, 0u);
  EXPECT_EQ(Complete[0].SplitDwarfBufferCapacityBytes, 0u);

  DirectPCGRunResult Oracle = runFreshDirectPCG(
      "direct-retention", Tuning, /*Hooks=*/nullptr, /*RecordCache=*/false);
  ASSERT_TRUE(Oracle.Succeeded) << Oracle.Failure;
  EXPECT_EQ(Result.Object, Oracle.Object);
}

TEST(ParallelCodeGenCacheTest, BenchmarkReclaimArmsChangeOnlyRetainedState) {
  const neverc::ParallelCodeGenTuning Tuning =
      overlapTuning(/*MaxPartitions=*/2, /*SCEVThreshold=*/41);
  using Record = std::pair<neverc::ParallelCodeGenRetentionPoint,
                           neverc::ParallelCodeGenRetentionSnapshot>;

  auto RunArm = [&](const char *Arm, std::vector<Record> &Records) {
    ScopedEnvironmentVariable Reclaim("NEVERC_PCG_BENCH_EAGER_RECLAIM", Arm);
    neverc::ParallelCodeGenObservers Observers;
    Observers.ObserveRetention =
        [&](neverc::ParallelCodeGenRetentionPoint Point,
            neverc::ParallelCodeGenRetentionSnapshot Snapshot) {
          Records.emplace_back(Point, Snapshot);
        };
    return runFreshDirectPCG("benchmark-reclaim-arms", Tuning,
                             /*Hooks=*/nullptr, /*RecordCache=*/false,
                             &Observers);
  };
  auto At = [](const std::vector<Record> &Records,
               neverc::ParallelCodeGenRetentionPoint Point) {
    std::vector<neverc::ParallelCodeGenRetentionSnapshot> Matches;
    for (const Record &Entry : Records)
      if (Entry.first == Point)
        Matches.push_back(Entry.second);
    return Matches;
  };

  std::vector<Record> ReclaimRecords;
  DirectPCGRunResult Reclaim = RunArm("1", ReclaimRecords);
  ASSERT_TRUE(Reclaim.Succeeded) << Reclaim.Failure;
  ASSERT_FALSE(Reclaim.Object.empty());
  expectObject(StringRef(Reclaim.Object.data(), Reclaim.Object.size()));

  std::vector<Record> RetainRecords;
  DirectPCGRunResult Retain = RunArm("0", RetainRecords);
  ASSERT_TRUE(Retain.Succeeded) << Retain.Failure;
  ASSERT_FALSE(Retain.Object.empty());
  expectObject(StringRef(Retain.Object.data(), Retain.Object.size()));
  EXPECT_EQ(Reclaim.Object, Retain.Object);

  const auto ReclaimAfterPrepare =
      At(ReclaimRecords, neverc::ParallelCodeGenRetentionPoint::AfterPrepare);
  const auto RetainAfterPrepare =
      At(RetainRecords, neverc::ParallelCodeGenRetentionPoint::AfterPrepare);
  ASSERT_EQ(ReclaimAfterPrepare.size(), 1u);
  ASSERT_EQ(RetainAfterPrepare.size(), 1u);
  EXPECT_EQ(ReclaimAfterPrepare[0].FullBitcodeCapacityBytes, 0u);
  EXPECT_GT(RetainAfterPrepare[0].FullBitcodeCapacityBytes, 0u);

  const auto ReclaimAfterWork =
      At(ReclaimRecords,
         neverc::ParallelCodeGenRetentionPoint::AfterPartitionWorkReclaim);
  const auto RetainAfterWork =
      At(RetainRecords,
         neverc::ParallelCodeGenRetentionPoint::AfterPartitionWorkReclaim);
  ASSERT_EQ(ReclaimAfterWork.size(), 1u);
  ASSERT_EQ(RetainAfterWork.size(), 1u);
  EXPECT_EQ(ReclaimAfterWork[0].LiveModules, 0u);
  EXPECT_EQ(ReclaimAfterWork[0].LiveContexts, 0u);
  EXPECT_EQ(ReclaimAfterWork[0].LiveTargetMachines, 0u);
  EXPECT_GT(RetainAfterWork[0].LiveModules, 0u);
  EXPECT_EQ(RetainAfterWork[0].LiveModules, RetainAfterWork[0].LiveContexts);
  EXPECT_EQ(RetainAfterWork[0].LiveModules,
            RetainAfterWork[0].LiveTargetMachines);

  const auto ReclaimComplete =
      At(ReclaimRecords, neverc::ParallelCodeGenRetentionPoint::Complete);
  const auto RetainComplete =
      At(RetainRecords, neverc::ParallelCodeGenRetentionPoint::Complete);
  ASSERT_EQ(ReclaimComplete.size(), 1u);
  ASSERT_EQ(RetainComplete.size(), 1u);
  EXPECT_EQ(ReclaimComplete[0].ObjectBufferCapacityBytes, 0u);
  EXPECT_GT(RetainComplete[0].ObjectBufferCapacityBytes, 0u);
}

TEST(ParallelCodeGenCacheTest,
     WholeModuleBarrierReleasesOuterStateBeforeFinalCodeGen) {
  const neverc::ParallelCodeGenTuning Tuning =
      overlapTuning(/*MaxPartitions=*/2, /*SCEVThreshold=*/41);
  using Record = std::pair<neverc::ParallelCodeGenRetentionPoint,
                           neverc::ParallelCodeGenRetentionSnapshot>;
  std::vector<Record> Records;
  neverc::ParallelCodeGenObservers Observers;
  Observers.ObserveRetention =
      [&](neverc::ParallelCodeGenRetentionPoint Point,
          neverc::ParallelCodeGenRetentionSnapshot Snapshot) {
        Records.emplace_back(Point, Snapshot);
      };

  neverc::ParallelOptimizationHooks Hooks = wholeModuleBarrier();
  DirectPCGRunResult Result =
      runFreshDirectPCG("whole-module-retention", Tuning, &Hooks,
                        /*RecordCache=*/false, &Observers);
  ASSERT_TRUE(Result.Succeeded) << Result.Failure;
  ASSERT_FALSE(Result.Object.empty());
  expectObject(StringRef(Result.Object.data(), Result.Object.size()));

  auto At = [&](neverc::ParallelCodeGenRetentionPoint Point) {
    std::vector<neverc::ParallelCodeGenRetentionSnapshot> Matches;
    for (const Record &Entry : Records)
      if (Entry.first == Point)
        Matches.push_back(Entry.second);
    return Matches;
  };

  const auto AfterPrepare =
      At(neverc::ParallelCodeGenRetentionPoint::AfterPrepare);
  ASSERT_EQ(AfterPrepare.size(), 2u);
  for (const auto &Snapshot : AfterPrepare) {
    EXPECT_GT(Snapshot.LiveModules, 0u);
    EXPECT_EQ(Snapshot.FullBitcodeCapacityBytes, 0u);
  }

  const auto AfterWork =
      At(neverc::ParallelCodeGenRetentionPoint::AfterPartitionWorkReclaim);
  ASSERT_EQ(AfterWork.size(), 2u);
  EXPECT_GT(AfterWork[0].LiveModules, 0u);
  EXPECT_EQ(AfterWork[0].LiveModules, AfterWork[0].LiveContexts);
  EXPECT_EQ(AfterWork[0].LiveTargetMachines, 0u);
  EXPECT_EQ(AfterWork[1].LiveModules, 0u);
  EXPECT_EQ(AfterWork[1].LiveContexts, 0u);
  EXPECT_EQ(AfterWork[1].LiveTargetMachines, 0u);

  const auto BeforeWhole =
      At(neverc::ParallelCodeGenRetentionPoint::BeforeWholeModulePostOpt);
  ASSERT_EQ(BeforeWhole.size(), 1u);
  EXPECT_EQ(BeforeWhole[0].LiveModules, 0u);
  EXPECT_EQ(BeforeWhole[0].LiveContexts, 0u);
  EXPECT_EQ(BeforeWhole[0].LiveTargetMachines, 0u);

  const auto BeforeMerge =
      At(neverc::ParallelCodeGenRetentionPoint::BeforeObjectMerge);
  ASSERT_EQ(BeforeMerge.size(), 1u);
  EXPECT_EQ(BeforeMerge[0].LiveModules, 0u);
  EXPECT_EQ(BeforeMerge[0].LiveContexts, 0u);
  EXPECT_EQ(BeforeMerge[0].LiveTargetMachines, 0u);

  neverc::ParallelOptimizationHooks OracleHooks = wholeModuleBarrier();
  DirectPCGRunResult Oracle =
      runFreshDirectPCG("whole-module-retention", Tuning, &OracleHooks,
                        /*RecordCache=*/false);
  ASSERT_TRUE(Oracle.Succeeded) << Oracle.Failure;
  EXPECT_EQ(Result.Object, Oracle.Object);
}

TEST(ParallelCodeGenCacheTest,
     WholeModulePassStateDiesBeforeFinalCodeGenPartitions) {
  const neverc::ParallelCodeGenTuning Tuning =
      overlapTuning(/*MaxPartitions=*/2, /*SCEVThreshold=*/41);
  std::weak_ptr<unsigned> WholeModulePassLifetime;
  neverc::ParallelOptimizationHooks Hooks;
  Hooks.WholeModulePostOpt = [&](ModulePassManager &MPM) {
    auto Lifetime = std::make_shared<unsigned>(0);
    WholeModulePassLifetime = Lifetime;
    MPM.addPass(WholeModulePassLifetimeProbe(std::move(Lifetime)));
  };

  bool ObservedFinalCodeGen = false;
  bool ReleasedBeforeFinalCodeGen = false;
  neverc::ParallelCodeGenObservers Observers;
  Observers.ObserveResolvedFinalCodeGenPartitions = [&](unsigned) {
    ObservedFinalCodeGen = true;
    ReleasedBeforeFinalCodeGen = WholeModulePassLifetime.expired();
  };

  DirectPCGRunResult Result =
      runFreshDirectPCG("whole-module-pass-lifetime", Tuning, &Hooks,
                        /*RecordCache=*/false, &Observers);
  ASSERT_TRUE(Result.Succeeded) << Result.Failure;
  ASSERT_TRUE(ObservedFinalCodeGen);
  EXPECT_TRUE(ReleasedBeforeFinalCodeGen)
      << "whole-module pass state overlapped final partition codegen";
  EXPECT_TRUE(WholeModulePassLifetime.expired());
}

TEST(ParallelCodeGenCacheTest,
     BenchmarkNoReclaimKeepsWholeModulePassStateThroughFinalCodeGen) {
  ScopedEnvironmentVariable Reclaim("NEVERC_PCG_BENCH_EAGER_RECLAIM", "0");
  const neverc::ParallelCodeGenTuning Tuning =
      overlapTuning(/*MaxPartitions=*/2, /*SCEVThreshold=*/41);
  std::weak_ptr<unsigned> WholeModulePassLifetime;
  neverc::ParallelOptimizationHooks Hooks;
  Hooks.WholeModulePostOpt = [&](ModulePassManager &MPM) {
    auto Lifetime = std::make_shared<unsigned>(0);
    WholeModulePassLifetime = Lifetime;
    MPM.addPass(WholeModulePassLifetimeProbe(std::move(Lifetime)));
  };

  bool ObservedFinalCodeGen = false;
  bool RetainedDuringFinalCodeGen = false;
  neverc::ParallelCodeGenObservers Observers;
  Observers.ObserveResolvedFinalCodeGenSCEVThreshold = [&](unsigned) {
    ObservedFinalCodeGen = true;
    RetainedDuringFinalCodeGen = !WholeModulePassLifetime.expired();
  };

  DirectPCGRunResult Result =
      runFreshDirectPCG("whole-module-pass-no-reclaim", Tuning, &Hooks,
                        /*RecordCache=*/false, &Observers);
  ASSERT_TRUE(Result.Succeeded) << Result.Failure;
  ASSERT_TRUE(ObservedFinalCodeGen);
  EXPECT_TRUE(RetainedDuringFinalCodeGen)
      << "benchmark no-reclaim arm released whole-module pass state";
  EXPECT_TRUE(WholeModulePassLifetime.expired())
      << "whole-module pass state escaped the completed request";
}

TEST(ParallelCodeGenCacheTest, ScheduleWorkMathSaturatesAtUint64Boundary) {
  constexpr std::uint64_t Max = std::numeric_limits<std::uint64_t>::max();
  neverc::ParallelCodeGenWorkEstimate Estimate{Max - 2, Max - 3};

  neverc::accumulateParallelCodeGenWorkEstimate(
      Estimate, /*InstructionWeight=*/4, /*LoopCount=*/5);
  EXPECT_EQ(Estimate.InstructionWeight, Max);
  EXPECT_EQ(Estimate.LoopCount, Max);

  EXPECT_EQ(neverc::scoreParallelCodeGenWork(
                Estimate, /*WeightDiv=*/std::numeric_limits<unsigned>::max(),
                /*LoopDiv=*/std::numeric_limits<unsigned>::max()),
            Max);
  EXPECT_EQ(neverc::scoreParallelCodeGenWork(
                {Max, 0}, /*WeightDiv=*/1,
                /*LoopDiv=*/std::numeric_limits<unsigned>::max()),
            Max);
  EXPECT_EQ(neverc::scoreParallelCodeGenWork(Estimate, /*WeightDiv=*/0,
                                             /*LoopDiv=*/0),
            0u);
}

TEST(ParallelCodeGenCacheTest, CodeGenWorkerConsumesHeavyFirstExecutionOrder) {
  std::unique_ptr<TargetMachine> Machine = createNativeTargetMachine();
  ASSERT_NE(Machine, nullptr);

  ScopedEnvironmentVariable Threads("NEVERC_PCG_THREADS", "1");
  LLVMContext Context;
  std::unique_ptr<Module> M =
      createScheduledModule(Context, *Machine, "codegen-heavy-first-schedule");
  SmallVector<char, 0> Output;
  raw_svector_ostream OutputStream(Output);

  neverc::ParallelCodeGenTuning Tuning;
  Tuning.MinDefinedFunctions = 1;
  Tuning.MinInstructionWeight = 0;
  Tuning.MinLoopCount = 0;
  Tuning.CodeGenInstructionsPerPartition = 1000;
  Tuning.CodeGenLoopsPerPartition = 1;
  Tuning.CodeGenMaxPartitions = 4;

  std::vector<unsigned> PreparedOrder;
  std::vector<unsigned> CodeGenOrder;
  neverc::ParallelCodeGenObservers Observers;
  Observers.ObservePartitionExecutionOrder =
      [&](neverc::ParallelCodeGenWorkerPhase Phase, ArrayRef<unsigned> Order) {
        switch (Phase) {
        case neverc::ParallelCodeGenWorkerPhase::Prepare:
          PreparedOrder.assign(Order.begin(), Order.end());
          break;
        case neverc::ParallelCodeGenWorkerPhase::CodeGen:
          CodeGenOrder.assign(Order.begin(), Order.end());
          break;
        case neverc::ParallelCodeGenWorkerPhase::OptCodeGen:
          ADD_FAILURE() << "unexpected opt+codegen worker phase";
          break;
        }
      };

  ASSERT_TRUE(neverc::runParallelCodeGenWithTuning(
      *M, *Machine, neverc::ParallelCodeGenOutputs{OutputStream}, Tuning,
      /*Cache=*/nullptr, &Observers));
  ASSERT_FALSE(Output.empty());
  expectObject(StringRef(Output.data(), Output.size()));

  EXPECT_EQ(PreparedOrder, (std::vector<unsigned>{3, 0, 1, 2}));
  EXPECT_EQ(CodeGenOrder, (std::vector<unsigned>{3, 0, 1, 2}));
}

TEST(ParallelCodeGenCacheTest,
     ExecutionOrderChangesWithoutChangingPartitionIndexedObjectBytes) {
  std::unique_ptr<TargetMachine> Machine = createNativeTargetMachine();
  ASSERT_NE(Machine, nullptr);

  ScopedEnvironmentVariable Threads("NEVERC_PCG_THREADS", "1");
  ScopedEnvironmentVariable NoFailure("NEVERC_PCG_FORCE_MERGE_FAIL", nullptr);

  struct RunResult {
    std::vector<unsigned> Order;
    std::vector<char> Object;
  };
  auto Run = [&](unsigned WeightDiv, unsigned LoopDiv) {
    LLVMContext Context;
    std::unique_ptr<Module> M = createExecutionOrderInvariantModule(
        Context, *Machine, "execution-order-invariance");
    SmallVector<char, 0> Output;
    raw_svector_ostream OutputStream(Output);

    neverc::ParallelCodeGenTuning Tuning;
    Tuning.MinDefinedFunctions = 1;
    Tuning.MinInstructionWeight = 0;
    Tuning.MinLoopCount = 0;
    Tuning.OptInstructionsPerPartition = WeightDiv;
    Tuning.OptLoopsPerPartition = LoopDiv;
    Tuning.OptMaxPartitions = 4;

    RunResult Result;
    neverc::ParallelCodeGenObservers Observers;
    Observers.ObservePartitionExecutionOrder =
        [&](neverc::ParallelCodeGenWorkerPhase Phase,
            ArrayRef<unsigned> Order) {
          if (Phase == neverc::ParallelCodeGenWorkerPhase::OptCodeGen)
            Result.Order.assign(Order.begin(), Order.end());
        };
    EXPECT_TRUE(neverc::runParallelOptAndCodeGenWithTuning(
        *M, *Machine, neverc::ParallelCodeGenOutputs{OutputStream},
        /*OptLevel=*/2, Tuning, /*Cache=*/nullptr, /*Hooks=*/nullptr,
        &Observers));
    Result.Object.assign(Output.begin(), Output.end());
    return Result;
  };

  // Equal loop counts make the loop-only arm use the stable partition-id
  // tie-break. The weight-only arm moves the intentionally heavy partition 3
  // to the front without changing partition count or ownership.
  RunResult ByPartitionID = Run(/*WeightDiv=*/0, /*LoopDiv=*/1);
  RunResult HeavyFirst = Run(/*WeightDiv=*/1, /*LoopDiv=*/0);

  EXPECT_EQ(ByPartitionID.Order, (std::vector<unsigned>{0, 1, 2, 3}));
  EXPECT_EQ(HeavyFirst.Order, (std::vector<unsigned>{3, 0, 1, 2}));
  ASSERT_FALSE(ByPartitionID.Object.empty());
  ASSERT_FALSE(HeavyFirst.Object.empty());
  expectObject(
      StringRef(ByPartitionID.Object.data(), ByPartitionID.Object.size()));
  expectObject(StringRef(HeavyFirst.Object.data(), HeavyFirst.Object.size()));
  EXPECT_EQ(HeavyFirst.Object, ByPartitionID.Object);
}

TEST(ParallelCodeGenCacheTest,
     CombinedScheduleUsesExactMaxAndDisabledSignalsKeepIdOrder) {
  std::unique_ptr<TargetMachine> Machine = createNativeTargetMachine();
  ASSERT_NE(Machine, nullptr);

  ScopedEnvironmentVariable Threads("NEVERC_PCG_THREADS", "1");

  struct RunResult {
    std::vector<unsigned> OptimizedOrder;
    std::vector<char> Object;
  };
  auto Run = [&](unsigned WeightDiv, unsigned LoopDiv) {
    LLVMContext Context;
    std::unique_ptr<Module> M = createExactScheduleFormulaModule(
        Context, *Machine, "exact-schedule-formula");
    const std::vector<ScheduledWorkEstimate> Work =
        captureScheduledWork(*M, /*NumPartitions=*/2);
    EXPECT_EQ(Work[0].InstructionWeight, 707u);
    EXPECT_EQ(Work[0].LoopCount, 7u);
    EXPECT_EQ(Work[1].InstructionWeight, 901u);
    EXPECT_EQ(Work[1].LoopCount, 0u);

    SmallVector<char, 0> Output;
    raw_svector_ostream OutputStream(Output);
    neverc::ParallelCodeGenTuning Tuning;
    Tuning.MinDefinedFunctions = 1;
    Tuning.MinInstructionWeight = 0;
    Tuning.MinLoopCount = 0;
    Tuning.OptInstructionsPerPartition = WeightDiv;
    Tuning.OptLoopsPerPartition = LoopDiv;
    Tuning.OptMaxPartitions = 2;

    RunResult Result;
    neverc::ParallelCodeGenObservers Observers;
    Observers.ObservePartitionExecutionOrder =
        [&](neverc::ParallelCodeGenWorkerPhase Phase,
            ArrayRef<unsigned> Order) {
          if (Phase == neverc::ParallelCodeGenWorkerPhase::OptCodeGen)
            Result.OptimizedOrder.assign(Order.begin(), Order.end());
          else if (Phase == neverc::ParallelCodeGenWorkerPhase::Prepare)
            ADD_FAILURE() << "fused direct worker mislabeled as prepare";
          else
            ADD_FAILURE() << "unexpected codegen-only worker phase";
        };
    EXPECT_TRUE(neverc::runParallelOptAndCodeGenWithTuning(
        *M, *Machine, neverc::ParallelCodeGenOutputs{OutputStream},
        /*OptLevel=*/2, Tuning, /*Cache=*/nullptr, /*Hooks=*/nullptr,
        &Observers));
    Result.Object.assign(Output.begin(), Output.end());
    return Result;
  };

  RunResult ExactMax = Run(/*WeightDiv=*/1000, /*LoopDiv=*/10);
  RunResult SignalsDisabled = Run(/*WeightDiv=*/0, /*LoopDiv=*/0);
  EXPECT_EQ(ExactMax.OptimizedOrder, (std::vector<unsigned>{1, 0}));
  EXPECT_EQ(SignalsDisabled.OptimizedOrder, (std::vector<unsigned>{0, 1}));
  ASSERT_FALSE(ExactMax.Object.empty());
  ASSERT_FALSE(SignalsDisabled.Object.empty());
  expectObject(StringRef(ExactMax.Object.data(), ExactMax.Object.size()));
  expectObject(
      StringRef(SignalsDisabled.Object.data(), SignalsDisabled.Object.size()));
  EXPECT_EQ(ExactMax.Object, SignalsDisabled.Object);
}

TEST(ParallelCodeGenCacheTest,
     EmptyHashBinsCompactBeforeFinalCountAndWorkScheduling) {
  std::unique_ptr<TargetMachine> Machine = createNativeTargetMachine();
  ASSERT_NE(Machine, nullptr);

  ScopedEnvironmentVariable Threads("NEVERC_PCG_THREADS", "1");
  LLVMContext Context;
  std::unique_ptr<Module> M =
      createCompactedScheduleModule(Context, *Machine, "compacted-schedule");
  SmallVector<char, 0> Output;
  raw_svector_ostream OutputStream(Output);

  neverc::ParallelCodeGenTuning Tuning;
  Tuning.MinDefinedFunctions = 1;
  Tuning.MinInstructionWeight = 0;
  Tuning.MinLoopCount = 0;
  Tuning.CodeGenInstructionsPerPartition = 1;
  Tuning.CodeGenLoopsPerPartition = 0;
  Tuning.CodeGenMaxPartitions = 4;

  unsigned FinalPartitions = 0;
  std::vector<unsigned> PreparedOrder;
  std::vector<unsigned> CodeGenOrder;
  neverc::ParallelCodeGenObservers Observers;
  Observers.ObserveResolvedFinalCodeGenPartitions = [&](unsigned Count) {
    FinalPartitions = Count;
  };
  Observers.ObservePartitionExecutionOrder =
      [&](neverc::ParallelCodeGenWorkerPhase Phase, ArrayRef<unsigned> Order) {
        if (Phase == neverc::ParallelCodeGenWorkerPhase::Prepare)
          PreparedOrder.assign(Order.begin(), Order.end());
        else if (Phase == neverc::ParallelCodeGenWorkerPhase::CodeGen)
          CodeGenOrder.assign(Order.begin(), Order.end());
        else
          ADD_FAILURE() << "unexpected opt+codegen worker phase";
      };

  ASSERT_TRUE(neverc::runParallelCodeGenWithTuning(
      *M, *Machine, neverc::ParallelCodeGenOutputs{OutputStream}, Tuning,
      /*Cache=*/nullptr, &Observers));
  ASSERT_FALSE(Output.empty());
  expectObject(StringRef(Output.data(), Output.size()));
  EXPECT_EQ(FinalPartitions, 2u);
  EXPECT_EQ(PreparedOrder, (std::vector<unsigned>{1, 0}));
  EXPECT_EQ(CodeGenOrder, (std::vector<unsigned>{1, 0}));
}

TEST(ParallelCodeGenCacheTest,
     OptimizedIRCommitsOnlyAfterValidatedFinalCodegen) {
#ifdef _WIN32
  GTEST_SKIP() << "fault-injection environment guard is POSIX-only";
#else
  std::unique_ptr<TargetMachine> Machine = createNativeTargetMachine();
  ASSERT_NE(Machine, nullptr);

  {
    LLVMContext Context;
    std::unique_ptr<Module> M =
        createLoopDenseModule(Context, *Machine, "cache-transaction-failure");
    SmallVector<char, 0> Output;
    raw_svector_ostream OutputStream(Output);
    RecordingCache Cache(Output);
    neverc::PartitionCacheHooks CacheHooks = Cache.hooks();
    neverc::ParallelOptimizationHooks OptimizationHooks = wholeModuleBarrier();
    ScopedEnvironmentVariable Strict("NEVERC_PCG_STRICT", nullptr);
    ScopedEnvironmentVariable ForceFailure("NEVERC_PCG_FORCE_MERGE_FAIL", "1");

    ASSERT_TRUE(neverc::runParallelOptAndCodeGen(
        *M, *Machine, neverc::ParallelCodeGenOutputs{OutputStream},
        /*NumPartitions=*/4, /*OptLevel=*/2, &CacheHooks, &OptimizationHooks));
    ASSERT_FALSE(Output.empty());
    expectObject(StringRef(Output.data(), Output.size()));

    const std::vector<std::string> LookupTags = Cache.lookupTags();
    ASSERT_GE(LookupTags.size(), 2u)
        << "the failure case must engage partitioned optimized-IR caching";
    for (const std::string &Tag : LookupTags)
      EXPECT_TRUE(
          StringRef(Tag).starts_with("p-opt-ir-v2;neverc-pcg-policy-v3"));
    EXPECT_TRUE(Cache.stores().empty())
        << "a serial fallback must not commit optimized partitions rejected "
           "by final parallel codegen";
  }

  {
    LLVMContext Context;
    std::unique_ptr<Module> M =
        createLoopDenseModule(Context, *Machine, "cache-transaction-success");
    SmallVector<char, 0> Output;
    raw_svector_ostream OutputStream(Output);
    RecordingCache Cache(Output);
    neverc::PartitionCacheHooks CacheHooks = Cache.hooks();
    neverc::ParallelOptimizationHooks OptimizationHooks = wholeModuleBarrier();
    ScopedEnvironmentVariable Strict("NEVERC_PCG_STRICT", nullptr);
    ScopedEnvironmentVariable NoFailure("NEVERC_PCG_FORCE_MERGE_FAIL", nullptr);

    ASSERT_TRUE(neverc::runParallelOptAndCodeGen(
        *M, *Machine, neverc::ParallelCodeGenOutputs{OutputStream},
        /*NumPartitions=*/4, /*OptLevel=*/2, &CacheHooks, &OptimizationHooks));
    ASSERT_FALSE(Output.empty());
    expectObject(StringRef(Output.data(), Output.size()));

    const std::vector<std::string> LookupTags = Cache.lookupTags();
    ASSERT_GE(LookupTags.size(), 2u)
        << "the success case prevents a false green from a declined cache";
    for (const std::string &Tag : LookupTags)
      EXPECT_TRUE(
          StringRef(Tag).starts_with("p-opt-ir-v2;neverc-pcg-policy-v3"));

    const std::vector<StoredArtifact> Stores = Cache.stores();
    ASSERT_GE(Stores.size(), 2u);
    for (const StoredArtifact &Store : Stores) {
      EXPECT_TRUE(Store.OutputWasReady)
          << "cache Store ran before the final object was committed";
      EXPECT_FALSE(Store.Key.empty());
      ASSERT_FALSE(Store.Bytes.empty());

      LLVMContext ParseContext;
      auto Parsed = parseBitcodeFile(
          MemoryBufferRef(StringRef(Store.Bytes.data(), Store.Bytes.size()),
                          "optimized-partition-cache-entry"),
          ParseContext);
      ASSERT_TRUE(static_cast<bool>(Parsed))
          << toString(Parsed.takeError()).str().str();
      std::string VerificationError;
      raw_string_ostream VerificationStream(VerificationError);
      EXPECT_FALSE(verifyModule(**Parsed, &VerificationStream))
          << VerificationStream.str();
    }
  }
#endif
}

TEST(ParallelCodeGenCacheTest,
     OptimizationLevelSeparatesDirectObjectCacheEntries) {
  std::unique_ptr<TargetMachine> Machine = createNativeTargetMachine();
  ASSERT_NE(Machine, nullptr);

  ReplayCache Cache;
  neverc::PartitionCacheHooks CacheHooks = Cache.hooks();
  const neverc::ParallelCodeGenTuning Tuning =
      overlapTuning(/*MaxPartitions=*/2, /*SCEVThreshold=*/41);

  auto Run = [&](unsigned OptLevel) {
    LLVMContext Context;
    std::unique_ptr<Module> M =
        createLoopDenseModule(Context, *Machine, "opt-level-cache-isolation");
    SmallVector<char, 0> Output;
    raw_svector_ostream OutputStream(Output);
    EXPECT_TRUE(neverc::runParallelOptAndCodeGenWithTuning(
        *M, *Machine, neverc::ParallelCodeGenOutputs{OutputStream}, OptLevel,
        Tuning, &CacheHooks));
    EXPECT_FALSE(Output.empty());
    if (!Output.empty())
      expectObject(StringRef(Output.data(), Output.size()));
  };

  Run(/*OptLevel=*/1);
  const unsigned StoresAfterPopulate = Cache.storeCount();
  ASSERT_GT(Cache.lookupCount(), 0u);
  ASSERT_GT(StoresAfterPopulate, 0u);
  ASSERT_EQ(Cache.hitCount(), 0u);

  Run(/*OptLevel=*/1);
  const unsigned HitsAfterSameLevelReplay = Cache.hitCount();
  ASSERT_GT(HitsAfterSameLevelReplay, 0u)
      << "the test cache must demonstrably replay O1 entries";
  ASSERT_EQ(Cache.storeCount(), StoresAfterPopulate);

  Run(/*OptLevel=*/3);
  EXPECT_EQ(Cache.hitCount(), HitsAfterSameLevelReplay)
      << "an O3 request must not reuse objects optimized at O1";
  EXPECT_GT(Cache.storeCount(), StoresAfterPopulate)
      << "an isolated O3 request must populate its own cache entries";
}

TEST(ParallelCodeGenCacheTest,
     BenchmarkReclaimPolicyDoesNotEnterCacheIdentity) {
  std::unique_ptr<TargetMachine> Machine = createNativeTargetMachine();
  ASSERT_NE(Machine, nullptr);
  const neverc::ParallelCodeGenTuning Tuning =
      overlapTuning(/*MaxPartitions=*/2, /*SCEVThreshold=*/41);

  auto RunPair = [&](const char *ColdArm, const char *WarmArm) {
    ReplayCache Cache;
    neverc::PartitionCacheHooks CacheHooks = Cache.hooks();
    auto Run = [&](const char *Arm) {
      ScopedEnvironmentVariable Reclaim("NEVERC_PCG_BENCH_EAGER_RECLAIM", Arm);
      LLVMContext Context;
      std::unique_ptr<Module> M = createLoopDenseModule(
          Context, *Machine, "benchmark-reclaim-cache-identity");
      SmallVector<char, 0> Output;
      raw_svector_ostream OutputStream(Output);
      EXPECT_TRUE(neverc::runParallelOptAndCodeGenWithTuning(
          *M, *Machine, neverc::ParallelCodeGenOutputs{OutputStream},
          /*OptLevel=*/2, Tuning, &CacheHooks));
      EXPECT_FALSE(Output.empty());
      if (!Output.empty())
        expectObject(StringRef(Output.data(), Output.size()));
      return std::vector<char>(Output.begin(), Output.end());
    };

    const std::vector<char> ColdOutput = Run(ColdArm);
    const unsigned StoresAfterCold = Cache.storeCount();
    ASSERT_GT(Cache.lookupCount(), 0u);
    ASSERT_GT(StoresAfterCold, 0u);
    ASSERT_EQ(Cache.hitCount(), 0u);

    const std::vector<char> WarmOutput = Run(WarmArm);
    EXPECT_GT(Cache.hitCount(), 0u)
        << "the opposite reclaim arm did not reuse identical cache entries";
    EXPECT_EQ(Cache.storeCount(), StoresAfterCold)
        << "the reclaim policy entered the cache identity";
    EXPECT_EQ(ColdOutput, WarmOutput);
  };

  RunPair(/*ColdArm=*/"0", /*WarmArm=*/"1");
  RunPair(/*ColdArm=*/"1", /*WarmArm=*/"0");
}

TEST(ParallelCodeGenCacheTest,
     OptimizedIRWarmHitSkipsPartitionOptimizationAndPreservesPolicy) {
  std::unique_ptr<TargetMachine> Machine = createNativeTargetMachine();
  ASSERT_NE(Machine, nullptr);

  ReplayCache Cache;
  neverc::PartitionCacheHooks CacheHooks = Cache.hooks();
  const neverc::ParallelCodeGenTuning Tuning =
      overlapTuning(/*MaxPartitions=*/2, /*SCEVThreshold=*/41);
  llvm::NevercPipelineTuningOptions PipelineTuning;
  PipelineTuning.ModuleInlinerThreshold = 3131;
  PipelineTuning.AutoLTOInlineThreshold = -47;
  PipelineTuning.InlinerLiteFSimpl = false;
  PipelineTuning.InlineMaxCallerLoops = 7;
  PipelineTuning.FullUnrollMaxLoopsPerFunction = 23;
  const PipelineTuningValues ExpectedPipelineTuning =
      pipelineTuningValues(PipelineTuning);
  using RetentionRecord = std::pair<neverc::ParallelCodeGenRetentionPoint,
                                    neverc::ParallelCodeGenRetentionSnapshot>;

  auto Run = [&](neverc::ParallelOptimizationHooks &Hooks,
                 const neverc::ParallelCodeGenObservers *Observers,
                 std::vector<char> &Object) {
    LLVMContext Context;
    std::unique_ptr<Module> M =
        createLoopDenseModule(Context, *Machine, "optimized-ir-cache-replay");
    SmallVector<char, 0> Output;
    raw_svector_ostream OutputStream(Output);
    EXPECT_TRUE(neverc::runParallelOptAndCodeGenWithTunings(
        *M, *Machine, neverc::ParallelCodeGenOutputs{OutputStream},
        /*OptLevel=*/2, Tuning, PipelineTuning, &CacheHooks, &Hooks,
        Observers));
    EXPECT_FALSE(Output.empty());
    if (!Output.empty())
      expectObject(StringRef(Output.data(), Output.size()));
    Object.assign(Output.begin(), Output.end());
  };

  std::atomic<unsigned> ColdPreOptBuilds{0};
  PipelineTuningCollector ColdWholeModuleTunings;
  neverc::ParallelOptimizationHooks ColdHooks;
  ColdHooks.PreOpt = [&](ModulePassManager &) {
    ColdPreOptBuilds.fetch_add(1, std::memory_order_relaxed);
  };
  ColdHooks.WholeModulePostOpt = [&](ModulePassManager &MPM) {
    MPM.addPass(PipelineTuningCollectingModulePass(ColdWholeModuleTunings));
  };
  std::vector<RetentionRecord> ColdRetention;
  neverc::ParallelCodeGenObservers ColdObservers;
  ColdObservers.ObserveRetention =
      [&](neverc::ParallelCodeGenRetentionPoint Point,
          neverc::ParallelCodeGenRetentionSnapshot Snapshot) {
        ColdRetention.emplace_back(Point, Snapshot);
      };
  std::vector<char> ColdObject;
  Run(ColdHooks, &ColdObservers, ColdObject);

  const unsigned StoresAfterColdRun = Cache.storeCount();
  ASSERT_GT(StoresAfterColdRun, 0u);
  ASSERT_EQ(Cache.hitCount(), 0u);
  EXPECT_EQ(ColdPreOptBuilds.load(std::memory_order_relaxed),
            StoresAfterColdRun);
  ASSERT_EQ(ColdWholeModuleTunings.snapshot().size(), 1u);
  EXPECT_EQ(ColdWholeModuleTunings.snapshot()[0], ExpectedPipelineTuning);
  auto ColdBeforeWhole = llvm::find_if(ColdRetention, [](const auto &Entry) {
    return Entry.first ==
           neverc::ParallelCodeGenRetentionPoint::BeforeWholeModulePostOpt;
  });
  ASSERT_NE(ColdBeforeWhole, ColdRetention.end());
  EXPECT_GT(ColdBeforeWhole->second.PendingOptimizedIRCapacityBytes, 0u)
      << "the cold run must stage optimized IR before committing its cache";
  ASSERT_FALSE(ColdRetention.empty());
  EXPECT_EQ(ColdRetention.back().first,
            neverc::ParallelCodeGenRetentionPoint::Complete);
  EXPECT_EQ(ColdRetention.back().second.PendingOptimizedIRCapacityBytes, 0u);
  EXPECT_EQ(ColdRetention.back().second.LiveModules, 0u);
  EXPECT_EQ(ColdRetention.back().second.LiveContexts, 0u);

  auto &RegisteredOptions = cl::getRegisteredOptions();
  auto AmbientCapIt = RegisteredOptions.find("neverc-inline-max-caller-loops");
  ASSERT_NE(AmbientCapIt, RegisteredOptions.end());
  cl::Option *AmbientCap = AmbientCapIt->second;
  std::function<void()> RestoreAmbientCap = AmbientCap->createStateRestorer();
  auto AmbientCapRestorer = make_scope_exit([&] { RestoreAmbientCap(); });
  AmbientCap->reset();
  ASSERT_FALSE(AmbientCap->addOccurrence(
      /*pos=*/1, AmbientCap->ArgStr, /*Value=*/"101"));

  std::atomic<unsigned> WarmPreOptBuilds{0};
  PipelineTuningCollector WarmWholeModuleTunings;
  PipelineTuningCollector WarmFinalCodeGenTunings;
  neverc::ParallelOptimizationHooks WarmHooks;
  WarmHooks.PreOpt = [&](ModulePassManager &) {
    WarmPreOptBuilds.fetch_add(1, std::memory_order_relaxed);
  };
  WarmHooks.WholeModulePostOpt = [&](ModulePassManager &MPM) {
    MPM.addPass(PipelineTuningCollectingModulePass(WarmWholeModuleTunings));
  };
  neverc::ParallelCodeGenObservers WarmObservers;
  std::vector<RetentionRecord> WarmRetention;
  WarmObservers.ObserveFinalCodeGenPartitionPipelineTuning =
      [&](unsigned, const llvm::NevercPipelineTuningOptions &Observed) {
        WarmFinalCodeGenTunings.record(Observed);
      };
  WarmObservers.ObserveRetention =
      [&](neverc::ParallelCodeGenRetentionPoint Point,
          neverc::ParallelCodeGenRetentionSnapshot Snapshot) {
        WarmRetention.emplace_back(Point, Snapshot);
      };
  std::vector<char> WarmObject;
  Run(WarmHooks, &WarmObservers, WarmObject);

  EXPECT_EQ(Cache.hitCount(), StoresAfterColdRun)
      << "every cold optimized-IR entry must be replayed";
  EXPECT_EQ(Cache.storeCount(), StoresAfterColdRun)
      << "cache hits must not be rewritten";
  EXPECT_EQ(WarmPreOptBuilds.load(std::memory_order_relaxed), 0u)
      << "a warm optimized-IR hit must bypass partition optimization";
  EXPECT_EQ(WarmObject, ColdObject);

  const std::vector<PipelineTuningValues> WarmWholeValues =
      WarmWholeModuleTunings.snapshot();
  ASSERT_EQ(WarmWholeValues.size(), 1u);
  EXPECT_EQ(WarmWholeValues[0], ExpectedPipelineTuning);
  const std::vector<PipelineTuningValues> WarmFinalValues =
      WarmFinalCodeGenTunings.snapshot();
  ASSERT_FALSE(WarmFinalValues.empty());
  for (const PipelineTuningValues &Values : WarmFinalValues)
    EXPECT_EQ(Values, ExpectedPipelineTuning);
  ASSERT_FALSE(WarmRetention.empty());
  EXPECT_EQ(WarmRetention.back().first,
            neverc::ParallelCodeGenRetentionPoint::Complete);
  EXPECT_EQ(WarmRetention.back().second.PendingOptimizedIRCapacityBytes, 0u);
  EXPECT_EQ(WarmRetention.back().second.LiveModules, 0u);
  EXPECT_EQ(WarmRetention.back().second.LiveContexts, 0u);
}

TEST(ParallelCodeGenCacheTest,
     TaskLocalPipelineTuningsOverlapInsidePartitionOptimization) {
  // Complete LLVM's native-target initialization before either PCG request
  // starts; each request below still owns a fresh TargetMachine.
  ASSERT_TRUE(createNativeTargetMachine());

  auto &RegisteredOptions = cl::getRegisteredOptions();
  auto AmbientMaxIt = RegisteredOptions.find("neverc-pcg-cg-max-parts");
  ASSERT_NE(AmbientMaxIt, RegisteredOptions.end());
  cl::Option *AmbientMax = AmbientMaxIt->second;
  std::function<void()> RestoreAmbientMax = AmbientMax->createStateRestorer();
  auto AmbientMaxRestorer = make_scope_exit([&] { RestoreAmbientMax(); });
  AmbientMax->reset();
  ASSERT_FALSE(AmbientMax->addOccurrence(
      /*pos=*/1, AmbientMax->ArgStr, /*Value=*/"1"));

  ScopedEnvironmentVariable Threads("NEVERC_PCG_THREADS", nullptr);
  ScopedEnvironmentVariable ForceFailure("NEVERC_PCG_FORCE_MERGE_FAIL",
                                         nullptr);
  SCEVProbeRendezvous Rendezvous;
  SessionSCEVProbeState AState(/*Session=*/0, Rendezvous);
  SessionSCEVProbeState BState(/*Session=*/1, Rendezvous);
  neverc::ParallelCodeGenObservers AObservers;
  neverc::ParallelCodeGenObservers BObservers;
  neverc::ParallelOptimizationHooks AHooks = overlapHooks(AState, AObservers);
  neverc::ParallelOptimizationHooks BHooks = overlapHooks(BState, BObservers);
  const neverc::ParallelCodeGenTuning ATuning =
      overlapTuning(/*MaxPartitions=*/2, /*SCEVThreshold=*/41);
  const neverc::ParallelCodeGenTuning BTuning =
      overlapTuning(/*MaxPartitions=*/4, /*SCEVThreshold=*/83);
  llvm::NevercPipelineTuningOptions APipelineTuning;
  APipelineTuning.ModuleInlinerThreshold = 1111;
  APipelineTuning.AutoLTOInlineThreshold = -31;
  APipelineTuning.InlinerLiteFSimpl = false;
  APipelineTuning.InlineMaxCallerLoops = 7;
  APipelineTuning.FullUnrollMaxLoopsPerFunction = 9;
  llvm::NevercPipelineTuningOptions BPipelineTuning;
  BPipelineTuning.ModuleInlinerThreshold = 2222;
  BPipelineTuning.AutoLTOInlineThreshold = 311;
  BPipelineTuning.InlinerLiteFSimpl = true;
  BPipelineTuning.InlineMaxCallerLoops = 13;
  BPipelineTuning.FullUnrollMaxLoopsPerFunction = 17;

  DirectPCGRunResult AResult;
  DirectPCGRunResult BResult;
  std::thread AThread([&] {
    auto Finished = make_scope_exit([&] { Rendezvous.sessionFinished(0); });
    AResult =
        runFreshDirectPCG("task-local-scev-a", ATuning, &AHooks,
                          /*RecordCache=*/true, &AObservers, &APipelineTuning);
  });
  std::thread BThread([&] {
    auto Finished = make_scope_exit([&] { Rendezvous.sessionFinished(1); });
    BResult =
        runFreshDirectPCG("task-local-scev-b", BTuning, &BHooks,
                          /*RecordCache=*/true, &BObservers, &BPipelineTuning);
  });
  AThread.join();
  BThread.join();

  const SCEVProbeRendezvous::Snapshot RendezvousSnapshot =
      Rendezvous.snapshot();
  EXPECT_FALSE(RendezvousSnapshot.Cancelled) << RendezvousSnapshot.Failure;
  EXPECT_EQ(RendezvousSnapshot.FinishedMask, 0b11u);
  for (unsigned Phase = 0; Phase != 2; ++Phase) {
    EXPECT_EQ(RendezvousSnapshot.ArrivedMasks[Phase], 0b11u);
    EXPECT_EQ(RendezvousSnapshot.CompletedMasks[Phase], 0b11u);
    EXPECT_TRUE(RendezvousSnapshot.SessionAReread[Phase]);
  }

  ASSERT_TRUE(AResult.Succeeded) << AResult.Failure;
  ASSERT_TRUE(BResult.Succeeded) << BResult.Failure;
  ASSERT_FALSE(AResult.Object.empty());
  ASSERT_FALSE(BResult.Object.empty());
  expectObject(StringRef(AResult.Object.data(), AResult.Object.size()));
  expectObject(StringRef(BResult.Object.data(), BResult.Object.size()));

  expectRequestTunings(AState.snapshot(), /*ExpectedPartitions=*/2,
                       /*ExpectedThreshold=*/41, APipelineTuning,
                       /*ExpectRereads=*/true);
  expectRequestTunings(BState.snapshot(), /*ExpectedPartitions=*/4,
                       /*ExpectedThreshold=*/83, BPipelineTuning,
                       /*ExpectRereads=*/false);
  expectTransactionalCache(AResult, /*ExpectedPartitions=*/2);
  expectTransactionalCache(BResult, /*ExpectedPartitions=*/4);

  // Each oracle owns a new context, module, machine, cache, and destination.
  // The analysis-only probes above cannot change emitted bytes, so exact
  // identity proves concurrency did not leak one request's tuning into the
  // other request's optimization or final codegen.
  neverc::ParallelOptimizationHooks AOracleHooks = wholeModuleBarrier();
  neverc::ParallelOptimizationHooks BOracleHooks = wholeModuleBarrier();
  DirectPCGRunResult AOracle = runFreshDirectPCG(
      "task-local-scev-a", ATuning, &AOracleHooks, /*RecordCache=*/true,
      /*Observers=*/nullptr, &APipelineTuning);
  DirectPCGRunResult BOracle = runFreshDirectPCG(
      "task-local-scev-b", BTuning, &BOracleHooks, /*RecordCache=*/true,
      /*Observers=*/nullptr, &BPipelineTuning);
  ASSERT_TRUE(AOracle.Succeeded) << AOracle.Failure;
  ASSERT_TRUE(BOracle.Succeeded) << BOracle.Failure;
  expectObject(StringRef(AOracle.Object.data(), AOracle.Object.size()));
  expectObject(StringRef(BOracle.Object.data(), BOracle.Object.size()));
  EXPECT_EQ(AResult.Object, AOracle.Object);
  EXPECT_EQ(BResult.Object, BOracle.Object);
}

TEST(ParallelCodeGenCacheTest,
     AmbientZeroSCEVThresholdIsCapturedAsLiteralValue) {
  ASSERT_TRUE(createNativeTargetMachine());

  auto &RegisteredOptions = cl::getRegisteredOptions();
  auto ThresholdIt =
      RegisteredOptions.find("scalar-evolution-huge-expr-threshold");
  ASSERT_NE(ThresholdIt, RegisteredOptions.end());
  cl::Option *AmbientThreshold = ThresholdIt->second;
  std::function<void()> RestoreAmbientThreshold =
      AmbientThreshold->createStateRestorer();
  auto AmbientThresholdRestorer =
      make_scope_exit([&] { RestoreAmbientThreshold(); });
  AmbientThreshold->reset();
  ASSERT_FALSE(AmbientThreshold->addOccurrence(
      /*pos=*/1, AmbientThreshold->ArgStr, /*Value=*/"0"));

  // One worker makes the deliberate post-freeze ambient mutation race-free.
  // Correct PCG has already resolved inherited zero into an engaged PTO value;
  // an implementation that loses literal zero and consults the global later
  // will instead expose 97 to every probe.
  ScopedEnvironmentVariable Threads("NEVERC_PCG_THREADS", "1");
  ScopedEnvironmentVariable ForceFailure("NEVERC_PCG_FORCE_MERGE_FAIL",
                                         nullptr);
  std::once_flag MutateAmbientOnce;
  std::atomic<bool> AmbientMutationFailed{false};
  SCEVThresholdCollector Collector;
  SCEVThresholdCollector FinalCodeGenCollector;
  neverc::ParallelOptimizationHooks Hooks;
  Hooks.ConfigurePassBuilder = [&](PassBuilder &) {
    std::call_once(MutateAmbientOnce, [&] {
      AmbientThreshold->reset();
      AmbientMutationFailed.store(
          AmbientThreshold->addOccurrence(
              /*pos=*/2, AmbientThreshold->ArgStr, /*Value=*/"97"),
          std::memory_order_release);
    });
  };
  Hooks.PreOpt = [&](ModulePassManager &MPM) {
    MPM.addPass(createModuleToFunctionPassAdaptor(
        SCEVThresholdCollectingPass(Collector)));
  };
  // Force the nested final-codegen path after the outer request has frozen the
  // ambient zero and ConfigurePassBuilder has changed the process global.
  Hooks.WholeModulePostOpt = [](ModulePassManager &) {};
  neverc::ParallelCodeGenObservers Observers;
  Observers.ObserveResolvedFinalCodeGenSCEVThreshold = [&](unsigned Threshold) {
    FinalCodeGenCollector.record(Threshold);
  };

  neverc::ParallelCodeGenTuning Tuning =
      overlapTuning(/*MaxPartitions=*/2, /*SCEVThreshold=*/0);
  DirectPCGRunResult Result = runFreshDirectPCG(
      "ambient-zero-scev", Tuning, &Hooks, /*RecordCache=*/false, &Observers);
  ASSERT_FALSE(AmbientMutationFailed.load(std::memory_order_acquire));
  ASSERT_TRUE(Result.Succeeded) << Result.Failure;
  ASSERT_FALSE(Result.Object.empty());
  expectObject(StringRef(Result.Object.data(), Result.Object.size()));

  const std::vector<unsigned> Thresholds = Collector.snapshot();
  ASSERT_FALSE(Thresholds.empty());
  for (unsigned Threshold : Thresholds)
    EXPECT_EQ(Threshold, 0u);

  const std::vector<unsigned> FinalCodeGenThresholds =
      FinalCodeGenCollector.snapshot();
  ASSERT_EQ(FinalCodeGenThresholds.size(), 1u);
  EXPECT_EQ(FinalCodeGenThresholds[0], 0u);
}

TEST(ParallelCodeGenCacheTest,
     ResolvedInheritedSCEVThresholdAffectsDirectPipeTag) {
  ASSERT_TRUE(createNativeTargetMachine());

  auto &RegisteredOptions = cl::getRegisteredOptions();
  auto ThresholdIt =
      RegisteredOptions.find("scalar-evolution-huge-expr-threshold");
  ASSERT_NE(ThresholdIt, RegisteredOptions.end());
  cl::Option *AmbientThreshold = ThresholdIt->second;
  std::function<void()> RestoreAmbientThreshold =
      AmbientThreshold->createStateRestorer();
  auto AmbientThresholdRestorer =
      make_scope_exit([&] { RestoreAmbientThreshold(); });

  const neverc::ParallelCodeGenTuning Tuning =
      overlapTuning(/*MaxPartitions=*/2, /*SCEVThreshold=*/0);
  neverc::ParallelOptimizationHooks FirstHooks = wholeModuleBarrier();
  neverc::ParallelOptimizationHooks SecondHooks = wholeModuleBarrier();

  AmbientThreshold->reset();
  ASSERT_FALSE(AmbientThreshold->addOccurrence(
      /*pos=*/1, AmbientThreshold->ArgStr, /*Value=*/"41"));
  DirectPCGRunResult First =
      runFreshDirectPCG("resolved-scev-cache-key", Tuning, &FirstHooks);

  AmbientThreshold->reset();
  ASSERT_FALSE(AmbientThreshold->addOccurrence(
      /*pos=*/1, AmbientThreshold->ArgStr, /*Value=*/"83"));
  DirectPCGRunResult Second =
      runFreshDirectPCG("resolved-scev-cache-key", Tuning, &SecondHooks);

  ASSERT_TRUE(First.Succeeded) << First.Failure;
  ASSERT_TRUE(Second.Succeeded) << Second.Failure;
  ASSERT_FALSE(First.LookupTags.empty());
  ASSERT_EQ(First.LookupTags.size(), Second.LookupTags.size());
  for (std::size_t Index = 0; Index != First.LookupTags.size(); ++Index) {
    SCOPED_TRACE(Index);
    EXPECT_NE(First.LookupTags[Index], Second.LookupTags[Index]);
  }
}

TEST(ParallelCodeGenCacheTest, EveryPipelineTuningFieldAffectsDirectPipeTag) {
  ASSERT_TRUE(createNativeTargetMachine());

  const neverc::ParallelCodeGenTuning Tuning =
      overlapTuning(/*MaxPartitions=*/2, /*SCEVThreshold=*/41);
  const llvm::NevercPipelineTuningOptions BaselinePipelineTuning;
  neverc::ParallelOptimizationHooks BaselineHooks = wholeModuleBarrier();
  DirectPCGRunResult Baseline = runFreshDirectPCG(
      "pipeline-policy-cache-key", Tuning, &BaselineHooks,
      /*RecordCache=*/true, /*Observers=*/nullptr, &BaselinePipelineTuning);
  ASSERT_TRUE(Baseline.Succeeded) << Baseline.Failure;
  ASSERT_FALSE(Baseline.LookupTags.empty());

  std::array<llvm::NevercPipelineTuningOptions, 5> Mutations;
  for (llvm::NevercPipelineTuningOptions &Mutation : Mutations)
    Mutation = BaselinePipelineTuning;
  Mutations[0].ModuleInlinerThreshold += 101;
  Mutations[1].AutoLTOInlineThreshold -= 17;
  Mutations[2].InlinerLiteFSimpl = !Mutations[2].InlinerLiteFSimpl;
  Mutations[3].InlineMaxCallerLoops += 103;
  Mutations[4].FullUnrollMaxLoopsPerFunction += 107;
  constexpr std::array<const char *, 5> Names = {
      "ModuleInlinerThreshold", "AutoLTOInlineThreshold", "InlinerLiteFSimpl",
      "InlineMaxCallerLoops", "FullUnrollMaxLoopsPerFunction"};

  for (std::size_t Index = 0; Index != Mutations.size(); ++Index) {
    SCOPED_TRACE(Names[Index]);
    neverc::ParallelOptimizationHooks Hooks = wholeModuleBarrier();
    DirectPCGRunResult Mutated = runFreshDirectPCG(
        "pipeline-policy-cache-key", Tuning, &Hooks,
        /*RecordCache=*/true, /*Observers=*/nullptr, &Mutations[Index]);
    ASSERT_TRUE(Mutated.Succeeded) << Mutated.Failure;
    ASSERT_EQ(Mutated.LookupTags.size(), Baseline.LookupTags.size());
    for (std::size_t TagIndex = 0; TagIndex != Mutated.LookupTags.size();
         ++TagIndex)
      EXPECT_NE(Mutated.LookupTags[TagIndex], Baseline.LookupTags[TagIndex]);
  }
}

TEST(ParallelCodeGenCacheTest,
     ExplicitPipelineTuningRemainsOnMotherContextAfterDecline) {
  std::unique_ptr<TargetMachine> Machine = createNativeTargetMachine();
  ASSERT_TRUE(Machine);
  LLVMContext Context;
  std::unique_ptr<Module> M =
      createLoopDenseModule(Context, *Machine, "pipeline-policy-decline");
  SmallVector<char, 0> Output;
  raw_svector_ostream OutputStream(Output);

  neverc::ParallelCodeGenTuning Tuning;
  llvm::NevercPipelineTuningOptions PipelineTuning;
  PipelineTuning.ModuleInlinerThreshold = 3131;
  PipelineTuning.AutoLTOInlineThreshold = -47;
  PipelineTuning.InlinerLiteFSimpl = false;
  PipelineTuning.InlineMaxCallerLoops = 19;
  PipelineTuning.FullUnrollMaxLoopsPerFunction = 23;

  EXPECT_FALSE(neverc::runParallelOptAndCodeGenWithTunings(
      *M, *Machine, neverc::ParallelCodeGenOutputs{OutputStream},
      /*OptLevel=*/0, Tuning, PipelineTuning));
  EXPECT_TRUE(Output.empty());
  EXPECT_EQ(pipelineTuningValues(Context.getNevercPipelineTuningOptions()),
            pipelineTuningValues(PipelineTuning));
}

TEST(ParallelFrontendTiming,
     AmbientPassTimingDeclinesConcurrentDirectRootsWithoutIRMutation) {
  auto &RegisteredOptions = cl::getRegisteredOptions();
  cl::Option *TimePassesOption = RegisteredOptions.lookup("time-passes");
  cl::Option *TimePassesPerRunOption =
      RegisteredOptions.lookup("time-passes-per-run");
  ASSERT_NE(TimePassesOption, nullptr);
  ASSERT_NE(TimePassesPerRunOption, nullptr);

  ScopedEnvironmentVariable PCGWorkers("NEVERC_PCG_THREADS", "2");
  const neverc::ParallelCodeGenTuning Tuning =
      overlapTuning(/*MaxPartitions=*/2, /*SCEVThreshold=*/41);

  for (bool PerRun : {false, true}) {
    SCOPED_TRACE(PerRun ? "time-passes-per-run" : "time-passes");

    std::unique_ptr<TargetMachine> CodeGenMachine = createNativeTargetMachine();
    std::unique_ptr<TargetMachine> OptCodeGenMachine =
        createNativeTargetMachine();
    ASSERT_TRUE(CodeGenMachine);
    ASSERT_TRUE(OptCodeGenMachine);

    LLVMContext CodeGenContext;
    LLVMContext OptCodeGenContext;
    std::atomic<unsigned> CodeGenErrors{0};
    std::atomic<unsigned> OptCodeGenErrors{0};
    auto CountErrors = [](const DiagnosticInfo &Diagnostic, void *Opaque) {
      if (Diagnostic.getSeverity() == DS_Error)
        static_cast<std::atomic<unsigned> *>(Opaque)->fetch_add(
            1, std::memory_order_relaxed);
    };
    CodeGenContext.setDiagnosticHandlerCallBack(CountErrors, &CodeGenErrors);
    OptCodeGenContext.setDiagnosticHandlerCallBack(CountErrors,
                                                   &OptCodeGenErrors);

    const char *ModeName = PerRun ? "per-run" : "aggregate";
    std::unique_ptr<Module> CodeGenModule = createLoopDenseModule(
        CodeGenContext, *CodeGenMachine,
        (Twine("ambient-direct-codegen-") + ModeName).str());
    std::unique_ptr<Module> OptCodeGenModule = createLoopDenseModule(
        OptCodeGenContext, *OptCodeGenMachine,
        (Twine("ambient-direct-opt-codegen-") + ModeName).str());
    const std::string CodeGenBefore = moduleIRText(*CodeGenModule);
    const std::string OptCodeGenBefore = moduleIRText(*OptCodeGenModule);

    SmallVector<char, 0> CodeGenOutput;
    SmallVector<char, 0> OptCodeGenOutput;
    raw_svector_ostream CodeGenStream(CodeGenOutput);
    raw_svector_ostream OptCodeGenStream(OptCodeGenOutput);
    RecordingCache CodeGenCache(CodeGenOutput);
    RecordingCache OptCodeGenCache(OptCodeGenOutput);
    neverc::PartitionCacheHooks CodeGenCacheHooks = CodeGenCache.hooks();
    neverc::PartitionCacheHooks OptCodeGenCacheHooks = OptCodeGenCache.hooks();

    std::atomic<unsigned> CodeGenObserverCalls{0};
    std::atomic<unsigned> OptCodeGenObserverCalls{0};
    neverc::ParallelCodeGenObservers CodeGenObservers;
    neverc::ParallelCodeGenObservers OptCodeGenObservers;
    countAllObservations(CodeGenObservers, CodeGenObserverCalls);
    countAllObservations(OptCodeGenObservers, OptCodeGenObserverCalls);

    std::atomic<unsigned> OptimizationHookCalls{0};
    auto NoteOptimizationHook = [&] {
      OptimizationHookCalls.fetch_add(1, std::memory_order_relaxed);
    };
    neverc::ParallelOptimizationHooks OptimizationHooks;
    OptimizationHooks.ConfigurePassBuilder = [&](PassBuilder &) {
      NoteOptimizationHook();
    };
    OptimizationHooks.PreOpt = [&](ModulePassManager &) {
      NoteOptimizationHook();
    };
    OptimizationHooks.PostOpt = [&](ModulePassManager &) {
      NoteOptimizationHook();
    };
    OptimizationHooks.WholeModulePostOpt = [&](ModulePassManager &) {
      NoteOptimizationHook();
    };

    bool SavedTimePasses = false;
    bool SavedTimePassesPerRun = false;
    int SavedTimePassesOccurrences = 0;
    int SavedTimePassesPerRunOccurrences = 0;
    std::function<void()> RestoreTimePassesState;
    std::function<void()> RestoreTimePassesPerRunState;
    {
      neverc::plugin::PluginLLVMOptionExclusiveLease Lease(
          neverc::plugin::pluginLLVMOptionGate());
      SavedTimePasses = llvm::TimePassesIsEnabled;
      SavedTimePassesPerRun = llvm::TimePassesPerRun;
      SavedTimePassesOccurrences = TimePassesOption->getNumOccurrences();
      SavedTimePassesPerRunOccurrences =
          TimePassesPerRunOption->getNumOccurrences();
      RestoreTimePassesState = TimePassesOption->createStateRestorer();
      RestoreTimePassesPerRunState =
          TimePassesPerRunOption->createStateRestorer();
      llvm::TimePassesIsEnabled = true;
      llvm::TimePassesPerRun = PerRun;
    }

    bool TimingStateRestored = false;
    auto RestoreTimingState = [&] {
      if (TimingStateRestored)
        return;
      neverc::plugin::PluginLLVMOptionExclusiveLease Lease(
          neverc::plugin::pluginLLVMOptionGate());
      RestoreTimePassesPerRunState();
      RestoreTimePassesState();
      llvm::TimePassesIsEnabled = SavedTimePasses;
      llvm::TimePassesPerRun = SavedTimePassesPerRun;
      TimingStateRestored = true;
    };
    auto TimingStateGuard = make_scope_exit(RestoreTimingState);

    std::mutex StartMutex;
    std::condition_variable StartCondition;
    unsigned Ready = 0;
    bool Start = false;
    bool CodeGenResult = true;
    bool OptCodeGenResult = true;
    auto WaitForStart = [&] {
      std::unique_lock<std::mutex> Lock(StartMutex);
      ++Ready;
      StartCondition.notify_all();
      StartCondition.wait(Lock, [&] { return Start; });
    };

    std::thread CodeGenThread([&] {
      WaitForStart();
      CodeGenResult = neverc::runParallelCodeGenWithTuning(
          *CodeGenModule, *CodeGenMachine,
          neverc::ParallelCodeGenOutputs{CodeGenStream}, Tuning,
          &CodeGenCacheHooks, &CodeGenObservers);
    });
    std::thread OptCodeGenThread([&] {
      WaitForStart();
      OptCodeGenResult = neverc::runParallelOptAndCodeGenWithTuning(
          *OptCodeGenModule, *OptCodeGenMachine,
          neverc::ParallelCodeGenOutputs{OptCodeGenStream}, /*OptLevel=*/2,
          Tuning, &OptCodeGenCacheHooks, &OptimizationHooks,
          &OptCodeGenObservers);
    });
    auto JoinThreads = make_scope_exit([&] {
      {
        std::lock_guard<std::mutex> Lock(StartMutex);
        Start = true;
      }
      StartCondition.notify_all();
      if (CodeGenThread.joinable())
        CodeGenThread.join();
      if (OptCodeGenThread.joinable())
        OptCodeGenThread.join();
    });
    {
      std::unique_lock<std::mutex> Lock(StartMutex);
      ASSERT_TRUE(StartCondition.wait_for(Lock, std::chrono::seconds(10),
                                          [&] { return Ready == 2; }));
      Start = true;
    }
    StartCondition.notify_all();
    CodeGenThread.join();
    OptCodeGenThread.join();
    JoinThreads.release();

    RestoreTimingState();
    {
      neverc::plugin::PluginLLVMOptionExclusiveLease Lease(
          neverc::plugin::pluginLLVMOptionGate());
      EXPECT_EQ(llvm::TimePassesIsEnabled, SavedTimePasses);
      EXPECT_EQ(llvm::TimePassesPerRun, SavedTimePassesPerRun);
      EXPECT_EQ(TimePassesOption->getNumOccurrences(),
                SavedTimePassesOccurrences);
      EXPECT_EQ(TimePassesPerRunOption->getNumOccurrences(),
                SavedTimePassesPerRunOccurrences);
    }

    // The public wrappers may acquire a request-local resource session and
    // install request tuning on their private LLVMContexts before init. The
    // fail-closed boundary is no IR/artifact/cache/observer work.
    EXPECT_FALSE(CodeGenResult);
    EXPECT_FALSE(OptCodeGenResult);
    EXPECT_TRUE(CodeGenOutput.empty());
    EXPECT_TRUE(OptCodeGenOutput.empty());
    EXPECT_TRUE(CodeGenCache.lookupTags().empty());
    EXPECT_TRUE(CodeGenCache.stores().empty());
    EXPECT_TRUE(OptCodeGenCache.lookupTags().empty());
    EXPECT_TRUE(OptCodeGenCache.stores().empty());
    EXPECT_EQ(CodeGenObserverCalls.load(std::memory_order_relaxed), 0u);
    EXPECT_EQ(OptCodeGenObserverCalls.load(std::memory_order_relaxed), 0u);
    EXPECT_EQ(OptimizationHookCalls.load(std::memory_order_relaxed), 0u);
    EXPECT_EQ(CodeGenErrors.load(std::memory_order_relaxed), 0u);
    EXPECT_EQ(OptCodeGenErrors.load(std::memory_order_relaxed), 0u);
    EXPECT_EQ(moduleIRText(*CodeGenModule), CodeGenBefore);
    EXPECT_EQ(moduleIRText(*OptCodeGenModule), OptCodeGenBefore);
  }
}
