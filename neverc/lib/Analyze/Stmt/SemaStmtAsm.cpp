#include "neverc/Analyze/ScopeInfo.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Tree/Type/StructLayout.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include <optional>
using namespace neverc;
using namespace sema;

// ===----------------------------------------------------------------------===
// GCC asm helpers
// ===----------------------------------------------------------------------===

namespace {
void removeLValueToRValueCast(Expr *E) {
  Expr *Parent = E;
  Expr *ExprUnderCast = nullptr;
  llvm::SmallVector<Expr *, 8> ParentsToUpdate;

  while (true) {
    ParentsToUpdate.push_back(Parent);
    if (auto *ParenE = dyn_cast<ParenExpr>(Parent)) {
      Parent = ParenE->getSubExpr();
      continue;
    }

    Expr *Child = nullptr;
    CastExpr *ParentCast = dyn_cast<CastExpr>(Parent);
    if (ParentCast)
      Child = ParentCast->getSubExpr();
    else
      return;

    if (auto *CastE = dyn_cast<CastExpr>(Child))
      if (CastE->getCastKind() == CK_LValueToRValue) {
        ExprUnderCast = CastE->getSubExpr();
        // LValueToRValue cast inside GCCAsmStmt requires an explicit cast.
        ParentCast->setSubExpr(ExprUnderCast);
        break;
      }
    Parent = Child;
  }

  assert(ExprUnderCast &&
         "Should be reachable only if LValueToRValue cast was found!");
  auto ValueKind = ExprUnderCast->getValueKind();
  for (Expr *E : ParentsToUpdate)
    E->setValueKind(ValueKind);
}

void emitAndFixInvalidAsmCastLValue(const Expr *LVal, Expr *BadArgument,
                                    Sema &S) {
  S.Diag(LVal->getBeginLoc(), diag::err_invalid_asm_cast_lvalue)
      << BadArgument->getSourceRange();
  removeLValueToRValueCast(BadArgument);
}

bool checkAsmLValue(Expr *E, Sema &S) {
  if (E->isLValue())
    return false; // Cool, this is an lvalue.

  // Okay, this is not an lvalue, but perhaps it is the result of a cast that we
  // are supposed to allow.
  const Expr *E2 = E->IgnoreParenNoopCasts(S.Context);
  if (E != E2 && E2->isLValue()) {
    emitAndFixInvalidAsmCastLValue(E2, E, S);
    // Accept, even if we emitted an error diagnostic.
    return false;
  }

  // None of the above, just randomly invalid non-lvalue.
  return true;
}

bool isOperandMentioned(
    unsigned OpNo, llvm::ArrayRef<GCCAsmStmt::AsmStringPiece> AsmStrPieces) {
  for (unsigned p = 0, e = AsmStrPieces.size(); p != e; ++p) {
    const GCCAsmStmt::AsmStringPiece &Piece = AsmStrPieces[p];
    if (!Piece.isOperand())
      continue;

    // If this is a reference to the input and if the input was the smaller
    // one, then we have to reject this asm.
    if (Piece.getOperandNo() == OpNo)
      return true;
  }
  return false;
}

bool checkNakedParmReference(Expr *E, Sema &S) {
  FunctionDecl *Func = dyn_cast<FunctionDecl>(S.CurContext);
  if (!Func)
    return false;
  if (!Func->hasAttr<NakedAttr>())
    return false;

  llvm::SmallVector<Expr *, 4> WorkList;
  WorkList.push_back(E);
  while (WorkList.size()) {
    Expr *E = WorkList.pop_back_val();
    if (DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E)) {
      if (isa<ParmVarDecl>(DRE->getDecl())) {
        S.Diag(DRE->getBeginLoc(), diag::err_asm_naked_parm_ref);
        S.Diag(Func->getAttr<NakedAttr>()->getLocation(), diag::note_attribute);
        return true;
      }
    }
    for (Stmt *Child : E->children()) {
      if (Expr *E = dyn_cast_or_null<Expr>(Child))
        WorkList.push_back(E);
    }
  }
  return false;
}

bool checkExprMemoryConstraintCompat(Sema &S, Expr *E,
                                     TargetInfo::ConstraintInfo &Info,
                                     bool is_input_expr) {
  enum {
    ExprBitfield = 0,
    ExprVectorElt,
    ExprGlobalRegVar,
    ExprSafeType
  } EType = ExprSafeType;

  if (E->refersToBitField())
    EType = ExprBitfield;
  else if (E->refersToVectorElement())
    EType = ExprVectorElt;
  else if (E->refersToGlobalRegisterVar())
    EType = ExprGlobalRegVar;

  if (EType != ExprSafeType) {
    S.Diag(E->getBeginLoc(), diag::err_asm_non_addr_value_in_memory_constraint)
        << EType << is_input_expr << Info.getConstraintStr()
        << E->getSourceRange();
    return true;
  }

  return false;
}

llvm::StringRef extractRegisterName(const Expr *Expression,
                                    const TargetInfo &Target) {
  Expression = Expression->IgnoreImpCasts();
  if (const DeclRefExpr *AsmDeclRef = dyn_cast<DeclRefExpr>(Expression)) {
    const VarDecl *Variable = dyn_cast<VarDecl>(AsmDeclRef->getDecl());
    if (Variable && Variable->getStorageClass() == SC_Register) {
      if (AsmLabelAttr *Attr = Variable->getAttr<AsmLabelAttr>())
        if (Target.isValidGCCRegisterName(Attr->getLabel()))
          return Target.getNormalizedGCCRegisterName(Attr->getLabel(), true);
    }
  }
  return "";
}

SourceLocation getClobberConflictLocation(MultiExprArg Exprs,
                                          StringLiteral **Constraints,
                                          StringLiteral **Clobbers,
                                          int NumClobbers, unsigned NumLabels,
                                          const TargetInfo &Target,
                                          TreeContext &Cont) {
  llvm::StringSet<> InOutVars;
  for (unsigned int i = 0; i < Exprs.size() - NumLabels; ++i) {
    llvm::StringRef Constraint = Constraints[i]->getString();
    llvm::StringRef InOutReg = Target.getConstraintRegister(
        Constraint, extractRegisterName(Exprs[i], Target));
    if (InOutReg != "")
      InOutVars.insert(InOutReg);
  }
  for (int i = 0; i < NumClobbers; ++i) {
    llvm::StringRef Clobber = Clobbers[i]->getString();
    // We only check registers, therefore we don't check cc and memory
    // clobbers
    if (Clobber == "cc" || Clobber == "memory" || Clobber == "unwind")
      continue;
    Clobber = Target.getNormalizedGCCRegisterName(Clobber, true);
    if (InOutVars.contains(Clobber))
      return Clobbers[i]->getBeginLoc();
  }
  return SourceLocation();
}
} // namespace

// ===----------------------------------------------------------------------===
// GCC asm statement
// ===----------------------------------------------------------------------===

StmtResult Sema::OnGCCAsmStmt(SourceLocation AsmLoc, bool IsSimple,
                              bool IsVolatile, unsigned NumOutputs,
                              unsigned NumInputs, IdentifierInfo **Names,
                              MultiExprArg constraints, MultiExprArg Exprs,
                              Expr *asmString, MultiExprArg clobbers,
                              unsigned NumLabels, SourceLocation RParenLoc) {
  unsigned NumClobbers = clobbers.size();
  StringLiteral **Constraints =
      reinterpret_cast<StringLiteral **>(constraints.data());
  StringLiteral *AsmString = cast<StringLiteral>(asmString);
  StringLiteral **Clobbers =
      reinterpret_cast<StringLiteral **>(clobbers.data());

  llvm::SmallVector<TargetInfo::ConstraintInfo, 4> OutputConstraintInfos;

  assert(AsmString->isOrdinary());

  FunctionDecl *FD = dyn_cast<FunctionDecl>(getCurLexicalContext());
  llvm::StringMap<bool> FeatureMap;
  Context.getFunctionFeatureMap(FeatureMap, FD);

  for (unsigned i = 0; i != NumOutputs; i++) {
    StringLiteral *Literal = Constraints[i];
    assert(Literal->isOrdinary());

    llvm::StringRef OutputName;
    if (Names[i])
      OutputName = Names[i]->getName();

    TargetInfo::ConstraintInfo Info(Literal->getString(), OutputName);
    if (!Context.getTargetInfo().validateOutputConstraint(Info)) {
      targetDiag(Literal->getBeginLoc(),
                 diag::err_asm_invalid_output_constraint)
          << Info.getConstraintStr();
      return new (Context)
          GCCAsmStmt(Context, AsmLoc, IsSimple, IsVolatile, NumOutputs,
                     NumInputs, Names, Constraints, Exprs.data(), AsmString,
                     NumClobbers, Clobbers, NumLabels, RParenLoc);
    }

    ExprResult ER = CheckPlaceholderExpr(Exprs[i]);
    if (ER.isInvalid())
      return StmtError();
    Exprs[i] = ER.get();

    Expr *OutputExpr = Exprs[i];

    if (checkNakedParmReference(OutputExpr, *this))
      return StmtError();

    if (Info.allowsMemory() &&
        checkExprMemoryConstraintCompat(*this, OutputExpr, Info, false))
      return StmtError();

    // Disallow bit-precise integer types, since the backends tend to have
    // difficulties with abnormal sizes.
    if (OutputExpr->getType()->isBitIntType())
      return StmtError(
          Diag(OutputExpr->getBeginLoc(), diag::err_asm_invalid_type)
          << OutputExpr->getType() << 0 /*Input*/
          << OutputExpr->getSourceRange());

    OutputConstraintInfos.push_back(Info);

    Expr::isModifiableLvalueResult IsLV =
        OutputExpr->isModifiableLvalue(Context, /*Loc=*/nullptr);
    switch (IsLV) {
    case Expr::MLV_Valid:
    case Expr::MLV_ArrayType:
      break;
    case Expr::MLV_LValueCast: {
      const Expr *LVal = OutputExpr->IgnoreParenNoopCasts(Context);
      emitAndFixInvalidAsmCastLValue(LVal, OutputExpr, *this);
      // Accept, even if we emitted an error diagnostic.
      break;
    }
    case Expr::MLV_IncompleteType:
    case Expr::MLV_IncompleteVoidType:
      if (RequireCompleteType(OutputExpr->getBeginLoc(), Exprs[i]->getType(),
                              diag::err_dereference_incomplete_type))
        return StmtError();
      [[fallthrough]];
    default:
      return StmtError(Diag(OutputExpr->getBeginLoc(),
                            diag::err_asm_invalid_lvalue_in_output)
                       << OutputExpr->getSourceRange());
    }

    unsigned Size = Context.getTypeSize(OutputExpr->getType());
    if (!Context.getTargetInfo().validateOutputSize(
            FeatureMap, Literal->getString(), Size)) {
      targetDiag(OutputExpr->getBeginLoc(), diag::err_asm_invalid_output_size)
          << Info.getConstraintStr();
      return new (Context)
          GCCAsmStmt(Context, AsmLoc, IsSimple, IsVolatile, NumOutputs,
                     NumInputs, Names, Constraints, Exprs.data(), AsmString,
                     NumClobbers, Clobbers, NumLabels, RParenLoc);
    }
  }

  llvm::SmallVector<TargetInfo::ConstraintInfo, 4> InputConstraintInfos;

  for (unsigned i = NumOutputs, e = NumOutputs + NumInputs; i != e; i++) {
    StringLiteral *Literal = Constraints[i];
    assert(Literal->isOrdinary());

    llvm::StringRef InputName;
    if (Names[i])
      InputName = Names[i]->getName();

    TargetInfo::ConstraintInfo Info(Literal->getString(), InputName);
    if (!Context.getTargetInfo().validateInputConstraint(OutputConstraintInfos,
                                                         Info)) {
      targetDiag(Literal->getBeginLoc(), diag::err_asm_invalid_input_constraint)
          << Info.getConstraintStr();
      return new (Context)
          GCCAsmStmt(Context, AsmLoc, IsSimple, IsVolatile, NumOutputs,
                     NumInputs, Names, Constraints, Exprs.data(), AsmString,
                     NumClobbers, Clobbers, NumLabels, RParenLoc);
    }

    ExprResult ER = CheckPlaceholderExpr(Exprs[i]);
    if (ER.isInvalid())
      return StmtError();
    Exprs[i] = ER.get();

    Expr *InputExpr = Exprs[i];

    if (checkNakedParmReference(InputExpr, *this))
      return StmtError();

    if (Info.allowsMemory() &&
        checkExprMemoryConstraintCompat(*this, InputExpr, Info, true))
      return StmtError();

    // Only allow void types for memory constraints.
    if (Info.allowsMemory() && !Info.allowsRegister()) {
      if (checkAsmLValue(InputExpr, *this))
        return StmtError(Diag(InputExpr->getBeginLoc(),
                              diag::err_asm_invalid_lvalue_in_input)
                         << Info.getConstraintStr()
                         << InputExpr->getSourceRange());
    } else {
      ExprResult Result = DefaultFunctionArrayLvalueConversion(Exprs[i]);
      if (Result.isInvalid())
        return StmtError();

      InputExpr = Exprs[i] = Result.get();

      if (Info.requiresImmediateConstant() && !Info.allowsRegister()) {
        {
          Expr::EvalResult EVResult;
          if (InputExpr->EvaluateAsRValue(EVResult, Context, true)) {
            // For compatibility with GCC, we also allow pointers that would be
            // integral constant expressions if they were cast to int.
            llvm::APSInt IntResult;
            if (EVResult.Val.toIntegralConstant(IntResult, InputExpr->getType(),
                                                Context))
              if (!Info.isValidAsmImmediate(IntResult))
                return StmtError(
                    Diag(InputExpr->getBeginLoc(),
                         diag::err_invalid_asm_value_for_constraint)
                    << toString(IntResult, 10) << Info.getConstraintStr()
                    << InputExpr->getSourceRange());
          }
        }
      }
    }

    if (Info.allowsRegister()) {
      if (InputExpr->getType()->isVoidType()) {
        return StmtError(
            Diag(InputExpr->getBeginLoc(), diag::err_asm_invalid_type_in_input)
            << InputExpr->getType() << Info.getConstraintStr()
            << InputExpr->getSourceRange());
      }
    }

    if (InputExpr->getType()->isBitIntType())
      return StmtError(
          Diag(InputExpr->getBeginLoc(), diag::err_asm_invalid_type)
          << InputExpr->getType() << 1 /*Output*/
          << InputExpr->getSourceRange());

    InputConstraintInfos.push_back(Info);

    const Type *Ty = Exprs[i]->getType().getTypePtr();
    if (!Ty->isVoidType() || !Info.allowsMemory())
      if (RequireCompleteType(InputExpr->getBeginLoc(), Exprs[i]->getType(),
                              diag::err_dereference_incomplete_type))
        return StmtError();

    unsigned Size = Context.getTypeSize(Ty);
    if (!Context.getTargetInfo().validateInputSize(FeatureMap,
                                                   Literal->getString(), Size))
      return targetDiag(InputExpr->getBeginLoc(),
                        diag::err_asm_invalid_input_size)
             << Info.getConstraintStr();
  }

  std::optional<SourceLocation> UnwindClobberLoc;

  for (unsigned i = 0; i != NumClobbers; i++) {
    StringLiteral *Literal = Clobbers[i];
    assert(Literal->isOrdinary());

    llvm::StringRef Clobber = Literal->getString();

    if (!Context.getTargetInfo().isValidClobber(Clobber)) {
      targetDiag(Literal->getBeginLoc(), diag::err_asm_unknown_register_name)
          << Clobber;
      return new (Context)
          GCCAsmStmt(Context, AsmLoc, IsSimple, IsVolatile, NumOutputs,
                     NumInputs, Names, Constraints, Exprs.data(), AsmString,
                     NumClobbers, Clobbers, NumLabels, RParenLoc);
    }

    if (Clobber == "unwind") {
      UnwindClobberLoc = Literal->getBeginLoc();
    }
  }

  // Using unwind clobber and asm-goto together is not supported right now.
  if (UnwindClobberLoc && NumLabels > 0) {
    targetDiag(*UnwindClobberLoc, diag::err_asm_unwind_and_goto);
    return new (Context)
        GCCAsmStmt(Context, AsmLoc, IsSimple, IsVolatile, NumOutputs, NumInputs,
                   Names, Constraints, Exprs.data(), AsmString, NumClobbers,
                   Clobbers, NumLabels, RParenLoc);
  }

  GCCAsmStmt *NS = new (Context)
      GCCAsmStmt(Context, AsmLoc, IsSimple, IsVolatile, NumOutputs, NumInputs,
                 Names, Constraints, Exprs.data(), AsmString, NumClobbers,
                 Clobbers, NumLabels, RParenLoc);

  llvm::SmallVector<GCCAsmStmt::AsmStringPiece, 8> Pieces;
  unsigned DiagOffs;
  if (unsigned DiagID = NS->AnalyzeAsmString(Pieces, Context, DiagOffs)) {
    targetDiag(getLocationOfStringLiteralByte(AsmString, DiagOffs), DiagID)
        << AsmString->getSourceRange();
    return NS;
  }

  for (unsigned i = 0, e = Pieces.size(); i != e; ++i) {
    GCCAsmStmt::AsmStringPiece &Piece = Pieces[i];
    if (!Piece.isOperand())
      continue;

    unsigned ConstraintIdx = Piece.getOperandNo();
    unsigned NumOperands = NS->getNumOutputs() + NS->getNumInputs();
    // Labels are the last in the Exprs list.
    if (NS->isAsmGoto() && ConstraintIdx >= NumOperands)
      continue;
    // Look for the (ConstraintIdx - NumOperands + 1)th constraint with
    // modifier '+'.
    if (ConstraintIdx >= NumOperands) {
      unsigned I = 0, E = NS->getNumOutputs();

      for (unsigned Cnt = ConstraintIdx - NumOperands; I != E; ++I)
        if (OutputConstraintInfos[I].isReadWrite() && Cnt-- == 0) {
          ConstraintIdx = I;
          break;
        }

      assert(I != E && "Invalid operand number should have been caught in "
                       " AnalyzeAsmString");
    }

    StringLiteral *Literal = Constraints[ConstraintIdx];
    const Type *Ty = Exprs[ConstraintIdx]->getType().getTypePtr();
    if (Ty->isIncompleteType())
      continue;

    unsigned Size = Context.getTypeSize(Ty);
    std::string SuggestedModifier;
    if (!Context.getTargetInfo().validateConstraintModifier(
            Literal->getString(), Piece.getModifier(), Size,
            SuggestedModifier)) {
      targetDiag(Exprs[ConstraintIdx]->getBeginLoc(),
                 diag::warn_asm_mismatched_size_modifier);

      if (!SuggestedModifier.empty()) {
        auto B = targetDiag(Piece.getRange().getBegin(),
                            diag::note_asm_missing_constraint_modifier)
                 << SuggestedModifier;
        SuggestedModifier = "%" + SuggestedModifier + Piece.getString();
        B << FixItHint::CreateReplacement(Piece.getRange(), SuggestedModifier);
      }
    }
  }

  unsigned NumAlternatives = ~0U;
  for (unsigned i = 0, e = OutputConstraintInfos.size(); i != e; ++i) {
    TargetInfo::ConstraintInfo &Info = OutputConstraintInfos[i];
    llvm::StringRef ConstraintStr = Info.getConstraintStr();
    unsigned AltCount = ConstraintStr.count(',') + 1;
    if (NumAlternatives == ~0U) {
      NumAlternatives = AltCount;
    } else if (NumAlternatives != AltCount) {
      targetDiag(NS->getOutputExpr(i)->getBeginLoc(),
                 diag::err_asm_unexpected_constraint_alternatives)
          << NumAlternatives << AltCount;
      return NS;
    }
  }
  llvm::SmallVector<size_t, 4> InputMatchedToOutput(
      OutputConstraintInfos.size(), ~0U);
  for (unsigned i = 0, e = InputConstraintInfos.size(); i != e; ++i) {
    TargetInfo::ConstraintInfo &Info = InputConstraintInfos[i];
    llvm::StringRef ConstraintStr = Info.getConstraintStr();
    unsigned AltCount = ConstraintStr.count(',') + 1;
    if (NumAlternatives == ~0U) {
      NumAlternatives = AltCount;
    } else if (NumAlternatives != AltCount) {
      targetDiag(NS->getInputExpr(i)->getBeginLoc(),
                 diag::err_asm_unexpected_constraint_alternatives)
          << NumAlternatives << AltCount;
      return NS;
    }

    // If this is a tied constraint, verify that the output and input have
    // either exactly the same type, or that they are int/ptr operands with the
    // same size (int/long, int*/long, are ok etc).
    if (!Info.hasTiedOperand())
      continue;

    unsigned TiedTo = Info.getTiedOperand();
    unsigned InputOpNo = i + NumOutputs;
    Expr *OutputExpr = Exprs[TiedTo];
    Expr *InputExpr = Exprs[InputOpNo];

    // Make sure no more than one input constraint matches each output.
    assert(TiedTo < InputMatchedToOutput.size() && "TiedTo value out of range");
    if (InputMatchedToOutput[TiedTo] != ~0U) {
      targetDiag(NS->getInputExpr(i)->getBeginLoc(),
                 diag::err_asm_input_duplicate_match)
          << TiedTo;
      targetDiag(NS->getInputExpr(InputMatchedToOutput[TiedTo])->getBeginLoc(),
                 diag::note_asm_input_duplicate_first)
          << TiedTo;
      return NS;
    }
    InputMatchedToOutput[TiedTo] = i;

    QualType InTy = InputExpr->getType();
    QualType OutTy = OutputExpr->getType();
    if (Context.hasSameType(InTy, OutTy))
      continue; // All types can be tied to themselves.

    // Decide if the input and output are in the same domain (integer/ptr or
    // floating point.
    enum AsmDomain { AD_Int, AD_FP, AD_Other } InputDomain, OutputDomain;

    if (InTy->isIntegerType() || InTy->isPointerType())
      InputDomain = AD_Int;
    else if (InTy->isRealFloatingType())
      InputDomain = AD_FP;
    else
      InputDomain = AD_Other;

    if (OutTy->isIntegerType() || OutTy->isPointerType())
      OutputDomain = AD_Int;
    else if (OutTy->isRealFloatingType())
      OutputDomain = AD_FP;
    else
      OutputDomain = AD_Other;

    // They are ok if they are the same size and in the same domain.  This
    // allows tying things like:
    //   void* to int*
    //   void* to int            if they are the same size.
    //   double to long double   if they are the same size.
    //
    uint64_t OutSize = Context.getTypeSize(OutTy);
    uint64_t InSize = Context.getTypeSize(InTy);
    if (OutSize == InSize && InputDomain == OutputDomain &&
        InputDomain != AD_Other)
      continue;

    // If the smaller input/output operand is not mentioned in the asm string,
    // then we can promote the smaller one to a larger input and the asm string
    // won't notice.
    bool SmallerValueMentioned = false;

    // If this is a reference to the input and if the input was the smaller
    // one, then we have to reject this asm.
    if (isOperandMentioned(InputOpNo, Pieces)) {
      // This is a use in the asm string of the smaller operand.  Since we
      // codegen this by promoting to a wider value, the asm will get printed
      // "wrong".
      SmallerValueMentioned |= InSize < OutSize;
    }
    if (isOperandMentioned(TiedTo, Pieces)) {
      // If this is a reference to the output, and if the output is the larger
      // value, then it's ok because we'll promote the input to the larger type.
      SmallerValueMentioned |= OutSize < InSize;
    }

    // If the smaller value wasn't mentioned in the asm string, and if the
    // output was a register, just extend the shorter one to the size of the
    // larger one.
    if (!SmallerValueMentioned && InputDomain != AD_Other &&
        OutputConstraintInfos[TiedTo].allowsRegister()) {
      // GCC supports the OutSize to be 128 at maximum. Currently codegen
      // crashes when the size is larger than the register size, so limit it.
      if (OutTy->isStructureType() &&
          Context.getIntTypeForBitwidth(OutSize, /*Signed*/ false).isNull()) {
        targetDiag(OutputExpr->getExprLoc(), diag::err_store_value_to_reg);
        return NS;
      }

      continue;
    }

    // Either both of the operands were mentioned or the smaller one was
    // mentioned.  One more special case that we'll allow: if the tied input is
    // integer, unmentioned, and is a constant, then we'll allow truncating it
    // down to the size of the destination.
    if (InputDomain == AD_Int && OutputDomain == AD_Int &&
        !isOperandMentioned(InputOpNo, Pieces) &&
        InputExpr->isEvaluatable(Context)) {
      CastKind castKind =
          (OutTy->isBooleanType() ? CK_IntegralToBoolean : CK_IntegralCast);
      InputExpr = ImpCastExprToType(InputExpr, OutTy, castKind).get();
      Exprs[InputOpNo] = InputExpr;
      NS->setInputExpr(i, InputExpr);
      continue;
    }

    targetDiag(InputExpr->getBeginLoc(), diag::err_asm_tying_incompatible_types)
        << InTy << OutTy << OutputExpr->getSourceRange()
        << InputExpr->getSourceRange();
    return NS;
  }

  SourceLocation ConstraintLoc =
      getClobberConflictLocation(Exprs, Constraints, Clobbers, NumClobbers,
                                 NumLabels, Context.getTargetInfo(), Context);
  if (ConstraintLoc.isValid())
    targetDiag(ConstraintLoc, diag::error_inoutput_conflict_with_clobber);

  typedef std::pair<llvm::StringRef, Expr *> NamedOperand;
  llvm::SmallVector<NamedOperand, 4> NamedOperandList;
  for (unsigned i = 0, e = NumOutputs + NumInputs + NumLabels; i != e; ++i)
    if (Names[i])
      NamedOperandList.emplace_back(
          std::make_pair(Names[i]->getName(), Exprs[i]));
  llvm::stable_sort(NamedOperandList, llvm::less_first());
  llvm::SmallVector<NamedOperand, 4>::iterator Found =
      std::adjacent_find(begin(NamedOperandList), end(NamedOperandList),
                         [](const NamedOperand &LHS, const NamedOperand &RHS) {
                           return LHS.first == RHS.first;
                         });
  if (Found != NamedOperandList.end()) {
    Diag((Found + 1)->second->getBeginLoc(),
         diag::error_duplicate_asm_operand_name)
        << (Found + 1)->first;
    Diag(Found->second->getBeginLoc(), diag::note_duplicate_asm_operand_name)
        << Found->first;
    return StmtError();
  }
  if (NS->isAsmGoto())
    setFunctionHasBranchIntoScope();

  DiscardCleanupsInEvaluationContext();
  return NS;
}

// ===----------------------------------------------------------------------===
// Inline asm identifier & field lookup
// ===----------------------------------------------------------------------===

void Sema::FillInlineAsmIdentifierInfo(Expr *Res,
                                       llvm::InlineAsmIdentifierInfo &Info) {
  QualType T = Res->getType();
  Expr::EvalResult Eval;
  if (T->isFunctionType())
    return Info.setLabel(Res);
  if (Res->isPRValue()) {
    bool IsEnum = isa<neverc::EnumType>(T);
    if (DeclRefExpr *DRE = dyn_cast<neverc::DeclRefExpr>(Res))
      if (DRE->getDecl()->getKind() == Decl::EnumConstant)
        IsEnum = true;
    if (IsEnum && Res->EvaluateAsRValue(Eval, Context))
      return Info.setEnum(Eval.Val.getInt().getSExtValue());

    return Info.setLabel(Res);
  }
  unsigned Size = Context.getTypeSizeInChars(T).getQuantity();
  unsigned Type = Size;
  if (const auto *ATy = Context.getAsArrayType(T))
    Type = Context.getTypeSizeInChars(ATy->getElementType()).getQuantity();
  bool IsGlobalLV = false;
  if (Res->EvaluateAsLValue(Eval, Context))
    IsGlobalLV = Eval.isGlobalLValue();
  Info.setVar(Res, IsGlobalLV, Size, Type);
}

ExprResult Sema::LookupInlineAsmIdentifier(UnqualifiedId &Id,
                                           bool IsUnevaluatedContext) {
  if (IsUnevaluatedContext)
    PushExpressionEvaluationContext(
        ExpressionEvaluationContext::UnevaluatedAbstract);

  ExprResult Result = OnIdExpression(getCurScope(), Id,
                                     /*trailing lparen*/ false,
                                     /*is & operand*/ false,
                                     /*CorrectionCandidateCallback=*/nullptr,
                                     /*IsInlineAsmIdentifier=*/true);

  if (IsUnevaluatedContext)
    PopExpressionEvaluationContext();

  if (!Result.isUsable())
    return Result;

  Result = CheckPlaceholderExpr(Result.get());
  if (!Result.isUsable())
    return Result;

  if (checkNakedParmReference(Result.get(), *this))
    return ExprError();

  QualType T = Result.get()->getType();

  if (T->isFunctionType())
    return Result;

  if (RequireCompleteExprType(Result.get(), diag::err_asm_incomplete_type)) {
    return ExprError();
  }

  return Result;
}

bool Sema::LookupInlineAsmField(llvm::StringRef Base, llvm::StringRef Member,
                                unsigned &Offset, SourceLocation AsmLoc) {
  Offset = 0;
  llvm::SmallVector<llvm::StringRef, 2> Members;
  Member.split(Members, ".");

  NamedDecl *FoundDecl = nullptr;

  // MS InlineAsm uses 'this' as a base
  LookupResult BaseResult(*this, &Context.Idents.get(Base), SourceLocation(),
                          ResolveOrdinary);
  if (ResolveName(BaseResult, getCurScope()) && BaseResult.isSingleResult())
    FoundDecl = BaseResult.getFoundDecl();

  if (!FoundDecl)
    return true;

  for (llvm::StringRef NextMember : Members) {
    const RecordType *RT = nullptr;
    if (VarDecl *VD = dyn_cast<VarDecl>(FoundDecl))
      RT = VD->getType()->getAs<RecordType>();
    else if (TypedefNameDecl *TD = dyn_cast<TypedefNameDecl>(FoundDecl)) {
      MarkAnyDeclReferenced(TD->getLocation(), TD, /*OdrUse=*/false);
      // MS InlineAsm often uses struct pointer aliases as a base
      QualType QT = TD->getUnderlyingType();
      if (const auto *PT = QT->getAs<PointerType>())
        QT = PT->getPointeeType();
      RT = QT->getAs<RecordType>();
    } else if (TypeDecl *TD = dyn_cast<TypeDecl>(FoundDecl))
      RT = TD->getTypeForDecl()->getAs<RecordType>();
    else if (FieldDecl *TD = dyn_cast<FieldDecl>(FoundDecl))
      RT = TD->getType()->getAs<RecordType>();
    if (!RT)
      return true;

    if (RequireCompleteType(AsmLoc, QualType(RT, 0),
                            diag::err_asm_incomplete_type))
      return true;

    LookupResult FieldResult(*this, &Context.Idents.get(NextMember),
                             SourceLocation(), ResolveMember);

    if (!LookupQualifiedName(FieldResult, RT->getDecl()))
      return true;

    if (!FieldResult.isSingleResult())
      return true;
    FoundDecl = FieldResult.getFoundDecl();

    FieldDecl *FD = dyn_cast<FieldDecl>(FoundDecl);
    if (!FD)
      return true;

    const StructRecordLayout &RL = Context.getStructRecordLayout(RT->getDecl());
    unsigned i = FD->getFieldIndex();
    CharUnits Result = Context.toCharUnitsFromBits(RL.getFieldOffset(i));
    Offset += (unsigned)Result.getQuantity();
  }

  return false;
}

ExprResult Sema::LookupInlineAsmVarDeclField(Expr *E, llvm::StringRef Member,
                                             SourceLocation AsmLoc) {

  QualType T = E->getType();
  const RecordType *RT = T->getAs<RecordType>();
  if (!RT)
    return ExprResult();

  LookupResult FieldResult(*this, &Context.Idents.get(Member), AsmLoc,
                           ResolveMember);

  if (!LookupQualifiedName(FieldResult, RT->getDecl()))
    return ExprResult();

  // Only normal and indirect field results will work.
  ValueDecl *FD = dyn_cast<FieldDecl>(FieldResult.getFoundDecl());
  if (!FD)
    FD = dyn_cast<IndirectFieldDecl>(FieldResult.getFoundDecl());
  if (!FD)
    return ExprResult();

  ExprResult Result = FormMemberReferenceExpr(E, E->getType(), AsmLoc,
                                              /*IsArrow=*/false, FieldResult);

  return Result;
}

// ===----------------------------------------------------------------------===
// MS asm statement
// ===----------------------------------------------------------------------===

StmtResult Sema::OnMSAsmStmt(SourceLocation AsmLoc, SourceLocation LBraceLoc,
                             llvm::ArrayRef<Token> AsmToks,
                             llvm::StringRef AsmString, unsigned NumOutputs,
                             unsigned NumInputs,
                             llvm::ArrayRef<llvm::StringRef> Constraints,
                             llvm::ArrayRef<llvm::StringRef> Clobbers,
                             llvm::ArrayRef<Expr *> Exprs,
                             SourceLocation EndLoc) {
  bool IsSimple = (NumOutputs != 0 || NumInputs != 0);
  setFunctionHasBranchProtectedScope();

  bool InvalidOperand = false;
  for (uint64_t I = 0; I < NumOutputs + NumInputs; ++I) {
    Expr *E = Exprs[I];
    if (E->getType()->isBitIntType()) {
      InvalidOperand = true;
      Diag(E->getBeginLoc(), diag::err_asm_invalid_type)
          << E->getType() << (I < NumOutputs) << E->getSourceRange();
    } else if (E->refersToBitField()) {
      InvalidOperand = true;
      FieldDecl *BitField = E->getSourceBitField();
      Diag(E->getBeginLoc(), diag::err_ms_asm_bitfield_unsupported)
          << E->getSourceRange();
      Diag(BitField->getLocation(), diag::note_bitfield_decl);
    }
  }
  if (InvalidOperand)
    return StmtError();

  MSAsmStmt *NS = new (Context)
      MSAsmStmt(Context, AsmLoc, LBraceLoc, IsSimple,
                /*IsVolatile*/ true, AsmToks, NumOutputs, NumInputs,
                Constraints, Exprs, AsmString, Clobbers, EndLoc);
  return NS;
}

LabelDecl *Sema::GetOrCreateMSAsmLabel(llvm::StringRef ExternalLabelName,
                                       SourceLocation Location,
                                       bool AlwaysCreate) {
  LabelDecl *Label =
      LookupOrCreateLabel(PP.getIdentifierInfo(ExternalLabelName), Location);

  if (Label->isMSAsmLabel()) {
    Label->markUsed(Context);
  } else {
    std::string InternalName;
    llvm::raw_string_ostream OS(InternalName);
    // Create an internal name for the label.  The name should not be a valid
    // mangled name, and should be unique.  We use a dot to make the name an
    // invalid mangled name. We use LLVM's inline asm ${:uid} escape so that a
    // unique label is generated each time this blob is emitted, even after
    // inlining or LTO.
    OS << "__MSASMLABEL_.${:uid}__";
    for (char C : ExternalLabelName) {
      OS << C;
      // We escape '$' in asm strings by replacing it with "$$"
      if (C == '$')
        OS << '$';
    }
    Label->setMSAsmLabel(OS.str());
  }
  if (AlwaysCreate) {
    // The label might have been created implicitly from a previously
    // encountered goto statement.  So, for both newly created and looked up
    // labels, we mark them as resolved.
    Label->setMSAsmLabelResolved();
  }
  Label->setLocation(Location);

  return Label;
}
