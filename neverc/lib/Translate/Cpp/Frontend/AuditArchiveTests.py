#!/usr/bin/env python3
"""Controlled symbol inventories for the builtin frontend link gate."""

import argparse
import ast
import io
import json
import re
from pathlib import Path
import subprocess
import sys
import tempfile
import types
import unittest
from unittest import mock

sys.dont_write_bytecode = True
import AuditArchive
import SetupGuidSymbols


# Independent expectations for the ten stdio identities in one final module.
# These controlled rows do not prove SDK macros, object layout or cross-DLL ABI.
MSVC_STDIO_MODULE_RECORDS = (
    ("__local_stdio_printf_options", "T", "__local_stdio_printf_options"),
    ("__local_stdio_scanf_options", "T", "__local_stdio_scanf_options"),
    ("_snprintf", "T", "_snprintf"),
    ("fprintf", "T", "fprintf"),
    ("printf", "T", "printf"),
    ("snprintf", "T", "snprintf"),
    ("sprintf_s", "T", "sprintf_s"),
    ("sscanf", "T", "sscanf"),
    ("?_OptionsStorage@?1??__local_stdio_printf_options@@9@4_KA", "B",
     "unsigned __int64 \x60extern \"C\" __local_stdio_printf_options'::"
     "\x602'::_OptionsStorage"),
    ("?_OptionsStorage@?1??__local_stdio_scanf_options@@9@4_KA", "B",
     "unsigned __int64 \x60extern \"C\" __local_stdio_scanf_options'::"
     "\x602'::_OptionsStorage"),
)


# Fixed witnesses copied from the c942 native SDK symbol reports, independent
# of the production policy's template-name construction.
SETUP_GUID_PAIRS = (
    ("_GUID_00000000_0000_0000_c000_000000000046", "neverc_cpp0000000000000000c000000000000046"),
    ("_GUID_177f0c4a_1cd3_4de7_a32c_71dbbb9fa36d", "neverc_cpp177f0c4a1cd34de7a32c71dbbb9fa36d"),
    ("_GUID_42843719_db4c_46c2_8e7c_64f1816efd5b", "neverc_cpp42843719db4c46c28e7c64f1816efd5b"),
    ("_GUID_26aab78c_4a60_49d6_af3b_3c35bc93365d", "neverc_cpp26aab78c4a6049d6af3b3c35bc93365d"),
    ("_GUID_42b21b78_6192_463e_87bf_d577838f1d5c", "neverc_cpp42b21b786192463e87bfd577838f1d5c"),
)
SETUP_GET_IID = (
    "?GetIID@?$_com_IIID@UISetupConfiguration@@$1?"
    "_GUID_42843719_db4c_46c2_8e7c_64f1816efd5b@@3U__s_GUID@@B@@SAAEBU_GUID@@XZ")
PRIVATE_SETUP_GET_IID = (
    "?GetIID@?$_com_IIID@UISetupConfiguration@@$1?"
    "neverc_cpp42843719db4c46c28e7c64f1816efd5b@@3U__s_GUID@@B@@SAAEBU_GUID@@XZ")
SETUP_CONVERT = (
    "??$?0V?$_com_IIID@UISetupConfiguration@@$1?"
    "_GUID_42843719_db4c_46c2_8e7c_64f1816efd5b@@3U__s_GUID@@B@@$0A@@"
    "?$_com_ptr_t@V?$_com_IIID@UISetupHelper@@$1?"
    "_GUID_42b21b78_6192_463e_87bf_d577838f1d5c@@3U__s_GUID@@B@@@@QEAA@AEBV"
    "?$_com_ptr_t@V?$_com_IIID@UISetupConfiguration@@$1?"
    "_GUID_42843719_db4c_46c2_8e7c_64f1816efd5b@@3U__s_GUID@@B@@@@@Z")
PRIVATE_SETUP_CONVERT = (
    "??$?0V?$_com_IIID@UISetupConfiguration@@$1?"
    "neverc_cpp42843719db4c46c28e7c64f1816efd5b@@3U__s_GUID@@B@@$0A@@"
    "?$_com_ptr_t@V?$_com_IIID@UISetupHelper@@$1?"
    "neverc_cpp42b21b786192463e87bfd577838f1d5c@@3U__s_GUID@@B@@@@QEAA@AEBV"
    "?$_com_ptr_t@V?$_com_IIID@UISetupConfiguration@@$1?"
    "neverc_cpp42843719db4c46c28e7c64f1816efd5b@@3U__s_GUID@@B@@@@@Z")
SETUP_RELEASE = (
    "?_Release@?$_com_ptr_t@V?$_com_IIID@UISetupConfiguration@@$1?"
    "_GUID_42843719_db4c_46c2_8e7c_64f1816efd5b@@3U__s_GUID@@B@@@@AEAAXXZ")
PRIVATE_SETUP_RELEASE = (
    "?_Release@?$_com_ptr_t@V?$_com_IIID@UISetupConfiguration@@$1?"
    "neverc_cpp42843719db4c46c28e7c64f1816efd5b@@3U__s_GUID@@B@@@@AEAAXXZ")
SETUP_DEFAULT = (
    "??0?$_com_ptr_t@V?$_com_IIID@UISetupConfiguration@@$1?"
    "_GUID_42843719_db4c_46c2_8e7c_64f1816efd5b@@3U__s_GUID@@B@@@@QEAA@XZ")
PRIVATE_SETUP_DEFAULT = (
    "??0?$_com_ptr_t@V?$_com_IIID@UISetupConfiguration@@$1?"
    "neverc_cpp42843719db4c46c28e7c64f1816efd5b@@3U__s_GUID@@B@@@@QEAA@XZ")
SETUP_INTERFACE_PTR = (
    "?GetInterfacePtr@?$_com_ptr_t@V?$_com_IIID@UISetupConfiguration2@@$1?"
    "_GUID_26aab78c_4a60_49d6_af3b_3c35bc93365d@@3U__s_GUID@@B@@@@QEAAAEAPEAUISetupConfiguration2@@XZ")
PRIVATE_SETUP_INTERFACE_PTR = (
    "?GetInterfacePtr@?$_com_ptr_t@V?$_com_IIID@UISetupConfiguration2@@$1?"
    "neverc_cpp26aab78c4a6049d6af3b3c35bc93365d@@3U__s_GUID@@B@@@@QEAAAEAPEAUISetupConfiguration2@@XZ")



# Exact independent source/result fixtures for array query dimension substitution.
ARRAY_QUERY_PATCHES = (
    ('clang/lib/Sema/SemaExprCXX.cpp',
     "ExprResult Sema::BuildArrayTypeTrait(ArrayTypeTrait ATT,\n                                     SourceLocation KWLoc,\n                                     TypeSourceInfo *TSInfo,\n                                     Expr* DimExpr,\n                                     SourceLocation RParen) {\n  QualType T = TSInfo->getType();\n\n  // FIXME: This should likely be tracked as an APInt to remove any host\n  // assumptions about the width of size_t on the target.\n  uint64_t Value = 0;\n  if (!T->isDependentType())\n    Value = EvaluateArrayTypeTrait(*this, ATT, T, DimExpr, KWLoc);\n\n  // While the specification for these traits from the Embarcadero C++\n  // compiler's documentation says the return type is 'unsigned int', Clang\n  // returns 'size_t'. On Windows, the primary platform for the Embarcadero\n  // compiler, there is no difference. On several other platforms this is an\n  // important distinction.\n  return new (Context) ArrayTypeTraitExpr(KWLoc, ATT, TSInfo, Value, DimExpr,\n                                          RParen, Context.getSizeType());\n}\n",
     "ExprResult Sema::BuildArrayTypeTrait(ArrayTypeTrait ATT,\n                                     SourceLocation KWLoc,\n                                     TypeSourceInfo *TSInfo,\n                                     Expr* DimExpr,\n                                     SourceLocation RParen) {\n  QualType T = TSInfo->getType();\n\n  // FIXME: This should likely be tracked as an APInt to remove any host\n  // assumptions about the width of size_t on the target.\n  uint64_t Value = 0;\n  // NeverC array-query dimensions must be substituted before evaluation.\n  if (!T->isDependentType() &&\n      (!DimExpr || (!DimExpr->isTypeDependent() && !DimExpr->isValueDependent())))\n    Value = EvaluateArrayTypeTrait(*this, ATT, T, DimExpr, KWLoc);\n\n  // While the specification for these traits from the Embarcadero C++\n  // compiler's documentation says the return type is 'unsigned int', Clang\n  // returns 'size_t'. On Windows, the primary platform for the Embarcadero\n  // compiler, there is no difference. On several other platforms this is an\n  // important distinction.\n  return new (Context) ArrayTypeTraitExpr(KWLoc, ATT, TSInfo, Value, DimExpr,\n                                          RParen, Context.getSizeType());\n}\n"),
    ('clang/lib/Sema/TreeTransform.h',
     'TreeTransform<Derived>::TransformArrayTypeTraitExpr(ArrayTypeTraitExpr *E) {\n  TypeSourceInfo *T = getDerived().TransformType(E->getQueriedTypeSourceInfo());\n  if (!T)\n    return ExprError();\n\n  if (!getDerived().AlwaysRebuild() &&\n      T == E->getQueriedTypeSourceInfo())\n    return E;\n\n  ExprResult SubExpr;\n  {\n    EnterExpressionEvaluationContext Unevaluated(\n        SemaRef, Sema::ExpressionEvaluationContext::Unevaluated);\n    SubExpr = getDerived().TransformExpr(E->getDimensionExpression());\n    if (SubExpr.isInvalid())\n      return ExprError();\n  }\n\n  return getDerived().RebuildArrayTypeTrait(E->getTrait(), E->getBeginLoc(), T,\n                                            SubExpr.get(), E->getEndLoc());\n}\n',
     'TreeTransform<Derived>::TransformArrayTypeTraitExpr(ArrayTypeTraitExpr *E) {\n  TypeSourceInfo *T = getDerived().TransformType(E->getQueriedTypeSourceInfo());\n  if (!T)\n    return ExprError();\n\n  ExprResult SubExpr;\n  {\n    EnterExpressionEvaluationContext Unevaluated(\n        SemaRef, Sema::ExpressionEvaluationContext::Unevaluated);\n    SubExpr = getDerived().TransformExpr(E->getDimensionExpression());\n    if (SubExpr.isInvalid())\n      return ExprError();\n  }\n\n  // NeverC array-query dimensions can change while the type stays fixed.\n  if (!getDerived().AlwaysRebuild() &&\n      T == E->getQueriedTypeSourceInfo() &&\n      SubExpr.get() == E->getDimensionExpression())\n    return E;\n\n  return getDerived().RebuildArrayTypeTrait(E->getTrait(), E->getBeginLoc(), T,\n                                            SubExpr.get(), E->getEndLoc());\n}\n'),
)

PSEUDO_DESTRUCTOR_BEFORE = '  case Expr::AddrLabelExprClass:\n  case Expr::ArrayTypeTraitExprClass:\n  case Expr::AtomicExprClass:\n  case Expr::TypeTraitExprClass:\n  case Expr::CXXBoolLiteralExprClass:\n  case Expr::CXXNoexceptExprClass:\n  case Expr::CXXNullPtrLiteralExprClass:\n  case Expr::CXXPseudoDestructorExprClass:\n  case Expr::CXXScalarValueInitExprClass:\n  case Expr::CXXThisExprClass:\n  case Expr::CXXUuidofExprClass:\n  case Expr::CharacterLiteralClass:\n  case Expr::ExpressionTraitExprClass:\n  case Expr::FloatingLiteralClass:\n  case Expr::GNUNullExprClass:\n  case Expr::ImaginaryLiteralClass:\n  case Expr::ImplicitValueInitExprClass:\n  case Expr::IntegerLiteralClass:\n  case Expr::FixedPointLiteralClass:\n  case Expr::ArrayInitIndexExprClass:\n  case Expr::NoInitExprClass:\n  case Expr::ObjCEncodeExprClass:\n  case Expr::ObjCStringLiteralClass:\n  case Expr::ObjCBoolLiteralExprClass:\n  case Expr::OpaqueValueExprClass:\n  case Expr::PredefinedExprClass:\n  case Expr::SizeOfPackExprClass:\n  case Expr::PackIndexingExprClass:\n  case Expr::StringLiteralClass:\n  case Expr::SourceLocExprClass:\n  case Expr::EmbedExprClass:\n  case Expr::ConceptSpecializationExprClass:\n  case Expr::RequiresExprClass:\n  case Expr::HLSLOutArgExprClass:\n  case Stmt::OpenACCEnterDataConstructClass:\n  case Stmt::OpenACCExitDataConstructClass:\n  case Stmt::OpenACCWaitConstructClass:\n  case Stmt::OpenACCInitConstructClass:\n  case Stmt::OpenACCShutdownConstructClass:\n  case Stmt::OpenACCSetConstructClass:\n  case Stmt::OpenACCUpdateConstructClass:\n    // These expressions can never throw.\n    return CT_Cannot;\n'
PSEUDO_DESTRUCTOR_AFTER = '  case Expr::CXXPseudoDestructorExprClass:\n    // NeverC pseudo-destructor receivers retain their potentially throwing evaluation.\n    return canThrow(cast<CXXPseudoDestructorExpr>(S)->getBase());\n\n  case Expr::AddrLabelExprClass:\n  case Expr::ArrayTypeTraitExprClass:\n  case Expr::AtomicExprClass:\n  case Expr::TypeTraitExprClass:\n  case Expr::CXXBoolLiteralExprClass:\n  case Expr::CXXNoexceptExprClass:\n  case Expr::CXXNullPtrLiteralExprClass:\n  case Expr::CXXScalarValueInitExprClass:\n  case Expr::CXXThisExprClass:\n  case Expr::CXXUuidofExprClass:\n  case Expr::CharacterLiteralClass:\n  case Expr::ExpressionTraitExprClass:\n  case Expr::FloatingLiteralClass:\n  case Expr::GNUNullExprClass:\n  case Expr::ImaginaryLiteralClass:\n  case Expr::ImplicitValueInitExprClass:\n  case Expr::IntegerLiteralClass:\n  case Expr::FixedPointLiteralClass:\n  case Expr::ArrayInitIndexExprClass:\n  case Expr::NoInitExprClass:\n  case Expr::ObjCEncodeExprClass:\n  case Expr::ObjCStringLiteralClass:\n  case Expr::ObjCBoolLiteralExprClass:\n  case Expr::OpaqueValueExprClass:\n  case Expr::PredefinedExprClass:\n  case Expr::SizeOfPackExprClass:\n  case Expr::PackIndexingExprClass:\n  case Expr::StringLiteralClass:\n  case Expr::SourceLocExprClass:\n  case Expr::EmbedExprClass:\n  case Expr::ConceptSpecializationExprClass:\n  case Expr::RequiresExprClass:\n  case Expr::HLSLOutArgExprClass:\n  case Stmt::OpenACCEnterDataConstructClass:\n  case Stmt::OpenACCExitDataConstructClass:\n  case Stmt::OpenACCWaitConstructClass:\n  case Stmt::OpenACCInitConstructClass:\n  case Stmt::OpenACCShutdownConstructClass:\n  case Stmt::OpenACCSetConstructClass:\n  case Stmt::OpenACCUpdateConstructClass:\n    // These expressions can never throw.\n    return CT_Cannot;\n'

class PseudoDestructorSourceTests(unittest.TestCase):
    def test_receiver_repair_checks_pinned_source_states(self):
        script = Path(__file__).resolve().with_name("IsolateSymbols.py")
        function = next(node for node in ast.parse(script.read_text()).body
                        if isinstance(node, ast.FunctionDef) and
                        node.name == "fix_pseudo_destructor_exception_spec")
        namespace = {}
        exec(compile(ast.Module(body=[function], type_ignores=[]), str(script), "exec"), namespace)
        repair = namespace[function.name]
        with tempfile.TemporaryDirectory(prefix="neverc-pseudo-destructor-source-") as temporary:
            path = Path(temporary) / "SemaExceptionSpec.cpp"
            path.write_text(PSEUDO_DESTRUCTOR_BEFORE)
            repair(path)
            self.assertEqual(path.read_text(), PSEUDO_DESTRUCTOR_AFTER)
            repair(path)
            self.assertEqual(path.read_text(), PSEUDO_DESTRUCTOR_AFTER)
            for name, contents in {
                "missing": None, "empty": "",
                "duplicate original": PSEUDO_DESTRUCTOR_BEFORE * 2,
                "duplicate rewritten": PSEUDO_DESTRUCTOR_AFTER * 2,
                "mixed": PSEUDO_DESTRUCTOR_BEFORE + PSEUDO_DESTRUCTOR_AFTER,
                "drift": PSEUDO_DESTRUCTOR_BEFORE.replace("CT_Cannot", "CT_Can"),
                "orphan marker": PSEUDO_DESTRUCTOR_BEFORE + "// NeverC pseudo-destructor receivers",
                "orphan case": PSEUDO_DESTRUCTOR_BEFORE + "case Expr::CXXPseudoDestructorExprClass:",
            }.items():
                with self.subTest(state=name):
                    if contents is None:
                        path.unlink()
                    else:
                        path.write_text(contents)
                    with self.assertRaises(SystemExit):
                        repair(path)
                    self.assertEqual(path.read_text() if path.exists() else None, contents)


# Independent pinned source/result fragments for query operand lifetime retention.
OPERATION_TRAIT_PATCHES = (('clang/include/clang/AST/ASTConsumer.h',
  (('  class ASTContext;', '  class ASTContext;\n  class TypeTraitExpr;'),
   ('  virtual bool shouldSkipFunctionBody(Decl *D) { return true; }',
    '  virtual bool shouldSkipFunctionBody(Decl *D) { return true; }\n'
    '\n'
    '  // NeverC reserves bounded source evidence before creating arena-owned operands.\n'
    '  virtual bool retainNeverCOperationTraitSource(\n'
    '      ASTContext &, unsigned, const SourceLocation &) { return false; }\n'
    '  // The array belongs to this callback; its elements and root belong to ASTContext.\n'
    '  // Incomplete includes failed selection/default conversion; a null root alone\n'
    '  // does not establish that no source operation was selected.\n'
    '  virtual void HandleNeverCOperationTraitSource(\n'
    '      const TypeTraitExpr *, Expr *, Expr *const *, unsigned, bool, bool) {}'))),
 ('clang/lib/Sema/SemaExprCXX.cpp',
  (('static bool EvaluateBinaryTypeTrait(Sema &Self, TypeTrait BTT, const TypeSourceInfo *Lhs,\n'
    '                                    const TypeSourceInfo *Rhs, SourceLocation KeyLoc);',
    '// NeverC operation-trait evidence belongs to one BuildTypeTrait invocation.\n'
    '// Do not retain stack/BumpPtrAllocator operands or treat a failed operation as\n'
    '// proof that no overload/default argument was selected.\n'
    'struct NeverCOperationTraitSource {\n'
    '  SmallVector<Expr *, 4> Operands;\n'
    '  Expr *Root = nullptr;\n'
    '  bool Attempted = false;\n'
    '  bool Complete = false;\n'
    '};\n'
    '\n'
    'static bool EvaluateBinaryTypeTrait(Sema &Self, TypeTrait BTT, const TypeSourceInfo *Lhs,\n'
    '                                    const TypeSourceInfo *Rhs, SourceLocation KeyLoc,\n'
    '                                    NeverCOperationTraitSource *NeverCSource);'),
   ('    SourceLocation KeyLoc, llvm::BumpPtrAllocator &OpaqueExprAllocator) {',
    '    SourceLocation KeyLoc, llvm::BumpPtrAllocator &OpaqueExprAllocator,\n'
    '    NeverCOperationTraitSource *NeverCSource = nullptr) {'),
   ('  Expr *From = new (OpaqueExprAllocator.Allocate<OpaqueValueExpr>())\n'
    '      OpaqueValueExpr(KeyLoc, LhsT.getNonLValueExprType(Self.Context),\n'
    '                      Expr::getValueKindForType(LhsT));',
    '  void *NeverCStorage = NeverCSource\n'
    '      ? Self.Context.Allocate(sizeof(OpaqueValueExpr), alignof(OpaqueValueExpr))\n'
    '      : OpaqueExprAllocator.Allocate<OpaqueValueExpr>();\n'
    '  Expr *From = new (NeverCStorage)\n'
    '      OpaqueValueExpr(KeyLoc, LhsT.getNonLValueExprType(Self.Context),\n'
    '                      Expr::getValueKindForType(LhsT));\n'
    '  if (NeverCSource) {\n'
    '    NeverCSource->Operands.push_back(From);\n'
    '    NeverCSource->Attempted = true;\n'
    '  }'),
   ('  ExprResult Result = Init.Perform(Self, To, Kind, From);\n'
    '  if (Result.isInvalid() || SFINAE.hasErrorOccurred())\n'
    '    return ExprError();\n'
    '\n'
    '  return Result;',
    '  ExprResult Result = Init.Perform(Self, To, Kind, From);\n'
    '  if (NeverCSource) {\n'
    '    NeverCSource->Root = Result.isInvalid() ? nullptr : Result.get();\n'
    '    NeverCSource->Complete = !Result.isInvalid() && !SFINAE.hasErrorOccurred();\n'
    '  }\n'
    '  if (Result.isInvalid() || SFINAE.hasErrorOccurred())\n'
    '    return ExprError();\n'
    '\n'
    '  return Result;'),
   ('                                     bool IsDependent) {\n  if (IsDependent)',
    '                                     bool IsDependent,\n'
    '                                     NeverCOperationTraitSource *NeverCSource) {\n'
    '  if (IsDependent)'),
   ('    return EvaluateBinaryTypeTrait(S, Kind, Args[0],\n'
    '                                   Args[1], RParenLoc);',
    '    return EvaluateBinaryTypeTrait(S, Kind, Args[0],\n'
    '                                   Args[1], RParenLoc, NeverCSource);'),
   ('      ArgExprs.push_back(\n'
    '          new (OpaqueExprAllocator.Allocate<OpaqueValueExpr>())\n'
    '              OpaqueValueExpr(Args[I]->getTypeLoc().getBeginLoc(),\n'
    '                              ArgTy.getNonLValueExprType(S.Context),\n'
    '                              Expr::getValueKindForType(ArgTy)));',
    '      void *NeverCStorage = NeverCSource\n'
    '          ? S.Context.Allocate(sizeof(OpaqueValueExpr), alignof(OpaqueValueExpr))\n'
    '          : OpaqueExprAllocator.Allocate<OpaqueValueExpr>();\n'
    '      ArgExprs.push_back(\n'
    '          new (NeverCStorage)\n'
    '              OpaqueValueExpr(Args[I]->getTypeLoc().getBeginLoc(),\n'
    '                              ArgTy.getNonLValueExprType(S.Context),\n'
    '                              Expr::getValueKindForType(ArgTy)));\n'
    '      if (NeverCSource)\n'
    '        NeverCSource->Operands.push_back(ArgExprs.back());'),
   ('    InitializationSequence Init(S, To, InitKind, ArgExprs);\n    if (Init.Failed())',
    '    if (NeverCSource)\n'
    '      NeverCSource->Attempted = true;\n'
    '    InitializationSequence Init(S, To, InitKind, ArgExprs);\n'
    '    if (Init.Failed())'),
   ('    ExprResult Result = Init.Perform(S, To, InitKind, ArgExprs);\n'
    '    if (Result.isInvalid() || SFINAE.hasErrorOccurred())',
    '    ExprResult Result = Init.Perform(S, To, InitKind, ArgExprs);\n'
    '    if (NeverCSource) {\n'
    '      NeverCSource->Root = Result.isInvalid() ? nullptr : Result.get();\n'
    '      NeverCSource->Complete = !Result.isInvalid() && !SFINAE.hasErrorOccurred();\n'
    '    }\n'
    '    if (Result.isInvalid() || SFINAE.hasErrorOccurred())'),
   ('    bool Result = EvaluateBooleanTypeTrait(*this, Kind, KWLoc, Args, RParenLoc,\n'
    '                                           Dependent);\n'
    '    return TypeTraitExpr::Create(Context, Context.getLogicalOperationType(),\n'
    '                                 KWLoc, Kind, Args, RParenLoc, Result);',
    '    NeverCOperationTraitSource NeverCStorage;\n'
    '    NeverCOperationTraitSource *NeverCSource = nullptr;\n'
    '    const bool NeverCOperation =\n'
    '        Kind == TT_IsConstructible || Kind == TT_IsNothrowConstructible ||\n'
    '        Kind == TT_IsTriviallyConstructible || Kind == BTT_IsAssignable ||\n'
    '        Kind == BTT_IsNothrowAssignable || Kind == BTT_IsTriviallyAssignable ||\n'
    '        Kind == BTT_IsConvertible || Kind == BTT_IsConvertibleTo ||\n'
    '        Kind == BTT_IsNothrowConvertible;\n'
    '    if (!Dependent && NeverCOperation &&\n'
    '        Consumer.retainNeverCOperationTraitSource(Context, Args.size(), KWLoc))\n'
    '      NeverCSource = &NeverCStorage;\n'
    '    bool Result = EvaluateBooleanTypeTrait(*this, Kind, KWLoc, Args, RParenLoc,\n'
    '                                           Dependent, NeverCSource);\n'
    '    auto *NeverCQuery = TypeTraitExpr::Create(\n'
    '        Context, Context.getLogicalOperationType(), KWLoc, Kind, Args,\n'
    '        RParenLoc, Result);\n'
    '    if (NeverCSource)\n'
    '      Consumer.HandleNeverCOperationTraitSource(\n'
    '          NeverCQuery, NeverCSource->Root, NeverCSource->Operands.data(),\n'
    '          NeverCSource->Operands.size(), NeverCSource->Attempted,\n'
    '          NeverCSource->Complete);\n'
    '    return NeverCQuery;'),
   ('static bool EvaluateBinaryTypeTrait(Sema &Self, TypeTrait BTT, const TypeSourceInfo *Lhs,\n'
    '                                    const TypeSourceInfo *Rhs, SourceLocation KeyLoc) {',
    'static bool EvaluateBinaryTypeTrait(Sema &Self, TypeTrait BTT, const TypeSourceInfo *Lhs,\n'
    '                                    const TypeSourceInfo *Rhs, SourceLocation KeyLoc,\n'
    '                                    NeverCOperationTraitSource *NeverCSource) {'),
   ('    ExprResult Result = CheckConvertibilityForTypeTraits(Self, Lhs, Rhs, KeyLoc,\n'
    '                                                         OpaqueExprAllocator);',
    '    ExprResult Result = CheckConvertibilityForTypeTraits(Self, Lhs, Rhs, KeyLoc,\n'
    '                                                         OpaqueExprAllocator,\n'
    '                                                         NeverCSource);'),
   ('    ExprResult Result = Self.BuildBinOp(/*S=*/nullptr, KeyLoc, BO_Assign, &Lhs,\n'
    '                                        &Rhs);\n'
    '    if (Result.isInvalid())',
    '    Expr *NeverCLhs = &Lhs, *NeverCRhs = &Rhs;\n'
    '    if (NeverCSource) {\n'
    '      NeverCLhs = new (Self.Context) OpaqueValueExpr(\n'
    '          KeyLoc, Lhs.getType(), Lhs.getValueKind());\n'
    '      NeverCRhs = new (Self.Context) OpaqueValueExpr(\n'
    '          KeyLoc, Rhs.getType(), Rhs.getValueKind());\n'
    '      NeverCSource->Operands.push_back(NeverCLhs);\n'
    '      NeverCSource->Operands.push_back(NeverCRhs);\n'
    '      NeverCSource->Attempted = true;\n'
    '    }\n'
    '    ExprResult Result = Self.BuildBinOp(/*S=*/nullptr, KeyLoc, BO_Assign,\n'
    '                                        NeverCLhs, NeverCRhs);\n'
    '    if (NeverCSource) {\n'
    '      NeverCSource->Root = Result.isInvalid() ? nullptr : Result.get();\n'
    '      NeverCSource->Complete = !Result.isInvalid() && !SFINAE.hasErrorOccurred();\n'
    '    }\n'
    '    if (Result.isInvalid())'))))


class OperationTraitSourceTests(unittest.TestCase):
    def test_query_source_rewrites_are_atomic_and_idempotent(self):
        script = Path(__file__).resolve().with_name("IsolateSymbols.py")
        function = next(node for node in ast.parse(script.read_text()).body
                        if isinstance(node, ast.FunctionDef) and
                        node.name == "preserve_operation_trait_source")
        namespace = {"re": re}
        exec(compile(ast.Module(body=[function], type_ignores=[]), str(script), "exec"), namespace)
        repair = namespace[function.name]
        with tempfile.TemporaryDirectory(prefix="neverc-operation-trait-source-") as temporary:
            root = Path(temporary)
            def reset(state=0):
                for relative, patches in OPERATION_TRAIT_PATCHES:
                    path = root / relative
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text("\n// Independent source boundary.\n".join(
                        pair[state] for pair in patches))
            def snapshot():
                return {str(path.relative_to(root)): path.read_bytes()
                        for path in root.rglob("*") if path.is_file()}
            reset()
            repair(root)
            for relative, patches in OPERATION_TRAIT_PATCHES:
                self.assertEqual((root / relative).read_text(),
                    "\n// Independent source boundary.\n".join(after for _, after in patches))
            complete = snapshot()
            repair(root)
            self.assertEqual(snapshot(), complete)
            for relative, patches in OPERATION_TRAIT_PATCHES:
                for before, after in patches:
                    for state in (0, 1):
                        for name in ("missing anchor", "duplicate", "drift", "mixed state"):
                            with self.subTest(file=relative, anchor=before[:60], state=state, case=name):
                                reset(state)
                                path = root / relative
                                text = path.read_text()
                                chosen = (before, after)[state]
                                if name == "missing anchor":
                                    text = text.replace(chosen, "", 1)
                                elif name == "duplicate":
                                    text += "\n" + chosen
                                elif name == "drift":
                                    text = text.replace(chosen, re.sub(r"\S", "@", chosen, count=1), 1)
                                else:
                                    text = text.replace(chosen, (after, before)[state], 1)
                                path.write_text(text)
                                old = snapshot()
                                with self.assertRaises(SystemExit):
                                    repair(root)
                                self.assertEqual(snapshot(), old)
                for state in (0, 1):
                    for name in ("missing file", "orphan marker"):
                        with self.subTest(file=relative, state=state, case=name):
                            reset(state)
                            path = root / relative
                            if name == "missing file":
                                path.unlink()
                            else:
                                path.write_text(path.read_text() + "\n// NeverCSource\n")
                            old = snapshot()
                            with self.assertRaises(SystemExit):
                                repair(root)
                            self.assertEqual(snapshot(), old)


class ArrayQuerySourceTests(unittest.TestCase):
    def test_pinned_dimension_repairs_are_atomic_and_idempotent(self):
        script = Path(__file__).resolve().with_name("IsolateSymbols.py")
        function = next(node for node in ast.parse(script.read_text()).body
                        if isinstance(node, ast.FunctionDef) and
                        node.name == "fix_array_type_query_dimensions")
        namespace = {}
        exec(compile(ast.Module(body=[function], type_ignores=[]), str(script), "exec"), namespace)
        repair = namespace[function.name]
        with tempfile.TemporaryDirectory(prefix="neverc-array-query-source-") as temporary:
            root = Path(temporary)
            def reset(states):
                for (relative, before, after), state in zip(ARRAY_QUERY_PATCHES, states):
                    path = root / relative
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text((before, after)[state])
            def snapshot():
                return {str(path.relative_to(root)): path.read_bytes()
                        for path in root.rglob("*") if path.is_file()}
            reset([0, 0])
            repair(root)
            for relative, before, after in ARRAY_QUERY_PATCHES:
                self.assertEqual((root / relative).read_text(), after)
            complete = snapshot()
            repair(root)
            self.assertEqual(snapshot(), complete)
            for index, (relative, before, after) in enumerate(ARRAY_QUERY_PATCHES):
                cases = {
                    "missing": None, "empty": "", "duplicate original": before * 2,
                    "duplicate rewritten": after * 2, "mixed blocks": before + after,
                    "drift": before.replace("return", "RETURN", 1),
                    "orphan marker": before + "\n// NeverC array-query dimensions\n",
                }
                for name, contents in cases.items():
                    with self.subTest(file=relative, state=name):
                        reset([0, 0])
                        path = root / relative
                        if contents is None:
                            path.unlink()
                        else:
                            path.write_text(contents)
                        prior = snapshot()
                        with self.assertRaises(SystemExit):
                            repair(root)
                        self.assertEqual(snapshot(), prior)
            for states in ([0, 1], [1, 0]):
                reset(states)
                prior = snapshot()
                with self.assertRaises(SystemExit):
                    repair(root)
                self.assertEqual(snapshot(), prior)


class SetupGuidPolicyTests(unittest.TestCase):
    def test_exact_five_data_mapping(self):
        self.assertEqual(SetupGuidSymbols.GUID_RENAMES, dict(SETUP_GUID_PAIRS))
        self.assertEqual(SetupGuidSymbols.build_rename_map(old for old, _ in SETUP_GUID_PAIRS),
                         dict(SETUP_GUID_PAIRS))
        for old, new in SETUP_GUID_PAIRS:
            with self.subTest(old=old):
                self.assertTrue(SetupGuidSymbols.contains_old_guid_name(old))
                self.assertEqual(SetupGuidSymbols.rewrite_name(old), new)
                self.assertTrue(SetupGuidSymbols.is_private_guid_name(new))
                self.assertFalse(SetupGuidSymbols.contains_old_guid_name(new))
                self.assertIsNone(SetupGuidSymbols.rewrite_name(new))
                self.assertEqual(len(old.encode("ascii")), 42)
                self.assertEqual(len(new.encode("ascii")), 42)
                self.assertFalse(SetupGuidSymbols.is_private_guid_metadata_name(new))

    def test_observed_full_names_rewrite_every_nttp(self):
        for old, new in ((SETUP_GET_IID, PRIVATE_SETUP_GET_IID),
                         (SETUP_CONVERT, PRIVATE_SETUP_CONVERT)):
            with self.subTest(old=old):
                self.assertEqual(SetupGuidSymbols.rewrite_name(old), new)
                self.assertTrue(SetupGuidSymbols.is_private_guid_name(new))
                self.assertFalse(SetupGuidSymbols.contains_old_guid_name(new))
                self.assertEqual(len(old.encode("ascii")), len(new.encode("ascii")))
                self.assertFalse(SetupGuidSymbols.is_private_guid_metadata_name(new))

    def test_unrelated_guid_and_symbols_are_unchanged(self):
        for name in ("_GUID_6380bcff_41d3_4b2e_8b2e_bf8a6810c848", "_GUID_unrelated",
                     "?GetIID@Unrelated@@SAAEBU_GUID@@XZ", "neverc_cpp_frontend_main"):
            with self.subTest(name=name):
                self.assertIsNone(SetupGuidSymbols.rewrite_name(name))
                self.assertFalse(SetupGuidSymbols.contains_old_guid_name(name))
                self.assertFalse(SetupGuidSymbols.is_private_guid_name(name))

    def test_unobserved_old_grammar_is_rejected(self):
        old = SETUP_GUID_PAIRS[2][0]
        for name in ("prefix" + old, old + "suffix", "$1?" + old + "@@3U__s_GUID@@B",
                     SETUP_GET_IID.replace("GetIID", "InventedMethod"),
                     SETUP_GET_IID.replace("UISetupConfiguration@@", "UOther@@"),
                     SETUP_GET_IID + "suffix", SETUP_CONVERT.replace(old, SETUP_GUID_PAIRS[2][1], 1)):
            with self.subTest(name=name):
                self.assertTrue(SetupGuidSymbols.contains_old_guid_name(name))
                with self.assertRaisesRegex(ValueError, "Unsupported original Setup GUID symbol grammar"):
                    SetupGuidSymbols.build_rename_map([name])

    def test_unobserved_new_grammar_is_rejected(self):
        for name in ("prefix" + SETUP_GUID_PAIRS[0][1], SETUP_GUID_PAIRS[0][1] + "suffix",
                     PRIVATE_SETUP_GET_IID.replace("GetIID", "InventedMethod")):
            with self.subTest(name=name):
                with self.assertRaisesRegex(ValueError, "Unsupported private Setup GUID symbol grammar"):
                    SetupGuidSymbols.is_private_guid_name(name)
                with self.assertRaisesRegex(ValueError, "Unsupported private Setup GUID symbol grammar"):
                    SetupGuidSymbols.build_rename_map([name])

    def test_existing_target_collides_but_duplicate_source_occurrences_are_valid(self):
        for old, new in (SETUP_GUID_PAIRS[0], (SETUP_GET_IID, PRIVATE_SETUP_GET_IID)):
            with self.subTest(old=old):
                with self.assertRaisesRegex(ValueError, "Setup GUID target-name collision"):
                    SetupGuidSymbols.build_rename_map([old, new])
                self.assertEqual(SetupGuidSymbols.build_rename_map([old, old]), {old: new})

    def test_mapping_must_be_injective(self):
        with mock.patch.object(SetupGuidSymbols, "rewrite_name", return_value=SETUP_GUID_PAIRS[0][1]):
            with self.assertRaisesRegex(ValueError, "Non-injective Setup GUID symbol mapping"):
                SetupGuidSymbols.build_rename_map(["A" * 42, "B" * 42])

    def test_partially_rewritten_inventory_is_rejected(self):
        # All five old data definitions can still exist after an incomplete
        # template-only rename. The writer must not accept that mixed state.
        for existing in (SETUP_GUID_PAIRS[0][1], PRIVATE_SETUP_GET_IID,
                         "$pdata$" + PRIVATE_SETUP_RELEASE):
            with self.subTest(existing=existing):
                with self.assertRaisesRegex(ValueError, "Setup GUID target-name collision"):
                    SetupGuidSymbols.build_rename_map([*(old for old, _ in SETUP_GUID_PAIRS), existing])

    def test_exact_observed_metadata_combinations_preserve_full_names_and_lengths(self):
        cases = [(prefix + SETUP_RELEASE, prefix + PRIVATE_SETUP_RELEASE)
                 for prefix in ("$pdata$", "$unwind$", "$cppxdata$", "$ip2state$")]
        cases += [(prefix + SETUP_CONVERT, prefix + PRIVATE_SETUP_CONVERT)
                  for prefix in ("$pdata$", "$unwind$")]
        cases += [("$pdata$" + SETUP_DEFAULT, "$pdata$" + PRIVATE_SETUP_DEFAULT),
                  ("$pdata$" + SETUP_INTERFACE_PTR, "$pdata$" + PRIVATE_SETUP_INTERFACE_PTR)]
        for old, new in cases:
            with self.subTest(old=old):
                self.assertEqual(SetupGuidSymbols.build_rename_map([old]), {old: new})
                self.assertTrue(SetupGuidSymbols.is_private_guid_name(new))
                self.assertTrue(SetupGuidSymbols.is_private_guid_metadata_name(new))
                self.assertFalse(SetupGuidSymbols.is_private_guid_metadata_name(old))
                self.assertEqual(len(old.encode("ascii")), len(new.encode("ascii")))
                self.assertFalse(SetupGuidSymbols.contains_old_guid_name(new))

    def test_metadata_prefixes_do_not_admit_unobserved_combinations(self):
        cases = [(prefix + SETUP_GET_IID, prefix + PRIVATE_SETUP_GET_IID)
                 for prefix in ("$pdata$", "$unwind$", "$cppxdata$", "$ip2state$")]
        cases += [(prefix + SETUP_CONVERT, prefix + PRIVATE_SETUP_CONVERT)
                  for prefix in ("$cppxdata$", "$ip2state$")]
        cases += [(prefix + old, prefix + new)
                  for old, new in ((SETUP_DEFAULT, PRIVATE_SETUP_DEFAULT),
                                   (SETUP_INTERFACE_PTR, PRIVATE_SETUP_INTERFACE_PTR))
                  for prefix in ("$unwind$", "$cppxdata$", "$ip2state$")]
        cases += [("$future$" + SETUP_RELEASE, "$future$" + PRIVATE_SETUP_RELEASE),
                  ("$pdata$" + SETUP_GUID_PAIRS[0][0], "$pdata$" + SETUP_GUID_PAIRS[0][1])]
        for old, new in cases:
            with self.subTest(old=old):
                with self.assertRaisesRegex(ValueError, "Unsupported original Setup GUID symbol grammar"):
                    SetupGuidSymbols.build_rename_map([old])
                with self.assertRaisesRegex(ValueError, "Unsupported private Setup GUID symbol grammar"):
                    SetupGuidSymbols.is_private_guid_name(new)

    def test_mapping_cannot_change_a_symbol_byte_length(self):
        with mock.patch.object(SetupGuidSymbols, "rewrite_name", return_value=SETUP_GUID_PAIRS[0][1]):
            with self.assertRaisesRegex(ValueError, "changes byte length"):
                SetupGuidSymbols.build_rename_map(["short_source"])


class ArchiveAuditTests(unittest.TestCase):
    # Controlled ABI spellings use the documented byte length / CRC / encoded
    # byte grammar. Real compiler+nm coverage lives in the CI toolchain test.
    llvm_literal = ('??_C@_06BCDEFGHI@llvm?3?3?$AA@', 'R', '"llvm::"')
    clang_literal = ('??_C@_07CDEFGHIJ@clang?3?3?$AA@', 'R', '"clang::"')
    # Actual fe866 MSVC x64/ARM64 private R row and complete nm decoding.
    # Unlike the controlled payload spellings above, this has no encoded bytes.
    msvc_empty_literal = ('??_C@_00CNPNBAHC@@', 'R', '""...')
    # Complete LLVM 20 demangling from the 984608 Windows Clang x64 audit.
    # Nested local declarations retain their own return and parameter types.
    microsoft_gcd_lambda = (
        "public: <auto> __cdecl `unsigned int __cdecl std::gcd<unsigned int, "
        "unsigned int>(unsigned int, unsigned int)'::`1'::<lambda_1>::operator()<"
        "class `decltype(auto) __cdecl std::_Select_countr_zero_impl<unsigned int, "
        "class `unsigned int __cdecl std::gcd<unsigned int, unsigned int>"
        "(unsigned int, unsigned int)'::`1'::<lambda_1>>(class `unsigned int "
        "__cdecl std::gcd<unsigned int, unsigned int>(unsigned int, unsigned int)'"
        "::`1'::<lambda_1>)'::`1'::<lambda_1>>(class `decltype(auto) __cdecl "
        "std::_Select_countr_zero_impl<unsigned int, class `unsigned int __cdecl "
        "std::gcd<unsigned int, unsigned int>(unsigned int, unsigned int)'::`1'"
        "::<lambda_1>>(class `unsigned int __cdecl std::gcd<unsigned int, "
        "unsigned int>(unsigned int, unsigned int)'::`1'::<lambda_1>)'::`1'"
        "::<lambda_1>) const")

    def audit_inventory(self, private, host=None, host_format="nm", coff_readobj=None,
                        prefix_header=None):
        private = [("neverc_cpp_frontend_main", "T", "neverc_cpp_frontend_main"),
                   *private]
        args = argparse.Namespace(nm="controlled-nm", nm_file=None,
                                  archive=Path("private.lib"), prefix_header=prefix_header,
                                  host_lib_dir=Path("host-libs") if host is not None else None,
                                  host_format=host_format, host_nm=None,
                                  coff_readobj=coff_readobj, coff_readobj_file=None)

        def inventory(_nm, paths, *options):
            rows = private if paths == [args.archive] else host
            self.assertIsNotNone(rows)
            if "--defined-only" in options:
                rows = [row for row in rows if not AuditArchive.is_undefined(row[1])]
            if "--format=posix" in options:
                return (f"\n{paths[0].stem}.cpp.obj:\n" +
                        "".join(f"{raw} {kind} 0 0\n" for raw, kind, _ in rows))
            self.assertIn("--format=just-symbols", options)
            self.assertIn("--no-sort", options)
            return "".join((decoded if "--demangle" in options else raw) + "\n"
                           for raw, _, decoded in rows)

        reader = types.ModuleType("HostCoffSymbols")
        reader.read_defined_symbols = mock.Mock(return_value={
            raw for raw, kind, _ in host or [] if not AuditArchive.is_undefined(kind)})
        with mock.patch.dict(sys.modules, {"HostCoffSymbols": reader}), \
                mock.patch.object(AuditArchive, "nm_output", side_effect=inventory), \
                mock.patch.object(AuditArchive, "host_archives",
                                  return_value=[Path("host.lib")]):
            AuditArchive.audit(args)

    def test_original_setup_guid_definitions_and_references_fail_without_host(self):
        for old, _ in SETUP_GUID_PAIRS:
            for kind in ("R", "D", "T", "W", "V", "U", "w", "v"):
                with self.subTest(name=old, kind=kind):
                    with self.assertRaisesRegex(ValueError, "unisolated Setup GUID symbol: " + old):
                        self.audit_inventory([(old, kind, old)])

    def test_original_setup_guid_gate_does_not_depend_on_host_intersection(self):
        for host_format in ("nm", "coff-index"):
            for old, _ in SETUP_GUID_PAIRS:
                for host in ([], [("host_only", "T", "host_only")], [(old, "R", old)]):
                    with self.subTest(host_format=host_format, name=old, host=host):
                        with self.assertRaisesRegex(ValueError, "unisolated Setup GUID symbol: " + old):
                            self.audit_inventory([(old, "U", old)], host, host_format)

    def test_original_setup_templates_and_unknown_spellings_fail_without_host(self):
        for name in (SETUP_GET_IID, SETUP_CONVERT, "$pdata$" + SETUP_RELEASE,
                     "invented_" + SETUP_GUID_PAIRS[0][0]):
            for kind in ("T", "U"):
                with self.subTest(name=name, kind=kind):
                    with self.assertRaises(ValueError) as failure:
                        self.audit_inventory([(name, kind, "void __cdecl std::controlled(void)")])
                    self.assertIn("unisolated Setup GUID symbol: " + name, str(failure.exception))

    def test_private_setup_definitions_close_references_without_host(self):
        for _, new in (*SETUP_GUID_PAIRS, (SETUP_GET_IID, PRIVATE_SETUP_GET_IID),
                       (SETUP_CONVERT, PRIVATE_SETUP_CONVERT)):
            with self.subTest(name=new):
                self.audit_inventory([(new, "R", new), (new, "U", new)])

    def test_host_cannot_supply_missing_private_setup_definitions(self):
        for old, new in (*SETUP_GUID_PAIRS, (SETUP_GET_IID, PRIVATE_SETUP_GET_IID)):
            for host_format in ("nm", "coff-index"):
                for host in (None, [(old, "R", old)], [(new, "R", new)]):
                    with self.subTest(name=new, host_format=host_format, host=host):
                        with self.assertRaises(ValueError) as failure:
                            self.audit_inventory([(new, "U", new)], host, host_format)
                        self.assertIn("unresolved private dependency: " + new, str(failure.exception))

    def test_closed_private_setup_names_remain_subject_to_host_intersection(self):
        old, new = SETUP_GUID_PAIRS[0]
        for host_format in ("nm", "coff-index"):
            with self.subTest(host_format=host_format):
                self.audit_inventory([(new, "R", new), (new, "U", new)],
                                     [(old, "R", old)], host_format)
                with self.assertRaisesRegex(ValueError, "private/host symbol intersection: " + new):
                    self.audit_inventory([(new, "R", new)], [(new, "R", new)], host_format)

    def test_unknown_private_setup_spelling_is_rejected(self):
        name = "invented_" + SETUP_GUID_PAIRS[0][1]
        with self.assertRaisesRegex(ValueError, "Unsupported private Setup GUID symbol grammar: " + name):
            self.audit_inventory([(name, "R", name)])

    def test_metadata_in_extern_only_nm_is_rejected_not_an_external_obligation(self):
        for prefix in ("$pdata$", "$unwind$", "$cppxdata$", "$ip2state$"):
            name = prefix + PRIVATE_SETUP_RELEASE
            for kind in ("R", "T", "U", "W", "w"):
                with self.subTest(name=name, kind=kind):
                    with self.assertRaises(ValueError) as failure:
                        self.audit_inventory([(name, kind, name)])
                    self.assertIn("Setup GUID metadata has external linkage: " + name,
                                  str(failure.exception))
                    self.assertNotIn("unresolved private dependency: " + name, str(failure.exception))

    def test_private_setup_weak_names_require_actual_coff_closure(self):
        for new in (SETUP_GUID_PAIRS[0][1], PRIVATE_SETUP_GET_IID):
            for kind in ("W", "V", "w", "v"):
                with self.subTest(name=new, kind=kind):
                    with self.assertRaises(ValueError) as failure:
                        self.audit_inventory([(new, kind, new)])
                    self.assertIn("Setup GUID weak closure requires a COFF reader: " + new,
                                  str(failure.exception))
                    reader = types.ModuleType("CoffWeakAliases")
                    reader.read_resolved_aliases = mock.Mock(return_value={new})
                    with mock.patch.dict(sys.modules, {"CoffWeakAliases": reader}):
                        self.audit_inventory([(new, kind, new)], coff_readobj="controlled-readobj")
                    self.assertIn(new, reader.read_resolved_aliases.call_args.args[3])

    def test_coff_missing_private_setup_definition_keeps_specific_diagnostic(self):
        new = SETUP_GUID_PAIRS[0][1]
        reader = types.ModuleType("CoffWeakAliases")
        reader.read_resolved_aliases = mock.Mock(
            side_effect=ValueError("unresolved private dependency: " + new + "; COFF definition missing"))
        with mock.patch.dict(sys.modules, {"CoffWeakAliases": reader}):
            with self.assertRaisesRegex(ValueError, "unresolved private dependency: " + new):
                self.audit_inventory([(new, "W", new)], [(new, "R", new)],
                                     coff_readobj="controlled-readobj")

    def test_coff_original_setup_name_keeps_specific_diagnostic(self):
        old = SETUP_GUID_PAIRS[0][0]
        reader = types.ModuleType("CoffWeakAliases")
        reader.read_resolved_aliases = mock.Mock(
            side_effect=ValueError("unisolated Setup GUID symbol: " + old + " in private.obj"))
        with mock.patch.dict(sys.modules, {"CoffWeakAliases": reader}):
            with self.assertRaisesRegex(ValueError, "unisolated Setup GUID symbol: " + old):
                self.audit_inventory([(old, "R", old)], coff_readobj="controlled-readobj")

    def test_windows_abort_handler_prefix_checks_definitions_and_references(self):
        with tempfile.TemporaryDirectory(prefix="neverc-abort-prefix-") as temporary:
            header = Path(temporary) / "PrivatePrefix.h"
            header.write_text("#define HandleAbort neverc_cpp_HandleAbort\n",
                              encoding="utf-8")
            for name in ("HandleAbort", "_HandleAbort"):
                for kind in ("T", "U"):
                    with self.subTest(name=name, kind=kind):
                        with self.assertRaisesRegex(ValueError, "HandleAbort"):
                            self.audit_inventory([(name, kind, name)], prefix_header=header)
            for name in ("neverc_cpp_HandleAbort", "_neverc_cpp_HandleAbort"):
                with self.subTest(name=name):
                    with self.assertRaisesRegex(ValueError, "unresolved private dependency"):
                        self.audit_inventory([(name, "U", name)], prefix_header=header)
                    for host_format in ("nm", "coff-index"):
                        self.audit_inventory(
                            [(name, "T", name), (name, "U", name)],
                            [("HandleAbort", "T", "HandleAbort")], host_format,
                            prefix_header=header)

    def test_windows_default_fenv_object_requires_private_definition_and_references(self):
        with tempfile.TemporaryDirectory(prefix="neverc-fenv-prefix-") as temporary:
            header = Path(temporary) / "PrivatePrefix.h"
            header.write_text("#define _Fenv1 neverc_cpp__Fenv1\n", encoding="utf-8")
            original, renamed = "_Fenv1", "neverc_cpp__Fenv1"
            host = [(original, "R", original)]
            unrelated_host = [("host_only", "T", "host_only")]
            for host_format in ("nm", "coff-index"):
                with self.subTest(host_format=host_format):
                    for kind in ("T", "R", "W", "U"):
                        with self.subTest(original_kind=kind):
                            with self.assertRaisesRegex(ValueError, "_Fenv1"):
                                # An unrelated host keeps the prefix check from
                                # passing merely because of an intersection.
                                self.audit_inventory([(original, kind, original)],
                                                     unrelated_host, host_format,
                                                     prefix_header=header)
                    for kind in ("T", "R"):
                        with self.subTest(renamed_kind=kind):
                            self.audit_inventory(
                                [(renamed, kind, renamed), (renamed, "U", renamed)],
                                host, host_format, prefix_header=header)
                    with self.assertRaisesRegex(ValueError, "unresolved private dependency"):
                        self.audit_inventory([(renamed, "U", renamed)], host, host_format,
                                             prefix_header=header)

    def test_pointer_bounds_patch_runs_real_script_with_checked_source_states(self):
        # This synthetic source tree tests the transformation script's input
        # contract. It does not model or compile the LLVM implementation.
        original_record = ("struct PointerBounds {\n"
                           "  TrackingVH<Value> Start;\n"
                           "  TrackingVH<Value> End;\n"
                           "  Value *StrideToCheck;\n"
                           "};")
        renamed_record = ("struct neverc_cpp_PointerBounds {\n"
                          "  TrackingVH<Value> Start;\n"
                          "  TrackingVH<Value> End;\n"
                          "  Value *StrideToCheck;\n"
                          "};\n"
                          "using PointerBounds = neverc_cpp_PointerBounds;")
        # Retain the six type uses from the pinned LoopUtils.cpp declarations
        # and lambda; the omitted first function body is irrelevant to rewriting.
        uses = """static PointerBounds expandBounds(const RuntimeCheckingPtrGroup *CG,
                                  Loop *TheLoop, Instruction *Loc,
                                  SCEVExpander &Exp, bool HoistRuntimeChecks);
static SmallVector<std::pair<PointerBounds, PointerBounds>, 4>
expandBounds(const SmallVectorImpl<RuntimePointerCheck> &PointerChecks, Loop *L,
             Instruction *Loc, SCEVExpander &Exp, bool HoistRuntimeChecks) {
  SmallVector<std::pair<PointerBounds, PointerBounds>, 4> ChecksWithBounds;
  transform(PointerChecks, std::back_inserter(ChecksWithBounds),
            [&](const RuntimePointerCheck &Check) {
              PointerBounds First = expandBounds(Check.first, L, Loc, Exp,
                                                 HoistRuntimeChecks),
                            Second = expandBounds(Check.second, L, Loc, Exp,
                                                  HoistRuntimeChecks);
              return std::make_pair(First, Second);
            });
  return ChecksWithBounds;
}
"""
        original_loop = original_record + "\n\n" + uses
        expected_loop = renamed_record + "\n\n" + uses
        notice_names = (
            "MD5.cpp", "xxhash.cpp", "UnicodeNameToCodepointGenerated.cpp",
            "ConvertUTF.cpp", "regex2.h", "regutils.h", "regex_impl.h",
            "regcomp.c", "regexec.c", "regerror.c", "regfree.c",
        )
        intrinsic = ("constexpr bool isVPIntrinsic(int id) { return id != 0; }\n"
                     "bool fixture_query(int id) {\n"
                     "  if (::isVPIntrinsic(id)) return true;\n"
                     "  return ::isVPIntrinsic(id);\n"
                     "}\n")
        expected_intrinsic = (
            "constexpr bool neverc_cpp_isVPIntrinsic(int id) { return id != 0; }\n"
            "bool fixture_query(int id) {\n"
            "  if (::neverc_cpp_isVPIntrinsic(id)) return true;\n"
            "  return ::neverc_cpp_isVPIntrinsic(id);\n"
            "}\n")
        debugify = ("#ifndef LLVM_TRANSFORMS_UTILS_DEBUGIFY_H\n"
                    "#define LLVM_TRANSFORMS_UTILS_DEBUGIFY_H\n"
                    "struct DebugInfoPerPass {};\n#endif\n")
        expected_debugify = (
            "#ifndef LLVM_TRANSFORMS_UTILS_DEBUGIFY_H\n"
            "#define LLVM_TRANSFORMS_UTILS_DEBUGIFY_H\n"
            "// Private NeverC frontend ABI: this upstream type is global.\n"
            "#define DebugInfoPerPass neverc_cpp_DebugInfoPerPass\n"
            "struct DebugInfoPerPass {};\n#endif\n")
        # Independent fixed input/output expressions, not imported from the
        # production script. The third spelling models an incomplete rewrite.
        math_calls = {
            "llvm/lib/Support/Signals.cpp": (
                ("std::log10(Depth)",
                 "std::log10(static_cast<double>(Depth))",
                 "std::log10(static_cast<double>(Depth)"),),
            "llvm/lib/Support/APFixedPoint.cpp": (
                ("std::pow(2, Sema.getLsbWeight())",
                 "std::pow(2.0, static_cast<double>(Sema.getLsbWeight()))",
                 "std::pow(2.0, Sema.getLsbWeight())"),
                ("std::pow(2, -DstFXSema.getLsbWeight())",
                 "std::pow(2.0, static_cast<double>(-DstFXSema.getLsbWeight()))",
                 "std::pow(2.0, -DstFXSema.getLsbWeight())"),
                ("std::pow(2, DstFXSema.getLsbWeight())",
                 "std::pow(2.0, static_cast<double>(DstFXSema.getLsbWeight()))",
                 "std::pow(2.0, DstFXSema.getLsbWeight())")),
            "llvm/lib/Analysis/ConstantFolding.cpp": (
                ("std::pow(Op1V.convertToFloat(), Exp)",
                 "std::pow(static_cast<double>(Op1V.convertToFloat()), "
                 "static_cast<double>(Exp))",
                 "std::pow(static_cast<double>(Op1V.convertToFloat()), Exp)"),
                ("std::pow(Op1V.convertToDouble(), Exp)",
                 "std::pow(Op1V.convertToDouble(), static_cast<double>(Exp))",
                 "std::pow(Op1V.convertToDouble(), static_cast<double>(Exp)")),
        }
        math_sources = {name: "".join(before + ";\n" for before, _, _ in calls)
                        for name, calls in math_calls.items()}
        expected_math = {name: "".join(after + ";\n" for _, after, _ in calls)
                         for name, calls in math_calls.items()}
        # Verbatim relevant spans from LLVM 20.1.8 MSVCPaths.cpp, not generated
        # from the patcher's constants. Omitted unrelated functions are outside
        # this source-state contract; this fixture is never a compiler proof.
        setup_preamble = """#ifdef _MSC_VER
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
#endif
"""
        setup_calls = """  ISetupInstancePtr NewestInstance;
  std::optional<uint64_t> NewestVersionNum;
  do {
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
  } while ((HR = EnumInstances->Next(1, &Instance, nullptr)) == S_OK);

  if (!NewestInstance)
    return false;

  bstr_t VCPathWide;
  HR = NewestInstance->ResolvePath(L"VC", VCPathWide.GetAddress());
  if (FAILED(HR))
    return false;

  std::string VCRootPath;
  convertWideToUTF8(std::wstring(VCPathWide), VCRootPath);
"""
        original_setup = setup_preamble + "\n" + setup_calls
        # Independent expected owner, not obtained by importing or extracting
        # SETUP_BSTR_OWNER from the script being tested.
        expected_owner = """namespace llvm {
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
        expected_setup_calls = """  ISetupInstancePtr NewestInstance;
  std::optional<uint64_t> NewestVersionNum;
  do {
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
  } while ((HR = EnumInstances->Next(1, &Instance, nullptr)) == S_OK);

  if (!NewestInstance)
    return false;

  NeverCSetupBstr VCPathWide;
  HR = NewestInstance->ResolvePath(L"VC", VCPathWide.out());
  if (FAILED(HR) || !VCPathWide.get())
    return false;

  std::string VCRootPath;
  convertWideToUTF8(std::wstring(VCPathWide.get()), VCRootPath);
"""
        expected_setup_preamble = (setup_preamble[:-len("#endif\n")] + "\n" +
                                   expected_owner + "#endif\n")
        expected_setup = expected_setup_preamble + "\n" + expected_setup_calls
        # Pinned EffectiveContext branch, with independent expected output.
        # These text fixtures check patch states; native access tests exercise
        # actual C++ name/type/constructor/destructor and friendship semantics.
        original_access = """      } else if (isa<FunctionDecl>(DC)) {
        FunctionDecl *Function = cast<FunctionDecl>(DC);
        Functions.push_back(Function->getCanonicalDecl());
        if (Function->getFriendObjectKind())
          DC = Function->getLexicalDeclContext();
        else
          DC = Function->getDeclContext();
      } else if (DC->isFileContext()) {"""
        expected_access = """      } else if (isa<FunctionDecl>(DC)) {
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
        files = {
            "clang/lib/Sema/SemaAccess.cpp": original_access,
            "llvm/lib/WindowsDriver/MSVCPaths.cpp": original_setup,
            "llvm/lib/IR/IntrinsicInst.cpp": intrinsic,
            "llvm/include/llvm/Transforms/Utils/Debugify.h": debugify,
            "llvm/lib/Transforms/Utils/LoopUtils.cpp": original_loop,
            "llvm/include/llvm-c/Core.h": (
                "#define LLVM_FOR_EACH_VALUE_SUBCLASS(macro) \\\n"
                "  macro(Argument)\n\n" +
                "".join(f"void LLVMFixture{index:04d}(void);\n"
                        for index in range(900))),
            "llvm/lib/Support/BLAKE3/llvm_blake3_prefix.h": (
                "#define blake3_compress_in_place llvm_blake3_compress_in_place\n"),
        }
        original_default_header = ("// Before header sentinel.\n"
                                   "  NamedDecl *getTargetDecl() const { return Underlying; }\n"
                                   "// After header sentinel.\n")
        # Independent LLVM 20.1.8 class-body lookup spans. Both primary and
        # partial walks must stop before leaving their own specialization.
        member_pattern_original = '''    if (auto *CTD = dyn_cast_if_present<ClassTemplateDecl *>(From)) {
      while (auto *NewCTD = CTD->getInstantiatedFromMemberTemplate()) {
        if (NewCTD->isMemberSpecialization())
          break;
        CTD = NewCTD;
      }
      return GetDefinitionOrSelf(CTD->getTemplatedDecl());
    }
    if (auto *CTPSD =
            dyn_cast_if_present<ClassTemplatePartialSpecializationDecl *>(
                From)) {
      while (auto *NewCTPSD = CTPSD->getInstantiatedFromMember()) {
        if (NewCTPSD->isMemberSpecialization())
          break;
        CTPSD = NewCTPSD;
      }
      return GetDefinitionOrSelf(CTPSD);
    }'''
        member_pattern_expected = '''    // NeverC member class body lookup stops at the current specialization.
    if (auto *CTD = dyn_cast_if_present<ClassTemplateDecl *>(From)) {
      while (auto *NewCTD = CTD->getInstantiatedFromMemberTemplate()) {
        if (CTD->isMemberSpecialization())
          break;
        CTD = NewCTD;
      }
      return GetDefinitionOrSelf(CTD->getTemplatedDecl());
    }
    if (auto *CTPSD =
            dyn_cast_if_present<ClassTemplatePartialSpecializationDecl *>(
                From)) {
      while (auto *NewCTPSD = CTPSD->getInstantiatedFromMember()) {
        if (CTPSD->isMemberSpecialization())
          break;
        CTPSD = NewCTPSD;
      }
      return GetDefinitionOrSelf(CTPSD);
    }'''
        original_default_source = ("// Before source sentinel.\n" +
                                   member_pattern_original + "\n" +
                                   "void UsingShadowDecl::anchor() {}\n\n"
                                   "UsingShadowDecl::UsingShadowDecl(Kind K) {}\n"
                                   "// After source sentinel.\n")
        files["clang/include/clang/AST/DeclCXX.h"] = original_default_header
        files["clang/lib/AST/DeclCXX.cpp"] = original_default_source
        original_declaration_access = '    if (!IsFriendDeclaration) {\n      S.DelayedDiagnostics.add(DelayedDiagnostic::makeAccess(Loc, Entity));\n      return Sema::AR_delayed;\n    }\n  }\n\n  EffectiveContext EC(S.CurContext);\n\nvoid Sema::HandleDelayedAccessCheck(DelayedDiagnostic &DD, Decl *D) {\n  // Access control for names used in the declarations of functions'
        rewritten_declaration_access = "    if (!IsFriendDeclaration) {\n      S.DelayedDiagnostics.add(DelayedDiagnostic::makeAccess(Loc, Entity));\n      return Sema::AR_delayed;\n    }\n    // Retain the immediate nominated-name check before redeclaration merging,\n    // then check the completed nested friend function's own access context.\n    const auto *NestedClass = dyn_cast<CXXRecordDecl>(S.CurContext);\n    if (NestedClass && isa<CXXRecordDecl>(NestedClass->getDeclContext())) {\n      auto Diagnostic = DelayedDiagnostic::makeAccess(Loc, Entity);\n      Diagnostic.NestedFriendAccess = true;\n      S.DelayedDiagnostics.add(Diagnostic);\n    }\n  }\n\n  EffectiveContext EC(S.CurContext);\n\nvoid Sema::HandleDelayedAccessCheck(DelayedDiagnostic &DD, Decl *D) {\n  // Supplemental function checks must not change type-friend declarations,\n  // including the ClassTemplateDecl returned by a templated friend tag.\n  if (DD.NestedFriendAccess &&\n      !isa<FunctionDecl, FunctionTemplateDecl>(D))\n    return;\n  // Access control for names used in the declarations of functions"
        files['clang/lib/Sema/SemaAccess.cpp'] += "\n\n" + original_declaration_access
        original_declaration_marker = '  DDKind Kind;\n  bool Triggered;\n\n  SourceLocation Loc;'
        rewritten_declaration_marker = '  DDKind Kind;\n  bool Triggered;\n  bool NestedFriendAccess = false;\n\n  SourceLocation Loc;'
        files['clang/include/clang/Sema/DelayedDiagnostic.h'] = original_declaration_marker
        original_declaration_parser = '    std::unique_ptr<CachedTokens> Toks = std::move(LM.DefaultArgs[I].Toks);\n    if (Toks) {\n      ParenBraceBracketBalancer BalancerRAIIObj(*this);'
        rewritten_declaration_parser = "    std::unique_ptr<CachedTokens> Toks = std::move(LM.DefaultArgs[I].Toks);\n    if (Toks) {\n      // A nested friend default uses the function's access context while\n      // preserving the surrounding class scopes for lexical name lookup.\n      auto *Function = dyn_cast<FunctionDecl>(LM.Method);\n      if (const auto *Template = dyn_cast<FunctionTemplateDecl>(LM.Method))\n        Function = Template->getTemplatedDecl();\n      const auto *Lexical = Function\n          ? dyn_cast<CXXRecordDecl>(Function->getLexicalDeclContext()) : nullptr;\n      bool NestedFriendDefault = Function && Function->getFriendObjectKind() &&\n          Lexical && isa<CXXRecordDecl>(Lexical->getDeclContext());\n      ParseScope DefaultScope(this, Scope::FnScope, NestedFriendDefault);\n      std::optional<Sema::ContextRAII> DefaultContext;\n      std::optional<Sema::FunctionScopeRAII> DefaultFunctionScope;\n      if (NestedFriendDefault) {\n        DefaultContext.emplace(Actions, Function, /*NewThisContext=*/false);\n        DefaultFunctionScope.emplace(Actions);\n        Actions.PushFunctionScope();\n      }\n      ParenBraceBracketBalancer BalancerRAIIObj(*this);"
        files['clang/lib/Parse/ParseCXXInlineMethods.cpp'] = original_declaration_parser
        full_initializer_original = '''// Before lazy initialization.
  if (!Field->getInClassInitializer()) {
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
  }
// After lazy initialization.
'''
        full_initializer_expected = '''// Before lazy initialization.
  if (!Field->getInClassInitializer()) {
    // Maybe we haven't instantiated the in-class initializer. Go check the
    // pattern FieldDecl to see if it has one.
    // NeverC copied fulls keep an explicit specialization kind while
    // their member-class origin still requires lazy initializer substitution.
    const auto *NeverCFull = dyn_cast<ClassTemplateSpecializationDecl>(ParentRD);
    const auto *NeverCMember = ParentRD->getMemberSpecializationInfo();
    const bool NeverCCopiedFull = Consumer.wantsNeverCTemplateSource() &&
        NeverCFull && NeverCFull->isClassScopeExplicitSpecialization() &&
        !ParentRD->isDependentContext() &&
        ParentRD->getInstantiatedFromMemberClass() && NeverCMember &&
        isTemplateInstantiation(NeverCMember->getTemplateSpecializationKind());
    if (isTemplateInstantiation(ParentRD->getTemplateSpecializationKind()) ||
        NeverCCopiedFull) {
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
  }
// After lazy initialization.
'''
        # Independent pinned branch and expected result. The enclosing
        # NeedDefinition/TSK guards and subsequent expression refresh stay put.
        deduced_variable_original = '''// Before variable instantiation.
      if (UsableInConstantExpr) {
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
      } else if (FirstInstantiation) {
// After variable instantiation.
'''
        deduced_variable_expected = '''// Before variable instantiation.
      // Clang 21.1.8 also instantiates undeduced variable types here: a
      // reference needs the initializer's type before its enclosing use is
      // checked. Keep this backport private to NeverC's source consumer.
      const bool NeverCNeedsVariableType =
          SemaRef.getASTConsumer().wantsNeverCTemplateSource() &&
          Var->getType()->isUndeducedType();
      if (UsableInConstantExpr || NeverCNeedsVariableType) {
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
      } else if (FirstInstantiation) {
// After variable instantiation.
'''
        files['clang/lib/Sema/SemaExpr.cpp'] = (full_initializer_original +
                                             deduced_variable_original)

        # Independent explicit-instantiation source callback fixtures.
        explicit_header_original = '// Independent template-source contract fixture.\n  class ASTContext;\n  class CXXRecordDecl;\n  class VarDecl;\n  class FunctionDecl;\n  class ImportDecl;\n// Unchanged between source anchors.\n  virtual void HandleCXXImplicitFunctionInstantiation(FunctionDecl *D) {}\n// Unchanged between source anchors.\n  virtual void HandleCXXStaticMemberVarInstantiation(VarDecl *D) {}\n// End source contract fixture.\n'
        explicit_header_expected = '// Independent template-source contract fixture.\n  class ASTContext;\n  class CXXRecordDecl;\n  class VarDecl;\n  class FunctionDecl;\n  class ImportDecl;\n  class TemplateArgumentListInfo;\n  class TypeSourceInfo;\n  struct DeclarationNameInfo;\n  class NestedNameSpecifierLoc;\n  class SourceLocation;\n  class TemplateDecl;\n  class NonTypeTemplateParmDecl;\n  class TemplateArgumentLoc;\n  class TemplateArgument;\n  class Type;\n  class NamedDecl;\n  class ClassTemplateSpecializationDecl;\n  class ClassTemplatePartialSpecializationDecl;\n  class TemplateArgumentList;\n  class VarTemplateSpecializationDecl;\n  class VarTemplatePartialSpecializationDecl;\n  class Expr;\n  class FriendDecl;\n  class FunctionTemplateDecl;\n  class ClassTemplateDecl;\n  class DeclContext;\n// Unchanged between source anchors.\n  virtual void HandleCXXImplicitFunctionInstantiation(FunctionDecl *D) {}\n\n  // NeverC private source evidence; this does not request instantiation.\n  virtual void HandleNeverCExplicitFunctionInstantiation(\n      FunctionDecl *, const TemplateArgumentListInfo &, TypeSourceInfo *,\n      const DeclarationNameInfo &, const NestedNameSpecifierLoc &,\n      const SourceLocation &, bool) {}\n// Unchanged between source anchors.\n  virtual void HandleCXXStaticMemberVarInstantiation(VarDecl *D) {}\n\n  // NeverC private source evidence for each static member directive.\n  virtual void HandleNeverCExplicitStaticDataInstantiation(\n      VarDecl *, TypeSourceInfo *, const NestedNameSpecifierLoc &,\n      const SourceLocation &, bool) {}\n\n  // NeverC retains defaults only after successful argument conversion.\n  virtual void HandleNeverCScalarTemplateDefault(\n      TemplateDecl *, NonTypeTemplateParmDecl *,\n      const TemplateArgumentLoc &, const TemplateArgumentLoc &,\n      const TemplateArgument &, const SourceLocation &) {}\n\n  // NeverC source preservation is opt-in; other consumers keep upstream ASTs.\n  virtual bool wantsNeverCTemplateSource() const { return false; }\n\n  // Separate semantic defaults only for consumers that require object identity.\n  enum class NeverCArrayFillerAction { KeepShared, Separate, Invalid };\n  virtual NeverCArrayFillerAction HandleNeverCArrayFiller(\n      ASTContext &, const Expr *, unsigned long long, unsigned long long) {\n    return NeverCArrayFillerAction::KeepShared;\n  }\n  // Preserve omitted-element provenance after semantic expansion.\n  virtual void HandleNeverCArrayFillerElement(const Expr *) {}\n  virtual void HandleNeverCTemplateTypeSource(\n      TemplateDecl *, const Type *, TypeSourceInfo *,\n      const TemplateArgumentListInfo &,\n      const TemplateArgument *, const TemplateArgument *, unsigned,\n      NamedDecl *const *, const TemplateArgumentLoc *, const TemplateArgumentLoc *,\n      unsigned, NamedDecl *const *, TypeSourceInfo *const *,\n      const unsigned *, unsigned, bool, const SourceLocation &) {}\n  virtual void HandleNeverCFunctionTemplateSource(\n      FunctionDecl *,\n      const TemplateArgument *, const TemplateArgument *, unsigned,\n      NamedDecl *const *, const TemplateArgumentLoc *, const TemplateArgumentLoc *,\n      unsigned, NamedDecl *const *, TypeSourceInfo *const *,\n      const unsigned *, unsigned, bool, const SourceLocation &) {}\n  virtual void HandleNeverCClassTemplateSource(\n      ClassTemplateSpecializationDecl *, const TemplateArgumentListInfo &, bool,\n      const TemplateArgument *, const TemplateArgument *, unsigned,\n      NamedDecl *const *, const TemplateArgumentLoc *, const TemplateArgumentLoc *,\n      unsigned, NamedDecl *const *, TypeSourceInfo *const *,\n      const unsigned *, unsigned, bool, const SourceLocation &) {}\n  virtual void HandleNeverCFunctionSpecializationSource(\n      FunctionDecl *, FunctionDecl *, const TemplateArgumentListInfo *,\n      const SourceLocation &) {}\n  // The exact deduced list later identifies the selected partial candidate.\n  virtual void HandleNeverCClassPartialSource(\n      ClassTemplatePartialSpecializationDecl *, const TemplateArgumentList *,\n      bool, const TemplateArgumentListInfo *,\n      const TemplateArgument *, const TemplateArgument *, unsigned,\n      NamedDecl *const *, const TemplateArgumentLoc *, const TemplateArgumentLoc *,\n      unsigned, NamedDecl *const *, TypeSourceInfo *const *,\n      const unsigned *, unsigned, bool, const SourceLocation &) {}\n  virtual void HandleNeverCVariableTemplateSource(\n      VarTemplateSpecializationDecl *, const TemplateArgumentListInfo &,\n      TypeSourceInfo *, bool, bool,\n      const TemplateArgument *, const TemplateArgument *, unsigned,\n      NamedDecl *const *, const TemplateArgumentLoc *, const TemplateArgumentLoc *,\n      unsigned, NamedDecl *const *, TypeSourceInfo *const *,\n      const unsigned *, unsigned, bool, const SourceLocation &) {}\n  virtual void HandleNeverCVariablePartialSource(\n      VarTemplatePartialSpecializationDecl *, const TemplateArgumentList *,\n      bool, const TemplateArgumentListInfo *,\n      const TemplateArgument *, const TemplateArgument *, unsigned,\n      NamedDecl *const *, const TemplateArgumentLoc *, const TemplateArgumentLoc *,\n      unsigned, NamedDecl *const *, TypeSourceInfo *const *,\n      const unsigned *, unsigned, bool, const SourceLocation &) {}\n  // Successful declaration checks are independent of later partial selection.\n  virtual void HandleNeverCPartialDeclarationSource(\n      NamedDecl *, NamedDecl *, const TemplateArgumentListInfo &,\n      const TemplateArgument *, const TemplateArgument *, unsigned,\n      NamedDecl *const *, const TemplateArgumentLoc *, const TemplateArgumentLoc *,\n      unsigned, NamedDecl *const *, TypeSourceInfo *const *,\n      const unsigned *, unsigned, bool, const SourceLocation &) {}\n  // Class-scope full copies retain the actual successful declaration check.\n  virtual void HandleNeverCClassFullDeclarationSource(\n      ClassTemplateSpecializationDecl *, ClassTemplateSpecializationDecl *,\n      const TemplateArgumentListInfo &,\n      const TemplateArgument *, const TemplateArgument *, unsigned,\n      NamedDecl *const *, const TemplateArgumentLoc *, const TemplateArgumentLoc *,\n      unsigned, NamedDecl *const *, TypeSourceInfo *const *,\n      const unsigned *, unsigned, bool, const SourceLocation &) {}\n  // Ordinary member-class directives otherwise have no separate AST node.\n  virtual void HandleNeverCExplicitMemberClassInstantiation(\n      CXXRecordDecl *, CXXRecordDecl *, const NestedNameSpecifierLoc &,\n      const SourceLocation &, const SourceLocation &, const SourceLocation &,\n      bool) {}\n  // Keep friend spelling separate from the selected signature/body source.\n  virtual void HandleNeverCFriendFunctionSource(\n      FunctionDecl *, FunctionDecl *, FunctionDecl *, CXXRecordDecl *) {}\n  virtual void HandleNeverCFriendDeclarationSource(FriendDecl *, FriendDecl *) {}\n  // Copying an outer class creates a primary, not an inner specialization.\n  virtual void HandleNeverCFriendFunctionTemplateSource(\n      FunctionTemplateDecl *, FunctionDecl *, FunctionDecl *, CXXRecordDecl *) {}\n  // Exact compatible definition context selected by existing Sema control flow.\n  virtual void HandleNeverCFunctionTemplateBodySource(\n      FunctionDecl *, FunctionTemplateDecl *, const FunctionDecl *, DeclContext *) {}\n  // Successful class-friend lookup and redeclaration merge, before return.\n  virtual void HandleNeverCFriendClassTemplateSource(\n      ClassTemplateDecl *, ClassTemplateDecl *, CXXRecordDecl *,\n      DeclContext *, ClassTemplateDecl *) {}\n  virtual void HandleNeverCVariableTypeSource(\n      VarTemplateSpecializationDecl *, VarTemplateSpecializationDecl *,\n      VarDecl *, TypeSourceInfo *, bool,\n      const SourceLocation &) {}\n  // Actual successful expression and original selection location, before wrappers.\n  virtual void HandleNeverCSelectedTemplateCallSource(\n      Expr *, FunctionDecl *, const SourceLocation &) {}\n// End source contract fixture.\n'
        explicit_source_original = '// Independent template-source contract fixture.\n    // Check the default template argument.\n    if (CheckTemplateArgument(*Param, Arg, Template, TemplateLoc, RAngleLoc, 0,\n                              CTAI, CTAK_Specified))\n      return true;\n\n    CTAI.SugaredConverted.back().setIsDefaulted(true);\n// Unchanged between source anchors.\n                                            Declarator &D) {\n  // Explicit instantiations always require a name.\n// Unchanged between source anchors.\n    CheckExplicitInstantiation(*this, Prev, D.getIdentifierLoc(), true, TSK);\n// Unchanged between source anchors.\n    Specialization = cast<FunctionDecl>(*Result);\n  }\n\n  // C++11 [except.spec]p4\n  // In an explicit instantiation an exception-specification may be specified,\n// Unchanged between source anchors.\n        /*Final=*/!getLangOpts().RetainSubstTemplateTypeParmTypeAstNodes);\n// Unchanged between source anchors.\n  QualType CanonType;\n\n  if (TypeAliasTemplateDecl *AliasTemplate =\n// Unchanged between source anchors.\n    CanonType =\n        SubstType(Pattern->getUnderlyingType(), TemplateArgLists,\n                  AliasTemplate->getLocation(), AliasTemplate->getDeclName());\n// Unchanged between source anchors.\n    MultiLevelTemplateArgumentList TemplateArgLists(Template, SugaredConverted,\n                                                    /*Final=*/true);\n// Unchanged between source anchors.\nstatic bool SubstDefaultTemplateArgument(\n    Sema &SemaRef, TemplateDecl *Template, SourceLocation TemplateLoc,\n    SourceLocation RAngleLoc, NonTypeTemplateParmDecl *Param,\n    ArrayRef<TemplateArgument> SugaredConverted,\n    ArrayRef<TemplateArgument> CanonicalConverted,\n    TemplateArgumentLoc &Output) {\n  Sema::InstantiatingTemplate Inst(SemaRef, TemplateLoc, Param, Template,\n                                   SugaredConverted,\n                                   SourceRange(TemplateLoc, RAngleLoc));\n  if (Inst.isInvalid())\n    return true;\n\n  // Only substitute for the innermost template argument list.\n  MultiLevelTemplateArgumentList TemplateArgLists(Template, SugaredConverted,\n                                                  /*Final=*/true);\n// Unchanged between source anchors.\n  return Context.getTemplateSpecializationType(Name, TemplateArgs.arguments(),\n                                               CanonType);\n// Unchanged between source anchors.\n  Specialization->setInvalidDecl(Invalid);\n  inferGslOwnerPointerAttribute(Specialization);\n  return Specialization;\n// Unchanged between source anchors.\n  // Syntax is now OK, so return if it has no other effect on semantics.\n  if (HasNoEffect) {\n// Unchanged between source anchors.\n  Previous.clear();\n  Previous.addDecl(Specialization);\n  return false;\n// Unchanged between source anchors.\n    QualType NTTPType = NTTP->getType();\n    if (NTTP->isParameterPack() && NTTP->isExpandedParameterPack())\n      NTTPType = NTTP->getExpansionType(ArgumentPackIndex);\n\n    if (NTTPType->isInstantiationDependentType() &&\n        !isa<TemplateTemplateParmDecl>(Template) &&\n        !Template->getDeclContext()->isDependentContext()) {\n      // Do substitution on the type of the non-type template parameter.\n      InstantiatingTemplate Inst(*this, TemplateLoc, Template, NTTP,\n                                 CTAI.SugaredConverted,\n                                 SourceRange(TemplateLoc, RAngleLoc));\n      if (Inst.isInvalid())\n        return true;\n\n      MultiLevelTemplateArgumentList MLTAL(Template, CTAI.SugaredConverted,\n                                           /*Final=*/true);\n      // If the parameter is a pack expansion, expand this slice of the pack.\n      if (auto *PET = NTTPType->getAs<PackExpansionType>()) {\n        Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(*this,\n                                                           ArgumentPackIndex);\n        NTTPType = SubstType(PET->getPattern(), MLTAL, NTTP->getLocation(),\n                             NTTP->getDeclName());\n      } else {\n        NTTPType = SubstType(NTTPType, MLTAL, NTTP->getLocation(),\n                             NTTP->getDeclName());\n      }\n\n      // If that worked, check the non-type template parameter type\n      // for validity.\n      if (!NTTPType.isNull())\n        NTTPType = CheckNonTypeTemplateParameterType(NTTPType,\n                                                     NTTP->getLocation());\n      if (NTTPType.isNull())\n        return true;\n    }\n\n\n// Unchanged between source anchors.\n    case TemplateArgument::Pack:\n      llvm_unreachable("Caller must expand template argument packs");\n    }\n\n    return false;\n// Unchanged between source anchors.\n  // Find the variable template specialization declaration that\n  // corresponds to these arguments.\n  void *InsertPos = nullptr;\n  if (VarTemplateSpecializationDecl *Spec =\n          Template->findSpecialization(CTAI.CanonicalConverted, InsertPos)) {\n// Unchanged between source anchors.\n    // If we already have a variable template specialization, return it.\n    return Spec;\n// Unchanged between source anchors.\n  assert(Decl && "No variable template specialization?");\n  return Decl;\n// Unchanged between source anchors.\n  return Specialization;\n}\n\nnamespace {\n/// A partial specialization whose template arguments have matched\n// Explicit ordinary member class directive exits.\n    if (HasNoEffect)\n      return TagD;\n  }\n\n  CXXRecordDecl *RecordDef\n  // FIXME: We don\'t have any representation for explicit instantiations of\n  // member classes. Such a representation is not needed for compilation, but it\n  // should be available for clients that want to see all of the declarations in\n  // the source code.\n  return TagD;\n}\n// End source contract fixture.\n'
        explicit_source_expected = '// Independent template-source contract fixture.\n    // Preserve original spelling as well as any conversion-added operations.\n    const auto NeverCWrittenDefault = Arg;\n    // Check the default template argument.\n    if (CheckTemplateArgument(*Param, Arg, Template, TemplateLoc, RAngleLoc, 0,\n                              CTAI, CTAK_Specified))\n      return true;\n\n    if (Consumer.wantsNeverCTemplateSource()) {\n      if (isa<TemplateTypeParmDecl, NonTypeTemplateParmDecl>(*Param))\n        CTAI.retainNeverCDefault(*Param, NeverCWrittenDefault, Arg);\n    } else {\n      if (auto *NeverCParameter = dyn_cast<NonTypeTemplateParmDecl>(*Param))\n        Consumer.HandleNeverCScalarTemplateDefault(\n            Template, NeverCParameter, NeverCWrittenDefault, Arg,\n            CTAI.CanonicalConverted.back(), TemplateLoc);\n    }\n    CTAI.SugaredConverted.back().setIsDefaulted(true);\n// Unchanged between source anchors.\n                                            Declarator &D) {\n  // Retain attributes before declarator type processing can consume them.\n  const bool NeverCWrittenAttributes = D.hasAttributes();\n  // Explicit instantiations always require a name.\n// Unchanged between source anchors.\n    // Preserve written static-member source before no-effect handling.\n    Consumer.HandleNeverCExplicitStaticDataInstantiation(\n        Prev, T, D.getCXXScopeSpec().getWithLocInContext(Context),\n        D.getIdentifierLoc(), NeverCWrittenAttributes);\n\n    CheckExplicitInstantiation(*this, Prev, D.getIdentifierLoc(), true, TSK);\n// Unchanged between source anchors.\n    Specialization = cast<FunctionDecl>(*Result);\n  }\n\n  // Preserve every directive before duplicate/no-effect early returns.\n  Consumer.HandleNeverCExplicitFunctionInstantiation(\n      Specialization, TemplateArgs, T, NameInfo,\n      D.getCXXScopeSpec().getWithLocInContext(Context),\n      D.getIdentifierLoc(), NeverCWrittenAttributes);\n\n  // C++11 [except.spec]p4\n  // In an explicit instantiation an exception-specification may be specified,\n// Unchanged between source anchors.\n        /*Final=*/!getLangOpts().RetainSubstTemplateTypeParmTypeAstNodes &&\n            !Consumer.wantsNeverCTemplateSource());\n// Unchanged between source anchors.\n  QualType CanonType;\n  TypeSourceInfo *NeverCAliasSource = nullptr;\n\n  if (TypeAliasTemplateDecl *AliasTemplate =\n// Unchanged between source anchors.\n    if (Consumer.wantsNeverCTemplateSource()) {\n      NeverCAliasSource =\n          SubstType(Pattern->getTypeSourceInfo(), TemplateArgLists,\n                    AliasTemplate->getLocation(), AliasTemplate->getDeclName());\n      CanonType = NeverCAliasSource ? NeverCAliasSource->getType() : QualType();\n    } else {\n      CanonType =\n          SubstType(Pattern->getUnderlyingType(), TemplateArgLists,\n                    AliasTemplate->getLocation(), AliasTemplate->getDeclName());\n    }\n// Unchanged between source anchors.\n    MultiLevelTemplateArgumentList TemplateArgLists(\n        Template, SugaredConverted,\n        /*Final=*/!SemaRef.getASTConsumer().wantsNeverCTemplateSource());\n// Unchanged between source anchors.\nstatic bool SubstDefaultTemplateArgument(\n    Sema &SemaRef, TemplateDecl *Template, SourceLocation TemplateLoc,\n    SourceLocation RAngleLoc, NonTypeTemplateParmDecl *Param,\n    ArrayRef<TemplateArgument> SugaredConverted,\n    ArrayRef<TemplateArgument> CanonicalConverted,\n    TemplateArgumentLoc &Output) {\n  Sema::InstantiatingTemplate Inst(SemaRef, TemplateLoc, Param, Template,\n                                   SugaredConverted,\n                                   SourceRange(TemplateLoc, RAngleLoc));\n  if (Inst.isInvalid())\n    return true;\n\n  // Only substitute for the innermost template argument list.\n  MultiLevelTemplateArgumentList TemplateArgLists(\n      Template, SugaredConverted,\n      /*Final=*/!SemaRef.getASTConsumer().wantsNeverCTemplateSource());\n// Unchanged between source anchors.\n  QualType NeverCTypeResult = Context.getTemplateSpecializationType(\n      Name, TemplateArgs.arguments(), CanonType);\n  if (Consumer.wantsNeverCTemplateSource())\n    Consumer.HandleNeverCTemplateTypeSource(\n        Template, NeverCTypeResult.getTypePtr(), NeverCAliasSource, TemplateArgs,\n        CTAI.CanonicalConverted.data(), CTAI.SugaredConverted.data(),\n        CTAI.CanonicalConverted.size(), CTAI.NeverCDefaultParameters.data(),\n        CTAI.NeverCWrittenDefaults.data(), CTAI.NeverCConvertedDefaults.data(),\n        CTAI.NeverCDefaultParameters.size(), CTAI.NeverCTypeParameters.data(),\n        CTAI.NeverCParameterTypes.data(), CTAI.NeverCParameterPackIndices.data(),\n        CTAI.NeverCTypeParameters.size(), CTAI.NeverCDefaultsOverflow, TemplateLoc);\n  return NeverCTypeResult;\n// Unchanged between source anchors.\n  Specialization->setInvalidDecl(Invalid);\n  inferGslOwnerPointerAttribute(Specialization);\n  if (Consumer.wantsNeverCTemplateSource() && !Invalid &&\n      !isPartialSpecialization && TUK != TagUseKind::Friend)\n    Consumer.HandleNeverCClassTemplateSource(\n        Specialization, TemplateArgs, /*Instantiation=*/false,\n        CTAI.CanonicalConverted.data(), CTAI.SugaredConverted.data(),\n        CTAI.CanonicalConverted.size(), CTAI.NeverCDefaultParameters.data(),\n        CTAI.NeverCWrittenDefaults.data(), CTAI.NeverCConvertedDefaults.data(),\n        CTAI.NeverCDefaultParameters.size(), CTAI.NeverCTypeParameters.data(),\n        CTAI.NeverCParameterTypes.data(), CTAI.NeverCParameterPackIndices.data(),\n        CTAI.NeverCTypeParameters.size(), CTAI.NeverCDefaultsOverflow, TemplateNameLoc);\n  if (Consumer.wantsNeverCTemplateSource() && !Invalid &&\n      isPartialSpecialization && TUK != TagUseKind::Friend)\n    Consumer.HandleNeverCPartialDeclarationSource(\n        Specialization, nullptr, TemplateArgs,\n        CTAI.CanonicalConverted.data(), CTAI.SugaredConverted.data(),\n        CTAI.CanonicalConverted.size(), CTAI.NeverCDefaultParameters.data(),\n        CTAI.NeverCWrittenDefaults.data(), CTAI.NeverCConvertedDefaults.data(),\n        CTAI.NeverCDefaultParameters.size(), CTAI.NeverCTypeParameters.data(),\n        CTAI.NeverCParameterTypes.data(), CTAI.NeverCParameterPackIndices.data(),\n        CTAI.NeverCTypeParameters.size(), CTAI.NeverCDefaultsOverflow,\n        Specialization->getLocation());\n  return Specialization;\n// Unchanged between source anchors.\n  // Preserve each declaration, including a no-effect repeated instantiation.\n  if (Consumer.wantsNeverCTemplateSource())\n    Consumer.HandleNeverCClassTemplateSource(\n        Specialization, TemplateArgs, /*Instantiation=*/true,\n        CTAI.CanonicalConverted.data(), CTAI.SugaredConverted.data(),\n        CTAI.CanonicalConverted.size(), CTAI.NeverCDefaultParameters.data(),\n        CTAI.NeverCWrittenDefaults.data(), CTAI.NeverCConvertedDefaults.data(),\n        CTAI.NeverCDefaultParameters.size(), CTAI.NeverCTypeParameters.data(),\n        CTAI.NeverCParameterTypes.data(), CTAI.NeverCParameterPackIndices.data(),\n        CTAI.NeverCTypeParameters.size(), CTAI.NeverCDefaultsOverflow, TemplateNameLoc);\n\n  // Syntax is now OK, so return if it has no other effect on semantics.\n  if (HasNoEffect) {\n// Unchanged between source anchors.\n  Previous.clear();\n  Previous.addDecl(Specialization);\n  if (Consumer.wantsNeverCTemplateSource())\n    Consumer.HandleNeverCFunctionSpecializationSource(\n        FD, Specialization,\n        ExplicitTemplateArgs ? &ConvertedTemplateArgs[Specialization] : nullptr,\n        FD->getLocation());\n  return false;\n// Unchanged between source anchors.\n    QualType NTTPType = NTTP->getType();\n    TypeSourceInfo *NeverCParameterTypeSource =\n        Consumer.wantsNeverCTemplateSource() ? NTTP->getTypeSourceInfo() : nullptr;\n    if (NTTP->isParameterPack() && NTTP->isExpandedParameterPack())\n      NTTPType = NTTP->getExpansionType(ArgumentPackIndex);\n    if (Consumer.wantsNeverCTemplateSource() && NTTP->isParameterPack() &&\n        NTTP->isExpandedParameterPack())\n      NeverCParameterTypeSource = NTTP->getExpansionTypeSourceInfo(ArgumentPackIndex);\n\n    if (NTTPType->isInstantiationDependentType() &&\n        !isa<TemplateTemplateParmDecl>(Template) &&\n        !Template->getDeclContext()->isDependentContext()) {\n      // Do substitution on the type of the non-type template parameter.\n      InstantiatingTemplate Inst(*this, TemplateLoc, Template, NTTP,\n                                 CTAI.SugaredConverted,\n                                 SourceRange(TemplateLoc, RAngleLoc));\n      if (Inst.isInvalid())\n        return true;\n\n      MultiLevelTemplateArgumentList MLTAL(Template, CTAI.SugaredConverted,\n                                           /*Final=*/!Consumer.wantsNeverCTemplateSource());\n      // If the parameter is a pack expansion, expand this slice of the pack.\n      if (auto *PET = NTTPType->getAs<PackExpansionType>()) {\n        Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(*this,\n                                                           ArgumentPackIndex);\n        if (NeverCParameterTypeSource) {\n          auto NeverCPattern = NeverCParameterTypeSource->getTypeLoc()\n                                   .getAs<PackExpansionTypeLoc>();\n          if (NeverCPattern) {\n            auto NeverCPatternLoc = NeverCPattern.getPatternLoc();\n            NeverCParameterTypeSource = Context.CreateTypeSourceInfo(PET->getPattern());\n            NeverCParameterTypeSource->getTypeLoc().initializeFullCopy(NeverCPatternLoc);\n          } else {\n            NeverCParameterTypeSource = nullptr;\n          }\n        }\n        if (NeverCParameterTypeSource) {\n          NeverCParameterTypeSource = SubstType(NeverCParameterTypeSource, MLTAL,\n                                               NTTP->getLocation(), NTTP->getDeclName());\n          NTTPType = NeverCParameterTypeSource ? NeverCParameterTypeSource->getType() : QualType();\n        } else {\n          NTTPType = SubstType(PET->getPattern(), MLTAL, NTTP->getLocation(),\n                               NTTP->getDeclName());\n        }\n      } else {\n        if (NeverCParameterTypeSource) {\n          NeverCParameterTypeSource = SubstType(NeverCParameterTypeSource, MLTAL,\n                                               NTTP->getLocation(), NTTP->getDeclName());\n          NTTPType = NeverCParameterTypeSource ? NeverCParameterTypeSource->getType() : QualType();\n        } else {\n          NTTPType = SubstType(NTTPType, MLTAL, NTTP->getLocation(),\n                               NTTP->getDeclName());\n        }\n      }\n\n      // If that worked, check the non-type template parameter type\n      // for validity.\n      if (!NTTPType.isNull())\n        NTTPType = CheckNonTypeTemplateParameterType(NTTPType,\n                                                     NTTP->getLocation());\n      if (NTTPType.isNull())\n        return true;\n    }\n\n\n// Unchanged between source anchors.\n    case TemplateArgument::Pack:\n      llvm_unreachable("Caller must expand template argument packs");\n    }\n\n    if (Consumer.wantsNeverCTemplateSource())\n      CTAI.retainNeverCParameterType(NTTP, NeverCParameterTypeSource, ArgumentPackIndex);\n    return false;\n// Unchanged between source anchors.\n  // Preserve every concrete use, including a fresh spelling of a cached id.\n  auto NeverCRetainVariable = [&](VarTemplateSpecializationDecl *Spec) {\n    if (Spec && !Spec->isInvalidDecl() && Consumer.wantsNeverCTemplateSource())\n      Consumer.HandleNeverCVariableTemplateSource(\n          Spec, TemplateArgs, nullptr, false, false,\n          CTAI.CanonicalConverted.data(), CTAI.SugaredConverted.data(),\n          CTAI.CanonicalConverted.size(), CTAI.NeverCDefaultParameters.data(),\n          CTAI.NeverCWrittenDefaults.data(), CTAI.NeverCConvertedDefaults.data(),\n          CTAI.NeverCDefaultParameters.size(), CTAI.NeverCTypeParameters.data(),\n          CTAI.NeverCParameterTypes.data(), CTAI.NeverCParameterPackIndices.data(),\n          CTAI.NeverCTypeParameters.size(), CTAI.NeverCDefaultsOverflow,\n          TemplateNameLoc);\n  };\n\n  // Find the variable template specialization declaration that\n  // corresponds to these arguments.\n  void *InsertPos = nullptr;\n  if (VarTemplateSpecializationDecl *Spec =\n          Template->findSpecialization(CTAI.CanonicalConverted, InsertPos)) {\n// Unchanged between source anchors.\n    // If we already have a variable template specialization, return it.\n    NeverCRetainVariable(Spec);\n    return Spec;\n// Unchanged between source anchors.\n  assert(Decl && "No variable template specialization?");\n  NeverCRetainVariable(Decl);\n  return Decl;\n// Unchanged between source anchors.\n  if (!IsPartialSpecialization && !Specialization->isInvalidDecl() &&\n      Consumer.wantsNeverCTemplateSource())\n    Consumer.HandleNeverCVariableTemplateSource(\n        Specialization, TemplateArgs, DI, true,\n        D.getDeclSpec().getStorageClassSpec() != DeclSpec::SCS_unspecified,\n        CTAI.CanonicalConverted.data(), CTAI.SugaredConverted.data(),\n        CTAI.CanonicalConverted.size(), CTAI.NeverCDefaultParameters.data(),\n        CTAI.NeverCWrittenDefaults.data(), CTAI.NeverCConvertedDefaults.data(),\n        CTAI.NeverCDefaultParameters.size(), CTAI.NeverCTypeParameters.data(),\n        CTAI.NeverCParameterTypes.data(), CTAI.NeverCParameterPackIndices.data(),\n        CTAI.NeverCTypeParameters.size(), CTAI.NeverCDefaultsOverflow,\n        TemplateNameLoc);\n\n  if (IsPartialSpecialization && !Specialization->isInvalidDecl() &&\n      Consumer.wantsNeverCTemplateSource())\n    Consumer.HandleNeverCPartialDeclarationSource(\n        Specialization, nullptr, TemplateArgs,\n        CTAI.CanonicalConverted.data(), CTAI.SugaredConverted.data(),\n        CTAI.CanonicalConverted.size(), CTAI.NeverCDefaultParameters.data(),\n        CTAI.NeverCWrittenDefaults.data(), CTAI.NeverCConvertedDefaults.data(),\n        CTAI.NeverCDefaultParameters.size(), CTAI.NeverCTypeParameters.data(),\n        CTAI.NeverCParameterTypes.data(), CTAI.NeverCParameterPackIndices.data(),\n        CTAI.NeverCTypeParameters.size(), CTAI.NeverCDefaultsOverflow,\n        Specialization->getLocation());\n\n  return Specialization;\n}\n\nnamespace {\n/// A partial specialization whose template arguments have matched\n// Explicit ordinary member class directive exits.\n    if (HasNoEffect) {\n      if (!Record->isInvalidDecl() &&\n          getASTConsumer().wantsNeverCTemplateSource())\n        getASTConsumer().HandleNeverCExplicitMemberClassInstantiation(\n            Record, Pattern, SS.getWithLocInContext(Context),\n            NameLoc, TemplateLoc, ExternLoc, !Attr.empty());\n      return TagD;\n    }\n  }\n\n  CXXRecordDecl *RecordDef\n  if (!Record->isInvalidDecl() &&\n      getASTConsumer().wantsNeverCTemplateSource())\n    getASTConsumer().HandleNeverCExplicitMemberClassInstantiation(\n        Record, Pattern, SS.getWithLocInContext(Context),\n        NameLoc, TemplateLoc, ExternLoc, !Attr.empty());\n  // FIXME: We don\'t have any representation for explicit instantiations of\n  // member classes. Such a representation is not needed for compilation, but it\n  // should be available for clients that want to see all of the declarations in\n  // the source code.\n  return TagD;\n}\n// End source contract fixture.\n'
        explicit_deduction_original = '// Independent template-source contract fixture.\n#include "clang/AST/ASTContext.h"\n// Unchanged between source anchors.\n    // Check whether we can actually use the default argument.\n    if (S.CheckTemplateArgument(\n            Param, DefArg, TD, TD->getLocation(), TD->getSourceRange().getEnd(),\n            /*ArgumentPackIndex=*/0, CTAI, Sema::CTAK_Specified)) {\n      Info.Param = makeTemplateParameter(\n                         const_cast<NamedDecl *>(TemplateParams->getParam(I)));\n      // FIXME: These template arguments are temporary. Free them!\n      Info.reset(\n          TemplateArgumentList::CreateCopy(S.Context, CTAI.SugaredConverted),\n          TemplateArgumentList::CreateCopy(S.Context, CTAI.CanonicalConverted));\n      return TemplateDeductionResult::SubstitutionFailure;\n    }\n\n    // If we get here, we successfully used the default template argument.\n// Unchanged between source anchors.\n    if (Inserted)\n      Pos->second.append(Info.diag_begin(), Info.diag_end());\n  }\n\n  return TemplateDeductionResult::Success;\n// Unchanged between source anchors.\n      MultiLevelTemplateArgumentList Args(Template, CTAI.SugaredConverted,\n                                          /*Final=*/true);\n\n      if (auto *NTTP = dyn_cast<NonTypeTemplateParmDecl>(Param)) {\n        Sema::InstantiatingTemplate Inst(S, Template->getLocation(), Template,\n                                         NTTP, CTAI.SugaredConverted,\n                                         Template->getSourceRange());\n        if (Inst.isInvalid() ||\n            S.SubstType(NTTP->getType(), Args, NTTP->getLocation(),\n                        NTTP->getDeclName()).isNull())\n          return true;\n      } else if (auto *TTP = dyn_cast<TemplateTemplateParmDecl>(Param)) {\n        Sema::InstantiatingTemplate Inst(S, Template->getLocation(), Template,\n                                         TTP, CTAI.SugaredConverted,\n                                         Template->getSourceRange());\n        if (Inst.isInvalid() || !S.SubstDecl(TTP, S.CurContext, Args))\n          return true;\n      }\n\n// Unchanged between source anchors.\n    TemplateDeductionInfo &Info) {\n  FunctionDecl *Function = FunctionTemplate->getTemplatedDecl();\n// Unchanged between source anchors.\n  Info.setExplicitArgs(SugaredExplicitArgumentList,\n                       CanonicalExplicitArgumentList);\n// Unchanged between source anchors.\n          CTAI.CanonicalConverted.push_back(\n              S.Context.getCanonicalTemplateArgument(Deduced[I]));\n          continue;\n// Unchanged between source anchors.\n  if (S.SubstTemplateArguments(\n          PartialTemplArgInfo->arguments(),\n          MultiLevelTemplateArgumentList(Partial, CTAI.SugaredConverted,\n                                         /*Final=*/true),\n          InstArgs)) {\n// Unchanged between source anchors.\n  if (!IsPartialOrdering) {\n    if (auto Result = CheckDeducedArgumentConstraints(\n            S, Partial, CTAI.SugaredConverted, CTAI.CanonicalConverted, Info);\n        Result != TemplateDeductionResult::Success)\n      return Result;\n  }\n\n  return TemplateDeductionResult::Success;\n}\n\n/// Complete template argument deduction for a class or variable template,\n// End source contract fixture.\n'
        explicit_deduction_expected = '// Independent template-source contract fixture.\n#include "clang/AST/ASTConsumer.h"\n#include "clang/AST/ASTContext.h"\n// Unchanged between source anchors.\n    // Preserve spelling before CheckTemplateArgument adds conversions.\n    const auto NeverCWrittenDefault = DefArg;\n    // Check whether we can actually use the default argument.\n    if (S.CheckTemplateArgument(\n            Param, DefArg, TD, TD->getLocation(), TD->getSourceRange().getEnd(),\n            /*ArgumentPackIndex=*/0, CTAI, Sema::CTAK_Specified)) {\n      Info.Param = makeTemplateParameter(\n                         const_cast<NamedDecl *>(TemplateParams->getParam(I)));\n      // FIXME: These template arguments are temporary. Free them!\n      Info.reset(\n          TemplateArgumentList::CreateCopy(S.Context, CTAI.SugaredConverted),\n          TemplateArgumentList::CreateCopy(S.Context, CTAI.CanonicalConverted));\n      return TemplateDeductionResult::SubstitutionFailure;\n    }\n\n    if (S.getASTConsumer().wantsNeverCTemplateSource()) {\n      if (isa<TemplateTypeParmDecl, NonTypeTemplateParmDecl>(Param))\n        CTAI.retainNeverCDefault(Param, NeverCWrittenDefault, DefArg);\n    } else {\n      if (auto *NeverCParameter = dyn_cast<NonTypeTemplateParmDecl>(Param))\n        S.getASTConsumer().HandleNeverCScalarTemplateDefault(\n            TD, NeverCParameter, NeverCWrittenDefault, DefArg,\n            CTAI.CanonicalConverted.back(), TD->getLocation());\n    }\n    // If we get here, we successfully used the default template argument.\n// Unchanged between source anchors.\n    if (Inserted)\n      Pos->second.append(Info.diag_begin(), Info.diag_end());\n  }\n\n  if (Consumer.wantsNeverCTemplateSource() && !IsIncomplete)\n    Consumer.HandleNeverCFunctionTemplateSource(\n        Specialization,\n        CTAI.CanonicalConverted.data(), CTAI.SugaredConverted.data(),\n        CTAI.CanonicalConverted.size(), CTAI.NeverCDefaultParameters.data(),\n        CTAI.NeverCWrittenDefaults.data(), CTAI.NeverCConvertedDefaults.data(),\n        CTAI.NeverCDefaultParameters.size(), CTAI.NeverCTypeParameters.data(),\n        CTAI.NeverCParameterTypes.data(), CTAI.NeverCParameterPackIndices.data(),\n        CTAI.NeverCTypeParameters.size(), CTAI.NeverCDefaultsOverflow, Info.getLocation());\n  return TemplateDeductionResult::Success;\n// Unchanged between source anchors.\n      MultiLevelTemplateArgumentList Args(Template, CTAI.SugaredConverted,\n                                          /*Final=*/!(S.getASTConsumer().wantsNeverCTemplateSource() &&\n                                                      isa<NonTypeTemplateParmDecl>(Param)));\n\n      if (auto *NTTP = dyn_cast<NonTypeTemplateParmDecl>(Param)) {\n        Sema::InstantiatingTemplate Inst(S, Template->getLocation(), Template,\n                                         NTTP, CTAI.SugaredConverted,\n                                         Template->getSourceRange());\n        if (Inst.isInvalid())\n          return true;\n        auto *NeverCEmptyParameterSource = S.getASTConsumer().wantsNeverCTemplateSource()\n                                              ? NTTP->getTypeSourceInfo() : nullptr;\n        if (NeverCEmptyParameterSource) {\n          NeverCEmptyParameterSource = S.SubstType(NeverCEmptyParameterSource, Args,\n                                                   NTTP->getLocation(), NTTP->getDeclName());\n          if (!NeverCEmptyParameterSource)\n            return true;\n        } else if (S.SubstType(NTTP->getType(), Args, NTTP->getLocation(),\n                               NTTP->getDeclName()).isNull()) {\n          return true;\n        }\n        if (S.getASTConsumer().wantsNeverCTemplateSource())\n          CTAI.retainNeverCParameterType(NTTP, NeverCEmptyParameterSource, ~0u);\n      } else if (auto *TTP = dyn_cast<TemplateTemplateParmDecl>(Param)) {\n        Sema::InstantiatingTemplate Inst(S, Template->getLocation(), Template,\n                                         TTP, CTAI.SugaredConverted,\n                                         Template->getSourceRange());\n        if (Inst.isInvalid() || !S.SubstDecl(TTP, S.CurContext, Args))\n          return true;\n      }\n\n// Unchanged between source anchors.\n    TemplateDeductionInfo &Info) {\n  if (Consumer.wantsNeverCTemplateSource())\n    Info.clearNeverCExplicitSource();\n  FunctionDecl *Function = FunctionTemplate->getTemplatedDecl();\n// Unchanged between source anchors.\n  Info.setExplicitArgs(SugaredExplicitArgumentList,\n                       CanonicalExplicitArgumentList);\n  if (Consumer.wantsNeverCTemplateSource()) {\n    Info.NeverCExplicitSourceTemplate = FunctionTemplate;\n    Info.NeverCExplicitTypeParameters = CTAI.NeverCTypeParameters;\n    Info.NeverCExplicitParameterTypes = CTAI.NeverCParameterTypes;\n    Info.NeverCExplicitPackIndices = CTAI.NeverCParameterPackIndices;\n    Info.NeverCExplicitSourceOverflow = CTAI.NeverCDefaultsOverflow;\n  }\n// Unchanged between source anchors.\n          CTAI.CanonicalConverted.push_back(\n              S.Context.getCanonicalTemplateArgument(Deduced[I]));\n          if (S.getASTConsumer().wantsNeverCTemplateSource() &&\n              isa<NonTypeTemplateParmDecl>(Param)) {\n            if (Info.NeverCExplicitSourceTemplate != dyn_cast<FunctionTemplateDecl>(Template)) {\n              CTAI.NeverCDefaultsOverflow = true;\n            } else {\n              CTAI.NeverCDefaultsOverflow |= Info.NeverCExplicitSourceOverflow;\n              for (unsigned E = 0; E < Info.NeverCExplicitTypeParameters.size(); ++E)\n                if (const auto *NeverCExplicitParameter = dyn_cast<NonTypeTemplateParmDecl>(\n                        Info.NeverCExplicitTypeParameters[E]);\n                    NeverCExplicitParameter && NeverCExplicitParameter->getIndex() == I)\n                  CTAI.retainNeverCParameterType(Info.NeverCExplicitTypeParameters[E],\n                                                Info.NeverCExplicitParameterTypes[E],\n                                                Info.NeverCExplicitPackIndices[E]);\n            }\n          }\n          continue;\n// Unchanged between source anchors.\n  if (S.SubstTemplateArguments(\n          PartialTemplArgInfo->arguments(),\n          MultiLevelTemplateArgumentList(Partial, CTAI.SugaredConverted,\n                                         /*Final=*/!(isa<ClassTemplatePartialSpecializationDecl,\n                                                         VarTemplatePartialSpecializationDecl>(Partial) &&\n                                                     !IsPartialOrdering &&\n                                                     S.getASTConsumer().wantsNeverCTemplateSource())),\n          InstArgs)) {\n// Unchanged between source anchors.\n  if (!IsPartialOrdering) {\n    if (auto Result = CheckDeducedArgumentConstraints(\n            S, Partial, CTAI.SugaredConverted, CTAI.CanonicalConverted, Info);\n        Result != TemplateDeductionResult::Success)\n      return Result;\n  }\n\n  if (auto *NeverCPartial = dyn_cast<ClassTemplatePartialSpecializationDecl>(Partial);\n      NeverCPartial && !IsPartialOrdering && !Trap.hasErrorOccurred() &&\n      S.getASTConsumer().wantsNeverCTemplateSource()) {\n    auto NeverCRetainPartial = [&](bool Pattern,\n                                   const TemplateArgumentListInfo *Written,\n                                   const Sema::CheckTemplateArgumentInfo &Checked) {\n      S.getASTConsumer().HandleNeverCClassPartialSource(\n          NeverCPartial, CanonicalDeducedArgumentList, Pattern, Written,\n          Checked.CanonicalConverted.data(), Checked.SugaredConverted.data(),\n          Checked.CanonicalConverted.size(), Checked.NeverCDefaultParameters.data(),\n          Checked.NeverCWrittenDefaults.data(), Checked.NeverCConvertedDefaults.data(),\n          Checked.NeverCDefaultParameters.size(), Checked.NeverCTypeParameters.data(),\n          Checked.NeverCParameterTypes.data(), Checked.NeverCParameterPackIndices.data(),\n          Checked.NeverCTypeParameters.size(), Checked.NeverCDefaultsOverflow,\n          Info.getLocation());\n    };\n    NeverCRetainPartial(false, nullptr, CTAI);\n    NeverCRetainPartial(true, &InstArgs, InstCTAI);\n  }\n\n  if (auto *NeverCVariablePartial = dyn_cast<VarTemplatePartialSpecializationDecl>(Partial);\n      NeverCVariablePartial && !IsPartialOrdering && !Trap.hasErrorOccurred() &&\n      S.getASTConsumer().wantsNeverCTemplateSource()) {\n    auto NeverCRetainVariablePartial = [&](bool Pattern,\n                                           const TemplateArgumentListInfo *Written,\n                                           const Sema::CheckTemplateArgumentInfo &Checked) {\n      // Variable selection transfers takeSugared(), unlike class selection.\n      S.getASTConsumer().HandleNeverCVariablePartialSource(\n          NeverCVariablePartial, SugaredDeducedArgumentList, Pattern, Written,\n          Checked.CanonicalConverted.data(), Checked.SugaredConverted.data(),\n          Checked.CanonicalConverted.size(), Checked.NeverCDefaultParameters.data(),\n          Checked.NeverCWrittenDefaults.data(), Checked.NeverCConvertedDefaults.data(),\n          Checked.NeverCDefaultParameters.size(), Checked.NeverCTypeParameters.data(),\n          Checked.NeverCParameterTypes.data(), Checked.NeverCParameterPackIndices.data(),\n          Checked.NeverCTypeParameters.size(), Checked.NeverCDefaultsOverflow,\n          Info.getLocation());\n    };\n    NeverCRetainVariablePartial(false, nullptr, CTAI);\n    NeverCRetainVariablePartial(true, &InstArgs, InstCTAI);\n  }\n\n  return TemplateDeductionResult::Success;\n}\n\n/// Complete template argument deduction for a class or variable template,\n// End source contract fixture.\n'
        explicit_sema_original = '// Independent template-source contract fixture.\n    SmallVector<TemplateArgument, 4> SugaredConverted, CanonicalConverted;\n\n// End source contract fixture.\n'
        explicit_sema_expected = '// Independent template-source contract fixture.\n    SmallVector<TemplateArgument, 4> SugaredConverted, CanonicalConverted;\n\n    // Keep defaults attached to this deduction, including ignored type args.\n    SmallVector<NamedDecl *, 4> NeverCDefaultParameters;\n    SmallVector<TemplateArgumentLoc, 4> NeverCWrittenDefaults, NeverCConvertedDefaults;\n    bool NeverCDefaultsOverflow = false;\n\n    void retainNeverCDefault(NamedDecl *Parameter,\n                            const TemplateArgumentLoc &Written,\n                            const TemplateArgumentLoc &Converted) {\n      if (NeverCDefaultParameters.size() == 64) {\n        NeverCDefaultsOverflow = true;\n        return;\n      }\n      NeverCDefaultParameters.push_back(Parameter);\n      NeverCWrittenDefaults.push_back(Written);\n      NeverCConvertedDefaults.push_back(Converted);\n    }\n\n    SmallVector<NamedDecl *, 4> NeverCTypeParameters;\n    SmallVector<TypeSourceInfo *, 4> NeverCParameterTypes;\n    SmallVector<unsigned, 4> NeverCParameterPackIndices;\n\n    void retainNeverCParameterType(NamedDecl *Parameter, TypeSourceInfo *Source,\n                                  unsigned PackIndex) {\n      if (NeverCTypeParameters.size() == 4096) {\n        NeverCDefaultsOverflow = true;\n        return;\n      }\n      NeverCTypeParameters.push_back(Parameter);\n      NeverCParameterTypes.push_back(Source);\n      NeverCParameterPackIndices.push_back(PackIndex);\n    }\n\n// End source contract fixture.\n'
        explicit_deduction_info_original = '// Independent template-source contract fixture.\npublic:\n  TemplateDeductionInfo(SourceLocation Loc, unsigned DeducedDepth = 0)\n// End source contract fixture.\n'
        explicit_deduction_info_expected = '// Independent template-source contract fixture.\npublic:\n  // NeverC keeps preliminary explicit conversions on this exact candidate.\n  // reset/take retain it; a new explicit-substitution invocation clears it.\n  FunctionTemplateDecl *NeverCExplicitSourceTemplate = nullptr;\n  SmallVector<NamedDecl *, 4> NeverCExplicitTypeParameters;\n  SmallVector<TypeSourceInfo *, 4> NeverCExplicitParameterTypes;\n  SmallVector<unsigned, 4> NeverCExplicitPackIndices;\n  bool NeverCExplicitSourceOverflow = false;\n\n  void clearNeverCExplicitSource() {\n    NeverCExplicitSourceTemplate = nullptr;\n    NeverCExplicitTypeParameters.clear();\n    NeverCExplicitParameterTypes.clear();\n    NeverCExplicitPackIndices.clear();\n    NeverCExplicitSourceOverflow = false;\n  }\n\n  TemplateDeductionInfo(SourceLocation Loc, unsigned DeducedDepth = 0)\n// End source contract fixture.\n'
        explicit_instantiate_original = '// Independent variable-type source contract fixture.\n  SemaRef.BuildVariableInstantiation(Var, D, TemplateArgs, LateAttrs, Owner,\n                                     StartingScope, false, PrevDecl);\n\n  return Var;\n// Unchanged between source anchors.\n  // Instantiate the initializer.\n  InstantiateVariableInitializer(VarSpec, PatternDecl, TemplateArgs);\n\n  if (getLangOpts().OpenCL)\n    deduceOpenCLAddressSpace(VarSpec);\n\n  return VarSpec;\n// Unchanged between source anchors.\n    MultiLevelTemplateArgumentList TemplateArgs = getTemplateInstantiationArgs(\n        Function, DC, /*Final=*/false, Innermost, false, PatternDecl);\n\n    // Substitute into the qualifier; we can get a substitution failure here\n// Generic class-scope full member specialization.\n  return VisitVarTemplateSpecializationDecl(InstVarTemplate, D,\n                                            VarTemplateArgsInfo,\n                                            CTAI.CanonicalConverted, PrevDecl);\n// Copied partial declaration source.\n  ClassTemplate->AddPartialSpecialization(InstPartialSpec,\n                                          /*InsertPos=*/nullptr);\n  return InstPartialSpec;\n// Copied partial declaration source.\n  SemaRef.BuildVariableInstantiation(InstPartialSpec, PartialSpec, TemplateArgs,\n                                     LateAttrs, Owner, StartingScope);\n\n  return InstPartialSpec;\n// Repeated non-inline member constant instantiation.\n    if (PatternDecl->isStaticDataMember() &&\n        (PatternDecl = PatternDecl->getFirstDecl())->hasInit() &&\n        !Var->hasInit()) {\n// Copied class full declaration source.\n  if (D->isThisDeclarationADefinition() &&\n      SemaRef.InstantiateClass(D->getLocation(), InstD, D, TemplateArgs,\n                               TSK_ImplicitInstantiation,\n                               /*Complain=*/true))\n    return nullptr;\n\n  return InstD;\n}\n\nDecl *TemplateDeclInstantiator::VisitVarTemplateSpecializationDecl(\n// Friend incoming signature, successful function and friend creation.\n    RewriteKind FunctionRewriteKind) {\n  // Check whether there is already a function template specialization for\n  // this declaration.\n  FunctionTemplateDecl *FunctionTemplate = D->getDescribedFunctionTemplate();\n  if (Function->isOverloadedOperator() && !DC->isRecord() &&\n      PrincipalDecl->isInIdentifierNamespace(Decl::IDNS_Ordinary))\n    PrincipalDecl->setNonMemberOperator();\n\n  return Function;\n}\n\nDecl *TemplateDeclInstantiator::VisitCXXMethodDecl(\n  FriendDecl *FD =\n    FriendDecl::Create(SemaRef.Context, Owner, D->getLocation(),\n                       cast<NamedDecl>(NewND), D->getFriendLoc());\n  FD->setAccess(AS_public);\n  FD->setUnsupportedFriend(D->isUnsupportedFriend());\n  Owner->addDecl(FD);\n  return FD;\n// Independent ordinary friend type substitution.\n    FriendDecl *FD = FriendDecl::Create(\n        SemaRef.Context, Owner, D->getLocation(), InstTy, D->getFriendLoc());\n    FD->setAccess(AS_public);\n    FD->setUnsupportedFriend(D->isUnsupportedFriend());\n    Owner->addDecl(FD);\n    return FD;\n  }\n\n  NamedDecl *ND = D->getFriendDecl();\n// Independent body-compatible template declaration selection.\n    NamedDecl *ND = Function;\n    DeclContext *DC = ND->getLexicalDeclContext();\n    std::optional<ArrayRef<TemplateArgument>> Innermost;\n      assert(It != Primary->redecls().end() &&\n             "Should\'t get here without a definition");\n      if (FunctionDecl *Def = cast<FunctionTemplateDecl>(*It)\n    PerformDependentDiagnostics(PatternDecl, TemplateArgs);\n\n    if (auto *Listener = getASTMutationListener())\n      Listener->FunctionDefinitionInstantiated(Function);\n// Independent successful class-friend target boundary.\n  // Finish handling of friends.\n  if (isFriend) {\n    DC->makeDeclVisibleInContext(Inst);\n    return Inst;\n  }\n// End source contract fixture.\n'
        explicit_instantiate_expected = '// Independent variable-type source contract fixture.\n  SemaRef.BuildVariableInstantiation(Var, D, TemplateArgs, LateAttrs, Owner,\n                                     StartingScope, false, PrevDecl);\n\n  if (!Var->isInvalidDecl() && SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n    SemaRef.getASTConsumer().HandleNeverCVariableTypeSource(\n        Var, PrevDecl, D, DI, false, Var->getLocation());\n\n  return Var;\n// Unchanged between source anchors.\n  // Instantiate the initializer.\n  InstantiateVariableInitializer(VarSpec, PatternDecl, TemplateArgs);\n\n  if (getLangOpts().OpenCL)\n    deduceOpenCLAddressSpace(VarSpec);\n\n  if (!VarSpec->isInvalidDecl() && Consumer.wantsNeverCTemplateSource())\n    Consumer.HandleNeverCVariableTypeSource(\n        VarSpec, nullptr, PatternDecl, DI, true, VarSpec->getLocation());\n\n  return VarSpec;\n// Unchanged between source anchors.\n    MultiLevelTemplateArgumentList TemplateArgs = getTemplateInstantiationArgs(\n        Function, DC, /*Final=*/false, Innermost, false, PatternDecl);\n\n    // The definition\'s name-location copy above retains its generic type.\n    // Preserve this definition\'s concrete conversion type and written syntax.\n    if (Consumer.wantsNeverCTemplateSource() && isa<CXXConversionDecl>(Function)) {\n      auto NeverCConversionName =\n          SubstDeclarationNameInfo(PatternDecl->getNameInfo(), TemplateArgs);\n      if (!NeverCConversionName.getName()) {\n        Function->setInvalidDecl();\n        return;\n      }\n      Function->setDeclarationNameLoc(NeverCConversionName.getInfo());\n    }\n\n    // Substitute into the qualifier; we can get a substitution failure here\n// Generic class-scope full member specialization.\n  Decl *NeverCMemberVariable = VisitVarTemplateSpecializationDecl(\n      InstVarTemplate, D, VarTemplateArgsInfo, CTAI.CanonicalConverted, PrevDecl);\n  if (auto *NeverCVariableFull =\n          dyn_cast_or_null<VarTemplateSpecializationDecl>(NeverCMemberVariable);\n      NeverCVariableFull && !NeverCVariableFull->isInvalidDecl() &&\n      SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n    SemaRef.getASTConsumer().HandleNeverCVariableTemplateSource(\n        NeverCVariableFull, VarTemplateArgsInfo,\n        NeverCVariableFull->getTypeSourceInfo(), /*Declaration=*/true,\n        /*WrittenStorageClass=*/false,\n        CTAI.CanonicalConverted.data(), CTAI.SugaredConverted.data(),\n        CTAI.CanonicalConverted.size(), CTAI.NeverCDefaultParameters.data(),\n        CTAI.NeverCWrittenDefaults.data(), CTAI.NeverCConvertedDefaults.data(),\n        CTAI.NeverCDefaultParameters.size(), CTAI.NeverCTypeParameters.data(),\n        CTAI.NeverCParameterTypes.data(), CTAI.NeverCParameterPackIndices.data(),\n        CTAI.NeverCTypeParameters.size(), CTAI.NeverCDefaultsOverflow,\n        NeverCVariableFull->getLocation());\n  return NeverCMemberVariable;\n// Copied partial declaration source.\n  ClassTemplate->AddPartialSpecialization(InstPartialSpec,\n                                          /*InsertPos=*/nullptr);\n  if (!InstPartialSpec->isInvalidDecl() &&\n      SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n    SemaRef.getASTConsumer().HandleNeverCPartialDeclarationSource(\n        InstPartialSpec, PartialSpec, InstTemplateArgs,\n        CTAI.CanonicalConverted.data(), CTAI.SugaredConverted.data(),\n        CTAI.CanonicalConverted.size(), CTAI.NeverCDefaultParameters.data(),\n        CTAI.NeverCWrittenDefaults.data(), CTAI.NeverCConvertedDefaults.data(),\n        CTAI.NeverCDefaultParameters.size(), CTAI.NeverCTypeParameters.data(),\n        CTAI.NeverCParameterTypes.data(), CTAI.NeverCParameterPackIndices.data(),\n        CTAI.NeverCTypeParameters.size(), CTAI.NeverCDefaultsOverflow,\n        InstPartialSpec->getLocation());\n  return InstPartialSpec;\n// Copied partial declaration source.\n  SemaRef.BuildVariableInstantiation(InstPartialSpec, PartialSpec, TemplateArgs,\n                                     LateAttrs, Owner, StartingScope);\n\n  if (!InstPartialSpec->isInvalidDecl() &&\n      SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n    SemaRef.getASTConsumer().HandleNeverCPartialDeclarationSource(\n        InstPartialSpec, PartialSpec, InstTemplateArgs,\n        CTAI.CanonicalConverted.data(), CTAI.SugaredConverted.data(),\n        CTAI.CanonicalConverted.size(), CTAI.NeverCDefaultParameters.data(),\n        CTAI.NeverCWrittenDefaults.data(), CTAI.NeverCConvertedDefaults.data(),\n        CTAI.NeverCDefaultParameters.size(), CTAI.NeverCTypeParameters.data(),\n        CTAI.NeverCParameterTypes.data(), CTAI.NeverCParameterPackIndices.data(),\n        CTAI.NeverCTypeParameters.size(), CTAI.NeverCDefaultsOverflow,\n        InstPartialSpec->getLocation());\n  return InstPartialSpec;\n// Repeated non-inline member constant instantiation.\n    // NeverCStaticMemberInitializer: another declaration can own the value.\n    if (PatternDecl->isStaticDataMember() &&\n        (PatternDecl = PatternDecl->getFirstDecl())->hasInit() &&\n        !Var->getAnyInitializer()) {\n// Copied class full declaration source.\n  if (D->isThisDeclarationADefinition() &&\n      SemaRef.InstantiateClass(D->getLocation(), InstD, D, TemplateArgs,\n                               TSK_ImplicitInstantiation,\n                               /*Complain=*/true))\n    return nullptr;\n\n  if (!InstD->isInvalidDecl() &&\n      SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n    SemaRef.getASTConsumer().HandleNeverCClassFullDeclarationSource(\n        InstD, D, InstTemplateArgs,\n        CTAI.CanonicalConverted.data(), CTAI.SugaredConverted.data(),\n        CTAI.CanonicalConverted.size(), CTAI.NeverCDefaultParameters.data(),\n        CTAI.NeverCWrittenDefaults.data(), CTAI.NeverCConvertedDefaults.data(),\n        CTAI.NeverCDefaultParameters.size(), CTAI.NeverCTypeParameters.data(),\n        CTAI.NeverCParameterTypes.data(), CTAI.NeverCParameterPackIndices.data(),\n        CTAI.NeverCTypeParameters.size(), CTAI.NeverCDefaultsOverflow,\n        InstD->getLocation());\n  return InstD;\n}\n\nDecl *TemplateDeclInstantiator::VisitVarTemplateSpecializationDecl(\n// Friend incoming signature, successful function and friend creation.\n    RewriteKind FunctionRewriteKind) {\n  FunctionDecl *NeverCIncomingFriendFunction = D;\n  // Check whether there is already a function template specialization for\n  // this declaration.\n  FunctionTemplateDecl *FunctionTemplate = D->getDescribedFunctionTemplate();\n  if (Function->isOverloadedOperator() && !DC->isRecord() &&\n      PrincipalDecl->isInIdentifierNamespace(Decl::IDNS_Ordinary))\n    PrincipalDecl->setNonMemberOperator();\n\n  if (isFriend && !FunctionTemplate && !TemplateParams &&\n      !Function->isInvalidDecl() && !D->isInvalidDecl() &&\n      !NeverCIncomingFriendFunction->isInvalidDecl() &&\n      SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n    if (auto *NeverCGrantingClass = dyn_cast<CXXRecordDecl>(Owner))\n      SemaRef.getASTConsumer().HandleNeverCFriendFunctionSource(\n          Function, NeverCIncomingFriendFunction, D, NeverCGrantingClass);\n  if (isFriend && TemplateParams && FunctionTemplate &&\n      Function->getDescribedFunctionTemplate() == FunctionTemplate &&\n      NeverCIncomingFriendFunction->getDescribedFunctionTemplate() &&\n      !Function->isInvalidDecl() && !D->isInvalidDecl() &&\n      !NeverCIncomingFriendFunction->isInvalidDecl() &&\n      SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n    if (auto *NeverCGrantingClass = dyn_cast<CXXRecordDecl>(Owner))\n      SemaRef.getASTConsumer().HandleNeverCFriendFunctionTemplateSource(\n          FunctionTemplate, NeverCIncomingFriendFunction, D, NeverCGrantingClass);\n  return Function;\n}\n\nDecl *TemplateDeclInstantiator::VisitCXXMethodDecl(\n  FriendDecl *FD =\n    FriendDecl::Create(SemaRef.Context, Owner, D->getLocation(),\n                       cast<NamedDecl>(NewND), D->getFriendLoc());\n  FD->setAccess(AS_public);\n  FD->setUnsupportedFriend(D->isUnsupportedFriend());\n  Owner->addDecl(FD);\n  if (((isa<FunctionDecl>(ND) && isa<FunctionDecl>(NewND)) ||\n       (isa<FunctionTemplateDecl>(ND) && isa<FunctionTemplateDecl>(NewND)) ||\n       (isa<ClassTemplateDecl>(ND) && isa<ClassTemplateDecl>(NewND))) &&\n      !FD->isInvalidDecl() && !D->isInvalidDecl() &&\n      !ND->isInvalidDecl() && !NewND->isInvalidDecl() &&\n      SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n    SemaRef.getASTConsumer().HandleNeverCFriendDeclarationSource(FD, D);\n  return FD;\n// Independent ordinary friend type substitution.\n    FriendDecl *FD = FriendDecl::Create(\n        SemaRef.Context, Owner, D->getLocation(), InstTy, D->getFriendLoc());\n    FD->setAccess(AS_public);\n    FD->setUnsupportedFriend(D->isUnsupportedFriend());\n    Owner->addDecl(FD);\n    // NeverC retains only an ordinary successful friend type substitution.\n    if (!D->isInvalidDecl() && !FD->isInvalidDecl() &&\n        !D->isUnsupportedFriend() && !D->isPackExpansion() &&\n        !D->getFriendTypeNumTemplateParameterLists() &&\n        SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n      SemaRef.getASTConsumer().HandleNeverCFriendDeclarationSource(FD, D);\n    return FD;\n  }\n\n  NamedDecl *ND = D->getFriendDecl();\n// Independent body-compatible template declaration selection.\n    NamedDecl *ND = Function;\n    DeclContext *DC = ND->getLexicalDeclContext();\n    FunctionTemplateDecl *NeverCCompatibleFunctionTemplate = nullptr;\n    std::optional<ArrayRef<TemplateArgument>> Innermost;\n      assert(It != Primary->redecls().end() &&\n             "Should\'t get here without a definition");\n      NeverCCompatibleFunctionTemplate = cast<FunctionTemplateDecl>(*It);\n      if (FunctionDecl *Def = cast<FunctionTemplateDecl>(*It)\n    PerformDependentDiagnostics(PatternDecl, TemplateArgs);\n\n    // Preserve the selected declaration and DC; do not repeat the search.\n    if (NeverCCompatibleFunctionTemplate && Function->getKind() == Decl::Function &&\n        Function->doesThisDeclarationHaveABody() && !Function->isInvalidDecl() &&\n        !PatternDecl->isInvalidDecl() && Consumer.wantsNeverCTemplateSource())\n      Consumer.HandleNeverCFunctionTemplateBodySource(\n          Function, NeverCCompatibleFunctionTemplate, PatternDecl, DC);\n\n    if (auto *Listener = getASTMutationListener())\n      Listener->FunctionDefinitionInstantiated(Function);\n// Independent successful class-friend target boundary.\n  // Finish handling of friends.\n  if (isFriend) {\n    DC->makeDeclVisibleInContext(Inst);\n    if (!Inst->isInvalidDecl() && !RecordInst->isInvalidDecl() &&\n        !D->isInvalidDecl() && !Pattern->isInvalidDecl() &&\n        Inst->getTemplatedDecl() == RecordInst &&\n        SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n      if (auto *NeverCGrantingClass = dyn_cast<CXXRecordDecl>(Owner))\n        SemaRef.getASTConsumer().HandleNeverCFriendClassTemplateSource(\n            Inst, D, NeverCGrantingClass, DC, PrevClassTemplate);\n    return Inst;\n  }\n// End source contract fixture.\n'
        # Independent pinned context-walk span. The namespace declaration stays
        # intact; only lookup while instantiating its friend body changes owner.
        friend_context_original = '''      if (FunctionDecl *FD = dyn_cast<FunctionDecl>(DC)) {
        if (FD->getFriendObjectKind() &&
            FD->getNonTransparentDeclContext()->isFileContext()) {
          DC = FD->getLexicalDeclContext();
          continue;
        }
        // An implicit deduction guide acts as if it's within the class template'''
        friend_context_expected = '''      if (FunctionDecl *FD = dyn_cast<FunctionDecl>(DC)) {
        if (FD->getNonTransparentDeclContext()->isFileContext()) {
          // NeverC visible friend bodies resolve class members in the same
          // compatible lexical owner used by InstantiateFunctionDefinition.
          // A namespace redeclaration can own FD without carrying friendship.
          DeclContext *NeverCFriendContext = nullptr;
          if (auto *Primary = FD->getPrimaryTemplate()) {
            for (auto *Redecl : Primary->redecls()) {
              auto *Template = cast<FunctionTemplateDecl>(Redecl);
              if (!Template->isCompatibleWithDefinition())
                continue;
              if (Template->getFriendObjectKind()) {
                auto *Definition = Template->getTemplatedDecl()->getDefinition();
                auto *Lexical = Definition ? Definition->getLexicalDeclContext()
                                           : Template->getLexicalDeclContext();
                if (isa<CXXRecordDecl>(Lexical) && !Lexical->isDependentContext())
                  NeverCFriendContext = Lexical;
              }
              break;
            }
          }
          if (NeverCFriendContext) {
            DC = NeverCFriendContext;
            continue;
          }
          if (FD->getFriendObjectKind()) {
            DC = FD->getLexicalDeclContext();
            continue;
          }
        }
        // An implicit deduction guide acts as if it's within the class template'''
        explicit_instantiate_original += "\n" + friend_context_original
        explicit_instantiate_expected += "\n" + friend_context_expected
        files['clang/lib/Sema/SemaTemplateInstantiateDecl.cpp'] = explicit_instantiate_original
        explicit_header_original += "\n  class Decl;\n"
        explicit_header_expected += "\n  class Decl;\n"
        for before, after in OPERATION_TRAIT_PATCHES[0][1]:
            if before in explicit_header_original:
                explicit_header_original = explicit_header_original.replace(before, after, 1)
                explicit_header_expected = explicit_header_expected.replace(before, after, 1)
            else:
                explicit_header_original += "\n" + after
                explicit_header_expected += "\n" + after
        files['clang/include/clang/AST/ASTConsumer.h'] = explicit_header_original
        files['clang/lib/Sema/SemaTemplate.cpp'] = explicit_source_original
        files['clang/lib/Sema/SemaTemplateDeduction.cpp'] = explicit_deduction_original
        files['clang/include/clang/Sema/Sema.h'] = explicit_sema_original
        files['clang/include/clang/Sema/TemplateDeduction.h'] = explicit_deduction_info_original
        reference_original = "// Before reference sentinel.\n      else\n        Conv = cast<CXXConversionDecl>(D);\n\n      // If the conversion function doesn't return a reference type,\n      // it can't be considered for this conversion unless we're allowed to\n      // consider rvalues.\n      // FIXME: Do we need to make sure that we only consider conversion\n      // candidates with reference-compatible results? That might be needed to\n      // break recursion.\n      if ((AllowRValues ||\n           Conv->getConversionType()->isLValueReferenceType())) {\n        if (ConvTemplate)\n// After reference sentinel.\n"
        reference_expected = "// Before reference sentinel.\n      else\n        Conv = cast<CXXConversionDecl>(D);\n\n      // NeverC deduced reference conversions: determine only return forms\n      // that can become lvalue references before the reference-only filter.\n      if (!AllowRValues && !ConvTemplate && S.getLangOpts().CPlusPlus14 &&\n          Conv->getConversionType()->isUndeducedType()) {\n        QualType NeverCReturn = Conv->getConversionType();\n        const auto *NeverCAuto = NeverCReturn->getAs<AutoType>();\n        const auto *NeverCRValue = NeverCReturn->getAs<RValueReferenceType>();\n        bool NeverCCanBeLValue =\n            (NeverCAuto && NeverCAuto->isDecltypeAuto()) ||\n            (NeverCRValue &&\n             !NeverCRValue->getPointeeType().hasQualifiers() &&\n             NeverCRValue->getPointeeType()->getAs<AutoType>());\n        if (NeverCCanBeLValue &&\n            S.DeduceReturnType(Conv, Initializer->getExprLoc()))\n          continue;\n      }\n\n      // If the conversion function doesn't return a reference type,\n      // it can't be considered for this conversion unless we're allowed to\n      // consider rvalues.\n      // FIXME: Do we need to make sure that we only consider conversion\n      // candidates with reference-compatible results? That might be needed to\n      // break recursion.\n      if ((AllowRValues ||\n           Conv->getConversionType()->isLValueReferenceType())) {\n        if (ConvTemplate)\n// After reference sentinel.\n"
        files["clang/lib/Sema/SemaInit.cpp"] = reference_original
        argument_reference_original = "// Before reference argument.\n    else\n      Conv = cast<CXXConversionDecl>(D);\n\n    if (AllowRvalues) {\n      // If we are initializing an rvalue reference, don't permit conversion\n      // functions that return lvalues.\n// After reference argument.\n"
        argument_reference_expected = "// Before reference argument.\n    else\n      Conv = cast<CXXConversionDecl>(D);\n\n    // NeverC reference-argument deduction precedes result-type filtering.\n    if (!ConvTemplate && S.getLangOpts().CPlusPlus14 &&\n        Conv->getConversionType()->isUndeducedType()) {\n      QualType NeverCArgumentResult = Conv->getConversionType();\n      const auto *NeverCAuto = NeverCArgumentResult->getAs<AutoType>();\n      const auto *NeverCRValue = NeverCArgumentResult->getAs<RValueReferenceType>();\n      const auto *NeverCLValue = NeverCArgumentResult->getAs<LValueReferenceType>();\n      bool NeverCCanBeLValue =\n          (NeverCAuto && NeverCAuto->isDecltypeAuto()) ||\n          (NeverCRValue && !NeverCRValue->getPointeeType().hasQualifiers() &&\n           NeverCRValue->getPointeeType()->getAs<AutoType>());\n      // Preserve the definite lvalue exclusion before touching a lazy body.\n      bool NeverCExcludedLValue = DeclType->isRValueReferenceType() &&\n          NeverCLValue && !NeverCLValue->getPointeeType()->isFunctionType();\n      bool NeverCNeedsResult = AllowRvalues ? !NeverCExcludedLValue\n                                          : NeverCCanBeLValue;\n      if (NeverCNeedsResult && S.DeduceReturnType(Conv, Init->getExprLoc()))\n        continue;\n    }\n\n    if (AllowRvalues) {\n      // If we are initializing an rvalue reference, don't permit conversion\n      // functions that return lvalues.\n// After reference argument.\n"
        files["clang/lib/Sema/SemaOverload.cpp"] = argument_reference_original
        files.update(math_sources)
        for name in notice_names:
            files["llvm/lib/Support/" + name] = (
                "// Controlled transformation fixture notice.\nint fixture_value;\n")

        call_overload_header_original = '    ImplicitConversionSequence()\n        : ConversionKind(Uninitialized),\n          InitializerListOfIncompleteArray(false) {\n// Copy constructor source.\n          InitializerListContainerType(Other.InitializerListContainerType) {\n// Conversion kind reset.\n      ConversionKind = K;\n    }\n// Ambiguous conversion reset.\n      ConversionKind = AmbiguousConversion;\n      Ambiguous.construct();\n'
        call_overload_header_expected = '    // Source-only metadata is outside the conversion union and never ranked.\n    SourceLocation NeverCTemplateDeductionLocation;\n\n    ImplicitConversionSequence()\n        : ConversionKind(Uninitialized),\n          InitializerListOfIncompleteArray(false),\n          NeverCTemplateDeductionLocation() {\n// Copy constructor source.\n          InitializerListContainerType(Other.InitializerListContainerType),\n          NeverCTemplateDeductionLocation(Other.NeverCTemplateDeductionLocation) {\n// Conversion kind reset.\n      ConversionKind = K;\n      NeverCTemplateDeductionLocation = {};\n    }\n// Ambiguous conversion reset.\n      ConversionKind = AmbiguousConversion;\n      NeverCTemplateDeductionLocation = {};\n      Ambiguous.construct();\n'
        call_overload_original = '#include "clang/AST/ASTContext.h"\n// Constructor ranking source.\n        if (ToCanon != FromCanon)\n          ICS.Standard.Second = ICK_Derived_To_Base;\n      }\n    }\n    break;\n\n  case OR_Ambiguous:\n// Reference conversion selection.\n    ICS.UserDefined.FoundConversionFunction = Best->FoundDecl;\n    ICS.UserDefined.EllipsisConversion = false;\n// Contextual conversion result.\n  if (Result.isInvalid())\n    return true;\n  // Record usage of conversion in an implicit cast.\n'
        call_overload_expected = '#include "clang/AST/ASTConsumer.h"\n#include "clang/AST/ASTContext.h"\n// Constructor ranking source.\n        if (ToCanon != FromCanon)\n          ICS.Standard.Second = ICK_Derived_To_Base;\n      }\n    }\n    // Keep the original candidate location even when constructor ranking\n    // represents the selected user conversion as a standard CopyConstructor.\n    if (S.getASTConsumer().wantsNeverCTemplateSource()) {\n      const auto *NeverCSelectedFunction = ICS.isUserDefined()\n          ? ICS.UserDefined.ConversionFunction : ICS.Standard.CopyConstructor;\n      if (NeverCSelectedFunction && NeverCSelectedFunction->getPrimaryTemplate())\n        ICS.NeverCTemplateDeductionLocation = Conversions.getLocation();\n    }\n    break;\n\n  case OR_Ambiguous:\n// Reference conversion selection.\n    ICS.UserDefined.FoundConversionFunction = Best->FoundDecl;\n    ICS.UserDefined.EllipsisConversion = false;\n    if (S.getASTConsumer().wantsNeverCTemplateSource() &&\n        Best->Function->getPrimaryTemplate())\n      ICS.NeverCTemplateDeductionLocation = CandidateSet.getLocation();\n// Contextual conversion result.\n  if (Result.isInvalid())\n    return true;\n  if (Conversion->getPrimaryTemplate() &&\n      SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n    SemaRef.getASTConsumer().HandleNeverCSelectedTemplateCallSource(\n        Result.get(), Conversion, Loc);\n  // Record usage of conversion in an implicit cast.\n'
        call_init_original = '#include "clang/AST/ASTContext.h"\n// Copy initialization result.\n  // If we\'re supposed to bind temporaries, do so.\n  if (!CurInit.isInvalid() && shouldBindAsTemporary(Entity))\n// Direct constructor initialization.\n  // Only check access if all of that succeeded.\n  S.CheckConstructorAccess(Loc, Constructor, Step.Function.FoundDecl, Entity);\n// Selected conversion result.\n      CurInit = ImplicitCastExpr::Create(\n          S.Context, CurInit.get()->getType(), CastKind, CurInit.get(), nullptr,\n          CurInit.get()->getValueKind(), S.CurFPFeatureOverrides());\n// Array empty-initialization loop.\n  bool SkipEmptyInitChecks = false;\n  for (uint64_t Init = 0; Init != NumElements; ++Init) {\n// Actual empty element.\n        Filler = ElementInit.getAs<Expr>();\n      }\n\n      if (hadError) {\n        // Do nothing\n      } else if (VerifyOnly) {\n'
        call_init_expected = '#include "clang/AST/ASTConsumer.h"\n#include "clang/AST/ASTContext.h"\n// Copy initialization result.\n  if (!CurInit.isInvalid() && Constructor->getPrimaryTemplate() &&\n      S.getASTConsumer().wantsNeverCTemplateSource())\n    S.getASTConsumer().HandleNeverCSelectedTemplateCallSource(\n        CurInit.get(), Constructor, CandidateSet.getLocation());\n\n  // If we\'re supposed to bind temporaries, do so.\n  if (!CurInit.isInvalid() && shouldBindAsTemporary(Entity))\n// Direct constructor initialization.\n  if (Constructor->getPrimaryTemplate() &&\n      S.getASTConsumer().wantsNeverCTemplateSource())\n    S.getASTConsumer().HandleNeverCSelectedTemplateCallSource(\n        CurInit.get(), Constructor, Kind.getLocation());\n\n  // Only check access if all of that succeeded.\n  S.CheckConstructorAccess(Loc, Constructor, Step.Function.FoundDecl, Entity);\n// Selected conversion result.\n      if (Fn->getPrimaryTemplate() &&\n          S.getASTConsumer().wantsNeverCTemplateSource())\n        S.getASTConsumer().HandleNeverCSelectedTemplateCallSource(\n            CurInit.get(), Fn, FailedCandidateSet.getLocation());\n\n      CurInit = ImplicitCastExpr::Create(\n          S.Context, CurInit.get()->getType(), CastKind, CurInit.get(), nullptr,\n          CurInit.get()->getValueKind(), S.CurFPFeatureOverrides());\n// Array empty-initialization loop.\n  // NeverC requesting consumers retain separate materializations in each\n  // omitted array element. The reservation is made once, before list growth.\n  bool NeverCSeparateArrayFillers = false;\n  bool SkipEmptyInitChecks = false;\n  for (uint64_t Init = 0; Init != NumElements; ++Init) {\n// Actual empty element.\n        Filler = ElementInit.getAs<Expr>();\n      }\n\n      // An unknown outer bound uses one sentinel to build its loop filler;\n      // it is not an omitted element that can be expanded into the prefix.\n      if (!hadError && !VerifyOnly && !FillWithNoInit &&\n          !Entity.isVariableLengthArrayNew() &&\n          ElementEntity.getKind() == InitializedEntity::EK_ArrayElement) {\n        using NeverCFillerAction = ASTConsumer::NeverCArrayFillerAction;\n        auto NeverCAction = NeverCSeparateArrayFillers\n            ? NeverCFillerAction::Separate\n            : SemaRef.getASTConsumer().HandleNeverCArrayFiller(\n                  SemaRef.Context, Filler, NumElements - Init, NumElements);\n        if (NeverCAction == NeverCFillerAction::Invalid) {\n          hadError = true;\n          return;\n        }\n        if (NeverCAction == NeverCFillerAction::Separate) {\n          if (!NeverCSeparateArrayFillers) {\n            // The requesting consumer bounds this size before narrowing it.\n            ILE->resizeInits(SemaRef.Context, unsigned(NumElements));\n            NumInits = ILE->getNumInits();\n            NeverCSeparateArrayFillers = true;\n            RequiresSecondPass = true;\n          }\n          // Keep this first result; each later empty slot gets its own\n          // PerformEmptyInit with the actual element entity and index.\n          ILE->setInit(Init, Filler);\n          SemaRef.getASTConsumer().HandleNeverCArrayFillerElement(Filler);\n          continue;\n        }\n      }\n\n      if (hadError) {\n        // Do nothing\n      } else if (VerifyOnly) {\n'
        call_expr_cxx_original = '#include "clang/AST/ASTContext.h"\n// Conversion builder signature.\n                                       bool HadMultipleCandidates,\n                                       Expr *From) {\n  switch (Kind) {\n// Constructed result before temporary binding.\n    if (Result.isInvalid())\n      return ExprError();\n\n    return S.MaybeBindToTemporary(Result.getAs<Expr>());\n  }\n\n  case CK_UserDefinedConversion:\n// Conversion result before cast wrapper.\n    if (Result.isInvalid())\n      return ExprError();\n    // Record usage of conversion in an implicit cast.\n// Standard conversion result.\n    From = Res.get();\n    break;\n  }\n\n  case ImplicitConversionSequence::UserDefinedConversion:\n// Conversion builder invocation.\n          ICS.UserDefined.HadMultipleCandidates, From);\n// Final allocation expression.\n  return CXXNewExpr::Create(Context, UseGlobal, OperatorNew, OperatorDelete,\n                            PassAlignment, UsualArrayDeleteWantsSize,\n                            PlacementArgs, TypeIdParens, ArraySize, InitStyle,\n                            Initializer, ResultType, AllocTypeInfo, Range,\n                            DirectInitRange);\n'
        call_expr_cxx_expected = '#include "clang/AST/ASTConsumer.h"\n#include "clang/AST/ASTContext.h"\n// Conversion builder signature.\n                                       bool HadMultipleCandidates,\n                                       Expr *From,\n                                       SourceLocation NeverCTemplateLocation) {\n  switch (Kind) {\n// Constructed result before temporary binding.\n    if (Result.isInvalid())\n      return ExprError();\n\n    if (Constructor->getPrimaryTemplate() &&\n        S.getASTConsumer().wantsNeverCTemplateSource())\n      S.getASTConsumer().HandleNeverCSelectedTemplateCallSource(\n          Result.get(), Constructor, NeverCTemplateLocation);\n    return S.MaybeBindToTemporary(Result.getAs<Expr>());\n  }\n\n  case CK_UserDefinedConversion:\n// Conversion result before cast wrapper.\n    if (Result.isInvalid())\n      return ExprError();\n    if (Conv->getPrimaryTemplate() &&\n        S.getASTConsumer().wantsNeverCTemplateSource())\n      S.getASTConsumer().HandleNeverCSelectedTemplateCallSource(\n          Result.get(), Conv, NeverCTemplateLocation);\n    // Record usage of conversion in an implicit cast.\n// Standard conversion result.\n    if (ICS.Standard.CopyConstructor &&\n        ICS.Standard.CopyConstructor->getPrimaryTemplate() &&\n        getASTConsumer().wantsNeverCTemplateSource())\n      getASTConsumer().HandleNeverCSelectedTemplateCallSource(\n          Res.get(), ICS.Standard.CopyConstructor,\n          ICS.NeverCTemplateDeductionLocation);\n    From = Res.get();\n    break;\n  }\n\n  case ImplicitConversionSequence::UserDefinedConversion:\n// Conversion builder invocation.\n          ICS.UserDefined.HadMultipleCandidates, From,\n          ICS.NeverCTemplateDeductionLocation);\n// Final allocation expression.\n  auto *NeverCAllocationResult = CXXNewExpr::Create(\n      Context, UseGlobal, OperatorNew, OperatorDelete, PassAlignment,\n      UsualArrayDeleteWantsSize, PlacementArgs, TypeIdParens, ArraySize,\n      InitStyle, Initializer, ResultType, AllocTypeInfo, Range, DirectInitRange);\n  if (Consumer.wantsNeverCTemplateSource() && OperatorNew &&\n      OperatorNew->getPrimaryTemplate())\n    Consumer.HandleNeverCSelectedTemplateCallSource(\n        NeverCAllocationResult, OperatorNew, StartLoc);\n  return NeverCAllocationResult;\n'
        files['clang/include/clang/Sema/Overload.h'] = call_overload_header_original
        files['clang/lib/Sema/SemaOverload.cpp'] = argument_reference_original + call_overload_original
        files['clang/lib/Sema/SemaInit.cpp'] = reference_original + call_init_original
        # This independently tested patch group stays complete while the
        # following test varies the older callback-source group in the same file.
        call_expr_cxx_original += ARRAY_QUERY_PATCHES[0][2]
        call_expr_cxx_expected += ARRAY_QUERY_PATCHES[0][2]
        files['clang/lib/Sema/TreeTransform.h'] = ARRAY_QUERY_PATCHES[1][2]
        files['clang/lib/Sema/SemaExceptionSpec.cpp'] = PSEUDO_DESTRUCTOR_AFTER
        operation_source = "\n".join(after for _, after in OPERATION_TRAIT_PATCHES[1][1])
        call_expr_cxx_original += "\n" + operation_source
        call_expr_cxx_expected += "\n" + operation_source
        files['clang/lib/Sema/SemaExprCXX.cpp'] = call_expr_cxx_original
        with tempfile.TemporaryDirectory(prefix="neverc-isolate-source-") as temporary:
            root = Path(temporary)
            source = root / "source"
            for name, contents in files.items():
                path = source / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(contents, encoding="utf-8")
            output = root / "generated" / "PrivatePrefix.h"
            command = [sys.executable, "-I", "-B",
                       str(Path(__file__).resolve().with_name("IsolateSymbols.py")),
                       "--source", str(source), "--output", str(output)]

            def run_script(success, expected_error=None):
                try:
                    result = subprocess.run(
                        command, cwd=root, capture_output=True, text=True,
                        encoding="utf-8", errors="replace", timeout=30, check=False)
                except subprocess.TimeoutExpired as error:
                    self.fail(f"IsolateSymbols timed out: {command!r}\n"
                              f"stdout: {error.stdout!r}\nstderr: {error.stderr!r}")
                diagnostic = (f"{command!r}\nexit: {result.returncode}\n"
                              f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}")
                if success:
                    self.assertEqual(result.returncode, 0, diagnostic)
                    self.assertIn("Isolated llvm namespace and", result.stdout, diagnostic)
                else:
                    self.assertNotEqual(result.returncode, 0, diagnostic)
                    if expected_error is None:
                        self.assertIn("Unexpected pinned LLVM PointerBounds", result.stderr,
                                      diagnostic)
                    else:
                        self.assertEqual(result.stderr, expected_error + "\n", diagnostic)

            def snapshot_all_files():
                return {path.relative_to(root): path.read_bytes()
                        for path in root.rglob("*") if path.is_file()}

            # The new source check must run before *any* existing rewrite or
            # generated-file creation, including when the pinned file is absent.
            setup_path = source / "llvm/lib/WindowsDriver/MSVCPaths.cpp"
            setup_path.unlink()
            untouched = snapshot_all_files()
            run_script(False, "Unexpected pinned LLVM Setup BSTR source in " + str(setup_path))
            self.assertEqual(snapshot_all_files(), untouched)
            self.assertFalse(output.exists())
            setup_path.write_text(original_setup, encoding="utf-8")

            run_script(True)
            reference_path = source / "clang/lib/Sema/SemaInit.cpp"
            self.assertEqual(reference_path.read_text(encoding="utf-8"), reference_expected + call_init_expected)
            argument_reference_path = source / "clang/lib/Sema/SemaOverload.cpp"
            self.assertEqual(argument_reference_path.read_text(encoding="utf-8"), argument_reference_expected + call_overload_expected)
            full_initializer_path = source / "clang/lib/Sema/SemaExpr.cpp"
            self.assertEqual(full_initializer_path.read_text(encoding="utf-8"),
                             full_initializer_expected + deduced_variable_expected)
            # All callback files must match the independently written contract.
            explicit_paths = [
                source / 'clang/include/clang/AST/ASTConsumer.h',
                source / 'clang/lib/Sema/SemaTemplate.cpp',
                source / 'clang/lib/Sema/SemaTemplateDeduction.cpp',
                source / 'clang/include/clang/Sema/Sema.h',
                source / 'clang/include/clang/Sema/TemplateDeduction.h',
                source / 'clang/lib/Sema/SemaTemplateInstantiateDecl.cpp',
                source / 'clang/include/clang/Sema/Overload.h',
                source / 'clang/lib/Sema/SemaOverload.cpp',
                source / 'clang/lib/Sema/SemaInit.cpp',
                source / 'clang/lib/Sema/SemaExprCXX.cpp',
            ]
            explicit_original = [explicit_header_original, explicit_source_original, explicit_deduction_original, explicit_sema_original, explicit_deduction_info_original, explicit_instantiate_original, call_overload_header_original, argument_reference_expected + call_overload_original, reference_expected + call_init_original, call_expr_cxx_original]
            explicit_expected = [explicit_header_expected, explicit_source_expected, explicit_deduction_expected, explicit_sema_expected, explicit_deduction_info_expected, explicit_instantiate_expected, call_overload_header_expected, argument_reference_expected + call_overload_expected, reference_expected + call_init_expected, call_expr_cxx_expected]
            for path, expected in zip(explicit_paths, explicit_expected):
                self.assertEqual(path.read_text(encoding="utf-8"), expected)
            # A correct text rewrite must also declare every callback type.
            # This header deliberately has no includes; upstream supplies the
            # CXXRecordDecl/VarDecl forwards retained in the fixture above.
            consumer = explicit_paths[0].read_text(encoding="utf-8")
            declared = set(re.findall(r"\b(?:class|struct)\s+(\w+)\s*;", consumer))
            referenced = set(re.findall(r"\b([A-Z]\w*)\s*(?:\*|&)", consumer))
            self.assertEqual(referenced - declared, set(),
                             "Private ASTConsumer callback has an undeclared parameter type")
            access_path = source / "clang/lib/Sema/SemaAccess.cpp"
            self.assertEqual(access_path.read_text(encoding="utf-8"),
                             expected_access + "\n\n" + rewritten_declaration_access)
            default_header = source / "clang/include/clang/AST/DeclCXX.h"
            default_source = source / "clang/lib/AST/DeclCXX.cpp"
            rewritten_default_header = default_header.read_text(encoding="utf-8")
            rewritten_default_source = default_source.read_text(encoding="utf-8")
            self.assertEqual(rewritten_default_header, original_default_header.replace(
                "  NamedDecl *getTargetDecl() const { return Underlying; }",
                "  NamedDecl *getTargetDecl() const;"))
            self.assertEqual(rewritten_default_source.count("UsingShadowDecl::getTargetDecl() const"), 1)
            self.assertEqual(rewritten_default_source.count("NeverC C++17 imported defaults"), 1)
            self.assertEqual(rewritten_default_source.count(member_pattern_expected), 1)
            self.assertNotIn(member_pattern_original, rewritten_default_source)
            self.assertTrue(rewritten_default_source.startswith("// Before source sentinel.\n"))
            self.assertTrue(rewritten_default_source.endswith(
                "UsingShadowDecl::UsingShadowDecl(Kind K) {}\n// After source sentinel.\n"))
            declaration_marker = source / "clang/include/clang/Sema/DelayedDiagnostic.h"
            declaration_parser = source / "clang/lib/Parse/ParseCXXInlineMethods.cpp"
            self.assertEqual(declaration_marker.read_text(encoding="utf-8"), rewritten_declaration_marker)
            self.assertEqual(declaration_parser.read_text(encoding="utf-8"), rewritten_declaration_parser)
            rewritten_setup = setup_path.read_text(encoding="utf-8")
            self.assertEqual(rewritten_setup, expected_setup)
            self.assertEqual(rewritten_setup.count(expected_owner), 1)
            self.assertNotIn("GetAddress", rewritten_setup)
            self.assertNotIn("bstr_t", rewritten_setup)
            loop = source / "llvm/lib/Transforms/Utils/LoopUtils.cpp"
            self.assertEqual(loop.read_text(encoding="utf-8"), expected_loop)
            self.assertEqual((source / "llvm/lib/IR/IntrinsicInst.cpp").read_text(
                encoding="utf-8"), expected_intrinsic)
            self.assertEqual((source / "llvm/include/llvm/Transforms/Utils/Debugify.h").read_text(
                encoding="utf-8"), expected_debugify)
            math_paths = {name: source / name for name in math_calls}
            for name, path in math_paths.items():
                self.assertEqual(path.read_text(encoding="utf-8"), expected_math[name], name)
            prefix = output.read_text(encoding="utf-8")
            for name in ("llvm", "LLVMFixture0000", "LLVMFixture0899", "LLVMIsAArgument",
                         "llvm_blake3_compress_in_place"):
                self.assertIn(f"#define {name} neverc_cpp_{name}\n", prefix)
            self.assertNotIn("#define PointerBounds", prefix)
            self.assertNotRegex(prefix, r"(?m)^\s*#\s*(?:define|undef)\s+_?bstr_t\b")
            notices = output.parent / "NeverCCppThirdPartyNotices.txt"
            expected_notices = "Additional notices from the pinned LLVM 20.1.8 sources.\n"
            for name in notice_names:
                expected_notices += ("\n\n===== llvm/lib/Support/" + name + " =====\n\n"
                                     "// Controlled transformation fixture notice.\n")
            self.assertEqual(notices.read_text(encoding="utf-8"), expected_notices)
            stable = {path: path.read_bytes() for path in (
                loop, output, notices, source / "llvm/lib/IR/IntrinsicInst.cpp",
                source / "llvm/include/llvm/Transforms/Utils/Debugify.h",
                setup_path, access_path, default_header, default_source, declaration_marker,
                declaration_parser, reference_path, argument_reference_path, *math_paths.values())}
            run_script(True)
            for path, contents in stable.items():
                self.assertEqual(path.read_bytes(), contents, str(path))

            reference_bad = {
                "missing-anchor": "// Missing reference conversion source.\n",
                "duplicate-original": reference_original + reference_original,
                "duplicate-rewritten": reference_expected + reference_expected,
                "mixed": reference_original + reference_expected,
                "drift-filter": reference_original.replace("isLValueReferenceType", "isReferenceType"),
                "partial-deduction": reference_expected.replace("S.DeduceReturnType(Conv, Initializer->getExprLoc())", "false"),
                "lost-shape": reference_expected.replace("NeverCAuto->isDecltypeAuto()", "true"),
                "lost-qualifiers": reference_expected.replace("!NeverCRValue->getPointeeType().hasQualifiers()", "true"),
                "stray-marker": reference_original + "// NeverC deduced reference conversions\n",
                "stray-partial": reference_original + "QualType NeverCReturn;\n",
            }
            for label, bad in reference_bad.items():
                with self.subTest(reference_source=label):
                    reference_path.write_text(bad + call_init_expected, encoding="utf-8")
                    untouched = snapshot_all_files()
                    run_script(False, "Unexpected pinned Clang deduced reference conversion source")
                    self.assertEqual(snapshot_all_files(), untouched)
            reference_path.unlink()
            untouched = snapshot_all_files()
            run_script(False, "Unexpected pinned Clang explicit-instantiation source in " + str(reference_path))
            self.assertEqual(snapshot_all_files(), untouched)
            reference_path.write_text(reference_expected + call_init_expected, encoding="utf-8")

            argument_reference_bad = {
                "missing-anchor": "// Missing reference argument.\n",
                "duplicate-original": argument_reference_original * 2,
                "duplicate-rewritten": argument_reference_expected * 2,
                "mixed": argument_reference_original + argument_reference_expected,
                "drift-filter": argument_reference_original.replace("if (AllowRvalues)", "if (false)"),
                "lost-deduction": argument_reference_expected.replace("S.DeduceReturnType(Conv, Init->getExprLoc())", "false"),
                "lost-lvalue-exclusion": argument_reference_expected.replace("AllowRvalues ? !NeverCExcludedLValue", "AllowRvalues ? true"),
                "lost-shape": argument_reference_expected.replace("NeverCAuto->isDecltypeAuto()", "true"),
                "lost-qualifiers": argument_reference_expected.replace("!NeverCRValue->getPointeeType().hasQualifiers()", "true"),
                "stray-marker": argument_reference_original + "// NeverC reference-argument deduction\n",
                "stray-partial": argument_reference_original + "QualType NeverCArgumentResult;\n",
                "stray-gate": argument_reference_original + "bool NeverCNeedsResult;\n",
            }
            for label, bad in argument_reference_bad.items():
                with self.subTest(reference_argument_source=label):
                    argument_reference_path.write_text(bad + call_overload_expected, encoding="utf-8")
                    untouched = snapshot_all_files()
                    run_script(False, "Unexpected pinned Clang reference-argument source")
                    self.assertEqual(snapshot_all_files(), untouched)
            argument_reference_path.unlink()
            untouched = snapshot_all_files()
            run_script(False, "Unexpected pinned Clang explicit-instantiation source in " + str(argument_reference_path))
            self.assertEqual(snapshot_all_files(), untouched)
            argument_reference_path.write_text(argument_reference_expected + call_overload_expected, encoding="utf-8")

            anchor = ("_COM_SMARTPTR_TYPEDEF(ISetupInstance2, "
                      "__uuidof(ISetupInstance2));\n")
            original_version = "    bstr_t VersionString;\n"
            private_version = "    NeverCSetupBstr VersionString;\n"
            setup_states = {
                "missing anchor": original_setup.replace(anchor, "", 1),
                "duplicate anchor": original_setup.replace(anchor, anchor + anchor, 1),
                "missing version declaration": original_setup.replace(original_version, "", 1),
                "duplicate original declaration": original_setup.replace(
                    original_version, original_version * 2, 1),
                "duplicate rewritten declaration": rewritten_setup.replace(
                    private_version, private_version * 2, 1),
                "mixed declarations": original_setup.replace(
                    original_version, original_version + private_version, 1),
                "one declaration rewritten": original_setup.replace(
                    original_version, private_version, 1),
                "one declaration original": rewritten_setup.replace(
                    private_version, original_version, 1),
                "only out call rewritten": original_setup.replace(
                    "VersionString.GetAddress()", "VersionString.out()", 1),
                "old out call remains": rewritten_setup.replace(
                    "VCPathWide.out()", "VCPathWide.GetAddress()", 1),
                "changed version call": original_setup.replace(
                    "GetInstallationVersion(VersionString.GetAddress())",
                    "GetInstallationVersion(nullptr)", 1),
                "changed path call": original_setup.replace(
                    'ResolvePath(L"VC", VCPathWide.GetAddress())',
                    'ResolvePath(L"Other", VCPathWide.GetAddress())', 1),
                "missing parsed get": rewritten_setup.replace(
                    "ParseVersion(VersionString.get(), &VersionNum)",
                    "ParseVersion(VersionString, &VersionNum)", 1),
                "missing path get": rewritten_setup.replace(
                    "std::wstring(VCPathWide.get())", "std::wstring(VCPathWide)", 1),
                "missing version null guard": rewritten_setup.replace(
                    "if (FAILED(HR) || !VersionString.get())", "if (FAILED(HR))", 1),
                "missing path null guard": rewritten_setup.replace(
                    "if (FAILED(HR) || !VCPathWide.get())", "if (FAILED(HR))", 1),
                "extra original declaration": original_setup + "bstr_t Extra;\n",
                "extra private declaration": rewritten_setup + "NeverCSetupBstr Extra;\n",
                "unexpected wrapper macro": original_setup + "#define _bstr_t NeverCSetupBstr\n",
                "owner missing": rewritten_setup.replace(expected_owner, "", 1),
                "duplicate owner": rewritten_setup.replace(
                    expected_owner, expected_owner * 2, 1),
                "owner only rewritten": expected_setup_preamble + "\n" + setup_calls,
                "owner outside guard": setup_preamble + "\n" + expected_owner +
                    expected_setup_calls,
                "owner in global namespace": rewritten_setup.replace(
                    expected_owner, expected_owner.replace("namespace llvm {\n", "", 1), 1),
                "changed owner destructor": rewritten_setup.replace(
                    "~NeverCSetupBstr() noexcept { reset(); }",
                    "~NeverCSetupBstr() noexcept {}", 1),
                "owner without copy deletion": rewritten_setup.replace(
                    "  NeverCSetupBstr(const NeverCSetupBstr &) = delete;\n", "", 1),
                "owner without out reset": rewritten_setup.replace(
                    "  BSTR *out() noexcept {\n    reset();\n",
                    "  BSTR *out() noexcept {\n", 1),
            }
            for state, invalid_source in setup_states.items():
                with self.subTest(setup_bstr_state=state):
                    # Every other file is an original, valid input. If Setup
                    # validation ran later, the intrinsic/math patches would
                    # modify these bytes before the failure was reported.
                    for name, contents in files.items():
                        (source / name).write_text(contents, encoding="utf-8")
                    setup_path.write_text(invalid_source, encoding="utf-8")
                    untouched = snapshot_all_files()
                    run_script(False, "Unexpected pinned LLVM Setup BSTR source in " +
                               str(setup_path))
                    self.assertEqual(snapshot_all_files(), untouched, state)
            for name, contents in files.items():
                (source / name).write_text(contents, encoding="utf-8")
            run_script(True)
            for path, contents in stable.items():
                self.assertEqual(path.read_bytes(), contents, str(path))

            full_initializer_states = {
                "missing block": "// Missing lazy initialization.\n",
                "duplicate original": full_initializer_original * 2,
                "duplicate rewritten": full_initializer_expected * 2,
                "mixed blocks": full_initializer_original + full_initializer_expected,
                "partial condition": full_initializer_original.replace(
                    "if (isTemplateInstantiation(", "if (NeverCCopiedFull || isTemplateInstantiation(", 1),
                "removed consumer guard": full_initializer_expected.replace(
                    "Consumer.wantsNeverCTemplateSource() &&", "true &&", 1),
                "removed member origin": full_initializer_expected.replace(
                    "ParentRD->getInstantiatedFromMemberClass() &&", "true &&", 1),
                "changed member kind": full_initializer_expected.replace(
                    "NeverCMember->getTemplateSpecializationKind()", "ParentRD->getTemplateSpecializationKind()", 1),
                "changed class scope": full_initializer_expected.replace(
                    "NeverCFull->isClassScopeExplicitSpecialization()", "NeverCFull->isExplicitSpecialization()", 1),
                "changed arguments": full_initializer_expected.replace(
                    "getTemplateInstantiationArgs(Field)", "getTemplateInstantiationArgs(ParentRD)", 1),
                "orphan marker": full_initializer_original + "bool NeverCCopiedFull;\n",
            }
            for state, contents in full_initializer_states.items():
                with self.subTest(copied_full_initializer_state=state):
                    full_initializer_path.write_text(contents + deduced_variable_expected,
                                                     encoding="utf-8")
                    untouched = snapshot_all_files()
                    run_script(False, "Unexpected pinned Clang copied full initializer source in " +
                               str(full_initializer_path))
                    self.assertEqual(snapshot_all_files(), untouched, state)
            full_initializer_path.unlink()
            untouched = snapshot_all_files()
            run_script(False, "Unexpected pinned Clang copied full initializer source in " +
                       str(full_initializer_path))
            self.assertEqual(snapshot_all_files(), untouched)
            full_initializer_path.write_text(full_initializer_original + deduced_variable_original,
                                             encoding="utf-8")
            run_script(True)
            for path, contents in stable.items():
                self.assertEqual(path.read_bytes(), contents, str(path))

            deduced_variable_states = {
                "missing block": "// Missing variable instantiation.\n",
                "duplicate original": deduced_variable_original * 2,
                "duplicate rewritten": deduced_variable_expected * 2,
                "mixed blocks": deduced_variable_original + deduced_variable_expected,
                "removed consumer guard": deduced_variable_expected.replace(
                    "SemaRef.getASTConsumer().wantsNeverCTemplateSource() &&", "true &&", 1),
                "changed type condition": deduced_variable_expected.replace(
                    "Var->getType()->isUndeducedType()", "true", 1),
                "changed definition target": deduced_variable_expected.replace(
                    "InstantiateVariableDefinition(PointOfInstantiation, Var)",
                    "InstantiateVariableDefinition(PointOfInstantiation, Pattern)", 1),
                "missing reference refresh": deduced_variable_expected.replace(
                    "          DRE->setDecl(DRE->getDecl());\n", "", 1),
                "missing member refresh": deduced_variable_expected.replace(
                    "          ME->setMemberDecl(ME->getMemberDecl());\n", "", 1),
                "orphan marker": deduced_variable_original + "bool NeverCNeedsVariableType;\n",
            }
            for state, contents in deduced_variable_states.items():
                with self.subTest(deduced_variable_state=state):
                    full_initializer_path.write_text(full_initializer_expected + contents,
                                                     encoding="utf-8")
                    untouched = snapshot_all_files()
                    run_script(False, "Unexpected pinned Clang deduced variable source in " +
                               str(full_initializer_path))
                    self.assertEqual(snapshot_all_files(), untouched, state)
            full_initializer_path.write_text(full_initializer_expected + deduced_variable_original,
                                             encoding="utf-8")
            run_script(True)
            for path, contents in stable.items():
                self.assertEqual(path.read_bytes(), contents, str(path))

            access_states = {
                "missing branch": "",
                "duplicate original": original_access * 2,
                "duplicate rewritten": expected_access * 2,
                "mixed branches": original_access + expected_access,
                "partial condition": original_access.replace(
                    "if (Function->getFriendObjectKind())", "if (false)", 1),
                "missing function identity": expected_access.replace(
                    "        Functions.push_back(Function->getCanonicalDecl());\n", "", 1),
                "partial nesting check": expected_access.replace(
                    "LexicalRecord->getDeclContext()", "Function->getDeclContext()", 1),
                "extra marker": original_access + "\nconst auto *LexicalRecord = nullptr;\n",
            }
            for state, contents in access_states.items():
                with self.subTest(nested_friend_access_state=state):
                    access_path.write_text(contents, encoding="utf-8")
                    untouched = snapshot_all_files()
                    run_script(False, "Unexpected pinned Clang nested friend access source in " + str(access_path))
                    self.assertEqual(snapshot_all_files(), untouched, state)
            access_path.unlink()
            untouched = snapshot_all_files()
            run_script(False, "Unexpected pinned Clang nested friend access source in " + str(access_path))
            self.assertEqual(snapshot_all_files(), untouched)
            access_path.write_text(original_access + "\n\n" + rewritten_declaration_access, encoding="utf-8")
            run_script(True)
            for path, contents in stable.items():
                self.assertEqual(path.read_bytes(), contents, str(path))

            # All supplemental friend sources are preflighted as one group.
            # Keep the prior body-context patch intact while varying this group.
            declaration_paths = (access_path, declaration_marker, declaration_parser)
            declaration_original = (expected_access + "\n\n" + original_declaration_access,
                                    original_declaration_marker, original_declaration_parser)
            declaration_rewritten = (expected_access + "\n\n" + rewritten_declaration_access,
                                     rewritten_declaration_marker, rewritten_declaration_parser)
            for mask in range(1, 7):
                with self.subTest(nested_declaration_mixed_state=mask):
                    for index, path in enumerate(declaration_paths):
                        path.write_text((declaration_rewritten if mask & (1 << index) else
                                         declaration_original)[index], encoding="utf-8")
                    untouched = snapshot_all_files()
                    run_script(False, "Unexpected pinned Clang nested friend declaration source group")
                    self.assertEqual(snapshot_all_files(), untouched)
            for fragment in ("Diagnostic.NestedFriendAccess = true;", "if (DD.NestedFriendAccess &&"):
                for path, text in zip(declaration_paths, declaration_rewritten):
                    path.write_text(text, encoding="utf-8")
                access_path.write_text(declaration_rewritten[0].replace(fragment, "", 1), encoding="utf-8")
                untouched = snapshot_all_files()
                run_script(False, "Unexpected pinned Clang nested friend declaration source group")
                self.assertEqual(snapshot_all_files(), untouched)
            for index, path in enumerate(declaration_paths):
                for state in ("missing file", "missing anchor", "duplicate block", "extra marker"):
                    with self.subTest(nested_declaration_path=index, state=state):
                        for target, text in zip(declaration_paths, declaration_rewritten):
                            target.write_text(text, encoding="utf-8")
                        if state == "missing file":
                            path.unlink()
                        elif state == "missing anchor":
                            text = declaration_rewritten[index]
                            token = ("Diagnostic.NestedFriendAccess = true;", "bool NestedFriendAccess = false;",
                                     "DefaultContext.emplace(Actions, Function, /*NewThisContext=*/false);")[index]
                            path.write_text(text.replace(token, "", 1), encoding="utf-8")
                        elif state == "duplicate block":
                            extra = (rewritten_declaration_access, rewritten_declaration_marker,
                                     rewritten_declaration_parser)[index]
                            path.write_text(declaration_rewritten[index] + "\n" + extra, encoding="utf-8")
                        else:
                            path.write_text(declaration_rewritten[index] + "\n// NestedFriendAccess\n", encoding="utf-8")
                        untouched = snapshot_all_files()
                        # Missing SemaAccess is rejected by the preceding body patch.
                        error = ("Unexpected pinned Clang nested friend access source in " + str(path)
                                 if index == 0 and state == "missing file" else
                                 "Unexpected pinned Clang nested friend declaration source group")
                        run_script(False, error)
                        self.assertEqual(snapshot_all_files(), untouched)
            for path, text in zip(declaration_paths, declaration_original):
                path.write_text(text, encoding="utf-8")
            run_script(True)
            for path, contents in stable.items():
                self.assertEqual(path.read_bytes(), contents, str(path))

            # Partial, duplicate and drifted callback groups never rewrite any file.
            explicit_states = [
                ("original header only", explicit_header_original, explicit_source_expected),
                ("original source only", explicit_header_expected, explicit_source_original),
                ("missing forward anchor", explicit_header_original.replace("  class ImportDecl;", ""), explicit_source_original),
                ("missing callback anchor", explicit_header_original.replace("  virtual void HandleCXXImplicitFunctionInstantiation(FunctionDecl *D) {}", ""), explicit_source_original),
                ("missing capture anchor", explicit_header_original, explicit_source_original.replace("Declarator &D) {", "Declarator &Other) {")),
                ("missing call anchor", explicit_header_original, explicit_source_original.replace("// C++11 [except.spec]p4", "// Drifted.")),
                ("duplicate header", explicit_header_original * 2, explicit_source_original),
                ("duplicate source", explicit_header_original, explicit_source_original * 2),
                ("partial forward declarations", explicit_header_expected.replace("  struct DeclarationNameInfo;", ""), explicit_source_expected),
                ("missing class-template forward declaration", explicit_header_expected.replace("  class ClassTemplateDecl;", ""), explicit_source_expected),
                ("partial signature", explicit_header_expected.replace("const DeclarationNameInfo &, ", ""), explicit_source_expected),
                ("partial capture", explicit_header_expected, explicit_source_expected.replace("D.hasAttributes()", "false")),
                ("partial call", explicit_header_expected, explicit_source_expected.replace("TemplateArgs, T, NameInfo,", "TemplateArgs, T,")),
                ("missing static header anchor", explicit_header_original.replace("  virtual void HandleCXXStaticMemberVarInstantiation(VarDecl *D) {}", ""), explicit_source_original),
                ("missing static source anchor", explicit_header_original, explicit_source_original.replace("CheckExplicitInstantiation(*this, Prev,", "CheckExplicitInstantiation(*this, Other,")),
                ("partial static signature", explicit_header_expected.replace("VarDecl *, TypeSourceInfo *,", "VarDecl *,"), explicit_source_expected),
                ("partial static call", explicit_header_expected, explicit_source_expected.replace("Prev, T, D.getCXXScopeSpec()", "Prev, nullptr, D.getCXXScopeSpec()")),
                ("duplicate static anchor", explicit_header_original, explicit_source_original + "    CheckExplicitInstantiation(*this, Prev, D.getIdentifierLoc(), true, TSK);\n"),
                ("extra static callback", explicit_header_expected + "// HandleNeverCExplicitStaticDataInstantiation\n", explicit_source_expected),
                ("extra callback", explicit_header_expected + "// HandleNeverCExplicitFunctionInstantiation\n", explicit_source_expected),
                ("extra marker", explicit_header_expected, explicit_source_expected + "// NeverCWrittenAttributes\n"),
            ]
            for state, header_text, source_text in explicit_states:
                with self.subTest(explicit_source_state=state):
                    explicit_paths[0].write_text(header_text, encoding="utf-8")
                    explicit_paths[1].write_text(source_text, encoding="utf-8")
                    explicit_paths[2].write_text(explicit_deduction_expected, encoding="utf-8")
                    explicit_paths[3].write_text(explicit_sema_expected, encoding="utf-8")
                    explicit_paths[4].write_text(explicit_deduction_info_expected, encoding="utf-8")
                    explicit_paths[5].write_text(explicit_instantiate_expected, encoding="utf-8")
                    for path, expected in zip(explicit_paths[6:], explicit_expected[6:]):
                        path.write_text(expected, encoding="utf-8")
                    untouched = snapshot_all_files()
                    if state in ("original header only", "original source only"):
                        error = "Unexpected partial pinned Clang explicit-instantiation source"
                    else:
                        # Derive the damaged file from its contents, so adding
                        # a header case cannot silently default to a source error.
                        index = 0 if header_text not in (explicit_header_original, explicit_header_expected) else 1
                        error = "Unexpected pinned Clang explicit-instantiation source in " + str(explicit_paths[index])
                    run_script(False, error)
                    self.assertEqual(snapshot_all_files(), untouched, state)
            original_group = (1 << 6) - 1
            complete_group = (1 << len(explicit_paths)) - 1
            mixed_masks = {mask for mask in range(1, original_group)}
            mixed_masks |= {mask | (complete_group ^ original_group)
                            for mask in range(1, original_group)}
            mixed_masks |= {1 << index for index in range(len(explicit_paths))}
            mixed_masks |= {complete_group ^ (1 << index)
                            for index in range(len(explicit_paths))}
            mixed_masks |= {original_group, complete_group ^ original_group}
            for mask in sorted(mixed_masks):
                with self.subTest(template_source_mixed_state=mask):
                    for index, path in enumerate(explicit_paths):
                        contents = (explicit_expected if mask & (1 << index) else explicit_original)[index]
                        path.write_text(contents, encoding="utf-8")
                    untouched = snapshot_all_files()
                    run_script(False, "Unexpected partial pinned Clang explicit-instantiation source")
                    self.assertEqual(snapshot_all_files(), untouched)
            default_source_states = [
                (9, 'allocation lost opt-in', call_expr_cxx_expected.replace('Consumer.wantsNeverCTemplateSource() && OperatorNew &&', 'OperatorNew &&', 1)),
                (9, 'allocation wrong selected function', call_expr_cxx_expected.replace('NeverCAllocationResult, OperatorNew, StartLoc', 'NeverCAllocationResult, OperatorDelete, StartLoc', 1)),
                (9, 'allocation wrong selection location', call_expr_cxx_expected.replace('NeverCAllocationResult, OperatorNew, StartLoc', 'NeverCAllocationResult, OperatorNew, TypeRange.getBegin()', 1)),
                (9, 'allocation lost result', call_expr_cxx_expected.replace('return NeverCAllocationResult;', 'return nullptr;', 1)),
                (9, 'allocation missing template test', call_expr_cxx_expected.replace('OperatorNew->getPrimaryTemplate()', 'OperatorNew->getIdentifier()', 1)),
                (9, "allocation orphan result", call_expr_cxx_expected + "// NeverCAllocationResult\n"),
                (5, 'member constant loses redeclaration initializer', explicit_instantiate_expected.replace(
                    '!Var->getAnyInitializer()', '!Var->hasInit()', 1)),
                (5, 'member constant repeats every initializer', explicit_instantiate_expected.replace(
                    '!Var->getAnyInitializer()', 'true', 1)),
                (5, 'member constant suppresses initial instantiation', explicit_instantiate_expected.replace(
                    '!Var->getAnyInitializer()', 'false', 1)),
                (0, "missing default forward", explicit_header_expected.replace("  class TemplateArgument;", "")),
                (0, "partial default signature", explicit_header_expected.replace("TemplateDecl *, NonTypeTemplateParmDecl *,", "TemplateDecl *,")),
                (0, "extra default callback", explicit_header_expected + "// HandleNeverCScalarTemplateDefault\n"),
                (1, "missing default capture", explicit_source_expected.replace("const auto NeverCWrittenDefault = Arg;", "")),
                (1, "partial default conversion", explicit_source_expected.replace("NeverCWrittenDefault, Arg,", "NeverCWrittenDefault, NeverCWrittenDefault,")),
                (1, "partial default canonical", explicit_source_expected.replace("CTAI.CanonicalConverted.back(), TemplateLoc", "CTAI.SugaredConverted.back(), TemplateLoc")),
                (1, "duplicate default marker", explicit_source_expected + "// NeverCWrittenDefault\n"),
                (2, "missing deduction include", explicit_deduction_expected.replace('#include "clang/AST/ASTConsumer.h"\n', "")),
                (2, "partial deduction conversion", explicit_deduction_expected.replace("NeverCWrittenDefault, DefArg,", "NeverCWrittenDefault, NeverCWrittenDefault,")),
                (2, "partial deduction capture", explicit_deduction_expected.replace("const auto NeverCWrittenDefault = DefArg;", "")),
                (2, "duplicate deduction", explicit_deduction_expected * 2),
                (2, "missing deduction anchor", explicit_deduction_original.replace("// Check whether we can actually use the default argument.", "// Drifted.")),
            ]
            for index, state, contents in default_source_states:
                with self.subTest(template_default_source_state=state):
                    for path, expected in zip(explicit_paths, explicit_expected):
                        path.write_text(expected, encoding="utf-8")
                    explicit_paths[index].write_text(contents, encoding="utf-8")
                    untouched = snapshot_all_files()
                    run_script(False, "Unexpected pinned Clang explicit-instantiation source in " + str(explicit_paths[index]))
                    self.assertEqual(snapshot_all_files(), untouched, state)
            # Independent literal anchors cover every source hook and payload.
            template_use_states = [
                (0, 'array filler missing hook', explicit_header_expected.replace('  virtual NeverCArrayFillerAction HandleNeverCArrayFiller(', '// missing array filler hook', 1)),
                (0, 'array filler changed default', explicit_header_expected.replace('return NeverCArrayFillerAction::KeepShared;', 'return NeverCArrayFillerAction::Separate;', 1)),
                (0, 'array filler orphan hook', explicit_header_expected + '// HandleNeverCArrayFillerElement\n'),
                (8, 'array filler lost loop flag', reference_expected + call_init_expected.replace('  bool NeverCSeparateArrayFillers = false;\n', '', 1)),
                (8, 'array filler lost reservation', reference_expected + call_init_expected.replace('            : SemaRef.getASTConsumer().HandleNeverCArrayFiller(', '            : MissingReservation(', 1)),
                (8, 'array filler lost verifier guard', reference_expected + call_init_expected.replace('      if (!hadError && !VerifyOnly && !FillWithNoInit &&', '      if (!hadError &&', 1)),
                (8, 'array filler lost runtime sentinel guard', reference_expected + call_init_expected.replace('          !Entity.isVariableLengthArrayNew() &&\n', '', 1)),
                (8, 'array filler wrong count', reference_expected + call_init_expected.replace('                  SemaRef.Context, Filler, NumElements - Init, NumElements);', '                  SemaRef.Context, Filler, NumElements, NumElements);', 1)),
                (8, 'array filler lost resize', reference_expected + call_init_expected.replace('            ILE->resizeInits(SemaRef.Context, unsigned(NumElements));\n', '', 1)),
                (8, 'array filler stale count', reference_expected + call_init_expected.replace('            NumInits = ILE->getNumInits();\n', '', 1)),
                (8, 'array filler lost second pass', reference_expected + call_init_expected.replace('            RequiresSecondPass = true;\n', '', 1)),
                (8, 'array filler lost slot', reference_expected + call_init_expected.replace('          ILE->setInit(Init, Filler);\n', '', 1)),
                (8, 'array filler lost total extent', reference_expected + call_init_expected.replace('                  SemaRef.Context, Filler, NumElements - Init, NumElements);', '                  SemaRef.Context, Filler, NumElements - Init, NumElements - Init);', 1)),
                (8, 'array filler lost cleanup provenance', reference_expected + call_init_expected.replace('          SemaRef.getASTConsumer().HandleNeverCArrayFillerElement(Filler);\n', '', 1)),
                (8, 'array filler orphan decision', reference_expected + call_init_expected + '// NeverCFillerAction\n'),
                (6, 'selected call duplicate overload_header original', call_overload_header_original * 2),
                (6, 'selected call duplicate overload_header expected', call_overload_header_expected * 2),
                (6, 'selected call overload_header lost statement 0', call_overload_header_expected.replace('    SourceLocation NeverCTemplateDeductionLocation;', "// Missing selected call statement.", 1)),
                (6, 'selected call overload_header lost statement 1', call_overload_header_expected.replace('          InitializerListOfIncompleteArray(false),', "// Missing selected call statement.", 1)),
                (6, 'selected call overload_header lost statement 2', call_overload_header_expected.replace('          NeverCTemplateDeductionLocation() {', "// Missing selected call statement.", 1)),
                (6, 'selected call overload_header lost statement 3', call_overload_header_expected.replace('          InitializerListContainerType(Other.InitializerListContainerType),', "// Missing selected call statement.", 1)),
                (6, 'selected call overload_header lost statement 4', call_overload_header_expected.replace('          NeverCTemplateDeductionLocation(Other.NeverCTemplateDeductionLocation) {', "// Missing selected call statement.", 1)),
                (6, 'selected call overload_header lost statement 5', call_overload_header_expected.replace('      NeverCTemplateDeductionLocation = {};', "// Missing selected call statement.", 1)),
                (6, 'selected call orphan overload_header', call_overload_header_expected + "// HandleNeverCSelectedTemplateCallSource\n"),
                (7, 'selected call duplicate overload original', argument_reference_expected + call_overload_original * 2),
                (7, 'selected call duplicate overload expected', argument_reference_expected + call_overload_expected * 2),
                (7, 'selected call overload lost statement 0', argument_reference_expected + call_overload_expected.replace('#include "clang/AST/ASTConsumer.h"', "// Missing selected call statement.", 1)),
                (7, 'selected call overload lost statement 1', argument_reference_expected + call_overload_expected.replace('    if (S.getASTConsumer().wantsNeverCTemplateSource()) {', "// Missing selected call statement.", 1)),
                (7, 'selected call overload lost statement 2', argument_reference_expected + call_overload_expected.replace('      const auto *NeverCSelectedFunction = ICS.isUserDefined()', "// Missing selected call statement.", 1)),
                (7, 'selected call overload lost statement 3', argument_reference_expected + call_overload_expected.replace('          ? ICS.UserDefined.ConversionFunction : ICS.Standard.CopyConstructor;', "// Missing selected call statement.", 1)),
                (7, 'selected call overload lost statement 4', argument_reference_expected + call_overload_expected.replace('      if (NeverCSelectedFunction && NeverCSelectedFunction->getPrimaryTemplate())', "// Missing selected call statement.", 1)),
                (7, 'selected call overload lost statement 5', argument_reference_expected + call_overload_expected.replace('        ICS.NeverCTemplateDeductionLocation = Conversions.getLocation();', "// Missing selected call statement.", 1)),
                (7, 'selected call overload lost statement 6', argument_reference_expected + call_overload_expected.replace('    if (S.getASTConsumer().wantsNeverCTemplateSource() &&', "// Missing selected call statement.", 1)),
                (7, 'selected call overload lost statement 7', argument_reference_expected + call_overload_expected.replace('        Best->Function->getPrimaryTemplate())', "// Missing selected call statement.", 1)),
                (7, 'selected call overload lost statement 8', argument_reference_expected + call_overload_expected.replace('      ICS.NeverCTemplateDeductionLocation = CandidateSet.getLocation();', "// Missing selected call statement.", 1)),
                (7, 'selected call overload lost statement 9', argument_reference_expected + call_overload_expected.replace('  if (Conversion->getPrimaryTemplate() &&', "// Missing selected call statement.", 1)),
                (7, 'selected call overload lost statement 10', argument_reference_expected + call_overload_expected.replace('      SemaRef.getASTConsumer().wantsNeverCTemplateSource())', "// Missing selected call statement.", 1)),
                (7, 'selected call overload lost statement 11', argument_reference_expected + call_overload_expected.replace('    SemaRef.getASTConsumer().HandleNeverCSelectedTemplateCallSource(', "// Missing selected call statement.", 1)),
                (7, 'selected call overload lost statement 12', argument_reference_expected + call_overload_expected.replace('        Result.get(), Conversion, Loc);', "// Missing selected call statement.", 1)),
                (7, 'selected call orphan overload', argument_reference_expected + call_overload_expected + "// HandleNeverCSelectedTemplateCallSource\n"),
                (8, 'selected call duplicate init original', reference_expected + call_init_original * 2),
                (8, 'selected call duplicate init expected', reference_expected + call_init_expected * 2),
                (8, 'selected call init lost statement 0', reference_expected + call_init_expected.replace('#include "clang/AST/ASTConsumer.h"', "// Missing selected call statement.", 1)),
                (8, 'selected call init lost statement 1', reference_expected + call_init_expected.replace('  if (!CurInit.isInvalid() && Constructor->getPrimaryTemplate() &&', "// Missing selected call statement.", 1)),
                (8, 'selected call init lost statement 2', reference_expected + call_init_expected.replace('      S.getASTConsumer().wantsNeverCTemplateSource())', "// Missing selected call statement.", 1)),
                (8, 'selected call init lost statement 3', reference_expected + call_init_expected.replace('    S.getASTConsumer().HandleNeverCSelectedTemplateCallSource(', "// Missing selected call statement.", 1)),
                (8, 'selected call init lost statement 4', reference_expected + call_init_expected.replace('        CurInit.get(), Constructor, CandidateSet.getLocation());', "// Missing selected call statement.", 1)),
                (8, 'selected call init lost statement 5', reference_expected + call_init_expected.replace('  if (Constructor->getPrimaryTemplate() &&', "// Missing selected call statement.", 1)),
                (8, 'selected call init lost statement 6', reference_expected + call_init_expected.replace('        CurInit.get(), Constructor, Kind.getLocation());', "// Missing selected call statement.", 1)),
                (8, 'selected call init lost statement 7', reference_expected + call_init_expected.replace('      if (Fn->getPrimaryTemplate() &&', "// Missing selected call statement.", 1)),
                (8, 'selected call init lost statement 8', reference_expected + call_init_expected.replace('          S.getASTConsumer().wantsNeverCTemplateSource())', "// Missing selected call statement.", 1)),
                (8, 'selected call init lost statement 9', reference_expected + call_init_expected.replace('        S.getASTConsumer().HandleNeverCSelectedTemplateCallSource(', "// Missing selected call statement.", 1)),
                (8, 'selected call init lost statement 10', reference_expected + call_init_expected.replace('            CurInit.get(), Fn, FailedCandidateSet.getLocation());', "// Missing selected call statement.", 1)),
                (8, 'selected call orphan init', reference_expected + call_init_expected + "// HandleNeverCSelectedTemplateCallSource\n"),
                (9, 'selected call duplicate expr_cxx original', call_expr_cxx_original * 2),
                (9, 'selected call duplicate expr_cxx expected', call_expr_cxx_expected * 2),
                (9, 'selected call expr_cxx lost statement 0', call_expr_cxx_expected.replace('#include "clang/AST/ASTConsumer.h"', "// Missing selected call statement.", 1)),
                (9, 'selected call expr_cxx lost statement 1', call_expr_cxx_expected.replace('                                       Expr *From,', "// Missing selected call statement.", 1)),
                (9, 'selected call expr_cxx lost statement 2', call_expr_cxx_expected.replace('                                       SourceLocation NeverCTemplateLocation) {', "// Missing selected call statement.", 1)),
                (9, 'selected call expr_cxx lost statement 3', call_expr_cxx_expected.replace('    if (Constructor->getPrimaryTemplate() &&', "// Missing selected call statement.", 1)),
                (9, 'selected call expr_cxx lost statement 4', call_expr_cxx_expected.replace('        S.getASTConsumer().wantsNeverCTemplateSource())', "// Missing selected call statement.", 1)),
                (9, 'selected call expr_cxx lost statement 5', call_expr_cxx_expected.replace('      S.getASTConsumer().HandleNeverCSelectedTemplateCallSource(', "// Missing selected call statement.", 1)),
                (9, 'selected call expr_cxx lost statement 6', call_expr_cxx_expected.replace('          Result.get(), Constructor, NeverCTemplateLocation);', "// Missing selected call statement.", 1)),
                (9, 'selected call expr_cxx lost statement 7', call_expr_cxx_expected.replace('    if (Conv->getPrimaryTemplate() &&', "// Missing selected call statement.", 1)),
                (9, 'selected call expr_cxx lost statement 8', call_expr_cxx_expected.replace('          Result.get(), Conv, NeverCTemplateLocation);', "// Missing selected call statement.", 1)),
                (9, 'selected call expr_cxx lost statement 9', call_expr_cxx_expected.replace('    if (ICS.Standard.CopyConstructor &&', "// Missing selected call statement.", 1)),
                (9, 'selected call expr_cxx lost statement 10', call_expr_cxx_expected.replace('        ICS.Standard.CopyConstructor->getPrimaryTemplate() &&', "// Missing selected call statement.", 1)),
                (9, 'selected call expr_cxx lost statement 11', call_expr_cxx_expected.replace('        getASTConsumer().wantsNeverCTemplateSource())', "// Missing selected call statement.", 1)),
                (9, 'selected call expr_cxx lost statement 12', call_expr_cxx_expected.replace('      getASTConsumer().HandleNeverCSelectedTemplateCallSource(', "// Missing selected call statement.", 1)),
                (9, 'selected call expr_cxx lost statement 13', call_expr_cxx_expected.replace('          Res.get(), ICS.Standard.CopyConstructor,', "// Missing selected call statement.", 1)),
                (9, 'selected call expr_cxx lost statement 14', call_expr_cxx_expected.replace('          ICS.NeverCTemplateDeductionLocation);', "// Missing selected call statement.", 1)),
                (9, 'selected call expr_cxx lost statement 15', call_expr_cxx_expected.replace('          ICS.UserDefined.HadMultipleCandidates, From,', "// Missing selected call statement.", 1)),
                (9, 'selected call orphan expr_cxx', call_expr_cxx_expected + "// HandleNeverCSelectedTemplateCallSource\n"),
                (0, "selected call missing Expr forward", explicit_header_expected.replace("  class Expr;\n", "", 1)),
                (0, "selected call missing callback", explicit_header_expected.replace('\n  // Actual successful expression and original selection location, before wrappers.\n  virtual void HandleNeverCSelectedTemplateCallSource(\n      Expr *, FunctionDecl *, const SourceLocation &) {}', "", 1)),
                (5, "conversion missing anchor", explicit_instantiate_original.replace('    MultiLevelTemplateArgumentList TemplateArgs = getTemplateInstantiationArgs(\n        Function, DC, /*Final=*/false, Innermost, false, PatternDecl);\n\n    // Substitute into the qualifier; we can get a substitution failure here', "// Missing conversion source anchor.", 1)),
                (5, "conversion drifted substitution", explicit_instantiate_expected.replace("PatternDecl->getNameInfo(), TemplateArgs", "Function->getNameInfo(), TemplateArgs", 1)),
                (5, "conversion lost request guard", explicit_instantiate_expected.replace("Consumer.wantsNeverCTemplateSource() && ", "", 1)),
                (5, "conversion lost kind guard", explicit_instantiate_expected.replace(" && isa<CXXConversionDecl>(Function)", "", 1)),
                (5, "conversion lost name assignment", explicit_instantiate_expected.replace("Function->setDeclarationNameLoc(NeverCConversionName.getInfo());", "", 1)),
                (5, "conversion lost invalid handling", explicit_instantiate_expected.replace("Function->setInvalidDecl();", "", 1)),
                (5, "conversion duplicate file", explicit_instantiate_expected * 2),
                (5, "conversion orphan marker", explicit_instantiate_expected + "// NeverCConversionName\n"),
                (1, 'variable source missing anchor 0', explicit_source_original.replace('  // Find the variable template specialization declaration that\n  // corresponds to these arguments.\n  void *InsertPos = nullptr;\n  if (VarTemplateSpecializationDecl *Spec =\n          Template->findSpecialization(CTAI.CanonicalConverted, InsertPos)) {', "// Missing variable source anchor.", 1)),
                (1, 'variable source drifted block 0', explicit_source_expected.replace('  // Preserve every concrete use, including a fresh spelling of a cached id.\n  auto NeverCRetainVariable = [&](VarTemplateSpecializationDecl *Spec) {\n    if (Spec && !Spec->isInvalidDecl() && Consumer.wantsNeverCTemplateSource())\n      Consumer.HandleNeverCVariableTemplateSource(\n          Spec, TemplateArgs, nullptr, false, false,\n          CTAI.CanonicalConverted.data(), CTAI.SugaredConverted.data(),\n          CTAI.CanonicalConverted.size(), CTAI.NeverCDefaultParameters.data(),\n          CTAI.NeverCWrittenDefaults.data(), CTAI.NeverCConvertedDefaults.data(),\n          CTAI.NeverCDefaultParameters.size(), CTAI.NeverCTypeParameters.data(),\n          CTAI.NeverCParameterTypes.data(), CTAI.NeverCParameterPackIndices.data(),\n          CTAI.NeverCTypeParameters.size(), CTAI.NeverCDefaultsOverflow,\n          TemplateNameLoc);\n  };\n\n  // Find the variable template specialization declaration that\n  // corresponds to these arguments.\n  void *InsertPos = nullptr;\n  if (VarTemplateSpecializationDecl *Spec =\n          Template->findSpecialization(CTAI.CanonicalConverted, InsertPos)) {', '  // Preserve every concrete use, including a fresh spelling of a cached id.\n  auto NeverCRetainVariable = [&](VarTemplateSpecializationDecl *Spec) {\n    if (Spec && !Spec->isInvalidDecl() && Consumer.wantsNeverCTemplateSource())\n      Consumer.HandleNeverCVariableTemplateSource(\n          Spec, TemplateArgs, nullptr, false, false,\n          CTAI.CanonicalConverted.data(), CTAI.SugaredConverted.data(),\n          CTAI.CanonicalConverted.size(), CTAI.NeverCDefaultParameters.data(),\n          CTAI.NeverCWrittenDefaults.data(), CTAI.NeverCConv/* Variable source drift. */ertedDefaults.data(),\n          CTAI.NeverCDefaultParameters.size(), CTAI.NeverCTypeParameters.data(),\n          CTAI.NeverCParameterTypes.data(), CTAI.NeverCParameterPackIndices.data(),\n          CTAI.NeverCTypeParameters.size(), CTAI.NeverCDefaultsOverflow,\n          TemplateNameLoc);\n  };\n\n  // Find the variable template specialization declaration that\n  // corresponds to these arguments.\n  void *InsertPos = nullptr;\n  if (VarTemplateSpecializationDecl *Spec =\n          Template->findSpecialization(CTAI.CanonicalConverted, InsertPos)) {', 1)),
                (1, 'variable source missing anchor 1', explicit_source_original.replace('    // If we already have a variable template specialization, return it.\n    return Spec;', "// Missing variable source anchor.", 1)),
                (1, 'variable source drifted block 1', explicit_source_expected.replace('    // If we already have a variable template specialization, return it.\n    NeverCRetainVariable(Spec);\n    return Spec;', '    // If we already have a variable template specialization/* Variable source drift. */, return it.\n    NeverCRetainVariable(Spec);\n    return Spec;', 1)),
                (1, 'variable source missing anchor 2', explicit_source_original.replace('  assert(Decl && "No variable template specialization?");\n  return Decl;', "// Missing variable source anchor.", 1)),
                (1, 'variable source drifted block 2', explicit_source_expected.replace('  assert(Decl && "No variable template specialization?");\n  NeverCRetainVariable(Decl);\n  return Decl;', '  assert(Decl && "No variable template specializati/* Variable source drift. */on?");\n  NeverCRetainVariable(Decl);\n  return Decl;', 1)),
                (1, 'variable source missing anchor 3', explicit_source_original.replace('  return Specialization;\n}\n\nnamespace {\n/// A partial specialization whose template arguments have matched', "// Missing variable source anchor.", 1)),
                (1, 'variable source drifted block 3', explicit_source_expected.replace(
                    "Specialization, TemplateArgs, DI, true,",
                    "Specialization, TemplateArgs, nullptr, true,", 1)),
                (2, 'variable deduction missing anchor 0', explicit_deduction_original.replace('  if (S.SubstTemplateArguments(\n          PartialTemplArgInfo->arguments(),\n          MultiLevelTemplateArgumentList(Partial, CTAI.SugaredConverted,\n                                         /*Final=*/true),\n          InstArgs)) {', "// Missing variable source anchor.", 1)),
                (2, 'variable deduction drifted block 0', explicit_deduction_expected.replace('  if (S.SubstTemplateArguments(\n          PartialTemplArgInfo->arguments(),\n          MultiLevelTemplateArgumentList(Partial, CTAI.SugaredConverted,\n                                         /*Final=*/!(isa<ClassTemplatePartialSpecializationDecl,\n                                                         VarTemplatePartialSpecializationDecl>(Partial) &&\n                                                     !IsPartialOrdering &&\n                                                     S.getASTConsumer().wantsNeverCTemplateSource())),\n          InstArgs)) {', '  if (S.SubstTemplateArguments(\n          PartialTemplArgInfo->arguments(),\n          MultiLevelTemplateArgumentList(Partial, CTAI.SugaredConverted,\n                                         /*Final=*/!(isa<ClassTemplatePartialSpecializationDecl,\n                              /* Variable source drift. */                           VarTemplatePartialSpecializationDecl>(Partial) &&\n                                                     !IsPartialOrdering &&\n                                                     S.getASTConsumer().wantsNeverCTemplateSource())),\n          InstArgs)) {', 1)),
                (2, 'variable deduction missing anchor 1', explicit_deduction_original.replace('  if (!IsPartialOrdering) {\n    if (auto Result = CheckDeducedArgumentConstraints(\n            S, Partial, CTAI.SugaredConverted, CTAI.CanonicalConverted, Info);\n        Result != TemplateDeductionResult::Success)\n      return Result;\n  }\n\n  return TemplateDeductionResult::Success;\n}\n\n/// Complete template argument deduction for a class or variable template,', "// Missing variable source anchor.", 1)),
                (2, 'variable deduction drifted block 1', explicit_deduction_expected.replace('  if (!IsPartialOrdering) {\n    if (auto Result = CheckDeducedArgumentConstraints(\n            S, Partial, CTAI.SugaredConverted, CTAI.CanonicalConverted, Info);\n        Result != TemplateDeductionResult::Success)\n      return Result;\n  }\n\n  if (auto *NeverCPartial = dyn_cast<ClassTemplatePartialSpecializationDecl>(Partial);\n      NeverCPartial && !IsPartialOrdering && !Trap.hasErrorOccurred() &&\n      S.getASTConsumer().wantsNeverCTemplateSource()) {\n    auto NeverCRetainPartial = [&](bool Pattern,\n                                   const TemplateArgumentListInfo *Written,\n                                   const Sema::CheckTemplateArgumentInfo &Checked) {\n      S.getASTConsumer().HandleNeverCClassPartialSource(\n          NeverCPartial, CanonicalDeducedArgumentList, Pattern, Written,\n          Checked.CanonicalConverted.data(), Checked.SugaredConverted.data(),\n          Checked.CanonicalConverted.size(), Checked.NeverCDefaultParameters.data(),\n          Checked.NeverCWrittenDefaults.data(), Checked.NeverCConvertedDefaults.data(),\n          Checked.NeverCDefaultParameters.size(), Checked.NeverCTypeParameters.data(),\n          Checked.NeverCParameterTypes.data(), Checked.NeverCParameterPackIndices.data(),\n          Checked.NeverCTypeParameters.size(), Checked.NeverCDefaultsOverflow,\n          Info.getLocation());\n    };\n    NeverCRetainPartial(false, nullptr, CTAI);\n    NeverCRetainPartial(true, &InstArgs, InstCTAI);\n  }\n\n  if (auto *NeverCVariablePartial = dyn_cast<VarTemplatePartialSpecializationDecl>(Partial);\n      NeverCVariablePartial && !IsPartialOrdering && !Trap.hasErrorOccurred() &&\n      S.getASTConsumer().wantsNeverCTemplateSource()) {\n    auto NeverCRetainVariablePartial = [&](bool Pattern,\n                                           const TemplateArgumentListInfo *Written,\n                                           const Sema::CheckTemplateArgumentInfo &Checked) {\n      // Variable selection transfers takeSugared(), unlike class selection.\n      S.getASTConsumer().HandleNeverCVariablePartialSource(\n          NeverCVariablePartial, SugaredDeducedArgumentList, Pattern, Written,\n          Checked.CanonicalConverted.data(), Checked.SugaredConverted.data(),\n          Checked.CanonicalConverted.size(), Checked.NeverCDefaultParameters.data(),\n          Checked.NeverCWrittenDefaults.data(), Checked.NeverCConvertedDefaults.data(),\n          Checked.NeverCDefaultParameters.size(), Checked.NeverCTypeParameters.data(),\n          Checked.NeverCParameterTypes.data(), Checked.NeverCParameterPackIndices.data(),\n          Checked.NeverCTypeParameters.size(), Checked.NeverCDefaultsOverflow,\n          Info.getLocation());\n    };\n    NeverCRetainVariablePartial(false, nullptr, CTAI);\n    NeverCRetainVariablePartial(true, &InstArgs, InstCTAI);\n  }\n\n  return TemplateDeductionResult::Success;\n}\n\n/// Complete template argument deduction for a class or variable template,', '  if (!IsPartialOrdering) {\n    if (auto Result = CheckDeducedArgumentConstraints(\n            S, Partial, CTAI.SugaredConverted, CTAI.CanonicalConverted, Info);\n        Result != TemplateDeductionResult::Success)\n      return Result;\n  }\n\n  if (auto *NeverCPartial = dyn_cast<ClassTemplatePartialSpecializationDecl>(Partial);\n      NeverCPartial && !IsPartialOrdering && !Trap.hasErrorOccurred() &&\n      S.getASTConsumer().wantsNeverCTemplateSource()) {\n    auto NeverCRetainPartial = [&](bool Pattern,\n                                   const TemplateArgumentListInfo *Written,\n                                   const Sema::CheckTemplateArgumentInfo &Checked) {\n      S.getASTConsumer().HandleNeverCClassPartialSource(\n          NeverCPartial, CanonicalDeducedArgumentList, Pattern, Written,\n          Checked.CanonicalConverted.data(), Checked.SugaredConverted.data(),\n          Checked.CanonicalConverted.size(), Checked.NeverCDefaultParameters.data(),\n          Checked.NeverCWrittenDefaults.data(), Checked.NeverCConvertedDefaults.data(),\n          Checked.NeverCDefaultParameters.size(), Checked.NeverCTypeParameters.data(),\n          Checked.NeverCParameterTypes.data(), Checked.NeverCParameterPackIndices.data(),\n          Checked.NeverCTypeParameters.size(), Checked.NeverCDefaultsOverflow,\n          Info.getLocation());\n    };\n    NeverCRetainPartial(false, nullptr, CTAI);\n    NeverCRetainPartial(true, &InstArgs, InstCTAI);\n  }\n\n  if (auto/* Variable source drift. */ *NeverCVariablePartial = dyn_cast<VarTemplatePartialSpecializationDecl>(Partial);\n      NeverCVariablePartial && !IsPartialOrdering && !Trap.hasErrorOccurred() &&\n      S.getASTConsumer().wantsNeverCTemplateSource()) {\n    auto NeverCRetainVariablePartial = [&](bool Pattern,\n                                           const TemplateArgumentListInfo *Written,\n                                           const Sema::CheckTemplateArgumentInfo &Checked) {\n      // Variable selection transfers takeSugared(), unlike class selection.\n      S.getASTConsumer().HandleNeverCVariablePartialSource(\n          NeverCVariablePartial, SugaredDeducedArgumentList, Pattern, Written,\n          Checked.CanonicalConverted.data(), Checked.SugaredConverted.data(),\n          Checked.CanonicalConverted.size(), Checked.NeverCDefaultParameters.data(),\n          Checked.NeverCWrittenDefaults.data(), Checked.NeverCConvertedDefaults.data(),\n          Checked.NeverCDefaultParameters.size(), Checked.NeverCTypeParameters.data(),\n          Checked.NeverCParameterTypes.data(), Checked.NeverCParameterPackIndices.data(),\n          Checked.NeverCTypeParameters.size(), Checked.NeverCDefaultsOverflow,\n          Info.getLocation());\n    };\n    NeverCRetainVariablePartial(false, nullptr, CTAI);\n    NeverCRetainVariablePartial(true, &InstArgs, InstCTAI);\n  }\n\n  return TemplateDeductionResult::Success;\n}\n\n/// Complete template argument deduction for a class or variable template,', 1)),
                (5, 'variable instantiate missing anchor 0', explicit_instantiate_original.replace('  SemaRef.BuildVariableInstantiation(Var, D, TemplateArgs, LateAttrs, Owner,\n                                     StartingScope, false, PrevDecl);\n\n  return Var;', "// Missing variable source anchor.", 1)),
                (5, 'variable instantiate drifted block 0', explicit_instantiate_expected.replace('  SemaRef.BuildVariableInstantiation(Var, D, TemplateArgs, LateAttrs, Owner,\n                                     StartingScope, false, PrevDecl);\n\n  if (!Var->isInvalidDecl() && SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n    SemaRef.getASTConsumer().HandleNeverCVariableTypeSource(\n        Var, PrevDecl, D, DI, false, Var->getLocation());\n\n  return Var;', '  SemaRef.BuildVariableInstantiation(Var, D, TemplateArgs, LateAttrs, Owner,\n                                     StartingScope, false, PrevDecl);\n\n  if (!Var->isInvalidDecl() &&/* Variable source drift. */ SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n    SemaRef.getASTConsumer().HandleNeverCVariableTypeSource(\n        Var, PrevDecl, D, DI, false, Var->getLocation());\n\n  return Var;', 1)),
                (5, 'variable instantiate missing anchor 1', explicit_instantiate_original.replace('  // Instantiate the initializer.\n  InstantiateVariableInitializer(VarSpec, PatternDecl, TemplateArgs);\n\n  if (getLangOpts().OpenCL)\n    deduceOpenCLAddressSpace(VarSpec);\n\n  return VarSpec;', "// Missing variable source anchor.", 1)),
                (5, 'variable instantiate drifted block 1', explicit_instantiate_expected.replace('  // Instantiate the initializer.\n  InstantiateVariableInitializer(VarSpec, PatternDecl, TemplateArgs);\n\n  if (getLangOpts().OpenCL)\n    deduceOpenCLAddressSpace(VarSpec);\n\n  if (!VarSpec->isInvalidDecl() && Consumer.wantsNeverCTemplateSource())\n    Consumer.HandleNeverCVariableTypeSource(\n        VarSpec, nullptr, PatternDecl, DI, true, VarSpec->getLocation());\n\n  return VarSpec;', '  // Instantiate the initializer.\n  InstantiateVariableInitializer(VarSpec, PatternDecl, TemplateArgs);\n\n  if (getLangOpts().OpenCL)\n    deduceOpenCLAddressSpace(VarSpec);\n\n  if (!VarSpec/* Variable source drift. */->isInvalidDecl() && Consumer.wantsNeverCTemplateSource())\n    Consumer.HandleNeverCVariableTypeSource(\n        VarSpec, nullptr, PatternDecl, DI, true, VarSpec->getLocation());\n\n  return VarSpec;', 1)),
                (0, "variable missing forward declarations", explicit_header_expected.replace('  class VarTemplateSpecializationDecl;\n  class VarTemplatePartialSpecializationDecl;', "", 1)),
                (0, "variable missing callbacks", explicit_header_expected.replace('\n  virtual void HandleNeverCVariableTemplateSource(\n      VarTemplateSpecializationDecl *, const TemplateArgumentListInfo &,\n      TypeSourceInfo *, bool, bool,\n      const TemplateArgument *, const TemplateArgument *, unsigned,\n      NamedDecl *const *, const TemplateArgumentLoc *, const TemplateArgumentLoc *,\n      unsigned, NamedDecl *const *, TypeSourceInfo *const *,\n      const unsigned *, unsigned, bool, const SourceLocation &) {}\n  virtual void HandleNeverCVariablePartialSource(\n      VarTemplatePartialSpecializationDecl *, const TemplateArgumentList *,\n      bool, const TemplateArgumentListInfo *,\n      const TemplateArgument *, const TemplateArgument *, unsigned,\n      NamedDecl *const *, const TemplateArgumentLoc *, const TemplateArgumentLoc *,\n      unsigned, NamedDecl *const *, TypeSourceInfo *const *,\n      const unsigned *, unsigned, bool, const SourceLocation &) {}\n  // Successful declaration checks are independent of later partial selection.\n  virtual void HandleNeverCPartialDeclarationSource(\n      NamedDecl *, NamedDecl *, const TemplateArgumentListInfo &,\n      const TemplateArgument *, const TemplateArgument *, unsigned,\n      NamedDecl *const *, const TemplateArgumentLoc *, const TemplateArgumentLoc *,\n      unsigned, NamedDecl *const *, TypeSourceInfo *const *,\n      const unsigned *, unsigned, bool, const SourceLocation &) {}\n  // Class-scope full copies retain the actual successful declaration check.\n  virtual void HandleNeverCClassFullDeclarationSource(\n      ClassTemplateSpecializationDecl *, ClassTemplateSpecializationDecl *,\n      const TemplateArgumentListInfo &,\n      const TemplateArgument *, const TemplateArgument *, unsigned,\n      NamedDecl *const *, const TemplateArgumentLoc *, const TemplateArgumentLoc *,\n      unsigned, NamedDecl *const *, TypeSourceInfo *const *,\n      const unsigned *, unsigned, bool, const SourceLocation &) {}\n  // Ordinary member-class directives otherwise have no separate AST node.\n  virtual void HandleNeverCExplicitMemberClassInstantiation(\n      CXXRecordDecl *, CXXRecordDecl *, const NestedNameSpecifierLoc &,\n      const SourceLocation &, const SourceLocation &, const SourceLocation &,\n      bool) {}\n  // Keep friend spelling separate from the selected signature/body source.\n  virtual void HandleNeverCFriendFunctionSource(\n      FunctionDecl *, FunctionDecl *, FunctionDecl *, CXXRecordDecl *) {}\n  virtual void HandleNeverCFriendDeclarationSource(FriendDecl *, FriendDecl *) {}\n  // Copying an outer class creates a primary, not an inner specialization.\n  virtual void HandleNeverCFriendFunctionTemplateSource(\n      FunctionTemplateDecl *, FunctionDecl *, FunctionDecl *, CXXRecordDecl *) {}\n  // Exact compatible definition context selected by existing Sema control flow.\n  virtual void HandleNeverCFunctionTemplateBodySource(\n      FunctionDecl *, FunctionTemplateDecl *, const FunctionDecl *, DeclContext *) {}\n  // Successful class-friend lookup and redeclaration merge, before return.\n  virtual void HandleNeverCFriendClassTemplateSource(\n      ClassTemplateDecl *, ClassTemplateDecl *, CXXRecordDecl *,\n      DeclContext *, ClassTemplateDecl *) {}\n  virtual void HandleNeverCVariableTypeSource(\n      VarTemplateSpecializationDecl *, VarTemplateSpecializationDecl *,\n      VarDecl *, TypeSourceInfo *, bool,\n      const SourceLocation &) {}', "", 1)),
                (5, "variable duplicate type file", explicit_instantiate_expected * 2),
                (0, 'orphan HandleNeverCVariableTemplateSource', explicit_header_expected + '// HandleNeverCVariableTemplateSource\n'),
                (1, 'orphan NeverCRetainVariable', explicit_source_expected + '// NeverCRetainVariable\n'),
                (2, 'orphan NeverCRetainVariablePartial', explicit_deduction_expected + '// NeverCRetainVariablePartial\n'),
                (1, 'ordinary member directive lost origin', explicit_source_expected.replace(
                    'Record, Pattern, SS.getWithLocInContext(Context),', 'Record, nullptr, SS.getWithLocInContext(Context),', 1)),
                (1, 'ordinary member directive lost qualifier', explicit_source_expected.replace(
                    'Record, Pattern, SS.getWithLocInContext(Context),', 'Record, Pattern, NestedNameSpecifierLoc(),', 1)),
                (1, 'ordinary member directive lost name location', explicit_source_expected.replace(
                    'NameLoc, TemplateLoc, ExternLoc, !Attr.empty());', 'TemplateLoc, TemplateLoc, ExternLoc, !Attr.empty());', 1)),
                (1, 'ordinary member directive lost attributes', explicit_source_expected.replace(
                    'NameLoc, TemplateLoc, ExternLoc, !Attr.empty());', 'NameLoc, TemplateLoc, ExternLoc, false);', 1)),
                (5, 'friend lost incoming source', explicit_instantiate_expected.replace(
                    'FunctionDecl *NeverCIncomingFriendFunction = D;', 'FunctionDecl *NeverCIncomingFriendFunction = nullptr;', 1)),
                (5, 'friend lost selected source', explicit_instantiate_expected.replace(
                    'Function, NeverCIncomingFriendFunction, D, NeverCGrantingClass);', 'Function, NeverCIncomingFriendFunction, NeverCIncomingFriendFunction, NeverCGrantingClass);', 1)),
                (5, 'friend lost actual target', explicit_instantiate_expected.replace(
                    'Function, NeverCIncomingFriendFunction, D, NeverCGrantingClass);', 'D, NeverCIncomingFriendFunction, D, NeverCGrantingClass);', 1)),
                (5, 'friend lost granting owner', explicit_instantiate_expected.replace(
                    'Function, NeverCIncomingFriendFunction, D, NeverCGrantingClass);', 'Function, NeverCIncomingFriendFunction, D, nullptr);', 1)),
                (5, 'friend declaration lost written source', explicit_instantiate_expected.replace(
                    'HandleNeverCFriendDeclarationSource(FD, D);', 'HandleNeverCFriendDeclarationSource(FD, FD);', 1)),
                (5, 'friend declaration lost actual target', explicit_instantiate_expected.replace(
                    'HandleNeverCFriendDeclarationSource(FD, D);', 'HandleNeverCFriendDeclarationSource(D, D);', 1)),
                (5, 'friend lost template boundary', explicit_instantiate_expected.replace(
                    'isFriend && !FunctionTemplate && !TemplateParams &&', 'isFriend &&', 1)),
                (5, 'friend type lost written', explicit_instantiate_expected.replace(
                    '    // NeverC retains only an ordinary successful friend type substitution.\n    if (!D->isInvalidDecl() && !FD->isInvalidDecl() &&\n        !D->isUnsupportedFriend() && !D->isPackExpansion() &&\n        !D->getFriendTypeNumTemplateParameterLists() &&\n        SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n      SemaRef.getASTConsumer().HandleNeverCFriendDeclarationSource(FD, D);\n', '    // NeverC retains only an ordinary successful friend type substitution.\n    if (!D->isInvalidDecl() && !FD->isInvalidDecl() &&\n        !D->isUnsupportedFriend() && !D->isPackExpansion() &&\n        !D->getFriendTypeNumTemplateParameterLists() &&\n        SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n      SemaRef.getASTConsumer().HandleNeverCFriendDeclarationSource(FD, FD);\n', 1)),
                (5, 'friend type lost actual', explicit_instantiate_expected.replace(
                    '    // NeverC retains only an ordinary successful friend type substitution.\n    if (!D->isInvalidDecl() && !FD->isInvalidDecl() &&\n        !D->isUnsupportedFriend() && !D->isPackExpansion() &&\n        !D->getFriendTypeNumTemplateParameterLists() &&\n        SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n      SemaRef.getASTConsumer().HandleNeverCFriendDeclarationSource(FD, D);\n', '    // NeverC retains only an ordinary successful friend type substitution.\n    if (!D->isInvalidDecl() && !FD->isInvalidDecl() &&\n        !D->isUnsupportedFriend() && !D->isPackExpansion() &&\n        !D->getFriendTypeNumTemplateParameterLists() &&\n        SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n      SemaRef.getASTConsumer().HandleNeverCFriendDeclarationSource(D, D);\n', 1)),
                (5, 'friend type lost unsupported guard', explicit_instantiate_expected.replace(
                    '    // NeverC retains only an ordinary successful friend type substitution.\n    if (!D->isInvalidDecl() && !FD->isInvalidDecl() &&\n        !D->isUnsupportedFriend() && !D->isPackExpansion() &&\n        !D->getFriendTypeNumTemplateParameterLists() &&\n        SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n      SemaRef.getASTConsumer().HandleNeverCFriendDeclarationSource(FD, D);\n', '    // NeverC retains only an ordinary successful friend type substitution.\n    if (!D->isInvalidDecl() && !FD->isInvalidDecl() &&\n        !D->isPackExpansion() &&\n        !D->getFriendTypeNumTemplateParameterLists() &&\n        SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n      SemaRef.getASTConsumer().HandleNeverCFriendDeclarationSource(FD, D);\n', 1)),
                (5, 'friend type lost pack guard', explicit_instantiate_expected.replace(
                    '    // NeverC retains only an ordinary successful friend type substitution.\n    if (!D->isInvalidDecl() && !FD->isInvalidDecl() &&\n        !D->isUnsupportedFriend() && !D->isPackExpansion() &&\n        !D->getFriendTypeNumTemplateParameterLists() &&\n        SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n      SemaRef.getASTConsumer().HandleNeverCFriendDeclarationSource(FD, D);\n', '    // NeverC retains only an ordinary successful friend type substitution.\n    if (!D->isInvalidDecl() && !FD->isInvalidDecl() &&\n        !D->isUnsupportedFriend() && true &&\n        !D->getFriendTypeNumTemplateParameterLists() &&\n        SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n      SemaRef.getASTConsumer().HandleNeverCFriendDeclarationSource(FD, D);\n', 1)),
                (5, 'friend type lost header guard', explicit_instantiate_expected.replace(
                    '    // NeverC retains only an ordinary successful friend type substitution.\n    if (!D->isInvalidDecl() && !FD->isInvalidDecl() &&\n        !D->isUnsupportedFriend() && !D->isPackExpansion() &&\n        !D->getFriendTypeNumTemplateParameterLists() &&\n        SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n      SemaRef.getASTConsumer().HandleNeverCFriendDeclarationSource(FD, D);\n', '    // NeverC retains only an ordinary successful friend type substitution.\n    if (!D->isInvalidDecl() && !FD->isInvalidDecl() &&\n        !D->isUnsupportedFriend() && !D->isPackExpansion() &&\n        true &&\n        SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n      SemaRef.getASTConsumer().HandleNeverCFriendDeclarationSource(FD, D);\n', 1)),
                (5, 'friend template lost actual primary', explicit_instantiate_expected.replace(
                    '  if (isFriend && TemplateParams && FunctionTemplate &&\n      Function->getDescribedFunctionTemplate() == FunctionTemplate &&\n      NeverCIncomingFriendFunction->getDescribedFunctionTemplate() &&\n      !Function->isInvalidDecl() && !D->isInvalidDecl() &&\n      !NeverCIncomingFriendFunction->isInvalidDecl() &&\n      SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n    if (auto *NeverCGrantingClass = dyn_cast<CXXRecordDecl>(Owner))\n      SemaRef.getASTConsumer().HandleNeverCFriendFunctionTemplateSource(\n          FunctionTemplate, NeverCIncomingFriendFunction, D, NeverCGrantingClass);\n', '  if (isFriend && TemplateParams && FunctionTemplate &&\n      Function->getDescribedFunctionTemplate() == FunctionTemplate &&\n      NeverCIncomingFriendFunction->getDescribedFunctionTemplate() &&\n      !Function->isInvalidDecl() && !D->isInvalidDecl() &&\n      !NeverCIncomingFriendFunction->isInvalidDecl() &&\n      SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n    if (auto *NeverCGrantingClass = dyn_cast<CXXRecordDecl>(Owner))\n      SemaRef.getASTConsumer().HandleNeverCFriendFunctionTemplateSource(\n          nullptr, NeverCIncomingFriendFunction, D, NeverCGrantingClass);\n', 1)),
                (5, 'friend template lost incoming', explicit_instantiate_expected.replace(
                    '  if (isFriend && TemplateParams && FunctionTemplate &&\n      Function->getDescribedFunctionTemplate() == FunctionTemplate &&\n      NeverCIncomingFriendFunction->getDescribedFunctionTemplate() &&\n      !Function->isInvalidDecl() && !D->isInvalidDecl() &&\n      !NeverCIncomingFriendFunction->isInvalidDecl() &&\n      SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n    if (auto *NeverCGrantingClass = dyn_cast<CXXRecordDecl>(Owner))\n      SemaRef.getASTConsumer().HandleNeverCFriendFunctionTemplateSource(\n          FunctionTemplate, NeverCIncomingFriendFunction, D, NeverCGrantingClass);\n', '  if (isFriend && TemplateParams && FunctionTemplate &&\n      Function->getDescribedFunctionTemplate() == FunctionTemplate &&\n      NeverCIncomingFriendFunction->getDescribedFunctionTemplate() &&\n      !Function->isInvalidDecl() && !D->isInvalidDecl() &&\n      !NeverCIncomingFriendFunction->isInvalidDecl() &&\n      SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n    if (auto *NeverCGrantingClass = dyn_cast<CXXRecordDecl>(Owner))\n      SemaRef.getASTConsumer().HandleNeverCFriendFunctionTemplateSource(\n          FunctionTemplate, D, D, NeverCGrantingClass);\n', 1)),
                (5, 'friend template lost selected', explicit_instantiate_expected.replace(
                    '  if (isFriend && TemplateParams && FunctionTemplate &&\n      Function->getDescribedFunctionTemplate() == FunctionTemplate &&\n      NeverCIncomingFriendFunction->getDescribedFunctionTemplate() &&\n      !Function->isInvalidDecl() && !D->isInvalidDecl() &&\n      !NeverCIncomingFriendFunction->isInvalidDecl() &&\n      SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n    if (auto *NeverCGrantingClass = dyn_cast<CXXRecordDecl>(Owner))\n      SemaRef.getASTConsumer().HandleNeverCFriendFunctionTemplateSource(\n          FunctionTemplate, NeverCIncomingFriendFunction, D, NeverCGrantingClass);\n', '  if (isFriend && TemplateParams && FunctionTemplate &&\n      Function->getDescribedFunctionTemplate() == FunctionTemplate &&\n      NeverCIncomingFriendFunction->getDescribedFunctionTemplate() &&\n      !Function->isInvalidDecl() && !D->isInvalidDecl() &&\n      !NeverCIncomingFriendFunction->isInvalidDecl() &&\n      SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n    if (auto *NeverCGrantingClass = dyn_cast<CXXRecordDecl>(Owner))\n      SemaRef.getASTConsumer().HandleNeverCFriendFunctionTemplateSource(\n          FunctionTemplate, NeverCIncomingFriendFunction, NeverCIncomingFriendFunction, NeverCGrantingClass);\n', 1)),
                (5, 'friend template lost granting class', explicit_instantiate_expected.replace(
                    '  if (isFriend && TemplateParams && FunctionTemplate &&\n      Function->getDescribedFunctionTemplate() == FunctionTemplate &&\n      NeverCIncomingFriendFunction->getDescribedFunctionTemplate() &&\n      !Function->isInvalidDecl() && !D->isInvalidDecl() &&\n      !NeverCIncomingFriendFunction->isInvalidDecl() &&\n      SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n    if (auto *NeverCGrantingClass = dyn_cast<CXXRecordDecl>(Owner))\n      SemaRef.getASTConsumer().HandleNeverCFriendFunctionTemplateSource(\n          FunctionTemplate, NeverCIncomingFriendFunction, D, NeverCGrantingClass);\n', '  if (isFriend && TemplateParams && FunctionTemplate &&\n      Function->getDescribedFunctionTemplate() == FunctionTemplate &&\n      NeverCIncomingFriendFunction->getDescribedFunctionTemplate() &&\n      !Function->isInvalidDecl() && !D->isInvalidDecl() &&\n      !NeverCIncomingFriendFunction->isInvalidDecl() &&\n      SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n    if (auto *NeverCGrantingClass = dyn_cast<CXXRecordDecl>(Owner))\n      SemaRef.getASTConsumer().HandleNeverCFriendFunctionTemplateSource(\n          FunctionTemplate, NeverCIncomingFriendFunction, D, nullptr);\n', 1)),
                (5, 'friend template lost creation boundary', explicit_instantiate_expected.replace(
                    '  if (isFriend && TemplateParams && FunctionTemplate &&\n      Function->getDescribedFunctionTemplate() == FunctionTemplate &&\n      NeverCIncomingFriendFunction->getDescribedFunctionTemplate() &&\n      !Function->isInvalidDecl() && !D->isInvalidDecl() &&\n      !NeverCIncomingFriendFunction->isInvalidDecl() &&\n      SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n    if (auto *NeverCGrantingClass = dyn_cast<CXXRecordDecl>(Owner))\n      SemaRef.getASTConsumer().HandleNeverCFriendFunctionTemplateSource(\n          FunctionTemplate, NeverCIncomingFriendFunction, D, NeverCGrantingClass);\n', '  if (isFriend && FunctionTemplate &&\n      Function->getDescribedFunctionTemplate() == FunctionTemplate &&\n      NeverCIncomingFriendFunction->getDescribedFunctionTemplate() &&\n      !Function->isInvalidDecl() && !D->isInvalidDecl() &&\n      !NeverCIncomingFriendFunction->isInvalidDecl() &&\n      SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n    if (auto *NeverCGrantingClass = dyn_cast<CXXRecordDecl>(Owner))\n      SemaRef.getASTConsumer().HandleNeverCFriendFunctionTemplateSource(\n          FunctionTemplate, NeverCIncomingFriendFunction, D, NeverCGrantingClass);\n', 1)),
                (5, 'friend template lost actual kind', explicit_instantiate_expected.replace(
                    '  if (isFriend && TemplateParams && FunctionTemplate &&\n      Function->getDescribedFunctionTemplate() == FunctionTemplate &&\n      NeverCIncomingFriendFunction->getDescribedFunctionTemplate() &&\n      !Function->isInvalidDecl() && !D->isInvalidDecl() &&\n      !NeverCIncomingFriendFunction->isInvalidDecl() &&\n      SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n    if (auto *NeverCGrantingClass = dyn_cast<CXXRecordDecl>(Owner))\n      SemaRef.getASTConsumer().HandleNeverCFriendFunctionTemplateSource(\n          FunctionTemplate, NeverCIncomingFriendFunction, D, NeverCGrantingClass);\n', '  if (isFriend && TemplateParams && FunctionTemplate &&\n      true &&\n      NeverCIncomingFriendFunction->getDescribedFunctionTemplate() &&\n      !Function->isInvalidDecl() && !D->isInvalidDecl() &&\n      !NeverCIncomingFriendFunction->isInvalidDecl() &&\n      SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n    if (auto *NeverCGrantingClass = dyn_cast<CXXRecordDecl>(Owner))\n      SemaRef.getASTConsumer().HandleNeverCFriendFunctionTemplateSource(\n          FunctionTemplate, NeverCIncomingFriendFunction, D, NeverCGrantingClass);\n', 1)),
                (5, 'friend template body lost compatible declaration', explicit_instantiate_expected.replace(
                    '      assert(It != Primary->redecls().end() &&\n             "Should\'t get here without a definition");\n      NeverCCompatibleFunctionTemplate = cast<FunctionTemplateDecl>(*It);\n      if (FunctionDecl *Def = cast<FunctionTemplateDecl>(*It)', '      assert(It != Primary->redecls().end() &&\n             "Should\'t get here without a definition");\n      NeverCCompatibleFunctionTemplate = Primary;\n      if (FunctionDecl *Def = cast<FunctionTemplateDecl>(*It)', 1)),
                (5, 'friend template body lost pattern', explicit_instantiate_expected.replace(
                    '    // Preserve the selected declaration and DC; do not repeat the search.\n    if (NeverCCompatibleFunctionTemplate && Function->getKind() == Decl::Function &&\n        Function->doesThisDeclarationHaveABody() && !Function->isInvalidDecl() &&\n        !PatternDecl->isInvalidDecl() && Consumer.wantsNeverCTemplateSource())\n      Consumer.HandleNeverCFunctionTemplateBodySource(\n          Function, NeverCCompatibleFunctionTemplate, PatternDecl, DC);\n\n', '    // Preserve the selected declaration and DC; do not repeat the search.\n    if (NeverCCompatibleFunctionTemplate && Function->getKind() == Decl::Function &&\n        Function->doesThisDeclarationHaveABody() && !Function->isInvalidDecl() &&\n        !PatternDecl->isInvalidDecl() && Consumer.wantsNeverCTemplateSource())\n      Consumer.HandleNeverCFunctionTemplateBodySource(\n          Function, NeverCCompatibleFunctionTemplate, Function, DC);\n\n', 1)),
                (5, 'friend template body lost lexical context', explicit_instantiate_expected.replace(
                    '    // Preserve the selected declaration and DC; do not repeat the search.\n    if (NeverCCompatibleFunctionTemplate && Function->getKind() == Decl::Function &&\n        Function->doesThisDeclarationHaveABody() && !Function->isInvalidDecl() &&\n        !PatternDecl->isInvalidDecl() && Consumer.wantsNeverCTemplateSource())\n      Consumer.HandleNeverCFunctionTemplateBodySource(\n          Function, NeverCCompatibleFunctionTemplate, PatternDecl, DC);\n\n', '    // Preserve the selected declaration and DC; do not repeat the search.\n    if (NeverCCompatibleFunctionTemplate && Function->getKind() == Decl::Function &&\n        Function->doesThisDeclarationHaveABody() && !Function->isInvalidDecl() &&\n        !PatternDecl->isInvalidDecl() && Consumer.wantsNeverCTemplateSource())\n      Consumer.HandleNeverCFunctionTemplateBodySource(\n          Function, NeverCCompatibleFunctionTemplate, PatternDecl, Function->getLexicalDeclContext());\n\n', 1)),
                (5, 'friend template body lost successful body guard', explicit_instantiate_expected.replace(
                    '    // Preserve the selected declaration and DC; do not repeat the search.\n    if (NeverCCompatibleFunctionTemplate && Function->getKind() == Decl::Function &&\n        Function->doesThisDeclarationHaveABody() && !Function->isInvalidDecl() &&\n        !PatternDecl->isInvalidDecl() && Consumer.wantsNeverCTemplateSource())\n      Consumer.HandleNeverCFunctionTemplateBodySource(\n          Function, NeverCCompatibleFunctionTemplate, PatternDecl, DC);\n\n', '    // Preserve the selected declaration and DC; do not repeat the search.\n    if (NeverCCompatibleFunctionTemplate && Function->getKind() == Decl::Function &&\n        !Function->isInvalidDecl() &&\n        !PatternDecl->isInvalidDecl() && Consumer.wantsNeverCTemplateSource())\n      Consumer.HandleNeverCFunctionTemplateBodySource(\n          Function, NeverCCompatibleFunctionTemplate, PatternDecl, DC);\n\n', 1)),
                (5, 'friend class lost actual target', explicit_instantiate_expected.replace(
                    '    if (!Inst->isInvalidDecl() && !RecordInst->isInvalidDecl() &&\n        !D->isInvalidDecl() && !Pattern->isInvalidDecl() &&\n        Inst->getTemplatedDecl() == RecordInst &&\n        SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n      if (auto *NeverCGrantingClass = dyn_cast<CXXRecordDecl>(Owner))\n        SemaRef.getASTConsumer().HandleNeverCFriendClassTemplateSource(\n            Inst, D, NeverCGrantingClass, DC, PrevClassTemplate);\n', '    if (!Inst->isInvalidDecl() && !RecordInst->isInvalidDecl() &&\n        !D->isInvalidDecl() && !Pattern->isInvalidDecl() &&\n        Inst->getTemplatedDecl() == RecordInst &&\n        SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n      if (auto *NeverCGrantingClass = dyn_cast<CXXRecordDecl>(Owner))\n        SemaRef.getASTConsumer().HandleNeverCFriendClassTemplateSource(\n            D, D, NeverCGrantingClass, DC, PrevClassTemplate);\n', 1)),
                (5, 'friend class lost original', explicit_instantiate_expected.replace(
                    '    if (!Inst->isInvalidDecl() && !RecordInst->isInvalidDecl() &&\n        !D->isInvalidDecl() && !Pattern->isInvalidDecl() &&\n        Inst->getTemplatedDecl() == RecordInst &&\n        SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n      if (auto *NeverCGrantingClass = dyn_cast<CXXRecordDecl>(Owner))\n        SemaRef.getASTConsumer().HandleNeverCFriendClassTemplateSource(\n            Inst, D, NeverCGrantingClass, DC, PrevClassTemplate);\n', '    if (!Inst->isInvalidDecl() && !RecordInst->isInvalidDecl() &&\n        !D->isInvalidDecl() && !Pattern->isInvalidDecl() &&\n        Inst->getTemplatedDecl() == RecordInst &&\n        SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n      if (auto *NeverCGrantingClass = dyn_cast<CXXRecordDecl>(Owner))\n        SemaRef.getASTConsumer().HandleNeverCFriendClassTemplateSource(\n            Inst, Inst, NeverCGrantingClass, DC, PrevClassTemplate);\n', 1)),
                (5, 'friend class lost granting record', explicit_instantiate_expected.replace(
                    '    if (!Inst->isInvalidDecl() && !RecordInst->isInvalidDecl() &&\n        !D->isInvalidDecl() && !Pattern->isInvalidDecl() &&\n        Inst->getTemplatedDecl() == RecordInst &&\n        SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n      if (auto *NeverCGrantingClass = dyn_cast<CXXRecordDecl>(Owner))\n        SemaRef.getASTConsumer().HandleNeverCFriendClassTemplateSource(\n            Inst, D, NeverCGrantingClass, DC, PrevClassTemplate);\n', '    if (!Inst->isInvalidDecl() && !RecordInst->isInvalidDecl() &&\n        !D->isInvalidDecl() && !Pattern->isInvalidDecl() &&\n        Inst->getTemplatedDecl() == RecordInst &&\n        SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n      if (auto *NeverCGrantingClass = dyn_cast<CXXRecordDecl>(Owner))\n        SemaRef.getASTConsumer().HandleNeverCFriendClassTemplateSource(\n            Inst, D, nullptr, DC, PrevClassTemplate);\n', 1)),
                (5, 'friend class lost lookup context', explicit_instantiate_expected.replace(
                    '    if (!Inst->isInvalidDecl() && !RecordInst->isInvalidDecl() &&\n        !D->isInvalidDecl() && !Pattern->isInvalidDecl() &&\n        Inst->getTemplatedDecl() == RecordInst &&\n        SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n      if (auto *NeverCGrantingClass = dyn_cast<CXXRecordDecl>(Owner))\n        SemaRef.getASTConsumer().HandleNeverCFriendClassTemplateSource(\n            Inst, D, NeverCGrantingClass, DC, PrevClassTemplate);\n', '    if (!Inst->isInvalidDecl() && !RecordInst->isInvalidDecl() &&\n        !D->isInvalidDecl() && !Pattern->isInvalidDecl() &&\n        Inst->getTemplatedDecl() == RecordInst &&\n        SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n      if (auto *NeverCGrantingClass = dyn_cast<CXXRecordDecl>(Owner))\n        SemaRef.getASTConsumer().HandleNeverCFriendClassTemplateSource(\n            Inst, D, NeverCGrantingClass, Owner, PrevClassTemplate);\n', 1)),
                (5, 'friend class lost selected previous', explicit_instantiate_expected.replace(
                    '    if (!Inst->isInvalidDecl() && !RecordInst->isInvalidDecl() &&\n        !D->isInvalidDecl() && !Pattern->isInvalidDecl() &&\n        Inst->getTemplatedDecl() == RecordInst &&\n        SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n      if (auto *NeverCGrantingClass = dyn_cast<CXXRecordDecl>(Owner))\n        SemaRef.getASTConsumer().HandleNeverCFriendClassTemplateSource(\n            Inst, D, NeverCGrantingClass, DC, PrevClassTemplate);\n', '    if (!Inst->isInvalidDecl() && !RecordInst->isInvalidDecl() &&\n        !D->isInvalidDecl() && !Pattern->isInvalidDecl() &&\n        Inst->getTemplatedDecl() == RecordInst &&\n        SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n      if (auto *NeverCGrantingClass = dyn_cast<CXXRecordDecl>(Owner))\n        SemaRef.getASTConsumer().HandleNeverCFriendClassTemplateSource(\n            Inst, D, NeverCGrantingClass, DC, nullptr);\n', 1)),
                (5, 'friend class lost target kind', explicit_instantiate_expected.replace(
                    '    if (!Inst->isInvalidDecl() && !RecordInst->isInvalidDecl() &&\n        !D->isInvalidDecl() && !Pattern->isInvalidDecl() &&\n        Inst->getTemplatedDecl() == RecordInst &&\n        SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n      if (auto *NeverCGrantingClass = dyn_cast<CXXRecordDecl>(Owner))\n        SemaRef.getASTConsumer().HandleNeverCFriendClassTemplateSource(\n            Inst, D, NeverCGrantingClass, DC, PrevClassTemplate);\n', '    if (!Inst->isInvalidDecl() && !RecordInst->isInvalidDecl() &&\n        !D->isInvalidDecl() && !Pattern->isInvalidDecl() &&\n        true &&\n        SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n      if (auto *NeverCGrantingClass = dyn_cast<CXXRecordDecl>(Owner))\n        SemaRef.getASTConsumer().HandleNeverCFriendClassTemplateSource(\n            Inst, D, NeverCGrantingClass, DC, PrevClassTemplate);\n', 1)),
                (5, 'friend class lost actual validity', explicit_instantiate_expected.replace(
                    '    if (!Inst->isInvalidDecl() && !RecordInst->isInvalidDecl() &&\n        !D->isInvalidDecl() && !Pattern->isInvalidDecl() &&\n        Inst->getTemplatedDecl() == RecordInst &&\n        SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n      if (auto *NeverCGrantingClass = dyn_cast<CXXRecordDecl>(Owner))\n        SemaRef.getASTConsumer().HandleNeverCFriendClassTemplateSource(\n            Inst, D, NeverCGrantingClass, DC, PrevClassTemplate);\n', '    if (true &&\n        !D->isInvalidDecl() && !Pattern->isInvalidDecl() &&\n        Inst->getTemplatedDecl() == RecordInst &&\n        SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n      if (auto *NeverCGrantingClass = dyn_cast<CXXRecordDecl>(Owner))\n        SemaRef.getASTConsumer().HandleNeverCFriendClassTemplateSource(\n            Inst, D, NeverCGrantingClass, DC, PrevClassTemplate);\n', 1)),
                (5, 'friend class lost original validity', explicit_instantiate_expected.replace(
                    '    if (!Inst->isInvalidDecl() && !RecordInst->isInvalidDecl() &&\n        !D->isInvalidDecl() && !Pattern->isInvalidDecl() &&\n        Inst->getTemplatedDecl() == RecordInst &&\n        SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n      if (auto *NeverCGrantingClass = dyn_cast<CXXRecordDecl>(Owner))\n        SemaRef.getASTConsumer().HandleNeverCFriendClassTemplateSource(\n            Inst, D, NeverCGrantingClass, DC, PrevClassTemplate);\n', '    if (!Inst->isInvalidDecl() && !RecordInst->isInvalidDecl() &&\n        true &&\n        Inst->getTemplatedDecl() == RecordInst &&\n        SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n      if (auto *NeverCGrantingClass = dyn_cast<CXXRecordDecl>(Owner))\n        SemaRef.getASTConsumer().HandleNeverCFriendClassTemplateSource(\n            Inst, D, NeverCGrantingClass, DC, PrevClassTemplate);\n', 1)),
                (5, 'friend class lost opt in', explicit_instantiate_expected.replace(
                    '    if (!Inst->isInvalidDecl() && !RecordInst->isInvalidDecl() &&\n        !D->isInvalidDecl() && !Pattern->isInvalidDecl() &&\n        Inst->getTemplatedDecl() == RecordInst &&\n        SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n      if (auto *NeverCGrantingClass = dyn_cast<CXXRecordDecl>(Owner))\n        SemaRef.getASTConsumer().HandleNeverCFriendClassTemplateSource(\n            Inst, D, NeverCGrantingClass, DC, PrevClassTemplate);\n', '    if (!Inst->isInvalidDecl() && !RecordInst->isInvalidDecl() &&\n        !D->isInvalidDecl() && !Pattern->isInvalidDecl() &&\n        Inst->getTemplatedDecl() == RecordInst &&\n        true)\n      if (auto *NeverCGrantingClass = dyn_cast<CXXRecordDecl>(Owner))\n        SemaRef.getASTConsumer().HandleNeverCFriendClassTemplateSource(\n            Inst, D, NeverCGrantingClass, DC, PrevClassTemplate);\n', 1)),
                (5, 'friend class rejected legitimate introduction', explicit_instantiate_expected.replace(
                    '  // Finish handling of friends.\n  if (isFriend) {\n    DC->makeDeclVisibleInContext(Inst);\n    if (!Inst->isInvalidDecl() && !RecordInst->isInvalidDecl() &&\n        !D->isInvalidDecl() && !Pattern->isInvalidDecl() &&\n        Inst->getTemplatedDecl() == RecordInst &&\n        SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n      if (auto *NeverCGrantingClass = dyn_cast<CXXRecordDecl>(Owner))\n        SemaRef.getASTConsumer().HandleNeverCFriendClassTemplateSource(\n            Inst, D, NeverCGrantingClass, DC, PrevClassTemplate);\n    return Inst;\n  }', '  // Finish handling of friends.\n  if (isFriend && PrevClassTemplate) {\n    DC->makeDeclVisibleInContext(Inst);\n    if (!Inst->isInvalidDecl() && !RecordInst->isInvalidDecl() &&\n        !D->isInvalidDecl() && !Pattern->isInvalidDecl() &&\n        Inst->getTemplatedDecl() == RecordInst &&\n        SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n      if (auto *NeverCGrantingClass = dyn_cast<CXXRecordDecl>(Owner))\n        SemaRef.getASTConsumer().HandleNeverCFriendClassTemplateSource(\n            Inst, D, NeverCGrantingClass, DC, PrevClassTemplate);\n    return Inst;\n  }', 1)),
                (5, 'friend class lost visibility success', explicit_instantiate_expected.replace(
                    '  // Finish handling of friends.\n  if (isFriend) {\n    DC->makeDeclVisibleInContext(Inst);\n    if (!Inst->isInvalidDecl() && !RecordInst->isInvalidDecl() &&\n        !D->isInvalidDecl() && !Pattern->isInvalidDecl() &&\n        Inst->getTemplatedDecl() == RecordInst &&\n        SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n      if (auto *NeverCGrantingClass = dyn_cast<CXXRecordDecl>(Owner))\n        SemaRef.getASTConsumer().HandleNeverCFriendClassTemplateSource(\n            Inst, D, NeverCGrantingClass, DC, PrevClassTemplate);\n    return Inst;\n  }', '  // Finish handling of friends.\n  if (isFriend) {\n    if (!Inst->isInvalidDecl() && !RecordInst->isInvalidDecl() &&\n        !D->isInvalidDecl() && !Pattern->isInvalidDecl() &&\n        Inst->getTemplatedDecl() == RecordInst &&\n        SemaRef.getASTConsumer().wantsNeverCTemplateSource())\n      if (auto *NeverCGrantingClass = dyn_cast<CXXRecordDecl>(Owner))\n        SemaRef.getASTConsumer().HandleNeverCFriendClassTemplateSource(\n            Inst, D, NeverCGrantingClass, DC, PrevClassTemplate);\n    return Inst;\n  }', 1)),
                (5, "copied class full lost exact origin", explicit_instantiate_expected.replace(
                    "InstD, D, InstTemplateArgs,", "InstD, nullptr, InstTemplateArgs,", 1)),
                (5, "copied class full lost written source", explicit_instantiate_expected.replace(
                    "InstD, D, InstTemplateArgs,", "InstD, D, TemplateArgumentListInfo(),", 1)),
                (5, "copied class full lost actual location", explicit_instantiate_expected.replace(
                    "InstD->getLocation());", "D->getLocation());", 1)),
                (5, "copied partial lost exact origin", explicit_instantiate_expected.replace(
                    "InstPartialSpec, PartialSpec, InstTemplateArgs,",
                    "InstPartialSpec, nullptr, InstTemplateArgs,", 1)),
                (5, "copied partial lost converted source", explicit_instantiate_expected.replace(
                    "InstPartialSpec, PartialSpec, InstTemplateArgs,",
                    "InstPartialSpec, PartialSpec, TemplateArgumentListInfo(),", 1)),
                (1, "written partial claims a copied origin", explicit_source_expected.replace(
                    "Specialization, nullptr, TemplateArgs,",
                    "Specialization, Specialization, TemplateArgs,", 1)),
                (5, "member full declaration lost argument source", explicit_instantiate_expected.replace(
                    "NeverCVariableFull, VarTemplateArgsInfo,",
                    "NeverCVariableFull, TemplateArgumentListInfo(),", 1)),
                (5, "member full declaration inherited storage is not written", explicit_instantiate_expected.replace(
                    "/*WrittenStorageClass=*/false,",
                    "/*WrittenStorageClass=*/true,", 1)),
                (5, "variable definition lost previous declaration", explicit_instantiate_expected.replace(
                    "Var, PrevDecl, D, DI, false, Var->getLocation()",
                    "Var, nullptr, D, DI, false, Var->getLocation()", 1)),
                (5, "variable completion borrowed previous declaration", explicit_instantiate_expected.replace(
                    "VarSpec, nullptr, PatternDecl, DI, true, VarSpec->getLocation()",
                    "VarSpec, PrevDecl, PatternDecl, DI, true, VarSpec->getLocation()", 1)),
                (5, 'orphan HandleNeverCVariableTypeSource', explicit_instantiate_expected + '// HandleNeverCVariableTypeSource\n'),
                (2, 'partial missing anchor 0', explicit_deduction_original.replace('  if (S.SubstTemplateArguments(\n          PartialTemplArgInfo->arguments(),\n          MultiLevelTemplateArgumentList(Partial, CTAI.SugaredConverted,\n                                         /*Final=*/true),\n          InstArgs)) {', "// Missing partial anchor.", 1)),
                (2, 'partial drifted block 0', explicit_deduction_expected.replace('  if (S.SubstTemplateArguments(\n          PartialTemplArgInfo->arguments(),\n          MultiLevelTemplateArgumentList(Partial, CTAI.SugaredConverted,\n                                         /*Final=*/!(isa<ClassTemplatePartialSpecializationDecl,\n                                                         VarTemplatePartialSpecializationDecl>(Partial) &&\n                                                     !IsPartialOrdering &&\n                                                     S.getASTConsumer().wantsNeverCTemplateSource())),\n          InstArgs)) {', '  if (S.SubstTemplateArguments(\n          PartialTemplArgInfo->arguments(),\n          MultiLevelTemplateArgumentList(Partial, CTAI.SugaredConverted,\n                                         /*Final=*/!(isa<ClassTemplatePartialSpe/* Partial source drift. */cializationDecl>(Partial) &&\n                                                     !IsPartialOrdering &&\n                                                     S.getASTConsumer().wantsNeverCTemplateSource())),\n          InstArgs)) {', 1)),
                (2, 'partial missing anchor 1', explicit_deduction_original.replace('  if (!IsPartialOrdering) {\n    if (auto Result = CheckDeducedArgumentConstraints(\n            S, Partial, CTAI.SugaredConverted, CTAI.CanonicalConverted, Info);\n        Result != TemplateDeductionResult::Success)\n      return Result;\n  }\n\n  return TemplateDeductionResult::Success;\n}\n\n/// Complete template argument deduction for a class or variable template,', "// Missing partial anchor.", 1)),
                (2, 'partial drifted block 1', explicit_deduction_expected.replace('  if (!IsPartialOrdering) {\n    if (auto Result = CheckDeducedArgumentConstraints(\n            S, Partial, CTAI.SugaredConverted, CTAI.CanonicalConverted, Info);\n        Result != TemplateDeductionResult::Success)\n      return Result;\n  }\n\n  if (auto *NeverCPartial = dyn_cast<ClassTemplatePartialSpecializationDecl>(Partial);\n      NeverCPartial && !IsPartialOrdering && !Trap.hasErrorOccurred() &&\n      S.getASTConsumer().wantsNeverCTemplateSource()) {\n    auto NeverCRetainPartial = [&](bool Pattern,\n                                   const TemplateArgumentListInfo *Written,\n                                   const Sema::CheckTemplateArgumentInfo &Checked) {\n      S.getASTConsumer().HandleNeverCClassPartialSource(\n          NeverCPartial, CanonicalDeducedArgumentList, Pattern, Written,\n          Checked.CanonicalConverted.data(), Checked.SugaredConverted.data(),\n          Checked.CanonicalConverted.size(), Checked.NeverCDefaultParameters.data(),\n          Checked.NeverCWrittenDefaults.data(), Checked.NeverCConvertedDefaults.data(),\n          Checked.NeverCDefaultParameters.size(), Checked.NeverCTypeParameters.data(),\n          Checked.NeverCParameterTypes.data(), Checked.NeverCParameterPackIndices.data(),\n          Checked.NeverCTypeParameters.size(), Checked.NeverCDefaultsOverflow,\n          Info.getLocation());\n    };\n    NeverCRetainPartial(false, nullptr, CTAI);\n    NeverCRetainPartial(true, &InstArgs, InstCTAI);\n  }\n\n  if (auto *NeverCVariablePartial = dyn_cast<VarTemplatePartialSpecializationDecl>(Partial);\n      NeverCVariablePartial && !IsPartialOrdering && !Trap.hasErrorOccurred() &&\n      S.getASTConsumer().wantsNeverCTemplateSource()) {\n    auto NeverCRetainVariablePartial = [&](bool Pattern,\n                                           const TemplateArgumentListInfo *Written,\n                                           const Sema::CheckTemplateArgumentInfo &Checked) {\n      // Variable selection transfers takeSugared(), unlike class selection.\n      S.getASTConsumer().HandleNeverCVariablePartialSource(\n          NeverCVariablePartial, SugaredDeducedArgumentList, Pattern, Written,\n          Checked.CanonicalConverted.data(), Checked.SugaredConverted.data(),\n          Checked.CanonicalConverted.size(), Checked.NeverCDefaultParameters.data(),\n          Checked.NeverCWrittenDefaults.data(), Checked.NeverCConvertedDefaults.data(),\n          Checked.NeverCDefaultParameters.size(), Checked.NeverCTypeParameters.data(),\n          Checked.NeverCParameterTypes.data(), Checked.NeverCParameterPackIndices.data(),\n          Checked.NeverCTypeParameters.size(), Checked.NeverCDefaultsOverflow,\n          Info.getLocation());\n    };\n    NeverCRetainVariablePartial(false, nullptr, CTAI);\n    NeverCRetainVariablePartial(true, &InstArgs, InstCTAI);\n  }\n\n  return TemplateDeductionResult::Success;\n}\n\n/// Complete template argument deduction for a class or variable template,', '  if (!IsPartialOrdering) {\n    if (auto Result = CheckDeducedArgumentConstraints(\n            S, Partial, CTAI.SugaredConverted, CTAI.CanonicalConverted, Info);\n        Result != TemplateDeductionResult::Success)\n      return Result;\n  }\n\n  if (auto *NeverCPartial = dyn_cast<ClassTemplatePartialSpecializationDecl>(Partial);\n      NeverCPartial && !IsPartialOrdering && !Trap.hasErrorOccurred() &&\n      S.getASTConsumer().wantsNeverCTemplateSource()) {\n    auto NeverCRetainPartial = [&](bool Pattern,\n                                   const TemplateArgumentListInfo *Written,\n                                   const Sema::CheckTemplateArgumentInfo &Checked) {\n      S.getASTConsumer().HandleNeverCClassPartialSource(\n          NeverCPartial, CanonicalDeducedArgumentList, Patt/* Partial source drift. */ern, Written,\n          Checked.CanonicalConverted.data(), Checked.SugaredConverted.data(),\n          Checked.CanonicalConverted.size(), Checked.NeverCDefaultParameters.data(),\n          Checked.NeverCWrittenDefaults.data(), Checked.NeverCConvertedDefaults.data(),\n          Checked.NeverCDefaultParameters.size(), Checked.NeverCTypeParameters.data(),\n          Checked.NeverCParameterTypes.data(), Checked.NeverCParameterPackIndices.data(),\n          Checked.NeverCTypeParameters.size(), Checked.NeverCDefaultsOverflow,\n          Info.getLocation());\n    };\n    NeverCRetainPartial(false, nullptr, CTAI);\n    NeverCRetainPartial(true, &InstArgs, InstCTAI);\n  }\n\n  return TemplateDeductionResult::Success;\n}\n\n/// Complete template argument deduction for a class or variable template,', 1)),
                (0, "partial missing declaration", explicit_header_expected.replace('  class ClassTemplatePartialSpecializationDecl;\n  class TemplateArgumentList;', "", 1)),
                (0, "partial missing callback", explicit_header_expected.replace('\n  // The exact deduced list later identifies the selected partial candidate.\n  virtual void HandleNeverCClassPartialSource(\n      ClassTemplatePartialSpecializationDecl *, const TemplateArgumentList *,\n      bool, const TemplateArgumentListInfo *,\n      const TemplateArgument *, const TemplateArgument *, unsigned,\n      NamedDecl *const *, const TemplateArgumentLoc *, const TemplateArgumentLoc *,\n      unsigned, NamedDecl *const *, TypeSourceInfo *const *,\n      const unsigned *, unsigned, bool, const SourceLocation &) {}', "", 1)),
                (0, 'orphan HandleNeverCClassPartialSource', explicit_header_expected + '// HandleNeverCClassPartialSource\n'),
                (2, 'orphan NeverCPartial', explicit_deduction_expected + '// NeverCPartial\n'),
                (4, 'orphan NeverCRetainPartial', explicit_deduction_info_expected + '// NeverCRetainPartial\n'),
                (0, "header missing anchor 0", explicit_header_original.replace('  class FunctionDecl;\n  class ImportDecl;', "// Missing source anchor.", 1)),
                (0, "header drifted block 0", explicit_header_expected.replace('  class FunctionDecl;\n  class ImportDecl;\n  class TemplateArgumentListInfo;\n  class TypeSourceInfo;\n  struct DeclarationNameInfo;\n  class NestedNameSpecifierLoc;\n  class SourceLocation;\n  class TemplateDecl;\n  class NonTypeTemplateParmDecl;\n  class TemplateArgumentLoc;\n  class TemplateArgument;\n  class Type;\n  class NamedDecl;\n  class ClassTemplateSpecializationDecl;', '  class FunctionDecl;\n  class ImportDecl;\n  class TemplateArgumentListInfo;\n  class TypeSourceInfo;\n  struct DeclarationNameInfo;\n  class NestedNameSpecifierLoc;\n  class SourceLocation/* Unexpected source drift. */;\n  class TemplateDecl;\n  class NonTypeTemplateParmDecl;\n  class TemplateArgumentLoc;\n  class TemplateArgument;\n  class Type;\n  class NamedDecl;\n  class ClassTemplateSpecializationDecl;', 1)),
                (0, "header missing anchor 1", explicit_header_original.replace('  virtual void HandleCXXImplicitFunctionInstantiation(FunctionDecl *D) {}', "// Missing source anchor.", 1)),
                (0, "header drifted block 1", explicit_header_expected.replace('  virtual void HandleCXXImplicitFunctionInstantiation(FunctionDecl *D) {}\n\n  // NeverC private source evidence; this does not request instantiation.\n  virtual void HandleNeverCExplicitFunctionInstantiation(\n      FunctionDecl *, const TemplateArgumentListInfo &, TypeSourceInfo *,\n      const DeclarationNameInfo &, const NestedNameSpecifierLoc &,\n      const SourceLocation &, bool) {}', '  virtual void HandleCXXImplicitFunctionInstantiation(FunctionDecl *D) {}\n\n  // NeverC private source evidence; this does not request instantiation.\n  virtual void HandleNeverCExplicitFunctionI/* Unexpected source drift. */nstantiation(\n      FunctionDecl *, const TemplateArgumentListInfo &, TypeSourceInfo *,\n      const DeclarationNameInfo &, const NestedNameSpecifierLoc &,\n      const SourceLocation &, bool) {}', 1)),
                (0, "header missing anchor 2", explicit_header_original.replace('  virtual void HandleCXXStaticMemberVarInstantiation(VarDecl *D) {}', "// Missing source anchor.", 1)),
                (0, "header drifted block 2", explicit_header_expected.replace('  virtual void HandleCXXStaticMemberVarInstantiation(VarDecl *D) {}\n\n  // NeverC private source evidence for each static member directive.\n  virtual void HandleNeverCExplicitStaticDataInstantiation(\n      VarDecl *, TypeSourceInfo *, const NestedNameSpecifierLoc &,\n      const SourceLocation &, bool) {}\n\n  // NeverC retains defaults only after successful argument conversion.\n  virtual void HandleNeverCScalarTemplateDefault(\n      TemplateDecl *, NonTypeTemplateParmDecl *,\n      const TemplateArgumentLoc &, const TemplateArgumentLoc &,\n      const TemplateArgument &, const SourceLocation &) {}\n\n  // NeverC source preservation is opt-in; other consumers keep upstream ASTs.\n  virtual bool wantsNeverCTemplateSource() const { return false; }\n\n  // Separate semantic defaults only for consumers that require object identity.\n  enum class NeverCArrayFillerAction { KeepShared, Separate, Invalid };\n  virtual NeverCArrayFillerAction HandleNeverCArrayFiller(\n      ASTContext &, const Expr *, unsigned long long, unsigned long long) {\n    return NeverCArrayFillerAction::KeepShared;\n  }\n  // Preserve omitted-element provenance after semantic expansion.\n  virtual void HandleNeverCArrayFillerElement(const Expr *) {}\n  virtual void HandleNeverCTemplateTypeSource(\n      TemplateDecl *, const Type *, TypeSourceInfo *,\n      const TemplateArgumentListInfo &,\n      const TemplateArgument *, const TemplateArgument *, unsigned,\n      NamedDecl *const *, const TemplateArgumentLoc *, const TemplateArgumentLoc *,\n      unsigned, NamedDecl *const *, TypeSourceInfo *const *,\n      const unsigned *, unsigned, bool, const SourceLocation &) {}\n  virtual void HandleNeverCFunctionTemplateSource(\n      FunctionDecl *,\n      const TemplateArgument *, const TemplateArgument *, unsigned,\n      NamedDecl *const *, const TemplateArgumentLoc *, const TemplateArgumentLoc *,\n      unsigned, NamedDecl *const *, TypeSourceInfo *const *,\n      const unsigned *, unsigned, bool, const SourceLocation &) {}\n  virtual void HandleNeverCClassTemplateSource(\n      ClassTemplateSpecializationDecl *, const TemplateArgumentListInfo &, bool,\n      const TemplateArgument *, const TemplateArgument *, unsigned,\n      NamedDecl *const *, const TemplateArgumentLoc *, const TemplateArgumentLoc *,\n      unsigned, NamedDecl *const *, TypeSourceInfo *const *,\n      const unsigned *, unsigned, bool, const SourceLocation &) {}\n  virtual void HandleNeverCFunctionSpecializationSource(\n      FunctionDecl *, FunctionDecl *, const TemplateArgumentListInfo *,\n      const SourceLocation &) {}', '  virtual void HandleCXXStaticMemberVarInstantiation(VarDecl *D) {}\n\n  // NeverC private source evidence for each static member directive.\n  virtual void HandleNeverCExplicitStaticDataInstantiation(\n      VarDecl *, TypeSourceInfo *, const NestedNameSpecifierLoc &,\n      const SourceLocation &, bool) {}\n\n  // NeverC retains defaults only after successful argument conversion.\n  virtual void HandleNeverCScalarTemplateDefault(\n      TemplateDecl *, NonTypeTemplateParmDecl *,\n      const TemplateArgumentLoc &, const TemplateArgumentLoc &,\n      const TemplateArgument &, const SourceLocation &) {}\n\n  // NeverC source preservation is opt-in; other consumers keep upstream ASTs.\n  virtual bool wantsNeverCTemplateSource() const { return false; }\n\n  // Separate semantic defaults only for consumers that require object identity.\n  enum class NeverCArrayFillerAction { KeepShared, Separate, Invalid };\n  virtual NeverCArrayFillerAction HandleNeverCArrayFiller(\n      ASTContext &, const Expr *, unsigned long long, unsigned long long) {\n    return NeverCArrayFillerAction::KeepShared;\n  }\n  // Preserve omitted-element provenance after semantic expansion.\n  virtual void HandleNeverCArrayFillerElement(const Expr *) {}\n  virtual void HandleNeverCTemplateTypeSource(\n      TemplateDecl *, const Type *, TypeSourceInfo *,\n      const TemplateArgumentListInfo &,\n      const TemplateArgument *, const TemplateArgument *, unsigned,\n      NamedDecl *const *, const TemplateArgumentLoc *, const TemplateArgumentLoc *,\n     /* Unexpected source drift. */ unsigned, NamedDecl *const *, TypeSourceInfo *const *,\n      const unsigned *, unsigned, bool, const SourceLocation &) {}\n  virtual void HandleNeverCFunctionTemplateSource(\n      FunctionDecl *,\n      const TemplateArgument *, const TemplateArgument *, unsigned,\n      NamedDecl *const *, const TemplateArgumentLoc *, const TemplateArgumentLoc *,\n      unsigned, NamedDecl *const *, TypeSourceInfo *const *,\n      const unsigned *, unsigned, bool, const SourceLocation &) {}\n  virtual void HandleNeverCClassTemplateSource(\n      ClassTemplateSpecializationDecl *, const TemplateArgumentListInfo &, bool,\n      const TemplateArgument *, const TemplateArgument *, unsigned,\n      NamedDecl *const *, const TemplateArgumentLoc *, const TemplateArgumentLoc *,\n      unsigned, NamedDecl *const *, TypeSourceInfo *const *,\n      const unsigned *, unsigned, bool, const SourceLocation &) {}\n  virtual void HandleNeverCFunctionSpecializationSource(\n      FunctionDecl *, FunctionDecl *, const TemplateArgumentListInfo *,\n      const SourceLocation &) {}', 1)),
                (1, "source missing anchor 0", explicit_source_original.replace('    // Check the default template argument.\n    if (CheckTemplateArgument(*Param, Arg, Template, TemplateLoc, RAngleLoc, 0,\n                              CTAI, CTAK_Specified))\n      return true;\n\n    CTAI.SugaredConverted.back().setIsDefaulted(true);', "// Missing source anchor.", 1)),
                (1, "source drifted block 0", explicit_source_expected.replace('    // Preserve original spelling as well as any conversion-added operations.\n    const auto NeverCWrittenDefault = Arg;\n    // Check the default template argument.\n    if (CheckTemplateArgument(*Param, Arg, Template, TemplateLoc, RAngleLoc, 0,\n                              CTAI, CTAK_Specified))\n      return true;\n\n    if (Consumer.wantsNeverCTemplateSource()) {\n      if (isa<TemplateTypeParmDecl, NonTypeTemplateParmDecl>(*Param))\n        CTAI.retainNeverCDefault(*Param, NeverCWrittenDefault, Arg);\n    } else {\n      if (auto *NeverCParameter = dyn_cast<NonTypeTemplateParmDecl>(*Param))\n        Consumer.HandleNeverCScalarTemplateDefault(\n            Template, NeverCParameter, NeverCWrittenDefault, Arg,\n            CTAI.CanonicalConverted.back(), TemplateLoc);\n    }\n    CTAI.SugaredConverted.back().setIsDefaulted(true);', '    // Preserve original spelling as well as any conversion-added operations.\n    const auto NeverCWrittenDefault = Arg;\n    // Check the default template argument.\n    if (CheckTemplateArgument(*Param, Arg, Template, TemplateLoc, RAngleLoc, 0,\n                              CTAI, CTAK_Specified))\n      return true;\n\n    if (Consumer.wantsNeverCTemplateSource()) {\n      if (isa<TemplateTypeParmDecl, NonTypeTempla/* Unexpected source drift. */teParmDecl>(*Param))\n        CTAI.retainNeverCDefault(*Param, NeverCWrittenDefault, Arg);\n    } else {\n      if (auto *NeverCParameter = dyn_cast<NonTypeTemplateParmDecl>(*Param))\n        Consumer.HandleNeverCScalarTemplateDefault(\n            Template, NeverCParameter, NeverCWrittenDefault, Arg,\n            CTAI.CanonicalConverted.back(), TemplateLoc);\n    }\n    CTAI.SugaredConverted.back().setIsDefaulted(true);', 1)),
                (1, "source missing anchor 1", explicit_source_original.replace('                                            Declarator &D) {\n  // Explicit instantiations always require a name.', "// Missing source anchor.", 1)),
                (1, "source drifted block 1", explicit_source_expected.replace('                                            Declarator &D) {\n  // Retain attributes before declarator type processing can consume them.\n  const bool NeverCWrittenAttributes = D.hasAttributes();\n  // Explicit instantiations always require a name.', '                                            Declarator &D) {\n  // Retain attributes before declarator type processing can /* Unexpected source drift. */consume them.\n  const bool NeverCWrittenAttributes = D.hasAttributes();\n  // Explicit instantiations always require a name.', 1)),
                (1, "source missing anchor 2", explicit_source_original.replace('    CheckExplicitInstantiation(*this, Prev, D.getIdentifierLoc(), true, TSK);', "// Missing source anchor.", 1)),
                (1, "source drifted block 2", explicit_source_expected.replace('    // Preserve written static-member source before no-effect handling.\n    Consumer.HandleNeverCExplicitStaticDataInstantiation(\n        Prev, T, D.getCXXScopeSpec().getWithLocInContext(Context),\n        D.getIdentifierLoc(), NeverCWrittenAttributes);\n\n    CheckExplicitInstantiation(*this, Prev, D.getIdentifierLoc(), true, TSK);', '    // Preserve written static-member source before no-effect handling.\n    Consumer.HandleNeverCExplicitStaticDataInstantiation(\n        Prev, T, D.getCXXScopeSpec(/* Unexpected source drift. */).getWithLocInContext(Context),\n        D.getIdentifierLoc(), NeverCWrittenAttributes);\n\n    CheckExplicitInstantiation(*this, Prev, D.getIdentifierLoc(), true, TSK);', 1)),
                (1, "source missing anchor 3", explicit_source_original.replace('    Specialization = cast<FunctionDecl>(*Result);\n  }\n\n  // C++11 [except.spec]p4\n  // In an explicit instantiation an exception-specification may be specified,', "// Missing source anchor.", 1)),
                (1, "source drifted block 3", explicit_source_expected.replace('    Specialization = cast<FunctionDecl>(*Result);\n  }\n\n  // Preserve every directive before duplicate/no-effect early returns.\n  Consumer.HandleNeverCExplicitFunctionInstantiation(\n      Specialization, TemplateArgs, T, NameInfo,\n      D.getCXXScopeSpec().getWithLocInContext(Context),\n      D.getIdentifierLoc(), NeverCWrittenAttributes);\n\n  // C++11 [except.spec]p4\n  // In an explicit instantiation an exception-specification may be specified,', '    Specialization = cast<FunctionDecl>(*Result);\n  }\n\n  // Preserve every directive before duplicate/no-effect early returns.\n  Consumer.HandleNeverCExplicitFunctionInstantiation(\n      Specialization, TemplateArgs, T, Nam/* Unexpected source drift. */eInfo,\n      D.getCXXScopeSpec().getWithLocInContext(Context),\n      D.getIdentifierLoc(), NeverCWrittenAttributes);\n\n  // C++11 [except.spec]p4\n  // In an explicit instantiation an exception-specification may be specified,', 1)),
                (1, "source missing anchor 4", explicit_source_original.replace('        /*Final=*/!getLangOpts().RetainSubstTemplateTypeParmTypeAstNodes);', "// Missing source anchor.", 1)),
                (1, "source drifted block 4", explicit_source_expected.replace('        /*Final=*/!getLangOpts().RetainSubstTemplateTypeParmTypeAstNodes &&\n            !Consumer.wantsNeverCTemplateSource());', '        /*Final=*/!getLangOpts().RetainSubstTemplateTypeParmTyp/* Unexpected source drift. */eAstNodes &&\n            !Consumer.wantsNeverCTemplateSource());', 1)),
                (1, "source missing anchor 5", explicit_source_original.replace('  QualType CanonType;\n\n  if (TypeAliasTemplateDecl *AliasTemplate =', "// Missing source anchor.", 1)),
                (1, "source drifted block 5", explicit_source_expected.replace('  QualType CanonType;\n  TypeSourceInfo *NeverCAliasSource = nullptr;\n\n  if (TypeAliasTemplateDecl *AliasTemplate =', '  QualType CanonType;\n  TypeSourceInfo *NeverCAliasSource/* Unexpected source drift. */ = nullptr;\n\n  if (TypeAliasTemplateDecl *AliasTemplate =', 1)),
                (1, "source missing anchor 6", explicit_source_original.replace('    CanonType =\n        SubstType(Pattern->getUnderlyingType(), TemplateArgLists,\n                  AliasTemplate->getLocation(), AliasTemplate->getDeclName());', "// Missing source anchor.", 1)),
                (1, "source drifted block 6", explicit_source_expected.replace('    if (Consumer.wantsNeverCTemplateSource()) {\n      NeverCAliasSource =\n          SubstType(Pattern->getTypeSourceInfo(), TemplateArgLists,\n                    AliasTemplate->getLocation(), AliasTemplate->getDeclName());\n      CanonType = NeverCAliasSource ? NeverCAliasSource->getType() : QualType();\n    } else {\n      CanonType =\n          SubstType(Pattern->getUnderlyingType(), TemplateArgLists,\n                    AliasTemplate->getLocation(), AliasTemplate->getDeclName());\n    }', '    if (Consumer.wantsNeverCTemplateSource()) {\n      NeverCAliasSource =\n          SubstType(Pattern->getTypeSourceInfo(), TemplateArgLists,\n                    AliasTemplate->getLocation(), AliasTemplate->getDeclName());\n      CanonType = Nev/* Unexpected source drift. */erCAliasSource ? NeverCAliasSource->getType() : QualType();\n    } else {\n      CanonType =\n          SubstType(Pattern->getUnderlyingType(), TemplateArgLists,\n                    AliasTemplate->getLocation(), AliasTemplate->getDeclName());\n    }', 1)),
                (1, "source missing anchor 7", explicit_source_original.replace('    MultiLevelTemplateArgumentList TemplateArgLists(Template, SugaredConverted,\n                                                    /*Final=*/true);', "// Missing source anchor.", 1)),
                (1, "source drifted block 7", explicit_source_expected.replace('    MultiLevelTemplateArgumentList TemplateArgLists(\n        Template, SugaredConverted,\n        /*Final=*/!SemaRef.getASTConsumer().wantsNeverCTemplateSource());', '    MultiLevelTemplateArgumentList TemplateArgLists(\n        Template, SugaredCon/* Unexpected source drift. */verted,\n        /*Final=*/!SemaRef.getASTConsumer().wantsNeverCTemplateSource());', 1)),
                (1, "source missing anchor 8", explicit_source_original.replace('static bool SubstDefaultTemplateArgument(\n    Sema &SemaRef, TemplateDecl *Template, SourceLocation TemplateLoc,\n    SourceLocation RAngleLoc, NonTypeTemplateParmDecl *Param,\n    ArrayRef<TemplateArgument> SugaredConverted,\n    ArrayRef<TemplateArgument> CanonicalConverted,\n    TemplateArgumentLoc &Output) {\n  Sema::InstantiatingTemplate Inst(SemaRef, TemplateLoc, Param, Template,\n                                   SugaredConverted,\n                                   SourceRange(TemplateLoc, RAngleLoc));\n  if (Inst.isInvalid())\n    return true;\n\n  // Only substitute for the innermost template argument list.\n  MultiLevelTemplateArgumentList TemplateArgLists(Template, SugaredConverted,\n                                                  /*Final=*/true);', "// Missing source anchor.", 1)),
                (1, "source drifted block 8", explicit_source_expected.replace('static bool SubstDefaultTemplateArgument(\n    Sema &SemaRef, TemplateDecl *Template, SourceLocation TemplateLoc,\n    SourceLocation RAngleLoc, NonTypeTemplateParmDecl *Param,\n    ArrayRef<TemplateArgument> SugaredConverted,\n    ArrayRef<TemplateArgument> CanonicalConverted,\n    TemplateArgumentLoc &Output) {\n  Sema::InstantiatingTemplate Inst(SemaRef, TemplateLoc, Param, Template,\n                                   SugaredConverted,\n                                   SourceRange(TemplateLoc, RAngleLoc));\n  if (Inst.isInvalid())\n    return true;\n\n  // Only substitute for the innermost template argument list.\n  MultiLevelTemplateArgumentList TemplateArgLists(\n      Template, SugaredConverted,\n      /*Final=*/!SemaRef.getASTConsumer().wantsNeverCTemplateSource());', 'static bool SubstDefaultTemplateArgument(\n    Sema &SemaRef, TemplateDecl *Template, SourceLocation TemplateLoc,\n    SourceLocation RAngleLoc, NonTypeTemplateParmDecl *Param,\n    ArrayRef<TemplateArgument> SugaredConverted,\n    ArrayRef<TemplateArgument> CanonicalConverted,\n    TemplateArgumentLoc &Output) {\n  Sema::InstantiatingTemplate Inst(SemaRef, TemplateLoc, Param, Template,\n /* Unexpected source drift. */                                  SugaredConverted,\n                                   SourceRange(TemplateLoc, RAngleLoc));\n  if (Inst.isInvalid())\n    return true;\n\n  // Only substitute for the innermost template argument list.\n  MultiLevelTemplateArgumentList TemplateArgLists(\n      Template, SugaredConverted,\n      /*Final=*/!SemaRef.getASTConsumer().wantsNeverCTemplateSource());', 1)),
                (1, "source missing anchor 9", explicit_source_original.replace('  return Context.getTemplateSpecializationType(Name, TemplateArgs.arguments(),\n                                               CanonType);', "// Missing source anchor.", 1)),
                (1, "source drifted block 9", explicit_source_expected.replace('  QualType NeverCTypeResult = Context.getTemplateSpecializationType(\n      Name, TemplateArgs.arguments(), CanonType);\n  if (Consumer.wantsNeverCTemplateSource())\n    Consumer.HandleNeverCTemplateTypeSource(\n        Template, NeverCTypeResult.getTypePtr(), NeverCAliasSource, TemplateArgs,\n        CTAI.CanonicalConverted.data(), CTAI.SugaredConverted.data(),\n        CTAI.CanonicalConverted.size(), CTAI.NeverCDefaultParameters.data(),\n        CTAI.NeverCWrittenDefaults.data(), CTAI.NeverCConvertedDefaults.data(),\n        CTAI.NeverCDefaultParameters.size(), CTAI.NeverCTypeParameters.data(),\n        CTAI.NeverCParameterTypes.data(), CTAI.NeverCParameterPackIndices.data(),\n        CTAI.NeverCTypeParameters.size(), CTAI.NeverCDefaultsOverflow, TemplateLoc);\n  return NeverCTypeResult;', '  QualType NeverCTypeResult = Context.getTemplateSpecializationType(\n      Name, TemplateArgs.arguments(), CanonType);\n  if (Consumer.wantsNeverCTemplateSource())\n    Consumer.HandleNeverCTemplateTypeSource(\n        Template, NeverCTypeResult.getTypePtr(), NeverCAliasSource, TemplateArgs,\n        CTAI.CanonicalConverted.data(), CTAI.SugaredConverted.data(),\n        CTAI.CanonicalConverted.si/* Unexpected source drift. */ze(), CTAI.NeverCDefaultParameters.data(),\n        CTAI.NeverCWrittenDefaults.data(), CTAI.NeverCConvertedDefaults.data(),\n        CTAI.NeverCDefaultParameters.size(), CTAI.NeverCTypeParameters.data(),\n        CTAI.NeverCParameterTypes.data(), CTAI.NeverCParameterPackIndices.data(),\n        CTAI.NeverCTypeParameters.size(), CTAI.NeverCDefaultsOverflow, TemplateLoc);\n  return NeverCTypeResult;', 1)),
                (1, "source missing anchor 10", explicit_source_original.replace('  Specialization->setInvalidDecl(Invalid);\n  inferGslOwnerPointerAttribute(Specialization);\n  return Specialization;', "// Missing source anchor.", 1)),
                (1, 'source drifted block 10', explicit_source_expected.replace(
                    "Specialization, TemplateArgs, /*Instantiation=*/false,",
                    "Specialization, TemplateArgs, /*Instantiation=*/true,", 1)),
                (1, "source missing anchor 11", explicit_source_original.replace('  // Syntax is now OK, so return if it has no other effect on semantics.\n  if (HasNoEffect) {', "// Missing source anchor.", 1)),
                (1, "source drifted block 11", explicit_source_expected.replace('  // Preserve each declaration, including a no-effect repeated instantiation.\n  if (Consumer.wantsNeverCTemplateSource())\n    Consumer.HandleNeverCClassTemplateSource(\n        Specialization, TemplateArgs, /*Instantiation=*/true,\n        CTAI.CanonicalConverted.data(), CTAI.SugaredConverted.data(),\n        CTAI.CanonicalConverted.size(), CTAI.NeverCDefaultParameters.data(),\n        CTAI.NeverCWrittenDefaults.data(), CTAI.NeverCConvertedDefaults.data(),\n        CTAI.NeverCDefaultParameters.size(), CTAI.NeverCTypeParameters.data(),\n        CTAI.NeverCParameterTypes.data(), CTAI.NeverCParameterPackIndices.data(),\n        CTAI.NeverCTypeParameters.size(), CTAI.NeverCDefaultsOverflow, TemplateNameLoc);\n\n  // Syntax is now OK, so return if it has no other effect on semantics.\n  if (HasNoEffect) {', '  // Preserve each declaration, including a no-effect repeated instantiation.\n  if (Consumer.wantsNeverCTemplateSource())\n    Consumer.HandleNeverCClassTemplateSource(\n        Specialization, TemplateArgs, /*Instantiation=*/true,\n        CTAI.CanonicalConverted.data(), CTAI.SugaredConverted.data(),\n        CTAI.CanonicalConverted.size(), CTAI.NeverCDefaultParameters.data(),\n        CTAI.NeverCWrit/* Unexpected source drift. */tenDefaults.data(), CTAI.NeverCConvertedDefaults.data(),\n        CTAI.NeverCDefaultParameters.size(), CTAI.NeverCTypeParameters.data(),\n        CTAI.NeverCParameterTypes.data(), CTAI.NeverCParameterPackIndices.data(),\n        CTAI.NeverCTypeParameters.size(), CTAI.NeverCDefaultsOverflow, TemplateNameLoc);\n\n  // Syntax is now OK, so return if it has no other effect on semantics.\n  if (HasNoEffect) {', 1)),
                (1, "source missing anchor 12", explicit_source_original.replace('  Previous.clear();\n  Previous.addDecl(Specialization);\n  return false;', "// Missing source anchor.", 1)),
                (1, "source drifted block 12", explicit_source_expected.replace('  Previous.clear();\n  Previous.addDecl(Specialization);\n  if (Consumer.wantsNeverCTemplateSource())\n    Consumer.HandleNeverCFunctionSpecializationSource(\n        FD, Specialization,\n        ExplicitTemplateArgs ? &ConvertedTemplateArgs[Specialization] : nullptr,\n        FD->getLocation());\n  return false;', '  Previous.clear();\n  Previous.addDecl(Specialization);\n  if (Consumer.wantsNeverCTemplateSource())\n    Consumer.HandleNeverCFunctionSpecializationSource/* Unexpected source drift. */(\n        FD, Specialization,\n        ExplicitTemplateArgs ? &ConvertedTemplateArgs[Specialization] : nullptr,\n        FD->getLocation());\n  return false;', 1)),
                (1, "source missing anchor 13", explicit_source_original.replace('    QualType NTTPType = NTTP->getType();\n    if (NTTP->isParameterPack() && NTTP->isExpandedParameterPack())\n      NTTPType = NTTP->getExpansionType(ArgumentPackIndex);\n\n    if (NTTPType->isInstantiationDependentType() &&\n        !isa<TemplateTemplateParmDecl>(Template) &&\n        !Template->getDeclContext()->isDependentContext()) {\n      // Do substitution on the type of the non-type template parameter.\n      InstantiatingTemplate Inst(*this, TemplateLoc, Template, NTTP,\n                                 CTAI.SugaredConverted,\n                                 SourceRange(TemplateLoc, RAngleLoc));\n      if (Inst.isInvalid())\n        return true;\n\n      MultiLevelTemplateArgumentList MLTAL(Template, CTAI.SugaredConverted,\n                                           /*Final=*/true);\n      // If the parameter is a pack expansion, expand this slice of the pack.\n      if (auto *PET = NTTPType->getAs<PackExpansionType>()) {\n        Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(*this,\n                                                           ArgumentPackIndex);\n        NTTPType = SubstType(PET->getPattern(), MLTAL, NTTP->getLocation(),\n                             NTTP->getDeclName());\n      } else {\n        NTTPType = SubstType(NTTPType, MLTAL, NTTP->getLocation(),\n                             NTTP->getDeclName());\n      }\n\n      // If that worked, check the non-type template parameter type\n      // for validity.\n      if (!NTTPType.isNull())\n        NTTPType = CheckNonTypeTemplateParameterType(NTTPType,\n                                                     NTTP->getLocation());\n      if (NTTPType.isNull())\n        return true;\n    }\n\n', "// Missing source anchor.", 1)),
                (1, "source drifted block 13", explicit_source_expected.replace('    QualType NTTPType = NTTP->getType();\n    TypeSourceInfo *NeverCParameterTypeSource =\n        Consumer.wantsNeverCTemplateSource() ? NTTP->getTypeSourceInfo() : nullptr;\n    if (NTTP->isParameterPack() && NTTP->isExpandedParameterPack())\n      NTTPType = NTTP->getExpansionType(ArgumentPackIndex);\n    if (Consumer.wantsNeverCTemplateSource() && NTTP->isParameterPack() &&\n        NTTP->isExpandedParameterPack())\n      NeverCParameterTypeSource = NTTP->getExpansionTypeSourceInfo(ArgumentPackIndex);\n\n    if (NTTPType->isInstantiationDependentType() &&\n        !isa<TemplateTemplateParmDecl>(Template) &&\n        !Template->getDeclContext()->isDependentContext()) {\n      // Do substitution on the type of the non-type template parameter.\n      InstantiatingTemplate Inst(*this, TemplateLoc, Template, NTTP,\n                                 CTAI.SugaredConverted,\n                                 SourceRange(TemplateLoc, RAngleLoc));\n      if (Inst.isInvalid())\n        return true;\n\n      MultiLevelTemplateArgumentList MLTAL(Template, CTAI.SugaredConverted,\n                                           /*Final=*/!Consumer.wantsNeverCTemplateSource());\n      // If the parameter is a pack expansion, expand this slice of the pack.\n      if (auto *PET = NTTPType->getAs<PackExpansionType>()) {\n        Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(*this,\n                                                           ArgumentPackIndex);\n        if (NeverCParameterTypeSource) {\n          auto NeverCPattern = NeverCParameterTypeSource->getTypeLoc()\n                                   .getAs<PackExpansionTypeLoc>();\n          if (NeverCPattern) {\n            auto NeverCPatternLoc = NeverCPattern.getPatternLoc();\n            NeverCParameterTypeSource = Context.CreateTypeSourceInfo(PET->getPattern());\n            NeverCParameterTypeSource->getTypeLoc().initializeFullCopy(NeverCPatternLoc);\n          } else {\n            NeverCParameterTypeSource = nullptr;\n          }\n        }\n        if (NeverCParameterTypeSource) {\n          NeverCParameterTypeSource = SubstType(NeverCParameterTypeSource, MLTAL,\n                                               NTTP->getLocation(), NTTP->getDeclName());\n          NTTPType = NeverCParameterTypeSource ? NeverCParameterTypeSource->getType() : QualType();\n        } else {\n          NTTPType = SubstType(PET->getPattern(), MLTAL, NTTP->getLocation(),\n                               NTTP->getDeclName());\n        }\n      } else {\n        if (NeverCParameterTypeSource) {\n          NeverCParameterTypeSource = SubstType(NeverCParameterTypeSource, MLTAL,\n                                               NTTP->getLocation(), NTTP->getDeclName());\n          NTTPType = NeverCParameterTypeSource ? NeverCParameterTypeSource->getType() : QualType();\n        } else {\n          NTTPType = SubstType(NTTPType, MLTAL, NTTP->getLocation(),\n                               NTTP->getDeclName());\n        }\n      }\n\n      // If that worked, check the non-type template parameter type\n      // for validity.\n      if (!NTTPType.isNull())\n        NTTPType = CheckNonTypeTemplateParameterType(NTTPType,\n                                                     NTTP->getLocation());\n      if (NTTPType.isNull())\n        return true;\n    }\n\n', '    QualType NTTPType = NTTP->getType();\n    TypeSourceInfo *NeverCParameterTypeSource =\n        Consumer.wantsNeverCTemplateSource() ? NTTP->getTypeSourceInfo() : nullptr;\n    if (NTTP->isParameterPack() && NTTP->isExpandedParameterPack())\n      NTTPType = NTTP->getExpansionType(ArgumentPackIndex);\n    if (Consumer.wantsNeverCTemplateSource() && NTTP->isParameterPack() &&\n        NTTP->isExpandedParameterPack())\n      NeverCParameterTypeSource = NTTP->getExpansionTypeSourceInfo(ArgumentPackIndex);\n\n    if (NTTPType->isInstantiationDependentType() &&\n        !isa<TemplateTemplateParmDecl>(Template) &&\n        !Template->getDeclContext()->isDependentContext()) {\n      // Do substitution on the type of the non-type template parameter.\n      InstantiatingTemplate Inst(*this, TemplateLoc, Template, NTTP,\n                                 CTAI.SugaredConverted,\n                                 SourceRange(TemplateLoc, RAngleLoc));\n      if (Inst.isInvalid())\n        return true;\n\n      MultiLevelTemplateArgumentList MLTAL(Template, CTAI.SugaredConverted,\n                                           /*Final=*/!Consumer.wantsNeverCTemplateSource());\n      // If the parameter is a pack expansion, expand this slice of the pack.\n      if (auto *PET = NTTPType->getAs<PackExpansionType>()) {\n        Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(*this,\n                                                           ArgumentPackIndex);\n        if (NeverCParameterTypeSource) {\n          auto NeverCPattern = NeverCParameterTypeSource->getTypeLoc()\n                                   .getAs<PackExpansionTypeLoc>();\n         /* Unexpected source drift. */ if (NeverCPattern) {\n            auto NeverCPatternLoc = NeverCPattern.getPatternLoc();\n            NeverCParameterTypeSource = Context.CreateTypeSourceInfo(PET->getPattern());\n            NeverCParameterTypeSource->getTypeLoc().initializeFullCopy(NeverCPatternLoc);\n          } else {\n            NeverCParameterTypeSource = nullptr;\n          }\n        }\n        if (NeverCParameterTypeSource) {\n          NeverCParameterTypeSource = SubstType(NeverCParameterTypeSource, MLTAL,\n                                               NTTP->getLocation(), NTTP->getDeclName());\n          NTTPType = NeverCParameterTypeSource ? NeverCParameterTypeSource->getType() : QualType();\n        } else {\n          NTTPType = SubstType(PET->getPattern(), MLTAL, NTTP->getLocation(),\n                               NTTP->getDeclName());\n        }\n      } else {\n        if (NeverCParameterTypeSource) {\n          NeverCParameterTypeSource = SubstType(NeverCParameterTypeSource, MLTAL,\n                                               NTTP->getLocation(), NTTP->getDeclName());\n          NTTPType = NeverCParameterTypeSource ? NeverCParameterTypeSource->getType() : QualType();\n        } else {\n          NTTPType = SubstType(NTTPType, MLTAL, NTTP->getLocation(),\n                               NTTP->getDeclName());\n        }\n      }\n\n      // If that worked, check the non-type template parameter type\n      // for validity.\n      if (!NTTPType.isNull())\n        NTTPType = CheckNonTypeTemplateParameterType(NTTPType,\n                                                     NTTP->getLocation());\n      if (NTTPType.isNull())\n        return true;\n    }\n\n', 1)),
                (1, "source missing anchor 14", explicit_source_original.replace('    case TemplateArgument::Pack:\n      llvm_unreachable("Caller must expand template argument packs");\n    }\n\n    return false;', "// Missing source anchor.", 1)),
                (1, "source drifted block 14", explicit_source_expected.replace('    case TemplateArgument::Pack:\n      llvm_unreachable("Caller must expand template argument packs");\n    }\n\n    if (Consumer.wantsNeverCTemplateSource())\n      CTAI.retainNeverCParameterType(NTTP, NeverCParameterTypeSource, ArgumentPackIndex);\n    return false;', '    case TemplateArgument::Pack:\n      llvm_unreachable("Caller must expand template argument packs");\n    }\n\n    if (Consumer.want/* Unexpected source drift. */sNeverCTemplateSource())\n      CTAI.retainNeverCParameterType(NTTP, NeverCParameterTypeSource, ArgumentPackIndex);\n    return false;', 1)),
                (2, "deduction missing anchor 0", explicit_deduction_original.replace('#include "clang/AST/ASTContext.h"', "// Missing source anchor.", 1)),
                (2, "deduction drifted block 0", explicit_deduction_expected.replace('#include "clang/AST/ASTConsumer.h"\n#include "clang/AST/ASTContext.h"', '#include "clang/AST/ASTConsumer.h"/* Unexpected source drift. */\n#include "clang/AST/ASTContext.h"', 1)),
                (2, "deduction missing anchor 1", explicit_deduction_original.replace('    // Check whether we can actually use the default argument.\n    if (S.CheckTemplateArgument(\n            Param, DefArg, TD, TD->getLocation(), TD->getSourceRange().getEnd(),\n            /*ArgumentPackIndex=*/0, CTAI, Sema::CTAK_Specified)) {\n      Info.Param = makeTemplateParameter(\n                         const_cast<NamedDecl *>(TemplateParams->getParam(I)));\n      // FIXME: These template arguments are temporary. Free them!\n      Info.reset(\n          TemplateArgumentList::CreateCopy(S.Context, CTAI.SugaredConverted),\n          TemplateArgumentList::CreateCopy(S.Context, CTAI.CanonicalConverted));\n      return TemplateDeductionResult::SubstitutionFailure;\n    }\n\n    // If we get here, we successfully used the default template argument.', "// Missing source anchor.", 1)),
                (2, "deduction drifted block 1", explicit_deduction_expected.replace('    // Preserve spelling before CheckTemplateArgument adds conversions.\n    const auto NeverCWrittenDefault = DefArg;\n    // Check whether we can actually use the default argument.\n    if (S.CheckTemplateArgument(\n            Param, DefArg, TD, TD->getLocation(), TD->getSourceRange().getEnd(),\n            /*ArgumentPackIndex=*/0, CTAI, Sema::CTAK_Specified)) {\n      Info.Param = makeTemplateParameter(\n                         const_cast<NamedDecl *>(TemplateParams->getParam(I)));\n      // FIXME: These template arguments are temporary. Free them!\n      Info.reset(\n          TemplateArgumentList::CreateCopy(S.Context, CTAI.SugaredConverted),\n          TemplateArgumentList::CreateCopy(S.Context, CTAI.CanonicalConverted));\n      return TemplateDeductionResult::SubstitutionFailure;\n    }\n\n    if (S.getASTConsumer().wantsNeverCTemplateSource()) {\n      if (isa<TemplateTypeParmDecl, NonTypeTemplateParmDecl>(Param))\n        CTAI.retainNeverCDefault(Param, NeverCWrittenDefault, DefArg);\n    } else {\n      if (auto *NeverCParameter = dyn_cast<NonTypeTemplateParmDecl>(Param))\n        S.getASTConsumer().HandleNeverCScalarTemplateDefault(\n            TD, NeverCParameter, NeverCWrittenDefault, DefArg,\n            CTAI.CanonicalConverted.back(), TD->getLocation());\n    }\n    // If we get here, we successfully used the default template argument.', '    // Preserve spelling before CheckTemplateArgument adds conversions.\n    const auto NeverCWrittenDefault = DefArg;\n    // Check whether we can actually use the default argument.\n    if (S.CheckTemplateArgument(\n            Param, DefArg, TD, TD->getLocation(), TD->getSourceRange().getEnd(),\n            /*ArgumentPackIndex=*/0, CTAI, Sema::CTAK_Specified)) {\n      Info.Param = makeTemplateParameter(\n                         const_cast<NamedDecl *>(TemplateParams->getParam(I)));\n      // FIXME: These template arguments are temporary. Free them!\n      Info.reset(\n          TemplateArgumentList::CreateCopy(S.Context, CTAI.SugaredConverted),\n          TemplateArgumentL/* Unexpected source drift. */ist::CreateCopy(S.Context, CTAI.CanonicalConverted));\n      return TemplateDeductionResult::SubstitutionFailure;\n    }\n\n    if (S.getASTConsumer().wantsNeverCTemplateSource()) {\n      if (isa<TemplateTypeParmDecl, NonTypeTemplateParmDecl>(Param))\n        CTAI.retainNeverCDefault(Param, NeverCWrittenDefault, DefArg);\n    } else {\n      if (auto *NeverCParameter = dyn_cast<NonTypeTemplateParmDecl>(Param))\n        S.getASTConsumer().HandleNeverCScalarTemplateDefault(\n            TD, NeverCParameter, NeverCWrittenDefault, DefArg,\n            CTAI.CanonicalConverted.back(), TD->getLocation());\n    }\n    // If we get here, we successfully used the default template argument.', 1)),
                (2, "deduction missing anchor 2", explicit_deduction_original.replace('    if (Inserted)\n      Pos->second.append(Info.diag_begin(), Info.diag_end());\n  }\n\n  return TemplateDeductionResult::Success;', "// Missing source anchor.", 1)),
                (2, "deduction drifted block 2", explicit_deduction_expected.replace('    if (Inserted)\n      Pos->second.append(Info.diag_begin(), Info.diag_end());\n  }\n\n  if (Consumer.wantsNeverCTemplateSource() && !IsIncomplete)\n    Consumer.HandleNeverCFunctionTemplateSource(\n        Specialization,\n        CTAI.CanonicalConverted.data(), CTAI.SugaredConverted.data(),\n        CTAI.CanonicalConverted.size(), CTAI.NeverCDefaultParameters.data(),\n        CTAI.NeverCWrittenDefaults.data(), CTAI.NeverCConvertedDefaults.data(),\n        CTAI.NeverCDefaultParameters.size(), CTAI.NeverCTypeParameters.data(),\n        CTAI.NeverCParameterTypes.data(), CTAI.NeverCParameterPackIndices.data(),\n        CTAI.NeverCTypeParameters.size(), CTAI.NeverCDefaultsOverflow, Info.getLocation());\n  return TemplateDeductionResult::Success;', '    if (Inserted)\n      Pos->second.append(Info.diag_begin(), Info.diag_end());\n  }\n\n  if (Consumer.wantsNeverCTemplateSource() && !IsIncomplete)\n    Consumer.HandleNeverCFunctionTemplateSource(\n        Specialization,\n        CTAI.CanonicalConverted.data(), CTAI.SugaredConverted.data(),\n        CTAI.CanonicalConverted.size(), CTAI.NeverCDefaultParameters.data(),\n    /* Unexpected source drift. */    CTAI.NeverCWrittenDefaults.data(), CTAI.NeverCConvertedDefaults.data(),\n        CTAI.NeverCDefaultParameters.size(), CTAI.NeverCTypeParameters.data(),\n        CTAI.NeverCParameterTypes.data(), CTAI.NeverCParameterPackIndices.data(),\n        CTAI.NeverCTypeParameters.size(), CTAI.NeverCDefaultsOverflow, Info.getLocation());\n  return TemplateDeductionResult::Success;', 1)),
                (2, "deduction missing anchor 3", explicit_deduction_original.replace('      MultiLevelTemplateArgumentList Args(Template, CTAI.SugaredConverted,\n                                          /*Final=*/true);\n\n      if (auto *NTTP = dyn_cast<NonTypeTemplateParmDecl>(Param)) {\n        Sema::InstantiatingTemplate Inst(S, Template->getLocation(), Template,\n                                         NTTP, CTAI.SugaredConverted,\n                                         Template->getSourceRange());\n        if (Inst.isInvalid() ||\n            S.SubstType(NTTP->getType(), Args, NTTP->getLocation(),\n                        NTTP->getDeclName()).isNull())\n          return true;\n      } else if (auto *TTP = dyn_cast<TemplateTemplateParmDecl>(Param)) {\n        Sema::InstantiatingTemplate Inst(S, Template->getLocation(), Template,\n                                         TTP, CTAI.SugaredConverted,\n                                         Template->getSourceRange());\n        if (Inst.isInvalid() || !S.SubstDecl(TTP, S.CurContext, Args))\n          return true;\n      }\n', "// Missing source anchor.", 1)),
                (2, "deduction drifted block 3", explicit_deduction_expected.replace('      MultiLevelTemplateArgumentList Args(Template, CTAI.SugaredConverted,\n                                          /*Final=*/!(S.getASTConsumer().wantsNeverCTemplateSource() &&\n                                                      isa<NonTypeTemplateParmDecl>(Param)));\n\n      if (auto *NTTP = dyn_cast<NonTypeTemplateParmDecl>(Param)) {\n        Sema::InstantiatingTemplate Inst(S, Template->getLocation(), Template,\n                                         NTTP, CTAI.SugaredConverted,\n                                         Template->getSourceRange());\n        if (Inst.isInvalid())\n          return true;\n        auto *NeverCEmptyParameterSource = S.getASTConsumer().wantsNeverCTemplateSource()\n                                              ? NTTP->getTypeSourceInfo() : nullptr;\n        if (NeverCEmptyParameterSource) {\n          NeverCEmptyParameterSource = S.SubstType(NeverCEmptyParameterSource, Args,\n                                                   NTTP->getLocation(), NTTP->getDeclName());\n          if (!NeverCEmptyParameterSource)\n            return true;\n        } else if (S.SubstType(NTTP->getType(), Args, NTTP->getLocation(),\n                               NTTP->getDeclName()).isNull()) {\n          return true;\n        }\n        if (S.getASTConsumer().wantsNeverCTemplateSource())\n          CTAI.retainNeverCParameterType(NTTP, NeverCEmptyParameterSource, ~0u);\n      } else if (auto *TTP = dyn_cast<TemplateTemplateParmDecl>(Param)) {\n        Sema::InstantiatingTemplate Inst(S, Template->getLocation(), Template,\n                                         TTP, CTAI.SugaredConverted,\n                                         Template->getSourceRange());\n        if (Inst.isInvalid() || !S.SubstDecl(TTP, S.CurContext, Args))\n          return true;\n      }\n', '      MultiLevelTemplateArgumentList Args(Template, CTAI.SugaredConverted,\n                                          /*Final=*/!(S.getASTConsumer().wantsNeverCTemplateSource() &&\n                                                      isa<NonTypeTemplateParmDecl>(Param)));\n\n      if (auto *NTTP = dyn_cast<NonTypeTemplateParmDecl>(Param)) {\n        Sema::InstantiatingTemplate Inst(S, Template->getLocation(), Template,\n                                         NTTP, CTAI.SugaredConverted,\n                                         Template->getSourceRange());\n        if (Inst.isInvalid())\n          return true;\n        auto *NeverCEmptyParameterSource = S.getASTConsumer().wantsNeverCTemplateSource()\n                                              ? NTTP->getTypeSourceInfo() : nullptr;\n        if (NeverCEmptyParameterSource) {\n          NeverCEmptyParameterSource = S.SubstType(NeverCEmpty/* Unexpected source drift. */ParameterSource, Args,\n                                                   NTTP->getLocation(), NTTP->getDeclName());\n          if (!NeverCEmptyParameterSource)\n            return true;\n        } else if (S.SubstType(NTTP->getType(), Args, NTTP->getLocation(),\n                               NTTP->getDeclName()).isNull()) {\n          return true;\n        }\n        if (S.getASTConsumer().wantsNeverCTemplateSource())\n          CTAI.retainNeverCParameterType(NTTP, NeverCEmptyParameterSource, ~0u);\n      } else if (auto *TTP = dyn_cast<TemplateTemplateParmDecl>(Param)) {\n        Sema::InstantiatingTemplate Inst(S, Template->getLocation(), Template,\n                                         TTP, CTAI.SugaredConverted,\n                                         Template->getSourceRange());\n        if (Inst.isInvalid() || !S.SubstDecl(TTP, S.CurContext, Args))\n          return true;\n      }\n', 1)),
                (2, "deduction missing anchor 4", explicit_deduction_original.replace('    TemplateDeductionInfo &Info) {\n  FunctionDecl *Function = FunctionTemplate->getTemplatedDecl();', "// Missing source anchor.", 1)),
                (2, "deduction drifted block 4", explicit_deduction_expected.replace('    TemplateDeductionInfo &Info) {\n  if (Consumer.wantsNeverCTemplateSource())\n    Info.clearNeverCExplicitSource();\n  FunctionDecl *Function = FunctionTemplate->getTemplatedDecl();', '    TemplateDeductionInfo &Info) {\n  if (Consumer.wantsNeverCTemplateSource())\n    Info.cl/* Unexpected source drift. */earNeverCExplicitSource();\n  FunctionDecl *Function = FunctionTemplate->getTemplatedDecl();', 1)),
                (2, "deduction missing anchor 5", explicit_deduction_original.replace('  Info.setExplicitArgs(SugaredExplicitArgumentList,\n                       CanonicalExplicitArgumentList);', "// Missing source anchor.", 1)),
                (2, "deduction drifted block 5", explicit_deduction_expected.replace('  Info.setExplicitArgs(SugaredExplicitArgumentList,\n                       CanonicalExplicitArgumentList);\n  if (Consumer.wantsNeverCTemplateSource()) {\n    Info.NeverCExplicitSourceTemplate = FunctionTemplate;\n    Info.NeverCExplicitTypeParameters = CTAI.NeverCTypeParameters;\n    Info.NeverCExplicitParameterTypes = CTAI.NeverCParameterTypes;\n    Info.NeverCExplicitPackIndices = CTAI.NeverCParameterPackIndices;\n    Info.NeverCExplicitSourceOverflow = CTAI.NeverCDefaultsOverflow;\n  }', '  Info.setExplicitArgs(SugaredExplicitArgumentList,\n                       CanonicalExplicitArgumentList);\n  if (Consumer.wantsNeverCTemplateSource()) {\n    Info.NeverCExplicitSourceTemplate = FunctionTemplate;\n    Info.NeverCExplicitTypeParam/* Unexpected source drift. */eters = CTAI.NeverCTypeParameters;\n    Info.NeverCExplicitParameterTypes = CTAI.NeverCParameterTypes;\n    Info.NeverCExplicitPackIndices = CTAI.NeverCParameterPackIndices;\n    Info.NeverCExplicitSourceOverflow = CTAI.NeverCDefaultsOverflow;\n  }', 1)),
                (2, "deduction missing anchor 6", explicit_deduction_original.replace('          CTAI.CanonicalConverted.push_back(\n              S.Context.getCanonicalTemplateArgument(Deduced[I]));\n          continue;', "// Missing source anchor.", 1)),
                (2, "deduction drifted block 6", explicit_deduction_expected.replace('          CTAI.CanonicalConverted.push_back(\n              S.Context.getCanonicalTemplateArgument(Deduced[I]));\n          if (S.getASTConsumer().wantsNeverCTemplateSource() &&\n              isa<NonTypeTemplateParmDecl>(Param)) {\n            if (Info.NeverCExplicitSourceTemplate != dyn_cast<FunctionTemplateDecl>(Template)) {\n              CTAI.NeverCDefaultsOverflow = true;\n            } else {\n              CTAI.NeverCDefaultsOverflow |= Info.NeverCExplicitSourceOverflow;\n              for (unsigned E = 0; E < Info.NeverCExplicitTypeParameters.size(); ++E)\n                if (const auto *NeverCExplicitParameter = dyn_cast<NonTypeTemplateParmDecl>(\n                        Info.NeverCExplicitTypeParameters[E]);\n                    NeverCExplicitParameter && NeverCExplicitParameter->getIndex() == I)\n                  CTAI.retainNeverCParameterType(Info.NeverCExplicitTypeParameters[E],\n                                                Info.NeverCExplicitParameterTypes[E],\n                                                Info.NeverCExplicitPackIndices[E]);\n            }\n          }\n          continue;', '          CTAI.CanonicalConverted.push_back(\n              S.Context.getCanonicalTemplateArgument(Deduced[I]));\n          if (S.getASTConsumer().wantsNeverCTemplateSource() &&\n              isa<NonTypeTemplateParmDecl>(Param)) {\n            if (Info.NeverCExplicitSourceTemplate != dyn_cast<FunctionTemplateDecl>(Template)) {\n              CTAI.NeverCDefaultsOverflow = true;\n            } else {\n              CTAI.NeverCDefaultsOverflow |= Info.NeverCExplicitSourceOverflow;\n              for (unsigned E = 0; E < Info.NeverCExplicitTypeParameters.size(/* Unexpected source drift. */); ++E)\n                if (const auto *NeverCExplicitParameter = dyn_cast<NonTypeTemplateParmDecl>(\n                        Info.NeverCExplicitTypeParameters[E]);\n                    NeverCExplicitParameter && NeverCExplicitParameter->getIndex() == I)\n                  CTAI.retainNeverCParameterType(Info.NeverCExplicitTypeParameters[E],\n                                                Info.NeverCExplicitParameterTypes[E],\n                                                Info.NeverCExplicitPackIndices[E]);\n            }\n          }\n          continue;', 1)),
                (3, "sema missing anchor 0", explicit_sema_original.replace('    SmallVector<TemplateArgument, 4> SugaredConverted, CanonicalConverted;\n', "// Missing source anchor.", 1)),
                (3, "sema drifted block 0", explicit_sema_expected.replace('    SmallVector<TemplateArgument, 4> SugaredConverted, CanonicalConverted;\n\n    // Keep defaults attached to this deduction, including ignored type args.\n    SmallVector<NamedDecl *, 4> NeverCDefaultParameters;\n    SmallVector<TemplateArgumentLoc, 4> NeverCWrittenDefaults, NeverCConvertedDefaults;\n    bool NeverCDefaultsOverflow = false;\n\n    void retainNeverCDefault(NamedDecl *Parameter,\n                            const TemplateArgumentLoc &Written,\n                            const TemplateArgumentLoc &Converted) {\n      if (NeverCDefaultParameters.size() == 64) {\n        NeverCDefaultsOverflow = true;\n        return;\n      }\n      NeverCDefaultParameters.push_back(Parameter);\n      NeverCWrittenDefaults.push_back(Written);\n      NeverCConvertedDefaults.push_back(Converted);\n    }\n\n    SmallVector<NamedDecl *, 4> NeverCTypeParameters;\n    SmallVector<TypeSourceInfo *, 4> NeverCParameterTypes;\n    SmallVector<unsigned, 4> NeverCParameterPackIndices;\n\n    void retainNeverCParameterType(NamedDecl *Parameter, TypeSourceInfo *Source,\n                                  unsigned PackIndex) {\n      if (NeverCTypeParameters.size() == 4096) {\n        NeverCDefaultsOverflow = true;\n        return;\n      }\n      NeverCTypeParameters.push_back(Parameter);\n      NeverCParameterTypes.push_back(Source);\n      NeverCParameterPackIndices.push_back(PackIndex);\n    }\n', '    SmallVector<TemplateArgument, 4> SugaredConverted, CanonicalConverted;\n\n    // Keep defaults attached to this deduction, including ignored type args.\n    SmallVector<NamedDecl *, 4> NeverCDefaultParameters;\n    SmallVector<TemplateArgumentLoc, 4> NeverCWrittenDefaults, NeverCConvertedDefaults;\n    bool NeverCDefaultsOverflow = false;\n\n    void retainNeverCDefault(NamedDecl *Parameter,\n                            const TemplateArgumentLoc &Written,\n                            const TemplateArgumentLoc &Converted) {\n      if (NeverCDefaultParameters.size() == 64) {\n        NeverCDefaultsOverflow = true;\n        return;\n      }\n      NeverCDefaultParameters.push_back(Parameter/* Unexpected source drift. */);\n      NeverCWrittenDefaults.push_back(Written);\n      NeverCConvertedDefaults.push_back(Converted);\n    }\n\n    SmallVector<NamedDecl *, 4> NeverCTypeParameters;\n    SmallVector<TypeSourceInfo *, 4> NeverCParameterTypes;\n    SmallVector<unsigned, 4> NeverCParameterPackIndices;\n\n    void retainNeverCParameterType(NamedDecl *Parameter, TypeSourceInfo *Source,\n                                  unsigned PackIndex) {\n      if (NeverCTypeParameters.size() == 4096) {\n        NeverCDefaultsOverflow = true;\n        return;\n      }\n      NeverCTypeParameters.push_back(Parameter);\n      NeverCParameterTypes.push_back(Source);\n      NeverCParameterPackIndices.push_back(PackIndex);\n    }\n', 1)),
                (3, "sema duplicate file", explicit_sema_expected * 2),
                (4, "deduction_info missing anchor 0", explicit_deduction_info_original.replace('public:\n  TemplateDeductionInfo(SourceLocation Loc, unsigned DeducedDepth = 0)', "// Missing source anchor.", 1)),
                (4, "deduction_info drifted block 0", explicit_deduction_info_expected.replace('public:\n  // NeverC keeps preliminary explicit conversions on this exact candidate.\n  // reset/take retain it; a new explicit-substitution invocation clears it.\n  FunctionTemplateDecl *NeverCExplicitSourceTemplate = nullptr;\n  SmallVector<NamedDecl *, 4> NeverCExplicitTypeParameters;\n  SmallVector<TypeSourceInfo *, 4> NeverCExplicitParameterTypes;\n  SmallVector<unsigned, 4> NeverCExplicitPackIndices;\n  bool NeverCExplicitSourceOverflow = false;\n\n  void clearNeverCExplicitSource() {\n    NeverCExplicitSourceTemplate = nullptr;\n    NeverCExplicitTypeParameters.clear();\n    NeverCExplicitParameterTypes.clear();\n    NeverCExplicitPackIndices.clear();\n    NeverCExplicitSourceOverflow = false;\n  }\n\n  TemplateDeductionInfo(SourceLocation Loc, unsigned DeducedDepth = 0)', 'public:\n  // NeverC keeps preliminary explicit conversions on this exact candidate.\n  // reset/take retain it; a new explicit-substitution invocation clears it.\n  FunctionTemplateDecl *NeverCExplicitSourceTemplate = nullptr;\n  SmallVector<NamedDecl *, 4> NeverCExplicitTypeParameters;\n  SmallVector<TypeSourceInfo *, 4> NeverCExplicitParameterTypes;\n  SmallVector<unsigned, 4> NeverCEx/* Unexpected source drift. */plicitPackIndices;\n  bool NeverCExplicitSourceOverflow = false;\n\n  void clearNeverCExplicitSource() {\n    NeverCExplicitSourceTemplate = nullptr;\n    NeverCExplicitTypeParameters.clear();\n    NeverCExplicitParameterTypes.clear();\n    NeverCExplicitPackIndices.clear();\n    NeverCExplicitSourceOverflow = false;\n  }\n\n  TemplateDeductionInfo(SourceLocation Loc, unsigned DeducedDepth = 0)', 1)),
                (4, "deduction_info duplicate file", explicit_deduction_info_expected * 2),
                (0, "body source pattern lost const", explicit_header_expected.replace(
                    "FunctionDecl *, FunctionTemplateDecl *, const FunctionDecl *, DeclContext *) {}",
                    "FunctionDecl *, FunctionTemplateDecl *, FunctionDecl *, DeclContext *) {}", 1)),
                (0, "orphan HandleNeverCClassTemplateSource", explicit_header_expected + "// HandleNeverCClassTemplateSource\n"),
                (1, "orphan HandleNeverCExplicitFunctionInstantiation", explicit_source_expected + "// HandleNeverCExplicitFunctionInstantiation\n"),
                (2, "orphan HandleNeverCExplicitStaticDataInstantiation", explicit_deduction_expected + "// HandleNeverCExplicitStaticDataInstantiation\n"),
                (3, "orphan HandleNeverCFunctionSpecializationSource", explicit_sema_expected + "// HandleNeverCFunctionSpecializationSource\n"),
                (4, "orphan HandleNeverCFunctionTemplateSource", explicit_deduction_info_expected + "// HandleNeverCFunctionTemplateSource\n"),
                (0, "orphan HandleNeverCScalarTemplateDefault", explicit_header_expected + "// HandleNeverCScalarTemplateDefault\n"),
                (1, "orphan HandleNeverCTemplateTypeSource", explicit_source_expected + "// HandleNeverCTemplateTypeSource\n"),
                (2, "orphan NeverCAliasSource", explicit_deduction_expected + "// NeverCAliasSource\n"),
                (3, "orphan NeverCConvertedDefaults", explicit_sema_expected + "// NeverCConvertedDefaults\n"),
                (4, "orphan NeverCDefaultParameters", explicit_deduction_info_expected + "// NeverCDefaultParameters\n"),
                (0, "orphan NeverCDefaultsOverflow", explicit_header_expected + "// NeverCDefaultsOverflow\n"),
                (1, "orphan NeverCEmptyParameterSource", explicit_source_expected + "// NeverCEmptyParameterSource\n"),
                (2, "orphan NeverCExplicitPackIndices", explicit_deduction_expected + "// NeverCExplicitPackIndices\n"),
                (3, "orphan NeverCExplicitParameter", explicit_sema_expected + "// NeverCExplicitParameter\n"),
                (4, "orphan NeverCExplicitParameterTypes", explicit_deduction_info_expected + "// NeverCExplicitParameterTypes\n"),
                (0, "orphan NeverCExplicitSourceOverflow", explicit_header_expected + "// NeverCExplicitSourceOverflow\n"),
                (1, "orphan NeverCExplicitSourceTemplate", explicit_source_expected + "// NeverCExplicitSourceTemplate\n"),
                (2, "orphan NeverCExplicitTypeParameters", explicit_deduction_expected + "// NeverCExplicitTypeParameters\n"),
                (3, "orphan NeverCParameter", explicit_sema_expected + "// NeverCParameter\n"),
                (4, "orphan NeverCParameterPackIndices", explicit_deduction_info_expected + "// NeverCParameterPackIndices\n"),
                (0, "orphan NeverCParameterTypeSource", explicit_header_expected + "// NeverCParameterTypeSource\n"),
                (1, "orphan NeverCParameterTypes", explicit_source_expected + "// NeverCParameterTypes\n"),
                (2, "orphan NeverCPattern", explicit_deduction_expected + "// NeverCPattern\n"),
                (3, "orphan NeverCPatternLoc", explicit_sema_expected + "// NeverCPatternLoc\n"),
                (4, "orphan NeverCTypeParameters", explicit_deduction_info_expected + "// NeverCTypeParameters\n"),
                (0, "orphan NeverCTypeResult", explicit_header_expected + "// NeverCTypeResult\n"),
                (1, "orphan NeverCWrittenAttributes", explicit_source_expected + "// NeverCWrittenAttributes\n"),
                (2, "orphan NeverCWrittenDefault", explicit_deduction_expected + "// NeverCWrittenDefault\n"),
                (3, "orphan NeverCWrittenDefaults", explicit_sema_expected + "// NeverCWrittenDefaults\n"),
                (4, "orphan clearNeverCExplicitSource", explicit_deduction_info_expected + "// clearNeverCExplicitSource\n"),
                (0, "orphan retainNeverCDefault", explicit_header_expected + "// retainNeverCDefault\n"),
                (1, "orphan retainNeverCParameterType", explicit_source_expected + "// retainNeverCParameterType\n"),
                (2, "orphan wantsNeverCTemplateSource", explicit_deduction_expected + "// wantsNeverCTemplateSource\n"),
            ]
            for index, state, contents in template_use_states:
                with self.subTest(template_use_source_state=state):
                    for path, expected in zip(explicit_paths, explicit_expected):
                        path.write_text(expected, encoding="utf-8")
                    explicit_paths[index].write_text(contents, encoding="utf-8")
                    untouched = snapshot_all_files()
                    run_script(False, "Unexpected pinned Clang explicit-instantiation source in " + str(explicit_paths[index]))
                    self.assertEqual(snapshot_all_files(), untouched, state)
            for missing in explicit_paths:
                for path, original in zip(explicit_paths, explicit_original):
                    path.write_text(original, encoding="utf-8")
                missing.unlink()
                untouched = snapshot_all_files()
                run_script(False, "Unexpected pinned Clang explicit-instantiation source in " + str(missing))
                self.assertEqual(snapshot_all_files(), untouched)
            for path, original in zip(explicit_paths, explicit_original):
                path.write_text(original, encoding="utf-8")
            run_script(True)
            for path, expected in zip(explicit_paths, explicit_expected):
                self.assertEqual(path.read_text(encoding="utf-8"), expected)
            run_script(True)
            for path, contents in stable.items():
                self.assertEqual(path.read_bytes(), contents, str(path))

            # The header and definition form one checked patch state. Neither
            # may change on failure, including a missing second source file.
            default_states = {
                "original header only": (original_default_header, rewritten_default_source),
                "original source only": (rewritten_default_header, original_default_source),
                "duplicate header": (original_default_header * 2, original_default_source),
                "duplicate source": (original_default_header, original_default_source * 2),
                "missing header anchor": ("// No accessor.\n", original_default_source),
                "missing source anchor": (original_default_header, "// No anchor.\n"),
                "partial implementation": (rewritten_default_header, rewritten_default_source.replace(
                    "Later->getMinRequiredArguments() < Required", "true", 1)),
                "extra definition": (rewritten_default_header, rewritten_default_source +
                                     "\nNamedDecl *UsingShadowDecl::getTargetDecl() const {}\n"),
                "extra marker": (original_default_header,
                                 original_default_source + "// NeverC C++17 imported defaults\n"),
            }
            for state, (header_text, source_text) in default_states.items():
                with self.subTest(imported_default_state=state):
                    default_header.write_text(header_text, encoding="utf-8")
                    default_source.write_text(source_text, encoding="utf-8")
                    untouched = snapshot_all_files()
                    run_script(False, "Unexpected pinned Clang imported default source pair")
                    self.assertEqual(snapshot_all_files(), untouched, state)
            for missing in (default_header, default_source):
                default_header.write_text(original_default_header, encoding="utf-8")
                default_source.write_text(original_default_source, encoding="utf-8")
                missing.unlink()
                untouched = snapshot_all_files()
                run_script(False, "Unexpected pinned Clang imported default source pair")
                self.assertEqual(snapshot_all_files(), untouched)
            default_header.write_text(original_default_header, encoding="utf-8")
            default_source.write_text(original_default_source, encoding="utf-8")
            run_script(True)
            for path, contents in stable.items():
                self.assertEqual(path.read_bytes(), contents, str(path))

            friend_context_states = {
                "missing block": "",
                "duplicate original": friend_context_original * 2,
                "duplicate rewritten": friend_context_expected * 2,
                "mixed blocks": friend_context_original + friend_context_expected,
                "lost compatibility": friend_context_expected.replace(
                    "if (!Template->isCompatibleWithDefinition())", "if (false)", 1),
                "lost friend check": friend_context_expected.replace(
                    "if (Template->getFriendObjectKind())", "if (true)", 1),
                "lost concrete owner": friend_context_expected.replace(
                    " && !Lexical->isDependentContext()", "", 1),
                "wrong owner": friend_context_expected.replace(
                    "DC = NeverCFriendContext;", "DC = FD->getDeclContext();", 1),
                "orphan marker": friend_context_original + "\n// NeverCFriendContext\n",
            }
            for state, contents in friend_context_states.items():
                with self.subTest(visible_friend_context_state=state):
                    explicit_paths[5].write_text(explicit_instantiate_expected.replace(
                        friend_context_expected, contents, 1), encoding="utf-8")
                    untouched = snapshot_all_files()
                    run_script(False, "Unexpected pinned Clang visible friend context source in " +
                               str(explicit_paths[5]))
                    self.assertEqual(snapshot_all_files(), untouched, state)
            explicit_paths[5].write_text(explicit_instantiate_expected.replace(
                friend_context_expected, friend_context_original, 1), encoding="utf-8")
            run_script(True)
            for path, contents in stable.items():
                self.assertEqual(path.read_bytes(), contents, str(path))

            member_pattern_states = {
                "missing block": "",
                "duplicate original": member_pattern_original * 2,
                "duplicate rewritten": member_pattern_expected * 2,
                "mixed blocks": member_pattern_original + member_pattern_expected,
                "primary still uses origin": member_pattern_expected.replace(
                    "if (CTD->isMemberSpecialization())", "if (NewCTD->isMemberSpecialization())", 1),
                "partial still uses origin": member_pattern_expected.replace(
                    "if (CTPSD->isMemberSpecialization())", "if (NewCTPSD->isMemberSpecialization())", 1),
                "primary origin lost": member_pattern_expected.replace("CTD = NewCTD;", "", 1),
                "partial origin lost": member_pattern_expected.replace("CTPSD = NewCTPSD;", "", 1),
                "orphan marker": member_pattern_original + "\n// NeverC member class body lookup\n",
            }
            for state, contents in member_pattern_states.items():
                with self.subTest(member_class_pattern_state=state):
                    default_source.write_text(rewritten_default_source.replace(
                        member_pattern_expected, contents, 1), encoding="utf-8")
                    untouched = snapshot_all_files()
                    run_script(False, "Unexpected pinned Clang member class pattern source in " +
                               str(default_source))
                    self.assertEqual(snapshot_all_files(), untouched, state)
            default_source.write_text(rewritten_default_source.replace(
                member_pattern_expected, member_pattern_original, 1), encoding="utf-8")
            run_script(True)
            for path, contents in stable.items():
                self.assertEqual(path.read_bytes(), contents, str(path))

            missing_use = original_loop.replace("static PointerBounds expandBounds(",
                                                "static int expandBounds(", 1)
            invalid = {
                "missing declaration": uses,
                "duplicate original": original_record + "\n" + original_loop,
                "duplicate rewritten": renamed_record + "\n" + expected_loop,
                "mixed declarations": original_record + "\n" + expected_loop,
                "partial rename": original_loop.replace(
                    "struct PointerBounds", "struct neverc_cpp_PointerBounds", 1),
                "changed member": original_loop.replace(
                    "Value *StrideToCheck;", "Value *Changed;", 1),
                "extra use": original_loop + "PointerBounds Unexpected;\n",
                "missing use": missing_use,
                # Preserve the count of six uses so these exercise the specific
                # declaration/alias/macro checks rather than just the count.
                "unknown declaration": missing_use + "struct [[nodiscard]] PointerBounds;\n",
                "extra alias": missing_use + "using PointerBounds = Unrelated;\n",
                "extra macro": missing_use + "#define PointerBounds Unrelated\n",
                "unexpected private name": original_loop + "neverc_cpp_PointerBounds *Unexpected;\n",
            }
            for name, contents in invalid.items():
                with self.subTest(state=name):
                    before = contents.encode("utf-8")
                    loop.write_bytes(before)
                    run_script(False)
                    self.assertEqual(loop.read_bytes(), before)

            # The preceding negatives leave an invalid LoopUtils.cpp. Restore
            # it so no PointerBounds error can hide a math validation failure.
            loop.write_text(expected_loop, encoding="utf-8")
            # Interrupted runs may leave whole files rewritten independently.
            # Original/all-rewritten states were checked above; exercise the
            # other six combinations of the three files as valid inputs.
            for rewritten_mask in range(1, 7):
                with self.subTest(math_rewritten_files=rewritten_mask):
                    for index, (name, path) in enumerate(math_paths.items()):
                        contents = (expected_math[name] if rewritten_mask & (1 << index)
                                    else math_sources[name])
                        path.write_text(contents, encoding="utf-8")
                    run_script(True)
                    for path, contents in stable.items():
                        self.assertEqual(path.read_bytes(), contents, str(path))

            for name, calls in math_calls.items():
                for before, after, partial in calls:
                    original, rewritten = math_sources[name], expected_math[name]
                    invalid_math = {
                        "missing call": original.replace(before, "0.0", 1),
                        "duplicate original": original + before + ";\n",
                        "duplicate rewritten": rewritten + after + ";\n",
                        "same call original and rewritten": original + after + ";\n",
                        "partial rewrite": original.replace(before, partial, 1),
                    }
                    if len(calls) > 1:
                        invalid_math.update({
                            "one call rewritten": original.replace(before, after, 1),
                            "one call original": rewritten.replace(after, before, 1),
                        })
                    for state, invalid_source in invalid_math.items():
                        with self.subTest(math_file=name, call=before, state=state):
                            for other_name, path in math_paths.items():
                                contents = (invalid_source if other_name == name
                                            else math_sources[other_name])
                                path.write_bytes(contents.encode("utf-8"))
                            unchanged = {path: path.read_bytes() for path in stable}
                            run_script(False, "Unexpected pinned LLVM math calls in " +
                                       str(math_paths[name]))
                            # Even when the last file is invalid, the earlier
                            # valid original math files must remain unmodified.
                            for path, contents in unchanged.items():
                                self.assertEqual(path.read_bytes(), contents, str(path))

    def test_global_pointer_bounds_record_identity_has_explicit_type_context(self):
        for record in ("PointerBounds", "neverc_cpp_PointerBounds"):
            cases = [
                ("?controlled@@", f"public: struct {record} & __cdecl "
                 f"{record}::operator=(struct {record} &&)"),
                ("?controlled@@", f"void __cdecl consume(struct {record} const &)"),
                ("?controlled@@", f"class {record} `RTTI Type Descriptor'"),
                ("_Zcontrolled", f"{record}::operator=({record}&&)"),
                ("_Zcontrolled", f"consume({record} const&)"),
                ("_Zcontrolled", f"consume({record}* const&)"),
                ("_Zcontrolled", f"consume({record} const* volatile* const&&)"),
                ("_Zcontrolled", f"typeinfo for {record}"),
                ("_Zcontrolled", f"Host::operator {record}() const"),
            ]
            for symbol, declaration in cases:
                with self.subTest(record=record, declaration=declaration):
                    self.assertTrue(AuditArchive.global_cpp_record_entity(
                        symbol, declaration, record))

    def test_global_record_identity_does_not_use_identifier_substrings(self):
        for record in ("PointerBounds", "neverc_cpp_PointerBounds"):
            for symbol in ("?controlled@@", "_Zcontrolled"):
                for declaration in (
                    f"Host::{record}::f()", f"struct Host::{record}",
                    f"Host::${record}::f()", f"More${record}::f()",
                    f"Ω{record}::f()", f"A\u0301{record}::f()", f"{record}Extra::f()",
                    f"void f(int {record})", f"{record}()", record,
                    f"void f<{record}>()", f"consume({record}&&&)",
                    f"consume({record}* constSuffix&)",
                ):
                    with self.subTest(symbol=symbol, declaration=declaration):
                        self.assertFalse(AuditArchive.global_cpp_record_entity(
                            symbol, declaration, record))

    def test_original_global_record_definitions_and_references_are_rejected(self):
        # Actual MSVC ARM64 984608 spelling from LoopUtils.cpp.obj. Its global
        # record contains version-specific LLVM TrackingVH<Value> members.
        msvc = "??4PointerBounds@@QEAAAEAU0@$$QEAU0@@Z"
        declaration = ("public: struct PointerBounds & __cdecl "
                       "PointerBounds::operator=(struct PointerBounds &&)")
        for raw, decoded in (
            (msvc, declaration),
            ("_ZN13PointerBoundsaSEOS_", "PointerBounds::operator=(PointerBounds&&)"),
        ):
            for kind in ("T", "W", "U"):
                with self.subTest(raw=raw, kind=kind):
                    with self.assertRaisesRegex(ValueError, "PointerBounds"):
                        self.audit_inventory([(raw, kind, decoded)])

    def test_private_global_record_methods_require_a_closed_definition(self):
        original = "??4PointerBounds@@QEAAAEAU0@$$QEAU0@@Z"
        renamed = original.replace("PointerBounds", "neverc_cpp_PointerBounds")
        original_decoded = ("public: struct PointerBounds & __cdecl "
                            "PointerBounds::operator=(struct PointerBounds &&)")
        renamed_decoded = original_decoded.replace(
            "PointerBounds", "neverc_cpp_PointerBounds")
        host = [(original, "T", original_decoded)]
        for host_format in ("nm", "coff-index"):
            with self.subTest(host_format=host_format):
                self.audit_inventory(
                    [(renamed, "T", renamed_decoded), (renamed, "U", renamed_decoded)],
                    host, host_format)
                with self.assertRaisesRegex(ValueError, "unresolved private dependency"):
                    self.audit_inventory([(renamed, "U", renamed_decoded)], host,
                                         host_format)
                # Isolating the global record must not hide an unresolved
                # method of the private LLVM value-handle implementation.
                handle = "?assign@ValueHandleBase@neverc_cpp_llvm@@QEAAXXZ"
                with self.assertRaisesRegex(ValueError, "unresolved private dependency"):
                    self.audit_inventory(
                        [(renamed, "T", renamed_decoded),
                         (handle, "U", "void __cdecl neverc_cpp_llvm::ValueHandleBase::assign(void)")],
                        host, host_format)

    def test_record_reference_closure_keeps_nested_host_types_separate(self):
        nested = "?f@neverc_cpp_PointerBounds@Host@@QEAAXXZ"
        self.audit_inventory(
            [(nested, "U", "void __cdecl Host::neverc_cpp_PointerBounds::f(void)")])
        raw = "_Z7consumeRKP24neverc_cpp_PointerBounds"
        with self.assertRaisesRegex(ValueError, "unresolved private dependency"):
            self.audit_inventory(
                [(raw, "U", "consume(neverc_cpp_PointerBounds* const&)")])

    def test_complete_failure_list_includes_count_and_private_symbol_identity(self):
        names = [f"host_collision_{index:03d}" for index in range(105)]
        names[0] = "?collision@private_detail@@YAXXZ"
        decoded = {name: name for name in names}
        decoded[names[0]] = "void __cdecl private_detail::collision(void)"
        private = [(name, "T", decoded[name]) for name in names]
        private.append((names[0], "U", decoded[names[0]]))
        host = [(name, "T", decoded[name]) for name in names]
        with mock.patch.object(sys, "stdout", new_callable=io.StringIO) as output:
            with self.assertRaises(ValueError) as failure:
                self.audit_inventory(private, host)
        message = str(failure.exception)
        self.assertIn("Unisolated symbols in builtin C++ frontend (total=105):", message)
        for name in names:
            self.assertIn("private/host symbol intersection: " + name + ";", message)
        self.assertIn("private_demangled='void __cdecl private_detail::collision(void)'; "
                      "private_definition=True; private_reference=True", message)
        evidence = output.getvalue()
        self.assertIn("ABI audit provenance: private nm archive='private.lib' "
                      "member_header='private.cpp.obj:' symbol='host_collision_104' "
                      "kind='T' raw='host_collision_104 T 0 0'", evidence)
        self.assertIn("ABI audit provenance: host nm archive='host.lib' "
                      "member_header='host.cpp.obj:' symbol='host_collision_104' "
                      "kind='T' raw='host_collision_104 T 0 0'", evidence)

    def test_failure_provenance_assigns_each_host_member_to_its_actual_archive(self):
        args = argparse.Namespace(nm="controlled-nm", nm_file=None,
                                  archive=Path("private.a"), prefix_header=None,
                                  host_lib_dir=Path("host-libs"), host_format="nm", host_nm=None)
        hosts = [Path("first.a"), Path("second.a"), Path("unrelated.a")]
        inventories = {
            args.archive: [("neverc_cpp_frontend_main", "T"),
                           ("shared_left", "U"), ("shared_right", "T")],
            hosts[0]: [("shared_left", "T")],
            hosts[1]: [("shared_right", "D")],
            hosts[2]: [("unrelated_symbol", "T")],
        }

        def inventory(_reader, paths, *options):
            rows = [(path, name, kind) for path in paths for name, kind in inventories[path]]
            if "--defined-only" in options:
                rows = [row for row in rows if not AuditArchive.is_undefined(row[2])]
            if "--format=posix" in options:
                # Equal member basenames in different archives must not be
                # attributed from the batch's filename-less header alone.
                return "".join(f"same-member.obj:\n{name} {kind} 1 2\n"
                               for _, name, kind in rows)
            return "".join(name + "\n" for _, name, _ in rows)

        with mock.patch.object(AuditArchive, "nm_output", side_effect=inventory) as reader, \
                mock.patch.object(AuditArchive, "host_archives", return_value=hosts), \
                mock.patch.object(sys, "stdout", new_callable=io.StringIO) as output:
            with self.assertRaisesRegex(ValueError, "total=2"):
                AuditArchive.audit(args)
        evidence = output.getvalue()
        self.assertIn("ABI audit provenance: host nm archive='first.a' "
                      "member_header='same-member.obj:' symbol='shared_left' "
                      "kind='T' raw='shared_left T 1 2'", evidence)
        self.assertIn("ABI audit provenance: host nm archive='second.a' "
                      "member_header='same-member.obj:' symbol='shared_right' "
                      "kind='D' raw='shared_right D 1 2'", evidence)
        self.assertNotIn("archive='unrelated.a'", evidence)
        self.assertNotIn("archive='first.a' member_header='same-member.obj:' "
                         "symbol='shared_right'", evidence)
        for archive in hosts[:2]:
            reader.assert_any_call("controlled-nm", [archive], "--format=posix")

    def test_coff_collision_provenance_never_invents_object_kind_or_member(self):
        with mock.patch.object(sys, "stdout", new_callable=io.StringIO) as output:
            with self.assertRaises(ValueError) as failure:
                self.audit_inventory([("HandleAbort", "U", "HandleAbort")],
                                     [("HandleAbort", "T", "HandleAbort")], "coff-index")
        self.assertIn("private_demangled='HandleAbort'; private_definition=False; "
                      "private_reference=True", str(failure.exception))
        evidence = output.getvalue()
        self.assertIn("ABI audit provenance: private nm archive='private.lib' "
                      "member_header='private.cpp.obj:' symbol='HandleAbort' "
                      "kind='U' raw='HandleAbort U 0 0'", evidence)
        self.assertIn("ABI audit provenance: host coff-index archive='host.lib' "
                      "symbol='HandleAbort' index-only; object kind, member and "
                      "raw nm row unavailable", evidence)
        self.assertNotIn("ABI audit provenance: host nm", evidence)
        for line in evidence.splitlines():
            if "ABI audit provenance: host coff-index" in line:
                self.assertNotIn("kind=", line)
                self.assertNotIn("member_header=", line)
                self.assertNotIn("raw=", line)

    def test_provenance_read_failure_preserves_the_original_complete_diagnostic(self):
        args = argparse.Namespace(nm="controlled-nm", nm_file=None,
                                  archive=Path("private.a"), prefix_header=None,
                                  host_lib_dir=Path("host-libs"), host_format="nm", host_nm=None)
        for change in ("read-error", "type-change"):
            with self.subTest(change=change):
                private_reads = 0

                def inventory(_reader, paths, *options):
                    nonlocal private_reads
                    if paths == [args.archive]:
                        if "--format=posix" in options:
                            private_reads += 1
                            if private_reads > 1 and change == "read-error":
                                raise OSError("evidence reader unavailable")
                            kind = "T" if private_reads > 1 else "U"
                            return ("private.obj:\nneverc_cpp_frontend_main T 0 0\n"
                                    f"blocked_symbol {kind} 0 0\n")
                        return "neverc_cpp_frontend_main\nblocked_symbol\n"
                    if "--format=posix" in options:
                        return "host.obj:\nblocked_symbol T 0 0\n"
                    return "blocked_symbol\n"

                with mock.patch.object(AuditArchive, "nm_output", side_effect=inventory), \
                        mock.patch.object(AuditArchive, "host_archives",
                                          return_value=[Path("host.a")]), \
                        mock.patch.object(sys, "stdout", new_callable=io.StringIO):
                    with self.assertRaises(ValueError) as failure:
                        AuditArchive.audit(args)
                message = str(failure.exception)
                self.assertIn("Unisolated symbols in builtin C++ frontend (total=1):", message)
                self.assertIn("private/host symbol intersection: blocked_symbol; "
                              "private_demangled='blocked_symbol'; private_definition=False; "
                              "private_reference=True", message)
                self.assertIn("ABI audit provenance collection failed:", message)
                self.assertIn("evidence reader unavailable" if change == "read-error" else
                              "Private symbol inventory changed", message)

    def test_microsoft_literal_requires_raw_identity_and_literal_decoding(self):
        # Wide and truncated spellings are from LLVM's ms-string-literals.test.
        for raw, decoded in (
                (self.llvm_literal[0], self.llvm_literal[2]),
                (self.clang_literal[0], self.clang_literal[2]),
                ('??_C@_01CNACBAHC@?$PP?$AA@', '"\\xFF"'),
                ('??_C@_13IIHIAFKH@?W?$PP?$AA?$AA@', 'L"\\xD7FF"'),
                ('??_C@_05OMLEGLOC@h?$AAi?$AA?$AA?$AA@', 'u"hi"'),
                ('??_C@_0M@GFNAJIPG@h?$AA?$AA?$AAi?$AA?$AA?$AA?$AA?$AA?$AA?$AA@',
                 'U"hi"'),
                ('??_C@_0CF@LABBIIMO@012345678901234567890123456789AB@',
                 '"012345678901234567890123456789AB"...')):
            with self.subTest(raw=raw):
                self.assertTrue(AuditArchive.microsoft_string_literal(raw, decoded))
                self.assertTrue(AuditArchive.standard_shared_symbol(raw, decoded))

    def test_quotes_or_literal_prefix_do_not_exempt_an_entity(self):
        for raw, decoded in (
                ("?call@llvm@@YAXXZ", '"llvm::"'),
                ("host_function", '"clang::"'),
                ("??_C@_host_function", '"llvm::"'),
                ("??_C@_06Q@llvm?3?3?$AA@", '"llvm::"'),
                (self.llvm_literal[0] + "suffix", self.llvm_literal[2]),
                (self.llvm_literal[0], "void __cdecl llvm::call(void)"),
                (self.llvm_literal[0], '"llvm::" unparsed'),
                (self.llvm_literal[0], '"llvm::unterminated'),
                (self.llvm_literal[0], '"llvm::\n"')):
            with self.subTest(raw=raw, decoded=decoded):
                self.assertFalse(AuditArchive.microsoft_string_literal(raw, decoded))
                self.assertFalse(AuditArchive.standard_shared_symbol(raw, decoded))

    def test_msvc_empty_literal_actual_pair_is_accepted(self):
        raw, _, decoded = self.msvc_empty_literal
        self.assertTrue(AuditArchive.microsoft_string_literal(raw, decoded))
        self.assertTrue(AuditArchive.standard_shared_symbol(raw, decoded))
        for host_format in ("nm", "coff-index"):
            with self.subTest(host_format=host_format):
                # The private R is observed; the matching host R is a controlled
                # model. COFF index mode makes no claim about the host kind.
                self.audit_inventory([self.msvc_empty_literal],
                                     [self.msvc_empty_literal], host_format)

    def test_msvc_empty_literal_requires_the_exact_zero_payload_raw_name(self):
        raw, _, decoded = self.msvc_empty_literal
        # Keep all mutations payload-free: an ordinary literal with a legal
        # encoded payload must still use the existing byte+ grammar.
        cases = (
            ("different-crc", "??_C@_00CNPNBAHD@@", decoded),
            ("different-length", "??_C@_01CNPNBAHC@@", decoded),
            ("wide-kind", "??_C@_10CNPNBAHC@@", decoded),
            ("raw-prefix", "prefix" + raw, decoded),
            ("raw-suffix", raw + "suffix", decoded),
            ("missing-terminator", raw[:-1], decoded),
            ("extra-terminator", raw + "@", decoded),
            ("extra-crc-terminator", "??_C@_00CNPNBAHC@@@@", decoded),
            ("host-raw-with-literal-decoding", "?call@Host@@YAXXZ", decoded),
            ("host-declaration-containing-literal", "?call@Host@@YAXXZ",
             'void __cdecl Host::call<""...>(void)'))
        for case, mutated_raw, mutated_decoded in cases:
            with self.subTest(case=case):
                self.assertFalse(AuditArchive.microsoft_string_literal(
                    mutated_raw, mutated_decoded))
                self.assertFalse(AuditArchive.standard_shared_symbol(
                    mutated_raw, mutated_decoded))
                for host_format in ("nm", "coff-index"):
                    with self.subTest(host_format=host_format):
                        row = (mutated_raw, "R", mutated_decoded)
                        with self.assertRaisesRegex(
                                ValueError, "private/host symbol intersection") as failure:
                            self.audit_inventory([row], [row], host_format)
                        self.assertIn("intersection: " + mutated_raw +
                                      "; private_demangled=", str(failure.exception))

    def test_msvc_empty_literal_requires_the_exact_decoding(self):
        raw, _, _ = self.msvc_empty_literal
        # Empty output, newline and NUL cannot represent one nm output row;
        # exercise those directly instead of turning a framing error into
        # supposed evidence that the intersection policy rejected the token.
        cases = (
            ("empty-output", "", False),
            ("missing-ellipsis", '""', True),
            ("wide-prefix", 'L""...', True),
            ("quoted-space", '" "', True),
            ("quoted-space-with-ellipsis", '" "...', True),
            ("escaped-nul", '"\\0"...', True),
            ("escaped-hex-nul", '"\\x00"...', True),
            ("actual-nul", '"\x00"...', False),
            ("newline", '"\n"...', False),
            ("carriage-return", '"\r"...', False),
            ("tab", '"\t"...', True),
            ("control-127", '"\x7f"...', True),
            ("leading-space", ' ""...', True),
            ("trailing-space", '""... ', True),
            ("suffix", '""...suffix', True))
        for case, decoded, line_safe in cases:
            with self.subTest(case=case):
                self.assertFalse(AuditArchive.microsoft_string_literal(raw, decoded))
                self.assertFalse(AuditArchive.standard_shared_symbol(raw, decoded))
                if line_safe:
                    for host_format in ("nm", "coff-index"):
                        with self.subTest(host_format=host_format):
                            row = (raw, "R", decoded)
                            with self.assertRaisesRegex(
                                    ValueError, "private/host symbol intersection") as failure:
                                self.audit_inventory([row], [row], host_format)
                            self.assertIn("intersection: " + raw +
                                          "; private_demangled=", str(failure.exception))

    def test_private_namespace_text_inside_literal_is_not_an_abi_entity(self):
        self.audit_inventory([self.llvm_literal, self.clang_literal])

    def test_host_namespace_text_and_identical_literal_comdats_are_accepted(self):
        self.audit_inventory([self.llvm_literal, self.clang_literal],
                             [self.llvm_literal, self.clang_literal])

    def test_coff_index_identical_literal_identity_is_accepted(self):
        self.audit_inventory([self.llvm_literal, self.clang_literal],
                             [self.llvm_literal, self.clang_literal], "coff-index")

    def test_real_private_llvm_entity_is_not_hidden_by_literal(self):
        for kind in ("T", "U"):
            with self.subTest(kind=kind):
                with self.assertRaisesRegex(ValueError, "llvm::call"):
                    self.audit_inventory([
                        self.llvm_literal,
                        ("?call@llvm@@YAXXZ", kind, "void __cdecl llvm::call(void)")])

    def test_real_host_clang_entity_is_not_hidden_by_literal(self):
        for host_format in ("nm", "coff-index"):
            with self.subTest(host_format=host_format):
                with self.assertRaisesRegex(ValueError, "unexpected host Clang"):
                    self.audit_inventory([self.clang_literal], [
                        self.clang_literal,
                        ("?call@clang@@YAXXZ", "T", "void __cdecl clang::call(void)")],
                        host_format)

    def test_quoted_nonliteral_shared_symbol_still_fails_intersection(self):
        with self.assertRaisesRegex(ValueError, "intersection: host_function"):
            self.audit_inventory([("host_function", "T", '"ordinary text"')],
                                 [("host_function", "T", '"ordinary text"')])

    def test_raw_decoded_inventory_length_mismatch_fails_closed(self):
        with mock.patch.object(AuditArchive, "nm_output", side_effect=[
                "raw_one\nraw_two\n", "decoded_one\n"]):
            with self.assertRaisesRegex(ValueError, "Inconsistent llvm-nm"):
                AuditArchive.decoded_symbols("controlled-nm", [Path("private.lib")])

    def test_proven_coff_alias_resolves_private_dependency(self):
        alias = "neverc_cpp_llvm_alias"
        reader = types.ModuleType("CoffWeakAliases")
        reader.read_resolved_aliases = mock.Mock(return_value={alias})
        with mock.patch.dict(sys.modules, {"CoffWeakAliases": reader}):
            self.audit_inventory([(alias, "w", alias)], coff_readobj="controlled-readobj")
        reader.read_resolved_aliases.assert_called_once_with(
            "controlled-readobj", Path("private.lib"), {"neverc_cpp_frontend_main"}, {alias})

    def test_msvc_runtime_constants_require_actual_definition_kinds(self):
        raw = "__real@3ff0000000000000"
        self.audit_inventory([(raw, "R", raw)], [(raw, "R", raw)], "coff-index")
        for private in ([(raw, "T", raw)], [(raw, "U", raw)],
                        [(raw, "R", raw), (raw, "T", raw)],
                        [(raw, "R", raw), (raw, "U", raw)]):
            with self.subTest(private=private):
                with self.assertRaisesRegex(ValueError, "private/host symbol intersection"):
                    self.audit_inventory(private, [(raw, "R", raw)], "coff-index")
        with self.assertRaisesRegex(ValueError, "private/host symbol intersection"):
            self.audit_inventory([(raw, "R", raw)], [(raw, "R", raw)], "nm")

    def test_msvc_runtime_observed_definition_inventory(self):
        fixture = json.loads(Path(__file__).with_name(
            "MsvcRuntimeSymbolsFixture.json").read_text(encoding="utf-8"))
        self.assertEqual(len(fixture), 229)
        definitions = [row for row in fixture if row[1] != ["w"]]
        self.assertEqual(len(definitions), 228)
        # Keep all original observations. These exact five R definitions are
        # runtime-literal candidates, but the independent Setup gate must reject
        # them even when the host exports the same names.
        setup_names = {old for old, _ in SETUP_GUID_PAIRS}
        setup = [row for row in definitions if row[0] in setup_names]
        self.assertEqual(len(setup), 5)
        self.assertEqual({name for name, _, _ in setup}, setup_names)
        for name, kinds, decoded in setup:
            self.assertEqual((kinds, decoded), (["R"], name))
        private = [(name, kinds[0], decoded) for name, kinds, decoded in definitions
                   if name not in setup_names]
        self.assertEqual(len(private), 223)
        self.audit_inventory(private, private, "coff-index")
        for name, kinds, decoded in setup:
            with self.subTest(setup_guid=name):
                rows = [(name, kinds[0], decoded)]
                with self.assertRaisesRegex(ValueError, "unisolated Setup GUID symbol: " + name):
                    self.audit_inventory(rows, rows, "coff-index")

    def test_msvc_delete_wrapper_requires_proven_exact_fallback(self):
        wrapper = "?__global_delete@@YAXPEAX_K@Z"
        fallback = "?__empty_global_delete@@YAXPEAX_K@Z"
        declaration = "void __cdecl __global_delete(void *, unsigned __int64)"
        fallback_decl = "void __cdecl __empty_global_delete(void *, unsigned __int64)"
        private = [(wrapper, "w", declaration), (fallback, "T", fallback_decl)]
        host = [(wrapper, "T", declaration), (fallback, "T", fallback_decl)]
        reader = types.ModuleType("CoffWeakAliases")
        reader.read_resolved_aliases = mock.Mock(return_value={wrapper})
        with mock.patch.dict(sys.modules, {"CoffWeakAliases": reader}):
            self.audit_inventory(private, host, "coff-index", "controlled-readobj")
        reader.read_resolved_aliases.assert_called_once_with(
            "controlled-readobj", Path("private.lib"),
            {"neverc_cpp_frontend_main", fallback}, {wrapper},
            expected_fallbacks={wrapper: fallback})

        for evidence in (None, set()):
            reader.read_resolved_aliases = mock.Mock(return_value=evidence)
            with self.subTest(evidence=evidence), \
                    mock.patch.dict(sys.modules, {"CoffWeakAliases": reader}):
                with self.assertRaisesRegex(ValueError, "private/host symbol intersection"):
                    self.audit_inventory(private, host, "coff-index",
                                         "controlled-readobj" if evidence is not None else None)
        for kind in ("U", "T", "W", "v"):
            with self.subTest(kind=kind), \
                    mock.patch.dict(sys.modules, {"CoffWeakAliases": reader}):
                reader.read_resolved_aliases = mock.Mock(return_value={wrapper})
                with self.assertRaisesRegex(ValueError, "private/host symbol intersection"):
                    self.audit_inventory([(wrapper, kind, declaration), private[1]],
                                         host, "coff-index", "controlled-readobj")
        reader.read_resolved_aliases = mock.Mock(side_effect=ValueError("wrong exact fallback"))
        with mock.patch.dict(sys.modules, {"CoffWeakAliases": reader}):
            with self.assertRaisesRegex(ValueError, "wrong exact fallback"):
                self.audit_inventory(private, host, "coff-index", "controlled-readobj")

    def test_defined_coff_alias_still_requires_fallback_validation(self):
        alias = "neverc_cpp_llvm_alias"
        reader = types.ModuleType("CoffWeakAliases")
        reader.read_resolved_aliases = mock.Mock(side_effect=ValueError("unresolved COFF alias"))
        with mock.patch.dict(sys.modules, {"CoffWeakAliases": reader}):
            with self.assertRaisesRegex(ValueError, "unresolved COFF alias"):
                self.audit_inventory([(alias, "W", alias)], coff_readobj="controlled-readobj")
        self.assertIn(alias, reader.read_resolved_aliases.call_args.args[3])

    def test_resolved_coff_alias_still_participates_in_host_collision_gate(self):
        alias = "neverc_cpp_llvm_alias"
        reader = types.ModuleType("CoffWeakAliases")
        reader.read_resolved_aliases = mock.Mock(return_value={alias})
        with mock.patch.dict(sys.modules, {"CoffWeakAliases": reader}):
            with self.assertRaisesRegex(ValueError, "intersection: " + alias):
                self.audit_inventory([(alias, "w", alias)], [(alias, "T", alias)],
                                     coff_readobj="controlled-readobj")

    def test_posix_records_preserve_original_member_headers_and_rows(self):
        output = "\nSignals.cpp.o:\n_strdup U 0 0\n\nprivate.a(copy.o):\n_strdup W 0 1\n"
        self.assertEqual(list(AuditArchive.symbol_records(output)), [
            ("_strdup", "U", "Signals.cpp.o:", "_strdup U 0 0"),
            ("_strdup", "W", "private.a(copy.o):", "_strdup W 0 1")])
        self.assertEqual(list(AuditArchive.symbol_rows(output)),
                         [("_strdup", "U"), ("_strdup", "W")])

    def test_strdup_reference_only_reports_actual_nm_provenance(self):
        for name in ("strdup", "_strdup"):
            for kind in ("U", "w", "v"):
                with self.subTest(name=name, kind=kind), \
                        mock.patch.object(sys, "stdout", new_callable=io.StringIO) as output:
                    self.audit_inventory([(name, kind, name)], [(name, "T", name)])
                    text = output.getvalue()
                    self.assertIn("private nm archive='private.lib' member_header='private.cpp.obj:'", text)
                    self.assertIn(f"kind={kind!r} raw='{name} {kind} 0 0'", text)
                    self.assertIn("host nm archive='host.lib' member_header='host.cpp.obj:'", text)
                    self.assertIn(f"kind='T' raw='{name} T 0 0'", text)
                    self.assertNotIn("neverc_cpp_frontend_main T", text)

    def test_strdup_private_strong_or_weak_definition_is_never_exempted(self):
        for name in ("strdup", "_strdup"):
            for kind in ("T", "D", "W", "V"):
                with self.subTest(name=name, kind=kind), \
                        mock.patch.object(sys, "stdout", new_callable=io.StringIO) as output:
                    # A reference in another member cannot hide a definition.
                    with self.assertRaisesRegex(ValueError, "intersection: " + name):
                        self.audit_inventory([(name, "U", name), (name, kind, name)],
                                             [(name, "T", name)])
                    text = output.getvalue()
                    self.assertIn(
                        "private nm archive='private.lib' member_header='private.cpp.obj:' "
                        f"symbol={name!r} kind={kind!r} raw='{name} {kind} 0 0'", text)
                    self.assertIn("host nm archive='host.lib'", text)

    def test_strdup_near_names_do_not_get_reference_exception(self):
        for name in ("__strdup", "strdup_custom", "_strdup_custom", "neverc_cpp_strdup"):
            with self.subTest(name=name):
                with self.assertRaisesRegex(ValueError, "intersection: " + name):
                    self.audit_inventory([(name, "U", name)], [(name, "T", name)])

    def test_strdup_coff_index_reports_limited_evidence_without_exception(self):
        for name in ("strdup", "_strdup"):
            with self.subTest(name=name), \
                    mock.patch.object(sys, "stdout", new_callable=io.StringIO) as output:
                with self.assertRaisesRegex(ValueError, "intersection: " + name):
                    self.audit_inventory([(name, "U", name)], [(name, "T", name)], "coff-index")
                text = output.getvalue()
                self.assertIn(f"kind='U' raw='{name} U 0 0'", text)
                self.assertIn("host coff-index archive='host.lib'", text)
                self.assertIn("object kind, member and raw nm row unavailable", text)
                self.assertIn("strdup exception disabled", text)
                self.assertNotIn("host nm", text)

    def test_strdup_batch_provenance_identifies_each_actual_host_archive(self):
        args = argparse.Namespace(nm="controlled-nm", nm_file=None,
                                  archive=Path("private.a"), prefix_header=None,
                                  host_lib_dir=Path("host-libs"), host_format="nm", host_nm=None)
        hosts = [Path("first.a"), Path("second.a")]

        def inventory(_nm, paths, *options):
            if paths == [args.archive]:
                if "--format=posix" in options:
                    return "private.o:\nneverc_cpp_frontend_main T 0 0\n_strdup U 0 0\n"
                return "neverc_cpp_frontend_main\n_strdup\n"
            if "--format=posix" not in options:
                return "_strdup\nother_host_symbol\n"
            return "".join("same-member.o:\n" + ("_strdup T 1 2\n" if archive == hosts[0]
                                                  else "other_host_symbol T 3 4\n")
                           for archive in paths)

        with mock.patch.object(AuditArchive, "nm_output", side_effect=inventory) as reader, \
                mock.patch.object(AuditArchive, "host_archives", return_value=hosts), \
                mock.patch.object(sys, "stdout", new_callable=io.StringIO) as output:
            AuditArchive.audit(args)
            text = output.getvalue()
            self.assertIn("host nm archive='first.a' member_header='same-member.o:'", text)
            self.assertIn("raw='_strdup T 1 2'", text)
            self.assertNotIn("archive='second.a'", text)
            for archive in hosts:
                reader.assert_any_call("controlled-nm", [archive], "--format=posix")

    def test_strdup_changed_host_inventory_fails_closed(self):
        args = argparse.Namespace(nm="controlled-nm", nm_file=None,
                                  archive=Path("private.a"), prefix_header=None,
                                  host_lib_dir=Path("host-libs"), host_format="nm", host_nm=None)
        hosts = [Path("first.a"), Path("second.a")]

        def inventory(_nm, paths, *options):
            if paths == [args.archive]:
                if "--format=posix" in options:
                    return "private.o:\nneverc_cpp_frontend_main T 0 0\n_strdup U 0 0\n"
                return "neverc_cpp_frontend_main\n_strdup\n"
            self.assertIn("--format=posix", options)
            if paths == hosts:
                return "same-member.o:\n_strdup T 1 2\n"
            if paths == [hosts[0]]:
                return "same-member.o:\n_strdup W 1 2\n"
            self.assertEqual(paths, [hosts[1]])
            return "same-member.o:\nother_host_symbol T 3 4\n"

        with mock.patch.object(AuditArchive, "nm_output", side_effect=inventory), \
                mock.patch.object(AuditArchive, "host_archives", return_value=hosts), \
                mock.patch.object(sys, "stdout", new_callable=io.StringIO) as output:
            with self.assertRaisesRegex(ValueError, "Host strdup symbol inventory changed"):
                AuditArchive.audit(args)
            self.assertIn("host nm archive='first.a' member_header='same-member.o:' "
                          "symbol='_strdup' kind='W' raw='_strdup W 1 2'", output.getvalue())
            self.assertIn("host nm batch_archives=['first.a', 'second.a'] "
                          "member_header='same-member.o:' symbol='_strdup' kind='T' "
                          "raw='_strdup T 1 2'", output.getvalue())

    def test_standard_entity_not_merely_standard_parameter(self):
        for name in ("_ZNSt3__16vectorIiNS_9allocatorIiEEE5clearEv",
                     "__ZNKSt3__16vectorIiNS_9allocatorIiEEE4sizeEv",
                     "_ZGVZNSt3__112__some_std_fnEvE5value",
                     "_ZTINSt3__19exceptionE", "_Znwm", "__ZdlPvm",
                     "_malloc", "__clang_call_terminate",
                     "DW.ref.__gxx_personality_v0"):
            with self.subTest(name=name):
                self.assertTrue(AuditArchive.standard_shared_symbol(name))
        for name in ("_Z3fooNSt3__112basic_stringIcEE",
                     "_ZN4host3fooENSt3__16vectorIiEE",
                     "_ZN5clang4Decl4kindEv", "LLVMCreateMessage",
                     "malloc_wrong_abi", "strdup", "_strdup", "strdup_custom", "_strdup_custom",
                     "neverc_cpp_strdup", "__Znwcustom", "DW.ref.host_llvm",
                     "_DW.ref.__gxx_personality_v0"):
            with self.subTest(name=name):
                self.assertFalse(AuditArchive.standard_shared_symbol(name))

    def test_microsoft_global_allocation_ci_references_allow_exact_comma_spacing(self):
        # Exact raw/decoded pairs from a551 MSVC ARM64 diagnostics at lines
        # 20156, 20158, 20159, 20160 and 20191. All private rows were U; host
        # evidence was only the mimalloc.lib COFF definition index. The T rows
        # below are a synthetic definition model, not observed host object kinds.
        declarations = (
            ("??2@YAPEAX_KAEBUnothrow_t@std@@@Z",
             "void * __cdecl operator new(unsigned __int64, "
             "struct std::nothrow_t const &)"),
            ("??2@YAPEAX_KW4align_val_t@std@@AEBUnothrow_t@1@@Z",
             "void * __cdecl operator new(unsigned __int64, enum std::align_val_t, "
             "struct std::nothrow_t const &)"),
            ("??3@YAXPEAX_K@Z",
             "void __cdecl operator delete(void *, unsigned __int64)"),
            ("??3@YAXPEAX_KW4align_val_t@std@@@Z",
             "void __cdecl operator delete(void *, unsigned __int64, "
             "enum std::align_val_t)"),
            ("??_V@YAXPEAX_K@Z",
             "void __cdecl operator delete[](void *, unsigned __int64)"))
        for raw, spaced in declarations:
            for decoded in (spaced, spaced.replace(", ", ",")):
                with self.subTest(raw=raw, decoded=decoded):
                    self.assertTrue(AuditArchive.standard_shared_symbol(raw, decoded))
                    for host_format in ("nm", "coff-index"):
                        with self.subTest(host_format=host_format):
                            self.audit_inventory([(raw, "U", decoded)],
                                                 [(raw, "T", decoded)], host_format)

    def test_microsoft_allocation_spacing_does_not_expand_the_symbol_policy(self):
        declarations = (
            # Placement's second void* remains outside the existing policy.
            ("??2@YAPEAX_KPEAX@Z",
             "void * __cdecl operator new(unsigned __int64, void *)"),
            ("??_U@YAPEAX_KPEAX@Z",
             "void * __cdecl operator new[](unsigned __int64, void *)"),
            ("??2Host@@SAPEAX_KAEBUnothrow_t@std@@@Z",
             "public: static void * __cdecl Host::operator new(unsigned __int64, "
             "struct std::nothrow_t const &)"),
            ("?host_function@@YAPEAX_KAEBUnothrow_t@std@@@Z",
             "void * __cdecl host_function(unsigned __int64, "
             "struct std::nothrow_t const &)"),
            ("??2@YAPEAX_KAEBUnothrow_t@Host@@@Z",
             "void * __cdecl operator new(unsigned __int64, "
             "struct Host::nothrow_t const &)"),
            ("??2@YAPEAX_KW4align_val_t@Host@@@Z",
             "void * __cdecl operator new(unsigned __int64, enum Host::align_val_t)"),
            # These actual a551 helper names are not global operator names.
            ("?__empty_global_delete@@YAXPEAX@Z",
             "void __cdecl __empty_global_delete(void *)"),
            ("?__empty_global_delete@@YAXPEAXW4align_val_t@std@@@Z",
             "void __cdecl __empty_global_delete(void *, enum std::align_val_t)"),
            ("?__empty_global_delete@@YAXPEAX_K@Z",
             "void __cdecl __empty_global_delete(void *, unsigned __int64)"),
            ("?__empty_global_delete@@YAXPEAX_KW4align_val_t@std@@@Z",
             "void __cdecl __empty_global_delete(void *, unsigned __int64, "
             "enum std::align_val_t)"),
            ("?__global_delete@@YAXPEAX_K@Z",
             "void __cdecl __global_delete(void *, unsigned __int64)"))
        for raw, spaced in declarations:
            for decoded in (spaced, spaced.replace(", ", ",")):
                with self.subTest(raw=raw, decoded=decoded):
                    self.assertFalse(AuditArchive.standard_shared_symbol(raw, decoded))
                    for host_format in ("nm", "coff-index"):
                        with self.subTest(host_format=host_format):
                            with self.assertRaisesRegex(ValueError, "private/host symbol intersection"):
                                self.audit_inventory([(raw, "U", decoded)],
                                                     [(raw, "T", decoded)], host_format)

    def test_microsoft_allocation_rejects_other_whitespace_and_trailing_text(self):
        raw = "??2@YAPEAX_KW4align_val_t@std@@AEBUnothrow_t@1@@Z"
        decoded = ("void * __cdecl operator new(unsigned __int64, "
                   "enum std::align_val_t, struct std::nothrow_t const &)")
        parts = decoded.split(", ")
        for separator in (",  ", ",\t", ",\r", ",\n", ",\0"):
            for position in (0, 1):
                malformed = (parts[0] + (separator if position == 0 else ", ") +
                             parts[1] + (separator if position == 1 else ", ") + parts[2])
                with self.subTest(separator=separator, position=position):
                    self.assertFalse(AuditArchive.standard_shared_symbol(raw, malformed))
                    # CR/LF are line framing in the inventory reader, so their
                    # rejection above deliberately tests the policy directly.
                    if separator == ",  ":
                        for host_format in ("nm", "coff-index"):
                            with self.assertRaisesRegex(ValueError, "private/host symbol intersection"):
                                self.audit_inventory([(raw, "U", malformed)],
                                                     [(raw, "T", malformed)], host_format)
        for suffix in (" ", " const", " junk", ")", "\n", "\0"):
            with self.subTest(suffix=suffix):
                self.assertFalse(AuditArchive.standard_shared_symbol(raw, decoded + suffix))

    def test_microsoft_std_in_return_argument_or_conversion_is_not_scope(self):
        for declaration in (
                "class std::string __cdecl host_function(void)",
                "void __cdecl host_function(class std::string)",
                "public: __cdecl Host::operator class std::string(void)",
                "void __cdecl Host::method<class std::string>(void)",
                "class std::string (__cdecl *Host::function(void))(void)",
                "const Host::`vftable'{for std::exception}",
                "public: virtual void * __cdecl Host::`scalar deleting destructor'(unsigned int)",
                "class Host<class std::string> `RTTI Type Descriptor'"):
            with self.subTest(declaration=declaration):
                self.assertFalse(AuditArchive.standard_shared_symbol(
                    "?host@@fake", declaration))
        self.assertTrue(AuditArchive.standard_shared_symbol(
            "?member@std@@fake",
            "public: void __cdecl std::vector<int>::clear(void)"))
        self.assertTrue(AuditArchive.standard_shared_symbol(
            "?member@std@@fake",
            "public: __cdecl std::function<void>::operator bool(void)"))
        self.assertTrue(AuditArchive.standard_shared_symbol(
            "??_R0?AVexception@std@@@8",
            "class std::exception `RTTI Type Descriptor'"))
        self.assertTrue(AuditArchive.standard_shared_symbol(
            "??_Gbad_alloc@std@@fake",
            "public: virtual void * __cdecl std::bad_alloc::`scalar deleting destructor'(unsigned int)"))

    def test_microsoft_all_19_ci_standard_declarations_have_standard_owners(self):
        declarations = [self.microsoft_gcd_lambda]
        for category in ("_Future_error_category2", "_Generic_error_category",
                         "_Iostream_error_category2", "_System_error_category"):
            function = (f"class std::{category} const & __cdecl "
                        f"std::_Immortalize_memcpy_image<class std::{category}>(void)")
            declarations.extend((f"int `{function}'::`2'::$TSS0",
                                 f"class std::{category} `{function}'::`2'::_Static"))
        for facet in (
                "std::codecvt<char, char, struct _Mbstatet>",
                "std::ctype<char>",
                "std::num_put<char, class std::ostreambuf_iterator<char, "
                "struct std::char_traits<char>>>",
                "std::numpunct<char>"):
            # The real demangler attaches the pointer '*' to the entity name.
            declarations.append("public: static class std::locale::facet const *"
                                f"std::_Facetptr<class {facet}>::_Psave")
        for facet in (
                "std::codecvt<char, char, struct _Mbstatet>",
                "std::num_put<char, class std::ostreambuf_iterator<char, "
                "struct std::char_traits<char>>>",
                "std::numpunct<char>"):
            declarations.append("void __cdecl `dynamic initializer for `public: "
                                f"static class std::locale::id {facet}::id''(void)")
        declarations.extend((
            "char const *const `public: virtual class std::basic_string<char, "
            "struct std::char_traits<char>, class std::allocator<char>> __cdecl "
            "std::_Iostream_error_category2::message(int) const'::`5'::_Iostream_error",
            "struct _Mbstatet `protected: void __cdecl std::basic_filebuf<char, "
            "struct std::char_traits<char>>::_Init(struct _iobuf *, enum "
            "std::basic_filebuf<char, struct std::char_traits<char>>::_Initfl)'"
            "::`2'::_Stinit",
            "char const *const `public: virtual class std::basic_string<char, "
            "struct std::char_traits<char>, class std::allocator<char>> __cdecl "
            "std::_System_error_category::message(int) const'::`7'::_Unknown_error"))
        self.assertEqual(len(declarations), 19)
        for declaration in declarations:
            with self.subTest(declaration=declaration):
                self.assertTrue(AuditArchive.microsoft_std_entity(declaration))

    def test_microsoft_standard_owner_reaches_the_existing_intersection_policy(self):
        declarations = (
            ("?_Psave@?$_Facetptr@V?$ctype@D@std@@@std@@2PEBVfacet@locale@2@EB", "B",
             "public: static class std::locale::facet const "
             "*std::_Facetptr<class std::ctype<char>>::_Psave"),
            ("??__E?id@?$numpunct@D@std@@2V0locale@2@A@@YAXXZ", "T",
             "void __cdecl `dynamic initializer for `public: static class "
             "std::locale::id std::numpunct<char>::id''(void)"))
        for name, kind, declaration in declarations:
            with self.subTest(name=name):
                self.assertTrue(AuditArchive.standard_shared_symbol(name, declaration))
                self.audit_inventory([(name, kind, declaration)],
                                     [(name, "W", declaration)])

    def test_microsoft_std_eh_arrays_and_throw_info_share_nm_identity(self):
        # Exact CTA/TI spellings from the cac5 Windows Clang audit. This
        # checks shared std ownership, not record layout or ABI equivalence.
        symbols = (
            "_CTA2?AVbad_cast@std@@",
            "_CTA2?AVbad_optional_access@std@@",
            "_CTA2?AVbad_variant_access@std@@",
            "_CTA3?AVbad_array_new_length@std@@",
            "_CTA3?AVfuture_error@std@@",
            "_CTA5?AVfailure@ios_base@std@@",
            "_TI2?AVbad_cast@std@@",
            "_TI2?AVbad_optional_access@std@@",
            "_TI2?AVbad_variant_access@std@@",
            "_TI3?AVbad_array_new_length@std@@",
            "_TI3?AVfuture_error@std@@",
            "_TI5?AVfailure@ios_base@std@@",
        )
        for name in symbols:
            with self.subTest(name=name):
                self.audit_inventory([(name, "R", name)],
                                     [(name, "W", name)], "nm")

    def test_microsoft_std_catchable_types_share_nm_identity(self):
        # Exact CT spellings from the cac5 Windows Clang audit. Only shared
        # std type/copy-constructor ownership is tested; the numeric suffix
        # does not establish size or layout.
        symbols = (
            "_CT??_R0?AV_System_error@std@@@8"
            "??0_System_error@std@@QEAA@AEBV01@@Z40",
            "_CT??_R0?AVbad_alloc@std@@@8"
            "??0bad_alloc@std@@QEAA@AEBV01@@Z24",
            "_CT??_R0?AVbad_array_new_length@std@@@8"
            "??0bad_array_new_length@std@@QEAA@AEBV01@@Z24",
            "_CT??_R0?AVbad_cast@std@@@8"
            "??0bad_cast@std@@QEAA@AEBV01@@Z24",
            "_CT??_R0?AVbad_optional_access@std@@@8"
            "??0bad_optional_access@std@@QEAA@AEBV01@@Z24",
            "_CT??_R0?AVbad_variant_access@std@@@8"
            "??0bad_variant_access@std@@QEAA@AEBV01@@Z24",
            "_CT??_R0?AVexception@std@@@8"
            "??0exception@std@@QEAA@AEBV01@@Z24",
            "_CT??_R0?AVfailure@ios_base@std@@@8"
            "??0failure@ios_base@std@@QEAA@AEBV012@@Z40",
            "_CT??_R0?AVfuture_error@std@@@8"
            "??0future_error@std@@QEAA@AEBV01@@Z40",
            "_CT??_R0?AVlogic_error@std@@@8"
            "??0logic_error@std@@QEAA@AEBV01@@Z24",
            "_CT??_R0?AVruntime_error@std@@@8"
            "??0runtime_error@std@@QEAA@AEBV01@@Z24",
            "_CT??_R0?AVsystem_error@std@@@8"
            "??0system_error@std@@QEAA@AEBV01@@Z40",
        )
        for name in symbols:
            with self.subTest(name=name):
                self.audit_inventory([(name, "R", name)],
                                     [(name, "W", name)], "nm")

    def test_microsoft_std_catchable_type_plain_owners_and_raw_tail_bounds(self):
        # These tails are accepted as raw decimal spellings, not decoded sizes.
        names = (
            "_CT??_R0?AVsample@std@@@8??0sample@std@@QEAA@AEBV01@@Z0",
            "_CT??_R0?AVsample@std@@@8??0sample@std@@QEAA@AEBV01@@Z4294967295",
            "_CT??_R0?AV_Error1@nested_2@std@@@8"
            "??0_Error1@nested_2@std@@QEAA@AEBV012@@Z24",
        )
        for name in names:
            for private in ([(name, "R", name)],
                            [(name, "R", name), (name, "R", name)]):
                with self.subTest(name=name, definitions=len(private)):
                    self.audit_inventory(private, [(name, "W", name)], "nm")

    def test_microsoft_std_catchable_type_rejects_unknown_or_mismatched_names(self):
        plain = "_CT??_R0?AVbad_cast@std@@@8??0bad_cast@std@@QEAA@AEBV01@@Z24"
        nested = ("_CT??_R0?AVfailure@ios_base@std@@@8"
                  "??0failure@ios_base@std@@QEAA@AEBV012@@Z40")
        names = (
            plain.replace("??0bad_cast@", "??0exception@"),
            nested.replace("??0failure@ios_base@", "??0failure@ios_other@"),
            nested.replace("??0failure@ios_base@", "??0ios_base@failure@"),
            plain.replace("@std@", "@Host@"),
            plain.replace("@std@", "@std@Host@"),
            plain.replace("@std@", "@std_extra@"),
            plain.replace("bad_cast@std", "std@std"),
            nested.replace("failure@ios_base@std", "failure@failure@std"),
            nested.replace("failure@ios_base@std", "failure@std@std"),
            nested.replace("failure@ios_base@std", "failure@ios_base@nested@std"),
            plain.replace("?AVbad_cast@", "?AV?$bad_cast@H@"),
            plain.replace("??0bad_cast@", "??0?$bad_cast@H@"),
            plain.replace("@std@", "@?A0x1234@"),
            plain.replace("?AV", "?AU"),
            plain.replace("?AV", "?BV"),
            plain.replace("@@@8", "@@8"),
            plain.replace("@@@8", "@@@9"),
            plain.replace("@@@8", "@@@@8"),
            plain.replace("@@@8", "@@@8extra"),
            plain.replace("??0bad_cast@std@@QEAA@AEBV01@@Z", ""),
            plain.replace("??0", "??_O"),
            plain.replace("??0", "??_F"),
            plain.replace("QEAA", "QAE"),
            plain.replace("AEBV01@@Z", "AEBV012@@Z"),
            plain.replace("AEBV01@@Z", "AEBV10@@Z"),
            plain.replace("AEBV01@@Z", "AEBV00@@Z"),
            plain.replace("AEBV01@@Z", "AEBV09@@Z"),
            plain.replace("AEBV01@@Z", "AEBV010@@Z"),
            plain.replace("AEBV01@@Z", "AEBV01@H@Z"),
            plain.replace("AEBV01@@Z", "AEBV01@@ZZ"),
            nested.replace("AEBV012@@Z", "AEBV01@@Z"),
            nested.replace("AEBV012@@Z", "AEBV021@@Z"),
            nested.replace("AEBV012@@Z", "AEBV013@@Z"),
            "_CT??@0123456789abcdef@@24",
            plain + "@",
            plain + nested,
        )
        for name in names:
            with self.subTest(name=name):
                with self.assertRaisesRegex(
                        ValueError, "private/host symbol intersection"):
                    self.audit_inventory([(name, "R", name)],
                                         [(name, "W", name)], "nm")

    def test_microsoft_std_catchable_type_rejects_unknown_numeric_tails(self):
        prefix = "_CT??_R0?AVbad_cast@std@@@8??0bad_cast@std@@QEAA@AEBV01@@Z"
        for tail in ("", "00", "024", "+24", "-24", "0x18", "\u0662",
                     "4294967296", "10000000000", "24x", "24@", "24_1", "24-120"):
            name = prefix + tail
            with self.subTest(tail=tail):
                with self.assertRaisesRegex(
                        ValueError, "private/host symbol intersection"):
                    self.audit_inventory([(name, "R", name)],
                                         [(name, "W", name)], "nm")

    def test_microsoft_std_catchable_type_requires_only_read_only_definitions(self):
        name = "_CT??_R0?AVbad_cast@std@@@8??0bad_cast@std@@QEAA@AEBV01@@Z24"
        for kinds in (("U",), ("w",), ("v",), ("B",), ("D",), ("T",), ("W",),
                      ("V",), ("R", "U"), ("U", "R"), ("R", "w"), ("R", "v"),
                      ("R", "B"), ("R", "D"), ("R", "T"), ("R", "W"), ("R", "V")):
            with self.subTest(kinds=kinds):
                with self.assertRaisesRegex(
                        ValueError, "private/host symbol intersection"):
                    self.audit_inventory([(name, kind, name) for kind in kinds],
                                         [(name, "W", name)], "nm")

    def test_microsoft_std_catchable_type_preserves_independent_private_findings(self):
        name = "_CT??_R0?AVbad_cast@std@@@8??0bad_cast@std@@QEAA@AEBV01@@Z24"
        for old, _ in SETUP_GUID_PAIRS:
            with self.subTest(setup_guid=old):
                with self.assertRaisesRegex(
                        ValueError, "unisolated Setup GUID symbol: " + old):
                    self.audit_inventory([(name, "R", name), (old, "R", old)],
                                         [(name, "W", name), (old, "R", old)], "nm")
        with self.assertRaisesRegex(ValueError, "LLVMContextCreate"):
            self.audit_inventory([(name, "R", name),
                                  ("LLVMContextCreate", "T", "LLVMContextCreate")],
                                 [(name, "W", name)], "nm")

    def test_microsoft_std_eh_simple_owners_and_count_bounds(self):
        for prefix in ("_CTA", "_TI"):
            for spelling in ("0?AVerror@std@@", "4294967295?AVerror@std@@",
                             "12?AV_Error1@nested_2@std@@",
                             "2?AVerror@" + "nested@" * 30 + "std@@"):
                name = prefix + spelling
                with self.subTest(name=name):
                    self.audit_inventory([(name, "R", name)],
                                         [(name, "W", name)], "nm")

    def test_microsoft_std_eh_rejects_unknown_or_malformed_names(self):
        spellings = (
            "2?AVerror@Host@@",
            "2?AVerror@std@Host@@",
            "2?AVerror@std_extra@@",
            "2?AVstd@@",
            "2?AVerror@@",
            "2?AVerror@@std@@",
            "2?AVerror@std@@suffix",
            "2?AVerror@std@@@",
            "2?AVerror@std@",
            "2?AVerror@0@@",
            "2?AVerror@0@std@@",
            "2?AV?$error@H@std@@",
            "2?AVerror@?$owner@H@std@@",
            "2?AVerror@?A0x1234@std@@",
            "2?AVerror$1@std@@",
            "2?AV9error@std@@",
            "2?AVerror@std::@@",
            "2?AUerror@std@@",
            "2?ATerror@std@@",
            "2?AW4error@std@@",
            "2?BVerror@std@@",
            "?AVerror@std@@",
            "00?AVerror@std@@",
            "02?AVerror@std@@",
            "+2?AVerror@std@@",
            "-2?AVerror@std@@",
            "0x2?AVerror@std@@",
            "\u0662?AVerror@std@@",
            "4294967296?AVerror@std@@",
            "10000000000?AVerror@std@@",
            "C2?AVerror@std@@",
            "V2?AVerror@std@@",
            "U2?AVerror@std@@",
            "CVU2?AVerror@std@@",
            "2?AV" + "a" * 65536 + "@std@@",
            "2?AVerror@" + "nested@" * 31 + "std@@",
        )
        for prefix in ("_CTA", "_TI"):
            for spelling in spellings:
                name = prefix + spelling
                with self.subTest(prefix=prefix, spelling=spelling[:100]):
                    with self.assertRaisesRegex(
                            ValueError, "private/host symbol intersection"):
                        self.audit_inventory([(name, "R", name)],
                                             [(name, "W", name)], "nm")

    def test_microsoft_std_eh_requires_only_read_only_definitions(self):
        for name in ("_CTA2?AVbad_cast@std@@", "_TI2?AVbad_cast@std@@"):
            for kinds in (("U",), ("w",), ("v",), ("B",), ("D",), ("T",),
                          ("W",), ("V",), ("R", "U"), ("U", "R"),
                          ("R", "w"), ("R", "v"), ("R", "B"),
                          ("R", "D"), ("R", "T"), ("R", "W"), ("R", "V")):
                with self.subTest(name=name, kinds=kinds):
                    with self.assertRaisesRegex(
                            ValueError, "private/host symbol intersection"):
                        self.audit_inventory([(name, kind, name) for kind in kinds],
                                             [(name, "W", name)], "nm")

    def test_microsoft_stdio_definitions_share_module_runtime_identity(self):
        # These are the actual Windows Clang x64/ARM64 private T7/B2 and host
        # W inventories. Host W is not evidence of a text or storage kind.
        # UCRT shares the accessors and their options storage within one final
        # module; this does not assert a process-wide or cross-DLL identity.
        records = (
            ("__local_stdio_printf_options", "T", "__local_stdio_printf_options"),
            ("__local_stdio_scanf_options", "T", "__local_stdio_scanf_options"),
            ("_snprintf", "T", "_snprintf"),
            ("fprintf", "T", "fprintf"),
            ("snprintf", "T", "snprintf"),
            ("sprintf_s", "T", "sprintf_s"),
            ("sscanf", "T", "sscanf"),
            ("?_OptionsStorage@?1??__local_stdio_printf_options@@9@4_KA", "B",
             "unsigned __int64 `extern \"C\" __local_stdio_printf_options'::"
             "`2'::_OptionsStorage"),
            ("?_OptionsStorage@?1??__local_stdio_scanf_options@@9@4_KA", "B",
             "unsigned __int64 `extern \"C\" __local_stdio_scanf_options'::"
             "`2'::_OptionsStorage"),
        )
        for name, kind, decoded in records:
            with self.subTest(name=name):
                self.audit_inventory([(name, kind, decoded)],
                                     [(name, "W", name)], "nm")

    def test_microsoft_avx2_fallback_definition_shares_module_identity(self):
        # UCRT coalesces this zero-initialized fallback in one final module.
        # Sharing this definition does not authorize the feature-variable alias.
        name = "_Avx2WmemEnabledWeakValue"
        self.audit_inventory([(name, "B", name)], [(name, "W", name)], "nm")

    def test_microsoft_avx2_fallback_duplicate_b_definitions_share_identity(self):
        name = "_Avx2WmemEnabledWeakValue"
        self.audit_inventory([(name, "B", name)] * 2,
                             [(name, "W", name)] * 2, "nm")

    def test_microsoft_avx2_fallback_rejects_other_kinds_and_references(self):
        name = "_Avx2WmemEnabledWeakValue"
        for wrong in ("C", "D", "R", "T", "W", "V", "U", "w", "v"):
            for kinds in ((wrong,), ("B", wrong), (wrong, "B")):
                with self.subTest(kinds=kinds):
                    with self.assertRaisesRegex(
                            ValueError, "private/host symbol intersection"):
                        self.audit_inventory([(name, kind, name) for kind in kinds],
                                             [(name, "W", name)], "nm")

    def test_microsoft_avx2_fallback_requires_exact_private_declaration(self):
        name = "_Avx2WmemEnabledWeakValue"
        declarations = ("different", "int std::state", " " + name,
                        name + " ", name + "_extra")
        declarations += tuple(name[:1] + control + name[1:]
                              for control in ("\x00", "\t", "\x1f", "\x7f"))
        for decoded in declarations:
            with self.subTest(decoded=repr(decoded)):
                with self.assertRaisesRegex(
                        ValueError, "private/host symbol intersection"):
                    self.audit_inventory([(name, "B", decoded)],
                                         [(name, "W", name)], "nm")

    def test_microsoft_avx2_fallback_does_not_authorize_other_symbols(self):
        fallback = "_Avx2WmemEnabledWeakValue"
        names = ("_Avx2WmemEnabled", "_" + fallback, fallback + "_extra",
                 "prefix_" + fallback, fallback.lower(),
                 "__isa_available_default", "_OtherFeatureWeakValue")
        for name in names:
            for kind in ("B", "T", "W", "U", "w", "v"):
                with self.subTest(name=name, kind=kind):
                    with self.assertRaisesRegex(
                            ValueError, "private/host symbol intersection"):
                        self.audit_inventory([(name, kind, name)],
                                             [(name, "W", name)], "nm")

    def test_microsoft_avx2_fallback_preserves_independent_private_findings(self):
        fallback = "_Avx2WmemEnabledWeakValue"
        cases = [
            ("LLVMContextCreate", "T", "LLVMContextCreate"),
            ("neverc_cpp_llvm_missing", "U",
             "unresolved private dependency: neverc_cpp_llvm_missing"),
        ]
        for old, _ in SETUP_GUID_PAIRS:
            for kind in ("R", "U"):
                cases.append((old, kind, "unisolated Setup GUID symbol: " + old))
        for name, kind, diagnostic in cases:
            with self.subTest(name=name, kind=kind):
                with self.assertRaises(ValueError) as failure:
                    self.audit_inventory(
                        [(fallback, "B", fallback), (name, kind, name)],
                        [(fallback, "W", fallback), (name, "W", name)], "nm")
                self.assertIn(diagnostic, str(failure.exception))

    def test_microsoft_stdio_duplicate_definition_kinds_share_module_identity(self):
        for name, kind, decoded in MSVC_STDIO_MODULE_RECORDS:
            with self.subTest(name=name):
                self.audit_inventory([(name, kind, decoded)] * 2,
                                     [(name, "W", name)] * 2, "nm")

    def test_microsoft_stdio_rejects_wrong_and_mixed_private_definition_kinds(self):
        for name, expected, decoded in MSVC_STDIO_MODULE_RECORDS:
            for wrong in ("B", "C", "D", "R", "T", "W", "V"):
                if wrong == expected:
                    continue
                for kinds in ((wrong,), (expected, wrong), (wrong, expected)):
                    with self.subTest(name=name, kinds=kinds):
                        with self.assertRaisesRegex(
                                ValueError, "private/host symbol intersection"):
                            self.audit_inventory(
                                [(name, kind, decoded) for kind in kinds],
                                [(name, "W", name)], "nm")

    def test_microsoft_stdio_rejects_references_with_or_without_definitions(self):
        for name, definition, decoded in MSVC_STDIO_MODULE_RECORDS:
            for reference in ("U", "w", "v"):
                for kinds in ((reference,), (definition, reference),
                              (reference, definition)):
                    with self.subTest(name=name, kinds=kinds):
                        with self.assertRaisesRegex(
                                ValueError, "private/host symbol intersection"):
                            self.audit_inventory(
                                [(name, kind, decoded) for kind in kinds],
                                [(name, "W", name)], "nm")

    def test_microsoft_stdio_requires_exact_untainted_private_declarations(self):
        for name, kind, decoded in MSVC_STDIO_MODULE_RECORDS:
            # A valid std owner must not rescue an exact storage name after
            # its independent stdio declaration check has failed.
            wrong = ("different declaration", "int std::state",
                     " " + decoded, decoded + " ", decoded + " trailing")
            wrong += tuple(decoded[:1] + control + decoded[1:]
                           for control in ("\x00", "\t", "\x1f", "\x7f"))
            for declaration in wrong:
                with self.subTest(name=name, declaration=repr(declaration)):
                    with self.assertRaisesRegex(
                            ValueError, "private/host symbol intersection"):
                        self.audit_inventory([(name, kind, declaration)],
                                             [(name, "W", name)], "nm")

    def test_microsoft_stdio_does_not_accept_nearby_raw_spellings(self):
        for name, kind, decoded in MSVC_STDIO_MODULE_RECORDS:
            for nearby in (name + "_extra", "prefix_" + name, "_" + name):
                with self.subTest(original=name, nearby=nearby):
                    with self.assertRaisesRegex(
                            ValueError, "private/host symbol intersection"):
                        self.audit_inventory([(nearby, kind, decoded)],
                                             [(nearby, "W", nearby)], "nm")

    def test_microsoft_printf_definition_shares_module_runtime_identity(self):
        # Windows Clang x64 install observes private MicrosoftDemangle T and
        # host GoogleTest W definitions of this UCRT header wrapper.
        self.audit_inventory([("printf", "T", "printf")],
                             [("printf", "W", "printf")], "nm")

    def test_microsoft_stdio_does_not_expand_to_other_runtime_families(self):
        records = (
            ("__real@3ff0000000000000", "R", "__real@3ff0000000000000"),
            ("?_OptionsStorage@?1??__local_stdio_printf_options@@9@9", "C",
             "extern \"C\" \x60extern \"C\" __local_stdio_printf_options'::"
             "\x602'::_OptionsStorage"),
            ("?_OptionsStorage@?1??__local_stdio_scanf_options@@9@9", "C",
             "extern \"C\" \x60extern \"C\" __local_stdio_scanf_options'::"
             "\x602'::_OptionsStorage"),
        )
        for name, kind, decoded in records:
            with self.subTest(name=name):
                with self.assertRaisesRegex(
                        ValueError, "private/host symbol intersection"):
                    self.audit_inventory([(name, kind, decoded)],
                                         [(name, "W", name)], "nm")

    def test_microsoft_stdio_sharing_preserves_independent_private_llvm_errors(self):
        shared = list(MSVC_STDIO_MODULE_RECORDS)
        host = [(name, "W", name) for name, _, _ in shared]
        cases = (
            ("LLVMGetGlobalContext", "T", "LLVMGetGlobalContext",
             "LLVMGetGlobalContext"),
            ("unisolated_llvm_owner", "T", "void __cdecl llvm::state(void)",
             "unisolated_llvm_owner => void __cdecl llvm::state(void)"),
            ("neverc_cpp_llvm_missing", "U", "neverc_cpp_llvm_missing",
             "unresolved private dependency: neverc_cpp_llvm_missing"),
        )
        for name, kind, decoded, diagnostic in cases:
            with self.subTest(name=name):
                with self.assertRaises(ValueError) as failure:
                    self.audit_inventory([*shared, (name, kind, decoded)], host, "nm")
                self.assertIn(diagnostic, str(failure.exception))

    def test_microsoft_stdio_sharing_preserves_independent_setup_errors(self):
        shared = list(MSVC_STDIO_MODULE_RECORDS)
        host = [(name, "W", name) for name, _, _ in shared]
        cases = []
        for old, new in SETUP_GUID_PAIRS:
            cases.extend((
                (old, "R", "unisolated Setup GUID symbol: " + old),
                (old, "U", "unisolated Setup GUID symbol: " + old),
                (new, "U", "unresolved private dependency: " + new),
                (new, "W", "Setup GUID weak closure requires a COFF reader: " + new),
                (new, "R", "private/host symbol intersection: " + new),
                ("invented_" + new, "R",
                 "Unsupported private Setup GUID symbol grammar: invented_" + new),
            ))
        for name in (SETUP_GET_IID, SETUP_CONVERT,
                     "invented_" + SETUP_GUID_PAIRS[0][0]):
            cases.append((name, "T", "unisolated Setup GUID symbol: " + name))
        for prefix in ("$pdata$", "$unwind$", "$cppxdata$", "$ip2state$"):
            name = prefix + PRIVATE_SETUP_RELEASE
            cases.append((name, "R", "Setup GUID metadata has external linkage: " + name))
        for name, kind, diagnostic in cases:
            with self.subTest(name=name, kind=kind):
                with self.assertRaises(ValueError) as failure:
                    self.audit_inventory([*shared, (name, kind, name)],
                                         [*host, (name, "W", name)], "nm")
                self.assertIn(diagnostic, str(failure.exception))
        reader = types.ModuleType("CoffWeakAliases")
        reader.read_resolved_aliases = mock.Mock(
            side_effect=ValueError("controlled COFF closure failure"))
        with mock.patch.dict(sys.modules, {"CoffWeakAliases": reader}):
            with self.assertRaisesRegex(ValueError, "controlled COFF closure failure"):
                self.audit_inventory(shared, host, "nm", "controlled-readobj")
        reader.read_resolved_aliases.assert_called_once()

    def test_microsoft_std_eh_does_not_share_other_runtime_records(self):
        records = (
            ("__real@3ff0000000000000", "R"),
        )
        for name, kind in records:
            with self.subTest(name=name):
                with self.assertRaisesRegex(
                        ValueError, "private/host symbol intersection"):
                    self.audit_inventory([(name, kind, name)],
                                         [(name, "W", name)], "nm")

    def test_microsoft_std_eh_sharing_preserves_independent_private_findings(self):
        for name in ("_CTA2?AVbad_cast@std@@", "_TI2?AVbad_cast@std@@"):
            for old, _ in SETUP_GUID_PAIRS:
                with self.subTest(name=name, setup_guid=old):
                    with self.assertRaisesRegex(
                            ValueError, "unisolated Setup GUID symbol: " + old):
                        self.audit_inventory([(name, "R", name), (old, "R", old)],
                                             [(name, "W", name), (old, "R", old)],
                                             "nm")
            with self.subTest(name=name, llvm_symbol="LLVMContextCreate"):
                with self.assertRaisesRegex(ValueError, "LLVMContextCreate"):
                    self.audit_inventory(
                        [(name, "R", name),
                         ("LLVMContextCreate", "T", "LLVMContextCreate")],
                        [(name, "W", name)], "nm")

    def test_microsoft_nonstandard_owners_cannot_hide_in_nested_quotes(self):
        declarations = (
            "int `class std::string __cdecl Host::get(void)'::`2'::$TSS0",
            "class std::string `class std::string __cdecl Host::get(void)'"
            "::`2'::_Static",
            "public: <auto> __cdecl `class std::vector<int> __cdecl Host::run(void)'"
            "::`1'::<lambda_1>::operator()(void) const",
            "class `void __cdecl std::run(void)'::`1'::<lambda_1> "
            "`void __cdecl Host::run(void)'::`2'::callback",
            "public: static class std::vector<int> "
            "`void __cdecl Host::run(void)'::`2'::Local::state",
            # std owns a nested argument's implementation, but not this lambda.
            self.microsoft_gcd_lambda.replace("std::gcd<", "Host::gcd<"),
            "int `class std::shared_ptr<struct Concurrency::scheduler_interface> "
            "* __cdecl Concurrency::details::_GetStaticAmbientSchedulerStorage(void)'"
            "::`2'::$TSS0")
        for declaration in declarations:
            with self.subTest(declaration=declaration):
                self.assertFalse(AuditArchive.microsoft_std_entity(declaration))

    def test_microsoft_variable_type_and_pointer_punctuation_do_not_change_owner(self):
        for declaration in (
                "public: static class std::string *Host::state",
                "public: static class std::string &Host::state",
                "public: static class std::string *Host<class std::string>::state",
                "public: static int vendor::std::state",
                "public: static int std_extra::state",
                "public: static int Host<std::vector<int>>::state",
                "public: void __cdecl Host<std::vector<int>>::method(void)",
                "public: __cdecl Host<std::vector<int>>::operator class std::string(void)",
                "class std::string __cdecl Host::method(class std::vector<int>)"):
            with self.subTest(declaration=declaration):
                self.assertFalse(AuditArchive.microsoft_std_entity(declaration))

    def test_microsoft_compound_operator_tokens_preserve_the_declared_owner(self):
        # These operator spellings/signatures occur in LLVM's ms-operators.test.
        for operator, parameters in (("->", "void"), ("->*", "int"),
                                     ("&&", "int"), ("||", "int"),
                                     ("++", "void"), ("++", "int"),
                                     ("--", "void"), ("--", "int")):
            for owner, expected in (("std::Box", True), ("Host", False)):
                declaration = f"int __cdecl {owner}::operator{operator}({parameters})"
                with self.subTest(declaration=declaration):
                    self.assertEqual(AuditArchive.microsoft_std_entity(declaration), expected)

    def test_microsoft_less_template_preserves_the_actual_ci_pair_owner(self):
        # Complete b24 Windows Clang x64 declaration: ?M is operator<, and
        # MicrosoftDemangleNodes appends the template '<' without a separator.
        string = ("class std::basic_string<char, struct std::char_traits<char>, "
                  "class std::allocator<char>>")
        pair = f"struct std::pair<{string}, {string}>"
        declaration = (f"bool __cdecl std::operator<<{string}, {string}, {string}, {string}>"
                       f"({pair} const &, {pair} const &)")
        raw = ("??$?MV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@"
               "V01@V01@V01@@std@@YA_NAEBU?$pair@V?$basic_string@DU?$char_traits@D@std@@"
               "V?$allocator@D@2@@std@@V12@@0@0@Z")
        self.assertTrue(AuditArchive.standard_shared_symbol(raw, declaration))
        for host_format in ("nm", "coff-index"):
            with self.subTest(host_format=host_format):
                self.audit_inventory([(raw, "T", declaration)],
                                     [(raw, "W", declaration)], host_format)
        # Keep every std type in the signature; only the declaration owner
        # changes. The enclosing function also owns its quoted local symbols.
        host = declaration.replace("std::operator<", "Host::operator<", 1)
        for parent, expected in ((declaration, True), (host, False)):
            for value in (parent, f"int `{parent}'::`2'::state"):
                with self.subTest(declaration=value):
                    self.assertEqual(AuditArchive.microsoft_std_entity(value), expected)

    def test_microsoft_less_and_shift_template_punctuation_preserves_owners(self):
        for name in ("operator<", "operator<<", "operator<<=",
                     "operator<<int>", "operator<<<int>",
                     "operator<<=<int>",
                     "operator<<class <unnamed-type-1>>",
                     "operator<<<<unnamed-type-1>>"):
            for owner, expected in (("std", True), ("Host", False)):
                parent = f"bool __cdecl {owner}::{name}(int, int)"
                for declaration in (parent, f"int `{parent}'::`2'::state"):
                    with self.subTest(declaration=declaration):
                        self.assertEqual(AuditArchive.microsoft_std_entity(declaration),
                                         expected)

    def test_microsoft_template_operator_malformed_and_ambiguous_forms_fail_closed(self):
        for declaration in (
                "bool __cdecl std::operator<<int(int, int)",
                "bool __cdecl std::operator<<int>>(int, int)",
                "bool __cdecl std::operator<<<int(int, int)",
                "bool __cdecl std::operator<<<int>>(int, int)",
                "bool __cdecl std::operator<<int>(int, int) trailing",
                "bool __cdecl std::operator<<int>(int, int) constconst",
                "int `bool __cdecl std::operator<<int>(int, int)::`2'::state",
                "int `bool __cdecl std::operator<<",
                # A tagless anonymous first argument is ambiguous with the
                # shift spelling. Do not guess an alternative operator split.
                "bool __cdecl std::operator<<<unnamed-type-1>>(int, int)",
                "bool __cdecl std::operator<<<<unnamed-type-1>>>(int, int)"):
            with self.subTest(declaration=declaration):
                self.assertFalse(AuditArchive.microsoft_std_entity(declaration))
        for control in ("\n", "\r", "\t", "\x00", "\x1f", "\x7f"):
            for declaration in ("bool __cdecl std::operator<<(int, int)",
                                "int `bool __cdecl std::operator<<(int, int)'::`2'::state"):
                with self.subTest(declaration=declaration, control=repr(control)):
                    self.assertFalse(AuditArchive.microsoft_std_entity(declaration + control))

    def test_microsoft_template_operator_nesting_remains_bounded(self):
        for operator in ("<", "<<"):
            for depth, expected in ((32, True), (33, False)):
                argument = "Box<" * (depth - 1) + "int" + ">" * (depth - 1)
                declaration = f"bool __cdecl std::operator{operator}<{argument}>(int, int)"
                with self.subTest(operator=operator, depth=depth):
                    self.assertEqual(AuditArchive.microsoft_std_entity(declaration), expected)

    def test_microsoft_function_suffix_requires_separate_ordered_qualifiers(self):
        declaration = "public: void __cdecl std::Box::f(void)"
        # MicrosoftDemangleNodes.cpp emits noexcept before the ref qualifier.
        for suffix in ("const noexcept &",
                       "const volatile __restrict __unaligned noexcept &&"):
            with self.subTest(suffix=suffix):
                self.assertTrue(AuditArchive.microsoft_std_entity(declaration + " " + suffix))
        for suffix in ("constvolatile", "const const", "&&&", "noexcept const",
                       "const & noexcept"):
            with self.subTest(suffix=suffix):
                self.assertFalse(AuditArchive.microsoft_std_entity(declaration + " " + suffix))

    def test_microsoft_dynamic_wrappers_follow_the_variable_owner(self):
        for operation in ("dynamic initializer", "dynamic atexit destructor"):
            standard = (f"void __cdecl `{operation} for `public: static "
                        "class std::locale::id std::numpunct<char>::id''(void)")
            host = (f"void __cdecl `{operation} for `public: static "
                    "class std::vector<int> Host::state''(void)")
            with self.subTest(operation=operation):
                self.assertTrue(AuditArchive.microsoft_std_entity(standard))
                self.assertFalse(AuditArchive.microsoft_std_entity(host))
        # Real negative from LLVM 20 llvm/test/Demangle/ms-operators.test.
        self.assertFalse(AuditArchive.microsoft_std_entity(
            "void __cdecl `dynamic atexit destructor for `private: static class "
            "std::vector<class antlr4::dfa::DFA, class std::allocator<class "
            "antlr4::dfa::DFA>> XPathLexer::_decisionToDFA''(void)"))

    def test_microsoft_malformed_standard_declarations_fail_closed(self):
        for declaration in (
                "", "std::", "void __cdecl std::f(",
                "int std::A:::value",
                "int `void __cdecl std::f(void)'::::value",
                "void __cdecl std::vector<int::clear(void)",
                "void __cdecl std::vector<int>>::clear(void)",
                "void __cdecl std::f(void))",
                "void __cdecl std::f(void) trailing_garbage",
                "void __cdecl std::f(void); void __cdecl Host::f(void)",
                "int `void __cdecl std::f(void)::`2'::state",
                "int `void __cdecl std::f(void)'::`2::state",
                "int `void __cdecl std::f(void)'::`2'::",
                "void __cdecl `dynamic initializer for `int std::state'(void)",
                "void __cdecl `dynamic initializer for `int std::state''(void) garbage",
                "void __cdecl std::f(void)\n",
                "void __cdecl std::f(void)\x00"):
            with self.subTest(declaration=declaration):
                self.assertFalse(AuditArchive.microsoft_std_entity(declaration))

    def test_microsoft_input_length_boundary_is_enforced(self):
        prefix, suffix = "int std::", "::value"
        declaration = prefix + "X" * (65536 - len(prefix) - len(suffix)) + suffix
        self.assertEqual(len(declaration), 65536)
        self.assertTrue(AuditArchive.microsoft_std_entity(declaration))
        self.assertFalse(AuditArchive.microsoft_std_entity(declaration + "x"))

    def test_microsoft_template_nesting_boundary_is_enforced(self):
        for depth, expected in ((32, True), (33, False)):
            declaration = "int " + "std::Box<" * depth + "int" + ">" * depth + "::value"
            with self.subTest(depth=depth):
                self.assertEqual(AuditArchive.microsoft_std_entity(declaration), expected)

    def test_microsoft_nested_quotes_and_parentheses_are_bounded(self):
        parent = "void __cdecl std::root(void)"
        for _ in range(40):
            parent = ("void __cdecl `" + parent + "'::`1'::<lambda_1>"
                      "::operator()(void)")
        self.assertFalse(AuditArchive.microsoft_std_entity(parent))
        # Parentheses count even when they occur in a template argument's type.
        self.assertFalse(AuditArchive.microsoft_std_entity(
            "void __cdecl std::function<" + "(" * 33 + "int" + ")" * 33 + ">::f(void)"))

    def test_scan_excludes_only_the_canonical_self_import_library(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary) / "library outputs"
            (directory / "other").mkdir(parents=True)
            (directory / "existing").mkdir()
            private = directory / "private.lib"
            own_output = directory / "renamed-compiler.lib"
            host = directory / "LLVMCore.lib"
            same_basename = directory / "other" / own_output.name
            same_stem = directory / "renamed-compiler.a"
            for path in (private, own_output, host, same_basename, same_stem):
                path.touch()
            output_spelling = directory / "existing" / ".." / own_output.name
            self.assertEqual(AuditArchive.host_archives(
                directory, private, self_import_library=output_spelling),
                [host, same_stem, same_basename])

    def test_scan_is_stable_before_and_after_its_own_link_output_exists(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            private = directory / "private.lib"
            own_output = directory / "neverc.lib"
            host = directory / "LLVMCore.lib"
            private.touch()
            host.touch()
            self.assertEqual(AuditArchive.host_archives(
                directory, private, self_import_library=own_output), [host])
            own_output.touch()
            for stage in (2, 3):
                with self.subTest(stage=stage):
                    self.assertEqual(AuditArchive.host_archives(
                        directory, private, self_import_library=own_output), [host])

    def test_scan_without_self_import_library_keeps_existing_outputs(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            private = directory / "private.lib"
            own_output = directory / "neverc.lib"
            host = directory / "LLVMCore.lib"
            for path in (private, own_output, host):
                path.touch()
            self.assertEqual(AuditArchive.host_archives(directory, private),
                             [host, own_output])
            self.assertEqual(AuditArchive.host_archives(
                directory, private, self_import_library=None), [host, own_output])

    def test_scan_rejects_no_host_after_excluding_its_own_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            private = directory / "private.lib"
            own_output = directory / "neverc.lib"
            (directory / "_deps").mkdir()
            (directory / "_deps" / "upstream.lib").touch()
            private.touch()
            for output_exists in (False, True):
                with self.subTest(output_exists=output_exists):
                    if output_exists:
                        own_output.touch()
                    with self.assertRaisesRegex(ValueError, "No host archives found"):
                        AuditArchive.host_archives(
                            directory, private, self_import_library=own_output)

    def check_self_import_library_audit(self, host_format, genuine_host_clang):
        # These are empty inventory fixtures, not native archives. Exercise the
        # real directory scan and audit with controlled readers for both formats.
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary) / "library outputs"
            (directory / "other").mkdir(parents=True)
            private = directory / "private.lib"
            own_output = directory / "renamed-compiler.lib"
            host = directory / "LLVMCore.lib"
            other = directory / "other" / own_output.name
            clang = ("_ZN5clang4Decl3fooEv", "T", "clang::Decl::foo()")
            records = {
                private: [("neverc_cpp_frontend_main", "T", "neverc_cpp_frontend_main"),
                          clang],
                own_output: [clang],
                host: [("host_only_function", "T", "host_only_function")],
            }
            if genuine_host_clang:
                records[other] = [clang]
            for path in records:
                path.touch()
            args = argparse.Namespace(
                nm="controlled-nm", nm_file=None, archive=private, prefix_header=None,
                host_lib_dir=directory, host_format=host_format, host_nm=None,
                self_import_library=own_output)
            host_reads = set()

            def inventory(_nm, paths, *options):
                if paths != [private]:
                    host_reads.update(paths)
                rows = [row for path in paths for row in records[path]]
                if "--format=posix" in options:
                    return "".join(f"{raw} {kind} 0 0\n" for raw, kind, _ in rows)
                self.assertIn("--format=just-symbols", options)
                return "".join((decoded if "--demangle" in options else raw) + "\n"
                               for raw, _, decoded in rows)

            def indexed(archive):
                host_reads.add(archive)
                return {raw for raw, _, _ in records[archive]}

            reader = types.ModuleType("HostCoffSymbols")
            reader.read_defined_symbols = mock.Mock(side_effect=indexed)
            with mock.patch.dict(sys.modules, {"HostCoffSymbols": reader}), \
                    mock.patch.object(AuditArchive, "nm_output", side_effect=inventory), \
                    mock.patch("sys.stdout", new_callable=io.StringIO):
                if genuine_host_clang:
                    with self.assertRaisesRegex(ValueError, "unexpected host Clang definition"):
                        AuditArchive.audit(args)
                else:
                    AuditArchive.audit(args)
            self.assertNotIn(own_output, host_reads)
            self.assertIn(host, host_reads)
            if genuine_host_clang:
                self.assertIn(other, host_reads)

    def test_audit_excludes_self_import_library_before_reading_host_symbols(self):
        for host_format in ("nm", "coff-index"):
            with self.subTest(host_format=host_format):
                self.check_self_import_library_audit(host_format, genuine_host_clang=False)

    def test_audit_still_rejects_clang_in_another_library_with_the_same_basename(self):
        for host_format in ("nm", "coff-index"):
            with self.subTest(host_format=host_format):
                self.check_self_import_library_audit(host_format, genuine_host_clang=True)

    def test_cli_passes_the_optional_self_import_library_path(self):
        for own_output in (None, Path("library outputs") / "renamed-compiler.lib"):
            with self.subTest(own_output=own_output):
                arguments = ["audit", "--nm", "controlled", "--archive", "private.a",
                             "--host-lib-dir", "library outputs"]
                if own_output is not None:
                    arguments.extend(("--self-import-library", str(own_output)))
                with mock.patch.object(sys, "argv", arguments), \
                        mock.patch.object(AuditArchive, "audit") as audit:
                    AuditArchive.main()
                audit.assert_called_once()
                self.assertEqual(audit.call_args.args[0].self_import_library, own_output)

    def test_failed_audit_does_not_delete_the_self_import_library(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            private = directory / "private.lib"
            own_output = directory / "neverc.lib"
            private.touch()
            own_output.write_bytes(b"controlled import-library inventory fixture\n")
            arguments = ["audit", "--nm", "controlled", "--archive", str(private),
                         "--host-lib-dir", str(directory),
                         "--self-import-library", str(own_output)]
            with mock.patch.object(sys, "argv", arguments), \
                    mock.patch.object(AuditArchive, "audit",
                                      side_effect=OSError("inspection failed")):
                with self.assertRaisesRegex(SystemExit, "inspection failed"):
                    AuditArchive.main()
            self.assertFalse(private.exists())
            self.assertEqual(own_output.read_bytes(),
                             b"controlled import-library inventory fixture\n")

    def test_scan_excludes_private_dependency_tree(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            (directory / "nested").mkdir()
            (directory / "_deps").mkdir()
            host = directory / "nested" / "host.lib"
            host.touch()
            (directory / "_deps" / "upstream.a").touch()
            private = directory / "private.a"
            private.touch()
            self.assertEqual(AuditArchive.host_archives(directory, private), [host])

    def check_collision(self, private_kind):
        args = argparse.Namespace(nm="controlled-nm", nm_file=None,
                                  archive=Path("private.a"), prefix_header=None,
                                  host_lib_dir=Path("host-libs"))

        def inventory(_nm, paths, *options):
            if paths == [args.archive]:
                if "--format=posix" in options:
                    return ("neverc_cpp_frontend_main T 0 0\n"
                            f"host_counter {private_kind} 0 0\n")
                return "neverc_cpp_frontend_main\nhost_counter\n"
            if "--format=posix" in options:
                return "host_counter D 0 0\n"
            return "host_counter\n"

        with mock.patch.object(AuditArchive, "nm_output", side_effect=inventory), \
                mock.patch.object(AuditArchive, "host_archives",
                                  return_value=[Path("host.a")]):
            with self.assertRaisesRegex(ValueError, "intersection: host_counter"):
                AuditArchive.audit(args)

    def test_private_definition_cannot_overlap_host(self):
        self.check_collision("D")

    def test_private_reference_cannot_bind_to_host(self):
        self.check_collision("U")

    def test_private_weak_reference_cannot_bind_to_host(self):
        self.check_collision("w")

    def test_host_reader_is_separate_from_private_reader(self):
        args = argparse.Namespace(nm="private-llvm-nm20", nm_file=None,
                                  archive=Path("private.a"), prefix_header=None,
                                  host_lib_dir=Path("host-libs"),
                                  host_format="nm", host_nm="host-llvm-nm22")

        def inventory(reader, paths, *options):
            if paths == [args.archive]:
                self.assertEqual(reader, "private-llvm-nm20")
                return ("neverc_cpp_frontend_main T 0 0\n" if "--format=posix" in options
                        else "neverc_cpp_frontend_main\n")
            self.assertEqual(reader, "host-llvm-nm22")
            return ("host_only_function T 0 0\n" if "--format=posix" in options
                    else "host_only_function\n")

        with mock.patch.object(AuditArchive, "nm_output", side_effect=inventory), \
                mock.patch.object(AuditArchive, "host_archives",
                                  return_value=[Path("host.a")]):
            AuditArchive.audit(args)

    def test_raw_clang_namespace_matches_without_demangling_ltcg(self):
        for name in ("?foo@Decl@clang@@QAEXXZ", "_ZN5clang4Decl3fooEv"):
            self.assertTrue(AuditArchive.has_raw_clang_name(name))
        for name in ("?foo@neverc@@YAXXZ", "_Z15clangSomethingv"):
            self.assertFalse(AuditArchive.has_raw_clang_name(name))

    def test_fallback_reader_failure_requires_compatible_host_tool(self):
        args = argparse.Namespace(nm="private-llvm-nm20", nm_file=None,
                                  archive=Path("private.a"), prefix_header=None,
                                  host_lib_dir=Path("host-libs"),
                                  host_format="nm", host_nm=None)

        def inventory(reader, paths, *options):
            self.assertEqual(reader, "private-llvm-nm20")
            if paths == [args.archive]:
                return ("neverc_cpp_frontend_main T 0 0\n" if "--format=posix" in options
                        else "neverc_cpp_frontend_main\n")
            raise OSError("unsupported host object")

        with mock.patch.object(AuditArchive, "nm_output", side_effect=inventory), \
                mock.patch.object(AuditArchive, "host_archives",
                                  return_value=[Path("host.a")]):
            with self.assertRaisesRegex(ValueError, "NEVERC_CPP_HOST_NM"):
                AuditArchive.audit(args)

    def test_coff_index_definitions_still_catch_private_references(self):
        args = argparse.Namespace(nm="private-llvm-nm20", nm_file=None,
                                  archive=Path("private.a"), prefix_header=None,
                                  host_lib_dir=Path("host-libs"),
                                  host_format="coff-index", host_nm=None)
        reader = types.ModuleType("HostCoffSymbols")
        reader.read_defined_symbols = mock.Mock(return_value={"host_counter"})

        def inventory(_nm, paths, *options):
            # The proprietary host objects must never reach llvm-nm.
            self.assertEqual(paths, [args.archive])
            return ("neverc_cpp_frontend_main T 0 0\nhost_counter U 0 0\n"
                    if "--format=posix" in options else "neverc_cpp_frontend_main\nhost_counter\n")

        with mock.patch.dict(sys.modules, {"HostCoffSymbols": reader}), \
                mock.patch.object(AuditArchive, "nm_output", side_effect=inventory), \
                mock.patch.object(AuditArchive, "host_archives",
                                  return_value=[Path("host.lib")]):
            with self.assertRaisesRegex(ValueError, "intersection: host_counter"):
                AuditArchive.audit(args)
        self.assertEqual(reader.read_defined_symbols.call_args_list,
                         [mock.call(Path("host.lib")), mock.call(Path("host.lib"))])

    def test_corrupt_coff_index_is_never_treated_as_no_definitions(self):
        args = argparse.Namespace(nm="private-llvm-nm20", nm_file=None,
                                  archive=Path("private.a"), prefix_header=None,
                                  host_lib_dir=Path("host-libs"),
                                  host_format="coff-index", host_nm=None)
        reader = types.ModuleType("HostCoffSymbols")
        reader.read_defined_symbols = mock.Mock(side_effect=ValueError("bad index"))

        def inventory(_nm, _paths, *options):
            return ("neverc_cpp_frontend_main T 0 0\n" if "--format=posix" in options
                    else "neverc_cpp_frontend_main\n")

        with mock.patch.dict(sys.modules, {"HostCoffSymbols": reader}), \
                mock.patch.object(AuditArchive, "nm_output",
                                  side_effect=inventory), \
                mock.patch.object(AuditArchive, "host_archives",
                                  return_value=[Path("host.lib")]):
            with self.assertRaisesRegex(ValueError, "bad index"):
                AuditArchive.audit(args)

    def test_inspection_failure_invalidates_aggregate(self):
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "private.a"
            archive.touch()
            with mock.patch.object(sys, "argv", ["audit", "--nm", "controlled",
                                                 "--archive", str(archive)]), \
                    mock.patch.object(AuditArchive, "audit",
                                      side_effect=OSError("inspection failed")):
                with self.assertRaisesRegex(SystemExit, "inspection failed"):
                    AuditArchive.main()
            self.assertFalse(archive.exists())


if __name__ == "__main__":
    unittest.main()
