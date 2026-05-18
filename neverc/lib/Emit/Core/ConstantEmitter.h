#ifndef NEVERC_LIB_CODEGEN_CORE_CONSTANTEMITTER_H
#define NEVERC_LIB_CODEGEN_CORE_CONSTANTEMITTER_H

#include "Core/FunctionEmitter.h"
#include "Core/ModuleEmitter.h"

namespace neverc {
namespace Emit {

class ConstantEmitter {
public:
  ModuleEmitter &ME;
  FunctionEmitter *const FE;

private:
  bool Abstract = false;

  bool InitializedNonAbstract = false;

  bool Finalized = false;

  bool Failed = false;

  bool InConstantContext = false;

  LangAS DestAddressSpace = LangAS::Default;

  llvm::SmallVector<std::pair<llvm::Constant *, llvm::GlobalVariable *>, 4>
      PlaceholderAddresses;

public:
  ConstantEmitter(ModuleEmitter &ME, FunctionEmitter *FE = nullptr)
      : ME(ME), FE(FE) {}

  ConstantEmitter(FunctionEmitter &FE) : ME(FE.ME), FE(&FE) {}

  ConstantEmitter(const ConstantEmitter &other) = delete;
  ConstantEmitter &operator=(const ConstantEmitter &other) = delete;

  ~ConstantEmitter();

  bool isAbstract() const { return Abstract; }

  bool isInConstantContext() const { return InConstantContext; }
  void setInConstantContext(bool var) { InConstantContext = var; }

  llvm::Constant *tryEmitForInitializer(const VarDecl &D);
  llvm::Constant *tryEmitForInitializer(const Expr *E, LangAS destAddrSpace,
                                        QualType destType);
  llvm::Constant *emitForInitializer(const APValue &value, LangAS destAddrSpace,
                                     QualType destType);

  void finalize(llvm::GlobalVariable *global);

  // All of the "abstract" emission methods below permit the emission to
  // be immediately discarded without finalizing anything.  Therefore, they
  // must also promise not to do anything that will, in the future, require
  // finalization:
  //
  //   - using the FE (if present) for anything other than establishing
  //     semantic context; for example, an expression with ignored
  //     side-effects must not be emitted as an abstract expression
  //
  //   - doing anything that would not be safe to duplicate within an
  //     initializer or to propagate to another context; for example,
  //     side effects, or emitting an initialization that requires a
  //     reference to its current location.

  llvm::Constant *tryEmitAbstractForInitializer(const VarDecl &D);

  llvm::Constant *emitAbstract(const Expr *E, QualType T);
  llvm::Constant *emitAbstract(SourceLocation loc, const APValue &value,
                               QualType T);

  llvm::Constant *tryEmitAbstract(const Expr *E, QualType T);
  llvm::Constant *tryEmitAbstractForMemory(const Expr *E, QualType T);

  llvm::Constant *tryEmitAbstract(const APValue &value, QualType T);
  llvm::Constant *tryEmitAbstractForMemory(const APValue &value, QualType T);

  llvm::Constant *tryEmitConstantExpr(const ConstantExpr *CE);

  llvm::Constant *emitNullForMemory(QualType T) {
    return emitNullForMemory(ME, T);
  }
  llvm::Constant *emitForMemory(llvm::Constant *C, QualType T) {
    return emitForMemory(ME, C, T);
  }

  static llvm::Constant *emitNullForMemory(ModuleEmitter &ME, QualType T);
  static llvm::Constant *emitForMemory(ModuleEmitter &ME, llvm::Constant *C,
                                       QualType T);

  // These are private helper routines of the constant emitter that
  // can't actually be private because things are split out into helper
  // functions and classes.

  llvm::Constant *tryEmitPrivateForVarInit(const VarDecl &D);

  llvm::Constant *tryEmitPrivate(const Expr *E, QualType T);
  llvm::Constant *tryEmitPrivateForMemory(const Expr *E, QualType T);

  llvm::Constant *tryEmitPrivate(const APValue &value, QualType T);
  llvm::Constant *tryEmitPrivateForMemory(const APValue &value, QualType T);

  llvm::GlobalValue *getCurrentAddrPrivate();

  void registerCurrentAddrPrivate(llvm::Constant *signal,
                                  llvm::GlobalValue *placeholder);

private:
  void initializeNonAbstract(LangAS destAS) {
    assert(!InitializedNonAbstract);
    InitializedNonAbstract = true;
    DestAddressSpace = destAS;
  }
  llvm::Constant *markIfFailed(llvm::Constant *init) {
    if (!init)
      Failed = true;
    return init;
  }

  struct AbstractState {
    bool OldValue;
    size_t OldPlaceholdersSize;
  };
  AbstractState pushAbstract() {
    AbstractState saved = {Abstract, PlaceholderAddresses.size()};
    Abstract = true;
    return saved;
  }
  llvm::Constant *validateAndPopAbstract(llvm::Constant *C, AbstractState save);
};

} // namespace Emit
} // namespace neverc

#endif
