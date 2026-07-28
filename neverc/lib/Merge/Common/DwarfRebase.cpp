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

/// Which section a line-table header entry's value points into, for the forms
/// a directory or file-name entry may legitimately use.  Everything else --
/// inline strings, indices, MD5 hashes -- is position independent.
DwarfSection sectionForLineEntryForm(dwarf::Form Form) {
  switch (Form) {
  case dwarf::DW_FORM_line_strp:
    return DwarfSection::LineStr;
  case dwarf::DW_FORM_strp:
  case dwarf::DW_FORM_strp_sup:
    return DwarfSection::Str;
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

/// Add \p Delta to each of \p Count consecutive offsets starting at \p Cursor,
/// advancing it past them.  Returns false unless they all fit below \p Limit,
/// which is the end of the unit being read -- bounding by the whole section
/// would let a malformed count walk into the next unit and rewrite it.
bool shiftOffsetArray(MutableArrayRef<char> Data, const DataExtractor &Extract,
                      uint64_t *Cursor, uint64_t Count, uint64_t Delta,
                      uint8_t OffsetSize, uint64_t Limit, bool LE) {
  if (*Cursor > Limit || Count > (Limit - *Cursor) / OffsetSize)
    return false;
  for (uint64_t I = 0; I != Count; ++I) {
    const uint64_t Pos = *Cursor;
    const uint64_t Value = readOffset(Extract, Cursor, OffsetSize);
    if (Delta != 0)
      writeOffset(Data, Pos, Value + Delta, OffsetSize, LE);
  }
  return true;
}

/// Read an initial-length field, setting \p OffsetSize to 4 or 8.  Returns the
/// length, or std::nullopt for the reserved values that are not a length.
std::optional<uint64_t> readInitialLength(const DataExtractor &Data,
                                          uint64_t *Cursor,
                                          uint8_t &OffsetSize) {
  const uint64_t First = Data.getU32(Cursor);
  if (First == 0xffffffffu) {
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
                     const PartitionDwarf &Part, bool LE) {
  if (Info.empty())
    return true;

  const DataExtractor AbbrevData(StringRef(Abbrev.data(), Abbrev.size()), LE,
                                 /*AddressSize=*/8);
  const DataExtractor InfoData(StringRef(Info.data(), Info.size()), LE,
                               /*AddressSize=*/8);

  uint64_t Cursor = 0;
  while (Cursor < Info.size()) {
    // Smallest possible unit header (DWARF32, version < 5).
    if (Cursor + 11 > Info.size())
      return false;

    uint8_t OffsetSize = 4;
    std::optional<uint64_t> UnitLength =
        readInitialLength(InfoData, &Cursor, OffsetSize);
    if (!UnitLength)
      return false;
    // A unit that runs past the contribution means we lost sync; rewriting
    // further would corrupt data that is currently merely mis-pointed.
    const uint64_t UnitEnd = Cursor + *UnitLength;
    if (UnitEnd > Info.size())
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

// ===----------------------------------------------------------------------===
// .debug_str_offsets
// ===----------------------------------------------------------------------===

/// Every entry in this table is an offset into .debug_str.  Under DWARF 5 the
/// string forms in .debug_info are indices through here, so leaving the table
/// alone makes each partition after the first resolve its names against the
/// first partition's strings -- silently, with no malformed structure for a
/// debugger to complain about.
bool rebaseStrOffsets(MutableArrayRef<char> Data, uint64_t Delta, bool LE) {
  if (Data.empty() || Delta == 0)
    return true;

  const DataExtractor Extract(StringRef(Data.data(), Data.size()), LE,
                              /*AddressSize=*/8);
  uint64_t Cursor = 0;
  while (Cursor < Data.size()) {
    if (Cursor + 8 > Data.size())
      return false;
    uint8_t OffsetSize = 4;
    std::optional<uint64_t> UnitLength =
        readInitialLength(Extract, &Cursor, OffsetSize);
    // The header counted by unit_length is version + padding.
    if (!UnitLength || *UnitLength < 4)
      return false;
    const uint64_t End = Cursor + *UnitLength;
    if (End > Data.size())
      return false;

    const uint16_t Version = Extract.getU16(&Cursor);
    if (Version < 5)
      return false;
    Extract.getU16(&Cursor); // padding

    const uint64_t Count = (End - Cursor) / OffsetSize;
    if (!shiftOffsetArray(Data, Extract, &Cursor, Count, Delta, OffsetSize, End,
                          LE))
      return false;
    Cursor = End;
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
    if (Cursor + 4 > Data.size())
      return false;
    uint8_t OffsetSize = 4;
    std::optional<uint64_t> UnitLength =
        readInitialLength(Extract, &Cursor, OffsetSize);
    if (!UnitLength)
      return false;
    const uint64_t UnitEnd = Cursor + *UnitLength;
    if (UnitEnd > Data.size())
      return false;

    const uint16_t Version = Extract.getU16(&Cursor);
    if (Version >= 5) {
      const uint8_t AddrSize = Extract.getU8(&Cursor);
      Extract.getU8(&Cursor); // segment_selector_size

      // header_length spans from just after itself to the first opcode, so it
      // bounds exactly the entry tables below.
      const uint64_t HeaderLength = readOffset(Extract, &Cursor, OffsetSize);
      const uint64_t ProgramStart = Cursor + HeaderLength;
      if (ProgramStart > UnitEnd)
        return false;

      Extract.getU8(&Cursor); // minimum_instruction_length
      Extract.getU8(&Cursor); // maximum_operations_per_instruction
      Extract.getU8(&Cursor); // default_is_stmt
      Extract.getU8(&Cursor); // line_base
      Extract.getU8(&Cursor); // line_range
      const uint8_t OpcodeBase = Extract.getU8(&Cursor);
      if (OpcodeBase == 0 || Cursor + (OpcodeBase - 1) > ProgramStart)
        return false;
      Cursor += OpcodeBase - 1; // standard_opcode_lengths

      const dwarf::FormParams Params{
          Version, AddrSize, OffsetSize == 8 ? dwarf::DWARF64 : dwarf::DWARF32};

      // Directories then files: same shape, a format description followed by
      // that many entries.
      for (int Table = 0; Table != 2; ++Table) {
        const uint8_t FormatCount = Extract.getU8(&Cursor);
        SmallVector<dwarf::Form, 4> Forms;
        Forms.reserve(FormatCount);
        for (uint8_t I = 0; I != FormatCount; ++I) {
          Extract.getULEB128(&Cursor); // content type code
          Forms.push_back(
              static_cast<dwarf::Form>(Extract.getULEB128(&Cursor)));
        }
        if (Cursor > ProgramStart)
          return false;

        const uint64_t EntryCount = Extract.getULEB128(&Cursor);
        // Every entry occupies at least one byte, so a count exceeding what is
        // left of the header cannot be honest -- and rejecting it here is what
        // bounds the loop below when the format list is empty.
        if (Cursor > ProgramStart || EntryCount > ProgramStart - Cursor)
          return false;
        for (uint64_t E = 0; E != EntryCount; ++E) {
          for (dwarf::Form Form : Forms) {
            const DwarfSection Target = sectionForLineEntryForm(Form);
            const uint64_t Delta =
                Target == DwarfSection::Count ? 0 : Part.start(Target);
            if (Delta == 0) {
              if (!DWARFFormValue::skipValue(Form, Extract, &Cursor, Params))
                return false;
              continue;
            }
            const uint64_t ValuePos = Cursor;
            const uint64_t Value = readOffset(Extract, &Cursor, OffsetSize);
            if (Cursor > ProgramStart)
              return false;
            writeOffset(Data, ValuePos, Value + Delta, OffsetSize, LE);
          }
        }
        if (Cursor > ProgramStart)
          return false;
      }
    }
    Cursor = UnitEnd;
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
  if (InfoDelta == 0 && StrDelta == 0)
    return true;

  const DataExtractor Extract(StringRef(Data.data(), Data.size()), LE,
                              /*AddressSize=*/8);
  uint64_t Cursor = 0;
  while (Cursor < Data.size()) {
    if (Cursor + 4 > Data.size())
      return false;
    uint8_t OffsetSize = 4;
    std::optional<uint64_t> UnitLength =
        readInitialLength(Extract, &Cursor, OffsetSize);
    if (!UnitLength)
      return false;
    const uint64_t End = Cursor + *UnitLength;
    if (End > Data.size())
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
    if (Cursor + AugmentationSize > End)
      return false;
    Cursor += AugmentationSize;

    // Compile-unit and local type-unit offsets both index .debug_info.
    if (!shiftOffsetArray(Data, Extract, &Cursor, CompUnitCount, InfoDelta,
                          OffsetSize, End, LE) ||
        !shiftOffsetArray(Data, Extract, &Cursor, LocalTypeUnitCount, InfoDelta,
                          OffsetSize, End, LE))
      return false;
    // Foreign type units are 8-byte signatures, not offsets.
    if (ForeignTypeUnitCount > (End - Cursor) / 8)
      return false;
    Cursor += uint64_t(ForeignTypeUnitCount) * 8;

    // Buckets index the name table; hashes are present only when bucketed.
    const uint64_t FixedWords =
        uint64_t(BucketCount) + (BucketCount ? uint64_t(NameCount) : 0);
    if (FixedWords > (End - Cursor) / 4)
      return false;
    Cursor += FixedWords * 4;

    if (!shiftOffsetArray(Data, Extract, &Cursor, NameCount, StrDelta,
                          OffsetSize, End, LE))
      return false;
    Cursor = End;
  }
  return true;
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
      .Case("names", DwarfSection::Names)
      .Default(DwarfSection::Count);
}

void PartitionDwarf::record(StringRef SectionName, unsigned MergedSectionIndex,
                            uint64_t Offset, uint64_t Size) {
  const DwarfSection S = classifyDwarfSection(SectionName);
  if (S == DwarfSection::Count)
    return;
  At[dwarfSectionIndex(S)] = {MergedSectionIndex, Offset, Size};
}

bool PartitionDwarf::needsRebase() const {
  // The first contributor to every section already sits where it was emitted,
  // so there is nothing to move.
  for (const Contribution &C : At)
    if (C.Start != 0)
      return true;
  return false;
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
                         LE) &&
         rebaseStrOffsets(At(DwarfSection::StrOffsets),
                          Part.start(DwarfSection::Str), LE) &&
         rebaseLineHeaders(At(DwarfSection::Line), Part, LE) &&
         rebaseDebugNames(At(DwarfSection::Names), Part, LE);
}

} // namespace neverc::merge
