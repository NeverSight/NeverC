#include "neverc/Emit/Backend/ParallelCodeGenMerge.h"
#include "neverc/Merge/Merger.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/StandardInstrumentations.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Triple.h"

#include "llvm/Support/thread.h"

#include <atomic>
#include <thread>

using namespace llvm;

namespace neverc {

namespace {

bool mergePartitionObjects(const Triple &TT,
                           ArrayRef<SmallVector<char, 0>> Bufs,
                           raw_pwrite_stream &OS) {
  using namespace neverc::merge;
  if (TT.isOSBinFormatCOFF())
    return mergeObjects(Bufs, OS, Format::COFF);
  if (TT.isOSBinFormatELF())
    return mergeObjects(Bufs, OS, Format::ELF64LE);
  if (TT.isOSBinFormatMachO())
    return mergeObjects(Bufs, OS, Format::MachO64);
  return false;
}

// ===----------------------------------------------------------------------===
// Shared infrastructure for both parallel codegen paths
// ===----------------------------------------------------------------------===

struct FuncEntry {
  Function *Fn;
  unsigned Weight;
};

struct LinkageEntry {
  GlobalValue *GV;
  std::string OrigName;
  GlobalValue::LinkageTypes Linkage;
  GlobalValue::VisibilityTypes Visibility;
};

struct PartitionResult {
  SmallVector<char, 0> ObjBuffer;
  bool Success = false;
};

struct PreparedPartition {
  std::unique_ptr<LLVMContext> Ctx;
  std::unique_ptr<Module> M;
  std::unique_ptr<TargetMachine> PTM;
  SmallVector<char, 0> *ObjBuf = nullptr;
  PreparedPartition() = default;
};

struct ParallelCGContext {
  const Target *TheTarget;
  std::string TripleStr;
  Triple TT;
  SmallVector<FuncEntry, 0> FuncList;
  unsigned TotalWeight = 0;
  unsigned FuncCount = 0;
  unsigned NumPartitions = 0;

  std::vector<PartitionResult> Results;
  CodeModel::Model SharedCM;
  CodeGenOptLevel SharedOptLevel;
  std::string SharedFeatures;
  TargetOptions SharedTgtOpts;

  SmallVector<LinkageEntry, 64> SavedLinkage;
  SmallVector<SmallVector<std::string, 0>, 8> Assignments;
  DenseMap<StringRef, unsigned> FuncPartition;
  SmallString<0> FullBC;

  std::unique_ptr<TargetLibraryInfoImpl> SharedTLII;
  std::vector<std::unique_ptr<PreparedPartition>> Parts;

  bool init(Module &Mod, TargetMachine &TM);
  bool resolvePartitions(unsigned WeightDiv, unsigned MaxParts);
  void externalizeAndSerialize(Module &Mod);
  void preparePartitions(StringRef BCRef, TargetMachine &TM);
  bool finalizeResults(raw_pwrite_stream &OS);
  void restoreLinkage();
};

bool ParallelCGContext::init(Module &Mod, TargetMachine &TM) {
  TheTarget = &TM.getTarget();
  TripleStr = Mod.getTargetTriple();
  TT = Triple(TripleStr);

  for (auto &F : Mod)
    if (!F.isDeclaration()) {
      unsigned W = 0;
      for (auto &BB : F)
        W += BB.size();
      TotalWeight += W;
      FuncList.push_back({&F, W});
    }
  FuncCount = FuncList.size();

  if (FuncCount < 8 || TotalWeight < 10000)
    return false;

  SharedCM = TM.getCodeModel();
  SharedOptLevel = TM.getOptLevel();
  SharedFeatures = TM.getTargetFeatureString().str();
  SharedTgtOpts = TM.Options;
  SharedTgtOpts.EmitAddrsig = false;
  SharedTLII = std::make_unique<TargetLibraryInfoImpl>(TT);
  return true;
}

bool ParallelCGContext::resolvePartitions(unsigned WeightDiv,
                                          unsigned MaxParts) {
  if (NumPartitions == 0) {
    unsigned HW = llvm::thread::hardware_concurrency();
    NumPartitions = std::min(
        {HW, std::max(TotalWeight / WeightDiv, 2u), FuncCount, MaxParts});
    if (NumPartitions < 2)
      return false;
  } else if (NumPartitions < 2) {
    return false;
  } else {
    unsigned WeightCap = std::max(TotalWeight / WeightDiv, 2u);
    NumPartitions = std::min({NumPartitions, WeightCap, MaxParts});
  }
  if (FuncCount < NumPartitions)
    NumPartitions = std::max(1u, FuncCount);
  if (NumPartitions < 2)
    return false;
  Results.resize(NumPartitions);
  return true;
}

void ParallelCGContext::externalizeAndSerialize(Module &Mod) {
  SmallString<32> PCGSuffix;
  {
    auto H = hash_value(Mod.getModuleIdentifier());
    raw_svector_ostream(PCGSuffix) << ".__pcg" << (H & 0xFFFFFFFF);
  }

  auto ExternalizeGV = [&](GlobalValue &GV) {
    if (!GV.hasLocalLinkage())
      return;
    SavedLinkage.push_back(
        {&GV, GV.getName().str(), GV.getLinkage(), GV.getVisibility()});
    SmallString<64> NewName(GV.getName());
    NewName += PCGSuffix;
    GV.setName(NewName);
    GV.setLinkage(GlobalValue::ExternalLinkage);
    GV.setVisibility(GlobalValue::HiddenVisibility);
  };
  for (Function &F : Mod)
    ExternalizeGV(F);
  for (GlobalVariable &GV : Mod.globals())
    ExternalizeGV(GV);
  for (GlobalAlias &GA : Mod.aliases())
    ExternalizeGV(GA);
  for (GlobalIFunc &IF : Mod.ifuncs())
    ExternalizeGV(IF);

  DenseSet<StringRef> PinnedToP0;
  for (GlobalAlias &GA : Mod.aliases())
    if (auto *F = dyn_cast<Function>(GA.getAliasee()->stripPointerCasts()))
      PinnedToP0.insert(F->getName());
  for (GlobalIFunc &IF : Mod.ifuncs())
    if (auto *F = dyn_cast<Function>(IF.getResolver()->stripPointerCasts()))
      PinnedToP0.insert(F->getName());

  Assignments.resize(NumPartitions);
  {
    SmallVector<unsigned, 16> Load(NumPartitions, 0);
    llvm::sort(FuncList, [](const FuncEntry &A, const FuncEntry &B) {
      return A.Weight > B.Weight;
    });
    for (auto &FE : FuncList) {
      StringRef Name = FE.Fn->getName();
      unsigned Best = 0;
      if (!PinnedToP0.count(Name))
        for (unsigned p = 1; p < NumPartitions; ++p)
          if (Load[p] < Load[Best])
            Best = p;
      Assignments[Best].push_back(Name.str());
      Load[Best] += FE.Weight;
    }
    for (unsigned p = 0; p < NumPartitions; ++p)
      for (auto &N : Assignments[p])
        FuncPartition[N] = p;
  }

  Mod.dropTriviallyDeadConstantArrays();
  for (StringRef MDName :
       {"llvm.ident", "llvm.linker.options", "llvm.dependent-libraries"})
    if (auto *NMD = Mod.getNamedMetadata(MDName))
      Mod.eraseNamedMetadata(NMD);
  for (Function &F : Mod) {
    if (F.isDeclaration())
      continue;
    for (BasicBlock &BB : F) {
      BB.setName("");
      for (Instruction &I : BB)
        I.setName("");
    }
  }

  FullBC.reserve(TotalWeight * 40);
  {
    raw_svector_ostream BCOS(FullBC);
    WriteBitcodeToFile(Mod, BCOS, false);
  }
}

void ParallelCGContext::preparePartitions(StringRef BCRef, TargetMachine &TM) {
  Parts.resize(NumPartitions);
  unsigned PrepThreadCount =
      std::min(llvm::thread::hardware_concurrency(), NumPartitions);
  std::atomic<unsigned> PrepNextPart{0};
  std::vector<std::thread> PrepWorkers;
  PrepWorkers.reserve(PrepThreadCount);

  auto PrepWorker = [&]() {
    while (true) {
      unsigned p = PrepNextPart.fetch_add(1, std::memory_order_relaxed);
      if (p >= NumPartitions)
        break;
      auto PP = std::make_unique<PreparedPartition>();
      PP->ObjBuf = &Results[p].ObjBuffer;
      PP->Ctx = std::make_unique<LLVMContext>();
      PP->Ctx->setDiscardValueNames(true);
      auto MOrErr =
          getLazyBitcodeModule(MemoryBufferRef(BCRef, "lto-pcg"), *PP->Ctx);
      if (!MOrErr) {
        consumeError(MOrErr.takeError());
        continue;
      }
      PP->M = std::move(*MOrErr);
      auto &MPart = *PP->M;

      bool Failed = false;
      for (const std::string &Name : Assignments[p]) {
        Function *F = MPart.getFunction(Name);
        if (!F || F->isDeclaration())
          continue;
        if (auto Err = F->materialize()) {
          consumeError(std::move(Err));
          Failed = true;
          break;
        }
      }
      if (Failed)
        continue;
      if (p == 0) {
        for (GlobalVariable &GV : MPart.globals())
          if (auto Err = GV.materialize()) {
            consumeError(std::move(Err));
            Failed = true;
            break;
          }
        if (Failed)
          continue;
      } else {
        for (GlobalVariable &GV : make_early_inc_range(MPart.globals())) {
          if (GV.isDeclaration())
            continue;
          if (GV.hasAppendingLinkage()) {
            GV.eraseFromParent();
            continue;
          }
          GV.setInitializer(nullptr);
          GV.setLinkage(GlobalValue::ExternalLinkage);
        }
      }
      for (Function &F : MPart) {
        if (F.isDeclaration())
          continue;
        auto It = FuncPartition.find(F.getName());
        if (It != FuncPartition.end() && It->second == p)
          continue;
        F.deleteBody();
        F.setComdat(nullptr);
      }
      if (p != 0) {
        for (GlobalAlias &GA : make_early_inc_range(MPart.aliases()))
          GA.eraseFromParent();
        for (GlobalIFunc &IF : make_early_inc_range(MPart.ifuncs()))
          IF.eraseFromParent();
      }

      PP->PTM.reset(TheTarget->createTargetMachine(
          TripleStr, TM.getTargetCPU().str(), SharedFeatures, SharedTgtOpts,
          SharedCM, SharedOptLevel));
      if (!PP->PTM)
        continue;
      MPart.setDataLayout(PP->PTM->createDataLayout());
      Parts[p] = std::move(PP);
    }
  };
  for (unsigned i = 0; i < PrepThreadCount; ++i)
    PrepWorkers.emplace_back(PrepWorker);
  for (auto &T : PrepWorkers)
    T.join();
}

bool ParallelCGContext::finalizeResults(raw_pwrite_stream &OS) {
  bool AllOK = true;
  for (unsigned i = 0; i < NumPartitions; ++i)
    if (!Results[i].Success) {
      AllOK = false;
      break;
    }

  if (!AllOK) {
    restoreLinkage();
    return false;
  }

  unsigned NonEmpty = 0, SingleIdx = 0;
  for (unsigned i = 0; i < NumPartitions; ++i)
    if (!Results[i].ObjBuffer.empty()) {
      NonEmpty++;
      SingleIdx = i;
    }
  if (NonEmpty <= 1) {
    if (NonEmpty == 1)
      OS.write(Results[SingleIdx].ObjBuffer.data(),
               Results[SingleIdx].ObjBuffer.size());
    return true;
  }

  SmallVector<SmallVector<char, 0>, 8> Bufs;
  for (unsigned i = 0; i < NumPartitions; ++i)
    Bufs.push_back(std::move(Results[i].ObjBuffer));

  bool OK = mergePartitionObjects(TT, Bufs, OS);

  // Push per-partition LLVMContext destruction onto a detached worker.
  // Each context owns ~MB of type/metadata/constant state whose destructor
  // would otherwise burn ~200ms of serial wall time on a Redis-sized link.
  {
    auto OwnedParts =
        std::make_unique<std::vector<std::unique_ptr<PreparedPartition>>>(
            std::move(Parts));
    std::thread([P = std::move(OwnedParts)]() mutable { P.reset(); }).detach();
  }
  return OK;
}

void ParallelCGContext::restoreLinkage() {
  for (auto &E : SavedLinkage) {
    E.GV->setName(E.OrigName);
    E.GV->setLinkage(E.Linkage);
    E.GV->setVisibility(E.Visibility);
  }
}

} // namespace

// ===----------------------------------------------------------------------===
// Public API: parallel codegen (no per-partition optimization)
// ===----------------------------------------------------------------------===

bool runParallelCodeGen(Module &Mod, TargetMachine &TM, raw_pwrite_stream &OS,
                        unsigned /*NumPartitions*/) {
  ParallelCGContext Ctx;
  if (!Ctx.init(Mod, TM))
    return false;

  if (!Ctx.resolvePartitions(/*WeightDiv=*/5000,
                             /*MaxParts=*/Ctx.FuncCount))
    return false;

  Ctx.externalizeAndSerialize(Mod);
  StringRef BCRef(Ctx.FullBC.data(), Ctx.FullBC.size());
  Ctx.preparePartitions(BCRef, TM);

  // Phase 2: codegen only (legacy PM).
  {
    unsigned ThreadCount =
        std::min(llvm::thread::hardware_concurrency(), Ctx.NumPartitions);
    std::atomic<unsigned> NextPart{0};
    std::vector<std::thread> Workers;
    Workers.reserve(ThreadCount);
    auto Worker = [&]() {
      while (true) {
        unsigned p = NextPart.fetch_add(1, std::memory_order_relaxed);
        if (p >= Ctx.NumPartitions)
          break;
        if (!Ctx.Parts[p])
          continue;
        auto &PP = *Ctx.Parts[p];
        raw_svector_ostream ObjOS(*PP.ObjBuf);
        legacy::PassManager PM;
        PM.add(createTargetTransformInfoWrapperPass(
            PP.PTM->getTargetIRAnalysis()));
        PM.add(new TargetLibraryInfoWrapperPass(*Ctx.SharedTLII));
        if (!PP.PTM->addPassesToEmitFile(PM, ObjOS, nullptr,
                                         CodeGenFileType::ObjectFile, true)) {
          PM.run(*PP.M);
          Ctx.Results[p].Success = true;
        }
      }
    };
    for (unsigned i = 0; i < ThreadCount; ++i)
      Workers.emplace_back(Worker);
    for (auto &T : Workers)
      T.join();
  }

  return Ctx.finalizeResults(OS);
}

// ===----------------------------------------------------------------------===
// Public API: parallel optimization + codegen
// ===----------------------------------------------------------------------===

bool runParallelOptAndCodeGen(Module &Mod, TargetMachine &TM,
                              raw_pwrite_stream &OS, unsigned /*NumPartitions*/,
                              unsigned OptLevel) {
  if (OptLevel == 0)
    return false;

  ParallelCGContext Ctx;
  if (!Ctx.init(Mod, TM))
    return false;

  if (!Ctx.resolvePartitions(/*WeightDiv=*/12000, /*MaxParts=*/16))
    return false;

  Ctx.externalizeAndSerialize(Mod);
  StringRef BCRef(Ctx.FullBC.data(), Ctx.FullBC.size());
  Ctx.preparePartitions(BCRef, TM);

  OptimizationLevel OL;
  switch (OptLevel) {
  case 1:
    OL = OptimizationLevel::O1;
    break;
  case 2:
    OL = OptimizationLevel::O2;
    break;
  default:
    OL = OptimizationLevel::O3;
    break;
  }

  PipelineTuningOptions SharedPTO;
  SharedPTO.LoopUnrolling = OptLevel >= 2;
  SharedPTO.LoopInterleaving = OptLevel >= 2;
  SharedPTO.LoopVectorization = OptLevel >= 2;
  SharedPTO.SLPVectorization = OptLevel >= 2;
  SharedPTO.CallGraphProfile = false;
  SharedPTO.NevercFastIPO = true;

  // Phase 2: per-partition optimization + codegen.
  {
    unsigned ThreadCount =
        std::min(llvm::thread::hardware_concurrency(), Ctx.NumPartitions);
    std::atomic<unsigned> NextPart{0};
    std::vector<std::thread> Workers;
    Workers.reserve(ThreadCount);
    auto Worker = [&]() {
      while (true) {
        unsigned p = NextPart.fetch_add(1, std::memory_order_relaxed);
        if (p >= Ctx.NumPartitions)
          break;
        if (!Ctx.Parts[p])
          continue;
        auto &PP = *Ctx.Parts[p];
        auto &MPart = *PP.M;

        {
          LoopAnalysisManager LAM;
          FunctionAnalysisManager FAM;
          CGSCCAnalysisManager CGAM;
          ModuleAnalysisManager MAM;
          PassInstrumentationCallbacks PIC;
          StandardInstrumentations SI(MPart.getContext(), false, false);
          SI.registerCallbacks(PIC, &MAM);
          PassBuilder PB(PP.PTM.get(), SharedPTO, std::nullopt, &PIC);
          FAM.registerPass(
              [&] { return TargetLibraryAnalysis(*Ctx.SharedTLII); });
          PB.registerModuleAnalyses(MAM);
          PB.registerCGSCCAnalyses(CGAM);
          PB.registerFunctionAnalyses(FAM);
          PB.registerLoopAnalyses(LAM);
          PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

          ModulePassManager MPM;
          MPM.addPass(PB.buildModuleOptimizationPipeline(
              OL, ThinOrFullLTOPhase::FullLTOPostLink));
          MPM.run(MPart, MAM);
        }

        raw_svector_ostream ObjOS(*PP.ObjBuf);
        legacy::PassManager PM;
        PM.add(createTargetTransformInfoWrapperPass(
            PP.PTM->getTargetIRAnalysis()));
        PM.add(new TargetLibraryInfoWrapperPass(*Ctx.SharedTLII));
        if (!PP.PTM->addPassesToEmitFile(PM, ObjOS, nullptr,
                                         CodeGenFileType::ObjectFile, true)) {
          PM.run(MPart);
          Ctx.Results[p].Success = true;
        }
      }
    };
    for (unsigned i = 0; i < ThreadCount; ++i)
      Workers.emplace_back(Worker);
    for (auto &T : Workers)
      T.join();
  }

  return Ctx.finalizeResults(OS);
}

} // namespace neverc
