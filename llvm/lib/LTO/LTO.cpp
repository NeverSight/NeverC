//===-LTO.cpp - LLVM Link Time Optimizer ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements functions and classes used to support LTO.
//
//===----------------------------------------------------------------------===//

#include "llvm/LTO/LTO.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/CodeGen/Analysis.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/AutoUpgrade.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMRemarkStreamer.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Metadata.h"
#include "llvm/LTO/LTOBackend.h"
#include "llvm/Linker/IRMover.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/IRObjectFile.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/VCSRevision.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Transforms/IPO.h"
#include <optional>
#include <set>

using namespace llvm;
using namespace lto;
using namespace object;

#define DEBUG_TYPE "lto"

namespace llvm {
/// Enable global value internalization in LTO.
cl::opt<bool> EnableLTOInternalization(
    "enable-lto-internalization", cl::init(true), cl::Hidden,
    cl::desc("Enable global value internalization in LTO"));

} // namespace llvm

InputFile::~InputFile() = default;

Expected<std::unique_ptr<InputFile>> InputFile::create(MemoryBufferRef Object) {
  std::unique_ptr<InputFile> File(new InputFile);

  Expected<IRSymtabFile> FOrErr = readIRSymtab(Object);
  if (!FOrErr)
    return FOrErr.takeError();

  File->TargetTriple = FOrErr->TheReader.getTargetTriple();
  File->SourceFileName = FOrErr->TheReader.getSourceFileName();
  File->COFFLinkerOpts = FOrErr->TheReader.getCOFFLinkerOpts();
  File->DependentLibraries = FOrErr->TheReader.getDependentLibraries();
  File->ComdatTable = FOrErr->TheReader.getComdatTable();

  for (unsigned I = 0; I != FOrErr->Mods.size(); ++I) {
    size_t Begin = File->Symbols.size();
    for (const irsymtab::Reader::SymbolRef &Sym :
         FOrErr->TheReader.module_symbols(I))
      // Skip symbols that are irrelevant to LTO. Note that this condition needs
      // to match the one in Skip() in LTO::addRegularLTO().
      if (Sym.isGlobal() && !Sym.isFormatSpecific())
        File->Symbols.push_back(Sym);
    File->ModuleSymIndices.push_back({Begin, File->Symbols.size()});
  }

  File->Mods = FOrErr->Mods;
  File->Strtab = std::move(FOrErr->Strtab);
  return std::move(File);
}

StringRef InputFile::getName() const { return Mods[0].getModuleIdentifier(); }

BitcodeModule &InputFile::getSingleBitcodeModule() {
  assert(Mods.size() == 1 && "Expect only one bitcode module");
  return Mods[0];
}

LTO::RegularLTOState::RegularLTOState(unsigned ParallelCodeGenParallelismLevel,
                                      const Config &Conf)
    : ParallelCodeGenParallelismLevel(ParallelCodeGenParallelismLevel),
      Ctx(Conf), CombinedModule(std::make_unique<Module>("ld-temp.o", Ctx)),
      Mover(std::make_unique<IRMover>(*CombinedModule)) {}

LTO::LTO(Config Conf, unsigned ParallelCodeGenParallelismLevel, LTOKind LTOMode)
    : Conf(std::move(Conf)),
      RegularLTO(ParallelCodeGenParallelismLevel, this->Conf),
      LTOMode(LTOMode) {}

// Requires a destructor for MapVector<BitcodeModule>.
LTO::~LTO() = default;

// Add the symbols in the given module to the GlobalResolutions map, and resolve
// their partitions.
void LTO::addModuleToGlobalRes(ArrayRef<InputFile::Symbol> Syms,
                               ArrayRef<SymbolResolution> Res,
                               unsigned Partition, bool InSummary) {
  auto *ResI = Res.begin();
  auto *ResE = Res.end();
  (void)ResE;
  const Triple TT(RegularLTO.CombinedModule->getTargetTriple());
  for (const InputFile::Symbol &Sym : Syms) {
    assert(ResI != ResE);
    SymbolResolution Res = *ResI++;

    auto &GlobalRes = GlobalResolutions[Sym.getName()];
    GlobalRes.UnnamedAddr &= Sym.isUnnamedAddr();
    if (Res.Prevailing) {
      assert(!GlobalRes.Prevailing &&
             "Multiple prevailing defs are not allowed");
      GlobalRes.Prevailing = true;
      GlobalRes.IRName = std::string(Sym.getIRName());
    } else if (!GlobalRes.Prevailing && GlobalRes.IRName.empty()) {
      // Sometimes it can be two copies of symbol in a module and prevailing
      // symbol can have no IR name. That might happen if symbol is defined in
      // module level inline asm block. In case we have multiple modules with
      // the same symbol we want to use IR name of the prevailing symbol.
      // Otherwise, if we haven't seen a prevailing symbol, set the name so that
      // we can later use it to check if there is any prevailing copy in IR.
      GlobalRes.IRName = std::string(Sym.getIRName());
    }

    // In rare occasion, the symbol used to initialize GlobalRes has a different
    // IRName from the inspected Symbol. This can happen on macOS + iOS, when a
    // symbol is referenced through its mangled name, say @"\01_symbol" while
    // the IRName is @symbol (the prefix underscore comes from MachO mangling).
    // In that case, we have the same actual Symbol that can get two different
    // GUID, leading to some invalid internalization. Workaround this by marking
    // the GlobalRes external.

    // FIXME: instead of this check, it would be desirable to compute GUIDs
    // based on mangled name, but this requires an access to the Target Triple
    // and would be relatively invasive on the codebase.
    if (GlobalRes.IRName != Sym.getIRName())
      GlobalRes.Partition = GlobalResolution::External;

    // Set the partition to external if we know it is re-defined by the linker
    // with -defsym or -wrap options, used elsewhere, e.g. it is visible to a
    // regular object, is referenced from llvm.compiler.used/llvm.used, or was
    // already recorded as being referenced from a different partition.
    if (Res.LinkerRedefined || Res.VisibleToRegularObj || Sym.isUsed() ||
        (GlobalRes.Partition != GlobalResolution::Unknown &&
         GlobalRes.Partition != Partition)) {
      GlobalRes.Partition = GlobalResolution::External;
    } else
      // First recorded reference, save the current partition.
      GlobalRes.Partition = Partition;
  }
}

static void writeToResolutionFile(raw_ostream &OS, InputFile *Input,
                                  ArrayRef<SymbolResolution> Res) {
  StringRef Path = Input->getName();
  OS << Path << '\n';
  auto ResI = Res.begin();
  for (const InputFile::Symbol &Sym : Input->symbols()) {
    assert(ResI != Res.end());
    SymbolResolution Res = *ResI++;

    OS << "-r=" << Path << ',' << Sym.getName() << ',';
    if (Res.Prevailing)
      OS << 'p';
    if (Res.FinalDefinitionInLinkageUnit)
      OS << 'l';
    if (Res.VisibleToRegularObj)
      OS << 'x';
    if (Res.LinkerRedefined)
      OS << 'r';
    OS << '\n';
  }
  OS.flush();
  assert(ResI == Res.end());
}

Error LTO::add(std::unique_ptr<InputFile> Input,
               ArrayRef<SymbolResolution> Res) {
  assert(!CalledGetMaxTasks);

  if (Conf.ResolutionFile)
    writeToResolutionFile(*Conf.ResolutionFile, Input.get(), Res);

  if (RegularLTO.CombinedModule->getTargetTriple().empty())
    RegularLTO.CombinedModule->setTargetTriple(Input->getTargetTriple());

  const SymbolResolution *ResI = Res.begin();
  for (unsigned I = 0; I != Input->Mods.size(); ++I)
    if (Error Err = addModule(*Input, I, ResI, Res.end()))
      return Err;

  assert(ResI == Res.end());
  return Error::success();
}

Error LTO::addModule(InputFile &Input, unsigned ModI,
                     const SymbolResolution *&ResI,
                     const SymbolResolution *ResE) {
  Expected<BitcodeLTOInfo> LTOInfo = Input.Mods[ModI].getLTOInfo();
  if (!LTOInfo)
    return LTOInfo.takeError();

  if (EnableSplitLTOUnit) {
    // If only some modules were split, flag this in the index so that
    // we can skip or error on optimizations that need consistently split
    // modules (whole program devirt and lower type tests).
    if (*EnableSplitLTOUnit != LTOInfo->EnableSplitLTOUnit)
      CombinedIndex.setPartiallySplitLTOUnits();
  } else
    EnableSplitLTOUnit = LTOInfo->EnableSplitLTOUnit;

  BitcodeModule BM = Input.Mods[ModI];

  if ((LTOMode == LTOK_UnifiedRegular) && !LTOInfo->UnifiedLTO)
    return make_error<StringError>(
        "unified LTO compilation must use "
        "compatible bitcode modules (use -funified-lto)",
        inconvertibleErrorCode());

  auto ModSyms = Input.module_symbols(ModI);
  addModuleToGlobalRes(ModSyms, {ResI, ResE}, 0, LTOInfo->HasSummary);

  RegularLTO.EmptyCombinedModule = false;
  Expected<RegularLTOState::AddedModule> ModOrErr =
      addRegularLTO(BM, ModSyms, ResI, ResE);
  if (!ModOrErr)
    return ModOrErr.takeError();

  if (!LTOInfo->HasSummary)
    return linkRegularLTO(std::move(*ModOrErr), /*LivenessFromIndex=*/false);

  // Regular LTO module summaries are added to a dummy module that represents
  // the combined regular LTO module.
  if (Error Err = BM.readSummary(CombinedIndex, ""))
    return Err;
  RegularLTO.ModsWithSummaries.push_back(std::move(*ModOrErr));
  return Error::success();
}

// Checks whether the given global value is in a non-prevailing comdat
// (comdat containing values the linker indicated were not prevailing,
// which we then dropped to available_externally), and if so, removes
// it from the comdat. This is called for all global values to ensure the
// comdat is empty rather than leaving an incomplete comdat. It is needed for
// regular LTO modules, in case we are in a mixed-LTO mode (both regular
// and thin LTO modules) compilation. Since the regular LTO module will be
// linked first in the final native link, we want to make sure the linker
// doesn't select any of these incomplete comdats that would be left
// in the regular LTO module without this cleanup.
static void
handleNonPrevailingComdat(GlobalValue &GV,
                          std::set<const Comdat *> &NonPrevailingComdats) {
  Comdat *C = GV.getComdat();
  if (!C)
    return;

  if (!NonPrevailingComdats.count(C))
    return;

  // Additionally need to drop all global values from the comdat to
  // available_externally, to satisfy the COMDAT requirement that all members
  // are discarded as a unit. The non-local linkage global values avoid
  // duplicate definition linker errors.
  GV.setLinkage(GlobalValue::AvailableExternallyLinkage);

  if (auto GO = dyn_cast<GlobalObject>(&GV))
    GO->setComdat(nullptr);
}

// Add a regular LTO object to the link.
// The resulting module needs to be linked into the combined LTO module with
// linkRegularLTO.
Expected<LTO::RegularLTOState::AddedModule>
LTO::addRegularLTO(BitcodeModule BM, ArrayRef<InputFile::Symbol> Syms,
                   const SymbolResolution *&ResI,
                   const SymbolResolution *ResE) {
  RegularLTOState::AddedModule Mod;
  Expected<std::unique_ptr<Module>> MOrErr =
      BM.getLazyModule(RegularLTO.Ctx, /*ShouldLazyLoadMetadata*/ true,
                       /*IsImporting*/ false);
  if (!MOrErr)
    return MOrErr.takeError();
  Module &M = **MOrErr;
  Mod.M = std::move(*MOrErr);

  if (Error Err = M.materializeMetadata())
    return std::move(Err);

  // If cfi.functions is present and we are in regular LTO mode, LowerTypeTests
  // will rename local functions in the merged module as "<function name>.1".
  // This causes linking errors, since other parts of the module expect the
  // original function name.
  if (LTOMode == LTOK_UnifiedRegular)
    if (NamedMDNode *CfiFunctionsMD = M.getNamedMetadata("cfi.functions"))
      M.eraseNamedMetadata(CfiFunctionsMD);

  UpgradeDebugInfo(M);

  ModuleSymbolTable SymTab;
  SymTab.addModule(&M);

  for (GlobalVariable &GV : M.globals())
    if (GV.hasAppendingLinkage())
      Mod.Keep.push_back(&GV);

  DenseSet<GlobalObject *> AliasedGlobals;
  for (auto &GA : M.aliases())
    if (GlobalObject *GO = GA.getAliaseeObject())
      AliasedGlobals.insert(GO);

  // In this function we need IR GlobalValues matching the symbols in Syms
  // (which is not backed by a module), so we need to enumerate them in the same
  // order. The symbol enumeration order of a ModuleSymbolTable intentionally
  // matches the order of an irsymtab, but when we read the irsymtab in
  // InputFile::create we omit some symbols that are irrelevant to LTO. The
  // Skip() function skips the same symbols from the module as InputFile does
  // from the symbol table.
  auto MsymI = SymTab.symbols().begin(), MsymE = SymTab.symbols().end();
  auto Skip = [&]() {
    while (MsymI != MsymE) {
      auto Flags = SymTab.getSymbolFlags(*MsymI);
      if ((Flags & object::BasicSymbolRef::SF_Global) &&
          !(Flags & object::BasicSymbolRef::SF_FormatSpecific))
        return;
      ++MsymI;
    }
  };
  Skip();

  std::set<const Comdat *> NonPrevailingComdats;
  SmallSet<StringRef, 2> NonPrevailingAsmSymbols;
  for (const InputFile::Symbol &Sym : Syms) {
    assert(ResI != ResE);
    SymbolResolution Res = *ResI++;

    assert(MsymI != MsymE);
    ModuleSymbolTable::Symbol Msym = *MsymI++;
    Skip();

    if (GlobalValue *GV = dyn_cast_if_present<GlobalValue *>(Msym)) {
      if (Res.Prevailing) {
        if (Sym.isUndefined())
          continue;
        Mod.Keep.push_back(GV);
        // For symbols re-defined with linker -wrap and -defsym options,
        // set the linkage to weak to inhibit IPO. The linkage will be
        // restored by the linker.
        if (Res.LinkerRedefined)
          GV->setLinkage(GlobalValue::WeakAnyLinkage);

        GlobalValue::LinkageTypes OriginalLinkage = GV->getLinkage();
        if (GlobalValue::isLinkOnceLinkage(OriginalLinkage))
          GV->setLinkage(GlobalValue::getWeakLinkage(
              GlobalValue::isLinkOnceODRLinkage(OriginalLinkage)));
      } else if (isa<GlobalObject>(GV) &&
                 (GV->hasLinkOnceODRLinkage() || GV->hasWeakODRLinkage() ||
                  GV->hasAvailableExternallyLinkage()) &&
                 !AliasedGlobals.count(cast<GlobalObject>(GV))) {
        // Any of the above three types of linkage indicates that the
        // chosen prevailing symbol will have the same semantics as this copy of
        // the symbol, so we may be able to link it with available_externally
        // linkage. We will decide later whether to do that when we link this
        // module (in linkRegularLTO), based on whether it is undefined.
        Mod.Keep.push_back(GV);
        GV->setLinkage(GlobalValue::AvailableExternallyLinkage);
        if (GV->hasComdat())
          NonPrevailingComdats.insert(GV->getComdat());
        cast<GlobalObject>(GV)->setComdat(nullptr);
      }

      // Set the 'local' flag based on the linker resolution for this symbol.
      if (Res.FinalDefinitionInLinkageUnit) {
        GV->setDSOLocal(true);
        if (GV->hasDLLImportStorageClass())
          GV->setDLLStorageClass(
              GlobalValue::DLLStorageClassTypes::DefaultStorageClass);
      }
    } else if (auto *AS =
                   dyn_cast_if_present<ModuleSymbolTable::AsmSymbol *>(Msym)) {
      // Collect non-prevailing symbols.
      if (!Res.Prevailing)
        NonPrevailingAsmSymbols.insert(AS->first);
    } else {
      llvm_unreachable("unknown symbol type");
    }

    // Common resolution: collect the maximum size/alignment over all commons.
    // We also record if we see an instance of a common as prevailing, so that
    // if none is prevailing we can ignore it later.
    if (Sym.isCommon()) {
      // FIXME: We should figure out what to do about commons defined by asm.
      // For now they aren't reported correctly by ModuleSymbolTable.
      auto &CommonRes = RegularLTO.Commons[std::string(Sym.getIRName())];
      CommonRes.Size = std::max(CommonRes.Size, Sym.getCommonSize());
      if (uint32_t SymAlignValue = Sym.getCommonAlignment()) {
        CommonRes.Alignment =
            std::max(Align(SymAlignValue), CommonRes.Alignment);
      }
      CommonRes.Prevailing |= Res.Prevailing;
    }
  }

  if (!M.getComdatSymbolTable().empty())
    for (GlobalValue &GV : M.global_values())
      handleNonPrevailingComdat(GV, NonPrevailingComdats);

  // Prepend ".lto_discard <sym>, <sym>*" directive to each module inline asm
  // block.
  if (!M.getModuleInlineAsm().empty()) {
    std::string NewIA = ".lto_discard";
    if (!NonPrevailingAsmSymbols.empty()) {
      // Don't dicard a symbol if there is a live .symver for it.
      ModuleSymbolTable::CollectAsmSymvers(
          M, [&](StringRef Name, StringRef Alias) {
            if (!NonPrevailingAsmSymbols.count(Alias))
              NonPrevailingAsmSymbols.erase(Name);
          });
      NewIA += " " + llvm::join(NonPrevailingAsmSymbols, ", ");
    }
    NewIA += "\n";
    M.setModuleInlineAsm(NewIA + M.getModuleInlineAsm());
  }

  assert(MsymI == MsymE);
  return std::move(Mod);
}

Error LTO::linkRegularLTO(RegularLTOState::AddedModule Mod,
                          bool LivenessFromIndex) {
  std::vector<GlobalValue *> Keep;
  for (GlobalValue *GV : Mod.Keep) {
    if (LivenessFromIndex && !CombinedIndex.isGUIDLive(GV->getGUID())) {
      if (Function *F = dyn_cast<Function>(GV)) {
        if (DiagnosticOutputFile) {
          if (Error Err = F->materialize())
            return Err;
          OptimizationRemarkEmitter ORE(F, nullptr);
          ORE.emit(OptimizationRemark(DEBUG_TYPE, "deadfunction", F)
                   << ore::NV("Function", F)
                   << " not added to the combined module ");
        }
      }
      continue;
    }

    if (!GV->hasAvailableExternallyLinkage()) {
      Keep.push_back(GV);
      continue;
    }

    // Only link available_externally definitions if we don't already have a
    // definition.
    GlobalValue *CombinedGV =
        RegularLTO.CombinedModule->getNamedValue(GV->getName());
    if (CombinedGV && !CombinedGV->isDeclaration())
      continue;

    Keep.push_back(GV);
  }

  return RegularLTO.Mover->move(std::move(Mod.M), Keep, nullptr,
                                /* IsPerformingImport */ false);
}

unsigned LTO::getMaxTasks() const {
  CalledGetMaxTasks = true;
  return RegularLTO.ParallelCodeGenParallelismLevel;
}

// If only some of the modules were split, we cannot correctly handle
// code that contains type tests or type checked loads.
Error LTO::checkPartiallySplit() {
  if (!CombinedIndex.partiallySplitLTOUnits())
    return Error::success();

  Function *TypeTestFunc = RegularLTO.CombinedModule->getFunction(
      Intrinsic::getName(Intrinsic::type_test));
  Function *TypeCheckedLoadFunc = RegularLTO.CombinedModule->getFunction(
      Intrinsic::getName(Intrinsic::type_checked_load));
  Function *TypeCheckedLoadRelativeFunc =
      RegularLTO.CombinedModule->getFunction(
          Intrinsic::getName(Intrinsic::type_checked_load_relative));

  // First check if there are type tests / type checked loads in the
  // merged regular LTO module IR.
  if ((TypeTestFunc && !TypeTestFunc->use_empty()) ||
      (TypeCheckedLoadFunc && !TypeCheckedLoadFunc->use_empty()) ||
      (TypeCheckedLoadRelativeFunc &&
       !TypeCheckedLoadRelativeFunc->use_empty()))
    return make_error<StringError>(
        "inconsistent LTO Unit splitting (recompile with -fsplit-lto-unit)",
        inconvertibleErrorCode());

  // Otherwise check if there are any recorded in the combined summary.
  for (auto &P : CombinedIndex) {
    for (auto &S : P.second.SummaryList) {
      auto *FS = dyn_cast<FunctionSummary>(S.get());
      if (!FS)
        continue;
      if (!FS->type_test_assume_vcalls().empty() ||
          !FS->type_checked_load_vcalls().empty() ||
          !FS->type_test_assume_const_vcalls().empty() ||
          !FS->type_checked_load_const_vcalls().empty() ||
          !FS->type_tests().empty())
        return make_error<StringError>(
            "inconsistent LTO Unit splitting (recompile with -fsplit-lto-unit)",
            inconvertibleErrorCode());
    }
  }
  return Error::success();
}

Error LTO::run(AddStreamFn AddStream, FileCache Cache) {
  // Setup output file to emit statistics.
  auto StatsFileOrErr = setupStatsFile(Conf.StatsFile);
  if (!StatsFileOrErr)
    return StatsFileOrErr.takeError();
  std::unique_ptr<ToolOutputFile> StatsFile = std::move(StatsFileOrErr.get());

  // TODO: Ideally this would be controlled automatically by detecting that we
  // are linking with an allocator that supports these interfaces, rather than
  // an internal option (which would still be needed for tests, however). For
  // example, if the library exported a symbol like __malloc_hot_cold the linker
  // could recognize that and set a flag in the lto::Config.

  Error Result = runRegularLTO(AddStream);

  if (StatsFile)
    PrintStatisticsJSON(StatsFile->os());

  return Result;
}

Error LTO::runRegularLTO(AddStreamFn AddStream) {
  // Setup optimization remarks.
  auto DiagFileOrErr = lto::setupLLVMOptimizationRemarks(
      RegularLTO.CombinedModule->getContext(), Conf.RemarksFilename,
      Conf.RemarksPasses, Conf.RemarksFormat, Conf.RemarksWithHotness,
      Conf.RemarksHotnessThreshold);
  LLVM_DEBUG(dbgs() << "Running regular LTO\n");
  if (!DiagFileOrErr)
    return DiagFileOrErr.takeError();
  DiagnosticOutputFile = std::move(*DiagFileOrErr);

  // Finalize linking of regular LTO modules containing summaries now that
  // we have computed liveness information.
  for (auto &M : RegularLTO.ModsWithSummaries)
    if (Error Err = linkRegularLTO(std::move(M),
                                   /*LivenessFromIndex=*/true))
      return Err;

  // Ensure we don't have inconsistently split LTO units with type tests.
  if (Error Err = checkPartiallySplit())
    return Err;

  // Make sure commons have the right size/alignment: we kept the largest from
  // all the prevailing when adding the inputs, and we apply it here.
  const DataLayout &DL = RegularLTO.CombinedModule->getDataLayout();
  for (auto &I : RegularLTO.Commons) {
    if (!I.second.Prevailing)
      // Don't do anything if no instance of this common was prevailing.
      continue;
    GlobalVariable *OldGV = RegularLTO.CombinedModule->getNamedGlobal(I.first);
    if (OldGV && DL.getTypeAllocSize(OldGV->getValueType()) == I.second.Size) {
      // Don't create a new global if the type is already correct, just make
      // sure the alignment is correct.
      OldGV->setAlignment(I.second.Alignment);
      continue;
    }
    ArrayType *Ty =
        ArrayType::get(Type::getInt8Ty(RegularLTO.Ctx), I.second.Size);
    auto *GV = new GlobalVariable(*RegularLTO.CombinedModule, Ty, false,
                                  GlobalValue::CommonLinkage,
                                  ConstantAggregateZero::get(Ty), "");
    GV->setAlignment(I.second.Alignment);
    if (OldGV) {
      OldGV->replaceAllUsesWith(GV);
      GV->takeName(OldGV);
      OldGV->eraseFromParent();
    } else {
      GV->setName(I.first);
    }
  }

  // C-only compiler: no whole-program devirtualization (no C++ vtables); skip
  // updateVCallVisibilityInModule / updatePublicTypeTestCalls (both no-op for
  // IR without llvm.public.type.test / !vcall_visibility metadata).

  if (Conf.PreOptModuleHook &&
      !Conf.PreOptModuleHook(0, *RegularLTO.CombinedModule))
    return finalizeOptimizationRemarks(std::move(DiagnosticOutputFile));

  if (!Conf.CodeGenOnly) {
    for (const auto &R : GlobalResolutions) {
      GlobalValue *GV =
          RegularLTO.CombinedModule->getNamedValue(R.second.IRName);
      if (!R.second.isPrevailingIRSymbol())
        continue;
      if (R.second.Partition != 0 &&
          R.second.Partition != GlobalResolution::External)
        continue;

      // Ignore symbols defined in other partitions.
      // Also skip declarations, which are not allowed to have internal linkage.
      if (!GV || GV->hasLocalLinkage() || GV->isDeclaration())
        continue;

      // Symbols that are marked DLLImport or DLLExport should not be
      // internalized, as they are either externally visible or referencing
      // external symbols. Symbols that have AvailableExternally or Appending
      // linkage might be used by future passes and should be kept as is.
      // These linkages are seen in Unified regular LTO, because the process
      // of creating split LTO units introduces symbols with that linkage into
      // one of the created modules.
      if ((LTOMode == LTOKind::LTOK_UnifiedRegular) &&
          ((GV->getDLLStorageClass() != GlobalValue::DefaultStorageClass) ||
           GV->hasAvailableExternallyLinkage() || GV->hasAppendingLinkage()))
        continue;

      GV->setUnnamedAddr(R.second.UnnamedAddr ? GlobalValue::UnnamedAddr::Global
                                              : GlobalValue::UnnamedAddr::None);
      if (EnableLTOInternalization && R.second.Partition == 0)
        GV->setLinkage(GlobalValue::InternalLinkage);
    }

    if (Conf.PostInternalizeModuleHook &&
        !Conf.PostInternalizeModuleHook(0, *RegularLTO.CombinedModule))
      return finalizeOptimizationRemarks(std::move(DiagnosticOutputFile));
  }

  if (!RegularLTO.EmptyCombinedModule || Conf.AlwaysEmitRegularLTOObj) {
    if (Error Err =
            backend(Conf, AddStream, RegularLTO.ParallelCodeGenParallelismLevel,
                    *RegularLTO.CombinedModule, CombinedIndex))
      return Err;
  }

  return finalizeOptimizationRemarks(std::move(DiagnosticOutputFile));
}

static const char *libcallRoutineNames[] = {
#define HANDLE_LIBCALL(code, name) name,
#include "llvm/IR/RuntimeLibcalls.def"
#undef HANDLE_LIBCALL
};

ArrayRef<const char *> LTO::getRuntimeLibcallSymbols() {
  return ArrayRef(libcallRoutineNames);
}

Expected<std::unique_ptr<ToolOutputFile>> lto::setupLLVMOptimizationRemarks(
    LLVMContext &Context, StringRef RemarksFilename, StringRef RemarksPasses,
    StringRef RemarksFormat, bool RemarksWithHotness,
    std::optional<uint64_t> RemarksHotnessThreshold, int Count) {
  std::string Filename = std::string(RemarksFilename);
  if (!Filename.empty() && Count != -1)
    Filename =
        (Twine(Filename) + "." + llvm::utostr(Count) + "." + RemarksFormat)
            .str();

  auto ResultOrErr = llvm::setupLLVMOptimizationRemarks(
      Context, Filename, RemarksPasses, RemarksFormat, RemarksWithHotness,
      RemarksHotnessThreshold);
  if (Error E = ResultOrErr.takeError())
    return std::move(E);

  if (*ResultOrErr)
    (*ResultOrErr)->keep();

  return ResultOrErr;
}

Expected<std::unique_ptr<ToolOutputFile>>
lto::setupStatsFile(StringRef StatsFilename) {
  if (StatsFilename.empty())
    return nullptr;

  llvm::EnableStatistics(false);
  std::error_code EC;
  auto StatsFile =
      std::make_unique<ToolOutputFile>(StatsFilename, EC, sys::fs::OF_None);
  if (EC)
    return errorCodeToError(EC);

  StatsFile->keep();
  return std::move(StatsFile);
}
