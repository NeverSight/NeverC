//===- SemaDeclDeclaratorFunc.cpp - Function declarations --------------------===//

#include "SemaDeclUtils.h"
#include "Type/TypeLocBuilder.h"
#include "neverc/Analyze/Initialization.h"
#include "neverc/Analyze/ScopeInfo.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Builtin/BuiltinString.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/IncludeResolver.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Scan/SourceScanner.h"
#include "neverc/Tree/Core/CharUnits.h"
#include "neverc/Tree/Core/TreeConsumer.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <optional>
#include <unordered_map>

using namespace neverc;
using namespace sema;

// ===----------------------------------------------------------------------===
// Function declarations
// ===----------------------------------------------------------------------===

namespace neverc {

FunctionDecl *createNewFunctionDecl(Sema &SemaRef, Declarator &D,
                                    DeclContext *DC, QualType &R,
                                    TypeSourceInfo *TInfo, StorageClass SC) {
  DeclarationNameInfo NameInfo = SemaRef.GetNameForDeclarator(D);
  const DeclSpec &DS = D.getDeclSpec();

  FunctionDecl *NewFD = nullptr;
  bool isInline = DS.isInlineSpecified();

  // Determine whether the function was written with a prototype. This is
  // true when:
  //   - there is a prototype in the declarator, or
  //   - the type R of the function is some kind of typedef or other non-
  //     attributed reference to a type name (which eventually refers to a
  //     function type). Note, we can't always look at the adjusted type to
  //     check this case because attributes may cause a non-function
  //     declarator to still have a function type. e.g.,
  //       typedef void func(int a);
  //       __attribute__((noreturn)) func other_func; // This has a prototype
  bool HasPrototype =
      (D.isFunctionDeclarator() && D.getFunctionTypeInfo().hasPrototype) ||
      (DS.isTypeRep() && SemaRef.GetTypeFromParser(DS.getRepAsType(), nullptr)
                             ->isFunctionProtoType()) ||
      (!R->getAsAdjusted<FunctionType>() && R->isFunctionProtoType());
  assert((HasPrototype || !SemaRef.getLangOpts().requiresStrictPrototypes()) &&
         "Strict prototypes are required");

  NewFD = nullptr;
  if (SemaRef.getLangOpts().CPlusPlus) {
    bool IsExplicit = DS.isExplicitSpecified();
    bool IsVirtual = DS.isVirtualSpecified();
    ConstexprSpecKind CSK = DS.getConstexprSpecifier();
    if (auto *RD = dyn_cast<CXXRecordDecl>(DC)) {
      // Member function: ctor / dtor / conversion / method.
      DeclarationName Name = NameInfo.getName();
      IdentifierInfo *II = Name.getAsIdentifierInfo();
      if (II && II == RD->getIdentifier()) {
        // Constructor (name matches class name).
        NewFD = CXXConstructorDecl::Create(
            SemaRef.Context, RD, D.getBeginLoc(), NameInfo, R, TInfo,
            IsExplicit, isInline, /*isImplicitlyDeclared=*/false, CSK);
      } else if (II && II->getName().starts_with("~") &&
                 II->getName().drop_front() == RD->getName()) {
        NewFD = CXXDestructorDecl::Create(
            SemaRef.Context, RD, D.getBeginLoc(), NameInfo, R, TInfo, isInline,
            /*isImplicitlyDeclared=*/false);
      } else {
        NewFD = CXXMethodDecl::Create(
            SemaRef.Context, RD, D.getBeginLoc(), NameInfo, R, TInfo, SC,
            SemaRef.getCurFPFeatures().isFPConstrained(), isInline, CSK);
      }
      // Virtual methods make the class polymorphic (dynamic).
      if (auto *MD = dyn_cast_or_null<CXXMethodDecl>(NewFD)) {
        if (IsVirtual) {
          MD->setVirtualAsWritten(true);
          if (RD->hasDefinitionData() || RD->getDefinition()) {
            CXXRecordDecl *Def = RD->getDefinition() ? RD->getDefinition() : RD;
            if (!Def->hasDefinitionData())
              Def->startDefinition();
            Def->setPolymorphic(true);
          } else {
            // Ensure definition data exists on the current record.
            RD->startDefinition();
            RD->setPolymorphic(true);
          }
        }
      }
      (void)IsExplicit;
    }
  }
  if (!NewFD) {
    NewFD = FunctionDecl::Create(
        SemaRef.Context, DC, D.getBeginLoc(), NameInfo, R, TInfo, SC,
        SemaRef.getCurFPFeatures().isFPConstrained(), isInline, HasPrototype,
        ConstexprSpecKind::Unspecified);
  }
  if (D.isInvalidType())
    NewFD->setInvalidDecl();

  return NewFD;
}

} // namespace neverc


NamedDecl *Sema::OnFunctionDeclarator(Scope *S, Declarator &D, DeclContext *DC,
                                      TypeSourceInfo *TInfo,
                                      LookupResult &Previous,
                                      bool &AddToScope) {
  QualType R = TInfo->getType();

  assert(R->isFunctionType());

  const DeclSpec &FDS = D.getDeclSpec();
  StorageClass SC = getFunctionStorageClass(*this, D);

  if (DeclSpec::TSCS TSCS = FDS.getThreadStorageClassSpec())
    Diag(FDS.getThreadStorageClassSpecLoc(), diag::err_invalid_thread)
        << DeclSpec::getSpecifierName(TSCS);

  DeclContext *OriginalDC = DC;
  bool IsLocalExternDecl = adjustContextForLocalExternDecl(DC);

  FunctionDecl *NewFD = createNewFunctionDecl(*this, D, DC, R, TInfo, SC);
  if (!NewFD)
    return nullptr;

  // Lexical context may differ from semantic context for locals.
  NewFD->setLexicalDeclContext(CurContext);

  if (IsLocalExternDecl)
    NewFD->setLocalExternDecl();

  FilterLookupForScope(Previous, OriginalDC, S, shouldConsiderLinkage(NewFD));

  // Handle GNU asm-label extension (encoded as an attribute).
  if (Expr *E = (Expr *)D.getAsmLabel()) {
    // The parser guarantees this is a string.
    StringLiteral *SE = cast<StringLiteral>(E);
    NewFD->addAttr(AsmLabelAttr::Create(Context, SE->getString(),
                                        /*IsLiteralLabel=*/true,
                                        SE->getStrTokenLoc(0)));
  }

  // Copy the parameter declarations from the declarator D to the function
  // declaration NewFD, if they are available.  First scavenge them into Params.
  llvm::SmallVector<ParmVarDecl *, 16> Params;
  unsigned FTIIdx;
  if (D.isFunctionDeclarator(FTIIdx)) {
    DeclaratorChunk::FunctionTypeInfo &FTI = D.getTypeObject(FTIIdx).Fun;

    // Check for C99 6.7.5.3p10 - foo(void) is a non-varargs
    // function that takes no arguments, not a function that takes a
    // single void argument.
    // We let through "const void" here because Sema::ResolveDeclaratorType
    // already checks for that case.
    if (FTIHasNonVoidParameters(FTI) && FTI.Params[0].Param) {
      for (unsigned i = 0, e = FTI.NumParams; i != e; ++i) {
        ParmVarDecl *Param = cast<ParmVarDecl>(FTI.Params[i].Param);
        assert(Param->getDeclContext() != NewFD && "Was set before ?");
        Param->setDeclContext(NewFD);
        Params.push_back(Param);

        if (Param->isInvalidDecl())
          NewFD->setInvalidDecl();
      }
    }

    {
      // Find all the tag declarations from the prototype and move them
      // into the function DeclContext.
      DeclContext *PrototypeTagContext =
          getTagInjectionContext(NewFD->getLexicalDeclContext());
      for (NamedDecl *NonParmDecl : FTI.getDeclsInPrototype()) {
        auto *TD = dyn_cast<TagDecl>(NonParmDecl);

        // We don't want to reparent enumerators. Look at their parent enum
        // instead.
        if (!TD) {
          if (auto *ECD = dyn_cast<EnumConstantDecl>(NonParmDecl))
            TD = cast<EnumDecl>(ECD->getDeclContext());
        }
        if (!TD)
          continue;
        DeclContext *TagDC = TD->getLexicalDeclContext();
        if (!TagDC->containsDecl(TD))
          continue;
        TagDC->removeDecl(TD);
        TD->setDeclContext(NewFD);
        NewFD->addDecl(TD);

        // Preserve the lexical DeclContext if it is not the surrounding tag
        // injection context of the FD. In this example, the semantic context of
        // E will be f and the lexical context will be S, while both the
        // semantic and lexical contexts of S will be f:
        //   void f(struct S { enum E { a } f; } s);
        if (TagDC != PrototypeTagContext)
          TD->setLexicalDeclContext(TagDC);
      }
    }
  } else if (const FunctionProtoType *FT = R->getAs<FunctionProtoType>()) {
    // When we're declaring a function with a typedef, typeof, etc as in the
    // following example, we'll need to synthesize (unnamed)
    // parameters for use in the declaration.
    //
    // @code
    // typedef void fn(int);
    // fn f;
    // @endcode

    // Synthesize a parameter for each argument type.
    for (const auto &AI : FT->param_types()) {
      ParmVarDecl *Param =
          FormParmVarDeclForTypedef(NewFD, D.getIdentifierLoc(), AI);
      Param->setScopeInfo(0, Params.size());
      Params.push_back(Param);
    }
  } else {
    assert(R->isFunctionNoProtoType() && NewFD->getNumParams() == 0 &&
           "Should not need args for typedef of non-prototype fn");
  }

  // Finally, we know we have the right number of parameters, install them.
  NewFD->setParams(Params);
  if (D.isFunctionDefinition())
    for (ParmVarDecl *Param : Params)
      attachNeverCStringCleanup(*this, S, Param, NewFD,
                                /*IsFunctionDefinitionParam=*/true);

  if (FDS.isNoreturnSpecified())
    NewFD->addAttr(C11NoReturnAttr::Create(Context, FDS.getNoreturnSpecLoc()));

  // C++ function-specifiers that attach to the declaration (not only the type).
  if (getLangOpts().CPlusPlus) {
    if (auto *MD = dyn_cast<CXXMethodDecl>(NewFD)) {
      if (FDS.isVirtualSpecified()) {
        MD->setVirtualAsWritten(true);
        if (auto *RD = dyn_cast<CXXRecordDecl>(MD->getParent())) {
          if (!RD->hasDefinitionData())
            RD->startDefinition();
          RD->setPolymorphic(true);
        }
      }
    }
  }

  // Functions returning a variably modified type violate C99 6.7.5.2p2
  // because all functions have linkage.
  if (!NewFD->isInvalidDecl() &&
      NewFD->getReturnType()->isVariablyModifiedType()) {
    Diag(NewFD->getLocation(), diag::err_vm_func_decl);
    NewFD->setInvalidDecl();
  }

  // Apply an implicit SectionAttr if '#pragma neverc section text' is active
  if (PragmaTextSection.Valid && D.isFunctionDefinition() &&
      !NewFD->hasAttr<SectionAttr>())
    NewFD->addAttr(PragmaNeverCTextSectionAttr::CreateImplicit(
        Context, PragmaTextSection.SectionName,
        PragmaTextSection.PragmaLocation));

  // Apply an implicit SectionAttr if #pragma code_seg is active.
  if (CodeSegStack.CurrentValue && D.isFunctionDefinition() &&
      !NewFD->hasAttr<SectionAttr>()) {
    NewFD->addAttr(SectionAttr::CreateImplicit(
        Context, CodeSegStack.CurrentValue->getString(),
        CodeSegStack.CurrentPragmaLocation, SectionAttr::Declspec_allocate));
    if (UnifySection(CodeSegStack.CurrentValue->getString(),
                     TreeContext::PSF_Implicit | TreeContext::PSF_Execute |
                         TreeContext::PSF_Read,
                     NewFD))
      NewFD->dropAttr<SectionAttr>();
  }

  // Apply an implicit StrictGuardStackCheckAttr if #pragma strict_gs_check is
  // active.
  if (StrictGuardStackCheckStack.CurrentValue && D.isFunctionDefinition() &&
      !NewFD->hasAttr<StrictGuardStackCheckAttr>())
    NewFD->addAttr(StrictGuardStackCheckAttr::CreateImplicit(
        Context, PragmaTextSection.PragmaLocation));

  // Apply an implicit SectionAttr from #pragma code_seg if active.
  if (!NewFD->hasAttr<CodeSegAttr>()) {
    if (Attr *SAttr = getImplicitCodeSegOrSectionAttrForFunction(
            NewFD, D.isFunctionDefinition())) {
      NewFD->addAttr(SAttr);
    }
  }

  ApplyDeclAttributes(S, NewFD, D);
  const auto *NewTVA = NewFD->getAttr<TargetVersionAttr>();
  if (NewTVA && !NewTVA->isDefaultVersion() &&
      !Context.getTargetInfo().hasFeature("fmv")) {
    // Don't add to scope fmv functions declarations if fmv disabled
    AddToScope = false;
    return NewFD;
  }

  // Perform semantic checking on the function declaration.
  if (!NewFD->isInvalidDecl() && NewFD->isMain())
    CheckMain(NewFD, FDS);

  if (!NewFD->isInvalidDecl() && NewFD->isMSVCRTEntryPoint())
    CheckMSVCRTEntryPoint(NewFD);

  if (!NewFD->isInvalidDecl())
    D.setRedeclaration(
        CheckFunctionDeclaration(S, NewFD, Previous, D.isFunctionDefinition()));
  else if (!Previous.empty())
    // Recover gracefully from an invalid redeclaration.
    D.setRedeclaration(true);
  assert((NewFD->isInvalidDecl() || !D.isRedeclaration() ||
          Previous.getResultKind() != LookupResult::FoundOverloaded) &&
         "previous declaration set still overloaded");

  // Diagnose no-prototype function declarations with calling conventions that
  // don't support variadic calls. Only do this in C and do it after merging
  // possibly prototyped redeclarations.
  const FunctionType *FT = NewFD->getType()->castAs<FunctionType>();
  if (isa<FunctionNoProtoType>(FT) && !D.isFunctionDefinition()) {
    CallingConv CC = FT->getExtInfo().getCC();
    if (!supportsVariadicCall(CC)) {
      // Windows system headers sometimes accidentally use stdcall without
      // (void) parameters, so we relax this to a warning.
      int DiagID =
          CC == CC_X86StdCall ? diag::warn_cconv_knr : diag::err_cconv_knr;
      Diag(NewFD->getLocation(), DiagID)
          << FunctionType::getNameForCallConv(CC);
    }
  }

  // If this is the first declaration of a library builtin function, add
  // attributes as appropriate.
  if (!D.isRedeclaration()) {
    if (IdentifierInfo *II = Previous.getLookupName().getAsIdentifierInfo()) {
      if (unsigned BuiltinID = II->getBuiltinID()) {
        if (NewFD->getDeclContext()->getRedeclContext()->isFileContext()) {
          // Validate the type matches unless this builtin is specified as
          // matching regardless of its declared type.
          if (Context.BuiltinInfo.allowTypeMismatch(BuiltinID)) {
            NewFD->addAttr(BuiltinAttr::CreateImplicit(Context, BuiltinID));
          } else {
            TreeContext::GetBuiltinTypeError Error;
            QualType BuiltinType = Context.GetBuiltinType(BuiltinID, Error);

            if (!Error && !BuiltinType.isNull() &&
                Context.hasSameType(NewFD->getType(), BuiltinType))
              NewFD->addAttr(BuiltinAttr::CreateImplicit(Context, BuiltinID));
          }
        }
      }
    }
  }

  ProcessPragmaWeak(S, NewFD);
  checkAttributesAfterMerging(*this, *NewFD);

  AddKnownFunctionAttributes(NewFD);

  if (NewFD->hasAttr<OverloadableAttr>() &&
      !NewFD->getType()->getAs<FunctionProtoType>()) {
    Diag(NewFD->getLocation(), diag::err_attribute_overloadable_no_prototype)
        << NewFD;
    NewFD->dropAttr<OverloadableAttr>();
  }

  // If there's a #pragma GCC visibility in scope, and this isn't a struct or
  // union member, set the visibility of this function.
  if (!DC->isRecord() && NewFD->isExternallyVisible())
    AddPushedVisibilityAttribute(NewFD);

  // If this is a function definition, check if we have to apply any
  // attributes (i.e. optnone and no_builtin) due to a pragma.
  if (D.isFunctionDefinition()) {
    AddRangeBasedOptnone(NewFD);
    AddImplicitMSFunctionNoBuiltinAttr(NewFD);
    AddSectionMSAllocText(NewFD);
    ModifyFnAttributesMSPragmaOptimize(NewFD);
  }

  // If this is the first declaration of an extern "C" function, update the map.
  if (NewFD->isFirstDecl() && !NewFD->isInvalidDecl() && NewFD->isExternC())
    RegisterLocallyScopedExternCDecl(NewFD, S);

  NewFD->setRangeEnd(D.getSourceRange().getEnd());

  if (D.isRedeclaration() && !Previous.empty()) {
    NamedDecl *Prev = Previous.getRepresentativeDecl();
    checkDLLAttributeRedeclaration(*this, Prev, NewFD, false,
                                   D.isFunctionDefinition());
  }

  MarkUnusedFileScopedDecl(NewFD);

  // Diagnose availability attributes. Availability cannot be used on functions
  // that are run during load/unload.
  if (const auto *attr = NewFD->getAttr<AvailabilityAttr>()) {
    if (NewFD->hasAttr<ConstructorAttr>()) {
      Diag(attr->getLocation(), diag::warn_availability_on_static_initializer)
          << 0;
      NewFD->dropAttr<AvailabilityAttr>();
    }
    if (NewFD->hasAttr<DestructorAttr>()) {
      Diag(attr->getLocation(), diag::warn_availability_on_static_initializer)
          << 1;
      NewFD->dropAttr<AvailabilityAttr>();
    }
  }

#ifndef _WIN32
  // Diagnose no_builtin attribute on function declaration that are not a
  // definition.
  // We should really be doing this in
  // SemaDeclAttr.cpp::handleNoBuiltinAttr, unfortunately we only have access to
  // the FunctionDecl and at this point of the code
  // FunctionDecl::isThisDeclarationADefinition() which always returns `false`
  // because Sema::OnStartOfFunctionDef has not been called yet.
  if (const auto *NBA = NewFD->getAttr<NoBuiltinAttr>())
    switch (D.getFunctionDefinitionKind()) {
    case FunctionDefinitionKind::Declaration:
      Diag(NBA->getLocation(), diag::err_attribute_no_builtin_on_non_definition)
          << NBA->getSpelling();
      break;
    case FunctionDefinitionKind::Definition:
      break;
    }
#endif
  return NewFD;
}

Attr *Sema::getImplicitCodeSegOrSectionAttrForFunction(const FunctionDecl *FD,
                                                       bool IsDefinition) {
  if (!FD->hasAttr<SectionAttr>() && IsDefinition && CodeSegStack.CurrentValue)
    return SectionAttr::CreateImplicit(
        getTreeContext(), CodeSegStack.CurrentValue->getString(),
        CodeSegStack.CurrentPragmaLocation, SectionAttr::Declspec_allocate);
  return nullptr;
}

// ===----------------------------------------------------------------------===
// Entry-point checking
// ===----------------------------------------------------------------------===

void Sema::CheckMain(FunctionDecl *FD, const DeclSpec &DS) {
  // Hosted: function specifiers on main are restricted. static main warns,
  // inline main errors, _Noreturn main is accepted as extension.
  if (FD->getStorageClass() == SC_Static)
    Diag(DS.getStorageClassSpecLoc(), diag::warn_static_main)
        << FixItHint::CreateRemoval(DS.getStorageClassSpecLoc());
  if (FD->isInlineSpecified())
    Diag(DS.getInlineSpecLoc(), diag::err_inline_main)
        << FixItHint::CreateRemoval(DS.getInlineSpecLoc());
  if (DS.isNoreturnSpecified()) {
    SourceLocation NoreturnLoc = DS.getNoreturnSpecLoc();
    SourceRange NoreturnRange(NoreturnLoc, getLocForEndOfToken(NoreturnLoc));
    Diag(NoreturnLoc, diag::ext_noreturn_main);
    Diag(NoreturnLoc, diag::note_main_remove_noreturn)
        << FixItHint::CreateRemoval(NoreturnRange);
  }
  if (FD->isConstexpr()) {
    Diag(DS.getConstexprSpecLoc(), diag::err_constexpr_main)
        << FixItHint::CreateRemoval(DS.getConstexprSpecLoc());
    FD->setConstexprKind(ConstexprSpecKind::Unspecified);
  }

  QualType T = FD->getType();
  assert(T->isFunctionType() && "function decl is not of function type");
  const FunctionType *FT = T->castAs<FunctionType>();

  if (FT->getCallConv() != CC_C) {
    FT = Context.adjustFunctionType(FT, FT->getExtInfo().withCallingConv(CC_C));
    FD->setType(QualType(FT, 0));
    T = Context.getCanonicalType(FD->getType());
  }

  if (getLangOpts().GNUMode || getLangOpts().MSVCCompat) {
    // In C with GNU extensions/MSVC we allow main() to have non-integer return
    // type, but we should warn about the extension, and we disable the
    // implicit-return-zero rule.

    // GCC in C mode accepts qualified 'int'.
    if (Context.hasSameUnqualifiedType(FT->getReturnType(), Context.IntTy))
      FD->setHasImplicitReturnZero(true);
    else {
      Diag(FD->getTypeSpecStartLoc(), diag::ext_main_returns_nonint);
      SourceRange RTRange = FD->getReturnTypeSourceRange();
      if (RTRange.isValid())
        Diag(RTRange.getBegin(), diag::note_main_change_return_type)
            << FixItHint::CreateReplacement(
                   RTRange, tok::getKeywordSpelling(tok::kw_int));
    }
  } else {
    // C99 5.1.2.2.3: falling off the end of `main` returns 0.
    // All the standards say that main() should return 'int'.
    if (Context.hasSameType(FT->getReturnType(), Context.IntTy))
      FD->setHasImplicitReturnZero(true);
    else {
      // Otherwise, this is just a flat-out error.
      SourceRange RTRange = FD->getReturnTypeSourceRange();
      Diag(FD->getTypeSpecStartLoc(), diag::err_main_returns_nonint)
          << (RTRange.isValid()
                  ? FixItHint::CreateReplacement(
                        RTRange, tok::getKeywordSpelling(tok::kw_int))
                  : FixItHint());
      FD->setInvalidDecl(true);
    }
  }

  // Treat protoless main() as nullary.
  if (isa<FunctionNoProtoType>(FT))
    return;

  const FunctionProtoType *FTP = cast<const FunctionProtoType>(FT);
  unsigned nparams = FTP->getNumParams();
  assert(FD->getNumParams() == nparams);

  bool HasExtraParameters = (nparams > 3);

  if (FTP->isVariadic()) {
    Diag(FD->getLocation(), diag::ext_variadic_main);
  }

  // Darwin passes an undocumented fourth argument of type char**.  If
  // other platforms start sprouting these, the logic below will start
  // getting shifty.
  if (nparams == 4 && Context.getTargetInfo().getTriple().isOSDarwin())
    HasExtraParameters = false;

  if (HasExtraParameters) {
    Diag(FD->getLocation(), diag::err_main_surplus_args) << nparams;
    FD->setInvalidDecl(true);
    nparams = 3;
  }

  QualType CharPP =
      Context.getPointerType(Context.getPointerType(Context.CharTy));
  QualType Expected[] = {Context.IntTy, CharPP, CharPP, CharPP};

  for (unsigned i = 0; i < nparams; ++i) {
    QualType AT = FTP->getParamType(i);

    bool mismatch = true;

    if (Context.hasSameUnqualifiedType(AT, Expected[i]))
      mismatch = false;
    else if (Expected[i] == CharPP) {
      // As an extension, the following forms are okay:
      //   char const **
      //   char const * const *
      //   char * const *

      QualifierCollector qs;
      const PointerType *PT;
      if ((PT = qs.strip(AT)->getAs<PointerType>()) &&
          (PT = qs.strip(PT->getPointeeType())->getAs<PointerType>()) &&
          Context.hasSameType(QualType(qs.strip(PT->getPointeeType()), 0),
                              Context.CharTy)) {
        qs.removeConst();
        mismatch = !qs.empty();
      }
    }

    if (mismatch) {
      Diag(FD->getLocation(), diag::err_main_arg_wrong) << i << Expected[i];
      FD->setInvalidDecl(true);
    }
  }

  if (nparams == 1 && !FD->isInvalidDecl()) {
    Diag(FD->getLocation(), diag::warn_main_one_arg);
  }
}

void Sema::CheckMSVCRTEntryPoint(FunctionDecl *FD) {
  QualType T = FD->getType();
  assert(T->isFunctionType() && "function decl is not of function type");
  const FunctionType *FT = T->castAs<FunctionType>();

  // Set an implicit return of 'zero' if the function can return some integral,
  // enumeration, pointer or nullptr type.
  if (FT->getReturnType()->isIntegralOrEnumerationType() ||
      FT->getReturnType()->isAnyPointerType() ||
      FT->getReturnType()->isNullPtrType())
    // DllMain is exempt because a return value of zero means it failed.
    if (FD->getName() != "DllMain")
      FD->setHasImplicitReturnZero(true);

  // Default calling convention for MSVC entry points is __cdecl.
  if (!hasExplicitCallingConv(T) && FT->getCallConv() != CC_C) {
    FT = Context.adjustFunctionType(FT, FT->getExtInfo().withCallingConv(CC_C));
    FD->setType(QualType(FT, 0));
  }
}

