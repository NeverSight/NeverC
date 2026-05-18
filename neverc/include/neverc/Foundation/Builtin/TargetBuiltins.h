#ifndef NEVERC_BASIC_TARGETBUILTINS_H
#define NEVERC_BASIC_TARGETBUILTINS_H

#include "neverc/Foundation/Builtin/Builtins.h"
#include "llvm/Support/MathExtras.h"
#include <algorithm>
#include <stdint.h>

namespace neverc {

namespace NEON {
enum {
  LastTIBuiltin = neverc::Builtin::FirstTSBuiltin - 1,
#define BUILTIN(ID, TYPE, ATTRS) BI##ID,
#define TARGET_BUILTIN(ID, TYPE, ATTRS, FEATURE) BI##ID,
#include "neverc/Foundation/Builtin/BuiltinsNEON.def"
  FirstTSBuiltin
};
} // namespace NEON

namespace SVE {
enum {
  LastNEONBuiltin = NEON::FirstTSBuiltin - 1,
#define BUILTIN(ID, TYPE, ATTRS) BI##ID,
#define TARGET_BUILTIN(ID, TYPE, ATTRS, FEATURE) BI##ID,
#include "neverc/Foundation/Builtin/BuiltinsSVE.def"
  FirstTSBuiltin,
};
} // namespace SVE

namespace SME {
enum {
  LastSVEBuiltin = SVE::FirstTSBuiltin - 1,
#define BUILTIN(ID, TYPE, ATTRS) BI##ID,
#define TARGET_BUILTIN(ID, TYPE, ATTRS, FEATURE) BI##ID,
#include "neverc/Foundation/Builtin/BuiltinsSME.def"
  FirstTSBuiltin,
};
} // namespace SME

namespace AArch64 {
enum {
  LastTIBuiltin = neverc::Builtin::FirstTSBuiltin - 1,
  LastNEONBuiltin = NEON::FirstTSBuiltin - 1,
  FirstSVEBuiltin = NEON::FirstTSBuiltin,
  LastSVEBuiltin = SVE::FirstTSBuiltin - 1,
  FirstSMEBuiltin = SVE::FirstTSBuiltin,
  LastSMEBuiltin = SME::FirstTSBuiltin - 1,
#define BUILTIN(ID, TYPE, ATTRS) BI##ID,
#include "neverc/Foundation/Builtin/BuiltinsAArch64.def"
  LastTSBuiltin
};
} // namespace AArch64

namespace X86 {
enum {
  LastTIBuiltin = neverc::Builtin::FirstTSBuiltin - 1,
#define BUILTIN(ID, TYPE, ATTRS) BI##ID,
#include "neverc/Foundation/Builtin/BuiltinsX86.def"
  FirstX86_64Builtin,
  LastX86CommonBuiltin = FirstX86_64Builtin - 1,
#define BUILTIN(ID, TYPE, ATTRS) BI##ID,
#include "neverc/Foundation/Builtin/BuiltinsX86_64.def"
  LastTSBuiltin
};
} // namespace X86

class NeonTypeFlags {
  enum { EltTypeMask = 0xf, UnsignedFlag = 0x10, QuadFlag = 0x20 };
  uint32_t Flags;

public:
  enum EltType {
    Int8,
    Int16,
    Int32,
    Int64,
    Poly8,
    Poly16,
    Poly64,
    Poly128,
    Float16,
    Float32,
    Float64,
    BFloat16
  };

  NeonTypeFlags(unsigned F) : Flags(F) {}
  NeonTypeFlags(EltType ET, bool IsUnsigned, bool IsQuad) : Flags(ET) {
    if (IsUnsigned)
      Flags |= UnsignedFlag;
    if (IsQuad)
      Flags |= QuadFlag;
  }

  EltType getEltType() const { return (EltType)(Flags & EltTypeMask); }
  bool isPoly() const {
    EltType ET = getEltType();
    return ET == Poly8 || ET == Poly16 || ET == Poly64;
  }
  bool isUnsigned() const { return (Flags & UnsignedFlag) != 0; }
  bool isQuad() const { return (Flags & QuadFlag) != 0; }
};

class SVETypeFlags {
  uint64_t Flags;
  unsigned EltTypeShift;
  unsigned MemEltTypeShift;
  unsigned MergeTypeShift;
  unsigned SplatOperandMaskShift;

public:
#define LLVM_GET_SVE_TYPEFLAGS
#include "neverc/Foundation/arm_sve_typeflags.td.h"
#undef LLVM_GET_SVE_TYPEFLAGS

  enum EltType {
#define LLVM_GET_SVE_ELTTYPES
#include "neverc/Foundation/arm_sve_typeflags.td.h"
#undef LLVM_GET_SVE_ELTTYPES
  };

  enum MemEltType {
#define LLVM_GET_SVE_MEMELTTYPES
#include "neverc/Foundation/arm_sve_typeflags.td.h"
#undef LLVM_GET_SVE_MEMELTTYPES
  };

  enum MergeType {
#define LLVM_GET_SVE_MERGETYPES
#include "neverc/Foundation/arm_sve_typeflags.td.h"
#undef LLVM_GET_SVE_MERGETYPES
  };

  enum ImmCheckType {
#define LLVM_GET_SVE_IMMCHECKTYPES
#include "neverc/Foundation/arm_sve_typeflags.td.h"
#undef LLVM_GET_SVE_IMMCHECKTYPES
  };

  SVETypeFlags(uint64_t F) : Flags(F) {
    EltTypeShift = llvm::countr_zero(EltTypeMask);
    MemEltTypeShift = llvm::countr_zero(MemEltTypeMask);
    MergeTypeShift = llvm::countr_zero(MergeTypeMask);
    SplatOperandMaskShift = llvm::countr_zero(SplatOperandMask);
  }

  EltType getEltType() const {
    return (EltType)((Flags & EltTypeMask) >> EltTypeShift);
  }

  MemEltType getMemEltType() const {
    return (MemEltType)((Flags & MemEltTypeMask) >> MemEltTypeShift);
  }

  MergeType getMergeType() const {
    return (MergeType)((Flags & MergeTypeMask) >> MergeTypeShift);
  }

  unsigned getSplatOperand() const {
    return ((Flags & SplatOperandMask) >> SplatOperandMaskShift) - 1;
  }

  bool hasSplatOperand() const { return Flags & SplatOperandMask; }

  bool isLoad() const { return Flags & IsLoad; }
  bool isStore() const { return Flags & IsStore; }
  bool isGatherLoad() const { return Flags & IsGatherLoad; }
  bool isScatterStore() const { return Flags & IsScatterStore; }
  bool isStructLoad() const { return Flags & IsStructLoad; }
  bool isStructStore() const { return Flags & IsStructStore; }
  bool isZExtReturn() const { return Flags & IsZExtReturn; }
  bool isByteIndexed() const { return Flags & IsByteIndexed; }
  bool isOverloadNone() const { return Flags & IsOverloadNone; }
  bool isOverloadWhile() const { return Flags & IsOverloadWhile; }
  bool isOverloadDefault() const { return !(Flags & OverloadKindMask); }
  bool isOverloadWhileRW() const { return Flags & IsOverloadWhileRW; }
  bool isOverloadCvt() const { return Flags & IsOverloadCvt; }
  bool isPrefetch() const { return Flags & IsPrefetch; }
  bool isReverseCompare() const { return Flags & ReverseCompare; }
  bool isAppendSVALL() const { return Flags & IsAppendSVALL; }
  bool isInsertOp1SVALL() const { return Flags & IsInsertOp1SVALL; }
  bool isGatherPrefetch() const { return Flags & IsGatherPrefetch; }
  bool isReverseUSDOT() const { return Flags & ReverseUSDOT; }
  bool isReverseMergeAnyBinOp() const { return Flags & ReverseMergeAnyBinOp; }
  bool isReverseMergeAnyAccOp() const { return Flags & ReverseMergeAnyAccOp; }
  bool isUndef() const { return Flags & IsUndef; }
  bool isTupleCreate() const { return Flags & IsTupleCreate; }
  bool isTupleGet() const { return Flags & IsTupleGet; }
  bool isTupleSet() const { return Flags & IsTupleSet; }
  bool isReadZA() const { return Flags & IsReadZA; }
  bool isWriteZA() const { return Flags & IsWriteZA; }
  bool isReductionQV() const { return Flags & IsReductionQV; }
  uint64_t getBits() const { return Flags; }
  bool isFlagSet(uint64_t Flag) const { return Flags & Flag; }
};

static constexpr uint64_t LargestBuiltinID =
    std::max<uint64_t>({AArch64::LastTSBuiltin, X86::LastTSBuiltin});

} // end namespace neverc.

#endif
