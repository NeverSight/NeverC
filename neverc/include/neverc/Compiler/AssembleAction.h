#ifndef NEVERC_COMPILER_ASSEMBLEACTION_H
#define NEVERC_COMPILER_ASSEMBLEACTION_H

#include "neverc/Compiler/FrontendAction.h"

namespace neverc {

class AssembleAction final : public PreprocessorFrontendAction {
protected:
  void ExecuteAction() override;
};

} // namespace neverc

#endif
