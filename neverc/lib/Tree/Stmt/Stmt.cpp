#include "neverc/Tree/Stmt/Stmt.h"
#include "neverc/Foundation/Core/CharInfo.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/Token.h"
#include "neverc/Tree/Core/Attr.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Core/TreeDiag.h"
#include "neverc/Tree/Decl/DeclGroup.h"
#include "neverc/Tree/Expr/Expr.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <utility>

using namespace neverc;

// ===----------------------------------------------------------------------===
// Stmt core
// ===----------------------------------------------------------------------===

namespace {

struct StmtClassNameTable {
  const char *Name;
  unsigned Counter;
  unsigned Size;
} StmtClassInfo[Stmt::lastStmtConstant + 1];

StmtClassNameTable &getStmtInfoTableEntry(Stmt::StmtClass E) {
  static const bool Initialized = [] {
#define ABSTRACT_STMT(STMT)
#define STMT(CLASS, PARENT)                                                    \
  StmtClassInfo[(unsigned)Stmt::CLASS##Class].Name = #CLASS;                   \
  StmtClassInfo[(unsigned)Stmt::CLASS##Class].Size = sizeof(CLASS);
#include "neverc/Tree/StmtNodes.td.h"
    return true;
  }();
  (void)Initialized;
  return StmtClassInfo[E];
}

} // namespace

void *Stmt::operator new(size_t bytes, const TreeContext &C,
                         unsigned alignment) {
  return ::operator new(bytes, C, alignment);
}

const char *Stmt::getStmtClassName() const {
  return getStmtInfoTableEntry((StmtClass)StmtBits.sClass).Name;
}

// Check that no statement / expression class is polymorphic. LLVM style RTTI
// should be used instead. If absolutely needed an exception can still be added
// here by defining the appropriate macro (but please don't do this).
#define STMT(CLASS, PARENT)                                                    \
  static_assert(!std::is_polymorphic<CLASS>::value,                            \
                #CLASS " should not be polymorphic!");
#include "neverc/Tree/StmtNodes.td.h"

// Check that no statement / expression class has a non-trival destructor.
// Statements and expressions are allocated with the BumpPtrAllocator from
// TreeContext and therefore their destructor is not executed.
#define STMT(CLASS, PARENT)                                                    \
  static_assert(std::is_trivially_destructible<CLASS>::value,                  \
                #CLASS " should be trivially destructible!");
#define INITLISTEXPR(CLASS, PARENT)
#include "neverc/Tree/StmtNodes.td.h"

void Stmt::PrintStats() {
  getStmtInfoTableEntry(Stmt::NullStmtClass);

  const int N = Stmt::lastStmtConstant + 1;
  unsigned TotalCount = 0;
  uint64_t TotalBytes = 0;
  llvm::errs() << "\n*** Stmt/Expr Stats:\n";

  for (int i = 0; i < N; ++i)
    if (StmtClassInfo[i].Name)
      TotalCount += StmtClassInfo[i].Counter;

  llvm::errs() << "  " << TotalCount << " stmts/exprs total.\n";

  for (int i = 0; i < N; ++i) {
    const auto &E = StmtClassInfo[i];
    if (!E.Name || E.Counter == 0)
      continue;
    uint64_t ClassBytes = static_cast<uint64_t>(E.Counter) * E.Size;
    llvm::errs() << "    " << E.Counter << " " << E.Name << ", " << E.Size
                 << " each (" << ClassBytes << " bytes)\n";
    TotalBytes += ClassBytes;
  }

  llvm::errs() << "Total bytes = " << TotalBytes << "\n";
}

void Stmt::addStmtClass(StmtClass s) { ++getStmtInfoTableEntry(s).Counter; }

bool Stmt::StatisticsEnabled = false;
void Stmt::EnableStatistics() { StatisticsEnabled = true; }

namespace {
std::pair<Stmt::Likelihood, const Attr *>
getLikelihood(llvm::ArrayRef<const Attr *> Attrs) {
  for (const auto *A : Attrs) {
    if (isa<LikelyAttr>(A))
      return std::make_pair(Stmt::LH_Likely, A);

    if (isa<UnlikelyAttr>(A))
      return std::make_pair(Stmt::LH_Unlikely, A);
  }

  return std::make_pair(Stmt::LH_None, nullptr);
}

std::pair<Stmt::Likelihood, const Attr *> getLikelihood(const Stmt *S) {
  if (const auto *AS = dyn_cast_or_null<AttributedStmt>(S))
    return getLikelihood(AS->getAttrs());

  return std::make_pair(Stmt::LH_None, nullptr);
}
} // namespace

Stmt::Likelihood Stmt::getLikelihood(llvm::ArrayRef<const Attr *> Attrs) {
  return ::getLikelihood(Attrs).first;
}

Stmt::Likelihood Stmt::getLikelihood(const Stmt *S) {
  return ::getLikelihood(S).first;
}

const Attr *Stmt::getLikelihoodAttr(const Stmt *S) {
  return ::getLikelihood(S).second;
}

Stmt::Likelihood Stmt::getLikelihood(const Stmt *Then, const Stmt *Else) {
  Likelihood LHT = ::getLikelihood(Then).first;
  Likelihood LHE = ::getLikelihood(Else).first;
  if (LHE == LH_None)
    return LHT;

  // If the same attribute is used on both branches there's a conflict.
  if (LHT == LHE)
    return LH_None;

  if (LHT != LH_None)
    return LHT;

  // Invert the value of Else to get the value for Then.
  return LHE == LH_Likely ? LH_Unlikely : LH_Likely;
}

std::tuple<bool, const Attr *, const Attr *>
Stmt::determineLikelihoodConflict(const Stmt *Then, const Stmt *Else) {
  std::pair<Likelihood, const Attr *> LHT = ::getLikelihood(Then);
  std::pair<Likelihood, const Attr *> LHE = ::getLikelihood(Else);
  // If the same attribute is used on both branches there's a conflict.
  if (LHT.first != LH_None && LHT.first == LHE.first)
    return std::make_tuple(true, LHT.second, LHE.second);

  return std::make_tuple(false, nullptr, nullptr);
}

Stmt *Stmt::IgnoreContainers(bool IgnoreCaptured) {
  Stmt *S = this;
  while (true) {
    if (auto AS = dyn_cast_or_null<AttributedStmt>(S))
      S = AS->getSubStmt();
    else if (auto CS = dyn_cast_or_null<CompoundStmt>(S)) {
      if (CS->size() != 1)
        break;
      S = CS->body_back();
    } else
      break;
  }
  return S;
}

const Stmt *Stmt::stripLabelLikeStatements() const {
  const Stmt *S = this;
  while (true) {
    if (const auto *LS = dyn_cast<LabelStmt>(S))
      S = LS->getSubStmt();
    else if (const auto *SC = dyn_cast<SwitchCase>(S))
      S = SC->getSubStmt();
    else if (const auto *AS = dyn_cast<AttributedStmt>(S))
      S = AS->getSubStmt();
    else
      return S;
  }
}

namespace {

struct good {};
struct bad {};

good is_good(good) { return good(); }

typedef Stmt::child_range children_t();
template <class T> good implements_children(children_t T::*) { return good(); }
LLVM_ATTRIBUTE_UNUSED
bad implements_children(children_t Stmt::*) { return bad(); }

typedef SourceLocation getBeginLoc_t() const;
template <class T> good implements_getBeginLoc(getBeginLoc_t T::*) {
  return good();
}
LLVM_ATTRIBUTE_UNUSED
bad implements_getBeginLoc(getBeginLoc_t Stmt::*) { return bad(); }

typedef SourceLocation getLocEnd_t() const;
template <class T> good implements_getEndLoc(getLocEnd_t T::*) {
  return good();
}
LLVM_ATTRIBUTE_UNUSED
bad implements_getEndLoc(getLocEnd_t Stmt::*) { return bad(); }

#define ASSERT_IMPLEMENTS_children(type)                                       \
  (void)is_good(implements_children(&type::children))
#define ASSERT_IMPLEMENTS_getBeginLoc(type)                                    \
  (void)is_good(implements_getBeginLoc(&type::getBeginLoc))
#define ASSERT_IMPLEMENTS_getEndLoc(type)                                      \
  (void)is_good(implements_getEndLoc(&type::getEndLoc))

LLVM_ATTRIBUTE_UNUSED
void check_implementations() {
#define ABSTRACT_STMT(type)
#define STMT(type, base)                                                       \
  ASSERT_IMPLEMENTS_children(type);                                            \
  ASSERT_IMPLEMENTS_getBeginLoc(type);                                         \
  ASSERT_IMPLEMENTS_getEndLoc(type);
#include "neverc/Tree/StmtNodes.td.h"
}

} // namespace

Stmt::child_range Stmt::children() {
  switch (getStmtClass()) {
  case Stmt::NoStmtClass:
    llvm_unreachable("statement without class");
#define ABSTRACT_STMT(type)
#define STMT(type, base)                                                       \
  case Stmt::type##Class:                                                      \
    return static_cast<type *>(this)->children();
#include "neverc/Tree/StmtNodes.td.h"
  }
  llvm_unreachable("unknown statement kind!");
}

// Amusing macro metaprogramming hack: check whether a class provides
// a more specific implementation of getSourceRange.
//
// See also Expr.cpp:getExprLoc().
namespace {

template <class S, class T>
SourceRange getSourceRangeImpl(const Stmt *stmt, SourceRange (T::*v)() const) {
  return static_cast<const S *>(stmt)->getSourceRange();
}

template <class S>
SourceRange getSourceRangeImpl(const Stmt *stmt,
                               SourceRange (Stmt::*v)() const) {
  return SourceRange(static_cast<const S *>(stmt)->getBeginLoc(),
                     static_cast<const S *>(stmt)->getEndLoc());
}

} // namespace

SourceRange Stmt::getSourceRange() const {
  switch (getStmtClass()) {
  case Stmt::NoStmtClass:
    llvm_unreachable("statement without class");
#define ABSTRACT_STMT(type)
#define STMT(type, base)                                                       \
  case Stmt::type##Class:                                                      \
    return getSourceRangeImpl<type>(this, &type::getSourceRange);
#include "neverc/Tree/StmtNodes.td.h"
  }
  llvm_unreachable("unknown statement kind!");
}

SourceLocation Stmt::getBeginLoc() const {
  switch (getStmtClass()) {
  case Stmt::NoStmtClass:
    llvm_unreachable("statement without class");
#define ABSTRACT_STMT(type)
#define STMT(type, base)                                                       \
  case Stmt::type##Class:                                                      \
    return static_cast<const type *>(this)->getBeginLoc();
#include "neverc/Tree/StmtNodes.td.h"
  }
  llvm_unreachable("unknown statement kind");
}

SourceLocation Stmt::getEndLoc() const {
  switch (getStmtClass()) {
  case Stmt::NoStmtClass:
    llvm_unreachable("statement without class");
#define ABSTRACT_STMT(type)
#define STMT(type, base)                                                       \
  case Stmt::type##Class:                                                      \
    return static_cast<const type *>(this)->getEndLoc();
#include "neverc/Tree/StmtNodes.td.h"
  }
  llvm_unreachable("unknown statement kind");
}

int64_t Stmt::getID(const TreeContext &Context) const {
  return Context.getAllocator().identifyKnownAlignedObject<Stmt>(this);
}

CompoundStmt::CompoundStmt(llvm::ArrayRef<Stmt *> Stmts,
                           FPOptionsOverride FPFeatures, SourceLocation LB,
                           SourceLocation RB)
    : Stmt(CompoundStmtClass), LBraceLoc(LB), RBraceLoc(RB) {
  CompoundStmtBits.NumStmts = Stmts.size();
  CompoundStmtBits.HasFPFeatures = FPFeatures.requiresTrailingStorage();
  setStmts(Stmts);
  if (hasStoredFPFeatures())
    setStoredFPFeatures(FPFeatures);
}

void CompoundStmt::setStmts(llvm::ArrayRef<Stmt *> Stmts) {
  assert(CompoundStmtBits.NumStmts == Stmts.size() &&
         "NumStmts doesn't fit in bits of CompoundStmtBits.NumStmts!");

  std::copy(Stmts.begin(), Stmts.end(), body_begin());
}

// ===----------------------------------------------------------------------===
// CompoundStmt
// ===----------------------------------------------------------------------===

CompoundStmt *CompoundStmt::Create(const TreeContext &C,
                                   llvm::ArrayRef<Stmt *> Stmts,
                                   FPOptionsOverride FPFeatures,
                                   SourceLocation LB, SourceLocation RB) {
  void *Mem =
      C.Allocate(totalSizeToAlloc<Stmt *, FPOptionsOverride>(
                     Stmts.size(), FPFeatures.requiresTrailingStorage()),
                 alignof(CompoundStmt));
  return new (Mem) CompoundStmt(Stmts, FPFeatures, LB, RB);
}

CompoundStmt *CompoundStmt::CreateEmpty(const TreeContext &C, unsigned NumStmts,
                                        bool HasFPFeatures) {
  void *Mem = C.Allocate(
      totalSizeToAlloc<Stmt *, FPOptionsOverride>(NumStmts, HasFPFeatures),
      alignof(CompoundStmt));
  CompoundStmt *New = new (Mem) CompoundStmt(EmptyShell());
  New->CompoundStmtBits.NumStmts = NumStmts;
  New->CompoundStmtBits.HasFPFeatures = HasFPFeatures;
  return New;
}

const Expr *ValueStmt::getExprStmt() const {
  const Stmt *S = this;
  do {
    if (const auto *E = dyn_cast<Expr>(S))
      return E;

    if (const auto *LS = dyn_cast<LabelStmt>(S))
      S = LS->getSubStmt();
    else if (const auto *AS = dyn_cast<AttributedStmt>(S))
      S = AS->getSubStmt();
    else
      llvm_unreachable("unknown kind of ValueStmt");
  } while (isa<ValueStmt>(S));

  return nullptr;
}

const char *LabelStmt::getName() const {
  return getDecl()->getIdentifier()->getNameStart();
}

AttributedStmt *AttributedStmt::Create(const TreeContext &C, SourceLocation Loc,
                                       llvm::ArrayRef<const Attr *> Attrs,
                                       Stmt *SubStmt) {
  assert(!Attrs.empty() && "Attrs should not be empty");
  void *Mem = C.Allocate(totalSizeToAlloc<const Attr *>(Attrs.size()),
                         alignof(AttributedStmt));
  return new (Mem) AttributedStmt(Loc, Attrs, SubStmt);
}

AttributedStmt *AttributedStmt::CreateEmpty(const TreeContext &C,
                                            unsigned NumAttrs) {
  assert(NumAttrs > 0 && "NumAttrs should be greater than zero");
  void *Mem = C.Allocate(totalSizeToAlloc<const Attr *>(NumAttrs),
                         alignof(AttributedStmt));
  return new (Mem) AttributedStmt(EmptyShell(), NumAttrs);
}

std::string AsmStmt::generateAsmString(const TreeContext &C) const {
  if (const auto *gccAsmStmt = dyn_cast<GCCAsmStmt>(this))
    return gccAsmStmt->generateAsmString(C);
  if (const auto *msAsmStmt = dyn_cast<MSAsmStmt>(this))
    return msAsmStmt->generateAsmString(C);
  llvm_unreachable("unknown asm statement kind!");
}

llvm::StringRef AsmStmt::getOutputConstraint(unsigned i) const {
  if (const auto *gccAsmStmt = dyn_cast<GCCAsmStmt>(this))
    return gccAsmStmt->getOutputConstraint(i);
  if (const auto *msAsmStmt = dyn_cast<MSAsmStmt>(this))
    return msAsmStmt->getOutputConstraint(i);
  llvm_unreachable("unknown asm statement kind!");
}

const Expr *AsmStmt::getOutputExpr(unsigned i) const {
  if (const auto *gccAsmStmt = dyn_cast<GCCAsmStmt>(this))
    return gccAsmStmt->getOutputExpr(i);
  if (const auto *msAsmStmt = dyn_cast<MSAsmStmt>(this))
    return msAsmStmt->getOutputExpr(i);
  llvm_unreachable("unknown asm statement kind!");
}

llvm::StringRef AsmStmt::getInputConstraint(unsigned i) const {
  if (const auto *gccAsmStmt = dyn_cast<GCCAsmStmt>(this))
    return gccAsmStmt->getInputConstraint(i);
  if (const auto *msAsmStmt = dyn_cast<MSAsmStmt>(this))
    return msAsmStmt->getInputConstraint(i);
  llvm_unreachable("unknown asm statement kind!");
}

const Expr *AsmStmt::getInputExpr(unsigned i) const {
  if (const auto *gccAsmStmt = dyn_cast<GCCAsmStmt>(this))
    return gccAsmStmt->getInputExpr(i);
  if (const auto *msAsmStmt = dyn_cast<MSAsmStmt>(this))
    return msAsmStmt->getInputExpr(i);
  llvm_unreachable("unknown asm statement kind!");
}

llvm::StringRef AsmStmt::getClobber(unsigned i) const {
  if (const auto *gccAsmStmt = dyn_cast<GCCAsmStmt>(this))
    return gccAsmStmt->getClobber(i);
  if (const auto *msAsmStmt = dyn_cast<MSAsmStmt>(this))
    return msAsmStmt->getClobber(i);
  llvm_unreachable("unknown asm statement kind!");
}

unsigned AsmStmt::getNumPlusOperands() const {
  unsigned Res = 0;
  for (unsigned i = 0, e = getNumOutputs(); i != e; ++i)
    if (isOutputPlusConstraint(i))
      ++Res;
  return Res;
}

char GCCAsmStmt::AsmStringPiece::getModifier() const {
  assert(isOperand() && "Only Operands can have modifiers.");
  return isLetter(Str[0]) ? Str[0] : '\0';
}

llvm::StringRef GCCAsmStmt::getClobber(unsigned i) const {
  return getClobberStringLiteral(i)->getString();
}

Expr *GCCAsmStmt::getOutputExpr(unsigned i) { return cast<Expr>(Exprs[i]); }

llvm::StringRef GCCAsmStmt::getOutputConstraint(unsigned i) const {
  return getOutputConstraintLiteral(i)->getString();
}

Expr *GCCAsmStmt::getInputExpr(unsigned i) {
  return cast<Expr>(Exprs[i + NumOutputs]);
}

void GCCAsmStmt::setInputExpr(unsigned i, Expr *E) {
  Exprs[i + NumOutputs] = E;
}

AddrLabelExpr *GCCAsmStmt::getLabelExpr(unsigned i) const {
  return cast<AddrLabelExpr>(Exprs[i + NumOutputs + NumInputs]);
}

llvm::StringRef GCCAsmStmt::getLabelName(unsigned i) const {
  return getLabelExpr(i)->getLabel()->getName();
}

llvm::StringRef GCCAsmStmt::getInputConstraint(unsigned i) const {
  return getInputConstraintLiteral(i)->getString();
}

void GCCAsmStmt::setOutputsAndInputsAndClobbers(
    const TreeContext &C, IdentifierInfo **Names, StringLiteral **Constraints,
    Stmt **Exprs, unsigned NumOutputs, unsigned NumInputs, unsigned NumLabels,
    StringLiteral **Clobbers, unsigned NumClobbers) {
  this->NumOutputs = NumOutputs;
  this->NumInputs = NumInputs;
  this->NumClobbers = NumClobbers;
  this->NumLabels = NumLabels;

  unsigned NumExprs = NumOutputs + NumInputs + NumLabels;

  C.Deallocate(this->Names);
  this->Names = new (C) IdentifierInfo *[NumExprs];
  std::copy(Names, Names + NumExprs, this->Names);

  C.Deallocate(this->Exprs);
  this->Exprs = new (C) Stmt *[NumExprs];
  std::copy(Exprs, Exprs + NumExprs, this->Exprs);

  unsigned NumConstraints = NumOutputs + NumInputs;
  C.Deallocate(this->Constraints);
  this->Constraints = new (C) StringLiteral *[NumConstraints];
  std::copy(Constraints, Constraints + NumConstraints, this->Constraints);

  C.Deallocate(this->Clobbers);
  this->Clobbers = new (C) StringLiteral *[NumClobbers];
  std::copy(Clobbers, Clobbers + NumClobbers, this->Clobbers);
}

int GCCAsmStmt::getNamedOperand(llvm::StringRef SymbolicName) const {
  unsigned NumOutputs = getNumOutputs();
  for (unsigned i = 0; i != NumOutputs; ++i)
    if (getOutputName(i) == SymbolicName)
      return i;

  unsigned NumInputs = getNumInputs();
  for (unsigned i = 0; i != NumInputs; ++i)
    if (getInputName(i) == SymbolicName)
      return NumOutputs + i;

  for (unsigned i = 0, e = getNumLabels(); i != e; ++i)
    if (getLabelName(i) == SymbolicName)
      return NumOutputs + NumInputs + getNumPlusOperands() + i;

  // Not found.
  return -1;
}

unsigned
GCCAsmStmt::AnalyzeAsmString(llvm::SmallVectorImpl<AsmStringPiece> &Pieces,
                             const TreeContext &C, unsigned &DiagOffs) const {
  llvm::StringRef Str = getAsmString()->getString();
  const char *StrStart = Str.begin();
  const char *StrEnd = Str.end();
  const char *CurPtr = StrStart;

  // "Simple" inline asms have no constraints or operands, just convert the asm
  // string to escape $'s.
  if (isSimple()) {
    std::string Result;
    for (; CurPtr != StrEnd; ++CurPtr) {
      switch (*CurPtr) {
      case '$':
        Result += "$$";
        break;
      default:
        Result += *CurPtr;
        break;
      }
    }
    Pieces.push_back(AsmStringPiece(Result));
    return 0;
  }

  // CurStringPiece - The current string that we are building up as we scan the
  // asm string.
  std::string CurStringPiece;

  bool HasVariants = !C.getTargetInfo().hasNoAsmVariants();

  unsigned LastAsmStringToken = 0;
  unsigned LastAsmStringOffset = 0;

  while (true) {
    // Done with the string?
    if (CurPtr == StrEnd) {
      if (!CurStringPiece.empty())
        Pieces.push_back(AsmStringPiece(CurStringPiece));
      return 0;
    }

    char CurChar = *CurPtr++;
    switch (CurChar) {
    case '$':
      CurStringPiece += "$$";
      continue;
    case '{':
      CurStringPiece += (HasVariants ? "$(" : "{");
      continue;
    case '|':
      CurStringPiece += (HasVariants ? "$|" : "|");
      continue;
    case '}':
      CurStringPiece += (HasVariants ? "$)" : "}");
      continue;
    case '%':
      break;
    default:
      CurStringPiece += CurChar;
      continue;
    }

    const TargetInfo &TI = C.getTargetInfo();

    // Escaped "%" character in asm string.
    if (CurPtr == StrEnd) {
      // % at end of string is invalid (no escape).
      DiagOffs = CurPtr - StrStart - 1;
      return diag::err_asm_invalid_escape;
    }
    char EscapedChar = *CurPtr++;
    switch (EscapedChar) {
    default:
      if (auto MaybeReplaceStr = TI.handleAsmEscapedChar(EscapedChar)) {
        CurStringPiece += *MaybeReplaceStr;
        continue;
      }
      break;
    case '%': // %% -> %
    case '{': // %{ -> {
    case '}': // %} -> }
      CurStringPiece += EscapedChar;
      continue;
    case '=': // %= -> Generate a unique ID.
      CurStringPiece += "${:uid}";
      continue;
    }

    // Otherwise, we have an operand.  If we have accumulated a string so far,
    // add it to the Pieces list.
    if (!CurStringPiece.empty()) {
      Pieces.push_back(AsmStringPiece(CurStringPiece));
      CurStringPiece.clear();
    }

    // Handle operands that have asmSymbolicName (e.g., %x[foo]) and those that
    // don't (e.g., %x4). 'x' following the '%' is the constraint modifier.

    const char *Begin = CurPtr - 1;  // Points to the character following '%'.
    const char *Percent = Begin - 1; // Points to '%'.

    if (isLetter(EscapedChar)) {
      if (CurPtr == StrEnd) { // Premature end.
        DiagOffs = CurPtr - StrStart - 1;
        return diag::err_asm_invalid_escape;
      }
      EscapedChar = *CurPtr++;
    }

    const SourceManager &SM = C.getSourceManager();
    const LangOptions &LO = C.getLangOpts();

    // Handle operands that don't have asmSymbolicName (e.g., %x4).
    if (isDigit(EscapedChar)) {
      // %n - Assembler operand n
      unsigned N = 0;

      --CurPtr;
      while (CurPtr != StrEnd && isDigit(*CurPtr))
        N = N * 10 + ((*CurPtr++) - '0');

      unsigned NumOperands = getNumOutputs() + getNumPlusOperands() +
                             getNumInputs() + getNumLabels();
      if (N >= NumOperands) {
        DiagOffs = CurPtr - StrStart - 1;
        return diag::err_asm_invalid_operand_number;
      }

      // Str contains "x4" (Operand without the leading %).
      std::string Str(Begin, CurPtr - Begin);

      // (BeginLoc, EndLoc) represents the range of the operand we are currently
      // processing. Unlike Str, the range includes the leading '%'.
      SourceLocation BeginLoc = getAsmString()->getLocationOfByte(
          Percent - StrStart, SM, LO, TI, &LastAsmStringToken,
          &LastAsmStringOffset);
      SourceLocation EndLoc = getAsmString()->getLocationOfByte(
          CurPtr - StrStart, SM, LO, TI, &LastAsmStringToken,
          &LastAsmStringOffset);

      Pieces.emplace_back(N, std::move(Str), BeginLoc, EndLoc);
      continue;
    }

    // Handle operands that have asmSymbolicName (e.g., %x[foo]).
    if (EscapedChar == '[') {
      DiagOffs = CurPtr - StrStart - 1;

      // Find the ']'.
      const char *NameEnd = (const char *)memchr(CurPtr, ']', StrEnd - CurPtr);
      if (NameEnd == nullptr)
        return diag::err_asm_unterminated_symbolic_operand_name;
      if (NameEnd == CurPtr)
        return diag::err_asm_empty_symbolic_operand_name;

      llvm::StringRef SymbolicName(CurPtr, NameEnd - CurPtr);

      int N = getNamedOperand(SymbolicName);
      if (N == -1) {
        // Verify that an operand with that name exists.
        DiagOffs = CurPtr - StrStart;
        return diag::err_asm_unknown_symbolic_operand_name;
      }

      // Str contains "x[foo]" (Operand without the leading %).
      std::string Str(Begin, NameEnd + 1 - Begin);

      // (BeginLoc, EndLoc) represents the range of the operand we are currently
      // processing. Unlike Str, the range includes the leading '%'.
      SourceLocation BeginLoc = getAsmString()->getLocationOfByte(
          Percent - StrStart, SM, LO, TI, &LastAsmStringToken,
          &LastAsmStringOffset);
      SourceLocation EndLoc = getAsmString()->getLocationOfByte(
          NameEnd + 1 - StrStart, SM, LO, TI, &LastAsmStringToken,
          &LastAsmStringOffset);

      Pieces.emplace_back(N, std::move(Str), BeginLoc, EndLoc);

      CurPtr = NameEnd + 1;
      continue;
    }

    DiagOffs = CurPtr - StrStart - 1;
    return diag::err_asm_invalid_escape;
  }
}

std::string GCCAsmStmt::generateAsmString(const TreeContext &C) const {
  // Analyze the asm string to decompose it into its pieces.  We know that Sema
  // has already done this, so it is guaranteed to be successful.
  llvm::SmallVector<GCCAsmStmt::AsmStringPiece, 4> Pieces;
  unsigned DiagOffs;
  AnalyzeAsmString(Pieces, C, DiagOffs);

  std::string AsmString;
  for (const auto &Piece : Pieces) {
    if (Piece.isString())
      AsmString += Piece.getString();
    else if (Piece.getModifier() == '\0')
      AsmString += '$' + llvm::utostr(Piece.getOperandNo());
    else
      AsmString += "${" + llvm::utostr(Piece.getOperandNo()) + ':' +
                   Piece.getModifier() + '}';
  }
  return AsmString;
}

std::string MSAsmStmt::generateAsmString(const TreeContext &C) const {
  llvm::SmallVector<llvm::StringRef, 8> Pieces;
  AsmStr.split(Pieces, "\n\t");
  std::string MSAsmString;
  for (size_t I = 0, E = Pieces.size(); I < E; ++I) {
    llvm::StringRef Instruction = Pieces[I];
    // For vex/vex2/vex3/evex masm style prefix, convert it to att style
    // since we don't support masm style prefix in backend.
    if (Instruction.starts_with("vex "))
      MSAsmString += '{' + Instruction.substr(0, 3).str() + '}' +
                     Instruction.substr(3).str();
    else if (Instruction.starts_with("vex2 ") ||
             Instruction.starts_with("vex3 ") ||
             Instruction.starts_with("evex "))
      MSAsmString += '{' + Instruction.substr(0, 4).str() + '}' +
                     Instruction.substr(4).str();
    else
      MSAsmString += Instruction.str();
    // If this is not the last instruction, adding back the '\n\t'.
    if (I < E - 1)
      MSAsmString += "\n\t";
  }
  return MSAsmString;
}

Expr *MSAsmStmt::getOutputExpr(unsigned i) { return cast<Expr>(Exprs[i]); }

Expr *MSAsmStmt::getInputExpr(unsigned i) {
  return cast<Expr>(Exprs[i + NumOutputs]);
}

void MSAsmStmt::setInputExpr(unsigned i, Expr *E) { Exprs[i + NumOutputs] = E; }

GCCAsmStmt::GCCAsmStmt(const TreeContext &C, SourceLocation asmloc,
                       bool issimple, bool isvolatile, unsigned numoutputs,
                       unsigned numinputs, IdentifierInfo **names,
                       StringLiteral **constraints, Expr **exprs,
                       StringLiteral *asmstr, unsigned numclobbers,
                       StringLiteral **clobbers, unsigned numlabels,
                       SourceLocation rparenloc)
    : AsmStmt(GCCAsmStmtClass, asmloc, issimple, isvolatile, numoutputs,
              numinputs, numclobbers),
      RParenLoc(rparenloc), AsmStr(asmstr), NumLabels(numlabels) {
  unsigned NumExprs = NumOutputs + NumInputs + NumLabels;

  Names = new (C) IdentifierInfo *[NumExprs];
  std::copy(names, names + NumExprs, Names);

  Exprs = new (C) Stmt *[NumExprs];
  std::copy(exprs, exprs + NumExprs, Exprs);

  unsigned NumConstraints = NumOutputs + NumInputs;
  Constraints = new (C) StringLiteral *[NumConstraints];
  std::copy(constraints, constraints + NumConstraints, Constraints);

  Clobbers = new (C) StringLiteral *[NumClobbers];
  std::copy(clobbers, clobbers + NumClobbers, Clobbers);
}

MSAsmStmt::MSAsmStmt(const TreeContext &C, SourceLocation asmloc,
                     SourceLocation lbraceloc, bool issimple, bool isvolatile,
                     llvm::ArrayRef<Token> asmtoks, unsigned numoutputs,
                     unsigned numinputs,
                     llvm::ArrayRef<llvm::StringRef> constraints,
                     llvm::ArrayRef<Expr *> exprs, llvm::StringRef asmstr,
                     llvm::ArrayRef<llvm::StringRef> clobbers,
                     SourceLocation endloc)
    : AsmStmt(MSAsmStmtClass, asmloc, issimple, isvolatile, numoutputs,
              numinputs, clobbers.size()),
      LBraceLoc(lbraceloc), EndLoc(endloc), NumAsmToks(asmtoks.size()) {
  initialize(C, asmstr, asmtoks, constraints, exprs, clobbers);
}

namespace {
llvm::StringRef copyIntoContext(const TreeContext &C, llvm::StringRef str) {
  return str.copy(C);
}
} // namespace

void MSAsmStmt::initialize(const TreeContext &C, llvm::StringRef asmstr,
                           llvm::ArrayRef<Token> asmtoks,
                           llvm::ArrayRef<llvm::StringRef> constraints,
                           llvm::ArrayRef<Expr *> exprs,
                           llvm::ArrayRef<llvm::StringRef> clobbers) {
  assert(NumAsmToks == asmtoks.size());
  assert(NumClobbers == clobbers.size());

  assert(exprs.size() == NumOutputs + NumInputs);
  assert(exprs.size() == constraints.size());

  AsmStr = copyIntoContext(C, asmstr);

  Exprs = new (C) Stmt *[exprs.size()];
  std::copy(exprs.begin(), exprs.end(), Exprs);

  AsmToks = new (C) Token[asmtoks.size()];
  std::copy(asmtoks.begin(), asmtoks.end(), AsmToks);

  Constraints = new (C) llvm::StringRef[exprs.size()];
  std::transform(constraints.begin(), constraints.end(), Constraints,
                 [&](llvm::StringRef Constraint) {
                   return copyIntoContext(C, Constraint);
                 });

  Clobbers = new (C) llvm::StringRef[NumClobbers];
  std::transform(
      clobbers.begin(), clobbers.end(), Clobbers,
      [&](llvm::StringRef Clobber) { return copyIntoContext(C, Clobber); });
}

IfStmt::IfStmt(const TreeContext &Ctx, SourceLocation IL, Stmt *Init,
               VarDecl *Var, Expr *Cond, SourceLocation LPL, SourceLocation RPL,
               Stmt *Then, SourceLocation EL, Stmt *Else)
    : Stmt(IfStmtClass), LParenLoc(LPL), RParenLoc(RPL) {
  bool HasElse = Else != nullptr;
  bool HasVar = Var != nullptr;
  bool HasInit = Init != nullptr;
  IfStmtBits.HasElse = HasElse;
  IfStmtBits.HasVar = HasVar;
  IfStmtBits.HasInit = HasInit;

  setCond(Cond);
  setThen(Then);
  if (HasElse)
    setElse(Else);
  if (HasVar)
    setConditionVariable(Ctx, Var);
  if (HasInit)
    setInit(Init);

  setIfLoc(IL);
  if (HasElse)
    setElseLoc(EL);
}

IfStmt::IfStmt(EmptyShell Empty, bool HasElse, bool HasVar, bool HasInit)
    : Stmt(IfStmtClass, Empty) {
  IfStmtBits.HasElse = HasElse;
  IfStmtBits.HasVar = HasVar;
  IfStmtBits.HasInit = HasInit;
}

IfStmt *IfStmt::Create(const TreeContext &Ctx, SourceLocation IL, Stmt *Init,
                       VarDecl *Var, Expr *Cond, SourceLocation LPL,
                       SourceLocation RPL, Stmt *Then, SourceLocation EL,
                       Stmt *Else) {
  bool HasElse = Else != nullptr;
  bool HasVar = Var != nullptr;
  bool HasInit = Init != nullptr;
  void *Mem = Ctx.Allocate(
      totalSizeToAlloc<Stmt *, SourceLocation>(
          NumMandatoryStmtPtr + HasElse + HasVar + HasInit, HasElse),
      alignof(IfStmt));
  return new (Mem) IfStmt(Ctx, IL, Init, Var, Cond, LPL, RPL, Then, EL, Else);
}

IfStmt *IfStmt::CreateEmpty(const TreeContext &Ctx, bool HasElse, bool HasVar,
                            bool HasInit) {
  void *Mem = Ctx.Allocate(
      totalSizeToAlloc<Stmt *, SourceLocation>(
          NumMandatoryStmtPtr + HasElse + HasVar + HasInit, HasElse),
      alignof(IfStmt));
  return new (Mem) IfStmt(EmptyShell(), HasElse, HasVar, HasInit);
}

VarDecl *IfStmt::getConditionVariable() {
  auto *DS = getConditionVariableDeclStmt();
  if (!DS)
    return nullptr;
  return cast<VarDecl>(DS->getSingleDecl());
}

void IfStmt::setConditionVariable(const TreeContext &Ctx, VarDecl *V) {
  assert(hasVarStorage() &&
         "This if statement has no storage for a condition variable!");

  if (!V) {
    getTrailingObjects<Stmt *>()[varOffset()] = nullptr;
    return;
  }

  SourceRange VarRange = V->getSourceRange();
  getTrailingObjects<Stmt *>()[varOffset()] = new (Ctx)
      DeclStmt(DeclGroupRef(V), VarRange.getBegin(), VarRange.getEnd());
}

ForStmt::ForStmt(const TreeContext &C, Stmt *Init, Expr *Cond, VarDecl *condVar,
                 Expr *Inc, Stmt *Body, SourceLocation FL, SourceLocation LP,
                 SourceLocation RP)
    : Stmt(ForStmtClass), LParenLoc(LP), RParenLoc(RP) {
  SubExprs[INIT] = Init;
  setConditionVariable(C, condVar);
  SubExprs[COND] = Cond;
  SubExprs[INC] = Inc;
  SubExprs[BODY] = Body;
  ForStmtBits.ForLoc = FL;
}

VarDecl *ForStmt::getConditionVariable() const {
  if (!SubExprs[CONDVAR])
    return nullptr;

  auto *DS = cast<DeclStmt>(SubExprs[CONDVAR]);
  return cast<VarDecl>(DS->getSingleDecl());
}

void ForStmt::setConditionVariable(const TreeContext &C, VarDecl *V) {
  if (!V) {
    SubExprs[CONDVAR] = nullptr;
    return;
  }

  SourceRange VarRange = V->getSourceRange();
  SubExprs[CONDVAR] =
      new (C) DeclStmt(DeclGroupRef(V), VarRange.getBegin(), VarRange.getEnd());
}

SwitchStmt::SwitchStmt(const TreeContext &Ctx, Stmt *Init, VarDecl *Var,
                       Expr *Cond, SourceLocation LParenLoc,
                       SourceLocation RParenLoc)
    : Stmt(SwitchStmtClass), FirstCase(nullptr), LParenLoc(LParenLoc),
      RParenLoc(RParenLoc) {
  bool HasInit = Init != nullptr;
  bool HasVar = Var != nullptr;
  SwitchStmtBits.HasInit = HasInit;
  SwitchStmtBits.HasVar = HasVar;
  SwitchStmtBits.AllEnumCasesCovered = false;

  setCond(Cond);
  setBody(nullptr);
  if (HasInit)
    setInit(Init);
  if (HasVar)
    setConditionVariable(Ctx, Var);

  setSwitchLoc(SourceLocation{});
}

SwitchStmt::SwitchStmt(EmptyShell Empty, bool HasInit, bool HasVar)
    : Stmt(SwitchStmtClass, Empty) {
  SwitchStmtBits.HasInit = HasInit;
  SwitchStmtBits.HasVar = HasVar;
  SwitchStmtBits.AllEnumCasesCovered = false;
}

SwitchStmt *SwitchStmt::Create(const TreeContext &Ctx, Stmt *Init, VarDecl *Var,
                               Expr *Cond, SourceLocation LParenLoc,
                               SourceLocation RParenLoc) {
  bool HasInit = Init != nullptr;
  bool HasVar = Var != nullptr;
  void *Mem = Ctx.Allocate(
      totalSizeToAlloc<Stmt *>(NumMandatoryStmtPtr + HasInit + HasVar),
      alignof(SwitchStmt));
  return new (Mem) SwitchStmt(Ctx, Init, Var, Cond, LParenLoc, RParenLoc);
}

SwitchStmt *SwitchStmt::CreateEmpty(const TreeContext &Ctx, bool HasInit,
                                    bool HasVar) {
  void *Mem = Ctx.Allocate(
      totalSizeToAlloc<Stmt *>(NumMandatoryStmtPtr + HasInit + HasVar),
      alignof(SwitchStmt));
  return new (Mem) SwitchStmt(EmptyShell(), HasInit, HasVar);
}

VarDecl *SwitchStmt::getConditionVariable() {
  auto *DS = getConditionVariableDeclStmt();
  if (!DS)
    return nullptr;
  return cast<VarDecl>(DS->getSingleDecl());
}

void SwitchStmt::setConditionVariable(const TreeContext &Ctx, VarDecl *V) {
  assert(hasVarStorage() &&
         "This switch statement has no storage for a condition variable!");

  if (!V) {
    getTrailingObjects<Stmt *>()[varOffset()] = nullptr;
    return;
  }

  SourceRange VarRange = V->getSourceRange();
  getTrailingObjects<Stmt *>()[varOffset()] = new (Ctx)
      DeclStmt(DeclGroupRef(V), VarRange.getBegin(), VarRange.getEnd());
}

WhileStmt::WhileStmt(const TreeContext &Ctx, VarDecl *Var, Expr *Cond,
                     Stmt *Body, SourceLocation WL, SourceLocation LParenLoc,
                     SourceLocation RParenLoc)
    : Stmt(WhileStmtClass) {
  bool HasVar = Var != nullptr;
  WhileStmtBits.HasVar = HasVar;

  setCond(Cond);
  setBody(Body);
  if (HasVar)
    setConditionVariable(Ctx, Var);

  setWhileLoc(WL);
  setLParenLoc(LParenLoc);
  setRParenLoc(RParenLoc);
}

WhileStmt::WhileStmt(EmptyShell Empty, bool HasVar)
    : Stmt(WhileStmtClass, Empty) {
  WhileStmtBits.HasVar = HasVar;
}

WhileStmt *WhileStmt::Create(const TreeContext &Ctx, VarDecl *Var, Expr *Cond,
                             Stmt *Body, SourceLocation WL,
                             SourceLocation LParenLoc,
                             SourceLocation RParenLoc) {
  bool HasVar = Var != nullptr;
  void *Mem =
      Ctx.Allocate(totalSizeToAlloc<Stmt *>(NumMandatoryStmtPtr + HasVar),
                   alignof(WhileStmt));
  return new (Mem) WhileStmt(Ctx, Var, Cond, Body, WL, LParenLoc, RParenLoc);
}

WhileStmt *WhileStmt::CreateEmpty(const TreeContext &Ctx, bool HasVar) {
  void *Mem =
      Ctx.Allocate(totalSizeToAlloc<Stmt *>(NumMandatoryStmtPtr + HasVar),
                   alignof(WhileStmt));
  return new (Mem) WhileStmt(EmptyShell(), HasVar);
}

VarDecl *WhileStmt::getConditionVariable() {
  auto *DS = getConditionVariableDeclStmt();
  if (!DS)
    return nullptr;
  return cast<VarDecl>(DS->getSingleDecl());
}

void WhileStmt::setConditionVariable(const TreeContext &Ctx, VarDecl *V) {
  assert(hasVarStorage() &&
         "This while statement has no storage for a condition variable!");

  if (!V) {
    getTrailingObjects<Stmt *>()[varOffset()] = nullptr;
    return;
  }

  SourceRange VarRange = V->getSourceRange();
  getTrailingObjects<Stmt *>()[varOffset()] = new (Ctx)
      DeclStmt(DeclGroupRef(V), VarRange.getBegin(), VarRange.getEnd());
}

// IndirectGotoStmt
LabelDecl *IndirectGotoStmt::getConstantTarget() {
  if (auto *E = dyn_cast<AddrLabelExpr>(getTarget()->IgnoreParenImpCasts()))
    return E->getLabel();
  return nullptr;
}

// ReturnStmt
ReturnStmt::ReturnStmt(SourceLocation RL, Expr *E, const VarDecl *NRVOCandidate)
    : Stmt(ReturnStmtClass), RetExpr(E) {
  bool HasNRVOCandidate = NRVOCandidate != nullptr;
  ReturnStmtBits.HasNRVOCandidate = HasNRVOCandidate;
  if (HasNRVOCandidate)
    setNRVOCandidate(NRVOCandidate);
  setReturnLoc(RL);
}

ReturnStmt::ReturnStmt(EmptyShell Empty, bool HasNRVOCandidate)
    : Stmt(ReturnStmtClass, Empty) {
  ReturnStmtBits.HasNRVOCandidate = HasNRVOCandidate;
}

ReturnStmt *ReturnStmt::Create(const TreeContext &Ctx, SourceLocation RL,
                               Expr *E, const VarDecl *NRVOCandidate) {
  bool HasNRVOCandidate = NRVOCandidate != nullptr;
  void *Mem = Ctx.Allocate(totalSizeToAlloc<const VarDecl *>(HasNRVOCandidate),
                           alignof(ReturnStmt));
  return new (Mem) ReturnStmt(RL, E, NRVOCandidate);
}

ReturnStmt *ReturnStmt::CreateEmpty(const TreeContext &Ctx,
                                    bool HasNRVOCandidate) {
  void *Mem = Ctx.Allocate(totalSizeToAlloc<const VarDecl *>(HasNRVOCandidate),
                           alignof(ReturnStmt));
  return new (Mem) ReturnStmt(EmptyShell(), HasNRVOCandidate);
}

// CaseStmt
CaseStmt *CaseStmt::Create(const TreeContext &Ctx, Expr *lhs, Expr *rhs,
                           SourceLocation caseLoc, SourceLocation ellipsisLoc,
                           SourceLocation colonLoc) {
  bool CaseStmtIsGNURange = rhs != nullptr;
  void *Mem = Ctx.Allocate(
      totalSizeToAlloc<Stmt *, SourceLocation>(
          NumMandatoryStmtPtr + CaseStmtIsGNURange, CaseStmtIsGNURange),
      alignof(CaseStmt));
  return new (Mem) CaseStmt(lhs, rhs, caseLoc, ellipsisLoc, colonLoc);
}

CaseStmt *CaseStmt::CreateEmpty(const TreeContext &Ctx,
                                bool CaseStmtIsGNURange) {
  void *Mem = Ctx.Allocate(
      totalSizeToAlloc<Stmt *, SourceLocation>(
          NumMandatoryStmtPtr + CaseStmtIsGNURange, CaseStmtIsGNURange),
      alignof(CaseStmt));
  return new (Mem) CaseStmt(EmptyShell(), CaseStmtIsGNURange);
}

SEHTryStmt::SEHTryStmt(SourceLocation TryLoc, Stmt *TryBlock, Stmt *Handler)
    : Stmt(SEHTryStmtClass), TryLoc(TryLoc) {
  Children[TRY] = TryBlock;
  Children[HANDLER] = Handler;
}

SEHTryStmt *SEHTryStmt::Create(const TreeContext &C, SourceLocation TryLoc,
                               Stmt *TryBlock, Stmt *Handler) {
  return new (C) SEHTryStmt(TryLoc, TryBlock, Handler);
}

SEHExceptStmt *SEHTryStmt::getExceptHandler() const {
  return dyn_cast<SEHExceptStmt>(getHandler());
}

SEHFinallyStmt *SEHTryStmt::getFinallyHandler() const {
  return dyn_cast<SEHFinallyStmt>(getHandler());
}

SEHExceptStmt::SEHExceptStmt(SourceLocation Loc, Expr *FilterExpr, Stmt *Block)
    : Stmt(SEHExceptStmtClass), Loc(Loc) {
  Children[FILTER_EXPR] = FilterExpr;
  Children[BLOCK] = Block;
}

SEHExceptStmt *SEHExceptStmt::Create(const TreeContext &C, SourceLocation Loc,
                                     Expr *FilterExpr, Stmt *Block) {
  return new (C) SEHExceptStmt(Loc, FilterExpr, Block);
}

SEHFinallyStmt::SEHFinallyStmt(SourceLocation Loc, Stmt *Block)
    : Stmt(SEHFinallyStmtClass), Loc(Loc), Block(Block) {}

SEHFinallyStmt *SEHFinallyStmt::Create(const TreeContext &C, SourceLocation Loc,
                                       Stmt *Block) {
  return new (C) SEHFinallyStmt(Loc, Block);
}

namespace {
const VariableArrayType *findVA(const Type *t) {
  while (const ArrayType *vt = dyn_cast<ArrayType>(t)) {
    if (const VariableArrayType *vat = dyn_cast<VariableArrayType>(vt))
      if (vat->getSizeExpr())
        return vat;

    t = vt->getElementType().getTypePtr();
  }

  return nullptr;
}
} // namespace

void StmtIteratorBase::NextVA() {
  assert(getVAPtr());

  const VariableArrayType *p = getVAPtr();
  p = findVA(p->getElementType().getTypePtr());
  setVAPtr(p);

  if (p)
    return;

  if (inDeclGroup()) {
    if (VarDecl *VD = dyn_cast<VarDecl>(*DGI))
      if (VD->hasInit())
        return;

    NextDecl();
  } else {
    assert(inSizeOfTypeVA());
    RawVAPtr = 0;
  }
}

void StmtIteratorBase::NextDecl(bool ImmediateAdvance) {
  assert(getVAPtr() == nullptr);
  assert(inDeclGroup());

  if (ImmediateAdvance)
    ++DGI;

  for (; DGI != DGE; ++DGI)
    if (AdvanceToDecl(*DGI))
      return;

  RawVAPtr = 0;
}

bool StmtIteratorBase::AdvanceToDecl(Decl *D) {
  if (VarDecl *VD = dyn_cast<VarDecl>(D)) {
    if (const VariableArrayType *VAPtr = findVA(VD->getType().getTypePtr())) {
      setVAPtr(VAPtr);
      return true;
    }

    if (VD->getInit())
      return true;
  } else if (TypedefNameDecl *TD = dyn_cast<TypedefNameDecl>(D)) {
    if (const VariableArrayType *VAPtr =
            findVA(TD->getUnderlyingType().getTypePtr())) {
      setVAPtr(VAPtr);
      return true;
    }
  } else if (EnumConstantDecl *ECD = dyn_cast<EnumConstantDecl>(D)) {
    if (ECD->getInitExpr())
      return true;
  }

  return false;
}

StmtIteratorBase::StmtIteratorBase(Decl **dgi, Decl **dge)
    : DGI(dgi), RawVAPtr(DeclGroupMode), DGE(dge) {
  NextDecl(false);
}

StmtIteratorBase::StmtIteratorBase(const VariableArrayType *t)
    : DGI(nullptr), RawVAPtr(SizeOfTypeVAMode) {
  RawVAPtr |= reinterpret_cast<uintptr_t>(t);
}

Stmt *&StmtIteratorBase::GetDeclExpr() const {
  if (const VariableArrayType *VAPtr = getVAPtr()) {
    assert(VAPtr->SizeExpr);
    return const_cast<Stmt *&>(VAPtr->SizeExpr);
  }

  assert(inDeclGroup());
  VarDecl *VD = cast<VarDecl>(*DGI);
  return *VD->getInitAddress();
}
