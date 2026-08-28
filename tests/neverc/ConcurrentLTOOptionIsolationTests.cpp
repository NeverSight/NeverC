#include "Backend/ParallelCodeGenMergeInternal.h"
#include "Linker/Core/Driver/CommonLTOConfig.h"
#include "Linker/Core/Driver/Dispatcher.h"
#include "Linker/Core/Driver/LTOCache.h"
#include "Linker/Core/Runtime/Session.h"
#include "neverc/Plugin/Host/IRGenProvider.h"
#include "neverc/Plugin/Host/PluginLLVMOptionSnapshot.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"

#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/CodeGen/CommandFlags.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/LTO/LTO.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Support/NevercPipelineTuning.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Host.h"

#include "gtest/gtest.h"

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

using namespace llvm;

namespace {

struct ParallelCodeGenOptionOracle {
  const char *Spelling;
  std::uint32_t neverc::ParallelCodeGenTuning::*Member;
  std::uint32_t DefaultValue;
};

// This is deliberately a literal, test-owned compatibility oracle. Keep it
// independent of ParallelCodeGenTuning.def so an accidental spelling/default
// change in the production table cannot update the test expectation with it.
constexpr std::array<ParallelCodeGenOptionOracle, 14>
    ParallelCodeGenOptionOracles = {{
        {"neverc-pcg-min-funcs",
         &neverc::ParallelCodeGenTuning::MinDefinedFunctions, 8},
        {"neverc-pcg-weight-floor",
         &neverc::ParallelCodeGenTuning::MinInstructionWeight, 10000},
        {"neverc-pcg-loop-floor", &neverc::ParallelCodeGenTuning::MinLoopCount,
         56},
        {"neverc-pcg-opt-weight-div",
         &neverc::ParallelCodeGenTuning::OptInstructionsPerPartition, 12000},
        {"neverc-pcg-opt-loop-div",
         &neverc::ParallelCodeGenTuning::OptLoopsPerPartition, 16},
        {"neverc-pcg-opt-max-parts",
         &neverc::ParallelCodeGenTuning::OptMaxPartitions, 16},
        {"neverc-pcg-cg-weight-div",
         &neverc::ParallelCodeGenTuning::CodeGenInstructionsPerPartition, 5000},
        {"neverc-pcg-cg-loop-div",
         &neverc::ParallelCodeGenTuning::CodeGenLoopsPerPartition, 0},
        {"neverc-pcg-cg-max-parts",
         &neverc::ParallelCodeGenTuning::CodeGenMaxPartitions, 16},
        {"neverc-auto-lto-indvars-widen-max-function-loops",
         &neverc::ParallelCodeGenTuning::IndVarWidenMaxFunctionLoops, 0},
        {"neverc-auto-lto-scev-huge-expr-threshold",
         &neverc::ParallelCodeGenTuning::SCEVHugeExprThreshold, 64},
        {"neverc-pcg-loops-per-thread",
         &neverc::ParallelCodeGenTuning::LoopsPerWorker, 96},
        {"neverc-pcg-weight-per-thread",
         &neverc::ParallelCodeGenTuning::InstructionsPerWorker, 10000},
        {"neverc-pcg-work-thread-floor",
         &neverc::ParallelCodeGenTuning::MinWorkerThreads, 6},
    }};

struct NevercPipelineOptionOracle {
  const char *Spelling;
  std::int64_t DefaultValue;
  std::function<std::int64_t(const llvm::NevercPipelineTuningOptions &)> Get;
  std::function<void(llvm::NevercPipelineTuningOptions &, std::int64_t)> Set;
};

// Deliberately independent of NevercPipelineTuning.def. A production table
// edit must not silently rewrite this compatibility oracle.
const std::array<NevercPipelineOptionOracle, 5> NevercPipelineOptionOracles = {{
    {"neverc-module-inliner-threshold", 6000,
     [](const llvm::NevercPipelineTuningOptions &Tuning) {
       return Tuning.ModuleInlinerThreshold;
     },
     [](llvm::NevercPipelineTuningOptions &Tuning, std::int64_t Value) {
       Tuning.ModuleInlinerThreshold = static_cast<unsigned>(Value);
     }},
    {"neverc-auto-lto-inline-threshold", 150,
     [](const llvm::NevercPipelineTuningOptions &Tuning) {
       return Tuning.AutoLTOInlineThreshold;
     },
     [](llvm::NevercPipelineTuningOptions &Tuning, std::int64_t Value) {
       Tuning.AutoLTOInlineThreshold = static_cast<int>(Value);
     }},
    {"neverc-inliner-lite-fsimpl", 1,
     [](const llvm::NevercPipelineTuningOptions &Tuning) {
       return Tuning.InlinerLiteFSimpl;
     },
     [](llvm::NevercPipelineTuningOptions &Tuning, std::int64_t Value) {
       Tuning.InlinerLiteFSimpl = Value != 0;
     }},
    {"neverc-inline-max-caller-loops", 32,
     [](const llvm::NevercPipelineTuningOptions &Tuning) {
       return Tuning.InlineMaxCallerLoops;
     },
     [](llvm::NevercPipelineTuningOptions &Tuning, std::int64_t Value) {
       Tuning.InlineMaxCallerLoops = static_cast<unsigned>(Value);
     }},
    {"neverc-full-unroll-max-loops-per-function", 100,
     [](const llvm::NevercPipelineTuningOptions &Tuning) {
       return Tuning.FullUnrollMaxLoopsPerFunction;
     },
     [](llvm::NevercPipelineTuningOptions &Tuning, std::int64_t Value) {
       Tuning.FullUnrollMaxLoopsPerFunction = static_cast<unsigned>(Value);
     }},
}};

void parseLLVMOptions(ArrayRef<std::string> Options) {
  SmallVector<const char *, 32> Argv;
  Argv.reserve(Options.size() + 1);
  Argv.push_back("neverc-pcg-tuning-test");
  for (const std::string &Option : Options)
    Argv.push_back(Option.c_str());
  cl::ResetAllOptionOccurrences();
  cl::ParseCommandLineOptions(static_cast<int>(Argv.size()), Argv.data());
}

struct LTOCacheIdentity {
  std::string Full;
  std::string Partition;

  friend bool operator==(const LTOCacheIdentity &LHS,
                         const LTOCacheIdentity &RHS) {
    return LHS.Full == RHS.Full && LHS.Partition == RHS.Partition;
  }
};

LTOCacheIdentity cacheIdentity(const linker::LinkerDriverConfig &Config) {
  // A key builder is stateful. Always start from a fresh instance so these
  // comparisons exercise appendConfig rather than accumulated test history.
  linker::LTOCacheKey FullKey;
  return {FullKey.finalize(Config, /*MaxTasks=*/4, "test-object-format",
                           /*EmitAddrsig=*/true),
          linker::ltoPartitionCacheSalt(Config, /*EmitAddrsig=*/true)};
}

using LegacyParallelCodeGenFunction =
    bool (*)(Module &, TargetMachine &, neverc::ParallelCodeGenOutputs,
             unsigned, const neverc::PartitionCacheHooks *);
using LegacyParallelOptAndCodeGenFunction =
    bool (*)(Module &, TargetMachine &, neverc::ParallelCodeGenOutputs,
             unsigned, unsigned, const neverc::PartitionCacheHooks *,
             const neverc::ParallelOptimizationHooks *);
using LegacyParseMllvmOptionsFunction =
    void (*)(const linker::LinkerDriverConfig &);
using SingularParallelCodeGenFunction = bool (*)(
    Module &, TargetMachine &, neverc::ParallelCodeGenOutputs,
    const neverc::ParallelCodeGenTuning &, const neverc::PartitionCacheHooks *,
    const neverc::ParallelCodeGenObservers *);
using SingularParallelOptAndCodeGenFunction = bool (*)(
    Module &, TargetMachine &, neverc::ParallelCodeGenOutputs, unsigned,
    const neverc::ParallelCodeGenTuning &, const neverc::PartitionCacheHooks *,
    const neverc::ParallelOptimizationHooks *,
    const neverc::ParallelCodeGenObservers *);
using PluralParallelCodeGenFunction =
    bool (*)(Module &, TargetMachine &, neverc::ParallelCodeGenOutputs,
             const neverc::ParallelCodeGenTuning &,
             const llvm::NevercPipelineTuningOptions &,
             const neverc::PartitionCacheHooks *, std::optional<unsigned>,
             const neverc::ParallelCodeGenObservers *);
using PluralParallelOptAndCodeGenFunction =
    bool (*)(Module &, TargetMachine &, neverc::ParallelCodeGenOutputs,
             unsigned, const neverc::ParallelCodeGenTuning &,
             const llvm::NevercPipelineTuningOptions &,
             const neverc::PartitionCacheHooks *,
             const neverc::ParallelOptimizationHooks *,
             const neverc::ParallelCodeGenObservers *, std::optional<unsigned>);

// The legacy entry point accepts this concrete hooks object through a pointer,
// so preserving only the function's mangled name is not sufficient: an old
// binary allocated exactly these four std::function members.  Pin the complete
// object layout independently of the production declaration so a new callback
// cannot silently make the legacy wrapper read beyond the caller's allocation.
struct LegacyParallelOptimizationHooksLayout {
  std::function<void(PassBuilder &)> ConfigurePassBuilder;
  std::function<void(ModulePassManager &)> PreOpt;
  std::function<void(ModulePassManager &)> PostOpt;
  std::function<void(ModulePassManager &)> WholeModulePostOpt;
};

static_assert(sizeof(neverc::ParallelOptimizationHooks) ==
              sizeof(LegacyParallelOptimizationHooksLayout));
static_assert(alignof(neverc::ParallelOptimizationHooks) ==
              alignof(LegacyParallelOptimizationHooksLayout));

static_assert(
    std::is_same_v<decltype(static_cast<LegacyParallelCodeGenFunction>(
                       &neverc::runParallelCodeGen)),
                   LegacyParallelCodeGenFunction>);
static_assert(
    std::is_same_v<decltype(static_cast<LegacyParallelOptAndCodeGenFunction>(
                       &neverc::runParallelOptAndCodeGen)),
                   LegacyParallelOptAndCodeGenFunction>);
static_assert(
    std::is_same_v<decltype(static_cast<LegacyParseMllvmOptionsFunction>(
                       &linker::parseMllvmOptions)),
                   LegacyParseMllvmOptionsFunction>);
static_assert(
    std::is_same_v<decltype(static_cast<SingularParallelCodeGenFunction>(
                       &neverc::runParallelCodeGenWithTuning)),
                   SingularParallelCodeGenFunction>);
static_assert(
    std::is_same_v<decltype(static_cast<SingularParallelOptAndCodeGenFunction>(
                       &neverc::runParallelOptAndCodeGenWithTuning)),
                   SingularParallelOptAndCodeGenFunction>);
static_assert(
    std::is_same_v<decltype(static_cast<PluralParallelCodeGenFunction>(
                       &neverc::runParallelCodeGenWithTunings)),
                   PluralParallelCodeGenFunction>);
static_assert(
    std::is_same_v<decltype(static_cast<PluralParallelOptAndCodeGenFunction>(
                       &neverc::runParallelOptAndCodeGenWithTunings)),
                   PluralParallelOptAndCodeGenFunction>);

// Compile real calls (not only decltype expressions) so both source
// compatibility and the old mangled link symbols remain pinned. These helpers
// are intentionally not executed: their only job is to compile and link calls
// whose legacy unsigned arguments are written as braced initializers.
bool callLegacyParallelCodeGenWithBracedPartitionCount(
    Module &Mod, TargetMachine &TM, neverc::ParallelCodeGenOutputs Outputs) {
  return neverc::runParallelCodeGen(Mod, TM, Outputs, {});
}

bool callLegacyParallelOptAndCodeGenWithBracedArguments(
    Module &Mod, TargetMachine &TM, neverc::ParallelCodeGenOutputs Outputs) {
  return neverc::runParallelOptAndCodeGen(Mod, TM, Outputs, {}, {});
}

cl::opt<unsigned> LTOProfileMarker(
    "neverc-test-lto-option-profile", cl::Hidden, cl::init(17),
    cl::desc("Test-only marker for an in-process LTO option profile"));

class RunPermit {
public:
  void release() {
    {
      std::lock_guard<std::mutex> Lock(Mutex);
      Released = true;
    }
    Condition.notify_all();
  }

  void wait() {
    std::unique_lock<std::mutex> Lock(Mutex);
    Condition.wait(Lock, [&] { return Released; });
  }

private:
  std::mutex Mutex;
  std::condition_variable Condition;
  bool Released = false;
};

std::unique_ptr<TargetMachine> createNativeTargetMachine() {
  static const bool Initialized = [] {
    static llvm::codegen::RegisterCodeGenFlags CodeGenFlags;
    (void)CodeGenFlags;
    return !InitializeNativeTarget() && !InitializeNativeTargetAsmPrinter();
  }();
  if (!Initialized)
    return nullptr;

  const std::string TripleName = sys::getDefaultTargetTriple();
  std::string LookupError;
  const Target *TheTarget =
      TargetRegistry::lookupTarget(TripleName, LookupError);
  if (!TheTarget)
    return nullptr;

  TargetOptions Options;
  return std::unique_ptr<TargetMachine>(
      TheTarget->createTargetMachine(TripleName, "generic", "", Options,
                                     std::nullopt, CodeGenOptLevel::Default));
}

SmallString<0> createLTOBitcode(TargetMachine &Machine) {
  LLVMContext Context;
  Module M("concurrent-lto-option-isolation", Context);
  M.setTargetTriple(Machine.getTargetTriple().str());
  M.setDataLayout(Machine.createDataLayout());

  Type *I64 = Type::getInt64Ty(Context);
  FunctionType *FunctionTy = FunctionType::get(I64, {I64}, false);
  for (unsigned Index = 0; Index != 8; ++Index) {
    Function *F = Function::Create(
        FunctionTy, GlobalValue::ExternalLinkage,
        (Twine("concurrent_lto_function_") + Twine(Index)).str(), M);
    F->addFnAttr(Attribute::NoInline);
    BasicBlock *Entry = BasicBlock::Create(Context, "entry", F);
    IRBuilder<> Builder(Entry);
    Value *Result = Builder.CreateAdd(
        F->getArg(0), ConstantInt::get(I64, Index + 1), "result");
    Builder.CreateRet(Result);
  }

  SmallString<0> Bitcode;
  raw_svector_ostream Stream(Bitcode);
  WriteBitcodeToFile(M, Stream);
  return Bitcode;
}

SmallString<0> serializeLTOFixture(Module &M) {
  SmallString<0> Bitcode;
  raw_svector_ostream Stream(Bitcode);
  WriteBitcodeToFile(M, Stream);
  return Bitcode;
}

BasicBlock *appendConstantTripLoop(Function &F, BasicBlock *Preheader,
                                   Function &SideEffect, unsigned Index) {
  LLVMContext &Context = F.getContext();
  Type *I32 = Type::getInt32Ty(Context);
  const std::string Prefix = (Twine("loop.") + Twine(Index)).str();
  BasicBlock *Header = BasicBlock::Create(Context, Prefix + ".header", &F);
  BasicBlock *Exit = BasicBlock::Create(Context, Prefix + ".exit", &F);

  IRBuilder<> Builder(Preheader);
  Builder.CreateBr(Header);
  Builder.SetInsertPoint(Header);
  PHINode *Iteration = Builder.CreatePHI(I32, 2, Prefix + ".iv");
  Iteration->addIncoming(ConstantInt::get(I32, 0), Preheader);
  Builder.CreateCall(&SideEffect, {Iteration});
  Value *Next =
      Builder.CreateAdd(Iteration, ConstantInt::get(I32, 1), Prefix + ".next");
  Builder.CreateCondBr(Builder.CreateICmpULT(Next, ConstantInt::get(I32, 4)),
                       Header, Exit);
  Iteration->addIncoming(Next, Header);
  return Exit;
}

SmallString<0> createSerialIPOLoopBitcode(TargetMachine &Machine,
                                          unsigned LoopCount) {
  LLVMContext Context;
  Module M("neverc-serial-ipo-loop-policy", Context);
  M.setTargetTriple(Machine.getTargetTriple().str());
  M.setDataLayout(Machine.createDataLayout());

  Type *Void = Type::getVoidTy(Context);
  Type *I32 = Type::getInt32Ty(Context);
  FunctionType *SideEffectTy = FunctionType::get(Void, {I32}, false);
  Function *SideEffect =
      Function::Create(SideEffectTy, GlobalValue::ExternalLinkage,
                       "neverc_serial_ipo_side_effect", M);
  Function *Probe = Function::Create(FunctionType::get(Void, false),
                                     GlobalValue::ExternalLinkage,
                                     "neverc_serial_ipo_loop_probe", M);
  Probe->setDSOLocal(true);

  BasicBlock *Next = BasicBlock::Create(Context, "entry", Probe);
  for (unsigned Index = 0; Index != LoopCount; ++Index)
    Next = appendConstantTripLoop(*Probe, Next, *SideEffect, Index);
  IRBuilder<>(Next).CreateRetVoid();
  return serializeLTOFixture(M);
}

SmallString<0> createSerialIPOInlineBitcode(TargetMachine &Machine) {
  LLVMContext Context;
  Module M("neverc-serial-ipo-inline-policy", Context);
  M.setTargetTriple(Machine.getTargetTriple().str());
  M.setDataLayout(Machine.createDataLayout());

  Type *I64 = Type::getInt64Ty(Context);
  Type *Ptr = PointerType::getUnqual(Context);
  FunctionType *LeafTy = FunctionType::get(I64, {Ptr}, false);
  Function *Leaf = Function::Create(LeafTy, GlobalValue::ExternalLinkage,
                                    "neverc_serial_ipo_inline_leaf", M);
  Leaf->setDSOLocal(true);
  BasicBlock *LeafEntry = BasicBlock::Create(Context, "entry", Leaf);
  IRBuilder<> LeafBuilder(LeafEntry);
  Value *Result = ConstantInt::get(I64, 0);
  for (unsigned Index = 0; Index != 48; ++Index) {
    Value *Address = LeafBuilder.CreateInBoundsGEP(
        I64, Leaf->getArg(0), ConstantInt::get(I64, Index));
    Value *Loaded = LeafBuilder.CreateLoad(I64, Address);
    Value *Weighted =
        LeafBuilder.CreateMul(Loaded, ConstantInt::get(I64, 2 * Index + 3));
    Result = LeafBuilder.CreateAdd(Result, Weighted);
  }
  LeafBuilder.CreateRet(Result);

  Function *Root = Function::Create(LeafTy, GlobalValue::ExternalLinkage,
                                    "neverc_serial_ipo_inline_root", M);
  Root->setDSOLocal(true);
  BasicBlock *RootEntry = BasicBlock::Create(Context, "entry", Root);
  IRBuilder<> RootBuilder(RootEntry);
  RootBuilder.CreateRet(RootBuilder.CreateCall(Leaf, {Root->getArg(0)}));
  return serializeLTOFixture(M);
}

unsigned countFunctionBackedges(const Module &M, StringRef FunctionName) {
  const Function *F = M.getFunction(FunctionName);
  if (!F)
    return std::numeric_limits<unsigned>::max();
  SmallVector<std::pair<const BasicBlock *, const BasicBlock *>, 8> Backedges;
  FindFunctionBackedges(*F, Backedges);
  return Backedges.size();
}

unsigned countDirectCalls(const Module &M, StringRef CallerName,
                          StringRef CalleeName) {
  const Function *Caller = M.getFunction(CallerName);
  if (!Caller)
    return std::numeric_limits<unsigned>::max();
  unsigned Count = 0;
  for (const Instruction &I : instructions(Caller))
    if (const auto *Call = dyn_cast<CallBase>(&I))
      if (const Function *Callee = Call->getCalledFunction())
        Count += Callee->getName() == CalleeName;
  return Count;
}

linker::LinkerDriverConfig createDriverConfig(unsigned Marker,
                                              unsigned ScevThreshold) {
  linker::LinkerDriverConfig Config;
  Config.threadCount = 2;
  Config.ltoPartitions = 2;
  Config.ltoOptLevel = 2;
  Config.nevercPluginPaths.push_back("disable-lto-cache-for-test");
  Config.mllvmOpts = {
      (Twine("-neverc-test-lto-option-profile=") + Twine(Marker)).str(),
      "-neverc-pcg-min-funcs=1",
      "-neverc-pcg-weight-floor=0",
      "-neverc-pcg-opt-weight-div=1",
      "-neverc-pcg-opt-max-parts=2",
      (Twine("-neverc-auto-lto-scev-huge-expr-threshold=") +
       Twine(ScevThreshold))
          .str(),
  };
  return Config;
}

linker::LinkerDriverConfig
createSerialIPODriverConfig(const NevercPipelineTuningOptions &Tuning) {
  linker::LinkerDriverConfig Config;
  Config.threadCount = 2;
  Config.ltoPartitions = 2;
  Config.ltoOptLevel = 2;
  Config.ltoPipelineTuning = Tuning;
  return Config;
}

class PreparedLTO {
public:
  using ConfigureFn = std::function<void(lto::Config &)>;

  static Expected<std::unique_ptr<PreparedLTO>>
  create(const linker::LinkerDriverConfig &DriverConfig,
         MemoryBufferRef Bitcode, ConfigureFn Configure = {}) {
    lto::Config Config = linker::createLTOConfig(
        DriverConfig, [](const DiagnosticInfo &) {}, /*EmitAddrsig=*/true);
    if (Configure)
      Configure(Config);
    auto Engine = std::make_unique<lto::LTO>(std::move(Config),
                                             DriverConfig.ltoPartitions);

    Expected<std::unique_ptr<lto::InputFile>> Input =
        lto::InputFile::create(Bitcode);
    if (!Input)
      return Input.takeError();

    std::vector<lto::SymbolResolution> Resolutions;
    Resolutions.reserve((*Input)->symbols().size());
    for (const lto::InputFile::Symbol &Symbol : (*Input)->symbols()) {
      lto::SymbolResolution Resolution;
      Resolution.VisibleToRegularObj = true;
      Resolution.Prevailing = !Symbol.isUndefined();
      Resolutions.push_back(Resolution);
    }
    if (Error E = Engine->add(std::move(*Input), Resolutions))
      return std::move(E);

    return std::unique_ptr<PreparedLTO>(new PreparedLTO(std::move(Engine)));
  }

  Error run() {
    linker::CommonLinkerContext Context;
    linker::LinkerContextGuard ContextGuard(Context);
    Context.configureParallel(/*RequestedThreads=*/2,
                              /*DefaultThreadLimit=*/2);
    Context.e.initialize(outs(), errs(), /*exitEarly=*/false,
                         /*disableOutput=*/false);
    Context.e.errorLimit = 20;
    Context.e.logName = "neverc-lto-option-isolation-test";

    Outputs.resize(Engine->getMaxTasks());
    return Engine->run(
        [this](unsigned Task,
               const Twine &) -> Expected<std::unique_ptr<CachedFileStream>> {
          if (Task >= Outputs.size())
            return createStringError(inconvertibleErrorCode(),
                                     "LTO task index is out of range");
          return std::make_unique<CachedFileStream>(
              std::make_unique<raw_svector_ostream>(Outputs[Task]));
        });
  }

  Error validateObjects() const {
    bool SawObject = false;
    const Triple HostTriple(sys::getDefaultTargetTriple());
    for (const SmallString<0> &Output : Outputs) {
      if (Output.empty())
        continue;
      SawObject = true;
      auto Object = object::ObjectFile::createObjectFile(MemoryBufferRef(
          StringRef(Output.data(), Output.size()), "concurrent-lto-output"));
      if (!Object)
        return Object.takeError();
      if (!(*Object)->isRelocatableObject())
        return createStringError(inconvertibleErrorCode(),
                                 "LTO output is not relocatable");
      if ((*Object)->getArch() != HostTriple.getArch())
        return createStringError(inconvertibleErrorCode(),
                                 "LTO output has the wrong architecture");
      const bool FormatMatches =
          (HostTriple.isOSBinFormatELF() && (*Object)->isELF()) ||
          (HostTriple.isOSBinFormatCOFF() && (*Object)->isCOFF()) ||
          (HostTriple.isOSBinFormatMachO() && (*Object)->isMachO());
      if (!FormatMatches)
        return createStringError(inconvertibleErrorCode(),
                                 "LTO output has the wrong object format");
    }
    if (!SawObject)
      return createStringError(inconvertibleErrorCode(),
                               "LTO emitted no native object");
    return Error::success();
  }

  bool machineHooksAliasHostContext() const {
    const auto &Hooks = Engine->Conf.MachinePassHooks;
    const auto &Host = Engine->Conf.HostContext;
    return Hooks && Host && !Hooks.owner_before(Host) &&
           !Host.owner_before(Hooks);
  }

private:
  explicit PreparedLTO(std::unique_ptr<lto::LTO> EngineValue)
      : Engine(std::move(EngineValue)) {}

  std::unique_ptr<lto::LTO> Engine;
  std::vector<SmallString<0>> Outputs;
};

std::string errorText(Error E) {
  if (!E)
    return {};
  return toString(std::move(E)).str().str();
}

Expected<unsigned>
observeSerialIPO(const linker::LinkerDriverConfig &DriverConfig,
                 MemoryBufferRef Bitcode,
                 const std::function<unsigned(const Module &)> &Metric) {
  bool Observed = false;
  unsigned Value = 0;
  Expected<std::unique_ptr<PreparedLTO>> PreparedOrError =
      PreparedLTO::create(DriverConfig, Bitcode, [&](lto::Config &Config) {
        Config.PostOptModuleHook = [&](unsigned, const Module &M) {
          Observed = true;
          Value = Metric(M);
          // This hook is the serial-IPO observation boundary. Stopping here
          // keeps the oracle independent of PCG partitioning and codegen.
          return false;
        };
      });
  if (!PreparedOrError)
    return PreparedOrError.takeError();

  std::unique_ptr<PreparedLTO> Prepared = std::move(*PreparedOrError);
  if (Error E = Prepared->run())
    return std::move(E);
  if (!Observed)
    return createStringError(inconvertibleErrorCode(),
                             "serial IPO observation hook was not called");
  return Value;
}

struct DeferredSerialObservation {
  bool PostOptObserved = false;
  bool ParallelOptCodeGenCalled = false;
  bool ParallelOptCodeGenAccepted = false;
  bool ParallelCodeGenCalled = false;
  bool ParallelCodeGenAccepted = false;
  bool PreCodeGenObserved = false;
  unsigned PostOptBackedges = 0;
  unsigned PreCodeGenBackedges = 0;
};

Expected<DeferredSerialObservation> observeDeferredSerialOptimization(
    const linker::LinkerDriverConfig &DriverConfig, MemoryBufferRef Bitcode) {
  DeferredSerialObservation Observation;
  Expected<std::unique_ptr<PreparedLTO>> PreparedOrError =
      PreparedLTO::create(DriverConfig, Bitcode, [&](lto::Config &Config) {
        auto OriginalPostOpt = std::move(Config.PostOptModuleHook);
        Config.PostOptModuleHook =
            [&, OriginalPostOpt = std::move(OriginalPostOpt)](unsigned Task,
                                                              const Module &M) {
              if (OriginalPostOpt && !OriginalPostOpt(Task, M))
                return false;
              Observation.PostOptObserved = true;
              Observation.PostOptBackedges =
                  countFunctionBackedges(M, "neverc_serial_ipo_loop_probe");
              return true;
            };

        auto OriginalParallelOptCodeGen =
            std::move(Config.ParallelOptCodeGenHook);
        Config.ParallelOptCodeGenHook =
            [&, Original = std::move(OriginalParallelOptCodeGen)](
                Module &M, TargetMachine &TM, raw_pwrite_stream &OS,
                unsigned Partitions, unsigned OptLevel) {
              Observation.ParallelOptCodeGenCalled = true;
              Observation.ParallelOptCodeGenAccepted =
                  Original && Original(M, TM, OS, Partitions, OptLevel);
              return Observation.ParallelOptCodeGenAccepted;
            };

        auto OriginalParallelCodeGen = std::move(Config.ParallelCodeGenHook);
        Config.ParallelCodeGenHook =
            [&, Original = std::move(OriginalParallelCodeGen)](
                Module &M, TargetMachine &TM, raw_pwrite_stream &OS,
                unsigned Partitions) {
              Observation.ParallelCodeGenCalled = true;
              Observation.ParallelCodeGenAccepted =
                  Original && Original(M, TM, OS, Partitions);
              return Observation.ParallelCodeGenAccepted;
            };

        auto OriginalPreCodeGen = std::move(Config.PreCodeGenModuleHook);
        Config.PreCodeGenModuleHook = [&, OriginalPreCodeGen =
                                              std::move(OriginalPreCodeGen)](
                                          unsigned Task, const Module &M) {
          if (OriginalPreCodeGen && !OriginalPreCodeGen(Task, M))
            return false;
          Observation.PreCodeGenObserved = true;
          Observation.PreCodeGenBackedges =
              countFunctionBackedges(M, "neverc_serial_ipo_loop_probe");
          // The deferred pipeline has completed. Avoid native codegen;
          // this test owns only the optimizer observation boundary.
          return false;
        };
      });
  if (!PreparedOrError)
    return PreparedOrError.takeError();

  std::unique_ptr<PreparedLTO> Prepared = std::move(*PreparedOrError);
  if (Error E = Prepared->run())
    return std::move(E);
  return Observation;
}

class MIRPluginSessionScope {
public:
  MIRPluginSessionScope()
      : Services("neverc-lto-option-isolation-tests", LLVM_VERSION_MAJOR) {}

  ~MIRPluginSessionScope() { consumeError(finish()); }

  Error initialize() {
    if (Error E = neverc::plugin::registerPluginIRInterface(Services))
      return E;
    if (Error E = Services.interfaces().freeze())
      return E;
    auto Loaded = Services.registry().load(NEVERC_TEST_MIR_PASS_PLUGIN);
    if (!Loaded)
      return Loaded.takeError();
    const std::array<StringRef, 1> Selected = {
        (*Loaded)->descriptor().PluginID};
    auto CreatedPlan =
        neverc::plugin::makePluginActivationPlan(Services.registry(), Selected);
    if (!CreatedPlan)
      return CreatedPlan.takeError();
    Plan.emplace(std::move(*CreatedPlan));
    auto CreatedSession =
        neverc::plugin::PluginSession::create(Services, *Plan);
    if (!CreatedSession)
      return CreatedSession.takeError();
    Session = std::shared_ptr<neverc::plugin::PluginSession>(
        std::move(*CreatedSession));
    return Error::success();
  }

  std::shared_ptr<neverc::plugin::PluginSession> session() const {
    return Session;
  }

  Error finish() {
    if (Finished)
      return Error::success();
    Error Result = Error::success();
    if (Session)
      Result = joinErrors(std::move(Result), Session->end());
    Session.reset();
    Plan.reset();
    Result = joinErrors(std::move(Result), Services.shutdown());
    Finished = true;
    return Result;
  }

private:
  neverc::plugin::PluginProcessServices Services;
  std::optional<neverc::plugin::PluginActivationPlan> Plan;
  std::shared_ptr<neverc::plugin::PluginSession> Session;
  bool Finished = false;
};

bool waitForExclusiveOptionWait(std::uint64_t PreviousEpoch,
                                std::chrono::milliseconds Timeout) {
  const auto Deadline = std::chrono::steady_clock::now() + Timeout;
  while (neverc::plugin::pluginLLVMOptionExclusiveWaitEpoch() ==
         PreviousEpoch) {
    if (std::chrono::steady_clock::now() >= Deadline)
      return false;
    std::this_thread::yield();
  }
  return true;
}

TEST(ParallelCodeGenTuningTest,
     LiteralOptionOracleMatchesRegisteredDefaultsAndCapture) {
  neverc::plugin::PluginLLVMOptionSnapshot Snapshot(
      neverc::plugin::pluginLLVMOptionGate());

  parseLLVMOptions(ArrayRef<std::string>());
  auto &Registered = cl::getRegisteredOptions(cl::SubCommand::getTopLevel());
  const neverc::ParallelCodeGenTuning StructDefaults;
  const neverc::ParallelCodeGenTuning CapturedDefaults =
      neverc::captureParallelCodeGenTuning();
  for (const ParallelCodeGenOptionOracle &Oracle :
       ParallelCodeGenOptionOracles) {
    SCOPED_TRACE(Oracle.Spelling);
    EXPECT_NE(Registered.find(Oracle.Spelling), Registered.end());
    EXPECT_EQ(StructDefaults.*Oracle.Member, Oracle.DefaultValue);
    EXPECT_EQ(CapturedDefaults.*Oracle.Member, Oracle.DefaultValue);
  }

  std::vector<std::string> Arguments;
  Arguments.reserve(ParallelCodeGenOptionOracles.size());
  std::array<std::uint32_t, ParallelCodeGenOptionOracles.size()> Values{};
  for (std::size_t Index = 0; Index != ParallelCodeGenOptionOracles.size();
       ++Index) {
    Values[Index] = 1001u + static_cast<std::uint32_t>(Index * 37u);
    Arguments.push_back((Twine("-") +
                         ParallelCodeGenOptionOracles[Index].Spelling + "=" +
                         Twine(Values[Index]))
                            .str());
  }
  parseLLVMOptions(Arguments);

  const neverc::ParallelCodeGenTuning Captured =
      neverc::captureParallelCodeGenTuning();
  for (std::size_t Index = 0; Index != ParallelCodeGenOptionOracles.size();
       ++Index) {
    SCOPED_TRACE(ParallelCodeGenOptionOracles[Index].Spelling);
    EXPECT_EQ(Captured.*ParallelCodeGenOptionOracles[Index].Member,
              Values[Index]);
  }
}

TEST(ParallelCodeGenTuningTest,
     OccurredOptionsOverlayTypedBaseWithLastOccurrenceSemantics) {
  neverc::plugin::PluginLLVMOptionSnapshot Snapshot(
      neverc::plugin::pluginLLVMOptionGate());

  neverc::ParallelCodeGenTuning Base;
  for (std::size_t Index = 0; Index != ParallelCodeGenOptionOracles.size();
       ++Index)
    Base.*ParallelCodeGenOptionOracles[Index].Member =
        2001u + static_cast<std::uint32_t>(Index * 41u);

  const std::vector<std::string> Arguments = {
      "-neverc-pcg-min-funcs=71",
      "-neverc-pcg-opt-max-parts",
      "72",
      "-neverc-auto-lto-scev-huge-expr-threshold=73",
      "-neverc-pcg-opt-max-parts=74",
  };
  parseLLVMOptions(Arguments);

  neverc::ParallelCodeGenTuning Expected = Base;
  Expected.MinDefinedFunctions = 71;
  Expected.OptMaxPartitions = 74;
  Expected.SCEVHugeExprThreshold = 73;
  const neverc::ParallelCodeGenTuning Overlaid =
      neverc::overlayOccurredParallelCodeGenTuning(Base);

  for (const ParallelCodeGenOptionOracle &Oracle :
       ParallelCodeGenOptionOracles) {
    SCOPED_TRACE(Oracle.Spelling);
    EXPECT_EQ(Overlaid.*Oracle.Member, Expected.*Oracle.Member);
  }
}

TEST(NevercPipelineTuningTest,
     LiteralOptionOracleMatchesRegisteredDefaultsAndCapture) {
  neverc::plugin::PluginLLVMOptionSnapshot Snapshot(
      neverc::plugin::pluginLLVMOptionGate());

  parseLLVMOptions(ArrayRef<std::string>());
  auto &Registered = cl::getRegisteredOptions(cl::SubCommand::getTopLevel());
  const llvm::NevercPipelineTuningOptions StructDefaults;
  const llvm::NevercPipelineTuningOptions CapturedDefaults =
      llvm::captureNevercPipelineTuningOptions();
  for (const NevercPipelineOptionOracle &Oracle : NevercPipelineOptionOracles) {
    SCOPED_TRACE(Oracle.Spelling);
    EXPECT_NE(Registered.find(Oracle.Spelling), Registered.end());
    EXPECT_EQ(Oracle.Get(StructDefaults), Oracle.DefaultValue);
    EXPECT_EQ(Oracle.Get(CapturedDefaults), Oracle.DefaultValue);
  }

  const std::vector<std::string> Arguments = {
      "-neverc-module-inliner-threshold=7001",
      "-neverc-auto-lto-inline-threshold",
      "-73",
      "-neverc-inliner-lite-fsimpl=0",
      "-neverc-inline-max-caller-loops=7004",
      "-neverc-full-unroll-max-loops-per-function=7005",
  };
  parseLLVMOptions(Arguments);

  const llvm::NevercPipelineTuningOptions Captured =
      llvm::captureNevercPipelineTuningOptions();
  const std::array<std::int64_t, 5> Expected = {7001, -73, 0, 7004, 7005};
  for (std::size_t Index = 0; Index != NevercPipelineOptionOracles.size();
       ++Index) {
    SCOPED_TRACE(NevercPipelineOptionOracles[Index].Spelling);
    EXPECT_EQ(NevercPipelineOptionOracles[Index].Get(Captured),
              Expected[Index]);
  }
}

TEST(NevercPipelineTuningTest,
     OccurredOptionsOverlayTypedBaseWithLastOccurrenceSemantics) {
  neverc::plugin::PluginLLVMOptionSnapshot Snapshot(
      neverc::plugin::pluginLLVMOptionGate());

  llvm::NevercPipelineTuningOptions Base;
  for (std::size_t Index = 0; Index != NevercPipelineOptionOracles.size();
       ++Index)
    NevercPipelineOptionOracles[Index].Set(Base, 8001 + Index * 43);

  const std::vector<std::string> Arguments = {
      "-neverc-module-inliner-threshold=81",
      "-neverc-auto-lto-inline-threshold",
      "82",
      "-neverc-inliner-lite-fsimpl=0",
      "-neverc-module-inliner-threshold=84",
  };
  parseLLVMOptions(Arguments);

  llvm::NevercPipelineTuningOptions Expected = Base;
  Expected.ModuleInlinerThreshold = 84;
  Expected.AutoLTOInlineThreshold = 82;
  Expected.InlinerLiteFSimpl = false;
  const llvm::NevercPipelineTuningOptions Overlaid =
      llvm::overlayOccurredNevercPipelineTuningOptions(Base);

  for (const NevercPipelineOptionOracle &Oracle : NevercPipelineOptionOracles) {
    SCOPED_TRACE(Oracle.Spelling);
    EXPECT_EQ(Oracle.Get(Overlaid), Oracle.Get(Expected));
  }
}

TEST(ParallelCodeGenABICompatibilityTest,
     LegacyNamesKeepExactFunctionPointersAndBracedCalls) {
  LegacyParallelCodeGenFunction CodeGen = &neverc::runParallelCodeGen;
  LegacyParallelOptAndCodeGenFunction OptAndCodeGen =
      &neverc::runParallelOptAndCodeGen;

  EXPECT_TRUE(CodeGen != nullptr);
  EXPECT_TRUE(OptAndCodeGen != nullptr);
  EXPECT_TRUE(&linker::parseMllvmOptions != nullptr);
  EXPECT_TRUE(&callLegacyParallelCodeGenWithBracedPartitionCount != nullptr);
  EXPECT_TRUE(&callLegacyParallelOptAndCodeGenWithBracedArguments != nullptr);
}

TEST(LTOSerialIPOPolicyTest,
     ModuleInlinerThresholdSelectsTheRealSerialIPOPipeline) {
  std::unique_ptr<TargetMachine> Machine = createNativeTargetMachine();
  ASSERT_TRUE(Machine);
  SmallString<0> Bitcode =
      createSerialIPOLoopBitcode(*Machine, /*LoopCount=*/1);
  MemoryBufferRef BitcodeRef(StringRef(Bitcode.data(), Bitcode.size()),
                             "serial-ipo-module-inliner.bc");

  NevercPipelineTuningOptions FlatTuning;
  FlatTuning.ModuleInlinerThreshold = 1;
  FlatTuning.AutoLTOInlineThreshold = 1;
  FlatTuning.InlinerLiteFSimpl = false;
  FlatTuning.InlineMaxCallerLoops = 0;
  FlatTuning.FullUnrollMaxLoopsPerFunction = 0;
  Expected<unsigned> FlatBackedges = observeSerialIPO(
      createSerialIPODriverConfig(FlatTuning), BitcodeRef, [](const Module &M) {
        return countFunctionBackedges(M, "neverc_serial_ipo_loop_probe");
      });
  ASSERT_TRUE(static_cast<bool>(FlatBackedges))
      << errorText(FlatBackedges.takeError());

  NevercPipelineTuningOptions CGSCCTuning = FlatTuning;
  CGSCCTuning.ModuleInlinerThreshold = 2;
  Expected<unsigned> CGSCCBackedges = observeSerialIPO(
      createSerialIPODriverConfig(CGSCCTuning), BitcodeRef,
      [](const Module &M) {
        return countFunctionBackedges(M, "neverc_serial_ipo_loop_probe");
      });
  ASSERT_TRUE(static_cast<bool>(CGSCCBackedges))
      << errorText(CGSCCBackedges.takeError());

  EXPECT_EQ(*FlatBackedges, 1u)
      << "the one-function module should select the flat module inliner, "
         "whose serial path does not run the CGSCC full-unroll pipeline";
  EXPECT_EQ(*CGSCCBackedges, 0u)
      << "raising the threshold above the one-function module should select "
         "the CGSCC pipeline and fully unroll the constant-trip loop";
}

TEST(LTOSerialIPOPolicyTest,
     AutoLTOInlineThresholdControlsTheRealSerialIPOAdvisor) {
  std::unique_ptr<TargetMachine> Machine = createNativeTargetMachine();
  ASSERT_TRUE(Machine);
  SmallString<0> Bitcode = createSerialIPOInlineBitcode(*Machine);
  MemoryBufferRef BitcodeRef(StringRef(Bitcode.data(), Bitcode.size()),
                             "serial-ipo-inline-threshold.bc");

  NevercPipelineTuningOptions LowTuning;
  LowTuning.ModuleInlinerThreshold = 0;
  LowTuning.AutoLTOInlineThreshold = 1;
  LowTuning.InlinerLiteFSimpl = true;
  LowTuning.InlineMaxCallerLoops = 0;
  Expected<unsigned> LowThresholdCalls = observeSerialIPO(
      createSerialIPODriverConfig(LowTuning), BitcodeRef, [](const Module &M) {
        return countDirectCalls(M, "neverc_serial_ipo_inline_root",
                                "neverc_serial_ipo_inline_leaf");
      });
  ASSERT_TRUE(static_cast<bool>(LowThresholdCalls))
      << errorText(LowThresholdCalls.takeError());

  NevercPipelineTuningOptions HighTuning = LowTuning;
  HighTuning.AutoLTOInlineThreshold = 100000;
  Expected<unsigned> HighThresholdCalls = observeSerialIPO(
      createSerialIPODriverConfig(HighTuning), BitcodeRef, [](const Module &M) {
        return countDirectCalls(M, "neverc_serial_ipo_inline_root",
                                "neverc_serial_ipo_inline_leaf");
      });
  ASSERT_TRUE(static_cast<bool>(HighThresholdCalls))
      << errorText(HighThresholdCalls.takeError());

  EXPECT_EQ(*LowThresholdCalls, 1u)
      << "the low request-local threshold should reject this medium-sized "
         "leaf at the real LTO call site";
  EXPECT_EQ(*HighThresholdCalls, 0u)
      << "the high request-local threshold should inline the same leaf";
}

TEST(LTOSerialIPOPolicyTest,
     InlinerLiteFSimplControlsTheRealSerialCGSCCCleanup) {
  std::unique_ptr<TargetMachine> Machine = createNativeTargetMachine();
  ASSERT_TRUE(Machine);
  SmallString<0> Bitcode =
      createSerialIPOLoopBitcode(*Machine, /*LoopCount=*/1);
  MemoryBufferRef BitcodeRef(StringRef(Bitcode.data(), Bitcode.size()),
                             "serial-ipo-inliner-lite.bc");

  NevercPipelineTuningOptions LiteTuning;
  LiteTuning.ModuleInlinerThreshold = 0;
  LiteTuning.AutoLTOInlineThreshold = 1;
  LiteTuning.InlinerLiteFSimpl = true;
  LiteTuning.InlineMaxCallerLoops = 0;
  LiteTuning.FullUnrollMaxLoopsPerFunction = 0;
  Expected<unsigned> LiteBackedges = observeSerialIPO(
      createSerialIPODriverConfig(LiteTuning), BitcodeRef, [](const Module &M) {
        return countFunctionBackedges(M, "neverc_serial_ipo_loop_probe");
      });
  ASSERT_TRUE(static_cast<bool>(LiteBackedges))
      << errorText(LiteBackedges.takeError());

  NevercPipelineTuningOptions FullTuning = LiteTuning;
  FullTuning.InlinerLiteFSimpl = false;
  Expected<unsigned> FullBackedges = observeSerialIPO(
      createSerialIPODriverConfig(FullTuning), BitcodeRef, [](const Module &M) {
        return countFunctionBackedges(M, "neverc_serial_ipo_loop_probe");
      });
  ASSERT_TRUE(static_cast<bool>(FullBackedges))
      << errorText(FullBackedges.takeError());

  EXPECT_EQ(*LiteBackedges, 1u)
      << "the lite CGSCC cleanup intentionally omits full unrolling";
  EXPECT_EQ(*FullBackedges, 0u)
      << "the full CGSCC cleanup should fully unroll the same loop";
}

TEST(LTOSerialIPOPolicyTest,
     DeferredOptimizationKeepsFullUnrollPolicyAfterPCGDeclines) {
  std::unique_ptr<TargetMachine> Machine = createNativeTargetMachine();
  ASSERT_TRUE(Machine);
  SmallString<0> Bitcode =
      createSerialIPOLoopBitcode(*Machine, /*LoopCount=*/2);
  MemoryBufferRef BitcodeRef(StringRef(Bitcode.data(), Bitcode.size()),
                             "deferred-full-unroll-policy.bc");

  NevercPipelineTuningOptions CappedTuning;
  CappedTuning.ModuleInlinerThreshold = 0;
  CappedTuning.AutoLTOInlineThreshold = 1;
  CappedTuning.InlinerLiteFSimpl = true;
  CappedTuning.InlineMaxCallerLoops = 0;
  CappedTuning.FullUnrollMaxLoopsPerFunction = 1;
  linker::LinkerDriverConfig CappedConfig =
      createSerialIPODriverConfig(CappedTuning);
  CappedConfig.parallelCodeGenTuning.MinDefinedFunctions = 8;
  Expected<DeferredSerialObservation> Capped =
      observeDeferredSerialOptimization(CappedConfig, BitcodeRef);
  ASSERT_TRUE(static_cast<bool>(Capped)) << errorText(Capped.takeError());

  NevercPipelineTuningOptions UncappedTuning = CappedTuning;
  UncappedTuning.FullUnrollMaxLoopsPerFunction = 0;
  linker::LinkerDriverConfig UncappedConfig =
      createSerialIPODriverConfig(UncappedTuning);
  UncappedConfig.parallelCodeGenTuning.MinDefinedFunctions = 8;
  Expected<DeferredSerialObservation> Uncapped =
      observeDeferredSerialOptimization(UncappedConfig, BitcodeRef);
  ASSERT_TRUE(static_cast<bool>(Uncapped)) << errorText(Uncapped.takeError());

  auto ExpectDeclinedDeferredPath = [](const DeferredSerialObservation &Value) {
    EXPECT_TRUE(Value.PostOptObserved);
    EXPECT_EQ(Value.PostOptBackedges, 2u)
        << "the lite serial IPO phase should leave both loops for deferred "
           "optimization";
    EXPECT_TRUE(Value.ParallelOptCodeGenCalled);
    EXPECT_FALSE(Value.ParallelOptCodeGenAccepted);
    EXPECT_TRUE(Value.ParallelCodeGenCalled);
    EXPECT_FALSE(Value.ParallelCodeGenAccepted);
    EXPECT_TRUE(Value.PreCodeGenObserved)
        << "declining both PCG hooks must reach LTOBackend's deferred serial "
           "optimization fallback";
  };
  ExpectDeclinedDeferredPath(*Capped);
  ExpectDeclinedDeferredPath(*Uncapped);

  EXPECT_EQ(Capped->PreCodeGenBackedges, 2u)
      << "the request-local loop-density cap must survive both PCG declines";
  EXPECT_EQ(Uncapped->PreCodeGenBackedges, 0u)
      << "the uncapped control must prove both loops are fully unrollable in "
         "the deferred optimization pipeline";
}

TEST(LTOCacheTest, PCGTuningAllFieldsAffectFullAndPartitionKeys) {
  linker::LinkerDriverConfig Base;
  Base.cpu = "cache-key-test-cpu";
  const LTOCacheIdentity Baseline = cacheIdentity(Base);
  const linker::LinkerDriverConfig Identical = Base;
  const LTOCacheIdentity IdenticalIdentity = cacheIdentity(Identical);
  EXPECT_EQ(Baseline.Full, IdenticalIdentity.Full);
  EXPECT_EQ(Baseline.Partition, IdenticalIdentity.Partition);

  for (std::size_t Index = 0; Index != ParallelCodeGenOptionOracles.size();
       ++Index) {
    linker::LinkerDriverConfig Mutated = Base;
    Mutated.parallelCodeGenTuning.*ParallelCodeGenOptionOracles[Index].Member +=
        101u + static_cast<std::uint32_t>(Index);
    const LTOCacheIdentity MutatedIdentity = cacheIdentity(Mutated);

    SCOPED_TRACE(ParallelCodeGenOptionOracles[Index].Spelling);
    EXPECT_NE(Baseline.Full, MutatedIdentity.Full);
    EXPECT_NE(Baseline.Partition, MutatedIdentity.Partition);
  }
}

TEST(LTOCacheTest, NevercPipelineTuningAllFieldsAffectFullAndPartitionKeys) {
  linker::LinkerDriverConfig Base;
  Base.cpu = "pipeline-tuning-cache-key-test-cpu";
  const LTOCacheIdentity Baseline = cacheIdentity(Base);

  for (std::size_t Index = 0; Index != NevercPipelineOptionOracles.size();
       ++Index) {
    linker::LinkerDriverConfig Mutated = Base;
    const std::int64_t Previous =
        NevercPipelineOptionOracles[Index].Get(Mutated.ltoPipelineTuning);
    NevercPipelineOptionOracles[Index].Set(
        Mutated.ltoPipelineTuning,
        NevercPipelineOptionOracles[Index].Spelling ==
                std::string("neverc-inliner-lite-fsimpl")
            ? !Previous
            : Previous + 101 + static_cast<std::int64_t>(Index));
    const LTOCacheIdentity MutatedIdentity = cacheIdentity(Mutated);

    SCOPED_TRACE(NevercPipelineOptionOracles[Index].Spelling);
    EXPECT_NE(Baseline.Full, MutatedIdentity.Full);
    EXPECT_NE(Baseline.Partition, MutatedIdentity.Partition);
  }
}

TEST(LTOCacheTest, PCGTuningRawArgumentVectorRemainsKeyMaterial) {
  linker::LinkerDriverConfig Config;
  Config.cpu = "cache-key-test-cpu";
  for (std::size_t Index = 0; Index != ParallelCodeGenOptionOracles.size();
       ++Index) {
    Config.mllvmOpts.push_back(
        (Twine("-") + ParallelCodeGenOptionOracles[Index].Spelling + "=" +
         Twine(3001u + static_cast<unsigned>(Index)))
            .str());
  }
  Config.mllvmOpts.push_back("-neverc-test-opaque-option=unchanged");
  Config.mllvmOpts.push_back("--");
  Config.mllvmOpts.push_back("-neverc-pcg-min-funcs=3999");

  const LTOCacheIdentity Baseline = cacheIdentity(Config);
  for (std::size_t Index = 0; Index != Config.mllvmOpts.size(); ++Index) {
    linker::LinkerDriverConfig Mutated = Config;
    Mutated.mllvmOpts[Index].append("-different");
    const LTOCacheIdentity MutatedIdentity = cacheIdentity(Mutated);

    SCOPED_TRACE(Index);
    EXPECT_NE(Baseline.Full, MutatedIdentity.Full);
    EXPECT_NE(Baseline.Partition, MutatedIdentity.Partition);
  }

  linker::LinkerDriverConfig Reordered = Config;
  std::swap(Reordered.mllvmOpts[0], Reordered.mllvmOpts[1]);
  const LTOCacheIdentity ReorderedIdentity = cacheIdentity(Reordered);
  EXPECT_NE(Baseline.Full, ReorderedIdentity.Full);
  EXPECT_NE(Baseline.Partition, ReorderedIdentity.Partition);
}

TEST(LTOCacheTest, PCGTuningTypedRawConflictsCannotAlias) {
  linker::LinkerDriverConfig TypedBaseA;
  TypedBaseA.cpu = "cache-key-test-cpu";
  TypedBaseA.parallelCodeGenTuning.MinDefinedFunctions = 401;
  TypedBaseA.mllvmOpts = {"-neverc-pcg-min-funcs=402"};

  linker::LinkerDriverConfig TypedBaseB = TypedBaseA;
  TypedBaseB.parallelCodeGenTuning.MinDefinedFunctions = 402;

  linker::LinkerDriverConfig RawFreeEquivalent = TypedBaseB;
  RawFreeEquivalent.mllvmOpts.clear();

  const LTOCacheIdentity ConflictA = cacheIdentity(TypedBaseA);
  const LTOCacheIdentity ConflictB = cacheIdentity(TypedBaseB);
  const LTOCacheIdentity NoRawArgument = cacheIdentity(RawFreeEquivalent);
  EXPECT_NE(ConflictA.Full, ConflictB.Full);
  EXPECT_NE(ConflictA.Partition, ConflictB.Partition);
  EXPECT_NE(ConflictB.Full, NoRawArgument.Full);
  EXPECT_NE(ConflictB.Partition, NoRawArgument.Partition);
}

TEST(LTOCacheTest, NevercPipelineTuningTypedRawConflictsCannotAlias) {
  linker::LinkerDriverConfig TypedBaseA;
  TypedBaseA.cpu = "pipeline-tuning-cache-key-test-cpu";
  TypedBaseA.ltoPipelineTuning.ModuleInlinerThreshold = 501;
  TypedBaseA.mllvmOpts = {"-neverc-module-inliner-threshold=502"};

  linker::LinkerDriverConfig TypedBaseB = TypedBaseA;
  TypedBaseB.ltoPipelineTuning.ModuleInlinerThreshold = 502;

  linker::LinkerDriverConfig RawFreeEquivalent = TypedBaseB;
  RawFreeEquivalent.mllvmOpts.clear();

  const LTOCacheIdentity ConflictA = cacheIdentity(TypedBaseA);
  const LTOCacheIdentity ConflictB = cacheIdentity(TypedBaseB);
  const LTOCacheIdentity NoRawArgument = cacheIdentity(RawFreeEquivalent);
  EXPECT_NE(ConflictA.Full, ConflictB.Full);
  EXPECT_NE(ConflictA.Partition, ConflictB.Partition);
  EXPECT_NE(ConflictB.Full, NoRawArgument.Full);
  EXPECT_NE(ConflictB.Partition, NoRawArgument.Partition);
}

TEST(ConcurrentLTOOptionIsolationTest,
     EmptyProfileStartsFromDefaultsAndRestoresAmbientState) {
  ASSERT_TRUE(createNativeTargetMachine());
  auto RestoreMarker = make_scope_exit([] { LTOProfileMarker.reset(); });

  LTOProfileMarker.reset();
  ASSERT_FALSE(
      LTOProfileMarker.addOccurrence(7, LTOProfileMarker.ArgStr, "29"));
  {
    linker::LinkerDriverConfig NonEmptyConfig = createDriverConfig(41, 64);
    lto::Config Profile = linker::createLTOConfig(
        NonEmptyConfig, [](const DiagnosticInfo &) {}, /*EmitAddrsig=*/true);
    EXPECT_EQ(static_cast<unsigned>(LTOProfileMarker), 41u);
    EXPECT_EQ(LTOProfileMarker.getNumOccurrences(), 1);
  }
  EXPECT_EQ(static_cast<unsigned>(LTOProfileMarker), 29u);
  EXPECT_EQ(LTOProfileMarker.getNumOccurrences(), 1);
  EXPECT_EQ(LTOProfileMarker.getPosition(), 7u);

  {
    linker::LinkerDriverConfig EmptyConfig;
    lto::Config Profile = linker::createLTOConfig(
        EmptyConfig, [](const DiagnosticInfo &) {}, /*EmitAddrsig=*/true);
    EXPECT_EQ(static_cast<unsigned>(LTOProfileMarker), 17u);
    EXPECT_EQ(LTOProfileMarker.getNumOccurrences(), 0);
  }
  EXPECT_EQ(static_cast<unsigned>(LTOProfileMarker), 29u);
  EXPECT_EQ(LTOProfileMarker.getNumOccurrences(), 1);
  EXPECT_EQ(LTOProfileMarker.getPosition(), 7u);
}

TEST(ConcurrentLTOOptionIsolationTest,
     OwnsProfilesThroughMIRPluginLTOAndRestoresEntryState) {
  std::unique_ptr<TargetMachine> Machine = createNativeTargetMachine();
  ASSERT_TRUE(Machine);
  SmallString<0> Bitcode = createLTOBitcode(*Machine);
  ASSERT_FALSE(Bitcode.empty());

  MIRPluginSessionScope PluginScope;
  std::string PluginInitError = errorText(PluginScope.initialize());
  ASSERT_TRUE(PluginInitError.empty()) << PluginInitError;

  auto RestoreMarker = make_scope_exit([] { LTOProfileMarker.reset(); });
  LTOProfileMarker.reset();
  ASSERT_FALSE(
      LTOProfileMarker.addOccurrence(7, LTOProfileMarker.ArgStr, "29"));
  ASSERT_EQ(static_cast<unsigned>(LTOProfileMarker), 29u);
  ASSERT_EQ(LTOProfileMarker.getNumOccurrences(), 1);
  ASSERT_EQ(LTOProfileMarker.getPosition(), 7u);

  linker::LinkerDriverConfig FirstConfig = createDriverConfig(41, 64);
  FirstConfig.pluginSession = PluginScope.session();
  linker::LinkerDriverConfig SecondConfig = createDriverConfig(83, 512);
  MemoryBufferRef BitcodeRef(StringRef(Bitcode.data(), Bitcode.size()),
                             "concurrent-lto-input.bc");

  Expected<std::unique_ptr<PreparedLTO>> FirstOrError =
      PreparedLTO::create(FirstConfig, BitcodeRef);
  ASSERT_TRUE(static_cast<bool>(FirstOrError))
      << errorText(FirstOrError.takeError());
  std::unique_ptr<PreparedLTO> First = std::move(*FirstOrError);
  EXPECT_EQ(static_cast<unsigned>(LTOProfileMarker), 41u);
  EXPECT_TRUE(First->machineHooksAliasHostContext());
  ASSERT_TRUE(
      neverc::plugin::pluginLLVMOptionGateHeldExclusivelyByCurrentThread());

  std::promise<void> AcquiredPromise;
  std::future<void> Acquired = AcquiredPromise.get_future();
  const std::uint64_t WaitEpoch =
      neverc::plugin::pluginLLVMOptionExclusiveWaitEpoch();
  RunPermit Permit;
  std::string SecondCreateError;
  std::string SecondRunError;
  std::string SecondObjectError;
  unsigned SecondMarkerBeforeRun = 0;
  unsigned SecondMarkerAfterRun = 0;

  std::thread Worker;
  auto Cleanup = make_scope_exit([&] {
    First.reset();
    Permit.release();
    if (Worker.joinable())
      Worker.join();
  });
  Worker = std::thread([&] {
    Expected<std::unique_ptr<PreparedLTO>> SecondOrError =
        PreparedLTO::create(SecondConfig, BitcodeRef);
    if (!SecondOrError) {
      SecondCreateError = errorText(SecondOrError.takeError());
      AcquiredPromise.set_value();
      return;
    }

    std::unique_ptr<PreparedLTO> Second = std::move(*SecondOrError);
    SecondMarkerBeforeRun = static_cast<unsigned>(LTOProfileMarker);
    AcquiredPromise.set_value();
    Permit.wait();
    SecondRunError = errorText(Second->run());
    SecondMarkerAfterRun = static_cast<unsigned>(LTOProfileMarker);
    if (SecondRunError.empty())
      SecondObjectError = errorText(Second->validateObjects());
  });

  EXPECT_TRUE(waitForExclusiveOptionWait(WaitEpoch, std::chrono::seconds(5)));
  EXPECT_EQ(Acquired.wait_for(std::chrono::milliseconds(0)),
            std::future_status::timeout);

  const unsigned FirstMarkerBeforeRun = static_cast<unsigned>(LTOProfileMarker);
  std::string FirstRunError = errorText(First->run());
  const unsigned FirstMarkerAfterRun = static_cast<unsigned>(LTOProfileMarker);
  EXPECT_TRUE(
      neverc::plugin::pluginLLVMOptionGateHeldExclusivelyByCurrentThread());
  std::string FirstObjectError;
  if (FirstRunError.empty())
    FirstObjectError = errorText(First->validateObjects());

  EXPECT_EQ(Acquired.wait_for(std::chrono::milliseconds(0)),
            std::future_status::timeout);
  First.reset();
  EXPECT_FALSE(
      neverc::plugin::pluginLLVMOptionGateHeldExclusivelyByCurrentThread());

  EXPECT_EQ(Acquired.wait_for(std::chrono::seconds(5)),
            std::future_status::ready);
  Permit.release();
  Worker.join();

  EXPECT_TRUE(FirstRunError.empty()) << FirstRunError;
  EXPECT_TRUE(FirstObjectError.empty()) << FirstObjectError;
  EXPECT_EQ(FirstMarkerBeforeRun, 41u);
  EXPECT_EQ(FirstMarkerAfterRun, 41u);
  EXPECT_TRUE(SecondCreateError.empty()) << SecondCreateError;
  EXPECT_TRUE(SecondRunError.empty()) << SecondRunError;
  EXPECT_TRUE(SecondObjectError.empty()) << SecondObjectError;
  EXPECT_EQ(SecondMarkerBeforeRun, 83u);
  EXPECT_EQ(SecondMarkerAfterRun, 83u);

  EXPECT_EQ(static_cast<unsigned>(LTOProfileMarker), 29u);
  EXPECT_EQ(LTOProfileMarker.getNumOccurrences(), 1);
  EXPECT_EQ(LTOProfileMarker.getPosition(), 7u);

  FirstConfig.pluginSession.reset();
  std::string PluginFinishError = errorText(PluginScope.finish());
  EXPECT_TRUE(PluginFinishError.empty()) << PluginFinishError;
}

} // namespace
