#include "ABI/ABIInfo.h"
#include "ABI/ABIInfoImpl.h"
#include "ABI/EmitterABI.h"
#include "ABI/TargetInfo.h"
#include "Core/FunctionEmitter.h"
#include "Core/ModuleEmitter.h"
#include "Core/RecordLayoutInfo.h"
#include "Stmt/CallEmitterInfo.h"
#include "Stmt/CleanupEmitterInfo.h"
#include "neverc/Emit/ABI/ABIFunctionInfo.h"
#include "neverc/Tree/Core/Attr.h"
#include "neverc/Tree/Decl/DeclC.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Assumptions.h"
#include "llvm/IR/AttributeMask.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Transforms/Utils/Local.h"
#include <optional>
using namespace neverc;
using namespace Emit;
using namespace CodeGen;

// ===----------------------------------------------------------------------===
// Calling convention & function type helpers
// ===----------------------------------------------------------------------===

unsigned TypeEmitter::neverCCallConvToLLVMCallConv(CallingConv CC) {
  switch (CC) {
  default:
    return llvm::CallingConv::C;
  case CC_X86StdCall:
  case CC_X86FastCall:
    return llvm::CallingConv::C;
  case CC_X86RegCall:
    return llvm::CallingConv::X86_RegCall;
  case CC_Win64:
    return llvm::CallingConv::Win64;
  case CC_X86_64SysV:
    return llvm::CallingConv::X86_64_SysV;
  case CC_X86VectorCall:
    return llvm::CallingConv::X86_VectorCall;
  case CC_AArch64VectorCall:
    return llvm::CallingConv::AArch64_VectorCall;
  case CC_AArch64SVEPCS:
    return llvm::CallingConv::AArch64_SVE_VectorCall;

  case CC_PreserveMost:
    return llvm::CallingConv::PreserveMost;
  case CC_PreserveAll:
    return llvm::CallingConv::PreserveAll;
  }
}

namespace {
CanQualType getReturnType(QualType RetTy) {
  return RetTy->getCanonicalTypeUnqualified().getUnqualifiedType();
}
} // namespace

const ABIFunctionInfo &
TypeEmitter::arrangeFreeFunctionType(CanQual<FunctionNoProtoType> FTNP) {
  // When translating an unprototyped function type, always use a
  // variadic type.
  return arrangeLLVMFunctionInfo(FTNP->getReturnType().getUnqualifiedType(),
                                 FnInfoOpts::None, std::nullopt,
                                 FTNP->getExtInfo(), {}, RequiredArgs(0));
}

namespace {
void addExtParameterInfosForCall(
    llvm::SmallVectorImpl<FunctionProtoType::ExtParameterInfo> &paramInfos,
    const FunctionProtoType *proto, unsigned prefixArgs, unsigned totalArgs) {
  assert(proto->hasExtParameterInfos());
  assert(paramInfos.size() <= prefixArgs);
  assert(proto->getNumParams() + prefixArgs <= totalArgs);

  paramInfos.reserve(totalArgs);

  paramInfos.resize(prefixArgs);

  for (const auto &ParamInfo : proto->getExtParameterInfos()) {
    paramInfos.push_back(ParamInfo);
    // pass_object_size params have no parameter info.
    if (ParamInfo.hasPassObjectSize())
      paramInfos.emplace_back();
  }

  assert(paramInfos.size() <= totalArgs &&
         "Did we forget to insert pass_object_size args?");
  paramInfos.resize(totalArgs);
}

void appendParameterTypes(
    const TypeEmitter &CGT, llvm::SmallVectorImpl<CanQualType> &prefix,
    llvm::SmallVectorImpl<FunctionProtoType::ExtParameterInfo> &paramInfos,
    CanQual<FunctionProtoType> FPT) {
  // Fast path: don't touch param info if we don't need to.
  if (!FPT->hasExtParameterInfos()) {
    assert(paramInfos.empty() &&
           "We have paramInfos, but the prototype doesn't?");
    prefix.append(FPT->param_type_begin(), FPT->param_type_end());
    return;
  }

  unsigned PrefixSize = prefix.size();
  // In the vast majority of cases, we'll have precisely FPT->getNumParams()
  // parameters; the only thing that can change this is the presence of
  // pass_object_size. So, we preallocate for the common case.
  prefix.reserve(prefix.size() + FPT->getNumParams());

  auto ExtInfos = FPT->getExtParameterInfos();
  assert(ExtInfos.size() == FPT->getNumParams());
  for (unsigned I = 0, E = FPT->getNumParams(); I != E; ++I) {
    prefix.push_back(FPT->getParamType(I));
    if (ExtInfos[I].hasPassObjectSize())
      prefix.push_back(CGT.getContext().getSizeType());
  }

  addExtParameterInfosForCall(paramInfos, FPT.getTypePtr(), PrefixSize,
                              prefix.size());
}

const ABIFunctionInfo &
arrangeLLVMFunctionInfo(TypeEmitter &CGT,
                        llvm::SmallVectorImpl<CanQualType> &prefix,
                        CanQual<FunctionProtoType> FTP) {
  llvm::SmallVector<FunctionProtoType::ExtParameterInfo, 16> paramInfos;
  RequiredArgs Required = RequiredArgs::forPrototypePlus(FTP, prefix.size());
  appendParameterTypes(CGT, prefix, paramInfos, FTP);
  CanQualType resultType = FTP->getReturnType().getUnqualifiedType();

  return CGT.arrangeLLVMFunctionInfo(resultType, FnInfoOpts::None, prefix,
                                     FTP->getExtInfo(), paramInfos, Required);
}
} // namespace

const ABIFunctionInfo &
TypeEmitter::arrangeFreeFunctionType(CanQual<FunctionProtoType> FTP) {
  llvm::SmallVector<CanQualType, 16> argTypes;
  return ::arrangeLLVMFunctionInfo(*this, argTypes, FTP);
}

namespace {
llvm::SmallVector<CanQualType, 16>
getArgTypesForDeclaration(TreeContext &ctx, const FunctionArgList &args) {
  llvm::SmallVector<CanQualType, 16> argTypes;
  for (auto &arg : args)
    argTypes.push_back(ctx.getCanonicalParamType(arg->getType()));
  return argTypes;
}
} // namespace

const ABIFunctionInfo &
TypeEmitter::arrangeFunctionDeclaration(const FunctionDecl *FD) {
  CanQualType FTy = FD->getType()->getCanonicalTypeUnqualified();

  assert(isa<FunctionType>(FTy));

  // When declaring a function without a prototype, always use a
  // non-variadic type.
  if (CanQual<FunctionNoProtoType> noProto = FTy.getAs<FunctionNoProtoType>()) {
    return arrangeLLVMFunctionInfo(noProto->getReturnType(), FnInfoOpts::None,
                                   std::nullopt, noProto->getExtInfo(), {},
                                   RequiredArgs::All);
  }

  return arrangeFreeFunctionType(FTy.castAs<FunctionProtoType>());
}

const ABIFunctionInfo &TypeEmitter::arrangeGlobalDeclaration(GlobalDecl GD) {
  const FunctionDecl *FD = cast<FunctionDecl>(GD.getDecl());
  return arrangeFunctionDeclaration(FD);
}

namespace {
const ABIFunctionInfo &
arrangeFreeFunctionLikeCall(TypeEmitter &CGT, ModuleEmitter &ME,
                            const CallArgList &args, const FunctionType *fnType,
                            unsigned numExtraRequiredArgs, bool chainCall) {
  assert(args.size() >= numExtraRequiredArgs);

  llvm::SmallVector<FunctionProtoType::ExtParameterInfo, 16> paramInfos;

  // In most cases, there are no optional arguments.
  RequiredArgs required = RequiredArgs::All;

  // If we have a variadic prototype, the required arguments are the
  // extra prefix plus the arguments in the prototype.
  if (const FunctionProtoType *proto = dyn_cast<FunctionProtoType>(fnType)) {
    if (proto->isVariadic())
      required = RequiredArgs::forPrototypePlus(proto, numExtraRequiredArgs);

    if (proto->hasExtParameterInfos())
      addExtParameterInfosForCall(paramInfos, proto, numExtraRequiredArgs,
                                  args.size());

    // If we don't have a prototype at all, but we're supposed to
    // explicitly use the variadic convention for unprototyped calls,
    // treat all of the arguments as required but preserve the nominal
    // possibility of variadics.
  } else if (ME.getTargetCodeGenInfo().isNoProtoCallVariadic(
                 args, cast<FunctionNoProtoType>(fnType))) {
    required = RequiredArgs(args.size());
  }

  llvm::SmallVector<CanQualType, 16> argTypes;
  for (const auto &arg : args)
    argTypes.push_back(CGT.getContext().getCanonicalParamType(arg.Ty));
  FnInfoOpts opts = chainCall ? FnInfoOpts::IsChainCall : FnInfoOpts::None;
  return CGT.arrangeLLVMFunctionInfo(getReturnType(fnType->getReturnType()),
                                     opts, argTypes, fnType->getExtInfo(),
                                     paramInfos, required);
}
} // namespace

const ABIFunctionInfo &TypeEmitter::arrangeFreeFunctionCall(
    const CallArgList &args, const FunctionType *fnType, bool chainCall) {
  return arrangeFreeFunctionLikeCall(*this, ME, args, fnType, chainCall ? 1 : 0,
                                     chainCall);
}

const ABIFunctionInfo &
TypeEmitter::arrangeBuiltinFunctionCall(QualType resultType,
                                        const CallArgList &args) {
  llvm::SmallVector<CanQualType, 16> argTypes;
  for (const auto &Arg : args)
    argTypes.push_back(Context.getCanonicalParamType(Arg.Ty));
  return arrangeLLVMFunctionInfo(getReturnType(resultType), FnInfoOpts::None,
                                 argTypes, FunctionType::ExtInfo(),
                                 /*paramInfos=*/{}, RequiredArgs::All);
}

const ABIFunctionInfo &
TypeEmitter::arrangeBuiltinFunctionDeclaration(QualType resultType,
                                               const FunctionArgList &args) {
  auto argTypes = getArgTypesForDeclaration(Context, args);

  return arrangeLLVMFunctionInfo(getReturnType(resultType), FnInfoOpts::None,
                                 argTypes, FunctionType::ExtInfo(), {},
                                 RequiredArgs::All);
}

const ABIFunctionInfo &TypeEmitter::arrangeBuiltinFunctionDeclaration(
    CanQualType resultType, llvm::ArrayRef<CanQualType> argTypes) {
  return arrangeLLVMFunctionInfo(resultType, FnInfoOpts::None, argTypes,
                                 FunctionType::ExtInfo(), {},
                                 RequiredArgs::All);
}

const ABIFunctionInfo &TypeEmitter::arrangeNullaryFunction() {
  return arrangeLLVMFunctionInfo(getContext().VoidTy, FnInfoOpts::None,
                                 std::nullopt, FunctionType::ExtInfo(), {},
                                 RequiredArgs::All);
}

const ABIFunctionInfo &TypeEmitter::arrangeLLVMFunctionInfo(
    CanQualType resultType, FnInfoOpts opts,
    llvm::ArrayRef<CanQualType> argTypes, FunctionType::ExtInfo info,
    llvm::ArrayRef<FunctionProtoType::ExtParameterInfo> paramInfos,
    RequiredArgs required) {
  assert(llvm::all_of(argTypes,
                      [](CanQualType T) { return T.isCanonicalAsParam(); }));

  // Lookup or create unique function info.
  llvm::FoldingSetNodeID ID;
  bool isInstanceMethod =
      (opts & FnInfoOpts::IsInstanceMethod) == FnInfoOpts::IsInstanceMethod;
  bool isChainCall =
      (opts & FnInfoOpts::IsChainCall) == FnInfoOpts::IsChainCall;
  bool isDelegateCall =
      (opts & FnInfoOpts::IsDelegateCall) == FnInfoOpts::IsDelegateCall;
  ABIFunctionInfo::Profile(ID, isInstanceMethod, isChainCall, isDelegateCall,
                           info, paramInfos, required, resultType, argTypes);

  void *insertPos = nullptr;
  ABIFunctionInfo *FI = FunctionInfos.FindNodeOrInsertPos(ID, insertPos);
  if (FI)
    return *FI;

  unsigned CC = neverCCallConvToLLVMCallConv(info.getCC());

  // Construct the function info.  We co-allocate the ArgInfos.
  FI =
      ABIFunctionInfo::create(CC, isInstanceMethod, isChainCall, isDelegateCall,
                              info, paramInfos, resultType, argTypes, required);
  FunctionInfos.InsertNode(FI, insertPos);

  bool inserted = FunctionsBeingProcessed.insert(FI).second;
  (void)inserted;
  assert(inserted && "Recursively being processed?");

  getABIInfo().computeInfo(*FI);

  // Loop over all of the computed argument and return value info.  If any of
  // them are direct or extend without a specified coerce type, specify the
  // default now.
  ABIArgInfo &retInfo = FI->getReturnInfo();
  if (retInfo.canHaveCoerceToType() && retInfo.getCoerceToType() == nullptr)
    retInfo.setCoerceToType(convertType(FI->getReturnType()));

  for (auto &I : FI->arguments())
    if (I.info.canHaveCoerceToType() && I.info.getCoerceToType() == nullptr)
      I.info.setCoerceToType(convertType(I.type));

  bool erased = FunctionsBeingProcessed.erase(FI);
  (void)erased;
  assert(erased && "Not in set?");

  return *FI;
}

ABIFunctionInfo *ABIFunctionInfo::create(
    unsigned llvmCC, bool instanceMethod, bool chainCall, bool delegateCall,
    const FunctionType::ExtInfo &info,
    llvm::ArrayRef<ExtParameterInfo> paramInfos, CanQualType resultType,
    llvm::ArrayRef<CanQualType> argTypes, RequiredArgs required) {
  assert(paramInfos.empty() || paramInfos.size() == argTypes.size());
  assert(!required.allowsOptionalArgs() ||
         required.getNumRequiredArgs() <= argTypes.size());

  void *buffer = operator new(totalSizeToAlloc<ArgInfo, ExtParameterInfo>(
      argTypes.size() + 1, paramInfos.size()));

  ABIFunctionInfo *FI = new (buffer) ABIFunctionInfo();
  FI->CallingConvention = llvmCC;
  FI->EffectiveCallingConvention = llvmCC;
  FI->ASTCallingConvention = info.getCC();
  FI->InstanceMethod = instanceMethod;
  FI->ChainCall = chainCall;
  FI->DelegateCall = delegateCall;
  FI->CmseNSCall = info.getCmseNSCall();
  FI->NoReturn = info.getNoReturn();
  FI->ReturnsRetained = info.getProducesResult();
  FI->NoCallerSavedRegs = info.getNoCallerSavedRegs();
  FI->NoCfCheck = info.getNoCfCheck();
  FI->Required = required;
  FI->HasRegParm = info.getHasRegParm();
  FI->RegParm = info.getRegParm();
  FI->NumArgs = argTypes.size();
  FI->HasExtParameterInfos = !paramInfos.empty();
  FI->getArgsBuffer()[0].type = resultType;
  FI->MaxVectorWidth = 0;
  for (unsigned i = 0, e = argTypes.size(); i != e; ++i)
    FI->getArgsBuffer()[i + 1].type = argTypes[i];
  for (unsigned i = 0, e = paramInfos.size(); i != e; ++i)
    FI->getExtParameterInfosBuffer()[i] = paramInfos[i];
  return FI;
}

// ===----------------------------------------------------------------------===
// Type expansion for ABIArgInfo::Expand
// ===----------------------------------------------------------------------===

namespace {

// Specifies the way QualType passed as ABIArgInfo::Expand is expanded.
struct TypeExpansion {
  enum TypeExpansionKind {
    // Elements of constant arrays are expanded recursively.
    TEK_ConstantArray,
    // Record fields are expanded recursively (but if record is a union, only
    // the field with the largest size is expanded).
    TEK_Record,
    // For complex types, real and imaginary parts are expanded recursively.
    TEK_Complex,
    // All other types are not expandable.
    TEK_None
  };

  const TypeExpansionKind Kind;

  TypeExpansion(TypeExpansionKind K) : Kind(K) {}
  virtual ~TypeExpansion() {}
};

struct ConstantArrayExpansion : TypeExpansion {
  QualType EltTy;
  uint64_t NumElts;

  ConstantArrayExpansion(QualType EltTy, uint64_t NumElts)
      : TypeExpansion(TEK_ConstantArray), EltTy(EltTy), NumElts(NumElts) {}
  static bool classof(const TypeExpansion *TE) {
    return TE->Kind == TEK_ConstantArray;
  }
};

struct RecordExpansion : TypeExpansion {
  llvm::SmallVector<const FieldDecl *, 1> Fields;

  RecordExpansion(llvm::SmallVector<const FieldDecl *, 1> &&Fields)
      : TypeExpansion(TEK_Record), Fields(std::move(Fields)) {}
  static bool classof(const TypeExpansion *TE) {
    return TE->Kind == TEK_Record;
  }
};

struct ComplexExpansion : TypeExpansion {
  QualType EltTy;

  ComplexExpansion(QualType EltTy) : TypeExpansion(TEK_Complex), EltTy(EltTy) {}
  static bool classof(const TypeExpansion *TE) {
    return TE->Kind == TEK_Complex;
  }
};

struct NoExpansion : TypeExpansion {
  NoExpansion() : TypeExpansion(TEK_None) {}
  static bool classof(const TypeExpansion *TE) { return TE->Kind == TEK_None; }
};
} // namespace

namespace {
std::unique_ptr<TypeExpansion> getTypeExpansion(QualType Ty,
                                                const TreeContext &Context) {
  if (const ConstantArrayType *AT = Context.getAsConstantArrayType(Ty)) {
    return std::make_unique<ConstantArrayExpansion>(
        AT->getElementType(), AT->getSize().getZExtValue());
  }
  if (const RecordType *RT = Ty->getAs<RecordType>()) {
    llvm::SmallVector<const FieldDecl *, 1> Fields;
    const RecordDecl *RD = RT->getDecl();
    assert(!RD->hasFlexibleArrayMember() &&
           "Cannot expand structure with flexible array.");
    if (RD->isUnion()) {
      // Unions can be here only in degenerative cases - all the fields are same
      // after flattening. Thus we have to use the "largest" field.
      const FieldDecl *LargestFD = nullptr;
      CharUnits UnionSize = CharUnits::Zero();

      for (const auto *FD : RD->fields()) {
        if (FD->isZeroLengthBitField(Context))
          continue;
        assert(!FD->isBitField() &&
               "Cannot expand structure with bit-field members.");
        CharUnits FieldSize = Context.getTypeSizeInChars(FD->getType());
        if (UnionSize < FieldSize) {
          UnionSize = FieldSize;
          LargestFD = FD;
        }
      }
      if (LargestFD)
        Fields.push_back(LargestFD);
    } else {

      for (const auto *FD : RD->fields()) {
        if (FD->isZeroLengthBitField(Context))
          continue;
        assert(!FD->isBitField() &&
               "Cannot expand structure with bit-field members.");
        Fields.push_back(FD);
      }
    }
    return std::make_unique<RecordExpansion>(std::move(Fields));
  }
  if (const ComplexType *CT = Ty->getAs<ComplexType>()) {
    return std::make_unique<ComplexExpansion>(CT->getElementType());
  }
  return std::make_unique<NoExpansion>();
}

int getExpansionSize(QualType Ty, const TreeContext &Context) {
  auto Exp = getTypeExpansion(Ty, Context);
  if (auto CAExp = dyn_cast<ConstantArrayExpansion>(Exp.get())) {
    return CAExp->NumElts * getExpansionSize(CAExp->EltTy, Context);
  }
  if (auto RExp = dyn_cast<RecordExpansion>(Exp.get())) {
    int Res = 0;
    for (auto FD : RExp->Fields)
      Res += getExpansionSize(FD->getType(), Context);
    return Res;
  }
  if (isa<ComplexExpansion>(Exp.get()))
    return 2;
  assert(isa<NoExpansion>(Exp.get()));
  return 1;
}
} // namespace

void TypeEmitter::getExpandedTypes(
    QualType Ty, llvm::SmallVectorImpl<llvm::Type *>::iterator &TI) {
  auto Exp = getTypeExpansion(Ty, Context);
  if (auto CAExp = dyn_cast<ConstantArrayExpansion>(Exp.get())) {
    for (int i = 0, n = CAExp->NumElts; i < n; i++) {
      getExpandedTypes(CAExp->EltTy, TI);
    }
  } else if (auto RExp = dyn_cast<RecordExpansion>(Exp.get())) {
    for (auto FD : RExp->Fields)
      getExpandedTypes(FD->getType(), TI);
  } else if (auto CExp = dyn_cast<ComplexExpansion>(Exp.get())) {
    llvm::Type *EltTy = convertType(CExp->EltTy);
    *TI++ = EltTy;
    *TI++ = EltTy;
  } else {
    assert(isa<NoExpansion>(Exp.get()));
    *TI++ = convertType(Ty);
  }
}

namespace {
void forConstantArrayExpansion(FunctionEmitter &FE, ConstantArrayExpansion *CAE,
                               Address BaseAddr,
                               llvm::function_ref<void(Address)> Fn) {
  CharUnits EltSize = FE.getContext().getTypeSizeInChars(CAE->EltTy);
  CharUnits EltAlign = BaseAddr.getAlignment().alignmentOfArrayElement(EltSize);
  llvm::Type *EltTy = FE.convertTypeForMem(CAE->EltTy);

  for (int i = 0, n = CAE->NumElts; i < n; i++) {
    llvm::Value *EltAddr = FE.Builder.CreateConstGEP2_32(
        BaseAddr.getElementType(), BaseAddr.getPointer(), 0, i);
    Fn(Address(EltAddr, EltTy, EltAlign));
  }
}
} // namespace

void FunctionEmitter::expandTypeFromArgs(QualType Ty, LValue LV,
                                         llvm::Function::arg_iterator &AI) {
  assert(LV.isSimple() &&
         "Unexpected non-simple lvalue during struct expansion.");

  auto Exp = getTypeExpansion(Ty, getContext());
  if (auto CAExp = dyn_cast<ConstantArrayExpansion>(Exp.get())) {
    forConstantArrayExpansion(
        *this, CAExp, LV.getAddress(*this), [&](Address EltAddr) {
          LValue LV = makeAddrLValue(EltAddr, CAExp->EltTy);
          expandTypeFromArgs(CAExp->EltTy, LV, AI);
        });
  } else if (auto RExp = dyn_cast<RecordExpansion>(Exp.get())) {
    for (auto FD : RExp->Fields) {
      LValue SubLV = genLValueForFieldInitialization(LV, FD);
      expandTypeFromArgs(FD->getType(), SubLV, AI);
    }
  } else if (isa<ComplexExpansion>(Exp.get())) {
    auto realValue = &*AI++;
    auto imagValue = &*AI++;
    genStoreOfComplex(ComplexPairTy(realValue, imagValue), LV, /*init*/ true);
  } else {
    // Call genStoreOfScalar except when the lvalue is a bitfield to emit a
    // primitive store.
    assert(isa<NoExpansion>(Exp.get()));
    llvm::Value *Arg = &*AI++;
    if (LV.isBitField()) {
      genStoreThroughLValue(RValue::get(Arg), LV);
    } else {
      if (Arg->getType()->isPointerTy()) {
        Address Addr = LV.getAddress(*this);
        Arg = Builder.CreateBitCast(Arg, Addr.getElementType());
      }
      genStoreOfScalar(Arg, LV);
    }
  }
}

void FunctionEmitter::expandTypeToArgs(
    QualType Ty, CallArg Arg, llvm::FunctionType *IRFuncTy,
    llvm::SmallVectorImpl<llvm::Value *> &IRCallArgs, unsigned &IRCallArgPos) {
  auto Exp = getTypeExpansion(Ty, getContext());
  if (auto CAExp = dyn_cast<ConstantArrayExpansion>(Exp.get())) {
    Address Addr = Arg.hasLValue() ? Arg.getKnownLValue().getAddress(*this)
                                   : Arg.getKnownRValue().getAggregateAddress();
    forConstantArrayExpansion(*this, CAExp, Addr, [&](Address EltAddr) {
      CallArg EltArg =
          CallArg(convertTempToRValue(EltAddr, CAExp->EltTy, SourceLocation()),
                  CAExp->EltTy);
      expandTypeToArgs(CAExp->EltTy, EltArg, IRFuncTy, IRCallArgs,
                       IRCallArgPos);
    });
  } else if (auto RExp = dyn_cast<RecordExpansion>(Exp.get())) {
    Address This = Arg.hasLValue() ? Arg.getKnownLValue().getAddress(*this)
                                   : Arg.getKnownRValue().getAggregateAddress();
    LValue LV = makeAddrLValue(This, Ty);
    for (auto FD : RExp->Fields) {
      CallArg FldArg =
          CallArg(genRValueForField(LV, FD, SourceLocation()), FD->getType());
      expandTypeToArgs(FD->getType(), FldArg, IRFuncTy, IRCallArgs,
                       IRCallArgPos);
    }
  } else if (isa<ComplexExpansion>(Exp.get())) {
    ComplexPairTy CV = Arg.getKnownRValue().getComplexVal();
    IRCallArgs[IRCallArgPos++] = CV.first;
    IRCallArgs[IRCallArgPos++] = CV.second;
  } else {
    assert(isa<NoExpansion>(Exp.get()));
    auto RV = Arg.getKnownRValue();
    assert(RV.isScalar() &&
           "Unexpected non-scalar rvalue during struct expansion.");

    llvm::Value *V = RV.getScalarVal();
    if (IRCallArgPos < IRFuncTy->getNumParams() &&
        V->getType() != IRFuncTy->getParamType(IRCallArgPos))
      V = Builder.CreateBitCast(V, IRFuncTy->getParamType(IRCallArgPos));

    IRCallArgs[IRCallArgPos++] = V;
  }
}

// ===----------------------------------------------------------------------===
// Coercion & ABI type conversion
// ===----------------------------------------------------------------------===

namespace {
Address createTempAllocaForCoercion(FunctionEmitter &FE, llvm::Type *Ty,
                                    CharUnits MinAlign,
                                    const llvm::Twine &Name = "tmp") {
  // Don't use an alignment that's worse than what LLVM would prefer.
  auto PrefAlign = FE.ME.getDataLayout().getPrefTypeAlign(Ty);
  CharUnits Align = std::max(MinAlign, CharUnits::fromQuantity(PrefAlign));

  return FE.createTempAlloca(Ty, Align, Name + ".coerce");
}

Address enterStructPointerForCoercedAccess(Address SrcPtr,
                                           llvm::StructType *SrcSTy,
                                           uint64_t DstSize,
                                           FunctionEmitter &FE) {
  // We can't dive into a zero-element struct.
  if (SrcSTy->getNumElements() == 0)
    return SrcPtr;

  llvm::Type *FirstElt = SrcSTy->getElementType(0);

  // If the first elt is at least as large as what we're looking for, or if the
  // first element is the same size as the whole struct, we can enter it. The
  // comparison must be made on the store size and not the alloca size. Using
  // the alloca size may overstate the size of the load.
  uint64_t FirstEltSize = FE.ME.getDataLayout().getTypeStoreSize(FirstElt);
  if (FirstEltSize < DstSize &&
      FirstEltSize < FE.ME.getDataLayout().getTypeStoreSize(SrcSTy))
    return SrcPtr;

  // GEP into the first element.
  SrcPtr = FE.Builder.CreateStructGEP(SrcPtr, 0, "coerce.dive");

  // If the first element is a struct, recurse.
  llvm::Type *SrcTy = SrcPtr.getElementType();
  if (llvm::StructType *SrcSTy = dyn_cast<llvm::StructType>(SrcTy))
    return enterStructPointerForCoercedAccess(SrcPtr, SrcSTy, DstSize, FE);

  return SrcPtr;
}

llvm::Value *coerceIntOrPtrToIntOrPtr(llvm::Value *Val, llvm::Type *Ty,
                                      FunctionEmitter &FE) {
  if (Val->getType() == Ty)
    return Val;

  if (isa<llvm::PointerType>(Val->getType())) {
    // If this is Pointer->Pointer avoid conversion to and from int.
    if (isa<llvm::PointerType>(Ty))
      return FE.Builder.CreateBitCast(Val, Ty, "coerce.val");

    // Convert the pointer to an integer so we can play with its width.
    Val = FE.Builder.CreatePtrToInt(Val, FE.IntPtrTy, "coerce.val.pi");
  }

  llvm::Type *DestIntTy = Ty;
  if (isa<llvm::PointerType>(DestIntTy))
    DestIntTy = FE.IntPtrTy;

  if (Val->getType() != DestIntTy)
    Val = FE.Builder.CreateIntCast(Val, DestIntTy, false, "coerce.val.ii");

  if (isa<llvm::PointerType>(Ty))
    Val = FE.Builder.CreateIntToPtr(Val, Ty, "coerce.val.ip");
  return Val;
}

llvm::Value *createCoercedLoad(Address Src, llvm::Type *Ty,
                               FunctionEmitter &FE) {
  llvm::Type *SrcTy = Src.getElementType();

  // If SrcTy and Ty are the same, just do a load.
  if (SrcTy == Ty)
    return FE.Builder.CreateLoad(Src);

  llvm::TypeSize DstSize = FE.ME.getDataLayout().getTypeAllocSize(Ty);

  if (llvm::StructType *SrcSTy = dyn_cast<llvm::StructType>(SrcTy)) {
    Src = enterStructPointerForCoercedAccess(Src, SrcSTy,
                                             DstSize.getFixedValue(), FE);
    SrcTy = Src.getElementType();
  }

  llvm::TypeSize SrcSize = FE.ME.getDataLayout().getTypeAllocSize(SrcTy);

  // If the source and destination are integer or pointer types, just do an
  // extension or truncation to the desired type.
  if ((isa<llvm::IntegerType>(Ty) || isa<llvm::PointerType>(Ty)) &&
      (isa<llvm::IntegerType>(SrcTy) || isa<llvm::PointerType>(SrcTy))) {
    llvm::Value *Load = FE.Builder.CreateLoad(Src);
    return coerceIntOrPtrToIntOrPtr(Load, Ty, FE);
  }

  // If load is legal, just bitcast the src pointer.
  if (!SrcSize.isScalable() && !DstSize.isScalable() &&
      SrcSize.getFixedValue() >= DstSize.getFixedValue()) {
    // Generally SrcSize is never greater than DstSize, since this means we are
    // losing bits. However, this can happen in cases where the structure has
    // additional padding, for example due to a user specified alignment.
    //
    Src = Src.withElementType(Ty);
    return FE.Builder.CreateLoad(Src);
  }

  // If coercing a fixed vector to a scalable vector for ABI compatibility, and
  // the types match, use the llvm.vector.insert intrinsic to perform the
  // conversion.
  if (auto *ScalableDst = dyn_cast<llvm::ScalableVectorType>(Ty)) {
    if (auto *FixedSrc = dyn_cast<llvm::FixedVectorType>(SrcTy)) {
      // If we are casting a fixed i8 vector to a scalable 16 x i1 predicate
      // vector, use a vector insert and bitcast the result.
      bool NeedsBitcast = false;
      auto PredType = llvm::ScalableVectorType::get(FE.Builder.getInt1Ty(), 16);
      llvm::Type *OrigType = Ty;
      if (ScalableDst == PredType &&
          FixedSrc->getElementType() == FE.Builder.getInt8Ty()) {
        ScalableDst = llvm::ScalableVectorType::get(FE.Builder.getInt8Ty(), 2);
        NeedsBitcast = true;
      }
      if (ScalableDst->getElementType() == FixedSrc->getElementType()) {
        auto *Load = FE.Builder.CreateLoad(Src);
        auto *UndefVec = llvm::UndefValue::get(ScalableDst);
        auto *Zero = llvm::Constant::getNullValue(FE.ME.Int64Ty);
        llvm::Value *Result = FE.Builder.CreateInsertVector(
            ScalableDst, UndefVec, Load, Zero, "cast.scalable");
        if (NeedsBitcast)
          Result = FE.Builder.CreateBitCast(Result, OrigType);
        return Result;
      }
    }
  }

  // Otherwise do coercion through memory. This is stupid, but simple.
  Address Tmp =
      createTempAllocaForCoercion(FE, Ty, Src.getAlignment(), Src.getName());
  FE.Builder.CreateMemCpy(
      Tmp.getPointer(), Tmp.getAlignment().getAsAlign(), Src.getPointer(),
      Src.getAlignment().getAsAlign(),
      llvm::ConstantInt::get(FE.IntPtrTy, SrcSize.getKnownMinValue()));
  return FE.Builder.CreateLoad(Tmp);
}
} // namespace

void FunctionEmitter::genAggregateStore(llvm::Value *Val, Address Dest,
                                        bool DestIsVolatile) {
  // Prefer scalar stores to first-class aggregate stores.
  if (llvm::StructType *STy = dyn_cast<llvm::StructType>(Val->getType())) {
    for (unsigned i = 0, e = STy->getNumElements(); i != e; ++i) {
      Address EltPtr = Builder.CreateStructGEP(Dest, i);
      llvm::Value *Elt = Builder.CreateExtractValue(Val, i);
      Builder.CreateStore(Elt, EltPtr, DestIsVolatile);
    }
  } else {
    Builder.CreateStore(Val, Dest, DestIsVolatile);
  }
}

namespace {
void createCoercedStore(llvm::Value *Src, Address Dst, bool DstIsVolatile,
                        FunctionEmitter &FE) {
  llvm::Type *SrcTy = Src->getType();
  llvm::Type *DstTy = Dst.getElementType();
  if (SrcTy == DstTy) {
    FE.Builder.CreateStore(Src, Dst, DstIsVolatile);
    return;
  }

  llvm::TypeSize SrcSize = FE.ME.getDataLayout().getTypeAllocSize(SrcTy);

  if (llvm::StructType *DstSTy = dyn_cast<llvm::StructType>(DstTy)) {
    Dst = enterStructPointerForCoercedAccess(Dst, DstSTy,
                                             SrcSize.getFixedValue(), FE);
    DstTy = Dst.getElementType();
  }

  llvm::PointerType *SrcPtrTy = llvm::dyn_cast<llvm::PointerType>(SrcTy);
  llvm::PointerType *DstPtrTy = llvm::dyn_cast<llvm::PointerType>(DstTy);
  if (SrcPtrTy && DstPtrTy &&
      SrcPtrTy->getAddressSpace() != DstPtrTy->getAddressSpace()) {
    Src = FE.Builder.CreateAddrSpaceCast(Src, DstTy);
    FE.Builder.CreateStore(Src, Dst, DstIsVolatile);
    return;
  }

  // If the source and destination are integer or pointer types, just do an
  // extension or truncation to the desired type.
  if ((isa<llvm::IntegerType>(SrcTy) || isa<llvm::PointerType>(SrcTy)) &&
      (isa<llvm::IntegerType>(DstTy) || isa<llvm::PointerType>(DstTy))) {
    Src = coerceIntOrPtrToIntOrPtr(Src, DstTy, FE);
    FE.Builder.CreateStore(Src, Dst, DstIsVolatile);
    return;
  }

  llvm::TypeSize DstSize = FE.ME.getDataLayout().getTypeAllocSize(DstTy);

  // If store is legal, just bitcast the src pointer.
  if (isa<llvm::ScalableVectorType>(SrcTy) ||
      isa<llvm::ScalableVectorType>(DstTy) ||
      SrcSize.getFixedValue() <= DstSize.getFixedValue()) {
    Dst = Dst.withElementType(SrcTy);
    FE.genAggregateStore(Src, Dst, DstIsVolatile);
  } else {
    // Otherwise do coercion through memory. This is stupid, but
    // simple.

    // Generally SrcSize is never greater than DstSize, since this means we are
    // losing bits. However, this can happen in cases where the structure has
    // additional padding, for example due to a user specified alignment.
    //
    Address Tmp = createTempAllocaForCoercion(FE, SrcTy, Dst.getAlignment());
    FE.Builder.CreateStore(Src, Tmp);
    FE.Builder.CreateMemCpy(
        Dst.getPointer(), Dst.getAlignment().getAsAlign(), Tmp.getPointer(),
        Tmp.getAlignment().getAsAlign(),
        llvm::ConstantInt::get(FE.IntPtrTy, DstSize.getFixedValue()));
  }
}

Address emitAddressAtOffset(FunctionEmitter &FE, Address addr,
                            const ABIArgInfo &info) {
  if (unsigned offset = info.getDirectOffset()) {
    addr = addr.withElementType(FE.Int8Ty);
    addr = FE.Builder.CreateConstInBoundsByteGEP(
        addr, CharUnits::fromQuantity(offset));
    addr = addr.withElementType(info.getCoerceToType());
  }
  return addr;
}
} // namespace

// ===----------------------------------------------------------------------===
// Argument mapping & function attributes
// ===----------------------------------------------------------------------===

namespace {

class FrontendToLLVMArgMapping {
  static const unsigned InvalidIndex = ~0U;
  unsigned InallocaArgNo;
  unsigned SRetArgNo;
  unsigned TotalIRArgs;

  struct IRArgs {
    unsigned PaddingArgIndex;
    // Argument is expanded to IR arguments at positions
    // [FirstArgIndex, FirstArgIndex + NumberOfArgs).
    unsigned FirstArgIndex;
    unsigned NumberOfArgs;

    IRArgs()
        : PaddingArgIndex(InvalidIndex), FirstArgIndex(InvalidIndex),
          NumberOfArgs(0) {}
  };

  llvm::SmallVector<IRArgs, 8> ArgInfo;

public:
  FrontendToLLVMArgMapping(const TreeContext &Context,
                           const ABIFunctionInfo &FI,
                           bool OnlyRequiredArgs = false)
      : InallocaArgNo(InvalidIndex), SRetArgNo(InvalidIndex), TotalIRArgs(0),
        ArgInfo(OnlyRequiredArgs ? FI.getNumRequiredArgs() : FI.arg_size()) {
    construct(Context, FI, OnlyRequiredArgs);
  }

  bool hasInallocaArg() const { return InallocaArgNo != InvalidIndex; }
  unsigned getInallocaArgNo() const {
    assert(hasInallocaArg());
    return InallocaArgNo;
  }

  bool hasSRetArg() const { return SRetArgNo != InvalidIndex; }
  unsigned getSRetArgNo() const {
    assert(hasSRetArg());
    return SRetArgNo;
  }

  unsigned totalIRArgs() const { return TotalIRArgs; }

  bool hasPaddingArg(unsigned ArgNo) const {
    assert(ArgNo < ArgInfo.size());
    return ArgInfo[ArgNo].PaddingArgIndex != InvalidIndex;
  }
  unsigned getPaddingArgNo(unsigned ArgNo) const {
    assert(hasPaddingArg(ArgNo));
    return ArgInfo[ArgNo].PaddingArgIndex;
  }

  std::pair<unsigned, unsigned> getIRArgs(unsigned ArgNo) const {
    assert(ArgNo < ArgInfo.size());
    return std::make_pair(ArgInfo[ArgNo].FirstArgIndex,
                          ArgInfo[ArgNo].NumberOfArgs);
  }

private:
  void construct(const TreeContext &Context, const ABIFunctionInfo &FI,
                 bool OnlyRequiredArgs);
};

void FrontendToLLVMArgMapping::construct(const TreeContext &Context,
                                         const ABIFunctionInfo &FI,
                                         bool OnlyRequiredArgs) {
  unsigned IRArgNo = 0;
  bool SwapThisWithSRet = false;
  const ABIArgInfo &RetAI = FI.getReturnInfo();

  if (RetAI.getKind() == ABIArgInfo::Indirect) {
    SwapThisWithSRet = RetAI.isSRetAfterThis();
    SRetArgNo = SwapThisWithSRet ? 1 : IRArgNo++;
  }

  unsigned ArgNo = 0;
  unsigned NumArgs = OnlyRequiredArgs ? FI.getNumRequiredArgs() : FI.arg_size();
  for (ABIFunctionInfo::const_arg_iterator I = FI.arg_begin(); ArgNo < NumArgs;
       ++I, ++ArgNo) {
    assert(I != FI.arg_end());
    QualType ArgType = I->type;
    const ABIArgInfo &AI = I->info;
    auto &IRArgs = ArgInfo[ArgNo];

    if (AI.getPaddingType())
      IRArgs.PaddingArgIndex = IRArgNo++;

    switch (AI.getKind()) {
    case ABIArgInfo::Extend:
    case ABIArgInfo::Direct: {
      llvm::StructType *STy = dyn_cast<llvm::StructType>(AI.getCoerceToType());
      if (AI.isDirect() && AI.getCanBeFlattened() && STy) {
        IRArgs.NumberOfArgs = STy->getNumElements();
      } else {
        IRArgs.NumberOfArgs = 1;
      }
      break;
    }
    case ABIArgInfo::Indirect:
    case ABIArgInfo::IndirectAliased:
      IRArgs.NumberOfArgs = 1;
      break;
    case ABIArgInfo::Ignore:
      IRArgs.NumberOfArgs = 0;
      break;
    case ABIArgInfo::CoerceAndExpand:
      IRArgs.NumberOfArgs = AI.getCoerceAndExpandTypeSequence().size();
      break;
    case ABIArgInfo::Expand:
      IRArgs.NumberOfArgs = getExpansionSize(ArgType, Context);
      break;
    }

    if (IRArgs.NumberOfArgs > 0) {
      IRArgs.FirstArgIndex = IRArgNo;
      IRArgNo += IRArgs.NumberOfArgs;
    }

    // Skip over the sret parameter when it comes second.  We already handled it
    // above.
    if (IRArgNo == 1 && SwapThisWithSRet)
      IRArgNo++;
  }
  assert(ArgNo == ArgInfo.size());

  TotalIRArgs = IRArgNo;
}
} // namespace

/***/

bool ModuleEmitter::returnTypeUsesSRet(const ABIFunctionInfo &FI) {
  const auto &RI = FI.getReturnInfo();
  return RI.isIndirect();
}

bool ModuleEmitter::returnSlotInterferesWithArgs(const ABIFunctionInfo &FI) {
  return returnTypeUsesSRet(FI) &&
         getTargetCodeGenInfo().doesReturnSlotInterfereWithArgs();
}

llvm::FunctionType *TypeEmitter::GetFunctionType(GlobalDecl GD) {
  const ABIFunctionInfo &FI = arrangeGlobalDeclaration(GD);
  return GetFunctionType(FI);
}

llvm::FunctionType *TypeEmitter::GetFunctionType(const ABIFunctionInfo &FI) {

  bool Inserted = FunctionsBeingProcessed.insert(&FI).second;
  (void)Inserted;
  assert(Inserted && "Recursively being processed?");

  llvm::Type *resultType = nullptr;
  const ABIArgInfo &retAI = FI.getReturnInfo();
  switch (retAI.getKind()) {
  case ABIArgInfo::Expand:
  case ABIArgInfo::IndirectAliased:
    llvm_unreachable("Invalid ABI kind for return argument");

  case ABIArgInfo::Extend:
  case ABIArgInfo::Direct:
    resultType = retAI.getCoerceToType();
    break;

  case ABIArgInfo::Indirect:
  case ABIArgInfo::Ignore:
    resultType = llvm::Type::getVoidTy(getLLVMContext());
    break;

  case ABIArgInfo::CoerceAndExpand:
    resultType = retAI.getUnpaddedCoerceAndExpandType();
    break;
  }

  FrontendToLLVMArgMapping IRFunctionArgs(getContext(), FI, true);
  llvm::SmallVector<llvm::Type *, 8> ArgTypes(IRFunctionArgs.totalIRArgs());

  if (IRFunctionArgs.hasSRetArg()) {
    QualType Ret = FI.getReturnType();
    unsigned AddressSpace = ME.getTypes().getTargetAddressSpace(Ret);
    ArgTypes[IRFunctionArgs.getSRetArgNo()] =
        llvm::PointerType::get(getLLVMContext(), AddressSpace);
  }

  unsigned ArgNo = 0;
  ABIFunctionInfo::const_arg_iterator it = FI.arg_begin(),
                                      ie = it + FI.getNumRequiredArgs();
  for (; it != ie; ++it, ++ArgNo) {
    const ABIArgInfo &ArgInfo = it->info;

    // Insert a padding type to ensure proper alignment.
    if (IRFunctionArgs.hasPaddingArg(ArgNo))
      ArgTypes[IRFunctionArgs.getPaddingArgNo(ArgNo)] =
          ArgInfo.getPaddingType();

    unsigned FirstIRArg, NumIRArgs;
    std::tie(FirstIRArg, NumIRArgs) = IRFunctionArgs.getIRArgs(ArgNo);

    switch (ArgInfo.getKind()) {
    case ABIArgInfo::Ignore:
      assert(NumIRArgs == 0);
      break;

    case ABIArgInfo::Indirect:
      assert(NumIRArgs == 1);
      // indirect arguments are always on the stack, which is alloca addr space.
      ArgTypes[FirstIRArg] = llvm::PointerType::get(
          getLLVMContext(), ME.getDataLayout().getAllocaAddrSpace());
      break;
    case ABIArgInfo::IndirectAliased:
      assert(NumIRArgs == 1);
      ArgTypes[FirstIRArg] = llvm::PointerType::get(
          getLLVMContext(), ArgInfo.getIndirectAddrSpace());
      break;
    case ABIArgInfo::Extend:
    case ABIArgInfo::Direct: {
      // Fast-isel and the optimizer generally like scalar values better than
      // FCAs, so we flatten them if this is safe to do for this argument.
      llvm::Type *argType = ArgInfo.getCoerceToType();
      llvm::StructType *st = dyn_cast<llvm::StructType>(argType);
      if (st && ArgInfo.isDirect() && ArgInfo.getCanBeFlattened()) {
        assert(NumIRArgs == st->getNumElements());
        for (unsigned i = 0, e = st->getNumElements(); i != e; ++i)
          ArgTypes[FirstIRArg + i] = st->getElementType(i);
      } else {
        assert(NumIRArgs == 1);
        ArgTypes[FirstIRArg] = argType;
      }
      break;
    }

    case ABIArgInfo::CoerceAndExpand: {
      auto ArgTypesIter = ArgTypes.begin() + FirstIRArg;
      for (auto *EltTy : ArgInfo.getCoerceAndExpandTypeSequence()) {
        *ArgTypesIter++ = EltTy;
      }
      assert(ArgTypesIter == ArgTypes.begin() + FirstIRArg + NumIRArgs);
      break;
    }

    case ABIArgInfo::Expand:
      auto ArgTypesIter = ArgTypes.begin() + FirstIRArg;
      getExpandedTypes(it->type, ArgTypesIter);
      assert(ArgTypesIter == ArgTypes.begin() + FirstIRArg + NumIRArgs);
      break;
    }
  }

  bool Erased = FunctionsBeingProcessed.erase(&FI);
  (void)Erased;
  assert(Erased && "Not in set?");

  return llvm::FunctionType::get(resultType, ArgTypes, FI.isVariadic());
}

namespace {

void addAttributesFromFunctionProtoType(TreeContext &Ctx,
                                        llvm::AttrBuilder &FuncAttrs,
                                        const FunctionProtoType *FPT) {
  if (!FPT)
    return;

  if (FPT->isNothrow())
    FuncAttrs.addAttribute(llvm::Attribute::NoUnwind);

  if (FPT->getAArch64SMEAttributes() & FunctionType::SME_PStateSMEnabledMask)
    FuncAttrs.addAttribute("aarch64_pstate_sm_enabled");
  if (FPT->getAArch64SMEAttributes() & FunctionType::SME_PStateSMCompatibleMask)
    FuncAttrs.addAttribute("aarch64_pstate_sm_compatible");
  if (FPT->getAArch64SMEAttributes() & FunctionType::SME_PStateZASharedMask)
    FuncAttrs.addAttribute("aarch64_pstate_za_shared");
  if (FPT->getAArch64SMEAttributes() & FunctionType::SME_PStateZAPreservedMask)
    FuncAttrs.addAttribute("aarch64_pstate_za_preserved");
}

void addAttributesFromAssumes(llvm::AttrBuilder &FuncAttrs,
                              const Decl *Callee) {
  if (!Callee)
    return;

  llvm::SmallVector<llvm::StringRef, 4> Attrs;

  for (const AssumptionAttr *AA : Callee->specific_attrs<AssumptionAttr>())
    AA->getAssumption().split(Attrs, ",");

  if (!Attrs.empty())
    FuncAttrs.addAttribute(llvm::AssumptionAttrKey,
                           llvm::join(Attrs.begin(), Attrs.end(), ","));
}
} // namespace

bool ModuleEmitter::mayDropFunctionReturn(const TreeContext &Context,
                                          QualType ReturnType) const {
  return ReturnType.isTriviallyCopyableType(Context);
}

namespace {
void addDenormalModeAttrs(llvm::DenormalMode FPDenormalMode,
                          llvm::DenormalMode FP32DenormalMode,
                          llvm::AttrBuilder &FuncAttrs) {
  if (FPDenormalMode != llvm::DenormalMode::getDefault())
    FuncAttrs.addAttribute("denormal-fp-math", FPDenormalMode.str());

  if (FP32DenormalMode != FPDenormalMode && FP32DenormalMode.isValid())
    FuncAttrs.addAttribute("denormal-fp-math-f32", FP32DenormalMode.str());
}

void addMergableDefaultFunctionAttributes(const CodeGenOptions &CodeGenOpts,
                                          llvm::AttrBuilder &FuncAttrs) {
  addDenormalModeAttrs(CodeGenOpts.FPDenormalMode, CodeGenOpts.FP32DenormalMode,
                       FuncAttrs);
}

void getTrivialDefaultFunctionAttributes(llvm::StringRef Name, bool HasOptnone,
                                         const CodeGenOptions &CodeGenOpts,
                                         const LangOptions &LangOpts,
                                         bool AttrOnCallSite,
                                         llvm::AttrBuilder &FuncAttrs) {
  // OptimizeNoneAttr takes precedence over -Os or -Oz. No warning needed.
  if (!HasOptnone) {
    if (CodeGenOpts.OptimizeSize)
      FuncAttrs.addAttribute(llvm::Attribute::OptimizeForSize);
    if (CodeGenOpts.OptimizeSize == 2)
      FuncAttrs.addAttribute(llvm::Attribute::MinSize);
  }

  if (CodeGenOpts.DisableRedZone)
    FuncAttrs.addAttribute(llvm::Attribute::NoRedZone);
  if (CodeGenOpts.IndirectTlsSegRefs)
    FuncAttrs.addAttribute("indirect-tls-seg-refs");
  if (CodeGenOpts.NoImplicitFloat)
    FuncAttrs.addAttribute(llvm::Attribute::NoImplicitFloat);

  if (AttrOnCallSite) {
    // Attributes that should go on the call site only.
    if (!CodeGenOpts.SimplifyLibCalls || LangOpts.isNoBuiltinFunc(Name))
      FuncAttrs.addAttribute(llvm::Attribute::NoBuiltin);
    if (!CodeGenOpts.TrapFuncName.empty())
      FuncAttrs.addAttribute("trap-func-name", CodeGenOpts.TrapFuncName);
  } else {
    switch (CodeGenOpts.getFramePointer()) {
    case CodeGenOptions::FramePointerKind::None:
      // This is the default behavior.
      break;
    case CodeGenOptions::FramePointerKind::NonLeaf:
    case CodeGenOptions::FramePointerKind::All:
      FuncAttrs.addAttribute("frame-pointer",
                             CodeGenOptions::getFramePointerKindName(
                                 CodeGenOpts.getFramePointer()));
    }

    if (CodeGenOpts.NullPointerIsValid)
      FuncAttrs.addAttribute(llvm::Attribute::NullPointerIsValid);

    if (LangOpts.getDefaultExceptionMode() == LangOptions::FPE_Ignore)
      FuncAttrs.addAttribute("no-trapping-math", "true");

    if (LangOpts.NoHonorInfs)
      FuncAttrs.addAttribute("no-infs-fp-math", "true");
    if (LangOpts.NoHonorNaNs)
      FuncAttrs.addAttribute("no-nans-fp-math", "true");
    if (LangOpts.ApproxFunc)
      FuncAttrs.addAttribute("approx-func-fp-math", "true");
    if (LangOpts.AllowFPReassoc && LangOpts.AllowRecip &&
        LangOpts.NoSignedZero && LangOpts.ApproxFunc &&
        (LangOpts.getDefaultFPContractMode() ==
             LangOptions::FPModeKind::FPM_Fast ||
         LangOpts.getDefaultFPContractMode() ==
             LangOptions::FPModeKind::FPM_FastHonorPragmas))
      FuncAttrs.addAttribute("unsafe-fp-math", "true");
    if (CodeGenOpts.SoftFloat)
      FuncAttrs.addAttribute("use-soft-float", "true");
    FuncAttrs.addAttribute("stack-protector-buffer-size",
                           llvm::utostr(CodeGenOpts.SSPBufferSize));
    if (LangOpts.NoSignedZero)
      FuncAttrs.addAttribute("no-signed-zeros-fp-math", "true");

    const std::vector<std::string> &Recips = CodeGenOpts.Reciprocals;
    if (!Recips.empty())
      FuncAttrs.addAttribute("reciprocal-estimates", llvm::join(Recips, ","));

    if (!CodeGenOpts.PreferVectorWidth.empty() &&
        CodeGenOpts.PreferVectorWidth != "none")
      FuncAttrs.addAttribute("prefer-vector-width",
                             CodeGenOpts.PreferVectorWidth);

    if (CodeGenOpts.StackRealignment)
      FuncAttrs.addAttribute("stackrealign");
    if (CodeGenOpts.EnableSegmentedStacks)
      FuncAttrs.addAttribute("split-stack");

    if (CodeGenOpts.SpeculativeLoadHardening)
      FuncAttrs.addAttribute(llvm::Attribute::SpeculativeLoadHardening);

    switch (CodeGenOpts.getZeroCallUsedRegs()) {
    case llvm::ZeroCallUsedRegs::ZeroCallUsedRegsKind::Skip:
      FuncAttrs.removeAttribute("zero-call-used-regs");
      break;
    case llvm::ZeroCallUsedRegs::ZeroCallUsedRegsKind::UsedGPRArg:
      FuncAttrs.addAttribute("zero-call-used-regs", "used-gpr-arg");
      break;
    case llvm::ZeroCallUsedRegs::ZeroCallUsedRegsKind::UsedGPR:
      FuncAttrs.addAttribute("zero-call-used-regs", "used-gpr");
      break;
    case llvm::ZeroCallUsedRegs::ZeroCallUsedRegsKind::UsedArg:
      FuncAttrs.addAttribute("zero-call-used-regs", "used-arg");
      break;
    case llvm::ZeroCallUsedRegs::ZeroCallUsedRegsKind::Used:
      FuncAttrs.addAttribute("zero-call-used-regs", "used");
      break;
    case llvm::ZeroCallUsedRegs::ZeroCallUsedRegsKind::AllGPRArg:
      FuncAttrs.addAttribute("zero-call-used-regs", "all-gpr-arg");
      break;
    case llvm::ZeroCallUsedRegs::ZeroCallUsedRegsKind::AllGPR:
      FuncAttrs.addAttribute("zero-call-used-regs", "all-gpr");
      break;
    case llvm::ZeroCallUsedRegs::ZeroCallUsedRegsKind::AllArg:
      FuncAttrs.addAttribute("zero-call-used-regs", "all-arg");
      break;
    case llvm::ZeroCallUsedRegs::ZeroCallUsedRegsKind::All:
      FuncAttrs.addAttribute("zero-call-used-regs", "all");
      break;
    }
  }

  for (llvm::StringRef Attr : CodeGenOpts.DefaultFunctionAttrs) {
    llvm::StringRef Var, Value;
    std::tie(Var, Value) = Attr.split('=');
    FuncAttrs.addAttribute(Var, Value);
  }
}

void overrideFunctionFeaturesWithTargetFeatures(
    llvm::AttrBuilder &FuncAttr, const llvm::Function &F,
    const TargetOptions &TargetOpts) {
  auto FFeatures = F.getFnAttribute("target-features");

  llvm::StringSet<> MergedNames;
  llvm::SmallVector<llvm::StringRef> MergedFeatures;
  MergedFeatures.reserve(TargetOpts.Features.size());

  auto AddUnmergedFeatures = [&](auto &&FeatureRange) {
    for (llvm::StringRef Feature : FeatureRange) {
      if (Feature.empty())
        continue;
      assert(Feature[0] == '+' || Feature[0] == '-');
      llvm::StringRef Name = Feature.drop_front(1);
      bool Merged = !MergedNames.insert(Name).second;
      if (!Merged)
        MergedFeatures.push_back(Feature);
    }
  };

  if (FFeatures.isValid())
    AddUnmergedFeatures(llvm::split(FFeatures.getValueAsString(), ','));
  AddUnmergedFeatures(TargetOpts.Features);

  if (!MergedFeatures.empty()) {
    llvm::sort(MergedFeatures);
    FuncAttr.addAttribute("target-features", llvm::join(MergedFeatures, ","));
  }
}
} // namespace

// ===----------------------------------------------------------------------===
// Default function attributes
// ===----------------------------------------------------------------------===

void Emit::mergeDefaultFunctionDefinitionAttributes(
    llvm::Function &F, const CodeGenOptions &CodeGenOpts,
    const LangOptions &LangOpts, const TargetOptions &TargetOpts,
    bool WillInternalize) {

  llvm::AttrBuilder FuncAttrs(F.getContext());
  // Here we only extract the options that are relevant compared to the version
  // from getCPUAndFeaturesAttributes.
  if (!TargetOpts.CPU.empty())
    FuncAttrs.addAttribute("target-cpu", TargetOpts.CPU);
  if (!TargetOpts.TuneCPU.empty())
    FuncAttrs.addAttribute("tune-cpu", TargetOpts.TuneCPU);

  ::getTrivialDefaultFunctionAttributes(F.getName(), F.hasOptNone(),
                                        CodeGenOpts, LangOpts,
                                        /*AttrOnCallSite=*/false, FuncAttrs);

  if (!WillInternalize && F.isInterposable()) {
    // Do not promote "dynamic" denormal-fp-math to this translation unit's
    // setting for weak functions that won't be internalized. The user has no
    // real control for how builtin bitcode is linked, so we shouldn't assume
    // later copies will use a consistent mode.
    F.addFnAttrs(FuncAttrs);
    return;
  }

  llvm::AttributeMask AttrsToRemove;

  llvm::DenormalMode DenormModeToMerge = F.getDenormalModeRaw();
  llvm::DenormalMode DenormModeToMergeF32 = F.getDenormalModeF32Raw();
  llvm::DenormalMode Merged =
      CodeGenOpts.FPDenormalMode.mergeCalleeMode(DenormModeToMerge);
  llvm::DenormalMode MergedF32 = CodeGenOpts.FP32DenormalMode;

  if (DenormModeToMergeF32.isValid()) {
    MergedF32 =
        CodeGenOpts.FP32DenormalMode.mergeCalleeMode(DenormModeToMergeF32);
  }

  if (Merged == llvm::DenormalMode::getDefault()) {
    AttrsToRemove.addAttribute("denormal-fp-math");
  } else if (Merged != DenormModeToMerge) {
    // Overwrite existing attribute
    FuncAttrs.addAttribute("denormal-fp-math",
                           CodeGenOpts.FPDenormalMode.str());
  }

  if (MergedF32 == llvm::DenormalMode::getDefault()) {
    AttrsToRemove.addAttribute("denormal-fp-math-f32");
  } else if (MergedF32 != DenormModeToMergeF32) {
    // Overwrite existing attribute
    FuncAttrs.addAttribute("denormal-fp-math-f32",
                           CodeGenOpts.FP32DenormalMode.str());
  }

  F.removeFnAttrs(AttrsToRemove);
  addDenormalModeAttrs(Merged, MergedF32, FuncAttrs);

  overrideFunctionFeaturesWithTargetFeatures(FuncAttrs, F, TargetOpts);

  F.addFnAttrs(FuncAttrs);
}

void ModuleEmitter::getTrivialDefaultFunctionAttributes(
    llvm::StringRef Name, bool HasOptnone, bool AttrOnCallSite,
    llvm::AttrBuilder &FuncAttrs) {
  ::getTrivialDefaultFunctionAttributes(Name, HasOptnone, getCodeGenOpts(),
                                        getLangOpts(), AttrOnCallSite,
                                        FuncAttrs);
}

void ModuleEmitter::getDefaultFunctionAttributes(llvm::StringRef Name,
                                                 bool HasOptnone,
                                                 bool AttrOnCallSite,
                                                 llvm::AttrBuilder &FuncAttrs) {
  getTrivialDefaultFunctionAttributes(Name, HasOptnone, AttrOnCallSite,
                                      FuncAttrs);
  // If we're just getting the default, get the default values for mergeable
  // attributes.
  if (!AttrOnCallSite)
    addMergableDefaultFunctionAttributes(CodeGenOpts, FuncAttrs);
}

void ModuleEmitter::addDefaultFunctionDefinitionAttributes(
    llvm::AttrBuilder &attrs) {
  getDefaultFunctionAttributes(/*function name*/ "", /*optnone*/ false,
                               /*for call*/ false, attrs);
  getCPUAndFeaturesAttributes(GlobalDecl(), attrs);
}

namespace {
void addNoBuiltinAttributes(llvm::AttrBuilder &FuncAttrs,
                            const LangOptions &LangOpts,
                            const NoBuiltinAttr *NBA = nullptr) {
  auto AddNoBuiltinAttr = [&FuncAttrs](llvm::StringRef BuiltinName) {
    llvm::SmallString<32> AttributeName;
    AttributeName += "no-builtin-";
    AttributeName += BuiltinName;
    FuncAttrs.addAttribute(AttributeName);
  };

  // First, handle the language options passed through -fno-builtin.
  if (LangOpts.NoBuiltin) {
    // -fno-builtin disables them all.
    FuncAttrs.addAttribute("no-builtins");
    return;
  }

  // Then, add attributes for builtins specified through -fno-builtin-<name>.
  llvm::for_each(LangOpts.NoBuiltinFuncs, AddNoBuiltinAttr);

  // Now, let's check the __attribute__((no_builtin("...")) attribute added to
  // the source.
  if (!NBA)
    return;

  // If there is a wildcard in the builtin names specified through the
  // attribute, disable them all.
  if (llvm::is_contained(NBA->builtinNames(), "*")) {
    FuncAttrs.addAttribute("no-builtins");
    return;
  }

  // And last, add the rest of the builtin names.
  llvm::for_each(NBA->builtinNames(), AddNoBuiltinAttr);
}

bool determineNoUndef(QualType QTy, TypeEmitter &Types,
                      const llvm::DataLayout &DL, const ABIArgInfo &AI,
                      bool CheckCoerce = true) {
  llvm::Type *Ty = Types.convertTypeForMem(QTy);
  if (AI.getKind() == ABIArgInfo::Indirect ||
      AI.getKind() == ABIArgInfo::IndirectAliased)
    return true;
  if (AI.getKind() == ABIArgInfo::Extend)
    return true;
  if (!DL.typeSizeEqualsStoreSize(Ty))
    return false;
  if (CheckCoerce && AI.canHaveCoerceToType()) {
    llvm::Type *CoerceTy = AI.getCoerceToType();
    if (llvm::TypeSize::isKnownGT(DL.getTypeSizeInBits(CoerceTy),
                                  DL.getTypeSizeInBits(Ty)))
      // If we're coercing to a type with a greater size than the canonical one,
      // we're introducing new undef bits.
      // Coercing to a type of smaller or equal size is ok, as we know that
      // there's no internal padding (typeSizeEqualsStoreSize).
      return false;
  }
  if (QTy->isBitIntType())
    return true;
  if (QTy->isNullPtrType())
    return false;
  if (QTy->isScalarType()) {
    if (const ComplexType *Complex = dyn_cast<ComplexType>(QTy))
      return determineNoUndef(Complex->getElementType(), Types, DL, AI, false);
    return true;
  }
  if (const VectorType *Vector = dyn_cast<VectorType>(QTy))
    return determineNoUndef(Vector->getElementType(), Types, DL, AI, false);
  if (const MatrixType *Matrix = dyn_cast<MatrixType>(QTy))
    return determineNoUndef(Matrix->getElementType(), Types, DL, AI, false);
  if (const ArrayType *Array = dyn_cast<ArrayType>(QTy))
    return determineNoUndef(Array->getElementType(), Types, DL, AI, false);

  return false;
}

bool isArgumentMaybeUndef(const Decl *TargetDecl, unsigned NumRequiredArgs,
                          unsigned ArgNo) {
  const auto *FD = dyn_cast_or_null<FunctionDecl>(TargetDecl);
  if (!FD)
    return false;

  // Assume variadic arguments do not have maybe_undef attribute.
  if (ArgNo >= NumRequiredArgs)
    return false;

  if (ArgNo < FD->getNumParams()) {
    const ParmVarDecl *Param = FD->getParamDecl(ArgNo);
    if (Param && Param->hasAttr<MaybeUndefAttr>())
      return true;
  }

  return false;
}

bool canApplyNoFPClass(const ABIArgInfo &AI, QualType ParamType,
                       bool IsReturn) {
  // Should only apply to FP types in the source, not ABI promoted.
  if (!ParamType->hasFloatingRepresentation())
    return false;

  // The promoted-to IR type also needs to support nofpclass.
  llvm::Type *IRTy = AI.getCoerceToType();
  if (llvm::AttributeFuncs::isNoFPClassCompatibleType(IRTy))
    return true;

  if (llvm::StructType *ST = dyn_cast<llvm::StructType>(IRTy)) {
    return !IsReturn && AI.getCanBeFlattened() &&
           llvm::all_of(ST->elements(), [](llvm::Type *Ty) {
             return llvm::AttributeFuncs::isNoFPClassCompatibleType(Ty);
           });
  }

  return false;
}

llvm::FPClassTest getNoFPClassTestMask(const LangOptions &LangOpts) {
  llvm::FPClassTest Mask = llvm::fcNone;
  if (LangOpts.NoHonorInfs)
    Mask |= llvm::fcInf;
  if (LangOpts.NoHonorNaNs)
    Mask |= llvm::fcNan;
  return Mask;
}
} // namespace

void ModuleEmitter::adjustMemoryAttribute(llvm::StringRef Name,
                                          FnCalleeInfo CalleeInfo,
                                          llvm::AttributeList &Attrs) {
  if (Attrs.getMemoryEffects().getModRef() == llvm::ModRefInfo::NoModRef) {
    Attrs = Attrs.removeFnAttribute(getLLVMContext(), llvm::Attribute::Memory);
    llvm::Attribute MemoryAttr = llvm::Attribute::getWithMemoryEffects(
        getLLVMContext(), llvm::MemoryEffects::writeOnly());
    Attrs = Attrs.addFnAttribute(getLLVMContext(), MemoryAttr);
  }
}

// ===----------------------------------------------------------------------===
// Attribute list construction
// ===----------------------------------------------------------------------===

__attribute__((hot)) void ModuleEmitter::constructAttributeList(
    llvm::StringRef Name, const ABIFunctionInfo &FI, FnCalleeInfo CalleeInfo,
    llvm::AttributeList &AttrList, unsigned &CallingConv, bool AttrOnCallSite) {
  llvm::AttrBuilder FuncAttrs(getLLVMContext());
  llvm::AttrBuilder RetAttrs(getLLVMContext());

  // Collect function IR attributes from the CC lowering.
  // We'll collect the paramete and result attributes later.
  CallingConv = FI.getEffectiveCallingConvention();
  if (FI.isNoReturn())
    FuncAttrs.addAttribute(llvm::Attribute::NoReturn);

  // Collect function IR attributes from the callee prototype if we have one.
  addAttributesFromFunctionProtoType(getContext(), FuncAttrs,
                                     CalleeInfo.getCalleeFunctionProtoType());

  const Decl *TargetDecl = CalleeInfo.getCalleeDecl().getDecl();

  // Attach assumption attributes to the declaration. If this is a call
  // site, attach assumptions from the caller to the call as well.
  addAttributesFromAssumes(FuncAttrs, TargetDecl);

  bool HasOptnone = false;
  // The NoBuiltinAttr attached to the target FunctionDecl.
  const NoBuiltinAttr *NBA = nullptr;

  // Some ABIs may result in additional accesses to arguments that may
  // otherwise not be present.
  auto AddPotentialArgAccess = [&]() {
    llvm::Attribute A = FuncAttrs.getAttribute(llvm::Attribute::Memory);
    if (A.isValid())
      FuncAttrs.addMemoryAttr(A.getMemoryEffects() |
                              llvm::MemoryEffects::argMemOnly());
  };

  // Collect function IR attributes based on declaration-specific
  // information.
  if (TargetDecl) {
    if (TargetDecl->hasAttr<ReturnsTwiceAttr>())
      FuncAttrs.addAttribute(llvm::Attribute::ReturnsTwice);
    if (TargetDecl->hasAttr<NoThrowAttr>())
      FuncAttrs.addAttribute(llvm::Attribute::NoUnwind);
    if (TargetDecl->hasAttr<NoReturnAttr>())
      FuncAttrs.addAttribute(llvm::Attribute::NoReturn);
    if (TargetDecl->hasAttr<ColdAttr>())
      FuncAttrs.addAttribute(llvm::Attribute::Cold);
    if (TargetDecl->hasAttr<HotAttr>())
      FuncAttrs.addAttribute(llvm::Attribute::Hot);
    if (TargetDecl->hasAttr<NoDuplicateAttr>())
      FuncAttrs.addAttribute(llvm::Attribute::NoDuplicate);
    if (TargetDecl->hasAttr<ConvergentAttr>())
      FuncAttrs.addAttribute(llvm::Attribute::Convergent);

    if (const FunctionDecl *Fn = dyn_cast<FunctionDecl>(TargetDecl)) {
      addAttributesFromFunctionProtoType(
          getContext(), FuncAttrs, Fn->getType()->getAs<FunctionProtoType>());
      if (Fn->isNoReturn())
        FuncAttrs.addAttribute(llvm::Attribute::NoReturn);
      NBA = Fn->getAttr<NoBuiltinAttr>();
    }

    if (isa<FunctionDecl>(TargetDecl) || isa<VarDecl>(TargetDecl)) {
      // Only place nomerge attribute on call sites, never functions. This
      // allows it to work on indirect function calls.
      if (AttrOnCallSite && TargetDecl->hasAttr<NoMergeAttr>())
        FuncAttrs.addAttribute(llvm::Attribute::NoMerge);
    }

    // 'const', 'pure' and 'noalias' attributed functions are also nounwind.
    if (TargetDecl->hasAttr<ConstAttr>()) {
      FuncAttrs.addMemoryAttr(llvm::MemoryEffects::none());
      FuncAttrs.addAttribute(llvm::Attribute::NoUnwind);
      // gcc specifies that 'const' functions have greater restrictions than
      // 'pure' functions, so they also cannot have infinite loops.
      FuncAttrs.addAttribute(llvm::Attribute::WillReturn);
    } else if (TargetDecl->hasAttr<PureAttr>()) {
      FuncAttrs.addMemoryAttr(llvm::MemoryEffects::readOnly());
      FuncAttrs.addAttribute(llvm::Attribute::NoUnwind);
      // gcc specifies that 'pure' functions cannot have infinite loops.
      FuncAttrs.addAttribute(llvm::Attribute::WillReturn);
    } else if (TargetDecl->hasAttr<NoAliasAttr>()) {
      FuncAttrs.addMemoryAttr(llvm::MemoryEffects::inaccessibleOrArgMemOnly());
      FuncAttrs.addAttribute(llvm::Attribute::NoUnwind);
    }
    if (TargetDecl->hasAttr<RestrictAttr>())
      RetAttrs.addAttribute(llvm::Attribute::NoAlias);
    if (TargetDecl->hasAttr<ReturnsNonNullAttr>() &&
        !CodeGenOpts.NullPointerIsValid)
      RetAttrs.addAttribute(llvm::Attribute::NonNull);
    if (TargetDecl->hasAttr<AnyX86NoCallerSavedRegistersAttr>())
      FuncAttrs.addAttribute("no_caller_saved_registers");
    if (TargetDecl->hasAttr<AnyX86NoCfCheckAttr>())
      FuncAttrs.addAttribute(llvm::Attribute::NoCfCheck);
    if (TargetDecl->hasAttr<LeafAttr>())
      FuncAttrs.addAttribute(llvm::Attribute::NoCallback);

    HasOptnone = TargetDecl->hasAttr<OptimizeNoneAttr>();
    if (auto *AllocSize = TargetDecl->getAttr<AllocSizeAttr>()) {
      std::optional<unsigned> NumElemsParam;
      if (AllocSize->getNumElemsParam().isValid())
        NumElemsParam = AllocSize->getNumElemsParam().getLLVMIndex();
      FuncAttrs.addAllocSizeAttr(AllocSize->getElemSizeParam().getLLVMIndex(),
                                 NumElemsParam);
    }

    if (TargetDecl->hasAttr<ArmLocallyStreamingAttr>())
      FuncAttrs.addAttribute("aarch64_pstate_sm_body");

    if (TargetDecl->hasAttr<ArmNewZAAttr>())
      FuncAttrs.addAttribute("aarch64_pstate_za_new");
  }

  // Attach "no-builtins" attributes to:
  // * call sites: both `nobuiltin` and "no-builtins" or "no-builtin-<name>".
  // * definitions: "no-builtins" or "no-builtin-<name>" only.
  // The attributes can come from:
  // * LangOpts: -ffreestanding, -fno-builtin, -fno-builtin-<name>
  // * FunctionDecl attributes: __attribute__((no_builtin(...)))
  addNoBuiltinAttributes(FuncAttrs, getLangOpts(), NBA);

  // Collect function IR attributes based on global settiings.
  getDefaultFunctionAttributes(Name, HasOptnone, AttrOnCallSite, FuncAttrs);

  // Override some default IR attributes based on declaration-specific
  // information.
  if (TargetDecl) {
    if (TargetDecl->hasAttr<NoSpeculativeLoadHardeningAttr>())
      FuncAttrs.removeAttribute(llvm::Attribute::SpeculativeLoadHardening);
    if (TargetDecl->hasAttr<SpeculativeLoadHardeningAttr>())
      FuncAttrs.addAttribute(llvm::Attribute::SpeculativeLoadHardening);
    if (TargetDecl->hasAttr<NoSplitStackAttr>())
      FuncAttrs.removeAttribute("split-stack");
    if (TargetDecl->hasAttr<ZeroCallUsedRegsAttr>()) {
      // A function "__attribute__((...))" overrides the command-line flag.
      auto Kind =
          TargetDecl->getAttr<ZeroCallUsedRegsAttr>()->getZeroCallUsedRegs();
      FuncAttrs.removeAttribute("zero-call-used-regs");
      FuncAttrs.addAttribute(
          "zero-call-used-regs",
          ZeroCallUsedRegsAttr::ConvertZeroCallUsedRegsKindToStr(Kind));
    }

    if (CodeGenOpts.NoPLT) {
      if (auto *Fn = dyn_cast<FunctionDecl>(TargetDecl)) {
        if (!Fn->isDefined() && !AttrOnCallSite) {
          FuncAttrs.addAttribute(llvm::Attribute::NonLazyBind);
        }
      }
    }
  }

  if (TargetDecl && CodeGenOpts.UniqueInternalLinkageNames) {
    if (const auto *FD = dyn_cast_or_null<FunctionDecl>(TargetDecl)) {
      if (!FD->isExternallyVisible())
        FuncAttrs.addAttribute("sample-profile-suffix-elision-policy",
                               "selected");
    }
  }

  if (!AttrOnCallSite) {
    auto shouldDisableTailCalls = [&] {
      if (CodeGenOpts.DisableTailCalls)
        return true;

      if (!TargetDecl)
        return false;

      if (TargetDecl->hasAttr<DisableTailCallsAttr>() ||
          TargetDecl->hasAttr<AnyX86InterruptAttr>())
        return true;

      return false;
    };
    if (shouldDisableTailCalls())
      FuncAttrs.addAttribute("disable-tail-calls", "true");

    getCPUAndFeaturesAttributes(CalleeInfo.getCalleeDecl(), FuncAttrs);
  }

  FrontendToLLVMArgMapping IRFunctionArgs(getContext(), FI);

  QualType RetTy = FI.getReturnType();
  const ABIArgInfo &RetAI = FI.getReturnInfo();
  const llvm::DataLayout &DL = getDataLayout();

  switch (RetAI.getKind()) {
  case ABIArgInfo::Extend:
    if (RetAI.isSignExt())
      RetAttrs.addAttribute(llvm::Attribute::SExt);
    else
      RetAttrs.addAttribute(llvm::Attribute::ZExt);
    [[fallthrough]];
  case ABIArgInfo::Direct:
    if (RetAI.getInReg())
      RetAttrs.addAttribute(llvm::Attribute::InReg);

    if (canApplyNoFPClass(RetAI, RetTy, true))
      RetAttrs.addNoFPClassAttr(getNoFPClassTestMask(getLangOpts()));

    break;
  case ABIArgInfo::Ignore:
    break;

  case ABIArgInfo::Indirect: {
    // sret disables readnone and readonly
    AddPotentialArgAccess();
    break;
  }

  case ABIArgInfo::CoerceAndExpand:
    break;

  case ABIArgInfo::Expand:
  case ABIArgInfo::IndirectAliased:
    llvm_unreachable("Invalid ABI kind for return argument");
  }

  llvm::SmallVector<llvm::AttributeSet, 4> ArgAttrs(
      IRFunctionArgs.totalIRArgs());

  // Attach attributes to sret.
  if (IRFunctionArgs.hasSRetArg()) {
    llvm::AttrBuilder SRETAttrs(getLLVMContext());
    SRETAttrs.addStructRetAttr(getTypes().convertTypeForMem(RetTy));
    if (RetAI.getInReg())
      SRETAttrs.addAttribute(llvm::Attribute::InReg);
    SRETAttrs.addAlignmentAttr(RetAI.getIndirectAlign().getQuantity());
    ArgAttrs[IRFunctionArgs.getSRetArgNo()] =
        llvm::AttributeSet::get(getLLVMContext(), SRETAttrs);
  }

  unsigned ArgNo = 0;
  for (ABIFunctionInfo::const_arg_iterator I = FI.arg_begin(), E = FI.arg_end();
       I != E; ++I, ++ArgNo) {
    QualType ParamType = I->type;
    const ABIArgInfo &AI = I->info;
    llvm::AttrBuilder Attrs(getLLVMContext());

    if (IRFunctionArgs.hasPaddingArg(ArgNo)) {
      if (AI.getPaddingInReg()) {
        ArgAttrs[IRFunctionArgs.getPaddingArgNo(ArgNo)] =
            llvm::AttributeSet::get(getLLVMContext(),
                                    llvm::AttrBuilder(getLLVMContext())
                                        .addAttribute(llvm::Attribute::InReg));
      }
    }

    // Decide whether the argument we're handling could be partially undef
    if (CodeGenOpts.EnableNoundefAttrs &&
        determineNoUndef(ParamType, getTypes(), DL, AI)) {
      Attrs.addAttribute(llvm::Attribute::NoUndef);
    }

    // 'restrict' -> 'noalias' is done in genFunctionProlog when we
    // have the corresponding parameter variable.  It doesn't make
    // sense to do it here because parameters are so messed up.
    switch (AI.getKind()) {
    case ABIArgInfo::Extend:
      if (AI.isSignExt())
        Attrs.addAttribute(llvm::Attribute::SExt);
      else
        Attrs.addAttribute(llvm::Attribute::ZExt);
      [[fallthrough]];
    case ABIArgInfo::Direct:
      if (ArgNo == 0 && FI.isChainCall())
        Attrs.addAttribute(llvm::Attribute::Nest);
      else if (AI.getInReg())
        Attrs.addAttribute(llvm::Attribute::InReg);
      Attrs.addStackAlignmentAttr(llvm::MaybeAlign(AI.getDirectAlign()));

      if (canApplyNoFPClass(AI, ParamType, false))
        Attrs.addNoFPClassAttr(getNoFPClassTestMask(getLangOpts()));
      break;
    case ABIArgInfo::Indirect: {
      if (AI.getInReg())
        Attrs.addAttribute(llvm::Attribute::InReg);

      if (AI.getIndirectByVal())
        Attrs.addByValAttr(getTypes().convertTypeForMem(ParamType));

      CharUnits Align = AI.getIndirectAlign();

      // In a byval argument, it is important that the required
      // alignment of the type is honored, as LLVM might be creating a
      // *new* stack object, and needs to know what alignment to give
      // it. (Sometimes it can deduce a sensible alignment on its own,
      // but not if NeverC decides it must emit a packed struct, or the
      // user specifies increased alignment requirements.)
      //
      // This is different from indirect *not* byval, where the object
      // exists already, and the align attribute is purely
      // informative.
      assert(!Align.isZero());

      // For now, only add this when we have a byval argument.
      if (AI.getIndirectByVal())
        Attrs.addAlignmentAttr(Align.getQuantity());

      // byval disables readnone and readonly.
      AddPotentialArgAccess();
      break;
    }
    case ABIArgInfo::IndirectAliased: {
      CharUnits Align = AI.getIndirectAlign();
      Attrs.addByRefAttr(getTypes().convertTypeForMem(ParamType));
      Attrs.addAlignmentAttr(Align.getQuantity());
      break;
    }
    case ABIArgInfo::Ignore:
    case ABIArgInfo::Expand:
    case ABIArgInfo::CoerceAndExpand:
      break;
    }

    if (FI.getExtParameterInfo(ArgNo).isNoEscape())
      Attrs.addAttribute(llvm::Attribute::NoCapture);

    if (Attrs.hasAttributes()) {
      unsigned FirstIRArg, NumIRArgs;
      std::tie(FirstIRArg, NumIRArgs) = IRFunctionArgs.getIRArgs(ArgNo);
      for (unsigned i = 0; i < NumIRArgs; i++)
        ArgAttrs[FirstIRArg + i] = ArgAttrs[FirstIRArg + i].addAttributes(
            getLLVMContext(), llvm::AttributeSet::get(getLLVMContext(), Attrs));
    }
  }
  assert(ArgNo == FI.arg_size());

  AttrList = llvm::AttributeList::get(
      getLLVMContext(), llvm::AttributeSet::get(getLLVMContext(), FuncAttrs),
      llvm::AttributeSet::get(getLLVMContext(), RetAttrs), ArgAttrs);
}

// ===----------------------------------------------------------------------===
// Call argument helpers
// ===----------------------------------------------------------------------===

namespace {
llvm::Value *emitArgumentDemotion(FunctionEmitter &FE, const VarDecl *var,
                                  llvm::Value *value) {
  llvm::Type *varType = FE.convertType(var->getType());

  // This can happen with promotions that actually don't change the
  // underlying type, like the enum promotions.
  if (value->getType() == varType)
    return value;

  assert((varType->isIntegerTy() || varType->isFloatingPointTy()) &&
         "unexpected promotion type");

  if (isa<llvm::IntegerType>(varType))
    return FE.Builder.CreateTrunc(value, varType, "arg.unpromote");

  return FE.Builder.CreateFPCast(value, varType, "arg.unpromote");
}

const NonNullAttr *getNonNullAttr(const Decl *FD, const ParmVarDecl *PVD,
                                  QualType ArgType, unsigned ArgNo) {
  if (!ArgType->isAnyPointerType())
    return nullptr;
  // First, check attribute on parameter itself.
  if (PVD) {
    if (auto ParmNNAttr = PVD->getAttr<NonNullAttr>())
      return ParmNNAttr;
  }
  if (!FD)
    return nullptr;
  for (const auto *NNAttr : FD->specific_attrs<NonNullAttr>()) {
    if (NNAttr->isNonNull(ArgNo))
      return NNAttr;
  }
  return nullptr;
}
} // namespace

// ===----------------------------------------------------------------------===
// Function prolog & call emission
// ===----------------------------------------------------------------------===

__attribute__((hot)) void
FunctionEmitter::genFunctionProlog(const ABIFunctionInfo &FI,
                                   llvm::Function *Fn,
                                   const FunctionArgList &Args) {
  if (CurCodeDecl && CurCodeDecl->hasAttr<NakedAttr>())
    return;

  if (const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(CurCodeDecl)) {
    if (FD->hasImplicitReturnZero()) {
      QualType RetTy = FD->getReturnType().getUnqualifiedType();
      llvm::Type *LLVMTy = ME.getTypes().convertType(RetTy);
      llvm::Constant *Zero = llvm::Constant::getNullValue(LLVMTy);
      Builder.CreateStore(Zero, ReturnValue);
    }
  }

  FrontendToLLVMArgMapping IRFunctionArgs(ME.getContext(), FI);
  assert(Fn->arg_size() == IRFunctionArgs.totalIRArgs());

  if (IRFunctionArgs.hasSRetArg()) {
    auto AI = Fn->getArg(IRFunctionArgs.getSRetArgNo());
    AI->setName("agg.result");
    AI->addAttr(llvm::Attribute::NoAlias);
  }

  llvm::SmallVector<ParamValue, 16> ArgVals;
  ArgVals.reserve(Args.size());

  assert(FI.arg_size() == Args.size() &&
         "Mismatch between function signature & arguments.");
  unsigned ArgNo = 0;
  ABIFunctionInfo::const_arg_iterator info_it = FI.arg_begin();
  for (FunctionArgList::const_iterator i = Args.begin(), e = Args.end(); i != e;
       ++i, ++info_it, ++ArgNo) {
    const VarDecl *Arg = *i;
    const ABIArgInfo &ArgI = info_it->info;

    bool isPromoted =
        isa<ParmVarDecl>(Arg) && cast<ParmVarDecl>(Arg)->isKNRPromoted();
    // We are converting from ABIArgInfo type to VarDecl type directly, unless
    // the parameter is promoted. In this case we convert to
    // ABIFunctionInfo::ArgInfo type with subsequent argument demotion.
    QualType Ty = isPromoted ? info_it->type : Arg->getType();
    assert(hasScalarEvaluationKind(Ty) ==
           hasScalarEvaluationKind(Arg->getType()));

    unsigned FirstIRArg, NumIRArgs;
    std::tie(FirstIRArg, NumIRArgs) = IRFunctionArgs.getIRArgs(ArgNo);

    switch (ArgI.getKind()) {
    case ABIArgInfo::Indirect:
    case ABIArgInfo::IndirectAliased: {
      assert(NumIRArgs == 1);
      Address ParamAddr = Address(Fn->getArg(FirstIRArg), convertTypeForMem(Ty),
                                  ArgI.getIndirectAlign(), KnownNonNull);

      if (!hasScalarEvaluationKind(Ty)) {
        Address V = ParamAddr;
        if (ArgI.getIndirectRealign() || ArgI.isIndirectAliased()) {
          Address AlignedTemp = createMemTemp(Ty, "coerce");

          CharUnits Size = getContext().getTypeSizeInChars(Ty);
          Builder.CreateMemCpy(
              AlignedTemp.getPointer(), AlignedTemp.getAlignment().getAsAlign(),
              ParamAddr.getPointer(), ParamAddr.getAlignment().getAsAlign(),
              llvm::ConstantInt::get(IntPtrTy, Size.getQuantity()));
          V = AlignedTemp;
        }
        ArgVals.push_back(ParamValue::forIndirect(V));
      } else {
        llvm::Value *V =
            genLoadOfScalar(ParamAddr, false, Ty, Arg->getBeginLoc());

        if (isPromoted)
          V = emitArgumentDemotion(*this, Arg, V);
        ArgVals.push_back(ParamValue::forDirect(V));
      }
      break;
    }

    case ABIArgInfo::Extend:
    case ABIArgInfo::Direct: {
      auto AI = Fn->getArg(FirstIRArg);
      llvm::Type *LTy = convertType(Arg->getType());

      if (ArgI.getDirectOffset() == 0 && LTy->isPointerTy() &&
          ArgI.getCoerceToType()->isPointerTy()) {
        assert(NumIRArgs == 1);

        if (const ParmVarDecl *PVD = dyn_cast<ParmVarDecl>(Arg)) {
          if (getNonNullAttr(CurCodeDecl, PVD, PVD->getType(),
                             PVD->getFunctionScopeIndex()) &&
              !ME.getCodeGenOpts().NullPointerIsValid)
            AI->addAttr(llvm::Attribute::NonNull);

          QualType OTy = PVD->getOriginalType();
          if (const auto *ArrTy = getContext().getAsConstantArrayType(OTy)) {
            // A C99 array parameter declaration with the static keyword also
            // indicates dereferenceability, and if the size is constant we can
            // use the dereferenceable attribute (which requires the size in
            // bytes).
            if (ArrTy->getSizeModifier() == ArraySizeModifier::Static) {
              QualType ETy = ArrTy->getElementType();
              llvm::Align Alignment =
                  ME.getNaturalTypeAlignment(ETy).getAsAlign();
              AI->addAttrs(llvm::AttrBuilder(getLLVMContext())
                               .addAlignmentAttr(Alignment));
              uint64_t ArrSize = ArrTy->getSize().getZExtValue();
              if (!ETy->isIncompleteType() && ETy->isConstantSizeType() &&
                  ArrSize) {
                llvm::AttrBuilder Attrs(getLLVMContext());
                Attrs.addDereferenceableAttr(
                    getContext().getTypeSizeInChars(ETy).getQuantity() *
                    ArrSize);
                AI->addAttrs(Attrs);
              } else if (getContext().getTargetInfo().getNullPointerValue(
                             ETy.getAddressSpace()) == 0 &&
                         !ME.getCodeGenOpts().NullPointerIsValid) {
                AI->addAttr(llvm::Attribute::NonNull);
              }
            }
          } else if (const auto *ArrTy =
                         getContext().getAsVariableArrayType(OTy)) {
            // For C99 VLAs with the static keyword, we don't know the size so
            // we can't use the dereferenceable attribute, but in addrspace(0)
            // we know that it must be nonnull.
            if (ArrTy->getSizeModifier() == ArraySizeModifier::Static) {
              QualType ETy = ArrTy->getElementType();
              llvm::Align Alignment =
                  ME.getNaturalTypeAlignment(ETy).getAsAlign();
              AI->addAttrs(llvm::AttrBuilder(getLLVMContext())
                               .addAlignmentAttr(Alignment));
              if (!getTypes().getTargetAddressSpace(ETy) &&
                  !ME.getCodeGenOpts().NullPointerIsValid)
                AI->addAttr(llvm::Attribute::NonNull);
            }
          }

          const auto *AVAttr = PVD->getAttr<AlignValueAttr>();
          if (!AVAttr)
            if (const auto *TOTy = OTy->getAs<TypedefType>())
              AVAttr = TOTy->getDecl()->getAttr<AlignValueAttr>();
          if (AVAttr) {
            llvm::ConstantInt *AlignmentCI =
                cast<llvm::ConstantInt>(genScalarExpr(AVAttr->getAlignment()));
            uint64_t AlignmentInt =
                AlignmentCI->getLimitedValue(llvm::Value::MaximumAlignment);
            if (AI->getParamAlign().valueOrOne() < AlignmentInt) {
              AI->removeAttr(llvm::Attribute::AttrKind::Alignment);
              AI->addAttrs(llvm::AttrBuilder(getLLVMContext())
                               .addAlignmentAttr(llvm::Align(AlignmentInt)));
            }
          }
        }

        if (Arg->getType().isRestrictQualified())
          AI->addAttr(llvm::Attribute::NoAlias);
      }

      if (!isa<llvm::StructType>(ArgI.getCoerceToType()) &&
          ArgI.getCoerceToType() == convertType(Ty) &&
          ArgI.getDirectOffset() == 0) {
        assert(NumIRArgs == 1);

        llvm::Value *V = AI;

        if (V->getType() != ArgI.getCoerceToType())
          V = Builder.CreateBitCast(V, ArgI.getCoerceToType());

        if (isPromoted)
          V = emitArgumentDemotion(*this, Arg, V);

        // Because of merging of function types from multiple decls it is
        // possible for the type of an argument to not match the corresponding
        // type in the function type. Since we are codegening the callee
        // in here, add a cast to the argument type.
        llvm::Type *LTy = convertType(Arg->getType());
        if (V->getType() != LTy)
          V = Builder.CreateBitCast(V, LTy);

        ArgVals.push_back(ParamValue::forDirect(V));
        break;
      }

      // VLST arguments are coerced to VLATs at the function boundary for
      // ABI consistency. If this is a VLST that was coerced to
      // a VLAT at the function boundary and the types match up, use
      // llvm.vector.extract to convert back to the original VLST.
      if (auto *VecTyTo = dyn_cast<llvm::FixedVectorType>(convertType(Ty))) {
        llvm::Value *Coerced = Fn->getArg(FirstIRArg);
        if (auto *VecTyFrom =
                dyn_cast<llvm::ScalableVectorType>(Coerced->getType())) {
          // If we are casting a scalable 16 x i1 predicate vector to a fixed i8
          // vector, bitcast the source and use a vector extract.
          auto PredType =
              llvm::ScalableVectorType::get(Builder.getInt1Ty(), 16);
          if (VecTyFrom == PredType &&
              VecTyTo->getElementType() == Builder.getInt8Ty()) {
            VecTyFrom = llvm::ScalableVectorType::get(Builder.getInt8Ty(), 2);
            Coerced = Builder.CreateBitCast(Coerced, VecTyFrom);
          }
          if (VecTyFrom->getElementType() == VecTyTo->getElementType()) {
            llvm::Value *Zero = llvm::Constant::getNullValue(ME.Int64Ty);

            assert(NumIRArgs == 1);
            Coerced->setName(Arg->getName() + ".coerce");
            ArgVals.push_back(ParamValue::forDirect(Builder.CreateExtractVector(
                VecTyTo, Coerced, Zero, "cast.fixed")));
            break;
          }
        }
      }

      Address Alloca =
          createMemTemp(Ty, getContext().getDeclAlign(Arg), Arg->getName());

      // Pointer to store into.
      Address Ptr = emitAddressAtOffset(*this, Alloca, ArgI);

      // Fast-isel and the optimizer generally like scalar values better than
      // FCAs, so we flatten them if this is safe to do for this argument.
      llvm::StructType *STy =
          dyn_cast<llvm::StructType>(ArgI.getCoerceToType());
      if (ArgI.isDirect() && ArgI.getCanBeFlattened() && STy &&
          STy->getNumElements() > 1) {
        llvm::TypeSize StructSize = ME.getDataLayout().getTypeAllocSize(STy);
        llvm::TypeSize PtrElementSize =
            ME.getDataLayout().getTypeAllocSize(Ptr.getElementType());
        if (StructSize.isScalable()) {
          assert(STy->containsHomogeneousScalableVectorTypes() &&
                 "ABI only supports structure with homogeneous scalable vector "
                 "type");
          assert(StructSize == PtrElementSize &&
                 "Only allow non-fractional movement of structure with"
                 "homogeneous scalable vector type");
          assert(STy->getNumElements() == NumIRArgs);

          llvm::Value *LoadedStructValue = llvm::PoisonValue::get(STy);
          for (unsigned i = 0, e = STy->getNumElements(); i != e; ++i) {
            auto *AI = Fn->getArg(FirstIRArg + i);
            AI->setName(Arg->getName() + ".coerce" + llvm::Twine(i));
            LoadedStructValue =
                Builder.CreateInsertValue(LoadedStructValue, AI, i);
          }

          Builder.CreateStore(LoadedStructValue, Ptr);
        } else {
          uint64_t SrcSize = StructSize.getFixedValue();
          uint64_t DstSize = PtrElementSize.getFixedValue();

          Address AddrToStoreInto = Address::invalid();
          if (SrcSize <= DstSize) {
            AddrToStoreInto = Ptr.withElementType(STy);
          } else {
            AddrToStoreInto =
                createTempAlloca(STy, Alloca.getAlignment(), "coerce");
          }

          assert(STy->getNumElements() == NumIRArgs);
          for (unsigned i = 0, e = STy->getNumElements(); i != e; ++i) {
            auto AI = Fn->getArg(FirstIRArg + i);
            AI->setName(Arg->getName() + ".coerce" + llvm::Twine(i));
            Address EltPtr = Builder.CreateStructGEP(AddrToStoreInto, i);
            Builder.CreateStore(AI, EltPtr);
          }

          if (SrcSize > DstSize) {
            Builder.CreateMemCpy(Ptr, AddrToStoreInto, DstSize);
          }
        }
      } else {
        // Simple case, just do a coerced store of the argument into the alloca.
        assert(NumIRArgs == 1);
        auto AI = Fn->getArg(FirstIRArg);
        AI->setName(Arg->getName() + ".coerce");
        createCoercedStore(AI, Ptr, /*DstIsVolatile=*/false, *this);
      }

      // Match to what genParmDecl is expecting for this type.
      if (FunctionEmitter::hasScalarEvaluationKind(Ty)) {
        llvm::Value *V = genLoadOfScalar(Alloca, false, Ty, Arg->getBeginLoc());
        if (isPromoted)
          V = emitArgumentDemotion(*this, Arg, V);
        ArgVals.push_back(ParamValue::forDirect(V));
      } else {
        ArgVals.push_back(ParamValue::forIndirect(Alloca));
      }
      break;
    }

    case ABIArgInfo::CoerceAndExpand: {
      // Reconstruct into a temporary.
      Address alloca = createMemTemp(Ty, getContext().getDeclAlign(Arg));
      ArgVals.push_back(ParamValue::forIndirect(alloca));

      auto coercionType = ArgI.getCoerceAndExpandType();
      alloca = alloca.withElementType(coercionType);

      unsigned argIndex = FirstIRArg;
      for (unsigned i = 0, e = coercionType->getNumElements(); i != e; ++i) {
        llvm::Type *eltType = coercionType->getElementType(i);
        if (ABIArgInfo::isPaddingForCoerceAndExpand(eltType))
          continue;

        auto eltAddr = Builder.CreateStructGEP(alloca, i);
        auto elt = Fn->getArg(argIndex++);
        Builder.CreateStore(elt, eltAddr);
      }
      assert(argIndex == FirstIRArg + NumIRArgs);
      break;
    }

    case ABIArgInfo::Expand: {
      // If this structure was expanded into multiple arguments then
      // we need to create a temporary and reconstruct it from the
      // arguments.
      Address Alloca = createMemTemp(Ty, getContext().getDeclAlign(Arg));
      LValue LV = makeAddrLValue(Alloca, Ty);
      ArgVals.push_back(ParamValue::forIndirect(Alloca));

      auto FnArgIter = Fn->arg_begin() + FirstIRArg;
      expandTypeFromArgs(Ty, LV, FnArgIter);
      assert(FnArgIter == Fn->arg_begin() + FirstIRArg + NumIRArgs);
      for (unsigned i = 0, e = NumIRArgs; i != e; ++i) {
        auto AI = Fn->getArg(FirstIRArg + i);
        AI->setName(Arg->getName() + "." + llvm::Twine(i));
      }
      break;
    }

    case ABIArgInfo::Ignore:
      assert(NumIRArgs == 0);
      if (!hasScalarEvaluationKind(Ty)) {
        ArgVals.push_back(ParamValue::forIndirect(createMemTemp(Ty)));
      } else {
        llvm::Value *U = llvm::UndefValue::get(convertType(Arg->getType()));
        ArgVals.push_back(ParamValue::forDirect(U));
      }
      break;
    }
  }

  for (unsigned I = 0, E = Args.size(); I != E; ++I)
    genParmDecl(*Args[I], ArgVals[I], I + 1);
}

namespace {
llvm::StoreInst *findDominatingStoreToReturnValue(FunctionEmitter &FE) {
  // Check if a User is a store which pointerOperand is the ReturnValue.
  // We are looking for stores to the ReturnValue, not for stores of the
  // ReturnValue to some other location.
  auto GetStoreIfValid = [&FE](llvm::User *U) -> llvm::StoreInst * {
    auto *SI = dyn_cast<llvm::StoreInst>(U);
    if (!SI || SI->getPointerOperand() != FE.ReturnValue.getPointer() ||
        SI->getValueOperand()->getType() != FE.ReturnValue.getElementType())
      return nullptr;
    // These aren't actually possible for non-coerced returns, and we
    // only care about non-coerced returns on this code path.
    // All memory instructions inside __try block are volatile.
    assert(!SI->isAtomic() &&
           (!SI->isVolatile() || FE.currentFunctionUsesSEHTry()));
    return SI;
  };
  // If there are multiple uses of the return-value slot, just check
  // for something immediately preceding the IP.  Sometimes this can
  // happen with how we generate implicit-returns; it can also happen
  // with noreturn cleanups.
  if (!FE.ReturnValue.getPointer()->hasOneUse()) {
    llvm::BasicBlock *IP = FE.Builder.GetInsertBlock();
    if (IP->empty())
      return nullptr;

    // Look at directly preceding instruction, skipping bitcasts and lifetime
    // markers.
    for (llvm::Instruction &I : make_range(IP->rbegin(), IP->rend())) {
      if (isa<llvm::BitCastInst>(&I))
        continue;
      if (auto *II = dyn_cast<llvm::IntrinsicInst>(&I))
        if (II->getIntrinsicID() == llvm::Intrinsic::lifetime_end)
          continue;

      return GetStoreIfValid(&I);
    }
    return nullptr;
  }

  llvm::StoreInst *store =
      GetStoreIfValid(FE.ReturnValue.getPointer()->user_back());
  if (!store)
    return nullptr;

  // Now do a first-and-dirty dominance check: just walk up the
  // single-predecessors chain from the current insertion point.
  llvm::BasicBlock *StoreBB = store->getParent();
  llvm::BasicBlock *IP = FE.Builder.GetInsertBlock();
  llvm::SmallPtrSet<llvm::BasicBlock *, 4> SeenBBs;
  while (IP != StoreBB) {
    if (!SeenBBs.insert(IP).second || !(IP = IP->getSinglePredecessor()))
      return nullptr;
  }

  // Okay, the store's basic block dominates the insertion point; we
  // can do our thing.
  return store;
}
} // namespace

// ===----------------------------------------------------------------------===
// Function epilog
// ===----------------------------------------------------------------------===

void FunctionEmitter::genFunctionEpilog(const ABIFunctionInfo &FI,
                                        bool genRetDbgLoc,
                                        SourceLocation EndLoc) {
  if (FI.isNoReturn()) {
    genUnreachable(EndLoc);
    return;
  }

  if (CurCodeDecl && CurCodeDecl->hasAttr<NakedAttr>()) {
    // Naked functions don't have epilogues.
    Builder.CreateUnreachable();
    return;
  }

  // Functions with no result always return void.
  if (!ReturnValue.isValid()) {
    Builder.CreateRetVoid();
    return;
  }

  llvm::DebugLoc RetDbgLoc;
  llvm::Value *RV = nullptr;
  QualType RetTy = FI.getReturnType();
  const ABIArgInfo &RetAI = FI.getReturnInfo();

  switch (RetAI.getKind()) {
  case ABIArgInfo::Indirect: {
    auto AI = CurFn->arg_begin();
    if (RetAI.isSRetAfterThis())
      ++AI;
    switch (getEvaluationKind(RetTy)) {
    case TEK_Complex: {
      ComplexPairTy RT =
          genLoadOfComplex(makeAddrLValue(ReturnValue, RetTy), EndLoc);
      genStoreOfComplex(RT, makeNaturalAlignAddrLValue(&*AI, RetTy),
                        /*isInit*/ true);
      break;
    }
    case TEK_Aggregate:
      // Do nothing; aggregates get evaluated directly into the destination.
      break;
    case TEK_Scalar: {
      LValueBaseInfo BaseInfo;
      TBAAAccessInfo TBAAInfo;
      CharUnits Alignment =
          ME.getNaturalTypeAlignment(RetTy, &BaseInfo, &TBAAInfo);
      Address ArgAddr(&*AI, convertType(RetTy), Alignment);
      LValue ArgVal =
          LValue::MakeAddr(ArgAddr, RetTy, getContext(), BaseInfo, TBAAInfo);
      genStoreOfScalar(Builder.CreateLoad(ReturnValue), ArgVal,
                       /*isInit*/ true);
      break;
    }
    }
    break;
  }

  case ABIArgInfo::Extend:
  case ABIArgInfo::Direct:
    if (RetAI.getCoerceToType() == convertType(RetTy) &&
        RetAI.getDirectOffset() == 0) {
      // The internal return value temp always will have pointer-to-return-type
      // type, just do a load.

      // If there is a dominating store to ReturnValue, we can elide
      // the load, zap the store, and usually zap the alloca.
      if (llvm::StoreInst *SI = findDominatingStoreToReturnValue(*this)) {
        // Reuse the debug location from the store unless there is
        // cleanup code to be emitted between the store and return
        // instruction.
        if (genRetDbgLoc)
          RetDbgLoc = SI->getDebugLoc();
        RV = SI->getValueOperand();
        SI->eraseFromParent();

        // Otherwise, we have to do a simple load.
      } else {
        RV = Builder.CreateLoad(ReturnValue);
      }
    } else {
      // If the value is offset in memory, apply the offset now.
      Address V = emitAddressAtOffset(*this, ReturnValue, RetAI);

      RV = createCoercedLoad(V, RetAI.getCoerceToType(), *this);
    }

    break;

  case ABIArgInfo::Ignore:
    break;

  case ABIArgInfo::CoerceAndExpand: {
    auto coercionType = RetAI.getCoerceAndExpandType();

    llvm::SmallVector<llvm::Value *, 4> results;
    Address addr = ReturnValue.withElementType(coercionType);
    for (unsigned i = 0, e = coercionType->getNumElements(); i != e; ++i) {
      auto coercedEltType = coercionType->getElementType(i);
      if (ABIArgInfo::isPaddingForCoerceAndExpand(coercedEltType))
        continue;

      auto eltAddr = Builder.CreateStructGEP(addr, i);
      auto elt = Builder.CreateLoad(eltAddr);
      results.push_back(elt);
    }

    // If we have one result, it's the single direct result type.
    if (results.size() == 1) {
      RV = results[0];

      // Otherwise, we need to make a first-class aggregate.
    } else {
      // Construct a return type that lacks padding elements.
      llvm::Type *returnType = RetAI.getUnpaddedCoerceAndExpandType();

      RV = llvm::PoisonValue::get(returnType);
      for (unsigned i = 0, e = results.size(); i != e; ++i) {
        RV = Builder.CreateInsertValue(RV, results[i], i);
      }
    }
    break;
  }
  case ABIArgInfo::Expand:
  case ABIArgInfo::IndirectAliased:
    llvm_unreachable("Invalid ABI kind for return argument");
  }

  llvm::Instruction *Ret;
  if (RV) {
    Ret = Builder.CreateRet(RV);
  } else {
    Ret = Builder.CreateRetVoid();
  }

  if (RetDbgLoc)
    Ret->setDebugLoc(std::move(RetDbgLoc));
}

// ===----------------------------------------------------------------------===
// Call argument processing
// ===----------------------------------------------------------------------===

void FunctionEmitter::genCallArgs(
    CallArgList &Args, PrototypeWrapper Prototype,
    llvm::iterator_range<CallExpr::const_arg_iterator> ArgRange,
    AbstractCallee AC, unsigned ParamsToSkip, EvaluationOrder Order) {
  llvm::SmallVector<QualType, 16> ArgTypes;

  assert((ParamsToSkip == 0 || Prototype.P) &&
         "Can't skip parameters if type info is not provided");

  // First, if a prototype was provided, use those argument types.
  bool IsVariadic = false;
  if (Prototype.P) {
    {
      const auto *FPT = Prototype.P;
      IsVariadic = FPT->isVariadic();
      ArgTypes.assign(FPT->param_type_begin() + ParamsToSkip,
                      FPT->param_type_end());
    }

#ifndef NDEBUG
    CallExpr::const_arg_iterator Arg = ArgRange.begin();
    for (QualType Ty : ArgTypes) {
      assert(Arg != ArgRange.end() && "Running over edge of argument list!");
      assert(
          (Ty->isVariablyModifiedType() ||
           getContext().getCanonicalType(Ty).getTypePtr() ==
               getContext().getCanonicalType((*Arg)->getType()).getTypePtr()) &&
          "type mismatch in call argument!");
      ++Arg;
    }
    assert((Arg == ArgRange.end() || IsVariadic) &&
           "Extra arguments in non-variadic function!");
#endif
  }

  // If we still have any arguments, emit them using the type of the argument.
  for (auto *A : llvm::drop_begin(ArgRange, ArgTypes.size()))
    ArgTypes.push_back(IsVariadic ? getVarArgType(A) : A->getType());
  assert((int)ArgTypes.size() == (ArgRange.end() - ArgRange.begin()));

  // Microsoft ABI: evaluate arguments right-to-left so callee teardown matches
  // parameter construction. Some expressions force left-to-right evaluation
  // instead.
  bool LeftToRight = Order != EvaluationOrder::ForceRightToLeft;

  auto MaybeEmitImplicitObjectSize = [&](unsigned I, const Expr *Arg,
                                         RValue EmittedArg) {
    if (!AC.hasFunctionDecl() || I >= AC.getNumParams())
      return;
    auto *PS = AC.getParamDecl(I)->getAttr<PassObjectSizeAttr>();
    if (PS == nullptr)
      return;

    const auto &Context = getContext();
    auto SizeTy = Context.getSizeType();
    auto T = Builder.getIntNTy(Context.getTypeSize(SizeTy));
    assert(EmittedArg.getScalarVal() && "We emitted nothing for the arg?");
    llvm::Value *V = evaluateOrEmitBuiltinObjectSize(
        Arg, PS->getType(), T, EmittedArg.getScalarVal(), PS->isDynamic());
    Args.add(RValue::get(V), SizeTy);
    // If we're emitting args in reverse, be sure to do so with
    // pass_object_size, as well.
    if (!LeftToRight)
      std::swap(Args.back(), *(&Args.back() - 1));
  };

  // Evaluate each argument in the appropriate order.
  size_t CallArgsStart = Args.size();
  for (unsigned I = 0, E = ArgTypes.size(); I != E; ++I) {
    unsigned Idx = LeftToRight ? I : E - I - 1;
    CallExpr::const_arg_iterator Arg = ArgRange.begin() + Idx;
    unsigned InitialArgSize = Args.size();
    genCallArg(Args, *Arg, ArgTypes[Idx]);
    // In particular, we depend on it being the last arg in Args, and the
    // objectsize bits depend on there only being one arg if !LeftToRight.
    assert(InitialArgSize + 1 == Args.size() &&
           "The code below depends on only adding one arg per genCallArg");
    (void)InitialArgSize;
    // @llvm.objectsize should never have side-effects and shouldn't need
    // destruction/cleanups, so we can safely "emit" it after its arg,
    // regardless of right-to-leftness
    if (!Args.back().hasLValue()) {
      RValue RVArg = Args.back().getKnownRValue();
      MaybeEmitImplicitObjectSize(Idx, *Arg, RVArg);
    }
  }

  if (!LeftToRight) {
    // Un-reverse the arguments we just evaluated so they match up with the LLVM
    // IR function.
    std::reverse(Args.begin() + CallArgsStart, Args.end());
  }
}

RValue CallArg::getRValue(FunctionEmitter &FE) const {
  if (!HasLV)
    return RV;
  LValue Copy = FE.makeAddrLValue(FE.createMemTemp(Ty), Ty);
  FE.genAggregateCopy(Copy, LV, Ty, AggValueSlot::DoesNotOverlap,
                      LV.isVolatile());
  IsUsed = true;
  return RValue::getAggregate(Copy.getAddress(FE));
}

void CallArg::copyInto(FunctionEmitter &FE, Address Addr) const {
  LValue Dst = FE.makeAddrLValue(Addr, Ty);
  if (!HasLV && RV.isScalar())
    FE.genStoreOfScalar(RV.getScalarVal(), Dst, /*isInit=*/true);
  else if (!HasLV && RV.isComplex())
    FE.genStoreOfComplex(RV.getComplexVal(), Dst, /*init=*/true);
  else {
    auto Addr = HasLV ? LV.getAddress(FE) : RV.getAggregateAddress();
    LValue SrcLV = FE.makeAddrLValue(Addr, Ty);
    // We assume that call args are never copied into subobjects.
    FE.genAggregateCopy(Dst, SrcLV, Ty, AggValueSlot::DoesNotOverlap,
                        HasLV ? LV.isVolatileQualified()
                              : RV.isVolatileQualified());
  }
  IsUsed = true;
}

void FunctionEmitter::genCallArg(CallArgList &args, const Expr *E,
                                 QualType type) {
  bool HasAggregateEvalKind = hasAggregateEvaluationKind(type);

  if (HasAggregateEvalKind && isa<ImplicitCastExpr>(E) &&
      cast<CastExpr>(E)->getCastKind() == CK_LValueToRValue) {
    LValue L = genLValue(cast<CastExpr>(E)->getSubExpr());
    assert(L.isSimple());
    args.addUncopiedAggregate(L, type);
    return;
  }

  args.add(genAnyExprToTemp(E), type);
}

QualType FunctionEmitter::getVarArgType(const Expr *Arg) {
  // System headers on Windows define NULL to 0 instead of 0LL on Win64. MSVC
  // implicitly widens null pointer constants that are arguments to varargs
  // functions to pointer-sized ints.
  if (!getTarget().getTriple().isOSWindows())
    return Arg->getType();

  if (Arg->getType()->isIntegerType() &&
      getContext().getTypeSize(Arg->getType()) <
          getContext().getTargetInfo().getPointerWidth(LangAS::Default) &&
      Arg->isNullPointerConstant(getContext(),
                                 Expr::NPC_ValueDependentIsNotNull)) {
    return getContext().getIntPtrType();
  }

  return Arg->getType();
}

llvm::CallInst *
FunctionEmitter::genNounwindRuntimeCall(llvm::FunctionCallee callee,
                                        const llvm::Twine &name) {
  return genNounwindRuntimeCall(callee, std::nullopt, name);
}

llvm::CallInst *
FunctionEmitter::genNounwindRuntimeCall(llvm::FunctionCallee callee,
                                        llvm::ArrayRef<llvm::Value *> args,
                                        const llvm::Twine &name) {
  llvm::CallInst *call = genRuntimeCall(callee, args, name);
  call->setDoesNotThrow();
  return call;
}

llvm::CallInst *FunctionEmitter::genRuntimeCall(llvm::FunctionCallee callee,
                                                const llvm::Twine &name) {
  return genRuntimeCall(callee, std::nullopt, name);
}

// Calls which may throw must have operand bundles indicating which funclet
// they are nested within.
llvm::SmallVector<llvm::OperandBundleDef, 1>
FunctionEmitter::getBundlesForFunclet(llvm::Value *Callee) {
  // There is no need for a funclet operand bundle if we aren't inside a
  // funclet.
  if (!CurrentFuncletPad)
    return (llvm::SmallVector<llvm::OperandBundleDef, 1>());

  // Skip intrinsics which cannot throw (as long as they don't lower into
  // regular function calls in the course of IR transformations).
  if (auto *CalleeFn = dyn_cast<llvm::Function>(Callee->stripPointerCasts())) {
    if (CalleeFn->isIntrinsic() && CalleeFn->doesNotThrow()) {
      auto IID = CalleeFn->getIntrinsicID();
      if (!llvm::IntrinsicInst::mayLowerToFunctionCall(IID))
        return (llvm::SmallVector<llvm::OperandBundleDef, 1>());
    }
  }

  llvm::SmallVector<llvm::OperandBundleDef, 1> BundleList;
  BundleList.emplace_back("funclet", CurrentFuncletPad);
  return BundleList;
}

llvm::CallInst *
FunctionEmitter::genRuntimeCall(llvm::FunctionCallee callee,
                                llvm::ArrayRef<llvm::Value *> args,
                                const llvm::Twine &name) {
  llvm::CallInst *call = Builder.CreateCall(
      callee, args, getBundlesForFunclet(callee.getCallee()), name);
  call->setCallingConv(getRuntimeCC());
  return call;
}

void FunctionEmitter::genNoreturnRuntimeCallOrInvoke(
    llvm::FunctionCallee callee, llvm::ArrayRef<llvm::Value *> args) {
  llvm::SmallVector<llvm::OperandBundleDef, 1> BundleList =
      getBundlesForFunclet(callee.getCallee());

  if (getInvokeDest()) {
    llvm::InvokeInst *invoke = Builder.CreateInvoke(
        callee, getUnreachableBlock(), getInvokeDest(), args, BundleList);
    invoke->setDoesNotReturn();
    invoke->setCallingConv(getRuntimeCC());
  } else {
    llvm::CallInst *call = Builder.CreateCall(callee, args, BundleList);
    call->setDoesNotReturn();
    call->setCallingConv(getRuntimeCC());
    Builder.CreateUnreachable();
  }
}

llvm::CallBase *
FunctionEmitter::genRuntimeCallOrInvoke(llvm::FunctionCallee callee,
                                        const llvm::Twine &name) {
  return genRuntimeCallOrInvoke(callee, std::nullopt, name);
}

llvm::CallBase *
FunctionEmitter::genRuntimeCallOrInvoke(llvm::FunctionCallee callee,
                                        llvm::ArrayRef<llvm::Value *> args,
                                        const llvm::Twine &name) {
  llvm::CallBase *call = genCallOrInvoke(callee, args, name);
  call->setCallingConv(getRuntimeCC());
  return call;
}

llvm::CallBase *
FunctionEmitter::genCallOrInvoke(llvm::FunctionCallee Callee,
                                 llvm::ArrayRef<llvm::Value *> Args,
                                 const llvm::Twine &Name) {
  llvm::BasicBlock *InvokeDest = getInvokeDest();
  llvm::SmallVector<llvm::OperandBundleDef, 1> BundleList =
      getBundlesForFunclet(Callee.getCallee());

  llvm::CallBase *Inst;
  if (!InvokeDest)
    Inst = Builder.CreateCall(Callee, Args, BundleList, Name);
  else {
    llvm::BasicBlock *ContBB = createBasicBlock("invoke.cont");
    Inst = Builder.CreateInvoke(Callee, ContBB, InvokeDest, Args, BundleList,
                                Name);
    genBlock(ContBB);
  }

  return Inst;
}

// ===----------------------------------------------------------------------===
// Call emission
// ===----------------------------------------------------------------------===

void FunctionEmitter::deferPlaceholderReplacement(llvm::Instruction *Old,
                                                  llvm::Value *New) {
  DeferredReplacements.push_back(
      std::make_pair(llvm::WeakTrackingVH(Old), New));
}

namespace {

[[nodiscard]] llvm::AttributeList
maybeRaiseRetAlignmentAttribute(llvm::LLVMContext &Ctx,
                                const llvm::AttributeList &Attrs,
                                llvm::Align NewAlign) {
  llvm::Align CurAlign = Attrs.getRetAlignment().valueOrOne();
  if (CurAlign >= NewAlign)
    return Attrs;
  llvm::Attribute AlignAttr = llvm::Attribute::getWithAlignment(Ctx, NewAlign);
  return Attrs.removeRetAttribute(Ctx, llvm::Attribute::AttrKind::Alignment)
      .addRetAttribute(Ctx, AlignAttr);
}

template <typename AlignedAttrTy> class AbstractAssumeAlignedAttrEmitter {
protected:
  FunctionEmitter &FE;

  const AlignedAttrTy *AA = nullptr;

  llvm::Value *Alignment = nullptr;      // May or may not be a constant.
  llvm::ConstantInt *OffsetCI = nullptr; // Constant, hopefully zero.

  AbstractAssumeAlignedAttrEmitter(FunctionEmitter &Emitter,
                                   const Decl *FuncDecl)
      : FE(Emitter) {
    if (!FuncDecl)
      return;
    AA = FuncDecl->getAttr<AlignedAttrTy>();
  }

public:
  [[nodiscard]] llvm::AttributeList
  TryEmitAsCallSiteAttribute(const llvm::AttributeList &Attrs) {
    if (!AA || OffsetCI)
      return Attrs;
    const auto *AlignmentCI = dyn_cast<llvm::ConstantInt>(Alignment);
    if (!AlignmentCI)
      return Attrs;
    if (!AlignmentCI->getValue().isPowerOf2())
      return Attrs;
    llvm::AttributeList NewAttrs = maybeRaiseRetAlignmentAttribute(
        FE.getLLVMContext(), Attrs,
        llvm::Align(
            AlignmentCI->getLimitedValue(llvm::Value::MaximumAlignment)));
    AA = nullptr; // We're done. Disallow doing anything else.
    return NewAttrs;
  }

  void genAsAnAssumption(SourceLocation, QualType RetTy, RValue &Ret) {
    if (!AA)
      return;
    FE.emitAlignmentAssumption(Ret.getScalarVal(), RetTy, Alignment, OffsetCI);
    AA = nullptr; // We're done. Disallow doing anything else.
  }
};

class AssumeAlignedAttrEmitter final
    : public AbstractAssumeAlignedAttrEmitter<AssumeAlignedAttr> {
public:
  AssumeAlignedAttrEmitter(FunctionEmitter &Emitter, const Decl *FuncDecl)
      : AbstractAssumeAlignedAttrEmitter(Emitter, FuncDecl) {
    if (!AA)
      return;
    Alignment = cast<llvm::ConstantInt>(FE.genScalarExpr(AA->getAlignment()));
    if (Expr *Offset = AA->getOffset()) {
      OffsetCI = cast<llvm::ConstantInt>(FE.genScalarExpr(Offset));
      if (OffsetCI->isNullValue())
        OffsetCI = nullptr;
    }
  }
};

class AllocAlignAttrEmitter final
    : public AbstractAssumeAlignedAttrEmitter<AllocAlignAttr> {
public:
  AllocAlignAttrEmitter(FunctionEmitter &Emitter, const Decl *FuncDecl,
                        const CallArgList &CallArgs)
      : AbstractAssumeAlignedAttrEmitter(Emitter, FuncDecl) {
    if (!AA)
      return;
    Alignment = CallArgs[AA->getParamIndex().getLLVMIndex()]
                    .getRValue(FE)
                    .getScalarVal();
  }
};

unsigned getMaxVectorWidth(const llvm::Type *Ty) {
  if (auto *VT = dyn_cast<llvm::VectorType>(Ty))
    return VT->getPrimitiveSizeInBits().getKnownMinValue();
  if (auto *AT = dyn_cast<llvm::ArrayType>(Ty))
    return getMaxVectorWidth(AT->getElementType());

  unsigned MaxVectorWidth = 0;
  if (auto *ST = dyn_cast<llvm::StructType>(Ty))
    for (auto *I : ST->elements())
      MaxVectorWidth = std::max(MaxVectorWidth, getMaxVectorWidth(I));
  return MaxVectorWidth;
}
} // namespace

__attribute__((hot)) RValue FunctionEmitter::genCall(
    const ABIFunctionInfo &CallInfo, const FnCallee &Callee,
    ReturnValueSlot ReturnValue, const CallArgList &CallArgs,
    llvm::CallBase **callOrInvoke, bool IsMustTail, SourceLocation Loc) {

  assert(Callee.isOrdinary());

  // Handle struct-return functions by passing a pointer to the
  // location that we would like to return into.
  QualType RetTy = CallInfo.getReturnType();
  const ABIArgInfo &RetAI = CallInfo.getReturnInfo();

  llvm::FunctionType *IRFuncTy = getTypes().GetFunctionType(CallInfo);

  const Decl *TargetDecl = Callee.getAbstractInfo().getCalleeDecl().getDecl();
  if (const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(TargetDecl)) {
    // We can only guarantee that a function is called from the correct
    // context/function based on the appropriate target attributes,
    // so only check in the case where we have both always_inline and target
    // since otherwise we could be making a conditional call after a check for
    // the proper cpu features (and it won't cause code generation issues due to
    // function based code generation).
    if (TargetDecl->hasAttr<AlwaysInlineAttr>() &&
        (TargetDecl->hasAttr<TargetAttr>() ||
         (CurFuncDecl && CurFuncDecl->hasAttr<TargetAttr>())))
      checkTargetFeatures(Loc, FD);

    // Some architectures (such as x86-64) have the ABI changed based on
    // attribute-target/features. Give them a chance to diagnose.
    ME.getTargetCodeGenInfo().checkFunctionCallABI(
        ME, Loc, dyn_cast_or_null<FunctionDecl>(CurCodeDecl), FD, CallArgs);
  }

  // 1. Set up the arguments.

  FrontendToLLVMArgMapping IRFunctionArgs(ME.getContext(), CallInfo);
  llvm::SmallVector<llvm::Value *, 16> IRCallArgs(IRFunctionArgs.totalIRArgs());

  // If the call returns a temporary with struct return, create a temporary
  // alloca to hold the result, unless one is given to us.
  Address SRetPtr = Address::invalid();
  Address SRetAlloca = Address::invalid();
  llvm::Value *UnusedReturnSizePtr = nullptr;
  if (RetAI.isIndirect() || RetAI.isCoerceAndExpand()) {
    if (!ReturnValue.isNull()) {
      SRetPtr = ReturnValue.getValue();
    } else {
      SRetPtr = createMemTemp(RetTy, "tmp", &SRetAlloca);
      if (haveInsertPoint() && ReturnValue.isUnused()) {
        llvm::TypeSize size =
            ME.getDataLayout().getTypeAllocSize(convertTypeForMem(RetTy));
        UnusedReturnSizePtr = genLifetimeStart(size, SRetAlloca.getPointer());
      }
    }
    if (IRFunctionArgs.hasSRetArg()) {
      IRCallArgs[IRFunctionArgs.getSRetArgNo()] = SRetPtr.getPointer();
    }
  }

  // When passing arguments using temporary allocas, we need to add the
  // appropriate lifetime markers. This vector keeps track of all the lifetime
  // markers that need to be ended right after the call.
  llvm::SmallVector<CallLifetimeEnd, 2> CallLifetimeEndAfterCall;

  // Translate all of the arguments as necessary to match the IR lowering.
  assert(CallInfo.arg_size() == CallArgs.size() &&
         "Mismatch between function signature & arguments.");
  unsigned ArgNo = 0;
  ABIFunctionInfo::const_arg_iterator info_it = CallInfo.arg_begin();
  for (CallArgList::const_iterator I = CallArgs.begin(), E = CallArgs.end();
       I != E; ++I, ++info_it, ++ArgNo) {
    const ABIArgInfo &ArgInfo = info_it->info;

    // Insert a padding argument to ensure proper alignment.
    if (IRFunctionArgs.hasPaddingArg(ArgNo))
      IRCallArgs[IRFunctionArgs.getPaddingArgNo(ArgNo)] =
          llvm::UndefValue::get(ArgInfo.getPaddingType());

    unsigned FirstIRArg, NumIRArgs;
    std::tie(FirstIRArg, NumIRArgs) = IRFunctionArgs.getIRArgs(ArgNo);

    bool ArgHasMaybeUndefAttr =
        isArgumentMaybeUndef(TargetDecl, CallInfo.getNumRequiredArgs(), ArgNo);

    switch (ArgInfo.getKind()) {
    case ABIArgInfo::Indirect:
    case ABIArgInfo::IndirectAliased: {
      assert(NumIRArgs == 1);
      if (!I->isAggregate()) {
        // Make a temporary alloca to pass the argument.
        Address Addr = createMemTempWithoutCast(
            I->Ty, ArgInfo.getIndirectAlign(), "indirect-arg-temp");

        llvm::Value *Val = Addr.getPointer();
        if (ArgHasMaybeUndefAttr)
          Val = Builder.CreateFreeze(Addr.getPointer());
        IRCallArgs[FirstIRArg] = Val;

        I->copyInto(*this, Addr);
      } else {
        // We want to avoid creating an unnecessary temporary+copy here;
        // however, we need one in three cases:
        // 1. If the argument is not byval, and we are required to copy the
        //    source.  (This case doesn't occur on any common architecture.)
        // 2. If the argument is byval, RV is not sufficiently aligned, and
        //    we cannot force it to be sufficiently aligned.
        // 3. If the argument is byval, but RV is not located in default
        //    or alloca address space.
        Address Addr = I->hasLValue()
                           ? I->getKnownLValue().getAddress(*this)
                           : I->getKnownRValue().getAggregateAddress();
        llvm::Value *V = Addr.getPointer();
        CharUnits Align = ArgInfo.getIndirectAlign();
        const llvm::DataLayout *TD = &ME.getDataLayout();

        assert((FirstIRArg >= IRFuncTy->getNumParams() ||
                IRFuncTy->getParamType(FirstIRArg)->getPointerAddressSpace() ==
                    TD->getAllocaAddrSpace()) &&
               "indirect argument must be in alloca address space");

        bool NeedCopy = false;
        if (Addr.getAlignment() < Align &&
            llvm::getOrEnforceKnownAlignment(V, Align.getAsAlign(), *TD) <
                Align.getAsAlign()) {
          NeedCopy = true;
        } else if (I->hasLValue()) {
          auto LV = I->getKnownLValue();
          auto AS = LV.getAddressSpace();

          bool isByValOrRef =
              ArgInfo.isIndirectAliased() || ArgInfo.getIndirectByVal();

          if (!isByValOrRef ||
              (LV.getAlignment() < getContext().getTypeAlignInChars(I->Ty))) {
            NeedCopy = true;
          }
          if ((isByValOrRef && (AS != LangAS::Default &&
                                AS != ME.getASTAllocaAddressSpace()))) {
            NeedCopy = true;
          } else if ((isByValOrRef && Addr.getType()->getAddressSpace() !=
                                          IRFuncTy->getParamType(FirstIRArg)
                                              ->getPointerAddressSpace())) {
            NeedCopy = true;
          }
        }

        if (NeedCopy) {
          Address AI = createMemTempWithoutCast(
              I->Ty, ArgInfo.getIndirectAlign(), "byval-temp");
          llvm::Value *Val = AI.getPointer();
          if (ArgHasMaybeUndefAttr)
            Val = Builder.CreateFreeze(AI.getPointer());
          IRCallArgs[FirstIRArg] = Val;

          llvm::TypeSize ByvalTempElementSize =
              ME.getDataLayout().getTypeAllocSize(AI.getElementType());
          llvm::Value *LifetimeSize =
              genLifetimeStart(ByvalTempElementSize, AI.getPointer());

          if (LifetimeSize)
            CallLifetimeEndAfterCall.emplace_back(AI, LifetimeSize);

          // Generate the copy.
          I->copyInto(*this, AI);
        } else {
          // Skip the extra memcpy call.
          auto *T = llvm::PointerType::get(
              ME.getLLVMContext(), ME.getDataLayout().getAllocaAddrSpace());

          llvm::Value *Val = getTargetHooks().performAddrSpaceCast(
              *this, V, LangAS::Default, ME.getASTAllocaAddressSpace(), T,
              true);
          if (ArgHasMaybeUndefAttr)
            Val = Builder.CreateFreeze(Val);
          IRCallArgs[FirstIRArg] = Val;
        }
      }
      break;
    }

    case ABIArgInfo::Ignore:
      assert(NumIRArgs == 0);
      break;

    case ABIArgInfo::Extend:
    case ABIArgInfo::Direct: {
      if (!isa<llvm::StructType>(ArgInfo.getCoerceToType()) &&
          ArgInfo.getCoerceToType() == convertType(info_it->type) &&
          ArgInfo.getDirectOffset() == 0) {
        assert(NumIRArgs == 1);
        llvm::Value *V;
        if (!I->isAggregate())
          V = I->getKnownRValue().getScalarVal();
        else
          V = Builder.CreateLoad(
              I->hasLValue() ? I->getKnownLValue().getAddress(*this)
                             : I->getKnownRValue().getAggregateAddress());

        // We might have to widen integers, but we should never truncate.
        if (ArgInfo.getCoerceToType() != V->getType() &&
            V->getType()->isIntegerTy())
          V = Builder.CreateZExt(V, ArgInfo.getCoerceToType());

        // If the argument doesn't match, perform a bitcast to coerce it.  This
        // can happen due to trivial type mismatches.
        if (FirstIRArg < IRFuncTy->getNumParams() &&
            V->getType() != IRFuncTy->getParamType(FirstIRArg))
          V = Builder.CreateBitCast(V, IRFuncTy->getParamType(FirstIRArg));

        if (ArgHasMaybeUndefAttr)
          V = Builder.CreateFreeze(V);
        IRCallArgs[FirstIRArg] = V;
        break;
      }

      Address Src = Address::invalid();
      if (!I->isAggregate()) {
        Src = createMemTemp(I->Ty, "coerce");
        I->copyInto(*this, Src);
      } else {
        Src = I->hasLValue() ? I->getKnownLValue().getAddress(*this)
                             : I->getKnownRValue().getAggregateAddress();
      }

      // If the value is offset in memory, apply the offset now.
      Src = emitAddressAtOffset(*this, Src, ArgInfo);

      // Fast-isel and the optimizer generally like scalar values better than
      // FCAs, so we flatten them if this is safe to do for this argument.
      llvm::StructType *STy =
          dyn_cast<llvm::StructType>(ArgInfo.getCoerceToType());
      if (STy && ArgInfo.isDirect() && ArgInfo.getCanBeFlattened()) {
        llvm::Type *SrcTy = Src.getElementType();
        llvm::TypeSize SrcTypeSize = ME.getDataLayout().getTypeAllocSize(SrcTy);
        llvm::TypeSize DstTypeSize = ME.getDataLayout().getTypeAllocSize(STy);
        if (SrcTypeSize.isScalable()) {
          assert(STy->containsHomogeneousScalableVectorTypes() &&
                 "ABI only supports structure with homogeneous scalable vector "
                 "type");
          assert(SrcTypeSize == DstTypeSize &&
                 "Only allow non-fractional movement of structure with "
                 "homogeneous scalable vector type");
          assert(NumIRArgs == STy->getNumElements());

          llvm::Value *StoredStructValue =
              Builder.CreateLoad(Src, Src.getName() + ".tuple");
          for (unsigned i = 0, e = STy->getNumElements(); i != e; ++i) {
            llvm::Value *Extract = Builder.CreateExtractValue(
                StoredStructValue, i,
                Src.getName() + ".extract" + llvm::Twine(i));
            IRCallArgs[FirstIRArg + i] = Extract;
          }
        } else {
          uint64_t SrcSize = SrcTypeSize.getFixedValue();
          uint64_t DstSize = DstTypeSize.getFixedValue();

          // If the source type is smaller than the destination type of the
          // coerce-to logic, copy the source value into a temp alloca the size
          // of the destination type to allow loading all of it. The bits past
          // the source value are left undef.
          if (SrcSize < DstSize) {
            Address TempAlloca = createTempAlloca(STy, Src.getAlignment(),
                                                  Src.getName() + ".coerce");
            Builder.CreateMemCpy(TempAlloca, Src, SrcSize);
            Src = TempAlloca;
          } else {
            Src = Src.withElementType(STy);
          }

          assert(NumIRArgs == STy->getNumElements());
          for (unsigned i = 0, e = STy->getNumElements(); i != e; ++i) {
            Address EltPtr = Builder.CreateStructGEP(Src, i);
            llvm::Value *LI = Builder.CreateLoad(EltPtr);
            if (ArgHasMaybeUndefAttr)
              LI = Builder.CreateFreeze(LI);
            IRCallArgs[FirstIRArg + i] = LI;
          }
        }
      } else {
        // In the simple case, just pass the coerced loaded value.
        assert(NumIRArgs == 1);
        llvm::Value *Load =
            createCoercedLoad(Src, ArgInfo.getCoerceToType(), *this);

        if (ArgHasMaybeUndefAttr)
          Load = Builder.CreateFreeze(Load);
        IRCallArgs[FirstIRArg] = Load;
      }

      break;
    }

    case ABIArgInfo::CoerceAndExpand: {
      auto coercionType = ArgInfo.getCoerceAndExpandType();
      auto layout = ME.getDataLayout().getStructLayout(coercionType);

      llvm::Value *tempSize = nullptr;
      Address addr = Address::invalid();
      Address AllocaAddr = Address::invalid();
      if (I->isAggregate()) {
        addr = I->hasLValue() ? I->getKnownLValue().getAddress(*this)
                              : I->getKnownRValue().getAggregateAddress();

      } else {
        RValue RV = I->getKnownRValue();
        assert(RV.isScalar()); // complex should always just be direct

        llvm::Type *scalarType = RV.getScalarVal()->getType();
        auto scalarSize = ME.getDataLayout().getTypeAllocSize(scalarType);
        auto scalarAlign = ME.getDataLayout().getPrefTypeAlign(scalarType);

        // Materialize to a temporary.
        addr = createTempAlloca(RV.getScalarVal()->getType(),
                                CharUnits::fromQuantity(std::max(
                                    layout->getAlignment(), scalarAlign)),
                                "tmp",
                                /*ArraySize=*/nullptr, &AllocaAddr);
        tempSize = genLifetimeStart(scalarSize, AllocaAddr.getPointer());

        Builder.CreateStore(RV.getScalarVal(), addr);
      }

      addr = addr.withElementType(coercionType);

      unsigned IRArgPos = FirstIRArg;
      for (unsigned i = 0, e = coercionType->getNumElements(); i != e; ++i) {
        llvm::Type *eltType = coercionType->getElementType(i);
        if (ABIArgInfo::isPaddingForCoerceAndExpand(eltType))
          continue;
        Address eltAddr = Builder.CreateStructGEP(addr, i);
        llvm::Value *elt = Builder.CreateLoad(eltAddr);
        if (ArgHasMaybeUndefAttr)
          elt = Builder.CreateFreeze(elt);
        IRCallArgs[IRArgPos++] = elt;
      }
      assert(IRArgPos == FirstIRArg + NumIRArgs);

      if (tempSize) {
        genLifetimeEnd(tempSize, AllocaAddr.getPointer());
      }

      break;
    }

    case ABIArgInfo::Expand: {
      unsigned IRArgPos = FirstIRArg;
      expandTypeToArgs(I->Ty, *I, IRFuncTy, IRCallArgs, IRArgPos);
      assert(IRArgPos == FirstIRArg + NumIRArgs);
      break;
    }
    }
  }

  const FnCallee &ConcreteCallee = Callee.prepareConcreteCallee(*this);
  llvm::Value *CalleePtr = ConcreteCallee.getFunctionPointer();

  // 2. Prepare the function pointer.

  // If the callee is a bitcast of a non-variadic function to have a
  // variadic function pointer type, check to see if we can remove the
  // bitcast.  This comes up with unprototyped functions.
  //
  // This makes the IR nicer, but more importantly it ensures that we
  // can inline the function at -O0 if it is marked always_inline.
  auto simplifyVariadicCallee = [](llvm::FunctionType *CalleeFT,
                                   llvm::Value *Ptr) -> llvm::Function * {
    if (!CalleeFT->isVarArg())
      return nullptr;

    if (llvm::ConstantExpr *CE = dyn_cast<llvm::ConstantExpr>(Ptr)) {
      if (CE->getOpcode() == llvm::Instruction::BitCast)
        Ptr = CE->getOperand(0);
    }

    llvm::Function *OrigFn = dyn_cast<llvm::Function>(Ptr);
    if (!OrigFn)
      return nullptr;

    llvm::FunctionType *OrigFT = OrigFn->getFunctionType();

    // If the original type is variadic, or if any of the component types
    // disagree, we cannot remove the cast.
    if (OrigFT->isVarArg() ||
        OrigFT->getNumParams() != CalleeFT->getNumParams() ||
        OrigFT->getReturnType() != CalleeFT->getReturnType())
      return nullptr;

    for (unsigned i = 0, e = OrigFT->getNumParams(); i != e; ++i)
      if (OrigFT->getParamType(i) != CalleeFT->getParamType(i))
        return nullptr;

    return OrigFn;
  };

  if (llvm::Function *OrigFn = simplifyVariadicCallee(IRFuncTy, CalleePtr)) {
    CalleePtr = OrigFn;
    IRFuncTy = OrigFn->getFunctionType();
  }

  // 3. Perform the actual call.

  // Assert that the arguments we computed match up.  The IR verifier
  // will catch this, but this is a common enough source of problems
  // during IRGen changes that it's way better for debugging to catch
  // it ourselves here.
#ifndef NDEBUG
  assert(IRCallArgs.size() == IRFuncTy->getNumParams() || IRFuncTy->isVarArg());
  for (unsigned i = 0; i < IRCallArgs.size(); ++i) {
    if (i < IRFuncTy->getNumParams())
      assert(IRCallArgs[i]->getType() == IRFuncTy->getParamType(i));
  }
#endif

  for (unsigned i = 0; i < IRCallArgs.size(); ++i)
    LargestVectorWidth = std::max(LargestVectorWidth,
                                  getMaxVectorWidth(IRCallArgs[i]->getType()));

  unsigned CallingConv;
  llvm::AttributeList Attrs;
  ME.constructAttributeList(CalleePtr->getName(), CallInfo,
                            Callee.getAbstractInfo(), Attrs, CallingConv,
                            /*AttrOnCallSite=*/true);

  if (const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(CurFuncDecl)) {
    if (FD->hasAttr<StrictFPAttr>())
      // All calls within a strictfp function are marked strictfp
      Attrs = Attrs.addFnAttribute(getLLVMContext(), llvm::Attribute::StrictFP);

    // If -ffast-math is enabled and the function is guarded by an
    // '__attribute__((optnone)) adjust the memory attribute so the BE emits the
    // library call instead of the intrinsic.
    if (FD->hasAttr<OptimizeNoneAttr>() && getLangOpts().FastMath)
      ME.adjustMemoryAttribute(CalleePtr->getName(), Callee.getAbstractInfo(),
                               Attrs);
  }
  if (InNoMergeAttributedStmt)
    Attrs = Attrs.addFnAttribute(getLLVMContext(), llvm::Attribute::NoMerge);

  if (InNoInlineAttributedStmt)
    Attrs = Attrs.addFnAttribute(getLLVMContext(), llvm::Attribute::NoInline);

  if (InAlwaysInlineAttributedStmt)
    Attrs =
        Attrs.addFnAttribute(getLLVMContext(), llvm::Attribute::AlwaysInline);

  // Apply some call-site-specific attributes.

  // Apply always_inline to all calls within flatten functions.
  if (CurCodeDecl && CurCodeDecl->hasAttr<FlattenAttr>() &&
      !InNoInlineAttributedStmt &&
      !(TargetDecl && TargetDecl->hasAttr<NoInlineAttr>())) {
    Attrs =
        Attrs.addFnAttribute(getLLVMContext(), llvm::Attribute::AlwaysInline);
  }

  // Disable inlining inside SEH __try blocks.
  if (isSEHTryScope()) {
    Attrs = Attrs.addFnAttribute(getLLVMContext(), llvm::Attribute::NoInline);
  }

  bool CannotThrow;
  if (currentFunctionUsesSEHTry()) {
    CannotThrow = false;
  } else {
    CannotThrow = Attrs.hasFnAttr(llvm::Attribute::NoUnwind);

    if (auto *FPtr = dyn_cast<llvm::Function>(CalleePtr))
      if (FPtr->hasFnAttribute(llvm::Attribute::NoUnwind))
        CannotThrow = true;
  }

  if (UnusedReturnSizePtr)
    pushFullExprCleanup<CallLifetimeEnd>(NormalEHLifetimeMarker, SRetAlloca,
                                         UnusedReturnSizePtr);

  llvm::BasicBlock *InvokeDest = CannotThrow ? nullptr : getInvokeDest();

  llvm::SmallVector<llvm::OperandBundleDef, 1> BundleList =
      getBundlesForFunclet(CalleePtr);

  if (const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(CurFuncDecl))
    if (FD->hasAttr<StrictFPAttr>())
      // All calls within a strictfp function are marked strictfp
      Attrs = Attrs.addFnAttribute(getLLVMContext(), llvm::Attribute::StrictFP);

  AssumeAlignedAttrEmitter AssumeAlignedAttrEmitter(*this, TargetDecl);
  Attrs = AssumeAlignedAttrEmitter.TryEmitAsCallSiteAttribute(Attrs);

  AllocAlignAttrEmitter AllocAlignAttrEmitter(*this, TargetDecl, CallArgs);
  Attrs = AllocAlignAttrEmitter.TryEmitAsCallSiteAttribute(Attrs);

  llvm::CallBase *CI;
  if (!InvokeDest) {
    CI = Builder.CreateCall(IRFuncTy, CalleePtr, IRCallArgs, BundleList);
  } else {
    llvm::BasicBlock *Cont = createBasicBlock("invoke.cont");
    CI = Builder.CreateInvoke(IRFuncTy, CalleePtr, Cont, InvokeDest, IRCallArgs,
                              BundleList);
    genBlock(Cont);
  }
  if (callOrInvoke)
    *callOrInvoke = CI;

  if (const auto *FD = dyn_cast_or_null<FunctionDecl>(CurFuncDecl)) {
    if (const auto *A = FD->getAttr<CFGuardAttr>()) {
      if (A->getGuard() == CFGuardAttr::GuardArg::nocf &&
          !CI->getCalledFunction())
        Attrs = Attrs.addFnAttribute(getLLVMContext(), "guard_nocf");
    }
  }

  CI->setAttributes(Attrs);
  CI->setCallingConv(static_cast<llvm::CallingConv::ID>(CallingConv));

  if (!CI->getType()->isVoidTy())
    CI->setName("call");

  LargestVectorWidth =
      std::max(LargestVectorWidth, getMaxVectorWidth(CI->getType()));

  if (llvm::CallInst *Call = dyn_cast<llvm::CallInst>(CI)) {
    if (TargetDecl && TargetDecl->hasAttr<NotTailCalledAttr>())
      Call->setTailCallKind(llvm::CallInst::TCK_NoTail);
    else if (IsMustTail)
      Call->setTailCallKind(llvm::CallInst::TCK_MustTail);
  }

  if (getDebugInfo() && TargetDecl && TargetDecl->hasAttr<MSAllocatorAttr>())
    getDebugInfo()->addHeapAllocSiteMetadata(CI, RetTy->getPointeeType(), Loc);

  if (TargetDecl && TargetDecl->hasAttr<ErrorAttr>()) {
    llvm::ConstantInt *Line =
        llvm::ConstantInt::get(Int32Ty, Loc.getRawEncoding());
    llvm::ConstantAsMetadata *MD = llvm::ConstantAsMetadata::get(Line);
    llvm::MDTuple *MDT = llvm::MDNode::get(getLLVMContext(), {MD});
    CI->setMetadata("srcloc", MDT);
  }

  if (CI->doesNotReturn()) {
    if (UnusedReturnSizePtr)
      popCleanupBlock();

    genUnreachable(Loc);
    Builder.ClearInsertionPoint();

    ensureInsertPoint();

    return getUndefRValue(RetTy);
  }

  if (IsMustTail) {
    for (auto it = EHStack.find(CurrentCleanupScopeDepth); it != EHStack.end();
         ++it) {
      EHCleanupScope *Cleanup = dyn_cast<EHCleanupScope>(&*it);
      if (!(Cleanup && Cleanup->getCleanup()->isRedundantBeforeReturn()))
        ME.errorUnsupported(MustTailCall, "tail call skipping over cleanups");
    }
    if (CI->getType()->isVoidTy())
      Builder.CreateRetVoid();
    else
      Builder.CreateRet(CI);
    Builder.ClearInsertionPoint();
    ensureInsertPoint();
    return getUndefRValue(RetTy);
  }

  // Extract the return value.
  RValue Ret = [&] {
    switch (RetAI.getKind()) {
    case ABIArgInfo::CoerceAndExpand: {
      auto coercionType = RetAI.getCoerceAndExpandType();

      Address addr = SRetPtr.withElementType(coercionType);

      assert(CI->getType() == RetAI.getUnpaddedCoerceAndExpandType());
      bool requiresExtract = isa<llvm::StructType>(CI->getType());

      unsigned unpaddedIndex = 0;
      for (unsigned i = 0, e = coercionType->getNumElements(); i != e; ++i) {
        llvm::Type *eltType = coercionType->getElementType(i);
        if (ABIArgInfo::isPaddingForCoerceAndExpand(eltType))
          continue;
        Address eltAddr = Builder.CreateStructGEP(addr, i);
        llvm::Value *elt = CI;
        if (requiresExtract)
          elt = Builder.CreateExtractValue(elt, unpaddedIndex++);
        else
          assert(unpaddedIndex == 0);
        Builder.CreateStore(elt, eltAddr);
      }
      [[fallthrough]];
    }

    case ABIArgInfo::Indirect: {
      RValue ret = convertTempToRValue(SRetPtr, RetTy, SourceLocation());
      if (UnusedReturnSizePtr)
        popCleanupBlock();
      return ret;
    }

    case ABIArgInfo::Ignore:
      // If we are ignoring an argument that had a result, make sure to
      // construct the appropriate return value for our caller.
      return getUndefRValue(RetTy);

    case ABIArgInfo::Extend:
    case ABIArgInfo::Direct: {
      llvm::Type *RetIRTy = convertType(RetTy);
      if (RetAI.getCoerceToType() == RetIRTy && RetAI.getDirectOffset() == 0) {
        switch (getEvaluationKind(RetTy)) {
        case TEK_Complex: {
          llvm::Value *Real = Builder.CreateExtractValue(CI, 0);
          llvm::Value *Imag = Builder.CreateExtractValue(CI, 1);
          return RValue::getComplex(std::make_pair(Real, Imag));
        }
        case TEK_Aggregate: {
          Address DestPtr = ReturnValue.getValue();
          bool DestIsVolatile = ReturnValue.isVolatile();

          if (!DestPtr.isValid()) {
            DestPtr = createMemTemp(RetTy, "agg.tmp");
            DestIsVolatile = false;
          }
          genAggregateStore(CI, DestPtr, DestIsVolatile);
          return RValue::getAggregate(DestPtr);
        }
        case TEK_Scalar: {
          // If the argument doesn't match, perform a bitcast to coerce it. This
          // can happen due to trivial type mismatches.
          llvm::Value *V = CI;
          if (V->getType() != RetIRTy)
            V = Builder.CreateBitCast(V, RetIRTy);
          return RValue::get(V);
        }
        }
        llvm_unreachable("bad evaluation kind");
      }

      // If coercing a fixed vector from a scalable vector for ABI
      // compatibility, and the types match, use the llvm.vector.extract
      // intrinsic to perform the conversion.
      if (auto *FixedDst = dyn_cast<llvm::FixedVectorType>(RetIRTy)) {
        llvm::Value *V = CI;
        if (auto *ScalableSrc =
                dyn_cast<llvm::ScalableVectorType>(V->getType())) {
          if (FixedDst->getElementType() == ScalableSrc->getElementType()) {
            llvm::Value *Zero = llvm::Constant::getNullValue(ME.Int64Ty);
            V = Builder.CreateExtractVector(FixedDst, V, Zero, "cast.fixed");
            return RValue::get(V);
          }
        }
      }

      Address DestPtr = ReturnValue.getValue();
      bool DestIsVolatile = ReturnValue.isVolatile();

      if (!DestPtr.isValid()) {
        DestPtr = createMemTemp(RetTy, "coerce");
        DestIsVolatile = false;
      }

      // An empty record can overlap other data (if declared with
      // no_unique_address); omit the store for such types - as there is no
      // actual data to store.
      if (!isEmptyRecord(getContext(), RetTy, true)) {
        // If the value is offset in memory, apply the offset now.
        Address StorePtr = emitAddressAtOffset(*this, DestPtr, RetAI);
        createCoercedStore(CI, StorePtr, DestIsVolatile, *this);
      }

      return convertTempToRValue(DestPtr, RetTy, SourceLocation());
    }

    case ABIArgInfo::Expand:
    case ABIArgInfo::IndirectAliased:
      llvm_unreachable("Invalid ABI kind for return argument");
    }

    llvm_unreachable("Unhandled ABIArgInfo::Kind");
  }();

  if (Ret.isScalar() && TargetDecl) {
    AssumeAlignedAttrEmitter.genAsAnAssumption(Loc, RetTy, Ret);
    AllocAlignAttrEmitter.genAsAnAssumption(Loc, RetTy, Ret);
  }

  // Explicitly call CallLifetimeEnd::Emit just to re-use the code even though
  // we can't use the full cleanup mechanism.
  for (CallLifetimeEnd &LifetimeEnd : CallLifetimeEndAfterCall)
    LifetimeEnd.Emit(*this, /*Flags=*/{});

  return Ret;
}

FnCallee FnCallee::prepareConcreteCallee(FunctionEmitter &FE) const {
  return *this;
}

/* VarArg handling */

Address FunctionEmitter::genVAArg(VAArgExpr *VE, Address &VAListAddr) {
  VAListAddr = VE->isMicrosoftABI() ? genMSVAListRef(VE->getSubExpr())
                                    : genVAListRef(VE->getSubExpr());
  QualType Ty = VE->getType();
  if (VE->isMicrosoftABI())
    return ME.getTypes().getABIInfo().genMSVAArg(*this, VAListAddr, Ty);
  return ME.getTypes().getABIInfo().genVAArg(*this, VAListAddr, Ty);
}
