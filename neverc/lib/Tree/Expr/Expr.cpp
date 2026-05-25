#include "neverc/Tree/Expr/Expr.h"
#include "neverc/Foundation/Builtin/Builtins.h"
#include "neverc/Foundation/Core/CharInfo.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/LiteralParser.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Scan/SourceScanner.h"
#include "neverc/Tree/Core/APValue.h"
#include "neverc/Tree/Core/Attr.h"
#include "neverc/Tree/Core/DependCalc.h"
#include "neverc/Tree/Core/Mangle.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Expr/EvaluatedExprVisitor.h"
#include "neverc/Tree/Expr/IgnoreExpr.h"
#include "neverc/Tree/Type/StructLayout.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cstring>
#include <optional>
using namespace neverc;

// ===----------------------------------------------------------------------===
// Expr base — value classification & source locations
// ===----------------------------------------------------------------------===

const Expr *Expr::skipRValueSubobjectAdjustments(
    llvm::SmallVectorImpl<const Expr *> &CommaLHSs,
    llvm::SmallVectorImpl<SubobjectAdjustment> &Adjustments) const {
  const Expr *E = this;
  while (true) {
    E = E->IgnoreParens();

    if (const CastExpr *CE = dyn_cast<CastExpr>(E)) {
      if (CE->getCastKind() == CK_NoOp) {
        E = CE->getSubExpr();
        continue;
      }
    } else if (const MemberExpr *ME = dyn_cast<MemberExpr>(E)) {
      if (!ME->isArrow()) {
        assert(ME->getBase()->getType()->isRecordType());
        if (FieldDecl *Field = dyn_cast<FieldDecl>(ME->getMemberDecl())) {
          if (!Field->isBitField()) {
            E = ME->getBase();
            Adjustments.push_back(SubobjectAdjustment(Field));
            continue;
          }
        }
      }
    } else if (const BinaryOperator *BO = dyn_cast<BinaryOperator>(E)) {
      if (BO->getOpcode() == BO_Comma) {
        CommaLHSs.push_back(BO->getLHS());
        E = BO->getRHS();
        continue;
      }
    }

    // Nothing changed.
    break;
  }
  return E;
}

bool Expr::isKnownToHaveBooleanValue(bool Semantic) const {
  const Expr *E = IgnoreParens();

  // If this value has _Bool type, it is obvious 0/1.
  if (E->getType()->isBooleanType())
    return true;
  // If this is a non-scalar-integer type, we don't care enough to try.
  if (!E->getType()->isIntegralOrEnumerationType())
    return false;

  if (const UnaryOperator *UO = dyn_cast<UnaryOperator>(E)) {
    switch (UO->getOpcode()) {
    case UO_Plus:
      return UO->getSubExpr()->isKnownToHaveBooleanValue(Semantic);
    case UO_LNot:
      return true;
    default:
      return false;
    }
  }

  // Only look through implicit casts.  If the user writes
  // '(int) (a && b)' treat it as an arbitrary int.
  if (const ImplicitCastExpr *CE = dyn_cast<ImplicitCastExpr>(E))
    return CE->getSubExpr()->isKnownToHaveBooleanValue(Semantic);

  if (const BinaryOperator *BO = dyn_cast<BinaryOperator>(E)) {
    switch (BO->getOpcode()) {
    default:
      return false;
    case BO_LT: // Relational operators.
    case BO_GT:
    case BO_LE:
    case BO_GE:
    case BO_EQ: // Equality operators.
    case BO_NE:
    case BO_LAnd: // AND operator.
    case BO_LOr:  // Logical OR operator.
      return true;

    case BO_And: // Bitwise AND operator.
    case BO_Xor: // Bitwise XOR operator.
    case BO_Or:  // Bitwise OR operator.
      return BO->getLHS()->isKnownToHaveBooleanValue(Semantic) &&
             BO->getRHS()->isKnownToHaveBooleanValue(Semantic);

    case BO_Comma:
    case BO_Assign:
      return BO->getRHS()->isKnownToHaveBooleanValue(Semantic);
    }
  }

  if (const ConditionalOperator *CO = dyn_cast<ConditionalOperator>(E))
    return CO->getTrueExpr()->isKnownToHaveBooleanValue(Semantic) &&
           CO->getFalseExpr()->isKnownToHaveBooleanValue(Semantic);

  if (const auto *OVE = dyn_cast<OpaqueValueExpr>(E))
    return OVE->getSourceExpr()->isKnownToHaveBooleanValue(Semantic);

  if (const FieldDecl *FD = E->getSourceBitField())
    if (!Semantic && FD->getType()->isUnsignedIntegerType() &&
        FD->getBitWidthValue(FD->getTreeContext()) == 1)
      return true;

  return false;
}

bool Expr::isFlexibleArrayMemberLike(
    TreeContext &Context,
    LangOptions::StrictFlexArraysLevelKind StrictFlexArraysLevel,
    bool IgnoreMacroSubstitution) const {

  // For compatibility with existing code, we treat arrays of length 0 or
  // 1 as flexible array members.
  const auto *CAT = Context.getAsConstantArrayType(getType());
  if (CAT) {
    llvm::APInt Size = CAT->getSize();

    using FAMKind = LangOptions::StrictFlexArraysLevelKind;

    if (StrictFlexArraysLevel == FAMKind::IncompleteOnly)
      return false;

    // GCC extension, only allowed to represent a FAM.
    if (Size == 0)
      return true;

    if (StrictFlexArraysLevel == FAMKind::ZeroOrIncomplete && Size.uge(1))
      return false;

    if (StrictFlexArraysLevel == FAMKind::OneZeroOrIncomplete && Size.uge(2))
      return false;
  } else if (!Context.getAsIncompleteArrayType(getType()))
    return false;

  const Expr *E = IgnoreParens();

  const NamedDecl *ND = nullptr;
  if (const auto *DRE = dyn_cast<DeclRefExpr>(E))
    ND = DRE->getDecl();
  else if (const auto *ME = dyn_cast<MemberExpr>(E))
    ND = ME->getMemberDecl();

  if (!ND)
    return false;

  // A flexible array member must be the last member in the struct/union.
  if (const auto *FD = dyn_cast<FieldDecl>(ND)) {
    // GCC treats an array memeber of a union as an FAM if the size is one or
    // zero.
    if (CAT) {
      llvm::APInt Size = CAT->getSize();
      if (FD->getParent()->isUnion() && (Size.isZero() || Size.isOne()))
        return true;
    }

    // Don't consider sizes resulting from macro expansions to form C89
    // tail-padded arrays.
    if (IgnoreMacroSubstitution) {
      TypeSourceInfo *TInfo = FD->getTypeSourceInfo();
      while (TInfo) {
        TypeLoc TL = TInfo->getTypeLoc();
        // Look through typedefs.
        if (TypedefTypeLoc TTL = TL.getAsAdjusted<TypedefTypeLoc>()) {
          const TypedefNameDecl *TDL = TTL.getTypedefNameDecl();
          TInfo = TDL->getTypeSourceInfo();
          continue;
        }
        if (ConstantArrayTypeLoc CTL = TL.getAs<ConstantArrayTypeLoc>()) {
          const Expr *SizeExpr = dyn_cast<IntegerLiteral>(CTL.getSizeExpr());
          if (!SizeExpr || SizeExpr->getExprLoc().isMacroID())
            return false;
        }
        break;
      }
    }

    RecordDecl::field_iterator FI(
        DeclContext::decl_iterator(const_cast<FieldDecl *>(FD)));
    return ++FI == FD->getParent()->field_end();
  }

  return false;
}

const ValueDecl *
Expr::getAsBuiltinConstantDeclRef(const TreeContext &Context) const {
  Expr::EvalResult Eval;

  if (EvaluateAsConstantExpr(Eval, Context)) {
    APValue &Value = Eval.Val;

    if (Value.isLValue() && Value.getLValueOffset().isZero())
      return Value.getLValueBase().dyn_cast<const ValueDecl *>();
  }

  return nullptr;
}

// Amusing macro metaprogramming hack: check whether a class provides
// a more specific implementation of getExprLoc().
//
// See also Stmt.cpp:{getBeginLoc(),getEndLoc()}.
namespace {
template <class E, class T>
SourceLocation getExprLocImpl(const Expr *expr,
                              SourceLocation (T::*v)() const) {
  return static_cast<const E *>(expr)->getExprLoc();
}

template <class E>
SourceLocation getExprLocImpl(const Expr *expr,
                              SourceLocation (Expr::*v)() const) {
  return static_cast<const E *>(expr)->getBeginLoc();
}
} // namespace

SourceLocation Expr::getExprLoc() const {
  switch (getStmtClass()) {
  case Stmt::NoStmtClass:
    llvm_unreachable("statement without class");
#define ABSTRACT_STMT(type)
#define STMT(type, base)                                                       \
  case Stmt::type##Class:                                                      \
    break;
#define EXPR(type, base)                                                       \
  case Stmt::type##Class:                                                      \
    return getExprLocImpl<type>(this, &type::getExprLoc);
#include "neverc/Tree/StmtNodes.td.h"
  }
  llvm_unreachable("unknown expression kind");
}

namespace {
void validateResultStorage(ConstantResultStorageKind Kind) {
  assert((Kind == ConstantResultStorageKind::APValue ||
          Kind == ConstantResultStorageKind::Int64 ||
          Kind == ConstantResultStorageKind::None) &&
         "Invalid StorageKind Value");
  (void)Kind;
}
} // namespace

ConstantResultStorageKind ConstantExpr::getStorageKind(const APValue &Value) {
  switch (Value.getKind()) {
  case APValue::None:
  case APValue::Indeterminate:
    return ConstantResultStorageKind::None;
  case APValue::Int:
    if (!Value.getInt().needsCleanup())
      return ConstantResultStorageKind::Int64;
    [[fallthrough]];
  default:
    return ConstantResultStorageKind::APValue;
  }
}

ConstantResultStorageKind
ConstantExpr::getStorageKind(const Type *T, const TreeContext &Context) {
  if (T->isIntegralOrEnumerationType() && Context.getTypeInfo(T).Width <= 64)
    return ConstantResultStorageKind::Int64;
  return ConstantResultStorageKind::APValue;
}

ConstantExpr::ConstantExpr(Expr *SubExpr, ConstantResultStorageKind StorageKind,
                           bool IsImmediateInvocation)
    : FullExpr(ConstantExprClass, SubExpr) {
  ConstantExprBits.ResultKind = llvm::to_underlying(StorageKind);
  ConstantExprBits.APValueKind = APValue::None;
  ConstantExprBits.IsUnsigned = false;
  ConstantExprBits.BitWidth = 0;
  ConstantExprBits.HasCleanup = false;
  ConstantExprBits.IsImmediateInvocation = IsImmediateInvocation;

  if (StorageKind == ConstantResultStorageKind::APValue)
    ::new (getTrailingObjects<APValue>()) APValue();
}

ConstantExpr *ConstantExpr::Create(const TreeContext &Context, Expr *E,
                                   ConstantResultStorageKind StorageKind,
                                   bool IsImmediateInvocation) {
  assert(!isa<ConstantExpr>(E));
  validateResultStorage(StorageKind);

  unsigned Size = totalSizeToAlloc<APValue, uint64_t>(
      StorageKind == ConstantResultStorageKind::APValue,
      StorageKind == ConstantResultStorageKind::Int64);
  void *Mem = Context.Allocate(Size, alignof(ConstantExpr));
  return new (Mem) ConstantExpr(E, StorageKind, IsImmediateInvocation);
}

ConstantExpr *ConstantExpr::Create(const TreeContext &Context, Expr *E,
                                   const APValue &Result) {
  ConstantResultStorageKind StorageKind = getStorageKind(Result);
  ConstantExpr *Self = Create(Context, E, StorageKind);
  Self->SetResult(Result, Context);
  return Self;
}

ConstantExpr::ConstantExpr(EmptyShell Empty,
                           ConstantResultStorageKind StorageKind)
    : FullExpr(ConstantExprClass, Empty) {
  ConstantExprBits.ResultKind = llvm::to_underlying(StorageKind);

  if (StorageKind == ConstantResultStorageKind::APValue)
    ::new (getTrailingObjects<APValue>()) APValue();
}

ConstantExpr *ConstantExpr::CreateEmpty(const TreeContext &Context,
                                        ConstantResultStorageKind StorageKind) {
  validateResultStorage(StorageKind);

  unsigned Size = totalSizeToAlloc<APValue, uint64_t>(
      StorageKind == ConstantResultStorageKind::APValue,
      StorageKind == ConstantResultStorageKind::Int64);
  void *Mem = Context.Allocate(Size, alignof(ConstantExpr));
  return new (Mem) ConstantExpr(EmptyShell(), StorageKind);
}

void ConstantExpr::MoveIntoResult(APValue &Value, const TreeContext &Context) {
  assert((unsigned)getStorageKind(Value) <= ConstantExprBits.ResultKind &&
         "Invalid storage for this value kind");
  ConstantExprBits.APValueKind = Value.getKind();
  switch (getResultStorageKind()) {
  case ConstantResultStorageKind::None:
    return;
  case ConstantResultStorageKind::Int64:
    Int64Result() = *Value.getInt().getRawData();
    ConstantExprBits.BitWidth = Value.getInt().getBitWidth();
    ConstantExprBits.IsUnsigned = Value.getInt().isUnsigned();
    return;
  case ConstantResultStorageKind::APValue:
    if (!ConstantExprBits.HasCleanup && Value.needsCleanup()) {
      ConstantExprBits.HasCleanup = true;
      Context.addDestruction(&APValueResult());
    }
    APValueResult() = std::move(Value);
    return;
  }
  llvm_unreachable("Invalid ResultKind Bits");
}

llvm::APSInt ConstantExpr::getResultAsAPSInt() const {
  switch (getResultStorageKind()) {
  case ConstantResultStorageKind::APValue:
    return APValueResult().getInt();
  case ConstantResultStorageKind::Int64:
    return llvm::APSInt(llvm::APInt(ConstantExprBits.BitWidth, Int64Result()),
                        ConstantExprBits.IsUnsigned);
  default:
    llvm_unreachable("invalid Accessor");
  }
}

APValue ConstantExpr::getAPValueResult() const {

  switch (getResultStorageKind()) {
  case ConstantResultStorageKind::APValue:
    return APValueResult();
  case ConstantResultStorageKind::Int64:
    return APValue(
        llvm::APSInt(llvm::APInt(ConstantExprBits.BitWidth, Int64Result()),
                     ConstantExprBits.IsUnsigned));
  case ConstantResultStorageKind::None:
    if (ConstantExprBits.APValueKind == APValue::Indeterminate)
      return APValue::IndeterminateValue();
    return APValue();
  }
  llvm_unreachable("invalid ResultKind");
}

DeclRefExpr::DeclRefExpr(const TreeContext &Ctx, ValueDecl *D, QualType T,
                         ExprValueKind VK, SourceLocation L,
                         NonOdrUseReason NOUR)
    : Expr(DeclRefExprClass, T, VK, OK_Ordinary), D(D) {
  DeclRefExprBits.HasFoundDecl = false;
  DeclRefExprBits.HadMultipleCandidates = false;
  DeclRefExprBits.NonOdrUseReason = NOUR;
  DeclRefExprBits.IsImmediateEscalating = false;
  DeclRefExprBits.Loc = L;
  setDependence(computeDependence(this, Ctx));
}

DeclRefExpr::DeclRefExpr(const TreeContext &Ctx, ValueDecl *D,
                         const DeclarationNameInfo &NameInfo, NamedDecl *FoundD,
                         QualType T, ExprValueKind VK, NonOdrUseReason NOUR)
    : Expr(DeclRefExprClass, T, VK, OK_Ordinary), D(D) {
  DeclRefExprBits.Loc = NameInfo.getLoc();
  DeclRefExprBits.HasFoundDecl = FoundD ? 1 : 0;
  if (FoundD)
    *getTrailingObjects<NamedDecl *>() = FoundD;
  DeclRefExprBits.NonOdrUseReason = NOUR;
  DeclRefExprBits.IsImmediateEscalating = false;
  DeclRefExprBits.HadMultipleCandidates = 0;
  setDependence(computeDependence(this, Ctx));
}

DeclRefExpr *DeclRefExpr::Create(const TreeContext &Context, ValueDecl *D,
                                 SourceLocation NameLoc, QualType T,
                                 ExprValueKind VK, NamedDecl *FoundD,
                                 NonOdrUseReason NOUR) {
  return Create(Context, D, DeclarationNameInfo(D->getDeclName(), NameLoc), T,
                VK, FoundD, NOUR);
}

DeclRefExpr *DeclRefExpr::Create(const TreeContext &Context, ValueDecl *D,
                                 const DeclarationNameInfo &NameInfo,
                                 QualType T, ExprValueKind VK,
                                 NamedDecl *FoundD, NonOdrUseReason NOUR) {
  if (D == FoundD)
    FoundD = nullptr;

  std::size_t Size = totalSizeToAlloc<NamedDecl *>(FoundD ? 1 : 0);
  void *Mem = Context.Allocate(Size, alignof(DeclRefExpr));
  return new (Mem) DeclRefExpr(Context, D, NameInfo, FoundD, T, VK, NOUR);
}

DeclRefExpr *DeclRefExpr::CreateEmpty(const TreeContext &Context,
                                      bool HasFoundDecl) {
  std::size_t Size = totalSizeToAlloc<NamedDecl *>(HasFoundDecl ? 1 : 0);
  void *Mem = Context.Allocate(Size, alignof(DeclRefExpr));
  return new (Mem) DeclRefExpr(EmptyShell());
}

void DeclRefExpr::setDecl(ValueDecl *NewD) {
  D = NewD;
  if (getType()->isUndeducedType())
    setType(NewD->getType());
  setDependence(computeDependence(this, NewD->getTreeContext()));
}

SourceLocation DeclRefExpr::getBeginLoc() const {
  return getNameInfo().getBeginLoc();
}
SourceLocation DeclRefExpr::getEndLoc() const {
  return getNameInfo().getEndLoc();
}

PredefinedExpr::PredefinedExpr(SourceLocation L, QualType FNTy,
                               PredefinedIdentKind IK, bool IsTransparent,
                               StringLiteral *SL)
    : Expr(PredefinedExprClass, FNTy, VK_LValue, OK_Ordinary) {
  PredefinedExprBits.Kind = llvm::to_underlying(IK);
  assert((getIdentKind() == IK) &&
         "IdentKind do not fit in PredefinedExprBitfields!");
  bool HasFunctionName = SL != nullptr;
  PredefinedExprBits.HasFunctionName = HasFunctionName;
  PredefinedExprBits.IsTransparent = IsTransparent;
  PredefinedExprBits.Loc = L;
  if (HasFunctionName)
    setFunctionName(SL);
  setDependence(computeDependence(this));
}

PredefinedExpr::PredefinedExpr(EmptyShell Empty, bool HasFunctionName)
    : Expr(PredefinedExprClass, Empty) {
  PredefinedExprBits.HasFunctionName = HasFunctionName;
}

PredefinedExpr *PredefinedExpr::Create(const TreeContext &Ctx, SourceLocation L,
                                       QualType FNTy, PredefinedIdentKind IK,
                                       bool IsTransparent, StringLiteral *SL) {
  bool HasFunctionName = SL != nullptr;
  void *Mem = Ctx.Allocate(totalSizeToAlloc<Stmt *>(HasFunctionName),
                           alignof(PredefinedExpr));
  return new (Mem) PredefinedExpr(L, FNTy, IK, IsTransparent, SL);
}

PredefinedExpr *PredefinedExpr::CreateEmpty(const TreeContext &Ctx,
                                            bool HasFunctionName) {
  void *Mem = Ctx.Allocate(totalSizeToAlloc<Stmt *>(HasFunctionName),
                           alignof(PredefinedExpr));
  return new (Mem) PredefinedExpr(EmptyShell(), HasFunctionName);
}

llvm::StringRef PredefinedExpr::getIdentKindName(PredefinedIdentKind IK) {
  switch (IK) {
  case PredefinedIdentKind::Func:
    return "__func__";
  case PredefinedIdentKind::Function:
    return "__FUNCTION__";
  case PredefinedIdentKind::FuncDName:
    return "__FUNCDNAME__";
  case PredefinedIdentKind::LFunction:
    return "L__FUNCTION__";
  case PredefinedIdentKind::PrettyFunction:
    return "__PRETTY_FUNCTION__";
  case PredefinedIdentKind::FuncSig:
    return "__FUNCSIG__";
  case PredefinedIdentKind::LFuncSig:
    return "L__FUNCSIG__";
  }
  llvm_unreachable("Unknown ident kind for PredefinedExpr");
}

std::string PredefinedExpr::ComputeName(PredefinedIdentKind IK,
                                        const Decl *CurrentDecl) {
  TreeContext &Context = CurrentDecl->getTreeContext();

  if (IK == PredefinedIdentKind::FuncDName) {
    if (const NamedDecl *ND = dyn_cast<NamedDecl>(CurrentDecl)) {
      std::unique_ptr<MangleContext> MC;
      MC.reset(Context.createMangleContext());

      if (MC->shouldMangleDeclName(ND)) {
        llvm::SmallString<256> Buffer;
        llvm::raw_svector_ostream Out(Buffer);
        GlobalDecl GD(ND);
        MC->mangleName(GD, Out);

        if (!Buffer.empty() && Buffer.front() == '\01')
          return std::string(Buffer.substr(1));
        return std::string(Buffer.str());
      }
      return std::string(ND->getIdentifier()->getName());
    }
    return "";
  }
  if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(CurrentDecl)) {
    if (IK != PredefinedIdentKind::PrettyFunction &&
        IK != PredefinedIdentKind::FuncSig &&
        IK != PredefinedIdentKind::LFuncSig)
      return FD->getNameAsString();

    llvm::SmallString<256> Name;
    llvm::raw_svector_ostream Out(Name);

    class PrettyCallbacks final : public PrintingCallbacks {
    public:
      PrettyCallbacks(const LangOptions &LO) : LO(LO) {}
      std::string remapPath(llvm::StringRef Path) const override {
        llvm::SmallString<128> p(Path);
        LO.remapPathPrefix(p);
        return std::string(p);
      }

    private:
      const LangOptions &LO;
    };
    PrintingPolicy Policy(Context.getLangOpts());
    PrettyCallbacks PrettyCB(Context.getLangOpts());
    Policy.Callbacks = &PrettyCB;
    std::string Proto;
    llvm::raw_string_ostream POut(Proto);

    const FunctionDecl *Decl = FD;
    const FunctionType *AFT = Decl->getType()->getAs<FunctionType>();
    const FunctionProtoType *FT = nullptr;
    if (FD->hasWrittenPrototype())
      FT = dyn_cast<FunctionProtoType>(AFT);

    if (IK == PredefinedIdentKind::FuncSig ||
        IK == PredefinedIdentKind::LFuncSig) {
      switch (AFT->getCallConv()) {
      case CC_C:
        POut << "__cdecl ";
        break;
      case CC_X86StdCall:
        POut << "__stdcall ";
        break;
      case CC_X86FastCall:
        POut << "__fastcall ";
        break;
      case CC_X86VectorCall:
        POut << "__vectorcall ";
        break;
      case CC_X86RegCall:
        POut << "__regcall ";
        break;
      // Only bother printing the conventions that MSVC knows about.
      default:
        break;
      }
    }

    FD->printQualifiedName(POut, Policy);

    POut << "(";
    if (FT) {
      for (unsigned i = 0, e = Decl->getNumParams(); i != e; ++i) {
        if (i)
          POut << ", ";
        POut << Decl->getParamDecl(i)->getType().stream(Policy);
      }

      if (FT->isVariadic()) {
        if (FD->getNumParams())
          POut << ", ";
        POut << "...";
      } else if (!Decl->getNumParams()) {
        POut << tok::getKeywordSpelling(tok::kw_void);
      }
    }
    POut << ")";

    POut.flush();

    AFT->getReturnType().getAsStringInternal(Proto, Policy);

    Out << Proto;

    return std::string(Name);
  }
  if (isa<TranslationUnitDecl>(CurrentDecl) &&
      IK == PredefinedIdentKind::PrettyFunction) {
    // __PRETTY_FUNCTION__ -> "top level", the others produce an empty string.
    return "top level";
  }
  return "";
}

// ===----------------------------------------------------------------------===
// Literals — numeric, character, string
// ===----------------------------------------------------------------------===

void APNumericStorage::setIntValue(const TreeContext &C,
                                   const llvm::APInt &Val) {
  if (hasAllocation())
    C.Deallocate(pVal);

  BitWidth = Val.getBitWidth();
  unsigned NumWords = Val.getNumWords();
  const uint64_t *Words = Val.getRawData();
  if (NumWords > 1) {
    pVal = new (C) uint64_t[NumWords];
    std::copy(Words, Words + NumWords, pVal);
  } else if (NumWords == 1)
    VAL = Words[0];
  else
    VAL = 0;
}

IntegerLiteral::IntegerLiteral(const TreeContext &C, const llvm::APInt &V,
                               QualType type, SourceLocation l)
    : Expr(IntegerLiteralClass, type, VK_PRValue, OK_Ordinary), Loc(l) {
  assert(type->isIntegerType() && "Illegal type in IntegerLiteral");
  assert(V.getBitWidth() == C.getIntWidth(type) &&
         "Integer type is not the correct size for constant.");
  setValue(C, V);
  setDependence(ExprDependence::None);
}

IntegerLiteral *IntegerLiteral::Create(const TreeContext &C,
                                       const llvm::APInt &V, QualType type,
                                       SourceLocation l) {
  return new (C) IntegerLiteral(C, V, type, l);
}

IntegerLiteral *IntegerLiteral::Create(const TreeContext &C, EmptyShell Empty) {
  return new (C) IntegerLiteral(Empty);
}

FixedPointLiteral::FixedPointLiteral(const TreeContext &C, const llvm::APInt &V,
                                     QualType type, SourceLocation l,
                                     unsigned Scale)
    : Expr(FixedPointLiteralClass, type, VK_PRValue, OK_Ordinary), Loc(l),
      Scale(Scale) {
  assert(type->isFixedPointType() && "Illegal type in FixedPointLiteral");
  assert(V.getBitWidth() == C.getTypeInfo(type).Width &&
         "Fixed point type is not the correct size for constant.");
  setValue(C, V);
  setDependence(ExprDependence::None);
}

FixedPointLiteral *FixedPointLiteral::CreateFromRawInt(const TreeContext &C,
                                                       const llvm::APInt &V,
                                                       QualType type,
                                                       SourceLocation l,
                                                       unsigned Scale) {
  return new (C) FixedPointLiteral(C, V, type, l, Scale);
}

FixedPointLiteral *FixedPointLiteral::Create(const TreeContext &C,
                                             EmptyShell Empty) {
  return new (C) FixedPointLiteral(Empty);
}

std::string FixedPointLiteral::getValueAsString(unsigned Radix) const {
  // Currently the longest decimal number that can be printed is the max for an
  // unsigned long _Accum: 4294967295.99999999976716935634613037109375
  // which is 43 characters.
  llvm::SmallString<64> S;
  FixedPointValueToString(
      S, llvm::APSInt::getUnsigned(getValue().getZExtValue()), Scale);
  return std::string(S.str());
}

void CharacterLiteral::print(unsigned Val, CharacterLiteralKind Kind,
                             llvm::raw_ostream &OS) {
  switch (Kind) {
  case CharacterLiteralKind::Ascii:
    break; // no prefix.
  case CharacterLiteralKind::Wide:
    OS << 'L';
    break;
  case CharacterLiteralKind::UTF8:
    OS << "u8";
    break;
  case CharacterLiteralKind::UTF16:
    OS << 'u';
    break;
  case CharacterLiteralKind::UTF32:
    OS << 'U';
    break;
  }

  llvm::StringRef Escaped = escapeCStyle<EscapeChar::Single>(Val);
  if (!Escaped.empty()) {
    OS << "'" << Escaped << "'";
  } else {
    // A character literal might be sign-extended, which
    // would result in an invalid \U escape sequence.
    if ((Val & ~0xFFu) == ~0xFFu && Kind == CharacterLiteralKind::Ascii)
      Val &= 0xFFu;
    if (Val < 256 && isPrintable((unsigned char)Val))
      OS << "'" << (char)Val << "'";
    else if (Val < 256)
      OS << "'\\x" << llvm::format("%02x", Val) << "'";
    else if (Val <= 0xFFFF)
      OS << "'\\u" << llvm::format("%04x", Val) << "'";
    else
      OS << "'\\U" << llvm::format("%08x", Val) << "'";
  }
}

FloatingLiteral::FloatingLiteral(const TreeContext &C, const llvm::APFloat &V,
                                 bool isexact, QualType Type, SourceLocation L)
    : Expr(FloatingLiteralClass, Type, VK_PRValue, OK_Ordinary), Loc(L) {
  setSemantics(V.getSemantics());
  FloatingLiteralBits.IsExact = isexact;
  setValue(C, V);
  setDependence(ExprDependence::None);
}

FloatingLiteral::FloatingLiteral(const TreeContext &C, EmptyShell Empty)
    : Expr(FloatingLiteralClass, Empty) {
  setRawSemantics(llvm::APFloatBase::S_IEEEhalf);
  FloatingLiteralBits.IsExact = false;
}

FloatingLiteral *FloatingLiteral::Create(const TreeContext &C,
                                         const llvm::APFloat &V, bool isexact,
                                         QualType Type, SourceLocation L) {
  return new (C) FloatingLiteral(C, V, isexact, Type, L);
}

FloatingLiteral *FloatingLiteral::Create(const TreeContext &C,
                                         EmptyShell Empty) {
  return new (C) FloatingLiteral(C, Empty);
}

double FloatingLiteral::getValueAsApproximateDouble() const {
  llvm::APFloat V = getValue();
  bool ignored;
  V.convert(llvm::APFloat::IEEEdouble(), llvm::APFloat::rmNearestTiesToEven,
            &ignored);
  return V.convertToDouble();
}

unsigned StringLiteral::mapCharByteWidth(TargetInfo const &Target,
                                         StringLiteralKind SK) {
  unsigned CharByteWidth = 0;
  switch (SK) {
  case StringLiteralKind::Ordinary:
  case StringLiteralKind::UTF8:
    CharByteWidth = Target.getCharWidth();
    break;
  case StringLiteralKind::Wide:
    CharByteWidth = Target.getWCharWidth();
    break;
  case StringLiteralKind::UTF16:
    CharByteWidth = Target.getChar16Width();
    break;
  case StringLiteralKind::UTF32:
    CharByteWidth = Target.getChar32Width();
    break;
  case StringLiteralKind::Unevaluated:
    return sizeof(char); // Host;
  }
  assert((CharByteWidth & 7) == 0 && "Assumes character size is byte multiple");
  CharByteWidth /= 8;
  assert((CharByteWidth == 1 || CharByteWidth == 2 || CharByteWidth == 4) &&
         "The only supported character byte widths are 1,2 and 4!");
  return CharByteWidth;
}

StringLiteral::StringLiteral(const TreeContext &Ctx, llvm::StringRef Str,
                             StringLiteralKind Kind, QualType Ty,
                             const SourceLocation *Loc,
                             unsigned NumConcatenated)
    : Expr(StringLiteralClass, Ty, VK_LValue, OK_Ordinary) {

  unsigned Length = Str.size();

  StringLiteralBits.Kind = llvm::to_underlying(Kind);
  StringLiteralBits.NumConcatenated = NumConcatenated;

  if (Kind != StringLiteralKind::Unevaluated) {
    assert(Ctx.getAsConstantArrayType(Ty) &&
           "StringLiteral must be of constant array type!");
    unsigned CharByteWidth = mapCharByteWidth(Ctx.getTargetInfo(), Kind);
    unsigned ByteLength = Str.size();
    assert((ByteLength % CharByteWidth == 0) &&
           "The size of the data must be a multiple of CharByteWidth!");

    switch (CharByteWidth) {
    case 1:
      Length = ByteLength;
      break;
    case 2:
      Length = ByteLength / 2;
      break;
    case 4:
      Length = ByteLength / 4;
      break;
    default:
      llvm_unreachable("Unsupported character width!");
    }

    StringLiteralBits.CharByteWidth = CharByteWidth;
    StringLiteralBits.IsPascal = false;
  } else {
    StringLiteralBits.CharByteWidth = 1;
    StringLiteralBits.IsPascal = false;
  }

  *getTrailingObjects<unsigned>() = Length;

  // SourceLocation is POD-like, safe to memcpy.
  std::memcpy(getTrailingObjects<SourceLocation>(), Loc,
              NumConcatenated * sizeof(SourceLocation));
  std::memcpy(getTrailingObjects<char>(), Str.data(), Str.size());

  setDependence(ExprDependence::None);
}

StringLiteral::StringLiteral(EmptyShell Empty, unsigned NumConcatenated,
                             unsigned Length, unsigned CharByteWidth)
    : Expr(StringLiteralClass, Empty) {
  StringLiteralBits.CharByteWidth = CharByteWidth;
  StringLiteralBits.NumConcatenated = NumConcatenated;
  *getTrailingObjects<unsigned>() = Length;
}

StringLiteral *StringLiteral::Create(const TreeContext &Ctx,
                                     llvm::StringRef Str,
                                     StringLiteralKind Kind, QualType Ty,
                                     const SourceLocation *Loc,
                                     unsigned NumConcatenated) {
  void *Mem = Ctx.Allocate(totalSizeToAlloc<unsigned, SourceLocation, char>(
                               1, NumConcatenated, Str.size()),
                           alignof(StringLiteral));
  return new (Mem) StringLiteral(Ctx, Str, Kind, Ty, Loc, NumConcatenated);
}

StringLiteral *StringLiteral::CreateEmpty(const TreeContext &Ctx,
                                          unsigned NumConcatenated,
                                          unsigned Length,
                                          unsigned CharByteWidth) {
  void *Mem = Ctx.Allocate(totalSizeToAlloc<unsigned, SourceLocation, char>(
                               1, NumConcatenated, Length * CharByteWidth),
                           alignof(StringLiteral));
  return new (Mem)
      StringLiteral(EmptyShell(), NumConcatenated, Length, CharByteWidth);
}

void StringLiteral::outputString(llvm::raw_ostream &OS) const {
  switch (getKind()) {
  case StringLiteralKind::Unevaluated:
  case StringLiteralKind::Ordinary:
    break; // no prefix.
  case StringLiteralKind::Wide:
    OS << 'L';
    break;
  case StringLiteralKind::UTF8:
    OS << "u8";
    break;
  case StringLiteralKind::UTF16:
    OS << 'u';
    break;
  case StringLiteralKind::UTF32:
    OS << 'U';
    break;
  }
  OS << '"';
  static const char Hex[] = "0123456789ABCDEF";

  unsigned LastSlashX = getLength();
  for (unsigned I = 0, N = getLength(); I != N; ++I) {
    uint32_t Char = getCodeUnit(I);
    llvm::StringRef Escaped = escapeCStyle<EscapeChar::Double>(Char);
    if (Escaped.empty()) {

      // Convert UTF-16 surrogate pairs back to codepoints before rendering.
      // Leave invalid surrogates alone; we'll use \x for those.
      if (getKind() == StringLiteralKind::UTF16 && I != N - 1 &&
          Char >= 0xd800 && Char <= 0xdbff) {
        uint32_t Trail = getCodeUnit(I + 1);
        if (Trail >= 0xdc00 && Trail <= 0xdfff) {
          Char = 0x10000 + ((Char - 0xd800) << 10) + (Trail - 0xdc00);
          ++I;
        }
      }

      if (Char > 0xff) {
        // If this is a wide string, output characters over 0xff using \x
        // escapes. Otherwise, this is a UTF-16 or UTF-32 string, and Char is a
        // codepoint: use \x escapes for invalid codepoints.
        if (getKind() == StringLiteralKind::Wide ||
            (Char >= 0xd800 && Char <= 0xdfff) || Char >= 0x110000) {
          OS << "\\x";
          int Shift = 28;
          while ((Char >> Shift) == 0)
            Shift -= 4;
          for (/**/; Shift >= 0; Shift -= 4)
            OS << Hex[(Char >> Shift) & 15];
          LastSlashX = I;
          continue;
        }

        if (Char > 0xffff)
          OS << "\\U00" << Hex[(Char >> 20) & 15] << Hex[(Char >> 16) & 15];
        else
          OS << "\\u";
        OS << Hex[(Char >> 12) & 15] << Hex[(Char >> 8) & 15]
           << Hex[(Char >> 4) & 15] << Hex[(Char >> 0) & 15];
        continue;
      }

      // If we used \x... for the previous character, and this character is a
      // hexadecimal digit, prevent it being slurped as part of the \x.
      if (LastSlashX + 1 == I) {
        switch (Char) {
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
        case 'a':
        case 'b':
        case 'c':
        case 'd':
        case 'e':
        case 'f':
        case 'A':
        case 'B':
        case 'C':
        case 'D':
        case 'E':
        case 'F':
          OS << "\"\"";
        }
      }

      assert(Char <= 0xff &&
             "Characters above 0xff should already have been handled.");

      if (isPrintable(Char))
        OS << (char)Char;
      else // Output anything hard as an octal escape.
        OS << '\\' << (char)('0' + ((Char >> 6) & 7))
           << (char)('0' + ((Char >> 3) & 7))
           << (char)('0' + ((Char >> 0) & 7));
    } else {
      OS << Escaped;
    }
  }
  OS << '"';
}

SourceLocation
StringLiteral::getLocationOfByte(unsigned ByteNo, const SourceManager &SM,
                                 const LangOptions &Features,
                                 const TargetInfo &Target, unsigned *StartToken,
                                 unsigned *StartTokenByteOffset) const {
  assert((getKind() == StringLiteralKind::Ordinary ||
          getKind() == StringLiteralKind::UTF8 ||
          getKind() == StringLiteralKind::Unevaluated) &&
         "Only narrow string literals are currently supported");

  // Loop over all of the tokens in this string until we find the one that
  // contains the byte we're looking for.
  unsigned TokNo = 0;
  unsigned StringOffset = 0;
  if (StartToken)
    TokNo = *StartToken;
  if (StartTokenByteOffset) {
    StringOffset = *StartTokenByteOffset;
    ByteNo -= StringOffset;
  }
  while (true) {
    assert(TokNo < getNumConcatenated() && "Invalid byte number!");
    SourceLocation StrTokLoc = getStrTokenLoc(TokNo);

    // Get the spelling of the string so that we can get the data that makes up
    // the string literal, not the identifier for the macro it is potentially
    // expanded through.
    SourceLocation StrTokSpellingLoc = SM.getSpellingLoc(StrTokLoc);

    // Re-lex the token to get its length and original spelling.
    std::pair<FileID, unsigned> LocInfo =
        SM.getDecomposedLoc(StrTokSpellingLoc);
    bool Invalid = false;
    llvm::StringRef Buffer = SM.getBufferData(LocInfo.first, &Invalid);
    if (Invalid) {
      if (StartTokenByteOffset != nullptr)
        *StartTokenByteOffset = StringOffset;
      if (StartToken != nullptr)
        *StartToken = TokNo;
      return StrTokSpellingLoc;
    }

    const char *StrData = Buffer.data() + LocInfo.second;
    SourceScanner TheLexer(SM.getLocForStartOfFile(LocInfo.first), Features,
                           Buffer.begin(), StrData, Buffer.end());
    Token TheTok;
    TheLexer.LexFromRawLexer(TheTok);

    // Use the StringLiteralParser to compute the length of the string in bytes.
    StringLiteralParser SLP(TheTok, SM, Features, Target);
    unsigned TokNumBytes = SLP.getStringLength();

    // If the byte is in this token, return the location of the byte.
    if (ByteNo < TokNumBytes ||
        (ByteNo == TokNumBytes && TokNo == getNumConcatenated() - 1)) {
      unsigned Offset = SLP.getOffsetOfStringByte(TheTok, ByteNo);

      // Now that we know the offset of the token in the spelling, use the
      // preprocessor to get the offset in the original source.
      if (StartTokenByteOffset != nullptr)
        *StartTokenByteOffset = StringOffset;
      if (StartToken != nullptr)
        *StartToken = TokNo;
      return SourceScanner::AdvanceToTokenCharacter(StrTokLoc, Offset, SM,
                                                    Features);
    }

    // Move to the next string token.
    StringOffset += TokNumBytes;
    ++TokNo;
    ByteNo -= TokNumBytes;
  }
}

// ===----------------------------------------------------------------------===
// Operators, calls & member expressions
// ===----------------------------------------------------------------------===

llvm::StringRef UnaryOperator::getOpcodeStr(Opcode Op) {
  switch (Op) {
#define UNARY_OPERATION(Name, Spelling)                                        \
  case UO_##Name:                                                              \
    return Spelling;
#include "neverc/Tree/Expr/OperationKinds.def"
  }
  llvm_unreachable("Unknown unary operator");
}

CallExpr::CallExpr(StmtClass SC, Expr *Fn, llvm::ArrayRef<Expr *> PreArgs,
                   llvm::ArrayRef<Expr *> Args, QualType Ty, ExprValueKind VK,
                   SourceLocation RParenLoc, FPOptionsOverride FPFeatures,
                   unsigned MinNumArgs)
    : Expr(SC, Ty, VK, OK_Ordinary), RParenLoc(RParenLoc) {
  NumArgs = std::max<unsigned>(Args.size(), MinNumArgs);
  unsigned NumPreArgs = PreArgs.size();
  CallExprBits.NumPreArgs = NumPreArgs;
  assert((NumPreArgs == getNumPreArgs()) && "NumPreArgs overflow!");

  unsigned OffsetToTrailingObjects = offsetToTrailingObjects(SC);
  CallExprBits.OffsetToTrailingObjects = OffsetToTrailingObjects;
  assert((CallExprBits.OffsetToTrailingObjects == OffsetToTrailingObjects) &&
         "OffsetToTrailingObjects overflow!");

  setCallee(Fn);
  for (unsigned I = 0; I != NumPreArgs; ++I)
    setPreArg(I, PreArgs[I]);
  for (unsigned I = 0; I != Args.size(); ++I)
    setArg(I, Args[I]);
  for (unsigned I = Args.size(); I != NumArgs; ++I)
    setArg(I, nullptr);

  this->computeDependence();

  CallExprBits.HasFPFeatures = FPFeatures.requiresTrailingStorage();
  if (hasStoredFPFeatures())
    setStoredFPFeatures(FPFeatures);
}

CallExpr::CallExpr(StmtClass SC, unsigned NumPreArgs, unsigned NumArgs,
                   bool HasFPFeatures, EmptyShell Empty)
    : Expr(SC, Empty), NumArgs(NumArgs) {
  CallExprBits.NumPreArgs = NumPreArgs;
  assert((NumPreArgs == getNumPreArgs()) && "NumPreArgs overflow!");

  unsigned OffsetToTrailingObjects = offsetToTrailingObjects(SC);
  CallExprBits.OffsetToTrailingObjects = OffsetToTrailingObjects;
  assert((CallExprBits.OffsetToTrailingObjects == OffsetToTrailingObjects) &&
         "OffsetToTrailingObjects overflow!");
  CallExprBits.HasFPFeatures = HasFPFeatures;
}

CallExpr *CallExpr::Create(const TreeContext &Ctx, Expr *Fn,
                           llvm::ArrayRef<Expr *> Args, QualType Ty,
                           ExprValueKind VK, SourceLocation RParenLoc,
                           FPOptionsOverride FPFeatures, unsigned MinNumArgs) {
  unsigned NumArgs = std::max<unsigned>(Args.size(), MinNumArgs);
  unsigned SizeOfTrailingObjects = CallExpr::sizeOfTrailingObjects(
      /*NumPreArgs=*/0, NumArgs, FPFeatures.requiresTrailingStorage());
  void *Mem =
      Ctx.Allocate(sizeof(CallExpr) + SizeOfTrailingObjects, alignof(CallExpr));
  return new (Mem) CallExpr(CallExprClass, Fn, /*PreArgs=*/{}, Args, Ty, VK,
                            RParenLoc, FPFeatures, MinNumArgs);
}

CallExpr *CallExpr::CreateTemporary(void *Mem, Expr *Fn, QualType Ty,
                                    ExprValueKind VK,
                                    SourceLocation RParenLoc) {
  assert(!(reinterpret_cast<uintptr_t>(Mem) % alignof(CallExpr)) &&
         "Misaligned memory in CallExpr::CreateTemporary!");
  return new (Mem) CallExpr(CallExprClass, Fn, /*PreArgs=*/{}, /*Args=*/{}, Ty,
                            VK, RParenLoc, FPOptionsOverride(),
                            /*MinNumArgs=*/0);
}

CallExpr *CallExpr::CreateEmpty(const TreeContext &Ctx, unsigned NumArgs,
                                bool HasFPFeatures, EmptyShell Empty) {
  unsigned SizeOfTrailingObjects =
      CallExpr::sizeOfTrailingObjects(/*NumPreArgs=*/0, NumArgs, HasFPFeatures);
  void *Mem =
      Ctx.Allocate(sizeof(CallExpr) + SizeOfTrailingObjects, alignof(CallExpr));
  return new (Mem)
      CallExpr(CallExprClass, /*NumPreArgs=*/0, NumArgs, HasFPFeatures, Empty);
}

unsigned CallExpr::offsetToTrailingObjects(StmtClass SC) {
  switch (SC) {
  case CallExprClass:
    return sizeof(CallExpr);
  default:
    llvm_unreachable("unexpected class deriving from CallExpr!");
  }
}

Decl *Expr::getReferencedDeclOfCallee() {
  Expr *CEE = IgnoreParenImpCasts();

  // If we're calling a dereference, look at the pointer instead.
  while (true) {
    if (auto *UO = dyn_cast<UnaryOperator>(CEE)) {
      if (UO->getOpcode() == UO_Deref || UO->getOpcode() == UO_AddrOf ||
          UO->getOpcode() == UO_Plus) {
        CEE = UO->getSubExpr()->IgnoreParenImpCasts();
        continue;
      }
    }
    break;
  }

  if (auto *DRE = dyn_cast<DeclRefExpr>(CEE))
    return DRE->getDecl();
  if (auto *ME = dyn_cast<MemberExpr>(CEE))
    return ME->getMemberDecl();
  return nullptr;
}

unsigned CallExpr::getBuiltinCallee() const {
  const auto *FDecl = getDirectCallee();
  return FDecl ? FDecl->getBuiltinID() : 0;
}

bool CallExpr::isUnevaluatedBuiltinCall(const TreeContext &Ctx) const {
  if (unsigned BI = getBuiltinCallee())
    return Ctx.BuiltinInfo.isUnevaluated(BI);
  return false;
}

QualType CallExpr::getCallReturnType(const TreeContext &Ctx) const {
  const Expr *Callee = getCallee();
  QualType CalleeType = Callee->getType();
  if (const auto *FnTypePtr = CalleeType->getAs<PointerType>()) {
    CalleeType = FnTypePtr->getPointeeType();
  } else if (CalleeType->isRecordType()) {
    // If the Callee is a record type, then it is a not-yet-resolved
    // dependent call to the call operator of that type.
    return Ctx.DependentTy;
  } else if (CalleeType->isSpecificPlaceholderType(BuiltinType::Overload)) {
    return Ctx.DependentTy;
  }

  const FunctionType *FnType = CalleeType->castAs<FunctionType>();
  return FnType->getReturnType();
}

const Attr *CallExpr::getUnusedResultAttr(const TreeContext &Ctx) const {
  // If the return type is a struct, union, or enum that is marked nodiscard,
  // then return the return type attribute.
  if (const TagDecl *TD = getCallReturnType(Ctx)->getAsTagDecl())
    if (const auto *A = TD->getAttr<WarnUnusedResultAttr>())
      return A;

  for (const auto *TD = getCallReturnType(Ctx)->getAs<TypedefType>(); TD;
       TD = TD->desugar()->getAs<TypedefType>())
    if (const auto *A = TD->getDecl()->getAttr<WarnUnusedResultAttr>())
      return A;

  // Otherwise, see if the callee is marked nodiscard and return that attribute
  // instead.
  const Decl *D = getCalleeDecl();
  return D ? D->getAttr<WarnUnusedResultAttr>() : nullptr;
}

SourceLocation CallExpr::getBeginLoc() const {
  SourceLocation begin = getCallee()->getBeginLoc();
  if (begin.isInvalid() && getNumArgs() > 0 && getArg(0))
    begin = getArg(0)->getBeginLoc();
  return begin;
}
SourceLocation CallExpr::getEndLoc() const {
  SourceLocation end = getRParenLoc();
  if (end.isInvalid() && getNumArgs() > 0 && getArg(getNumArgs() - 1))
    end = getArg(getNumArgs() - 1)->getEndLoc();
  return end;
}

OffsetOfExpr *OffsetOfExpr::Create(const TreeContext &C, QualType type,
                                   SourceLocation OperatorLoc,
                                   TypeSourceInfo *tsi,
                                   llvm::ArrayRef<OffsetOfNode> comps,
                                   llvm::ArrayRef<Expr *> exprs,
                                   SourceLocation RParenLoc) {
  void *Mem = C.Allocate(
      totalSizeToAlloc<OffsetOfNode, Expr *>(comps.size(), exprs.size()));

  return new (Mem)
      OffsetOfExpr(C, type, OperatorLoc, tsi, comps, exprs, RParenLoc);
}

OffsetOfExpr *OffsetOfExpr::CreateEmpty(const TreeContext &C, unsigned numComps,
                                        unsigned numExprs) {
  void *Mem =
      C.Allocate(totalSizeToAlloc<OffsetOfNode, Expr *>(numComps, numExprs));
  return new (Mem) OffsetOfExpr(numComps, numExprs);
}

OffsetOfExpr::OffsetOfExpr(const TreeContext &C, QualType type,
                           SourceLocation OperatorLoc, TypeSourceInfo *tsi,
                           llvm::ArrayRef<OffsetOfNode> comps,
                           llvm::ArrayRef<Expr *> exprs,
                           SourceLocation RParenLoc)
    : Expr(OffsetOfExprClass, type, VK_PRValue, OK_Ordinary),
      OperatorLoc(OperatorLoc), RParenLoc(RParenLoc), TSInfo(tsi),
      NumComps(comps.size()), NumExprs(exprs.size()) {
  for (unsigned i = 0; i != comps.size(); ++i)
    setComponent(i, comps[i]);
  for (unsigned i = 0; i != exprs.size(); ++i)
    setIndexExpr(i, exprs[i]);

  setDependence(computeDependence(this));
}

IdentifierInfo *OffsetOfNode::getFieldName() const {
  assert(getKind() == Field || getKind() == Identifier);
  if (getKind() == Field)
    return getField()->getIdentifier();

  return reinterpret_cast<IdentifierInfo *>(Data & ~(uintptr_t)Mask);
}

UnaryExprOrTypeTraitExpr::UnaryExprOrTypeTraitExpr(
    UnaryExprOrTypeTrait ExprKind, Expr *E, QualType resultType,
    SourceLocation op, SourceLocation rp)
    : Expr(UnaryExprOrTypeTraitExprClass, resultType, VK_PRValue, OK_Ordinary),
      OpLoc(op), RParenLoc(rp) {
  assert(ExprKind <= UETT_Last && "invalid enum value!");
  UnaryExprOrTypeTraitExprBits.Kind = ExprKind;
  assert(static_cast<unsigned>(ExprKind) == UnaryExprOrTypeTraitExprBits.Kind &&
         "UnaryExprOrTypeTraitExprBits.Kind overflow!");
  UnaryExprOrTypeTraitExprBits.IsType = false;
  Argument.Ex = E;
  setDependence(computeDependence(this));
}

MemberExpr::MemberExpr(Expr *Base, bool IsArrow, SourceLocation OperatorLoc,
                       ValueDecl *MemberDecl,
                       const DeclarationNameInfo &NameInfo, QualType T,
                       ExprValueKind VK, ExprObjectKind OK,
                       NonOdrUseReason NOUR)
    : Expr(MemberExprClass, T, VK, OK), Base(Base), MemberDecl(MemberDecl),
      MemberLoc(NameInfo.getLoc()) {
  assert(!NameInfo.getName() ||
         MemberDecl->getDeclName() == NameInfo.getName());
  MemberExprBits.IsArrow = IsArrow;
  MemberExprBits.HasQualifierOrFoundDecl = false;
  MemberExprBits.HadMultipleCandidates = false;
  MemberExprBits.NonOdrUseReason = NOUR;
  MemberExprBits.OperatorLoc = OperatorLoc;
  setDependence(computeDependence(this));
}

MemberExpr *MemberExpr::Create(const TreeContext &C, Expr *Base, bool IsArrow,
                               SourceLocation OperatorLoc,
                               ValueDecl *MemberDecl, NamedDecl *FoundDecl,
                               DeclarationNameInfo NameInfo, QualType T,
                               ExprValueKind VK, ExprObjectKind OK,
                               NonOdrUseReason NOUR) {
  bool HasFoundDecl = FoundDecl != MemberDecl;
  std::size_t Size =
      totalSizeToAlloc<MemberExprNameQualifier>(HasFoundDecl ? 1 : 0);

  void *Mem = C.Allocate(Size, alignof(MemberExpr));
  MemberExpr *E = new (Mem) MemberExpr(Base, IsArrow, OperatorLoc, MemberDecl,
                                       NameInfo, T, VK, OK, NOUR);

  if (HasFoundDecl) {
    E->MemberExprBits.HasQualifierOrFoundDecl = true;
    E->getTrailingObjects<MemberExprNameQualifier>()->FoundDecl = FoundDecl;
  }

  return E;
}

MemberExpr *MemberExpr::CreateEmpty(const TreeContext &Context,
                                    bool HasFoundDecl) {
  std::size_t Size =
      totalSizeToAlloc<MemberExprNameQualifier>(HasFoundDecl ? 1 : 0);
  void *Mem = Context.Allocate(Size, alignof(MemberExpr));
  return new (Mem) MemberExpr(EmptyShell());
}

void MemberExpr::setMemberDecl(ValueDecl *NewD) {
  MemberDecl = NewD;
  if (getType()->isUndeducedType())
    setType(NewD->getType());
  setDependence(computeDependence(this));
}

SourceLocation MemberExpr::getBeginLoc() const {
  SourceLocation BaseStartLoc = getBase()->getBeginLoc();
  if (BaseStartLoc.isValid())
    return BaseStartLoc;
  return MemberLoc;
}
SourceLocation MemberExpr::getEndLoc() const {
  SourceLocation EndLoc = getMemberNameInfo().getEndLoc();
  if (EndLoc.isInvalid())
    EndLoc = getBase()->getEndLoc();
  return EndLoc;
}

bool CastExpr::CastConsistency() const {
  switch (getCastKind()) {
  case CK_FunctionToPointerDecay:
    assert(getType()->isPointerType());
    assert(getSubExpr()->getType()->isFunctionType());
    break;
  case CK_AddressSpaceConversion: {
    auto Ty = getType();
    auto SETy = getSubExpr()->getType();
    if (isPRValue()) {
      Ty = Ty->getPointeeType();
      SETy = SETy->getPointeeType();
    }
    assert(!Ty.isNull() && !SETy.isNull() &&
           Ty.getAddressSpace() != SETy.getAddressSpace());
    break;
  }
  case CK_FloatingToFixedPoint:
  case CK_FixedPointToFloating:
  case CK_FixedPointCast:
  case CK_FixedPointToIntegral:
  case CK_IntegralToFixedPoint:
  case CK_MatrixCast:
    assert(!getType()->isBooleanType() && "unheralded conversion to bool");
    break;
  default:
    break;
  }
  return true;
}

const char *CastExpr::getCastKindName(CastKind CK) {
  switch (CK) {
#define CAST_OPERATION(Name)                                                   \
  case CK_##Name:                                                              \
    return #Name;
#include "neverc/Tree/Expr/OperationKinds.def"
  }
  llvm_unreachable("Unhandled cast kind!");
}

namespace {
// Skip over implicit nodes produced as part of semantic analysis.
// Designed for use with IgnoreExprNodes.
Expr *stripImplicitCasts(Expr *E) {
  if (auto *Full = dyn_cast<FullExpr>(E))
    return Full->getSubExpr();

  return E;
}
} // namespace

Expr *CastExpr::getSubExprAsWritten() {
  const Expr *SubExpr = nullptr;

  for (const CastExpr *E = this; E; E = dyn_cast<ImplicitCastExpr>(SubExpr)) {
    SubExpr = IgnoreExprNodes(E->getSubExpr(), stripImplicitCasts);
  }

  return const_cast<Expr *>(SubExpr);
}

const FieldDecl *CastExpr::getTargetFieldForToUnionCast(QualType unionType,
                                                        QualType opType) {
  auto RD = unionType->castAs<RecordType>()->getDecl();
  return getTargetFieldForToUnionCast(RD, opType);
}

const FieldDecl *CastExpr::getTargetFieldForToUnionCast(const RecordDecl *RD,
                                                        QualType OpType) {
  auto &Ctx = RD->getTreeContext();
  RecordDecl::field_iterator Field, FieldEnd;
  for (Field = RD->field_begin(), FieldEnd = RD->field_end(); Field != FieldEnd;
       ++Field) {
    if (Ctx.hasSameUnqualifiedType(Field->getType(), OpType) &&
        !Field->isUnnamedBitfield()) {
      return *Field;
    }
  }
  return nullptr;
}

FPOptionsOverride *CastExpr::getTrailingFPFeatures() {
  assert(hasStoredFPFeatures());
  switch (getStmtClass()) {
  case ImplicitCastExprClass:
    return static_cast<ImplicitCastExpr *>(this)
        ->getTrailingObjects<FPOptionsOverride>();
  case CStyleCastExprClass:
    return static_cast<CStyleCastExpr *>(this)
        ->getTrailingObjects<FPOptionsOverride>();
  default:
    llvm_unreachable("Cast does not have FPFeatures");
  }
}

ImplicitCastExpr *ImplicitCastExpr::Create(const TreeContext &C, QualType T,
                                           CastKind Kind, Expr *Operand,
                                           ExprValueKind VK,
                                           FPOptionsOverride FPO) {
  void *Buffer = C.Allocate(
      totalSizeToAlloc<FPOptionsOverride>(FPO.requiresTrailingStorage()));
  return new (Buffer) ImplicitCastExpr(T, Kind, Operand, FPO, VK);
}

ImplicitCastExpr *ImplicitCastExpr::CreateEmpty(const TreeContext &C,
                                                bool HasFPFeatures) {
  void *Buffer = C.Allocate(totalSizeToAlloc<FPOptionsOverride>(HasFPFeatures));
  return new (Buffer) ImplicitCastExpr(EmptyShell(), HasFPFeatures);
}

CStyleCastExpr *CStyleCastExpr::Create(const TreeContext &C, QualType T,
                                       ExprValueKind VK, CastKind K, Expr *Op,
                                       FPOptionsOverride FPO,
                                       TypeSourceInfo *WrittenTy,
                                       SourceLocation L, SourceLocation R) {
  void *Buffer = C.Allocate(
      totalSizeToAlloc<FPOptionsOverride>(FPO.requiresTrailingStorage()));
  return new (Buffer) CStyleCastExpr(T, VK, K, Op, FPO, WrittenTy, L, R);
}

CStyleCastExpr *CStyleCastExpr::CreateEmpty(const TreeContext &C,
                                            bool HasFPFeatures) {
  void *Buffer = C.Allocate(totalSizeToAlloc<FPOptionsOverride>(HasFPFeatures));
  return new (Buffer) CStyleCastExpr(EmptyShell(), HasFPFeatures);
}

llvm::StringRef BinaryOperator::getOpcodeStr(Opcode Op) {
  switch (Op) {
#define BINARY_OPERATION(Name, Spelling)                                       \
  case BO_##Name:                                                              \
    return Spelling;
#include "neverc/Tree/Expr/OperationKinds.def"
  }
  llvm_unreachable("Invalid OpCode!");
}

bool BinaryOperator::isNullPointerArithmeticExtension(TreeContext &Ctx,
                                                      Opcode Opc,
                                                      const Expr *LHS,
                                                      const Expr *RHS) {
  if (Opc != BO_Add)
    return false;
  const Expr *PExp;
  if (LHS->getType()->isPointerType()) {
    if (!RHS->getType()->isIntegerType())
      return false;
    PExp = LHS;
  } else if (RHS->getType()->isPointerType()) {
    if (!LHS->getType()->isIntegerType())
      return false;
    PExp = RHS;
  } else {
    return false;
  }
  if (!PExp->IgnoreParenCasts()->isNullPointerConstant(
          Ctx, Expr::NPC_ValueDependentIsNotNull))
    return false;
  const PointerType *PTy = PExp->getType()->getAs<PointerType>();
  if (!PTy || !PTy->getPointeeType()->isCharType())
    return false;

  return true;
}

SourceLocExpr::SourceLocExpr(const TreeContext &Ctx, SourceLocIdentKind Kind,
                             QualType ResultTy, SourceLocation BLoc,
                             SourceLocation RParenLoc,
                             DeclContext *ParentContext)
    : Expr(SourceLocExprClass, ResultTy, VK_PRValue, OK_Ordinary),
      BuiltinLoc(BLoc), RParenLoc(RParenLoc), ParentContext(ParentContext) {
  SourceLocExprBits.Kind = llvm::to_underlying(Kind);
  setDependence(ExprDependence::None);
}

llvm::StringRef SourceLocExpr::getBuiltinStr() const {
  switch (getIdentKind()) {
  case SourceLocIdentKind::File:
    return "__builtin_FILE";
  case SourceLocIdentKind::FileName:
    return "__builtin_FILE_NAME";
  case SourceLocIdentKind::Function:
    return "__builtin_FUNCTION";
  case SourceLocIdentKind::FuncSig:
    return "__builtin_FUNCSIG";
  case SourceLocIdentKind::Line:
    return "__builtin_LINE";
  case SourceLocIdentKind::Column:
    return "__builtin_COLUMN";
  }
  llvm_unreachable("unexpected IdentKind!");
}

APValue SourceLocExpr::EvaluateInContext(const TreeContext &Ctx,
                                         const Expr *DefaultExpr) const {
  SourceLocation Loc = getLocation();
  const DeclContext *Context = getParentContext();

  PresumedLoc PLoc = Ctx.getSourceManager().getPresumedLoc(
      Ctx.getSourceManager().getExpansionRange(Loc).getEnd());

  auto MakeStringLiteral = [&](llvm::StringRef Tmp) {
    using LValuePathEntry = APValue::LValuePathEntry;
    StringLiteral *Res = Ctx.getPredefinedStringLiteralFromCache(Tmp);
    // Decay the string to a pointer to the first character.
    LValuePathEntry Path[1] = {LValuePathEntry::ArrayIndex(0)};
    return APValue(Res, CharUnits::Zero(), Path, /*OnePastTheEnd=*/false);
  };

  switch (getIdentKind()) {
  case SourceLocIdentKind::FileName: {
    // __builtin_FILE_NAME() is a NeverC-specific extension that expands to the
    // the last part of __builtin_FILE().
    llvm::SmallString<256> FileName;
    neverc::PrepEngine::formatPathToFileName(FileName, PLoc, Ctx.getLangOpts(),
                                             Ctx.getTargetInfo());
    return MakeStringLiteral(FileName);
  }
  case SourceLocIdentKind::File: {
    llvm::SmallString<256> Path(PLoc.getFilename());
    neverc::PrepEngine::formatPathForFileMacro(Path, Ctx.getLangOpts(),
                                               Ctx.getTargetInfo());
    return MakeStringLiteral(Path);
  }
  case SourceLocIdentKind::Function:
  case SourceLocIdentKind::FuncSig: {
    const auto *CurDecl = dyn_cast<Decl>(Context);
    const auto Kind = getIdentKind() == SourceLocIdentKind::Function
                          ? PredefinedIdentKind::Function
                          : PredefinedIdentKind::FuncSig;
    return MakeStringLiteral(
        CurDecl ? PredefinedExpr::ComputeName(Kind, CurDecl) : std::string(""));
  }
  case SourceLocIdentKind::Line:
    return APValue(Ctx.MakeIntValue(PLoc.getLine(), Ctx.UnsignedIntTy));
  case SourceLocIdentKind::Column:
    return APValue(Ctx.MakeIntValue(PLoc.getColumn(), Ctx.UnsignedIntTy));
  }
  llvm_unreachable("unhandled case");
}

InitListExpr::InitListExpr(const TreeContext &C, SourceLocation lbraceloc,
                           llvm::ArrayRef<Expr *> initExprs,
                           SourceLocation rbraceloc)
    : Expr(InitListExprClass, QualType(), VK_PRValue, OK_Ordinary),
      InitExprs(C, initExprs.size()), LBraceLoc(lbraceloc),
      RBraceLoc(rbraceloc), AltForm(nullptr, true) {
  sawArrayRangeDesignator(false);
  InitExprs.insert(C, InitExprs.end(), initExprs.begin(), initExprs.end());

  setDependence(computeDependence(this));
}

void InitListExpr::reserveInits(const TreeContext &C, unsigned NumInits) {
  if (NumInits > InitExprs.size())
    InitExprs.reserve(C, NumInits);
}

void InitListExpr::resizeInits(const TreeContext &C, unsigned NumInits) {
  InitExprs.resize(C, NumInits, nullptr);
}

Expr *InitListExpr::updateInit(const TreeContext &C, unsigned Init,
                               Expr *expr) {
  if (Init >= InitExprs.size()) {
    InitExprs.insert(C, InitExprs.end(), Init - InitExprs.size() + 1, nullptr);
    setInit(Init, expr);
    return nullptr;
  }

  Expr *Result = cast_or_null<Expr>(InitExprs[Init]);
  setInit(Init, expr);
  return Result;
}

void InitListExpr::setArrayFiller(Expr *filler) {
  assert(!hasArrayFiller() && "Filler already set!");
  ArrayFillerOrUnionFieldInit = filler;
  // Fill out any "holes" in the array due to designated initializers.
  Expr **inits = getInits();
  for (unsigned i = 0, e = getNumInits(); i != e; ++i)
    if (inits[i] == nullptr)
      inits[i] = filler;
}

bool InitListExpr::isStringLiteralInit() const {
  if (getNumInits() != 1)
    return false;
  const ArrayType *AT = getType()->getAsArrayTypeUnsafe();
  if (!AT || !AT->getElementType()->isIntegerType())
    return false;
  // It is possible for getInit() to return null.
  const Expr *Init = getInit(0);
  if (!Init)
    return false;
  Init = Init->IgnoreParenImpCasts();
  return isa<StringLiteral>(Init);
}

bool InitListExpr::isTransparent() const {
  assert(isSemanticForm() && "syntactic form never semantically transparent");

  // An lvalue InitListExpr is always just sugar.
  if (isLValue()) {
    assert(getNumInits() == 1 && "multiple inits in lvalue init list");
    return true;
  }

  // Otherwise, we're sugar if and only if we have exactly one initializer that
  // is of the same type.
  if (getNumInits() != 1 || !getInit(0))
    return false;

  // Don't confuse aggregate initialization of a struct X { X &x; }; with a
  // transparent struct copy.
  if (!getInit(0)->isPRValue() && getType()->isRecordType())
    return false;

  return getType().getCanonicalType() ==
         getInit(0)->getType().getCanonicalType();
}

bool InitListExpr::isIdiomaticZeroInitializer(
    const LangOptions &LangOpts) const {
  assert(isSyntacticForm() && "only test syntactic form as zero initializer");

  if (getNumInits() != 1 || !getInit(0)) {
    return false;
  }

  const IntegerLiteral *Lit =
      dyn_cast<IntegerLiteral>(getInit(0)->IgnoreImplicit());
  return Lit && Lit->getValue() == 0;
}

SourceLocation InitListExpr::getBeginLoc() const {
  if (InitListExpr *SyntacticForm = getSyntacticForm())
    return SyntacticForm->getBeginLoc();
  SourceLocation Beg = LBraceLoc;
  if (Beg.isInvalid()) {
    for (const Stmt *S : InitExprs) {
      if (S) {
        Beg = S->getBeginLoc();
        break;
      }
    }
  }
  return Beg;
}

SourceLocation InitListExpr::getEndLoc() const {
  if (InitListExpr *SyntacticForm = getSyntacticForm())
    return SyntacticForm->getEndLoc();
  SourceLocation End = RBraceLoc;
  if (End.isInvalid()) {
    // Find the first non-null initializer from the end.
    for (Stmt *S : llvm::reverse(InitExprs)) {
      if (S) {
        End = S->getEndLoc();
        break;
      }
    }
  }
  return End;
}

// ===----------------------------------------------------------------------===
// Diagnostics, side-effects & ignore helpers
// ===----------------------------------------------------------------------===

bool Expr::isUnusedResultAWarning(const Expr *&WarnE, SourceLocation &Loc,
                                  SourceRange &R1, SourceRange &R2,
                                  TreeContext &Ctx) const {
  // Don't warn if the expr is type dependent. The type could end up
  // instantiating to void.
  switch (getStmtClass()) {
  default:
    if (getType()->isVoidType())
      return false;
    WarnE = this;
    Loc = getExprLoc();
    R1 = getSourceRange();
    return true;
  case ParenExprClass:
    return cast<ParenExpr>(this)->getSubExpr()->isUnusedResultAWarning(
        WarnE, Loc, R1, R2, Ctx);
  case GenericSelectionExprClass:
    return cast<GenericSelectionExpr>(this)
        ->getResultExpr()
        ->isUnusedResultAWarning(WarnE, Loc, R1, R2, Ctx);
  case ChooseExprClass:
    return cast<ChooseExpr>(this)->getChosenSubExpr()->isUnusedResultAWarning(
        WarnE, Loc, R1, R2, Ctx);
  case UnaryOperatorClass: {
    const UnaryOperator *UO = cast<UnaryOperator>(this);

    switch (UO->getOpcode()) {
    case UO_Plus:
    case UO_Minus:
    case UO_AddrOf:
    case UO_Not:
    case UO_LNot:
    case UO_Deref:
      break;
    case UO_PostInc:
    case UO_PostDec:
    case UO_PreInc:
    case UO_PreDec: // ++/--
      return false; // Not a warning.
    case UO_Real:
    case UO_Imag:
      // accessing a piece of a volatile complex is a side-effect.
      if (Ctx.getCanonicalType(UO->getSubExpr()->getType())
              .isVolatileQualified())
        return false;
      break;
    case UO_Extension:
      return UO->getSubExpr()->isUnusedResultAWarning(WarnE, Loc, R1, R2, Ctx);
    }
    WarnE = this;
    Loc = UO->getOperatorLoc();
    R1 = UO->getSubExpr()->getSourceRange();
    return true;
  }
  case BinaryOperatorClass: {
    const BinaryOperator *BO = cast<BinaryOperator>(this);
    switch (BO->getOpcode()) {
    default:
      break;
    // Consider the RHS of comma for side effects. LHS was checked by
    // Sema::CheckCommaOperands.
    case BO_Comma:
      // ((foo = <blah>), 0) is an idiom for hiding the result (and
      // lvalue-ness) of an assignment written in a macro.
      if (IntegerLiteral *IE =
              dyn_cast<IntegerLiteral>(BO->getRHS()->IgnoreParens()))
        if (IE->getValue() == 0)
          return false;
      return BO->getRHS()->isUnusedResultAWarning(WarnE, Loc, R1, R2, Ctx);
    // Consider '||', '&&' to have side effects if the LHS or RHS does.
    case BO_LAnd:
    case BO_LOr:
      if (!BO->getLHS()->isUnusedResultAWarning(WarnE, Loc, R1, R2, Ctx) ||
          !BO->getRHS()->isUnusedResultAWarning(WarnE, Loc, R1, R2, Ctx))
        return false;
      break;
    }
    if (BO->isAssignmentOp())
      return false;
    WarnE = this;
    Loc = BO->getOperatorLoc();
    R1 = BO->getLHS()->getSourceRange();
    R2 = BO->getRHS()->getSourceRange();
    return true;
  }
  case CompoundAssignOperatorClass:
  case VAArgExprClass:
  case AtomicExprClass:
    return false;

  case ConditionalOperatorClass: {
    // If only one of the LHS or RHS is a warning, the operator might
    // be being used for control flow. Only warn if both the LHS and
    // RHS are warnings.
    const auto *Exp = cast<ConditionalOperator>(this);
    return Exp->getLHS()->isUnusedResultAWarning(WarnE, Loc, R1, R2, Ctx) &&
           Exp->getRHS()->isUnusedResultAWarning(WarnE, Loc, R1, R2, Ctx);
  }
  case BinaryConditionalOperatorClass: {
    const auto *Exp = cast<BinaryConditionalOperator>(this);
    return Exp->getFalseExpr()->isUnusedResultAWarning(WarnE, Loc, R1, R2, Ctx);
  }

  case MemberExprClass:
    WarnE = this;
    Loc = cast<MemberExpr>(this)->getMemberLoc();
    R1 = SourceRange(Loc, Loc);
    R2 = cast<MemberExpr>(this)->getBase()->getSourceRange();
    return true;

  case ArraySubscriptExprClass:
    WarnE = this;
    Loc = cast<ArraySubscriptExpr>(this)->getRBracketLoc();
    R1 = cast<ArraySubscriptExpr>(this)->getLHS()->getSourceRange();
    R2 = cast<ArraySubscriptExpr>(this)->getRHS()->getSourceRange();
    return true;

  case CallExprClass: {
    // If this is a direct call, get the callee.
    const CallExpr *CE = cast<CallExpr>(this);
    if (const Decl *FD = CE->getCalleeDecl()) {
      // If the callee has attribute pure, const, or warn_unused_result, warn
      // about it. void foo() { strlen("bar"); } should warn.
      //
      // Note: If new cases are added here, DiagnoseUnusedExprResult should be
      // updated to match for QoI.
      if (CE->hasUnusedResultAttr(Ctx) || FD->hasAttr<PureAttr>() ||
          FD->hasAttr<ConstAttr>()) {
        WarnE = this;
        Loc = CE->getCallee()->getBeginLoc();
        R1 = CE->getCallee()->getSourceRange();

        if (unsigned NumArgs = CE->getNumArgs())
          R2 = SourceRange(CE->getArg(0)->getBeginLoc(),
                           CE->getArg(NumArgs - 1)->getEndLoc());
        return true;
      }
    }
    return false;
  }

  // If we don't know precisely what we're looking at, let's not warn.
  case RecoveryExprClass:
    return false;

  case PseudoObjectExprClass: {
    const auto *POE = cast<PseudoObjectExpr>(this);

    // For some syntactic forms, we should always warn.

    // For others, we should never warn.
    if (auto *BO = dyn_cast<BinaryOperator>(POE->getSyntacticForm()))
      if (BO->isAssignmentOp())
        return false;
    if (auto *UO = dyn_cast<UnaryOperator>(POE->getSyntacticForm()))
      if (UO->isIncrementDecrementOp())
        return false;

    // Otherwise, warn if the result expression would warn.
    const Expr *Result = POE->getResultExpr();
    return Result && Result->isUnusedResultAWarning(WarnE, Loc, R1, R2, Ctx);
  }

  case StmtExprClass: {
    // Statement exprs don't logically have side effects themselves, but are
    // sometimes used in macros in ways that give them a type that is unused.
    // For example ({ blah; foo(); }) will end up with a type if foo has a type.
    // however, if the result of the stmt expr is dead, we don't want to emit a
    // warning.
    const CompoundStmt *CS = cast<StmtExpr>(this)->getSubStmt();
    if (!CS->body_empty()) {
      if (const Expr *E = dyn_cast<Expr>(CS->body_back()))
        return E->isUnusedResultAWarning(WarnE, Loc, R1, R2, Ctx);
      if (const LabelStmt *Label = dyn_cast<LabelStmt>(CS->body_back()))
        if (const Expr *E = dyn_cast<Expr>(Label->getSubStmt()))
          return E->isUnusedResultAWarning(WarnE, Loc, R1, R2, Ctx);
    }

    if (getType()->isVoidType())
      return false;
    WarnE = this;
    Loc = cast<StmtExpr>(this)->getLParenLoc();
    R1 = getSourceRange();
    return true;
  }
  case CStyleCastExprClass: {
    // Ignore explicit cast to `void` unless a volatile lvalue would still
    // undergo an implicit read in this language mode.
    const CastExpr *CE = cast<CastExpr>(this);
    if (CE->getCastKind() == CK_ToVoid)
      return false;

    if (CE->getCastKind() == CK_Dependent)
      return false;

    WarnE = this;
    const CStyleCastExpr *CStyleCE = cast<CStyleCastExpr>(this);
    Loc = CStyleCE->getLParenLoc();
    R1 = CStyleCE->getSubExpr()->getSourceRange();
    return true;
  }
  case ImplicitCastExprClass: {
    const CastExpr *ICE = cast<ImplicitCastExpr>(this);

    // lvalue-to-rvalue conversion on a volatile lvalue is a side-effect.
    if (ICE->getCastKind() == CK_LValueToRValue &&
        ICE->getSubExpr()->getType().isVolatileQualified())
      return false;

    return ICE->getSubExpr()->isUnusedResultAWarning(WarnE, Loc, R1, R2, Ctx);
  }
  case ExprWithCleanupsClass:
    return cast<ExprWithCleanups>(this)->getSubExpr()->isUnusedResultAWarning(
        WarnE, Loc, R1, R2, Ctx);
  }
}

Expr *Expr::IgnoreImpCasts() {
  return IgnoreExprNodes(this, IgnoreImplicitCastsSingleStep);
}

Expr *Expr::IgnoreCasts() {
  return IgnoreExprNodes(this, IgnoreCastsSingleStep);
}

Expr *Expr::IgnoreImplicit() {
  return IgnoreExprNodes(this, IgnoreImplicitSingleStep);
}

Expr *Expr::IgnoreImplicitAsWritten() {
  return IgnoreExprNodes(this, IgnoreImplicitAsWrittenSingleStep);
}

Expr *Expr::IgnoreParens() {
  return IgnoreExprNodes(this, IgnoreParensSingleStep);
}

Expr *Expr::IgnoreParenImpCasts() {
  return IgnoreExprNodes(this, IgnoreParensSingleStep,
                         IgnoreImplicitCastsExtraSingleStep);
}

Expr *Expr::IgnoreParenCasts() {
  return IgnoreExprNodes(this, IgnoreParensSingleStep, IgnoreCastsSingleStep);
}

Expr *Expr::IgnoreParenLValueCasts() {
  return IgnoreExprNodes(this, IgnoreParensSingleStep,
                         IgnoreLValueCastsSingleStep);
}

Expr *Expr::IgnoreParenBaseCasts() {
  return IgnoreExprNodes(this, IgnoreParensSingleStep,
                         IgnoreBaseCastsSingleStep);
}

Expr *Expr::IgnoreParenNoopCasts(const TreeContext &Ctx) {
  auto IgnoreNoopCastsSingleStep = [&Ctx](Expr *E) {
    if (auto *CE = dyn_cast<CastExpr>(E)) {
      // We ignore integer <-> casts that are of the same width, ptr<->ptr and
      // ptr<->int casts of the same width. We also ignore all identity casts.
      Expr *SubExpr = CE->getSubExpr();
      bool IsIdentityCast =
          Ctx.hasSameUnqualifiedType(E->getType(), SubExpr->getType());
      bool IsSameWidthCast = (E->getType()->isPointerType() ||
                              E->getType()->isIntegralType(Ctx)) &&
                             (SubExpr->getType()->isPointerType() ||
                              SubExpr->getType()->isIntegralType(Ctx)) &&
                             (Ctx.getTypeSize(E->getType()) ==
                              Ctx.getTypeSize(SubExpr->getType()));

      if (IsIdentityCast || IsSameWidthCast)
        return SubExpr;
    }

    return E;
  };
  return IgnoreExprNodes(this, IgnoreParensSingleStep,
                         IgnoreNoopCastsSingleStep);
}

Expr *Expr::IgnoreUnlessSpelledInSource() {
  return IgnoreExprNodes(this, IgnoreImplicitSingleStep,
                         IgnoreImplicitCastsExtraSingleStep,
                         IgnoreParensOnlySingleStep);
}

bool Expr::isConstantInitializer(TreeContext &Ctx, bool IsForRef,
                                 const Expr **Culprit) const {
  // This function is attempting whether an expression is an initializer
  // which can be evaluated at compile-time. It very closely parallels
  // ConstExprEmitter in CGExprConstant.cpp; if they don't match, it
  // will lead to unexpected results.  Like ConstExprEmitter, it falls back
  // to isEvaluatable most of the time.
  //
  // If we ever capture reference-binding directly in the AST, we can
  // kill the second parameter.

  if (IsForRef) {
    if (auto *EWC = dyn_cast<ExprWithCleanups>(this))
      return EWC->getSubExpr()->isConstantInitializer(Ctx, true, Culprit);
    EvalResult Result;
    if (EvaluateAsLValue(Result, Ctx) && !Result.HasSideEffects)
      return true;
    if (Culprit)
      *Culprit = this;
    return false;
  }

  switch (getStmtClass()) {
  default:
    break;
  case Stmt::ExprWithCleanupsClass:
    return cast<ExprWithCleanups>(this)->getSubExpr()->isConstantInitializer(
        Ctx, IsForRef, Culprit);
  case StringLiteralClass:
    return true;
  case ConstantExprClass: {
    const Expr *Exp = cast<ConstantExpr>(this)->getSubExpr();
    return Exp->isConstantInitializer(Ctx, false, Culprit);
  }
  case CompoundLiteralExprClass: {
    // This handles gcc's extension that allows global initializers like
    // "struct x {int x;} x = (struct x) {};".
    const Expr *Exp = cast<CompoundLiteralExpr>(this)->getInitializer();
    return Exp->isConstantInitializer(Ctx, false, Culprit);
  }
  case DesignatedInitUpdateExprClass: {
    const DesignatedInitUpdateExpr *DIUE = cast<DesignatedInitUpdateExpr>(this);
    return DIUE->getBase()->isConstantInitializer(Ctx, false, Culprit) &&
           DIUE->getUpdater()->isConstantInitializer(Ctx, false, Culprit);
  }
  case InitListExprClass: {
    const InitListExpr *ILE = cast<InitListExpr>(this);
    assert(ILE->isSemanticForm() && "InitListExpr must be in semantic form");
    if (ILE->getType()->isArrayType()) {
      unsigned numInits = ILE->getNumInits();
      for (unsigned i = 0; i < numInits; i++) {
        if (!ILE->getInit(i)->isConstantInitializer(Ctx, false, Culprit))
          return false;
      }
      return true;
    }

    if (ILE->getType()->isRecordType()) {
      unsigned ElementNo = 0;
      RecordDecl *RD = ILE->getType()->castAs<RecordType>()->getDecl();
      for (const auto *Field : RD->fields()) {
        // If this is a union, skip all the fields that aren't being
        // initialized.
        if (RD->isUnion() && ILE->getInitializedFieldInUnion() != Field)
          continue;

        // Don't emit anonymous bitfields, they just affect layout.
        if (Field->isUnnamedBitfield())
          continue;

        if (ElementNo < ILE->getNumInits()) {
          const Expr *Elt = ILE->getInit(ElementNo++);
          if (Field->isBitField()) {
            // Bitfields have to evaluate to an integer.
            EvalResult Result;
            if (!Elt->EvaluateAsInt(Result, Ctx)) {
              if (Culprit)
                *Culprit = Elt;
              return false;
            }
          } else {
            if (!Elt->isConstantInitializer(Ctx, false, Culprit))
              return false;
          }
        }
      }
      return true;
    }

    break;
  }
  case ImplicitValueInitExprClass:
  case NoInitExprClass:
    return true;
  case ParenExprClass:
    return cast<ParenExpr>(this)->getSubExpr()->isConstantInitializer(
        Ctx, IsForRef, Culprit);
  case GenericSelectionExprClass:
    return cast<GenericSelectionExpr>(this)
        ->getResultExpr()
        ->isConstantInitializer(Ctx, IsForRef, Culprit);
  case ChooseExprClass:
    if (cast<ChooseExpr>(this)->isConditionDependent()) {
      if (Culprit)
        *Culprit = this;
      return false;
    }
    return cast<ChooseExpr>(this)->getChosenSubExpr()->isConstantInitializer(
        Ctx, IsForRef, Culprit);
  case UnaryOperatorClass: {
    const UnaryOperator *Exp = cast<UnaryOperator>(this);
    if (Exp->getOpcode() == UO_Extension)
      return Exp->getSubExpr()->isConstantInitializer(Ctx, false, Culprit);
    break;
  }
  case ImplicitCastExprClass:
  case CStyleCastExprClass: {
    const CastExpr *CE = cast<CastExpr>(this);

    if (CE->getCastKind() == CK_NoOp ||
        CE->getCastKind() == CK_LValueToRValue ||
        CE->getCastKind() == CK_ToUnion ||
        CE->getCastKind() == CK_NonAtomicToAtomic ||
        CE->getCastKind() == CK_AtomicToNonAtomic ||
        CE->getCastKind() == CK_NullToPointer)
      return CE->getSubExpr()->isConstantInitializer(Ctx, false, Culprit);

    break;
  }
  }
  // Allow certain forms of UB in constant initializers: signed integer
  // overflow and floating-point division by zero. We'll give a warning on
  // these, but they're common enough that we have to accept them.
  if (isEvaluatable(Ctx, SE_AllowUndefinedBehavior))
    return true;
  if (Culprit)
    *Culprit = this;
  return false;
}

bool CallExpr::isBuiltinAssumeFalse(const TreeContext &Ctx) const {
  unsigned BuiltinID = getBuiltinCallee();
  if (BuiltinID != Builtin::BI__assume &&
      BuiltinID != Builtin::BI__builtin_assume)
    return false;

  const Expr *Arg = getArg(0);
  bool ArgVal;
  return Arg->EvaluateAsBooleanCondition(ArgVal, Ctx) && !ArgVal;
}

namespace {
class SideEffectFinder : public ConstEvaluatedExprVisitor<SideEffectFinder> {
  typedef ConstEvaluatedExprVisitor<SideEffectFinder> Inherited;
  const bool IncludePossibleEffects;
  bool HasSideEffects;

public:
  explicit SideEffectFinder(const TreeContext &Context, bool IncludePossible)
      : Inherited(Context), IncludePossibleEffects(IncludePossible),
        HasSideEffects(false) {}

  bool hasSideEffects() const { return HasSideEffects; }

  void VisitDecl(const Decl *D) {
    if (!D)
      return;

    // We assume the caller checks subexpressions (eg, the initializer, VLA
    // bounds) for side-effects on our behalf.
    if (auto *VD = dyn_cast<VarDecl>(D)) {
      // Registering a destructor is a side-effect.
      if (IncludePossibleEffects && VD->isThisDeclarationADefinition() &&
          VD->needsDestruction(Context))
        HasSideEffects = true;
    }
  }

  void VisitDeclStmt(const DeclStmt *DS) {
    for (auto *D : DS->decls())
      VisitDecl(D);
    Inherited::VisitDeclStmt(DS);
  }

  void VisitExpr(const Expr *E) {
    if (!HasSideEffects && E->HasSideEffects(Context, IncludePossibleEffects))
      HasSideEffects = true;
  }
};
} // namespace

bool Expr::HasSideEffects(const TreeContext &Ctx,
                          bool IncludePossibleEffects) const {
  // In circumstances where we care about definite side effects instead of
  // potential side effects, we want to ignore expressions that are part of a
  // macro expansion as a potential side effect.
  if (!IncludePossibleEffects && getExprLoc().isMacroID())
    return false;

  switch (getStmtClass()) {
  case NoStmtClass:
#define ABSTRACT_STMT(Type)
#define STMT(Type, Base) case Type##Class:
#define EXPR(Type, Base)
#include "neverc/Tree/StmtNodes.td.h"
    llvm_unreachable("unexpected Expr kind");

  case TypoExprClass:
  case RecoveryExprClass:
    // Make a conservative assumption for dependent nodes.
    return IncludePossibleEffects;

  case DeclRefExprClass:
  case PredefinedExprClass:
  case IntegerLiteralClass:
  case FixedPointLiteralClass:
  case FloatingLiteralClass:
  case ImaginaryLiteralClass:
  case StringLiteralClass:
  case CharacterLiteralClass:
  case OffsetOfExprClass:
  case ImplicitValueInitExprClass:
  case UnaryExprOrTypeTraitExprClass:
  case AddrLabelExprClass:
  case ArrayInitIndexExprClass:
  case NoInitExprClass:
  case NullPtrLiteralExprClass:
  case OpaqueValueExprClass:
  case SourceLocExprClass:
    return false;

  case ConstantExprClass:
    return cast<ConstantExpr>(this)->getSubExpr()->HasSideEffects(
        Ctx, IncludePossibleEffects);

  case CallExprClass: {
    // We don't know a call definitely has side effects, except for calls
    // to pure/const functions that definitely don't.
    // If the call itself is considered side-effect free, check the operands.
    const Decl *FD = cast<CallExpr>(this)->getCalleeDecl();
    bool IsPure = FD && (FD->hasAttr<ConstAttr>() || FD->hasAttr<PureAttr>());
    if (IsPure || !IncludePossibleEffects)
      break;
    return true;
  }

  case CompoundAssignOperatorClass:
  case VAArgExprClass:
  case AtomicExprClass:
    // These always have a side-effect.
    return true;

  case StmtExprClass: {
    // StmtExprs have a side-effect if any substatement does.
    SideEffectFinder Finder(Ctx, IncludePossibleEffects);
    Finder.Visit(cast<StmtExpr>(this)->getSubStmt());
    return Finder.hasSideEffects();
  }

  case ExprWithCleanupsClass:
    if (IncludePossibleEffects)
      if (cast<ExprWithCleanups>(this)->cleanupsHaveSideEffects())
        return true;
    break;

  case ParenExprClass:
  case ArraySubscriptExprClass:
  case MatrixSubscriptExprClass:
  case MemberExprClass:
  case ConditionalOperatorClass:
  case BinaryConditionalOperatorClass:
  case CompoundLiteralExprClass:
  case ExtVectorElementExprClass:
  case DesignatedInitExprClass:
  case DesignatedInitUpdateExprClass:
  case ArrayInitLoopExprClass:
  case ParenListExprClass:
  case ShuffleVectorExprClass:
  case ConvertVectorExprClass:
    // These have a side-effect if any subexpression does.
    break;

  case UnaryOperatorClass:
    if (cast<UnaryOperator>(this)->isIncrementDecrementOp())
      return true;
    break;

  case BinaryOperatorClass:
    if (cast<BinaryOperator>(this)->isAssignmentOp())
      return true;
    break;

  case InitListExprClass:
    if (const Expr *E = cast<InitListExpr>(this)->getArrayFiller())
      if (E->HasSideEffects(Ctx, IncludePossibleEffects))
        return true;
    break;

  case GenericSelectionExprClass:
    return cast<GenericSelectionExpr>(this)->getResultExpr()->HasSideEffects(
        Ctx, IncludePossibleEffects);

  case ChooseExprClass:
    return cast<ChooseExpr>(this)->getChosenSubExpr()->HasSideEffects(
        Ctx, IncludePossibleEffects);

  case ImplicitCastExprClass:
  case CStyleCastExprClass: {
    // While volatile reads are side-effecting, we treat them
    // as having possible (not definite) side-effects. This allows idiomatic
    // code to behave without warning, such as sizeof(*v) for a volatile-
    // qualified pointer.
    if (!IncludePossibleEffects)
      break;

    const CastExpr *CE = cast<CastExpr>(this);
    if (CE->getCastKind() == CK_LValueToRValue &&
        CE->getSubExpr()->getType().isVolatileQualified())
      return true;
    break;
  }

  case PseudoObjectExprClass: {
    // Only look for side-effects in the semantic form, and look past
    // OpaqueValueExpr bindings in that form.
    const PseudoObjectExpr *PO = cast<PseudoObjectExpr>(this);
    for (PseudoObjectExpr::const_semantics_iterator I = PO->semantics_begin(),
                                                    E = PO->semantics_end();
         I != E; ++I) {
      const Expr *Subexpr = *I;
      if (const OpaqueValueExpr *OVE = dyn_cast<OpaqueValueExpr>(Subexpr))
        Subexpr = OVE->getSourceExpr();
      if (Subexpr->HasSideEffects(Ctx, IncludePossibleEffects))
        return true;
    }
    return false;
  }
  }

  // Recurse to children.
  for (const Stmt *SubStmt : children())
    if (SubStmt &&
        cast<Expr>(SubStmt)->HasSideEffects(Ctx, IncludePossibleEffects))
      return true;

  return false;
}

FPOptions Expr::getFPFeaturesInEffect(const LangOptions &LO) const {
  if (auto Call = dyn_cast<CallExpr>(this))
    return Call->getFPFeaturesInEffect(LO);
  if (auto UO = dyn_cast<UnaryOperator>(this))
    return UO->getFPFeaturesInEffect(LO);
  if (auto BO = dyn_cast<BinaryOperator>(this))
    return BO->getFPFeaturesInEffect(LO);
  if (auto Cast = dyn_cast<CastExpr>(this))
    return Cast->getFPFeaturesInEffect(LO);
  return FPOptions::defaultWithoutTrailingStorage(LO);
}

Expr::NullPointerConstantKind
Expr::isNullPointerConstant(TreeContext &Ctx,
                            NullPointerConstantValueDependence NPC) const {
  if (isValueDependent()) {
    if (containsErrors())
      return NPCK_NotNull;
    switch (NPC) {
    case NPC_NeverValueDependent:
      llvm_unreachable("Unexpected value dependent expression!");
    case NPC_ValueDependentIsNull:
      if (getType()->isIntegralType(Ctx))
        return NPCK_ZeroExpression;
      else
        return NPCK_NotNull;
    case NPC_ValueDependentIsNotNull:
      return NPCK_NotNull;
    }
  }

  if (const ExplicitCastExpr *CE = dyn_cast<ExplicitCastExpr>(this)) {
    if (const PointerType *PT = CE->getType()->getAs<PointerType>()) {
      QualType Pointee = PT->getPointeeType();
      Qualifiers Qs = Pointee.getQualifiers();
      if (Pointee->isVoidType() && Qs.empty() &&
          CE->getSubExpr()->getType()->isIntegerType())
        return CE->getSubExpr()->isNullPointerConstant(Ctx, NPC);
    }
  } else if (const ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(this)) {
    // Ignore the ImplicitCastExpr type entirely.
    return ICE->getSubExpr()->isNullPointerConstant(Ctx, NPC);
  } else if (const ParenExpr *PE = dyn_cast<ParenExpr>(this)) {
    // Accept ((void*)0) as a null pointer constant, as many other
    // implementations do.
    return PE->getSubExpr()->isNullPointerConstant(Ctx, NPC);
  } else if (const GenericSelectionExpr *GE =
                 dyn_cast<GenericSelectionExpr>(this)) {
    if (GE->isResultDependent())
      return NPCK_NotNull;
    return GE->getResultExpr()->isNullPointerConstant(Ctx, NPC);
  } else if (const ChooseExpr *CE = dyn_cast<ChooseExpr>(this)) {
    if (CE->isConditionDependent())
      return NPCK_NotNull;
    return CE->getChosenSubExpr()->isNullPointerConstant(Ctx, NPC);
  } else if (const OpaqueValueExpr *OVE = dyn_cast<OpaqueValueExpr>(this)) {
    if (const Expr *Source = OVE->getSourceExpr())
      return Source->isNullPointerConstant(Ctx, NPC);
  }

  // If the expression has no type information, it cannot be a null pointer
  // constant.
  if (getType().isNull())
    return NPCK_NotNull;

  // C23 nullptr_t is always a null pointer constant.
  if (getType()->isNullPtrType())
    return NPCK_nullptr;

  if (const RecordType *UT = getType()->getAsUnionType())
    if (UT && UT->getDecl()->hasAttr<TransparentUnionAttr>())
      if (const CompoundLiteralExpr *CLE =
              dyn_cast<CompoundLiteralExpr>(this)) {
        const Expr *InitExpr = CLE->getInitializer();
        if (const InitListExpr *ILE = dyn_cast<InitListExpr>(InitExpr))
          return ILE->getInit(0)->isNullPointerConstant(Ctx, NPC);
      }
  // This expression must be an integer type.
  if (!getType()->isIntegerType())
    return NPCK_NotNull;

  if (!isIntegerConstantExpr(Ctx))
    return NPCK_NotNull;

  if (EvaluateKnownConstInt(Ctx) != 0)
    return NPCK_NotNull;

  if (isa<IntegerLiteral>(this))
    return NPCK_ZeroLiteral;
  return NPCK_ZeroExpression;
}

FieldDecl *Expr::getSourceBitField() {
  Expr *E = this->IgnoreParens();

  while (ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(E)) {
    if (ICE->getCastKind() == CK_LValueToRValue ||
        (ICE->isLValue() && ICE->getCastKind() == CK_NoOp))
      E = ICE->getSubExpr()->IgnoreParens();
    else
      break;
  }

  if (MemberExpr *MemRef = dyn_cast<MemberExpr>(E))
    if (FieldDecl *Field = dyn_cast<FieldDecl>(MemRef->getMemberDecl()))
      if (Field->isBitField())
        return Field;

  if (DeclRefExpr *DeclRef = dyn_cast<DeclRefExpr>(E)) {
    if (FieldDecl *Field = dyn_cast<FieldDecl>(DeclRef->getDecl()))
      if (Field->isBitField())
        return Field;
  }

  if (BinaryOperator *BinOp = dyn_cast<BinaryOperator>(E)) {
    if (BinOp->isAssignmentOp() && BinOp->getLHS())
      return BinOp->getLHS()->getSourceBitField();

    if (BinOp->getOpcode() == BO_Comma && BinOp->getRHS())
      return BinOp->getRHS()->getSourceBitField();
  }

  if (UnaryOperator *UnOp = dyn_cast<UnaryOperator>(E))
    if (UnOp->isPrefix() && UnOp->isIncrementDecrementOp())
      return UnOp->getSubExpr()->getSourceBitField();

  return nullptr;
}

bool Expr::refersToVectorElement() const {
  const Expr *E = this->IgnoreParens();

  while (const ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(E)) {
    if (ICE->isLValue() && ICE->getCastKind() == CK_NoOp)
      E = ICE->getSubExpr()->IgnoreParens();
    else
      break;
  }

  if (const ArraySubscriptExpr *ASE = dyn_cast<ArraySubscriptExpr>(E))
    return ASE->getBase()->getType()->isVectorType();

  if (isa<ExtVectorElementExpr>(E))
    return true;

  return false;
}

bool Expr::refersToGlobalRegisterVar() const {
  const Expr *E = this->IgnoreParenImpCasts();

  if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E))
    if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl()))
      if (VD->getStorageClass() == SC_Register && VD->hasAttr<AsmLabelAttr>() &&
          !VD->isLocalVarDecl())
        return true;

  return false;
}

bool Expr::isSameComparisonOperand(const Expr *E1, const Expr *E2) {
  E1 = E1->IgnoreParens();
  E2 = E2->IgnoreParens();

  if (E1->getStmtClass() != E2->getStmtClass())
    return false;

  switch (E1->getStmtClass()) {
  default:
    return false;
  case DeclRefExprClass: {
    const auto *DRE1 = cast<DeclRefExpr>(E1);
    const auto *DRE2 = cast<DeclRefExpr>(E2);
    return DRE1->isPRValue() && DRE2->isPRValue() &&
           DRE1->getDecl() == DRE2->getDecl();
  }
  case ImplicitCastExprClass: {
    // Peel off implicit casts.
    while (true) {
      const auto *ICE1 = dyn_cast<ImplicitCastExpr>(E1);
      const auto *ICE2 = dyn_cast<ImplicitCastExpr>(E2);
      if (!ICE1 || !ICE2)
        return false;
      if (ICE1->getCastKind() != ICE2->getCastKind())
        return false;
      E1 = ICE1->getSubExpr()->IgnoreParens();
      E2 = ICE2->getSubExpr()->IgnoreParens();
      // The final cast must be one of these types.
      if (ICE1->getCastKind() == CK_LValueToRValue ||
          ICE1->getCastKind() == CK_ArrayToPointerDecay ||
          ICE1->getCastKind() == CK_FunctionToPointerDecay) {
        break;
      }
    }

    const auto *DRE1 = dyn_cast<DeclRefExpr>(E1);
    const auto *DRE2 = dyn_cast<DeclRefExpr>(E2);
    if (DRE1 && DRE2)
      return declaresSameEntity(DRE1->getDecl(), DRE2->getDecl());

    const auto *Array1 = dyn_cast<ArraySubscriptExpr>(E1);
    const auto *Array2 = dyn_cast<ArraySubscriptExpr>(E2);
    if (Array1 && Array2) {
      if (!isSameComparisonOperand(Array1->getBase(), Array2->getBase()))
        return false;

      auto Idx1 = Array1->getIdx();
      auto Idx2 = Array2->getIdx();
      const auto Integer1 = dyn_cast<IntegerLiteral>(Idx1);
      const auto Integer2 = dyn_cast<IntegerLiteral>(Idx2);
      if (Integer1 && Integer2) {
        if (!llvm::APInt::isSameValue(Integer1->getValue(),
                                      Integer2->getValue()))
          return false;
      } else {
        if (!isSameComparisonOperand(Idx1, Idx2))
          return false;
      }

      return true;
    }

    // Walk the MemberExpr chain.
    while (isa<MemberExpr>(E1) && isa<MemberExpr>(E2)) {
      const auto *ME1 = cast<MemberExpr>(E1);
      const auto *ME2 = cast<MemberExpr>(E2);
      if (!declaresSameEntity(ME1->getMemberDecl(), ME2->getMemberDecl()))
        return false;
      E1 = ME1->getBase()->IgnoreParenImpCasts();
      E2 = ME2->getBase()->IgnoreParenImpCasts();
    }

    auto getAnyDecl = [](const Expr *E) -> const ValueDecl * {
      if (const auto *DRE = dyn_cast<DeclRefExpr>(E))
        return DRE->getDecl();
      if (const auto *ME = dyn_cast<MemberExpr>(E))
        return ME->getMemberDecl();
      return nullptr;
    };

    const ValueDecl *VD1 = getAnyDecl(E1);
    const ValueDecl *VD2 = getAnyDecl(E2);
    return declaresSameEntity(VD1, VD2);
  }
  }
}

bool ExtVectorElementExpr::isArrow() const {
  return getBase()->getType()->isPointerType();
}

unsigned ExtVectorElementExpr::getNumElements() const {
  if (const VectorType *VT = getType()->getAs<VectorType>())
    return VT->getNumElements();
  return 1;
}

bool ExtVectorElementExpr::containsDuplicateElements() const {
  llvm::StringRef Comp = Accessor->getName();

  // Halving swizzles do not contain duplicate elements.
  if (Comp == "hi" || Comp == "lo" || Comp == "even" || Comp == "odd")
    return false;

  // Advance past s-char prefix on hex swizzles.
  if (Comp[0] == 's' || Comp[0] == 'S')
    Comp = Comp.substr(1);

  for (unsigned i = 0, e = Comp.size(); i != e; ++i)
    if (Comp.substr(i + 1).contains(Comp[i]))
      return true;

  return false;
}

void ExtVectorElementExpr::getEncodedElementAccess(
    llvm::SmallVectorImpl<uint32_t> &Elts) const {
  llvm::StringRef Comp = Accessor->getName();
  bool isNumericAccessor = false;
  if (Comp[0] == 's' || Comp[0] == 'S') {
    Comp = Comp.substr(1);
    isNumericAccessor = true;
  }

  bool isHi = Comp == "hi";
  bool isLo = Comp == "lo";
  bool isEven = Comp == "even";
  bool isOdd = Comp == "odd";

  for (unsigned i = 0, e = getNumElements(); i != e; ++i) {
    uint64_t Index;

    if (isHi)
      Index = e + i;
    else if (isLo)
      Index = i;
    else if (isEven)
      Index = 2 * i;
    else if (isOdd)
      Index = 2 * i + 1;
    else
      Index = ExtVectorType::getAccessorIdx(Comp[i], isNumericAccessor);

    Elts.push_back(Index);
  }
}

ShuffleVectorExpr::ShuffleVectorExpr(const TreeContext &C,
                                     llvm::ArrayRef<Expr *> args, QualType Type,
                                     SourceLocation BLoc, SourceLocation RP)
    : Expr(ShuffleVectorExprClass, Type, VK_PRValue, OK_Ordinary),
      BuiltinLoc(BLoc), RParenLoc(RP), NumExprs(args.size()) {
  SubExprs = new (C) Stmt *[args.size()];
  for (unsigned i = 0; i != args.size(); i++)
    SubExprs[i] = args[i];

  setDependence(computeDependence(this));
}

void ShuffleVectorExpr::setExprs(const TreeContext &C,
                                 llvm::ArrayRef<Expr *> Exprs) {
  if (SubExprs)
    C.Deallocate(SubExprs);

  this->NumExprs = Exprs.size();
  SubExprs = new (C) Stmt *[NumExprs];
  memcpy(SubExprs, Exprs.data(), sizeof(Expr *) * Exprs.size());
}

GenericSelectionExpr::GenericSelectionExpr(
    const TreeContext &, SourceLocation GenericLoc, Expr *ControllingExpr,
    llvm::ArrayRef<TypeSourceInfo *> AssocTypes,
    llvm::ArrayRef<Expr *> AssocExprs, SourceLocation DefaultLoc,
    SourceLocation RParenLoc, unsigned ResultIndex)
    : Expr(GenericSelectionExprClass, AssocExprs[ResultIndex]->getType(),
           AssocExprs[ResultIndex]->getValueKind(),
           AssocExprs[ResultIndex]->getObjectKind()),
      NumAssocs(AssocExprs.size()), ResultIndex(ResultIndex),
      IsExprPredicate(true), DefaultLoc(DefaultLoc), RParenLoc(RParenLoc) {
  assert(AssocTypes.size() == AssocExprs.size() &&
         "Must have the same number of association expressions"
         " and TypeSourceInfo!");
  assert(ResultIndex < NumAssocs && "ResultIndex is out-of-bounds!");

  GenericSelectionExprBits.GenericLoc = GenericLoc;
  getTrailingObjects<Stmt *>()[getIndexOfControllingExpression()] =
      ControllingExpr;
  std::copy(AssocExprs.begin(), AssocExprs.end(),
            getTrailingObjects<Stmt *>() + getIndexOfStartOfAssociatedExprs());
  std::copy(AssocTypes.begin(), AssocTypes.end(),
            getTrailingObjects<TypeSourceInfo *>() +
                getIndexOfStartOfAssociatedTypes());

  setDependence(computeDependence(this));
}

GenericSelectionExpr::GenericSelectionExpr(
    const TreeContext &, SourceLocation GenericLoc,
    TypeSourceInfo *ControllingType,
    llvm::ArrayRef<TypeSourceInfo *> AssocTypes,
    llvm::ArrayRef<Expr *> AssocExprs, SourceLocation DefaultLoc,
    SourceLocation RParenLoc, unsigned ResultIndex)
    : Expr(GenericSelectionExprClass, AssocExprs[ResultIndex]->getType(),
           AssocExprs[ResultIndex]->getValueKind(),
           AssocExprs[ResultIndex]->getObjectKind()),
      NumAssocs(AssocExprs.size()), ResultIndex(ResultIndex),
      IsExprPredicate(false), DefaultLoc(DefaultLoc), RParenLoc(RParenLoc) {
  assert(AssocTypes.size() == AssocExprs.size() &&
         "Must have the same number of association expressions"
         " and TypeSourceInfo!");
  assert(ResultIndex < NumAssocs && "ResultIndex is out-of-bounds!");

  GenericSelectionExprBits.GenericLoc = GenericLoc;
  getTrailingObjects<TypeSourceInfo *>()[getIndexOfControllingType()] =
      ControllingType;
  std::copy(AssocExprs.begin(), AssocExprs.end(),
            getTrailingObjects<Stmt *>() + getIndexOfStartOfAssociatedExprs());
  std::copy(AssocTypes.begin(), AssocTypes.end(),
            getTrailingObjects<TypeSourceInfo *>() +
                getIndexOfStartOfAssociatedTypes());

  setDependence(computeDependence(this));
}

GenericSelectionExpr::GenericSelectionExpr(
    const TreeContext &Context, SourceLocation GenericLoc,
    Expr *ControllingExpr, llvm::ArrayRef<TypeSourceInfo *> AssocTypes,
    llvm::ArrayRef<Expr *> AssocExprs, SourceLocation DefaultLoc,
    SourceLocation RParenLoc)
    : Expr(GenericSelectionExprClass, Context.DependentTy, VK_PRValue,
           OK_Ordinary),
      NumAssocs(AssocExprs.size()), ResultIndex(ResultDependentIndex),
      IsExprPredicate(true), DefaultLoc(DefaultLoc), RParenLoc(RParenLoc) {
  assert(AssocTypes.size() == AssocExprs.size() &&
         "Must have the same number of association expressions"
         " and TypeSourceInfo!");

  GenericSelectionExprBits.GenericLoc = GenericLoc;
  getTrailingObjects<Stmt *>()[getIndexOfControllingExpression()] =
      ControllingExpr;
  std::copy(AssocExprs.begin(), AssocExprs.end(),
            getTrailingObjects<Stmt *>() + getIndexOfStartOfAssociatedExprs());
  std::copy(AssocTypes.begin(), AssocTypes.end(),
            getTrailingObjects<TypeSourceInfo *>() +
                getIndexOfStartOfAssociatedTypes());

  setDependence(computeDependence(this));
}

GenericSelectionExpr::GenericSelectionExpr(
    const TreeContext &Context, SourceLocation GenericLoc,
    TypeSourceInfo *ControllingType,
    llvm::ArrayRef<TypeSourceInfo *> AssocTypes,
    llvm::ArrayRef<Expr *> AssocExprs, SourceLocation DefaultLoc,
    SourceLocation RParenLoc)
    : Expr(GenericSelectionExprClass, Context.DependentTy, VK_PRValue,
           OK_Ordinary),
      NumAssocs(AssocExprs.size()), ResultIndex(ResultDependentIndex),
      IsExprPredicate(false), DefaultLoc(DefaultLoc), RParenLoc(RParenLoc) {
  assert(AssocTypes.size() == AssocExprs.size() &&
         "Must have the same number of association expressions"
         " and TypeSourceInfo!");

  GenericSelectionExprBits.GenericLoc = GenericLoc;
  getTrailingObjects<TypeSourceInfo *>()[getIndexOfControllingType()] =
      ControllingType;
  std::copy(AssocExprs.begin(), AssocExprs.end(),
            getTrailingObjects<Stmt *>() + getIndexOfStartOfAssociatedExprs());
  std::copy(AssocTypes.begin(), AssocTypes.end(),
            getTrailingObjects<TypeSourceInfo *>() +
                getIndexOfStartOfAssociatedTypes());

  setDependence(computeDependence(this));
}

GenericSelectionExpr::GenericSelectionExpr(EmptyShell Empty, unsigned NumAssocs)
    : Expr(GenericSelectionExprClass, Empty), NumAssocs(NumAssocs) {}

GenericSelectionExpr *GenericSelectionExpr::Create(
    const TreeContext &Context, SourceLocation GenericLoc,
    Expr *ControllingExpr, llvm::ArrayRef<TypeSourceInfo *> AssocTypes,
    llvm::ArrayRef<Expr *> AssocExprs, SourceLocation DefaultLoc,
    SourceLocation RParenLoc, unsigned ResultIndex) {
  unsigned NumAssocs = AssocExprs.size();
  void *Mem = Context.Allocate(
      totalSizeToAlloc<Stmt *, TypeSourceInfo *>(1 + NumAssocs, NumAssocs),
      alignof(GenericSelectionExpr));
  return new (Mem)
      GenericSelectionExpr(Context, GenericLoc, ControllingExpr, AssocTypes,
                           AssocExprs, DefaultLoc, RParenLoc, ResultIndex);
}

GenericSelectionExpr *GenericSelectionExpr::Create(
    const TreeContext &Context, SourceLocation GenericLoc,
    Expr *ControllingExpr, llvm::ArrayRef<TypeSourceInfo *> AssocTypes,
    llvm::ArrayRef<Expr *> AssocExprs, SourceLocation DefaultLoc,
    SourceLocation RParenLoc) {
  unsigned NumAssocs = AssocExprs.size();
  void *Mem = Context.Allocate(
      totalSizeToAlloc<Stmt *, TypeSourceInfo *>(1 + NumAssocs, NumAssocs),
      alignof(GenericSelectionExpr));
  return new (Mem)
      GenericSelectionExpr(Context, GenericLoc, ControllingExpr, AssocTypes,
                           AssocExprs, DefaultLoc, RParenLoc);
}

GenericSelectionExpr *GenericSelectionExpr::Create(
    const TreeContext &Context, SourceLocation GenericLoc,
    TypeSourceInfo *ControllingType,
    llvm::ArrayRef<TypeSourceInfo *> AssocTypes,
    llvm::ArrayRef<Expr *> AssocExprs, SourceLocation DefaultLoc,
    SourceLocation RParenLoc, unsigned ResultIndex) {
  unsigned NumAssocs = AssocExprs.size();
  void *Mem = Context.Allocate(
      totalSizeToAlloc<Stmt *, TypeSourceInfo *>(1 + NumAssocs, NumAssocs),
      alignof(GenericSelectionExpr));
  return new (Mem)
      GenericSelectionExpr(Context, GenericLoc, ControllingType, AssocTypes,
                           AssocExprs, DefaultLoc, RParenLoc, ResultIndex);
}

GenericSelectionExpr *GenericSelectionExpr::Create(
    const TreeContext &Context, SourceLocation GenericLoc,
    TypeSourceInfo *ControllingType,
    llvm::ArrayRef<TypeSourceInfo *> AssocTypes,
    llvm::ArrayRef<Expr *> AssocExprs, SourceLocation DefaultLoc,
    SourceLocation RParenLoc) {
  unsigned NumAssocs = AssocExprs.size();
  void *Mem = Context.Allocate(
      totalSizeToAlloc<Stmt *, TypeSourceInfo *>(1 + NumAssocs, NumAssocs),
      alignof(GenericSelectionExpr));
  return new (Mem)
      GenericSelectionExpr(Context, GenericLoc, ControllingType, AssocTypes,
                           AssocExprs, DefaultLoc, RParenLoc);
}

GenericSelectionExpr *
GenericSelectionExpr::CreateEmpty(const TreeContext &Context,
                                  unsigned NumAssocs) {
  void *Mem = Context.Allocate(
      totalSizeToAlloc<Stmt *, TypeSourceInfo *>(1 + NumAssocs, NumAssocs),
      alignof(GenericSelectionExpr));
  return new (Mem) GenericSelectionExpr(EmptyShell(), NumAssocs);
}

const IdentifierInfo *DesignatedInitExpr::Designator::getFieldName() const {
  assert(isFieldDesignator() && "Only valid on a field designator");
  if (FieldInfo.NameOrField & 0x01)
    return reinterpret_cast<IdentifierInfo *>(FieldInfo.NameOrField & ~0x01);
  return getFieldDecl()->getIdentifier();
}

DesignatedInitExpr::DesignatedInitExpr(const TreeContext &C, QualType Ty,
                                       llvm::ArrayRef<Designator> Designators,
                                       SourceLocation EqualOrColonLoc,
                                       bool GNUSyntax,
                                       llvm::ArrayRef<Expr *> IndexExprs,
                                       Expr *Init)
    : Expr(DesignatedInitExprClass, Ty, Init->getValueKind(),
           Init->getObjectKind()),
      EqualOrColonLoc(EqualOrColonLoc), GNUSyntax(GNUSyntax),
      NumDesignators(Designators.size()), NumSubExprs(IndexExprs.size() + 1) {
  this->Designators = new (C) Designator[NumDesignators];

  // Record the initializer itself.
  child_iterator Child = child_begin();
  *Child++ = Init;

  // Copy the designators and their subexpressions, computing
  // value-dependence along the way.
  unsigned IndexIdx = 0;
  for (unsigned I = 0; I != NumDesignators; ++I) {
    this->Designators[I] = Designators[I];
    if (this->Designators[I].isArrayDesignator()) {
      // Copy the index expressions into permanent storage.
      *Child++ = IndexExprs[IndexIdx++];
    } else if (this->Designators[I].isArrayRangeDesignator()) {
      // Copy the start/end expressions into permanent storage.
      *Child++ = IndexExprs[IndexIdx++];
      *Child++ = IndexExprs[IndexIdx++];
    }
  }

  assert(IndexIdx == IndexExprs.size() && "Wrong number of index expressions");
  setDependence(computeDependence(this));
}

DesignatedInitExpr *DesignatedInitExpr::Create(
    const TreeContext &C, llvm::ArrayRef<Designator> Designators,
    llvm::ArrayRef<Expr *> IndexExprs, SourceLocation ColonOrEqualLoc,
    bool UsesColonSyntax, Expr *Init) {
  void *Mem = C.Allocate(totalSizeToAlloc<Stmt *>(IndexExprs.size() + 1),
                         alignof(DesignatedInitExpr));
  return new (Mem) DesignatedInitExpr(C, C.VoidTy, Designators, ColonOrEqualLoc,
                                      UsesColonSyntax, IndexExprs, Init);
}

DesignatedInitExpr *DesignatedInitExpr::CreateEmpty(const TreeContext &C,
                                                    unsigned NumIndexExprs) {
  void *Mem = C.Allocate(totalSizeToAlloc<Stmt *>(NumIndexExprs + 1),
                         alignof(DesignatedInitExpr));
  return new (Mem) DesignatedInitExpr(NumIndexExprs + 1);
}

void DesignatedInitExpr::setDesignators(const TreeContext &C,
                                        const Designator *Desigs,
                                        unsigned NumDesigs) {
  Designators = new (C) Designator[NumDesigs];
  NumDesignators = NumDesigs;
  for (unsigned I = 0; I != NumDesigs; ++I)
    Designators[I] = Desigs[I];
}

SourceRange DesignatedInitExpr::getDesignatorsSourceRange() const {
  DesignatedInitExpr *DIE = const_cast<DesignatedInitExpr *>(this);
  if (size() == 1)
    return DIE->getDesignator(0)->getSourceRange();
  return SourceRange(DIE->getDesignator(0)->getBeginLoc(),
                     DIE->getDesignator(size() - 1)->getEndLoc());
}

SourceLocation DesignatedInitExpr::getBeginLoc() const {
  auto *DIE = const_cast<DesignatedInitExpr *>(this);
  Designator &First = *DIE->getDesignator(0);
  if (First.isFieldDesignator())
    return GNUSyntax ? First.getFieldLoc() : First.getDotLoc();
  return First.getLBracketLoc();
}

SourceLocation DesignatedInitExpr::getEndLoc() const {
  return getInit()->getEndLoc();
}

Expr *DesignatedInitExpr::getArrayIndex(const Designator &D) const {
  assert(D.isArrayDesignator() && "Requires array designator");
  return getSubExpr(D.getArrayIndex() + 1);
}

Expr *DesignatedInitExpr::getArrayRangeStart(const Designator &D) const {
  assert(D.isArrayRangeDesignator() && "Requires array range designator");
  return getSubExpr(D.getArrayIndex() + 1);
}

Expr *DesignatedInitExpr::getArrayRangeEnd(const Designator &D) const {
  assert(D.isArrayRangeDesignator() && "Requires array range designator");
  return getSubExpr(D.getArrayIndex() + 2);
}

void DesignatedInitExpr::ExpandDesignator(const TreeContext &C, unsigned Idx,
                                          const Designator *First,
                                          const Designator *Last) {
  unsigned NumNewDesignators = Last - First;
  if (NumNewDesignators == 0) {
    std::copy_backward(Designators + Idx + 1, Designators + NumDesignators,
                       Designators + Idx);
    --NumNewDesignators;
    return;
  }
  if (NumNewDesignators == 1) {
    Designators[Idx] = *First;
    return;
  }

  Designator *NewDesignators =
      new (C) Designator[NumDesignators - 1 + NumNewDesignators];
  std::copy(Designators, Designators + Idx, NewDesignators);
  std::copy(First, Last, NewDesignators + Idx);
  std::copy(Designators + Idx + 1, Designators + NumDesignators,
            NewDesignators + Idx + NumNewDesignators);
  Designators = NewDesignators;
  NumDesignators = NumDesignators - 1 + NumNewDesignators;
}

DesignatedInitUpdateExpr::DesignatedInitUpdateExpr(const TreeContext &C,
                                                   SourceLocation lBraceLoc,
                                                   Expr *baseExpr,
                                                   SourceLocation rBraceLoc)
    : Expr(DesignatedInitUpdateExprClass, baseExpr->getType(), VK_PRValue,
           OK_Ordinary) {
  BaseAndUpdaterExprs[0] = baseExpr;

  InitListExpr *ILE =
      new (C) InitListExpr(C, lBraceLoc, std::nullopt, rBraceLoc);
  ILE->setType(baseExpr->getType());
  BaseAndUpdaterExprs[1] = ILE;

  setDependence(ExprDependence::None);
}

SourceLocation DesignatedInitUpdateExpr::getBeginLoc() const {
  return getBase()->getBeginLoc();
}

SourceLocation DesignatedInitUpdateExpr::getEndLoc() const {
  return getBase()->getEndLoc();
}

ParenListExpr::ParenListExpr(SourceLocation LParenLoc,
                             llvm::ArrayRef<Expr *> Exprs,
                             SourceLocation RParenLoc)
    : Expr(ParenListExprClass, QualType(), VK_PRValue, OK_Ordinary),
      LParenLoc(LParenLoc), RParenLoc(RParenLoc) {
  ParenListExprBits.NumExprs = Exprs.size();

  for (unsigned I = 0, N = Exprs.size(); I != N; ++I)
    getTrailingObjects<Stmt *>()[I] = Exprs[I];
  setDependence(computeDependence(this));
}

ParenListExpr::ParenListExpr(EmptyShell Empty, unsigned NumExprs)
    : Expr(ParenListExprClass, Empty) {
  ParenListExprBits.NumExprs = NumExprs;
}

ParenListExpr *ParenListExpr::Create(const TreeContext &Ctx,
                                     SourceLocation LParenLoc,
                                     llvm::ArrayRef<Expr *> Exprs,
                                     SourceLocation RParenLoc) {
  void *Mem = Ctx.Allocate(totalSizeToAlloc<Stmt *>(Exprs.size()),
                           alignof(ParenListExpr));
  return new (Mem) ParenListExpr(LParenLoc, Exprs, RParenLoc);
}

ParenListExpr *ParenListExpr::CreateEmpty(const TreeContext &Ctx,
                                          unsigned NumExprs) {
  void *Mem =
      Ctx.Allocate(totalSizeToAlloc<Stmt *>(NumExprs), alignof(ParenListExpr));
  return new (Mem) ParenListExpr(EmptyShell(), NumExprs);
}

BinaryOperator::BinaryOperator(const TreeContext &Ctx, Expr *lhs, Expr *rhs,
                               Opcode opc, QualType ResTy, ExprValueKind VK,
                               ExprObjectKind OK, SourceLocation opLoc,
                               FPOptionsOverride FPFeatures)
    : Expr(BinaryOperatorClass, ResTy, VK, OK) {
  BinaryOperatorBits.Opc = opc;
  assert(!isCompoundAssignmentOp() &&
         "Use CompoundAssignOperator for compound assignments");
  BinaryOperatorBits.OpLoc = opLoc;
  SubExprs[LHS] = lhs;
  SubExprs[RHS] = rhs;
  BinaryOperatorBits.HasFPFeatures = FPFeatures.requiresTrailingStorage();
  if (hasStoredFPFeatures())
    setStoredFPFeatures(FPFeatures);
  setDependence(computeDependence(this));
}

BinaryOperator::BinaryOperator(const TreeContext &Ctx, Expr *lhs, Expr *rhs,
                               Opcode opc, QualType ResTy, ExprValueKind VK,
                               ExprObjectKind OK, SourceLocation opLoc,
                               FPOptionsOverride FPFeatures,
                               bool IsCompoundAssign)
    : Expr(CompoundAssignOperatorClass, ResTy, VK, OK) {
  BinaryOperatorBits.Opc = opc;
  assert(isCompoundAssignmentOp() &&
         "Use CompoundAssignOperator for compound assignments");
  BinaryOperatorBits.OpLoc = opLoc;
  SubExprs[LHS] = lhs;
  SubExprs[RHS] = rhs;
  BinaryOperatorBits.HasFPFeatures = FPFeatures.requiresTrailingStorage();
  if (hasStoredFPFeatures())
    setStoredFPFeatures(FPFeatures);
  setDependence(computeDependence(this));
}

BinaryOperator *BinaryOperator::CreateEmpty(const TreeContext &C,
                                            bool HasFPFeatures) {
  unsigned Extra = sizeOfTrailingObjects(HasFPFeatures);
  void *Mem =
      C.Allocate(sizeof(BinaryOperator) + Extra, alignof(BinaryOperator));
  return new (Mem) BinaryOperator(EmptyShell());
}

BinaryOperator *BinaryOperator::Create(const TreeContext &C, Expr *lhs,
                                       Expr *rhs, Opcode opc, QualType ResTy,
                                       ExprValueKind VK, ExprObjectKind OK,
                                       SourceLocation opLoc,
                                       FPOptionsOverride FPFeatures) {
  bool HasFPFeatures = FPFeatures.requiresTrailingStorage();
  unsigned Extra = sizeOfTrailingObjects(HasFPFeatures);
  void *Mem =
      C.Allocate(sizeof(BinaryOperator) + Extra, alignof(BinaryOperator));
  return new (Mem)
      BinaryOperator(C, lhs, rhs, opc, ResTy, VK, OK, opLoc, FPFeatures);
}

CompoundAssignOperator *
CompoundAssignOperator::CreateEmpty(const TreeContext &C, bool HasFPFeatures) {
  unsigned Extra = sizeOfTrailingObjects(HasFPFeatures);
  void *Mem = C.Allocate(sizeof(CompoundAssignOperator) + Extra,
                         alignof(CompoundAssignOperator));
  return new (Mem) CompoundAssignOperator(C, EmptyShell(), HasFPFeatures);
}

CompoundAssignOperator *
CompoundAssignOperator::Create(const TreeContext &C, Expr *lhs, Expr *rhs,
                               Opcode opc, QualType ResTy, ExprValueKind VK,
                               ExprObjectKind OK, SourceLocation opLoc,
                               FPOptionsOverride FPFeatures,
                               QualType CompLHSType, QualType CompResultType) {
  bool HasFPFeatures = FPFeatures.requiresTrailingStorage();
  unsigned Extra = sizeOfTrailingObjects(HasFPFeatures);
  void *Mem = C.Allocate(sizeof(CompoundAssignOperator) + Extra,
                         alignof(CompoundAssignOperator));
  return new (Mem)
      CompoundAssignOperator(C, lhs, rhs, opc, ResTy, VK, OK, opLoc, FPFeatures,
                             CompLHSType, CompResultType);
}

UnaryOperator *UnaryOperator::CreateEmpty(const TreeContext &C,
                                          bool hasFPFeatures) {
  void *Mem = C.Allocate(totalSizeToAlloc<FPOptionsOverride>(hasFPFeatures),
                         alignof(UnaryOperator));
  return new (Mem) UnaryOperator(hasFPFeatures, EmptyShell());
}

UnaryOperator::UnaryOperator(const TreeContext &Ctx, Expr *input, Opcode opc,
                             QualType type, ExprValueKind VK, ExprObjectKind OK,
                             SourceLocation l, bool CanOverflow,
                             FPOptionsOverride FPFeatures)
    : Expr(UnaryOperatorClass, type, VK, OK), Val(input) {
  UnaryOperatorBits.Opc = opc;
  UnaryOperatorBits.CanOverflow = CanOverflow;
  UnaryOperatorBits.Loc = l;
  UnaryOperatorBits.HasFPFeatures = FPFeatures.requiresTrailingStorage();
  if (hasStoredFPFeatures())
    setStoredFPFeatures(FPFeatures);
  setDependence(computeDependence(this, Ctx));
}

UnaryOperator *UnaryOperator::Create(const TreeContext &C, Expr *input,
                                     Opcode opc, QualType type,
                                     ExprValueKind VK, ExprObjectKind OK,
                                     SourceLocation l, bool CanOverflow,
                                     FPOptionsOverride FPFeatures) {
  bool HasFPFeatures = FPFeatures.requiresTrailingStorage();
  unsigned Size = totalSizeToAlloc<FPOptionsOverride>(HasFPFeatures);
  void *Mem = C.Allocate(Size, alignof(UnaryOperator));
  return new (Mem)
      UnaryOperator(C, input, opc, type, VK, OK, l, CanOverflow, FPFeatures);
}

PseudoObjectExpr *PseudoObjectExpr::Create(const TreeContext &Context,
                                           EmptyShell sh,
                                           unsigned numSemanticExprs) {
  void *buffer =
      Context.Allocate(totalSizeToAlloc<Expr *>(1 + numSemanticExprs),
                       alignof(PseudoObjectExpr));
  return new (buffer) PseudoObjectExpr(sh, numSemanticExprs);
}

PseudoObjectExpr::PseudoObjectExpr(EmptyShell shell, unsigned numSemanticExprs)
    : Expr(PseudoObjectExprClass, shell) {
  PseudoObjectExprBits.NumSubExprs = numSemanticExprs + 1;
}

PseudoObjectExpr *PseudoObjectExpr::Create(const TreeContext &C, Expr *syntax,
                                           llvm::ArrayRef<Expr *> semantics,
                                           unsigned resultIndex) {
  assert(syntax && "no syntactic expression!");
  assert(semantics.size() && "no semantic expressions!");

  QualType type;
  ExprValueKind VK;
  if (resultIndex == NoResult) {
    type = C.VoidTy;
    VK = VK_PRValue;
  } else {
    assert(resultIndex < semantics.size());
    type = semantics[resultIndex]->getType();
    VK = semantics[resultIndex]->getValueKind();
    assert(semantics[resultIndex]->getObjectKind() == OK_Ordinary);
  }

  void *buffer = C.Allocate(totalSizeToAlloc<Expr *>(semantics.size() + 1),
                            alignof(PseudoObjectExpr));
  return new (buffer)
      PseudoObjectExpr(type, VK, syntax, semantics, resultIndex);
}

PseudoObjectExpr::PseudoObjectExpr(QualType type, ExprValueKind VK,
                                   Expr *syntax,
                                   llvm::ArrayRef<Expr *> semantics,
                                   unsigned resultIndex)
    : Expr(PseudoObjectExprClass, type, VK, OK_Ordinary) {
  PseudoObjectExprBits.NumSubExprs = semantics.size() + 1;
  PseudoObjectExprBits.ResultIndex = resultIndex + 1;

  for (unsigned i = 0, e = semantics.size() + 1; i != e; ++i) {
    Expr *E = (i == 0 ? syntax : semantics[i - 1]);
    getSubExprsBuffer()[i] = E;

    if (isa<OpaqueValueExpr>(E))
      assert(cast<OpaqueValueExpr>(E)->getSourceExpr() != nullptr &&
             "opaque-value semantic expressions for pseudo-object "
             "operations must have sources");
  }

  setDependence(computeDependence(this));
}

// UnaryExprOrTypeTraitExpr
Stmt::child_range UnaryExprOrTypeTraitExpr::children() {
  const_child_range CCR =
      const_cast<const UnaryExprOrTypeTraitExpr *>(this)->children();
  return child_range(cast_away_const(CCR.begin()), cast_away_const(CCR.end()));
}

Stmt::const_child_range UnaryExprOrTypeTraitExpr::children() const {
  // If this is of a type and the type is a VLA type (and not a typedef), the
  // size expression of the VLA needs to be treated as an executable expression.
  // Why isn't this weirdness documented better in StmtIterator?
  if (isArgumentType()) {
    if (const VariableArrayType *T =
            dyn_cast<VariableArrayType>(getArgumentType().getTypePtr()))
      return const_child_range(const_child_iterator(T), const_child_iterator());
    return const_child_range(const_child_iterator(), const_child_iterator());
  }
  return const_child_range(&Argument.Ex, &Argument.Ex + 1);
}

AtomicExpr::AtomicExpr(SourceLocation BLoc, llvm::ArrayRef<Expr *> args,
                       QualType t, AtomicOp op, SourceLocation RP)
    : Expr(AtomicExprClass, t, VK_PRValue, OK_Ordinary),
      NumSubExprs(args.size()), BuiltinLoc(BLoc), RParenLoc(RP), Op(op) {
  assert(args.size() == getNumSubExprs(op) && "wrong number of subexpressions");
  for (unsigned i = 0; i != args.size(); i++)
    SubExprs[i] = args[i];
  setDependence(computeDependence(this));
}

unsigned AtomicExpr::getNumSubExprs(AtomicOp Op) {
  switch (Op) {
  case AO__c11_atomic_init:
  case AO__c11_atomic_load:
  case AO__atomic_load_n:
    return 2;

  case AO__scoped_atomic_load_n:
  case AO__c11_atomic_store:
  case AO__c11_atomic_exchange:
  case AO__atomic_load:
  case AO__atomic_store:
  case AO__atomic_store_n:
  case AO__atomic_exchange_n:
  case AO__c11_atomic_fetch_add:
  case AO__c11_atomic_fetch_sub:
  case AO__c11_atomic_fetch_and:
  case AO__c11_atomic_fetch_or:
  case AO__c11_atomic_fetch_xor:
  case AO__c11_atomic_fetch_nand:
  case AO__c11_atomic_fetch_max:
  case AO__c11_atomic_fetch_min:
  case AO__atomic_fetch_add:
  case AO__atomic_fetch_sub:
  case AO__atomic_fetch_and:
  case AO__atomic_fetch_or:
  case AO__atomic_fetch_xor:
  case AO__atomic_fetch_nand:
  case AO__atomic_add_fetch:
  case AO__atomic_sub_fetch:
  case AO__atomic_and_fetch:
  case AO__atomic_or_fetch:
  case AO__atomic_xor_fetch:
  case AO__atomic_nand_fetch:
  case AO__atomic_min_fetch:
  case AO__atomic_max_fetch:
  case AO__atomic_fetch_min:
  case AO__atomic_fetch_max:
    return 3;

  case AO__scoped_atomic_load:
  case AO__scoped_atomic_store:
  case AO__scoped_atomic_store_n:
  case AO__scoped_atomic_fetch_add:
  case AO__scoped_atomic_fetch_sub:
  case AO__scoped_atomic_fetch_and:
  case AO__scoped_atomic_fetch_or:
  case AO__scoped_atomic_fetch_xor:
  case AO__scoped_atomic_fetch_nand:
  case AO__scoped_atomic_add_fetch:
  case AO__scoped_atomic_sub_fetch:
  case AO__scoped_atomic_and_fetch:
  case AO__scoped_atomic_or_fetch:
  case AO__scoped_atomic_xor_fetch:
  case AO__scoped_atomic_nand_fetch:
  case AO__scoped_atomic_min_fetch:
  case AO__scoped_atomic_max_fetch:
  case AO__scoped_atomic_fetch_min:
  case AO__scoped_atomic_fetch_max:
  case AO__scoped_atomic_exchange_n:
  case AO__atomic_exchange:
    return 4;

  case AO__scoped_atomic_exchange:
  case AO__c11_atomic_compare_exchange_strong:
  case AO__c11_atomic_compare_exchange_weak:
    return 5;
  case AO__atomic_compare_exchange:
  case AO__atomic_compare_exchange_n:
    return 6;

  case AO__scoped_atomic_compare_exchange:
  case AO__scoped_atomic_compare_exchange_n:
    return 7;
  }
  llvm_unreachable("unknown atomic op");
}

QualType AtomicExpr::getValueType() const {
  auto T = getPtr()->getType()->castAs<PointerType>()->getPointeeType();
  if (auto AT = T->getAs<AtomicType>())
    return AT->getValueType();
  return T;
}

RecoveryExpr::RecoveryExpr(TreeContext &Ctx, QualType T,
                           SourceLocation BeginLoc, SourceLocation EndLoc,
                           llvm::ArrayRef<Expr *> SubExprs)
    : Expr(RecoveryExprClass, T, VK_PRValue, OK_Ordinary), BeginLoc(BeginLoc),
      EndLoc(EndLoc), NumExprs(SubExprs.size()) {
  assert(!T.isNull());
  assert(!llvm::is_contained(SubExprs, nullptr));

  llvm::copy(SubExprs, getTrailingObjects<Expr *>());
  setDependence(computeDependence(this));
}

RecoveryExpr *RecoveryExpr::Create(TreeContext &Ctx, QualType T,
                                   SourceLocation BeginLoc,
                                   SourceLocation EndLoc,
                                   llvm::ArrayRef<Expr *> SubExprs) {
  void *Mem = Ctx.Allocate(totalSizeToAlloc<Expr *>(SubExprs.size()),
                           alignof(RecoveryExpr));
  return new (Mem) RecoveryExpr(Ctx, T, BeginLoc, EndLoc, SubExprs);
}

RecoveryExpr *RecoveryExpr::CreateEmpty(TreeContext &Ctx,
                                        unsigned NumSubExprs) {
  void *Mem = Ctx.Allocate(totalSizeToAlloc<Expr *>(NumSubExprs),
                           alignof(RecoveryExpr));
  return new (Mem) RecoveryExpr(EmptyShell(), NumSubExprs);
}

// ExprWithCleanups -- carries the trailing CleanupObject array used for
// `__attribute__((cleanup(...)))`.  Lives in `Expr.cpp` because it is an Expr.

ExprWithCleanups *
ExprWithCleanups::Create(const TreeContext &C, Expr *subexpr,
                         bool CleanupsHaveSideEffects,
                         llvm::ArrayRef<CleanupObject> objects) {
  void *buffer = C.Allocate(totalSizeToAlloc<CleanupObject>(objects.size()),
                            alignof(ExprWithCleanups));
  return new (buffer)
      ExprWithCleanups(subexpr, CleanupsHaveSideEffects, objects);
}

ExprWithCleanups *ExprWithCleanups::Create(const TreeContext &C,
                                           EmptyShell empty,
                                           unsigned numObjects) {
  void *buffer = C.Allocate(totalSizeToAlloc<CleanupObject>(numObjects),
                            alignof(ExprWithCleanups));
  auto *E = new (buffer) ExprWithCleanups(empty, numObjects);
  return E;
}

ExprWithCleanups::ExprWithCleanups(Expr *SubExpr, bool CleanupsHaveSideEffects,
                                   llvm::ArrayRef<CleanupObject> Objects)
    : FullExpr(ExprWithCleanupsClass, SubExpr) {
  ExprWithCleanupsBits.CleanupsHaveSideEffects = CleanupsHaveSideEffects;
  ExprWithCleanupsBits.NumObjects = Objects.size();
  for (unsigned i = 0, e = Objects.size(); i != e; ++i)
    getTrailingObjects<CleanupObject>()[i] = Objects[i];
}

ExprWithCleanups::ExprWithCleanups(EmptyShell empty, unsigned numObjects)
    : FullExpr(ExprWithCleanupsClass, empty) {
  ExprWithCleanupsBits.NumObjects = numObjects;
}
