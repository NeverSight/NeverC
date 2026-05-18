#include "ABI/ABIInfo.h"
#include "ABI/TargetInfo.h"
#include "Builtin/BuiltinEmitterUtils.h"
#include "Core/ConstantEmitter.h"
#include "Core/FunctionEmitter.h"
#include "Core/ModuleEmitter.h"
#include "Core/RecordLayoutInfo.h"
#include "Decl/PatternInit.h"
#include "neverc/Emit/ABI/ABIFunctionInfo.h"
#include "neverc/Foundation/Builtin/TargetBuiltins.h"
#include "neverc/Tree/Core/Attr.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Format/OSLog.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/FloatingPointMode.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsAArch64.h"
#include "llvm/IR/IntrinsicsX86.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/MatrixBuilder.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/TargetParser/AArch64TargetParser.h"
#include "llvm/TargetParser/X86TargetParser.h"
#include <optional>

using namespace neverc;
using namespace Emit;
using namespace llvm;

// === Split from BuiltinEmitter.cpp ===
// This translation unit hosts all AArch64 / NEON / SVE / SME builtin lowering
// previously living in BuiltinEmitter.cpp (lines 4620..10891).
// Public FunctionEmitter member entry points exposed elsewhere:
//   - genAArch64BuiltinExpr / genAArch64SVEBuiltinExpr /
//   genAArch64SMEBuiltinExpr
//   - genAArch64CompareBuiltinExpr / Emit* NEON helpers used by the above
// File-local statics (NEON intrinsic tables and *ProvenSorted flags) are kept
// here because they are exclusively consumed by AArch64 lowering paths.

namespace {
llvm::FixedVectorType *GetNeonType(FunctionEmitter *FE, NeonTypeFlags TypeFlags,
                                   bool HasLegalHalfType = true,
                                   bool V1Ty = false,
                                   bool AllowBFloatArgsAndRet = true) {
  int IsQuad = TypeFlags.isQuad();
  switch (TypeFlags.getEltType()) {
  case NeonTypeFlags::Int8:
  case NeonTypeFlags::Poly8:
    return llvm::FixedVectorType::get(FE->Int8Ty, V1Ty ? 1 : (8 << IsQuad));
  case NeonTypeFlags::Int16:
  case NeonTypeFlags::Poly16:
    return llvm::FixedVectorType::get(FE->Int16Ty, V1Ty ? 1 : (4 << IsQuad));
  case NeonTypeFlags::BFloat16:
    if (AllowBFloatArgsAndRet)
      return llvm::FixedVectorType::get(FE->BFloatTy, V1Ty ? 1 : (4 << IsQuad));
    else
      return llvm::FixedVectorType::get(FE->Int16Ty, V1Ty ? 1 : (4 << IsQuad));
  case NeonTypeFlags::Float16:
    if (HasLegalHalfType)
      return llvm::FixedVectorType::get(FE->HalfTy, V1Ty ? 1 : (4 << IsQuad));
    else
      return llvm::FixedVectorType::get(FE->Int16Ty, V1Ty ? 1 : (4 << IsQuad));
  case NeonTypeFlags::Int32:
    return llvm::FixedVectorType::get(FE->Int32Ty, V1Ty ? 1 : (2 << IsQuad));
  case NeonTypeFlags::Int64:
  case NeonTypeFlags::Poly64:
    return llvm::FixedVectorType::get(FE->Int64Ty, V1Ty ? 1 : (1 << IsQuad));
  case NeonTypeFlags::Poly128:
    return llvm::FixedVectorType::get(FE->Int8Ty, 16);
  case NeonTypeFlags::Float32:
    return llvm::FixedVectorType::get(FE->FloatTy, V1Ty ? 1 : (2 << IsQuad));
  case NeonTypeFlags::Float64:
    return llvm::FixedVectorType::get(FE->DoubleTy, V1Ty ? 1 : (1 << IsQuad));
  }
  llvm_unreachable("Unknown vector element type!");
}

llvm::VectorType *GetFloatNeonType(FunctionEmitter *FE,
                                   NeonTypeFlags IntTypeFlags) {
  int IsQuad = IntTypeFlags.isQuad();
  switch (IntTypeFlags.getEltType()) {
  case NeonTypeFlags::Int16:
    return llvm::FixedVectorType::get(FE->HalfTy, (4 << IsQuad));
  case NeonTypeFlags::Int32:
    return llvm::FixedVectorType::get(FE->FloatTy, (2 << IsQuad));
  case NeonTypeFlags::Int64:
    return llvm::FixedVectorType::get(FE->DoubleTy, (1 << IsQuad));
  default:
    llvm_unreachable("Type can't be converted to floating-point!");
  }
}
} // namespace

Value *FunctionEmitter::genNeonSplat(Value *V, Constant *C,
                                     const ElementCount &Count) {
  Value *SV = llvm::ConstantVector::getSplat(Count, C);
  return Builder.CreateShuffleVector(V, V, SV, "lane");
}

Value *FunctionEmitter::genNeonSplat(Value *V, Constant *C) {
  ElementCount EC = cast<llvm::VectorType>(V->getType())->getElementCount();
  return genNeonSplat(V, C, EC);
}

Value *FunctionEmitter::genNeonCall(Function *F,
                                    llvm::SmallVectorImpl<Value *> &Ops,
                                    const char *name, unsigned shift,
                                    bool rightshift) {
  unsigned j = 0;
  for (Function::const_arg_iterator ai = F->arg_begin(), ae = F->arg_end();
       ai != ae; ++ai, ++j) {
    if (F->isConstrainedFPIntrinsic())
      if (ai->getType()->isMetadataTy())
        continue;
    if (shift > 0 && shift == j)
      Ops[j] = genNeonShiftVector(Ops[j], ai->getType(), rightshift);
    else
      Ops[j] = Builder.CreateBitCast(Ops[j], ai->getType(), name);
  }

  if (F->isConstrainedFPIntrinsic())
    return Builder.CreateConstrainedFPCall(F, Ops, name);
  else
    return Builder.CreateCall(F, Ops, name);
}

Value *FunctionEmitter::genNeonShiftVector(Value *V, llvm::Type *Ty, bool neg) {
  int SV = cast<ConstantInt>(V)->getSExtValue();
  return ConstantInt::get(Ty, neg ? -SV : SV);
}

// Right-shift a vector by a constant.
Value *FunctionEmitter::genNeonRShiftImm(Value *Vec, Value *Shift,
                                         llvm::Type *Ty, bool usgn,
                                         const char *name) {
  llvm::VectorType *VTy = cast<llvm::VectorType>(Ty);

  int ShiftAmt = cast<ConstantInt>(Shift)->getSExtValue();
  int EltSize = VTy->getScalarSizeInBits();

  Vec = Builder.CreateBitCast(Vec, Ty);

  // lshr/ashr are undefined when the shift amount is equal to the vector
  // element size.
  if (ShiftAmt == EltSize) {
    if (usgn) {
      // Right-shifting an unsigned value by its size yields 0.
      return llvm::ConstantAggregateZero::get(VTy);
    } else {
      // Right-shifting a signed value by its size is equivalent
      // to a shift of size-1.
      --ShiftAmt;
      Shift = ConstantInt::get(VTy->getElementType(), ShiftAmt);
    }
  }

  Shift = genNeonShiftVector(Shift, Ty, false);
  if (usgn)
    return Builder.CreateLShr(Vec, Shift, name);
  else
    return Builder.CreateAShr(Vec, Shift, name);
}

enum {
  AddRetType = (1 << 0),
  Add1ArgType = (1 << 1),
  Add2ArgTypes = (1 << 2),

  VectorizeRetType = (1 << 3),
  VectorizeArgTypes = (1 << 4),

  InventFloatType = (1 << 5),
  UnsignedAlts = (1 << 6),

  Use64BitVectors = (1 << 7),
  Use128BitVectors = (1 << 8),

  Vectorize1ArgType = Add1ArgType | VectorizeArgTypes,
  VectorRet = AddRetType | VectorizeRetType,
  VectorRetGetArgs01 =
      AddRetType | Add2ArgTypes | VectorizeRetType | VectorizeArgTypes,
  FpCmpzModifiers =
      AddRetType | VectorizeRetType | Add1ArgType | InventFloatType
};

namespace {
struct ARMVectorIntrinsicInfo {
  const char *NameHint;
  unsigned BuiltinID;
  unsigned LLVMIntrinsic;
  unsigned AltLLVMIntrinsic;
  uint64_t TypeModifier;

  bool operator<(unsigned RHSBuiltinID) const {
    return BuiltinID < RHSBuiltinID;
  }
  bool operator<(const ARMVectorIntrinsicInfo &TE) const {
    return BuiltinID < TE.BuiltinID;
  }
};
} // end anonymous namespace

#define NEONMAP0(NameBase)                                                     \
  {#NameBase, NEON::BI__builtin_neon_##NameBase, 0, 0, 0}

#define NEONMAP1(NameBase, LLVMIntrinsic, TypeModifier)                        \
  {#NameBase, NEON::BI__builtin_neon_##NameBase, Intrinsic::LLVMIntrinsic, 0,  \
   TypeModifier}

#define NEONMAP2(NameBase, LLVMIntrinsic, AltLLVMIntrinsic, TypeModifier)      \
  {#NameBase, NEON::BI__builtin_neon_##NameBase, Intrinsic::LLVMIntrinsic,     \
   Intrinsic::AltLLVMIntrinsic, TypeModifier}

namespace {
const ARMVectorIntrinsicInfo AArch64SIMDIntrinsicMap[] = {
    NEONMAP1(__a64_vcvtq_low_bf16_f32, aarch64_neon_bfcvtn, 0),
    NEONMAP0(splat_lane_v),
    NEONMAP0(splat_laneq_v),
    NEONMAP0(splatq_lane_v),
    NEONMAP0(splatq_laneq_v),
    NEONMAP1(vabs_v, aarch64_neon_abs, 0),
    NEONMAP1(vabsq_v, aarch64_neon_abs, 0),
    NEONMAP0(vadd_v),
    NEONMAP0(vaddhn_v),
    NEONMAP0(vaddq_p128),
    NEONMAP0(vaddq_v),
    NEONMAP1(vaesdq_u8, aarch64_crypto_aesd, 0),
    NEONMAP1(vaeseq_u8, aarch64_crypto_aese, 0),
    NEONMAP1(vaesimcq_u8, aarch64_crypto_aesimc, 0),
    NEONMAP1(vaesmcq_u8, aarch64_crypto_aesmc, 0),
    NEONMAP2(vbcaxq_s16, aarch64_crypto_bcaxu, aarch64_crypto_bcaxs,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(vbcaxq_s32, aarch64_crypto_bcaxu, aarch64_crypto_bcaxs,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(vbcaxq_s64, aarch64_crypto_bcaxu, aarch64_crypto_bcaxs,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(vbcaxq_s8, aarch64_crypto_bcaxu, aarch64_crypto_bcaxs,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(vbcaxq_u16, aarch64_crypto_bcaxu, aarch64_crypto_bcaxs,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(vbcaxq_u32, aarch64_crypto_bcaxu, aarch64_crypto_bcaxs,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(vbcaxq_u64, aarch64_crypto_bcaxu, aarch64_crypto_bcaxs,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(vbcaxq_u8, aarch64_crypto_bcaxu, aarch64_crypto_bcaxs,
             Add1ArgType | UnsignedAlts),
    NEONMAP1(vbfdot_f32, aarch64_neon_bfdot, 0),
    NEONMAP1(vbfdotq_f32, aarch64_neon_bfdot, 0),
    NEONMAP1(vbfmlalbq_f32, aarch64_neon_bfmlalb, 0),
    NEONMAP1(vbfmlaltq_f32, aarch64_neon_bfmlalt, 0),
    NEONMAP1(vbfmmlaq_f32, aarch64_neon_bfmmla, 0),
    NEONMAP1(vcadd_rot270_f16, aarch64_neon_vcadd_rot270, Add1ArgType),
    NEONMAP1(vcadd_rot270_f32, aarch64_neon_vcadd_rot270, Add1ArgType),
    NEONMAP1(vcadd_rot90_f16, aarch64_neon_vcadd_rot90, Add1ArgType),
    NEONMAP1(vcadd_rot90_f32, aarch64_neon_vcadd_rot90, Add1ArgType),
    NEONMAP1(vcaddq_rot270_f16, aarch64_neon_vcadd_rot270, Add1ArgType),
    NEONMAP1(vcaddq_rot270_f32, aarch64_neon_vcadd_rot270, Add1ArgType),
    NEONMAP1(vcaddq_rot270_f64, aarch64_neon_vcadd_rot270, Add1ArgType),
    NEONMAP1(vcaddq_rot90_f16, aarch64_neon_vcadd_rot90, Add1ArgType),
    NEONMAP1(vcaddq_rot90_f32, aarch64_neon_vcadd_rot90, Add1ArgType),
    NEONMAP1(vcaddq_rot90_f64, aarch64_neon_vcadd_rot90, Add1ArgType),
    NEONMAP1(vcage_v, aarch64_neon_facge, 0),
    NEONMAP1(vcageq_v, aarch64_neon_facge, 0),
    NEONMAP1(vcagt_v, aarch64_neon_facgt, 0),
    NEONMAP1(vcagtq_v, aarch64_neon_facgt, 0),
    NEONMAP1(vcale_v, aarch64_neon_facge, 0),
    NEONMAP1(vcaleq_v, aarch64_neon_facge, 0),
    NEONMAP1(vcalt_v, aarch64_neon_facgt, 0),
    NEONMAP1(vcaltq_v, aarch64_neon_facgt, 0),
    NEONMAP0(vceqz_v),
    NEONMAP0(vceqzq_v),
    NEONMAP0(vcgez_v),
    NEONMAP0(vcgezq_v),
    NEONMAP0(vcgtz_v),
    NEONMAP0(vcgtzq_v),
    NEONMAP0(vclez_v),
    NEONMAP0(vclezq_v),
    NEONMAP1(vcls_v, aarch64_neon_cls, Add1ArgType),
    NEONMAP1(vclsq_v, aarch64_neon_cls, Add1ArgType),
    NEONMAP0(vcltz_v),
    NEONMAP0(vcltzq_v),
    NEONMAP1(vclz_v, ctlz, Add1ArgType),
    NEONMAP1(vclzq_v, ctlz, Add1ArgType),
    NEONMAP1(vcmla_f16, aarch64_neon_vcmla_rot0, Add1ArgType),
    NEONMAP1(vcmla_f32, aarch64_neon_vcmla_rot0, Add1ArgType),
    NEONMAP1(vcmla_rot180_f16, aarch64_neon_vcmla_rot180, Add1ArgType),
    NEONMAP1(vcmla_rot180_f32, aarch64_neon_vcmla_rot180, Add1ArgType),
    NEONMAP1(vcmla_rot270_f16, aarch64_neon_vcmla_rot270, Add1ArgType),
    NEONMAP1(vcmla_rot270_f32, aarch64_neon_vcmla_rot270, Add1ArgType),
    NEONMAP1(vcmla_rot90_f16, aarch64_neon_vcmla_rot90, Add1ArgType),
    NEONMAP1(vcmla_rot90_f32, aarch64_neon_vcmla_rot90, Add1ArgType),
    NEONMAP1(vcmlaq_f16, aarch64_neon_vcmla_rot0, Add1ArgType),
    NEONMAP1(vcmlaq_f32, aarch64_neon_vcmla_rot0, Add1ArgType),
    NEONMAP1(vcmlaq_f64, aarch64_neon_vcmla_rot0, Add1ArgType),
    NEONMAP1(vcmlaq_rot180_f16, aarch64_neon_vcmla_rot180, Add1ArgType),
    NEONMAP1(vcmlaq_rot180_f32, aarch64_neon_vcmla_rot180, Add1ArgType),
    NEONMAP1(vcmlaq_rot180_f64, aarch64_neon_vcmla_rot180, Add1ArgType),
    NEONMAP1(vcmlaq_rot270_f16, aarch64_neon_vcmla_rot270, Add1ArgType),
    NEONMAP1(vcmlaq_rot270_f32, aarch64_neon_vcmla_rot270, Add1ArgType),
    NEONMAP1(vcmlaq_rot270_f64, aarch64_neon_vcmla_rot270, Add1ArgType),
    NEONMAP1(vcmlaq_rot90_f16, aarch64_neon_vcmla_rot90, Add1ArgType),
    NEONMAP1(vcmlaq_rot90_f32, aarch64_neon_vcmla_rot90, Add1ArgType),
    NEONMAP1(vcmlaq_rot90_f64, aarch64_neon_vcmla_rot90, Add1ArgType),
    NEONMAP1(vcnt_v, ctpop, Add1ArgType),
    NEONMAP1(vcntq_v, ctpop, Add1ArgType),
    NEONMAP1(vcvt_f16_f32, aarch64_neon_vcvtfp2hf, 0),
    NEONMAP0(vcvt_f16_s16),
    NEONMAP0(vcvt_f16_u16),
    NEONMAP1(vcvt_f32_f16, aarch64_neon_vcvthf2fp, 0),
    NEONMAP0(vcvt_f32_v),
    NEONMAP1(vcvt_n_f16_s16, aarch64_neon_vcvtfxs2fp, 0),
    NEONMAP1(vcvt_n_f16_u16, aarch64_neon_vcvtfxu2fp, 0),
    NEONMAP2(vcvt_n_f32_v, aarch64_neon_vcvtfxu2fp, aarch64_neon_vcvtfxs2fp, 0),
    NEONMAP2(vcvt_n_f64_v, aarch64_neon_vcvtfxu2fp, aarch64_neon_vcvtfxs2fp, 0),
    NEONMAP1(vcvt_n_s16_f16, aarch64_neon_vcvtfp2fxs, 0),
    NEONMAP1(vcvt_n_s32_v, aarch64_neon_vcvtfp2fxs, 0),
    NEONMAP1(vcvt_n_s64_v, aarch64_neon_vcvtfp2fxs, 0),
    NEONMAP1(vcvt_n_u16_f16, aarch64_neon_vcvtfp2fxu, 0),
    NEONMAP1(vcvt_n_u32_v, aarch64_neon_vcvtfp2fxu, 0),
    NEONMAP1(vcvt_n_u64_v, aarch64_neon_vcvtfp2fxu, 0),
    NEONMAP0(vcvtq_f16_s16),
    NEONMAP0(vcvtq_f16_u16),
    NEONMAP0(vcvtq_f32_v),
    NEONMAP1(vcvtq_high_bf16_f32, aarch64_neon_bfcvtn2, 0),
    NEONMAP1(vcvtq_n_f16_s16, aarch64_neon_vcvtfxs2fp, 0),
    NEONMAP1(vcvtq_n_f16_u16, aarch64_neon_vcvtfxu2fp, 0),
    NEONMAP2(vcvtq_n_f32_v, aarch64_neon_vcvtfxu2fp, aarch64_neon_vcvtfxs2fp,
             0),
    NEONMAP2(vcvtq_n_f64_v, aarch64_neon_vcvtfxu2fp, aarch64_neon_vcvtfxs2fp,
             0),
    NEONMAP1(vcvtq_n_s16_f16, aarch64_neon_vcvtfp2fxs, 0),
    NEONMAP1(vcvtq_n_s32_v, aarch64_neon_vcvtfp2fxs, 0),
    NEONMAP1(vcvtq_n_s64_v, aarch64_neon_vcvtfp2fxs, 0),
    NEONMAP1(vcvtq_n_u16_f16, aarch64_neon_vcvtfp2fxu, 0),
    NEONMAP1(vcvtq_n_u32_v, aarch64_neon_vcvtfp2fxu, 0),
    NEONMAP1(vcvtq_n_u64_v, aarch64_neon_vcvtfp2fxu, 0),
    NEONMAP1(vcvtx_f32_v, aarch64_neon_fcvtxn, AddRetType | Add1ArgType),
    NEONMAP1(vdot_s32, aarch64_neon_sdot, 0),
    NEONMAP1(vdot_u32, aarch64_neon_udot, 0),
    NEONMAP1(vdotq_s32, aarch64_neon_sdot, 0),
    NEONMAP1(vdotq_u32, aarch64_neon_udot, 0),
    NEONMAP2(veor3q_s16, aarch64_crypto_eor3u, aarch64_crypto_eor3s,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(veor3q_s32, aarch64_crypto_eor3u, aarch64_crypto_eor3s,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(veor3q_s64, aarch64_crypto_eor3u, aarch64_crypto_eor3s,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(veor3q_s8, aarch64_crypto_eor3u, aarch64_crypto_eor3s,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(veor3q_u16, aarch64_crypto_eor3u, aarch64_crypto_eor3s,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(veor3q_u32, aarch64_crypto_eor3u, aarch64_crypto_eor3s,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(veor3q_u64, aarch64_crypto_eor3u, aarch64_crypto_eor3s,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(veor3q_u8, aarch64_crypto_eor3u, aarch64_crypto_eor3s,
             Add1ArgType | UnsignedAlts),
    NEONMAP0(vext_v),
    NEONMAP0(vextq_v),
    NEONMAP0(vfma_v),
    NEONMAP0(vfmaq_v),
    NEONMAP1(vfmlal_high_f16, aarch64_neon_fmlal2, 0),
    NEONMAP1(vfmlal_low_f16, aarch64_neon_fmlal, 0),
    NEONMAP1(vfmlalq_high_f16, aarch64_neon_fmlal2, 0),
    NEONMAP1(vfmlalq_low_f16, aarch64_neon_fmlal, 0),
    NEONMAP1(vfmlsl_high_f16, aarch64_neon_fmlsl2, 0),
    NEONMAP1(vfmlsl_low_f16, aarch64_neon_fmlsl, 0),
    NEONMAP1(vfmlslq_high_f16, aarch64_neon_fmlsl2, 0),
    NEONMAP1(vfmlslq_low_f16, aarch64_neon_fmlsl, 0),
    NEONMAP2(vhadd_v, aarch64_neon_uhadd, aarch64_neon_shadd,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(vhaddq_v, aarch64_neon_uhadd, aarch64_neon_shadd,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(vhsub_v, aarch64_neon_uhsub, aarch64_neon_shsub,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(vhsubq_v, aarch64_neon_uhsub, aarch64_neon_shsub,
             Add1ArgType | UnsignedAlts),
    NEONMAP1(vld1_x2_v, aarch64_neon_ld1x2, 0),
    NEONMAP1(vld1_x3_v, aarch64_neon_ld1x3, 0),
    NEONMAP1(vld1_x4_v, aarch64_neon_ld1x4, 0),
    NEONMAP1(vld1q_x2_v, aarch64_neon_ld1x2, 0),
    NEONMAP1(vld1q_x3_v, aarch64_neon_ld1x3, 0),
    NEONMAP1(vld1q_x4_v, aarch64_neon_ld1x4, 0),
    NEONMAP1(vmmlaq_s32, aarch64_neon_smmla, 0),
    NEONMAP1(vmmlaq_u32, aarch64_neon_ummla, 0),
    NEONMAP0(vmovl_v),
    NEONMAP0(vmovn_v),
    NEONMAP1(vmul_v, aarch64_neon_pmul, Add1ArgType),
    NEONMAP1(vmulq_v, aarch64_neon_pmul, Add1ArgType),
    NEONMAP1(vpadd_v, aarch64_neon_addp, Add1ArgType),
    NEONMAP2(vpaddl_v, aarch64_neon_uaddlp, aarch64_neon_saddlp, UnsignedAlts),
    NEONMAP2(vpaddlq_v, aarch64_neon_uaddlp, aarch64_neon_saddlp, UnsignedAlts),
    NEONMAP1(vpaddq_v, aarch64_neon_addp, Add1ArgType),
    NEONMAP1(vqabs_v, aarch64_neon_sqabs, Add1ArgType),
    NEONMAP1(vqabsq_v, aarch64_neon_sqabs, Add1ArgType),
    NEONMAP2(vqadd_v, aarch64_neon_uqadd, aarch64_neon_sqadd,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(vqaddq_v, aarch64_neon_uqadd, aarch64_neon_sqadd,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(vqdmlal_v, aarch64_neon_sqdmull, aarch64_neon_sqadd, 0),
    NEONMAP2(vqdmlsl_v, aarch64_neon_sqdmull, aarch64_neon_sqsub, 0),
    NEONMAP1(vqdmulh_lane_v, aarch64_neon_sqdmulh_lane, 0),
    NEONMAP1(vqdmulh_laneq_v, aarch64_neon_sqdmulh_laneq, 0),
    NEONMAP1(vqdmulh_v, aarch64_neon_sqdmulh, Add1ArgType),
    NEONMAP1(vqdmulhq_lane_v, aarch64_neon_sqdmulh_lane, 0),
    NEONMAP1(vqdmulhq_laneq_v, aarch64_neon_sqdmulh_laneq, 0),
    NEONMAP1(vqdmulhq_v, aarch64_neon_sqdmulh, Add1ArgType),
    NEONMAP1(vqdmull_v, aarch64_neon_sqdmull, Add1ArgType),
    NEONMAP2(vqmovn_v, aarch64_neon_uqxtn, aarch64_neon_sqxtn,
             Add1ArgType | UnsignedAlts),
    NEONMAP1(vqmovun_v, aarch64_neon_sqxtun, Add1ArgType),
    NEONMAP1(vqneg_v, aarch64_neon_sqneg, Add1ArgType),
    NEONMAP1(vqnegq_v, aarch64_neon_sqneg, Add1ArgType),
    NEONMAP1(vqrdmlah_s16, aarch64_neon_sqrdmlah, Add1ArgType),
    NEONMAP1(vqrdmlah_s32, aarch64_neon_sqrdmlah, Add1ArgType),
    NEONMAP1(vqrdmlahq_s16, aarch64_neon_sqrdmlah, Add1ArgType),
    NEONMAP1(vqrdmlahq_s32, aarch64_neon_sqrdmlah, Add1ArgType),
    NEONMAP1(vqrdmlsh_s16, aarch64_neon_sqrdmlsh, Add1ArgType),
    NEONMAP1(vqrdmlsh_s32, aarch64_neon_sqrdmlsh, Add1ArgType),
    NEONMAP1(vqrdmlshq_s16, aarch64_neon_sqrdmlsh, Add1ArgType),
    NEONMAP1(vqrdmlshq_s32, aarch64_neon_sqrdmlsh, Add1ArgType),
    NEONMAP1(vqrdmulh_lane_v, aarch64_neon_sqrdmulh_lane, 0),
    NEONMAP1(vqrdmulh_laneq_v, aarch64_neon_sqrdmulh_laneq, 0),
    NEONMAP1(vqrdmulh_v, aarch64_neon_sqrdmulh, Add1ArgType),
    NEONMAP1(vqrdmulhq_lane_v, aarch64_neon_sqrdmulh_lane, 0),
    NEONMAP1(vqrdmulhq_laneq_v, aarch64_neon_sqrdmulh_laneq, 0),
    NEONMAP1(vqrdmulhq_v, aarch64_neon_sqrdmulh, Add1ArgType),
    NEONMAP2(vqrshl_v, aarch64_neon_uqrshl, aarch64_neon_sqrshl,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(vqrshlq_v, aarch64_neon_uqrshl, aarch64_neon_sqrshl,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(vqshl_n_v, aarch64_neon_uqshl, aarch64_neon_sqshl, UnsignedAlts),
    NEONMAP2(vqshl_v, aarch64_neon_uqshl, aarch64_neon_sqshl,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(vqshlq_n_v, aarch64_neon_uqshl, aarch64_neon_sqshl, UnsignedAlts),
    NEONMAP2(vqshlq_v, aarch64_neon_uqshl, aarch64_neon_sqshl,
             Add1ArgType | UnsignedAlts),
    NEONMAP1(vqshlu_n_v, aarch64_neon_sqshlu, 0),
    NEONMAP1(vqshluq_n_v, aarch64_neon_sqshlu, 0),
    NEONMAP2(vqsub_v, aarch64_neon_uqsub, aarch64_neon_sqsub,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(vqsubq_v, aarch64_neon_uqsub, aarch64_neon_sqsub,
             Add1ArgType | UnsignedAlts),
    NEONMAP1(vraddhn_v, aarch64_neon_raddhn, Add1ArgType),
    NEONMAP1(vrax1q_u64, aarch64_crypto_rax1, 0),
    NEONMAP2(vrecpe_v, aarch64_neon_frecpe, aarch64_neon_urecpe, 0),
    NEONMAP2(vrecpeq_v, aarch64_neon_frecpe, aarch64_neon_urecpe, 0),
    NEONMAP1(vrecps_v, aarch64_neon_frecps, Add1ArgType),
    NEONMAP1(vrecpsq_v, aarch64_neon_frecps, Add1ArgType),
    NEONMAP2(vrhadd_v, aarch64_neon_urhadd, aarch64_neon_srhadd,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(vrhaddq_v, aarch64_neon_urhadd, aarch64_neon_srhadd,
             Add1ArgType | UnsignedAlts),
    NEONMAP1(vrnd32x_f32, aarch64_neon_frint32x, Add1ArgType),
    NEONMAP1(vrnd32x_f64, aarch64_neon_frint32x, Add1ArgType),
    NEONMAP1(vrnd32xq_f32, aarch64_neon_frint32x, Add1ArgType),
    NEONMAP1(vrnd32xq_f64, aarch64_neon_frint32x, Add1ArgType),
    NEONMAP1(vrnd32z_f32, aarch64_neon_frint32z, Add1ArgType),
    NEONMAP1(vrnd32z_f64, aarch64_neon_frint32z, Add1ArgType),
    NEONMAP1(vrnd32zq_f32, aarch64_neon_frint32z, Add1ArgType),
    NEONMAP1(vrnd32zq_f64, aarch64_neon_frint32z, Add1ArgType),
    NEONMAP1(vrnd64x_f32, aarch64_neon_frint64x, Add1ArgType),
    NEONMAP1(vrnd64x_f64, aarch64_neon_frint64x, Add1ArgType),
    NEONMAP1(vrnd64xq_f32, aarch64_neon_frint64x, Add1ArgType),
    NEONMAP1(vrnd64xq_f64, aarch64_neon_frint64x, Add1ArgType),
    NEONMAP1(vrnd64z_f32, aarch64_neon_frint64z, Add1ArgType),
    NEONMAP1(vrnd64z_f64, aarch64_neon_frint64z, Add1ArgType),
    NEONMAP1(vrnd64zq_f32, aarch64_neon_frint64z, Add1ArgType),
    NEONMAP1(vrnd64zq_f64, aarch64_neon_frint64z, Add1ArgType),
    NEONMAP0(vrndi_v),
    NEONMAP0(vrndiq_v),
    NEONMAP2(vrshl_v, aarch64_neon_urshl, aarch64_neon_srshl,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(vrshlq_v, aarch64_neon_urshl, aarch64_neon_srshl,
             Add1ArgType | UnsignedAlts),
    NEONMAP2(vrshr_n_v, aarch64_neon_urshl, aarch64_neon_srshl, UnsignedAlts),
    NEONMAP2(vrshrq_n_v, aarch64_neon_urshl, aarch64_neon_srshl, UnsignedAlts),
    NEONMAP2(vrsqrte_v, aarch64_neon_frsqrte, aarch64_neon_ursqrte, 0),
    NEONMAP2(vrsqrteq_v, aarch64_neon_frsqrte, aarch64_neon_ursqrte, 0),
    NEONMAP1(vrsqrts_v, aarch64_neon_frsqrts, Add1ArgType),
    NEONMAP1(vrsqrtsq_v, aarch64_neon_frsqrts, Add1ArgType),
    NEONMAP1(vrsubhn_v, aarch64_neon_rsubhn, Add1ArgType),
    NEONMAP1(vsha1su0q_u32, aarch64_crypto_sha1su0, 0),
    NEONMAP1(vsha1su1q_u32, aarch64_crypto_sha1su1, 0),
    NEONMAP1(vsha256h2q_u32, aarch64_crypto_sha256h2, 0),
    NEONMAP1(vsha256hq_u32, aarch64_crypto_sha256h, 0),
    NEONMAP1(vsha256su0q_u32, aarch64_crypto_sha256su0, 0),
    NEONMAP1(vsha256su1q_u32, aarch64_crypto_sha256su1, 0),
    NEONMAP1(vsha512h2q_u64, aarch64_crypto_sha512h2, 0),
    NEONMAP1(vsha512hq_u64, aarch64_crypto_sha512h, 0),
    NEONMAP1(vsha512su0q_u64, aarch64_crypto_sha512su0, 0),
    NEONMAP1(vsha512su1q_u64, aarch64_crypto_sha512su1, 0),
    NEONMAP0(vshl_n_v),
    NEONMAP2(vshl_v, aarch64_neon_ushl, aarch64_neon_sshl,
             Add1ArgType | UnsignedAlts),
    NEONMAP0(vshll_n_v),
    NEONMAP0(vshlq_n_v),
    NEONMAP2(vshlq_v, aarch64_neon_ushl, aarch64_neon_sshl,
             Add1ArgType | UnsignedAlts),
    NEONMAP0(vshr_n_v),
    NEONMAP0(vshrn_n_v),
    NEONMAP0(vshrq_n_v),
    NEONMAP1(vsm3partw1q_u32, aarch64_crypto_sm3partw1, 0),
    NEONMAP1(vsm3partw2q_u32, aarch64_crypto_sm3partw2, 0),
    NEONMAP1(vsm3ss1q_u32, aarch64_crypto_sm3ss1, 0),
    NEONMAP1(vsm3tt1aq_u32, aarch64_crypto_sm3tt1a, 0),
    NEONMAP1(vsm3tt1bq_u32, aarch64_crypto_sm3tt1b, 0),
    NEONMAP1(vsm3tt2aq_u32, aarch64_crypto_sm3tt2a, 0),
    NEONMAP1(vsm3tt2bq_u32, aarch64_crypto_sm3tt2b, 0),
    NEONMAP1(vsm4ekeyq_u32, aarch64_crypto_sm4ekey, 0),
    NEONMAP1(vsm4eq_u32, aarch64_crypto_sm4e, 0),
    NEONMAP1(vst1_x2_v, aarch64_neon_st1x2, 0),
    NEONMAP1(vst1_x3_v, aarch64_neon_st1x3, 0),
    NEONMAP1(vst1_x4_v, aarch64_neon_st1x4, 0),
    NEONMAP1(vst1q_x2_v, aarch64_neon_st1x2, 0),
    NEONMAP1(vst1q_x3_v, aarch64_neon_st1x3, 0),
    NEONMAP1(vst1q_x4_v, aarch64_neon_st1x4, 0),
    NEONMAP0(vsubhn_v),
    NEONMAP0(vtst_v),
    NEONMAP0(vtstq_v),
    NEONMAP1(vusdot_s32, aarch64_neon_usdot, 0),
    NEONMAP1(vusdotq_s32, aarch64_neon_usdot, 0),
    NEONMAP1(vusmmlaq_s32, aarch64_neon_usmmla, 0),
    NEONMAP1(vxarq_u64, aarch64_crypto_xar, 0),
};

const ARMVectorIntrinsicInfo AArch64SISDIntrinsicMap[] = {
    NEONMAP1(vabdd_f64, aarch64_sisd_fabd, Add1ArgType),
    NEONMAP1(vabds_f32, aarch64_sisd_fabd, Add1ArgType),
    NEONMAP1(vabsd_s64, aarch64_neon_abs, Add1ArgType),
    NEONMAP1(vaddlv_s32, aarch64_neon_saddlv, AddRetType | Add1ArgType),
    NEONMAP1(vaddlv_u32, aarch64_neon_uaddlv, AddRetType | Add1ArgType),
    NEONMAP1(vaddlvq_s32, aarch64_neon_saddlv, AddRetType | Add1ArgType),
    NEONMAP1(vaddlvq_u32, aarch64_neon_uaddlv, AddRetType | Add1ArgType),
    NEONMAP1(vaddv_f32, aarch64_neon_faddv, AddRetType | Add1ArgType),
    NEONMAP1(vaddv_s32, aarch64_neon_saddv, AddRetType | Add1ArgType),
    NEONMAP1(vaddv_u32, aarch64_neon_uaddv, AddRetType | Add1ArgType),
    NEONMAP1(vaddvq_f32, aarch64_neon_faddv, AddRetType | Add1ArgType),
    NEONMAP1(vaddvq_f64, aarch64_neon_faddv, AddRetType | Add1ArgType),
    NEONMAP1(vaddvq_s32, aarch64_neon_saddv, AddRetType | Add1ArgType),
    NEONMAP1(vaddvq_s64, aarch64_neon_saddv, AddRetType | Add1ArgType),
    NEONMAP1(vaddvq_u32, aarch64_neon_uaddv, AddRetType | Add1ArgType),
    NEONMAP1(vaddvq_u64, aarch64_neon_uaddv, AddRetType | Add1ArgType),
    NEONMAP1(vcaged_f64, aarch64_neon_facge, AddRetType | Add1ArgType),
    NEONMAP1(vcages_f32, aarch64_neon_facge, AddRetType | Add1ArgType),
    NEONMAP1(vcagtd_f64, aarch64_neon_facgt, AddRetType | Add1ArgType),
    NEONMAP1(vcagts_f32, aarch64_neon_facgt, AddRetType | Add1ArgType),
    NEONMAP1(vcaled_f64, aarch64_neon_facge, AddRetType | Add1ArgType),
    NEONMAP1(vcales_f32, aarch64_neon_facge, AddRetType | Add1ArgType),
    NEONMAP1(vcaltd_f64, aarch64_neon_facgt, AddRetType | Add1ArgType),
    NEONMAP1(vcalts_f32, aarch64_neon_facgt, AddRetType | Add1ArgType),
    NEONMAP1(vcvtad_s64_f64, aarch64_neon_fcvtas, AddRetType | Add1ArgType),
    NEONMAP1(vcvtad_u64_f64, aarch64_neon_fcvtau, AddRetType | Add1ArgType),
    NEONMAP1(vcvtas_s32_f32, aarch64_neon_fcvtas, AddRetType | Add1ArgType),
    NEONMAP1(vcvtas_u32_f32, aarch64_neon_fcvtau, AddRetType | Add1ArgType),
    NEONMAP1(vcvtd_n_f64_s64, aarch64_neon_vcvtfxs2fp,
             AddRetType | Add1ArgType),
    NEONMAP1(vcvtd_n_f64_u64, aarch64_neon_vcvtfxu2fp,
             AddRetType | Add1ArgType),
    NEONMAP1(vcvtd_n_s64_f64, aarch64_neon_vcvtfp2fxs,
             AddRetType | Add1ArgType),
    NEONMAP1(vcvtd_n_u64_f64, aarch64_neon_vcvtfp2fxu,
             AddRetType | Add1ArgType),
    NEONMAP1(vcvtd_s64_f64, aarch64_neon_fcvtzs, AddRetType | Add1ArgType),
    NEONMAP1(vcvtd_u64_f64, aarch64_neon_fcvtzu, AddRetType | Add1ArgType),
    NEONMAP1(vcvth_bf16_f32, aarch64_neon_bfcvt, 0),
    NEONMAP1(vcvtmd_s64_f64, aarch64_neon_fcvtms, AddRetType | Add1ArgType),
    NEONMAP1(vcvtmd_u64_f64, aarch64_neon_fcvtmu, AddRetType | Add1ArgType),
    NEONMAP1(vcvtms_s32_f32, aarch64_neon_fcvtms, AddRetType | Add1ArgType),
    NEONMAP1(vcvtms_u32_f32, aarch64_neon_fcvtmu, AddRetType | Add1ArgType),
    NEONMAP1(vcvtnd_s64_f64, aarch64_neon_fcvtns, AddRetType | Add1ArgType),
    NEONMAP1(vcvtnd_u64_f64, aarch64_neon_fcvtnu, AddRetType | Add1ArgType),
    NEONMAP1(vcvtns_s32_f32, aarch64_neon_fcvtns, AddRetType | Add1ArgType),
    NEONMAP1(vcvtns_u32_f32, aarch64_neon_fcvtnu, AddRetType | Add1ArgType),
    NEONMAP1(vcvtpd_s64_f64, aarch64_neon_fcvtps, AddRetType | Add1ArgType),
    NEONMAP1(vcvtpd_u64_f64, aarch64_neon_fcvtpu, AddRetType | Add1ArgType),
    NEONMAP1(vcvtps_s32_f32, aarch64_neon_fcvtps, AddRetType | Add1ArgType),
    NEONMAP1(vcvtps_u32_f32, aarch64_neon_fcvtpu, AddRetType | Add1ArgType),
    NEONMAP1(vcvts_n_f32_s32, aarch64_neon_vcvtfxs2fp,
             AddRetType | Add1ArgType),
    NEONMAP1(vcvts_n_f32_u32, aarch64_neon_vcvtfxu2fp,
             AddRetType | Add1ArgType),
    NEONMAP1(vcvts_n_s32_f32, aarch64_neon_vcvtfp2fxs,
             AddRetType | Add1ArgType),
    NEONMAP1(vcvts_n_u32_f32, aarch64_neon_vcvtfp2fxu,
             AddRetType | Add1ArgType),
    NEONMAP1(vcvts_s32_f32, aarch64_neon_fcvtzs, AddRetType | Add1ArgType),
    NEONMAP1(vcvts_u32_f32, aarch64_neon_fcvtzu, AddRetType | Add1ArgType),
    NEONMAP1(vcvtxd_f32_f64, aarch64_sisd_fcvtxn, 0),
    NEONMAP1(vmaxnmv_f32, aarch64_neon_fmaxnmv, AddRetType | Add1ArgType),
    NEONMAP1(vmaxnmvq_f32, aarch64_neon_fmaxnmv, AddRetType | Add1ArgType),
    NEONMAP1(vmaxnmvq_f64, aarch64_neon_fmaxnmv, AddRetType | Add1ArgType),
    NEONMAP1(vmaxv_f32, aarch64_neon_fmaxv, AddRetType | Add1ArgType),
    NEONMAP1(vmaxv_s32, aarch64_neon_smaxv, AddRetType | Add1ArgType),
    NEONMAP1(vmaxv_u32, aarch64_neon_umaxv, AddRetType | Add1ArgType),
    NEONMAP1(vmaxvq_f32, aarch64_neon_fmaxv, AddRetType | Add1ArgType),
    NEONMAP1(vmaxvq_f64, aarch64_neon_fmaxv, AddRetType | Add1ArgType),
    NEONMAP1(vmaxvq_s32, aarch64_neon_smaxv, AddRetType | Add1ArgType),
    NEONMAP1(vmaxvq_u32, aarch64_neon_umaxv, AddRetType | Add1ArgType),
    NEONMAP1(vminnmv_f32, aarch64_neon_fminnmv, AddRetType | Add1ArgType),
    NEONMAP1(vminnmvq_f32, aarch64_neon_fminnmv, AddRetType | Add1ArgType),
    NEONMAP1(vminnmvq_f64, aarch64_neon_fminnmv, AddRetType | Add1ArgType),
    NEONMAP1(vminv_f32, aarch64_neon_fminv, AddRetType | Add1ArgType),
    NEONMAP1(vminv_s32, aarch64_neon_sminv, AddRetType | Add1ArgType),
    NEONMAP1(vminv_u32, aarch64_neon_uminv, AddRetType | Add1ArgType),
    NEONMAP1(vminvq_f32, aarch64_neon_fminv, AddRetType | Add1ArgType),
    NEONMAP1(vminvq_f64, aarch64_neon_fminv, AddRetType | Add1ArgType),
    NEONMAP1(vminvq_s32, aarch64_neon_sminv, AddRetType | Add1ArgType),
    NEONMAP1(vminvq_u32, aarch64_neon_uminv, AddRetType | Add1ArgType),
    NEONMAP1(vmull_p64, aarch64_neon_pmull64, 0),
    NEONMAP1(vmulxd_f64, aarch64_neon_fmulx, Add1ArgType),
    NEONMAP1(vmulxs_f32, aarch64_neon_fmulx, Add1ArgType),
    NEONMAP1(vpaddd_s64, aarch64_neon_uaddv, AddRetType | Add1ArgType),
    NEONMAP1(vpaddd_u64, aarch64_neon_uaddv, AddRetType | Add1ArgType),
    NEONMAP1(vpmaxnmqd_f64, aarch64_neon_fmaxnmv, AddRetType | Add1ArgType),
    NEONMAP1(vpmaxnms_f32, aarch64_neon_fmaxnmv, AddRetType | Add1ArgType),
    NEONMAP1(vpmaxqd_f64, aarch64_neon_fmaxv, AddRetType | Add1ArgType),
    NEONMAP1(vpmaxs_f32, aarch64_neon_fmaxv, AddRetType | Add1ArgType),
    NEONMAP1(vpminnmqd_f64, aarch64_neon_fminnmv, AddRetType | Add1ArgType),
    NEONMAP1(vpminnms_f32, aarch64_neon_fminnmv, AddRetType | Add1ArgType),
    NEONMAP1(vpminqd_f64, aarch64_neon_fminv, AddRetType | Add1ArgType),
    NEONMAP1(vpmins_f32, aarch64_neon_fminv, AddRetType | Add1ArgType),
    NEONMAP1(vqabsb_s8, aarch64_neon_sqabs,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqabsd_s64, aarch64_neon_sqabs, Add1ArgType),
    NEONMAP1(vqabsh_s16, aarch64_neon_sqabs,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqabss_s32, aarch64_neon_sqabs, Add1ArgType),
    NEONMAP1(vqaddb_s8, aarch64_neon_sqadd,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqaddb_u8, aarch64_neon_uqadd,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqaddd_s64, aarch64_neon_sqadd, Add1ArgType),
    NEONMAP1(vqaddd_u64, aarch64_neon_uqadd, Add1ArgType),
    NEONMAP1(vqaddh_s16, aarch64_neon_sqadd,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqaddh_u16, aarch64_neon_uqadd,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqadds_s32, aarch64_neon_sqadd, Add1ArgType),
    NEONMAP1(vqadds_u32, aarch64_neon_uqadd, Add1ArgType),
    NEONMAP1(vqdmulhh_s16, aarch64_neon_sqdmulh,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqdmulhs_s32, aarch64_neon_sqdmulh, Add1ArgType),
    NEONMAP1(vqdmullh_s16, aarch64_neon_sqdmull, VectorRet | Use128BitVectors),
    NEONMAP1(vqdmulls_s32, aarch64_neon_sqdmulls_scalar, 0),
    NEONMAP1(vqmovnd_s64, aarch64_neon_scalar_sqxtn, AddRetType | Add1ArgType),
    NEONMAP1(vqmovnd_u64, aarch64_neon_scalar_uqxtn, AddRetType | Add1ArgType),
    NEONMAP1(vqmovnh_s16, aarch64_neon_sqxtn, VectorRet | Use64BitVectors),
    NEONMAP1(vqmovnh_u16, aarch64_neon_uqxtn, VectorRet | Use64BitVectors),
    NEONMAP1(vqmovns_s32, aarch64_neon_sqxtn, VectorRet | Use64BitVectors),
    NEONMAP1(vqmovns_u32, aarch64_neon_uqxtn, VectorRet | Use64BitVectors),
    NEONMAP1(vqmovund_s64, aarch64_neon_scalar_sqxtun,
             AddRetType | Add1ArgType),
    NEONMAP1(vqmovunh_s16, aarch64_neon_sqxtun, VectorRet | Use64BitVectors),
    NEONMAP1(vqmovuns_s32, aarch64_neon_sqxtun, VectorRet | Use64BitVectors),
    NEONMAP1(vqnegb_s8, aarch64_neon_sqneg,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqnegd_s64, aarch64_neon_sqneg, Add1ArgType),
    NEONMAP1(vqnegh_s16, aarch64_neon_sqneg,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqnegs_s32, aarch64_neon_sqneg, Add1ArgType),
    NEONMAP1(vqrdmlahh_s16, aarch64_neon_sqrdmlah,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqrdmlahs_s32, aarch64_neon_sqrdmlah, Add1ArgType),
    NEONMAP1(vqrdmlshh_s16, aarch64_neon_sqrdmlsh,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqrdmlshs_s32, aarch64_neon_sqrdmlsh, Add1ArgType),
    NEONMAP1(vqrdmulhh_s16, aarch64_neon_sqrdmulh,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqrdmulhs_s32, aarch64_neon_sqrdmulh, Add1ArgType),
    NEONMAP1(vqrshlb_s8, aarch64_neon_sqrshl,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqrshlb_u8, aarch64_neon_uqrshl,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqrshld_s64, aarch64_neon_sqrshl, Add1ArgType),
    NEONMAP1(vqrshld_u64, aarch64_neon_uqrshl, Add1ArgType),
    NEONMAP1(vqrshlh_s16, aarch64_neon_sqrshl,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqrshlh_u16, aarch64_neon_uqrshl,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqrshls_s32, aarch64_neon_sqrshl, Add1ArgType),
    NEONMAP1(vqrshls_u32, aarch64_neon_uqrshl, Add1ArgType),
    NEONMAP1(vqrshrnd_n_s64, aarch64_neon_sqrshrn, AddRetType),
    NEONMAP1(vqrshrnd_n_u64, aarch64_neon_uqrshrn, AddRetType),
    NEONMAP1(vqrshrnh_n_s16, aarch64_neon_sqrshrn, VectorRet | Use64BitVectors),
    NEONMAP1(vqrshrnh_n_u16, aarch64_neon_uqrshrn, VectorRet | Use64BitVectors),
    NEONMAP1(vqrshrns_n_s32, aarch64_neon_sqrshrn, VectorRet | Use64BitVectors),
    NEONMAP1(vqrshrns_n_u32, aarch64_neon_uqrshrn, VectorRet | Use64BitVectors),
    NEONMAP1(vqrshrund_n_s64, aarch64_neon_sqrshrun, AddRetType),
    NEONMAP1(vqrshrunh_n_s16, aarch64_neon_sqrshrun,
             VectorRet | Use64BitVectors),
    NEONMAP1(vqrshruns_n_s32, aarch64_neon_sqrshrun,
             VectorRet | Use64BitVectors),
    NEONMAP1(vqshlb_n_s8, aarch64_neon_sqshl,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqshlb_n_u8, aarch64_neon_uqshl,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqshlb_s8, aarch64_neon_sqshl,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqshlb_u8, aarch64_neon_uqshl,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqshld_s64, aarch64_neon_sqshl, Add1ArgType),
    NEONMAP1(vqshld_u64, aarch64_neon_uqshl, Add1ArgType),
    NEONMAP1(vqshlh_n_s16, aarch64_neon_sqshl,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqshlh_n_u16, aarch64_neon_uqshl,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqshlh_s16, aarch64_neon_sqshl,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqshlh_u16, aarch64_neon_uqshl,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqshls_n_s32, aarch64_neon_sqshl, Add1ArgType),
    NEONMAP1(vqshls_n_u32, aarch64_neon_uqshl, Add1ArgType),
    NEONMAP1(vqshls_s32, aarch64_neon_sqshl, Add1ArgType),
    NEONMAP1(vqshls_u32, aarch64_neon_uqshl, Add1ArgType),
    NEONMAP1(vqshlub_n_s8, aarch64_neon_sqshlu,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqshluh_n_s16, aarch64_neon_sqshlu,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqshlus_n_s32, aarch64_neon_sqshlu, Add1ArgType),
    NEONMAP1(vqshrnd_n_s64, aarch64_neon_sqshrn, AddRetType),
    NEONMAP1(vqshrnd_n_u64, aarch64_neon_uqshrn, AddRetType),
    NEONMAP1(vqshrnh_n_s16, aarch64_neon_sqshrn, VectorRet | Use64BitVectors),
    NEONMAP1(vqshrnh_n_u16, aarch64_neon_uqshrn, VectorRet | Use64BitVectors),
    NEONMAP1(vqshrns_n_s32, aarch64_neon_sqshrn, VectorRet | Use64BitVectors),
    NEONMAP1(vqshrns_n_u32, aarch64_neon_uqshrn, VectorRet | Use64BitVectors),
    NEONMAP1(vqshrund_n_s64, aarch64_neon_sqshrun, AddRetType),
    NEONMAP1(vqshrunh_n_s16, aarch64_neon_sqshrun, VectorRet | Use64BitVectors),
    NEONMAP1(vqshruns_n_s32, aarch64_neon_sqshrun, VectorRet | Use64BitVectors),
    NEONMAP1(vqsubb_s8, aarch64_neon_sqsub,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqsubb_u8, aarch64_neon_uqsub,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqsubd_s64, aarch64_neon_sqsub, Add1ArgType),
    NEONMAP1(vqsubd_u64, aarch64_neon_uqsub, Add1ArgType),
    NEONMAP1(vqsubh_s16, aarch64_neon_sqsub,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqsubh_u16, aarch64_neon_uqsub,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vqsubs_s32, aarch64_neon_sqsub, Add1ArgType),
    NEONMAP1(vqsubs_u32, aarch64_neon_uqsub, Add1ArgType),
    NEONMAP1(vrecped_f64, aarch64_neon_frecpe, Add1ArgType),
    NEONMAP1(vrecpes_f32, aarch64_neon_frecpe, Add1ArgType),
    NEONMAP1(vrecpxd_f64, aarch64_neon_frecpx, Add1ArgType),
    NEONMAP1(vrecpxs_f32, aarch64_neon_frecpx, Add1ArgType),
    NEONMAP1(vrshld_s64, aarch64_neon_srshl, Add1ArgType),
    NEONMAP1(vrshld_u64, aarch64_neon_urshl, Add1ArgType),
    NEONMAP1(vrsqrted_f64, aarch64_neon_frsqrte, Add1ArgType),
    NEONMAP1(vrsqrtes_f32, aarch64_neon_frsqrte, Add1ArgType),
    NEONMAP1(vrsqrtsd_f64, aarch64_neon_frsqrts, Add1ArgType),
    NEONMAP1(vrsqrtss_f32, aarch64_neon_frsqrts, Add1ArgType),
    NEONMAP1(vsha1cq_u32, aarch64_crypto_sha1c, 0),
    NEONMAP1(vsha1h_u32, aarch64_crypto_sha1h, 0),
    NEONMAP1(vsha1mq_u32, aarch64_crypto_sha1m, 0),
    NEONMAP1(vsha1pq_u32, aarch64_crypto_sha1p, 0),
    NEONMAP1(vshld_s64, aarch64_neon_sshl, Add1ArgType),
    NEONMAP1(vshld_u64, aarch64_neon_ushl, Add1ArgType),
    NEONMAP1(vslid_n_s64, aarch64_neon_vsli, Vectorize1ArgType),
    NEONMAP1(vslid_n_u64, aarch64_neon_vsli, Vectorize1ArgType),
    NEONMAP1(vsqaddb_u8, aarch64_neon_usqadd,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vsqaddd_u64, aarch64_neon_usqadd, Add1ArgType),
    NEONMAP1(vsqaddh_u16, aarch64_neon_usqadd,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vsqadds_u32, aarch64_neon_usqadd, Add1ArgType),
    NEONMAP1(vsrid_n_s64, aarch64_neon_vsri, Vectorize1ArgType),
    NEONMAP1(vsrid_n_u64, aarch64_neon_vsri, Vectorize1ArgType),
    NEONMAP1(vuqaddb_s8, aarch64_neon_suqadd,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vuqaddd_s64, aarch64_neon_suqadd, Add1ArgType),
    NEONMAP1(vuqaddh_s16, aarch64_neon_suqadd,
             Vectorize1ArgType | Use64BitVectors),
    NEONMAP1(vuqadds_s32, aarch64_neon_suqadd, Add1ArgType),
    // FP16 scalar intrinisics go here.
    NEONMAP1(vabdh_f16, aarch64_sisd_fabd, Add1ArgType),
    NEONMAP1(vcvtah_s32_f16, aarch64_neon_fcvtas, AddRetType | Add1ArgType),
    NEONMAP1(vcvtah_s64_f16, aarch64_neon_fcvtas, AddRetType | Add1ArgType),
    NEONMAP1(vcvtah_u32_f16, aarch64_neon_fcvtau, AddRetType | Add1ArgType),
    NEONMAP1(vcvtah_u64_f16, aarch64_neon_fcvtau, AddRetType | Add1ArgType),
    NEONMAP1(vcvth_n_f16_s32, aarch64_neon_vcvtfxs2fp,
             AddRetType | Add1ArgType),
    NEONMAP1(vcvth_n_f16_s64, aarch64_neon_vcvtfxs2fp,
             AddRetType | Add1ArgType),
    NEONMAP1(vcvth_n_f16_u32, aarch64_neon_vcvtfxu2fp,
             AddRetType | Add1ArgType),
    NEONMAP1(vcvth_n_f16_u64, aarch64_neon_vcvtfxu2fp,
             AddRetType | Add1ArgType),
    NEONMAP1(vcvth_n_s32_f16, aarch64_neon_vcvtfp2fxs,
             AddRetType | Add1ArgType),
    NEONMAP1(vcvth_n_s64_f16, aarch64_neon_vcvtfp2fxs,
             AddRetType | Add1ArgType),
    NEONMAP1(vcvth_n_u32_f16, aarch64_neon_vcvtfp2fxu,
             AddRetType | Add1ArgType),
    NEONMAP1(vcvth_n_u64_f16, aarch64_neon_vcvtfp2fxu,
             AddRetType | Add1ArgType),
    NEONMAP1(vcvth_s32_f16, aarch64_neon_fcvtzs, AddRetType | Add1ArgType),
    NEONMAP1(vcvth_s64_f16, aarch64_neon_fcvtzs, AddRetType | Add1ArgType),
    NEONMAP1(vcvth_u32_f16, aarch64_neon_fcvtzu, AddRetType | Add1ArgType),
    NEONMAP1(vcvth_u64_f16, aarch64_neon_fcvtzu, AddRetType | Add1ArgType),
    NEONMAP1(vcvtmh_s32_f16, aarch64_neon_fcvtms, AddRetType | Add1ArgType),
    NEONMAP1(vcvtmh_s64_f16, aarch64_neon_fcvtms, AddRetType | Add1ArgType),
    NEONMAP1(vcvtmh_u32_f16, aarch64_neon_fcvtmu, AddRetType | Add1ArgType),
    NEONMAP1(vcvtmh_u64_f16, aarch64_neon_fcvtmu, AddRetType | Add1ArgType),
    NEONMAP1(vcvtnh_s32_f16, aarch64_neon_fcvtns, AddRetType | Add1ArgType),
    NEONMAP1(vcvtnh_s64_f16, aarch64_neon_fcvtns, AddRetType | Add1ArgType),
    NEONMAP1(vcvtnh_u32_f16, aarch64_neon_fcvtnu, AddRetType | Add1ArgType),
    NEONMAP1(vcvtnh_u64_f16, aarch64_neon_fcvtnu, AddRetType | Add1ArgType),
    NEONMAP1(vcvtph_s32_f16, aarch64_neon_fcvtps, AddRetType | Add1ArgType),
    NEONMAP1(vcvtph_s64_f16, aarch64_neon_fcvtps, AddRetType | Add1ArgType),
    NEONMAP1(vcvtph_u32_f16, aarch64_neon_fcvtpu, AddRetType | Add1ArgType),
    NEONMAP1(vcvtph_u64_f16, aarch64_neon_fcvtpu, AddRetType | Add1ArgType),
    NEONMAP1(vmulxh_f16, aarch64_neon_fmulx, Add1ArgType),
    NEONMAP1(vrecpeh_f16, aarch64_neon_frecpe, Add1ArgType),
    NEONMAP1(vrecpxh_f16, aarch64_neon_frecpx, Add1ArgType),
    NEONMAP1(vrsqrteh_f16, aarch64_neon_frsqrte, Add1ArgType),
    NEONMAP1(vrsqrtsh_f16, aarch64_neon_frsqrts, Add1ArgType),
};
} // namespace

// Some intrinsics are equivalent for codegen.
namespace {
const std::pair<unsigned, unsigned> NEONEquivalentIntrinsicMap[] = {
    {
        NEON::BI__builtin_neon_splat_lane_bf16,
        NEON::BI__builtin_neon_splat_lane_v,
    },
    {
        NEON::BI__builtin_neon_splat_laneq_bf16,
        NEON::BI__builtin_neon_splat_laneq_v,
    },
    {
        NEON::BI__builtin_neon_splatq_lane_bf16,
        NEON::BI__builtin_neon_splatq_lane_v,
    },
    {
        NEON::BI__builtin_neon_splatq_laneq_bf16,
        NEON::BI__builtin_neon_splatq_laneq_v,
    },
    {
        NEON::BI__builtin_neon_vabd_f16,
        NEON::BI__builtin_neon_vabd_v,
    },
    {
        NEON::BI__builtin_neon_vabdq_f16,
        NEON::BI__builtin_neon_vabdq_v,
    },
    {
        NEON::BI__builtin_neon_vabs_f16,
        NEON::BI__builtin_neon_vabs_v,
    },
    {
        NEON::BI__builtin_neon_vabsq_f16,
        NEON::BI__builtin_neon_vabsq_v,
    },
    {
        NEON::BI__builtin_neon_vbsl_f16,
        NEON::BI__builtin_neon_vbsl_v,
    },
    {
        NEON::BI__builtin_neon_vbslq_f16,
        NEON::BI__builtin_neon_vbslq_v,
    },
    {
        NEON::BI__builtin_neon_vcage_f16,
        NEON::BI__builtin_neon_vcage_v,
    },
    {
        NEON::BI__builtin_neon_vcageq_f16,
        NEON::BI__builtin_neon_vcageq_v,
    },
    {
        NEON::BI__builtin_neon_vcagt_f16,
        NEON::BI__builtin_neon_vcagt_v,
    },
    {
        NEON::BI__builtin_neon_vcagtq_f16,
        NEON::BI__builtin_neon_vcagtq_v,
    },
    {
        NEON::BI__builtin_neon_vcale_f16,
        NEON::BI__builtin_neon_vcale_v,
    },
    {
        NEON::BI__builtin_neon_vcaleq_f16,
        NEON::BI__builtin_neon_vcaleq_v,
    },
    {
        NEON::BI__builtin_neon_vcalt_f16,
        NEON::BI__builtin_neon_vcalt_v,
    },
    {
        NEON::BI__builtin_neon_vcaltq_f16,
        NEON::BI__builtin_neon_vcaltq_v,
    },
    {
        NEON::BI__builtin_neon_vceqz_f16,
        NEON::BI__builtin_neon_vceqz_v,
    },
    {
        NEON::BI__builtin_neon_vceqzq_f16,
        NEON::BI__builtin_neon_vceqzq_v,
    },
    {
        NEON::BI__builtin_neon_vcgez_f16,
        NEON::BI__builtin_neon_vcgez_v,
    },
    {
        NEON::BI__builtin_neon_vcgezq_f16,
        NEON::BI__builtin_neon_vcgezq_v,
    },
    {
        NEON::BI__builtin_neon_vcgtz_f16,
        NEON::BI__builtin_neon_vcgtz_v,
    },
    {
        NEON::BI__builtin_neon_vcgtzq_f16,
        NEON::BI__builtin_neon_vcgtzq_v,
    },
    {
        NEON::BI__builtin_neon_vclez_f16,
        NEON::BI__builtin_neon_vclez_v,
    },
    {
        NEON::BI__builtin_neon_vclezq_f16,
        NEON::BI__builtin_neon_vclezq_v,
    },
    {
        NEON::BI__builtin_neon_vcltz_f16,
        NEON::BI__builtin_neon_vcltz_v,
    },
    {
        NEON::BI__builtin_neon_vcltzq_f16,
        NEON::BI__builtin_neon_vcltzq_v,
    },
    {
        NEON::BI__builtin_neon_vext_f16,
        NEON::BI__builtin_neon_vext_v,
    },
    {
        NEON::BI__builtin_neon_vextq_f16,
        NEON::BI__builtin_neon_vextq_v,
    },
    {
        NEON::BI__builtin_neon_vfma_f16,
        NEON::BI__builtin_neon_vfma_v,
    },
    {
        NEON::BI__builtin_neon_vfma_lane_f16,
        NEON::BI__builtin_neon_vfma_lane_v,
    },
    {
        NEON::BI__builtin_neon_vfma_laneq_f16,
        NEON::BI__builtin_neon_vfma_laneq_v,
    },
    {
        NEON::BI__builtin_neon_vfmaq_f16,
        NEON::BI__builtin_neon_vfmaq_v,
    },
    {
        NEON::BI__builtin_neon_vfmaq_lane_f16,
        NEON::BI__builtin_neon_vfmaq_lane_v,
    },
    {
        NEON::BI__builtin_neon_vfmaq_laneq_f16,
        NEON::BI__builtin_neon_vfmaq_laneq_v,
    },
    {NEON::BI__builtin_neon_vld1_bf16_x2, NEON::BI__builtin_neon_vld1_x2_v},
    {NEON::BI__builtin_neon_vld1_bf16_x3, NEON::BI__builtin_neon_vld1_x3_v},
    {NEON::BI__builtin_neon_vld1_bf16_x4, NEON::BI__builtin_neon_vld1_x4_v},
    {NEON::BI__builtin_neon_vld1_bf16, NEON::BI__builtin_neon_vld1_v},
    {NEON::BI__builtin_neon_vld1_dup_bf16, NEON::BI__builtin_neon_vld1_dup_v},
    {NEON::BI__builtin_neon_vld1_lane_bf16, NEON::BI__builtin_neon_vld1_lane_v},
    {NEON::BI__builtin_neon_vld1q_bf16_x2, NEON::BI__builtin_neon_vld1q_x2_v},
    {NEON::BI__builtin_neon_vld1q_bf16_x3, NEON::BI__builtin_neon_vld1q_x3_v},
    {NEON::BI__builtin_neon_vld1q_bf16_x4, NEON::BI__builtin_neon_vld1q_x4_v},
    {NEON::BI__builtin_neon_vld1q_bf16, NEON::BI__builtin_neon_vld1q_v},
    {NEON::BI__builtin_neon_vld1q_dup_bf16, NEON::BI__builtin_neon_vld1q_dup_v},
    {NEON::BI__builtin_neon_vld1q_lane_bf16,
     NEON::BI__builtin_neon_vld1q_lane_v},
    {NEON::BI__builtin_neon_vld2_bf16, NEON::BI__builtin_neon_vld2_v},
    {NEON::BI__builtin_neon_vld2_dup_bf16, NEON::BI__builtin_neon_vld2_dup_v},
    {NEON::BI__builtin_neon_vld2_lane_bf16, NEON::BI__builtin_neon_vld2_lane_v},
    {NEON::BI__builtin_neon_vld2q_bf16, NEON::BI__builtin_neon_vld2q_v},
    {NEON::BI__builtin_neon_vld2q_dup_bf16, NEON::BI__builtin_neon_vld2q_dup_v},
    {NEON::BI__builtin_neon_vld2q_lane_bf16,
     NEON::BI__builtin_neon_vld2q_lane_v},
    {NEON::BI__builtin_neon_vld3_bf16, NEON::BI__builtin_neon_vld3_v},
    {NEON::BI__builtin_neon_vld3_dup_bf16, NEON::BI__builtin_neon_vld3_dup_v},
    {NEON::BI__builtin_neon_vld3_lane_bf16, NEON::BI__builtin_neon_vld3_lane_v},
    {NEON::BI__builtin_neon_vld3q_bf16, NEON::BI__builtin_neon_vld3q_v},
    {NEON::BI__builtin_neon_vld3q_dup_bf16, NEON::BI__builtin_neon_vld3q_dup_v},
    {NEON::BI__builtin_neon_vld3q_lane_bf16,
     NEON::BI__builtin_neon_vld3q_lane_v},
    {NEON::BI__builtin_neon_vld4_bf16, NEON::BI__builtin_neon_vld4_v},
    {NEON::BI__builtin_neon_vld4_dup_bf16, NEON::BI__builtin_neon_vld4_dup_v},
    {NEON::BI__builtin_neon_vld4_lane_bf16, NEON::BI__builtin_neon_vld4_lane_v},
    {NEON::BI__builtin_neon_vld4q_bf16, NEON::BI__builtin_neon_vld4q_v},
    {NEON::BI__builtin_neon_vld4q_dup_bf16, NEON::BI__builtin_neon_vld4q_dup_v},
    {NEON::BI__builtin_neon_vld4q_lane_bf16,
     NEON::BI__builtin_neon_vld4q_lane_v},
    {
        NEON::BI__builtin_neon_vmax_f16,
        NEON::BI__builtin_neon_vmax_v,
    },
    {
        NEON::BI__builtin_neon_vmaxnm_f16,
        NEON::BI__builtin_neon_vmaxnm_v,
    },
    {
        NEON::BI__builtin_neon_vmaxnmq_f16,
        NEON::BI__builtin_neon_vmaxnmq_v,
    },
    {
        NEON::BI__builtin_neon_vmaxq_f16,
        NEON::BI__builtin_neon_vmaxq_v,
    },
    {
        NEON::BI__builtin_neon_vmin_f16,
        NEON::BI__builtin_neon_vmin_v,
    },
    {
        NEON::BI__builtin_neon_vminnm_f16,
        NEON::BI__builtin_neon_vminnm_v,
    },
    {
        NEON::BI__builtin_neon_vminnmq_f16,
        NEON::BI__builtin_neon_vminnmq_v,
    },
    {
        NEON::BI__builtin_neon_vminq_f16,
        NEON::BI__builtin_neon_vminq_v,
    },
    {
        NEON::BI__builtin_neon_vmulx_f16,
        NEON::BI__builtin_neon_vmulx_v,
    },
    {
        NEON::BI__builtin_neon_vmulxq_f16,
        NEON::BI__builtin_neon_vmulxq_v,
    },
    {
        NEON::BI__builtin_neon_vpadd_f16,
        NEON::BI__builtin_neon_vpadd_v,
    },
    {
        NEON::BI__builtin_neon_vpaddq_f16,
        NEON::BI__builtin_neon_vpaddq_v,
    },
    {
        NEON::BI__builtin_neon_vpmax_f16,
        NEON::BI__builtin_neon_vpmax_v,
    },
    {
        NEON::BI__builtin_neon_vpmaxnm_f16,
        NEON::BI__builtin_neon_vpmaxnm_v,
    },
    {
        NEON::BI__builtin_neon_vpmaxnmq_f16,
        NEON::BI__builtin_neon_vpmaxnmq_v,
    },
    {
        NEON::BI__builtin_neon_vpmaxq_f16,
        NEON::BI__builtin_neon_vpmaxq_v,
    },
    {
        NEON::BI__builtin_neon_vpmin_f16,
        NEON::BI__builtin_neon_vpmin_v,
    },
    {
        NEON::BI__builtin_neon_vpminnm_f16,
        NEON::BI__builtin_neon_vpminnm_v,
    },
    {
        NEON::BI__builtin_neon_vpminnmq_f16,
        NEON::BI__builtin_neon_vpminnmq_v,
    },
    {
        NEON::BI__builtin_neon_vpminq_f16,
        NEON::BI__builtin_neon_vpminq_v,
    },
    {
        NEON::BI__builtin_neon_vrecpe_f16,
        NEON::BI__builtin_neon_vrecpe_v,
    },
    {
        NEON::BI__builtin_neon_vrecpeq_f16,
        NEON::BI__builtin_neon_vrecpeq_v,
    },
    {
        NEON::BI__builtin_neon_vrecps_f16,
        NEON::BI__builtin_neon_vrecps_v,
    },
    {
        NEON::BI__builtin_neon_vrecpsq_f16,
        NEON::BI__builtin_neon_vrecpsq_v,
    },
    {
        NEON::BI__builtin_neon_vrnd_f16,
        NEON::BI__builtin_neon_vrnd_v,
    },
    {
        NEON::BI__builtin_neon_vrnda_f16,
        NEON::BI__builtin_neon_vrnda_v,
    },
    {
        NEON::BI__builtin_neon_vrndaq_f16,
        NEON::BI__builtin_neon_vrndaq_v,
    },
    {
        NEON::BI__builtin_neon_vrndi_f16,
        NEON::BI__builtin_neon_vrndi_v,
    },
    {
        NEON::BI__builtin_neon_vrndiq_f16,
        NEON::BI__builtin_neon_vrndiq_v,
    },
    {
        NEON::BI__builtin_neon_vrndm_f16,
        NEON::BI__builtin_neon_vrndm_v,
    },
    {
        NEON::BI__builtin_neon_vrndmq_f16,
        NEON::BI__builtin_neon_vrndmq_v,
    },
    {
        NEON::BI__builtin_neon_vrndn_f16,
        NEON::BI__builtin_neon_vrndn_v,
    },
    {
        NEON::BI__builtin_neon_vrndnq_f16,
        NEON::BI__builtin_neon_vrndnq_v,
    },
    {
        NEON::BI__builtin_neon_vrndp_f16,
        NEON::BI__builtin_neon_vrndp_v,
    },
    {
        NEON::BI__builtin_neon_vrndpq_f16,
        NEON::BI__builtin_neon_vrndpq_v,
    },
    {
        NEON::BI__builtin_neon_vrndq_f16,
        NEON::BI__builtin_neon_vrndq_v,
    },
    {
        NEON::BI__builtin_neon_vrndx_f16,
        NEON::BI__builtin_neon_vrndx_v,
    },
    {
        NEON::BI__builtin_neon_vrndxq_f16,
        NEON::BI__builtin_neon_vrndxq_v,
    },
    {
        NEON::BI__builtin_neon_vrsqrte_f16,
        NEON::BI__builtin_neon_vrsqrte_v,
    },
    {
        NEON::BI__builtin_neon_vrsqrteq_f16,
        NEON::BI__builtin_neon_vrsqrteq_v,
    },
    {
        NEON::BI__builtin_neon_vrsqrts_f16,
        NEON::BI__builtin_neon_vrsqrts_v,
    },
    {
        NEON::BI__builtin_neon_vrsqrtsq_f16,
        NEON::BI__builtin_neon_vrsqrtsq_v,
    },
    {
        NEON::BI__builtin_neon_vsqrt_f16,
        NEON::BI__builtin_neon_vsqrt_v,
    },
    {
        NEON::BI__builtin_neon_vsqrtq_f16,
        NEON::BI__builtin_neon_vsqrtq_v,
    },
    {NEON::BI__builtin_neon_vst1_bf16_x2, NEON::BI__builtin_neon_vst1_x2_v},
    {NEON::BI__builtin_neon_vst1_bf16_x3, NEON::BI__builtin_neon_vst1_x3_v},
    {NEON::BI__builtin_neon_vst1_bf16_x4, NEON::BI__builtin_neon_vst1_x4_v},
    {NEON::BI__builtin_neon_vst1_bf16, NEON::BI__builtin_neon_vst1_v},
    {NEON::BI__builtin_neon_vst1_lane_bf16, NEON::BI__builtin_neon_vst1_lane_v},
    {NEON::BI__builtin_neon_vst1q_bf16_x2, NEON::BI__builtin_neon_vst1q_x2_v},
    {NEON::BI__builtin_neon_vst1q_bf16_x3, NEON::BI__builtin_neon_vst1q_x3_v},
    {NEON::BI__builtin_neon_vst1q_bf16_x4, NEON::BI__builtin_neon_vst1q_x4_v},
    {NEON::BI__builtin_neon_vst1q_bf16, NEON::BI__builtin_neon_vst1q_v},
    {NEON::BI__builtin_neon_vst1q_lane_bf16,
     NEON::BI__builtin_neon_vst1q_lane_v},
    {NEON::BI__builtin_neon_vst2_bf16, NEON::BI__builtin_neon_vst2_v},
    {NEON::BI__builtin_neon_vst2_lane_bf16, NEON::BI__builtin_neon_vst2_lane_v},
    {NEON::BI__builtin_neon_vst2q_bf16, NEON::BI__builtin_neon_vst2q_v},
    {NEON::BI__builtin_neon_vst2q_lane_bf16,
     NEON::BI__builtin_neon_vst2q_lane_v},
    {NEON::BI__builtin_neon_vst3_bf16, NEON::BI__builtin_neon_vst3_v},
    {NEON::BI__builtin_neon_vst3_lane_bf16, NEON::BI__builtin_neon_vst3_lane_v},
    {NEON::BI__builtin_neon_vst3q_bf16, NEON::BI__builtin_neon_vst3q_v},
    {NEON::BI__builtin_neon_vst3q_lane_bf16,
     NEON::BI__builtin_neon_vst3q_lane_v},
    {NEON::BI__builtin_neon_vst4_bf16, NEON::BI__builtin_neon_vst4_v},
    {NEON::BI__builtin_neon_vst4_lane_bf16, NEON::BI__builtin_neon_vst4_lane_v},
    {NEON::BI__builtin_neon_vst4q_bf16, NEON::BI__builtin_neon_vst4q_v},
    {NEON::BI__builtin_neon_vst4q_lane_bf16,
     NEON::BI__builtin_neon_vst4q_lane_v},
    {
        NEON::BI__builtin_neon_vtrn_f16,
        NEON::BI__builtin_neon_vtrn_v,
    },
    {
        NEON::BI__builtin_neon_vtrnq_f16,
        NEON::BI__builtin_neon_vtrnq_v,
    },
    {
        NEON::BI__builtin_neon_vuzp_f16,
        NEON::BI__builtin_neon_vuzp_v,
    },
    {
        NEON::BI__builtin_neon_vuzpq_f16,
        NEON::BI__builtin_neon_vuzpq_v,
    },
    {
        NEON::BI__builtin_neon_vzip_f16,
        NEON::BI__builtin_neon_vzip_v,
    },
    {
        NEON::BI__builtin_neon_vzipq_f16,
        NEON::BI__builtin_neon_vzipq_v,
    },
    // The mangling rules cause us to have one ID for each type for
    // vldap1(q)_lane and vstl1(q)_lane, but codegen is equivalent for all of
    // them. Choose an arbitrary one to be handled as tha canonical variation.
    {NEON::BI__builtin_neon_vldap1_lane_u64,
     NEON::BI__builtin_neon_vldap1_lane_s64},
    {NEON::BI__builtin_neon_vldap1_lane_f64,
     NEON::BI__builtin_neon_vldap1_lane_s64},
    {NEON::BI__builtin_neon_vldap1_lane_p64,
     NEON::BI__builtin_neon_vldap1_lane_s64},
    {NEON::BI__builtin_neon_vldap1q_lane_u64,
     NEON::BI__builtin_neon_vldap1q_lane_s64},
    {NEON::BI__builtin_neon_vldap1q_lane_f64,
     NEON::BI__builtin_neon_vldap1q_lane_s64},
    {NEON::BI__builtin_neon_vldap1q_lane_p64,
     NEON::BI__builtin_neon_vldap1q_lane_s64},
    {NEON::BI__builtin_neon_vstl1_lane_u64,
     NEON::BI__builtin_neon_vstl1_lane_s64},
    {NEON::BI__builtin_neon_vstl1_lane_f64,
     NEON::BI__builtin_neon_vstl1_lane_s64},
    {NEON::BI__builtin_neon_vstl1_lane_p64,
     NEON::BI__builtin_neon_vstl1_lane_s64},
    {NEON::BI__builtin_neon_vstl1q_lane_u64,
     NEON::BI__builtin_neon_vstl1q_lane_s64},
    {NEON::BI__builtin_neon_vstl1q_lane_f64,
     NEON::BI__builtin_neon_vstl1q_lane_s64},
    {NEON::BI__builtin_neon_vstl1q_lane_p64,
     NEON::BI__builtin_neon_vstl1q_lane_s64},
};
} // namespace

#undef NEONMAP0
#undef NEONMAP1
#undef NEONMAP2

#define SVEMAP1(NameBase, LLVMIntrinsic, TypeModifier)                         \
  {#NameBase, SVE::BI__builtin_sve_##NameBase, Intrinsic::LLVMIntrinsic, 0,    \
   TypeModifier}

#define SVEMAP2(NameBase, TypeModifier)                                        \
  {#NameBase, SVE::BI__builtin_sve_##NameBase, 0, 0, TypeModifier}
namespace {
const ARMVectorIntrinsicInfo AArch64SVEIntrinsicMap[] = {
#define GET_SVE_LLVM_INTRINSIC_MAP
#include "neverc/Foundation/Builtin/BuiltinsAArch64NeonSVEBridge_cg.def"
#include "neverc/Foundation/arm_sve_builtin_cg.td.h"
#undef GET_SVE_LLVM_INTRINSIC_MAP
};
} // namespace

#undef SVEMAP1
#undef SVEMAP2

#define SMEMAP1(NameBase, LLVMIntrinsic, TypeModifier)                         \
  {#NameBase, SME::BI__builtin_sme_##NameBase, Intrinsic::LLVMIntrinsic, 0,    \
   TypeModifier}

#define SMEMAP2(NameBase, TypeModifier)                                        \
  {#NameBase, SME::BI__builtin_sme_##NameBase, 0, 0, TypeModifier}
namespace {
const ARMVectorIntrinsicInfo AArch64SMEIntrinsicMap[] = {
#define GET_SME_LLVM_INTRINSIC_MAP
#include "neverc/Foundation/arm_sme_builtin_cg.td.h"
#undef GET_SME_LLVM_INTRINSIC_MAP
};
} // namespace

#undef SMEMAP1
#undef SMEMAP2

namespace {
bool AArch64SIMDIntrinsicsProvenSorted = false;
bool AArch64SISDIntrinsicsProvenSorted = false;
bool AArch64SVEIntrinsicsProvenSorted = false;
bool AArch64SMEIntrinsicsProvenSorted = false;

const ARMVectorIntrinsicInfo *
findARMVectorIntrinsicInMap(llvm::ArrayRef<ARMVectorIntrinsicInfo> IntrinsicMap,
                            unsigned BuiltinID, bool &MapProvenSorted) {

#ifndef NDEBUG
  if (!MapProvenSorted) {
    assert(llvm::is_sorted(IntrinsicMap));
    MapProvenSorted = true;
  }
#endif

  const ARMVectorIntrinsicInfo *Builtin =
      llvm::lower_bound(IntrinsicMap, BuiltinID);

  if (Builtin != IntrinsicMap.end() && Builtin->BuiltinID == BuiltinID)
    return Builtin;

  return nullptr;
}
} // namespace

Function *FunctionEmitter::lookupNeonLLVMIntrinsic(unsigned IntrinsicID,
                                                   unsigned Modifier,
                                                   llvm::Type *ArgType,
                                                   const CallExpr *E) {
  int VectorSize = 0;
  if (Modifier & Use64BitVectors)
    VectorSize = 64;
  else if (Modifier & Use128BitVectors)
    VectorSize = 128;

  llvm::SmallVector<llvm::Type *, 3> Tys;
  if (Modifier & AddRetType) {
    llvm::Type *Ty = convertType(E->getCallReturnType(getContext()));
    if (Modifier & VectorizeRetType)
      Ty = llvm::FixedVectorType::get(
          Ty, VectorSize ? VectorSize / Ty->getPrimitiveSizeInBits() : 1);

    Tys.push_back(Ty);
  }

  // Arguments.
  if (Modifier & VectorizeArgTypes) {
    int Elts = VectorSize ? VectorSize / ArgType->getPrimitiveSizeInBits() : 1;
    ArgType = llvm::FixedVectorType::get(ArgType, Elts);
  }

  if (Modifier & (Add1ArgType | Add2ArgTypes))
    Tys.push_back(ArgType);

  if (Modifier & Add2ArgTypes)
    Tys.push_back(ArgType);

  if (Modifier & InventFloatType)
    Tys.push_back(FloatTy);

  return ME.getIntrinsic(IntrinsicID, Tys);
}

namespace {
Value *genCommonNeonSISDBuiltinExpr(FunctionEmitter &FE,
                                    const ARMVectorIntrinsicInfo &SISDInfo,
                                    llvm::SmallVectorImpl<Value *> &Ops,
                                    const CallExpr *E) {
  unsigned BuiltinID = SISDInfo.BuiltinID;
  unsigned int Int = SISDInfo.LLVMIntrinsic;
  unsigned Modifier = SISDInfo.TypeModifier;
  const char *s = SISDInfo.NameHint;

  switch (BuiltinID) {
  case NEON::BI__builtin_neon_vcled_s64:
  case NEON::BI__builtin_neon_vcled_u64:
  case NEON::BI__builtin_neon_vcles_f32:
  case NEON::BI__builtin_neon_vcled_f64:
  case NEON::BI__builtin_neon_vcltd_s64:
  case NEON::BI__builtin_neon_vcltd_u64:
  case NEON::BI__builtin_neon_vclts_f32:
  case NEON::BI__builtin_neon_vcltd_f64:
  case NEON::BI__builtin_neon_vcales_f32:
  case NEON::BI__builtin_neon_vcaled_f64:
  case NEON::BI__builtin_neon_vcalts_f32:
  case NEON::BI__builtin_neon_vcaltd_f64:
    // Only one direction of comparisons actually exist, cmle is actually a cmge
    // with swapped operands. The table gives us the right intrinsic but we
    // still need to do the swap.
    std::swap(Ops[0], Ops[1]);
    break;
  }

  assert(Int && "Generic code assumes a valid intrinsic");

  const Expr *Arg = E->getArg(0);
  llvm::Type *ArgTy = FE.convertType(Arg->getType());
  Function *F = FE.lookupNeonLLVMIntrinsic(Int, Modifier, ArgTy, E);

  int j = 0;
  ConstantInt *C0 = ConstantInt::get(FE.SizeTy, 0);
  for (Function::const_arg_iterator ai = F->arg_begin(), ae = F->arg_end();
       ai != ae; ++ai, ++j) {
    llvm::Type *ArgTy = ai->getType();
    if (Ops[j]->getType()->getPrimitiveSizeInBits() ==
        ArgTy->getPrimitiveSizeInBits())
      continue;

    assert(ArgTy->isVectorTy() && !Ops[j]->getType()->isVectorTy());
    // The constant argument to an _n_ intrinsic always has Int32Ty, so truncate
    // it before inserting.
    Ops[j] = FE.Builder.CreateTruncOrBitCast(
        Ops[j], cast<llvm::VectorType>(ArgTy)->getElementType());
    Ops[j] =
        FE.Builder.CreateInsertElement(PoisonValue::get(ArgTy), Ops[j], C0);
  }

  Value *Result = FE.genNeonCall(F, Ops, s);
  llvm::Type *ResultType = FE.convertType(E->getType());
  if (ResultType->getPrimitiveSizeInBits().getFixedValue() <
      Result->getType()->getPrimitiveSizeInBits().getFixedValue())
    return FE.Builder.CreateExtractElement(Result, C0);

  return FE.Builder.CreateBitCast(Result, ResultType, s);
}
} // namespace

Value *FunctionEmitter::genCommonNeonBuiltinExpr(
    unsigned BuiltinID, unsigned LLVMIntrinsic, unsigned AltLLVMIntrinsic,
    const char *NameHint, unsigned Modifier, const CallExpr *E,
    llvm::SmallVectorImpl<llvm::Value *> &Ops, Address PtrOp0, Address PtrOp1,
    llvm::Triple::ArchType Arch) {
  const Expr *Arg = E->getArg(E->getNumArgs() - 1);
  std::optional<llvm::APSInt> NeonTypeConst =
      Arg->getIntegerConstantExpr(getContext());
  if (!NeonTypeConst)
    return nullptr;

  NeonTypeFlags Type(NeonTypeConst->getZExtValue());
  bool Usgn = Type.isUnsigned();
  bool Quad = Type.isQuad();
  const bool HasLegalHalfType = getTarget().hasLegalHalfType();
  const bool AllowBFloatArgsAndRet =
      getTargetHooks().getABIInfo().allowBFloatArgsAndRet();

  llvm::FixedVectorType *VTy =
      GetNeonType(this, Type, HasLegalHalfType, false, AllowBFloatArgsAndRet);
  llvm::Type *Ty = VTy;
  if (!Ty)
    return nullptr;

  auto getAlignmentValue32 = [&](Address addr) -> Value * {
    return Builder.getInt32(addr.getAlignment().getQuantity());
  };

  unsigned Int = LLVMIntrinsic;
  if ((Modifier & UnsignedAlts) && !Usgn)
    Int = AltLLVMIntrinsic;

  switch (BuiltinID) {
  default:
    break;
  case NEON::BI__builtin_neon_splat_lane_v:
  case NEON::BI__builtin_neon_splat_laneq_v:
  case NEON::BI__builtin_neon_splatq_lane_v:
  case NEON::BI__builtin_neon_splatq_laneq_v: {
    auto NumElements = VTy->getElementCount();
    if (BuiltinID == NEON::BI__builtin_neon_splatq_lane_v)
      NumElements = NumElements * 2;
    if (BuiltinID == NEON::BI__builtin_neon_splat_laneq_v)
      NumElements = NumElements.divideCoefficientBy(2);

    Ops[0] = Builder.CreateBitCast(Ops[0], VTy);
    return genNeonSplat(Ops[0], cast<ConstantInt>(Ops[1]), NumElements);
  }
  case NEON::BI__builtin_neon_vpadd_v:
  case NEON::BI__builtin_neon_vpaddq_v:
    // We don't allow fp/int overloading of intrinsics.
    if (VTy->getElementType()->isFloatingPointTy() &&
        Int == Intrinsic::aarch64_neon_addp)
      Int = Intrinsic::aarch64_neon_faddp;
    break;
  case NEON::BI__builtin_neon_vabs_v:
  case NEON::BI__builtin_neon_vabsq_v:
    if (VTy->getElementType()->isFloatingPointTy())
      return genNeonCall(ME.getIntrinsic(Intrinsic::fabs, Ty), Ops, "vabs");
    return genNeonCall(ME.getIntrinsic(LLVMIntrinsic, Ty), Ops, "vabs");
  case NEON::BI__builtin_neon_vadd_v:
  case NEON::BI__builtin_neon_vaddq_v: {
    llvm::Type *VTy = llvm::FixedVectorType::get(Int8Ty, Quad ? 16 : 8);
    Ops[0] = Builder.CreateBitCast(Ops[0], VTy);
    Ops[1] = Builder.CreateBitCast(Ops[1], VTy);
    Ops[0] = Builder.CreateXor(Ops[0], Ops[1]);
    return Builder.CreateBitCast(Ops[0], Ty);
  }
  case NEON::BI__builtin_neon_vaddhn_v: {
    llvm::FixedVectorType *SrcTy =
        llvm::FixedVectorType::getExtendedElementVectorType(VTy);

    // %sum = add <4 x i32> %lhs, %rhs
    Ops[0] = Builder.CreateBitCast(Ops[0], SrcTy);
    Ops[1] = Builder.CreateBitCast(Ops[1], SrcTy);
    Ops[0] = Builder.CreateAdd(Ops[0], Ops[1], "vaddhn");

    // %high = lshr <4 x i32> %sum, <i32 16, i32 16, i32 16, i32 16>
    Constant *ShiftAmt =
        ConstantInt::get(SrcTy, SrcTy->getScalarSizeInBits() / 2);
    Ops[0] = Builder.CreateLShr(Ops[0], ShiftAmt, "vaddhn");

    // %res = trunc <4 x i32> %high to <4 x i16>
    return Builder.CreateTrunc(Ops[0], VTy, "vaddhn");
  }
  case NEON::BI__builtin_neon_vcale_v:
  case NEON::BI__builtin_neon_vcaleq_v:
  case NEON::BI__builtin_neon_vcalt_v:
  case NEON::BI__builtin_neon_vcaltq_v:
    std::swap(Ops[0], Ops[1]);
    [[fallthrough]];
  case NEON::BI__builtin_neon_vcage_v:
  case NEON::BI__builtin_neon_vcageq_v:
  case NEON::BI__builtin_neon_vcagt_v:
  case NEON::BI__builtin_neon_vcagtq_v: {
    llvm::Type *Ty;
    switch (VTy->getScalarSizeInBits()) {
    default:
      llvm_unreachable("unexpected type");
    case 32:
      Ty = FloatTy;
      break;
    case 64:
      Ty = DoubleTy;
      break;
    case 16:
      Ty = HalfTy;
      break;
    }
    auto *VecFlt = llvm::FixedVectorType::get(Ty, VTy->getNumElements());
    llvm::Type *Tys[] = {VTy, VecFlt};
    Function *F = ME.getIntrinsic(LLVMIntrinsic, Tys);
    return genNeonCall(F, Ops, NameHint);
  }
  case NEON::BI__builtin_neon_vceqz_v:
  case NEON::BI__builtin_neon_vceqzq_v:
    return genAArch64CompareBuiltinExpr(Ops[0], Ty, ICmpInst::FCMP_OEQ,
                                        ICmpInst::ICMP_EQ, "vceqz");
  case NEON::BI__builtin_neon_vcgez_v:
  case NEON::BI__builtin_neon_vcgezq_v:
    return genAArch64CompareBuiltinExpr(Ops[0], Ty, ICmpInst::FCMP_OGE,
                                        ICmpInst::ICMP_SGE, "vcgez");
  case NEON::BI__builtin_neon_vclez_v:
  case NEON::BI__builtin_neon_vclezq_v:
    return genAArch64CompareBuiltinExpr(Ops[0], Ty, ICmpInst::FCMP_OLE,
                                        ICmpInst::ICMP_SLE, "vclez");
  case NEON::BI__builtin_neon_vcgtz_v:
  case NEON::BI__builtin_neon_vcgtzq_v:
    return genAArch64CompareBuiltinExpr(Ops[0], Ty, ICmpInst::FCMP_OGT,
                                        ICmpInst::ICMP_SGT, "vcgtz");
  case NEON::BI__builtin_neon_vcltz_v:
  case NEON::BI__builtin_neon_vcltzq_v:
    return genAArch64CompareBuiltinExpr(Ops[0], Ty, ICmpInst::FCMP_OLT,
                                        ICmpInst::ICMP_SLT, "vcltz");
  case NEON::BI__builtin_neon_vclz_v:
  case NEON::BI__builtin_neon_vclzq_v:
    // We generate target-independent intrinsic, which needs a second argument
    // for whether or not clz of zero is undefined; on AArch64 it isn't.
    Ops.push_back(Builder.getInt1(getTarget().isCLZForZeroUndef()));
    break;
  case NEON::BI__builtin_neon_vcvt_f32_v:
  case NEON::BI__builtin_neon_vcvtq_f32_v:
    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    Ty = GetNeonType(this, NeonTypeFlags(NeonTypeFlags::Float32, false, Quad),
                     HasLegalHalfType);
    return Usgn ? Builder.CreateUIToFP(Ops[0], Ty, "vcvt")
                : Builder.CreateSIToFP(Ops[0], Ty, "vcvt");
  case NEON::BI__builtin_neon_vcvt_f16_s16:
  case NEON::BI__builtin_neon_vcvt_f16_u16:
  case NEON::BI__builtin_neon_vcvtq_f16_s16:
  case NEON::BI__builtin_neon_vcvtq_f16_u16:
    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    Ty = GetNeonType(this, NeonTypeFlags(NeonTypeFlags::Float16, false, Quad),
                     HasLegalHalfType);
    return Usgn ? Builder.CreateUIToFP(Ops[0], Ty, "vcvt")
                : Builder.CreateSIToFP(Ops[0], Ty, "vcvt");
  case NEON::BI__builtin_neon_vcvt_n_f16_s16:
  case NEON::BI__builtin_neon_vcvt_n_f16_u16:
  case NEON::BI__builtin_neon_vcvtq_n_f16_s16:
  case NEON::BI__builtin_neon_vcvtq_n_f16_u16: {
    llvm::Type *Tys[2] = {GetFloatNeonType(this, Type), Ty};
    Function *F = ME.getIntrinsic(Int, Tys);
    return genNeonCall(F, Ops, "vcvt_n");
  }
  case NEON::BI__builtin_neon_vcvt_n_f32_v:
  case NEON::BI__builtin_neon_vcvt_n_f64_v:
  case NEON::BI__builtin_neon_vcvtq_n_f32_v:
  case NEON::BI__builtin_neon_vcvtq_n_f64_v: {
    llvm::Type *Tys[2] = {GetFloatNeonType(this, Type), Ty};
    Int = Usgn ? LLVMIntrinsic : AltLLVMIntrinsic;
    Function *F = ME.getIntrinsic(Int, Tys);
    return genNeonCall(F, Ops, "vcvt_n");
  }
  case NEON::BI__builtin_neon_vcvt_n_s16_f16:
  case NEON::BI__builtin_neon_vcvt_n_s32_v:
  case NEON::BI__builtin_neon_vcvt_n_u16_f16:
  case NEON::BI__builtin_neon_vcvt_n_u32_v:
  case NEON::BI__builtin_neon_vcvt_n_s64_v:
  case NEON::BI__builtin_neon_vcvt_n_u64_v:
  case NEON::BI__builtin_neon_vcvtq_n_s16_f16:
  case NEON::BI__builtin_neon_vcvtq_n_s32_v:
  case NEON::BI__builtin_neon_vcvtq_n_u16_f16:
  case NEON::BI__builtin_neon_vcvtq_n_u32_v:
  case NEON::BI__builtin_neon_vcvtq_n_s64_v:
  case NEON::BI__builtin_neon_vcvtq_n_u64_v: {
    llvm::Type *Tys[2] = {Ty, GetFloatNeonType(this, Type)};
    Function *F = ME.getIntrinsic(LLVMIntrinsic, Tys);
    return genNeonCall(F, Ops, "vcvt_n");
  }
  case NEON::BI__builtin_neon_vcvt_s32_v:
  case NEON::BI__builtin_neon_vcvt_u32_v:
  case NEON::BI__builtin_neon_vcvt_s64_v:
  case NEON::BI__builtin_neon_vcvt_u64_v:
  case NEON::BI__builtin_neon_vcvt_s16_f16:
  case NEON::BI__builtin_neon_vcvt_u16_f16:
  case NEON::BI__builtin_neon_vcvtq_s32_v:
  case NEON::BI__builtin_neon_vcvtq_u32_v:
  case NEON::BI__builtin_neon_vcvtq_s64_v:
  case NEON::BI__builtin_neon_vcvtq_u64_v:
  case NEON::BI__builtin_neon_vcvtq_s16_f16:
  case NEON::BI__builtin_neon_vcvtq_u16_f16: {
    Ops[0] = Builder.CreateBitCast(Ops[0], GetFloatNeonType(this, Type));
    return Usgn ? Builder.CreateFPToUI(Ops[0], Ty, "vcvt")
                : Builder.CreateFPToSI(Ops[0], Ty, "vcvt");
  }
  case NEON::BI__builtin_neon_vcvta_s16_f16:
  case NEON::BI__builtin_neon_vcvta_s32_v:
  case NEON::BI__builtin_neon_vcvta_s64_v:
  case NEON::BI__builtin_neon_vcvta_u16_f16:
  case NEON::BI__builtin_neon_vcvta_u32_v:
  case NEON::BI__builtin_neon_vcvta_u64_v:
  case NEON::BI__builtin_neon_vcvtaq_s16_f16:
  case NEON::BI__builtin_neon_vcvtaq_s32_v:
  case NEON::BI__builtin_neon_vcvtaq_s64_v:
  case NEON::BI__builtin_neon_vcvtaq_u16_f16:
  case NEON::BI__builtin_neon_vcvtaq_u32_v:
  case NEON::BI__builtin_neon_vcvtaq_u64_v:
  case NEON::BI__builtin_neon_vcvtn_s16_f16:
  case NEON::BI__builtin_neon_vcvtn_s32_v:
  case NEON::BI__builtin_neon_vcvtn_s64_v:
  case NEON::BI__builtin_neon_vcvtn_u16_f16:
  case NEON::BI__builtin_neon_vcvtn_u32_v:
  case NEON::BI__builtin_neon_vcvtn_u64_v:
  case NEON::BI__builtin_neon_vcvtnq_s16_f16:
  case NEON::BI__builtin_neon_vcvtnq_s32_v:
  case NEON::BI__builtin_neon_vcvtnq_s64_v:
  case NEON::BI__builtin_neon_vcvtnq_u16_f16:
  case NEON::BI__builtin_neon_vcvtnq_u32_v:
  case NEON::BI__builtin_neon_vcvtnq_u64_v:
  case NEON::BI__builtin_neon_vcvtp_s16_f16:
  case NEON::BI__builtin_neon_vcvtp_s32_v:
  case NEON::BI__builtin_neon_vcvtp_s64_v:
  case NEON::BI__builtin_neon_vcvtp_u16_f16:
  case NEON::BI__builtin_neon_vcvtp_u32_v:
  case NEON::BI__builtin_neon_vcvtp_u64_v:
  case NEON::BI__builtin_neon_vcvtpq_s16_f16:
  case NEON::BI__builtin_neon_vcvtpq_s32_v:
  case NEON::BI__builtin_neon_vcvtpq_s64_v:
  case NEON::BI__builtin_neon_vcvtpq_u16_f16:
  case NEON::BI__builtin_neon_vcvtpq_u32_v:
  case NEON::BI__builtin_neon_vcvtpq_u64_v:
  case NEON::BI__builtin_neon_vcvtm_s16_f16:
  case NEON::BI__builtin_neon_vcvtm_s32_v:
  case NEON::BI__builtin_neon_vcvtm_s64_v:
  case NEON::BI__builtin_neon_vcvtm_u16_f16:
  case NEON::BI__builtin_neon_vcvtm_u32_v:
  case NEON::BI__builtin_neon_vcvtm_u64_v:
  case NEON::BI__builtin_neon_vcvtmq_s16_f16:
  case NEON::BI__builtin_neon_vcvtmq_s32_v:
  case NEON::BI__builtin_neon_vcvtmq_s64_v:
  case NEON::BI__builtin_neon_vcvtmq_u16_f16:
  case NEON::BI__builtin_neon_vcvtmq_u32_v:
  case NEON::BI__builtin_neon_vcvtmq_u64_v: {
    llvm::Type *Tys[2] = {Ty, GetFloatNeonType(this, Type)};
    return genNeonCall(ME.getIntrinsic(LLVMIntrinsic, Tys), Ops, NameHint);
  }
  case NEON::BI__builtin_neon_vcvtx_f32_v: {
    llvm::Type *Tys[2] = {VTy->getTruncatedElementVectorType(VTy), Ty};
    return genNeonCall(ME.getIntrinsic(LLVMIntrinsic, Tys), Ops, NameHint);
  }
  case NEON::BI__builtin_neon_vext_v:
  case NEON::BI__builtin_neon_vextq_v: {
    int CV = cast<ConstantInt>(Ops[2])->getSExtValue();
    llvm::SmallVector<int, 16> Indices;
    for (unsigned i = 0, e = VTy->getNumElements(); i != e; ++i)
      Indices.push_back(i + CV);

    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    return Builder.CreateShuffleVector(Ops[0], Ops[1], Indices, "vext");
  }
  case NEON::BI__builtin_neon_vfma_v:
  case NEON::BI__builtin_neon_vfmaq_v: {
    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[2] = Builder.CreateBitCast(Ops[2], Ty);

    // NEON intrinsic puts accumulator first, unlike the LLVM fma.
    return emitCallMaybeConstrainedFPBuiltin(
        *this, Intrinsic::fma, Intrinsic::experimental_constrained_fma, Ty,
        {Ops[1], Ops[2], Ops[0]});
  }
  case NEON::BI__builtin_neon_vld1_v:
  case NEON::BI__builtin_neon_vld1q_v: {
    llvm::Type *Tys[] = {Ty, Int8PtrTy};
    Ops.push_back(getAlignmentValue32(PtrOp0));
    return genNeonCall(ME.getIntrinsic(LLVMIntrinsic, Tys), Ops, "vld1");
  }
  case NEON::BI__builtin_neon_vld1_x2_v:
  case NEON::BI__builtin_neon_vld1q_x2_v:
  case NEON::BI__builtin_neon_vld1_x3_v:
  case NEON::BI__builtin_neon_vld1q_x3_v:
  case NEON::BI__builtin_neon_vld1_x4_v:
  case NEON::BI__builtin_neon_vld1q_x4_v: {
    llvm::Type *Tys[2] = {VTy, UnqualPtrTy};
    Function *F = ME.getIntrinsic(LLVMIntrinsic, Tys);
    Ops[1] = Builder.CreateCall(F, Ops[1], "vld1xN");
    return Builder.CreateDefaultAlignedStore(Ops[1], Ops[0]);
  }
  case NEON::BI__builtin_neon_vld2_v:
  case NEON::BI__builtin_neon_vld2q_v:
  case NEON::BI__builtin_neon_vld3_v:
  case NEON::BI__builtin_neon_vld3q_v:
  case NEON::BI__builtin_neon_vld4_v:
  case NEON::BI__builtin_neon_vld4q_v:
  case NEON::BI__builtin_neon_vld2_dup_v:
  case NEON::BI__builtin_neon_vld2q_dup_v:
  case NEON::BI__builtin_neon_vld3_dup_v:
  case NEON::BI__builtin_neon_vld3q_dup_v:
  case NEON::BI__builtin_neon_vld4_dup_v:
  case NEON::BI__builtin_neon_vld4q_dup_v: {
    llvm::Type *Tys[] = {Ty, Int8PtrTy};
    Function *F = ME.getIntrinsic(LLVMIntrinsic, Tys);
    Value *Align = getAlignmentValue32(PtrOp1);
    Ops[1] = Builder.CreateCall(F, {Ops[1], Align}, NameHint);
    return Builder.CreateDefaultAlignedStore(Ops[1], Ops[0]);
  }
  case NEON::BI__builtin_neon_vld1_dup_v:
  case NEON::BI__builtin_neon_vld1q_dup_v: {
    Value *V = PoisonValue::get(Ty);
    PtrOp0 = PtrOp0.withElementType(VTy->getElementType());
    LoadInst *Ld = Builder.CreateLoad(PtrOp0);
    llvm::Constant *CI = ConstantInt::get(SizeTy, 0);
    Ops[0] = Builder.CreateInsertElement(V, Ld, CI);
    return genNeonSplat(Ops[0], CI);
  }
  case NEON::BI__builtin_neon_vld2_lane_v:
  case NEON::BI__builtin_neon_vld2q_lane_v:
  case NEON::BI__builtin_neon_vld3_lane_v:
  case NEON::BI__builtin_neon_vld3q_lane_v:
  case NEON::BI__builtin_neon_vld4_lane_v:
  case NEON::BI__builtin_neon_vld4q_lane_v: {
    llvm::Type *Tys[] = {Ty, Int8PtrTy};
    Function *F = ME.getIntrinsic(LLVMIntrinsic, Tys);
    for (unsigned I = 2; I < Ops.size() - 1; ++I)
      Ops[I] = Builder.CreateBitCast(Ops[I], Ty);
    Ops.push_back(getAlignmentValue32(PtrOp1));
    Ops[1] = Builder.CreateCall(F, llvm::ArrayRef(Ops).slice(1), NameHint);
    return Builder.CreateDefaultAlignedStore(Ops[1], Ops[0]);
  }
  case NEON::BI__builtin_neon_vmovl_v: {
    llvm::FixedVectorType *DTy =
        llvm::FixedVectorType::getTruncatedElementVectorType(VTy);
    Ops[0] = Builder.CreateBitCast(Ops[0], DTy);
    if (Usgn)
      return Builder.CreateZExt(Ops[0], Ty, "vmovl");
    return Builder.CreateSExt(Ops[0], Ty, "vmovl");
  }
  case NEON::BI__builtin_neon_vmovn_v: {
    llvm::FixedVectorType *QTy =
        llvm::FixedVectorType::getExtendedElementVectorType(VTy);
    Ops[0] = Builder.CreateBitCast(Ops[0], QTy);
    return Builder.CreateTrunc(Ops[0], Ty, "vmovn");
  }
  case NEON::BI__builtin_neon_vmull_v:
    Int = Usgn ? Intrinsic::aarch64_neon_umull : Intrinsic::aarch64_neon_smull;
    Int = Type.isPoly() ? (unsigned)Intrinsic::aarch64_neon_pmull : Int;
    return genNeonCall(ME.getIntrinsic(Int, Ty), Ops, "vmull");
  case NEON::BI__builtin_neon_vpadal_v:
  case NEON::BI__builtin_neon_vpadalq_v: {
    // The source operand type has twice as many elements of half the size.
    unsigned EltBits = VTy->getElementType()->getPrimitiveSizeInBits();
    llvm::Type *EltTy = llvm::IntegerType::get(getLLVMContext(), EltBits / 2);
    auto *NarrowTy =
        llvm::FixedVectorType::get(EltTy, VTy->getNumElements() * 2);
    llvm::Type *Tys[2] = {Ty, NarrowTy};
    return genNeonCall(ME.getIntrinsic(Int, Tys), Ops, NameHint);
  }
  case NEON::BI__builtin_neon_vpaddl_v:
  case NEON::BI__builtin_neon_vpaddlq_v: {
    // The source operand type has twice as many elements of half the size.
    unsigned EltBits = VTy->getElementType()->getPrimitiveSizeInBits();
    llvm::Type *EltTy = llvm::IntegerType::get(getLLVMContext(), EltBits / 2);
    auto *NarrowTy =
        llvm::FixedVectorType::get(EltTy, VTy->getNumElements() * 2);
    llvm::Type *Tys[2] = {Ty, NarrowTy};
    return genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vpaddl");
  }
  case NEON::BI__builtin_neon_vqdmlal_v:
  case NEON::BI__builtin_neon_vqdmlsl_v: {
    llvm::SmallVector<Value *, 2> MulOps(Ops.begin() + 1, Ops.end());
    Ops[1] = genNeonCall(ME.getIntrinsic(LLVMIntrinsic, Ty), MulOps, "vqdmlal");
    Ops.resize(2);
    return genNeonCall(ME.getIntrinsic(AltLLVMIntrinsic, Ty), Ops, NameHint);
  }
  case NEON::BI__builtin_neon_vqdmulhq_lane_v:
  case NEON::BI__builtin_neon_vqdmulh_lane_v:
  case NEON::BI__builtin_neon_vqrdmulhq_lane_v:
  case NEON::BI__builtin_neon_vqrdmulh_lane_v: {
    auto *RTy = cast<llvm::FixedVectorType>(Ty);
    if (BuiltinID == NEON::BI__builtin_neon_vqdmulhq_lane_v ||
        BuiltinID == NEON::BI__builtin_neon_vqrdmulhq_lane_v)
      RTy = llvm::FixedVectorType::get(RTy->getElementType(),
                                       RTy->getNumElements() * 2);
    llvm::Type *Tys[2] = {
        RTy, GetNeonType(this, NeonTypeFlags(Type.getEltType(), false,
                                             /*isQuad*/ false))};
    return genNeonCall(ME.getIntrinsic(Int, Tys), Ops, NameHint);
  }
  case NEON::BI__builtin_neon_vqdmulhq_laneq_v:
  case NEON::BI__builtin_neon_vqdmulh_laneq_v:
  case NEON::BI__builtin_neon_vqrdmulhq_laneq_v:
  case NEON::BI__builtin_neon_vqrdmulh_laneq_v: {
    llvm::Type *Tys[2] = {
        Ty, GetNeonType(this, NeonTypeFlags(Type.getEltType(), false,
                                            /*isQuad*/ true))};
    return genNeonCall(ME.getIntrinsic(Int, Tys), Ops, NameHint);
  }
  case NEON::BI__builtin_neon_vqshl_n_v:
  case NEON::BI__builtin_neon_vqshlq_n_v:
    return genNeonCall(ME.getIntrinsic(Int, Ty), Ops, "vqshl_n", 1, false);
  case NEON::BI__builtin_neon_vqshlu_n_v:
  case NEON::BI__builtin_neon_vqshluq_n_v:
    return genNeonCall(ME.getIntrinsic(Int, Ty), Ops, "vqshlu_n", 1, false);
  case NEON::BI__builtin_neon_vrecpe_v:
  case NEON::BI__builtin_neon_vrecpeq_v:
  case NEON::BI__builtin_neon_vrsqrte_v:
  case NEON::BI__builtin_neon_vrsqrteq_v:
    Int = Ty->isFPOrFPVectorTy() ? LLVMIntrinsic : AltLLVMIntrinsic;
    return genNeonCall(ME.getIntrinsic(Int, Ty), Ops, NameHint);
  case NEON::BI__builtin_neon_vrndi_v:
  case NEON::BI__builtin_neon_vrndiq_v:
    Int = Builder.getIsFPConstrained()
              ? Intrinsic::experimental_constrained_nearbyint
              : Intrinsic::nearbyint;
    return genNeonCall(ME.getIntrinsic(Int, Ty), Ops, NameHint);
  case NEON::BI__builtin_neon_vrshr_n_v:
  case NEON::BI__builtin_neon_vrshrq_n_v:
    return genNeonCall(ME.getIntrinsic(Int, Ty), Ops, "vrshr_n", 1, true);
  case NEON::BI__builtin_neon_vsha512hq_u64:
  case NEON::BI__builtin_neon_vsha512h2q_u64:
  case NEON::BI__builtin_neon_vsha512su0q_u64:
  case NEON::BI__builtin_neon_vsha512su1q_u64: {
    Function *F = ME.getIntrinsic(Int);
    return genNeonCall(F, Ops, "");
  }
  case NEON::BI__builtin_neon_vshl_n_v:
  case NEON::BI__builtin_neon_vshlq_n_v:
    Ops[1] = genNeonShiftVector(Ops[1], Ty, false);
    return Builder.CreateShl(Builder.CreateBitCast(Ops[0], Ty), Ops[1],
                             "vshl_n");
  case NEON::BI__builtin_neon_vshll_n_v: {
    llvm::FixedVectorType *SrcTy =
        llvm::FixedVectorType::getTruncatedElementVectorType(VTy);
    Ops[0] = Builder.CreateBitCast(Ops[0], SrcTy);
    if (Usgn)
      Ops[0] = Builder.CreateZExt(Ops[0], VTy);
    else
      Ops[0] = Builder.CreateSExt(Ops[0], VTy);
    Ops[1] = genNeonShiftVector(Ops[1], VTy, false);
    return Builder.CreateShl(Ops[0], Ops[1], "vshll_n");
  }
  case NEON::BI__builtin_neon_vshrn_n_v: {
    llvm::FixedVectorType *SrcTy =
        llvm::FixedVectorType::getExtendedElementVectorType(VTy);
    Ops[0] = Builder.CreateBitCast(Ops[0], SrcTy);
    Ops[1] = genNeonShiftVector(Ops[1], SrcTy, false);
    if (Usgn)
      Ops[0] = Builder.CreateLShr(Ops[0], Ops[1]);
    else
      Ops[0] = Builder.CreateAShr(Ops[0], Ops[1]);
    return Builder.CreateTrunc(Ops[0], Ty, "vshrn_n");
  }
  case NEON::BI__builtin_neon_vshr_n_v:
  case NEON::BI__builtin_neon_vshrq_n_v:
    return genNeonRShiftImm(Ops[0], Ops[1], Ty, Usgn, "vshr_n");
  case NEON::BI__builtin_neon_vst1_v:
  case NEON::BI__builtin_neon_vst1q_v:
  case NEON::BI__builtin_neon_vst2_v:
  case NEON::BI__builtin_neon_vst2q_v:
  case NEON::BI__builtin_neon_vst3_v:
  case NEON::BI__builtin_neon_vst3q_v:
  case NEON::BI__builtin_neon_vst4_v:
  case NEON::BI__builtin_neon_vst4q_v:
  case NEON::BI__builtin_neon_vst2_lane_v:
  case NEON::BI__builtin_neon_vst2q_lane_v:
  case NEON::BI__builtin_neon_vst3_lane_v:
  case NEON::BI__builtin_neon_vst3q_lane_v:
  case NEON::BI__builtin_neon_vst4_lane_v:
  case NEON::BI__builtin_neon_vst4q_lane_v: {
    llvm::Type *Tys[] = {Int8PtrTy, Ty};
    Ops.push_back(getAlignmentValue32(PtrOp0));
    return genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "");
  }
  case NEON::BI__builtin_neon_vsm3partw1q_u32:
  case NEON::BI__builtin_neon_vsm3partw2q_u32:
  case NEON::BI__builtin_neon_vsm3ss1q_u32:
  case NEON::BI__builtin_neon_vsm4ekeyq_u32:
  case NEON::BI__builtin_neon_vsm4eq_u32: {
    Function *F = ME.getIntrinsic(Int);
    return genNeonCall(F, Ops, "");
  }
  case NEON::BI__builtin_neon_vsm3tt1aq_u32:
  case NEON::BI__builtin_neon_vsm3tt1bq_u32:
  case NEON::BI__builtin_neon_vsm3tt2aq_u32:
  case NEON::BI__builtin_neon_vsm3tt2bq_u32: {
    Function *F = ME.getIntrinsic(Int);
    Ops[3] = Builder.CreateZExt(Ops[3], Int64Ty);
    return genNeonCall(F, Ops, "");
  }
  case NEON::BI__builtin_neon_vst1_x2_v:
  case NEON::BI__builtin_neon_vst1q_x2_v:
  case NEON::BI__builtin_neon_vst1_x3_v:
  case NEON::BI__builtin_neon_vst1q_x3_v:
  case NEON::BI__builtin_neon_vst1_x4_v:
  case NEON::BI__builtin_neon_vst1q_x4_v: {
    llvm::Type *Tys[2] = {VTy, UnqualPtrTy};
    std::rotate(Ops.begin(), Ops.begin() + 1, Ops.end());
    return genNeonCall(ME.getIntrinsic(LLVMIntrinsic, Tys), Ops, "");
  }
  case NEON::BI__builtin_neon_vsubhn_v: {
    llvm::FixedVectorType *SrcTy =
        llvm::FixedVectorType::getExtendedElementVectorType(VTy);

    // %sum = add <4 x i32> %lhs, %rhs
    Ops[0] = Builder.CreateBitCast(Ops[0], SrcTy);
    Ops[1] = Builder.CreateBitCast(Ops[1], SrcTy);
    Ops[0] = Builder.CreateSub(Ops[0], Ops[1], "vsubhn");

    // %high = lshr <4 x i32> %sum, <i32 16, i32 16, i32 16, i32 16>
    Constant *ShiftAmt =
        ConstantInt::get(SrcTy, SrcTy->getScalarSizeInBits() / 2);
    Ops[0] = Builder.CreateLShr(Ops[0], ShiftAmt, "vsubhn");

    // %res = trunc <4 x i32> %high to <4 x i16>
    return Builder.CreateTrunc(Ops[0], VTy, "vsubhn");
  }
  case NEON::BI__builtin_neon_vtrn_v:
  case NEON::BI__builtin_neon_vtrnq_v: {
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[2] = Builder.CreateBitCast(Ops[2], Ty);
    Value *SV = nullptr;

    for (unsigned vi = 0; vi != 2; ++vi) {
      llvm::SmallVector<int, 16> Indices;
      for (unsigned i = 0, e = VTy->getNumElements(); i != e; i += 2) {
        Indices.push_back(i + vi);
        Indices.push_back(i + e + vi);
      }
      Value *Addr = Builder.CreateConstInBoundsGEP1_32(Ty, Ops[0], vi);
      SV = Builder.CreateShuffleVector(Ops[1], Ops[2], Indices, "vtrn");
      SV = Builder.CreateDefaultAlignedStore(SV, Addr);
    }
    return SV;
  }
  case NEON::BI__builtin_neon_vtst_v:
  case NEON::BI__builtin_neon_vtstq_v: {
    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[0] = Builder.CreateAnd(Ops[0], Ops[1]);
    Ops[0] = Builder.CreateICmp(ICmpInst::ICMP_NE, Ops[0],
                                ConstantAggregateZero::get(Ty));
    return Builder.CreateSExt(Ops[0], Ty, "vtst");
  }
  case NEON::BI__builtin_neon_vuzp_v:
  case NEON::BI__builtin_neon_vuzpq_v: {
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[2] = Builder.CreateBitCast(Ops[2], Ty);
    Value *SV = nullptr;

    for (unsigned vi = 0; vi != 2; ++vi) {
      llvm::SmallVector<int, 16> Indices;
      for (unsigned i = 0, e = VTy->getNumElements(); i != e; ++i)
        Indices.push_back(2 * i + vi);

      Value *Addr = Builder.CreateConstInBoundsGEP1_32(Ty, Ops[0], vi);
      SV = Builder.CreateShuffleVector(Ops[1], Ops[2], Indices, "vuzp");
      SV = Builder.CreateDefaultAlignedStore(SV, Addr);
    }
    return SV;
  }
  case NEON::BI__builtin_neon_vxarq_u64: {
    Function *F = ME.getIntrinsic(Int);
    Ops[2] = Builder.CreateZExt(Ops[2], Int64Ty);
    return genNeonCall(F, Ops, "");
  }
  case NEON::BI__builtin_neon_vzip_v:
  case NEON::BI__builtin_neon_vzipq_v: {
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[2] = Builder.CreateBitCast(Ops[2], Ty);
    Value *SV = nullptr;

    for (unsigned vi = 0; vi != 2; ++vi) {
      llvm::SmallVector<int, 16> Indices;
      for (unsigned i = 0, e = VTy->getNumElements(); i != e; i += 2) {
        Indices.push_back((i + vi * e) >> 1);
        Indices.push_back(((i + vi * e) >> 1) + e);
      }
      Value *Addr = Builder.CreateConstInBoundsGEP1_32(Ty, Ops[0], vi);
      SV = Builder.CreateShuffleVector(Ops[1], Ops[2], Indices, "vzip");
      SV = Builder.CreateDefaultAlignedStore(SV, Addr);
    }
    return SV;
  }
  case NEON::BI__builtin_neon_vdot_s32:
  case NEON::BI__builtin_neon_vdot_u32:
  case NEON::BI__builtin_neon_vdotq_s32:
  case NEON::BI__builtin_neon_vdotq_u32: {
    auto *InputTy =
        llvm::FixedVectorType::get(Int8Ty, Ty->getPrimitiveSizeInBits() / 8);
    llvm::Type *Tys[2] = {Ty, InputTy};
    return genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vdot");
  }
  case NEON::BI__builtin_neon_vfmlal_low_f16:
  case NEON::BI__builtin_neon_vfmlalq_low_f16: {
    auto *InputTy =
        llvm::FixedVectorType::get(HalfTy, Ty->getPrimitiveSizeInBits() / 16);
    llvm::Type *Tys[2] = {Ty, InputTy};
    return genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vfmlal_low");
  }
  case NEON::BI__builtin_neon_vfmlsl_low_f16:
  case NEON::BI__builtin_neon_vfmlslq_low_f16: {
    auto *InputTy =
        llvm::FixedVectorType::get(HalfTy, Ty->getPrimitiveSizeInBits() / 16);
    llvm::Type *Tys[2] = {Ty, InputTy};
    return genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vfmlsl_low");
  }
  case NEON::BI__builtin_neon_vfmlal_high_f16:
  case NEON::BI__builtin_neon_vfmlalq_high_f16: {
    auto *InputTy =
        llvm::FixedVectorType::get(HalfTy, Ty->getPrimitiveSizeInBits() / 16);
    llvm::Type *Tys[2] = {Ty, InputTy};
    return genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vfmlal_high");
  }
  case NEON::BI__builtin_neon_vfmlsl_high_f16:
  case NEON::BI__builtin_neon_vfmlslq_high_f16: {
    auto *InputTy =
        llvm::FixedVectorType::get(HalfTy, Ty->getPrimitiveSizeInBits() / 16);
    llvm::Type *Tys[2] = {Ty, InputTy};
    return genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vfmlsl_high");
  }
  case NEON::BI__builtin_neon_vmmlaq_s32:
  case NEON::BI__builtin_neon_vmmlaq_u32: {
    auto *InputTy =
        llvm::FixedVectorType::get(Int8Ty, Ty->getPrimitiveSizeInBits() / 8);
    llvm::Type *Tys[2] = {Ty, InputTy};
    return genNeonCall(ME.getIntrinsic(LLVMIntrinsic, Tys), Ops, "vmmla");
  }
  case NEON::BI__builtin_neon_vusmmlaq_s32: {
    auto *InputTy =
        llvm::FixedVectorType::get(Int8Ty, Ty->getPrimitiveSizeInBits() / 8);
    llvm::Type *Tys[2] = {Ty, InputTy};
    return genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vusmmla");
  }
  case NEON::BI__builtin_neon_vusdot_s32:
  case NEON::BI__builtin_neon_vusdotq_s32: {
    auto *InputTy =
        llvm::FixedVectorType::get(Int8Ty, Ty->getPrimitiveSizeInBits() / 8);
    llvm::Type *Tys[2] = {Ty, InputTy};
    return genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vusdot");
  }
  case NEON::BI__builtin_neon_vbfdot_f32:
  case NEON::BI__builtin_neon_vbfdotq_f32: {
    llvm::Type *InputTy =
        llvm::FixedVectorType::get(BFloatTy, Ty->getPrimitiveSizeInBits() / 16);
    llvm::Type *Tys[2] = {Ty, InputTy};
    return genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vbfdot");
  }
  }

  assert(Int && "Expected valid intrinsic number");

  Function *F = lookupNeonLLVMIntrinsic(Int, Modifier, Ty, E);

  Value *Result = genNeonCall(F, Ops, NameHint);
  llvm::Type *ResultType = convertType(E->getType());
  // AArch64 intrinsic one-element vector type cast to
  // scalar type expected by the builtin
  return Builder.CreateBitCast(Result, ResultType, NameHint);
}

Value *FunctionEmitter::genAArch64CompareBuiltinExpr(
    Value *Op, llvm::Type *Ty, const CmpInst::Predicate Fp,
    const CmpInst::Predicate Ip, const llvm::Twine &Name) {
  llvm::Type *OTy = Op->getType();

  if (BitCastInst *BI = dyn_cast<BitCastInst>(Op))
    OTy = BI->getOperand(0)->getType();

  Op = Builder.CreateBitCast(Op, OTy);
  if (OTy->getScalarType()->isFloatingPointTy()) {
    if (Fp == CmpInst::FCMP_OEQ)
      Op = Builder.CreateFCmp(Fp, Op, Constant::getNullValue(OTy));
    else
      Op = Builder.CreateFCmpS(Fp, Op, Constant::getNullValue(OTy));
  } else {
    Op = Builder.CreateICmp(Ip, Op, Constant::getNullValue(OTy));
  }
  return Builder.CreateSExt(Op, Ty, Name);
}

namespace {
Value *packTBLDVectorList(FunctionEmitter &FE, llvm::ArrayRef<Value *> Ops,
                          Value *ExtOp, Value *IndexOp, llvm::Type *ResTy,
                          unsigned IntID, const char *Name) {
  llvm::SmallVector<Value *, 2> TblOps;
  if (ExtOp)
    TblOps.push_back(ExtOp);

  llvm::SmallVector<int, 16> Indices;
  auto *TblTy = cast<llvm::FixedVectorType>(Ops[0]->getType());
  for (unsigned i = 0, e = TblTy->getNumElements(); i != e; ++i) {
    Indices.push_back(2 * i);
    Indices.push_back(2 * i + 1);
  }

  int PairPos = 0, End = Ops.size() - 1;
  while (PairPos < End) {
    TblOps.push_back(FE.Builder.CreateShuffleVector(
        Ops[PairPos], Ops[PairPos + 1], Indices, Name));
    PairPos += 2;
  }

  // If there's an odd number of 64-bit lookup table, fill the high 64-bit
  // of the 128-bit lookup table with zero.
  if (PairPos == End) {
    Value *ZeroTbl = ConstantAggregateZero::get(TblTy);
    TblOps.push_back(
        FE.Builder.CreateShuffleVector(Ops[PairPos], ZeroTbl, Indices, Name));
  }

  Function *TblF;
  TblOps.push_back(IndexOp);
  TblF = FE.ME.getIntrinsic(IntID, ResTy);

  return FE.genNeonCall(TblF, TblOps, Name);
}
} // namespace

enum SpecialRegisterAccessKind {
  NormalRead,
  VolatileRead,
  Write,
};

// Generates the IR for the read/write special register builtin,
// ValueType is the type of the value that is to be written or read,
// RegisterType is the type of the register being written to or read from.
namespace {
Value *genSpecialRegisterBuiltin(FunctionEmitter &FE, const CallExpr *E,
                                 llvm::Type *RegisterType,
                                 llvm::Type *ValueType,
                                 SpecialRegisterAccessKind AccessKind,
                                 llvm::StringRef SysReg = "") {
  // write and register intrinsics only support 32, 64 and 128 bit operations.
  assert((RegisterType->isIntegerTy(32) || RegisterType->isIntegerTy(64) ||
          RegisterType->isIntegerTy(128)) &&
         "Unsupported size for register.");

  Emit::CGBuilderTy &Builder = FE.Builder;
  Emit::ModuleEmitter &ME = FE.ME;
  LLVMContext &Context = ME.getLLVMContext();

  if (SysReg.empty()) {
    const Expr *SysRegStrExpr = E->getArg(0)->IgnoreParenCasts();
    SysReg = cast<neverc::StringLiteral>(SysRegStrExpr)->getString();
  }

  llvm::Metadata *Ops[] = {llvm::MDString::get(Context, SysReg)};
  llvm::MDNode *RegName = llvm::MDNode::get(Context, Ops);
  llvm::Value *Metadata = llvm::MetadataAsValue::get(Context, RegName);

  llvm::Type *Types[] = {RegisterType};

  bool MixedTypes = RegisterType->isIntegerTy(64) && ValueType->isIntegerTy(32);
  assert(!(RegisterType->isIntegerTy(32) && ValueType->isIntegerTy(64)) &&
         "Can't fit 64-bit value in 32-bit register");

  if (AccessKind != Write) {
    assert(AccessKind == NormalRead || AccessKind == VolatileRead);
    llvm::Function *F = ME.getIntrinsic(
        AccessKind == VolatileRead ? llvm::Intrinsic::read_volatile_register
                                   : llvm::Intrinsic::read_register,
        Types);
    llvm::Value *Call = Builder.CreateCall(F, Metadata);

    if (MixedTypes)
      // Read into 64 bit register and then truncate result to 32 bit.
      return Builder.CreateTrunc(Call, ValueType);

    if (ValueType->isPointerTy())
      // Have i32/i64 result (Call) but want to return a VoidPtrTy (i8*).
      return Builder.CreateIntToPtr(Call, ValueType);

    return Call;
  }

  llvm::Function *F = ME.getIntrinsic(llvm::Intrinsic::write_register, Types);
  llvm::Value *ArgValue = FE.genScalarExpr(E->getArg(1));
  if (MixedTypes) {
    // Extend 32 bit write value to 64 bit to pass to write.
    ArgValue = Builder.CreateZExt(ArgValue, RegisterType);
    return Builder.CreateCall(F, {Metadata, ArgValue});
  }

  if (ValueType->isPointerTy()) {
    // Have VoidPtrTy ArgValue but want to return an i32/i64.
    ArgValue = Builder.CreatePtrToInt(ArgValue, RegisterType);
    return Builder.CreateCall(F, {Metadata, ArgValue});
  }

  return Builder.CreateCall(F, {Metadata, ArgValue});
}

Value *genAArch64TblBuiltinExpr(FunctionEmitter &FE, unsigned BuiltinID,
                                const CallExpr *E,
                                llvm::SmallVectorImpl<Value *> &Ops,
                                llvm::Triple::ArchType Arch) {
  unsigned int Int = 0;
  const char *s = nullptr;

  switch (BuiltinID) {
  default:
    return nullptr;
  case NEON::BI__builtin_neon_vtbl1_v:
  case NEON::BI__builtin_neon_vqtbl1_v:
  case NEON::BI__builtin_neon_vqtbl1q_v:
  case NEON::BI__builtin_neon_vtbl2_v:
  case NEON::BI__builtin_neon_vqtbl2_v:
  case NEON::BI__builtin_neon_vqtbl2q_v:
  case NEON::BI__builtin_neon_vtbl3_v:
  case NEON::BI__builtin_neon_vqtbl3_v:
  case NEON::BI__builtin_neon_vqtbl3q_v:
  case NEON::BI__builtin_neon_vtbl4_v:
  case NEON::BI__builtin_neon_vqtbl4_v:
  case NEON::BI__builtin_neon_vqtbl4q_v:
    break;
  case NEON::BI__builtin_neon_vtbx1_v:
  case NEON::BI__builtin_neon_vqtbx1_v:
  case NEON::BI__builtin_neon_vqtbx1q_v:
  case NEON::BI__builtin_neon_vtbx2_v:
  case NEON::BI__builtin_neon_vqtbx2_v:
  case NEON::BI__builtin_neon_vqtbx2q_v:
  case NEON::BI__builtin_neon_vtbx3_v:
  case NEON::BI__builtin_neon_vqtbx3_v:
  case NEON::BI__builtin_neon_vqtbx3q_v:
  case NEON::BI__builtin_neon_vtbx4_v:
  case NEON::BI__builtin_neon_vqtbx4_v:
  case NEON::BI__builtin_neon_vqtbx4q_v:
    break;
  }

  assert(E->getNumArgs() >= 3);

  const Expr *Arg = E->getArg(E->getNumArgs() - 1);
  std::optional<llvm::APSInt> Result =
      Arg->getIntegerConstantExpr(FE.getContext());
  if (!Result)
    return nullptr;

  NeonTypeFlags Type = Result->getZExtValue();
  llvm::FixedVectorType *Ty = GetNeonType(&FE, Type);
  if (!Ty)
    return nullptr;

  Emit::CGBuilderTy &Builder = FE.Builder;

  // AArch64 scalar builtins are not overloaded, they do not have an extra
  // argument that specifies the vector type, need to handle each case.
  switch (BuiltinID) {
  case NEON::BI__builtin_neon_vtbl1_v: {
    return packTBLDVectorList(FE, llvm::ArrayRef(Ops).slice(0, 1), nullptr,
                              Ops[1], Ty, Intrinsic::aarch64_neon_tbl1,
                              "vtbl1");
  }
  case NEON::BI__builtin_neon_vtbl2_v: {
    return packTBLDVectorList(FE, llvm::ArrayRef(Ops).slice(0, 2), nullptr,
                              Ops[2], Ty, Intrinsic::aarch64_neon_tbl1,
                              "vtbl1");
  }
  case NEON::BI__builtin_neon_vtbl3_v: {
    return packTBLDVectorList(FE, llvm::ArrayRef(Ops).slice(0, 3), nullptr,
                              Ops[3], Ty, Intrinsic::aarch64_neon_tbl2,
                              "vtbl2");
  }
  case NEON::BI__builtin_neon_vtbl4_v: {
    return packTBLDVectorList(FE, llvm::ArrayRef(Ops).slice(0, 4), nullptr,
                              Ops[4], Ty, Intrinsic::aarch64_neon_tbl2,
                              "vtbl2");
  }
  case NEON::BI__builtin_neon_vtbx1_v: {
    Value *TblRes =
        packTBLDVectorList(FE, llvm::ArrayRef(Ops).slice(1, 1), nullptr, Ops[2],
                           Ty, Intrinsic::aarch64_neon_tbl1, "vtbl1");

    llvm::Constant *EightV = ConstantInt::get(Ty, 8);
    Value *CmpRes = Builder.CreateICmp(ICmpInst::ICMP_UGE, Ops[2], EightV);
    CmpRes = Builder.CreateSExt(CmpRes, Ty);

    Value *EltsFromInput = Builder.CreateAnd(CmpRes, Ops[0]);
    Value *EltsFromTbl = Builder.CreateAnd(Builder.CreateNot(CmpRes), TblRes);
    return Builder.CreateOr(EltsFromInput, EltsFromTbl, "vtbx");
  }
  case NEON::BI__builtin_neon_vtbx2_v: {
    return packTBLDVectorList(FE, llvm::ArrayRef(Ops).slice(1, 2), Ops[0],
                              Ops[3], Ty, Intrinsic::aarch64_neon_tbx1,
                              "vtbx1");
  }
  case NEON::BI__builtin_neon_vtbx3_v: {
    Value *TblRes =
        packTBLDVectorList(FE, llvm::ArrayRef(Ops).slice(1, 3), nullptr, Ops[4],
                           Ty, Intrinsic::aarch64_neon_tbl2, "vtbl2");

    llvm::Constant *TwentyFourV = ConstantInt::get(Ty, 24);
    Value *CmpRes = Builder.CreateICmp(ICmpInst::ICMP_UGE, Ops[4], TwentyFourV);
    CmpRes = Builder.CreateSExt(CmpRes, Ty);

    Value *EltsFromInput = Builder.CreateAnd(CmpRes, Ops[0]);
    Value *EltsFromTbl = Builder.CreateAnd(Builder.CreateNot(CmpRes), TblRes);
    return Builder.CreateOr(EltsFromInput, EltsFromTbl, "vtbx");
  }
  case NEON::BI__builtin_neon_vtbx4_v: {
    return packTBLDVectorList(FE, llvm::ArrayRef(Ops).slice(1, 4), Ops[0],
                              Ops[5], Ty, Intrinsic::aarch64_neon_tbx2,
                              "vtbx2");
  }
  case NEON::BI__builtin_neon_vqtbl1_v:
  case NEON::BI__builtin_neon_vqtbl1q_v:
    Int = Intrinsic::aarch64_neon_tbl1;
    s = "vtbl1";
    break;
  case NEON::BI__builtin_neon_vqtbl2_v:
  case NEON::BI__builtin_neon_vqtbl2q_v: {
    Int = Intrinsic::aarch64_neon_tbl2;
    s = "vtbl2";
    break;
  case NEON::BI__builtin_neon_vqtbl3_v:
  case NEON::BI__builtin_neon_vqtbl3q_v:
    Int = Intrinsic::aarch64_neon_tbl3;
    s = "vtbl3";
    break;
  case NEON::BI__builtin_neon_vqtbl4_v:
  case NEON::BI__builtin_neon_vqtbl4q_v:
    Int = Intrinsic::aarch64_neon_tbl4;
    s = "vtbl4";
    break;
  case NEON::BI__builtin_neon_vqtbx1_v:
  case NEON::BI__builtin_neon_vqtbx1q_v:
    Int = Intrinsic::aarch64_neon_tbx1;
    s = "vtbx1";
    break;
  case NEON::BI__builtin_neon_vqtbx2_v:
  case NEON::BI__builtin_neon_vqtbx2q_v:
    Int = Intrinsic::aarch64_neon_tbx2;
    s = "vtbx2";
    break;
  case NEON::BI__builtin_neon_vqtbx3_v:
  case NEON::BI__builtin_neon_vqtbx3q_v:
    Int = Intrinsic::aarch64_neon_tbx3;
    s = "vtbx3";
    break;
  case NEON::BI__builtin_neon_vqtbx4_v:
  case NEON::BI__builtin_neon_vqtbx4q_v:
    Int = Intrinsic::aarch64_neon_tbx4;
    s = "vtbx4";
    break;
  }
  }

  if (!Int)
    return nullptr;

  Function *F = FE.ME.getIntrinsic(Int, Ty);
  return FE.genNeonCall(F, Ops, s);
}
} // namespace

Value *FunctionEmitter::vectorWrapScalar16(Value *Op) {
  auto *VTy = llvm::FixedVectorType::get(Int16Ty, 4);
  Op = Builder.CreateBitCast(Op, Int16Ty);
  Value *V = PoisonValue::get(VTy);
  llvm::Constant *CI = ConstantInt::get(SizeTy, 0);
  Op = Builder.CreateInsertElement(V, Op, CI);
  return Op;
}

llvm::Type *FunctionEmitter::sveBuiltinMemEltTy(const SVETypeFlags &TypeFlags) {
  switch (TypeFlags.getMemEltType()) {
  case SVETypeFlags::MemEltTyDefault:
    return getEltType(TypeFlags);
  case SVETypeFlags::MemEltTyInt8:
    return Builder.getInt8Ty();
  case SVETypeFlags::MemEltTyInt16:
    return Builder.getInt16Ty();
  case SVETypeFlags::MemEltTyInt32:
    return Builder.getInt32Ty();
  case SVETypeFlags::MemEltTyInt64:
    return Builder.getInt64Ty();
  }
  llvm_unreachable("Unknown MemEltType");
}

llvm::Type *FunctionEmitter::getEltType(const SVETypeFlags &TypeFlags) {
  switch (TypeFlags.getEltType()) {
  default:
    llvm_unreachable("Invalid SVETypeFlag!");

  case SVETypeFlags::EltTyInt8:
    return Builder.getInt8Ty();
  case SVETypeFlags::EltTyInt16:
    return Builder.getInt16Ty();
  case SVETypeFlags::EltTyInt32:
    return Builder.getInt32Ty();
  case SVETypeFlags::EltTyInt64:
    return Builder.getInt64Ty();
  case SVETypeFlags::EltTyInt128:
    return Builder.getInt128Ty();

  case SVETypeFlags::EltTyFloat16:
    return Builder.getHalfTy();
  case SVETypeFlags::EltTyFloat32:
    return Builder.getFloatTy();
  case SVETypeFlags::EltTyFloat64:
    return Builder.getDoubleTy();

  case SVETypeFlags::EltTyBFloat16:
    return Builder.getBFloatTy();

  case SVETypeFlags::EltTyBool8:
  case SVETypeFlags::EltTyBool16:
  case SVETypeFlags::EltTyBool32:
  case SVETypeFlags::EltTyBool64:
    return Builder.getInt1Ty();
  }
}

// Return the llvm predicate vector type corresponding to the specified element
// TypeFlags.
llvm::ScalableVectorType *
FunctionEmitter::getSVEPredType(const SVETypeFlags &TypeFlags) {
  switch (TypeFlags.getEltType()) {
  default:
    llvm_unreachable("Unhandled SVETypeFlag!");

  case SVETypeFlags::EltTyInt8:
    return llvm::ScalableVectorType::get(Builder.getInt1Ty(), 16);
  case SVETypeFlags::EltTyInt16:
    return llvm::ScalableVectorType::get(Builder.getInt1Ty(), 8);
  case SVETypeFlags::EltTyInt32:
    return llvm::ScalableVectorType::get(Builder.getInt1Ty(), 4);
  case SVETypeFlags::EltTyInt64:
    return llvm::ScalableVectorType::get(Builder.getInt1Ty(), 2);

  case SVETypeFlags::EltTyBFloat16:
    return llvm::ScalableVectorType::get(Builder.getInt1Ty(), 8);
  case SVETypeFlags::EltTyFloat16:
    return llvm::ScalableVectorType::get(Builder.getInt1Ty(), 8);
  case SVETypeFlags::EltTyFloat32:
    return llvm::ScalableVectorType::get(Builder.getInt1Ty(), 4);
  case SVETypeFlags::EltTyFloat64:
    return llvm::ScalableVectorType::get(Builder.getInt1Ty(), 2);

  case SVETypeFlags::EltTyBool8:
    return llvm::ScalableVectorType::get(Builder.getInt1Ty(), 16);
  case SVETypeFlags::EltTyBool16:
    return llvm::ScalableVectorType::get(Builder.getInt1Ty(), 8);
  case SVETypeFlags::EltTyBool32:
    return llvm::ScalableVectorType::get(Builder.getInt1Ty(), 4);
  case SVETypeFlags::EltTyBool64:
    return llvm::ScalableVectorType::get(Builder.getInt1Ty(), 2);
  }
}

// Return the llvm vector type corresponding to the specified element TypeFlags.
llvm::ScalableVectorType *
FunctionEmitter::getSVEType(const SVETypeFlags &TypeFlags) {
  switch (TypeFlags.getEltType()) {
  default:
    llvm_unreachable("Invalid SVETypeFlag!");

  case SVETypeFlags::EltTyInt8:
    return llvm::ScalableVectorType::get(Builder.getInt8Ty(), 16);
  case SVETypeFlags::EltTyInt16:
    return llvm::ScalableVectorType::get(Builder.getInt16Ty(), 8);
  case SVETypeFlags::EltTyInt32:
    return llvm::ScalableVectorType::get(Builder.getInt32Ty(), 4);
  case SVETypeFlags::EltTyInt64:
    return llvm::ScalableVectorType::get(Builder.getInt64Ty(), 2);

  case SVETypeFlags::EltTyFloat16:
    return llvm::ScalableVectorType::get(Builder.getHalfTy(), 8);
  case SVETypeFlags::EltTyBFloat16:
    return llvm::ScalableVectorType::get(Builder.getBFloatTy(), 8);
  case SVETypeFlags::EltTyFloat32:
    return llvm::ScalableVectorType::get(Builder.getFloatTy(), 4);
  case SVETypeFlags::EltTyFloat64:
    return llvm::ScalableVectorType::get(Builder.getDoubleTy(), 2);

  case SVETypeFlags::EltTyBool8:
    return llvm::ScalableVectorType::get(Builder.getInt1Ty(), 16);
  case SVETypeFlags::EltTyBool16:
    return llvm::ScalableVectorType::get(Builder.getInt1Ty(), 8);
  case SVETypeFlags::EltTyBool32:
    return llvm::ScalableVectorType::get(Builder.getInt1Ty(), 4);
  case SVETypeFlags::EltTyBool64:
    return llvm::ScalableVectorType::get(Builder.getInt1Ty(), 2);
  }
}

llvm::Value *FunctionEmitter::genSVEAllTruePred(const SVETypeFlags &TypeFlags) {
  Function *Ptrue =
      ME.getIntrinsic(Intrinsic::aarch64_sve_ptrue, getSVEPredType(TypeFlags));
  return Builder.CreateCall(Ptrue, {Builder.getInt32(/*SV_ALL*/ 31)});
}

constexpr unsigned SVEBitsPerBlock = 128;

namespace {
llvm::ScalableVectorType *getSVEVectorForElementType(llvm::Type *EltTy) {
  unsigned NumElts = SVEBitsPerBlock / EltTy->getScalarSizeInBits();
  return llvm::ScalableVectorType::get(EltTy, NumElts);
}
} // namespace

// Reinterpret the input predicate so that it can be used to correctly isolate
// the elements of the specified datatype.
Value *FunctionEmitter::genSVEPredicateCast(Value *Pred,
                                            llvm::ScalableVectorType *VTy) {

  if (isa<TargetExtType>(Pred->getType()) &&
      cast<TargetExtType>(Pred->getType())->getName() == "aarch64.svcount")
    return Pred;

  auto *RTy = llvm::VectorType::get(IntegerType::get(getLLVMContext(), 1), VTy);
  if (Pred->getType() == RTy)
    return Pred;

  unsigned IntID;
  llvm::Type *IntrinsicTy;
  switch (VTy->getMinNumElements()) {
  default:
    llvm_unreachable("unsupported element count!");
  case 1:
  case 2:
  case 4:
  case 8:
    IntID = Intrinsic::aarch64_sve_convert_from_svbool;
    IntrinsicTy = RTy;
    break;
  case 16:
    IntID = Intrinsic::aarch64_sve_convert_to_svbool;
    IntrinsicTy = Pred->getType();
    break;
  }

  Function *F = ME.getIntrinsic(IntID, IntrinsicTy);
  Value *C = Builder.CreateCall(F, Pred);
  assert(C->getType() == RTy && "Unexpected return type!");
  return C;
}

Value *FunctionEmitter::genSVEGatherLoad(const SVETypeFlags &TypeFlags,
                                         llvm::SmallVectorImpl<Value *> &Ops,
                                         unsigned IntID) {
  auto *ResultTy = getSVEType(TypeFlags);
  auto *OverloadedTy =
      llvm::ScalableVectorType::get(sveBuiltinMemEltTy(TypeFlags), ResultTy);

  Function *F = nullptr;
  if (Ops[1]->getType()->isVectorTy())
    // This is the "vector base, scalar offset" case. In order to uniquely
    // map this built-in to an LLVM IR intrinsic, we need both the return type
    // and the type of the vector base.
    F = ME.getIntrinsic(IntID, {OverloadedTy, Ops[1]->getType()});
  else
    // This is the "scalar base, vector offset case". The type of the offset
    // is encoded in the name of the intrinsic. We only need to specify the
    // return type in order to uniquely map this built-in to an LLVM IR
    // intrinsic.
    F = ME.getIntrinsic(IntID, OverloadedTy);

  // At the ACLE level there's only one predicate type, svbool_t, which is
  // mapped to <n x 16 x i1>. However, this might be incompatible with the
  // actual type being loaded. For example, when loading doubles (i64) the
  // predicate should be <n x 2 x i1> instead. At the IR level the type of
  // the predicate and the data being loaded must match. Cast to the type
  // expected by the intrinsic. The intrinsic itself should be defined in
  // a way than enforces relations between parameter types.
  Ops[0] = genSVEPredicateCast(
      Ops[0], cast<llvm::ScalableVectorType>(F->getArg(0)->getType()));

  // Pass 0 when the offset is missing. This can only be applied when using
  // the "vector base" addressing mode for which ACLE allows no offset. The
  // corresponding LLVM IR always requires an offset.
  if (Ops.size() == 2) {
    assert(Ops[1]->getType()->isVectorTy() && "Scalar base requires an offset");
    Ops.push_back(ConstantInt::get(Int64Ty, 0));
  }

  // For "vector base, scalar index" scale the index so that it becomes a
  // scalar offset.
  if (!TypeFlags.isByteIndexed() && Ops[1]->getType()->isVectorTy()) {
    unsigned BytesPerElt =
        OverloadedTy->getElementType()->getScalarSizeInBits() / 8;
    Ops[2] = Builder.CreateShl(Ops[2], Log2_32(BytesPerElt));
  }

  Value *Call = Builder.CreateCall(F, Ops);

  // The following sext/zext is only needed when ResultTy != OverloadedTy. In
  // other cases it's folded into a nop.
  return TypeFlags.isZExtReturn() ? Builder.CreateZExt(Call, ResultTy)
                                  : Builder.CreateSExt(Call, ResultTy);
}

Value *FunctionEmitter::genSVEScatterStore(const SVETypeFlags &TypeFlags,
                                           llvm::SmallVectorImpl<Value *> &Ops,
                                           unsigned IntID) {
  auto *SrcDataTy = getSVEType(TypeFlags);
  auto *OverloadedTy =
      llvm::ScalableVectorType::get(sveBuiltinMemEltTy(TypeFlags), SrcDataTy);

  // In ACLE the source data is passed in the last argument, whereas in LLVM IR
  // it's the first argument. Move it accordingly.
  Ops.insert(Ops.begin(), Ops.pop_back_val());

  Function *F = nullptr;
  if (Ops[2]->getType()->isVectorTy())
    // This is the "vector base, scalar offset" case. In order to uniquely
    // map this built-in to an LLVM IR intrinsic, we need both the return type
    // and the type of the vector base.
    F = ME.getIntrinsic(IntID, {OverloadedTy, Ops[2]->getType()});
  else
    // This is the "scalar base, vector offset case". The type of the offset
    // is encoded in the name of the intrinsic. We only need to specify the
    // return type in order to uniquely map this built-in to an LLVM IR
    // intrinsic.
    F = ME.getIntrinsic(IntID, OverloadedTy);

  // Pass 0 when the offset is missing. This can only be applied when using
  // the "vector base" addressing mode for which ACLE allows no offset. The
  // corresponding LLVM IR always requires an offset.
  if (Ops.size() == 3) {
    assert(Ops[1]->getType()->isVectorTy() && "Scalar base requires an offset");
    Ops.push_back(ConstantInt::get(Int64Ty, 0));
  }

  // Truncation is needed when SrcDataTy != OverloadedTy. In other cases it's
  // folded into a nop.
  Ops[0] = Builder.CreateTrunc(Ops[0], OverloadedTy);

  // At the ACLE level there's only one predicate type, svbool_t, which is
  // mapped to <n x 16 x i1>. However, this might be incompatible with the
  // actual type being stored. For example, when storing doubles (i64) the
  // predicated should be <n x 2 x i1> instead. At the IR level the type of
  // the predicate and the data being stored must match. Cast to the type
  // expected by the intrinsic. The intrinsic itself should be defined in
  // a way that enforces relations between parameter types.
  Ops[1] = genSVEPredicateCast(
      Ops[1], cast<llvm::ScalableVectorType>(F->getArg(1)->getType()));

  // For "vector base, scalar index" scale the index so that it becomes a
  // scalar offset.
  if (!TypeFlags.isByteIndexed() && Ops[2]->getType()->isVectorTy()) {
    unsigned BytesPerElt =
        OverloadedTy->getElementType()->getScalarSizeInBits() / 8;
    Ops[3] = Builder.CreateShl(Ops[3], Log2_32(BytesPerElt));
  }

  return Builder.CreateCall(F, Ops);
}

Value *
FunctionEmitter::genSVEGatherPrefetch(const SVETypeFlags &TypeFlags,
                                      llvm::SmallVectorImpl<Value *> &Ops,
                                      unsigned IntID) {
  // The gather prefetches are overloaded on the vector input - this can either
  // be the vector of base addresses or vector of offsets.
  auto *OverloadedTy = dyn_cast<llvm::ScalableVectorType>(Ops[1]->getType());
  if (!OverloadedTy)
    OverloadedTy = cast<llvm::ScalableVectorType>(Ops[2]->getType());

  Ops[0] = genSVEPredicateCast(Ops[0], OverloadedTy);

  // vector + imm addressing modes
  if (Ops[1]->getType()->isVectorTy()) {
    if (Ops.size() == 3) {
      // Pass 0 for 'vector+imm' when the index is omitted.
      Ops.push_back(ConstantInt::get(Int64Ty, 0));

      // The sv_prfop is the last operand in the builtin and IR intrinsic.
      std::swap(Ops[2], Ops[3]);
    } else {
      // Index needs to be passed as scaled offset.
      llvm::Type *MemEltTy = sveBuiltinMemEltTy(TypeFlags);
      unsigned BytesPerElt = MemEltTy->getPrimitiveSizeInBits() / 8;
      if (BytesPerElt > 1)
        Ops[2] = Builder.CreateShl(Ops[2], Log2_32(BytesPerElt));
    }
  }

  Function *F = ME.getIntrinsic(IntID, OverloadedTy);
  return Builder.CreateCall(F, Ops);
}

Value *FunctionEmitter::genSVEStructLoad(const SVETypeFlags &TypeFlags,
                                         llvm::SmallVectorImpl<Value *> &Ops,
                                         unsigned IntID) {
  llvm::ScalableVectorType *VTy = getSVEType(TypeFlags);

  unsigned N;
  switch (IntID) {
  case Intrinsic::aarch64_sve_ld2_sret:
  case Intrinsic::aarch64_sve_ld1_pn_x2:
  case Intrinsic::aarch64_sve_ldnt1_pn_x2:
  case Intrinsic::aarch64_sve_ld2q_sret:
    N = 2;
    break;
  case Intrinsic::aarch64_sve_ld3_sret:
  case Intrinsic::aarch64_sve_ld3q_sret:
    N = 3;
    break;
  case Intrinsic::aarch64_sve_ld4_sret:
  case Intrinsic::aarch64_sve_ld1_pn_x4:
  case Intrinsic::aarch64_sve_ldnt1_pn_x4:
  case Intrinsic::aarch64_sve_ld4q_sret:
    N = 4;
    break;
  default:
    llvm_unreachable("unknown intrinsic!");
  }
  auto RetTy =
      llvm::VectorType::get(VTy->getElementType(), VTy->getElementCount() * N);

  Value *Predicate = genSVEPredicateCast(Ops[0], VTy);
  Value *BasePtr = Ops[1];

  // Does the load have an offset?
  if (Ops.size() > 2)
    BasePtr = Builder.CreateGEP(VTy, BasePtr, Ops[2]);

  Function *F = ME.getIntrinsic(IntID, {VTy});
  Value *Call = Builder.CreateCall(F, {Predicate, BasePtr});
  unsigned MinElts = VTy->getMinNumElements();
  Value *Ret = llvm::PoisonValue::get(RetTy);
  for (unsigned I = 0; I < N; I++) {
    Value *Idx = ConstantInt::get(ME.Int64Ty, I * MinElts);
    Value *SRet = Builder.CreateExtractValue(Call, I);
    Ret = Builder.CreateInsertVector(RetTy, Ret, SRet, Idx);
  }
  return Ret;
}

Value *FunctionEmitter::genSVEStructStore(const SVETypeFlags &TypeFlags,
                                          llvm::SmallVectorImpl<Value *> &Ops,
                                          unsigned IntID) {
  llvm::ScalableVectorType *VTy = getSVEType(TypeFlags);

  unsigned N;
  switch (IntID) {
  case Intrinsic::aarch64_sve_st2:
  case Intrinsic::aarch64_sve_st1_pn_x2:
  case Intrinsic::aarch64_sve_stnt1_pn_x2:
  case Intrinsic::aarch64_sve_st2q:
    N = 2;
    break;
  case Intrinsic::aarch64_sve_st3:
  case Intrinsic::aarch64_sve_st3q:
    N = 3;
    break;
  case Intrinsic::aarch64_sve_st4:
  case Intrinsic::aarch64_sve_st1_pn_x4:
  case Intrinsic::aarch64_sve_stnt1_pn_x4:
  case Intrinsic::aarch64_sve_st4q:
    N = 4;
    break;
  default:
    llvm_unreachable("unknown intrinsic!");
  }

  Value *Predicate = genSVEPredicateCast(Ops[0], VTy);
  Value *BasePtr = Ops[1];

  // Does the store have an offset?
  if (Ops.size() > (2 + N))
    BasePtr = Builder.CreateGEP(VTy, BasePtr, Ops[2]);

  // The llvm.aarch64.sve.st2/3/4 intrinsics take legal part vectors, so we
  // need to break up the tuple vector.
  llvm::SmallVector<llvm::Value *, 5> Operands;
  for (unsigned I = Ops.size() - N; I < Ops.size(); ++I)
    Operands.push_back(Ops[I]);
  Operands.append({Predicate, BasePtr});
  Function *F = ME.getIntrinsic(IntID, {VTy});

  return Builder.CreateCall(F, Operands);
}

// SVE2's svpmullb and svpmullt builtins are similar to the svpmullb_pair and
// svpmullt_pair intrinsics, with the exception that their results are bitcast
// to a wider type.
Value *FunctionEmitter::genSVEPMull(const SVETypeFlags &TypeFlags,
                                    llvm::SmallVectorImpl<Value *> &Ops,
                                    unsigned BuiltinID) {
  // Splat scalar operand to vector (intrinsics with _n infix)
  if (TypeFlags.hasSplatOperand()) {
    unsigned OpNo = TypeFlags.getSplatOperand();
    Ops[OpNo] = genSVEDupX(Ops[OpNo]);
  }

  // The pair-wise function has a narrower overloaded type.
  Function *F = ME.getIntrinsic(BuiltinID, Ops[0]->getType());
  Value *Call = Builder.CreateCall(F, {Ops[0], Ops[1]});

  // Now bitcast to the wider result type.
  llvm::ScalableVectorType *Ty = getSVEType(TypeFlags);
  return genSVEReinterpret(Call, Ty);
}

Value *FunctionEmitter::genSVEMovl(const SVETypeFlags &TypeFlags,
                                   llvm::ArrayRef<Value *> Ops,
                                   unsigned BuiltinID) {
  llvm::Type *OverloadedTy = getSVEType(TypeFlags);
  Function *F = ME.getIntrinsic(BuiltinID, OverloadedTy);
  return Builder.CreateCall(F, {Ops[0], Builder.getInt32(0)});
}

Value *FunctionEmitter::genSVEPrefetchLoad(const SVETypeFlags &TypeFlags,
                                           llvm::SmallVectorImpl<Value *> &Ops,
                                           unsigned BuiltinID) {
  auto *MemEltTy = sveBuiltinMemEltTy(TypeFlags);
  auto *VectorTy = getSVEVectorForElementType(MemEltTy);
  auto *MemoryTy = llvm::ScalableVectorType::get(MemEltTy, VectorTy);

  Value *Predicate = genSVEPredicateCast(Ops[0], MemoryTy);
  Value *BasePtr = Ops[1];

  // Implement the index operand if not omitted.
  if (Ops.size() > 3)
    BasePtr = Builder.CreateGEP(MemoryTy, BasePtr, Ops[2]);

  Value *PrfOp = Ops.back();

  Function *F = ME.getIntrinsic(BuiltinID, Predicate->getType());
  return Builder.CreateCall(F, {Predicate, BasePtr, PrfOp});
}

Value *FunctionEmitter::genSVEMaskedLoad(const CallExpr *E,
                                         llvm::Type *ReturnTy,
                                         llvm::SmallVectorImpl<Value *> &Ops,
                                         unsigned IntrinsicID,
                                         bool IsZExtReturn) {
  QualType LangPTy = E->getArg(1)->getType();
  llvm::Type *MemEltTy = ME.getTypes().convertType(
      LangPTy->castAs<PointerType>()->getPointeeType());

  // The vector type that is returned may be different from the
  // eventual type loaded from memory.
  auto VectorTy = cast<llvm::ScalableVectorType>(ReturnTy);
  llvm::ScalableVectorType *MemoryTy = nullptr;
  llvm::ScalableVectorType *PredTy = nullptr;
  bool IsQuadLoad = false;
  switch (IntrinsicID) {
  case Intrinsic::aarch64_sve_ld1uwq:
  case Intrinsic::aarch64_sve_ld1udq:
    MemoryTy = llvm::ScalableVectorType::get(MemEltTy, 1);
    PredTy = llvm::ScalableVectorType::get(
        llvm::Type::getInt1Ty(getLLVMContext()), 1);
    IsQuadLoad = true;
    break;
  default:
    MemoryTy = llvm::ScalableVectorType::get(MemEltTy, VectorTy);
    PredTy = MemoryTy;
    break;
  }

  Value *Predicate = genSVEPredicateCast(Ops[0], PredTy);
  Value *BasePtr = Ops[1];

  // Does the load have an offset?
  if (Ops.size() > 2)
    BasePtr = Builder.CreateGEP(MemoryTy, BasePtr, Ops[2]);

  Function *F = ME.getIntrinsic(IntrinsicID, IsQuadLoad ? VectorTy : MemoryTy);
  auto *Load =
      cast<llvm::Instruction>(Builder.CreateCall(F, {Predicate, BasePtr}));
  auto TBAAInfo = ME.getTBAAAccessInfo(LangPTy->getPointeeType());
  ME.decorateInstructionWithTBAA(Load, TBAAInfo);

  if (IsQuadLoad)
    return Load;

  return IsZExtReturn ? Builder.CreateZExt(Load, VectorTy)
                      : Builder.CreateSExt(Load, VectorTy);
}

Value *FunctionEmitter::genSVEMaskedStore(const CallExpr *E,
                                          llvm::SmallVectorImpl<Value *> &Ops,
                                          unsigned IntrinsicID) {
  QualType LangPTy = E->getArg(1)->getType();
  llvm::Type *MemEltTy = ME.getTypes().convertType(
      LangPTy->castAs<PointerType>()->getPointeeType());

  // The vector type that is stored may be different from the
  // eventual type stored to memory.
  auto VectorTy = cast<llvm::ScalableVectorType>(Ops.back()->getType());
  auto MemoryTy = llvm::ScalableVectorType::get(MemEltTy, VectorTy);

  auto PredTy = MemoryTy;
  auto AddrMemoryTy = MemoryTy;
  bool IsQuadStore = false;

  switch (IntrinsicID) {
  case Intrinsic::aarch64_sve_st1uwq:
  case Intrinsic::aarch64_sve_st1udq:
    AddrMemoryTy = llvm::ScalableVectorType::get(MemEltTy, 1);
    PredTy =
        llvm::ScalableVectorType::get(IntegerType::get(getLLVMContext(), 1), 1);
    IsQuadStore = true;
    break;
  default:
    break;
  }
  Value *Predicate = genSVEPredicateCast(Ops[0], PredTy);
  Value *BasePtr = Ops[1];

  // Does the store have an offset?
  if (Ops.size() == 4)
    BasePtr = Builder.CreateGEP(AddrMemoryTy, BasePtr, Ops[2]);

  // Last value is always the data
  Value *Val =
      IsQuadStore ? Ops.back() : Builder.CreateTrunc(Ops.back(), MemoryTy);

  Function *F = ME.getIntrinsic(IntrinsicID, IsQuadStore ? VectorTy : MemoryTy);
  auto *Store =
      cast<llvm::Instruction>(Builder.CreateCall(F, {Val, Predicate, BasePtr}));
  auto TBAAInfo = ME.getTBAAAccessInfo(LangPTy->getPointeeType());
  ME.decorateInstructionWithTBAA(Store, TBAAInfo);
  return Store;
}

Value *FunctionEmitter::genSMELd1St1(const SVETypeFlags &TypeFlags,
                                     llvm::SmallVectorImpl<Value *> &Ops,
                                     unsigned IntID) {
  Ops[2] = genSVEPredicateCast(
      Ops[2], getSVEVectorForElementType(sveBuiltinMemEltTy(TypeFlags)));

  llvm::SmallVector<Value *> NewOps;
  NewOps.push_back(Ops[2]);

  llvm::Value *BasePtr = Ops[3];

  // If the intrinsic contains the vnum parameter, multiply it with the vector
  // size in bytes.
  if (Ops.size() == 5) {
    Function *StreamingVectorLength =
        ME.getIntrinsic(Intrinsic::aarch64_sme_cntsb);
    llvm::Value *StreamingVectorLengthCall =
        Builder.CreateCall(StreamingVectorLength);
    llvm::Value *Mulvl =
        Builder.CreateMul(StreamingVectorLengthCall, Ops[4], "mulvl");
    // The type of the ptr parameter is void *, so use Int8Ty here.
    BasePtr = Builder.CreateGEP(Int8Ty, Ops[3], Mulvl);
  }
  NewOps.push_back(BasePtr);
  NewOps.push_back(Ops[0]);
  NewOps.push_back(Ops[1]);
  Function *F = ME.getIntrinsic(IntID);
  return Builder.CreateCall(F, NewOps);
}

Value *FunctionEmitter::genSMEReadWrite(const SVETypeFlags &TypeFlags,
                                        llvm::SmallVectorImpl<Value *> &Ops,
                                        unsigned IntID) {
  auto *VecTy = getSVEType(TypeFlags);
  Function *F = ME.getIntrinsic(IntID, VecTy);
  if (TypeFlags.isReadZA())
    Ops[1] = genSVEPredicateCast(Ops[1], VecTy);
  else if (TypeFlags.isWriteZA())
    Ops[2] = genSVEPredicateCast(Ops[2], VecTy);
  return Builder.CreateCall(F, Ops);
}

Value *FunctionEmitter::genSMEZero(const SVETypeFlags &TypeFlags,
                                   llvm::SmallVectorImpl<Value *> &Ops,
                                   unsigned IntID) {
  // svzero_za() intrinsic zeros the entire za tile and has no parameters.
  if (Ops.size() == 0)
    Ops.push_back(llvm::ConstantInt::get(Int32Ty, 255));
  Function *F = ME.getIntrinsic(IntID, {});
  return Builder.CreateCall(F, Ops);
}

Value *FunctionEmitter::genSMELdrStr(const SVETypeFlags &TypeFlags,
                                     llvm::SmallVectorImpl<Value *> &Ops,
                                     unsigned IntID) {
  if (Ops.size() == 2)
    Ops.push_back(Builder.getInt32(0));
  else
    Ops[2] = Builder.CreateIntCast(Ops[2], Int32Ty, true);
  Function *F = ME.getIntrinsic(IntID, {});
  return Builder.CreateCall(F, Ops);
}

// Limit the usage of scalable llvm IR generated by the ACLE by using the
// sve dup.x intrinsic instead of IRBuilder::CreateVectorSplat.
Value *FunctionEmitter::genSVEDupX(Value *Scalar, llvm::Type *Ty) {
  return Builder.CreateVectorSplat(
      cast<llvm::VectorType>(Ty)->getElementCount(), Scalar);
}

Value *FunctionEmitter::genSVEDupX(Value *Scalar) {
  return genSVEDupX(Scalar, getSVEVectorForElementType(Scalar->getType()));
}

Value *FunctionEmitter::genSVEReinterpret(Value *Val, llvm::Type *Ty) {
  return Builder.CreateBitCast(Val, Ty);
}

namespace {
void insertExplicitZeroOperand(CGBuilderTy &Builder, llvm::Type *Ty,
                               llvm::SmallVectorImpl<Value *> &Ops) {
  auto *SplatZero = Constant::getNullValue(Ty);
  Ops.insert(Ops.begin(), SplatZero);
}

void insertExplicitUndefOperand(CGBuilderTy &Builder, llvm::Type *Ty,
                                llvm::SmallVectorImpl<Value *> &Ops) {
  auto *SplatUndef = UndefValue::get(Ty);
  Ops.insert(Ops.begin(), SplatUndef);
}
} // namespace

llvm::SmallVector<llvm::Type *, 2>
FunctionEmitter::getSVEOverloadTypes(const SVETypeFlags &TypeFlags,
                                     llvm::Type *ResultType,
                                     llvm::ArrayRef<Value *> Ops) {
  if (TypeFlags.isOverloadNone())
    return {};

  llvm::Type *DefaultType = getSVEType(TypeFlags);

  if (TypeFlags.isOverloadWhile())
    return {DefaultType, Ops[1]->getType()};

  if (TypeFlags.isOverloadWhileRW())
    return {getSVEPredType(TypeFlags), Ops[0]->getType()};

  if (TypeFlags.isOverloadCvt())
    return {Ops[0]->getType(), Ops.back()->getType()};

  if (TypeFlags.isReductionQV() && !ResultType->isScalableTy() &&
      ResultType->isVectorTy())
    return {ResultType, Ops[1]->getType()};

  assert(TypeFlags.isOverloadDefault() && "Unexpected value for overloads");
  return {DefaultType};
}

Value *FunctionEmitter::genSVETupleSetOrGet(const SVETypeFlags &TypeFlags,
                                            llvm::Type *Ty,
                                            llvm::ArrayRef<Value *> Ops) {
  assert((TypeFlags.isTupleSet() || TypeFlags.isTupleGet()) &&
         "Expects TypleFlag isTupleSet or TypeFlags.isTupleSet()");

  unsigned I = cast<ConstantInt>(Ops[1])->getSExtValue();
  auto *SingleVecTy = dyn_cast<llvm::ScalableVectorType>(
      TypeFlags.isTupleSet() ? Ops[2]->getType() : Ty);
  Value *Idx =
      ConstantInt::get(ME.Int64Ty, I * SingleVecTy->getMinNumElements());

  if (TypeFlags.isTupleSet())
    return Builder.CreateInsertVector(Ty, Ops[0], Ops[2], Idx);
  return Builder.CreateExtractVector(Ty, Ops[0], Idx);
}

Value *FunctionEmitter::genSVETupleCreate(const SVETypeFlags &TypeFlags,
                                          llvm::Type *Ty,
                                          llvm::ArrayRef<Value *> Ops) {
  assert(TypeFlags.isTupleCreate() && "Expects TypleFlag isTupleCreate");

  auto *SrcTy = dyn_cast<llvm::ScalableVectorType>(Ops[0]->getType());
  unsigned MinElts = SrcTy->getMinNumElements();
  Value *Call = llvm::PoisonValue::get(Ty);
  for (unsigned I = 0; I < Ops.size(); I++) {
    Value *Idx = ConstantInt::get(ME.Int64Ty, I * MinElts);
    Call = Builder.CreateInsertVector(Ty, Call, Ops[I], Idx);
  }

  return Call;
}

Value *FunctionEmitter::formSVEBuiltinResult(Value *Call) {
  // Multi-vector results should be broken up into a single (wide) result
  // vector.
  auto *StructTy = dyn_cast<StructType>(Call->getType());
  if (!StructTy)
    return Call;

  auto *VTy = dyn_cast<ScalableVectorType>(StructTy->getTypeAtIndex(0U));
  if (!VTy)
    return Call;
  unsigned N = StructTy->getNumElements();

  // We may need to emit a cast to a svbool_t
  bool IsPredTy = VTy->getElementType()->isIntegerTy(1);
  unsigned MinElts = IsPredTy ? 16 : VTy->getMinNumElements();

  ScalableVectorType *WideVTy =
      ScalableVectorType::get(VTy->getElementType(), MinElts * N);
  Value *Ret = llvm::PoisonValue::get(WideVTy);
  for (unsigned I = 0; I < N; ++I) {
    Value *SRet = Builder.CreateExtractValue(Call, I);
    assert(SRet->getType() == VTy && "Unexpected type for result value");
    Value *Idx = ConstantInt::get(ME.Int64Ty, I * MinElts);

    if (IsPredTy)
      SRet = genSVEPredicateCast(
          SRet, ScalableVectorType::get(Builder.getInt1Ty(), 16));

    Ret = Builder.CreateInsertVector(WideVTy, Ret, SRet, Idx);
  }
  Call = Ret;

  return Call;
}

void FunctionEmitter::getAArch64SVEProcessedOperands(
    unsigned BuiltinID, const CallExpr *E, llvm::SmallVectorImpl<Value *> &Ops,
    SVETypeFlags TypeFlags) {
  // Find out if any arguments are required to be integer constant expressions.
  unsigned ICEArguments = 0;
  TreeContext::GetBuiltinTypeError Error;
  getContext().GetBuiltinType(BuiltinID, Error, &ICEArguments);
  assert(Error == TreeContext::GE_None && "Should not codegen an error");

  // Tuple set/get only requires one insert/extract vector, which is
  // created by genSVETupleSetOrGet.
  bool IsTupleGetOrSet = TypeFlags.isTupleSet() || TypeFlags.isTupleGet();

  for (unsigned i = 0, e = E->getNumArgs(); i != e; i++) {
    bool IsICE = ICEArguments & (1 << i);
    Value *Arg = genScalarExpr(E->getArg(i));

    if (IsICE) {
      // If this is required to be a constant, constant fold it so that we know
      // that the generated intrinsic gets a ConstantInt.
      std::optional<llvm::APSInt> Result =
          E->getArg(i)->getIntegerConstantExpr(getContext());
      assert(Result && "Expected argument to be a constant");

      // Immediates for SVE llvm intrinsics are always 32bit.  We can safely
      // truncate because the immediate has been range checked and no valid
      // immediate requires more than a handful of bits.
      *Result = Result->extOrTrunc(32);
      Ops.push_back(llvm::ConstantInt::get(getLLVMContext(), *Result));
      continue;
    }

    if (IsTupleGetOrSet || !isa<ScalableVectorType>(Arg->getType())) {
      Ops.push_back(Arg);
      continue;
    }

    auto *VTy = cast<ScalableVectorType>(Arg->getType());
    unsigned MinElts = VTy->getMinNumElements();
    bool IsPred = VTy->getElementType()->isIntegerTy(1);
    unsigned N = (MinElts * VTy->getScalarSizeInBits()) / (IsPred ? 16 : 128);

    if (N == 1) {
      Ops.push_back(Arg);
      continue;
    }

    for (unsigned I = 0; I < N; ++I) {
      Value *Idx = ConstantInt::get(ME.Int64Ty, (I * MinElts) / N);
      auto *NewVTy =
          ScalableVectorType::get(VTy->getElementType(), MinElts / N);
      Ops.push_back(Builder.CreateExtractVector(NewVTy, Arg, Idx));
    }
  }
}

Value *FunctionEmitter::genAArch64SVEBuiltinExpr(unsigned BuiltinID,
                                                 const CallExpr *E) {
  llvm::Type *Ty = convertType(E->getType());
  if (BuiltinID >= SVE::BI__builtin_sve_reinterpret_s8_s8 &&
      BuiltinID <= SVE::BI__builtin_sve_reinterpret_f64_f64_x4) {
    Value *Val = genScalarExpr(E->getArg(0));
    return genSVEReinterpret(Val, Ty);
  }

  auto *Builtin = findARMVectorIntrinsicInMap(AArch64SVEIntrinsicMap, BuiltinID,
                                              AArch64SVEIntrinsicsProvenSorted);

  llvm::SmallVector<Value *, 4> Ops;
  SVETypeFlags TypeFlags(Builtin->TypeModifier);
  getAArch64SVEProcessedOperands(BuiltinID, E, Ops, TypeFlags);

  if (TypeFlags.isLoad())
    return genSVEMaskedLoad(E, Ty, Ops, Builtin->LLVMIntrinsic,
                            TypeFlags.isZExtReturn());
  else if (TypeFlags.isStore())
    return genSVEMaskedStore(E, Ops, Builtin->LLVMIntrinsic);
  else if (TypeFlags.isGatherLoad())
    return genSVEGatherLoad(TypeFlags, Ops, Builtin->LLVMIntrinsic);
  else if (TypeFlags.isScatterStore())
    return genSVEScatterStore(TypeFlags, Ops, Builtin->LLVMIntrinsic);
  else if (TypeFlags.isPrefetch())
    return genSVEPrefetchLoad(TypeFlags, Ops, Builtin->LLVMIntrinsic);
  else if (TypeFlags.isGatherPrefetch())
    return genSVEGatherPrefetch(TypeFlags, Ops, Builtin->LLVMIntrinsic);
  else if (TypeFlags.isStructLoad())
    return genSVEStructLoad(TypeFlags, Ops, Builtin->LLVMIntrinsic);
  else if (TypeFlags.isStructStore())
    return genSVEStructStore(TypeFlags, Ops, Builtin->LLVMIntrinsic);
  else if (TypeFlags.isTupleSet() || TypeFlags.isTupleGet())
    return genSVETupleSetOrGet(TypeFlags, Ty, Ops);
  else if (TypeFlags.isTupleCreate())
    return genSVETupleCreate(TypeFlags, Ty, Ops);
  else if (TypeFlags.isUndef())
    return UndefValue::get(Ty);
  else if (Builtin->LLVMIntrinsic != 0) {
    if (TypeFlags.getMergeType() == SVETypeFlags::MergeZeroExp)
      insertExplicitZeroOperand(Builder, Ty, Ops);

    if (TypeFlags.getMergeType() == SVETypeFlags::MergeAnyExp)
      insertExplicitUndefOperand(Builder, Ty, Ops);

    // Some ACLE builtins leave out the argument to specify the predicate
    // pattern, which is expected to be expanded to an SV_ALL pattern.
    if (TypeFlags.isAppendSVALL())
      Ops.push_back(Builder.getInt32(/*SV_ALL*/ 31));
    if (TypeFlags.isInsertOp1SVALL())
      Ops.insert(&Ops[1], Builder.getInt32(/*SV_ALL*/ 31));

    // Predicates must match the main datatype.
    for (unsigned i = 0, e = Ops.size(); i != e; ++i)
      if (auto PredTy = dyn_cast<llvm::VectorType>(Ops[i]->getType()))
        if (PredTy->getElementType()->isIntegerTy(1))
          Ops[i] = genSVEPredicateCast(Ops[i], getSVEType(TypeFlags));

    // Splat scalar operand to vector (intrinsics with _n infix)
    if (TypeFlags.hasSplatOperand()) {
      unsigned OpNo = TypeFlags.getSplatOperand();
      Ops[OpNo] = genSVEDupX(Ops[OpNo]);
    }

    if (TypeFlags.isReverseCompare())
      std::swap(Ops[1], Ops[2]);
    else if (TypeFlags.isReverseUSDOT())
      std::swap(Ops[1], Ops[2]);
    else if (TypeFlags.isReverseMergeAnyBinOp() &&
             TypeFlags.getMergeType() == SVETypeFlags::MergeAny)
      std::swap(Ops[1], Ops[2]);
    else if (TypeFlags.isReverseMergeAnyAccOp() &&
             TypeFlags.getMergeType() == SVETypeFlags::MergeAny)
      std::swap(Ops[1], Ops[3]);

    // Predicated intrinsics with _z suffix need a select w/ zeroinitializer.
    if (TypeFlags.getMergeType() == SVETypeFlags::MergeZero) {
      llvm::Type *OpndTy = Ops[1]->getType();
      auto *SplatZero = Constant::getNullValue(OpndTy);
      Ops[1] = Builder.CreateSelect(Ops[0], Ops[1], SplatZero);
    }

    Function *F = ME.getIntrinsic(Builtin->LLVMIntrinsic,
                                  getSVEOverloadTypes(TypeFlags, Ty, Ops));
    Value *Call = Builder.CreateCall(F, Ops);

    // Predicate results must be converted to svbool_t.
    if (auto PredTy = dyn_cast<llvm::VectorType>(Call->getType()))
      if (PredTy->getScalarType()->isIntegerTy(1))
        Call = genSVEPredicateCast(Call, cast<llvm::ScalableVectorType>(Ty));

    return formSVEBuiltinResult(Call);
  }

  switch (BuiltinID) {
  default:
    return nullptr;

  case SVE::BI__builtin_sve_svreinterpret_b: {
    auto SVCountTy =
        llvm::TargetExtType::get(getLLVMContext(), "aarch64.svcount");
    Function *CastFromSVCountF =
        ME.getIntrinsic(Intrinsic::aarch64_sve_convert_to_svbool, SVCountTy);
    return Builder.CreateCall(CastFromSVCountF, Ops[0]);
  }
  case SVE::BI__builtin_sve_svreinterpret_c: {
    auto SVCountTy =
        llvm::TargetExtType::get(getLLVMContext(), "aarch64.svcount");
    Function *CastToSVCountF =
        ME.getIntrinsic(Intrinsic::aarch64_sve_convert_from_svbool, SVCountTy);
    return Builder.CreateCall(CastToSVCountF, Ops[0]);
  }

  case SVE::BI__builtin_sve_svpsel_lane_b8:
  case SVE::BI__builtin_sve_svpsel_lane_b16:
  case SVE::BI__builtin_sve_svpsel_lane_b32:
  case SVE::BI__builtin_sve_svpsel_lane_b64:
  case SVE::BI__builtin_sve_svpsel_lane_c8:
  case SVE::BI__builtin_sve_svpsel_lane_c16:
  case SVE::BI__builtin_sve_svpsel_lane_c32:
  case SVE::BI__builtin_sve_svpsel_lane_c64: {
    bool IsSVCount = isa<TargetExtType>(Ops[0]->getType());
    assert(((!IsSVCount || cast<TargetExtType>(Ops[0]->getType())->getName() ==
                               "aarch64.svcount")) &&
           "Unexpected TargetExtType");
    auto SVCountTy =
        llvm::TargetExtType::get(getLLVMContext(), "aarch64.svcount");
    Function *CastFromSVCountF =
        ME.getIntrinsic(Intrinsic::aarch64_sve_convert_to_svbool, SVCountTy);
    Function *CastToSVCountF =
        ME.getIntrinsic(Intrinsic::aarch64_sve_convert_from_svbool, SVCountTy);

    auto OverloadedTy = getSVEType(SVETypeFlags(Builtin->TypeModifier));
    Function *F = ME.getIntrinsic(Intrinsic::aarch64_sve_psel, OverloadedTy);
    llvm::Value *Ops0 =
        IsSVCount ? Builder.CreateCall(CastFromSVCountF, Ops[0]) : Ops[0];
    llvm::Value *Ops1 = genSVEPredicateCast(Ops[1], OverloadedTy);
    llvm::Value *PSel = Builder.CreateCall(F, {Ops0, Ops1, Ops[2]});
    return IsSVCount ? Builder.CreateCall(CastToSVCountF, PSel) : PSel;
  }
  case SVE::BI__builtin_sve_svmov_b_z: {
    // svmov_b_z(pg, op) <=> svand_b_z(pg, op, op)
    SVETypeFlags TypeFlags(Builtin->TypeModifier);
    llvm::Type *OverloadedTy = getSVEType(TypeFlags);
    Function *F = ME.getIntrinsic(Intrinsic::aarch64_sve_and_z, OverloadedTy);
    return Builder.CreateCall(F, {Ops[0], Ops[1], Ops[1]});
  }

  case SVE::BI__builtin_sve_svnot_b_z: {
    // svnot_b_z(pg, op) <=> sveor_b_z(pg, op, pg)
    SVETypeFlags TypeFlags(Builtin->TypeModifier);
    llvm::Type *OverloadedTy = getSVEType(TypeFlags);
    Function *F = ME.getIntrinsic(Intrinsic::aarch64_sve_eor_z, OverloadedTy);
    return Builder.CreateCall(F, {Ops[0], Ops[1], Ops[0]});
  }

  case SVE::BI__builtin_sve_svmovlb_u16:
  case SVE::BI__builtin_sve_svmovlb_u32:
  case SVE::BI__builtin_sve_svmovlb_u64:
    return genSVEMovl(TypeFlags, Ops, Intrinsic::aarch64_sve_ushllb);

  case SVE::BI__builtin_sve_svmovlb_s16:
  case SVE::BI__builtin_sve_svmovlb_s32:
  case SVE::BI__builtin_sve_svmovlb_s64:
    return genSVEMovl(TypeFlags, Ops, Intrinsic::aarch64_sve_sshllb);

  case SVE::BI__builtin_sve_svmovlt_u16:
  case SVE::BI__builtin_sve_svmovlt_u32:
  case SVE::BI__builtin_sve_svmovlt_u64:
    return genSVEMovl(TypeFlags, Ops, Intrinsic::aarch64_sve_ushllt);

  case SVE::BI__builtin_sve_svmovlt_s16:
  case SVE::BI__builtin_sve_svmovlt_s32:
  case SVE::BI__builtin_sve_svmovlt_s64:
    return genSVEMovl(TypeFlags, Ops, Intrinsic::aarch64_sve_sshllt);

  case SVE::BI__builtin_sve_svpmullt_u16:
  case SVE::BI__builtin_sve_svpmullt_u64:
  case SVE::BI__builtin_sve_svpmullt_n_u16:
  case SVE::BI__builtin_sve_svpmullt_n_u64:
    return genSVEPMull(TypeFlags, Ops, Intrinsic::aarch64_sve_pmullt_pair);

  case SVE::BI__builtin_sve_svpmullb_u16:
  case SVE::BI__builtin_sve_svpmullb_u64:
  case SVE::BI__builtin_sve_svpmullb_n_u16:
  case SVE::BI__builtin_sve_svpmullb_n_u64:
    return genSVEPMull(TypeFlags, Ops, Intrinsic::aarch64_sve_pmullb_pair);

  case SVE::BI__builtin_sve_svdup_n_b8:
  case SVE::BI__builtin_sve_svdup_n_b16:
  case SVE::BI__builtin_sve_svdup_n_b32:
  case SVE::BI__builtin_sve_svdup_n_b64: {
    Value *CmpNE =
        Builder.CreateICmpNE(Ops[0], Constant::getNullValue(Ops[0]->getType()));
    llvm::ScalableVectorType *OverloadedTy = getSVEType(TypeFlags);
    Value *Dup = genSVEDupX(CmpNE, OverloadedTy);
    return genSVEPredicateCast(Dup, cast<llvm::ScalableVectorType>(Ty));
  }

  case SVE::BI__builtin_sve_svdupq_n_b8:
  case SVE::BI__builtin_sve_svdupq_n_b16:
  case SVE::BI__builtin_sve_svdupq_n_b32:
  case SVE::BI__builtin_sve_svdupq_n_b64:
  case SVE::BI__builtin_sve_svdupq_n_u8:
  case SVE::BI__builtin_sve_svdupq_n_s8:
  case SVE::BI__builtin_sve_svdupq_n_u64:
  case SVE::BI__builtin_sve_svdupq_n_f64:
  case SVE::BI__builtin_sve_svdupq_n_s64:
  case SVE::BI__builtin_sve_svdupq_n_u16:
  case SVE::BI__builtin_sve_svdupq_n_f16:
  case SVE::BI__builtin_sve_svdupq_n_bf16:
  case SVE::BI__builtin_sve_svdupq_n_s16:
  case SVE::BI__builtin_sve_svdupq_n_u32:
  case SVE::BI__builtin_sve_svdupq_n_f32:
  case SVE::BI__builtin_sve_svdupq_n_s32: {
    // These builtins are implemented by storing each element to an array and
    // using ld1rq to materialize a vector.
    unsigned NumOpnds = Ops.size();

    bool IsBoolTy =
        cast<llvm::VectorType>(Ty)->getElementType()->isIntegerTy(1);

    // For svdupq_n_b* the element type of is an integer of type 128/numelts,
    // so that the compare can use the width that is natural for the expected
    // number of predicate lanes.
    llvm::Type *EltTy = Ops[0]->getType();
    if (IsBoolTy)
      EltTy = IntegerType::get(getLLVMContext(), SVEBitsPerBlock / NumOpnds);

    llvm::SmallVector<llvm::Value *, 16> VecOps;
    for (unsigned I = 0; I < NumOpnds; ++I)
      VecOps.push_back(Builder.CreateZExt(Ops[I], EltTy));
    Value *Vec = formVector(VecOps);

    llvm::Type *OverloadedTy = getSVEVectorForElementType(EltTy);
    Value *InsertSubVec = Builder.CreateInsertVector(
        OverloadedTy, PoisonValue::get(OverloadedTy), Vec, Builder.getInt64(0));

    Function *F =
        ME.getIntrinsic(Intrinsic::aarch64_sve_dupq_lane, OverloadedTy);
    Value *DupQLane =
        Builder.CreateCall(F, {InsertSubVec, Builder.getInt64(0)});

    if (!IsBoolTy)
      return DupQLane;

    SVETypeFlags TypeFlags(Builtin->TypeModifier);
    Value *Pred = genSVEAllTruePred(TypeFlags);

    // For svdupq_n_b* we need to add an additional 'cmpne' with '0'.
    F = ME.getIntrinsic(NumOpnds == 2 ? Intrinsic::aarch64_sve_cmpne
                                      : Intrinsic::aarch64_sve_cmpne_wide,
                        OverloadedTy);
    Value *Call = Builder.CreateCall(
        F, {Pred, DupQLane, genSVEDupX(Builder.getInt64(0))});
    return genSVEPredicateCast(Call, cast<llvm::ScalableVectorType>(Ty));
  }

  case SVE::BI__builtin_sve_svpfalse_b:
    return ConstantInt::getFalse(Ty);

  case SVE::BI__builtin_sve_svpfalse_c: {
    auto SVBoolTy = ScalableVectorType::get(Builder.getInt1Ty(), 16);
    Function *CastToSVCountF =
        ME.getIntrinsic(Intrinsic::aarch64_sve_convert_from_svbool, Ty);
    return Builder.CreateCall(CastToSVCountF, ConstantInt::getFalse(SVBoolTy));
  }

  case SVE::BI__builtin_sve_svlen_bf16:
  case SVE::BI__builtin_sve_svlen_f16:
  case SVE::BI__builtin_sve_svlen_f32:
  case SVE::BI__builtin_sve_svlen_f64:
  case SVE::BI__builtin_sve_svlen_s8:
  case SVE::BI__builtin_sve_svlen_s16:
  case SVE::BI__builtin_sve_svlen_s32:
  case SVE::BI__builtin_sve_svlen_s64:
  case SVE::BI__builtin_sve_svlen_u8:
  case SVE::BI__builtin_sve_svlen_u16:
  case SVE::BI__builtin_sve_svlen_u32:
  case SVE::BI__builtin_sve_svlen_u64: {
    SVETypeFlags TF(Builtin->TypeModifier);
    auto VTy = cast<llvm::VectorType>(getSVEType(TF));
    auto *NumEls =
        llvm::ConstantInt::get(Ty, VTy->getElementCount().getKnownMinValue());

    Function *F = ME.getIntrinsic(Intrinsic::vscale, Ty);
    return Builder.CreateMul(NumEls, Builder.CreateCall(F));
  }

  case SVE::BI__builtin_sve_svtbl2_u8:
  case SVE::BI__builtin_sve_svtbl2_s8:
  case SVE::BI__builtin_sve_svtbl2_u16:
  case SVE::BI__builtin_sve_svtbl2_s16:
  case SVE::BI__builtin_sve_svtbl2_u32:
  case SVE::BI__builtin_sve_svtbl2_s32:
  case SVE::BI__builtin_sve_svtbl2_u64:
  case SVE::BI__builtin_sve_svtbl2_s64:
  case SVE::BI__builtin_sve_svtbl2_f16:
  case SVE::BI__builtin_sve_svtbl2_bf16:
  case SVE::BI__builtin_sve_svtbl2_f32:
  case SVE::BI__builtin_sve_svtbl2_f64: {
    SVETypeFlags TF(Builtin->TypeModifier);
    auto VTy = cast<llvm::ScalableVectorType>(getSVEType(TF));
    Function *F = ME.getIntrinsic(Intrinsic::aarch64_sve_tbl2, VTy);
    return Builder.CreateCall(F, Ops);
  }

  case SVE::BI__builtin_sve_svset_neonq_s8:
  case SVE::BI__builtin_sve_svset_neonq_s16:
  case SVE::BI__builtin_sve_svset_neonq_s32:
  case SVE::BI__builtin_sve_svset_neonq_s64:
  case SVE::BI__builtin_sve_svset_neonq_u8:
  case SVE::BI__builtin_sve_svset_neonq_u16:
  case SVE::BI__builtin_sve_svset_neonq_u32:
  case SVE::BI__builtin_sve_svset_neonq_u64:
  case SVE::BI__builtin_sve_svset_neonq_f16:
  case SVE::BI__builtin_sve_svset_neonq_f32:
  case SVE::BI__builtin_sve_svset_neonq_f64:
  case SVE::BI__builtin_sve_svset_neonq_bf16: {
    return Builder.CreateInsertVector(Ty, Ops[0], Ops[1], Builder.getInt64(0));
  }

  case SVE::BI__builtin_sve_svget_neonq_s8:
  case SVE::BI__builtin_sve_svget_neonq_s16:
  case SVE::BI__builtin_sve_svget_neonq_s32:
  case SVE::BI__builtin_sve_svget_neonq_s64:
  case SVE::BI__builtin_sve_svget_neonq_u8:
  case SVE::BI__builtin_sve_svget_neonq_u16:
  case SVE::BI__builtin_sve_svget_neonq_u32:
  case SVE::BI__builtin_sve_svget_neonq_u64:
  case SVE::BI__builtin_sve_svget_neonq_f16:
  case SVE::BI__builtin_sve_svget_neonq_f32:
  case SVE::BI__builtin_sve_svget_neonq_f64:
  case SVE::BI__builtin_sve_svget_neonq_bf16: {
    return Builder.CreateExtractVector(Ty, Ops[0], Builder.getInt64(0));
  }

  case SVE::BI__builtin_sve_svdup_neonq_s8:
  case SVE::BI__builtin_sve_svdup_neonq_s16:
  case SVE::BI__builtin_sve_svdup_neonq_s32:
  case SVE::BI__builtin_sve_svdup_neonq_s64:
  case SVE::BI__builtin_sve_svdup_neonq_u8:
  case SVE::BI__builtin_sve_svdup_neonq_u16:
  case SVE::BI__builtin_sve_svdup_neonq_u32:
  case SVE::BI__builtin_sve_svdup_neonq_u64:
  case SVE::BI__builtin_sve_svdup_neonq_f16:
  case SVE::BI__builtin_sve_svdup_neonq_f32:
  case SVE::BI__builtin_sve_svdup_neonq_f64:
  case SVE::BI__builtin_sve_svdup_neonq_bf16: {
    Value *Insert = Builder.CreateInsertVector(Ty, PoisonValue::get(Ty), Ops[0],
                                               Builder.getInt64(0));
    return Builder.CreateIntrinsic(Intrinsic::aarch64_sve_dupq_lane, {Ty},
                                   {Insert, Builder.getInt64(0)});
  }
  }

  return nullptr;
}

namespace {
void swapCommutativeSMEOperands(unsigned BuiltinID,
                                llvm::SmallVectorImpl<Value *> &Ops) {
  unsigned MultiVec;
  switch (BuiltinID) {
  default:
    return;
  case SME::BI__builtin_sme_svsumla_za32_s8_vg4x1:
    MultiVec = 1;
    break;
  case SME::BI__builtin_sme_svsumla_za32_s8_vg4x2:
  case SME::BI__builtin_sme_svsudot_za32_s8_vg1x2:
    MultiVec = 2;
    break;
  case SME::BI__builtin_sme_svsudot_za32_s8_vg1x4:
  case SME::BI__builtin_sme_svsumla_za32_s8_vg4x4:
    MultiVec = 4;
    break;
  }

  if (MultiVec > 0)
    for (unsigned I = 0; I < MultiVec; ++I)
      std::swap(Ops[I + 1], Ops[I + 1 + MultiVec]);
}
} // namespace

Value *FunctionEmitter::genAArch64SMEBuiltinExpr(unsigned BuiltinID,
                                                 const CallExpr *E) {
  auto *Builtin = findARMVectorIntrinsicInMap(AArch64SMEIntrinsicMap, BuiltinID,
                                              AArch64SMEIntrinsicsProvenSorted);

  llvm::SmallVector<Value *, 4> Ops;
  SVETypeFlags TypeFlags(Builtin->TypeModifier);
  getAArch64SVEProcessedOperands(BuiltinID, E, Ops, TypeFlags);

  if (TypeFlags.isLoad() || TypeFlags.isStore())
    return genSMELd1St1(TypeFlags, Ops, Builtin->LLVMIntrinsic);
  else if (TypeFlags.isReadZA() || TypeFlags.isWriteZA())
    return genSMEReadWrite(TypeFlags, Ops, Builtin->LLVMIntrinsic);
  else if (BuiltinID == SME::BI__builtin_sme_svzero_mask_za ||
           BuiltinID == SME::BI__builtin_sme_svzero_za)
    return genSMEZero(TypeFlags, Ops, Builtin->LLVMIntrinsic);
  else if (BuiltinID == SME::BI__builtin_sme_svldr_vnum_za ||
           BuiltinID == SME::BI__builtin_sme_svstr_vnum_za ||
           BuiltinID == SME::BI__builtin_sme_svldr_za ||
           BuiltinID == SME::BI__builtin_sme_svstr_za)
    return genSMELdrStr(TypeFlags, Ops, Builtin->LLVMIntrinsic);

  swapCommutativeSMEOperands(BuiltinID, Ops);

  // Should not happen!
  if (Builtin->LLVMIntrinsic == 0)
    return nullptr;

  // Predicates must match the main datatype.
  for (unsigned i = 0, e = Ops.size(); i != e; ++i)
    if (auto PredTy = dyn_cast<llvm::VectorType>(Ops[i]->getType()))
      if (PredTy->getElementType()->isIntegerTy(1))
        Ops[i] = genSVEPredicateCast(Ops[i], getSVEType(TypeFlags));

  Function *F =
      TypeFlags.isOverloadNone()
          ? ME.getIntrinsic(Builtin->LLVMIntrinsic)
          : ME.getIntrinsic(Builtin->LLVMIntrinsic, {getSVEType(TypeFlags)});
  Value *Call = Builder.CreateCall(F, Ops);

  return formSVEBuiltinResult(Call);
}

Value *FunctionEmitter::genAArch64BuiltinExpr(unsigned BuiltinID,
                                              const CallExpr *E,
                                              llvm::Triple::ArchType Arch) {
  if (BuiltinID >= neverc::AArch64::FirstSVEBuiltin &&
      BuiltinID <= neverc::AArch64::LastSVEBuiltin)
    return genAArch64SVEBuiltinExpr(BuiltinID, E);

  if (BuiltinID >= neverc::AArch64::FirstSMEBuiltin &&
      BuiltinID <= neverc::AArch64::LastSMEBuiltin)
    return genAArch64SMEBuiltinExpr(BuiltinID, E);

  unsigned HintID = static_cast<unsigned>(-1);
  switch (BuiltinID) {
  default:
    break;
  case neverc::AArch64::BI__builtin_arm_nop:
    HintID = 0;
    break;
  case neverc::AArch64::BI__builtin_arm_yield:
  case neverc::AArch64::BI__yield:
    HintID = 1;
    break;
  case neverc::AArch64::BI__builtin_arm_wfe:
  case neverc::AArch64::BI__wfe:
    HintID = 2;
    break;
  case neverc::AArch64::BI__builtin_arm_wfi:
  case neverc::AArch64::BI__wfi:
    HintID = 3;
    break;
  case neverc::AArch64::BI__builtin_arm_sev:
  case neverc::AArch64::BI__sev:
    HintID = 4;
    break;
  case neverc::AArch64::BI__builtin_arm_sevl:
  case neverc::AArch64::BI__sevl:
    HintID = 5;
    break;
  }

  if (HintID != static_cast<unsigned>(-1)) {
    Function *F = ME.getIntrinsic(Intrinsic::aarch64_hint);
    return Builder.CreateCall(F, llvm::ConstantInt::get(Int32Ty, HintID));
  }

  if (BuiltinID == neverc::AArch64::BI__builtin_arm_rbit) {
    assert((getContext().getTypeSize(E->getType()) == 32) &&
           "rbit of unusual size!");
    llvm::Value *Arg = genScalarExpr(E->getArg(0));
    return Builder.CreateCall(
        ME.getIntrinsic(Intrinsic::bitreverse, Arg->getType()), Arg, "rbit");
  }
  if (BuiltinID == neverc::AArch64::BI__builtin_arm_rbit64) {
    assert((getContext().getTypeSize(E->getType()) == 64) &&
           "rbit of unusual size!");
    llvm::Value *Arg = genScalarExpr(E->getArg(0));
    return Builder.CreateCall(
        ME.getIntrinsic(Intrinsic::bitreverse, Arg->getType()), Arg, "rbit");
  }

  if (BuiltinID == neverc::AArch64::BI__builtin_arm_clz ||
      BuiltinID == neverc::AArch64::BI__builtin_arm_clz64) {
    llvm::Value *Arg = genScalarExpr(E->getArg(0));
    Function *F = ME.getIntrinsic(Intrinsic::ctlz, Arg->getType());
    Value *Res = Builder.CreateCall(F, {Arg, Builder.getInt1(false)});
    if (BuiltinID == neverc::AArch64::BI__builtin_arm_clz64)
      Res = Builder.CreateTrunc(Res, Builder.getInt32Ty());
    return Res;
  }

  if (BuiltinID == neverc::AArch64::BI__builtin_arm_cls) {
    llvm::Value *Arg = genScalarExpr(E->getArg(0));
    return Builder.CreateCall(ME.getIntrinsic(Intrinsic::aarch64_cls), Arg,
                              "cls");
  }
  if (BuiltinID == neverc::AArch64::BI__builtin_arm_cls64) {
    llvm::Value *Arg = genScalarExpr(E->getArg(0));
    return Builder.CreateCall(ME.getIntrinsic(Intrinsic::aarch64_cls64), Arg,
                              "cls");
  }

  if (BuiltinID == neverc::AArch64::BI__builtin_arm_rint32zf ||
      BuiltinID == neverc::AArch64::BI__builtin_arm_rint32z) {
    llvm::Value *Arg = genScalarExpr(E->getArg(0));
    llvm::Type *Ty = Arg->getType();
    return Builder.CreateCall(ME.getIntrinsic(Intrinsic::aarch64_frint32z, Ty),
                              Arg, "frint32z");
  }

  if (BuiltinID == neverc::AArch64::BI__builtin_arm_rint64zf ||
      BuiltinID == neverc::AArch64::BI__builtin_arm_rint64z) {
    llvm::Value *Arg = genScalarExpr(E->getArg(0));
    llvm::Type *Ty = Arg->getType();
    return Builder.CreateCall(ME.getIntrinsic(Intrinsic::aarch64_frint64z, Ty),
                              Arg, "frint64z");
  }

  if (BuiltinID == neverc::AArch64::BI__builtin_arm_rint32xf ||
      BuiltinID == neverc::AArch64::BI__builtin_arm_rint32x) {
    llvm::Value *Arg = genScalarExpr(E->getArg(0));
    llvm::Type *Ty = Arg->getType();
    return Builder.CreateCall(ME.getIntrinsic(Intrinsic::aarch64_frint32x, Ty),
                              Arg, "frint32x");
  }

  if (BuiltinID == neverc::AArch64::BI__builtin_arm_rint64xf ||
      BuiltinID == neverc::AArch64::BI__builtin_arm_rint64x) {
    llvm::Value *Arg = genScalarExpr(E->getArg(0));
    llvm::Type *Ty = Arg->getType();
    return Builder.CreateCall(ME.getIntrinsic(Intrinsic::aarch64_frint64x, Ty),
                              Arg, "frint64x");
  }

  if (BuiltinID == neverc::AArch64::BI__builtin_arm_jcvt) {
    assert((getContext().getTypeSize(E->getType()) == 32) &&
           "__jcvt of unusual size!");
    llvm::Value *Arg = genScalarExpr(E->getArg(0));
    return Builder.CreateCall(ME.getIntrinsic(Intrinsic::aarch64_fjcvtzs), Arg);
  }

  if (BuiltinID == neverc::AArch64::BI__builtin_arm_ld64b ||
      BuiltinID == neverc::AArch64::BI__builtin_arm_st64b ||
      BuiltinID == neverc::AArch64::BI__builtin_arm_st64bv ||
      BuiltinID == neverc::AArch64::BI__builtin_arm_st64bv0) {
    llvm::Value *MemAddr = genScalarExpr(E->getArg(0));
    llvm::Value *ValPtr = genScalarExpr(E->getArg(1));

    if (BuiltinID == neverc::AArch64::BI__builtin_arm_ld64b) {
      // Load from the address via an LLVM intrinsic, receiving a
      // tuple of 8 i64 words, and store each one to ValPtr.
      Function *F = ME.getIntrinsic(Intrinsic::aarch64_ld64b);
      llvm::Value *Val = Builder.CreateCall(F, MemAddr);
      llvm::Value *ToRet;
      for (size_t i = 0; i < 8; i++) {
        llvm::Value *ValOffsetPtr =
            Builder.CreateGEP(Int64Ty, ValPtr, Builder.getInt32(i));
        Address Addr =
            Address(ValOffsetPtr, Int64Ty, CharUnits::fromQuantity(8));
        ToRet = Builder.CreateStore(Builder.CreateExtractValue(Val, i), Addr);
      }
      return ToRet;
    } else {
      // Load 8 i64 words from ValPtr, and store them to the address
      // via an LLVM intrinsic.
      llvm::SmallVector<llvm::Value *, 9> Args;
      Args.push_back(MemAddr);
      for (size_t i = 0; i < 8; i++) {
        llvm::Value *ValOffsetPtr =
            Builder.CreateGEP(Int64Ty, ValPtr, Builder.getInt32(i));
        Address Addr =
            Address(ValOffsetPtr, Int64Ty, CharUnits::fromQuantity(8));
        Args.push_back(Builder.CreateLoad(Addr));
      }

      auto Intr = (BuiltinID == neverc::AArch64::BI__builtin_arm_st64b
                       ? Intrinsic::aarch64_st64b
                   : BuiltinID == neverc::AArch64::BI__builtin_arm_st64bv
                       ? Intrinsic::aarch64_st64bv
                       : Intrinsic::aarch64_st64bv0);
      Function *F = ME.getIntrinsic(Intr);
      return Builder.CreateCall(F, Args);
    }
  }

  if (BuiltinID == neverc::AArch64::BI__builtin_arm_rndr ||
      BuiltinID == neverc::AArch64::BI__builtin_arm_rndrrs) {

    auto Intr = (BuiltinID == neverc::AArch64::BI__builtin_arm_rndr
                     ? Intrinsic::aarch64_rndr
                     : Intrinsic::aarch64_rndrrs);
    Function *F = ME.getIntrinsic(Intr);
    llvm::Value *Val = Builder.CreateCall(F);
    Value *RandomValue = Builder.CreateExtractValue(Val, 0);
    Value *Status = Builder.CreateExtractValue(Val, 1);

    Address MemAddress = genPointerWithAlignment(E->getArg(0));
    Builder.CreateStore(RandomValue, MemAddress);
    Status = Builder.CreateZExt(Status, Int32Ty);
    return Status;
  }

  if (BuiltinID == neverc::AArch64::BI__clear_cache) {
    assert(E->getNumArgs() == 2 && "__clear_cache takes 2 arguments");
    const FunctionDecl *FD = E->getDirectCallee();
    Value *Ops[2];
    for (unsigned i = 0; i < 2; i++)
      Ops[i] = genScalarExpr(E->getArg(i));
    llvm::Type *Ty = ME.getTypes().convertType(FD->getType());
    llvm::FunctionType *FTy = cast<llvm::FunctionType>(Ty);
    llvm::StringRef Name = FD->getName();
    return genNounwindRuntimeCall(ME.createRuntimeFunction(FTy, Name), Ops);
  }

  if ((BuiltinID == neverc::AArch64::BI__builtin_arm_ldrex ||
       BuiltinID == neverc::AArch64::BI__builtin_arm_ldaex) &&
      getContext().getTypeSize(E->getType()) == 128) {
    Function *F =
        ME.getIntrinsic(BuiltinID == neverc::AArch64::BI__builtin_arm_ldaex
                            ? Intrinsic::aarch64_ldaxp
                            : Intrinsic::aarch64_ldxp);

    Value *LdPtr = genScalarExpr(E->getArg(0));
    Value *Val = Builder.CreateCall(F, LdPtr, "ldxp");

    Value *Val0 = Builder.CreateExtractValue(Val, 1);
    Value *Val1 = Builder.CreateExtractValue(Val, 0);
    llvm::Type *Int128Ty = llvm::IntegerType::get(getLLVMContext(), 128);
    Val0 = Builder.CreateZExt(Val0, Int128Ty);
    Val1 = Builder.CreateZExt(Val1, Int128Ty);

    Value *ShiftCst = llvm::ConstantInt::get(Int128Ty, 64);
    Val = Builder.CreateShl(Val0, ShiftCst, "shl", true /* nuw */);
    Val = Builder.CreateOr(Val, Val1);
    return Builder.CreateBitCast(Val, convertType(E->getType()));
  } else if (BuiltinID == neverc::AArch64::BI__builtin_arm_ldrex ||
             BuiltinID == neverc::AArch64::BI__builtin_arm_ldaex) {
    Value *LoadAddr = genScalarExpr(E->getArg(0));

    QualType Ty = E->getType();
    llvm::Type *RealResTy = convertType(Ty);
    llvm::Type *IntTy =
        llvm::IntegerType::get(getLLVMContext(), getContext().getTypeSize(Ty));

    Function *F =
        ME.getIntrinsic(BuiltinID == neverc::AArch64::BI__builtin_arm_ldaex
                            ? Intrinsic::aarch64_ldaxr
                            : Intrinsic::aarch64_ldxr,
                        UnqualPtrTy);
    CallInst *Val = Builder.CreateCall(F, LoadAddr, "ldxr");
    Val->addParamAttr(
        0, Attribute::get(getLLVMContext(), Attribute::ElementType, IntTy));

    if (RealResTy->isPointerTy())
      return Builder.CreateIntToPtr(Val, RealResTy);

    llvm::Type *IntResTy = llvm::IntegerType::get(
        getLLVMContext(), ME.getDataLayout().getTypeSizeInBits(RealResTy));
    return Builder.CreateBitCast(Builder.CreateTruncOrBitCast(Val, IntResTy),
                                 RealResTy);
  }

  if ((BuiltinID == neverc::AArch64::BI__builtin_arm_strex ||
       BuiltinID == neverc::AArch64::BI__builtin_arm_stlex) &&
      getContext().getTypeSize(E->getArg(0)->getType()) == 128) {
    Function *F =
        ME.getIntrinsic(BuiltinID == neverc::AArch64::BI__builtin_arm_stlex
                            ? Intrinsic::aarch64_stlxp
                            : Intrinsic::aarch64_stxp);
    llvm::Type *STy = llvm::StructType::get(Int64Ty, Int64Ty);

    Address Tmp = createMemTemp(E->getArg(0)->getType());
    genAnyExprToMem(E->getArg(0), Tmp, Qualifiers(), /*init*/ true);

    Tmp = Tmp.withElementType(STy);
    llvm::Value *Val = Builder.CreateLoad(Tmp);

    Value *Arg0 = Builder.CreateExtractValue(Val, 0);
    Value *Arg1 = Builder.CreateExtractValue(Val, 1);
    Value *StPtr = genScalarExpr(E->getArg(1));
    return Builder.CreateCall(F, {Arg0, Arg1, StPtr}, "stxp");
  }

  if (BuiltinID == neverc::AArch64::BI__builtin_arm_strex ||
      BuiltinID == neverc::AArch64::BI__builtin_arm_stlex) {
    Value *StoreVal = genScalarExpr(E->getArg(0));
    Value *StoreAddr = genScalarExpr(E->getArg(1));

    QualType Ty = E->getArg(0)->getType();
    llvm::Type *StoreTy =
        llvm::IntegerType::get(getLLVMContext(), getContext().getTypeSize(Ty));

    if (StoreVal->getType()->isPointerTy())
      StoreVal = Builder.CreatePtrToInt(StoreVal, Int64Ty);
    else {
      llvm::Type *IntTy = llvm::IntegerType::get(
          getLLVMContext(),
          ME.getDataLayout().getTypeSizeInBits(StoreVal->getType()));
      StoreVal = Builder.CreateBitCast(StoreVal, IntTy);
      StoreVal = Builder.CreateZExtOrBitCast(StoreVal, Int64Ty);
    }

    Function *F =
        ME.getIntrinsic(BuiltinID == neverc::AArch64::BI__builtin_arm_stlex
                            ? Intrinsic::aarch64_stlxr
                            : Intrinsic::aarch64_stxr,
                        StoreAddr->getType());
    CallInst *CI = Builder.CreateCall(F, {StoreVal, StoreAddr}, "stxr");
    CI->addParamAttr(
        1, Attribute::get(getLLVMContext(), Attribute::ElementType, StoreTy));
    return CI;
  }

  if (BuiltinID == neverc::AArch64::BI__getReg) {
    Expr::EvalResult Result;
    if (!E->getArg(0)->EvaluateAsInt(Result, ME.getContext()))
      llvm_unreachable("Sema will ensure that the parameter is constant");

    llvm::APSInt Value = Result.Val.getInt();
    LLVMContext &Context = ME.getLLVMContext();
    std::string Reg = Value == 31 ? "sp" : "x" + toString(Value, 10);

    llvm::Metadata *Ops[] = {llvm::MDString::get(Context, Reg)};
    llvm::MDNode *RegName = llvm::MDNode::get(Context, Ops);
    llvm::Value *Metadata = llvm::MetadataAsValue::get(Context, RegName);

    llvm::Function *F =
        ME.getIntrinsic(llvm::Intrinsic::read_register, {Int64Ty});
    return Builder.CreateCall(F, Metadata);
  }

  if (BuiltinID == neverc::AArch64::BI__break) {
    Expr::EvalResult Result;
    if (!E->getArg(0)->EvaluateAsInt(Result, ME.getContext()))
      llvm_unreachable("Sema will ensure that the parameter is constant");

    llvm::Function *F = ME.getIntrinsic(llvm::Intrinsic::aarch64_break);
    return Builder.CreateCall(F, {genScalarExpr(E->getArg(0))});
  }

  if (BuiltinID == neverc::AArch64::BI__builtin_arm_clrex) {
    Function *F = ME.getIntrinsic(Intrinsic::aarch64_clrex);
    return Builder.CreateCall(F);
  }

  if (BuiltinID == neverc::AArch64::BI_ReadWriteBarrier)
    return Builder.CreateFence(llvm::AtomicOrdering::SequentiallyConsistent,
                               llvm::SyncScope::SingleThread);

  // CRC32
  Intrinsic::ID CRCIntrinsicID = Intrinsic::not_intrinsic;
  switch (BuiltinID) {
  case neverc::AArch64::BI__builtin_arm_crc32b:
    CRCIntrinsicID = Intrinsic::aarch64_crc32b;
    break;
  case neverc::AArch64::BI__builtin_arm_crc32cb:
    CRCIntrinsicID = Intrinsic::aarch64_crc32cb;
    break;
  case neverc::AArch64::BI__builtin_arm_crc32h:
    CRCIntrinsicID = Intrinsic::aarch64_crc32h;
    break;
  case neverc::AArch64::BI__builtin_arm_crc32ch:
    CRCIntrinsicID = Intrinsic::aarch64_crc32ch;
    break;
  case neverc::AArch64::BI__builtin_arm_crc32w:
    CRCIntrinsicID = Intrinsic::aarch64_crc32w;
    break;
  case neverc::AArch64::BI__builtin_arm_crc32cw:
    CRCIntrinsicID = Intrinsic::aarch64_crc32cw;
    break;
  case neverc::AArch64::BI__builtin_arm_crc32d:
    CRCIntrinsicID = Intrinsic::aarch64_crc32x;
    break;
  case neverc::AArch64::BI__builtin_arm_crc32cd:
    CRCIntrinsicID = Intrinsic::aarch64_crc32cx;
    break;
  }

  if (CRCIntrinsicID != Intrinsic::not_intrinsic) {
    Value *Arg0 = genScalarExpr(E->getArg(0));
    Value *Arg1 = genScalarExpr(E->getArg(1));
    Function *F = ME.getIntrinsic(CRCIntrinsicID);

    llvm::Type *DataTy = F->getFunctionType()->getParamType(1);
    Arg1 = Builder.CreateZExtOrBitCast(Arg1, DataTy);

    return Builder.CreateCall(F, {Arg0, Arg1});
  }

  // Memory Operations (MOPS)
  if (BuiltinID == AArch64::BI__builtin_arm_mops_memset_tag) {
    Value *Dst = genScalarExpr(E->getArg(0));
    Value *Val = genScalarExpr(E->getArg(1));
    Value *Size = genScalarExpr(E->getArg(2));
    Dst = Builder.CreatePointerCast(Dst, Int8PtrTy);
    Val = Builder.CreateTrunc(Val, Int8Ty);
    Size = Builder.CreateIntCast(Size, Int64Ty, false);
    return Builder.CreateCall(
        ME.getIntrinsic(Intrinsic::aarch64_mops_memset_tag), {Dst, Val, Size});
  }

  // Memory Tagging Extensions (MTE) Intrinsics
  Intrinsic::ID MTEIntrinsicID = Intrinsic::not_intrinsic;
  switch (BuiltinID) {
  case neverc::AArch64::BI__builtin_arm_irg:
    MTEIntrinsicID = Intrinsic::aarch64_irg;
    break;
  case neverc::AArch64::BI__builtin_arm_addg:
    MTEIntrinsicID = Intrinsic::aarch64_addg;
    break;
  case neverc::AArch64::BI__builtin_arm_gmi:
    MTEIntrinsicID = Intrinsic::aarch64_gmi;
    break;
  case neverc::AArch64::BI__builtin_arm_ldg:
    MTEIntrinsicID = Intrinsic::aarch64_ldg;
    break;
  case neverc::AArch64::BI__builtin_arm_stg:
    MTEIntrinsicID = Intrinsic::aarch64_stg;
    break;
  case neverc::AArch64::BI__builtin_arm_subp:
    MTEIntrinsicID = Intrinsic::aarch64_subp;
    break;
  }

  if (MTEIntrinsicID != Intrinsic::not_intrinsic) {
    llvm::Type *T = convertType(E->getType());

    if (MTEIntrinsicID == Intrinsic::aarch64_irg) {
      Value *Pointer = genScalarExpr(E->getArg(0));
      Value *Mask = genScalarExpr(E->getArg(1));

      Pointer = Builder.CreatePointerCast(Pointer, Int8PtrTy);
      Mask = Builder.CreateZExt(Mask, Int64Ty);
      Value *RV =
          Builder.CreateCall(ME.getIntrinsic(MTEIntrinsicID), {Pointer, Mask});
      return Builder.CreatePointerCast(RV, T);
    }
    if (MTEIntrinsicID == Intrinsic::aarch64_addg) {
      Value *Pointer = genScalarExpr(E->getArg(0));
      Value *TagOffset = genScalarExpr(E->getArg(1));

      Pointer = Builder.CreatePointerCast(Pointer, Int8PtrTy);
      TagOffset = Builder.CreateZExt(TagOffset, Int64Ty);
      Value *RV = Builder.CreateCall(ME.getIntrinsic(MTEIntrinsicID),
                                     {Pointer, TagOffset});
      return Builder.CreatePointerCast(RV, T);
    }
    if (MTEIntrinsicID == Intrinsic::aarch64_gmi) {
      Value *Pointer = genScalarExpr(E->getArg(0));
      Value *ExcludedMask = genScalarExpr(E->getArg(1));

      ExcludedMask = Builder.CreateZExt(ExcludedMask, Int64Ty);
      Pointer = Builder.CreatePointerCast(Pointer, Int8PtrTy);
      return Builder.CreateCall(ME.getIntrinsic(MTEIntrinsicID),
                                {Pointer, ExcludedMask});
    }
    // Although it is possible to supply a different return
    // address (first arg) to this intrinsic, for now we set
    // return address same as input address.
    if (MTEIntrinsicID == Intrinsic::aarch64_ldg) {
      Value *TagAddress = genScalarExpr(E->getArg(0));
      TagAddress = Builder.CreatePointerCast(TagAddress, Int8PtrTy);
      Value *RV = Builder.CreateCall(ME.getIntrinsic(MTEIntrinsicID),
                                     {TagAddress, TagAddress});
      return Builder.CreatePointerCast(RV, T);
    }
    // Although it is possible to supply a different tag (to set)
    // to this intrinsic (as first arg), for now we supply
    // the tag that is in input address arg (common use case).
    if (MTEIntrinsicID == Intrinsic::aarch64_stg) {
      Value *TagAddress = genScalarExpr(E->getArg(0));
      TagAddress = Builder.CreatePointerCast(TagAddress, Int8PtrTy);
      return Builder.CreateCall(ME.getIntrinsic(MTEIntrinsicID),
                                {TagAddress, TagAddress});
    }
    if (MTEIntrinsicID == Intrinsic::aarch64_subp) {
      Value *PointerA = genScalarExpr(E->getArg(0));
      Value *PointerB = genScalarExpr(E->getArg(1));
      PointerA = Builder.CreatePointerCast(PointerA, Int8PtrTy);
      PointerB = Builder.CreatePointerCast(PointerB, Int8PtrTy);
      return Builder.CreateCall(ME.getIntrinsic(MTEIntrinsicID),
                                {PointerA, PointerB});
    }
  }

  if (BuiltinID == neverc::AArch64::BI__builtin_arm_rsr ||
      BuiltinID == neverc::AArch64::BI__builtin_arm_rsr64 ||
      BuiltinID == neverc::AArch64::BI__builtin_arm_rsr128 ||
      BuiltinID == neverc::AArch64::BI__builtin_arm_rsrp ||
      BuiltinID == neverc::AArch64::BI__builtin_arm_wsr ||
      BuiltinID == neverc::AArch64::BI__builtin_arm_wsr64 ||
      BuiltinID == neverc::AArch64::BI__builtin_arm_wsr128 ||
      BuiltinID == neverc::AArch64::BI__builtin_arm_wsrp) {

    SpecialRegisterAccessKind AccessKind = Write;
    if (BuiltinID == neverc::AArch64::BI__builtin_arm_rsr ||
        BuiltinID == neverc::AArch64::BI__builtin_arm_rsr64 ||
        BuiltinID == neverc::AArch64::BI__builtin_arm_rsr128 ||
        BuiltinID == neverc::AArch64::BI__builtin_arm_rsrp)
      AccessKind = VolatileRead;

    bool IsPointerBuiltin =
        BuiltinID == neverc::AArch64::BI__builtin_arm_rsrp ||
        BuiltinID == neverc::AArch64::BI__builtin_arm_wsrp;

    bool Is32Bit = BuiltinID == neverc::AArch64::BI__builtin_arm_rsr ||
                   BuiltinID == neverc::AArch64::BI__builtin_arm_wsr;

    bool Is128Bit = BuiltinID == neverc::AArch64::BI__builtin_arm_rsr128 ||
                    BuiltinID == neverc::AArch64::BI__builtin_arm_wsr128;

    llvm::Type *ValueType;
    llvm::Type *RegisterType = Int64Ty;
    if (Is32Bit) {
      ValueType = Int32Ty;
    } else if (Is128Bit) {
      llvm::Type *Int128Ty =
          llvm::IntegerType::getInt128Ty(ME.getLLVMContext());
      ValueType = Int128Ty;
      RegisterType = Int128Ty;
    } else if (IsPointerBuiltin) {
      ValueType = VoidPtrTy;
    } else {
      ValueType = Int64Ty;
    };

    return genSpecialRegisterBuiltin(*this, E, RegisterType, ValueType,
                                     AccessKind);
  }

  if (BuiltinID == neverc::AArch64::BI_ReadStatusReg ||
      BuiltinID == neverc::AArch64::BI_WriteStatusReg) {
    LLVMContext &Context = ME.getLLVMContext();

    unsigned SysReg =
        E->getArg(0)->EvaluateKnownConstInt(getContext()).getZExtValue();

    std::string SysRegStr;
    llvm::raw_string_ostream(SysRegStr)
        << ((1 << 1) | ((SysReg >> 14) & 1)) << ":" << ((SysReg >> 11) & 7)
        << ":" << ((SysReg >> 7) & 15) << ":" << ((SysReg >> 3) & 15) << ":"
        << (SysReg & 7);

    llvm::Metadata *Ops[] = {llvm::MDString::get(Context, SysRegStr)};
    llvm::MDNode *RegName = llvm::MDNode::get(Context, Ops);
    llvm::Value *Metadata = llvm::MetadataAsValue::get(Context, RegName);

    llvm::Type *RegisterType = Int64Ty;
    llvm::Type *Types[] = {RegisterType};

    if (BuiltinID == neverc::AArch64::BI_ReadStatusReg) {
      llvm::Function *F =
          ME.getIntrinsic(llvm::Intrinsic::read_register, Types);

      return Builder.CreateCall(F, Metadata);
    }

    llvm::Function *F = ME.getIntrinsic(llvm::Intrinsic::write_register, Types);
    llvm::Value *ArgValue = genScalarExpr(E->getArg(1));

    return Builder.CreateCall(F, {Metadata, ArgValue});
  }

  if (BuiltinID == neverc::AArch64::BI_AddressOfReturnAddress) {
    llvm::Function *F =
        ME.getIntrinsic(Intrinsic::addressofreturnaddress, AllocaInt8PtrTy);
    return Builder.CreateCall(F);
  }

  if (BuiltinID == neverc::AArch64::BI__builtin_sponentry) {
    llvm::Function *F = ME.getIntrinsic(Intrinsic::sponentry, AllocaInt8PtrTy);
    return Builder.CreateCall(F);
  }

  if (BuiltinID == neverc::AArch64::BI__mulh ||
      BuiltinID == neverc::AArch64::BI__umulh) {
    llvm::Type *ResType = convertType(E->getType());
    llvm::Type *Int128Ty = llvm::IntegerType::get(getLLVMContext(), 128);

    bool IsSigned = BuiltinID == neverc::AArch64::BI__mulh;
    Value *LHS =
        Builder.CreateIntCast(genScalarExpr(E->getArg(0)), Int128Ty, IsSigned);
    Value *RHS =
        Builder.CreateIntCast(genScalarExpr(E->getArg(1)), Int128Ty, IsSigned);

    Value *MulResult, *HigherBits;
    if (IsSigned) {
      MulResult = Builder.CreateNSWMul(LHS, RHS);
      HigherBits = Builder.CreateAShr(MulResult, 64);
    } else {
      MulResult = Builder.CreateNUWMul(LHS, RHS);
      HigherBits = Builder.CreateLShr(MulResult, 64);
    }
    HigherBits = Builder.CreateIntCast(HigherBits, ResType, IsSigned);

    return HigherBits;
  }

  if (BuiltinID == AArch64::BI__writex18byte ||
      BuiltinID == AArch64::BI__writex18word ||
      BuiltinID == AArch64::BI__writex18dword ||
      BuiltinID == AArch64::BI__writex18qword) {
    // Read x18 as i8*
    LLVMContext &Context = ME.getLLVMContext();
    llvm::Metadata *Ops[] = {llvm::MDString::get(Context, "x18")};
    llvm::MDNode *RegName = llvm::MDNode::get(Context, Ops);
    llvm::Value *Metadata = llvm::MetadataAsValue::get(Context, RegName);
    llvm::Function *F =
        ME.getIntrinsic(llvm::Intrinsic::read_register, {Int64Ty});
    llvm::Value *X18 = Builder.CreateCall(F, Metadata);
    X18 = Builder.CreateIntToPtr(X18, Int8PtrTy);

    Value *Offset = Builder.CreateZExt(genScalarExpr(E->getArg(0)), Int64Ty);
    Value *Ptr = Builder.CreateGEP(Int8Ty, X18, Offset);
    Value *Val = genScalarExpr(E->getArg(1));
    StoreInst *Store = Builder.CreateAlignedStore(Val, Ptr, CharUnits::One());
    return Store;
  }

  if (BuiltinID == AArch64::BI__readx18byte ||
      BuiltinID == AArch64::BI__readx18word ||
      BuiltinID == AArch64::BI__readx18dword ||
      BuiltinID == AArch64::BI__readx18qword) {
    llvm::Type *IntTy = convertType(E->getType());

    // Read x18 as i8*
    LLVMContext &Context = ME.getLLVMContext();
    llvm::Metadata *Ops[] = {llvm::MDString::get(Context, "x18")};
    llvm::MDNode *RegName = llvm::MDNode::get(Context, Ops);
    llvm::Value *Metadata = llvm::MetadataAsValue::get(Context, RegName);
    llvm::Function *F =
        ME.getIntrinsic(llvm::Intrinsic::read_register, {Int64Ty});
    llvm::Value *X18 = Builder.CreateCall(F, Metadata);
    X18 = Builder.CreateIntToPtr(X18, Int8PtrTy);

    Value *Offset = Builder.CreateZExt(genScalarExpr(E->getArg(0)), Int64Ty);
    Value *Ptr = Builder.CreateGEP(Int8Ty, X18, Offset);
    LoadInst *Load = Builder.CreateAlignedLoad(IntTy, Ptr, CharUnits::One());
    return Load;
  }

  if (BuiltinID == AArch64::BI_CopyDoubleFromInt64 ||
      BuiltinID == AArch64::BI_CopyFloatFromInt32 ||
      BuiltinID == AArch64::BI_CopyInt32FromFloat ||
      BuiltinID == AArch64::BI_CopyInt64FromDouble) {
    Value *Arg = genScalarExpr(E->getArg(0));
    llvm::Type *RetTy = convertType(E->getType());
    return Builder.CreateBitCast(Arg, RetTy);
  }

  if (BuiltinID == AArch64::BI_CountLeadingOnes ||
      BuiltinID == AArch64::BI_CountLeadingOnes64 ||
      BuiltinID == AArch64::BI_CountLeadingZeros ||
      BuiltinID == AArch64::BI_CountLeadingZeros64) {
    Value *Arg = genScalarExpr(E->getArg(0));
    llvm::Type *ArgType = Arg->getType();

    if (BuiltinID == AArch64::BI_CountLeadingOnes ||
        BuiltinID == AArch64::BI_CountLeadingOnes64)
      Arg = Builder.CreateXor(Arg, Constant::getAllOnesValue(ArgType));

    Function *F = ME.getIntrinsic(Intrinsic::ctlz, ArgType);
    Value *Result = Builder.CreateCall(F, {Arg, Builder.getInt1(false)});

    if (BuiltinID == AArch64::BI_CountLeadingOnes64 ||
        BuiltinID == AArch64::BI_CountLeadingZeros64)
      Result = Builder.CreateTrunc(Result, Builder.getInt32Ty());
    return Result;
  }

  if (BuiltinID == AArch64::BI_CountLeadingSigns ||
      BuiltinID == AArch64::BI_CountLeadingSigns64) {
    Value *Arg = genScalarExpr(E->getArg(0));

    Function *F = (BuiltinID == AArch64::BI_CountLeadingSigns)
                      ? ME.getIntrinsic(Intrinsic::aarch64_cls)
                      : ME.getIntrinsic(Intrinsic::aarch64_cls64);

    Value *Result = Builder.CreateCall(F, Arg, "cls");
    if (BuiltinID == AArch64::BI_CountLeadingSigns64)
      Result = Builder.CreateTrunc(Result, Builder.getInt32Ty());
    return Result;
  }

  if (BuiltinID == AArch64::BI_CountOneBits ||
      BuiltinID == AArch64::BI_CountOneBits64) {
    Value *ArgValue = genScalarExpr(E->getArg(0));
    llvm::Type *ArgType = ArgValue->getType();
    Function *F = ME.getIntrinsic(Intrinsic::ctpop, ArgType);

    Value *Result = Builder.CreateCall(F, ArgValue);
    if (BuiltinID == AArch64::BI_CountOneBits64)
      Result = Builder.CreateTrunc(Result, Builder.getInt32Ty());
    return Result;
  }

  if (BuiltinID == AArch64::BI__prefetch) {
    Value *Address = genScalarExpr(E->getArg(0));
    Value *RW = llvm::ConstantInt::get(Int32Ty, 0);
    Value *Locality = ConstantInt::get(Int32Ty, 3);
    Value *Data = llvm::ConstantInt::get(Int32Ty, 1);
    Function *F = ME.getIntrinsic(Intrinsic::prefetch, Address->getType());
    return Builder.CreateCall(F, {Address, RW, Locality, Data});
  }

  // Handle MSVC intrinsics before argument evaluation to prevent double
  // evaluation.
  if (std::optional<MSVCIntrin> MsvcIntId =
          translateAarch64ToMsvcIntrin(BuiltinID))
    return genMSVCBuiltinExpr(*MsvcIntId, E);

  // Some intrinsics are equivalent - if they are use the base intrinsic ID.
  auto It = llvm::find_if(NEONEquivalentIntrinsicMap, [BuiltinID](auto &P) {
    return P.first == BuiltinID;
  });
  if (It != end(NEONEquivalentIntrinsicMap))
    BuiltinID = It->second;

  // Find out if any arguments are required to be integer constant
  // expressions.
  unsigned ICEArguments = 0;
  TreeContext::GetBuiltinTypeError Error;
  getContext().GetBuiltinType(BuiltinID, Error, &ICEArguments);
  assert(Error == TreeContext::GE_None && "Should not codegen an error");

  llvm::SmallVector<Value *, 4> Ops;
  Address PtrOp0 = Address::invalid();
  for (unsigned i = 0, e = E->getNumArgs() - 1; i != e; i++) {
    if (i == 0) {
      switch (BuiltinID) {
      case NEON::BI__builtin_neon_vld1_v:
      case NEON::BI__builtin_neon_vld1q_v:
      case NEON::BI__builtin_neon_vld1_dup_v:
      case NEON::BI__builtin_neon_vld1q_dup_v:
      case NEON::BI__builtin_neon_vld1_lane_v:
      case NEON::BI__builtin_neon_vld1q_lane_v:
      case NEON::BI__builtin_neon_vst1_v:
      case NEON::BI__builtin_neon_vst1q_v:
      case NEON::BI__builtin_neon_vst1_lane_v:
      case NEON::BI__builtin_neon_vst1q_lane_v:
      case NEON::BI__builtin_neon_vldap1_lane_s64:
      case NEON::BI__builtin_neon_vldap1q_lane_s64:
      case NEON::BI__builtin_neon_vstl1_lane_s64:
      case NEON::BI__builtin_neon_vstl1q_lane_s64:
        // Get the alignment for the argument in addition to the value;
        // we'll use it later.
        PtrOp0 = genPointerWithAlignment(E->getArg(0));
        Ops.push_back(PtrOp0.getPointer());
        continue;
      }
    }
    Ops.push_back(genScalarOrConstFoldImmArg(ICEArguments, i, E));
  }

  auto SISDMap = llvm::ArrayRef(AArch64SISDIntrinsicMap);
  const ARMVectorIntrinsicInfo *Builtin = findARMVectorIntrinsicInMap(
      SISDMap, BuiltinID, AArch64SISDIntrinsicsProvenSorted);

  if (Builtin) {
    Ops.push_back(genScalarExpr(E->getArg(E->getNumArgs() - 1)));
    Value *Result = genCommonNeonSISDBuiltinExpr(*this, *Builtin, Ops, E);
    assert(Result && "SISD intrinsic should have been handled");
    return Result;
  }

  const Expr *Arg = E->getArg(E->getNumArgs() - 1);
  NeonTypeFlags Type(0);
  if (std::optional<llvm::APSInt> Result =
          Arg->getIntegerConstantExpr(getContext()))
    Type = NeonTypeFlags(Result->getZExtValue());

  bool usgn = Type.isUnsigned();
  bool quad = Type.isQuad();

  switch (BuiltinID) {
  default:
    break;
  case NEON::BI__builtin_neon_vabsh_f16:
    Ops.push_back(genScalarExpr(E->getArg(0)));
    return genNeonCall(ME.getIntrinsic(Intrinsic::fabs, HalfTy), Ops, "vabs");
  case NEON::BI__builtin_neon_vaddq_p128: {
    llvm::Type *Ty = GetNeonType(this, NeonTypeFlags::Poly128);
    Ops.push_back(genScalarExpr(E->getArg(1)));
    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[0] = Builder.CreateXor(Ops[0], Ops[1]);
    llvm::Type *Int128Ty = llvm::Type::getIntNTy(getLLVMContext(), 128);
    return Builder.CreateBitCast(Ops[0], Int128Ty);
  }
  case NEON::BI__builtin_neon_vldrq_p128: {
    llvm::Type *Int128Ty = llvm::Type::getIntNTy(getLLVMContext(), 128);
    Value *Ptr = genScalarExpr(E->getArg(0));
    return Builder.CreateAlignedLoad(Int128Ty, Ptr,
                                     CharUnits::fromQuantity(16));
  }
  case NEON::BI__builtin_neon_vstrq_p128: {
    Value *Ptr = Ops[0];
    return Builder.CreateDefaultAlignedStore(genScalarExpr(E->getArg(1)), Ptr);
  }
  case NEON::BI__builtin_neon_vcvts_f32_u32:
  case NEON::BI__builtin_neon_vcvtd_f64_u64:
    usgn = true;
    [[fallthrough]];
  case NEON::BI__builtin_neon_vcvts_f32_s32:
  case NEON::BI__builtin_neon_vcvtd_f64_s64: {
    Ops.push_back(genScalarExpr(E->getArg(0)));
    bool Is64 = Ops[0]->getType()->getPrimitiveSizeInBits() == 64;
    llvm::Type *InTy = Is64 ? Int64Ty : Int32Ty;
    llvm::Type *FTy = Is64 ? DoubleTy : FloatTy;
    Ops[0] = Builder.CreateBitCast(Ops[0], InTy);
    if (usgn)
      return Builder.CreateUIToFP(Ops[0], FTy);
    return Builder.CreateSIToFP(Ops[0], FTy);
  }
  case NEON::BI__builtin_neon_vcvth_f16_u16:
  case NEON::BI__builtin_neon_vcvth_f16_u32:
  case NEON::BI__builtin_neon_vcvth_f16_u64:
    usgn = true;
    [[fallthrough]];
  case NEON::BI__builtin_neon_vcvth_f16_s16:
  case NEON::BI__builtin_neon_vcvth_f16_s32:
  case NEON::BI__builtin_neon_vcvth_f16_s64: {
    Ops.push_back(genScalarExpr(E->getArg(0)));
    llvm::Type *FTy = HalfTy;
    llvm::Type *InTy;
    if (Ops[0]->getType()->getPrimitiveSizeInBits() == 64)
      InTy = Int64Ty;
    else if (Ops[0]->getType()->getPrimitiveSizeInBits() == 32)
      InTy = Int32Ty;
    else
      InTy = Int16Ty;
    Ops[0] = Builder.CreateBitCast(Ops[0], InTy);
    if (usgn)
      return Builder.CreateUIToFP(Ops[0], FTy);
    return Builder.CreateSIToFP(Ops[0], FTy);
  }
  case NEON::BI__builtin_neon_vcvtah_u16_f16:
  case NEON::BI__builtin_neon_vcvtmh_u16_f16:
  case NEON::BI__builtin_neon_vcvtnh_u16_f16:
  case NEON::BI__builtin_neon_vcvtph_u16_f16:
  case NEON::BI__builtin_neon_vcvth_u16_f16:
  case NEON::BI__builtin_neon_vcvtah_s16_f16:
  case NEON::BI__builtin_neon_vcvtmh_s16_f16:
  case NEON::BI__builtin_neon_vcvtnh_s16_f16:
  case NEON::BI__builtin_neon_vcvtph_s16_f16:
  case NEON::BI__builtin_neon_vcvth_s16_f16: {
    unsigned Int;
    llvm::Type *InTy = Int32Ty;
    llvm::Type *FTy = HalfTy;
    llvm::Type *Tys[2] = {InTy, FTy};
    Ops.push_back(genScalarExpr(E->getArg(0)));
    switch (BuiltinID) {
    default:
      llvm_unreachable("missing builtin ID in switch!");
    case NEON::BI__builtin_neon_vcvtah_u16_f16:
      Int = Intrinsic::aarch64_neon_fcvtau;
      break;
    case NEON::BI__builtin_neon_vcvtmh_u16_f16:
      Int = Intrinsic::aarch64_neon_fcvtmu;
      break;
    case NEON::BI__builtin_neon_vcvtnh_u16_f16:
      Int = Intrinsic::aarch64_neon_fcvtnu;
      break;
    case NEON::BI__builtin_neon_vcvtph_u16_f16:
      Int = Intrinsic::aarch64_neon_fcvtpu;
      break;
    case NEON::BI__builtin_neon_vcvth_u16_f16:
      Int = Intrinsic::aarch64_neon_fcvtzu;
      break;
    case NEON::BI__builtin_neon_vcvtah_s16_f16:
      Int = Intrinsic::aarch64_neon_fcvtas;
      break;
    case NEON::BI__builtin_neon_vcvtmh_s16_f16:
      Int = Intrinsic::aarch64_neon_fcvtms;
      break;
    case NEON::BI__builtin_neon_vcvtnh_s16_f16:
      Int = Intrinsic::aarch64_neon_fcvtns;
      break;
    case NEON::BI__builtin_neon_vcvtph_s16_f16:
      Int = Intrinsic::aarch64_neon_fcvtps;
      break;
    case NEON::BI__builtin_neon_vcvth_s16_f16:
      Int = Intrinsic::aarch64_neon_fcvtzs;
      break;
    }
    Ops[0] = genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "fcvt");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vcaleh_f16:
  case NEON::BI__builtin_neon_vcalth_f16:
  case NEON::BI__builtin_neon_vcageh_f16:
  case NEON::BI__builtin_neon_vcagth_f16: {
    unsigned Int;
    llvm::Type *InTy = Int32Ty;
    llvm::Type *FTy = HalfTy;
    llvm::Type *Tys[2] = {InTy, FTy};
    Ops.push_back(genScalarExpr(E->getArg(1)));
    switch (BuiltinID) {
    default:
      llvm_unreachable("missing builtin ID in switch!");
    case NEON::BI__builtin_neon_vcageh_f16:
      Int = Intrinsic::aarch64_neon_facge;
      break;
    case NEON::BI__builtin_neon_vcagth_f16:
      Int = Intrinsic::aarch64_neon_facgt;
      break;
    case NEON::BI__builtin_neon_vcaleh_f16:
      Int = Intrinsic::aarch64_neon_facge;
      std::swap(Ops[0], Ops[1]);
      break;
    case NEON::BI__builtin_neon_vcalth_f16:
      Int = Intrinsic::aarch64_neon_facgt;
      std::swap(Ops[0], Ops[1]);
      break;
    }
    Ops[0] = genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "facg");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vcvth_n_s16_f16:
  case NEON::BI__builtin_neon_vcvth_n_u16_f16: {
    unsigned Int;
    llvm::Type *InTy = Int32Ty;
    llvm::Type *FTy = HalfTy;
    llvm::Type *Tys[2] = {InTy, FTy};
    Ops.push_back(genScalarExpr(E->getArg(1)));
    switch (BuiltinID) {
    default:
      llvm_unreachable("missing builtin ID in switch!");
    case NEON::BI__builtin_neon_vcvth_n_s16_f16:
      Int = Intrinsic::aarch64_neon_vcvtfp2fxs;
      break;
    case NEON::BI__builtin_neon_vcvth_n_u16_f16:
      Int = Intrinsic::aarch64_neon_vcvtfp2fxu;
      break;
    }
    Ops[0] = genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "fcvth_n");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vcvth_n_f16_s16:
  case NEON::BI__builtin_neon_vcvth_n_f16_u16: {
    unsigned Int;
    llvm::Type *FTy = HalfTy;
    llvm::Type *InTy = Int32Ty;
    llvm::Type *Tys[2] = {FTy, InTy};
    Ops.push_back(genScalarExpr(E->getArg(1)));
    switch (BuiltinID) {
    default:
      llvm_unreachable("missing builtin ID in switch!");
    case NEON::BI__builtin_neon_vcvth_n_f16_s16:
      Int = Intrinsic::aarch64_neon_vcvtfxs2fp;
      Ops[0] = Builder.CreateSExt(Ops[0], InTy, "sext");
      break;
    case NEON::BI__builtin_neon_vcvth_n_f16_u16:
      Int = Intrinsic::aarch64_neon_vcvtfxu2fp;
      Ops[0] = Builder.CreateZExt(Ops[0], InTy);
      break;
    }
    return genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "fcvth_n");
  }
  case NEON::BI__builtin_neon_vpaddd_s64: {
    auto *Ty = llvm::FixedVectorType::get(Int64Ty, 2);
    Value *Vec = genScalarExpr(E->getArg(0));
    // The vector is v2f64, so make sure it's bitcast to that.
    Vec = Builder.CreateBitCast(Vec, Ty, "v2i64");
    llvm::Value *Idx0 = llvm::ConstantInt::get(SizeTy, 0);
    llvm::Value *Idx1 = llvm::ConstantInt::get(SizeTy, 1);
    Value *Op0 = Builder.CreateExtractElement(Vec, Idx0, "lane0");
    Value *Op1 = Builder.CreateExtractElement(Vec, Idx1, "lane1");
    // Pairwise addition of a v2f64 into a scalar f64.
    return Builder.CreateAdd(Op0, Op1, "vpaddd");
  }
  case NEON::BI__builtin_neon_vpaddd_f64: {
    auto *Ty = llvm::FixedVectorType::get(DoubleTy, 2);
    Value *Vec = genScalarExpr(E->getArg(0));
    // The vector is v2f64, so make sure it's bitcast to that.
    Vec = Builder.CreateBitCast(Vec, Ty, "v2f64");
    llvm::Value *Idx0 = llvm::ConstantInt::get(SizeTy, 0);
    llvm::Value *Idx1 = llvm::ConstantInt::get(SizeTy, 1);
    Value *Op0 = Builder.CreateExtractElement(Vec, Idx0, "lane0");
    Value *Op1 = Builder.CreateExtractElement(Vec, Idx1, "lane1");
    // Pairwise addition of a v2f64 into a scalar f64.
    return Builder.CreateFAdd(Op0, Op1, "vpaddd");
  }
  case NEON::BI__builtin_neon_vpadds_f32: {
    auto *Ty = llvm::FixedVectorType::get(FloatTy, 2);
    Value *Vec = genScalarExpr(E->getArg(0));
    // The vector is v2f32, so make sure it's bitcast to that.
    Vec = Builder.CreateBitCast(Vec, Ty, "v2f32");
    llvm::Value *Idx0 = llvm::ConstantInt::get(SizeTy, 0);
    llvm::Value *Idx1 = llvm::ConstantInt::get(SizeTy, 1);
    Value *Op0 = Builder.CreateExtractElement(Vec, Idx0, "lane0");
    Value *Op1 = Builder.CreateExtractElement(Vec, Idx1, "lane1");
    // Pairwise addition of a v2f32 into a scalar f32.
    return Builder.CreateFAdd(Op0, Op1, "vpaddd");
  }
  case NEON::BI__builtin_neon_vceqzd_s64:
  case NEON::BI__builtin_neon_vceqzd_f64:
  case NEON::BI__builtin_neon_vceqzs_f32:
  case NEON::BI__builtin_neon_vceqzh_f16:
    Ops.push_back(genScalarExpr(E->getArg(0)));
    return genAArch64CompareBuiltinExpr(
        Ops[0], convertType(E->getCallReturnType(getContext())),
        ICmpInst::FCMP_OEQ, ICmpInst::ICMP_EQ, "vceqz");
  case NEON::BI__builtin_neon_vcgezd_s64:
  case NEON::BI__builtin_neon_vcgezd_f64:
  case NEON::BI__builtin_neon_vcgezs_f32:
  case NEON::BI__builtin_neon_vcgezh_f16:
    Ops.push_back(genScalarExpr(E->getArg(0)));
    return genAArch64CompareBuiltinExpr(
        Ops[0], convertType(E->getCallReturnType(getContext())),
        ICmpInst::FCMP_OGE, ICmpInst::ICMP_SGE, "vcgez");
  case NEON::BI__builtin_neon_vclezd_s64:
  case NEON::BI__builtin_neon_vclezd_f64:
  case NEON::BI__builtin_neon_vclezs_f32:
  case NEON::BI__builtin_neon_vclezh_f16:
    Ops.push_back(genScalarExpr(E->getArg(0)));
    return genAArch64CompareBuiltinExpr(
        Ops[0], convertType(E->getCallReturnType(getContext())),
        ICmpInst::FCMP_OLE, ICmpInst::ICMP_SLE, "vclez");
  case NEON::BI__builtin_neon_vcgtzd_s64:
  case NEON::BI__builtin_neon_vcgtzd_f64:
  case NEON::BI__builtin_neon_vcgtzs_f32:
  case NEON::BI__builtin_neon_vcgtzh_f16:
    Ops.push_back(genScalarExpr(E->getArg(0)));
    return genAArch64CompareBuiltinExpr(
        Ops[0], convertType(E->getCallReturnType(getContext())),
        ICmpInst::FCMP_OGT, ICmpInst::ICMP_SGT, "vcgtz");
  case NEON::BI__builtin_neon_vcltzd_s64:
  case NEON::BI__builtin_neon_vcltzd_f64:
  case NEON::BI__builtin_neon_vcltzs_f32:
  case NEON::BI__builtin_neon_vcltzh_f16:
    Ops.push_back(genScalarExpr(E->getArg(0)));
    return genAArch64CompareBuiltinExpr(
        Ops[0], convertType(E->getCallReturnType(getContext())),
        ICmpInst::FCMP_OLT, ICmpInst::ICMP_SLT, "vcltz");

  case NEON::BI__builtin_neon_vceqzd_u64: {
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Ops[0] = Builder.CreateBitCast(Ops[0], Int64Ty);
    Ops[0] =
        Builder.CreateICmpEQ(Ops[0], llvm::Constant::getNullValue(Int64Ty));
    return Builder.CreateSExt(Ops[0], Int64Ty, "vceqzd");
  }
  case NEON::BI__builtin_neon_vceqd_f64:
  case NEON::BI__builtin_neon_vcled_f64:
  case NEON::BI__builtin_neon_vcltd_f64:
  case NEON::BI__builtin_neon_vcged_f64:
  case NEON::BI__builtin_neon_vcgtd_f64: {
    llvm::CmpInst::Predicate P;
    switch (BuiltinID) {
    default:
      llvm_unreachable("missing builtin ID in switch!");
    case NEON::BI__builtin_neon_vceqd_f64:
      P = llvm::FCmpInst::FCMP_OEQ;
      break;
    case NEON::BI__builtin_neon_vcled_f64:
      P = llvm::FCmpInst::FCMP_OLE;
      break;
    case NEON::BI__builtin_neon_vcltd_f64:
      P = llvm::FCmpInst::FCMP_OLT;
      break;
    case NEON::BI__builtin_neon_vcged_f64:
      P = llvm::FCmpInst::FCMP_OGE;
      break;
    case NEON::BI__builtin_neon_vcgtd_f64:
      P = llvm::FCmpInst::FCMP_OGT;
      break;
    }
    Ops.push_back(genScalarExpr(E->getArg(1)));
    Ops[0] = Builder.CreateBitCast(Ops[0], DoubleTy);
    Ops[1] = Builder.CreateBitCast(Ops[1], DoubleTy);
    if (P == llvm::FCmpInst::FCMP_OEQ)
      Ops[0] = Builder.CreateFCmp(P, Ops[0], Ops[1]);
    else
      Ops[0] = Builder.CreateFCmpS(P, Ops[0], Ops[1]);
    return Builder.CreateSExt(Ops[0], Int64Ty, "vcmpd");
  }
  case NEON::BI__builtin_neon_vceqs_f32:
  case NEON::BI__builtin_neon_vcles_f32:
  case NEON::BI__builtin_neon_vclts_f32:
  case NEON::BI__builtin_neon_vcges_f32:
  case NEON::BI__builtin_neon_vcgts_f32: {
    llvm::CmpInst::Predicate P;
    switch (BuiltinID) {
    default:
      llvm_unreachable("missing builtin ID in switch!");
    case NEON::BI__builtin_neon_vceqs_f32:
      P = llvm::FCmpInst::FCMP_OEQ;
      break;
    case NEON::BI__builtin_neon_vcles_f32:
      P = llvm::FCmpInst::FCMP_OLE;
      break;
    case NEON::BI__builtin_neon_vclts_f32:
      P = llvm::FCmpInst::FCMP_OLT;
      break;
    case NEON::BI__builtin_neon_vcges_f32:
      P = llvm::FCmpInst::FCMP_OGE;
      break;
    case NEON::BI__builtin_neon_vcgts_f32:
      P = llvm::FCmpInst::FCMP_OGT;
      break;
    }
    Ops.push_back(genScalarExpr(E->getArg(1)));
    Ops[0] = Builder.CreateBitCast(Ops[0], FloatTy);
    Ops[1] = Builder.CreateBitCast(Ops[1], FloatTy);
    if (P == llvm::FCmpInst::FCMP_OEQ)
      Ops[0] = Builder.CreateFCmp(P, Ops[0], Ops[1]);
    else
      Ops[0] = Builder.CreateFCmpS(P, Ops[0], Ops[1]);
    return Builder.CreateSExt(Ops[0], Int32Ty, "vcmpd");
  }
  case NEON::BI__builtin_neon_vceqh_f16:
  case NEON::BI__builtin_neon_vcleh_f16:
  case NEON::BI__builtin_neon_vclth_f16:
  case NEON::BI__builtin_neon_vcgeh_f16:
  case NEON::BI__builtin_neon_vcgth_f16: {
    llvm::CmpInst::Predicate P;
    switch (BuiltinID) {
    default:
      llvm_unreachable("missing builtin ID in switch!");
    case NEON::BI__builtin_neon_vceqh_f16:
      P = llvm::FCmpInst::FCMP_OEQ;
      break;
    case NEON::BI__builtin_neon_vcleh_f16:
      P = llvm::FCmpInst::FCMP_OLE;
      break;
    case NEON::BI__builtin_neon_vclth_f16:
      P = llvm::FCmpInst::FCMP_OLT;
      break;
    case NEON::BI__builtin_neon_vcgeh_f16:
      P = llvm::FCmpInst::FCMP_OGE;
      break;
    case NEON::BI__builtin_neon_vcgth_f16:
      P = llvm::FCmpInst::FCMP_OGT;
      break;
    }
    Ops.push_back(genScalarExpr(E->getArg(1)));
    Ops[0] = Builder.CreateBitCast(Ops[0], HalfTy);
    Ops[1] = Builder.CreateBitCast(Ops[1], HalfTy);
    if (P == llvm::FCmpInst::FCMP_OEQ)
      Ops[0] = Builder.CreateFCmp(P, Ops[0], Ops[1]);
    else
      Ops[0] = Builder.CreateFCmpS(P, Ops[0], Ops[1]);
    return Builder.CreateSExt(Ops[0], Int16Ty, "vcmpd");
  }
  case NEON::BI__builtin_neon_vceqd_s64:
  case NEON::BI__builtin_neon_vceqd_u64:
  case NEON::BI__builtin_neon_vcgtd_s64:
  case NEON::BI__builtin_neon_vcgtd_u64:
  case NEON::BI__builtin_neon_vcltd_s64:
  case NEON::BI__builtin_neon_vcltd_u64:
  case NEON::BI__builtin_neon_vcged_u64:
  case NEON::BI__builtin_neon_vcged_s64:
  case NEON::BI__builtin_neon_vcled_u64:
  case NEON::BI__builtin_neon_vcled_s64: {
    llvm::CmpInst::Predicate P;
    switch (BuiltinID) {
    default:
      llvm_unreachable("missing builtin ID in switch!");
    case NEON::BI__builtin_neon_vceqd_s64:
    case NEON::BI__builtin_neon_vceqd_u64:
      P = llvm::ICmpInst::ICMP_EQ;
      break;
    case NEON::BI__builtin_neon_vcgtd_s64:
      P = llvm::ICmpInst::ICMP_SGT;
      break;
    case NEON::BI__builtin_neon_vcgtd_u64:
      P = llvm::ICmpInst::ICMP_UGT;
      break;
    case NEON::BI__builtin_neon_vcltd_s64:
      P = llvm::ICmpInst::ICMP_SLT;
      break;
    case NEON::BI__builtin_neon_vcltd_u64:
      P = llvm::ICmpInst::ICMP_ULT;
      break;
    case NEON::BI__builtin_neon_vcged_u64:
      P = llvm::ICmpInst::ICMP_UGE;
      break;
    case NEON::BI__builtin_neon_vcged_s64:
      P = llvm::ICmpInst::ICMP_SGE;
      break;
    case NEON::BI__builtin_neon_vcled_u64:
      P = llvm::ICmpInst::ICMP_ULE;
      break;
    case NEON::BI__builtin_neon_vcled_s64:
      P = llvm::ICmpInst::ICMP_SLE;
      break;
    }
    Ops.push_back(genScalarExpr(E->getArg(1)));
    Ops[0] = Builder.CreateBitCast(Ops[0], Int64Ty);
    Ops[1] = Builder.CreateBitCast(Ops[1], Int64Ty);
    Ops[0] = Builder.CreateICmp(P, Ops[0], Ops[1]);
    return Builder.CreateSExt(Ops[0], Int64Ty, "vceqd");
  }
  case NEON::BI__builtin_neon_vtstd_s64:
  case NEON::BI__builtin_neon_vtstd_u64: {
    Ops.push_back(genScalarExpr(E->getArg(1)));
    Ops[0] = Builder.CreateBitCast(Ops[0], Int64Ty);
    Ops[1] = Builder.CreateBitCast(Ops[1], Int64Ty);
    Ops[0] = Builder.CreateAnd(Ops[0], Ops[1]);
    Ops[0] = Builder.CreateICmp(ICmpInst::ICMP_NE, Ops[0],
                                llvm::Constant::getNullValue(Int64Ty));
    return Builder.CreateSExt(Ops[0], Int64Ty, "vtstd");
  }
  case NEON::BI__builtin_neon_vset_lane_i8:
  case NEON::BI__builtin_neon_vset_lane_i16:
  case NEON::BI__builtin_neon_vset_lane_i32:
  case NEON::BI__builtin_neon_vset_lane_i64:
  case NEON::BI__builtin_neon_vset_lane_bf16:
  case NEON::BI__builtin_neon_vset_lane_f32:
  case NEON::BI__builtin_neon_vsetq_lane_i8:
  case NEON::BI__builtin_neon_vsetq_lane_i16:
  case NEON::BI__builtin_neon_vsetq_lane_i32:
  case NEON::BI__builtin_neon_vsetq_lane_i64:
  case NEON::BI__builtin_neon_vsetq_lane_bf16:
  case NEON::BI__builtin_neon_vsetq_lane_f32:
    Ops.push_back(genScalarExpr(E->getArg(2)));
    return Builder.CreateInsertElement(Ops[1], Ops[0], Ops[2], "vset_lane");
  case NEON::BI__builtin_neon_vset_lane_f64:
    // The vector type needs a cast for the v1f64 variant.
    Ops[1] =
        Builder.CreateBitCast(Ops[1], llvm::FixedVectorType::get(DoubleTy, 1));
    Ops.push_back(genScalarExpr(E->getArg(2)));
    return Builder.CreateInsertElement(Ops[1], Ops[0], Ops[2], "vset_lane");
  case NEON::BI__builtin_neon_vsetq_lane_f64:
    // The vector type needs a cast for the v2f64 variant.
    Ops[1] =
        Builder.CreateBitCast(Ops[1], llvm::FixedVectorType::get(DoubleTy, 2));
    Ops.push_back(genScalarExpr(E->getArg(2)));
    return Builder.CreateInsertElement(Ops[1], Ops[0], Ops[2], "vset_lane");

  case NEON::BI__builtin_neon_vget_lane_i8:
  case NEON::BI__builtin_neon_vdupb_lane_i8:
    Ops[0] =
        Builder.CreateBitCast(Ops[0], llvm::FixedVectorType::get(Int8Ty, 8));
    return Builder.CreateExtractElement(Ops[0], genScalarExpr(E->getArg(1)),
                                        "vget_lane");
  case NEON::BI__builtin_neon_vgetq_lane_i8:
  case NEON::BI__builtin_neon_vdupb_laneq_i8:
    Ops[0] =
        Builder.CreateBitCast(Ops[0], llvm::FixedVectorType::get(Int8Ty, 16));
    return Builder.CreateExtractElement(Ops[0], genScalarExpr(E->getArg(1)),
                                        "vgetq_lane");
  case NEON::BI__builtin_neon_vget_lane_i16:
  case NEON::BI__builtin_neon_vduph_lane_i16:
    Ops[0] =
        Builder.CreateBitCast(Ops[0], llvm::FixedVectorType::get(Int16Ty, 4));
    return Builder.CreateExtractElement(Ops[0], genScalarExpr(E->getArg(1)),
                                        "vget_lane");
  case NEON::BI__builtin_neon_vgetq_lane_i16:
  case NEON::BI__builtin_neon_vduph_laneq_i16:
    Ops[0] =
        Builder.CreateBitCast(Ops[0], llvm::FixedVectorType::get(Int16Ty, 8));
    return Builder.CreateExtractElement(Ops[0], genScalarExpr(E->getArg(1)),
                                        "vgetq_lane");
  case NEON::BI__builtin_neon_vget_lane_i32:
  case NEON::BI__builtin_neon_vdups_lane_i32:
    Ops[0] =
        Builder.CreateBitCast(Ops[0], llvm::FixedVectorType::get(Int32Ty, 2));
    return Builder.CreateExtractElement(Ops[0], genScalarExpr(E->getArg(1)),
                                        "vget_lane");
  case NEON::BI__builtin_neon_vdups_lane_f32:
    Ops[0] =
        Builder.CreateBitCast(Ops[0], llvm::FixedVectorType::get(FloatTy, 2));
    return Builder.CreateExtractElement(Ops[0], genScalarExpr(E->getArg(1)),
                                        "vdups_lane");
  case NEON::BI__builtin_neon_vgetq_lane_i32:
  case NEON::BI__builtin_neon_vdups_laneq_i32:
    Ops[0] =
        Builder.CreateBitCast(Ops[0], llvm::FixedVectorType::get(Int32Ty, 4));
    return Builder.CreateExtractElement(Ops[0], genScalarExpr(E->getArg(1)),
                                        "vgetq_lane");
  case NEON::BI__builtin_neon_vget_lane_i64:
  case NEON::BI__builtin_neon_vdupd_lane_i64:
    Ops[0] =
        Builder.CreateBitCast(Ops[0], llvm::FixedVectorType::get(Int64Ty, 1));
    return Builder.CreateExtractElement(Ops[0], genScalarExpr(E->getArg(1)),
                                        "vget_lane");
  case NEON::BI__builtin_neon_vdupd_lane_f64:
    Ops[0] =
        Builder.CreateBitCast(Ops[0], llvm::FixedVectorType::get(DoubleTy, 1));
    return Builder.CreateExtractElement(Ops[0], genScalarExpr(E->getArg(1)),
                                        "vdupd_lane");
  case NEON::BI__builtin_neon_vgetq_lane_i64:
  case NEON::BI__builtin_neon_vdupd_laneq_i64:
    Ops[0] =
        Builder.CreateBitCast(Ops[0], llvm::FixedVectorType::get(Int64Ty, 2));
    return Builder.CreateExtractElement(Ops[0], genScalarExpr(E->getArg(1)),
                                        "vgetq_lane");
  case NEON::BI__builtin_neon_vget_lane_f32:
    Ops[0] =
        Builder.CreateBitCast(Ops[0], llvm::FixedVectorType::get(FloatTy, 2));
    return Builder.CreateExtractElement(Ops[0], genScalarExpr(E->getArg(1)),
                                        "vget_lane");
  case NEON::BI__builtin_neon_vget_lane_f64:
    Ops[0] =
        Builder.CreateBitCast(Ops[0], llvm::FixedVectorType::get(DoubleTy, 1));
    return Builder.CreateExtractElement(Ops[0], genScalarExpr(E->getArg(1)),
                                        "vget_lane");
  case NEON::BI__builtin_neon_vgetq_lane_f32:
  case NEON::BI__builtin_neon_vdups_laneq_f32:
    Ops[0] =
        Builder.CreateBitCast(Ops[0], llvm::FixedVectorType::get(FloatTy, 4));
    return Builder.CreateExtractElement(Ops[0], genScalarExpr(E->getArg(1)),
                                        "vgetq_lane");
  case NEON::BI__builtin_neon_vgetq_lane_f64:
  case NEON::BI__builtin_neon_vdupd_laneq_f64:
    Ops[0] =
        Builder.CreateBitCast(Ops[0], llvm::FixedVectorType::get(DoubleTy, 2));
    return Builder.CreateExtractElement(Ops[0], genScalarExpr(E->getArg(1)),
                                        "vgetq_lane");
  case NEON::BI__builtin_neon_vaddh_f16:
    Ops.push_back(genScalarExpr(E->getArg(1)));
    return Builder.CreateFAdd(Ops[0], Ops[1], "vaddh");
  case NEON::BI__builtin_neon_vsubh_f16:
    Ops.push_back(genScalarExpr(E->getArg(1)));
    return Builder.CreateFSub(Ops[0], Ops[1], "vsubh");
  case NEON::BI__builtin_neon_vmulh_f16:
    Ops.push_back(genScalarExpr(E->getArg(1)));
    return Builder.CreateFMul(Ops[0], Ops[1], "vmulh");
  case NEON::BI__builtin_neon_vdivh_f16:
    Ops.push_back(genScalarExpr(E->getArg(1)));
    return Builder.CreateFDiv(Ops[0], Ops[1], "vdivh");
  case NEON::BI__builtin_neon_vfmah_f16:
    // NEON intrinsic puts accumulator first, unlike the LLVM fma.
    return emitCallMaybeConstrainedFPBuiltin(
        *this, Intrinsic::fma, Intrinsic::experimental_constrained_fma, HalfTy,
        {genScalarExpr(E->getArg(1)), genScalarExpr(E->getArg(2)), Ops[0]});
  case NEON::BI__builtin_neon_vfmsh_f16: {
    Value *Neg = Builder.CreateFNeg(genScalarExpr(E->getArg(1)), "vsubh");

    // NEON intrinsic puts accumulator first, unlike the LLVM fma.
    return emitCallMaybeConstrainedFPBuiltin(
        *this, Intrinsic::fma, Intrinsic::experimental_constrained_fma, HalfTy,
        {Neg, genScalarExpr(E->getArg(2)), Ops[0]});
  }
  case NEON::BI__builtin_neon_vaddd_s64:
  case NEON::BI__builtin_neon_vaddd_u64:
    return Builder.CreateAdd(Ops[0], genScalarExpr(E->getArg(1)), "vaddd");
  case NEON::BI__builtin_neon_vsubd_s64:
  case NEON::BI__builtin_neon_vsubd_u64:
    return Builder.CreateSub(Ops[0], genScalarExpr(E->getArg(1)), "vsubd");
  case NEON::BI__builtin_neon_vqdmlalh_s16:
  case NEON::BI__builtin_neon_vqdmlslh_s16: {
    llvm::SmallVector<Value *, 2> ProductOps;
    ProductOps.push_back(vectorWrapScalar16(Ops[1]));
    ProductOps.push_back(vectorWrapScalar16(genScalarExpr(E->getArg(2))));
    auto *VTy = llvm::FixedVectorType::get(Int32Ty, 4);
    Ops[1] = genNeonCall(ME.getIntrinsic(Intrinsic::aarch64_neon_sqdmull, VTy),
                         ProductOps, "vqdmlXl");
    Constant *CI = ConstantInt::get(SizeTy, 0);
    Ops[1] = Builder.CreateExtractElement(Ops[1], CI, "lane0");

    unsigned AccumInt = BuiltinID == NEON::BI__builtin_neon_vqdmlalh_s16
                            ? Intrinsic::aarch64_neon_sqadd
                            : Intrinsic::aarch64_neon_sqsub;
    return genNeonCall(ME.getIntrinsic(AccumInt, Int32Ty), Ops, "vqdmlXl");
  }
  case NEON::BI__builtin_neon_vqshlud_n_s64: {
    Ops.push_back(genScalarExpr(E->getArg(1)));
    Ops[1] = Builder.CreateZExt(Ops[1], Int64Ty);
    return genNeonCall(ME.getIntrinsic(Intrinsic::aarch64_neon_sqshlu, Int64Ty),
                       Ops, "vqshlu_n");
  }
  case NEON::BI__builtin_neon_vqshld_n_u64:
  case NEON::BI__builtin_neon_vqshld_n_s64: {
    unsigned Int = BuiltinID == NEON::BI__builtin_neon_vqshld_n_u64
                       ? Intrinsic::aarch64_neon_uqshl
                       : Intrinsic::aarch64_neon_sqshl;
    Ops.push_back(genScalarExpr(E->getArg(1)));
    Ops[1] = Builder.CreateZExt(Ops[1], Int64Ty);
    return genNeonCall(ME.getIntrinsic(Int, Int64Ty), Ops, "vqshl_n");
  }
  case NEON::BI__builtin_neon_vrshrd_n_u64:
  case NEON::BI__builtin_neon_vrshrd_n_s64: {
    unsigned Int = BuiltinID == NEON::BI__builtin_neon_vrshrd_n_u64
                       ? Intrinsic::aarch64_neon_urshl
                       : Intrinsic::aarch64_neon_srshl;
    Ops.push_back(genScalarExpr(E->getArg(1)));
    int SV = cast<ConstantInt>(Ops[1])->getSExtValue();
    Ops[1] = ConstantInt::get(Int64Ty, -SV);
    return genNeonCall(ME.getIntrinsic(Int, Int64Ty), Ops, "vrshr_n");
  }
  case NEON::BI__builtin_neon_vrsrad_n_u64:
  case NEON::BI__builtin_neon_vrsrad_n_s64: {
    unsigned Int = BuiltinID == NEON::BI__builtin_neon_vrsrad_n_u64
                       ? Intrinsic::aarch64_neon_urshl
                       : Intrinsic::aarch64_neon_srshl;
    Ops[1] = Builder.CreateBitCast(Ops[1], Int64Ty);
    Ops.push_back(Builder.CreateNeg(genScalarExpr(E->getArg(2))));
    Ops[1] = Builder.CreateCall(ME.getIntrinsic(Int, Int64Ty),
                                {Ops[1], Builder.CreateSExt(Ops[2], Int64Ty)});
    return Builder.CreateAdd(Ops[0], Builder.CreateBitCast(Ops[1], Int64Ty));
  }
  case NEON::BI__builtin_neon_vshld_n_s64:
  case NEON::BI__builtin_neon_vshld_n_u64: {
    llvm::ConstantInt *Amt = cast<ConstantInt>(genScalarExpr(E->getArg(1)));
    return Builder.CreateShl(
        Ops[0], ConstantInt::get(Int64Ty, Amt->getZExtValue()), "shld_n");
  }
  case NEON::BI__builtin_neon_vshrd_n_s64: {
    llvm::ConstantInt *Amt = cast<ConstantInt>(genScalarExpr(E->getArg(1)));
    return Builder.CreateAShr(
        Ops[0],
        ConstantInt::get(
            Int64Ty, std::min(static_cast<uint64_t>(63), Amt->getZExtValue())),
        "shrd_n");
  }
  case NEON::BI__builtin_neon_vshrd_n_u64: {
    llvm::ConstantInt *Amt = cast<ConstantInt>(genScalarExpr(E->getArg(1)));
    uint64_t ShiftAmt = Amt->getZExtValue();
    // Right-shifting an unsigned value by its size yields 0.
    if (ShiftAmt == 64)
      return ConstantInt::get(Int64Ty, 0);
    return Builder.CreateLShr(Ops[0], ConstantInt::get(Int64Ty, ShiftAmt),
                              "shrd_n");
  }
  case NEON::BI__builtin_neon_vsrad_n_s64: {
    llvm::ConstantInt *Amt = cast<ConstantInt>(genScalarExpr(E->getArg(2)));
    Ops[1] = Builder.CreateAShr(
        Ops[1],
        ConstantInt::get(
            Int64Ty, std::min(static_cast<uint64_t>(63), Amt->getZExtValue())),
        "shrd_n");
    return Builder.CreateAdd(Ops[0], Ops[1]);
  }
  case NEON::BI__builtin_neon_vsrad_n_u64: {
    llvm::ConstantInt *Amt = cast<ConstantInt>(genScalarExpr(E->getArg(2)));
    uint64_t ShiftAmt = Amt->getZExtValue();
    // Right-shifting an unsigned value by its size yields 0.
    // As Op + 0 = Op, return Ops[0] directly.
    if (ShiftAmt == 64)
      return Ops[0];
    Ops[1] = Builder.CreateLShr(Ops[1], ConstantInt::get(Int64Ty, ShiftAmt),
                                "shrd_n");
    return Builder.CreateAdd(Ops[0], Ops[1]);
  }
  case NEON::BI__builtin_neon_vqdmlalh_lane_s16:
  case NEON::BI__builtin_neon_vqdmlalh_laneq_s16:
  case NEON::BI__builtin_neon_vqdmlslh_lane_s16:
  case NEON::BI__builtin_neon_vqdmlslh_laneq_s16: {
    Ops[2] = Builder.CreateExtractElement(Ops[2], genScalarExpr(E->getArg(3)),
                                          "lane");
    llvm::SmallVector<Value *, 2> ProductOps;
    ProductOps.push_back(vectorWrapScalar16(Ops[1]));
    ProductOps.push_back(vectorWrapScalar16(Ops[2]));
    auto *VTy = llvm::FixedVectorType::get(Int32Ty, 4);
    Ops[1] = genNeonCall(ME.getIntrinsic(Intrinsic::aarch64_neon_sqdmull, VTy),
                         ProductOps, "vqdmlXl");
    Constant *CI = ConstantInt::get(SizeTy, 0);
    Ops[1] = Builder.CreateExtractElement(Ops[1], CI, "lane0");
    Ops.pop_back();

    unsigned AccInt = (BuiltinID == NEON::BI__builtin_neon_vqdmlalh_lane_s16 ||
                       BuiltinID == NEON::BI__builtin_neon_vqdmlalh_laneq_s16)
                          ? Intrinsic::aarch64_neon_sqadd
                          : Intrinsic::aarch64_neon_sqsub;
    return genNeonCall(ME.getIntrinsic(AccInt, Int32Ty), Ops, "vqdmlXl");
  }
  case NEON::BI__builtin_neon_vqdmlals_s32:
  case NEON::BI__builtin_neon_vqdmlsls_s32: {
    llvm::SmallVector<Value *, 2> ProductOps;
    ProductOps.push_back(Ops[1]);
    ProductOps.push_back(genScalarExpr(E->getArg(2)));
    Ops[1] =
        genNeonCall(ME.getIntrinsic(Intrinsic::aarch64_neon_sqdmulls_scalar),
                    ProductOps, "vqdmlXl");

    unsigned AccumInt = BuiltinID == NEON::BI__builtin_neon_vqdmlals_s32
                            ? Intrinsic::aarch64_neon_sqadd
                            : Intrinsic::aarch64_neon_sqsub;
    return genNeonCall(ME.getIntrinsic(AccumInt, Int64Ty), Ops, "vqdmlXl");
  }
  case NEON::BI__builtin_neon_vqdmlals_lane_s32:
  case NEON::BI__builtin_neon_vqdmlals_laneq_s32:
  case NEON::BI__builtin_neon_vqdmlsls_lane_s32:
  case NEON::BI__builtin_neon_vqdmlsls_laneq_s32: {
    Ops[2] = Builder.CreateExtractElement(Ops[2], genScalarExpr(E->getArg(3)),
                                          "lane");
    llvm::SmallVector<Value *, 2> ProductOps;
    ProductOps.push_back(Ops[1]);
    ProductOps.push_back(Ops[2]);
    Ops[1] =
        genNeonCall(ME.getIntrinsic(Intrinsic::aarch64_neon_sqdmulls_scalar),
                    ProductOps, "vqdmlXl");
    Ops.pop_back();

    unsigned AccInt = (BuiltinID == NEON::BI__builtin_neon_vqdmlals_lane_s32 ||
                       BuiltinID == NEON::BI__builtin_neon_vqdmlals_laneq_s32)
                          ? Intrinsic::aarch64_neon_sqadd
                          : Intrinsic::aarch64_neon_sqsub;
    return genNeonCall(ME.getIntrinsic(AccInt, Int64Ty), Ops, "vqdmlXl");
  }
  case NEON::BI__builtin_neon_vget_lane_bf16:
  case NEON::BI__builtin_neon_vduph_lane_bf16:
  case NEON::BI__builtin_neon_vduph_lane_f16: {
    return Builder.CreateExtractElement(Ops[0], genScalarExpr(E->getArg(1)),
                                        "vget_lane");
  }
  case NEON::BI__builtin_neon_vgetq_lane_bf16:
  case NEON::BI__builtin_neon_vduph_laneq_bf16:
  case NEON::BI__builtin_neon_vduph_laneq_f16: {
    return Builder.CreateExtractElement(Ops[0], genScalarExpr(E->getArg(1)),
                                        "vgetq_lane");
  }

  case neverc::AArch64::BI_InterlockedAdd:
  case neverc::AArch64::BI_InterlockedAdd64: {
    Address DestAddr = CheckAtomicAlignment(*this, E);
    Value *Val = genScalarExpr(E->getArg(1));
    AtomicRMWInst *RMWI =
        Builder.CreateAtomicRMW(AtomicRMWInst::Add, DestAddr, Val,
                                llvm::AtomicOrdering::SequentiallyConsistent);
    return Builder.CreateAdd(RMWI, Val);
  }
  }

  llvm::FixedVectorType *VTy = GetNeonType(this, Type);
  llvm::Type *Ty = VTy;
  if (!Ty)
    return nullptr;

  // Not all intrinsics handled by the common case work for AArch64 yet, so only
  // defer to common code if it's been added to our special map.
  Builtin = findARMVectorIntrinsicInMap(AArch64SIMDIntrinsicMap, BuiltinID,
                                        AArch64SIMDIntrinsicsProvenSorted);

  if (Builtin)
    return genCommonNeonBuiltinExpr(
        Builtin->BuiltinID, Builtin->LLVMIntrinsic, Builtin->AltLLVMIntrinsic,
        Builtin->NameHint, Builtin->TypeModifier, E, Ops,
        /*never use addresses*/ Address::invalid(), Address::invalid(), Arch);

  if (Value *V = genAArch64TblBuiltinExpr(*this, BuiltinID, E, Ops, Arch))
    return V;

  unsigned Int;
  switch (BuiltinID) {
  default:
    return nullptr;
  case NEON::BI__builtin_neon_vbsl_v:
  case NEON::BI__builtin_neon_vbslq_v: {
    llvm::Type *BitTy = llvm::VectorType::getInteger(VTy);
    Ops[0] = Builder.CreateBitCast(Ops[0], BitTy, "vbsl");
    Ops[1] = Builder.CreateBitCast(Ops[1], BitTy, "vbsl");
    Ops[2] = Builder.CreateBitCast(Ops[2], BitTy, "vbsl");

    Ops[1] = Builder.CreateAnd(Ops[0], Ops[1], "vbsl");
    Ops[2] = Builder.CreateAnd(Builder.CreateNot(Ops[0]), Ops[2], "vbsl");
    Ops[0] = Builder.CreateOr(Ops[1], Ops[2], "vbsl");
    return Builder.CreateBitCast(Ops[0], Ty);
  }
  case NEON::BI__builtin_neon_vfma_lane_v:
  case NEON::BI__builtin_neon_vfmaq_lane_v: { // Only used for FP types
    // The AArch64 builtins (and instructions) have the addend as the first
    // operand, but the 'fma' intrinsics have it last. Swap it around here.
    Value *Addend = Ops[0];
    Value *Multiplicand = Ops[1];
    Value *LaneSource = Ops[2];
    Ops[0] = Multiplicand;
    Ops[1] = LaneSource;
    Ops[2] = Addend;

    // Now adjust things to handle the lane access.
    auto *SourceTy = BuiltinID == NEON::BI__builtin_neon_vfmaq_lane_v
                         ? llvm::FixedVectorType::get(VTy->getElementType(),
                                                      VTy->getNumElements() / 2)
                         : VTy;
    llvm::Constant *cst = cast<Constant>(Ops[3]);
    Value *SV = llvm::ConstantVector::getSplat(VTy->getElementCount(), cst);
    Ops[1] = Builder.CreateBitCast(Ops[1], SourceTy);
    Ops[1] = Builder.CreateShuffleVector(Ops[1], Ops[1], SV, "lane");

    Ops.pop_back();
    Int = Builder.getIsFPConstrained() ? Intrinsic::experimental_constrained_fma
                                       : Intrinsic::fma;
    return genNeonCall(ME.getIntrinsic(Int, Ty), Ops, "fmla");
  }
  case NEON::BI__builtin_neon_vfma_laneq_v: {
    auto *VTy = cast<llvm::FixedVectorType>(Ty);
    // v1f64 fma should be mapped to Neon scalar f64 fma
    if (VTy && VTy->getElementType() == DoubleTy) {
      Ops[0] = Builder.CreateBitCast(Ops[0], DoubleTy);
      Ops[1] = Builder.CreateBitCast(Ops[1], DoubleTy);
      llvm::FixedVectorType *VTy =
          GetNeonType(this, NeonTypeFlags(NeonTypeFlags::Float64, false, true));
      Ops[2] = Builder.CreateBitCast(Ops[2], VTy);
      Ops[2] = Builder.CreateExtractElement(Ops[2], Ops[3], "extract");
      Value *Result;
      Result = emitCallMaybeConstrainedFPBuiltin(
          *this, Intrinsic::fma, Intrinsic::experimental_constrained_fma,
          DoubleTy, {Ops[1], Ops[2], Ops[0]});
      return Builder.CreateBitCast(Result, Ty);
    }
    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);

    auto *STy = llvm::FixedVectorType::get(VTy->getElementType(),
                                           VTy->getNumElements() * 2);
    Ops[2] = Builder.CreateBitCast(Ops[2], STy);
    Value *SV = llvm::ConstantVector::getSplat(VTy->getElementCount(),
                                               cast<ConstantInt>(Ops[3]));
    Ops[2] = Builder.CreateShuffleVector(Ops[2], Ops[2], SV, "lane");

    return emitCallMaybeConstrainedFPBuiltin(
        *this, Intrinsic::fma, Intrinsic::experimental_constrained_fma, Ty,
        {Ops[2], Ops[1], Ops[0]});
  }
  case NEON::BI__builtin_neon_vfmaq_laneq_v: {
    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);

    Ops[2] = Builder.CreateBitCast(Ops[2], Ty);
    Ops[2] = genNeonSplat(Ops[2], cast<ConstantInt>(Ops[3]));
    return emitCallMaybeConstrainedFPBuiltin(
        *this, Intrinsic::fma, Intrinsic::experimental_constrained_fma, Ty,
        {Ops[2], Ops[1], Ops[0]});
  }
  case NEON::BI__builtin_neon_vfmah_lane_f16:
  case NEON::BI__builtin_neon_vfmas_lane_f32:
  case NEON::BI__builtin_neon_vfmah_laneq_f16:
  case NEON::BI__builtin_neon_vfmas_laneq_f32:
  case NEON::BI__builtin_neon_vfmad_lane_f64:
  case NEON::BI__builtin_neon_vfmad_laneq_f64: {
    Ops.push_back(genScalarExpr(E->getArg(3)));
    llvm::Type *Ty = convertType(E->getCallReturnType(getContext()));
    Ops[2] = Builder.CreateExtractElement(Ops[2], Ops[3], "extract");
    return emitCallMaybeConstrainedFPBuiltin(
        *this, Intrinsic::fma, Intrinsic::experimental_constrained_fma, Ty,
        {Ops[1], Ops[2], Ops[0]});
  }
  case NEON::BI__builtin_neon_vmull_v:
    Int = usgn ? Intrinsic::aarch64_neon_umull : Intrinsic::aarch64_neon_smull;
    if (Type.isPoly())
      Int = Intrinsic::aarch64_neon_pmull;
    return genNeonCall(ME.getIntrinsic(Int, Ty), Ops, "vmull");
  case NEON::BI__builtin_neon_vmax_v:
  case NEON::BI__builtin_neon_vmaxq_v:
    Int = usgn ? Intrinsic::aarch64_neon_umax : Intrinsic::aarch64_neon_smax;
    if (Ty->isFPOrFPVectorTy())
      Int = Intrinsic::aarch64_neon_fmax;
    return genNeonCall(ME.getIntrinsic(Int, Ty), Ops, "vmax");
  case NEON::BI__builtin_neon_vmaxh_f16: {
    Ops.push_back(genScalarExpr(E->getArg(1)));
    Int = Intrinsic::aarch64_neon_fmax;
    return genNeonCall(ME.getIntrinsic(Int, HalfTy), Ops, "vmax");
  }
  case NEON::BI__builtin_neon_vmin_v:
  case NEON::BI__builtin_neon_vminq_v:
    Int = usgn ? Intrinsic::aarch64_neon_umin : Intrinsic::aarch64_neon_smin;
    if (Ty->isFPOrFPVectorTy())
      Int = Intrinsic::aarch64_neon_fmin;
    return genNeonCall(ME.getIntrinsic(Int, Ty), Ops, "vmin");
  case NEON::BI__builtin_neon_vminh_f16: {
    Ops.push_back(genScalarExpr(E->getArg(1)));
    Int = Intrinsic::aarch64_neon_fmin;
    return genNeonCall(ME.getIntrinsic(Int, HalfTy), Ops, "vmin");
  }
  case NEON::BI__builtin_neon_vabd_v:
  case NEON::BI__builtin_neon_vabdq_v:
    Int = usgn ? Intrinsic::aarch64_neon_uabd : Intrinsic::aarch64_neon_sabd;
    if (Ty->isFPOrFPVectorTy())
      Int = Intrinsic::aarch64_neon_fabd;
    return genNeonCall(ME.getIntrinsic(Int, Ty), Ops, "vabd");
  case NEON::BI__builtin_neon_vpadal_v:
  case NEON::BI__builtin_neon_vpadalq_v: {
    unsigned ArgElts = VTy->getNumElements();
    llvm::IntegerType *EltTy = cast<IntegerType>(VTy->getElementType());
    unsigned BitWidth = EltTy->getBitWidth();
    auto *ArgTy = llvm::FixedVectorType::get(
        llvm::IntegerType::get(getLLVMContext(), BitWidth / 2), 2 * ArgElts);
    llvm::Type *Tys[2] = {VTy, ArgTy};
    Int =
        usgn ? Intrinsic::aarch64_neon_uaddlp : Intrinsic::aarch64_neon_saddlp;
    llvm::SmallVector<llvm::Value *, 1> TmpOps;
    TmpOps.push_back(Ops[1]);
    Function *F = ME.getIntrinsic(Int, Tys);
    llvm::Value *tmp = genNeonCall(F, TmpOps, "vpadal");
    llvm::Value *addend = Builder.CreateBitCast(Ops[0], tmp->getType());
    return Builder.CreateAdd(tmp, addend);
  }
  case NEON::BI__builtin_neon_vpmin_v:
  case NEON::BI__builtin_neon_vpminq_v:
    Int = usgn ? Intrinsic::aarch64_neon_uminp : Intrinsic::aarch64_neon_sminp;
    if (Ty->isFPOrFPVectorTy())
      Int = Intrinsic::aarch64_neon_fminp;
    return genNeonCall(ME.getIntrinsic(Int, Ty), Ops, "vpmin");
  case NEON::BI__builtin_neon_vpmax_v:
  case NEON::BI__builtin_neon_vpmaxq_v:
    Int = usgn ? Intrinsic::aarch64_neon_umaxp : Intrinsic::aarch64_neon_smaxp;
    if (Ty->isFPOrFPVectorTy())
      Int = Intrinsic::aarch64_neon_fmaxp;
    return genNeonCall(ME.getIntrinsic(Int, Ty), Ops, "vpmax");
  case NEON::BI__builtin_neon_vminnm_v:
  case NEON::BI__builtin_neon_vminnmq_v:
    Int = Intrinsic::aarch64_neon_fminnm;
    return genNeonCall(ME.getIntrinsic(Int, Ty), Ops, "vminnm");
  case NEON::BI__builtin_neon_vminnmh_f16:
    Ops.push_back(genScalarExpr(E->getArg(1)));
    Int = Intrinsic::aarch64_neon_fminnm;
    return genNeonCall(ME.getIntrinsic(Int, HalfTy), Ops, "vminnm");
  case NEON::BI__builtin_neon_vmaxnm_v:
  case NEON::BI__builtin_neon_vmaxnmq_v:
    Int = Intrinsic::aarch64_neon_fmaxnm;
    return genNeonCall(ME.getIntrinsic(Int, Ty), Ops, "vmaxnm");
  case NEON::BI__builtin_neon_vmaxnmh_f16:
    Ops.push_back(genScalarExpr(E->getArg(1)));
    Int = Intrinsic::aarch64_neon_fmaxnm;
    return genNeonCall(ME.getIntrinsic(Int, HalfTy), Ops, "vmaxnm");
  case NEON::BI__builtin_neon_vrecpss_f32: {
    Ops.push_back(genScalarExpr(E->getArg(1)));
    return genNeonCall(ME.getIntrinsic(Intrinsic::aarch64_neon_frecps, FloatTy),
                       Ops, "vrecps");
  }
  case NEON::BI__builtin_neon_vrecpsd_f64:
    Ops.push_back(genScalarExpr(E->getArg(1)));
    return genNeonCall(
        ME.getIntrinsic(Intrinsic::aarch64_neon_frecps, DoubleTy), Ops,
        "vrecps");
  case NEON::BI__builtin_neon_vrecpsh_f16:
    Ops.push_back(genScalarExpr(E->getArg(1)));
    return genNeonCall(ME.getIntrinsic(Intrinsic::aarch64_neon_frecps, HalfTy),
                       Ops, "vrecps");
  case NEON::BI__builtin_neon_vqshrun_n_v:
    Int = Intrinsic::aarch64_neon_sqshrun;
    return genNeonCall(ME.getIntrinsic(Int, Ty), Ops, "vqshrun_n");
  case NEON::BI__builtin_neon_vqrshrun_n_v:
    Int = Intrinsic::aarch64_neon_sqrshrun;
    return genNeonCall(ME.getIntrinsic(Int, Ty), Ops, "vqrshrun_n");
  case NEON::BI__builtin_neon_vqshrn_n_v:
    Int =
        usgn ? Intrinsic::aarch64_neon_uqshrn : Intrinsic::aarch64_neon_sqshrn;
    return genNeonCall(ME.getIntrinsic(Int, Ty), Ops, "vqshrn_n");
  case NEON::BI__builtin_neon_vrshrn_n_v:
    Int = Intrinsic::aarch64_neon_rshrn;
    return genNeonCall(ME.getIntrinsic(Int, Ty), Ops, "vrshrn_n");
  case NEON::BI__builtin_neon_vqrshrn_n_v:
    Int = usgn ? Intrinsic::aarch64_neon_uqrshrn
               : Intrinsic::aarch64_neon_sqrshrn;
    return genNeonCall(ME.getIntrinsic(Int, Ty), Ops, "vqrshrn_n");
  case NEON::BI__builtin_neon_vrndah_f16: {
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Int = Builder.getIsFPConstrained()
              ? Intrinsic::experimental_constrained_round
              : Intrinsic::round;
    return genNeonCall(ME.getIntrinsic(Int, HalfTy), Ops, "vrnda");
  }
  case NEON::BI__builtin_neon_vrnda_v:
  case NEON::BI__builtin_neon_vrndaq_v: {
    Int = Builder.getIsFPConstrained()
              ? Intrinsic::experimental_constrained_round
              : Intrinsic::round;
    return genNeonCall(ME.getIntrinsic(Int, Ty), Ops, "vrnda");
  }
  case NEON::BI__builtin_neon_vrndih_f16: {
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Int = Builder.getIsFPConstrained()
              ? Intrinsic::experimental_constrained_nearbyint
              : Intrinsic::nearbyint;
    return genNeonCall(ME.getIntrinsic(Int, HalfTy), Ops, "vrndi");
  }
  case NEON::BI__builtin_neon_vrndmh_f16: {
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Int = Builder.getIsFPConstrained()
              ? Intrinsic::experimental_constrained_floor
              : Intrinsic::floor;
    return genNeonCall(ME.getIntrinsic(Int, HalfTy), Ops, "vrndm");
  }
  case NEON::BI__builtin_neon_vrndm_v:
  case NEON::BI__builtin_neon_vrndmq_v: {
    Int = Builder.getIsFPConstrained()
              ? Intrinsic::experimental_constrained_floor
              : Intrinsic::floor;
    return genNeonCall(ME.getIntrinsic(Int, Ty), Ops, "vrndm");
  }
  case NEON::BI__builtin_neon_vrndnh_f16: {
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Int = Builder.getIsFPConstrained()
              ? Intrinsic::experimental_constrained_roundeven
              : Intrinsic::roundeven;
    return genNeonCall(ME.getIntrinsic(Int, HalfTy), Ops, "vrndn");
  }
  case NEON::BI__builtin_neon_vrndn_v:
  case NEON::BI__builtin_neon_vrndnq_v: {
    Int = Builder.getIsFPConstrained()
              ? Intrinsic::experimental_constrained_roundeven
              : Intrinsic::roundeven;
    return genNeonCall(ME.getIntrinsic(Int, Ty), Ops, "vrndn");
  }
  case NEON::BI__builtin_neon_vrndns_f32: {
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Int = Builder.getIsFPConstrained()
              ? Intrinsic::experimental_constrained_roundeven
              : Intrinsic::roundeven;
    return genNeonCall(ME.getIntrinsic(Int, FloatTy), Ops, "vrndn");
  }
  case NEON::BI__builtin_neon_vrndph_f16: {
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Int = Builder.getIsFPConstrained()
              ? Intrinsic::experimental_constrained_ceil
              : Intrinsic::ceil;
    return genNeonCall(ME.getIntrinsic(Int, HalfTy), Ops, "vrndp");
  }
  case NEON::BI__builtin_neon_vrndp_v:
  case NEON::BI__builtin_neon_vrndpq_v: {
    Int = Builder.getIsFPConstrained()
              ? Intrinsic::experimental_constrained_ceil
              : Intrinsic::ceil;
    return genNeonCall(ME.getIntrinsic(Int, Ty), Ops, "vrndp");
  }
  case NEON::BI__builtin_neon_vrndxh_f16: {
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Int = Builder.getIsFPConstrained()
              ? Intrinsic::experimental_constrained_rint
              : Intrinsic::rint;
    return genNeonCall(ME.getIntrinsic(Int, HalfTy), Ops, "vrndx");
  }
  case NEON::BI__builtin_neon_vrndx_v:
  case NEON::BI__builtin_neon_vrndxq_v: {
    Int = Builder.getIsFPConstrained()
              ? Intrinsic::experimental_constrained_rint
              : Intrinsic::rint;
    return genNeonCall(ME.getIntrinsic(Int, Ty), Ops, "vrndx");
  }
  case NEON::BI__builtin_neon_vrndh_f16: {
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Int = Builder.getIsFPConstrained()
              ? Intrinsic::experimental_constrained_trunc
              : Intrinsic::trunc;
    return genNeonCall(ME.getIntrinsic(Int, HalfTy), Ops, "vrndz");
  }
  case NEON::BI__builtin_neon_vrnd32x_f32:
  case NEON::BI__builtin_neon_vrnd32xq_f32:
  case NEON::BI__builtin_neon_vrnd32x_f64:
  case NEON::BI__builtin_neon_vrnd32xq_f64: {
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Int = Intrinsic::aarch64_neon_frint32x;
    return genNeonCall(ME.getIntrinsic(Int, Ty), Ops, "vrnd32x");
  }
  case NEON::BI__builtin_neon_vrnd32z_f32:
  case NEON::BI__builtin_neon_vrnd32zq_f32:
  case NEON::BI__builtin_neon_vrnd32z_f64:
  case NEON::BI__builtin_neon_vrnd32zq_f64: {
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Int = Intrinsic::aarch64_neon_frint32z;
    return genNeonCall(ME.getIntrinsic(Int, Ty), Ops, "vrnd32z");
  }
  case NEON::BI__builtin_neon_vrnd64x_f32:
  case NEON::BI__builtin_neon_vrnd64xq_f32:
  case NEON::BI__builtin_neon_vrnd64x_f64:
  case NEON::BI__builtin_neon_vrnd64xq_f64: {
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Int = Intrinsic::aarch64_neon_frint64x;
    return genNeonCall(ME.getIntrinsic(Int, Ty), Ops, "vrnd64x");
  }
  case NEON::BI__builtin_neon_vrnd64z_f32:
  case NEON::BI__builtin_neon_vrnd64zq_f32:
  case NEON::BI__builtin_neon_vrnd64z_f64:
  case NEON::BI__builtin_neon_vrnd64zq_f64: {
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Int = Intrinsic::aarch64_neon_frint64z;
    return genNeonCall(ME.getIntrinsic(Int, Ty), Ops, "vrnd64z");
  }
  case NEON::BI__builtin_neon_vrnd_v:
  case NEON::BI__builtin_neon_vrndq_v: {
    Int = Builder.getIsFPConstrained()
              ? Intrinsic::experimental_constrained_trunc
              : Intrinsic::trunc;
    return genNeonCall(ME.getIntrinsic(Int, Ty), Ops, "vrndz");
  }
  case NEON::BI__builtin_neon_vcvt_f64_v:
  case NEON::BI__builtin_neon_vcvtq_f64_v:
    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    Ty = GetNeonType(this, NeonTypeFlags(NeonTypeFlags::Float64, false, quad));
    return usgn ? Builder.CreateUIToFP(Ops[0], Ty, "vcvt")
                : Builder.CreateSIToFP(Ops[0], Ty, "vcvt");
  case NEON::BI__builtin_neon_vcvt_f64_f32: {
    assert(Type.getEltType() == NeonTypeFlags::Float64 && quad &&
           "unexpected vcvt_f64_f32 builtin");
    NeonTypeFlags SrcFlag = NeonTypeFlags(NeonTypeFlags::Float32, false, false);
    Ops[0] = Builder.CreateBitCast(Ops[0], GetNeonType(this, SrcFlag));

    return Builder.CreateFPExt(Ops[0], Ty, "vcvt");
  }
  case NEON::BI__builtin_neon_vcvt_f32_f64: {
    assert(Type.getEltType() == NeonTypeFlags::Float32 &&
           "unexpected vcvt_f32_f64 builtin");
    NeonTypeFlags SrcFlag = NeonTypeFlags(NeonTypeFlags::Float64, false, true);
    Ops[0] = Builder.CreateBitCast(Ops[0], GetNeonType(this, SrcFlag));

    return Builder.CreateFPTrunc(Ops[0], Ty, "vcvt");
  }
  case NEON::BI__builtin_neon_vcvt_s32_v:
  case NEON::BI__builtin_neon_vcvt_u32_v:
  case NEON::BI__builtin_neon_vcvt_s64_v:
  case NEON::BI__builtin_neon_vcvt_u64_v:
  case NEON::BI__builtin_neon_vcvt_s16_f16:
  case NEON::BI__builtin_neon_vcvt_u16_f16:
  case NEON::BI__builtin_neon_vcvtq_s32_v:
  case NEON::BI__builtin_neon_vcvtq_u32_v:
  case NEON::BI__builtin_neon_vcvtq_s64_v:
  case NEON::BI__builtin_neon_vcvtq_u64_v:
  case NEON::BI__builtin_neon_vcvtq_s16_f16:
  case NEON::BI__builtin_neon_vcvtq_u16_f16: {
    Int =
        usgn ? Intrinsic::aarch64_neon_fcvtzu : Intrinsic::aarch64_neon_fcvtzs;
    llvm::Type *Tys[2] = {Ty, GetFloatNeonType(this, Type)};
    return genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vcvtz");
  }
  case NEON::BI__builtin_neon_vcvta_s16_f16:
  case NEON::BI__builtin_neon_vcvta_u16_f16:
  case NEON::BI__builtin_neon_vcvta_s32_v:
  case NEON::BI__builtin_neon_vcvtaq_s16_f16:
  case NEON::BI__builtin_neon_vcvtaq_s32_v:
  case NEON::BI__builtin_neon_vcvta_u32_v:
  case NEON::BI__builtin_neon_vcvtaq_u16_f16:
  case NEON::BI__builtin_neon_vcvtaq_u32_v:
  case NEON::BI__builtin_neon_vcvta_s64_v:
  case NEON::BI__builtin_neon_vcvtaq_s64_v:
  case NEON::BI__builtin_neon_vcvta_u64_v:
  case NEON::BI__builtin_neon_vcvtaq_u64_v: {
    Int =
        usgn ? Intrinsic::aarch64_neon_fcvtau : Intrinsic::aarch64_neon_fcvtas;
    llvm::Type *Tys[2] = {Ty, GetFloatNeonType(this, Type)};
    return genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vcvta");
  }
  case NEON::BI__builtin_neon_vcvtm_s16_f16:
  case NEON::BI__builtin_neon_vcvtm_s32_v:
  case NEON::BI__builtin_neon_vcvtmq_s16_f16:
  case NEON::BI__builtin_neon_vcvtmq_s32_v:
  case NEON::BI__builtin_neon_vcvtm_u16_f16:
  case NEON::BI__builtin_neon_vcvtm_u32_v:
  case NEON::BI__builtin_neon_vcvtmq_u16_f16:
  case NEON::BI__builtin_neon_vcvtmq_u32_v:
  case NEON::BI__builtin_neon_vcvtm_s64_v:
  case NEON::BI__builtin_neon_vcvtmq_s64_v:
  case NEON::BI__builtin_neon_vcvtm_u64_v:
  case NEON::BI__builtin_neon_vcvtmq_u64_v: {
    Int =
        usgn ? Intrinsic::aarch64_neon_fcvtmu : Intrinsic::aarch64_neon_fcvtms;
    llvm::Type *Tys[2] = {Ty, GetFloatNeonType(this, Type)};
    return genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vcvtm");
  }
  case NEON::BI__builtin_neon_vcvtn_s16_f16:
  case NEON::BI__builtin_neon_vcvtn_s32_v:
  case NEON::BI__builtin_neon_vcvtnq_s16_f16:
  case NEON::BI__builtin_neon_vcvtnq_s32_v:
  case NEON::BI__builtin_neon_vcvtn_u16_f16:
  case NEON::BI__builtin_neon_vcvtn_u32_v:
  case NEON::BI__builtin_neon_vcvtnq_u16_f16:
  case NEON::BI__builtin_neon_vcvtnq_u32_v:
  case NEON::BI__builtin_neon_vcvtn_s64_v:
  case NEON::BI__builtin_neon_vcvtnq_s64_v:
  case NEON::BI__builtin_neon_vcvtn_u64_v:
  case NEON::BI__builtin_neon_vcvtnq_u64_v: {
    Int =
        usgn ? Intrinsic::aarch64_neon_fcvtnu : Intrinsic::aarch64_neon_fcvtns;
    llvm::Type *Tys[2] = {Ty, GetFloatNeonType(this, Type)};
    return genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vcvtn");
  }
  case NEON::BI__builtin_neon_vcvtp_s16_f16:
  case NEON::BI__builtin_neon_vcvtp_s32_v:
  case NEON::BI__builtin_neon_vcvtpq_s16_f16:
  case NEON::BI__builtin_neon_vcvtpq_s32_v:
  case NEON::BI__builtin_neon_vcvtp_u16_f16:
  case NEON::BI__builtin_neon_vcvtp_u32_v:
  case NEON::BI__builtin_neon_vcvtpq_u16_f16:
  case NEON::BI__builtin_neon_vcvtpq_u32_v:
  case NEON::BI__builtin_neon_vcvtp_s64_v:
  case NEON::BI__builtin_neon_vcvtpq_s64_v:
  case NEON::BI__builtin_neon_vcvtp_u64_v:
  case NEON::BI__builtin_neon_vcvtpq_u64_v: {
    Int =
        usgn ? Intrinsic::aarch64_neon_fcvtpu : Intrinsic::aarch64_neon_fcvtps;
    llvm::Type *Tys[2] = {Ty, GetFloatNeonType(this, Type)};
    return genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vcvtp");
  }
  case NEON::BI__builtin_neon_vmulx_v:
  case NEON::BI__builtin_neon_vmulxq_v: {
    Int = Intrinsic::aarch64_neon_fmulx;
    return genNeonCall(ME.getIntrinsic(Int, Ty), Ops, "vmulx");
  }
  case NEON::BI__builtin_neon_vmulxh_lane_f16:
  case NEON::BI__builtin_neon_vmulxh_laneq_f16: {
    // vmulx_lane should be mapped to Neon scalar mulx after
    // extracting the scalar element
    Ops.push_back(genScalarExpr(E->getArg(2)));
    Ops[1] = Builder.CreateExtractElement(Ops[1], Ops[2], "extract");
    Ops.pop_back();
    Int = Intrinsic::aarch64_neon_fmulx;
    return genNeonCall(ME.getIntrinsic(Int, HalfTy), Ops, "vmulx");
  }
  case NEON::BI__builtin_neon_vmul_lane_v:
  case NEON::BI__builtin_neon_vmul_laneq_v: {
    // v1f64 vmul_lane should be mapped to Neon scalar mul lane
    bool Quad = false;
    if (BuiltinID == NEON::BI__builtin_neon_vmul_laneq_v)
      Quad = true;
    Ops[0] = Builder.CreateBitCast(Ops[0], DoubleTy);
    llvm::FixedVectorType *VTy =
        GetNeonType(this, NeonTypeFlags(NeonTypeFlags::Float64, false, Quad));
    Ops[1] = Builder.CreateBitCast(Ops[1], VTy);
    Ops[1] = Builder.CreateExtractElement(Ops[1], Ops[2], "extract");
    Value *Result = Builder.CreateFMul(Ops[0], Ops[1]);
    return Builder.CreateBitCast(Result, Ty);
  }
  case NEON::BI__builtin_neon_vnegd_s64:
    return Builder.CreateNeg(genScalarExpr(E->getArg(0)), "vnegd");
  case NEON::BI__builtin_neon_vnegh_f16:
    return Builder.CreateFNeg(genScalarExpr(E->getArg(0)), "vnegh");
  case NEON::BI__builtin_neon_vpmaxnm_v:
  case NEON::BI__builtin_neon_vpmaxnmq_v: {
    Int = Intrinsic::aarch64_neon_fmaxnmp;
    return genNeonCall(ME.getIntrinsic(Int, Ty), Ops, "vpmaxnm");
  }
  case NEON::BI__builtin_neon_vpminnm_v:
  case NEON::BI__builtin_neon_vpminnmq_v: {
    Int = Intrinsic::aarch64_neon_fminnmp;
    return genNeonCall(ME.getIntrinsic(Int, Ty), Ops, "vpminnm");
  }
  case NEON::BI__builtin_neon_vsqrth_f16: {
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Int = Builder.getIsFPConstrained()
              ? Intrinsic::experimental_constrained_sqrt
              : Intrinsic::sqrt;
    return genNeonCall(ME.getIntrinsic(Int, HalfTy), Ops, "vsqrt");
  }
  case NEON::BI__builtin_neon_vsqrt_v:
  case NEON::BI__builtin_neon_vsqrtq_v: {
    Int = Builder.getIsFPConstrained()
              ? Intrinsic::experimental_constrained_sqrt
              : Intrinsic::sqrt;
    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    return genNeonCall(ME.getIntrinsic(Int, Ty), Ops, "vsqrt");
  }
  case NEON::BI__builtin_neon_vrbit_v:
  case NEON::BI__builtin_neon_vrbitq_v: {
    Int = Intrinsic::bitreverse;
    return genNeonCall(ME.getIntrinsic(Int, Ty), Ops, "vrbit");
  }
  case NEON::BI__builtin_neon_vaddv_u8:
    usgn = true;
    [[fallthrough]];
  case NEON::BI__builtin_neon_vaddv_s8: {
    Int = usgn ? Intrinsic::aarch64_neon_uaddv : Intrinsic::aarch64_neon_saddv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int8Ty, 8);
    llvm::Type *Tys[2] = {Ty, VTy};
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Ops[0] = genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vaddv");
    return Builder.CreateTrunc(Ops[0], Int8Ty);
  }
  case NEON::BI__builtin_neon_vaddv_u16:
    usgn = true;
    [[fallthrough]];
  case NEON::BI__builtin_neon_vaddv_s16: {
    Int = usgn ? Intrinsic::aarch64_neon_uaddv : Intrinsic::aarch64_neon_saddv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int16Ty, 4);
    llvm::Type *Tys[2] = {Ty, VTy};
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Ops[0] = genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vaddv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vaddvq_u8:
    usgn = true;
    [[fallthrough]];
  case NEON::BI__builtin_neon_vaddvq_s8: {
    Int = usgn ? Intrinsic::aarch64_neon_uaddv : Intrinsic::aarch64_neon_saddv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int8Ty, 16);
    llvm::Type *Tys[2] = {Ty, VTy};
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Ops[0] = genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vaddv");
    return Builder.CreateTrunc(Ops[0], Int8Ty);
  }
  case NEON::BI__builtin_neon_vaddvq_u16:
    usgn = true;
    [[fallthrough]];
  case NEON::BI__builtin_neon_vaddvq_s16: {
    Int = usgn ? Intrinsic::aarch64_neon_uaddv : Intrinsic::aarch64_neon_saddv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int16Ty, 8);
    llvm::Type *Tys[2] = {Ty, VTy};
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Ops[0] = genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vaddv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vmaxv_u8: {
    Int = Intrinsic::aarch64_neon_umaxv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int8Ty, 8);
    llvm::Type *Tys[2] = {Ty, VTy};
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Ops[0] = genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vmaxv");
    return Builder.CreateTrunc(Ops[0], Int8Ty);
  }
  case NEON::BI__builtin_neon_vmaxv_u16: {
    Int = Intrinsic::aarch64_neon_umaxv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int16Ty, 4);
    llvm::Type *Tys[2] = {Ty, VTy};
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Ops[0] = genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vmaxv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vmaxvq_u8: {
    Int = Intrinsic::aarch64_neon_umaxv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int8Ty, 16);
    llvm::Type *Tys[2] = {Ty, VTy};
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Ops[0] = genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vmaxv");
    return Builder.CreateTrunc(Ops[0], Int8Ty);
  }
  case NEON::BI__builtin_neon_vmaxvq_u16: {
    Int = Intrinsic::aarch64_neon_umaxv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int16Ty, 8);
    llvm::Type *Tys[2] = {Ty, VTy};
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Ops[0] = genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vmaxv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vmaxv_s8: {
    Int = Intrinsic::aarch64_neon_smaxv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int8Ty, 8);
    llvm::Type *Tys[2] = {Ty, VTy};
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Ops[0] = genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vmaxv");
    return Builder.CreateTrunc(Ops[0], Int8Ty);
  }
  case NEON::BI__builtin_neon_vmaxv_s16: {
    Int = Intrinsic::aarch64_neon_smaxv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int16Ty, 4);
    llvm::Type *Tys[2] = {Ty, VTy};
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Ops[0] = genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vmaxv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vmaxvq_s8: {
    Int = Intrinsic::aarch64_neon_smaxv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int8Ty, 16);
    llvm::Type *Tys[2] = {Ty, VTy};
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Ops[0] = genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vmaxv");
    return Builder.CreateTrunc(Ops[0], Int8Ty);
  }
  case NEON::BI__builtin_neon_vmaxvq_s16: {
    Int = Intrinsic::aarch64_neon_smaxv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int16Ty, 8);
    llvm::Type *Tys[2] = {Ty, VTy};
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Ops[0] = genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vmaxv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vmaxv_f16: {
    Int = Intrinsic::aarch64_neon_fmaxv;
    Ty = HalfTy;
    VTy = llvm::FixedVectorType::get(HalfTy, 4);
    llvm::Type *Tys[2] = {Ty, VTy};
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Ops[0] = genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vmaxv");
    return Builder.CreateTrunc(Ops[0], HalfTy);
  }
  case NEON::BI__builtin_neon_vmaxvq_f16: {
    Int = Intrinsic::aarch64_neon_fmaxv;
    Ty = HalfTy;
    VTy = llvm::FixedVectorType::get(HalfTy, 8);
    llvm::Type *Tys[2] = {Ty, VTy};
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Ops[0] = genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vmaxv");
    return Builder.CreateTrunc(Ops[0], HalfTy);
  }
  case NEON::BI__builtin_neon_vminv_u8: {
    Int = Intrinsic::aarch64_neon_uminv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int8Ty, 8);
    llvm::Type *Tys[2] = {Ty, VTy};
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Ops[0] = genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vminv");
    return Builder.CreateTrunc(Ops[0], Int8Ty);
  }
  case NEON::BI__builtin_neon_vminv_u16: {
    Int = Intrinsic::aarch64_neon_uminv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int16Ty, 4);
    llvm::Type *Tys[2] = {Ty, VTy};
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Ops[0] = genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vminv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vminvq_u8: {
    Int = Intrinsic::aarch64_neon_uminv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int8Ty, 16);
    llvm::Type *Tys[2] = {Ty, VTy};
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Ops[0] = genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vminv");
    return Builder.CreateTrunc(Ops[0], Int8Ty);
  }
  case NEON::BI__builtin_neon_vminvq_u16: {
    Int = Intrinsic::aarch64_neon_uminv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int16Ty, 8);
    llvm::Type *Tys[2] = {Ty, VTy};
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Ops[0] = genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vminv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vminv_s8: {
    Int = Intrinsic::aarch64_neon_sminv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int8Ty, 8);
    llvm::Type *Tys[2] = {Ty, VTy};
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Ops[0] = genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vminv");
    return Builder.CreateTrunc(Ops[0], Int8Ty);
  }
  case NEON::BI__builtin_neon_vminv_s16: {
    Int = Intrinsic::aarch64_neon_sminv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int16Ty, 4);
    llvm::Type *Tys[2] = {Ty, VTy};
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Ops[0] = genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vminv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vminvq_s8: {
    Int = Intrinsic::aarch64_neon_sminv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int8Ty, 16);
    llvm::Type *Tys[2] = {Ty, VTy};
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Ops[0] = genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vminv");
    return Builder.CreateTrunc(Ops[0], Int8Ty);
  }
  case NEON::BI__builtin_neon_vminvq_s16: {
    Int = Intrinsic::aarch64_neon_sminv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int16Ty, 8);
    llvm::Type *Tys[2] = {Ty, VTy};
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Ops[0] = genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vminv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vminv_f16: {
    Int = Intrinsic::aarch64_neon_fminv;
    Ty = HalfTy;
    VTy = llvm::FixedVectorType::get(HalfTy, 4);
    llvm::Type *Tys[2] = {Ty, VTy};
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Ops[0] = genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vminv");
    return Builder.CreateTrunc(Ops[0], HalfTy);
  }
  case NEON::BI__builtin_neon_vminvq_f16: {
    Int = Intrinsic::aarch64_neon_fminv;
    Ty = HalfTy;
    VTy = llvm::FixedVectorType::get(HalfTy, 8);
    llvm::Type *Tys[2] = {Ty, VTy};
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Ops[0] = genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vminv");
    return Builder.CreateTrunc(Ops[0], HalfTy);
  }
  case NEON::BI__builtin_neon_vmaxnmv_f16: {
    Int = Intrinsic::aarch64_neon_fmaxnmv;
    Ty = HalfTy;
    VTy = llvm::FixedVectorType::get(HalfTy, 4);
    llvm::Type *Tys[2] = {Ty, VTy};
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Ops[0] = genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vmaxnmv");
    return Builder.CreateTrunc(Ops[0], HalfTy);
  }
  case NEON::BI__builtin_neon_vmaxnmvq_f16: {
    Int = Intrinsic::aarch64_neon_fmaxnmv;
    Ty = HalfTy;
    VTy = llvm::FixedVectorType::get(HalfTy, 8);
    llvm::Type *Tys[2] = {Ty, VTy};
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Ops[0] = genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vmaxnmv");
    return Builder.CreateTrunc(Ops[0], HalfTy);
  }
  case NEON::BI__builtin_neon_vminnmv_f16: {
    Int = Intrinsic::aarch64_neon_fminnmv;
    Ty = HalfTy;
    VTy = llvm::FixedVectorType::get(HalfTy, 4);
    llvm::Type *Tys[2] = {Ty, VTy};
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Ops[0] = genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vminnmv");
    return Builder.CreateTrunc(Ops[0], HalfTy);
  }
  case NEON::BI__builtin_neon_vminnmvq_f16: {
    Int = Intrinsic::aarch64_neon_fminnmv;
    Ty = HalfTy;
    VTy = llvm::FixedVectorType::get(HalfTy, 8);
    llvm::Type *Tys[2] = {Ty, VTy};
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Ops[0] = genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vminnmv");
    return Builder.CreateTrunc(Ops[0], HalfTy);
  }
  case NEON::BI__builtin_neon_vmul_n_f64: {
    Ops[0] = Builder.CreateBitCast(Ops[0], DoubleTy);
    Value *RHS = Builder.CreateBitCast(genScalarExpr(E->getArg(1)), DoubleTy);
    return Builder.CreateFMul(Ops[0], RHS);
  }
  case NEON::BI__builtin_neon_vaddlv_u8: {
    Int = Intrinsic::aarch64_neon_uaddlv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int8Ty, 8);
    llvm::Type *Tys[2] = {Ty, VTy};
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Ops[0] = genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vaddlv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vaddlv_u16: {
    Int = Intrinsic::aarch64_neon_uaddlv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int16Ty, 4);
    llvm::Type *Tys[2] = {Ty, VTy};
    Ops.push_back(genScalarExpr(E->getArg(0)));
    return genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vaddlv");
  }
  case NEON::BI__builtin_neon_vaddlvq_u8: {
    Int = Intrinsic::aarch64_neon_uaddlv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int8Ty, 16);
    llvm::Type *Tys[2] = {Ty, VTy};
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Ops[0] = genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vaddlv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vaddlvq_u16: {
    Int = Intrinsic::aarch64_neon_uaddlv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int16Ty, 8);
    llvm::Type *Tys[2] = {Ty, VTy};
    Ops.push_back(genScalarExpr(E->getArg(0)));
    return genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vaddlv");
  }
  case NEON::BI__builtin_neon_vaddlv_s8: {
    Int = Intrinsic::aarch64_neon_saddlv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int8Ty, 8);
    llvm::Type *Tys[2] = {Ty, VTy};
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Ops[0] = genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vaddlv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vaddlv_s16: {
    Int = Intrinsic::aarch64_neon_saddlv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int16Ty, 4);
    llvm::Type *Tys[2] = {Ty, VTy};
    Ops.push_back(genScalarExpr(E->getArg(0)));
    return genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vaddlv");
  }
  case NEON::BI__builtin_neon_vaddlvq_s8: {
    Int = Intrinsic::aarch64_neon_saddlv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int8Ty, 16);
    llvm::Type *Tys[2] = {Ty, VTy};
    Ops.push_back(genScalarExpr(E->getArg(0)));
    Ops[0] = genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vaddlv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vaddlvq_s16: {
    Int = Intrinsic::aarch64_neon_saddlv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int16Ty, 8);
    llvm::Type *Tys[2] = {Ty, VTy};
    Ops.push_back(genScalarExpr(E->getArg(0)));
    return genNeonCall(ME.getIntrinsic(Int, Tys), Ops, "vaddlv");
  }
  case NEON::BI__builtin_neon_vsri_n_v:
  case NEON::BI__builtin_neon_vsriq_n_v: {
    Int = Intrinsic::aarch64_neon_vsri;
    llvm::Function *Intrin = ME.getIntrinsic(Int, Ty);
    return genNeonCall(Intrin, Ops, "vsri_n");
  }
  case NEON::BI__builtin_neon_vsli_n_v:
  case NEON::BI__builtin_neon_vsliq_n_v: {
    Int = Intrinsic::aarch64_neon_vsli;
    llvm::Function *Intrin = ME.getIntrinsic(Int, Ty);
    return genNeonCall(Intrin, Ops, "vsli_n");
  }
  case NEON::BI__builtin_neon_vsra_n_v:
  case NEON::BI__builtin_neon_vsraq_n_v:
    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    Ops[1] = genNeonRShiftImm(Ops[1], Ops[2], Ty, usgn, "vsra_n");
    return Builder.CreateAdd(Ops[0], Ops[1]);
  case NEON::BI__builtin_neon_vrsra_n_v:
  case NEON::BI__builtin_neon_vrsraq_n_v: {
    Int = usgn ? Intrinsic::aarch64_neon_urshl : Intrinsic::aarch64_neon_srshl;
    llvm::SmallVector<llvm::Value *, 2> TmpOps;
    TmpOps.push_back(Ops[1]);
    TmpOps.push_back(Ops[2]);
    Function *F = ME.getIntrinsic(Int, Ty);
    llvm::Value *tmp = genNeonCall(F, TmpOps, "vrshr_n", 1, true);
    Ops[0] = Builder.CreateBitCast(Ops[0], VTy);
    return Builder.CreateAdd(Ops[0], tmp);
  }
  case NEON::BI__builtin_neon_vld1_v:
  case NEON::BI__builtin_neon_vld1q_v: {
    return Builder.CreateAlignedLoad(VTy, Ops[0], PtrOp0.getAlignment());
  }
  case NEON::BI__builtin_neon_vst1_v:
  case NEON::BI__builtin_neon_vst1q_v:
    Ops[1] = Builder.CreateBitCast(Ops[1], VTy);
    return Builder.CreateAlignedStore(Ops[1], Ops[0], PtrOp0.getAlignment());
  case NEON::BI__builtin_neon_vld1_lane_v:
  case NEON::BI__builtin_neon_vld1q_lane_v: {
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[0] = Builder.CreateAlignedLoad(VTy->getElementType(), Ops[0],
                                       PtrOp0.getAlignment());
    return Builder.CreateInsertElement(Ops[1], Ops[0], Ops[2], "vld1_lane");
  }
  case NEON::BI__builtin_neon_vldap1_lane_s64:
  case NEON::BI__builtin_neon_vldap1q_lane_s64: {
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    llvm::LoadInst *LI = Builder.CreateAlignedLoad(
        VTy->getElementType(), Ops[0], PtrOp0.getAlignment());
    LI->setAtomic(llvm::AtomicOrdering::Acquire);
    Ops[0] = LI;
    return Builder.CreateInsertElement(Ops[1], Ops[0], Ops[2], "vldap1_lane");
  }
  case NEON::BI__builtin_neon_vld1_dup_v:
  case NEON::BI__builtin_neon_vld1q_dup_v: {
    Value *V = PoisonValue::get(Ty);
    Ops[0] = Builder.CreateAlignedLoad(VTy->getElementType(), Ops[0],
                                       PtrOp0.getAlignment());
    llvm::Constant *CI = ConstantInt::get(Int32Ty, 0);
    Ops[0] = Builder.CreateInsertElement(V, Ops[0], CI);
    return genNeonSplat(Ops[0], CI);
  }
  case NEON::BI__builtin_neon_vst1_lane_v:
  case NEON::BI__builtin_neon_vst1q_lane_v:
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[1] = Builder.CreateExtractElement(Ops[1], Ops[2]);
    return Builder.CreateAlignedStore(Ops[1], Ops[0], PtrOp0.getAlignment());
  case NEON::BI__builtin_neon_vstl1_lane_s64:
  case NEON::BI__builtin_neon_vstl1q_lane_s64: {
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[1] = Builder.CreateExtractElement(Ops[1], Ops[2]);
    llvm::StoreInst *SI =
        Builder.CreateAlignedStore(Ops[1], Ops[0], PtrOp0.getAlignment());
    SI->setAtomic(llvm::AtomicOrdering::Release);
    return SI;
  }
  case NEON::BI__builtin_neon_vld2_v:
  case NEON::BI__builtin_neon_vld2q_v: {
    llvm::Type *Tys[2] = {VTy, UnqualPtrTy};
    Function *F = ME.getIntrinsic(Intrinsic::aarch64_neon_ld2, Tys);
    Ops[1] = Builder.CreateCall(F, Ops[1], "vld2");
    return Builder.CreateDefaultAlignedStore(Ops[1], Ops[0]);
  }
  case NEON::BI__builtin_neon_vld3_v:
  case NEON::BI__builtin_neon_vld3q_v: {
    llvm::Type *Tys[2] = {VTy, UnqualPtrTy};
    Function *F = ME.getIntrinsic(Intrinsic::aarch64_neon_ld3, Tys);
    Ops[1] = Builder.CreateCall(F, Ops[1], "vld3");
    return Builder.CreateDefaultAlignedStore(Ops[1], Ops[0]);
  }
  case NEON::BI__builtin_neon_vld4_v:
  case NEON::BI__builtin_neon_vld4q_v: {
    llvm::Type *Tys[2] = {VTy, UnqualPtrTy};
    Function *F = ME.getIntrinsic(Intrinsic::aarch64_neon_ld4, Tys);
    Ops[1] = Builder.CreateCall(F, Ops[1], "vld4");
    return Builder.CreateDefaultAlignedStore(Ops[1], Ops[0]);
  }
  case NEON::BI__builtin_neon_vld2_dup_v:
  case NEON::BI__builtin_neon_vld2q_dup_v: {
    llvm::Type *Tys[2] = {VTy, UnqualPtrTy};
    Function *F = ME.getIntrinsic(Intrinsic::aarch64_neon_ld2r, Tys);
    Ops[1] = Builder.CreateCall(F, Ops[1], "vld2");
    return Builder.CreateDefaultAlignedStore(Ops[1], Ops[0]);
  }
  case NEON::BI__builtin_neon_vld3_dup_v:
  case NEON::BI__builtin_neon_vld3q_dup_v: {
    llvm::Type *Tys[2] = {VTy, UnqualPtrTy};
    Function *F = ME.getIntrinsic(Intrinsic::aarch64_neon_ld3r, Tys);
    Ops[1] = Builder.CreateCall(F, Ops[1], "vld3");
    return Builder.CreateDefaultAlignedStore(Ops[1], Ops[0]);
  }
  case NEON::BI__builtin_neon_vld4_dup_v:
  case NEON::BI__builtin_neon_vld4q_dup_v: {
    llvm::Type *Tys[2] = {VTy, UnqualPtrTy};
    Function *F = ME.getIntrinsic(Intrinsic::aarch64_neon_ld4r, Tys);
    Ops[1] = Builder.CreateCall(F, Ops[1], "vld4");
    return Builder.CreateDefaultAlignedStore(Ops[1], Ops[0]);
  }
  case NEON::BI__builtin_neon_vld2_lane_v:
  case NEON::BI__builtin_neon_vld2q_lane_v: {
    llvm::Type *Tys[2] = {VTy, Ops[1]->getType()};
    Function *F = ME.getIntrinsic(Intrinsic::aarch64_neon_ld2lane, Tys);
    std::rotate(Ops.begin() + 1, Ops.begin() + 2, Ops.end());
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[2] = Builder.CreateBitCast(Ops[2], Ty);
    Ops[3] = Builder.CreateZExt(Ops[3], Int64Ty);
    Ops[1] = Builder.CreateCall(F, llvm::ArrayRef(Ops).slice(1), "vld2_lane");
    return Builder.CreateDefaultAlignedStore(Ops[1], Ops[0]);
  }
  case NEON::BI__builtin_neon_vld3_lane_v:
  case NEON::BI__builtin_neon_vld3q_lane_v: {
    llvm::Type *Tys[2] = {VTy, Ops[1]->getType()};
    Function *F = ME.getIntrinsic(Intrinsic::aarch64_neon_ld3lane, Tys);
    std::rotate(Ops.begin() + 1, Ops.begin() + 2, Ops.end());
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[2] = Builder.CreateBitCast(Ops[2], Ty);
    Ops[3] = Builder.CreateBitCast(Ops[3], Ty);
    Ops[4] = Builder.CreateZExt(Ops[4], Int64Ty);
    Ops[1] = Builder.CreateCall(F, llvm::ArrayRef(Ops).slice(1), "vld3_lane");
    return Builder.CreateDefaultAlignedStore(Ops[1], Ops[0]);
  }
  case NEON::BI__builtin_neon_vld4_lane_v:
  case NEON::BI__builtin_neon_vld4q_lane_v: {
    llvm::Type *Tys[2] = {VTy, Ops[1]->getType()};
    Function *F = ME.getIntrinsic(Intrinsic::aarch64_neon_ld4lane, Tys);
    std::rotate(Ops.begin() + 1, Ops.begin() + 2, Ops.end());
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[2] = Builder.CreateBitCast(Ops[2], Ty);
    Ops[3] = Builder.CreateBitCast(Ops[3], Ty);
    Ops[4] = Builder.CreateBitCast(Ops[4], Ty);
    Ops[5] = Builder.CreateZExt(Ops[5], Int64Ty);
    Ops[1] = Builder.CreateCall(F, llvm::ArrayRef(Ops).slice(1), "vld4_lane");
    return Builder.CreateDefaultAlignedStore(Ops[1], Ops[0]);
  }
  case NEON::BI__builtin_neon_vst2_v:
  case NEON::BI__builtin_neon_vst2q_v: {
    std::rotate(Ops.begin(), Ops.begin() + 1, Ops.end());
    llvm::Type *Tys[2] = {VTy, Ops[2]->getType()};
    return genNeonCall(ME.getIntrinsic(Intrinsic::aarch64_neon_st2, Tys), Ops,
                       "");
  }
  case NEON::BI__builtin_neon_vst2_lane_v:
  case NEON::BI__builtin_neon_vst2q_lane_v: {
    std::rotate(Ops.begin(), Ops.begin() + 1, Ops.end());
    Ops[2] = Builder.CreateZExt(Ops[2], Int64Ty);
    llvm::Type *Tys[2] = {VTy, Ops[3]->getType()};
    return genNeonCall(ME.getIntrinsic(Intrinsic::aarch64_neon_st2lane, Tys),
                       Ops, "");
  }
  case NEON::BI__builtin_neon_vst3_v:
  case NEON::BI__builtin_neon_vst3q_v: {
    std::rotate(Ops.begin(), Ops.begin() + 1, Ops.end());
    llvm::Type *Tys[2] = {VTy, Ops[3]->getType()};
    return genNeonCall(ME.getIntrinsic(Intrinsic::aarch64_neon_st3, Tys), Ops,
                       "");
  }
  case NEON::BI__builtin_neon_vst3_lane_v:
  case NEON::BI__builtin_neon_vst3q_lane_v: {
    std::rotate(Ops.begin(), Ops.begin() + 1, Ops.end());
    Ops[3] = Builder.CreateZExt(Ops[3], Int64Ty);
    llvm::Type *Tys[2] = {VTy, Ops[4]->getType()};
    return genNeonCall(ME.getIntrinsic(Intrinsic::aarch64_neon_st3lane, Tys),
                       Ops, "");
  }
  case NEON::BI__builtin_neon_vst4_v:
  case NEON::BI__builtin_neon_vst4q_v: {
    std::rotate(Ops.begin(), Ops.begin() + 1, Ops.end());
    llvm::Type *Tys[2] = {VTy, Ops[4]->getType()};
    return genNeonCall(ME.getIntrinsic(Intrinsic::aarch64_neon_st4, Tys), Ops,
                       "");
  }
  case NEON::BI__builtin_neon_vst4_lane_v:
  case NEON::BI__builtin_neon_vst4q_lane_v: {
    std::rotate(Ops.begin(), Ops.begin() + 1, Ops.end());
    Ops[4] = Builder.CreateZExt(Ops[4], Int64Ty);
    llvm::Type *Tys[2] = {VTy, Ops[5]->getType()};
    return genNeonCall(ME.getIntrinsic(Intrinsic::aarch64_neon_st4lane, Tys),
                       Ops, "");
  }
  case NEON::BI__builtin_neon_vtrn_v:
  case NEON::BI__builtin_neon_vtrnq_v: {
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[2] = Builder.CreateBitCast(Ops[2], Ty);
    Value *SV = nullptr;

    for (unsigned vi = 0; vi != 2; ++vi) {
      llvm::SmallVector<int, 16> Indices;
      for (unsigned i = 0, e = VTy->getNumElements(); i != e; i += 2) {
        Indices.push_back(i + vi);
        Indices.push_back(i + e + vi);
      }
      Value *Addr = Builder.CreateConstInBoundsGEP1_32(Ty, Ops[0], vi);
      SV = Builder.CreateShuffleVector(Ops[1], Ops[2], Indices, "vtrn");
      SV = Builder.CreateDefaultAlignedStore(SV, Addr);
    }
    return SV;
  }
  case NEON::BI__builtin_neon_vuzp_v:
  case NEON::BI__builtin_neon_vuzpq_v: {
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[2] = Builder.CreateBitCast(Ops[2], Ty);
    Value *SV = nullptr;

    for (unsigned vi = 0; vi != 2; ++vi) {
      llvm::SmallVector<int, 16> Indices;
      for (unsigned i = 0, e = VTy->getNumElements(); i != e; ++i)
        Indices.push_back(2 * i + vi);

      Value *Addr = Builder.CreateConstInBoundsGEP1_32(Ty, Ops[0], vi);
      SV = Builder.CreateShuffleVector(Ops[1], Ops[2], Indices, "vuzp");
      SV = Builder.CreateDefaultAlignedStore(SV, Addr);
    }
    return SV;
  }
  case NEON::BI__builtin_neon_vzip_v:
  case NEON::BI__builtin_neon_vzipq_v: {
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[2] = Builder.CreateBitCast(Ops[2], Ty);
    Value *SV = nullptr;

    for (unsigned vi = 0; vi != 2; ++vi) {
      llvm::SmallVector<int, 16> Indices;
      for (unsigned i = 0, e = VTy->getNumElements(); i != e; i += 2) {
        Indices.push_back((i + vi * e) >> 1);
        Indices.push_back(((i + vi * e) >> 1) + e);
      }
      Value *Addr = Builder.CreateConstInBoundsGEP1_32(Ty, Ops[0], vi);
      SV = Builder.CreateShuffleVector(Ops[1], Ops[2], Indices, "vzip");
      SV = Builder.CreateDefaultAlignedStore(SV, Addr);
    }
    return SV;
  }
  case NEON::BI__builtin_neon_vqtbl1q_v: {
    return genNeonCall(ME.getIntrinsic(Intrinsic::aarch64_neon_tbl1, Ty), Ops,
                       "vtbl1");
  }
  case NEON::BI__builtin_neon_vqtbl2q_v: {
    return genNeonCall(ME.getIntrinsic(Intrinsic::aarch64_neon_tbl2, Ty), Ops,
                       "vtbl2");
  }
  case NEON::BI__builtin_neon_vqtbl3q_v: {
    return genNeonCall(ME.getIntrinsic(Intrinsic::aarch64_neon_tbl3, Ty), Ops,
                       "vtbl3");
  }
  case NEON::BI__builtin_neon_vqtbl4q_v: {
    return genNeonCall(ME.getIntrinsic(Intrinsic::aarch64_neon_tbl4, Ty), Ops,
                       "vtbl4");
  }
  case NEON::BI__builtin_neon_vqtbx1q_v: {
    return genNeonCall(ME.getIntrinsic(Intrinsic::aarch64_neon_tbx1, Ty), Ops,
                       "vtbx1");
  }
  case NEON::BI__builtin_neon_vqtbx2q_v: {
    return genNeonCall(ME.getIntrinsic(Intrinsic::aarch64_neon_tbx2, Ty), Ops,
                       "vtbx2");
  }
  case NEON::BI__builtin_neon_vqtbx3q_v: {
    return genNeonCall(ME.getIntrinsic(Intrinsic::aarch64_neon_tbx3, Ty), Ops,
                       "vtbx3");
  }
  case NEON::BI__builtin_neon_vqtbx4q_v: {
    return genNeonCall(ME.getIntrinsic(Intrinsic::aarch64_neon_tbx4, Ty), Ops,
                       "vtbx4");
  }
  case NEON::BI__builtin_neon_vsqadd_v:
  case NEON::BI__builtin_neon_vsqaddq_v: {
    Int = Intrinsic::aarch64_neon_usqadd;
    return genNeonCall(ME.getIntrinsic(Int, Ty), Ops, "vsqadd");
  }
  case NEON::BI__builtin_neon_vuqadd_v:
  case NEON::BI__builtin_neon_vuqaddq_v: {
    Int = Intrinsic::aarch64_neon_suqadd;
    return genNeonCall(ME.getIntrinsic(Int, Ty), Ops, "vuqadd");
  }
  }
}
