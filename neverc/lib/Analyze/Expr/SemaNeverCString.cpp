//===- SemaNeverCString.cpp -- NeverC string Sema helpers (out-of-line) ---===//
//
// Out-of-line definitions for the NeverC builtin string helpers that
// were previously inline in SemaInternal.h.  Moving them here avoids
// compiling their bodies in every translation unit that includes
// SemaInternal.h (23 files) when only 3 files actually call them.
//

#include "neverc/Analyze/Lookup.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Expr/Expr.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

using namespace neverc;

StringLiteral *neverc::getNeverCStringLiteral(Expr *E) {
  E = E->IgnoreParens();
  auto *SL = dyn_cast<StringLiteral>(E);
  if (!SL)
    return nullptr;
  switch (SL->getKind()) {
  case StringLiteralKind::Ordinary:
  case StringLiteralKind::UTF8:
  case StringLiteralKind::Wide:
  case StringLiteralKind::UTF16:
  case StringLiteralKind::UTF32:
    return SL;
  default:
    return nullptr;
  }
}

void neverc::encodeNeverCStringWideCodeUnitsToUtf8(
    StringLiteral *SL, unsigned WidthInBits, llvm::SmallVectorImpl<char> &Out) {
  auto AppendCodepoint = [&Out](uint32_t CP) {
    if (CP > 0x10FFFFu || (CP >= 0xD800u && CP <= 0xDFFFu))
      CP = 0xFFFDu;
    if (CP < 0x80u) {
      Out.push_back(static_cast<char>(CP));
    } else if (CP < 0x800u) {
      Out.push_back(static_cast<char>(0xC0u | (CP >> 6)));
      Out.push_back(static_cast<char>(0x80u | (CP & 0x3Fu)));
    } else if (CP < 0x10000u) {
      Out.push_back(static_cast<char>(0xE0u | (CP >> 12)));
      Out.push_back(static_cast<char>(0x80u | ((CP >> 6) & 0x3Fu)));
      Out.push_back(static_cast<char>(0x80u | (CP & 0x3Fu)));
    } else {
      Out.push_back(static_cast<char>(0xF0u | (CP >> 18)));
      Out.push_back(static_cast<char>(0x80u | ((CP >> 12) & 0x3Fu)));
      Out.push_back(static_cast<char>(0x80u | ((CP >> 6) & 0x3Fu)));
      Out.push_back(static_cast<char>(0x80u | (CP & 0x3Fu)));
    }
  };

  unsigned Length = SL->getLength();
  if (WidthInBits == 16) {
    for (unsigned i = 0; i < Length; ++i) {
      uint32_t U = SL->getCodeUnit(i) & 0xFFFFu;
      if (U >= 0xD800u && U <= 0xDBFFu && i + 1 < Length) {
        uint32_t Lo = SL->getCodeUnit(i + 1) & 0xFFFFu;
        if (Lo >= 0xDC00u && Lo <= 0xDFFFu) {
          uint32_t CP = 0x10000u + ((U - 0xD800u) << 10) + (Lo - 0xDC00u);
          AppendCodepoint(CP);
          ++i;
          continue;
        }
      }
      AppendCodepoint(U);
    }
  } else {
    for (unsigned i = 0; i < Length; ++i)
      AppendCodepoint(SL->getCodeUnit(i));
  }
}

StringLiteral *neverc::foldNeverCStringWideLiteralToUtf8(Sema &S,
                                                         StringLiteral *SL) {
  if (!SL)
    return nullptr;
  StringLiteralKind Kind = SL->getKind();
  if (Kind == StringLiteralKind::Ordinary || Kind == StringLiteralKind::UTF8)
    return SL;

  unsigned WidthInBits = 0;
  switch (Kind) {
  case StringLiteralKind::Wide:
    WidthInBits = S.Context.getTargetInfo().getWCharWidth();
    break;
  case StringLiteralKind::UTF16:
    WidthInBits = 16;
    break;
  case StringLiteralKind::UTF32:
    WidthInBits = 32;
    break;
  default:
    return nullptr;
  }
  if (WidthInBits != 16 && WidthInBits != 32)
    return nullptr;

  llvm::SmallVector<char, 64> Bytes;
  encodeNeverCStringWideCodeUnitsToUtf8(SL, WidthInBits, Bytes);

  llvm::SmallVector<SourceLocation, 1> Locs;
  Locs.push_back(SL->getBeginLoc());

  QualType StrTy = S.Context.getStringLiteralArrayType(
      S.Context.CharTy, static_cast<unsigned>(Bytes.size()));
  return StringLiteral::Create(
      S.Context, llvm::StringRef(Bytes.data(), Bytes.size()),
      StringLiteralKind::Ordinary, StrTy, Locs.data(), Locs.size());
}

ExprResult neverc::buildNeverCStringRuntimeCall(Sema &S, Scope *Sc,
                                                SourceLocation Loc,
                                                llvm::StringRef FunctionName,
                                                MultiExprArg Args,
                                                SourceLocation EndLoc) {
  FunctionDecl *FD =
      S.lookupNeverCStringFunctionDecl(FunctionName, /*Sc=*/nullptr, Loc);
  if (!FD)
    return ExprError();
  ExprResult DeclRef = S.MakeDeclRefExpr(FD, FD->getType(), VK_LValue, Loc);
  if (DeclRef.isInvalid())
    return ExprError();
  return S.FormCallExpr(Sc, DeclRef.get(), Loc, Args, EndLoc);
}
