#include "Core/ConstantEmitter.h"
#include "ABI/EmitterABI.h"
#include "ABI/TargetInfo.h"
#include "Core/FunctionEmitter.h"
#include "Core/ModuleEmitter.h"
#include "Core/RecordLayoutInfo.h"
#include "neverc/Foundation/Builtin/Builtins.h"
#include "neverc/Tree/Core/APValue.h"
#include "neverc/Tree/Core/Attr.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Stmt/StmtVisitor.h"
#include "neverc/Tree/Type/StructLayout.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Sequence.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include <optional>
using namespace neverc;
using namespace Emit;

// ===----------------------------------------------------------------------===
// Internal constant builders
// ===----------------------------------------------------------------------===

namespace {
class ConstExprEmitter;

struct ConstantAggregateBuilderUtils {
  ModuleEmitter &ME;

  ConstantAggregateBuilderUtils(ModuleEmitter &ME) : ME(ME) {}

  CharUnits getAlignment(const llvm::Constant *C) const {
    return CharUnits::fromQuantity(
        ME.getDataLayout().getABITypeAlign(C->getType()));
  }

  CharUnits getSize(llvm::Type *Ty) const {
    return CharUnits::fromQuantity(ME.getDataLayout().getTypeAllocSize(Ty));
  }

  CharUnits getSize(const llvm::Constant *C) const {
    return getSize(C->getType());
  }

  llvm::Constant *getPadding(CharUnits PadSize) const {
    llvm::Type *Ty = ME.CharTy;
    if (PadSize > CharUnits::One())
      Ty = llvm::ArrayType::get(Ty, PadSize.getQuantity());
    return llvm::UndefValue::get(Ty);
  }

  llvm::Constant *getZeroes(CharUnits ZeroSize) const {
    llvm::Type *Ty = llvm::ArrayType::get(ME.CharTy, ZeroSize.getQuantity());
    return llvm::ConstantAggregateZero::get(Ty);
  }
};

class ConstantAggregateBuilder : private ConstantAggregateBuilderUtils {
  llvm::SmallVector<llvm::Constant *, 32> Elems;
  llvm::SmallVector<CharUnits, 32> Offsets;

  CharUnits Size = CharUnits::Zero();

  bool NaturalLayout = true;

  bool split(size_t Index, CharUnits Hint);
  std::optional<size_t> splitAt(CharUnits Pos);

  static llvm::Constant *buildFrom(ModuleEmitter &ME,
                                   llvm::ArrayRef<llvm::Constant *> Elems,
                                   llvm::ArrayRef<CharUnits> Offsets,
                                   CharUnits StartOffset, CharUnits Size,
                                   bool NaturalLayout, llvm::Type *DesiredTy,
                                   bool AllowOversized);

public:
  ConstantAggregateBuilder(ModuleEmitter &ME)
      : ConstantAggregateBuilderUtils(ME) {}

  bool add(llvm::Constant *C, CharUnits Offset, bool AllowOverwrite);

  bool addBits(llvm::APInt Bits, uint64_t OffsetInBits, bool AllowOverwrite);

  void condense(CharUnits Offset, llvm::Type *DesiredTy);

  llvm::Constant *build(llvm::Type *DesiredTy, bool AllowOversized) const {
    return buildFrom(ME, Elems, Offsets, CharUnits::Zero(), Size, NaturalLayout,
                     DesiredTy, AllowOversized);
  }
};

template <typename Container, typename Range = std::initializer_list<
                                  typename Container::value_type>>
void replace(Container &C, size_t BeginOff, size_t EndOff, Range Vals) {
  assert(BeginOff <= EndOff && "invalid replacement range");
  llvm::replace(C, C.begin() + BeginOff, C.begin() + EndOff, Vals);
}

bool ConstantAggregateBuilder::add(llvm::Constant *C, CharUnits Offset,
                                   bool AllowOverwrite) {
  // Common case: appending to a layout.
  if (Offset >= Size) {
    CharUnits Align = getAlignment(C);
    CharUnits AlignedSize = Size.alignTo(Align);
    if (AlignedSize > Offset || Offset.alignTo(Align) != Offset)
      NaturalLayout = false;
    else if (AlignedSize < Offset) {
      Elems.push_back(getPadding(Offset - Size));
      Offsets.push_back(Size);
    }
    Elems.push_back(C);
    Offsets.push_back(Offset);
    Size = Offset + getSize(C);
    return true;
  }

  // Uncommon case: constant overlaps what we've already created.
  std::optional<size_t> FirstElemToReplace = splitAt(Offset);
  if (!FirstElemToReplace)
    return false;

  CharUnits CSize = getSize(C);
  std::optional<size_t> LastElemToReplace = splitAt(Offset + CSize);
  if (!LastElemToReplace)
    return false;

  assert((FirstElemToReplace == LastElemToReplace || AllowOverwrite) &&
         "unexpectedly overwriting field");

  replace(Elems, *FirstElemToReplace, *LastElemToReplace, {C});
  replace(Offsets, *FirstElemToReplace, *LastElemToReplace, {Offset});
  Size = std::max(Size, Offset + CSize);
  NaturalLayout = false;
  return true;
}

bool ConstantAggregateBuilder::addBits(llvm::APInt Bits, uint64_t OffsetInBits,
                                       bool AllowOverwrite) {
  const TreeContext &Context = ME.getContext();
  const uint64_t CharWidth = ME.getContext().getCharWidth();

  // Offset of where we want the first bit to go within the bits of the
  // current char.
  unsigned OffsetWithinChar = OffsetInBits % CharWidth;

  // We split bit-fields up into individual bytes. Walk over the bytes and
  // update them.
  for (CharUnits OffsetInChars =
           Context.toCharUnitsFromBits(OffsetInBits - OffsetWithinChar);
       /**/; ++OffsetInChars) {
    // Number of bits we want to fill in this char.
    unsigned WantedBits =
        std::min((uint64_t)Bits.getBitWidth(), CharWidth - OffsetWithinChar);

    // Get a char containing the bits we want in the right places. The other
    // bits have unspecified values.
    llvm::APInt BitsThisChar = Bits;
    if (BitsThisChar.getBitWidth() < CharWidth)
      BitsThisChar = BitsThisChar.zext(CharWidth);
    BitsThisChar = BitsThisChar.shl(OffsetWithinChar);
    if (BitsThisChar.getBitWidth() > CharWidth)
      BitsThisChar = BitsThisChar.trunc(CharWidth);

    if (WantedBits == CharWidth) {
      // Got a full byte: just add it directly.
      add(llvm::ConstantInt::get(ME.getLLVMContext(), BitsThisChar),
          OffsetInChars, AllowOverwrite);
    } else {
      // Partial byte: update the existing integer if there is one. If we
      // can't split out a 1-CharUnit range to update, then we can't add
      // these bits and fail the entire constant emission.
      std::optional<size_t> FirstElemToUpdate = splitAt(OffsetInChars);
      if (!FirstElemToUpdate)
        return false;
      std::optional<size_t> LastElemToUpdate =
          splitAt(OffsetInChars + CharUnits::One());
      if (!LastElemToUpdate)
        return false;
      assert(*LastElemToUpdate - *FirstElemToUpdate < 2 &&
             "should have at most one element covering one byte");

      llvm::APInt UpdateMask(CharWidth, 0);
      UpdateMask.setBits(OffsetWithinChar, OffsetWithinChar + WantedBits);
      BitsThisChar &= UpdateMask;

      if (*FirstElemToUpdate == *LastElemToUpdate ||
          Elems[*FirstElemToUpdate]->isNullValue() ||
          isa<llvm::UndefValue>(Elems[*FirstElemToUpdate])) {
        // All existing bits are either zero or undef.
        add(llvm::ConstantInt::get(ME.getLLVMContext(), BitsThisChar),
            OffsetInChars, /*AllowOverwrite*/ true);
      } else {
        llvm::Constant *&ToUpdate = Elems[*FirstElemToUpdate];
        // In order to perform a partial update, we need the existing bitwise
        // value, which we can only extract for a constant int.
        auto *CI = dyn_cast<llvm::ConstantInt>(ToUpdate);
        if (!CI)
          return false;
        // Because this is a 1-CharUnit range, the constant occupying it must
        // be exactly one CharUnit wide.
        assert(CI->getBitWidth() == CharWidth && "splitAt failed");
        assert((!(CI->getValue() & UpdateMask) || AllowOverwrite) &&
               "unexpectedly overwriting bitfield");
        BitsThisChar |= (CI->getValue() & ~UpdateMask);
        ToUpdate = llvm::ConstantInt::get(ME.getLLVMContext(), BitsThisChar);
      }
    }

    // Stop if we've added all the bits.
    if (WantedBits == Bits.getBitWidth())
      break;

    Bits.lshrInPlace(WantedBits);
    Bits = Bits.trunc(Bits.getBitWidth() - WantedBits);
    OffsetWithinChar = 0;
  }

  return true;
}

std::optional<size_t> ConstantAggregateBuilder::splitAt(CharUnits Pos) {
  if (Pos >= Size)
    return Offsets.size();

  while (true) {
    auto FirstAfterPos = llvm::upper_bound(Offsets, Pos);
    if (FirstAfterPos == Offsets.begin())
      return 0;

    // If we already have an element starting at Pos, we're done.
    size_t LastAtOrBeforePosIndex = FirstAfterPos - Offsets.begin() - 1;
    if (Offsets[LastAtOrBeforePosIndex] == Pos)
      return LastAtOrBeforePosIndex;

    // We found an element starting before Pos. Check for overlap.
    if (Offsets[LastAtOrBeforePosIndex] +
            getSize(Elems[LastAtOrBeforePosIndex]) <=
        Pos)
      return LastAtOrBeforePosIndex + 1;

    // Try to decompose it into smaller constants.
    if (!split(LastAtOrBeforePosIndex, Pos))
      return std::nullopt;
  }
}

bool ConstantAggregateBuilder::split(size_t Index, CharUnits Hint) {
  NaturalLayout = false;
  llvm::Constant *C = Elems[Index];
  CharUnits Offset = Offsets[Index];

  if (auto *CA = dyn_cast<llvm::ConstantAggregate>(C)) {
    // Expand the sequence into its contained elements.
    replace(Elems, Index, Index + 1,
            llvm::map_range(llvm::seq(0u, CA->getNumOperands()),
                            [&](unsigned Op) { return CA->getOperand(Op); }));
    if (isa<llvm::ArrayType>(CA->getType()) ||
        isa<llvm::VectorType>(CA->getType())) {
      // Array or vector.
      llvm::Type *ElemTy =
          llvm::GetElementPtrInst::getTypeAtIndex(CA->getType(), (uint64_t)0);
      CharUnits ElemSize = getSize(ElemTy);
      replace(
          Offsets, Index, Index + 1,
          llvm::map_range(llvm::seq(0u, CA->getNumOperands()),
                          [&](unsigned Op) { return Offset + Op * ElemSize; }));
    } else {
      // Must be a struct.
      auto *ST = cast<llvm::StructType>(CA->getType());
      const llvm::StructLayout *Layout = ME.getDataLayout().getStructLayout(ST);
      replace(Offsets, Index, Index + 1,
              llvm::map_range(
                  llvm::seq(0u, CA->getNumOperands()), [&](unsigned Op) {
                    return Offset + CharUnits::fromQuantity(
                                        Layout->getElementOffset(Op));
                  }));
    }
    return true;
  }

  if (auto *CDS = dyn_cast<llvm::ConstantDataSequential>(C)) {
    // Expand the sequence into its contained elements.
    CharUnits ElemSize = getSize(CDS->getElementType());
    replace(Elems, Index, Index + 1,
            llvm::map_range(llvm::seq(0u, CDS->getNumElements()),
                            [&](unsigned Elem) {
                              return CDS->getElementAsConstant(Elem);
                            }));
    replace(Offsets, Index, Index + 1,
            llvm::map_range(
                llvm::seq(0u, CDS->getNumElements()),
                [&](unsigned Elem) { return Offset + Elem * ElemSize; }));
    return true;
  }

  if (isa<llvm::ConstantAggregateZero>(C)) {
    // Split into two zeros at the hinted offset.
    CharUnits ElemSize = getSize(C);
    assert(Hint > Offset && Hint < Offset + ElemSize && "nothing to split");
    replace(Elems, Index, Index + 1,
            {getZeroes(Hint - Offset), getZeroes(Offset + ElemSize - Hint)});
    replace(Offsets, Index, Index + 1, {Offset, Hint});
    return true;
  }

  if (isa<llvm::UndefValue>(C)) {
    // Drop undef; it doesn't contribute to the final layout.
    replace(Elems, Index, Index + 1, {});
    replace(Offsets, Index, Index + 1, {});
    return true;
  }

  return false;
}

llvm::Constant *
genArrayConstant(ModuleEmitter &ME, llvm::ArrayType *DesiredType,
                 llvm::Type *CommonElementType, unsigned ArrayBound,
                 llvm::SmallVectorImpl<llvm::Constant *> &Elements,
                 llvm::Constant *Filler);

llvm::Constant *ConstantAggregateBuilder::buildFrom(
    ModuleEmitter &ME, llvm::ArrayRef<llvm::Constant *> Elems,
    llvm::ArrayRef<CharUnits> Offsets, CharUnits StartOffset, CharUnits Size,
    bool NaturalLayout, llvm::Type *DesiredTy, bool AllowOversized) {
  ConstantAggregateBuilderUtils Utils(ME);

  if (Elems.empty())
    return llvm::UndefValue::get(DesiredTy);

  auto Offset = [&](size_t I) { return Offsets[I] - StartOffset; };

  // If we want an array type, see if all the elements are the same type and
  // appropriately spaced.
  if (llvm::ArrayType *ATy = dyn_cast<llvm::ArrayType>(DesiredTy)) {
    assert(!AllowOversized && "oversized array emission not supported");

    bool CanEmitArray = true;
    llvm::Type *CommonType = Elems[0]->getType();
    llvm::Constant *Filler = llvm::Constant::getNullValue(CommonType);
    CharUnits ElemSize = Utils.getSize(ATy->getElementType());
    llvm::SmallVector<llvm::Constant *, 32> ArrayElements;
    for (size_t I = 0; I != Elems.size(); ++I) {
      // Skip zeroes; we'll use a zero value as our array filler.
      if (Elems[I]->isNullValue())
        continue;

      // All remaining elements must be the same type.
      if (Elems[I]->getType() != CommonType || Offset(I) % ElemSize != 0) {
        CanEmitArray = false;
        break;
      }
      ArrayElements.resize(Offset(I) / ElemSize + 1, Filler);
      ArrayElements.back() = Elems[I];
    }

    if (CanEmitArray) {
      return genArrayConstant(ME, ATy, CommonType, ATy->getNumElements(),
                              ArrayElements, Filler);
    }

    // Can't emit as an array, carry on to emit as a struct.
  }

  // The size of the constant we plan to generate.  This is usually just
  // the size of the initialized type, but in AllowOversized mode (i.e.
  // flexible array init), it can be larger.
  CharUnits DesiredSize = Utils.getSize(DesiredTy);
  if (Size > DesiredSize) {
    assert(AllowOversized && "Elems are oversized");
    DesiredSize = Size;
  }

  // The natural alignment of an unpacked LLVM struct with the given elements.
  CharUnits Align = CharUnits::One();
  for (llvm::Constant *C : Elems)
    Align = std::max(Align, Utils.getAlignment(C));

  // The natural size of an unpacked LLVM struct with the given elements.
  CharUnits AlignedSize = Size.alignTo(Align);

  bool Packed = false;
  llvm::ArrayRef<llvm::Constant *> UnpackedElems = Elems;
  llvm::SmallVector<llvm::Constant *, 32> UnpackedElemStorage;
  if (DesiredSize < AlignedSize || DesiredSize.alignTo(Align) != DesiredSize) {
    // The natural layout would be too big; force use of a packed layout.
    NaturalLayout = false;
    Packed = true;
  } else if (DesiredSize > AlignedSize) {
    // The natural layout would be too small. Add padding to fix it. (This
    // is ignored if we choose a packed layout.)
    UnpackedElemStorage.assign(Elems.begin(), Elems.end());
    UnpackedElemStorage.push_back(Utils.getPadding(DesiredSize - Size));
    UnpackedElems = UnpackedElemStorage;
  }

  // If we don't have a natural layout, insert padding as necessary.
  // As we go, double-check to see if we can actually just emit Elems
  // as a non-packed struct and do so opportunistically if possible.
  llvm::SmallVector<llvm::Constant *, 32> PackedElems;
  if (!NaturalLayout) {
    CharUnits SizeSoFar = CharUnits::Zero();
    for (size_t I = 0; I != Elems.size(); ++I) {
      CharUnits Align = Utils.getAlignment(Elems[I]);
      CharUnits NaturalOffset = SizeSoFar.alignTo(Align);
      CharUnits DesiredOffset = Offset(I);
      assert(DesiredOffset >= SizeSoFar && "elements out of order");

      if (DesiredOffset != NaturalOffset)
        Packed = true;
      if (DesiredOffset != SizeSoFar)
        PackedElems.push_back(Utils.getPadding(DesiredOffset - SizeSoFar));
      PackedElems.push_back(Elems[I]);
      SizeSoFar = DesiredOffset + Utils.getSize(Elems[I]);
    }
    // If we're using the packed layout, pad it out to the desired size if
    // necessary.
    if (Packed) {
      assert(SizeSoFar <= DesiredSize &&
             "requested size is too small for contents");
      if (SizeSoFar < DesiredSize)
        PackedElems.push_back(Utils.getPadding(DesiredSize - SizeSoFar));
    }
  }

  llvm::StructType *STy = llvm::ConstantStruct::getTypeForElements(
      ME.getLLVMContext(), Packed ? PackedElems : UnpackedElems, Packed);

  // Pick the type to use.  If the type is layout identical to the desired
  // type then use it, otherwise use whatever the builder produced for us.
  if (llvm::StructType *DesiredSTy = dyn_cast<llvm::StructType>(DesiredTy)) {
    if (DesiredSTy->isLayoutIdentical(STy))
      STy = DesiredSTy;
  }

  return llvm::ConstantStruct::get(STy, Packed ? PackedElems : UnpackedElems);
}

void ConstantAggregateBuilder::condense(CharUnits Offset,
                                        llvm::Type *DesiredTy) {
  CharUnits Size = getSize(DesiredTy);

  std::optional<size_t> FirstElemToReplace = splitAt(Offset);
  if (!FirstElemToReplace)
    return;
  size_t First = *FirstElemToReplace;

  std::optional<size_t> LastElemToReplace = splitAt(Offset + Size);
  if (!LastElemToReplace)
    return;
  size_t Last = *LastElemToReplace;

  size_t Length = Last - First;
  if (Length == 0)
    return;

  if (Length == 1 && Offsets[First] == Offset &&
      getSize(Elems[First]) == Size) {
    // Re-wrap single element structs if necessary. Otherwise, leave any single
    // element constant of the right size alone even if it has the wrong type.
    auto *STy = dyn_cast<llvm::StructType>(DesiredTy);
    if (STy && STy->getNumElements() == 1 &&
        STy->getElementType(0) == Elems[First]->getType())
      Elems[First] = llvm::ConstantStruct::get(STy, Elems[First]);
    return;
  }

  llvm::Constant *Replacement = buildFrom(
      ME, llvm::ArrayRef(Elems).slice(First, Length),
      llvm::ArrayRef(Offsets).slice(First, Length), Offset, getSize(DesiredTy),
      /*known to have natural layout=*/false, DesiredTy, false);
  replace(Elems, First, Last, {Replacement});
  replace(Offsets, First, Last, {Offset});
}

class ConstStructBuilder {
  ModuleEmitter &ME;
  ConstantEmitter &Emitter;
  ConstantAggregateBuilder &Builder;
  CharUnits StartOffset;

public:
  static llvm::Constant *FormStruct(ConstantEmitter &Emitter, InitListExpr *ILE,
                                    QualType StructTy);
  static llvm::Constant *FormStruct(ConstantEmitter &Emitter,
                                    const APValue &Value, QualType ValTy);
  static bool UpdateStruct(ConstantEmitter &Emitter,
                           ConstantAggregateBuilder &Const, CharUnits Offset,
                           InitListExpr *Updater);

private:
  ConstStructBuilder(ConstantEmitter &Emitter,
                     ConstantAggregateBuilder &Builder, CharUnits StartOffset)
      : ME(Emitter.ME), Emitter(Emitter), Builder(Builder),
        StartOffset(StartOffset) {}

  bool AppendField(const FieldDecl *Field, uint64_t FieldOffset,
                   llvm::Constant *InitExpr, bool AllowOverwrite = false);

  bool AppendBytes(CharUnits FieldOffsetInChars, llvm::Constant *InitCst,
                   bool AllowOverwrite = false);

  bool AppendBitField(const FieldDecl *Field, uint64_t FieldOffset,
                      llvm::ConstantInt *InitExpr, bool AllowOverwrite = false);

  bool Build(InitListExpr *ILE, bool AllowOverwrite);
  bool Build(const APValue &Val, const RecordDecl *RD, CharUnits BaseOffset);
  llvm::Constant *Finalize(QualType Ty);
};

bool ConstStructBuilder::AppendField(const FieldDecl *Field,
                                     uint64_t FieldOffset,
                                     llvm::Constant *InitCst,
                                     bool AllowOverwrite) {
  const TreeContext &Context = ME.getContext();

  CharUnits FieldOffsetInChars = Context.toCharUnitsFromBits(FieldOffset);

  return AppendBytes(FieldOffsetInChars, InitCst, AllowOverwrite);
}

bool ConstStructBuilder::AppendBytes(CharUnits FieldOffsetInChars,
                                     llvm::Constant *InitCst,
                                     bool AllowOverwrite) {
  return Builder.add(InitCst, StartOffset + FieldOffsetInChars, AllowOverwrite);
}

bool ConstStructBuilder::AppendBitField(const FieldDecl *Field,
                                        uint64_t FieldOffset,
                                        llvm::ConstantInt *CI,
                                        bool AllowOverwrite) {
  const RecordLayoutInfo &RL =
      ME.getTypes().getRecordLayoutInfo(Field->getParent());
  const BitFieldInfo &Info = RL.getBitFieldInfo(Field);
  llvm::APInt FieldValue = CI->getValue();

  // Promote the size of FieldValue if necessary
  if (Info.Size > FieldValue.getBitWidth())
    FieldValue = FieldValue.zext(Info.Size);

  // Truncate the size of FieldValue to the bit field size.
  if (Info.Size < FieldValue.getBitWidth())
    FieldValue = FieldValue.trunc(Info.Size);

  return Builder.addBits(FieldValue,
                         ME.getContext().toBits(StartOffset) + FieldOffset,
                         AllowOverwrite);
}

bool genDesignatedInitUpdater(ConstantEmitter &Emitter,
                              ConstantAggregateBuilder &Const, CharUnits Offset,
                              QualType Type, InitListExpr *Updater) {
  if (Type->isRecordType())
    return ConstStructBuilder::UpdateStruct(Emitter, Const, Offset, Updater);

  auto CAT = Emitter.ME.getContext().getAsConstantArrayType(Type);
  if (!CAT)
    return false;
  QualType ElemType = CAT->getElementType();
  CharUnits ElemSize = Emitter.ME.getContext().getTypeSizeInChars(ElemType);
  llvm::Type *ElemTy = Emitter.ME.getTypes().convertTypeForMem(ElemType);

  llvm::Constant *FillC = nullptr;
  if (Expr *Filler = Updater->getArrayFiller()) {
    if (!isa<NoInitExpr>(Filler)) {
      FillC = Emitter.tryEmitAbstractForMemory(Filler, ElemType);
      if (!FillC)
        return false;
    }
  }

  unsigned NumElementsToUpdate =
      FillC ? CAT->getSize().getZExtValue() : Updater->getNumInits();
  for (unsigned I = 0; I != NumElementsToUpdate; ++I, Offset += ElemSize) {
    Expr *Init = nullptr;
    if (I < Updater->getNumInits())
      Init = Updater->getInit(I);

    if (!Init && FillC) {
      if (!Const.add(FillC, Offset, true))
        return false;
    } else if (!Init || isa<NoInitExpr>(Init)) {
      continue;
    } else if (InitListExpr *ChildILE = dyn_cast<InitListExpr>(Init)) {
      if (!genDesignatedInitUpdater(Emitter, Const, Offset, ElemType, ChildILE))
        return false;
      // Attempt to reduce the array element to a single constant if necessary.
      Const.condense(Offset, ElemTy);
    } else {
      llvm::Constant *Val = Emitter.tryEmitPrivateForMemory(Init, ElemType);
      if (!Const.add(Val, Offset, true))
        return false;
    }
  }

  return true;
}

bool ConstStructBuilder::Build(InitListExpr *ILE, bool AllowOverwrite) {
  RecordDecl *RD = ILE->getType()->castAs<RecordType>()->getDecl();
  const StructRecordLayout &Layout = ME.getContext().getStructRecordLayout(RD);

  unsigned FieldNo = -1;
  unsigned ElementNo = 0;

  for (FieldDecl *Field : RD->fields()) {
    ++FieldNo;

    // If this is a union, skip all the fields that aren't being initialized.
    if (RD->isUnion() &&
        !declaresSameEntity(ILE->getInitializedFieldInUnion(), Field))
      continue;

    // Don't emit anonymous bitfields.
    if (Field->isUnnamedBitfield())
      continue;

    // Get the initializer.  A struct can include fields without initializers,
    // we just use explicit null values for them.
    Expr *Init = nullptr;
    if (ElementNo < ILE->getNumInits())
      Init = ILE->getInit(ElementNo++);
    if (Init && isa<NoInitExpr>(Init))
      continue;

    // Zero-sized fields are not emitted, but their initializers may still
    // prevent emission of this struct as a constant.
    if (Field->isZeroSize(ME.getContext())) {
      if (Init->HasSideEffects(ME.getContext()))
        return false;
      continue;
    }

    // When emitting a DesignatedInitUpdateExpr, a nested InitListExpr
    // represents additional overwriting of our current constant value, and not
    // a new constant to emit independently.
    if (AllowOverwrite &&
        (Field->getType()->isArrayType() || Field->getType()->isRecordType())) {
      if (auto *SubILE = dyn_cast<InitListExpr>(Init)) {
        CharUnits Offset =
            ME.getContext().toCharUnitsFromBits(Layout.getFieldOffset(FieldNo));
        if (!genDesignatedInitUpdater(Emitter, Builder, StartOffset + Offset,
                                      Field->getType(), SubILE))
          return false;
        // If we split apart the field's value, try to collapse it down to a
        // single value now.
        Builder.condense(StartOffset + Offset,
                         ME.getTypes().convertTypeForMem(Field->getType()));
        continue;
      }
    }

    llvm::Constant *EltInit =
        Init ? Emitter.tryEmitPrivateForMemory(Init, Field->getType())
             : Emitter.emitNullForMemory(Field->getType());
    if (!EltInit)
      return false;

    if (!Field->isBitField()) {
      if (!AppendField(Field, Layout.getFieldOffset(FieldNo), EltInit,
                       AllowOverwrite))
        return false;
    } else {
      if (auto *CI = dyn_cast<llvm::ConstantInt>(EltInit)) {
        if (!AppendBitField(Field, Layout.getFieldOffset(FieldNo), CI,
                            AllowOverwrite))
          return false;
      } else {
        return false;
      }
    }
  }

  return true;
}

bool ConstStructBuilder::Build(const APValue &Val, const RecordDecl *RD,
                               CharUnits Offset) {
  const StructRecordLayout &Layout = ME.getContext().getStructRecordLayout(RD);

  unsigned FieldNo = 0;
  uint64_t OffsetBits = ME.getContext().toBits(Offset);

  bool AllowOverwrite = false;
  for (RecordDecl::field_iterator Field = RD->field_begin(),
                                  FieldEnd = RD->field_end();
       Field != FieldEnd; ++Field, ++FieldNo) {
    // If this is a union, skip all the fields that aren't being initialized.
    if (RD->isUnion() && !declaresSameEntity(Val.getUnionField(), *Field))
      continue;

    // Don't emit anonymous bitfields or zero-sized fields.
    if (Field->isUnnamedBitfield() || Field->isZeroSize(ME.getContext()))
      continue;

    const APValue &FieldValue =
        RD->isUnion() ? Val.getUnionValue() : Val.getStructField(FieldNo);
    llvm::Constant *EltInit =
        Emitter.tryEmitPrivateForMemory(FieldValue, Field->getType());
    if (!EltInit)
      return false;

    if (!Field->isBitField()) {
      if (!AppendField(*Field, Layout.getFieldOffset(FieldNo) + OffsetBits,
                       EltInit, AllowOverwrite))
        return false;
    } else {
      if (!AppendBitField(*Field, Layout.getFieldOffset(FieldNo) + OffsetBits,
                          cast<llvm::ConstantInt>(EltInit), AllowOverwrite))
        return false;
    }
  }

  return true;
}

llvm::Constant *ConstStructBuilder::Finalize(QualType Type) {
  RecordDecl *RD = Type->castAs<RecordType>()->getDecl();
  llvm::Type *ValTy = ME.getTypes().convertType(Type);
  return Builder.build(ValTy, RD->hasFlexibleArrayMember());
}

llvm::Constant *ConstStructBuilder::FormStruct(ConstantEmitter &Emitter,
                                               InitListExpr *ILE,
                                               QualType ValTy) {
  ConstantAggregateBuilder Const(Emitter.ME);
  ConstStructBuilder Builder(Emitter, Const, CharUnits::Zero());

  if (!Builder.Build(ILE, /*AllowOverwrite*/ false))
    return nullptr;

  return Builder.Finalize(ValTy);
}

llvm::Constant *ConstStructBuilder::FormStruct(ConstantEmitter &Emitter,
                                               const APValue &Val,
                                               QualType ValTy) {
  ConstantAggregateBuilder Const(Emitter.ME);
  ConstStructBuilder Builder(Emitter, Const, CharUnits::Zero());

  const RecordDecl *RD = ValTy->castAs<RecordType>()->getDecl();
  if (!Builder.Build(Val, RD, CharUnits::Zero()))
    return nullptr;

  return Builder.Finalize(ValTy);
}

bool ConstStructBuilder::UpdateStruct(ConstantEmitter &Emitter,
                                      ConstantAggregateBuilder &Const,
                                      CharUnits Offset, InitListExpr *Updater) {
  return ConstStructBuilder(Emitter, Const, Offset)
      .Build(Updater, /*AllowOverwrite*/ true);
}

ConstantAddress tryEmitGlobalCompoundLiteral(ConstantEmitter &emitter,
                                             const CompoundLiteralExpr *E) {
  ModuleEmitter &ME = emitter.ME;
  CharUnits Align = ME.getContext().getTypeAlignInChars(E->getType());
  if (llvm::GlobalVariable *Addr =
          ME.getAddrOfConstantCompoundLiteralIfEmitted(E))
    return ConstantAddress(Addr, Addr->getValueType(), Align);

  LangAS addressSpace = E->getType().getAddressSpace();
  llvm::Constant *C = emitter.tryEmitForInitializer(E->getInitializer(),
                                                    addressSpace, E->getType());
  if (!C) {
    assert(!E->isFileScope() &&
           "file-scope compound literal did not have constant initializer!");
    return ConstantAddress::invalid();
  }

  auto GV = new llvm::GlobalVariable(
      ME.getModule(), C->getType(),
      E->getType().isConstantStorage(ME.getContext(), true, false),
      llvm::GlobalValue::InternalLinkage, C, ".compoundliteral", nullptr,
      llvm::GlobalVariable::NotThreadLocal,
      ME.getContext().getTargetAddressSpace(addressSpace));
  emitter.finalize(GV);
  GV->setAlignment(Align.getAsAlign());
  ME.setAddrOfConstantCompoundLiteral(E, GV);
  return ConstantAddress(GV, GV->getValueType(), Align);
}

// ===----------------------------------------------------------------------===
// Constant expression visitor
// ===----------------------------------------------------------------------===

llvm::Constant *
genArrayConstant(ModuleEmitter &ME, llvm::ArrayType *DesiredType,
                 llvm::Type *CommonElementType, unsigned ArrayBound,
                 llvm::SmallVectorImpl<llvm::Constant *> &Elements,
                 llvm::Constant *Filler) {
  // Figure out how long the initial prefix of non-zero elements is.
  unsigned NonzeroLength = ArrayBound;
  if (Elements.size() < NonzeroLength && Filler->isNullValue())
    NonzeroLength = Elements.size();
  if (NonzeroLength == Elements.size()) {
    while (NonzeroLength > 0 && Elements[NonzeroLength - 1]->isNullValue())
      --NonzeroLength;
  }

  if (NonzeroLength == 0)
    return llvm::ConstantAggregateZero::get(DesiredType);

  // Add a zeroinitializer array filler if we have lots of trailing zeroes.
  unsigned TrailingZeroes = ArrayBound - NonzeroLength;
  if (TrailingZeroes >= 8) {
    assert(Elements.size() >= NonzeroLength &&
           "missing initializer for non-zero element");

    // If all the elements had the same type up to the trailing zeroes, emit a
    // struct of two arrays (the nonzero data and the zeroinitializer).
    if (CommonElementType && NonzeroLength >= 8) {
      llvm::Constant *Initial = llvm::ConstantArray::get(
          llvm::ArrayType::get(CommonElementType, NonzeroLength),
          llvm::ArrayRef(Elements).take_front(NonzeroLength));
      Elements.resize(2);
      Elements[0] = Initial;
    } else {
      Elements.resize(NonzeroLength + 1);
    }

    auto *FillerType =
        CommonElementType ? CommonElementType : DesiredType->getElementType();
    FillerType = llvm::ArrayType::get(FillerType, TrailingZeroes);
    Elements.back() = llvm::ConstantAggregateZero::get(FillerType);
    CommonElementType = nullptr;
  } else if (Elements.size() != ArrayBound) {
    // Otherwise pad to the right size with the filler if necessary.
    Elements.resize(ArrayBound, Filler);
    if (Filler->getType() != CommonElementType)
      CommonElementType = nullptr;
  }

  if (CommonElementType)
    return llvm::ConstantArray::get(
        llvm::ArrayType::get(CommonElementType, ArrayBound), Elements);

  llvm::SmallVector<llvm::Type *, 16> Types;
  Types.reserve(Elements.size());
  for (llvm::Constant *Elt : Elements)
    Types.push_back(Elt->getType());
  llvm::StructType *SType =
      llvm::StructType::get(ME.getLLVMContext(), Types, true);
  return llvm::ConstantStruct::get(SType, Elements);
}

// Emits constants for structs and unions (and similar aggregates). Other
// scalars are handled by the general constant-folding path.
//
// Constant folding is currently missing support for a few features supported
// here: CK_ToUnion and DesignatedInitUpdateExpr.
class ConstExprEmitter
    : public StmtVisitor<ConstExprEmitter, llvm::Constant *, QualType> {
  ModuleEmitter &ME;
  ConstantEmitter &Emitter;
  llvm::LLVMContext &VMContext;

public:
  ConstExprEmitter(ConstantEmitter &emitter)
      : ME(emitter.ME), Emitter(emitter), VMContext(ME.getLLVMContext()) {}

  //===--------------------------------------------------------------------===
  //                            Visitor Methods
  //===--------------------------------------------------------------------===

  llvm::Constant *VisitStmt(Stmt *S, QualType T) { return nullptr; }

  llvm::Constant *VisitConstantExpr(ConstantExpr *CE, QualType T) {
    if (llvm::Constant *Result = Emitter.tryEmitConstantExpr(CE))
      return Result;
    return Visit(CE->getSubExpr(), T);
  }

  llvm::Constant *VisitParenExpr(ParenExpr *PE, QualType T) {
    return Visit(PE->getSubExpr(), T);
  }

  llvm::Constant *VisitGenericSelectionExpr(GenericSelectionExpr *GE,
                                            QualType T) {
    return Visit(GE->getResultExpr(), T);
  }

  llvm::Constant *VisitChooseExpr(ChooseExpr *CE, QualType T) {
    return Visit(CE->getChosenSubExpr(), T);
  }

  llvm::Constant *VisitCompoundLiteralExpr(CompoundLiteralExpr *E, QualType T) {
    return Visit(E->getInitializer(), T);
  }

  llvm::Constant *VisitCastExpr(CastExpr *E, QualType destType) {
    if (const auto *ECE = dyn_cast<ExplicitCastExpr>(E))
      ME.genExplicitCastExprType(ECE, Emitter.FE);
    Expr *subExpr = E->getSubExpr();

    switch (E->getCastKind()) {
    case CK_ToUnion: {
      // GCC cast to union extension
      assert(E->getType()->isUnionType() &&
             "Destination type is not union type!");

      auto field = E->getTargetUnionField();

      auto C = Emitter.tryEmitPrivateForMemory(subExpr, field->getType());
      if (!C)
        return nullptr;

      auto destTy = convertType(destType);
      if (C->getType() == destTy)
        return C;

      llvm::SmallVector<llvm::Constant *, 2> Elts;
      llvm::SmallVector<llvm::Type *, 2> Types;
      Elts.push_back(C);
      Types.push_back(C->getType());
      unsigned CurSize = ME.getDataLayout().getTypeAllocSize(C->getType());
      unsigned TotalSize = ME.getDataLayout().getTypeAllocSize(destTy);

      assert(CurSize <= TotalSize && "Union size mismatch!");
      if (unsigned NumPadBytes = TotalSize - CurSize) {
        llvm::Type *Ty = ME.CharTy;
        if (NumPadBytes > 1)
          Ty = llvm::ArrayType::get(Ty, NumPadBytes);

        Elts.push_back(llvm::UndefValue::get(Ty));
        Types.push_back(Ty);
      }

      llvm::StructType *STy = llvm::StructType::get(VMContext, Types, false);
      return llvm::ConstantStruct::get(STy, Elts);
    }

    case CK_AddressSpaceConversion: {
      auto C = Emitter.tryEmitPrivate(subExpr, subExpr->getType());
      if (!C)
        return nullptr;
      LangAS destAS = E->getType()->getPointeeType().getAddressSpace();
      LangAS srcAS = subExpr->getType()->getPointeeType().getAddressSpace();
      llvm::Type *destTy = convertType(E->getType());
      return ME.getTargetCodeGenInfo().performAddrSpaceCast(ME, C, srcAS,
                                                            destAS, destTy);
    }

    case CK_LValueToRValue: {
      // We don't really support doing lvalue-to-rvalue conversions here; any
      // interesting conversions should be done in Evaluate().  But as a
      // special case, allow compound literals to support the gcc extension
      // allowing "struct x {int x;} x = (struct x) {};".
      if (auto *E = dyn_cast<CompoundLiteralExpr>(subExpr->IgnoreParens()))
        return Visit(E->getInitializer(), destType);
      return nullptr;
    }

    case CK_AtomicToNonAtomic:
    case CK_NonAtomicToAtomic:
    case CK_NoOp:
      return Visit(subExpr, destType);

    case CK_ArrayToPointerDecay:
      if (const auto *S = dyn_cast<StringLiteral>(subExpr))
        return ME.addrOfConstantStringFromLiteral(S).getPointer();
      return nullptr;
    case CK_NullToPointer:
      if (Visit(subExpr, destType))
        return ME.genNullConstant(destType);
      return nullptr;

    case CK_IntegralCast: {
      QualType FromType = subExpr->getType();
      // See also HandleIntToIntCast in ExprConstant.cpp
      if (FromType->isIntegerType())
        if (llvm::Constant *C = Visit(subExpr, FromType))
          if (auto *CI = dyn_cast<llvm::ConstantInt>(C)) {
            unsigned SrcWidth = ME.getContext().getIntWidth(FromType);
            unsigned DstWidth = ME.getContext().getIntWidth(destType);
            if (DstWidth == SrcWidth)
              return CI;
            llvm::APInt A = FromType->isSignedIntegerType()
                                ? CI->getValue().sextOrTrunc(DstWidth)
                                : CI->getValue().zextOrTrunc(DstWidth);
            return llvm::ConstantInt::get(ME.getLLVMContext(), A);
          }
      return nullptr;
    }

    case CK_Dependent:
      llvm_unreachable("saw dependent cast!");

    case CK_BuiltinFnToFnPtr:
      llvm_unreachable("builtin functions are handled elsewhere");

    // These don't need to be handled here because Evaluate knows how to
    // evaluate them in the cases where they can be folded.
    case CK_BitCast:
    case CK_ToVoid:
    case CK_LValueBitCast:
    case CK_LValueToRValueBitCast:
    case CK_FunctionToPointerDecay:
    case CK_VectorSplat:
    case CK_FloatingRealToComplex:
    case CK_FloatingComplexToReal:
    case CK_FloatingComplexToBoolean:
    case CK_FloatingComplexCast:
    case CK_FloatingComplexToIntegralComplex:
    case CK_IntegralRealToComplex:
    case CK_IntegralComplexToReal:
    case CK_IntegralComplexToBoolean:
    case CK_IntegralComplexCast:
    case CK_IntegralComplexToFloatingComplex:
    case CK_PointerToIntegral:
    case CK_PointerToBoolean:
    case CK_BooleanToSignedIntegral:
    case CK_IntegralToPointer:
    case CK_IntegralToBoolean:
    case CK_IntegralToFloating:
    case CK_FloatingToIntegral:
    case CK_FloatingToBoolean:
    case CK_FloatingCast:
    case CK_FloatingToFixedPoint:
    case CK_FixedPointToFloating:
    case CK_FixedPointCast:
    case CK_FixedPointToBoolean:
    case CK_FixedPointToIntegral:
    case CK_IntegralToFixedPoint:
    case CK_MatrixCast:
      return nullptr;
    }
    llvm_unreachable("Invalid CastKind");
  }

  llvm::Constant *VisitExprWithCleanups(ExprWithCleanups *E, QualType T) {
    return Visit(E->getSubExpr(), T);
  }

  llvm::Constant *VisitIntegerLiteral(IntegerLiteral *I, QualType T) {
    return llvm::ConstantInt::get(ME.getLLVMContext(), I->getValue());
  }

  llvm::Constant *genArrayInitialization(InitListExpr *ILE, QualType T) {
    auto *CAT = ME.getContext().getAsConstantArrayType(ILE->getType());
    assert(CAT && "can't emit array init for non-constant-bound array");
    unsigned NumInitElements = ILE->getNumInits();
    unsigned NumElements = CAT->getSize().getZExtValue();

    // Initialising an array requires us to automatically
    // initialise any elements that have not been initialised explicitly
    unsigned NumInitableElts = std::min(NumInitElements, NumElements);

    QualType EltType = CAT->getElementType();

    llvm::Constant *fillC = nullptr;
    if (Expr *filler = ILE->getArrayFiller()) {
      fillC = Emitter.tryEmitAbstractForMemory(filler, EltType);
      if (!fillC)
        return nullptr;
    }

    llvm::SmallVector<llvm::Constant *, 16> Elts;
    if (fillC && fillC->isNullValue())
      Elts.reserve(NumInitableElts + 1);
    else
      Elts.reserve(NumElements);

    llvm::Type *CommonElementType = nullptr;
    for (unsigned i = 0; i < NumInitableElts; ++i) {
      Expr *Init = ILE->getInit(i);
      llvm::Constant *C = Emitter.tryEmitPrivateForMemory(Init, EltType);
      if (!C)
        return nullptr;
      if (i == 0)
        CommonElementType = C->getType();
      else if (C->getType() != CommonElementType)
        CommonElementType = nullptr;
      Elts.push_back(C);
    }

    llvm::ArrayType *Desired =
        cast<llvm::ArrayType>(ME.getTypes().convertType(ILE->getType()));
    return genArrayConstant(ME, Desired, CommonElementType, NumElements, Elts,
                            fillC);
  }

  llvm::Constant *genRecordInitialization(InitListExpr *ILE, QualType T) {
    return ConstStructBuilder::FormStruct(Emitter, ILE, T);
  }

  llvm::Constant *VisitImplicitValueInitExpr(ImplicitValueInitExpr *E,
                                             QualType T) {
    return ME.genNullConstant(T);
  }

  llvm::Constant *VisitInitListExpr(InitListExpr *ILE, QualType T) {
    if (ILE->isTransparent())
      return Visit(ILE->getInit(0), T);

    if (ILE->getType()->isArrayType())
      return genArrayInitialization(ILE, T);

    if (ILE->getType()->isRecordType())
      return genRecordInitialization(ILE, T);

    return nullptr;
  }

  llvm::Constant *VisitDesignatedInitUpdateExpr(DesignatedInitUpdateExpr *E,
                                                QualType destType) {
    auto C = Visit(E->getBase(), destType);
    if (!C)
      return nullptr;

    ConstantAggregateBuilder Const(ME);
    Const.add(C, CharUnits::Zero(), false);

    if (!genDesignatedInitUpdater(Emitter, Const, CharUnits::Zero(), destType,
                                  E->getUpdater()))
      return nullptr;

    llvm::Type *ValTy = ME.getTypes().convertType(destType);
    bool HasFlexibleArray = false;
    if (auto *RT = destType->getAs<RecordType>())
      HasFlexibleArray = RT->getDecl()->hasFlexibleArrayMember();
    return Const.build(ValTy, HasFlexibleArray);
  }

  llvm::Constant *VisitStringLiteral(StringLiteral *E, QualType T) {
    // This is a string literal initializing an array in an initializer.
    return ME.getConstantArrayFromStringLiteral(E);
  }

  llvm::Constant *VisitUnaryExtension(const UnaryOperator *E, QualType T) {
    return Visit(E->getSubExpr(), T);
  }

  llvm::Constant *VisitUnaryMinus(UnaryOperator *U, QualType T) {
    if (llvm::Constant *C = Visit(U->getSubExpr(), T))
      if (auto *CI = dyn_cast<llvm::ConstantInt>(C))
        return llvm::ConstantInt::get(ME.getLLVMContext(), -CI->getValue());
    return nullptr;
  }

  // Utility methods
  llvm::Type *convertType(QualType T) { return ME.getTypes().convertType(T); }
};

} // end anonymous namespace.

// ===----------------------------------------------------------------------===
// ConstantEmitter public API
// ===----------------------------------------------------------------------===

llvm::Constant *ConstantEmitter::validateAndPopAbstract(llvm::Constant *C,
                                                        AbstractState saved) {
  Abstract = saved.OldValue;

  assert(saved.OldPlaceholdersSize == PlaceholderAddresses.size() &&
         "created a placeholder while doing an abstract emission?");

  // No validation necessary for now.
  // No cleanup to do for now.
  return C;
}

llvm::Constant *
ConstantEmitter::tryEmitAbstractForInitializer(const VarDecl &D) {
  auto state = pushAbstract();
  auto C = tryEmitPrivateForVarInit(D);
  return validateAndPopAbstract(C, state);
}

llvm::Constant *ConstantEmitter::tryEmitAbstract(const Expr *E,
                                                 QualType destType) {
  auto state = pushAbstract();
  auto C = tryEmitPrivate(E, destType);
  return validateAndPopAbstract(C, state);
}

llvm::Constant *ConstantEmitter::tryEmitAbstract(const APValue &value,
                                                 QualType destType) {
  auto state = pushAbstract();
  auto C = tryEmitPrivate(value, destType);
  return validateAndPopAbstract(C, state);
}

llvm::Constant *ConstantEmitter::tryEmitConstantExpr(const ConstantExpr *CE) {
  if (!CE->hasAPValueResult())
    return nullptr;

  QualType RetType = CE->getType();
  return emitAbstract(CE->getBeginLoc(), CE->getAPValueResult(), RetType);
}

llvm::Constant *ConstantEmitter::emitAbstract(const Expr *E,
                                              QualType destType) {
  auto state = pushAbstract();
  auto C = tryEmitPrivate(E, destType);
  C = validateAndPopAbstract(C, state);
  if (!C) {
    ME.error(E->getExprLoc(),
             "internal error: could not emit constant value \"abstractly\"");
    C = ME.genNullConstant(destType);
  }
  return C;
}

llvm::Constant *ConstantEmitter::emitAbstract(SourceLocation loc,
                                              const APValue &value,
                                              QualType destType) {
  auto state = pushAbstract();
  auto C = tryEmitPrivate(value, destType);
  C = validateAndPopAbstract(C, state);
  if (!C) {
    ME.error(loc,
             "internal error: could not emit constant value \"abstractly\"");
    C = ME.genNullConstant(destType);
  }
  return C;
}

llvm::Constant *ConstantEmitter::tryEmitForInitializer(const VarDecl &D) {
  initializeNonAbstract(D.getType().getAddressSpace());
  return markIfFailed(tryEmitPrivateForVarInit(D));
}

llvm::Constant *ConstantEmitter::tryEmitForInitializer(const Expr *E,
                                                       LangAS destAddrSpace,
                                                       QualType destType) {
  initializeNonAbstract(destAddrSpace);
  return markIfFailed(tryEmitPrivateForMemory(E, destType));
}

llvm::Constant *ConstantEmitter::emitForInitializer(const APValue &value,
                                                    LangAS destAddrSpace,
                                                    QualType destType) {
  initializeNonAbstract(destAddrSpace);
  auto C = tryEmitPrivateForMemory(value, destType);
  assert(C && "couldn't emit constant value non-abstractly?");
  return C;
}

llvm::GlobalValue *ConstantEmitter::getCurrentAddrPrivate() {
  assert(!Abstract && "cannot get current address for abstract constant");

  // Make an obviously ill-formed global that should blow up compilation
  // if it survives.
  auto global = new llvm::GlobalVariable(
      ME.getModule(), ME.Int8Ty, true, llvm::GlobalValue::PrivateLinkage,
      /*init*/ nullptr,
      /*name*/ "",
      /*before*/ nullptr, llvm::GlobalVariable::NotThreadLocal,
      ME.getContext().getTargetAddressSpace(DestAddressSpace));

  PlaceholderAddresses.push_back(std::make_pair(nullptr, global));

  return global;
}

void ConstantEmitter::registerCurrentAddrPrivate(
    llvm::Constant *signal, llvm::GlobalValue *placeholder) {
  assert(!PlaceholderAddresses.empty());
  assert(PlaceholderAddresses.back().first == nullptr);
  assert(PlaceholderAddresses.back().second == placeholder);
  PlaceholderAddresses.back().first = signal;
}

namespace {
struct ReplacePlaceholders {
  ModuleEmitter &ME;

  llvm::Constant *Base;
  llvm::Type *BaseValueTy = nullptr;

  llvm::DenseMap<llvm::Constant *, llvm::GlobalVariable *> PlaceholderAddresses;

  llvm::DenseMap<llvm::GlobalVariable *, llvm::Constant *> Locations;

  llvm::SmallVector<unsigned, 8> Indices;
  llvm::SmallVector<llvm::Constant *, 8> IndexValues;

  ReplacePlaceholders(
      ModuleEmitter &ME, llvm::Constant *base,
      llvm::ArrayRef<std::pair<llvm::Constant *, llvm::GlobalVariable *>>
          addresses)
      : ME(ME), Base(base),
        PlaceholderAddresses(addresses.begin(), addresses.end()) {}

  void replaceInInitializer(llvm::Constant *init) {
    // Remember the type of the top-most initializer.
    BaseValueTy = init->getType();

    Indices.push_back(0);
    IndexValues.push_back(nullptr);
    findLocations(init);

    assert(IndexValues.size() == Indices.size() && "mismatch");
    assert(Indices.size() == 1 && "didn't pop all indices");
    assert(Locations.size() == PlaceholderAddresses.size() &&
           "missed a placeholder?");

    // We're iterating over a hashtable, so this would be a source of
    // non-determinism in compiler output *except* that we're just
    // messing around with llvm::Constant structures, which never itself
    // does anything that should be visible in compiler output.
    for (auto &entry : Locations) {
      assert(entry.first->getParent() == nullptr && "not a placeholder!");
      entry.first->replaceAllUsesWith(entry.second);
      entry.first->eraseFromParent();
    }
  }

private:
  void findLocations(llvm::Constant *init) {
    // Recurse into aggregates.
    if (auto agg = dyn_cast<llvm::ConstantAggregate>(init)) {
      for (unsigned i = 0, e = agg->getNumOperands(); i != e; ++i) {
        Indices.push_back(i);
        IndexValues.push_back(nullptr);

        findLocations(agg->getOperand(i));

        IndexValues.pop_back();
        Indices.pop_back();
      }
      return;
    }

    // Otherwise, check for registered constants.
    while (true) {
      auto it = PlaceholderAddresses.find(init);
      if (it != PlaceholderAddresses.end()) {
        setLocation(it->second);
        break;
      }

      // Look through bitcasts or other expressions.
      if (auto expr = dyn_cast<llvm::ConstantExpr>(init)) {
        init = expr->getOperand(0);
      } else {
        break;
      }
    }
  }

  void setLocation(llvm::GlobalVariable *placeholder) {
    assert(!Locations.contains(placeholder) &&
           "already found location for placeholder!");

    // Lazily fill in IndexValues with the values from Indices.
    // We do this in reverse because we should always have a strict
    // prefix of indices from the start.
    assert(Indices.size() == IndexValues.size());
    for (size_t i = Indices.size() - 1; i != size_t(-1); --i) {
      if (IndexValues[i]) {
#ifndef NDEBUG
        for (size_t j = 0; j != i + 1; ++j) {
          assert(IndexValues[j] && isa<llvm::ConstantInt>(IndexValues[j]) &&
                 cast<llvm::ConstantInt>(IndexValues[j])->getZExtValue() ==
                     Indices[j]);
        }
#endif
        break;
      }

      IndexValues[i] = llvm::ConstantInt::get(ME.Int32Ty, Indices[i]);
    }

    llvm::Constant *location = llvm::ConstantExpr::getInBoundsGetElementPtr(
        BaseValueTy, Base, IndexValues);

    Locations.insert({placeholder, location});
  }
};
} // namespace

void ConstantEmitter::finalize(llvm::GlobalVariable *global) {
  assert(InitializedNonAbstract &&
         "finalizing emitter that was used for abstract emission?");
  assert(!Finalized && "finalizing emitter multiple times");
  assert(global->getInitializer());

  // Note that we might also be Failed.
  Finalized = true;

  if (!PlaceholderAddresses.empty()) {
    ReplacePlaceholders(ME, global, PlaceholderAddresses)
        .replaceInInitializer(global->getInitializer());
    PlaceholderAddresses.clear(); // satisfy
  }
}

ConstantEmitter::~ConstantEmitter() {
  assert((!InitializedNonAbstract || Finalized || Failed) &&
         "not finalized after being initialized for non-abstract emission");
  assert(PlaceholderAddresses.empty() && "unhandled placeholders");
}

// ===----------------------------------------------------------------------===
// Memory representation & type conversion
// ===----------------------------------------------------------------------===

namespace {
QualType getNonMemoryType(ModuleEmitter &ME, QualType type) {
  if (auto AT = type->getAs<AtomicType>()) {
    return ME.getContext().getQualifiedType(AT->getValueType(),
                                            type.getQualifiers());
  }
  return type;
}
} // namespace

llvm::Constant *ConstantEmitter::tryEmitPrivateForVarInit(const VarDecl &D) {
  InConstantContext = D.hasConstantInitialization();

  QualType destType = D.getType();
  const Expr *E = D.getInit();
  assert(E && "No initializer to emit");

  QualType nonMemoryDestType = getNonMemoryType(ME, destType);
  if (llvm::Constant *C = ConstExprEmitter(*this).Visit(const_cast<Expr *>(E),
                                                        nonMemoryDestType))
    return emitForMemory(C, destType);

  // Try to emit the initializer.  Note that this can allow some things that
  // are not allowed by tryEmitPrivateForMemory alone.
  if (APValue *value = D.evaluateValue())
    return tryEmitPrivateForMemory(*value, destType);

  return nullptr;
}

llvm::Constant *ConstantEmitter::tryEmitAbstractForMemory(const Expr *E,
                                                          QualType destType) {
  auto nonMemoryDestType = getNonMemoryType(ME, destType);
  auto C = tryEmitAbstract(E, nonMemoryDestType);
  return (C ? emitForMemory(C, destType) : nullptr);
}

llvm::Constant *ConstantEmitter::tryEmitAbstractForMemory(const APValue &value,
                                                          QualType destType) {
  auto nonMemoryDestType = getNonMemoryType(ME, destType);
  auto C = tryEmitAbstract(value, nonMemoryDestType);
  return (C ? emitForMemory(C, destType) : nullptr);
}

llvm::Constant *ConstantEmitter::tryEmitPrivateForMemory(const Expr *E,
                                                         QualType destType) {
  auto nonMemoryDestType = getNonMemoryType(ME, destType);
  llvm::Constant *C = tryEmitPrivate(E, nonMemoryDestType);
  return (C ? emitForMemory(C, destType) : nullptr);
}

llvm::Constant *ConstantEmitter::tryEmitPrivateForMemory(const APValue &value,
                                                         QualType destType) {
  auto nonMemoryDestType = getNonMemoryType(ME, destType);
  auto C = tryEmitPrivate(value, nonMemoryDestType);
  return (C ? emitForMemory(C, destType) : nullptr);
}

llvm::Constant *ConstantEmitter::emitForMemory(ModuleEmitter &ME,
                                               llvm::Constant *C,
                                               QualType destType) {
  // For an _Atomic-qualified constant, we may need to add tail padding.
  if (auto AT = destType->getAs<AtomicType>()) {
    QualType destValueType = AT->getValueType();
    C = emitForMemory(ME, C, destValueType);

    uint64_t innerSize = ME.getContext().getTypeSize(destValueType);
    uint64_t outerSize = ME.getContext().getTypeSize(destType);
    if (innerSize == outerSize)
      return C;

    assert(innerSize < outerSize && "emitted over-large constant for atomic");
    llvm::Constant *elts[] = {
        C, llvm::ConstantAggregateZero::get(
               llvm::ArrayType::get(ME.Int8Ty, (outerSize - innerSize) / 8))};
    return llvm::ConstantStruct::getAnon(elts);
  }

  // Zero-extend bool.
  if (C->getType()->isIntegerTy(1) && !destType->isBitIntType()) {
    llvm::Type *boolTy = ME.getTypes().convertTypeForMem(destType);
    llvm::Constant *Res = llvm::ConstantFoldCastOperand(
        llvm::Instruction::ZExt, C, boolTy, ME.getDataLayout());
    assert(Res && "Constant folding must succeed");
    return Res;
  }

  return C;
}

llvm::Constant *ConstantEmitter::tryEmitPrivate(const Expr *E,
                                                QualType destType) {
  assert(!destType->isVoidType() && "can't emit a void constant");

  if (llvm::Constant *C =
          ConstExprEmitter(*this).Visit(const_cast<Expr *>(E), destType))
    return C;

  Expr::EvalResult Result;
  bool Success =
      E->EvaluateAsRValue(Result, ME.getContext(), InConstantContext);

  if (Success && !Result.HasSideEffects)
    return tryEmitPrivate(Result.Val, destType);

  return nullptr;
}

llvm::Constant *ModuleEmitter::getNullPointer(llvm::PointerType *T,
                                              QualType QT) {
  return getTargetCodeGenInfo().getNullPointer(*this, T, QT);
}

// ===----------------------------------------------------------------------===
// LValue constant emission
// ===----------------------------------------------------------------------===

namespace {
struct ConstantLValue {
  llvm::Constant *Value;
  bool HasOffsetApplied;

  /*implicit*/ ConstantLValue(llvm::Constant *value,
                              bool hasOffsetApplied = false)
      : Value(value), HasOffsetApplied(hasOffsetApplied) {}

  /*implicit*/ ConstantLValue(ConstantAddress address)
      : ConstantLValue(address.getPointer()) {}
};

class ConstantLValueEmitter
    : public ConstStmtVisitor<ConstantLValueEmitter, ConstantLValue> {
  ModuleEmitter &ME;
  ConstantEmitter &Emitter;
  const APValue &Value;
  QualType DestType;

  // Befriend StmtVisitorBase so that we don't have to expose Visit*.
  friend StmtVisitorBase;

public:
  ConstantLValueEmitter(ConstantEmitter &emitter, const APValue &value,
                        QualType destType)
      : ME(emitter.ME), Emitter(emitter), Value(value), DestType(destType) {}

  llvm::Constant *tryEmit();

private:
  llvm::Constant *tryEmitAbsolute(llvm::Type *destTy);
  ConstantLValue tryEmitBase(const APValue::LValueBase &base);

  ConstantLValue VisitStmt(const Stmt *S) { return nullptr; }
  ConstantLValue VisitConstantExpr(const ConstantExpr *E);
  ConstantLValue VisitCompoundLiteralExpr(const CompoundLiteralExpr *E);
  ConstantLValue VisitStringLiteral(const StringLiteral *E);
  ConstantLValue VisitPredefinedExpr(const PredefinedExpr *E);
  ConstantLValue VisitAddrLabelExpr(const AddrLabelExpr *E);
  ConstantLValue VisitCallExpr(const CallExpr *E);

  bool hasNonZeroOffset() const { return !Value.getLValueOffset().isZero(); }

  llvm::Constant *getOffset() {
    return llvm::ConstantInt::get(ME.Int64Ty,
                                  Value.getLValueOffset().getQuantity());
  }

  llvm::Constant *applyOffset(llvm::Constant *C) {
    if (!hasNonZeroOffset())
      return C;

    return llvm::ConstantExpr::getGetElementPtr(ME.Int8Ty, C, getOffset());
  }
};

} // namespace

llvm::Constant *ConstantLValueEmitter::tryEmit() {
  const APValue::LValueBase &base = Value.getLValueBase();

  // The destination type should be a pointer or reference
  // type, but it might also be a cast thereof.
  //
  auto destTy = ME.getTypes().convertTypeForMem(DestType);
  assert(isa<llvm::IntegerType>(destTy) || isa<llvm::PointerType>(destTy));

  // If there's no base at all, this is a null or absolute pointer,
  // possibly cast back to an integer type.
  if (!base) {
    return tryEmitAbsolute(destTy);
  }

  // Otherwise, try to emit the base.
  ConstantLValue result = tryEmitBase(base);

  // If that failed, we're done.
  llvm::Constant *value = result.Value;
  if (!value)
    return nullptr;

  // Apply the offset if necessary and not already done.
  if (!result.HasOffsetApplied) {
    value = applyOffset(value);
  }

  // Convert to the appropriate type; this could be an lvalue for
  // an integer.
  if (isa<llvm::PointerType>(destTy))
    return llvm::ConstantExpr::getPointerCast(value, destTy);

  return llvm::ConstantExpr::getPtrToInt(value, destTy);
}

llvm::Constant *ConstantLValueEmitter::tryEmitAbsolute(llvm::Type *destTy) {
  // If we're producing a pointer, this is easy.
  auto destPtrTy = cast<llvm::PointerType>(destTy);
  if (Value.isNullPointer()) {
    return ME.getNullPointer(destPtrTy, DestType);
  }

  auto intptrTy = ME.getDataLayout().getIntPtrType(destPtrTy);
  llvm::Constant *C;
  C = llvm::ConstantFoldIntegerCast(getOffset(), intptrTy, /*isSigned*/ false,
                                    ME.getDataLayout());
  assert(C && "Must have folded, as Offset is a ConstantInt");
  C = llvm::ConstantExpr::getIntToPtr(C, destPtrTy);
  return C;
}

ConstantLValue
ConstantLValueEmitter::tryEmitBase(const APValue::LValueBase &base) {
  if (const ValueDecl *D = base.dyn_cast<const ValueDecl *>()) {
    D = cast<ValueDecl>(D->getMostRecentDecl());

    if (D->hasAttr<WeakRefAttr>())
      return ME.getWeakRefReference(D).getPointer();

    if (auto FD = dyn_cast<FunctionDecl>(D))
      return ME.addrOfFunction(FD);

    if (auto VD = dyn_cast<VarDecl>(D)) {
      if (!VD->hasLocalStorage()) {
        if (VD->isFileVarDecl() || VD->hasExternalStorage())
          return ME.getGlobalVarAddr(VD);

        if (VD->isLocalVarDecl()) {
          return ME.getOrCreateStaticVarDecl(
              *VD, ME.getLLVMLinkageVarDefinition(VD));
        }
      }
    }

    return nullptr;
  }

  // Otherwise, it must be an expression.
  return Visit(base.get<const Expr *>());
}

ConstantLValue ConstantLValueEmitter::VisitConstantExpr(const ConstantExpr *E) {
  if (llvm::Constant *Result = Emitter.tryEmitConstantExpr(E))
    return Result;
  return Visit(E->getSubExpr());
}

ConstantLValue
ConstantLValueEmitter::VisitCompoundLiteralExpr(const CompoundLiteralExpr *E) {
  ConstantEmitter CompoundLiteralEmitter(ME, Emitter.FE);
  CompoundLiteralEmitter.setInConstantContext(Emitter.isInConstantContext());
  return tryEmitGlobalCompoundLiteral(CompoundLiteralEmitter, E);
}

ConstantLValue
ConstantLValueEmitter::VisitStringLiteral(const StringLiteral *E) {
  return ME.addrOfConstantStringFromLiteral(E);
}

ConstantLValue
ConstantLValueEmitter::VisitPredefinedExpr(const PredefinedExpr *E) {
  return ME.addrOfConstantStringFromLiteral(E->getFunctionName());
}

ConstantLValue
ConstantLValueEmitter::VisitAddrLabelExpr(const AddrLabelExpr *E) {
  assert(Emitter.FE && "Invalid address of label expression outside function");
  llvm::Constant *Ptr = Emitter.FE->addrOfLabel(E->getLabel());
  return Ptr;
}

ConstantLValue ConstantLValueEmitter::VisitCallExpr(const CallExpr *E) {
  unsigned builtin = E->getBuiltinCallee();
  if (builtin == Builtin::BI__builtin_function_start)
    return ME.getFunctionStart(
        E->getArg(0)->getAsBuiltinConstantDeclRef(ME.getContext()));
  return nullptr;
}

// ===----------------------------------------------------------------------===
// APValue → LLVM Constant conversion
// ===----------------------------------------------------------------------===

llvm::Constant *ConstantEmitter::tryEmitPrivate(const APValue &Value,
                                                QualType DestType) {
  switch (Value.getKind()) {
  case APValue::None:
  case APValue::Indeterminate:
    // Out-of-lifetime and indeterminate values can be modeled as 'undef'.
    return llvm::UndefValue::get(ME.getTypes().convertType(DestType));
  case APValue::LValue:
    return ConstantLValueEmitter(*this, Value, DestType).tryEmit();
  case APValue::Int:
    return llvm::ConstantInt::get(ME.getLLVMContext(), Value.getInt());
  case APValue::FixedPoint:
    return llvm::ConstantInt::get(ME.getLLVMContext(),
                                  Value.getFixedPoint().getValue());
  case APValue::ComplexInt: {
    llvm::Constant *Complex[2];

    Complex[0] =
        llvm::ConstantInt::get(ME.getLLVMContext(), Value.getComplexIntReal());
    Complex[1] =
        llvm::ConstantInt::get(ME.getLLVMContext(), Value.getComplexIntImag());

    llvm::StructType *STy =
        llvm::StructType::get(Complex[0]->getType(), Complex[1]->getType());
    return llvm::ConstantStruct::get(STy, Complex);
  }
  case APValue::Float: {
    const llvm::APFloat &Init = Value.getFloat();
    if (&Init.getSemantics() == &llvm::APFloat::IEEEhalf() &&
        !ME.getContext().getLangOpts().NativeHalfType &&
        ME.getContext().getTargetInfo().useFP16ConversionIntrinsics())
      return llvm::ConstantInt::get(ME.getLLVMContext(), Init.bitcastToAPInt());
    else
      return llvm::ConstantFP::get(ME.getLLVMContext(), Init);
  }
  case APValue::ComplexFloat: {
    llvm::Constant *Complex[2];

    Complex[0] =
        llvm::ConstantFP::get(ME.getLLVMContext(), Value.getComplexFloatReal());
    Complex[1] =
        llvm::ConstantFP::get(ME.getLLVMContext(), Value.getComplexFloatImag());

    llvm::StructType *STy =
        llvm::StructType::get(Complex[0]->getType(), Complex[1]->getType());
    return llvm::ConstantStruct::get(STy, Complex);
  }
  case APValue::Vector: {
    unsigned NumElts = Value.getVectorLength();
    llvm::SmallVector<llvm::Constant *, 4> Inits(NumElts);

    for (unsigned I = 0; I != NumElts; ++I) {
      const APValue &Elt = Value.getVectorElt(I);
      if (Elt.isInt())
        Inits[I] = llvm::ConstantInt::get(ME.getLLVMContext(), Elt.getInt());
      else if (Elt.isFloat())
        Inits[I] = llvm::ConstantFP::get(ME.getLLVMContext(), Elt.getFloat());
      else if (Elt.isIndeterminate())
        Inits[I] = llvm::UndefValue::get(ME.getTypes().convertType(
            DestType->castAs<VectorType>()->getElementType()));
      else
        llvm_unreachable("unsupported vector element type");
    }
    return llvm::ConstantVector::get(Inits);
  }
  case APValue::AddrLabelDiff: {
    const AddrLabelExpr *LHSExpr = Value.getAddrLabelDiffLHS();
    const AddrLabelExpr *RHSExpr = Value.getAddrLabelDiffRHS();
    llvm::Constant *LHS = tryEmitPrivate(LHSExpr, LHSExpr->getType());
    llvm::Constant *RHS = tryEmitPrivate(RHSExpr, RHSExpr->getType());
    if (!LHS || !RHS)
      return nullptr;

    // Compute difference
    llvm::Type *ResultType = ME.getTypes().convertType(DestType);
    LHS = llvm::ConstantExpr::getPtrToInt(LHS, ME.IntPtrTy);
    RHS = llvm::ConstantExpr::getPtrToInt(RHS, ME.IntPtrTy);
    llvm::Constant *AddrLabelDiff = llvm::ConstantExpr::getSub(LHS, RHS);

    // LLVM is a bit sensitive about the exact format of the
    // address-of-label difference; make sure to truncate after
    // the subtraction.
    return llvm::ConstantExpr::getTruncOrBitCast(AddrLabelDiff, ResultType);
  }
  case APValue::Struct:
  case APValue::Union:
    return ConstStructBuilder::FormStruct(*this, Value, DestType);
  case APValue::Array: {
    const ArrayType *ArrayTy = ME.getContext().getAsArrayType(DestType);
    unsigned NumElements = Value.getArraySize();
    unsigned NumInitElts = Value.getArrayInitializedElts();

    llvm::Constant *Filler = nullptr;
    if (Value.hasArrayFiller()) {
      Filler = tryEmitAbstractForMemory(Value.getArrayFiller(),
                                        ArrayTy->getElementType());
      if (!Filler)
        return nullptr;
    }

    llvm::SmallVector<llvm::Constant *, 16> Elts;
    if (Filler && Filler->isNullValue())
      Elts.reserve(NumInitElts + 1);
    else
      Elts.reserve(NumElements);

    llvm::Type *CommonElementType = nullptr;
    for (unsigned I = 0; I < NumInitElts; ++I) {
      llvm::Constant *C = tryEmitPrivateForMemory(
          Value.getArrayInitializedElt(I), ArrayTy->getElementType());
      if (!C)
        return nullptr;

      if (I == 0)
        CommonElementType = C->getType();
      else if (C->getType() != CommonElementType)
        CommonElementType = nullptr;
      Elts.push_back(C);
    }

    llvm::ArrayType *Desired =
        cast<llvm::ArrayType>(ME.getTypes().convertType(DestType));

    // Fix the type of incomplete arrays if the initializer isn't empty.
    if (DestType->isIncompleteArrayType() && !Elts.empty())
      Desired = llvm::ArrayType::get(Desired->getElementType(), Elts.size());

    return genArrayConstant(ME, Desired, CommonElementType, NumElements, Elts,
                            Filler);
  }
  }
  llvm_unreachable("Unknown APValue kind");
}

llvm::GlobalVariable *ModuleEmitter::getAddrOfConstantCompoundLiteralIfEmitted(
    const CompoundLiteralExpr *E) {
  return EmittedCompoundLiterals.lookup(E);
}

void ModuleEmitter::setAddrOfConstantCompoundLiteral(
    const CompoundLiteralExpr *CLE, llvm::GlobalVariable *GV) {
  bool Ok = EmittedCompoundLiterals.insert(std::make_pair(CLE, GV)).second;
  (void)Ok;
  assert(Ok && "CLE has already been emitted!");
}

ConstantAddress
ModuleEmitter::addrOfConstantCompoundLiteral(const CompoundLiteralExpr *E) {
  assert(E->isFileScope() && "not a file-scope compound literal expr");
  ConstantEmitter emitter(*this);
  return tryEmitGlobalCompoundLiteral(emitter, E);
}

// ===----------------------------------------------------------------------===
// Null constant generation
// ===----------------------------------------------------------------------===

namespace {
llvm::Constant *genNullConstant(ModuleEmitter &ME, const RecordDecl *record) {
  const RecordLayoutInfo &layout = ME.getTypes().getRecordLayoutInfo(record);
  llvm::StructType *structure = layout.getLLVMType();

  unsigned numElements = structure->getNumElements();
  std::vector<llvm::Constant *> elements(numElements);

  // Fill in all the fields.
  for (const auto *Field : record->fields()) {
    // Fill in non-bitfields. (Bitfields always use a zero pattern, which we
    // will fill in later.)
    if (!Field->isBitField() && !Field->isZeroSize(ME.getContext())) {
      unsigned fieldIndex = layout.getLLVMFieldNo(Field);
      elements[fieldIndex] = ME.genNullConstant(Field->getType());
    }

    // For unions, stop after the first named field.
    if (record->isUnion()) {
      if (Field->getIdentifier())
        break;
      if (const auto *FieldRD = Field->getType()->getAsRecordDecl())
        if (FieldRD->findFirstNamedDataMember())
          break;
    }
  }

  // Now go through all other fields and zero them out.
  for (unsigned i = 0; i != numElements; ++i) {
    if (!elements[i])
      elements[i] = llvm::Constant::getNullValue(structure->getElementType(i));
  }

  return llvm::ConstantStruct::get(structure, elements);
}
} // namespace

llvm::Constant *ConstantEmitter::emitNullForMemory(ModuleEmitter &ME,
                                                   QualType T) {
  return emitForMemory(ME, ME.genNullConstant(T), T);
}

llvm::Constant *ModuleEmitter::genNullConstant(QualType T) {
  if (T->getAs<PointerType>())
    return getNullPointer(
        cast<llvm::PointerType>(getTypes().convertTypeForMem(T)), T);

  if (getTypes().isZeroInitializable(T))
    return llvm::Constant::getNullValue(getTypes().convertTypeForMem(T));

  if (const ConstantArrayType *CAT = Context.getAsConstantArrayType(T)) {
    llvm::ArrayType *ATy =
        cast<llvm::ArrayType>(getTypes().convertTypeForMem(T));

    QualType ElementTy = CAT->getElementType();

    llvm::Constant *Element =
        ConstantEmitter::emitNullForMemory(*this, ElementTy);
    unsigned NumElements = CAT->getSize().getZExtValue();
    llvm::SmallVector<llvm::Constant *, 8> Array(NumElements, Element);
    return llvm::ConstantArray::get(ATy, Array);
  }

  if (const RecordType *RT = T->getAs<RecordType>())
    return ::genNullConstant(*this, RT->getDecl());

  llvm_unreachable("Unexpected non-zero-initializable type");
}
