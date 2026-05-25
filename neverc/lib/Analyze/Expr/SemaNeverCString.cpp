//===- SemaNeverCString.cpp -- NeverC string Sema helpers (out-of-line) ---===//
//
// Out-of-line definitions for the NeverC builtin string helpers that
// were previously inline in SemaInternal.h.  Moving them here avoids
// compiling their bodies in every translation unit that includes
// SemaInternal.h (23 files) when only 3 files actually call them.
//

#include "neverc/Analyze/Lookup.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Builtin/BuiltinString.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Expr/Expr.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include <ctime>

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

ExprResult neverc::buildNeverCStringEncryptedLiteral(Sema &S, Scope *Sc,
                                                     Expr *Base,
                                                     StringLiteral *SL,
                                                     SourceLocation LParenLoc,
                                                     SourceLocation RParenLoc) {
  if (StringLiteral *Folded = foldNeverCStringWideLiteralToUtf8(S, SL))
    SL = Folded;

  llvm::StringRef Bytes = SL->getBytes();
  unsigned Len = Bytes.size();

  static uint64_t Counter = 0;
  uint64_t BaseKey = S.getLangOpts().StringEncryptKey;
  if (BaseKey == 0) {
    static uint64_t TimeKey =
        static_cast<uint64_t>(std::time(nullptr)) * 0x9E3779B97F4A7C15ULL;
    TimeKey |= 1;
    BaseKey = TimeKey;
  }
  QualType SizeTy = S.Context.getSizeType();
  unsigned SizeBits = S.Context.getTypeSize(SizeTy);
  unsigned KeyBytes = SizeBits / 8;

  uint64_t Key = BaseKey ^ (++Counter * 0x517CC1B727220A95ULL);
  Key &= llvm::maskTrailingOnes<uint64_t>(SizeBits);

  // Mirrors the default NEVERC_STRING_ENCRYPT_BYTE(byte, key, idx) macro.
  // If a user overrides the macro, they must ensure this C++ function and
  // the C macro produce identical output for every (byte, key, idx) triple.
  auto encryptByte = [KeyBytes](unsigned char byte, uint64_t key,
                                unsigned idx) -> char {
    auto k = static_cast<unsigned char>(key >> (8 * (idx % KeyBytes)));
    return static_cast<char>(byte ^ k);
  };

  llvm::SmallVector<char, 256> EncBytes(Len);
  for (unsigned i = 0; i < Len; ++i)
    EncBytes[i] = encryptByte(static_cast<unsigned char>(Bytes[i]), Key, i);

  llvm::SmallVector<SourceLocation, 1> Locs;
  Locs.push_back(SL->getBeginLoc());
  QualType EncStrTy = S.Context.getStringLiteralArrayType(
      S.Context.CharTy, Len);
  StringLiteral *EncSL = StringLiteral::Create(
      S.Context, llvm::StringRef(EncBytes.data(), Len),
      StringLiteralKind::Ordinary, EncStrTy, Locs.data(), Locs.size());

  IntegerLiteral *LenLit = IntegerLiteral::Create(
      S.Context, llvm::APInt(SizeBits, Len), SizeTy, LParenLoc);
  IntegerLiteral *KeyLit = IntegerLiteral::Create(
      S.Context, llvm::APInt(SizeBits, Key), SizeTy, LParenLoc);

  Expr *Args[] = {EncSL, LenLit, KeyLit};
  return buildNeverCStringRuntimeCall(S, Sc, LParenLoc,
                                      BuiltinStringNames::DecryptLiteralFunctionName,
                                      Args, RParenLoc);
}

const CallExpr *neverc::getDecryptLiteralCall(const Expr *E) {
  const auto *CE = dyn_cast<CallExpr>(E->IgnoreParenImpCasts());
  if (!CE)
    return nullptr;
  const auto *Callee = CE->getDirectCallee();
  if (!Callee)
    return nullptr;
  if (Callee->getName() == BuiltinStringNames::DecryptLiteralFunctionName)
    return CE;
  return nullptr;
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
