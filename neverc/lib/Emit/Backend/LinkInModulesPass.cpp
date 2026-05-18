#include "Backend/LinkInModulesPass.h"
#include "Backend/BackendConsumer.h"

using namespace llvm;

LinkInModulesPass::LinkInModulesPass(neverc::EmitterConsumer *BC,
                                     bool ShouldLinkFiles)
    : BC(BC), ShouldLinkFiles(ShouldLinkFiles) {}

PreservedAnalyses LinkInModulesPass::run(Module &M, ModuleAnalysisManager &AM) {

  if (BC && BC->LinkInModules(&M, ShouldLinkFiles))
    report_fatal_error("Bitcode module linking failed, compilation aborted!");

  return PreservedAnalyses::all();
}
