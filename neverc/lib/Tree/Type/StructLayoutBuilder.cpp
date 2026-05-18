#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Tree/Core/Attr.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Core/TreeDiag.h"
#include "neverc/Tree/Type/StructLayout.h"
#include "llvm/Support/MathExtras.h"
#include <cassert>

using namespace neverc;

// ===----------------------------------------------------------------------===
// Struct layout computation
// ===----------------------------------------------------------------------===

namespace {

struct ExternalLayout {
  ExternalLayout() = default;

  uint64_t Size = 0;

  uint64_t Align = 0;

  llvm::DenseMap<const FieldDecl *, uint64_t> FieldOffsets;

  uint64_t getExternalFieldOffset(const FieldDecl *FD) {
    auto It = FieldOffsets.find(FD);
    assert(It != FieldOffsets.end() &&
           "Field does not have an external offset");
    return It->second;
  }
};

class ItaniumRecordLayoutBuilder {
protected:
  friend class neverc::TreeContext;

  const TreeContext &Context;

  uint64_t Size;

  CharUnits Alignment;

  CharUnits UnpackedAlignment;

  CharUnits UnadjustedAlignment;

  llvm::SmallVector<uint64_t, 16> FieldOffsets;

  unsigned UseExternalLayout : 1;

  unsigned InferAlignment : 1;

  unsigned Packed : 1;

  unsigned IsUnion : 1;

  unsigned IsMsStruct : 1;

  unsigned char UnfilledBitsInLastUnit;

  unsigned char LastBitfieldStorageUnitSize;

  CharUnits MaxFieldAlignment;

  uint64_t DataSize;

  CharUnits PaddedFieldSize;

  bool HasPackedField;

  ExternalLayout External;

  explicit ItaniumRecordLayoutBuilder(const TreeContext &Context)
      : Context(Context), Size(0), Alignment(CharUnits::One()),
        UnpackedAlignment(CharUnits::One()),
        UnadjustedAlignment(CharUnits::One()), UseExternalLayout(false),
        InferAlignment(false), Packed(false), IsUnion(false), IsMsStruct(false),
        UnfilledBitsInLastUnit(0), LastBitfieldStorageUnitSize(0),
        MaxFieldAlignment(CharUnits::Zero()), DataSize(0),
        PaddedFieldSize(CharUnits::Zero()), HasPackedField(false) {}

  void Layout(const RecordDecl *D);

  void LayoutFields(const RecordDecl *D);
  void LayoutField(const FieldDecl *D);
  void LayoutBitField(const FieldDecl *D);

  void InitializeLayout(const Decl *D);

  void FinishLayout(const NamedDecl *D);

  void UpdateAlignment(CharUnits NewAlignment, CharUnits UnpackedNewAlignment);
  void UpdateAlignment(CharUnits NewAlignment) {
    UpdateAlignment(NewAlignment, NewAlignment);
  }

  uint64_t updateExternalFieldOffset(const FieldDecl *Field,
                                     uint64_t ComputedOffset);

  void CheckFieldPadding(uint64_t Offset, uint64_t UnpaddedOffset,
                         uint64_t UnpackedOffset, unsigned UnpackedAlign,
                         bool isPacked, const FieldDecl *D);

  DiagnosticBuilder Diag(SourceLocation Loc, unsigned DiagID);

  CharUnits getSize() const {
    assert(Size % Context.getCharWidth() == 0);
    return Context.toCharUnitsFromBits(Size);
  }
  uint64_t getSizeInBits() const { return Size; }

  void setSize(CharUnits NewSize) { Size = Context.toBits(NewSize); }
  void setSize(uint64_t NewSize) { Size = NewSize; }

  CharUnits getDataSize() const {
    assert(DataSize % Context.getCharWidth() == 0);
    return Context.toCharUnitsFromBits(DataSize);
  }
  uint64_t getDataSizeInBits() const { return DataSize; }

  void setDataSize(CharUnits NewSize) { DataSize = Context.toBits(NewSize); }
  void setDataSize(uint64_t NewSize) { DataSize = NewSize; }

  ItaniumRecordLayoutBuilder(const ItaniumRecordLayoutBuilder &) = delete;
  void operator=(const ItaniumRecordLayoutBuilder &) = delete;
};
} // end anonymous namespace

void ItaniumRecordLayoutBuilder::InitializeLayout(const Decl *D) {
  if (const RecordDecl *RD = dyn_cast<RecordDecl>(D)) {
    IsUnion = RD->isUnion();
    IsMsStruct = RD->isMsStruct(Context);
  }

  Packed = D->hasAttr<PackedAttr>();

  // Honor the default struct packing maximum alignment flag.
  if (unsigned DefaultMaxFieldAlignment = Context.getLangOpts().PackStruct) {
    MaxFieldAlignment = CharUnits::fromQuantity(DefaultMaxFieldAlignment);
  }

  if (const MaxFieldAlignmentAttr *MFAA = D->getAttr<MaxFieldAlignmentAttr>())
    MaxFieldAlignment = Context.toCharUnitsFromBits(MFAA->getAlignment());

  if (unsigned MaxAlign = D->getMaxAlignment())
    UpdateAlignment(Context.toCharUnitsFromBits(MaxAlign));
}

void ItaniumRecordLayoutBuilder::Layout(const RecordDecl *D) {
  InitializeLayout(D);
  LayoutFields(D);

  // Finally, round the size of the total struct up to the alignment of the
  // struct itself.
  FinishLayout(D);
}

void ItaniumRecordLayoutBuilder::LayoutFields(const RecordDecl *D) {
  // Layout each field, for now, just sequentially, respecting alignment.  In
  // the future, this will need to be tweakable by targets.
  for (auto I = D->field_begin(), End = D->field_end(); I != End; ++I)
    LayoutField(*I);
}

// Rounds the specified size to have it a multiple of the char size.
namespace {
uint64_t roundUpSizeToCharAlignment(uint64_t Size, const TreeContext &Context) {
  uint64_t CharAlignment = Context.getTargetInfo().getCharAlign();
  return llvm::alignTo(Size, CharAlignment);
}
} // namespace

void ItaniumRecordLayoutBuilder::LayoutBitField(const FieldDecl *D) {
  bool FieldPacked = Packed || D->hasAttr<PackedAttr>();
  uint64_t FieldSize = D->getBitWidthValue(Context);
  TypeInfo FieldInfo = Context.getTypeInfo(D->getType());
  uint64_t StorageUnitSize = FieldInfo.Width;
  unsigned FieldAlign = FieldInfo.Align;
  // UnfilledBitsInLastUnit is the difference between the end of the
  // last allocated bitfield (i.e. the first bit offset available for
  // bitfields) and the end of the current data size in bits (i.e. the
  // first bit offset available for non-bitfields).  The current data
  // size in bits is always a multiple of the char size; additionally,
  // for ms_struct records it's also a multiple of the
  // LastBitfieldStorageUnitSize (if set).

  // The struct-layout algorithm is dictated by the platform ABI,
  // which in principle could use almost any rules it likes.  In
  // practice, UNIXy targets tend to inherit the algorithm described
  // in the System V generic ABI.  The basic bitfield layout rule in
  // System V is to place bitfields at the next available bit offset
  // where the entire bitfield would fit in an aligned storage unit of
  // the declared type; it's okay if an earlier or later non-bitfield
  // is allocated in the same storage unit.  However, some targets
  // (those that !useBitFieldTypeAlignment()) don't
  // require this storage unit to be aligned, and therefore always put
  // the bitfield at the next available bit offset.

  // ms_struct basically requests a complete replacement of the
  // platform ABI's struct-layout algorithm, with the high-level goal
  // of duplicating MSVC's layout.  For non-bitfields, this follows
  // the standard algorithm.  The basic bitfield layout rule is to
  // allocate an entire unit of the bitfield's declared type
  // (e.g. 'unsigned long'), then parcel it up among successive
  // bitfields whose declared types have the same size, making a new
  // unit as soon as the last can no longer store the whole value.
  // Since it completely replaces the platform ABI's algorithm,
  // settings like !useBitFieldTypeAlignment() do not apply.

  // A zero-width bitfield forces the use of a new storage unit for
  // later bitfields.  In general, this occurs by rounding up the
  // current size of the struct as if the algorithm were about to
  // place a non-bitfield of the field's formal type.  Usually this
  // does not change the alignment of the struct itself, but it does
  // on some targets (those that useZeroLengthBitfieldAlignment(),
  // e.g. AArch64).  In ms_struct layout, zero-width bitfields are
  // ignored unless they follow a non-zero-width bitfield.

  // A field alignment restriction (e.g. from #pragma pack) or
  // specification (e.g. from __attribute__((aligned))) changes the
  // formal alignment of the field.  For System V, this alters the
  // required alignment of the notional storage unit that must contain
  // the bitfield.  For ms_struct, this only affects the placement of
  // new storage units.  In both cases, the effect of #pragma pack is
  // ignored on zero-width bitfields.

  // On System V, a packed field (e.g. from #pragma pack or
  // __attribute__((packed))) always uses the next available bit
  // offset.

  // In an ms_struct struct, the alignment of a fundamental type is
  // always equal to its size.

  // First, some simple bookkeeping to perform for ms_struct structs.
  if (IsMsStruct) {
    // The field alignment for integer types is always the size.
    FieldAlign = StorageUnitSize;

    // If the previous field was not a bitfield, or was a bitfield
    // with a different storage unit size, or if this field doesn't fit into
    // the current storage unit, we're done with that storage unit.
    if (LastBitfieldStorageUnitSize != StorageUnitSize ||
        UnfilledBitsInLastUnit < FieldSize) {
      // Also, ignore zero-length bitfields after non-bitfields.
      if (!LastBitfieldStorageUnitSize && !FieldSize)
        FieldAlign = 1;

      UnfilledBitsInLastUnit = 0;
      LastBitfieldStorageUnitSize = 0;
    }
  }

  if (FieldSize > StorageUnitSize)
    return;

  // Compute the next available bit offset.
  uint64_t FieldOffset =
      IsUnion ? 0 : (getDataSizeInBits() - UnfilledBitsInLastUnit);

  // Handle targets that don't honor bitfield type alignment.
  if (!IsMsStruct && !Context.getTargetInfo().useBitFieldTypeAlignment()) {
    // Some such targets do honor it on zero-width bitfields.
    if (FieldSize == 0 &&
        Context.getTargetInfo().useZeroLengthBitfieldAlignment()) {
      // Some targets don't honor leading zero-width bitfield.
      if (!IsUnion && FieldOffset == 0 &&
          !Context.getTargetInfo().useLeadingZeroLengthBitfield())
        FieldAlign = 1;
      else {
        // The alignment to round up to is the max of the field's natural
        // alignment and a target-specific fixed value (sometimes zero).
        unsigned ZeroLengthBitfieldBoundary =
            Context.getTargetInfo().getZeroLengthBitfieldBoundary();
        FieldAlign = std::max(FieldAlign, ZeroLengthBitfieldBoundary);
      }
      // If that doesn't apply, just ignore the field alignment.
    } else {
      FieldAlign = 1;
    }
  }

  // Remember the alignment we would have used if the field were not packed.
  unsigned UnpackedFieldAlign = FieldAlign;

  // Ignore the field alignment if the field is packed unless it has zero-size.
  if (!IsMsStruct && FieldPacked && FieldSize != 0)
    FieldAlign = 1;

  // But, if there's an 'aligned' attribute on the field, honor that.
  unsigned ExplicitFieldAlign = D->getMaxAlignment();
  if (ExplicitFieldAlign) {
    FieldAlign = std::max(FieldAlign, ExplicitFieldAlign);
    UnpackedFieldAlign = std::max(UnpackedFieldAlign, ExplicitFieldAlign);
  }

  // But, if there's a #pragma pack in play, that takes precedent over
  // even the 'aligned' attribute, for non-zero-width bitfields.
  unsigned MaxFieldAlignmentInBits = Context.toBits(MaxFieldAlignment);
  if (!MaxFieldAlignment.isZero() && FieldSize) {
    UnpackedFieldAlign = std::min(UnpackedFieldAlign, MaxFieldAlignmentInBits);
    if (FieldPacked)
      FieldAlign = UnpackedFieldAlign;
    else
      FieldAlign = std::min(FieldAlign, MaxFieldAlignmentInBits);
  }

  // But, ms_struct just ignores all of that in unions, even explicit
  // alignment attributes.
  if (IsMsStruct && IsUnion) {
    FieldAlign = UnpackedFieldAlign = 1;
  }

  // For purposes of diagnostics, we're going to simultaneously
  // compute the field offsets that we would have used if we weren't
  // adding any alignment padding or if the field weren't packed.
  uint64_t UnpaddedFieldOffset = FieldOffset;
  uint64_t UnpackedFieldOffset = FieldOffset;

  // Check if we need to add padding to fit the bitfield within an
  // allocation unit with the right size and alignment.  The rules are
  // somewhat different here for ms_struct structs.
  if (IsMsStruct) {
    // If it's not a zero-width bitfield, and we can fit the bitfield
    // into the active storage unit (and we haven't already decided to
    // start a new storage unit), just do so, regardless of any other
    // other consideration.  Otherwise, round up to the right alignment.
    if (FieldSize == 0 || FieldSize > UnfilledBitsInLastUnit) {
      FieldOffset = llvm::alignTo(FieldOffset, FieldAlign);
      UnpackedFieldOffset =
          llvm::alignTo(UnpackedFieldOffset, UnpackedFieldAlign);
      UnfilledBitsInLastUnit = 0;
    }

  } else {
    // #pragma pack, with any value, suppresses the insertion of padding.
    bool AllowPadding = MaxFieldAlignment.isZero();

    // Compute the real offset.
    if (FieldSize == 0 ||
        (AllowPadding &&
         (FieldOffset & (FieldAlign - 1)) + FieldSize > StorageUnitSize)) {
      FieldOffset = llvm::alignTo(FieldOffset, FieldAlign);
    } else if (ExplicitFieldAlign &&
               (MaxFieldAlignmentInBits == 0 ||
                ExplicitFieldAlign <= MaxFieldAlignmentInBits) &&
               Context.getTargetInfo().useExplicitBitFieldAlignment()) {
      FieldOffset = llvm::alignTo(FieldOffset, ExplicitFieldAlign);
    }

    // Repeat the computation for diagnostic purposes.
    if (FieldSize == 0 ||
        (AllowPadding &&
         (UnpackedFieldOffset & (UnpackedFieldAlign - 1)) + FieldSize >
             StorageUnitSize))
      UnpackedFieldOffset =
          llvm::alignTo(UnpackedFieldOffset, UnpackedFieldAlign);
    else if (ExplicitFieldAlign &&
             (MaxFieldAlignmentInBits == 0 ||
              ExplicitFieldAlign <= MaxFieldAlignmentInBits) &&
             Context.getTargetInfo().useExplicitBitFieldAlignment())
      UnpackedFieldOffset =
          llvm::alignTo(UnpackedFieldOffset, ExplicitFieldAlign);
  }

  // If we're using external layout, give the external layout a chance
  // to override this information.
  if (UseExternalLayout)
    FieldOffset = updateExternalFieldOffset(D, FieldOffset);

  // Okay, place the bitfield at the calculated offset.
  FieldOffsets.push_back(FieldOffset);

  // Bookkeeping:

  // Anonymous members don't affect the overall record alignment,
  // except on targets where they do.
  if (!IsMsStruct &&
      !Context.getTargetInfo().useZeroLengthBitfieldAlignment() &&
      !D->getIdentifier())
    FieldAlign = UnpackedFieldAlign = 1;

  // Diagnose differences in layout due to padding or packing.
  if (!UseExternalLayout)
    CheckFieldPadding(FieldOffset, UnpaddedFieldOffset, UnpackedFieldOffset,
                      UnpackedFieldAlign, FieldPacked, D);

  // Update DataSize to include the last byte containing (part of) the bitfield.

  // For unions, this is just a max operation, as usual.
  if (IsUnion) {
    // For ms_struct, allocate the entire storage unit --- unless this
    // is a zero-width bitfield, in which case just use a size of 1.
    uint64_t RoundedFieldSize;
    if (IsMsStruct) {
      RoundedFieldSize = (FieldSize ? StorageUnitSize
                                    : Context.getTargetInfo().getCharWidth());

      // Otherwise, allocate just the number of bytes required to store
      // the bitfield.
    } else {
      RoundedFieldSize = roundUpSizeToCharAlignment(FieldSize, Context);
    }
    setDataSize(std::max(getDataSizeInBits(), RoundedFieldSize));

    // For non-zero-width bitfields in ms_struct structs, allocate a new
    // storage unit if necessary.
  } else if (IsMsStruct && FieldSize) {
    // We should have cleared UnfilledBitsInLastUnit in every case
    // where we changed storage units.
    if (!UnfilledBitsInLastUnit) {
      setDataSize(FieldOffset + StorageUnitSize);
      UnfilledBitsInLastUnit = StorageUnitSize;
    }
    UnfilledBitsInLastUnit -= FieldSize;
    LastBitfieldStorageUnitSize = StorageUnitSize;

    // Otherwise, bump the data size up to include the bitfield,
    // including padding up to char alignment, and then remember how
    // bits we didn't use.
  } else {
    uint64_t NewSizeInBits = FieldOffset + FieldSize;
    uint64_t CharAlignment = Context.getTargetInfo().getCharAlign();
    setDataSize(llvm::alignTo(NewSizeInBits, CharAlignment));
    UnfilledBitsInLastUnit = getDataSizeInBits() - NewSizeInBits;

    // The only time we can get here for an ms_struct is if this is a
    // zero-width bitfield, which doesn't count as anything for the
    // purposes of unfilled bits.
    LastBitfieldStorageUnitSize = 0;
  }

  // Update the size.
  setSize(std::max(getSizeInBits(), getDataSizeInBits()));

  // Remember max struct alignment.
  UnadjustedAlignment =
      std::max(UnadjustedAlignment, Context.toCharUnitsFromBits(FieldAlign));
  UpdateAlignment(Context.toCharUnitsFromBits(FieldAlign),
                  Context.toCharUnitsFromBits(UnpackedFieldAlign));
}

void ItaniumRecordLayoutBuilder::LayoutField(const FieldDecl *D) {
  CharUnits FieldOffset = IsUnion ? CharUnits::Zero() : getDataSize();

  if (D->isBitField()) {
    LayoutBitField(D);
    return;
  }

  uint64_t UnpaddedFieldOffset = getDataSizeInBits() - UnfilledBitsInLastUnit;
  // Reset the unfilled bits.
  UnfilledBitsInLastUnit = 0;
  LastBitfieldStorageUnitSize = 0;

  bool IsIncompleteArray = D->getType()->isIncompleteArrayType();
  auto TI = Context.getTypeInfoInChars(D->getType());
  CharUnits FieldAlign = TI.Align;
  CharUnits FieldSize = IsIncompleteArray ? CharUnits::Zero() : TI.Width;

  if (!IsIncompleteArray) {
    if (IsMsStruct) {
      // If MS bitfield layout is required, figure out what type is being
      // laid out and align the field to the width of that type.

      // Resolve all typedefs down to their base type and round up the field
      // alignment if necessary.
      QualType T = Context.getBaseElementType(D->getType());
      if (const BuiltinType *BTy = T->getAs<BuiltinType>()) {
        CharUnits TypeSize = Context.getTypeSizeInChars(BTy);

        if (!llvm::isPowerOf2_64(TypeSize.getQuantity())) {
          assert(
              !Context.getTargetInfo().getTriple().isWindowsMSVCEnvironment() &&
              "Non PowerOf2 size in MSVC mode");
          Diag(D->getLocation(), diag::warn_npot_ms_struct);
        }
        if (TypeSize > FieldAlign &&
            llvm::isPowerOf2_64(TypeSize.getQuantity()))
          FieldAlign = TypeSize;
      }
    }
  }

  bool FieldPacked = Packed || D->hasAttr<PackedAttr>();

  CharUnits UnpackedFieldAlign = FieldAlign;
  CharUnits PackedFieldAlign = CharUnits::One();
  CharUnits UnpackedFieldOffset = FieldOffset;
  CharUnits OriginalFieldAlign = UnpackedFieldAlign;

  CharUnits MaxAlignmentInChars =
      Context.toCharUnitsFromBits(D->getMaxAlignment());
  PackedFieldAlign = std::max(PackedFieldAlign, MaxAlignmentInChars);
  UnpackedFieldAlign = std::max(UnpackedFieldAlign, MaxAlignmentInChars);

  if (!MaxFieldAlignment.isZero()) {
    PackedFieldAlign = std::min(PackedFieldAlign, MaxFieldAlignment);
    UnpackedFieldAlign = std::min(UnpackedFieldAlign, MaxFieldAlignment);
  }

  if (FieldPacked)
    FieldAlign = PackedFieldAlign;
  else
    FieldAlign = UnpackedFieldAlign;

  CharUnits AlignTo = FieldAlign;
  // Round up the current record size to the field's alignment boundary.
  FieldOffset = FieldOffset.alignTo(AlignTo);
  UnpackedFieldOffset = UnpackedFieldOffset.alignTo(UnpackedFieldAlign);

  if (UseExternalLayout) {
    FieldOffset = Context.toCharUnitsFromBits(
        updateExternalFieldOffset(D, Context.toBits(FieldOffset)));
  }

  // Place this field at the current location.
  FieldOffsets.push_back(Context.toBits(FieldOffset));

  if (!UseExternalLayout)
    CheckFieldPadding(Context.toBits(FieldOffset), UnpaddedFieldOffset,
                      Context.toBits(UnpackedFieldOffset),
                      Context.toBits(UnpackedFieldAlign), FieldPacked, D);

  // Reserve space for this field.
  if (IsUnion) {
    uint64_t FieldSizeInBits = Context.toBits(FieldSize);
    setDataSize(std::max(getDataSizeInBits(), FieldSizeInBits));
  } else
    setDataSize(FieldOffset + FieldSize);

  PaddedFieldSize = std::max(PaddedFieldSize, FieldOffset + FieldSize);
  setSize(std::max(getSizeInBits(), getDataSizeInBits()));

  // Remember max struct ABI-specified alignment.
  UnadjustedAlignment = std::max(UnadjustedAlignment, FieldAlign);
  UpdateAlignment(FieldAlign, UnpackedFieldAlign);

  // For checking the alignment of inner fields against
  // the alignment of its parent record.
  if (const RecordDecl *RD = D->getParent()) {
    // Check if packed attribute or pragma pack is present.
    if (RD->hasAttr<PackedAttr>() || !MaxFieldAlignment.isZero())
      if (FieldAlign < OriginalFieldAlign)
        if (D->getType()->isRecordType()) {
          // If the offset is a multiple of the alignment of
          // the type, raise the warning.
          if (FieldOffset % OriginalFieldAlign != 0)
            Diag(D->getLocation(), diag::warn_unaligned_access)
                << Context.getTypeDeclType(RD) << D->getName() << D->getType();
        }
  }

  if (Packed && !FieldPacked && PackedFieldAlign < FieldAlign)
    Diag(D->getLocation(), diag::warn_unpacked_field) << D;
}

void ItaniumRecordLayoutBuilder::FinishLayout(const NamedDecl *D) {
  // If we have any remaining field tail padding, include that in the overall
  // size.
  setSize(std::max(getSizeInBits(), (uint64_t)Context.toBits(PaddedFieldSize)));

  // Finally, round the size of the record up to the alignment of the
  // record itself.
  uint64_t UnpaddedSize = getSizeInBits() - UnfilledBitsInLastUnit;
  uint64_t UnpackedSizeInBits =
      llvm::alignTo(getSizeInBits(), Context.toBits(UnpackedAlignment));

  uint64_t RoundedSize =
      llvm::alignTo(getSizeInBits(), Context.toBits(Alignment));

  if (UseExternalLayout) {
    // If we're inferring alignment, and the external size is smaller than
    // our size after we've rounded up to alignment, conservatively set the
    // alignment to 1.
    if (InferAlignment && External.Size < RoundedSize) {
      Alignment = CharUnits::One();
      InferAlignment = false;
    }
    setSize(External.Size);
    return;
  }

  // Set the size to the final size.
  setSize(RoundedSize);

  unsigned CharBitNum = Context.getTargetInfo().getCharWidth();
  if (const RecordDecl *RD = dyn_cast<RecordDecl>(D)) {
    // Warn if padding was introduced to the struct/union.
    if (getSizeInBits() > UnpaddedSize) {
      unsigned PadSize = getSizeInBits() - UnpaddedSize;
      bool InBits = true;
      if (PadSize % CharBitNum == 0) {
        PadSize = PadSize / CharBitNum;
        InBits = false;
      }
      Diag(RD->getLocation(), diag::warn_padded_struct_size)
          << Context.getTypeDeclType(RD) << PadSize
          << (InBits ? 1 : 0); // (byte|bit)
    }

    // Warn if we packed it unnecessarily, when the unpacked alignment is not
    // greater than the one after packing, the size in bits doesn't change and
    // the offset of each field is identical.
    if (Packed && UnpackedAlignment <= Alignment &&
        UnpackedSizeInBits == getSizeInBits() && !HasPackedField)
      Diag(D->getLocation(), diag::warn_unnecessary_packed)
          << Context.getTypeDeclType(RD);
  }
}

void ItaniumRecordLayoutBuilder::UpdateAlignment(
    CharUnits NewAlignment, CharUnits UnpackedNewAlignment) {
  if (UseExternalLayout && !InferAlignment)
    return;

  if (NewAlignment > Alignment) {
    assert(llvm::isPowerOf2_64(NewAlignment.getQuantity()) &&
           "Alignment not a power of 2");
    Alignment = NewAlignment;
  }

  if (UnpackedNewAlignment > UnpackedAlignment) {
    assert(llvm::isPowerOf2_64(UnpackedNewAlignment.getQuantity()) &&
           "Alignment not a power of 2");
    UnpackedAlignment = UnpackedNewAlignment;
  }
}

uint64_t
ItaniumRecordLayoutBuilder::updateExternalFieldOffset(const FieldDecl *Field,
                                                      uint64_t ComputedOffset) {
  uint64_t ExternalFieldOffset = External.getExternalFieldOffset(Field);

  if (InferAlignment && ExternalFieldOffset < ComputedOffset) {
    // The externally-supplied field offset is before the field offset we
    // computed. Assume that the structure is packed.
    Alignment = CharUnits::One();
    InferAlignment = false;
  }

  // Use the externally-supplied field offset.
  return ExternalFieldOffset;
}

void ItaniumRecordLayoutBuilder::CheckFieldPadding(
    uint64_t Offset, uint64_t UnpaddedOffset, uint64_t UnpackedOffset,
    unsigned UnpackedAlign, bool isPacked, const FieldDecl *D) {
  // Don't warn about structs created without a SourceLocation.  This can
  // be done by clients of the AST, such as codegen.
  if (D->getLocation().isInvalid())
    return;

  unsigned CharBitNum = Context.getTargetInfo().getCharWidth();

  // Warn if padding was introduced to the struct.
  if (!IsUnion && Offset > UnpaddedOffset) {
    unsigned PadSize = Offset - UnpaddedOffset;
    bool InBits = true;
    if (PadSize % CharBitNum == 0) {
      PadSize = PadSize / CharBitNum;
      InBits = false;
    }
    if (D->getIdentifier()) {
      auto Diagnostic = D->isBitField() ? diag::warn_padded_struct_bitfield
                                        : diag::warn_padded_struct_field;
      Diag(D->getLocation(), Diagnostic)
          << Context.getTypeDeclType(D->getParent()) << PadSize
          << (InBits ? 1 : 0) // (byte|bit)
          << D->getIdentifier();
    } else {
      auto Diagnostic = D->isBitField() ? diag::warn_padded_struct_anon_bitfield
                                        : diag::warn_padded_struct_anon_field;
      Diag(D->getLocation(), Diagnostic)
          << Context.getTypeDeclType(D->getParent()) << PadSize
          << (InBits ? 1 : 0); // (byte|bit)
    }
  }
  if (isPacked && Offset != UnpackedOffset) {
    HasPackedField = true;
  }
}

DiagnosticBuilder ItaniumRecordLayoutBuilder::Diag(SourceLocation Loc,
                                                   unsigned DiagID) {
  return Context.getDiagnostics().Report(Loc, DiagID);
}

const StructRecordLayout &
TreeContext::getStructRecordLayout(const RecordDecl *D) const {
  // These asserts test different things.  A record has a definition
  // as soon as we begin to parse the definition.  That definition is
  // not a complete definition (which is what isDefinition() tests)
  // until we *finish* parsing the definition.

  (void)D->getMostRecentDecl();

  D = D->getDefinition();
  assert(D && "Cannot get layout of forward declarations!");
  assert(!D->isInvalidDecl() && "Cannot get layout of invalid decl!");
  assert(D->isCompleteDefinition() && "Cannot layout type before complete!");

  // Look up this layout, if already laid out, return what we have.
  // Note that we can't save a reference to the entry because this function
  // is recursive.
  const StructRecordLayout *Entry = StructRecordLayouts[D];
  if (Entry)
    return *Entry;

  const StructRecordLayout *NewEntry = nullptr;

  {
    ItaniumRecordLayoutBuilder Builder(*this);
    Builder.Layout(D);

    NewEntry = new (*this) StructRecordLayout(
        *this, Builder.getSize(), Builder.Alignment,
        Builder.UnadjustedAlignment, Builder.getSize(), Builder.FieldOffsets);
  }

  StructRecordLayouts[D] = NewEntry;
  return *NewEntry;
}

namespace {
uint64_t getFieldOffset(const TreeContext &C, const FieldDecl *FD) {
  const StructRecordLayout &Layout = C.getStructRecordLayout(FD->getParent());
  return Layout.getFieldOffset(FD->getFieldIndex());
}
} // namespace

uint64_t TreeContext::getFieldOffset(const ValueDecl *VD) const {
  uint64_t OffsetInBits;
  if (const FieldDecl *FD = dyn_cast<FieldDecl>(VD)) {
    OffsetInBits = ::getFieldOffset(*this, FD);
  } else {
    const IndirectFieldDecl *IFD = cast<IndirectFieldDecl>(VD);

    OffsetInBits = 0;
    for (const NamedDecl *ND : IFD->chain())
      OffsetInBits += ::getFieldOffset(*this, cast<FieldDecl>(ND));
  }

  return OffsetInBits;
}

void StructRecordLayout::Destroy(TreeContext &Ctx) {
  this->~StructRecordLayout();
  Ctx.Deallocate(this);
}

StructRecordLayout::StructRecordLayout(const TreeContext &Ctx, CharUnits size,
                                       CharUnits alignment,
                                       CharUnits unadjustedAlignment,
                                       CharUnits datasize,
                                       llvm::ArrayRef<uint64_t> fieldoffsets)
    : Size(size), DataSize(datasize), Alignment(alignment),
      UnadjustedAlignment(unadjustedAlignment) {
  FieldOffsets.append(Ctx, fieldoffsets.begin(), fieldoffsets.end());
}
