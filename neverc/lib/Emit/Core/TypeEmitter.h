#ifndef NEVERC_LIB_EMIT_CORE_TYPEEMITTER_H
#define NEVERC_LIB_EMIT_CORE_TYPEEMITTER_H

#include "Stmt/CallEmitterInfo.h"
#include "neverc/Emit/ABI/ABIFunctionInfo.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/Module.h"
#include <array>

namespace llvm {
class FunctionType;
class DataLayout;
class Type;
class LLVMContext;
class StructType;
} // namespace llvm

namespace neverc {
class TreeContext;
template <typename> class CanQual;
class CodeGenOptions;
class FunctionProtoType;
class QualType;
class RecordDecl;
class TagDecl;
class TargetInfo;
class Type;
typedef CanQual<Type> CanQualType;
class GlobalDecl;

namespace Emit {
class ABIInfo;
class CGABI;
class RecordLayoutInfo;
class ModuleEmitter;
class RequiredArgs;

class TypeEmitter {
  ModuleEmitter &ME;
  // Some of this stuff should probably be left on the ME.
  TreeContext &Context;
  llvm::Module &TheModule;
  const TargetInfo &Target;
  CGABI &TheABI;

  // This should not be moved earlier, since its initialization depends on some
  // of the previous reference members being already initialized
  const ABIInfo &TheABIInfo;

  llvm::DenseMap<const Type *, std::unique_ptr<RecordLayoutInfo>>
      RecordLayoutInfos;

  llvm::DenseMap<const Type *, llvm::StructType *> RecordDeclTypes;

  llvm::FoldingSet<ABIFunctionInfo> FunctionInfos{FunctionInfosLog2InitSize};

  llvm::SmallPtrSet<const ABIFunctionInfo *, 4> FunctionsBeingProcessed;

  bool SkippedLayout;

  bool LongDoubleReferenced;

  llvm::DenseMap<const Type *, llvm::Type *> TypeCache;

  static constexpr unsigned InlineCacheSize = 8;
  static constexpr unsigned InlineCacheSets = InlineCacheSize / 2;
  mutable std::array<const Type *, InlineCacheSize> ICacheKeys{};
  mutable std::array<llvm::Type *, InlineCacheSize> ICacheVals{};
  mutable std::array<uint8_t, InlineCacheSets> ICacheLRU{};

  static constexpr unsigned FunctionInfosLog2InitSize = 9;
  llvm::Type *ConvertFunctionTypeInternal(QualType FT);

public:
  TypeEmitter(ModuleEmitter &cgm);
  ~TypeEmitter();

  const llvm::DataLayout &getDataLayout() const {
    return TheModule.getDataLayout();
  }
  ModuleEmitter &getModuleEmitter() const { return ME; }
  TreeContext &getContext() const { return Context; }
  const ABIInfo &getABIInfo() const { return TheABIInfo; }
  const TargetInfo &getTarget() const { return Target; }
  CGABI &getCGABI() const { return TheABI; }
  llvm::LLVMContext &getLLVMContext() { return TheModule.getContext(); }
  const CodeGenOptions &getCodeGenOpts() const;

  unsigned neverCCallConvToLLVMCallConv(CallingConv CC);

  llvm::Type *convertType(QualType T);

  llvm::Type *convertTypeForMem(QualType T, bool ForBitField = false);

  llvm::FunctionType *GetFunctionType(const ABIFunctionInfo &Info);

  llvm::FunctionType *GetFunctionType(GlobalDecl GD);

  bool isFuncTypeConvertible(const FunctionType *FT);
  bool isFuncParamTypeConvertible(QualType Ty);

  const RecordLayoutInfo &getRecordLayoutInfo(const RecordDecl *);

  void updateCompletedType(const TagDecl *TD);

  // The arrangement methods are split into three families:
  //   - those meant to drive the signature and prologue/epilogue
  //     of a function declaration or definition,
  //   - those meant for the computation of the LLVM type for an abstract
  //     appearance of a function, and
  //   - those meant for performing the IR-generation of a call.
  // They differ mainly in how they deal with optional (i.e. variadic)
  // arguments, as well as unprototyped functions.
  //
  // Key points:
  // - The ABIFunctionInfo for emitting a specific call site must include
  //   entries for the optional arguments.
  // - The function type used at the call site must reflect the formal
  //   signature of the declaration being called, or else the call will
  //   go awry.
  // - For the most part, unprototyped functions are called by casting to
  //   a formal signature inferred from the specific argument types used
  //   at the call-site.  However, some targets (e.g. x86-64) screw with
  //   this for compatibility reasons.

  const ABIFunctionInfo &arrangeGlobalDeclaration(GlobalDecl GD);

  const ABIFunctionInfo &arrangeFunctionDeclaration(const FunctionDecl *FD);
  const ABIFunctionInfo &arrangeFreeFunctionCall(const CallArgList &Args,
                                                 const FunctionType *Ty,
                                                 bool ChainCall);
  const ABIFunctionInfo &arrangeFreeFunctionType(CanQual<FunctionProtoType> Ty);
  const ABIFunctionInfo &
  arrangeFreeFunctionType(CanQual<FunctionNoProtoType> Ty);

  const ABIFunctionInfo &arrangeNullaryFunction();

  const ABIFunctionInfo &
  arrangeBuiltinFunctionDeclaration(QualType resultType,
                                    const FunctionArgList &args);
  const ABIFunctionInfo &
  arrangeBuiltinFunctionDeclaration(CanQualType resultType,
                                    llvm::ArrayRef<CanQualType> argTypes);
  const ABIFunctionInfo &arrangeBuiltinFunctionCall(QualType resultType,
                                                    const CallArgList &args);

  const ABIFunctionInfo &arrangeLLVMFunctionInfo(
      CanQualType returnType, FnInfoOpts opts,
      llvm::ArrayRef<CanQualType> argTypes, FunctionType::ExtInfo info,
      llvm::ArrayRef<FunctionProtoType::ExtParameterInfo> paramInfos,
      RequiredArgs args);

  std::unique_ptr<RecordLayoutInfo> computeRecordLayout(const RecordDecl *D,
                                                        llvm::StructType *Ty);

  void addRecordTypeName(const RecordDecl *RD, llvm::StructType *Ty,
                         llvm::StringRef suffix);

public: // These are internal details of CGT that shouldn't be used externally.
  llvm::StructType *ConvertRecordDeclType(const RecordDecl *TD);

  void getExpandedTypes(QualType Ty,
                        llvm::SmallVectorImpl<llvm::Type *>::iterator &TI);

  bool isZeroInitializable(QualType T);

  bool isPointerZeroInitializable(QualType T);

  bool isZeroInitializable(const RecordDecl *RD);

  bool isLongDoubleReferenced() const { return LongDoubleReferenced; }
  bool isRecordLayoutComplete(const Type *Ty) const;
  unsigned getTargetAddressSpace(QualType T) const;
};

} // end namespace Emit
} // end namespace neverc

#endif
