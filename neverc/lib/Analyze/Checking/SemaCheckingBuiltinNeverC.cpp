#include "Checking/SemaCheckingUtils.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Builtin/XorStrCipher.h"
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

  const uint64_t Nonce = ++Counter;
  std::seed_seq KeySeed{
      static_cast<unsigned>(BaseKey), static_cast<unsigned>(BaseKey >> 32),
      static_cast<unsigned>(Nonce), static_cast<unsigned>(Nonce >> 32)};
  std::mt19937_64 KeyGenerator(KeySeed);
  uint64_t Key =
      (KeyGenerator() & llvm::maskTrailingOnes<uint64_t>(SizeBits)) | 1U;

  llvm::SmallVector<char, 256> EncBytes(Len);
  xorstr::CipherSchedule Schedule = xorstr::makeSchedule(Key, Len, SizeBits);
  uint64_t State = Schedule.InitialState;
  for (unsigned i = 0; i < Len; ++i) {
    State = xorstr::advanceState(State, i, Schedule, SizeBits);
    EncBytes[i] =
        static_cast<char>(static_cast<unsigned char>(Bytes[i]) ^
                          xorstr::streamByte(State, Schedule, SizeBits));
  }

  uint64_t LengthToken = xorstr::sealLength(Len, Key, SizeBits);

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
      S.Context, llvm::APInt(SizeBits, LengthToken), SizeTy, Loc);
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
