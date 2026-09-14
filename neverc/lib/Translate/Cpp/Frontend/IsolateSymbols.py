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
             '  class FunctionDecl;\n  class ImportDecl;\n  class TemplateArgumentListInfo;\n  class TypeSourceInfo;\n  struct DeclarationNameInfo;\n  class NestedNameSpecifierLoc;\n  class SourceLocation;\n  class TemplateDecl;\n  class NonTypeTemplateParmDecl;\n  class TemplateArgumentLoc;\n  class TemplateArgument;\n  class Type;\n  class NamedDecl;\n  class ClassTemplateSpecializationDecl;\n  class ClassTemplatePartialSpecializationDecl;\n  class TemplateArgumentList;\n  class VarTemplateSpecializationDecl;\n  class VarTemplatePartialSpecializationDecl;\n  class Expr;\n  class FriendDecl;\n  class FunctionTemplateDecl;\n  class ClassTemplateDecl;\n  class DeclContext;'),
            ('  virtual void HandleCXXImplicitFunctionInstantiation(FunctionDecl *D) {}',
             '  virtual void HandleCXXImplicitFunctionInstantiation(FunctionDecl *D) {}\n\n  // NeverC private source evidence; this does not request instantiation.\n  virtual void HandleNeverCExplicitFunctionInstantiation(\n      FunctionDecl *, const TemplateArgumentListInfo &, TypeSourceInfo *,\n      const DeclarationNameInfo &, const NestedNameSpecifierLoc &,\n      const SourceLocation &, bool) {}'),
            ('  virtual void HandleCXXStaticMemberVarInstantiation(VarDecl *D) {}',
             '  virtual void HandleCXXStaticMemberVarInstantiation(VarDecl *D) {}\n\n  // NeverC private source evidence for each static member directive.\n  virtual void HandleNeverCExplicitStaticDataInstantiation(\n      VarDecl *, TypeSourceInfo *, const NestedNameSpecifierLoc &,\n      const SourceLocation &, bool) {}\n\n  // NeverC retains defaults only after successful argument conversion.\n  virtual void HandleNeverCScalarTemplateDefault(\n      TemplateDecl *, NonTypeTemplateParmDecl *,\n      const TemplateArgumentLoc &, const TemplateArgumentLoc &,\n      const TemplateArgument &, const SourceLocation &) {}\n\n  // NeverC source preservation is opt-in; other consumers keep upstream ASTs.\n  virtual bool wantsNeverCTemplateSource() const { return false; }\n  virtual void HandleNeverCTemplateTypeSource(\n      TemplateDecl *, const Type *, TypeSourceInfo *,\n      const TemplateArgumentListInfo &,\n      const TemplateArgument *, const TemplateArgument *, unsigned,\n      NamedDecl *const *, const TemplateArgumentLoc *, const TemplateArgumentLoc *,\n      unsigned, NamedDecl *const *, TypeSourceInfo *const *,\n      const unsigned *, unsigned, bool, const SourceLocation &) {}\n  virtual void HandleNeverCFunctionTemplateSource(\n      FunctionDecl *,\n      const TemplateArgument *, const TemplateArgument *, unsigned,\n      NamedDecl *const *, const TemplateArgumentLoc *, const TemplateArgumentLoc *,\n      unsigned, NamedDecl *const *, TypeSourceInfo *const *,\n      const unsigned *, unsigned, bool, const SourceLocation &) {}\n  virtual void HandleNeverCClassTemplateSource(\n      ClassTemplateSpecializationDecl *, const TemplateArgumentListInfo &, bool,\n      const TemplateArgument *, const TemplateArgument *, unsigned,\n      NamedDecl *const *, const TemplateArgumentLoc *, const TemplateArgumentLoc *,\n      unsigned, NamedDecl *const *, TypeSourceInfo *const *,\n      const unsigned *, unsigned, bool, const SourceLocation &) {}\n  virtual void HandleNeverCFunctionSpecializationSource(\n      FunctionDecl *, FunctionDecl *, const TemplateArgumentListInfo *,\n      const SourceLocation &) {}\n  // The exact deduced list later identifies the selected partial candidate.\n  virtual void HandleNeverCClassPartialSource(\n      ClassTemplatePartialSpecializationDecl *, const TemplateArgumentList *,\n      bool, const TemplateArgumentListInfo *,\n      const TemplateArgument *, const TemplateArgument *, unsigned,\n      NamedDecl *const *, const TemplateArgumentLoc *, const TemplateArgumentLoc *,\n      unsigned, NamedDecl *const *, TypeSourceInfo *const *,\n      const unsigned *, unsigned, bool, const SourceLocation &) {}\n  virtual void HandleNeverCVariableTemplateSource(\n      VarTemplateSpecializationDecl *, const TemplateArgumentListInfo &,\n      TypeSourceInfo *, bool, bool,\n      const TemplateArgument *, const TemplateArgument *, unsigned,\n      NamedDecl *const *, const TemplateArgumentLoc *, const TemplateArgumentLoc *,\n      unsigned, NamedDecl *const *, TypeSourceInfo *const *,\n      const unsigned *, unsigned, bool, const SourceLocation &) {}\n  virtual void HandleNeverCVariablePartialSource(\n      VarTemplatePartialSpecializationDecl *, const TemplateArgumentList *,\n      bool, const TemplateArgumentListInfo *,\n      const TemplateArgument *, const TemplateArgument *, unsigned,\n      NamedDecl *const *, const TemplateArgumentLoc *, const TemplateArgumentLoc *,\n      unsigned, NamedDecl *const *, TypeSourceInfo *const *,\n      const unsigned *, unsigned, bool, const SourceLocation &) {}\n  // Successful declaration checks are independent of later partial selection.\n  virtual void HandleNeverCPartialDeclarationSource(\n      NamedDecl *, NamedDecl *, const TemplateArgumentListInfo &,\n      const TemplateArgument *, const TemplateArgument *, unsigned,\n      NamedDecl *const *, const TemplateArgumentLoc *, const TemplateArgumentLoc *,\n      unsigned, NamedDecl *const *, TypeSourceInfo *const *,\n      const unsigned *, unsigned, bool, const SourceLocation &) {}\n  // Class-scope full copies retain the actual successful declaration check.\n  virtual void HandleNeverCClassFullDeclarationSource(\n      ClassTemplateSpecializationDecl *, ClassTemplateSpecializationDecl *,\n      const TemplateArgumentListInfo &,\n      const TemplateArgument *, const TemplateArgument *, unsigned,\n      NamedDecl *const *, const TemplateArgumentLoc *, const TemplateArgumentLoc *,\n      unsigned, NamedDecl *const *, TypeSourceInfo *const *,\n      const unsigned *, unsigned, bool, const SourceLocation &) {}\n  // Ordinary member-class directives otherwise have no separate AST node.\n  virtual void HandleNeverCExplicitMemberClassInstantiation(\n      CXXRecordDecl *, CXXRecordDecl *, const NestedNameSpecifierLoc &,\n      const SourceLocation &, const SourceLocation &, const SourceLocation &,\n      bool) {}\n  // Keep friend spelling separate from the selected signature/body source.\n  virtual void HandleNeverCFriendFunctionSource(\n      FunctionDecl *, FunctionDecl *, FunctionDecl *, CXXRecordDecl *) {}\n  virtual void HandleNeverCFriendDeclarationSource(FriendDecl *, FriendDecl *) {}\n  // Copying an outer class creates a primary, not an inner specialization.\n  virtual void HandleNeverCFriendFunctionTemplateSource(\n      FunctionTemplateDecl *, FunctionDecl *, FunctionDecl *, CXXRecordDecl *) {}\n  // Exact compatible definition context selected by existing Sema control flow.\n  virtual void HandleNeverCFunctionTemplateBodySource(\n      FunctionDecl *, FunctionTemplateDecl *, const FunctionDecl *, DeclContext *) {}\n  // Successful class-friend lookup and redeclaration merge, before return.\n  virtual void HandleNeverCFriendClassTemplateSource(\n      ClassTemplateDecl *, ClassTemplateDecl *, CXXRecordDecl *,\n      DeclContext *, ClassTemplateDecl *) {}\n  virtual void HandleNeverCVariableTypeSource(\n      VarTemplateSpecializationDecl *, VarTemplateSpecializationDecl *,\n      VarDecl *, TypeSourceInfo *, bool,\n      const SourceLocation &) {}\n  // Actual successful expression and original selection location, before wrappers.\n  virtual void HandleNeverCSelectedTemplateCallSource(\n      Expr *, FunctionDecl *, const SourceLocation &) {}'),
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
             '  Specialization->setInvalidDecl(Invalid);\n  inferGslOwnerPointerAttribute(Specialization);\n  if (Consumer.wantsNeverCTemplateSource() && !Invalid &&\n      !isPartialSpecialization && TUK != TagUseKind::Friend)\n    Consumer.HandleNeverCClassTemplateSource(\n        Specialization, TemplateArgs, /*Instantiation=*/false,\n        CTAI.CanonicalConverted.data(), CTAI.SugaredConverted.data(),\n        CTAI.CanonicalConverted.size(), CTAI.NeverCDefaultParameters.data(),\n        CTAI.NeverCWrittenDefaults.data(), CTAI.NeverCConvertedDefaults.data(),\n        CTAI.NeverCDefaultParameters.size(), CTAI.NeverCTypeParameters.data(),\n        CTAI.NeverCParameterTypes.data(), CTAI.NeverCParameterPackIndices.data(),\n        CTAI.NeverCTypeParameters.size(), CTAI.NeverCDefaultsOverflow, TemplateNameLoc);\n  if (Consumer.wantsNeverCTemplateSource() && !Invalid &&\n      isPartialSpecialization && TUK != TagUseKind::Friend)\n    Consumer.HandleNeverCPartialDeclarationSource(\n        Specialization, nullptr, TemplateArgs,\n        CTAI.CanonicalConverted.data(), CTAI.SugaredConverted.data(),\n        CTAI.CanonicalConverted.size(), CTAI.NeverCDefaultParameters.data(),\n        CTAI.NeverCWrittenDefaults.data(), CTAI.NeverCConvertedDefaults.data(),\n        CTAI.NeverCDefaultParameters.size(), CTAI.NeverCTypeParameters.data(),\n        CTAI.NeverCParameterTypes.data(), CTAI.NeverCParameterPackIndices.data(),\n        CTAI.NeverCTypeParameters.size(), CTAI.NeverCDefaultsOverflow,\n        Specialization->getLocation());\n  return Specialization;'),
            ('  // Syntax is now OK, so return if it has no other effect on semantics.\n  if (HasNoEffect) {',
             '  // Preserve each declaration, including a no-effect repeated instantiation.\n  if (Consumer.wantsNeverCTemplateSource())\n    Consumer.HandleNeverCClassTemplateSource(\n        Specialization, TemplateArgs, /*Instantiation=*/true,\n        CTAI.CanonicalConverted.data(), CTAI.SugaredConverted.data(),\n        CTAI.CanonicalConverted.size(), CTAI.NeverCDefaultParameters.data(),\n        CTAI.NeverCWrittenDefaults.data(), CTAI.NeverCConvertedDefaults.data(),\n        CTAI.NeverCDefaultParameters.size(), CTAI.NeverCTypeParameters.data(),\n        CTAI.NeverCParameterTypes.data(), CTAI.NeverCParameterPackIndices.data(),\n        CTAI.NeverCTypeParameters.size(), CTAI.NeverCDefaultsOverflow, TemplateNameLoc);\n\n  // Syntax is now OK, so return if it has no other effect on semantics.\n  if (HasNoEffect) {'),
            ('  Previous.clear();\n  Previous.addDecl(Specialization);\n  return false;',
             '  Previous.clear();\n  Previous.addDecl(Specialization);\n  if (Consumer.wantsNeverCTemplateSource())\n    Consumer.HandleNeverCFunctionSpecializationSource(\n        FD, Specialization,\n        ExplicitTemplateArgs ? &ConvertedTemplateArgs[Specialization] : nullptr,\n        FD->getLocation());\n  return false;'),
            ('    QualType NTTPType = NTTP->getType();\n    if (NTTP->isParameterPack() && NTTP->isExpandedParameterPack())\n      NTTPType = NTTP->getExpansionType(ArgumentPackIndex);\n\n    if (NTTPType->isInstantiationDependentType() &&\n        !isa<TemplateTemplateParmDecl>(Template) &&\n        !Template->getDeclContext()->isDependentContext()) {\n      // Do substitution on the type of the non-type template parameter.\n      InstantiatingTemplate Inst(*this, TemplateLoc, Template, NTTP,\n                                 CTAI.SugaredConverted,\n                                 SourceRange(TemplateLoc, RAngleLoc));\n      if (Inst.isInvalid())\n        return true;\n\n      MultiLevelTemplateArgumentList MLTAL(Template, CTAI.SugaredConverted,\n                                           /*Final=*/true);\n      // If the parameter is a pack expansion, expand this slice of the pack.\n      if (auto *PET = NTTPType->getAs<PackExpansionType>()) {\n        Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(*this,\n                                                           ArgumentPackIndex);\n        NTTPType = SubstType(PET->getPattern(), MLTAL, NTTP->getLocation(),\n                             NTTP->getDeclName());\n      } else {\n        NTTPType = SubstType(NTTPType, MLTAL, NTTP->getLocation(),\n                             NTTP->getDeclName());\n      }\n\n      // If that worked, check the non-type template parameter type\n      // for validity.\n      if (!NTTPType.isNull())\n        NTTPType = CheckNonTypeTemplateParameterType(NTTPType,\n                                                     NTTP->getLocation());\n      if (NTTPType.isNull())\n        return true;\n    }\n\n',
             '    QualType NTTPType = NTTP->getType();\n    TypeSourceInfo *NeverCParameterTypeSource =\n        Consumer.wantsNeverCTemplateSource() ? NTTP->getTypeSourceInfo() : nullptr;\n    if (NTTP->isParameterPack() && NTTP->isExpandedParameterPack())\n      NTTPType = NTTP->getExpansionType(ArgumentPackIndex);\n    if (Consumer.wantsNeverCTemplateSource() && NTTP->isParameterPack() &&\n        NTTP->isExpandedParameterPack())\n      NeverCParameterTypeSource = NTTP->getExpansionTypeSourceInfo(ArgumentPackIndex);\n\n    if (NTTPType->isInstantiationDependentType() &&\n        !isa<TemplateTemplateParmDecl>(Template) &&\n        !Template->getDeclContext()->isDependentContext()) {\n      // Do substitution on the type of the non-type template parameter.\n      InstantiatingTemplate Inst(*this, TemplateLoc, Template, NTTP,\n                                 CTAI.SugaredConverted,\n                                 SourceRange(TemplateLoc, RAngleLoc));\n      if (Inst.isInvalid())\n        return true;\n\n      MultiLevelTemplateArgumentList MLTAL(Template, CTAI.SugaredConverted,\n                                           /*Final=*/!Consumer.wantsNeverCTemplateSource());\n      // If the parameter is a pack expansion, expand this slice of the pack.\n      if (auto *PET = NTTPType->getAs<PackExpansionType>()) {\n        Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(*this,\n                                                           ArgumentPackIndex);\n        if (NeverCParameterTypeSource) {\n          auto NeverCPattern = NeverCParameterTypeSource->getTypeLoc()\n                                   .getAs<PackExpansionTypeLoc>();\n          if (NeverCPattern) {\n            auto NeverCPatternLoc = NeverCPattern.getPatternLoc();\n            NeverCParameterTypeSource = Context.CreateTypeSourceInfo(PET->getPattern());\n            NeverCParameterTypeSource->getTypeLoc().initializeFullCopy(NeverCPatternLoc);\n          } else {\n            NeverCParameterTypeSource = nullptr;\n          }\n        }\n        if (NeverCParameterTypeSource) {\n          NeverCParameterTypeSource = SubstType(NeverCParameterTypeSource, MLTAL,\n                                               NTTP->getLocation(), NTTP->getDeclName());\n          NTTPType = NeverCParameterTypeSource ? NeverCParameterTypeSource->getType() : QualType();\n        } else {\n          NTTPType = SubstType(PET->getPattern(), MLTAL, NTTP->getLocation(),\n                               NTTP->getDeclName());\n        }\n      } else {\n        if (NeverCParameterTypeSource) {\n          NeverCParameterTypeSource = SubstType(NeverCParameterTypeSource, MLTAL,\n                                               NTTP->getLocation(), NTTP->getDeclName());\n          NTTPType = NeverCParameterTypeSource ? NeverCParameterTypeSource->getType() : QualType();\n        } else {\n          NTTPType = SubstType(NTTPType, MLTAL, NTTP->getLocation(),\n                               NTTP->getDeclName());\n        }\n      }\n\n      // If that worked, check the non-type template parameter type\n      // for validity.\n      if (!NTTPType.isNull())\n        NTTPType = CheckNonTypeTemplateParameterType(NTTPType,\n                                                     NTTP->getLocation());\n      if (NTTPType.isNull())\n        return true;\n    }\n\n'),
            ('    case TemplateArgument::Pack:\n      llvm_unreachable("Caller must expand template argument packs");\n    }\n\n    return false;',
             '    case TemplateArgument::Pack:\n      llvm_unreachable("Caller must expand template argument packs");\n    }\n\n    if (Consumer.wantsNeverCTemplateSource())\n      CTAI.retainNeverCParameterType(NTTP, NeverCParameterTypeSource, ArgumentPackIndex);\n    return false;'),
            ('  // Find the variable template specialization declaration that\n  // corresponds to these arguments.\n  void *InsertPos = nullptr;\n  if (VarTemplateSpecializationDecl *Spec =\n          Template->findSpecialization(CTAI.CanonicalConverted, InsertPos)) {',
             '  // Preserve every concrete use, including a fresh spelling of a cached id.\n  auto NeverCRetainVariable = [&](VarTemplateSpecializationDecl *Spec) {\n    if (Spec && !Spec->isInvalidDecl() && Consumer.wantsNeverCTemplateSource())\n      Consumer.HandleNeverCVariableTemplateSource(\n          Spec, TemplateArgs, nullptr, false, false,\n          CTAI.CanonicalConverted.data(), CTAI.SugaredConverted.data(),\n          CTAI.CanonicalConverted.size(), CTAI.NeverCDefaultParameters.data(),\n          CTAI.NeverCWrittenDefaults.data(), CTAI.NeverCConvertedDefaults.data(),\n          CTAI.NeverCDefaultParameters.size(), CTAI.NeverCTypeParameters.data(),\n          CTAI.NeverCParameterTypes.data(), CTAI.NeverCParameterPackIndices.data(),\n          CTAI.NeverCTypeParameters.size(), CTAI.NeverCDefaultsOverflow,\n          TemplateNameLoc);\n  };\n\n  // Find the variable template specialization declaration that\n  // corresponds to these arguments.\n  void *InsertPos = nullptr;\n  if (VarTemplateSpecializationDecl *Spec =\n          Template->findSpecialization(CTAI.CanonicalConverted, InsertPos)) {'),
            ('    // If we already have a variable template specialization, return it.\n    return Spec;',
             '    // If we already have a variable template specialization, return it.\n    NeverCRetainVariable(Spec);\n    return Spec;'),
            ('  assert(Decl && "No variable template specialization?");\n  return Decl;',
             '  assert(Decl && "No variable template specialization?");\n  NeverCRetainVariable(Decl);\n  return Decl;'),
            ('  return Specialization;\n}\n\nnamespace {\n/// A partial specialization whose template arguments have matched',
             '  if (!IsPartialSpecialization && !Specialization->isInvalidDecl() &&\n      Consumer.wantsNeverCTemplateSource())\n    Consumer.HandleNeverCVariableTemplateSource(\n        Specialization, TemplateArgs, DI, true,\n        D.getDeclSpec().getStorageClassSpec() != DeclSpec::SCS_unspecified,\n        CTAI.CanonicalConverted.data(), CTAI.SugaredConverted.data(),\n        CTAI.CanonicalConverted.size(), CTAI.NeverCDefaultParameters.data(),\n        CTAI.NeverCWrittenDefaults.data(), CTAI.NeverCConvertedDefaults.data(),\n        CTAI.NeverCDefaultParameters.size(), CTAI.NeverCTypeParameters.data(),\n        CTAI.NeverCParameterTypes.data(), CTAI.NeverCParameterPackIndices.data(),\n        CTAI.NeverCTypeParameters.size(), CTAI.NeverCDefaultsOverflow,\n        TemplateNameLoc);\n\n  if (IsPartialSpecialization && !Specialization->isInvalidDecl() &&\n      Consumer.wantsNeverCTemplateSource())\n    Consumer.HandleNeverCPartialDeclarationSource(\n        Specialization, nullptr, TemplateArgs,\n        CTAI.CanonicalConverted.data(), CTAI.SugaredConverted.data(),\n        CTAI.CanonicalConverted.size(), CTAI.NeverCDefaultParameters.data(),\n        CTAI.NeverCWrittenDefaults.data(), CTAI.NeverCConvertedDefaults.data(),\n        CTAI.NeverCDefaultParameters.size(), CTAI.NeverCTypeParameters.data(),\n        CTAI.NeverCParameterTypes.data(), CTAI.NeverCParameterPackIndices.data(),\n        CTAI.NeverCTypeParameters.size(), CTAI.NeverCDefaultsOverflow,\n        Specialization->getLocation());\n\n  return Specialization;\n}\n\nnamespace {\n/// A partial specialization whose template arguments have matched'),
            ('    if (HasNoEffect)\n      return TagD;\n  }\n\n  CXXRecordDecl *RecordDef',
             '    if (HasNoEffect) {\n      if (!Record->isInvalidDecl() &&\n          getASTConsumer().wantsNeverCTemplateSource())\n        getASTConsumer().HandleNeverCExplicitMemberClassInstantiation(\n            Record, Pattern, SS.getWithLocInContext(Context),\n            NameLoc, TemplateLoc, ExternLoc, !Attr.empty());\n      return TagD;\n    }\n  }\n\n  CXXRecordDecl *RecordDef'),
            ("  // FIXME: We don't have any representation for explicit instantiations of\n  // member classes. Such a representation is not needed for compilation, but it\n  // should be available for clients that want to see all of the declarations in\n  // the source code.\n  return TagD;\n}",
             "  if (!Record->isInvalidDecl() &&\n      getASTConsumer().wantsNeverCTemplateSource())\n    getASTConsumer().HandleNeverCExplicitMemberClassInstantiation(\n        Record, Pattern, SS.getWithLocInContext(Context),\n        NameLoc, TemplateLoc, ExternLoc, !Attr.empty());\n  // FIXME: We don't have any representation for explicit instantiations of\n  // member classes. Such a representation is not needed for compilation, but it\n  // should be available for clients that want to see all of the declarations in\n  // the source code.\n  return TagD;\n}"),
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
            ('  if (S.SubstTemplateArguments(\n          PartialTemplArgInfo->arguments(),\n          MultiLevelTemplateArgumentList(Partial, CTAI.SugaredConverted,\n                                         /*Final=*/true),\n          InstArgs)) {',
             '  if (S.SubstTemplateArguments(\n          PartialTemplArgInfo->arguments(),\n          MultiLevelTemplateArgumentList(Partial, CTAI.SugaredConverted,\n                                         /*Final=*/!(isa<ClassTemplatePartialSpecializationDecl,\n                                                         VarTemplatePartialSpecializationDecl>(Partial) &&\n                                                     !IsPartialOrdering &&\n                                                     S.getASTConsumer().wantsNeverCTemplateSource())),\n          InstArgs)) {'),
            ('  if (!IsPartialOrdering) {\n    if (auto Result = CheckDeducedArgumentConstraints(\n            S, Partial, CTAI.SugaredConverted, CTAI.CanonicalConverted, Info);\n        Result != TemplateDeductionResult::Success)\n      return Result;\n  }\n\n  return TemplateDeductionResult::Success;\n}\n\n/// Complete template argument deduction for a class or variable template,',
             '  if (!IsPartialOrdering) {\n    if (auto Result = CheckDeducedArgumentConstraints(\n            S, Partial, CTAI.SugaredConverted, CTAI.CanonicalConverted, Info);\n        Result != TemplateDeductionResult::Success)\n      return Result;\n  }\n\n  if (auto *NeverCPartial = dyn_cast<ClassTemplatePartialSpecializationDecl>(Partial);\n      NeverCPartial && !IsPartialOrdering && !Trap.hasErrorOccurred() &&\n      S.getASTConsumer().wantsNeverCTemplateSource()) {\n    auto NeverCRetainPartial = [&](bool Pattern,\n                                   const TemplateArgumentListInfo *Written,\n                                   const Sema::CheckTemplateArgumentInfo &Checked) {\n      S.getASTConsumer().HandleNeverCClassPartialSource(\n          NeverCPartial, CanonicalDeducedArgumentList, Pattern, Written,\n          Checked.CanonicalConverted.data(), Checked.SugaredConverted.data(),\n          Checked.CanonicalConverted.size(), Checked.NeverCDefaultParameters.data(),\n          Checked.NeverCWrittenDefaults.data(), Checked.NeverCConvertedDefaults.data(),\n          Checked.NeverCDefaultParameters.size(), Checked.NeverCTypeParameters.data(),\n          Checked.NeverCParameterTypes.data(), Checked.NeverCParameterPackIndices.data(),\n          Checked.NeverCTypeParameters.size(), Checked.NeverCDefaultsOverflow,\n          Info.getLocation());\n    };\n    NeverCRetainPartial(false, nullptr, CTAI);\n    NeverCRetainPartial(true, &InstArgs, InstCTAI);\n  }\n\n  if (auto *NeverCVariablePartial = dyn_cast<VarTemplatePartialSpecializationDecl>(Partial);\n      NeverCVariablePartial && !IsPartialOrdering && !Trap.hasErrorOccurred() &&\n      S.getASTConsumer().wantsNeverCTemplateSource()) {\n    auto NeverCRetainVariablePartial = [&](bool Pattern,\n                                           const TemplateArgumentListInfo *Written,\n                                           const Sema::CheckTemplateArgumentInfo &Checked) {\n      // Variable selection transfers takeSugared(), unlike class selection.\n      S.getASTConsumer().HandleNeverCVariablePartialSource(\n          NeverCVariablePartial, SugaredDeducedArgumentList, Pattern, Written,\n          Checked.CanonicalConverted.data(), Checked.SugaredConverted.data(),\n          Checked.CanonicalConverted.size(), Checked.NeverCDefaultParameters.data(),\n          Checked.NeverCWrittenDefaults.data(), Checked.NeverCConvertedDefaults.data(),\n          Checked.NeverCDefaultParameters.size(), Checked.NeverCTypeParameters.data(),\n          Checked.NeverCParameterTypes.data(), Checked.NeverCParameterPackIndices.data(),\n          Checked.NeverCTypeParameters.size(), Checked.NeverCDefaultsOverflow,\n          Info.getLocation());\n    };\n    NeverCRetainVariablePartial(false, nullptr, CTAI);\n    NeverCRetainVariablePartial(true, &InstArgs, InstCTAI);\n  }\n\n  return TemplateDeductionResult::Success;\n}\n\n/// Complete template argument deduction for a class or variable template,'),
        )),
        ('clang/include/clang/Sema/Sema.h', (
            ('    SmallVector<TemplateArgument, 4> SugaredConverted, CanonicalConverted;\n',
             '    SmallVector<TemplateArgument, 4> SugaredConverted, CanonicalConverted;\n\n    // Keep defaults attached to this deduction, including ignored type args.\n    SmallVector<NamedDecl *, 4> NeverCDefaultParameters;\n    SmallVector<TemplateArgumentLoc, 4> NeverCWrittenDefaults, NeverCConvertedDefaults;\n    bool NeverCDefaultsOverflow = false;\n\n    void retainNeverCDefault(NamedDecl *Parameter,\n                            const TemplateArgumentLoc &Written,\n                            const TemplateArgumentLoc &Converted) {\n      if (NeverCDefaultParameters.size() == 64) {\n        NeverCDefaultsOverflow = true;\n        return;\n      }\n      NeverCDefaultParameters.push_back(Parameter);\n      NeverCWrittenDefaults.push_back(Written);\n      NeverCConvertedDefaults.push_back(Converted);\n    }\n\n    SmallVector<NamedDecl *, 4> NeverCTypeParameters;\n    SmallVector<TypeSourceInfo *, 4> NeverCParameterTypes;\n    SmallVector<unsigned, 4> NeverCParameterPackIndices;\n\n    void retainNeverCParameterType(NamedDecl *Parameter, TypeSourceInfo *Source,\n                                  unsigned PackIndex) {\n      if (NeverCTypeParameters.size() == 4096) {\n        NeverCDefaultsOverflow = true;\n        return;\n      }\n      NeverCTypeParameters.push_back(Parameter);\n      NeverCParameterTypes.push_back(Source);\n      NeverCParameterPackIndices.push_back(PackIndex);\n    }\n'),
        )),
        ('clang/include/clang/Sema/TemplateDeduction.h', (
            ('public:\n  TemplateDeductionInfo(SourceLocation Loc, unsigned DeducedDepth = 0)',
             'public:\n  // NeverC keeps preliminary explicit conversions on this exact candidate.\n  // reset/take retain it; a new explicit-substitution invocation clears it.\n  FunctionTemplateDecl *NeverCExplicitSourceTemplate = nullptr;\n  SmallVector<NamedDecl *, 4> NeverCExplicitTypeParameters;\n  SmallVector<TypeSourceInfo *, 4> NeverCExplicitParameterTypes;\n  SmallVector<unsigned, 4> NeverCExplicitPackIndices;\n  bool NeverCExplicitSourceOverflow = false;\n\n  void clearNeverCExplicitSource() {\n    NeverCExplicitSourceTemplate = nullptr;\n    NeverCExplicitTypeParameters.clear();\n    NeverCExplicitParameterTypes.clear();\n    NeverCExplicitPackIndices.clear();\n    NeverCExplicitSourceOverflow = false;\n  }\n\n  TemplateDeductionInfo(SourceLocation Loc, unsigned DeducedDepth = 0)'),
        )),
        ('clang/lib/Sema/SemaTemplateInstantiateDecl.cpp', (
            ('  SemaRef.BuildVariableInstantiation(Var, D, TemplateArgs, LateAttrs, Owner,\n                                     StartingScope, false, PrevDecl);\n\n  return Var;',
             '  SemaRef.BuildVariableInstantiation(Var, D, TemplateArgs, LateAttrs, Owner,\n                                     StartingScope, false, PrevDecl);\n\n  if (!Var->isInvalidDecl() && SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n    SemaRef.getASTConsumer().HandleNeverCVariableTypeSource(\n        Var, PrevDecl, D, DI, false, Var->getLocation());\n\n  return Var;'),
            ('  // Instantiate the initializer.\n  InstantiateVariableInitializer(VarSpec, PatternDecl, TemplateArgs);\n\n  if (getLangOpts().OpenCL)\n    deduceOpenCLAddressSpace(VarSpec);\n\n  return VarSpec;',
             '  // Instantiate the initializer.\n  InstantiateVariableInitializer(VarSpec, PatternDecl, TemplateArgs);\n\n  if (getLangOpts().OpenCL)\n    deduceOpenCLAddressSpace(VarSpec);\n\n  if (!VarSpec->isInvalidDecl() && Consumer.wantsNeverCTemplateSource())\n    Consumer.HandleNeverCVariableTypeSource(\n        VarSpec, nullptr, PatternDecl, DI, true, VarSpec->getLocation());\n\n  return VarSpec;'),
            ('    MultiLevelTemplateArgumentList TemplateArgs = getTemplateInstantiationArgs(\n        Function, DC, /*Final=*/false, Innermost, false, PatternDecl);\n\n    // Substitute into the qualifier; we can get a substitution failure here',
             "    MultiLevelTemplateArgumentList TemplateArgs = getTemplateInstantiationArgs(\n        Function, DC, /*Final=*/false, Innermost, false, PatternDecl);\n\n    // The definition's name-location copy above retains its generic type.\n    // Preserve this definition's concrete conversion type and written syntax.\n    if (Consumer.wantsNeverCTemplateSource() && isa<CXXConversionDecl>(Function)) {\n      auto NeverCConversionName =\n          SubstDeclarationNameInfo(PatternDecl->getNameInfo(), TemplateArgs);\n      if (!NeverCConversionName.getName()) {\n        Function->setInvalidDecl();\n        return;\n      }\n      Function->setDeclarationNameLoc(NeverCConversionName.getInfo());\n    }\n\n    // Substitute into the qualifier; we can get a substitution failure here"),
            ('  return VisitVarTemplateSpecializationDecl(InstVarTemplate, D,\n                                            VarTemplateArgsInfo,\n                                            CTAI.CanonicalConverted, PrevDecl);',
             '  Decl *NeverCMemberVariable = VisitVarTemplateSpecializationDecl(\n      InstVarTemplate, D, VarTemplateArgsInfo, CTAI.CanonicalConverted, PrevDecl);\n  if (auto *NeverCVariableFull =\n          dyn_cast_or_null<VarTemplateSpecializationDecl>(NeverCMemberVariable);\n      NeverCVariableFull && !NeverCVariableFull->isInvalidDecl() &&\n      SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n    SemaRef.getASTConsumer().HandleNeverCVariableTemplateSource(\n        NeverCVariableFull, VarTemplateArgsInfo,\n        NeverCVariableFull->getTypeSourceInfo(), /*Declaration=*/true,\n        /*WrittenStorageClass=*/false,\n        CTAI.CanonicalConverted.data(), CTAI.SugaredConverted.data(),\n        CTAI.CanonicalConverted.size(), CTAI.NeverCDefaultParameters.data(),\n        CTAI.NeverCWrittenDefaults.data(), CTAI.NeverCConvertedDefaults.data(),\n        CTAI.NeverCDefaultParameters.size(), CTAI.NeverCTypeParameters.data(),\n        CTAI.NeverCParameterTypes.data(), CTAI.NeverCParameterPackIndices.data(),\n        CTAI.NeverCTypeParameters.size(), CTAI.NeverCDefaultsOverflow,\n        NeverCVariableFull->getLocation());\n  return NeverCMemberVariable;'),
            ('  ClassTemplate->AddPartialSpecialization(InstPartialSpec,\n                                          /*InsertPos=*/nullptr);\n  return InstPartialSpec;',
             '  ClassTemplate->AddPartialSpecialization(InstPartialSpec,\n                                          /*InsertPos=*/nullptr);\n  if (!InstPartialSpec->isInvalidDecl() &&\n      SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n    SemaRef.getASTConsumer().HandleNeverCPartialDeclarationSource(\n        InstPartialSpec, PartialSpec, InstTemplateArgs,\n        CTAI.CanonicalConverted.data(), CTAI.SugaredConverted.data(),\n        CTAI.CanonicalConverted.size(), CTAI.NeverCDefaultParameters.data(),\n        CTAI.NeverCWrittenDefaults.data(), CTAI.NeverCConvertedDefaults.data(),\n        CTAI.NeverCDefaultParameters.size(), CTAI.NeverCTypeParameters.data(),\n        CTAI.NeverCParameterTypes.data(), CTAI.NeverCParameterPackIndices.data(),\n        CTAI.NeverCTypeParameters.size(), CTAI.NeverCDefaultsOverflow,\n        InstPartialSpec->getLocation());\n  return InstPartialSpec;'),
            ('  SemaRef.BuildVariableInstantiation(InstPartialSpec, PartialSpec, TemplateArgs,\n                                     LateAttrs, Owner, StartingScope);\n\n  return InstPartialSpec;',
             '  SemaRef.BuildVariableInstantiation(InstPartialSpec, PartialSpec, TemplateArgs,\n                                     LateAttrs, Owner, StartingScope);\n\n  if (!InstPartialSpec->isInvalidDecl() &&\n      SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n    SemaRef.getASTConsumer().HandleNeverCPartialDeclarationSource(\n        InstPartialSpec, PartialSpec, InstTemplateArgs,\n        CTAI.CanonicalConverted.data(), CTAI.SugaredConverted.data(),\n        CTAI.CanonicalConverted.size(), CTAI.NeverCDefaultParameters.data(),\n        CTAI.NeverCWrittenDefaults.data(), CTAI.NeverCConvertedDefaults.data(),\n        CTAI.NeverCDefaultParameters.size(), CTAI.NeverCTypeParameters.data(),\n        CTAI.NeverCParameterTypes.data(), CTAI.NeverCParameterPackIndices.data(),\n        CTAI.NeverCTypeParameters.size(), CTAI.NeverCDefaultsOverflow,\n        InstPartialSpec->getLocation());\n  return InstPartialSpec;'),
            ('    if (PatternDecl->isStaticDataMember() &&\n        (PatternDecl = PatternDecl->getFirstDecl())->hasInit() &&\n        !Var->hasInit()) {',
             '    // NeverCStaticMemberInitializer: another declaration can own the value.\n    if (PatternDecl->isStaticDataMember() &&\n        (PatternDecl = PatternDecl->getFirstDecl())->hasInit() &&\n        !Var->getAnyInitializer()) {'),
            ('  if (D->isThisDeclarationADefinition() &&\n      SemaRef.InstantiateClass(D->getLocation(), InstD, D, TemplateArgs,\n                               TSK_ImplicitInstantiation,\n                               /*Complain=*/true))\n    return nullptr;\n\n  return InstD;\n}\n\nDecl *TemplateDeclInstantiator::VisitVarTemplateSpecializationDecl(',
             '  if (D->isThisDeclarationADefinition() &&\n      SemaRef.InstantiateClass(D->getLocation(), InstD, D, TemplateArgs,\n                               TSK_ImplicitInstantiation,\n                               /*Complain=*/true))\n    return nullptr;\n\n  if (!InstD->isInvalidDecl() &&\n      SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n    SemaRef.getASTConsumer().HandleNeverCClassFullDeclarationSource(\n        InstD, D, InstTemplateArgs,\n        CTAI.CanonicalConverted.data(), CTAI.SugaredConverted.data(),\n        CTAI.CanonicalConverted.size(), CTAI.NeverCDefaultParameters.data(),\n        CTAI.NeverCWrittenDefaults.data(), CTAI.NeverCConvertedDefaults.data(),\n        CTAI.NeverCDefaultParameters.size(), CTAI.NeverCTypeParameters.data(),\n        CTAI.NeverCParameterTypes.data(), CTAI.NeverCParameterPackIndices.data(),\n        CTAI.NeverCTypeParameters.size(), CTAI.NeverCDefaultsOverflow,\n        InstD->getLocation());\n  return InstD;\n}\n\nDecl *TemplateDeclInstantiator::VisitVarTemplateSpecializationDecl('),
            ('    RewriteKind FunctionRewriteKind) {\n  // Check whether there is already a function template specialization for\n  // this declaration.\n  FunctionTemplateDecl *FunctionTemplate = D->getDescribedFunctionTemplate();',
             '    RewriteKind FunctionRewriteKind) {\n  FunctionDecl *NeverCIncomingFriendFunction = D;\n  // Check whether there is already a function template specialization for\n  // this declaration.\n  FunctionTemplateDecl *FunctionTemplate = D->getDescribedFunctionTemplate();'),
            ('  if (Function->isOverloadedOperator() && !DC->isRecord() &&\n      PrincipalDecl->isInIdentifierNamespace(Decl::IDNS_Ordinary))\n    PrincipalDecl->setNonMemberOperator();\n\n  return Function;\n}\n\nDecl *TemplateDeclInstantiator::VisitCXXMethodDecl(',
             '  if (Function->isOverloadedOperator() && !DC->isRecord() &&\n      PrincipalDecl->isInIdentifierNamespace(Decl::IDNS_Ordinary))\n    PrincipalDecl->setNonMemberOperator();\n\n  if (isFriend && !FunctionTemplate && !TemplateParams &&\n      !Function->isInvalidDecl() && !D->isInvalidDecl() &&\n      !NeverCIncomingFriendFunction->isInvalidDecl() &&\n      SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n    if (auto *NeverCGrantingClass = dyn_cast<CXXRecordDecl>(Owner))\n      SemaRef.getASTConsumer().HandleNeverCFriendFunctionSource(\n          Function, NeverCIncomingFriendFunction, D, NeverCGrantingClass);\n  if (isFriend && TemplateParams && FunctionTemplate &&\n      Function->getDescribedFunctionTemplate() == FunctionTemplate &&\n      NeverCIncomingFriendFunction->getDescribedFunctionTemplate() &&\n      !Function->isInvalidDecl() && !D->isInvalidDecl() &&\n      !NeverCIncomingFriendFunction->isInvalidDecl() &&\n      SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n    if (auto *NeverCGrantingClass = dyn_cast<CXXRecordDecl>(Owner))\n      SemaRef.getASTConsumer().HandleNeverCFriendFunctionTemplateSource(\n          FunctionTemplate, NeverCIncomingFriendFunction, D, NeverCGrantingClass);\n  return Function;\n}\n\nDecl *TemplateDeclInstantiator::VisitCXXMethodDecl('),
            ('  FriendDecl *FD =\n    FriendDecl::Create(SemaRef.Context, Owner, D->getLocation(),\n                       cast<NamedDecl>(NewND), D->getFriendLoc());\n  FD->setAccess(AS_public);\n  FD->setUnsupportedFriend(D->isUnsupportedFriend());\n  Owner->addDecl(FD);\n  return FD;',
             '  FriendDecl *FD =\n    FriendDecl::Create(SemaRef.Context, Owner, D->getLocation(),\n                       cast<NamedDecl>(NewND), D->getFriendLoc());\n  FD->setAccess(AS_public);\n  FD->setUnsupportedFriend(D->isUnsupportedFriend());\n  Owner->addDecl(FD);\n  if (((isa<FunctionDecl>(ND) && isa<FunctionDecl>(NewND)) ||\n       (isa<FunctionTemplateDecl>(ND) && isa<FunctionTemplateDecl>(NewND)) ||\n       (isa<ClassTemplateDecl>(ND) && isa<ClassTemplateDecl>(NewND))) &&\n      !FD->isInvalidDecl() && !D->isInvalidDecl() &&\n      !ND->isInvalidDecl() && !NewND->isInvalidDecl() &&\n      SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n    SemaRef.getASTConsumer().HandleNeverCFriendDeclarationSource(FD, D);\n  return FD;'),
            ('    FriendDecl *FD = FriendDecl::Create(\n        SemaRef.Context, Owner, D->getLocation(), InstTy, D->getFriendLoc());\n    FD->setAccess(AS_public);\n    FD->setUnsupportedFriend(D->isUnsupportedFriend());\n    Owner->addDecl(FD);\n    return FD;\n  }\n\n  NamedDecl *ND = D->getFriendDecl();',
             '    FriendDecl *FD = FriendDecl::Create(\n        SemaRef.Context, Owner, D->getLocation(), InstTy, D->getFriendLoc());\n    FD->setAccess(AS_public);\n    FD->setUnsupportedFriend(D->isUnsupportedFriend());\n    Owner->addDecl(FD);\n    // NeverC retains only an ordinary successful friend type substitution.\n    if (!D->isInvalidDecl() && !FD->isInvalidDecl() &&\n        !D->isUnsupportedFriend() && !D->isPackExpansion() &&\n        !D->getFriendTypeNumTemplateParameterLists() &&\n        SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n      SemaRef.getASTConsumer().HandleNeverCFriendDeclarationSource(FD, D);\n    return FD;\n  }\n\n  NamedDecl *ND = D->getFriendDecl();'),
            ('    NamedDecl *ND = Function;\n    DeclContext *DC = ND->getLexicalDeclContext();\n    std::optional<ArrayRef<TemplateArgument>> Innermost;',
             '    NamedDecl *ND = Function;\n    DeclContext *DC = ND->getLexicalDeclContext();\n    FunctionTemplateDecl *NeverCCompatibleFunctionTemplate = nullptr;\n    std::optional<ArrayRef<TemplateArgument>> Innermost;'),
            ('      assert(It != Primary->redecls().end() &&\n             "Should\'t get here without a definition");\n      if (FunctionDecl *Def = cast<FunctionTemplateDecl>(*It)',
             '      assert(It != Primary->redecls().end() &&\n             "Should\'t get here without a definition");\n      NeverCCompatibleFunctionTemplate = cast<FunctionTemplateDecl>(*It);\n      if (FunctionDecl *Def = cast<FunctionTemplateDecl>(*It)'),
            ('    PerformDependentDiagnostics(PatternDecl, TemplateArgs);\n\n    if (auto *Listener = getASTMutationListener())\n      Listener->FunctionDefinitionInstantiated(Function);',
             '    PerformDependentDiagnostics(PatternDecl, TemplateArgs);\n\n    // Preserve the selected declaration and DC; do not repeat the search.\n    if (NeverCCompatibleFunctionTemplate && Function->getKind() == Decl::Function &&\n        Function->doesThisDeclarationHaveABody() && !Function->isInvalidDecl() &&\n        !PatternDecl->isInvalidDecl() && Consumer.wantsNeverCTemplateSource())\n      Consumer.HandleNeverCFunctionTemplateBodySource(\n          Function, NeverCCompatibleFunctionTemplate, PatternDecl, DC);\n\n    if (auto *Listener = getASTMutationListener())\n      Listener->FunctionDefinitionInstantiated(Function);'),
            ('  // Finish handling of friends.\n  if (isFriend) {\n    DC->makeDeclVisibleInContext(Inst);\n    return Inst;\n  }',
             '  // Finish handling of friends.\n  if (isFriend) {\n    DC->makeDeclVisibleInContext(Inst);\n    if (!Inst->isInvalidDecl() && !RecordInst->isInvalidDecl() &&\n        !D->isInvalidDecl() && !Pattern->isInvalidDecl() &&\n        Inst->getTemplatedDecl() == RecordInst &&\n        SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n      if (auto *NeverCGrantingClass = dyn_cast<CXXRecordDecl>(Owner))\n        SemaRef.getASTConsumer().HandleNeverCFriendClassTemplateSource(\n            Inst, D, NeverCGrantingClass, DC, PrevClassTemplate);\n    return Inst;\n  }'),
        )),
        ('clang/include/clang/Sema/Overload.h', (
            ('    ImplicitConversionSequence()\n        : ConversionKind(Uninitialized),\n          InitializerListOfIncompleteArray(false) {',
             '    // Source-only metadata is outside the conversion union and never ranked.\n    SourceLocation NeverCTemplateDeductionLocation;\n\n    ImplicitConversionSequence()\n        : ConversionKind(Uninitialized),\n          InitializerListOfIncompleteArray(false),\n          NeverCTemplateDeductionLocation() {'),
            ('          InitializerListContainerType(Other.InitializerListContainerType) {',
             '          InitializerListContainerType(Other.InitializerListContainerType),\n          NeverCTemplateDeductionLocation(Other.NeverCTemplateDeductionLocation) {'),
            ('      ConversionKind = K;\n    }',
             '      ConversionKind = K;\n      NeverCTemplateDeductionLocation = {};\n    }'),
            ('      ConversionKind = AmbiguousConversion;\n      Ambiguous.construct();',
             '      ConversionKind = AmbiguousConversion;\n      NeverCTemplateDeductionLocation = {};\n      Ambiguous.construct();'),
        )),
        ('clang/lib/Sema/SemaOverload.cpp', (
            ('#include "clang/AST/ASTContext.h"',
             '#include "clang/AST/ASTConsumer.h"\n#include "clang/AST/ASTContext.h"'),
            ('        if (ToCanon != FromCanon)\n          ICS.Standard.Second = ICK_Derived_To_Base;\n      }\n    }\n    break;\n\n  case OR_Ambiguous:',
             '        if (ToCanon != FromCanon)\n          ICS.Standard.Second = ICK_Derived_To_Base;\n      }\n    }\n    // Keep the original candidate location even when constructor ranking\n    // represents the selected user conversion as a standard CopyConstructor.\n    if (S.getASTConsumer().wantsNeverCTemplateSource()) {\n      const auto *NeverCSelectedFunction = ICS.isUserDefined()\n          ? ICS.UserDefined.ConversionFunction : ICS.Standard.CopyConstructor;\n      if (NeverCSelectedFunction && NeverCSelectedFunction->getPrimaryTemplate())\n        ICS.NeverCTemplateDeductionLocation = Conversions.getLocation();\n    }\n    break;\n\n  case OR_Ambiguous:'),
            ('    ICS.UserDefined.FoundConversionFunction = Best->FoundDecl;\n    ICS.UserDefined.EllipsisConversion = false;',
             '    ICS.UserDefined.FoundConversionFunction = Best->FoundDecl;\n    ICS.UserDefined.EllipsisConversion = false;\n    if (S.getASTConsumer().wantsNeverCTemplateSource() &&\n        Best->Function->getPrimaryTemplate())\n      ICS.NeverCTemplateDeductionLocation = CandidateSet.getLocation();'),
            ('  if (Result.isInvalid())\n    return true;\n  // Record usage of conversion in an implicit cast.',
             '  if (Result.isInvalid())\n    return true;\n  if (Conversion->getPrimaryTemplate() &&\n      SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n    SemaRef.getASTConsumer().HandleNeverCSelectedTemplateCallSource(\n        Result.get(), Conversion, Loc);\n  // Record usage of conversion in an implicit cast.'),
        )),
        ('clang/lib/Sema/SemaInit.cpp', (
            ('#include "clang/AST/ASTContext.h"',
             '#include "clang/AST/ASTConsumer.h"\n#include "clang/AST/ASTContext.h"'),
            ("  // If we're supposed to bind temporaries, do so.\n  if (!CurInit.isInvalid() && shouldBindAsTemporary(Entity))",
             "  if (!CurInit.isInvalid() && Constructor->getPrimaryTemplate() &&\n      S.getASTConsumer().wantsNeverCTemplateSource())\n    S.getASTConsumer().HandleNeverCSelectedTemplateCallSource(\n        CurInit.get(), Constructor, CandidateSet.getLocation());\n\n  // If we're supposed to bind temporaries, do so.\n  if (!CurInit.isInvalid() && shouldBindAsTemporary(Entity))"),
            ('  // Only check access if all of that succeeded.\n  S.CheckConstructorAccess(Loc, Constructor, Step.Function.FoundDecl, Entity);',
             '  if (Constructor->getPrimaryTemplate() &&\n      S.getASTConsumer().wantsNeverCTemplateSource())\n    S.getASTConsumer().HandleNeverCSelectedTemplateCallSource(\n        CurInit.get(), Constructor, Kind.getLocation());\n\n  // Only check access if all of that succeeded.\n  S.CheckConstructorAccess(Loc, Constructor, Step.Function.FoundDecl, Entity);'),
            ('      CurInit = ImplicitCastExpr::Create(\n          S.Context, CurInit.get()->getType(), CastKind, CurInit.get(), nullptr,\n          CurInit.get()->getValueKind(), S.CurFPFeatureOverrides());',
             '      if (Fn->getPrimaryTemplate() &&\n          S.getASTConsumer().wantsNeverCTemplateSource())\n        S.getASTConsumer().HandleNeverCSelectedTemplateCallSource(\n            CurInit.get(), Fn, FailedCandidateSet.getLocation());\n\n      CurInit = ImplicitCastExpr::Create(\n          S.Context, CurInit.get()->getType(), CastKind, CurInit.get(), nullptr,\n          CurInit.get()->getValueKind(), S.CurFPFeatureOverrides());'),
        )),
        ('clang/lib/Sema/SemaExprCXX.cpp', (
            ('#include "clang/AST/ASTContext.h"',
             '#include "clang/AST/ASTConsumer.h"\n#include "clang/AST/ASTContext.h"'),
            ('                                       bool HadMultipleCandidates,\n                                       Expr *From) {\n  switch (Kind) {',
             '                                       bool HadMultipleCandidates,\n                                       Expr *From,\n                                       SourceLocation NeverCTemplateLocation) {\n  switch (Kind) {'),
            ('    if (Result.isInvalid())\n      return ExprError();\n\n    return S.MaybeBindToTemporary(Result.getAs<Expr>());\n  }\n\n  case CK_UserDefinedConversion:',
             '    if (Result.isInvalid())\n      return ExprError();\n\n    if (Constructor->getPrimaryTemplate() &&\n        S.getASTConsumer().wantsNeverCTemplateSource())\n      S.getASTConsumer().HandleNeverCSelectedTemplateCallSource(\n          Result.get(), Constructor, NeverCTemplateLocation);\n    return S.MaybeBindToTemporary(Result.getAs<Expr>());\n  }\n\n  case CK_UserDefinedConversion:'),
            ('    if (Result.isInvalid())\n      return ExprError();\n    // Record usage of conversion in an implicit cast.',
             '    if (Result.isInvalid())\n      return ExprError();\n    if (Conv->getPrimaryTemplate() &&\n        S.getASTConsumer().wantsNeverCTemplateSource())\n      S.getASTConsumer().HandleNeverCSelectedTemplateCallSource(\n          Result.get(), Conv, NeverCTemplateLocation);\n    // Record usage of conversion in an implicit cast.'),
            ('    From = Res.get();\n    break;\n  }\n\n  case ImplicitConversionSequence::UserDefinedConversion:',
             '    if (ICS.Standard.CopyConstructor &&\n        ICS.Standard.CopyConstructor->getPrimaryTemplate() &&\n        getASTConsumer().wantsNeverCTemplateSource())\n      getASTConsumer().HandleNeverCSelectedTemplateCallSource(\n          Res.get(), ICS.Standard.CopyConstructor,\n          ICS.NeverCTemplateDeductionLocation);\n    From = Res.get();\n    break;\n  }\n\n  case ImplicitConversionSequence::UserDefinedConversion:'),
            ('          ICS.UserDefined.HadMultipleCandidates, From);',
             '          ICS.UserDefined.HadMultipleCandidates, From,\n          ICS.NeverCTemplateDeductionLocation);'),
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
        if re.search(r"\b(?:NeverCStaticMemberInitializer|NeverCIncomingFriendFunction|NeverCGrantingClass|HandleNeverCFriendFunctionSource|HandleNeverCFriendDeclarationSource|NeverCCompatibleFunctionTemplate|HandleNeverCFriendClassTemplateSource|HandleNeverCFriendFunctionTemplateSource|HandleNeverCFunctionTemplateBodySource|HandleNeverCExplicitMemberClassInstantiation|HandleNeverCClassFullDeclarationSource|HandleNeverCPartialDeclarationSource|NeverCMemberVariable|NeverCVariableFull|HandleNeverCSelectedTemplateCallSource|NeverCTemplateDeductionLocation|NeverCTemplateLocation|NeverCSelectedFunction|NeverCConversionName|HandleNeverCVariableTemplateSource|HandleNeverCVariablePartialSource|HandleNeverCVariableTypeSource|NeverCRetainVariable|NeverCVariablePartial|NeverCRetainVariablePartial|HandleNeverCClassPartialSource|NeverCPartial|NeverCRetainPartial|HandleNeverCExplicitFunctionInstantiation|HandleNeverCExplicitStaticDataInstantiation|HandleNeverCScalarTemplateDefault|NeverCWrittenAttributes|NeverCWrittenDefault|NeverCParameter|wantsNeverCTemplateSource|HandleNeverCTemplateTypeSource|HandleNeverCFunctionTemplateSource|HandleNeverCClassTemplateSource|HandleNeverCFunctionSpecializationSource|NeverCAliasSource|NeverCTypeResult|NeverCDefaultParameters|NeverCWrittenDefaults|NeverCConvertedDefaults|NeverCDefaultsOverflow|NeverCAliasSource|NeverCConvertedDefaults|NeverCDefaultParameters|NeverCDefaultsOverflow|NeverCEmptyParameterSource|NeverCExplicitPackIndices|NeverCExplicitParameterTypes|NeverCExplicitSourceOverflow|NeverCExplicitSourceTemplate|NeverCExplicitTypeParameters|NeverCParameter|NeverCParameterPackIndices|NeverCParameterTypeSource|NeverCParameterTypes|NeverCPattern|NeverCPatternLoc|NeverCTypeParameters|NeverCTypeResult|NeverCWrittenAttributes|NeverCWrittenDefault|NeverCWrittenDefaults|NeverCExplicitParameter|clearNeverCExplicitSource|retainNeverCDefault|retainNeverCParameterType)\b", remainder):
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


def fix_copied_full_initializers(path):
    before = '''  if (!Field->getInClassInitializer()) {
    // Maybe we haven't instantiated the in-class initializer. Go check the
    // pattern FieldDecl to see if it has one.
    if (isTemplateInstantiation(ParentRD->getTemplateSpecializationKind())) {
      FieldDecl *Pattern =
          FindFieldDeclInstantiationPattern(getASTContext(), Field);
      assert(Pattern && "We must have set the Pattern!");
      if (!Pattern->hasInClassInitializer() ||
          InstantiateInClassInitializer(Loc, Field, Pattern,
                                        getTemplateInstantiationArgs(Field))) {
        Field->setInvalidDecl();
        return ExprError();
      }
    }
  }'''
    replacement = '''    // NeverC copied fulls keep an explicit specialization kind while
    // their member-class origin still requires lazy initializer substitution.
    const auto *NeverCFull = dyn_cast<ClassTemplateSpecializationDecl>(ParentRD);
    const auto *NeverCMember = ParentRD->getMemberSpecializationInfo();
    const bool NeverCCopiedFull = Consumer.wantsNeverCTemplateSource() &&
        NeverCFull && NeverCFull->isClassScopeExplicitSpecialization() &&
        !ParentRD->isDependentContext() &&
        ParentRD->getInstantiatedFromMemberClass() && NeverCMember &&
        isTemplateInstantiation(NeverCMember->getTemplateSpecializationKind());
    if (isTemplateInstantiation(ParentRD->getTemplateSpecializationKind()) ||
        NeverCCopiedFull) {'''
    after = before.replace(
        "    if (isTemplateInstantiation(ParentRD->getTemplateSpecializationKind())) {",
        replacement, 1)
    error_message = "Unexpected pinned Clang copied full initializer source in " + str(path)
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
    if re.search(r"\b(?:NeverCFull|NeverCMember|NeverCCopiedFull)\b", remainder):
        raise SystemExit(error_message)
    if state == 0:
        path.write_text(text.replace(before, after, 1), encoding="utf-8")


def fix_deduced_variable_instantiations(path):
    before = '''      if (UsableInConstantExpr) {
        // Do not defer instantiations of variables that could be used in a
        // constant expression.
        SemaRef.runWithSufficientStackSpace(PointOfInstantiation, [&] {
          SemaRef.InstantiateVariableDefinition(PointOfInstantiation, Var);
        });

        // Re-set the member to trigger a recomputation of the dependence bits
        // for the expression.
        if (auto *DRE = dyn_cast_or_null<DeclRefExpr>(E))
          DRE->setDecl(DRE->getDecl());
        else if (auto *ME = dyn_cast_or_null<MemberExpr>(E))
          ME->setMemberDecl(ME->getMemberDecl());
      } else if (FirstInstantiation) {'''
    replacement = '''      // Clang 21.1.8 also instantiates undeduced variable types here: a
      // reference needs the initializer's type before its enclosing use is
      // checked. Keep this backport private to NeverC's source consumer.
      const bool NeverCNeedsVariableType =
          SemaRef.getASTConsumer().wantsNeverCTemplateSource() &&
          Var->getType()->isUndeducedType();
      if (UsableInConstantExpr || NeverCNeedsVariableType) {'''
    after = before.replace("      if (UsableInConstantExpr) {", replacement, 1)
    error_message = "Unexpected pinned Clang deduced variable source in " + str(path)
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
    if re.search(r"\bNeverCNeedsVariableType\b", remainder):
        raise SystemExit(error_message)
    if state == 0:
        path.write_text(text.replace(before, after, 1), encoding="utf-8")


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
fix_copied_full_initializers(args.source / "clang/lib/Sema/SemaExpr.cpp")
fix_deduced_variable_instantiations(args.source / "clang/lib/Sema/SemaExpr.cpp")

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
