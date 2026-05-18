#include "neverc/Analyze/Designator.h"
#include "neverc/Analyze/Initialization.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Tree/Expr/IgnoreExpr.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace neverc;

// ===----------------------------------------------------------------------===
// String & scalar initialization helpers
// ===----------------------------------------------------------------------===

namespace {
bool isWideCharCompatible(QualType T, TreeContext &Context) {
  if (Context.typesAreCompatible(Context.getWideCharType(), T))
    return true;
  if (Context.getLangOpts().C11) {
    return Context.typesAreCompatible(Context.Char16Ty, T) ||
           Context.typesAreCompatible(Context.Char32Ty, T);
  }
  return false;
}
} // namespace

enum StringInitFailureKind {
  SIF_None,
  SIF_NarrowStringIntoWideChar,
  SIF_WideStringIntoChar,
  SIF_IncompatWideStringIntoWideChar,
  SIF_UTF8StringIntoPlainChar,
  SIF_PlainStringIntoUTF8Char,
  SIF_Other
};

namespace {
StringInitFailureKind isStringInit(Expr *Init, const ArrayType *AT,
                                   TreeContext &Context) {
  if (!isa<ConstantArrayType>(AT) && !isa<IncompleteArrayType>(AT))
    return SIF_Other;

  Init = Init->IgnoreParens();
  StringLiteral *SL = dyn_cast<StringLiteral>(Init);
  if (!SL)
    return SIF_Other;

  const QualType ElemTy =
      Context.getCanonicalType(AT->getElementType()).getUnqualifiedType();

  auto IsCharOrUnsignedChar = [](const QualType &T) {
    const BuiltinType *BT = dyn_cast<BuiltinType>(T.getTypePtr());
    return BT && BT->isCharType() && BT->getKind() != BuiltinType::SChar;
  };

  switch (SL->getKind()) {
  case StringLiteralKind::UTF8:
    // char8_t array can be initialized with a UTF-8 string; char/unsigned char
    // may also be initialized by a UTF-8 string literal when char8_t support
    // is enabled.
    if (ElemTy->isChar8Type() ||
        (Context.getLangOpts().Char8 &&
         IsCharOrUnsignedChar(ElemTy.getCanonicalType())))
      return SIF_None;
    [[fallthrough]];
  case StringLiteralKind::Ordinary:
    // char array can be initialized with a narrow string.
    // Only allow char x[] = "foo";  not char x[] = L"foo";
    if (ElemTy->isCharType())
      return (SL->getKind() == StringLiteralKind::UTF8 &&
              Context.getLangOpts().Char8)
                 ? SIF_UTF8StringIntoPlainChar
                 : SIF_None;
    if (ElemTy->isChar8Type())
      return SIF_PlainStringIntoUTF8Char;
    if (isWideCharCompatible(ElemTy, Context))
      return SIF_NarrowStringIntoWideChar;
    return SIF_Other;
  // Wide char arrays can be initialized by a string literal whose
  // encoding prefix matches the element type (L, u, or U).
  case StringLiteralKind::UTF16:
    if (Context.typesAreCompatible(Context.Char16Ty, ElemTy))
      return SIF_None;
    if (ElemTy->isCharType() || ElemTy->isChar8Type())
      return SIF_WideStringIntoChar;
    if (isWideCharCompatible(ElemTy, Context))
      return SIF_IncompatWideStringIntoWideChar;
    return SIF_Other;
  case StringLiteralKind::UTF32:
    if (Context.typesAreCompatible(Context.Char32Ty, ElemTy))
      return SIF_None;
    if (ElemTy->isCharType() || ElemTy->isChar8Type())
      return SIF_WideStringIntoChar;
    if (isWideCharCompatible(ElemTy, Context))
      return SIF_IncompatWideStringIntoWideChar;
    return SIF_Other;
  case StringLiteralKind::Wide:
    if (Context.typesAreCompatible(Context.getWideCharType(), ElemTy))
      return SIF_None;
    if (ElemTy->isCharType() || ElemTy->isChar8Type())
      return SIF_WideStringIntoChar;
    if (isWideCharCompatible(ElemTy, Context))
      return SIF_IncompatWideStringIntoWideChar;
    return SIF_Other;
  case StringLiteralKind::Unevaluated:
    assert(false && "Unevaluated string literal in initialization");
    break;
  }

  llvm_unreachable("missed a StringLiteral kind?");
}

StringInitFailureKind isStringInit(Expr *init, QualType declType,
                                   TreeContext &Context) {
  const ArrayType *arrayType = Context.getAsArrayType(declType);
  if (!arrayType)
    return SIF_Other;
  return isStringInit(init, arrayType, Context);
}
} // namespace

bool Sema::IsStringInit(Expr *Init, const ArrayType *AT) {
  return ::isStringInit(Init, AT, Context) == SIF_None;
}

namespace {
void updateStringLiteralType(Expr *E, QualType Ty) {
  while (true) {
    E->setType(Ty);
    E->setValueKind(VK_PRValue);
    if (isa<StringLiteral>(E))
      break;
    E = IgnoreParensSingleStep(E);
  }
}

void updateGNUCompoundLiteralRValue(Expr *E) {
  while (true) {
    E->setValueKind(VK_PRValue);
    if (isa<CompoundLiteralExpr>(E))
      break;
    E = IgnoreParensSingleStep(E);
  }
}

void checkStringInit(Expr *Str, QualType &DeclT, const ArrayType *AT, Sema &S) {
  auto *ConstantArrayTy =
      cast<ConstantArrayType>(Str->getType()->getAsArrayTypeUnsafe());
  uint64_t StrLength = ConstantArrayTy->getSize().getZExtValue();

  if (const IncompleteArrayType *IAT = dyn_cast<IncompleteArrayType>(AT)) {
    // Incomplete char array initialized by string: size = string length.
    llvm::APInt ConstVal(32, StrLength);
    DeclT = S.Context.getConstantArrayType(
        IAT->getElementType(), ConstVal, nullptr, ArraySizeModifier::Normal, 0);
    updateStringLiteralType(Str, DeclT);
    return;
  }

  const ConstantArrayType *CAT = cast<ConstantArrayType>(AT);

  // We have an array of character type with known size.  However,
  // the size may be smaller or larger than the string we are initializing.
  // Warn if string initializer is too long for the array.
  if (StrLength - 1 > CAT->getSize().getZExtValue())
    S.Diag(Str->getBeginLoc(),
           diag::ext_initializer_string_for_char_array_too_long)
        << Str->getSourceRange();

  // Set the type to the actual size that we are initializing.  If we have
  // something like:
  //   char x[1] = "foo";
  // then this will set the string literal's type to char[1].
  updateStringLiteralType(Str, DeclT);
}
} // namespace

// ===----------------------------------------------------------------------===
// InitListChecker — aggregate initialization
// ===----------------------------------------------------------------------===

namespace {

class InitListChecker {
  Sema &SemaRef;
  bool hadError = false;
  bool VerifyOnly;                // No diagnostics.
  bool TreatUnavailableAsInvalid; // Used only in VerifyOnly mode.
  InitListExpr *FullyStructuredList = nullptr;
  NoInitExpr *DummyExpr = nullptr;
  llvm::SmallVectorImpl<QualType> *AggrDeductionCandidateParamTypes = nullptr;

  NoInitExpr *getDummyInit() {
    if (!DummyExpr)
      DummyExpr = new (SemaRef.Context) NoInitExpr(SemaRef.Context.VoidTy);
    return DummyExpr;
  }

  void CheckImplicitInitList(const InitializedEntity &Entity,
                             InitListExpr *ParentIList, QualType T,
                             unsigned &Index, InitListExpr *StructuredList,
                             unsigned &StructuredIndex);
  void CheckExplicitInitList(const InitializedEntity &Entity,
                             InitListExpr *IList, QualType &T,
                             InitListExpr *StructuredList,
                             bool TopLevelObject = false);
  void CheckListElementTypes(const InitializedEntity &Entity,
                             InitListExpr *IList, QualType &DeclType,
                             bool SubobjectIsDesignatorContext, unsigned &Index,
                             InitListExpr *StructuredList,
                             unsigned &StructuredIndex,
                             bool TopLevelObject = false);
  void CheckSubElementType(const InitializedEntity &Entity, InitListExpr *IList,
                           QualType ElemType, unsigned &Index,
                           InitListExpr *StructuredList,
                           unsigned &StructuredIndex,
                           bool DirectlyDesignated = false);
  void CheckComplexType(const InitializedEntity &Entity, InitListExpr *IList,
                        QualType DeclType, unsigned &Index,
                        InitListExpr *StructuredList,
                        unsigned &StructuredIndex);
  void CheckScalarType(const InitializedEntity &Entity, InitListExpr *IList,
                       QualType DeclType, unsigned &Index,
                       InitListExpr *StructuredList, unsigned &StructuredIndex);
  void CheckVectorType(const InitializedEntity &Entity, InitListExpr *IList,
                       QualType DeclType, unsigned &Index,
                       InitListExpr *StructuredList, unsigned &StructuredIndex);
  void CheckStructUnionTypes(const InitializedEntity &Entity,
                             InitListExpr *IList, QualType DeclType,
                             RecordDecl::field_iterator Field,
                             bool SubobjectIsDesignatorContext, unsigned &Index,
                             InitListExpr *StructuredList,
                             unsigned &StructuredIndex,
                             bool TopLevelObject = false);
  void CheckArrayType(const InitializedEntity &Entity, InitListExpr *IList,
                      QualType &DeclType, llvm::APSInt elementIndex,
                      bool SubobjectIsDesignatorContext, unsigned &Index,
                      InitListExpr *StructuredList, unsigned &StructuredIndex);
  bool CheckDesignatedInitializer(
      const InitializedEntity &Entity, InitListExpr *IList,
      DesignatedInitExpr *DIE, unsigned DesigIdx, QualType &CurrentObjectType,
      RecordDecl::field_iterator *NextField, llvm::APSInt *NextElementIndex,
      unsigned &Index, InitListExpr *StructuredList, unsigned &StructuredIndex,
      bool FinishSubobjectInit, bool TopLevelObject);
  InitListExpr *getStructuredSubobjectInit(InitListExpr *IList, unsigned Index,
                                           QualType CurrentObjectType,
                                           InitListExpr *StructuredList,
                                           unsigned StructuredIndex,
                                           SourceRange InitRange,
                                           bool IsFullyOverwritten = false);
  void UpdateStructuredListElement(InitListExpr *StructuredList,
                                   unsigned &StructuredIndex, Expr *expr);
  InitListExpr *createInitListExpr(QualType CurrentObjectType,
                                   SourceRange InitRange,
                                   unsigned ExpectedNumInits);
  int numArrayElements(QualType DeclType);
  int numStructUnionElements(QualType DeclType);
  static RecordDecl *getRecordDecl(QualType DeclType);

  ExprResult PerformEmptyInit(SourceLocation Loc,
                              const InitializedEntity &Entity);

  void diagnoseInitOverride(Expr *OldInit, SourceRange NewInitRange,
                            bool UnionOverride = false,
                            bool FullyOverwritten = true) {
    // Overriding an initializer via a designator follows C designated-init
    // rules; some other language modes treat overlapping designators
    // differently.
    unsigned DiagID = diag::warn_initializer_overrides;

    if (OldInit->getType().isDestructedType() && !FullyOverwritten) {
      DiagID = diag::err_initializer_overrides_destructed;
    } else if (!OldInit->getSourceRange().isValid()) {
      // We need to check on source range validity because the previous
      // initializer does not have to be an explicit initializer. e.g.,
      //
      // struct P { int a, b; };
      // struct PP { struct P p } l = { { .a = 2 }, .p.b = 3 };
      //
      // There is an overwrite taking place because the first braced initializer
      // list "{ .a = 2 }" already provides value for .p.b (which is zero).
      //
      // Such overwrites are harmless, so we don't diagnose them.
      return;
    }

    if (!VerifyOnly) {
      SemaRef.Diag(NewInitRange.getBegin(), DiagID)
          << NewInitRange << FullyOverwritten << OldInit->getType();
      SemaRef.Diag(OldInit->getBeginLoc(), diag::note_previous_initializer)
          << (OldInit->HasSideEffects(SemaRef.Context) && FullyOverwritten)
          << OldInit->getSourceRange();
    }
  }

  // Explanation on the "FillWithNoInit" mode:
  //
  // Assume we have the following definitions (Case#1):
  // struct P { char x[6][6]; } xp = { .x[1] = "bar" };
  // struct PP { struct P lp; } l = { .lp = xp, .lp.x[1][2] = 'f' };
  //
  // l.lp.x[1][0..1] should not be filled with implicit initializers because the
  // "base" initializer "xp" will provide values for them; l.lp.x[1] will be
  // "baf".
  //
  // But if we have (Case#2):
  // struct PP l = { .lp = xp, .lp.x[1] = { [2] = 'f' } };
  //
  // l.lp.x[1][0..1] are implicitly initialized and do not use values from the
  // "base" initializer; l.lp.x[1] will be "\0\0f\0\0\0".
  //
  // To distinguish Case#1 from Case#2, and also to avoid leaving many "holes"
  // in the InitListExpr, the "holes" in Case#1 are filled not with empty
  // initializers but with special "NoInitExpr" place holders, which tells the
  // CodeGen not to generate any initializers for these parts.
  void FillInEmptyInitForField(unsigned Init, FieldDecl *Field,
                               const InitializedEntity &ParentEntity,
                               InitListExpr *ILE, bool &RequiresSecondPass,
                               bool FillWithNoInit = false);
  void FillInEmptyInitializations(const InitializedEntity &Entity,
                                  InitListExpr *ILE, bool &RequiresSecondPass,
                                  InitListExpr *OuterILE, unsigned OuterIndex,
                                  bool FillWithNoInit = false);
  bool CheckFlexibleArrayInit(const InitializedEntity &Entity, Expr *InitExpr,
                              FieldDecl *Field, bool TopLevelObject);
  void CheckEmptyInitializable(const InitializedEntity &Entity,
                               SourceLocation Loc);

public:
  InitListChecker(Sema &S, const InitializedEntity &Entity, InitListExpr *IL,
                  QualType &T, bool VerifyOnly, bool TreatUnavailableAsInvalid,
                  llvm::SmallVectorImpl<QualType>
                      *AggrDeductionCandidateParamTypes = nullptr);
  InitListChecker(
      Sema &S, const InitializedEntity &Entity, InitListExpr *IL, QualType &T,
      llvm::SmallVectorImpl<QualType> &AggrDeductionCandidateParamTypes)
      : InitListChecker(S, Entity, IL, T, /*VerifyOnly=*/true,
                        /*TreatUnavailableAsInvalid=*/false,
                        &AggrDeductionCandidateParamTypes) {};

  bool HadError() { return hadError; }

  // Retrieves the fully-structured initializer list used for
  // semantic analysis and code generation.
  InitListExpr *getFullyStructuredList() const { return FullyStructuredList; }
};

} // end anonymous namespace

ExprResult InitListChecker::PerformEmptyInit(SourceLocation Loc,
                                             const InitializedEntity &Entity) {
  InitializationKind Kind =
      InitializationKind::CreateValue(Loc, Loc, Loc, true);
  MultiExprArg SubInit;
  InitializationSequence InitSeq(SemaRef, Entity, Kind, SubInit);
  if (!InitSeq) {
    if (!VerifyOnly) {
      InitSeq.Diagnose(SemaRef, Entity, Kind, SubInit);
      if (Entity.getKind() == InitializedEntity::EK_Member)
        SemaRef.Diag(Entity.getDecl()->getLocation(),
                     diag::note_in_omitted_aggregate_initializer)
            << /*field*/ 1 << Entity.getDecl();
      else if (Entity.getKind() == InitializedEntity::EK_ArrayElement) {

        SemaRef.Diag(Loc, diag::note_in_omitted_aggregate_initializer)
            << /*array element*/ 0 << Entity.getElementIndex();
      }
    }
    hadError = true;
    return ExprError();
  }

  return VerifyOnly ? ExprResult()
                    : InitSeq.Perform(SemaRef, Entity, Kind, SubInit);
}

void InitListChecker::CheckEmptyInitializable(const InitializedEntity &Entity,
                                              SourceLocation Loc) {
  // If we're building a fully-structured list, we'll check this at the end
  // once we know which elements are actually initialized. Otherwise, we know
  // that there are no designators so we can just check now.
  if (FullyStructuredList)
    return;
  PerformEmptyInit(Loc, Entity);
}

void InitListChecker::FillInEmptyInitForField(
    unsigned Init, FieldDecl *Field, const InitializedEntity &ParentEntity,
    InitListExpr *ILE, bool &RequiresSecondPass, bool FillWithNoInit) {
  SourceLocation Loc = ILE->getEndLoc();
  unsigned NumInits = ILE->getNumInits();
  InitializedEntity MemberEntity =
      InitializedEntity::InitializeMember(Field, &ParentEntity);

  if (Init >= NumInits || !ILE->getInit(Init)) {
    if (const RecordType *RType = ILE->getType()->getAs<RecordType>())
      if (!RType->getDecl()->isUnion())
        assert((Init < NumInits || VerifyOnly) &&
               "This ILE should have been expanded");

    if (FillWithNoInit) {
      assert(!VerifyOnly && "should not fill with no-init in verify-only mode");
      Expr *Filler = new (SemaRef.Context) NoInitExpr(Field->getType());
      if (Init < NumInits)
        ILE->setInit(Init, Filler);
      else
        ILE->updateInit(SemaRef.Context, Init, Filler);
      return;
    }

    ExprResult MemberInit = PerformEmptyInit(Loc, MemberEntity);
    if (MemberInit.isInvalid()) {
      hadError = true;
      return;
    }

    if (hadError || VerifyOnly) {
      // Do nothing
    } else if (Init < NumInits) {
      ILE->setInit(Init, MemberInit.getAs<Expr>());
    } else {
      ILE->setInit(Init, MemberInit.getAs<Expr>());
    }
  } else if (InitListExpr *InnerILE =
                 dyn_cast<InitListExpr>(ILE->getInit(Init))) {
    FillInEmptyInitializations(MemberEntity, InnerILE, RequiresSecondPass, ILE,
                               Init, FillWithNoInit);
  } else if (DesignatedInitUpdateExpr *InnerDIUE =
                 dyn_cast<DesignatedInitUpdateExpr>(ILE->getInit(Init))) {
    FillInEmptyInitializations(MemberEntity, InnerDIUE->getUpdater(),
                               RequiresSecondPass, ILE, Init,
                               /*FillWithNoInit =*/true);
  }
}

void InitListChecker::FillInEmptyInitializations(
    const InitializedEntity &Entity, InitListExpr *ILE,
    bool &RequiresSecondPass, InitListExpr *OuterILE, unsigned OuterIndex,
    bool FillWithNoInit) {
  assert((ILE->getType() != SemaRef.Context.VoidTy) &&
         "Should not have void type");

  // We don't need to do any checks when just filling NoInitExprs; that can't
  // fail.
  if (FillWithNoInit && VerifyOnly)
    return;

  // If this is a nested initializer list, we might have changed its contents
  // (and therefore some of its properties, such as instantiation-dependence)
  // while filling it in. Inform the outer initializer list so that its state
  // can be updated to match.
  // We should fully build the inner initializers before constructing
  // the outer InitListExpr instead of mutating AST nodes after they have
  // been used as subexpressions of other nodes.
  struct UpdateOuterILEWithUpdatedInit {
    InitListExpr *Outer;
    unsigned OuterIndex;
    ~UpdateOuterILEWithUpdatedInit() {
      if (Outer)
        Outer->setInit(OuterIndex, Outer->getInit(OuterIndex));
    }
  } UpdateOuterRAII = {OuterILE, OuterIndex};

  // A transparent ILE is not performing aggregate initialization and should
  // not be filled in.
  if (ILE->isTransparent())
    return;

  if (const RecordType *RType = ILE->getType()->getAs<RecordType>()) {
    const RecordDecl *RDecl = RType->getDecl();
    if (RDecl->isUnion() && ILE->getInitializedFieldInUnion())
      FillInEmptyInitForField(0, ILE->getInitializedFieldInUnion(), Entity, ILE,
                              RequiresSecondPass, FillWithNoInit);
    else {
      // The fields beyond ILE->getNumInits() are default initialized, so in
      // order to leave them uninitialized, the ILE is expanded and the extra
      // fields are then filled with NoInitExpr.
      unsigned NumElems = numStructUnionElements(ILE->getType());
      if (!RDecl->isUnion() && RDecl->hasFlexibleArrayMember())
        ++NumElems;
      if (!VerifyOnly && ILE->getNumInits() < NumElems)
        ILE->resizeInits(SemaRef.Context, NumElems);

      unsigned Init = 0;

      for (auto *Field : RDecl->fields()) {
        if (Field->isUnnamedBitfield())
          continue;

        if (hadError)
          return;

        FillInEmptyInitForField(Init, Field, Entity, ILE, RequiresSecondPass,
                                FillWithNoInit);
        if (hadError)
          return;

        ++Init;

        // Only look at the first initialization of a union.
        if (RDecl->isUnion())
          break;
      }
    }

    return;
  }

  QualType ElementType;

  InitializedEntity ElementEntity = Entity;
  unsigned NumInits = ILE->getNumInits();
  unsigned NumElements = NumInits;
  if (const ArrayType *AType = SemaRef.Context.getAsArrayType(ILE->getType())) {
    ElementType = AType->getElementType();
    if (const auto *CAType = dyn_cast<ConstantArrayType>(AType))
      NumElements = CAType->getSize().getZExtValue();
    ElementEntity =
        InitializedEntity::InitializeElement(SemaRef.Context, 0, Entity);
  } else if (const VectorType *VType = ILE->getType()->getAs<VectorType>()) {
    ElementType = VType->getElementType();
    NumElements = VType->getNumElements();
    ElementEntity =
        InitializedEntity::InitializeElement(SemaRef.Context, 0, Entity);
  } else
    ElementType = ILE->getType();

  bool SkipEmptyInitChecks = false;
  for (unsigned Init = 0; Init != NumElements; ++Init) {
    if (hadError)
      return;

    if (ElementEntity.getKind() == InitializedEntity::EK_ArrayElement ||
        ElementEntity.getKind() == InitializedEntity::EK_VectorElement)
      ElementEntity.setElementIndex(Init);

    if (Init >= NumInits && (ILE->hasArrayFiller() || SkipEmptyInitChecks))
      return;

    Expr *InitExpr = (Init < NumInits ? ILE->getInit(Init) : nullptr);
    if (!InitExpr && Init < NumInits && ILE->hasArrayFiller())
      ILE->setInit(Init, ILE->getArrayFiller());
    else if (!InitExpr && !ILE->hasArrayFiller()) {
      // In VerifyOnly mode, there's no point performing empty initialization
      // more than once.
      if (SkipEmptyInitChecks)
        continue;

      Expr *Filler = nullptr;

      if (FillWithNoInit)
        Filler = new (SemaRef.Context) NoInitExpr(ElementType);
      else {
        ExprResult ElementInit =
            PerformEmptyInit(ILE->getEndLoc(), ElementEntity);
        if (ElementInit.isInvalid()) {
          hadError = true;
          return;
        }

        Filler = ElementInit.getAs<Expr>();
      }

      if (hadError) {
        // Do nothing
      } else if (VerifyOnly) {
        SkipEmptyInitChecks = true;
      } else if (Init < NumInits) {
        // For arrays, just set the expression used for value-initialization
        // of the "holes" in the array.
        if (ElementEntity.getKind() == InitializedEntity::EK_ArrayElement)
          ILE->setArrayFiller(Filler);
        else
          ILE->setInit(Init, Filler);
      } else {
        // For arrays, just set the expression used for value-initialization
        // of the rest of elements and exit.
        if (ElementEntity.getKind() == InitializedEntity::EK_ArrayElement) {
          ILE->setArrayFiller(Filler);
          return;
        }
      }
    } else if (InitListExpr *InnerILE =
                   dyn_cast_or_null<InitListExpr>(InitExpr)) {
      FillInEmptyInitializations(ElementEntity, InnerILE, RequiresSecondPass,
                                 ILE, Init, FillWithNoInit);
    } else if (DesignatedInitUpdateExpr *InnerDIUE =
                   dyn_cast_or_null<DesignatedInitUpdateExpr>(InitExpr)) {
      FillInEmptyInitializations(ElementEntity, InnerDIUE->getUpdater(),
                                 RequiresSecondPass, ILE, Init,
                                 /*FillWithNoInit =*/true);
    }
  }
}

namespace {
bool hasAnyDesignatedInits(const InitListExpr *IL) {
  for (const Stmt *Init : *IL)
    if (isa_and_nonnull<DesignatedInitExpr>(Init))
      return true;
  return false;
}
} // namespace

InitListChecker::InitListChecker(
    Sema &S, const InitializedEntity &Entity, InitListExpr *IL, QualType &T,
    bool VerifyOnly, bool TreatUnavailableAsInvalid,
    llvm::SmallVectorImpl<QualType> *AggrDeductionCandidateParamTypes)
    : SemaRef(S), VerifyOnly(VerifyOnly),
      TreatUnavailableAsInvalid(TreatUnavailableAsInvalid),
      AggrDeductionCandidateParamTypes(AggrDeductionCandidateParamTypes) {
  if (!VerifyOnly || hasAnyDesignatedInits(IL)) {
    FullyStructuredList =
        createInitListExpr(T, IL->getSourceRange(), IL->getNumInits());

    if (!VerifyOnly)
      FullyStructuredList->setSyntacticForm(IL);
  }

  CheckExplicitInitList(Entity, IL, T, FullyStructuredList,
                        /*TopLevelObject=*/true);

  if (!hadError && !AggrDeductionCandidateParamTypes && FullyStructuredList) {
    bool RequiresSecondPass = false;
    FillInEmptyInitializations(Entity, FullyStructuredList, RequiresSecondPass,
                               /*OuterILE=*/nullptr, /*OuterIndex=*/0);
  }
  if (hadError && FullyStructuredList)
    FullyStructuredList->markError();
}

int InitListChecker::numArrayElements(QualType DeclType) {
  int maxElements = 0x7FFFFFFF;
  if (const ConstantArrayType *CAT =
          SemaRef.Context.getAsConstantArrayType(DeclType)) {
    maxElements = static_cast<int>(CAT->getSize().getZExtValue());
  }
  return maxElements;
}

int InitListChecker::numStructUnionElements(QualType DeclType) {
  RecordDecl *structDecl = DeclType->castAs<RecordType>()->getDecl();
  int InitializableMembers = 0;
  for (const auto *Field : structDecl->fields())
    if (!Field->isUnnamedBitfield())
      ++InitializableMembers;

  if (structDecl->isUnion())
    return std::min(InitializableMembers, 1);
  return InitializableMembers - structDecl->hasFlexibleArrayMember();
}

RecordDecl *InitListChecker::getRecordDecl(QualType DeclType) {
  if (const auto *RT = DeclType->getAs<RecordType>())
    return RT->getDecl();
  return nullptr;
}

namespace {
bool isIdiomaticBraceElisionEntity(const InitializedEntity &Entity) {
  if (!Entity.getParent())
    return false;

  // Allow brace elision if the only subobject is a field.
  if (Entity.getKind() == InitializedEntity::EK_Member) {
    auto *ParentRD =
        Entity.getParent()->getType()->castAs<RecordType>()->getDecl();
    auto FieldIt = ParentRD->field_begin();
    assert(FieldIt != ParentRD->field_end() &&
           "no fields but have initializer for member?");
    return ++FieldIt == ParentRD->field_end();
  }

  return false;
}
} // namespace

void InitListChecker::CheckImplicitInitList(const InitializedEntity &Entity,
                                            InitListExpr *ParentIList,
                                            QualType T, unsigned &Index,
                                            InitListExpr *StructuredList,
                                            unsigned &StructuredIndex) {
  int maxElements = 0;

  if (T->isArrayType())
    maxElements = numArrayElements(T);
  else if (T->isRecordType())
    maxElements = numStructUnionElements(T);
  else if (T->isVectorType())
    maxElements = T->castAs<VectorType>()->getNumElements();
  else
    llvm_unreachable("CheckImplicitInitList(): Illegal type");

  if (maxElements == 0) {
    if (!VerifyOnly)
      SemaRef.Diag(ParentIList->getInit(Index)->getBeginLoc(),
                   diag::err_implicit_empty_initializer);
    ++Index;
    hadError = true;
    return;
  }

  InitListExpr *StructuredSubobjectInitList = getStructuredSubobjectInit(
      ParentIList, Index, T, StructuredList, StructuredIndex,
      SourceRange(ParentIList->getInit(Index)->getBeginLoc(),
                  ParentIList->getSourceRange().getEnd()));
  unsigned StructuredSubobjectInitIndex = 0;
  unsigned StartIndex = Index;
  CheckListElementTypes(Entity, ParentIList, T,
                        /*SubobjectIsDesignatorContext=*/false, Index,
                        StructuredSubobjectInitList,
                        StructuredSubobjectInitIndex);

  if (StructuredSubobjectInitList) {
    StructuredSubobjectInitList->setType(T);

    unsigned EndIndex = (Index == StartIndex ? StartIndex : Index - 1);
    // Update the structured sub-object initializer so that it's ending
    // range corresponds with the end of the last initializer it used.
    if (EndIndex < ParentIList->getNumInits() &&
        ParentIList->getInit(EndIndex)) {
      SourceLocation EndLoc =
          ParentIList->getInit(EndIndex)->getSourceRange().getEnd();
      StructuredSubobjectInitList->setRBraceLoc(EndLoc);
    }
#ifndef _WIN32
    // Complain about missing braces.
    if (!VerifyOnly && (T->isArrayType() || T->isRecordType()) &&
        !ParentIList->isIdiomaticZeroInitializer(SemaRef.getLangOpts()) &&
        !isIdiomaticBraceElisionEntity(Entity)) {
      SemaRef.Diag(StructuredSubobjectInitList->getBeginLoc(),
                   diag::warn_missing_braces)
          << StructuredSubobjectInitList->getSourceRange()
          << FixItHint::CreateInsertion(
                 StructuredSubobjectInitList->getBeginLoc(), "{")
          << FixItHint::CreateInsertion(
                 SemaRef.getLocForEndOfToken(
                     StructuredSubobjectInitList->getEndLoc()),
                 "}");
    }
#endif
  }
}

namespace {
void warnBracedScalarInit(Sema &S, const InitializedEntity &Entity,
                          SourceRange Braces) {
  unsigned DiagID = 0;

  switch (Entity.getKind()) {
  case InitializedEntity::EK_VectorElement:
  case InitializedEntity::EK_ComplexElement:
  case InitializedEntity::EK_ArrayElement:
  case InitializedEntity::EK_Parameter:
  case InitializedEntity::EK_Result:
    // Extra braces here are suspicious.
    DiagID = diag::warn_braces_around_init;
    break;

  case InitializedEntity::EK_Member:
    if (Entity.getParent())
      DiagID = diag::warn_braces_around_init;
    break;

  case InitializedEntity::EK_Variable:
    break;

  case InitializedEntity::EK_Temporary:
  case InitializedEntity::EK_CompoundLiteralInit:
    break;

  case InitializedEntity::EK_StmtExprResult:
    llvm_unreachable("unexpected braced scalar init");
  }

  if (DiagID) {
    S.Diag(Braces.getBegin(), DiagID)
        << Entity.getType()->isSizelessBuiltinType() << Braces
        << FixItHint::CreateRemoval(Braces.getBegin())
        << FixItHint::CreateRemoval(Braces.getEnd());
  }
}
} // namespace

void InitListChecker::CheckExplicitInitList(const InitializedEntity &Entity,
                                            InitListExpr *IList, QualType &T,
                                            InitListExpr *StructuredList,
                                            bool TopLevelObject) {
  unsigned Index = 0, StructuredIndex = 0;
  CheckListElementTypes(Entity, IList, T, /*SubobjectIsDesignatorContext=*/true,
                        Index, StructuredList, StructuredIndex, TopLevelObject);
  if (StructuredList) {
    QualType ExprTy = T;
    if (!ExprTy->isArrayType())
      ExprTy = ExprTy.getNonLValueExprType(SemaRef.Context);
    if (!VerifyOnly)
      IList->setType(ExprTy);
    StructuredList->setType(ExprTy);
  }
  if (hadError)
    return;

  // Don't complain for incomplete types, since we'll get an error elsewhere.
  if (Index < IList->getNumInits() && !T->isIncompleteType()) {
    // We have leftover initializers
    bool ExtraInitsIsError = false;
    hadError = ExtraInitsIsError;
    if (VerifyOnly) {
      return;
    } else if (StructuredIndex == 1 &&
               isStringInit(StructuredList->getInit(0), T, SemaRef.Context) ==
                   SIF_None) {
      unsigned DK =
          ExtraInitsIsError
              ? diag::err_excess_initializers_in_char_array_initializer
              : diag::ext_excess_initializers_in_char_array_initializer;
      SemaRef.Diag(IList->getInit(Index)->getBeginLoc(), DK)
          << IList->getInit(Index)->getSourceRange();
    } else if (T->isSizelessBuiltinType()) {
      unsigned DK = ExtraInitsIsError
                        ? diag::err_excess_initializers_for_sizeless_type
                        : diag::ext_excess_initializers_for_sizeless_type;
      SemaRef.Diag(IList->getInit(Index)->getBeginLoc(), DK)
          << T << IList->getInit(Index)->getSourceRange();
    } else {
      int initKind = T->isArrayType()    ? 0
                     : T->isVectorType() ? 1
                     : T->isScalarType() ? 2
                     : T->isUnionType()  ? 3
                                         : 4;

      unsigned DK = ExtraInitsIsError ? diag::err_excess_initializers
                                      : diag::ext_excess_initializers;
      SemaRef.Diag(IList->getInit(Index)->getBeginLoc(), DK)
          << initKind << IList->getInit(Index)->getSourceRange();
    }
  }

  if (!VerifyOnly) {
    if (T->isScalarType() && IList->getNumInits() == 1 &&
        !isa<InitListExpr>(IList->getInit(0)))
      warnBracedScalarInit(SemaRef, Entity, IList->getSourceRange());
  }
}

void InitListChecker::CheckListElementTypes(
    const InitializedEntity &Entity, InitListExpr *IList, QualType &DeclType,
    bool SubobjectIsDesignatorContext, unsigned &Index,
    InitListExpr *StructuredList, unsigned &StructuredIndex,
    bool TopLevelObject) {
  if (DeclType->isAnyComplexType() && SubobjectIsDesignatorContext) {
    // Explicitly braced initializer for complex type can be real+imaginary
    // parts.
    CheckComplexType(Entity, IList, DeclType, Index, StructuredList,
                     StructuredIndex);
  } else if (DeclType->isScalarType()) {
    CheckScalarType(Entity, IList, DeclType, Index, StructuredList,
                    StructuredIndex);
  } else if (DeclType->isVectorType()) {
    CheckVectorType(Entity, IList, DeclType, Index, StructuredList,
                    StructuredIndex);
  } else if (const RecordDecl *RD = getRecordDecl(DeclType)) {
    if (DeclType->isRecordType()) {
      assert(DeclType->isAggregateType() &&
             "non-aggregate records should be handed in CheckSubElementType");
    }
    CheckStructUnionTypes(Entity, IList, DeclType, RD->field_begin(),
                          SubobjectIsDesignatorContext, Index, StructuredList,
                          StructuredIndex, TopLevelObject);
  } else if (DeclType->isArrayType()) {
    llvm::APSInt Zero(
        SemaRef.Context.getTypeSize(SemaRef.Context.getSizeType()), false);
    CheckArrayType(Entity, IList, DeclType, Zero, SubobjectIsDesignatorContext,
                   Index, StructuredList, StructuredIndex);
  } else if (DeclType->isVoidType() || DeclType->isFunctionType()) {
    // This type is invalid, issue a diagnostic.
    ++Index;
    if (!VerifyOnly)
      SemaRef.Diag(IList->getBeginLoc(), diag::err_illegal_initializer_type)
          << DeclType;
    hadError = true;
  } else if (DeclType->isSizelessBuiltinType()) {
    CheckScalarType(Entity, IList, DeclType, Index, StructuredList,
                    StructuredIndex);
  } else {
    if (!VerifyOnly)
      SemaRef.Diag(IList->getBeginLoc(), diag::err_illegal_initializer_type)
          << DeclType;
    hadError = true;
  }
}

void InitListChecker::CheckSubElementType(const InitializedEntity &Entity,
                                          InitListExpr *IList,
                                          QualType ElemType, unsigned &Index,
                                          InitListExpr *StructuredList,
                                          unsigned &StructuredIndex,
                                          bool DirectlyDesignated) {
  Expr *expr = IList->getInit(Index);

  if (InitListExpr *SubInitList = dyn_cast<InitListExpr>(expr)) {
    if (SubInitList->getNumInits() == 1 &&
        isStringInit(SubInitList->getInit(0), ElemType, SemaRef.Context) ==
            SIF_None) {
      expr = SubInitList->getInit(0);
    }
    // Nested aggregate cases fall through below.
  } else if (isa<ImplicitValueInitExpr>(expr)) {
    assert(SemaRef.Context.hasSameType(expr->getType(), ElemType) &&
           "found implicit initialization for the wrong type");
    UpdateStructuredListElement(StructuredList, StructuredIndex, expr);
    ++Index;
    return;
  }

  if (isa<InitListExpr>(expr)) {

    InitializationKind Kind =
        InitializationKind::CreateCopy(expr->getBeginLoc(), SourceLocation());

    // Vector elements can be initialized from other vectors in which case
    // we need initialization entity with a type of a vector (and not a vector
    // element!) initializing multiple vector elements.
    auto TmpEntity =
        (ElemType->isExtVectorType() && !Entity.getType()->isExtVectorType())
            ? InitializedEntity::InitializeTemporary(ElemType)
            : Entity;

    {
      InitializationSequence Seq(SemaRef, TmpEntity, Kind, expr,
                                 /*TopLevelOfInitList*/ true);
      if (Seq || isa<InitListExpr>(expr)) {
        if (!VerifyOnly) {
          ExprResult Result = Seq.Perform(SemaRef, TmpEntity, Kind, expr);
          if (Result.isInvalid())
            hadError = true;

          UpdateStructuredListElement(StructuredList, StructuredIndex,
                                      Result.getAs<Expr>());
        } else if (!Seq) {
          hadError = true;
        } else if (StructuredList) {
          UpdateStructuredListElement(StructuredList, StructuredIndex,
                                      getDummyInit());
        }
        ++Index;
        if (AggrDeductionCandidateParamTypes)
          AggrDeductionCandidateParamTypes->push_back(ElemType);
        return;
      }
    }

    // Fall through for subaggregate initialization
  } else if (ElemType->isScalarType() || ElemType->isAtomicType()) {
    return CheckScalarType(Entity, IList, ElemType, Index, StructuredList,
                           StructuredIndex);
  } else if (const ArrayType *arrayType =
                 SemaRef.Context.getAsArrayType(ElemType)) {
    // arrayType can be incomplete if we're initializing a flexible
    // array member.  There's nothing we can do with the completed
    // type here, though.

    if (isStringInit(expr, arrayType, SemaRef.Context) == SIF_None) {
      if (!VerifyOnly)
        checkStringInit(expr, ElemType, arrayType, SemaRef);
      if (StructuredList)
        UpdateStructuredListElement(StructuredList, StructuredIndex, expr);
      ++Index;
      return;
    }

    // Fall through for subaggregate initialization.

  } else {
    assert((ElemType->isRecordType() || ElemType->isVectorType()) &&
           "Unexpected type");

    // Struct/union with auto storage: accept a compatible single expression.
    ExprResult ExprRes = expr;
    if (SemaRef.CheckSingleAssignmentConstraints(
            ElemType, ExprRes, !VerifyOnly) != Sema::Incompatible) {
      if (ExprRes.isInvalid())
        hadError = true;
      else {
        ExprRes = SemaRef.DefaultFunctionArrayLvalueConversion(ExprRes.get());
        if (ExprRes.isInvalid())
          hadError = true;
      }
      UpdateStructuredListElement(StructuredList, StructuredIndex,
                                  ExprRes.getAs<Expr>());
      ++Index;
      return;
    }
    ExprRes.get();
    // Fall through for subaggregate initialization
  }

  // Brace elision: initializer applies to the first member of the subaggregate.
  if (ElemType->isVectorType() || ElemType->isAggregateType()) {
    CheckImplicitInitList(Entity, IList, ElemType, Index, StructuredList,
                          StructuredIndex);
    ++StructuredIndex;
  } else {
    if (!VerifyOnly) {
      // We cannot initialize this element, so let PerformCopyInitialization
      // produce the appropriate diagnostic. We already checked that this
      // initialization will fail.
      ExprResult Copy =
          SemaRef.PerformCopyInitialization(Entity, SourceLocation(), expr,
                                            /*TopLevelOfInitList=*/true);
      (void)Copy;
      assert(Copy.isInvalid() &&
             "expected non-aggregate initialization to fail");
    }
    hadError = true;
    ++Index;
    ++StructuredIndex;
  }
}

void InitListChecker::CheckComplexType(const InitializedEntity &Entity,
                                       InitListExpr *IList, QualType DeclType,
                                       unsigned &Index,
                                       InitListExpr *StructuredList,
                                       unsigned &StructuredIndex) {
  assert(Index == 0 && "Index in explicit init list must be zero");

  // As an extension, NeverC supports complex initializers, which initialize
  // a complex number component-wise.  When an explicit initializer list for
  // a complex number contains two initializers, this extension kicks in:
  // it expects the initializer list to contain two elements convertible to
  // the element type of the complex type. The first element initializes
  // the real part, and the second element intitializes the imaginary part.

  if (IList->getNumInits() < 2)
    return CheckScalarType(Entity, IList, DeclType, Index, StructuredList,
                           StructuredIndex);

  // Component-wise complex init is a NeverC extension in C.
  if (!VerifyOnly)
    SemaRef.Diag(IList->getBeginLoc(), diag::ext_complex_component_init)
        << IList->getSourceRange();
  QualType elementType = DeclType->castAs<ComplexType>()->getElementType();
  InitializedEntity ElementEntity =
      InitializedEntity::InitializeElement(SemaRef.Context, 0, Entity);

  for (unsigned i = 0; i < 2; ++i) {
    ElementEntity.setElementIndex(Index);
    CheckSubElementType(ElementEntity, IList, elementType, Index,
                        StructuredList, StructuredIndex);
  }
}

void InitListChecker::CheckScalarType(const InitializedEntity &Entity,
                                      InitListExpr *IList, QualType DeclType,
                                      unsigned &Index,
                                      InitListExpr *StructuredList,
                                      unsigned &StructuredIndex) {
  if (Index >= IList->getNumInits()) {
    hadError = false;
    ++Index;
    ++StructuredIndex;
    return;
  }

  Expr *expr = IList->getInit(Index);
  if (InitListExpr *SubIList = dyn_cast<InitListExpr>(expr)) {
    // This is invalid, and accepting it may cause incorrect
    // initialization in some corner cases.
    if (!VerifyOnly)
      SemaRef.Diag(SubIList->getBeginLoc(), diag::ext_many_braces_around_init)
          << DeclType->isSizelessBuiltinType() << SubIList->getSourceRange();

    CheckScalarType(Entity, SubIList, DeclType, Index, StructuredList,
                    StructuredIndex);
    return;
  } else if (isa<DesignatedInitExpr>(expr)) {
    if (!VerifyOnly)
      SemaRef.Diag(expr->getBeginLoc(),
                   diag::err_designator_for_scalar_or_sizeless_init)
          << DeclType->isSizelessBuiltinType() << DeclType
          << expr->getSourceRange();
    hadError = true;
    ++Index;
    ++StructuredIndex;
    return;
  }

  ExprResult Result;
  if (VerifyOnly) {
    if (SemaRef.CanPerformCopyInitialization(Entity, expr))
      Result = getDummyInit();
    else
      Result = ExprError();
  } else {
    Result =
        SemaRef.PerformCopyInitialization(Entity, expr->getBeginLoc(), expr,
                                          /*TopLevelOfInitList=*/true);
  }

  Expr *ResultExpr = nullptr;

  if (Result.isInvalid())
    hadError = true; // types weren't compatible.
  else {
    ResultExpr = Result.getAs<Expr>();

    if (ResultExpr != expr && !VerifyOnly) {
      // The type was promoted, update initializer list.
      IList->setInit(Index, ResultExpr);
    }
  }
  UpdateStructuredListElement(StructuredList, StructuredIndex, ResultExpr);
  ++Index;
  if (AggrDeductionCandidateParamTypes)
    AggrDeductionCandidateParamTypes->push_back(DeclType);
}

void InitListChecker::CheckVectorType(const InitializedEntity &Entity,
                                      InitListExpr *IList, QualType DeclType,
                                      unsigned &Index,
                                      InitListExpr *StructuredList,
                                      unsigned &StructuredIndex) {
  const VectorType *VT = DeclType->castAs<VectorType>();
  unsigned maxElements = VT->getNumElements();
  unsigned numEltsInit = 0;
  QualType elementType = VT->getElementType();

  if (Index >= IList->getNumInits()) {
    // Make sure the element type can be value-initialized.
    CheckEmptyInitializable(
        InitializedEntity::InitializeElement(SemaRef.Context, 0, Entity),
        IList->getEndLoc());
    return;
  }

  {
    // If the initializing element is a vector, try to copy-initialize
    // instead of breaking it apart (which is doomed to failure anyway).
    Expr *Init = IList->getInit(Index);
    if (!isa<InitListExpr>(Init) && Init->getType()->isVectorType()) {
      ExprResult Result;
      if (VerifyOnly) {
        if (SemaRef.CanPerformCopyInitialization(Entity, Init))
          Result = getDummyInit();
        else
          Result = ExprError();
      } else {
        Result =
            SemaRef.PerformCopyInitialization(Entity, Init->getBeginLoc(), Init,
                                              /*TopLevelOfInitList=*/true);
      }

      Expr *ResultExpr = nullptr;
      if (Result.isInvalid())
        hadError = true; // types weren't compatible.
      else {
        ResultExpr = Result.getAs<Expr>();

        if (ResultExpr != Init && !VerifyOnly) {
          // The type was promoted, update initializer list.
          IList->setInit(Index, ResultExpr);
        }
      }
      UpdateStructuredListElement(StructuredList, StructuredIndex, ResultExpr);
      ++Index;
      if (AggrDeductionCandidateParamTypes)
        AggrDeductionCandidateParamTypes->push_back(elementType);
      return;
    }

    InitializedEntity ElementEntity =
        InitializedEntity::InitializeElement(SemaRef.Context, 0, Entity);

    for (unsigned i = 0; i < maxElements; ++i, ++numEltsInit) {
      // Don't attempt to go past the end of the init list
      if (Index >= IList->getNumInits()) {
        CheckEmptyInitializable(ElementEntity, IList->getEndLoc());
        break;
      }

      ElementEntity.setElementIndex(Index);
      CheckSubElementType(ElementEntity, IList, elementType, Index,
                          StructuredList, StructuredIndex);
    }

    if (VerifyOnly)
      return;

    return;
  }

  InitializedEntity ElementEntity =
      InitializedEntity::InitializeElement(SemaRef.Context, 0, Entity);

  // Vectors can be constructed from vectors.
  for (unsigned i = 0; i < maxElements; ++i) {
    // Don't attempt to go past the end of the init list
    if (Index >= IList->getNumInits())
      break;

    ElementEntity.setElementIndex(Index);

    QualType IType = IList->getInit(Index)->getType();
    if (!IType->isVectorType()) {
      CheckSubElementType(ElementEntity, IList, elementType, Index,
                          StructuredList, StructuredIndex);
      ++numEltsInit;
    } else {
      QualType VecType;
      const VectorType *IVT = IType->castAs<VectorType>();
      unsigned numIElts = IVT->getNumElements();

      if (IType->isExtVectorType())
        VecType = SemaRef.Context.getExtVectorType(elementType, numIElts);
      else
        VecType = SemaRef.Context.getVectorType(elementType, numIElts,
                                                IVT->getVectorKind());
      CheckSubElementType(ElementEntity, IList, VecType, Index, StructuredList,
                          StructuredIndex);
      numEltsInit += numIElts;
    }
  }

  // All elements must be initialized.
  if (numEltsInit != maxElements) {
    if (!VerifyOnly)
      SemaRef.Diag(IList->getBeginLoc(),
                   diag::err_vector_incorrect_num_initializers)
          << (numEltsInit < maxElements) << maxElements << numEltsInit;
    hadError = true;
  }
}

void InitListChecker::CheckArrayType(
    const InitializedEntity &Entity, InitListExpr *IList, QualType &DeclType,
    llvm::APSInt elementIndex, bool SubobjectIsDesignatorContext,
    unsigned &Index, InitListExpr *StructuredList, unsigned &StructuredIndex) {
  const ArrayType *arrayType = SemaRef.Context.getAsArrayType(DeclType);

  // Check for the special-case of initializing an array with a string.
  if (Index < IList->getNumInits()) {
    if (isStringInit(IList->getInit(Index), arrayType, SemaRef.Context) ==
        SIF_None) {
      // We place the string literal directly into the resulting
      // initializer list. This is the only place where the structure
      // of the structured initializer list doesn't match exactly,
      // because doing so would involve allocating one character
      // constant for each string.
      if (!VerifyOnly)
        checkStringInit(IList->getInit(Index), DeclType, arrayType, SemaRef);
      if (StructuredList) {
        UpdateStructuredListElement(StructuredList, StructuredIndex,
                                    IList->getInit(Index));
        StructuredList->resizeInits(SemaRef.Context, StructuredIndex);
      }
      ++Index;
      if (AggrDeductionCandidateParamTypes)
        AggrDeductionCandidateParamTypes->push_back(DeclType);
      return;
    }
  }
  if (const VariableArrayType *VAT = dyn_cast<VariableArrayType>(arrayType)) {
    // Check for VLAs; in standard C it would be possible to check this
    // earlier, but I don't know where NeverC accepts VLAs (gcc accepts
    // them in all sorts of strange places).
    bool HasErr = IList->getNumInits() != 0;
    if (!VerifyOnly) {
      // VLA can only be initialized with `{}`.
      // Parser-side extension warnings are handled in ParseBraceInitializer().
      if (HasErr)
        SemaRef.Diag(VAT->getSizeExpr()->getBeginLoc(),
                     diag::err_variable_object_no_init)
            << VAT->getSizeExpr()->getSourceRange();
    }
    hadError = HasErr;
    ++Index;
    ++StructuredIndex;
    return;
  }

  // We might know the maximum number of elements in advance.
  llvm::APSInt maxElements(elementIndex.getBitWidth(),
                           elementIndex.isUnsigned());
  bool maxElementsKnown = false;
  if (const ConstantArrayType *CAT = dyn_cast<ConstantArrayType>(arrayType)) {
    maxElements = CAT->getSize();
    elementIndex = elementIndex.extOrTrunc(maxElements.getBitWidth());
    elementIndex.setIsUnsigned(maxElements.isUnsigned());
    maxElementsKnown = true;
  }

  QualType elementType = arrayType->getElementType();
  while (Index < IList->getNumInits()) {
    Expr *Init = IList->getInit(Index);
    if (DesignatedInitExpr *DIE = dyn_cast<DesignatedInitExpr>(Init)) {
      // If we're not the subobject that matches up with the '{' for
      // the designator, we shouldn't be handling the
      // designator. Return immediately.
      if (!SubobjectIsDesignatorContext)
        return;

      // Handle this designated initializer. elementIndex will be
      // updated to be the next array element we'll initialize.
      if (CheckDesignatedInitializer(Entity, IList, DIE, 0, DeclType, nullptr,
                                     &elementIndex, Index, StructuredList,
                                     StructuredIndex, true, false)) {
        hadError = true;
        continue;
      }

      if (elementIndex.getBitWidth() > maxElements.getBitWidth())
        maxElements = maxElements.extend(elementIndex.getBitWidth());
      else if (elementIndex.getBitWidth() < maxElements.getBitWidth())
        elementIndex = elementIndex.extend(maxElements.getBitWidth());
      elementIndex.setIsUnsigned(maxElements.isUnsigned());

      // If the array is of incomplete type, keep track of the number of
      // elements in the initializer.
      if (!maxElementsKnown && elementIndex > maxElements)
        maxElements = elementIndex;

      continue;
    }

    // If we know the maximum number of elements, and we've already
    // hit it, stop consuming elements in the initializer list.
    if (maxElementsKnown && elementIndex == maxElements)
      break;

    InitializedEntity ElementEntity = InitializedEntity::InitializeElement(
        SemaRef.Context, StructuredIndex, Entity);
    CheckSubElementType(ElementEntity, IList, elementType, Index,
                        StructuredList, StructuredIndex);
    ++elementIndex;

    // If the array is of incomplete type, keep track of the number of
    // elements in the initializer.
    if (!maxElementsKnown && elementIndex > maxElements)
      maxElements = elementIndex;
  }
  if (!hadError && DeclType->isIncompleteArrayType() && !VerifyOnly) {
    // If this is an incomplete array type, the actual type needs to
    // be calculated here.
    llvm::APSInt Zero(maxElements.getBitWidth(), maxElements.isUnsigned());
    if (maxElements == Zero) {
      SemaRef.Diag(IList->getBeginLoc(), diag::ext_typecheck_zero_array_size);
    }

    DeclType = SemaRef.Context.getConstantArrayType(
        elementType, maxElements, nullptr, ArraySizeModifier::Normal, 0);
  }
  if (!hadError) {
    // If there are any members of the array that get value-initialized, check
    // that is possible. That happens if we know the bound and don't have
    // enough elements, or if we're performing an array new with an unknown
    // bound.
    if (maxElementsKnown && elementIndex < maxElements)
      CheckEmptyInitializable(
          InitializedEntity::InitializeElement(SemaRef.Context, 0, Entity),
          IList->getEndLoc());
  }
}

bool InitListChecker::CheckFlexibleArrayInit(const InitializedEntity &Entity,
                                             Expr *InitExpr, FieldDecl *Field,
                                             bool TopLevelObject) {
  // Handle GNU flexible array initializers.
  unsigned FlexArrayDiag;
  if (isa<InitListExpr>(InitExpr) &&
      cast<InitListExpr>(InitExpr)->getNumInits() == 0) {
    // Empty flexible array init always allowed as an extension
    FlexArrayDiag = diag::ext_flexible_array_init;
  } else if (!TopLevelObject) {
    // Disallow flexible array init on non-top-level object
    FlexArrayDiag = diag::err_flexible_array_init;
  } else if (Entity.getKind() != InitializedEntity::EK_Variable) {
    // Disallow flexible array init on anything which is not a variable.
    FlexArrayDiag = diag::err_flexible_array_init;
  } else if (cast<VarDecl>(Entity.getDecl())->hasLocalStorage()) {
    // Disallow flexible array init on local variables.
    FlexArrayDiag = diag::err_flexible_array_init;
  } else {
    // Allow other cases.
    FlexArrayDiag = diag::ext_flexible_array_init;
  }

  if (!VerifyOnly) {
    SemaRef.Diag(InitExpr->getBeginLoc(), FlexArrayDiag)
        << InitExpr->getBeginLoc();
    SemaRef.Diag(Field->getLocation(), diag::note_flexible_array_member)
        << Field;
  }

  return FlexArrayDiag != diag::ext_flexible_array_init;
}

void InitListChecker::CheckStructUnionTypes(
    const InitializedEntity &Entity, InitListExpr *IList, QualType DeclType,
    RecordDecl::field_iterator Field, bool SubobjectIsDesignatorContext,
    unsigned &Index, InitListExpr *StructuredList, unsigned &StructuredIndex,
    bool TopLevelObject) {
  const RecordDecl *RD = getRecordDecl(DeclType);

  // If the record is invalid, some of it's members are invalid. To avoid
  // confusion, we forgo checking the initializer for the entire record.
  if (RD->isInvalidDecl()) {
    // Assume it was supposed to consume a single initializer.
    ++Index;
    hadError = true;
    return;
  }

  if (RD->isUnion() && IList->getNumInits() == 0) {

    // Value-initialize the first member of the union that isn't an unnamed
    // bitfield.
    for (RecordDecl::field_iterator FieldEnd = RD->field_end();
         Field != FieldEnd; ++Field) {
      if (!Field->isUnnamedBitfield()) {
        CheckEmptyInitializable(
            InitializedEntity::InitializeMember(*Field, &Entity),
            IList->getEndLoc());
        if (StructuredList)
          StructuredList->setInitializedFieldInUnion(*Field);
        break;
      }
    }
    return;
  }

  bool InitializedSomething = false;

  // If structDecl is a forward declaration, this loop won't do
  // anything except look at designated initializers; That's okay,
  // because an error should get printed out elsewhere. It might be
  // worthwhile to skip over the rest of the initializer, though.
  RecordDecl::field_iterator FieldEnd = RD->field_end();
  size_t NumRecordDecls = llvm::count_if(RD->decls(), [&](const Decl *D) {
    return isa<FieldDecl>(D) || isa<RecordDecl>(D);
  });
  bool CheckForMissingFields =
      !IList->isIdiomaticZeroInitializer(SemaRef.getLangOpts());
  bool HasDesignatedInit = false;

  llvm::SmallPtrSet<FieldDecl *, 4> InitializedFields;

  while (Index < IList->getNumInits()) {
    Expr *Init = IList->getInit(Index);
    SourceLocation InitLoc = Init->getBeginLoc();

    if (DesignatedInitExpr *DIE = dyn_cast<DesignatedInitExpr>(Init)) {
      // If we're not the subobject that matches up with the '{' for
      // the designator, we shouldn't be handling the
      // designator. Return immediately.
      if (!SubobjectIsDesignatorContext)
        return;

      HasDesignatedInit = true;

      // Handle this designated initializer. Field will be updated to
      // the next field that we'll be initializing.
      bool DesignatedInitFailed = CheckDesignatedInitializer(
          Entity, IList, DIE, 0, DeclType, &Field, nullptr, Index,
          StructuredList, StructuredIndex, true, TopLevelObject);
      if (DesignatedInitFailed)
        hadError = true;

      DesignatedInitExpr::Designator *D = DIE->getDesignator(0);
      if (!VerifyOnly && D->isFieldDesignator()) {
        FieldDecl *F = D->getFieldDecl();
        InitializedFields.insert(F);
      }

      InitializedSomething = true;

      // Disable check for missing fields when designators are used.
      // This matches gcc behaviour.
      CheckForMissingFields = false;
      continue;
    }

    // Check if this is an initializer of forms:
    //
    //   struct foo f = {};
    //   struct foo g = {0};
    //
    // These are okay for randomized structures.
    //
    // Also, if there is only one element in the structure, we allow something
    // like this, because it's really not randomized in the tranditional sense.
    //
    //   struct foo h = {bar};
    auto IsZeroInitializer = [&](const Expr *I) {
      if (IList->getNumInits() == 1) {
        if (NumRecordDecls == 1)
          return true;
        if (const auto *IL = dyn_cast<IntegerLiteral>(I))
          return IL->getValue().isZero();
      }
      return false;
    };
#ifndef _WIN32
    // Don't allow non-designated initializers on randomized structures.
    if (RD->isRandomized() && !IsZeroInitializer(Init)) {
      if (!VerifyOnly)
        SemaRef.Diag(InitLoc, diag::err_non_designated_init_used);
      hadError = true;
      break;
    }
#endif
    if (Field == FieldEnd) {
      // We've run out of fields. We're done.
      break;
    }

    // We've already initialized a member of a union. We're done.
    if (InitializedSomething && RD->isUnion())
      break;

    // If we've hit the flexible array member at the end, we're done.
    if (Field->getType()->isIncompleteArrayType())
      break;

    if (Field->isUnnamedBitfield()) {
      // Don't initialize unnamed bitfields, e.g. "int : 20;"
      ++Field;
      continue;
    }

    // Make sure we can use this declaration.
    bool InvalidUse;
    if (VerifyOnly)
      InvalidUse = !SemaRef.CanUseDecl(*Field, TreatUnavailableAsInvalid);
    else
      InvalidUse =
          SemaRef.CheckDeclUsage(*Field, IList->getInit(Index)->getBeginLoc());
    if (InvalidUse) {
      ++Index;
      ++Field;
      hadError = true;
      continue;
    }

    InitializedEntity MemberEntity =
        InitializedEntity::InitializeMember(*Field, &Entity);
    CheckSubElementType(MemberEntity, IList, Field->getType(), Index,
                        StructuredList, StructuredIndex);
    InitializedSomething = true;
    InitializedFields.insert(*Field);

    if (RD->isUnion() && StructuredList) {
      StructuredList->setInitializedFieldInUnion(*Field);
    }

    ++Field;
  }
  if (!VerifyOnly && InitializedSomething && CheckForMissingFields &&
      !RD->isUnion()) {
    // It is possible we have one or more unnamed bitfields remaining.
    // Find first (if any) named field and emit warning.
    for (RecordDecl::field_iterator it = HasDesignatedInit ? RD->field_begin()
                                                           : Field,
                                    end = RD->field_end();
         it != end; ++it) {
      if (HasDesignatedInit && InitializedFields.contains(*it))
        continue;

      if (!it->isUnnamedBitfield() && !it->getType()->isIncompleteArrayType()) {
        SemaRef.Diag(IList->getSourceRange().getEnd(),
                     diag::warn_missing_field_initializers)
            << *it;
        break;
      }
    }
  }

  // Check that any remaining fields can be value-initialized if we're not
  // building a structured list. (If we are, we'll check this later.)
  if (!StructuredList && Field != FieldEnd && !RD->isUnion() &&
      !Field->getType()->isIncompleteArrayType()) {
    for (; Field != FieldEnd && !hadError; ++Field) {
      if (!Field->isUnnamedBitfield())
        CheckEmptyInitializable(
            InitializedEntity::InitializeMember(*Field, &Entity),
            IList->getEndLoc());
    }
  }

  if (Field == FieldEnd || !Field->getType()->isIncompleteArrayType() ||
      Index >= IList->getNumInits())
    return;

  if (CheckFlexibleArrayInit(Entity, IList->getInit(Index), *Field,
                             TopLevelObject)) {
    hadError = true;
    ++Index;
    return;
  }

  InitializedEntity MemberEntity =
      InitializedEntity::InitializeMember(*Field, &Entity);

  if (isa<InitListExpr>(IList->getInit(Index)) ||
      AggrDeductionCandidateParamTypes)
    CheckSubElementType(MemberEntity, IList, Field->getType(), Index,
                        StructuredList, StructuredIndex);
  else
    CheckImplicitInitList(MemberEntity, IList, Field->getType(), Index,
                          StructuredList, StructuredIndex);
}

namespace {
void expandAnonymousFieldDesignator(Sema &SemaRef, DesignatedInitExpr *DIE,
                                    unsigned DesigIdx,
                                    IndirectFieldDecl *IndirectField) {
  typedef DesignatedInitExpr::Designator Designator;

  llvm::SmallVector<Designator, 4> Replacements;
  for (IndirectFieldDecl::chain_iterator PI = IndirectField->chain_begin(),
                                         PE = IndirectField->chain_end();
       PI != PE; ++PI) {
    if (PI + 1 == PE)
      Replacements.push_back(Designator::CreateFieldDesignator(
          (IdentifierInfo *)nullptr, DIE->getDesignator(DesigIdx)->getDotLoc(),
          DIE->getDesignator(DesigIdx)->getFieldLoc()));
    else
      Replacements.push_back(Designator::CreateFieldDesignator(
          (IdentifierInfo *)nullptr, SourceLocation(), SourceLocation()));
    assert(isa<FieldDecl>(*PI));
    Replacements.back().setFieldDecl(cast<FieldDecl>(*PI));
  }

  // Expand the current designator into the set of replacement
  // designators, so we have a full subobject path down to where the
  // member of the anonymous struct/union is actually stored.
  DIE->ExpandDesignator(SemaRef.Context, DesigIdx, &Replacements[0],
                        &Replacements[0] + Replacements.size());
}

DesignatedInitExpr *cloneDesignatedInitExpr(Sema &SemaRef,
                                            DesignatedInitExpr *DIE) {
  unsigned NumIndexExprs = DIE->getNumSubExprs() - 1;
  llvm::SmallVector<Expr *, 4> IndexExprs(NumIndexExprs);
  for (unsigned I = 0; I < NumIndexExprs; ++I)
    IndexExprs[I] = DIE->getSubExpr(I + 1);
  return DesignatedInitExpr::Create(SemaRef.Context, DIE->designators(),
                                    IndexExprs, DIE->getEqualOrColonLoc(),
                                    DIE->usesGNUSyntax(), DIE->getInit());
}
} // namespace

namespace {

// Callback to only accept typo corrections that are for field members of
// the given struct or union.
class FieldInitializerValidatorCCC final : public CorrectionCandidateCallback {
public:
  explicit FieldInitializerValidatorCCC(const RecordDecl *RD) : Record(RD) {}

  bool ValidateCandidate(const TypoCorrection &candidate) override {
    FieldDecl *FD = candidate.getCorrectionDeclAs<FieldDecl>();
    return FD && FD->getDeclContext()->getRedeclContext()->Equals(Record);
  }

  std::unique_ptr<CorrectionCandidateCallback> clone() override {
    return std::make_unique<FieldInitializerValidatorCCC>(*this);
  }

private:
  const RecordDecl *Record;
};

} // end anonymous namespace

bool InitListChecker::CheckDesignatedInitializer(
    const InitializedEntity &Entity, InitListExpr *IList,
    DesignatedInitExpr *DIE, unsigned DesigIdx, QualType &CurrentObjectType,
    RecordDecl::field_iterator *NextField, llvm::APSInt *NextElementIndex,
    unsigned &Index, InitListExpr *StructuredList, unsigned &StructuredIndex,
    bool FinishSubobjectInit, bool TopLevelObject) {
  if (DesigIdx == DIE->size()) {
    // Designated initialization may require direct-list-initialization of the
    // designated subobject; handle that before ordinary aggregate rules.
    if (DIE->isDirectInit()) {
      Expr *Init = DIE->getInit();
      assert(isa<InitListExpr>(Init) &&
             "designator result in direct non-list initialization?");
      InitializationKind Kind = InitializationKind::CreateDirectList(
          DIE->getBeginLoc(), Init->getBeginLoc(), Init->getEndLoc());
      InitializationSequence Seq(SemaRef, Entity, Kind, Init,
                                 /*TopLevelOfInitList*/ true);
      if (StructuredList) {
        ExprResult Result = VerifyOnly
                                ? getDummyInit()
                                : Seq.Perform(SemaRef, Entity, Kind, Init);
        UpdateStructuredListElement(StructuredList, StructuredIndex,
                                    Result.get());
      }
      ++Index;
      if (AggrDeductionCandidateParamTypes)
        AggrDeductionCandidateParamTypes->push_back(CurrentObjectType);
      return !Seq;
    }

    bool prevHadError = hadError;

    // Temporarily remove the designator expression from the
    // initializer list that the child calls see, so that we don't try
    // to re-process the designator.
    unsigned OldIndex = Index;
    IList->setInit(OldIndex, DIE->getInit());

    CheckSubElementType(Entity, IList, CurrentObjectType, Index, StructuredList,
                        StructuredIndex, /*DirectlyDesignated=*/true);

    // Restore the designated initializer expression in the syntactic
    // form of the initializer list.
    if (IList->getInit(OldIndex) != DIE->getInit())
      DIE->setInit(IList->getInit(OldIndex));
    IList->setInit(OldIndex, DIE);

    return hadError && !prevHadError;
  }

  DesignatedInitExpr::Designator *D = DIE->getDesignator(DesigIdx);
  bool IsFirstDesignator = (DesigIdx == 0);
  if (IsFirstDesignator ? FullyStructuredList : StructuredList) {
    if (IsFirstDesignator)
      StructuredList = FullyStructuredList;
    else {
      Expr *ExistingInit = StructuredIndex < StructuredList->getNumInits()
                               ? StructuredList->getInit(StructuredIndex)
                               : nullptr;
      if (!ExistingInit && StructuredList->hasArrayFiller())
        ExistingInit = StructuredList->getArrayFiller();

      if (!ExistingInit)
        StructuredList = getStructuredSubobjectInit(
            IList, Index, CurrentObjectType, StructuredList, StructuredIndex,
            SourceRange(D->getBeginLoc(), DIE->getEndLoc()));
      else if (InitListExpr *Result = dyn_cast<InitListExpr>(ExistingInit))
        StructuredList = Result;
      else {
        // We are creating an initializer list that initializes the
        // subobjects of the current object, but there was already an
        // initialization that completely initialized the current
        // subobject, e.g., by a compound literal:
        //
        // struct X { int a, b; };
        // struct X xs[] = { [0] = (struct X) { 1, 2 }, [0].b = 3 };
        //
        // Here, xs[0].a == 1 and xs[0].b == 3, since the second,
        // designated initializer re-initializes only its current object
        // subobject [0].b.
        diagnoseInitOverride(ExistingInit,
                             SourceRange(D->getBeginLoc(), DIE->getEndLoc()),
                             /*UnionOverride=*/false,
                             /*FullyOverwritten=*/false);

        if (!VerifyOnly) {
          if (DesignatedInitUpdateExpr *E =
                  dyn_cast<DesignatedInitUpdateExpr>(ExistingInit))
            StructuredList = E->getUpdater();
          else {
            DesignatedInitUpdateExpr *DIUE = new (SemaRef.Context)
                DesignatedInitUpdateExpr(SemaRef.Context, D->getBeginLoc(),
                                         ExistingInit, DIE->getEndLoc());
            StructuredList->updateInit(SemaRef.Context, StructuredIndex, DIUE);
            StructuredList = DIUE->getUpdater();
          }
        } else {
          // We don't need to track the structured representation of a
          // designated init update of an already-fully-initialized object in
          // verify-only mode. The only reason we would need the structure is
          // to determine where the uninitialized "holes" are, and in this
          // case, we know there aren't any and we can't introduce any.
          StructuredList = nullptr;
        }
      }
    }
  }

  if (D->isFieldDesignator()) {
    // Field designator (.member): requires struct/union context.
    RecordDecl *RD = getRecordDecl(CurrentObjectType);
    if (!RD) {
      SourceLocation Loc = D->getDotLoc();
      if (Loc.isInvalid())
        Loc = D->getFieldLoc();
      if (!VerifyOnly)
        SemaRef.Diag(Loc, diag::err_field_designator_non_aggr)
            << CurrentObjectType;
      ++Index;
      return true;
    }

    FieldDecl *KnownField = D->getFieldDecl();
    if (!KnownField) {
      const IdentifierInfo *FieldName = D->getFieldName();
      ValueDecl *VD = SemaRef.tryLookupUnambiguousFieldDecl(RD, FieldName);
      if (auto *FD = dyn_cast_if_present<FieldDecl>(VD)) {
        KnownField = FD;
      } else if (auto *IFD = dyn_cast_if_present<IndirectFieldDecl>(VD)) {
        // In verify mode, don't modify the original.
        if (VerifyOnly)
          DIE = cloneDesignatedInitExpr(SemaRef, DIE);
        expandAnonymousFieldDesignator(SemaRef, DIE, DesigIdx, IFD);
        D = DIE->getDesignator(DesigIdx);
        KnownField = cast<FieldDecl>(*IFD->chain_begin());
      }
      if (!KnownField) {
        if (VerifyOnly) {
          ++Index;
          return true; // No typo correction when just trying this out.
        }

        // Name lookup found something, but it wasn't a field.
        if (DeclContextLookupResult Lookup = RD->lookup(FieldName);
            !Lookup.empty()) {
          SemaRef.Diag(D->getFieldLoc(), diag::err_field_designator_nonfield)
              << FieldName;
          SemaRef.Diag(Lookup.front()->getLocation(),
                       diag::note_field_designator_found);
          ++Index;
          return true;
        }

        // Name lookup didn't find anything.
        FieldInitializerValidatorCCC CCC(RD);
        if (TypoCorrection Corrected = SemaRef.CorrectTypo(
                DeclarationNameInfo(FieldName, D->getFieldLoc()),
                neverc::ResolveMember, /*Scope=*/nullptr, CCC,
                Sema::CTK_ErrorRecovery, RD)) {
          SemaRef.diagnoseTypo(
              Corrected,
              SemaRef.PDiag(diag::err_field_designator_unknown_suggest)
                  << FieldName << CurrentObjectType);
          KnownField = Corrected.getCorrectionDeclAs<FieldDecl>();
          hadError = true;
        } else {
          // Typo correction didn't find anything.
          SourceLocation Loc = D->getFieldLoc();

          // The loc can be invalid with a "null" designator (i.e. an anonymous
          // union/struct). Do our best to approximate the location.
          if (Loc.isInvalid())
            Loc = IList->getBeginLoc();

          SemaRef.Diag(Loc, diag::err_field_designator_unknown)
              << FieldName << CurrentObjectType << DIE->getSourceRange();
          ++Index;
          return true;
        }
      }
    }

    unsigned FieldIndex = 0;

    for (auto *FI : RD->fields()) {
      if (FI->isUnnamedBitfield())
        continue;
      if (declaresSameEntity(KnownField, FI)) {
        KnownField = FI;
        break;
      }
      ++FieldIndex;
    }

    RecordDecl::field_iterator Field =
        RecordDecl::field_iterator(DeclContext::decl_iterator(KnownField));

    // All of the fields of a union are located at the same place in
    // the initializer list.
    if (RD->isUnion()) {
      FieldIndex = 0;
      if (StructuredList) {
        FieldDecl *CurrentField = StructuredList->getInitializedFieldInUnion();
        if (CurrentField && !declaresSameEntity(CurrentField, *Field)) {
          assert(StructuredList->getNumInits() == 1 &&
                 "A union should never have more than one initializer!");

          Expr *ExistingInit = StructuredList->getInit(0);
          if (ExistingInit) {
            // We're about to throw away an initializer, emit warning.
            diagnoseInitOverride(
                ExistingInit, SourceRange(D->getBeginLoc(), DIE->getEndLoc()),
                /*UnionOverride=*/true,
                /*FullyOverwritten=*/true);
          }

          // remove existing initializer
          StructuredList->resizeInits(SemaRef.Context, 0);
          StructuredList->setInitializedFieldInUnion(nullptr);
        }

        StructuredList->setInitializedFieldInUnion(*Field);
      }
    }

    // Make sure we can use this declaration.
    bool InvalidUse;
    if (VerifyOnly)
      InvalidUse = !SemaRef.CanUseDecl(*Field, TreatUnavailableAsInvalid);
    else
      InvalidUse = SemaRef.CheckDeclUsage(*Field, D->getFieldLoc());
    if (InvalidUse) {
      ++Index;
      return true;
    }

    // Designators must follow struct member order; this is checked
    // during actual initialization, not in VerifyOnly mode.
    //
    // Update the designator with the field declaration.
    if (!VerifyOnly)
      D->setFieldDecl(*Field);

    // Make sure that our non-designated initializer list has space
    // for a subobject corresponding to this field.
    if (StructuredList && FieldIndex >= StructuredList->getNumInits())
      StructuredList->resizeInits(SemaRef.Context, FieldIndex + 1);

    // This designator names a flexible array member.
    if (Field->getType()->isIncompleteArrayType()) {
      bool Invalid = false;
      if ((DesigIdx + 1) != DIE->size()) {
        // We can't designate an object within the flexible array
        // member (because GCC doesn't allow it).
        if (!VerifyOnly) {
          DesignatedInitExpr::Designator *NextD =
              DIE->getDesignator(DesigIdx + 1);
          SemaRef.Diag(NextD->getBeginLoc(),
                       diag::err_designator_into_flexible_array_member)
              << SourceRange(NextD->getBeginLoc(), DIE->getEndLoc());
          SemaRef.Diag(Field->getLocation(), diag::note_flexible_array_member)
              << *Field;
        }
        Invalid = true;
      }

      if (!hadError && !isa<InitListExpr>(DIE->getInit()) &&
          !isa<StringLiteral>(DIE->getInit())) {
        // The initializer is not an initializer list.
        if (!VerifyOnly) {
          SemaRef.Diag(DIE->getInit()->getBeginLoc(),
                       diag::err_flexible_array_init_needs_braces)
              << DIE->getInit()->getSourceRange();
          SemaRef.Diag(Field->getLocation(), diag::note_flexible_array_member)
              << *Field;
        }
        Invalid = true;
      }

      if (!Invalid && CheckFlexibleArrayInit(Entity, DIE->getInit(), *Field,
                                             TopLevelObject))
        Invalid = true;

      if (Invalid) {
        ++Index;
        return true;
      }
      bool prevHadError = hadError;
      unsigned newStructuredIndex = FieldIndex;
      unsigned OldIndex = Index;
      IList->setInit(Index, DIE->getInit());

      InitializedEntity MemberEntity =
          InitializedEntity::InitializeMember(*Field, &Entity);
      CheckSubElementType(MemberEntity, IList, Field->getType(), Index,
                          StructuredList, newStructuredIndex);

      IList->setInit(OldIndex, DIE);
      if (hadError && !prevHadError) {
        ++Field;
        ++FieldIndex;
        if (NextField)
          *NextField = Field;
        StructuredIndex = FieldIndex;
        return true;
      }
    } else {
      // Recurse to check later designated subobjects.
      QualType FieldType = Field->getType();
      unsigned newStructuredIndex = FieldIndex;

      InitializedEntity MemberEntity =
          InitializedEntity::InitializeMember(*Field, &Entity);
      if (CheckDesignatedInitializer(MemberEntity, IList, DIE, DesigIdx + 1,
                                     FieldType, nullptr, nullptr, Index,
                                     StructuredList, newStructuredIndex,
                                     FinishSubobjectInit, false))
        return true;
    }

    ++Field;
    ++FieldIndex;

    // If this the first designator, our caller will continue checking
    // the rest of this struct/union subobject.
    if (IsFirstDesignator) {
      if (Field != RD->field_end() && Field->isUnnamedBitfield())
        ++Field;

      if (NextField)
        *NextField = Field;

      StructuredIndex = FieldIndex;
      return false;
    }

    if (!FinishSubobjectInit)
      return false;

    // We've already initialized something in the union; we're done.
    if (RD->isUnion())
      return hadError;
    bool prevHadError = hadError;

    CheckStructUnionTypes(Entity, IList, CurrentObjectType, Field, false, Index,
                          StructuredList, FieldIndex);
    return hadError && !prevHadError;
  }

  // Array designator [expr]: requires array context. Also handles GNU range
  // designator [expr ... expr].
  const ArrayType *AT = SemaRef.Context.getAsArrayType(CurrentObjectType);
  if (!AT) {
    if (!VerifyOnly)
      SemaRef.Diag(D->getLBracketLoc(), diag::err_array_designator_non_array)
          << CurrentObjectType;
    ++Index;
    return true;
  }

  Expr *IndexExpr = nullptr;
  llvm::APSInt DesignatedStartIndex, DesignatedEndIndex;
  if (D->isArrayDesignator()) {
    IndexExpr = DIE->getArrayIndex(*D);
    DesignatedStartIndex = IndexExpr->EvaluateKnownConstInt(SemaRef.Context);
    DesignatedEndIndex = DesignatedStartIndex;
  } else {
    assert(D->isArrayRangeDesignator() && "Need array-range designator");

    DesignatedStartIndex =
        DIE->getArrayRangeStart(*D)->EvaluateKnownConstInt(SemaRef.Context);
    DesignatedEndIndex =
        DIE->getArrayRangeEnd(*D)->EvaluateKnownConstInt(SemaRef.Context);
    IndexExpr = DIE->getArrayRangeEnd(*D);

    // Codegen can't handle evaluating array range designators that have side
    // effects, because we replicate the AST value for each initialized element.
    // As such, set the sawArrayRangeDesignator() bit if we initialize multiple
    // elements with something that has a side effect, so codegen can emit an
    // "error unsupported" error instead of miscompiling the app.
    if (DesignatedStartIndex.getZExtValue() !=
            DesignatedEndIndex.getZExtValue() &&
        DIE->getInit()->HasSideEffects(SemaRef.Context) && !VerifyOnly)
      FullyStructuredList->sawArrayRangeDesignator();
  }

  if (isa<ConstantArrayType>(AT)) {
    llvm::APSInt MaxElements(cast<ConstantArrayType>(AT)->getSize(), false);
    DesignatedStartIndex =
        DesignatedStartIndex.extOrTrunc(MaxElements.getBitWidth());
    DesignatedStartIndex.setIsUnsigned(MaxElements.isUnsigned());
    DesignatedEndIndex =
        DesignatedEndIndex.extOrTrunc(MaxElements.getBitWidth());
    DesignatedEndIndex.setIsUnsigned(MaxElements.isUnsigned());
    if (DesignatedEndIndex >= MaxElements) {
      if (!VerifyOnly)
        SemaRef.Diag(IndexExpr->getBeginLoc(),
                     diag::err_array_designator_too_large)
            << toString(DesignatedEndIndex, 10) << toString(MaxElements, 10)
            << IndexExpr->getSourceRange();
      ++Index;
      return true;
    }
  } else {
    unsigned DesignatedIndexBitWidth =
        ConstantArrayType::getMaxSizeBits(SemaRef.Context);
    DesignatedStartIndex =
        DesignatedStartIndex.extOrTrunc(DesignatedIndexBitWidth);
    DesignatedEndIndex = DesignatedEndIndex.extOrTrunc(DesignatedIndexBitWidth);
    DesignatedStartIndex.setIsUnsigned(true);
    DesignatedEndIndex.setIsUnsigned(true);
  }

  bool IsStringLiteralInitUpdate =
      StructuredList && StructuredList->isStringLiteralInit();
  if (IsStringLiteralInitUpdate && VerifyOnly) {
    // We're just verifying an update to a string literal init. We don't need
    // to split the string up into individual characters to do that.
    StructuredList = nullptr;
  } else if (IsStringLiteralInitUpdate) {
    // We're modifying a string literal init; we have to decompose the string
    // so we can modify the individual characters.
    TreeContext &Context = SemaRef.Context;
    Expr *SubExpr = StructuredList->getInit(0)->IgnoreParenImpCasts();

    QualType CharTy = AT->getElementType();
    QualType PromotedCharTy = CharTy;
    if (Context.isPromotableIntegerType(CharTy))
      PromotedCharTy = Context.getPromotedIntegerType(CharTy);
    unsigned PromotedCharTyWidth = Context.getTypeSize(PromotedCharTy);

    if (StringLiteral *SL = dyn_cast<StringLiteral>(SubExpr)) {
      uint64_t StrLen = SL->getLength();
      if (cast<ConstantArrayType>(AT)->getSize().ult(StrLen))
        StrLen = cast<ConstantArrayType>(AT)->getSize().getZExtValue();
      StructuredList->resizeInits(Context, StrLen);

      for (unsigned i = 0, e = StrLen; i != e; ++i) {
        llvm::APInt CodeUnit(PromotedCharTyWidth, SL->getCodeUnit(i));
        Expr *Init = new (Context) IntegerLiteral(
            Context, CodeUnit, PromotedCharTy, SubExpr->getExprLoc());
        if (CharTy != PromotedCharTy)
          Init =
              ImplicitCastExpr::Create(Context, CharTy, CK_IntegralCast, Init,
                                       VK_PRValue, FPOptionsOverride());
        StructuredList->updateInit(Context, i, Init);
      }
    } else {
      llvm_unreachable("unsupported");
    }
  }

  // Make sure that our non-designated initializer list has space
  // for a subobject corresponding to this array element.
  if (StructuredList &&
      DesignatedEndIndex.getZExtValue() >= StructuredList->getNumInits())
    StructuredList->resizeInits(SemaRef.Context,
                                DesignatedEndIndex.getZExtValue() + 1);

  // Repeatedly perform subobject initializations in the range
  // [DesignatedStartIndex, DesignatedEndIndex].

  // Move to the next designator
  unsigned ElementIndex = DesignatedStartIndex.getZExtValue();
  unsigned OldIndex = Index;

  InitializedEntity ElementEntity =
      InitializedEntity::InitializeElement(SemaRef.Context, 0, Entity);

  while (DesignatedStartIndex <= DesignatedEndIndex) {
    // Recurse to check later designated subobjects.
    QualType ElementType = AT->getElementType();
    Index = OldIndex;

    ElementEntity.setElementIndex(ElementIndex);
    if (CheckDesignatedInitializer(
            ElementEntity, IList, DIE, DesigIdx + 1, ElementType, nullptr,
            nullptr, Index, StructuredList, ElementIndex,
            FinishSubobjectInit && (DesignatedStartIndex == DesignatedEndIndex),
            false))
      return true;

    // Move to the next index in the array that we'll be initializing.
    ++DesignatedStartIndex;
    ElementIndex = DesignatedStartIndex.getZExtValue();
  }

  // If this the first designator, our caller will continue checking
  // the rest of this array subobject.
  if (IsFirstDesignator) {
    if (NextElementIndex)
      *NextElementIndex = DesignatedStartIndex;
    StructuredIndex = ElementIndex;
    return false;
  }

  if (!FinishSubobjectInit)
    return false;
  bool prevHadError = hadError;
  CheckArrayType(Entity, IList, CurrentObjectType, DesignatedStartIndex,
                 /*SubobjectIsDesignatorContext=*/false, Index, StructuredList,
                 ElementIndex);
  return hadError && !prevHadError;
}

// Get the structured initializer list for a subobject of type
// @p CurrentObjectType.
InitListExpr *InitListChecker::getStructuredSubobjectInit(
    InitListExpr *IList, unsigned Index, QualType CurrentObjectType,
    InitListExpr *StructuredList, unsigned StructuredIndex,
    SourceRange InitRange, bool IsFullyOverwritten) {
  if (!StructuredList)
    return nullptr;

  Expr *ExistingInit = nullptr;
  if (StructuredIndex < StructuredList->getNumInits())
    ExistingInit = StructuredList->getInit(StructuredIndex);

  if (InitListExpr *Result = dyn_cast_or_null<InitListExpr>(ExistingInit))
    // A subsequent initializer list overwrites the entire subobject. e.g.,
    //
    // struct P { char x[6]; };
    // struct P l = { .x[2] = 'x', .x = { [0] = 'f' } };
    //
    // The first designated initializer is ignored, and l.x is just "f".
    if (!IsFullyOverwritten)
      return Result;

  if (ExistingInit) {
    // We are creating an initializer list that initializes the
    // subobjects of the current object, but there was already an
    // initialization that completely initialized the current
    // subobject:
    //
    // struct X { int a, b; };
    // struct X xs[] = { [0] = { 1, 2 }, [0].b = 3 };
    //
    // Here, xs[0].a == 1 and xs[0].b == 3, since the second,
    // designated initializer overwrites the [0].b initializer
    // from the prior initialization.
    //
    // When the existing initializer is an expression rather than an
    // initializer list, we cannot decompose and update it in this way.
    // For example:
    //
    // struct X xs[] = { [0] = (struct X) { 1, 2 }, [0].b = 3 };
    //
    // This case is handled by CheckDesignatedInitializer.
    diagnoseInitOverride(ExistingInit, InitRange);
  }

  unsigned ExpectedNumInits = 0;
  if (Index < IList->getNumInits()) {
    if (auto *Init = dyn_cast_or_null<InitListExpr>(IList->getInit(Index)))
      ExpectedNumInits = Init->getNumInits();
    else
      ExpectedNumInits = IList->getNumInits() - Index;
  }

  InitListExpr *Result =
      createInitListExpr(CurrentObjectType, InitRange, ExpectedNumInits);

  // Link this new initializer list into the structured initializer
  // lists.
  StructuredList->updateInit(SemaRef.Context, StructuredIndex, Result);
  return Result;
}

InitListExpr *InitListChecker::createInitListExpr(QualType CurrentObjectType,
                                                  SourceRange InitRange,
                                                  unsigned ExpectedNumInits) {
  InitListExpr *Result = new (SemaRef.Context) InitListExpr(
      SemaRef.Context, InitRange.getBegin(), std::nullopt, InitRange.getEnd());

  QualType ResultType = CurrentObjectType;
  if (!ResultType->isArrayType())
    ResultType = ResultType.getNonLValueExprType(SemaRef.Context);
  Result->setType(ResultType);

  // Pre-allocate storage for the structured initializer list.
  unsigned NumElements = 0;

  if (const ArrayType *AType =
          SemaRef.Context.getAsArrayType(CurrentObjectType)) {
    if (const ConstantArrayType *CAType = dyn_cast<ConstantArrayType>(AType)) {
      NumElements = CAType->getSize().getZExtValue();
      // Simple heuristic so that we don't allocate a very large
      // initializer with many empty entries at the end.
      if (NumElements > ExpectedNumInits)
        NumElements = 0;
    }
  } else if (const VectorType *VType = CurrentObjectType->getAs<VectorType>()) {
    NumElements = VType->getNumElements();
  } else if (CurrentObjectType->isRecordType()) {
    NumElements = numStructUnionElements(CurrentObjectType);
  }

  Result->reserveInits(SemaRef.Context, NumElements);

  return Result;
}

void InitListChecker::UpdateStructuredListElement(InitListExpr *StructuredList,
                                                  unsigned &StructuredIndex,
                                                  Expr *expr) {
  // No structured initializer list to update
  if (!StructuredList)
    return;

  if (Expr *PrevInit =
          StructuredList->updateInit(SemaRef.Context, StructuredIndex, expr)) {
    // This initializer overwrites a previous initializer.
    // No need to diagnose when `expr` is nullptr because a more relevant
    // diagnostic has already been issued and this diagnostic is potentially
    // noise.
    if (expr)
      diagnoseInitOverride(PrevInit, expr->getSourceRange());
  }

  ++StructuredIndex;
}

namespace {
ExprResult checkArrayDesignatorExpr(Sema &S, Expr *Index, llvm::APSInt &Value) {
  SourceLocation Loc = Index->getBeginLoc();

  // Make sure this is an integer constant expression.
  ExprResult Result =
      S.VerifyIntegerConstantExpression(Index, &Value, Sema::AllowFold);
  if (Result.isInvalid())
    return Result;

  if (Value.isSigned() && Value.isNegative())
    return S.Diag(Loc, diag::err_array_designator_negative)
           << toString(Value, 10) << Index->getSourceRange();

  Value.setIsUnsigned(true);
  return Result;
}
} // namespace

// ===----------------------------------------------------------------------===
// Designated initializers
// ===----------------------------------------------------------------------===

ExprResult Sema::OnDesignatedInitializer(Designation &Desig,
                                         SourceLocation EqualOrColonLoc,
                                         bool GNUSyntax, ExprResult Init) {
  typedef DesignatedInitExpr::Designator ASTDesignator;

  bool Invalid = false;
  llvm::SmallVector<ASTDesignator, 32> Designators;
  llvm::SmallVector<Expr *, 32> InitExpressions;

  for (unsigned Idx = 0; Idx < Desig.getNumDesignators(); ++Idx) {
    const Designator &D = Desig.getDesignator(Idx);

    if (D.isFieldDesignator()) {
      Designators.push_back(ASTDesignator::CreateFieldDesignator(
          D.getFieldDecl(), D.getDotLoc(), D.getFieldLoc()));
    } else if (D.isArrayDesignator()) {
      Expr *Index = static_cast<Expr *>(D.getArrayIndex());
      llvm::APSInt IndexValue;
      Index = checkArrayDesignatorExpr(*this, Index, IndexValue).get();
      if (!Index)
        Invalid = true;
      else {
        Designators.push_back(ASTDesignator::CreateArrayDesignator(
            InitExpressions.size(), D.getLBracketLoc(), D.getRBracketLoc()));
        InitExpressions.push_back(Index);
      }
    } else if (D.isArrayRangeDesignator()) {
      Expr *StartIndex = static_cast<Expr *>(D.getArrayRangeStart());
      Expr *EndIndex = static_cast<Expr *>(D.getArrayRangeEnd());
      llvm::APSInt StartValue;
      llvm::APSInt EndValue;
      StartIndex =
          checkArrayDesignatorExpr(*this, StartIndex, StartValue).get();
      EndIndex = checkArrayDesignatorExpr(*this, EndIndex, EndValue).get();

      if (!StartIndex || !EndIndex)
        Invalid = true;
      else {
        if (StartValue.getBitWidth() > EndValue.getBitWidth())
          EndValue = EndValue.extend(StartValue.getBitWidth());
        else if (StartValue.getBitWidth() < EndValue.getBitWidth())
          StartValue = StartValue.extend(EndValue.getBitWidth());

        if (EndValue < StartValue) {
          Diag(D.getEllipsisLoc(), diag::err_array_designator_empty_range)
              << toString(StartValue, 10) << toString(EndValue, 10)
              << StartIndex->getSourceRange() << EndIndex->getSourceRange();
          Invalid = true;
        } else {
          Designators.push_back(ASTDesignator::CreateArrayRangeDesignator(
              InitExpressions.size(), D.getLBracketLoc(), D.getEllipsisLoc(),
              D.getRBracketLoc()));
          InitExpressions.push_back(StartIndex);
          InitExpressions.push_back(EndIndex);
        }
      }
    }
  }

  if (Invalid || Init.isInvalid())
    return ExprError();

  return DesignatedInitExpr::Create(Context, Designators, InitExpressions,
                                    EqualOrColonLoc, GNUSyntax,
                                    Init.getAs<Expr>());
}

InitializedEntity::InitializedEntity(TreeContext &Context, unsigned Index,
                                     const InitializedEntity &Parent)
    : Parent(&Parent), Index(Index) {
  if (const ArrayType *AT = Context.getAsArrayType(Parent.getType())) {
    Kind = EK_ArrayElement;
    Type = AT->getElementType();
  } else if (const VectorType *VT = Parent.getType()->getAs<VectorType>()) {
    Kind = EK_VectorElement;
    Type = VT->getElementType();
  } else {
    const ComplexType *CT = Parent.getType()->getAs<ComplexType>();
    assert(CT && "Unexpected type");
    Kind = EK_ComplexElement;
    Type = CT->getElementType();
  }
}

DeclarationName InitializedEntity::getName() const {
  switch (getKind()) {
  case EK_Parameter: {
    ParmVarDecl *D = Parameter.getPointer();
    return (D ? D->getDeclName() : DeclarationName());
  }

  case EK_Variable:
  case EK_Member:
    return Variable.VariableOrMember->getDeclName();

  case EK_Result:
  case EK_StmtExprResult:
  case EK_Temporary:
  case EK_ArrayElement:
  case EK_VectorElement:
  case EK_ComplexElement:
  case EK_CompoundLiteralInit:
    return DeclarationName();
  }

  llvm_unreachable("Invalid EntityKind!");
}

ValueDecl *InitializedEntity::getDecl() const {
  switch (getKind()) {
  case EK_Variable:
  case EK_Member:
    return Variable.VariableOrMember;

  case EK_Parameter:
    return Parameter.getPointer();

  case EK_Result:
  case EK_StmtExprResult:
  case EK_Temporary:
  case EK_ArrayElement:
  case EK_VectorElement:
  case EK_ComplexElement:
  case EK_CompoundLiteralInit:
    return nullptr;
  }

  llvm_unreachable("Invalid EntityKind!");
}

bool InitializedEntity::allowsNRVO() const {
  switch (getKind()) {
  case EK_Result:
    return LocAndNRVO.NRVO;

  case EK_StmtExprResult:
  case EK_Variable:
  case EK_Parameter:
  case EK_Member:
  case EK_Temporary:
  case EK_CompoundLiteralInit:
  case EK_ArrayElement:
  case EK_VectorElement:
  case EK_ComplexElement:
    break;
  }

  return false;
}

unsigned InitializedEntity::dumpImpl(llvm::raw_ostream &OS) const {
  assert(getParent() != this);
  unsigned Depth = getParent() ? getParent()->dumpImpl(OS) : 0;
  for (unsigned I = 0; I != Depth; ++I)
    OS << "`-";

  switch (getKind()) {
  case EK_Variable:
    OS << "Variable";
    break;
  case EK_Parameter:
    OS << "Parameter";
    break;
  case EK_Result:
    OS << "Result";
    break;
  case EK_StmtExprResult:
    OS << "StmtExprResult";
    break;
  case EK_Member:
    OS << "Member";
    break;
  case EK_Temporary:
    OS << "Temporary";
    break;
  case EK_CompoundLiteralInit:
    OS << "CompoundLiteral";
    break;
  case EK_ArrayElement:
    OS << "ArrayElement " << Index;
    break;
  case EK_VectorElement:
    OS << "VectorElement " << Index;
    break;
  case EK_ComplexElement:
    OS << "ComplexElement " << Index;
    break;
  }

  if (auto *D = getDecl()) {
    OS << " ";
    D->printQualifiedName(OS);
  }

  OS << " '" << getType() << "'\n";

  return Depth + 1;
}

LLVM_DUMP_METHOD void InitializedEntity::dump() const {
  dumpImpl(llvm::errs());
}

void InitializationSequence::Step::Destroy() {
  switch (Kind) {
  case SK_ListInitialization:
  case SK_ZeroInitialization:
  case SK_CAssignment:
  case SK_StringInit:
  case SK_ArrayLoopIndex:
  case SK_ArrayLoopInit:
  case SK_ArrayInit:
  case SK_GNUArrayInit:
    break;
  }
}

bool InitializationSequence::isAmbiguous() const {
  if (!Failed())
    return false;

  switch (getFailureKind()) {
  case FK_ArrayNeedsInitList:
  case FK_ArrayNeedsInitListOrStringLiteral:
  case FK_ArrayNeedsInitListOrWideStringLiteral:
  case FK_NarrowStringIntoWideCharArray:
  case FK_WideStringIntoCharArray:
  case FK_IncompatWideStringIntoWideChar:
  case FK_PlainStringIntoUTF8Char:
  case FK_UTF8StringIntoPlainChar:
  case FK_ConversionFailed:
  case FK_TooManyInitsForScalar:
  case FK_InitListBadDestinationType:
  case FK_DefaultInitOfConst:
  case FK_Incomplete:
  case FK_ArrayTypeMismatch:
  case FK_NonConstantArrayInit:
  case FK_ListInitializationFailed:
  case FK_VariableLengthArrayHasInitializer:
  case FK_PlaceholderType:
  case FK_AddressOfUnaddressableFunction:
  case FK_DesignatedInitForNonAggregate:
    return false;
  }

  llvm_unreachable("Invalid EntityKind!");
}

void InitializationSequence::AddListInitializationStep(QualType T) {
  Step S;
  S.Kind = SK_ListInitialization;
  S.Type = T;
  Steps.push_back(S);
}

void InitializationSequence::AddZeroInitializationStep(QualType T) {
  Step S;
  S.Kind = SK_ZeroInitialization;
  S.Type = T;
  Steps.push_back(S);
}

void InitializationSequence::AddCAssignmentStep(QualType T) {
  Step S;
  S.Kind = SK_CAssignment;
  S.Type = T;
  Steps.push_back(S);
}

void InitializationSequence::AddStringInitStep(QualType T) {
  Step S;
  S.Kind = SK_StringInit;
  S.Type = T;
  Steps.push_back(S);
}

void InitializationSequence::AddArrayInitStep(QualType T, bool IsGNUExtension) {
  Step S;
  S.Kind = IsGNUExtension ? SK_GNUArrayInit : SK_ArrayInit;
  S.Type = T;
  Steps.push_back(S);
}

void InitializationSequence::AddArrayInitLoopStep(QualType T, QualType EltT) {
  Step S;
  S.Kind = SK_ArrayLoopIndex;
  S.Type = EltT;
  Steps.insert(Steps.begin(), S);

  S.Kind = SK_ArrayLoopInit;
  S.Type = T;
  Steps.push_back(S);
}

namespace {
void tryListInitialization(Sema &S, const InitializedEntity &Entity,
                           const InitializationKind &Kind,
                           InitListExpr *InitList,
                           InitializationSequence &Sequence,
                           bool TreatUnavailableAsInvalid);

void tryValueInitialization(Sema &S, const InitializedEntity &Entity,
                            const InitializationKind &Kind,
                            InitializationSequence &Sequence,
                            InitListExpr *InitList = nullptr);

void tryListInitialization(Sema &S, const InitializedEntity &Entity,
                           const InitializationKind &Kind,
                           InitListExpr *InitList,
                           InitializationSequence &Sequence,
                           bool TreatUnavailableAsInvalid) {
  QualType DestType = Entity.getType();

  if (DestType->isRecordType() &&
      !S.isCompleteType(InitList->getBeginLoc(), DestType)) {
    Sequence.setIncompleteTypeFailure(DestType);
    return;
  }

  // A braced list with designators requires an aggregate destination; we also
  // allow array types so array designators are covered.
  //
  // We follow other compilers in allowing things like 'Aggr &&a = {.x = 1};'
  // as a tentative DR resolution.
  bool IsDesignatedInit = InitList->hasDesignatedInit();
  if (!DestType->isAggregateType() && IsDesignatedInit) {
    Sequence.SetFailed(
        InitializationSequence::FK_DesignatedInitForNonAggregate);
    return;
  }

  InitListChecker CheckInitList(S, Entity, InitList, DestType,
                                /*VerifyOnly=*/true, TreatUnavailableAsInvalid);
  if (CheckInitList.HadError()) {
    Sequence.SetFailed(InitializationSequence::FK_ListInitializationFailed);
    return;
  }

  Sequence.AddListInitializationStep(DestType);
}

void tryStringLiteralInitialization(Sema &S, const InitializedEntity &Entity,
                                    const InitializationKind &Kind,
                                    Expr *Initializer,
                                    InitializationSequence &Sequence) {
  Sequence.AddStringInitStep(Entity.getType());
}

void tryValueInitialization(Sema &S, const InitializedEntity &Entity,
                            const InitializationKind &Kind,
                            InitializationSequence &Sequence,
                            InitListExpr *InitList) {
  assert((!InitList || InitList->getNumInits() == 0) &&
         "Shouldn't use value-init for non-empty init lists");

  // Value-initialization: for arrays, each element is value-initialized; we
  // unwrap to the base element type for the zero-init step.
  QualType T = Entity.getType();

  // Array: elements handled via zero-init of the whole array storage.
  T = S.Context.getBaseElementType(T);

  Sequence.AddZeroInitializationStep(Entity.getType());
}

void tryDefaultInitialization(Sema &S, const InitializedEntity &Entity,
                              const InitializationKind &Kind,
                              InitializationSequence &Sequence) {
  assert(Kind.getKind() == InitializationKind::IK_Default);
}

bool hasCompatibleArrayTypes(TreeContext &Context, const ArrayType *Dest,
                             const ArrayType *Source) {
  // If the source and destination array types are equivalent, we're
  // done.
  if (Context.hasSameType(QualType(Dest, 0), QualType(Source, 0)))
    return true;

  // Make sure that the element types are the same.
  if (!Context.hasSameType(Dest->getElementType(), Source->getElementType()))
    return false;

  // The only mismatch we allow is when the destination is an
  // incomplete array type and the source is a constant array type.
  return Source->isConstantArrayType() && Dest->isIncompleteArrayType();
}
} // namespace

InitializationSequence::InitializationSequence(Sema &S,
                                               const InitializedEntity &Entity,
                                               const InitializationKind &Kind,
                                               MultiExprArg Args,
                                               bool TopLevelOfInitList,
                                               bool TreatUnavailableAsInvalid) {
  InitializeFrom(S, Entity, Kind, Args, TopLevelOfInitList,
                 TreatUnavailableAsInvalid);
}

namespace {
bool canPerformArrayCopy(const InitializedEntity &Entity) {
  switch (Entity.getKind()) {
  case InitializedEntity::EK_Variable:
    return false;

  case InitializedEntity::EK_Member:
    return false;

  case InitializedEntity::EK_ArrayElement:
    // All the above cases are intended to apply recursively, even though none
    // of them actually say that.
    if (auto *E = Entity.getParent())
      return canPerformArrayCopy(*E);
    break;

  default:
    break;
  }

  return false;
}
} // namespace

void InitializationSequence::InitializeFrom(Sema &S,
                                            const InitializedEntity &Entity,
                                            const InitializationKind &Kind,
                                            MultiExprArg Args,
                                            bool TopLevelOfInitList,
                                            bool TreatUnavailableAsInvalid) {
  TreeContext &Context = S.Context;

  // Eliminate non-overload placeholder types in the arguments.  We
  // need to do this before checking whether types are dependent
  // because lowering a pseudo-object expression might well give us
  // something of dependent type.
  for (unsigned I = 0, E = Args.size(); I != E; ++I)
    if (Args[I]->getType()->isNonOverloadPlaceholderType()) {
      ExprResult result = S.CheckPlaceholderExpr(Args[I]);
      if (result.isInvalid()) {
        SetFailed(FK_PlaceholderType);
        return;
      }
      Args[I] = result.get();
    }

  // Destination type is the object or reference being initialized.
  QualType DestType = Entity.getType();

  // Almost everything is a normal sequence.
  setSequenceKind(NormalSequence);

  QualType SourceType;
  Expr *Initializer = nullptr;
  if (Args.size() == 1) {
    Initializer = Args[0];
    if (!isa<InitListExpr>(Initializer))
      SourceType = Initializer->getType();
  }

  //     - If the initializer is a (non-parenthesized) braced-init-list, the
  //       object is list-initialized (8.5.4).
  if (Kind.getKind() != InitializationKind::IK_Direct) {
    if (InitListExpr *InitList = dyn_cast_or_null<InitListExpr>(Initializer)) {
      tryListInitialization(S, Entity, Kind, InitList, *this,
                            TreatUnavailableAsInvalid);
      return;
    }
  }

  //     - If the destination type is a reference type, see 8.5.3.
  //     - If the initializer is (), the object is value-initialized.
  if (Kind.getKind() == InitializationKind::IK_Value ||
      (Kind.getKind() == InitializationKind::IK_Direct && Args.empty())) {
    tryValueInitialization(S, Entity, Kind, *this);
    return;
  }

  if (Kind.getKind() == InitializationKind::IK_Default) {
    tryDefaultInitialization(S, Entity, Kind, *this);
    return;
  }

  //     - If the destination type is an array of characters, an array of
  //       char16_t, an array of char32_t, or an array of wchar_t, and the
  //       initializer is a string literal, see 8.5.2.
  //     - Otherwise, if the destination type is an array, the program is
  //       ill-formed.
  if (const ArrayType *DestAT = Context.getAsArrayType(DestType)) {
    if (Initializer && isa<VariableArrayType>(DestAT)) {
      SetFailed(FK_VariableLengthArrayHasInitializer);
      return;
    }

    if (Initializer) {
      switch (isStringInit(Initializer, DestAT, Context)) {
      case SIF_None:
        tryStringLiteralInitialization(S, Entity, Kind, Initializer, *this);
        return;
      case SIF_NarrowStringIntoWideChar:
        SetFailed(FK_NarrowStringIntoWideCharArray);
        return;
      case SIF_WideStringIntoChar:
        SetFailed(FK_WideStringIntoCharArray);
        return;
      case SIF_IncompatWideStringIntoWideChar:
        SetFailed(FK_IncompatWideStringIntoWideChar);
        return;
      case SIF_PlainStringIntoUTF8Char:
        SetFailed(FK_PlainStringIntoUTF8Char);
        return;
      case SIF_UTF8StringIntoPlainChar:
        SetFailed(FK_UTF8StringIntoPlainChar);
        return;
      case SIF_Other:
        break;
      }
    }

    // Some kinds of initialization permit an array to be initialized from
    // another array of the same type, and perform elementwise initialization.
    if (Initializer && isa<ConstantArrayType>(DestAT) &&
        S.Context.hasSameUnqualifiedType(Initializer->getType(),
                                         Entity.getType()) &&
        canPerformArrayCopy(Entity)) {
      // If source is a prvalue, use it directly.
      if (Initializer->isPRValue()) {
        AddArrayInitStep(DestType, /*IsGNUExtension*/ false);
        return;
      }
      InitializedEntity Element =
          InitializedEntity::InitializeElement(S.Context, 0, Entity);
      QualType InitEltT =
          Context.getAsArrayType(Initializer->getType())->getElementType();
      OpaqueValueExpr OVE(Initializer->getExprLoc(), InitEltT,
                          Initializer->getValueKind(),
                          Initializer->getObjectKind());
      Expr *OVEAsExpr = &OVE;
      InitializeFrom(S, Element, Kind, OVEAsExpr, TopLevelOfInitList,
                     TreatUnavailableAsInvalid);
      if (!Failed())
        AddArrayInitLoopStep(Entity.getType(), InitEltT);
      return;
    }

    // Note: as an GNU C extension, we allow initialization of an
    // array from a compound literal that creates an array of the same
    // type, so long as the initializer has no side effects.
    if (Initializer && isa<CompoundLiteralExpr>(Initializer->IgnoreParens()) &&
        Initializer->getType()->isArrayType()) {
      const ArrayType *SourceAT =
          Context.getAsArrayType(Initializer->getType());
      if (!hasCompatibleArrayTypes(S.Context, DestAT, SourceAT))
        SetFailed(FK_ArrayTypeMismatch);
      else if (Initializer->HasSideEffects(S.Context))
        SetFailed(FK_NonConstantArrayInit);
      else {
        AddArrayInitStep(DestType, /*IsGNUExtension*/ true);
      }
    } else if (DestAT->getElementType()->isCharType())
      SetFailed(FK_ArrayNeedsInitListOrStringLiteral);
    else if (isWideCharCompatible(DestAT->getElementType(), Context))
      SetFailed(FK_ArrayNeedsInitListOrWideStringLiteral);
    else
      SetFailed(FK_ArrayNeedsInitList);

    return;
  }
  assert(Initializer && "Initializer must be non-null");
  AddCAssignmentStep(DestType);
}

InitializationSequence::~InitializationSequence() {
  for (auto &S : Steps)
    S.Destroy();
}

namespace {
Sema::AssignmentAction getAssignmentAction(const InitializedEntity &Entity,
                                           bool Diagnose = false) {
  switch (Entity.getKind()) {
  case InitializedEntity::EK_Variable:
  case InitializedEntity::EK_Member:
  case InitializedEntity::EK_ArrayElement:
  case InitializedEntity::EK_VectorElement:
  case InitializedEntity::EK_ComplexElement:
  case InitializedEntity::EK_CompoundLiteralInit:
    return Sema::AA_Initializing;

  case InitializedEntity::EK_Parameter:
    return Sema::AA_Passing;

  case InitializedEntity::EK_Result:
  case InitializedEntity::EK_StmtExprResult:
    return Sema::AA_Returning;

  case InitializedEntity::EK_Temporary:
    return Sema::AA_Casting;
  }

  llvm_unreachable("Invalid EntityKind!");
}

bool shouldBindAsTemporary(const InitializedEntity &Entity) {
  switch (Entity.getKind()) {
  case InitializedEntity::EK_ArrayElement:
  case InitializedEntity::EK_Member:
  case InitializedEntity::EK_Result:
  case InitializedEntity::EK_StmtExprResult:
  case InitializedEntity::EK_Variable:
  case InitializedEntity::EK_VectorElement:
  case InitializedEntity::EK_ComplexElement:
  case InitializedEntity::EK_CompoundLiteralInit:
    return false;

  case InitializedEntity::EK_Parameter:
  case InitializedEntity::EK_Temporary:
    return true;
  }

  llvm_unreachable("missed an InitializedEntity kind?");
}
} // namespace

void InitializationSequence::PrintInitLocationNote(
    Sema &S, const InitializedEntity &Entity) {
  if (Entity.isParameterKind() && Entity.getDecl()) {
    if (Entity.getDecl()->getLocation().isInvalid())
      return;

    if (Entity.getDecl()->getDeclName())
      S.Diag(Entity.getDecl()->getLocation(), diag::note_parameter_named_here)
          << Entity.getDecl()->getDeclName();
    else
      S.Diag(Entity.getDecl()->getLocation(), diag::note_parameter_here);
  }
}

namespace {
enum LifetimeKind {
  LK_FullExpression,

  LK_Extended,

  LK_Return,

  LK_StmtExprResult,
};
using LifetimeResult =
    llvm::PointerIntPair<const InitializedEntity *, 3, LifetimeKind>;
} // namespace

namespace {
LifetimeResult getEntityLifetime(const InitializedEntity *Entity,
                                 const InitializedEntity *InitField = nullptr) {
  // Reference / temporary lifetime classification.
  switch (Entity->getKind()) {
  case InitializedEntity::EK_Variable:
    //   The temporary [...] persists for the lifetime of the reference
    return {Entity, LK_Extended};

  case InitializedEntity::EK_Member:
    // For subobjects, we look at the complete object.
    if (Entity->getParent())
      return getEntityLifetime(Entity->getParent(), Entity);

    return {Entity, LK_Extended};

  case InitializedEntity::EK_Parameter:
    return {nullptr, LK_FullExpression};

  case InitializedEntity::EK_Result:
    //   -- The lifetime of a temporary bound to the returned value in a
    //      function return statement is not extended; the temporary is
    //      destroyed at the end of the full-expression in the return statement.
    return {nullptr, LK_Return};

  case InitializedEntity::EK_StmtExprResult:
    return {nullptr, LK_StmtExprResult};

  case InitializedEntity::EK_Temporary:
  case InitializedEntity::EK_CompoundLiteralInit:
    // Assume it's got full-expression duration for now, it will patch up our
    // storage duration if that's not correct.
    return {nullptr, LK_FullExpression};

  case InitializedEntity::EK_ArrayElement:
    // For subobjects, we look at the complete object.
    return getEntityLifetime(Entity->getParent(), InitField);

  case InitializedEntity::EK_VectorElement:
  case InitializedEntity::EK_ComplexElement:
    return {nullptr, LK_FullExpression};
  }

  llvm_unreachable("unknown entity kind");
}
} // namespace

namespace {
enum ReferenceKind {
  RK_ReferenceBinding,
};

using Local = Expr *;

struct IndirectLocalPathEntry {
  enum EntryKind {
    DefaultInit,
    AddressOf,
    VarInit,
    LValToRVal,
    TemporaryCopy
  } Kind;
  Expr *E;
  const Decl *D = nullptr;
  IndirectLocalPathEntry() {}
  IndirectLocalPathEntry(EntryKind K, Expr *E) : Kind(K), E(E) {}
  IndirectLocalPathEntry(EntryKind K, Expr *E, const Decl *D)
      : Kind(K), E(E), D(D) {}
};

using IndirectLocalPath = llvm::SmallVectorImpl<IndirectLocalPathEntry>;

struct RevertToOldSizeRAII {
  IndirectLocalPath &Path;
  unsigned OldSize = Path.size();
  RevertToOldSizeRAII(IndirectLocalPath &Path) : Path(Path) {}
  ~RevertToOldSizeRAII() { Path.resize(OldSize); }
};

using LocalVisitor = llvm::function_ref<bool(IndirectLocalPath &Path, Local L,
                                             ReferenceKind RK)>;
} // namespace

namespace {
bool isVarOnPath(IndirectLocalPath &Path, VarDecl *VD) {
  for (auto E : Path)
    if (E.Kind == IndirectLocalPathEntry::VarInit && E.D == VD)
      return true;
  return false;
}

void visitLocalsRetainedByInitializer(IndirectLocalPath &Path, Expr *Init,
                                      LocalVisitor Visit, bool RevisitSubinits);

void visitLocalsRetainedByReferenceBinding(IndirectLocalPath &Path, Expr *Init,
                                           ReferenceKind RK,
                                           LocalVisitor Visit);

void visitLocalsRetainedByReferenceBinding(IndirectLocalPath &Path, Expr *Init,
                                           ReferenceKind RK,
                                           LocalVisitor Visit) {
  RevertToOldSizeRAII RAII(Path);

  // Walk past any constructs which we can lifetime-extend across.
  Expr *Old;
  do {
    Old = Init;

    if (auto *FE = dyn_cast<FullExpr>(Init))
      Init = FE->getSubExpr();

    if (InitListExpr *ILE = dyn_cast<InitListExpr>(Init)) {
      // If this is just redundant braces around an initializer, step over it.
      if (ILE->isTransparent())
        Init = ILE->getInit(0);
    }

    // Step over any subobject adjustments; we may have a materialized
    // temporary inside them.
    Init = const_cast<Expr *>(Init->skipRValueSubobjectAdjustments());

    // Look through casts to reference type when performing lifetime extension.
    if (CastExpr *CE = dyn_cast<CastExpr>(Init))
      if (CE->getSubExpr()->isLValue())
        Init = CE->getSubExpr();

    // Look through array element access on array glvalues when performing
    // lifetime extension.
    if (auto *ASE = dyn_cast<ArraySubscriptExpr>(Init)) {
      Init = ASE->getBase();
      auto *ICE = dyn_cast<ImplicitCastExpr>(Init);
      if (ICE && ICE->getCastKind() == CK_ArrayToPointerDecay)
        Init = ICE->getSubExpr();
      else
        // We can't lifetime extend through this but we might still find some
        // retained temporaries.
        return visitLocalsRetainedByInitializer(Path, Init, Visit, true);
    }

  } while (Init != Old);

  switch (Init->getStmtClass()) {
  case Stmt::DeclRefExprClass: {
    // If we find the name of a local non-reference parameter, we could have a
    // lifetime problem.
    auto *DRE = cast<DeclRefExpr>(Init);
    auto *VD = dyn_cast<VarDecl>(DRE->getDecl());
    if (VD && VD->hasLocalStorage()) {
      Visit(Path, Local(DRE), RK);
    }
    break;
  }

  case Stmt::UnaryOperatorClass: {
    // The only unary operator that make sense to handle here
    // is Deref.  All others don't resolve to a "name."  This includes
    // handling all sorts of rvalues passed to a unary operator.
    const UnaryOperator *U = cast<UnaryOperator>(Init);
    if (U->getOpcode() == UO_Deref)
      visitLocalsRetainedByInitializer(Path, U->getSubExpr(), Visit, true);
    break;
  }

  case Stmt::ConditionalOperatorClass:
  case Stmt::BinaryConditionalOperatorClass: {
    auto *C = cast<AbstractConditionalOperator>(Init);
    if (!C->getTrueExpr()->getType()->isVoidType())
      visitLocalsRetainedByReferenceBinding(Path, C->getTrueExpr(), RK, Visit);
    if (!C->getFalseExpr()->getType()->isVoidType())
      visitLocalsRetainedByReferenceBinding(Path, C->getFalseExpr(), RK, Visit);
    break;
  }

  default:
    break;
  }
}

void visitLocalsRetainedByInitializer(IndirectLocalPath &Path, Expr *Init,
                                      LocalVisitor Visit,
                                      bool RevisitSubinits) {
  RevertToOldSizeRAII RAII(Path);

  Expr *Old;
  do {
    Old = Init;

    if (auto *FE = dyn_cast<FullExpr>(Init))
      Init = FE->getSubExpr();

    Init = const_cast<Expr *>(Init->skipRValueSubobjectAdjustments());
    Init = Init->IgnoreParens();

    // Step over value-preserving rvalue casts.
    if (auto *CE = dyn_cast<CastExpr>(Init)) {
      switch (CE->getCastKind()) {
      case CK_LValueToRValue:
        // If we can match the lvalue to a const object, we can look at its
        // initializer.
        Path.push_back({IndirectLocalPathEntry::LValToRVal, CE});
        return visitLocalsRetainedByReferenceBinding(
            Path, Init, RK_ReferenceBinding,
            [&](IndirectLocalPath &Path, Local L, ReferenceKind RK) -> bool {
              if (auto *DRE = dyn_cast<DeclRefExpr>(L)) {
                auto *VD = dyn_cast<VarDecl>(DRE->getDecl());
                if (VD && VD->getType().isConstQualified() && VD->getInit() &&
                    !isVarOnPath(Path, VD)) {
                  Path.push_back({IndirectLocalPathEntry::VarInit, DRE, VD});
                  visitLocalsRetainedByInitializer(Path, VD->getInit(), Visit,
                                                   true);
                }
              }
              return false;
            });

        // We assume that objects can be retained by pointers cast to integers,
        // but not if the integer is cast to floating-point type or to _Complex.
        // We assume that casts to 'bool' do not preserve enough information to
        // retain a local object.
      case CK_NoOp:
      case CK_BitCast:
      case CK_ToUnion:
      case CK_IntegralToPointer:
      case CK_PointerToIntegral:
      case CK_VectorSplat:
      case CK_IntegralCast:
      case CK_AddressSpaceConversion:
        break;

      case CK_ArrayToPointerDecay:
        // Model array-to-pointer decay as taking the address of the array
        // lvalue.
        Path.push_back({IndirectLocalPathEntry::AddressOf, CE});
        return visitLocalsRetainedByReferenceBinding(
            Path, CE->getSubExpr(), RK_ReferenceBinding, Visit);

      default:
        return;
      }

      Init = CE->getSubExpr();
    }
  } while (Old != Init);

  if (InitListExpr *ILE = dyn_cast<InitListExpr>(Init)) {
    // We already visited the elements of this initializer list while
    // performing the initialization. Don't visit them again unless we've
    // changed the lifetime of the initialized entity.
    if (!RevisitSubinits)
      return;

    if (ILE->isTransparent())
      return visitLocalsRetainedByInitializer(Path, ILE->getInit(0), Visit,
                                              RevisitSubinits);

    if (ILE->getType()->isArrayType()) {
      for (unsigned I = 0, N = ILE->getNumInits(); I != N; ++I)
        visitLocalsRetainedByInitializer(Path, ILE->getInit(I), Visit,
                                         RevisitSubinits);
      return;
    }

    return;
  }

  switch (Init->getStmtClass()) {
  case Stmt::UnaryOperatorClass: {
    auto *UO = cast<UnaryOperator>(Init);
    // If the initializer is the address of a local, we could have a lifetime
    // problem.
    if (UO->getOpcode() == UO_AddrOf) {
      // If this is &rvalue, then it's ill-formed and we have already diagnosed
      // it. Don't produce a redundant warning about the lifetime of the
      // temporary.
      Path.push_back({IndirectLocalPathEntry::AddressOf, UO});
      visitLocalsRetainedByReferenceBinding(Path, UO->getSubExpr(),
                                            RK_ReferenceBinding, Visit);
    }
    break;
  }

  case Stmt::BinaryOperatorClass: {
    auto *BO = cast<BinaryOperator>(Init);
    BinaryOperatorKind BOK = BO->getOpcode();
    if (!BO->getType()->isPointerType() || (BOK != BO_Add && BOK != BO_Sub))
      break;

    if (BO->getLHS()->getType()->isPointerType())
      visitLocalsRetainedByInitializer(Path, BO->getLHS(), Visit, true);
    else if (BO->getRHS()->getType()->isPointerType())
      visitLocalsRetainedByInitializer(Path, BO->getRHS(), Visit, true);
    break;
  }

  case Stmt::ConditionalOperatorClass:
  case Stmt::BinaryConditionalOperatorClass: {
    auto *C = cast<AbstractConditionalOperator>(Init);
    // Void operands are not interesting for lifetime analysis.
    if (!C->getTrueExpr()->getType()->isVoidType())
      visitLocalsRetainedByInitializer(Path, C->getTrueExpr(), Visit, true);
    if (!C->getFalseExpr()->getType()->isVoidType())
      visitLocalsRetainedByInitializer(Path, C->getFalseExpr(), Visit, true);
    break;
  }

  case Stmt::AddrLabelExprClass:
    // We want to warn if the address of a label would escape the function.
    Visit(Path, Local(cast<AddrLabelExpr>(Init)), RK_ReferenceBinding);
    break;

  default:
    break;
  }
}

SourceRange nextPathEntryRange(const IndirectLocalPath &Path, unsigned I,
                               Expr *E) {
  for (unsigned N = Path.size(); I != N; ++I) {
    switch (Path[I].Kind) {
    case IndirectLocalPathEntry::AddressOf:
    case IndirectLocalPathEntry::LValToRVal:
    case IndirectLocalPathEntry::TemporaryCopy:
      // These exist primarily to mark the path as not permitting or
      // supporting lifetime extension.
      break;

    case IndirectLocalPathEntry::VarInit:
      if (cast<VarDecl>(Path[I].D)->isImplicit())
        return SourceRange();
      [[fallthrough]];
    case IndirectLocalPathEntry::DefaultInit:
      return Path[I].E->getSourceRange();
    }
  }
  return E->getSourceRange();
}
} // namespace

// ===----------------------------------------------------------------------===
// Lifetime checking & copy initialization
// ===----------------------------------------------------------------------===

void Sema::checkInitializerLifetime(const InitializedEntity &Entity,
                                    Expr *Init) {
  LifetimeResult LR = getEntityLifetime(&Entity);
  LifetimeKind LK = LR.getInt();

  // If this entity doesn't have an interesting lifetime, don't bother looking
  // for temporaries within its initializer.
  if (LK == LK_FullExpression)
    return;

  auto TemporaryVisitor = [&](IndirectLocalPath &Path, Local L,
                              ReferenceKind RK) -> bool {
    SourceRange DiagRange = nextPathEntryRange(Path, 0, L);
    SourceLocation DiagLoc = DiagRange.getBegin();

    switch (LK) {
    case LK_FullExpression:
      llvm_unreachable("already handled this");

    case LK_Extended:
      return false;

    case LK_Return:
    case LK_StmtExprResult:
      if (auto *DRE = dyn_cast<DeclRefExpr>(L)) {
        // We can't determine if the local variable outlives the statement
        // expression.
        if (LK == LK_StmtExprResult)
          return false;
        Diag(DiagLoc, diag::warn_ret_stack_addr_ref)
            << false << DRE->getDecl() << isa<ParmVarDecl>(DRE->getDecl())
            << DiagRange;
      } else if (isa<AddrLabelExpr>(L)) {
        // Don't warn when returning a label from a statement expression.
        // Leaving the scope doesn't end its lifetime.
        if (LK == LK_StmtExprResult)
          return false;
        Diag(DiagLoc, diag::warn_ret_addr_label) << DiagRange;
      } else {
        Diag(DiagLoc, diag::warn_ret_local_temp_addr_ref) << false << DiagRange;
      }
      break;
    }

    for (unsigned I = 0; I != Path.size(); ++I) {
      auto Elem = Path[I];

      switch (Elem.Kind) {
      case IndirectLocalPathEntry::AddressOf:
      case IndirectLocalPathEntry::LValToRVal:
        // These exist primarily to mark the path as not permitting or
        // supporting lifetime extension.
        break;

      case IndirectLocalPathEntry::TemporaryCopy:
        break;

      case IndirectLocalPathEntry::DefaultInit: {
        auto *FD = cast<FieldDecl>(Elem.D);
        Diag(FD->getLocation(), diag::note_init_with_default_member_initializer)
            << FD << nextPathEntryRange(Path, I + 1, L);
        break;
      }

      case IndirectLocalPathEntry::VarInit: {
        const VarDecl *VD = cast<VarDecl>(Elem.D);
        Diag(VD->getLocation(), diag::note_local_var_initializer)
            << false << VD->isImplicit() << VD->getDeclName()
            << nextPathEntryRange(Path, I + 1, L);
        break;
      }
      }
    }

    // We didn't lifetime-extend, so don't go any further; we don't need more
    // warnings or errors on inner temporaries within this one's initializer.
    return false;
  };

  llvm::SmallVector<IndirectLocalPathEntry, 8> Path;
  if (Init->isLValue())
    visitLocalsRetainedByReferenceBinding(Path, Init, RK_ReferenceBinding,
                                          TemporaryVisitor);
  else
    visitLocalsRetainedByInitializer(Path, Init, TemporaryVisitor, false);
}

ExprResult InitializationSequence::Perform(Sema &S,
                                           const InitializedEntity &Entity,
                                           const InitializationKind &Kind,
                                           MultiExprArg Args,
                                           QualType *ResultType) {
  if (Failed()) {
    Diagnose(S, Entity, Kind, Args);
    return ExprError();
  }
  if (!ZeroInitializationFixit.empty()) {
    const Decl *D = Entity.getDecl();
    const auto *VD = dyn_cast_or_null<VarDecl>(D);
    QualType DestType = Entity.getType();

    // The initialization would have succeeded with this fixit. Since the fixit
    // is on the error, we need to build a valid AST in this case, so this isn't
    // handled in the Failed() branch above.
    if (!DestType->isRecordType() && VD && VD->isConstexpr()) {
      // Use a more useful diagnostic for constexpr variables.
      S.Diag(Kind.getLocation(), diag::err_constexpr_var_requires_const_init)
          << VD
          << FixItHint::CreateInsertion(ZeroInitializationFixitLoc,
                                        ZeroInitializationFixit);
    } else {
      unsigned DiagID = diag::err_default_init_const;
      if (S.getLangOpts().MSVCCompat && D && D->hasAttr<SelectAnyAttr>())
        DiagID = diag::ext_default_init_const;

      S.Diag(Kind.getLocation(), DiagID)
          << DestType
          << FixItHint::CreateInsertion(ZeroInitializationFixitLoc,
                                        ZeroInitializationFixit);
    }
  }

  if (getKind() == DependentSequence) {
    if (ResultType && Args.size() == 1) {
      QualType DeclType = Entity.getType();
      if (S.Context.getAsIncompleteArrayType(DeclType)) {
        // We don't currently have the ability to accurately
        // compute the length of an initializer list without
        // performing full type-checking of the initializer list
        // (since we have to determine where braces are implicitly
        // introduced and such).  So, we fall back to making the array
        // type a dependently-sized array type with no specified
        // bound.
        if (isa<InitListExpr>((Expr *)Args[0])) {
          SourceRange Brackets;

          // Scavange the location of the brackets from the entity, if we can.
          if (auto *DD = dyn_cast_or_null<DeclaratorDecl>(Entity.getDecl())) {
            if (TypeSourceInfo *TInfo = DD->getTypeSourceInfo()) {
              TypeLoc TL = TInfo->getTypeLoc();
              if (IncompleteArrayTypeLoc ArrayLoc =
                      TL.getAs<IncompleteArrayTypeLoc>())
                Brackets = ArrayLoc.getBracketsRange();
            }
          }
        }
      }
    }
    if (Kind.getKind() == InitializationKind::IK_Direct &&
        !Kind.isExplicitCast()) {
      // Rebuild the ParenListExpr.
      SourceRange ParenRange = Kind.getParenOrBraceRange();
      return S.OnParenListExpr(ParenRange.getBegin(), ParenRange.getEnd(),
                               Args);
    }
    assert(Kind.getKind() == InitializationKind::IK_Copy ||
           Kind.isExplicitCast() ||
           Kind.getKind() == InitializationKind::IK_DirectList);
    return ExprResult(Args[0]);
  }

  // No steps means no initialization.
  if (Steps.empty())
    return ExprResult((Expr *)nullptr);

  if (S.getLangOpts().MicrosoftExt && Args.size() == 1 &&
      isa<PredefinedExpr>(Args[0]) && Entity.getType()->isArrayType()) {
    // Produce a Microsoft compatibility warning when initializing from a
    // predefined expression since MSVC treats predefined expressions as string
    // literals.
    Expr *Init = Args[0];
    S.Diag(Init->getBeginLoc(), diag::ext_init_from_predefined) << Init;
  }

  // Hack around the fact that Entity.getType() is not
  // the same as Entity.getDecl()->getType() in cases involving type merging,
  //  and we want latter when it makes sense.
  if (ResultType)
    *ResultType =
        Entity.getDecl() ? Entity.getDecl()->getType() : Entity.getType();

  ExprResult CurInit((Expr *)nullptr);
  llvm::SmallVector<Expr *, 4> ArrayLoopCommonExprs;

  // For initialization steps that start with a single initializer,
  // grab the only argument out the Args and place it into the "current"
  // initializer.
  switch (Steps.front().Kind) {
  case SK_ListInitialization:
  case SK_CAssignment:
  case SK_StringInit:
  case SK_ArrayLoopIndex:
  case SK_ArrayLoopInit:
  case SK_ArrayInit:
  case SK_GNUArrayInit: {
    assert(Args.size() == 1);
    CurInit = Args[0];
    if (!CurInit.get())
      return ExprError();
    break;
  }

  case SK_ZeroInitialization:
    break;
  }

  // Walk through the computed steps for the initialization sequence,
  // performing the specified conversions along the way.
  for (step_iterator Step = step_begin(), StepEnd = step_end(); Step != StepEnd;
       ++Step) {
    if (CurInit.isInvalid())
      return ExprError();

    switch (Step->Kind) {
    case SK_ListInitialization: {
      InitListExpr *InitList = cast<InitListExpr>(CurInit.get());
      // If we're not initializing the top-level entity, we need to create an
      // InitializeTemporary entity for our target type.
      QualType Ty = Step->Type;
      bool IsTemporary = !S.Context.hasSameType(Entity.getType(), Ty);
      InitializedEntity TempEntity = InitializedEntity::InitializeTemporary(Ty);
      InitializedEntity InitEntity = IsTemporary ? TempEntity : Entity;
      InitListChecker PerformInitList(S, InitEntity, InitList, Ty,
                                      /*VerifyOnly=*/false,
                                      /*TreatUnavailableAsInvalid=*/false);
      if (PerformInitList.HadError())
        return ExprError();

      // Hack: We must update *ResultType if available in order to set the
      // bounds of arrays, e.g. in 'int ar[] = {1, 2, 3};'.
      if (ResultType && (*ResultType)->isIncompleteArrayType()) {
        *ResultType = Ty;
      }

      InitListExpr *StructuredInitList =
          PerformInitList.getFullyStructuredList();
      CurInit.get();
      CurInit = shouldBindAsTemporary(InitEntity)
                    ? S.MaybeBindToTemporary(StructuredInitList)
                    : StructuredInitList;
      break;
    }

    case SK_ZeroInitialization: {
      CurInit = new (S.Context) ImplicitValueInitExpr(Step->Type);
      break;
    }

    case SK_CAssignment: {
      QualType SourceType = CurInit.get()->getType();

      // Save off the initial CurInit in case we need to emit a diagnostic
      ExprResult InitialCurInit = CurInit;
      ExprResult Result = CurInit;
      Sema::AssignConvertType ConvTy =
          S.CheckSingleAssignmentConstraints(Step->Type, Result);
      if (Result.isInvalid())
        return ExprError();
      CurInit = Result;

      // If this is a call, allow conversion to a transparent union.
      ExprResult CurInitExprRes = CurInit;
      if (ConvTy != Sema::Compatible && Entity.isParameterKind() &&
          S.CheckTransparentUnionArgumentConstraints(
              Step->Type, CurInitExprRes) == Sema::Compatible)
        ConvTy = Sema::Compatible;
      if (CurInitExprRes.isInvalid())
        return ExprError();
      CurInit = CurInitExprRes;

      bool Complained;
      if (S.DiagnoseAssignmentResult(ConvTy, Kind.getLocation(), Step->Type,
                                     SourceType, InitialCurInit.get(),
                                     getAssignmentAction(Entity, true),
                                     &Complained)) {
        PrintInitLocationNote(S, Entity);
        return ExprError();
      } else if (Complained)
        PrintInitLocationNote(S, Entity);
      break;
    }

    case SK_StringInit: {
      QualType Ty = Step->Type;
      bool UpdateType = ResultType && Entity.getType()->isIncompleteArrayType();
      checkStringInit(CurInit.get(), UpdateType ? *ResultType : Ty,
                      S.Context.getAsArrayType(Ty), S);
      break;
    }

    case SK_ArrayLoopIndex: {
      Expr *Cur = CurInit.get();
      Expr *BaseExpr = new (S.Context)
          OpaqueValueExpr(Cur->getExprLoc(), Cur->getType(),
                          Cur->getValueKind(), Cur->getObjectKind(), Cur);
      Expr *IndexExpr =
          new (S.Context) ArrayInitIndexExpr(S.Context.getSizeType());
      CurInit = S.CreateBuiltinArraySubscriptExpr(
          BaseExpr, Kind.getLocation(), IndexExpr, Kind.getLocation());
      ArrayLoopCommonExprs.push_back(BaseExpr);
      break;
    }

    case SK_ArrayLoopInit: {
      assert(!ArrayLoopCommonExprs.empty() &&
             "mismatched SK_ArrayLoopIndex and SK_ArrayLoopInit");
      Expr *Common = ArrayLoopCommonExprs.pop_back_val();
      CurInit =
          new (S.Context) ArrayInitLoopExpr(Step->Type, Common, CurInit.get());
      break;
    }

    case SK_GNUArrayInit:
      // Okay: we checked everything before creating this step. Note that
      // this is a GNU extension.
      S.Diag(Kind.getLocation(), diag::ext_array_init_copy)
          << Step->Type << CurInit.get()->getType()
          << CurInit.get()->getSourceRange();
      updateGNUCompoundLiteralRValue(CurInit.get());
      [[fallthrough]];
    case SK_ArrayInit:
      // If the destination type is an incomplete array type, update the
      // type accordingly.
      if (ResultType) {
        if (const IncompleteArrayType *IncompleteDest =
                S.Context.getAsIncompleteArrayType(Step->Type)) {
          if (const ConstantArrayType *ConstantSource =
                  S.Context.getAsConstantArrayType(CurInit.get()->getType())) {
            *ResultType = S.Context.getConstantArrayType(
                IncompleteDest->getElementType(), ConstantSource->getSize(),
                ConstantSource->getSizeExpr(), ArraySizeModifier::Normal, 0);
          }
        }
      }
      break;
    }
  }

  Expr *Init = CurInit.get();
  if (!Init)
    return ExprError();

  // Check whether the initializer has a shorter lifetime than the initialized
  // entity, and if not, either lifetime-extend or warn as appropriate.
  S.checkInitializerLifetime(Entity, Init);

  // Diagnose non-fatal problems with the completed initialization.
  if (InitializedEntity::EntityKind EK = Entity.getKind();
      (EK == InitializedEntity::EK_Member) &&
      cast<FieldDecl>(Entity.getDecl())->isBitField())
    S.CheckBitFieldInitialization(Kind.getLocation(),
                                  cast<FieldDecl>(Entity.getDecl()), Init);

  return Init;
}

namespace {
void diagnoseListInit(Sema &S, const InitializedEntity &Entity,
                      InitListExpr *InitList) {
  QualType DestType = Entity.getType();
  InitListChecker DiagnoseInitList(S, Entity, InitList, DestType,
                                   /*VerifyOnly=*/false,
                                   /*TreatUnavailableAsInvalid=*/false);
  assert(DiagnoseInitList.HadError() && "Inconsistent init list check result.");
}
} // namespace

bool InitializationSequence::Diagnose(Sema &S, const InitializedEntity &Entity,
                                      const InitializationKind &Kind,
                                      llvm::ArrayRef<Expr *> Args) {
  if (!Failed())
    return false;

  // When we want to diagnose only one element of a braced-init-list,
  // we need to factor it out.
  Expr *OnlyArg;
  if (Args.size() == 1) {
    auto *List = dyn_cast<InitListExpr>(Args[0]);
    if (List && List->getNumInits() == 1)
      OnlyArg = List->getInit(0);
    else
      OnlyArg = Args[0];
  } else
    OnlyArg = nullptr;

  QualType DestType = Entity.getType();
  switch (Failure) {
  case FK_ArrayNeedsInitList:
    S.Diag(Kind.getLocation(), diag::err_array_init_not_init_list) << 0;
    break;
  case FK_ArrayNeedsInitListOrStringLiteral:
    S.Diag(Kind.getLocation(), diag::err_array_init_not_init_list) << 1;
    break;
  case FK_ArrayNeedsInitListOrWideStringLiteral:
    S.Diag(Kind.getLocation(), diag::err_array_init_not_init_list) << 2;
    break;
  case FK_NarrowStringIntoWideCharArray:
    S.Diag(Kind.getLocation(), diag::err_array_init_narrow_string_into_wchar);
    break;
  case FK_WideStringIntoCharArray:
    S.Diag(Kind.getLocation(), diag::err_array_init_wide_string_into_char);
    break;
  case FK_IncompatWideStringIntoWideChar:
    S.Diag(Kind.getLocation(),
           diag::err_array_init_incompat_wide_string_into_wchar);
    break;
  case FK_PlainStringIntoUTF8Char:
    S.Diag(Kind.getLocation(), diag::err_array_init_plain_string_into_char8_t);
    S.Diag(Args.front()->getBeginLoc(),
           diag::note_array_init_plain_string_into_char8_t)
        << FixItHint::CreateInsertion(Args.front()->getBeginLoc(), "u8");
    break;
  case FK_UTF8StringIntoPlainChar:
    S.Diag(Kind.getLocation(), diag::err_array_init_utf8_string_into_char)
        << DestType->isSignedIntegerType();
    break;
  case FK_ArrayTypeMismatch:
  case FK_NonConstantArrayInit:
    S.Diag(Kind.getLocation(), (Failure == FK_ArrayTypeMismatch
                                    ? diag::err_array_init_different_type
                                    : diag::err_array_init_non_constant_array))
        << DestType << OnlyArg->getType() << Args[0]->getSourceRange();
    break;

  case FK_VariableLengthArrayHasInitializer:
    S.Diag(Kind.getLocation(), diag::err_variable_object_no_init)
        << Args[0]->getSourceRange();
    break;

  case FK_AddressOfUnaddressableFunction: {
    auto *FD = cast<FunctionDecl>(cast<DeclRefExpr>(OnlyArg)->getDecl());
    S.checkAddressOfFunctionIsAvailable(FD, /*Complain=*/true,
                                        OnlyArg->getBeginLoc());
    break;
  }

  case FK_ConversionFailed: {
    QualType FromType = OnlyArg->getType();
    PartialDiagnostic PDiag = S.PDiag(diag::err_init_conversion_failed)
                              << (int)Entity.getKind() << DestType
                              << OnlyArg->isLValue() << FromType
                              << Args[0]->getSourceRange();
    S.DiagnoseFunctionTypeMismatch(PDiag, FromType, DestType);
    S.Diag(Kind.getLocation(), PDiag);
    break;
  }

  case FK_TooManyInitsForScalar: {
    SourceRange R;

    auto *InitList = dyn_cast<InitListExpr>(Args[0]);
    if (InitList && InitList->getNumInits() >= 1) {
      R = SourceRange(InitList->getInit(0)->getEndLoc(), InitList->getEndLoc());
    } else {
      assert(Args.size() > 1 && "Expected multiple initializers!");
      R = SourceRange(Args.front()->getEndLoc(), Args.back()->getEndLoc());
    }

    R.setBegin(S.getLocForEndOfToken(R.getBegin()));
    if (Kind.isCStyleOrFunctionalCast())
      S.Diag(Kind.getLocation(), diag::err_builtin_func_cast_more_than_one_arg)
          << R;
    else
      S.Diag(Kind.getLocation(), diag::err_excess_initializers)
          << /*scalar=*/2 << R;
    break;
  }

  case FK_InitListBadDestinationType:
    S.Diag(Kind.getLocation(), diag::err_init_list_bad_dest_type)
        << (DestType->isRecordType()) << DestType << Args[0]->getSourceRange();
    break;

  case FK_DefaultInitOfConst:
    if (const auto *VD = dyn_cast_if_present<VarDecl>(Entity.getDecl());
        VD && VD->isConstexpr()) {
      S.Diag(Kind.getLocation(), diag::err_constexpr_var_requires_const_init)
          << VD;
    } else {
      S.Diag(Kind.getLocation(), diag::err_default_init_const) << DestType;
    }
    break;

  case FK_Incomplete:
    S.RequireCompleteType(Kind.getLocation(), FailedIncompleteType,
                          diag::err_init_incomplete_type);
    break;

  case FK_ListInitializationFailed: {
    // Run the init list checker again to emit diagnostics.
    InitListExpr *InitList = cast<InitListExpr>(Args[0]);
    diagnoseListInit(S, Entity, InitList);
    break;
  }

  case FK_PlaceholderType: {
    break;
  }

  case FK_DesignatedInitForNonAggregate: {
    InitListExpr *InitList = cast<InitListExpr>(Args[0]);
    S.Diag(Kind.getLocation(), diag::err_designated_init_for_non_aggregate)
        << Entity.getType() << InitList->getSourceRange();
    break;
  }
  }

  PrintInitLocationNote(S, Entity);
  return true;
}

void InitializationSequence::dump(llvm::raw_ostream &OS) const {
  switch (SequenceKind) {
  case FailedSequence: {
    OS << "Failed sequence: ";
    switch (Failure) {
    case FK_ArrayNeedsInitList:
      OS << "array requires initializer list";
      break;

    case FK_AddressOfUnaddressableFunction:
      OS << "address of unaddressable function was taken";
      break;

    case FK_ArrayNeedsInitListOrStringLiteral:
      OS << "array requires initializer list or string literal";
      break;

    case FK_ArrayNeedsInitListOrWideStringLiteral:
      OS << "array requires initializer list or wide string literal";
      break;

    case FK_NarrowStringIntoWideCharArray:
      OS << "narrow string into wide char array";
      break;

    case FK_WideStringIntoCharArray:
      OS << "wide string into char array";
      break;

    case FK_IncompatWideStringIntoWideChar:
      OS << "incompatible wide string into wide char array";
      break;

    case FK_PlainStringIntoUTF8Char:
      OS << "plain string literal into char8_t array";
      break;

    case FK_UTF8StringIntoPlainChar:
      OS << "u8 string literal into char array";
      break;

    case FK_ArrayTypeMismatch:
      OS << "array type mismatch";
      break;

    case FK_NonConstantArrayInit:
      OS << "non-constant array initializer";
      break;

    case FK_ConversionFailed:
      OS << "conversion failed";
      break;

    case FK_TooManyInitsForScalar:
      OS << "too many initializers for scalar";
      break;

    case FK_InitListBadDestinationType:
      OS << "initializer list for non-aggregate, non-scalar type";
      break;

    case FK_DefaultInitOfConst:
      OS << "default initialization of a const variable";
      break;

    case FK_Incomplete:
      OS << "initialization of incomplete type";
      break;

    case FK_ListInitializationFailed:
      OS << "list initialization checker failure";
      break;

    case FK_VariableLengthArrayHasInitializer:
      OS << "variable length array has an initializer";
      break;

    case FK_PlaceholderType:
      OS << "initializer expression isn't contextually valid";
      break;

    case FK_DesignatedInitForNonAggregate:
      OS << "designated initializer for non-aggregate type";
      break;
    }
    OS << '\n';
    return;
  }

  case DependentSequence:
    OS << "Dependent sequence\n";
    return;

  case NormalSequence:
    OS << "Normal sequence: ";
    break;
  }

  for (step_iterator S = step_begin(), SEnd = step_end(); S != SEnd; ++S) {
    if (S != step_begin()) {
      OS << " -> ";
    }

    switch (S->Kind) {
    case SK_ListInitialization:
      OS << "list aggregate initialization";
      break;

    case SK_ZeroInitialization:
      OS << "zero initialization";
      break;

    case SK_CAssignment:
      OS << "C assignment";
      break;

    case SK_StringInit:
      OS << "string initialization";
      break;

    case SK_ArrayLoopIndex:
      OS << "indexing for array initialization loop";
      break;

    case SK_ArrayLoopInit:
      OS << "array initialization loop";
      break;

    case SK_ArrayInit:
      OS << "array initialization";
      break;

    case SK_GNUArrayInit:
      OS << "array initialization (GNU extension)";
      break;
    }

    OS << " [" << S->Type << ']';
  }

  OS << '\n';
}

void InitializationSequence::dump() const { dump(llvm::errs()); }

bool Sema::CanPerformCopyInitialization(const InitializedEntity &Entity,
                                        ExprResult Init) {
  if (Init.isInvalid())
    return false;

  Expr *InitE = Init.get();
  assert(InitE && "No initialization expression");

  InitializationKind Kind =
      InitializationKind::CreateCopy(InitE->getBeginLoc(), SourceLocation());
  InitializationSequence Seq(*this, Entity, Kind, InitE);
  return !Seq.Failed();
}

ExprResult Sema::PerformCopyInitialization(const InitializedEntity &Entity,
                                           SourceLocation EqualLoc,
                                           ExprResult Init,
                                           bool TopLevelOfInitList) {
  if (Init.isInvalid())
    return ExprError();

  Expr *InitE = Init.get();
  assert(InitE && "No initialization expression?");

  if (EqualLoc.isInvalid())
    EqualLoc = InitE->getBeginLoc();

  InitializationKind Kind =
      InitializationKind::CreateCopy(InitE->getBeginLoc(), EqualLoc);
  InitializationSequence Seq(*this, Entity, Kind, InitE, TopLevelOfInitList);

  return Seq.Perform(*this, Entity, Kind, InitE);
}
