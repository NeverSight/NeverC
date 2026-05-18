#ifndef NEVERC_FRONTEND_FRONTENDACTION_H
#define NEVERC_FRONTEND_FRONTENDACTION_H

#include "neverc/Compiler/FrontendOptions.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Tree/Core/TreeConsumer.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <memory>
#include <string>
#include <vector>

namespace neverc {
class CompilerInstance;

class FrontendAction {
  FrontendInputFile CurrentInput;
  CompilerInstance *Instance;

private:
  std::unique_ptr<TreeConsumer> CreateWrappedConsumer(CompilerInstance &CI,
                                                      llvm::StringRef InFile);

protected:
  virtual bool PrepareToExecuteAction(CompilerInstance &CI) { return true; }

  virtual std::unique_ptr<TreeConsumer>
  CreateTreeConsumer(CompilerInstance &CI, llvm::StringRef InFile) = 0;

  virtual bool BeginInvocation(CompilerInstance &CI) { return true; }

  virtual bool BeginSourceFileAction(CompilerInstance &CI) { return true; }

  virtual void ExecuteAction() = 0;

  virtual void EndSourceFileAction() {}

  virtual bool shouldEraseOutputFiles();

public:
  FrontendAction();
  virtual ~FrontendAction();

  CompilerInstance &getCompilerInstance() const {
    assert(Instance && "Compiler instance not registered!");
    return *Instance;
  }

  void setCompilerInstance(CompilerInstance *Value) { Instance = Value; }

  const FrontendInputFile &getCurrentInput() const { return CurrentInput; }

  llvm::StringRef getCurrentFile() const {
    assert(!CurrentInput.isEmpty() && "No current file!");
    return CurrentInput.getFile();
  }

  llvm::StringRef getCurrentFileOrBufferName() const {
    assert(!CurrentInput.isEmpty() && "No current file!");
    return CurrentInput.isFile()
               ? CurrentInput.getFile()
               : CurrentInput.getBuffer().getBufferIdentifier();
  }

  InputKind getCurrentFileKind() const {
    assert(!CurrentInput.isEmpty() && "No current file!");
    return CurrentInput.getKind();
  }

  void setCurrentInput(const FrontendInputFile &CurrentInput);

  virtual bool usesPreprocessorOnly() const = 0;

  virtual bool hasIRSupport() const { return false; }

  bool PrepareToExecute(CompilerInstance &CI) {
    return PrepareToExecuteAction(CI);
  }

  bool BeginSourceFile(CompilerInstance &CI, const FrontendInputFile &Input);

  llvm::Error Execute();

  virtual void EndSourceFile();
};

class ASTFrontendAction : public FrontendAction {
protected:
  void ExecuteAction() override;

public:
  ASTFrontendAction() {}
  bool usesPreprocessorOnly() const override { return false; }
};

class PreprocessorFrontendAction : public FrontendAction {
protected:
  std::unique_ptr<TreeConsumer>
  CreateTreeConsumer(CompilerInstance &CI, llvm::StringRef InFile) override;

public:
  bool usesPreprocessorOnly() const override { return true; }
};

} // end namespace neverc

#endif
