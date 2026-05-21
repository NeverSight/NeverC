#include "Backend/MimallocRuntimeLinker.h"
#include "Backend/RuntimeLinkerUtils.h"
#include "neverc/Foundation/Builtin/BuiltinMimalloc.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Module.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

using namespace llvm;
using namespace neverc;

namespace {

// malloc/free family symbols that mimalloc overrides.  These retain
// external linkage so the system linker picks them up; everything
// else becomes internal.
bool isMallocOverrideSymbol(StringRef Name) {
  // C allocation API that mimalloc overrides via MI_OVERRIDE.
  // NeverC is a pure C compiler — no C++ operator new/delete needed.
  return Name == "malloc" || Name == "free" || Name == "calloc" ||
         Name == "realloc" || Name == "posix_memalign" ||
         Name == "aligned_alloc" || Name == "memalign" ||
         Name == "valloc" || Name == "pvalloc" ||
         Name == "reallocf" || Name == "malloc_size" ||
         Name == "malloc_usable_size" || Name == "malloc_good_size" ||
         Name == "_malloc_default_zone" ||
         // mimalloc public API (mi_malloc, mi_free, etc.)
         Name.starts_with("mi_");
}

} // namespace

PreservedAnalyses
MimallocRuntimeLinkerPass::run(Module &M, ModuleAnalysisManager &) {
  Triple TT(M.getTargetTriple());
  StringRef Embedded = BuiltinMimalloc::getEmbeddedBitcode(TT.getOS());
  if (Embedded.empty())
    return PreservedAnalyses::all();

  auto Buf = MemoryBuffer::getMemBuffer(
      Embedded, "neverc_mimalloc_runtime", /*RequiresNullTerminator=*/false);

  auto ExpectedMod = parseBitcodeFile(Buf->getMemBufferRef(), M.getContext());
  if (!ExpectedMod)
    report_fatal_error(Twine("Failed to parse neverc mimalloc runtime: ") +
                       toString(ExpectedMod.takeError()));
  auto MimallocMod = std::move(*ExpectedMod);

  // Strip host-specific attributes from the precompiled bitcode so
  // functions inherit the user module's target defaults after merge.
  stripHostTargetAttributes(*MimallocMod);

  // Capture names of all mimalloc definitions before the merge.
  // Must own the strings: linkModules destroys the source Module,
  // invalidating any StringRef into its ValueSymbolTable.
  StringSet<> MimallocFnNames;
  StringSet<> MimallocGlobalNames;
  for (const Function &F : *MimallocMod)
    if (!F.isDeclaration())
      MimallocFnNames.insert(F.getName());
  for (const GlobalVariable &GV : MimallocMod->globals())
    if (!GV.isDeclaration())
      MimallocGlobalNames.insert(GV.getName());

  // Align module metadata with the user module.
  MimallocMod->setDataLayout(M.getDataLayout());
  MimallocMod->setTargetTriple(M.getTargetTriple());

  if (auto *Flags = MimallocMod->getModuleFlagsMetadata())
    Flags->clearOperands();

  // Whole-archive merge — all mimalloc symbols are linked in.
  if (Linker::linkModules(M, std::move(MimallocMod),
                          Linker::Flags::OverrideFromSrc)) {
    report_fatal_error("Failed to link neverc mimalloc runtime");
  }

  // Post-merge: internalize helper functions, keep override entry
  // points external so the system linker resolves them.
  for (Function &F : M) {
    if (!MimallocFnNames.count(F.getName()))
      continue;
    if (!isMallocOverrideSymbol(F.getName()))
      F.setLinkage(GlobalValue::InternalLinkage);
  }

  for (GlobalVariable &GV : M.globals()) {
    if (!GV.isDeclaration() && MimallocGlobalNames.count(GV.getName()))
      GV.setLinkage(GlobalValue::InternalLinkage);
  }

  // Clean llvm.used / llvm.compiler.used of internalized mimalloc entries.
  removeFromUsedLists(M, [&](Constant *C) {
    auto *GV = dyn_cast<GlobalValue>(C->stripPointerCasts());
    if (!GV)
      return isa<PoisonValue>(C) || isa<UndefValue>(C);
    if (auto *F = dyn_cast<Function>(GV))
      return MimallocFnNames.count(F->getName()) != 0 &&
             !isMallocOverrideSymbol(F->getName());
    if (auto *GVar = dyn_cast<GlobalVariable>(GV))
      return MimallocGlobalNames.count(GVar->getName()) != 0;
    return false;
  });

  return PreservedAnalyses::none();
}
