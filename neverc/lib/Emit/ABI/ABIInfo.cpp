#include "ABI/ABIInfo.h"
#include "ABI/ABIInfoImpl.h"

using namespace neverc;
using namespace neverc::CodeGen;

// Pin the vtable to this file.
ABIInfo::~ABIInfo() = default;

CGABI &ABIInfo::getCGABI() const { return CGT.getCGABI(); }

TreeContext &ABIInfo::getContext() const { return CGT.getContext(); }

llvm::LLVMContext &ABIInfo::getVMContext() const {
  return CGT.getLLVMContext();
}

const llvm::DataLayout &ABIInfo::getDataLayout() const {
  return CGT.getDataLayout();
}

const TargetInfo &ABIInfo::getTarget() const { return CGT.getTarget(); }

const CodeGenOptions &ABIInfo::getCodeGenOpts() const {
  return CGT.getCodeGenOpts();
}

bool ABIInfo::isAndroid() const { return getTarget().getTriple().isAndroid(); }

Address ABIInfo::genMSVAArg(FunctionEmitter &FE, Address VAListAddr,
                            QualType Ty) const {
  return Address::invalid();
}

bool ABIInfo::isHomogeneousAggregateBaseType(QualType Ty) const {
  return false;
}

bool ABIInfo::isHomogeneousAggregateSmallEnough(const Type *Base,
                                                uint64_t Members) const {
  return false;
}

bool ABIInfo::isZeroLengthBitfieldPermittedInHomogeneousAggregate() const {
  return false;
}

bool ABIInfo::isHomogeneousAggregate(QualType Ty, const Type *&Base,
                                     uint64_t &Members) const {
  if (const ConstantArrayType *AT = getContext().getAsConstantArrayType(Ty)) {
    uint64_t NElements = AT->getSize().getZExtValue();
    if (NElements == 0)
      return false;
    if (!isHomogeneousAggregate(AT->getElementType(), Base, Members))
      return false;
    Members *= NElements;
  } else if (const RecordType *RT = Ty->getAs<RecordType>()) {
    const RecordDecl *RD = RT->getDecl();
    if (RD->hasFlexibleArrayMember())
      return false;

    Members = 0;

    for (const auto *FD : RD->fields()) {
      // Ignore (non-zero arrays of) empty records.
      QualType FT = FD->getType();
      while (const ConstantArrayType *AT =
                 getContext().getAsConstantArrayType(FT)) {
        if (AT->getSize().getZExtValue() == 0)
          return false;
        FT = AT->getElementType();
      }
      if (isEmptyRecord(getContext(), FT, true))
        continue;

      if (isZeroLengthBitfieldPermittedInHomogeneousAggregate() &&
          FD->isZeroLengthBitField(getContext()))
        continue;

      uint64_t FldMembers;
      if (!isHomogeneousAggregate(FD->getType(), Base, FldMembers))
        return false;

      Members = (RD->isUnion() ? std::max(Members, FldMembers)
                               : Members + FldMembers);
    }

    if (!Base)
      return false;

    // Ensure there is no padding.
    if (getContext().getTypeSize(Base) * Members !=
        getContext().getTypeSize(Ty))
      return false;
  } else {
    Members = 1;
    if (const ComplexType *CT = Ty->getAs<ComplexType>()) {
      Members = 2;
      Ty = CT->getElementType();
    }

    // Most ABIs only support float, double, and some vector type widths.
    if (!isHomogeneousAggregateBaseType(Ty))
      return false;

    // The base type must be the same for all members.  Types that
    // agree in both total size and mode (float vs. vector) are
    // treated as being equivalent here.
    const Type *TyPtr = Ty.getTypePtr();
    if (!Base) {
      Base = TyPtr;
      // If it's a non-power-of-2 vector, its size is already a power-of-2,
      // so make sure to widen it explicitly.
      if (const VectorType *VT = Base->getAs<VectorType>()) {
        QualType EltTy = VT->getElementType();
        unsigned NumElements =
            getContext().getTypeSize(VT) / getContext().getTypeSize(EltTy);
        Base = getContext()
                   .getVectorType(EltTy, NumElements, VT->getVectorKind())
                   .getTypePtr();
      }
    }

    if (Base->isVectorType() != TyPtr->isVectorType() ||
        getContext().getTypeSize(Base) != getContext().getTypeSize(TyPtr))
      return false;
  }
  return Members > 0 && isHomogeneousAggregateSmallEnough(Base, Members);
}

bool ABIInfo::isPromotableIntegerTypeForABI(QualType Ty) const {
  if (getContext().isPromotableIntegerType(Ty))
    return true;

  if (const auto *EIT = Ty->getAs<BitIntType>())
    if (EIT->getNumBits() < getContext().getTypeSize(getContext().IntTy))
      return true;

  return false;
}

ABIArgInfo ABIInfo::getNaturalAlignIndirect(QualType Ty, bool ByVal,
                                            bool Realign,
                                            llvm::Type *Padding) const {
  return ABIArgInfo::getIndirect(getContext().getTypeAlignInChars(Ty), ByVal,
                                 Realign, Padding);
}
