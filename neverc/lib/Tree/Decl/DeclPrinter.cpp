#include "neverc/Foundation/Core/TokenKinds.h"
#include "neverc/Tree/Core/Attr.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Decl/DeclVisitor.h"
#include "neverc/Tree/Expr/Expr.h"
#include "neverc/Tree/Stmt/Stmt.h"
#include "llvm/Support/raw_ostream.h"
using namespace neverc;

// ===----------------------------------------------------------------------===
// Declaration printing
// ===----------------------------------------------------------------------===

namespace {
class DeclPrinter : public DeclVisitor<DeclPrinter> {
  llvm::raw_ostream &Out;
  PrintingPolicy Policy;
  const TreeContext &Context;
  unsigned Indentation;

  llvm::raw_ostream &Indent() { return Indent(Indentation); }
  llvm::raw_ostream &Indent(unsigned Indentation);
  void ProcessDeclGroup(llvm::SmallVectorImpl<Decl *> &Decls);

  enum class AttrPrintLoc {
    None = 0,
    Left = 1,
    Right = 2,
    Any = Left | Right,

    LLVM_MARK_AS_BITMASK_ENUM(/*DefaultValue=*/Any)
  };

  void prettyPrintAttributes(Decl *D, llvm::raw_ostream &out,
                             AttrPrintLoc loc = AttrPrintLoc::Any);

public:
  DeclPrinter(llvm::raw_ostream &Out, const PrintingPolicy &Policy,
              const TreeContext &Context, unsigned Indentation = 0)
      : Out(Out), Policy(Policy), Context(Context), Indentation(Indentation) {}

  void VisitDeclContext(DeclContext *DC, bool Indent = true);

  void VisitTranslationUnitDecl(TranslationUnitDecl *D);
  void VisitTypedefDecl(TypedefDecl *D);
  void VisitEnumDecl(EnumDecl *D);
  void VisitRecordDecl(RecordDecl *D);
  void VisitEnumConstantDecl(EnumConstantDecl *D);
  void VisitEmptyDecl(EmptyDecl *D);
  void VisitFunctionDecl(FunctionDecl *D);
  void VisitFieldDecl(FieldDecl *D);
  void VisitVarDecl(VarDecl *D);
  void VisitLabelDecl(LabelDecl *D);
  void VisitFileScopeAsmDecl(FileScopeAsmDecl *D);

  inline void prettyPrintAttributes(Decl *D) { prettyPrintAttributes(D, Out); }

  void prettyPrintPragmas(Decl *D);
  void printDeclType(QualType T, llvm::StringRef DeclName);
};
} // namespace

void Decl::print(llvm::raw_ostream &Out, unsigned Indentation) const {
  print(Out, getTreeContext().getPrintingPolicy(), Indentation);
}

void Decl::print(llvm::raw_ostream &Out, const PrintingPolicy &Policy,
                 unsigned Indentation) const {
  DeclPrinter Printer(Out, Policy, getTreeContext(), Indentation);
  Printer.Visit(const_cast<Decl *>(this));
}

namespace {
QualType getBaseType(QualType T) {
  QualType BaseType = T;
  while (!BaseType->isSpecifierType()) {
    if (const PointerType *PTy = BaseType->getAs<PointerType>())
      BaseType = PTy->getPointeeType();
    else if (const ArrayType *ATy = dyn_cast<ArrayType>(BaseType))
      BaseType = ATy->getElementType();
    else if (const FunctionType *FTy = BaseType->getAs<FunctionType>())
      BaseType = FTy->getReturnType();
    else if (const VectorType *VTy = BaseType->getAs<VectorType>())
      BaseType = VTy->getElementType();
    else if (const ParenType *PTy = BaseType->getAs<ParenType>())
      BaseType = PTy->desugar();
    else
      // This must be a syntax error.
      break;
  }
  return BaseType;
}

QualType getDeclType(Decl *D) {
  if (TypedefNameDecl *TDD = dyn_cast<TypedefNameDecl>(D))
    return TDD->getUnderlyingType();
  if (ValueDecl *VD = dyn_cast<ValueDecl>(D))
    return VD->getType();
  return QualType();
}
} // namespace

void Decl::printGroup(Decl **Begin, unsigned NumDecls, llvm::raw_ostream &Out,
                      const PrintingPolicy &Policy, unsigned Indentation) {
  if (NumDecls == 1) {
    (*Begin)->print(Out, Policy, Indentation);
    return;
  }

  Decl **End = Begin + NumDecls;
  TagDecl *TD = dyn_cast<TagDecl>(*Begin);
  if (TD)
    ++Begin;

  PrintingPolicy SubPolicy(Policy);

  bool isFirst = true;
  for (; Begin != End; ++Begin) {
    if (isFirst) {
      if (TD)
        SubPolicy.IncludeTagDefinition = true;
      SubPolicy.SuppressSpecifiers = false;
      isFirst = false;
    } else {
      if (!isFirst)
        Out << ", ";
      SubPolicy.IncludeTagDefinition = false;
      SubPolicy.SuppressSpecifiers = true;
    }

    (*Begin)->print(Out, SubPolicy, Indentation);
  }
}

LLVM_DUMP_METHOD void DeclContext::dumpDeclContext() const {
  const DeclContext *DC = this;
  while (!DC->isTranslationUnit())
    DC = DC->getParent();

  TreeContext &Ctx = cast<TranslationUnitDecl>(DC)->getTreeContext();
  DeclPrinter Printer(llvm::errs(), Ctx.getPrintingPolicy(), Ctx, 0);
  Printer.VisitDeclContext(const_cast<DeclContext *>(this), /*Indent=*/false);
}

llvm::raw_ostream &DeclPrinter::Indent(unsigned Indentation) {
  for (unsigned i = 0; i != Indentation; ++i)
    Out << "  ";
  return Out;
}

// For NEVERC_ATTR_LIST_CanPrintOnLeft macro.
#include "neverc/Foundation/AttrLeftSideCanPrintList.td.h"

// For NEVERC_ATTR_LIST_PrintOnLeft macro.
#include "neverc/Foundation/AttrLeftSideMustPrintList.td.h"

namespace {
bool canPrintOnLeftSide(attr::Kind kind) {
#ifdef NEVERC_ATTR_LIST_CanPrintOnLeft
  switch (kind) {
    NEVERC_ATTR_LIST_CanPrintOnLeft return true;
  default:
    return false;
  }
#else
  return false;
#endif
}

bool canPrintOnLeftSide(const Attr *A) {
  if (A->isStandardAttributeSyntax())
    return false;

  return canPrintOnLeftSide(A->getKind());
}

bool mustPrintOnLeftSide(attr::Kind kind) {
#ifdef NEVERC_ATTR_LIST_PrintOnLeft
  switch (kind) {
    NEVERC_ATTR_LIST_PrintOnLeft return true;
  default:
    return false;
  }
#else
  return false;
#endif
}

bool mustPrintOnLeftSide(const Attr *A) {
  if (A->isDeclspecAttribute())
    return true;

  return mustPrintOnLeftSide(A->getKind());
}
} // namespace

void DeclPrinter::prettyPrintAttributes(Decl *D, llvm::raw_ostream &Out,
                                        AttrPrintLoc Loc) {
  if (D->hasAttrs()) {
    AttrVec &Attrs = D->getAttrs();
    for (auto *A : Attrs) {
      if (A->isInherited() || A->isImplicit())
        continue;

      AttrPrintLoc AttrLoc = AttrPrintLoc::Right;
      if (mustPrintOnLeftSide(A)) {
        // If we must always print on left side (e.g. declspec), then mark as
        // so.
        AttrLoc = AttrPrintLoc::Left;
      } else if (canPrintOnLeftSide(A)) {
        // For functions with body defined we print the attributes on the left
        // side so that GCC accept our dumps as well.
        if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(D);
            FD && FD->isThisDeclarationADefinition())
          AttrLoc = AttrPrintLoc::Left;
      }
      // Only print the side matches the user requested.
      if ((Loc & AttrLoc) != AttrPrintLoc::None)
        A->printPretty(Out, Policy);
    }
  }
}

void DeclPrinter::prettyPrintPragmas(Decl *D) {
  if (D->hasAttrs()) {
    AttrVec &Attrs = D->getAttrs();
    for (auto *A : Attrs) {
      switch (A->getKind()) {
#define ATTR(X)
#define PRAGMA_SPELLING_ATTR(X) case attr::X:
#include "neverc/Foundation/AttrList.td.h"
        A->printPretty(Out, Policy);
        Indent();
        break;
      default:
        break;
      }
    }
  }
}

void DeclPrinter::printDeclType(QualType T, llvm::StringRef DeclName) {
  T.print(Out, Policy, DeclName, Indentation);
}

void DeclPrinter::ProcessDeclGroup(llvm::SmallVectorImpl<Decl *> &Decls) {
  this->Indent();
  Decl::printGroup(Decls.data(), Decls.size(), Out, Policy, Indentation);
  Out << ";\n";
  Decls.clear();
}

//----------------------------------------------------------------------------
// Common C declarations
//----------------------------------------------------------------------------

void DeclPrinter::VisitDeclContext(DeclContext *DC, bool Indent) {
  if (Indent)
    Indentation += Policy.Indentation;

  llvm::SmallVector<Decl *, 2> Decls;
  for (DeclContext::decl_iterator D = DC->decls_begin(), DEnd = DC->decls_end();
       D != DEnd; ++D) {

    // Skip over implicit declarations in pretty-printing mode.
    if ((*D)->isImplicit())
      continue;

    // The next bits of code handle stuff like "struct {int x;} a,b"; we're
    // forced to merge the declarations because there's no other way to
    // refer to the struct in question.  When that struct is named instead, we
    // also need to merge to avoid splitting off a stand-alone struct
    // declaration that produces the warning ext_no_declarators in some
    // contexts.
    //
    // This limited merging is safe without a bunch of other checks because it
    // only merges declarations directly referring to the tag, not typedefs.
    //
    // Check whether the current declaration should be grouped with a previous
    // non-free-standing tag declaration.
    QualType CurDeclType = getDeclType(*D);
    if (!Decls.empty() && !CurDeclType.isNull()) {
      QualType BaseType = getBaseType(CurDeclType);
      if (!BaseType.isNull() && isa<ElaboratedType>(BaseType) &&
          cast<ElaboratedType>(BaseType)->getOwnedTagDecl() == Decls[0]) {
        Decls.push_back(*D);
        continue;
      }
    }

    // If we have a merged group waiting to be handled, handle it now.
    if (!Decls.empty())
      ProcessDeclGroup(Decls);

    // If the current declaration is not a free standing declaration, save it
    // so we can merge it with the subsequent declaration(s) using it.
    if (isa<TagDecl>(*D) && !cast<TagDecl>(*D)->isFreeStanding()) {
      Decls.push_back(*D);
      continue;
    }

    this->Indent();
    Visit(*D);

    const char *Terminator = nullptr;
    if (auto FD = dyn_cast<FunctionDecl>(*D)) {
      if (FD->doesThisDeclarationHaveABody())
        Terminator = nullptr;
      else
        Terminator = ";";
    } else if (isa<EnumConstantDecl>(*D)) {
      DeclContext::decl_iterator Next = D;
      ++Next;
      if (Next != DEnd)
        Terminator = ",";
    } else
      Terminator = ";";

    if (Terminator)
      Out << Terminator;
    if (!(isa<FunctionDecl>(*D) &&
          cast<FunctionDecl>(*D)->doesThisDeclarationHaveABody()))
      Out << "\n";
  }

  if (!Decls.empty())
    ProcessDeclGroup(Decls);

  if (Indent)
    Indentation -= Policy.Indentation;
}

void DeclPrinter::VisitTranslationUnitDecl(TranslationUnitDecl *D) {
  VisitDeclContext(D, false);
}

void DeclPrinter::VisitTypedefDecl(TypedefDecl *D) {
  if (!Policy.SuppressSpecifiers) {
    Out << tok::getKeywordSpelling(tok::kw_typedef) << ' ';
  }
  QualType Ty = D->getTypeSourceInfo()->getType();
  Ty.print(Out, Policy, D->getName(), Indentation);
  prettyPrintAttributes(D);
}

void DeclPrinter::VisitEnumDecl(EnumDecl *D) {
  Out << tok::getKeywordSpelling(tok::kw_enum);

  prettyPrintAttributes(D);

  if (D->getDeclName())
    Out << ' ' << D->getDeclName();

  if (D->isFixed())
    Out << " : " << D->getIntegerType().stream(Policy);

  if (D->isCompleteDefinition()) {
    Out << " {\n";
    VisitDeclContext(D);
    Indent() << "}";
  }
}

void DeclPrinter::VisitRecordDecl(RecordDecl *D) {
  Out << D->getKindName();

  prettyPrintAttributes(D);

  if (D->getIdentifier())
    Out << ' ' << *D;

  if (D->isCompleteDefinition()) {
    Out << " {\n";
    VisitDeclContext(D);
    Indent() << "}";
  }
}

void DeclPrinter::VisitEnumConstantDecl(EnumConstantDecl *D) {
  Out << *D;
  prettyPrintAttributes(D);
  if (Expr *Init = D->getInitExpr()) {
    Out << " = ";
    Init->printPretty(Out, nullptr, Policy, Indentation, "\n");
  }
}

void DeclPrinter::VisitFunctionDecl(FunctionDecl *D) {
  prettyPrintPragmas(D);

  std::string LeftsideAttrs;
  llvm::raw_string_ostream LSAS(LeftsideAttrs);

  prettyPrintAttributes(D, LSAS, AttrPrintLoc::Left);

  // prettyPrintAttributes print a space on left side of the attribute.
  if (LeftsideAttrs[0] == ' ') {
    // Skip the space prettyPrintAttributes generated.
    LeftsideAttrs.erase(0, LeftsideAttrs.find_first_not_of(' '));

    // Add a single space between the attribute and the Decl name.
    LSAS << ' ';
  }

  Out << LeftsideAttrs;

  if (!Policy.SuppressSpecifiers) {
    switch (D->getStorageClass()) {
    case SC_None:
      break;
    case SC_Extern:
      Out << tok::getKeywordSpelling(tok::kw_extern) << ' ';
      break;
    case SC_Static:
      Out << tok::getKeywordSpelling(tok::kw_static) << ' ';
      break;
    case SC_PrivateExtern:
      Out << tok::getKeywordSpelling(tok::kw___private_extern__) << ' ';
      break;
    case SC_Auto:
    case SC_Register:
      llvm_unreachable("invalid for functions");
    }

    if (D->isInlineSpecified())
      Out << tok::getKeywordSpelling(tok::kw_inline) << ' ';
  }

  PrintingPolicy SubPolicy(Policy);
  SubPolicy.SuppressSpecifiers = false;
  std::string Proto;

  {
    llvm::raw_string_ostream OS(Proto);
    D->getNameInfo().printName(OS, Policy);
  }

  QualType Ty = D->getType();
  while (const ParenType *PT = dyn_cast<ParenType>(Ty)) {
    Proto = '(' + Proto + ')';
    Ty = PT->getInnerType();
  }

  if (const FunctionType *AFT = Ty->getAs<FunctionType>()) {
    const FunctionProtoType *FT = nullptr;
    if (D->hasWrittenPrototype())
      FT = dyn_cast<FunctionProtoType>(AFT);

    Proto += "(";
    if (FT) {
      llvm::raw_string_ostream POut(Proto);
      DeclPrinter ParamPrinter(POut, SubPolicy, Context, Indentation);
      for (unsigned i = 0, e = D->getNumParams(); i != e; ++i) {
        if (i)
          POut << ", ";
        ParamPrinter.VisitParmVarDecl(D->getParamDecl(i));
      }

      if (FT->isVariadic()) {
        if (D->getNumParams())
          POut << ", ";
        POut << "...";
      } else if (!D->getNumParams()) {
        POut << tok::getKeywordSpelling(tok::kw_void);
      }
    } else if (D->doesThisDeclarationHaveABody() && !D->hasPrototype()) {
      for (unsigned i = 0, e = D->getNumParams(); i != e; ++i) {
        if (i)
          Proto += ", ";
        Proto += D->getParamDecl(i)->getNameAsString();
      }
    }

    Proto += ")";

    AFT->getReturnType().print(Out, Policy, Proto);
    Proto.clear();
    Out << Proto;

  } else {
    Ty.print(Out, Policy, Proto);
  }

  prettyPrintAttributes(D, Out, AttrPrintLoc::Right);

  if (D->doesThisDeclarationHaveABody()) {
    if (!D->hasPrototype() && D->getNumParams()) {
      // This is a K&R function definition, so we need to print the
      // parameters.
      Out << '\n';
      DeclPrinter ParamPrinter(Out, SubPolicy, Context, Indentation);
      Indentation += Policy.Indentation;
      for (unsigned i = 0, e = D->getNumParams(); i != e; ++i) {
        Indent();
        ParamPrinter.VisitParmVarDecl(D->getParamDecl(i));
        Out << ";\n";
      }
      Indentation -= Policy.Indentation;
    }

    if (D->getBody())
      D->getBody()->printPrettyControlled(Out, nullptr, SubPolicy, Indentation,
                                          "\n");
  }
}

void DeclPrinter::VisitFieldDecl(FieldDecl *D) {
  Out << D->getType().stream(Policy, D->getName(), Indentation);

  if (D->isBitField()) {
    Out << " : ";
    D->getBitWidth()->printPretty(Out, nullptr, Policy, Indentation, "\n");
  }

  prettyPrintAttributes(D);
}

void DeclPrinter::VisitLabelDecl(LabelDecl *D) { Out << *D << ":"; }

void DeclPrinter::VisitVarDecl(VarDecl *D) {
  prettyPrintPragmas(D);

  std::string LeftSide;
  llvm::raw_string_ostream LeftSideStream(LeftSide);

  // Print attributes that should be placed on the left, such as __declspec.
  prettyPrintAttributes(D, LeftSideStream, AttrPrintLoc::Left);

  // prettyPrintAttributes print a space on left side of the attribute.
  if (LeftSide[0] == ' ') {
    // Skip the space prettyPrintAttributes generated.
    LeftSide.erase(0, LeftSide.find_first_not_of(' '));

    // Add a single space between the attribute and the Decl name.
    LeftSideStream << ' ';
  }

  Out << LeftSide;

  QualType T =
      D->getTypeSourceInfo() ? D->getTypeSourceInfo()->getType() : D->getType();

  if (!Policy.SuppressSpecifiers) {
    StorageClass SC = D->getStorageClass();
    if (SC != SC_None)
      Out << VarDecl::getStorageClassSpecifierString(SC) << " ";

    switch (D->getTSCSpec()) {
    case TSCS_unspecified:
      break;
    case TSCS___thread:
      Out << tok::getKeywordSpelling(tok::kw___thread) << ' ';
      break;
    case TSCS__Thread_local:
    case TSCS_thread_local:
      Out << tok::getKeywordSpelling(tok::kw__Thread_local) << ' ';
      break;
    }

    if (D->isConstexpr()) {
      Out << tok::getKeywordSpelling(tok::kw_constexpr) << ' ';
      T.removeLocalConst();
    }
  }

  llvm::StringRef Name = D->getName();

  printDeclType(T, Name);

  // Print the attributes that should be placed right before the end of the
  // decl.
  prettyPrintAttributes(D, Out, AttrPrintLoc::Right);

  Expr *Init = D->getInit();
  if (Init) {
    Out << " = ";
    PrintingPolicy SubPolicy(Policy);
    SubPolicy.SuppressSpecifiers = false;
    SubPolicy.IncludeTagDefinition = false;
    Init->printPretty(Out, nullptr, SubPolicy, Indentation, "\n");
  }
}

void DeclPrinter::VisitFileScopeAsmDecl(FileScopeAsmDecl *D) {
  Out << "__asm (";
  D->getAsmString()->printPretty(Out, nullptr, Policy, Indentation, "\n");
  Out << ")";
}

void DeclPrinter::VisitEmptyDecl(EmptyDecl *D) { prettyPrintAttributes(D); }
