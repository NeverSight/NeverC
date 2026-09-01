#include "BuiltinLLVMObjectWriter.h"
#include "BuiltinELFTableCanonicalizer.h"
#include "neverc/Merge/Merger.h"
#include "neverc/Plugin/Host/AssemblySymbolName.h"
#include "neverc/Plugin/Host/BuiltinLLVMAsmParser.h"
#include "neverc/Plugin/Host/BuiltinObjectExtension.h"
#include "neverc/Plugin/Host/BuiltinTargetProvider.h"
#include "neverc/Plugin/Host/NativeRelocationFacts.h"
#include "neverc/Plugin/Host/ObjectSectionRole.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

namespace neverc::plugin {
namespace {

// Each failure site has a stable number, and the index of the entity being
// processed -- when there is one -- occupies the low decimal digits. The two
// have to be encoded separately: adding an index straight onto a base made
// distinct failures indistinguishable as soon as a graph held a handful of
// sections or a few hundred symbols, which is every real translation unit.
enum DetailSite : uint64_t {
  DetailRequestArguments = 100,
  DetailRequestABI,
  DetailComdatFirst,
  DetailComdatNext,
  DetailComdatInfo,
  DetailComdatName,
  DetailComdatNameUnsupported,
  DetailComdatExtensionBytes,
  DetailComdatExtensionUnsupported,
  DetailSectionFirst,
  DetailSectionNext,
  DetailSectionInfo,
  DetailSectionDataView,
  DetailSectionName,
  DetailSectionNameUnsupported,
  DetailSectionBytes,
  DetailSectionExtensionUnsupported,
  DetailSymbolFirst,
  DetailSymbolNext,
  DetailSymbolInfo,
  DetailSymbolName,
  DetailSymbolNameUnsupported,
  DetailSymbolExtensionBytes,
  DetailSymbolExtensionUnsupported,
  DetailRelocationFirst,
  DetailRelocationNext,
  DetailRelocationInfo,
  DetailRelocationExtensionBytes,
  DetailRelocationExtensionUnsupported,
  DetailRelocationWidthUnsupported,
  DetailRelocationTargetSymbolMissing,
  DetailRelocationTargetSectionMissing,
  DetailRelocationTargetKindUnsupported,
  DetailRelocationKindUnsupported,
  DetailRelocationImageRelativeWidth,
  DetailRelocationSectionRelativeWidth,
  DetailRelocationNameMissing,
  DetailRelocationNotExpressible,
  DetailSymbolOutsideSection,
  DetailRelocationWidthInvalid,
  DetailRelocationOutsideSection,
  DetailRelocationOverlapsSymbol,
  DetailSymbolsUnplaced,
  DetailMachOExecutableRelocation,
  DetailComdatMachOUnsupported,
  DetailComdatELFSelectionUnsupported,
  DetailComdatCOFFSelectionUnsupported,
  DetailComdatMissing,
  DetailComdatParentMissing,
  DetailTargetRouteMissing,
  DetailTargetLookupFailed,
  DetailAssemblyParseFailed,
  DetailBinaryWriteFailed,
  DetailCommonAlignmentUnsupported,
  DetailComdatClaimedTwice,
  DetailSymbolNameRepeated,
  DetailSymbolNamePrivate,
  DetailSectionNotDistinguishable,
  DetailSectionCountUnsupported,
  DetailSectionAlignmentUnsupported,
  DetailSectionNameTooLong,
  DetailCommonSizeUnsupported,
  DetailSectionNameImpliesFlags,
  DetailELFCanonicalizationFailed,
  DetailRelocationTargetValueUnsupported,
  DetailRelocationTargetValueOutsideSection,
  DetailRelocationTargetValueOverflow,
};

constexpr uint64_t DetailIndexScale = 1000000;

uint64_t detailAt(DetailSite Site, size_t Index) {
  return static_cast<uint64_t>(Site) * DetailIndexScale +
         std::min<uint64_t>(Index, DetailIndexScale - 1);
}

uint64_t detail(DetailSite Site) { return detailAt(Site, 0); }

struct SectionRecord {
  NevercObjectSectionHandle Handle{};
  std::string Name;
  NevercObjectSectionKind Kind = 0;
  NevercObjectSectionFlags Flags = 0;
  uint64_t Alignment = 1;
  std::vector<uint8_t> Data;
  uint64_t ZeroFillSize = 0;
  NevercObjectComdatHandle Comdat{};
  NevercObjectFormatID ExtensionOwner{};
  uint32_t ExtensionVersion = 0;
  std::vector<uint8_t> Extension;
};

struct SymbolRecord {
  NevercObjectSymbolHandle Handle{};
  std::string Name;
  NevercObjectSymbolBinding Binding = 0;
  NevercObjectSymbolVisibility Visibility = 0;
  NevercObjectSymbolType Type = 0;
  NevercObjectSymbolDefinition Definition = 0;
  NevercObjectSectionHandle Section{};
  uint64_t Value = 0;
  uint64_t Size = 0;
  uint64_t Alignment = 1;
  NevercObjectComdatHandle Comdat{};
  NevercObjectSymbolFlags Flags = 0;
  NevercObjectFormatID ExtensionOwner{};
  uint32_t ExtensionVersion = 0;
  std::vector<uint8_t> Extension;
};

struct ComdatRecord {
  NevercObjectComdatHandle Handle{};
  std::string Name;
  NevercObjectComdatSelection Selection = 0;
  NevercObjectComdatHandle AssociatedComdat{};
  NevercObjectFormatID ExtensionOwner{};
  uint32_t ExtensionVersion = 0;
  std::vector<uint8_t> Extension;
};

struct RelocationRecord {
  NevercObjectRelocationHandle Handle{};
  NevercObjectSectionHandle Section{};
  uint64_t Offset = 0;
  NevercObjectRelocationKind Kind = 0;
  NevercObjectRelocationTargetKind TargetKind = 0;
  uint32_t Width = 0;
  bool IsPCRelative = false;
  bool IsSigned = false;
  int64_t Addend = 0;
  NevercObjectSymbolHandle TargetSymbol{};
  NevercObjectSectionHandle TargetSection{};
  uint64_t TargetValue = 0;
  uint32_t TargetExtensionKind = 0;
  NevercObjectFormatID ExtensionOwner{};
  uint32_t ExtensionVersion = 0;
  std::vector<uint8_t> Extension;
};

NevercStatus writerStatus(NevercStatusCode Code, uint64_t Detail) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  Status.Detail = Detail;
  return Status;
}

bool sameID(NevercInterfaceID Left, NevercInterfaceID Right) {
  return Left.High == Right.High && Left.Low == Right.Low;
}

bool copyString(NevercStringView View, std::string &Output) {
  if (View.Length > std::numeric_limits<size_t>::max() ||
      (!View.Data && View.Length != 0))
    return false;
  Output.assign(View.Data ? View.Data : "", static_cast<size_t>(View.Length));
  return true;
}

bool copyBytes(NevercByteView View, std::vector<uint8_t> &Output) {
  if (View.Length > std::numeric_limits<size_t>::max() ||
      (!View.Data && View.Length != 0))
    return false;
  if (View.Length != 0)
    Output.assign(View.Data, View.Data + static_cast<size_t>(View.Length));
  return true;
}

bool supportedExtension(NevercObjectFormatID FormatID,
                        NevercObjectFormatID Owner, uint32_t Version,
                        ArrayRef<uint8_t> Bytes, StringRef Tag,
                        uint32_t MaxVersion) {
  if (Bytes.empty())
    return Owner.High == 0 && Owner.Low == 0 && Version == 0;
  return sameID(Owner, FormatID) && Version >= 1 && Version <= MaxVersion &&
         builtinext::hasTag(Bytes, Tag);
}

uint64_t nativeEntrySize(const SectionRecord &Section) {
  if (Section.ExtensionVersion < 2)
    return 0;
  return builtinext::field(Section.Extension, builtinext::SectionEntrySize)
      .value_or(0);
}

// The relocation's native type number, as recorded by the reader. Absent for a
// graph the reader did not produce, which is why every caller has to have an
// answer for "no native type" rather than assuming one.
std::optional<uint64_t> nativeRelocationType(const RelocationRecord &R) {
  return builtinext::nativeRelocationType(R.Extension);
}

NevercStatus collectComdats(const NevercObjectWriteRequest &Request,
                            std::vector<ComdatRecord> &Comdats) {
  NevercObjectComdatHandle Handle{};
  NevercStatus Status = Request.Object->GetFirstComdat(
      Request.Object->Context, Request.Task, Request.Graph, &Handle);
  if (Status.Code == NEVERC_STATUS_NOT_FOUND)
    return neverc_status_ok();
  if (!neverc_status_is_ok(Status)) {
    if (Status.Detail == 0)
      Status.Detail = detail(DetailComdatFirst);
    return Status;
  }
  for (;;) {
    const size_t Index = Comdats.size();
    NevercObjectComdatInfo Info{};
    Info.Header = {sizeof(Info), NEVERC_OBJECT_API_MAJOR,
                   NEVERC_OBJECT_API_MINOR, 0};
    Status = Request.Object->GetComdatInfo(Request.Object->Context,
                                           Request.Task, Handle, &Info);
    if (!neverc_status_is_ok(Status)) {
      if (Status.Detail == 0)
        Status.Detail = detailAt(DetailComdatInfo, Index);
      return Status;
    }
    ComdatRecord Record;
    Record.Handle = Handle;
    if (!copyString(Info.Name, Record.Name))
      return writerStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                          detailAt(DetailComdatName, Index));
    if (!expressibleName(Record.Name))
      return writerStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE,
                          detailAt(DetailComdatNameUnsupported, Index));
    Record.Selection = Info.Selection;
    Record.AssociatedComdat = Info.AssociatedComdat;
    Record.ExtensionOwner = Info.ExtensionOwner;
    Record.ExtensionVersion = Info.ExtensionVersion;
    if (!copyBytes(Info.Extension, Record.Extension))
      return writerStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                          detailAt(DetailComdatExtensionBytes, Index));
    if (!supportedExtension(Request.FormatID, Record.ExtensionOwner,
                            Record.ExtensionVersion, Record.Extension,
                            builtinext::ComdatTag, builtinext::ComdatVersion))
      return writerStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE,
                          detailAt(DetailComdatExtensionUnsupported, Index));
    Comdats.push_back(std::move(Record));

    NevercObjectComdatHandle Next{};
    Status = Request.Object->GetNextComdat(Request.Object->Context,
                                           Request.Task, Handle, &Next);
    if (Status.Code == NEVERC_STATUS_NOT_FOUND)
      return neverc_status_ok();
    if (!neverc_status_is_ok(Status)) {
      if (Status.Detail == 0)
        Status.Detail = detailAt(DetailComdatNext, Index);
      return Status;
    }
    Handle = Next;
  }
}

NevercStatus collectSections(const NevercObjectWriteRequest &Request,
                             std::vector<SectionRecord> &Sections) {
  NevercObjectSectionHandle Handle{};
  NevercStatus Status = Request.Object->GetFirstSection(
      Request.Object->Context, Request.Task, Request.Graph, &Handle);
  if (Status.Code == NEVERC_STATUS_NOT_FOUND)
    return neverc_status_ok();
  if (!neverc_status_is_ok(Status)) {
    if (Status.Detail == 0)
      Status.Detail = detail(DetailSectionFirst);
    return Status;
  }
  for (;;) {
    const size_t Index = Sections.size();
    NevercObjectSectionInfo Info{};
    Info.Header = {sizeof(Info), NEVERC_OBJECT_API_MAJOR,
                   NEVERC_OBJECT_API_MINOR, 0};
    Status = Request.Object->GetSectionInfo(Request.Object->Context,
                                            Request.Task, Handle, &Info);
    if (!neverc_status_is_ok(Status)) {
      if (Status.Detail == 0)
        Status.Detail = detailAt(DetailSectionInfo, Index);
      return Status;
    }
    if (Info.Data.Length > std::numeric_limits<size_t>::max() ||
        (!Info.Data.Data && Info.Data.Length != 0))
      return writerStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                          detailAt(DetailSectionDataView, Index));
    SectionRecord Record;
    Record.Handle = Handle;
    if (!copyString(Info.Name, Record.Name))
      return writerStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                          detailAt(DetailSectionName, Index));
    if (!expressibleName(Record.Name))
      return writerStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE,
                          detailAt(DetailSectionNameUnsupported, Index));
    Record.Kind = Info.Kind;
    Record.Flags = Info.Flags;
    Record.Alignment = Info.Alignment;
    if (!copyBytes(Info.Data, Record.Data) ||
        !copyBytes(Info.Extension, Record.Extension))
      return writerStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                          detailAt(DetailSectionBytes, Index));
    Record.ZeroFillSize = Info.ZeroFillSize;
    Record.Comdat = Info.Comdat;
    Record.ExtensionOwner = Info.ExtensionOwner;
    Record.ExtensionVersion = Info.ExtensionVersion;
    if (!supportedExtension(Request.FormatID, Record.ExtensionOwner,
                            Record.ExtensionVersion, Record.Extension,
                            builtinext::SectionTag, builtinext::SectionVersion))
      return writerStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE,
                          detailAt(DetailSectionExtensionUnsupported, Index));
    Sections.push_back(std::move(Record));

    NevercObjectSectionHandle Next{};
    Status = Request.Object->GetNextSection(Request.Object->Context,
                                            Request.Task, Handle, &Next);
    if (Status.Code == NEVERC_STATUS_NOT_FOUND)
      return neverc_status_ok();
    if (!neverc_status_is_ok(Status)) {
      if (Status.Detail == 0)
        Status.Detail = detailAt(DetailSectionNext, Index);
      return Status;
    }
    Handle = Next;
  }
}

NevercStatus collectSymbols(const NevercObjectWriteRequest &Request,
                            std::vector<SymbolRecord> &Symbols) {
  NevercObjectSymbolHandle Handle{};
  NevercStatus Status = Request.Object->GetFirstSymbol(
      Request.Object->Context, Request.Task, Request.Graph, &Handle);
  if (Status.Code == NEVERC_STATUS_NOT_FOUND)
    return neverc_status_ok();
  if (!neverc_status_is_ok(Status)) {
    if (Status.Detail == 0)
      Status.Detail = detail(DetailSymbolFirst);
    return Status;
  }
  for (;;) {
    const size_t Index = Symbols.size();
    NevercObjectSymbolInfo Info{};
    Info.Header = {sizeof(Info), NEVERC_OBJECT_API_MAJOR,
                   NEVERC_OBJECT_API_MINOR, 0};
    Status = Request.Object->GetSymbolInfo(Request.Object->Context,
                                           Request.Task, Handle, &Info);
    if (!neverc_status_is_ok(Status)) {
      if (Status.Detail == 0)
        Status.Detail = detailAt(DetailSymbolInfo, Index);
      return Status;
    }
    SymbolRecord Record;
    Record.Handle = Handle;
    if (!copyString(Info.Name, Record.Name))
      return writerStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                          detailAt(DetailSymbolName, Index));
    if (!expressibleName(Record.Name))
      return writerStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE,
                          detailAt(DetailSymbolNameUnsupported, Index));
    Record.Binding = Info.Binding;
    Record.Visibility = Info.Visibility;
    Record.Type = Info.Type;
    Record.Definition = Info.Definition;
    Record.Section = Info.Section;
    Record.Value = Info.Value;
    Record.Size = Info.Size;
    Record.Alignment = Info.Alignment;
    Record.Comdat = Info.Comdat;
    Record.Flags = Info.Flags;
    Record.ExtensionOwner = Info.ExtensionOwner;
    Record.ExtensionVersion = Info.ExtensionVersion;
    if (!copyBytes(Info.Extension, Record.Extension))
      return writerStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                          detailAt(DetailSymbolExtensionBytes, Index));
    if (!supportedExtension(Request.FormatID, Record.ExtensionOwner,
                            Record.ExtensionVersion, Record.Extension,
                            builtinext::SymbolTag, builtinext::SymbolVersion))
      return writerStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE,
                          detailAt(DetailSymbolExtensionUnsupported, Index));
    Symbols.push_back(std::move(Record));

    NevercObjectSymbolHandle Next{};
    Status = Request.Object->GetNextSymbol(Request.Object->Context,
                                           Request.Task, Handle, &Next);
    if (Status.Code == NEVERC_STATUS_NOT_FOUND)
      return neverc_status_ok();
    if (!neverc_status_is_ok(Status)) {
      if (Status.Detail == 0)
        Status.Detail = detailAt(DetailSymbolNext, Index);
      return Status;
    }
    Handle = Next;
  }
}

NevercStatus collectRelocations(const NevercObjectWriteRequest &Request,
                                std::vector<RelocationRecord> &Relocations) {
  NevercObjectRelocationHandle Handle{};
  NevercStatus Status = Request.Object->GetFirstRelocation(
      Request.Object->Context, Request.Task, Request.Graph, &Handle);
  if (Status.Code == NEVERC_STATUS_NOT_FOUND)
    return neverc_status_ok();
  if (!neverc_status_is_ok(Status)) {
    if (Status.Detail == 0)
      Status.Detail = detail(DetailRelocationFirst);
    return Status;
  }
  for (;;) {
    const size_t Index = Relocations.size();
    NevercObjectRelocationInfo Info{};
    Info.Header = {sizeof(Info), NEVERC_OBJECT_API_MAJOR,
                   NEVERC_OBJECT_API_MINOR, 0};
    Status = Request.Object->GetRelocationInfo(Request.Object->Context,
                                               Request.Task, Handle, &Info);
    if (!neverc_status_is_ok(Status)) {
      if (Status.Detail == 0)
        Status.Detail = detailAt(DetailRelocationInfo, Index);
      return Status;
    }
    RelocationRecord Record;
    Record.Handle = Handle;
    Record.Section = Info.Section;
    Record.Offset = Info.Offset;
    Record.Kind = Info.Kind;
    Record.TargetKind = Info.TargetKind;
    Record.Width = Info.Width;
    Record.IsPCRelative = Info.IsPCRelative != NEVERC_FALSE;
    Record.IsSigned = Info.IsSigned != NEVERC_FALSE;
    Record.Addend = Info.Addend;
    Record.TargetSymbol = Info.TargetSymbol;
    Record.TargetSection = Info.TargetSection;
    Record.TargetValue = Info.TargetValue;
    Record.TargetExtensionKind = Info.TargetExtensionKind;
    Record.ExtensionOwner = Info.ExtensionOwner;
    Record.ExtensionVersion = Info.ExtensionVersion;
    if (!copyBytes(Info.Extension, Record.Extension))
      return writerStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                          detailAt(DetailRelocationExtensionBytes, Index));
    if (!supportedExtension(Request.FormatID, Record.ExtensionOwner,
                            Record.ExtensionVersion, Record.Extension,
                            builtinext::RelocationTag,
                            builtinext::RelocationVersion))
      return writerStatus(
          NEVERC_STATUS_CAPABILITY_UNAVAILABLE,
          detailAt(DetailRelocationExtensionUnsupported, Index));
    Relocations.push_back(std::move(Record));

    NevercObjectRelocationHandle Next{};
    Status = Request.Object->GetNextRelocation(Request.Object->Context,
                                               Request.Task, Handle, &Next);
    if (Status.Code == NEVERC_STATUS_NOT_FOUND)
      return neverc_status_ok();
    if (!neverc_status_is_ok(Status)) {
      if (Status.Detail == 0)
        Status.Detail = detailAt(DetailRelocationNext, Index);
      return Status;
    }
    Handle = Next;
  }
}

void emitBytes(raw_ostream &OS, ArrayRef<uint8_t> Bytes) {
  static constexpr char Hex[] = "0123456789abcdef";
  for (size_t Offset = 0; Offset != Bytes.size();) {
    OS << "\t.byte\t";
    const size_t End = std::min(Bytes.size(), Offset + 16);
    for (size_t Index = Offset; Index != End; ++Index) {
      if (Index != Offset)
        OS << ',';
      const uint8_t Byte = Bytes[Index];
      OS << "0x" << Hex[Byte >> 4] << Hex[Byte & 15];
    }
    OS << '\n';
    Offset = End;
  }
}

StringRef coffComdatSelection(NevercObjectComdatSelection Selection) {
  switch (Selection) {
  case NEVERC_OBJECT_COMDAT_ANY:
    return "discard";
  case NEVERC_OBJECT_COMDAT_EXACT_MATCH:
    return "same_contents";
  case NEVERC_OBJECT_COMDAT_SAME_SIZE:
    return "same_size";
  case NEVERC_OBJECT_COMDAT_NO_DUPLICATES:
    return "one_only";
  case NEVERC_OBJECT_COMDAT_LARGEST:
    return "largest";
  case NEVERC_OBJECT_COMDAT_ASSOCIATIVE:
    return "associative";
  default:
    return {};
  }
}

// SHF_MERGE is only meaningful with an entry size, and the assembler rejects
// "M" without one. A section that lost its entry size on the way in is written
// back as a plain section rather than as an ill-formed mergeable one.
bool elfMergeFlags(const SectionRecord &Section, std::string &Flags,
                   uint64_t &EntrySize) {
  EntrySize = nativeEntrySize(Section);
  if ((Section.Flags & NEVERC_OBJECT_SECTION_MERGEABLE) == 0 || EntrySize == 0)
    return false;
  Flags.push_back('M');
  if ((Section.Flags & NEVERC_OBJECT_SECTION_STRINGS) != 0)
    Flags.push_back('S');
  return true;
}

// \p AssociatedName is the parent COMDAT's name, set only for an associative
// COFF COMDAT. The directive has to name the section it is tied to -- naming
// itself is what "associative with sectionless symbol" means to the assembler.
bool isZeroFillKind(NevercObjectSectionKind Kind) {
  return Kind == NEVERC_OBJECT_SECTION_KIND_ZERO_FILL ||
         Kind == NEVERC_OBJECT_SECTION_KIND_TLS_ZERO_FILL;
}

// COFF states what a section contains and how it is protected in one flag
// string, and the content letters are mutually exclusive: 'b' (uninitialised)
// alongside 'd' (initialised) is rejected outright as conflicting. Building the
// string in one place keeps that constraint in one place too.
//
// An uninitialised section is spelled 'b' alone. There is no letter for a
// read-only one -- 'r' would bring the conflicting 'd' meaning with it -- and
// the assembler leaves a section writable unless told otherwise, which is what
// every real .bss wants.
std::string coffSectionFlags(NevercObjectSectionFlags SectionFlags,
                             bool ZeroFill) {
  const bool Writable = (SectionFlags & NEVERC_OBJECT_SECTION_WRITABLE) != 0;
  if ((SectionFlags & NEVERC_OBJECT_SECTION_EXECUTABLE) != 0)
    return Writable ? "xw" : "xr";
  if (ZeroFill)
    return "b";
  return Writable ? "dw" : "dr";
}

// Mach-O names a section by segment and section both, and the graph carries
// only the second, so the segment is inferred from what the section holds.
// Two things need the answer -- the directive and the identity below -- and
// they have to agree, or a pair of sections the assembler will merge is
// counted as distinct.
StringRef machOSegment(const SectionRecord &Section) {
  return (Section.Flags & NEVERC_OBJECT_SECTION_EXECUTABLE) != 0 ? "__TEXT"
                                                                 : "__DATA";
}

// What tells the section a directive is about to start apart from the ones the
// assembler already holds.
//
// Naming a section it has seen switches back to that one and appends to it
// instead of starting another, so two sections sharing this leave the object
// with a single section holding both -- silently, since the result assembles
// and reads back as a well-formed object. An ELF object really can hold
// several sections of one name, which is what -fno-unique-section-names
// produces for every function and every global, so this is an ordinary input
// and not a malformed one.
//
// What makes up the identity differs by format: ELF and COFF key a section on
// its name together with the group or COMDAT symbol it belongs to, while
// Mach-O names one by segment and section with nothing left over. A COFF
// COMDAT that is associative names its parent rather than a symbol of its own,
// so two of those under one parent are one section to the assembler even
// though the graph holds them apart.
std::string sectionIdentity(const SectionRecord &Section, const Triple &Target,
                            const ComdatRecord *Comdat,
                            StringRef AssociatedName) {
  // A name cannot hold a NUL -- expressibleName refuses one -- so it separates
  // the parts without any of them running into the next.
  const char Separator = '\0';
  if (Target.isOSBinFormatMachO())
    return machOSegment(Section).str() + Separator + Section.Name;
  if (!Comdat)
    return Section.Name + Separator;
  return Section.Name + Separator +
         (AssociatedName.empty() ? StringRef(Comdat->Name) : AssociatedName)
             .str();
}

// Every section is written as an explicit .section carrying the flags the graph
// holds. The shorthand directives -- .text, .data, .bss -- would be shorter,
// but they name a section *and* pick its attributes from the assembler's
// defaults, so a section whose flags differ from those defaults comes back
// changed. Spelling the flags out keeps the graph, not the assembler, as the
// source of truth.
//
// \p IdentityRepeated says an earlier section already claimed this one's
// identity. Only ELF can still keep them apart, by numbering the section with
// "unique"; the number is left off otherwise so that naming ".text" goes on
// reaching the section the assembler starts in rather than leaving that one
// behind empty beside a second ".text". No other format has a way to say it,
// so there the pair is refused rather than written out as one section.
NevercStatus emitSectionDirective(raw_ostream &OS, const SectionRecord &Section,
                                  size_t SectionIndex, const Triple &Target,
                                  const ComdatRecord *Comdat,
                                  StringRef AssociatedName,
                                  bool IdentityRepeated) {
  if (IdentityRepeated && !Target.isOSBinFormatELF())
    return writerStatus(
        NEVERC_STATUS_CAPABILITY_UNAVAILABLE,
        detailAt(DetailSectionNotDistinguishable, SectionIndex));
  const std::string Name = assemblyName(Section.Name);
  const bool ZeroFill = isZeroFillKind(Section.Kind);
  const bool Executable =
      (Section.Flags & NEVERC_OBJECT_SECTION_EXECUTABLE) != 0;
  // A graph the reader built states thread-locality twice, in the kind and in
  // the flags, so either alone is enough to answer. Reading only one of them
  // made the same section come out with SHF_TLS or without it depending on
  // which path below wrote it.
  const bool TLS = Section.Kind == NEVERC_OBJECT_SECTION_KIND_TLS_DATA ||
                   Section.Kind == NEVERC_OBJECT_SECTION_KIND_TLS_ZERO_FILL ||
                   (Section.Flags & NEVERC_OBJECT_SECTION_TLS) != 0;
  // The ELF assembler adds what a section's name implies to the flags the
  // directive states, so a name whose meaning goes beyond them produces a
  // section this did not describe -- and produces it quietly, since the object
  // assembles and reads back as a well-formed one.
  if (Target.isOSBinFormatELF() &&
      !elfNameAgreesWithFlags(
          Section.Name, (Section.Flags & NEVERC_OBJECT_SECTION_ALLOCATED) != 0,
          (Section.Flags & NEVERC_OBJECT_SECTION_WRITABLE) != 0, Executable,
          TLS))
    return writerStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE,
                        detailAt(DetailSectionNameImpliesFlags, SectionIndex));
  // Written only where it is needed, so that a graph without repeats produces
  // the same assembly it always did.
  const std::string Unique = IdentityRepeated
                                 ? ",unique," + std::to_string(SectionIndex)
                                 : std::string();

  if (Comdat) {
    if (Target.isOSBinFormatMachO())
      return writerStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE,
                          detail(DetailComdatMachOUnsupported));
    std::string Flags;
    if (Target.isOSBinFormatELF()) {
      if (Comdat->Selection != NEVERC_OBJECT_COMDAT_ANY)
        return writerStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE,
                            detail(DetailComdatELFSelectionUnsupported));
      if ((Section.Flags & NEVERC_OBJECT_SECTION_ALLOCATED) != 0)
        Flags.push_back('a');
      if ((Section.Flags & NEVERC_OBJECT_SECTION_WRITABLE) != 0)
        Flags.push_back('w');
      if (Executable)
        Flags.push_back('x');
      uint64_t EntrySize = 0;
      const bool Mergeable = elfMergeFlags(Section, Flags, EntrySize);
      if (TLS)
        Flags.push_back('T');
      Flags.push_back('G');
      OS << "\t.section\t" << Name << ",\"" << Flags << "\",@"
         << (ZeroFill ? "nobits" : "progbits");
      if (Mergeable)
        OS << ',' << EntrySize;
      OS << ',' << assemblyName(Comdat->Name) << ",comdat" << Unique << '\n';
      return neverc_status_ok();
    }
    StringRef Selection = coffComdatSelection(Comdat->Selection);
    if (Selection.empty())
      return writerStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE,
                          detail(DetailComdatCOFFSelectionUnsupported));
    Flags = coffSectionFlags(Section.Flags, ZeroFill);
    OS << "\t.section\t" << Name << ",\"" << Flags << "\"," << Selection << ','
       << assemblyName(AssociatedName.empty() ? StringRef(Comdat->Name)
                                              : AssociatedName)
       << '\n';
    return neverc_status_ok();
  }

  // Mach-O keeps read/write/execute protection on the segment, so a section
  // states its nature through its type and attributes instead. A zero-fill
  // section in particular has to say so: without the type the assembler makes
  // it an ordinary section and the trailing .zero becomes real bytes in the
  // file.
  if (Target.isOSBinFormatMachO()) {
    // A Mach-O section header holds its segment and section names in two
    // sixteen-byte fields, so a longer one has nowhere to go. The assembler
    // says so rather than truncating, but it says it as a syntax error about
    // a line the caller never wrote.
    if (Section.Name.size() > 16)
      return writerStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE,
                          detailAt(DetailSectionNameTooLong, SectionIndex));
    OS << "\t.section\t" << machOSegment(Section) << ',' << Name;
    if (TLS)
      OS << (ZeroFill ? ",thread_local_zerofill" : ",thread_local_regular");
    else if (ZeroFill)
      OS << ",zerofill";
    else if (Executable)
      OS << ",regular,pure_instructions";
    OS << '\n';
    return neverc_status_ok();
  }

  if (Target.isOSBinFormatELF()) {
    std::string Flags;
    if ((Section.Flags & NEVERC_OBJECT_SECTION_ALLOCATED) != 0)
      Flags.push_back('a');
    if ((Section.Flags & NEVERC_OBJECT_SECTION_WRITABLE) != 0)
      Flags.push_back('w');
    if (Executable)
      Flags.push_back('x');
    uint64_t EntrySize = 0;
    const bool Mergeable = elfMergeFlags(Section, Flags, EntrySize);
    if (TLS)
      Flags.push_back('T');
    OS << "\t.section\t" << Name << ",\"" << Flags << "\",@"
       << (ZeroFill ? "nobits" : "progbits");
    if (Mergeable)
      OS << ',' << EntrySize;
    OS << Unique << '\n';
    return neverc_status_ok();
  }

  OS << "\t.section\t" << Name << ",\""
     << coffSectionFlags(Section.Flags, ZeroFill) << "\"\n";
  return neverc_status_ok();
}

// How a tentative definition is spelled, or nothing when the format cannot
// hold its alignment.
//
// ".comm" hands its symbol to the linker to be matched against one of the same
// name in another translation unit, so a tentative definition belonging to
// this file alone cannot be written that way -- written so, it becomes part of
// the object's interface and merges with whatever else claims the name. Each
// format says "local" differently: ELF has no ".lcomm" and marks the symbol
// with ".local" beside an ordinary ".comm", while Mach-O and COFF take
// ".lcomm" and place the storage in .bss.
//
// The alignment operand means different things in different places, and the
// directive carries nothing to say which was meant. ELF counts bytes while
// Mach-O and COFF read a log2 exponent, so an 8 written the ELF way reads as
// 2^8 on the others and a common symbol came back out of a rewrite wanting 256
// bytes instead of 8. COFF then reads the same operand on ".lcomm" as a byte
// count again -- the opposite of what its own ".comm" means by it. Rounding up
// to a power of two first is what leaves every spelling well-formed, since an
// alignment that is not one has no exponent to write and the byte-counting
// parsers refuse it too.
//
// COFF keeps a ".comm" alignment in a field that stops at 32 bytes, and the MC
// layer answers a larger one by killing the process rather than by failing the
// write, so that bound has to be checked before the assembler sees it. The
// ".lcomm" path lands in an ordinary section and carries no such limit.
struct CommonSpelling {
  StringRef Directive;
  // ELF states the binding on a line of its own; no other format has one.
  bool NeedsLocalDirective = false;
  uint64_t AlignmentOperand = 1;
};

std::optional<CommonSpelling> commonSpelling(uint64_t Alignment, bool Local,
                                             const Triple &Target) {
  // There is no power of two above the top bit to round up to, and PowerOf2Ceil
  // shifts by 64 rather than saying so.
  if (Alignment > (UINT64_C(1) << 63))
    return std::nullopt;
  const uint64_t Bytes = PowerOf2Ceil(std::max<uint64_t>(Alignment, 1));
  if (Target.isOSBinFormatELF())
    return CommonSpelling{".comm", Local, Bytes};
  if (Target.isOSBinFormatCOFF()) {
    if (Local)
      return CommonSpelling{".lcomm", false, Bytes};
    if (Target.isWindowsMSVCEnvironment() && Bytes > 32)
      return std::nullopt;
    return CommonSpelling{".comm", false, Log2_64(Bytes)};
  }
  return CommonSpelling{Local ? ".lcomm" : ".comm", false, Log2_64(Bytes)};
}

void emitSymbolAttributes(raw_ostream &OS, const SymbolRecord &Symbol,
                          const Triple &Target) {
  if (Symbol.Binding == NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL)
    OS << "\t.globl\t" << assemblyName(Symbol.Name) << '\n';
  else if (Symbol.Binding == NEVERC_OBJECT_SYMBOL_BINDING_WEAK) {
    if (Target.isOSBinFormatMachO())
      OS << (Symbol.Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED
                 ? "\t.weak_definition\t"
                 : "\t.weak_reference\t")
         << assemblyName(Symbol.Name) << '\n';
    else
      OS << "\t.weak\t" << assemblyName(Symbol.Name) << '\n';
  }
  if (Symbol.Visibility == NEVERC_OBJECT_SYMBOL_VISIBILITY_HIDDEN) {
    if (Target.isOSBinFormatMachO())
      OS << "\t.private_extern\t" << assemblyName(Symbol.Name) << '\n';
    else if (Target.isOSBinFormatELF())
      OS << "\t.hidden\t" << assemblyName(Symbol.Name) << '\n';
  }
  if (Target.isOSBinFormatELF() &&
      Symbol.Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED) {
    if (Symbol.Type == NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION)
      OS << "\t.type\t" << assemblyName(Symbol.Name) << ",@function\n";
    else if (Symbol.Type == NEVERC_OBJECT_SYMBOL_TYPE_OBJECT ||
             Symbol.Type == NEVERC_OBJECT_SYMBOL_TYPE_TLS)
      OS << "\t.type\t" << assemblyName(Symbol.Name) << ",@object\n";
  }
}

// Handles are looked up once per relocation and once per section, so a linear
// scan makes the writer quadratic in the size of the graph. A translation unit
// with ten thousand symbols and as many relocations is ordinary under LTO.
using HandleKey = std::pair<uint64_t, uint64_t>;

HandleKey handleKey(NevercHandle Handle) {
  return {Handle.Owner, Handle.Value};
}

template <typename Record>
DenseMap<HandleKey, const Record *> indexByHandle(ArrayRef<Record> Records) {
  DenseMap<HandleKey, const Record *> Index;
  Index.reserve(Records.size());
  for (const Record &Value : Records)
    Index.try_emplace(handleKey(Value.Handle), &Value);
  return Index;
}

template <typename Record>
const Record *lookupHandle(const DenseMap<HandleKey, const Record *> &Index,
                           NevercHandle Handle) {
  const auto It = Index.find(handleKey(Handle));
  return It == Index.end() ? nullptr : It->second;
}

// Everything the emitters need to resolve a handle back to the record it names.
struct GraphIndex {
  DenseMap<HandleKey, const SectionRecord *> Sections;
  DenseMap<HandleKey, const SymbolRecord *> Symbols;
  DenseMap<HandleKey, const ComdatRecord *> Comdats;
  // Symbols and relocations grouped by the section that holds them, each list
  // already in the order the writer emits them.
  DenseMap<HandleKey, std::vector<const SymbolRecord *>> DefinedBySection;
  DenseMap<HandleKey, std::vector<const RelocationRecord *>>
      RelocationsBySection;
};

const ComdatRecord *findComdat(const GraphIndex &Index,
                               NevercObjectComdatHandle Handle) {
  if (neverc_handle_is_null(Handle))
    return nullptr;
  return lookupHandle(Index.Comdats, Handle);
}

std::string sectionLabel(size_t Index, const Triple &Target) {
  return (Target.isOSBinFormatMachO() ? "Lneverc_section_"
                                      : ".Lneverc_section_") +
         std::to_string(Index);
}

// Renders what a relocation points at -- "symbol", "section_label", with any
// addend folded in -- shared by the data-patching and .reloc paths.
NevercStatus relocationTargetExpression(const RelocationRecord &Relocation,
                                        ArrayRef<SectionRecord> Sections,
                                        const GraphIndex &Index,
                                        const Triple &Target,
                                        std::string &TargetExpression,
                                        bool IncludeRelocationAddend = true) {
  raw_string_ostream Expression(TargetExpression);
  int64_t EffectiveAddend = IncludeRelocationAddend ? Relocation.Addend : 0;
  if (Relocation.TargetKind == NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL) {
    const SymbolRecord *Symbol =
        lookupHandle(Index.Symbols, Relocation.TargetSymbol);
    if (!Symbol)
      return writerStatus(NEVERC_STATUS_VERIFICATION_FAILED,
                          detail(DetailRelocationTargetSymbolMissing));
    if (Relocation.TargetValue != 0)
      return writerStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE,
                          detail(DetailRelocationTargetValueUnsupported));
    Expression << assemblyName(Symbol->Name);
  } else if (Relocation.TargetKind == NEVERC_OBJECT_RELOCATION_TARGET_SECTION) {
    const SectionRecord *Section =
        lookupHandle(Index.Sections, Relocation.TargetSection);
    if (!Section)
      return writerStatus(NEVERC_STATUS_VERIFICATION_FAILED,
                          detail(DetailRelocationTargetSectionMissing));
    const uint64_t InitializedSize = Section->Data.size();
    if (Section->ZeroFillSize >
        std::numeric_limits<uint64_t>::max() - InitializedSize)
      return writerStatus(NEVERC_STATUS_VERIFICATION_FAILED,
                          detail(DetailRelocationTargetValueOutsideSection));
    const uint64_t SectionSize = InitializedSize + Section->ZeroFillSize;
    if (Relocation.TargetValue > SectionSize)
      return writerStatus(NEVERC_STATUS_VERIFICATION_FAILED,
                          detail(DetailRelocationTargetValueOutsideSection));
    if (Relocation.TargetValue >
            static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
        AddOverflow(static_cast<int64_t>(Relocation.TargetValue),
                    EffectiveAddend, EffectiveAddend))
      return writerStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE,
                          detail(DetailRelocationTargetValueOverflow));
    const size_t SectionIndex = static_cast<size_t>(Section - Sections.data());
    Expression << sectionLabel(SectionIndex, Target);
  } else {
    return writerStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE,
                        detail(DetailRelocationTargetKindUnsupported));
  }
  if (EffectiveAddend > 0)
    Expression << '+' << EffectiveAddend;
  else if (EffectiveAddend < 0)
    Expression << EffectiveAddend;
  Expression.flush();
  return neverc_status_ok();
}

StringRef nativeRelocationName(const RelocationRecord &Relocation) {
  if (!builtinext::hasTag(Relocation.Extension, builtinext::RelocationTag))
    return StringRef();
  return builtinext::relocationName(Relocation.Extension);
}

// ELF and reader-originated COFF relocations can be stated as a .reloc
// directive over bytes that stay exactly as they were read. Patching the
// covered bytes with a .long instead only works when the relocated field
// occupies whole bytes of its own; for a field that lives inside an instruction
// -- AArch64 CALL26, ADR_PREL_PG_HI21, the LO12 forms -- it overwrites the
// opcode along with the field and silently produces a different instruction.
// Naming the relocation also removes the need to infer its width or
// PC-relativeness, which no name-derived guess gets right for the GOT, PLT and
// TLS forms every real translation unit contains.
NevercStatus emitRelocDirective(raw_ostream &OS,
                                const RelocationRecord &Relocation,
                                size_t SectionIndex,
                                ArrayRef<SectionRecord> Sections,
                                const GraphIndex &Index, const Triple &Target,
                                bool IncludeRelocationAddend = true) {
  const StringRef Name = nativeRelocationName(Relocation);
  if (Name.empty())
    return writerStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE,
                        detail(DetailRelocationNameMissing));
  std::string TargetExpression;
  NevercStatus Status =
      relocationTargetExpression(Relocation, Sections, Index, Target,
                                 TargetExpression, IncludeRelocationAddend);
  if (!neverc_status_is_ok(Status))
    return Status;
  OS << "\t.reloc\t" << sectionLabel(SectionIndex, Target) << '+'
     << Relocation.Offset << ", " << Name << ", " << TargetExpression << '\n';
  return neverc_status_ok();
}

// A COFF relocation stores its addend in the bytes it covers.  The reader
// deliberately records that addend in the stable graph as well, so native
// .reloc passthrough is safe only while the two representations still agree.
// Instruction-field relocations keep opcode bits in those bytes and therefore
// carry no separately editable graph addend.
std::optional<int64_t> coffImplicitAddend(const SectionRecord &Section,
                                          const RelocationRecord &Relocation) {
  if (Relocation.Width == 0 || Relocation.Width > 64 ||
      (Relocation.Width % 8) != 0)
    return std::nullopt;
  const uint64_t Width = Relocation.Width / 8;
  if (Relocation.Offset > Section.Data.size() ||
      Width > Section.Data.size() - Relocation.Offset)
    return std::nullopt;
  uint64_t Raw = 0;
  for (uint32_t I = 0; I != Relocation.Width / 8; ++I)
    Raw |= static_cast<uint64_t>(Section.Data[Relocation.Offset + I])
           << (I * 8);
  if (Relocation.Width < 64) {
    const uint64_t SignBit = UINT64_C(1) << (Relocation.Width - 1);
    if ((Raw & SignBit) != 0)
      Raw |= ~((UINT64_C(1) << Relocation.Width) - 1);
  }
  return static_cast<int64_t>(Raw);
}

NevercStatus validateNativeCOFFRelocation(const RelocationRecord &Relocation,
                                          const SectionRecord &Section,
                                          const Triple &Target,
                                          bool &OutNative) {
  OutNative = false;
  const std::optional<uint64_t> Type = nativeRelocationType(Relocation);
  if (!Type)
    return neverc_status_ok();
  const std::optional<NativeRelocationFacts> Facts =
      nativeRelocationFacts(Target, *Type);
  if (!Facts || Facts->IsNoOp || nativeRelocationName(Relocation).empty())
    return writerStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE,
                        detail(DetailRelocationKindUnsupported));
  if (Facts->Width != Relocation.Width ||
      Facts->IsPCRelative != Relocation.IsPCRelative ||
      Facts->IsSigned != Relocation.IsSigned || Facts->Kind != Relocation.Kind)
    return writerStatus(NEVERC_STATUS_VERIFICATION_FAILED,
                        detail(DetailRelocationKindUnsupported));
  if (Facts->IsInstructionField) {
    if (Relocation.Addend != 0)
      return writerStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE,
                          detail(DetailRelocationNotExpressible));
  } else {
    const std::optional<int64_t> Addend =
        coffImplicitAddend(Section, Relocation);
    if (!Addend || *Addend != Relocation.Addend)
      return writerStatus(NEVERC_STATUS_VERIFICATION_FAILED,
                          detail(DetailRelocationNotExpressible));
  }
  OutNative = true;
  return neverc_status_ok();
}

// The data directives that can restate a relocation. Anything a relocation
// needs beyond these -- a field inside an instruction, an addressing base that
// is not the field itself -- has no faithful spelling here.
enum class DataDirective { Byte, Short, Long, Quad, RVA, SecRel32, SecIdx };

StringRef directiveText(DataDirective Directive) {
  switch (Directive) {
  case DataDirective::Byte:
    return ".byte";
  case DataDirective::Short:
    return ".short";
  case DataDirective::Long:
    return ".long";
  case DataDirective::Quad:
    return ".quad";
  case DataDirective::RVA:
    return ".rva";
  case DataDirective::SecRel32:
    return ".secrel32";
  case DataDirective::SecIdx:
    return ".secidx";
  }
  llvm_unreachable("unknown data directive");
}

// How many bits the directive lays down. The caller advances its cursor by the
// relocation's width, so a directive that writes a different number of bytes
// would shift everything after it -- silently, since the result still
// assembles.
uint32_t directiveWidth(DataDirective Directive) {
  switch (Directive) {
  case DataDirective::Byte:
    return 8;
  case DataDirective::Short:
  case DataDirective::SecIdx:
    return 16;
  case DataDirective::Long:
  case DataDirective::RVA:
  case DataDirective::SecRel32:
    return 32;
  case DataDirective::Quad:
    return 64;
  }
  llvm_unreachable("unknown data directive");
}

// Which relocation form the directive states.
NevercObjectRelocationKind directiveKind(DataDirective Directive) {
  switch (Directive) {
  case DataDirective::Byte:
  case DataDirective::Short:
  case DataDirective::Long:
  case DataDirective::Quad:
    return NEVERC_OBJECT_RELOCATION_ABSOLUTE;
  case DataDirective::RVA:
    return NEVERC_OBJECT_RELOCATION_IMAGE_RELATIVE;
  case DataDirective::SecRel32:
  case DataDirective::SecIdx:
    return NEVERC_OBJECT_RELOCATION_SECTION_RELATIVE;
  }
  llvm_unreachable("unknown data directive");
}

// Whether the directive can spell \p Addend at all. Only the plain data
// directives take an ordinary expression: ".secidx" wants a bare symbol,
// because a section index has no room for an offset and its parser rejects
// one; ".secrel32" reads a '+' and stops at anything else, and holds an
// unsigned 32-bit field; ".rva" holds a signed 32-bit one. An addend outside
// what the directive can carry reaches the assembler as a syntax error
// instead, which says nothing about what the writer could not express.
bool directiveCarriesAddend(DataDirective Directive, int64_t Addend) {
  switch (Directive) {
  case DataDirective::SecIdx:
    return Addend == 0;
  case DataDirective::SecRel32:
    return Addend >= 0 &&
           Addend <= static_cast<int64_t>(std::numeric_limits<uint32_t>::max());
  case DataDirective::RVA:
    return Addend >= std::numeric_limits<int32_t>::min() &&
           Addend <= std::numeric_limits<int32_t>::max();
  case DataDirective::Byte:
  case DataDirective::Short:
  case DataDirective::Long:
  case DataDirective::Quad:
    return true;
  }
  llvm_unreachable("unknown data directive");
}

std::optional<DataDirective> absoluteDirective(uint32_t Width) {
  switch (Width) {
  case 8:
    return DataDirective::Byte;
  case 16:
    return DataDirective::Short;
  case 32:
    return DataDirective::Long;
  case 64:
    return DataDirective::Quad;
  default:
    return std::nullopt;
  }
}

// COFF relocation types that name a whole data field. Every type left out
// patches a field inside an instruction (the ARM64 BRANCH/PAGE/LOW12 forms) or
// counts its displacement from the end of the instruction rather than from the
// field (the AMD64 REL32 forms), so no data directive reproduces it.
std::optional<DataDirective> coffNativeDirective(uint64_t Type,
                                                 const Triple &Target) {
  if (Target.getArch() == Triple::x86_64) {
    switch (Type) {
    case COFF::IMAGE_REL_AMD64_ADDR64:
      return DataDirective::Quad;
    case COFF::IMAGE_REL_AMD64_ADDR32:
      return DataDirective::Long;
    case COFF::IMAGE_REL_AMD64_ADDR32NB:
      return DataDirective::RVA;
    case COFF::IMAGE_REL_AMD64_SECREL:
      return DataDirective::SecRel32;
    case COFF::IMAGE_REL_AMD64_SECTION:
      return DataDirective::SecIdx;
    default:
      return std::nullopt;
    }
  }
  if (Target.getArch() == Triple::aarch64) {
    switch (Type) {
    case COFF::IMAGE_REL_ARM64_ADDR64:
      return DataDirective::Quad;
    case COFF::IMAGE_REL_ARM64_ADDR32:
      return DataDirective::Long;
    case COFF::IMAGE_REL_ARM64_ADDR32NB:
      return DataDirective::RVA;
    case COFF::IMAGE_REL_ARM64_SECREL:
      return DataDirective::SecRel32;
    case COFF::IMAGE_REL_ARM64_SECTION:
      return DataDirective::SecIdx;
    default:
      return std::nullopt;
    }
  }
  return std::nullopt;
}

// Only ELF can state a relocation without disturbing the bytes it covers. Every
// other format has to write a data directive whose value the assembler then
// relocates, and that is faithful only when the relocated field is a data field
// in its own right and is addressed from its own start.
//
// A field inside an instruction fails both tests, and not visibly: a .long over
// an AArch64 `bl` replaces the opcode along with the offset, and an x86
// PC-relative field counts from the end of the instruction, so `sym-.` lands
// four bytes off. Both still assemble. Refusing is the only way the caller
// learns the rewrite could not be done.
std::optional<DataDirective>
faithfulDirective(const RelocationRecord &Relocation, const Triple &Target) {
  if (Target.isOSBinFormatCOFF()) {
    std::optional<DataDirective> Directive;
    if (std::optional<uint64_t> Type = nativeRelocationType(Relocation)) {
      Directive = coffNativeDirective(*Type, Target);
      // The native type and the graph's own description are two statements
      // about the same field, and only a graph the reader built for this
      // architecture is guaranteed to keep them in step. A type number does
      // not say which architecture wrote it, and the object format ID stamped
      // on the extension names COFF without naming an architecture, so a graph
      // that crossed one reads as a different relocation than it is -- 2 is
      // AMD64's ADDR32 and ARM64's ADDR32NB, both 32 bits wide. Following the
      // type over the graph would advance the cursor by a width that misplaces
      // every later byte, or restate an image-relative reference as an
      // absolute one, and either way the object still assembles.
      if (Directive && (directiveWidth(*Directive) != Relocation.Width ||
                        directiveKind(*Directive) != Relocation.Kind ||
                        Relocation.IsPCRelative))
        return std::nullopt;
    } else {
      // Without a native type the graph's own classification is all there is,
      // and it only distinguishes forms that happen to have a directive.
      switch (Relocation.Kind) {
      case NEVERC_OBJECT_RELOCATION_ABSOLUTE:
        if (!Relocation.IsPCRelative)
          Directive = absoluteDirective(Relocation.Width);
        break;
      case NEVERC_OBJECT_RELOCATION_IMAGE_RELATIVE:
        if (Relocation.Width == 32)
          Directive = DataDirective::RVA;
        break;
      case NEVERC_OBJECT_RELOCATION_SECTION_RELATIVE:
        if (Relocation.Width == 32)
          Directive = DataDirective::SecRel32;
        else if (Relocation.Width == 16)
          Directive = DataDirective::SecIdx;
        break;
      default:
        break;
      }
    }
    if (Directive && !directiveCarriesAddend(*Directive, Relocation.Addend))
      return std::nullopt;
    return Directive;
  }
  // Mach-O has no .reloc at all, so the same rule applies -- and here the
  // native type is the only thing that states it. The guard that refuses
  // relocations in an executable section cannot: "executable" is read back
  // from S_ATTR_PURE_INSTRUCTIONS, which a section holding code need not
  // carry, and a section without it takes this path with its instructions
  // still in the bytes a data directive would overwrite.
  //
  // ARM64_RELOC_PAGEOFF12 is the one this is really about: it patches the
  // immediate of an `add` or a load, yet it is neither PC-relative nor GOT- or
  // TLS-bound, so it arrives looking exactly like a plain pointer.
  if (Target.isOSBinFormatMachO())
    if (std::optional<uint64_t> Type = nativeRelocationType(Relocation))
      if (nativeRelocationFieldIsWholeBytes(Target, *Type) != true)
        return std::nullopt;
  if (Relocation.Kind != NEVERC_OBJECT_RELOCATION_ABSOLUTE ||
      Relocation.IsPCRelative)
    return std::nullopt;
  return absoluteDirective(Relocation.Width);
}

NevercStatus emitRelocationValue(raw_ostream &OS,
                                 const RelocationRecord &Relocation,
                                 ArrayRef<SectionRecord> Sections,
                                 const GraphIndex &Index,
                                 const Triple &Target) {
  const std::optional<DataDirective> Directive =
      faithfulDirective(Relocation, Target);
  if (!Directive)
    return writerStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE,
                        detail(DetailRelocationNotExpressible));

  std::string TargetExpression;
  NevercStatus TargetStatus = relocationTargetExpression(
      Relocation, Sections, Index, Target, TargetExpression);
  if (!neverc_status_is_ok(TargetStatus))
    return TargetStatus;
  OS << '\t' << directiveText(*Directive) << '\t' << TargetExpression << '\n';
  return neverc_status_ok();
}

NevercStatus emitSectionContents(raw_ostream &OS, const SectionRecord &Section,
                                 size_t SectionIndex,
                                 ArrayRef<SectionRecord> Sections,
                                 const GraphIndex &Index,
                                 const Triple &Target) {
  OS << sectionLabel(SectionIndex, Target) << ":\n";

  ArrayRef<const SymbolRecord *> Defined;
  if (auto It = Index.DefinedBySection.find(handleKey(Section.Handle));
      It != Index.DefinedBySection.end())
    Defined = It->second;
  ArrayRef<const RelocationRecord *> SectionRelocations;
  if (auto It = Index.RelocationsBySection.find(handleKey(Section.Handle));
      It != Index.RelocationsBySection.end())
    SectionRelocations = It->second;

  // A section spans its stored bytes followed by its zero fill, and a symbol
  // may sit anywhere in either part. The fill therefore has to be split around
  // the symbols that fall inside it rather than emitted as one run at the end,
  // which would leave every .bss symbol past the first with nowhere to go.
  const uint64_t DataSize = Section.Data.size();
  const uint64_t TotalSize = DataSize + Section.ZeroFillSize;

  uint64_t Offset = 0;
  size_t SymbolIndex = 0;
  auto emitContentThrough = [&](uint64_t End) {
    if (End <= Offset)
      return;
    if (Offset < DataSize) {
      const uint64_t DataEnd = std::min(End, DataSize);
      emitBytes(OS, ArrayRef<uint8_t>(Section.Data)
                        .slice(static_cast<size_t>(Offset),
                               static_cast<size_t>(DataEnd - Offset)));
      Offset = DataEnd;
    }
    if (End > Offset) {
      OS << "\t.zero\t" << (End - Offset) << '\n';
      Offset = End;
    }
  };
  auto emitSymbolsThrough = [&](uint64_t End) {
    while (SymbolIndex != Defined.size() &&
           Defined[SymbolIndex]->Value <= End) {
      const SymbolRecord &Symbol = *Defined[SymbolIndex];
      if (Symbol.Value < Offset || Symbol.Value > TotalSize)
        return writerStatus(NEVERC_STATUS_VERIFICATION_FAILED,
                            detail(DetailSymbolOutsideSection));
      emitContentThrough(Symbol.Value);
      OS << assemblyName(Symbol.Name) << ":\n";
      ++SymbolIndex;
    }
    return neverc_status_ok();
  };

  // Mach-O has no .reloc directive, so a relocation there can only be written
  // by replacing the bytes it covers with a symbol expression. Inside
  // executable content that is not faithful: an AArch64 relocation patches a
  // field within a 32-bit instruction, so replacing those four bytes overwrites
  // the opcode as well and silently assembles a different instruction, and a
  // PC-relative reference to an undefined symbol cannot be spelled as a
  // subtraction at all. Refuse rather than miscompile.
  if (Target.isOSBinFormatMachO() && !SectionRelocations.empty() &&
      (Section.Flags & NEVERC_OBJECT_SECTION_EXECUTABLE) != 0)
    return writerStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE,
                        detail(DetailMachOExecutableRelocation));

  // A relocation patches stored bytes, so it has to lie inside them; zero fill
  // has nothing for it to apply to.
  for (const RelocationRecord *Relocation : SectionRelocations) {
    if (Relocation->Width == 0 || (Relocation->Width % 8) != 0)
      return writerStatus(NEVERC_STATUS_VERIFICATION_FAILED,
                          detail(DetailRelocationWidthInvalid));
    const uint64_t Width = Relocation->Width / 8;
    if (Relocation->Offset > DataSize || Width > DataSize - Relocation->Offset)
      return writerStatus(NEVERC_STATUS_VERIFICATION_FAILED,
                          detail(DetailRelocationOutsideSection));
  }

  if (Target.isOSBinFormatELF()) {
    NevercStatus Status = emitSymbolsThrough(TotalSize);
    if (!neverc_status_is_ok(Status))
      return Status;
    emitContentThrough(TotalSize);
    if (SymbolIndex != Defined.size())
      return writerStatus(NEVERC_STATUS_VERIFICATION_FAILED,
                          detail(DetailSymbolsUnplaced));
    for (const RelocationRecord *Relocation : SectionRelocations) {
      Status = emitRelocDirective(OS, *Relocation, SectionIndex, Sections,
                                  Index, Target);
      if (!neverc_status_is_ok(Status))
        return Status;
    }
    return neverc_status_ok();
  }

  for (const RelocationRecord *Relocation : SectionRelocations) {
    const uint64_t Width = Relocation->Width / 8;
    if (Relocation->Offset < Offset)
      return writerStatus(NEVERC_STATUS_VERIFICATION_FAILED,
                          detail(DetailRelocationOutsideSection));
    NevercStatus Status = emitSymbolsThrough(Relocation->Offset);
    if (!neverc_status_is_ok(Status))
      return Status;
    emitContentThrough(Relocation->Offset);
    if (SymbolIndex != Defined.size() &&
        Defined[SymbolIndex]->Value < Offset + Width)
      return writerStatus(NEVERC_STATUS_VERIFICATION_FAILED,
                          detail(DetailRelocationOverlapsSymbol));
    bool NativeCOFF = false;
    if (Target.isOSBinFormatCOFF()) {
      Status = validateNativeCOFFRelocation(*Relocation, Section, Target,
                                            NativeCOFF);
      if (!neverc_status_is_ok(Status))
        return Status;
    }
    if (NativeCOFF) {
      emitBytes(OS, ArrayRef<uint8_t>(Section.Data)
                        .slice(static_cast<size_t>(Offset),
                               static_cast<size_t>(Width)));
      Offset += Width;
      // The preserved COFF field already carries the implicit addend.
      Status = emitRelocDirective(OS, *Relocation, SectionIndex, Sections,
                                  Index, Target,
                                  /*IncludeRelocationAddend=*/false);
    } else {
      Status = emitRelocationValue(OS, *Relocation, Sections, Index, Target);
      Offset += Width;
    }
    if (!neverc_status_is_ok(Status))
      return Status;
  }

  NevercStatus Status = emitSymbolsThrough(TotalSize);
  if (!neverc_status_is_ok(Status))
    return Status;
  emitContentThrough(TotalSize);
  if (SymbolIndex != Defined.size())
    return writerStatus(NEVERC_STATUS_VERIFICATION_FAILED,
                        detail(DetailSymbolsUnplaced));
  return neverc_status_ok();
}

NevercStatus buildAssembly(const NevercObjectWriteRequest &Request,
                           const Triple &Target, std::string &Assembly) {
  std::vector<SectionRecord> Sections;
  std::vector<SymbolRecord> Symbols;
  std::vector<ComdatRecord> Comdats;
  std::vector<RelocationRecord> Relocations;
  NevercStatus Status = collectComdats(Request, Comdats);
  if (!neverc_status_is_ok(Status))
    return Status;
  Status = collectSections(Request, Sections);
  if (!neverc_status_is_ok(Status))
    return Status;
  Status = collectSymbols(Request, Symbols);
  if (!neverc_status_is_ok(Status))
    return Status;
  Status = collectRelocations(Request, Relocations);
  if (!neverc_status_is_ok(Status))
    return Status;

  // The Mach-O assembler mints its own "ltmp<n>" labels for section starts, so
  // a graph carrying symbols by those names would collide with them. Only the
  // exact minted shape is renamed -- matching the prefix alone would also catch
  // a program's own "ltmpBuffer". The replacement keeps a lowercase leading
  // letter on purpose: an uppercase "L" marks a symbol as assembler-local on
  // Mach-O, which would drop it from the symbol table and change what the
  // object exports.
  if (Target.isOSBinFormatMachO()) {
    size_t TemporaryIndex = 0;
    for (SymbolRecord &Symbol : Symbols) {
      if (Symbol.Binding != NEVERC_OBJECT_SYMBOL_BINDING_LOCAL)
        continue;
      StringRef Name(Symbol.Name);
      unsigned Minted = 0;
      if (Name.starts_with("ltmp") &&
          !Name.drop_front(4).getAsInteger(10, Minted))
        Symbol.Name = "lneverc_local_" + std::to_string(TemporaryIndex++);
    }
  }

  // Built after the Mach-O renaming above, and not touched afterwards: it holds
  // pointers into the vectors, which must not be resized from here on.
  GraphIndex Index;
  Index.Sections = indexByHandle<SectionRecord>(Sections);
  Index.Symbols = indexByHandle<SymbolRecord>(Symbols);
  Index.Comdats = indexByHandle<ComdatRecord>(Comdats);
  for (const SymbolRecord &Symbol : Symbols)
    if (Symbol.Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED &&
        !neverc_handle_is_null(Symbol.Section))
      Index.DefinedBySection[handleKey(Symbol.Section)].push_back(&Symbol);
  for (const RelocationRecord &Relocation : Relocations)
    Index.RelocationsBySection[handleKey(Relocation.Section)].push_back(
        &Relocation);
  for (auto &Entry : Index.DefinedBySection)
    llvm::sort(Entry.second,
               [](const SymbolRecord *Left, const SymbolRecord *Right) {
                 if (Left->Value != Right->Value)
                   return Left->Value < Right->Value;
                 return Left->Name < Right->Name;
               });
  for (auto &Entry : Index.RelocationsBySection)
    llvm::stable_sort(Entry.second, [](const RelocationRecord *Left,
                                       const RelocationRecord *Right) {
      return Left->Offset < Right->Offset;
    });

  // A symbol table holds one entry per name, so two symbols defining the same
  // one describe an object that cannot exist -- and MC does not answer such a
  // pair with an error. A repeated ".comm" reaches report_fatal_error, and a
  // name that is both a ".comm" or ".set" and a label reaches an assertion in
  // MCSymbol::setOffset, "Cannot set offset for a common/variable symbol".
  // Both end the host process, so the pair is refused here instead.
  //
  // Only a symbol that actually produces a definition claims its name: an
  // undefined one produces nothing, and neither does a defined one with no
  // section to hold its label.
  auto definesName = [](const SymbolRecord &Symbol) {
    switch (Symbol.Definition) {
    case NEVERC_OBJECT_SYMBOL_DEFINITION_COMMON:
    case NEVERC_OBJECT_SYMBOL_DEFINITION_ABSOLUTE:
      return true;
    case NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED:
      return !neverc_handle_is_null(Symbol.Section);
    default:
      return false;
    }
  };
  DenseSet<StringRef> DefinedNames;

  raw_string_ostream OS(Assembly);
  for (const SymbolRecord &Symbol : Symbols) {
    // A name the assembler reserves for its own scratch labels cannot be
    // written back: spelled locally it is dropped from the symbol table
    // without a word, and spelled globally the assembler refuses it. The
    // scratch labels this writer emits use the same prefix deliberately, and
    // a graph symbol wearing it would collide with them besides.
    if (isPrivateLabelName(Symbol.Name, Target))
      return writerStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE,
                          detail(DetailSymbolNamePrivate));
    if (definesName(Symbol) &&
        !DefinedNames.insert(StringRef(Symbol.Name)).second)
      return writerStatus(NEVERC_STATUS_VERIFICATION_FAILED,
                          detail(DetailSymbolNameRepeated));
    emitSymbolAttributes(OS, Symbol, Target);
    if (Symbol.Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_COMMON) {
      const std::optional<CommonSpelling> Spelling = commonSpelling(
          Symbol.Alignment,
          Symbol.Binding == NEVERC_OBJECT_SYMBOL_BINDING_LOCAL, Target);
      if (!Spelling)
        return writerStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE,
                            detail(DetailCommonAlignmentUnsupported));
      // The size operand is parsed as a signed value and refused when it comes
      // out negative, so the top half of the unsigned range has no spelling.
      if (Symbol.Size >
          static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
        return writerStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE,
                            detail(DetailCommonSizeUnsupported));
      if (Spelling->NeedsLocalDirective)
        OS << "\t.local\t" << assemblyName(Symbol.Name) << '\n';
      OS << '\t' << Spelling->Directive << '\t' << assemblyName(Symbol.Name)
         << ',' << Symbol.Size << ',' << Spelling->AlignmentOperand << '\n';
    } else if (Symbol.Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_ABSOLUTE)
      OS << "\t.set\t" << assemblyName(Symbol.Name) << ',' << Symbol.Value
         << '\n';
  }

  // A COFF COMDAT is keyed on a symbol that names its one section, and MC
  // answers a second section claiming the same one by calling
  // report_fatal_error -- which takes the host process down rather than
  // failing the write. An ELF group really does hold several sections, so this
  // is a COFF rule and not something the graph itself forbids. An associative
  // COMDAT names its parent instead of claiming a section, and MC exempts it
  // for that reason, so this does too.
  DenseSet<HandleKey> ClaimedComdats;

  // Two sections the assembler cannot tell apart become one, with the second
  // one's bytes appended to the first. The number that keeps them apart on ELF
  // has to fit the field the assembler parses it into, and 2^32-1 is spelled
  // for "no unique ID" there.
  if (Sections.size() > std::numeric_limits<uint32_t>::max() - 1)
    return writerStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE,
                        detail(DetailSectionCountUnsupported));
  std::set<std::string> SectionIdentities;

  for (size_t SectionIndex = 0; SectionIndex != Sections.size();
       ++SectionIndex) {
    const SectionRecord &Section = Sections[SectionIndex];
    const ComdatRecord *Comdat = findComdat(Index, Section.Comdat);
    if (!neverc_handle_is_null(Section.Comdat) && !Comdat)
      return writerStatus(NEVERC_STATUS_VERIFICATION_FAILED,
                          detailAt(DetailComdatMissing, SectionIndex));
    if (Comdat && Target.isOSBinFormatCOFF() &&
        Comdat->Selection != NEVERC_OBJECT_COMDAT_ASSOCIATIVE &&
        !ClaimedComdats.insert(handleKey(Section.Comdat)).second)
      return writerStatus(NEVERC_STATUS_VERIFICATION_FAILED,
                          detailAt(DetailComdatClaimedTwice, SectionIndex));
    StringRef AssociatedName;
    if (Comdat && Comdat->Selection == NEVERC_OBJECT_COMDAT_ASSOCIATIVE) {
      const ComdatRecord *Parent = findComdat(Index, Comdat->AssociatedComdat);
      if (!Parent)
        return writerStatus(NEVERC_STATUS_VERIFICATION_FAILED,
                            detailAt(DetailComdatParentMissing, SectionIndex));
      AssociatedName = Parent->Name;
    }
    const bool IdentityRepeated =
        !SectionIdentities
             .insert(sectionIdentity(Section, Target, Comdat, AssociatedName))
             .second;
    Status = emitSectionDirective(OS, Section, SectionIndex, Target, Comdat,
                                  AssociatedName, IdentityRepeated);
    if (!neverc_status_is_ok(Status))
      return Status;
    // ".p2align" states an exponent and the assembler holds the alignment it
    // stands for in 32 bits, so anything above 2^31 has no spelling. Leaving
    // the directive off instead would say the section is byte-aligned, which
    // is a different section from the one the graph describes.
    if (Section.Alignment > 1) {
      if (!isPowerOf2_64(Section.Alignment) || Log2_64(Section.Alignment) > 31)
        return writerStatus(
            NEVERC_STATUS_CAPABILITY_UNAVAILABLE,
            detailAt(DetailSectionAlignmentUnsupported, SectionIndex));
      OS << "\t.p2align\t" << Log2_64(Section.Alignment) << '\n';
    }
    Status =
        emitSectionContents(OS, Section, SectionIndex, Sections, Index, Target);
    if (!neverc_status_is_ok(Status))
      return Status;
  }

  if (Target.isOSBinFormatELF())
    for (const SymbolRecord &Symbol : Symbols)
      if (Symbol.Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED &&
          Symbol.Size != 0)
        OS << "\t.size\t" << assemblyName(Symbol.Name) << ',' << Symbol.Size
           << '\n';
  OS.flush();
  return neverc_status_ok();
}

std::string joinedFeatures(NevercStringArrayView Features) {
  std::string Result;
  const auto *Data = reinterpret_cast<const uint8_t *>(Features.Data);
  // A stride shorter than an element would make consecutive reads overlap and
  // run past the array, so treat the view as unusable rather than trusting it.
  if (!Data || Features.ElementStride < sizeof(NevercStringView))
    return Result;
  for (uint64_t Index = 0; Index != Features.Count; ++Index) {
    const auto *Feature = reinterpret_cast<const NevercStringView *>(
        Data + Index * Features.ElementStride);
    if (!Feature->Data || Feature->Length > std::numeric_limits<size_t>::max())
      continue;
    if (!Result.empty())
      Result.push_back(',');
    Result.append(Feature->Data, static_cast<size_t>(Feature->Length));
  }
  return Result;
}

} // namespace

NevercStatus NEVERC_CALL
writeBuiltinLLVMObject(void *, const NevercObjectWriteRequest *Request) {
  if (!Request || !Request->Object || !Request->Binary ||
      !Request->Binary->Write)
    return writerStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                        detail(DetailRequestArguments));
  if (Request->Header.StructSize < sizeof(*Request) ||
      Request->Header.Major != NEVERC_OBJECT_FORMAT_API_MAJOR ||
      Request->Header.Minor > NEVERC_OBJECT_FORMAT_API_MINOR)
    return writerStatus(NEVERC_STATUS_ABI_MISMATCH, detail(DetailRequestABI));
  if (Request->Header.Flags != 0 &&
      Request->Header.Minor < NEVERC_OBJECT_WRITE_REQUEST_FLAGS_API_MINOR)
    return writerStatus(NEVERC_STATUS_ABI_MISMATCH, detail(DetailRequestABI));
  if ((Request->Header.Flags & ~NEVERC_OBJECT_WRITE_REQUEST_KNOWN_FLAGS) != 0)
    return writerStatus(NEVERC_STATUS_ABI_MISMATCH, detail(DetailRequestABI));
  const bool CanonicalELFTables =
      (Request->Header.Flags & NEVERC_OBJECT_WRITE_CANONICAL_ELF_TABLES) != 0;
  const bool AndroidKernelRelease =
      (Request->Header.Flags & NEVERC_OBJECT_WRITE_ANDROID_KERNEL_RELEASE) != 0;
  const bool DropDebugInfo =
      (Request->Header.Flags & NEVERC_OBJECT_WRITE_DROP_DEBUG_INFO) != 0;
  if ((AndroidKernelRelease || DropDebugInfo) && !CanonicalELFTables)
    return writerStatus(NEVERC_STATUS_ABI_MISMATCH, detail(DetailRequestABI));

  StringRef TripleText(
      Request->Target.RawTriple.Data ? Request->Target.RawTriple.Data : "",
      static_cast<size_t>(Request->Target.RawTriple.Length));
  Triple TargetTriple(Triple::normalize(TripleText));
  const BuiltinTargetRoute *Route = findBuiltinTargetRoute(TripleText);
  if (!Route || !Route->SupportsObject)
    return writerStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE,
                        detail(DetailTargetRouteMissing));
  auto Target = lookupBuiltinLLVMTarget(*Route);
  if (!Target) {
    consumeError(Target.takeError());
    return writerStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE,
                        detail(DetailTargetLookupFailed));
  }

  std::string Assembly;
  NevercStatus Status = buildAssembly(*Request, TargetTriple, Assembly);
  if (!neverc_status_is_ok(Status))
    return Status;

  SmallVector<char, 0> ObjectBytes;
  raw_svector_ostream Output(ObjectBytes);
  StringRef CPU(Request->Target.CPU.Data ? Request->Target.CPU.Data : "",
                static_cast<size_t>(Request->Target.CPU.Length));
  const std::string Features = joinedFeatures(Request->Target.Features);
  BuiltinLLVMAsmParserRequest ParseRequest;
  ParseRequest.Target = *Target;
  ParseRequest.TargetTriple = TargetTriple;
  ParseRequest.CPU = CPU;
  ParseRequest.Features = Features;
  ParseRequest.Input = MemoryBufferRef(Assembly, "<neverc-object-graph>");
  ParseRequest.Output = &Output;
  if (Error E = runBuiltinLLVMAsmParser(ParseRequest)) {
    consumeError(std::move(E));
    return writerStatus(NEVERC_STATUS_VERIFICATION_FAILED,
                        detail(DetailAssemblyParseFailed));
  }

  if (CanonicalELFTables) {
    if (!TargetTriple.isOSBinFormatELF())
      return writerStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE,
                          detail(DetailELFCanonicalizationFailed));

    // LLVM MC deliberately reuses one `.strtab` for symbol and section names.
    // Canonical-table-only writes split those tables transactionally without
    // running link semantics over a single object: COMDAT, profile metadata,
    // symbol multiplicity, and relocation records must remain writer output.
    // A serialized Android release remains a separate authoritative final byte
    // boundary and therefore still runs the native release finalizer.
    const StringRef Input(ObjectBytes.data(), ObjectBytes.size());
    if (!AndroidKernelRelease) {
      auto Canonical = canonicalizeBuiltinELFTables(Input, DropDebugInfo);
      if (!Canonical) {
        consumeError(Canonical.takeError());
        return writerStatus(NEVERC_STATUS_VERIFICATION_FAILED,
                            detail(DetailELFCanonicalizationFailed));
      }
      ObjectBytes = std::move(*Canonical);
    } else {
      SmallVector<char, 0> CanonicalBytes;
      raw_svector_ostream CanonicalOutput(CanonicalBytes);
      neverc::merge::Options MergeOptions;
      MergeOptions.pureC = false;
      MergeOptions.mergeSections = false;
      MergeOptions.stripUnneededSymbols = AndroidKernelRelease;
      MergeOptions.dropDebugInfo = DropDebugInfo;
      MergeOptions.androidKernelModule = AndroidKernelRelease;
      MergeOptions.finalizeAndroidKernelModule = AndroidKernelRelease;
      MergeOptions.verify = true;
      if (!neverc::merge::mergeObjects(
              ArrayRef<StringRef>(&Input, 1), CanonicalOutput,
              neverc::merge::Format::ELF64LE, MergeOptions))
        return writerStatus(NEVERC_STATUS_VERIFICATION_FAILED,
                            detail(DetailELFCanonicalizationFailed));
      ObjectBytes = std::move(CanonicalBytes);
    }
  }

  NevercByteView Bytes{reinterpret_cast<const uint8_t *>(ObjectBytes.data()),
                       static_cast<uint64_t>(ObjectBytes.size())};
  Status = Request->Binary->Write(Request->Binary->Context, Request->Task,
                                  Request->Builder, Bytes);
  if (!neverc_status_is_ok(Status) && Status.Detail == 0)
    Status.Detail = detail(DetailBinaryWriteFailed);
  return Status;
}

} // namespace neverc::plugin
