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
#include "clang/Basic/DiagnosticAST.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/PartialDiagnostic.h"
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
static bool allocationOperatorKind(OverloadedOperatorKind Kind) {
  return Kind == OO_New || Kind == OO_Array_New ||
         Kind == OO_Delete || Kind == OO_Array_Delete;
}

static bool ordinaryOperatorKind(OverloadedOperatorKind Kind) {
  switch (Kind) {
  case OO_New: case OO_Array_New: case OO_Delete: case OO_Array_Delete:
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

// Shape only. Import classification separately requires source ownership,
// checked attributes and the absence of a definition across all redeclarations.
static unsigned nativeHeapDeclaration(const FunctionDecl *F) {
  if (!F || F->getKind() != Decl::Function || !F->getIdentifier() ||
      F->isImplicit() || F->isInvalidDecl() || F->isInlined() || F->isConstexpr() ||
      F->isDeleted() || F->getTemplatedKind() != FunctionDecl::TK_NonTemplate ||
      !F->isExternC() || F->getFormalLinkage() != Linkage::External ||
      !F->getDeclContext()->getRedeclContext()->isTranslationUnit())
    return 0;
  const auto *Prototype = F->getType()->getAs<FunctionProtoType>();
  if (!Prototype || Prototype->isVariadic() || Prototype->getCallConv() != CC_C ||
      !standardExceptionSpecification(Prototype))
    return 0;
  for (const auto *P : F->parameters())
    if (P->hasDefaultArg() || P->isParameterPack())
      return 0;
  auto &Context = F->getASTContext();
  auto Same = [&](QualType A, QualType B) { return Context.hasSameUnqualifiedType(A, B); };
  const auto Name = F->getName();
  if (Name == "free")
    return F->getNumParams() == 1 && Same(F->getReturnType(), Context.VoidTy) &&
                   Same(F->getParamDecl(0)->getType(), Context.VoidPtrTy)
               ? Builtin::BIfree : 0;
  const unsigned Count = Name == "malloc" ? 1 : Name == "calloc" ? 2 : 0;
  if (!Count || F->getNumParams() != Count || !Same(F->getReturnType(), Context.VoidPtrTy))
    return 0;
  for (const auto *P : F->parameters())
    if (!Same(P->getType(), Context.getSizeType()))
      return 0;
  return Count == 1 ? Builtin::BImalloc : Builtin::BIcalloc;
}

// Structural source checks stay attribute-free except for the standard final
// class keyword. Non-record declarations retain their original strict policy.
static bool hasNonFinalAttributes(const Decl *D) {
  if (!D->hasAttrs())
    return false;
  if (!isa<CXXRecordDecl>(D))
    return true;
  for (const auto *Attribute : D->attrs())
    if (!isa<FinalAttr>(Attribute) ||
        llvm::StringRef(Attribute->getSpelling()) != "final")
      return true;
  return false;
}

static bool supportedDeclarationAttributes(const Decl *D) {
  if (!hasNonFinalAttributes(D))
    return true;
  const auto *F = dyn_cast<FunctionDecl>(D);
  if (const auto ID = nativeHeapDeclaration(F)) {
    // Bare source declarations acquire these exact pinned Clang facts too.
    // Written attributes and unrelated implicit builtin properties stay denied.
    for (const auto *Attribute : F->attrs()) {
      if (!Attribute->isImplicit()) return false;
      if (const auto *Builtin = dyn_cast<BuiltinAttr>(Attribute);
          Builtin && Builtin->getID() == ID)
        continue;
      if (const auto *Size = dyn_cast<AllocSizeAttr>(Attribute);
          Size && ID != Builtin::BIfree && Size->getElemSizeParam().isValid() &&
          Size->getElemSizeParam().getSourceIndex() == 1 &&
          (ID == Builtin::BIcalloc
               ? Size->getNumElemsParam().isValid() && Size->getNumElemsParam().getSourceIndex() == 2
               : !Size->getNumElemsParam().isValid()))
        continue;
      return false;
    }
    return true;
  }
  if (!F || !allocationOperatorKind(F->getOverloadedOperator()))
    return false;
  const bool Allocate = F->getOverloadedOperator() == OO_New ||
                        F->getOverloadedOperator() == OO_Array_New;
  std::optional<unsigned> AlignmentParameter;
  bool Nothrow = false;
  if (!F->isReplaceableGlobalAllocationFunction(&AlignmentParameter, &Nothrow))
    return false;
  // These exact implicit facts are synthesized by pinned Clang's
  // AddKnownFunctionAttributesForReplaceableGlobalAllocationFunction. Written
  // attributes never gain this exception and are not copied to emitted calls.
  for (const auto *Attribute : F->attrs()) {
    // Sema's visibility merge reconstructs this inherited attribute without
    // its implicit bit. Prove its origin in the exact prior implicit global
    // allocation declaration; an absent source range alone is insufficient.
    if (const auto *Visibility = dyn_cast<VisibilityAttr>(Attribute);
        Visibility && Visibility->isInherited() &&
        Visibility->getRange().getBegin().isInvalid() &&
        Visibility->getRange().getEnd().isInvalid() &&
        Visibility->getVisibility() == VisibilityAttr::Default) {
      bool Generated = false;
      for (const auto *Prior = F->getPreviousDecl(); Prior;
           Prior = Prior->getPreviousDecl()) {
        if (!Prior->isImplicit() || Prior->getLocation().isValid() ||
            !Prior->isReplaceableGlobalAllocationFunction())
          continue;
        for (const auto *Original : Prior->specific_attrs<VisibilityAttr>())
          Generated |= Original->isImplicit() && Original->getRange().getBegin().isInvalid() &&
                       Original->getRange().getEnd().isInvalid() &&
                       Original->getVisibility() == VisibilityAttr::Default;
      }
      if (Generated)
        continue;
    }
    if (!Allocate)
      return false;
    if (!Attribute->isImplicit())
      return false;
    if (isa<ReturnsNonNullAttr>(Attribute) && !Nothrow)
      continue;
    if (const auto *Size = dyn_cast<AllocSizeAttr>(Attribute);
        Size && Size->getElemSizeParam().isValid() &&
        Size->getElemSizeParam().getSourceIndex() == 1 &&
        !Size->getNumElemsParam().isValid())
      continue;
    if (const auto *Align = dyn_cast<AllocAlignAttr>(Attribute);
        Align && AlignmentParameter && Align->getParamIndex().isValid() &&
        Align->getParamIndex().getSourceIndex() == *AlignmentParameter)
      continue;
    return false;
  }
  return true;
}

// Name shape only; primary ownership and concrete types are checked separately.
static bool ordinaryFreeFunctionName(const FunctionDecl *F) {
  return F && F->getKind() == Decl::Function &&
         (F->getIdentifier() || ordinaryOperatorKind(F->getOverloadedOperator()));
}

static bool binaryFloatingType(QualType T) {
  return T->isSpecificBuiltinType(BuiltinType::Float) ||
         T->isSpecificBuiltinType(BuiltinType::Double);
}

// Shape only: Allowlist separately validates the primary, arguments and body.
static bool concreteFreeFunctionTemplate(const FunctionDecl *F) {
  return ordinaryFreeFunctionName(F) &&
         F->getTemplatedKind() == FunctionDecl::TK_FunctionTemplateSpecialization &&
         F->getPrimaryTemplate() && !F->isDependentContext() &&
         !F->getType().isNull() && !F->getType()->isDependentType();
}

static bool ordinaryMemberTemplateName(const FunctionDecl *F) {
  const auto *M = dyn_cast_or_null<CXXMethodDecl>(F);
  return M && (isa<CXXConstructorDecl, CXXConversionDecl>(M) ||
               (M->getKind() == Decl::CXXMethod &&
                (M->getIdentifier() || ordinaryOperatorKind(M->getOverloadedOperator()))));
}

// A function-template specialization has its own inner argument list. It is
// distinct from a non-template method instantiated with its enclosing class.
static bool concreteMemberFunctionTemplate(const FunctionDecl *F) {
  const auto *M = dyn_cast_or_null<CXXMethodDecl>(F);
  return ordinaryMemberTemplateName(M) &&
         M->getTemplatedKind() == FunctionDecl::TK_FunctionTemplateSpecialization &&
         M->getPrimaryTemplate() && !M->isDependentContext() &&
         !M->getParent()->isDependentContext() &&
         !M->getType().isNull() && !M->getType()->isDependentType();
}

static bool concreteFunctionTemplate(const FunctionDecl *F) {
  return concreteFreeFunctionTemplate(F) || concreteMemberFunctionTemplate(F);
}

// TemplateDecl excludes class partial specializations. Keep the parameter
// owner distinct from the primary that supplies a concrete record's identity.
static const TemplateParameterList *templateSourceParameters(const NamedDecl *Owner) {
  if (const auto *Partial = dyn_cast_or_null<VarTemplatePartialSpecializationDecl>(Owner))
    return Partial->getTemplateParameters();
  if (const auto *Partial = dyn_cast_or_null<ClassTemplatePartialSpecializationDecl>(Owner))
    return Partial->getTemplateParameters();
  if (const auto *Template = dyn_cast_or_null<TemplateDecl>(Owner))
    return Template->getTemplateParameters();
  return nullptr;
}

static const NamedDecl *variableTemplatePattern(const VarDecl *Variable) {
  if (const auto *Partial = dyn_cast_or_null<VarTemplatePartialSpecializationDecl>(Variable))
    return Partial;
  if (const auto *Instance = dyn_cast_or_null<VarTemplateSpecializationDecl>(Variable)) {
    if (Instance->getKind() != Decl::VarTemplateSpecialization ||
        Instance->getDeclContext()->isDependentContext() || Instance->isExplicitSpecialization())
      return nullptr;
    auto Selected = Instance->getSpecializedTemplateOrPartial();
    if (const auto *Partial = Selected.dyn_cast<VarTemplatePartialSpecializationDecl *>())
      return Partial;
    return Selected.dyn_cast<VarTemplateDecl *>();
  }
  return Variable && Variable->getKind() == Decl::Var
             ? Variable->getDescribedVarTemplate() : nullptr;
}

static const VarDecl *variablePatternDecl(const NamedDecl *Owner) {
  if (const auto *Partial = dyn_cast_or_null<VarTemplatePartialSpecializationDecl>(Owner))
    return Partial;
  if (const auto *Primary = dyn_cast_or_null<VarTemplateDecl>(Owner))
    return Primary->getTemplatedDecl();
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

// Parameters belong to the actual primary/partial, while instantiated bodies
// can come from an earlier member pattern. Never use this helper for slots.
static const NamedDecl *classDefinitionPattern(const NamedDecl *Owner) {
  std::set<const NamedDecl *> Seen;
  while (Owner) {
    Owner = cast<NamedDecl>(Owner->getCanonicalDecl());
    if (Seen.size() >= 64 || !Seen.insert(Owner).second)
      return nullptr;
    if (const auto *Partial = dyn_cast<ClassTemplatePartialSpecializationDecl>(Owner)) {
      if (Partial->isMemberSpecialization() || !Partial->getInstantiatedFromMember())
        return Partial;
      Owner = Partial->getInstantiatedFromMember();
    } else if (const auto *Primary = dyn_cast<ClassTemplateDecl>(Owner)) {
      if (Primary->isMemberSpecialization() || !Primary->getInstantiatedFromMemberTemplate())
        return Primary;
      Owner = Primary->getInstantiatedFromMemberTemplate();
    } else {
      return nullptr;
    }
  }
  return nullptr;
}

static const CXXRecordDecl *classDefinitionRecord(const NamedDecl *Owner) {
  return classPatternRecord(classDefinitionPattern(Owner));
}

// A class-scope full specialization owns a body, but no template parameter
// level. Its specialized primary remains the argument-list owner.
static bool classScopeFullIdentity(const CXXRecordDecl *Record) {
  const auto *Full = dyn_cast_or_null<ClassTemplateSpecializationDecl>(Record);
  return Full && Full->getKind() == Decl::ClassTemplateSpecialization &&
         Full->isClassScopeExplicitSpecialization() && !Full->isInvalidDecl() &&
         !Full->isUnion() && !Full->isLocalClass() && !Full->isLambda() &&
         Full->getLexicalDeclContext() == Full->getDeclContext() &&
         Full->getSpecializedTemplate()->getDeclContext() == Full->getDeclContext();
}

static bool genericClassFullIdentity(const CXXRecordDecl *Record) {
  return classScopeFullIdentity(Record) && Record->isDependentContext() &&
         Record->getDeclContext()->isDependentContext() &&
         !Record->getInstantiatedFromMemberClass();
}

// Ordinary named members add a body scope, never a template argument level.
static bool ordinaryMemberClassIdentity(const CXXRecordDecl *Record) {
  return Record && Record->getKind() == Decl::CXXRecord &&
         !Record->getDescribedClassTemplate() && Record->getIdentifier() &&
         !Record->isInvalidDecl() && !Record->isInjectedClassName() &&
         !Record->isUnion() && !Record->isLambda() && !Record->isLocalClass() &&
         isa<CXXRecordDecl>(Record->getDeclContext());
}

static bool ordinaryMemberClassScope(const CXXRecordDecl *Record) {
  return ordinaryMemberClassIdentity(Record) &&
         (Record->isDependentContext() || Record->getInstantiatedFromMemberClass());
}

// Pinned Sema mutates an earlier implicit canonical declaration's kind and
// location. Only an actual out-of-line redeclaration spells the own header.
// The allowlist independently verifies this source against lexical inventory.
static const CXXRecordDecl *writtenOwnMemberClass(const CXXRecordDecl *Record) {
  if (!ordinaryMemberClassScope(Record))
    return nullptr;
  const CXXRecordDecl *Written = nullptr;
  unsigned Count = 0;
  for (const auto *Tag : Record->redecls()) {
    if (++Count > 64)
      return nullptr;
    const auto *D = dyn_cast<CXXRecordDecl>(Tag);
    if (!ordinaryMemberClassIdentity(D) || !D->getInstantiatedFromMemberClass() ||
        D->getTemplateSpecializationKind() != TSK_ExplicitSpecialization ||
        !D->getNumTemplateParameterLists() || !D->getQualifier() ||
        !isa<TranslationUnitDecl, NamespaceDecl>(D->getLexicalDeclContext()))
      continue;
    if (D->isCompleteDefinition())
      return D;
    Written = D;
  }
  return Written;
}

static bool ordinaryCopiedBody(const CXXRecordDecl *Record) {
  return ordinaryMemberClassScope(Record) &&
         Record->getInstantiatedFromMemberClass() && !writtenOwnMemberClass(Record);
}

// Body identity only. Source/structural validation must independently admit
// the actual declaration and each copy edge before any member is emitted.
static const CXXRecordDecl *classBodyRecord(const CXXRecordDecl *Record) {
  if (ordinaryMemberClassIdentity(Record)) {
    std::set<const CXXRecordDecl *> Seen;
    while (ordinaryMemberClassIdentity(Record)) {
      if (Seen.size() >= 64 || !Seen.insert(Record->getCanonicalDecl()).second)
        return nullptr;
      if (const auto *Own = writtenOwnMemberClass(Record))
        return Own;
      const auto *Origin = Record->getInstantiatedFromMemberClass();
      if (!Origin)
        return Record;
      if (Record->getTemplateSpecializationKind() == TSK_ExplicitSpecialization)
        return nullptr; // A mutated canonical bit is not its own written body.
      Record = Origin;
    }
    return nullptr;
  }
  const auto *Full = dyn_cast_or_null<ClassTemplateSpecializationDecl>(Record);
  if (!Full || Full->getKind() != Decl::ClassTemplateSpecialization ||
      !Full->isExplicitSpecialization())
    return classDefinitionRecord(classTemplatePattern(Record));
  std::set<const CXXRecordDecl *> Seen;
  while (Record) {
    if (Seen.size() >= 64 || !Seen.insert(Record->getCanonicalDecl()).second)
      return nullptr;
    const auto *Origin = Record->getInstantiatedFromMemberClass();
    if (!Origin)
      return Record;
    if (!classScopeFullIdentity(Record) || !classScopeFullIdentity(Origin))
      return nullptr;
    Record = Origin;
  }
  return nullptr;
}

// Source packs in a full body still belong to a real enclosing parameter
// level. This helper is only used for pack indexing, never argument lookup.
static const NamedDecl *classBodyPackOwner(const CXXRecordDecl *Record) {
  std::set<const CXXRecordDecl *> Seen;
  while (classScopeFullIdentity(Record) || ordinaryMemberClassIdentity(Record)) {
    if (Seen.size() >= 64 || !Seen.insert(Record->getCanonicalDecl()).second)
      return nullptr;
    Record = dyn_cast<CXXRecordDecl>(Record->getDeclContext());
  }
  return classTemplatePattern(Record);
}

// Keep this classification separate from classFunctionPattern: a full body
// must never be returned as a synthetic template parameter owner.
static const ClassTemplateSpecializationDecl *copiedFullClassFunction(
    const FunctionDecl *Function) {
  const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Function);
  if (!Method || Method->getDescribedFunctionTemplate() ||
      Method->getTemplatedKind() != FunctionDecl::TK_MemberSpecialization ||
      !Method->getMemberSpecializationInfo())
    return nullptr;
  const auto *Full = dyn_cast<ClassTemplateSpecializationDecl>(Method->getParent());
  const auto *Pattern = dyn_cast_or_null<CXXMethodDecl>(Method->getInstantiatedFromMemberFunction());
  const auto *Body = classBodyRecord(Full);
  return classScopeFullIdentity(Full) && !Full->isDependentContext() &&
         Full->getInstantiatedFromMemberClass() && Pattern && Body &&
         Pattern->getKind() == Method->getKind() &&
         Pattern->getParent()->getCanonicalDecl() == Body->getCanonicalDecl()
             ? Full : nullptr;
}

static const ClassTemplateSpecializationDecl *copiedFullClassStatic(const VarDecl *Variable) {
  if (!Variable || Variable->getKind() != Decl::Var ||
      !Variable->isStaticDataMember() || Variable->getDescribedVarTemplate() ||
      !Variable->getMemberSpecializationInfo())
    return nullptr;
  const auto *Full = dyn_cast<ClassTemplateSpecializationDecl>(Variable->getDeclContext());
  const auto *Pattern = Variable->getInstantiatedFromStaticDataMember();
  const auto *Parent = Pattern ? dyn_cast<CXXRecordDecl>(Pattern->getDeclContext()) : nullptr;
  const auto *Body = classBodyRecord(Full);
  return classScopeFullIdentity(Full) && !Full->isDependentContext() &&
         Full->getInstantiatedFromMemberClass() && Pattern && Parent && Body &&
         Pattern->getKind() == Decl::Var && Pattern->isStaticDataMember() &&
         !Pattern->getDescribedVarTemplate() &&
         Parent->getCanonicalDecl() == Body->getCanonicalDecl() ? Full : nullptr;
}

// Ordinary copied methods/statics have no synthetic primary of their own.
static const CXXRecordDecl *copiedOrdinaryClassFunction(const FunctionDecl *Function) {
  const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Function);
  if (!Method || Method->getDescribedFunctionTemplate() ||
      Method->getTemplatedKind() != FunctionDecl::TK_MemberSpecialization ||
      !Method->getMemberSpecializationInfo())
    return nullptr;
  const auto *Record = Method->getParent();
  const auto *Pattern = dyn_cast_or_null<CXXMethodDecl>(Method->getInstantiatedFromMemberFunction());
  const auto *Body = classBodyRecord(Record);
  return ordinaryMemberClassScope(Record) && !Record->isDependentContext() &&
         Record->getInstantiatedFromMemberClass() && Pattern && Body &&
         Pattern->getKind() == Method->getKind() &&
         Pattern->getParent()->getCanonicalDecl() == Body->getCanonicalDecl()
             ? Record : nullptr;
}

static const CXXRecordDecl *copiedOrdinaryClassStatic(const VarDecl *Variable) {
  if (!Variable || Variable->getKind() != Decl::Var ||
      !Variable->isStaticDataMember() || Variable->getDescribedVarTemplate() ||
      !Variable->getMemberSpecializationInfo())
    return nullptr;
  const auto *Record = dyn_cast<CXXRecordDecl>(Variable->getDeclContext());
  const auto *Pattern = Variable->getInstantiatedFromStaticDataMember();
  const auto *Parent = Pattern ? dyn_cast<CXXRecordDecl>(Pattern->getDeclContext()) : nullptr;
  const auto *Body = classBodyRecord(Record);
  return ordinaryMemberClassScope(Record) && !Record->isDependentContext() &&
         Record->getInstantiatedFromMemberClass() && Pattern && Parent && Body &&
         Pattern->getKind() == Decl::Var && Pattern->isStaticDataMember() &&
         !Pattern->getDescribedVarTemplate() &&
         Parent->getCanonicalDecl() == Body->getCanonicalDecl() ? Record : nullptr;
}

// Derive depth from actual enclosing owners, independently of the parameter
// being checked. Clang removes substituted outer levels from copied primaries.
static std::optional<unsigned> templateSourceParameterDepth(const NamedDecl *Owner) {
  if (!Owner || !templateSourceParameters(Owner))
    return std::nullopt;
  const auto *Context = Owner->getDeclContext();
  if (const auto *Function = dyn_cast<FunctionTemplateDecl>(Owner);
      Function && ordinaryFreeFunctionName(Function->getTemplatedDecl()) &&
      Function->getTemplatedDecl()->getFriendObjectKind() &&
      isa<CXXRecordDecl>(Function->getLexicalDeclContext()))
    Context = Function->getLexicalDeclContext();
  if (const auto *Class = dyn_cast<ClassTemplateDecl>(Owner);
      Class && Class->getFriendObjectKind() &&
      isa<CXXRecordDecl>(Class->getLexicalDeclContext()))
    Context = Class->getLexicalDeclContext();
  std::set<const DeclContext *> Seen;
  unsigned Depth = 0;
  while (Context && !isa<TranslationUnitDecl, NamespaceDecl>(Context)) {
    const auto *Record = dyn_cast<CXXRecordDecl>(Context);
    if (!Record || Record->isInvalidDecl() || Record->isUnion() ||
        Record->isLambda() || Record->isLocalClass() ||
        Seen.size() >= 64 || !Seen.insert(Context).second)
      return std::nullopt;
    if (Record->isDependentContext() && !genericClassFullIdentity(Record) &&
        !ordinaryMemberClassIdentity(Record)) {
      if (!isa<ClassTemplatePartialSpecializationDecl>(Record) &&
          !(Record->getKind() == Decl::CXXRecord && Record->getDescribedClassTemplate()))
        return std::nullopt;
      ++Depth;
    }
    Context = Record->getDeclContext();
  }
  return Context ? std::optional<unsigned>(Depth) : std::nullopt;
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
      Record->isDependentContext() || Record->getInstantiatedFromMemberClass())
    return nullptr;
  return classTemplatePattern(Record);
}

static bool concreteClassFunction(const FunctionDecl *F) {
  return (classFunctionPattern(F) || copiedFullClassFunction(F) ||
          copiedOrdinaryClassFunction(F)) && !F->isDependentContext() &&
         !F->getType().isNull() && !F->getType()->isDependentType();
}

// Structural function category only. The allowlist separately requires both
// exact friend source events before traversing or emitting this declaration.
static bool concreteFriendFunction(const FunctionDecl *F) {
  if (!F || F->getKind() != Decl::Function || !F->getFriendObjectKind() ||
      F->getDescribedFunctionTemplate() || F->getPrimaryTemplate() ||
      F->getTemplatedKind() != FunctionDecl::TK_MemberSpecialization ||
      !F->getMemberSpecializationInfo() || !F->getInstantiatedFromMemberFunction() ||
      !F->getDeclContext()->getRedeclContext()->isFileContext() ||
      F->isDependentContext() || F->getType().isNull() || F->getType()->isDependentType())
    return false;
  const auto *Parent = dyn_cast<CXXRecordDecl>(F->getLexicalDeclContext());
  return Parent && !Parent->isDependentContext() && !Parent->isLocalClass() &&
         classBodyRecord(Parent);
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
    if (concreteFunctionTemplate(Owner) || concreteClassFunction(Owner) ||
        concreteFriendFunction(Owner))
      return true;
    F = Owner; // A local class can itself be declared in a local-class method.
  }
  return false;
}

static bool concreteMemberFunction(const FunctionDecl *F) {
  return concreteMemberFunctionTemplate(F) || concreteClassFunction(F) ||
         concreteLocalClassFunction(F);
}

// Identity only: a static member definition/initializer can still be lazy.
static const NamedDecl *classStaticDataPattern(const VarDecl *V) {
  if (!V || V->getKind() != Decl::Var || !V->isStaticDataMember() ||
      V->getDescribedVarTemplate() || !V->getMemberSpecializationInfo())
    return nullptr;
  const auto *Record = dyn_cast<ClassTemplateSpecializationDecl>(V->getDeclContext());
  const auto *Origin = V->getInstantiatedFromStaticDataMember();
  if (!Record || Record->getKind() != Decl::ClassTemplateSpecialization ||
      Record->isDependentContext() || Record->getInstantiatedFromMemberClass() ||
      !Origin || Origin->getKind() != Decl::Var ||
      !Origin->isStaticDataMember() || Origin->getDescribedVarTemplate())
    return nullptr;
  const auto *Primary = classTemplatePattern(Record);
  const auto *Parent = dyn_cast<CXXRecordDecl>(Origin->getDeclContext());
  const auto *Body = classBodyRecord(Record);
  return Primary && Parent && Body &&
                 Parent->getCanonicalDecl() == Body->getCanonicalDecl()
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
  if ((!classFunctionPattern(M) && !copiedFullClassFunction(M) &&
       !copiedOrdinaryClassFunction(M)) || Specialization)
    return nullptr;
  const auto *Pattern =
      dyn_cast_or_null<CXXMethodDecl>(M->getInstantiatedFromMemberFunction());
  const auto *Body = classBodyRecord(M->getParent());
  if (!Pattern || !Body || Pattern->getKind() != M->getKind() ||
      Pattern->getParent()->getCanonicalDecl() != Body->getCanonicalDecl())
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

// Deleted declarations take part in Sema's overload resolution, but never have
// an executable definition. Keep their source signature checks separate from
// the predicates that admit callable functions and generated special members.
static bool deletedFunctionDeclaration(const FunctionDecl *F) {
  const auto *M = dyn_cast_or_null<CXXMethodDecl>(F);
  if (!F || F->isImplicit() || F->isInvalidDecl() || !F->isDeleted() ||
      (!F->getCanonicalDecl()->isDeletedAsWritten() && !defaultedSpecialMember(M)) ||
      F->hasBody() || F->isVariadic() || F->isConsteval())
    return false;
  if (M) {
    if (M->isVirtual() || M->isExplicitObjectMemberFunction() ||
        M->getMethodQualifiers().hasVolatile() ||
        M->getMethodQualifiers().hasRestrict())
      return false;
    if (const auto *C = dyn_cast<CXXConstructorDecl>(M)) {
      if (C->isInheritingConstructor())
        return false;
    } else if (!isa<CXXDestructorDecl, CXXConversionDecl>(M) &&
               !M->getIdentifier() &&
               !ordinaryOperatorKind(M->getOverloadedOperator())) {
      return false;
    }
  } else if (!F->getIdentifier() &&
             !ordinaryOperatorKind(F->getOverloadedOperator())) {
    return false;
  }
  for (const auto *D : F->redecls()) {
    const auto *Info = D->getTypeSourceInfo();
    if (D->isInvalidDecl() || !Info)
      return false;
    auto Location = Info->getTypeLoc().getAs<FunctionProtoTypeLoc>();
    if (!Location || (Location.getExceptionSpecRange().isValid() &&
        !standardExceptionSpecification(D->getType()->getAs<FunctionProtoType>())))
      return false;
  }
  return true;
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
       !concreteFreeFunctionTemplate(F) && !concreteMemberFunction(F) &&
       !concreteFriendFunction(F)) ||
      F->isDeletedAsWritten() || F->isDefaulted() || F->isConsteval())
    return false;
  if (!ordinaryOperatorKind(F->getOverloadedOperator()))
    return false;
  if (const auto *M = dyn_cast<CXXMethodDecl>(F))
    if (!M->isUserProvided() || M->isVirtual() ||
        M->isStatic() != allocationOperatorKind(F->getOverloadedOperator()) ||
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
  if (const auto *D = dyn_cast_or_null<CXXDestructorDecl>(M))
    return ordinaryDestructor(D) || defaultedLifecycle(D);
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
  const auto *Descriptor = M->getLifetimeExtendedTemporaryDecl();
  if (!Owner || Owner->getKind() != Decl::Var || Owner->isImplicit() ||
      !Owner->isLocalVarDecl() || !Owner->hasLocalStorage() ||
      (!Owner->getType()->isReferenceType() && !Owner->getType()->isRecordType() &&
       !Context.getAsConstantArrayType(Owner->getType())) ||
      !Descriptor || Descriptor->getTemporaryExpr() != M->getSubExpr() ||
      Descriptor->getExtendingDecl() != Owner || Descriptor->getStorageDuration() != SD_Automatic)
    return nullptr;
  return Owner->getCanonicalDecl();
}
const VarDecl *Adapter::staticTemporaryOwner(const MaterializeTemporaryExpr *M) const {
  if (!S.coreV2() || !temporaryShape(M, Context) || M->getStorageDuration() != SD_Static ||
      M->getType().isVolatileQualified())
    return nullptr;
  const auto *Descriptor = M->getLifetimeExtendedTemporaryDecl();
  const auto *Owner = dyn_cast_or_null<VarDecl>(M->getExtendingDecl());
  if (!Descriptor || Descriptor->getTemporaryExpr() != M->getSubExpr() ||
      Descriptor->getExtendingDecl() != Owner || Descriptor->getStorageDuration() != SD_Static ||
      !Owner || Owner->isImplicit() || Owner->isInvalidDecl() ||
      Owner->getDeclContext()->isDependentContext() ||
      (!Owner->getType()->isReferenceType() && !Owner->getType()->isRecordType() &&
       !Context.getAsConstantArrayType(Owner->getType())) ||
      !Owner->hasGlobalStorage() || Owner->getTLSKind() != VarDecl::TLS_None ||
      !S.owns(Sources, M->getExprLoc()) || !S.owns(Sources, Owner->getLocation()))
    return nullptr;
  const auto *Definition = Owner->getDefinition();
  if (!Definition || !S.owns(Sources, Definition->getLocation()) ||
      Definition->getCanonicalDecl() != Owner->getCanonicalDecl() ||
      !Context.hasSameType(Definition->getType(), Owner->getType()))
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
  if (const auto *Template = dyn_cast<VarTemplateDecl>(Associated))
    return Template;
  if (const auto *Variable = dyn_cast<VarDecl>(Associated))
    return variableTemplatePattern(Variable);
  if (const auto *Function = dyn_cast<FunctionDecl>(Associated))
    return concreteFunctionTemplate(Function) ? Function->getPrimaryTemplate() : nullptr;
  if (const auto *Template = dyn_cast<FunctionTemplateDecl>(Associated))
    return ordinaryFreeFunctionName(Template->getTemplatedDecl()) ||
                   ordinaryMemberTemplateName(Template->getTemplatedDecl()) ? Template : nullptr;
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
    return Type->getDepth() <= 64 && Type->isParameterPack() && !Type->hasTypeConstraint();
  if (const auto *Value = dyn_cast<NonTypeTemplateParmDecl>(Pack))
    return Value->getDepth() <= 64 && Value->isParameterPack();
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

static bool scalarTemplateType(QualType Type) {
  return !Type.isNull() && !Type->isDependentType() &&
         (Type->isIntegralOrEnumerationType() || Type->isNullPtrType());
}

static QualType scalarTemplateArgumentType(const TemplateArgument &Argument) {
  // Clang also uses NullPtr for null values of pointer type. Only nullptr_t
  // joins the existing integer/boolean/enum template value domain here.
  if (Argument.getKind() == TemplateArgument::Integral) {
    auto Type = Argument.getIntegralType();
    if (scalarTemplateType(Type) && Type->isIntegralOrEnumerationType())
      return Type;
  } else if (Argument.getKind() == TemplateArgument::NullPtr) {
    auto Type = Argument.getNullPtrType();
    if (scalarTemplateType(Type) && Type->isNullPtrType())
      return Type;
  }
  return {};
}

static bool scalarTemplateValue(QualType Type, const APValue &Value) {
  return scalarTemplateType(Type) &&
         (Type->isNullPtrType() ? Value.isLValue() && Value.isNullPointer()
                               : Value.isInt());
}

static bool scalarTemplateValueMatches(const Expr *Expression,
                                       const TemplateArgument &Argument,
                                       ASTContext &Context) {
  auto Type = scalarTemplateArgumentType(Argument);
  if (Type.isNull() || !Expression || Expression->getType().isNull() ||
      Expression->isInstantiationDependent() ||
      !Context.hasSameType(Expression->getType(), Type))
    return false;
  APValue Value;
  return Expression->isCXX11ConstantExpr(Context, &Value) &&
         scalarTemplateValue(Type, Value) &&
         (Type->isNullPtrType() || Value.getInt() == Argument.getAsIntegral());
}

const Expr *scalarTemplateReplacement(const SubstNonTypeTemplateParmExpr *E,
                                      ASTContext &Context) {
  if (!E || E->getType().isNull() || !E->isPRValue() ||
      !scalarTemplateType(E->getType()) || E->isTypeDependent() ||
      E->isValueDependent() || E->isInstantiationDependent() ||
      E->isReferenceParameter())
    return nullptr;
  const auto *Primary = scalarTemplateOwner(E);
  const auto Depth = templateSourceParameterDepth(Primary);
  if (!Depth || !Primary || !templateSourceParameters(Primary) ||
      E->getIndex() >= templateSourceParameters(Primary)->size())
    return nullptr;
  // getParameter() performs an unchecked index and cast in pinned Clang.
  const auto *Parameter = dyn_cast<NonTypeTemplateParmDecl>(
      templateSourceParameters(Primary)->getParam(E->getIndex()));
  if (!Parameter || Parameter->getDepth() != *Depth ||
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
  return Replacement->isCXX11ConstantExpr(Context, &Value) &&
                 scalarTemplateValue(Replacement->getType(), Value)
             ? Replacement : nullptr;
}

static bool lazyTemplateDefault(const ParmVarDecl *P) {
  if (!P || !P->hasDefaultArg() || P->hasUnparsedDefaultArg() ||
      !P->hasUninstantiatedDefaultArg())
    return false;
  const auto *Function = dyn_cast<FunctionDecl>(P->getDeclContext());
  return concreteFreeFunctionTemplate(Function) || concreteMemberFunction(Function) ||
         concreteFriendFunction(Function);
}

const Expr *defaultArgumentInitializer(const ParmVarDecl *P, ASTContext &Context) {
  if (!P || P->isImplicit() || P->isInvalidDecl() || !P->hasDefaultArg() ||
      P->hasUnparsedDefaultArg() || P->hasUninstantiatedDefaultArg() ||
      P->getType().isNull() || P->getType()->isDependentType())
    return nullptr;
  const auto *F = dyn_cast<FunctionDecl>(P->getDeclContext());
  if (!F || (F->getTemplatedKind() != FunctionDecl::TK_NonTemplate &&
             !concreteFreeFunctionTemplate(F) && !concreteMemberFunction(F) &&
             !concreteFriendFunction(F)) ||
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

const CXXPseudoDestructorExpr *scalarDestruction(const CallExpr *Call,
                                                ASTContext &Context) {
  if (!Call || Call->getNumArgs() || !Call->getType()->isVoidType() ||
      Call->isTypeDependent() || Call->isValueDependent() ||
      Call->isInstantiationDependent())
    return nullptr;
  const auto *D = dyn_cast<CXXPseudoDestructorExpr>(Call->getCallee()->IgnoreParens());
  if (!D || !D->getBase() || !D->getDestroyedTypeInfo())
    return nullptr;
  auto Object = D->getBase()->getType();
  if (D->isArrow()) {
    if (!Object->isPointerType())
      return nullptr;
    Object = Object->getPointeeType();
  }
  auto Destroyed = D->getDestroyedType();
  return !Destroyed.isNull() && Destroyed->isScalarType() &&
                 Context.hasSameUnqualifiedType(Object, Destroyed)
             ? D : nullptr;
}

std::string Adapter::requireStaticDestruction(llvm::StringRef Global, QualType T,
                                               SourceLocation L) {
  if (!S.coreV2() || !needsDestruction(T))
    throw Failure{};
  chargeExpansion(1, L);
  auto Name = "nct_static_destruction_" + digest(Global).substr(0, 16);
  StaticDestructions.push_back({Global.str(), Name, T, L});
  return Name;
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

// Only these existing direct-call syntax roles bypass callback-value rules.
// A compound postfix expression must use value lowering even when Sema can
// determine a direct target, otherwise its side effects would disappear.
const Expr *directFunctionReference(const CallExpr *Call) {
  const auto *F = Call->getDirectCallee();
  if (!F)
    return nullptr;
  if (isa<CXXMethodDecl>(F))
    return directMethodReference(Call);
  const Expr *E = Call->getCallee();
  while (true) {
    if (const auto *P = dyn_cast<ParenExpr>(E))
      E = P->getSubExpr();
    else if (const auto *C = dyn_cast<ImplicitCastExpr>(E);
             C && C->getCastKind() == CK_FunctionToPointerDecay)
      E = C->getSubExpr();
    else
      break;
  }
  const auto *R = dyn_cast<DeclRefExpr>(E);
  return R && R->getDecl()->getCanonicalDecl() == F->getCanonicalDecl() ? E : nullptr;
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
    for (const auto &Base : R->bases()) {
      auto Added = storageUnits(Base.getType(), Depth + 1);
      if (Added > Limit - Units)
        return Limit + 1;
      Units += Added;
    }
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

// The source prototype must describe exactly the default emitted function ABI.
static bool ordinaryCallbackPrototype(const FunctionProtoType *P) {
  if (!P || P->isVariadic() || P->getNumParams() > 64 ||
      !standardExceptionSpecification(P) ||
      P->getExtInfo() != FunctionType::ExtInfo() ||
      !P->getMethodQuals().empty() || P->getRefQualifier() != RQ_None ||
      P->getAArch64SMEAttributes() != FunctionType::SME_NormalFunction ||
      !P->getFunctionEffects().empty())
    return false;
  for (unsigned I = 0; I < P->getNumParams(); ++I)
    if (P->getExtParameterInfo(I) != FunctionType::ExtParameterInfo())
      return false;
  return true;
}

std::string Adapter::functionPointerType(QualType T, SourceLocation L,
                                         unsigned Depth) {
  const auto *P = T->isFunctionPointerType()
                      ? T->getPointeeType()->getAs<FunctionProtoType>() : nullptr;
  if (!S.coreV2() || !P || Depth > 64 || T.getAddressSpace() != LangAS::Default ||
      T->getPointeeType().getAddressSpace() != LangAS::Default ||
      !ordinaryCallbackPrototype(P)) {
    reject(L, "function pointer type", "A bounded ordinary default-ABI function prototype is required.");
    return {};
  }
  auto Carrier = Context.VoidPtrTy;
  if (Context.getTypeSize(T) != Context.getTypeSize(Carrier) ||
      Context.getTypeAlign(T) != Context.getTypeAlign(Carrier)) {
    reject(L, "function pointer layout", "Source code-pointer and default carrier layouts differ.", "TR0204");
    return {};
  }
  chargeExpansion(P->getNumParams() + 1, L);
  std::string Result = "fnptr:" + std::to_string(P->getNumParams()) + ":";
  auto Component = [&](QualType C, bool Void) {
    if (C->isRecordType() || C->isArrayType()) {
      reject(L, "callback signature", "Record and array values need a separate callback ownership contract.");
      return false;
    }
    auto Spelling = type(C, L, Void, Depth + 1);
    if (Spelling.empty())
      return false;
    Result += std::to_string(Spelling.size()) + ":" + Spelling;
    if (Result.size() > 4096) {
      reject(L, "function pointer type", "Callable type spelling exceeds the protocol limit.");
      return false;
    }
    return true;
  };
  if (!Component(P->getReturnType(), true))
    return {};
  for (auto Parameter : P->param_types())
    if (!Component(Parameter, false))
      return {};
  return Result;
}

void Adapter::checkQueryType(QualType T, SourceLocation L) {
  if (T.isNull() || T->isDependentType() || T->isInstantiationDependentType()) {
    reject(L, "query type source", "Every queried type requires a resolved written type source.");
    throw Failure{};
  }
  if (T->isFunctionType()) {
    // Bare function aliases already use this signature contract. Check the
    // prototype before forming a pointer to reject cv/ref-qualified functions.
    if (T.hasQualifiers() ||
        !ordinaryCallbackPrototype(T->getAs<FunctionProtoType>())) {
      reject(L, "queried function type", "Type queries require an ordinary admitted callback signature.");
      throw Failure{};
    }
    if (functionPointerType(Context.getPointerType(T), L).empty())
      throw Failure{};
  } else if (type(T, L, true).empty()) {
    throw Failure{};
  }
}

// A hypothetical operation needs source admission, but no runtime owner or
// helper. User operations require exact definitions that have completed normal
// source traversal. Do not reuse runtime construction/default caches or infer
// completed source proof from the function emission queue.
static bool operationTraitSource(Adapter &A, const OperationTraitSource &Source,
    const std::set<const FunctionDecl *> *Definitions = nullptr,
    bool RequiresExceptionSource = false) {
  if (!Source.Attempted)
    return !Source.Root && Source.Operands.empty();
  if (!Source.Complete || !Source.Root)
    return false;
  auto ExceptionSource = [&](const FunctionDecl *Function) {
    // Sema applies canThrow to this retained root. Read its resolved selected
    // signatures; never resolve a later callee skipped by canThrow's early exit.
    return !RequiresExceptionSource || (Function &&
        standardExceptionSpecification(Function->getType()->getAs<FunctionProtoType>()));
  };
  auto Defined = [&](const FunctionDecl *Function) {
    const auto *Definition = Function ? Function->getDefinition() : nullptr;
    return Definitions && Definition && Definitions->count(Definition) &&
           Function->getTemplatedKind() == FunctionDecl::TK_NonTemplate &&
           A.S.owns(A.Sources, Function->getLocation());
  };
  // A written destructor body omits implicit field/base destruction. Check
  // those owning subobjects separately, including when Sema's root has no bind.
  auto Destruction = [&](auto &&Self, const CXXRecordDecl *Record,
                         unsigned Depth) -> bool {
    Record = Record ? Record->getDefinition() : nullptr;
    if (!Record || Depth > 64 || !A.S.owns(A.Sources, Record->getLocation()))
      return false;
    A.chargeExpansion(1, Record->getLocation());
    if (Record->hasUserDeclaredDestructor()) {
      const auto *Destructor = Record->getDestructor();
      if (!ordinaryDestructor(Destructor) || !Defined(Destructor))
        return false;
    } else if (!Record->hasTrivialDestructor()) {
      return false;
    }
    for (const auto &Base : Record->bases())
      if (!Self(Self, Base.getType()->getAsCXXRecordDecl(), Depth + 1))
        return false;
    for (const auto *Field : Record->fields())
      if (const auto *Member = A.Context.getBaseElementType(Field->getType())->getAsCXXRecordDecl())
        if (!Self(Self, Member, Depth + 1))
          return false;
    return true;
  };
  enum class Family { Default, CopyMove, Assignment };
  // A trivial generated parent can still select an explicitly defaulted field
  // operation with written noexcept source. Until those selections are retained,
  // require this operation family to be implicit throughout owned subobjects.
  auto ImplicitClosure = [&](auto &&Self, const CXXRecordDecl *Record,
                             Family Operation, unsigned Depth) -> bool {
    Record = Record ? Record->getDefinition() : nullptr;
    if (!Record || Depth > 64 || !A.S.owns(A.Sources, Record->getLocation()))
      return false;
    A.chargeExpansion(1, Record->getLocation());
    if ((Operation == Family::Default && Record->hasUserDeclaredConstructor()) ||
        (Operation == Family::CopyMove && (Record->hasUserDeclaredCopyConstructor() ||
                                         Record->hasUserDeclaredMoveConstructor())) ||
        (Operation == Family::Assignment && (Record->hasUserDeclaredCopyAssignment() ||
                                            Record->hasUserDeclaredMoveAssignment())) ||
        (Operation != Family::Assignment && (Record->hasUserDeclaredDestructor() ||
                                            !Record->hasTrivialDestructor())))
      return false;
    for (const auto &Base : Record->bases())
      if (!Self(Self, Base.getType()->getAsCXXRecordDecl(), Operation, Depth + 1))
        return false;
    for (const auto *Field : Record->fields()) {
      auto T = A.Context.getBaseElementType(Field->getType());
      if (const auto *Member = T->getAsCXXRecordDecl())
        if (!Self(Self, Member, Operation, Depth + 1))
          return false;
    }
    return true;
  };
  const std::set<const Expr *> Operands(Source.Operands.begin(), Source.Operands.end());
  auto Check = [&](auto &&Self, const Expr *E, unsigned Depth) -> bool {
    if (!E || Depth > 64 || E->isInstantiationDependent())
      return false;
    A.chargeExpansion(1, E->getExprLoc());
    A.checkQueryType(E->getType(), E->getExprLoc());
    if (isa<OpaqueValueExpr>(E))
      return Operands.count(E);
    if (const auto *Cast = dyn_cast<ImplicitCastExpr>(E)) {
      switch (Cast->getCastKind()) {
      case CK_NoOp: case CK_LValueToRValue:
      case CK_ArrayToPointerDecay: case CK_FunctionToPointerDecay:
      case CK_IntegralCast: case CK_IntegralToBoolean:
      case CK_IntegralToFloating: case CK_FloatingToIntegral:
      case CK_FloatingToBoolean: case CK_FloatingCast:
      case CK_NullToPointer: case CK_PointerToBoolean:
        return Self(Self, Cast->getSubExpr(), Depth + 1);
      case CK_ConstructorConversion:
        return constructorConversion(Cast, A.Context) &&
               Self(Self, Cast->getSubExpr(), Depth + 1);
      case CK_UserDefinedConversion:
        return userConversionCall(Cast, A.Context) &&
               Self(Self, Cast->getSubExpr(), Depth + 1);
      case CK_BitCast:
        return Cast->getType()->isPointerType() &&
               Cast->getSubExpr()->getType()->isPointerType() &&
               !Cast->getType()->isFunctionPointerType() &&
               !Cast->getSubExpr()->getType()->isFunctionPointerType() &&
               (Cast->getType()->getPointeeType()->isVoidType() ||
                Cast->getSubExpr()->getType()->getPointeeType()->isVoidType()) &&
               Self(Self, Cast->getSubExpr(), Depth + 1);
      default:
        return false;
      }
    }
    if (const auto *Construction = dyn_cast<CXXConstructExpr>(E)) {
      const auto *Constructor = Construction->getConstructor();
      const bool Implicit = Constructor->isImplicit() && Constructor->isTrivial() &&
          (Constructor->isDefaultConstructor() || Constructor->isCopyOrMoveConstructor()) &&
          ImplicitClosure(ImplicitClosure, Constructor->getParent(),
                          Constructor->isDefaultConstructor() ? Family::Default
                                                              : Family::CopyMove, 0);
      if ((!Implicit && !(ordinaryConstructor(Constructor) && Defined(Constructor))) ||
          !ExceptionSource(Constructor) ||
          !A.S.owns(A.Sources, Constructor->getLocation()) || !Construction->isPRValue() ||
          Construction->getConstructionKind() != CXXConstructionKind::Complete ||
          Construction->getNumArgs() != Constructor->getNumParams() ||
          !A.Context.hasSameUnqualifiedType(A.Context.getBaseElementType(Construction->getType()),
              A.Context.getRecordType(Constructor->getParent())) ||
          !Destruction(Destruction, Constructor->getParent(), 0))
        return false;
      for (const auto *Argument : Construction->arguments())
        if (!Self(Self, Argument, Depth + 1))
          return false;
      return true;
    }
    if (const auto *Call = dyn_cast<CXXOperatorCallExpr>(E)) {
      const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Call->getDirectCallee());
      if (Call->getOperator() != OO_Equal || !Method || Method->getNumParams() != 1 ||
          !A.S.owns(A.Sources, Method->getLocation()) || Call->getNumArgs() != 2)
        return false;
      const bool Implicit = Method->isImplicit() && Method->isTrivial() &&
          (Method->isCopyAssignmentOperator() || Method->isMoveAssignmentOperator()) &&
          ImplicitClosure(ImplicitClosure, Method->getParent(), Family::Assignment, 0);
      if (!Implicit && !(ordinaryOperator(Method) && Defined(Method) && directMethodReference(Call)))
        return false;
      if (!ExceptionSource(Method))
        return false;
      if (Call->isPRValue() && Call->getType()->isRecordType() &&
          !Destruction(Destruction, Call->getType()->getAsCXXRecordDecl(), 0))
        return false;
      return Self(Self, Call->getArg(0), Depth + 1) &&
             Self(Self, Call->getArg(1), Depth + 1);
    }
    if (const auto *Call = dyn_cast<CXXMemberCallExpr>(E)) {
      const auto *Conversion = dyn_cast_or_null<CXXConversionDecl>(Call->getDirectCallee());
      if (!ordinaryConversion(Conversion) || !Defined(Conversion) ||
          !ExceptionSource(Conversion) ||
          !directMethodReference(Call) || Call->getNumArgs() != 0 ||
          !Call->getImplicitObjectArgument())
        return false;
      if (Call->isPRValue() && Call->getType()->isRecordType() &&
          !Destruction(Destruction, Call->getType()->getAsCXXRecordDecl(), 0))
        return false;
      return Self(Self, Call->getImplicitObjectArgument(), Depth + 1);
    }
    if (const auto *Assignment = dyn_cast<BinaryOperator>(E))
      return Assignment->getOpcode() == BO_Assign && Assignment->isLValue() &&
             Assignment->getLHS()->isLValue() &&
             Assignment->getType()->isScalarType() &&
             A.Context.hasSameType(Assignment->getType(), Assignment->getLHS()->getType()) &&
             Self(Self, Assignment->getLHS(), Depth + 1) &&
             Self(Self, Assignment->getRHS(), Depth + 1);
    if (const auto *Temporary = dyn_cast<CXXBindTemporaryExpr>(E)) {
      const auto *Destructor = Temporary->getTemporary()->getDestructor();
      const auto *Sub = Temporary->getSubExpr();
      return Destructor && Sub && Temporary->isPRValue() && Sub->isPRValue() &&
             ExceptionSource(Destructor) &&
             A.Context.hasSameType(Temporary->getType(), Sub->getType()) &&
             A.Context.hasSameUnqualifiedType(Temporary->getType(),
                 A.Context.getRecordType(Destructor->getParent())) &&
             Destruction(Destruction, Destructor->getParent(), 0) &&
             Self(Self, Temporary->getSubExpr(), Depth + 1);
    }
    if (const auto *Temporary = dyn_cast<MaterializeTemporaryExpr>(E))
      return fullExpressionTemporary(Temporary, A.Context) &&
             Self(Self, Temporary->getSubExpr(), Depth + 1);
    if (const auto *Cleanup = dyn_cast<ExprWithCleanups>(E))
      return Cleanup->getNumObjects() == 0 && Cleanup->getSubExpr() &&
             A.Context.hasSameType(Cleanup->getType(), Cleanup->getSubExpr()->getType()) &&
             Cleanup->getValueKind() == Cleanup->getSubExpr()->getValueKind() &&
             Self(Self, Cleanup->getSubExpr(), Depth + 1);
    return isa<ImplicitValueInitExpr>(E) &&
           !A.Context.getBaseElementType(E->getType())->isRecordType();
  };
  return Check(Check, Source.Root, 0);
}

static bool operationTraitNeedsExceptionSource(TypeTrait Trait) {
  return Trait == TT_IsNothrowConstructible || Trait == BTT_IsNothrowAssignable ||
         Trait == BTT_IsNothrowConvertible;
}

static unsigned metadataTypeClassificationArity(TypeTrait Trait) {
  switch (Trait) {
  case UTT_IsArithmetic: case UTT_IsFloatingPoint: case UTT_IsIntegral:
  case UTT_IsVoid: case UTT_IsArray: case UTT_IsFunction:
  case UTT_IsReference: case UTT_IsLvalueReference: case UTT_IsRvalueReference:
  case UTT_IsFundamental: case UTT_IsObject: case UTT_IsScalar:
  case UTT_IsCompound: case UTT_IsPointer: case UTT_IsMemberObjectPointer:
  case UTT_IsMemberFunctionPointer: case UTT_IsMemberPointer:
  case UTT_IsConst: case UTT_IsVolatile: case UTT_IsSigned: case UTT_IsUnsigned:
  case UTT_IsEnum: case UTT_IsClass: case UTT_IsUnion:
  case UTT_IsAggregate: case UTT_IsEmpty: case UTT_IsStandardLayout:
  case UTT_IsTrivial: case UTT_IsTriviallyCopyable: case UTT_IsPOD:
  case UTT_IsPolymorphic: case UTT_IsAbstract:
  case UTT_IsFinal: case UTT_IsLiteral: case UTT_HasUniqueObjectRepresentations:
  // These inspect deletion/access without resolving a destructor's noexcept.
  case UTT_IsDestructible: case UTT_IsTriviallyDestructible:
    return 1;
  case BTT_IsSame: case BTT_IsBaseOf:
    return 2;
  default:
    return 0;
  }
}

bool Adapter::typeClassificationValue(const TypeTraitExpr *Query) {
  const auto L = Query->getExprLoc();
  unsigned Arity = metadataTypeClassificationArity(Query->getTrait());
  bool OperationTrait = false;
  switch (Query->getTrait()) {
  case UTT_IsNothrowDestructible:
    Arity = 1;
    OperationTrait = true;
    break;
  case BTT_IsAssignable: case BTT_IsNothrowAssignable:
  case BTT_IsTriviallyAssignable: case BTT_IsConvertible:
  case BTT_IsConvertibleTo: case BTT_IsNothrowConvertible:
    Arity = 2;
    OperationTrait = true;
    break;
  case TT_IsConstructible: case TT_IsNothrowConstructible:
  case TT_IsTriviallyConstructible:
    // One destination and at most 64 hypothetical constructor arguments.
    if (Query->getNumArgs() <= 65)
      Arity = Query->getNumArgs();
    OperationTrait = true;
    break;
  default:
    break;
  }
  if (!S.coreV2() || !Arity || Query->getNumArgs() != Arity ||
      !Query->isPRValue() || Query->isTypeDependent() ||
      Query->isValueDependent() || Query->isInstantiationDependent() ||
      !Context.hasSameType(Query->getType(), Context.BoolTy)) {
    reject(L, "type classification", "A resolved supported boolean type-classification query with its exact operands is required.");
    throw Failure{};
  }
  chargeExpansion(Arity + 1, L);
  if (OperationTrait && Query->getTrait() != UTT_IsNothrowDestructible) {
    auto Found = OperationTraits.find(Query);
    if (Found == OperationTraits.end()) {
      reject(L, "operation trait source", "An operation query requires its exact retained semantic source.");
      throw Failure{};
    }
    const auto &Source = Found->second;
    const bool Construction = Query->getTrait() == TT_IsConstructible ||
        Query->getTrait() == TT_IsNothrowConstructible ||
        Query->getTrait() == TT_IsTriviallyConstructible;
    const bool Conversion = Query->getTrait() == BTT_IsConvertible ||
        Query->getTrait() == BTT_IsConvertibleTo ||
        Query->getTrait() == BTT_IsNothrowConvertible;
    const unsigned Count = Construction ? Arity - 1 : Conversion ? 1 : 2;
    if ((Source.Complete && (!Source.Attempted || !Source.Root)) ||
        (!Source.Attempted && (Source.Root || !Source.Operands.empty())) ||
        (Source.Attempted && Source.Operands.size() != Count)) {
      reject(L, "operation trait source", "Retained operation status and operand count must agree.");
      throw Failure{};
    }
    for (unsigned I = 0; I < Source.Operands.size(); ++I) {
      const auto *Operand = dyn_cast_or_null<OpaqueValueExpr>(Source.Operands[I]);
      QualType T = Query->getArg(I + (Construction ? 1 : 0))->getType();
      if (T->isObjectType() || T->isFunctionType())
        T = Context.getRValueReferenceType(T);
      if (!Operand || Operand->getSourceExpr() ||
          !Context.hasSameType(Operand->getType(), T.getNonLValueExprType(Context)) ||
          Operand->getValueKind() != Expr::getValueKindForType(T)) {
        reject(L, "operation trait source", "Synthetic operands must retain their exact queried type and value category.");
        throw Failure{};
      }
    }
    // Read after Sema's helper/allocator lifetime has ended. The producer owns
    // these nodes in ASTContext, even when trivial/nothrow queries return false.
    if (Source.Root && (Source.Root->isTypeDependent() ||
                       Source.Root->isValueDependent() ||
                       Source.Root->isInstantiationDependent())) {
      reject(L, "operation trait source", "An operation root must be concrete.");
      throw Failure{};
    }
  }
  bool RecordOperand = false;
  for (const auto *Argument : Query->getArgs()) {
    if (!Argument) {
      reject(L, "type classification source", "Every classified type requires its resolved written type source.");
      throw Failure{};
    }
    checkQueryType(Argument->getType(), L);
    RecordOperand |= Context.getBaseElementType(
        Argument->getType().getNonReferenceType())->isRecordType();
  }
  // Pinned Sema returns true for a reference before LookupDestructor and
  // ResolveExceptionSpec. The referred type and its written source are still
  // checked above and by RAV; no destructor selection can be borrowed here.
  const bool ReferenceDestruction = Query->getTrait() == UTT_IsNothrowDestructible &&
      Query->getArg(0)->getType()->isReferenceType();
  if (OperationTrait && RecordOperand && !ReferenceDestruction) {
    const auto Kind = Query->getTrait();
    auto Found = OperationTraits.find(Query);
    if (Kind == UTT_IsNothrowDestructible || Found == OperationTraits.end()) {
      reject(L, "operation trait source",
             "Record-value destruction requires retained selected source and exception dependencies.");
      throw Failure{};
    }
    if (!VerifiedOperationQueries.count(Query) &&
        !operationTraitSource(*this, Found->second, nullptr,
                              operationTraitNeedsExceptionSource(Kind))) {
      if (CheckingSource && Found->second.Attempted &&
          Found->second.Complete && Found->second.Root) {
        if (DeferredOperationQueries.insert(Query).second)
          PendingOperationQueries.push_back(Query);
      } else {
        reject(L, "operation trait source",
               "Record queries require a complete operation with checked selected source or a checked pre-operation result.");
        throw Failure{};
      }
    }
  }
  // RAV separately visits every TypeSourceInfo, including decltype operands,
  // array bounds, noexcept specifications and template substitution sources.
  // Preserve Clang's source identity: enums/references/noexcept can share IR
  // carriers while remaining different inputs to these predicates.
  return Query->getValue();
}

uint64_t Adapter::arrayTypeQueryValue(const ArrayTypeTraitExpr *Query) {
  const auto L = Query->getExprLoc();
  const bool Rank = Query->getTrait() == ATT_ArrayRank;
  const auto *Dimension = Query->getDimensionExpression();
  if (!S.coreV2() || (!Rank && Query->getTrait() != ATT_ArrayExtent) ||
      !Query->isPRValue() || Query->isTypeDependent() ||
      Query->isValueDependent() || Query->isInstantiationDependent() ||
      !Context.hasSameType(Query->getType(), Context.getSizeType()) ||
      !Query->getQueriedTypeSourceInfo() || (Rank ? Dimension != nullptr : !Dimension)) {
    reject(L, "array type query", "A resolved rank or extent query requires its written type, exact dimension and native size_t result.");
    throw Failure{};
  }
  chargeExpansion(2, L);
  checkQueryType(Query->getQueriedType(), L);
  if (Dimension) {
    APValue Value;
    if (Dimension->getType().isNull() ||
        !Dimension->getType()->isIntegralOrUnscopedEnumerationType() ||
        Dimension->isTypeDependent() || Dimension->isValueDependent() ||
        Dimension->isInstantiationDependent() ||
        !Dimension->isCXX11ConstantExpr(Context, &Value) || !Value.isInt() ||
        (Value.getInt().isSigned() && Value.getInt().isNegative())) {
      reject(L, "array query dimension", "An extent query requires a nonnegative constant integer dimension.");
      throw Failure{};
    }
    if (type(Dimension->getType(), L).empty())
      throw Failure{};
  }
  // Both operands are inspected by Allowlist, including the dimension omitted
  // from pinned RAV's default traversal. Neither creates runtime instructions.
  return Query->getValue();
}

bool Adapter::emptyBaseChainShape(const CXXRecordDecl *Record) {
  std::set<const CXXRecordDecl *> Seen;
  while (Record) {
    Record = Record->getDefinition();
    if (!S.coreV2() || !Record || Record->isDependentContext() ||
        !S.owns(Sources, Record->getLocation()) || Record->isInvalidDecl() ||
        hasNonFinalAttributes(Record) || Record->isUnion() || !Record->field_empty() ||
        !Record->isStandardLayout() || Record->isDynamicClass() ||
        Record->getNumBases() > 1 || !Record->isTriviallyCopyable() ||
        !Record->hasTrivialDefaultConstructor() || !Record->hasTrivialDestructor() ||
        Seen.size() >= 64 || !Seen.insert(Record->getCanonicalDecl()).second)
      return false;
    chargeExpansion(1, Record->getLocation());
    // The first increment has no nontrivial base lifecycle helpers. Ordinary
    // methods/conversions may have effects; selected constructors may not.
    for (const auto *Constructor : Record->ctors())
      if (!Constructor->isDeleted() && !Constructor->isTrivial())
        return false;
    for (const auto *Declaration : Record->decls())
      if (const auto *Template = dyn_cast<FunctionTemplateDecl>(Declaration))
        if (const auto *Constructor = dyn_cast<CXXConstructorDecl>(Template->getTemplatedDecl());
            Constructor && !Constructor->isDeleted())
          return false; // Constructor templates are not included in ctors().
    const auto &Layout = Context.getASTRecordLayout(Record);
    if (Layout.getSize().getQuantity() != 1 ||
        Layout.getAlignment().getQuantity() != 1)
      return false;
    if (!Record->getNumBases())
      return true;
    const auto &Base = *Record->bases_begin();
    const auto *Next = Base.getType()->getAsCXXRecordDecl();
    if (Base.isVirtual() || Base.isPackExpansion() || !Base.getTypeSourceInfo() ||
        !S.owns(Sources, Base.getBeginLoc()) || !Next || !Next->getDefinition() ||
        !Layout.getBaseClassOffset(Next).isZero())
      return false;
    Record = Next;
  }
  return false;
}

const CheckedEmptyBase *Adapter::emptyBase(const CXXRecordDecl *Record) {
  Record = Record ? Record->getDefinition() : nullptr;
  if (!Record || !Record->getNumBases())
    return nullptr;
  if (!emptyBaseChainShape(Record)) {
    reject(Record->getLocation(), "empty base storage",
           "A base requires a source-owned, trivial, one-byte standard-layout empty single-base chain.");
    throw Failure{};
  }
  const auto &Base = *Record->bases_begin();
  const auto *Canonical = Record->getCanonicalDecl();
  auto Inserted = EmptyBases.try_emplace(Canonical, CheckedEmptyBase{
      Canonical, Base.getType()->getAsCXXRecordDecl()->getCanonicalDecl(),
      &Base, Base.getTypeSourceInfo(), "nct_base_storage"});
  return &Inserted.first->second;
}

std::vector<const CheckedEmptyBase *> Adapter::emptyBaseCast(const CastExpr *Cast) {
  auto L = Cast->getExprLoc();
  auto Reject = [&]() {
    reject(L, "empty base conversion", "The conversion must retain its exact nonvirtual empty first-member chain and source qualifiers.");
    throw Failure{};
  };
  const bool Down = Cast->getCastKind() == CK_BaseToDerived;
  if (!S.coreV2() || (!Down && Cast->getCastKind() != CK_DerivedToBase &&
                     Cast->getCastKind() != CK_UncheckedDerivedToBase) ||
      !Cast->getSubExpr() || Cast->isTypeDependent() || Cast->isValueDependent() ||
      Cast->isInstantiationDependent() || Cast->path_empty())
    Reject();
  auto From = Cast->getSubExpr()->getType(), To = Cast->getType();
  if (From->isPointerType() != To->isPointerType())
    Reject();
  if (From->isPointerType()) {
    From = From->getPointeeType();
    To = To->getPointeeType();
  } else if (!Cast->isGLValue() || !Cast->getSubExpr()->isGLValue()) {
    Reject();
  }
  if ((From.isConstQualified() && !To.isConstQualified()) ||
      type(From, L).empty() || type(To, L).empty())
    Reject();
  const auto *Current = (Down ? To : From)->getAsCXXRecordDecl();
  const auto *Target = (Down ? From : To)->getAsCXXRecordDecl();
  if (!Current || !Target)
    Reject();
  std::vector<const CheckedEmptyBase *> Path;
  for (const auto *Step : Cast->path()) {
    const auto *Base = emptyBase(Current);
    if (!Base || Step != Base->Specifier || Path.size() >= 64)
      Reject();
    Path.push_back(Base);
    Current = Base->Base;
  }
  if (Current->getCanonicalDecl() != Target->getCanonicalDecl())
    Reject();
  return Path;
}

bool Adapter::functionAddressTarget(const FunctionDecl *F, SourceLocation L) {
  if (F && allocationOperatorKind(F->getOverloadedOperator()))
    if (const auto *Definition = F->getDefinition())
      F = Definition;
  const auto *Method = dyn_cast_or_null<CXXMethodDecl>(F);
  // Classification alone does not admit a template source use: Allowlist still
  // checks the selected declaration, deduction source, primary and actual body.
  const bool ConcreteTemplate = concreteFunctionTemplate(F);
  if (!S.coreV2() || !F || F->isInvalidDecl() || !supportedDeclarationAttributes(F) ||
      F->isImplicit() || F->isMain() || F->getBuiltinID() ||
      !S.owns(Sources, F->getLocation()) || F->isDeletedAsWritten() ||
      F->isDefaulted() || F->isConsteval() ||
      F->getDescribedFunctionTemplate() ||
      (F->getPrimaryTemplate() && !ConcreteTemplate) ||
      (F->getTemplatedKind() != FunctionDecl::TK_NonTemplate &&
       !ConcreteTemplate && !concreteMemberFunction(F) &&
       !concreteFriendFunction(F)) ||
      (Method && (!Method->isStatic() ||
                  (!ordinaryMethod(Method) && !ordinaryOperator(Method))))) {
    reject(L, "function address", "An ordinary source-owned free function or static method is required.");
    return false;
  }
  if (functionPointerType(Context.getPointerType(F->getType()), L).empty())
    return false;
  const auto *Definition = F->getDefinition();
  if (!Definition || !S.owns(Sources, Definition->getLocation())) {
    reject(L, "function address definition", "A named callback requires its definition in this source unit.", "TR0203");
    return false;
  }
  return true;
}

std::string Adapter::nativeHeapImport(const FunctionDecl *F, SourceLocation L) {
  if (!S.coreV2() || !nativeHeapDeclaration(F) || F->getDefinition())
    return {};
  for (const auto *Declaration : F->redecls())
    if (!S.owns(Sources, Declaration->getLocation()) ||
        nativeHeapDeclaration(Declaration) != nativeHeapDeclaration(F) ||
        !supportedDeclarationAttributes(Declaration))
      return {};
  const auto &Target = Context.getTargetInfo();
  const auto &Triple = Target.getTriple();
  const auto Size = Context.getSizeType();
  if ((!Triple.isMacOSX() && !(Triple.isOSLinux() && !Triple.isAndroid()) &&
       !Triple.isKnownWindowsMSVCEnvironment() && !Triple.isWindowsGNUEnvironment()) ||
      !Size->isUnsignedIntegerType() ||
      Context.getTypeSize(Size) != Context.getTypeSize(Context.VoidPtrTy) ||
      Context.getTypeAlign(Size) != Context.getTypeAlign(Context.VoidPtrTy)) {
    reject(L, "native heap ABI", "Native heap declarations require a checked hosted C ABI and native size_t layout.", "TR0204");
    throw Failure{};
  }
  return F->getName().str();
}

// Only Clang's untouched implicit global sized delete has the standard default
// forwarding body. A written declaration, including a later redeclaration,
// cannot be given an inferred implementation.
static bool implicitSizedDeallocation(const FunctionDecl *F, ASTContext &Context,
                                      OverloadedOperatorKind Operator) {
  if (!F || F->getDefinition() ||
      (Operator != OO_Delete && Operator != OO_Array_Delete))
    return false;
  for (const auto *D : F->redecls()) {
    const auto *Prototype = D->getType()->getAs<FunctionProtoType>();
    if (!D->isImplicit() || D->isInvalidDecl() || D->getLocation().isValid() ||
        D->getBeginLoc().isValid() || D->getEndLoc().isValid() ||
        D->getTypeSourceInfo() || !D->getDeclContext()->isTranslationUnit() ||
        isa<CXXMethodDecl>(D) || D->getTemplatedKind() != FunctionDecl::TK_NonTemplate ||
        D->getOverloadedOperator() != Operator ||
        !D->isReplaceableGlobalAllocationFunction() ||
        !ordinaryCallbackPrototype(Prototype) ||
        Prototype->getExceptionSpecType() != EST_BasicNoexcept ||
        D->getNumParams() != 2 ||
        !Context.hasSameType(D->getReturnType(), Context.VoidTy) ||
        !Context.hasSameType(D->getParamDecl(0)->getType(), Context.VoidPtrTy) ||
        !Context.hasSameType(D->getParamDecl(1)->getType(), Context.getSizeType()))
      return false;
    for (const auto *Attribute : D->attrs()) {
      const auto *Visibility = dyn_cast<VisibilityAttr>(Attribute);
      if (!Visibility || !Visibility->isImplicit() ||
          Visibility->getRange().isValid() ||
          Visibility->getVisibility() != VisibilityAttr::Default)
        return false;
    }
    for (const auto *P : D->parameters())
      if (!P->isImplicit() || P->isInvalidDecl() || P->getLocation().isValid() ||
          P->getBeginLoc().isValid() || P->getEndLoc().isValid() ||
          P->getTypeSourceInfo() || P->hasAttrs() || P->hasDefaultArg())
        return false;
  }
  return true;
}

const FunctionDecl *Adapter::allocationFunction(const FunctionDecl *F,
                                               bool Allocate, SourceLocation L,
                                               bool Array) {
  const auto Operator = Allocate ? (Array ? OO_Array_New : OO_New)
                                 : (Array ? OO_Array_Delete : OO_Delete);
  const auto *Definition = F ? F->getDefinition() : nullptr;
  if (S.coreV2() && !Allocate &&
      implicitSizedDeallocation(F, Context, Operator)) {
    for (const auto *Declaration : Context.getTranslationUnitDecl()->lookup(
             Context.DeclarationNames.getCXXOperatorName(Operator))) {
      const auto *Candidate = dyn_cast<FunctionDecl>(Declaration);
      if (!Candidate || isa<CXXMethodDecl>(Candidate) || Candidate->isVariadic() ||
          Candidate->getTemplatedKind() != FunctionDecl::TK_NonTemplate ||
          Candidate->getNumParams() != 1 ||
          !Context.hasSameType(Candidate->getReturnType(), Context.VoidTy) ||
          !Context.hasSameUnqualifiedType(Candidate->getParamDecl(0)->getType(),
                                          Context.VoidPtrTy))
        continue;
      const auto *Body = Candidate->getDefinition();
      // Namespace definitions are visited by the ordinary owned-source walk.
      // Do not instantiate or borrow a class-template friend's body here.
      if (!Body || !Body->getDeclContext()->getRedeclContext()->isTranslationUnit() ||
          !Body->getLexicalDeclContext()->getRedeclContext()->isTranslationUnit() ||
          Body->getTemplatedKind() != FunctionDecl::TK_NonTemplate ||
          !ordinaryCallbackPrototype(Body->getType()->getAs<FunctionProtoType>()))
        continue;
      if (Definition && Definition->getCanonicalDecl() != Body->getCanonicalDecl()) {
        reject(L, "default sized deallocation",
               "A unique source-defined matching unsized operator is required.", "TR0203");
        throw Failure{};
      }
      Definition = Body;
    }
  }
  if (!Definition || !S.owns(Sources, Definition->getLocation())) {
    reject(L, Allocate ? "allocation definition" : "deallocation definition",
           "The selected allocation function requires its definition in this source unit.", "TR0203");
    throw Failure{};
  }
  if (!S.coreV2() || Definition->getOverloadedOperator() != Operator ||
      !ordinaryOperator(Definition) || !supportedDeclarationAttributes(Definition) ||
      Definition->isReservedGlobalPlacementOperator() ||
      Definition->isDestroyingOperatorDelete() || !Definition->getNumParams() ||
      !Context.hasSameUnqualifiedType(Definition->getReturnType(),
                                      Allocate ? Context.VoidPtrTy : Context.VoidTy) ||
      !Context.hasSameUnqualifiedType(Definition->getParamDecl(0)->getType(),
                                      Allocate ? Context.getSizeType() : Context.VoidPtrTy)) {
    reject(L, Allocate ? "allocation function" : "deallocation function",
           "An admitted source-defined matching allocation operator is required.");
    throw Failure{};
  }
  type(Definition->getReturnType(), L, true);
  for (const auto *P : Definition->parameters())
    type(P->getType(), L);
  return Definition;
}

ArrayAllocationLayout Adapter::arrayAllocationLayout(
    QualType Object, bool UsualDeleteWantsSize, SourceLocation L) {
  const auto &Target = Context.getTargetInfo();
  const auto &Triple = Target.getTriple();
  const auto ABI = Context.getCXXABIKind();
  const bool Microsoft = Triple.isKnownWindowsMSVCEnvironment() &&
                         ABI == TargetCXXABI::Microsoft;
  const bool Apple = Triple.isMacOSX() && Triple.getArch() == llvm::Triple::aarch64 &&
                     ABI == TargetCXXABI::AppleARM64;
  const bool Itanium = (Triple.isMacOSX() ||
                        (Triple.isOSLinux() && !Triple.isAndroid()) ||
                        Triple.isWindowsGNUEnvironment()) &&
                       !(Triple.isMacOSX() && Triple.getArch() == llvm::Triple::aarch64) &&
                       (ABI == TargetCXXABI::GenericItanium || ABI == TargetCXXABI::GenericAArch64);
  const auto Size = Context.getSizeType();
  if ((!Microsoft && !Apple && !Itanium) || !Size->isUnsignedIntegerType() ||
      Context.getTypeSize(Size) != Context.getTypeSize(Context.VoidPtrTy) ||
      Context.getTypeAlign(Size) != Context.getTypeAlign(Context.VoidPtrTy)) {
    reject(L, "array cookie ABI", "Array allocation requires a checked native C++ ABI and size_t layout.", "TR0204");
    throw Failure{};
  }
  S.Module["array_cookie_abi"] = Microsoft ? "msvc" : Apple ? "apple-arm64" : "itanium";
  auto Element = Context.getBaseElementType(Object).getUnqualifiedType();
  ArrayAllocationLayout Result;
  Result.Element = Element;
  Result.ElementBytes = Context.getTypeSizeInChars(Element).getQuantity();
  if (!needsDestruction(Object) && (Microsoft || !UsualDeleteWantsSize))
    return Result;
  const uint64_t Word = Context.getTypeSizeInChars(Size).getQuantity();
  const uint64_t Alignment = (Itanium ? Context.getPreferredTypeAlignInChars(Element)
                                     : Context.getTypeAlignInChars(Element)).getQuantity();
  Result.CookieBytes = std::max((Apple ? 2 : 1) * Word, Alignment);
  Result.CountOffset = Apple ? Word : Microsoft ? 0 : Result.CookieBytes - Word;
  Result.StoresElementSize = Apple;
  return Result;
}

bool omittedDefaultConstruction(const Expr *Init, QualType Element, ASTContext &Context) {
  const auto *Record = Element->getAsCXXRecordDecl();
  if (!Init || !Record)
    return false;
  while (true) {
    Init = Init->IgnoreParens();
    if (const auto *W = dyn_cast<ExprWithCleanups>(Init)) {
      Init = W->getSubExpr();
    } else if (const auto *B = dyn_cast<CXXBindTemporaryExpr>(Init)) {
      Init = B->getSubExpr();
    } else if (const auto *C = dyn_cast<ConstantExpr>(Init)) {
      Init = C->getSubExpr();
    } else if (const auto *M = dyn_cast<MaterializeTemporaryExpr>(Init);
               M && fullExpressionTemporary(M, Context)) {
      Init = M->getSubExpr();
    } else if (const auto *C = dyn_cast<CastExpr>(Init);
               C && C->getCastKind() == CK_ConstructorConversion) {
      Init = constructorConversion(C, Context);
      if (!Init)
        return false;
    } else if (const auto *C = dyn_cast<CastExpr>(Init);
               C && C->getCastKind() == CK_NoOp && C->isPRValue() &&
               Context.hasSameUnqualifiedType(C->getType(), C->getSubExpr()->getType())) {
      Init = C->getSubExpr();
    } else {
      const auto *Construct = dyn_cast<CXXConstructExpr>(Init);
      return Construct && Construct->getConstructor()->isDefaultConstructor() &&
             Construct->getConstructor()->getParent()->getCanonicalDecl() ==
                 Record->getCanonicalDecl();
    }
  }
}

static bool repeatedAggregateInitializer(const Expr *Init, QualType Destination,
                                         Adapter &A, unsigned Depth = 0) {
  if (!Init || Depth > 64)
    return false;
  A.chargeExpansion(1, Init->getExprLoc());
  // A null Destination denotes evaluation/binding, where aggregate prvalues
  // would create separate automatic result storage. Lists are safe only when
  // initialize() writes them into the actual allocated object or its fields.
  if (const auto *P = dyn_cast<ParenExpr>(Init))
    return repeatedAggregateInitializer(P->getSubExpr(), Destination, A, Depth + 1);
  if (const auto *W = dyn_cast<ExprWithCleanups>(Init))
    return !W->getNumObjects() &&
           repeatedAggregateInitializer(W->getSubExpr(), Destination, A, Depth + 1);
  if (const auto *C = dyn_cast<ConstantExpr>(Init))
    return repeatedAggregateInitializer(C->getSubExpr(), Destination, A, Depth + 1);
  if (const auto *D = dyn_cast<CXXDefaultInitExpr>(Init))
    return repeatedAggregateInitializer(D->getExpr(), Destination, A, Depth + 1);
  if (const auto *D = dyn_cast<CXXDefaultArgExpr>(Init))
    return repeatedAggregateInitializer(selectedDefaultArgument(D, A.Context),
                                        Destination, A, Depth + 1);
  if (Init->getType().isNull() || Init->isInstantiationDependent() ||
      isa<MaterializeTemporaryExpr, CXXBindTemporaryExpr, OpaqueValueExpr>(Init))
    return false;
  if (!Destination.isNull() && Destination->isReferenceType())
    Destination = {}; // bind() must designate already-live storage.
  const bool AggregateDestination = !Destination.isNull() &&
      (Destination->isRecordType() || Destination->isArrayType());
  if (AggregateDestination) {
    if (!A.Context.hasSameUnqualifiedType(Destination, Init->getType()))
      return false;
    if (isa<ImplicitValueInitExpr>(Init))
      return true;
    if (Destination->isArrayType() && isa<StringLiteral>(Init))
      return true;
    const auto *List = dyn_cast<InitListExpr>(Init);
    if (!List)
      return false; // Constructed subobjects and record-return calls need a later proof.
    if (List->isSyntacticForm() && List->getSemanticForm())
      List = List->getSemanticForm();
    if (const auto *Array = A.Context.getAsConstantArrayType(Destination)) {
      for (const auto *Element : List->inits())
        if (!repeatedAggregateInitializer(Element, Array->getElementType(), A, Depth + 1))
          return false;
      return !List->getArrayFiller() || repeatedAggregateInitializer(
          List->getArrayFiller(), Array->getElementType(), A, Depth + 1);
    }
    const auto *Record = Destination->getAsCXXRecordDecl();
    if (!Record || !(Record = Record->getDefinition()) ||
        List->getNumInits() != Record->getNumBases() +
            std::distance(Record->field_begin(), Record->field_end()))
      return false;
    unsigned Index = 0;
    if (const auto *Base = A.emptyBase(Record))
      if (!repeatedAggregateInitializer(List->getInit(Index++),
                                        A.Context.getRecordType(Base->Base), A, Depth + 1))
        return false;
    for (const auto *Field : Record->fields())
      if (!repeatedAggregateInitializer(List->getInit(Index++), Field->getType(), A, Depth + 1))
        return false;
    return true;
  }
  // Inspect every evaluated child, even when Clang can fold the final scalar.
  // In particular, discarded R{} still materializes outside a destination role.
  if (Init->isPRValue() && (Init->getType()->isRecordType() || Init->getType()->isArrayType()))
    return false;
  if (const auto *Call = dyn_cast<CallExpr>(Init)) {
    const auto *Function = Call->getDirectCallee();
    auto Type = Function ? Function->getType() : Call->getCallee()->getType();
    if (Type->isPointerType())
      Type = Type->getPointeeType();
    const auto *Prototype = Type->getAs<FunctionProtoType>();
    if (!Prototype)
      return false;
    // Caller-owned by-value record arguments use separate storage. Keep them
    // outside this first proof even when the callee ends their lifetime.
    for (auto Parameter : Prototype->param_types())
      if (Parameter->isRecordType())
        return false;
  }
  if (const auto *List = dyn_cast<InitListExpr>(Init)) {
    if (List->getArrayFiller())
      return false;
  }
  for (const auto *Child : Init->children()) {
    if (!Child)
      continue;
    const auto *Expression = dyn_cast<Expr>(Child);
    if (!Expression || !repeatedAggregateInitializer(Expression, {}, A, Depth + 1))
      return false;
  }
  return true;
}

ArrayNewInfo Adapter::arrayNewInfo(const CXXNewExpr *N) {
  const auto L = N->getExprLoc();
  const auto Bound = N->getArraySize();
  if (!Bound) {
    reject(L, "new array extent", "A checked outer array bound is required.");
    throw Failure{};
  }
  ArrayNewInfo Result;
  const Expr *Converted = *Bound;
  while (true) {
    Converted = Converted->IgnoreParens();
    if (const auto *Wrapper = dyn_cast<ExprWithCleanups>(Converted))
      Converted = Wrapper->getSubExpr();
    else if (const auto *Constant = dyn_cast<ConstantExpr>(Converted))
      Converted = Constant->getSubExpr();
    else
      break;
  }
  Result.BoundBeforeConversion = Converted;
  if (const auto *Cast = dyn_cast<ImplicitCastExpr>(Converted);
      Cast && Cast->getCastKind() == CK_IntegralCast &&
      Context.hasSameUnqualifiedType(Cast->getType(), Context.getSizeType()))
    Result.BoundBeforeConversion = Cast->getSubExpr();
  auto Value = (*Bound)->getIntegerConstantExpr(Context);
  if (Value) {
    // The final unsigned conversion may erase a negative wide bound. Positive
    // wide values retain normal conversion semantics, including truncation.
    auto Before = Result.BoundBeforeConversion->getIntegerConstantExpr(Context);
    if ((Before && Before->isSigned() && Before->isNegative()) ||
        (Value->isSigned() && Value->isNegative()) || Value->getLimitedValue(65537) > 65536) {
      reject(L, "new array extent", "Constant array bounds must be nonnegative and within the expansion limit.");
      throw Failure{};
    }
    Result.Count = Value->getZExtValue();
  } else {
    const auto *Function = N->getOperatorNew();
    const auto *Prototype = Function->getType()->getAs<FunctionProtoType>();
    if (!isa<CXXMethodDecl>(Function) || N->getNumPlacementArgs() ||
        !Prototype || !Prototype->isNothrow() || !N->shouldNullCheckAllocation()) {
      reject(L, "runtime new array extent", "Runtime array bounds require a source-owned nonthrowing class allocator without placement arguments; throwing length errors require exception runtime support.");
      throw Failure{};
    }
  }
  const auto Object = N->getAllocatedType();
  const auto Units = storageUnits(Object);
  if (!Units || Units > 200000 || (Result.Count && *Result.Count && Units > 200000 / *Result.Count)) {
    reject(L, "new array storage", "Array initialization exceeds the storage expansion limit.");
    throw Failure{};
  }
  const auto Layout = arrayAllocationLayout(Object, N->doesUsualArrayDeleteWantSize(), L);
  const uint64_t Bytes = Context.getTypeSizeInChars(Object).getQuantity();
  const auto Maximum = llvm::APInt::getMaxValue(Context.getTypeSize(Context.getSizeType())).getZExtValue();
  if (!Bytes || Layout.CookieBytes > Maximum ||
      (Result.Count && *Result.Count > (Maximum - Layout.CookieBytes) / Bytes)) {
    reject(L, "new array size", "Array allocation bytes and cookie must fit native size_t.");
    throw Failure{};
  }
  const Expr *Init = N->getInitializer();
  while (Init) {
    const auto *Array = Init->getType().isNull() ? nullptr : Context.getAsArrayType(Init->getType());
    const auto *Constant = dyn_cast_or_null<ConstantArrayType>(Array);
    const auto *List = dyn_cast<InitListExpr>(Init);
    if (List && List->isSyntacticForm() && List->getSemanticForm())
      List = List->getSemanticForm();
    const uint64_t Extent = Constant ? Constant->getSize().getLimitedValue() : 0;
    bool Shape = Array && Context.hasSameUnqualifiedType(Array->getElementType(), Object);
    if (Result.Count)
      Shape &= Constant && (isa<StringLiteral>(Init) ? Extent <= *Result.Count : Extent == *Result.Count);
    else if (const auto *Wrapper = dyn_cast<ExprWithCleanups>(Init))
      Shape &= Context.hasSameType(Init->getType(), Wrapper->getSubExpr()->getType());
    else
      Shape &= (List && Constant && Extent == List->getNumInits()) ||
               (!List && isa_and_nonnull<IncompleteArrayType>(Array));
    if (!Shape) {
      reject(L, "new array initializer", "The exact semantic array initializer must match its checked element type and constant extent or runtime prefix.");
      throw Failure{};
    }
    if (const auto *Wrapper = dyn_cast<ExprWithCleanups>(Init)) {
      Init = Wrapper->getSubExpr();
      continue;
    }
    Result.Initializer = List ? List : Init;
    if (List) {
      Result.PrefixCount = List->getNumInits();
      if (Result.Count && !List->isStringLiteralInit() && Result.PrefixCount > *Result.Count) {
        reject(L, "new array initializer", "Too many array initializer clauses.");
        throw Failure{};
      }
    }
    break;
  }
  if (Result.Count || !Result.Initializer)
    return Result;
  if (Result.PrefixCount > 65536 ||
      (Result.PrefixCount && Units > 200000 / Result.PrefixCount)) {
    reject(L, "new array initializer", "Explicit array clauses exceed the storage expansion limit.");
    throw Failure{};
  }
  const auto *List = dyn_cast<InitListExpr>(Result.Initializer);
  Result.Filler = List ? List->getArrayFiller() : Result.Initializer;
  if (List && Result.Filler &&
      !Context.hasSameUnqualifiedType(Result.Filler->getType(), Object)) {
    reject(L, "runtime array initializer", "The repeated filler must initialize the exact allocated element type.");
    throw Failure{};
  }
  if (Result.Filler && isa<ImplicitValueInitExpr>(Result.Filler)) {
    Result.Repeated = RuntimeArrayInitialization::Zero;
    return Result;
  }
  const bool Default = List
      ? omittedDefaultConstruction(Result.Filler, Object, Context)
      : omittedDefaultConstruction(Result.Filler, Context.getBaseElementType(Object), Context);
  if (Default) {
    Result.Repeated = RuntimeArrayInitialization::DefaultConstruction;
    return Result;
  }
  if (List && repeatedAggregateInitializer(Result.Filler, Object, *this)) {
    Result.Repeated = RuntimeArrayInitialization::Aggregate;
    return Result;
  }
  // The same classification runs before source erasure and before lowering.
  // Explicit clauses have bounded fresh storage; repeated aggregate fillers
  // can create distinct temporaries that all outlive the construction loop.
  reject(L, "runtime array initializer", "Repeated elements require direct default construction, zero initialization or aggregate initialization without separate temporary storage.");
  throw Failure{};
}

json::Object Adapter::functionAddress(const FunctionDecl *F, SourceLocation L) {
  if (!functionAddressTarget(F, L))
    throw Failure{};
  return json::Object{{"kind", "function_address"},
                      {"type", functionPointerType(Context.getPointerType(F->getType()), L)},
                      {"name", name(F)}, {"loc", loc(L)}};
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
  if (S.coreV2() && C->isFunctionPointerType())
    return functionPointerType(T, L, Depth);
  if (S.coreV2() && (C->isPointerType() || C->isReferenceType())) {
    QualType Pointee = C->getPointeeType();
    if (T.getAddressSpace() != LangAS::Default ||
        Pointee.getAddressSpace() != LangAS::Default) {
      reject(L, "pointer address space", "Pointers and reference carriers require the default address space.");
      return {};
    }
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
    case BuiltinType::NullPtr:
      if (S.coreV2()) {
        if (Context.getTypeSize(T) != Context.getTypeSize(Context.VoidPtrTy) ||
            Context.getTypeAlign(T) != Context.getTypeAlign(Context.VoidPtrTy)) {
          reject(L, "nullptr layout", "Source nullptr and default carrier layouts differ.", "TR0204");
          return {};
        }
        return "nullptr";
      }
      break;
    case BuiltinType::Double:
      if (S.math() || S.coreV2())
        return "double";
      break;
    case BuiltinType::Float:
      if (S.coreV2())
        return "float";
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
  if (Kind == "float")
    return floatingLiteral(llvm::APFloat::getZero(llvm::APFloat::IEEEsingle()), L);
  if (T->isPointerType() || T->isReferenceType() || T->isNullPtrType())
    return json::Object{{"kind", "null"}, {"type", Kind}, {"loc", loc(L)}};
  if (const auto *R = T->getAsCXXRecordDecl()) {
    json::Array Args;
    if (const auto *Base = emptyBase(R))
      Args.push_back(zero(Context.getRecordType(Base->Base), L));
    for (const auto *F : R->getDefinition()->fields())
      Args.push_back(zero(F->getType(), L));
    return json::Object{{"kind", "aggregate"},
                        {"type", Kind},
                        {"args", std::move(Args)},
                        {"loc", loc(L)}};
  }
  return literal(llvm::APSInt(integerBits(Kind), unsignedInteger(Kind)), Kind, L);
}

void Adapter::checkStringLiteral(const StringLiteral *Literal) {
  auto L = Literal->getExprLoc();
  const auto *Array = Context.getAsConstantArrayType(Literal->getType());
  if (!S.coreV2() || !Array || Literal->isUnevaluated() || Literal->isPascal() ||
      !Array->getElementType()->isAnyCharacterType() ||
      Context.getTypeSize(Array->getElementType()) != Literal->getCharByteWidth() * 8 ||
      Array->getSize().getLimitedValue() < uint64_t(Literal->getLength()) + 1) {
    reject(L, "string literal", "A checked C++ character array with its terminating zero is required.");
    throw Failure{};
  }
  // Sema adjusts an initializing literal's array type to the destination bound
  // and character type, including signed/unsigned char and trailing zero fill.
  if (type(Literal->getType(), L).empty())
    throw Failure{};
}

json::Object Adapter::stringInitializer(const StringLiteral *Literal) {
  checkStringLiteral(Literal);
  auto L = Literal->getExprLoc();
  const auto *Array = Context.getAsConstantArrayType(Literal->getType());
  auto Count = Array->getSize().getZExtValue();
  auto Element = type(Array->getElementType(), L);
  chargeExpansion(Count + 1, L);
  json::Array Values;
  for (uint64_t I = 0; I < Count; ++I) {
    uint32_t Unit = I < Literal->getLength() ? Literal->getCodeUnit(I) : 0;
    Values.push_back(literal(
        llvm::APSInt(llvm::APInt(integerBits(Element), Unit), unsignedInteger(Element)), Element, L));
  }
  return json::Object{{"kind", "aggregate"}, {"type", type(Literal->getType(), L)},
                      {"args", std::move(Values)}, {"loc", loc(L)}};
}

json::Object Adapter::stringObject(const StringLiteral *Literal) {
  checkStringLiteral(Literal);
  auto L = Literal->getExprLoc();
  auto Found = StringObjects.find(Literal);
  if (Found == StringObjects.end()) {
    auto Name = "nct_string_" + std::to_string(StringObjects.size());
    Found = StringObjects.emplace(Literal, Name).first;
    StringGlobals.push_back(json::Object{
        {"name", Name}, {"type", type(Literal->getType(), L)},
        {"value", stringInitializer(Literal)}, {"loc", loc(L)}});
  }
  return json::Object{{"kind", "var"}, {"type", type(Literal->getType(), L)},
                      {"name", Found->second}, {"loc", loc(L)}};
}

void Adapter::checkConstantTemporaryOccurrences(const VarDecl *Owner) {
  if (!CheckedConstantTemporaryOccurrences.insert(Owner).second)
    return;
  std::map<const MaterializeTemporaryExpr *, unsigned> Occurrences;
  auto Walk = [&](auto &&Self, const Stmt *Node, unsigned Repeats, unsigned Depth) -> void {
    if (!Node || !Repeats)
      return;
    chargeExpansion(1, Owner->getLocation());
    if (Depth > 64) {
      reject(Owner->getLocation(), "constant temporary depth", "Static initialization exceeds the bounded source depth.");
      throw Failure{};
    }
    if (const auto *M = dyn_cast<MaterializeTemporaryExpr>(Node);
        M && M->getStorageDuration() == SD_Static &&
        staticTemporaryOwner(M) == Owner && (Occurrences[M] += Repeats) > 1) {
      // Private semantic filling gives materializing defaults distinct ASTs.
      // Clang retains one APValue per static MTE, so any unexpected sharing
      // still cannot represent separate elements' values and identities.
      reject(M->getExprLoc(), "shared constant temporary",
             "Repeated constant array fillers require distinct temporary identities from the source frontend.");
      throw Failure{};
    }
    if (const auto *D = dyn_cast<CXXDefaultInitExpr>(Node)) {
      Self(Self, D->getExpr(), Repeats, Depth + 1);
      return;
    }
    if (const auto *I = dyn_cast<InitListExpr>(Node)) {
      if (I->isSyntacticForm() && I->getSemanticForm())
        I = I->getSemanticForm();
      for (const auto *Init : I->inits())
        Self(Self, Init, Repeats, Depth + 1);
      if (const auto *Array = Context.getAsConstantArrayType(I->getType())) {
        auto Count = Array->getSize().getLimitedValue(65537);
        if (Count > I->getNumInits())
          Self(Self, I->getArrayFiller(),
               Repeats > 1 || Count - I->getNumInits() > 1 ? 2u : 1u, Depth + 1);
      }
      return;
    }
    for (const auto *Child : Node->children())
      Self(Self, Child, Repeats, Depth + 1);
  };
  Walk(Walk, Owner->getAnyInitializer(), 1, 0);
}

json::Object Adapter::staticTemporaryObject(const MaterializeTemporaryExpr *Temporary) {
  auto L = Temporary->getExprLoc();
  auto T = Temporary->getType();
  const auto *Owner = staticTemporaryOwner(Temporary);
  const VarDecl *InitializingDecl = nullptr;
  if (Owner && !ConstantStaticTemporaryOwners.count(Owner)) {
    // A class member can refer to a later static reference definition before
    // the source walk visits that owner. Clang's native flag is also positive
    // evidence: it is set only by successful constant initialization with no
    // diagnostics, not by the mere presence of a retained temporary APValue.
    const auto *Init = Owner->getAnyInitializer(InitializingDecl);
    if (Init && InitializingDecl && S.owns(Sources, InitializingDecl->getLocation()) &&
        InitializingDecl->getCanonicalDecl() == Owner &&
        InitializingDecl->getDeclContext()->getRedeclContext() ==
            Owner->getDeclContext()->getRedeclContext() &&
        Context.hasSameType(InitializingDecl->getType(), Owner->getType()) &&
        InitializingDecl->hasConstantInitialization())
      ConstantStaticTemporaryOwners.insert(Owner);
  }
  // C++17's checked constant initializer rejects a static MTE whose complete
  // type requires destruction, recursively including array/member subobjects.
  // Do not turn a retained partial value into an unregistered static lifetime.
  if (needsDestruction(T)) {
    reject(L, "static temporary storage", "A nontrivial static temporary requires dynamic lifetime lowering.");
    throw Failure{};
  }
  if (!Owner || DynamicStaticObjects.count(Owner) ||
      !ConstantStaticTemporaryOwners.count(Owner)) {
    reject(L, "static temporary storage",
           "A constant static temporary requires successful evaluation of its exact extending owner.");
    throw Failure{};
  }
  checkConstantTemporaryOccurrences(Owner);
  auto Kind = type(T, L);
  if (Kind.empty())
    throw Failure{};
  auto Found = StaticTemporaryObjects.find(Temporary);
  if (Found == StaticTemporaryObjects.end()) {
    // EvaluateAsInitializer of the owning reference retains this complete
    // temporary's value in Clang. Never evaluate the operand a second time or
    // fold it as an independent expression: self pointers belong to this object.
    const auto *Value = Temporary->getOrCreateValue(false);
    if (!Value || !Value->hasValue()) {
      reject(L, "static temporary initializer",
             "The owning reference must retain a fully defined constant temporary value.");
      throw Failure{};
    }
    chargeExpansion(storageUnits(T) + 1, L);
    auto Name = "nct_static_temporary_" + std::to_string(StaticTemporarySerial++);
    // Register identity before serializing fields so self/cyclic addresses can
    // name the actual object without recursively constructing another copy.
    Found = StaticTemporaryObjects.emplace(Temporary, Name).first;
    json::Object Global{{"name", Name}, {"type", Kind},
                        {"value", constant(*Value, T, L)}, {"loc", loc(L)}};
    if (!T.isConstQualified())
      Global["mutable"] = true;
    StaticTemporaryGlobals.push_back(std::move(Global));
  }
  return json::Object{{"kind", "var"}, {"type", Kind},
                      {"name", Found->second}, {"loc", loc(L)}};
}

json::Object Adapter::dynamicStaticTemporaryObject(const MaterializeTemporaryExpr *Temporary,
                                                  const VarDecl *Owner) {
  auto L = Temporary->getExprLoc();
  auto T = Temporary->getType();
  if (!Owner || staticTemporaryOwner(Temporary) != Owner ||
      !DynamicStaticObjects.count(Owner)) {
    reject(L, "static temporary initialization",
           "A runtime static temporary requires its exact dynamic extending owner.");
    throw Failure{};
  }
  auto Kind = type(T, L);
  if (Kind.empty())
    throw Failure{};
  chargeExpansion(storageUnits(T) + 1, L);
  auto Name = "nct_static_temporary_" + std::to_string(StaticTemporarySerial++);
  // Shared array filler ASTs can evaluate the same materialization for several
  // distinct elements. Each lowered occurrence needs its own permanent object.
  // Failed constant evaluation's partial APValues are never consumed here.
  json::Object Global{{"name", Name}, {"type", Kind}, {"value", zero(T, L)},
                      {"initialization_owner", name(Owner)}, {"loc", loc(L)}};
  if (!T.isConstQualified())
    Global["mutable"] = true;
  if (needsDestruction(T))
    Global["destructor"] = requireStaticDestruction(Name, T, L);
  StaticTemporaryGlobals.push_back(std::move(Global));
  return json::Object{{"kind", "var"}, {"type", Kind},
                      {"name", Name}, {"loc", loc(L)}};
}

json::Object Adapter::constantPointer(const APValue &V, QualType T, SourceLocation L,
                                      bool ReferenceBinding) {
  auto Reject = [&](llvm::StringRef Reason, llvm::StringRef Code = "TR0201") {
    reject(L, "constant object address", Reason, Code);
    throw Failure{};
  };
  auto ResultType = type(T, L);
  if (ResultType.empty() || !S.coreV2() || !T->isPointerType() || T->isFunctionPointerType() ||
      !V.isLValue() || V.getLValueCallIndex())
    Reject("A constant pointer must identify null or permanent source-owned object storage.");
  // Clang assigns a fresh version to each evaluated string literal, including
  // static literals. That version is not an automatic-object lifetime. This
  // translator consistently pools evaluations of the same literal AST node.
  const auto Base = V.getLValueBase();
  if (V.getLValueVersion() &&
      !isa_and_nonnull<StringLiteral>(Base.dyn_cast<const Expr *>()))
    Reject("Only string literal evaluations may carry a permanent-object version.");
  if (V.isNullPointer()) {
    if (ReferenceBinding)
      Reject("A static reference must bind to an existing object, not null.");
    if (!V.getLValueOffset().isZero() || V.isLValueOnePastTheEnd() ||
        (V.hasLValuePath() && !V.getLValuePath().empty()))
      Reject("A null object pointer cannot carry a subobject path or offset.");
    return json::Object{{"kind", "null"}, {"type", ResultType}, {"loc", loc(L)}};
  }
  if (!V.hasLValuePath())
    Reject("A nonnull constant pointer requires its typed source subobject path.");
  QualType Current;
  json::Object Place;
  if (const auto *Variable = dyn_cast_or_null<VarDecl>(Base.dyn_cast<const ValueDecl *>())) {
    const auto *Definition = Variable->getDefinition();
    if (!Definition || !S.owns(Sources, Definition->getLocation()))
      Reject("The addressed variable requires its source-owned definition in this unit.", "TR0203");
    if (!Definition->hasGlobalStorage() || Definition->getTLSKind() != VarDecl::TLS_None ||
        Definition->getType()->isReferenceType() ||
        Definition->getCanonicalDecl() != Variable->getCanonicalDecl())
      Reject("The addressed variable requires a source-owned static definition without TLS.");
    Current = Definition->getType();
    if (type(Current, L).empty())
      Reject("The addressed object must retain a supported complete storage type.");
    Place = json::Object{{"kind", "var"}, {"name", name(Definition)},
                         {"type", type(Current, L)}, {"loc", loc(L)}};
  } else if (const auto *Literal = dyn_cast_or_null<StringLiteral>(Base.dyn_cast<const Expr *>())) {
    Current = Literal->getType();
    Place = stringObject(Literal);
  } else if (const auto *Temporary = dyn_cast_or_null<MaterializeTemporaryExpr>(Base.dyn_cast<const Expr *>())) {
    Current = Temporary->getType();
    Place = staticTemporaryObject(Temporary);
  } else {
    Reject("Permanent addresses require owned static variables, string literals or checked static temporaries.");
  }
  int64_t Offset = 0;
  bool AtArrayEnd = false;
  auto Address = [&](json::Object Object, QualType ObjectType) {
    return json::Object{{"kind", "address"},
                        {"type", type(Context.getPointerType(ObjectType), L)},
                        {"args", json::Array{std::move(Object)}}, {"loc", loc(L)}};
  };
  auto Index = [&](json::Object Pointer, QualType Element, uint64_t N) {
    return json::Object{{"kind", "index"}, {"type", type(Element, L)},
                        {"args", json::Array{std::move(Pointer),
                            literal(llvm::APSInt(llvm::APInt(64, N), true), "u64", L)}},
                        {"loc", loc(L)}};
  };
  const auto Path = V.getLValuePath();
  chargeExpansion(Path.size() * 4 + 3, L);
  for (unsigned I = 0; I < Path.size(); ++I) {
    if (const auto *Array = Context.getAsConstantArrayType(Current)) {
      auto N = Path[I].getAsArrayIndex();
      auto Count = Array->getSize().getLimitedValue(65537);
      if (N > Count || (N == Count && (ReferenceBinding || I + 1 != Path.size())))
        Reject("A constant array path must stay within its extent; only the final address may be one-past.");
      auto Element = Array->getElementType();
      json::Object Decay{{"kind", "array_decay"},
                         {"type", type(Context.getPointerType(Element), L)},
                         {"args", json::Array{std::move(Place)}}, {"loc", loc(L)}};
      Place = Index(std::move(Decay), Element, N);
      Offset += int64_t(N) * Context.getTypeSizeInChars(Element).getQuantity();
      Current = Element;
      AtArrayEnd = N == Count;
    } else if (const auto *Record = Current->getAsCXXRecordDecl()) {
      auto Entry = Path[I].getAsBaseOrMember();
      if (const auto *BaseRecord = dyn_cast_or_null<CXXRecordDecl>(Entry.getPointer())) {
        const auto *Base = emptyBase(Record);
        if (!Base || Entry.getInt() ||
            BaseRecord->getCanonicalDecl() != Base->Base)
          Reject("A constant base path requires its exact checked nonvirtual direct base.");
        auto BaseType = Context.getRecordType(Base->Base);
        if (Current.isConstQualified())
          BaseType = BaseType.withConst();
        Place = json::Object{{"kind", "member"}, {"name", Base->Member},
                             {"type", type(BaseType, L)},
                             {"args", json::Array{std::move(Place)}}, {"loc", loc(L)}};
        Current = BaseType;
        continue; // The descriptor has checked the actual base offset is zero.
      }
      const auto *Field = dyn_cast_or_null<FieldDecl>(Entry.getPointer());
      if (!Field || Entry.getInt() || Field->isBitField() || Field->isMutable() ||
          Field->getType()->isReferenceType() ||
          !S.owns(Sources, Field->getLocation()) || !Record->getDefinition() ||
          Field->getParent()->getCanonicalDecl() != Record->getCanonicalDecl())
        Reject("A constant record path requires its actual supported field, without a base-class adjustment.");
      Offset += Context.getASTRecordLayout(Record->getDefinition())
                    .getFieldOffset(Field->getFieldIndex()) / Context.getCharWidth();
      auto FieldType = Field->getType();
      if (Current.isConstQualified())
        FieldType = FieldType.withConst();
      Place = json::Object{{"kind", "member"}, {"name", name(Field)},
                           {"type", type(FieldType, L)},
                           {"args", json::Array{std::move(Place)}}, {"loc", loc(L)}};
      Current = FieldType;
    } else {
      Reject("A constant address path can traverse only complete arrays and record fields.");
    }
  }
  if (V.isLValueOnePastTheEnd()) {
    if (ReferenceBinding)
      Reject("A static reference must designate an existing object, not one-past storage.");
    if (AtArrayEnd)
      Reject("A constant address cannot advance beyond an array's one-past position.");
    Place = Index(Address(std::move(Place), Current), Current, 1);
    Offset += Context.getTypeSizeInChars(Current).getQuantity();
  }
  if (V.getLValueOffset().getQuantity() != Offset)
    Reject("The typed subobject path must reproduce the frontend's exact constant byte offset.");
  auto Result = Address(std::move(Place), Current);
  if (Result.getString("type") != ResultType)
    Result = json::Object{{"kind", "cast"}, {"type", ResultType},
                          {"args", json::Array{std::move(Result)}}, {"loc", loc(L)}};
  return Result;
}

json::Object Adapter::constant(const APValue &V, QualType T, SourceLocation L) {
  auto Kind = type(T, L);
  if (S.coreV2() && T->isReferenceType())
    return constantPointer(V, Context.getPointerType(T->getPointeeType()), L,
                           /*ReferenceBinding=*/true);
  if (V.isInt())
    return literal(V.getInt(), Kind, L);
  if (V.isFloat() && (S.math() || S.coreV2()))
    return floatingLiteral(V.getFloat(), L);
  if (S.coreV2() && T->isPointerType() && !T->isFunctionPointerType())
    return constantPointer(V, T, L);
  if (V.isArray() && S.coreV2()) {
    const auto *Array = Context.getAsConstantArrayType(T);
    if (!Array || V.getArraySize() != Array->getSize().getZExtValue()) {
      reject(L, "constant array", "The folded value must retain its complete array extent.");
      throw Failure{};
    }
    chargeExpansion(V.getArraySize() + 1, L);
    json::Array Values;
    for (unsigned I = 0; I < V.getArraySize(); ++I)
      Values.push_back(constant(I < V.getArrayInitializedElts()
                                   ? V.getArrayInitializedElt(I) : V.getArrayFiller(),
                               Array->getElementType(), L));
    return json::Object{{"kind", "aggregate"}, {"type", Kind},
                        {"args", std::move(Values)}, {"loc", loc(L)}};
  }
  if (S.coreV2() && T->isNullPtrType() && V.isLValue() && V.isNullPointer())
    return json::Object{{"kind", "null"}, {"type", Kind}, {"loc", loc(L)}};
  if (S.coreV2() && T->isFunctionPointerType() && V.isLValue() &&
      V.getLValueOffset().isZero() && !V.isLValueOnePastTheEnd() &&
      !V.getLValueCallIndex() && !V.getLValueVersion() &&
      (!V.hasLValuePath() || V.getLValuePath().empty())) {
    if (V.isNullPointer())
      return json::Object{{"kind", "null"}, {"type", Kind}, {"loc", loc(L)}};
    const auto *Target = dyn_cast_or_null<FunctionDecl>(
        V.getLValueBase().dyn_cast<const ValueDecl *>());
    if (Target) {
      auto Address = functionAddress(Target, L);
      if (Address.getString("type") == Kind)
        return Address;
    }
  }
  if (V.isStruct()) {
    json::Array Args;
    const auto *Record = T->getAsCXXRecordDecl();
    Record = Record ? Record->getDefinition() : nullptr;
    if (!Record || V.getStructNumBases() != Record->getNumBases() ||
        V.getStructNumFields() != std::distance(Record->field_begin(), Record->field_end())) {
      reject(L, "constant record", "A folded record must retain every actual base and field value.");
      throw Failure{};
    }
    if (const auto *Base = emptyBase(Record))
      Args.push_back(constant(V.getStructBase(0), Context.getRecordType(Base->Base), L));
    unsigned I = 0;
    for (const auto *F : Record->fields())
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
  std::set<const FunctionDecl *> CompletedOperationDefinitions;
  std::map<const CXXRecordDecl *, bool> HasArray;
  std::map<const CXXRecordDecl *, bool> FlatReferenceLayouts;
  std::set<const Expr *> DirectFunctionCallees, GeneratedBuiltinCallees, FunctionValueDesignators;
  std::map<const Expr *, SourceLocation> DirectTemplateCallLocations;
  std::set<const CallExpr *> GeneratedArrayAssignments;
  std::map<const OpaqueValueExpr *, const Expr *> ArraySources;
  unsigned ArrayIndexDepth = 0;
  const CXXMethodDecl *CurrentMethod = nullptr;
  const FunctionDecl *CurrentFunction = nullptr;
  const DeclaratorDecl *CurrentDeclarator = nullptr;
  const FieldDecl *CurrentDefaultField = nullptr;
  SourceLocation ImplicitInitializerOwner;
  const TemplateParameterList *TemplateParameterTypeSource = nullptr;
  std::map<const FunctionDecl *, const FriendFunctionSource *> FriendFunctionSources;
  std::map<const FriendDecl *, const FriendDeclarationSource *> FriendDeclarationSources;
  std::map<const FunctionDecl *, const FriendDecl *> FriendFunctionDeclarations;
  std::map<const FunctionDecl *, const FriendDecl *> WrittenFriendFunctions;
  std::set<const FriendDecl *> WrittenFriendDeclarations, CheckedFriendDeclarations;
  std::map<const FunctionTemplateDecl *, const FriendFunctionTemplateSource *> FriendTemplateSources;
  std::map<const FunctionTemplateDecl *, const FriendDecl *>
      FriendTemplateDeclarations, WrittenFriendTemplates;
  std::map<const FunctionTemplateDecl *, std::vector<const FriendFunctionTemplateSource *>>
      FriendTemplateFamilies;
  std::map<const FunctionDecl *, const FunctionTemplateBodySource *> TemplateBodySources;
  std::set<const FunctionTemplateDecl *> IndependentTemplateIdentities, FriendTemplateCanonicals;
  std::set<const FunctionDecl *> CheckedFriendTemplateFunctions;
  std::map<const ClassTemplateDecl *, const FriendClassTemplateSource *> FriendClassSources;
  std::map<const ClassTemplateDecl *, const FriendDecl *>
      FriendClassDeclarations, WrittenFriendClasses;
  std::set<const ClassTemplateDecl *> FriendClassCanonicals;
  std::set<const FunctionDecl *> CheckedFriendFunctions, FriendCanonicalFunctions,
      EmittedFriendDefinitions;
  std::map<const NamedDecl *, const NamedDecl *> PackOwners;
  std::map<const NamedDecl *, std::pair<const Decl *, unsigned>> OuterPackDeclarations;
  using TypeSourceKey = std::pair<const Type *, unsigned>;
  using FunctionSourceKey = std::pair<const FunctionDecl *, unsigned>;
  using VariableSourceKey = std::pair<const VarDecl *, unsigned>;
  std::map<TypeSourceKey, std::vector<const TemplateUseSource *>> TypeSources;
  std::map<FunctionSourceKey, std::vector<const TemplateUseSource *>> FunctionSources;
  std::map<const Expr *, std::vector<const SelectedTemplateCallSource *>> SelectedCallSources;
  std::map<const Decl *, std::vector<const TemplateUseSource *>> ClassSources;
  std::map<const Decl *, std::vector<const TemplateUseSource *>> ClassFullDeclarations;
  std::set<const Decl *> WrittenClassFullDeclarations, CheckedClassFullDeclarations,
      ActiveClassFullDeclarations;
  std::map<const FunctionDecl *, std::vector<const FunctionSpecializationSource *>> SpecializationSources;
  std::map<VariableSourceKey, std::vector<const TemplateUseSource *>> VariableSources;
  std::map<const Decl *, std::vector<const TemplateUseSource *>> PartialDeclarations;
  std::set<const Decl *> WrittenPartialDeclarations, CheckedPartialDeclarations, ActivePartialDeclarations;
  std::map<const VarDecl *, std::vector<const TemplateUseSource *>> VariableDeclarations;
  std::map<const VarDecl *, std::vector<const TemplateUseSource *>> VariableArgumentSources;
  std::map<const VarDecl *, std::vector<const VariableTypeSource *>> VariableTypeSources;
  std::set<const VarTemplatePartialSpecializationDecl *> WrittenVariablePartials;
  std::set<const ClassTemplateDecl *> WrittenClassTemplates;
  std::set<const NamedDecl *> ActiveClassShapes;
  std::set<const CXXRecordDecl *> WrittenOrdinaryClasses, CheckedOrdinaryClasses,
      ActiveOrdinaryClasses, TraversedOrdinaryClasses;
  std::set<const Decl *> ActiveClassTemplateSources, CheckedClassTemplateSources;
  struct PartialSource {
    const TemplateUseSource *Deduction = nullptr, *Pattern = nullptr;
    bool Conflict = false;
  };
  std::map<const TemplateArgumentList *, PartialSource> PartialSources;
  std::map<const TemplateArgumentList *, PartialSource> VariablePartialSources;
  std::set<const CXXRecordDecl *> CheckedPartialRecords;
  std::vector<const CXXRecordDecl *> ActivePartialRecords;
  struct TemplateSourceFrame {
    const TemplateUseSource &Source;
    std::vector<bool> Ready;
  };
  std::vector<TemplateSourceFrame *> TemplateFrames;
  struct DefinitionSourceFrame {
    const Decl *Declaration;
    const NamedDecl *Owner;
    const TemplateArgumentList *Arguments;
    std::size_t TemplateDepth;
  };
  // One stack defines namespace/member/default source nesting. A new
  // definition fences caller use frames while keeping its actual argument list.
  std::vector<DefinitionSourceFrame> DefinitionFrames;
  std::set<const VarDecl *> CheckedVariablePartials, CheckedVariableDeclarations;
  std::vector<const VarDecl *> ActiveVariablePartials, ActiveVariableDeclarations;
  std::vector<const TemplateUseSource *> ActiveTemplateUses;
  std::set<const Expr *> CheckedSemanticInitializers;
  std::map<const UnresolvedLookupExpr *, const DeclRefExpr *> InitializerLookups;
  std::set<const Stmt *> InitializerLookupWrappers;
  std::set<const InitListExpr *> EmptyVoidLists;
  std::set<const CXXConstructExpr *> CheckedConstructions;
  std::set<const Expr *> NewArrayInitializers;
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
  bool templateParametersShape(const TemplateParameterList *Parameters,
                               std::optional<unsigned> ExpectedDepth = 0) {
    if (!ExpectedDepth || !Parameters || !Parameters->size() || Parameters->size() > 64 ||
        Parameters->hasAssociatedConstraints())
      return false;
    for (const auto *Parameter : *Parameters) {
      if (!owned(Parameter) || Parameter->isInvalidDecl() || Parameter->hasAttrs())
        return false;
      if (const auto *Type = dyn_cast<TemplateTypeParmDecl>(Parameter)) {
        if (Type->getDepth() != *ExpectedDepth || Type->hasTypeConstraint())
          return false;
        continue;
      }
      const auto *Value = dyn_cast<NonTypeTemplateParmDecl>(Parameter);
      if (!Value || Value->getDepth() != *ExpectedDepth ||
          Value->getType().isNull())
        return false;
      auto T = Value->getType();
      if (T.isVolatileQualified() || T.isRestrictQualified() ||
          (!T->isDependentType() && !T->isUndeducedAutoType() &&
           !scalarTemplateType(T)))
        return false;
    }
    return true;
  }
  // Structural copy edges only: member enumeration runs in a separate helper.
  bool ordinaryClassIdentityShape(const CXXRecordDecl *Record) {
    std::set<const CXXRecordDecl *> Seen;
    for (const auto *Current = Record; Current;) {
      if (!ordinaryMemberClassIdentity(Current) || !owned(Current) ||
          hasNonFinalAttributes(Current) || Current->getFriendObjectKind() ||
          (Current->getLexicalDeclContext() != Current->getDeclContext() &&
           !isa<TranslationUnitDecl, NamespaceDecl>(Current->getLexicalDeclContext())) ||
          !outerTemplateListsShape(Current) || Seen.size() >= 64 ||
          !Seen.insert(Current->getCanonicalDecl()).second)
        return false;
      A.chargeExpansion(1, Current->getLocation());
      const auto Qualifier = Current->getQualifierLoc();
      if (Qualifier && !A.S.owns(A.Sources, Qualifier.getBeginLoc()))
        return false;
      const auto *Origin = Current->getInstantiatedFromMemberClass();
      if (!Origin)
        return WrittenOrdinaryClasses.count(Current);
      const auto *Own = writtenOwnMemberClass(Current);
      if (Own && (!WrittenOrdinaryClasses.count(Own) || !owned(Own) ||
                  Own->isInvalidDecl() || hasNonFinalAttributes(Own) ||
                  Own->getFriendObjectKind() || !outerTemplateListsShape(Own)))
        return false;
      if (!Own && Current->getTemplateSpecializationKind() == TSK_ExplicitSpecialization)
        return false;
      if (!ordinaryMemberClassIdentity(Origin) || !owned(Origin) ||
          Current->getDeclName() != Origin->getDeclName() ||
          (!Own && Current->getTagKind() != Origin->getTagKind()))
        return false;
      const auto *Parent = cast<CXXRecordDecl>(Current->getDeclContext());
      const auto *OriginParent = cast<CXXRecordDecl>(Origin->getDeclContext());
      const auto *Body = classBodyRecord(Parent);
      if (!Body || Body->getCanonicalDecl() != OriginParent->getCanonicalDecl())
        return false;
      // Keep the origin relation even when the source supplies a new own body.
      Current = Origin;
    }
    return false;
  }
  bool ordinaryClassDeclarationShape(const CXXRecordDecl *Record) {
    return ordinaryClassIdentityShape(Record) &&
           classOwnerScope(Record->getDeclContext());
  }
  bool emptyBasesShape(const CXXRecordDecl *Record) {
    if (!Record->getNumBases())
      return true;
    if (!A.S.coreV2() || Record->getNumBases() != 1 || !Record->field_empty())
      return false;
    const auto &Base = *Record->bases_begin();
    if (Base.isVirtual() || Base.isPackExpansion() || !Base.getTypeSourceInfo() ||
        !A.S.owns(A.Sources, Base.getBeginLoc()))
      return false;
    if (!Record->isDependentContext())
      return A.emptyBaseChainShape(Record);
    return Base.getType()->isDependentType() ||
           A.emptyBaseChainShape(Base.getType()->getAsCXXRecordDecl());
  }
  bool ordinaryClassBodyShape(const CXXRecordDecl *Record) {
    if (!ordinaryClassDeclarationShape(Record))
      return false;
    const auto *Definition = Record->getDefinition();
    if (!Definition)
      return true; // Nonlocal ordinary nested definitions are instantiated lazily.
    if (ActiveClassShapes.size() >= 64 || !ActiveClassShapes.insert(Record).second)
      return false;
    auto Restore = llvm::make_scope_exit([&] { ActiveClassShapes.erase(Record); });
    return owned(Definition) && !Definition->isInvalidDecl() &&
           !hasNonFinalAttributes(Definition) && emptyBasesShape(Definition) &&
           classTemplateMembers(Definition);
  }
  bool zeroParameterClassBodyShape(const CXXRecordDecl *Record) {
    if (ordinaryMemberClassScope(Record))
      return ordinaryClassBodyShape(Record);
    return classFullBodyShape(dyn_cast_or_null<ClassTemplateSpecializationDecl>(Record));
  }
  // Local structural edges only: never enumerate a parent's members here.
  bool classFullIdentityShape(const CXXRecordDecl *Record) {
    if (!classScopeFullIdentity(Record) || !owned(Record) || hasNonFinalAttributes(Record) ||
        !Record->getIdentifier() || Record->getFriendObjectKind())
      return false;
    const auto *D = cast<ClassTemplateSpecializationDecl>(Record);
    const auto *Primary = D->getSpecializedTemplate();
    if (!owned(Primary) || Primary->isInvalidDecl() || Primary->hasAttrs() ||
        !D->getTemplateArgsAsWritten() || !outerTemplateListsShape(D, true))
      return false;
    const auto *Origin = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
        D->getInstantiatedFromMemberClass());
    if (!Origin)
      return genericClassFullIdentity(D) && WrittenClassFullDeclarations.count(D);
    // The successful copy producer supplies a final full object and the exact
    // written full object, independently of the primary's redeclaration chain.
    if (D->isDependentContext() || !genericClassFullIdentity(Origin) ||
        !owned(Origin) || hasNonFinalAttributes(Origin) || Origin->getFriendObjectKind() ||
        !WrittenClassFullDeclarations.count(Origin))
      return false;
    const auto *Parent = dyn_cast<CXXRecordDecl>(D->getDeclContext());
    if (!Parent || (!isa<ClassTemplateSpecializationDecl>(Parent) &&
                    !ordinaryClassIdentityShape(Parent)))
      return false;
    const auto *OriginParent = dyn_cast<CXXRecordDecl>(Origin->getDeclContext());
    const auto *Body = classBodyRecord(Parent);
    const auto *PrimaryOrigin = Primary->getInstantiatedFromMemberTemplate();
    return owned(Parent) && !Parent->isDependentContext() && OriginParent && Body &&
           Body->getCanonicalDecl() == OriginParent->getCanonicalDecl() &&
           PrimaryOrigin && PrimaryOrigin->getCanonicalDecl() ==
                                Origin->getSpecializedTemplate()->getCanonicalDecl();
  }
  bool classFullDeclarationShape(const ClassTemplateSpecializationDecl *D) {
    return classFullIdentityShape(D) && classOwnerScope(D->getDeclContext()) &&
           classTemplateDeclarationShape(D->getSpecializedTemplate()) &&
           classPatternOriginShape(D->getSpecializedTemplate());
  }
  bool classFullBodyShape(const ClassTemplateSpecializationDecl *D) {
    if (!classFullDeclarationShape(D))
      return false;
    const auto *Definition = D->getDefinition();
    if (!Definition)
      return true;
    if (ActiveClassShapes.size() >= 64 || !ActiveClassShapes.insert(D).second)
      return false;
    auto Restore = llvm::make_scope_exit([&] { ActiveClassShapes.erase(D); });
    return owned(Definition) && !Definition->isInvalidDecl() &&
           !hasNonFinalAttributes(Definition) && emptyBasesShape(Definition) &&
           classTemplateMembers(Definition);
  }
  // Structural ancestors only. A child declaration must not recursively ask
  // its parent to enumerate that same child through classTemplateMembers.
  bool classOwnerScope(const DeclContext *Context) {
    std::set<const CXXRecordDecl *> Seen;
    while (Context && !isa<TranslationUnitDecl, NamespaceDecl>(Context)) {
      const auto *Record = dyn_cast<CXXRecordDecl>(Context);
      if (!owned(Record) || Record->isInvalidDecl() || hasNonFinalAttributes(Record) ||
          !Record->getIdentifier() || Record->isUnion() || Record->isLambda() ||
          Record->isLocalClass())
        return false;
      const bool OrdinaryBody = ordinaryMemberClassScope(Record);
      const bool FullBody = genericClassFullIdentity(Record) ||
          (classScopeFullIdentity(Record) && Record->getInstantiatedFromMemberClass());
      if ((FullBody && !classFullIdentityShape(Record)) ||
          (OrdinaryBody && !ordinaryClassIdentityShape(Record)))
        return false;
      A.chargeExpansion(1, Record->getLocation());
      if (Seen.size() >= 64 || !Seen.insert(Record->getCanonicalDecl()).second)
        return false;
      const auto *Pattern = classTemplatePattern(Record);
      if (Record->isDependentContext() && !FullBody && !OrdinaryBody &&
          (!owned(Pattern) ||
           !templateParametersShape(templateSourceParameters(Pattern),
                                    templateSourceParameterDepth(Pattern))))
        return false;
      if (!Pattern && !FullBody && !OrdinaryBody && (Record->getKind() != Decl::CXXRecord ||
                       Record->getMemberSpecializationInfo() ||
                       Record->getNumTemplateParameterLists()))
        return false;
      const auto *Definition = Record->getDefinition();
      if (!Definition && Pattern) {
        const auto *Body = classBodyRecord(Record);
        Definition = Body ? Body->getDefinition() : nullptr;
      }
      if (!owned(Definition) || Definition->isInvalidDecl() ||
          hasNonFinalAttributes(Definition) || !emptyBasesShape(Definition) ||
          (Definition->getLexicalDeclContext() != Definition->getDeclContext() &&
           !isa<TranslationUnitDecl, NamespaceDecl>(Definition->getLexicalDeclContext())))
        return false;
      Context = Record->getDeclContext();
    }
    return Context != nullptr;
  }
  // Pinned Sema walks from the written qualifier's innermost record outwards,
  // stopping at an explicit specialization, then matches headers outer first.
  // Null entries represent a concrete level's empty template<> header.
  bool outerTemplateOwners(const Decl *D, bool Full,
                           std::vector<const NamedDecl *> &Owners) {
    if (!owned(D))
      return false;
    const auto *Context = D->getDeclContext();
    std::set<const DeclContext *> Seen;
    while (Context && Context != D->getLexicalDeclContext() &&
           !isa<TranslationUnitDecl, NamespaceDecl>(Context)) {
      const auto *Record = dyn_cast<CXXRecordDecl>(Context);
      if (!owned(Record) || Seen.size() >= 64 || !Seen.insert(Context).second)
        return false;
      A.chargeExpansion(1, Record->getLocation());
      const auto *Spec = dyn_cast<ClassTemplateSpecializationDecl>(Record);
      if (Spec && !isa<ClassTemplatePartialSpecializationDecl>(Spec) &&
          Spec->isExplicitSpecialization())
        break;
      const auto *Owner = classTemplatePattern(Record);
      if (Record->isDependentContext()) {
        if (Owner)
          Owners.push_back(Owner);
        else if (!ordinaryMemberClassIdentity(Record) && !genericClassFullIdentity(Record))
          return false;
      } else if (Spec) {
        Owners.push_back(nullptr);
      }
      // Clang assigns this kind to partials too, but their members still need
      // the parameter lists of enclosing templates. Only a full stops here.
      if (!isa<ClassTemplatePartialSpecializationDecl>(Record) &&
          Record->getTemplateSpecializationKind() == TSK_ExplicitSpecialization)
        break;
      Context = Record->getDeclContext();
    }
    if (!Context)
      return false;
    std::reverse(Owners.begin(), Owners.end());
    if (Full)
      Owners.push_back(nullptr);
    return Owners.size() <= 64;
  }
  bool outerTemplateListShape(const TemplateParameterList *Parameters,
                              const NamedDecl *Owner) {
    if (!Parameters || !A.S.owns(A.Sources, Parameters->getTemplateLoc()))
      return false;
    if (!Owner)
      return !Parameters->size() && !Parameters->hasAssociatedConstraints();
    const auto *Expected = templateSourceParameters(Owner);
    if (!Expected || Parameters->size() != Expected->size() ||
        !templateParametersShape(Parameters, templateSourceParameterDepth(Owner)))
      return false;
    for (unsigned I = 0; I < Parameters->size(); ++I) {
      const auto *Actual = Parameters->getParam(I);
      const auto *Pattern = Expected->getParam(I);
      if (Actual->getKind() != Pattern->getKind() ||
          Actual->isTemplateParameterPack() != Pattern->isTemplateParameterPack())
        return false;
    }
    return true;
  }
  template <class Declaration>
  bool outerTemplateListsShape(const Declaration *D, bool Full = false) {
    // Instantiation drops these lists; the written origin is checked separately.
    if (!D->getNumTemplateParameterLists())
      return true;
    std::vector<const NamedDecl *> Owners;
    if (!outerTemplateOwners(D, Full, Owners) ||
        Owners.size() != D->getNumTemplateParameterLists())
      return false;
    for (unsigned I = 0; I < Owners.size(); ++I)
      if (!outerTemplateListShape(D->getTemplateParameterList(I), Owners[I]))
        return false;
    return true;
  }
  bool memberClassDeclarationShape(const CXXRecordDecl *Record, bool Full = false) {
    if (Full && (genericClassFullIdentity(Record) ||
                 (Record && Record->getInstantiatedFromMemberClass())))
      return classFullDeclarationShape(dyn_cast<ClassTemplateSpecializationDecl>(Record));
    if (!owned(Record) || !isa<CXXRecordDecl>(Record->getDeclContext()) ||
        !classOwnerScope(Record->getDeclContext()) || Record->isInvalidDecl() ||
        hasNonFinalAttributes(Record) || Record->getFriendObjectKind() ||
        Record->getInstantiatedFromMemberClass() ||
        (Record->getLexicalDeclContext() != Record->getDeclContext() &&
         !isa<TranslationUnitDecl, NamespaceDecl>(Record->getLexicalDeclContext())) ||
        !outerTemplateListsShape(Record, Full))
      return false;
    const auto Qualifier = Record->getQualifierLoc();
    return !Qualifier || A.S.owns(A.Sources, Qualifier.getBeginLoc());
  }
  bool writtenConstructorInitializersShape(const CXXConstructorDecl *C) {
    for (const auto *Init : C->inits()) {
      if (!Init->isWritten() || Init->isAnyMemberInitializer())
        continue;
      // Sema keeps a dependent delegation as a type/base initializer until
      // substitution. These owners have no bases; only one such initializer
      // can become a delegating constructor in a concrete instance.
      if (C->getNumCtorInitializers() != 1 || Init->isPackExpansion() ||
          !Init->getTypeSourceInfo() || !Init->getInit() ||
          (!Init->isDelegatingInitializer() &&
           !(C->getParent()->isDependentContext() && Init->isBaseInitializer())))
        return false;
    }
    return true;
  }
  bool memberTemplateDeclarationShape(const FunctionTemplateDecl *D) {
    if (!D || !owned(D) || D->isInvalidDecl() || D->hasAttrs() ||
        D->isAbbreviated() || D->getFriendObjectKind() ||
        D->hasAssociatedConstraints())
      return false;
    const auto *M = dyn_cast<CXXMethodDecl>(D->getTemplatedDecl());
    if (!ordinaryMemberTemplateName(M) || !owned(M) || M->isInvalidDecl() ||
        M->hasAttrs() || M->getFriendObjectKind() || M->isVirtual() ||
        M->isVariadic() || M->isDefaulted() ||
        M->isConsteval() || M->isExplicitObjectMemberFunction() ||
        M->getTrailingRequiresClause() ||
        M->getDescribedFunctionTemplate() != D ||
        M->getMethodQualifiers().hasVolatile() ||
        M->getMethodQualifiers().hasRestrict())
      return false;
    const auto *Parent = M->getParent();
    // Do not recurse through classTemplateMembers here: that inventory calls
    // this helper for every member template. Its class checks run separately.
    if (!owned(Parent) || Parent->isInvalidDecl() || hasNonFinalAttributes(Parent) ||
        !Parent->getIdentifier() || Parent->isUnion() || Parent->isLambda() ||
        Parent->isLocalClass() || D->getDeclContext() != Parent ||
        !classOwnerScope(Parent->getDeclContext()) ||
        (D->getLexicalDeclContext() != Parent &&
         !isa<TranslationUnitDecl, NamespaceDecl>(D->getLexicalDeclContext())))
      return false;
    const auto *Definition = Parent->getDefinition();
    if (Definition && (!owned(Definition) || Definition->isInvalidDecl() ||
                       hasNonFinalAttributes(Definition) || !emptyBasesShape(Definition)))
      return false;
    const auto *Outer = classTemplatePattern(Parent);
    const bool OrdinaryBody = ordinaryMemberClassScope(Parent);
    const bool FullBody = genericClassFullIdentity(Parent) ||
        (classScopeFullIdentity(Parent) && Parent->getInstantiatedFromMemberClass());
    if ((FullBody && !classFullDeclarationShape(dyn_cast<ClassTemplateSpecializationDecl>(Parent))) ||
        (OrdinaryBody && !ordinaryClassDeclarationShape(Parent)))
      return false;
    if (Parent->isDependentContext() && !FullBody && !OrdinaryBody &&
        (!owned(Outer) || !templateParametersShape(templateSourceParameters(Outer), templateSourceParameterDepth(Outer)) ||
         !isa<ClassTemplateDecl, ClassTemplatePartialSpecializationDecl>(Outer)))
      return false;
    if (!templateParametersShape(D->getTemplateParameters(),
                                 templateSourceParameterDepth(D)) ||
        !outerTemplateListsShape(M))
      return false;
    if (const auto *C = dyn_cast<CXXConstructorDecl>(M)) {
      if ((!C->isUserProvided() && !C->isDeletedAsWritten()) || C->isStatic() ||
          C->isInheritingConstructor() || C->getMethodQualifiers().getCVRQualifiers())
        return false;
      if (!writtenConstructorInitializersShape(C))
        return false;
    } else if (const auto *C = dyn_cast<CXXConversionDecl>(M)) {
      if ((!C->isUserProvided() && !C->isDeletedAsWritten()) ||
          C->isStatic() || C->getNumParams())
        return false;
    } else if (M->isOverloadedOperator() &&
               ((!M->isUserProvided() && !M->isDeletedAsWritten()) ||
                M->isStatic() != allocationOperatorKind(M->getOverloadedOperator()))) {
      return false;
    }
    for (const auto *Parameter : M->parameters()) {
      A.chargeExpansion(1, Parameter->getLocation());
      if (!owned(Parameter) || Parameter->isInvalidDecl() || Parameter->hasAttrs())
        return false;
    }
    return true;
  }
  bool memberTemplateShape(const FunctionTemplateDecl *D) {
    if (!memberTemplateDeclarationShape(D))
      return false;
    auto Found = A.TemplateOrdinals.find(D->getCanonicalDecl());
    if (Found == A.TemplateOrdinals.end())
      return false;
    const auto Ordinal = Found->second;
    std::set<const FunctionTemplateDecl *> Seen;
    for (auto *Current = D->getCanonicalDecl(); Current;
         Current = Current->getInstantiatedFromMemberTemplate()) {
      A.chargeExpansion(1, Current->getLocation());
      if (Seen.size() >= 64 || !Seen.insert(Current->getCanonicalDecl()).second ||
          !memberTemplateDeclarationShape(Current))
        return false;
      auto Indexed = A.TemplateOrdinals.find(Current->getCanonicalDecl());
      if (Indexed == A.TemplateOrdinals.end() || Indexed->second != Ordinal)
        return false; // The index has already checked each selected class edge.
    }
    return true;
  }
  bool classTemplateFunctionShape(const CXXMethodDecl *M) {
    if (!M || !owned(M) || M->isInvalidDecl() || M->hasAttrs() ||
        M->getFriendObjectKind() || M->getDescribedFunctionTemplate() ||
        M->getPrimaryTemplate() || M->isVirtual() || M->isVariadic() ||
        M->isConsteval() ||
        M->isExplicitObjectMemberFunction() || M->getTrailingRequiresClause() ||
        M->getMethodQualifiers().hasVolatile() ||
        M->getMethodQualifiers().hasRestrict())
      return false;
    const bool Defaulted = defaultedSpecialMember(M);
    const bool Deleted = M->isDeletedAsWritten();
    if (const auto *Defaulting = defaultedDeclaration(M))
      if (!Defaulted || !owned(Defaulting))
        return false;
    if (const auto *Constructor = dyn_cast<CXXConstructorDecl>(M)) {
      if ((!Defaulted && !Deleted && !Constructor->isUserProvided()) ||
          Constructor->isInheritingConstructor() || Constructor->isStatic() ||
          Constructor->getMethodQualifiers().getCVRQualifiers())
        return false;
      if (!writtenConstructorInitializersShape(Constructor))
        return false;
    } else if (const auto *Destructor = dyn_cast<CXXDestructorDecl>(M)) {
      if ((!Defaulted && !Deleted && !Destructor->isUserProvided()) || Destructor->isStatic() ||
          Destructor->getNumParams() ||
          Destructor->getMethodQualifiers().getCVRQualifiers())
        return false;
    } else if (const auto *Conversion = dyn_cast<CXXConversionDecl>(M)) {
      if ((!Deleted && !Conversion->isUserProvided()) || Conversion->isStatic() ||
          Conversion->getNumParams())
        return false;
    } else if (M->getKind() != Decl::CXXMethod) {
      return false;
    } else if (!M->getIdentifier() && !Defaulted) {
      if ((!Deleted && !M->isUserProvided()) ||
          M->isStatic() != allocationOperatorKind(M->getOverloadedOperator()) ||
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
    if (T->isPointerType() || T->isReferenceType() || T->isRecordType())
      return true; // Actual instances retain source, ABI and constant checks.
    if (T->isArrayType())
      return true; // Bound, element type and initializer are checked on use.
    return T->isDependentType() || T->isUndeducedAutoType() || T->isNullPtrType() ||
           T->isIntegralOrEnumerationType() || binaryFloatingType(T);
  }
  bool memberVariableOwnerShape(const VarDecl *D) {
    const auto *Parent = D ? dyn_cast<CXXRecordDecl>(D->getDeclContext()) : nullptr;
    if (!owned(Parent) || !D->isStaticDataMember() || Parent->isInvalidDecl() ||
        hasNonFinalAttributes(Parent) || !Parent->getIdentifier() || Parent->isUnion() ||
        Parent->isLambda() || Parent->isLocalClass() ||
        !classOwnerScope(Parent->getDeclContext()) ||
        (D->getLexicalDeclContext() != Parent &&
         !isa<TranslationUnitDecl, NamespaceDecl>(D->getLexicalDeclContext())))
      return false;
    // Structural owner only: classTemplateMembers calls this helper itself.
    const auto *Definition = Parent->getDefinition();
    if (!owned(Definition) || Definition->isInvalidDecl() ||
        hasNonFinalAttributes(Definition) || !emptyBasesShape(Definition))
      return false;
    const auto *Outer = classTemplatePattern(Parent);
    const bool OrdinaryBody = ordinaryMemberClassScope(Parent);
    const bool FullBody = genericClassFullIdentity(Parent) ||
        (classScopeFullIdentity(Parent) && Parent->getInstantiatedFromMemberClass());
    if ((FullBody && !classFullDeclarationShape(dyn_cast<ClassTemplateSpecializationDecl>(Parent))) ||
        (OrdinaryBody && !ordinaryClassDeclarationShape(Parent)))
      return false;
    if (Parent->isDependentContext() && !FullBody && !OrdinaryBody &&
        (!owned(Outer) ||
         !isa<ClassTemplateDecl, ClassTemplatePartialSpecializationDecl>(Outer) ||
         !templateParametersShape(templateSourceParameters(Outer), templateSourceParameterDepth(Outer))))
      return false;
    const auto *Instance = dyn_cast<VarTemplateSpecializationDecl>(D);
    const bool Full = Instance && Instance->getKind() == Decl::VarTemplateSpecialization &&
                      Instance->isExplicitSpecialization();
    if (!outerTemplateListsShape(D, Full))
      return false;
    return true;
  }
  bool variablePatternType(const VarDecl *D) {
    if (!D || !owned(D) || !D->getIdentifier() || D->isInvalidDecl() || D->hasAttrs() ||
        D->getTLSKind() != VarDecl::TLS_None ||
        D->getType().isNull() || D->getType().isVolatileQualified() ||
        D->getType().isRestrictQualified() || !D->getTypeSourceInfo())
      return false;
    if (D->isStaticDataMember()) {
      if (!memberVariableOwnerShape(D))
        return false;
    } else if (!isa<TranslationUnitDecl, NamespaceDecl>(D->getDeclContext()) ||
               !isa<TranslationUnitDecl, NamespaceDecl>(D->getLexicalDeclContext())) {
      return false;
    }
    auto Type = D->getType();
    if (Type->isPointerType() || Type->isReferenceType() || Type->isRecordType())
      return true; // Concrete source, signature and initializer checks follow.
    if (Type->isArrayType())
      return true; // Concrete instances retain their complete written type.
    return Type->isDependentType() || Type->isUndeducedAutoType() || Type->isNullPtrType() ||
           Type->isIntegralOrEnumerationType() || binaryFloatingType(Type);
  }
  bool variableTemplateDeclarationShape(const VarTemplateDecl *D) {
    if (!D || !owned(D) || D->isInvalidDecl() || D->hasAttrs() ||
        D->hasAssociatedConstraints() ||
        !templateParametersShape(D->getTemplateParameters(), templateSourceParameterDepth(D)))
      return false;
    const auto *Pattern = D->getTemplatedDecl();
    if (Pattern->getKind() != Decl::Var || Pattern->getDescribedVarTemplate() != D ||
        D->getDeclContext() != Pattern->getDeclContext() ||
        D->getLexicalDeclContext() != Pattern->getLexicalDeclContext() ||
        !variablePatternType(Pattern))
      return false;
    return Pattern->isStaticDataMember() ||
           (!D->getInstantiatedFromMemberTemplate() && !D->isMemberSpecialization());
  }
  bool variableTemplateShape(const VarTemplateDecl *D) {
    if (!variableTemplateDeclarationShape(D))
      return false;
    if (!D->getTemplatedDecl()->isStaticDataMember())
      return true;
    std::set<const VarTemplateDecl *> Seen;
    for (auto *Current = D->getCanonicalDecl(); Current;) {
      A.chargeExpansion(1, Current->getLocation());
      if (Seen.size() >= 64 || !Seen.insert(Current).second ||
          !variableTemplateDeclarationShape(Current))
        return false;
      const auto *Parent = cast<CXXRecordDecl>(Current->getDeclContext());
      const auto *Next = Current->getInstantiatedFromMemberTemplate();
      if (!Next) {
        if (ordinaryCopiedBody(Parent))
          return false;
        const auto *Instance = dyn_cast<ClassTemplateSpecializationDecl>(Parent);
        return !Instance || Instance->getKind() == Decl::ClassTemplatePartialSpecialization ||
               Instance->isExplicitSpecialization();
      }
      if (!variableTemplateDeclarationShape(Next) ||
          Current->getDeclName() != Next->getDeclName() ||
          Current->getTemplateParameters()->size() != Next->getTemplateParameters()->size())
        return false;
      const auto *NextParent = dyn_cast<CXXRecordDecl>(Next->getDeclContext());
      const auto *Selected = classBodyRecord(Parent);
      if (!NextParent || (Parent->getCanonicalDecl() != NextParent->getCanonicalDecl() &&
          (!Selected || Selected->getCanonicalDecl() != NextParent->getCanonicalDecl())))
        return false;
      Current = Next->getCanonicalDecl();
    }
    return false;
  }
  bool variablePartialDeclarationShape(const VarTemplatePartialSpecializationDecl *D) {
    if (!D || !variablePatternType(D) || D->hasAssociatedConstraints() ||
        !variableTemplateShape(D->getSpecializedTemplate()) ||
        D->getDeclContext() != D->getSpecializedTemplate()->getDeclContext() ||
        !templateParametersShape(D->getTemplateParameters(), templateSourceParameterDepth(D)) ||
        !D->getTemplateArgsAsWritten() ||
        (!D->isStaticDataMember() &&
         (D->getInstantiatedFromMember() || D->isMemberSpecialization())))
      return false;
    for (const auto *Parameter : *D->getTemplateParameters()) {
      A.chargeExpansion(1, Parameter->getLocation());
      if (const auto *Type = dyn_cast<TemplateTypeParmDecl>(Parameter);
          Type && Type->hasDefaultArgument())
        return false;
      if (const auto *Value = dyn_cast<NonTypeTemplateParmDecl>(Parameter);
          Value && Value->hasDefaultArgument())
        return false;
    }
    return true;
  }
  bool variablePartialShape(const VarTemplatePartialSpecializationDecl *D) {
    if (!variablePartialDeclarationShape(D))
      return false;
    if (!D->isStaticDataMember())
      return true;
    std::set<const VarTemplatePartialSpecializationDecl *> Seen;
    for (auto *Current = cast<VarTemplatePartialSpecializationDecl>(D->getCanonicalDecl()); Current;) {
      A.chargeExpansion(1, Current->getLocation());
      if (Seen.size() >= 64 || !Seen.insert(Current).second ||
          !variablePartialDeclarationShape(Current))
        return false;
      const auto *Parent = cast<CXXRecordDecl>(Current->getDeclContext());
      const auto *Next = Current->getInstantiatedFromMember();
      if (!Next) {
        if (ordinaryCopiedBody(Parent))
          return WrittenVariablePartials.count(Current);
        const auto *Instance = dyn_cast<ClassTemplateSpecializationDecl>(Parent);
        // A new partial written for a concrete outer instance owns its pattern;
        // hidden copies must instead retain the producer's member-origin link.
        return !Instance || Instance->getKind() == Decl::ClassTemplatePartialSpecialization ||
               Instance->isExplicitSpecialization() || WrittenVariablePartials.count(Current);
      }
      if (!variablePartialDeclarationShape(Next) ||
          Current->getDeclName() != Next->getDeclName() ||
          Current->getTemplateParameters()->size() != Next->getTemplateParameters()->size())
        return false;
      const auto *NextParent = dyn_cast<CXXRecordDecl>(Next->getDeclContext());
      const auto *Selected = classBodyRecord(Parent);
      const auto *PrimaryOrigin = Current->getSpecializedTemplate()->getInstantiatedFromMemberTemplate();
      if (!NextParent || !PrimaryOrigin ||
          PrimaryOrigin->getCanonicalDecl() != Next->getSpecializedTemplate()->getCanonicalDecl() ||
          (Parent->getCanonicalDecl() != NextParent->getCanonicalDecl() &&
           (!Selected || Selected->getCanonicalDecl() != NextParent->getCanonicalDecl())))
        return false;
      Current = cast<VarTemplatePartialSpecializationDecl>(Next->getCanonicalDecl());
    }
    return false;
  }
  bool variablePatternShape(const NamedDecl *D) {
    return variableTemplateShape(dyn_cast_or_null<VarTemplateDecl>(D)) ||
           variablePartialShape(dyn_cast_or_null<VarTemplatePartialSpecializationDecl>(D));
  }
  bool memberAliasDeclarationShape(const TypeAliasTemplateDecl *D) {
    if (!D || !owned(D) || D->isInvalidDecl() || D->hasAttrs() ||
        D->isMemberSpecialization() || D->hasAssociatedConstraints())
      return false;
    const auto *Pattern = D->getTemplatedDecl();
    const auto *Parent = dyn_cast<CXXRecordDecl>(D->getDeclContext());
    if (!owned(Parent) || Parent->isInvalidDecl() || hasNonFinalAttributes(Parent) ||
        !Parent->getIdentifier() || Parent->isUnion() || Parent->isLambda() ||
        Parent->isLocalClass() ||
        !classOwnerScope(Parent->getDeclContext()) ||
        D->getLexicalDeclContext() != Parent || !owned(Pattern) ||
        Pattern->isInvalidDecl() || Pattern->hasAttrs() ||
        !Pattern->getIdentifier() || !Pattern->getTypeSourceInfo() ||
        Pattern->getDescribedAliasTemplate() != D ||
        Pattern->getDeclContext() != Parent ||
        Pattern->getLexicalDeclContext() != Parent)
      return false;
    const auto *Definition = Parent->getDefinition();
    if (!owned(Definition) || Definition->isInvalidDecl() ||
        hasNonFinalAttributes(Definition) || !emptyBasesShape(Definition))
      return false;
    // Check the outer structural owner without recursively enumerating this
    // alias through classTemplateMembers again.
    const auto *Outer = classTemplatePattern(Parent);
    const bool OrdinaryBody = ordinaryMemberClassScope(Parent);
    const bool FullBody = genericClassFullIdentity(Parent) ||
        (classScopeFullIdentity(Parent) && Parent->getInstantiatedFromMemberClass());
    if ((FullBody && !classFullDeclarationShape(dyn_cast<ClassTemplateSpecializationDecl>(Parent))) ||
        (OrdinaryBody && !ordinaryClassDeclarationShape(Parent)))
      return false;
    if (Parent->isDependentContext() && !FullBody && !OrdinaryBody &&
        (!owned(Outer) ||
         !isa<ClassTemplateDecl, ClassTemplatePartialSpecializationDecl>(Outer) ||
         !templateParametersShape(templateSourceParameters(Outer), templateSourceParameterDepth(Outer))))
      return false;
    return templateParametersShape(D->getTemplateParameters(),
                                   templateSourceParameterDepth(D));
  }
  bool memberAliasShape(const TypeAliasTemplateDecl *D) {
    if (!memberAliasDeclarationShape(D))
      return false;
    std::set<const TypeAliasTemplateDecl *> Seen;
    for (auto *Current = D->getCanonicalDecl(); Current;) {
      A.chargeExpansion(1, Current->getLocation());
      if (Seen.size() >= 64 || !Seen.insert(Current).second ||
          !memberAliasDeclarationShape(Current))
        return false;
      const auto *Parent = cast<CXXRecordDecl>(Current->getDeclContext());
      const auto *Next = Current->getInstantiatedFromMemberTemplate();
      if (!Next) {
        if (ordinaryCopiedBody(Parent))
          return false;
        const auto *Instance = dyn_cast<ClassTemplateSpecializationDecl>(Parent);
        // An alias written in a full class specialization owns its definition.
        // Every implicitly copied alias must retain its actual written origin.
        return !Instance || Instance->getKind() == Decl::ClassTemplatePartialSpecialization ||
               Instance->isExplicitSpecialization();
      }
      if (!memberAliasDeclarationShape(Next) ||
          Current->getDeclName() != Next->getDeclName() ||
          Current->getTemplateParameters()->size() != Next->getTemplateParameters()->size())
        return false;
      const auto *NextParent = cast<CXXRecordDecl>(Next->getDeclContext());
      const auto *Selected = classBodyRecord(Parent);
      if (Parent->getCanonicalDecl() != NextParent->getCanonicalDecl() &&
          (!Selected || Selected->getCanonicalDecl() != NextParent->getCanonicalDecl()))
        return false;
      Current = Next->getCanonicalDecl();
    }
    return false;
  }
  bool friendFunctionSourceShape(const FunctionDecl *F) {
    if (!owned(F) || F->getKind() != Decl::Function || F->isInvalidDecl() ||
        !supportedDeclarationAttributes(F) || (!F->getIdentifier() &&
                         !ordinaryOperatorKind(F->getOverloadedOperator())) ||
        !F->getDeclContext()->getRedeclContext()->isFileContext() ||
        F->getDescribedFunctionTemplate() || F->getPrimaryTemplate() ||
        F->getNumTemplateParameterLists() || F->isVariadic() ||
        F->isExplicitlyDefaulted() || F->isConsteval() ||
        F->getTrailingRequiresClause() || !F->getTypeSourceInfo() || F->getType().isNull())
      return false;
    for (const auto *Parameter : F->parameters()) {
      A.chargeExpansion(1, Parameter->getLocation());
      if (!owned(Parameter) || Parameter->isInvalidDecl() || Parameter->hasAttrs() ||
          Parameter->hasUnparsedDefaultArg())
        return false;
    }
    return true;
  }
  bool genericFriendFunctionShape(const FriendDecl *D) {
    if (!owned(D) || D->isInvalidDecl() || D->hasAttrs() ||
        D->isUnsupportedFriend() || D->getFriendType() || D->isPackExpansion() ||
        D->getFriendTypeNumTemplateParameterLists())
      return false;
    const auto *Parent = dyn_cast<CXXRecordDecl>(D->getDeclContext());
    const auto *Function = dyn_cast_or_null<FunctionDecl>(D->getFriendDecl());
    if (!owned(Parent) || Parent->isInvalidDecl() || hasNonFinalAttributes(Parent) ||
        !Parent->getIdentifier() || Parent->isUnion() || Parent->isLambda() ||
        Parent->isLocalClass() || !classOwnerScope(Parent->getDeclContext()) ||
        !friendFunctionSourceShape(Function) || !Function->getFriendObjectKind() ||
        Function->getLexicalDeclContext() != Parent)
      return false;
    return Function->getTemplatedKind() == FunctionDecl::TK_NonTemplate ||
           (Function->getTemplatedKind() == FunctionDecl::TK_MemberSpecialization &&
            Function->getMemberSpecializationInfo() &&
            Function->getInstantiatedFromMemberFunction());
  }

  bool friendFunctionTemplateDeclarationShape(const FunctionTemplateDecl *D) {
    if (!owned(D) || D->isInvalidDecl() || D->hasAttrs() ||
        D->isAbbreviated() || D->hasAssociatedConstraints())
      return false;
    const auto *Function = D->getTemplatedDecl();
    const auto *Parent = dyn_cast<CXXRecordDecl>(D->getLexicalDeclContext());
    if (!owned(Parent) || Parent->isInvalidDecl() || hasNonFinalAttributes(Parent) ||
        !Parent->getIdentifier() || Parent->isUnion() || Parent->isLambda() ||
        Parent->isLocalClass() || !classOwnerScope(Parent->getDeclContext()) ||
        !D->getDeclContext()->getRedeclContext()->isFileContext() ||
        !ordinaryFreeFunctionName(Function) || !owned(Function) ||
        Function->isInvalidDecl() || !supportedDeclarationAttributes(Function) ||
        !Function->getFriendObjectKind() ||
        Function->getDescribedFunctionTemplate() != D ||
        Function->getLexicalDeclContext() != Parent ||
        Function->getDeclContext() != D->getDeclContext() ||
        Function->getPrimaryTemplate() || Function->getNumTemplateParameterLists() ||
        Function->isVariadic() ||
        Function->isDefaulted() || Function->isConsteval() ||
        Function->getTrailingRequiresClause() || !Function->getTypeSourceInfo() ||
        Function->getType().isNull() ||
        !templateParametersShape(D->getTemplateParameters(), templateSourceParameterDepth(D)))
      return false;
    for (const auto *Parameter : Function->parameters()) {
      A.chargeExpansion(1, Parameter->getLocation());
      if (!owned(Parameter) || Parameter->isInvalidDecl() || Parameter->hasAttrs() ||
          Parameter->hasUnparsedDefaultArg())
        return false;
    }
    return true;
  }
  bool genericFriendTemplateShape(const FriendDecl *D) {
    if (!owned(D) || D->isInvalidDecl() || D->hasAttrs() ||
        D->isUnsupportedFriend() || D->getFriendType() || D->isPackExpansion() ||
        D->getFriendTypeNumTemplateParameterLists())
      return false;
    const auto *Template = dyn_cast_or_null<FunctionTemplateDecl>(D->getFriendDecl());
    return friendFunctionTemplateDeclarationShape(Template) &&
           Template->getLexicalDeclContext() == D->getDeclContext();
  }
  bool friendTemplateSourceIdentity(const FriendDecl *D) {
    auto Found = FriendDeclarationSources.find(D);
    const auto *Template = D ? dyn_cast_or_null<FunctionTemplateDecl>(D->getFriendDecl()) : nullptr;
    auto Event = FriendTemplateSources.find(Template);
    if (Found == FriendDeclarationSources.end() || Event == FriendTemplateSources.end() ||
        !genericFriendTemplateShape(D))
      return false;
    const auto *Written = Found->second->Written;
    const auto &Source = *Event->second;
    const auto *IncomingTemplate = Source.Incoming->getDescribedFunctionTemplate();
    const auto *SelectedTemplate = Source.Selected->getDescribedFunctionTemplate();
    const auto *Parent = cast<CXXRecordDecl>(D->getDeclContext());
    const auto *Body = classBodyRecord(Parent);
    if (!genericFriendTemplateShape(Written) || !WrittenFriendDeclarations.count(Written) ||
        Found->second->Declaration != D || Source.Template != Template ||
        Source.GrantingClass != Parent || IncomingTemplate != Written->getFriendDecl() ||
        !owned(Source.Incoming) || !owned(Source.Selected) || !owned(SelectedTemplate) ||
        Source.Incoming->isInvalidDecl() || Source.Selected->isInvalidDecl() ||
        !owned(Body) || Body->getCanonicalDecl() !=
            cast<CXXRecordDecl>(Written->getDeclContext())->getCanonicalDecl() ||
        D->getFriendLoc() != Written->getFriendLoc() ||
        Template->getDeclName() != Source.Incoming->getDeclName() ||
        (Source.Selected != Source.Incoming && !normalizedFriendSourceReady(Source.Incoming)))
      return false;
    bool Selected = false;
    unsigned Count = 0;
    for (const auto *Declaration : Source.Incoming->redecls()) {
      A.chargeExpansion(1, Declaration->getLocation());
      if (++Count > 64)
        return false;
      Selected |= Declaration == Source.Selected;
    }
    // FT origin metadata is shared across redeclarations. A declaration-only
    // friend can acquire it later, so this exact event is the source authority.
    return Selected && SelectedTemplate->getCanonicalDecl() == IncomingTemplate->getCanonicalDecl();
  }
  void indexFriendTemplateIdentities() {
    for (const auto &[Template, Source] : FriendTemplateSources) {
      auto Declaration = FriendTemplateDeclarations.find(Template);
      if (Declaration == FriendTemplateDeclarations.end() ||
          !friendTemplateSourceIdentity(Declaration->second)) {
        A.reject(Template->getLocation(), "friend template source",
                 "Each copied primary requires its exact written friend and class substitution.");
        return;
      }
      indexTemplatePackSources(Template);
      const auto *Canonical = Template->getCanonicalDecl();
      // A genuinely written namespace/friend declaration can share this
      // canonical template. Preserve that independently indexed identity.
      if (IndependentTemplateIdentities.count(Canonical))
        continue;
      auto CanonicalEvent = FriendTemplateSources.find(Canonical);
      if (CanonicalEvent == FriendTemplateSources.end()) {
        A.reject(Template->getLocation(), "friend template canonical source",
                 "The canonical copied primary must retain its own substitution event.");
        return;
      }
      const auto *Origin = CanonicalEvent->second->Incoming->getDescribedFunctionTemplate();
      auto Ordinal = Origin ? A.TemplateOrdinals.find(Origin->getCanonicalDecl())
                            : A.TemplateOrdinals.end();
      if (Ordinal == A.TemplateOrdinals.end()) {
        A.reject(Template->getLocation(), "friend template source ordinal",
                 "The actual canonical copy requires a previously indexed written primary.");
        return;
      }
      const auto *Owner = CanonicalEvent->second->GrantingClass->getCanonicalDecl();
      auto Inserted = A.TemplateOrdinals.emplace(Canonical, Ordinal->second);
      auto Identity = A.FriendTemplateIdentityOwners.emplace(Canonical, Owner);
      if ((!Inserted.second && Inserted.first->second != Ordinal->second) ||
          (!Identity.second && Identity.first->second != Owner)) {
        A.reject(Template->getLocation(), "friend template identity conflict",
                 "A copied canonical primary must keep its proven source ordinal and outer class.");
        return;
      }
    }
    // Corroborate a shared origin against the whole exact-event family, never
    // against a declaration-only friend's final null/non-null state.
    for (const auto &[Template, Source] : FriendTemplateSources) {
      const auto *Origin = Template->getInstantiatedFromMemberTemplate();
      if (!Origin)
        continue;
      bool Related = false;
      auto Family = FriendTemplateFamilies.find(Template->getCanonicalDecl());
      if (Family != FriendTemplateFamilies.end())
        for (const auto *Other : Family->second) {
          A.chargeExpansion(1, Other->Template->getLocation());
          for (const auto *Function : {Other->Incoming, Other->Selected})
            if (const auto *Pattern = Function->getDescribedFunctionTemplate())
              Related |= Pattern->getCanonicalDecl() == Origin->getCanonicalDecl();
        }
      if (!Related) {
        A.reject(Template->getLocation(), "friend template shared origin",
                 "Shared origin metadata requires a matching actual declaration event.");
        return;
      }
    }
  }
  bool friendTemplateBodyIdentity(const FunctionTemplateBodySource &Source) {
    const auto *Function = Source.Function;
    const auto *Compatible = Source.Compatible;
    const auto *Primary = Function->getPrimaryTemplate();
    if (!concreteFreeFunctionTemplate(Function) || !owned(Function) ||
        Function->isInvalidDecl() || !Function->doesThisDeclarationHaveABody() ||
        !owned(Compatible) || Compatible->isInvalidDecl() || !owned(Source.Pattern) ||
        Source.Pattern->isInvalidDecl() || !Source.Pattern->doesThisDeclarationHaveABody() ||
        !Primary || Compatible->getCanonicalDecl() != Primary->getCanonicalDecl() ||
        !Compatible->isCompatibleWithDefinition())
      return false;
    const auto *Definition = Compatible->getTemplatedDecl()->getDefinition();
    const auto *Context = Definition ? Definition->getLexicalDeclContext()
                                     : Compatible->getLexicalDeclContext();
    if (Source.LexicalContext != Context)
      return false;
    // Follow only real origin edges to the selected body. The compatible
    // declaration's actual lexical context is retained independently above.
    std::set<const FunctionTemplateDecl *> Seen;
    auto *Pattern = Primary;
    while (Pattern && !Pattern->isMemberSpecialization()) {
      A.chargeExpansion(1, Pattern->getLocation());
      if (Seen.size() >= 64 || !Seen.insert(Pattern->getCanonicalDecl()).second || !owned(Pattern))
        return false;
      const auto *Next = Pattern->getInstantiatedFromMemberTemplate();
      if (!Next)
        break;
      Pattern = Next;
    }
    if (!Pattern)
      return false;
    const auto *Selected = Pattern->getTemplatedDecl()->getDefinition();
    return Selected == Source.Pattern;
  }

  bool copiedFriendTemplate(const FriendDecl *D) {
    if (!D || !isa_and_nonnull<FunctionTemplateDecl>(D->getFriendDecl()))
      return false;
    const auto *Parent = dyn_cast<CXXRecordDecl>(D->getDeclContext());
    const auto *Body = classBodyRecord(Parent);
    return Parent && Body && Parent->getCanonicalDecl() != Body->getCanonicalDecl();
  }
  bool checkFriendTemplateFunction(const FunctionDecl *Function) {
    const auto *Primary = Function ? Function->getPrimaryTemplate() : nullptr;
    if (!concreteFreeFunctionTemplate(Function) || !Primary ||
        !FriendTemplateCanonicals.count(Primary->getCanonicalDecl()))
      return true;
    if (!CheckedFriendTemplateFunctions.insert(Function).second)
      return true;
    unsigned Count = 0;
    for (const auto *Declaration : Primary->redecls()) {
      const auto *Template = cast<FunctionTemplateDecl>(Declaration);
      A.chargeExpansion(1, Template->getLocation());
      if (++Count > 64) {
        A.reject(Function->getLocation(), "friend template declaration depth",
                 "A selected friend template needs bounded source declarations.");
        return true;
      }
      if (auto Source = FriendTemplateDeclarations.find(Template);
          Source != FriendTemplateDeclarations.end()) {
        if (!friendTemplateSourceIdentity(Source->second)) {
          A.reject(Function->getLocation(), "selected friend template source",
                   "Each selected copied primary must retain its actual source declaration.");
          return true;
        }
        const auto *Written = FriendDeclarationSources.find(Source->second)->second->Written;
        if (!traverseWrittenFriendSignature(Written) ||
            !traverseWrittenFriendSignature(Source->second))
          return false;
      } else if (auto Written = WrittenFriendTemplates.find(Template);
                 Written != WrittenFriendTemplates.end()) {
        if (!traverseWrittenFriendSignature(Written->second))
          return false;
      } else if (isa<CXXRecordDecl>(Template->getLexicalDeclContext())) {
        A.reject(Function->getLocation(), "missing friend template declaration",
                 "A friend primary requires its written or exact copied declaration.");
        return true;
      }
    }
    if (!Function->doesThisDeclarationHaveABody() ||
        Function->getTemplateSpecializationKind() == TSK_ExplicitSpecialization)
      return true;
    auto Body = TemplateBodySources.find(Function);
    if (Body == TemplateBodySources.end() || !friendTemplateBodyIdentity(*Body->second)) {
      A.reject(Function->getLocation(), "friend template body source",
               "The selected body requires its exact compatible declaration, pattern and lexical context.");
      return true;
    }
    if (const auto *Parent = dyn_cast<CXXRecordDecl>(Body->second->LexicalContext))
      if (!owned(Parent) || Parent->isInvalidDecl() || hasNonFinalAttributes(Parent) ||
          !Parent->getIdentifier() || Parent->isUnion() || Parent->isLocalClass() ||
          Parent->isLambda() || !classOwnerScope(Parent->getDeclContext()) ||
          !checkClassFullOwnerSources(Parent)) {
        A.reject(Function->getLocation(), "friend template body owner",
                 "The instantiated body needs its actual supported granting-class source.");
        return true;
      }
    return true;
  }

  bool friendClassTemplateDeclarationShape(const ClassTemplateDecl *D) {
    if (!owned(D) || D->isInvalidDecl() || D->hasAttrs() ||
        !D->getFriendObjectKind() || D->hasAssociatedConstraints() ||
        !classOwnerScope(D->getDeclContext()) ||
        !templateParametersShape(D->getTemplateParameters(), templateSourceParameterDepth(D)))
      return false;
    const auto *Parent = dyn_cast<CXXRecordDecl>(D->getLexicalDeclContext());
    const auto *Pattern = D->getTemplatedDecl();
    return owned(Parent) && !Parent->isInvalidDecl() && !hasNonFinalAttributes(Parent) &&
           Parent->getIdentifier() && !Parent->isUnion() && !Parent->isLocalClass() &&
           !Parent->isLambda() && classOwnerScope(Parent->getDeclContext()) &&
           owned(Pattern) && !Pattern->isInvalidDecl() && !hasNonFinalAttributes(Pattern) &&
           Pattern->getKind() == Decl::CXXRecord && Pattern->getIdentifier() &&
           !Pattern->isUnion() && !Pattern->isLocalClass() && !Pattern->isLambda() &&
           !Pattern->isCompleteDefinition() && !Pattern->getNumTemplateParameterLists() &&
           Pattern->getDescribedClassTemplate() == D &&
           Pattern->getLexicalDeclContext() == Parent &&
           Pattern->getDeclContext() == D->getDeclContext() &&
           (!Pattern->getQualifierLoc() ||
            A.S.owns(A.Sources, Pattern->getQualifierLoc().getBeginLoc()));
  }
  bool genericFriendClassTemplateShape(const FriendDecl *D) {
    if (!owned(D) || D->isInvalidDecl() || D->hasAttrs() || D->isUnsupportedFriend() ||
        D->isPackExpansion() || D->getFriendType() ||
        D->getFriendTypeNumTemplateParameterLists())
      return false;
    const auto *Template = dyn_cast_or_null<ClassTemplateDecl>(D->getFriendDecl());
    return friendClassTemplateDeclarationShape(Template) &&
           Template->getLexicalDeclContext() == D->getDeclContext();
  }
  bool copiedFriendClassTemplate(const FriendDecl *D) {
    if (!D || !isa_and_nonnull<ClassTemplateDecl>(D->getFriendDecl()))
      return false;
    const auto *Parent = dyn_cast<CXXRecordDecl>(D->getDeclContext());
    const auto *Body = classBodyRecord(Parent);
    return Parent && Body && Parent->getCanonicalDecl() != Body->getCanonicalDecl();
  }
  // Pinned Sema skips this language check in dependent granting contexts.
  // Run on the exact lexical original before any dependent-source deferral.
  bool checkWrittenFriendClassDefaults(const ClassTemplateDecl *Template) {
    if (!Template || !Template->getTemplateParameters() ||
        Template->getTemplateParameters()->size() > 64)
      return false;
    for (const auto *Parameter : *Template->getTemplateParameters()) {
      A.chargeExpansion(1, Parameter->getLocation());
      const auto *Type = dyn_cast<TemplateTypeParmDecl>(Parameter);
      const auto *Value = dyn_cast<NonTypeTemplateParmDecl>(Parameter);
      if ((Type && Type->hasDefaultArgument() && !Type->defaultArgumentWasInherited()) ||
          (Value && Value->hasDefaultArgument() && !Value->defaultArgumentWasInherited())) {
        A.reject(Parameter->getLocation(), "friend class template default",
                 "A friend class-template declaration cannot introduce default template arguments.",
                 "TR0202");
        return false;
      }
    }
    return true;
  }
  bool friendClassSourceIdentity(const FriendDecl *D) {
    auto Pair = FriendDeclarationSources.find(D);
    const auto *Template = D ? dyn_cast_or_null<ClassTemplateDecl>(D->getFriendDecl()) : nullptr;
    auto Event = FriendClassSources.find(Template);
    if (Pair == FriendDeclarationSources.end() || Event == FriendClassSources.end() ||
        !genericFriendClassTemplateShape(D))
      return false;
    const auto &Source = *Event->second;
    const auto *Written = Pair->second->Written;
    const auto *Parent = cast<CXXRecordDecl>(D->getDeclContext());
    const auto *Body = classBodyRecord(Parent);
    if (Pair->second->Declaration != D || !genericFriendClassTemplateShape(Written) ||
        !WrittenFriendDeclarations.count(Written) || Source.Template != Template ||
        Source.Written != Written->getFriendDecl() || Source.GrantingClass != Parent ||
        Parent->isDependentContext() || !owned(Body) ||
        Body->getCanonicalDecl() !=
            cast<CXXRecordDecl>(Written->getDeclContext())->getCanonicalDecl() ||
        D->getFriendLoc() != Written->getFriendLoc() ||
        Template->getDeclName() != Source.Written->getDeclName() ||
        Template->getDeclContext() != Source.Context ||
        Template->getTemplateParameters()->size() != Source.Written->getTemplateParameters()->size())
      return false;
    const auto *Previous = Source.Previous;
    const auto *LinkedPrevious = Template->getPreviousDecl();
    if (!Previous)
      return !LinkedPrevious && !Template->getTemplatedDecl()->getQualifier() &&
             isa<TranslationUnitDecl, NamespaceDecl>(Source.Context) &&
             Template == Template->getCanonicalDecl();
    if (!owned(Previous) || Previous->isInvalidDecl() || !owned(LinkedPrevious) ||
        !Previous->getDeclContext()->getRedeclContext()->Equals(Source.Context->getRedeclContext()) ||
        Previous->getCanonicalDecl() != Template->getCanonicalDecl() ||
        Previous->getDeclName() != Template->getDeclName() ||
        Template->getTemplatedDecl()->getPreviousDecl() != LinkedPrevious->getTemplatedDecl())
      return false;
    // Lookup may return an earlier visible declaration. Clang links both the
    // template and its record to the most recent redeclaration instead. Keep
    // the exact lookup result and prove it belongs to the preceding chain.
    std::set<const ClassTemplateDecl *> Seen;
    for (auto *Current = LinkedPrevious; Current; Current = Current->getPreviousDecl()) {
      A.chargeExpansion(1, Current->getLocation());
      if (Seen.size() >= 64 || !Seen.insert(Current).second || !owned(Current) ||
          Current->isInvalidDecl() || Current->getCanonicalDecl() != Template->getCanonicalDecl())
        return false;
      if (Current == Previous)
        return true;
    }
    return false;
  }
  bool admittedFriendClassDeclaration(const ClassTemplateDecl *Template) {
    if (!friendClassTemplateDeclarationShape(Template))
      return false;
    if (auto Original = WrittenFriendClasses.find(Template); Original != WrittenFriendClasses.end())
      return !copiedFriendClassTemplate(Original->second) &&
             checkWrittenFriendClassDefaults(Template);
    auto Copy = FriendClassDeclarations.find(Template);
    return Copy != FriendClassDeclarations.end() && friendClassSourceIdentity(Copy->second);
  }
  void indexFriendClassSources() {
    for (const auto &[Template, Source] : FriendClassSources) {
      auto Pair = FriendClassDeclarations.find(Template);
      if (Pair == FriendClassDeclarations.end() || !friendClassSourceIdentity(Pair->second)) {
        A.reject(Template->getLocation(), "friend class template source",
                 "A copied class target requires its exact written friend, grant and lookup result.");
        return;
      }
      if (!checkWrittenFriendClassDefaults(Source->Written))
        return;
      indexTemplatePackSources(Template);
    }
  }
  bool friendClassDefinitionShape(const ClassTemplateDecl *Template) {
    if (!Template || !FriendClassCanonicals.count(Template->getCanonicalDecl()))
      return true;
    // A forward friend never owns the target's body. Check the real definition
    // on the shared target chain without importing the granting source's slots.
    const auto *Record = Template->getTemplatedDecl()->getDefinition();
    if (!Record)
      return true;
    const auto *Definition = Record->getDescribedClassTemplate();
    return owned(Definition) && !Definition->getFriendObjectKind() &&
           Definition->getCanonicalDecl() == Template->getCanonicalDecl() &&
           classTemplateDeclarationShape(Definition) && WrittenClassTemplates.count(Definition);
  }
  bool traverseFriendClassTemplateSource(const FriendDecl *D) {
    if (!genericFriendClassTemplateShape(D)) {
      A.reject(D->getFriendLoc(), "friend class template",
               "An owned class-template target in an admitted granting class is required.");
      return true;
    }
    if (!CheckedFriendDeclarations.insert(D).second)
      return true;
    const auto *Template = cast<ClassTemplateDecl>(D->getFriendDecl());
    auto Pair = FriendDeclarationSources.find(D);
    if (Pair != FriendDeclarationSources.end()) {
      if (!friendClassSourceIdentity(D)) {
        A.reject(D->getFriendLoc(), "copied friend class template",
                 "Each substituted grant requires its exact written and target events.");
        return true;
      }
      if (!traverseFriendClassTemplateSource(Pair->second->Written))
        return false;
    } else if (!WrittenFriendClasses.count(Template) || copiedFriendClassTemplate(D)) {
      A.reject(D->getFriendLoc(), "missing friend class template source",
               "A class friend requires its written declaration or exact copied event.");
      return true;
    } else if (!checkWrittenFriendClassDefaults(Template)) {
      return true;
    }
    if (!A.S.Diagnostics.empty())
      return true;
    if (DefinitionFrames.size() >= 64) {
      A.reject(D->getFriendLoc(), "friend class source depth",
               "Friend class-template headers require a bounded source context.");
      return true;
    }
    auto *SavedFunction = CurrentFunction;
    auto *SavedMethod = CurrentMethod;
    auto *SavedField = CurrentDefaultField;
    auto SavedOwner = ImplicitInitializerOwner;
    auto *SavedParameterSource = TemplateParameterTypeSource;
    CurrentFunction = nullptr;
    CurrentMethod = nullptr;
    CurrentDefaultField = nullptr;
    TemplateParameterTypeSource = nullptr;
    ImplicitInitializerOwner = D->getFriendLoc();
    DefinitionFrames.push_back({D, nullptr, nullptr, TemplateFrames.size()});
    auto Restore = llvm::make_scope_exit([&] {
      DefinitionFrames.pop_back();
      CurrentFunction = SavedFunction;
      CurrentMethod = SavedMethod;
      CurrentDefaultField = SavedField;
      ImplicitInitializerOwner = SavedOwner;
      TemplateParameterTypeSource = SavedParameterSource;
    });
    // The actual copy has already substituted outer levels. Its own inner
    // parameters stay lazy, while every nondependent written type is checked.
    return traverseTemplateParameterSource(Template->getTemplateParameters(), true) &&
           (!Template->getTemplatedDecl()->getQualifier() ||
            Template->getTemplatedDecl()->getQualifier()->isDependent() ||
            TraverseNestedNameSpecifierLoc(Template->getTemplatedDecl()->getQualifierLoc()));
  }

  bool genericFriendTypeShape(const FriendDecl *D) {
    if (!owned(D) || D->isInvalidDecl() || D->hasAttrs() ||
        D->isUnsupportedFriend() || D->isPackExpansion() ||
        D->getFriendTypeNumTemplateParameterLists() || D->getFriendDecl())
      return false;
    const auto *Type = D->getFriendType();
    const auto *Parent = dyn_cast<CXXRecordDecl>(D->getDeclContext());
    return Type && !Type->getType().isNull() &&
           A.S.owns(A.Sources, Type->getTypeLoc().getBeginLoc()) &&
           owned(Parent) && !Parent->isInvalidDecl() && !hasNonFinalAttributes(Parent) &&
           Parent->getIdentifier() && !Parent->isUnion() && !Parent->isLambda() &&
           !Parent->isLocalClass() && classOwnerScope(Parent->getDeclContext());
  }
  bool copiedFriendType(const FriendDecl *D) {
    if (!D || !D->getFriendType())
      return false;
    const auto *Parent = dyn_cast<CXXRecordDecl>(D->getDeclContext());
    const auto *Body = classBodyRecord(Parent);
    return Parent && Body && Body->getCanonicalDecl() != Parent->getCanonicalDecl();
  }
  bool genericFriendDeclarationShape(const FriendDecl *D) {
    return genericFriendFunctionShape(D) || genericFriendTypeShape(D) ||
           genericFriendTemplateShape(D) || genericFriendClassTemplateShape(D);
  }
  bool friendTypeSourceIdentity(const FriendDecl *D) {
    auto Found = FriendDeclarationSources.find(D);
    if (Found == FriendDeclarationSources.end() || !genericFriendTypeShape(D))
      return false;
    const auto *Written = Found->second->Written;
    const auto *Parent = dyn_cast<CXXRecordDecl>(D->getDeclContext());
    const auto *Body = classBodyRecord(Parent);
    return Found->second->Declaration == D && genericFriendTypeShape(Written) &&
           WrittenFriendDeclarations.count(Written) && owned(Body) &&
           D->getFriendLoc() == Written->getFriendLoc() &&
           Body->getCanonicalDecl() ==
               cast<CXXRecordDecl>(Written->getDeclContext())->getCanonicalDecl();
  }
  bool traverseFriendTypeSource(const FriendDecl *D) {
    if (!genericFriendTypeShape(D)) {
      A.reject(D->getFriendLoc(), "friend type source",
               "An ordinary source-owned friend type and granting class are required.");
      return true;
    }
    auto Found = FriendDeclarationSources.find(D);
    if (Found != FriendDeclarationSources.end()) {
      if (!friendTypeSourceIdentity(D)) {
        A.reject(D->getFriendLoc(), "copied friend type source",
                 "A substituted friend type requires its exact written declaration and selected class body.");
        return true;
      }
    } else if (copiedFriendType(D) || !WrittenFriendDeclarations.count(D)) {
      // Types have no member-specialization function metadata to catch a lost
      // event. The copied granting body must not fall through ordinary friends.
      A.reject(D->getFriendLoc(), "missing friend type source",
               "A copied type friend requires its retained declaration event.");
      return true;
    }
    if (!CheckedFriendDeclarations.insert(D).second)
      return true;
    if (Found != FriendDeclarationSources.end() &&
        !traverseFriendTypeSource(Found->second->Written))
      return false;
    if (!A.S.Diagnostics.empty())
      return true;
    const auto *Info = D->getFriendType();
    const auto Type = Info->getType();
    if (Type->isDependentType() || Type->isInstantiationDependentType()) {
      if (!D->getDeclContext()->isDependentContext())
        A.reject(D->getFriendLoc(), "unresolved friend type",
                 "A concrete granting class requires its actual resolved friend type.");
      return true;
    }
    if (DefinitionFrames.size() >= 64) {
      A.reject(D->getFriendLoc(), "friend type depth", "Friend type source exceeds the bounded context depth.");
      return true;
    }
    auto *SavedFunction = CurrentFunction;
    auto *SavedMethod = CurrentMethod;
    auto *SavedField = CurrentDefaultField;
    auto SavedOwner = ImplicitInitializerOwner;
    auto *SavedParameterSource = TemplateParameterTypeSource;
    CurrentFunction = nullptr;
    CurrentMethod = nullptr;
    CurrentDefaultField = nullptr;
    TemplateParameterTypeSource = nullptr;
    ImplicitInitializerOwner = D->getFriendLoc();
    DefinitionFrames.push_back({D, nullptr, nullptr, TemplateFrames.size()});
    auto Restore = llvm::make_scope_exit([&] {
      DefinitionFrames.pop_back();
      CurrentFunction = SavedFunction;
      CurrentMethod = SavedMethod;
      CurrentDefaultField = SavedField;
      ImplicitInitializerOwner = SavedOwner;
      TemplateParameterTypeSource = SavedParameterSource;
    });
    A.type(Type, D->getFriendLoc(), true);
    if (!A.S.Diagnostics.empty())
      return true;
    if (!TraverseTypeLoc(Info->getTypeLoc()))
      return false;
    // RAV also visits an elaborated friend type's owned namespace tag.
    if (const auto *Elaborated = Type->getAs<ElaboratedType>())
      return TraverseDecl(Elaborated->getOwnedTagDecl());
    return true;
  }

  bool classTemplateMembers(const CXXRecordDecl *D) {
    for (const auto *Member : D->decls()) {
      A.chargeExpansion(1, Member->getLocation());
      if (Member->isImplicit())
        continue;
      if (!owned(Member) || Member->isInvalidDecl() || hasNonFinalAttributes(Member) ||
          (!isa<FieldDecl, TypedefNameDecl, EnumDecl, EnumConstantDecl,
                StaticAssertDecl, AccessSpecDecl, EmptyDecl>(Member) &&
           !classTemplateStaticDataShape(dyn_cast<VarDecl>(Member)) &&
           !classTemplateFunctionShape(dyn_cast<CXXMethodDecl>(Member)) &&
           !genericFriendDeclarationShape(dyn_cast<FriendDecl>(Member)) &&
           !memberTemplateShape(dyn_cast<FunctionTemplateDecl>(Member)) &&
           !memberAliasShape(dyn_cast<TypeAliasTemplateDecl>(Member)) &&
           !classTemplateShape(dyn_cast<ClassTemplateDecl>(Member)) &&
           !classPartialShape(dyn_cast<ClassTemplatePartialSpecializationDecl>(Member)) &&
           !classFullBodyShape(dyn_cast<ClassTemplateSpecializationDecl>(Member)) &&
           !ordinaryClassBodyShape(dyn_cast<CXXRecordDecl>(Member)) &&
           !(isa<ClassTemplateSpecializationDecl>(Member) &&
             !isa<ClassTemplatePartialSpecializationDecl>(Member) &&
             cast<ClassTemplateSpecializationDecl>(Member)->isExplicitSpecialization() &&
             !cast<ClassTemplateSpecializationDecl>(Member)->isDependentContext() &&
             memberClassDeclarationShape(cast<ClassTemplateSpecializationDecl>(Member), true)) &&
           !variablePatternShape(dyn_cast<NamedDecl>(Member)) &&
           !(isa<VarTemplateSpecializationDecl>(Member) &&
             variablePatternType(cast<VarTemplateSpecializationDecl>(Member)) &&
             variableTemplateShape(cast<VarTemplateSpecializationDecl>(Member)->getSpecializedTemplate())) &&
           !(concreteMemberFunctionTemplate(dyn_cast<FunctionDecl>(Member)) &&
             memberTemplateShape(cast<FunctionDecl>(Member)->getPrimaryTemplate()))))
        return false;
    }
    return true;
  }
  bool classTemplateDeclarationShape(const ClassTemplateDecl *D) {
    if (D && D->getFriendObjectKind() && isa<CXXRecordDecl>(D->getLexicalDeclContext()))
      return admittedFriendClassDeclaration(D);
    if (!D || !owned(D) || D->isInvalidDecl() || D->hasAttrs() ||
        D->getFriendObjectKind() || D->hasAssociatedConstraints() ||
        !classOwnerScope(D->getDeclContext()) ||
        !templateParametersShape(D->getTemplateParameters(), templateSourceParameterDepth(D)))
      return false;
    const auto *Pattern = D->getTemplatedDecl();
    return owned(Pattern) && !Pattern->isInvalidDecl() && !hasNonFinalAttributes(Pattern) &&
           Pattern->getKind() == Decl::CXXRecord && Pattern->getIdentifier() &&
           !Pattern->isUnion() && !Pattern->isLambda() && !Pattern->isLocalClass() &&
           Pattern->getDescribedClassTemplate() == D &&
           Pattern->getDeclContext() == D->getDeclContext() &&
           Pattern->getLexicalDeclContext() == D->getLexicalDeclContext() &&
           (D->getLexicalDeclContext() == D->getDeclContext() ||
            isa<TranslationUnitDecl, NamespaceDecl>(D->getLexicalDeclContext())) &&
           (!isa<CXXRecordDecl>(D->getDeclContext()) || memberClassDeclarationShape(Pattern));
  }
  bool classPartialDeclarationShape(const ClassTemplatePartialSpecializationDecl *D) {
    if (!D || !owned(D) || D->isInvalidDecl() || hasNonFinalAttributes(D) ||
        !D->getIdentifier() || D->isUnion() || D->getFriendObjectKind() ||
        D->hasAssociatedConstraints() ||
        !classOwnerScope(D->getDeclContext()) ||
        (D->getLexicalDeclContext() != D->getDeclContext() &&
         !isa<TranslationUnitDecl, NamespaceDecl>(D->getLexicalDeclContext())) ||
        (isa<CXXRecordDecl>(D->getDeclContext()) && !memberClassDeclarationShape(D)) ||
        !classTemplateDeclarationShape(D->getSpecializedTemplate()) ||
        D->getDeclContext() != D->getSpecializedTemplate()->getDeclContext() ||
        !templateParametersShape(D->getTemplateParameters(), templateSourceParameterDepth(D)) ||
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
    return true;
  }
  bool classPatternDeclarationShape(const NamedDecl *D) {
    return classTemplateDeclarationShape(dyn_cast_or_null<ClassTemplateDecl>(D)) ||
           classPartialDeclarationShape(dyn_cast_or_null<ClassTemplatePartialSpecializationDecl>(D));
  }
  bool writtenOwnClassPattern(const NamedDecl *Owner) {
    // The canonical member-specialization bit is shared with an earlier hidden
    // copy. Require a lexical declaration that actually spells an outer header.
    auto Written = [&](const CXXRecordDecl *Record, bool Indexed) {
      return Indexed && owned(Record) && !Record->isInvalidDecl() &&
             !hasNonFinalAttributes(Record) && !Record->getFriendObjectKind() &&
             Record->getNumTemplateParameterLists() && outerTemplateListsShape(Record);
    };
    if (const auto *Primary = dyn_cast<ClassTemplateDecl>(Owner)) {
      for (const auto *D : Primary->redecls()) {
        A.chargeExpansion(1, D->getLocation());
        const auto *Template = dyn_cast<ClassTemplateDecl>(D);
        if (Template && Written(Template->getTemplatedDecl(), WrittenClassTemplates.count(Template)))
          return true;
      }
    } else if (const auto *Partial = dyn_cast<ClassTemplatePartialSpecializationDecl>(Owner)) {
      for (const auto *D : Partial->redecls()) {
        A.chargeExpansion(1, D->getLocation());
        if (Written(dyn_cast<CXXRecordDecl>(D), WrittenPartialDeclarations.count(D)))
          return true;
      }
    }
    return false;
  }
  bool classPatternOriginShape(const NamedDecl *D) {
    std::set<const NamedDecl *> Seen;
    for (auto *Current = D ? cast<NamedDecl>(D->getCanonicalDecl()) : nullptr; Current;) {
      A.chargeExpansion(1, Current->getLocation());
      if (Seen.size() >= 64 || !Seen.insert(Current).second ||
          !classPatternDeclarationShape(Current))
        return false;
      const NamedDecl *Next = nullptr;
      bool Own = false;
      if (const auto *Primary = dyn_cast<ClassTemplateDecl>(Current)) {
        Next = Primary->getInstantiatedFromMemberTemplate();
        Own = Primary->isMemberSpecialization();
      } else {
        const auto *Partial = cast<ClassTemplatePartialSpecializationDecl>(Current);
        Next = Partial->getInstantiatedFromMember();
        Own = Partial->isMemberSpecialization();
      }
      if (Own)
        return writtenOwnClassPattern(Current);
      const auto *Parent = dyn_cast<CXXRecordDecl>(Current->getDeclContext());
      if (!Next) {
        if (ordinaryCopiedBody(Parent))
          return writtenOwnClassPattern(Current);
        const auto *Instance = dyn_cast_or_null<ClassTemplateSpecializationDecl>(Parent);
        return !Instance || isa<ClassTemplatePartialSpecializationDecl>(Instance) ||
               Instance->isExplicitSpecialization() || writtenOwnClassPattern(Current);
      }
      if (!Parent || !classPatternDeclarationShape(Next) ||
          Current->getKind() != Next->getKind() || Current->getDeclName() != Next->getDeclName() ||
          templateSourceParameters(Current)->size() != templateSourceParameters(Next)->size())
        return false;
      const auto *NextParent = dyn_cast<CXXRecordDecl>(Next->getDeclContext());
      const auto *Selected = classBodyRecord(Parent);
      if (!NextParent || (Parent->getCanonicalDecl() != NextParent->getCanonicalDecl() &&
          (!Selected || Selected->getCanonicalDecl() != NextParent->getCanonicalDecl())))
        return false;
      if (const auto *Partial = dyn_cast<ClassTemplatePartialSpecializationDecl>(Current)) {
        const auto *NextPartial = cast<ClassTemplatePartialSpecializationDecl>(Next);
        const auto *Primary = Partial->getSpecializedTemplate();
        const auto *Origin = Primary->getInstantiatedFromMemberTemplate();
        if (Primary->getCanonicalDecl() != NextPartial->getSpecializedTemplate()->getCanonicalDecl() &&
            (!Origin || Origin->getCanonicalDecl() !=
                            NextPartial->getSpecializedTemplate()->getCanonicalDecl()))
          return false;
      }
      Current = cast<NamedDecl>(Next->getCanonicalDecl());
    }
    return false;
  }
  bool classPatternBodyShape(const NamedDecl *D) {
    const auto *Pattern = classDefinitionRecord(D);
    if (!owned(Pattern))
      return false;
    const auto *Definition = Pattern->getDefinition();
    if (!Definition)
      return true; // A forward pattern is usable only after a real definition.
    if (ActiveClassShapes.size() >= 64 || !ActiveClassShapes.insert(D).second)
      return false;
    auto Restore = llvm::make_scope_exit([&] { ActiveClassShapes.erase(D); });
    return owned(Definition) && !Definition->isInvalidDecl() &&
           !hasNonFinalAttributes(Definition) && emptyBasesShape(Definition) &&
           classTemplateMembers(Definition);
  }
  bool classTemplateShape(const ClassTemplateDecl *D) {
    return classTemplateDeclarationShape(D) && classPatternOriginShape(D) &&
           friendClassDefinitionShape(D) && classPatternBodyShape(D);
  }
  bool classPartialShape(const ClassTemplatePartialSpecializationDecl *D) {
    // The primary is checked structurally here: enumerating its members would
    // recursively visit this same partial declaration again.
    return classPartialDeclarationShape(D) && classPatternOriginShape(D) &&
           classPatternOriginShape(D->getSpecializedTemplate()) && classPatternBodyShape(D);
  }
  bool classPatternShape(const NamedDecl *D) {
    return classTemplateShape(dyn_cast_or_null<ClassTemplateDecl>(D)) ||
           classPartialShape(dyn_cast_or_null<ClassTemplatePartialSpecializationDecl>(D));
  }
  bool aliasTemplateShape(const TypeAliasTemplateDecl *D) {
    if (D && isa<CXXRecordDecl>(D->getDeclContext()))
      return memberAliasShape(D);
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
    if (D && isa<CXXMethodDecl>(D->getTemplatedDecl()))
      return memberTemplateShape(D);
    if (D && isa<CXXRecordDecl>(D->getLexicalDeclContext())) {
      if (!friendFunctionTemplateDeclarationShape(D) ||
          !A.TemplateOrdinals.count(D->getCanonicalDecl()))
        return false;
      if (WrittenFriendTemplates.count(D))
        return true;
      auto Source = FriendTemplateDeclarations.find(D);
      return Source != FriendTemplateDeclarations.end() && friendTemplateSourceIdentity(Source->second);
    }
    if (!D || !owned(D) || D->isInvalidDecl() ||
        !isa<TranslationUnitDecl, NamespaceDecl>(D->getDeclContext()) ||
        !isa<TranslationUnitDecl, NamespaceDecl>(D->getLexicalDeclContext()))
      return false;
    const auto *Parameters = D->getTemplateParameters();
    const auto *Pattern = D->getTemplatedDecl();
    if (!templateParametersShape(Parameters) || D->isAbbreviated() ||
        !owned(Pattern) || Pattern->isInvalidDecl() ||
        !ordinaryFreeFunctionName(Pattern) ||
        Pattern->isVariadic() ||
        Pattern->isDefaulted() || Pattern->isConsteval() ||
        Pattern->getTrailingRequiresClause() || !supportedDeclarationAttributes(Pattern))
      return false;
    for (const auto *Parameter : Pattern->parameters()) {
      A.chargeExpansion(1, Parameter->getLocation());
      if (!owned(Parameter) || Parameter->isInvalidDecl() || Parameter->hasAttrs())
        return false;
    }
    return true;
  }
  bool traverseTemplateParameterSource(const TemplateParameterList *Parameters,
                                           bool DeferDependentType = false) {
    for (const auto *Parameter : *Parameters) {
      if (const auto *Value = dyn_cast<NonTypeTemplateParmDecl>(Parameter)) {
        if (const auto *Info = Value->getTypeSourceInfo();
            Info && (!DeferDependentType || !Info->getType()->isInstantiationDependentType())) {
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
                              SourceLocation L,
                              const std::vector<bool> *Pending = nullptr,
                              unsigned Prefix = ~0u) {
    if (Prefix == ~0u ? Arguments.size() != Parameters->size()
                     : Prefix > Arguments.size() || Prefix > Parameters->size()) {
      A.reject(L, "template specialization", "Concrete arguments must match the primary parameter list.");
      return;
    }
    for (unsigned Index = 0; Index < std::min(Arguments.size(), Prefix); ++Index) {
      const auto *Parameter = Parameters->getParam(Index);
      const auto &Argument = Arguments.get(Index);
      A.chargeExpansion(1, L);
      auto CheckElement = [&](const TemplateArgument &Element) {
        if (Pending && (*Pending)[Index]) {
          if (isa<TemplateTypeParmDecl>(Parameter) &&
              Element.getKind() == TemplateArgument::Type &&
              !Element.getAsType().isNull() && Element.isInstantiationDependent())
            return;
          if (isa<NonTypeTemplateParmDecl>(Parameter) &&
              Element.getKind() == TemplateArgument::Expression && Element.getAsExpr()) {
            if (!Element.getAsExpr()->isTypeDependent())
              A.type(Element.getAsExpr()->getType(), L);
            return; // Written expression and actual type source are checked below.
          }
        }
        if (isa<TemplateTypeParmDecl>(Parameter)) {
          if (Element.getKind() != TemplateArgument::Type ||
              Element.getAsType().isNull() || Element.getAsType()->isDependentType())
            A.reject(L, "template argument", "A resolved supported type argument is required.");
          else
            A.type(Element.getAsType(), L, true);
        } else if (scalarTemplateArgumentType(Element).isNull()) {
          A.reject(L, "template argument",
                   "A resolved integer, boolean, enum or nullptr_t value argument is required.");
        } else {
          A.type(scalarTemplateArgumentType(Element), L);
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
  template <class Declaration>
  void recordOuterPackLists(const Declaration *D, bool Full) {
    if (!owned(D) || !D->getNumTemplateParameterLists())
      return;
    std::vector<const NamedDecl *> Owners;
    if (!outerTemplateOwners(D, Full, Owners) ||
        Owners.size() != D->getNumTemplateParameterLists())
      return;
    for (unsigned I = 0; I < Owners.size(); ++I) {
      const auto *Parameters = D->getTemplateParameterList(I);
      if (!Owners[I] || !outerTemplateListShape(Parameters, Owners[I]))
        continue;
      recordTemplatePacks(Parameters, Owners[I]);
      for (const auto *Parameter : *Parameters)
        if (supportedPackDeclaration(Parameter))
          OuterPackDeclarations.emplace(Parameter,
                                        std::make_pair(static_cast<const Decl *>(D), I));
    }
  }
  void recordOuterPacks(const DeclaratorDecl *D, const NamedDecl *Owner) {
    if (!owned(Owner))
      return;
    const auto *Variable = dyn_cast_or_null<VarTemplateSpecializationDecl>(D);
    recordOuterPackLists(D, Variable && Variable->getKind() == Decl::VarTemplateSpecialization &&
                               Variable->isExplicitSpecialization());
  }
  void recordClassOuterPacks(const CXXRecordDecl *D) {
    // TagDecl retains its own outer headers; it is not a DeclaratorDecl.
    const auto *Spec = dyn_cast_or_null<ClassTemplateSpecializationDecl>(D);
    recordOuterPackLists(D, Spec && Spec->getKind() == Decl::ClassTemplateSpecialization &&
                               Spec->isExplicitSpecialization());
  }
  void recordFunctionPacks(const FunctionDecl *Function, const NamedDecl *Owner) {
    if (!owned(Function) || !owned(Owner))
      return;
    const NamedDecl *FunctionOwner = Owner;
    const NamedDecl *OuterOwner = Owner;
    if (const auto *Method = dyn_cast<CXXMethodDecl>(Function)) {
      const auto *Template = Method->getPrimaryTemplate();
      if (!Template)
        Template = Method->getDescribedFunctionTemplate();
      if (Template) {
        // Function parameter packs belong to the inner member template, while
        // written outer class parameter lists retain their separate owner.
        FunctionOwner = Template;
        OuterOwner = classBodyPackOwner(Method->getParent());
      }
    }
    A.chargeExpansion(1 + Function->getNumParams(), Function->getLocation());
    for (const auto *Parameter : Function->parameters())
      recordPack(Parameter, FunctionOwner);
    if (OuterOwner)
      recordOuterPacks(Function, OuterOwner);
  }
  void indexTemplatePackSources(const NamedDecl *Template) {
    if (!owned(Template))
      return;
    recordTemplatePacks(templateSourceParameters(Template), Template);
    if (const auto *Function = dyn_cast<FunctionTemplateDecl>(Template)) {
      recordFunctionPacks(Function->getTemplatedDecl(), Template);
    } else if (const auto *Variable = variablePatternDecl(Template)) {
      const auto *Parent = dyn_cast<CXXRecordDecl>(Variable->getDeclContext());
      const auto *Outer = Parent ? classBodyPackOwner(Parent) : Template;
      if (Outer)
        recordOuterPacks(Variable, Outer);
    } else if (const auto *Pattern = classPatternRecord(Template)) {
      recordClassOuterPacks(Pattern);
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
      if (concreteFunctionTemplate(Owner)) {
        Primary = Owner->getPrimaryTemplate();
        if (FriendTemplateCanonicals.count(Owner->getPrimaryTemplate()->getCanonicalDecl())) {
          auto Body = TemplateBodySources.find(Owner);
          if (Body == TemplateBodySources.end() || !friendTemplateBodyIdentity(*Body->second)) {
            Primary = nullptr;
            break;
          }
          Primary = Body->second->Pattern->getDescribedFunctionTemplate();
        }
        break;
      }
      if ((Primary = classFunctionPattern(Owner)))
        break;
      if (const auto *Full = copiedFullClassFunction(Owner)) {
        Primary = classBodyPackOwner(Full);
        break;
      }
      if (concreteFriendFunction(Owner)) {
        auto Source = FriendFunctionSources.find(Owner);
        if (Source == FriendFunctionSources.end())
          break;
        const auto *Selected = Source->second->Selected;
        const auto *Parent = dyn_cast<CXXRecordDecl>(Selected->getLexicalDeclContext());
        if (!Parent)
          return; // A selected namespace body has no class pack level.
        Primary = classBodyPackOwner(Parent);
        break;
      }
      if (const auto *Ordinary = copiedOrdinaryClassFunction(Owner)) {
        Primary = classBodyPackOwner(Ordinary);
        break;
      }
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
    const auto *Type = dyn_cast<TemplateTypeParmDecl>(Pack);
    const auto *Value = dyn_cast<NonTypeTemplateParmDecl>(Pack);
    if (Type || Value) {
      const unsigned Index = Type ? Type->getIndex() : Value->getIndex();
      bool Matches = parameterInTemplate(Found->second, Pack, Index);
      // Out-of-line member headers own fresh parameter declarations. They
      // are not redeclarations of the class template's parameter objects.
      auto Written = OuterPackDeclarations.find(Pack);
      if (!Matches && Written != OuterPackDeclarations.end()) {
        const auto *Declaration = Written->second.first;
        const unsigned ListIndex = Written->second.second;
        auto MatchesHeader = [&](const auto *D, bool Full) {
          if (!owned(D) || D->isInvalidDecl())
            return false;
          std::vector<const NamedDecl *> Owners;
          if (!outerTemplateOwners(D, Full, Owners) ||
              Owners.size() != D->getNumTemplateParameterLists() ||
              ListIndex >= Owners.size() || !Owners[ListIndex] ||
              Owners[ListIndex]->getCanonicalDecl() != Found->second->getCanonicalDecl())
            return false;
          const auto *Parameters = D->getTemplateParameterList(ListIndex);
          return Index < Parameters->size() && Parameters->getParam(Index) == Pack &&
                 outerTemplateListShape(Parameters, Owners[ListIndex]);
        };
        if (const auto *Declarator = dyn_cast<DeclaratorDecl>(Declaration)) {
          const auto *Variable = dyn_cast<VarTemplateSpecializationDecl>(Declarator);
          Matches = MatchesHeader(Declarator,
              Variable && Variable->getKind() == Decl::VarTemplateSpecialization &&
                  Variable->isExplicitSpecialization());
        } else if (const auto *Record = dyn_cast<CXXRecordDecl>(Declaration)) {
          const auto *Spec = dyn_cast<ClassTemplateSpecializationDecl>(Record);
          Matches = MatchesHeader(Record,
              Spec && Spec->getKind() == Decl::ClassTemplateSpecialization &&
                  Spec->isExplicitSpecialization());
        }
      }
      if (!Matches)
        return false;
    }
    return functionTemplateShape(dyn_cast<FunctionTemplateDecl>(Found->second)) ||
           classPatternShape(Found->second) || variablePatternShape(Found->second) ||
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
  bool parameterTypeQueryMetadata(const TypeTraitExpr *Query) {
    const unsigned Arity = metadataTypeClassificationArity(Query->getTrait());
    if (!TemplateParameterTypeSource || !Arity || Query->getNumArgs() != Arity ||
        Query->isTypeDependent() || !Query->isValueDependent() ||
        !Query->isInstantiationDependent() || !Query->isPRValue() ||
        !A.Context.hasSameType(Query->getType(), A.Context.BoolTy))
      return false;
    for (const auto *Info : Query->getArgs()) {
      A.chargeExpansion(1, Query->getExprLoc());
      if (!Info)
        return false;
      if (!Info->getType()->isInstantiationDependentType()) {
        A.checkQueryType(Info->getType(), Query->getExprLoc());
        continue;
      }
      auto Written = Info->getTypeLoc().getUnqualifiedLoc().getAs<TemplateTypeParmTypeLoc>();
      const auto *Parameter = Written ? Written.getDecl() : nullptr;
      if (!owned(Parameter) || Parameter->isInvalidDecl() || Parameter->hasAttrs() ||
          Parameter->isParameterPack())
        return false;
      bool SameList = false;
      for (const auto *Candidate : *TemplateParameterTypeSource) {
        A.chargeExpansion(1, Query->getExprLoc());
        SameList |= Candidate == Parameter;
      }
      if (!SameList)
        return false;
    }
    // This declaration-only proof never reads a dependent boolean. RAV still
    // visits written types; each selected substitution is checked concretely.
    return true;
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
                     aliasTemplateShape(dyn_cast<TypeAliasTemplateDecl>(Target)) ||
                     variableTemplateShape(dyn_cast<VarTemplateDecl>(Target))) &&
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
        if (const auto *Field = dyn_cast<FieldDecl>(M->getMemberDecl());
            Field && Field->getType()->isReferenceType())
          return false; // The binding does not inherit its container's lifetime.
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
  void checkSelectedTemplateCall(const Expr *Expression, const FunctionDecl *Function,
                                  SourceLocation L) {
    if (!(isa<CXXNewExpr>(Expression) ? concreteFunctionTemplate(Function)
                                     : concreteMemberFunctionTemplate(Function)))
      return;
    auto Found = SelectedCallSources.find(Expression);
    if (Found == SelectedCallSources.end() || Found->second.empty()) {
      A.reject(L, "selected member template source", "Construction, conversion or allocation needs its exact successful selection source.");
      return;
    }
    for (const auto *Source : Found->second) {
      A.chargeExpansion(1, Source->Location);
      if (Source->Expression != Expression || Source->Function != Function ||
          !A.S.owns(A.Sources, Source->Location)) {
        A.reject(L, "selected member template identity", "Retained source must identify this actual expression and selected function.");
        return;
      }
      checkFunctionTemplateUse(Function, Source->Location, {});
      if (!A.S.Diagnostics.empty())
        return;
    }
  }
  void checkConstruction(const CXXConstructExpr *C, SourceLocation L) {
    if (!CheckedConstructions.insert(C).second)
      return;
    const auto *Constructor = C->getConstructor();
    if (A.S.coreV2() && concreteMemberFunctionTemplate(Constructor))
      checkSelectedTemplateCall(C, Constructor, L);
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
  void retainInitializerLookup(const Expr *Written, const Expr *Selected) {
    if (!Written || !Selected || Written == Selected)
      return;
    // Sema replaces an overloaded name in the semantic clause while retaining
    // the unresolved node in the written list. Pair only the same explicit
    // clause and the source components copied by FixOverloadedFunctionReference.
    std::vector<const Expr *> Wrappers;
    if (const auto *Cast = dyn_cast<ImplicitCastExpr>(Selected);
        Cast && Cast->getCastKind() == CK_FunctionToPointerDecay)
      Selected = Cast->getSubExpr();
    while (true) {
      A.chargeExpansion(1, Written->getBeginLoc());
      if (const auto *Paren = dyn_cast<ParenExpr>(Written)) {
        const auto *Resolved = dyn_cast<ParenExpr>(Selected);
        if (!Resolved || Paren->getLParen() != Resolved->getLParen() ||
            Paren->getRParen() != Resolved->getRParen())
          return;
        Wrappers.push_back(Written);
        Written = Paren->getSubExpr();
        Selected = Resolved->getSubExpr();
      } else if (const auto *Address = dyn_cast<UnaryOperator>(Written);
                 Address && Address->getOpcode() == UO_AddrOf) {
        const auto *Resolved = dyn_cast<UnaryOperator>(Selected);
        if (!Resolved || Resolved->getOpcode() != UO_AddrOf ||
            Address->getOperatorLoc() != Resolved->getOperatorLoc())
          return;
        Wrappers.push_back(Written);
        Written = Address->getSubExpr();
        Selected = Resolved->getSubExpr();
      } else {
        break;
      }
    }
    const auto *Lookup = dyn_cast<UnresolvedLookupExpr>(Written);
    const auto *Reference = dyn_cast<DeclRefExpr>(Selected);
    const auto *Function = Reference ? dyn_cast<FunctionDecl>(Reference->getDecl()) : nullptr;
    if (!Lookup || !Function || !owned(Function) ||
        !A.S.owns(A.Sources, Lookup->getNameLoc()) ||
        Lookup->getName() != Reference->getDecl()->getDeclName() ||
        Lookup->getNameLoc() != Reference->getLocation() ||
        Lookup->getSourceRange() != Reference->getSourceRange() ||
        !(Lookup->getQualifierLoc() == Reference->getQualifierLoc()) ||
        Lookup->getTemplateKeywordLoc() != Reference->getTemplateKeywordLoc() ||
        Lookup->getLAngleLoc() != Reference->getLAngleLoc() ||
        Lookup->getRAngleLoc() != Reference->getRAngleLoc() ||
        Lookup->getNumTemplateArgs() != Reference->getNumTemplateArgs())
      return;
    bool Found = false;
    for (const auto *Candidate : Lookup->decls()) {
      A.chargeExpansion(1, Lookup->getNameLoc());
      Found |= Candidate == Reference->getFoundDecl();
    }
    if (!Found)
      return;
    for (unsigned I = 0; I < Lookup->getNumTemplateArgs(); ++I)
      if (!sameArgumentSource(Lookup->template_arguments()[I],
                              Reference->template_arguments()[I]))
        return;
    auto Inserted = InitializerLookups.emplace(Lookup, Reference);
    if (!Inserted.second && Inserted.first->second != Reference)
      A.reject(Lookup->getNameLoc(), "initializer lookup source",
               "One written callback clause must have one selected reference.");
    InitializerLookupWrappers.insert(Wrappers.begin(), Wrappers.end());
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
        if (const auto *Written = I->getSyntacticForm();
            Written && Written->getNumInits() == I->getNumInits())
          for (unsigned Index = 0; Index < I->getNumInits(); ++Index)
            retainInitializerLookup(Written->getInit(Index), I->getInit(Index));
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
  void indexFriendSources(llvm::ArrayRef<FriendFunctionSource> Functions,
                          llvm::ArrayRef<FriendDeclarationSource> Declarations,
                          llvm::ArrayRef<FriendFunctionTemplateSource> Templates,
                          llvm::ArrayRef<FunctionTemplateBodySource> Bodies,
                          llvm::ArrayRef<FriendClassTemplateSource> Classes) {
    for (const auto &Source : Functions) {
      A.chargeExpansion(1, Source.Function->getLocation());
      auto Inserted = FriendFunctionSources.emplace(Source.Function, &Source);
      if (!Inserted.second) {
        const auto &Previous = *Inserted.first->second;
        if (Previous.Incoming != Source.Incoming || Previous.Selected != Source.Selected ||
            Previous.GrantingClass != Source.GrantingClass)
          A.reject(Source.Function->getLocation(), "friend source conflict",
                   "A friend function needs one exact incoming, selected and granting identity.");
      }
      FriendCanonicalFunctions.insert(Source.Function->getCanonicalDecl());
    }
    for (const auto &Source : Templates) {
      A.chargeExpansion(1, Source.Template->getLocation());
      FriendTemplateCanonicals.insert(Source.Template->getCanonicalDecl());
      auto Inserted = FriendTemplateSources.emplace(Source.Template, &Source);
      if (!Inserted.second) {
        const auto &Previous = *Inserted.first->second;
        if (Previous.Incoming != Source.Incoming || Previous.Selected != Source.Selected ||
            Previous.GrantingClass != Source.GrantingClass)
          A.reject(Source.Template->getLocation(), "friend template source conflict",
                   "Each actual primary requires one exact substitution identity.");
      } else {
        FriendTemplateFamilies[Source.Template->getCanonicalDecl()].push_back(&Source);
      }
    }
    for (const auto &Source : Bodies) {
      A.chargeExpansion(1, Source.Function->getLocation());
      auto Inserted = TemplateBodySources.emplace(Source.Function, &Source);
      if (!Inserted.second) {
        const auto &Previous = *Inserted.first->second;
        if (Previous.Compatible != Source.Compatible || Previous.Pattern != Source.Pattern ||
            Previous.LexicalContext != Source.LexicalContext)
          A.reject(Source.Function->getLocation(), "function template body source conflict",
                   "Each actual body requires one compatible declaration and lexical context.");
      }
    }
    for (const auto &Source : Classes) {
      A.chargeExpansion(1, Source.Template->getLocation());
      FriendClassCanonicals.insert(Source.Template->getCanonicalDecl());
      auto Inserted = FriendClassSources.emplace(Source.Template, &Source);
      if (!Inserted.second) {
        const auto &Previous = *Inserted.first->second;
        if (Previous.Written != Source.Written || Previous.GrantingClass != Source.GrantingClass ||
            Previous.Context != Source.Context || Previous.Previous != Source.Previous)
          A.reject(Source.Template->getLocation(), "friend class source conflict",
                   "Each copied target requires one exact granting source and lookup result.");
      }
    }
    for (const auto &Source : Declarations) {
      A.chargeExpansion(1, Source.Declaration->getFriendLoc());
      auto Inserted = FriendDeclarationSources.emplace(Source.Declaration, &Source);
      if (!Inserted.second && Inserted.first->second->Written != Source.Written)
        A.reject(Source.Declaration->getFriendLoc(), "friend declaration conflict",
                 "Each copied friend needs its exact written declaration.");
      if (const auto *Template = dyn_cast_or_null<FunctionTemplateDecl>(Source.Declaration->getFriendDecl())) {
        auto Target = FriendTemplateDeclarations.emplace(Template, Source.Declaration);
        if (!Target.second && Target.first->second != Source.Declaration)
          A.reject(Source.Declaration->getFriendLoc(), "friend template target conflict",
                   "The copied primary must pair with its exact friend declaration.");
      }
      if (const auto *Template = dyn_cast_or_null<ClassTemplateDecl>(Source.Declaration->getFriendDecl())) {
        auto Target = FriendClassDeclarations.emplace(Template, Source.Declaration);
        if (!Target.second && Target.first->second != Source.Declaration)
          A.reject(Source.Declaration->getFriendLoc(), "friend class target conflict",
                   "The copied class template must pair with its exact friend declaration.");
      }
      const auto *Function = dyn_cast_or_null<FunctionDecl>(Source.Declaration->getFriendDecl());
      if (Function) {
        auto Target = FriendFunctionDeclarations.emplace(Function, Source.Declaration);
        if (!Target.second && Target.first->second != Source.Declaration)
          A.reject(Source.Declaration->getFriendLoc(), "friend target conflict",
                   "The function event must pair with the exact copied friend target.");
      }
    }
  }
  bool normalizedFriendSourceReady(const FunctionDecl *Incoming) {
    const auto *Info = Incoming->getTypeSourceInfo();
    const auto Prototype = Info ? Info->getTypeLoc().IgnoreParens().getAs<FunctionProtoTypeLoc>()
                                : FunctionProtoTypeLoc();
    if (!Prototype || Incoming->getType()->isInstantiationDependentType() ||
        Info->getType()->isInstantiationDependentType() ||
        !standardExceptionSpecification(Prototype.getTypePtr()) ||
        !standardExceptionSpecification(Incoming->getType()->getAs<FunctionProtoType>()))
      return false;
    auto ResolvedExpression = [&](const Expr *Expression) {
      return Expression && A.S.owns(A.Sources, Expression->getBeginLoc()) &&
             !Expression->isTypeDependent() && !Expression->isValueDependent() &&
             !Expression->isInstantiationDependent();
    };
    if (const auto *Exception = Prototype.getTypePtr()->getNoexceptExpr())
      if (!ResolvedExpression(Exception))
        return false;
    for (const auto *Parameter : Incoming->parameters()) {
      const auto *Type = Parameter->getTypeSourceInfo();
      if (!Type || Type->getType()->isInstantiationDependentType())
        return false; // Array parameter adjustment must not hide its written bound.
      if (Parameter->hasDefaultArg() &&
          (Parameter->hasUnparsedDefaultArg() || Parameter->hasUninstantiatedDefaultArg() ||
           Parameter->hasInheritedDefaultArg() || !ResolvedExpression(Parameter->getDefaultArg())))
        return false;
    }
    if (Incoming->getQualifier() && Incoming->getQualifier()->isDependent())
      return false;
    return true;
  }
  bool friendSourceIdentity(const FriendDecl *D) {
    if (D && D->getFriendType())
      return friendTypeSourceIdentity(D);
    if (D && isa_and_nonnull<FunctionTemplateDecl>(D->getFriendDecl()))
      return friendTemplateSourceIdentity(D);
    if (D && isa_and_nonnull<ClassTemplateDecl>(D->getFriendDecl()))
      return friendClassSourceIdentity(D);
    auto Found = FriendDeclarationSources.find(D);
    const auto *Function = D ? dyn_cast_or_null<FunctionDecl>(D->getFriendDecl()) : nullptr;
    auto Event = FriendFunctionSources.find(Function);
    if (Found == FriendDeclarationSources.end() || Event == FriendFunctionSources.end() ||
        !genericFriendFunctionShape(D))
      return false;
    const auto &Source = *Found->second;
    const auto &Substitution = *Event->second;
    const auto *Written = Source.Written;
    const auto *Parent = dyn_cast<CXXRecordDecl>(D->getDeclContext());
    const auto *Body = classBodyRecord(Parent);
    if (!genericFriendFunctionShape(Written) ||
        !WrittenFriendDeclarations.count(Written) || !owned(Body) ||
        !friendFunctionSourceShape(Substitution.Incoming) ||
        !friendFunctionSourceShape(Substitution.Selected) ||
        Source.Declaration != D || Substitution.Function != Function ||
        Substitution.Incoming != Written->getFriendDecl() ||
        Substitution.GrantingClass != Parent ||
        Function->getLexicalDeclContext() != Parent ||
        Body->getCanonicalDecl() !=
            cast<CXXRecordDecl>(Written->getDeclContext())->getCanonicalDecl() ||
        D->getFriendLoc() != Written->getFriendLoc() ||
        Function->getDeclName() != Substitution.Incoming->getDeclName())
      return false;
    // Canonical namespace merging is allowed only after retaining this exact
    // actual declaration; the canonical first declaration can have no MSI.
    bool Selected = false;
    unsigned Count = 0;
    for (const auto *Declaration : Substitution.Incoming->redecls()) {
      A.chargeExpansion(1, Declaration->getLocation());
      if (++Count > 64)
        return false;
      Selected |= Declaration == Substitution.Selected;
    }
    if (!Selected)
      return false;
    // The selected signature was substituted by Sema. A different dependent
    // incoming spelling has no corresponding substituted type event here;
    // retain the evidence and reject instead of treating it as checked.
    if (Substitution.Selected != Substitution.Incoming &&
        !normalizedFriendSourceReady(Substitution.Incoming))
      return false;
    if (Substitution.Selected->isThisDeclarationADefinition()) {
      if (Function->getTemplatedKind() != FunctionDecl::TK_MemberSpecialization ||
          !Function->getMemberSpecializationInfo() ||
          Function->getInstantiatedFromMemberFunction() != Substitution.Selected)
        return false;
    } else if (Function->getTemplatedKind() != FunctionDecl::TK_NonTemplate ||
               Function->getMemberSpecializationInfo()) {
      return false;
    }
    return true;
  }
  void checkFriendSourceIdentities() {
    for (const auto &[Declaration, Source] : FriendDeclarationSources)
      if (!friendSourceIdentity(Declaration))
        A.reject(Declaration->getFriendLoc(), "copied friend source",
                 "The actual friend requires its exact written source and granting-class identity.");
    if (!A.S.Diagnostics.empty())
      return;
    for (const auto &[Function, Source] : FriendFunctionSources)
      for (const auto *Pattern : {Source->Incoming, Source->Selected})
        if (const auto *Parent = dyn_cast<CXXRecordDecl>(Pattern->getLexicalDeclContext()))
          if (const auto *Owner = classBodyPackOwner(Parent))
            recordFunctionPacks(Pattern, Owner);
    for (const auto &[Function, Source] : FriendFunctionSources)
      if (!FriendFunctionDeclarations.count(Function))
        A.reject(Function->getLocation(), "friend function source",
                 "A substituted friend function requires its exact copied friend declaration event.");
  }

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
    if (concreteFunctionTemplate(Source.Function))
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
    if (const auto *Variable = dyn_cast_or_null<VarTemplateSpecializationDecl>(Source.Variable)) {
      if (!Source.Type || Source.HasAttributes || !variablePatternType(Variable) ||
          !A.Context.hasSameType(Source.Type->getType(), Variable->getType())) {
        A.reject(Source.Location, "explicit variable instantiation", "An attribute-free scalar variable directive with its exact written type is required.");
        return;
      }
      if (!checkVariableTemplateUse(Variable, Source.Location, {}, false) ||
          !TraverseNestedNameSpecifierLoc(Source.Qualifier) ||
          !TraverseTypeLoc(Source.Type->getTypeLoc()))
        return;
      TraverseVarTemplateSpecializationDecl(const_cast<VarTemplateSpecializationDecl *>(Variable));
      return;
    }
    const auto *FullStatic = copiedFullClassStatic(Source.Variable);
    const auto *OrdinaryStatic = copiedOrdinaryClassStatic(Source.Variable);
    const auto *Body = OrdinaryStatic ? OrdinaryStatic : FullStatic;
    if (!owned(Source.Variable) || !Source.Type || Source.HasAttributes ||
        !(Body ? zeroParameterClassBodyShape(Body)
               : classPatternShape(classStaticDataPattern(Source.Variable))) ||
        !classTemplateStaticDataShape(Source.Variable) ||
        !A.Context.hasSameType(Source.Type->getType(), Source.Variable->getType())) {
      A.reject(Source.Location, "explicit static instantiation",
               "An owned scalar member and matching attribute-free source type are required.");
      return;
    }
    if (Body && (!checkClassFullOwnerSources(Body) || !A.S.Diagnostics.empty()))
      return;
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
  void checkExplicitMemberClassInstantiation(
      const ExplicitMemberClassInstantiationSource &Source) {
    if (!A.S.coreV2() || !A.S.owns(A.Sources, Source.Location))
      return;
    A.chargeExpansion(1, Source.Location);
    const auto *Qualifier = Source.Qualifier.getNestedNameSpecifier();
    const auto *Type = Qualifier ? Qualifier->getAsType() : nullptr;
    const auto *Parent = Type ? Type->getAsCXXRecordDecl() : nullptr;
    if (!ordinaryMemberClassScope(Source.Record) || Source.Record->isDependentContext() ||
        !ordinaryClassDeclarationShape(Source.Record) || !owned(Source.Origin) ||
        Source.Record->getInstantiatedFromMemberClass() != Source.Origin ||
        Source.HasAttributes || !Qualifier || Qualifier->isDependent() || !Parent ||
        Parent->getCanonicalDecl() !=
            cast<CXXRecordDecl>(Source.Record->getDeclContext())->getCanonicalDecl() ||
        !A.S.owns(A.Sources, Source.Qualifier.getBeginLoc()) ||
        !A.S.owns(A.Sources, Source.Qualifier.getEndLoc()) ||
        !A.S.owns(A.Sources, Source.TemplateLocation) ||
        (Source.ExternLocation.isValid() && !A.S.owns(A.Sources, Source.ExternLocation))) {
      A.reject(Source.Location, "explicit ordinary member class instantiation",
               "Each directive needs its exact member/origin and an attribute-free owned qualifier.");
      return;
    }
    const auto Kind = Source.Record->getTemplateSpecializationKind();
    // If the class definition already exists, pinned Sema only instantiates
    // its members and can leave the record's earlier implicit kind unchanged.
    if (Kind != TSK_ExplicitInstantiationDeclaration &&
        Kind != TSK_ExplicitInstantiationDefinition && Kind != TSK_ExplicitSpecialization &&
        !(Kind == TSK_ImplicitInstantiation && Source.Record->getDefinition())) {
      A.reject(Source.Location, "explicit ordinary member class state",
               "The actual member needs a valid state for its successful explicit directive.");
      return;
    }
    if (!checkClassFullOwnerSources(Source.Record->getDeclContext()) ||
        !checkOrdinaryClassSource(Source.Record) || !A.S.Diagnostics.empty())
      return;
    // Later directives may share and mutate Record. Every written occurrence,
    // including extern/no-effect directives, still checks its own source.
    TraverseNestedNameSpecifierLoc(Source.Qualifier);
  }
  bool templateSourceShape(const NamedDecl *D) {
    return functionTemplateShape(dyn_cast_or_null<FunctionTemplateDecl>(D)) ||
           classPatternShape(D) || variablePatternShape(D) ||
           aliasTemplateShape(dyn_cast_or_null<TypeAliasTemplateDecl>(D));
  }
  static const Expr *argumentExpression(const TemplateArgumentLoc &Argument) {
    switch (Argument.getArgument().getKind()) {
    case TemplateArgument::Expression: return Argument.getSourceExpression();
    case TemplateArgument::Integral: return Argument.getSourceIntegralExpression();
    case TemplateArgument::NullPtr: return Argument.getSourceNullPtrExpression();
    default: return nullptr;
    }
  }
  static bool hasArgumentSource(const TemplateArgumentLoc &Argument) {
    // Successful C++17 Sema events retain a typed Expr payload for NullPtr.
    // Do not dereference a missing expression through getLocation/getSourceRange.
    return Argument.getArgument().getKind() != TemplateArgument::NullPtr ||
           Argument.getSourceNullPtrExpression();
  }
  static SourceLocation argumentLocation(const TemplateArgumentLoc &Argument) {
    return hasArgumentSource(Argument) ? Argument.getLocation() : SourceLocation();
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
    case TemplateArgument::NullPtr:
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
  bool sameCanonicalArguments(const TemplateArgumentList *Canonical,
                                const TemplateArgumentList *Sugared) {
    if (!Canonical || !Sugared || Canonical->size() != Sugared->size())
      return false;
    for (unsigned I = 0; I < Canonical->size(); ++I)
      if (!Canonical->get(I).structurallyEquals(
              A.Context.getCanonicalTemplateArgument(Sugared->get(I))))
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
    return scalarTemplateValueMatches(argumentExpression(Source), Canonical, A.Context);
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
          return owned(Declaration) && templateParametersShape(
              Parameters, templateSourceParameterDepth(Declaration));
      }
      return false;
    };
    return Find(dyn_cast<FunctionTemplateDecl>(Template)) ||
           Find(dyn_cast<ClassTemplateDecl>(Template)) ||
           Find(dyn_cast<TypeAliasTemplateDecl>(Template)) ||
           Find(dyn_cast<ClassTemplatePartialSpecializationDecl>(Template)) ||
           Find(dyn_cast<VarTemplateDecl>(Template)) ||
           Find(dyn_cast<VarTemplatePartialSpecializationDecl>(Template));
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
  const TemplateArgument *sourceSlot(const NamedDecl *Primary, unsigned Index,
                                      SourceLocation Location) {
    // A definition has its own parameter context. Only source frames
    // entered after it may shadow that context; caller frames remain fenced.
    const auto Floor = DefinitionFrames.empty() ? 0 : DefinitionFrames.back().TemplateDepth;
    for (std::size_t I = TemplateFrames.size(); I > Floor; --I) {
      const auto &Frame = *TemplateFrames[I - 1];
      if (Frame.Source.Template->getCanonicalDecl() != Primary->getCanonicalDecl())
        continue;
      if (Index < Frame.Ready.size() && Frame.Ready[Index])
        return &Frame.Source.Canonical->get(Index);
      A.reject(Location, "template source edge", "The matching template argument is not checked yet.");
      return nullptr;
    }
    if (!DefinitionFrames.empty()) {
      const auto &Frame = DefinitionFrames.back();
      if (Frame.Owner && Frame.Arguments &&
          Frame.Owner->getCanonicalDecl() == Primary->getCanonicalDecl() &&
          Index < Frame.Arguments->size())
        return &Frame.Arguments->get(Index);
    }
    A.reject(Location, "template source edge",
             "A substitution requires the matching previously checked argument slot.");
    return nullptr;
  }
  const TemplateArgument *sourceEdge(const NamedDecl *Primary, unsigned Index,
                                      std::optional<unsigned> PackIndex,
                                      SourceLocation Location) {
    const auto *Argument = sourceSlot(Primary, Index, Location);
    if (Argument) {
      if (!PackIndex && Argument->getKind() != TemplateArgument::Pack)
        return Argument;
      if (PackIndex && Argument->getKind() == TemplateArgument::Pack &&
          Argument->pack_size() <= 64 && *PackIndex < Argument->pack_size())
        return &Argument->pack_elements()[Argument->pack_size() - 1 - *PackIndex];
      A.reject(Location, "template source pack edge", "The checked argument must match the bounded pack position.");
    }
    return nullptr;
  }
  const TemplateArgument *concreteSourceEdge(const Decl *Associated,
                                               const NamedDecl *Primary,
                                               unsigned Index,
                                               std::optional<unsigned> PackIndex,
                                               SourceLocation L, bool WholePack = false) {
    const TemplateArgument *Argument = nullptr;
    if (const auto *Record = dyn_cast_or_null<ClassTemplateSpecializationDecl>(Associated);
        Record && Record->getKind() == Decl::ClassTemplateSpecialization) {
      const auto *Pattern = classTemplatePattern(Record);
      if (!owned(Record) || Record->isDependentContext() || !Pattern ||
          !classPatternShape(Primary) ||
          Pattern->getCanonicalDecl() != Primary->getCanonicalDecl() ||
          !checkClassPartialSource(Record, L) || !A.S.Diagnostics.empty())
        return nullptr;
      const auto &Arguments = Record->getTemplateInstantiationArgs();
      if (Index < Arguments.size())
        Argument = &Arguments.get(Index);
    } else if (const auto *Function = dyn_cast_or_null<FunctionDecl>(Associated);
               concreteFunctionTemplate(Function)) {
      const auto Floor = DefinitionFrames.empty() ? 0 : DefinitionFrames.back().TemplateDepth;
      for (std::size_t I = TemplateFrames.size(); I > Floor; --I) {
        const auto &Frame = *TemplateFrames[I - 1];
        const auto *Selected = dyn_cast_or_null<FunctionDecl>(Frame.Source.Declaration);
        if (Frame.Source.Kind != TemplateSourceKind::Function || !Selected ||
            Selected->getCanonicalDecl() != Function->getCanonicalDecl())
          continue;
        if (Frame.Source.Template->getCanonicalDecl() != Primary->getCanonicalDecl() ||
            !sameArguments(Frame.Source.Canonical, Function->getTemplateSpecializationArgs()) ||
            Index >= Frame.Ready.size() || !Frame.Ready[Index]) {
          A.reject(L, "function source slot", "The actual selected function requires its checked argument slot.");
          return nullptr;
        }
        Argument = &Frame.Source.Canonical->get(Index);
        break;
      }
      if (!Argument && !DefinitionFrames.empty()) {
        const auto &Frame = DefinitionFrames.back();
        const auto *Definition = dyn_cast_or_null<FunctionDecl>(Frame.Declaration);
        if (Definition && Definition->getCanonicalDecl() == Function->getCanonicalDecl() &&
            Frame.Owner && Frame.Owner->getCanonicalDecl() == Primary->getCanonicalDecl() &&
            sameArguments(Frame.Arguments, Function->getTemplateSpecializationArgs()) &&
            Index < Frame.Arguments->size())
          Argument = &Frame.Arguments->get(Index);
      }
      if (!Argument) {
        A.reject(L, "function definition source", "A substitution cannot borrow another instance of the same function template.");
        return nullptr;
      }
    } else {
      return WholePack ? sourceSlot(Primary, Index, L)
                       : sourceEdge(Primary, Index, PackIndex, L);
    }
    if (Argument && !PackIndex &&
        (WholePack || Argument->getKind() != TemplateArgument::Pack))
      return Argument;
    if (Argument && PackIndex && Argument->getKind() == TemplateArgument::Pack &&
        Argument->pack_size() <= 64 && *PackIndex < Argument->pack_size())
      return &Argument->pack_elements()[Argument->pack_size() - 1 - *PackIndex];
    A.reject(L, "concrete source slot", "The concrete declaration requires its matching scalar or bounded pack slot.");
    return nullptr;
  }
  bool hasSourceFrame(const NamedDecl *Primary) const {
    const auto Floor = DefinitionFrames.empty() ? 0 : DefinitionFrames.back().TemplateDepth;
    for (std::size_t I = TemplateFrames.size(); I > Floor; --I)
      if (TemplateFrames[I - 1]->Source.Template->getCanonicalDecl() == Primary->getCanonicalDecl())
        return true;
    return !DefinitionFrames.empty() && DefinitionFrames.back().Owner &&
           DefinitionFrames.back().Owner->getCanonicalDecl() == Primary->getCanonicalDecl();
  }
  void checkScalarSourceEdge(const SubstNonTypeTemplateParmExpr *E, SourceLocation L) {
    const auto *Primary = scalarTemplateOwner(E);
    if (!Primary || (!isa<TypeAliasTemplateDecl, ClassTemplatePartialSpecializationDecl,
                         VarTemplateDecl, VarTemplatePartialSpecializationDecl>(Primary) &&
                     !isa<ClassTemplateSpecializationDecl>(E->getAssociatedDecl()) &&
                     !concreteFunctionTemplate(dyn_cast<FunctionDecl>(E->getAssociatedDecl())) &&
                     !hasSourceFrame(Primary)))
      return; // Existing concrete function/class bodies retain their source checks.
    const auto *Argument = concreteSourceEdge(
        E->getAssociatedDecl(), Primary, E->getIndex(), E->getPackIndex(), L);
    if (!Argument || scalarTemplateArgumentType(*Argument).isNull() ||
        !A.Context.hasSameType(E->getType(), scalarTemplateArgumentType(*Argument)) ||
        !scalarTemplateValueMatches(E->getReplacement(), *Argument, A.Context))
      A.reject(L, "template scalar source", "The replacement must equal its checked scalar argument.");
  }
  bool emptyTypePackSource(const SubstTemplateTypeParmPackType *Type,
                           SourceLocation L) {
    const auto *Primary = templateSourceOwner(Type->getAssociatedDecl());
    const auto Depth = templateSourceParameterDepth(Primary);
    if (!Depth || !owned(Type->getAssociatedDecl()) || !templateSourceShape(Primary) ||
        Type->getNumArgs() || Type->getIndex() >= templateSourceParameters(Primary)->size())
      return false;
    const auto *Parameter = dyn_cast<TemplateTypeParmDecl>(
        templateSourceParameters(Primary)->getParam(Type->getIndex()));
    if (!Parameter || !Parameter->isParameterPack() ||
        Parameter->getDepth() != *Depth || !owned(Parameter))
      return false;
    const auto *Argument = concreteSourceEdge(
        Type->getAssociatedDecl(), Primary, Type->getIndex(), {}, L, true);
    A.chargeExpansion(1, L);
    return Argument && Argument->getKind() == TemplateArgument::Pack && !Argument->pack_size();
  }
  bool pendingGenericParameterType(const TypeSourceInfo *Info) const {
    if (!Info || Info->getType().isNull() ||
        !Info->getType()->isInstantiationDependentType())
      return false;
    const auto *Auto = dyn_cast<AutoType>(Info->getType().getTypePtr());
    // Plain auto is checked metadata: the converted scalar supplies its type.
    return !Auto || !Auto->getDeducedType().isNull() || Auto->isConstrained();
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
                          !scalarTemplateType(Type))) ||
        (Argument && (scalarTemplateArgumentType(*Argument).isNull() ||
                      (!Placeholder && !A.Context.hasSameType(Type.getUnqualifiedType(),
                                      scalarTemplateArgumentType(*Argument).getUnqualifiedType()))))) {
      A.reject(L, "template parameter type", "A concrete scalar parameter type must match its converted argument; plain auto remains source metadata.");
      return true;
    }
    if (!Placeholder)
      A.type(Type, L);
    return TraverseTypeLoc(Info->getTypeLoc());
  }
  bool partialDeclarationIdentity(const TemplateUseSource &Source) {
    const auto *D = dyn_cast_or_null<NamedDecl>(Source.Declaration);
    const NamedDecl *Primary = nullptr, *Origin = nullptr;
    const TemplateArgumentList *Arguments = nullptr;
    const ASTTemplateArgumentListInfo *Written = nullptr;
    if (const auto *Class = dyn_cast_or_null<ClassTemplatePartialSpecializationDecl>(D)) {
      if (!classPartialShape(Class))
        return false;
      Primary = Class->getSpecializedTemplate();
      Origin = Class->getInstantiatedFromMember();
      Arguments = &Class->getTemplateArgs();
      Written = Class->getTemplateArgsAsWritten();
    } else if (const auto *Variable = dyn_cast_or_null<VarTemplatePartialSpecializationDecl>(D)) {
      if (!variablePartialShape(Variable))
        return false;
      Primary = Variable->getSpecializedTemplate();
      Origin = Variable->getInstantiatedFromMember();
      Arguments = &Variable->getTemplateArgs();
      Written = Variable->getTemplateArgsAsWritten();
    } else {
      return false;
    }
    if (!owned(D) || D->isInvalidDecl() || Source.Kind != TemplateSourceKind::PartialDeclaration ||
        !Source.Template || !Primary || Source.Template != Primary ||
        Source.Location != D->getLocation() || Source.Type || Source.Underlying ||
        Source.Selection || Source.Instantiation || Source.WrittenStorageClass ||
        !sameArguments(Source.Canonical, Arguments) || !Written || !Source.Written ||
        Source.Written->getLAngleLoc() != Written->getLAngleLoc() ||
        Source.Written->getRAngleLoc() != Written->getRAngleLoc() ||
        Source.Written->getNumTemplateArgs() != Written->getNumTemplateArgs())
      return false;
    // A direct own specialization can inherit the first declaration's origin.
    // Only a copied event claims to have come from that exact original object.
    if (Source.Origin ? Source.Origin != Origin || !owned(Origin) || Origin->isInvalidDecl()
                      : !WrittenPartialDeclarations.count(D))
      return false;
    for (unsigned I = 0; I < Written->getNumTemplateArgs(); ++I)
      if (!sameArgumentSource(Source.Written->arguments()[I], Written->arguments()[I]))
        return false;
    return true;
  }
  struct PartialArgumentFrontier {
    unsigned Index = ~0u, WrittenIndex = 0, PackIndex = 0;
    bool ExpandedPack = false;
    explicit operator bool() const { return Index != ~0u; }
  };
  PartialArgumentFrontier partialArgumentFrontier(
      const TemplateParameterList *Parameters,
      llvm::ArrayRef<TemplateArgumentLoc> Written) {
    unsigned Position = 0;
    for (unsigned I = 0; I < Parameters->size() && Position < Written.size(); ++I) {
      const auto *Parameter = Parameters->getParam(I);
      const auto Expanded = getExpandedPackSize(Parameter);
      if (Parameter->isTemplateParameterPack() && !Expanded)
        break; // The trailing unexpanded pack keeps its usual packed shape.
      const unsigned Size = Expanded ? *Expanded : 1;
      if (Size > 64)
        break; // Ordinary bounded source validation rejects this size.
      for (unsigned K = 0; K < Size && Position < Written.size(); ++K, ++Position)
        if (Written[Position].getArgument().isPackExpansion())
          return {I, Position, K, Expanded.has_value()};
    }
    return {};
  }
  bool checkPartialDeclarationSource(NamedDecl *D) {
    if (CheckedPartialDeclarations.count(D))
      return true;
    auto Found = PartialDeclarations.find(D);
    if (Found == PartialDeclarations.end() || Found->second.empty()) {
      A.reject(D->getLocation(), "partial declaration source",
               "Each written or copied partial requires its successful primary argument source.");
      return true;
    }
    if (DefinitionFrames.size() >= 64 || !ActivePartialDeclarations.insert(D).second) {
      A.reject(D->getLocation(), "partial declaration source depth",
               "Partial declaration source checking must be bounded and acyclic.");
      return true;
    }
    auto *SavedFunction = CurrentFunction;
    auto *SavedMethod = CurrentMethod;
    auto *SavedField = CurrentDefaultField;
    auto SavedInitializer = ImplicitInitializerOwner;
    CurrentFunction = nullptr;
    CurrentMethod = nullptr;
    CurrentDefaultField = nullptr;
    ImplicitInitializerOwner = D->getLocation();
    DefinitionFrames.push_back({D, nullptr, nullptr, TemplateFrames.size()});
    auto Restore = llvm::make_scope_exit([&] {
      DefinitionFrames.pop_back();
      ActivePartialDeclarations.erase(D);
      CurrentFunction = SavedFunction;
      CurrentMethod = SavedMethod;
      CurrentDefaultField = SavedField;
      ImplicitInitializerOwner = SavedInitializer;
    });
    const TemplateUseSource *First = nullptr;
    for (const auto *Source : Found->second) {
      if (!partialDeclarationIdentity(*Source) ||
          (First && (First->Origin != Source->Origin || !equivalentUse(*First, *Source)))) {
        A.reject(D->getLocation(), "partial declaration source identity",
                 "The actual declaration, primary, written arguments and copy origin must agree.");
        return true;
      }
      First = Source;
      if (!checkTemplateUse(*Source, Source->Written->arguments(), true))
        return false;
      if (!A.S.Diagnostics.empty())
        return true;
    }
    CheckedPartialDeclarations.insert(D);
    return true;
  }
  bool classFullDeclarationIdentity(const TemplateUseSource &Source) {
    const auto *D = dyn_cast_or_null<ClassTemplateSpecializationDecl>(Source.Declaration);
    if (!classFullDeclarationShape(D) || Source.Kind != TemplateSourceKind::ClassFullDeclaration ||
        Source.Template != D->getSpecializedTemplate() || Source.Location != D->getLocation() ||
        Source.Type || Source.Underlying || Source.Selection || Source.Instantiation ||
        Source.WrittenStorageClass || !sameArguments(Source.Canonical, &D->getTemplateArgs()))
      return false;
    const auto *Origin = D->getInstantiatedFromMemberClass();
    if (Source.Origin != Origin || (!Origin && !genericClassFullIdentity(D)))
      return false;
    const auto *Written = D->getTemplateArgsAsWritten();
    if (!Written || !Source.Written ||
        Source.Written->getLAngleLoc() != Written->getLAngleLoc() ||
        Source.Written->getRAngleLoc() != Written->getRAngleLoc() ||
        Source.Written->getNumTemplateArgs() != Written->getNumTemplateArgs())
      return false;
    for (unsigned I = 0; I < Written->getNumTemplateArgs(); ++I)
      if (!sameArgumentSource(Source.Written->arguments()[I], Written->arguments()[I]))
        return false;
    return true;
  }
  bool checkClassFullDeclarationSource(const ClassTemplateSpecializationDecl *D) {
    if (CheckedClassFullDeclarations.count(D))
      return true;
    if (!classFullDeclarationShape(D)) {
      A.reject(D->getLocation(), "class full declaration owner",
               "A class-scope full requires its exact written or copied body and primary owners.");
      return true;
    }
    auto Found = ClassFullDeclarations.find(D);
    if (Found == ClassFullDeclarations.end() || Found->second.empty()) {
      A.reject(D->getLocation(), "class full declaration source",
               "Each original or copied full requires its successful primary argument source.");
      return true;
    }
    if (DefinitionFrames.size() >= 64 || !ActiveClassFullDeclarations.insert(D).second) {
      A.reject(D->getLocation(), "class full declaration source depth",
               "Full declaration source checking must be bounded and acyclic.");
      return true;
    }
    auto RestoreActive = llvm::make_scope_exit([&] { ActiveClassFullDeclarations.erase(D); });
    if (const auto *Origin = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
            D->getInstantiatedFromMemberClass())) {
      if (!checkClassFullDeclarationSource(Origin))
        return false;
      if (!A.S.Diagnostics.empty())
        return true;
    }
    auto *SavedFunction = CurrentFunction;
    auto *SavedMethod = CurrentMethod;
    auto *SavedField = CurrentDefaultField;
    auto SavedInitializer = ImplicitInitializerOwner;
    CurrentFunction = nullptr;
    CurrentMethod = nullptr;
    CurrentDefaultField = nullptr;
    ImplicitInitializerOwner = D->getLocation();
    // A full has no parameter level. Only the exact primary-use frame below
    // provides checked inner arguments; actual outer substitutions keep theirs.
    DefinitionFrames.push_back({D, nullptr, nullptr, TemplateFrames.size()});
    auto Restore = llvm::make_scope_exit([&] {
      DefinitionFrames.pop_back();
      CurrentFunction = SavedFunction;
      CurrentMethod = SavedMethod;
      CurrentDefaultField = SavedField;
      ImplicitInitializerOwner = SavedInitializer;
    });
    const TemplateUseSource *First = nullptr;
    for (const auto *Source : Found->second) {
      if (!classFullDeclarationIdentity(*Source) ||
          (First && (First->Origin != Source->Origin || !equivalentUse(*First, *Source)))) {
        A.reject(D->getLocation(), "class full declaration source identity",
                 "The full declaration, primary, arguments and exact copy origin must agree.");
        return true;
      }
      First = Source;
      if (!checkTemplateUse(*Source, Source->Written->arguments(), genericClassFullIdentity(D)))
        return false;
      if (!A.S.Diagnostics.empty())
        return true;
    }
    CheckedClassFullDeclarations.insert(D);
    return true;
  }
  bool checkOrdinaryClassSource(const CXXRecordDecl *D) {
    if (CheckedOrdinaryClasses.count(D))
      return true;
    if (!ordinaryClassDeclarationShape(D)) {
      A.reject(D->getLocation(), "ordinary member class source owner",
               "A named member requires its exact written declaration and checked copy edges.");
      return true;
    }
    if (ActiveOrdinaryClasses.size() >= 64 || !ActiveOrdinaryClasses.insert(D).second) {
      A.reject(D->getLocation(), "ordinary member class source depth",
               "Ordinary member declaration sources must be bounded and acyclic.");
      return true;
    }
    auto RestoreActive = llvm::make_scope_exit([&] { ActiveOrdinaryClasses.erase(D); });
    if (const auto *Origin = D->getInstantiatedFromMemberClass()) {
      if (!checkOrdinaryClassSource(Origin))
        return false;
      if (!A.S.Diagnostics.empty())
        return true;
    }
    // A declaration's written headers/qualifier are checked without walking
    // lazy fields or methods. An ordinary body adds no argument-list owner.
    if (DefinitionFrames.size() >= 64) {
      A.reject(D->getLocation(), "ordinary member class definition depth",
               "Member declaration sources require a bounded definition context.");
      return true;
    }
    auto *SavedFunction = CurrentFunction;
    auto *SavedMethod = CurrentMethod;
    auto *SavedField = CurrentDefaultField;
    auto SavedInitializer = ImplicitInitializerOwner;
    CurrentFunction = nullptr;
    CurrentMethod = nullptr;
    CurrentDefaultField = nullptr;
    ImplicitInitializerOwner = D->getLocation();
    DefinitionFrames.push_back({D, nullptr, nullptr, TemplateFrames.size()});
    auto RestoreDefinition = llvm::make_scope_exit([&] {
      DefinitionFrames.pop_back();
      CurrentFunction = SavedFunction;
      CurrentMethod = SavedMethod;
      CurrentDefaultField = SavedField;
      ImplicitInitializerOwner = SavedInitializer;
    });
    auto CheckWritten = [&](const CXXRecordDecl *Written) {
      if (!outerTemplateListsShape(Written)) {
        A.reject(Written->getLocation(), "ordinary member class outer headers",
                 "Each header must match its actual enclosing parameter owner.");
        return true;
      }
      for (unsigned I = 0; I < Written->getNumTemplateParameterLists(); ++I) {
        const auto *Parameters = Written->getTemplateParameterList(I);
        A.chargeExpansion(1, Written->getLocation());
        if (Parameters->size() && !traverseTemplateParameterSource(Parameters, true))
          return false;
      }
      return !Written->getQualifier() || Written->getQualifier()->isDependent() ||
             TraverseNestedNameSpecifierLoc(Written->getQualifierLoc());
    };
    unsigned Count = 0;
    for (const auto *Tag : D->redecls()) {
      if (++Count > 64) {
        A.reject(D->getLocation(), "ordinary member class redeclaration depth",
                 "Each member record requires a bounded redeclaration chain.");
        return true;
      }
      const auto *Written = dyn_cast<CXXRecordDecl>(Tag);
      if (Written && WrittenOrdinaryClasses.count(Written) && !CheckWritten(Written))
        return false;
      if (!A.S.Diagnostics.empty())
        return true;
    }
    // A concrete copied qualifier can retain substituted source of its own.
    if (!WrittenOrdinaryClasses.count(D) && !CheckWritten(D))
      return false;
    if (A.S.Diagnostics.empty())
      CheckedOrdinaryClasses.insert(D);
    return true;
  }
  bool checkClassFullOwnerSources(const DeclContext *Context) {
    std::set<const DeclContext *> Seen;
    while (const auto *Record = dyn_cast_or_null<CXXRecordDecl>(Context)) {
      A.chargeExpansion(1, Record->getLocation());
      // The starting record has depth zero: 64 enclosing edges contain 65
      // records, matching record layout and type dependency validation.
      if (Seen.size() > 64 || !Seen.insert(Context).second) {
        A.reject(Record->getLocation(), "class full owner source depth",
                 "Enclosing full declaration sources must be bounded and acyclic.");
        return true;
      }
      if (ordinaryMemberClassScope(Record)) {
        if (!checkOrdinaryClassSource(Record))
          return false;
        if (!A.S.Diagnostics.empty())
          return true;
      }
      if (genericClassFullIdentity(Record) ||
          (classScopeFullIdentity(Record) && Record->getInstantiatedFromMemberClass())) {
        const auto *Full = dyn_cast<ClassTemplateSpecializationDecl>(Record);
        if (!Full || !classFullDeclarationShape(Full)) {
          A.reject(Record->getLocation(), "class full owner source",
                   "An enclosing copied class must have an admitted full declaration source.");
          return true;
        }
        if (!checkClassFullDeclarationSource(Full))
          return false;
        if (!A.S.Diagnostics.empty())
          return true;
      }
      Context = Record->getDeclContext();
    }
    return true;
  }
  bool checkTemplateUse(const TemplateUseSource &Source,
                        llvm::ArrayRef<TemplateArgumentLoc> Written,
                        bool DeferredDeclaration = false) {
    const auto L = Source.Location;
    A.chargeExpansion(1, L);
    const bool PartialDeclaration = Source.Kind == TemplateSourceKind::PartialDeclaration;
    const bool OriginalClassFull = Source.Kind == TemplateSourceKind::ClassFullDeclaration &&
        !Source.Origin && classFullDeclarationIdentity(Source) &&
        genericClassFullIdentity(dyn_cast_or_null<CXXRecordDecl>(Source.Declaration));
    if (DeferredDeclaration && !(OriginalClassFull ||
        (PartialDeclaration ? partialDeclarationIdentity(Source)
        : Source.Kind == TemplateSourceKind::VariableDeclaration && genericMemberVariableFullShape(
            dyn_cast_or_null<VarTemplateSpecializationDecl>(Source.Declaration))))) {
      A.reject(L, "generic template source mode", "Only an exact partial or original class-scope full declaration may retain pending source.");
      return true;
    }
    const auto *SourceParameters = templateSourceParameters(Source.Template);
    const auto Frontier = (PartialDeclaration || OriginalClassFull) && SourceParameters
        ? partialArgumentFrontier(SourceParameters, Written) : PartialArgumentFrontier{};
    if (!templateSourceShape(Source.Template) || !Source.Canonical || !Source.Sugared ||
        Source.DefaultsOverflow || Source.Defaults.size() > 64 || Source.ParameterTypes.size() > 4096 ||
        (!Frontier && Source.Canonical->size() != SourceParameters->size()) ||
        (Frontier && (!DeferredDeclaration || !Source.Defaults.empty() ||
          Source.Canonical->size() != Written.size() || Written.size() > 64 ||
          Frontier.Index >= Source.Canonical->size() ||
          !Source.Canonical->get(Frontier.Index).isInstantiationDependent())) ||
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
    if (Source.Kind == TemplateSourceKind::ClassFullDeclaration &&
        !classFullDeclarationIdentity(Source)) {
      A.reject(L, "class full declaration source", "A checked exact full declaration source is required.");
      return true;
    }
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
    const unsigned Matched = Frontier ? Frontier.Index : Count;
    std::vector<bool> Pending(Count, false);
    if (Frontier)
      for (unsigned I = Matched; I < Count; ++I)
        Pending[I] = true;
    if (DeferredDeclaration) {
      for (unsigned I = 0; I < Matched; ++I)
        Pending[I] = Source.Canonical->get(I).isInstantiationDependent() ||
                     Source.Sugared->get(I).isInstantiationDependent();
      // Clang deliberately does not substitute a dependent parameter type in
      // a dependent outer context. Even a written integer then remains pending.
      for (const auto &Type : Source.ParameterTypes)
        if (const auto *Parameter = dyn_cast_or_null<NonTypeTemplateParmDecl>(Type.Parameter);
            Parameter && Parameter->getIndex() < Count && pendingGenericParameterType(Type.Type))
          Pending[Parameter->getIndex()] = true;
      for (const auto &Default : Source.Defaults) {
        const auto *Type = dyn_cast_or_null<TemplateTypeParmDecl>(Default.Parameter);
        const auto *Value = dyn_cast_or_null<NonTypeTemplateParmDecl>(Default.Parameter);
        const unsigned Index = Type ? Type->getIndex() : Value ? Value->getIndex() : Count;
        if (Index < Count && Default.Converted.getArgument().isInstantiationDependent())
          Pending[Index] = true;
      }
    }
    checkTemplateArguments(Parameters, *Source.Canonical, L,
                           DeferredDeclaration ? &Pending : nullptr, Frontier ? Matched : ~0u);
    if (!A.S.Diagnostics.empty())
      return true;
    for (unsigned I = 0; I < Source.Canonical->size(); ++I) {
      const auto &Canonical = Source.Canonical->get(I);
      const auto &Sugared = Source.Sugared->get(I);
      bool Bounded = Canonical.getKind() == Sugared.getKind();
      if (Bounded && Canonical.getKind() == TemplateArgument::Pack) {
        Bounded = Canonical.pack_size() == Sugared.pack_size();
        if (Bounded)
          for (const auto &Element : Sugared.pack_elements())
            Bounded &= Element.getKind() == TemplateArgument::Type ||
                       !scalarTemplateArgumentType(Element).isNull() ||
                       (DeferredDeclaration && (Frontier || Pending[I]) && Element.getKind() == TemplateArgument::Expression);
      }
      if (!Bounded || !Canonical.structurallyEquals(A.Context.getCanonicalTemplateArgument(Sugared))) {
        A.reject(L, "template argument sugar", "Written substitution arguments must preserve their bounded canonical values.");
        return true;
      }
    }
    if (Frontier)
      for (unsigned I = Matched; I < Source.Canonical->size(); ++I) {
        const auto &Argument = Source.Canonical->get(I);
        if ((Argument.getKind() != TemplateArgument::Type &&
             scalarTemplateArgumentType(Argument).isNull() &&
             Argument.getKind() != TemplateArgument::Expression) ||
            (Argument.getKind() == TemplateArgument::Type && Argument.getAsType().isNull()) ||
            (Argument.getKind() == TemplateArgument::Expression && !Argument.getAsExpr())) {
          A.reject(L, "partial expansion source", "An unknown-length tail requires its supported actual type or scalar syntax.");
          return true;
        }
        if (Argument.getKind() == TemplateArgument::Expression && !Argument.getAsExpr()->isTypeDependent())
          A.type(Argument.getAsExpr()->getType(), L);
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
          (!sourceMatchesArgument(Default.Converted, Source.Canonical->get(Index)) &&
           !(DeferredDeclaration && Pending[Index] && Source.Canonical->get(Index).structurallyEquals(
               A.Context.getCanonicalTemplateArgument(Default.Converted.getArgument()))))) {
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
      if (Index >= Count || (Frontier && (Index > Frontier.Index ||
          (Index == Frontier.Index && Type.PackIndex > Frontier.PackIndex))) ||
          !parameterInTemplate(Source.Template, Type.Parameter, Index) ||
          (!Parameter->isParameterPack() && Type.PackIndex != 0) ||
          (Parameter->isParameterPack() && Type.PackIndex >= 64 && Type.PackIndex != ~0u) ||
          !ParameterTypes[Index].emplace(Type.PackIndex, &Type).second) {
        A.reject(L, "template parameter source slots", "Parameter source records must identify distinct owned scalar slots and forward pack positions.");
        return true;
      }
    }
    for (unsigned Index = 0; Index < Count; ++Index) {
      unsigned Expected = 0;
      bool Empty = false;
      if ((!Frontier || Index <= Frontier.Index) &&
          isa<NonTypeTemplateParmDecl>(Parameters->getParam(Index))) {
        const auto &Argument = Source.Canonical->get(Index);
        Expected = Frontier && Index == Frontier.Index ? Frontier.PackIndex + 1
            : Argument.getKind() == TemplateArgument::Pack ? Argument.pack_size() : 1;
        // Function and partial deduction substitute empty NTTP pack types;
        // primary class/alias argument checking forms an empty pack directly.
        Empty = !Expected && (Source.Kind == TemplateSourceKind::Function ||
                               Source.Kind == TemplateSourceKind::PartialDeduction ||
                               Source.Kind == TemplateSourceKind::VariablePartialDeduction);
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
    for (unsigned Index = 0; Index < Matched; ++Index) {
      const auto &Argument = Source.Canonical->get(Index);
      if (Defaults[Index])
        continue;
      unsigned Elements = Argument.getKind() == TemplateArgument::Pack ? Argument.pack_size() : 1;
      if (Elements > 64)
        return true; // checkTemplateArguments already diagnosed this pack.
      unsigned Available = std::min(Elements, WrittenCount - Position);
      for (unsigned E = 0; E < Available; ++E) {
        const auto &Argument = Written[Position++];
        if (DeferredDeclaration && !A.S.owns(A.Sources, argumentLocation(Argument))) {
          A.reject(L, "generic written argument source", "Each original argument must retain owned syntax.");
          return true;
        }
        if ((!DeferredDeclaration || !Argument.getArgument().isInstantiationDependent()) &&
            !TraverseTemplateArgumentLoc(Argument))
          return false;
        if (DeferredDeclaration && Argument.getArgument().isInstantiationDependent())
          Pending[Index] = true;
      }
      if (Source.Kind != TemplateSourceKind::Function &&
          Source.Kind != TemplateSourceKind::PartialDeduction &&
          Source.Kind != TemplateSourceKind::VariablePartialDeduction && Available != Elements) {
        A.reject(L, "template argument source", "Every nondeduced argument requires its written source or selected default.");
        return true;
      }
      // Function arguments not written in <...> come from ordinary deduction;
      // their call expressions and instantiated signatures are checked by RAV.
      Frame.Ready[Index] = !DeferredDeclaration ||
                           (!Pending[Index] && ParameterTypes[Index].empty());
    }
    if (Frontier) {
      // A checked expansion at a fixed/expanded parameter ends Sema's slot
      // matching. The remaining written arguments retain syntax, not slots.
      if (Frontier.WrittenIndex != Matched + Frontier.PackIndex) {
        A.reject(L, "partial expansion position", "The source frontier must match this exact primary prefix.");
        return true;
      }
      while (Position < WrittenCount) {
        const auto &Argument = Written[Position++];
        if (!A.S.owns(A.Sources, argumentLocation(Argument))) {
          A.reject(L, "partial tail source", "Every unresolved tail argument requires its owned written source.");
          return true;
        }
        if (!Argument.getArgument().isInstantiationDependent() && !TraverseTemplateArgumentLoc(Argument))
          return false;
      }
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
        if (Frontier && Index == Frontier.Index) {
          // Sema appends the expansion before flushing earlier elements of
          // the currently expanded pack. Preserve that actual CTAI layout.
          Argument = Entry.first == Frontier.PackIndex ? nullptr
              : &Source.Canonical->get(Frontier.Index + 1 + Entry.first);
        } else if (Entry.first == ~0u)
          Argument = nullptr;
        else if (Canonical.getKind() == TemplateArgument::Pack)
          Argument = &Canonical.pack_elements()[Entry.first];
        auto *Info = Entry.second->Type;
        if (DeferredDeclaration && (!Info || Info->getType().isNull() ||
            !A.S.owns(A.Sources, Info->getTypeLoc().getBeginLoc()) ||
            Info->getType().isVolatileQualified() || Info->getType().isRestrictQualified())) {
          A.reject(L, "generic parameter type source", "Each original parameter slot needs owned supported type syntax.");
          return true;
        }
        if (DeferredDeclaration && pendingGenericParameterType(Info))
          continue; // This exact pending type is checked on the actual outer copy.
        if (DeferredDeclaration && Argument && Argument->isInstantiationDependent())
          Argument = nullptr; // Check its resolved type source before its value exists.
        if (!checkNonTypeParameterSource(*Entry.second, Argument, L))
          return false;
        if (!A.S.Diagnostics.empty())
          return true;
      }
      const auto *Default = Defaults[Index];
      if (!Default) {
        if (DeferredDeclaration)
          Frame.Ready[Index] = !Pending[Index];
        continue;
      }
      for (const auto *Argument : {&Default->Written, &Default->Converted}) {
        const bool Dependent = Argument->getArgument().isInstantiationDependent();
        if (!A.S.owns(A.Sources, argumentLocation(*Argument)) ||
            (Dependent && (!DeferredDeclaration ||
                          !Default->Converted.getArgument().isInstantiationDependent()))) {
          A.reject(L, "template default source", "A selected default requires concrete owned source evidence.");
          return true;
        }
        if ((!DeferredDeclaration || !Dependent) && !TraverseTemplateArgumentLoc(*Argument))
          return false;
      }
      Frame.Ready[Index] = !Pending[Index];
    }
    if (Source.Kind == TemplateSourceKind::VariablePartialDeduction) {
      auto Found = VariablePartialSources.find(Source.Selection);
      if (Found == VariablePartialSources.end() || Found->second.Conflict ||
          Found->second.Deduction != &Source || !Found->second.Pattern ||
          !Found->second.Pattern->Written || !Source.Defaults.empty()) {
        A.reject(L, "variable partial pattern source", "A selected variable deduction requires its exact paired pattern.");
        return true;
      }
      const auto &Pattern = *Found->second.Pattern;
      return checkTemplateUse(Pattern, Pattern.Written->arguments());
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
      if (L.Parameter != R.Parameter || !hasArgumentSource(L.Written) ||
          !hasArgumentSource(R.Written) || !hasArgumentSource(L.Converted) ||
          !hasArgumentSource(R.Converted) ||
          L.Written.getSourceRange() != R.Written.getSourceRange() ||
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
                                 SourceLocation QualifiedBegin = {},
                                 SourceLocation CallLocation = {}) {
    if (!concreteFunctionTemplate(Function))
      return;
    const auto *Method = dyn_cast<CXXMethodDecl>(Function);
    if (QualifiedBegin == Location || (Method && !Method->isStatic()))
      QualifiedBegin = {};
    if (CallLocation == Location || CallLocation == QualifiedBegin)
      CallLocation = {};
    const TemplateUseSource *First = nullptr;
    // Overload-call deduction uses the unresolved expression's begin location,
    // including a written namespace qualifier or enclosing parentheses.
    // Address selection and explicit directives use the name location. Each
    // location is tied to this exact selected function and source expression.
    for (auto UseLocation : {Location, QualifiedBegin, CallLocation}) {
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
  SourceLocation directTemplateCallLocation(const Expr *Reference) const {
    auto Found = DirectTemplateCallLocations.find(Reference);
    return Found == DirectTemplateCallLocations.end() ? SourceLocation() : Found->second;
  }
  void indexSelectedTemplateCalls(llvm::ArrayRef<SelectedTemplateCallSource> Sources) {
    for (const auto &Source : Sources) {
      A.chargeExpansion(1, Source.Location);
      const FunctionDecl *Selected = nullptr;
      if (const auto *Construction = dyn_cast_or_null<CXXConstructExpr>(Source.Expression))
        Selected = Construction->getConstructor();
      else if (const auto *Call = dyn_cast_or_null<CXXMemberCallExpr>(Source.Expression))
        Selected = Call->getDirectCallee();
      else if (const auto *Allocation = dyn_cast_or_null<CXXNewExpr>(Source.Expression))
        Selected = Allocation->getOperatorNew();
      if (!Selected || Selected != Source.Function ||
          !(isa<CXXNewExpr>(Source.Expression) ? concreteFunctionTemplate(Selected)
                                              : concreteMemberFunctionTemplate(Selected)) || !owned(Selected) ||
          !A.S.owns(A.Sources, Source.Location)) {
        A.reject(Source.Location, "selected member template source", "A retained record requires its actual direct source-owned template call.");
        continue;
      }
      SelectedCallSources[Source.Expression].push_back(&Source);
    }
  }
  bool variableSourceIdentity(const VarTemplateSpecializationDecl *Variable,
                               const TemplateUseSource &Source, bool GenericFull = false) {
    const auto L = Source.Location;
    if (Source.Kind == TemplateSourceKind::VariableDeclaration && Source.WrittenStorageClass) {
      A.reject(L, "variable specialization storage class",
               "An explicit variable specialization cannot carry a storage-class specifier.", "TR0202");
      return false;
    }
    if (!Variable || Variable->getKind() != Decl::VarTemplateSpecialization ||
        !variablePatternType(Variable) ||
        (GenericFull ? !genericMemberVariableFullShape(Variable)
                     : Variable->getDeclContext()->isDependentContext()) ||
        (GenericFull && Source.Kind != TemplateSourceKind::VariableDeclaration) ||
        !variableTemplateShape(Variable->getSpecializedTemplate()) ||
        (Source.Kind != TemplateSourceKind::VariableUse &&
         Source.Kind != TemplateSourceKind::VariableDeclaration) ||
        Source.Declaration != Variable || !Source.Template || !Source.Written ||
        Source.Template->getCanonicalDecl() != Variable->getSpecializedTemplate()->getCanonicalDecl() ||
        !sameArguments(Source.Canonical, &Variable->getTemplateArgs()) ||
        !sameCanonicalArguments(Source.Canonical, Source.Sugared) ||
        Source.DefaultsOverflow || Source.Defaults.size() > 64 || Source.ParameterTypes.size() > 4096) {
      A.reject(L, "variable template source identity",
               "Each concrete variable needs its own successful primary argument evidence.");
      return false;
    }
    return GenericFull || !Variable->isExplicitSpecialization() ||
           Source.Kind != TemplateSourceKind::VariableDeclaration ||
           copiedMemberVariableFullSource(Variable, Source);
  }
  bool genericMemberVariableFullShape(const VarTemplateSpecializationDecl *D) {
    if (!D || D->getKind() != Decl::VarTemplateSpecialization ||
        !D->isExplicitSpecialization() || !variablePatternType(D) ||
        !D->getDeclContext()->isDependentContext() ||
        D->getLexicalDeclContext() != D->getDeclContext() ||
        !variableTemplateShape(D->getSpecializedTemplate()) ||
        D->getSpecializedTemplate()->getDeclContext() != D->getDeclContext() ||
        !D->getTemplateArgsAsWritten())
      return false;
    return isa<CXXRecordDecl>(D->getDeclContext());
  }
  bool traverseGenericMemberVariableFull(VarTemplateSpecializationDecl *D) {
    if (CheckedVariableDeclarations.count(D))
      return true;
    if (!genericMemberVariableFullShape(D) || !VisitDecl(D)) {
      A.reject(D->getLocation(), "generic full member variable",
               "A class-scope full declaration must belong to its admitted dependent outer class.");
      return true;
    }
    auto Found = VariableDeclarations.find(D);
    if (Found == VariableDeclarations.end() || Found->second.empty()) {
      A.reject(D->getLocation(), "generic full variable source",
               "The original full declaration needs its own successful argument source.");
      return true;
    }
    auto *SavedFunction = CurrentFunction;
    auto *SavedMethod = CurrentMethod;
    auto *SavedField = CurrentDefaultField;
    auto SavedInitializer = ImplicitInitializerOwner;
    CurrentFunction = nullptr;
    CurrentMethod = nullptr;
    CurrentDefaultField = nullptr;
    ImplicitInitializerOwner = D->getLocation();
    // The primary argument frame is installed only by checkTemplateUse. The
    // full definition itself has no inner slots and cannot borrow caller slots.
    DefinitionFrames.push_back({D, nullptr, nullptr, TemplateFrames.size()});
    auto Restore = llvm::make_scope_exit([&] {
      DefinitionFrames.pop_back();
      CurrentFunction = SavedFunction;
      CurrentMethod = SavedMethod;
      CurrentDefaultField = SavedField;
      ImplicitInitializerOwner = SavedInitializer;
    });
    for (const auto *Source : Found->second) {
      A.chargeExpansion(1, Source->Location);
      if (!variableSourceIdentity(D, *Source, true))
        return true;
      if (!checkTemplateUse(*Source, Source->Written->arguments(), true))
        return false;
      auto *Info = Source->Underlying;
      if (!Info || Info->getType().isNull() ||
          !A.S.owns(A.Sources, Info->getTypeLoc().getBeginLoc())) {
        A.reject(Source->Location, "generic full variable type source",
                 "The original full declaration must retain its owned written type.");
        return true;
      }
      auto Type = Info->getType();
      const bool AutoToken = Type->isUndeducedAutoType() &&
          !Info->getTypeLoc().getUnqualifiedLoc().getAs<AutoTypeLoc>().isNull();
      if (!AutoToken && !A.Context.hasSameType(Type, D->getType())) {
        A.reject(Source->Location, "generic full variable type identity",
                 "The written type must belong to this exact full declaration.");
        return true;
      }
      if (!Type->isInstantiationDependentType() && !Type->isDependentType()) {
        if (!AutoToken)
          A.type(Type, Source->Location);
        if (!TraverseTypeLoc(Info->getTypeLoc()))
          return false;
      }
      if (!A.S.Diagnostics.empty())
        return true;
    }
    if (D->getQualifier() && !D->getQualifier()->isDependent() &&
        !TraverseNestedNameSpecifierLoc(D->getQualifierLoc()))
      return false;
    // Initializers still depending on an outer instance stay lazy, as do the
    // generic primary/partial initializers. Concrete copies check them normally.
    if (A.S.Diagnostics.empty())
      CheckedVariableDeclarations.insert(D);
    return true;
  }
  bool copiedMemberVariableFullOrigin(const VarTemplateSpecializationDecl *Variable,
                                      const VariableTypeSource &TypeSource) {
    const auto *Pattern = dyn_cast_or_null<VarTemplateSpecializationDecl>(TypeSource.Pattern);
    const auto *Parent = dyn_cast<CXXRecordDecl>(Variable->getDeclContext());
    const auto *Primary = Variable->getSpecializedTemplate();
    const auto *Origin = Primary->getInstantiatedFromMemberTemplate();
    const auto *Selected = Parent ? classBodyRecord(Parent) : nullptr;
    if (TypeSource.Variable != Variable || !Variable->isExplicitSpecialization() ||
        !genericMemberVariableFullShape(Pattern) || !owned(Parent) ||
        (Parent->getKind() != Decl::ClassTemplateSpecialization && !ordinaryCopiedBody(Parent)) ||
        Parent->isDependentContext() ||
        !Origin || !Selected || Primary->getDeclContext() != Parent ||
        Variable->getDeclName() != Pattern->getDeclName() ||
        Origin->getCanonicalDecl() != Pattern->getSpecializedTemplate()->getCanonicalDecl() ||
        Selected->getCanonicalDecl() !=
            cast<CXXRecordDecl>(Pattern->getDeclContext())->getCanonicalDecl() ||
        !TypeSource.Type || !variablePreviousIdentity(Variable, TypeSource)) {
      A.reject(TypeSource.Location, "copied full member variable origin",
               "The copied full declaration needs its exact original member and selected outer class.");
      return false;
    }
    if (!traverseGenericMemberVariableFull(
            const_cast<VarTemplateSpecializationDecl *>(Pattern)))
      return false;
    return A.S.Diagnostics.empty();
  }
  bool copiedMemberVariableFullSource(const VarTemplateSpecializationDecl *Variable,
                                      const TemplateUseSource &Source) {
    auto Found = VariableTypeSources.find(Variable);
    if (Found == VariableTypeSources.end() || Found->second.empty())
      return true; // A directly written concrete full declaration is independent.
    bool Joined = false;
    for (const auto *TypeSource : Found->second) {
      A.chargeExpansion(1, TypeSource->Location);
      if (!copiedMemberVariableFullOrigin(Variable, *TypeSource))
        return false;
      Joined |= !TypeSource->Completion && TypeSource->Type == Source.Underlying;
    }
    if (!Joined)
      A.reject(Source.Location, "copied full variable declaration source",
               "The argument event must join the exact type event of this copied full declaration.");
    return Joined;
  }
  bool variablePatternOrigin(const NamedDecl *Owner, const VarDecl *Pattern) {
    if (!variablePatternShape(Owner) || !variablePatternType(Pattern))
      return false;
    const auto *SourceOwner = variableTemplatePattern(Pattern);
    std::set<const Decl *> Seen;
    for (const auto *Current = Owner; Current;) {
      A.chargeExpansion(1, Current->getLocation());
      if (Seen.size() >= 64 || !Seen.insert(Current->getCanonicalDecl()).second ||
          !variablePatternShape(Current))
        return false;
      if (SourceOwner && SourceOwner->getCanonicalDecl() == Current->getCanonicalDecl() &&
          Pattern->getCanonicalDecl() == variablePatternDecl(Current)->getCanonicalDecl())
        return true;
      if (const auto *Primary = dyn_cast<VarTemplateDecl>(Current)) {
        if (Primary->isMemberSpecialization())
          return false; // An own member definition replaces the generic pattern.
        Current = Primary->getInstantiatedFromMemberTemplate();
      } else {
        const auto *Partial = cast<VarTemplatePartialSpecializationDecl>(Current);
        if (Partial->isMemberSpecialization())
          return false;
        Current = Partial->getInstantiatedFromMember();
      }
    }
    return false;
  }
  bool variablePreviousIdentity(const VarTemplateSpecializationDecl *Variable,
                                const VariableTypeSource &Source) {
    const auto *Previous = Source.Previous;
    if (!Previous)
      return true;
    const auto *Parent = dyn_cast<CXXRecordDecl>(Variable->getDeclContext());
    const auto *PreviousParent = dyn_cast<CXXRecordDecl>(Previous->getDeclContext());
    if (Source.Variable != Variable || Source.Completion || Previous == Variable ||
        Variable->getPreviousDecl() != Previous ||
        Variable->getKind() != Decl::VarTemplateSpecialization ||
        Previous->getKind() != Decl::VarTemplateSpecialization ||
        !variablePatternType(Variable) || !variablePatternType(Previous) ||
        Variable->getDeclContext()->isDependentContext() ||
        Previous->getDeclContext()->isDependentContext() || !Parent || !PreviousParent ||
        Parent->getCanonicalDecl() != PreviousParent->getCanonicalDecl() ||
        Variable->getCanonicalDecl() != Previous->getCanonicalDecl() ||
        Variable->getSpecializedTemplate()->getCanonicalDecl() !=
            Previous->getSpecializedTemplate()->getCanonicalDecl() ||
        !sameArguments(&Variable->getTemplateArgs(), &Previous->getTemplateArgs()) ||
        Variable->getSpecializedTemplateOrPartial() != Previous->getSpecializedTemplateOrPartial() ||
        (Variable->getSpecializedTemplateOrPartial().is<VarTemplatePartialSpecializationDecl *>() &&
         &Variable->getTemplateInstantiationArgs() != &Previous->getTemplateInstantiationArgs()) ||
        !owned(Source.Pattern) || !Source.Pattern->isThisDeclarationADefinition()) {
      A.reject(Source.Location, "member variable previous declaration",
               "A separate definition must retain its exact preceding scalar instance and selected pattern.");
      return false;
    }
    return true;
  }
  const VarTemplateSpecializationDecl *variablePreviousDeclaration(
      const VarTemplateSpecializationDecl *Variable) {
    const VarTemplateSpecializationDecl *Previous = nullptr;
    auto Found = VariableTypeSources.find(Variable);
    if (Found == VariableTypeSources.end())
      return nullptr;
    for (const auto *Source : Found->second) {
      A.chargeExpansion(1, Source->Location);
      if (!variablePreviousIdentity(Variable, *Source))
        return nullptr;
      if (Source->Previous) {
        if (Previous && Previous != Source->Previous) {
          A.reject(Source->Location, "member variable previous source conflict",
                   "All first-type records for a definition must agree on its previous declaration.");
          return nullptr;
        }
        Previous = Source->Previous;
      }
    }
    return Previous;
  }
  const TemplateUseSource *variablePrimarySource(const VarTemplateSpecializationDecl *Variable,
                                                SourceLocation L) {
    std::set<const VarTemplateSpecializationDecl *> Seen;
    for (const auto *Current = Variable; Current;) {
      A.chargeExpansion(1, Current->getLocation());
      if (Seen.size() >= 64 || !Seen.insert(Current).second) {
        A.reject(L, "variable primary declaration depth", "The exact declaration chain must be bounded and acyclic.");
        return nullptr;
      }
      const auto *Previous = variablePreviousDeclaration(Current);
      if (!A.S.Diagnostics.empty())
        return nullptr;
      auto Found = VariableArgumentSources.find(Current);
      if (Found != VariableArgumentSources.end() && !Found->second.empty()) {
        for (const auto *Source : Found->second) {
          A.chargeExpansion(1, Source->Location);
          if (!variableSourceIdentity(Current, *Source))
            return nullptr;
        }
        checkTemplateArguments(Variable->getSpecializedTemplate()->getTemplateParameters(),
                               Variable->getTemplateArgs(), L);
        return A.S.Diagnostics.empty() ? Found->second.front() : nullptr;
      }
      Current = Previous; // Only the producer's exact previous-definition edge.
    }
    A.reject(L, "variable primary source", "A concrete variable needs a captured successful argument check.");
    return nullptr;
  }
  const TemplateUseSource *variablePartialSource(const VarTemplateSpecializationDecl *Variable,
                                                SourceLocation L) {
    if (!Variable || Variable->getKind() != Decl::VarTemplateSpecialization ||
        Variable->isExplicitSpecialization())
      return nullptr;
    const auto *Partial = Variable->getSpecializedTemplateOrPartial()
                              .dyn_cast<VarTemplatePartialSpecializationDecl *>();
    if (!Partial)
      return nullptr;
    if (!variablePartialShape(Partial) || !owned(Variable) || Variable->getDeclContext()->isDependentContext()) {
      A.reject(L, "selected variable partial", "An owned admitted variable partial is required.");
      return nullptr;
    }
    // Pinned variable selection transfers Info.takeSugared(), unlike classes.
    const auto *Selection = &Variable->getTemplateInstantiationArgs();
    auto Found = VariablePartialSources.find(Selection);
    if (Found == VariablePartialSources.end() || Found->second.Conflict ||
        !Found->second.Deduction || !Found->second.Pattern) {
      A.reject(L, "selected variable partial evidence", "The exact selected deduction and pattern source are required.");
      return nullptr;
    }
    const auto &Deduction = *Found->second.Deduction;
    const auto &Pattern = *Found->second.Pattern;
    if (Deduction.Kind != TemplateSourceKind::VariablePartialDeduction ||
        Pattern.Kind != TemplateSourceKind::VariablePartialPattern ||
        Deduction.Selection != Selection || Pattern.Selection != Selection ||
        Deduction.Template != Partial || Deduction.Declaration != Partial ||
        Pattern.Declaration != Partial || !Pattern.Template ||
        Pattern.Template->getCanonicalDecl() != Variable->getSpecializedTemplate()->getCanonicalDecl() ||
        !sameArguments(Deduction.Sugared, Selection) ||
        !sameCanonicalArguments(Deduction.Canonical, Selection) ||
        !sameArguments(Pattern.Canonical, &Variable->getTemplateArgs()) ||
        Deduction.Written || !Pattern.Written || !Deduction.Defaults.empty()) {
      A.reject(L, "selected variable partial identity", "The selected sugared list and both parameter sets must agree.");
      return nullptr;
    }
    const auto *Canonical = Variable->getCanonicalDecl();
    if (CheckedVariablePartials.count(Canonical))
      return &Deduction;
    if (ActiveVariablePartials.size() >= 64 ||
        std::find(ActiveVariablePartials.begin(), ActiveVariablePartials.end(), Canonical) != ActiveVariablePartials.end()) {
      A.reject(L, "variable partial source recursion", "Selected variable source exceeds its depth or contains a cycle.");
      return nullptr;
    }
    ActiveVariablePartials.push_back(Canonical);
    auto Restore = llvm::make_scope_exit([&] { ActiveVariablePartials.pop_back(); });
    if (!checkTemplateUse(Deduction, {}) || !A.S.Diagnostics.empty())
      return nullptr;
    CheckedVariablePartials.insert(Canonical);
    return &Deduction;
  }
  bool checkVariableTemplateUse(const VarTemplateSpecializationDecl *Variable,
                                SourceLocation L,
                                llvm::ArrayRef<TemplateArgumentLoc> Written,
                                bool CompareWritten = true) {
    auto Found = VariableSources.find({Variable, L.getRawEncoding()});
    if (Found == VariableSources.end() || Found->second.empty()) {
      A.reject(L, "variable use source", "This variable use needs its exact successful source event.");
      return true;
    }
    const TemplateUseSource *First = nullptr;
    for (const auto *Source : Found->second) {
      if (!variableSourceIdentity(Variable, *Source))
        return true;
      if ((First && !equivalentUse(*First, *Source)) ||
          (CompareWritten && Written.size() != Source->Written->NumTemplateArgs)) {
        A.reject(L, "variable use source conflict", "Repeated events must retain this use's complete written arguments.");
        return true;
      }
      if (CompareWritten)
        for (unsigned I = 0; I < Written.size(); ++I)
          if (!sameArgumentSource(Written[I], Source->Written->arguments()[I])) {
            A.reject(L, "variable argument source", "The actual argument's type or expression source must match.");
            return true;
          }
      First = Source;
      if (!checkTemplateUse(*Source, CompareWritten ? Written : Source->Written->arguments()))
        return false;
    }
    return true;
  }
  void indexTemplateSources(llvm::ArrayRef<TemplateUseSource> Sources,
                            llvm::ArrayRef<FunctionSpecializationSource> Specializations,
                            llvm::ArrayRef<VariableTypeSource> VariableTypes) {
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
      case TemplateSourceKind::ClassFullDeclaration:
        ClassFullDeclarations[Source.Declaration].push_back(&Source);
        break;
      case TemplateSourceKind::ClassDeclaration:
        ClassSources[Source.Declaration].push_back(&Source);
        break;
      case TemplateSourceKind::PartialDeclaration:
        PartialDeclarations[Source.Declaration].push_back(&Source);
        break;
      case TemplateSourceKind::VariableUse:
        if (const auto *Variable = dyn_cast_or_null<VarTemplateSpecializationDecl>(Source.Declaration))
          {
            VariableSources[{Variable, Source.Location.getRawEncoding()}].push_back(&Source);
            VariableArgumentSources[Variable].push_back(&Source);
          }
        break;
      case TemplateSourceKind::VariableDeclaration:
        if (const auto *Variable = dyn_cast_or_null<VarTemplateSpecializationDecl>(Source.Declaration))
          {
            VariableDeclarations[Variable].push_back(&Source);
            VariableArgumentSources[Variable].push_back(&Source);
          }
        break;
      case TemplateSourceKind::VariablePartialDeduction:
      case TemplateSourceKind::VariablePartialPattern: {
        auto &Pair = VariablePartialSources[Source.Selection];
        auto &Slot = Source.Kind == TemplateSourceKind::VariablePartialDeduction
                         ? Pair.Deduction : Pair.Pattern;
        if (Slot || !Source.Selection)
          Pair.Conflict = true;
        else
          Slot = &Source;
        break;
      }
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
    for (const auto &Source : VariableTypes) {
      A.chargeExpansion(1, Source.Location);
      VariableTypeSources[Source.Variable].push_back(&Source);
    }
    for (const auto &Source : Specializations) {
      A.chargeExpansion(1, Source.Location);
      SpecializationSources[Source.Declaration].push_back(&Source);
    }
  }
  void indexMemberTemplates() {
    std::vector<std::pair<Decl *, unsigned>> Work{
        {A.Context.getTranslationUnitDecl(), 0}};
    std::set<const Decl *> Seen;
    std::vector<ClassTemplateDecl *> Classes;
    std::set<const ClassTemplateDecl *> SeenClasses;
    std::vector<FunctionTemplateDecl *> Copies;
    std::set<const FunctionTemplateDecl *> SeenCopies;
    bool HiddenInstances = false;
    auto Queue = [&](Decl *D, unsigned Depth) {
      if (!D)
        return;
      A.chargeExpansion(1, D->getLocation());
      if (const auto *Record = dyn_cast<CXXRecordDecl>(D);
          Record && Record->isInjectedClassName())
        return;
      // Match the existing record-depth limit: the outermost record is
      // depth zero. Template wrappers, injected names and fields add no edge.
      if (isa<CXXRecordDecl>(D) && isa<CXXRecordDecl>(D->getDeclContext()))
        ++Depth;
      if (Depth > 64) {
        A.reject(D->getLocation(), "member template index depth",
                 "Nested declaration indexing exceeds the source depth limit.");
        throw Failure{};
      }
      Work.emplace_back(D, Depth);
    };
    auto QueueClassInstances = [&](ClassTemplateDecl *Template) {
      for (auto *Instance : Template->specializations())
        for (auto *Declaration : Instance->redecls())
          Queue(Declaration, 0);
      llvm::SmallVector<ClassTemplatePartialSpecializationDecl *, 4> Partials;
      Template->getPartialSpecializations(Partials);
      for (auto *Partial : Partials)
        for (auto *Declaration : Partial->redecls())
          Queue(Declaration, 0);
    };
    // Complete lexical indexing before visiting hidden specialization lists.
    // Copies reuse a written origin ordinal, independent of allocation order.
    for (unsigned Phase = 0; Phase != 2; ++Phase) {
      while (!Work.empty()) {
        auto [D, Depth] = Work.back();
        Work.pop_back();
        if (!Seen.insert(D).second ||
            (!isa<TranslationUnitDecl>(D) && !owned(D)))
          continue;
        if (!HiddenInstances && ordinaryMemberClassIdentity(dyn_cast<CXXRecordDecl>(D)))
          WrittenOrdinaryClasses.insert(cast<CXXRecordDecl>(D));
        if (!HiddenInstances && genericClassFullIdentity(dyn_cast<CXXRecordDecl>(D)))
          WrittenClassFullDeclarations.insert(D);
        if (!HiddenInstances)
          if (const auto *Friend = dyn_cast<FriendDecl>(D);
              Friend && !FriendDeclarationSources.count(Friend) && !copiedFriendTemplate(Friend) &&
              !copiedFriendClassTemplate(Friend)) {
            WrittenFriendDeclarations.insert(Friend);
            if (const auto *Template = dyn_cast_or_null<ClassTemplateDecl>(Friend->getFriendDecl())) {
              WrittenFriendClasses.emplace(Template, Friend);
              FriendClassCanonicals.insert(Template->getCanonicalDecl());
              if (!checkWrittenFriendClassDefaults(Template))
                return;
              indexTemplatePackSources(Template);
            }
            if (const auto *Function = dyn_cast_or_null<FunctionDecl>(Friend->getFriendDecl()))
              WrittenFriendFunctions.emplace(Function, Friend);
            if (const auto *Template = dyn_cast_or_null<FunctionTemplateDecl>(Friend->getFriendDecl())) {
              WrittenFriendTemplates.emplace(Template, Friend);
              FriendTemplateCanonicals.insert(Template->getCanonicalDecl());
              indexTemplatePackSources(Template);
              const auto *Canonical = Template->getCanonicalDecl();
              IndependentTemplateIdentities.insert(Canonical);
              if (!A.TemplateOrdinals.count(Canonical))
                A.TemplateOrdinals.emplace(Canonical, A.TemplateOrdinals.size());
            }
          }
        if (auto *Friend = dyn_cast<FriendDecl>(D))
          if (auto *Template = dyn_cast_or_null<ClassTemplateDecl>(Friend->getFriendDecl()))
            Queue(Template, Depth);
        if (!HiddenInstances &&
            isa<ClassTemplatePartialSpecializationDecl, VarTemplatePartialSpecializationDecl>(D))
          WrittenPartialDeclarations.insert(D);
        if (const auto *Variable = dyn_cast<VarTemplateDecl>(D)) {
          indexTemplatePackSources(Variable);
          // Copied member partials can live only in the template's hidden list.
          llvm::SmallVector<VarTemplatePartialSpecializationDecl *, 4> Partials;
          Variable->getPartialSpecializations(Partials);
          for (const auto *Partial : Partials)
            for (const auto *Declaration : Partial->redecls()) {
              A.chargeExpansion(1, Declaration->getLocation());
              if (const auto *Pattern = dyn_cast<VarTemplatePartialSpecializationDecl>(Declaration))
                indexTemplatePackSources(Pattern);
            }
          continue;
        }
        if (const auto *Partial = dyn_cast<VarTemplatePartialSpecializationDecl>(D)) {
          indexTemplatePackSources(Partial);
          if (!HiddenInstances)
            WrittenVariablePartials.insert(cast<VarTemplatePartialSpecializationDecl>(Partial->getCanonicalDecl()));
          continue;
        }
        if (const auto *Alias = dyn_cast<TypeAliasTemplateDecl>(D)) {
          // Aliases erase to their checked types and need no function ordinal.
          // Both written and copied parameter packs retain their exact owner.
          indexTemplatePackSources(Alias);
          continue;
        }
        if (auto *Template = dyn_cast<FunctionTemplateDecl>(D)) {
          auto *Canonical = Template->getCanonicalDecl();
          const auto *Method = dyn_cast<CXXMethodDecl>(Template->getTemplatedDecl());
          if (!Method)
            continue; // Namespace primaries retain the existing ordinal pass.
          indexTemplatePackSources(Template);
          if (Canonical->getInstantiatedFromMemberTemplate()) {
            if (SeenCopies.insert(Canonical).second)
              Copies.push_back(Canonical);
          } else if (!A.TemplateOrdinals.count(Canonical)) {
            // A hidden copied primary must never acquire an allocation-order
            // identity merely because its origin metadata is missing.
            if (HiddenInstances) {
              A.reject(Template->getLocation(), "member template index origin",
                       "A hidden member primary requires its written template origin.");
              return;
            }
            A.TemplateOrdinals.emplace(Canonical, A.TemplateOrdinals.size());
          }
          continue;
        }
        if (auto *Template = dyn_cast<ClassTemplateDecl>(D)) {
          if (!HiddenInstances)
            WrittenClassTemplates.insert(Template);
          // Namespace templates already have their own source-index pass.
          if (isa<CXXRecordDecl>(Template->getDeclContext())) {
            indexTemplatePackSources(Template);
            if (Template == Template->getCanonicalDecl()) {
              llvm::SmallVector<ClassTemplatePartialSpecializationDecl *, 4> Partials;
              Template->getPartialSpecializations(Partials);
              for (const auto *Partial : Partials)
                for (const auto *Declaration : Partial->redecls()) {
                  A.chargeExpansion(1, Declaration->getLocation());
                  if (const auto *Pattern = dyn_cast<ClassTemplatePartialSpecializationDecl>(Declaration))
                    indexTemplatePackSources(Pattern);
                }
            }
          }
          if (SeenClasses.insert(Template->getCanonicalDecl()).second) {
            if (HiddenInstances)
              QueueClassInstances(Template->getCanonicalDecl());
            else
              Classes.push_back(Template->getCanonicalDecl());
          }
          Queue(Template->getTemplatedDecl(), Depth);
          continue;
        }
        if (const auto *Partial = dyn_cast<ClassTemplatePartialSpecializationDecl>(D))
          indexTemplatePackSources(Partial);
        if (const auto *Full = dyn_cast<CXXRecordDecl>(D);
            classScopeFullIdentity(Full) || ordinaryMemberClassScope(Full)) {
          recordClassOuterPacks(Full);
          const auto *Owner = classBodyPackOwner(Full);
          if (owned(Owner))
            for (const auto *Member : Full->decls()) {
              A.chargeExpansion(1, Member->getLocation());
              if (const auto *Method = dyn_cast<CXXMethodDecl>(Member))
                recordFunctionPacks(Method, Owner);
              else if (const auto *Variable = dyn_cast<VarDecl>(Member);
                       Variable && Variable->isStaticDataMember())
                recordOuterPacks(Variable, Owner);
            }
        }
        const DeclContext *Context = nullptr;
        if (auto *Unit = dyn_cast<TranslationUnitDecl>(D))
          Context = Unit;
        else if (auto *Namespace = dyn_cast<NamespaceDecl>(D))
          Context = Namespace;
        else if (auto *Linkage = dyn_cast<LinkageSpecDecl>(D))
          Context = Linkage;
        else if (auto *Record = dyn_cast<CXXRecordDecl>(D)) {
          if (Record->isLambda())
            continue;
          Context = Record;
        }
        if (!Context)
          continue;
        const auto Begin = Work.size();
        for (auto *Child : Context->decls())
          Queue(Child, Depth);
        std::reverse(Work.begin() + Begin, Work.end());
      }
      if (Phase == 0) {
        HiddenInstances = true;
        for (auto *Template : Classes)
          QueueClassInstances(Template);
      }
    }
    for (auto *Copy : Copies) {
      auto *Origin = Copy;
      std::set<const FunctionTemplateDecl *> Chain;
      while (auto *Next = Origin->getInstantiatedFromMemberTemplate()) {
        A.chargeExpansion(1, Origin->getLocation());
        if (Chain.size() >= 64 || !Chain.insert(Origin).second ||
            !owned(Origin) || !owned(Next) || Origin->isInvalidDecl() ||
            Next->isInvalidDecl()) {
          A.reject(Copy->getLocation(), "member template index chain",
                   "A member primary needs a bounded owned origin chain.");
          return;
        }
        const auto *Method = dyn_cast<CXXMethodDecl>(Origin->getTemplatedDecl());
        const auto *Pattern = dyn_cast<CXXMethodDecl>(Next->getTemplatedDecl());
        const auto *Record = Method ? classBodyRecord(Method->getParent()) : nullptr;
        if (!Method || !Pattern || Method->getKind() != Pattern->getKind() ||
            (Method->getParent()->getCanonicalDecl() != Pattern->getParent()->getCanonicalDecl() &&
             (!Record || Record->getCanonicalDecl() != Pattern->getParent()->getCanonicalDecl()))) {
          A.reject(Copy->getLocation(), "member template index owner",
                   "Each copied member primary must match its selected class pattern.");
          return;
        }
        Origin = Next->getCanonicalDecl();
      }
      auto Found = A.TemplateOrdinals.find(Origin->getCanonicalDecl());
      if (Found == A.TemplateOrdinals.end()) {
        A.reject(Copy->getLocation(), "member template source ordinal",
                 "The written origin must be indexed before any instance identity.");
        return;
      }
      auto Inserted = A.TemplateOrdinals.emplace(Copy->getCanonicalDecl(), Found->second);
      if (!Inserted.second && Inserted.first->second != Found->second) {
        A.reject(Copy->getLocation(), "member template source ordinal",
                 "Redeclarations must preserve the same written origin ordinal.");
        return;
      }
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
      if (auto *Template = dyn_cast<FunctionTemplateDecl>(D);
          Template && owned(Template) &&
          ordinaryFreeFunctionName(Template->getTemplatedDecl()) &&
          Template->getLexicalDeclContext()->getRedeclContext()->isFileContext()) {
        auto *Canonical = Template->getCanonicalDecl();
        IndependentTemplateIdentities.insert(Canonical);
        if (!A.TemplateOrdinals.count(Canonical))
          A.TemplateOrdinals.emplace(Canonical, A.TemplateOrdinals.size());
      }
      if (const auto *Template = dyn_cast<NamedDecl>(D);
          Template && isa<FunctionTemplateDecl, ClassTemplateDecl, TypeAliasTemplateDecl,
                          ClassTemplatePartialSpecializationDecl, VarTemplateDecl,
                          VarTemplatePartialSpecializationDecl>(Template))
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
      if (const auto *Template = dyn_cast<VarTemplateDecl>(D); Template && owned(Template)) {
        llvm::SmallVector<VarTemplatePartialSpecializationDecl *, 4> Partials;
        Template->getPartialSpecializations(Partials);
        for (const auto *Partial : Partials)
          for (const auto *Declaration : Partial->redecls())
            if (const auto *Pattern = dyn_cast<VarTemplatePartialSpecializationDecl>(Declaration))
              indexTemplatePackSources(Pattern);
      }
      // Out-of-line member definitions are separate lexical namespace entries.
      if (const auto *Method = dyn_cast<CXXMethodDecl>(D)) {
        if (const auto *Primary = classBodyPackOwner(Method->getParent()))
          recordFunctionPacks(Method, Primary);
      } else if (const auto *Variable = dyn_cast<VarDecl>(D);
                 Variable && Variable->isStaticDataMember()) {
        const auto *Parent = dyn_cast<CXXRecordDecl>(Variable->getDeclContext());
        if (const auto *Primary = classBodyPackOwner(Parent))
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
    indexMemberTemplates();
    if (A.S.Diagnostics.empty())
      indexFriendClassSources();
    if (A.S.Diagnostics.empty())
      indexFriendTemplateIdentities();
  }
  bool variableWrittenTypeSource(const VarTemplateSpecializationDecl *Variable,
                                 TypeSourceInfo *Info, SourceLocation L) {
    if (!Info || Info->getType().isNull() ||
        !A.S.owns(A.Sources, Info->getTypeLoc().getBeginLoc())) {
      A.reject(L, "variable written type", "The actual declaration must retain its source-owned type.");
      return true;
    }
    auto Type = Info->getType();
    // Qualifiers and pointer/reference declarators surround the written auto
    // token. Inspect that exact token, then traverse the complete written type.
    const auto Auto = Info->getTypeLoc().getContainedAutoTypeLoc();
    const bool AutoToken = Auto && Auto.getTypePtr()->getDeducedType().isNull() &&
                          !Auto.isConstrained();
    if ((!AutoToken && (Type->isDependentType() || Type->isInstantiationDependentType() ||
                       !A.Context.hasSameType(Type, Variable->getType()))) ||
        (AutoToken && !staticStorageType(Variable->getType()))) {
      A.reject(L, "variable written type", "The retained source must match the concrete storage type or its actual deduced auto token.");
      return true;
    }
    return TraverseTypeLoc(Info->getTypeLoc());
  }
  bool variableTypeSource(const VarTemplateSpecializationDecl *Variable,
                           const VariableTypeSource &Source, const NamedDecl *Owner) {
    const auto *Pattern = variablePatternDecl(Owner);
    const auto *Primary = dyn_cast_or_null<VarTemplateDecl>(Owner);
    const auto *Partial = dyn_cast_or_null<VarTemplatePartialSpecializationDecl>(Owner);
    const bool OwnMember = (Primary && Primary->isMemberSpecialization()) ||
                           (Partial && Partial->isMemberSpecialization());
    auto *Info = Source.Type;
    if (Source.Variable != Variable || !Pattern || !Info ||
        !variablePatternOrigin(Owner, Source.Pattern) ||
        !variablePreviousIdentity(Variable, Source) ||
        ((Source.Completion || Source.Previous) && !Source.Pattern->isThisDeclarationADefinition()) ||
        (!Source.Completion && !Source.Previous && !OwnMember &&
         Source.Pattern != Pattern->getFirstDecl()) ||
        (Source.Completion && Source.Previous) ||
        Info->getType().isNull() || !A.S.owns(A.Sources, Info->getTypeLoc().getBeginLoc())) {
      A.reject(Source.Location, "variable type source", "Each type substitution must belong to this variable's actual pattern declaration.");
      return true;
    }
    return variableWrittenTypeSource(Variable, Info, Source.Location);
  }
  bool traverseVariableTemplateParameterSource(const NamedDecl *Owner) {
    const auto *Pattern = variablePatternDecl(Owner);
    const auto *Parent = Pattern ? dyn_cast<CXXRecordDecl>(Pattern->getDeclContext()) : nullptr;
    const bool DependentClass = Parent && Parent->isDependentContext();
    if (!traverseTemplateParameterSource(templateSourceParameters(Owner), DependentClass))
      return false;
    for (unsigned I = 0; I < Pattern->getNumTemplateParameterLists(); ++I) {
      const auto *Parameters = Pattern->getTemplateParameterList(I);
      if (Parameters->size() && !traverseTemplateParameterSource(Parameters))
        return false;
    }
    // A concrete qualifier remains a declaration use even without an instance.
    return !Pattern->getQualifier() || Pattern->getQualifier()->isDependent() ||
           TraverseNestedNameSpecifierLoc(Pattern->getQualifierLoc());
  }
  bool TraverseVarTemplateDecl(VarTemplateDecl *D) {
    if (!A.S.coreV2() || !owned(D))
      return RecursiveASTVisitor<Allowlist>::TraverseVarTemplateDecl(D);
    if (!checkClassFullOwnerSources(D->getDeclContext()))
      return false;
    if (!A.S.Diagnostics.empty())
      return true;
    if (!WalkUpFromVarTemplateDecl(D))
      return false;
    if (!variableTemplateShape(D)) {
      A.reject(D->getLocation(), "variable template", "An owned namespace or member scalar template with bounded supported parameters is required.");
      return true;
    }
    if (!traverseVariableTemplateParameterSource(D))
      return false;
    // Generic initializer syntax retains C++ template instantiation laziness.
    if (D->getDeclContext()->isDependentContext() || D != D->getCanonicalDecl())
      return true;
    llvm::SmallVector<VarTemplatePartialSpecializationDecl *, 4> Partials;
    D->getPartialSpecializations(Partials);
    for (auto *Partial : Partials)
      for (auto *Declaration : Partial->redecls()) {
        A.chargeExpansion(1, Declaration->getLocation());
        if (auto *Pattern = dyn_cast<VarTemplatePartialSpecializationDecl>(Declaration))
          if (!TraverseVarTemplatePartialSpecializationDecl(Pattern))
            return false;
      }
    for (auto *Instance : D->specializations())
      for (auto *Declaration : Instance->redecls()) {
        A.chargeExpansion(1, Declaration->getLocation());
        if (auto *Variable = dyn_cast<VarTemplateSpecializationDecl>(Declaration))
          if (!TraverseDecl(Variable))
            return false;
      }
    return true;
  }
  bool TraverseVarTemplatePartialSpecializationDecl(VarTemplatePartialSpecializationDecl *D) {
    if (!A.S.coreV2() || !owned(D))
      return RecursiveASTVisitor<Allowlist>::TraverseVarTemplatePartialSpecializationDecl(D);
    if (!checkClassFullOwnerSources(D->getDeclContext()))
      return false;
    if (!A.S.Diagnostics.empty())
      return true;
    if (!VisitDecl(D))
      return false;
    if (!variablePartialShape(D)) {
      A.reject(D->getLocation(), "variable partial pattern", "An owned namespace or member scalar partial with supported parameters and written arguments is required.");
      return true;
    }
    if (!traverseVariableTemplateParameterSource(D))
      return false;
    return checkPartialDeclarationSource(D);
  }
  bool TraverseVarTemplateSpecializationDecl(VarTemplateSpecializationDecl *D) {
    if (!A.S.coreV2() || !owned(D))
      return RecursiveASTVisitor<Allowlist>::TraverseVarTemplateSpecializationDecl(D);
    if (CheckedVariableDeclarations.count(D))
      return true;
    auto L = D->getLocation();
    if (D->getDeclContext()->isDependentContext())
      return traverseGenericMemberVariableFull(D);
    if (ActiveVariableDeclarations.size() >= 64) {
      A.reject(L, "variable definition depth", "Nested concrete variable source exceeds its depth limit.");
      return true;
    }
    if (std::find(ActiveVariableDeclarations.begin(), ActiveVariableDeclarations.end(), D) != ActiveVariableDeclarations.end())
      return true; // A type-only reference can refer to the definition being checked.
    ActiveVariableDeclarations.push_back(D);
    auto RestoreActive = llvm::make_scope_exit([&] { ActiveVariableDeclarations.pop_back(); });
    const auto *Previous = variablePreviousDeclaration(D);
    if (!A.S.Diagnostics.empty())
      return true;
    if (Previous && !TraverseVarTemplateSpecializationDecl(
                        const_cast<VarTemplateSpecializationDecl *>(Previous)))
      return false;
    if (!A.S.Diagnostics.empty())
      return true;
    const auto *PrimarySource = variablePrimarySource(D, L);
    if (!PrimarySource)
      return true;
    // Explicit declarations carry independent source even if Sema reused a node.
    auto Declarations = VariableDeclarations.find(D);
    if (D->isExplicitSpecialization() &&
        (Declarations == VariableDeclarations.end() || Declarations->second.empty())) {
      A.reject(L, "variable specialization source", "Every full specialization requires its concrete declaration source.");
      return true;
    }
    if (Declarations != VariableDeclarations.end())
      for (const auto *Source : Declarations->second) {
        if (!variableSourceIdentity(D, *Source))
          return true;
        if (!checkTemplateUse(*Source, Source->Written->arguments()))
          return false;
      }
    if (!A.S.Diagnostics.empty())
      return true;
    const NamedDecl *Owner = nullptr;
    const TemplateArgumentList *Arguments = nullptr;
    if (!D->isExplicitSpecialization()) {
      Owner = variableTemplatePattern(D);
      Arguments = PrimarySource->Canonical;
      if (isa_and_nonnull<VarTemplatePartialSpecializationDecl>(Owner)) {
        const auto *Deduction = variablePartialSource(D, L);
        if (!Deduction)
          return true;
        Arguments = Deduction->Canonical;
      }
      if (!variablePatternShape(Owner)) {
        A.reject(L, "variable initializer owner", "The concrete initializer needs its actual admitted primary or partial pattern.");
        return true;
      }
    }
    if (DefinitionFrames.size() >= 64) {
      A.reject(L, "definition source depth", "Nested definition source exceeds its depth limit.");
      return true;
    }
    auto *SavedFunction = CurrentFunction;
    auto *SavedMethod = CurrentMethod;
    auto *SavedField = CurrentDefaultField;
    auto SavedInitializer = ImplicitInitializerOwner;
    CurrentFunction = nullptr;
    CurrentMethod = nullptr;
    CurrentDefaultField = nullptr;
    ImplicitInitializerOwner = L;
    // A full specialization has no generic slots, but still fences caller scope.
    DefinitionFrames.push_back({D, Owner, Arguments, TemplateFrames.size()});
    auto Restore = llvm::make_scope_exit([&] {
      DefinitionFrames.pop_back();
      CurrentFunction = SavedFunction;
      CurrentMethod = SavedMethod;
      CurrentDefaultField = SavedField;
      ImplicitInitializerOwner = SavedInitializer;
    });
    if (D->isExplicitSpecialization()) {
      for (const auto *Source : Declarations->second) {
        if (!variableWrittenTypeSource(D, Source->Underlying, Source->Location))
          return false;
      }
      if (auto Found = VariableTypeSources.find(D); Found != VariableTypeSources.end())
        for (const auto *Source : Found->second) {
          if (!copiedMemberVariableFullOrigin(D, *Source))
            return true;
          if (!variableWrittenTypeSource(D, Source->Type, Source->Location))
            return false;
        }
    } else {
      auto Found = VariableTypeSources.find(D);
      if (Found == VariableTypeSources.end() || Found->second.empty()) {
        A.reject(L, "variable instantiation type source", "The actual type substitutions must be retained.");
        return true;
      }
      bool HasFirstType = false;
      for (const auto *Source : Found->second) {
        A.chargeExpansion(1, Source->Location);
        HasFirstType |= !Source->Completion;
        if (!variableTypeSource(D, *Source, Owner))
          return false;
      }
      if (!HasFirstType) {
        A.reject(L, "variable first type source", "The first declaration's actual type source must also be checked.");
        return true;
      }
    }
    if (!A.S.Diagnostics.empty())
      return true;
    // Retained type events above include later definitions; the usual VarDecl
    // traversal still checks the stored type and actual materialized initializer.
    if (!RecursiveASTVisitor<Allowlist>::TraverseVarDecl(D))
      return false;
    if (A.S.Diagnostics.empty())
      CheckedVariableDeclarations.insert(D);
    return true;
  }
  bool TraverseTypeAliasTemplateDecl(TypeAliasTemplateDecl *D) {
    if (!A.S.coreV2() || !owned(D))
      return RecursiveASTVisitor<Allowlist>::TraverseTypeAliasTemplateDecl(D);
    if (!checkClassFullOwnerSources(D->getDeclContext()))
      return false;
    if (!A.S.Diagnostics.empty())
      return true;
    if (!WalkUpFromTypeAliasTemplateDecl(D))
      return false;
    if (!aliasTemplateShape(D)) {
      A.reject(D->getLocation(), "alias template", "An owned namespace or admitted member alias with supported type or scalar parameters is required.");
      return true;
    }
    const auto *Parent = dyn_cast<CXXRecordDecl>(D->getDeclContext());
    // A member alias's own parameter type can depend on an earlier inner
    // parameter even when its class is ordinary. Every selected alias use
    // checks the retained substituted parameter type in checkTemplateUse.
    if (!traverseTemplateParameterSource(D->getTemplateParameters(), Parent != nullptr))
      return false;
    auto *Pattern = D->getTemplatedDecl();
    auto Type = Pattern->getUnderlyingType();
    if (Type.isNull() || Type->isDependentType() || Type->isInstantiationDependentType())
      return true;
    // Even an unused nondependent alias retains ordinary source checks. An
    // erased canonical type such as Ignore<T> is still instantiation-dependent.
    return TraverseDecl(Pattern);
  }
  bool qualifierTemplateMatches(TemplateSpecializationTypeLoc TL,
                                const ClassTemplateSpecializationDecl *Record) {
    if (!Record || Record->getKind() != Decl::ClassTemplateSpecialization ||
        !owned(Record) || Record->isDependentContext() || Record->isInvalidDecl())
      return false;
    const auto *Actual = Record->getSpecializedTemplate();
    const auto *Written = dyn_cast_or_null<ClassTemplateDecl>(
        TL.getTypePtr()->getTemplateName().getAsTemplateDecl());
    if (!Written || !owned(Written) || !classTemplateShape(Actual) ||
        Actual->getDeclName() != Written->getDeclName())
      return false;
    // Sema can retain the original member primary in an otherwise substituted
    // qualifier. Only the actual selected primary's verified origin chain fits.
    std::set<const ClassTemplateDecl *> Seen;
    bool Matched = false;
    for (auto *Primary = Actual; Primary;) {
      A.chargeExpansion(1, TL.getBeginLoc());
      Primary = Primary->getCanonicalDecl();
      if (Seen.size() >= 64 || !Seen.insert(Primary).second)
        return false;
      if (Primary == Written->getCanonicalDecl()) {
        Matched = true;
        break;
      }
      if (Primary->isMemberSpecialization())
        break;
      Primary = Primary->getInstantiatedFromMemberTemplate();
    }
    if (!Matched || !checkClassPartialSource(Record, TL.getBeginLoc()) ||
        !A.S.Diagnostics.empty())
      return false;
    std::vector<const TemplateArgument *> Arguments;
    for (const auto &Argument : Record->getTemplateArgs().asArray()) {
      if (Argument.getKind() == TemplateArgument::Pack) {
        if (Argument.pack_size() > 64)
          return false;
        for (const auto &Element : Argument.pack_elements())
          Arguments.push_back(&Element);
      } else {
        Arguments.push_back(&Argument);
      }
      if (Arguments.size() > 64)
        return false;
    }
    if (Arguments.size() != TL.getNumArgs())
      return false;
    for (unsigned I = 0; I < TL.getNumArgs(); ++I) {
      A.chargeExpansion(1, TL.getArgLoc(I).getLocation());
      if (!sourceMatchesArgument(TL.getArgLoc(I), *Arguments[I]))
        return false;
    }
    return true;
  }
  bool concreteDeclarationQualifier(TemplateSpecializationTypeLoc TL) {
    if (!CurrentDeclarator || !owned(CurrentDeclarator) ||
        CurrentDeclarator->getDeclContext()->isDependentContext())
      return false;
    const auto *Method = dyn_cast<CXXMethodDecl>(CurrentDeclarator);
    const auto *Variable = dyn_cast<VarDecl>(CurrentDeclarator);
    if (!(Method && concreteMemberFunction(Method)) &&
        !(Variable && classStaticDataPattern(Variable)))
      return false;
    auto Qualifier = CurrentDeclarator->getQualifierLoc();
    bool Found = false;
    unsigned Components = 0;
    for (auto Q = Qualifier; Q; Q = Q.getPrefix()) {
      if (++Components > 64)
        return false;
      auto Type = Q.getTypeLoc();
      Found |= Type && Type.getType() == TL.getType() &&
               Type.getOpaqueData() == TL.getOpaqueData();
    }
    if (!Found)
      return false; // No parameter, body or unrelated type can borrow this scope.
    const auto *Context = CurrentDeclarator->getDeclContext();
    for (auto Q = Qualifier; Q; Q = Q.getPrefix()) {
      auto Type = Q.getTypeLoc();
      if (!Type)
        continue; // Namespace/global prefixes retain normal source traversal.
      const auto *Record = dyn_cast<CXXRecordDecl>(Context);
      if (!Record || !owned(Record) || Record->isDependentContext())
        return false;
      A.chargeExpansion(1, Type.getBeginLoc());
      if (Type.getType()->isDependentType() ||
          Type.getType()->isInstantiationDependentType()) {
        auto Template = Type.getAs<TemplateSpecializationTypeLoc>();
        if (!Template || !qualifierTemplateMatches(
                Template, dyn_cast<ClassTemplateSpecializationDecl>(Record)))
          return false;
      } else if (!A.Context.hasSameType(Type.getType(),
                                        A.Context.getTypeDeclType(Record))) {
        return false;
      }
      Context = Record->getDeclContext();
    }
    return true;
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
      // Clang 20 retains dependent member-primary sugar in instantiated
      // out-of-line qualifiers even when the selected owner/arguments are real.
      if (concreteDeclarationQualifier(TL)) {
        if (!WalkUpFromTemplateSpecializationTypeLoc(TL) ||
            (shouldWalkTypesOfTypeLocs() &&
             !WalkUpFromTemplateSpecializationType(const_cast<TemplateSpecializationType *>(Type))))
          return false;
        for (unsigned I = 0; I < TL.getNumArgs(); ++I)
          if (!TraverseTemplateArgumentLoc(TL.getArgLoc(I)))
            return false;
        return true;
      }
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
    const auto Depth = templateSourceParameterDepth(Primary);
    if (!Depth || !owned(Type->getAssociatedDecl()) || !templateSourceShape(Primary) ||
        Type->getIndex() >= templateSourceParameters(Primary)->size()) {
      A.reject(L, "template type replacement", "A type substitution requires its exact admitted parameter owner.");
      return true;
    }
    const auto *Parameter = dyn_cast<TemplateTypeParmDecl>(
        templateSourceParameters(Primary)->getParam(Type->getIndex()));
    if (!Parameter || Parameter->getDepth() != *Depth ||
        !owned(Parameter) ||
        Parameter->isParameterPack() != Type->getPackIndex().has_value() ||
        (Type->getPackIndex() && *Type->getPackIndex() >= 64) ||
        Type->getReplacementType().isNull() ||
        Type->getReplacementType()->isInstantiationDependentType()) {
      A.reject(L, "template type parameter", "The resolved type must match a guarded type parameter and pack position.");
      return true;
    }
    if (isa<TypeAliasTemplateDecl, ClassTemplatePartialSpecializationDecl,
                         VarTemplateDecl, VarTemplatePartialSpecializationDecl>(Primary) ||
        isa<ClassTemplateSpecializationDecl>(Type->getAssociatedDecl()) ||
        concreteFunctionTemplate(dyn_cast<FunctionDecl>(Type->getAssociatedDecl())) ||
        hasSourceFrame(Primary)) {
      const auto *Argument = concreteSourceEdge(
          Type->getAssociatedDecl(), Primary, Type->getIndex(), Type->getPackIndex(), L);
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
  bool TraverseUnresolvedLookupExpr(UnresolvedLookupExpr *Lookup) {
    auto Found = InitializerLookups.find(Lookup);
    if (!A.S.coreV2() || Found == InitializerLookups.end())
      return RecursiveASTVisitor<Allowlist>::TraverseUnresolvedLookupExpr(Lookup);
    // The selected semantic clause was traversed before the written list.
    // Its declaration, qualifier and complete template source remain checked;
    // the stale overload pseudo-type is not a second runtime expression.
    return true;
  }
  bool TraverseDeclRefExpr(DeclRefExpr *Reference) {
    if (!A.S.coreV2() || !A.S.owns(A.Sources, Reference->getLocation()) ||
        (!isa<VarTemplateSpecializationDecl>(Reference->getDecl()) &&
         !concreteFunctionTemplate(dyn_cast<FunctionDecl>(Reference->getDecl()))))
      return RecursiveASTVisitor<Allowlist>::TraverseDeclRefExpr(Reference);
    // Check source before WalkUp's callback-definition validation. An
    // unevaluated address may lack a body, but its written arguments still
    // require inspection before that diagnostic suppresses further traversal.
    if (const auto *Function = dyn_cast<FunctionDecl>(Reference->getDecl());
        concreteFunctionTemplate(Function))
      checkFunctionTemplateUse(Function, Reference->getLocation(), Reference->template_arguments(),
          Reference->hasQualifier() ? Reference->getBeginLoc() : SourceLocation(),
          directTemplateCallLocation(Reference));
    // Each source event owns argument traversal before the callee's frame.
    // RAV's normal argument traversal would visit nested template uses again
    // at every level, making a linear source chain expand exponentially.
    // DeclRefExpr has no statement children; retain its qualifier/name visits.
    return WalkUpFromDeclRefExpr(Reference) &&
           TraverseNestedNameSpecifierLoc(Reference->getQualifierLoc()) &&
           TraverseDeclarationNameInfo(Reference->getNameInfo());
  }
  bool VisitDeclRefExpr(DeclRefExpr *Reference) {
    if (!A.S.coreV2() || !A.S.owns(A.Sources, Reference->getLocation()))
      return true;
    if (auto *Variable = dyn_cast<VarTemplateSpecializationDecl>(Reference->getDecl())) {
      if (!checkVariableTemplateUse(Variable, Reference->getLocation(), Reference->template_arguments()))
        return false;
      if (!A.S.Diagnostics.empty())
        return true;
      return TraverseVarTemplateSpecializationDecl(Variable);
    }
    if (auto *Function = dyn_cast<FunctionDecl>(Reference->getDecl());
        Function && FriendCanonicalFunctions.count(Function->getCanonicalDecl())) {
      if (FriendFunctionSources.count(Function) && !TraverseDecl(Function))
        return false;
      if (auto *Definition = Function->getDefinition(); Definition && Definition != CurrentFunction &&
          !CheckedFriendFunctions.count(Definition))
        if (!TraverseDecl(Definition))
          return false;
    }
    return true;
  }
  bool TraverseMemberExpr(MemberExpr *Reference) {
    if (!A.S.coreV2() || Reference->getMemberLoc().isInvalid() ||
        !A.S.owns(A.Sources, Reference->getMemberLoc()) ||
        (!isa<VarTemplateSpecializationDecl>(Reference->getMemberDecl()) &&
         !concreteMemberFunctionTemplate(dyn_cast<FunctionDecl>(Reference->getMemberDecl()))))
      return RecursiveASTVisitor<Allowlist>::TraverseMemberExpr(Reference);
    // The selected source check owns explicit arguments here as well. Keep
    // the receiver traversal: its type, source and effects still matter.
    if (const auto *Function = dyn_cast<FunctionDecl>(Reference->getMemberDecl());
        concreteMemberFunctionTemplate(Function))
      checkFunctionTemplateUse(Function, Reference->getMemberLoc(),
                               Reference->template_arguments(), {},
                               directTemplateCallLocation(Reference));
    return WalkUpFromMemberExpr(Reference) &&
           TraverseNestedNameSpecifierLoc(Reference->getQualifierLoc()) &&
           TraverseDeclarationNameInfo(Reference->getMemberNameInfo()) &&
           TraverseStmt(Reference->getBase());
  }
  bool VisitMemberExpr(MemberExpr *Reference) {
    if (!A.S.coreV2() || Reference->getMemberLoc().isInvalid() ||
        !A.S.owns(A.Sources, Reference->getMemberLoc()))
      return true;
    if (auto *Variable = dyn_cast<VarTemplateSpecializationDecl>(Reference->getMemberDecl())) {
      if (!checkVariableTemplateUse(Variable, Reference->getMemberLoc(), Reference->template_arguments()))
        return false;
      return !A.S.Diagnostics.empty() || TraverseVarTemplateSpecializationDecl(Variable);
    }
    return true;
  }
  bool lazyFreeFunctionSignature(const FunctionDecl *Function) {
    if (!A.S.coreV2() || !concreteFreeFunctionTemplate(Function) ||
        !owned(Function) || Function->hasBody() ||
        Function->isUsed(/*CheckUsedAttr=*/false) || Function->isDeleted() ||
        Function->getTemplateSpecializationKind() != TSK_ImplicitInstantiation ||
        !Function->getTypeSourceInfo())
      return false;
    const auto *Primary = Function->getPrimaryTemplate();
    const auto *Pattern = Primary ? Primary->getTemplatedDecl() : nullptr;
    const auto *Definition = Pattern ? Pattern->getDefinition() : nullptr;
    return functionTemplateShape(Primary) && owned(Definition) &&
           !Function->getType()->isInstantiationDependentType() &&
           standardExceptionSpecification(Function->getType()->getAs<FunctionProtoType>());
  }
  bool TraverseFunctionTemplateDecl(FunctionTemplateDecl *D) {
    if (!A.S.coreV2() || !owned(D))
      return RecursiveASTVisitor<Allowlist>::TraverseFunctionTemplateDecl(D);
    if (!checkClassFullOwnerSources(D->getDeclContext()))
      return false;
    if (!A.S.Diagnostics.empty())
      return true;
    if (!WalkUpFromFunctionTemplateDecl(D))
      return false;
    if (!functionTemplateShape(D)) {
      A.reject(D->getLocation(), "function template",
               "An owned namespace or admitted member function template with bounded supported parameters is required.");
      return true;
    }
    A.chargeExpansion(1 + D->getTemplateParameters()->size(), D->getLocation());
    const auto *Method = dyn_cast<CXXMethodDecl>(D->getTemplatedDecl());
    const auto *FriendClass = !Method ? dyn_cast<CXXRecordDecl>(D->getLexicalDeclContext()) : nullptr;
    const bool DependentClass = Method ? Method->getParent()->isDependentContext()
                                      : FriendClass && FriendClass->isDependentContext();
    // Generic outer class substitutions are deferred to the actual copied
    // member primary. Nondependent written defaults remain checked here.
    if (!traverseTemplateParameterSource(D->getTemplateParameters(), DependentClass))
      return false;
    if (Method)
      for (unsigned I = 0; I < Method->getNumTemplateParameterLists(); ++I) {
        const auto *Outer = Method->getTemplateParameterList(I);
        if (Outer->size() && !traverseTemplateParameterSource(Outer))
          return false;
      }
    // A concrete outer qualifier is a declaration use even when this member
    // template has no instantiated body. RAV normally visits it on FunctionDecl,
    // whose generic traversal is deliberately skipped here for body laziness.
    if (Method && Method->getQualifier() && !Method->getQualifier()->isDependent() &&
        !TraverseNestedNameSpecifierLoc(Method->getQualifierLoc()))
      return false;
    if (DependentClass)
      return true;
    // Sema has finished. Inspect materialized definitions, not dependent
    // patterns or unused overload candidates that have only a signature.
    if (D != D->getCanonicalDecl())
      return true;
    for (auto *Specialization : D->specializations()) {
      for (auto *Declaration : Specialization->redecls()) {
        A.chargeExpansion(1, Declaration->getLocation());
        auto Kind = Declaration->getTemplateSpecializationKind();
        if (!Declaration->hasBody() &&
            (Kind == TSK_Undeclared || Kind == TSK_ImplicitInstantiation) &&
            !(Declaration->isReferenced() && lazyFreeFunctionSignature(Declaration)))
          continue;
        if (!TraverseDecl(Declaration))
          return false;
      }
    }
    return true;
  }
  bool traverseClassMemberTemplateSources(const CXXRecordDecl *Record) {
    const auto *Definition = Record ? Record->getDefinition() : nullptr;
    if (!owned(Definition))
      return true;
    // Custom generic-class traversal skips the record itself. Inspect written
    // nondependent bases now; concrete RAV checks substituted dependent bases.
    for (const auto &Base : Definition->bases())
      if (!Base.getType()->isDependentType() && !TraverseCXXBaseSpecifier(Base))
        return false;
    for (auto *Member : Definition->decls()) {
      A.chargeExpansion(1, Member->getLocation());
      if (auto *Friend = dyn_cast<FriendDecl>(Member)) {
        if (!TraverseFriendDecl(Friend))
          return false;
      } else if (auto *Template = dyn_cast<FunctionTemplateDecl>(Member)) {
        if (!TraverseFunctionTemplateDecl(Template))
          return false;
      } else if (auto *Class = dyn_cast<ClassTemplateDecl>(Member)) {
        if (!TraverseClassTemplateDecl(Class))
          return false;
      } else if (auto *Partial = dyn_cast<ClassTemplatePartialSpecializationDecl>(Member)) {
        if (!TraverseClassTemplatePartialSpecializationDecl(Partial))
          return false;
      } else if (auto *Full = dyn_cast<ClassTemplateSpecializationDecl>(Member)) {
        if (!TraverseDecl(Full))
          return false;
      } else if (auto *Nested = dyn_cast<CXXRecordDecl>(Member);
                 ordinaryMemberClassScope(Nested)) {
        if (!TraverseDecl(Nested))
          return false;
      } else if (auto *Alias = dyn_cast<TypeAliasTemplateDecl>(Member)) {
        if (!TraverseTypeAliasTemplateDecl(Alias))
          return false;
      } else if (auto *Variable = dyn_cast<VarTemplateDecl>(Member)) {
        if (!TraverseVarTemplateDecl(Variable))
          return false;
      } else if (auto *Partial = dyn_cast<VarTemplatePartialSpecializationDecl>(Member)) {
        if (!TraverseVarTemplatePartialSpecializationDecl(Partial))
          return false;
      } else if (auto *Full = dyn_cast<VarTemplateSpecializationDecl>(Member)) {
        if (!TraverseVarTemplateSpecializationDecl(Full))
          return false;
      }
    }
    return true;
  }
  bool traverseClassTemplateParameterSource(const NamedDecl *Owner,
                                            const CXXRecordDecl *Pattern) {
    if (!traverseTemplateParameterSource(templateSourceParameters(Owner),
                                        Owner->getDeclContext()->isDependentContext()))
      return false;
    for (unsigned I = 0; I < Pattern->getNumTemplateParameterLists(); ++I) {
      const auto *Parameters = Pattern->getTemplateParameterList(I);
      if (Parameters->size() && !traverseTemplateParameterSource(Parameters, true))
        return false;
    }
    return !Pattern->getQualifier() || Pattern->getQualifier()->isDependent() ||
           TraverseNestedNameSpecifierLoc(Pattern->getQualifierLoc());
  }
  bool TraverseClassTemplateDecl(ClassTemplateDecl *D) {
    if (!A.S.coreV2() || !owned(D))
      return RecursiveASTVisitor<Allowlist>::TraverseClassTemplateDecl(D);
    if (D->getFriendObjectKind()) {
      const FriendDecl *Friend = nullptr;
      if (auto Copy = FriendClassDeclarations.find(D); Copy != FriendClassDeclarations.end())
        Friend = Copy->second;
      else if (auto Written = WrittenFriendClasses.find(D); Written != WrittenFriendClasses.end())
        Friend = Written->second;
      if (!Friend) {
        A.reject(D->getLocation(), "class friend declaration source",
                 "The actual target header must retain its exact friend declaration.");
        return true;
      }
      if (!checkClassFullOwnerSources(Friend->getDeclContext()) ||
          !traverseFriendClassTemplateSource(Friend))
        return false;
    }
    if (!checkClassFullOwnerSources(D->getDeclContext()))
      return false;
    if (!A.S.Diagnostics.empty())
      return true;
    if (CheckedClassTemplateSources.count(D))
      return true;
    if (ActiveClassTemplateSources.size() >= 64 || !ActiveClassTemplateSources.insert(D).second) {
      A.reject(D->getLocation(), "member class source depth",
               "Class template declaration sources require a bounded acyclic traversal.");
      return true;
    }
    auto RestoreSource = llvm::make_scope_exit([&] { ActiveClassTemplateSources.erase(D); });
    if (!WalkUpFromClassTemplateDecl(D))
      return false;
    if (!classTemplateShape(D)) {
      A.reject(D->getLocation(), "class template",
               "Only owned class templates in admitted namespace or class scopes with checked parameters, origins and members are admitted.");
      return true;
    }
    A.chargeExpansion(1 + D->getTemplateParameters()->size(), D->getLocation());
    const auto *Pattern = D->getTemplatedDecl();
    if ((!D->getFriendObjectKind() && !traverseClassTemplateParameterSource(D, Pattern)) ||
        !traverseClassMemberTemplateSources(D->getTemplatedDecl()))
      return false;
    if (D != D->getCanonicalDecl()) {
      if (A.S.Diagnostics.empty())
        CheckedClassTemplateSources.insert(D);
      return true;
    }
    // Sema keeps copied partials in a hidden list, even when their class body
    // has not been instantiated. Resolved declaration source is still checked.
    llvm::SmallVector<ClassTemplatePartialSpecializationDecl *, 4> Partials;
    D->getPartialSpecializations(Partials);
    for (auto *Partial : Partials)
      for (auto *Declaration : Partial->redecls()) {
        A.chargeExpansion(1, Declaration->getLocation());
        if (!TraverseClassTemplatePartialSpecializationDecl(
                cast<ClassTemplatePartialSpecializationDecl>(Declaration)))
          return false;
      }
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
    if (A.S.Diagnostics.empty())
      CheckedClassTemplateSources.insert(D);
    return true;
  }
  bool TraverseCXXRecordDecl(CXXRecordDecl *D) {
    if (!A.S.coreV2() || !owned(D) || !ordinaryMemberClassScope(D))
      return RecursiveASTVisitor<Allowlist>::TraverseCXXRecordDecl(D);
    if (!TraversedOrdinaryClasses.insert(D).second)
      return true;
    if (!ordinaryClassBodyShape(D)) {
      A.reject(D->getLocation(), "ordinary member class body",
               "A named ordinary nested record needs a checked source and actual body owner.");
      return true;
    }
    if (!checkOrdinaryClassSource(D) || !VisitDecl(D))
      return false;
    if (!A.S.Diagnostics.empty())
      return true;
    if (D->isDependentContext())
      return traverseClassMemberTemplateSources(D);
    // VisitCXXRecordDecl handles real definitions. An unused ordinary member
    // copy is only a declaration; its generic source is never a runtime body.
    if (!D->getDefinition())
      return true;
    return RecursiveASTVisitor<Allowlist>::TraverseCXXRecordDecl(D);
  }
  bool TraverseClassTemplateSpecializationDecl(ClassTemplateSpecializationDecl *D) {
    if (!A.S.coreV2() || !owned(D))
      return RecursiveASTVisitor<Allowlist>::TraverseClassTemplateSpecializationDecl(D);
    const bool FullSource = genericClassFullIdentity(D) || D->getInstantiatedFromMemberClass();
    if (FullSource) {
      if (!classFullBodyShape(D)) {
        A.reject(D->getLocation(), "class-scope full body",
                 "An owned full body with its exact original and actual outer owner is required.");
        return true;
      }
      if (!checkClassFullDeclarationSource(D) || !VisitDecl(D))
        return false;
      if (!A.S.Diagnostics.empty())
        return true;
      if (D->getQualifier() && !D->getQualifier()->isDependent() &&
          !TraverseNestedNameSpecifierLoc(D->getQualifierLoc()))
        return false;
      if (genericClassFullIdentity(D))
        return traverseClassMemberTemplateSources(D);
    }
    if (!FullSource && D->isExplicitSpecialization() && isa<CXXRecordDecl>(D->getDeclContext())) {
      const auto *Written = D->getTemplateArgsAsWritten();
      auto Found = ClassSources.find(D);
      if (!memberClassDeclarationShape(D, true) || D->isDependentContext() ||
          !classTemplateShape(D->getSpecializedTemplate()) || !Written ||
          Found == ClassSources.end() || Found->second.empty()) {
        A.reject(D->getLocation(), "member class full declaration",
                 "A full member class needs an admitted concrete outer scope and its exact written declaration source.");
        return true;
      }
      for (const auto *Source : Found->second) {
        A.chargeExpansion(1 + Written->getNumTemplateArgs(), D->getLocation());
        if (Source->Kind != TemplateSourceKind::ClassDeclaration || Source->Declaration != D ||
            Source->Instantiation || Source->Location != D->getLocation() || !Source->Written ||
            Source->Written->getLAngleLoc() != Written->getLAngleLoc() ||
            Source->Written->getRAngleLoc() != Written->getRAngleLoc() ||
            Source->Written->getNumTemplateArgs() != Written->getNumTemplateArgs()) {
          A.reject(D->getLocation(), "member class full source identity",
                   "Each full declaration must retain its actual written argument list.");
          return true;
        }
        for (unsigned I = 0; I < Written->getNumTemplateArgs(); ++I)
          if (!sameArgumentSource(Source->Written->arguments()[I], Written->arguments()[I])) {
            A.reject(D->getLocation(), "member class full argument identity",
                     "Full specialization source must match each exact written argument.");
            return true;
          }
      }
    }
    if (auto Found = ClassSources.find(D); Found != ClassSources.end())
      for (const auto *Source : Found->second)
        if (!checkTemplateUse(*Source, Source->Written->arguments()))
          return false;
    if (const auto *Written = D->getTemplateArgsAsWritten(); Written && !FullSource)
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
    if (!checkClassFullOwnerSources(D->getDeclContext()))
      return false;
    if (!A.S.Diagnostics.empty())
      return true;
    if (CheckedClassTemplateSources.count(D))
      return true;
    if (ActiveClassTemplateSources.size() >= 64 || !ActiveClassTemplateSources.insert(D).second) {
      A.reject(D->getLocation(), "member class source depth",
               "Class template declaration sources require a bounded acyclic traversal.");
      return true;
    }
    auto RestoreSource = llvm::make_scope_exit([&] { ActiveClassTemplateSources.erase(D); });
    if (!VisitDecl(D))
      return false;
    if (!classPartialShape(D)) {
      A.reject(D->getLocation(), "partial specialization", "An owned class partial with admitted outer owners, parameters, origins and members is required.");
      return true;
    }
    A.chargeExpansion(1 + D->getTemplateParameters()->size(), D->getLocation());
    if (!traverseClassTemplateParameterSource(D, D) ||
        !traverseClassMemberTemplateSources(D))
      return false;
    if (!checkPartialDeclarationSource(D))
      return false;
    if (A.S.Diagnostics.empty())
      CheckedClassTemplateSources.insert(D);
    return true;
  }
  bool TraverseTemplateArgumentLoc(const TemplateArgumentLoc &Argument) {
    if (A.S.coreV2() && Argument.getArgument().getKind() == TemplateArgument::NullPtr) {
      auto *Written = Argument.getSourceNullPtrExpression();
      if (!Written || scalarTemplateArgumentType(Argument.getArgument()).isNull()) {
        auto Location = !ActiveTemplateUses.empty() ? ActiveTemplateUses.back()->Location
            : CurrentFunction ? CurrentFunction->getLocation()
            : A.Sources.getLocForStartOfFile(A.Sources.getMainFileID());
        A.reject(Location, "nullptr template argument source",
                 "A nullptr_t argument requires its retained source expression.");
        return true;
      }
      // Pinned RAV skips NullPtr arguments entirely, including folded syntax.
      return TraverseStmt(Written);
    }
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
         !concreteMemberFunction(CurrentFunction) && !concreteFriendFunction(CurrentFunction)))
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
    if (A.S.coreV2() && owned(D))
      if (auto *Function = dyn_cast<FunctionDecl>(D)) {
        if (!checkFriendTemplateFunction(Function))
          return false;
        if (!A.S.Diagnostics.empty())
          return true;
        auto Source = FriendFunctionSources.find(Function);
        if (Source != FriendFunctionSources.end()) {
          auto Declaration = FriendFunctionDeclarations.find(Function);
          if (Declaration == FriendFunctionDeclarations.end() ||
              !friendSourceIdentity(Declaration->second)) {
            A.reject(Function->getLocation(), "friend function source",
                     "A concrete friend function needs its exact checked source events.");
            return true;
          }
          if (!CheckedFriendFunctions.insert(Function).second)
            return true;
          const auto *Written = FriendDeclarationSources.find(Declaration->second)->second->Written;
          if (!traverseWrittenFriendSignature(Written))
            return false;
          if (!A.S.Diagnostics.empty())
            return true;
          if (!Function->hasBody() && Source->second->Selected->isThisDeclarationADefinition()) {
            if (Function->isUsed(/*CheckUsedAttr=*/false))
              A.reject(Function->getLocation(), "friend definition",
                       "A required friend definition must be materialized in this source unit.", "TR0203");
            return true; // Keep unused ordinary bodies and undeduced auto lazy.
          }
        } else if (concreteFriendFunction(Function)) {
          A.reject(Function->getLocation(), "friend function source",
                   "A member-specialized free friend requires paired substitution evidence.");
          return true;
        } else if (auto Written = WrittenFriendFunctions.find(Function);
                   Written != WrittenFriendFunctions.end() &&
                   Function->getLexicalDeclContext()->isDependentContext()) {
          return traverseWrittenFriendSignature(Written->second);
        } else if (FriendCanonicalFunctions.count(Function->getCanonicalDecl()) &&
                   !CheckedFriendFunctions.insert(Function).second) {
          return true; // Merged namespace definitions can be mutually recursive.
        }
      }
    if (A.S.coreV2() && owned(D)) {
      if (!checkClassFullOwnerSources(D->getDeclContext()))
        return false;
      if (!A.S.Diagnostics.empty())
        return true;
    }
    if (A.S.coreV2()) {
      if (auto *Variable = dyn_cast_or_null<VarDecl>(D);
          Variable && Variable->isStaticDataMember() && owned(Variable) &&
          !Variable->getDescribedVarTemplate() && !isa<VarTemplateSpecializationDecl>(Variable)) {
        const auto *Parent = dyn_cast<CXXRecordDecl>(Variable->getDeclContext());
        if (const auto *Primary = classTemplatePattern(Parent);
            Parent && Parent->isDependentContext() &&
            (Primary || genericClassFullIdentity(Parent) || ordinaryMemberClassScope(Parent))) {
          const bool Shape = genericClassFullIdentity(Parent) || ordinaryMemberClassScope(Parent)
              ? zeroParameterClassBodyShape(Parent) : classPatternShape(Primary);
          if (!Shape || !classTemplateStaticDataShape(Variable) ||
              !outerTemplateListsShape(Variable)) {
            A.reject(Variable->getLocation(), "class static member pattern",
                     "An admitted scalar or fixed-array static member of an owned class template is required.");
            return true;
          }
          for (unsigned I = 0; I < Variable->getNumTemplateParameterLists(); ++I) {
            const auto *Parameters = Variable->getTemplateParameterList(I);
            A.chargeExpansion(1, Variable->getLocation());
            if (Parameters->size() && !traverseTemplateParameterSource(Parameters, true))
              return false;
          }
          return true; // The member type and initializer retain normal laziness.
        }
        const auto *FullStatic = copiedFullClassStatic(Variable);
        const auto *OrdinaryStatic = copiedOrdinaryClassStatic(Variable);
        if (const auto *Primary = classStaticDataPattern(Variable); Primary || FullStatic || OrdinaryStatic) {
          const auto *Body = OrdinaryStatic ? OrdinaryStatic : FullStatic;
          if (!(Body ? zeroParameterClassBodyShape(Body) : classPatternShape(Primary)) ||
              !classTemplateStaticDataShape(Variable) ||
              !owned(Variable->getInstantiatedFromStaticDataMember())) {
            A.reject(Variable->getLocation(), "class static member instance",
                     "An owned static member with matching template origin is required.");
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
            Method->getParent()->isDependentContext() &&
            (Primary || genericClassFullIdentity(Method->getParent()) ||
             ordinaryMemberClassScope(Method->getParent()))) {
          // Out-of-line definitions are separate declarations in the namespace.
          // Their own outer parameter spelling must be checked before erasure.
          const bool Shape = genericClassFullIdentity(Method->getParent()) ||
                             ordinaryMemberClassScope(Method->getParent())
              ? zeroParameterClassBodyShape(Method->getParent()) : classPatternShape(Primary);
          if (!Shape || !classTemplateFunctionShape(Method) ||
              !outerTemplateListsShape(Method)) {
            A.reject(Method->getLocation(), "class template function pattern",
                     "An admitted member function of an owned class template is required.");
            return true;
          }
          for (unsigned I = 0; I < Method->getNumTemplateParameterLists(); ++I) {
            const auto *Parameters = Method->getTemplateParameterList(I);
            A.chargeExpansion(1, Method->getLocation());
            if (Parameters->size() && !traverseTemplateParameterSource(Parameters, true))
              return false;
          }
          return true; // No uninstantiated body, qualifier or function default.
        }
        const auto *FullMethod = copiedFullClassFunction(Method);
        const auto *OrdinaryMethod = copiedOrdinaryClassFunction(Method);
        if (const auto *Primary = classFunctionPattern(Method); Primary || FullMethod || OrdinaryMethod) {
          const auto *Body = OrdinaryMethod ? OrdinaryMethod : FullMethod;
          if (!(Body ? zeroParameterClassBodyShape(Body) : classPatternShape(Primary)) ||
              !classTemplateFunctionShape(Method)) {
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
                !defaultedCopyOrMoveConstructor(dyn_cast<CXXConstructorDecl>(Method)) &&
                !deletedFunctionDeclaration(Method)) {
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
                if (!ordinaryDestructor(Destructor) && !deletedFunctionDeclaration(Destructor)) {
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
          concreteFunctionTemplate(Function) &&
          !CheckedTemplateDeclarations.insert(Function).second)
        return true;
    if (A.S.coreV2())
      if (const auto *Method = dyn_cast_or_null<CXXMethodDecl>(D))
        indexLocalFunctionPacks(Method);
    const auto DefinitionDepth = DefinitionFrames.size();
    if (A.S.coreV2())
      if (const auto *Function = dyn_cast_or_null<FunctionDecl>(D);
          concreteFunctionTemplate(Function)) {
        const auto *Primary = Function->getPrimaryTemplate();
        const auto *Arguments = Function->getTemplateSpecializationArgs();
        if (DefinitionDepth >= 64 || !functionTemplateShape(Primary) || !Arguments ||
            Arguments->size() != templateSourceParameters(Primary)->size()) {
          A.reject(Function->getLocation(), "function definition context", "A concrete definition needs a bounded matching primary and argument list.");
          return true;
        }
        checkTemplateArguments(templateSourceParameters(Primary), *Arguments, Function->getLocation());
        if (!A.S.Diagnostics.empty())
          return true;
        DefinitionFrames.push_back({Function, Primary, Arguments, TemplateFrames.size()});
      }
    if (A.S.coreV2())
      if (const auto *Function = dyn_cast_or_null<FunctionDecl>(D);
          Function && FriendCanonicalFunctions.count(Function->getCanonicalDecl())) {
        if (DefinitionDepth >= 64) {
          A.reject(Function->getLocation(), "friend definition depth",
                   "Friend definition source exceeds the bounded context depth.");
          return true;
        }
        DefinitionFrames.push_back({Function, nullptr, nullptr, TemplateFrames.size()});
      }
    auto RestoreDefinition = llvm::make_scope_exit([&] { DefinitionFrames.resize(DefinitionDepth); });
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
    auto RestoreMethod = llvm::make_scope_exit([&] { CurrentMethod = Saved; });
    if (A.S.coreV2())
      if (const auto *Function = dyn_cast_or_null<FunctionDecl>(D))
        if (auto Found = SpecializationSources.find(Function); Found != SpecializationSources.end())
          for (const auto *Source : Found->second)
            checkFunctionTemplateUse(Source->Selected, Source->Location,
                Source->Written ? Source->Written->arguments() : llvm::ArrayRef<TemplateArgumentLoc>{});
    const auto *SavedDeclarator = CurrentDeclarator;
    CurrentDeclarator = dyn_cast_or_null<DeclaratorDecl>(D);
    auto RestoreDeclarator = llvm::make_scope_exit([&] { CurrentDeclarator = SavedDeclarator; });
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
        if (I->isDelegatingInitializer() && C->isDelegatingConstructor() &&
            I->isWritten() && !I->isPackExpansion() && I->getTypeSourceInfo() &&
            I->getInit() && C->getTargetConstructor() &&
            C->getTargetConstructor()->getParent()->getCanonicalDecl() ==
                C->getParent()->getCanonicalDecl())
          continue; // RAV visited the written type, arguments and selected call.
        if (!I->isMemberInitializer() || I->isPackExpansion() ||
            I->getMember()->getParent() != C->getParent() ||
            !Initialized.insert(I->getMember()->getCanonicalDecl()).second ||
            !I->getInit()) {
          A.reject(C->getLocation(), "constructor initializer",
                   "Expected unique direct fields or one checked delegating initializer.");
          continue;
        }
        if (!I->isWritten()) {
          auto PreviousOwner = ImplicitInitializerOwner;
          ImplicitInitializerOwner = C->getLocation();
          auto RestoreOwner = llvm::make_scope_exit([&] {
            ImplicitInitializerOwner = PreviousOwner;
          });
          Result = TraverseStmt(I->getInit());
          if (!Result)
            break;
        }
      }
    }
    // Out-of-line template definitions can be hidden namespace declarations.
    // Source checking must follow the actual initializer owner, not only the use.
    if (auto *Variable = dyn_cast_or_null<VarDecl>(D);
        Result && A.S.coreV2() &&
        (classStaticDataPattern(Variable) || copiedFullClassStatic(Variable) ||
         copiedOrdinaryClassStatic(Variable)))
      if (auto *Definition = Variable->getDefinition(); Definition && Definition != Variable)
        Result = TraverseDecl(Definition);
    if (const auto *Function = dyn_cast_or_null<FunctionDecl>(D);
        Result && A.S.coreV2() && A.S.Diagnostics.empty() && Function && owned(Function) &&
        Function->isUserProvided() && Function->doesThisDeclarationHaveABody() &&
        Function->getTemplatedKind() == FunctionDecl::TK_NonTemplate)
      CompletedOperationDefinitions.insert(Function);
    return Result;
  }
  bool finishGeneratedMethods() {
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
          if (!TraverseStmt(I->getInit()))
            return false;
        }
      }
      if (!TraverseStmt(const_cast<CompoundStmt *>(Body)))
        return false;
      A.Functions.push_back(const_cast<CXXMethodDecl *>(Method));
    }
    return A.S.Diagnostics.empty();
  }
  bool finishOperationQueries() {
    // Deferral is closed before this pass. A query in the selected definition's
    // own body can use its completed proof, but all pending roots must pass
    // before any runtime lowering observes the provisional boolean values.
    for (const auto *Query : A.PendingOperationQueries) {
      auto Source = A.OperationTraits.find(Query);
      if (Source == A.OperationTraits.end() ||
          !operationTraitSource(A, Source->second, &CompletedOperationDefinitions,
                                operationTraitNeedsExceptionSource(Query->getTrait()))) {
        A.reject(Query->getExprLoc(), "operation trait source",
                 "A complete hypothetical operation requires checked exact ordinary definitions, arguments and destruction source.");
        return false;
      }
      A.VerifiedOperationQueries.insert(Query);
    }
    return A.S.Diagnostics.empty();
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
  bool TraverseArrayTypeTraitExpr(ArrayTypeTraitExpr *Query) {
    if (!RecursiveASTVisitor<Allowlist>::TraverseArrayTypeTraitExpr(Query))
      return false;
    // ArrayTypeTraitExpr has no Stmt children. RAV visits only the type, so
    // explicitly inspect the original index rather than just its folded value.
    return !Query->getDimensionExpression() ||
           TraverseStmt(Query->getDimensionExpression());
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
    auto *SavedFunction = CurrentFunction;
    const auto DefinitionDepth = DefinitionFrames.size();
    const auto *Function = P ? dyn_cast<FunctionDecl>(P->getDeclContext()) : nullptr;
    ImplicitInitializerOwner = L;
    CurrentDefaultField = nullptr;
    CurrentMethod = nullptr;
    CurrentFunction = Function;
    auto Restore = llvm::make_scope_exit([&] {
      ImplicitInitializerOwner = SavedOwner;
      CurrentDefaultField = SavedField;
      CurrentMethod = SavedMethod;
      CurrentFunction = SavedFunction;
      DefinitionFrames.resize(DefinitionDepth);
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
    if (concreteFunctionTemplate(Function)) {
      if (!checkFriendTemplateFunction(Function))
        return false;
      if (!A.S.Diagnostics.empty())
        return true;
      const auto *Primary = Function->getPrimaryTemplate();
      const auto *Arguments = Function->getTemplateSpecializationArgs();
      if (DefinitionDepth >= 64 || !functionTemplateShape(Primary) || !Arguments ||
          Arguments->size() != templateSourceParameters(Primary)->size()) {
        A.reject(L, "default argument source context", "The actual selected parameter needs its bounded concrete function context.");
        return true;
      }
      checkTemplateArguments(templateSourceParameters(Primary), *Arguments, L);
      if (!A.S.Diagnostics.empty())
        return true;
      DefinitionFrames.push_back({Function, Primary, Arguments, TemplateFrames.size()});
    }
    if (Function && FriendCanonicalFunctions.count(Function->getCanonicalDecl())) {
      if (DefinitionDepth >= 64) {
        A.reject(L, "friend default depth", "The selected friend default needs a bounded source context.");
        return true;
      }
      DefinitionFrames.push_back({Function, nullptr, nullptr, TemplateFrames.size()});
    }
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
        !A.Context.hasSameUnqualifiedType(Field->getType().getNonReferenceType(), Default->getType()) ||
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
    if (D->hasAttrs() && (!A.S.coreV2() || !supportedDeclarationAttributes(D))) {
      std::string Detail;
      llvm::raw_string_ostream OS(Detail);
      for (const auto *Attribute : D->attrs()) {
        OS << (Attribute->isImplicit() ? " implicit " : " written ");
        Attribute->printPretty(OS, A.Context.getPrintingPolicy());
      }
      A.reject(D->getLocation(), "attribute",
               "Source declaration attributes are unsupported:" + OS.str());
    }
    const bool ExtendedDeclaration =
        A.S.coreV2() &&
        (isa<TypedefNameDecl, EnumDecl, EnumConstantDecl, StaticAssertDecl, FriendDecl, EmptyDecl,
             NamespaceAliasDecl, UsingDirectiveDecl, UsingDecl,
             FunctionTemplateDecl, ClassTemplateDecl, TypeAliasTemplateDecl, VarTemplateDecl>(D) ||
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
  bool traverseWrittenFriendSignature(const FriendDecl *D) {
    if (!genericFriendFunctionShape(D) && !genericFriendTemplateShape(D)) {
      A.reject(D->getFriendLoc(), "friend source declaration",
               "The original friend must remain an admitted free function declaration.");
      return true;
    }
    if (!CheckedFriendDeclarations.insert(D).second)
      return true;
    const auto *Template = dyn_cast<FunctionTemplateDecl>(D->getFriendDecl());
    const auto *Function = Template ? Template->getTemplatedDecl()
                                    : cast<FunctionDecl>(D->getFriendDecl());
    if (DefinitionFrames.size() >= 64) {
      A.reject(D->getFriendLoc(), "friend source depth",
               "Friend declaration source exceeds the bounded context depth.");
      return true;
    }
    auto *SavedFunction = CurrentFunction;
    auto *SavedMethod = CurrentMethod;
    auto *SavedField = CurrentDefaultField;
    auto SavedOwner = ImplicitInitializerOwner;
    auto *SavedParameterSource = TemplateParameterTypeSource;
    TemplateParameterTypeSource = nullptr;
    CurrentFunction = Function;
    CurrentMethod = nullptr;
    CurrentDefaultField = nullptr;
    ImplicitInitializerOwner = D->getFriendLoc();
    DefinitionFrames.push_back({Function, nullptr, nullptr, TemplateFrames.size()});
    auto Restore = llvm::make_scope_exit([&] {
      DefinitionFrames.pop_back();
      CurrentFunction = SavedFunction;
      CurrentMethod = SavedMethod;
      CurrentDefaultField = SavedField;
      ImplicitInitializerOwner = SavedOwner;
      TemplateParameterTypeSource = SavedParameterSource;
    });
    auto CheckType = [&](TypeLoc Location) {
      auto Type = Location.getType();
      if (Type.isNull()) {
        A.reject(D->getFriendLoc(), "friend signature source", "An owned written type is required.");
        return true;
      }
      if (Type->isDependentType() || Type->isInstantiationDependentType() ||
          Type->isUndeducedAutoType())
        return true; // Its actual selected substitution retains the later check.
      A.type(Type, Location.getBeginLoc(), true);
      return TraverseTypeLoc(Location);
    };
    const auto Prototype = Function->getTypeSourceInfo()->getTypeLoc()
        .IgnoreParens().getAs<FunctionProtoTypeLoc>();
    if (!Prototype) {
      A.reject(D->getFriendLoc(), "friend prototype source", "The complete original prototype must be retained.");
      return true;
    }
    if (!CheckType(Prototype.getReturnLoc()))
      return false;
    for (const auto *Parameter : Function->parameters()) {
      const auto *Info = Parameter->getTypeSourceInfo();
      if (!Info) {
        A.reject(Parameter->getLocation(), "friend parameter source", "Each original parameter needs its written type.");
        return true;
      }
      if (!CheckType(Info->getTypeLoc()))
        return false;
      if (Parameter->hasDefaultArg() && !Parameter->hasUninstantiatedDefaultArg()) {
        auto *Default = Parameter->getDefaultArg();
        if (Default && !Default->isTypeDependent() && !Default->isValueDependent() &&
            !Default->isInstantiationDependent() && !TraverseStmt(const_cast<Expr *>(Default)))
          return false;
      }
    }
    const auto *Exception = Prototype.getTypePtr()->getNoexceptExpr();
    if (Exception && !Exception->isTypeDependent() && !Exception->isValueDependent() &&
        !Exception->isInstantiationDependent() && !TraverseStmt(const_cast<Expr *>(Exception)))
      return false;
    if (Function->getQualifier() && !Function->getQualifier()->isDependent() &&
        !TraverseNestedNameSpecifierLoc(Function->getQualifierLoc()))
      return false;
    return TraverseDeclarationNameInfo(Function->getNameInfo());
  }
  bool TraverseFriendDecl(FriendDecl *D) {
    if (A.S.coreV2() && owned(D))
      if (auto *Template = dyn_cast_or_null<ClassTemplateDecl>(D->getFriendDecl())) {
        if (!WalkUpFromFriendDecl(D))
          return false;
        if (!A.S.Diagnostics.empty())
          return true;
        if (!checkClassFullOwnerSources(D->getDeclContext()) ||
            !traverseFriendClassTemplateSource(D))
          return false;
        return !A.S.Diagnostics.empty() || TraverseClassTemplateDecl(Template);
      }
    if (A.S.coreV2() && owned(D))
      if (auto *Template = dyn_cast_or_null<FunctionTemplateDecl>(D->getFriendDecl())) {
        if (!WalkUpFromFriendDecl(D))
          return false;
        if (!A.S.Diagnostics.empty())
          return true;
        if (!checkClassFullOwnerSources(D->getDeclContext()))
          return false;
        if (auto Source = FriendDeclarationSources.find(D); Source != FriendDeclarationSources.end()) {
          if (!friendTemplateSourceIdentity(D)) {
            A.reject(D->getFriendLoc(), "copied friend template source",
                     "The copied primary requires its exact original and granting-class event.");
            return true;
          }
          if (!traverseWrittenFriendSignature(Source->second->Written))
            return false;
        } else if (copiedFriendTemplate(D) || !WrittenFriendTemplates.count(Template)) {
          A.reject(D->getFriendLoc(), "missing friend template source",
                   "An actual friend template requires its written or exact copied declaration.");
          return true;
        }
        if (!traverseWrittenFriendSignature(D))
          return false;
        return !A.S.Diagnostics.empty() || TraverseFunctionTemplateDecl(Template);
      }
    const auto *Parent = D ? dyn_cast<CXXRecordDecl>(D->getDeclContext()) : nullptr;
    if (!A.S.coreV2() || !owned(D) || !Parent ||
        (!Parent->isDependentContext() && !FriendDeclarationSources.count(D) &&
         !copiedFriendType(D)))
      return RecursiveASTVisitor<Allowlist>::TraverseFriendDecl(D);
    if (!WalkUpFromFriendDecl(D) || !A.S.Diagnostics.empty())
      return A.S.Diagnostics.empty();
    if (D->getFriendType())
      return traverseFriendTypeSource(D);
    if (auto Found = FriendDeclarationSources.find(D); Found != FriendDeclarationSources.end()) {
      if (!friendSourceIdentity(D)) {
        A.reject(D->getFriendLoc(), "copied friend source",
                 "The actual friend requires its exact written, selected and granting-class source.");
        return true;
      }
      if (!traverseWrittenFriendSignature(Found->second->Written))
        return false;
      if (!A.S.Diagnostics.empty())
        return true;
      return TraverseDecl(D->getFriendDecl());
    }
    return traverseWrittenFriendSignature(D);
  }

  bool VisitFriendDecl(FriendDecl *D) {
    if (!owned(D) || !A.S.coreV2())
      return true;
    if (isa_and_nonnull<ClassTemplateDecl>(D->getFriendDecl())) {
      if (!genericFriendClassTemplateShape(D))
        A.reject(D->getFriendLoc(), "friend class template",
                 "A source-owned class-template target in an admitted granting class is required.");
      return true;
    }
    if (isa_and_nonnull<FunctionTemplateDecl>(D->getFriendDecl())) {
      if (!genericFriendTemplateShape(D))
        A.reject(D->getFriendLoc(), "friend function template",
                 "A source-owned free friend template in an admitted class is required.");
      return true;
    }
    const auto *Parent = dyn_cast<CXXRecordDecl>(D->getDeclContext());
    if ((Parent && Parent->isDependentContext()) || FriendDeclarationSources.count(D) ||
        copiedFriendType(D)) {
      if (!genericFriendDeclarationShape(D))
        A.reject(D->getFriendLoc(), "generic friend declaration",
                 "A source-owned non-template free function or ordinary type friend is required.");
      return true;
    }
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
    if (owned(D) && A.S.coreV2()) {
      if (D->getUnderlyingType()->isFunctionType())
        A.functionPointerType(A.Context.getPointerType(D->getUnderlyingType()), D->getLocation());
      else
        A.type(D->getUnderlyingType(), D->getLocation(), true);
    }
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
    if (D->getDeclName().getNameKind() == DeclarationName::CXXLiteralOperatorName)
      A.reject(D->getLocation(), "literal operator declaration",
               "User-defined literal operators are outside the selected profile.");
    const bool Template = A.S.coreV2() && concreteFunctionTemplate(D);
    const bool InstantiatedMember = A.S.coreV2() && concreteMemberFunction(D);
    const bool InstantiatedFriend = A.S.coreV2() && concreteFriendFunction(D) &&
                                    FriendFunctionDeclarations.count(D);
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
    const bool Deleted = A.S.coreV2() && deletedFunctionDeclaration(D);
    const bool Defaulted = A.S.coreV2() &&
        ((Deleted && defaultedSpecialMember(Method)) ||
         defaultedLifecycle(Method) || defaultedAssignment(Method) ||
         defaultedCopyOrMoveConstructor(dyn_cast_or_null<CXXConstructorDecl>(Method)));
    if ((Method && (!A.S.coreV2() ||
                    (!callableMethod(Method) &&
                     !supportedConstructor(dyn_cast<CXXConstructorDecl>(Method)) &&
                     !ordinaryDestructor(dyn_cast<CXXDestructorDecl>(Method)) &&
                     !Defaulted && !Deleted))) ||
        D->isVariadic() ||
        D->getDescribedFunctionTemplate() ||
        (D->getTemplatedKind() != FunctionDecl::TK_NonTemplate &&
         !Template && !InstantiatedMember && !InstantiatedFriend) ||
        (D->isDeleted() && !Deleted) ||
        (D->isExplicitlyDefaulted() && !Defaulted) ||
        D->isConsteval())
      A.reject(D->getLocation(), "function",
               "This member, template, variadic or special function form is "
               "outside the selected profile.");
    if (A.S.coreV2() && D->isOverloadedOperator() && !Deleted &&
        !ordinaryOperator(D) && !supportedAssignment(Method))
      A.reject(D->getLocation(), "operator declaration",
               "This operator function is outside the selected profile.");
    if (A.S.coreV2() && allocationOperatorKind(D->getOverloadedOperator()) &&
        D->isReservedGlobalPlacementOperator())
      A.reject(D->getLocation(), "reserved placement operator",
               "Reserved global placement operators require the standard library runtime.", "TR0203");
    const auto *Prototype = D->getType()->getAs<FunctionProtoType>();
    if (Prototype && Prototype->hasExceptionSpec() && !Defaulted && !Deleted &&
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
    if (!D->hasBody() && !Defaulted && !Deleted && !lazyFreeFunctionSignature(D) &&
        A.nativeHeapImport(D, D->getLocation()).empty() &&
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
    if (D->doesThisDeclarationHaveABody() && !D->isImplicit() && !Defaulted && !Deleted) {
      if (const auto *Destructor = dyn_cast<CXXDestructorDecl>(D);
          A.S.coreV2() && Destructor)
        A.requireDestruction(Destructor->getParent(), Destructor->getLocation());
      else if (!FriendCanonicalFunctions.count(D->getCanonicalDecl()) ||
               EmittedFriendDefinitions.insert(D->getCanonicalDecl()).second)
        A.Functions.push_back(D);
    }
    return true;
  }
  bool staticScalarType(QualType T) {
    return T->isIntegralOrEnumerationType() || T->isPointerType() ||
           T->isNullPtrType() || binaryFloatingType(T);
  }
  bool staticStorageType(QualType T) {
    return staticScalarType(T) || T->isReferenceType() || T->isRecordType() ||
           A.Context.getAsConstantArrayType(T);
  }
  bool staticObjectElementType(QualType T, unsigned Depth = 0) {
    if (Depth > 64)
      return false;
    if (T->isReferenceType())
      return T->getPointeeType()->isObjectType();
    if (staticScalarType(T))
      return true;
    if (const auto *Array = A.Context.getAsConstantArrayType(T))
      return staticObjectElementType(Array->getElementType(), Depth + 1);
    if (const auto *Record = T->getAsCXXRecordDecl(); Record && Record->getDefinition()) {
      A.chargeExpansion(1, Record->getLocation());
      for (const auto *Field : Record->getDefinition()->fields())
        if (!staticObjectElementType(Field->getType(), Depth + 1))
          return false;
      return true;
    }
    return false;
  }
  bool evaluateStaticInitializer(const Expr *Init, const VarDecl *Definition,
                                 const VarDecl *InitializingDecl, APValue &Value,
                                 bool &Constant) {
    llvm::SmallVector<PartialDiagnosticAt, 8> Notes;
    const bool Evaluated =
        Init->EvaluateAsInitializer(Value, A.Context, Definition, Notes, true);
    // An inability to read runtime state permits dynamic initialization. A
    // proven invalid address or expired lifetime does not. These structured
    // diagnostics come from the pinned evaluator; never consume a failed
    // evaluation's partial APValue or turn its temporary storage into globals.
    bool NonGlobal = false, Temporary = false;
    for (const auto &Note : Notes) {
      switch (Note.second.getDiagID()) {
      case clang::diag::note_constexpr_array_index:
      case clang::diag::note_constexpr_past_end:
      case clang::diag::note_constexpr_past_end_subobject:
      case clang::diag::note_constexpr_null_subobject:
      case clang::diag::note_constexpr_access_null:
      case clang::diag::note_constexpr_access_past_end:
      case clang::diag::note_constexpr_lifetime_ended:
        A.reject(Note.first, "static initializer lifetime",
                 "A static initializer cannot use a proven invalid object address or expired lifetime.");
        return false;
      case clang::diag::note_constexpr_non_global:
        NonGlobal = true;
        break;
      case clang::diag::note_constexpr_temporary_here:
        Temporary = true;
        break;
      default:
        break;
      }
    }
    if ((NonGlobal && Temporary) ||
        (Evaluated && Definition->getType()->isReferenceType() &&
         Value.isLValue() && (Value.isNullPointer() || Value.isLValueOnePastTheEnd()))) {
      A.reject(Definition->getLocation(), "static reference lifetime",
               "A static reference requires a live object, not null, a past-end address or an unextended temporary.");
      return false;
    }
    // Preserve the native point-of-definition decision even when later
    // definitions let the evaluator calculate a value during this check.
    Constant = Evaluated && Notes.empty() &&
        (Definition->isStaticLocal() || InitializingDecl->hasConstantInitialization());
    return true;
  }
  bool cacheStaticObjectInitializer(VarDecl *Definition) {
    auto Canonical = Definition->getCanonicalDecl();
    if (A.ConstantStaticInitializers.count(Canonical))
      return true;
    auto T = Definition->getType();
    if (!staticObjectElementType(T) ||
        Definition->getTLSKind() != VarDecl::TLS_None || T.isVolatileQualified()) {
      A.reject(Definition->getLocation(), "static object storage",
               "Static arrays and records require admitted elements without TLS or volatile storage.");
      return false;
    }
    const VarDecl *InitializingDecl = nullptr;
    const auto *Init = Definition->getAnyInitializer(InitializingDecl);
    json::Object Initializer;
    if (Init) {
      APValue Value;
      if (!InitializingDecl || !owned(InitializingDecl) ||
          InitializingDecl->getCanonicalDecl() != Canonical ||
          InitializingDecl->getDeclContext()->getRedeclContext() !=
              Definition->getDeclContext()->getRedeclContext() ||
          !A.Context.hasSameType(InitializingDecl->getType(), T)) {
        A.reject(Definition->getLocation(), "static object initializer",
                 "A static object requires its exact source-owned initializer.");
        return false;
      }
      const auto *Construct = dyn_cast<CXXConstructExpr>(Init->IgnoreParenImpCasts());
      const bool ZeroOnly = Construct && !Construct->getNumArgs() &&
          Construct->getConstructor()->isDefaultConstructor() &&
          Construct->getConstructor()->isTrivial();
      if (ZeroOnly) {
        // Static storage is zero-initialized before a trivial default constructor.
        // Evaluating that constructor alone would leave scalar fields indeterminate.
        Initializer = A.zero(T, Definition->getLocation());
      } else {
        bool Constant = false;
        if (!evaluateStaticInitializer(Init, Definition, InitializingDecl, Value, Constant))
          return false;
        if (!Constant) {
          A.DynamicStaticObjects.insert(Canonical);
          Initializer = A.zero(T, Definition->getLocation());
        } else {
          A.ConstantStaticTemporaryOwners.insert(Canonical);
          Initializer = A.constant(Value, T, Definition->getLocation());
        }
      }
    } else {
      if (T.isConstQualified() && !T->isRecordType()) {
        A.reject(Definition->getLocation(), "static object initializer",
                 "A const static array requires its initializer in this unit.");
        return false;
      }
      Initializer = A.zero(T, Definition->getLocation());
    }
    A.ConstantStaticInitializers.emplace(Canonical, std::move(Initializer));
    return true;
  }
  bool staticScalarValue(const APValue &Value, QualType T, SourceLocation L) {
    if (T->isPointerType() || T->isNullPtrType()) {
      // Normalization checks null, a callback signature, or an object's typed
      // static subobject path and exact offset.
      // The original initializer still undergoes the ordinary source walk.
      A.constant(Value, T, L);
      return true;
    }
    return Value.isInt() || (binaryFloatingType(T) && Value.isFloat());
  }
  bool cacheStaticScalarInitializer(VarDecl *Definition) {
    const auto *Canonical = Definition->getCanonicalDecl();
    if (A.ConstantStaticInitializers.count(Canonical))
      return true;
    auto T = Definition->getType();
    auto L = Definition->getLocation();
    if (!staticScalarType(T) || Definition->getTLSKind() != VarDecl::TLS_None ||
        T.isVolatileQualified()) {
      A.reject(L, "static scalar storage",
               "A static scalar requires an admitted non-volatile type without TLS.");
      return false;
    }
    const VarDecl *InitializingDecl = nullptr;
    const auto *Init = Definition->getAnyInitializer(InitializingDecl);
    json::Object Initializer;
    if (Init) {
      if (!InitializingDecl || !owned(InitializingDecl) ||
          InitializingDecl->getCanonicalDecl() != Canonical ||
          InitializingDecl->getDeclContext()->getRedeclContext() !=
              Definition->getDeclContext()->getRedeclContext() ||
          !A.Context.hasSameType(InitializingDecl->getType(), T)) {
        A.reject(L, "static scalar initializer",
                 "A static scalar requires its exact source-owned initializer.");
        return false;
      }
      APValue Value;
      bool Constant = false;
      if (!evaluateStaticInitializer(Init, Definition, InitializingDecl, Value, Constant))
        return false;
      if (!Constant) {
        A.DynamicStaticObjects.insert(Canonical);
        Initializer = A.zero(T, L);
      } else {
        if (!staticScalarValue(Value, T, L)) {
          A.reject(L, "static scalar initializer",
                   "A static scalar requires an admitted value representation.");
          return false;
        }
        Initializer = A.constant(Value, T, L);
      }
    } else {
      if (T.isConstQualified()) {
        A.reject(L, "static scalar initializer",
                 "A const static scalar requires an initializer in this unit.");
        return false;
      }
      Initializer = A.zero(T, L);
    }
    A.ConstantStaticInitializers.emplace(Canonical, std::move(Initializer));
    return true;
  }
  bool cacheStaticReferenceInitializer(VarDecl *Definition) {
    auto Canonical = Definition->getCanonicalDecl();
    if (A.StaticReferenceInitializers.count(Canonical))
      return true;
    auto T = Definition->getType();
    if (!Definition->hasGlobalStorage() || !T->isReferenceType() ||
        !T->getPointeeType()->isObjectType() ||
        Definition->getTLSKind() != VarDecl::TLS_None) {
      A.reject(Definition->getLocation(), "static reference storage",
               "A static reference requires an admitted object type without TLS.");
      return false;
    }
    const VarDecl *InitializingDecl = nullptr;
    const auto *Init = Definition->getAnyInitializer(InitializingDecl);
    APValue Value;
    if (!Init || !InitializingDecl || !owned(InitializingDecl) ||
        InitializingDecl->getCanonicalDecl() != Canonical ||
        InitializingDecl->getDeclContext()->getRedeclContext() !=
            Definition->getDeclContext()->getRedeclContext() ||
        !A.Context.hasSameType(InitializingDecl->getType(), T)) {
      A.reject(Definition->getLocation(), "static reference initializer",
               "A static reference requires its source-owned initializer.");
      return false;
    }
    auto PointerType = A.Context.getPointerType(T->getPointeeType());
    bool Constant = false;
    if (!evaluateStaticInitializer(Init, Definition, InitializingDecl, Value, Constant))
      return false;
    if (!Constant) {
      // The binding and its lifetime-extended temporaries initialize together.
      // Runtime allocation never consumes a partial APValue.
      A.DynamicStaticObjects.insert(Canonical);
      A.StaticReferenceInitializers.emplace(
          Canonical, A.zero(PointerType, Definition->getLocation()));
      return true;
    }
    A.ConstantStaticTemporaryOwners.insert(Canonical);
    auto Initializer = A.constantPointer(Value, PointerType, Definition->getLocation(), /*ReferenceBinding=*/true);
    A.StaticReferenceInitializers.emplace(Canonical, std::move(Initializer));
    return true;
  }
  bool checkStaticData(VarDecl *D, bool TemplateInstance = false) {
    const auto ExpectedKind = TemplateInstance ? Decl::VarTemplateSpecialization : Decl::Var;
    const auto *Parent = dyn_cast<CXXRecordDecl>(D->getDeclContext());
    if (D->getKind() != ExpectedKind || !Parent || !owned(Parent) ||
        Parent->isDependentContext() ||
        !staticStorageType(D->getType()) ||
        D->getTLSKind() != VarDecl::TLS_None ||
        D->getType().isVolatileQualified()) {
      A.reject(D->getLocation(), "static data member",
               "Only non-thread-local scalar, reference, record or fixed-array members in supported owned classes are admitted.");
      return true;
    }
    auto *Definition = D->getDefinition();
    if (!Definition && !D->isInline() && D->getType().isConstQualified()) {
      const VarDecl *InitializingDecl = nullptr;
      const auto *Init = D->getAnyInitializer(InitializingDecl);
      APValue Value;
      if (Init && InitializingDecl && owned(InitializingDecl) &&
          InitializingDecl->getCanonicalDecl() == D->getCanonicalDecl() &&
          (!TemplateInstance || InitializingDecl->getDeclContext() == D->getDeclContext()) &&
          A.Context.hasSameType(InitializingDecl->getType(), D->getType()) &&
          D->isUsableInConstantExpressions(A.Context) &&
          Init->isCXX11ConstantExpr(A.Context, &Value) && Value.isInt()) {
        if (A.StaticMemberValues.emplace(D->getCanonicalDecl(), Value.getInt()).second)
          A.chargeExpansion(1, D->getLocation());
        return true; // Checked value metadata, not a storage definition.
      }
    }
    if (!Definition && TemplateInstance && !D->isUsed(/*CheckUsedAttr=*/false) &&
        cast<VarTemplateSpecializationDecl>(D)->getSpecializationKind() !=
            TSK_ExplicitInstantiationDefinition)
      return true; // An unevaluated fixed-type use does not request storage.
    const auto *DefinitionParent = Definition
        ? dyn_cast<CXXRecordDecl>(Definition->getDeclContext()) : nullptr;
    if (!Definition || !owned(Definition) || Definition->getKind() != ExpectedKind ||
        !Definition->isStaticDataMember() || !DefinitionParent ||
        DefinitionParent->getCanonicalDecl() != Parent->getCanonicalDecl() ||
        Definition->getCanonicalDecl() != D->getCanonicalDecl() ||
        !A.Context.hasSameType(Definition->getType(), D->getType())) {
      A.reject(D->getLocation(), "static data definition",
               "A static member requires one source-owned definition in this unit.",
               "TR0203");
      return true;
    }
    if (D->getType()->isArrayType() || D->getType()->isRecordType()) {
      if (cacheStaticObjectInitializer(Definition) &&
          CheckedScalarGlobals.insert(Definition->getCanonicalDecl()).second)
        A.Globals.push_back(Definition);
      return true;
    }
    if (D->getType()->isReferenceType()) {
      if (cacheStaticReferenceInitializer(Definition) &&
          CheckedScalarGlobals.insert(Definition->getCanonicalDecl()).second)
        A.Globals.push_back(Definition);
      return true;
    }
    if (TemplateInstance) {
      const VarDecl *InitializingDecl = nullptr;
      if (Definition->getAnyInitializer(InitializingDecl) &&
          (!InitializingDecl || InitializingDecl->getDeclContext() != D->getDeclContext())) {
        A.reject(D->getLocation(), "static data initializer",
                 "A member variable instance requires its own initializing context.");
        return true;
      }
    }
    if (!cacheStaticScalarInitializer(Definition))
      return true;
    if (CheckedScalarGlobals.insert(Definition->getCanonicalDecl()).second)
      A.Globals.push_back(Definition);
    // A non-inline const member's initializer can belong to its in-class
    // declaration. RAV still checks every written initializer and body.
    return true;
  }
  bool VisitVarDecl(VarDecl *D) {
    if (!owned(D))
      return true;
    A.type(D->getType(), D->getLocation());
    if (A.S.coreV2())
      if (const auto *Variable = dyn_cast<VarTemplateSpecializationDecl>(D)) {
        if (Variable->getKind() != Decl::VarTemplateSpecialization ||
            !variablePatternType(Variable) || !staticStorageType(D->getType())) {
          A.reject(D->getLocation(), "variable specialization storage", "Only concrete namespace or admitted member scalar, reference, record and fixed-array variables are admitted.");
          return true;
        }
        if (D->isStaticDataMember())
          return checkStaticData(D, true);
        auto *Definition = D->getDefinition();
        if (!Definition) {
          if (D->isUsed(/*CheckUsedAttr=*/false) ||
              Variable->getSpecializationKind() == TSK_ExplicitInstantiationDefinition)
            A.reject(D->getLocation(), "variable template definition", "A required variable instance needs its definition in this source unit.", "TR0203");
          return true; // Fixed-type unevaluated uses do not create storage.
        }
        if (!owned(Definition) || Definition->getKind() != Decl::VarTemplateSpecialization ||
            !variablePatternType(Definition) ||
            Definition->getCanonicalDecl() != D->getCanonicalDecl() ||
            !A.Context.hasSameType(Definition->getType(), D->getType())) {
          A.reject(D->getLocation(), "variable template definition identity", "A variable instance requires its own source-owned canonical definition.", "TR0203");
          return true;
        }
        if (D->getType()->isArrayType() || D->getType()->isRecordType()) {
          if (cacheStaticObjectInitializer(Definition) &&
              CheckedScalarGlobals.insert(Definition->getCanonicalDecl()).second)
            A.Globals.push_back(Definition);
          return true;
        }
        if (D->getType()->isReferenceType()) {
          if (cacheStaticReferenceInitializer(Definition) &&
              CheckedScalarGlobals.insert(Definition->getCanonicalDecl()).second)
            A.Globals.push_back(Definition);
          return true;
        }
        if (!cacheStaticScalarInitializer(Definition))
          return true;
        if (CheckedScalarGlobals.insert(Definition->getCanonicalDecl()).second)
          A.Globals.push_back(Definition);
        return true;
      }
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
    if (A.S.coreV2() && D->isStaticDataMember())
      return checkStaticData(D);
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
           !concreteFreeFunctionTemplate(Parent) && !concreteMemberFunction(Parent) &&
           !concreteFriendFunction(Parent)) ||
          D->hasExternalStorage() || D->getTLSKind() != VarDecl::TLS_None ||
          D->getType().isVolatileQualified() ||
          !staticStorageType(D->getType()) || Definition != D) {
        A.reject(D->getLocation(), "static local",
                 "Only owned non-volatile scalar, reference, record or fixed-array static locals in non-constexpr functions are supported.");
        return true;
      }
      if (D->getType()->isArrayType() || D->getType()->isRecordType()) {
        if (cacheStaticObjectInitializer(D) &&
            A.StaticLocals.insert(D->getCanonicalDecl()).second) {
          A.chargeExpansion(1, D->getLocation());
          A.Globals.push_back(D);
        }
        return true;
      }
      if (D->getType()->isReferenceType()) {
        if (cacheStaticReferenceInitializer(D) &&
            A.StaticLocals.insert(D->getCanonicalDecl()).second) {
          A.chargeExpansion(1, D->getLocation());
          A.Globals.push_back(D);
        }
        return true;
      }
      if (!cacheStaticScalarInitializer(D))
        return true;
      if (A.StaticLocals.insert(D->getCanonicalDecl()).second) {
        A.chargeExpansion(1, D->getLocation());
        A.Globals.push_back(D);
      }
      // RAV checks the original initializer for both initialization categories.
      return true;
    }
    if (A.S.coreV2() && !D->isLocalVarDeclOrParm() && D->getType()->isReferenceType()) {
      auto *Definition = D->getDefinition();
      if (!Definition || !owned(Definition) || Definition->getKind() != Decl::Var ||
          !Definition->getDeclContext()->getRedeclContext()->isFileContext() ||
          Definition->getCanonicalDecl() != D->getCanonicalDecl() ||
          !A.Context.hasSameType(Definition->getType(), D->getType())) {
        A.reject(D->getLocation(), "static reference definition",
                 "A namespace reference requires its owned definition in this unit.", "TR0203");
        return true;
      }
      if (cacheStaticReferenceInitializer(Definition) &&
          CheckedScalarGlobals.insert(Definition->getCanonicalDecl()).second)
        A.Globals.push_back(Definition);
      return true;
    }
    if (A.S.coreV2() && D->getType()->isReferenceType() && D->getInit())
      checkBinding(D->getInit(), HasDefault,
                   D->getKind() == Decl::Var && D->isLocalVarDecl() && D->hasLocalStorage()
                       ? D->getCanonicalDecl() : nullptr);
    if (A.S.coreV2() && !D->isLocalVarDeclOrParm() &&
        (D->getType()->isArrayType() || D->getType()->isRecordType())) {
      auto *Definition = D->getDefinition();
      if (!Definition || !owned(Definition) || Definition->getKind() != Decl::Var ||
          !Definition->getDeclContext()->getRedeclContext()->isFileContext() ||
          Definition->getCanonicalDecl() != D->getCanonicalDecl() ||
          !A.Context.hasSameType(Definition->getType(), D->getType())) {
        A.reject(D->getLocation(), "static object definition",
                 "A static array or record requires its owned definition in this unit.", "TR0203");
        return true;
      }
      auto Canonical = Definition->getCanonicalDecl();
      if (A.ConstantStaticInitializers.count(Canonical))
        return true;
      if (!cacheStaticObjectInitializer(Definition))
        return true;
      A.Globals.push_back(Definition);
      return true;
    }
    if (!D->isLocalVarDeclOrParm() &&
        ((D->getType()->isPointerType() && !A.S.coreV2()) ||
         D->getType()->isReferenceType() ||
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
      if (A.S.coreV2() && staticScalarType(D->getType())) {
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
        if (!cacheStaticScalarInitializer(Definition))
          return true;
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
  bool admittedRecordLayout(const CXXRecordDecl *D, unsigned Depth = 0) {
    if (!D || Depth > 64 || !D->getDefinition())
      return false;
    D = D->getDefinition();
    if (D->isStandardLayout())
      return true;
    auto Found = FlatReferenceLayouts.find(D);
    if (Found != FlatReferenceLayouts.end())
      return Found->second;
    FlatReferenceLayouts.emplace(D, false);
    if (D->isUnion() || D->getNumBases() || D->isDynamicClass() || hasNonFinalAttributes(D))
      return false;
    bool HasReference = false;
    std::optional<AccessSpecifier> Access;
    for (const auto *Field : D->fields()) {
      A.chargeExpansion(1, Field->getLocation());
      if (Field->isBitField() || Field->isMutable() || Field->hasAttrs() ||
          (Access && *Access != Field->getAccess()))
        return false;
      Access = Field->getAccess();
      auto T = A.Context.getBaseElementType(Field->getType());
      if (T->isReferenceType()) {
        if (!T->getPointeeType()->isObjectType())
          return false;
        HasReference = true;
      } else if (const auto *Member = T->getAsCXXRecordDecl()) {
        if (!admittedRecordLayout(Member, Depth + 1))
          return false;
        HasReference |= !Member->isStandardLayout();
      }
    }
    FlatReferenceLayouts[D] = HasReference;
    return HasReference;
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
            !admittedRecordLayout(D) || !classTemplateMembers(D)) {
          A.reject(D->getLocation(), "class template specialization",
                   "A concrete specialization with admitted flat layout and an owned primary is required.");
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
    const bool ConstructedRecord = A.S.coreV2() && admittedRecordLayout(D);
    if (D->isUnion() || (!D->isAggregate() && !ConstructedRecord) ||
        (A.S.coreV2() && !ConstructedRecord) ||
        (!A.S.coreV2() && D->field_empty()) ||
        !emptyBasesShape(D) || D->getDescribedClassTemplate() ||
        (D->getDeclContext()->isRecord() &&
         (!A.S.coreV2() || !D->getIdentifier())))
      A.reject(D->getLocation(), "record",
               "Only admitted flat records with supported selected special "
               "members and checked trivial empty base chains are admitted; empty and named nested "
               "records require core v2.");
    A.Records.push_back(D);
    return true;
  }
  bool VisitFieldDecl(FieldDecl *D) {
    if (!owned(D))
      return true;
    A.type(D->getType(), D->getLocation());
    // Const members share the initialization carriers used by const locals.
    // Clang checks source writes; addresses/references retain source qualifiers.
    if (D->isBitField() ||
        (!A.S.coreV2() && (D->hasInClassInitializer() || D->getType().isConstQualified())) ||
        D->isMutable() || (!A.S.coreV2() && D->getType()->isReferenceType()))
      A.reject(D->getLocation(), "field",
               "Bitfield and mutable fields are unsupported; "
               "reference/const fields and default field initializers require core v2.");
    return true;
  }
  bool defaultFieldTemporary(const MaterializeTemporaryExpr *M) {
    const auto *Descriptor = M->getLifetimeExtendedTemporaryDecl();
    return CurrentDefaultField && CurrentDefaultField->hasInClassInitializer() &&
           owned(CurrentDefaultField) && temporaryShape(M, A.Context) &&
           M->getStorageDuration() == SD_Automatic &&
           M->getExtendingDecl() == CurrentDefaultField && Descriptor &&
           Descriptor->getExtendingDecl() == CurrentDefaultField &&
           Descriptor->getTemporaryExpr() == M->getSubExpr() &&
           Descriptor->getStorageDuration() == SD_Automatic;
  }
  bool VisitStmt(Stmt *S) {
    if (!S || (!ImplicitInitializerOwner.isValid() &&
               !A.S.owns(A.Sources, S->getBeginLoc())))
      return true;
    if (A.S.coreV2() && InitializerLookupWrappers.count(S))
      return true; // Exact source wrappers of an already checked callback clause.
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
      if (const auto *Call = dyn_cast<CallExpr>(S)) {
        const Expr *Leaf = directFunctionReference(Call);
        if (!Leaf)
          Leaf = scalarDestruction(Call, A.Context);
        if (Leaf) {
          // BuildOverloadedCallExpr ranks candidates at the complete callee's
          // expression location, which can be a parenthesis before the name.
          DirectTemplateCallLocations.emplace(Leaf, Call->getCallee()->getExprLoc());
          const Expr *E = Call->getCallee();
          while (true) {
            DirectFunctionCallees.insert(E);
            if (E == Leaf)
              break;
            if (const auto *P = dyn_cast<ParenExpr>(E))
              E = P->getSubExpr();
            else
              E = cast<ImplicitCastExpr>(E)->getSubExpr();
          }
        }
      }
    if (const auto *Reference = dyn_cast<DeclRefExpr>(S))
      if (const auto *Function = dyn_cast<FunctionDecl>(Reference->getDecl());
          Function && !DirectFunctionCallees.count(Reference) &&
          !A.nativeHeapImport(Function, L).empty())
        A.reject(L, "native heap function value", "Native heap functions require an exact direct call; function addresses and discarded designators are unsupported.");
    if (const auto *E = dyn_cast<Expr>(S)) {
      // Clang's unevaluated diagnostic strings have no QualType. They can
      // appear below an already rejected declaration (for example a v1
      // static_assert), so diagnose them before inspecting expression types.
      if (E->getType().isNull()) {
        if (A.S.coreV2() && NewArrayInitializers.count(E))
          if (const auto *List = dyn_cast<InitListExpr>(E)) {
            checkSemanticInitializers(List, L);
            return true; // RAV still visits this exact written list's children.
          }
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
          case CK_IntegralToFloating:
          case CK_FloatingToIntegral:
          case CK_FloatingToBoolean:
          case CK_FloatingCast:
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
          case CK_DerivedToBase:
          case CK_UncheckedDerivedToBase:
          case CK_BaseToDerived:
            A.emptyBaseCast(C);
            break;
          case CK_BitCast:
            if (C->getType()->isPointerType() &&
                C->getSubExpr()->getType()->isPointerType() &&
                !C->getType()->isFunctionPointerType() &&
                !C->getSubExpr()->getType()->isFunctionPointerType() &&
                (C->getType()->getPointeeType()->isVoidType() ||
                 C->getSubExpr()->getType()->getPointeeType()->isVoidType()))
              break;
            [[fallthrough]];
          default:
            A.reject(E->getExprLoc(), "cast",
                     "This cast operation is outside the core v2 profile.");
          }
        }
      if (A.S.coreV2() && !DirectFunctionCallees.count(E)) {
        if (const auto *C = dyn_cast<CastExpr>(E);
            C && C->getCastKind() == CK_FunctionToPointerDecay)
          FunctionValueDesignators.insert(C->getSubExpr());
        if (const auto *U = dyn_cast<UnaryOperator>(E);
            U && U->getOpcode() == UO_AddrOf &&
            U->getSubExpr()->getType()->isFunctionType())
          FunctionValueDesignators.insert(U->getSubExpr());
        if (FunctionValueDesignators.count(E)) {
          if (const auto *P = dyn_cast<ParenExpr>(E))
            FunctionValueDesignators.insert(P->getSubExpr());
          else if (const auto *C = dyn_cast<ConstantExpr>(E))
            FunctionValueDesignators.insert(C->getSubExpr());
          else if (const auto *W = dyn_cast<ExprWithCleanups>(E))
            FunctionValueDesignators.insert(W->getSubExpr());
          else if (const auto *B = dyn_cast<BinaryOperator>(E);
                   B && B->getOpcode() == BO_Comma)
            FunctionValueDesignators.insert(B->getRHS());
          else if (const auto *C = dyn_cast<ConditionalOperator>(E)) {
            FunctionValueDesignators.insert(C->getTrueExpr());
            FunctionValueDesignators.insert(C->getFalseExpr());
          }
        }
      }
      const auto *Cast = dyn_cast<ImplicitCastExpr>(E);
      bool FunctionDecay =
          Cast && Cast->getCastKind() == CK_FunctionToPointerDecay;
      if (!DirectFunctionCallees.count(E) && !GeneratedBuiltinCallees.count(E)) {
        if (A.S.coreV2() && E->getType()->isFunctionType() &&
            FunctionValueDesignators.count(E)) {
          A.functionPointerType(A.Context.getPointerType(E->getType()), E->getExprLoc());
          const ValueDecl *Target = nullptr;
          if (const auto *Reference = dyn_cast<DeclRefExpr>(E))
            Target = Reference->getDecl();
          else if (const auto *Member = dyn_cast<MemberExpr>(E))
            Target = Member->getMemberDecl();
          if (const auto *Function = dyn_cast_or_null<FunctionDecl>(Target))
            A.functionAddressTarget(Function, E->getExprLoc());
        } else if (!NewArrayInitializers.count(E) &&
                   !(A.S.coreV2() && isa<CXXNullPtrLiteralExpr>(E->IgnoreParens())) &&
                   !E->getType()->isFunctionType() &&
                   (A.S.coreV2() || !FunctionDecay)) {
          A.type(E->getType(), E->getExprLoc(), true);
        }
      }
    }
    const auto *Opaque = dyn_cast<OpaqueValueExpr>(S);
    const bool GeneratedArrayNode = A.S.coreV2() &&
        ImplicitInitializerOwner.isValid() &&
        defaultedCopyOrMoveConstructor(dyn_cast_or_null<CXXConstructorDecl>(CurrentMethod)) &&
        (isa<ArrayInitLoopExpr>(S) || (Opaque && ArraySources.count(Opaque)) ||
         (isa<ArrayInitIndexExpr>(S) && ArrayIndexDepth));
    if (!GeneratedArrayNode && !((A.S.math() || A.S.coreV2()) && isa<FloatingLiteral>(S)) &&
        !(A.S.coreV2() &&
          isa<ConstantExpr, CXXNullPtrLiteralExpr, CXXConstCastExpr,
              CXXFunctionalCastExpr, ArraySubscriptExpr, SwitchStmt, CaseStmt,
              DefaultStmt, AttributedStmt, CharacterLiteral, StringLiteral,
              UnaryExprOrTypeTraitExpr, TypeTraitExpr, ArrayTypeTraitExpr,
              CXXNoexceptExpr, CXXThisExpr,
              CXXDefaultInitExpr, CXXDefaultArgExpr, CXXForRangeStmt,
              SubstNonTypeTemplateParmExpr, SizeOfPackExpr,
              CXXPseudoDestructorExpr, CXXNewExpr, CXXDeleteExpr>(S)) &&
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
      if (const auto *N = dyn_cast<CXXNewExpr>(S)) {
        auto Object = N->getAllocatedType();
        if ((!N->isArray() && Object->isArrayType()) || !Object->isObjectType() ||
            Object->isIncompleteType() || N->isTypeDependent() ||
            N->isValueDependent() || N->isInstantiationDependent()) {
          A.reject(L, "new expression", "Complete admitted objects require checked allocation and initialization.");
          return true;
        }
        A.type(Object, L);
        if (A.storageUnits(Object) > 200000)
          A.reject(L, "new object storage", "Allocated object exceeds the storage limit.");
        const auto *Selected = N->getOperatorNew();
        const auto *F = A.allocationFunction(Selected, true, L, N->isArray());
        if (N->isArray()) {
          A.arrayNewInfo(N); // Shared bound/initializer proof also covers erased source.
          const Expr *Init = N->getInitializer();
          while (Init) {
            // Exempt only this new-expression's exact array wrapper. Its
            // clauses, defaults, constructors and written types remain checked.
            NewArrayInitializers.insert(Init);
            if (const auto *List = dyn_cast<InitListExpr>(Init)) {
              if (const auto *Written = List->getSyntacticForm())
                NewArrayInitializers.insert(Written);
              if (const auto *Semantic = List->getSemanticForm())
                NewArrayInitializers.insert(Semantic);
            }
            if (const auto *Wrapper = dyn_cast<ExprWithCleanups>(Init))
              Init = Wrapper->getSubExpr();
            else
              break;
          }
        }
        checkSelectedTemplateCall(N, Selected, L);
        unsigned Prefix = 1 + unsigned(N->passAlignment());
        if (F->getNumParams() != Prefix + N->getNumPlacementArgs() ||
            (N->passAlignment() && !F->getParamDecl(1)->getType()->isAlignValT())) {
          A.reject(L, "allocation arguments", "Allocation size, alignment and placement arguments must match the selected function.");
        } else {
          for (unsigned I = 0; I < N->getNumPlacementArgs(); ++I) {
            const auto *Arg = N->getPlacementArg(I);
            checkDefaultArgument(Arg, Selected, Prefix + I, L);
            if (F->getParamDecl(Prefix + I)->getType()->isReferenceType())
              checkBinding(Arg, true);
          }
        }
        A.S.Module["memory_lifetimes"] = true;
      }
      if (const auto *Delete = dyn_cast<CXXDeleteExpr>(S)) {
        auto Object = Delete->getDestroyedType();
        if (Object.isNull() || !Object->isObjectType() ||
            (!Delete->isArrayForm() && Object->isArrayType()) || Object->isIncompleteType()) {
          A.reject(L, "delete expression", "Complete admitted objects require their selected deallocation function.");
          return true;
        }
        A.type(Object, L);
        const auto *F = A.allocationFunction(Delete->getOperatorDelete(), false, L, Delete->isArrayForm());
        unsigned Index = 1;
        const bool Sized = Index < F->getNumParams() &&
            A.Context.hasSameUnqualifiedType(F->getParamDecl(Index)->getType(), A.Context.getSizeType());
        if (Sized)
          ++Index;
        if (Index < F->getNumParams() && F->getParamDecl(Index)->getType()->isAlignValT())
          ++Index;
        if (Index != F->getNumParams() || concreteFunctionTemplate(F))
          A.reject(L, "deallocation arguments", "Usual deallocation requires a pointer followed only by selected size and alignment values.");
        if (Delete->isArrayForm()) {
          const auto Layout = A.arrayAllocationLayout(Object, Delete->doesUsualArrayDeleteWantSize(), L);
          if (Sized && !Layout.CookieBytes)
            A.reject(L, "sized array delete", "The native array ABI supplies no count for this selected sized deallocation function.");
        }
        A.S.Module["memory_lifetimes"] = true;
      }
      if (const auto *D = dyn_cast<CXXPseudoDestructorExpr>(S);
          D && !DirectFunctionCallees.count(D))
        A.reject(L, "scalar destruction", "A checked zero-argument pseudo-destructor call is required.");
      if (isa<UserDefinedLiteral>(S))
        A.reject(L, "user-defined literal",
                 "User-defined literal calls require their own checked source contract.");
      if (const auto *Literal = dyn_cast<StringLiteral>(S))
        A.checkStringLiteral(Literal);
      if (const auto *M = dyn_cast<MaterializeTemporaryExpr>(S);
          M && !fullExpressionTemporary(M, A.Context) && !A.temporaryOwner(M) &&
          !A.staticTemporaryOwner(M) && !defaultFieldTemporary(M))
        A.reject(L, "temporary lifetime",
                 "A checked full-expression temporary, source field initializer or exact automatic/static extending owner is required.");
      if (const auto *Query = dyn_cast<CXXNoexceptExpr>(S)) {
        if (!Query->getOperand() || Query->isTypeDependent() ||
            Query->isValueDependent() || Query->isInstantiationDependent())
          A.reject(Query->getExprLoc(), "noexcept query",
                   "A resolved constant noexcept query is required.");
        // RAV visits the unevaluated operand and written specification
        // expressions. Unsupported source must not disappear behind a bool.
      }
      if (const auto *Query = dyn_cast<TypeTraitExpr>(S);
          Query && !parameterTypeQueryMetadata(Query))
        A.typeClassificationValue(Query);
      if (const auto *Query = dyn_cast<ArrayTypeTraitExpr>(S))
        A.arrayTypeQueryValue(Query);
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
      if (A.S.coreV2())
        if (const auto *D = scalarDestruction(C, A.Context)) {
          A.type(D->getDestroyedType(), L);
          if (const auto *Scope = D->getScopeTypeInfo())
            A.type(Scope->getType(), L);
          A.S.Module["memory_lifetimes"] = true;
          return true; // RAV still checks the base, qualifier and written types.
        }
      if (A.S.coreV2() && !directFunctionReference(C) &&
          C->getCallee()->getType()->isFunctionPointerType()) {
        if (A.functionPointerType(C->getCallee()->getType(), L).empty())
          return true;
        const auto *Prototype = C->getCallee()->getType()->getPointeeType()->getAs<FunctionProtoType>();
        if (C->getNumArgs() != Prototype->getNumParams())
          A.reject(L, "indirect call arguments", "Callable and argument counts differ.");
        for (unsigned I = 0; I < C->getNumArgs() && I < Prototype->getNumParams(); ++I)
          if (Prototype->getParamType(I)->isReferenceType())
            checkBinding(C->getArg(I), true);
        return true; // RAV still checks the complete postfix and argument sources.
      }
      const auto *F = C->getDirectCallee();
      const auto *Method = dyn_cast_or_null<CXXMethodDecl>(F);
      if (A.S.coreV2() && concreteMemberFunctionTemplate(F) &&
          isa<CXXConversionDecl>(F)) {
        const auto *Reference = dyn_cast_or_null<MemberExpr>(directMethodReference(C));
        if (Reference && Reference->getMemberLoc().isInvalid())
          checkSelectedTemplateCall(C, F, L);
      }
      if (A.S.coreV2())
        if (const auto *D = dyn_cast_or_null<CXXDestructorDecl>(F)) {
          const auto *Reference = dyn_cast_or_null<MemberExpr>(directMethodReference(C));
          const auto *Record = D->getParent()->getDefinition();
          if (C->getNumArgs() || !Reference || !callableMethod(D) ||
              !Record || !owned(Record)) {
            A.reject(L, "explicit destructor call", "A direct nonvirtual destructor of an admitted owned record is required.");
          } else {
            auto Object = A.Context.getRecordType(Record);
            A.type(Object, L);
            const auto *Base = Reference->getBase();
            if (Reference->isArrow() ? temporaryArrayBase(Base, true)
                                      : temporaryBinding(Base, true))
              A.reject(L, "destructor receiver", "A supported live or full-expression temporary receiver is required.");
            // Unevaluated uses do not instantiate a lazy destructor body.
            // Actual runtime lowering queues the required destruction helper.
            A.S.Module["memory_lifetimes"] = true;
          }
          return true; // A trivial defaulted destructor need not have a body.
        }
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
      if (!A.nativeHeapImport(F, L).empty()) {
        if (!directFunctionReference(C) || C->getNumArgs() != F->getNumParams())
          A.reject(L, "native heap call", "A checked direct call with exact arguments is required.");
        return true; // RAV still checks every source argument and callee leaf.
      }
      const bool GeneratedAssignment = A.S.coreV2() && defaultedAssignment(Method);
      if (GeneratedAssignment) {
        if (!A.S.owns(A.Sources, Method->getLocation()))
          A.reject(L, "assignment", "The selected assignment must be source-owned.", "TR0203");
        else
          queueGenerated(Method, L);
      }
      const bool LazySignature = lazyFreeFunctionSignature(F);
      // Sema's unused specialization needs its complete signature and selected
      // source, but does not request a body. Runtime-used instances still need
      // definitions and lowering never emits this hypothetical call.
      if (LazySignature && !TraverseDecl(const_cast<FunctionDecl *>(F)))
        return false;
      if (!F ||
          (!GeneratedAssignment && !LazySignature && !F->isImplicit() && !F->hasBody() &&
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
           U->getSubExpr()->getType()->isPointerType() &&
           !(A.S.coreV2() && U->getSubExpr()->getType()->isFunctionPointerType())) ||
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
          Method && !Method->isImplicit() && !DirectFunctionCallees.count(Reference) &&
          !(A.S.coreV2() && Method->isStatic()))
        A.reject(Reference->getExprLoc(), "method value",
                 "A method name is supported only as a direct call target.");
    if (const auto *M = dyn_cast<MemberExpr>(S)) {
      const auto *V = dyn_cast<VarDecl>(M->getMemberDecl());
      const bool StaticData = A.S.coreV2() && V && V->isStaticDataMember();
      const auto *Method = dyn_cast<CXXMethodDecl>(M->getMemberDecl());
      const bool StaticMethod = A.S.coreV2() && Method && Method->isStatic();
      if ((!A.S.coreV2() && M->isArrow()) ||
          (!isa<FieldDecl>(M->getMemberDecl()) && !StaticData && !StaticMethod &&
           !DirectFunctionCallees.count(M)))
        A.reject(S->getBeginLoc(), "member access",
                 "Only fields, core-v2 static data and direct method calls are supported.");
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
        const bool Ordering = B->isRelationalOp();
        if (!Offset && !Ordering)
          A.reject(S->getBeginLoc(), "pointer binary operator",
                   "Only object pointer offsets, difference and comparisons are supported.");
        for (const auto *Operand : {B->getLHS(), B->getRHS()}) {
          if (!Operand->getType()->isPointerType())
            continue;
          auto Pointee = Operand->getType()->getPointeeType();
          if (!Pointee->isObjectType() || Pointee->isIncompleteType())
            A.reject(S->getBeginLoc(), "pointer object operation",
                     "Pointer offsets and ordering require complete object pointees.");
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
// fields and array elements require complete definitions in the protocol and
// emitted declarators, including arrays reached through callback signatures.
// Ordinary pointers to records require only the existing forward declarations.
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
    if (const auto *Base = A.emptyBase(R)) {
      auto Found = Indices.find(Base->Base);
      if (Found == Indices.end()) {
        A.reject(R->getLocation(), "base dependency", "An empty base requires its checked complete record definition.");
        throw Failure{};
      }
      Self(Self, Found->second, Depth + 1);
      if (Heights[Found->second] >= 64) {
        A.reject(R->getLocation(), "base dependency", "Empty base nesting exceeds the declaration limit.");
        throw Failure{};
      }
      Heights[I] = Heights[Found->second] + 1;
    }
    for (const auto *F : R->fields()) {
      // A pointer to a record needs only its forward declaration, but an array
      // element needs a complete definition even inside a callback signature.
      auto RequireType = [&](auto &&Walk, QualType T, bool Complete,
                             unsigned TypeDepth) -> void {
        if (TypeDepth > 64) {
          A.reject(F->getLocation(), "record dependency",
                   "Field declaration type nesting exceeds the depth limit.");
          throw Failure{};
        }
        A.chargeExpansion(1, F->getLocation());
        if (const auto *Array = A.Context.getAsConstantArrayType(T)) {
          Walk(Walk, Array->getElementType(), true, TypeDepth + 1);
          return;
        }
        if (T->isPointerType() || T->isReferenceType()) {
          Walk(Walk, T->getPointeeType(), false, TypeDepth + 1);
          return;
        }
        if (const auto *Prototype = T->getAs<FunctionProtoType>()) {
          Walk(Walk, Prototype->getReturnType(), true, TypeDepth + 1);
          for (auto Parameter : Prototype->param_types())
            Walk(Walk, Parameter, true, TypeDepth + 1);
          return;
        }
        const auto *Dependency = T->getAsCXXRecordDecl();
        if (!Complete || !Dependency)
          return;
        auto Found = Indices.find(Dependency->getCanonicalDecl());
        if (Found == Indices.end()) {
          A.reject(F->getLocation(), "record dependency",
                   "A field declaration requires a checked complete record definition.");
          throw Failure{};
        }
        Self(Self, Found->second, Depth + 1);
        // Cached dependencies still contribute their whole declaration depth.
        if (Heights[Found->second] >= 64) {
          A.reject(F->getLocation(), "record dependency",
                   "Record declaration nesting exceeds the depth limit.");
          throw Failure{};
        }
        Heights[I] = std::max(Heights[I], Heights[Found->second] + 1);
      };
      RequireType(RequireType, F->getType(), true, 0);
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
                  llvm::ArrayRef<FunctionSpecializationSource> Specializations,
                  llvm::ArrayRef<VariableTypeSource> VariableTypes,
                  llvm::ArrayRef<SelectedTemplateCallSource> SelectedCalls,
                  llvm::ArrayRef<ExplicitMemberClassInstantiationSource> MemberClassDirectives,
                  llvm::ArrayRef<FriendFunctionSource> FriendFunctions,
                  llvm::ArrayRef<FriendDeclarationSource> FriendDeclarations,
                  llvm::ArrayRef<FriendFunctionTemplateSource> FriendTemplates,
                  llvm::ArrayRef<FunctionTemplateBodySource> TemplateBodies,
                  llvm::ArrayRef<FriendClassTemplateSource> FriendClasses) {
  CheckingSource = true;
  auto FinishSource = llvm::make_scope_exit([&] { CheckingSource = false; });
  Allowlist Check(*this);
  Check.indexFriendSources(FriendFunctions, FriendDeclarations, FriendTemplates, TemplateBodies, FriendClasses);
  Check.indexTemplates();
  Check.indexTemplateSources(TemplateUses, Specializations, VariableTypes);
  Check.indexSelectedTemplateCalls(SelectedCalls);
  Check.checkFriendSourceIdentities();
  for (const auto &Directive : Directives)
    Check.checkExplicitFunctionInstantiation(Directive);
  for (const auto &Directive : StaticDirectives)
    Check.checkExplicitStaticDataInstantiation(Directive);
  for (const auto &Directive : MemberClassDirectives)
    Check.checkExplicitMemberClassInstantiation(Directive);
  bool Traversed = Check.TraverseDecl(Context.getTranslationUnitDecl());
  if (Traversed && S.Diagnostics.empty())
    Traversed = Check.finishGeneratedMethods();
  if (!Traversed && S.Diagnostics.empty())
    reject(Context.getTranslationUnitDecl()->getBeginLoc(), "source traversal",
           "Complete source traversal is required before operation validation and lowering.");
  if (!Traversed || !S.Diagnostics.empty())
    return;
  CheckingSource = false;
  if (!Check.finishOperationQueries())
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
    const auto *Base = S.coreV2() ? emptyBase(R) : nullptr;
    if (Base)
      Fields.push_back(json::Object{{"name", Base->Member},
          {"type", type(Context.getRecordType(Base->Base), R->getLocation())}});
    for (const auto *F : R->fields())
      Fields.push_back(json::Object{
          {"name", name(F)}, {"type", type(F->getType(), F->getLocation())}});
    json::Object Record{{"id", name(R)}, {"fields", std::move(Fields)},
                        {"loc", loc(R->getLocation())}};
    if (S.coreV2()) {
      const auto &Layout = Context.getASTRecordLayout(R);
      json::Array Offsets;
      if (Base)
        Offsets.push_back(uint64_t(Layout.getBaseClassOffset(Base->Base).getQuantity()) * 8);
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
    const bool Mutable = S.coreV2() && !G->getType().isConstQualified() &&
                         !G->getType()->isReferenceType();
    json::Object Initializer;
    const auto *Init = S.coreV2() && G->isStaticDataMember()
                           ? G->getAnyInitializer() : G->getInit();
    const bool Dynamic = DynamicStaticObjects.count(G->getCanonicalDecl());
    if (Dynamic) {
      auto StorageType = G->getType()->isReferenceType()
                             ? Context.getPointerType(G->getType()->getPointeeType())
                             : G->getType();
      Initializer = zero(StorageType, G->getLocation());
    } else if (auto Found = StaticReferenceInitializers.find(G->getCanonicalDecl());
        Found != StaticReferenceInitializers.end()) {
      Initializer = json::Object(Found->second);
    } else if (auto Found = ConstantStaticInitializers.find(G->getCanonicalDecl());
        Found != ConstantStaticInitializers.end()) {
      Initializer = json::Object(Found->second);
    } else if (Init) {
      APValue Value;
      if (!Init->isCXX11ConstantExpr(Context, &Value) || (Mutable && !Value.isInt() && !Value.isFloat() &&
          !G->getType()->isPointerType() && !G->getType()->isNullPtrType()))
        throw Failure{};
      Initializer = constant(Value, G->getType(), G->getLocation());
    } else {
      if (!Mutable || (!G->getType()->isIntegralOrEnumerationType() &&
                       !G->getType()->isRealFloatingType() &&
                       !G->getType()->isNullPtrType() &&
                       !G->getType()->isPointerType()))
        throw Failure{};
      Initializer = zero(G->getType(), G->getLocation());
    }
    json::Object Global{{"name", name(G)},
                        {"type", type(G->getType(), G->getLocation())},
                        {"value", std::move(Initializer)},
                        {"loc", loc(G->getLocation())}};
    if (Mutable)
      Global["mutable"] = true;
    if (Dynamic)
      Global["dynamic_initialization"] = true;
    if (S.coreV2() && needsDestruction(G->getType()))
      Global["destructor"] = requireStaticDestruction(name(G), G->getType(), G->getLocation());
    GlobalData.push_back(std::move(Global));
  }
  for (auto *F : Functions)
    FunctionData.push_back(lower(F));
  if (S.coreV2()) {
    std::vector<const VarDecl *> StartupObjects;
    std::set<const VarDecl *> StartupDefinitions;
    for (const auto *G : Globals)
      if (!G->isStaticLocal() &&
          (DynamicStaticObjects.count(G->getCanonicalDecl()) || needsDestruction(G->getType())) &&
          StartupDefinitions.insert(G->getCanonicalDecl()).second)
        StartupObjects.push_back(G);
    // Original locations retain declaration order inside one macro expansion.
    // Unordered template instances at identical locations use stable identities.
    std::sort(StartupObjects.begin(), StartupObjects.end(), [&](const auto *A, const auto *B) {
      auto AL = A->getLocation(), BL = B->getLocation();
      if (Sources.isBeforeInTranslationUnit(AL, BL))
        return true;
      if (Sources.isBeforeInTranslationUnit(BL, AL))
        return false;
      return name(A) < name(B);
    });
    if (!StartupObjects.empty()) {
      auto Startup = lowerStartup(StartupObjects);
      S.Module["startup"] = Startup.getString("name")->str();
      FunctionData.push_back(std::move(Startup));
    }
  }
  if (S.coreV2()) {
    // Either helper can discover another static temporary or record cleanup.
    // Copy work items before lowering: discovery may reallocate these vectors.
    std::size_t StaticIndex = 0, RecordIndex = 0;
    while (StaticIndex < StaticDestructions.size() || RecordIndex < Destructions.size()) {
      if (StaticIndex < StaticDestructions.size()) {
        auto Object = StaticDestructions[StaticIndex++];
        FunctionData.push_back(lowerStaticDestruction(Object));
      } else {
        const auto *Record = Destructions[RecordIndex++];
        FunctionData.push_back(lowerDestruction(Record));
      }
    }
  }
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
    if (Target.getFloatWidth() != 32 || Target.getDoubleWidth() != 64 ||
        &Target.getFloatFormat() != &llvm::APFloat::IEEEsingle() ||
        &Target.getDoubleFormat() != &llvm::APFloat::IEEEdouble()) {
      S.diagnose("TR0204", "floating data model",
                 "Core v2 requires IEEE binary32 float and binary64 double.",
                 "Select an admitted native target.");
      return;
    }
    const std::pair<const char *, QualType> Carriers[] = {
        {"bool", Context.BoolTy}, {"i8", Context.SignedCharTy},
        {"u8", Context.UnsignedCharTy}, {"i16", Context.ShortTy},
        {"u16", Context.UnsignedShortTy}, {"int", Context.IntTy},
        {"uint", Context.UnsignedIntTy}, {"i64", Context.LongLongTy},
        {"u64", Context.UnsignedLongLongTy}, {"default-pointer", Context.VoidPtrTy},
        {"float", Context.FloatTy}, {"double", Context.DoubleTy}};
    json::Object Layout{{"char_bits", Target.getCharWidth()}};
    for (const auto &[Name, T] : Carriers)
      Layout[Name] = json::Object{{"size_bits", Context.getTypeSize(T)},
                                   {"abi_align_bits", Context.getTypeAlign(T)}};
    TargetData["carrier_layout"] = std::move(Layout);
  }
  S.Module["target"] = std::move(TargetData);
  S.Module["records"] = std::move(RecordData);
  // Literal lvalues discovered in any function or cleanup helper have static
  // storage. Publish their complete definitions before emitting function code.
  for (auto &Global : StringGlobals)
    GlobalData.push_back(std::move(Global));
  for (auto &Global : StaticTemporaryGlobals)
    GlobalData.push_back(std::move(Global));
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
  std::vector<ExplicitMemberClassInstantiationSource> MemberClassDirectives;
  std::vector<TemplateUseSource> TemplateUses;
  std::vector<FunctionSpecializationSource> Specializations;
  std::vector<VariableTypeSource> VariableTypes;
  std::vector<SelectedTemplateCallSource> SelectedCalls;
  std::map<const TypeTraitExpr *, OperationTraitSource> OperationTraits;
  std::vector<FriendFunctionSource> FriendFunctions;
  std::vector<FriendDeclarationSource> FriendDeclarations;
  std::vector<FriendFunctionTemplateSource> FriendTemplates;
  std::vector<FunctionTemplateBodySource> TemplateBodies;
  std::vector<FriendClassTemplateSource> FriendClasses;
  std::size_t DirectiveUnits = 0;
  std::size_t ArrayFillerUnits = 0;
  std::set<const Expr *> SeparateArrayFillers;

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
      const TemplateArgumentList *Selection = nullptr,
      bool WrittenStorageClass = false, const NamedDecl *Origin = nullptr) {
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
    const auto *Full = dyn_cast_or_null<VarTemplateSpecializationDecl>(Declaration);
    const auto *ClassFull = dyn_cast_or_null<ClassTemplateSpecializationDecl>(Declaration);
    const bool PendingDeclaration = Kind == TemplateSourceKind::PartialDeclaration ||
        (Kind == TemplateSourceKind::ClassFullDeclaration && ClassFull && !Origin &&
         ClassFull->getKind() == Decl::ClassTemplateSpecialization &&
         ClassFull->isClassScopeExplicitSpecialization() &&
         ClassFull->getDeclContext()->isDependentContext() &&
         isa<CXXRecordDecl>(ClassFull->getDeclContext()) &&
         ClassFull->getLexicalDeclContext() == ClassFull->getDeclContext() &&
         !ClassFull->getInstantiatedFromMemberClass()) ||
        (Kind == TemplateSourceKind::VariableDeclaration && Full &&
         Full->getKind() == Decl::VarTemplateSpecialization && Full->isExplicitSpecialization() &&
         Full->getDeclContext()->isDependentContext() &&
         isa<CXXRecordDecl>(Full->getDeclContext()) &&
         Full->getLexicalDeclContext() == Full->getDeclContext());
    std::size_t Units = 2 * std::size_t(Count) + 3 * std::size_t(DefaultCount) +
                        3 * std::size_t(TypeCount) +
                        (Written ? Written->size() : 0);
    for (unsigned I = 0; I < Count; ++I) {
      if (Canonical[I].isNull() ||
          (!PendingDeclaration && Canonical[I].isInstantiationDependent()))
        return; // Ordinary uses still require fully resolved canonical arguments.
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
        {}, {}, Location, Overflow, Instantiation, Selection, WrittenStorageClass};
    Source.Origin = Origin;
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
  bool retainNeverCOperationTraitSource(
      ASTContext &Context, unsigned Count, const SourceLocation &Location) override {
    if (!S.coreV2() || !S.Diagnostics.empty() ||
        !S.owns(Context.getSourceManager(), Location))
      return false;
    if (Count > 65) {
      auto P = Context.getSourceManager().getPresumedLoc(Location);
      S.diagnose("TR0201", "operation trait source",
                 "Operation queries exceed the 64-argument construction limit.",
                 "Reduce the number of queried argument types.",
                 P.isValid() ? P.getLine() : 1, P.isValid() ? P.getColumn() : 1);
      return false;
    }
    return reserveSourceUnits(Context.getSourceManager(), Location, Count + 1);
  }
  void HandleNeverCOperationTraitSource(
      const TypeTraitExpr *Query, Expr *Root, Expr *const *Operands,
      unsigned Count, bool Attempted, bool Complete) override {
    if (!S.coreV2() || !S.Diagnostics.empty())
      return;
    OperationTraitSource Source{Root, {}, Attempted, Complete};
    for (unsigned I = 0; I < Count; ++I)
      Source.Operands.push_back(Operands[I]);
    if (!OperationTraits.emplace(Query, std::move(Source)).second)
      S.diagnose("TR0201", "operation trait source",
                 "One query has duplicate semantic source evidence.",
                 "Use a frontend with consistent query source ownership.");
  }
  NeverCArrayFillerAction HandleNeverCArrayFiller(
      ASTContext &Context, const Expr *Filler, unsigned long long Count,
      unsigned long long Extent) override {
    if (!S.coreV2())
      return NeverCArrayFillerAction::KeepShared;
    if (!S.Diagnostics.empty())
      return NeverCArrayFillerAction::Invalid;
    auto Reject = [&] {
      auto P = Filler ? Context.getSourceManager().getPresumedLoc(Filler->getExprLoc())
                      : PresumedLoc();
      S.diagnose("TR0201", "array filler source expansion",
                 "Separate array default initializers exceed the frontend source budget.",
                 "Reduce the array extent or default initializer complexity.",
                 P.isValid() ? P.getLine() : 1, P.isValid() ? P.getColumn() : 1);
      return NeverCArrayFillerAction::Invalid;
    };
    constexpr std::size_t Limit = 200000;
    std::size_t Nodes = 0;
    bool HasTemporary = false;
    auto Walk = [&](auto &&Self, const Stmt *Node, unsigned Depth) -> bool {
      if (!Node)
        return true;
      if (Depth > 64 || Nodes == Limit)
        return false;
      ++Nodes;
      HasTemporary |= isa<MaterializeTemporaryExpr>(Node);
      if (const auto *Default = dyn_cast<CXXDefaultInitExpr>(Node))
        return Self(Self, Default->getExpr(), Depth + 1);
      if (const auto *Default = dyn_cast<CXXDefaultArgExpr>(Node))
        return Self(Self, Default->getExpr(), Depth + 1);
      if (const auto *List = dyn_cast<InitListExpr>(Node)) {
        if (List->isSyntacticForm() && List->getSemanticForm())
          List = List->getSemanticForm();
        for (const auto *Init : List->inits())
          if (!Self(Self, Init, Depth + 1))
            return false;
        return !List->hasArrayFiller() ||
               Self(Self, List->getArrayFiller(), Depth + 1);
      }
      for (const auto *Child : Node->children())
        if (!Self(Self, Child, Depth + 1))
          return false;
      return true;
    };
    if (!Filler || !Count || Count > Extent || !Walk(Walk, Filler, 0))
      return Reject();
    if (!HasTemporary)
      return NeverCArrayFillerAction::KeepShared;
    // Even one omitted element needs an explicit semantic slot: Clang's later
    // outer-reference lifetime revisit does not traverse ArrayFiller. Reserve
    // before rebuilding, including nested/reentrant construction of new lists.
    if (Extent > 65536 || Count > (Limit - ArrayFillerUnits) / Nodes)
      return Reject();
    ArrayFillerUnits += std::size_t(Count) * Nodes;
    return NeverCArrayFillerAction::Separate;
  }
  void HandleNeverCArrayFillerElement(const Expr *Filler) override {
    if (S.coreV2() && S.Diagnostics.empty())
      SeparateArrayFillers.insert(Filler);
  }
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
  void HandleNeverCSelectedTemplateCallSource(
      Expr *Expression, FunctionDecl *Function,
      const SourceLocation &Location) override {
    if (!S.coreV2() || !Function || !Function->getPrimaryTemplate())
      return;
    auto &Sources = Function->getASTContext().getSourceManager();
    if (!S.owns(Sources, Location) || !reserveSourceUnits(Sources, Location, 2))
      return;
    // All admitted producer paths capture direct nodes before casts or
    // lifetime wrappers. Preserve invalid records for deterministic rejection.
    SelectedCalls.push_back({Expression, Function, Location});
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
    const bool GenericFull = Declaration && !Instantiation &&
        Declaration->getKind() == Decl::ClassTemplateSpecialization &&
        Declaration->isClassScopeExplicitSpecialization() &&
        Declaration->getDeclContext()->isDependentContext() &&
        isa<CXXRecordDecl>(Declaration->getDeclContext()) &&
        Declaration->getLexicalDeclContext() == Declaration->getDeclContext() &&
        !Declaration->getInstantiatedFromMemberClass();
    retainTemplateUse(GenericFull ? TemplateSourceKind::ClassFullDeclaration
                                  : TemplateSourceKind::ClassDeclaration,
                      Declaration ? Declaration->getSpecializedTemplate() : nullptr,
                      Declaration, nullptr, nullptr, &Written,
                      Canonical, Sugared, Count, DefaultParameters,
                      OriginalDefaults, ConvertedDefaults, DefaultCount,
                      TypeParameters, ParameterTypes, PackIndices, TypeCount, Overflow,
                      Location, Instantiation);
  }
  void HandleNeverCClassFullDeclarationSource(
      ClassTemplateSpecializationDecl *Declaration,
      ClassTemplateSpecializationDecl *Origin,
      const TemplateArgumentListInfo &Written,
      const TemplateArgument *Canonical, const TemplateArgument *Sugared, unsigned Count,
      NamedDecl *const *DefaultParameters, const TemplateArgumentLoc *OriginalDefaults,
      const TemplateArgumentLoc *ConvertedDefaults, unsigned DefaultCount,
      NamedDecl *const *TypeParameters, TypeSourceInfo *const *ParameterTypes,
      const unsigned *PackIndices, unsigned TypeCount,
      bool Overflow, const SourceLocation &Location) override {
    retainTemplateUse(TemplateSourceKind::ClassFullDeclaration,
                      Declaration ? Declaration->getSpecializedTemplate() : nullptr,
                      Declaration, nullptr, nullptr, &Written,
                      Canonical, Sugared, Count, DefaultParameters,
                      OriginalDefaults, ConvertedDefaults, DefaultCount,
                      TypeParameters, ParameterTypes, PackIndices, TypeCount, Overflow,
                      Location, false, nullptr, false, Origin);
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
  void HandleNeverCVariableTemplateSource(
      VarTemplateSpecializationDecl *Variable,
      const TemplateArgumentListInfo &Written, TypeSourceInfo *Type,
      bool Declaration, bool WrittenStorageClass,
      const TemplateArgument *Canonical, const TemplateArgument *Sugared, unsigned Count,
      NamedDecl *const *DefaultParameters, const TemplateArgumentLoc *OriginalDefaults,
      const TemplateArgumentLoc *ConvertedDefaults, unsigned DefaultCount,
      NamedDecl *const *TypeParameters, TypeSourceInfo *const *ParameterTypes,
      const unsigned *PackIndices, unsigned TypeCount, bool Overflow,
      const SourceLocation &Location) override {
    if (!Variable)
      return;
    retainTemplateUse(Declaration ? TemplateSourceKind::VariableDeclaration
                                  : TemplateSourceKind::VariableUse,
                      Variable->getSpecializedTemplate(), Variable, nullptr, Type,
                      &Written, Canonical, Sugared, Count, DefaultParameters,
                      OriginalDefaults, ConvertedDefaults, DefaultCount,
                      TypeParameters, ParameterTypes, PackIndices, TypeCount,
                      Overflow, Location, false, nullptr, WrittenStorageClass);
  }
  void HandleNeverCVariablePartialSource(
      VarTemplatePartialSpecializationDecl *Partial,
      const TemplateArgumentList *Selection, bool Pattern,
      const TemplateArgumentListInfo *Written,
      const TemplateArgument *Canonical, const TemplateArgument *Sugared, unsigned Count,
      NamedDecl *const *DefaultParameters, const TemplateArgumentLoc *OriginalDefaults,
      const TemplateArgumentLoc *ConvertedDefaults, unsigned DefaultCount,
      NamedDecl *const *TypeParameters, TypeSourceInfo *const *ParameterTypes,
      const unsigned *PackIndices, unsigned TypeCount, bool Overflow,
      const SourceLocation &Location) override {
    if (!Partial)
      return;
    NamedDecl *Owner = Pattern ? static_cast<NamedDecl *>(Partial->getSpecializedTemplate())
                              : static_cast<NamedDecl *>(Partial);
    retainTemplateUse(Pattern ? TemplateSourceKind::VariablePartialPattern
                              : TemplateSourceKind::VariablePartialDeduction,
                      Owner, Partial, nullptr, nullptr, Written,
                      Canonical, Sugared, Count, DefaultParameters,
                      OriginalDefaults, ConvertedDefaults, DefaultCount,
                      TypeParameters, ParameterTypes, PackIndices, TypeCount,
                      Overflow, Location, false, Selection);
  }
  void HandleNeverCPartialDeclarationSource(
      NamedDecl *Partial, NamedDecl *Origin, const TemplateArgumentListInfo &Written,
      const TemplateArgument *Canonical, const TemplateArgument *Sugared, unsigned Count,
      NamedDecl *const *DefaultParameters, const TemplateArgumentLoc *OriginalDefaults,
      const TemplateArgumentLoc *ConvertedDefaults, unsigned DefaultCount,
      NamedDecl *const *TypeParameters, TypeSourceInfo *const *ParameterTypes,
      const unsigned *PackIndices, unsigned TypeCount, bool Overflow,
      const SourceLocation &Location) override {
    NamedDecl *Primary = nullptr;
    if (auto *Class = dyn_cast_or_null<ClassTemplatePartialSpecializationDecl>(Partial))
      Primary = Class->getSpecializedTemplate();
    else if (auto *Variable = dyn_cast_or_null<VarTemplatePartialSpecializationDecl>(Partial))
      Primary = Variable->getSpecializedTemplate();
    if (!Primary)
      return;
    retainTemplateUse(TemplateSourceKind::PartialDeclaration, Primary, Partial,
                      nullptr, nullptr, &Written, Canonical, Sugared, Count,
                      DefaultParameters, OriginalDefaults, ConvertedDefaults, DefaultCount,
                      TypeParameters, ParameterTypes, PackIndices, TypeCount,
                      Overflow, Location, false, nullptr, false, Origin);
  }
  void HandleNeverCVariableTypeSource(
      VarTemplateSpecializationDecl *Variable, VarTemplateSpecializationDecl *Previous,
      VarDecl *Pattern, TypeSourceInfo *Type, bool Completion,
      const SourceLocation &Location) override {
    if (!S.coreV2() || !Variable || !Pattern || !Type || !S.Diagnostics.empty())
      return;
    auto &Sources = Variable->getASTContext().getSourceManager();
    if (!S.owns(Sources, Location) || !reserveSourceUnits(Sources, Location, 3))
      return;
    VariableTypes.push_back({Variable, Previous, Pattern, Type, Location, Completion});
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
  void HandleNeverCExplicitMemberClassInstantiation(
      CXXRecordDecl *Record, CXXRecordDecl *Origin,
      const NestedNameSpecifierLoc &Qualifier, const SourceLocation &Location,
      const SourceLocation &TemplateLocation, const SourceLocation &ExternLocation,
      bool HasAttributes) override {
    if (!S.coreV2() || !Record || !Origin || !S.Diagnostics.empty())
      return;
    auto &Sources = Record->getASTContext().getSourceManager();
    if (!S.owns(Sources, Location) || !reserveSourceUnits(Sources, Location, 0))
      return;
    MemberClassDirectives.push_back({Record, Origin, Qualifier, Location,
                                     TemplateLocation, ExternLocation, HasAttributes});
  }
  void HandleNeverCFriendFunctionSource(FunctionDecl *Function,
      FunctionDecl *Incoming, FunctionDecl *Selected, CXXRecordDecl *GrantingClass) override {
    if (!S.coreV2() || !Function || !Incoming || !Selected || !GrantingClass ||
        !S.Diagnostics.empty())
      return;
    auto &Sources = Function->getASTContext().getSourceManager();
    auto Location = Incoming->getLocation();
    if (!S.owns(Sources, Location) || !reserveSourceUnits(Sources, Location, 0))
      return;
    FriendFunctions.push_back({Function, Incoming, Selected, GrantingClass});
  }
  void HandleNeverCFriendDeclarationSource(FriendDecl *Declaration,
                                           FriendDecl *Written) override {
    if (!S.coreV2() || !Declaration || !Written || !S.Diagnostics.empty())
      return;
    auto &Sources = Declaration->getASTContext().getSourceManager();
    auto Location = Written->getFriendLoc();
    if (!S.owns(Sources, Location) || !reserveSourceUnits(Sources, Location, 0))
      return;
    FriendDeclarations.push_back({Declaration, Written});
  }
  void HandleNeverCFriendFunctionTemplateSource(FunctionTemplateDecl *Template,
      FunctionDecl *Incoming, FunctionDecl *Selected, CXXRecordDecl *GrantingClass) override {
    if (!S.coreV2() || !Template || !Incoming || !Selected || !GrantingClass ||
        !S.Diagnostics.empty())
      return;
    auto &Sources = Template->getASTContext().getSourceManager();
    auto Location = Incoming->getLocation();
    if (!S.owns(Sources, Location) || !reserveSourceUnits(Sources, Location, 0))
      return;
    FriendTemplates.push_back({Template, Incoming, Selected, GrantingClass});
  }
  void HandleNeverCFunctionTemplateBodySource(FunctionDecl *Function,
      FunctionTemplateDecl *Compatible, const FunctionDecl *Pattern,
      DeclContext *LexicalContext) override {
    if (!S.coreV2() || !Function || !Compatible || !Pattern || !LexicalContext ||
        !S.Diagnostics.empty())
      return;
    auto &Sources = Function->getASTContext().getSourceManager();
    auto Location = Pattern->getLocation();
    if (!S.owns(Sources, Location) || !reserveSourceUnits(Sources, Location, 0))
      return;
    TemplateBodies.push_back({Function, Compatible, Pattern, LexicalContext});
  }
  void HandleNeverCFriendClassTemplateSource(ClassTemplateDecl *Template,
      ClassTemplateDecl *Written, CXXRecordDecl *GrantingClass,
      DeclContext *Context, ClassTemplateDecl *Previous) override {
    if (!S.coreV2() || !Template || !Written || !GrantingClass || !Context ||
        !S.Diagnostics.empty())
      return;
    auto &Sources = Template->getASTContext().getSourceManager();
    auto Location = Written->getLocation();
    if (!S.owns(Sources, Location) || !reserveSourceUnits(Sources, Location, 0))
      return;
    FriendClasses.push_back({Template, Written, GrantingClass, Context, Previous});
  }
  void HandleTranslationUnit(ASTContext &C) override {
    if (!S.Diagnostics.empty() || C.getDiagnostics().hasErrorOccurred())
      return;
    Adapter A(S, C);
    A.SeparateArrayFillers = std::move(SeparateArrayFillers);
    A.OperationTraits = std::move(OperationTraits);
    try {
      A.run(Directives, StaticDirectives, TemplateUses, Specializations,
            VariableTypes, SelectedCalls, MemberClassDirectives,
            FriendFunctions, FriendDeclarations, FriendTemplates, TemplateBodies, FriendClasses);
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
       T.getArch() != llvm::Triple::x86_64 &&
       T.getArch() != llvm::Triple::x86) ||
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
                               "-Wc++26-extensions", "-Werror=writable-strings",
                               "-fno-ms-compatibility",
                               "-fno-ms-extensions",
                               "-fno-delayed-template-parsing",
                               "-fno-fast-math", "-ffp-contract=off"});
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
