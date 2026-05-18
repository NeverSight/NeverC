#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Core/TokenKinds.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Tree/Core/Attr.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Expr/Expr.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/SaveAndRestore.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <string>

using namespace neverc;

// ===----------------------------------------------------------------------===
// Type printing
// ===----------------------------------------------------------------------===

namespace {

class ParamPolicyRAII {
  PrintingPolicy &Policy;
  bool Old;

public:
  explicit ParamPolicyRAII(PrintingPolicy &Policy)
      : Policy(Policy), Old(Policy.SuppressSpecifiers) {
    Policy.SuppressSpecifiers = false;
  }

  ~ParamPolicyRAII() { Policy.SuppressSpecifiers = Old; }
};

class ElaboratedTypePolicyRAII {
  PrintingPolicy &Policy;
  bool SuppressTagKeyword;
  bool SuppressScope;

public:
  explicit ElaboratedTypePolicyRAII(PrintingPolicy &Policy) : Policy(Policy) {
    SuppressTagKeyword = Policy.SuppressTagKeyword;
    SuppressScope = Policy.SuppressScope;
    Policy.SuppressTagKeyword = true;
    Policy.SuppressScope = true;
  }

  ~ElaboratedTypePolicyRAII() {
    Policy.SuppressTagKeyword = SuppressTagKeyword;
    Policy.SuppressScope = SuppressScope;
  }
};

class TypePrinter {
  PrintingPolicy Policy;
  unsigned Indentation;
  bool HasEmptyPlaceHolder = false;
  bool InsideCCAttribute = false;

public:
  explicit TypePrinter(const PrintingPolicy &Policy, unsigned Indentation = 0)
      : Policy(Policy), Indentation(Indentation) {}

  void print(const Type *ty, Qualifiers qs, llvm::raw_ostream &OS,
             llvm::StringRef PlaceHolder);
  void print(QualType T, llvm::raw_ostream &OS, llvm::StringRef PlaceHolder);

  static bool canPrefixQualifiers(const Type *T);
  void spaceBeforePlaceHolder(llvm::raw_ostream &OS);
  void printTypeSpec(NamedDecl *D, llvm::raw_ostream &OS);

  void printBefore(QualType T, llvm::raw_ostream &OS);
  void printAfter(QualType T, llvm::raw_ostream &OS);
  void AppendScope(DeclContext *DC, llvm::raw_ostream &OS);
  void printTag(TagDecl *T, llvm::raw_ostream &OS);
  void printFunctionAfter(const FunctionType::ExtInfo &Info,
                          llvm::raw_ostream &OS);
#define ABSTRACT_TYPE(CLASS, PARENT)
#define TYPE(CLASS, PARENT)                                                    \
  void print##CLASS##Before(const CLASS##Type *T, llvm::raw_ostream &OS);      \
  void print##CLASS##After(const CLASS##Type *T, llvm::raw_ostream &OS);
#include "neverc/Tree/TypeNodes.td.h"

private:
  void printBefore(const Type *ty, Qualifiers qs, llvm::raw_ostream &OS);
  void printAfter(const Type *ty, Qualifiers qs, llvm::raw_ostream &OS);
};

} // namespace

namespace {
void appendTypeQualList(llvm::raw_ostream &OS, unsigned TypeQuals,
                        bool HasRestrictKeyword) {
  bool appendSpace = false;
  if (TypeQuals & Qualifiers::Const) {
    OS << tok::getKeywordSpelling(tok::kw_const);
    appendSpace = true;
  }
  if (TypeQuals & Qualifiers::Volatile) {
    if (appendSpace)
      OS << ' ';
    OS << tok::getKeywordSpelling(tok::kw_volatile);
    appendSpace = true;
  }
  if (TypeQuals & Qualifiers::Restrict) {
    if (appendSpace)
      OS << ' ';
    if (HasRestrictKeyword) {
      OS << tok::getKeywordSpelling(tok::kw_restrict);
    } else {
      OS << "__restrict";
    }
  }
}
} // namespace

void TypePrinter::spaceBeforePlaceHolder(llvm::raw_ostream &OS) {
  if (!HasEmptyPlaceHolder)
    OS << ' ';
}

namespace {
SplitQualType splitAccordingToPolicy(QualType QT,
                                     const PrintingPolicy &Policy) {
  if (Policy.PrintCanonicalTypes)
    QT = QT.getCanonicalType();
  return QT.split();
}
} // namespace

void TypePrinter::print(QualType t, llvm::raw_ostream &OS,
                        llvm::StringRef PlaceHolder) {
  SplitQualType split = splitAccordingToPolicy(t, Policy);
  print(split.Ty, split.Quals, OS, PlaceHolder);
}

void TypePrinter::print(const Type *T, Qualifiers Quals, llvm::raw_ostream &OS,
                        llvm::StringRef PlaceHolder) {
  if (!T) {
    OS << "NULL TYPE";
    return;
  }

  SaveAndRestore PHVal(HasEmptyPlaceHolder, PlaceHolder.empty());

  printBefore(T, Quals, OS);
  OS << PlaceHolder;
  printAfter(T, Quals, OS);
}

bool TypePrinter::canPrefixQualifiers(const Type *T) {
  // CanPrefixQualifiers - We prefer to print type qualifiers before the type,
  // so that we get "const int" instead of "int const", but we can't do this if
  // the type is complex.  For example if the type is "int*", we *must* print
  // "int * const", printing "const int *" is different.  Only do this when the
  // type expands to a simple string.
  bool CanPrefixQualifiers = false;
  const Type *UnderlyingType = T;
  if (const auto *AT = dyn_cast<AutoType>(T))
    UnderlyingType = AT->desugar().getTypePtr();
  Type::TypeClass TC = UnderlyingType->getTypeClass();

  switch (TC) {
  case Type::Auto:
  case Type::Builtin:
  case Type::Complex:
  case Type::Typedef:
  case Type::TypeOfExpr:
  case Type::TypeOf:
  case Type::Record:
  case Type::Enum:
  case Type::Elaborated:
  case Type::Atomic:
  case Type::BitInt:
  case Type::BTFTagAttributed:
    CanPrefixQualifiers = true;
    break;

  case Type::VariableArray:
  case Type::ConstantArray:
  case Type::IncompleteArray:
    return canPrefixQualifiers(
        cast<ArrayType>(UnderlyingType)->getElementType().getTypePtr());

  case Type::Adjusted:
  case Type::Decayed:
  case Type::Pointer:
  case Type::Vector:
  case Type::ExtVector:
  case Type::ConstantMatrix:
  case Type::FunctionProto:
  case Type::FunctionNoProto:
  case Type::Paren:
  case Type::MacroQualified:
    CanPrefixQualifiers = false;
    break;

  case Type::Attributed: {
    const auto *AttrTy = cast<AttributedType>(UnderlyingType);
    CanPrefixQualifiers = AttrTy->getAttrKind() == attr::AddressSpace;
    break;
  }
  }

  return CanPrefixQualifiers;
}

void TypePrinter::printBefore(QualType T, llvm::raw_ostream &OS) {
  SplitQualType Split = splitAccordingToPolicy(T, Policy);
  printBefore(Split.Ty, Split.Quals, OS);
}

void TypePrinter::printBefore(const Type *T, Qualifiers Quals,
                              llvm::raw_ostream &OS) {
  if (Policy.SuppressSpecifiers && T->isSpecifierType())
    return;

  SaveAndRestore PrevPHIsEmpty(HasEmptyPlaceHolder);

  // Print qualifiers as appropriate.

  bool CanPrefixQualifiers = canPrefixQualifiers(T);

  if (CanPrefixQualifiers && !Quals.empty()) {
    Quals.print(OS, Policy, /*appendSpaceIfNonEmpty=*/true);
  }

  bool hasAfterQuals = false;
  if (!CanPrefixQualifiers && !Quals.empty()) {
    hasAfterQuals = !Quals.isEmptyWhenPrinted(Policy);
    if (hasAfterQuals)
      HasEmptyPlaceHolder = false;
  }

  switch (T->getTypeClass()) {
#define ABSTRACT_TYPE(CLASS, PARENT)
#define TYPE(CLASS, PARENT)                                                    \
  case Type::CLASS:                                                            \
    print##CLASS##Before(cast<CLASS##Type>(T), OS);                            \
    break;
#include "neverc/Tree/TypeNodes.td.h"
  }

  if (hasAfterQuals) {
    Quals.print(OS, Policy, /*appendSpaceIfNonEmpty=*/!PrevPHIsEmpty.get());
  }
}

void TypePrinter::printAfter(QualType t, llvm::raw_ostream &OS) {
  SplitQualType split = splitAccordingToPolicy(t, Policy);
  printAfter(split.Ty, split.Quals, OS);
}

void TypePrinter::printAfter(const Type *T, Qualifiers Quals,
                             llvm::raw_ostream &OS) {
  switch (T->getTypeClass()) {
#define ABSTRACT_TYPE(CLASS, PARENT)
#define TYPE(CLASS, PARENT)                                                    \
  case Type::CLASS:                                                            \
    print##CLASS##After(cast<CLASS##Type>(T), OS);                             \
    break;
#include "neverc/Tree/TypeNodes.td.h"
  }
}

void TypePrinter::printBuiltinBefore(const BuiltinType *T,
                                     llvm::raw_ostream &OS) {
  OS << T->getName(Policy);
  spaceBeforePlaceHolder(OS);
}

void TypePrinter::printBuiltinAfter(const BuiltinType *T,
                                    llvm::raw_ostream &OS) {}

void TypePrinter::printComplexBefore(const ComplexType *T,
                                     llvm::raw_ostream &OS) {
  OS << "_Complex ";
  printBefore(T->getElementType(), OS);
}

void TypePrinter::printComplexAfter(const ComplexType *T,
                                    llvm::raw_ostream &OS) {
  printAfter(T->getElementType(), OS);
}

void TypePrinter::printPointerBefore(const PointerType *T,
                                     llvm::raw_ostream &OS) {
  SaveAndRestore NonEmptyPH(HasEmptyPlaceHolder, false);
  printBefore(T->getPointeeType(), OS);
  // Handle things like 'int (*A)[4];' correctly.
  if (isa<ArrayType>(T->getPointeeType()))
    OS << '(';
  OS << '*';
}

void TypePrinter::printPointerAfter(const PointerType *T,
                                    llvm::raw_ostream &OS) {
  SaveAndRestore NonEmptyPH(HasEmptyPlaceHolder, false);
  // Handle things like 'int (*A)[4];' correctly.
  if (isa<ArrayType>(T->getPointeeType()))
    OS << ')';
  printAfter(T->getPointeeType(), OS);
}

void TypePrinter::printConstantArrayBefore(const ConstantArrayType *T,
                                           llvm::raw_ostream &OS) {
  printBefore(T->getElementType(), OS);
}

void TypePrinter::printConstantArrayAfter(const ConstantArrayType *T,
                                          llvm::raw_ostream &OS) {
  OS << '[';
  if (T->getIndexTypeQualifiers().hasQualifiers()) {
    appendTypeQualList(OS, T->getIndexTypeCVRQualifiers(), Policy.Restrict);
    OS << ' ';
  }

  if (T->getSizeModifier() == ArraySizeModifier::Static)
    OS << tok::getKeywordSpelling(tok::kw_static) << ' ';

  OS << T->getSize().getZExtValue() << ']';
  printAfter(T->getElementType(), OS);
}

void TypePrinter::printIncompleteArrayBefore(const IncompleteArrayType *T,
                                             llvm::raw_ostream &OS) {
  printBefore(T->getElementType(), OS);
}

void TypePrinter::printIncompleteArrayAfter(const IncompleteArrayType *T,
                                            llvm::raw_ostream &OS) {
  OS << "[]";
  printAfter(T->getElementType(), OS);
}

void TypePrinter::printVariableArrayBefore(const VariableArrayType *T,
                                           llvm::raw_ostream &OS) {
  printBefore(T->getElementType(), OS);
}

void TypePrinter::printVariableArrayAfter(const VariableArrayType *T,
                                          llvm::raw_ostream &OS) {
  OS << '[';
  if (T->getIndexTypeQualifiers().hasQualifiers()) {
    appendTypeQualList(OS, T->getIndexTypeCVRQualifiers(), Policy.Restrict);
    OS << ' ';
  }

  if (T->getSizeModifier() == ArraySizeModifier::Static)
    OS << tok::getKeywordSpelling(tok::kw_static) << ' ';
  else if (T->getSizeModifier() == ArraySizeModifier::Star)
    OS << '*';

  if (T->getSizeExpr())
    T->getSizeExpr()->printPretty(OS, nullptr, Policy);
  OS << ']';

  printAfter(T->getElementType(), OS);
}

void TypePrinter::printAdjustedBefore(const AdjustedType *T,
                                      llvm::raw_ostream &OS) {
  // Print the adjusted representation, otherwise the adjustment will be
  // invisible.
  printBefore(T->getAdjustedType(), OS);
}

void TypePrinter::printAdjustedAfter(const AdjustedType *T,
                                     llvm::raw_ostream &OS) {
  printAfter(T->getAdjustedType(), OS);
}

void TypePrinter::printDecayedBefore(const DecayedType *T,
                                     llvm::raw_ostream &OS) {
  // Print as though it's a pointer.
  printAdjustedBefore(T, OS);
}

void TypePrinter::printDecayedAfter(const DecayedType *T,
                                    llvm::raw_ostream &OS) {
  printAdjustedAfter(T, OS);
}

void TypePrinter::printVectorBefore(const VectorType *T,
                                    llvm::raw_ostream &OS) {
  switch (T->getVectorKind()) {
  case VectorKind::Neon:
    OS << "__attribute__((neon_vector_type(" << T->getNumElements() << "))) ";
    printBefore(T->getElementType(), OS);
    break;
  case VectorKind::NeonPoly:
    OS << "__attribute__((neon_polyvector_type(" << T->getNumElements()
       << "))) ";
    printBefore(T->getElementType(), OS);
    break;
  case VectorKind::Generic: {
    OS << "__attribute__((__vector_size__(" << T->getNumElements()
       << " * sizeof(";
    print(T->getElementType(), OS, llvm::StringRef());
    OS << ")))) ";
    printBefore(T->getElementType(), OS);
    break;
  }
  case VectorKind::SveFixedLengthData:
  case VectorKind::SveFixedLengthPredicate:
    OS << "__attribute__((__arm_sve_vector_bits__(";

    if (T->getVectorKind() == VectorKind::SveFixedLengthPredicate)
      // Predicates take a bit per byte of the vector size, multiply by 8 to
      // get the number of bits passed to the attribute.
      OS << T->getNumElements() * 8;
    else
      OS << T->getNumElements();

    OS << " * sizeof(";
    print(T->getElementType(), OS, llvm::StringRef());
    // Multiply by 8 for the number of bits.
    OS << ") * 8))) ";
    printBefore(T->getElementType(), OS);
    break;
  }
}

void TypePrinter::printVectorAfter(const VectorType *T, llvm::raw_ostream &OS) {
  printAfter(T->getElementType(), OS);
}

void TypePrinter::printExtVectorBefore(const ExtVectorType *T,
                                       llvm::raw_ostream &OS) {
  printBefore(T->getElementType(), OS);
}

void TypePrinter::printExtVectorAfter(const ExtVectorType *T,
                                      llvm::raw_ostream &OS) {
  printAfter(T->getElementType(), OS);
  OS << " __attribute__((ext_vector_type(";
  OS << T->getNumElements();
  OS << ")))";
}

void TypePrinter::printConstantMatrixBefore(const ConstantMatrixType *T,
                                            llvm::raw_ostream &OS) {
  printBefore(T->getElementType(), OS);
  OS << " __attribute__((matrix_type(";
  OS << T->getNumRows() << ", " << T->getNumColumns();
  OS << ")))";
}

void TypePrinter::printConstantMatrixAfter(const ConstantMatrixType *T,
                                           llvm::raw_ostream &OS) {
  printAfter(T->getElementType(), OS);
}

void FunctionProtoType::printExceptionSpecification(
    llvm::raw_ostream &OS, const PrintingPolicy &) const {
  if (getExceptionSpecType() == EST_NoThrow)
    OS << " __attribute__((nothrow))";
}

void TypePrinter::printFunctionProtoBefore(const FunctionProtoType *T,
                                           llvm::raw_ostream &OS) {
  SaveAndRestore PrevPHIsEmpty(HasEmptyPlaceHolder, false);
  printBefore(T->getReturnType(), OS);
  if (!PrevPHIsEmpty.get())
    OS << '(';
}

void TypePrinter::printFunctionProtoAfter(const FunctionProtoType *T,
                                          llvm::raw_ostream &OS) {
  // If needed for precedence reasons, wrap the inner part in grouping parens.
  if (!HasEmptyPlaceHolder)
    OS << ')';
  SaveAndRestore NonEmptyPH(HasEmptyPlaceHolder, false);

  OS << '(';
  {
    ParamPolicyRAII ParamPolicy(Policy);
    for (unsigned i = 0, e = T->getNumParams(); i != e; ++i) {
      if (i)
        OS << ", ";

      auto EPI = T->getExtParameterInfo(i);
      if (EPI.isNoEscape())
        OS << "__attribute__((noescape)) ";

      print(T->getParamType(i), OS, llvm::StringRef());
    }
  }

  if (T->isVariadic()) {
    if (T->getNumParams())
      OS << ", ";
    OS << "...";
  } else if (T->getNumParams() == 0 && Policy.UseVoidForZeroParams) {
    // Do not emit int() if we have a proto, emit 'int(void)'.
    OS << tok::getKeywordSpelling(tok::kw_void);
  }

  OS << ')';

  FunctionType::ExtInfo Info = T->getExtInfo();

  if ((T->getAArch64SMEAttributes() & FunctionType::SME_PStateSMCompatibleMask))
    OS << " __arm_streaming_compatible";
  if ((T->getAArch64SMEAttributes() & FunctionType::SME_PStateSMEnabledMask))
    OS << " __arm_streaming";
  if ((T->getAArch64SMEAttributes() & FunctionType::SME_PStateZASharedMask))
    OS << " __arm_shared_za";
  if ((T->getAArch64SMEAttributes() & FunctionType::SME_PStateZAPreservedMask))
    OS << " __arm_preserves_za";

  printFunctionAfter(Info, OS);

  if (!T->getMethodQuals().empty())
    OS << " " << T->getMethodQuals().getAsString();

  T->printExceptionSpecification(OS, Policy);

  printAfter(T->getReturnType(), OS);
}

void TypePrinter::printFunctionAfter(const FunctionType::ExtInfo &Info,
                                     llvm::raw_ostream &OS) {
  if (!InsideCCAttribute) {
    switch (Info.getCC()) {
    case CC_C:
      // The C calling convention is the default on the vast majority of
      // platforms we support.  If the user wrote it explicitly, it will usually
      // be printed while traversing the AttributedType.  If the type has been
      // desugared, let the canonical spelling be the implicit calling
      // convention.
      break;
    case CC_X86StdCall:
      OS << " __attribute__((stdcall))";
      break;
    case CC_X86FastCall:
      OS << " __attribute__((fastcall))";
      break;
    case CC_X86VectorCall:
      OS << " __attribute__((vectorcall))";
      break;
    case CC_AArch64VectorCall:
      OS << "__attribute__((aarch64_vector_pcs))";
      break;
    case CC_AArch64SVEPCS:
      OS << "__attribute__((aarch64_sve_pcs))";
      break;

    case CC_Win64:
      OS << " __attribute__((ms_abi))";
      break;
    case CC_X86_64SysV:
      OS << " __attribute__((sysv_abi))";
      break;
    case CC_X86RegCall:
      OS << " __attribute__((regcall))";
      break;

    case CC_PreserveMost:
      OS << " __attribute__((preserve_most))";
      break;
    case CC_PreserveAll:
      OS << " __attribute__((preserve_all))";
      break;
    }
  }

  if (Info.getNoReturn())
    OS << " __attribute__((noreturn))";
  if (Info.getRegParm())
    OS << " __attribute__((regparm (" << Info.getRegParm() << ")))";
  if (Info.getNoCallerSavedRegs())
    OS << " __attribute__((no_caller_saved_registers))";
  if (Info.getNoCfCheck())
    OS << " __attribute__((nocf_check))";
}

void TypePrinter::printFunctionNoProtoBefore(const FunctionNoProtoType *T,
                                             llvm::raw_ostream &OS) {
  // If needed for precedence reasons, wrap the inner part in grouping parens.
  SaveAndRestore PrevPHIsEmpty(HasEmptyPlaceHolder, false);
  printBefore(T->getReturnType(), OS);
  if (!PrevPHIsEmpty.get())
    OS << '(';
}

void TypePrinter::printFunctionNoProtoAfter(const FunctionNoProtoType *T,
                                            llvm::raw_ostream &OS) {
  // If needed for precedence reasons, wrap the inner part in grouping parens.
  if (!HasEmptyPlaceHolder)
    OS << ')';
  SaveAndRestore NonEmptyPH(HasEmptyPlaceHolder, false);

  OS << "()";
  printFunctionAfter(T->getExtInfo(), OS);
  printAfter(T->getReturnType(), OS);
}

void TypePrinter::printTypeSpec(NamedDecl *D, llvm::raw_ostream &OS) {

  // Compute the full nested-name-specifier for this type.
  // In C, this will always be empty except when the type
  // being printed is anonymous within other Record.
  if (!Policy.SuppressScope)
    AppendScope(D->getDeclContext(), OS);

  IdentifierInfo *II = D->getIdentifier();
  OS << II->getName();
  spaceBeforePlaceHolder(OS);
}

void TypePrinter::printTypedefBefore(const TypedefType *T,
                                     llvm::raw_ostream &OS) {
  printTypeSpec(T->getDecl(), OS);
}

void TypePrinter::printMacroQualifiedBefore(const MacroQualifiedType *T,
                                            llvm::raw_ostream &OS) {
  llvm::StringRef MacroName = T->getMacroIdentifier()->getName();
  OS << MacroName << " ";

  // Since this type is meant to print the macro instead of the whole attribute,
  // we trim any attributes and go directly to the original modified type.
  printBefore(T->getModifiedType(), OS);
}

void TypePrinter::printMacroQualifiedAfter(const MacroQualifiedType *T,
                                           llvm::raw_ostream &OS) {
  printAfter(T->getModifiedType(), OS);
}

void TypePrinter::printTypedefAfter(const TypedefType *T,
                                    llvm::raw_ostream &OS) {}

void TypePrinter::printTypeOfExprBefore(const TypeOfExprType *T,
                                        llvm::raw_ostream &OS) {
  OS << (T->getKind() == TypeOfKind::Unqualified ? "typeof_unqual "
                                                 : "typeof ");
  if (T->getUnderlyingExpr())
    T->getUnderlyingExpr()->printPretty(OS, nullptr, Policy);
  spaceBeforePlaceHolder(OS);
}

void TypePrinter::printTypeOfExprAfter(const TypeOfExprType *T,
                                       llvm::raw_ostream &OS) {}

void TypePrinter::printTypeOfBefore(const TypeOfType *T,
                                    llvm::raw_ostream &OS) {
  OS << (T->getKind() == TypeOfKind::Unqualified ? "typeof_unqual("
                                                 : "typeof(");
  print(T->getUnmodifiedType(), OS, llvm::StringRef());
  OS << ')';
  spaceBeforePlaceHolder(OS);
}

void TypePrinter::printTypeOfAfter(const TypeOfType *T, llvm::raw_ostream &OS) {
}

void TypePrinter::printAutoBefore(const AutoType *T, llvm::raw_ostream &OS) {
  // If the type has been deduced, do not print 'auto'.
  if (!T->getDeducedType().isNull()) {
    printBefore(T->getDeducedType(), OS);
  } else {
    switch (T->getKeyword()) {
    case AutoTypeKeyword::Auto:
      OS << tok::getKeywordSpelling(tok::kw_auto);
      break;
    case AutoTypeKeyword::GNUAutoType:
      OS << tok::getKeywordSpelling(tok::kw___auto_type);
      break;
    }
    spaceBeforePlaceHolder(OS);
  }
}

void TypePrinter::printAutoAfter(const AutoType *T, llvm::raw_ostream &OS) {
  // If the type has been deduced, do not print 'auto'.
  if (!T->getDeducedType().isNull())
    printAfter(T->getDeducedType(), OS);
}

void TypePrinter::printAtomicBefore(const AtomicType *T,
                                    llvm::raw_ostream &OS) {
  OS << tok::getKeywordSpelling(tok::kw__Atomic) << '(';
  print(T->getValueType(), OS, llvm::StringRef());
  OS << ')';
  spaceBeforePlaceHolder(OS);
}

void TypePrinter::printAtomicAfter(const AtomicType *T, llvm::raw_ostream &OS) {
}

void TypePrinter::printBitIntBefore(const BitIntType *T,
                                    llvm::raw_ostream &OS) {
  if (T->isUnsigned())
    OS << tok::getKeywordSpelling(tok::kw_unsigned) << ' ';
  OS << tok::getKeywordSpelling(tok::kw__BitInt) << '(' << T->getNumBits()
     << ')';
  spaceBeforePlaceHolder(OS);
}

void TypePrinter::printBitIntAfter(const BitIntType *T, llvm::raw_ostream &OS) {
}

void TypePrinter::AppendScope(DeclContext *DC, llvm::raw_ostream &OS) {
  if (DC->isTranslationUnit())
    return;

  if (DC->isFunctionOrMethod())
    return;

  if (const auto *Tag = dyn_cast<TagDecl>(DC)) {
    AppendScope(DC->getParent(), OS);
    if (TypedefNameDecl *Typedef = Tag->getTypedefNameForAnonDecl())
      OS << Typedef->getIdentifier()->getName() << "::";
    else if (Tag->getIdentifier())
      OS << Tag->getIdentifier()->getName() << "::";
    else
      return;
  } else {
    AppendScope(DC->getParent(), OS);
  }
}

void TypePrinter::printTag(TagDecl *D, llvm::raw_ostream &OS) {
  if (Policy.IncludeTagDefinition) {
    PrintingPolicy SubPolicy{Policy};
    SubPolicy.IncludeTagDefinition = false;
    D->print(OS, SubPolicy, Indentation);
    spaceBeforePlaceHolder(OS);
    return;
  }

  bool HasKindDecoration = false;

  // We don't print tags unless this is an elaborated type.
  // In C, we just assume every RecordType is an elaborated type.
  if (!Policy.SuppressTagKeyword && !D->getTypedefNameForAnonDecl()) {
    HasKindDecoration = true;
    OS << D->getKindName();
    OS << ' ';
  }

  // Compute the full nested-name-specifier for this type.
  // In C, this will always be empty except when the type
  // being printed is anonymous within other Record.
  if (!Policy.SuppressScope)
    AppendScope(D->getDeclContext(), OS);

  if (const IdentifierInfo *II = D->getIdentifier())
    OS << II->getName();
  else if (TypedefNameDecl *Typedef = D->getTypedefNameForAnonDecl()) {
    assert(Typedef->getIdentifier() && "Typedef without identifier?");
    OS << Typedef->getIdentifier()->getName();
  } else {
    // Make an unambiguous representation for anonymous types, e.g.
    //   (anonymous enum at /usr/include/string.h:120:9)
    OS << (Policy.MSVCFormatting ? '`' : '(');

    if (isa<RecordDecl>(D) && cast<RecordDecl>(D)->isAnonymousStructOrUnion()) {
      OS << "anonymous";
    } else {
      OS << "unnamed";
    }

    if (Policy.AnonymousTagLocations) {
      // Suppress the redundant tag keyword if we just printed one.
      // We don't have to worry about ElaboratedTypes here because you can't
      // refer to an anonymous type with one.
      if (!HasKindDecoration)
        OS << " " << D->getKindName();

      PresumedLoc PLoc = D->getTreeContext().getSourceManager().getPresumedLoc(
          D->getLocation());
      if (PLoc.isValid()) {
        OS << " at ";
        llvm::StringRef File = PLoc.getFilename();
        llvm::SmallString<1024> WrittenFile(File);
        if (auto *Callbacks = Policy.Callbacks)
          WrittenFile = Callbacks->remapPath(File);
        // Fix inconsistent path separator created by
        // neverc::PathEntry::ResolveInclude when the file path is relative
        // path.
        llvm::sys::path::Style Style =
            llvm::sys::path::is_absolute(WrittenFile)
                ? llvm::sys::path::Style::native
                : (Policy.MSVCFormatting
                       ? llvm::sys::path::Style::windows_backslash
                       : llvm::sys::path::Style::posix);
        llvm::sys::path::native(WrittenFile, Style);
        OS << WrittenFile << ':' << PLoc.getLine() << ':' << PLoc.getColumn();
      }
    }

    OS << (Policy.MSVCFormatting ? '\'' : ')');
  }

  spaceBeforePlaceHolder(OS);
}

void TypePrinter::printRecordBefore(const RecordType *T,
                                    llvm::raw_ostream &OS) {
  printTag(T->getDecl(), OS);
}

void TypePrinter::printRecordAfter(const RecordType *T, llvm::raw_ostream &OS) {
}

void TypePrinter::printEnumBefore(const EnumType *T, llvm::raw_ostream &OS) {
  printTag(T->getDecl(), OS);
}

void TypePrinter::printEnumAfter(const EnumType *T, llvm::raw_ostream &OS) {}

void TypePrinter::printElaboratedBefore(const ElaboratedType *T,
                                        llvm::raw_ostream &OS) {
  if (Policy.IncludeTagDefinition && T->getOwnedTagDecl()) {
    TagDecl *OwnedTagDecl = T->getOwnedTagDecl();
    assert(OwnedTagDecl->getTypeForDecl() == T->getNamedType().getTypePtr() &&
           "OwnedTagDecl expected to be a declaration for the type");
    PrintingPolicy SubPolicy{Policy};
    SubPolicy.IncludeTagDefinition = false;
    OwnedTagDecl->print(OS, SubPolicy, Indentation);
    spaceBeforePlaceHolder(OS);
    return;
  }

  if (!Policy.IncludeTagDefinition) {
    OS << TypeWithKeyword::getKeywordName(T->getKeyword());
    if (T->getKeyword() != ElaboratedTypeKeyword::None)
      OS << " ";
  }

  ElaboratedTypePolicyRAII PolicyRAII(Policy);
  printBefore(T->getNamedType(), OS);
}

void TypePrinter::printElaboratedAfter(const ElaboratedType *T,
                                       llvm::raw_ostream &OS) {
  if (Policy.IncludeTagDefinition && T->getOwnedTagDecl())
    return;

  ElaboratedTypePolicyRAII PolicyRAII(Policy);
  printAfter(T->getNamedType(), OS);
}

void TypePrinter::printParenBefore(const ParenType *T, llvm::raw_ostream &OS) {
  if (!HasEmptyPlaceHolder && !isa<FunctionType>(T->getInnerType())) {
    printBefore(T->getInnerType(), OS);
    OS << '(';
  } else
    printBefore(T->getInnerType(), OS);
}

void TypePrinter::printParenAfter(const ParenType *T, llvm::raw_ostream &OS) {
  if (!HasEmptyPlaceHolder && !isa<FunctionType>(T->getInnerType())) {
    OS << ')';
    printAfter(T->getInnerType(), OS);
  } else
    printAfter(T->getInnerType(), OS);
}

void TypePrinter::printAttributedBefore(const AttributedType *T,
                                        llvm::raw_ostream &OS) {

  if (T->getAttrKind() == attr::AddressSpace)
    printBefore(T->getEquivalentType(), OS);
  else
    printBefore(T->getModifiedType(), OS);

  if (T->isMSTypeSpec()) {
    switch (T->getAttrKind()) {
    default:
      return;
    case attr::Ptr32:
      OS << " __ptr32";
      break;
    case attr::Ptr64:
      OS << " __ptr64";
      break;
    case attr::SPtr:
      OS << " __sptr";
      break;
    case attr::UPtr:
      OS << " __uptr";
      break;
    }
    spaceBeforePlaceHolder(OS);
  }

  // Print nullability type specifiers.
  if (T->getImmediateNullability()) {
    if (T->getAttrKind() == attr::TypeNonNull)
      OS << " _Nonnull";
    else if (T->getAttrKind() == attr::TypeNullable)
      OS << " _Nullable";
    else if (T->getAttrKind() == attr::TypeNullUnspecified)
      OS << " _Null_unspecified";
    else
      llvm_unreachable("unhandled nullability");
    spaceBeforePlaceHolder(OS);
  }
}

void TypePrinter::printAttributedAfter(const AttributedType *T,
                                       llvm::raw_ostream &OS) {

  // If this is a calling convention attribute, don't print the implicit CC from
  // the modified type.
  SaveAndRestore MaybeSuppressCC(InsideCCAttribute, T->isCallingConv());

  printAfter(T->getModifiedType(), OS);

  // Some attributes are printed as qualifiers before the type, so we have
  // nothing left to do.
  if (T->isMSTypeSpec() || T->getImmediateNullability())
    return;

  // The printing of the address_space attribute is handled by the qualifier
  // since it is still stored in the qualifier. Return early to prevent printing
  // this twice.
  if (T->getAttrKind() == attr::AddressSpace)
    return;

  if (T->getAttrKind() == attr::AnnotateType) {
    OS << " [[neverc::annotate_type(...)]]";
    return;
  }

  if (T->getAttrKind() == attr::ArmStreaming) {
    OS << "__arm_streaming";
    return;
  }
  if (T->getAttrKind() == attr::ArmStreamingCompatible) {
    OS << "__arm_streaming_compatible";
    return;
  }
  if (T->getAttrKind() == attr::ArmSharedZA) {
    OS << "__arm_shared_za";
    return;
  }
  if (T->getAttrKind() == attr::ArmPreservesZA) {
    OS << "__arm_preserves_za";
    return;
  }

  OS << " __attribute__((";
  switch (T->getAttrKind()) {
#define TYPE_ATTR(NAME)
#define DECL_OR_TYPE_ATTR(NAME)
#define ATTR(NAME) case attr::NAME:
#include "neverc/Foundation/AttrList.td.h"
    llvm_unreachable("non-type attribute attached to type");

  case attr::BTFTypeTag:
    llvm_unreachable("BTFTypeTag attribute handled separately");

    break;

  case attr::TypeNonNull:
  case attr::TypeNullable:
  case attr::TypeNullUnspecified:
  case attr::Ptr32:
  case attr::Ptr64:
  case attr::SPtr:
  case attr::UPtr:
  case attr::AddressSpace:
  case attr::AnnotateType:
  case attr::ArmStreaming:
  case attr::ArmStreamingCompatible:
  case attr::ArmSharedZA:
  case attr::ArmPreservesZA:
    llvm_unreachable("This attribute should have been handled already");

  case attr::AnyX86NoCfCheck:
    OS << "nocf_check";
    break;
  case attr::CDecl:
    OS << "cdecl";
    break;
  case attr::FastCall:
    OS << "fastcall";
    break;
  case attr::StdCall:
    OS << "stdcall";
    break;
  case attr::VectorCall:
    OS << "vectorcall";
    break;
  case attr::MSABI:
    OS << "ms_abi";
    break;
  case attr::SysVABI:
    OS << "sysv_abi";
    break;
  case attr::RegCall:
    OS << "regcall";
    break;
  case attr::AArch64VectorPcs:
    OS << "aarch64_vector_pcs";
    break;
  case attr::AArch64SVEPcs:
    OS << "aarch64_sve_pcs";
    break;
  case attr::PreserveMost:
    OS << "preserve_most";
    break;

  case attr::PreserveAll:
    OS << "preserve_all";
    break;
  case attr::NoDeref:
    OS << "noderef";
    break;
  case attr::AcquireHandle:
    OS << "acquire_handle";
    break;
  }
  OS << "))";
}

void TypePrinter::printBTFTagAttributedBefore(const BTFTagAttributedType *T,
                                              llvm::raw_ostream &OS) {
  printBefore(T->getWrappedType(), OS);
  OS << " __attribute__((btf_type_tag(\"" << T->getAttr()->getBTFTypeTag()
     << "\")))";
}

void TypePrinter::printBTFTagAttributedAfter(const BTFTagAttributedType *T,
                                             llvm::raw_ostream &OS) {
  printAfter(T->getWrappedType(), OS);
}

std::string Qualifiers::getAsString() const {
  return getAsString(PrintingPolicy{LangOptions()});
}

// Appends qualifiers to the given string, separated by spaces.  Will
// prefix a space if the string is non-empty.  Will not append a final
// space.
std::string Qualifiers::getAsString(const PrintingPolicy &Policy) const {
  llvm::SmallString<64> Buf;
  llvm::raw_svector_ostream StrOS(Buf);
  print(StrOS, Policy);
  return std::string(StrOS.str());
}

bool Qualifiers::isEmptyWhenPrinted(const PrintingPolicy &Policy) const {
  if (getCVRQualifiers())
    return false;

  if (getAddressSpace() != LangAS::Default)
    return false;

  return true;
}

std::string Qualifiers::getAddrSpaceAsString(LangAS AS) {
  switch (AS) {
  case LangAS::Default:
    return "";
  case LangAS::ptr32_sptr:
    return "__sptr __ptr32";
  case LangAS::ptr32_uptr:
    return "__uptr __ptr32";
  case LangAS::ptr64:
    return "__ptr64";
  default:
    return std::to_string(toTargetAddressSpace(AS));
  }
}

// Appends qualifiers to the given string, separated by spaces.  Will
// prefix a space if the string is non-empty.  Will not append a final
// space.
void Qualifiers::print(llvm::raw_ostream &OS, const PrintingPolicy &Policy,
                       bool appendSpaceIfNonEmpty) const {
  bool addSpace = false;

  unsigned quals = getCVRQualifiers();
  if (quals) {
    appendTypeQualList(OS, quals, Policy.Restrict);
    addSpace = true;
  }
  if (hasUnaligned()) {
    if (addSpace)
      OS << ' ';
    OS << tok::getKeywordSpelling(tok::kw___unaligned);
    addSpace = true;
  }
  auto ASStr = getAddrSpaceAsString(getAddressSpace());
  if (!ASStr.empty()) {
    if (addSpace)
      OS << ' ';
    addSpace = true;
    // Wrap target address space into an attribute syntax
    if (isTargetAddressSpace(getAddressSpace()))
      OS << "__attribute__((address_space(" << ASStr << ")))";
    else
      OS << ASStr;
  }

  if (appendSpaceIfNonEmpty && addSpace)
    OS << ' ';
}

std::string QualType::getAsString() const {
  return getAsString(split(), PrintingPolicy{LangOptions()});
}

std::string QualType::getAsString(const PrintingPolicy &Policy) const {
  std::string S;
  getAsStringInternal(S, Policy);
  return S;
}

std::string QualType::getAsString(const Type *ty, Qualifiers qs,
                                  const PrintingPolicy &Policy) {
  std::string buffer;
  getAsStringInternal(ty, qs, buffer, Policy);
  return buffer;
}

void QualType::print(llvm::raw_ostream &OS, const PrintingPolicy &Policy,
                     const llvm::Twine &PlaceHolder,
                     unsigned Indentation) const {
  print(splitAccordingToPolicy(*this, Policy), OS, Policy, PlaceHolder,
        Indentation);
}

void QualType::print(const Type *ty, Qualifiers qs, llvm::raw_ostream &OS,
                     const PrintingPolicy &policy,
                     const llvm::Twine &PlaceHolder, unsigned Indentation) {
  llvm::SmallString<128> PHBuf;
  llvm::StringRef PH = PlaceHolder.toStringRef(PHBuf);

  TypePrinter(policy, Indentation).print(ty, qs, OS, PH);
}

void QualType::getAsStringInternal(std::string &Str,
                                   const PrintingPolicy &Policy) const {
  return getAsStringInternal(splitAccordingToPolicy(*this, Policy), Str,
                             Policy);
}

void QualType::getAsStringInternal(const Type *ty, Qualifiers qs,
                                   std::string &buffer,
                                   const PrintingPolicy &policy) {
  llvm::SmallString<256> Buf;
  llvm::raw_svector_ostream StrOS(Buf);
  TypePrinter(policy).print(ty, qs, StrOS, buffer);
  std::string str = std::string(StrOS.str());
  buffer.swap(str);
}

llvm::raw_ostream &neverc::operator<<(llvm::raw_ostream &OS, QualType QT) {
  SplitQualType S = QT.split();
  TypePrinter(PrintingPolicy{LangOptions()})
      .print(S.Ty, S.Quals, OS,
             /*PlaceHolder=*/"");
  return OS;
}
