#ifndef NEVERC_EMIT_CORE_EMITTERACTION_H
#define NEVERC_EMIT_CORE_EMITTERACTION_H

#include "neverc/Compiler/FrontendAction.h"
#include <memory>

namespace llvm {
class LLVMContext;
class Module;
} // namespace llvm

namespace neverc {
class EmitterConsumer;
class IRGenerator;

class EmitterAction : public ASTFrontendAction {
private:
  // Let EmitterConsumer access LinkModule.
  friend class EmitterConsumer;

  struct LinkModule {
    /// The module to link in.
    std::unique_ptr<llvm::Module> Module;

    /// If true, we set attributes on Module's functions according to our
    /// CodeGenOptions and LangOptions, as though we were generating the
    /// function ourselves.
    bool PropagateAttrs;

    /// If true, we use LLVM module internalizer.
    bool Internalize;

    /// Bitwise combination of llvm::LinkerFlags used when we link the module.
    unsigned LinkFlags;
  };

  unsigned Act;
  std::unique_ptr<llvm::Module> TheModule;

  llvm::SmallVector<LinkModule, 4> LinkModules;
  llvm::LLVMContext *VMContext;
  bool OwnsVMContext;

  std::unique_ptr<llvm::Module> importModule(llvm::MemoryBufferRef MBRef);

  bool loadLinkModules(CompilerInstance &CI);

protected:
  EmitterAction(unsigned _Act, llvm::LLVMContext *_VMContext = nullptr);

  bool hasIRSupport() const override;

  std::unique_ptr<TreeConsumer>
  CreateTreeConsumer(CompilerInstance &CI, llvm::StringRef InFile) override;

  void ExecuteAction() override;

  void EndSourceFileAction() override;

public:
  ~EmitterAction() override;

  std::unique_ptr<llvm::Module> takeModule();

  llvm::LLVMContext *takeLLVMContext();

  IRGenerator *getCodeGenerator() const;

  EmitterConsumer *BEConsumer = nullptr;
};

class GenAssemblyAction : public EmitterAction {
  virtual void anchor();

public:
  GenAssemblyAction(llvm::LLVMContext *_VMContext = nullptr);
};

class GenBCAction : public EmitterAction {
  virtual void anchor();

public:
  GenBCAction(llvm::LLVMContext *_VMContext = nullptr);
};

class GenLLVMAction : public EmitterAction {
  virtual void anchor();

public:
  GenLLVMAction(llvm::LLVMContext *_VMContext = nullptr);
};

class GenObjAction : public EmitterAction {
  virtual void anchor();

public:
  GenObjAction(llvm::LLVMContext *_VMContext = nullptr);
};

} // namespace neverc

#endif
