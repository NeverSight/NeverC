#include "ABI/EmitterABI.h"
#include "Core/RecordLayoutInfo.h"
#include "Core/TypeEmitter.h"
#include "neverc/Foundation/LangOpts/CodeGenOptions.h"
#include "neverc/Tree/Core/Attr.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Expr/Expr.h"
#include "neverc/Tree/Type/StructLayout.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
using namespace neverc;
using namespace Emit;

// ===----------------------------------------------------------------------===
// RecordLowering
// ===----------------------------------------------------------------------===

namespace {
struct RecordLowering {
  // Sentinel member type ensures correct rounding alongside standard fields.
  struct MemberInfo {
    CharUnits Offset;
    enum InfoKind { Field, Scissor } Kind;
    llvm::Type *Data;
    const FieldDecl *FD;
    MemberInfo(CharUnits Offset, InfoKind Kind, llvm::Type *Data,
               const FieldDecl *FD = nullptr)
        : Offset(Offset), Kind(Kind), Data(Data), FD(FD) {}
    bool operator<(const MemberInfo &a) const { return Offset < a.Offset; }
  };

  RecordLowering(TypeEmitter &Types, const RecordDecl *D, bool Packed);
  MemberInfo StorageInfo(CharUnits Offset, llvm::Type *Data) {
    return MemberInfo(Offset, MemberInfo::Field, Data);
  }

  bool isDiscreteBitFieldABI() { return D->isMsStruct(Context); }

  bool isAAPCS() const {
    return Context.getTargetInfo().getABI().starts_with("aapcs");
  }

  llvm::Type *getIntNType(uint64_t NumBits) {
    unsigned AlignedBits = llvm::alignTo(NumBits, Context.getCharWidth());
    return llvm::Type::getIntNTy(Types.getLLVMContext(), AlignedBits);
  }
  llvm::Type *getCharType() {
    return llvm::Type::getIntNTy(Types.getLLVMContext(),
                                 Context.getCharWidth());
  }
  llvm::Type *getByteArrayType(CharUnits NumChars) {
    assert(!NumChars.isZero() && "Empty byte arrays aren't allowed.");
    llvm::Type *Type = getCharType();
    return NumChars == CharUnits::One() ? Type
                                        : (llvm::Type *)llvm::ArrayType::get(
                                              Type, NumChars.getQuantity());
  }
  llvm::Type *getStorageType(const FieldDecl *FD) {
    llvm::Type *Type = Types.convertTypeForMem(FD->getType());
    if (!FD->isBitField())
      return Type;
    if (isDiscreteBitFieldABI())
      return Type;
    return getIntNType(std::min(FD->getBitWidthValue(Context),
                                (unsigned)Context.toBits(getSize(Type))));
  }
  CharUnits bitsToCharUnits(uint64_t BitOffset) {
    return Context.toCharUnitsFromBits(BitOffset);
  }
  CharUnits getSize(llvm::Type *Type) {
    return CharUnits::fromQuantity(DataLayout.getTypeAllocSize(Type));
  }
  CharUnits getAlignment(llvm::Type *Type) {
    return CharUnits::fromQuantity(DataLayout.getABITypeAlign(Type));
  }
  bool isZeroInitializable(const FieldDecl *FD) {
    return Types.isZeroInitializable(FD->getType());
  }
  bool isZeroInitializable(const RecordDecl *RD) {
    return Types.isZeroInitializable(RD);
  }
  void appendPaddingBytes(CharUnits Size) {
    if (!Size.isZero())
      FieldTypes.push_back(getByteArrayType(Size));
  }
  uint64_t getFieldBitOffset(const FieldDecl *FD) {
    return Layout.getFieldOffset(FD->getFieldIndex());
  }

  void setBitFieldInfo(const FieldDecl *FD, CharUnits StartOffset,
                       llvm::Type *StorageType);
  void lower();
  void lowerUnion();
  void accumulateFields();
  void accumulateBitFields(RecordDecl::field_iterator Field,
                           RecordDecl::field_iterator FieldEnd);
  void computeVolatileBitfields();
  void calculateZeroInit();
  void clipTailPadding();
  void determinePacked();
  void insertPadding();
  void fillOutputFields();

  TypeEmitter &Types;
  const TreeContext &Context;
  const RecordDecl *D;
  const StructRecordLayout &Layout;
  const llvm::DataLayout &DataLayout;
  std::vector<MemberInfo> Members;
  // Consumed by TypeEmitter::computeRecordLayout.
  llvm::SmallVector<llvm::Type *, 16> FieldTypes;
  llvm::DenseMap<const FieldDecl *, unsigned> Fields;
  llvm::DenseMap<const FieldDecl *, BitFieldInfo> BitFields;
  bool IsZeroInitializable : 1;
  bool Packed : 1;

private:
  RecordLowering(const RecordLowering &) = delete;
  void operator=(const RecordLowering &) = delete;
};
} // namespace

RecordLowering::RecordLowering(TypeEmitter &Types, const RecordDecl *D,
                               bool Packed)
    : Types(Types), Context(Types.getContext()), D(D),
      Layout(Types.getContext().getStructRecordLayout(D)),
      DataLayout(Types.getDataLayout()), IsZeroInitializable(true),
      Packed(Packed) {}

void RecordLowering::setBitFieldInfo(const FieldDecl *FD, CharUnits StartOffset,
                                     llvm::Type *StorageType) {
  BitFieldInfo &Info = BitFields[FD->getCanonicalDecl()];
  Info.IsSigned = FD->getType()->isSignedIntegerOrEnumerationType();
  Info.Offset = (unsigned)(getFieldBitOffset(FD) - Context.toBits(StartOffset));
  Info.Size = FD->getBitWidthValue(Context);
  Info.StorageSize = (unsigned)DataLayout.getTypeAllocSizeInBits(StorageType);
  Info.StorageOffset = StartOffset;
  if (Info.Size > Info.StorageSize)
    Info.Size = Info.StorageSize;

  Info.VolatileStorageSize = 0;
  Info.VolatileOffset = 0;
  Info.VolatileStorageOffset = CharUnits::Zero();
}

void RecordLowering::lower() {
  // The lowering process implemented in this function takes a variety of
  // carefully ordered phases.
  // 1) Store all members (fields and bases) in a list and sort them by offset.
  // 2) Add a 1-byte capstone member at the Size of the structure.
  // 3) Clip bitfield storages members if their tail padding is or might be
  //    used by another field or base.  The clipping process uses the capstone
  //    by treating it as another object that occurs after the record.
  // 4) Determine if the llvm-struct requires packing.  It's important that this
  //    phase occur after clipping, because clipping changes the llvm type.
  //    This phase reads the offset of the capstone when determining packedness
  //    and updates the alignment of the capstone to be equal of the alignment
  //    of the record after doing so.
  // 5) Insert padding everywhere it is needed.  This phase requires 'Packed' to
  //    have been computed and needs to know the alignment of the record in
  //    order to understand if explicit tail padding is needed.
  // 6) Remove the capstone, we don't need it anymore.
  // 7) Determine if this record can be zero-initialized.  This phase could have
  //    been placed anywhere after phase 1.
  // 8) Format the complete list of members in a way that can be consumed by
  //    TypeEmitter::computeRecordLayout.
  CharUnits Size = Layout.getSize();
  if (D->isUnion()) {
    lowerUnion();
    computeVolatileBitfields();
    return;
  }
  accumulateFields();
  llvm::stable_sort(Members);
  Members.push_back(StorageInfo(Size, getIntNType(8)));
  clipTailPadding();
  determinePacked();
  insertPadding();
  Members.pop_back();
  calculateZeroInit();
  fillOutputFields();
  computeVolatileBitfields();
}

void RecordLowering::lowerUnion() {
  CharUnits LayoutSize = Layout.getSize();
  llvm::Type *StorageType = nullptr;
  bool SeenNamedMember = false;
  // The storage-type heuristic isn't strictly necessary (the first non-zero
  // bitfield's type would work), but changing it would churn lit tests.
  for (const auto *Field : D->fields()) {
    if (Field->isBitField()) {
      if (Field->isZeroLengthBitField(Context))
        continue;
      llvm::Type *FieldType = getStorageType(Field);
      if (LayoutSize < getSize(FieldType))
        FieldType = getByteArrayType(LayoutSize);
      setBitFieldInfo(Field, CharUnits::Zero(), FieldType);
    }
    Fields[Field->getCanonicalDecl()] = 0;
    llvm::Type *FieldType = getStorageType(Field);
    // A union containing e.g. a pointer-to-data-member may not be zero-
    // initializable; once we know that, stop looking for a "better" type.
    if (!SeenNamedMember) {
      SeenNamedMember = Field->getIdentifier();
      if (!SeenNamedMember)
        if (const auto *FieldRD = Field->getType()->getAsRecordDecl())
          SeenNamedMember = FieldRD->findFirstNamedDataMember();
      if (SeenNamedMember && !isZeroInitializable(Field)) {
        IsZeroInitializable = false;
        StorageType = FieldType;
      }
    }
    if (!IsZeroInitializable)
      continue;
    if (!StorageType || getAlignment(FieldType) > getAlignment(StorageType) ||
        (getAlignment(FieldType) == getAlignment(StorageType) &&
         getSize(FieldType) > getSize(StorageType)))
      StorageType = FieldType;
  }
  if (!StorageType)
    return appendPaddingBytes(LayoutSize);
  // Packed bitfields on Itanium can exceed the layout size.
  if (LayoutSize < getSize(StorageType))
    StorageType = getByteArrayType(LayoutSize);
  FieldTypes.push_back(StorageType);
  appendPaddingBytes(LayoutSize - getSize(StorageType));
  if (LayoutSize % getAlignment(StorageType))
    Packed = true;
}

void RecordLowering::accumulateFields() {
  for (RecordDecl::field_iterator Field = D->field_begin(),
                                  FieldEnd = D->field_end();
       Field != FieldEnd;) {
    if (Field->isBitField()) {
      RecordDecl::field_iterator Start = Field;
      // Iterate to gather the list of bitfields.
      for (++Field; Field != FieldEnd && Field->isBitField(); ++Field)
        ;
      accumulateBitFields(Start, Field);
    } else if (!Field->isZeroSize(Context)) {
      // Use base subobject layout for the potentially-overlapping field,
      // as it is done in RecordLayoutBuilder
      Members.push_back(MemberInfo(bitsToCharUnits(getFieldBitOffset(*Field)),
                                   MemberInfo::Field, getStorageType(*Field),
                                   *Field));
      ++Field;
    } else {
      ++Field;
    }
  }
}

void RecordLowering::accumulateBitFields(RecordDecl::field_iterator Field,
                                         RecordDecl::field_iterator FieldEnd) {
  // Run stores the first element of the current run of bitfields.  FieldEnd is
  // used as a special value to note that we don't have a current run.  A
  // bitfield run is a contiguous collection of bitfields that can be stored in
  // the same storage block.  Zero-sized bitfields and bitfields that would
  // cross an alignment boundary break a run and start a new one.
  RecordDecl::field_iterator Run = FieldEnd;
  // Tail is the offset of the first bit off the end of the current run.  It's
  // used to determine if the StructRecordLayout is treating these two bitfields
  // as contiguous.  StartBitOffset is offset of the beginning of the Run.
  uint64_t StartBitOffset, Tail = 0;
  if (isDiscreteBitFieldABI()) {
    for (; Field != FieldEnd; ++Field) {
      uint64_t BitOffset = getFieldBitOffset(*Field);
      // Zero-width bitfields end runs.
      if (Field->isZeroLengthBitField(Context)) {
        Run = FieldEnd;
        continue;
      }
      llvm::Type *Type =
          Types.convertTypeForMem(Field->getType(), /*ForBitField=*/true);
      // Start a new run if none exists or this field falls outside the current.
      if (Run == FieldEnd || BitOffset >= Tail) {
        Run = Field;
        StartBitOffset = BitOffset;
        Tail = StartBitOffset + DataLayout.getTypeAllocSizeInBits(Type);
        // Storage must precede bitfield members for stable_sort ordering.
        Members.push_back(StorageInfo(bitsToCharUnits(StartBitOffset), Type));
      }
      Members.push_back(MemberInfo(bitsToCharUnits(StartBitOffset),
                                   MemberInfo::Field, nullptr, *Field));
    }
    return;
  }

  // Check if OffsetInRecord (the size in bits of the current run) is better
  // as a single field run. When OffsetInRecord has legal integer width, and
  // its bitfield offset is naturally aligned, it is better to make the
  // bitfield a separate storage component so as it can be accessed directly
  // with lower cost.
  auto IsBetterAsSingleFieldRun = [&](uint64_t OffsetInRecord,
                                      uint64_t StartBitOffset) {
    if (!Types.getCodeGenOpts().FineGrainedBitfieldAccesses)
      return false;
    if (OffsetInRecord < 8 || !llvm::isPowerOf2_64(OffsetInRecord) ||
        !DataLayout.fitsInLegalInteger(OffsetInRecord))
      return false;
    // Make sure StartBitOffset is naturally aligned if it is treated as an
    // IType integer.
    if (StartBitOffset %
            Context.toBits(getAlignment(getIntNType(OffsetInRecord))) !=
        0)
      return false;
    return true;
  };

  // The start field is better as a single field run.
  bool StartFieldAsSingleRun = false;
  for (;;) {
    if (Run == FieldEnd) {
      // If we're out of fields, return.
      if (Field == FieldEnd)
        break;
      // Any non-zero-length bitfield can start a new run.
      if (!Field->isZeroLengthBitField(Context)) {
        Run = Field;
        StartBitOffset = getFieldBitOffset(*Field);
        Tail = StartBitOffset + Field->getBitWidthValue(Context);
        StartFieldAsSingleRun =
            IsBetterAsSingleFieldRun(Tail - StartBitOffset, StartBitOffset);
      }
      ++Field;
      continue;
    }

    // Flush the current run when: it's better as a single-field run, a
    // zero-length bitfield breaks the run (with alignment options), or
    // the field offset is discontinuous.
    if (!StartFieldAsSingleRun && Field != FieldEnd &&
        !IsBetterAsSingleFieldRun(Tail - StartBitOffset, StartBitOffset) &&
        (!Field->isZeroLengthBitField(Context) ||
         (!Context.getTargetInfo().useZeroLengthBitfieldAlignment() &&
          !Context.getTargetInfo().useBitFieldTypeAlignment())) &&
        Tail == getFieldBitOffset(*Field)) {
      Tail += Field->getBitWidthValue(Context);
      ++Field;
      continue;
    }

    llvm::Type *Type = getIntNType(Tail - StartBitOffset);
    Members.push_back(StorageInfo(bitsToCharUnits(StartBitOffset), Type));
    for (; Run != Field; ++Run)
      Members.push_back(MemberInfo(bitsToCharUnits(StartBitOffset),
                                   MemberInfo::Field, nullptr, *Run));
    Run = FieldEnd;
    StartFieldAsSingleRun = false;
  }
}

void RecordLowering::computeVolatileBitfields() {
  if (!isAAPCS() || !Types.getCodeGenOpts().AAPCSBitfieldWidth)
    return;

  for (auto &I : BitFields) {
    const FieldDecl *Field = I.first;
    BitFieldInfo &Info = I.second;
    llvm::Type *ResLTy = Types.convertTypeForMem(Field->getType());
    // If the record alignment is less than the type width, we can't enforce a
    // aligned load, bail out.
    if ((uint64_t)(Context.toBits(Layout.getAlignment())) <
        ResLTy->getPrimitiveSizeInBits())
      continue;
    const unsigned OldOffset = Info.Offset;
    // Offset to the bit-field from the beginning of the struct.
    const unsigned AbsoluteOffset =
        Context.toBits(Info.StorageOffset) + OldOffset;

    // Container size is the width of the bit-field type.
    const unsigned StorageSize = ResLTy->getPrimitiveSizeInBits();
    // Nothing to do if the access uses the desired
    // container width and is naturally aligned.
    if (Info.StorageSize == StorageSize && (OldOffset % StorageSize == 0))
      continue;

    // Offset within the container.
    unsigned Offset = AbsoluteOffset & (StorageSize - 1);
    // Bail out if an aligned load of the container cannot cover the entire
    // bit-field. This can happen for example, if the bit-field is part of a
    // packed struct. AAPCS does not define access rules for such cases, we let
    // NeverC to follow its own rules.
    if (Offset + Info.Size > StorageSize)
      continue;

    const CharUnits StorageOffset =
        Context.toCharUnitsFromBits(AbsoluteOffset & ~(StorageSize - 1));
    const CharUnits End = StorageOffset +
                          Context.toCharUnitsFromBits(StorageSize) -
                          CharUnits::One();

    const StructRecordLayout &Layout =
        Context.getStructRecordLayout(Field->getParent());
    // Bail out if the access would reach past the record.
    const CharUnits RecordSize = Layout.getSize();
    if (End >= RecordSize)
      continue;

    // Bail out if performing this load would access non-bit-fields members.
    bool Conflict = false;
    for (const auto *F : D->fields()) {
      // Allow sized bit-fields overlaps.
      if (F->isBitField() && !F->isZeroLengthBitField(Context))
        continue;

      const CharUnits FOffset = Context.toCharUnitsFromBits(
          Layout.getFieldOffset(F->getFieldIndex()));

      // As C11 defines, a zero sized bit-field defines a barrier, so
      // fields after and before it should be race condition free.
      // The AAPCS acknowledges it and imposes no restritions when the
      // natural container overlaps a zero-length bit-field.
      if (F->isZeroLengthBitField(Context)) {
        if (End > FOffset && StorageOffset < FOffset) {
          Conflict = true;
          break;
        }
      }

      const CharUnits FEnd =
          FOffset +
          Context.toCharUnitsFromBits(
              Types.convertTypeForMem(F->getType())->getPrimitiveSizeInBits()) -
          CharUnits::One();
      if (End < FOffset || FEnd < StorageOffset)
        continue;
      Conflict = true;
      break;
    }

    if (Conflict)
      continue;
    // Write the new bit-field access parameters.
    // As the storage offset now is defined as the number of elements from the
    // start of the structure, we should divide the Offset by the element size.
    Info.VolatileStorageOffset =
        StorageOffset / Context.toCharUnitsFromBits(StorageSize).getQuantity();
    Info.VolatileStorageSize = StorageSize;
    Info.VolatileOffset = Offset;
  }
}

void RecordLowering::calculateZeroInit() {
  for (std::vector<MemberInfo>::const_iterator Member = Members.begin(),
                                               MemberEnd = Members.end();
       IsZeroInitializable && Member != MemberEnd; ++Member) {
    if (Member->Kind == MemberInfo::Field) {
      if (!Member->FD || isZeroInitializable(Member->FD))
        continue;
      IsZeroInitializable = false;
    }
  }
}

void RecordLowering::clipTailPadding() {
  std::vector<MemberInfo>::iterator Prior = Members.begin();
  CharUnits Tail = getSize(Prior->Data);
  for (std::vector<MemberInfo>::iterator Member = Prior + 1,
                                         MemberEnd = Members.end();
       Member != MemberEnd; ++Member) {
    // Only members with data and the scissor can cut into tail padding.
    if (!Member->Data && Member->Kind != MemberInfo::Scissor)
      continue;
    if (Member->Offset < Tail) {
      assert(Prior->Kind == MemberInfo::Field &&
             "Only storage fields have tail padding!");
      if (!Prior->FD || Prior->FD->isBitField())
        Prior->Data = getByteArrayType(bitsToCharUnits(llvm::alignTo(
            cast<llvm::IntegerType>(Prior->Data)->getIntegerBitWidth(), 8)));
      else {
        assert(false && "should not have reused this field's tail padding");
        Prior->Data = getByteArrayType(
            Context.getTypeInfoDataSizeInChars(Prior->FD->getType()).Width);
      }
    }
    if (Member->Data)
      Prior = Member;
    Tail = Prior->Offset + getSize(Prior->Data);
  }
}

void RecordLowering::determinePacked() {
  if (Packed)
    return;
  CharUnits Alignment = CharUnits::One();
  for (std::vector<MemberInfo>::const_iterator Member = Members.begin(),
                                               MemberEnd = Members.end();
       Member != MemberEnd; ++Member) {
    if (!Member->Data)
      continue;
    if (Member->Offset % getAlignment(Member->Data))
      Packed = true;
    Alignment = std::max(Alignment, getAlignment(Member->Data));
  }
  if (Members.back().Offset % Alignment)
    Packed = true;
  if (!Packed)
    Members.back().Data = getIntNType(Context.toBits(Alignment));
}

void RecordLowering::insertPadding() {
  std::vector<std::pair<CharUnits, CharUnits>> Padding;
  CharUnits Size = CharUnits::Zero();
  for (std::vector<MemberInfo>::const_iterator Member = Members.begin(),
                                               MemberEnd = Members.end();
       Member != MemberEnd; ++Member) {
    if (!Member->Data)
      continue;
    CharUnits Offset = Member->Offset;
    assert(Offset >= Size);
    if (Offset !=
        Size.alignTo(Packed ? CharUnits::One() : getAlignment(Member->Data)))
      Padding.push_back(std::make_pair(Size, Offset - Size));
    Size = Offset + getSize(Member->Data);
  }
  if (Padding.empty())
    return;
  for (std::vector<std::pair<CharUnits, CharUnits>>::const_iterator
           Pad = Padding.begin(),
           PadEnd = Padding.end();
       Pad != PadEnd; ++Pad)
    Members.push_back(StorageInfo(Pad->first, getByteArrayType(Pad->second)));
  llvm::stable_sort(Members);
}

void RecordLowering::fillOutputFields() {
  for (std::vector<MemberInfo>::const_iterator Member = Members.begin(),
                                               MemberEnd = Members.end();
       Member != MemberEnd; ++Member) {
    if (Member->Data)
      FieldTypes.push_back(Member->Data);
    if (Member->Kind == MemberInfo::Field) {
      if (Member->FD)
        Fields[Member->FD->getCanonicalDecl()] = FieldTypes.size() - 1;
      // A field without storage must be a bitfield.
      if (!Member->Data)
        setBitFieldInfo(Member->FD, Member->Offset, FieldTypes.back());
    }
  }
}

// ===----------------------------------------------------------------------===
// BitFieldInfo & TypeEmitter record layout
// ===----------------------------------------------------------------------===

BitFieldInfo BitFieldInfo::MakeInfo(TypeEmitter &Types, const FieldDecl *FD,
                                    uint64_t Offset, uint64_t Size,
                                    uint64_t StorageSize,
                                    CharUnits StorageOffset) {
  // This function is vestigial from RecordLayoutInfoBuilder days.
  llvm::Type *Ty = Types.convertTypeForMem(FD->getType());
  CharUnits TypeSizeInBytes =
      CharUnits::fromQuantity(Types.getDataLayout().getTypeAllocSize(Ty));
  uint64_t TypeSizeInBits = Types.getContext().toBits(TypeSizeInBytes);

  bool IsSigned = FD->getType()->isSignedIntegerOrEnumerationType();

  if (Size > TypeSizeInBits) {
    // We have a wide bit-field. The extra bits are only used for padding, so
    // if we have a bitfield of type T, with size N:
    //
    // T t : N;
    //
    // We can just assume that it's:
    //
    // T t : sizeof(T);
    //
    Size = TypeSizeInBits;
  }

  return BitFieldInfo(Offset, Size, IsSigned, StorageSize, StorageOffset);
}

std::unique_ptr<RecordLayoutInfo>
TypeEmitter::computeRecordLayout(const RecordDecl *D, llvm::StructType *Ty) {
  RecordLowering Builder(*this, D, /*Packed=*/false);

  Builder.lower();

  // Fill in the struct *after* computing the base type.  Filling in the body
  // signifies that the type is no longer opaque and record layout is complete,
  // but we may need to recursively layout D while laying D out as a base type.
  Ty->setBody(Builder.FieldTypes, Builder.Packed);

  auto RL =
      std::make_unique<RecordLayoutInfo>(Ty, (bool)Builder.IsZeroInitializable);

  RL->FieldInfo.swap(Builder.Fields);
  RL->BitFields.swap(Builder.BitFields);

#ifndef NDEBUG
  const StructRecordLayout &Layout = getContext().getStructRecordLayout(D);

  uint64_t TypeSizeInBits = getContext().toBits(Layout.getSize());
  assert(TypeSizeInBits == getDataLayout().getTypeAllocSizeInBits(Ty) &&
         "Type size mismatch!");

  llvm::StructType *ST = RL->getLLVMType();
  const llvm::StructLayout *SL = getDataLayout().getStructLayout(ST);

  const StructRecordLayout &AST_RL = getContext().getStructRecordLayout(D);
  RecordDecl::field_iterator it = D->field_begin();
  for (unsigned i = 0, e = AST_RL.getFieldCount(); i != e; ++i, ++it) {
    const FieldDecl *FD = *it;

    // Ignore zero-sized fields.
    if (FD->isZeroSize(getContext()))
      continue;

    if (!FD->isBitField()) {
      unsigned FieldNo = RL->getLLVMFieldNo(FD);
      assert(AST_RL.getFieldOffset(i) == SL->getElementOffsetInBits(FieldNo) &&
             "Invalid field offset!");
      continue;
    }

    if (!FD->getDeclName())
      continue;

    const BitFieldInfo &Info = RL->getBitFieldInfo(FD);
    llvm::Type *ElementTy = ST->getTypeAtIndex(RL->getLLVMFieldNo(FD));

    // Unions have overlapping elements dictating their layout, but for
    // non-unions we can verify that this section of the layout is the exact
    // expected size.
    if (D->isUnion()) {
      assert(Info.Offset == 0 &&
             "Little endian union bitfield with a non-zero offset");
      assert(Info.StorageSize <= SL->getSizeInBits() &&
             "Union not large enough for bitfield storage");
    } else {
      assert((Info.StorageSize ==
                  getDataLayout().getTypeAllocSizeInBits(ElementTy) ||
              Info.VolatileStorageSize ==
                  getDataLayout().getTypeAllocSizeInBits(ElementTy)) &&
             "Storage size does not match the element type size");
    }
    assert(Info.Size > 0 && "Empty bitfield!");
    assert(static_cast<unsigned>(Info.Offset) + Info.Size <= Info.StorageSize &&
           "Bitfield outside of its allocated storage");
  }
#endif

  return RL;
}

// ===----------------------------------------------------------------------===
// Debug printing
// ===----------------------------------------------------------------------===

void RecordLayoutInfo::print(llvm::raw_ostream &OS) const {
  OS << "<RecordLayoutInfo\n";
  OS << "  LLVMType:" << *CompleteObjectType << "\n";
  OS << "  IsZeroInitializable:" << IsZeroInitializable << "\n";
  OS << "  BitFields:[\n";

  // Sort by declaration order.
  std::vector<std::pair<unsigned, const BitFieldInfo *>> BFIs;
  for (llvm::DenseMap<const FieldDecl *, BitFieldInfo>::const_iterator
           it = BitFields.begin(),
           ie = BitFields.end();
       it != ie; ++it) {
    const RecordDecl *RD = it->first->getParent();
    unsigned Index = 0;
    for (RecordDecl::field_iterator it2 = RD->field_begin(); *it2 != it->first;
         ++it2)
      ++Index;
    BFIs.push_back(std::make_pair(Index, &it->second));
  }
  llvm::array_pod_sort(BFIs.begin(), BFIs.end());
  for (unsigned i = 0, e = BFIs.size(); i != e; ++i) {
    OS.indent(4);
    BFIs[i].second->print(OS);
    OS << "\n";
  }

  OS << "]>\n";
}

LLVM_DUMP_METHOD void RecordLayoutInfo::dump() const { print(llvm::errs()); }

void BitFieldInfo::print(llvm::raw_ostream &OS) const {
  OS << "<BitFieldInfo"
     << " Offset:" << Offset << " Size:" << Size << " IsSigned:" << IsSigned
     << " StorageSize:" << StorageSize
     << " StorageOffset:" << StorageOffset.getQuantity()
     << " VolatileOffset:" << VolatileOffset
     << " VolatileStorageSize:" << VolatileStorageSize
     << " VolatileStorageOffset:" << VolatileStorageOffset.getQuantity() << ">";
}

LLVM_DUMP_METHOD void BitFieldInfo::dump() const { print(llvm::errs()); }
