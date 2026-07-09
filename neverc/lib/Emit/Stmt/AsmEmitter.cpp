#include "ABI/TargetInfo.h"
#include "Core/FunctionEmitter.h"
#include "Core/ModuleEmitter.h"
#include "Debug/DebugEmitterInfo.h"
#include "neverc/Foundation/Builtin/Builtins.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Diagnostic/DiagnosticSema.h"
#include "neverc/Tree/Core/Attr.h"
#include "neverc/Tree/Stmt/StmtVisitor.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Config/config.h"
#include "llvm/IR/Assumptions.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/Support/SaveAndRestore.h"

using namespace neverc;
using namespace Emit;

// ===----------------------------------------------------------------------===
// Inline assembly
// ===----------------------------------------------------------------------===

namespace {

std::string simplifyConstraint(
    const char *Constraint, const TargetInfo &Target,
    llvm::SmallVectorImpl<TargetInfo::ConstraintInfo> *OutCons = nullptr) {
  std::string Result;

  while (*Constraint) {
    switch (*Constraint) {
    default:
      Result += Target.convertConstraint(Constraint);
      break;
    // Ignore these
    case '*':
    case '?':
    case '!':
    case '=': // Will see this and the following in mult-alt constraints.
    case '+':
      break;
    case '#': // Ignore the rest of the constraint alternative.
      while (Constraint[1] && Constraint[1] != ',')
        Constraint++;
      break;
    case '&':
    case '%':
      Result += *Constraint;
      while (Constraint[1] && Constraint[1] == *Constraint)
        Constraint++;
      break;
    case ',':
      Result += "|";
      break;
    case 'g':
      Result += "imr";
      break;
    case '[': {
      assert(OutCons &&
             "Must pass output names to constraints with a symbolic name");
      unsigned Index;
      bool result = Target.resolveSymbolicName(Constraint, *OutCons, Index);
      assert(result && "Could not resolve symbolic name");
      (void)result;
      Result += llvm::utostr(Index);
      break;
    }
    }

    Constraint++;
  }

  return Result;
}

std::string addVariableConstraints(const std::string &Constraint,
                                   const Expr &AsmExpr,
                                   const TargetInfo &Target, ModuleEmitter &ME,
                                   const AsmStmt &Stmt, const bool EarlyClobber,
                                   std::string *GCCReg = nullptr) {
  const DeclRefExpr *AsmDeclRef = dyn_cast<DeclRefExpr>(&AsmExpr);
  if (!AsmDeclRef)
    return Constraint;
  const ValueDecl &Value = *AsmDeclRef->getDecl();
  const VarDecl *Variable = dyn_cast<VarDecl>(&Value);
  if (!Variable)
    return Constraint;
  if (Variable->getStorageClass() != SC_Register)
    return Constraint;
  AsmLabelAttr *Attr = Variable->getAttr<AsmLabelAttr>();
  if (!Attr)
    return Constraint;
  llvm::StringRef Register = Attr->getLabel();
  assert(Target.isValidGCCRegisterName(Register));
  // We're using validateOutputConstraint here because we only care if
  // this is a register constraint.
  TargetInfo::ConstraintInfo Info(Constraint, "");
  if (Target.validateOutputConstraint(Info) && !Info.allowsRegister()) {
    ME.errorUnsupported(&Stmt, "__asm__");
    return Constraint;
  }
  // Canonicalize the register here before returning it.
  Register = Target.getNormalizedGCCRegisterName(Register);
  if (GCCReg != nullptr)
    *GCCReg = Register.str();
  return (EarlyClobber ? "&{" : "{") + Register.str() + "}";
}

} // namespace

std::pair<llvm::Value *, llvm::Type *> FunctionEmitter::genAsmInputLValue(
    const TargetInfo::ConstraintInfo &Info, LValue InputValue,
    QualType InputType, std::string &ConstraintStr, SourceLocation Loc) {
  if (Info.allowsRegister() || !Info.allowsMemory()) {
    if (FunctionEmitter::hasScalarEvaluationKind(InputType))
      return {genLoadOfLValue(InputValue, Loc).getScalarVal(), nullptr};

    llvm::Type *Ty = convertType(InputType);
    uint64_t Size = ME.getDataLayout().getTypeSizeInBits(Ty);
    if ((Size <= 64 && llvm::isPowerOf2_64(Size)) ||
        getTargetHooks().isScalarizableAsmOperand(*this, Ty)) {
      Ty = llvm::IntegerType::get(getLLVMContext(), Size);

      return {
          Builder.CreateLoad(InputValue.getAddress(*this).withElementType(Ty)),
          nullptr};
    }
  }

  Address Addr = InputValue.getAddress(*this);
  ConstraintStr += '*';
  return {Addr.getPointer(), Addr.getElementType()};
}

std::pair<llvm::Value *, llvm::Type *>
FunctionEmitter::genAsmInput(const TargetInfo::ConstraintInfo &Info,
                             const Expr *InputExpr,
                             std::string &ConstraintStr) {
  // If this can't be a register or memory, i.e., has to be a constant
  // (immediate or symbolic), try to emit it as such.
  if (!Info.allowsRegister() && !Info.allowsMemory()) {
    if (Info.requiresImmediateConstant()) {
      Expr::EvalResult EVResult;
      InputExpr->EvaluateAsRValue(EVResult, getContext(), true);

      llvm::APSInt IntResult;
      if (EVResult.Val.toIntegralConstant(IntResult, InputExpr->getType(),
                                          getContext()))
        return {llvm::ConstantInt::get(getLLVMContext(), IntResult), nullptr};
    }

    Expr::EvalResult Result;
    if (InputExpr->EvaluateAsInt(Result, getContext()))
      return {llvm::ConstantInt::get(getLLVMContext(), Result.Val.getInt()),
              nullptr};
  }

  if (Info.allowsRegister() || !Info.allowsMemory())
    if (FunctionEmitter::hasScalarEvaluationKind(InputExpr->getType()))
      return {genScalarExpr(InputExpr), nullptr};
  InputExpr = InputExpr->IgnoreParenNoopCasts(getContext());
  LValue Dest = genLValue(InputExpr);
  return genAsmInputLValue(Info, Dest, InputExpr->getType(), ConstraintStr,
                           InputExpr->getExprLoc());
}

namespace {

llvm::MDNode *getAsmSrcLocInfo(const StringLiteral *Str, FunctionEmitter &FE) {
  llvm::SmallVector<llvm::Metadata *, 8> Locs;
  Locs.push_back(llvm::ConstantAsMetadata::get(
      llvm::ConstantInt::get(FE.Int64Ty, Str->getBeginLoc().getRawEncoding())));
  llvm::StringRef StrVal = Str->getString();
  if (!StrVal.empty()) {
    const SourceManager &SM = FE.ME.getContext().getSourceManager();
    const LangOptions &LangOpts = FE.ME.getLangOpts();
    unsigned StartToken = 0;
    unsigned ByteOffset = 0;

    for (unsigned i = 0, e = StrVal.size() - 1; i != e; ++i) {
      if (StrVal[i] != '\n')
        continue;
      SourceLocation LineLoc = Str->getLocationOfByte(
          i + 1, SM, LangOpts, FE.getTarget(), &StartToken, &ByteOffset);
      Locs.push_back(llvm::ConstantAsMetadata::get(
          llvm::ConstantInt::get(FE.Int64Ty, LineLoc.getRawEncoding())));
    }
  }

  return llvm::MDNode::get(FE.getLLVMContext(), Locs);
}

void updateAsmCallInst(llvm::CallBase &Result, bool HasSideEffect,
                       bool HasUnwindClobber, bool ReadOnly, bool ReadNone,
                       bool NoMerge, const AsmStmt &S,
                       const std::vector<llvm::Type *> &ResultRegTypes,
                       const std::vector<llvm::Type *> &ArgElemTypes,
                       FunctionEmitter &FE,
                       std::vector<llvm::Value *> &RegResults) {
  if (!HasUnwindClobber)
    Result.addFnAttr(llvm::Attribute::NoUnwind);

  if (NoMerge)
    Result.addFnAttr(llvm::Attribute::NoMerge);
  // Attach readnone and readonly attributes.
  if (!HasSideEffect) {
    if (ReadNone)
      Result.setDoesNotAccessMemory();
    else if (ReadOnly)
      Result.setOnlyReadsMemory();
  }

  for (auto Pair : llvm::enumerate(ArgElemTypes)) {
    if (Pair.value()) {
      auto Attr = llvm::Attribute::get(
          FE.getLLVMContext(), llvm::Attribute::ElementType, Pair.value());
      Result.addParamAttr(Pair.index(), Attr);
    }
  }

  // Slap the source location of the inline asm into a !srcloc metadata on the
  // call.
  if (const auto *gccAsmStmt = dyn_cast<GCCAsmStmt>(&S))
    Result.setMetadata("srcloc",
                       getAsmSrcLocInfo(gccAsmStmt->getAsmString(), FE));
  else {
    // At least put the line number on MS inline asm blobs.
    llvm::Constant *Loc =
        llvm::ConstantInt::get(FE.Int64Ty, S.getAsmLoc().getRawEncoding());
    Result.setMetadata("srcloc",
                       llvm::MDNode::get(FE.getLLVMContext(),
                                         llvm::ConstantAsMetadata::get(Loc)));
  }

  // Extract all of the register value results from the asm.
  if (ResultRegTypes.size() == 1) {
    RegResults.push_back(&Result);
  } else {
    for (unsigned i = 0, e = ResultRegTypes.size(); i != e; ++i) {
      llvm::Value *Tmp = FE.Builder.CreateExtractValue(&Result, i, "asmresult");
      RegResults.push_back(Tmp);
    }
  }
}

void genAsmStores(FunctionEmitter &FE, const AsmStmt &S,
                  const llvm::ArrayRef<llvm::Value *> RegResults,
                  const llvm::ArrayRef<llvm::Type *> ResultRegTypes,
                  const llvm::ArrayRef<llvm::Type *> ResultTruncRegTypes,
                  const llvm::ArrayRef<LValue> ResultRegDests,
                  const llvm::ArrayRef<QualType> ResultRegQualTys,
                  const llvm::BitVector &ResultTypeRequiresCast,
                  const llvm::BitVector &ResultRegIsFlagReg) {
  CGBuilderTy &Builder = FE.Builder;
  ModuleEmitter &ME = FE.ME;
  llvm::LLVMContext &CTX = FE.getLLVMContext();

  assert(RegResults.size() == ResultRegTypes.size());
  assert(RegResults.size() == ResultTruncRegTypes.size());
  assert(RegResults.size() == ResultRegDests.size());
  // ResultRegDests can be also populated by addReturnRegisterOutputs() above,
  // in which case its size may grow.
  assert(ResultTypeRequiresCast.size() <= ResultRegDests.size());
  assert(ResultRegIsFlagReg.size() <= ResultRegDests.size());

  for (unsigned i = 0, e = RegResults.size(); i != e; ++i) {
    llvm::Value *Tmp = RegResults[i];
    llvm::Type *TruncTy = ResultTruncRegTypes[i];

    if ((i < ResultRegIsFlagReg.size()) && ResultRegIsFlagReg[i]) {
      // Target must guarantee the Value `Tmp` here is lowered to a boolean
      // value.
      llvm::Constant *Two = llvm::ConstantInt::get(Tmp->getType(), 2);
      llvm::Value *IsBooleanValue =
          Builder.CreateCmp(llvm::CmpInst::ICMP_ULT, Tmp, Two);
      llvm::Function *FnAssume = ME.getIntrinsic(llvm::Intrinsic::assume);
      Builder.CreateCall(FnAssume, IsBooleanValue);
    }

    // If the result type of the LLVM IR asm doesn't match the result type of
    // the expression, do the conversion.
    if (ResultRegTypes[i] != TruncTy) {

      // Truncate the integer result to the right size, note that TruncTy can be
      // a pointer.
      if (TruncTy->isFloatingPointTy())
        Tmp = Builder.CreateFPTrunc(Tmp, TruncTy);
      else if (TruncTy->isPointerTy() && Tmp->getType()->isIntegerTy()) {
        uint64_t ResSize = ME.getDataLayout().getTypeSizeInBits(TruncTy);
        Tmp = Builder.CreateTrunc(
            Tmp, llvm::IntegerType::get(CTX, (unsigned)ResSize));
        Tmp = Builder.CreateIntToPtr(Tmp, TruncTy);
      } else if (Tmp->getType()->isPointerTy() && TruncTy->isIntegerTy()) {
        uint64_t TmpSize = ME.getDataLayout().getTypeSizeInBits(Tmp->getType());
        Tmp = Builder.CreatePtrToInt(
            Tmp, llvm::IntegerType::get(CTX, (unsigned)TmpSize));
        Tmp = Builder.CreateTrunc(Tmp, TruncTy);
      } else if (TruncTy->isIntegerTy()) {
        Tmp = Builder.CreateZExtOrTrunc(Tmp, TruncTy);
      } else if (TruncTy->isVectorTy()) {
        Tmp = Builder.CreateBitCast(Tmp, TruncTy);
      }
    }

    LValue Dest = ResultRegDests[i];
    // ResultTypeRequiresCast elements correspond to the first
    // ResultTypeRequiresCast.size() elements of RegResults.
    if ((i < ResultTypeRequiresCast.size()) && ResultTypeRequiresCast[i]) {
      unsigned Size = FE.getContext().getTypeSize(ResultRegQualTys[i]);
      Address A = Dest.getAddress(FE).withElementType(ResultRegTypes[i]);
      if (FE.getTargetHooks().isScalarizableAsmOperand(FE, TruncTy)) {
        Builder.CreateStore(Tmp, A);
        continue;
      }

      QualType Ty =
          FE.getContext().getIntTypeForBitwidth(Size, /*Signed=*/false);
      if (Ty.isNull()) {
        const Expr *OutExpr = S.getOutputExpr(i);
        ME.getDiags().Report(OutExpr->getExprLoc(),
                             diag::err_store_value_to_reg);
        return;
      }
      Dest = FE.makeAddrLValue(A, Ty);
    }
    FE.genStoreThroughLValue(RValue::get(Tmp), Dest);
  }
}

} // namespace

void FunctionEmitter::genAsmStmt(const AsmStmt &S) {
  // Pop all cleanup blocks at the end of the asm statement.
  FunctionEmitter::RunCleanupsScope Cleanups(*this);

  // Assemble the final asm string.
  std::string AsmString = S.generateAsmString(getContext());

  llvm::SmallVector<TargetInfo::ConstraintInfo, 4> OutputConstraintInfos;
  llvm::SmallVector<TargetInfo::ConstraintInfo, 4> InputConstraintInfos;

  for (unsigned i = 0, e = S.getNumOutputs(); i != e; i++) {
    llvm::StringRef Name;
    if (const GCCAsmStmt *GAS = dyn_cast<GCCAsmStmt>(&S))
      Name = GAS->getOutputName(i);
    TargetInfo::ConstraintInfo Info(S.getOutputConstraint(i), Name);
    bool IsValid = getTarget().validateOutputConstraint(Info);
    (void)IsValid;
    assert(IsValid && "Failed to parse output constraint");
    OutputConstraintInfos.push_back(Info);
  }

  for (unsigned i = 0, e = S.getNumInputs(); i != e; i++) {
    llvm::StringRef Name;
    if (const GCCAsmStmt *GAS = dyn_cast<GCCAsmStmt>(&S))
      Name = GAS->getInputName(i);
    TargetInfo::ConstraintInfo Info(S.getInputConstraint(i), Name);
    assert(getTarget().validateInputConstraint(OutputConstraintInfos, Info) &&
           "Failed to parse input constraint");
    InputConstraintInfos.push_back(Info);
  }

  std::string Constraints;

  std::vector<LValue> ResultRegDests;
  std::vector<QualType> ResultRegQualTys;
  std::vector<llvm::Type *> ResultRegTypes;
  std::vector<llvm::Type *> ResultTruncRegTypes;
  std::vector<llvm::Type *> ArgTypes;
  std::vector<llvm::Type *> ArgElemTypes;
  std::vector<llvm::Value *> Args;
  llvm::BitVector ResultTypeRequiresCast;
  llvm::BitVector ResultRegIsFlagReg;

  // Keep track of inout constraints.
  std::string InOutConstraints;
  std::vector<llvm::Value *> InOutArgs;
  std::vector<llvm::Type *> InOutArgTypes;
  std::vector<llvm::Type *> InOutArgElemTypes;

  // Keep track of out constraints for tied input operand.
  std::vector<std::string> OutputConstraints;

  // Keep track of defined physregs.
  llvm::SmallSet<std::string, 8> PhysRegOutputs;

  // An inline asm can be marked readonly if it meets the following conditions:
  //  - it doesn't have any sideeffects
  //  - it doesn't clobber memory
  //  - it doesn't return a value by-reference
  // It can be marked readnone if it doesn't have any input memory constraints
  // in addition to meeting the conditions listed above.
  bool ReadOnly = true, ReadNone = true;

  for (unsigned i = 0, e = S.getNumOutputs(); i != e; i++) {
    TargetInfo::ConstraintInfo &Info = OutputConstraintInfos[i];

    // Simplify the output constraint.
    std::string OutputConstraint(S.getOutputConstraint(i));
    OutputConstraint = simplifyConstraint(OutputConstraint.c_str() + 1,
                                          getTarget(), &OutputConstraintInfos);

    const Expr *OutExpr = S.getOutputExpr(i);
    OutExpr = OutExpr->IgnoreParenNoopCasts(getContext());

    std::string GCCReg;
    OutputConstraint =
        addVariableConstraints(OutputConstraint, *OutExpr, getTarget(), ME, S,
                               Info.earlyClobber(), &GCCReg);
    // Give an error on multiple outputs to same physreg.
    if (!GCCReg.empty() && !PhysRegOutputs.insert(GCCReg).second)
      ME.error(S.getAsmLoc(), "multiple outputs to hard register: " + GCCReg);

    OutputConstraints.push_back(OutputConstraint);
    LValue Dest = genLValue(OutExpr);
    if (!Constraints.empty())
      Constraints += ',';

    // If this is a register output, then make the inline asm return it
    // by-value.  If this is a memory result, return the value by-reference.
    QualType QTy = OutExpr->getType();
    const bool IsScalarOrAggregate =
        hasScalarEvaluationKind(QTy) || hasAggregateEvaluationKind(QTy);
    if (!Info.allowsMemory() && IsScalarOrAggregate) {

      Constraints += "=" + OutputConstraint;
      ResultRegQualTys.push_back(QTy);
      ResultRegDests.push_back(Dest);

      bool IsFlagReg = llvm::StringRef(OutputConstraint).starts_with("{@cc");
      ResultRegIsFlagReg.push_back(IsFlagReg);

      llvm::Type *Ty = convertTypeForMem(QTy);
      const bool RequiresCast =
          Info.allowsRegister() &&
          (getTargetHooks().isScalarizableAsmOperand(*this, Ty) ||
           Ty->isAggregateType());

      ResultTruncRegTypes.push_back(Ty);
      ResultTypeRequiresCast.push_back(RequiresCast);

      if (RequiresCast) {
        unsigned Size = getContext().getTypeSize(QTy);
        Ty = llvm::IntegerType::get(getLLVMContext(), Size);
      }
      ResultRegTypes.push_back(Ty);
      // If this output is tied to an input, and if the input is larger, then
      // we need to set the actual result type of the inline asm node to be the
      // same as the input type.
      if (Info.hasMatchingInput()) {
        unsigned InputNo;
        for (InputNo = 0; InputNo != S.getNumInputs(); ++InputNo) {
          TargetInfo::ConstraintInfo &Input = InputConstraintInfos[InputNo];
          if (Input.hasTiedOperand() && Input.getTiedOperand() == i)
            break;
        }
        assert(InputNo != S.getNumInputs() && "Didn't find matching input!");

        QualType InputTy = S.getInputExpr(InputNo)->getType();
        QualType OutputType = OutExpr->getType();

        uint64_t InputSize = getContext().getTypeSize(InputTy);
        if (getContext().getTypeSize(OutputType) < InputSize) {
          // Form the asm to return the value as a larger integer or fp type.
          ResultRegTypes.back() = convertType(InputTy);
        }
      }
      if (llvm::Type *AdjTy = getTargetHooks().adjustInlineAsmType(
              *this, OutputConstraint, ResultRegTypes.back()))
        ResultRegTypes.back() = AdjTy;
      else {
        ME.getDiags().Report(S.getAsmLoc(), diag::err_asm_invalid_type_in_input)
            << OutExpr->getType() << OutputConstraint;
      }

      if (auto *VT = dyn_cast<llvm::VectorType>(ResultRegTypes.back()))
        LargestVectorWidth =
            std::max((uint64_t)LargestVectorWidth,
                     VT->getPrimitiveSizeInBits().getKnownMinValue());
    } else {
      Address DestAddr = Dest.getAddress(*this);
      // Matrix types in memory are represented by arrays, but accessed through
      // vector pointers, with the alignment specified on the access operation.
      // For inline assembly, update pointer arguments to use vector pointers.
      // Otherwise there will be a mis-match if the matrix is also an
      // input-argument which is represented as vector.
      if (isa<MatrixType>(OutExpr->getType().getCanonicalType()))
        DestAddr = DestAddr.withElementType(convertType(OutExpr->getType()));

      ArgTypes.push_back(DestAddr.getType());
      ArgElemTypes.push_back(DestAddr.getElementType());
      Args.push_back(DestAddr.getPointer());
      Constraints += "=*";
      Constraints += OutputConstraint;
      ReadOnly = ReadNone = false;
    }

    if (Info.isReadWrite()) {
      InOutConstraints += ',';

      const Expr *InputExpr = S.getOutputExpr(i);
      llvm::Value *Arg;
      llvm::Type *ArgElemType;
      std::tie(Arg, ArgElemType) =
          genAsmInputLValue(Info, Dest, InputExpr->getType(), InOutConstraints,
                            InputExpr->getExprLoc());

      if (llvm::Type *AdjTy = getTargetHooks().adjustInlineAsmType(
              *this, OutputConstraint, Arg->getType()))
        Arg = Builder.CreateBitCast(Arg, AdjTy);

      if (auto *VT = dyn_cast<llvm::VectorType>(Arg->getType()))
        LargestVectorWidth =
            std::max((uint64_t)LargestVectorWidth,
                     VT->getPrimitiveSizeInBits().getKnownMinValue());
      // Only tie earlyclobber physregs.
      if (Info.allowsRegister() && (GCCReg.empty() || Info.earlyClobber()))
        InOutConstraints += llvm::utostr(i);
      else
        InOutConstraints += OutputConstraint;

      InOutArgTypes.push_back(Arg->getType());
      InOutArgElemTypes.push_back(ArgElemType);
      InOutArgs.push_back(Arg);
    }
  }

  // If this is a Microsoft-style asm blob, store the return registers (RAX:RDX)
  // to the return value slot. Only do this when returning in registers.
  if (isa<MSAsmStmt>(&S)) {
    const ABIArgInfo &RetAI = CurFnInfo->getReturnInfo();
    if (RetAI.isDirect() || RetAI.isExtend()) {
      // Make a fake lvalue for the return value slot.
      LValue ReturnSlot = makeAddrLValueWithoutTBAA(ReturnValue, FnRetTy);
      ME.getTargetCodeGenInfo().addReturnRegisterOutputs(
          *this, ReturnSlot, Constraints, ResultRegTypes, ResultTruncRegTypes,
          ResultRegDests, AsmString, S.getNumOutputs());
      SawAsmBlock = true;
    }
  }

  for (unsigned i = 0, e = S.getNumInputs(); i != e; i++) {
    const Expr *InputExpr = S.getInputExpr(i);

    TargetInfo::ConstraintInfo &Info = InputConstraintInfos[i];

    if (Info.allowsMemory())
      ReadNone = false;

    if (!Constraints.empty())
      Constraints += ',';

    // Simplify the input constraint.
    std::string InputConstraint(S.getInputConstraint(i));
    InputConstraint = simplifyConstraint(InputConstraint.c_str(), getTarget(),
                                         &OutputConstraintInfos);

    InputConstraint = addVariableConstraints(
        InputConstraint, *InputExpr->IgnoreParenNoopCasts(getContext()),
        getTarget(), ME, S, false /* No EarlyClobber */);

    std::string ReplaceConstraint(InputConstraint);
    llvm::Value *Arg;
    llvm::Type *ArgElemType;
    std::tie(Arg, ArgElemType) = genAsmInput(Info, InputExpr, Constraints);

    // If this input argument is tied to a larger output result, extend the
    // input to be the same size as the output.  The LLVM backend wants to see
    // the input and output of a matching constraint be the same size.  Note
    // that GCC does not define what the top bits are here.  We use zext because
    // that is usually cheaper, but LLVM IR should really get an anyext someday.
    if (Info.hasTiedOperand()) {
      unsigned Output = Info.getTiedOperand();
      QualType OutputType = S.getOutputExpr(Output)->getType();
      QualType InputTy = InputExpr->getType();

      if (getContext().getTypeSize(OutputType) >
          getContext().getTypeSize(InputTy)) {
        // Use ptrtoint as appropriate so that we can do our extension.
        if (isa<llvm::PointerType>(Arg->getType()))
          Arg = Builder.CreatePtrToInt(Arg, IntPtrTy);
        llvm::Type *OutputTy = convertType(OutputType);
        if (isa<llvm::IntegerType>(OutputTy))
          Arg = Builder.CreateZExt(Arg, OutputTy);
        else if (isa<llvm::PointerType>(OutputTy))
          Arg = Builder.CreateZExt(Arg, IntPtrTy);
        else if (OutputTy->isFloatingPointTy())
          Arg = Builder.CreateFPExt(Arg, OutputTy);
      }
      // Deal with the tied operands' constraint code in adjustInlineAsmType.
      ReplaceConstraint = OutputConstraints[Output];
    }
    if (llvm::Type *AdjTy = getTargetHooks().adjustInlineAsmType(
            *this, ReplaceConstraint, Arg->getType()))
      Arg = Builder.CreateBitCast(Arg, AdjTy);
    else
      ME.getDiags().Report(S.getAsmLoc(), diag::err_asm_invalid_type_in_input)
          << InputExpr->getType() << InputConstraint;

    if (auto *VT = dyn_cast<llvm::VectorType>(Arg->getType()))
      LargestVectorWidth =
          std::max((uint64_t)LargestVectorWidth,
                   VT->getPrimitiveSizeInBits().getKnownMinValue());

    ArgTypes.push_back(Arg->getType());
    ArgElemTypes.push_back(ArgElemType);
    Args.push_back(Arg);
    Constraints += InputConstraint;
  }

  // Append the "input" part of inout constraints.
  for (unsigned i = 0, e = InOutArgs.size(); i != e; i++) {
    ArgTypes.push_back(InOutArgTypes[i]);
    ArgElemTypes.push_back(InOutArgElemTypes[i]);
    Args.push_back(InOutArgs[i]);
  }
  Constraints += InOutConstraints;

  // Labels
  llvm::SmallVector<llvm::BasicBlock *, 16> Transfer;
  llvm::BasicBlock *Fallthrough = nullptr;
  bool IsGCCAsmGoto = false;
  if (const auto *GS = dyn_cast<GCCAsmStmt>(&S)) {
    IsGCCAsmGoto = GS->isAsmGoto();
    if (IsGCCAsmGoto) {
      for (const auto *E : GS->labels()) {
        JumpDest Dest = getJumpDestForLabel(E->getLabel());
        Transfer.push_back(Dest.getBlock());
        if (!Constraints.empty())
          Constraints += ',';
        Constraints += "!i";
      }
      Fallthrough = createBasicBlock("asm.fallthrough");
    }
  }

  bool HasUnwindClobber = false;

  // Clobbers
  for (unsigned i = 0, e = S.getNumClobbers(); i != e; i++) {
    llvm::StringRef Clobber = S.getClobber(i);

    if (Clobber == "memory")
      ReadOnly = ReadNone = false;
    else if (Clobber == "unwind") {
      HasUnwindClobber = true;
      continue;
    } else if (Clobber != "cc") {
      Clobber = getTarget().getNormalizedGCCRegisterName(Clobber);
      if (ME.getCodeGenOpts().StackClashProtector &&
          getTarget().isSPRegName(Clobber)) {
        ME.getDiags().Report(S.getAsmLoc(),
                             diag::warn_stack_clash_protection_inline_asm);
      }
    }

    if (isa<MSAsmStmt>(&S)) {
      if (Clobber == "eax" || Clobber == "edx") {
        if (Constraints.find("=&A") != std::string::npos)
          continue;
        std::string::size_type position1 =
            Constraints.find("={" + Clobber.str() + "}");
        if (position1 != std::string::npos) {
          Constraints.insert(position1 + 1, "&");
          continue;
        }
        std::string::size_type position2 = Constraints.find("=A");
        if (position2 != std::string::npos) {
          Constraints.insert(position2 + 1, "&");
          continue;
        }
      }
    }
    if (!Constraints.empty())
      Constraints += ',';

    Constraints += "~{";
    Constraints += Clobber;
    Constraints += '}';
  }

  assert(!(HasUnwindClobber && IsGCCAsmGoto) &&
         "unwind clobber can't be used with asm goto");

  std::string_view MachineClobbers = getTarget().getClobbers();
  if (!MachineClobbers.empty()) {
    if (!Constraints.empty())
      Constraints += ',';
    Constraints += MachineClobbers;
  }

  llvm::Type *ResultType;
  if (ResultRegTypes.empty())
    ResultType = VoidTy;
  else if (ResultRegTypes.size() == 1)
    ResultType = ResultRegTypes[0];
  else
    ResultType = llvm::StructType::get(getLLVMContext(), ResultRegTypes);

  llvm::FunctionType *FTy =
      llvm::FunctionType::get(ResultType, ArgTypes, false);

  bool HasSideEffect = S.isVolatile() || S.getNumOutputs() == 0;

  llvm::InlineAsm::AsmDialect GnuAsmDialect =
      ME.getCodeGenOpts().getInlineAsmDialect() == CodeGenOptions::IAD_ATT
          ? llvm::InlineAsm::AD_ATT
          : llvm::InlineAsm::AD_Intel;
  llvm::InlineAsm::AsmDialect AsmDialect =
      isa<MSAsmStmt>(&S) ? llvm::InlineAsm::AD_Intel : GnuAsmDialect;

  llvm::InlineAsm *IA = llvm::InlineAsm::get(
      FTy, AsmString, Constraints, HasSideEffect,
      /* IsAlignStack */ false, AsmDialect, HasUnwindClobber);
  std::vector<llvm::Value *> RegResults;
  llvm::CallBrInst *CBR;
  llvm::DenseMap<llvm::BasicBlock *, llvm::SmallVector<llvm::Value *, 4>>
      CBRRegResults;
  if (IsGCCAsmGoto) {
    CBR = Builder.CreateCallBr(IA, Fallthrough, Transfer, Args);
    genBlock(Fallthrough);
    updateAsmCallInst(*CBR, HasSideEffect, false, ReadOnly, ReadNone,
                      InNoMergeAttributedStmt, S, ResultRegTypes, ArgElemTypes,
                      *this, RegResults);
    // Because we are emitting code top to bottom, we don't have enough
    // information at this point to know precisely whether we have a critical
    // edge. If we have outputs, split all indirect destinations.
    if (!RegResults.empty()) {
      unsigned i = 0;
      for (llvm::BasicBlock *Dest : CBR->getIndirectDests()) {
        llvm::Twine SynthName = Dest->getName() + ".split";
        llvm::BasicBlock *SynthBB = createBasicBlock(SynthName);
        llvm::IRBuilderBase::InsertPointGuard IPG(Builder);
        Builder.SetInsertPoint(SynthBB);

        if (ResultRegTypes.size() == 1) {
          CBRRegResults[SynthBB].push_back(CBR);
        } else {
          for (unsigned j = 0, e = ResultRegTypes.size(); j != e; ++j) {
            llvm::Value *Tmp = Builder.CreateExtractValue(CBR, j, "asmresult");
            CBRRegResults[SynthBB].push_back(Tmp);
          }
        }

        genBranch(Dest);
        genBlock(SynthBB);
        CBR->setIndirectDest(i++, SynthBB);
      }
    }
  } else if (HasUnwindClobber) {
    llvm::CallBase *Result = genCallOrInvoke(IA, Args, "");
    updateAsmCallInst(*Result, HasSideEffect, true, ReadOnly, ReadNone,
                      InNoMergeAttributedStmt, S, ResultRegTypes, ArgElemTypes,
                      *this, RegResults);
  } else {
    llvm::CallInst *Result =
        Builder.CreateCall(IA, Args, getBundlesForFunclet(IA));
    updateAsmCallInst(*Result, HasSideEffect, false, ReadOnly, ReadNone,
                      InNoMergeAttributedStmt, S, ResultRegTypes, ArgElemTypes,
                      *this, RegResults);
  }

  genAsmStores(*this, S, RegResults, ResultRegTypes, ResultTruncRegTypes,
               ResultRegDests, ResultRegQualTys, ResultTypeRequiresCast,
               ResultRegIsFlagReg);

  // If this is an asm goto with outputs, repeat genAsmStores, but with a
  // different insertion point; one for each indirect destination and with
  // CBRRegResults rather than RegResults.
  if (IsGCCAsmGoto && !CBRRegResults.empty()) {
    for (llvm::BasicBlock *Succ : CBR->getIndirectDests()) {
      llvm::IRBuilderBase::InsertPointGuard IPG(Builder);
      Builder.SetInsertPoint(Succ, --(Succ->end()));
      genAsmStores(*this, S, CBRRegResults[Succ], ResultRegTypes,
                   ResultTruncRegTypes, ResultRegDests, ResultRegQualTys,
                   ResultTypeRequiresCast, ResultRegIsFlagReg);
    }
  }
}
