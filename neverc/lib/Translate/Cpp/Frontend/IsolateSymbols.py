#!/usr/bin/env python3
"""Isolate frontend globals in the hash-pinned LLVM release sources."""

import argparse
from pathlib import Path
import re

parser = argparse.ArgumentParser()
parser.add_argument("--source", required=True, type=Path)
parser.add_argument("--output", required=True, type=Path)
args = parser.parse_args()

# These global C++ identifiers need file-specific handling: the intrinsic
# helper shares a spelling with a class member, Debugify's type occurs in public
# signatures, and PointerBounds shares a spelling with unrelated analysis
# members and parameters. Patch only the extracted private release source.
def replace_once(path, before, after):
    text = path.read_text(encoding="utf-8")
    if after in text:
        return
    if text.count(before) != 1:
        raise SystemExit("Unexpected pinned LLVM source while isolating " + str(path))
    path.write_text(text.replace(before, after), encoding="utf-8")


def preserve_explicit_function_instantiation_source(source_root):
    # Pinned Sema reuses declarations and discards each directive's spelling,
    # including no-effect directives and converted defaults. Keep Sema semantics.
    groups = (
        ('clang/include/clang/AST/ASTConsumer.h', (
            ('  class FunctionDecl;\n  class ImportDecl;',
             '  class FunctionDecl;\n  class ImportDecl;\n  class TemplateArgumentListInfo;\n  class TypeSourceInfo;\n  struct DeclarationNameInfo;\n  class NestedNameSpecifierLoc;\n  class SourceLocation;\n  class TemplateDecl;\n  class NonTypeTemplateParmDecl;\n  class TemplateArgumentLoc;\n  class TemplateArgument;\n  class Type;\n  class NamedDecl;\n  class ClassTemplateSpecializationDecl;'),
            ('  virtual void HandleCXXImplicitFunctionInstantiation(FunctionDecl *D) {}',
             '  virtual void HandleCXXImplicitFunctionInstantiation(FunctionDecl *D) {}\n\n  // NeverC private source evidence; this does not request instantiation.\n  virtual void HandleNeverCExplicitFunctionInstantiation(\n      FunctionDecl *, const TemplateArgumentListInfo &, TypeSourceInfo *,\n      const DeclarationNameInfo &, const NestedNameSpecifierLoc &,\n      const SourceLocation &, bool) {}'),
            ('  virtual void HandleCXXStaticMemberVarInstantiation(VarDecl *D) {}',
             '  virtual void HandleCXXStaticMemberVarInstantiation(VarDecl *D) {}\n\n  // NeverC private source evidence for each static member directive.\n  virtual void HandleNeverCExplicitStaticDataInstantiation(\n      VarDecl *, TypeSourceInfo *, const NestedNameSpecifierLoc &,\n      const SourceLocation &, bool) {}\n\n  // NeverC retains defaults only after successful argument conversion.\n  virtual void HandleNeverCScalarTemplateDefault(\n      TemplateDecl *, NonTypeTemplateParmDecl *,\n      const TemplateArgumentLoc &, const TemplateArgumentLoc &,\n      const TemplateArgument &, const SourceLocation &) {}\n\n  // NeverC source preservation is opt-in; other consumers keep upstream ASTs.\n  virtual bool wantsNeverCTemplateSource() const { return false; }\n  virtual void HandleNeverCTemplateTypeSource(\n      TemplateDecl *, const Type *, TypeSourceInfo *,\n      const TemplateArgumentListInfo &,\n      const TemplateArgument *, const TemplateArgument *, unsigned,\n      NamedDecl *const *, const TemplateArgumentLoc *, const TemplateArgumentLoc *,\n      unsigned, NamedDecl *const *, TypeSourceInfo *const *,\n      const unsigned *, unsigned, bool, const SourceLocation &) {}\n  virtual void HandleNeverCFunctionTemplateSource(\n      FunctionDecl *,\n      const TemplateArgument *, const TemplateArgument *, unsigned,\n      NamedDecl *const *, const TemplateArgumentLoc *, const TemplateArgumentLoc *,\n      unsigned, NamedDecl *const *, TypeSourceInfo *const *,\n      const unsigned *, unsigned, bool, const SourceLocation &) {}\n  virtual void HandleNeverCClassTemplateSource(\n      ClassTemplateSpecializationDecl *, const TemplateArgumentListInfo &, bool,\n      const TemplateArgument *, const TemplateArgument *, unsigned,\n      NamedDecl *const *, const TemplateArgumentLoc *, const TemplateArgumentLoc *,\n      unsigned, NamedDecl *const *, TypeSourceInfo *const *,\n      const unsigned *, unsigned, bool, const SourceLocation &) {}\n  virtual void HandleNeverCFunctionSpecializationSource(\n      FunctionDecl *, FunctionDecl *, const TemplateArgumentListInfo *,\n      const SourceLocation &) {}'),
        )),
        ('clang/lib/Sema/SemaTemplate.cpp', (
            ('    // Check the default template argument.\n    if (CheckTemplateArgument(*Param, Arg, Template, TemplateLoc, RAngleLoc, 0,\n                              CTAI, CTAK_Specified))\n      return true;\n\n    CTAI.SugaredConverted.back().setIsDefaulted(true);',
             '    // Preserve original spelling as well as any conversion-added operations.\n    const auto NeverCWrittenDefault = Arg;\n    // Check the default template argument.\n    if (CheckTemplateArgument(*Param, Arg, Template, TemplateLoc, RAngleLoc, 0,\n                              CTAI, CTAK_Specified))\n      return true;\n\n    if (Consumer.wantsNeverCTemplateSource()) {\n      if (isa<TemplateTypeParmDecl, NonTypeTemplateParmDecl>(*Param))\n        CTAI.retainNeverCDefault(*Param, NeverCWrittenDefault, Arg);\n    } else {\n      if (auto *NeverCParameter = dyn_cast<NonTypeTemplateParmDecl>(*Param))\n        Consumer.HandleNeverCScalarTemplateDefault(\n            Template, NeverCParameter, NeverCWrittenDefault, Arg,\n            CTAI.CanonicalConverted.back(), TemplateLoc);\n    }\n    CTAI.SugaredConverted.back().setIsDefaulted(true);'),
            ('                                            Declarator &D) {\n  // Explicit instantiations always require a name.',
             '                                            Declarator &D) {\n  // Retain attributes before declarator type processing can consume them.\n  const bool NeverCWrittenAttributes = D.hasAttributes();\n  // Explicit instantiations always require a name.'),
            ('    CheckExplicitInstantiation(*this, Prev, D.getIdentifierLoc(), true, TSK);',
             '    // Preserve written static-member source before no-effect handling.\n    Consumer.HandleNeverCExplicitStaticDataInstantiation(\n        Prev, T, D.getCXXScopeSpec().getWithLocInContext(Context),\n        D.getIdentifierLoc(), NeverCWrittenAttributes);\n\n    CheckExplicitInstantiation(*this, Prev, D.getIdentifierLoc(), true, TSK);'),
            ('    Specialization = cast<FunctionDecl>(*Result);\n  }\n\n  // C++11 [except.spec]p4\n  // In an explicit instantiation an exception-specification may be specified,',
             '    Specialization = cast<FunctionDecl>(*Result);\n  }\n\n  // Preserve every directive before duplicate/no-effect early returns.\n  Consumer.HandleNeverCExplicitFunctionInstantiation(\n      Specialization, TemplateArgs, T, NameInfo,\n      D.getCXXScopeSpec().getWithLocInContext(Context),\n      D.getIdentifierLoc(), NeverCWrittenAttributes);\n\n  // C++11 [except.spec]p4\n  // In an explicit instantiation an exception-specification may be specified,'),
            ('        /*Final=*/!getLangOpts().RetainSubstTemplateTypeParmTypeAstNodes);',
             '        /*Final=*/!getLangOpts().RetainSubstTemplateTypeParmTypeAstNodes &&\n            !Consumer.wantsNeverCTemplateSource());'),
            ('  QualType CanonType;\n\n  if (TypeAliasTemplateDecl *AliasTemplate =',
             '  QualType CanonType;\n  TypeSourceInfo *NeverCAliasSource = nullptr;\n\n  if (TypeAliasTemplateDecl *AliasTemplate ='),
            ('    CanonType =\n        SubstType(Pattern->getUnderlyingType(), TemplateArgLists,\n                  AliasTemplate->getLocation(), AliasTemplate->getDeclName());',
             '    if (Consumer.wantsNeverCTemplateSource()) {\n      NeverCAliasSource =\n          SubstType(Pattern->getTypeSourceInfo(), TemplateArgLists,\n                    AliasTemplate->getLocation(), AliasTemplate->getDeclName());\n      CanonType = NeverCAliasSource ? NeverCAliasSource->getType() : QualType();\n    } else {\n      CanonType =\n          SubstType(Pattern->getUnderlyingType(), TemplateArgLists,\n                    AliasTemplate->getLocation(), AliasTemplate->getDeclName());\n    }'),
            ('    MultiLevelTemplateArgumentList TemplateArgLists(Template, SugaredConverted,\n                                                    /*Final=*/true);',
             '    MultiLevelTemplateArgumentList TemplateArgLists(\n        Template, SugaredConverted,\n        /*Final=*/!SemaRef.getASTConsumer().wantsNeverCTemplateSource());'),
            ('static bool SubstDefaultTemplateArgument(\n    Sema &SemaRef, TemplateDecl *Template, SourceLocation TemplateLoc,\n    SourceLocation RAngleLoc, NonTypeTemplateParmDecl *Param,\n    ArrayRef<TemplateArgument> SugaredConverted,\n    ArrayRef<TemplateArgument> CanonicalConverted,\n    TemplateArgumentLoc &Output) {\n  Sema::InstantiatingTemplate Inst(SemaRef, TemplateLoc, Param, Template,\n                                   SugaredConverted,\n                                   SourceRange(TemplateLoc, RAngleLoc));\n  if (Inst.isInvalid())\n    return true;\n\n  // Only substitute for the innermost template argument list.\n  MultiLevelTemplateArgumentList TemplateArgLists(Template, SugaredConverted,\n                                                  /*Final=*/true);',
             'static bool SubstDefaultTemplateArgument(\n    Sema &SemaRef, TemplateDecl *Template, SourceLocation TemplateLoc,\n    SourceLocation RAngleLoc, NonTypeTemplateParmDecl *Param,\n    ArrayRef<TemplateArgument> SugaredConverted,\n    ArrayRef<TemplateArgument> CanonicalConverted,\n    TemplateArgumentLoc &Output) {\n  Sema::InstantiatingTemplate Inst(SemaRef, TemplateLoc, Param, Template,\n                                   SugaredConverted,\n                                   SourceRange(TemplateLoc, RAngleLoc));\n  if (Inst.isInvalid())\n    return true;\n\n  // Only substitute for the innermost template argument list.\n  MultiLevelTemplateArgumentList TemplateArgLists(\n      Template, SugaredConverted,\n      /*Final=*/!SemaRef.getASTConsumer().wantsNeverCTemplateSource());'),
            ('  return Context.getTemplateSpecializationType(Name, TemplateArgs.arguments(),\n                                               CanonType);',
             '  QualType NeverCTypeResult = Context.getTemplateSpecializationType(\n      Name, TemplateArgs.arguments(), CanonType);\n  if (Consumer.wantsNeverCTemplateSource())\n    Consumer.HandleNeverCTemplateTypeSource(\n        Template, NeverCTypeResult.getTypePtr(), NeverCAliasSource, TemplateArgs,\n        CTAI.CanonicalConverted.data(), CTAI.SugaredConverted.data(),\n        CTAI.CanonicalConverted.size(), CTAI.NeverCDefaultParameters.data(),\n        CTAI.NeverCWrittenDefaults.data(), CTAI.NeverCConvertedDefaults.data(),\n        CTAI.NeverCDefaultParameters.size(), CTAI.NeverCTypeParameters.data(),\n        CTAI.NeverCParameterTypes.data(), CTAI.NeverCParameterPackIndices.data(),\n        CTAI.NeverCTypeParameters.size(), CTAI.NeverCDefaultsOverflow, TemplateLoc);\n  return NeverCTypeResult;'),
            ('  Specialization->setInvalidDecl(Invalid);\n  inferGslOwnerPointerAttribute(Specialization);\n  return Specialization;',
             '  Specialization->setInvalidDecl(Invalid);\n  inferGslOwnerPointerAttribute(Specialization);\n  if (Consumer.wantsNeverCTemplateSource() && !Invalid &&\n      !isPartialSpecialization && TUK != TagUseKind::Friend)\n    Consumer.HandleNeverCClassTemplateSource(\n        Specialization, TemplateArgs, /*Instantiation=*/false,\n        CTAI.CanonicalConverted.data(), CTAI.SugaredConverted.data(),\n        CTAI.CanonicalConverted.size(), CTAI.NeverCDefaultParameters.data(),\n        CTAI.NeverCWrittenDefaults.data(), CTAI.NeverCConvertedDefaults.data(),\n        CTAI.NeverCDefaultParameters.size(), CTAI.NeverCTypeParameters.data(),\n        CTAI.NeverCParameterTypes.data(), CTAI.NeverCParameterPackIndices.data(),\n        CTAI.NeverCTypeParameters.size(), CTAI.NeverCDefaultsOverflow, TemplateNameLoc);\n  return Specialization;'),
            ('  // Syntax is now OK, so return if it has no other effect on semantics.\n  if (HasNoEffect) {',
             '  // Preserve each declaration, including a no-effect repeated instantiation.\n  if (Consumer.wantsNeverCTemplateSource())\n    Consumer.HandleNeverCClassTemplateSource(\n        Specialization, TemplateArgs, /*Instantiation=*/true,\n        CTAI.CanonicalConverted.data(), CTAI.SugaredConverted.data(),\n        CTAI.CanonicalConverted.size(), CTAI.NeverCDefaultParameters.data(),\n        CTAI.NeverCWrittenDefaults.data(), CTAI.NeverCConvertedDefaults.data(),\n        CTAI.NeverCDefaultParameters.size(), CTAI.NeverCTypeParameters.data(),\n        CTAI.NeverCParameterTypes.data(), CTAI.NeverCParameterPackIndices.data(),\n        CTAI.NeverCTypeParameters.size(), CTAI.NeverCDefaultsOverflow, TemplateNameLoc);\n\n  // Syntax is now OK, so return if it has no other effect on semantics.\n  if (HasNoEffect) {'),
            ('  Previous.clear();\n  Previous.addDecl(Specialization);\n  return false;',
             '  Previous.clear();\n  Previous.addDecl(Specialization);\n  if (Consumer.wantsNeverCTemplateSource())\n    Consumer.HandleNeverCFunctionSpecializationSource(\n        FD, Specialization,\n        ExplicitTemplateArgs ? &ConvertedTemplateArgs[Specialization] : nullptr,\n        FD->getLocation());\n  return false;'),
            ('    QualType NTTPType = NTTP->getType();\n    if (NTTP->isParameterPack() && NTTP->isExpandedParameterPack())\n      NTTPType = NTTP->getExpansionType(ArgumentPackIndex);\n\n    if (NTTPType->isInstantiationDependentType() &&\n        !isa<TemplateTemplateParmDecl>(Template) &&\n        !Template->getDeclContext()->isDependentContext()) {\n      // Do substitution on the type of the non-type template parameter.\n      InstantiatingTemplate Inst(*this, TemplateLoc, Template, NTTP,\n                                 CTAI.SugaredConverted,\n                                 SourceRange(TemplateLoc, RAngleLoc));\n      if (Inst.isInvalid())\n        return true;\n\n      MultiLevelTemplateArgumentList MLTAL(Template, CTAI.SugaredConverted,\n                                           /*Final=*/true);\n      // If the parameter is a pack expansion, expand this slice of the pack.\n      if (auto *PET = NTTPType->getAs<PackExpansionType>()) {\n        Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(*this,\n                                                           ArgumentPackIndex);\n        NTTPType = SubstType(PET->getPattern(), MLTAL, NTTP->getLocation(),\n                             NTTP->getDeclName());\n      } else {\n        NTTPType = SubstType(NTTPType, MLTAL, NTTP->getLocation(),\n                             NTTP->getDeclName());\n      }\n\n      // If that worked, check the non-type template parameter type\n      // for validity.\n      if (!NTTPType.isNull())\n        NTTPType = CheckNonTypeTemplateParameterType(NTTPType,\n                                                     NTTP->getLocation());\n      if (NTTPType.isNull())\n        return true;\n    }\n\n',
             '    QualType NTTPType = NTTP->getType();\n    TypeSourceInfo *NeverCParameterTypeSource =\n        Consumer.wantsNeverCTemplateSource() ? NTTP->getTypeSourceInfo() : nullptr;\n    if (NTTP->isParameterPack() && NTTP->isExpandedParameterPack())\n      NTTPType = NTTP->getExpansionType(ArgumentPackIndex);\n    if (Consumer.wantsNeverCTemplateSource() && NTTP->isParameterPack() &&\n        NTTP->isExpandedParameterPack())\n      NeverCParameterTypeSource = NTTP->getExpansionTypeSourceInfo(ArgumentPackIndex);\n\n    if (NTTPType->isInstantiationDependentType() &&\n        !isa<TemplateTemplateParmDecl>(Template) &&\n        !Template->getDeclContext()->isDependentContext()) {\n      // Do substitution on the type of the non-type template parameter.\n      InstantiatingTemplate Inst(*this, TemplateLoc, Template, NTTP,\n                                 CTAI.SugaredConverted,\n                                 SourceRange(TemplateLoc, RAngleLoc));\n      if (Inst.isInvalid())\n        return true;\n\n      MultiLevelTemplateArgumentList MLTAL(Template, CTAI.SugaredConverted,\n                                           /*Final=*/!Consumer.wantsNeverCTemplateSource());\n      // If the parameter is a pack expansion, expand this slice of the pack.\n      if (auto *PET = NTTPType->getAs<PackExpansionType>()) {\n        Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(*this,\n                                                           ArgumentPackIndex);\n        if (NeverCParameterTypeSource) {\n          auto NeverCPattern = NeverCParameterTypeSource->getTypeLoc()\n                                   .getAs<PackExpansionTypeLoc>();\n          if (NeverCPattern) {\n            auto NeverCPatternLoc = NeverCPattern.getPatternLoc();\n            NeverCParameterTypeSource = Context.CreateTypeSourceInfo(PET->getPattern());\n            NeverCParameterTypeSource->getTypeLoc().initializeFullCopy(NeverCPatternLoc);\n          } else {\n            NeverCParameterTypeSource = nullptr;\n          }\n        }\n        if (NeverCParameterTypeSource) {\n          NeverCParameterTypeSource = SubstType(NeverCParameterTypeSource, MLTAL,\n                                               NTTP->getLocation(), NTTP->getDeclName());\n          NTTPType = NeverCParameterTypeSource ? NeverCParameterTypeSource->getType() : QualType();\n        } else {\n          NTTPType = SubstType(PET->getPattern(), MLTAL, NTTP->getLocation(),\n                               NTTP->getDeclName());\n        }\n      } else {\n        if (NeverCParameterTypeSource) {\n          NeverCParameterTypeSource = SubstType(NeverCParameterTypeSource, MLTAL,\n                                               NTTP->getLocation(), NTTP->getDeclName());\n          NTTPType = NeverCParameterTypeSource ? NeverCParameterTypeSource->getType() : QualType();\n        } else {\n          NTTPType = SubstType(NTTPType, MLTAL, NTTP->getLocation(),\n                               NTTP->getDeclName());\n        }\n      }\n\n      // If that worked, check the non-type template parameter type\n      // for validity.\n      if (!NTTPType.isNull())\n        NTTPType = CheckNonTypeTemplateParameterType(NTTPType,\n                                                     NTTP->getLocation());\n      if (NTTPType.isNull())\n        return true;\n    }\n\n'),
            ('    case TemplateArgument::Pack:\n      llvm_unreachable("Caller must expand template argument packs");\n    }\n\n    return false;',
             '    case TemplateArgument::Pack:\n      llvm_unreachable("Caller must expand template argument packs");\n    }\n\n    if (Consumer.wantsNeverCTemplateSource())\n      CTAI.retainNeverCParameterType(NTTP, NeverCParameterTypeSource, ArgumentPackIndex);\n    return false;'),
        )),
        ('clang/lib/Sema/SemaTemplateDeduction.cpp', (
            ('#include "clang/AST/ASTContext.h"',
             '#include "clang/AST/ASTConsumer.h"\n#include "clang/AST/ASTContext.h"'),
            ('    // Check whether we can actually use the default argument.\n    if (S.CheckTemplateArgument(\n            Param, DefArg, TD, TD->getLocation(), TD->getSourceRange().getEnd(),\n            /*ArgumentPackIndex=*/0, CTAI, Sema::CTAK_Specified)) {\n      Info.Param = makeTemplateParameter(\n                         const_cast<NamedDecl *>(TemplateParams->getParam(I)));\n      // FIXME: These template arguments are temporary. Free them!\n      Info.reset(\n          TemplateArgumentList::CreateCopy(S.Context, CTAI.SugaredConverted),\n          TemplateArgumentList::CreateCopy(S.Context, CTAI.CanonicalConverted));\n      return TemplateDeductionResult::SubstitutionFailure;\n    }\n\n    // If we get here, we successfully used the default template argument.',
             '    // Preserve spelling before CheckTemplateArgument adds conversions.\n    const auto NeverCWrittenDefault = DefArg;\n    // Check whether we can actually use the default argument.\n    if (S.CheckTemplateArgument(\n            Param, DefArg, TD, TD->getLocation(), TD->getSourceRange().getEnd(),\n            /*ArgumentPackIndex=*/0, CTAI, Sema::CTAK_Specified)) {\n      Info.Param = makeTemplateParameter(\n                         const_cast<NamedDecl *>(TemplateParams->getParam(I)));\n      // FIXME: These template arguments are temporary. Free them!\n      Info.reset(\n          TemplateArgumentList::CreateCopy(S.Context, CTAI.SugaredConverted),\n          TemplateArgumentList::CreateCopy(S.Context, CTAI.CanonicalConverted));\n      return TemplateDeductionResult::SubstitutionFailure;\n    }\n\n    if (S.getASTConsumer().wantsNeverCTemplateSource()) {\n      if (isa<TemplateTypeParmDecl, NonTypeTemplateParmDecl>(Param))\n        CTAI.retainNeverCDefault(Param, NeverCWrittenDefault, DefArg);\n    } else {\n      if (auto *NeverCParameter = dyn_cast<NonTypeTemplateParmDecl>(Param))\n        S.getASTConsumer().HandleNeverCScalarTemplateDefault(\n            TD, NeverCParameter, NeverCWrittenDefault, DefArg,\n            CTAI.CanonicalConverted.back(), TD->getLocation());\n    }\n    // If we get here, we successfully used the default template argument.'),
            ('    if (Inserted)\n      Pos->second.append(Info.diag_begin(), Info.diag_end());\n  }\n\n  return TemplateDeductionResult::Success;',
             '    if (Inserted)\n      Pos->second.append(Info.diag_begin(), Info.diag_end());\n  }\n\n  if (Consumer.wantsNeverCTemplateSource() && !IsIncomplete)\n    Consumer.HandleNeverCFunctionTemplateSource(\n        Specialization,\n        CTAI.CanonicalConverted.data(), CTAI.SugaredConverted.data(),\n        CTAI.CanonicalConverted.size(), CTAI.NeverCDefaultParameters.data(),\n        CTAI.NeverCWrittenDefaults.data(), CTAI.NeverCConvertedDefaults.data(),\n        CTAI.NeverCDefaultParameters.size(), CTAI.NeverCTypeParameters.data(),\n        CTAI.NeverCParameterTypes.data(), CTAI.NeverCParameterPackIndices.data(),\n        CTAI.NeverCTypeParameters.size(), CTAI.NeverCDefaultsOverflow, Info.getLocation());\n  return TemplateDeductionResult::Success;'),
            ('      MultiLevelTemplateArgumentList Args(Template, CTAI.SugaredConverted,\n                                          /*Final=*/true);\n\n      if (auto *NTTP = dyn_cast<NonTypeTemplateParmDecl>(Param)) {\n        Sema::InstantiatingTemplate Inst(S, Template->getLocation(), Template,\n                                         NTTP, CTAI.SugaredConverted,\n                                         Template->getSourceRange());\n        if (Inst.isInvalid() ||\n            S.SubstType(NTTP->getType(), Args, NTTP->getLocation(),\n                        NTTP->getDeclName()).isNull())\n          return true;\n      } else if (auto *TTP = dyn_cast<TemplateTemplateParmDecl>(Param)) {\n        Sema::InstantiatingTemplate Inst(S, Template->getLocation(), Template,\n                                         TTP, CTAI.SugaredConverted,\n                                         Template->getSourceRange());\n        if (Inst.isInvalid() || !S.SubstDecl(TTP, S.CurContext, Args))\n          return true;\n      }\n',
             '      MultiLevelTemplateArgumentList Args(Template, CTAI.SugaredConverted,\n                                          /*Final=*/!(S.getASTConsumer().wantsNeverCTemplateSource() &&\n                                                      isa<NonTypeTemplateParmDecl>(Param)));\n\n      if (auto *NTTP = dyn_cast<NonTypeTemplateParmDecl>(Param)) {\n        Sema::InstantiatingTemplate Inst(S, Template->getLocation(), Template,\n                                         NTTP, CTAI.SugaredConverted,\n                                         Template->getSourceRange());\n        if (Inst.isInvalid())\n          return true;\n        auto *NeverCEmptyParameterSource = S.getASTConsumer().wantsNeverCTemplateSource()\n                                              ? NTTP->getTypeSourceInfo() : nullptr;\n        if (NeverCEmptyParameterSource) {\n          NeverCEmptyParameterSource = S.SubstType(NeverCEmptyParameterSource, Args,\n                                                   NTTP->getLocation(), NTTP->getDeclName());\n          if (!NeverCEmptyParameterSource)\n            return true;\n        } else if (S.SubstType(NTTP->getType(), Args, NTTP->getLocation(),\n                               NTTP->getDeclName()).isNull()) {\n          return true;\n        }\n        if (S.getASTConsumer().wantsNeverCTemplateSource())\n          CTAI.retainNeverCParameterType(NTTP, NeverCEmptyParameterSource, ~0u);\n      } else if (auto *TTP = dyn_cast<TemplateTemplateParmDecl>(Param)) {\n        Sema::InstantiatingTemplate Inst(S, Template->getLocation(), Template,\n                                         TTP, CTAI.SugaredConverted,\n                                         Template->getSourceRange());\n        if (Inst.isInvalid() || !S.SubstDecl(TTP, S.CurContext, Args))\n          return true;\n      }\n'),
            ('    TemplateDeductionInfo &Info) {\n  FunctionDecl *Function = FunctionTemplate->getTemplatedDecl();',
             '    TemplateDeductionInfo &Info) {\n  if (Consumer.wantsNeverCTemplateSource())\n    Info.clearNeverCExplicitSource();\n  FunctionDecl *Function = FunctionTemplate->getTemplatedDecl();'),
            ('  Info.setExplicitArgs(SugaredExplicitArgumentList,\n                       CanonicalExplicitArgumentList);',
             '  Info.setExplicitArgs(SugaredExplicitArgumentList,\n                       CanonicalExplicitArgumentList);\n  if (Consumer.wantsNeverCTemplateSource()) {\n    Info.NeverCExplicitSourceTemplate = FunctionTemplate;\n    Info.NeverCExplicitTypeParameters = CTAI.NeverCTypeParameters;\n    Info.NeverCExplicitParameterTypes = CTAI.NeverCParameterTypes;\n    Info.NeverCExplicitPackIndices = CTAI.NeverCParameterPackIndices;\n    Info.NeverCExplicitSourceOverflow = CTAI.NeverCDefaultsOverflow;\n  }'),
            ('          CTAI.CanonicalConverted.push_back(\n              S.Context.getCanonicalTemplateArgument(Deduced[I]));\n          continue;',
             '          CTAI.CanonicalConverted.push_back(\n              S.Context.getCanonicalTemplateArgument(Deduced[I]));\n          if (S.getASTConsumer().wantsNeverCTemplateSource() &&\n              isa<NonTypeTemplateParmDecl>(Param)) {\n            if (Info.NeverCExplicitSourceTemplate != dyn_cast<FunctionTemplateDecl>(Template)) {\n              CTAI.NeverCDefaultsOverflow = true;\n            } else {\n              CTAI.NeverCDefaultsOverflow |= Info.NeverCExplicitSourceOverflow;\n              for (unsigned E = 0; E < Info.NeverCExplicitTypeParameters.size(); ++E)\n                if (const auto *NeverCExplicitParameter = dyn_cast<NonTypeTemplateParmDecl>(\n                        Info.NeverCExplicitTypeParameters[E]);\n                    NeverCExplicitParameter && NeverCExplicitParameter->getIndex() == I)\n                  CTAI.retainNeverCParameterType(Info.NeverCExplicitTypeParameters[E],\n                                                Info.NeverCExplicitParameterTypes[E],\n                                                Info.NeverCExplicitPackIndices[E]);\n            }\n          }\n          continue;'),
        )),
        ('clang/include/clang/Sema/Sema.h', (
            ('    SmallVector<TemplateArgument, 4> SugaredConverted, CanonicalConverted;\n',
             '    SmallVector<TemplateArgument, 4> SugaredConverted, CanonicalConverted;\n\n    // Keep defaults attached to this deduction, including ignored type args.\n    SmallVector<NamedDecl *, 4> NeverCDefaultParameters;\n    SmallVector<TemplateArgumentLoc, 4> NeverCWrittenDefaults, NeverCConvertedDefaults;\n    bool NeverCDefaultsOverflow = false;\n\n    void retainNeverCDefault(NamedDecl *Parameter,\n                            const TemplateArgumentLoc &Written,\n                            const TemplateArgumentLoc &Converted) {\n      if (NeverCDefaultParameters.size() == 64) {\n        NeverCDefaultsOverflow = true;\n        return;\n      }\n      NeverCDefaultParameters.push_back(Parameter);\n      NeverCWrittenDefaults.push_back(Written);\n      NeverCConvertedDefaults.push_back(Converted);\n    }\n\n    SmallVector<NamedDecl *, 4> NeverCTypeParameters;\n    SmallVector<TypeSourceInfo *, 4> NeverCParameterTypes;\n    SmallVector<unsigned, 4> NeverCParameterPackIndices;\n\n    void retainNeverCParameterType(NamedDecl *Parameter, TypeSourceInfo *Source,\n                                  unsigned PackIndex) {\n      if (NeverCTypeParameters.size() == 4096) {\n        NeverCDefaultsOverflow = true;\n        return;\n      }\n      NeverCTypeParameters.push_back(Parameter);\n      NeverCParameterTypes.push_back(Source);\n      NeverCParameterPackIndices.push_back(PackIndex);\n    }\n'),
        )),
        ('clang/include/clang/Sema/TemplateDeduction.h', (
            ('public:\n  TemplateDeductionInfo(SourceLocation Loc, unsigned DeducedDepth = 0)',
             'public:\n  // NeverC keeps preliminary explicit conversions on this exact candidate.\n  // reset/take retain it; a new explicit-substitution invocation clears it.\n  FunctionTemplateDecl *NeverCExplicitSourceTemplate = nullptr;\n  SmallVector<NamedDecl *, 4> NeverCExplicitTypeParameters;\n  SmallVector<TypeSourceInfo *, 4> NeverCExplicitParameterTypes;\n  SmallVector<unsigned, 4> NeverCExplicitPackIndices;\n  bool NeverCExplicitSourceOverflow = false;\n\n  void clearNeverCExplicitSource() {\n    NeverCExplicitSourceTemplate = nullptr;\n    NeverCExplicitTypeParameters.clear();\n    NeverCExplicitParameterTypes.clear();\n    NeverCExplicitPackIndices.clear();\n    NeverCExplicitSourceOverflow = false;\n  }\n\n  TemplateDeductionInfo(SourceLocation Loc, unsigned DeducedDepth = 0)'),
        )),
    )
    updates = []
    states = []
    for relative, replacements in groups:
        path = source_root / relative
        message = "Unexpected pinned Clang explicit-instantiation source in " + str(path)
        try:
            text = path.read_text(encoding="utf-8")
        except (OSError, UnicodeError) as error:
            raise SystemExit(message) from error
        # Some rewritten blocks contain the original anchor as a prefix. Count
        # pristine anchors only after removing each complete rewritten block.
        counts = [(text.replace(after, "").count(before), text.count(after))
                  for before, after in replacements]
        if all(count == (1, 0) for count in counts):
            state = 0
        elif all(count == (0, 1) for count in counts):
            state = 1
        else:
            raise SystemExit(message)
        remainder = text
        for pair in replacements:
            remainder = remainder.replace(pair[state], "", 1)
        if re.search(r"\b(?:HandleNeverCExplicitFunctionInstantiation|HandleNeverCExplicitStaticDataInstantiation|HandleNeverCScalarTemplateDefault|NeverCWrittenAttributes|NeverCWrittenDefault|NeverCParameter|wantsNeverCTemplateSource|HandleNeverCTemplateTypeSource|HandleNeverCFunctionTemplateSource|HandleNeverCClassTemplateSource|HandleNeverCFunctionSpecializationSource|NeverCAliasSource|NeverCTypeResult|NeverCDefaultParameters|NeverCWrittenDefaults|NeverCConvertedDefaults|NeverCDefaultsOverflow|NeverCAliasSource|NeverCConvertedDefaults|NeverCDefaultParameters|NeverCDefaultsOverflow|NeverCEmptyParameterSource|NeverCExplicitPackIndices|NeverCExplicitParameterTypes|NeverCExplicitSourceOverflow|NeverCExplicitSourceTemplate|NeverCExplicitTypeParameters|NeverCParameter|NeverCParameterPackIndices|NeverCParameterTypeSource|NeverCParameterTypes|NeverCPattern|NeverCPatternLoc|NeverCTypeParameters|NeverCTypeResult|NeverCWrittenAttributes|NeverCWrittenDefault|NeverCWrittenDefaults|NeverCExplicitParameter|clearNeverCExplicitSource|retainNeverCDefault|retainNeverCParameterType)\b", remainder):
            raise SystemExit(message)
        states.append(state)
        if state == 0:
            for before, after in replacements:
                text = text.replace(before, after, 1)
        updates.append((path, text))
    if len(set(states)) != 1:
        raise SystemExit("Unexpected partial pinned Clang explicit-instantiation source")
    if states[0] == 0:
        for path, text in updates:
            path.write_text(text, encoding="utf-8")


def isolate_pointer_bounds(path):
    before = ("struct PointerBounds {\n"
              "  TrackingVH<Value> Start;\n"
              "  TrackingVH<Value> End;\n"
              "  Value *StrideToCheck;\n"
              "};")
    after = ("struct neverc_cpp_PointerBounds {\n"
             "  TrackingVH<Value> Start;\n"
             "  TrackingVH<Value> End;\n"
             "  Value *StrideToCheck;\n"
             "};\n"
             "using PointerBounds = neverc_cpp_PointerBounds;")
    text = path.read_text(encoding="utf-8")
    counts = (text.count(before), text.count(after))
    if counts == (1, 0):
        block = before
    elif counts == (0, 1):
        block = after
    else:
        raise SystemExit("Unexpected pinned LLVM PointerBounds declaration in " + str(path))

    # The pinned file has six uses after the record declaration. An alias keeps
    # those uses intact while changing the actual record's mangled identity.
    # Neither an additional declaration nor a partial previous rewrite is valid.
    remainder = text.replace(block, "", 1)
    if (len(re.findall(r"\bPointerBounds\b", remainder)) != 6
            or re.search(r"\b(?:struct|class|union)\b[^;{}]*\bPointerBounds\b", remainder)
            or re.search(r"\bneverc_cpp_PointerBounds\b", remainder)
            or re.search(r"\busing\s+PointerBounds\s*=", remainder)
            or re.search(r"^\s*#\s*(?:define|undef)\s+PointerBounds\b", remainder, re.M)):
        raise SystemExit("Unexpected pinned LLVM PointerBounds uses in " + str(path))
    if block == before:
        path.write_text(text.replace(before, after, 1), encoding="utf-8")


def isolate_math_calls(source_root):
    # The integer/mixed cmath templates convert their arguments to double
    # before calling the C math overload. Make those same conversions at the
    # six pinned call sites instead of emitting shared global MSVC templates.
    # Keep the original getter, negation and float/half conversion order.
    files = (
        ("llvm/lib/Support/Signals.cpp", (
            ("std::log10(Depth)", "std::log10(static_cast<double>(Depth))"),
        )),
        ("llvm/lib/Support/APFixedPoint.cpp", (
            ("std::pow(2, Sema.getLsbWeight())",
             "std::pow(2.0, static_cast<double>(Sema.getLsbWeight()))"),
            ("std::pow(2, -DstFXSema.getLsbWeight())",
             "std::pow(2.0, static_cast<double>(-DstFXSema.getLsbWeight()))"),
            ("std::pow(2, DstFXSema.getLsbWeight())",
             "std::pow(2.0, static_cast<double>(DstFXSema.getLsbWeight()))"),
        )),
        ("llvm/lib/Analysis/ConstantFolding.cpp", (
            ("std::pow(Op1V.convertToFloat(), Exp)",
             "std::pow(static_cast<double>(Op1V.convertToFloat()), "
             "static_cast<double>(Exp))"),
            ("std::pow(Op1V.convertToDouble(), Exp)",
             "std::pow(Op1V.convertToDouble(), static_cast<double>(Exp))"),
        )),
    )
    updates = []
    for relative, replacements in files:
        path = source_root / relative
        try:
            text = path.read_text(encoding="utf-8")
        except (OSError, UnicodeError) as error:
            raise SystemExit("Unexpected pinned LLVM math calls in " + str(path)) from error
        counts = [(text.count(before), text.count(after))
                  for before, after in replacements]
        if all(count == (0, 1) for count in counts):
            continue
        if not all(count == (1, 0) for count in counts):
            raise SystemExit("Unexpected pinned LLVM math calls in " + str(path))
        for before, after in replacements:
            text = text.replace(before, after, 1)
        updates.append((path, text))
    # Validate all three inputs before changing any math source. Each file is
    # either wholly original or wholly rewritten; partial edits fail closed.
    for path, text in updates:
        path.write_text(text, encoding="utf-8")


def fix_deduced_reference_conversions(path):
    before = """      else
        Conv = cast<CXXConversionDecl>(D);

      // If the conversion function doesn't return a reference type,
      // it can't be considered for this conversion unless we're allowed to
      // consider rvalues.
      // FIXME: Do we need to make sure that we only consider conversion
      // candidates with reference-compatible results? That might be needed to
      // break recursion.
      if ((AllowRValues ||
           Conv->getConversionType()->isLValueReferenceType())) {
        if (ConvTemplate)"""
    insertion = """      // NeverC deduced reference conversions: determine only return forms
      // that can become lvalue references before the reference-only filter.
      if (!AllowRValues && !ConvTemplate && S.getLangOpts().CPlusPlus14 &&
          Conv->getConversionType()->isUndeducedType()) {
        QualType NeverCReturn = Conv->getConversionType();
        const auto *NeverCAuto = NeverCReturn->getAs<AutoType>();
        const auto *NeverCRValue = NeverCReturn->getAs<RValueReferenceType>();
        bool NeverCCanBeLValue =
            (NeverCAuto && NeverCAuto->isDecltypeAuto()) ||
            (NeverCRValue &&
             !NeverCRValue->getPointeeType().hasQualifiers() &&
             NeverCRValue->getPointeeType()->getAs<AutoType>());
        if (NeverCCanBeLValue &&
            S.DeduceReturnType(Conv, Initializer->getExprLoc()))
          continue;
      }

"""
    after = before.replace("      // If the conversion function", insertion +
                           "      // If the conversion function", 1)
    error_message = "Unexpected pinned Clang deduced reference conversion source"
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        raise SystemExit(error_message) from error
    counts = (text.count(before), text.count(after))
    if counts == (1, 0):
        state = 0
    elif counts == (0, 1):
        state = 1
    else:
        raise SystemExit(error_message)
    remainder = text.replace((before, after)[state], "", 1)
    if "NeverC deduced reference conversions" in remainder or "NeverCReturn" in remainder:
        raise SystemExit(error_message)
    if state == 0:
        path.write_text(text.replace(before, after, 1), encoding="utf-8")


def fix_deduced_reference_arguments(path):
    before = "    else\n      Conv = cast<CXXConversionDecl>(D);\n\n    if (AllowRvalues) {\n      // If we are initializing an rvalue reference, don't permit conversion\n      // functions that return lvalues."
    after = "    else\n      Conv = cast<CXXConversionDecl>(D);\n\n    // NeverC reference-argument deduction precedes result-type filtering.\n    if (!ConvTemplate && S.getLangOpts().CPlusPlus14 &&\n        Conv->getConversionType()->isUndeducedType()) {\n      QualType NeverCArgumentResult = Conv->getConversionType();\n      const auto *NeverCAuto = NeverCArgumentResult->getAs<AutoType>();\n      const auto *NeverCRValue = NeverCArgumentResult->getAs<RValueReferenceType>();\n      const auto *NeverCLValue = NeverCArgumentResult->getAs<LValueReferenceType>();\n      bool NeverCCanBeLValue =\n          (NeverCAuto && NeverCAuto->isDecltypeAuto()) ||\n          (NeverCRValue && !NeverCRValue->getPointeeType().hasQualifiers() &&\n           NeverCRValue->getPointeeType()->getAs<AutoType>());\n      // Preserve the definite lvalue exclusion before touching a lazy body.\n      bool NeverCExcludedLValue = DeclType->isRValueReferenceType() &&\n          NeverCLValue && !NeverCLValue->getPointeeType()->isFunctionType();\n      bool NeverCNeedsResult = AllowRvalues ? !NeverCExcludedLValue\n                                          : NeverCCanBeLValue;\n      if (NeverCNeedsResult && S.DeduceReturnType(Conv, Init->getExprLoc()))\n        continue;\n    }\n\n    if (AllowRvalues) {\n      // If we are initializing an rvalue reference, don't permit conversion\n      // functions that return lvalues."
    error_message = "Unexpected pinned Clang reference-argument source"
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        raise SystemExit(error_message) from error
    counts = (text.count(before), text.count(after))
    if counts == (1, 0):
        state = 0
    elif counts == (0, 1):
        state = 1
    else:
        raise SystemExit(error_message)
    remainder = text.replace((before, after)[state], "", 1)
    if ("NeverC reference-argument deduction" in remainder or
        re.search(r"\b(?:NeverCArgumentResult|NeverCAuto|NeverCRValue|NeverCLValue|NeverCCanBeLValue|NeverCExcludedLValue|NeverCNeedsResult)\b", remainder)):
        raise SystemExit(error_message)
    if state == 0:
        path.write_text(text.replace(before, after, 1), encoding="utf-8")


def fix_imported_namespace_defaults(source_root):
    # Check both parts before writing either private Clang source file.
    header_before = "  NamedDecl *getTargetDecl() const { return Underlying; }"
    header_after = "  NamedDecl *getTargetDecl() const;"
    source_before = "void UsingShadowDecl::anchor() {}\n\nUsingShadowDecl::UsingShadowDecl("
    source_after = """void UsingShadowDecl::anchor() {}

NamedDecl *UsingShadowDecl::getTargetDecl() const {
  // NeverC C++17 imported defaults: later declarations of the same namespace
  // function contribute defaults without extending the imported overload set.
  auto *Original = dyn_cast<FunctionDecl>(Underlying);
  if (!Original || Original->getKind() != Decl::Function ||
      Original->getTemplatedKind() != FunctionDecl::TK_NonTemplate ||
      !Original->getASTContext().getLangOpts().CPlusPlus ||
      !Original->getLexicalDeclContext()->getRedeclContext()->isFileContext())
    return Underlying;
  const auto *Namespace = Original->getDeclContext()->getRedeclContext();
  if (!Namespace->isFileContext())
    return Underlying;
  Namespace = Namespace->getPrimaryContext();
  unsigned Required = Original->getMinRequiredArguments();
  if (!Required)
    return Underlying;
  for (auto *Later = Original->getMostRecentDecl(); Later && Later != Original;
       Later = Later->getPreviousDecl()) {
    if (!Later->isInvalidDecl() && !Later->isLocalExternDecl() &&
        Later->getLexicalDeclContext()->getRedeclContext()->isFileContext() &&
        Later->getDeclContext()->getRedeclContext()->getPrimaryContext() == Namespace &&
        Later->getMinRequiredArguments() < Required)
      return Later;
  }
  return Underlying;
}

UsingShadowDecl::UsingShadowDecl("""
    pairs = (
        (source_root / "clang/include/clang/AST/DeclCXX.h", header_before, header_after),
        (source_root / "clang/lib/AST/DeclCXX.cpp", source_before, source_after),
    )
    error_message = "Unexpected pinned Clang imported default source pair"
    updates = []
    states = []
    for path, before, after in pairs:
        try:
            text = path.read_text(encoding="utf-8")
        except (OSError, UnicodeError) as error:
            raise SystemExit(error_message) from error
        counts = (text.count(before), text.count(after))
        if counts == (1, 0):
            state = 0
        elif counts == (0, 1):
            state = 1
        else:
            raise SystemExit(error_message)
        remainder = text.replace((before, after)[state], "", 1)
        if "NeverC C++17 imported defaults" in remainder or "UsingShadowDecl::getTargetDecl" in remainder:
            raise SystemExit(error_message)
        states.append(state)
        updates.append((path, text.replace(before, after, 1) if state == 0 else text))
    if states[0] != states[1]:
        raise SystemExit(error_message)
    if states[0] == 0:
        for path, text in updates:
            path.write_text(text, encoding="utf-8")


def fix_nested_friend_declaration_access(source_root):
    # Keep the supplemental checks together: access queue, diagnostic marker,
    # and the late default's matching parser/Sema function context.
    groups = (
        ('clang/lib/Sema/SemaAccess.cpp', (
            ("""    if (!IsFriendDeclaration) {
      S.DelayedDiagnostics.add(DelayedDiagnostic::makeAccess(Loc, Entity));
      return Sema::AR_delayed;
    }
  }

  EffectiveContext EC(S.CurContext);""",
             """    if (!IsFriendDeclaration) {
      S.DelayedDiagnostics.add(DelayedDiagnostic::makeAccess(Loc, Entity));
      return Sema::AR_delayed;
    }
    // Retain the immediate nominated-name check before redeclaration merging,
    // then check the completed nested friend function's own access context.
    const auto *NestedClass = dyn_cast<CXXRecordDecl>(S.CurContext);
    if (NestedClass && isa<CXXRecordDecl>(NestedClass->getDeclContext())) {
      auto Diagnostic = DelayedDiagnostic::makeAccess(Loc, Entity);
      Diagnostic.NestedFriendAccess = true;
      S.DelayedDiagnostics.add(Diagnostic);
    }
  }

  EffectiveContext EC(S.CurContext);"""),
            ("""void Sema::HandleDelayedAccessCheck(DelayedDiagnostic &DD, Decl *D) {
  // Access control for names used in the declarations of functions""",
             """void Sema::HandleDelayedAccessCheck(DelayedDiagnostic &DD, Decl *D) {
  // Supplemental function checks must not change type-friend declarations,
  // including the ClassTemplateDecl returned by a templated friend tag.
  if (DD.NestedFriendAccess &&
      !isa<FunctionDecl, FunctionTemplateDecl>(D))
    return;
  // Access control for names used in the declarations of functions"""),
        )),
        ('clang/include/clang/Sema/DelayedDiagnostic.h', (
            ("""  DDKind Kind;
  bool Triggered;

  SourceLocation Loc;""",
             """  DDKind Kind;
  bool Triggered;
  bool NestedFriendAccess = false;

  SourceLocation Loc;"""),
        )),
        ('clang/lib/Parse/ParseCXXInlineMethods.cpp', (
            ("""    std::unique_ptr<CachedTokens> Toks = std::move(LM.DefaultArgs[I].Toks);
    if (Toks) {
      ParenBraceBracketBalancer BalancerRAIIObj(*this);""",
             """    std::unique_ptr<CachedTokens> Toks = std::move(LM.DefaultArgs[I].Toks);
    if (Toks) {
      // A nested friend default uses the function's access context while
      // preserving the surrounding class scopes for lexical name lookup.
      auto *Function = dyn_cast<FunctionDecl>(LM.Method);
      if (const auto *Template = dyn_cast<FunctionTemplateDecl>(LM.Method))
        Function = Template->getTemplatedDecl();
      const auto *Lexical = Function
          ? dyn_cast<CXXRecordDecl>(Function->getLexicalDeclContext()) : nullptr;
      bool NestedFriendDefault = Function && Function->getFriendObjectKind() &&
          Lexical && isa<CXXRecordDecl>(Lexical->getDeclContext());
      ParseScope DefaultScope(this, Scope::FnScope, NestedFriendDefault);
      std::optional<Sema::ContextRAII> DefaultContext;
      std::optional<Sema::FunctionScopeRAII> DefaultFunctionScope;
      if (NestedFriendDefault) {
        DefaultContext.emplace(Actions, Function, /*NewThisContext=*/false);
        DefaultFunctionScope.emplace(Actions);
        Actions.PushFunctionScope();
      }
      ParenBraceBracketBalancer BalancerRAIIObj(*this);"""),
        )),
    )
    error_message = "Unexpected pinned Clang nested friend declaration source group"
    updates = []
    states = []
    for relative, replacements in groups:
        path = source_root / relative
        try:
            text = path.read_text(encoding="utf-8")
        except (OSError, UnicodeError) as error:
            raise SystemExit(error_message) from error
        counts = [(text.count(before), text.count(after)) for before, after in replacements]
        if all(count == (1, 0) for count in counts):
            state = 0
        elif all(count == (0, 1) for count in counts):
            state = 1
        else:
            raise SystemExit(error_message)
        remainder = text
        for pair in replacements:
            remainder = remainder.replace(pair[state], "", 1)
        if "NestedFriendAccess" in remainder or "NestedFriendDefault" in remainder:
            raise SystemExit(error_message)
        states.append(state)
        if state == 0:
            for before, after in replacements:
                text = text.replace(before, after, 1)
        updates.append((path, text))
    if len(set(states)) != 1:
        raise SystemExit(error_message)
    if states[0] == 0:
        for path, text in updates:
            path.write_text(text, encoding="utf-8")


def fix_nested_friend_access(path):
    # Restrict the pinned access-context walk, preserving canonical function
    # grants. This changes only the extracted private Clang library source.
    before = """      } else if (isa<FunctionDecl>(DC)) {
        FunctionDecl *Function = cast<FunctionDecl>(DC);
        Functions.push_back(Function->getCanonicalDecl());
        if (Function->getFriendObjectKind())
          DC = Function->getLexicalDeclContext();
        else
          DC = Function->getDeclContext();
      } else if (DC->isFileContext()) {"""
    after = """      } else if (isa<FunctionDecl>(DC)) {
        FunctionDecl *Function = cast<FunctionDecl>(DC);
        Functions.push_back(Function->getCanonicalDecl());
        // C++17 [class.nest]/4: a nested inline friend has no implicit access
        // to enclosing classes. Its own explicit friendship remains above.
        const auto *LexicalRecord =
            dyn_cast<CXXRecordDecl>(Function->getLexicalDeclContext());
        if (Function->getFriendObjectKind() &&
            !(LexicalRecord && isa<CXXRecordDecl>(LexicalRecord->getDeclContext())))
          DC = Function->getLexicalDeclContext();
        else
          DC = Function->getDeclContext();
      } else if (DC->isFileContext()) {"""
    error_message = "Unexpected pinned Clang nested friend access source in " + str(path)
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        raise SystemExit(error_message) from error
    counts = (text.count(before), text.count(after))
    if counts == (1, 0):
        block = before
    elif counts == (0, 1):
        block = after
    else:
        raise SystemExit(error_message)
    remainder = text.replace(block, "", 1)
    if "LexicalRecord" in remainder or "C++17 [class.nest]/4" in remainder:
        raise SystemExit(error_message)
    if block == before:
        path.write_text(text.replace(before, after, 1), encoding="utf-8")


# Only the two Setup discovery outputs in the pinned MSVCPaths.cpp need this
# owner. Keep it in the private llvm namespace and leave the host SDK intact.
SETUP_BSTR_OWNER = """namespace llvm {
class NeverCSetupBstr final {
  BSTR Value = nullptr;

public:
  NeverCSetupBstr() noexcept = default;
  NeverCSetupBstr(const NeverCSetupBstr &) = delete;
  NeverCSetupBstr &operator=(const NeverCSetupBstr &) = delete;
  NeverCSetupBstr(NeverCSetupBstr &&) = delete;
  NeverCSetupBstr &operator=(NeverCSetupBstr &&) = delete;
  ~NeverCSetupBstr() noexcept { reset(); }

  void reset() noexcept {
    ::SysFreeString(Value);
    Value = nullptr;
  }
  BSTR *out() noexcept {
    reset();
    return &Value;
  }
  BSTR get() const noexcept { return Value; }
};
} // namespace llvm
"""


def isolate_setup_bstr(path):
    preamble = """#ifdef _MSC_VER
// Don't support SetupApi on MinGW.
#define USE_MSVC_SETUP_API

// Make sure this comes before MSVCSetupApi.h
#include <comdef.h>

#include "llvm/Support/COM.h"
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnon-virtual-dtor"
#endif
#include "llvm/WindowsDriver/MSVCSetupApi.h"
#ifdef __clang__
#pragma clang diagnostic pop
#endif
_COM_SMARTPTR_TYPEDEF(ISetupConfiguration, __uuidof(ISetupConfiguration));
_COM_SMARTPTR_TYPEDEF(ISetupConfiguration2, __uuidof(ISetupConfiguration2));
_COM_SMARTPTR_TYPEDEF(ISetupHelper, __uuidof(ISetupHelper));
_COM_SMARTPTR_TYPEDEF(IEnumSetupInstances, __uuidof(IEnumSetupInstances));
_COM_SMARTPTR_TYPEDEF(ISetupInstance, __uuidof(ISetupInstance));
_COM_SMARTPTR_TYPEDEF(ISetupInstance2, __uuidof(ISetupInstance2));
#endif"""
    version_before = """  do {
    bstr_t VersionString;
    uint64_t VersionNum;
    HR = Instance->GetInstallationVersion(VersionString.GetAddress());
    if (FAILED(HR))
      continue;
    HR = ISetupHelperPtr(Query)->ParseVersion(VersionString, &VersionNum);
    if (FAILED(HR))
      continue;
    if (!NewestVersionNum || (VersionNum > NewestVersionNum)) {
      NewestInstance = Instance;
      NewestVersionNum = VersionNum;
    }
  } while ((HR = EnumInstances->Next(1, &Instance, nullptr)) == S_OK);"""
    version_after = """  do {
    NeverCSetupBstr VersionString;
    uint64_t VersionNum;
    HR = Instance->GetInstallationVersion(VersionString.out());
    if (FAILED(HR) || !VersionString.get())
      continue;
    HR = ISetupHelperPtr(Query)->ParseVersion(VersionString.get(), &VersionNum);
    if (FAILED(HR))
      continue;
    if (!NewestVersionNum || (VersionNum > NewestVersionNum)) {
      NewestInstance = Instance;
      NewestVersionNum = VersionNum;
    }
  } while ((HR = EnumInstances->Next(1, &Instance, nullptr)) == S_OK);"""
    path_before = """  bstr_t VCPathWide;
  HR = NewestInstance->ResolvePath(L"VC", VCPathWide.GetAddress());
  if (FAILED(HR))
    return false;

  std::string VCRootPath;
  convertWideToUTF8(std::wstring(VCPathWide), VCRootPath);"""
    path_after = """  NeverCSetupBstr VCPathWide;
  HR = NewestInstance->ResolvePath(L"VC", VCPathWide.out());
  if (FAILED(HR) || !VCPathWide.get())
    return false;

  std::string VCRootPath;
  convertWideToUTF8(std::wstring(VCPathWide.get()), VCRootPath);"""
    replacements = (
        (preamble, preamble[:-len("#endif")] + "\n" + SETUP_BSTR_OWNER + "#endif"),
        (version_before, version_after),
        (path_before, path_after),
    )
    error_message = "Unexpected pinned LLVM Setup BSTR source in " + str(path)
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        raise SystemExit(error_message) from error
    counts = [(text.count(before), text.count(after))
              for before, after in replacements]
    if all(count == (1, 0) for count in counts):
        state = 0
    elif all(count == (0, 1) for count in counts):
        state = 1
    else:
        raise SystemExit(error_message)

    # Validate the insertion guard and both complete call blocks together.
    # Reject additional or partially rewritten uses, even when the three
    # expected blocks themselves still match. All checks precede the source write.
    remainder = text
    for pair in replacements:
        remainder = remainder.replace(pair[state], "", 1)
    if re.search(r"\b(?:bstr_t|_bstr_t|NeverCSetupBstr)\b|\.GetAddress\s*\(", remainder):
        raise SystemExit(error_message)
    if state == 0:
        for before, after in replacements:
            text = text.replace(before, after, 1)
        path.write_text(text, encoding="utf-8")


# Check Setup source before any other patch writes. Later unrelated failures
# do not roll back previously completed patches.
isolate_setup_bstr(args.source / "llvm/lib/WindowsDriver/MSVCPaths.cpp")
preserve_explicit_function_instantiation_source(args.source)
fix_nested_friend_access(args.source / "clang/lib/Sema/SemaAccess.cpp")
fix_nested_friend_declaration_access(args.source)
fix_imported_namespace_defaults(args.source)
fix_deduced_reference_conversions(args.source / "clang/lib/Sema/SemaInit.cpp")
fix_deduced_reference_arguments(args.source / "clang/lib/Sema/SemaOverload.cpp")

intrinsics = args.source / "llvm/lib/IR/IntrinsicInst.cpp"
for before, after in [
    ("constexpr bool isVPIntrinsic(", "constexpr bool neverc_cpp_isVPIntrinsic("),
    ("return ::isVPIntrinsic(", "return ::neverc_cpp_isVPIntrinsic("),
    ("if (::isVPIntrinsic(", "if (::neverc_cpp_isVPIntrinsic("),
]:
    replace_once(intrinsics, before, after)
debugify = args.source / "llvm/include/llvm/Transforms/Utils/Debugify.h"
replace_once(debugify, "#define LLVM_TRANSFORMS_UTILS_DEBUGIFY_H",
             "#define LLVM_TRANSFORMS_UTILS_DEBUGIFY_H\n"
             "// Private NeverC frontend ABI: this upstream type is global.\n"
             "#define DebugInfoPerPass neverc_cpp_DebugInfoPerPass")
isolate_pointer_bounds(args.source / "llvm/lib/Transforms/Utils/LoopUtils.cpp")
isolate_math_calls(args.source)

symbols = set()
for header in sorted((args.source / "llvm/include/llvm-c").glob("*.h")):
    source = header.read_text(encoding="utf-8")
    symbols.update(re.findall(r"\b(LLVM[A-Z][A-Za-z0-9_]*)\s*\(", source))
    symbols.update(re.findall(r"\bllvm_blake3_[A-Za-z0-9_]+\b", source))

# BLAKE3's public and internal APIs include architecture-specific names. Scan
# the upstream prefix table so one inventory covers all supported build hosts.
blake = args.source / "llvm/lib/Support/BLAKE3/llvm_blake3_prefix.h"
symbols.update(re.findall(r"\bllvm_blake3_[A-Za-z0-9_]+\b", blake.read_text(encoding="utf-8")))
core = (args.source / "llvm/include/llvm-c/Core.h").read_text(encoding="utf-8")
classes = core.split("#define LLVM_FOR_EACH_VALUE_SUBCLASS(macro)", 1)[1].split("\n\n", 1)[0]
symbols.update("LLVMIsA" + name for name in re.findall(r"\bmacro\((\w+)\)", classes))
symbols.update({
    "llvm_regcomp", "llvm_regerror", "llvm_regexec", "llvm_regfree", "llvm_strlcpy",
    "UseNewDbgInfoFormat", "WriteNewDbgInfoFormat", "WriteNewDbgInfoFormatToBitcode",
    "WriteNewDbgInfoFormatToBitcode2", "PreserveInputDbgFormat", "UseDerefAtPointSemantics",
    "__crashreporter_info__",
    # Windows Signals.inc defines/registers this with C linkage inside llvm.
    # Renaming the namespace alone leaves its process-wide symbol unchanged.
    "HandleAbort",
    # UCRT's selectany default floating-point environment is a value object.
    # Prefix its header definition and FE_DFL_ENV references together; the CRT
    # fesetenv/feclearexcept/fetestexcept entry points retain their normal ABI.
    "_Fenv1",
    # shlguid.h defines these immutable GUID values with C linkage and selectany.
    # Keep the SDK layout/initializers and rewrite identifier uses, including
    # expansion of the SID_SUrlHistory alias, into the private frontend ABI.
    "CLSID_CUrlHistory", "CLSID_CUrlHistoryBoth",
})
if len(symbols) < 900:
    raise SystemExit("Unexpected LLVM 20.1.8 symbol inventory; review the pinned source")
text = [
    "// Generated for the SHA256-pinned LLVM 20.1.8 source. Do not edit.",
    "#ifndef NEVERC_CPP_PRIVATE_PREFIX_H",
    "#define NEVERC_CPP_PRIVATE_PREFIX_H",
    "#define llvm neverc_cpp_llvm",
]
text.extend(f"#define {name} neverc_cpp_{name}" for name in sorted(symbols))
text.append("#endif\n")
result = "\n".join(text)
args.output.parent.mkdir(parents=True, exist_ok=True)
if not args.output.exists() or args.output.read_text(encoding="utf-8") != result:
    args.output.write_text(result, encoding="utf-8")

# These upstream Support sources carry additional notices in their initial
# comment blocks. Preserve the exact text beside the project/BLAKE3 licenses.
notice_files = [
    "llvm/lib/Support/MD5.cpp", "llvm/lib/Support/xxhash.cpp",
    "llvm/lib/Support/UnicodeNameToCodepointGenerated.cpp",
    "llvm/lib/Support/ConvertUTF.cpp", "llvm/lib/Support/regex2.h",
    "llvm/lib/Support/regutils.h", "llvm/lib/Support/regex_impl.h",
    "llvm/lib/Support/regcomp.c", "llvm/lib/Support/regexec.c",
    "llvm/lib/Support/regerror.c", "llvm/lib/Support/regfree.c",
]
notices = ["Additional notices from the pinned LLVM 20.1.8 sources.\n"]
for name in notice_files:
    source = (args.source / name).read_text(encoding="utf-8")
    prefix = re.match(r"(?:\s+|/\*.*?\*/|//[^\n]*(?:\n|$))*", source, re.S)[0]
    if not prefix.strip():
        raise SystemExit("Missing expected upstream notice: " + name)
    notices.extend(["\n===== " + name + " =====\n", prefix])
notice_path = args.output.parent / "NeverCCppThirdPartyNotices.txt"
notice_text = "\n".join(notices)
if not notice_path.exists() or notice_path.read_text(encoding="utf-8") != notice_text:
    notice_path.write_text(notice_text, encoding="utf-8")
print(f"Isolated llvm namespace and {len(symbols)} C/global identifiers")
