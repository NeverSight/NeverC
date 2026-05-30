#include "BridgeCastHelpers.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"

using namespace llvm;

namespace neverc {
namespace plugin {


// ===----------------------------------------------------------------------===
//  IRBuilder
// ===----------------------------------------------------------------------===

static NevercBuilderRef bridgeBuilderCreate(NevercContextRef C) {
  if (LLVM_UNLIKELY(!C))
    return nullptr;
  return wrapB(new IRBuilder<>(*unwrapCtx(C)));
}

static void bridgeBuilderDispose(NevercBuilderRef B) {
  if (B)
    delete unwrapB(B);
}

static void bridgeBuilderSetInsertPoint(NevercBuilderRef B,
                                        NevercBasicBlockRef BB) {
  if (LLVM_UNLIKELY(!B || !BB))
    return;
  unwrapB(B)->SetInsertPoint(unwrapBB(BB));
}

static void bridgeBuilderSetInsertPointBefore(NevercBuilderRef B,
                                              NevercValueRef Inst) {
  if (LLVM_UNLIKELY(!B || !Inst))
    return;
  auto *I = dyn_cast<Instruction>(unwrapV(Inst));
  if (LLVM_UNLIKELY(!I || !I->getParent()))
    return;
  unwrapB(B)->SetInsertPoint(I->getIterator());
}

static NevercValueRef bridgeBuildAdd(NevercBuilderRef B, NevercValueRef LHS,
                                     NevercValueRef RHS, const char *Name) {
  if (LLVM_UNLIKELY(!B || !LHS || !RHS))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateAdd(unwrapV(LHS), unwrapV(RHS), nameStr(Name)));
}

static NevercValueRef bridgeBuildSub(NevercBuilderRef B, NevercValueRef LHS,
                                     NevercValueRef RHS, const char *Name) {
  if (LLVM_UNLIKELY(!B || !LHS || !RHS))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateSub(unwrapV(LHS), unwrapV(RHS), nameStr(Name)));
}

static NevercValueRef bridgeBuildMul(NevercBuilderRef B, NevercValueRef LHS,
                                     NevercValueRef RHS, const char *Name) {
  if (LLVM_UNLIKELY(!B || !LHS || !RHS))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateMul(unwrapV(LHS), unwrapV(RHS), nameStr(Name)));
}

static NevercValueRef bridgeBuildUDiv(NevercBuilderRef B, NevercValueRef LHS,
                                      NevercValueRef RHS, const char *Name) {
  if (LLVM_UNLIKELY(!B || !LHS || !RHS))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateUDiv(unwrapV(LHS), unwrapV(RHS), nameStr(Name)));
}

static NevercValueRef bridgeBuildSDiv(NevercBuilderRef B, NevercValueRef LHS,
                                      NevercValueRef RHS, const char *Name) {
  if (LLVM_UNLIKELY(!B || !LHS || !RHS))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateSDiv(unwrapV(LHS), unwrapV(RHS), nameStr(Name)));
}

static NevercValueRef bridgeBuildAnd(NevercBuilderRef B, NevercValueRef LHS,
                                     NevercValueRef RHS, const char *Name) {
  if (LLVM_UNLIKELY(!B || !LHS || !RHS))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateAnd(unwrapV(LHS), unwrapV(RHS), nameStr(Name)));
}

static NevercValueRef bridgeBuildOr(NevercBuilderRef B, NevercValueRef LHS,
                                    NevercValueRef RHS, const char *Name) {
  if (LLVM_UNLIKELY(!B || !LHS || !RHS))
    return nullptr;
  return wrapV(unwrapB(B)->CreateOr(unwrapV(LHS), unwrapV(RHS), nameStr(Name)));
}

static NevercValueRef bridgeBuildXor(NevercBuilderRef B, NevercValueRef LHS,
                                     NevercValueRef RHS, const char *Name) {
  if (LLVM_UNLIKELY(!B || !LHS || !RHS))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateXor(unwrapV(LHS), unwrapV(RHS), nameStr(Name)));
}

static NevercValueRef bridgeBuildShl(NevercBuilderRef B, NevercValueRef LHS,
                                     NevercValueRef RHS, const char *Name) {
  if (LLVM_UNLIKELY(!B || !LHS || !RHS))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateShl(unwrapV(LHS), unwrapV(RHS), nameStr(Name)));
}

static NevercValueRef bridgeBuildLShr(NevercBuilderRef B, NevercValueRef LHS,
                                      NevercValueRef RHS, const char *Name) {
  if (LLVM_UNLIKELY(!B || !LHS || !RHS))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateLShr(unwrapV(LHS), unwrapV(RHS), nameStr(Name)));
}

static NevercValueRef bridgeBuildAShr(NevercBuilderRef B, NevercValueRef LHS,
                                      NevercValueRef RHS, const char *Name) {
  if (LLVM_UNLIKELY(!B || !LHS || !RHS))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateAShr(unwrapV(LHS), unwrapV(RHS), nameStr(Name)));
}

static NevercValueRef bridgeBuildICmp(NevercBuilderRef B, unsigned Pred,
                                      NevercValueRef LHS, NevercValueRef RHS,
                                      const char *Name) {
  if (LLVM_UNLIKELY(!B || !LHS || !RHS))
    return nullptr;
  return wrapV(unwrapB(B)->CreateICmp(
      static_cast<CmpInst::Predicate>(Pred), unwrapV(LHS), unwrapV(RHS),
      nameStr(Name)));
}

static NevercValueRef bridgeBuildFCmp(NevercBuilderRef B, unsigned Pred,
                                      NevercValueRef LHS, NevercValueRef RHS,
                                      const char *Name) {
  if (LLVM_UNLIKELY(!B || !LHS || !RHS))
    return nullptr;
  return wrapV(unwrapB(B)->CreateFCmp(
      static_cast<CmpInst::Predicate>(Pred), unwrapV(LHS), unwrapV(RHS),
      nameStr(Name)));
}

static NevercValueRef bridgeBuildBr(NevercBuilderRef B,
                                    NevercBasicBlockRef Dest) {
  if (LLVM_UNLIKELY(!B || !Dest))
    return nullptr;
  return wrapV(unwrapB(B)->CreateBr(unwrapBB(Dest)));
}

static NevercValueRef bridgeBuildCondBr(NevercBuilderRef B,
                                        NevercValueRef Cond,
                                        NevercBasicBlockRef Then,
                                        NevercBasicBlockRef Else) {
  if (LLVM_UNLIKELY(!B || !Cond || !Then || !Else))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateCondBr(unwrapV(Cond), unwrapBB(Then), unwrapBB(Else)));
}

static NevercValueRef bridgeBuildRet(NevercBuilderRef B, NevercValueRef V) {
  if (LLVM_UNLIKELY(!B || !V))
    return nullptr;
  return wrapV(unwrapB(B)->CreateRet(unwrapV(V)));
}

static NevercValueRef bridgeBuildRetVoid(NevercBuilderRef B) {
  if (LLVM_UNLIKELY(!B))
    return nullptr;
  return wrapV(unwrapB(B)->CreateRetVoid());
}

static NevercValueRef bridgeBuildCall(NevercBuilderRef B, NevercTypeRef FnTy,
                                      NevercValueRef Fn, NevercValueRef *Args,
                                      unsigned ArgCount, const char *Name) {
  if (LLVM_UNLIKELY(!B || !FnTy || !Fn || (ArgCount > 0 && !Args)))
    return nullptr;
  auto *FT = dyn_cast<FunctionType>(unwrapTy(FnTy));
  if (LLVM_UNLIKELY(!FT))
    return nullptr;
  SmallVector<Value *, 8> ArgVec;
  for (unsigned I = 0; I < ArgCount; ++I) {
    if (LLVM_UNLIKELY(!Args[I]))
      return nullptr;
    ArgVec.push_back(unwrapV(Args[I]));
  }
  return wrapV(unwrapB(B)->CreateCall(FT, unwrapV(Fn), ArgVec, nameStr(Name)));
}

static NevercValueRef bridgeBuildGEP(NevercBuilderRef B, NevercTypeRef Ty,
                                     NevercValueRef Ptr,
                                     NevercValueRef *Indices,
                                     unsigned NumIndices, const char *Name) {
  if (LLVM_UNLIKELY(!B || !Ty || !Ptr || (NumIndices > 0 && !Indices)))
    return nullptr;
  SmallVector<Value *, 4> Idxs;
  for (unsigned I = 0; I < NumIndices; ++I) {
    if (LLVM_UNLIKELY(!Indices[I]))
      return nullptr;
    Idxs.push_back(unwrapV(Indices[I]));
  }
  return wrapV(
      unwrapB(B)->CreateGEP(unwrapTy(Ty), unwrapV(Ptr), Idxs, nameStr(Name)));
}

static NevercValueRef bridgeBuildLoad(NevercBuilderRef B, NevercTypeRef Ty,
                                      NevercValueRef Ptr, const char *Name) {
  if (LLVM_UNLIKELY(!B || !Ty || !Ptr))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateLoad(unwrapTy(Ty), unwrapV(Ptr), nameStr(Name)));
}

static NevercValueRef bridgeBuildStore(NevercBuilderRef B, NevercValueRef Val,
                                       NevercValueRef Ptr) {
  if (LLVM_UNLIKELY(!B || !Val || !Ptr))
    return nullptr;
  return wrapV(unwrapB(B)->CreateStore(unwrapV(Val), unwrapV(Ptr)));
}

static NevercValueRef bridgeBuildAlloca(NevercBuilderRef B, NevercTypeRef Ty,
                                        const char *Name) {
  if (LLVM_UNLIKELY(!B || !Ty))
    return nullptr;
  return wrapV(unwrapB(B)->CreateAlloca(unwrapTy(Ty), nullptr, nameStr(Name)));
}

static NevercValueRef bridgeBuildBitCast(NevercBuilderRef B, NevercValueRef V,
                                         NevercTypeRef DestTy,
                                         const char *Name) {
  if (LLVM_UNLIKELY(!B || !V || !DestTy))
    return nullptr;
  return wrapV(unwrapB(B)->CreateBitCast(unwrapV(V), unwrapTy(DestTy),
                                        nameStr(Name)));
}

static NevercValueRef bridgeBuildIntToPtr(NevercBuilderRef B, NevercValueRef V,
                                          NevercTypeRef DestTy,
                                          const char *Name) {
  if (LLVM_UNLIKELY(!B || !V || !DestTy))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateIntToPtr(unwrapV(V), unwrapTy(DestTy), nameStr(Name)));
}

static NevercValueRef bridgeBuildPtrToInt(NevercBuilderRef B, NevercValueRef V,
                                          NevercTypeRef DestTy,
                                          const char *Name) {
  if (LLVM_UNLIKELY(!B || !V || !DestTy))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreatePtrToInt(unwrapV(V), unwrapTy(DestTy), nameStr(Name)));
}

static NevercValueRef bridgeBuildZExt(NevercBuilderRef B, NevercValueRef V,
                                      NevercTypeRef DestTy, const char *Name) {
  if (LLVM_UNLIKELY(!B || !V || !DestTy))
    return nullptr;
  return wrapV(unwrapB(B)->CreateZExt(unwrapV(V), unwrapTy(DestTy),
                                     nameStr(Name)));
}

static NevercValueRef bridgeBuildSExt(NevercBuilderRef B, NevercValueRef V,
                                      NevercTypeRef DestTy, const char *Name) {
  if (LLVM_UNLIKELY(!B || !V || !DestTy))
    return nullptr;
  return wrapV(unwrapB(B)->CreateSExt(unwrapV(V), unwrapTy(DestTy),
                                     nameStr(Name)));
}

static NevercValueRef bridgeBuildTrunc(NevercBuilderRef B, NevercValueRef V,
                                       NevercTypeRef DestTy,
                                       const char *Name) {
  if (LLVM_UNLIKELY(!B || !V || !DestTy))
    return nullptr;
  return wrapV(unwrapB(B)->CreateTrunc(unwrapV(V), unwrapTy(DestTy),
                                      nameStr(Name)));
}

static NevercValueRef bridgeBuildSelect(NevercBuilderRef B, NevercValueRef Cond,
                                        NevercValueRef Then,
                                        NevercValueRef Else,
                                        const char *Name) {
  if (LLVM_UNLIKELY(!B || !Cond || !Then || !Else))
    return nullptr;
  return wrapV(unwrapB(B)->CreateSelect(
      unwrapV(Cond), unwrapV(Then), unwrapV(Else), nameStr(Name)));
}

static NevercValueRef bridgeBuildPhi(NevercBuilderRef B, NevercTypeRef Ty,
                                     const char *Name) {
  if (LLVM_UNLIKELY(!B || !Ty))
    return nullptr;
  return wrapV(unwrapB(B)->CreatePHI(unwrapTy(Ty), 2, nameStr(Name)));
}

static void bridgePhiAddIncoming(NevercValueRef Phi, NevercValueRef *Values,
                                 NevercBasicBlockRef *Blocks, unsigned Count) {
  if (LLVM_UNLIKELY(!Phi || !Values || !Blocks || Count == 0))
    return;
  auto *PN = dyn_cast<PHINode>(unwrapV(Phi));
  if (LLVM_UNLIKELY(!PN))
    return;
  for (unsigned I = 0; I < Count; ++I) {
    if (LLVM_UNLIKELY(!Values[I] || !Blocks[I]))
      continue;
    PN->addIncoming(unwrapV(Values[I]), unwrapBB(Blocks[I]));
  }
}


// ===----------------------------------------------------------------------===
//  Unary builder ops
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeBuildNeg(NevercBuilderRef B, NevercValueRef V,
                                     const char *Name) {
  if (LLVM_UNLIKELY(!B || !V))
    return nullptr;
  return wrapV(unwrapB(B)->CreateNeg(unwrapV(V), nameStr(Name)));
}

static NevercValueRef bridgeBuildNot(NevercBuilderRef B, NevercValueRef V,
                                     const char *Name) {
  if (LLVM_UNLIKELY(!B || !V))
    return nullptr;
  return wrapV(unwrapB(B)->CreateNot(unwrapV(V), nameStr(Name)));
}

static NevercValueRef bridgeBuildFNeg(NevercBuilderRef B, NevercValueRef V,
                                      const char *Name) {
  if (LLVM_UNLIKELY(!B || !V))
    return nullptr;
  return wrapV(unwrapB(B)->CreateFNeg(unwrapV(V), nameStr(Name)));
}

static NevercValueRef bridgeBuildUnreachable(NevercBuilderRef B) {
  if (LLVM_UNLIKELY(!B))
    return nullptr;
  return wrapV(unwrapB(B)->CreateUnreachable());
}

// ===----------------------------------------------------------------------===
//  Switch instruction
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeBuildSwitch(NevercBuilderRef B, NevercValueRef V,
                                        NevercBasicBlockRef DefaultBB,
                                        unsigned NumCases) {
  if (LLVM_UNLIKELY(!B || !V || !DefaultBB))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateSwitch(unwrapV(V), unwrapBB(DefaultBB), NumCases));
}

static void bridgeSwitchAddCase(NevercValueRef Switch, NevercValueRef OnVal,
                                NevercBasicBlockRef Dest) {
  if (LLVM_UNLIKELY(!Switch || !OnVal || !Dest))
    return;
  auto *SI = dyn_cast<SwitchInst>(unwrapV(Switch));
  if (LLVM_UNLIKELY(!SI))
    return;
  auto *CI = dyn_cast<ConstantInt>(unwrapV(OnVal));
  if (LLVM_UNLIKELY(!CI)) {
    bridgeDiagWarning("SwitchAddCase: case value is not ConstantInt; ignoring");
    return;
  }
  SI->addCase(CI, unwrapBB(Dest));
}


// ===----------------------------------------------------------------------===
//  Remainder ops
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeBuildURem(NevercBuilderRef B, NevercValueRef LHS,
                                      NevercValueRef RHS, const char *Name) {
  if (LLVM_UNLIKELY(!B || !LHS || !RHS))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateURem(unwrapV(LHS), unwrapV(RHS), nameStr(Name)));
}

static NevercValueRef bridgeBuildSRem(NevercBuilderRef B, NevercValueRef LHS,
                                      NevercValueRef RHS, const char *Name) {
  if (LLVM_UNLIKELY(!B || !LHS || !RHS))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateSRem(unwrapV(LHS), unwrapV(RHS), nameStr(Name)));
}

// ===----------------------------------------------------------------------===
//  FP cast ops
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeBuildFPToSI(NevercBuilderRef B, NevercValueRef V,
                                        NevercTypeRef DestTy,
                                        const char *Name) {
  if (LLVM_UNLIKELY(!B || !V || !DestTy))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateFPToSI(unwrapV(V), unwrapTy(DestTy), nameStr(Name)));
}

static NevercValueRef bridgeBuildSIToFP(NevercBuilderRef B, NevercValueRef V,
                                        NevercTypeRef DestTy,
                                        const char *Name) {
  if (LLVM_UNLIKELY(!B || !V || !DestTy))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateSIToFP(unwrapV(V), unwrapTy(DestTy), nameStr(Name)));
}

static NevercValueRef bridgeBuildFPToUI(NevercBuilderRef B, NevercValueRef V,
                                        NevercTypeRef DestTy,
                                        const char *Name) {
  if (LLVM_UNLIKELY(!B || !V || !DestTy))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateFPToUI(unwrapV(V), unwrapTy(DestTy), nameStr(Name)));
}

static NevercValueRef bridgeBuildUIToFP(NevercBuilderRef B, NevercValueRef V,
                                        NevercTypeRef DestTy,
                                        const char *Name) {
  if (LLVM_UNLIKELY(!B || !V || !DestTy))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateUIToFP(unwrapV(V), unwrapTy(DestTy), nameStr(Name)));
}


// ===----------------------------------------------------------------------===
//  Floating-point arithmetic
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeBuildFAdd(NevercBuilderRef B, NevercValueRef LHS,
                                      NevercValueRef RHS, const char *Name) {
  if (LLVM_UNLIKELY(!B || !LHS || !RHS))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateFAdd(unwrapV(LHS), unwrapV(RHS), nameStr(Name)));
}

static NevercValueRef bridgeBuildFSub(NevercBuilderRef B, NevercValueRef LHS,
                                      NevercValueRef RHS, const char *Name) {
  if (LLVM_UNLIKELY(!B || !LHS || !RHS))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateFSub(unwrapV(LHS), unwrapV(RHS), nameStr(Name)));
}

static NevercValueRef bridgeBuildFMul(NevercBuilderRef B, NevercValueRef LHS,
                                      NevercValueRef RHS, const char *Name) {
  if (LLVM_UNLIKELY(!B || !LHS || !RHS))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateFMul(unwrapV(LHS), unwrapV(RHS), nameStr(Name)));
}

static NevercValueRef bridgeBuildFDiv(NevercBuilderRef B, NevercValueRef LHS,
                                      NevercValueRef RHS, const char *Name) {
  if (LLVM_UNLIKELY(!B || !LHS || !RHS))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateFDiv(unwrapV(LHS), unwrapV(RHS), nameStr(Name)));
}

static NevercValueRef bridgeBuildFRem(NevercBuilderRef B, NevercValueRef LHS,
                                      NevercValueRef RHS, const char *Name) {
  if (LLVM_UNLIKELY(!B || !LHS || !RHS))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateFRem(unwrapV(LHS), unwrapV(RHS), nameStr(Name)));
}

// ===----------------------------------------------------------------------===
//  Aggregate value ops
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeBuildExtractValue(NevercBuilderRef B,
                                              NevercValueRef Agg, unsigned Idx,
                                              const char *Name) {
  if (LLVM_UNLIKELY(!B || !Agg))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateExtractValue(unwrapV(Agg), Idx, nameStr(Name)));
}

static NevercValueRef bridgeBuildInsertValue(NevercBuilderRef B,
                                             NevercValueRef Agg,
                                             NevercValueRef Val, unsigned Idx,
                                             const char *Name) {
  if (LLVM_UNLIKELY(!B || !Agg || !Val))
    return nullptr;
  return wrapV(unwrapB(B)->CreateInsertValue(unwrapV(Agg), unwrapV(Val), Idx,
                                             nameStr(Name)));
}


// ===----------------------------------------------------------------------===
//  Global string pointer convenience
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeBuildGlobalStringPtr(NevercBuilderRef B,
                                                 const char *Str,
                                                 const char *Name) {
  if (LLVM_UNLIKELY(!B || !Str))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateGlobalStringPtr(Str, nameStr(Name)));
}


// ===----------------------------------------------------------------------===
//  Inbounds GEP
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeBuildInBoundsGEP(NevercBuilderRef B,
                                             NevercTypeRef Ty,
                                             NevercValueRef Ptr,
                                             NevercValueRef *Indices,
                                             unsigned NumIndices,
                                             const char *Name) {
  if (LLVM_UNLIKELY(!B || !Ty || !Ptr || (NumIndices > 0 && !Indices)))
    return nullptr;
  SmallVector<Value *, 4> Idxs;
  for (unsigned I = 0; I < NumIndices; ++I) {
    if (LLVM_UNLIKELY(!Indices[I]))
      return nullptr;
    Idxs.push_back(unwrapV(Indices[I]));
  }
  return wrapV(unwrapB(B)->CreateInBoundsGEP(unwrapTy(Ty), unwrapV(Ptr), Idxs,
                                             nameStr(Name)));
}


// ===----------------------------------------------------------------------===
//  IRBuilder insert-point query
// ===----------------------------------------------------------------------===

static NevercBasicBlockRef bridgeBuilderGetInsertBlock(NevercBuilderRef B) {
  if (LLVM_UNLIKELY(!B))
    return nullptr;
  auto *BB = unwrapB(B)->GetInsertBlock();
  return BB ? wrapBB(BB) : nullptr;
}


// ===----------------------------------------------------------------------===
//  FP precision cast ops
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeBuildFPExt(NevercBuilderRef B, NevercValueRef V,
                                       NevercTypeRef DestTy,
                                       const char *Name) {
  if (LLVM_UNLIKELY(!B || !V || !DestTy))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateFPExt(unwrapV(V), unwrapTy(DestTy), nameStr(Name)));
}

static NevercValueRef bridgeBuildFPTrunc(NevercBuilderRef B, NevercValueRef V,
                                         NevercTypeRef DestTy,
                                         const char *Name) {
  if (LLVM_UNLIKELY(!B || !V || !DestTy))
    return nullptr;
  return wrapV(unwrapB(B)->CreateFPTrunc(unwrapV(V), unwrapTy(DestTy),
                                         nameStr(Name)));
}


// ===----------------------------------------------------------------------===
//  Builder: insert before terminator
// ===----------------------------------------------------------------------===

static void bridgeBuilderSetInsertPointBeforeTerminator(
    NevercBuilderRef B, NevercBasicBlockRef BB) {
  if (LLVM_UNLIKELY(!B || !BB))
    return;
  auto *Block = unwrapBB(BB);
  auto *Term = Block->getTerminator();
  if (Term)
    unwrapB(B)->SetInsertPoint(Term->getIterator());
  else
    unwrapB(B)->SetInsertPoint(Block);
}


// ===----------------------------------------------------------------------===
//  Aligned Load/Store builders
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeBuildAlignedLoad(NevercBuilderRef B,
                                             NevercTypeRef Ty,
                                             NevercValueRef Ptr,
                                             unsigned AlignVal, int IsVolatile,
                                             const char *Name) {
  if (LLVM_UNLIKELY(!B || !Ty || !Ptr))
    return nullptr;
  return wrapV(unwrapB(B)->CreateAlignedLoad(
      unwrapTy(Ty), unwrapV(Ptr), MaybeAlign(AlignVal), IsVolatile != 0,
      nameStr(Name)));
}

static NevercValueRef bridgeBuildAlignedStore(NevercBuilderRef B,
                                              NevercValueRef Val,
                                              NevercValueRef Ptr,
                                              unsigned AlignVal,
                                              int IsVolatile) {
  if (LLVM_UNLIKELY(!B || !Val || !Ptr))
    return nullptr;
  return wrapV(unwrapB(B)->CreateAlignedStore(
      unwrapV(Val), unwrapV(Ptr), MaybeAlign(AlignVal), IsVolatile != 0));
}


// ===----------------------------------------------------------------------===
//  Memory intrinsic builders (memcpy, memset, memmove)
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeBuildMemCpy(NevercBuilderRef B,
                                         NevercValueRef Dst, unsigned DstAlign,
                                         NevercValueRef Src, unsigned SrcAlign,
                                         NevercValueRef Len, int IsVolatile) {
  if (LLVM_UNLIKELY(!B || !Dst || !Src || !Len))
    return nullptr;
  auto *Builder = unwrapB(B);
  return wrapV(Builder->CreateMemCpy(
      unwrapV(Dst), MaybeAlign(DstAlign),
      unwrapV(Src), MaybeAlign(SrcAlign),
      unwrapV(Len), IsVolatile != 0));
}

static NevercValueRef bridgeBuildMemSet(NevercBuilderRef B,
                                         NevercValueRef Dst, unsigned DstAlign,
                                         NevercValueRef Val,
                                         NevercValueRef Len, int IsVolatile) {
  if (LLVM_UNLIKELY(!B || !Dst || !Val || !Len))
    return nullptr;
  auto *Builder = unwrapB(B);
  return wrapV(Builder->CreateMemSet(
      unwrapV(Dst), unwrapV(Val),
      unwrapV(Len), MaybeAlign(DstAlign), IsVolatile != 0));
}

static NevercValueRef bridgeBuildMemMove(NevercBuilderRef B,
                                          NevercValueRef Dst, unsigned DstAlign,
                                          NevercValueRef Src, unsigned SrcAlign,
                                          NevercValueRef Len, int IsVolatile) {
  if (LLVM_UNLIKELY(!B || !Dst || !Src || !Len))
    return nullptr;
  auto *Builder = unwrapB(B);
  return wrapV(Builder->CreateMemMove(
      unwrapV(Dst), MaybeAlign(DstAlign),
      unwrapV(Src), MaybeAlign(SrcAlign),
      unwrapV(Len), IsVolatile != 0));
}


// ===----------------------------------------------------------------------===
//  Vector element operations
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeBuildExtractElement(NevercBuilderRef B,
                                                NevercValueRef Vec,
                                                NevercValueRef Idx,
                                                const char *Name) {
  if (LLVM_UNLIKELY(!B || !Vec || !Idx))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateExtractElement(unwrapV(Vec), unwrapV(Idx),
                                       nameStr(Name)));
}

static NevercValueRef bridgeBuildInsertElement(NevercBuilderRef B,
                                               NevercValueRef Vec,
                                               NevercValueRef Val,
                                               NevercValueRef Idx,
                                               const char *Name) {
  if (LLVM_UNLIKELY(!B || !Vec || !Val || !Idx))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateInsertElement(unwrapV(Vec), unwrapV(Val),
                                      unwrapV(Idx), nameStr(Name)));
}

static NevercValueRef bridgeBuildShuffleVector(NevercBuilderRef B,
                                               NevercValueRef V1,
                                               NevercValueRef V2,
                                               int *MaskVals,
                                               unsigned MaskLen,
                                               const char *Name) {
  if (LLVM_UNLIKELY(!B || !V1 || !V2 || !MaskVals || MaskLen == 0))
    return nullptr;
  SmallVector<int, 16> Mask(MaskVals, MaskVals + MaskLen);
  return wrapV(
      unwrapB(B)->CreateShuffleVector(unwrapV(V1), unwrapV(V2), Mask,
                                      nameStr(Name)));
}

// ===----------------------------------------------------------------------===
//  Freeze instruction
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeBuildFreeze(NevercBuilderRef B, NevercValueRef V,
                                        const char *Name) {
  if (LLVM_UNLIKELY(!B || !V))
    return nullptr;
  return wrapV(unwrapB(B)->CreateFreeze(unwrapV(V), nameStr(Name)));
}

// ===----------------------------------------------------------------------===
//  AddrSpaceCast
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeBuildAddrSpaceCast(NevercBuilderRef B,
                                               NevercValueRef V,
                                               NevercTypeRef DestTy,
                                               const char *Name) {
  if (LLVM_UNLIKELY(!B || !V || !DestTy))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateAddrSpaceCast(unwrapV(V), unwrapTy(DestTy),
                                      nameStr(Name)));
}


static NevercValueRef bridgeBuildNSWNeg(NevercBuilderRef B, NevercValueRef V,
                                        const char *Name) {
  if (LLVM_UNLIKELY(!B || !V))
    return nullptr;
  return wrapV(unwrapB(B)->CreateNSWNeg(unwrapV(V), nameStr(Name)));
}

// ===----------------------------------------------------------------------===
//  Exception handling
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeBuildInvoke(NevercBuilderRef B, NevercTypeRef FnTy,
                                        NevercValueRef Fn,
                                        NevercValueRef *Args, unsigned ArgCount,
                                        NevercBasicBlockRef NormalDest,
                                        NevercBasicBlockRef UnwindDest,
                                        const char *Name) {
  if (LLVM_UNLIKELY(!B || !FnTy || !Fn || !NormalDest || !UnwindDest ||
                    (ArgCount > 0 && !Args)))
    return nullptr;
  auto *FTy = dyn_cast<FunctionType>(unwrapTy(FnTy));
  if (LLVM_UNLIKELY(!FTy))
    return nullptr;
  SmallVector<Value *, 8> ArgVec;
  for (unsigned I = 0; I < ArgCount; ++I) {
    if (LLVM_UNLIKELY(!Args[I]))
      return nullptr;
    ArgVec.push_back(unwrapV(Args[I]));
  }
  return wrapV(unwrapB(B)->CreateInvoke(FTy, unwrapV(Fn), unwrapBB(NormalDest),
                                        unwrapBB(UnwindDest), ArgVec,
                                        nameStr(Name)));
}

static NevercValueRef bridgeBuildLandingPad(NevercBuilderRef B,
                                            NevercTypeRef Ty,
                                            unsigned NumClauses,
                                            const char *Name) {
  if (LLVM_UNLIKELY(!B || !Ty))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateLandingPad(unwrapTy(Ty), NumClauses, nameStr(Name)));
}

static void bridgeLandingPadAddClause(NevercValueRef LPad,
                                      NevercValueRef ClauseVal) {
  if (LLVM_UNLIKELY(!LPad || !ClauseVal))
    return;
  auto *LP = dyn_cast<LandingPadInst>(unwrapV(LPad));
  // LandingPadInst::addClause() requires a Constant; use dyn_cast (not cast)
  // so a plugin passing a non-constant clause is rejected, not a host abort.
  auto *Clause = dyn_cast<Constant>(unwrapV(ClauseVal));
  if (LP && Clause)
    LP->addClause(Clause);
}

static void bridgeLandingPadSetCleanup(NevercValueRef LPad, int IsCleanup) {
  if (LLVM_UNLIKELY(!LPad))
    return;
  auto *LP = dyn_cast<LandingPadInst>(unwrapV(LPad));
  if (LP)
    LP->setCleanup(IsCleanup != 0);
}

static NevercValueRef bridgeBuildResume(NevercBuilderRef B,
                                        NevercValueRef Val) {
  if (LLVM_UNLIKELY(!B || !Val))
    return nullptr;
  return wrapV(unwrapB(B)->CreateResume(unwrapV(Val)));
}


// ===----------------------------------------------------------------------===
//  NSW/NUW arithmetic (macro-generated)
// ===----------------------------------------------------------------------===

#define BRIDGE_BINOP_FLAG(NAME, METHOD)                                        \
  static NevercValueRef bridge##NAME(NevercBuilderRef B, NevercValueRef LHS,   \
                                     NevercValueRef RHS, const char *Name) {   \
    if (LLVM_UNLIKELY(!B || !LHS || !RHS))                                     \
      return nullptr;                                                          \
    const char *Nm = nameStr(Name);                                            \
    return wrapV(unwrapB(B)->METHOD(unwrapV(LHS), unwrapV(RHS), Nm));          \
  }

BRIDGE_BINOP_FLAG(BuildNSWAdd, CreateNSWAdd)
BRIDGE_BINOP_FLAG(BuildNUWAdd, CreateNUWAdd)
BRIDGE_BINOP_FLAG(BuildNSWSub, CreateNSWSub)
BRIDGE_BINOP_FLAG(BuildNUWSub, CreateNUWSub)
BRIDGE_BINOP_FLAG(BuildNSWMul, CreateNSWMul)
BRIDGE_BINOP_FLAG(BuildNUWMul, CreateNUWMul)
BRIDGE_BINOP_FLAG(BuildExactSDiv, CreateExactSDiv)
BRIDGE_BINOP_FLAG(BuildExactUDiv, CreateExactUDiv)
#undef BRIDGE_BINOP_FLAG

void populateIRBuilderBridge(NevercHostAPI &API) {
  API.BuildAShr = bridgeBuildAShr;
  API.BuildAdd = bridgeBuildAdd;
  API.BuildAddrSpaceCast = bridgeBuildAddrSpaceCast;
  API.BuildAlignedLoad = bridgeBuildAlignedLoad;
  API.BuildAlignedStore = bridgeBuildAlignedStore;
  API.BuildAlloca = bridgeBuildAlloca;
  API.BuildAnd = bridgeBuildAnd;
  API.BuildBitCast = bridgeBuildBitCast;
  API.BuildBr = bridgeBuildBr;
  API.BuildCall = bridgeBuildCall;
  API.BuildCondBr = bridgeBuildCondBr;
  API.BuildExactSDiv = bridgeBuildExactSDiv;
  API.BuildExactUDiv = bridgeBuildExactUDiv;
  API.BuildExtractElement = bridgeBuildExtractElement;
  API.BuildExtractValue = bridgeBuildExtractValue;
  API.BuildFAdd = bridgeBuildFAdd;
  API.BuildFCmp = bridgeBuildFCmp;
  API.BuildFDiv = bridgeBuildFDiv;
  API.BuildFMul = bridgeBuildFMul;
  API.BuildFNeg = bridgeBuildFNeg;
  API.BuildFPExt = bridgeBuildFPExt;
  API.BuildFPToSI = bridgeBuildFPToSI;
  API.BuildFPToUI = bridgeBuildFPToUI;
  API.BuildFPTrunc = bridgeBuildFPTrunc;
  API.BuildFRem = bridgeBuildFRem;
  API.BuildFSub = bridgeBuildFSub;
  API.BuildFreeze = bridgeBuildFreeze;
  API.BuildGEP = bridgeBuildGEP;
  API.BuildGlobalStringPtr = bridgeBuildGlobalStringPtr;
  API.BuildICmp = bridgeBuildICmp;
  API.BuildInBoundsGEP = bridgeBuildInBoundsGEP;
  API.BuildInsertElement = bridgeBuildInsertElement;
  API.BuildInsertValue = bridgeBuildInsertValue;
  API.BuildIntToPtr = bridgeBuildIntToPtr;
  API.BuildInvoke = bridgeBuildInvoke;
  API.BuildLShr = bridgeBuildLShr;
  API.BuildLandingPad = bridgeBuildLandingPad;
  API.BuildLoad = bridgeBuildLoad;
  API.BuildMemCpy = bridgeBuildMemCpy;
  API.BuildMemMove = bridgeBuildMemMove;
  API.BuildMemSet = bridgeBuildMemSet;
  API.BuildMul = bridgeBuildMul;
  API.BuildNSWAdd = bridgeBuildNSWAdd;
  API.BuildNSWMul = bridgeBuildNSWMul;
  API.BuildNSWNeg = bridgeBuildNSWNeg;
  API.BuildNSWSub = bridgeBuildNSWSub;
  API.BuildNUWAdd = bridgeBuildNUWAdd;
  API.BuildNUWMul = bridgeBuildNUWMul;
  API.BuildNUWSub = bridgeBuildNUWSub;
  API.BuildNeg = bridgeBuildNeg;
  API.BuildNot = bridgeBuildNot;
  API.BuildOr = bridgeBuildOr;
  API.BuildPhi = bridgeBuildPhi;
  API.BuildPtrToInt = bridgeBuildPtrToInt;
  API.BuildResume = bridgeBuildResume;
  API.BuildRet = bridgeBuildRet;
  API.BuildRetVoid = bridgeBuildRetVoid;
  API.BuildSDiv = bridgeBuildSDiv;
  API.BuildSExt = bridgeBuildSExt;
  API.BuildSIToFP = bridgeBuildSIToFP;
  API.BuildSRem = bridgeBuildSRem;
  API.BuildSelect = bridgeBuildSelect;
  API.BuildShl = bridgeBuildShl;
  API.BuildShuffleVector = bridgeBuildShuffleVector;
  API.BuildStore = bridgeBuildStore;
  API.BuildSub = bridgeBuildSub;
  API.BuildSwitch = bridgeBuildSwitch;
  API.BuildTrunc = bridgeBuildTrunc;
  API.BuildUDiv = bridgeBuildUDiv;
  API.BuildUIToFP = bridgeBuildUIToFP;
  API.BuildURem = bridgeBuildURem;
  API.BuildUnreachable = bridgeBuildUnreachable;
  API.BuildXor = bridgeBuildXor;
  API.BuildZExt = bridgeBuildZExt;
  API.BuilderCreate = bridgeBuilderCreate;
  API.BuilderDispose = bridgeBuilderDispose;
  API.BuilderGetInsertBlock = bridgeBuilderGetInsertBlock;
  API.BuilderSetInsertPoint = bridgeBuilderSetInsertPoint;
  API.BuilderSetInsertPointBefore = bridgeBuilderSetInsertPointBefore;
  API.BuilderSetInsertPointBeforeTerminator =
      bridgeBuilderSetInsertPointBeforeTerminator;
  API.LandingPadAddClause = bridgeLandingPadAddClause;
  API.LandingPadSetCleanup = bridgeLandingPadSetCleanup;
  API.PhiAddIncoming = bridgePhiAddIncoming;
  API.SwitchAddCase = bridgeSwitchAddCase;
}

} // namespace plugin
} // namespace neverc
