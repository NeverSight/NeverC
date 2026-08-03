#include "neverc/Tree/Core/Mangle.h"
#include "neverc/Foundation/Core/Linkage.h"
#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Tree/Core/Attr.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Decl/Decl.h"
#include "neverc/Tree/Decl/DeclarationName.h"
#include "neverc/Tree/Decl/DeclC.h"
#include "neverc/Tree/Type/Type.h"
#include "neverc/Tree/Core/PrettyPrinter.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Support/raw_ostream.h"

using namespace neverc;

void MangleContext::anchor() {}

// ===----------------------------------------------------------------------===
// Calling convention -> mangling-flavor classification
// ===----------------------------------------------------------------------===

enum CCMangling { CCM_Other, CCM_Fast, CCM_RegCall, CCM_Vector, CCM_Std };

namespace {
CCMangling classifyCallingConvMangling(const TreeContext &Context,
                                       const NamedDecl *ND) {
  const TargetInfo &TI = Context.getTargetInfo();
  const llvm::Triple &Triple = TI.getTriple();

  if (!Triple.isOSWindows() || !Triple.isX86())
    return CCM_Other;

  const FunctionDecl *FD = dyn_cast<FunctionDecl>(ND);
  if (!FD)
    return CCM_Other;
  QualType T = FD->getType();

  const FunctionType *FT = T->castAs<FunctionType>();

  CallingConv CC = FT->getCallConv();
  switch (CC) {
  default:
    return CCM_Other;
  case CC_X86FastCall:
    return CCM_Fast;
  case CC_X86StdCall:
    return CCM_Std;
  case CC_X86VectorCall:
    return CCM_Vector;
  }
}
} // namespace

namespace {
// Defined later with mangleSimpleTypeName (same TU anonymous namespace).
void mangleCXXName(const TreeContext &Ctx, const NamedDecl *D,
                   llvm::raw_ostream &Out);
} // namespace

// ===----------------------------------------------------------------------===
// MangleContext: high-level name mangling entry points
// ===----------------------------------------------------------------------===

bool MangleContext::shouldMangleDeclName(const NamedDecl *D) {
  const TreeContext &TreeContext = getTreeContext();

  CCMangling CC = classifyCallingConvMangling(TreeContext, D);
  if (CC != CCM_Other)
    return true;

  if (isUniqueInternalLinkageDecl(D))
    return true;

  // C++: mangle non-C-linkage functions and variables with internal/external
  // linkage that are not in an extern "C" context. Main is never mangled.
  if (TreeContext.getLangOpts().CPlusPlus) {
    if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
      if (FD->isMain())
        return false;
      // extern "C" functions keep unmangled names.
      if (FD->getLanguageLinkage() == CLanguageLinkage)
        return false;
      return true;
    }
    if (const auto *VD = dyn_cast<VarDecl>(D)) {
      if (VD->getLanguageLinkage() == CLanguageLinkage)
        return false;
      // Mangling needed for namespace-scope / static data members later.
      if (VD->getDeclContext() && !VD->getDeclContext()->isTranslationUnit())
        return true;
    }
  }

  if (!D->hasAttrs())
    return false;

  if (D->hasAttr<AsmLabelAttr>())
    return true;

  return false;
}

void MangleContext::mangleName(GlobalDecl GD, llvm::raw_ostream &Out) {
  const TreeContext &TreeContext = getTreeContext();
  const NamedDecl *D = cast<NamedDecl>(GD.getDecl());

  if (const AsmLabelAttr *ALA = D->getAttr<AsmLabelAttr>()) {
    if (!ALA->getIsLiteralLabel() || ALA->getLabel().starts_with("llvm.")) {
      Out << ALA->getLabel();
      return;
    }

    llvm::StringRef UserLabelPrefix =
        getTreeContext().getTargetInfo().getUserLabelPrefix();
#ifndef NDEBUG
    char GlobalPrefix =
        llvm::DataLayout(getTreeContext().getTargetInfo().getDataLayoutString())
            .getGlobalPrefix();
    assert((UserLabelPrefix.empty() && !GlobalPrefix) ||
           (UserLabelPrefix.size() == 1 && UserLabelPrefix[0] == GlobalPrefix));
#endif
    if (!UserLabelPrefix.empty())
      Out << '\01';

    Out << ALA->getLabel();
    return;
  }

  CCMangling CC = classifyCallingConvMangling(TreeContext, D);

  const TargetInfo &TI = Context.getTargetInfo();
  if (CC == CCM_Other) {
    // C++ Itanium-style mangling for NeverC ABI v1.
    if (TreeContext.getLangOpts().CPlusPlus && shouldMangleDeclName(D)) {
      mangleCXXName(TreeContext, D, Out);
      return;
    }
    IdentifierInfo *II = D->getIdentifier();
    assert(II && "Attempt to mangle unnamed decl.");
    Out << II->getName();
    return;
  }

  Out << '\01';
  if (CC == CCM_Std)
    Out << '_';
  else if (CC == CCM_Fast)
    Out << '@';
  else if (CC == CCM_RegCall) {
    Out << "__regcall3__";
  }

  Out << D->getIdentifier()->getName();

  const FunctionDecl *FD = cast<FunctionDecl>(D);
  const FunctionType *FT = FD->getType()->castAs<FunctionType>();
  const FunctionProtoType *Proto = dyn_cast<FunctionProtoType>(FT);
  if (CC == CCM_Vector)
    Out << '@';
  Out << '@';
  if (!Proto) {
    Out << '0';
    return;
  }
  assert(!Proto->isVariadic());
  unsigned ArgWords = 0;
  uint64_t DefaultPtrWidth = TI.getPointerWidth(LangAS::Default);
  for (const auto &AT : Proto->param_types()) {
    if (AT->isIncompleteType())
      break;
    ArgWords += llvm::alignTo(TreeContext.getTypeSize(AT), DefaultPtrWidth) /
                DefaultPtrWidth;
  }
  Out << ((DefaultPtrWidth / 8) * ArgWords);
}

// ===----------------------------------------------------------------------===
// Minimal Itanium-style mangler (used for type RTTI strings & SEH helpers)
// ===----------------------------------------------------------------------===
// Type / C++ name mangling helpers (NeverC ABI v1, Itanium-inspired)
// ===----------------------------------------------------------------------===

namespace {

void mangleSimpleTypeName(QualType T, llvm::raw_ostream &Out) {
  T = T.getCanonicalType();
  const Type *Ty = T.getTypePtr();

  if (const auto *BT = dyn_cast<BuiltinType>(Ty)) {
    switch (BT->getKind()) {
    case BuiltinType::Void:
      Out << "v";
      return;
    case BuiltinType::Bool:
      Out << "b";
      return;
    case BuiltinType::Char_U:
    case BuiltinType::Char_S:
      Out << "c";
      return;
    case BuiltinType::UChar:
      Out << "h";
      return;
    case BuiltinType::SChar:
      Out << "a";
      return;
    case BuiltinType::Short:
      Out << "s";
      return;
    case BuiltinType::UShort:
      Out << "t";
      return;
    case BuiltinType::Int:
      Out << "i";
      return;
    case BuiltinType::UInt:
      Out << "j";
      return;
    case BuiltinType::Long:
      Out << "l";
      return;
    case BuiltinType::ULong:
      Out << "m";
      return;
    case BuiltinType::LongLong:
      Out << "x";
      return;
    case BuiltinType::ULongLong:
      Out << "y";
      return;
    case BuiltinType::Int128:
      Out << "n";
      return;
    case BuiltinType::UInt128:
      Out << "o";
      return;
    case BuiltinType::Float:
      Out << "f";
      return;
    case BuiltinType::Double:
      Out << "d";
      return;
    case BuiltinType::LongDouble:
      Out << "e";
      return;
    case BuiltinType::Float128:
      Out << "g";
      return;
    case BuiltinType::WChar_S:
    case BuiltinType::WChar_U:
      Out << "w";
      return;
    case BuiltinType::Char8:
      Out << "Du";
      return;
    case BuiltinType::Char16:
      Out << "Ds";
      return;
    case BuiltinType::Char32:
      Out << "Di";
      return;
    default:
      break;
    }
  }

  if (const auto *TT = dyn_cast<TagType>(Ty)) {
    if (const TagDecl *TD = TT->getDecl()) {
      if (IdentifierInfo *II = TD->getIdentifier()) {
        llvm::StringRef Name = II->getName();
        Out << Name.size() << Name;
        return;
      }
    }
  }

  if (const auto *PT = dyn_cast<PointerType>(Ty)) {
    Out << "P";
    mangleSimpleTypeName(PT->getPointeeType(), Out);
    return;
  }

  if (const auto *RT = dyn_cast<LValueReferenceType>(Ty)) {
    Out << "R";
    mangleSimpleTypeName(RT->getPointeeType(), Out);
    return;
  }

  if (const auto *RRT = dyn_cast<RValueReferenceType>(Ty)) {
    Out << "O";
    mangleSimpleTypeName(RRT->getPointeeType(), Out);
    return;
  }

  if (const auto *CAT = dyn_cast<ConstantArrayType>(Ty)) {
    Out << "A" << CAT->getSize() << "_";
    mangleSimpleTypeName(CAT->getElementType(), Out);
    return;
  }

  if (const auto *AT = dyn_cast<ArrayType>(Ty)) {
    Out << "A_";
    mangleSimpleTypeName(AT->getElementType(), Out);
    return;
  }

  if (const auto *FT = dyn_cast<FunctionProtoType>(Ty)) {
    Out << "F";
    mangleSimpleTypeName(FT->getReturnType(), Out);
    if (FT->getNumParams() == 0)
      Out << "v";
    else
      for (unsigned I = 0, E = FT->getNumParams(); I != E; ++I)
        mangleSimpleTypeName(FT->getParamType(I), Out);
    Out << "E";
    return;
  }

  // Fallback: length-prefixed printed type spelling.
  std::string TypeStr;
  llvm::raw_string_ostream TOS(TypeStr);
  T.print(TOS, PrintingPolicy{LangOptions()});
  Out << TypeStr.size() << TypeStr;
}

// NeverC C++ ABI v1 name mangling (Itanium-inspired, not system-ABI compatible).
static void mangleCXXSourceName(llvm::StringRef Name, llvm::raw_ostream &Out) {
  Out << Name.size() << Name;
}

static const char *mangleOperatorEncoding(OverloadedOperatorKind Op) {
  switch (Op) {
  case OO_New: return "nw";
  case OO_Delete: return "dl";
  case OO_Array_New: return "na";
  case OO_Array_Delete: return "da";
  case OO_Plus: return "pl";
  case OO_Minus: return "mi";
  case OO_Star: return "ml";
  case OO_Slash: return "dv";
  case OO_Percent: return "rm";
  case OO_Caret: return "eo";
  case OO_Amp: return "an";
  case OO_Pipe: return "or";
  case OO_Tilde: return "co";
  case OO_Exclaim: return "nt";
  case OO_Equal: return "aS";
  case OO_Less: return "lt";
  case OO_Greater: return "gt";
  case OO_PlusEqual: return "pL";
  case OO_MinusEqual: return "mI";
  case OO_StarEqual: return "mL";
  case OO_SlashEqual: return "dV";
  case OO_PercentEqual: return "rM";
  case OO_Caretequal: return "eO";
  case OO_Ampequal: return "aN";
  case OO_PipeEqual: return "oR";
  case OO_LessLess: return "ls";
  case OO_GreaterGreater: return "rs";
  case OO_LessLessequal: return "lS";
  case OO_GreaterGreaterequal: return "rS";
  case OO_EqualEqual: return "eq";
  case OO_Exclaimequal: return "ne";
  case OO_Lessequal: return "le";
  case OO_Greaterequal: return "ge";
  case OO_Spaceship: return "ss";
  case OO_AmpAmp: return "aa";
  case OO_PipePipe: return "oo";
  case OO_PlusPlus: return "pp";
  case OO_MinusMinus: return "mm";
  case OO_Comma: return "cm";
  case OO_ArrowStar: return "pm";
  case OO_Arrow: return "pt";
  case OO_Call: return "cl";
  case OO_Subscript: return "ix";
  case OO_Coawait: return "aw";
  default: return "v0"; // unknown / conditional
  }
}

static void mangleCXXUnqualifiedName(const NamedDecl *ND,
                                     llvm::raw_ostream &Out) {
  DeclarationName Name = ND->getDeclName();
  switch (Name.getNameKind()) {
  case DeclarationName::Identifier:
    if (const IdentifierInfo *II = Name.getAsIdentifierInfo())
      mangleCXXSourceName(II->getName(), Out);
    else
      Out << "4anon";
    break;
  case DeclarationName::CXXConstructorName:
    Out << "C1"; // complete object constructor
    break;
  case DeclarationName::CXXDestructorName:
    Out << "D1"; // complete object destructor
    break;
  case DeclarationName::CXXConversionFunctionName:
    Out << "cv";
    // Conversion target type encoding omitted in simplified ABI v1.
    Out << "v";
    break;
  case DeclarationName::CXXOperatorName:
    Out << mangleOperatorEncoding(Name.getCXXOverloadedOperator());
    break;
  case DeclarationName::CXXLiteralOperatorName:
    Out << "li";
    if (const IdentifierInfo *II = Name.getCXXLiteralIdentifier())
      mangleCXXSourceName(II->getName(), Out);
    else
      Out << "4anon";
    break;
  default:
    Out << "4anon";
    break;
  }
}

static void mangleCXXNestedName(const NamedDecl *ND, llvm::raw_ostream &Out) {
  llvm::SmallVector<const NamedDecl *, 8> Prefix;
  const DeclContext *DC = ND->getDeclContext();
  while (DC && !DC->isTranslationUnit()) {
    if (const auto *NS = dyn_cast<NamespaceDecl>(DC)) {
      if (!NS->isAnonymousNamespace())
        Prefix.push_back(NS);
      DC = DC->getParent();
      continue;
    }
    if (const auto *RD = dyn_cast<RecordDecl>(DC)) {
      Prefix.push_back(RD);
      DC = DC->getParent();
      continue;
    }
    if (const auto *FD = dyn_cast<FunctionDecl>(DC)) {
      // Local names: skip nested function contexts for now.
      DC = DC->getParent();
      (void)FD;
      continue;
    }
    DC = DC->getParent();
  }

  if (Prefix.empty()) {
    mangleCXXUnqualifiedName(ND, Out);
    return;
  }

  Out << 'N';
  for (auto It = Prefix.rbegin(), E = Prefix.rend(); It != E; ++It) {
    if (const IdentifierInfo *II = (*It)->getIdentifier())
      mangleCXXSourceName(II->getName(), Out);
    else
      Out << "Ut_";
  }
  mangleCXXUnqualifiedName(ND, Out);
  Out << 'E';
}

static void mangleCXXBareFunctionType(const FunctionDecl *FD,
                                      llvm::raw_ostream &Out) {
  // Constructors/destructors omit return type in Itanium; bare params only.
  const auto *Proto = FD->getType()->getAs<FunctionProtoType>();
  if (!Proto || Proto->getNumParams() == 0) {
    Out << 'v';
    return;
  }
  for (unsigned I = 0, E = Proto->getNumParams(); I != E; ++I)
    mangleSimpleTypeName(Proto->getParamType(I), Out);
}

void mangleCXXName(const TreeContext &Ctx, const NamedDecl *D,
                   llvm::raw_ostream &Out) {
  (void)Ctx;
  Out << "_Z";
  mangleCXXNestedName(D, Out);
  // Variables stop after the name; functions append bare function type.
  if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
    // Skip bare type for ctors/dtors? Still emit params for overloads.
    DeclarationName DN = FD->getDeclName();
    if (DN.getNameKind() != DeclarationName::CXXConstructorName &&
        DN.getNameKind() != DeclarationName::CXXDestructorName)
      mangleCXXBareFunctionType(FD, Out);
    else
      mangleCXXBareFunctionType(FD, Out);
  }
}

} // namespace

namespace {

class MinimalItaniumMangleContextImpl : public ItaniumMangleContext {
public:
  explicit MinimalItaniumMangleContextImpl(TreeContext &Context,
                                           DiagnosticsEngine &Diags)
      : ItaniumMangleContext(Context, Diags) {}

  void mangleSEHFilterExpression(GlobalDecl EnclosingDecl,
                                 llvm::raw_ostream &Out) override {
    Out << "__filt_";
    auto *FD = cast<FunctionDecl>(EnclosingDecl.getDecl());
    if (IdentifierInfo *II = FD->getIdentifier())
      Out << II->getName();
    else
      Out << "_anon";
  }

  void mangleSEHFinallyBlock(GlobalDecl EnclosingDecl,
                             llvm::raw_ostream &Out) override {
    Out << "__fin_";
    auto *FD = cast<FunctionDecl>(EnclosingDecl.getDecl());
    if (IdentifierInfo *II = FD->getIdentifier())
      Out << II->getName();
    else
      Out << "_anon";
  }

  void mangleCanonicalTypeName(QualType T, llvm::raw_ostream &Out) override {
    Out << "_ZTS";
    mangleSimpleTypeName(T, Out);
  }
};

} // anonymous namespace

// ===----------------------------------------------------------------------===
// Factory
// ===----------------------------------------------------------------------===

ItaniumMangleContext *ItaniumMangleContext::create(TreeContext &Context,
                                                   DiagnosticsEngine &Diags) {
  return new MinimalItaniumMangleContextImpl(Context, Diags);
}
