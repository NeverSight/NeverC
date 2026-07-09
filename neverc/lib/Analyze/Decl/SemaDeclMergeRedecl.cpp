//===- SemaDeclMergeRedecl.cpp - Function & variable declaration merging --===//

#include "SemaDeclUtils.h"
#include "Type/TypeLocBuilder.h"
#include "neverc/Analyze/Initialization.h"
#include "neverc/Analyze/ScopeInfo.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Builtin/BuiltinString.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Diagnostic/DiagnosticSema.h"
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

namespace {

struct GNUCompatibleParamWarning {
  ParmVarDecl *OldParm;
  ParmVarDecl *NewParm;
  QualType PromotedType;
};

template <typename T>
std::pair<diag::kind, SourceLocation>
getNoteDiagForInvalidRedeclaration(const T *Old, const T *New) {
  diag::kind PrevDiag;
  SourceLocation OldLocation = Old->getLocation();
  if (Old->isThisDeclarationADefinition())
    PrevDiag = diag::note_previous_definition;
  else if (Old->isImplicit()) {
    PrevDiag = diag::note_previous_implicit_declaration;
    if (const auto *FD = dyn_cast<FunctionDecl>(Old)) {
      if (FD->getBuiltinID())
        PrevDiag = diag::note_previous_builtin_declaration;
    }
    if (OldLocation.isInvalid())
      OldLocation = New->getLocation();
  } else
    PrevDiag = diag::note_previous_declaration;
  return std::make_pair(PrevDiag, OldLocation);
}

} // namespace

// ===----------------------------------------------------------------------===
// Function & variable declaration merging
// ===----------------------------------------------------------------------===

bool Sema::MergeFunctionDecl(FunctionDecl *New, NamedDecl *&OldD, Scope *S,
                             bool MergeTypeWithOld, bool NewDeclIsDefn) {
  FunctionDecl *Old = OldD->getAsFunction();
  if (!Old) {
    Diag(New->getLocation(), diag::err_redefinition_different_kind)
        << New->getDeclName();
    notePreviousDefinition(OldD, New->getLocation());
    return true;
  }

  // If the old declaration is invalid, just give up here.
  if (Old->isInvalidDecl())
    return true;

  // Disallow redeclaration of some builtins.
  if (!getTreeContext().canBuiltinBeRedeclared(Old)) {
    Diag(New->getLocation(), diag::err_builtin_redeclare) << Old->getDeclName();
    Diag(Old->getLocation(), diag::note_previous_builtin_declaration)
        << Old << Old->getType();
    return true;
  }

  diag::kind PrevDiag;
  SourceLocation OldLocation;
  std::tie(PrevDiag, OldLocation) =
      getNoteDiagForInvalidRedeclaration(Old, New);

  // Don't complain about this if we're in GNU89 mode and the old function
  // is an extern inline function.
  // Don't complain about specializations. They are not supposed to have
  // storage classes.
  if (New->getStorageClass() == SC_Static && Old->hasExternalFormalLinkage() &&
      !canRedefineFunction(Old, getLangOpts())) {
    if (getLangOpts().MicrosoftExt) {
      Diag(New->getLocation(), diag::ext_static_non_static) << New;
      Diag(OldLocation, PrevDiag) << Old << Old->getType();
    } else {
      Diag(New->getLocation(), diag::err_static_non_static) << New;
      Diag(OldLocation, PrevDiag) << Old << Old->getType();
      return true;
    }
  }

  if (const auto *ILA = New->getAttr<InternalLinkageAttr>())
    if (!Old->hasAttr<InternalLinkageAttr>()) {
      Diag(New->getLocation(), diag::err_attribute_missing_on_first_decl)
          << ILA;
      Diag(Old->getLocation(), diag::note_previous_declaration);
      New->dropAttr<InternalLinkageAttr>();
    }

  if (auto *EA = New->getAttr<ErrorAttr>()) {
    if (!Old->hasAttr<ErrorAttr>()) {
      Diag(EA->getLocation(), diag::err_attribute_missing_on_first_decl) << EA;
      Diag(Old->getLocation(), diag::note_previous_declaration);
      New->dropAttr<ErrorAttr>();
    }
  }

  {
    bool OldOvl = Old->hasAttr<OverloadableAttr>();
    if (OldOvl != New->hasAttr<OverloadableAttr>() && !Old->isImplicit()) {
      Diag(New->getLocation(), diag::err_attribute_overloadable_mismatch)
          << New << OldOvl;

      // Try our best to find a decl that actually has the overloadable
      // attribute for the note. In most cases (e.g. programs with only one
      // broken declaration/definition), this won't matter.
      //
      const Decl *DiagOld = Old;
      if (OldOvl) {
        auto OldIter = llvm::find_if(Old->redecls(), [](const Decl *D) {
          const auto *A = D->getAttr<OverloadableAttr>();
          return A && !A->isImplicit();
        });
        // If we've implicitly added *all* of the overloadable attrs to this
        // chain, emitting a "previous redecl" note is pointless.
        DiagOld = OldIter == Old->redecls_end() ? nullptr : *OldIter;
      }

      if (DiagOld)
        Diag(DiagOld->getLocation(),
             diag::note_attribute_overloadable_prev_overload)
            << OldOvl;

      if (OldOvl)
        New->addAttr(OverloadableAttr::CreateImplicit(Context));
      else
        New->dropAttr<OverloadableAttr>();
    }
  }

  // It is not permitted to redeclare an SME function with different SME
  // attributes.
  if (IsInvalidSMECallConversion(Old->getType(), New->getType(),
                                 AArch64SMECallConversionKind::MatchExactly)) {
    Diag(New->getLocation(), diag::err_sme_attr_mismatch)
        << New->getType() << Old->getType();
    Diag(OldLocation, diag::note_previous_declaration);
    return true;
  }

  // If a function is first declared with a calling convention, but is later
  // declared or defined without one, all following decls assume the calling
  // convention of the first.
  //
  // It's OK if a function is first declared without a calling convention,
  // but is later declared or defined with the default calling convention.
  //
  // To test if either decl has an explicit calling convention, we look for
  // AttributedType sugar nodes on the type as written.  If they are missing or
  // were canonicalized away, we assume the calling convention was implicit.
  //
  // Note also that we DO NOT return at this point, because we still have
  // other tests to run.
  QualType OldQType = Context.getCanonicalType(Old->getType());
  QualType NewQType = Context.getCanonicalType(New->getType());
  const FunctionType *OldType = cast<FunctionType>(OldQType);
  const FunctionType *NewType = cast<FunctionType>(NewQType);
  FunctionType::ExtInfo OldTypeInfo = OldType->getExtInfo();
  FunctionType::ExtInfo NewTypeInfo = NewType->getExtInfo();
  bool RequiresAdjustment = false;

  if (OldTypeInfo.getCC() != NewTypeInfo.getCC()) {
    FunctionDecl *First = Old->getFirstDecl();
    const FunctionType *FT =
        First->getType().getCanonicalType()->castAs<FunctionType>();
    FunctionType::ExtInfo FI = FT->getExtInfo();
    bool NewCCExplicit = getCallingConvAttributedType(New->getType());
    if (!NewCCExplicit) {
      // Inherit the CC from the previous declaration if it was specified
      // there but not here.
      NewTypeInfo = NewTypeInfo.withCallingConv(OldTypeInfo.getCC());
      RequiresAdjustment = true;
    } else if (Old->getBuiltinID()) {
      // Builtin attribute isn't propagated to the new one yet at this point,
      // so we check if the old one is a builtin.

      // Calling Conventions on a Builtin aren't really useful and setting a
      // default calling convention and cdecl'ing some builtin redeclarations is
      // common, so warn and ignore the calling convention on the redeclaration.
      Diag(New->getLocation(), diag::warn_cconv_unsupported)
          << FunctionType::getNameForCallConv(NewTypeInfo.getCC())
          << (int)CallingConventionIgnoredReason::BuiltinFunction;
      NewTypeInfo = NewTypeInfo.withCallingConv(OldTypeInfo.getCC());
      RequiresAdjustment = true;
    } else {
      // Calling conventions aren't compatible, so complain.
      bool FirstCCExplicit = getCallingConvAttributedType(First->getType());
      Diag(New->getLocation(), diag::err_cconv_change)
          << FunctionType::getNameForCallConv(NewTypeInfo.getCC())
          << !FirstCCExplicit
          << (!FirstCCExplicit ? ""
                               : FunctionType::getNameForCallConv(FI.getCC()));

      // Put the note on the first decl, since it is the one that matters.
      Diag(First->getLocation(), diag::note_previous_declaration);
      return true;
    }
  }

  if (OldTypeInfo.getNoReturn() && !NewTypeInfo.getNoReturn()) {
    NewTypeInfo = NewTypeInfo.withNoReturn(true);
    RequiresAdjustment = true;
  }

  // Merge regparm attribute.
  if (OldTypeInfo.getHasRegParm() != NewTypeInfo.getHasRegParm() ||
      OldTypeInfo.getRegParm() != NewTypeInfo.getRegParm()) {
    if (NewTypeInfo.getHasRegParm()) {
      Diag(New->getLocation(), diag::err_regparm_mismatch)
          << NewType->getRegParmType() << OldType->getRegParmType();
      Diag(OldLocation, diag::note_previous_declaration);
      return true;
    }

    NewTypeInfo = NewTypeInfo.withRegParm(OldTypeInfo.getRegParm());
    RequiresAdjustment = true;
  }

  if (OldTypeInfo.getNoCallerSavedRegs() !=
      NewTypeInfo.getNoCallerSavedRegs()) {
    if (NewTypeInfo.getNoCallerSavedRegs()) {
      AnyX86NoCallerSavedRegistersAttr *Attr =
          New->getAttr<AnyX86NoCallerSavedRegistersAttr>();
      Diag(New->getLocation(), diag::err_function_attribute_mismatch) << Attr;
      Diag(OldLocation, diag::note_previous_declaration);
      return true;
    }

    NewTypeInfo = NewTypeInfo.withNoCallerSavedRegs(true);
    RequiresAdjustment = true;
  }

  if (RequiresAdjustment) {
    const FunctionType *AdjustedType = New->getType()->getAs<FunctionType>();
    AdjustedType = Context.adjustFunctionType(AdjustedType, NewTypeInfo);
    New->setType(QualType(AdjustedType, 0));
    NewQType = Context.getCanonicalType(New->getType());
  }

  // If this redeclaration makes the function inline, we may need to add it to
  // UndefinedButUsed.
  if (!Old->isInlined() && New->isInlined() && !New->hasAttr<GNUInlineAttr>() &&
      !getLangOpts().GNUInline && Old->isUsed(false) && !Old->isDefined() &&
      !New->isThisDeclarationADefinition())
    UndefinedButUsed.insert(
        std::make_pair(Old->getCanonicalDecl(), SourceLocation()));

  // If this redeclaration makes it newly gnu_inline, we don't want to warn
  // about it.
  if (New->hasAttr<GNUInlineAttr>() && Old->isInlined() &&
      !Old->hasAttr<GNUInlineAttr>()) {
    UndefinedButUsed.erase(Old->getCanonicalDecl());
  }

  // If pass_object_size params don't match up perfectly, this isn't a valid
  // redeclaration.
  if (Old->getNumParams() > 0 && Old->getNumParams() == New->getNumParams() &&
      !hasIdenticalPassObjectSizeAttrs(Old, New)) {
    Diag(New->getLocation(), diag::err_different_pass_object_size_params)
        << New->getDeclName();
    Diag(OldLocation, PrevDiag) << Old << Old->getType();
    return true;
  }

  // C: Function types need to be compatible, not identical. This handles
  // duplicate function decls like "void f(int); void f(enum X);" properly.
  {
    // Prototype vs K&R definition: parameter counts must match and each
    // parameter type must be compatible after default argument promotions.
    // This cannot be handled by TreeContext::typesAreCompatible() because that
    // doesn't know whether the function type is for a definition or not when
    // eventually calling TreeContext::mergeFunctionTypes(). The only situation
    // we need to cover here is that the number of arguments agree as the
    // default argument promotion rules were already checked by
    // TreeContext::typesAreCompatible().
    if (Old->hasPrototype() && !New->hasWrittenPrototype() && NewDeclIsDefn &&
        Old->getNumParams() != New->getNumParams() && !Old->isImplicit()) {
      if (Old->hasInheritedPrototype())
        Old = Old->getCanonicalDecl();
      Diag(New->getLocation(), diag::err_conflicting_types) << New;
      Diag(Old->getLocation(), PrevDiag) << Old << Old->getType();
      return true;
    }

    // If we are merging two functions where only one of them has a prototype,
    // we may have enough information to decide to issue a diagnostic that the
    // function without a protoype will change behavior in C23. This handles
    // cases like:
    //   void i(); void i(int j);
    //   void i(int j); void i();
    //   void i(); void i(int j) {}
    // See OnFinishFunctionBody() for other cases of the behavior change
    // diagnostic. See GetFullTypeForDeclarator() for handling of a function
    // type without a prototype.
    if (New->hasWrittenPrototype() != Old->hasWrittenPrototype() &&
        !New->isImplicit() && !Old->isImplicit()) {
      const FunctionDecl *WithProto, *WithoutProto;
      if (New->hasWrittenPrototype()) {
        WithProto = New;
        WithoutProto = Old;
      } else {
        WithProto = Old;
        WithoutProto = New;
      }

      if (WithProto->getNumParams() != 0) {
        if (WithoutProto->getBuiltinID() == 0 && !WithoutProto->isImplicit()) {
          // The one without the prototype will be changing behavior in C23, so
          // warn about that one so long as it's a user-visible declaration.
          bool IsWithoutProtoADef = false, IsWithProtoADef = false;
          if (WithoutProto == New)
            IsWithoutProtoADef = NewDeclIsDefn;
          else
            IsWithProtoADef = NewDeclIsDefn;
          Diag(WithoutProto->getLocation(),
               diag::warn_non_prototype_changes_behavior)
              << IsWithoutProtoADef << (WithoutProto->getNumParams() ? 0 : 1)
              << (WithoutProto == Old) << IsWithProtoADef;

          // The reason the one without the prototype will be changing behavior
          // is because of the one with the prototype, so note that so long as
          // it's a user-visible declaration. There is one exception to this:
          // when the new declaration is a definition without a prototype, the
          // old declaration with a prototype is not the cause of the issue,
          // and that does not need to be noted because the one with a
          // prototype will not change behavior in C23.
          if (WithProto->getBuiltinID() == 0 && !WithProto->isImplicit() &&
              !IsWithoutProtoADef)
            Diag(WithProto->getLocation(), diag::note_conflicting_prototype);
        }
      }
    }

    if (Context.typesAreCompatible(OldQType, NewQType)) {
      const FunctionType *OldFuncType = OldQType->getAs<FunctionType>();
      const FunctionType *NewFuncType = NewQType->getAs<FunctionType>();
      const FunctionProtoType *OldProto = nullptr;
      if (MergeTypeWithOld && isa<FunctionNoProtoType>(NewFuncType) &&
          (OldProto = dyn_cast<FunctionProtoType>(OldFuncType))) {
        // The old declaration provided a function prototype, but the
        // new declaration does not. Merge in the prototype.
        assert(!OldProto->hasExceptionSpec() && "Exception spec in C");
        NewQType = Context.getFunctionType(NewFuncType->getReturnType(),
                                           OldProto->getParamTypes(),
                                           OldProto->getExtProtoInfo());
        New->setType(NewQType);
        New->setHasInheritedPrototype();

        // Synthesize parameters with the same types.
        llvm::SmallVector<ParmVarDecl *, 16> Params;
        for (const auto &ParamType : OldProto->param_types()) {
          ParmVarDecl *Param = ParmVarDecl::Create(
              Context, New, SourceLocation(), SourceLocation(), nullptr,
              ParamType, /*TInfo=*/nullptr, SC_None, nullptr);
          Param->setScopeInfo(0, Params.size());
          Param->setImplicit();
          Params.push_back(Param);
        }

        New->setParams(Params);
      }

      return MergeCompatibleFunctionDecls(New, Old, S, MergeTypeWithOld);
    }
  }

  // Check if the function types are compatible when pointer size address
  // spaces are ignored.
  if (Context.hasSameFunctionTypeIgnoringPtrSizes(OldQType, NewQType))
    return false;

  // GNU C permits a K&R definition to follow a prototype declaration
  // if the declared types of the parameters in the K&R definition
  // match the types in the prototype declaration, even when the
  // promoted types of the parameters from the K&R definition differ
  // from the types in the prototype. GCC then keeps the types from
  // the prototype.
  //
  // If a variadic prototype is followed by a non-variadic K&R definition,
  // Edge case: K&R definition of a previously prototyped variadic function.
  if (Old->hasPrototype() && !New->hasPrototype() &&
      New->getType()->getAs<FunctionProtoType>() &&
      Old->getNumParams() == New->getNumParams()) {
    llvm::SmallVector<QualType, 16> ArgTypes;
    llvm::SmallVector<GNUCompatibleParamWarning, 16> Warnings;
    const FunctionProtoType *OldProto =
        Old->getType()->getAs<FunctionProtoType>();
    const FunctionProtoType *NewProto =
        New->getType()->getAs<FunctionProtoType>();

    // Determine whether this is the GNU C extension.
    QualType MergedReturn = Context.mergeTypes(OldProto->getReturnType(),
                                               NewProto->getReturnType());
    bool LooseCompatible = !MergedReturn.isNull();
    for (unsigned Idx = 0, End = Old->getNumParams();
         LooseCompatible && Idx != End; ++Idx) {
      ParmVarDecl *OldParm = Old->getParamDecl(Idx);
      ParmVarDecl *NewParm = New->getParamDecl(Idx);
      if (Context.typesAreCompatible(OldParm->getType(),
                                     NewProto->getParamType(Idx))) {
        ArgTypes.push_back(NewParm->getType());
      } else if (Context.typesAreCompatible(OldParm->getType(),
                                            NewParm->getType(),
                                            /*CompareUnqualified=*/true)) {
        GNUCompatibleParamWarning Warn = {OldParm, NewParm,
                                          NewProto->getParamType(Idx)};
        Warnings.push_back(Warn);
        ArgTypes.push_back(NewParm->getType());
      } else
        LooseCompatible = false;
    }

    if (LooseCompatible) {
      for (unsigned Warn = 0; Warn < Warnings.size(); ++Warn) {
        Diag(Warnings[Warn].NewParm->getLocation(),
             diag::ext_param_promoted_not_compatible_with_prototype)
            << Warnings[Warn].PromotedType << Warnings[Warn].OldParm->getType();
        if (Warnings[Warn].OldParm->getLocation().isValid())
          Diag(Warnings[Warn].OldParm->getLocation(),
               diag::note_previous_declaration);
      }

      if (MergeTypeWithOld)
        New->setType(Context.getFunctionType(MergedReturn, ArgTypes,
                                             OldProto->getExtProtoInfo()));
      return MergeCompatibleFunctionDecls(New, Old, S, MergeTypeWithOld);
    }

    // Fall through to diagnose conflicting types.
  }

  // A function that has already been declared has been redeclared or
  // defined with a different type; show an appropriate diagnostic.

  // If the previous declaration was an implicitly-generated builtin
  // declaration, then at the very least we should use a specialized note.
  unsigned BuiltinID;
  if (Old->isImplicit() && (BuiltinID = Old->getBuiltinID())) {
    // If it's actually a library-defined builtin function like 'malloc'
    // or 'printf', just warn about the incompatible redeclaration.
    if (Context.BuiltinInfo.isPredefinedLibFunction(BuiltinID)) {
      Diag(New->getLocation(), diag::warn_redecl_library_builtin) << New;
      Diag(OldLocation, diag::note_previous_builtin_declaration)
          << Old << Old->getType();
      return false;
    }

    PrevDiag = diag::note_previous_builtin_declaration;
  }

  // Define a flag to determine whether merging MSVC compatible function
  // declarations is necessary.
  bool IsMergeRequiredForMSVC = false;

  // Check if Microsoft extensions or MSVC compatibility mode are enabled.
  if (getLangOpts().MicrosoftExt || getLangOpts().MSVCCompat)
    IsMergeRequiredForMSVC = true;

  // For Windows/COFF targets, merging is always required.
  const llvm::Triple &Triple = Context.getTargetInfo().getTriple();
  if (Triple.isOSWindows() || Triple.isOSBinFormatCOFF())
    IsMergeRequiredForMSVC = true;

  // Perform the merge if required.
  if (IsMergeRequiredForMSVC) {
    // Check and merge MSVC compatible function declarations.
    if (!MergeMSVCCompatibleFunctionDecls(New, Old, S, MergeTypeWithOld)) {
      // Return false if the merge was successful.
      return false;
    }
  }

  // The old declaration is Sema's own implicit definition
  // (`int f()` from ImplicitlyDefineFunction).  The authoritative
  // version is the one in the user's source, so silently replace
  // the old prototype with the new one.
  if (Old->isImplicit() ||
      Context.getSourceManager().isWrittenInBuiltinFile(OldLocation)) {
    New->setPreviousDecl(nullptr);
    return false;
  }

  Diag(New->getLocation(), diag::err_conflicting_types) << New->getDeclName();
  Diag(OldLocation, PrevDiag) << Old << Old->getType();
  return true;
}

bool Sema::MergeCompatibleFunctionDecls(FunctionDecl *New, FunctionDecl *Old,
                                        Scope *S, bool MergeTypeWithOld) {
  // Merge the attributes
  mergeDeclAttributes(New, Old);

  // Merge "used" flag.
  if (Old->getMostRecentDecl()->isUsed(false))
    New->setIsUsed();

  // Merge attributes from the parameters.  These can mismatch with K&R
  // declarations.
  if (New->getNumParams() == Old->getNumParams())
    for (unsigned i = 0, e = New->getNumParams(); i != e; ++i) {
      ParmVarDecl *NewParam = New->getParamDecl(i);
      ParmVarDecl *OldParam = Old->getParamDecl(i);
      mergeParamDeclAttributes(NewParam, OldParam, *this);
      mergeParamDeclTypes(NewParam, OldParam, *this);
    }

  // Merge the function types so the we get the composite types for the return
  // and argument types. Per C11 6.2.7/4, only update the type if the old decl
  // was visible.
  QualType Merged = Context.mergeTypes(Old->getType(), New->getType());
  if (!Merged.isNull() && MergeTypeWithOld)
    New->setType(Merged);

  return false;
}

// The same as MergeCompatibleFunctionDecls, but for MSVC-compatible
bool Sema::MergeMSVCCompatibleFunctionDecls(FunctionDecl *New,
                                            FunctionDecl *Old, Scope *S,
                                            bool MergeTypeWithOld) {
  QualType OldQType = Context.getCanonicalType(Old->getType());
  QualType NewQType = Context.getCanonicalType(New->getType());

  const FunctionType *OldFuncType = OldQType->getAs<FunctionType>();
  const FunctionType *NewFuncType = NewQType->getAs<FunctionType>();

  const FunctionProtoType *OldProto = nullptr;
  const FunctionProtoType *NewProto = nullptr;

  // If the types are not funtion types, do not merge the declarations.
  if (!((NewProto = dyn_cast<FunctionProtoType>(NewFuncType)) &&
        (OldProto = dyn_cast<FunctionProtoType>(OldFuncType))))
    return true;

  // If the parameter types are different, do not merge the declarations.
  if (OldProto->param_types().size() != NewProto->param_types().size())
    return true;

  bool CanMerge = false;
  if (OldProto->param_types().size() != 0) {
    for (size_t i = 0; i < OldProto->param_types().size(); ++i) {
      QualType OldParamType = OldProto->param_types()[i];
      QualType NewParamType = NewProto->param_types()[i];

      OldParamType.removeLocalConst();
      NewParamType.removeLocalConst();
      OldParamType.removeLocalVolatile();
      NewParamType.removeLocalVolatile();

      // If the parameter types are different, we need to check if we can
      // merge the declarations.
      if (OldParamType.getAsString() != NewParamType.getAsString()) {
        // If the parameter types are PointerType, we can merge the
        // declarations.
        if (isa<PointerType>(OldParamType) && isa<PointerType>(NewParamType)) {
          CanMerge = true;
          continue;
        }
        CanMerge = false;
        break;
      }
    }
  }
  if (!CanMerge)
    return true;

  if (MergeTypeWithOld) {
    assert(!OldProto->hasExceptionSpec() && "Exception spec in C");
    NewQType = Context.getFunctionType(NewFuncType->getReturnType(),
                                       OldProto->getParamTypes(),
                                       OldProto->getExtProtoInfo());
    New->setType(NewQType);

    // Both declarations have prototypes here (enforced by the
    // FunctionProtoType casts above), so New already has params from
    // ActOnFunctionDeclarator.  Update their types in-place to match
    // Old's prototype instead of re-synthesizing + re-calling
    // setParams (which would assert on the double-write).
    for (unsigned i = 0, e = New->getNumParams(); i != e; ++i)
      New->getParamDecl(i)->setType(OldProto->getParamType(i));
  }
  return MergeCompatibleFunctionDecls(New, Old, S, MergeTypeWithOld);
}

namespace {
LLVM_ATTRIBUTE_NOINLINE
void diagnoseVarDeclTypeMismatch(Sema &S, VarDecl *New, VarDecl *Old) {
  assert(!S.Context.hasSameType(New->getType(), Old->getType()));

  S.Diag(New->getLocation(), New->isThisDeclarationADefinition()
                                 ? diag::err_redefinition_different_type
                                 : diag::err_redeclaration_different_type)
      << New->getDeclName() << New->getType() << Old->getType();

  diag::kind PrevDiag;
  SourceLocation OldLocation;
  std::tie(PrevDiag, OldLocation) =
      getNoteDiagForInvalidRedeclaration(Old, New);
  S.Diag(OldLocation, PrevDiag) << Old << Old->getType();
  New->setInvalidDecl();
}
} // namespace

void Sema::MergeVarDeclTypes(VarDecl *New, VarDecl *Old,
                             bool MergeTypeWithOld) {
  if (New->isInvalidDecl() || Old->isInvalidDecl() ||
      New->getType()->containsErrors() || Old->getType()->containsErrors())
    return;

  QualType MergedT;
  MergedT = Context.mergeTypes(New->getType(), Old->getType());
  if (MergedT.isNull())
    return diagnoseVarDeclTypeMismatch(*this, New, Old);

  // Don't actually update the type on the new declaration if the old
  // declaration was an extern declaration in a different scope.
  if (MergeTypeWithOld)
    New->setType(MergedT);
}

namespace {
bool mergeTypeWithPrevious(Sema &S, VarDecl *NewVD, VarDecl *OldVD,
                           LookupResult &Previous) {
  // C11 6.2.7p4:
  //   For an identifier with internal or external linkage declared
  //   in a scope in which a prior declaration of that identifier is
  //   visible, if the prior declaration specifies internal or
  //   external linkage, the type of the identifier at the later
  //   declaration becomes the composite type.
  //
  // If the variable isn't visible, we do not merge with its type.
  if (Previous.isShadowed())
    return false;

  // If the old declaration was function-local, don't merge with its
  // type unless we're in the same function.
  return !OldVD->getLexicalDeclContext()->isFunctionOrMethod() ||
         OldVD->getLexicalDeclContext() == NewVD->getLexicalDeclContext();
}
} // namespace

void Sema::MergeVarDecl(VarDecl *New, LookupResult &Previous) {
  // If the new decl is already invalid, don't do any other checking.
  if (New->isInvalidDecl())
    return;

  if (!shouldLinkPossiblyHiddenDecl(Previous, New))
    return;

  VarDecl *Old = nullptr;
  if (Previous.isSingleResult())
    Old = dyn_cast<VarDecl>(Previous.getFoundDecl());
  if (!Old) {
    Diag(New->getLocation(), diag::err_redefinition_different_kind)
        << New->getDeclName();
    notePreviousDefinition(Previous.getRepresentativeDecl(),
                           New->getLocation());
    return New->setInvalidDecl();
  }

  mergeDeclAttributes(New, Old);
  // Warn if an already-declared variable is made a weak_import in a subsequent
  // declaration
  if (New->hasAttr<WeakImportAttr>() && Old->getStorageClass() == SC_None &&
      !Old->hasAttr<WeakImportAttr>()) {
    Diag(New->getLocation(), diag::warn_weak_import) << New->getDeclName();
    Diag(Old->getLocation(), diag::note_previous_declaration);
    // Remove weak_import attribute on new declaration.
    New->dropAttr<WeakImportAttr>();
  }

  if (const auto *ILA = New->getAttr<InternalLinkageAttr>())
    if (!Old->hasAttr<InternalLinkageAttr>()) {
      Diag(New->getLocation(), diag::err_attribute_missing_on_first_decl)
          << ILA;
      Diag(Old->getLocation(), diag::note_previous_declaration);
      New->dropAttr<InternalLinkageAttr>();
    }

  // Merge the types.
  VarDecl *MostRecent = Old->getMostRecentDecl();
  if (MostRecent != Old) {
    MergeVarDeclTypes(New, MostRecent,
                      mergeTypeWithPrevious(*this, New, MostRecent, Previous));
    if (New->isInvalidDecl())
      return;
  }

  MergeVarDeclTypes(New, Old, mergeTypeWithPrevious(*this, New, Old, Previous));
  if (New->isInvalidDecl())
    return;

  diag::kind PrevDiag;
  SourceLocation OldLocation;
  std::tie(PrevDiag, OldLocation) =
      getNoteDiagForInvalidRedeclaration(Old, New);

  // Static redeclaration after a declaration with external linkage (C11 6.2.2).
  if (New->getStorageClass() == SC_Static && Old->hasExternalFormalLinkage()) {
    if (getLangOpts().MicrosoftExt) {
      Diag(New->getLocation(), diag::ext_static_non_static)
          << New->getDeclName();
      Diag(OldLocation, PrevDiag);
    } else {
      Diag(New->getLocation(), diag::err_static_non_static)
          << New->getDeclName();
      Diag(OldLocation, PrevDiag);
      return New->setInvalidDecl();
    }
  }
  // C99 6.2.2p4:
  //   For an identifier declared with the storage-class specifier
  //   extern in a scope in which a prior declaration of that
  //   identifier is visible,23) if the prior declaration specifies
  //   internal or external linkage, the linkage of the identifier at
  //   the later declaration is the same as the linkage specified at
  //   the prior declaration. If no prior declaration is visible, or
  //   if the prior declaration specifies no linkage, then the
  //   identifier has external linkage.
  if (New->hasExternalStorage() && Old->hasLinkage())
    /* Okay */;
  else if (New->getCanonicalDecl()->getStorageClass() != SC_Static &&
           Old->getCanonicalDecl()->getStorageClass() == SC_Static) {
    Diag(New->getLocation(), diag::err_non_static_static) << New->getDeclName();
    Diag(OldLocation, PrevDiag);
    return New->setInvalidDecl();
  }

  // Check if extern is followed by non-extern and vice-versa.
  if (New->hasExternalStorage() && !Old->hasLinkage() &&
      Old->isLocalVarDeclOrParm()) {
    Diag(New->getLocation(), diag::err_extern_non_extern) << New->getDeclName();
    Diag(OldLocation, PrevDiag);
    return New->setInvalidDecl();
  }
  if (Old->hasLinkage() && New->isLocalVarDeclOrParm() &&
      !New->hasExternalStorage()) {
    Diag(New->getLocation(), diag::err_non_extern_extern) << New->getDeclName();
    Diag(OldLocation, PrevDiag);
    return New->setInvalidDecl();
  }

  // Variables with external linkage are analyzed in FinalizeDeclaratorGroup.

  if (!New->hasExternalStorage() && !New->isFileVarDecl()) {
    Diag(New->getLocation(), diag::err_redefinition) << New->getDeclName();
    Diag(OldLocation, PrevDiag);
    return New->setInvalidDecl();
  }

  if (New->isInline() && !Old->getMostRecentDecl()->isInline()) {
    if (VarDecl *Def = Old->getDefinition()) {
      // Definition cannot precede the first `inline` declaration (C23 inline
      // variables).
      Diag(New->getLocation(), diag::err_inline_decl_follows_def) << New;
      Diag(Def->getLocation(), diag::note_previous_definition);
    }
  }

  // If this redeclaration makes the variable inline, we may need to add it to
  // UndefinedButUsed.
  if (!Old->isInline() && New->isInline() && Old->isUsed(false) &&
      !Old->getDefinition() && !New->isThisDeclarationADefinition())
    UndefinedButUsed.insert(
        std::make_pair(Old->getCanonicalDecl(), SourceLocation()));

  if (New->getTLSKind() != Old->getTLSKind()) {
    if (!Old->getTLSKind()) {
      Diag(New->getLocation(), diag::err_thread_non_thread)
          << New->getDeclName();
      Diag(OldLocation, PrevDiag);
    } else if (!New->getTLSKind()) {
      Diag(New->getLocation(), diag::err_non_thread_thread)
          << New->getDeclName();
      Diag(OldLocation, PrevDiag);
    } else {
      // Do not allow redeclaration to change the variable between requiring
      // static and dynamic initialization.
      // GCC allows this, but uses the TLS keyword on the first
      // declaration to determine the kind.
      Diag(New->getLocation(), diag::err_thread_thread_different_kind)
          << New->getDeclName() << (New->getTLSKind() == VarDecl::TLS_Dynamic);
      Diag(OldLocation, PrevDiag);
    }
  }

  // Merge "used" flag.
  if (Old->getMostRecentDecl()->isUsed(false))
    New->setIsUsed();

  // Keep a chain of previous declarations.
  New->setPreviousDecl(Old);

  // Inherit access appropriately.
  New->setAccess(Old->getAccess());

  if (Old->isInline())
    New->setImplicitlyInline();
}

void Sema::notePreviousDefinition(const NamedDecl *Old, SourceLocation New) {
  SourceManager &SrcMgr = getSourceManager();
  auto FNewDecLoc = SrcMgr.getDecomposedLoc(New);
  auto FOldDecLoc = SrcMgr.getDecomposedLoc(Old->getLocation());
  auto *FNew = SrcMgr.getFileEntryForID(FNewDecLoc.first);
  auto FOld = SrcMgr.getFileEntryRefForID(FOldDecLoc.first);
  auto &HSI = PP.getIncludeResolver();
  llvm::StringRef HdrFilename =
      SrcMgr.getFilename(SrcMgr.getSpellingLoc(Old->getLocation()));

  auto noteFromInclude = [&](SourceLocation IncLoc) -> bool {
    if (IncLoc.isValid()) {
      Diag(IncLoc, diag::note_redefinition_include_same_file) << HdrFilename;
      return true;
    }
    return false;
  };

  // Is it the same file and same offset? Provide more information on why
  // this leads to a redefinition error.
  if (FNew == FOld && FNewDecLoc.second == FOldDecLoc.second) {
    SourceLocation OldIncLoc = SrcMgr.getIncludeLoc(FOldDecLoc.first);
    SourceLocation NewIncLoc = SrcMgr.getIncludeLoc(FNewDecLoc.first);
    bool EmittedDiag = noteFromInclude(OldIncLoc);
    EmittedDiag |= noteFromInclude(NewIncLoc);

    // If the header has no guards, emit a note suggesting one.
    if (FOld && !HSI.isFileMultipleIncludeGuarded(*FOld))
      Diag(Old->getLocation(), diag::note_use_ifdef_guards);

    if (EmittedDiag)
      return;
  }

  // Redefinition coming from different files or couldn't do better above.
  if (Old->getLocation().isValid())
    Diag(Old->getLocation(), diag::note_previous_definition);
}

bool Sema::checkVarDeclRedefinition(VarDecl *Old, VarDecl *New) {
  Diag(New->getLocation(), diag::err_redefinition) << New;
  notePreviousDefinition(Old, New->getLocation());
  New->setInvalidDecl();
  return true;
}

