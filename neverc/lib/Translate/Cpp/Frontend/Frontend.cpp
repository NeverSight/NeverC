#include "Frontend.h"
#include "FrontendEntry.h"
#include "BuildID.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclFriend.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/StmtCXX.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Index/USRGeneration.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <set>
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

using namespace clang;
namespace nct {

static bool standardExceptionSpecification(const FunctionProtoType *Prototype) {
  if (!Prototype)
    return false;
  switch (Prototype->getExceptionSpecType()) {
  case EST_None:
  case EST_DynamicNone: // C++17 throw() has the noexcept(true) meaning.
  case EST_BasicNoexcept:
  case EST_NoexceptTrue:
  case EST_NoexceptFalse:
    return true;
  default:
    return false; // No dependent, unresolved, typed or vendor specifications.
  }
}

// Name shape only: patterns do not yet have concrete parameter/result types.
static bool ordinaryOperatorKind(OverloadedOperatorKind Kind) {
  switch (Kind) {
  case OO_Plus: case OO_Minus: case OO_Star: case OO_Slash: case OO_Percent:
  case OO_Caret: case OO_Amp: case OO_Pipe: case OO_Tilde: case OO_Exclaim:
  case OO_Equal: case OO_Less: case OO_Greater:
  case OO_PlusEqual: case OO_MinusEqual: case OO_StarEqual: case OO_SlashEqual:
  case OO_PercentEqual: case OO_CaretEqual: case OO_AmpEqual: case OO_PipeEqual:
  case OO_LessLess: case OO_GreaterGreater:
  case OO_LessLessEqual: case OO_GreaterGreaterEqual:
  case OO_EqualEqual: case OO_ExclaimEqual: case OO_LessEqual: case OO_GreaterEqual:
  case OO_AmpAmp: case OO_PipePipe: case OO_PlusPlus: case OO_MinusMinus:
  case OO_Comma: case OO_ArrowStar: case OO_Arrow: case OO_Call: case OO_Subscript:
    return true;
  default:
    return false;
  }
}

// Name shape only; primary ownership and concrete types are checked separately.
static bool ordinaryFreeFunctionName(const FunctionDecl *F) {
  return F && F->getKind() == Decl::Function &&
         (F->getIdentifier() || ordinaryOperatorKind(F->getOverloadedOperator()));
}

// Shape only: Allowlist separately validates the primary, arguments and body.
static bool concreteFreeFunctionTemplate(const FunctionDecl *F) {
  return ordinaryFreeFunctionName(F) &&
         F->getTemplatedKind() == FunctionDecl::TK_FunctionTemplateSpecialization &&
         F->getPrimaryTemplate() && !F->isDependentContext() &&
         !F->getType().isNull() && !F->getType()->isDependentType();
}

// TemplateDecl excludes class partial specializations. Keep the parameter
// owner distinct from the primary that supplies a concrete record's identity.
static const TemplateParameterList *templateSourceParameters(const NamedDecl *Owner) {
  if (const auto *Partial = dyn_cast_or_null<ClassTemplatePartialSpecializationDecl>(Owner))
    return Partial->getTemplateParameters();
  if (const auto *Template = dyn_cast_or_null<TemplateDecl>(Owner))
    return Template->getTemplateParameters();
  return nullptr;
}

static const NamedDecl *classTemplatePattern(const CXXRecordDecl *Record) {
  if (const auto *Partial = dyn_cast_or_null<ClassTemplatePartialSpecializationDecl>(Record))
    return Partial;
  if (const auto *Instance = dyn_cast_or_null<ClassTemplateSpecializationDecl>(Record)) {
    if (Instance->getKind() != Decl::ClassTemplateSpecialization || Instance->isDependentContext())
      return nullptr;
    auto Selected = Instance->getSpecializedTemplateOrPartial();
    if (const auto *Partial = Selected.dyn_cast<ClassTemplatePartialSpecializationDecl *>())
      return Partial;
    return Selected.dyn_cast<ClassTemplateDecl *>();
  }
  return Record && Record->getKind() == Decl::CXXRecord
             ? Record->getDescribedClassTemplate() : nullptr;
}

static const CXXRecordDecl *classPatternRecord(const NamedDecl *Owner) {
  if (const auto *Partial = dyn_cast_or_null<ClassTemplatePartialSpecializationDecl>(Owner))
    return Partial;
  if (const auto *Primary = dyn_cast_or_null<ClassTemplateDecl>(Owner))
    return Primary->getTemplatedDecl();
  return nullptr;
}

// Identity only: unused member instances can have an undeduced auto return.
static const NamedDecl *classFunctionPattern(const FunctionDecl *F) {
  const auto *M = dyn_cast_or_null<CXXMethodDecl>(F);
  if (!M || (!isa<CXXConstructorDecl, CXXDestructorDecl, CXXConversionDecl>(M) &&
             (M->getKind() != Decl::CXXMethod ||
              (!M->getIdentifier() &&
               !ordinaryOperatorKind(M->getOverloadedOperator())))) ||
      M->getTemplatedKind() != FunctionDecl::TK_MemberSpecialization ||
      !M->getMemberSpecializationInfo() || M->getDescribedFunctionTemplate())
    return nullptr;
  const auto *Record = dyn_cast<ClassTemplateSpecializationDecl>(M->getParent());
  if (!Record || Record->getKind() != Decl::ClassTemplateSpecialization ||
      Record->isDependentContext())
    return nullptr;
  return classTemplatePattern(Record);
}

static bool concreteClassFunction(const FunctionDecl *F) {
  return classFunctionPattern(F) && !F->isDependentContext() &&
         !F->getType().isNull() && !F->getType()->isDependentType();
}

// Local classes are instantiated with their enclosing function. They have
// member-specialization metadata but are not ClassTemplateSpecializationDecls.
static bool concreteLocalClassFunction(const FunctionDecl *F) {
  for (unsigned Depth = 0; Depth < 64; ++Depth) {
    const auto *M = dyn_cast_or_null<CXXMethodDecl>(F);
    if (!M || (!isa<CXXConstructorDecl, CXXDestructorDecl, CXXConversionDecl>(M) &&
               (M->getKind() != Decl::CXXMethod ||
                (!M->getIdentifier() &&
                 !ordinaryOperatorKind(M->getOverloadedOperator())))) ||
        M->getTemplatedKind() != FunctionDecl::TK_MemberSpecialization ||
        !M->getMemberSpecializationInfo() || M->getDescribedFunctionTemplate() ||
        M->isDependentContext() || M->getType().isNull() ||
        M->getType()->isDependentType())
      return false;
    const auto *Record = M->getParent();
    const auto *Owner = Record->isLocalClass();
    const auto *Origin = Record->getInstantiatedFromMemberClass();
    const auto *Pattern =
        dyn_cast_or_null<CXXMethodDecl>(M->getInstantiatedFromMemberFunction());
    if (Record->getKind() != Decl::CXXRecord || Record->isDependentContext() ||
        !Owner || !Origin || !Pattern || Pattern->getKind() != M->getKind() ||
        Origin->getCanonicalDecl() != Pattern->getParent()->getCanonicalDecl())
      return false;
    if (concreteFreeFunctionTemplate(Owner) || concreteClassFunction(Owner))
      return true;
    F = Owner; // A local class can itself be declared in a local-class method.
  }
  return false;
}

static bool concreteMemberFunction(const FunctionDecl *F) {
  return concreteClassFunction(F) || concreteLocalClassFunction(F);
}

// Identity only: a static member definition/initializer can still be lazy.
static const NamedDecl *classStaticDataPattern(const VarDecl *V) {
  if (!V || V->getKind() != Decl::Var || !V->isStaticDataMember() ||
      V->getDescribedVarTemplate() || !V->getMemberSpecializationInfo())
    return nullptr;
  const auto *Record = dyn_cast<ClassTemplateSpecializationDecl>(V->getDeclContext());
  const auto *Origin = V->getInstantiatedFromStaticDataMember();
  if (!Record || Record->getKind() != Decl::ClassTemplateSpecialization ||
      Record->isDependentContext() || !Origin || Origin->getKind() != Decl::Var ||
      !Origin->isStaticDataMember() || Origin->getDescribedVarTemplate())
    return nullptr;
  const auto *Primary = classTemplatePattern(Record);
  const auto *Parent = dyn_cast<CXXRecordDecl>(Origin->getDeclContext());
  return Primary && Parent &&
                 Parent->getCanonicalDecl() == classPatternRecord(Primary)->getCanonicalDecl()
             ? Primary : nullptr;
}

// Classification evidence only. In particular, an out-of-line defaulted
// pattern does not supply an uninstantiated concrete runtime body.
static const FunctionDecl *defaultedDeclaration(const CXXMethodDecl *M) {
  if (!M)
    return nullptr;
  const bool Specialization =
      M->getTemplateSpecializationKind() == TSK_ExplicitSpecialization;
  // A specialization's redeclaration chain can retain an instantiated member.
  // Only its own spelled template<> declaration establishes defaulting there.
  for (const auto *D : M->redecls())
    if (D->isDefaulted() &&
        (!Specialization || D->getNumTemplateParameterLists()))
      return D;
  const auto *Primary = classFunctionPattern(M);
  if (!Primary || Specialization)
    return nullptr;
  const auto *Pattern =
      dyn_cast_or_null<CXXMethodDecl>(M->getInstantiatedFromMemberFunction());
  if (!Pattern || Pattern->getKind() != M->getKind() ||
      Pattern->getParent()->getCanonicalDecl() !=
          classPatternRecord(Primary)->getCanonicalDecl())
    return nullptr;
  for (const auto *D : Pattern->redecls())
    if (D->isDefaulted())
      return D;
  return nullptr;
}

static bool defaultedSpecialMember(const CXXMethodDecl *M) {
  if (!defaultedDeclaration(M))
    return false;
  if (const auto *C = dyn_cast<CXXConstructorDecl>(M))
    return C->isDefaultConstructor() || C->isCopyOrMoveConstructor();
  return isa<CXXDestructorDecl>(M) || M->isCopyAssignmentOperator() ||
         M->isMoveAssignmentOperator();
}

bool ordinaryMethod(const CXXMethodDecl *M) {
  if (!M || M->isImplicit() || !M->getIdentifier() || M->isVirtual() ||
      M->isExplicitObjectMemberFunction() || M->isVariadic() ||
      (M->getTemplatedKind() != FunctionDecl::TK_NonTemplate &&
       !concreteMemberFunction(M)) ||
      M->isDeletedAsWritten() || M->isExplicitlyDefaulted() || M->isConsteval() ||
      M->getMethodQualifiers().hasVolatile() ||
      M->getMethodQualifiers().hasRestrict())
    return false;
  const auto *Prototype = M->getType()->getAs<FunctionProtoType>();
  return standardExceptionSpecification(Prototype);
}

bool ordinaryOperator(const FunctionDecl *F) {
  if (!F || !F->isOverloadedOperator() || F->isImplicit() || F->isVariadic() ||
      (F->getTemplatedKind() != FunctionDecl::TK_NonTemplate &&
       !concreteFreeFunctionTemplate(F) && !concreteMemberFunction(F)) ||
      F->isDeletedAsWritten() || F->isDefaulted() || F->isConsteval())
    return false;
  if (!ordinaryOperatorKind(F->getOverloadedOperator()))
    return false;
  if (const auto *M = dyn_cast<CXXMethodDecl>(F))
    if (!M->isUserProvided() || M->isVirtual() || M->isStatic() ||
        M->isExplicitObjectMemberFunction() ||
        M->getMethodQualifiers().hasVolatile() ||
        M->getMethodQualifiers().hasRestrict())
      return false;
  return standardExceptionSpecification(F->getType()->getAs<FunctionProtoType>());
}

bool ordinaryConversion(const CXXConversionDecl *C) {
  if (!C || C->isImplicit() || !C->isUserProvided() || C->isVirtual() ||
      C->isStatic() || C->isExplicitObjectMemberFunction() || C->isVariadic() ||
      (C->getTemplatedKind() != FunctionDecl::TK_NonTemplate &&
       !concreteMemberFunction(C)) ||
      C->isDeletedAsWritten() || C->isDefaulted() || C->isConsteval() ||
      C->getNumParams() || C->getMethodQualifiers().hasVolatile() ||
      C->getMethodQualifiers().hasRestrict())
    return false;
  return standardExceptionSpecification(C->getType()->getAs<FunctionProtoType>());
}

bool ordinaryConstructor(const CXXConstructorDecl *C) {
  if (!C || C->isImplicit() || !C->isUserProvided() || C->isVariadic() ||
      (C->getTemplatedKind() != FunctionDecl::TK_NonTemplate &&
       !concreteMemberFunction(C)) ||
      C->isDeletedAsWritten() || C->isExplicitlyDefaulted() || C->isConsteval() ||
      C->isDelegatingConstructor() ||
      C->isInheritingConstructor())
    return false;
  if (C->isCopyOrMoveConstructor()) {
    if (!C->getNumParams())
      return false;
    for (unsigned I = 1; I < C->getNumParams(); ++I) {
      const auto *P = C->getParamDecl(I);
      if (!P->hasDefaultArg() || P->hasUnparsedDefaultArg() ||
          (P->hasUninstantiatedDefaultArg() && !concreteMemberFunction(C)))
        return false;
    }
    auto Source = C->getParamDecl(0)->getType();
    if (C->isMoveConstructor() ? !Source->isRValueReferenceType()
                               : !Source->isLValueReferenceType())
      return false;
    auto Pointee = Source->getPointeeType();
    const auto *Record = Pointee->getAsCXXRecordDecl();
    if (Pointee.isVolatileQualified() || Pointee.isRestrictQualified() ||
        !Record || Record->getCanonicalDecl() != C->getParent()->getCanonicalDecl())
      return false;
  }
  const auto *Prototype = C->getType()->getAs<FunctionProtoType>();
  return standardExceptionSpecification(Prototype);
}

static bool ordinaryAssignment(const CXXMethodDecl *M, bool Move) {
  if (!M || M->isImplicit() || !M->isUserProvided() ||
      (Move ? !M->isMoveAssignmentOperator() : !M->isCopyAssignmentOperator()) ||
      M->isVirtual() ||
      M->isExplicitObjectMemberFunction() || M->isVariadic() ||
      (M->getTemplatedKind() != FunctionDecl::TK_NonTemplate &&
       !concreteMemberFunction(M)) ||
      M->isDeletedAsWritten() || M->isExplicitlyDefaulted() || M->isConsteval() ||
      M->getNumParams() != 1 || M->getMethodQualifiers().getCVRQualifiers() ||
      (!Move && M->getRefQualifier() == RQ_RValue))
    return false;
  auto Source = M->getParamDecl(0)->getType();
  auto Result = M->getReturnType();
  if ((Move ? !Source->isRValueReferenceType() : !Source->isLValueReferenceType()) ||
      Source->getPointeeType().isVolatileQualified() ||
      Source->getPointeeType().isRestrictQualified() ||
      !Result->isLValueReferenceType() ||
      Result->getPointeeType().getQualifiers().getCVRQualifiers())
    return false;
  const auto *SourceRecord = Source->getPointeeType()->getAsCXXRecordDecl();
  const auto *ResultRecord = Result->getPointeeType()->getAsCXXRecordDecl();
  const auto *Prototype = M->getType()->getAs<FunctionProtoType>();
  return SourceRecord && ResultRecord &&
         SourceRecord->getCanonicalDecl() == M->getParent()->getCanonicalDecl() &&
         ResultRecord->getCanonicalDecl() == M->getParent()->getCanonicalDecl() &&
         standardExceptionSpecification(Prototype);
}

bool ordinaryCopyAssignment(const CXXMethodDecl *M) {
  return ordinaryAssignment(M, false);
}

bool ordinaryMoveAssignment(const CXXMethodDecl *M) {
  return ordinaryAssignment(M, true);
}

bool supportedCopyAssignment(const CXXMethodDecl *M) {
  return ordinaryCopyAssignment(M) || defaultedCopyAssignment(M);
}

bool supportedAssignment(const CXXMethodDecl *M) {
  return supportedCopyAssignment(M) || ordinaryMoveAssignment(M) || defaultedMoveAssignment(M);
}

bool callableMethod(const CXXMethodDecl *M) {
  return ordinaryMethod(M) || ordinaryOperator(M) ||
         ordinaryConversion(dyn_cast_or_null<CXXConversionDecl>(M)) || supportedAssignment(M);
}

static bool defaultedFunction(const CXXMethodDecl *M) {
  if (!M || M->isInvalidDecl() || M->isDeleted() || M->isVirtual() ||
      M->isVariadic() || M->isExplicitObjectMemberFunction() ||
      (M->getTemplatedKind() != FunctionDecl::TK_NonTemplate &&
       !concreteMemberFunction(M)) || M->isConsteval())
    return false;
  if (!defaultedDeclaration(M))
    return false;
  for (const auto *D : M->redecls()) {
    if (D->isInvalidDecl() || D->isDeleted())
      return false;
    // Preserve lazy unwritten implicit specifications. Written forms must be
    // resolved standard specifications on every redeclaration; normal TypeLoc
    // traversal still inspects the original noexcept expressions.
    if (const auto *Info = D->getTypeSourceInfo()) {
      auto Location = Info->getTypeLoc().getAs<FunctionProtoTypeLoc>();
      if (!Location || (Location.getExceptionSpecRange().isValid() &&
          !standardExceptionSpecification(D->getType()->getAs<FunctionProtoType>())))
        return false;
    } else if (!D->isImplicit()) {
      return false;
    }
  }
  return true;
}

bool defaultedLifecycle(const CXXMethodDecl *M) {
  if (!defaultedFunction(M) || M->getNumParams())
    return false;
  if (const auto *C = dyn_cast<CXXConstructorDecl>(M))
    return C->isDefaultConstructor() && !C->isDelegatingConstructor() &&
           !C->isInheritingConstructor();
  return isa<CXXDestructorDecl>(M);
}

bool defaultedCopyConstructor(const CXXConstructorDecl *C) {
  if (!defaultedFunction(C) || !C->isCopyConstructor() ||
      C->isMoveConstructor() || C->isDelegatingConstructor() ||
      C->isInheritingConstructor() || C->getNumParams() != 1)
    return false;
  auto Source = C->getParamDecl(0)->getType();
  if (!Source->isLValueReferenceType())
    return false;
  auto Pointee = Source->getPointeeType();
  const auto *Record = Pointee->getAsCXXRecordDecl();
  return !Pointee.isVolatileQualified() && !Pointee.isRestrictQualified() &&
         Record && Record->getCanonicalDecl() == C->getParent()->getCanonicalDecl();
}

bool defaultedCopyAssignment(const CXXMethodDecl *M) {
  if (!defaultedFunction(M) || !M->isCopyAssignmentOperator() ||
      M->isMoveAssignmentOperator() || M->getNumParams() != 1 ||
      M->getMethodQualifiers().getCVRQualifiers() || M->getRefQualifier() == RQ_RValue)
    return false;
  auto Source = M->getParamDecl(0)->getType();
  auto Result = M->getReturnType();
  if (!Source->isLValueReferenceType() || !Result->isLValueReferenceType() ||
      Source->getPointeeType().isVolatileQualified() ||
      Source->getPointeeType().isRestrictQualified() ||
      Result->getPointeeType().getQualifiers().getCVRQualifiers())
    return false;
  const auto *SourceRecord = Source->getPointeeType()->getAsCXXRecordDecl();
  const auto *ResultRecord = Result->getPointeeType()->getAsCXXRecordDecl();
  return SourceRecord && ResultRecord &&
         SourceRecord->getCanonicalDecl() == M->getParent()->getCanonicalDecl() &&
         ResultRecord->getCanonicalDecl() == M->getParent()->getCanonicalDecl();
}

bool defaultedMoveConstructor(const CXXConstructorDecl *C) {
  if (!defaultedFunction(C) || !C->isMoveConstructor() ||
      C->isDelegatingConstructor() || C->isInheritingConstructor() ||
      C->getNumParams() != 1)
    return false;
  auto Source = C->getParamDecl(0)->getType();
  if (!Source->isRValueReferenceType())
    return false;
  auto Pointee = Source->getPointeeType();
  const auto *Record = Pointee->getAsCXXRecordDecl();
  return !Pointee.getQualifiers().getCVRQualifiers() && Record &&
         Record->getCanonicalDecl() == C->getParent()->getCanonicalDecl();
}

bool defaultedCopyOrMoveConstructor(const CXXConstructorDecl *C) {
  return defaultedCopyConstructor(C) || defaultedMoveConstructor(C);
}

bool defaultedMoveAssignment(const CXXMethodDecl *M) {
  if (!defaultedFunction(M) || !M->isMoveAssignmentOperator() ||
      M->getNumParams() != 1 || M->getMethodQualifiers().getCVRQualifiers())
    return false;
  auto Source = M->getParamDecl(0)->getType();
  auto Result = M->getReturnType();
  if (!Source->isRValueReferenceType() || !Result->isLValueReferenceType() ||
      Source->getPointeeType().getQualifiers().getCVRQualifiers() ||
      Result->getPointeeType().getQualifiers().getCVRQualifiers())
    return false;
  const auto *SourceRecord = Source->getPointeeType()->getAsCXXRecordDecl();
  const auto *ResultRecord = Result->getPointeeType()->getAsCXXRecordDecl();
  return SourceRecord && ResultRecord &&
         SourceRecord->getCanonicalDecl() == M->getParent()->getCanonicalDecl() &&
         ResultRecord->getCanonicalDecl() == M->getParent()->getCanonicalDecl();
}

bool defaultedAssignment(const CXXMethodDecl *M) {
  return defaultedCopyAssignment(M) || defaultedMoveAssignment(M);
}

std::optional<GeneratedArrayAssignment> generatedArrayAssignment(
    const CallExpr *Call, const CXXMethodDecl *Owner, ASTContext &Context) {
  if (!defaultedAssignment(Owner) || !Call || Call->getNumArgs() != 3 ||
      !Call->getDirectCallee() ||
      Call->getDirectCallee()->getBuiltinID() != Builtin::BI__builtin_memcpy)
    return std::nullopt;
  auto MemberAddress = [](const Expr *E) -> const MemberExpr * {
    E = E->IgnoreParens();
    while (const auto *C = dyn_cast<ImplicitCastExpr>(E)) {
      if ((C->getCastKind() != CK_BitCast && C->getCastKind() != CK_NoOp) ||
          !C->getType()->isPointerType() ||
          !C->getType()->getPointeeType()->isVoidType() ||
          !C->getSubExpr()->getType()->isPointerType())
        return nullptr;
      E = C->getSubExpr()->IgnoreParens();
    }
    const auto *Address = dyn_cast<UnaryOperator>(E);
    return Address && Address->getOpcode() == UO_AddrOf
               ? dyn_cast<MemberExpr>(Address->getSubExpr()->IgnoreParens()) : nullptr;
  };
  const bool Moving = defaultedMoveAssignment(Owner);
  const auto *To = MemberAddress(Call->getArg(0));
  const auto *From = MemberAddress(Call->getArg(1));
  if (!To || !From || !To->isArrow() || From->isArrow() ||
      !To->isLValue() || (Moving ? !From->isXValue() : !From->isLValue()) ||
      To->getType().isConstQualified() ||
      !isa<CXXThisExpr>(To->getBase()->IgnoreParenImpCasts()))
    return std::nullopt;
  const Expr *Base = From->getBase()->IgnoreParens();
  if (Moving) {
    // Sema's CastForMoving builds exactly static_cast<R&&>(the parameter).
    // Its generated address-of-xvalue is valid here; no source builtin or
    // arbitrary cast gains admission from recognizing this internal shape.
    const auto *Cast = dyn_cast<CXXStaticCastExpr>(Base);
    if (!Cast || Cast->getCastKind() != CK_NoOp || !Cast->isXValue() ||
        !Cast->getSubExpr()->isLValue() ||
        !Context.hasSameType(Cast->getType(), Cast->getSubExpr()->getType()) ||
        !Context.hasSameType(Cast->getType(), Owner->getParamDecl(0)->getType()->getPointeeType()))
      return std::nullopt;
    Base = Cast->getSubExpr()->IgnoreParens();
  }
  const auto *Parameter = dyn_cast<DeclRefExpr>(Base->IgnoreParenImpCasts());
  const auto *Field = dyn_cast<FieldDecl>(To->getMemberDecl());
  const auto *Bytes = dyn_cast<IntegerLiteral>(Call->getArg(2)->IgnoreParenImpCasts());
  if (!Field || !Parameter || !Bytes ||
      Field->getParent()->getCanonicalDecl() != Owner->getParent()->getCanonicalDecl() ||
      From->getMemberDecl()->getCanonicalDecl() != Field->getCanonicalDecl() ||
      Parameter->getDecl()->getCanonicalDecl() != Owner->getParamDecl(0)->getCanonicalDecl() ||
      !Context.hasSameUnqualifiedType(To->getType(), From->getType()) ||
      !Context.hasSameUnqualifiedType(To->getType(), Field->getType()))
    return std::nullopt;
  auto T = Field->getType();
  const auto *Array = Context.getAsConstantArrayType(T);
  if (!Array || !Array->getSize().getLimitedValue(65537) ||
      Array->getSize().getLimitedValue(65537) > 65536 ||
      Bytes->getValue().getLimitedValue() != uint64_t(Context.getTypeSizeInChars(T).getQuantity()))
    return std::nullopt;
  auto Element = Context.getBaseElementType(T);
  const auto *Record = Element->getAsCXXRecordDecl();
  if (!(Element->isIntegerType() || Element->isEnumeralType() ||
        Element->isPointerType() ||
        (Record && (Record->hasTrivialCopyAssignment() ||
                    (Moving && Record->hasTrivialMoveAssignment())))))
    return std::nullopt;
  // Sema emits this shape only after selecting trivial assignment. Construction
  // and destruction may still be nontrivial; never substitute those operations.
  return GeneratedArrayAssignment{To, From, T};
}

bool supportedConstructor(const CXXConstructorDecl *C) {
  return ordinaryConstructor(C) || defaultedLifecycle(C) ||
         defaultedCopyOrMoveConstructor(C);
}

const CXXConstructExpr *constructorConversion(const CastExpr *Cast,
                                              ASTContext &Context) {
  if (!Cast || Cast->getCastKind() != CK_ConstructorConversion ||
      !Cast->getType()->isRecordType())
    return nullptr;
  const Expr *Inner = Cast->getSubExpr()->IgnoreParens();
  // Clang binds a constructed temporary before wrapping it in an explicit
  // constructor conversion. Its lifetime still belongs to our destination;
  // inspecting this wrapper must not create another owned object.
  while (const auto *Binding = dyn_cast<CXXBindTemporaryExpr>(Inner))
    Inner = Binding->getSubExpr()->IgnoreParens();
  const auto *Construction = dyn_cast<CXXConstructExpr>(Inner);
  if (!Construction || !supportedConstructor(Construction->getConstructor()) ||
      !Context.hasSameUnqualifiedType(Cast->getType(), Construction->getType()))
    return nullptr;
  return Construction;
}

const CallExpr *userConversionCall(const CastExpr *Cast, ASTContext &Context) {
  if (!Cast || Cast->getCastKind() != CK_UserDefinedConversion || !Cast->getSubExpr())
    return nullptr;
  const Expr *Inner = Cast->getSubExpr()->IgnoreParens();
  while (const auto *Binding = dyn_cast<CXXBindTemporaryExpr>(Inner)) {
    const auto *Sub = Binding->getSubExpr();
    if (!Sub || !Context.hasSameType(Binding->getType(), Sub->getType()) ||
        Binding->getValueKind() != Sub->getValueKind())
      return nullptr;
    Inner = Sub->IgnoreParens();
  }
  const auto *Call = dyn_cast<CXXMemberCallExpr>(Inner);
  if (!Call || !ordinaryConversion(dyn_cast_or_null<CXXConversionDecl>(Call->getDirectCallee())) ||
      !directMethodReference(Call) ||
      !Context.hasSameType(Cast->getType(), Call->getType()) ||
      Cast->getValueKind() != Call->getValueKind())
    return nullptr;
  // Sema wraps the selected call with its own result type and value category.
  // Subsequent standard conversions remain separate inspected AST nodes.
  return Call;
}

static bool temporaryShape(const MaterializeTemporaryExpr *M, ASTContext &Context) {
  return M && (!M->getType()->isArrayType() ||
               Context.getAsConstantArrayType(M->getType())) &&
         M->getSubExpr() && M->getSubExpr()->isPRValue() &&
         Context.hasSameUnqualifiedType(M->getType(), M->getSubExpr()->getType());
}
bool fullExpressionTemporary(const MaterializeTemporaryExpr *M, ASTContext &Context) {
  return temporaryShape(M, Context) && M->getStorageDuration() == SD_FullExpression &&
         !M->getExtendingDecl();
}
const VarDecl *automaticTemporaryOwner(const MaterializeTemporaryExpr *M,
                                      ASTContext &Context) {
  if (!temporaryShape(M, Context) || M->getStorageDuration() != SD_Automatic)
    return nullptr;
  const auto *Owner = dyn_cast_or_null<VarDecl>(M->getExtendingDecl());
  if (!Owner || Owner->getKind() != Decl::Var || Owner->isImplicit() ||
      !Owner->isLocalVarDecl() || !Owner->hasLocalStorage() ||
      !Owner->getType()->isReferenceType())
    return nullptr;
  return Owner->getCanonicalDecl();
}
std::optional<RangeForComponents> rangeForComponents(const CXXForRangeStmt *Loop) {
  auto Resolved = [](const Expr *E) {
    return E && !E->getType().isNull() && !E->isTypeDependent() &&
           !E->isValueDependent() && !E->isInstantiationDependent();
  };
  if (!Loop || Loop->getInit() || Loop->getCoawaitLoc().isValid() ||
      !Loop->getBody() || !Resolved(Loop->getCond()) ||
      !Loop->getCond()->getType()->isBooleanType() || !Resolved(Loop->getInc()))
    return std::nullopt;
  auto Variable = [&](const DeclStmt *Statement) -> const VarDecl * {
    if (!Statement || !Statement->isSingleDecl())
      return nullptr;
    const auto *V = dyn_cast<VarDecl>(Statement->getSingleDecl());
    if (!V || V->getKind() != Decl::Var || V->isInvalidDecl() ||
        !V->isLocalVarDecl() || !V->hasLocalStorage() ||
        V->getType().isNull() || !Resolved(V->getInit()))
      return nullptr;
    return V;
  };
  RangeForComponents Parts{Variable(Loop->getRangeStmt()),
                           Variable(Loop->getBeginStmt()),
                           Variable(Loop->getEndStmt()),
                           Variable(Loop->getLoopVarStmt())};
  if (!Parts.Range || !Parts.Begin || !Parts.End || !Parts.Variable ||
      !Parts.Range->isImplicit() || !Parts.Begin->isImplicit() ||
      !Parts.End->isImplicit() || Parts.Variable->isImplicit() ||
      !Parts.Variable->isCXXForRangeDecl() ||
      !Parts.Range->getType()->isReferenceType())
    return std::nullopt;
  std::set<const VarDecl *> Identities;
  for (const auto *V : {Parts.Range, Parts.Begin, Parts.End, Parts.Variable})
    if (!Identities.insert(V->getCanonicalDecl()).second)
      return std::nullopt;
  return Parts;
}
bool Adapter::registerRangeFor(const CXXForRangeStmt *Loop) {
  const auto Parts = rangeForComponents(Loop);
  if (!S.coreV2() || !Parts || !S.owns(Sources, Loop->getForLoc()))
    return false;
  for (const auto *V : {Parts->Range, Parts->Begin, Parts->End}) {
    auto Found = RangeDeclarations.find(V->getCanonicalDecl());
    if (Found != RangeDeclarations.end() && Found->second != Loop)
      return false;
  }
  for (const auto *V : {Parts->Range, Parts->Begin, Parts->End})
    RangeDeclarations.emplace(V->getCanonicalDecl(), Loop);
  return true;
}
const CXXForRangeStmt *Adapter::rangeForOwner(const VarDecl *Variable) const {
  if (!S.coreV2() || !Variable)
    return nullptr;
  auto Found = RangeDeclarations.find(Variable->getCanonicalDecl());
  if (Found == RangeDeclarations.end() || !S.owns(Sources, Found->second->getForLoc()))
    return nullptr;
  const auto Parts = rangeForComponents(Found->second);
  if (!Parts)
    return nullptr;
  for (const auto *V : {Parts->Range, Parts->Begin, Parts->End})
    if (Variable->getCanonicalDecl() == V->getCanonicalDecl())
      return Found->second;
  return nullptr;
}
const VarDecl *Adapter::temporaryOwner(const MaterializeTemporaryExpr *Temporary) {
  if (const auto *Owner = automaticTemporaryOwner(Temporary, Context))
    return Owner;
  if (!temporaryShape(Temporary, Context) || Temporary->getStorageDuration() != SD_Automatic)
    return nullptr;
  const auto *Owner = dyn_cast_or_null<VarDecl>(Temporary->getExtendingDecl());
  const auto *Loop = rangeForOwner(Owner);
  const auto Parts = rangeForComponents(Loop);
  // Only __range's exact declaration can extend a temporary to loop scope.
  // The other registered declarations are ordinary iterator owners.
  return Parts && Owner->getCanonicalDecl() == Parts->Range->getCanonicalDecl()
             ? Owner->getCanonicalDecl() : nullptr;
}
const Expr *referenceListInitializer(const InitListExpr *List, ASTContext &Context) {
  if (!List)
    return nullptr;
  if (List->isSyntacticForm() && List->getSemanticForm())
    List = List->getSemanticForm();
  // isTransparent() asserts semantic form and a single element for glvalues.
  if (!List->isSemanticForm() || !List->isGLValue() || List->getNumInits() != 1 ||
      !List->getInit(0) || List->hasArrayFiller() || List->hasDesignatedInit() ||
      List->getInitializedFieldInUnion() || !List->isTransparent())
    return nullptr;
  const auto *Init = List->getInit(0);
  return Context.hasSameType(List->getType(), Init->getType()) &&
                 List->getValueKind() == Init->getValueKind()
             ? Init : nullptr;
}

const InitListExpr *emptyVoidInitializer(const Expr *E) {
  const auto *C = dyn_cast_or_null<CXXFunctionalCastExpr>(E);
  if (!C || C->getCastKind() != CK_ToVoid || !C->isPRValue() ||
      C->getType().isNull() || !C->getType()->isVoidType() ||
      C->getTypeAsWritten().isNull() || !C->getTypeAsWritten()->isVoidType() ||
      C->isTypeDependent() || C->isValueDependent() ||
      C->isInstantiationDependent())
    return nullptr;
  const auto *List = dyn_cast_or_null<InitListExpr>(C->getSubExpr());
  // Sema leaves void{}'s empty list untyped, with no alternate form. It is
  // both syntactic and semantic; no generic typeless expression is admitted.
  if (!List || !List->isPRValue() || List->getNumInits() ||
      List->hasArrayFiller() || List->hasDesignatedInit() ||
      List->getInitializedFieldInUnion() || List->getSemanticForm() ||
      List->getSyntacticForm() ||
      (!List->getType().isNull() && !List->getType()->isVoidType()))
    return nullptr;
  return List;
}

static const NamedDecl *templateSourceOwner(const Decl *Associated) {
  if (!Associated)
    return nullptr;
  if (const auto *Alias = dyn_cast<TypeAliasTemplateDecl>(Associated))
    return Alias;
  if (const auto *Function = dyn_cast<FunctionDecl>(Associated))
    return concreteFreeFunctionTemplate(Function) ? Function->getPrimaryTemplate() : nullptr;
  if (const auto *Template = dyn_cast<FunctionTemplateDecl>(Associated))
    return Template->getTemplatedDecl()->getKind() == Decl::Function ? Template : nullptr;
  if (const auto *Template = dyn_cast<ClassTemplateDecl>(Associated))
    return Template;
  if (const auto *Record = dyn_cast<CXXRecordDecl>(Associated))
    return classTemplatePattern(Record);
  return nullptr;
}

static const NamedDecl *scalarTemplateOwner(const SubstNonTypeTemplateParmExpr *E) {
  return E ? templateSourceOwner(E->getAssociatedDecl()) : nullptr;
}

static bool supportedPackDeclaration(const NamedDecl *Pack) {
  if (!Pack || Pack->isInvalidDecl() || Pack->hasAttrs())
    return false;
  if (const auto *Type = dyn_cast<TemplateTypeParmDecl>(Pack))
    return !Type->getDepth() && Type->isParameterPack() && !Type->hasTypeConstraint();
  if (const auto *Value = dyn_cast<NonTypeTemplateParmDecl>(Pack))
    return !Value->getDepth() && Value->isParameterPack();
  if (const auto *Parameter = dyn_cast<ParmVarDecl>(Pack))
    return Parameter->isParameterPack();
  return false;
}

std::optional<unsigned> concretePackSize(const SizeOfPackExpr *E) {
  if (!E || E->getType().isNull() || !E->isPRValue() ||
      !E->getType()->isIntegralOrEnumerationType() || E->isTypeDependent() ||
      E->isValueDependent() || E->isInstantiationDependent() ||
      E->isPartiallySubstituted() || !supportedPackDeclaration(E->getPack()))
    return std::nullopt;
  unsigned Size = E->getPackLength();
  return Size <= 64 ? std::optional<unsigned>(Size) : std::nullopt;
}

const Expr *scalarTemplateReplacement(const SubstNonTypeTemplateParmExpr *E,
                                      ASTContext &Context) {
  if (!E || E->getType().isNull() || !E->isPRValue() ||
      !E->getType()->isIntegralOrEnumerationType() || E->isTypeDependent() ||
      E->isValueDependent() || E->isInstantiationDependent() ||
      E->isReferenceParameter())
    return nullptr;
  const auto *Primary = scalarTemplateOwner(E);
  if (!Primary || !templateSourceParameters(Primary) ||
      E->getIndex() >= templateSourceParameters(Primary)->size())
    return nullptr;
  // getParameter() performs an unchecked index and cast in pinned Clang.
  const auto *Parameter = dyn_cast<NonTypeTemplateParmDecl>(
      templateSourceParameters(Primary)->getParam(E->getIndex()));
  if (!Parameter || Parameter->getDepth() ||
      Parameter->isParameterPack() != E->getPackIndex().has_value() ||
      (E->getPackIndex() && *E->getPackIndex() >= 64))
    return nullptr;
  // Pack indices count from the end in Clang. The replacement is already the
  // selected scalar; never use this index to subscript a forward argument list.
  const auto *Replacement = E->getReplacement();
  if (!Replacement || Replacement->getType().isNull() ||
      !Replacement->isPRValue() || Replacement->isTypeDependent() ||
      Replacement->isValueDependent() || Replacement->isInstantiationDependent() ||
      !Context.hasSameType(E->getType(), Replacement->getType()))
    return nullptr;
  APValue Value;
  return Replacement->isCXX11ConstantExpr(Context, &Value) && Value.isInt()
             ? Replacement : nullptr;
}

static bool lazyTemplateDefault(const ParmVarDecl *P) {
  if (!P || !P->hasDefaultArg() || P->hasUnparsedDefaultArg() ||
      !P->hasUninstantiatedDefaultArg())
    return false;
  const auto *Function = dyn_cast<FunctionDecl>(P->getDeclContext());
  return concreteFreeFunctionTemplate(Function) || concreteMemberFunction(Function);
}

const Expr *defaultArgumentInitializer(const ParmVarDecl *P, ASTContext &Context) {
  if (!P || P->isImplicit() || P->isInvalidDecl() || !P->hasDefaultArg() ||
      P->hasUnparsedDefaultArg() || P->hasUninstantiatedDefaultArg() ||
      P->getType().isNull() || P->getType()->isDependentType())
    return nullptr;
  const auto *F = dyn_cast<FunctionDecl>(P->getDeclContext());
  if (!F || (F->getTemplatedKind() != FunctionDecl::TK_NonTemplate &&
             !concreteFreeFunctionTemplate(F) && !concreteMemberFunction(F)) ||
      P->getFunctionScopeIndex() >= F->getNumParams() ||
      F->getParamDecl(P->getFunctionScopeIndex()) != P)
    return nullptr;
  const auto *Init = P->getDefaultArg();
  if (!Init || Init->getType().isNull() || Init->isTypeDependent() ||
      Init->isValueDependent() || Init->isInstantiationDependent() ||
      !Context.hasSameUnqualifiedType(P->getType().getNonReferenceType(),
                                      Init->getType()))
    return nullptr;
  return Init;
}

const Expr *selectedDefaultArgument(const CXXDefaultArgExpr *Default,
                                    ASTContext &Context) {
  if (!Default || !defaultArgumentInitializer(Default->getParam(), Context) ||
      Default->getType().isNull() || Default->isTypeDependent() ||
      Default->isValueDependent() || Default->isInstantiationDependent())
    return nullptr;
  // getExpr() selects Sema's per-use rewrite when one exists. Its original
  // parameter initializer remains independently checked at the declaration.
  const auto *Init = Default->getExpr();
  if (!Init || Init->getType().isNull() || Init->isTypeDependent() ||
      Init->isValueDependent() || Init->isInstantiationDependent() ||
      !Context.hasSameType(Default->getType(), Init->getType()) ||
      Default->getValueKind() != Init->getValueKind())
    return nullptr;
  return Init;
}

bool ordinaryDestructor(const CXXDestructorDecl *D) {
  if (!D || D->isImplicit() || !D->isUserProvided() || D->isVirtual() ||
      D->isDeletedAsWritten() || D->isExplicitlyDefaulted() ||
      (D->getTemplatedKind() != FunctionDecl::TK_NonTemplate &&
       !concreteMemberFunction(D)))
    return false;
  // A spelled destructor without noexcept still has an implicit exception
  // specification. Keep that lazy state; written forms use the standard gate.
  const auto *Info = D->getTypeSourceInfo();
  if (!Info)
    return false;
  auto Location = Info->getTypeLoc().getAs<FunctionProtoTypeLoc>();
  return Location && (Location.getExceptionSpecRange().isInvalid() ||
      standardExceptionSpecification(D->getType()->getAs<FunctionProtoType>()));
}

bool needsDestruction(QualType T) {
  while (const auto *Array = dyn_cast<ArrayType>(T.getCanonicalType().getTypePtr()))
    T = Array->getElementType();
  const auto *Record = T->getAsCXXRecordDecl();
  return Record && !Record->hasTrivialDestructor();
}

std::string Adapter::destructionName(const CXXRecordDecl *Record) {
  return name(Record) + "_destroy";
}

void Adapter::requireDestruction(const CXXRecordDecl *Record, SourceLocation L) {
  const auto *Definition = Record ? Record->getDefinition() : nullptr;
  if (!Definition) {
    reject(L, "destruction", "A complete admitted record is required.");
    throw Failure{};
  }
  if (RequiredDestructions.insert(Definition).second) {
    chargeExpansion(1, L);
    Destructions.push_back(Definition);
  }
}

const Expr *directMethodReference(const CallExpr *Call) {
  const auto *M = dyn_cast_or_null<CXXMethodDecl>(Call->getDirectCallee());
  if (!callableMethod(M))
    return nullptr;
  const Expr *E = Call->getCallee();
  while (true) {
    if (const auto *P = dyn_cast<ParenExpr>(E)) {
      E = P->getSubExpr();
      continue;
    }
    if (const auto *C = dyn_cast<ImplicitCastExpr>(E);
        C && C->getCastKind() == CK_FunctionToPointerDecay) {
      E = C->getSubExpr();
      continue;
    }
    break;
  }
  const ValueDecl *D = nullptr;
  if (const auto *Member = dyn_cast<MemberExpr>(E))
    D = Member->getMemberDecl();
  else if (const auto *Reference = dyn_cast<DeclRefExpr>(E);
           Reference && (M->isStatic() ||
                         (isa<CXXOperatorCallExpr>(Call) &&
                          (ordinaryOperator(M) || supportedAssignment(M)))))
    D = Reference->getDecl();
  return D && D->getCanonicalDecl() == M->getCanonicalDecl() ? E : nullptr;
}

std::string digest(llvm::StringRef Text) {
  return llvm::toHex(llvm::SHA256::hash(llvm::arrayRefFromStringRef(Text)),
                     true);
}

bool validExportName(llvm::StringRef N) {
  if (N.empty() || N.starts_with("_") || N.starts_with("nct_") ||
      N.starts_with("nc_") || N.starts_with("neverc_") || N.contains("__"))
    return false;
  for (char C : N)
    if (!llvm::isAlnum(C) && C != '_')
      return false;
  if (llvm::isDigit(N.front()))
    return false;
  static const std::set<std::string> Reserved{
      "auto",          "bool",      "break",         "case",         "char",
      "const",         "constexpr", "continue",      "default",      "do",
      "double",        "else",      "enum",          "extern",       "false",
      "float",         "for",       "goto",          "if",           "inline",
      "int",           "long",      "nullptr",       "register",     "restrict",
      "return",        "short",     "signed",        "sizeof",       "static",
      "static_assert", "struct",    "switch",        "thread_local", "true",
      "typedef",       "typeof",    "typeof_unqual", "union",        "unsigned",
      "void",          "volatile",  "while",         "alignas",      "alignof",
      "string",        "i8",        "i16",           "i32",          "i64",
      "i128",          "u8",        "u16",           "u32",          "u64",
      "u128",          "isize",     "usize"};
  return !Reserved.count(N.str());
}

void State::diagnose(llvm::StringRef Code, llvm::StringRef Construct,
                     llvm::StringRef Reason, llvm::StringRef Guidance,
                     unsigned Line, unsigned Column, llvm::StringRef File) {
  if (Diagnostics.size() >= 100)
    return;
  Diagnostics.push_back(json::Object{
      {"code", Code.str()},
      {"file",
       File.empty() ? (Relative.empty() ? Source : Relative) : File.str()},
      {"line", Line ? Line : 1},
      {"column", Column ? Column : 1},
      {"construct", Construct.str()},
      {"reason", Reason.str()},
      {"guidance", Guidance.str()}});
}

json::Object Adapter::loc(SourceLocation L) const {
  auto P = Sources.getPresumedLoc(Sources.getExpansionLoc(L));
  auto File = S.project() ? S.sourcePath(Sources, L) : S.Relative;
  return json::Object{{"file", File.empty() ? S.Relative : File},
                      {"line", P.isValid() ? P.getLine() : 1},
                      {"column", P.isValid() ? P.getColumn() : 1}};
}

void Adapter::reject(SourceLocation L, llvm::StringRef Construct,
                     llvm::StringRef Reason, llvm::StringRef Code) {
  auto P = Sources.getPresumedLoc(Sources.getExpansionLoc(L));
  S.diagnose(
      Code, Construct, Reason,
      "Rewrite using the documented constructs in the selected profile.",
      P.isValid() ? P.getLine() : 1, P.isValid() ? P.getColumn() : 1,
      S.project() ? S.sourcePath(Sources, L) : S.Relative);
}

std::string Adapter::name(const NamedDecl *D) {
  auto *Canonical = D->getCanonicalDecl();
  auto Found = Names.find(Canonical);
  if (Found != Names.end())
    return Found->second;
  std::string Name;
  if (const auto *F = dyn_cast<FunctionDecl>(D);
      F && ((F->isExternC() && F->getFormalLinkage() != Linkage::Internal) ||
            F->isMain())) {
    Name = F->getNameAsString();
  } else {
    Name = "nct_" + digest(identity(D)).substr(0, 24);
  }
  Names.emplace(Canonical, Name);
  return Name;
}

std::size_t Adapter::storageUnits(QualType T, unsigned Depth) {
  constexpr std::size_t Limit = 200000;
  if (Depth > 64)
    return Limit + 1;
  if (const auto *Array = Context.getAsConstantArrayType(T)) {
    auto Count = Array->getSize().getLimitedValue(65537);
    auto Element = storageUnits(Array->getElementType(), Depth + 1);
    return !Count || Count > 65536 || Element > Limit / Count
               ? Limit + 1 : Element * Count;
  }
  if (const auto *R = T->getAsCXXRecordDecl(); R && R->getDefinition()) {
    R = R->getDefinition();
    auto Found = StorageUnits.find(R);
    if (Found != StorageUnits.end())
      return Found->second;
    StorageUnits.emplace(R, Limit + 1);
    std::size_t Units = R->field_empty() ? 1 : 0;
    for (const auto *F : R->fields()) {
      auto Added = storageUnits(F->getType(), Depth + 1);
      if (Added > Limit - Units)
        return Limit + 1;
      Units += Added;
    }
    StorageUnits[R] = Units;
    return Units;
  }
  // Pointers/references consume storage but do not expand their pointees.
  return 1;
}

void Adapter::chargeExpansion(std::size_t Nodes, SourceLocation L) {
  if (!S.coreV2())
    return;
  constexpr std::size_t Limit = 200000;
  if (Nodes > Limit - ExpandedNodes) {
    reject(L, "initialization expansion",
           "Expanded initializer/assignment nodes exceed the frontend budget.");
    throw Failure{};
  }
  ExpandedNodes += Nodes;
}

std::string Adapter::type(QualType T, SourceLocation L, bool AllowVoid,
                          unsigned Depth) {
  if (Depth > 64) {
    reject(L, "type", "Pointer type nesting exceeds the protocol limit.");
    return {};
  }
  if (T.isVolatileQualified() || T->isAtomicType() || T.isRestrictQualified()) {
    reject(L, "qualified type",
           "Volatile, atomic and restrict-qualified types are unsupported.");
    return {};
  }
  const Type *C = T.getCanonicalType().getTypePtr();
  if (S.coreV2())
    if (const auto *Array = Context.getAsConstantArrayType(T)) {
      auto Count = Array->getSize().getLimitedValue(65537);
      if (!Count || Count > 65536 || storageUnits(T) > 200000) {
        reject(L, "array extent", "Fixed arrays exceed the extent/storage limit.");
        return {};
      }
      auto Element = type(Array->getElementType(), L, false, Depth + 1);
      if (Element.empty())
        return {};
      return "arr:" + std::to_string(Count) + ":" + Element;
    }
  if (S.coreV2() && (C->isPointerType() || C->isReferenceType())) {
    QualType Pointee = C->getPointeeType();
    auto Element = type(Pointee, L, C->isPointerType(), Depth + 1);
    if (Element.empty())
      return {};
    return std::string(Pointee.isConstQualified() ? "cptr:" : "ptr:") + Element;
  }
  if (const auto *B = dyn_cast<BuiltinType>(C)) {
    if (S.coreV2() && B->isInteger() && B->getKind() != BuiltinType::Bool) {
      unsigned Bits = Context.getTypeSize(T);
      bool Signed = T->isSignedIntegerType();
      QualType Carrier;
      switch (Bits) {
      case 8:
        Carrier = Signed ? Context.SignedCharTy : Context.UnsignedCharTy;
        break;
      case 16:
        Carrier = Signed ? Context.ShortTy : Context.UnsignedShortTy;
        break;
      case 32:
        Carrier = Signed ? Context.IntTy : Context.UnsignedIntTy;
        break;
      case 64:
        Carrier = Signed ? Context.LongLongTy : Context.UnsignedLongLongTy;
        break;
      default:
        reject(L, "integer width",
               "Only 8-, 16-, 32- and 64-bit integers are supported.");
        return {};
      }
      if (Context.getTypeSize(T) != Context.getTypeSize(Carrier) ||
          Context.getTypeAlign(T) != Context.getTypeAlign(Carrier)) {
        reject(L, "integer layout",
               "Source integer and emission carrier layouts differ.", "TR0204");
        return {};
      }
      return Bits == 32 ? (Signed ? "int" : "uint")
                        : std::string(Signed ? "i" : "u") + std::to_string(Bits);
    }
    switch (B->getKind()) {
    case BuiltinType::Int:
      return "int";
    case BuiltinType::UInt:
      return "uint";
    case BuiltinType::Bool:
      return "bool";
    case BuiltinType::Double:
      if (S.math())
        return "double";
      break;
    case BuiltinType::Void:
      if (AllowVoid)
        return "void";
      break;
    default:
      break;
    }
  }
  if (S.coreV2())
    if (const auto *E = dyn_cast<EnumType>(C)) {
      QualType Underlying = E->getDecl()->getIntegerType();
      if (!Underlying.isNull() && Underlying->isIntegerType())
        return type(Underlying, L);
      reject(L, "enum underlying type",
             "Core v2 enums require an established supported integral underlying type.");
      return {};
    }
  if (const auto *R = C->getAs<RecordType>()) {
    const auto *D = dyn_cast<CXXRecordDecl>(R->getDecl());
    if (D && D->getDefinition() && !D->isUnion())
      return name(D);
  }
  reject(L, "type",
         "Only the documented scalar, record and core v2 pointer/reference "
         "types are admitted.");
  return {};
}

json::Object Adapter::literal(const llvm::APSInt &V, llvm::StringRef T,
                              SourceLocation L) {
  json::Object O{{"kind", "literal"}, {"type", T.str()}, {"loc", loc(L)}};
  if (T == "bool")
    O["value"] = !V.isZero();
  else {
    llvm::SmallString<32> Text;
    auto Value = V.extOrTrunc(integerBits(T));
    Value.setIsUnsigned(unsignedInteger(T));
    Value.toString(Text, 10);
    O["value"] = Text.str().str();
  }
  return O;
}

json::Object Adapter::zero(QualType T, SourceLocation L) {
  std::string Kind = type(T, L);
  if (Kind.empty())
    throw Failure{};
  chargeExpansion(2 + std::count(Kind.begin(), Kind.end(), ':'), L);
  if (const auto *Array = Context.getAsConstantArrayType(T); Array && S.coreV2()) {
    json::Array Values;
    auto Count = Array->getSize().getLimitedValue(65537);
    for (uint64_t I = 0; I < Count; ++I)
      Values.push_back(zero(Array->getElementType(), L));
    return json::Object{{"kind", "aggregate"}, {"type", Kind},
                        {"args", std::move(Values)}, {"loc", loc(L)}};
  }
  if (Kind == "double")
    return floatingLiteral(llvm::APFloat::getZero(llvm::APFloat::IEEEdouble()),
                           L);
  if (T->isPointerType())
    return json::Object{{"kind", "null"}, {"type", Kind}, {"loc", loc(L)}};
  if (const auto *R = T->getAsCXXRecordDecl()) {
    json::Array Args;
    for (const auto *F : R->getDefinition()->fields())
      Args.push_back(zero(F->getType(), L));
    return json::Object{{"kind", "aggregate"},
                        {"type", Kind},
                        {"args", std::move(Args)},
                        {"loc", loc(L)}};
  }
  return literal(llvm::APSInt(integerBits(Kind), unsignedInteger(Kind)), Kind, L);
}

json::Object Adapter::constant(const APValue &V, QualType T, SourceLocation L) {
  auto Kind = type(T, L);
  if (V.isInt())
    return literal(V.getInt(), Kind, L);
  if (V.isFloat() && S.math())
    return floatingLiteral(V.getFloat(), L);
  if (V.isStruct()) {
    json::Array Args;
    unsigned I = 0;
    for (const auto *F : T->getAsCXXRecordDecl()->getDefinition()->fields())
      Args.push_back(constant(V.getStructField(I++), F->getType(), L));
    return json::Object{{"kind", "aggregate"},
                        {"type", Kind},
                        {"args", std::move(Args)},
                        {"loc", loc(L)}};
  }
  reject(L, "constant initializer",
         "Initializer is not a fully defined scalar/aggregate constant.");
  throw Failure{};
}

class Allowlist : public RecursiveASTVisitor<Allowlist> {
  Adapter &A;
  std::map<const CXXRecordDecl *, bool> HasArray;
  std::set<const Expr *> DirectMethodCallees, GeneratedBuiltinCallees;
  std::set<const CallExpr *> GeneratedArrayAssignments;
  std::map<const OpaqueValueExpr *, const Expr *> ArraySources;
  unsigned ArrayIndexDepth = 0;
  const CXXMethodDecl *CurrentMethod = nullptr;
  const FunctionDecl *CurrentFunction = nullptr;
  const FieldDecl *CurrentDefaultField = nullptr;
  SourceLocation ImplicitInitializerOwner;
  const TemplateParameterList *TemplateParameterTypeSource = nullptr;
  std::map<const NamedDecl *, const NamedDecl *> PackOwners;
  using TypeSourceKey = std::pair<const Type *, unsigned>;
  using FunctionSourceKey = std::pair<const FunctionDecl *, unsigned>;
  std::map<TypeSourceKey, std::vector<const TemplateUseSource *>> TypeSources;
  std::map<FunctionSourceKey, std::vector<const TemplateUseSource *>> FunctionSources;
  std::map<const Decl *, std::vector<const TemplateUseSource *>> ClassSources;
  std::map<const FunctionDecl *, std::vector<const FunctionSpecializationSource *>> SpecializationSources;
  struct ClassPartialSource {
    const TemplateUseSource *Deduction = nullptr, *Pattern = nullptr;
    bool Conflict = false;
  };
  std::map<const TemplateArgumentList *, ClassPartialSource> PartialSources;
  std::set<const CXXRecordDecl *> CheckedPartialRecords;
  std::vector<const CXXRecordDecl *> ActivePartialRecords;
  struct TemplateSourceFrame {
    const TemplateUseSource &Source;
    std::vector<bool> Ready;
  };
  std::vector<TemplateSourceFrame *> TemplateFrames;
  std::vector<const TemplateUseSource *> ActiveTemplateUses;
  std::set<const Expr *> CheckedSemanticInitializers;
  std::set<const InitListExpr *> EmptyVoidLists;
  std::set<const CXXConstructExpr *> CheckedConstructions;
  std::set<const VarDecl *> CheckedScalarGlobals;
  std::set<const UsingShadowDecl *> CheckedUsingShadows;
  std::set<const FunctionDecl *> CheckedTemplateDeclarations;
  std::set<const VarDecl *> CheckedTemplateStaticDeclarations;
  std::set<const Expr *> CheckedDiscardedResults, DiscardedStaticValues;
  std::set<const Decl *> QueuedGeneratedMethods;
  std::set<const ClassTemplateSpecializationDecl *> CheckedClassTemplateDeclarations;
  std::vector<const CXXMethodDecl *> GeneratedMethods;
  bool owned(const Decl *D) {
    if (!D)
      return false;
    if (!D->isImplicit())
      return A.S.owns(A.Sources, D->getLocation());
    return A.rangeForOwner(dyn_cast<VarDecl>(D)) != nullptr;
  }
  bool templateParametersShape(const TemplateParameterList *Parameters) {
    if (!Parameters || !Parameters->size() || Parameters->size() > 64 ||
        Parameters->hasAssociatedConstraints())
      return false;
    for (const auto *Parameter : *Parameters) {
      if (!owned(Parameter) || Parameter->isInvalidDecl() || Parameter->hasAttrs())
        return false;
      if (const auto *Type = dyn_cast<TemplateTypeParmDecl>(Parameter)) {
        if (Type->getDepth() || Type->hasTypeConstraint())
          return false;
        continue;
      }
      const auto *Value = dyn_cast<NonTypeTemplateParmDecl>(Parameter);
      if (!Value || Value->getDepth() ||
          Value->getType().isNull())
        return false;
      auto T = Value->getType();
      if (T.isVolatileQualified() || T.isRestrictQualified() ||
          (!T->isDependentType() && !T->isUndeducedAutoType() &&
           !T->isIntegralOrEnumerationType()))
        return false;
    }
    return true;
  }
  bool classTemplateFunctionShape(const CXXMethodDecl *M) {
    if (!M || !owned(M) || M->isInvalidDecl() || M->hasAttrs() ||
        M->getFriendObjectKind() || M->getDescribedFunctionTemplate() ||
        M->getPrimaryTemplate() || M->isVirtual() || M->isVariadic() ||
        M->isDeletedAsWritten() || M->isConsteval() ||
        M->isExplicitObjectMemberFunction() || M->getTrailingRequiresClause() ||
        M->getMethodQualifiers().hasVolatile() ||
        M->getMethodQualifiers().hasRestrict())
      return false;
    const bool Defaulted = defaultedSpecialMember(M);
    if (const auto *Defaulting = defaultedDeclaration(M))
      if (!Defaulted || !owned(Defaulting))
        return false;
    if (const auto *Constructor = dyn_cast<CXXConstructorDecl>(M)) {
      if ((!Defaulted && !Constructor->isUserProvided()) ||
          Constructor->isDelegatingConstructor() ||
          Constructor->isInheritingConstructor() || Constructor->isStatic() ||
          Constructor->getMethodQualifiers().getCVRQualifiers())
        return false;
      // In a dependent primary, Sema represents delegation as a type/base
      // initializer until instantiation. These class templates admit no bases.
      for (const auto *Init : Constructor->inits())
        if (Init->isWritten() && !Init->isAnyMemberInitializer())
          return false;
    } else if (const auto *Destructor = dyn_cast<CXXDestructorDecl>(M)) {
      if ((!Defaulted && !Destructor->isUserProvided()) || Destructor->isStatic() ||
          Destructor->getNumParams() ||
          Destructor->getMethodQualifiers().getCVRQualifiers())
        return false;
    } else if (const auto *Conversion = dyn_cast<CXXConversionDecl>(M)) {
      if (!Conversion->isUserProvided() || Conversion->isStatic() ||
          Conversion->getNumParams())
        return false;
    } else if (M->getKind() != Decl::CXXMethod) {
      return false;
    } else if (!M->getIdentifier() && !Defaulted) {
      if (!M->isUserProvided() || M->isStatic() ||
          !ordinaryOperatorKind(M->getOverloadedOperator()))
        return false;
    }
    A.chargeExpansion(1, M->getLocation());
    for (const auto *Parameter : M->parameters()) {
      A.chargeExpansion(1, Parameter->getLocation());
      if (!owned(Parameter) || Parameter->isInvalidDecl() ||
          Parameter->hasAttrs())
        return false;
    }
    return true;
  }
  bool classTemplateStaticDataShape(const VarDecl *V) {
    if (!V || V->getKind() != Decl::Var || !owned(V) || !V->getIdentifier() ||
        V->isInvalidDecl() || V->hasAttrs() || !V->isStaticDataMember() ||
        V->getDescribedVarTemplate() || V->getTLSKind() != VarDecl::TLS_None ||
        V->getType().isNull() || V->getType().isVolatileQualified() ||
        V->getType().isRestrictQualified())
      return false;
    auto T = V->getType();
    if (T->isPointerType() || T->isReferenceType() || T->isArrayType() || T->isRecordType())
      return false;
    return T->isDependentType() || T->isUndeducedAutoType() ||
           T->isIntegralOrEnumerationType();
  }
  bool classTemplateMembers(const CXXRecordDecl *D) {
    for (const auto *Member : D->decls()) {
      A.chargeExpansion(1, Member->getLocation());
      if (Member->isImplicit())
        continue;
      if (!owned(Member) || Member->isInvalidDecl() || Member->hasAttrs() ||
          (!isa<FieldDecl, TypedefNameDecl, EnumDecl, EnumConstantDecl,
                StaticAssertDecl, AccessSpecDecl>(Member) &&
           !classTemplateStaticDataShape(dyn_cast<VarDecl>(Member)) &&
           !classTemplateFunctionShape(dyn_cast<CXXMethodDecl>(Member))))
        return false;
    }
    return true;
  }
  bool classTemplateShape(const ClassTemplateDecl *D) {
    if (!D || !owned(D) || D->isInvalidDecl() || D->hasAttrs() ||
        !isa<TranslationUnitDecl, NamespaceDecl>(D->getDeclContext()) ||
        !isa<TranslationUnitDecl, NamespaceDecl>(D->getLexicalDeclContext()) ||
        !templateParametersShape(D->getTemplateParameters()))
      return false;
    const auto *Pattern = D->getTemplatedDecl();
    if (!owned(Pattern) || Pattern->isInvalidDecl() || Pattern->hasAttrs() ||
        Pattern->getKind() != Decl::CXXRecord || !Pattern->getIdentifier() ||
        Pattern->isUnion())
      return false;
    const auto *Definition = Pattern->getDefinition();
    // getNumBases() needs definition data, which a forward primary lacks.
    return !Definition ||
           (owned(Definition) && !Definition->isInvalidDecl() &&
            !Definition->hasAttrs() && !Definition->getNumBases() &&
            classTemplateMembers(Definition));
  }
  bool classPartialShape(const ClassTemplatePartialSpecializationDecl *D) {
    if (!D || !owned(D) || D->isInvalidDecl() || D->hasAttrs() ||
        !D->getIdentifier() || D->isUnion() ||
        !isa<TranslationUnitDecl, NamespaceDecl>(D->getDeclContext()) ||
        !isa<TranslationUnitDecl, NamespaceDecl>(D->getLexicalDeclContext()) ||
        D->getInstantiatedFromMember() || D->isMemberSpecialization() ||
        D->isClassScopeExplicitSpecialization() ||
        !classTemplateShape(D->getSpecializedTemplate()) ||
        !templateParametersShape(D->getTemplateParameters()) ||
        !D->getTemplateArgsAsWritten())
      return false;
    for (const auto *Parameter : *D->getTemplateParameters()) {
      if (const auto *Type = dyn_cast<TemplateTypeParmDecl>(Parameter);
          Type && Type->hasDefaultArgument())
        return false;
      if (const auto *Value = dyn_cast<NonTypeTemplateParmDecl>(Parameter);
          Value && Value->hasDefaultArgument())
        return false;
    }
    const auto *Definition = D->getDefinition();
    return !Definition ||
           (owned(Definition) && !Definition->isInvalidDecl() &&
            !Definition->hasAttrs() && !Definition->getNumBases() &&
            classTemplateMembers(Definition));
  }
  bool classPatternShape(const NamedDecl *D) {
    return classTemplateShape(dyn_cast_or_null<ClassTemplateDecl>(D)) ||
           classPartialShape(dyn_cast_or_null<ClassTemplatePartialSpecializationDecl>(D));
  }
  bool aliasTemplateShape(const TypeAliasTemplateDecl *D) {
    if (!D || !owned(D) || D->isInvalidDecl() || D->hasAttrs() ||
        !isa<TranslationUnitDecl, NamespaceDecl>(D->getDeclContext()) ||
        !isa<TranslationUnitDecl, NamespaceDecl>(D->getLexicalDeclContext()) ||
        !templateParametersShape(D->getTemplateParameters()))
      return false;
    const auto *Pattern = D->getTemplatedDecl();
    return owned(Pattern) && !Pattern->isInvalidDecl() && !Pattern->hasAttrs() &&
           Pattern->getIdentifier() && Pattern->getTypeSourceInfo();
  }
  bool functionTemplateShape(const FunctionTemplateDecl *D) {
    if (!D || !owned(D) || D->isInvalidDecl() ||
        !isa<TranslationUnitDecl, NamespaceDecl>(D->getDeclContext()) ||
        !isa<TranslationUnitDecl, NamespaceDecl>(D->getLexicalDeclContext()))
      return false;
    const auto *Parameters = D->getTemplateParameters();
    const auto *Pattern = D->getTemplatedDecl();
    if (!templateParametersShape(Parameters) || D->isAbbreviated() ||
        !owned(Pattern) || Pattern->isInvalidDecl() ||
        !ordinaryFreeFunctionName(Pattern) ||
        Pattern->isVariadic() || Pattern->isDeletedAsWritten() ||
        Pattern->isDefaulted() || Pattern->isConsteval() ||
        Pattern->getTrailingRequiresClause() || Pattern->hasAttrs())
      return false;
    for (const auto *Parameter : Pattern->parameters()) {
      A.chargeExpansion(1, Parameter->getLocation());
      if (!owned(Parameter) || Parameter->isInvalidDecl() || Parameter->hasAttrs())
        return false;
    }
    return true;
  }
  bool traverseTemplateParameterSource(const TemplateParameterList *Parameters) {
    for (const auto *Parameter : *Parameters) {
      if (const auto *Value = dyn_cast<NonTypeTemplateParmDecl>(Parameter)) {
        if (const auto *Info = Value->getTypeSourceInfo()) {
          auto *Saved = TemplateParameterTypeSource;
          TemplateParameterTypeSource = Parameters;
          auto Restore = llvm::make_scope_exit([&] { TemplateParameterTypeSource = Saved; });
          if (!TraverseTypeLoc(Info->getTypeLoc()))
            return false;
        }
        // Nondependent written defaults cannot hide unsupported source. A
        // dependent default stays lazy until Sema successfully converts a use.
        if (Value->hasDefaultArgument() &&
            !Value->getDefaultArgument().getArgument().isInstantiationDependent() &&
            !TraverseTemplateArgumentLoc(Value->getDefaultArgument()))
          return false;
      } else if (const auto *Type = dyn_cast<TemplateTypeParmDecl>(Parameter);
                 Type && Type->hasDefaultArgument() &&
                 !Type->getDefaultArgument().getArgument().isInstantiationDependent()) {
        if (!TraverseTemplateArgumentLoc(Type->getDefaultArgument()))
          return false;
      }
    }
    return true;
  }
  void checkTemplateArguments(const TemplateParameterList *Parameters,
                              const TemplateArgumentList &Arguments,
                              SourceLocation L) {
    if (Arguments.size() != Parameters->size()) {
      A.reject(L, "template specialization", "Concrete arguments must match the primary parameter list.");
      return;
    }
    for (unsigned Index = 0; Index < Arguments.size(); ++Index) {
      const auto *Parameter = Parameters->getParam(Index);
      const auto &Argument = Arguments.get(Index);
      A.chargeExpansion(1, L);
      auto CheckElement = [&](const TemplateArgument &Element) {
        if (isa<TemplateTypeParmDecl>(Parameter)) {
          if (Element.getKind() != TemplateArgument::Type ||
              Element.getAsType().isNull() || Element.getAsType()->isDependentType())
            A.reject(L, "template argument", "A resolved supported type argument is required.");
          else
            A.type(Element.getAsType(), L, true);
        } else if (Element.getKind() != TemplateArgument::Integral ||
                   Element.getIntegralType().isNull() ||
                   Element.getIntegralType()->isDependentType() ||
                   !Element.getIntegralType()->isIntegralOrEnumerationType()) {
          A.reject(L, "template argument",
                   "A resolved integer, boolean or enum value argument is required.");
        } else {
          A.type(Element.getIntegralType(), L);
        }
      };
      if (supportedPackDeclaration(Parameter)) {
        if (Argument.getKind() != TemplateArgument::Pack || Argument.pack_size() > 64) {
          A.reject(L, "template argument pack", "A concrete pack of at most 64 supported elements is required.");
          continue;
        }
        A.chargeExpansion(Argument.pack_size(), L);
        for (const auto &Element : Argument.pack_elements())
          CheckElement(Element);
      } else {
        CheckElement(Argument);
      }
    }
  }
  void recordPack(const NamedDecl *Pack, const NamedDecl *Owner) {
    if (!supportedPackDeclaration(Pack) || !owned(Pack) || !owned(Owner))
      return;
    const auto *Canonical = cast<NamedDecl>(Owner->getCanonicalDecl());
    auto Inserted = PackOwners.emplace(Pack, Canonical);
    if (!Inserted.second && Inserted.first->second != Canonical) {
      A.reject(Pack->getLocation(), "pack origin", "A source pack has conflicting template owners.");
      throw Failure{};
    }
  }
  void recordTemplatePacks(const TemplateParameterList *Parameters,
                           const NamedDecl *Owner) {
    if (!owned(Owner) || !Parameters || Parameters->size() > 64)
      return;
    A.chargeExpansion(1 + Parameters->size(), Owner->getLocation());
    for (const auto *Parameter : *Parameters)
      recordPack(Parameter, Owner);
  }
  void recordOuterPacks(const DeclaratorDecl *D, const NamedDecl *Owner) {
    if (!owned(D) || !owned(Owner) || D->getNumTemplateParameterLists() > 1)
      return;
    for (unsigned I = 0; I < D->getNumTemplateParameterLists(); ++I)
      recordTemplatePacks(D->getTemplateParameterList(I), Owner);
  }
  void recordFunctionPacks(const FunctionDecl *Function, const NamedDecl *Owner) {
    if (!owned(Function) || !owned(Owner))
      return;
    A.chargeExpansion(1 + Function->getNumParams(), Function->getLocation());
    for (const auto *Parameter : Function->parameters())
      recordPack(Parameter, Owner);
    recordOuterPacks(Function, Owner);
  }
  void indexTemplatePackSources(const NamedDecl *Template) {
    if (!owned(Template))
      return;
    recordTemplatePacks(templateSourceParameters(Template), Template);
    if (const auto *Function = dyn_cast<FunctionTemplateDecl>(Template)) {
      recordFunctionPacks(Function->getTemplatedDecl(), Template);
    } else if (const auto *Pattern = classPatternRecord(Template)) {
      const auto *Definition = Pattern->getDefinition();
      if (!owned(Definition))
        return;
      for (const auto *Member : Definition->decls()) {
        A.chargeExpansion(1, Member->getLocation());
        if (const auto *Method = dyn_cast<CXXMethodDecl>(Member))
          recordFunctionPacks(Method, Template);
        else if (const auto *Variable = dyn_cast<VarDecl>(Member);
                 Variable && Variable->isStaticDataMember())
          recordOuterPacks(Variable, Template);
      }
    }
  }
  void indexLocalFunctionPacks(const CXXMethodDecl *Method) {
    if (!owned(Method) || !concreteLocalClassFunction(Method))
      return;
    const NamedDecl *Primary = nullptr;
    const FunctionDecl *Function = Method;
    for (unsigned Depth = 0; Depth < 64; ++Depth) {
      A.chargeExpansion(1, Function->getLocation());
      const auto *Member = dyn_cast<CXXMethodDecl>(Function);
      const auto *Owner = Member ? Member->getParent()->isLocalClass() : nullptr;
      if (!owned(Owner))
        break;
      if (concreteFreeFunctionTemplate(Owner)) {
        Primary = Owner->getPrimaryTemplate();
        break;
      }
      if ((Primary = classFunctionPattern(Owner)))
        break;
      Function = Owner;
    }
    const auto *Pattern = Method->getInstantiatedFromMemberFunction();
    if (!owned(Primary) || !owned(Pattern)) {
      A.reject(Method->getLocation(), "local pack origin",
               "A local method pack requires an owned instantiated-from method and outer primary.");
      return;
    }
    unsigned Count = 0;
    for (const auto *Declaration : Pattern->redecls()) {
      if (++Count > 64) {
        A.reject(Method->getLocation(), "local pack origin", "Local method redeclarations exceed the source limit.");
        return;
      }
      recordFunctionPacks(Declaration, Primary);
    }
  }
  bool validPackOwner(const NamedDecl *Pack) {
    auto Found = PackOwners.find(Pack);
    if (!owned(Pack) || !supportedPackDeclaration(Pack) || Found == PackOwners.end())
      return false;
    return functionTemplateShape(dyn_cast<FunctionTemplateDecl>(Found->second)) ||
           classPatternShape(Found->second) ||
           aliasTemplateShape(dyn_cast<TypeAliasTemplateDecl>(Found->second));
  }
  bool parameterTypeQueryMetadata(const UnaryExprOrTypeTraitExpr *Query) {
    if (!TemplateParameterTypeSource || !Query->isArgumentType() ||
        (Query->getKind() != UETT_SizeOf && Query->getKind() != UETT_AlignOf) ||
        Query->isTypeDependent() || !Query->isValueDependent() ||
        !Query->isInstantiationDependent() || !Query->isPRValue() ||
        Query->getType().isNull() || !Query->getType()->isIntegralOrEnumerationType())
      return false;
    const auto *Info = Query->getArgumentTypeInfo();
    if (!Info || !Info->getType()->isDependentType())
      return false;
    auto Written = Info->getTypeLoc().getUnqualifiedLoc().getAs<TemplateTypeParmTypeLoc>();
    const auto *Parameter = Written ? Written.getDecl() : nullptr;
    if (!owned(Parameter) || Parameter->isInvalidDecl() || Parameter->hasAttrs() ||
        Parameter->isParameterPack())
      return false;
    // Only a direct type parameter from this written declaration's own list
    // is metadata. Selected substitutions still undergo concrete source checks.
    for (const auto *Candidate : *TemplateParameterTypeSource) {
      A.chargeExpansion(1, Query->getExprLoc());
      if (Candidate == Parameter)
        return true;
    }
    return false;
  }
  void checkPackSize(const SizeOfPackExpr *Query) {
    A.chargeExpansion(1, Query->getExprLoc());
    if (!validPackOwner(Query->getPack())) {
      A.reject(Query->getExprLoc(), "pack size", "A count requires the exact pack of an admitted owned template.");
      return;
    }
    if (TemplateParameterTypeSource && !Query->isTypeDependent() &&
        Query->isValueDependent() && Query->isInstantiationDependent() &&
        !Query->isPartiallySubstituted() && Query->isPRValue() &&
        !Query->getType().isNull() && Query->getType()->isIntegralOrEnumerationType()) {
      // This declaration-only context is limited to the same written template
      // parameter list. It never obtains or emits an unresolved pack length.
      for (const auto *Parameter : *TemplateParameterTypeSource)
        if (Parameter == Query->getPack())
          return;
    }
    if (!concretePackSize(Query))
      A.reject(Query->getExprLoc(), "pack size", "A fully resolved pack of at most 64 elements is required.");
  }
  bool importContext(const DeclContext *Context) {
    if (!Context || Context->isDependentContext())
      return false;
    Context = Context->getRedeclContext();
    return isa<TranslationUnitDecl, NamespaceDecl, FunctionDecl>(Context);
  }
  bool usingShape(const UsingDecl *D) {
    return D && owned(D) && !D->isInvalidDecl() &&
           D->getUsingLoc().isValid() && importContext(D->getDeclContext()) &&
           D->getQualifier() && !D->getQualifier()->isDependent();
  }
  bool namespaceTarget(const NamedDecl *Target, SourceLocation L) {
    std::set<const NamedDecl *> Seen;
    unsigned Aliases = 0;
    while (Target && owned(Target) && !Target->isInvalidDecl()) {
      A.chargeExpansion(1, L);
      if (!Seen.insert(Target).second)
        break;
      if (const auto *N = dyn_cast<NamespaceDecl>(Target))
        return !N->isDependentContext();
      const auto *Alias = dyn_cast<NamespaceAliasDecl>(Target);
      if (!Alias || ++Aliases > 64 ||
          !importContext(Alias->getDeclContext()) ||
          (Alias->getQualifier() && Alias->getQualifier()->isDependent()))
        break;
      Target = Alias->getAliasedNamespace();
    }
    return false;
  }
  void usingTarget(const UsingShadowDecl *D) {
    if (CheckedUsingShadows.count(D))
      return;
    const NamedDecl *Target = D;
    std::set<const UsingShadowDecl *> Seen;
    unsigned Links = 0;
    while (const auto *Shadow = dyn_cast_or_null<UsingShadowDecl>(Target)) {
      A.chargeExpansion(1, D->getLocation());
      if (Shadow->getKind() != Decl::UsingShadow ||
          !A.S.owns(A.Sources, Shadow->getLocation()) ||
          Shadow->isInvalidDecl() || ++Links > 64 ||
          !Seen.insert(Shadow).second ||
          !usingShape(dyn_cast_or_null<UsingDecl>(Shadow->getIntroducer()))) {
        A.reject(D->getLocation(), "using declaration",
                 "Expected a bounded chain of ordinary source-owned namespace or block imports.");
        return;
      }
      Target = Shadow->getTargetDecl();
    }
    bool Supported = Target && owned(Target) && !Target->isInvalidDecl();
    if (Supported) {
      if (const auto *Constant = dyn_cast<EnumConstantDecl>(Target)) {
        const auto *Enum = dyn_cast<EnumDecl>(Constant->getDeclContext());
        Supported = Enum && owned(Enum) && !Enum->isScoped() &&
                    importContext(Enum->getDeclContext());
      } else {
        Supported = (isa<FunctionDecl, VarDecl, TypedefNameDecl, CXXRecordDecl,
                         EnumDecl>(Target) ||
                     functionTemplateShape(dyn_cast<FunctionTemplateDecl>(Target)) ||
                     classTemplateShape(dyn_cast<ClassTemplateDecl>(Target)) ||
                     aliasTemplateShape(dyn_cast<TypeAliasTemplateDecl>(Target))) &&
                    importContext(Target->getDeclContext());
      }
    }
    if (!Supported) {
      A.reject(D->getLocation(), "using target",
               "An owned namespace or block declaration, or an unscoped non-member enumerator, is required.");
      return;
    }
    A.chargeExpansion(1, D->getLocation());
    CheckedUsingShadows.insert(D);
    // The normal traversal still checks every original target's type, body and
    // definition. Imports add no source entities or emitted storage of their own.
  }
  // Clang 20 does not consistently mark discarded uses as NOUR_Discarded.
  // Track only the potential results of actual discarded-value contexts, never
  // arbitrary descendants such as address operands or reference call arguments.
  void discardedStaticResults(const Expr *E, unsigned Depth = 0) {
    if (!E || E->getType().isNull() || !E->isGLValue() ||
        E->getType().isVolatileQualified() ||
        !E->getType()->isIntegralOrEnumerationType())
      return;
    if (Depth > 64) {
      A.reject(E->getExprLoc(), "discarded constant",
               "Discarded-value potential results exceed the depth limit.");
      throw Failure{};
    }
    if (!CheckedDiscardedResults.insert(E).second)
      return;
    A.chargeExpansion(1, E->getExprLoc());
    if (const auto *P = dyn_cast<ParenExpr>(E)) {
      discardedStaticResults(P->getSubExpr(), Depth + 1);
      return;
    }
    if (const auto *W = dyn_cast<ExprWithCleanups>(E)) {
      if (!A.Context.hasSameType(W->getType(), W->getSubExpr()->getType()) ||
          W->getValueKind() != W->getSubExpr()->getValueKind()) {
        A.reject(E->getExprLoc(), "discarded constant",
                 "A cleanup wrapper must preserve its operand's type and value category.");
        throw Failure{};
      }
      discardedStaticResults(W->getSubExpr(), Depth + 1);
      return;
    }
    if (const auto *C = dyn_cast<ImplicitCastExpr>(E);
        C && C->getCastKind() == CK_NoOp &&
        A.Context.hasSameUnqualifiedType(C->getType(), C->getSubExpr()->getType())) {
      discardedStaticResults(C->getSubExpr(), Depth + 1);
      return;
    }
    if (const auto *C = dyn_cast<ConditionalOperator>(E)) {
      discardedStaticResults(C->getTrueExpr(), Depth + 1);
      discardedStaticResults(C->getFalseExpr(), Depth + 1);
      return;
    }
    if (const auto *B = dyn_cast<BinaryOperator>(E); B && B->getOpcode() == BO_Comma) {
      discardedStaticResults(B->getRHS(), Depth + 1);
      return;
    }
    const ValueDecl *Declaration = nullptr;
    if (const auto *R = dyn_cast<DeclRefExpr>(E))
      Declaration = R->getDecl();
    else if (const auto *M = dyn_cast<MemberExpr>(E))
      Declaration = M->getMemberDecl();
    const auto *V = dyn_cast_or_null<VarDecl>(Declaration);
    if (V && V->isStaticDataMember() && V->getType().isConstQualified() &&
        !V->getDefinition())
      DiscardedStaticValues.insert(E);
  }
  void checkStaticValueUse(const Stmt *S) {
    auto Statement = [&](const Stmt *Body) {
      discardedStaticResults(dyn_cast_or_null<Expr>(Body));
    };
    if (const auto *Block = dyn_cast<CompoundStmt>(S))
      for (const auto *Child : Block->body())
        Statement(Child);
    if (const auto *I = dyn_cast<IfStmt>(S)) {
      Statement(I->getThen());
      Statement(I->getElse());
    }
    if (const auto *F = dyn_cast<ForStmt>(S)) {
      Statement(F->getInit());
      discardedStaticResults(F->getInc());
      Statement(F->getBody());
    }
    if (const auto *F = dyn_cast<CXXForRangeStmt>(S))
      Statement(F->getBody());
    if (const auto *W = dyn_cast<WhileStmt>(S))
      Statement(W->getBody());
    if (const auto *D = dyn_cast<DoStmt>(S))
      Statement(D->getBody());
    if (const auto *W = dyn_cast<SwitchStmt>(S))
      Statement(W->getBody());
    if (const auto *C = dyn_cast<SwitchCase>(S))
      Statement(C->getSubStmt());
    if (const auto *B = dyn_cast<BinaryOperator>(S); B && B->getOpcode() == BO_Comma)
      discardedStaticResults(B->getLHS());
    if (const auto *C = dyn_cast<CastExpr>(S); C && C->getCastKind() == CK_ToVoid)
      discardedStaticResults(C->getSubExpr());

    const ValueDecl *Declaration = nullptr;
    NonOdrUseReason Reason = NOUR_None;
    if (const auto *R = dyn_cast<DeclRefExpr>(S)) {
      Declaration = R->getDecl();
      Reason = R->isNonOdrUse();
    } else if (const auto *M = dyn_cast<MemberExpr>(S)) {
      Declaration = M->getMemberDecl();
      Reason = M->isNonOdrUse();
    }
    const auto *V = dyn_cast_or_null<VarDecl>(Declaration);
    if (V && V->isStaticDataMember() && V->getType().isConstQualified() &&
        !V->getType().isVolatileQualified() &&
        V->getType()->isIntegralOrEnumerationType() && !V->getDefinition() &&
        Reason == NOUR_None && !DiscardedStaticValues.count(cast<Expr>(S)))
      A.reject(S->getBeginLoc(), "static data definition",
               "An address or reference use of a static constant requires a definition.",
               "TR0203");
  }
  // Follow the identity of an lvalue, not arbitrary call arguments. A by-value
  // temporary passed to a function returning some other live object is safe.
  bool temporaryArrayBase(const Expr *E, bool AllowFullExpression = false,
                          const VarDecl *ExpectedExtender = nullptr) {
    E = E->IgnoreParens();
    if (const auto *Default = dyn_cast<CXXDefaultArgExpr>(E)) {
      const auto *Init = selectedDefaultArgument(Default, A.Context);
      return !Init || temporaryArrayBase(Init, AllowFullExpression, ExpectedExtender);
    }
    if (const auto *List = dyn_cast<InitListExpr>(E)) {
      const auto *Init = referenceListInitializer(List, A.Context);
      return !Init || temporaryArrayBase(Init, AllowFullExpression, ExpectedExtender);
    }
    if (E->getType()->isArrayType())
      return temporaryBinding(E, AllowFullExpression, ExpectedExtender);
    if (const auto *C = dyn_cast<CastExpr>(E)) {
      if (C->getCastKind() == CK_ArrayToPointerDecay)
        return temporaryBinding(C->getSubExpr(), AllowFullExpression, ExpectedExtender);
      if (C->getCastKind() == CK_NoOp || C->getCastKind() == CK_BitCast)
        return temporaryArrayBase(C->getSubExpr(), AllowFullExpression, ExpectedExtender);
    }
    if (const auto *W = dyn_cast<ExprWithCleanups>(E))
      return temporaryArrayBase(W->getSubExpr(), AllowFullExpression, ExpectedExtender);
    if (const auto *C = dyn_cast<ConditionalOperator>(E))
      return temporaryArrayBase(C->getTrueExpr(), AllowFullExpression, ExpectedExtender) ||
             temporaryArrayBase(C->getFalseExpr(), AllowFullExpression, ExpectedExtender);
    if (const auto *B = dyn_cast<BinaryOperator>(E)) {
      if (B->getOpcode() == BO_Comma)
        return temporaryArrayBase(B->getRHS(), AllowFullExpression, ExpectedExtender);
      if (B->getType()->isPointerType() &&
          (B->getOpcode() == BO_Add || B->getOpcode() == BO_Sub))
        return temporaryArrayBase(B->getLHS()->getType()->isPointerType()
                                      ? B->getLHS()
                                      : B->getRHS(), AllowFullExpression, ExpectedExtender);
    }
    if (const auto *U = dyn_cast<UnaryOperator>(E); U && U->getOpcode() == UO_AddrOf)
      return temporaryBinding(U->getSubExpr(), AllowFullExpression, ExpectedExtender);
    // A pointer prvalue (including a call result) is not a temporary pointee.
    return false;
  }
  bool temporaryBinding(const Expr *E, bool AllowFullExpression = false,
                        const VarDecl *ExpectedExtender = nullptr) {
    E = E->IgnoreParens();
    if (const auto *Default = dyn_cast<CXXDefaultArgExpr>(E)) {
      const auto *Init = selectedDefaultArgument(Default, A.Context);
      return !Init || temporaryBinding(Init, AllowFullExpression, ExpectedExtender);
    }
    if (const auto *List = dyn_cast<InitListExpr>(E)) {
      const auto *Init = referenceListInitializer(List, A.Context);
      return !Init || temporaryBinding(Init, AllowFullExpression, ExpectedExtender);
    }
    if (const auto *Opaque = dyn_cast<OpaqueValueExpr>(E)) {
      auto Found = ArraySources.find(Opaque);
      return Found == ArraySources.end() || temporaryBinding(Found->second, AllowFullExpression, ExpectedExtender);
    }
    if (const auto *M = dyn_cast<MaterializeTemporaryExpr>(E))
      return !(AllowFullExpression && fullExpressionTemporary(M, A.Context)) &&
             !(ExpectedExtender && A.temporaryOwner(M) == ExpectedExtender);
    if (isa<CXXBindTemporaryExpr>(E))
      return true;
    if (const auto *C = dyn_cast<CastExpr>(E))
      return temporaryBinding(C->getSubExpr(), AllowFullExpression, ExpectedExtender);
    if (const auto *W = dyn_cast<ExprWithCleanups>(E))
      return temporaryBinding(W->getSubExpr(), AllowFullExpression, ExpectedExtender);
    if (const auto *M = dyn_cast<MemberExpr>(E)) {
      if (A.S.coreV2())
        if (const auto *V = dyn_cast<VarDecl>(M->getMemberDecl());
            V && V->isStaticDataMember())
          return false; // Static storage does not inherit its receiver's lifetime.
      return M->isArrow() ? temporaryArrayBase(M->getBase(), AllowFullExpression, ExpectedExtender)
                          : temporaryBinding(M->getBase(), AllowFullExpression, ExpectedExtender);
    }
    if (const auto *Index = dyn_cast<ArraySubscriptExpr>(E))
      return temporaryArrayBase(Index->getBase(), AllowFullExpression, ExpectedExtender);
    if (const auto *C = dyn_cast<ConditionalOperator>(E))
      return temporaryBinding(C->getTrueExpr(), AllowFullExpression, ExpectedExtender) ||
             temporaryBinding(C->getFalseExpr(), AllowFullExpression, ExpectedExtender);
    if (const auto *B = dyn_cast<BinaryOperator>(E)) {
      if (B->getOpcode() == BO_Comma)
        return temporaryBinding(B->getRHS(), AllowFullExpression, ExpectedExtender);
      if (B->isAssignmentOp())
        return temporaryBinding(B->getLHS(), AllowFullExpression, ExpectedExtender);
    }
    if (const auto *U = dyn_cast<UnaryOperator>(E)) {
      if (U->getOpcode() == UO_Deref)
        return temporaryArrayBase(U->getSubExpr(), AllowFullExpression, ExpectedExtender);
      if (U->isIncrementDecrementOp())
        return temporaryBinding(U->getSubExpr(), AllowFullExpression, ExpectedExtender);
    }
    // An xvalue can still designate a live object. Temporary wrappers above
    // retain their own rejection; changing category alone creates no owner.
    return !E->isGLValue();
  }
  bool containsArray(QualType T, unsigned Depth = 0) {
    if (Depth > 64 || T->isArrayType())
      return true;
    if (const auto *R = T->getAsCXXRecordDecl(); R && R->getDefinition()) {
      R = R->getDefinition();
      auto Found = HasArray.find(R);
      if (Found != HasArray.end())
        return Found->second;
      HasArray.emplace(R, true);
      for (const auto *F : R->getDefinition()->fields())
        if (containsArray(F->getType(), Depth + 1))
          return true;
      HasArray[R] = false;
    }
    return false;
  }
  void checkBinding(const Expr *E, bool AllowFullExpression = false,
                    const VarDecl *ExpectedExtender = nullptr) {
    if (E && temporaryBinding(E, AllowFullExpression, ExpectedExtender))
      A.reject(E->getExprLoc(), "reference binding",
               "The temporary does not have an admitted lifetime for this reference binding.");
  }

  void checkDefaultArgument(const Expr *E, const FunctionDecl *F, unsigned Index,
                            SourceLocation L) {
    const auto *Default = dyn_cast<CXXDefaultArgExpr>(E->IgnoreParens());
    if (!Default)
      return;
    const auto *P = Default->getParam();
    const auto *Owner = P ? dyn_cast<FunctionDecl>(P->getDeclContext()) : nullptr;
    if (!selectedDefaultArgument(Default, A.Context) || !owned(P) || !Owner ||
        !F || Owner->getCanonicalDecl() != F->getCanonicalDecl() ||
        P->getFunctionScopeIndex() != Index)
      A.reject(L, "default argument",
               "The selected parameter and call argument slot must agree.");
  }

  void queueGenerated(const CXXMethodDecl *Method, SourceLocation L) {
    const FunctionDecl *Definition = nullptr;
    if (Method->isTrivial())
      return;
    if (!Method->hasBody(Definition)) {
      if (concreteMemberFunction(Method) && Method->isUsed(/*CheckUsedAttr=*/false))
        A.reject(L, "generated definition",
                 "A used nontrivial defaulted member needs a generated definition.", "TR0203");
      return; // Unevaluated uses can have no lazy body.
    }
    if (!A.S.owns(A.Sources, Definition->getLocation())) {
      A.reject(L, "generated definition", "The generated definition must be source-owned.", "TR0203");
      return;
    }
    if (QueuedGeneratedMethods.insert(Method->getCanonicalDecl()).second) {
      A.chargeExpansion(1, L);
      GeneratedMethods.push_back(cast<CXXMethodDecl>(Definition));
    }
  }
  void checkConstruction(const CXXConstructExpr *C, SourceLocation L) {
    if (!CheckedConstructions.insert(C).second)
      return;
    const auto *Constructor = C->getConstructor();
    if (A.S.coreV2() && supportedConstructor(Constructor)) {
      if (!A.S.owns(A.Sources, Constructor->getLocation())) {
        A.reject(L, "construction", "The selected constructor must be source-owned.", "TR0203");
      } else if (defaultedLifecycle(Constructor) || defaultedCopyOrMoveConstructor(Constructor)) {
        queueGenerated(Constructor, L);
      } else if (!Constructor->hasBody()) {
        A.reject(L, "construction",
                 "The selected constructor requires a source-owned definition.",
                 "TR0203");
      }
      // Preserve the existing inline implicit/trivial move operation, whose
      // source can already be materialized for this one value copy. This is
      // not a general reference binding or a call to a user/defaulted helper.
      const bool InlineMove = Constructor->isImplicit() && Constructor->isTrivial() &&
                              Constructor->isMoveConstructor();
      for (unsigned I = 0; I < C->getNumArgs() &&
                           I < Constructor->getNumParams(); ++I) {
        checkDefaultArgument(C->getArg(I), Constructor, I, L);
        if (!InlineMove && Constructor->getParamDecl(I)->getType()->isReferenceType())
          checkBinding(C->getArg(I), true);
      }
    } else if (!Constructor->isImplicit() || !Constructor->isTrivial()) {
      A.reject(L, "construction",
               "Only admitted constructors and implicit trivial "
               "default/copy construction are supported.");
    }
  }
  void checkSemanticInitializers(const InitListExpr *List, SourceLocation Owner) {
    // RAV visits only the written form of a braced initializer by default.
    // Follow its semantic elements and shared array filler once for admission;
    // lowering still evaluates a filler separately for every destination.
    std::vector<const Expr *> Work{List};
    while (!Work.empty()) {
      const auto *E = Work.back();
      Work.pop_back();
      if (const auto *I = dyn_cast<InitListExpr>(E)) {
        if (I->isSyntacticForm() && I->getSemanticForm())
          I = I->getSemanticForm();
        if (!CheckedSemanticInitializers.insert(I).second)
          continue;
        if (I->isGLValue() && !referenceListInitializer(I, A.Context))
          A.reject(Owner, "reference initializer", "A transparent single-element reference list is required.");
        for (const auto *Init : I->inits())
          if (Init) Work.push_back(Init);
        if (const auto *Filler = I->getArrayFiller())
          Work.push_back(Filler);
        continue;
      }
      if (!CheckedSemanticInitializers.insert(E).second)
        continue;
      // A reference temporary can itself contain a semantic initializer list
      // with an implicit conversion call. Traverse every semantic element;
      // inspecting only constructors or outer materializations misses those
      // calls when RAV follows the nested list's written form instead.
      auto PreviousOwner = ImplicitInitializerOwner;
      ImplicitInitializerOwner = Owner;
      auto Restore = llvm::make_scope_exit([&] { ImplicitInitializerOwner = PreviousOwner; });
      TraverseStmt(const_cast<Expr *>(E));
    }
  }

public:
  explicit Allowlist(Adapter &A) : A(A) {}
  void checkExplicitFunctionInstantiation(
      const ExplicitFunctionInstantiationSource &Source) {
    if (!A.S.coreV2() || !A.S.owns(A.Sources, Source.Location))
      return;
    A.chargeExpansion(1 + Source.Arguments->NumTemplateArgs, Source.Location);
    if (!owned(Source.Function) || !Source.Type ||
        (!concreteFreeFunctionTemplate(Source.Function) &&
         !concreteMemberFunction(Source.Function)) || Source.HasAttributes) {
      A.reject(Source.Location, "explicit function instantiation",
               "An owned admitted function and attribute-free source directive are required.");
      return;
    }
    auto *SavedFunction = CurrentFunction;
    auto *SavedMethod = CurrentMethod;
    auto SavedOwner = ImplicitInitializerOwner;
    CurrentFunction = Source.Function;
    CurrentMethod = dyn_cast<CXXMethodDecl>(Source.Function);
    ImplicitInitializerOwner = Source.Location;
    auto Restore = llvm::make_scope_exit([&] {
      CurrentFunction = SavedFunction;
      CurrentMethod = SavedMethod;
      ImplicitInitializerOwner = SavedOwner;
    });
    // A directive is source evidence, not another function declaration. Its
    // written type/name can differ from the reused function's source metadata.
    const auto *Prototype = Source.Type->getType()->getAs<FunctionProtoType>();
    if (!Prototype) {
      A.reject(Source.Location, "explicit instantiation type",
               "A resolved function prototype is required.");
      return;
    }
    A.type(Prototype->getReturnType(), Source.Location, true);
    for (auto Parameter : Prototype->param_types())
      A.type(Parameter, Source.Location);
    if (concreteFreeFunctionTemplate(Source.Function))
      checkFunctionTemplateUse(Source.Function, Source.Name.getLoc(), Source.Arguments->arguments());
    else
      for (const auto &Argument : Source.Arguments->arguments())
        if (!TraverseTemplateArgumentLoc(Argument))
          return;
    if (!TraverseDeclarationNameInfo(Source.Name) ||
        !TraverseNestedNameSpecifierLoc(Source.Qualifier))
      return;
    TraverseTypeLoc(Source.Type->getTypeLoc());
  }
  void checkExplicitStaticDataInstantiation(
      const ExplicitStaticDataInstantiationSource &Source) {
    if (!A.S.coreV2() || !A.S.owns(A.Sources, Source.Location))
      return;
    A.chargeExpansion(1, Source.Location);
    if (!owned(Source.Variable) || !Source.Type || Source.HasAttributes ||
        !classPatternShape(classStaticDataPattern(Source.Variable)) ||
        !classTemplateStaticDataShape(Source.Variable) ||
        !A.Context.hasSameType(Source.Type->getType(), Source.Variable->getType())) {
      A.reject(Source.Location, "explicit static instantiation",
               "An owned scalar member and matching attribute-free source type are required.");
      return;
    }
    auto *SavedFunction = CurrentFunction;
    auto *SavedMethod = CurrentMethod;
    auto SavedOwner = ImplicitInitializerOwner;
    CurrentFunction = nullptr;
    CurrentMethod = nullptr;
    ImplicitInitializerOwner = Source.Location;
    auto Restore = llvm::make_scope_exit([&] {
      CurrentFunction = SavedFunction;
      CurrentMethod = SavedMethod;
      ImplicitInitializerOwner = SavedOwner;
    });
    A.type(Source.Type->getType(), Source.Location);
    if (TraverseNestedNameSpecifierLoc(Source.Qualifier))
      TraverseTypeLoc(Source.Type->getTypeLoc());
  }
  bool templateSourceShape(const NamedDecl *D) {
    return functionTemplateShape(dyn_cast_or_null<FunctionTemplateDecl>(D)) ||
           classPatternShape(D) ||
           aliasTemplateShape(dyn_cast_or_null<TypeAliasTemplateDecl>(D));
  }
  static const Expr *argumentExpression(const TemplateArgumentLoc &Argument) {
    switch (Argument.getArgument().getKind()) {
    case TemplateArgument::Expression: return Argument.getSourceExpression();
    case TemplateArgument::Integral: return Argument.getSourceIntegralExpression();
    default: return nullptr;
    }
  }
  static bool sameArgumentSource(const TemplateArgumentLoc &Left,
                                 const TemplateArgumentLoc &Right) {
    if (Left.getArgument().getKind() != Right.getArgument().getKind())
      return false;
    switch (Left.getArgument().getKind()) {
    case TemplateArgument::Type:
      return Left.getTypeSourceInfo() &&
             Left.getTypeSourceInfo() == Right.getTypeSourceInfo();
    case TemplateArgument::Expression:
    case TemplateArgument::Integral:
      return argumentExpression(Left) &&
             argumentExpression(Left) == argumentExpression(Right);
    default: return false;
    }
  }
  static bool sameArguments(const TemplateArgumentList *Left,
                            const TemplateArgumentList *Right) {
    if (!Left || !Right || Left->size() != Right->size())
      return false;
    for (unsigned I = 0; I < Left->size(); ++I)
      if (!Left->get(I).structurallyEquals(Right->get(I)))
        return false;
    return true;
  }
  bool sourceMatchesArgument(const TemplateArgumentLoc &Source,
                             const TemplateArgument &Canonical) {
    if (Canonical.getKind() == TemplateArgument::Type) {
      const auto &Argument = Source.getArgument();
      return Argument.getKind() == TemplateArgument::Type &&
             Source.getTypeSourceInfo() && !Argument.getAsType().isNull() &&
             !Argument.isInstantiationDependent() &&
             A.Context.hasSameType(Argument.getAsType(), Canonical.getAsType());
    }
    const auto *E = argumentExpression(Source);
    if (Canonical.getKind() != TemplateArgument::Integral || !E ||
        E->getType().isNull() || E->isInstantiationDependent() ||
        !E->getType()->isIntegralOrEnumerationType())
      return false;
    APValue Value;
    return A.Context.hasSameType(E->getType(), Canonical.getIntegralType()) &&
           E->isCXX11ConstantExpr(A.Context, &Value) && Value.isInt() &&
           Value.getInt() == Canonical.getAsIntegral();
  }
  bool parameterInTemplate(const NamedDecl *Template, const NamedDecl *Parameter,
                           unsigned Index) {
    if (!Template || !owned(Parameter) || Parameter->isInvalidDecl() || Index >= 64)
      return false;
    auto Find = [&](const auto *Primary) {
      if (!Primary)
        return false;
      unsigned Count = 0;
      for (const auto *Declaration : Primary->redecls()) {
        A.chargeExpansion(1, Parameter->getLocation());
        if (++Count > 64)
          return false;
        const auto *Parameters = templateSourceParameters(Declaration);
        if (Parameters && Index < Parameters->size() && Parameters->getParam(Index) == Parameter)
          return owned(Declaration) && templateParametersShape(Parameters);
      }
      return false;
    };
    return Find(dyn_cast<FunctionTemplateDecl>(Template)) ||
           Find(dyn_cast<ClassTemplateDecl>(Template)) ||
           Find(dyn_cast<TypeAliasTemplateDecl>(Template)) ||
           Find(dyn_cast<ClassTemplatePartialSpecializationDecl>(Template));
  }
  bool checkClassPartialSource(const ClassTemplateSpecializationDecl *Record,
                               SourceLocation L) {
    if (!Record || Record->getKind() != Decl::ClassTemplateSpecialization)
      return true;
    const auto *Partial = Record->getSpecializedTemplateOrPartial()
                              .dyn_cast<ClassTemplatePartialSpecializationDecl *>();
    if (!Partial)
      return true;
    const auto *CanonicalRecord = Record->getCanonicalDecl();
    if (CheckedPartialRecords.count(CanonicalRecord))
      return true;
    A.chargeExpansion(1, L);
    if (!owned(Record) || Record->isDependentContext() || !classPartialShape(Partial)) {
      A.reject(L, "selected class partial", "A concrete instance of an owned admitted partial is required.");
      return true;
    }
    if (ActivePartialRecords.size() >= 64 ||
        std::find(ActivePartialRecords.begin(), ActivePartialRecords.end(), CanonicalRecord) !=
            ActivePartialRecords.end()) {
      A.reject(L, "partial source recursion", "Selected partial source exceeds its depth limit or forms a cycle.");
      return true;
    }
    ActivePartialRecords.push_back(CanonicalRecord);
    auto Restore = llvm::make_scope_exit([&] { ActivePartialRecords.pop_back(); });
    const auto *Selection = &Record->getTemplateInstantiationArgs();
    auto Found = PartialSources.find(Selection);
    if (Found == PartialSources.end() || Found->second.Conflict ||
        !Found->second.Deduction || !Found->second.Pattern) {
      A.reject(L, "selected partial evidence", "The selected class needs its exact successful deduction and pattern source.");
      return true;
    }
    const auto &Deduction = *Found->second.Deduction;
    const auto &Pattern = *Found->second.Pattern;
    if (Deduction.Kind != TemplateSourceKind::PartialDeduction ||
        Pattern.Kind != TemplateSourceKind::PartialPattern ||
        Deduction.Selection != Selection || Pattern.Selection != Selection ||
        Deduction.Template != Partial || Deduction.Declaration != Partial ||
        Pattern.Declaration != Partial || !Pattern.Template ||
        Pattern.Template->getCanonicalDecl() != Record->getSpecializedTemplate()->getCanonicalDecl() ||
        !sameArguments(Deduction.Canonical, Selection) ||
        !sameArguments(Pattern.Canonical, &Record->getTemplateArgs()) ||
        Deduction.Written || !Pattern.Written || !Deduction.Defaults.empty()) {
      A.reject(L, "selected partial identity", "Primary arguments, deduced slots and the exact selected pattern must agree.");
      return true;
    }
    if (!checkTemplateUse(Deduction, {}))
      return false;
    if (A.S.Diagnostics.empty())
      CheckedPartialRecords.insert(CanonicalRecord);
    return true;
  }
  const TemplateArgument *selectedPartialSlot(const Decl *Associated, unsigned Index,
                                             SourceLocation L) {
    const auto *Record = dyn_cast_or_null<ClassTemplateSpecializationDecl>(Associated);
    if (!Record || Record->getKind() != Decl::ClassTemplateSpecialization ||
        !Record->getSpecializedTemplateOrPartial().is<ClassTemplatePartialSpecializationDecl *>() ||
        !checkClassPartialSource(Record, L) || !A.S.Diagnostics.empty())
      return nullptr;
    const auto &Arguments = Record->getTemplateInstantiationArgs();
    return Index < Arguments.size() ? &Arguments.get(Index) : nullptr;
  }
  const TemplateArgument *partialSourceEdge(const Decl *Associated,
                                            const NamedDecl *Owner, unsigned Index,
                                            std::optional<unsigned> PackIndex,
                                            SourceLocation L) {
    if (const auto *Record = dyn_cast_or_null<ClassTemplateSpecializationDecl>(Associated);
        Record && Record->getKind() == Decl::ClassTemplateSpecialization) {
      const auto *Argument = selectedPartialSlot(Associated, Index, L);
      if (!Argument)
        return nullptr;
      if (!PackIndex)
        return Argument->getKind() != TemplateArgument::Pack ? Argument : nullptr;
      if (Argument->getKind() == TemplateArgument::Pack && Argument->pack_size() <= 64 &&
          *PackIndex < Argument->pack_size())
        return &Argument->pack_elements()[Argument->pack_size() - 1 - *PackIndex];
      return nullptr;
    }
    // A pattern's own substitution precedes any concrete class association.
    // Its exact partial owner must therefore be on the active source stack.
    return sourceEdge(Owner, Index, PackIndex, L);
  }
  const TemplateArgument *sourceEdge(const NamedDecl *Primary, unsigned Index,
                                      std::optional<unsigned> PackIndex,
                                      SourceLocation Location) {
    for (auto I = TemplateFrames.rbegin(); I != TemplateFrames.rend(); ++I) {
      const auto &Frame = **I;
      if (Frame.Source.Template->getCanonicalDecl() != Primary->getCanonicalDecl())
        continue;
      if (Index >= Frame.Ready.size() || !Frame.Ready[Index])
        break;
      const auto *Argument = &Frame.Source.Canonical->get(Index);
      if (!PackIndex)
        return Argument->getKind() != TemplateArgument::Pack ? Argument : nullptr;
      if (Argument->getKind() == TemplateArgument::Pack &&
          Argument->pack_size() <= 64 && *PackIndex < Argument->pack_size())
        return &Argument->pack_elements()[Argument->pack_size() - 1 - *PackIndex];
      break;
    }
    A.reject(Location, "template source edge",
             "A substitution requires the matching previously checked argument slot.");
    return nullptr;
  }
  bool hasSourceFrame(const NamedDecl *Primary) const {
    for (const auto *Frame : TemplateFrames)
      if (Frame->Source.Template->getCanonicalDecl() == Primary->getCanonicalDecl())
        return true;
    return false;
  }
  void checkScalarSourceEdge(const SubstNonTypeTemplateParmExpr *E, SourceLocation L) {
    const auto *Primary = scalarTemplateOwner(E);
    if (!Primary || (!isa<TypeAliasTemplateDecl, ClassTemplatePartialSpecializationDecl>(Primary) &&
                     !hasSourceFrame(Primary)))
      return; // Existing concrete function/class bodies retain their source checks.
    const auto *Argument = isa<ClassTemplatePartialSpecializationDecl>(Primary)
        ? partialSourceEdge(E->getAssociatedDecl(), Primary, E->getIndex(), E->getPackIndex(), L)
        : sourceEdge(Primary, E->getIndex(), E->getPackIndex(), L);
    APValue Value;
    if (!Argument || Argument->getKind() != TemplateArgument::Integral ||
        !A.Context.hasSameType(E->getType(), Argument->getIntegralType()) ||
        !E->getReplacement()->isCXX11ConstantExpr(A.Context, &Value) || !Value.isInt() ||
        Value.getInt() != Argument->getAsIntegral())
      A.reject(L, "template scalar source", "The replacement must equal its checked scalar argument.");
  }
  bool emptyTypePackSource(const SubstTemplateTypeParmPackType *Type,
                           SourceLocation L) {
    const auto *Primary = templateSourceOwner(Type->getAssociatedDecl());
    if (!owned(Type->getAssociatedDecl()) || !templateSourceShape(Primary) ||
        Type->getNumArgs() || Type->getIndex() >= templateSourceParameters(Primary)->size())
      return false;
    const auto *Parameter = dyn_cast<TemplateTypeParmDecl>(
        templateSourceParameters(Primary)->getParam(Type->getIndex()));
    if (!Parameter || !Parameter->isParameterPack() || Parameter->getDepth() || !owned(Parameter))
      return false;
    if (const auto *Record = dyn_cast<ClassTemplateSpecializationDecl>(Type->getAssociatedDecl());
        Record && Record->getKind() == Decl::ClassTemplateSpecialization &&
        isa<ClassTemplatePartialSpecializationDecl>(Primary)) {
      const auto *Argument = selectedPartialSlot(Record, Type->getIndex(), L);
      return Argument && Argument->getKind() == TemplateArgument::Pack && !Argument->pack_size();
    }
    for (auto I = TemplateFrames.rbegin(); I != TemplateFrames.rend(); ++I) {
      const auto &Frame = **I;
      if (Frame.Source.Template->getCanonicalDecl() != Primary->getCanonicalDecl())
        continue;
      if (Type->getIndex() >= Frame.Ready.size() || !Frame.Ready[Type->getIndex()])
        return false;
      const auto &Argument = Frame.Source.Canonical->get(Type->getIndex());
      A.chargeExpansion(1, L);
      return Argument.getKind() == TemplateArgument::Pack && !Argument.pack_size();
    }
    return false;
  }
  bool checkNonTypeParameterSource(const NonTypeParameterSource &Source,
                                    const TemplateArgument *Argument,
                                    SourceLocation L) {
    auto *Info = Source.Type;
    if (!Info || Info->getType().isNull() ||
        !A.S.owns(A.Sources, Info->getTypeLoc().getBeginLoc())) {
      A.reject(L, "template parameter type source", "Each checked non-type argument needs its actual parameter type source.");
      return true;
    }
    auto Type = Info->getType();
    if (!Argument) {
      if (auto Expansion = Info->getTypeLoc().getAs<PackExpansionTypeLoc>()) {
        auto Pattern = Expansion.getPatternLoc();
        const auto *Pack = Pattern.getType()->getAs<SubstTemplateTypeParmPackType>();
        if (!Pack || Pattern.getType().isVolatileQualified() ||
            Pattern.getType().isRestrictQualified() || !emptyTypePackSource(Pack, L)) {
          A.reject(L, "empty parameter type expansion", "An unresolved empty type expansion requires its checked empty source pack.");
          return true;
        }
        return TraverseTypeLoc(Pattern);
      }
    }
    const auto *Auto = dyn_cast<AutoType>(Type.getTypePtr());
    const bool Placeholder = Auto && Auto->getDeducedType().isNull() && !Auto->isConstrained();
    if (Type.isVolatileQualified() || Type.isRestrictQualified() ||
        (!Placeholder && (Type->isInstantiationDependentType() ||
                          !Type->isIntegralOrEnumerationType())) ||
        (Argument && (Argument->getKind() != TemplateArgument::Integral ||
                      (!Placeholder && !A.Context.hasSameType(Type.getUnqualifiedType(),
                                      Argument->getIntegralType().getUnqualifiedType()))))) {
      A.reject(L, "template parameter type", "A concrete scalar parameter type must match its converted argument; plain auto remains source metadata.");
      return true;
    }
    if (!Placeholder)
      A.type(Type, L);
    return TraverseTypeLoc(Info->getTypeLoc());
  }
  bool checkTemplateUse(const TemplateUseSource &Source,
                        llvm::ArrayRef<TemplateArgumentLoc> Written) {
    const auto L = Source.Location;
    A.chargeExpansion(1, L);
    if (!templateSourceShape(Source.Template) || !Source.Canonical || !Source.Sugared ||
        Source.DefaultsOverflow || Source.Defaults.size() > 64 || Source.ParameterTypes.size() > 4096 ||
        Source.Canonical->size() != templateSourceParameters(Source.Template)->size() ||
        Source.Canonical->size() != Source.Sugared->size()) {
      A.reject(L, "template use source", "A complete use of an admitted template parameter owner is required.");
      return true;
    }
    if (ActiveTemplateUses.size() >= 64) {
      A.reject(L, "template source depth", "Nested template source uses exceed the expansion limit.");
      return true;
    }
    for (const auto *Active : ActiveTemplateUses)
      if (Active == &Source) {
        A.reject(L, "template source cycle", "Template source evidence cannot recursively refer to itself.");
        return true;
      }
    ActiveTemplateUses.push_back(&Source);
    auto RestoreActive = llvm::make_scope_exit([&] { ActiveTemplateUses.pop_back(); });
    if (Source.Kind == TemplateSourceKind::ClassDeclaration) {
      const auto *Declaration = dyn_cast_or_null<ClassTemplateSpecializationDecl>(Source.Declaration);
      const auto *Primary = Declaration ? Declaration->getSpecializedTemplate() : nullptr;
      if (!owned(Declaration) || !Primary ||
          !sameArguments(Source.Canonical, &Declaration->getTemplateArgs()) ||
          Primary->getCanonicalDecl() != Source.Template->getCanonicalDecl()) {
        A.reject(L, "class template declaration source", "Each declaration needs its own matching concrete argument evidence.");
        return true;
      }
    }
    const auto *Parameters = templateSourceParameters(Source.Template);
    const unsigned Count = Parameters->size();
    checkTemplateArguments(Parameters, *Source.Canonical, L);
    if (!A.S.Diagnostics.empty())
      return true;
    for (unsigned I = 0; I < Count; ++I) {
      const auto &Canonical = Source.Canonical->get(I);
      const auto &Sugared = Source.Sugared->get(I);
      bool Bounded = Canonical.getKind() == Sugared.getKind();
      if (Bounded && Canonical.getKind() == TemplateArgument::Pack) {
        Bounded = Canonical.pack_size() == Sugared.pack_size();
        if (Bounded)
          for (const auto &Element : Sugared.pack_elements())
            Bounded &= Element.getKind() == TemplateArgument::Type ||
                       Element.getKind() == TemplateArgument::Integral;
      }
      if (!Bounded || !Canonical.structurallyEquals(A.Context.getCanonicalTemplateArgument(Sugared))) {
        A.reject(L, "template argument sugar", "Written substitution arguments must preserve their bounded canonical values.");
        return true;
      }
    }
    std::vector<const TemplateDefaultArgumentSource *> Defaults(Count, nullptr);
    unsigned Previous = 0;
    bool First = true;
    for (const auto &Default : Source.Defaults) {
      const auto *Type = dyn_cast_or_null<TemplateTypeParmDecl>(Default.Parameter);
      const auto *Value = dyn_cast_or_null<NonTypeTemplateParmDecl>(Default.Parameter);
      const unsigned Index = Type ? Type->getIndex() : Value ? Value->getIndex() : Count;
      if (Index >= Count || (!First && Index <= Previous) || Defaults[Index] ||
          !parameterInTemplate(Source.Template, Default.Parameter, Index) ||
          (Type ? (!Type->hasDefaultArgument() || Type->isParameterPack())
                : (!Value || !Value->hasDefaultArgument() || Value->isParameterPack())) ||
          !sourceMatchesArgument(Default.Converted, Source.Canonical->get(Index))) {
        A.reject(L, "selected template default", "Defaults must match ordered, owned parameter slots and converted values.");
        return true;
      }
      Defaults[Index] = &Default;
      First = false;
      Previous = Index;
    }
    std::vector<std::map<unsigned, const NonTypeParameterSource *>> ParameterTypes(Count);
    for (const auto &Type : Source.ParameterTypes) {
      const auto *Parameter = dyn_cast_or_null<NonTypeTemplateParmDecl>(Type.Parameter);
      const unsigned Index = Parameter ? Parameter->getIndex() : Count;
      if (Index >= Count || !parameterInTemplate(Source.Template, Type.Parameter, Index) ||
          (!Parameter->isParameterPack() && Type.PackIndex != 0) ||
          (Parameter->isParameterPack() && Type.PackIndex >= 64 && Type.PackIndex != ~0u) ||
          !ParameterTypes[Index].emplace(Type.PackIndex, &Type).second) {
        A.reject(L, "template parameter source slots", "Parameter source records must identify distinct owned scalar slots and forward pack positions.");
        return true;
      }
    }
    for (unsigned Index = 0; Index < Count; ++Index) {
      const auto &Argument = Source.Canonical->get(Index);
      unsigned Expected = 0;
      bool Empty = false;
      if (isa<NonTypeTemplateParmDecl>(Parameters->getParam(Index))) {
        Expected = Argument.getKind() == TemplateArgument::Pack ? Argument.pack_size() : 1;
        // Function and partial deduction substitute empty NTTP pack types;
        // primary class/alias argument checking forms an empty pack directly.
        Empty = !Expected && (Source.Kind == TemplateSourceKind::Function ||
                               Source.Kind == TemplateSourceKind::PartialDeduction);
        if (Empty)
          Expected = 1;
      }
      if (ParameterTypes[Index].size() != Expected) {
        A.reject(L, "template parameter source coverage", "Every checked argument and deduced empty parameter pack needs its complete type source.");
        return true;
      }
      for (unsigned E = 0; E < Expected; ++E)
        if (!ParameterTypes[Index].count(Empty ? ~0u : E)) {
          A.reject(L, "template parameter pack source", "Source indices must cover this exact scalar argument pack.");
          return true;
        }
    }
    TemplateSourceFrame Frame{Source, std::vector<bool>(Count, false)};
    const unsigned WrittenCount = Written.size();
    unsigned Position = 0;
    for (unsigned Index = 0; Index < Count; ++Index) {
      const auto &Argument = Source.Canonical->get(Index);
      if (Defaults[Index])
        continue;
      unsigned Elements = Argument.getKind() == TemplateArgument::Pack ? Argument.pack_size() : 1;
      if (Elements > 64)
        return true; // checkTemplateArguments already diagnosed this pack.
      unsigned Available = std::min(Elements, WrittenCount - Position);
      for (unsigned E = 0; E < Available; ++E)
        if (!TraverseTemplateArgumentLoc(Written[Position++]))
          return false;
      if (Source.Kind != TemplateSourceKind::Function &&
          Source.Kind != TemplateSourceKind::PartialDeduction && Available != Elements) {
        A.reject(L, "template argument source", "Every nondeduced argument requires its written source or selected default.");
        return true;
      }
      // Function arguments not written in <...> come from ordinary deduction;
      // their call expressions and instantiated signatures are checked by RAV.
      Frame.Ready[Index] = true;
    }
    if (Position != WrittenCount) {
      A.reject(L, "template argument source", "Written argument count does not match this selected use.");
      return true;
    }
    // Explicit arguments belong to the caller's source context. Installing
    // this callee frame earlier would capture substitutions from a recursive
    // call to the same primary, before the callee's own slots were checked.
    TemplateFrames.push_back(&Frame);
    auto Restore = llvm::make_scope_exit([&] { TemplateFrames.pop_back(); });
    for (unsigned Index = 0; Index < Count; ++Index) {
      for (const auto &Entry : ParameterTypes[Index]) {
        const auto &Canonical = Source.Canonical->get(Index);
        const TemplateArgument *Argument = &Canonical;
        if (Entry.first == ~0u)
          Argument = nullptr;
        else if (Canonical.getKind() == TemplateArgument::Pack)
          Argument = &Canonical.pack_elements()[Entry.first];
        if (!checkNonTypeParameterSource(*Entry.second, Argument, L))
          return false;
        if (!A.S.Diagnostics.empty())
          return true;
      }
      const auto *Default = Defaults[Index];
      if (!Default)
        continue;
      for (const auto *Argument : {&Default->Written, &Default->Converted}) {
        if (!A.S.owns(A.Sources, Argument->getLocation()) ||
            Argument->getArgument().isInstantiationDependent()) {
          A.reject(L, "template default source", "A selected default requires concrete owned source evidence.");
          return true;
        }
        if (!TraverseTemplateArgumentLoc(*Argument))
          return false;
      }
      Frame.Ready[Index] = true;
    }
    if (Source.Kind == TemplateSourceKind::PartialDeduction) {
      auto Found = PartialSources.find(Source.Selection);
      if (Found == PartialSources.end() || Found->second.Conflict ||
          Found->second.Deduction != &Source || !Found->second.Pattern ||
          !Found->second.Pattern->Written || !Source.Defaults.empty()) {
        A.reject(L, "partial pattern source", "A selected deduction requires its paired substituted pattern.");
        return true;
      }
      const auto &Pattern = *Found->second.Pattern;
      return checkTemplateUse(Pattern, Pattern.Written->arguments());
    }
    if (isa<TypeAliasTemplateDecl>(Source.Template)) {
      const auto *Alias = dyn_cast_or_null<TemplateSpecializationType>(Source.Type);
      if (Source.Kind != TemplateSourceKind::Type || !Alias || !Alias->isTypeAlias() || !Source.Underlying ||
          Source.Underlying->getType().isNull() ||
          Source.Underlying->getType()->isInstantiationDependentType() ||
          !A.Context.hasSameType(QualType(Source.Type, 0), Source.Underlying->getType())) {
        A.reject(L, "alias underlying source", "The alias requires its matching concrete substituted source type.");
        return true;
      }
      A.type(Source.Underlying->getType(), L, true);
      return TraverseTypeLoc(Source.Underlying->getTypeLoc());
    }
    return true;
  }
  bool equivalentUse(const TemplateUseSource &Left, const TemplateUseSource &Right) {
    if (Left.Template->getCanonicalDecl() != Right.Template->getCanonicalDecl() ||
        !sameArguments(Left.Canonical, Right.Canonical) ||
        Left.DefaultsOverflow != Right.DefaultsOverflow ||
        Left.Defaults.size() != Right.Defaults.size() ||
        Left.ParameterTypes.size() != Right.ParameterTypes.size())
      return false;
    for (unsigned I = 0; I < Left.Defaults.size(); ++I) {
      const auto &L = Left.Defaults[I];
      const auto &R = Right.Defaults[I];
      if (L.Parameter != R.Parameter || L.Written.getSourceRange() != R.Written.getSourceRange() ||
          L.Converted.getSourceRange() != R.Converted.getSourceRange())
        return false;
    }
    for (unsigned I = 0; I < Left.ParameterTypes.size(); ++I) {
      const auto &L = Left.ParameterTypes[I];
      const auto &R = Right.ParameterTypes[I];
      if (L.Parameter != R.Parameter || L.PackIndex != R.PackIndex || !L.Type || !R.Type ||
          !A.Context.hasSameType(L.Type->getType(), R.Type->getType()) ||
          L.Type->getTypeLoc().getSourceRange() != R.Type->getTypeLoc().getSourceRange())
        return false;
    }
    // Every matched event is traversed, including fresh substituted expression
    // nodes. Equivalent result types alone never exempt their written source.
    return true;
  }
  void checkFunctionTemplateUse(const FunctionDecl *Function, SourceLocation Location,
                                 llvm::ArrayRef<TemplateArgumentLoc> Written,
                                 SourceLocation QualifiedBegin = {}) {
    if (!concreteFreeFunctionTemplate(Function))
      return;
    if (QualifiedBegin == Location)
      QualifiedBegin = {};
    const TemplateUseSource *First = nullptr;
    // Overload-call deduction uses the unresolved expression's begin location,
    // including a written namespace qualifier. Address selection and explicit
    // directives use the name location. Both still identify this exact function.
    for (auto UseLocation : {Location, QualifiedBegin}) {
      if (UseLocation.isInvalid())
        continue;
      auto Found = FunctionSources.find({Function, UseLocation.getRawEncoding()});
      if (Found == FunctionSources.end())
        continue;
      for (const auto *Source : Found->second) {
        if ((First && !equivalentUse(*First, *Source)) ||
            !sameArguments(Source->Canonical, Function->getTemplateSpecializationArgs())) {
          A.reject(Location, "selected template source conflict", "Repeated selected deductions must preserve equivalent complete source evidence.");
          return;
        }
        First = Source;
        if (!checkTemplateUse(*Source, Written))
          return;
      }
    }
    if (!First)
      A.reject(Location, "selected function template source", "The selected function needs its exact successful deduction source.");
  }
  void indexTemplateSources(llvm::ArrayRef<TemplateUseSource> Sources,
                            llvm::ArrayRef<FunctionSpecializationSource> Specializations) {
    if (!A.S.coreV2())
      return;
    for (const auto &Source : Sources) {
      A.chargeExpansion(1, Source.Location);
      switch (Source.Kind) {
      case TemplateSourceKind::Type:
        TypeSources[{Source.Type, Source.Location.getRawEncoding()}].push_back(&Source);
        break;
      case TemplateSourceKind::Function:
        if (const auto *Function = dyn_cast_or_null<FunctionDecl>(Source.Declaration))
          FunctionSources[{Function, Source.Location.getRawEncoding()}].push_back(&Source);
        break;
      case TemplateSourceKind::ClassDeclaration:
        ClassSources[Source.Declaration].push_back(&Source);
        break;
      case TemplateSourceKind::PartialDeduction:
      case TemplateSourceKind::PartialPattern: {
        auto &Pair = PartialSources[Source.Selection];
        auto &Slot = Source.Kind == TemplateSourceKind::PartialDeduction
                         ? Pair.Deduction : Pair.Pattern;
        if (Slot || !Source.Selection)
          Pair.Conflict = true;
        else
          Slot = &Source;
        break;
      }
      }
    }
    for (const auto &Source : Specializations) {
      A.chargeExpansion(1, Source.Location);
      SpecializationSources[Source.Declaration].push_back(&Source);
    }
  }
  void indexTemplates() {
    if (!A.S.coreV2())
      return;
    std::vector<Decl *> Work{A.Context.getTranslationUnitDecl()};
    A.chargeExpansion(1, Work.back()->getLocation());
    while (!Work.empty()) {
      auto *D = Work.back();
      Work.pop_back();
      if (auto *Template = dyn_cast<FunctionTemplateDecl>(D); Template && owned(Template)) {
        auto *Canonical = Template->getCanonicalDecl();
        if (!A.TemplateOrdinals.count(Canonical))
          A.TemplateOrdinals.emplace(Canonical, A.TemplateOrdinals.size());
      }
      if (const auto *Template = dyn_cast<NamedDecl>(D);
          Template && isa<FunctionTemplateDecl, ClassTemplateDecl, TypeAliasTemplateDecl,
                          ClassTemplatePartialSpecializationDecl>(Template))
        indexTemplatePackSources(Template);
      if (const auto *Class = dyn_cast<ClassTemplateDecl>(D);
          Class && owned(Class) && Class == Class->getCanonicalDecl()) {
        llvm::SmallVector<ClassTemplatePartialSpecializationDecl *, 4> Partials;
        Class->getPartialSpecializations(Partials);
        for (const auto *Partial : Partials)
          for (const auto *Declaration : Partial->redecls())
            if (const auto *Pattern = dyn_cast<ClassTemplatePartialSpecializationDecl>(Declaration))
              indexTemplatePackSources(Pattern);
      }
      // Out-of-line member definitions are separate lexical namespace entries.
      if (const auto *Method = dyn_cast<CXXMethodDecl>(D)) {
        if (const auto *Primary = classTemplatePattern(Method->getParent()))
          recordFunctionPacks(Method, Primary);
      } else if (const auto *Variable = dyn_cast<VarDecl>(D);
                 Variable && Variable->isStaticDataMember()) {
        const auto *Parent = dyn_cast<CXXRecordDecl>(Variable->getDeclContext());
        if (const auto *Primary = classTemplatePattern(Parent))
          recordOuterPacks(Variable, Primary);
      }
      const DeclContext *Context = nullptr;
      if (auto *Unit = dyn_cast<TranslationUnitDecl>(D))
        Context = Unit;
      else if (auto *Namespace = dyn_cast<NamespaceDecl>(D); Namespace && owned(Namespace))
        Context = Namespace;
      else if (auto *Linkage = dyn_cast<LinkageSpecDecl>(D); Linkage && owned(Linkage))
        Context = Linkage;
      if (!Context)
        continue;
      auto Begin = Work.size();
      for (auto *Child : Context->decls()) {
        A.chargeExpansion(1, Child->getLocation());
        Work.push_back(Child);
      }
      std::reverse(Work.begin() + Begin, Work.end());
    }
  }
  bool TraverseTypeAliasTemplateDecl(TypeAliasTemplateDecl *D) {
    if (!A.S.coreV2() || !owned(D))
      return RecursiveASTVisitor<Allowlist>::TraverseTypeAliasTemplateDecl(D);
    if (!WalkUpFromTypeAliasTemplateDecl(D))
      return false;
    if (!aliasTemplateShape(D)) {
      A.reject(D->getLocation(), "alias template", "An owned namespace alias with supported type or scalar parameters is required.");
      return true;
    }
    if (!traverseTemplateParameterSource(D->getTemplateParameters()))
      return false;
    auto *Pattern = D->getTemplatedDecl();
    auto Type = Pattern->getUnderlyingType();
    if (Type.isNull() || Type->isDependentType() || Type->isInstantiationDependentType())
      return true;
    // Even an unused nondependent alias retains ordinary source checks. An
    // erased canonical type such as Ignore<T> is still instantiation-dependent.
    return TraverseDecl(Pattern);
  }
  bool TraverseTemplateSpecializationTypeLoc(TemplateSpecializationTypeLoc TL) {
    auto Normal = [&] {
      return RecursiveASTVisitor<Allowlist>::TraverseTemplateSpecializationTypeLoc(TL);
    };
    if (!A.S.coreV2() || !A.S.owns(A.Sources, TL.getTemplateNameLoc()))
      return Normal();
    const auto *Type = TL.getTypePtr();
    if (Type->isDependentType() || Type->isInstantiationDependentType()) {
      if (TemplateParameterTypeSource)
        return Normal();
      A.reject(TL.getTemplateNameLoc(), "dependent template source", "A materialized source type must have concrete arguments.");
      return true;
    }
    if (!WalkUpFromTemplateSpecializationTypeLoc(TL) ||
        (shouldWalkTypesOfTypeLocs() &&
         !WalkUpFromTemplateSpecializationType(const_cast<TemplateSpecializationType *>(Type))))
      return false;
    auto Found = TypeSources.find({Type, TL.getTemplateNameLoc().getRawEncoding()});
    const TemplateUseSource *First = nullptr;
    if (Found != TypeSources.end())
      for (const auto *Source : Found->second) {
        if (!Source->Written || Source->Written->NumTemplateArgs != TL.getNumArgs())
          continue;
        bool Matches = true;
        for (unsigned I = 0; I < TL.getNumArgs(); ++I)
          Matches &= sameArgumentSource(TL.getArgLoc(I), Source->Written->arguments()[I]);
        if (!Matches)
          continue;
        auto *Template = Type->getTemplateName().getAsTemplateDecl();
        if (!Template || Template->getCanonicalDecl() != Source->Template->getCanonicalDecl() ||
            (First && !equivalentUse(*First, *Source))) {
          A.reject(TL.getTemplateNameLoc(), "template type source conflict", "Type sugar and exact use evidence must identify the same primary and arguments.");
          return true;
        }
        First = Source;
        if (!checkTemplateUse(*Source, Source->Written->arguments()))
          return false;
      }
    if (!First)
      A.reject(TL.getTemplateNameLoc(), "template type source", "A concrete template type requires its exact written substitution evidence.");
    return true;
  }
  bool TraverseSubstTemplateTypeParmTypeLoc(SubstTemplateTypeParmTypeLoc TL) {
    if (!A.S.coreV2())
      return RecursiveASTVisitor<Allowlist>::TraverseSubstTemplateTypeParmTypeLoc(TL);
    const auto *Type = TL.getTypePtr();
    auto L = TL.getBeginLoc();
    A.chargeExpansion(1, L);
    const auto *Primary = templateSourceOwner(Type->getAssociatedDecl());
    if (!owned(Type->getAssociatedDecl()) || !templateSourceShape(Primary) ||
        Type->getIndex() >= templateSourceParameters(Primary)->size()) {
      A.reject(L, "template type replacement", "A type substitution requires its exact admitted parameter owner.");
      return true;
    }
    const auto *Parameter = dyn_cast<TemplateTypeParmDecl>(
        templateSourceParameters(Primary)->getParam(Type->getIndex()));
    if (!Parameter || Parameter->getDepth() || !owned(Parameter) ||
        Parameter->isParameterPack() != Type->getPackIndex().has_value() ||
        (Type->getPackIndex() && *Type->getPackIndex() >= 64) ||
        Type->getReplacementType().isNull() ||
        Type->getReplacementType()->isInstantiationDependentType()) {
      A.reject(L, "template type parameter", "The resolved type must match a guarded type parameter and pack position.");
      return true;
    }
    if (isa<TypeAliasTemplateDecl, ClassTemplatePartialSpecializationDecl>(Primary) ||
        hasSourceFrame(Primary)) {
      const auto *Argument = isa<ClassTemplatePartialSpecializationDecl>(Primary)
          ? partialSourceEdge(Type->getAssociatedDecl(), Primary, Type->getIndex(), Type->getPackIndex(), L)
          : sourceEdge(Primary, Type->getIndex(), Type->getPackIndex(), L);
      if (!Argument || Argument->getKind() != TemplateArgument::Type ||
          !A.Context.hasSameType(Argument->getAsType(), Type->getReplacementType())) {
        A.reject(L, "template type source edge", "The replacement must equal the previously checked type argument.");
        return true;
      }
    }
    // A retained substitution is a parameter edge, never a new alias use at
    // the parameter's synthetic name location. Its source slot was checked
    // by the enclosing use; concrete function/class signatures keep their
    // existing canonical argument and ordinary written-source validation.
    A.type(Type->getReplacementType(), L, true);
    return WalkUpFromSubstTemplateTypeParmTypeLoc(TL) &&
           (!shouldWalkTypesOfTypeLocs() ||
            WalkUpFromSubstTemplateTypeParmType(const_cast<SubstTemplateTypeParmType *>(Type)));
  }
  bool VisitDeclRefExpr(DeclRefExpr *Reference) {
    if (!A.S.coreV2() || !A.S.owns(A.Sources, Reference->getLocation()))
      return true;
    if (const auto *Function = dyn_cast<FunctionDecl>(Reference->getDecl());
        concreteFreeFunctionTemplate(Function))
      checkFunctionTemplateUse(Function, Reference->getLocation(), Reference->template_arguments(),
          Reference->hasQualifier() ? Reference->getBeginLoc() : SourceLocation());
    return true;
  }
  bool TraverseFunctionTemplateDecl(FunctionTemplateDecl *D) {
    if (!A.S.coreV2() || !owned(D))
      return RecursiveASTVisitor<Allowlist>::TraverseFunctionTemplateDecl(D);
    if (!WalkUpFromFunctionTemplateDecl(D))
      return false;
    if (!functionTemplateShape(D)) {
      A.reject(D->getLocation(), "function template",
               "Only ordinary owned namespace function templates with up to 64 type or scalar value parameters and bounded concrete packs are supported.");
      return true;
    }
    A.chargeExpansion(1 + D->getTemplateParameters()->size(), D->getLocation());
    // TypeLoc retains source expressions that disappear from a folded type.
    // Direct dependent T/auto have no expression and remain lazy metadata.
    if (!traverseTemplateParameterSource(D->getTemplateParameters()))
      return false;
    // Sema has finished. Inspect materialized definitions, not dependent
    // patterns or unused overload candidates that have only a signature.
    if (D != D->getCanonicalDecl())
      return true;
    for (auto *Specialization : D->specializations()) {
      for (auto *Declaration : Specialization->redecls()) {
        A.chargeExpansion(1, Declaration->getLocation());
        auto Kind = Declaration->getTemplateSpecializationKind();
        if (!Declaration->hasBody() &&
            (Kind == TSK_Undeclared || Kind == TSK_ImplicitInstantiation))
          continue;
        if (!TraverseDecl(Declaration))
          return false;
      }
    }
    return true;
  }
  bool TraverseClassTemplateDecl(ClassTemplateDecl *D) {
    if (!A.S.coreV2() || !owned(D))
      return RecursiveASTVisitor<Allowlist>::TraverseClassTemplateDecl(D);
    if (!WalkUpFromClassTemplateDecl(D))
      return false;
    if (!classTemplateShape(D)) {
      A.reject(D->getLocation(), "class template",
               "Only owned namespace class templates with supported parameters and field/type/member-function declarations are admitted.");
      return true;
    }
    A.chargeExpansion(1 + D->getTemplateParameters()->size(), D->getLocation());
    if (!traverseTemplateParameterSource(D->getTemplateParameters()))
      return false;
    if (D != D->getCanonicalDecl())
      return true;
    for (auto *Specialization : D->specializations()) {
      for (auto *Redeclaration : Specialization->redecls()) {
        auto *Declaration = cast<ClassTemplateSpecializationDecl>(Redeclaration);
        A.chargeExpansion(1, Declaration->getLocation());
        auto Kind = Declaration->getTemplateSpecializationKind();
        if (!Declaration->getDefinition() &&
            (Kind == TSK_Undeclared || Kind == TSK_ImplicitInstantiation))
          continue;
        if (!TraverseDecl(Declaration))
          return false;
      }
    }
    return true;
  }
  bool TraverseClassTemplateSpecializationDecl(ClassTemplateSpecializationDecl *D) {
    if (!A.S.coreV2() || !owned(D))
      return RecursiveASTVisitor<Allowlist>::TraverseClassTemplateSpecializationDecl(D);
    if (auto Found = ClassSources.find(D); Found != ClassSources.end())
      for (const auto *Source : Found->second)
        if (!checkTemplateUse(*Source, Source->Written->arguments()))
          return false;
    if (const auto *Written = D->getTemplateArgsAsWritten())
      for (const auto &Argument : Written->arguments()) {
        A.chargeExpansion(1, D->getLocation());
        if (!TraverseTemplateArgumentLoc(Argument))
          return false;
      }
    if (!checkClassPartialSource(D, D->getLocation()))
      return false;
    if (!A.S.Diagnostics.empty())
      return true;
    // Traverse the concrete record without enabling all implicit AST nodes.
    return RecursiveASTVisitor<Allowlist>::TraverseCXXRecordDecl(D);
  }
  bool TraverseClassTemplatePartialSpecializationDecl(ClassTemplatePartialSpecializationDecl *D) {
    if (!A.S.coreV2() || !owned(D))
      return RecursiveASTVisitor<Allowlist>::TraverseClassTemplatePartialSpecializationDecl(D);
    if (!VisitDecl(D))
      return false;
    if (!classPartialShape(D)) {
      A.reject(D->getLocation(), "partial specialization", "An owned namespace class partial with admitted parameters and members is required.");
      return true;
    }
    A.chargeExpansion(1 + D->getTemplateParameters()->size(), D->getLocation());
    if (!traverseTemplateParameterSource(D->getTemplateParameters()))
      return false;
    for (const auto &Argument : D->getTemplateArgsAsWritten()->arguments()) {
      A.chargeExpansion(1, Argument.getLocation());
      if (!Argument.getArgument().isInstantiationDependent() &&
          !TraverseTemplateArgumentLoc(Argument))
        return false;
    }
    return true;
  }
  bool TraverseTemplateArgumentLoc(const TemplateArgumentLoc &Argument) {
    if (A.S.coreV2() && Argument.getArgument().getKind() == TemplateArgument::Type) {
      auto Type = Argument.getArgument().getAsType();
      // RAV visits a written builtin TypeLoc without admitting its type. This
      // also covers nondependent defaults of otherwise unused templates.
      if (!Type.isNull() && !Type->isDependentType() &&
          !Type->isInstantiationDependentType())
        A.type(Type, Argument.getLocation(), true);
    }
    if (A.S.coreV2() && Argument.getArgument().getKind() == TemplateArgument::Integral)
      if (auto *Written = Argument.getSourceIntegralExpression())
        return TraverseStmt(Written);
    return RecursiveASTVisitor<Allowlist>::TraverseTemplateArgumentLoc(Argument);
  }
  bool TraverseParmVarDecl(ParmVarDecl *D) {
    if (!A.S.coreV2() || !lazyTemplateDefault(D))
      return RecursiveASTVisitor<Allowlist>::TraverseParmVarDecl(D);
    // RAV normally traverses the uninstantiated pattern expression here.
    // Its concrete type is still checked; only Sema-selected defaults run.
    if (!WalkUpFromParmVarDecl(D))
      return false;
    const auto *Info = D->getTypeSourceInfo();
    return !Info || TraverseTypeLoc(Info->getTypeLoc());
  }
  bool TraverseFunctionProtoTypeLoc(FunctionProtoTypeLoc TL) {
    auto Normal = [&] {
      return RecursiveASTVisitor<Allowlist>::TraverseFunctionProtoTypeLoc(TL);
    };
    if (!A.S.coreV2() ||
        (!concreteFreeFunctionTemplate(CurrentFunction) &&
         !concreteMemberFunction(CurrentFunction)))
      return Normal();
    const auto *Info = CurrentFunction->getTypeSourceInfo();
    auto Outer = Info ? Info->getTypeLoc().IgnoreParens().getAs<FunctionProtoTypeLoc>()
                      : FunctionProtoTypeLoc();
    if (!Outer || Outer.getOpaqueData() != TL.getOpaqueData() ||
        Outer.getType() != TL.getType())
      return Normal();
    const auto *Written = TL.getTypePtr();
    const auto *Resolved = CurrentFunction->getType()->getAs<FunctionProtoType>();
    const auto *OldExpression = Written->getNoexceptExpr();
    if (!Resolved || !OldExpression ||
        (!OldExpression->isTypeDependent() && !OldExpression->isValueDependent() &&
         !OldExpression->isInstantiationDependent()) ||
        OldExpression == Resolved->getNoexceptExpr())
      return Normal();
    auto *Expression = Resolved->getNoexceptExpr();
    if (!standardExceptionSpecification(Resolved) ||
        (Expression && (Expression->isTypeDependent() || Expression->isValueDependent() ||
                        Expression->isInstantiationDependent()))) {
      A.reject(CurrentFunction->getLocation(), "template exception specification",
               "A concrete function requires a resolved standard exception specification.");
      return true;
    }
    // Instantiation can replace FunctionDecl's type while its TypeSourceInfo
    // retains the primary's dependent noexcept. Keep all other written source.
    if (!WalkUpFromFunctionProtoTypeLoc(TL) ||
        (shouldWalkTypesOfTypeLocs() &&
         !WalkUpFromFunctionProtoType(const_cast<FunctionProtoType *>(Written))) ||
        !TraverseTypeLoc(TL.getReturnLoc()))
      return false;
    for (unsigned I = 0; I < TL.getNumParams(); ++I) {
      if (auto *Parameter = TL.getParam(I)) {
        if (!TraverseDecl(Parameter))
          return false;
      } else if (I < Written->getNumParams() &&
                 !TraverseType(Written->getParamType(I))) {
        return false;
      }
    }
    for (auto Exception : Written->exceptions())
      if (!TraverseType(Exception))
        return false;
    return !Expression || TraverseStmt(Expression);
  }
  bool TraverseDecl(Decl *D) {
    if (A.S.coreV2()) {
      if (auto *Variable = dyn_cast_or_null<VarDecl>(D);
          Variable && Variable->isStaticDataMember() && owned(Variable)) {
        const auto *Parent = dyn_cast<CXXRecordDecl>(Variable->getDeclContext());
        if (const auto *Primary = classTemplatePattern(Parent);
            Primary && Parent->isDependentContext()) {
          if (!classPatternShape(Primary) || !classTemplateStaticDataShape(Variable) ||
              Variable->getNumTemplateParameterLists() > 1) {
            A.reject(Variable->getLocation(), "class static member pattern",
                     "An admitted scalar static member of an owned class template is required.");
            return true;
          }
          for (unsigned I = 0; I < Variable->getNumTemplateParameterLists(); ++I) {
            const auto *Parameters = Variable->getTemplateParameterList(I);
            A.chargeExpansion(1, Variable->getLocation());
            if (!templateParametersShape(Parameters)) {
              A.reject(Variable->getLocation(), "class static template parameters",
                       "An admitted outer class parameter list is required.");
              return true;
            }
            if (!traverseTemplateParameterSource(Parameters))
              return false;
          }
          return true; // The member type and initializer retain normal laziness.
        }
        if (const auto *Primary = classStaticDataPattern(Variable)) {
          if (!classPatternShape(Primary) || !classTemplateStaticDataShape(Variable) ||
              !owned(Variable->getInstantiatedFromStaticDataMember())) {
            A.reject(Variable->getLocation(), "class static member instance",
                     "An owned scalar static member with matching template origin is required.");
            return true;
          }
          A.chargeExpansion(1, Variable->getLocation());
          if (!CheckedTemplateStaticDeclarations.insert(Variable).second)
            return true;
          auto Kind = Variable->getTemplateSpecializationKind();
          if ((Kind == TSK_Undeclared || Kind == TSK_ImplicitInstantiation) &&
              !Variable->getDefinition() && !Variable->getAnyInitializer() &&
              !Variable->isUsed(/*CheckUsedAttr=*/false) && !Variable->isReferenced())
            return true; // Do not manufacture an unused definition or initializer.
        }
      }
      if (const auto *Method = dyn_cast_or_null<CXXMethodDecl>(D);
          Method && owned(Method)) {
        if (const auto *Primary = classTemplatePattern(Method->getParent());
            Primary && Method->getParent()->isDependentContext()) {
          // Out-of-line definitions are separate declarations in the namespace.
          // Their own outer parameter spelling must be checked before erasure.
          if (!classPatternShape(Primary) || !classTemplateFunctionShape(Method) ||
              Method->getNumTemplateParameterLists() > 1) {
            A.reject(Method->getLocation(), "class template function pattern",
                     "An admitted member function of an owned namespace class template is required.");
            return true;
          }
          for (unsigned I = 0; I < Method->getNumTemplateParameterLists(); ++I) {
            const auto *Parameters = Method->getTemplateParameterList(I);
            A.chargeExpansion(1, Method->getLocation());
            if (!templateParametersShape(Parameters)) {
              A.reject(Method->getLocation(), "class function template parameters",
                       "An admitted outer class parameter list is required.");
              return true;
            }
            if (!traverseTemplateParameterSource(Parameters))
              return false;
          }
          return true; // No uninstantiated body, qualifier or function default.
        }
        if (const auto *Primary = classFunctionPattern(Method)) {
          if (!classPatternShape(Primary) || !classTemplateFunctionShape(Method)) {
            A.reject(Method->getLocation(), "class template function",
                     "An admitted member-function instance of an owned class pattern is required.");
            return true;
          }
          auto Kind = Method->getTemplateSpecializationKind();
          if (!Method->hasBody() && defaultedSpecialMember(Method)) {
            if (!Method->isTrivial() && Method->isUsed(/*CheckUsedAttr=*/false)) {
              A.reject(Method->getLocation(), "generated definition",
                       "A used nontrivial defaulted member needs a generated definition.", "TR0203");
              return true;
            }
            if (!Method->isReferenced())
              return true; // Preserve lazy deletion and unresolved specifications.
            if (!defaultedLifecycle(Method) && !defaultedAssignment(Method) &&
                !defaultedCopyOrMoveConstructor(dyn_cast<CXXConstructorDecl>(Method))) {
              A.reject(Method->getLocation(), "defaulted member specification",
                       "An admitted concrete defaulted special member is required.");
              return true;
            }
            auto *SavedFunction = CurrentFunction;
            auto *SavedMethod = CurrentMethod;
            CurrentFunction = Method;
            CurrentMethod = Method;
            auto Restore = llvm::make_scope_exit([&] {
              CurrentFunction = SavedFunction;
              CurrentMethod = SavedMethod;
            });
            const auto *Info = Method->getTypeSourceInfo();
            return Info && TraverseTypeLoc(Info->getTypeLoc());
          }
          if (!Method->hasBody() &&
              (Kind == TSK_Undeclared || Kind == TSK_ImplicitInstantiation)) {
            if (const auto *Destructor = dyn_cast<CXXDestructorDecl>(Method)) {
              // Returning directly into caller storage may need a definition
              // without emitting a cleanup call in the current function.
              if (Destructor->isUsed(/*CheckUsedAttr=*/false)) {
                A.reject(Destructor->getLocation(), "destructor definition",
                         "A required destructor needs a definition in this source unit.",
                         "TR0203");
                return true;
              }
              if (Destructor->isReferenced()) {
                // Unevaluated references resolve noexcept without requiring a
                // body. Check its written/resolved source with method context.
                if (!ordinaryDestructor(Destructor)) {
                  A.reject(Destructor->getLocation(), "destructor specification",
                           "An admitted resolved destructor specification is required.");
                  return true;
                }
                auto *SavedFunction = CurrentFunction;
                auto *SavedMethod = CurrentMethod;
                CurrentFunction = Method;
                CurrentMethod = Method;
                auto Restore = llvm::make_scope_exit([&] {
                  CurrentFunction = SavedFunction;
                  CurrentMethod = SavedMethod;
                });
                return TraverseTypeLoc(Destructor->getTypeSourceInfo()->getTypeLoc());
              }
            }
            return true; // Includes still-undeduced auto results of unused methods.
          }
          if (!concreteClassFunction(Method)) {
            A.reject(Method->getLocation(), "class template function type",
                     "A materialized member function requires a resolved type.");
            return true;
          }
          if (!CheckedTemplateDeclarations.insert(Method).second)
            return true;
        }
      }
    }
    if (A.S.coreV2())
      if (const auto *Record = dyn_cast_or_null<ClassTemplateSpecializationDecl>(D);
          Record && !CheckedClassTemplateDeclarations.insert(Record).second)
        return true;
    if (A.S.coreV2())
      if (const auto *Function = dyn_cast_or_null<FunctionDecl>(D);
          concreteFreeFunctionTemplate(Function) &&
          !CheckedTemplateDeclarations.insert(Function).second)
        return true;
    if (A.S.coreV2())
      if (const auto *Method = dyn_cast_or_null<CXXMethodDecl>(D))
        indexLocalFunctionPacks(Method);
    auto *SavedFunction = CurrentFunction;
    if (auto *Function = dyn_cast_or_null<FunctionDecl>(D))
      CurrentFunction = Function;
    auto RestoreFunction = llvm::make_scope_exit([&] { CurrentFunction = SavedFunction; });
    auto *SavedField = CurrentDefaultField;
    if (auto *Field = dyn_cast_or_null<FieldDecl>(D))
      CurrentDefaultField = owned(Field) && Field->hasInClassInitializer() ? Field : nullptr;
    auto RestoreField = llvm::make_scope_exit([&] { CurrentDefaultField = SavedField; });
    auto *Saved = CurrentMethod;
    if (D && isa<FunctionDecl>(D))
      CurrentMethod = dyn_cast<CXXMethodDecl>(D);
    if (A.S.coreV2())
      if (const auto *Function = dyn_cast_or_null<FunctionDecl>(D))
        if (auto Found = SpecializationSources.find(Function); Found != SpecializationSources.end())
          for (const auto *Source : Found->second)
            checkFunctionTemplateUse(Source->Selected, Source->Location,
                Source->Written ? Source->Written->arguments() : llvm::ArrayRef<TemplateArgumentLoc>{});
    bool Result = RecursiveASTVisitor<Allowlist>::TraverseDecl(D);
    if (const auto *C = dyn_cast_or_null<CXXConstructorDecl>(D);
        Result && A.S.coreV2() && C && owned(C) &&
        !defaultedLifecycle(C) && !defaultedCopyOrMoveConstructor(C) &&
        C->doesThisDeclarationHaveABody()) {
      // RAV skips non-written initializers in both TraverseFunctionHelper and
      // TraverseConstructorInitializer. Inspect these semantic expressions
      // explicitly; written expressions were already visited by RAV.
      std::set<const Decl *> Initialized;
      for (const auto *I : C->inits()) {
        if (!I->isMemberInitializer() || I->isPackExpansion() ||
            I->getMember()->getParent() != C->getParent() ||
            !Initialized.insert(I->getMember()->getCanonicalDecl()).second ||
            !I->getInit()) {
          A.reject(C->getLocation(), "constructor initializer",
                   "Only unique direct field initializers are supported.");
          continue;
        }
        if (!I->isWritten()) {
          auto PreviousOwner = ImplicitInitializerOwner;
          ImplicitInitializerOwner = C->getLocation();
          Result = TraverseStmt(I->getInit());
          ImplicitInitializerOwner = PreviousOwner;
          if (!Result)
            break;
        }
      }
    }
    // Out-of-line template definitions can be hidden namespace declarations.
    // Source checking must follow the actual initializer owner, not only the use.
    if (auto *Variable = dyn_cast_or_null<VarDecl>(D);
        Result && A.S.coreV2() && classStaticDataPattern(Variable))
      if (auto *Definition = Variable->getDefinition(); Definition && Definition != Variable)
        Result = TraverseDecl(Definition);
    CurrentMethod = Saved;
    return Result;
  }
  void finishGeneratedMethods() {
    // Inspect selected definitions only. RAV normally skips defaulted bodies;
    // visiting all implicit declarations would broaden source admission.
    for (std::size_t Index = 0; Index < GeneratedMethods.size(); ++Index) {
      const auto *Method = GeneratedMethods[Index];
      const auto *C = dyn_cast<CXXConstructorDecl>(Method);
      const auto *Body = dyn_cast_or_null<CompoundStmt>(Method->getBody());
      if (!Body || (C ? ((!defaultedLifecycle(C) && !defaultedCopyOrMoveConstructor(C)) ||
                         !Body->body_empty()) : !defaultedAssignment(Method))) {
        A.reject(Method->getLocation(), "generated method",
                 "Expected an admitted defaulted definition with a semantic body.");
        continue;
      }
      auto *SavedMethod = CurrentMethod;
      auto *SavedFunction = CurrentFunction;
      auto SavedOwner = ImplicitInitializerOwner;
      CurrentMethod = Method;
      CurrentFunction = Method;
      ImplicitInitializerOwner = Method->getLocation();
      auto Restore = llvm::make_scope_exit([&] {
        CurrentMethod = SavedMethod;
        CurrentFunction = SavedFunction;
        ImplicitInitializerOwner = SavedOwner;
      });
      A.type(Method->getReturnType(), Method->getLocation(), true);
      A.type(Method->getThisType(), Method->getLocation());
      for (const auto *Parameter : Method->parameters())
        A.type(Parameter->getType(), Method->getLocation());
      if (C) {
        std::set<const Decl *> Initialized;
        for (const auto *I : C->inits()) {
          if (!I->isMemberInitializer() || I->isPackExpansion() ||
              I->getMember()->getParent() != C->getParent() || !I->getInit() ||
              !Initialized.insert(I->getMember()->getCanonicalDecl()).second) {
            A.reject(C->getLocation(), "generated constructor initializer",
                     "Only unique direct field initializers are supported.");
            continue;
          }
          TraverseStmt(I->getInit());
        }
      }
      TraverseStmt(const_cast<CompoundStmt *>(Body));
      A.Functions.push_back(const_cast<CXXMethodDecl *>(Method));
    }
  }
  bool TraverseCXXForRangeStmt(CXXForRangeStmt *Loop) {
    if (!A.S.coreV2())
      return RecursiveASTVisitor<Allowlist>::TraverseCXXForRangeStmt(Loop);
    const auto SavedOwner = ImplicitInitializerOwner;
    ImplicitInitializerOwner = Loop->getForLoc();
    auto RestoreOwner = llvm::make_scope_exit([&] { ImplicitInitializerOwner = SavedOwner; });
    if (!WalkUpFromCXXForRangeStmt(Loop))
      return false;
    const auto Parts = rangeForComponents(Loop);
    if (!Parts || !A.registerRangeFor(Loop)) {
      A.reject(Loop->getForLoc(), "range for",
               "A resolved C++17 range with one ordinary loop variable is required.");
      return true;
    }
    // RAV omits these declarations and their resolved calls by default.
    // Register all three identities before checking a range initializer's MTE.
    for (const auto *V : {Parts->Range, Parts->Begin, Parts->End}) {
      if (!WalkUpFromVarDecl(const_cast<VarDecl *>(V)) ||
          !TraverseStmt(const_cast<Expr *>(V->getInit())))
        return false;
    }
    // The written type still needs ordinary declaration traversal. RAV skips
    // the semantic initializer of the user isCXXForRangeDecl variable.
    return TraverseDecl(const_cast<VarDecl *>(Parts->Variable)) &&
           TraverseStmt(const_cast<Expr *>(Parts->Variable->getInit())) &&
           TraverseStmt(Loop->getCond()) && TraverseStmt(Loop->getInc()) &&
           TraverseStmt(Loop->getBody());
  }
  bool TraverseMaterializeTemporaryExpr(MaterializeTemporaryExpr *Temporary) {
    if (!A.S.coreV2())
      return RecursiveASTVisitor<Allowlist>::TraverseMaterializeTemporaryExpr(Temporary);
    if (!WalkUpFromMaterializeTemporaryExpr(Temporary))
      return false;
    if (const auto *Descriptor = Temporary->getLifetimeExtendedTemporaryDecl()) {
      if (Descriptor->getTemporaryExpr() != Temporary->getSubExpr() ||
          Descriptor->getExtendingDecl() != Temporary->getExtendingDecl() ||
          Descriptor->getStorageDuration() != Temporary->getStorageDuration()) {
        A.reject(Temporary->getExprLoc(), "temporary lifetime",
                 "The lifetime descriptor must identify this materialized temporary.");
        return true;
      }
    }
    // RAV otherwise visits a LifetimeExtendedTemporaryDecl that Clang creates
    // without setting isImplicit(). It describes this same operand, not a
    // source declaration. WalkUp has checked its type, duration and owner;
    // inspect the operand synchronously without broadening declaration admission.
    return TraverseStmt(Temporary->getSubExpr());
  }
  bool TraverseCXXDefaultArgExpr(CXXDefaultArgExpr *Default) {
    const auto *P = Default->getParam();
    auto L = Default->getUsedLocation();
    if (L.isInvalid() && P)
      L = P->getLocation();
    auto SavedOwner = ImplicitInitializerOwner;
    auto *SavedField = CurrentDefaultField;
    auto *SavedMethod = CurrentMethod;
    ImplicitInitializerOwner = L;
    CurrentDefaultField = nullptr;
    CurrentMethod = nullptr;
    auto Restore = llvm::make_scope_exit([&] {
      ImplicitInitializerOwner = SavedOwner;
      CurrentDefaultField = SavedField;
      CurrentMethod = SavedMethod;
    });
    if (!WalkUpFromCXXDefaultArgExpr(Default))
      return false;
    const auto *Init = selectedDefaultArgument(Default, A.Context);
    if (!A.S.coreV2() || !Init || !owned(P) || !A.S.owns(A.Sources, L)) {
      A.reject(L, "default argument",
               "A resolved source-owned parameter default is required.");
      return true;
    }
    A.chargeExpansion(1, L);
    // This AST node has no children. Traverse its selected expression now,
    // without borrowing a caller's this or caching a per-use runtime value.
    return TraverseStmt(const_cast<Expr *>(Init));
  }
  bool TraverseCXXDefaultInitExpr(CXXDefaultInitExpr *Default) {
    const auto *Field = Default->getField();
    auto L = Default->getUsedLocation();
    if (L.isInvalid() && Field)
      L = Field->getLocation();
    auto SavedOwner = ImplicitInitializerOwner;
    ImplicitInitializerOwner = L;
    auto RestoreOwner = llvm::make_scope_exit([&] { ImplicitInitializerOwner = SavedOwner; });
    if (!WalkUpFromCXXDefaultInitExpr(Default))
      return false;
    if (!A.S.coreV2() || !Field || !owned(Field) || !Field->hasInClassInitializer() ||
        !isa<CXXRecordDecl>(Field->getParent()) || !Default->getExpr() ||
        !A.Context.hasSameUnqualifiedType(Field->getType(), Default->getType()) ||
        !A.Context.hasSameUnqualifiedType(Default->getExpr()->getType(), Default->getType())) {
      A.reject(L, "default member initializer", "Expected a selected source-owned field initializer.");
      return true;
    }
    A.type(A.Context.getRecordType(Field->getParent()), L);
    A.chargeExpansion(1, L);
    auto *SavedField = CurrentDefaultField;
    CurrentDefaultField = Field;
    auto RestoreField = llvm::make_scope_exit([&] { CurrentDefaultField = SavedField; });
    // Synchronous traversal keeps the field context alive. Use the selected
    // expression, which can include Clang's semantic rewrite of the default.
    return TraverseStmt(Default->getExpr());
  }
  bool TraverseArrayInitLoopExpr(ArrayInitLoopExpr *Loop) {
    // Use synchronous traversal: RAV's queued traversal would inspect children
    // after the binding scope had ended. Still run generic expression checks.
    if (!WalkUpFromArrayInitLoopExpr(Loop))
      return false;
    auto L = ImplicitInitializerOwner;
    const auto *Array = A.Context.getAsConstantArrayType(Loop->getType());
    const auto *Common = Loop->getCommonExpr();
    const auto *Source = Common ? Common->getSourceExpr() : nullptr;
    if (!A.S.coreV2() || !L.isValid() ||
        !defaultedCopyOrMoveConstructor(dyn_cast_or_null<CXXConstructorDecl>(CurrentMethod)) ||
        !Array || !Source || !Source->isGLValue() ||
        Source->getValueKind() != Common->getValueKind() ||
        (cast<CXXConstructorDecl>(CurrentMethod)->isCopyConstructor() && !Source->isLValue()) ||
        !A.Context.hasSameUnqualifiedType(Source->getType(), Loop->getType()) ||
        !A.Context.hasSameType(Source->getType(), Common->getType()) ||
        !Loop->getSubExpr() ||
        !A.Context.hasSameUnqualifiedType(Array->getElementType(), Loop->getSubExpr()->getType()) ||
        Array->getSize() != Loop->getArraySize() ||
        !Array->getSize().getLimitedValue(65537) ||
        Array->getSize().getLimitedValue(65537) > 65536 ||
        A.storageUnits(Loop->getType()) > 200000 || ArraySources.count(Common)) {
      A.reject(L, "generated array initialization", "Expected bounded semantic member-array copying or moving.");
      return true;
    }
    A.chargeExpansion(1, L);
    // A nested common source references the containing loop's index. Bind the
    // inner index only after that source has been checked in the outer scope.
    if (!TraverseStmt(const_cast<Expr *>(Source)))
      return false;
    ArraySources.emplace(Common, Source);
    auto RestoreSource = llvm::make_scope_exit([&] { ArraySources.erase(Common); });
    if (!TraverseStmt(const_cast<OpaqueValueExpr *>(Common)))
      return false;
    ++ArrayIndexDepth;
    auto RestoreIndex = llvm::make_scope_exit([&] { --ArrayIndexDepth; });
    return TraverseStmt(Loop->getSubExpr());
  }
  bool VisitDecl(Decl *D) {
    if (!owned(D))
      return true;
    if (D->hasAttrs())
      A.reject(D->getLocation(), "attribute",
               "Source declaration attributes are unsupported.");
    const bool ExtendedDeclaration =
        A.S.coreV2() &&
        (isa<TypedefNameDecl, EnumDecl, EnumConstantDecl, StaticAssertDecl, FriendDecl,
             NamespaceAliasDecl, UsingDirectiveDecl, UsingDecl,
             FunctionTemplateDecl, ClassTemplateDecl, TypeAliasTemplateDecl>(D) ||
         D->getKind() == Decl::UsingShadow ||
         (isa<AccessSpecDecl>(D) && D->getDeclContext()->isRecord()));
    if (!ExtendedDeclaration &&
        !isa<NamespaceDecl, LinkageSpecDecl, FunctionDecl, VarDecl,
             CXXRecordDecl, FieldDecl>(D))
      A.reject(D->getLocation(), D->getDeclKindName(),
               "Declaration is outside the selected profile.");
    if (const auto *N = dyn_cast<NamespaceDecl>(D); N && N->isInline() && !A.S.coreV2())
      A.reject(D->getLocation(), "inline namespace",
               "Inline namespaces are not in the core profile.");
    if (A.S.math())
      if (const auto *N = dyn_cast<NamespaceDecl>(D); N && N->isStdNamespace())
        A.reject(D->getLocation(), "owned std namespace",
                 "Owned declarations cannot extend or impersonate an approved "
                 "standard-library namespace.");
    return true;
  }
  bool VisitNamespaceDecl(NamespaceDecl *D) {
    if (!owned(D) || !A.S.coreV2() || !D->isNested() || !D->isInline())
      return true;
    // Sema inherits inline status when reopening a namespace. In the C++17
    // nested spelling its start token remains ::; an explicit inline token
    // at that position is the C++20 extension, even when macro-expanded.
    A.chargeExpansion(1, D->getBeginLoc());
    Token Start;
    auto L = A.Sources.getSpellingLoc(D->getBeginLoc());
    if (L.isInvalid() ||
        Lexer::getRawToken(L, Start, A.Sources, A.Context.getLangOpts()) ||
        !Start.is(tok::coloncolon))
      A.reject(D->getBeginLoc(), "nested inline namespace",
               "An inline keyword inside a nested namespace definition requires C++20.");
    return true;
  }
  bool VisitNamespaceAliasDecl(NamespaceAliasDecl *D) {
    if (owned(D) && A.S.coreV2() &&
        (D->isInvalidDecl() || !importContext(D->getDeclContext()) ||
         !namespaceTarget(D, D->getLocation())))
      A.reject(D->getLocation(), "namespace alias",
               "Expected a bounded alias chain to an owned namespace.");
    return true;
  }
  bool VisitUsingDirectiveDecl(UsingDirectiveDecl *D) {
    if (owned(D) && A.S.coreV2() &&
        (D->isInvalidDecl() || !importContext(D->getDeclContext()) ||
         (D->getQualifier() && D->getQualifier()->isDependent()) ||
         !namespaceTarget(D->getNominatedNamespaceAsWritten(), D->getUsingLoc())))
      A.reject(D->getUsingLoc(), "using directive",
               "Expected a resolved directive to an owned namespace.");
    return true;
  }
  bool VisitUsingDecl(UsingDecl *D) {
    if (!owned(D) || !A.S.coreV2())
      return true;
    if (!usingShape(D)) {
      A.reject(D->getLocation(), "using declaration",
               "Expected an ordinary resolved using-declaration in namespace or block scope.");
      return true;
    }
    // A repeated import may have no new shadows. Lookup and overload visibility
    // are already resolved by Clang at each source use, including later defaults.
    for (const auto *Shadow : D->shadows()) {
      A.chargeExpansion(1, D->getLocation());
      usingTarget(Shadow);
    }
    return true;
  }
  bool VisitUsingShadowDecl(UsingShadowDecl *D) {
    if (owned(D) && A.S.coreV2())
      usingTarget(D);
    return true;
  }
  bool VisitFriendDecl(FriendDecl *D) {
    if (!owned(D) || !A.S.coreV2())
      return true;
    const auto *Parent = dyn_cast<CXXRecordDecl>(D->getDeclContext());
    if (D->isInvalidDecl() || !Parent || !owned(Parent) ||
        Parent->isDependentContext() || D->isUnsupportedFriend() ||
        D->isPackExpansion() || D->getFriendTypeNumTemplateParameterLists()) {
      A.reject(D->getFriendLoc(), "friend declaration",
               "A resolved non-template friend in a supported owned class is required.");
      return true;
    }
    if (const auto *Type = D->getFriendType()) {
      if (Type->getType().isNull() || Type->getType()->isDependentType())
        A.reject(D->getFriendLoc(), "friend type", "A resolved supported friend type is required.");
      else
        A.type(Type->getType(), D->getFriendLoc(), true);
    } else {
      const auto *Function = dyn_cast_or_null<FunctionDecl>(D->getFriendDecl());
      if (!Function || !owned(Function) ||
          Function->getTemplatedKind() != FunctionDecl::TK_NonTemplate)
        A.reject(D->getFriendLoc(), "friend function",
                 "An ordinary source-owned function declaration is required.");
    }
    // RAV still traverses the written type/owned tag or complete inner
    // declaration. Friendship never exempts defaults or bodies from checking.
    return true;
  }
  bool VisitTypedefNameDecl(TypedefNameDecl *D) {
    if (owned(D) && A.S.coreV2())
      A.type(D->getUnderlyingType(), D->getLocation(), true);
    return true;
  }
  bool VisitEnumDecl(EnumDecl *D) {
    if (owned(D) && A.S.coreV2())
      A.type(A.Context.getTypeDeclType(D), D->getLocation());
    return true;
  }
  bool TraverseStaticAssertDecl(StaticAssertDecl *D) {
    if (!A.S.coreV2())
      return RecursiveASTVisitor<Allowlist>::TraverseStaticAssertDecl(D);
    if (!WalkUpFromStaticAssertDecl(D))
      return false;
    // Sema validates the assertion. Still inspect its condition so constant
    // evaluation cannot conceal unsupported source. The message is diagnostic
    // text, not a runtime string expression to translate.
    return TraverseStmt(D->getAssertExpr());
  }
  bool VisitFunctionDecl(FunctionDecl *D) {
    if (!owned(D))
      return true;
    const bool Template = A.S.coreV2() && concreteFreeFunctionTemplate(D);
    const bool InstantiatedMember = A.S.coreV2() && concreteMemberFunction(D);
    if (Template) {
      const auto *Primary = D->getPrimaryTemplate();
      const auto *Arguments = D->getTemplateSpecializationArgs();
      if (!functionTemplateShape(Primary) || !Arguments ||
          Arguments->size() != templateSourceParameters(Primary)->size()) {
        A.reject(D->getLocation(), "template specialization",
                 "A concrete specialization must match an admitted owned primary template.");
        return true;
      }
      checkTemplateArguments(templateSourceParameters(Primary), *Arguments, D->getLocation());
    }
    A.type(D->getReturnType(), D->getLocation(), true);
    const auto *Method = dyn_cast<CXXMethodDecl>(D);
    const bool Defaulted = A.S.coreV2() &&
        (defaultedLifecycle(Method) || defaultedAssignment(Method) ||
         defaultedCopyOrMoveConstructor(dyn_cast_or_null<CXXConstructorDecl>(Method)));
    if ((Method && (!A.S.coreV2() ||
                    (!callableMethod(Method) &&
                     !supportedConstructor(dyn_cast<CXXConstructorDecl>(Method)) &&
                     !ordinaryDestructor(dyn_cast<CXXDestructorDecl>(Method)) && !Defaulted))) ||
        D->isVariadic() ||
        D->getDescribedFunctionTemplate() ||
        (D->getTemplatedKind() != FunctionDecl::TK_NonTemplate &&
         !Template && !InstantiatedMember) ||
        D->isDeletedAsWritten() || (D->isExplicitlyDefaulted() && !Defaulted) ||
        D->isConsteval())
      A.reject(D->getLocation(), "function",
               "This member, template, variadic or special function form is "
               "outside the selected profile.");
    if (A.S.coreV2() && D->isOverloadedOperator() &&
        !ordinaryOperator(D) && !supportedAssignment(Method))
      A.reject(D->getLocation(), "operator declaration",
               "This operator function is outside the selected profile.");
    const auto *Prototype = D->getType()->getAs<FunctionProtoType>();
    if (Prototype && Prototype->hasExceptionSpec() && !Defaulted &&
        !(A.S.coreV2() && (standardExceptionSpecification(Prototype) ||
                           ordinaryDestructor(dyn_cast<CXXDestructorDecl>(D)))))
      A.reject(D->getLocation(), "exception specification",
               "This exception specification is outside the selected profile.");
    if (D->isMain() &&
        (D->getNumParams() ||
         !D->getReturnType()->isSpecificBuiltinType(BuiltinType::Int)))
      A.reject(D->getLocation(), "main",
               "Only no-argument int main() is supported.");
    if (D->isExternC() &&
        (!A.S.project() || D->getFormalLinkage() != Linkage::Internal)) {
      if (!validExportName(D->getName()) || D->getName() == "main")
        A.reject(D->getLocation(), "C export",
                 "C export name is reserved or incompatible with NeverC.");
      if (D->getReturnType()->isRecordType())
        A.reject(D->getLocation(), "C export",
                 "C ABI exports require scalar results and parameters.");
      for (const auto *P : D->parameters())
        if (P->getType()->isRecordType())
          A.reject(P->getLocation(), "C export",
                   "C ABI exports require scalar results and parameters.");
    }
    if (!D->hasBody() && !Defaulted &&
        (!A.S.project() || D->getFormalLinkage() == Linkage::Internal))
      A.reject(
          D->getLocation(), "function declaration",
          "Every declared function needs a definition in this source unit.",
          "TR0203");
    if (A.S.project()) {
      auto &Declaration = A.FunctionDeclarations[D->getCanonicalDecl()];
      if (!Declaration || D->doesThisDeclarationHaveABody())
        Declaration = D;
    }
    // Explicit instantiation can generate a body without a runtime caller.
    if (InstantiatedMember && Defaulted && D->hasBody()) {
      if (isa<CXXDestructorDecl>(D)) {
        const auto *Body = dyn_cast_or_null<CompoundStmt>(D->getBody());
        if (!Body || !Body->body_empty())
          A.reject(D->getLocation(), "defaulted destructor",
                   "A defaulted destructor requires an empty generated body.");
      } else {
        queueGenerated(Method, D->getLocation());
      }
    }
    // Materialized user destructor bodies retain source-unit checks even if
    // uncalled. Their helpers include the member destruction epilogue.
    if (D->doesThisDeclarationHaveABody() && !D->isImplicit() && !Defaulted) {
      if (const auto *Destructor = dyn_cast<CXXDestructorDecl>(D);
          A.S.coreV2() && Destructor)
        A.requireDestruction(Destructor->getParent(), Destructor->getLocation());
      else
        A.Functions.push_back(D);
    }
    return true;
  }
  bool VisitVarDecl(VarDecl *D) {
    if (!owned(D))
      return true;
    A.type(D->getType(), D->getLocation());
    const auto *Parameter = dyn_cast<ParmVarDecl>(D);
    const bool HasDefault = Parameter && Parameter->hasDefaultArg();
    if (A.S.coreV2() && lazyTemplateDefault(Parameter))
      return true;
    if (HasDefault && (!A.S.coreV2() ||
                       !defaultArgumentInitializer(Parameter, A.Context))) {
      A.reject(D->getLocation(), "default argument",
               "Only resolved core-v2 parameter defaults are supported.");
      return true;
    }
    if (A.S.coreV2() && D->isStaticDataMember()) {
      const auto *Parent = dyn_cast<CXXRecordDecl>(D->getDeclContext());
      if (D->getKind() != Decl::Var || !Parent || !owned(Parent) ||
          Parent->isDependentContext() ||
          !D->getType()->isIntegralOrEnumerationType() ||
          D->getTLSKind() != VarDecl::TLS_None ||
          D->getType().isVolatileQualified()) {
        A.reject(D->getLocation(), "static data member",
                 "Only non-thread-local scalar members in supported owned classes are admitted.");
        return true;
      }
      auto *Definition = D->getDefinition();
      if (!Definition && !D->isInline() && D->getType().isConstQualified()) {
        const VarDecl *InitializingDecl = nullptr;
        const auto *Init = D->getAnyInitializer(InitializingDecl);
        APValue Value;
        if (Init && InitializingDecl && owned(InitializingDecl) &&
            InitializingDecl->getCanonicalDecl() == D->getCanonicalDecl() &&
            A.Context.hasSameType(InitializingDecl->getType(), D->getType()) &&
            D->isUsableInConstantExpressions(A.Context) &&
            Init->isCXX11ConstantExpr(A.Context, &Value) && Value.isInt()) {
          if (A.StaticMemberValues.emplace(D->getCanonicalDecl(), Value.getInt()).second)
            A.chargeExpansion(1, D->getLocation());
          return true; // Checked value metadata, not a storage definition.
        }
      }
      const auto *DefinitionParent = Definition
          ? dyn_cast<CXXRecordDecl>(Definition->getDeclContext()) : nullptr;
      if (!Definition || !owned(Definition) || Definition->getKind() != Decl::Var ||
          !Definition->isStaticDataMember() || !DefinitionParent ||
          DefinitionParent->getCanonicalDecl() != Parent->getCanonicalDecl() ||
          Definition->getCanonicalDecl() != D->getCanonicalDecl() ||
          !A.Context.hasSameType(Definition->getType(), D->getType())) {
        A.reject(D->getLocation(), "static data definition",
                 "A scalar static member requires one source-owned definition in this unit.",
                 "TR0203");
        return true;
      }
      const VarDecl *InitializingDecl = nullptr;
      if (const auto *Init = Definition->getAnyInitializer(InitializingDecl)) {
        APValue Value;
        if (!InitializingDecl || !owned(InitializingDecl) ||
            InitializingDecl->getCanonicalDecl() != D->getCanonicalDecl() ||
            !A.Context.hasSameType(InitializingDecl->getType(), D->getType()) ||
            !Init->isCXX11ConstantExpr(A.Context, &Value) || !Value.isInt()) {
          A.reject(D->getLocation(), "static data initializer",
                   "A scalar static member requires a source-owned constant initializer.");
          return true;
        }
      } else if (D->getType().isConstQualified()) {
        A.reject(D->getLocation(), "static data initializer",
                 "A const static member requires an initializer in this unit.");
        return true;
      }
      if (CheckedScalarGlobals.insert(Definition->getCanonicalDecl()).second)
        A.Globals.push_back(Definition);
      // A non-inline const member's initializer can belong to its in-class
      // declaration. RAV still checks every written initializer and body.
      return true;
    }
    if (A.S.coreV2() && D->isStaticLocal()) {
      const auto *Parent = dyn_cast<FunctionDecl>(D->getDeclContext()->getRedeclContext());
      const auto *LexicalParent =
          dyn_cast<FunctionDecl>(D->getLexicalDeclContext()->getRedeclContext());
      auto *Definition = D->getDefinition();
      if (D->getKind() != Decl::Var || D->isImplicit() || !D->isLocalVarDecl() ||
          !Parent || !LexicalParent || !owned(Parent) ||
          Parent->getCanonicalDecl() != LexicalParent->getCanonicalDecl() ||
          Parent->isDependentContext() || Parent->isConstexpr() ||
          (Parent->getTemplatedKind() != FunctionDecl::TK_NonTemplate &&
           !concreteFreeFunctionTemplate(Parent) && !concreteMemberFunction(Parent)) ||
          D->hasExternalStorage() || D->getTLSKind() != VarDecl::TLS_None ||
          D->getType().isVolatileQualified() ||
          !D->getType()->isIntegralOrEnumerationType() || Definition != D) {
        A.reject(D->getLocation(), "static local",
                 "Only owned non-volatile scalar static locals in non-constexpr functions are supported.");
        return true;
      }
      if (const auto *Init = D->getInit()) {
        APValue Value;
        if (!Init->isCXX11ConstantExpr(A.Context, &Value) || !Value.isInt()) {
          A.reject(D->getLocation(), "static local initializer",
                   "A scalar static local requires zero or fully defined constant initialization.");
          return true;
        }
      } else if (D->getType().isConstQualified()) {
        A.reject(D->getLocation(), "static local initializer",
                 "A const static local requires a constant initializer.");
        return true;
      }
      if (A.StaticLocals.insert(D->getCanonicalDecl()).second) {
        A.chargeExpansion(1, D->getLocation());
        A.Globals.push_back(D);
      }
      // The declaration allocates static storage, not a block-entry action.
      // RAV still checks every written initializer, including folded operations.
      return true;
    }
    if (A.S.coreV2() && D->getType()->isReferenceType() && D->getInit())
      checkBinding(D->getInit(), HasDefault,
                   D->getKind() == Decl::Var && D->isLocalVarDecl() && D->hasLocalStorage()
                       ? D->getCanonicalDecl() : nullptr);
    if (A.S.coreV2() && !D->isLocalVarDeclOrParm() &&
        needsDestruction(D->getType())) {
      A.reject(D->getLocation(), "global destruction",
               "Global records requiring destruction need static lifetime lowering.");
      return true;
    }
    if (!D->isLocalVarDeclOrParm() &&
        (D->getType()->isPointerType() || D->getType()->isReferenceType() ||
         (A.S.coreV2() && containsArray(D->getType())))) {
      A.reject(D->getLocation(), "global variable",
               "Pointer/reference/array globals require global lifetime lowering.");
      return true;
    }
    if (D->getTLSKind() != VarDecl::TLS_None || D->isStaticLocal() ||
        (D->isLocalVarDecl() && D->hasExternalStorage()))
      A.reject(D->getLocation(), "storage duration",
               "Static/thread-local locals and local extern declarations are "
               "unsupported.");
    if (!D->isLocalVarDeclOrParm()) {
      if (A.S.coreV2() && !D->getType().isConstQualified() &&
          D->getType()->isIntegralOrEnumerationType()) {
        auto *Definition = D->getDefinition();
        if (!Definition || !owned(Definition) || Definition->getKind() != Decl::Var ||
            Definition->isStaticDataMember() ||
            !Definition->getDeclContext()->getRedeclContext()->isFileContext() ||
            Definition->getCanonicalDecl() != D->getCanonicalDecl() ||
            !A.Context.hasSameType(Definition->getType(), D->getType())) {
          A.reject(D->getLocation(), "global definition",
                   "A scalar global requires one source-owned definition in this unit.",
                   "TR0203");
          return true;
        }
        if (Definition->getTLSKind() != VarDecl::TLS_None ||
            Definition->getType().isVolatileQualified()) {
          A.reject(D->getLocation(), "global storage",
                   "Thread-local and volatile globals require separate storage semantics.");
          return true;
        }
        if (const auto *Init = Definition->getInit()) {
          APValue Value;
          if (!Init->isCXX11ConstantExpr(A.Context, &Value) || !Value.isInt()) {
            A.reject(Definition->getLocation(), "global initializer",
                     "A scalar global requires zero or fully defined constant initialization.");
            return true;
          }
        }
        if (CheckedScalarGlobals.insert(Definition->getCanonicalDecl()).second)
          A.Globals.push_back(Definition);
        // RAV still visits each source declaration's initializer. Constant
        // folding does not exempt its selected operations from the allowlist.
        return true;
      }
      if (A.S.project()) {
        auto &Declaration = A.GlobalDeclarations[D->getCanonicalDecl()];
        if (!Declaration || D->getInit())
          Declaration = D;
      }
      if (A.S.project() && D->getType().isConstQualified() && !D->getInit() &&
          D->hasExternalStorage() && D->getFormalLinkage() != Linkage::Internal)
        return true;
      if (!D->getType().isConstQualified() || !D->getInit()) {
        A.reject(D->getLocation(), "global variable",
                 "Only initialized compile-time const globals are supported.");
      } else {
        APValue Value;
        if (!D->getInit()->isCXX11ConstantExpr(A.Context, &Value))
          A.reject(D->getLocation(), "global initializer",
                   "Dynamic or partially undefined global initialization is "
                   "unsupported.");
        else
          A.Globals.push_back(D);
      }
    }
    return true;
  }
  bool VisitCXXRecordDecl(CXXRecordDecl *D) {
    if (!owned(D))
      return true;
    if (!D->isCompleteDefinition()) {
      if (!D->getDefinition())
        A.reject(D->getLocation(), "record",
                 "Incomplete records are unsupported.");
      return true;
    }
    if (A.S.coreV2())
      if (const auto *Specialization = dyn_cast<ClassTemplateSpecializationDecl>(D)) {
        const auto *Primary = Specialization->getSpecializedTemplate();
        const auto *Pattern = classTemplatePattern(Specialization);
        if (Specialization->getKind() != Decl::ClassTemplateSpecialization ||
            D->isDependentContext() || !classTemplateShape(Primary) || !classPatternShape(Pattern) ||
            !D->isStandardLayout() || !classTemplateMembers(D)) {
          A.reject(D->getLocation(), "class template specialization",
                   "A concrete standard-layout specialization of an admitted owned primary is required.");
          return true;
        }
        checkTemplateArguments(templateSourceParameters(Primary),
                               Specialization->getTemplateArgs(), D->getLocation());
        if (isa<ClassTemplatePartialSpecializationDecl>(Pattern))
          checkTemplateArguments(templateSourceParameters(Pattern),
                                 Specialization->getTemplateInstantiationArgs(), D->getLocation());
      }
    // Special-member behavior is checked at each selected operation. A record
    // containing a user-copyable field can be aggregate-initialized without
    // selecting its unsupported implicit nontrivial copy constructor.
    const bool ConstructedRecord = A.S.coreV2() && D->isStandardLayout();
    if (D->isUnion() || (!D->isAggregate() && !ConstructedRecord) ||
        (A.S.coreV2() && !D->isStandardLayout()) ||
        (!A.S.coreV2() && D->field_empty()) ||
        D->getNumBases() || D->getDescribedClassTemplate() ||
        (D->getDeclContext()->isRecord() &&
         (!A.S.coreV2() || !D->getIdentifier())))
      A.reject(D->getLocation(), "record",
               "Only standard-layout records with supported selected special "
               "members and no bases are admitted; empty and named nested "
               "records require core v2.");
    A.Records.push_back(D);
    return true;
  }
  bool VisitFieldDecl(FieldDecl *D) {
    if (!owned(D))
      return true;
    A.type(D->getType(), D->getLocation());
    if (D->isBitField() || (!A.S.coreV2() && D->hasInClassInitializer()) || D->isMutable() ||
        D->getType().isConstQualified() || D->getType()->isReferenceType())
      A.reject(D->getLocation(), "field",
               "Bitfield and mutable/const/reference fields are unsupported; "
               "default field initializers require core v2.");
    return true;
  }
  bool VisitStmt(Stmt *S) {
    if (!S || (!ImplicitInitializerOwner.isValid() &&
               !A.S.owns(A.Sources, S->getBeginLoc())))
      return true;
    auto L = S->getBeginLoc().isValid() ? S->getBeginLoc()
                                        : ImplicitInitializerOwner;
    if (A.S.coreV2()) {
      checkStaticValueUse(S);
      if (const auto *Query = dyn_cast<SizeOfPackExpr>(S))
        checkPackSize(Query);
      if (const auto *Substitution = dyn_cast<SubstNonTypeTemplateParmExpr>(S)) {
        A.chargeExpansion(1, L);
        if (!scalarTemplateReplacement(Substitution, A.Context) ||
            !owned(Substitution->getAssociatedDecl()) ||
            !owned(Substitution->getParameter()) ||
            !templateSourceShape(scalarTemplateOwner(Substitution)))
          A.reject(L, "template value replacement",
                   "A checked scalar replacement from an admitted owned template is required.");
        else
          checkScalarSourceEdge(Substitution, L);
      }
    }
    if (A.S.coreV2())
      if (const auto *List = emptyVoidInitializer(dyn_cast<Expr>(S)))
        EmptyVoidLists.insert(List);
    if (A.S.coreV2() && ImplicitInitializerOwner.isValid())
      if (const auto *Call = dyn_cast<CallExpr>(S))
        if (auto Copy = generatedArrayAssignment(Call, CurrentMethod, A.Context)) {
          A.type(Copy->Type, L);
          if (A.storageUnits(Copy->Type) > 200000)
            A.reject(L, "generated array assignment", "Array assignment exceeds the storage limit.");
          GeneratedArrayAssignments.insert(Call);
          const Expr *E = Call->getCallee();
          while (true) {
            GeneratedBuiltinCallees.insert(E);
            if (const auto *P = dyn_cast<ParenExpr>(E))
              E = P->getSubExpr();
            else if (const auto *C = dyn_cast<ImplicitCastExpr>(E))
              E = C->getSubExpr();
            else
              break;
          }
        }
    // The visitor is preorder. Mark only the direct callee path before its
    // children are inspected, including Clang's BoundMemberTy expressions.
    if (A.S.coreV2())
      if (const auto *Call = dyn_cast<CallExpr>(S))
        if (const auto *Leaf = directMethodReference(Call)) {
          const Expr *E = Call->getCallee();
          while (true) {
            DirectMethodCallees.insert(E);
            if (E == Leaf)
              break;
            if (const auto *P = dyn_cast<ParenExpr>(E))
              E = P->getSubExpr();
            else
              E = cast<ImplicitCastExpr>(E)->getSubExpr();
          }
        }
    if (const auto *E = dyn_cast<Expr>(S)) {
      // Clang's unevaluated diagnostic strings have no QualType. They can
      // appear below an already rejected declaration (for example a v1
      // static_assert), so diagnose them before inspecting expression types.
      if (E->getType().isNull()) {
        if (A.S.coreV2())
          if (const auto *List = dyn_cast<InitListExpr>(E);
              List && EmptyVoidLists.count(List))
            return true;
        A.reject(E->getExprLoc(), "untyped expression",
                 "Diagnostic-only source text is not a translatable value.");
        return true;
      }
      if (const auto *WrittenCast = dyn_cast<ExplicitCastExpr>(E))
        A.type(WrittenCast->getTypeAsWritten(), E->getExprLoc(), true);
      if (A.S.coreV2())
        if (const auto *C = dyn_cast<CastExpr>(E); C && !GeneratedBuiltinCallees.count(C)) {
          // Enum initializers and static assertions are erased after checking.
          // Validate their operations before erasure, not only in lowering.
          switch (C->getCastKind()) {
          case CK_LValueToRValue:
          case CK_NoOp:
          case CK_IntegralCast:
          case CK_IntegralToBoolean:
          case CK_FunctionToPointerDecay:
          case CK_NullToPointer:
          case CK_PointerToBoolean:
          case CK_ArrayToPointerDecay:
            break;
          case CK_ToVoid:
            if (!C->isPRValue() || !C->getType()->isVoidType() ||
                C->isTypeDependent() || C->isValueDependent() ||
                C->isInstantiationDependent() || !C->getSubExpr() ||
                (C->getSubExpr()->getType().isNull() && !emptyVoidInitializer(C)))
              A.reject(L, "void expression",
                       "A resolved void cast with a checked operand is required.");
            break;
          case CK_UserDefinedConversion:
            if (!userConversionCall(C, A.Context))
              A.reject(L, "user conversion", "A checked direct conversion-function call is required.");
            break;
          case CK_ConstructorConversion: {
            if (constructorConversion(C, A.Context))
              break;
            A.reject(L, "constructor conversion",
                     "Only direct admitted constructor conversions are supported.");
            break;
          }
          case CK_BitCast:
            if (C->getType()->isPointerType() &&
                C->getSubExpr()->getType()->isPointerType() &&
                (C->getType()->getPointeeType()->isVoidType() ||
                 C->getSubExpr()->getType()->getPointeeType()->isVoidType()))
              break;
            [[fallthrough]];
          default:
            A.reject(E->getExprLoc(), "cast",
                     "This cast operation is outside the core v2 profile.");
          }
        }
      const auto *Cast = dyn_cast<ImplicitCastExpr>(E);
      bool FunctionDecay =
          Cast && Cast->getCastKind() == CK_FunctionToPointerDecay;
      if (!DirectMethodCallees.count(E) && !GeneratedBuiltinCallees.count(E) && !FunctionDecay &&
          !E->getType()->isFunctionType() &&
          !(A.S.coreV2() &&
            isa<CXXNullPtrLiteralExpr>(E->IgnoreParens())))
        A.type(E->getType(), E->getExprLoc(), true);
    }
    const auto *Opaque = dyn_cast<OpaqueValueExpr>(S);
    const bool GeneratedArrayNode = A.S.coreV2() &&
        ImplicitInitializerOwner.isValid() &&
        defaultedCopyOrMoveConstructor(dyn_cast_or_null<CXXConstructorDecl>(CurrentMethod)) &&
        (isa<ArrayInitLoopExpr>(S) || (Opaque && ArraySources.count(Opaque)) ||
         (isa<ArrayInitIndexExpr>(S) && ArrayIndexDepth));
    if (!GeneratedArrayNode && !(A.S.math() && isa<FloatingLiteral>(S)) &&
        !(A.S.coreV2() &&
          isa<ConstantExpr, CXXNullPtrLiteralExpr, CXXConstCastExpr,
              CXXFunctionalCastExpr, ArraySubscriptExpr, SwitchStmt, CaseStmt,
              DefaultStmt, AttributedStmt, CharacterLiteral,
              UnaryExprOrTypeTraitExpr, CXXNoexceptExpr, CXXThisExpr,
              CXXDefaultInitExpr, CXXDefaultArgExpr, CXXForRangeStmt,
              SubstNonTypeTemplateParmExpr, SizeOfPackExpr>(S)) &&
        !isa<CompoundStmt, DeclStmt, NullStmt, ReturnStmt, IfStmt, WhileStmt,
             DoStmt, ForStmt, BreakStmt, ContinueStmt, IntegerLiteral,
             CXXBoolLiteralExpr, DeclRefExpr, ParenExpr, ImplicitCastExpr,
             CStyleCastExpr, CXXStaticCastExpr, BinaryOperator, UnaryOperator,
             CallExpr, MemberExpr, InitListExpr, ImplicitValueInitExpr,
             CXXScalarValueInitExpr, CXXConstructExpr, CXXTemporaryObjectExpr,
             MaterializeTemporaryExpr, ExprWithCleanups, CXXBindTemporaryExpr,
             ConditionalOperator>(S))
      A.reject(S->getBeginLoc(), S->getStmtClassName(),
               "Expression or statement is outside the selected profile.");
    if (A.S.coreV2()) {
      if (const auto *M = dyn_cast<MaterializeTemporaryExpr>(S);
          M && !fullExpressionTemporary(M, A.Context) && !A.temporaryOwner(M))
        A.reject(L, "temporary lifetime",
                 "A checked full-expression temporary or exact automatic reference owner is required.");
      if (const auto *Query = dyn_cast<CXXNoexceptExpr>(S)) {
        if (!Query->getOperand() || Query->isTypeDependent() ||
            Query->isValueDependent() || Query->isInstantiationDependent())
          A.reject(Query->getExprLoc(), "noexcept query",
                   "A resolved constant noexcept query is required.");
        // RAV visits the unevaluated operand and written specification
        // expressions. Unsupported source must not disappear behind a bool.
      }
      if (const auto *Query = dyn_cast<UnaryExprOrTypeTraitExpr>(S);
          Query && !parameterTypeQueryMetadata(Query)) {
        auto Operand = Query->getTypeOfArgument();
        if ((Query->getKind() != UETT_SizeOf && Query->getKind() != UETT_AlignOf) ||
            (Query->getKind() == UETT_AlignOf && !Query->isArgumentType()) ||
            Operand->isVoidType() || Operand->isFunctionType() ||
            Operand->isVariablyModifiedType() || Operand->isDependentType())
          A.reject(Query->getExprLoc(), "size/alignment query",
                   "Only standard constant sizeof and type-form alignof are supported.");
        else
          A.type(Operand, Query->getExprLoc());
      }
      if (const auto *C = dyn_cast<CaseStmt>(S); C && C->getRHS())
        A.reject(S->getBeginLoc(), "case range",
                 "GNU case ranges are outside the core v2 profile.");
      if (const auto *Attributed = dyn_cast<AttributedStmt>(S)) {
        if (!isa<NullStmt>(Attributed->getSubStmt()) ||
            Attributed->getAttrs().empty() ||
            !std::all_of(Attributed->getAttrs().begin(),
                         Attributed->getAttrs().end(),
                         [](const Attr *A) { return isa<FallThroughAttr>(A); }))
          A.reject(S->getBeginLoc(), "statement attribute",
                   "Only a validated fallthrough annotation is supported.");
      }
    }
    if (const auto *C = dyn_cast<CallExpr>(S)) {
      if (GeneratedArrayAssignments.count(C))
        return true; // Its typed argument subtrees are still visited by RAV.
      const auto *F = C->getDirectCallee();
      const auto *Method = dyn_cast_or_null<CXXMethodDecl>(F);
      if (A.S.coreV2() && isa_and_nonnull<CXXDestructorDecl>(F))
        A.reject(S->getBeginLoc(), "explicit destructor call",
                 "Explicit destruction requires separate lifetime restart rules.");
      const auto *Operator = dyn_cast<CXXOperatorCallExpr>(C);
      unsigned ArgumentOffset = Operator && Method && !Method->isStatic() ? 1 : 0;
      // This inline operator form already supports materialized temporaries.
      // Keep its narrow boundary while sharing reference capture and stores.
      const bool InlineMove = A.S.coreV2() && Operator && Method &&
          Operator->getOperator() == OO_Equal && Operator->getNumArgs() == 2 &&
          Method->isImplicit() && Method->isTrivial() && Method->isMoveAssignmentOperator();
      if (A.S.coreV2() && Operator) {
        bool TrivialAssignment = Operator->getOperator() == OO_Equal && Method &&
                                 Method->isImplicit() && Method->isTrivial() &&
                                 Operator->getNumArgs() == 2;
        const bool Ordinary = ordinaryOperator(F) &&
            F->getOverloadedOperator() == Operator->getOperator();
        if (!TrivialAssignment && !Ordinary &&
            !(supportedAssignment(Method) && Operator->getOperator() == OO_Equal &&
              Operator->getNumArgs() == 2))
          A.reject(L, "overloaded operator", "Unsupported selected operator function.");
        if (F && Operator->getNumArgs() != F->getNumParams() + ArgumentOffset)
          A.reject(L, "operator arguments", "Operator and selected parameter counts differ.");
      }
      if (A.S.coreV2() && Method && callableMethod(Method)) {
        const auto *Reference = directMethodReference(C);
        if (!Reference)
          A.reject(S->getBeginLoc(), "method call",
                   "Methods require a checked direct callee.");
        else if (!Method->isStatic() && !InlineMove) {
          const Expr *Base = nullptr;
          bool Arrow = false;
          if (Operator && C->getNumArgs()) {
            Base = C->getArg(0);
          } else if (const auto *Member = dyn_cast<MemberExpr>(Reference)) {
            Base = Member->getBase();
            Arrow = Member->isArrow();
          }
          if (!Base || (Arrow ? temporaryArrayBase(Base, true) : temporaryBinding(Base, true)))
            A.reject(L, "method receiver",
                     "A supported live or full-expression temporary receiver is required.");
        }
      }
      if (A.S.coreV2() && F && !InlineMove && (!Method || callableMethod(Method)))
        for (unsigned I = 0; I < F->getNumParams() && I + ArgumentOffset < C->getNumArgs(); ++I) {
          checkDefaultArgument(C->getArg(I + ArgumentOffset), F, I, L);
          if (F->getParamDecl(I)->getType()->isReferenceType())
            checkBinding(C->getArg(I + ArgumentOffset), true);
        }
      if (A.S.math() && F &&
          (F->getBuiltinID() || !A.S.owns(A.Sources, F->getLocation()))) {
        if (A.mapping(C).empty())
          A.reject(S->getBeginLoc(), "library call",
                   "Call is not an approved std::fabs(double) or "
                   "std::floor(double) import from the pinned SDK.",
                   "TR0203");
        return true;
      }
      const bool GeneratedAssignment = A.S.coreV2() && defaultedAssignment(Method);
      if (GeneratedAssignment) {
        if (!A.S.owns(A.Sources, Method->getLocation()))
          A.reject(L, "assignment", "The selected assignment must be source-owned.", "TR0203");
        else
          queueGenerated(Method, L);
      }
      if (!F ||
          (!GeneratedAssignment && !F->isImplicit() && !F->hasBody() &&
           (!A.S.project() || F->getFormalLinkage() == Linkage::Internal ||
            F->isInlined())))
        A.reject(
            S->getBeginLoc(), "call",
            "Call target has no supported definition in this translation unit.",
            "TR0203");
    }
    if (A.S.project())
      if (const auto *Reference = dyn_cast<DeclRefExpr>(S))
        if (const auto *V = dyn_cast<VarDecl>(Reference->getDecl());
            V && !V->isLocalVarDeclOrParm() && V->isInline() &&
            !V->getDefinition())
          A.reject(S->getBeginLoc(), "inline global",
                   "A used inline variable requires a definition in this "
                   "translation unit.",
                   "TR0203");
    if (const auto *C = dyn_cast<CXXConstructExpr>(S))
      checkConstruction(C, L);
    if (A.S.coreV2())
      if (const auto *I = dyn_cast<InitListExpr>(S))
        checkSemanticInitializers(I, L);
    if (const auto *U = dyn_cast<UnaryOperator>(S)) {
      if ((!A.S.coreV2() &&
           (U->getOpcode() == UO_AddrOf || U->getOpcode() == UO_Deref)) ||
          (!A.S.coreV2() && U->isIncrementDecrementOp() &&
           U->getType()->isPointerType()) ||
          (U->getOpcode() == UO_Plus &&
           U->getSubExpr()->getType()->isPointerType()) ||
          U->getOpcode() == UO_Extension)
        A.reject(S->getBeginLoc(), "unary operator",
                 "This pointer operation or GNU extension is unsupported.");
      if (A.S.coreV2() && U->isIncrementDecrementOp() &&
          U->getType()->isPointerType()) {
        auto Pointee = U->getType()->getPointeeType();
        if (!Pointee->isObjectType() || Pointee->isIncompleteType())
          A.reject(S->getBeginLoc(), "pointer increment",
                   "Pointer increment requires a complete object pointee.");
      }
    }
    if (const auto *This = dyn_cast<CXXThisExpr>(S)) {
      const auto *Record = This->getType()->getPointeeType()->getAsCXXRecordDecl();
      bool FieldThis = CurrentDefaultField && Record &&
          Record->getCanonicalDecl() == CurrentDefaultField->getParent()->getCanonicalDecl();
      bool MethodThis = CurrentMethod && !CurrentMethod->isStatic() && Record &&
          Record->getCanonicalDecl() == CurrentMethod->getParent()->getCanonicalDecl() &&
          (callableMethod(CurrentMethod) ||
           supportedConstructor(dyn_cast<CXXConstructorDecl>(CurrentMethod)) ||
           ordinaryDestructor(dyn_cast<CXXDestructorDecl>(CurrentMethod)) ||
           defaultedLifecycle(CurrentMethod));
      if (!A.S.coreV2() || (!FieldThis && !MethodThis))
        A.reject(L, "this", "This requires its owning field initializer or an admitted instance method.");
    }
    if (const auto *Reference = dyn_cast<DeclRefExpr>(S))
      if (const auto *Method = dyn_cast<CXXMethodDecl>(Reference->getDecl());
          Method && !Method->isImplicit() && !DirectMethodCallees.count(Reference))
        A.reject(Reference->getExprLoc(), "method value",
                 "A method name is supported only as a direct call target.");
    if (const auto *M = dyn_cast<MemberExpr>(S)) {
      const auto *V = dyn_cast<VarDecl>(M->getMemberDecl());
      const bool StaticData = A.S.coreV2() && V && V->isStaticDataMember();
      if ((!A.S.coreV2() && M->isArrow()) ||
          (!isa<FieldDecl>(M->getMemberDecl()) && !StaticData &&
           !DirectMethodCallees.count(M)))
        A.reject(S->getBeginLoc(), "member access",
                 "Only fields, core-v2 scalar static data and direct method calls are supported.");
    }
    if (A.S.coreV2()) {
      if (const auto *R = dyn_cast<ReturnStmt>(S);
          R && R->getRetValue() && R->getRetValue()->isGLValue())
        checkBinding(R->getRetValue());
      if (const auto *B = dyn_cast<BinaryOperator>(S);
          B && (B->getLHS()->getType()->isPointerType() ||
                B->getRHS()->getType()->isPointerType()) &&
          B->getOpcode() != BO_Assign && B->getOpcode() != BO_Comma &&
          B->getOpcode() != BO_EQ && B->getOpcode() != BO_NE) {
        bool Offset = B->getOpcode() == BO_Add || B->getOpcode() == BO_Sub ||
                      B->getOpcode() == BO_AddAssign ||
                      B->getOpcode() == BO_SubAssign;
        if (!Offset)
          A.reject(S->getBeginLoc(), "pointer binary operator",
                   "Only pointer offsets, difference and equality are supported.");
        for (const auto *Operand : {B->getLHS(), B->getRHS()}) {
          if (!Operand->getType()->isPointerType())
            continue;
          auto Pointee = Operand->getType()->getPointeeType();
          if (!Pointee->isObjectType() || Pointee->isIncompleteType())
            A.reject(S->getBeginLoc(), "pointer arithmetic",
                     "Pointer arithmetic requires complete object pointees.");
        }
        if (B->getOpcode() == BO_Sub &&
            B->getLHS()->getType()->isPointerType() &&
            B->getRHS()->getType()->isPointerType() &&
            (!B->getType()->isSignedIntegerType() ||
             A.Context.getTypeSize(B->getType()) !=
                 A.Context.getTargetInfo().getPointerWidth(LangAS::Default)))
          A.reject(S->getBeginLoc(), "pointer difference",
                   "Source ptrdiff must be signed and match native pointer width.",
                   "TR0204");
      }
    }
    if (A.S.math()) {
      if (const auto *U = dyn_cast<UnaryOperator>(S);
          U && U->getSubExpr()->getType()->isRealFloatingType() &&
          U->getOpcode() != UO_Plus && U->getOpcode() != UO_Minus)
        A.reject(S->getBeginLoc(), "floating unary operator",
                 "Only unary plus/minus are admitted on double values.");
      if (const auto *B = dyn_cast<BinaryOperator>(S);
          B &&
          (B->getLHS()->getType()->isRealFloatingType() ||
           B->getRHS()->getType()->isRealFloatingType()) &&
          B->getOpcode() != BO_Assign && B->getOpcode() != BO_Comma &&
          !B->isComparisonOp())
        A.reject(S->getBeginLoc(), "floating binary operator",
                 "Double arithmetic and compound assignments are outside the "
                 "bounded math profile.");
    }
    if (const auto *I = dyn_cast<IfStmt>(S)) {
      if (I->isConsteval())
        A.reject(L, "if consteval", "Consteval branches require C++23.");
      else if (I->isConstexpr()) {
        const auto *Condition = I->getCond();
        if (!A.S.coreV2() || !Condition || Condition->isTypeDependent() ||
            Condition->isValueDependent() || Condition->isInstantiationDependent() ||
            !I->getNondiscardedCase(A.Context))
          A.reject(L, "if constexpr",
                   "A resolved core-v2 constant condition is required.");
        // RAV still checks the condition and both source substatements. A
        // present selection containing nullptr means false without an else.
      }
    }
    return true;
  }
};

// RAV visits an enclosing record before its nested declarations. By-value
// fields require complete definitions in both the protocol and emitted source;
// pointer fields only require the existing forward declarations.
static void orderCoreV2Records(Adapter &A) {
  llvm::DenseMap<const CXXRecordDecl *, std::size_t> Indices;
  for (std::size_t I = 0; I < A.Records.size(); ++I) {
    const auto *R = A.Records[I];
    A.chargeExpansion(1, R->getLocation());
    if (!Indices.try_emplace(R->getCanonicalDecl(), I).second) {
      A.reject(R->getLocation(), "record definition",
               "A record definition was collected more than once.");
      throw Failure{};
    }
  }
  enum class Visit { Unvisited, Active, Done };
  std::vector<Visit> State(A.Records.size(), Visit::Unvisited);
  std::vector<unsigned> Heights(A.Records.size(), 0);
  std::vector<CXXRecordDecl *> Ordered;
  Ordered.reserve(A.Records.size());
  auto Order = [&](auto &&Self, std::size_t I, unsigned Depth) -> void {
    auto *R = A.Records[I];
    if (Depth > 64 || State[I] == Visit::Active) {
      A.reject(R->getLocation(), "record dependency",
               "By-value record dependencies are cyclic or exceed the depth limit.");
      throw Failure{};
    }
    if (State[I] == Visit::Done)
      return;
    State[I] = Visit::Active;
    for (const auto *F : R->fields()) {
      A.chargeExpansion(1, F->getLocation());
      QualType T = F->getType();
      unsigned ArrayDepth = 0;
      while (const auto *Array = A.Context.getAsConstantArrayType(T)) {
        if (++ArrayDepth > 64) {
          A.reject(F->getLocation(), "record dependency",
                   "Array field nesting exceeds the depth limit.");
          throw Failure{};
        }
        A.chargeExpansion(1, F->getLocation());
        T = Array->getElementType();
      }
      const auto *Dependency = T->getAsCXXRecordDecl();
      if (!Dependency)
        continue;
      auto Found = Indices.find(Dependency->getCanonicalDecl());
      if (Found == Indices.end()) {
        A.reject(F->getLocation(), "record dependency",
                 "A by-value field requires a checked source-owned record definition.");
        throw Failure{};
      }
      Self(Self, Found->second, Depth + 1);
      // Cache subtree height as well as visitation. A dependency emitted by an
      // earlier root still contributes its full depth to the current record.
      if (Heights[Found->second] >= 64) {
        A.reject(F->getLocation(), "record dependency",
                 "By-value record nesting exceeds the depth limit.");
        throw Failure{};
      }
      Heights[I] = std::max(Heights[I], Heights[Found->second] + 1);
    }
    State[I] = Visit::Done;
    Ordered.push_back(R);
  };
  for (std::size_t I = 0; I < A.Records.size(); ++I)
    Order(Order, I, 0);
  A.Records = std::move(Ordered);
}

void Adapter::run(llvm::ArrayRef<ExplicitFunctionInstantiationSource> Directives,
                  llvm::ArrayRef<ExplicitStaticDataInstantiationSource> StaticDirectives,
                  llvm::ArrayRef<TemplateUseSource> TemplateUses,
                  llvm::ArrayRef<FunctionSpecializationSource> Specializations) {
  Allowlist Check(*this);
  Check.indexTemplates();
  Check.indexTemplateSources(TemplateUses, Specializations);
  for (const auto &Directive : Directives)
    Check.checkExplicitFunctionInstantiation(Directive);
  for (const auto &Directive : StaticDirectives)
    Check.checkExplicitStaticDataInstantiation(Directive);
  Check.TraverseDecl(Context.getTranslationUnitDecl());
  if (!S.Diagnostics.empty())
    return;
  Check.finishGeneratedMethods();
  if (!S.Diagnostics.empty())
    return;
  if (S.coreV2())
    orderCoreV2Records(*this);
  const auto &Target = Context.getTargetInfo();
  if (Target.getIntWidth() != 32 || Target.getCharWidth() != 8 ||
      !Target.isLittleEndian()) {
    S.diagnose("TR0204", "data model",
               "Core requires 32-bit int, 8-bit bytes and little endian.",
               "Select an admitted native target.");
    return;
  }
  json::Array RecordData, GlobalData, FunctionData;
  for (const auto *R : Records) {
    json::Array Fields;
    for (const auto *F : R->fields())
      Fields.push_back(json::Object{
          {"name", name(F)}, {"type", type(F->getType(), F->getLocation())}});
    json::Object Record{{"id", name(R)}, {"fields", std::move(Fields)},
                        {"loc", loc(R->getLocation())}};
    if (S.coreV2()) {
      const auto &Layout = Context.getASTRecordLayout(R);
      json::Array Offsets;
      for (unsigned I = 0; I < Layout.getFieldCount(); ++I)
        Offsets.push_back(Layout.getFieldOffset(I));
      Record["layout"] = json::Object{
          {"size_bits", uint64_t(Layout.getSize().getQuantity()) * 8},
          {"abi_align_bits", uint64_t(Layout.getAlignment().getQuantity()) * 8},
          {"field_offsets_bits", std::move(Offsets)}};
    }
    RecordData.push_back(std::move(Record));
  }
  for (const auto *G : Globals) {
    const bool Mutable = S.coreV2() && !G->getType().isConstQualified();
    json::Object Initializer;
    const auto *Init = S.coreV2() && G->isStaticDataMember()
                           ? G->getAnyInitializer() : G->getInit();
    if (Init) {
      APValue Value;
      if (!Init->isCXX11ConstantExpr(Context, &Value) || (Mutable && !Value.isInt()))
        throw Failure{};
      Initializer = constant(Value, G->getType(), G->getLocation());
    } else {
      if (!Mutable || !G->getType()->isIntegralOrEnumerationType())
        throw Failure{};
      Initializer = zero(G->getType(), G->getLocation());
    }
    json::Object Global{{"name", name(G)},
                        {"type", type(G->getType(), G->getLocation())},
                        {"value", std::move(Initializer)},
                        {"loc", loc(G->getLocation())}};
    if (Mutable)
      Global["mutable"] = true;
    GlobalData.push_back(std::move(Global));
  }
  for (auto *F : Functions)
    FunctionData.push_back(lower(F));
  if (S.coreV2())
    // Lowering a helper can request additional member/array destruction.
    // Unused implicit helpers must not force lazy template destructor bodies.
    for (std::size_t I = 0; I < Destructions.size(); ++I)
      FunctionData.push_back(lowerDestruction(Destructions[I]));
  if (S.project())
    addProjectMetadata();
  if (S.math()) {
    if (Target.getDoubleWidth() != 64 ||
        &Target.getDoubleFormat() != &llvm::APFloat::IEEEdouble()) {
      S.diagnose("TR0204", "double data model",
                 "Math profile requires IEEE binary64 double.",
                 "Use an approved math target.");
      return;
    }
    S.addSDKMetadata();
  }
  if (!S.Diagnostics.empty())
    return;
  json::Object TargetData{
      {"triple", S.Target}, {"int_bits", Target.getIntWidth()},
      {"pointer_bits", Target.getPointerWidth(LangAS::Default)},
      {"little_endian", Target.isLittleEndian()}};
  if (S.coreV2()) {
    const std::pair<const char *, QualType> Carriers[] = {
        {"bool", Context.BoolTy}, {"i8", Context.SignedCharTy},
        {"u8", Context.UnsignedCharTy}, {"i16", Context.ShortTy},
        {"u16", Context.UnsignedShortTy}, {"int", Context.IntTy},
        {"uint", Context.UnsignedIntTy}, {"i64", Context.LongLongTy},
        {"u64", Context.UnsignedLongLongTy}, {"default-pointer", Context.VoidPtrTy}};
    json::Object Layout{{"char_bits", Target.getCharWidth()}};
    for (const auto &[Name, T] : Carriers)
      Layout[Name] = json::Object{{"size_bits", Context.getTypeSize(T)},
                                   {"abi_align_bits", Context.getTypeAlign(T)}};
    TargetData["carrier_layout"] = std::move(Layout);
  }
  S.Module["target"] = std::move(TargetData);
  S.Module["records"] = std::move(RecordData);
  S.Module["globals"] = std::move(GlobalData);
  // The allowlist resolves every owned call, including unreachable source.
  // Lowering may prune its instruction, so publish evidence only for mappings
  // still referenced by the final bodies consumed by the shared verifier.
  std::set<std::string> UsedMappings;
  for (const auto &F : FunctionData)
    for (const auto &I : *F.getAsObject()->getArray("body")) {
      const auto &Instruction = *I.getAsObject();
      if (Instruction.getString("op") == "mapped_call")
        UsedMappings.insert(Instruction.getString("mapping")->str());
    }
  S.Module["functions"] = std::move(FunctionData);
  json::Array Mappings;
  for (const auto &M : MappedFunctions)
    if (UsedMappings.count(M.first))
      Mappings.push_back(json::Object(M.second));
  S.Module["mappings"] = std::move(Mappings);
  json::Array Dependencies;
  Dependencies.push_back(
      json::Object{{"path", S.Relative}, {"sha256", digest(S.SourceText)}});
  if (S.project())
    for (const auto &Dependency : S.Dependencies)
      if (Dependency.first != S.Relative)
        Dependencies.push_back(json::Object{{"path", Dependency.first},
                                            {"sha256", Dependency.second}});
  S.Module["dependencies"] = std::move(Dependencies);
}

class Diagnostics : public DiagnosticConsumer {
  State &S;
  std::set<unsigned> LaterStandardDiagnostics;

public:
  explicit Diagnostics(State &S) : S(S) {
    if (!S.coreV2())
      return;
    DiagnosticIDs IDs;
    for (auto Group : {"c++20-extensions", "c++23-extensions", "c++26-extensions"}) {
      llvm::SmallVector<diag::kind, 128> Values;
      if (IDs.getDiagnosticsInGroup(diag::Flavor::WarningOrError, Group, Values)) {
        S.diagnose("TR0102", "C++ language version",
                   "The embedded frontend is missing a required language diagnostic group.",
                   "Use a compatible NeverC frontend build.");
        continue;
      }
      LaterStandardDiagnostics.insert(Values.begin(), Values.end());
    }
  }
  void HandleDiagnostic(DiagnosticsEngine::Level Level,
                        const Diagnostic &D) override {
    DiagnosticConsumer::HandleDiagnostic(Level, D);
    const bool LaterStandard = Level == DiagnosticsEngine::Warning &&
                               LaterStandardDiagnostics.count(D.getID());
    if (Level < DiagnosticsEngine::Error && !LaterStandard)
      return;
    llvm::SmallString<256> Message;
    D.FormatDiagnostic(Message);
    unsigned Line = 1, Column = 1;
    std::string File;
    if (D.hasSourceManager()) {
      auto P = D.getSourceManager().getPresumedLoc(D.getLocation());
      if (P.isValid()) {
        Line = P.getLine();
        Column = P.getColumn();
      }
      if (S.project())
        File = S.sourcePath(D.getSourceManager(), D.getLocation());
    }
    S.diagnose(LaterStandard ? "TR0201" : "TR0202", "C++ source", Message,
               "Fix the C++17 source diagnostic before translating.", Line,
               Column, File);
  }
};

class PreprocessorPolicy : public PPCallbacks {
  State &S;
  SourceManager &SM;
  Preprocessor &PP;
  std::set<unsigned> Checked;
  std::set<std::string> OncePragmas;
  void reject(SourceLocation L, llvm::StringRef What,
              llvm::StringRef Reason =
                  "Unsupported or environment-sensitive preprocessing.",
              llvm::StringRef Code = "TR0201") {
    if (!S.owns(SM, L))
      return;
    auto P = SM.getPresumedLoc(SM.getExpansionLoc(L));
    S.diagnose(Code, What, Reason,
               S.project() ? "Use declared project-owned headers and "
                             "deterministic macro inputs."
                           : "Use a self-contained C++17 source with "
                             "deterministic macro inputs.",
               P.isValid() ? P.getLine() : 1, P.isValid() ? P.getColumn() : 1,
               S.project() ? S.sourcePath(SM, L) : S.Relative);
  }

public:
  PreprocessorPolicy(State &S, Preprocessor &PP)
      : S(S), SM(PP.getSourceManager()), PP(PP) {}
  void InclusionDirective(SourceLocation L, const Token &, llvm::StringRef,
                          bool, CharSourceRange, OptionalFileEntryRef File,
                          llvm::StringRef, llvm::StringRef, const Module *,
                          bool, SrcMgr::CharacteristicKind) override {
    if (!S.project()) {
      reject(L, "include");
      return;
    }
    if (!File) {
      if (S.owns(SM, L))
        reject(L, "include",
               "Declared header is missing; generate or provide it before "
               "translation.",
               "TR0203");
      else
        S.diagnose("TR0203", "SDK include", "A required SDK header is missing.",
                   "Restore the exact approved SDK header distribution.");
    } else if (S.relativePath(File->getName()).empty() &&
               !(S.math() && S.sdkFile(*File))) {
      if (S.owns(SM, L))
        reject(L, "include",
               "Resolved header is outside the owned project and approved SDK "
               "boundaries.",
               "TR0203");
      else
        S.diagnose(
            "TR0203", "SDK include",
            "SDK include is outside the compiled approved header catalog.",
            "Restore the exact approved SDK header distribution.");
    }
  }
  void PragmaDirective(SourceLocation L, PragmaIntroducerKind) override {
    if (S.project() &&
        OncePragmas.count(S.sourcePath(SM, L) + ":" +
                          std::to_string(SM.getSpellingLineNumber(L))))
      return;
    reject(L, "pragma");
  }
  void LexedFileChanged(FileID FID, LexedFileChangeReason Reason,
                        SrcMgr::CharacteristicKind, FileID,
                        SourceLocation) override {
    if (!S.project() || Reason != LexedFileChangeReason::EnterFile)
      return;
    auto L = SM.getLocForStartOfFile(FID);
    auto Path = S.sourcePath(SM, L);
    if (Path.empty() && S.math() && S.sdkFile(SM, L)) {
      S.consumeSDKFile(SM, FID);
      return;
    }
    if (Path.empty())
      return; // Foreign includes already fail at their include site.
    bool Invalid = false;
    auto Bytes = SM.getBufferData(FID, &Invalid);
    if (Invalid) {
      reject(L, "header", "Cannot read the selected header buffer.", "TR0203");
      return;
    }
    S.Dependencies[Path] = digest(Bytes);
    checkDirectives(PP, FID);
  }
  void MacroExpands(const Token &T, const MacroDefinition &, SourceRange,
                    const MacroArgs *) override {
    if (!T.getIdentifierInfo())
      return;
    auto N = T.getIdentifierInfo()->getName();
    if (N == "__DATE__" || N == "__TIME__" || N == "__TIMESTAMP__" ||
        N == "__FILE__" || N == "__BASE_FILE__" || N == "__FILE_NAME__" ||
        N == "__COUNTER__" || N == "__INCLUDE_LEVEL__" ||
        N == "__has_include" || N == "__has_include_next")
      reject(T.getLocation(), N);
  }
  void checkDirectives(Preprocessor &PP, FileID FID = FileID()) {
    if (FID.isInvalid())
      FID = SM.getMainFileID();
    if (!Checked.insert(FID.getHashValue()).second)
      return;
    Lexer Raw(FID, SM.getBufferOrFake(FID), SM, PP.getLangOpts());
    Token T;
    bool AfterHash = false;
    while (!Raw.LexFromRawLexer(T)) {
      if (T.isAtStartOfLine())
        AfterHash = false;
      if (T.is(tok::hash) && T.isAtStartOfLine()) {
        AfterHash = true;
        continue;
      }
      if (!T.is(tok::raw_identifier)) {
        if (AfterHash && !T.is(tok::comment)) {
          reject(T.getLocation(), "preprocessor directive");
          AfterHash = false;
        }
        continue;
      }
      auto Text = Lexer::getSpelling(T, SM, PP.getLangOpts());
      if (AfterHash && S.project() && Text == "pragma") {
        auto L = T.getLocation();
        Token Kind;
        Raw.LexFromRawLexer(Kind);
        if (!Kind.isAtStartOfLine() && Kind.is(tok::raw_identifier) &&
            Lexer::getSpelling(Kind, SM, PP.getLangOpts()) == "once")
          OncePragmas.insert(S.sourcePath(SM, L) + ":" +
                             std::to_string(SM.getSpellingLineNumber(L)));
        else
          reject(L, "pragma");
        AfterHash = false;
        continue;
      }
      if (Text == "__DATE__" || Text == "__TIME__" || Text == "__TIMESTAMP__" ||
          Text == "__FILE__" || Text == "__BASE_FILE__" ||
          Text == "__FILE_NAME__" || Text == "__COUNTER__" ||
          Text == "__INCLUDE_LEVEL__" || Text == "__has_include" ||
          Text == "__has_include_next")
        reject(T.getLocation(), Text);
      if (AfterHash && Text != "define" && Text != "undef" && Text != "if" &&
          Text != "ifdef" && Text != "ifndef" && Text != "elif" &&
          Text != "else" && Text != "endif" && Text != "error" &&
          !(S.project() && Text == "include"))
        reject(T.getLocation(), Text);
      AfterHash = false;
    }
  }
};

class Consumer : public ASTConsumer {
  State &S;
  std::vector<ExplicitFunctionInstantiationSource> Directives;
  std::vector<ExplicitStaticDataInstantiationSource> StaticDirectives;
  std::vector<TemplateUseSource> TemplateUses;
  std::vector<FunctionSpecializationSource> Specializations;
  std::size_t DirectiveUnits = 0;

  bool reserveSourceUnits(SourceManager &Sources, SourceLocation Location,
                          std::size_t ArgumentCount) {
    constexpr std::size_t Limit = 200000;
    if (ArgumentCount >= Limit || 1 + ArgumentCount > Limit - DirectiveUnits) {
      auto P = Sources.getPresumedLoc(Location);
      S.diagnose("TR0201", "template source evidence",
                 "Template source evidence exceeds the frontend budget.",
                 "Reduce source directives, defaults and template arguments.",
                 P.isValid() ? P.getLine() : 1, P.isValid() ? P.getColumn() : 1);
      return false;
    }
    DirectiveUnits += 1 + ArgumentCount;
    return true;
  }

  void retainTemplateUse(
      TemplateSourceKind Kind, NamedDecl *Template, const Decl *Declaration,
      const Type *Type, TypeSourceInfo *Underlying,
      const TemplateArgumentListInfo *Written,
      const TemplateArgument *Canonical, const TemplateArgument *Sugared,
      unsigned Count, NamedDecl *const *DefaultParameters,
      const TemplateArgumentLoc *OriginalDefaults,
      const TemplateArgumentLoc *ConvertedDefaults, unsigned DefaultCount,
      NamedDecl *const *TypeParameters, TypeSourceInfo *const *ParameterTypes,
      const unsigned *PackIndices, unsigned TypeCount,
      bool Overflow, const SourceLocation &Location, bool Instantiation = false,
      const TemplateArgumentList *Selection = nullptr) {
    if (!S.coreV2() || !Template || !S.Diagnostics.empty())
      return;
    auto &Context = Template->getASTContext();
    auto &Sources = Context.getSourceManager();
    if (!S.owns(Sources, Location))
      return;
    // These are successful Clang callbacks, not input protocol records. Bound
    // collection globally without eagerly rejecting an unused generic body.
    constexpr std::size_t Limit = 200000;
    if (Count >= Limit || DefaultCount > 64 || TypeCount > 4096 ||
        (Written && Written->size() >= Limit)) {
      reserveSourceUnits(Sources, Location, Limit);
      return;
    }
    if ((Count && (!Canonical || !Sugared)) ||
        (DefaultCount && (!DefaultParameters || !OriginalDefaults || !ConvertedDefaults)) ||
        (TypeCount && (!TypeParameters || !ParameterTypes || !PackIndices))) {
      S.diagnose("TR0201", "template source evidence",
                 "Successful template source metadata is incomplete.",
                 "Use a frontend build with matching source-preservation hooks.");
      return;
    }
    std::size_t Units = 2 * std::size_t(Count) + 3 * std::size_t(DefaultCount) +
                        3 * std::size_t(TypeCount) +
                        (Written ? Written->size() : 0);
    for (unsigned I = 0; I < Count; ++I) {
      if (Canonical[I].isNull() || Canonical[I].isInstantiationDependent())
        return; // Unresolved primary metadata has no materialized source use.
      for (const auto *Argument : {&Canonical[I], &Sugared[I]})
        if (Argument->getKind() == TemplateArgument::Pack) {
          if (Argument->pack_size() >= Limit || Units >= Limit ||
              Argument->pack_size() >= Limit - Units) {
            reserveSourceUnits(Sources, Location, Limit);
            return;
          }
          Units += Argument->pack_size();
        }
    }
    if (!reserveSourceUnits(Sources, Location, Units))
      return;
    TemplateUseSource Source{
        Kind, Template, Declaration, Type, Underlying,
        Written ? ASTTemplateArgumentListInfo::Create(Context, *Written) : nullptr,
        TemplateArgumentList::CreateCopy(Context, llvm::ArrayRef<TemplateArgument>(Canonical, Count)),
        TemplateArgumentList::CreateCopy(Context, llvm::ArrayRef<TemplateArgument>(Sugared, Count)),
        {}, {}, Location, Overflow, Instantiation, Selection};
    Source.Defaults.reserve(DefaultCount);
    for (unsigned I = 0; I < DefaultCount; ++I)
      Source.Defaults.push_back({DefaultParameters[I], OriginalDefaults[I], ConvertedDefaults[I]});
    Source.ParameterTypes.reserve(TypeCount);
    for (unsigned I = 0; I < TypeCount; ++I)
      Source.ParameterTypes.push_back({TypeParameters[I], ParameterTypes[I], PackIndices[I]});
    TemplateUses.push_back(std::move(Source));
  }

public:
  explicit Consumer(State &S) : S(S) {}
  bool wantsNeverCTemplateSource() const override { return S.coreV2(); }
  void HandleNeverCTemplateTypeSource(
      TemplateDecl *Template, const Type *Type, TypeSourceInfo *Underlying,
      const TemplateArgumentListInfo &Written,
      const TemplateArgument *Canonical, const TemplateArgument *Sugared, unsigned Count,
      NamedDecl *const *DefaultParameters, const TemplateArgumentLoc *OriginalDefaults,
      const TemplateArgumentLoc *ConvertedDefaults, unsigned DefaultCount,
      NamedDecl *const *TypeParameters, TypeSourceInfo *const *ParameterTypes,
      const unsigned *PackIndices, unsigned TypeCount,
      bool Overflow, const SourceLocation &Location) override {
    retainTemplateUse(TemplateSourceKind::Type, Template, nullptr, Type, Underlying,
                      &Written, Canonical, Sugared, Count, DefaultParameters,
                      OriginalDefaults, ConvertedDefaults, DefaultCount,
                      TypeParameters, ParameterTypes, PackIndices, TypeCount, Overflow, Location);
  }
  void HandleNeverCFunctionTemplateSource(
      FunctionDecl *Function,
      const TemplateArgument *Canonical, const TemplateArgument *Sugared, unsigned Count,
      NamedDecl *const *DefaultParameters, const TemplateArgumentLoc *OriginalDefaults,
      const TemplateArgumentLoc *ConvertedDefaults, unsigned DefaultCount,
      NamedDecl *const *TypeParameters, TypeSourceInfo *const *ParameterTypes,
      const unsigned *PackIndices, unsigned TypeCount,
      bool Overflow, const SourceLocation &Location) override {
    retainTemplateUse(TemplateSourceKind::Function,
                      Function ? Function->getPrimaryTemplate() : nullptr,
                      Function, nullptr, nullptr, nullptr,
                      Canonical, Sugared, Count, DefaultParameters,
                      OriginalDefaults, ConvertedDefaults, DefaultCount,
                      TypeParameters, ParameterTypes, PackIndices, TypeCount, Overflow, Location);
  }
  void HandleNeverCClassTemplateSource(
      ClassTemplateSpecializationDecl *Declaration,
      const TemplateArgumentListInfo &Written, bool Instantiation,
      const TemplateArgument *Canonical, const TemplateArgument *Sugared, unsigned Count,
      NamedDecl *const *DefaultParameters, const TemplateArgumentLoc *OriginalDefaults,
      const TemplateArgumentLoc *ConvertedDefaults, unsigned DefaultCount,
      NamedDecl *const *TypeParameters, TypeSourceInfo *const *ParameterTypes,
      const unsigned *PackIndices, unsigned TypeCount,
      bool Overflow, const SourceLocation &Location) override {
    retainTemplateUse(TemplateSourceKind::ClassDeclaration,
                      Declaration ? Declaration->getSpecializedTemplate() : nullptr,
                      Declaration, nullptr, nullptr, &Written,
                      Canonical, Sugared, Count, DefaultParameters,
                      OriginalDefaults, ConvertedDefaults, DefaultCount,
                      TypeParameters, ParameterTypes, PackIndices, TypeCount, Overflow,
                      Location, Instantiation);
  }
  void HandleNeverCClassPartialSource(
      ClassTemplatePartialSpecializationDecl *Partial,
      const TemplateArgumentList *Selection, bool Pattern,
      const TemplateArgumentListInfo *Written,
      const TemplateArgument *Canonical, const TemplateArgument *Sugared, unsigned Count,
      NamedDecl *const *DefaultParameters, const TemplateArgumentLoc *OriginalDefaults,
      const TemplateArgumentLoc *ConvertedDefaults, unsigned DefaultCount,
      NamedDecl *const *TypeParameters, TypeSourceInfo *const *ParameterTypes,
      const unsigned *PackIndices, unsigned TypeCount,
      bool Overflow, const SourceLocation &Location) override {
    NamedDecl *Owner = Partial;
    if (Pattern && Partial)
      Owner = Partial->getSpecializedTemplate();
    retainTemplateUse(Pattern ? TemplateSourceKind::PartialPattern
                              : TemplateSourceKind::PartialDeduction,
                      Owner, Partial, nullptr, nullptr, Written,
                      Canonical, Sugared, Count, DefaultParameters,
                      OriginalDefaults, ConvertedDefaults, DefaultCount,
                      TypeParameters, ParameterTypes, PackIndices, TypeCount,
                      Overflow, Location, false, Selection);
  }
  void HandleNeverCFunctionSpecializationSource(
      FunctionDecl *Declaration, FunctionDecl *Selected,
      const TemplateArgumentListInfo *Written,
      const SourceLocation &Location) override {
    if (!S.coreV2() || !Declaration || !Selected || !S.Diagnostics.empty())
      return;
    auto &Context = Declaration->getASTContext();
    auto &Sources = Context.getSourceManager();
    if (!S.owns(Sources, Location) ||
        !reserveSourceUnits(Sources, Location, Written ? Written->size() : 0))
      return;
    Specializations.push_back({Declaration, Selected,
        Written ? ASTTemplateArgumentListInfo::Create(Context, *Written) : nullptr, Location});
  }
  void HandleNeverCExplicitFunctionInstantiation(
      FunctionDecl *Function, const TemplateArgumentListInfo &Arguments,
      TypeSourceInfo *Type, const DeclarationNameInfo &Name,
      const NestedNameSpecifierLoc &Qualifier, const SourceLocation &Location,
      bool HasAttributes) override {
    if (!S.coreV2() || !Function || !S.Diagnostics.empty())
      return;
    auto &Context = Function->getASTContext();
    auto &Sources = Context.getSourceManager();
    if (!S.owns(Sources, Location))
      return;
    // Directives and converted defaults share one source-evidence budget.
    if (!reserveSourceUnits(Sources, Location, Arguments.size()))
      return;
    Directives.push_back({Function,
        ASTTemplateArgumentListInfo::Create(Context, Arguments), Type, Name,
        Qualifier, Location, HasAttributes});
  }
  void HandleNeverCExplicitStaticDataInstantiation(
      VarDecl *Variable, TypeSourceInfo *Type,
      const NestedNameSpecifierLoc &Qualifier, const SourceLocation &Location,
      bool HasAttributes) override {
    if (!S.coreV2() || !Variable || !S.Diagnostics.empty())
      return;
    auto &Sources = Variable->getASTContext().getSourceManager();
    if (!S.owns(Sources, Location) || !reserveSourceUnits(Sources, Location, 0))
      return;
    StaticDirectives.push_back({Variable, Type, Qualifier, Location, HasAttributes});
  }
  void HandleTranslationUnit(ASTContext &C) override {
    if (!S.Diagnostics.empty() || C.getDiagnostics().hasErrorOccurred())
      return;
    Adapter A(S, C);
    try {
      A.run(Directives, StaticDirectives, TemplateUses, Specializations);
    } catch (const Failure &) {
    }
  }
};

class Action : public ASTFrontendAction {
  State &S;

public:
  explicit Action(State &S) : S(S) {}
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 llvm::StringRef) override {
    auto Policy = std::make_unique<PreprocessorPolicy>(S, CI.getPreprocessor());
    Policy->checkDirectives(CI.getPreprocessor());
    if (!S.Diagnostics.empty())
      return nullptr;
    if (S.project()) {
      auto &SM = CI.getSourceManager();
      auto &Lang = CI.getLangOpts();
      CI.getPreprocessor().setTokenWatcher([this, &SM, &Lang](const Token &T) {
        if (T.is(tok::eof) || T.is(tok::comment) || T.isAnnotation())
          return;
        auto L = SM.getExpansionLoc(T.getLocation());
        if (!S.owns(SM, L))
          return;
        auto Spelling = Lexer::getSpelling(T, SM, Lang);
        auto Bytes = std::to_string(static_cast<unsigned>(T.getKind())) + ":" +
                     std::to_string(Spelling.size()) + ":" + Spelling + ";";
        S.ExpandedTokens[SM.getFileID(L).getHashValue()].push_back(
            {SM.getFileOffset(L), std::move(Bytes)});
      });
    }
    CI.getPreprocessor().addPPCallbacks(std::move(Policy));
    return std::make_unique<Consumer>(S);
  }
};

class Factory : public tooling::FrontendActionFactory {
  State &S;

public:
  explicit Factory(State &S) : S(S) {}
  std::unique_ptr<FrontendAction> create() override {
    return std::make_unique<Action>(S);
  }
};

static bool options(State &S, const json::Array &Args) {
  for (size_t I = 0; I < Args.size(); ++I) {
    auto Text = Args[I].getAsString();
    if (!Text)
      return false;
    auto Arg = *Text;
    if (Arg.contains('\n') || Arg.contains('\r') || Arg.contains('\0'))
      return false;
    if (Arg == "-std=c++17" ||
        (S.project() && (Arg == "-O0" || Arg == "-O2"))) {
      S.Arguments.push_back(Arg.str());
      continue;
    }
    if (S.project() &&
        (Arg == "-Wall" || Arg == "-Wextra" || Arg == "-Wpedantic" ||
         Arg == "-Werror" || Arg == "-Wno-error" ||
         Arg == "-Wno-unused-parameter")) {
      S.Arguments.push_back(Arg.str());
      continue;
    }
    if (S.project()) {
      llvm::StringRef Include;
      for (auto Prefix : {llvm::StringRef("-iquote"),
                          llvm::StringRef("-isystem"), llvm::StringRef("-I")})
        if (Arg.starts_with(Prefix)) {
          Include = Prefix;
          break;
        }
      if (!Include.empty()) {
        auto Path = Arg.drop_front(Include.size());
        if (Path.empty()) {
          if (++I == Args.size())
            return false;
          auto Next = Args[I].getAsString();
          if (!Next)
            return false;
          Path = *Next;
        }
        if (Path.empty() || Path.contains('\0') || Path.contains('\r') ||
            Path.contains('\n'))
          return false;
        llvm::SmallString<256> Absolute(Path), Real;
        if (!llvm::sys::path::is_absolute(Absolute)) {
          Absolute = S.WorkingDirectory;
          llvm::sys::path::append(Absolute, Path);
        }
        const auto Error = llvm::sys::fs::real_path(Absolute, Real);
        const auto Canonical = llvm::sys::path::convert_to_slash(Real);
        if (Error || !llvm::sys::fs::is_directory(Canonical) ||
            (Canonical != S.Root && S.relativePath(Canonical).empty())) {
          S.diagnose("TR0203", "include directory",
                     "Include directories must exist inside the declared "
                     "project root.",
                     "Generate owned headers and declare their owned include "
                     "directories before translation.");
          return false;
        }
        S.Arguments.push_back(Include.str());
        S.Arguments.push_back(Canonical);
        continue;
      }
    }
    if (!Arg.starts_with("-D") && !Arg.starts_with("-U"))
      return false;
    auto Name = Arg.drop_front(2).split('=').first;
    if (Name.empty() || llvm::isDigit(Name.front()) || Name.starts_with("__") ||
        (Name.size() > 1 && Name[0] == '_' && llvm::isUpper(Name[1])))
      return false;
    for (char C : Name)
      if (!llvm::isAlnum(C) && C != '_')
        return false;
    if (Arg.starts_with("-U") && Arg.contains('='))
      return false;
    S.Arguments.push_back(Arg.str());
  }
  return true;
}

static std::optional<std::string> readRegularFile(llvm::StringRef Path,
                                                 uint64_t Limit) {
  if (Path.empty() || Path.contains('\0'))
    return std::nullopt;
#ifdef _WIN32
  auto Opened = llvm::sys::fs::openNativeFileForRead(Path);
  if (!Opened) {
    llvm::consumeError(Opened.takeError());
    return std::nullopt;
  }
  llvm::sys::fs::file_t FD = *Opened;
#else
  // A path-only stat followed by blocking open can race a FIFO replacement.
  // Inspect the exact nonblocking descriptor and never mmap mutable input.
  llvm::sys::fs::file_t FD =
      ::open(Path.str().c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  if (FD == -1)
    return std::nullopt;
#endif
  auto Close = llvm::make_scope_exit([&]() { (void)llvm::sys::fs::closeFile(FD); });
  llvm::sys::fs::file_status Status;
  if (llvm::sys::fs::status(FD, Status) ||
      !llvm::sys::fs::is_regular_file(Status) || Status.getSize() > Limit)
    return std::nullopt;
  std::string Contents;
  char Buffer[64 * 1024];
  for (;;) {
    const uint64_t Remaining = Limit - Contents.size();
    const size_t Count = Remaining >= sizeof(Buffer)
                             ? sizeof(Buffer)
                             : static_cast<size_t>(Remaining) + 1;
    auto Read = llvm::sys::fs::readNativeFile(
        FD, llvm::MutableArrayRef<char>(Buffer, Count));
    if (!Read) {
      llvm::consumeError(Read.takeError());
      return std::nullopt;
    }
    if (*Read > Remaining)
      return std::nullopt;
    if (!*Read)
      return Contents;
    Contents.append(Buffer, *Read);
  }
}

static bool boundedRequestJSON(llvm::StringRef Text) {
  unsigned Depth = 0;
  bool String = false, Escape = false;
  for (char C : Text) {
    if (String) {
      if (Escape)
        Escape = false;
      else if (C == '\\')
        Escape = true;
      else if (C == '"')
        String = false;
    } else if (C == '"') {
      String = true;
    } else if (C == '[' || C == '{') {
      if (++Depth > 32)
        return false;
    } else if (C == ']' || C == '}') {
      if (!Depth)
        return false;
      --Depth;
    }
  }
  return !String && Depth == 0;
}

static bool request(State &S, llvm::StringRef Path) {
  auto Buffer = readRegularFile(Path, 1024 * 1024);
  if (!Buffer || !boundedRequestJSON(*Buffer))
    return false;
  auto Data = json::parse(*Buffer);
  if (!Data) {
    llvm::consumeError(Data.takeError());
    return false;
  }
  const auto *O = Data->getAsObject();
  if (!O || O->getInteger("protocol") != 1 ||
      (O->getString("profile") != "cpp-core-v1" &&
       O->getString("profile") != "cpp-core-v2" &&
       O->getString("profile") != "cpp-project-v1" &&
       O->getString("profile") != "cpp-math-v1"))
    return false;
  S.Profile = O->getString("profile")->str();
  S.Module["profile"] = S.Profile;
  // Required keys are checked below. An exact count also rejects unknown keys
  // and project/SDK context accidentally attached to a different profile.
  const size_t ExpectedFields = S.math() ? 10 : (S.project() ? 9 : 6);
  if (O->size() != ExpectedFields)
    return false;
  auto Root = O->getString("root"), Source = O->getString("source"),
       Target = O->getString("target");
  const auto *Args = O->getArray("arguments");
  if (!Root || !Source || !Target || !Args ||
      !llvm::sys::path::is_absolute(*Root) ||
      !llvm::sys::path::is_absolute(*Source) || Root->contains('\0') ||
      Source->contains('\0'))
    return false;
  S.Root = Root->str();
  S.Source = Source->str();
  S.Target = Target->str();
  llvm::SmallString<256> RealRoot, RealSource;
  if (llvm::sys::fs::real_path(S.Root, RealRoot) ||
      llvm::sys::fs::real_path(S.Source, RealSource))
    return false;
  // real_path uses native separators on Windows; all stored paths and wire
  // locations use slashes. On POSIX, literal backslashes remain unchanged.
  S.Root = llvm::sys::path::convert_to_slash(RealRoot);
  S.Source = llvm::sys::path::convert_to_slash(RealSource);
  auto Prefix = llvm::StringRef(S.Root).ends_with("/") ? S.Root : S.Root + "/";
  if (!llvm::StringRef(S.Source).starts_with(Prefix))
    return false;
  S.Relative = llvm::StringRef(S.Source).drop_front(Prefix.size()).str();
  S.WorkingDirectory = S.Root;
  if (S.project()) {
    auto TU = O->getString("translation_unit"),
         Configuration = O->getString("configuration_id"),
         Working = O->getString("working_directory");
    if (S.relativePath(S.Source).empty() || !TU || *TU != S.Relative ||
        !Configuration || Configuration->size() != 64 || !Working ||
        Working->contains('\0') || !llvm::sys::path::is_absolute(*Working))
      return false;
    for (char C : *Configuration)
      if (!(C >= '0' && C <= '9') && !(C >= 'a' && C <= 'f'))
        return false;
    llvm::SmallString<256> RealWorking;
    const auto Error = llvm::sys::fs::real_path(*Working, RealWorking);
    const auto Canonical = llvm::sys::path::convert_to_slash(RealWorking);
    if (Error || !llvm::sys::fs::is_directory(Canonical) ||
        (Canonical != S.Root && S.relativePath(Canonical).empty()))
      return false;
    S.WorkingDirectory = Canonical;
    S.ConfigurationID = Configuration->str();
  }
  llvm::Triple T(S.Target);
  if (S.math()) {
    const auto *SDK = O->getObject("sdk");
    llvm::VersionTuple Deployment;
    if (T.getVendor() != llvm::Triple::Apple || !T.isMacOSX() ||
        !T.getEnvironmentName().empty() || !T.getMacOSXVersion(Deployment) ||
        Deployment.getMajor() != 15 || Deployment.getMinor().value_or(0) != 0 ||
        Deployment.getSubminor().value_or(0) != 0) {
      S.diagnose(
          "TR0204", "math target",
          "The approved math SDK currently supports Apple macOS 15.0 targets "
          "only.",
          "Select arm64-apple-macosx15.0.0 or x86_64-apple-macosx15.0.0.");
      return false;
    }
    if (!SDK || !S.configureSDK(*SDK))
      return false;
  }
  if ((T.getArch() != llvm::Triple::aarch64 &&
       T.getArch() != llvm::Triple::x86_64) ||
      (!T.isMacOSX() && !(T.isOSLinux() && !T.isAndroid()) &&
       !T.isOSWindows())) {
    S.diagnose(
        "TR0204", "target",
        "Target architecture/OS is outside the core data-model contract.",
        "Use the current NeverC native target.");
    return false;
  }
  if (!options(S, *Args)) {
    if (S.Diagnostics.empty())
      S.diagnose("TR0004", "source option",
                 S.project() ? "Only C++17, -O0/-O2, -D/-U and owned "
                               "-I/-iquote/-isystem directories are supported."
                             : "Only -std=c++17 and attached "
                               "-DNAME[=VALUE]/-UNAME options are supported.",
                 "Remove options outside the documented profile allowlist.");
    return false;
  }
  auto SourceBuffer = readRegularFile(S.Source, 8 * 1024 * 1024);
  if (!SourceBuffer)
    return false;
  S.SourceText = std::move(*SourceBuffer);
  return true;
}

} // namespace nct

extern "C" int neverc_cpp_frontend_main(int Argc, const char **Argv) {
  using namespace nct;
  std::string Request, Output;
  if (Argc == 2 && std::string(Argv[1]) == "--version") {
    llvm::outs() << "neverc-cpp-frontend 20.1.8 protocol 1 " NEVERC_CPP_BUILD_ID
                    "\n";
    return 0;
  }
  for (int I = 1; I < Argc; ++I) {
    std::string Arg = Argv[I];
    if (I + 1 >= Argc || (Arg != "--request" && Arg != "--output")) {
      llvm::errs() << "usage: neverc __neverc_cpp_frontend --request request.json "
                      "--output response.json\n";
      return 1;
    }
    auto &Destination = Arg == "--request" ? Request : Output;
    if (!Destination.empty())
      return 1;
    Destination = Argv[++I];
  }
  if (Request.empty() || Output.empty() || Output == "-")
    return 1;
  State S;
  S.Module =
      json::Object{{"protocol", 1},
                   {"profile", "cpp-core-v1"},
                   {"frontend", json::Object{{"name", "neverc-cpp-frontend"},
                                             {"version", "20.1.8"},
                                             {"build", NEVERC_CPP_BUILD_ID}}}};
  bool Valid = request(S, Request);
  if (Valid && S.project() && !isolateProjectEnvironment()) {
    S.diagnose(
        "TR0101", "frontend environment",
        "Cannot isolate implicit compiler configuration from the environment.",
        "Run the internal frontend in an environment with removable compiler "
        "configuration variables.");
    Valid = false;
  }
  if (!Valid && S.Diagnostics.empty())
    S.diagnose("TR0103", "frontend request",
               "Invalid protocol request, paths or source size.",
               "Use a compatible NeverC translate driver and readable source "
               "under the declared root.");
  if (Valid) {
    std::vector<std::string> Args{
        "--no-default-config", "-x",        "c++",
        "-std=c++17",          "-nostdinc", "-nostdinc++",
        "-fsyntax-only",       "-target",   S.Target};
    Args.insert(Args.end(), S.Arguments.begin(), S.Arguments.end());
    if (S.coreV2()) {
      // Keep standard template parsing and diagnostics independent of the
      // target's default Microsoft compatibility options.
      Args.insert(Args.end(), {"-Wc++20-extensions", "-Wc++23-extensions",
                               "-Wc++26-extensions", "-fno-ms-compatibility",
                               "-fno-ms-extensions",
                               "-fno-delayed-template-parsing"});
    }
    if (S.math()) {
      Args.insert(Args.end(),
                  {"-fno-fast-math", "-ffp-contract=off", "-resource-dir",
                   S.SDKRoots.at("resource"), "-isystem",
                   S.SDKRoots.at("libcxx"), "-isystem",
                   S.SDKRoots.at("resource") + "/include", "-isystem",
                   S.SDKRoots.at("platform") + "/usr/include", "-isysroot",
                   S.SDKRoots.at("platform")});
    }
    if (auto FileSystem = S.createFileSystem()) {
      tooling::FixedCompilationDatabase Database(S.WorkingDirectory, Args);
      tooling::ClangTool Tool(Database, {S.Source},
                              std::make_shared<PCHContainerOperations>(),
                              FileSystem);
      Tool.mapVirtualFile(S.Source, S.SourceText);
      Diagnostics Diags(S);
      Tool.setDiagnosticConsumer(&Diags);
      Factory F(S);
      if (Tool.run(&F) && S.Diagnostics.empty())
        S.diagnose("TR0102", "Clang frontend",
                   "Source analysis failed without a structured diagnostic.",
                   "Inspect the frontend installation and input.");
    }
  }
  bool Failed = !S.Diagnostics.empty();
  S.Module["diagnostics"] = std::move(S.Diagnostics);
  std::error_code Error;
  llvm::raw_fd_ostream OS(Output, Error, llvm::sys::fs::CD_CreateNew,
                          llvm::sys::fs::FA_Write, llvm::sys::fs::OF_Text);
  if (Error) {
    llvm::errs() << Error.message() << "\n";
    return 1;
  }
  OS << llvm::formatv("{0:2}\n", json::Value(std::move(S.Module)));
  OS.flush();
  if (OS.has_error())
    return 1;
  return Failed ? 1 : 0;
}
