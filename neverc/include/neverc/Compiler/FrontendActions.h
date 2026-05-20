#ifndef NEVERC_COMPILER_FRONTENDACTIONS_H
#define NEVERC_COMPILER_FRONTENDACTIONS_H

#include "neverc/Compiler/FrontendAction.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>
#include <string>

namespace neverc {

using llvm::raw_pwrite_stream;

//===----------------------------------------------------------------------===//
// AST Consumer Actions
//===----------------------------------------------------------------------===//

class SyntaxOnlyAction : public ASTFrontendAction {
protected:
  std::unique_ptr<TreeConsumer>
  CreateTreeConsumer(CompilerInstance &CI, llvm::StringRef InFile) override;

public:
  ~SyntaxOnlyAction() override;
};

//===----------------------------------------------------------------------===//
// Preprocessing actions
//===----------------------------------------------------------------------===//

class PreprocessOnlyAction : public PreprocessorFrontendAction {
protected:
  void ExecuteAction() override;
};

class PrintPreprocessedAction : public PreprocessorFrontendAction {
protected:
  void ExecuteAction() override;
};

} // end namespace neverc

#endif
