#include "ABI/ABIInfoImpl.h"
#include "ABI/TargetInfo.h"
#include "neverc/Foundation/Diagnostic/DiagnosticFrontend.h"
#include "neverc/Tree/Type/StructLayout.h"
#include "llvm/ADT/SmallBitVector.h"

using namespace neverc;
using namespace neverc::CodeGen;

// ===----------------------------------------------------------------------===
// X86-64 helper functions
// ===----------------------------------------------------------------------===

namespace {

llvm::Type *x86AdjustInlineAsmType(Emit::FunctionEmitter &FE,
                                   llvm::StringRef Constraint, llvm::Type *Ty) {
  bool IsMMXCons = llvm::StringSwitch<bool>(Constraint)
                       .Cases("y", "&y", "^Ym", true)
                       .Default(false);
  if (IsMMXCons && Ty->isVectorTy()) {
    if (cast<llvm::VectorType>(Ty)->getPrimitiveSizeInBits().getFixedValue() !=
        64) {
      // Invalid MMX constraint
      return nullptr;
    }

    return llvm::Type::getX86_MMXTy(FE.getLLVMContext());
  }

  // No operation needed
  return Ty;
}

bool isX86VectorTypeForVectorCall(TreeContext &Context, QualType Ty) {
  if (const BuiltinType *BT = Ty->getAs<BuiltinType>()) {
    if (BT->isFloatingPoint() && BT->getKind() != BuiltinType::Half) {
      if (BT->getKind() == BuiltinType::LongDouble) {
        if (&Context.getTargetInfo().getLongDoubleFormat() ==
            &llvm::APFloat::x87DoubleExtended())
          return false;
      }
      return true;
    }
  } else if (const VectorType *VT = Ty->getAs<VectorType>()) {
    // vectorcall can pass XMM, YMM, and ZMM vectors. We don't pass SSE1 MMX
    // registers specially.
    unsigned VecSize = Context.getTypeSize(VT);
    if (VecSize == 128 || VecSize == 256 || VecSize == 512)
      return true;
  }
  return false;
}

bool isX86VectorCallAggregateSmallEnough(uint64_t NumMembers) {
  return NumMembers <= 4;
}

ABIArgInfo getDirectX86Hva(llvm::Type *T = nullptr) {
  auto AI = ABIArgInfo::getDirect(T);
  AI.setInReg(true);
  AI.setCanBeFlattened(false);
  return AI;
}
void addX86InterruptAttrs(const FunctionDecl *FD, llvm::GlobalValue *GV,
                          Emit::ModuleEmitter &ME) {
  if (!FD->hasAttr<AnyX86InterruptAttr>())
    return;

  llvm::Function *Fn = cast<llvm::Function>(GV);
  Fn->setCallingConv(llvm::CallingConv::X86_INTR);
  if (FD->getNumParams() == 0)
    return;

  auto PtrTy = cast<PointerType>(FD->getParamDecl(0)->getType());
  llvm::Type *ByValTy = ME.getTypes().convertType(PtrTy->getPointeeType());
  llvm::Attribute NewAttr =
      llvm::Attribute::getWithByValType(Fn->getContext(), ByValTy);
  Fn->addParamAttr(0, NewAttr);
}

} // namespace

namespace {

unsigned getNativeVectorSizeForAVXABI(X86AVXABILevel AVXLevel) {
  switch (AVXLevel) {
  case X86AVXABILevel::AVX512:
    return 512;
  case X86AVXABILevel::AVX:
    return 256;
  case X86AVXABILevel::None:
    return 128;
  }
  llvm_unreachable("Unknown AVXLevel");
}

// ===----------------------------------------------------------------------===
// X86_64ABIInfo
// ===----------------------------------------------------------------------===

class X86_64ABIInfo : public ABIInfo {
  enum Class {
    Integer = 0,
    SSE,
    SSEUp,
    X87,
    X87Up,
    ComplexX87,
    NoClass,
    Memory
  };

  static Class merge(Class Accum, Class Field);

  void postMerge(unsigned AggregateSize, Class &Lo, Class &Hi) const;

  void classify(QualType T, uint64_t OffsetBase, Class &Lo, Class &Hi,
                bool isNamedArg, bool IsRegCall = false) const;

  llvm::Type *GetByteVectorType(QualType Ty) const;
  llvm::Type *GetSSETypeAtOffset(llvm::Type *IRType, unsigned IROffset,
                                 QualType SourceTy,
                                 unsigned SourceOffset) const;
  llvm::Type *GetINTEGERTypeAtOffset(llvm::Type *IRType, unsigned IROffset,
                                     QualType SourceTy,
                                     unsigned SourceOffset) const;

  ABIArgInfo getIndirectReturnResult(QualType Ty) const;

  ABIArgInfo getIndirectResult(QualType Ty, unsigned freeIntRegs) const;

  ABIArgInfo classifyReturnType(QualType RetTy) const;

  ABIArgInfo classifyArgumentType(QualType Ty, unsigned freeIntRegs,
                                  unsigned &neededInt, unsigned &neededSSE,
                                  bool isNamedArg,
                                  bool IsRegCall = false) const;

  ABIArgInfo classifyRegCallStructType(QualType Ty, unsigned &NeededInt,
                                       unsigned &NeededSSE,
                                       unsigned &MaxVectorWidth) const;

  ABIArgInfo classifyRegCallStructTypeImpl(QualType Ty, unsigned &NeededInt,
                                           unsigned &NeededSSE,
                                           unsigned &MaxVectorWidth) const;

  bool IsIllegalVectorType(QualType Ty) const;

  bool honorsRevision0_98() const {
    return !getTarget().getTriple().isOSDarwin();
  }

  bool classifyIntegerMMXAsSSE() const {
    // ABI <= 3.8 did not do this.
    if (getContext().getLangOpts().getNeverCABICompat() <=
        LangOptions::NeverCABI::Ver3_8)
      return false;

    const llvm::Triple &Triple = getTarget().getTriple();
    if (Triple.isOSDarwin())
      return false;
    return true;
  }

  // GCC classifies vectors of __int128 as memory.
  bool passInt128VectorsInMem() const {
    // ABI <= 9.0 did not do this.
    if (getContext().getLangOpts().getNeverCABICompat() <=
        LangOptions::NeverCABI::Ver9)
      return false;

    const llvm::Triple &T = getTarget().getTriple();
    return T.isOSLinux();
  }

  X86AVXABILevel AVXLevel;

public:
  X86_64ABIInfo(Emit::TypeEmitter &CGT, X86AVXABILevel AVXLevel)
      : ABIInfo(CGT), AVXLevel(AVXLevel) {}

  bool isPassedUsingAVXType(QualType type) const {
    unsigned neededInt, neededSSE;
    // The freeIntRegs argument doesn't matter here.
    ABIArgInfo info = classifyArgumentType(type, 0, neededInt, neededSSE,
                                           /*isNamedArg*/ true);
    if (info.isDirect()) {
      llvm::Type *ty = info.getCoerceToType();
      if (llvm::VectorType *vectorTy = dyn_cast_or_null<llvm::VectorType>(ty))
        return vectorTy->getPrimitiveSizeInBits().getFixedValue() > 128;
    }
    return false;
  }

  void computeInfo(ABIFunctionInfo &FI) const override;

  Address genVAArg(FunctionEmitter &FE, Address VAListAddr,
                   QualType Ty) const override;
  Address genMSVAArg(FunctionEmitter &FE, Address VAListAddr,
                     QualType Ty) const override;
};

class WinX86_64ABIInfo : public ABIInfo {
public:
  WinX86_64ABIInfo(Emit::TypeEmitter &CGT, X86AVXABILevel AVXLevel)
      : ABIInfo(CGT), AVXLevel(AVXLevel) {}

  void computeInfo(ABIFunctionInfo &FI) const override;

  Address genVAArg(FunctionEmitter &FE, Address VAListAddr,
                   QualType Ty) const override;

  bool isHomogeneousAggregateBaseType(QualType Ty) const override {
    return isX86VectorTypeForVectorCall(getContext(), Ty);
  }

  bool isHomogeneousAggregateSmallEnough(const Type *Ty,
                                         uint64_t NumMembers) const override {
    return isX86VectorCallAggregateSmallEnough(NumMembers);
  }

private:
  ABIArgInfo classify(QualType Ty, unsigned &FreeSSERegs, bool IsReturnType,
                      bool IsVectorCall, bool IsRegCall) const;
  ABIArgInfo reclassifyHvaArgForVectorCall(QualType Ty, unsigned &FreeSSERegs,
                                           const ABIArgInfo &current) const;

  X86AVXABILevel AVXLevel;
};

// ===----------------------------------------------------------------------===
// X86_64TargetCodeGenInfo
// ===----------------------------------------------------------------------===

class X86_64TargetCodeGenInfo : public TargetCodeGenInfo {
public:
  X86_64TargetCodeGenInfo(Emit::TypeEmitter &CGT, X86AVXABILevel AVXLevel)
      : TargetCodeGenInfo(std::make_unique<X86_64ABIInfo>(CGT, AVXLevel)) {}

  int getDwarfEHStackPointer(Emit::ModuleEmitter &ME) const override {
    return 7;
  }

  bool initDwarfEHRegSizeTable(Emit::FunctionEmitter &FE,
                               llvm::Value *Address) const override {
    llvm::Value *Eight8 = llvm::ConstantInt::get(FE.Int8Ty, 8);

    // 0-15 are the 16 integer registers.
    // 16 is %rip.
    assignToArrayRange(FE.Builder, Address, Eight8, 0, 16);
    return false;
  }

  llvm::Type *adjustInlineAsmType(Emit::FunctionEmitter &FE,
                                  llvm::StringRef Constraint,
                                  llvm::Type *Ty) const override {
    return x86AdjustInlineAsmType(FE, Constraint, Ty);
  }

  bool isNoProtoCallVariadic(const CallArgList &args,
                             const FunctionNoProtoType *fnType) const override {
    // The default CC on x86-64 sets %al to the number of SSA
    // registers used, and GCC sets this when calling an unprototyped
    // function, so we override the default behavior.  However, don't do
    // that when AVX types are involved: the ABI explicitly states it is
    // undefined, and it doesn't work in practice because of how the ABI
    // defines varargs anyway.
    if (fnType->getCallConv() == CC_C) {
      bool HasAVXType = false;
      for (CallArgList::const_iterator it = args.begin(), ie = args.end();
           it != ie; ++it) {
        if (getABIInfo<X86_64ABIInfo>().isPassedUsingAVXType(it->Ty)) {
          HasAVXType = true;
          break;
        }
      }

      if (!HasAVXType)
        return true;
    }

    return TargetCodeGenInfo::isNoProtoCallVariadic(args, fnType);
  }

  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           Emit::ModuleEmitter &ME) const override {
    if (GV->isDeclaration())
      return;
    if (const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(D)) {
      if (FD->hasAttr<X86ForceAlignArgPointerAttr>()) {
        llvm::Function *Fn = cast<llvm::Function>(GV);
        Fn->addFnAttr("stackrealign");
      }

      addX86InterruptAttrs(FD, GV, ME);
    }
  }

  void checkFunctionCallABI(ModuleEmitter &ME, SourceLocation CallLoc,
                            const FunctionDecl *Caller,
                            const FunctionDecl *Callee,
                            const CallArgList &Args) const override;
};
} // namespace

void initFeatureMaps(const TreeContext &Ctx, llvm::StringMap<bool> &CallerMap,
                     const FunctionDecl *Caller,
                     llvm::StringMap<bool> &CalleeMap,
                     const FunctionDecl *Callee) {
  if (CalleeMap.empty() && CallerMap.empty()) {
    // The caller is potentially nullptr in the case where the call isn't in a
    // function.  In this case, the getFunctionFeatureMap ensures we just get
    // the TU level setting (since it cannot be modified by 'target'..
    Ctx.getFunctionFeatureMap(CallerMap, Caller);
    Ctx.getFunctionFeatureMap(CalleeMap, Callee);
  }
}

bool checkAVXParamFeature(DiagnosticsEngine &Diag, SourceLocation CallLoc,
                          const llvm::StringMap<bool> &CallerMap,
                          const llvm::StringMap<bool> &CalleeMap, QualType Ty,
                          llvm::StringRef Feature, bool IsArgument) {
  bool CallerHasFeat = CallerMap.lookup(Feature);
  bool CalleeHasFeat = CalleeMap.lookup(Feature);
  if (!CallerHasFeat && !CalleeHasFeat)
    return Diag.Report(CallLoc, diag::warn_avx_calling_convention)
           << IsArgument << Ty << Feature;

  // Mixing calling conventions here is very clearly an error.
  if (!CallerHasFeat || !CalleeHasFeat)
    return Diag.Report(CallLoc, diag::err_avx_calling_convention)
           << IsArgument << Ty << Feature;

  // Else, both caller and callee have the required feature, so there is no need
  // to diagnose.
  return false;
}

bool checkAVX512ParamFeature(DiagnosticsEngine &Diag, SourceLocation CallLoc,
                             const llvm::StringMap<bool> &CallerMap,
                             const llvm::StringMap<bool> &CalleeMap,
                             QualType Ty, bool IsArgument) {
  bool Caller256 = CallerMap.lookup("avx512f") && !CallerMap.lookup("evex512");
  bool Callee256 = CalleeMap.lookup("avx512f") && !CalleeMap.lookup("evex512");

  // Forbid 512-bit or larger vector pass or return when we disabled ZMM
  // instructions.
  if (Caller256 || Callee256)
    return Diag.Report(CallLoc, diag::err_avx_calling_convention)
           << IsArgument << Ty << "evex512";

  return checkAVXParamFeature(Diag, CallLoc, CallerMap, CalleeMap, Ty,
                              "avx512f", IsArgument);
}

bool checkAVXParam(DiagnosticsEngine &Diag, TreeContext &Ctx,
                   SourceLocation CallLoc,
                   const llvm::StringMap<bool> &CallerMap,
                   const llvm::StringMap<bool> &CalleeMap, QualType Ty,
                   bool IsArgument) {
  uint64_t Size = Ctx.getTypeSize(Ty);
  if (Size > 256)
    return checkAVX512ParamFeature(Diag, CallLoc, CallerMap, CalleeMap, Ty,
                                   IsArgument);

  if (Size > 128)
    return checkAVXParamFeature(Diag, CallLoc, CallerMap, CalleeMap, Ty, "avx",
                                IsArgument);

  return false;
}

void X86_64TargetCodeGenInfo::checkFunctionCallABI(
    ModuleEmitter &ME, SourceLocation CallLoc, const FunctionDecl *Caller,
    const FunctionDecl *Callee, const CallArgList &Args) const {
  llvm::StringMap<bool> CallerMap;
  llvm::StringMap<bool> CalleeMap;
  unsigned ArgIndex = 0;

  // We need to loop through the actual call arguments rather than the
  // function's parameters, in case this variadic.
  for (const CallArg &Arg : Args) {
    // The "avx" feature changes how vectors >128 in size are passed. "avx512f"
    // additionally changes how vectors >256 in size are passed. Like GCC, we
    // warn when a function is called with an argument where this will change.
    // Unlike GCC, we also error when it is an obvious ABI mismatch, that is,
    // the caller and callee features are mismatched.
    // Unfortunately, we cannot do this diagnostic in SEMA, since the callee can
    // change its ABI with attribute-target after this call.
    if (Arg.getType()->isVectorType() &&
        ME.getContext().getTypeSize(Arg.getType()) > 128) {
      initFeatureMaps(ME.getContext(), CallerMap, Caller, CalleeMap, Callee);
      QualType Ty = Arg.getType();
      // The CallArg seems to have desugared the type already, so for clearer
      // diagnostics, replace it with the type in the FunctionDecl if possible.
      if (ArgIndex < Callee->getNumParams())
        Ty = Callee->getParamDecl(ArgIndex)->getType();

      if (checkAVXParam(ME.getDiags(), ME.getContext(), CallLoc, CallerMap,
                        CalleeMap, Ty, /*IsArgument*/ true))
        return;
    }
    ++ArgIndex;
  }

  // Check return always, as we don't have a good way of knowing in codegen
  // whether this value is used, tail-called, etc.
  if (Callee->getReturnType()->isVectorType() &&
      ME.getContext().getTypeSize(Callee->getReturnType()) > 128) {
    initFeatureMaps(ME.getContext(), CallerMap, Caller, CalleeMap, Callee);
    checkAVXParam(ME.getDiags(), ME.getContext(), CallLoc, CallerMap, CalleeMap,
                  Callee->getReturnType(),
                  /*IsArgument*/ false);
  }
}

std::string TargetCodeGenInfo::qualifyWindowsLibrary(llvm::StringRef Lib) {
  // If the argument does not end in .lib, automatically add the suffix.
  // If the argument contains a space, enclose it in quotes.
  // This matches the behavior of MSVC.
  bool Quote = Lib.contains(' ');
  std::string ArgStr = Quote ? "\"" : "";
  ArgStr += Lib;
  if (!Lib.ends_with_insensitive(".lib") && !Lib.ends_with_insensitive(".a"))
    ArgStr += ".lib";
  ArgStr += Quote ? "\"" : "";
  return ArgStr;
}

// ===----------------------------------------------------------------------===
// WinX86_64TargetCodeGenInfo
// ===----------------------------------------------------------------------===

namespace {
class WinX86_64TargetCodeGenInfo : public TargetCodeGenInfo {
public:
  WinX86_64TargetCodeGenInfo(Emit::TypeEmitter &CGT, X86AVXABILevel AVXLevel)
      : TargetCodeGenInfo(std::make_unique<WinX86_64ABIInfo>(CGT, AVXLevel)) {}

  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           Emit::ModuleEmitter &ME) const override;

  int getDwarfEHStackPointer(Emit::ModuleEmitter &ME) const override {
    return 7;
  }

  bool initDwarfEHRegSizeTable(Emit::FunctionEmitter &FE,
                               llvm::Value *Address) const override {
    llvm::Value *Eight8 = llvm::ConstantInt::get(FE.Int8Ty, 8);

    // 0-15 are the 16 integer registers.
    // 16 is %rip.
    assignToArrayRange(FE.Builder, Address, Eight8, 0, 16);
    return false;
  }

  void getDependentLibraryOption(llvm::StringRef Lib,
                                 llvm::SmallString<24> &Opt) const override {
    Opt = "--defaultlib=";
    Opt += qualifyWindowsLibrary(Lib);
  }

  void getDetectMismatchOption(llvm::StringRef Name, llvm::StringRef Value,
                               llvm::SmallString<32> &Opt) const override {
    Opt = "--failifmismatch=\"" + Name.str() + "=" + Value.str() + "\"";
  }
};
} // namespace

void WinX86_64TargetCodeGenInfo::setTargetAttributes(
    const Decl *D, llvm::GlobalValue *GV, Emit::ModuleEmitter &ME) const {
  TargetCodeGenInfo::setTargetAttributes(D, GV, ME);
  if (GV->isDeclaration())
    return;
  if (const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(D)) {
    if (FD->hasAttr<X86ForceAlignArgPointerAttr>()) {
      llvm::Function *Fn = cast<llvm::Function>(GV);
      Fn->addFnAttr("stackrealign");
    }

    addX86InterruptAttrs(FD, GV, ME);
  }

  addStackProbeTargetAttributes(D, GV, ME);
}

// ===----------------------------------------------------------------------===
// X86_64ABIInfo implementation
// ===----------------------------------------------------------------------===

void X86_64ABIInfo::postMerge(unsigned AggregateSize, Class &Lo,
                              Class &Hi) const {
  // AMD64-ABI 3.2.3p2: Rule 5. Then a post merger cleanup is done:
  //
  // (a) If one of the classes is Memory, the whole argument is passed in
  //     memory.
  //
  // (b) If X87UP is not preceded by X87, the whole argument is passed in
  //     memory.
  //
  // (c) If the size of the aggregate exceeds two eightbytes and the first
  //     eightbyte isn't SSE or any other eightbyte isn't SSEUP, the whole
  //     argument is passed in memory. NOTE: This is necessary to keep the
  //     ABI working for processors that don't support the __m256 type.
  //
  // (d) If SSEUP is not preceded by SSE or SSEUP, it is converted to SSE.
  //
  // Some of these are enforced by the merging logic.  Others can arise
  // only with unions; for example:
  //   union { _Complex double; unsigned; }
  //
  // Note that clauses (b) and (c) were added in 0.98.
  //
  if (Hi == Memory)
    Lo = Memory;
  if (Hi == X87Up && Lo != X87 && honorsRevision0_98())
    Lo = Memory;
  if (AggregateSize > 128 && (Lo != SSE || Hi != SSEUp))
    Lo = Memory;
  if (Hi == SSEUp && Lo != SSE)
    Hi = SSE;
}

X86_64ABIInfo::Class X86_64ABIInfo::merge(Class Accum, Class Field) {
  // AMD64-ABI 3.2.3p2: Rule 4. Each field of an object is
  // classified recursively so that always two fields are
  // considered. The resulting class is calculated according to
  // the classes of the fields in the eightbyte:
  //
  // (a) If both classes are equal, this is the resulting class.
  //
  // (b) If one of the classes is NO_CLASS, the resulting class is
  // the other class.
  //
  // (c) If one of the classes is MEMORY, the result is the MEMORY
  // class.
  //
  // (d) If one of the classes is INTEGER, the result is the
  // INTEGER.
  //
  // (e) If one of the classes is X87, X87UP, COMPLEX_X87 class,
  // MEMORY is used as class.
  //
  // (f) Otherwise class SSE is used.

  // Accum should never be memory (we should have returned) or
  // ComplexX87 (because this cannot be passed in a structure).
  assert((Accum != Memory && Accum != ComplexX87) &&
         "Invalid accumulated classification during merge.");
  if (Accum == Field || Field == NoClass)
    return Accum;
  if (Field == Memory)
    return Memory;
  if (Accum == NoClass)
    return Field;
  if (Accum == Integer || Field == Integer)
    return Integer;
  if (Field == X87 || Field == X87Up || Field == ComplexX87 || Accum == X87 ||
      Accum == X87Up)
    return Memory;
  return SSE;
}

void X86_64ABIInfo::classify(QualType Ty, uint64_t OffsetBase, Class &Lo,
                             Class &Hi, bool isNamedArg, bool IsRegCall) const {

  Lo = Hi = NoClass;

  Class &Current = OffsetBase < 64 ? Lo : Hi;
  Current = Memory;

  if (const BuiltinType *BT = Ty->getAs<BuiltinType>()) {
    BuiltinType::Kind k = BT->getKind();

    if (k == BuiltinType::Void) {
      Current = NoClass;
    } else if (k == BuiltinType::Int128 || k == BuiltinType::UInt128) {
      Lo = Integer;
      Hi = Integer;
    } else if (k >= BuiltinType::Bool && k <= BuiltinType::LongLong) {
      Current = Integer;
    } else if (k == BuiltinType::Float || k == BuiltinType::Double ||
               k == BuiltinType::Float16 || k == BuiltinType::BFloat16) {
      Current = SSE;
    } else if (k == BuiltinType::LongDouble) {
      const llvm::fltSemantics *LDF = &getTarget().getLongDoubleFormat();
      if (LDF == &llvm::APFloat::IEEEquad()) {
        Lo = SSE;
        Hi = SSEUp;
      } else if (LDF == &llvm::APFloat::x87DoubleExtended()) {
        Lo = X87;
        Hi = X87Up;
      } else if (LDF == &llvm::APFloat::IEEEdouble()) {
        Current = SSE;
      } else
        llvm_unreachable("unexpected long double representation!");
    }
    return;
  }

  if (const EnumType *ET = Ty->getAs<EnumType>()) {
    // Classify the underlying integer type.
    classify(ET->getDecl()->getIntegerType(), OffsetBase, Lo, Hi, isNamedArg);
    return;
  }

  if (Ty->hasPointerRepresentation()) {
    Current = Integer;
    return;
  }

  if (const VectorType *VT = Ty->getAs<VectorType>()) {
    uint64_t Size = getContext().getTypeSize(VT);
    if (Size == 1 || Size == 8 || Size == 16 || Size == 32) {
      // gcc passes the following as integer:
      // 4 bytes - <4 x char>, <2 x short>, <1 x int>, <1 x float>
      // 2 bytes - <2 x char>, <1 x short>
      // 1 byte  - <1 x char>
      Current = Integer;

      // If this type crosses an eightbyte boundary, it should be
      // split.
      uint64_t EB_Lo = (OffsetBase) / 64;
      uint64_t EB_Hi = (OffsetBase + Size - 1) / 64;
      if (EB_Lo != EB_Hi)
        Hi = Lo;
    } else if (Size == 64) {
      QualType ElementType = VT->getElementType();

      // gcc passes <1 x double> in memory. :(
      if (ElementType->isSpecificBuiltinType(BuiltinType::Double))
        return;

      // gcc passes <1 x long long> as SSE but NeverC unconditionally
      // passes them as integer.  For platforms where NeverC is the de facto
      // platform compiler, we must continue to use integer.
      if (!classifyIntegerMMXAsSSE() &&
          (ElementType->isSpecificBuiltinType(BuiltinType::LongLong) ||
           ElementType->isSpecificBuiltinType(BuiltinType::ULongLong) ||
           ElementType->isSpecificBuiltinType(BuiltinType::Long) ||
           ElementType->isSpecificBuiltinType(BuiltinType::ULong)))
        Current = Integer;
      else
        Current = SSE;

      // If this type crosses an eightbyte boundary, it should be
      // split.
      if (OffsetBase && OffsetBase != 64)
        Hi = Lo;
    } else if (Size == 128 ||
               (isNamedArg && Size <= getNativeVectorSizeForAVXABI(AVXLevel))) {
      QualType ElementType = VT->getElementType();

      // gcc passes 256 and 512 bit <X x __int128> vectors in memory. :(
      if (passInt128VectorsInMem() && Size != 128 &&
          (ElementType->isSpecificBuiltinType(BuiltinType::Int128) ||
           ElementType->isSpecificBuiltinType(BuiltinType::UInt128)))
        return;

      // Arguments of 256-bits are split into four eightbyte chunks. The
      // least significant one belongs to class SSE and all the others to class
      // SSEUP. The original Lo and Hi design considers that types can't be
      // greater than 128-bits, so a 64-bit split in Hi and Lo makes sense.
      // This design isn't correct for 256-bits, but since there're no cases
      // where the upper parts would need to be inspected, avoid adding
      // complexity and just consider Hi to match the 64-256 part.
      //
      // Note that per 3.5.7 of AMD64-ABI, 256-bit args are only passed in
      // registers if they are "named", i.e. not part of the "..." of a
      // variadic function.
      //
      // Similarly, per 3.2.3. of the AVX512 draft, 512-bits ("named") args are
      // split into eight eightbyte chunks, one SSE and seven SSEUP.
      Lo = SSE;
      Hi = SSEUp;
    }
    return;
  }

  if (const ComplexType *CT = Ty->getAs<ComplexType>()) {
    QualType ET = getContext().getCanonicalType(CT->getElementType());

    uint64_t Size = getContext().getTypeSize(Ty);
    if (ET->isIntegralOrEnumerationType()) {
      if (Size <= 64)
        Current = Integer;
      else if (Size <= 128)
        Lo = Hi = Integer;
    } else if (ET->isFloat16Type() || ET == getContext().FloatTy ||
               ET->isBFloat16Type()) {
      Current = SSE;
    } else if (ET == getContext().DoubleTy) {
      Lo = Hi = SSE;
    } else if (ET == getContext().LongDoubleTy) {
      const llvm::fltSemantics *LDF = &getTarget().getLongDoubleFormat();
      if (LDF == &llvm::APFloat::IEEEquad())
        Current = Memory;
      else if (LDF == &llvm::APFloat::x87DoubleExtended())
        Current = ComplexX87;
      else if (LDF == &llvm::APFloat::IEEEdouble())
        Lo = Hi = SSE;
      else
        llvm_unreachable("unexpected long double representation!");
    }

    // If this complex type crosses an eightbyte boundary then it
    // should be split.
    uint64_t EB_Real = (OffsetBase) / 64;
    uint64_t EB_Imag = (OffsetBase + getContext().getTypeSize(ET)) / 64;
    if (Hi == NoClass && EB_Real != EB_Imag)
      Hi = Lo;

    return;
  }

  if (const auto *EITy = Ty->getAs<BitIntType>()) {
    if (EITy->getNumBits() <= 64)
      Current = Integer;
    else if (EITy->getNumBits() <= 128)
      Lo = Hi = Integer;
    // Larger values need to get passed in memory.
    return;
  }

  if (const ConstantArrayType *AT = getContext().getAsConstantArrayType(Ty)) {
    // Arrays are treated like structures.

    uint64_t Size = getContext().getTypeSize(Ty);

    // AMD64-ABI 3.2.3p2: Rule 1. If the size of an object is larger
    // than eight eightbytes, ..., it has class MEMORY.
    // regcall ABI doesn't have limitation to an object. The only limitation
    // is the free registers, which will be checked in computeInfo.
    if (!IsRegCall && Size > 512)
      return;

    // AMD64-ABI 3.2.3p2: Rule 1. If ..., or it contains unaligned
    // fields, it has class MEMORY.
    //
    // Only need to check alignment of array base.
    if (OffsetBase % getContext().getTypeAlign(AT->getElementType()))
      return;

    // Otherwise implement simplified merge. We could be smarter about
    // this, but it isn't worth it and would be harder to verify.
    Current = NoClass;
    uint64_t EltSize = getContext().getTypeSize(AT->getElementType());
    uint64_t ArraySize = AT->getSize().getZExtValue();

    // The only case a 256-bit wide vector could be used is when the array
    // contains a single 256-bit element. Since Lo and Hi logic isn't extended
    // to work for sizes wider than 128, early check and fallback to memory.
    //
    if (Size > 128 &&
        (Size != EltSize || Size > getNativeVectorSizeForAVXABI(AVXLevel)))
      return;

    for (uint64_t i = 0, Offset = OffsetBase; i < ArraySize;
         ++i, Offset += EltSize) {
      Class FieldLo, FieldHi;
      classify(AT->getElementType(), Offset, FieldLo, FieldHi, isNamedArg);
      Lo = merge(Lo, FieldLo);
      Hi = merge(Hi, FieldHi);
      if (Lo == Memory || Hi == Memory)
        break;
    }

    postMerge(Size, Lo, Hi);
    assert((Hi != SSEUp || Lo == SSE) && "Invalid SSEUp array classification.");
    return;
  }

  if (const RecordType *RT = Ty->getAs<RecordType>()) {
    uint64_t Size = getContext().getTypeSize(Ty);

    // AMD64-ABI 3.2.3p2: Rule 1. If the size of an object is larger
    // than eight eightbytes, ..., it has class MEMORY.
    if (Size > 512)
      return;

    // AMD64-ABI 3.2.3p2: Rule 2. Indirect passing check.
    if (getRecordArgABI(RT))
      return;

    const RecordDecl *RD = RT->getDecl();

    // Assume variable sized types are passed in memory.
    if (RD->hasFlexibleArrayMember())
      return;

    const StructRecordLayout &Layout = getContext().getStructRecordLayout(RD);

    // Reset Lo class, this will be recomputed.
    Current = NoClass;

    // Classify the fields one at a time, merging the results.
    unsigned idx = 0;
    bool UseABI11Compat = getContext().getLangOpts().getNeverCABICompat() <=
                          LangOptions::NeverCABI::Ver11;
    bool IsUnion = RT->isUnionType() && !UseABI11Compat;

    for (RecordDecl::field_iterator i = RD->field_begin(), e = RD->field_end();
         i != e; ++i, ++idx) {
      uint64_t Offset = OffsetBase + Layout.getFieldOffset(idx);
      bool BitField = i->isBitField();

      // Ignore padding bit-fields.
      if (BitField && i->isUnnamedBitfield())
        continue;

      // AMD64-ABI 3.2.3p2: Rule 1. If the size of an object is larger than
      // eight eightbytes, or it contains unaligned fields, it has class MEMORY.
      //
      // The only case a 256-bit or a 512-bit wide vector could be used is when
      // the struct contains a single 256-bit or 512-bit element. Early check
      // and fallback to memory.
      //
      if (Size > 128 &&
          ((!IsUnion && Size != getContext().getTypeSize(i->getType())) ||
           Size > getNativeVectorSizeForAVXABI(AVXLevel))) {
        Lo = Memory;
        postMerge(Size, Lo, Hi);
        return;
      }
      // Note, skip this test for bit-fields, see below.
      if (!BitField && Offset % getContext().getTypeAlign(i->getType())) {
        Lo = Memory;
        postMerge(Size, Lo, Hi);
        return;
      }

      // Classify this field.
      //
      // AMD64-ABI 3.2.3p2: Rule 3. If the size of the aggregate
      // exceeds a single eightbyte, each is classified
      // separately. Each eightbyte gets initialized to class
      // NO_CLASS.
      Class FieldLo, FieldHi;

      // Bit-fields require special handling, they do not force the
      // structure to be passed in memory even if unaligned, and
      // therefore they can straddle an eightbyte.
      if (BitField) {
        assert(!i->isUnnamedBitfield());
        uint64_t Offset = OffsetBase + Layout.getFieldOffset(idx);
        uint64_t Size = i->getBitWidthValue(getContext());

        uint64_t EB_Lo = Offset / 64;
        uint64_t EB_Hi = (Offset + Size - 1) / 64;

        if (EB_Lo) {
          assert(EB_Hi == EB_Lo && "Invalid classification, type > 16 bytes.");
          FieldLo = NoClass;
          FieldHi = Integer;
        } else {
          FieldLo = Integer;
          FieldHi = EB_Hi ? Integer : NoClass;
        }
      } else
        classify(i->getType(), Offset, FieldLo, FieldHi, isNamedArg);
      Lo = merge(Lo, FieldLo);
      Hi = merge(Hi, FieldHi);
      if (Lo == Memory || Hi == Memory)
        break;
    }

    postMerge(Size, Lo, Hi);
  }
}

ABIArgInfo X86_64ABIInfo::getIndirectReturnResult(QualType Ty) const {
  // If this is a scalar LLVM value then assume LLVM will pass it in the right
  // place naturally.
  if (!isAggregateTypeForABI(Ty)) {
    // Treat an enum type as its underlying type.
    if (const EnumType *EnumTy = Ty->getAs<EnumType>())
      Ty = EnumTy->getDecl()->getIntegerType();

    if (Ty->isBitIntType())
      return getNaturalAlignIndirect(Ty);

    return (isPromotableIntegerTypeForABI(Ty) ? ABIArgInfo::getExtend(Ty)
                                              : ABIArgInfo::getDirect());
  }

  return getNaturalAlignIndirect(Ty);
}

bool X86_64ABIInfo::IsIllegalVectorType(QualType Ty) const {
  if (const VectorType *VecTy = Ty->getAs<VectorType>()) {
    uint64_t Size = getContext().getTypeSize(VecTy);
    unsigned LargestVector = getNativeVectorSizeForAVXABI(AVXLevel);
    if (Size <= 64 || Size > LargestVector)
      return true;
    QualType EltTy = VecTy->getElementType();
    if (passInt128VectorsInMem() &&
        (EltTy->isSpecificBuiltinType(BuiltinType::Int128) ||
         EltTy->isSpecificBuiltinType(BuiltinType::UInt128)))
      return true;
  }

  return false;
}

ABIArgInfo X86_64ABIInfo::getIndirectResult(QualType Ty,
                                            unsigned freeIntRegs) const {
  // If this is a scalar LLVM value then assume LLVM will pass it in the right
  // place naturally.
  //
  // This assumption is optimistic, as there could be free registers available
  // when we need to pass this argument in memory, and LLVM could try to pass
  // the argument in the free register. This does not seem to happen currently,
  // but this code would be much safer if we could mark the argument with
  // 'onstack'. See PR12193.
  if (!isAggregateTypeForABI(Ty) && !IsIllegalVectorType(Ty) &&
      !Ty->isBitIntType()) {
    // Treat an enum type as its underlying type.
    if (const EnumType *EnumTy = Ty->getAs<EnumType>())
      Ty = EnumTy->getDecl()->getIntegerType();

    return (isPromotableIntegerTypeForABI(Ty) ? ABIArgInfo::getExtend(Ty)
                                              : ABIArgInfo::getDirect());
  }

  if (CGABI::RecordArgABI RAA = getRecordArgABI(Ty))
    return getNaturalAlignIndirect(Ty, RAA == CGABI::RAA_DirectInMemory);

  // Compute the byval alignment. We specify the alignment of the byval in all
  // cases so that the mid-level optimizer knows the alignment of the byval.
  unsigned Align = std::max(getContext().getTypeAlign(Ty) / 8, 8U);

  // Attempt to avoid passing indirect results using byval when possible. This
  // is important for good codegen.
  //
  // We do this by coercing the value into a scalar type which the backend can
  // handle naturally (i.e., without using byval).
  //
  // For simplicity, we currently only do this when we have exhausted all of the
  // free integer registers. Doing this when there are free integer registers
  // would require more care, as we would have to ensure that the coerced value
  // did not claim the unused register. That would require either reording the
  // arguments to the function (so that any subsequent inreg values came first),
  // or only doing this optimization when there were no following arguments that
  // might be inreg.
  //
  // We currently expect it to be rare (particularly in well written code) for
  // arguments to be passed on the stack when there are still free integer
  // registers available (this would typically imply large structs being passed
  // by value), so this seems like a fair tradeoff for now.
  //
  // We can revisit this if the backend grows support for 'onstack' parameter
  // attributes. See PR12193.
  if (freeIntRegs == 0) {
    uint64_t Size = getContext().getTypeSize(Ty);

    // If this type fits in an eightbyte, coerce it into the matching integral
    // type, which will end up on the stack (with alignment 8).
    if (Align == 8 && Size <= 64)
      return ABIArgInfo::getDirect(
          llvm::IntegerType::get(getVMContext(), Size));
  }

  return ABIArgInfo::getIndirect(CharUnits::fromQuantity(Align));
}

llvm::Type *X86_64ABIInfo::GetByteVectorType(QualType Ty) const {
  // Wrapper structs/arrays that only contain vectors are passed just like
  // vectors; strip them off if present.
  if (const Type *InnerTy = isSingleElementStruct(Ty, getContext()))
    Ty = QualType(InnerTy, 0);

  llvm::Type *IRType = CGT.convertType(Ty);
  if (isa<llvm::VectorType>(IRType)) {
    // Don't pass vXi128 vectors in their native type, the backend can't
    // legalize them.
    if (passInt128VectorsInMem() &&
        cast<llvm::VectorType>(IRType)->getElementType()->isIntegerTy(128)) {
      // Use a vXi64 vector.
      uint64_t Size = getContext().getTypeSize(Ty);
      return llvm::FixedVectorType::get(llvm::Type::getInt64Ty(getVMContext()),
                                        Size / 64);
    }

    return IRType;
  }

  if (IRType->getTypeID() == llvm::Type::FP128TyID)
    return IRType;

  // We couldn't find the preferred IR vector type for 'Ty'.
  uint64_t Size = getContext().getTypeSize(Ty);
  assert((Size == 128 || Size == 256 || Size == 512) && "Invalid type found!");

  return llvm::FixedVectorType::get(llvm::Type::getDoubleTy(getVMContext()),
                                    Size / 64);
}

namespace {
bool bitsContainNoUserData(QualType Ty, unsigned StartBit, unsigned EndBit,
                           TreeContext &Context) {
  // If the bytes being queried are off the end of the type, there is no user
  // data hiding here.  This handles analysis of builtins, vectors and other
  // types that don't contain interesting padding.
  unsigned TySize = (unsigned)Context.getTypeSize(Ty);
  if (TySize <= StartBit)
    return true;

  if (const ConstantArrayType *AT = Context.getAsConstantArrayType(Ty)) {
    unsigned EltSize = (unsigned)Context.getTypeSize(AT->getElementType());
    unsigned NumElts = (unsigned)AT->getSize().getZExtValue();

    for (unsigned i = 0; i != NumElts; ++i) {
      // If the element is after the span we care about, then we're done..
      unsigned EltOffset = i * EltSize;
      if (EltOffset >= EndBit)
        break;

      unsigned EltStart = EltOffset < StartBit ? StartBit - EltOffset : 0;
      if (!bitsContainNoUserData(AT->getElementType(), EltStart,
                                 EndBit - EltOffset, Context))
        return false;
    }
    // If it overlaps no elements, then it is safe to process as padding.
    return true;
  }

  if (const RecordType *RT = Ty->getAs<RecordType>()) {
    const RecordDecl *RD = RT->getDecl();
    const StructRecordLayout &Layout = Context.getStructRecordLayout(RD);

    // Verify that no field has data that overlaps the region of interest.  Yes
    // this could be sped up a lot by being smarter about queried fields,
    // however we're only looking at structs up to 16 bytes, so we don't care
    // much.
    unsigned idx = 0;
    for (RecordDecl::field_iterator i = RD->field_begin(), e = RD->field_end();
         i != e; ++i, ++idx) {
      unsigned FieldOffset = (unsigned)Layout.getFieldOffset(idx);

      // If we found a field after the region we care about, then we're done.
      if (FieldOffset >= EndBit)
        break;

      unsigned FieldStart = FieldOffset < StartBit ? StartBit - FieldOffset : 0;
      if (!bitsContainNoUserData(i->getType(), FieldStart, EndBit - FieldOffset,
                                 Context))
        return false;
    }

    // If nothing in this record overlapped the area of interest, then we're
    // clean.
    return true;
  }

  return false;
}

llvm::Type *getFPTypeAtOffset(llvm::Type *IRType, unsigned IROffset,
                              const llvm::DataLayout &TD) {
  if (IROffset == 0 && IRType->isFloatingPointTy())
    return IRType;

  // If this is a struct, recurse into the field at the specified offset.
  if (llvm::StructType *STy = dyn_cast<llvm::StructType>(IRType)) {
    if (!STy->getNumContainedTypes())
      return nullptr;

    const llvm::StructLayout *SL = TD.getStructLayout(STy);
    unsigned Elt = SL->getElementContainingOffset(IROffset);
    IROffset -= SL->getElementOffset(Elt);
    return getFPTypeAtOffset(STy->getElementType(Elt), IROffset, TD);
  }

  // If this is an array, recurse into the field at the specified offset.
  if (llvm::ArrayType *ATy = dyn_cast<llvm::ArrayType>(IRType)) {
    llvm::Type *EltTy = ATy->getElementType();
    unsigned EltSize = TD.getTypeAllocSize(EltTy);
    IROffset -= IROffset / EltSize * EltSize;
    return getFPTypeAtOffset(EltTy, IROffset, TD);
  }

  return nullptr;
}

llvm::Type *X86_64ABIInfo::GetSSETypeAtOffset(llvm::Type *IRType,
                                              unsigned IROffset,
                                              QualType SourceTy,
                                              unsigned SourceOffset) const {
  const llvm::DataLayout &TD = getDataLayout();
  unsigned SourceSize =
      (unsigned)getContext().getTypeSize(SourceTy) / 8 - SourceOffset;
  llvm::Type *T0 = getFPTypeAtOffset(IRType, IROffset, TD);
  if (!T0 || T0->isDoubleTy())
    return llvm::Type::getDoubleTy(getVMContext());

  llvm::Type *T1 = nullptr;
  unsigned T0Size = TD.getTypeAllocSize(T0);
  if (SourceSize > T0Size)
    T1 = getFPTypeAtOffset(IRType, IROffset + T0Size, TD);
  if (T1 == nullptr) {
    // Check if IRType is a half/bfloat + float. float type will be in
    // IROffset+4 due to its alignment.
    if (T0->is16bitFPTy() && SourceSize > 4)
      T1 = getFPTypeAtOffset(IRType, IROffset + 4, TD);
    // If we can't get a second FP type, return a simple half or float.
    // avx512fp16-abi.c:pr51813_2 shows it works to return float for
    // {float, i8} too.
    if (T1 == nullptr)
      return T0;
  }

  if (T0->isFloatTy() && T1->isFloatTy())
    return llvm::FixedVectorType::get(T0, 2);

  if (T0->is16bitFPTy() && T1->is16bitFPTy()) {
    llvm::Type *T2 = nullptr;
    if (SourceSize > 4)
      T2 = getFPTypeAtOffset(IRType, IROffset + 4, TD);
    if (T2 == nullptr)
      return llvm::FixedVectorType::get(T0, 2);
    return llvm::FixedVectorType::get(T0, 4);
  }

  if (T0->is16bitFPTy() || T1->is16bitFPTy())
    return llvm::FixedVectorType::get(llvm::Type::getHalfTy(getVMContext()), 4);

  return llvm::Type::getDoubleTy(getVMContext());
}

llvm::Type *X86_64ABIInfo::GetINTEGERTypeAtOffset(llvm::Type *IRType,
                                                  unsigned IROffset,
                                                  QualType SourceTy,
                                                  unsigned SourceOffset) const {
  // If we're dealing with an un-offset LLVM IR type, then it means that we're
  // returning an 8-byte unit starting with it.  See if we can safely use it.
  if (IROffset == 0) {
    // Pointers and int64's always fill the 8-byte unit.
    if (isa<llvm::PointerType>(IRType) || IRType->isIntegerTy(64))
      return IRType;

    // If we have a 1/2/4-byte integer, we can use it only if the rest of the
    // goodness in the source type is just tail padding.  This is allowed to
    // kick in for struct {double,int} on the int, but not on
    // struct{double,int,int} because we wouldn't return the second int.  We
    // have to do this analysis on the source type because we can't depend on
    // unions being lowered a specific way etc.
    if (IRType->isIntegerTy(8) || IRType->isIntegerTy(16) ||
        IRType->isIntegerTy(32)) {
      unsigned BitWidth = cast<llvm::IntegerType>(IRType)->getBitWidth();

      if (bitsContainNoUserData(SourceTy, SourceOffset * 8 + BitWidth,
                                SourceOffset * 8 + 64, getContext()))
        return IRType;
    }
  }

  if (llvm::StructType *STy = dyn_cast<llvm::StructType>(IRType)) {
    // If this is a struct, recurse into the field at the specified offset.
    const llvm::StructLayout *SL = getDataLayout().getStructLayout(STy);
    if (IROffset < SL->getSizeInBytes()) {
      unsigned FieldIdx = SL->getElementContainingOffset(IROffset);
      IROffset -= SL->getElementOffset(FieldIdx);

      return GetINTEGERTypeAtOffset(STy->getElementType(FieldIdx), IROffset,
                                    SourceTy, SourceOffset);
    }
  }

  if (llvm::ArrayType *ATy = dyn_cast<llvm::ArrayType>(IRType)) {
    llvm::Type *EltTy = ATy->getElementType();
    unsigned EltSize = getDataLayout().getTypeAllocSize(EltTy);
    unsigned EltOffset = IROffset / EltSize * EltSize;
    return GetINTEGERTypeAtOffset(EltTy, IROffset - EltOffset, SourceTy,
                                  SourceOffset);
  }

  // Okay, we don't have any better idea of what to pass, so we pass this in an
  // integer register that isn't too big to fit the rest of the struct.
  unsigned TySizeInBytes =
      (unsigned)getContext().getTypeSizeInChars(SourceTy).getQuantity();

  assert(TySizeInBytes != SourceOffset && "Empty field?");

  // It is always safe to classify this as an integer type up to i64 that
  // isn't larger than the structure.
  return llvm::IntegerType::get(getVMContext(),
                                std::min(TySizeInBytes - SourceOffset, 8U) * 8);
}

llvm::Type *getX86_64ByValArgumentPair(llvm::Type *Lo, llvm::Type *Hi,
                                       const llvm::DataLayout &TD) {
  // In order to correctly satisfy the ABI, we need to the high part to start
  // at offset 8.  If the high and low parts we inferred are both 4-byte types
  // (e.g. i32 and i32) then the resultant struct type ({i32,i32}) won't have
  // the second element at offset 8.  Check for this:
  unsigned LoSize = (unsigned)TD.getTypeAllocSize(Lo);
  llvm::Align HiAlign = TD.getABITypeAlign(Hi);
  unsigned HiStart = llvm::alignTo(LoSize, HiAlign);
  assert(HiStart != 0 && HiStart <= 8 && "Invalid x86-64 argument pair!");

  // To handle this, we have to increase the size of the low part so that the
  // second element will start at an 8 byte offset.  We can't increase the size
  // of the second element because it might make us access off the end of the
  // struct.
  if (HiStart != 8) {
    // There are usually two sorts of types the ABI generation code can produce
    // for the low part of a pair that aren't 8 bytes in size: half, float or
    // i8/i16/i32.
    // Promote these to a larger type.
    if (Lo->isHalfTy() || Lo->isFloatTy())
      Lo = llvm::Type::getDoubleTy(Lo->getContext());
    else {
      assert((Lo->isIntegerTy() || Lo->isPointerTy()) &&
             "Invalid/unknown lo type");
      Lo = llvm::Type::getInt64Ty(Lo->getContext());
    }
  }

  llvm::StructType *Result = llvm::StructType::get(Lo, Hi);

  // Verify that the second element is at an 8-byte offset.
  assert(TD.getStructLayout(Result)->getElementOffset(1) == 8 &&
         "Invalid x86-64 argument pair!");
  return Result;
}
} // namespace

ABIArgInfo X86_64ABIInfo::classifyReturnType(QualType RetTy) const {
  // AMD64-ABI 3.2.3p4: Rule 1. Classify the return type with the
  // classification algorithm.
  X86_64ABIInfo::Class Lo, Hi;
  classify(RetTy, 0, Lo, Hi, /*isNamedArg*/ true);

  assert((Hi != Memory || Lo == Memory) && "Invalid memory classification.");
  assert((Hi != SSEUp || Lo == SSE) && "Invalid SSEUp classification.");

  llvm::Type *ResType = nullptr;
  switch (Lo) {
  case NoClass:
    if (Hi == NoClass)
      return ABIArgInfo::getIgnore();
    // If the low part is just padding, it takes no register, leave ResType
    // null.
    assert((Hi == SSE || Hi == Integer || Hi == X87Up) &&
           "Unknown missing lo part");
    break;

  case SSEUp:
  case X87Up:
    llvm_unreachable("Invalid classification for lo word.");

    // AMD64-ABI 3.2.3p4: Rule 2. Types of class memory are returned via
    // hidden argument.
  case Memory:
    return getIndirectReturnResult(RetTy);

    // AMD64-ABI 3.2.3p4: Rule 3. If the class is INTEGER, the next
    // available register of the sequence %rax, %rdx is used.
  case Integer:
    ResType = GetINTEGERTypeAtOffset(CGT.convertType(RetTy), 0, RetTy, 0);

    // If we have a sign or zero extended integer, make sure to return Extend
    // so that the parameter gets the right LLVM IR attributes.
    if (Hi == NoClass && isa<llvm::IntegerType>(ResType)) {
      // Treat an enum type as its underlying type.
      if (const EnumType *EnumTy = RetTy->getAs<EnumType>())
        RetTy = EnumTy->getDecl()->getIntegerType();

      if (RetTy->isIntegralOrEnumerationType() &&
          isPromotableIntegerTypeForABI(RetTy))
        return ABIArgInfo::getExtend(RetTy);
    }
    break;

    // AMD64-ABI 3.2.3p4: Rule 4. If the class is SSE, the next
    // available SSE register of the sequence %xmm0, %xmm1 is used.
  case SSE:
    ResType = GetSSETypeAtOffset(CGT.convertType(RetTy), 0, RetTy, 0);
    break;

    // AMD64-ABI 3.2.3p4: Rule 6. If the class is X87, the value is
    // returned on the X87 stack in %st0 as 80-bit x87 number.
  case X87:
    ResType = llvm::Type::getX86_FP80Ty(getVMContext());
    break;

    // AMD64-ABI 3.2.3p4: Rule 8. If the class is COMPLEX_X87, the real
    // part of the value is returned in %st0 and the imaginary part in
    // %st1.
  case ComplexX87:
    assert(Hi == ComplexX87 && "Unexpected ComplexX87 classification.");
    ResType = llvm::StructType::get(llvm::Type::getX86_FP80Ty(getVMContext()),
                                    llvm::Type::getX86_FP80Ty(getVMContext()));
    break;
  }

  llvm::Type *HighPart = nullptr;
  switch (Hi) {
    // Memory was handled previously and X87 should
    // never occur as a hi class.
  case Memory:
  case X87:
    llvm_unreachable("Invalid classification for hi word.");

  case ComplexX87: // Previously handled.
  case NoClass:
    break;

  case Integer:
    HighPart = GetINTEGERTypeAtOffset(CGT.convertType(RetTy), 8, RetTy, 8);
    if (Lo == NoClass) // Return HighPart at offset 8 in memory.
      return ABIArgInfo::getDirect(HighPart, 8);
    break;
  case SSE:
    HighPart = GetSSETypeAtOffset(CGT.convertType(RetTy), 8, RetTy, 8);
    if (Lo == NoClass) // Return HighPart at offset 8 in memory.
      return ABIArgInfo::getDirect(HighPart, 8);
    break;

    // AMD64-ABI 3.2.3p4: Rule 5. If the class is SSEUP, the eightbyte
    // is passed in the next available eightbyte chunk if the last used
    // vector register.
    //
    // SSEUP should always be preceded by SSE, just widen.
  case SSEUp:
    assert(Lo == SSE && "Unexpected SSEUp classification.");
    ResType = GetByteVectorType(RetTy);
    break;

    // AMD64-ABI 3.2.3p4: Rule 7. If the class is X87UP, the value is
    // returned together with the previous X87 value in %st0.
  case X87Up:
    // If X87Up is preceded by X87, we don't need to do
    // anything. However, in some cases with unions it may not be
    // preceded by X87. In such situations we follow gcc and pass the
    // extra bits in an SSE reg.
    if (Lo != X87) {
      HighPart = GetSSETypeAtOffset(CGT.convertType(RetTy), 8, RetTy, 8);
      if (Lo == NoClass) // Return HighPart at offset 8 in memory.
        return ABIArgInfo::getDirect(HighPart, 8);
    }
    break;
  }

  // If a high part was specified, merge it together with the low part.  It is
  // known to pass in the high eightbyte of the result.  We do this by forming a
  // first class struct aggregate with the high and low part: {low, high}
  if (HighPart)
    ResType = getX86_64ByValArgumentPair(ResType, HighPart, getDataLayout());

  return ABIArgInfo::getDirect(ResType);
}

ABIArgInfo
X86_64ABIInfo::classifyArgumentType(QualType Ty, unsigned freeIntRegs,
                                    unsigned &neededInt, unsigned &neededSSE,
                                    bool isNamedArg, bool IsRegCall) const {
  Ty = useFirstFieldIfTransparentUnion(Ty);

  X86_64ABIInfo::Class Lo, Hi;
  classify(Ty, 0, Lo, Hi, isNamedArg, IsRegCall);

  assert((Hi != Memory || Lo == Memory) && "Invalid memory classification.");
  assert((Hi != SSEUp || Lo == SSE) && "Invalid SSEUp classification.");

  neededInt = 0;
  neededSSE = 0;
  llvm::Type *ResType = nullptr;
  switch (Lo) {
  case NoClass:
    if (Hi == NoClass)
      return ABIArgInfo::getIgnore();
    // If the low part is just padding, it takes no register, leave ResType
    // null.
    assert((Hi == SSE || Hi == Integer || Hi == X87Up) &&
           "Unknown missing lo part");
    break;

    // AMD64-ABI 3.2.3p3: Rule 1. If the class is MEMORY, pass the argument
    // on the stack.
  case Memory:

    // AMD64-ABI 3.2.3p3: Rule 5. If the class is X87, X87UP or
    // COMPLEX_X87, it is passed in memory.
  case X87:
  case ComplexX87:
    if (getRecordArgABI(Ty) == CGABI::RAA_Indirect)
      ++neededInt;
    return getIndirectResult(Ty, freeIntRegs);

  case SSEUp:
  case X87Up:
    llvm_unreachable("Invalid classification for lo word.");

    // AMD64-ABI 3.2.3p3: Rule 2. If the class is INTEGER, the next
    // available register of the sequence %rdi, %rsi, %rdx, %rcx, %r8
    // and %r9 is used.
  case Integer:
    ++neededInt;

    // Pick an 8-byte type based on the preferred type.
    ResType = GetINTEGERTypeAtOffset(CGT.convertType(Ty), 0, Ty, 0);

    // If we have a sign or zero extended integer, make sure to return Extend
    // so that the parameter gets the right LLVM IR attributes.
    if (Hi == NoClass && isa<llvm::IntegerType>(ResType)) {
      // Treat an enum type as its underlying type.
      if (const EnumType *EnumTy = Ty->getAs<EnumType>())
        Ty = EnumTy->getDecl()->getIntegerType();

      if (Ty->isIntegralOrEnumerationType() &&
          isPromotableIntegerTypeForABI(Ty))
        return ABIArgInfo::getExtend(Ty);
    }

    break;

    // AMD64-ABI 3.2.3p3: Rule 3. If the class is SSE, the next
    // available SSE register is used, the registers are taken in the
    // order from %xmm0 to %xmm7.
  case SSE: {
    llvm::Type *IRType = CGT.convertType(Ty);
    ResType = GetSSETypeAtOffset(IRType, 0, Ty, 0);
    ++neededSSE;
    break;
  }
  }

  llvm::Type *HighPart = nullptr;
  switch (Hi) {
    // Memory was handled previously, ComplexX87 and X87 should
    // never occur as hi classes, and X87Up must be preceded by X87,
    // which is passed in memory.
  case Memory:
  case X87:
  case ComplexX87:
    llvm_unreachable("Invalid classification for hi word.");

  case NoClass:
    break;

  case Integer:
    ++neededInt;
    // Pick an 8-byte type based on the preferred type.
    HighPart = GetINTEGERTypeAtOffset(CGT.convertType(Ty), 8, Ty, 8);

    if (Lo == NoClass) // Pass HighPart at offset 8 in memory.
      return ABIArgInfo::getDirect(HighPart, 8);
    break;

    // X87Up generally doesn't occur here (long double is passed in
    // memory), except in situations involving unions.
  case X87Up:
  case SSE:
    HighPart = GetSSETypeAtOffset(CGT.convertType(Ty), 8, Ty, 8);

    if (Lo == NoClass) // Pass HighPart at offset 8 in memory.
      return ABIArgInfo::getDirect(HighPart, 8);

    ++neededSSE;
    break;

    // AMD64-ABI 3.2.3p3: Rule 4. If the class is SSEUP, the
    // eightbyte is passed in the upper half of the last used SSE
    // register.  This only happens when 128-bit vectors are passed.
  case SSEUp:
    assert(Lo == SSE && "Unexpected SSEUp classification");
    ResType = GetByteVectorType(Ty);
    break;
  }

  // If a high part was specified, merge it together with the low part.  It is
  // known to pass in the high eightbyte of the result.  We do this by forming a
  // first class struct aggregate with the high and low part: {low, high}
  if (HighPart)
    ResType = getX86_64ByValArgumentPair(ResType, HighPart, getDataLayout());

  return ABIArgInfo::getDirect(ResType);
}

ABIArgInfo
X86_64ABIInfo::classifyRegCallStructTypeImpl(QualType Ty, unsigned &NeededInt,
                                             unsigned &NeededSSE,
                                             unsigned &MaxVectorWidth) const {
  auto RT = Ty->getAs<RecordType>();
  assert(RT && "classifyRegCallStructType only valid with struct types");

  if (RT->getDecl()->hasFlexibleArrayMember())
    return getIndirectReturnResult(Ty);

  // Sum up bases

  // Sum up members
  for (const auto *FD : RT->getDecl()->fields()) {
    QualType MTy = FD->getType();
    if (MTy->isRecordType() && !MTy->isUnionType()) {
      if (classifyRegCallStructTypeImpl(MTy, NeededInt, NeededSSE,
                                        MaxVectorWidth)
              .isIndirect()) {
        NeededInt = NeededSSE = 0;
        return getIndirectReturnResult(Ty);
      }
    } else {
      unsigned LocalNeededInt, LocalNeededSSE;
      if (classifyArgumentType(MTy, UINT_MAX, LocalNeededInt, LocalNeededSSE,
                               true, true)
              .isIndirect()) {
        NeededInt = NeededSSE = 0;
        return getIndirectReturnResult(Ty);
      }
      if (const auto *AT = getContext().getAsConstantArrayType(MTy))
        MTy = AT->getElementType();
      if (const auto *VT = MTy->getAs<VectorType>())
        if (getContext().getTypeSize(VT) > MaxVectorWidth)
          MaxVectorWidth = getContext().getTypeSize(VT);
      NeededInt += LocalNeededInt;
      NeededSSE += LocalNeededSSE;
    }
  }

  return ABIArgInfo::getDirect();
}

ABIArgInfo
X86_64ABIInfo::classifyRegCallStructType(QualType Ty, unsigned &NeededInt,
                                         unsigned &NeededSSE,
                                         unsigned &MaxVectorWidth) const {

  NeededInt = 0;
  NeededSSE = 0;
  MaxVectorWidth = 0;

  return classifyRegCallStructTypeImpl(Ty, NeededInt, NeededSSE,
                                       MaxVectorWidth);
}

void X86_64ABIInfo::computeInfo(ABIFunctionInfo &FI) const {

  const unsigned CallingConv = FI.getCallingConvention();
  // It is possible to force Win64 calling convention on any x86_64 target by
  // using __attribute__((ms_abi)). In such case to correctly emit Win64
  // compatible code delegate this call to WinX86_64ABIInfo::computeInfo.
  if (CallingConv == llvm::CallingConv::Win64) {
    WinX86_64ABIInfo Win64ABIInfo(CGT, AVXLevel);
    Win64ABIInfo.computeInfo(FI);
    return;
  }

  bool IsRegCall = CallingConv == llvm::CallingConv::X86_RegCall;

  // Keep track of the number of assigned registers.
  unsigned FreeIntRegs = IsRegCall ? 11 : 6;
  unsigned FreeSSERegs = IsRegCall ? 16 : 8;
  unsigned NeededInt = 0, NeededSSE = 0, MaxVectorWidth = 0;

  if (!::classifyReturnType(FI, *this)) {
    if (IsRegCall && FI.getReturnType()->getTypePtr()->isRecordType() &&
        !FI.getReturnType()->getTypePtr()->isUnionType()) {
      FI.getReturnInfo() = classifyRegCallStructType(
          FI.getReturnType(), NeededInt, NeededSSE, MaxVectorWidth);
      if (FreeIntRegs >= NeededInt && FreeSSERegs >= NeededSSE) {
        FreeIntRegs -= NeededInt;
        FreeSSERegs -= NeededSSE;
      } else {
        FI.getReturnInfo() = getIndirectReturnResult(FI.getReturnType());
      }
    } else if (IsRegCall && FI.getReturnType()->getAs<ComplexType>() &&
               getContext().getCanonicalType(FI.getReturnType()
                                                 ->getAs<ComplexType>()
                                                 ->getElementType()) ==
                   getContext().LongDoubleTy)
      // Complex Long Double Type is passed in Memory when Regcall
      // calling convention is used.
      FI.getReturnInfo() = getIndirectReturnResult(FI.getReturnType());
    else
      FI.getReturnInfo() = classifyReturnType(FI.getReturnType());
  }

  // If the return value is indirect, then the hidden argument is consuming one
  // integer register.
  if (FI.getReturnInfo().isIndirect())
    --FreeIntRegs;
  else if (NeededSSE && MaxVectorWidth > 0)
    FI.setMaxVectorWidth(MaxVectorWidth);

  // The chain argument effectively gives us another free register.
  if (FI.isChainCall())
    ++FreeIntRegs;

  unsigned NumRequiredArgs = FI.getNumRequiredArgs();
  // AMD64-ABI 3.2.3p3: Once arguments are classified, the registers
  // get assigned (in left-to-right order) for passing as follows...
  unsigned ArgNo = 0;
  for (ABIFunctionInfo::arg_iterator it = FI.arg_begin(), ie = FI.arg_end();
       it != ie; ++it, ++ArgNo) {
    bool IsNamedArg = ArgNo < NumRequiredArgs;

    if (IsRegCall && it->type->isStructureType())
      it->info = classifyRegCallStructType(it->type, NeededInt, NeededSSE,
                                           MaxVectorWidth);
    else
      it->info = classifyArgumentType(it->type, FreeIntRegs, NeededInt,
                                      NeededSSE, IsNamedArg);

    // AMD64-ABI 3.2.3p3: If there are no registers available for any
    // eightbyte of an argument, the whole argument is passed on the
    // stack. If registers have already been assigned for some
    // eightbytes of such an argument, the assignments get reverted.
    if (FreeIntRegs >= NeededInt && FreeSSERegs >= NeededSSE) {
      FreeIntRegs -= NeededInt;
      FreeSSERegs -= NeededSSE;
      if (MaxVectorWidth > FI.getMaxVectorWidth())
        FI.setMaxVectorWidth(MaxVectorWidth);
    } else {
      it->info = getIndirectResult(it->type, FreeIntRegs);
    }
  }
}

namespace {
Address genX86_64VAArgFromMemory(FunctionEmitter &FE, Address VAListAddr,
                                 QualType Ty) {
  Address overflow_arg_area_p =
      FE.Builder.CreateStructGEP(VAListAddr, 2, "overflow_arg_area_p");
  llvm::Value *overflow_arg_area =
      FE.Builder.CreateLoad(overflow_arg_area_p, "overflow_arg_area");

  // AMD64-ABI 3.5.7p5: Step 7. Align l->overflow_arg_area upwards to a 16
  // byte boundary if alignment needed by type exceeds 8 byte boundary.
  // It isn't stated explicitly in the standard, but in practice we use
  // alignment greater than 16 where necessary.
  CharUnits Align = FE.getContext().getTypeAlignInChars(Ty);
  if (Align > CharUnits::fromQuantity(8)) {
    overflow_arg_area =
        emitRoundPointerUpToAlignment(FE, overflow_arg_area, Align);
  }

  // AMD64-ABI 3.5.7p5: Step 8. Fetch type from l->overflow_arg_area.
  llvm::Type *LTy = FE.convertTypeForMem(Ty);
  llvm::Value *Res = overflow_arg_area;

  // AMD64-ABI 3.5.7p5: Step 9. Set l->overflow_arg_area to:
  // l->overflow_arg_area + sizeof(type).
  // AMD64-ABI 3.5.7p5: Step 10. Align l->overflow_arg_area upwards to
  // an 8 byte boundary.

  uint64_t SizeInBytes = (FE.getContext().getTypeSize(Ty) + 7) / 8;
  llvm::Value *Offset =
      llvm::ConstantInt::get(FE.Int32Ty, (SizeInBytes + 7) & ~7);
  overflow_arg_area = FE.Builder.CreateGEP(FE.Int8Ty, overflow_arg_area, Offset,
                                           "overflow_arg_area.next");
  FE.Builder.CreateStore(overflow_arg_area, overflow_arg_area_p);

  // AMD64-ABI 3.5.7p5: Step 11. Return the fetched type.
  return Address(Res, LTy, Align);
}
} // namespace

Address X86_64ABIInfo::genVAArg(FunctionEmitter &FE, Address VAListAddr,
                                QualType Ty) const {
  // Assume that va_list type is correct; should be pointer to LLVM type:
  // struct {
  //   i32 gp_offset;
  //   i32 fp_offset;
  //   i8* overflow_arg_area;
  //   i8* reg_save_area;
  // };
  unsigned neededInt, neededSSE;

  Ty = getContext().getCanonicalType(Ty);
  ABIArgInfo AI = classifyArgumentType(Ty, 0, neededInt, neededSSE,
                                       /*isNamedArg*/ false);

  // AMD64-ABI 3.5.7p5: Step 1. Determine whether type may be passed
  // in the registers. If not go to step 7.
  if (!neededInt && !neededSSE)
    return genX86_64VAArgFromMemory(FE, VAListAddr, Ty);

  // AMD64-ABI 3.5.7p5: Step 2. Compute num_gp to hold the number of
  // general purpose registers needed to pass type and num_fp to hold
  // the number of floating point registers needed.

  // AMD64-ABI 3.5.7p5: Step 3. Verify whether arguments fit into
  // registers. In the case: l->gp_offset > 48 - num_gp * 8 or
  // l->fp_offset > 304 - num_fp * 16 go to step 7.
  //
  // NOTE: 304 is a typo, there are (6 * 8 + 8 * 16) = 176 bytes of
  // register save space).

  llvm::Value *InRegs = nullptr;
  Address gp_offset_p = Address::invalid(), fp_offset_p = Address::invalid();
  llvm::Value *gp_offset = nullptr, *fp_offset = nullptr;
  if (neededInt) {
    gp_offset_p = FE.Builder.CreateStructGEP(VAListAddr, 0, "gp_offset_p");
    gp_offset = FE.Builder.CreateLoad(gp_offset_p, "gp_offset");
    InRegs = llvm::ConstantInt::get(FE.Int32Ty, 48 - neededInt * 8);
    InRegs = FE.Builder.CreateICmpULE(gp_offset, InRegs, "fits_in_gp");
  }

  if (neededSSE) {
    fp_offset_p = FE.Builder.CreateStructGEP(VAListAddr, 1, "fp_offset_p");
    fp_offset = FE.Builder.CreateLoad(fp_offset_p, "fp_offset");
    llvm::Value *FitsInFP =
        llvm::ConstantInt::get(FE.Int32Ty, 176 - neededSSE * 16);
    FitsInFP = FE.Builder.CreateICmpULE(fp_offset, FitsInFP, "fits_in_fp");
    InRegs = InRegs ? FE.Builder.CreateAnd(InRegs, FitsInFP) : FitsInFP;
  }

  llvm::BasicBlock *InRegBlock = FE.createBasicBlock("vaarg.in_reg");
  llvm::BasicBlock *InMemBlock = FE.createBasicBlock("vaarg.in_mem");
  llvm::BasicBlock *ContBlock = FE.createBasicBlock("vaarg.end");
  FE.Builder.CreateCondBr(InRegs, InRegBlock, InMemBlock);

  FE.genBlock(InRegBlock);

  // AMD64-ABI 3.5.7p5: Step 4. Fetch type from l->reg_save_area with
  // an offset of l->gp_offset and/or l->fp_offset. This may require
  // copying to a temporary location in case the parameter is passed
  // in different register classes or requires an alignment greater
  // than 8 for general purpose registers and 16 for XMM registers.
  //
  llvm::Type *LTy = FE.convertTypeForMem(Ty);
  llvm::Value *RegSaveArea = FE.Builder.CreateLoad(
      FE.Builder.CreateStructGEP(VAListAddr, 3), "reg_save_area");

  Address RegAddr = Address::invalid();
  if (neededInt && neededSSE) {
    assert(AI.isDirect() && "Unexpected ABI info for mixed regs");
    llvm::StructType *ST = cast<llvm::StructType>(AI.getCoerceToType());
    Address Tmp = FE.createMemTemp(Ty);
    Tmp = Tmp.withElementType(ST);
    assert(ST->getNumElements() == 2 && "Unexpected ABI info for mixed regs");
    llvm::Type *TyLo = ST->getElementType(0);
    llvm::Type *TyHi = ST->getElementType(1);
    assert((TyLo->isFPOrFPVectorTy() ^ TyHi->isFPOrFPVectorTy()) &&
           "Unexpected ABI info for mixed regs");
    llvm::Value *GPAddr =
        FE.Builder.CreateGEP(FE.Int8Ty, RegSaveArea, gp_offset);
    llvm::Value *FPAddr =
        FE.Builder.CreateGEP(FE.Int8Ty, RegSaveArea, fp_offset);
    llvm::Value *RegLoAddr = TyLo->isFPOrFPVectorTy() ? FPAddr : GPAddr;
    llvm::Value *RegHiAddr = TyLo->isFPOrFPVectorTy() ? GPAddr : FPAddr;

    // Copy the first element.
    llvm::Value *V = FE.Builder.CreateAlignedLoad(
        TyLo, RegLoAddr,
        CharUnits::fromQuantity(getDataLayout().getABITypeAlign(TyLo)));
    FE.Builder.CreateStore(V, FE.Builder.CreateStructGEP(Tmp, 0));

    V = FE.Builder.CreateAlignedLoad(
        TyHi, RegHiAddr,
        CharUnits::fromQuantity(getDataLayout().getABITypeAlign(TyHi)));
    FE.Builder.CreateStore(V, FE.Builder.CreateStructGEP(Tmp, 1));

    RegAddr = Tmp.withElementType(LTy);
  } else if (neededInt) {
    RegAddr = Address(FE.Builder.CreateGEP(FE.Int8Ty, RegSaveArea, gp_offset),
                      LTy, CharUnits::fromQuantity(8));

    // Copy to a temporary if necessary to ensure the appropriate alignment.
    auto TInfo = getContext().getTypeInfoInChars(Ty);
    uint64_t TySize = TInfo.Width.getQuantity();
    CharUnits TyAlign = TInfo.Align;

    // Copy into a temporary if the type is more aligned than the
    // register save area.
    if (TyAlign.getQuantity() > 8) {
      Address Tmp = FE.createMemTemp(Ty);
      FE.Builder.CreateMemCpy(Tmp, RegAddr, TySize, false);
      RegAddr = Tmp;
    }

  } else if (neededSSE == 1) {
    RegAddr = Address(FE.Builder.CreateGEP(FE.Int8Ty, RegSaveArea, fp_offset),
                      LTy, CharUnits::fromQuantity(16));
  } else {
    assert(neededSSE == 2 && "Invalid number of needed registers!");
    // SSE registers are spaced 16 bytes apart in the register save
    // area, we need to collect the two eightbytes together.
    // The ABI isn't explicit about this, but it seems reasonable
    // to assume that the slots are 16-byte aligned, since the stack is
    // naturally 16-byte aligned and the prologue is expected to store
    // all the SSE registers to the RSA.
    Address RegAddrLo =
        Address(FE.Builder.CreateGEP(FE.Int8Ty, RegSaveArea, fp_offset),
                FE.Int8Ty, CharUnits::fromQuantity(16));
    Address RegAddrHi = FE.Builder.CreateConstInBoundsByteGEP(
        RegAddrLo, CharUnits::fromQuantity(16));
    llvm::Type *ST = AI.canHaveCoerceToType()
                         ? AI.getCoerceToType()
                         : llvm::StructType::get(FE.DoubleTy, FE.DoubleTy);
    llvm::Value *V;
    Address Tmp = FE.createMemTemp(Ty);
    Tmp = Tmp.withElementType(ST);
    V = FE.Builder.CreateLoad(
        RegAddrLo.withElementType(ST->getStructElementType(0)));
    FE.Builder.CreateStore(V, FE.Builder.CreateStructGEP(Tmp, 0));
    V = FE.Builder.CreateLoad(
        RegAddrHi.withElementType(ST->getStructElementType(1)));
    FE.Builder.CreateStore(V, FE.Builder.CreateStructGEP(Tmp, 1));

    RegAddr = Tmp.withElementType(LTy);
  }

  // AMD64-ABI 3.5.7p5: Step 5. Set:
  // l->gp_offset = l->gp_offset + num_gp * 8
  // l->fp_offset = l->fp_offset + num_fp * 16.
  if (neededInt) {
    llvm::Value *Offset = llvm::ConstantInt::get(FE.Int32Ty, neededInt * 8);
    FE.Builder.CreateStore(FE.Builder.CreateAdd(gp_offset, Offset),
                           gp_offset_p);
  }
  if (neededSSE) {
    llvm::Value *Offset = llvm::ConstantInt::get(FE.Int32Ty, neededSSE * 16);
    FE.Builder.CreateStore(FE.Builder.CreateAdd(fp_offset, Offset),
                           fp_offset_p);
  }
  FE.genBranch(ContBlock);

  FE.genBlock(InMemBlock);
  Address MemAddr = genX86_64VAArgFromMemory(FE, VAListAddr, Ty);

  FE.genBlock(ContBlock);
  Address ResAddr =
      emitMergePHI(FE, RegAddr, InRegBlock, MemAddr, InMemBlock, "vaarg.addr");
  return ResAddr;
}

Address X86_64ABIInfo::genMSVAArg(FunctionEmitter &FE, Address VAListAddr,
                                  QualType Ty) const {
  // MS x64 ABI requirement: "Any argument that doesn't fit in 8 bytes, or is
  // not 1, 2, 4, or 8 bytes, must be passed by reference."
  uint64_t Width = getContext().getTypeSize(Ty);
  bool IsIndirect = Width > 64 || !llvm::isPowerOf2_64(Width);

  return emitVoidPtrVAArg(FE, VAListAddr, Ty, IsIndirect,
                          FE.getContext().getTypeInfoInChars(Ty),
                          CharUnits::fromQuantity(8),
                          /*allowHigherAlign*/ false);
}

// ===----------------------------------------------------------------------===
// WinX86_64ABIInfo implementation
// ===----------------------------------------------------------------------===

ABIArgInfo WinX86_64ABIInfo::reclassifyHvaArgForVectorCall(
    QualType Ty, unsigned &FreeSSERegs, const ABIArgInfo &current) const {
  const Type *Base = nullptr;
  uint64_t NumElts = 0;

  if (!Ty->isBuiltinType() && !Ty->isVectorType() &&
      isHomogeneousAggregate(Ty, Base, NumElts) && FreeSSERegs >= NumElts) {
    FreeSSERegs -= NumElts;
    return getDirectX86Hva();
  }
  return current;
}

ABIArgInfo WinX86_64ABIInfo::classify(QualType Ty, unsigned &FreeSSERegs,
                                      bool IsReturnType, bool IsVectorCall,
                                      bool IsRegCall) const {

  if (Ty->isVoidType())
    return ABIArgInfo::getIgnore();

  if (const EnumType *EnumTy = Ty->getAs<EnumType>())
    Ty = EnumTy->getDecl()->getIntegerType();

  TypeInfo Info = getContext().getTypeInfo(Ty);
  uint64_t Width = Info.Width;
  CharUnits Align = getContext().toCharUnitsFromBits(Info.Align);

  const RecordType *RT = Ty->getAs<RecordType>();
  if (RT) {
    if (!IsReturnType) {
      if (CGABI::RecordArgABI RAA = getRecordArgABI(RT))
        return getNaturalAlignIndirect(Ty, RAA == CGABI::RAA_DirectInMemory);
    }

    if (RT->getDecl()->hasFlexibleArrayMember())
      return getNaturalAlignIndirect(Ty, /*ByVal=*/false);
  }

  const Type *Base = nullptr;
  uint64_t NumElts = 0;
  // vectorcall adds the concept of a homogenous vector aggregate, similar to
  // other targets.
  if ((IsVectorCall || IsRegCall) &&
      isHomogeneousAggregate(Ty, Base, NumElts)) {
    if (IsRegCall) {
      if (FreeSSERegs >= NumElts) {
        FreeSSERegs -= NumElts;
        if (IsReturnType || Ty->isBuiltinType() || Ty->isVectorType())
          return ABIArgInfo::getDirect();
        return ABIArgInfo::getExpand();
      }
      return ABIArgInfo::getIndirect(Align, /*ByVal=*/false);
    } else if (IsVectorCall) {
      if (FreeSSERegs >= NumElts &&
          (IsReturnType || Ty->isBuiltinType() || Ty->isVectorType())) {
        FreeSSERegs -= NumElts;
        return ABIArgInfo::getDirect();
      } else if (IsReturnType) {
        return ABIArgInfo::getExpand();
      } else if (!Ty->isBuiltinType() && !Ty->isVectorType()) {
        // HVAs are delayed and reclassified in the 2nd step.
        return ABIArgInfo::getIndirect(Align, /*ByVal=*/false);
      }
    }
  }

  if (RT || Ty->isAnyComplexType()) {
    // MS x64 ABI requirement: "Any argument that doesn't fit in 8 bytes, or is
    // not 1, 2, 4, or 8 bytes, must be passed by reference."
    if (Width > 64 || !llvm::isPowerOf2_64(Width))
      return getNaturalAlignIndirect(Ty, /*ByVal=*/false);

    // Otherwise, coerce it to a small integer.
    return ABIArgInfo::getDirect(llvm::IntegerType::get(getVMContext(), Width));
  }

  if (const BuiltinType *BT = Ty->getAs<BuiltinType>()) {
    switch (BT->getKind()) {
    case BuiltinType::Bool:
      // Bool type is always extended to the ABI, other builtin types are not
      // extended.
      return ABIArgInfo::getExtend(Ty);

    case BuiltinType::LongDouble:
      break;

    case BuiltinType::Int128:
    case BuiltinType::UInt128:
      // If it's a parameter type, the normal ABI rule is that arguments larger
      // than 8 bytes are passed indirectly. GCC follows it. We follow it too,
      // even though it isn't particularly efficient.
      if (!IsReturnType)
        return ABIArgInfo::getIndirect(Align, /*ByVal=*/false);

      // i128 is returned in XMM0, coerced to v2i64.
      return ABIArgInfo::getDirect(llvm::FixedVectorType::get(
          llvm::Type::getInt64Ty(getVMContext()), 2));

    default:
      break;
    }
  }

  if (Ty->isBitIntType()) {
    // MS x64 ABI requirement: "Any argument that doesn't fit in 8 bytes, or is
    // not 1, 2, 4, or 8 bytes, must be passed by reference."
    // However, non-power-of-two bit-precise integers will be passed as 1, 2, 4,
    // or 8 bytes anyway as long is it fits in them, so we don't have to check
    // the power of 2.
    if (Width <= 64)
      return ABIArgInfo::getDirect();
    return ABIArgInfo::getIndirect(Align, /*ByVal=*/false);
  }

  return ABIArgInfo::getDirect();
}

void WinX86_64ABIInfo::computeInfo(ABIFunctionInfo &FI) const {
  const unsigned CC = FI.getCallingConvention();
  bool IsVectorCall = CC == llvm::CallingConv::X86_VectorCall;
  bool IsRegCall = CC == llvm::CallingConv::X86_RegCall;

  // If __attribute__((sysv_abi)) is in use, use the SysV argument
  // classification rules.
  if (CC == llvm::CallingConv::X86_64_SysV) {
    X86_64ABIInfo SysVABIInfo(CGT, AVXLevel);
    SysVABIInfo.computeInfo(FI);
    return;
  }

  unsigned FreeSSERegs = 0;
  if (IsVectorCall) {
    // We can use up to 4 SSE return registers with vectorcall.
    FreeSSERegs = 4;
  } else if (IsRegCall) {
    // RegCall gives us 16 SSE registers.
    FreeSSERegs = 16;
  }

  FI.getReturnInfo() =
      classify(FI.getReturnType(), FreeSSERegs, true, IsVectorCall, IsRegCall);

  if (IsVectorCall) {
    // We can use up to 6 SSE register parameters with vectorcall.
    FreeSSERegs = 6;
  } else if (IsRegCall) {
    // RegCall gives us 16 SSE registers, we can reuse the return registers.
    FreeSSERegs = 16;
  }

  unsigned ArgNum = 0;
  unsigned ZeroSSERegs = 0;
  for (auto &I : FI.arguments()) {
    // Vectorcall in x64 only permits the first 6 arguments to be passed as
    // XMM/YMM registers. After the sixth argument, pretend no vector
    // registers are left.
    unsigned *MaybeFreeSSERegs =
        (IsVectorCall && ArgNum >= 6) ? &ZeroSSERegs : &FreeSSERegs;
    I.info =
        classify(I.type, *MaybeFreeSSERegs, false, IsVectorCall, IsRegCall);
    ++ArgNum;
  }

  if (IsVectorCall) {
    // For vectorcall, assign aggregate HVAs to any free vector registers in a
    // second pass.
    for (auto &I : FI.arguments())
      I.info = reclassifyHvaArgForVectorCall(I.type, FreeSSERegs, I.info);
  }
}

Address WinX86_64ABIInfo::genVAArg(FunctionEmitter &FE, Address VAListAddr,
                                   QualType Ty) const {
  // MS x64 ABI requirement: "Any argument that doesn't fit in 8 bytes, or is
  // not 1, 2, 4, or 8 bytes, must be passed by reference."
  uint64_t Width = getContext().getTypeSize(Ty);
  bool IsIndirect = Width > 64 || !llvm::isPowerOf2_64(Width);

  return emitVoidPtrVAArg(FE, VAListAddr, Ty, IsIndirect,
                          FE.getContext().getTypeInfoInChars(Ty),
                          CharUnits::fromQuantity(8),
                          /*allowHigherAlign*/ false);
}

std::unique_ptr<TargetCodeGenInfo>
Emit::createX86_64TargetCodeGenInfo(ModuleEmitter &ME,
                                    X86AVXABILevel AVXLevel) {
  return std::make_unique<X86_64TargetCodeGenInfo>(ME.getTypes(), AVXLevel);
}

std::unique_ptr<TargetCodeGenInfo>
Emit::createWinX86_64TargetCodeGenInfo(ModuleEmitter &ME,
                                       X86AVXABILevel AVXLevel) {
  return std::make_unique<WinX86_64TargetCodeGenInfo>(ME.getTypes(), AVXLevel);
}
