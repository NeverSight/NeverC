#include "Checking/SemaCheckingUtils.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Builtin/XorStrNames.h"
#include "neverc/Foundation/Diagnostic/DiagnosticSema.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Transforms/StrHash/StrHashCompute.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/MathExtras.h"
#include <random>

using namespace neverc;
using namespace sema;

// ===----------------------------------------------------------------------===
// NeverC-specific builtin semantic checks (xorstr / strhash / random)
// ===----------------------------------------------------------------------===

ExprResult semaBuiltinNeverCXorstr(Sema &S, CallExpr *TheCall) {
  if (checkArgCount(S, TheCall, 1))
    return ExprError();

  Expr *Arg = TheCall->getArg(0)->IgnoreParenCasts();
  StringLiteral *SL = dyn_cast<StringLiteral>(Arg);
  if (!SL) {
    S.Diag(Arg->getBeginLoc(), diag::err_expr_not_string_literal)
        << Arg->getSourceRange();
    return ExprError();
  }

  if (SL->getKind() != StringLiteralKind::Ordinary &&
      SL->getKind() != StringLiteralKind::UTF8) {
    StringLiteral *Folded = foldNeverCStringWideLiteralToUtf8(S, SL);
    if (Folded)
      SL = Folded;
  }

  llvm::StringRef Bytes = SL->getBytes();
  unsigned Len = Bytes.size();

  static uint64_t Counter = 0;
  uint64_t BaseKey = S.getLangOpts().StringEncryptKey;
  if (BaseKey == 0) {
    static uint64_t SeedKey = [] {
      std::random_device rd;
      uint64_t h = (static_cast<uint64_t>(rd()) << 32) | rd();
      return h | 1;
    }();
    BaseKey = SeedKey;
  }
  QualType SizeTy = S.Context.getSizeType();
  unsigned SizeBits = S.Context.getTypeSize(SizeTy);
  unsigned KeyBytes = SizeBits / 8;

  uint64_t Key = BaseKey ^ (++Counter * 0x517CC1B727220A95ULL);
  Key &= llvm::maskTrailingOnes<uint64_t>(SizeBits);

  auto encryptByte = [KeyBytes](unsigned char byte, uint64_t key,
                                unsigned idx) -> char {
    auto k = static_cast<unsigned char>(key >> (8 * (idx % KeyBytes)));
    return static_cast<char>(byte ^ k);
  };

  llvm::SmallVector<char, 256> EncBytes(Len);
  for (unsigned i = 0; i < Len; ++i)
    EncBytes[i] = encryptByte(static_cast<unsigned char>(Bytes[i]), Key, i);

  SourceLocation Loc = TheCall->getBeginLoc();
  SourceLocation EndLoc = TheCall->getEndLoc();

  QualType EncStrTy =
      S.Context.getStringLiteralArrayType(S.Context.CharTy, Len);
  SmallVector<SourceLocation, 1> SLLocs;
  SLLocs.push_back(SL->getBeginLoc());
  StringLiteral *EncSL = StringLiteral::Create(
      S.Context, llvm::StringRef(EncBytes.data(), Len),
      StringLiteralKind::Ordinary, EncStrTy, SLLocs.data(), SLLocs.size());

  IntegerLiteral *LenLit = IntegerLiteral::Create(
      S.Context, llvm::APInt(SizeBits, Len), SizeTy, Loc);
  IntegerLiteral *KeyLit = IntegerLiteral::Create(
      S.Context, llvm::APInt(SizeBits, Key), SizeTy, Loc);

  FunctionDecl *FD = S.lookupNeverCStringFunctionDecl(
      neverc::XorStrNames::DecryptFunctionName, nullptr, Loc);
  if (!FD) {
    unsigned DiagID = S.Diags.getCustomDiagID(
        DiagnosticsEngine::Error,
        "'__builtin_neverc_xorstr' requires '#include <neverc/xorstr/xorstr.h>'");
    S.Diag(Loc, DiagID);
    return ExprError();
  }

  ExprResult DeclRef =
      S.MakeDeclRefExpr(FD, FD->getType(), VK_LValue, Loc);
  if (DeclRef.isInvalid())
    return ExprError();

  // Create a compound literal (char[Len+1]){0} for the output buffer.
  // The size is a compile-time constant → static alloca after inlining,
  // no stacksave/stackrestore.
  QualType BufArrayTy = S.Context.getConstantArrayType(
      S.Context.CharTy, llvm::APInt(32, Len + 1), /*SizeExpr=*/nullptr,
      ArraySizeModifier::Normal, /*IndexTypeQuals=*/0);
  TypeSourceInfo *BufTInfo =
      S.Context.getTrivialTypeSourceInfo(BufArrayTy, Loc);
  Expr *ZeroExpr = IntegerLiteral::Create(
      S.Context, llvm::APInt(S.Context.getCharWidth(), 0),
      S.Context.CharTy, Loc);
  InitListExpr *BufInit =
      new (S.Context) InitListExpr(S.Context, Loc, {ZeroExpr}, Loc);
  BufInit->setType(BufArrayTy);
  CompoundLiteralExpr *BufLit = new (S.Context) CompoundLiteralExpr(
      Loc, BufTInfo, BufArrayTy, VK_LValue, BufInit, /*fileScope=*/false);

  Expr *Args[] = {EncSL, LenLit, KeyLit, BufLit};
  return S.FormCallExpr(nullptr, DeclRef.get(), Loc, Args, EndLoc);
}

ExprResult semaBuiltinNeverCStrHash(Sema &S, CallExpr *TheCall) {
  if (checkArgCount(S, TheCall, 1))
    return ExprError();

  Expr *Arg = TheCall->getArg(0)->IgnoreParenCasts();
  StringLiteral *SL = dyn_cast<StringLiteral>(Arg);
  if (!SL) {
    S.Diag(Arg->getBeginLoc(), diag::err_expr_not_string_literal)
        << Arg->getSourceRange();
    return ExprError();
  }

  if (SL->getKind() != StringLiteralKind::Ordinary &&
      SL->getKind() != StringLiteralKind::UTF8) {
    StringLiteral *Folded = foldNeverCStringWideLiteralToUtf8(S, SL);
    if (Folded)
      SL = Folded;
  }

  llvm::StringRef Bytes = SL->getBytes();
  unsigned Algo = S.getLangOpts().StrHashAlgo;
  uint64_t Hash = neverc::strhash::computeStrHash(Bytes, Algo);

  SourceLocation Loc = TheCall->getBeginLoc();
  QualType Ty = S.Context.UnsignedLongLongTy;
  unsigned Bits = S.Context.getTypeSize(Ty);
  IntegerLiteral *Result = IntegerLiteral::Create(
      S.Context, llvm::APInt(Bits, Hash), Ty, Loc);

  TheCall->setType(Ty);
  return Result;
}

ExprResult semaBuiltinNeverCRandomU64(Sema &S, CallExpr *TheCall) {
  if (checkArgCount(S, TheCall, 0))
    return ExprError();

  static uint64_t Counter = 0;
  uint64_t BaseKey = S.getLangOpts().StringEncryptKey;
  if (BaseKey == 0) {
    static uint64_t Seed = [] {
      std::random_device rd;
      uint64_t h = (static_cast<uint64_t>(rd()) << 32) | rd();
      return h | 1;
    }();
    BaseKey = Seed;
  }
  uint64_t Value = BaseKey ^ (++Counter * 0x6C62272E07BB0142ULL);

  SourceLocation Loc = TheCall->getBeginLoc();
  QualType Ty = S.Context.UnsignedLongLongTy;
  unsigned Bits = S.Context.getTypeSize(Ty);
  return IntegerLiteral::Create(S.Context, llvm::APInt(Bits, Value), Ty, Loc);
}
