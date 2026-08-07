#include "neverc/Tree/Core/Mangle.h"
#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Tree/Core/Attr.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Decl/DeclC.h"
#include "neverc/Tree/Type/Type.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Support/ErrorHandling.h"
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
// Itanium type-name mangler (used for type RTTI strings, KCFI & SEH helpers)
// ===----------------------------------------------------------------------===

namespace {

/// The KCFI ABI hashes the Itanium RTTI spelling of a canonical source type.
/// LLVM IR uses opaque pointers, so this spelling must be produced here while
/// the source type (including tag names and qualifiers) is still available.
/// This is the concrete C subset of Clang's CXXNameMangler, including the one
/// shared substitution sequence used by normalized integers.
class CanonicalItaniumTypeMangler {
  TreeContext &Context;
  llvm::raw_ostream &Out;
  bool NormalizeIntegers;
  llvm::DenseMap<uintptr_t, unsigned> Substitutions;
  llvm::DenseMap<unsigned, unsigned> NormalizedIntegerSubstitutions;
  unsigned SeqID = 0;

  [[noreturn]] void unsupported(llvm::StringRef What) const {
    llvm::report_fatal_error("cannot form canonical Itanium type name for " +
                             What);
  }

  void mangleSeqID(unsigned ID) {
    if (ID == 1) {
      Out << '0';
    } else if (ID > 1) {
      --ID;
      char Buffer[7];
      char *I = std::end(Buffer);
      do {
        const unsigned Digit = ID % 36;
        *--I = Digit < 10 ? '0' + Digit : 'A' + Digit - 10;
        ID /= 36;
      } while (ID);
      Out.write(I, std::end(Buffer) - I);
    }
    Out << '_';
  }

  bool mangleSubstitution(uintptr_t Key) {
    auto It = Substitutions.find(Key);
    if (It == Substitutions.end())
      return false;
    Out << 'S';
    mangleSeqID(It->second);
    return true;
  }

  void addSubstitution(uintptr_t Key) {
    auto Inserted = Substitutions.try_emplace(Key, SeqID);
    assert(Inserted.second && "duplicate Itanium substitution");
    ++SeqID;
  }

  uintptr_t substitutionKey(QualType T) const {
    if (!T.getQualifiers().hasQualifiers()) {
      if (const auto *TT = T->getAs<TagType>())
        return reinterpret_cast<uintptr_t>(TT->getDecl()->getCanonicalDecl());
    }
    return reinterpret_cast<uintptr_t>(T.getAsOpaquePtr());
  }

  void mangleVendorQualifier(llvm::StringRef Name) {
    Out << 'U' << Name.size() << Name;
  }

  void mangleQualifiers(Qualifiers Quals) {
    if (Quals.hasAddressSpace()) {
      llvm::SmallString<24> Name;
      const LangAS AS = Quals.getAddressSpace();
      if (Context.addressSpaceMapManglingFor(AS)) {
        const unsigned TargetAS = Context.getTargetAddressSpace(AS);
        if (TargetAS != 0 ||
            Context.getTargetAddressSpace(LangAS::Default) != 0) {
          Name = "AS";
          Name += llvm::utostr(TargetAS);
        }
      } else {
        switch (AS) {
        case LangAS::Default:
          break;
        case LangAS::ptr32_sptr:
          Name = "ptr32_sptr";
          break;
        case LangAS::ptr32_uptr:
          Name = "ptr32_uptr";
          break;
        case LangAS::ptr64:
          Name = "ptr64";
          break;
        default:
          unsupported("address-space-qualified type");
        }
      }
      if (!Name.empty())
        mangleVendorQualifier(Name);
    }
    if (Quals.hasUnaligned())
      mangleVendorQualifier("__unaligned");
    // Itanium ABI 5.1.5: restrict, volatile, const, in this order.
    if (Quals.hasRestrict())
      Out << 'r';
    if (Quals.hasVolatile())
      Out << 'V';
    if (Quals.hasConst())
      Out << 'K';
  }

  void mangleNormalizedInteger(const BuiltinType *BT) {
    const bool Signed = BT->isSignedInteger();
    const unsigned Width = Context.getTypeSize(QualType(BT, 0));
    unsigned Key;
    llvm::StringRef Encoding;
    switch (Width) {
    case 8:
      Key = Signed ? BuiltinType::SChar : BuiltinType::UChar;
      Encoding = Signed ? "u2i8" : "u2u8";
      break;
    case 16:
      Key = Signed ? BuiltinType::Short : BuiltinType::UShort;
      Encoding = Signed ? "u3i16" : "u3u16";
      break;
    case 32:
      Key = Signed ? BuiltinType::Int : BuiltinType::UInt;
      Encoding = Signed ? "u3i32" : "u3u32";
      break;
    case 64:
      Key = Signed ? BuiltinType::Long : BuiltinType::ULong;
      Encoding = Signed ? "u3i64" : "u3u64";
      break;
    case 128:
      Key = Signed ? BuiltinType::Int128 : BuiltinType::UInt128;
      Encoding = Signed ? "u4i128" : "u4u128";
      break;
    default:
      unsupported("integer with unsupported normalization width");
    }

    auto It = NormalizedIntegerSubstitutions.find(Key);
    if (It != NormalizedIntegerSubstitutions.end()) {
      Out << 'S';
      mangleSeqID(It->second);
      return;
    }
    Out << Encoding;
    NormalizedIntegerSubstitutions.try_emplace(Key, SeqID++);
  }

  void mangleBuiltinType(const BuiltinType *BT) {
    if (NormalizeIntegers && BT->isInteger()) {
      mangleNormalizedInteger(BT);
      return;
    }

    switch (BT->getKind()) {
    case BuiltinType::Void:
      Out << 'v';
      return;
    case BuiltinType::Bool:
      Out << 'b';
      return;
    case BuiltinType::Char_U:
    case BuiltinType::Char_S:
      Out << 'c';
      return;
    case BuiltinType::UChar:
      Out << 'h';
      return;
    case BuiltinType::UShort:
      Out << 't';
      return;
    case BuiltinType::UInt:
      Out << 'j';
      return;
    case BuiltinType::ULong:
      Out << 'm';
      return;
    case BuiltinType::ULongLong:
      Out << 'y';
      return;
    case BuiltinType::UInt128:
      Out << 'o';
      return;
    case BuiltinType::SChar:
      Out << 'a';
      return;
    case BuiltinType::WChar_S:
    case BuiltinType::WChar_U:
      Out << 'w';
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
    case BuiltinType::Short:
      Out << 's';
      return;
    case BuiltinType::Int:
      Out << 'i';
      return;
    case BuiltinType::Long:
      Out << 'l';
      return;
    case BuiltinType::LongLong:
      Out << 'x';
      return;
    case BuiltinType::Int128:
      Out << 'n';
      return;
    case BuiltinType::ShortAccum:
      Out << "DAs";
      return;
    case BuiltinType::Accum:
      Out << "DAi";
      return;
    case BuiltinType::LongAccum:
      Out << "DAl";
      return;
    case BuiltinType::UShortAccum:
      Out << "DAt";
      return;
    case BuiltinType::UAccum:
      Out << "DAj";
      return;
    case BuiltinType::ULongAccum:
      Out << "DAm";
      return;
    case BuiltinType::ShortFract:
      Out << "DRs";
      return;
    case BuiltinType::Fract:
      Out << "DRi";
      return;
    case BuiltinType::LongFract:
      Out << "DRl";
      return;
    case BuiltinType::UShortFract:
      Out << "DRt";
      return;
    case BuiltinType::UFract:
      Out << "DRj";
      return;
    case BuiltinType::ULongFract:
      Out << "DRm";
      return;
    case BuiltinType::SatShortAccum:
      Out << "DSDAs";
      return;
    case BuiltinType::SatAccum:
      Out << "DSDAi";
      return;
    case BuiltinType::SatLongAccum:
      Out << "DSDAl";
      return;
    case BuiltinType::SatUShortAccum:
      Out << "DSDAt";
      return;
    case BuiltinType::SatUAccum:
      Out << "DSDAj";
      return;
    case BuiltinType::SatULongAccum:
      Out << "DSDAm";
      return;
    case BuiltinType::SatShortFract:
      Out << "DSDRs";
      return;
    case BuiltinType::SatFract:
      Out << "DSDRi";
      return;
    case BuiltinType::SatLongFract:
      Out << "DSDRl";
      return;
    case BuiltinType::SatUShortFract:
      Out << "DSDRt";
      return;
    case BuiltinType::SatUFract:
      Out << "DSDRj";
      return;
    case BuiltinType::SatULongFract:
      Out << "DSDRm";
      return;
    case BuiltinType::Half:
      Out << "Dh";
      return;
    case BuiltinType::Float16:
      Out << "DF16_";
      return;
    case BuiltinType::Float:
      Out << 'f';
      return;
    case BuiltinType::Double:
      Out << 'd';
      return;
    case BuiltinType::LongDouble:
      Out << Context.getTargetInfo().getLongDoubleMangling();
      return;
    case BuiltinType::BFloat16:
      Out << Context.getTargetInfo().getBFloat16Mangling();
      return;
    case BuiltinType::Float128:
      Out << Context.getTargetInfo().getFloat128Mangling();
      return;
    case BuiltinType::NullPtr:
      Out << "Dn";
      return;
    default:
      unsupported("placeholder or target-specific builtin type");
    }
  }

  void mangleFunctionExtInfo(const FunctionType *FT) {
    llvm::StringRef Qualifier;
    switch (FT->getCallConv()) {
    case CC_C:
    case CC_X86VectorCall:
    case CC_X86RegCall:
    case CC_PreserveMost:
    case CC_PreserveAll:
    case CC_AArch64VectorCall:
    case CC_AArch64SVEPCS:
      break;
    case CC_X86StdCall:
      Qualifier = "stdcall";
      break;
    case CC_X86FastCall:
      Qualifier = "fastcall";
      break;
    case CC_X86_64SysV:
      Qualifier = "sysv_abi";
      break;
    case CC_Win64:
      Qualifier = "ms_abi";
      break;
    }
    if (!Qualifier.empty())
      mangleVendorQualifier(Qualifier);
  }

  void mangleTagType(const TagType *TT) {
    const TagDecl *TD = TT->getDecl();
    if (!TD->getDeclContext()->isTranslationUnit())
      unsupported("non-file-scope tag type without full local-name mangling");
    const NamedDecl *NameDecl = TD;
    if (!TD->getIdentifier())
      NameDecl = TD->getTypedefNameForAnonDecl();
    if (!NameDecl || !NameDecl->getIdentifier())
      unsupported("anonymous tag without a linkage name");
    const llvm::StringRef Name = NameDecl->getIdentifier()->getName();
    Out << Name.size() << Name;
  }

  llvm::StringRef aarch64VectorBase(const BuiltinType *BT) const {
    switch (BT->getKind()) {
    case BuiltinType::SChar:
      return "Int8";
    case BuiltinType::Short:
      return "Int16";
    case BuiltinType::Int:
      return "Int32";
    case BuiltinType::Long:
    case BuiltinType::LongLong:
      return "Int64";
    case BuiltinType::UChar:
      return "Uint8";
    case BuiltinType::UShort:
      return "Uint16";
    case BuiltinType::UInt:
      return "Uint32";
    case BuiltinType::ULong:
    case BuiltinType::ULongLong:
      return "Uint64";
    case BuiltinType::Half:
      return "Float16";
    case BuiltinType::Float:
      return "Float32";
    case BuiltinType::Double:
      return "Float64";
    case BuiltinType::BFloat16:
      return "Bfloat16";
    default:
      unsupported("AArch64 Neon vector element");
    }
  }

  void mangleVectorType(const VectorType *VT) {
    const auto Kind = VT->getVectorKind();
    const auto Arch = Context.getTargetInfo().getTriple().getArch();
    if ((Kind == VectorKind::Neon || Kind == VectorKind::NeonPoly) &&
        Arch == llvm::Triple::aarch64 &&
        !Context.getTargetInfo().getTriple().isOSDarwin()) {
      const auto *BT = dyn_cast<BuiltinType>(VT->getElementType());
      if (!BT)
        unsupported("AArch64 Neon vector element");
      llvm::StringRef Base;
      if (Kind == VectorKind::NeonPoly) {
        switch (BT->getKind()) {
        case BuiltinType::UChar:
          Base = "Poly8";
          break;
        case BuiltinType::UShort:
          Base = "Poly16";
          break;
        case BuiltinType::ULong:
        case BuiltinType::ULongLong:
          Base = "Poly64";
          break;
        default:
          unsupported("AArch64 polynomial vector element");
        }
      } else {
        Base = aarch64VectorBase(BT);
      }
      std::string Name =
          ("__" + Base + "x" + llvm::Twine(VT->getNumElements()) + "_t").str();
      Out << Name.size() << Name;
      return;
    }
    if (Kind == VectorKind::SveFixedLengthData ||
        Kind == VectorKind::SveFixedLengthPredicate)
      unsupported("fixed-length SVE vector type");
    Out << "Dv" << VT->getNumElements() << '_';
    mangleType(VT->getElementType());
  }

  void mangleUnqualifiedType(const Type *Ty) {
    if (const auto *BT = dyn_cast<BuiltinType>(Ty)) {
      mangleBuiltinType(BT);
    } else if (const auto *FT = dyn_cast<FunctionProtoType>(Ty)) {
      if (FT->getAArch64SMEAttributes() != FunctionType::SME_NormalFunction)
        unsupported("SME-attributed function type without a pinned Clang ABI");
      mangleFunctionExtInfo(FT);
      Out << 'F';
      mangleType(FT->getReturnType());
      if (FT->getNumParams() == 0 && !FT->isVariadic()) {
        Out << 'v';
      } else {
        for (unsigned I = 0; I != FT->getNumParams(); ++I) {
          if (FT->getExtParameterInfo(I).isNoEscape())
            mangleVendorQualifier("noescape");
          mangleType(Context.getSignatureParameterType(FT->getParamType(I)));
        }
        if (FT->isVariadic())
          Out << 'z';
      }
      Out << 'E';
    } else if (const auto *FT = dyn_cast<FunctionNoProtoType>(Ty)) {
      Out << 'F';
      mangleType(FT->getReturnType());
      Out << 'E';
    } else if (const auto *TT = dyn_cast<TagType>(Ty)) {
      mangleTagType(TT);
    } else if (const auto *PT = dyn_cast<PointerType>(Ty)) {
      Out << 'P';
      mangleType(PT->getPointeeType());
    } else if (const auto *AT = dyn_cast<ConstantArrayType>(Ty)) {
      Out << 'A' << AT->getSize() << '_';
      mangleType(AT->getElementType());
    } else if (const auto *AT = dyn_cast<IncompleteArrayType>(Ty)) {
      Out << "A_";
      mangleType(AT->getElementType());
    } else if (const auto *AT = dyn_cast<VariableArrayType>(Ty)) {
      if (AT->getSizeExpr())
        unsupported("non-decayed variable-length array bound");
      Out << "A_";
      mangleType(AT->getElementType());
    } else if (const auto *CT = dyn_cast<ComplexType>(Ty)) {
      Out << 'C';
      mangleType(CT->getElementType());
    } else if (const auto *AT = dyn_cast<AtomicType>(Ty)) {
      Out << "U7_Atomic";
      mangleType(AT->getValueType());
    } else if (const auto *BIT = dyn_cast<BitIntType>(Ty)) {
      Out << 'D' << (BIT->isUnsigned() ? 'U' : 'B') << BIT->getNumBits() << '_';
    } else if (const auto *VT = dyn_cast<VectorType>(Ty)) {
      mangleVectorType(VT);
    } else if (const auto *MT = dyn_cast<ConstantMatrixType>(Ty)) {
      Out << "u11matrix_typeI";
      Out << 'L';
      mangleType(Context.getSizeType());
      Out << MT->getNumRows() << 'E';
      Out << 'L';
      mangleType(Context.getSizeType());
      Out << MT->getNumColumns() << 'E';
      mangleType(MT->getElementType());
      Out << 'E';
    } else {
      unsupported("unsupported canonical source type");
    }
  }

public:
  CanonicalItaniumTypeMangler(TreeContext &Context, llvm::raw_ostream &Out,
                              bool NormalizeIntegers)
      : Context(Context), Out(Out), NormalizeIntegers(NormalizeIntegers) {}

  void mangleType(QualType T) {
    T = T.getCanonicalType();
    const SplitQualType Split = T.split();
    Qualifiers Quals = Split.Quals;
    const Type *Ty = Split.Ty;
    const bool IsSubstitutable = Quals.hasQualifiers() || !isa<BuiltinType>(Ty);
    const uintptr_t Key = IsSubstitutable ? substitutionKey(T) : 0;
    if (IsSubstitutable && mangleSubstitution(Key))
      return;

    // C's qualifiers on an array type qualify its element type.  Keep the
    // original T as the substitution key, matching the Itanium ABI, while
    // asking TreeContext for the array view with qualifiers pushed down.
    if (Quals.hasQualifiers() && isa<ArrayType>(Ty)) {
      Ty = Context.getAsArrayType(T);
      Quals = Qualifiers();
    }

    if (Quals.hasQualifiers()) {
      mangleQualifiers(Quals);
      mangleType(QualType(Ty, 0));
    } else {
      mangleUnqualifiedType(Ty);
    }

    if (IsSubstitutable)
      addSubstitution(Key);
  }
};

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

  void mangleCanonicalTypeName(QualType T, llvm::raw_ostream &Out,
                               bool NormalizeIntegers) override {
    Out << "_ZTS";
    CanonicalItaniumTypeMangler(getTreeContext(), Out, NormalizeIntegers)
        .mangleType(T);
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
