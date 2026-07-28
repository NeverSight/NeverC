//===- DwarfRebase.cpp - Re-point DWARF offsets after an object merge -----===//

#include "DwarfRebase.h"

#include "llvm/ADT/StringSwitch.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DWARF/DWARFAbbreviationDeclaration.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugAbbrev.h"
#include "llvm/DebugInfo/DWARF/DWARFFormValue.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/Support/Error.h"

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
  case dwarf::DW_FORM_strp_sup:
    return DwarfSection::Str;
  case dwarf::DW_FORM_line_strp:
    return DwarfSection::LineStr;
  // A reference to a DIE in another unit is an offset into .debug_info
  // itself, so it moves with this partition's contribution.
  case dwarf::DW_FORM_ref_addr:
    return DwarfSection::Info;
  case dwarf::DW_FORM_sec_offset:
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
    return DwarfSection::Addr;
  case dwarf::DW_AT_rnglists_base:
    return DwarfSection::RngLists;
  case dwarf::DW_AT_loclists_base:
    return DwarfSection::LocLists;
  case dwarf::DW_AT_macro_info:
    return DwarfSection::MacInfo;
  case dwarf::DW_AT_macros:
    return DwarfSection::Macro;
  default:
    return DwarfSection::Count;
  }
}

uint64_t readOffset(const DataExtractor &Data, uint64_t *Cursor,
                    uint8_t ByteSize) {
  return ByteSize == 8 ? Data.getU64(Cursor) : Data.getU32(Cursor);
}

void writeOffset(MutableArrayRef<char> Buf, uint64_t Pos, uint64_t Value,
                 uint8_t ByteSize, bool LE) {
  auto *P = reinterpret_cast<uint8_t *>(Buf.data() + Pos);
  for (uint8_t I = 0; I != ByteSize; ++I) {
    unsigned Shift = LE ? 8 * I : 8 * (ByteSize - 1 - I);
    P[I] = static_cast<uint8_t>((Value >> Shift) & 0xff);
  }
}

} // namespace

DwarfSection classifyDwarfSection(StringRef Name) {
  // Normalise both spellings to the bare section name.
  if (!Name.consume_front("__debug_") && !Name.consume_front(".debug_"))
    return DwarfSection::Count;

  return StringSwitch<DwarfSection>(Name)
      .Case("info", DwarfSection::Info)
      .Case("abbrev", DwarfSection::Abbrev)
      .Case("str", DwarfSection::Str)
      .Case("line_str", DwarfSection::LineStr)
      .Case("line", DwarfSection::Line)
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
      .Default(DwarfSection::Count);
}

void PartitionDwarf::record(StringRef SectionName, unsigned MergedSectionIndex,
                            uint64_t Offset, uint64_t Size) {
  const DwarfSection S = classifyDwarfSection(SectionName);
  if (S == DwarfSection::Count)
    return;

  Starts[static_cast<size_t>(S)] = Offset;
  if (S == DwarfSection::Info) {
    InfoSectionIndex = MergedSectionIndex;
    InfoSize = Size;
  } else if (S == DwarfSection::Abbrev) {
    AbbrevSectionIndex = MergedSectionIndex;
    AbbrevSize = Size;
  }
}

bool PartitionDwarf::needsRebase() const {
  // No compile units means nothing holds an offset that could be stale.  A
  // partition that contributed units but no abbreviations is deliberately not
  // excluded here: it is malformed, and letting the rewrite run reports the
  // failure rather than quietly emitting DWARF a debugger cannot read.
  if (InfoSectionIndex == NoSection || InfoSize == 0)
    return false;
  // The first contributor to every section already sits where its units
  // expect, so there is nothing to move.
  for (uint64_t Start : Starts)
    if (Start != 0)
      return true;
  return false;
}

bool rebaseDebugInfo(MutableArrayRef<char> Info, ArrayRef<char> Abbrev,
                     const PartitionDwarf &Part, bool LE) {
  const DataExtractor AbbrevData(StringRef(Abbrev.data(), Abbrev.size()), LE,
                                 /*AddressSize=*/8);
  const DataExtractor InfoData(StringRef(Info.data(), Info.size()), LE,
                               /*AddressSize=*/8);

  uint64_t Cursor = 0;
  while (Cursor < Info.size()) {
    // Smallest possible unit header (DWARF32, version < 5).
    if (Cursor + 11 > Info.size())
      return false;

    uint64_t UnitLength = InfoData.getU32(&Cursor);
    uint8_t OffsetSize = 4;
    if (UnitLength == 0xffffffffu) {
      UnitLength = InfoData.getU64(&Cursor);
      OffsetSize = 8;
    }
    // A unit that runs past the contribution means we lost sync; rewriting
    // further would corrupt data that is currently merely mis-pointed.
    const uint64_t UnitEnd = Cursor + UnitLength;
    if (UnitLength == 0 || UnitEnd > Info.size())
      return false;

    const uint16_t Version = InfoData.getU16(&Cursor);
    uint8_t AddrSize;
    uint64_t AbbrOffPos;
    uint64_t AbbrOff;
    if (Version >= 5) {
      InfoData.getU8(&Cursor); // unit_type
      AddrSize = InfoData.getU8(&Cursor);
      AbbrOffPos = Cursor;
      AbbrOff = readOffset(InfoData, &Cursor, OffsetSize);
    } else {
      AbbrOffPos = Cursor;
      AbbrOff = readOffset(InfoData, &Cursor, OffsetSize);
      AddrSize = InfoData.getU8(&Cursor);
    }

    // The abbreviations are read at their pre-merge offset, so parse before
    // the header is rewritten.
    DWARFAbbreviationDeclarationSet Decls;
    uint64_t AbbrCursor = AbbrOff;
    if (Error E = Decls.extract(AbbrevData, &AbbrCursor)) {
      consumeError(std::move(E));
      return false;
    }
    writeOffset(Info, AbbrOffPos, AbbrOff + Part.start(DwarfSection::Abbrev),
                OffsetSize, LE);

    const dwarf::FormParams Params{
        Version, AddrSize, OffsetSize == 8 ? dwarf::DWARF64 : dwarf::DWARF32};

    while (Cursor < UnitEnd) {
      const uint64_t Code = InfoData.getULEB128(&Cursor);
      if (Code == 0) // end of a sibling chain
        continue;
      const DWARFAbbreviationDeclaration *Decl =
          Decls.getAbbreviationDeclaration(static_cast<uint32_t>(Code));
      if (!Decl)
        return false;

      for (const auto &Spec : Decl->attributes()) {
        if (Spec.isImplicitConst())
          continue; // the value lives in the abbreviation, not the data

        const DwarfSection Target =
            sectionForAttribute(Spec.Attr, Spec.Form, Version);
        const uint64_t Delta =
            Target == DwarfSection::Count ? 0 : Part.start(Target);
        if (Delta == 0) {
          if (!DWARFFormValue::skipValue(Spec.Form, InfoData, &Cursor, Params))
            return false;
          continue;
        }

        // Every form that reaches here is a fixed-width section offset.
        const uint64_t ValuePos = Cursor;
        const uint64_t Value = readOffset(InfoData, &Cursor, OffsetSize);
        if (Cursor > UnitEnd)
          return false;
        writeOffset(Info, ValuePos, Value + Delta, OffsetSize, LE);
      }
    }
    Cursor = UnitEnd;
  }
  return true;
}

} // namespace neverc::merge
