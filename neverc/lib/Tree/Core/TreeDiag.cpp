#include "neverc/Tree/Core/TreeDiag.h"
#include "neverc/Tree/Core/Attr.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Expr/Expr.h"
#include "llvm/Support/raw_ostream.h"

using namespace neverc;

// ===----------------------------------------------------------------------===
// Type desugaring for diagnostics
// ===----------------------------------------------------------------------===

// Returns a desugared version of the QualType, and marks ShouldAKA as true
// whenever we remove significant sugar from the type. Make sure ShouldAKA
// is initialized before passing it in.
QualType neverc::desugarForDiagnostic(TreeContext &Context, QualType QT,
                                      bool &ShouldAKA) {
  QualifierCollector QC;

  while (true) {
    const Type *Ty = QC.strip(QT);

    switch (Ty->getTypeClass()) {
    case Type::Elaborated:
      QT = cast<ElaboratedType>(Ty)->desugar();
      continue;
    case Type::Paren:
      QT = cast<ParenType>(Ty)->desugar();
      continue;
    case Type::MacroQualified:
      QT = cast<MacroQualifiedType>(Ty)->desugar();
      continue;
    case Type::Attributed:
      QT = cast<AttributedType>(Ty)->desugar();
      continue;
    case Type::Adjusted:
    case Type::Decayed: {
      QT = cast<AdjustedType>(Ty)->desugar();
      continue;
    }
    case Type::Auto: {
      const auto *AT = cast<AutoType>(Ty);
      if (!AT->isSugared())
        goto done;
      QT = AT->desugar();
      continue;
    }
    default:
      break;
    }

    // Desugar FunctionType if return type or any parameter type should be
    // desugared. Preserve nullability attribute on desugared types.
    if (const FunctionType *FT = dyn_cast<FunctionType>(Ty)) {
      bool DesugarReturn = false;
      QualType SugarRT = FT->getReturnType();
      QualType RT = desugarForDiagnostic(Context, SugarRT, DesugarReturn);
      if (auto nullability = AttributedType::stripOuterNullability(SugarRT)) {
        RT = Context.getAttributedType(
            AttributedType::getNullabilityAttrKind(*nullability), RT, RT);
      }

      bool DesugarArgument = false;
      llvm::SmallVector<QualType, 4> Args;
      const FunctionProtoType *FPT = dyn_cast<FunctionProtoType>(FT);
      if (FPT) {
        for (QualType SugarPT : FPT->param_types()) {
          QualType PT = desugarForDiagnostic(Context, SugarPT, DesugarArgument);
          if (auto nullability =
                  AttributedType::stripOuterNullability(SugarPT)) {
            PT = Context.getAttributedType(
                AttributedType::getNullabilityAttrKind(*nullability), PT, PT);
          }
          Args.push_back(PT);
        }
      }

      if (DesugarReturn || DesugarArgument) {
        ShouldAKA = true;
        QT = FPT ? Context.getFunctionType(RT, Args, FPT->getExtProtoInfo())
                 : Context.getFunctionNoProtoType(RT, FT->getExtInfo());
        break;
      }
    }

    if (const auto *AT = dyn_cast<ArrayType>(Ty)) {
      QualType ElementTy =
          desugarForDiagnostic(Context, AT->getElementType(), ShouldAKA);
      if (const auto *CAT = dyn_cast<ConstantArrayType>(AT))
        QT = Context.getConstantArrayType(
            ElementTy, CAT->getSize(), CAT->getSizeExpr(),
            CAT->getSizeModifier(), CAT->getIndexTypeCVRQualifiers());
      else if (const auto *VAT = dyn_cast<VariableArrayType>(AT))
        QT = Context.getVariableArrayType(
            ElementTy, VAT->getSizeExpr(), VAT->getSizeModifier(),
            VAT->getIndexTypeCVRQualifiers(), VAT->getBracketsRange());
      else if (const auto *IAT = dyn_cast<IncompleteArrayType>(AT))
        QT = Context.getIncompleteArrayType(ElementTy, IAT->getSizeModifier(),
                                            IAT->getIndexTypeCVRQualifiers());
      else
        llvm_unreachable("Unhandled array type");
      break;
    }

    // Don't desugar magic types.

    // Don't desugar va_list.
    if (QualType(Ty, 0) == Context.getBuiltinVaListType() ||
        QualType(Ty, 0) == Context.getBuiltinMSVaListType())
      break;

    // Otherwise, do a single-step desugar.
    QualType Underlying;
    bool IsSugar = false;
    switch (Ty->getTypeClass()) {
#define ABSTRACT_TYPE(Class, Base)
#define TYPE(Class, Base)                                                      \
  case Type::Class: {                                                          \
    const Class##Type *CTy = cast<Class##Type>(Ty);                            \
    if (CTy->isSugared()) {                                                    \
      IsSugar = true;                                                          \
      Underlying = CTy->desugar();                                             \
    }                                                                          \
    break;                                                                     \
  }
#include "neverc/Tree/TypeNodes.td.h"
    }

    // If it wasn't sugared, we're done.
    if (!IsSugar)
      break;

    // If the desugared type is a vector type, we don't want to expand
    // it, it will turn into an attribute mess. People want their "vec4".
    if (isa<VectorType>(Underlying))
      break;

    // Don't desugar through the primary typedef of an anonymous type.
    if (const TagType *UTT = Underlying->getAs<TagType>())
      if (const TypedefType *QTT = dyn_cast<TypedefType>(QT))
        if (UTT->getDecl()->getTypedefNameForAnonDecl() == QTT->getDecl())
          break;

    // Record that we actually looked through an opaque type here.
    ShouldAKA = true;
    QT = Underlying;
  }
done:

  // If we have a pointer-like type, desugar the pointee as well.
  if (const PointerType *Ty = QT->getAs<PointerType>()) {
    QT = Context.getPointerType(
        desugarForDiagnostic(Context, Ty->getPointeeType(), ShouldAKA));
  }

  return QC.apply(Context, QT);
}

// ===----------------------------------------------------------------------===
// Type formatting (with optional a.k.a. clauses)
// ===----------------------------------------------------------------------===

namespace {
std::string formatTypeForDiagnostic(
    TreeContext &Context, QualType Ty,
    llvm::ArrayRef<DiagnosticsEngine::ArgumentValue> PrevArgs,
    llvm::ArrayRef<intptr_t> QualTypeVals) {
  bool ForceAKA = false;
  QualType CanTy = Ty.getCanonicalType();
  std::string S = Ty.getAsString(Context.getPrintingPolicy());
  std::string CanS = CanTy.getAsString(Context.getPrintingPolicy());

  for (const intptr_t &QualTypeVal : QualTypeVals) {
    QualType CompareTy =
        QualType::getFromOpaquePtr(reinterpret_cast<void *>(QualTypeVal));
    if (CompareTy.isNull())
      continue;
    if (CompareTy == Ty)
      continue; // Same types
    QualType CompareCanTy = CompareTy.getCanonicalType();
    if (CompareCanTy == CanTy)
      continue; // Same canonical types
    std::string CompareS = CompareTy.getAsString(Context.getPrintingPolicy());
    bool ShouldAKA = false;
    QualType CompareDesugar =
        desugarForDiagnostic(Context, CompareTy, ShouldAKA);
    std::string CompareDesugarStr =
        CompareDesugar.getAsString(Context.getPrintingPolicy());
    if (CompareS != S && CompareDesugarStr != S)
      continue; // The type string is different than the comparison string
                // and the desugared comparison string.
    std::string CompareCanS =
        CompareCanTy.getAsString(Context.getPrintingPolicy());

    if (CompareCanS == CanS)
      continue; // No new info from canonical type

    ForceAKA = true;
    break;
  }

  // Check to see if we already desugared this type in this
  // diagnostic.  If so, don't do it again.
  bool Repeated = false;
  for (const auto &PrevArg : PrevArgs) {
    if (PrevArg.first == DiagnosticsEngine::ak_qualtype) {
      QualType PrevTy(
          QualType::getFromOpaquePtr(reinterpret_cast<void *>(PrevArg.second)));
      if (PrevTy == Ty) {
        Repeated = true;
        break;
      }
    }
  }

  // Consider producing an a.k.a. clause if removing all the direct
  // sugar gives us something "significantly different".
  if (!Repeated) {
    bool ShouldAKA = false;
    QualType DesugaredTy = desugarForDiagnostic(Context, Ty, ShouldAKA);
    if (ShouldAKA || ForceAKA) {
      if (DesugaredTy == Ty) {
        DesugaredTy = Ty.getCanonicalType();
      }
      std::string akaStr = DesugaredTy.getAsString(Context.getPrintingPolicy());
      if (akaStr != S) {
        S = "'" + S + "' (aka '" + akaStr + "')";
        return S;
      }
    }

    // Give some additional info on vector types. These are either not desugared
    // or displaying complex __attribute__ expressions so add details of the
    // type and element count.
    if (const auto *VTy = Ty->getAs<VectorType>()) {
      std::string DecoratedString;
      llvm::raw_string_ostream OS(DecoratedString);
      const char *Values = VTy->getNumElements() > 1 ? "values" : "value";
      OS << "'" << S << "' (vector of " << VTy->getNumElements() << " '"
         << VTy->getElementType().getAsString(Context.getPrintingPolicy())
         << "' " << Values << ")";
      return DecoratedString;
    }
  }

  S = "'" + S + "'";
  return S;
}
} // namespace

// ===----------------------------------------------------------------------===
// FormatASTNodeDiagnosticArgument: dispatch on argument kind
// ===----------------------------------------------------------------------===

void neverc::FormatASTNodeDiagnosticArgument(
    DiagnosticsEngine::ArgumentKind Kind, intptr_t Val,
    llvm::StringRef Modifier, llvm::StringRef Argument,
    llvm::ArrayRef<DiagnosticsEngine::ArgumentValue> PrevArgs,
    llvm::SmallVectorImpl<char> &Output, void *Cookie,
    llvm::ArrayRef<intptr_t> QualTypeVals) {
  TreeContext &Context = *static_cast<TreeContext *>(Cookie);

  size_t OldEnd = Output.size();
  llvm::raw_svector_ostream OS(Output);
  bool NeedQuotes = true;

  switch (Kind) {
  default:
    llvm_unreachable("unknown ArgumentKind");
  case DiagnosticsEngine::ak_addrspace: {
    assert(Modifier.empty() && Argument.empty() &&
           "Invalid modifier for Qualifiers argument");

    auto S = Qualifiers::getAddrSpaceAsString(static_cast<LangAS>(Val));
    if (S.empty()) {
      OS << "generic";
      OS << " address space";
    } else {
      OS << "address space";
      OS << " '" << S << "'";
    }
    NeedQuotes = false;
    break;
  }
  case DiagnosticsEngine::ak_qual: {
    assert(Modifier.empty() && Argument.empty() &&
           "Invalid modifier for Qualifiers argument");

    Qualifiers Q(Qualifiers::fromOpaqueValue(Val));
    auto S = Q.getAsString();
    if (S.empty()) {
      OS << "unqualified";
      NeedQuotes = false;
    } else {
      OS << S;
    }
    break;
  }
  case DiagnosticsEngine::ak_qualtype_pair: {
    TypeDiffInfo &TDT = *reinterpret_cast<TypeDiffInfo *>(Val);
    Val = TDT.PrintFromType ? TDT.FromType : TDT.ToType;
    Modifier = llvm::StringRef();
    Argument = llvm::StringRef();
    [[fallthrough]];
  }
  case DiagnosticsEngine::ak_qualtype: {
    assert(Modifier.empty() && Argument.empty() &&
           "Invalid modifier for QualType argument");

    QualType Ty(QualType::getFromOpaquePtr(reinterpret_cast<void *>(Val)));
    OS << formatTypeForDiagnostic(Context, Ty, PrevArgs, QualTypeVals);
    NeedQuotes = false;
    break;
  }
  case DiagnosticsEngine::ak_declarationname: {
    assert(Modifier.empty() && Argument.empty() &&
           "Invalid modifier for DeclarationName argument");
    OS << DeclarationName::getFromOpaqueInteger(Val);
    break;
  }
  case DiagnosticsEngine::ak_nameddecl: {
    bool Qualified;
    if (Modifier == "q" && Argument.empty())
      Qualified = true;
    else {
      assert(Modifier.empty() && Argument.empty() &&
             "Invalid modifier for NamedDecl* argument");
      Qualified = false;
    }
    const NamedDecl *ND = reinterpret_cast<const NamedDecl *>(Val);
    ND->getNameForDiagnostic(OS, Context.getPrintingPolicy(), Qualified);
    break;
  }
  case DiagnosticsEngine::ak_declcontext: {
    DeclContext *DC = reinterpret_cast<DeclContext *>(Val);
    assert(DC && "Should never have a null declaration context");
    NeedQuotes = false;

    if (DC->isTranslationUnit()) {
      OS << "the global scope";
    } else if (TypeDecl *Type = dyn_cast<TypeDecl>(DC)) {
      OS << formatTypeForDiagnostic(Context, Context.getTypeDeclType(Type),
                                    PrevArgs, QualTypeVals);
    } else {
      assert(isa<NamedDecl>(DC) && "Expected a NamedDecl");
      NamedDecl *ND = cast<NamedDecl>(DC);
      if (isa<FunctionDecl>(ND))
        OS << "function ";

      OS << '\'';
      ND->getNameForDiagnostic(OS, Context.getPrintingPolicy(), true);
      OS << '\'';
    }
    break;
  }
  case DiagnosticsEngine::ak_attr: {
    const Attr *At = reinterpret_cast<Attr *>(Val);
    assert(At && "Received null Attr object!");
    OS << '\'' << At->getSpelling() << '\'';
    NeedQuotes = false;
    break;
  }
  }

  if (NeedQuotes) {
    Output.insert(Output.begin() + OldEnd, '\'');
    Output.push_back('\'');
  }
}
