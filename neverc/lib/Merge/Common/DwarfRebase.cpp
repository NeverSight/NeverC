//===- DwarfRebase.cpp - Re-point DWARF offsets after an object merge -----===//

#include "DwarfRebase.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DWARF/DWARFAbbreviationDeclaration.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugAbbrev.h"
#include "llvm/DebugInfo/DWARF/DWARFFormValue.h"
#include "llvm/DebugInfo/DWARF/DWARFUnitIndex.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MathExtras.h"

#include <limits>

using namespace llvm;

namespace neverc::merge {

namespace {

/// Which section an attribute's value indexes into, or Count when the value
/// is not a reference into another DWARF section and must be left alone.
///
/// A form alone is not always enough: DW_FORM_sec_offset says only "an offset
/// into some section", and the attribute decides which one.
DwarfSection sectionForAttribute(dwarf::Attribute Attr, dwarf::Form Form,
                                 uint16_t Version) {
  switch (Form) {
  case dwarf::DW_FORM_strp:
    return DwarfSection::Str;
  case dwarf::DW_FORM_line_strp:
    return DwarfSection::LineStr;
  // A reference to a DIE in another unit is an offset into .debug_info
  // itself, so it moves with this partition's contribution.
  case dwarf::DW_FORM_ref_addr:
    return DwarfSection::Info;
  case dwarf::DW_FORM_sec_offset:
    break;
  // Before DWARF 4 introduced DW_FORM_sec_offset, section offsets used the
  // fixed-width data forms.  The attribute still identifies the destination.
  case dwarf::DW_FORM_data4:
  case dwarf::DW_FORM_data8:
    if (Version > 3)
      return DwarfSection::Count;
    break;
  default:
    return DwarfSection::Count;
  }

  switch (Attr) {
  case dwarf::DW_AT_stmt_list:
    return DwarfSection::Line;
  // DWARF 5 moved range and location lists into their own sections.
  case dwarf::DW_AT_ranges:
  case dwarf::DW_AT_start_scope:
    return Version >= 5 ? DwarfSection::RngLists : DwarfSection::Ranges;
  case dwarf::DW_AT_location:
  case dwarf::DW_AT_string_length:
  case dwarf::DW_AT_return_addr:
  case dwarf::DW_AT_data_member_location:
  case dwarf::DW_AT_frame_base:
  case dwarf::DW_AT_segment:
  case dwarf::DW_AT_static_link:
  case dwarf::DW_AT_use_location:
  case dwarf::DW_AT_vtable_elem_location:
    return Version >= 5 ? DwarfSection::LocLists : DwarfSection::Loc;
  case dwarf::DW_AT_str_offsets_base:
    return DwarfSection::StrOffsets;
  case dwarf::DW_AT_addr_base:
  case dwarf::DW_AT_GNU_addr_base:
    return DwarfSection::Addr;
  case dwarf::DW_AT_rnglists_base:
    return DwarfSection::RngLists;
  case dwarf::DW_AT_GNU_ranges_base:
    return DwarfSection::Ranges;
  case dwarf::DW_AT_loclists_base:
    return DwarfSection::LocLists;
  case dwarf::DW_AT_macro_info:
    return DwarfSection::MacInfo;
  case dwarf::DW_AT_macros:
  case dwarf::DW_AT_GNU_macros:
    return DwarfSection::Macro;
  case dwarf::DW_AT_GNU_locviews:
    return Version >= 5 ? DwarfSection::LocLists : DwarfSection::Loc;
  case dwarf::DW_AT_MIPS_fde:
    return DwarfSection::Frame;
  default:
    return DwarfSection::Count;
  }
}

/// Which section a line-table header entry's value points into, for the forms
/// a directory or file-name entry may legitimately use.  Everything else --
/// inline strings, indices, MD5 hashes -- is position independent.
DwarfSection sectionForLineEntryForm(dwarf::Form Form) {
  switch (Form) {
  case dwarf::DW_FORM_line_strp:
    return DwarfSection::LineStr;
  case dwarf::DW_FORM_strp:
    return DwarfSection::Str;
  default:
    return DwarfSection::Count;
  }
}

uint8_t sectionOffsetByteSize(dwarf::Form Form,
                              const dwarf::FormParams &Params) {
  switch (Form) {
  case dwarf::DW_FORM_data4:
    return sizeof(uint32_t);
  case dwarf::DW_FORM_data8:
    return sizeof(uint64_t);
  case dwarf::DW_FORM_ref_addr:
    return Params.getRefAddrByteSize();
  default:
    return Params.getDwarfOffsetByteSize();
  }
}

uint64_t readOffset(const DataExtractor &Data, uint64_t *Cursor,
                    uint8_t ByteSize) {
  return Data.getUnsigned(Cursor, ByteSize);
}

bool writeOffset(MutableArrayRef<char> Buf, uint64_t Pos, uint64_t Value,
                 uint8_t ByteSize, bool LE) {
  if (ByteSize == 0 || ByteSize > sizeof(uint64_t) ||
      Pos > Buf.size() || ByteSize > Buf.size() - Pos ||
      (ByteSize < sizeof(uint64_t) &&
       Value >= (uint64_t{1} << (ByteSize * 8))))
    return false;
  auto *P = reinterpret_cast<uint8_t *>(Buf.data() + Pos);
  for (uint8_t I = 0; I != ByteSize; ++I) {
    unsigned Shift = LE ? 8 * I : 8 * (ByteSize - 1 - I);
    P[I] = static_cast<uint8_t>((Value >> Shift) & 0xff);
  }
  return true;
}

std::optional<uint64_t> shiftedOffset(uint64_t Value, uint64_t Delta,
                                      uint8_t ByteSize) {
  if (ByteSize == 0 || ByteSize > sizeof(uint64_t) ||
      Delta > std::numeric_limits<uint64_t>::max() - Value)
    return std::nullopt;
  const uint64_t Shifted = Value + Delta;
  if (ByteSize < sizeof(uint64_t) &&
      Shifted >= (uint64_t{1} << (ByteSize * 8)))
    return std::nullopt;
  return Shifted;
}

std::optional<uint64_t> checkedEnd(uint64_t Cursor, uint64_t Length,
                                   uint64_t Limit) {
  if (Cursor > Limit || Length > Limit - Cursor)
    return std::nullopt;
  return Cursor + Length;
}

std::optional<uint64_t> readULEB128(const DataExtractor &Data,
                                    uint64_t *Cursor, uint64_t Limit) {
  if (*Cursor >= Limit)
    return std::nullopt;

  Error ExtractError = Error::success();
  const uint64_t Value = Data.getULEB128(Cursor, &ExtractError);
  if (ExtractError) {
    consumeError(std::move(ExtractError));
    return std::nullopt;
  }
  if (*Cursor > Limit)
    return std::nullopt;
  return Value;
}

/// Resolve DW_FORM_indirect at the point where its concrete form is encoded.
/// The concrete form may itself be indirect, so consume until reaching the
/// value's actual representation. DW_FORM_implicit_const is invalid here:
/// its value belongs to an abbreviation declaration, which an indirect form
/// does not carry.
std::optional<dwarf::Form>
resolveIndirectForm(const DataExtractor &Data, uint64_t *Cursor,
                    uint64_t Limit, dwarf::Form Form) {
  while (Form == dwarf::DW_FORM_indirect) {
    const std::optional<uint64_t> Encoded =
        readULEB128(Data, Cursor, Limit);
    if (!Encoded || *Encoded > std::numeric_limits<uint16_t>::max())
      return std::nullopt;
    Form = static_cast<dwarf::Form>(*Encoded);
  }
  if (Form == dwarf::DW_FORM_implicit_const)
    return std::nullopt;
  return Form;
}

/// Add \p Delta to each of \p Count consecutive offsets starting at \p Cursor,
/// advancing it past them.  Returns false unless they all fit below \p Limit,
/// which is the end of the unit being read -- bounding by the whole section
/// would let a malformed count walk into the next unit and rewrite it.
bool shiftOffsetArray(MutableArrayRef<char> Data, const DataExtractor &Extract,
                      uint64_t *Cursor, uint64_t Count, uint64_t Delta,
                      uint8_t OffsetSize, uint64_t Limit, bool LE) {
  if ((OffsetSize != 4 && OffsetSize != 8) || *Cursor > Limit ||
      Count > (Limit - *Cursor) / OffsetSize)
    return false;
  for (uint64_t I = 0; I != Count; ++I) {
    const uint64_t Pos = *Cursor;
    const uint64_t Value = readOffset(Extract, Cursor, OffsetSize);
    if (Delta != 0) {
      std::optional<uint64_t> Shifted =
          shiftedOffset(Value, Delta, OffsetSize);
      if (!Shifted || !writeOffset(Data, Pos, *Shifted, OffsetSize, LE))
        return false;
    }
  }
  return true;
}

/// Read an initial-length field, setting \p OffsetSize to 4 or 8.  Returns the
/// length, or std::nullopt for the reserved values that are not a length.
std::optional<uint64_t> readInitialLength(const DataExtractor &Data,
                                          uint64_t Limit,
                                          uint64_t *Cursor,
                                          uint8_t &OffsetSize) {
  if (*Cursor > Limit || Limit - *Cursor < sizeof(uint32_t))
    return std::nullopt;
  const uint64_t First = Data.getU32(Cursor);
  if (First == 0xffffffffu) {
    if (*Cursor > Limit || Limit - *Cursor < sizeof(uint64_t))
      return std::nullopt;
    OffsetSize = 8;
    return Data.getU64(Cursor);
  }
  // 0xfffffff0..0xfffffffe are reserved; 0 is not a well-formed unit.
  if (First == 0 || First >= 0xfffffff0u)
    return std::nullopt;
  OffsetSize = 4;
  return First;
}

// ===----------------------------------------------------------------------===
// .debug_info
// ===----------------------------------------------------------------------===

bool rebaseDebugInfo(MutableArrayRef<char> Info, ArrayRef<char> Abbrev,
                     const PartitionDwarf &Part, DwarfSection UnitSection,
                     bool LE) {
  if (Info.empty())
    return true;
  if (UnitSection != DwarfSection::Info &&
      UnitSection != DwarfSection::Types)
    return false;

  const DataExtractor AbbrevData(StringRef(Abbrev.data(), Abbrev.size()), LE,
                                 /*AddressSize=*/8);
  const DataExtractor InfoData(StringRef(Info.data(), Info.size()), LE,
                               /*AddressSize=*/8);

  uint64_t Cursor = 0;
  while (Cursor < Info.size()) {
    uint8_t OffsetSize = 4;
    std::optional<uint64_t> UnitLength =
        readInitialLength(InfoData, Info.size(), &Cursor, OffsetSize);
    if (!UnitLength)
      return false;
    // A unit that runs past the contribution means we lost sync; rewriting
    // further would corrupt data that is currently merely mis-pointed.
    std::optional<uint64_t> UnitEnd =
        checkedEnd(Cursor, *UnitLength, Info.size());
    if (!UnitEnd || *UnitEnd - Cursor < sizeof(uint16_t))
      return false;

    const uint16_t Version = InfoData.getU16(&Cursor);
    if (Version < 2 || Version > 5)
      return false;
    uint8_t AddrSize;
    uint64_t AbbrOffPos;
    uint64_t AbbrOff;
    if (Version >= 5) {
      if (*UnitEnd - Cursor < 2 + OffsetSize)
        return false;
      const uint8_t UnitType = InfoData.getU8(&Cursor);
      AddrSize = InfoData.getU8(&Cursor);
      AbbrOffPos = Cursor;
      AbbrOff = readOffset(InfoData, &Cursor, OffsetSize);
      uint64_t ExtraHeaderSize = 0;
      switch (UnitType) {
      case dwarf::DW_UT_compile:
      case dwarf::DW_UT_partial:
        break;
      case dwarf::DW_UT_skeleton:
      case dwarf::DW_UT_split_compile:
        ExtraHeaderSize = sizeof(uint64_t); // dwo_id
        break;
      case dwarf::DW_UT_type:
      case dwarf::DW_UT_split_type:
        ExtraHeaderSize = sizeof(uint64_t) + OffsetSize;
        break;
      default:
        return false;
      }
      if (ExtraHeaderSize > *UnitEnd - Cursor)
        return false;
      Cursor += ExtraHeaderSize;
    } else {
      if (*UnitEnd - Cursor < OffsetSize + 1)
        return false;
      AbbrOffPos = Cursor;
      AbbrOff = readOffset(InfoData, &Cursor, OffsetSize);
      AddrSize = InfoData.getU8(&Cursor);
      if (UnitSection == DwarfSection::Types) {
        // DWARF 4 type units carry a signature and a unit-relative type DIE
        // offset after the common pre-v5 header.
        if (Version != 4 ||
            *UnitEnd - Cursor < sizeof(uint64_t) + OffsetSize)
          return false;
        Cursor += sizeof(uint64_t) + OffsetSize;
      }
    }
    if (AddrSize == 0 || AddrSize > sizeof(uint64_t))
      return false;

    // The abbreviations are read at their pre-merge offset, so parse before
    // the header is rewritten.  LLVM's DataExtractor::getULEB128 asserts
    // `*OffsetPtr <= Bytes.size()` and only then returns Error, so an
    // attacker-controlled debug_abbrev_offset past the contribution (or an
    // absent .debug_abbrev) would abort rather than fail the merge.  Bound it
    // here the way readULEB128 already bounds every later cursor.
    if (AbbrOff > Abbrev.size())
      return false;
    DWARFAbbreviationDeclarationSet Decls;
    uint64_t AbbrCursor = AbbrOff;
    if (Error E = Decls.extract(AbbrevData, &AbbrCursor)) {
      consumeError(std::move(E));
      return false;
    }
    std::optional<uint64_t> ShiftedAbbr =
        shiftedOffset(AbbrOff, Part.start(DwarfSection::Abbrev), OffsetSize);
    if (!ShiftedAbbr ||
        !writeOffset(Info, AbbrOffPos, *ShiftedAbbr, OffsetSize, LE))
      return false;

    const dwarf::FormParams Params{
        Version, AddrSize, OffsetSize == 8 ? dwarf::DWARF64 : dwarf::DWARF32};

    while (Cursor < *UnitEnd) {
      const std::optional<uint64_t> Code =
          readULEB128(InfoData, &Cursor, *UnitEnd);
      if (!Code)
        return false;
      if (*Code == 0) // end of a sibling chain
        continue;
      if (*Code > std::numeric_limits<uint32_t>::max())
        return false;
      const DWARFAbbreviationDeclaration *Decl =
          Decls.getAbbreviationDeclaration(static_cast<uint32_t>(*Code));
      if (!Decl)
        return false;

      for (const auto &Spec : Decl->attributes()) {
        if (Spec.isImplicitConst())
          continue; // the value lives in the abbreviation, not the data

        std::optional<dwarf::Form> ResolvedForm =
            resolveIndirectForm(InfoData, &Cursor, *UnitEnd, Spec.Form);
        if (!ResolvedForm)
          return false;
        const dwarf::Form Form = *ResolvedForm;
        const DwarfSection Target =
            sectionForAttribute(Spec.Attr, Form, Version);
        const uint64_t Delta =
            Target == DwarfSection::Count ? 0 : Part.start(Target);
        if (Delta == 0) {
          if (!DWARFFormValue::skipValue(Form, InfoData, &Cursor, Params))
            return false;
          if (Cursor > *UnitEnd)
            return false;
          continue;
        }

        // Every form that reaches here is a fixed-width section offset.
        const uint8_t ValueSize = sectionOffsetByteSize(Form, Params);
        if (ValueSize > *UnitEnd - Cursor)
          return false;
        const uint64_t ValuePos = Cursor;
        const uint64_t Value = readOffset(InfoData, &Cursor, ValueSize);
        std::optional<uint64_t> Shifted =
            shiftedOffset(Value, Delta, ValueSize);
        if (!Shifted ||
            !writeOffset(Info, ValuePos, *Shifted, ValueSize, LE))
          return false;
      }
    }
    Cursor = *UnitEnd;
  }
  return true;
}

// ===----------------------------------------------------------------------===
// .debug_aranges
// ===----------------------------------------------------------------------===

/// Each address-range set records the section-relative offset of the compile
/// unit it describes.  The address tuples themselves are relocated by the
/// object merger, but Mach-O stores this unit reference as a plain integer.
bool rebaseAranges(MutableArrayRef<char> Data, uint64_t InfoDelta, bool LE) {
  if (Data.empty())
    return true;

  const DataExtractor Extract(StringRef(Data.data(), Data.size()), LE,
                              /*AddressSize=*/8);
  uint64_t Cursor = 0;
  while (Cursor < Data.size()) {
    uint8_t OffsetSize = 4;
    std::optional<uint64_t> UnitLength =
        readInitialLength(Extract, Data.size(), &Cursor, OffsetSize);
    if (!UnitLength)
      return false;
    std::optional<uint64_t> End =
        checkedEnd(Cursor, *UnitLength, Data.size());
    const uint64_t HeaderSize =
        sizeof(uint16_t) + OffsetSize + 2 * sizeof(uint8_t);
    if (!End || *End - Cursor < HeaderSize)
      return false;

    if (Extract.getU16(&Cursor) != 2)
      return false;
    const uint64_t InfoOffsetPos = Cursor;
    const uint64_t InfoOffset = readOffset(Extract, &Cursor, OffsetSize);
    const uint8_t AddrSize = Extract.getU8(&Cursor);
    const uint8_t SegmentSize = Extract.getU8(&Cursor);
    if (AddrSize == 0 || AddrSize > sizeof(uint64_t) ||
        SegmentSize > sizeof(uint64_t))
      return false;

    if (InfoDelta != 0) {
      std::optional<uint64_t> Shifted =
          shiftedOffset(InfoOffset, InfoDelta, OffsetSize);
      if (!Shifted ||
          !writeOffset(Data, InfoOffsetPos, *Shifted, OffsetSize, LE))
        return false;
    }
    Cursor = *End;
  }
  return true;
}

// ===----------------------------------------------------------------------===
// .debug_pubnames / .debug_pubtypes
// ===----------------------------------------------------------------------===

/// The standard and GNU public-name tables begin each contribution with the
/// same header: version, compile-unit offset, compile-unit length.  Their
/// entry encodings differ, but every remaining offset is relative to that
/// compile unit and therefore stays unchanged.
bool rebasePubSection(MutableArrayRef<char> Data, uint64_t InfoDelta, bool LE) {
  if (Data.empty())
    return true;

  const DataExtractor Extract(StringRef(Data.data(), Data.size()), LE,
                              /*AddressSize=*/8);
  uint64_t Cursor = 0;
  while (Cursor < Data.size()) {
    uint8_t OffsetSize = 4;
    std::optional<uint64_t> UnitLength =
        readInitialLength(Extract, Data.size(), &Cursor, OffsetSize);
    if (!UnitLength)
      return false;
    std::optional<uint64_t> End =
        checkedEnd(Cursor, *UnitLength, Data.size());
    const uint64_t HeaderSize =
        sizeof(uint16_t) + 2 * uint64_t(OffsetSize);
    if (!End || *End - Cursor < HeaderSize ||
        Extract.getU16(&Cursor) != 2)
      return false;

    const uint64_t InfoOffsetPos = Cursor;
    const uint64_t InfoOffset = readOffset(Extract, &Cursor, OffsetSize);
    readOffset(Extract, &Cursor, OffsetSize); // compile-unit length
    if (InfoDelta != 0) {
      std::optional<uint64_t> Shifted =
          shiftedOffset(InfoOffset, InfoDelta, OffsetSize);
      if (!Shifted ||
          !writeOffset(Data, InfoOffsetPos, *Shifted, OffsetSize, LE))
        return false;
    }
    Cursor = *End;
  }
  return true;
}

// ===----------------------------------------------------------------------===
// .debug_frame
// ===----------------------------------------------------------------------===

/// An FDE's CIE pointer is an absolute section offset in .debug_frame (unlike
/// the backwards-relative pointer in .eh_frame), so concatenating partition
/// contributions must move it by this contribution's section start.
bool rebaseDebugFrame(MutableArrayRef<char> Data, uint64_t FrameDelta,
                      bool LE) {
  if (Data.empty())
    return true;

  const DataExtractor Extract(StringRef(Data.data(), Data.size()), LE,
                              /*AddressSize=*/8);
  uint64_t Cursor = 0;
  while (Cursor < Data.size()) {
    if (Data.size() - Cursor < sizeof(uint32_t))
      return false;
    const uint32_t First = Extract.getU32(&Cursor);
    if (First == 0)
      continue; // accepted zero-length terminator/padding

    uint8_t OffsetSize = 4;
    uint64_t Length = First;
    if (First == 0xffffffffu) {
      if (Data.size() - Cursor < sizeof(uint64_t))
        return false;
      OffsetSize = 8;
      Length = Extract.getU64(&Cursor);
    } else if (First >= 0xfffffff0u) {
      return false;
    }

    std::optional<uint64_t> End = checkedEnd(Cursor, Length, Data.size());
    if (!End || *End - Cursor < OffsetSize)
      return false;
    const uint64_t CIEPointerPos = Cursor;
    const uint64_t CIEPointer = readOffset(Extract, &Cursor, OffsetSize);
    const uint64_t CIEMarker =
        OffsetSize == 8 ? std::numeric_limits<uint64_t>::max()
                        : std::numeric_limits<uint32_t>::max();
    if (CIEPointer != CIEMarker && FrameDelta != 0) {
      std::optional<uint64_t> Shifted =
          shiftedOffset(CIEPointer, FrameDelta, OffsetSize);
      if (!Shifted ||
          !writeOffset(Data, CIEPointerPos, *Shifted, OffsetSize, LE))
        return false;
    }
    Cursor = *End;
  }
  return true;
}

// ===----------------------------------------------------------------------===
// .debug_str_offsets
// ===----------------------------------------------------------------------===

/// Every entry in this table is an offset into .debug_str.  Under DWARF 5 the
/// string forms in .debug_info are indices through here, so leaving the table
/// alone makes each partition after the first resolve its names against the
/// first partition's strings -- silently, with no malformed structure for a
/// debugger to complain about.
bool rebaseStrOffsets(MutableArrayRef<char> Data, uint64_t Delta, bool LE) {
  if (Data.empty())
    return true;

  const DataExtractor Extract(StringRef(Data.data(), Data.size()), LE,
                              /*AddressSize=*/8);
  uint64_t Cursor = 0;
  while (Cursor < Data.size()) {
    uint8_t OffsetSize = 4;
    std::optional<uint64_t> UnitLength =
        readInitialLength(Extract, Data.size(), &Cursor, OffsetSize);
    // The header counted by unit_length is version + padding.
    if (!UnitLength || *UnitLength < 4)
      return false;
    std::optional<uint64_t> End =
        checkedEnd(Cursor, *UnitLength, Data.size());
    if (!End)
      return false;

    const uint16_t Version = Extract.getU16(&Cursor);
    if (Version != 5)
      return false;
    Extract.getU16(&Cursor); // padding

    if ((*End - Cursor) % OffsetSize != 0)
      return false;
    const uint64_t Count = (*End - Cursor) / OffsetSize;
    if (!shiftOffsetArray(Data, Extract, &Cursor, Count, Delta, OffsetSize,
                          *End, LE))
      return false;
    Cursor = *End;
  }
  return true;
}

// ===----------------------------------------------------------------------===
// .debug_line
// ===----------------------------------------------------------------------===

/// A DWARF 5 line-table header names its directories and files through
/// .debug_line_str (or .debug_str) rather than inline, so those offsets move
/// with the merge too.  Earlier versions spell the names inline and need
/// nothing here.
bool rebaseLineHeaders(MutableArrayRef<char> Data, const PartitionDwarf &Part,
                       bool LE) {
  if (Data.empty())
    return true;

  const DataExtractor Extract(StringRef(Data.data(), Data.size()), LE,
                              /*AddressSize=*/8);
  uint64_t Cursor = 0;
  while (Cursor < Data.size()) {
    uint8_t OffsetSize = 4;
    std::optional<uint64_t> UnitLength =
        readInitialLength(Extract, Data.size(), &Cursor, OffsetSize);
    if (!UnitLength)
      return false;
    std::optional<uint64_t> UnitEnd =
        checkedEnd(Cursor, *UnitLength, Data.size());
    if (!UnitEnd || *UnitEnd - Cursor < sizeof(uint16_t))
      return false;

    const uint16_t Version = Extract.getU16(&Cursor);
    if (Version < 2 || Version > 5)
      return false;
    if (Version >= 5) {
      if (*UnitEnd - Cursor < 2 + OffsetSize)
        return false;
      const uint8_t AddrSize = Extract.getU8(&Cursor);
      if (AddrSize == 0 || AddrSize > sizeof(uint64_t))
        return false;
      Extract.getU8(&Cursor); // segment_selector_size

      // header_length spans from just after itself to the first opcode, so it
      // bounds exactly the entry tables below.
      const uint64_t HeaderLength = readOffset(Extract, &Cursor, OffsetSize);
      std::optional<uint64_t> ProgramStart =
          checkedEnd(Cursor, HeaderLength, *UnitEnd);
      if (!ProgramStart || *ProgramStart - Cursor < 6)
        return false;

      Extract.getU8(&Cursor); // minimum_instruction_length
      Extract.getU8(&Cursor); // maximum_operations_per_instruction
      Extract.getU8(&Cursor); // default_is_stmt
      Extract.getU8(&Cursor); // line_base
      Extract.getU8(&Cursor); // line_range
      const uint8_t OpcodeBase = Extract.getU8(&Cursor);
      if (OpcodeBase == 0 || OpcodeBase - 1 > *ProgramStart - Cursor)
        return false;
      Cursor += OpcodeBase - 1; // standard_opcode_lengths

      const dwarf::FormParams Params{
          Version, AddrSize, OffsetSize == 8 ? dwarf::DWARF64 : dwarf::DWARF32};

      // Directories then files: same shape, a format description followed by
      // that many entries.
      for (int Table = 0; Table != 2; ++Table) {
        if (Cursor >= *ProgramStart)
          return false;
        const uint8_t FormatCount = Extract.getU8(&Cursor);
        SmallVector<dwarf::Form, 4> Forms;
        Forms.reserve(FormatCount);
        for (uint8_t I = 0; I != FormatCount; ++I) {
          if (!readULEB128(Extract, &Cursor, *ProgramStart))
            return false;
          const std::optional<uint64_t> Form =
              readULEB128(Extract, &Cursor, *ProgramStart);
          if (!Form || *Form > std::numeric_limits<uint16_t>::max())
            return false;
          Forms.push_back(static_cast<dwarf::Form>(*Form));
        }
        if (Cursor >= *ProgramStart)
          return false;

        const std::optional<uint64_t> EntryCount =
            readULEB128(Extract, &Cursor, *ProgramStart);
        // Every entry occupies at least one byte, so a count exceeding what is
        // left of the header cannot be honest -- and rejecting it here is what
        // bounds the loop below when the format list is empty.
        if (!EntryCount || *EntryCount > *ProgramStart - Cursor)
          return false;
        if (*EntryCount != 0 && Forms.empty())
          return false;
        for (uint64_t E = 0; E != *EntryCount; ++E) {
          for (dwarf::Form DeclaredForm : Forms) {
            std::optional<dwarf::Form> ResolvedForm = resolveIndirectForm(
                Extract, &Cursor, *ProgramStart, DeclaredForm);
            if (!ResolvedForm)
              return false;
            const dwarf::Form Form = *ResolvedForm;
            const DwarfSection Target = sectionForLineEntryForm(Form);
            const uint64_t Delta =
                Target == DwarfSection::Count ? 0 : Part.start(Target);
            if (Delta == 0) {
              if (!DWARFFormValue::skipValue(Form, Extract, &Cursor, Params))
                return false;
              if (Cursor > *ProgramStart)
                return false;
              continue;
            }
            const uint64_t ValuePos = Cursor;
            if (OffsetSize > *ProgramStart - Cursor)
              return false;
            const uint64_t Value = readOffset(Extract, &Cursor, OffsetSize);
            std::optional<uint64_t> Shifted =
                shiftedOffset(Value, Delta, OffsetSize);
            if (!Shifted ||
                !writeOffset(Data, ValuePos, *Shifted, OffsetSize, LE))
              return false;
          }
        }
        if (Cursor > *ProgramStart)
          return false;
      }
    }
    Cursor = *UnitEnd;
  }
  return true;
}

// ===----------------------------------------------------------------------===
// .debug_macro
// ===----------------------------------------------------------------------===

bool skipCString(ArrayRef<char> Data, uint64_t *Cursor, uint64_t Limit) {
  if (*Cursor >= Limit || Limit > Data.size())
    return false;
  while (*Cursor < Limit)
    if (Data[(*Cursor)++] == '\0')
      return true;
  return false;
}

bool shiftOneOffset(MutableArrayRef<char> Data, const DataExtractor &Extract,
                    uint64_t *Cursor, uint8_t OffsetSize, uint64_t Limit,
                    uint64_t Delta, bool LE) {
  if ((OffsetSize != 4 && OffsetSize != 8) || *Cursor > Limit ||
      OffsetSize > Limit - *Cursor)
    return false;
  const uint64_t Pos = *Cursor;
  const uint64_t Value = readOffset(Extract, Cursor, OffsetSize);
  if (Delta == 0)
    return true;
  std::optional<uint64_t> Shifted = shiftedOffset(Value, Delta, OffsetSize);
  return Shifted && writeOffset(Data, Pos, *Shifted, OffsetSize, LE);
}

/// Rebase DWARF 5 macro-unit references.  Unlike most DWARF tables, a macro
/// unit has no initial-length field: its zero opcode is the only terminator.
/// A unit can point into `.debug_line`, `.debug_str`, and another contribution
/// in `.debug_macro`.  The latter is especially easy to miss:
/// DW_MACRO_import stores a plain section offset, so a later partition
/// otherwise imports an unrelated macro unit from partition zero.
bool rebaseDebugMacro(MutableArrayRef<char> Data, const PartitionDwarf &Part,
                      bool LE) {
  if (Data.empty())
    return true;

  constexpr uint8_t OffsetSizeFlag = 0x01;
  constexpr uint8_t DebugLineOffsetFlag = 0x02;
  constexpr uint8_t OpcodeOperandsTableFlag = 0x04;
  constexpr uint8_t KnownFlags =
      OffsetSizeFlag | DebugLineOffsetFlag | OpcodeOperandsTableFlag;

  const DataExtractor Extract(StringRef(Data.data(), Data.size()), LE,
                              /*AddressSize=*/8);
  uint64_t Cursor = 0;
  while (Cursor < Data.size()) {
    const uint64_t End = Data.size();
    if (End - Cursor < sizeof(uint16_t) + sizeof(uint8_t))
      return false;

    if (Extract.getU16(&Cursor) != 5)
      return false;
    const uint8_t Flags = Extract.getU8(&Cursor);
    if (Flags & ~KnownFlags)
      return false;
    const uint8_t OffsetSize = Flags & OffsetSizeFlag ? 8 : 4;

    if (Flags & DebugLineOffsetFlag)
      if (!shiftOneOffset(Data, Extract, &Cursor, OffsetSize, End,
                          Part.start(DwarfSection::Line), LE))
        return false;

    // The table describes vendor opcodes, but their forms do not identify what
    // a DW_FORM_sec_offset refers to.  Parse the table so the standard opcode
    // stream starts at the right byte; if a vendor opcode is encountered below
    // we reject it rather than guess and silently point it at the wrong
    // section.
    if (Flags & OpcodeOperandsTableFlag) {
      if (Cursor >= End)
        return false;
      const uint8_t OpcodeCount = Extract.getU8(&Cursor);
      for (uint8_t I = 0; I != OpcodeCount; ++I) {
        if (Cursor >= End)
          return false;
        Extract.getU8(&Cursor); // opcode
        std::optional<uint64_t> OperandCount =
            readULEB128(Extract, &Cursor, End);
        if (!OperandCount || *OperandCount > End - Cursor)
          return false;
        for (uint64_t O = 0; O != *OperandCount; ++O)
          if (!readULEB128(Extract, &Cursor, End))
            return false;
      }
    }

    bool Terminated = false;
    while (Cursor < End) {
      const uint8_t Opcode = Extract.getU8(&Cursor);
      if (Opcode == 0) {
        Terminated = true;
        break;
      }

      auto ReadLine = [&]() {
        return readULEB128(Extract, &Cursor, End).has_value();
      };
      switch (Opcode) {
      case dwarf::DW_MACRO_define:
      case dwarf::DW_MACRO_undef:
        if (!ReadLine() || !skipCString(Data, &Cursor, End))
          return false;
        break;
      case dwarf::DW_MACRO_start_file:
        if (!ReadLine() || !ReadLine())
          return false;
        break;
      case dwarf::DW_MACRO_end_file:
        break;
      case dwarf::DW_MACRO_define_strp:
      case dwarf::DW_MACRO_undef_strp:
        if (!ReadLine() ||
            !shiftOneOffset(Data, Extract, &Cursor, OffsetSize, End,
                            Part.start(DwarfSection::Str), LE))
          return false;
        break;
      case dwarf::DW_MACRO_import:
        if (!shiftOneOffset(Data, Extract, &Cursor, OffsetSize, End,
                            Part.start(DwarfSection::Macro), LE))
          return false;
        break;
      case dwarf::DW_MACRO_define_sup:
      case dwarf::DW_MACRO_undef_sup:
        if (!ReadLine() ||
            !shiftOneOffset(Data, Extract, &Cursor, OffsetSize, End,
                            /*Delta=*/0, LE))
          return false;
        break;
      case dwarf::DW_MACRO_import_sup:
        if (!shiftOneOffset(Data, Extract, &Cursor, OffsetSize, End,
                            /*Delta=*/0, LE))
          return false;
        break;
      case dwarf::DW_MACRO_define_strx:
      case dwarf::DW_MACRO_undef_strx:
        if (!ReadLine() || !ReadLine())
          return false;
        break;
      default:
        return false;
      }
    }
    if (!Terminated)
      return false;
  }
  return true;
}

// ===----------------------------------------------------------------------===
// .debug_names
// ===----------------------------------------------------------------------===

/// The DWARF 5 accelerator table.  Its name index blocks concatenate cleanly,
/// but each block records where "its" compile units and strings live, so
/// without this a debugger looking a name up lands in partition 0's unit and
/// reads partition 0's string.  Entry offsets are relative to the block's own
/// entry pool and DW_IDX_die_offset is relative to its unit, so neither moves.
bool rebaseDebugNames(MutableArrayRef<char> Data, const PartitionDwarf &Part,
                      bool LE) {
  if (Data.empty())
    return true;
  const uint64_t InfoDelta = Part.start(DwarfSection::Info);
  const uint64_t StrDelta = Part.start(DwarfSection::Str);

  const DataExtractor Extract(StringRef(Data.data(), Data.size()), LE,
                              /*AddressSize=*/8);
  uint64_t Cursor = 0;
  while (Cursor < Data.size()) {
    uint8_t OffsetSize = 4;
    std::optional<uint64_t> UnitLength =
        readInitialLength(Extract, Data.size(), &Cursor, OffsetSize);
    if (!UnitLength)
      return false;
    std::optional<uint64_t> End =
        checkedEnd(Cursor, *UnitLength, Data.size());
    constexpr uint64_t FixedHeaderSize =
        2 * sizeof(uint16_t) + 7 * sizeof(uint32_t);
    if (!End || *End - Cursor < FixedHeaderSize)
      return false;

    const uint16_t Version = Extract.getU16(&Cursor);
    if (Version != 5)
      return false;
    Extract.getU16(&Cursor); // padding
    const uint32_t CompUnitCount = Extract.getU32(&Cursor);
    const uint32_t LocalTypeUnitCount = Extract.getU32(&Cursor);
    const uint32_t ForeignTypeUnitCount = Extract.getU32(&Cursor);
    const uint32_t BucketCount = Extract.getU32(&Cursor);
    const uint32_t NameCount = Extract.getU32(&Cursor);
    Extract.getU32(&Cursor); // abbrev_table_size
    const uint32_t AugmentationSize = Extract.getU32(&Cursor);
    if (AugmentationSize > *End - Cursor)
      return false;
    Cursor += AugmentationSize;

    // Compile-unit and local type-unit offsets both index .debug_info.
    if (!shiftOffsetArray(Data, Extract, &Cursor, CompUnitCount, InfoDelta,
                          OffsetSize, *End, LE) ||
        !shiftOffsetArray(Data, Extract, &Cursor, LocalTypeUnitCount, InfoDelta,
                          OffsetSize, *End, LE))
      return false;
    // Foreign type units are 8-byte signatures, not offsets.
    if (ForeignTypeUnitCount > (*End - Cursor) / 8)
      return false;
    Cursor += uint64_t(ForeignTypeUnitCount) * 8;

    // Buckets index the name table; hashes are present only when bucketed.
    const uint64_t FixedWords =
        uint64_t(BucketCount) + (BucketCount ? uint64_t(NameCount) : 0);
    if (FixedWords > (*End - Cursor) / 4)
      return false;
    Cursor += FixedWords * 4;

    if (!shiftOffsetArray(Data, Extract, &Cursor, NameCount, StrDelta,
                          OffsetSize, *End, LE))
      return false;
    Cursor = *End;
  }
  return true;
}

// ===----------------------------------------------------------------------===
// DWARF package indexes
// ===----------------------------------------------------------------------===

struct PackageColumn {
  DWARFSectionKind Kind;
  DwarfSection Section;
};

constexpr PackageColumn PackageColumns[] = {
    {DW_SECT_INFO, DwarfSection::Info},
    {DW_SECT_ABBREV, DwarfSection::Abbrev},
    {DW_SECT_LINE, DwarfSection::Line},
    {DW_SECT_LOCLISTS, DwarfSection::LocLists},
    {DW_SECT_STR_OFFSETS, DwarfSection::StrOffsets},
    {DW_SECT_MACRO, DwarfSection::Macro},
    {DW_SECT_RNGLISTS, DwarfSection::RngLists},
};

struct PackageContribution {
  uint32_t Offset = 0;
  uint32_t Length = 0;
};

struct PackageRow {
  uint64_t Signature = 0;
  std::array<PackageContribution, DW_SECT_RNGLISTS> Contributions{};
};

size_t contributionIndex(DWARFSectionKind Kind) {
  const uint32_t Serialized = serializeSectionKind(Kind, /*IndexVersion=*/5);
  assert(Serialized >= DW_SECT_INFO && Serialized <= DW_SECT_RNGLISTS);
  return Serialized - DW_SECT_INFO;
}

bool getPackageContribution(const PartitionDwarf &Part, DwarfSection Section,
                            PackageContribution &Contribution) {
  const uint64_t Offset = Part.start(Section);
  const uint64_t Length = Part.size(Section);
  if (Length == 0) {
    Contribution = {};
    return true;
  }

  constexpr uint64_t Max = std::numeric_limits<uint32_t>::max();
  if (Offset > Max || Length > Max || Length > Max - Offset)
    return false;
  Contribution = {static_cast<uint32_t>(Offset), static_cast<uint32_t>(Length)};
  return true;
}

bool fillPackageContributions(const PartitionDwarf &Part, PackageRow &Row) {
  for (const PackageColumn &Column : PackageColumns)
    if (!getPackageContribution(
            Part, Column.Section,
            Row.Contributions[contributionIndex(Column.Kind)]))
      return false;
  return true;
}

bool collectPackageRows(ArrayRef<char> Info, const PartitionDwarf &Part,
                        bool LE, SmallVectorImpl<PackageRow> &CompileRows,
                        SmallVectorImpl<PackageRow> &TypeRows) {
  if (Info.empty() || Part.size(DwarfSection::Abbrev) == 0)
    return false;

  const DataExtractor Extract(StringRef(Info.data(), Info.size()), LE,
                              /*AddressSize=*/8);
  uint64_t Cursor = 0;
  while (Cursor < Info.size()) {
    const uint64_t UnitStart = Cursor;
    uint8_t OffsetSize = 4;
    std::optional<uint64_t> UnitLength =
        readInitialLength(Extract, Info.size(), &Cursor, OffsetSize);
    if (!UnitLength)
      return false;
    std::optional<uint64_t> UnitEnd =
        checkedEnd(Cursor, *UnitLength, Info.size());
    const uint64_t FixedHeaderSize =
        sizeof(uint16_t) + 2 * sizeof(uint8_t) + OffsetSize + sizeof(uint64_t);
    if (!UnitEnd || *UnitEnd - Cursor < FixedHeaderSize)
      return false;

    if (Extract.getU16(&Cursor) != 5)
      return false;
    const uint8_t UnitType = Extract.getU8(&Cursor);
    const uint8_t AddressSize = Extract.getU8(&Cursor);
    if (AddressSize == 0 || AddressSize > sizeof(uint64_t))
      return false;

    // DWP consumers require this field to be zero and obtain the merged
    // abbreviation contribution from the package index.
    if (readOffset(Extract, &Cursor, OffsetSize) != 0)
      return false;
    const uint64_t Signature = Extract.getU64(&Cursor);

    SmallVectorImpl<PackageRow> *Rows = nullptr;
    if (UnitType == dwarf::DW_UT_split_compile) {
      Rows = &CompileRows;
    } else if (UnitType == dwarf::DW_UT_split_type) {
      if (OffsetSize > *UnitEnd - Cursor)
        return false;
      readOffset(Extract, &Cursor, OffsetSize); // type DIE offset
      Rows = &TypeRows;
    } else {
      return false;
    }
    if (Cursor >= *UnitEnd)
      return false;

    PackageRow Row;
    Row.Signature = Signature;
    if (!fillPackageContributions(Part, Row))
      return false;

    const uint64_t MergedOffset = Part.start(DwarfSection::Info) + UnitStart;
    const uint64_t TotalLength = *UnitEnd - UnitStart;
    constexpr uint64_t Max = std::numeric_limits<uint32_t>::max();
    if (MergedOffset > Max || TotalLength > Max ||
        TotalLength > Max - MergedOffset)
      return false;
    Row.Contributions[contributionIndex(DW_SECT_INFO)] = {
        static_cast<uint32_t>(MergedOffset),
        static_cast<uint32_t>(TotalLength)};
    Rows->push_back(std::move(Row));
    Cursor = *UnitEnd;
  }
  return true;
}

void appendUnsigned(SmallVectorImpl<char> &Out, uint64_t Value,
                    unsigned ByteSize, bool LE) {
  for (unsigned I = 0; I != ByteSize; ++I) {
    const unsigned Shift = LE ? I * 8 : (ByteSize - 1 - I) * 8;
    Out.push_back(static_cast<char>((Value >> Shift) & 0xff));
  }
}

bool buildPackageIndex(ArrayRef<PackageRow> Rows, bool LE,
                       SmallVectorImpl<char> &Out) {
  Out.clear();
  if (Rows.empty())
    return true;

  SmallVector<const PackageColumn *, 8> ActiveColumns;
  for (const PackageColumn &Column : PackageColumns) {
    const size_t Index = contributionIndex(Column.Kind);
    if (llvm::any_of(Rows, [Index](const PackageRow &Row) {
          return Row.Contributions[Index].Length != 0;
        }))
      ActiveColumns.push_back(&Column);
  }
  if (ActiveColumns.empty() || ActiveColumns.front()->Kind != DW_SECT_INFO ||
      llvm::none_of(ActiveColumns, [](const PackageColumn *Column) {
        return Column->Kind == DW_SECT_ABBREV;
      }))
    return false;

  if (Rows.size() > std::numeric_limits<uint32_t>::max() / 3)
    return false;
  const uint64_t BucketCount = NextPowerOf2(3 * Rows.size() / 2);
  if (BucketCount == 0 || BucketCount > std::numeric_limits<uint32_t>::max())
    return false;

  SmallVector<uint32_t, 16> Buckets(BucketCount, 0);
  const uint64_t Mask = BucketCount - 1;
  for (size_t I = 0; I != Rows.size(); ++I) {
    const uint64_t Signature = Rows[I].Signature;
    uint64_t Hash = Signature & Mask;
    const uint64_t Probe = ((Signature >> 32) & Mask) | 1;
    while (Buckets[Hash] != 0) {
      if (Rows[Buckets[Hash] - 1].Signature == Signature)
        return false;
      Hash = (Hash + Probe) & Mask;
    }
    Buckets[Hash] = static_cast<uint32_t>(I + 1);
  }

  appendUnsigned(Out, 5, 2, LE); // Version.
  appendUnsigned(Out, 0, 2, LE); // DWARF 5 header padding.
  appendUnsigned(Out, ActiveColumns.size(), 4, LE);
  appendUnsigned(Out, Rows.size(), 4, LE);
  appendUnsigned(Out, BucketCount, 4, LE);

  for (uint32_t Bucket : Buckets)
    appendUnsigned(Out, Bucket == 0 ? 0 : Rows[Bucket - 1].Signature, 8, LE);
  for (uint32_t Bucket : Buckets)
    appendUnsigned(Out, Bucket, 4, LE);
  for (const PackageColumn *Column : ActiveColumns)
    appendUnsigned(Out, serializeSectionKind(Column->Kind, 5), 4, LE);

  for (const PackageRow &Row : Rows)
    for (const PackageColumn *Column : ActiveColumns)
      appendUnsigned(Out,
                     Row.Contributions[contributionIndex(Column->Kind)].Offset,
                     4, LE);
  for (const PackageRow &Row : Rows)
    for (const PackageColumn *Column : ActiveColumns)
      appendUnsigned(Out,
                     Row.Contributions[contributionIndex(Column->Kind)].Length,
                     4, LE);
  return true;
}

} // namespace

DwarfSection classifyDwarfSection(StringRef Name) {
  // Normalise both spellings to the bare section name.
  if (!Name.consume_front("__debug_") && !Name.consume_front(".debug_"))
    return DwarfSection::Count;
  // Standalone split-debug objects use `.debug_info.dwo`,
  // `.debug_abbrev.dwo`, and so on. They carry the same DWARF payloads but no
  // relocations, which is exactly why they need this rebaser.
  Name.consume_back(".dwo");

  return StringSwitch<DwarfSection>(Name)
      .Case("info", DwarfSection::Info)
      .Case("types", DwarfSection::Types)
      .Case("abbrev", DwarfSection::Abbrev)
      .Case("aranges", DwarfSection::Aranges)
      .Cases("pubnames", "gnu_pubnames", "gnu_pubn", DwarfSection::PubNames)
      .Cases("pubtypes", "gnu_pubtypes", "gnu_pubt", DwarfSection::PubTypes)
      .Case("str", DwarfSection::Str)
      .Case("line_str", DwarfSection::LineStr)
      .Case("line", DwarfSection::Line)
      .Case("frame", DwarfSection::Frame)
      .Case("ranges", DwarfSection::Ranges)
      .Case("rnglists", DwarfSection::RngLists)
      .Case("loc", DwarfSection::Loc)
      .Case("loclists", DwarfSection::LocLists)
      // Mach-O caps section names at 16 characters, which truncates
      // "__debug_str_offsets" to "__debug_str_offs".
      .Cases("str_offsets", "str_offs", DwarfSection::StrOffsets)
      .Case("addr", DwarfSection::Addr)
      .Case("macinfo", DwarfSection::MacInfo)
      .Case("macro", DwarfSection::Macro)
      .Case("names", DwarfSection::Names)
      .Default(DwarfSection::Count);
}

void PartitionDwarf::record(StringRef SectionName, unsigned MergedSectionIndex,
                            uint64_t Offset, uint64_t Size) {
  record(classifyDwarfSection(SectionName), MergedSectionIndex, Offset, Size);
}

void PartitionDwarf::record(DwarfSection Section,
                            unsigned MergedSectionIndex, uint64_t Offset,
                            uint64_t Size) {
  if (Section == DwarfSection::Count)
    return;
  At[dwarfSectionIndex(Section)] = {MergedSectionIndex, Offset, Size};
}

bool PartitionDwarf::needsRebase() const {
  // Even a first contribution has to enter the parser when its compile units
  // have no abbreviation table.  Otherwise every offset is zero, this method
  // would skip it, and malformed DWARF would reach the output instead of
  // making parallel codegen fall back safely.
  if ((size(DwarfSection::Info) != 0 || size(DwarfSection::Types) != 0) &&
      size(DwarfSection::Abbrev) == 0)
    return true;

  // The first contributor to every section already sits where it was emitted,
  // so there is nothing to move.
  for (const Contribution &C : At)
    if (C.Start != 0)
      return true;
  return false;
}

bool finalizeDwarfPackage(
    ArrayRef<PartitionDwarf> Partitions,
    function_ref<MutableArrayRef<char>(unsigned)> SectionData, bool LE,
    DwarfPackageIndexes &Indexes) {
  Indexes = {};
  SmallVector<PackageRow, 8> CompileRows;
  SmallVector<PackageRow, 8> TypeRows;

  for (const PartitionDwarf &Part : Partitions) {
    if (Part.size(DwarfSection::Info) == 0) {
      for (size_t I = 0; I != DwarfSectionCount; ++I)
        if (Part.size(static_cast<DwarfSection>(I)) != 0)
          return false;
      continue;
    }

    DwarfSlices Slices;
    for (size_t I = 0; I != DwarfSectionCount; ++I) {
      const DwarfSection Section = static_cast<DwarfSection>(I);
      const unsigned MergedIndex = Part.sectionIndex(Section);
      if (MergedIndex == PartitionDwarf::NoSection)
        continue;
      MutableArrayRef<char> All = SectionData(MergedIndex);
      const uint64_t Start = Part.start(Section);
      const uint64_t Size = Part.size(Section);
      if (Start > All.size() || Size > All.size() - Start)
        return false;
      Slices[I] = All.slice(Start, Size);
    }

    // `.debug_str.dwo` is one global section in a package. Its offset table is
    // the only generated DWO contribution whose entries themselves must move;
    // abbreviation/range/location bases remain contribution-relative and are
    // supplied by the package indexes below.
    if (!rebaseStrOffsets(Slices[dwarfSectionIndex(DwarfSection::StrOffsets)],
                          Part.start(DwarfSection::Str), LE) ||
        !collectPackageRows(Slices[dwarfSectionIndex(DwarfSection::Info)], Part,
                            LE, CompileRows, TypeRows))
      return false;
  }

  if (CompileRows.empty() && TypeRows.empty())
    return false;
  return buildPackageIndex(CompileRows, LE, Indexes.CompileUnits) &&
         buildPackageIndex(TypeRows, LE, Indexes.TypeUnits);
}

bool rebasePartitionDwarf(const DwarfSlices &Slices, const PartitionDwarf &Part,
                          bool LE) {
  auto At = [&](DwarfSection S) -> MutableArrayRef<char> {
    return Slices[dwarfSectionIndex(S)];
  };

  // A unit cannot be read without its abbreviations, so a partition that
  // contributed one without the other is malformed and rebaseDebugInfo will
  // say so rather than quietly emit DWARF a debugger cannot read.
  return rebaseDebugInfo(At(DwarfSection::Info), At(DwarfSection::Abbrev), Part,
                         DwarfSection::Info, LE) &&
         rebaseDebugInfo(At(DwarfSection::Types), At(DwarfSection::Abbrev),
                         Part, DwarfSection::Types, LE) &&
         rebaseAranges(At(DwarfSection::Aranges),
                       Part.start(DwarfSection::Info), LE) &&
         rebasePubSection(At(DwarfSection::PubNames),
                          Part.start(DwarfSection::Info), LE) &&
         rebasePubSection(At(DwarfSection::PubTypes),
                          Part.start(DwarfSection::Info), LE) &&
         rebaseDebugFrame(At(DwarfSection::Frame),
                          Part.start(DwarfSection::Frame), LE) &&
         rebaseStrOffsets(At(DwarfSection::StrOffsets),
                          Part.start(DwarfSection::Str), LE) &&
         rebaseLineHeaders(At(DwarfSection::Line), Part, LE) &&
         rebaseDebugMacro(At(DwarfSection::Macro), Part, LE) &&
         rebaseDebugNames(At(DwarfSection::Names), Part, LE);
}

} // namespace neverc::merge
