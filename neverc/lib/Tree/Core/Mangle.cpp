#include "neverc/Tree/Core/Mangle.h"
#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Tree/Core/Attr.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Decl/DeclC.h"
#include "neverc/Tree/Type/Type.h"
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

  if (const auto *CAT = dyn_cast<ConstantArrayType>(Ty)) {
    Out << "A" << CAT->getSize() << "_";
    mangleSimpleTypeName(CAT->getElementType(), Out);
    return;
  }

  std::string TypeStr;
  llvm::raw_string_ostream TOS(TypeStr);
  T.print(TOS, PrintingPolicy{LangOptions()});
  Out << TypeStr.size() << TypeStr;
}

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
