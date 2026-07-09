//===- SemaDeclDeclaratorFuncMultiVersion.cpp - Multiversion function decls -===//

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
// Multiversion function checking
// ===----------------------------------------------------------------------===

namespace {
bool checkMultiVersionValue(Sema &S, const FunctionDecl *FD) {
  const auto *TA = FD->getAttr<TargetAttr>();
  const auto *TVA = FD->getAttr<TargetVersionAttr>();
  assert(
      (TA || TVA) &&
      "MultiVersion candidate requires a target or target_version attribute");
  const TargetInfo &TargetInfo = S.Context.getTargetInfo();
  enum ErrType { Feature = 0, Architecture = 1 };

  if (TA) {
    ParsedTargetAttr ParseInfo =
        S.getTreeContext().getTargetInfo().parseTargetAttr(
            TA->getFeaturesStr());
    if (!ParseInfo.CPU.empty() && !TargetInfo.validateCpuIs(ParseInfo.CPU)) {
      S.Diag(FD->getLocation(), diag::err_bad_multiversion_option)
          << Architecture << ParseInfo.CPU;
      return true;
    }
    for (const auto &Feat : ParseInfo.Features) {
      auto BareFeat = llvm::StringRef{Feat}.substr(1);
      if (Feat[0] == '-') {
        S.Diag(FD->getLocation(), diag::err_bad_multiversion_option)
            << Feature << ("no-" + BareFeat).str();
        return true;
      }

      if (!TargetInfo.validateCpuSupports(BareFeat) ||
          !TargetInfo.isValidFeatureName(BareFeat)) {
        S.Diag(FD->getLocation(), diag::err_bad_multiversion_option)
            << Feature << BareFeat;
        return true;
      }
    }
  }

  if (TVA) {
    llvm::SmallVector<llvm::StringRef, 8> Feats;
    TVA->getFeatures(Feats);
    for (const auto &Feat : Feats) {
      if (!TargetInfo.validateCpuSupports(Feat)) {
        S.Diag(FD->getLocation(), diag::err_bad_multiversion_option)
            << Feature << Feat;
        return true;
      }
    }
  }
  return false;
}
} // namespace

// Provide a white-list of attributes that are allowed to be combined with
// multiversion functions.
namespace {
bool attrCompatibleWithMultiVersion(attr::Kind Kind, MultiVersionKind MVKind) {
  // Note: this list/diagnosis must match the list in
  // checkMultiversionAttributesAllSame.
  switch (Kind) {
  default:
    return false;
  case attr::Used:
    return MVKind == MultiVersionKind::Target;
  case attr::NonNull:
  case attr::NoThrow:
    return true;
  }
}
} // namespace

namespace {
bool checkNonMultiVersionCompatAttributes(Sema &S, const FunctionDecl *FD,
                                          const FunctionDecl *CausedFD,
                                          MultiVersionKind MVKind) {
  const auto Diagnose = [FD, CausedFD, MVKind](Sema &S, const Attr *A) {
    S.Diag(FD->getLocation(), diag::err_multiversion_disallowed_other_attr)
        << static_cast<unsigned>(MVKind) << A;
    if (CausedFD)
      S.Diag(CausedFD->getLocation(), diag::note_multiversioning_caused_here);
    return true;
  };

  for (const Attr *A : FD->attrs()) {
    switch (A->getKind()) {
    case attr::CPUDispatch:
    case attr::CPUSpecific:
      if (MVKind != MultiVersionKind::CPUDispatch &&
          MVKind != MultiVersionKind::CPUSpecific)
        return Diagnose(S, A);
      break;
    case attr::Target:
      if (MVKind != MultiVersionKind::Target)
        return Diagnose(S, A);
      break;
    case attr::TargetVersion:
      if (MVKind != MultiVersionKind::TargetVersion)
        return Diagnose(S, A);
      break;
    case attr::TargetClones:
      if (MVKind != MultiVersionKind::TargetClones)
        return Diagnose(S, A);
      break;
    default:
      if (!attrCompatibleWithMultiVersion(A->getKind(), MVKind))
        return Diagnose(S, A);
      break;
    }
  }
  return false;
}
} // namespace

bool Sema::areMultiversionVariantFunctionsCompatible(
    const FunctionDecl *OldFD, const FunctionDecl *NewFD,
    const PartialDiagnostic &NoProtoDiagID,
    const PartialDiagnosticAt &NoteCausedDiagIDAt,
    const PartialDiagnosticAt &DiffDiagIDAt) {
  // Indices match err_multiversion_diff %select (see DiagnosticSemaKinds.td).
  enum Different {
    CallingConv = 0,
    ReturnType = 1,
    InlineSpec = 3,
    Linkage = 4,
    LanguageLinkage = 5,
  };

  if (NoProtoDiagID.getDiagID() != 0 && OldFD &&
      !OldFD->getType()->getAs<FunctionProtoType>()) {
    Diag(OldFD->getLocation(), NoProtoDiagID);
    Diag(NoteCausedDiagIDAt.first, NoteCausedDiagIDAt.second);
    return true;
  }

  if (NoProtoDiagID.getDiagID() != 0 &&
      !NewFD->getType()->getAs<FunctionProtoType>())
    return Diag(NewFD->getLocation(), NoProtoDiagID);

  QualType NewQType = Context.getCanonicalType(NewFD->getType());
  const auto *NewType = cast<FunctionType>(NewQType);
  QualType NewReturnType = NewType->getReturnType();

  // Ensure the return type is identical.
  if (OldFD) {
    QualType OldQType = Context.getCanonicalType(OldFD->getType());
    const auto *OldType = cast<FunctionType>(OldQType);
    FunctionType::ExtInfo OldTypeInfo = OldType->getExtInfo();
    FunctionType::ExtInfo NewTypeInfo = NewType->getExtInfo();

    if (OldTypeInfo.getCC() != NewTypeInfo.getCC())
      return Diag(DiffDiagIDAt.first, DiffDiagIDAt.second) << CallingConv;

    QualType OldReturnType = OldType->getReturnType();

    if (OldReturnType != NewReturnType)
      return Diag(DiffDiagIDAt.first, DiffDiagIDAt.second) << ReturnType;

    if (OldFD->isInlineSpecified() != NewFD->isInlineSpecified())
      return Diag(DiffDiagIDAt.first, DiffDiagIDAt.second) << InlineSpec;

    if (OldFD->getFormalLinkage() != NewFD->getFormalLinkage())
      return Diag(DiffDiagIDAt.first, DiffDiagIDAt.second) << Linkage;

    if (OldFD->isExternC() != NewFD->isExternC())
      return Diag(DiffDiagIDAt.first, DiffDiagIDAt.second) << LanguageLinkage;
  }
  return false;
}

namespace {
bool checkMultiVersionAdditionalRules(Sema &S, const FunctionDecl *OldFD,
                                      const FunctionDecl *NewFD, bool CausesMV,
                                      MultiVersionKind MVKind) {
  if (!S.getTreeContext().getTargetInfo().supportsMultiVersioning()) {
    S.Diag(NewFD->getLocation(), diag::err_multiversion_not_supported);
    if (OldFD)
      S.Diag(OldFD->getLocation(), diag::note_previous_declaration);
    return true;
  }

  if (CausesMV && OldFD &&
      checkNonMultiVersionCompatAttributes(S, OldFD, NewFD, MVKind))
    return true;

  if (checkNonMultiVersionCompatAttributes(S, NewFD, nullptr, MVKind))
    return true;

  // Only allow transition to MultiVersion if it hasn't been used.
  if (OldFD && CausesMV && OldFD->isUsed(false))
    return S.Diag(NewFD->getLocation(), diag::err_multiversion_after_used);

  return S.areMultiversionVariantFunctionsCompatible(
      OldFD, NewFD, S.PDiag(diag::err_multiversion_noproto),
      PartialDiagnosticAt(NewFD->getLocation(),
                          S.PDiag(diag::note_multiversioning_caused_here)),
      PartialDiagnosticAt(NewFD->getLocation(),
                          S.PDiag(diag::err_multiversion_diff)));
}
} // namespace

namespace {
bool checkMultiVersionFirstFunction(Sema &S, FunctionDecl *FD) {
  MultiVersionKind MVKind = FD->getMultiVersionKind();
  assert(MVKind != MultiVersionKind::None &&
         "Function lacks multiversion attribute");
  const auto *TA = FD->getAttr<TargetAttr>();
  const auto *TVA = FD->getAttr<TargetVersionAttr>();
  // Target and target_version only causes MV if it is default, otherwise this
  // is a normal function.
  if ((TA && !TA->isDefaultVersion()) || (TVA && !TVA->isDefaultVersion()))
    return false;

  if ((TA || TVA) && checkMultiVersionValue(S, FD)) {
    FD->setInvalidDecl();
    return true;
  }

  if (checkMultiVersionAdditionalRules(S, nullptr, FD, true, MVKind)) {
    FD->setInvalidDecl();
    return true;
  }

  FD->setIsMultiVersion();
  return false;
}
} // namespace

namespace {
bool previousDeclsHaveMultiVersionAttribute(const FunctionDecl *FD) {
  for (const Decl *D = FD->getPreviousDecl(); D; D = D->getPreviousDecl()) {
    if (D->getAsFunction()->getMultiVersionKind() != MultiVersionKind::None)
      return true;
  }

  return false;
}
} // namespace

namespace {
bool checkTargetCausesMultiVersioning(Sema &S, FunctionDecl *OldFD,
                                      FunctionDecl *NewFD, bool &Redeclaration,
                                      NamedDecl *&OldDecl,
                                      LookupResult &Previous) {
  const auto *NewTA = NewFD->getAttr<TargetAttr>();
  const auto *NewTVA = NewFD->getAttr<TargetVersionAttr>();
  const auto *OldTA = OldFD->getAttr<TargetAttr>();
  const auto *OldTVA = OldFD->getAttr<TargetVersionAttr>();
  // If the old decl is NOT MultiVersioned yet, and we don't cause that
  // to change, this is a simple redeclaration.
  if ((NewTA && !NewTA->isDefaultVersion() &&
       (!OldTA || OldTA->getFeaturesStr() == NewTA->getFeaturesStr())) ||
      (NewTVA && !NewTVA->isDefaultVersion() &&
       (!OldTVA || OldTVA->getName() == NewTVA->getName())))
    return false;

  // Otherwise, this decl causes MultiVersioning.
  if (checkMultiVersionAdditionalRules(S, OldFD, NewFD, true,
                                       NewTVA ? MultiVersionKind::TargetVersion
                                              : MultiVersionKind::Target)) {
    NewFD->setInvalidDecl();
    return true;
  }

  if (checkMultiVersionValue(S, NewFD)) {
    NewFD->setInvalidDecl();
    return true;
  }

  // If this is 'default', permit the forward declaration.
  if (!OldFD->isMultiVersion() &&
      ((NewTA && NewTA->isDefaultVersion() && !OldTA) ||
       (NewTVA && NewTVA->isDefaultVersion() && !OldTVA))) {
    Redeclaration = true;
    OldDecl = OldFD;
    OldFD->setIsMultiVersion();
    NewFD->setIsMultiVersion();
    return false;
  }

  if (checkMultiVersionValue(S, OldFD)) {
    S.Diag(NewFD->getLocation(), diag::note_multiversioning_caused_here);
    NewFD->setInvalidDecl();
    return true;
  }

  if (NewTA) {
    ParsedTargetAttr OldParsed =
        S.getTreeContext().getTargetInfo().parseTargetAttr(
            OldTA->getFeaturesStr());
    llvm::sort(OldParsed.Features);
    ParsedTargetAttr NewParsed =
        S.getTreeContext().getTargetInfo().parseTargetAttr(
            NewTA->getFeaturesStr());
    // Sort order doesn't matter, it just needs to be consistent.
    llvm::sort(NewParsed.Features);
    if (OldParsed == NewParsed) {
      S.Diag(NewFD->getLocation(), diag::err_multiversion_duplicate);
      S.Diag(OldFD->getLocation(), diag::note_previous_declaration);
      NewFD->setInvalidDecl();
      return true;
    }
  }

  if (NewTVA) {
    llvm::SmallVector<llvm::StringRef, 8> Feats;
    OldTVA->getFeatures(Feats);
    llvm::sort(Feats);
    llvm::SmallVector<llvm::StringRef, 8> NewFeats;
    NewTVA->getFeatures(NewFeats);
    llvm::sort(NewFeats);

    if (Feats == NewFeats) {
      S.Diag(NewFD->getLocation(), diag::err_multiversion_duplicate);
      S.Diag(OldFD->getLocation(), diag::note_previous_declaration);
      NewFD->setInvalidDecl();
      return true;
    }
  }

  for (const auto *FD : OldFD->redecls()) {
    const auto *CurTA = FD->getAttr<TargetAttr>();
    const auto *CurTVA = FD->getAttr<TargetVersionAttr>();
    // We allow forward declarations before ANY multiversioning attributes, but
    // nothing after the fact.
    if (previousDeclsHaveMultiVersionAttribute(FD) &&
        ((NewTA && (!CurTA || CurTA->isInherited())) ||
         (NewTVA && (!CurTVA || CurTVA->isInherited())))) {
      S.Diag(FD->getLocation(), diag::err_multiversion_required_in_redecl)
          << (NewTA ? 0 : 2);
      S.Diag(NewFD->getLocation(), diag::note_multiversioning_caused_here);
      NewFD->setInvalidDecl();
      return true;
    }
  }

  OldFD->setIsMultiVersion();
  NewFD->setIsMultiVersion();
  Redeclaration = false;
  OldDecl = nullptr;
  Previous.clear();
  return false;
}
} // namespace

namespace {
bool multiVersionTypesCompatible(MultiVersionKind Old, MultiVersionKind New) {
  if (Old == New || Old == MultiVersionKind::None ||
      New == MultiVersionKind::None)
    return true;

  return (Old == MultiVersionKind::CPUDispatch &&
          New == MultiVersionKind::CPUSpecific) ||
         (Old == MultiVersionKind::CPUSpecific &&
          New == MultiVersionKind::CPUDispatch);
}
} // namespace

namespace {
bool checkMultiVersionAdditionalDecl(
    Sema &S, FunctionDecl *OldFD, FunctionDecl *NewFD,
    MultiVersionKind NewMVKind, const CPUDispatchAttr *NewCPUDisp,
    const CPUSpecificAttr *NewCPUSpec, const TargetClonesAttr *NewClones,
    bool &Redeclaration, NamedDecl *&OldDecl, LookupResult &Previous) {
  const auto *NewTA = NewFD->getAttr<TargetAttr>();
  const auto *NewTVA = NewFD->getAttr<TargetVersionAttr>();
  MultiVersionKind OldMVKind = OldFD->getMultiVersionKind();
  // Disallow mixing of multiversioning types.
  if (!multiVersionTypesCompatible(OldMVKind, NewMVKind)) {
    S.Diag(NewFD->getLocation(), diag::err_multiversion_types_mixed);
    S.Diag(OldFD->getLocation(), diag::note_previous_declaration);
    NewFD->setInvalidDecl();
    return true;
  }

  ParsedTargetAttr NewParsed;
  if (NewTA) {
    NewParsed = S.getTreeContext().getTargetInfo().parseTargetAttr(
        NewTA->getFeaturesStr());
    llvm::sort(NewParsed.Features);
  }
  llvm::SmallVector<llvm::StringRef, 8> NewFeats;
  if (NewTVA) {
    NewTVA->getFeatures(NewFeats);
    llvm::sort(NewFeats);
  }

  bool MayNeedOverloadableChecks =
      canOverloadFunction(Previous, S.Context, NewFD);

  // Next, check ALL non-invalid non-overloads to see if this is a redeclaration
  // of a previous member of the MultiVersion set.
  for (NamedDecl *ND : Previous) {
    FunctionDecl *CurFD = ND->getAsFunction();
    if (!CurFD || CurFD->isInvalidDecl())
      continue;
    if (MayNeedOverloadableChecks && S.IsOverload(NewFD, CurFD))
      continue;

    if (NewMVKind == MultiVersionKind::None &&
        OldMVKind == MultiVersionKind::TargetVersion) {
      NewFD->addAttr(TargetVersionAttr::CreateImplicit(
          S.Context, "default", NewFD->getSourceRange()));
      NewFD->setIsMultiVersion();
      NewMVKind = MultiVersionKind::TargetVersion;
      if (!NewTVA) {
        NewTVA = NewFD->getAttr<TargetVersionAttr>();
        NewTVA->getFeatures(NewFeats);
        llvm::sort(NewFeats);
      }
    }

    switch (NewMVKind) {
    case MultiVersionKind::None:
      assert(OldMVKind == MultiVersionKind::TargetClones &&
             "Only target_clones can be omitted in subsequent declarations");
      break;
    case MultiVersionKind::Target: {
      const auto *CurTA = CurFD->getAttr<TargetAttr>();
      if (CurTA->getFeaturesStr() == NewTA->getFeaturesStr()) {
        NewFD->setIsMultiVersion();
        Redeclaration = true;
        OldDecl = ND;
        return false;
      }

      ParsedTargetAttr CurParsed =
          S.getTreeContext().getTargetInfo().parseTargetAttr(
              CurTA->getFeaturesStr());
      llvm::sort(CurParsed.Features);
      if (CurParsed == NewParsed) {
        S.Diag(NewFD->getLocation(), diag::err_multiversion_duplicate);
        S.Diag(CurFD->getLocation(), diag::note_previous_declaration);
        NewFD->setInvalidDecl();
        return true;
      }
      break;
    }
    case MultiVersionKind::TargetVersion: {
      const auto *CurTVA = CurFD->getAttr<TargetVersionAttr>();
      if (CurTVA->getName() == NewTVA->getName()) {
        NewFD->setIsMultiVersion();
        Redeclaration = true;
        OldDecl = ND;
        return false;
      }
      llvm::SmallVector<llvm::StringRef, 8> CurFeats;
      if (CurTVA) {
        CurTVA->getFeatures(CurFeats);
        llvm::sort(CurFeats);
      }
      if (CurFeats == NewFeats) {
        S.Diag(NewFD->getLocation(), diag::err_multiversion_duplicate);
        S.Diag(CurFD->getLocation(), diag::note_previous_declaration);
        NewFD->setInvalidDecl();
        return true;
      }
      break;
    }
    case MultiVersionKind::TargetClones: {
      const auto *CurClones = CurFD->getAttr<TargetClonesAttr>();
      Redeclaration = true;
      OldDecl = CurFD;
      NewFD->setIsMultiVersion();

      if (CurClones && NewClones &&
          (CurClones->featuresStrs_size() != NewClones->featuresStrs_size() ||
           !std::equal(CurClones->featuresStrs_begin(),
                       CurClones->featuresStrs_end(),
                       NewClones->featuresStrs_begin()))) {
        S.Diag(NewFD->getLocation(), diag::err_target_clone_doesnt_match);
        S.Diag(CurFD->getLocation(), diag::note_previous_declaration);
        NewFD->setInvalidDecl();
        return true;
      }

      return false;
    }
    case MultiVersionKind::CPUSpecific:
    case MultiVersionKind::CPUDispatch: {
      const auto *CurCPUSpec = CurFD->getAttr<CPUSpecificAttr>();
      const auto *CurCPUDisp = CurFD->getAttr<CPUDispatchAttr>();
      // Handle CPUDispatch/CPUSpecific versions.
      // Only 1 CPUDispatch function is allowed, this will make it go through
      // the redeclaration errors.
      if (NewMVKind == MultiVersionKind::CPUDispatch &&
          CurFD->hasAttr<CPUDispatchAttr>()) {
        if (CurCPUDisp->cpus_size() == NewCPUDisp->cpus_size() &&
            std::equal(
                CurCPUDisp->cpus_begin(), CurCPUDisp->cpus_end(),
                NewCPUDisp->cpus_begin(),
                [](const IdentifierInfo *Cur, const IdentifierInfo *New) {
                  return Cur->getName() == New->getName();
                })) {
          NewFD->setIsMultiVersion();
          Redeclaration = true;
          OldDecl = ND;
          return false;
        }

        // If the declarations don't match, this is an error condition.
        S.Diag(NewFD->getLocation(), diag::err_cpu_dispatch_mismatch);
        S.Diag(CurFD->getLocation(), diag::note_previous_declaration);
        NewFD->setInvalidDecl();
        return true;
      }
      if (NewMVKind == MultiVersionKind::CPUSpecific && CurCPUSpec) {
        if (CurCPUSpec->cpus_size() == NewCPUSpec->cpus_size() &&
            std::equal(
                CurCPUSpec->cpus_begin(), CurCPUSpec->cpus_end(),
                NewCPUSpec->cpus_begin(),
                [](const IdentifierInfo *Cur, const IdentifierInfo *New) {
                  return Cur->getName() == New->getName();
                })) {
          NewFD->setIsMultiVersion();
          Redeclaration = true;
          OldDecl = ND;
          return false;
        }

        // Only 1 version of CPUSpecific is allowed for each CPU.
        for (const IdentifierInfo *CurII : CurCPUSpec->cpus()) {
          for (const IdentifierInfo *NewII : NewCPUSpec->cpus()) {
            if (CurII == NewII) {
              S.Diag(NewFD->getLocation(), diag::err_cpu_specific_multiple_defs)
                  << NewII;
              S.Diag(CurFD->getLocation(), diag::note_previous_declaration);
              NewFD->setInvalidDecl();
              return true;
            }
          }
        }
      }
      break;
    }
    }
  }

  // Else, this is simply a non-redecl case.  Checking the 'value' is only
  // necessary in the Target case, since The CPUSpecific/Dispatch cases are
  // handled in the attribute adding step.
  if ((NewMVKind == MultiVersionKind::TargetVersion ||
       NewMVKind == MultiVersionKind::Target) &&
      checkMultiVersionValue(S, NewFD)) {
    NewFD->setInvalidDecl();
    return true;
  }

  if (checkMultiVersionAdditionalRules(S, OldFD, NewFD,
                                       !OldFD->isMultiVersion(), NewMVKind)) {
    NewFD->setInvalidDecl();
    return true;
  }

  // Permit forward declarations in the case where these two are compatible.
  if (!OldFD->isMultiVersion()) {
    OldFD->setIsMultiVersion();
    NewFD->setIsMultiVersion();
    Redeclaration = true;
    OldDecl = OldFD;
    return false;
  }

  NewFD->setIsMultiVersion();
  Redeclaration = false;
  OldDecl = nullptr;
  Previous.clear();
  return false;
}
} // namespace

namespace {
bool checkMultiVersionFunction(Sema &S, FunctionDecl *NewFD,
                               bool &Redeclaration, NamedDecl *&OldDecl,
                               LookupResult &Previous) {
  const auto *NewTA = NewFD->getAttr<TargetAttr>();
  const auto *NewTVA = NewFD->getAttr<TargetVersionAttr>();
  const auto *NewCPUDisp = NewFD->getAttr<CPUDispatchAttr>();
  const auto *NewCPUSpec = NewFD->getAttr<CPUSpecificAttr>();
  const auto *NewClones = NewFD->getAttr<TargetClonesAttr>();
  MultiVersionKind MVKind = NewFD->getMultiVersionKind();

  // Main isn't allowed to become a multiversion function, however it IS
  // permitted to have 'main' be marked with the 'target' optimization hint,
  // for 'target_version' only default is allowed.
  if (NewFD->isMain()) {
    if (MVKind != MultiVersionKind::None &&
        !(MVKind == MultiVersionKind::Target && !NewTA->isDefaultVersion()) &&
        !(MVKind == MultiVersionKind::TargetVersion &&
          NewTVA->isDefaultVersion())) {
      S.Diag(NewFD->getLocation(), diag::err_multiversion_not_allowed_on_main);
      NewFD->setInvalidDecl();
      return true;
    }
    return false;
  }

  // Target attribute on AArch64 is not used for multiversioning
  if (NewTA && S.getTreeContext().getTargetInfo().getTriple().isAArch64())
    return false;

  if (!OldDecl || !OldDecl->getAsFunction() ||
      OldDecl->getDeclContext()->getRedeclContext() !=
          NewFD->getDeclContext()->getRedeclContext()) {
    // If there's no previous declaration, AND this isn't attempting to cause
    // multiversioning, this isn't an error condition.
    if (MVKind == MultiVersionKind::None)
      return false;
    return checkMultiVersionFirstFunction(S, NewFD);
  }

  FunctionDecl *OldFD = OldDecl->getAsFunction();

  if (!OldFD->isMultiVersion() && MVKind == MultiVersionKind::None) {
    if (NewTVA || !OldFD->getAttr<TargetVersionAttr>())
      return false;
    if (!NewFD->getType()->getAs<FunctionProtoType>()) {
      // Multiversion declaration doesn't have prototype.
      S.Diag(NewFD->getLocation(), diag::err_multiversion_noproto);
      NewFD->setInvalidDecl();
    } else {
      // No "target_version" attribute is equivalent to "default" attribute.
      NewFD->addAttr(TargetVersionAttr::CreateImplicit(
          S.Context, "default", NewFD->getSourceRange()));
      NewFD->setIsMultiVersion();
      OldFD->setIsMultiVersion();
      OldDecl = OldFD;
      Redeclaration = true;
    }
    return true;
  }

  // Multiversioned redeclarations aren't allowed to omit the attribute, except
  // for target_clones and target_version.
  if (OldFD->isMultiVersion() && MVKind == MultiVersionKind::None &&
      OldFD->getMultiVersionKind() != MultiVersionKind::TargetClones &&
      OldFD->getMultiVersionKind() != MultiVersionKind::TargetVersion) {
    S.Diag(NewFD->getLocation(), diag::err_multiversion_required_in_redecl)
        << (OldFD->getMultiVersionKind() != MultiVersionKind::Target);
    NewFD->setInvalidDecl();
    return true;
  }

  if (!OldFD->isMultiVersion()) {
    switch (MVKind) {
    case MultiVersionKind::Target:
    case MultiVersionKind::TargetVersion:
      return checkTargetCausesMultiVersioning(S, OldFD, NewFD, Redeclaration,
                                              OldDecl, Previous);
    case MultiVersionKind::TargetClones:
      if (OldFD->isUsed(false)) {
        NewFD->setInvalidDecl();
        return S.Diag(NewFD->getLocation(), diag::err_multiversion_after_used);
      }
      OldFD->setIsMultiVersion();
      break;

    case MultiVersionKind::CPUDispatch:
    case MultiVersionKind::CPUSpecific:
    case MultiVersionKind::None:
      break;
    }
  }

  // At this point, we have a multiversion function decl (in OldFD) AND an
  // appropriate attribute in the current function decl.  Resolve that these are
  // still compatible with previous declarations.
  return checkMultiVersionAdditionalDecl(S, OldFD, NewFD, MVKind, NewCPUDisp,
                                         NewCPUSpec, NewClones, Redeclaration,
                                         OldDecl, Previous);
}
} // namespace

bool Sema::CheckFunctionDeclaration(Scope *S, FunctionDecl *NewFD,
                                    LookupResult &Previous, bool DeclIsDefn) {
  assert(!NewFD->getReturnType()->isVariablyModifiedType() &&
         "Variably modified return types are not handled here");

  // When lookup is not shadowed, merge the function type with the previous
  // visible declaration.
  bool MergeTypeWithPrevious = !Previous.isShadowed();

  bool Redeclaration = false;
  NamedDecl *OldDecl = nullptr;
  bool MayNeedOverloadableChecks = false;

  // Merge or overload the declaration with an existing declaration of
  // the same name, if appropriate.
  if (!Previous.empty()) {
    // Determine whether NewFD is an overload of PrevDecl or
    // a declaration that requires merging. If it's an overload,
    // there's no more work to do here; we'll just add the new
    // function to the scope.
    if (!canOverloadFunction(Previous, Context, NewFD)) {
      NamedDecl *Candidate = Previous.getRepresentativeDecl();
      if (shouldLinkPossiblyHiddenDecl(Candidate, NewFD)) {
        Redeclaration = true;
        OldDecl = Candidate;
      }
    } else {
      MayNeedOverloadableChecks = true;
      switch (CheckOverload(NewFD, Previous, OldDecl)) {
      case Ovl_Match:
        Redeclaration = true;
        break;

      case Ovl_NonFunction:
        Redeclaration = true;
        break;

      case Ovl_Overload:
        Redeclaration = false;
        break;
      }
    }
  }

  // Check for a previous extern "C" declaration with this name.
  if (!Redeclaration &&
      checkForConflictWithNonVisibleExternC(*this, NewFD, Previous)) {
    if (!Previous.empty()) {
      // This is an extern "C" declaration with the same name as a previous
      // declaration, and thus redeclares that entity...
      Redeclaration = true;
      OldDecl = Previous.getFoundDecl();
      MergeTypeWithPrevious = false;

      // ... except in the presence of __attribute__((overloadable)).
      if (OldDecl->hasAttr<OverloadableAttr>() ||
          NewFD->hasAttr<OverloadableAttr>()) {
        if (IsOverload(NewFD, cast<FunctionDecl>(OldDecl))) {
          MayNeedOverloadableChecks = true;
          Redeclaration = false;
          OldDecl = nullptr;
        }
      }
    }
  }

  if (checkMultiVersionFunction(*this, NewFD, Redeclaration, OldDecl, Previous))
    return Redeclaration;

  if (Redeclaration) {
    // NewFD and OldDecl represent declarations that need to be
    // merged.
    if (MergeFunctionDecl(NewFD, OldDecl, S, MergeTypeWithPrevious,
                          DeclIsDefn)) {
      NewFD->setInvalidDecl();
      return Redeclaration;
    }

    Previous.clear();
    Previous.addDecl(OldDecl);

    NewFD->setPreviousDeclaration(cast<FunctionDecl>(OldDecl));
  } else if (MayNeedOverloadableChecks && !NewFD->getAttr<OverloadableAttr>()) {
    assert((Previous.empty() ||
            llvm::any_of(Previous,
                         [](const NamedDecl *ND) {
                           return ND->hasAttr<OverloadableAttr>();
                         })) &&
           "Non-redecls shouldn't happen without overloadable present");

    auto OtherUnmarkedIter = llvm::find_if(Previous, [](const NamedDecl *ND) {
      const auto *FD = dyn_cast<FunctionDecl>(ND);
      return FD && !FD->hasAttr<OverloadableAttr>();
    });

    if (OtherUnmarkedIter != Previous.end()) {
      Diag(NewFD->getLocation(),
           diag::err_attribute_overloadable_multiple_unmarked_overloads);
      Diag((*OtherUnmarkedIter)->getLocation(),
           diag::note_attribute_overloadable_prev_overload)
          << false;

      NewFD->addAttr(OverloadableAttr::CreateImplicit(Context));
    }
  }

  // Semantic checking for this function declaration (in isolation).

  // Check if the function definition uses any AArch64 SME features without
  // having the '+sme' feature enabled.
  if (DeclIsDefn) {
    bool UsesSM = NewFD->hasAttr<ArmLocallyStreamingAttr>();
    bool UsesZA = NewFD->hasAttr<ArmNewZAAttr>();
    if (const auto *FPT = NewFD->getType()->getAs<FunctionProtoType>()) {
      FunctionProtoType::ExtProtoInfo EPI = FPT->getExtProtoInfo();
      UsesSM |=
          EPI.AArch64SMEAttributes & FunctionType::SME_PStateSMEnabledMask;
      UsesZA |= EPI.AArch64SMEAttributes & FunctionType::SME_PStateZASharedMask;
    }

    if (UsesSM || UsesZA) {
      llvm::StringMap<bool> FeatureMap;
      Context.getFunctionFeatureMap(FeatureMap, NewFD);
      if (!FeatureMap.contains("sme")) {
        if (UsesSM)
          Diag(NewFD->getLocation(),
               diag::err_sme_definition_using_sm_in_non_sme_target);
        else
          Diag(NewFD->getLocation(),
               diag::err_sme_definition_using_za_in_non_sme_target);
      }
    }
  }

  return Redeclaration;
}

