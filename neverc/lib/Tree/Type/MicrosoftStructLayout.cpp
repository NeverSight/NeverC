//===- MicrosoftStructLayout.cpp - Microsoft C records ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Adapted from Clang's MicrosoftRecordLayoutBuilder. Only C records belong
// here; the tree layout is also the authority used by record IR emission.
//===----------------------------------------------------------------------===//

#include "MicrosoftStructLayout.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Tree/Core/Attr.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Core/TreeDiag.h"
#include "neverc/Tree/Type/StructLayout.h"
#include <algorithm>

using namespace neverc;

namespace {
class MicrosoftCLayoutBuilder {
  const TreeContext &Context;
  const RecordDecl *Record;
  detail::MicrosoftStructLayout Result{CharUnits::Zero(),
                                       CharUnits::One(),
                                       CharUnits::Zero(),
                                       CharUnits::One(),
                                       {}};
  CharUnits MaxFieldAlignment = CharUnits::Zero();
  CharUnits CurrentBitfieldSize = CharUnits::Zero();
  uint64_t RemainingBits = 0;
  bool LastNonzeroBitfield = false;

  struct ElementInfo {
    CharUnits Size, Alignment;
  };

  ElementInfo fieldInfo(const FieldDecl *Field) {
    const auto Type = Field->getType();
    const auto Info =
        Context.getTypeInfoInChars(Type->getUnqualifiedDesugaredType());
    ElementInfo E{Info.Width, Info.Align};
    CharUnits Required = Context.toCharUnitsFromBits(Field->getMaxAlignment());
    if (Context.isAlignmentRequired(Type))
      Required = std::max(Required, Context.getTypeAlignInChars(Type));
    if (Field->isBitField())
      E.Alignment = std::max(E.Alignment, Required);
    else {
      if (const auto *RT =
              Context.getBaseElementType(Type)->getAs<RecordType>())
        Required =
            std::max(Required, Context.getStructRecordLayout(RT->getDecl())
                                   .getRequiredAlignment());
      Result.RequiredAlignment = std::max(Result.RequiredAlignment, Required);
    }
    if (!MaxFieldAlignment.isZero())
      E.Alignment = std::min(E.Alignment, MaxFieldAlignment);
    if (Field->hasAttr<PackedAttr>())
      E.Alignment = CharUnits::One();
    E.Alignment = std::max(E.Alignment, Required);
    return E;
  }

  void place(const FieldDecl *Field, CharUnits Offset) {
    const uint64_t Bits = Context.toBits(Offset);
    const uint64_t Unpadded = Context.toBits(Result.Size) - RemainingBits;
    Result.FieldOffsets.push_back(Bits);
    if (Record->isUnion() || Bits <= Unpadded ||
        Field->getLocation().isInvalid())
      return;
    unsigned Padding = Bits - Unpadded;
    const unsigned CharBits = Context.getCharWidth();
    const bool InBits = Padding % CharBits;
    if (!InBits)
      Padding /= CharBits;
    const unsigned ID =
        Field->getIdentifier()
            ? (Field->isBitField() ? diag::warn_padded_struct_bitfield
                                   : diag::warn_padded_struct_field)
            : (Field->isBitField() ? diag::warn_padded_struct_anon_bitfield
                                   : diag::warn_padded_struct_anon_field);
    auto Diag = Context.getDiagnostics().Report(Field->getLocation(), ID);
    Diag << Context.getTypeDeclType(Record) << Padding << unsigned(InBits);
    if (Field->getIdentifier())
      Diag << Field->getIdentifier();
  }

  void layoutField(const FieldDecl *Field) {
    if (Field->isBitField()) {
      layoutBitField(Field);
      return;
    }
    LastNonzeroBitfield = false;
    const auto Info = fieldInfo(Field);
    const auto Offset = Record->isUnion() ? CharUnits::Zero()
                                          : Result.Size.alignTo(Info.Alignment);
    place(Field, Offset);
    RemainingBits = 0;
    Result.Size = std::max(Result.Size, Offset + Info.Size);
    Result.Alignment = std::max(Result.Alignment, Info.Alignment);
  }

  void layoutBitField(const FieldDecl *Field) {
    unsigned Width = Field->getBitWidthValue(Context);
    if (!Width && !LastNonzeroBitfield) {
      Result.FieldOffsets.push_back(
          Record->isUnion() ? 0 : Context.toBits(Result.Size));
      return;
    }
    const auto Info = fieldInfo(Field);
    Width = std::min<uint64_t>(Width, Context.toBits(Info.Size));
    if (Width && !Record->isUnion() && LastNonzeroBitfield &&
        CurrentBitfieldSize == Info.Size && Width <= RemainingBits) {
      Result.FieldOffsets.push_back(Context.toBits(Result.Size) -
                                    RemainingBits);
      RemainingBits -= Width;
      return;
    }
    LastNonzeroBitfield = Width != 0;
    CurrentBitfieldSize = Info.Size;
    if (Record->isUnion()) {
      Result.FieldOffsets.push_back(0);
      Result.Size = std::max(Result.Size, Info.Size);
      // Microsoft unions allocate full units but ignore bitfield alignment.
      return;
    }
    const auto Offset = Result.Size.alignTo(Info.Alignment);
    place(Field, Offset);
    Result.Alignment = std::max(Result.Alignment, Info.Alignment);
    Result.Size = Offset + (Width ? Info.Size : CharUnits::Zero());
    RemainingBits = Width ? Context.toBits(Info.Size) - Width : 0;
  }

public:
  MicrosoftCLayoutBuilder(const TreeContext &Context, const RecordDecl *Record)
      : Context(Context), Record(Record) {}

  detail::MicrosoftStructLayout build() {
    if (unsigned Pack = Context.getLangOpts().PackStruct)
      MaxFieldAlignment = CharUnits::fromQuantity(Pack);
    if (const auto *Pack = Record->getAttr<MaxFieldAlignmentAttr>())
      if (Pack->getAlignment() <=
          Context.getTargetInfo().getPointerWidth(LangAS::Default))
        MaxFieldAlignment = Context.toCharUnitsFromBits(Pack->getAlignment());
    if (Record->hasAttr<PackedAttr>())
      MaxFieldAlignment = CharUnits::One();
    for (const auto *Field : Record->fields())
      layoutField(Field);
    const uint64_t Unpadded = Context.toBits(Result.Size) - RemainingBits;
    Result.UnadjustedAlignment = Result.Alignment;
    Result.RequiredAlignment =
        std::max(Result.RequiredAlignment,
                 Context.toCharUnitsFromBits(Record->getMaxAlignment()));
    Result.Alignment = std::max(Result.Alignment, Result.RequiredAlignment);
    Result.Size = Result.Size.alignTo(Result.Alignment);
    // Microsoft C empty records have four bytes unless explicit alignment
    // requires a larger object. This is distinct from C++ empty classes.
    const auto EmptySize = CharUnits::fromQuantity(4);
    if (Result.Size.isZero())
      Result.Size =
          Result.RequiredAlignment >= EmptySize ? Result.Alignment : EmptySize;
    if (Context.toBits(Result.Size) > Unpadded &&
        Record->getLocation().isValid()) {
      unsigned Padding = Context.toBits(Result.Size) - Unpadded;
      const unsigned CharBits = Context.getCharWidth();
      const bool InBits = Padding % CharBits;
      if (!InBits)
        Padding /= CharBits;
      Context.getDiagnostics().Report(Record->getLocation(),
                                      diag::warn_padded_struct_size)
          << Context.getTypeDeclType(Record) << Padding << unsigned(InBits);
    }
    return std::move(Result);
  }
};
} // namespace

detail::MicrosoftStructLayout
detail::computeMicrosoftStructLayout(const TreeContext &Context,
                                     const RecordDecl *Record) {
  return MicrosoftCLayoutBuilder(Context, Record).build();
}
