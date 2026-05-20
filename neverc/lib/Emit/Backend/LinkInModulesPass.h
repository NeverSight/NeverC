#ifndef NEVERC_LIB_EMIT_BACKEND_LINKINMODULESPASS_H
#define NEVERC_LIB_EMIT_BACKEND_LINKINMODULESPASS_H

#include "Backend/BackendConsumer.h"
#include "llvm/IR/PassManager.h"

namespace llvm {
class Module;

class LinkInModulesPass : public PassInfoMixin<LinkInModulesPass> {
  neverc::EmitterConsumer *BC;
  bool ShouldLinkFiles;

public:
  LinkInModulesPass(neverc::EmitterConsumer *BC, bool ShouldLinkFiles = true);

  PreservedAnalyses run(Module &M, AnalysisManager<Module> &);
  static bool isRequired() { return true; }
};

} // namespace llvm

#endif
