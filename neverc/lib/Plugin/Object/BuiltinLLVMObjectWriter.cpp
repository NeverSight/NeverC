#include "BuiltinLLVMObjectWriter.h"
#include "neverc/Plugin/Host/BuiltinLLVMAsmParser.h"
#include "neverc/Plugin/Host/BuiltinTargetProvider.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

namespace neverc::plugin {
namespace {

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

bool sameHandle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

bool sameID(NevercInterfaceID Left, NevercInterfaceID Right) {
  return Left.High == Right.High && Left.Low == Right.Low;
}

bool validName(StringRef Name) {
  if (Name.empty())
    return false;
  return llvm::all_of(Name, [](unsigned char C) {
    return std::isalnum(C) || C == '_' || C == '.' || C == '$' ||
           C == '@' || C == '-';
  });
}

bool copyString(NevercStringView View, std::string &Output) {
  if (View.Length > std::numeric_limits<size_t>::max() ||
      (!View.Data && View.Length != 0))
    return false;
  StringRef Text(View.Data ? View.Data : "",
                 static_cast<size_t>(View.Length));
  if (!validName(Text))
    return false;
  Output = Text.str();
  return true;
}

bool copyBytes(NevercByteView View, std::vector<uint8_t> &Output) {
  if (View.Length > std::numeric_limits<size_t>::max() ||
      (!View.Data && View.Length != 0))
    return false;
  if (View.Length != 0)
    Output.assign(View.Data,
                  View.Data + static_cast<size_t>(View.Length));
  return true;
}

bool supportedExtension(NevercObjectFormatID FormatID,
                        NevercObjectFormatID Owner, uint32_t Version,
                        ArrayRef<uint8_t> Bytes, StringRef Tag) {
  if (Bytes.empty())
    return Owner.High == 0 && Owner.Low == 0 && Version == 0;
  return sameID(Owner, FormatID) && Version == 1 && Bytes.size() >= 8 &&
         StringRef(reinterpret_cast<const char *>(Bytes.data()), 4) == Tag;
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
      Status.Detail = 151;
    return Status;
  }
  for (;;) {
    NevercObjectComdatInfo Info{};
    Info.Header = {sizeof(Info), NEVERC_OBJECT_API_MAJOR,
                   NEVERC_OBJECT_API_MINOR, 0};
    Status = Request.Object->GetComdatInfo(
        Request.Object->Context, Request.Task, Handle, &Info);
    if (!neverc_status_is_ok(Status)) {
      if (Status.Detail == 0)
        Status.Detail = 160 + Comdats.size();
      return Status;
    }
    ComdatRecord Record;
    Record.Handle = Handle;
    if (!copyString(Info.Name, Record.Name))
      return writerStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                          170 + Comdats.size());
    Record.Selection = Info.Selection;
    Record.AssociatedComdat = Info.AssociatedComdat;
    Record.ExtensionOwner = Info.ExtensionOwner;
    Record.ExtensionVersion = Info.ExtensionVersion;
    if (!copyBytes(Info.Extension, Record.Extension))
      return writerStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                          180 + Comdats.size());
    if (!supportedExtension(Request.FormatID, Record.ExtensionOwner,
                            Record.ExtensionVersion, Record.Extension,
                            "NCCO"))
      return writerStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE, 190);
    Comdats.push_back(std::move(Record));

    NevercObjectComdatHandle Next{};
    Status = Request.Object->GetNextComdat(
        Request.Object->Context, Request.Task, Handle, &Next);
    if (Status.Code == NEVERC_STATUS_NOT_FOUND)
      return neverc_status_ok();
    if (!neverc_status_is_ok(Status)) {
      if (Status.Detail == 0)
        Status.Detail = 191;
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
      Status.Detail = 201;
    return Status;
  }
  for (;;) {
    NevercObjectSectionInfo Info{};
    Info.Header = {sizeof(Info), NEVERC_OBJECT_API_MAJOR,
                   NEVERC_OBJECT_API_MINOR, 0};
    Status = Request.Object->GetSectionInfo(
        Request.Object->Context, Request.Task, Handle, &Info);
    if (!neverc_status_is_ok(Status)) {
      if (Status.Detail == 0)
        Status.Detail = 210 + Sections.size();
      return Status;
    }
    if (Info.Data.Length > std::numeric_limits<size_t>::max() ||
        (!Info.Data.Data && Info.Data.Length != 0))
      return writerStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                          220 + Sections.size());
    SectionRecord Record;
    Record.Handle = Handle;
    if (!copyString(Info.Name, Record.Name))
      return writerStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                          230 + Sections.size());
    Record.Kind = Info.Kind;
    Record.Flags = Info.Flags;
    Record.Alignment = Info.Alignment;
    if (!copyBytes(Info.Data, Record.Data) ||
        !copyBytes(Info.Extension, Record.Extension))
      return writerStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                          235 + Sections.size());
    Record.ZeroFillSize = Info.ZeroFillSize;
    Record.Comdat = Info.Comdat;
    Record.ExtensionOwner = Info.ExtensionOwner;
    Record.ExtensionVersion = Info.ExtensionVersion;
    if (!supportedExtension(Request.FormatID, Record.ExtensionOwner,
                            Record.ExtensionVersion, Record.Extension,
                            "NCSE"))
      return writerStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE, 241);
    Sections.push_back(std::move(Record));

    NevercObjectSectionHandle Next{};
    Status = Request.Object->GetNextSection(
        Request.Object->Context, Request.Task, Handle, &Next);
    if (Status.Code == NEVERC_STATUS_NOT_FOUND)
      return neverc_status_ok();
    if (!neverc_status_is_ok(Status)) {
      if (Status.Detail == 0)
        Status.Detail = 250;
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
      Status.Detail = 301;
    return Status;
  }
  for (;;) {
    NevercObjectSymbolInfo Info{};
    Info.Header = {sizeof(Info), NEVERC_OBJECT_API_MAJOR,
                   NEVERC_OBJECT_API_MINOR, 0};
    Status = Request.Object->GetSymbolInfo(
        Request.Object->Context, Request.Task, Handle, &Info);
    if (!neverc_status_is_ok(Status)) {
      if (Status.Detail == 0)
        Status.Detail = 310 + Symbols.size();
      return Status;
    }
    SymbolRecord Record;
    Record.Handle = Handle;
    if (!copyString(Info.Name, Record.Name))
      return writerStatus(NEVERC_STATUS_INVALID_ARGUMENT,
                          320 + Symbols.size());
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
                          325 + Symbols.size());
    if (!supportedExtension(Request.FormatID, Record.ExtensionOwner,
                            Record.ExtensionVersion, Record.Extension,
                            "NCSY"))
      return writerStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE, 331);
    Symbols.push_back(std::move(Record));

    NevercObjectSymbolHandle Next{};
    Status = Request.Object->GetNextSymbol(
        Request.Object->Context, Request.Task, Handle, &Next);
    if (Status.Code == NEVERC_STATUS_NOT_FOUND)
      return neverc_status_ok();
    if (!neverc_status_is_ok(Status)) {
      if (Status.Detail == 0)
        Status.Detail = 340;
      return Status;
    }
    Handle = Next;
  }
}

NevercStatus collectRelocations(
    const NevercObjectWriteRequest &Request,
    std::vector<RelocationRecord> &Relocations) {
  NevercObjectRelocationHandle Handle{};
  NevercStatus Status = Request.Object->GetFirstRelocation(
      Request.Object->Context, Request.Task, Request.Graph, &Handle);
  if (Status.Code == NEVERC_STATUS_NOT_FOUND)
    return neverc_status_ok();
  if (!neverc_status_is_ok(Status)) {
    if (Status.Detail == 0)
      Status.Detail = 401;
    return Status;
  }
  for (;;) {
    NevercObjectRelocationInfo Info{};
    Info.Header = {sizeof(Info), NEVERC_OBJECT_API_MAJOR,
                   NEVERC_OBJECT_API_MINOR, 0};
    Status = Request.Object->GetRelocationInfo(
        Request.Object->Context, Request.Task, Handle, &Info);
    if (!neverc_status_is_ok(Status)) {
      if (Status.Detail == 0)
        Status.Detail = 410 + Relocations.size();
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
                          420 + Relocations.size());
    if (!supportedExtension(Request.FormatID, Record.ExtensionOwner,
                            Record.ExtensionVersion, Record.Extension,
                            "NCRL"))
      return writerStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE, 430);
    Relocations.push_back(std::move(Record));

    NevercObjectRelocationHandle Next{};
    Status = Request.Object->GetNextRelocation(
        Request.Object->Context, Request.Task, Handle, &Next);
    if (Status.Code == NEVERC_STATUS_NOT_FOUND)
      return neverc_status_ok();
    if (!neverc_status_is_ok(Status)) {
      if (Status.Detail == 0)
        Status.Detail = 440;
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

NevercStatus emitSectionDirective(raw_ostream &OS,
                                  const SectionRecord &Section,
                                  const Triple &Target,
                                  const ComdatRecord *Comdat) {
  if (Comdat) {
    if (Target.isOSBinFormatMachO())
      return writerStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE, 470);
    const bool ZeroFill =
        Section.Kind == NEVERC_OBJECT_SECTION_KIND_ZERO_FILL ||
        Section.Kind == NEVERC_OBJECT_SECTION_KIND_TLS_ZERO_FILL;
    std::string Flags;
    if (Target.isOSBinFormatELF()) {
      if (Comdat->Selection != NEVERC_OBJECT_COMDAT_ANY)
        return writerStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE, 471);
      if ((Section.Flags & NEVERC_OBJECT_SECTION_ALLOCATED) != 0)
        Flags.push_back('a');
      if ((Section.Flags & NEVERC_OBJECT_SECTION_WRITABLE) != 0)
        Flags.push_back('w');
      if ((Section.Flags & NEVERC_OBJECT_SECTION_EXECUTABLE) != 0)
        Flags.push_back('x');
      if ((Section.Flags & NEVERC_OBJECT_SECTION_TLS) != 0)
        Flags.push_back('T');
      Flags.push_back('G');
      OS << "\t.section\t" << Section.Name << ",\"" << Flags << "\",@"
         << (ZeroFill ? "nobits" : "progbits") << ',' << Comdat->Name
         << ",comdat\n";
      return neverc_status_ok();
    }
    StringRef Selection = coffComdatSelection(Comdat->Selection);
    if (Selection.empty())
      return writerStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE, 472);
    if ((Section.Flags & NEVERC_OBJECT_SECTION_EXECUTABLE) != 0)
      Flags = "xr";
    else
      Flags = (Section.Flags & NEVERC_OBJECT_SECTION_WRITABLE) != 0
                  ? "dw"
                  : "dr";
    if (ZeroFill)
      Flags.push_back('b');
    OS << "\t.section\t" << Section.Name << ",\"" << Flags << "\","
       << Selection << ',' << Comdat->Name << '\n';
    return neverc_status_ok();
  }
  if (Section.Kind == NEVERC_OBJECT_SECTION_KIND_TLS_DATA) {
    if (Target.isOSBinFormatMachO())
      OS << "\t.section\t__DATA," << Section.Name
         << ",thread_local_regular\n";
    else if (Target.isOSBinFormatELF())
      OS << "\t.section\t" << Section.Name << ",\"awT\",@progbits\n";
    else
      OS << "\t.section\t" << Section.Name << ",\"dw\"\n";
    return neverc_status_ok();
  }
  if (Section.Kind == NEVERC_OBJECT_SECTION_KIND_TLS_ZERO_FILL) {
    if (Target.isOSBinFormatMachO())
      OS << "\t.section\t__DATA," << Section.Name
         << ",thread_local_zerofill\n";
    else if (Target.isOSBinFormatELF())
      OS << "\t.section\t" << Section.Name << ",\"awT\",@nobits\n";
    else
      OS << "\t.section\t" << Section.Name << ",\"bw\"\n";
    return neverc_status_ok();
  }
  if (Section.Kind == NEVERC_OBJECT_SECTION_KIND_TEXT &&
      Section.Name == ".text") {
    OS << "\t.text\n";
    return neverc_status_ok();
  }
  if (Section.Kind == NEVERC_OBJECT_SECTION_KIND_DATA &&
      Section.Name == ".data") {
    OS << "\t.data\n";
    return neverc_status_ok();
  }
  if (Section.Kind == NEVERC_OBJECT_SECTION_KIND_ZERO_FILL &&
      (Section.Name == ".bss" || Section.Name == "__bss")) {
    OS << "\t.bss\n";
    return neverc_status_ok();
  }
  if (Target.isOSBinFormatMachO()) {
    const StringRef Segment =
        (Section.Flags & NEVERC_OBJECT_SECTION_EXECUTABLE) != 0
            ? "__TEXT"
            : "__DATA";
    OS << "\t.section\t" << Segment << ',' << Section.Name << '\n';
    return neverc_status_ok();
  }
  if (Target.isOSBinFormatELF()) {
    std::string Flags;
    if ((Section.Flags & NEVERC_OBJECT_SECTION_ALLOCATED) != 0)
      Flags.push_back('a');
    if ((Section.Flags & NEVERC_OBJECT_SECTION_WRITABLE) != 0)
      Flags.push_back('w');
    if ((Section.Flags & NEVERC_OBJECT_SECTION_EXECUTABLE) != 0)
      Flags.push_back('x');
    const bool ZeroFill =
        Section.Kind == NEVERC_OBJECT_SECTION_KIND_ZERO_FILL;
    OS << "\t.section\t" << Section.Name << ",\"" << Flags << "\",@"
       << (ZeroFill ? "nobits" : "progbits") << '\n';
    return neverc_status_ok();
  }
  std::string Flags;
  if ((Section.Flags & NEVERC_OBJECT_SECTION_EXECUTABLE) != 0)
    Flags.push_back('x');
  else
    Flags.push_back((Section.Flags & NEVERC_OBJECT_SECTION_WRITABLE) != 0
                        ? 'd'
                        : 'r');
  if (Section.Kind == NEVERC_OBJECT_SECTION_KIND_ZERO_FILL)
    Flags.push_back('b');
  OS << "\t.section\t" << Section.Name << ",\"" << Flags << "\"\n";
  return neverc_status_ok();
}

void emitSymbolAttributes(raw_ostream &OS, const SymbolRecord &Symbol,
                          const Triple &Target) {
  if (Symbol.Binding == NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL)
    OS << "\t.globl\t" << Symbol.Name << '\n';
  else if (Symbol.Binding == NEVERC_OBJECT_SYMBOL_BINDING_WEAK) {
    if (Target.isOSBinFormatMachO())
      OS << (Symbol.Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED
                 ? "\t.weak_definition\t"
                 : "\t.weak_reference\t")
         << Symbol.Name << '\n';
    else
      OS << "\t.weak\t" << Symbol.Name << '\n';
  }
  if (Symbol.Visibility == NEVERC_OBJECT_SYMBOL_VISIBILITY_HIDDEN) {
    if (Target.isOSBinFormatMachO())
      OS << "\t.private_extern\t" << Symbol.Name << '\n';
    else if (Target.isOSBinFormatELF())
      OS << "\t.hidden\t" << Symbol.Name << '\n';
  }
  if (Target.isOSBinFormatELF() &&
      Symbol.Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED) {
    if (Symbol.Type == NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION)
      OS << "\t.type\t" << Symbol.Name << ",@function\n";
    else if (Symbol.Type == NEVERC_OBJECT_SYMBOL_TYPE_OBJECT ||
             Symbol.Type == NEVERC_OBJECT_SYMBOL_TYPE_TLS)
      OS << "\t.type\t" << Symbol.Name << ",@object\n";
  }
}

const SymbolRecord *
findSymbol(ArrayRef<SymbolRecord> Symbols, NevercObjectSymbolHandle Handle) {
  auto It = llvm::find_if(Symbols, [&](const SymbolRecord &Symbol) {
    return sameHandle(Symbol.Handle, Handle);
  });
  return It == Symbols.end() ? nullptr : &*It;
}

const ComdatRecord *
findComdat(ArrayRef<ComdatRecord> Comdats,
           NevercObjectComdatHandle Handle) {
  if (neverc_handle_is_null(Handle))
    return nullptr;
  auto It = llvm::find_if(Comdats, [&](const ComdatRecord &Comdat) {
    return sameHandle(Comdat.Handle, Handle);
  });
  return It == Comdats.end() ? nullptr : &*It;
}

const SectionRecord *
findSection(ArrayRef<SectionRecord> Sections,
            NevercObjectSectionHandle Handle) {
  auto It = llvm::find_if(Sections, [&](const SectionRecord &Section) {
    return sameHandle(Section.Handle, Handle);
  });
  return It == Sections.end() ? nullptr : &*It;
}

std::string sectionLabel(size_t Index, const Triple &Target) {
  return (Target.isOSBinFormatMachO() ? "Lneverc_section_"
                                     : ".Lneverc_section_") +
         std::to_string(Index);
}

NevercStatus emitRelocationValue(
    raw_ostream &OS, const RelocationRecord &Relocation,
    ArrayRef<SectionRecord> Sections, ArrayRef<SymbolRecord> Symbols,
    const Triple &Target) {
  StringRef Directive;
  switch (Relocation.Width) {
  case 8:
    Directive = ".byte";
    break;
  case 16:
    Directive = ".short";
    break;
  case 32:
    Directive = ".long";
    break;
  case 64:
    Directive = ".quad";
    break;
  default:
    return writerStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE, 450);
  }

  std::string TargetExpression;
  raw_string_ostream Expression(TargetExpression);
  if (Relocation.TargetKind ==
      NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL) {
    const SymbolRecord *Symbol =
        findSymbol(Symbols, Relocation.TargetSymbol);
    if (!Symbol)
      return writerStatus(NEVERC_STATUS_VERIFICATION_FAILED, 451);
    Expression << Symbol->Name;
  } else if (Relocation.TargetKind ==
             NEVERC_OBJECT_RELOCATION_TARGET_SECTION) {
    const SectionRecord *Section =
        findSection(Sections, Relocation.TargetSection);
    if (!Section)
      return writerStatus(NEVERC_STATUS_VERIFICATION_FAILED, 452);
    const size_t Index =
        static_cast<size_t>(Section - Sections.data());
    Expression << sectionLabel(Index, Target);
  } else {
    return writerStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE, 453);
  }
  if (Relocation.Addend > 0)
    Expression << '+' << Relocation.Addend;
  else if (Relocation.Addend < 0)
    Expression << Relocation.Addend;

  if (Relocation.IsPCRelative ||
      Relocation.Kind == NEVERC_OBJECT_RELOCATION_PC_RELATIVE) {
    Expression << "-.";
  } else if (Relocation.Kind != NEVERC_OBJECT_RELOCATION_ABSOLUTE) {
    return writerStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE, 454);
  }
  Expression.flush();
  OS << '\t' << Directive << '\t' << TargetExpression << '\n';
  return neverc_status_ok();
}

NevercStatus emitSectionContents(
    raw_ostream &OS, const SectionRecord &Section, size_t SectionIndex,
    ArrayRef<SectionRecord> Sections, ArrayRef<SymbolRecord> Symbols,
    ArrayRef<RelocationRecord> Relocations, const Triple &Target) {
  OS << sectionLabel(SectionIndex, Target) << ":\n";

  std::vector<const SymbolRecord *> Defined;
  for (const SymbolRecord &Symbol : Symbols)
    if (Symbol.Definition ==
            NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED &&
        sameHandle(Symbol.Section, Section.Handle))
      Defined.push_back(&Symbol);
  llvm::sort(Defined,
             [](const SymbolRecord *Left, const SymbolRecord *Right) {
               if (Left->Value != Right->Value)
                 return Left->Value < Right->Value;
               return Left->Name < Right->Name;
             });

  std::vector<const RelocationRecord *> SectionRelocations;
  for (const RelocationRecord &Relocation : Relocations)
    if (sameHandle(Relocation.Section, Section.Handle))
      SectionRelocations.push_back(&Relocation);
  llvm::sort(SectionRelocations,
             [](const RelocationRecord *Left,
                const RelocationRecord *Right) {
               return Left->Offset < Right->Offset;
             });

  uint64_t Offset = 0;
  size_t SymbolIndex = 0;
  auto emitSymbolsThrough = [&](uint64_t End, bool IncludeEnd) {
    while (SymbolIndex != Defined.size() &&
           (Defined[SymbolIndex]->Value < End ||
            (IncludeEnd && Defined[SymbolIndex]->Value == End))) {
      const SymbolRecord &Symbol = *Defined[SymbolIndex];
      if (Symbol.Value < Offset || Symbol.Value > Section.Data.size())
        return writerStatus(NEVERC_STATUS_VERIFICATION_FAILED, 460);
      emitBytes(OS, ArrayRef<uint8_t>(Section.Data)
                        .slice(static_cast<size_t>(Offset),
                               static_cast<size_t>(Symbol.Value - Offset)));
      Offset = Symbol.Value;
      OS << Symbol.Name << ":\n";
      ++SymbolIndex;
    }
    return neverc_status_ok();
  };

  for (const RelocationRecord *Relocation : SectionRelocations) {
    if (Relocation->Width == 0 || (Relocation->Width % 8) != 0)
      return writerStatus(NEVERC_STATUS_VERIFICATION_FAILED, 461);
    const uint64_t Width = Relocation->Width / 8;
    if (Relocation->Offset < Offset ||
        Relocation->Offset > Section.Data.size() ||
        Width > Section.Data.size() - Relocation->Offset)
      return writerStatus(NEVERC_STATUS_VERIFICATION_FAILED, 462);
    NevercStatus Status =
        emitSymbolsThrough(Relocation->Offset, true);
    if (!neverc_status_is_ok(Status))
      return Status;
    emitBytes(OS, ArrayRef<uint8_t>(Section.Data)
                      .slice(static_cast<size_t>(Offset),
                             static_cast<size_t>(Relocation->Offset -
                                                 Offset)));
    Offset = Relocation->Offset;
    if (SymbolIndex != Defined.size() &&
        Defined[SymbolIndex]->Value < Offset + Width)
      return writerStatus(NEVERC_STATUS_VERIFICATION_FAILED, 463);
    Status = emitRelocationValue(OS, *Relocation, Sections, Symbols,
                                 Target);
    if (!neverc_status_is_ok(Status))
      return Status;
    Offset += Width;
  }

  NevercStatus Status =
      emitSymbolsThrough(Section.Data.size(), true);
  if (!neverc_status_is_ok(Status))
    return Status;
  emitBytes(OS, ArrayRef<uint8_t>(Section.Data)
                    .drop_front(static_cast<size_t>(Offset)));
  if (SymbolIndex != Defined.size())
    return writerStatus(NEVERC_STATUS_VERIFICATION_FAILED, 464);
  if (Section.ZeroFillSize != 0)
    OS << "\t.zero\t" << Section.ZeroFillSize << '\n';
  return neverc_status_ok();
}

NevercStatus buildAssembly(const NevercObjectWriteRequest &Request,
                           const Triple &Target,
                           std::string &Assembly) {
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

  if (Target.isOSBinFormatMachO()) {
    size_t TemporaryIndex = 0;
    for (SymbolRecord &Symbol : Symbols)
      if (Symbol.Binding == NEVERC_OBJECT_SYMBOL_BINDING_LOCAL &&
          StringRef(Symbol.Name).starts_with("ltmp"))
        Symbol.Name =
            "Lneverc_local_" + std::to_string(TemporaryIndex++);
  }

  raw_string_ostream OS(Assembly);
  for (const SymbolRecord &Symbol : Symbols) {
    emitSymbolAttributes(OS, Symbol, Target);
    if (Symbol.Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_COMMON)
      OS << "\t.comm\t" << Symbol.Name << ',' << Symbol.Size << ','
         << std::max<uint64_t>(Symbol.Alignment, 1) << '\n';
    else if (Symbol.Definition ==
             NEVERC_OBJECT_SYMBOL_DEFINITION_ABSOLUTE)
      OS << "\t.set\t" << Symbol.Name << ',' << Symbol.Value << '\n';
  }

  for (size_t SectionIndex = 0; SectionIndex != Sections.size();
       ++SectionIndex) {
    const SectionRecord &Section = Sections[SectionIndex];
    const ComdatRecord *Comdat =
        findComdat(Comdats, Section.Comdat);
    if (!neverc_handle_is_null(Section.Comdat) && !Comdat)
      return writerStatus(NEVERC_STATUS_VERIFICATION_FAILED, 473);
    Status = emitSectionDirective(OS, Section, Target, Comdat);
    if (!neverc_status_is_ok(Status))
      return Status;
    if (Section.Alignment > 1 && isPowerOf2_64(Section.Alignment))
      OS << "\t.p2align\t" << Log2_64(Section.Alignment) << '\n';
    Status = emitSectionContents(OS, Section, SectionIndex, Sections,
                                 Symbols, Relocations, Target);
    if (!neverc_status_is_ok(Status))
      return Status;
  }

  if (Target.isOSBinFormatELF())
    for (const SymbolRecord &Symbol : Symbols)
      if (Symbol.Definition ==
              NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED &&
          Symbol.Size != 0)
        OS << "\t.size\t" << Symbol.Name << ',' << Symbol.Size << '\n';
  OS.flush();
  return neverc_status_ok();
}

std::string joinedFeatures(NevercStringArrayView Features) {
  std::string Result;
  const auto *Data =
      reinterpret_cast<const uint8_t *>(Features.Data);
  for (uint64_t Index = 0; Data && Index != Features.Count; ++Index) {
    const auto *Feature =
        reinterpret_cast<const NevercStringView *>(
            Data + Index * Features.ElementStride);
    if (!Result.empty())
      Result.push_back(',');
    if (Feature->Data)
      Result.append(Feature->Data,
                    static_cast<size_t>(Feature->Length));
  }
  return Result;
}

} // namespace

NevercStatus NEVERC_CALL writeBuiltinLLVMObject(
    void *, const NevercObjectWriteRequest *Request) {
  if (!Request || !Request->Object || !Request->Binary ||
      !Request->Binary->Write)
    return writerStatus(NEVERC_STATUS_INVALID_ARGUMENT, 100);
  if (Request->Header.StructSize < sizeof(*Request) ||
      Request->Header.Major != NEVERC_OBJECT_FORMAT_API_MAJOR ||
      Request->Header.Minor > NEVERC_OBJECT_FORMAT_API_MINOR)
    return writerStatus(NEVERC_STATUS_ABI_MISMATCH, 101);

  StringRef TripleText(
      Request->Target.RawTriple.Data
          ? Request->Target.RawTriple.Data
          : "",
      static_cast<size_t>(Request->Target.RawTriple.Length));
  Triple TargetTriple(Triple::normalize(TripleText));
  const BuiltinTargetRoute *Route =
      findBuiltinTargetRoute(TripleText);
  if (!Route || !Route->SupportsObject)
    return writerStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE, 500);
  auto Target = lookupBuiltinLLVMTarget(*Route);
  if (!Target) {
    consumeError(Target.takeError());
    return writerStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE, 501);
  }

  std::string Assembly;
  NevercStatus Status =
      buildAssembly(*Request, TargetTriple, Assembly);
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
  ParseRequest.Input =
      MemoryBufferRef(Assembly, "<neverc-object-graph>");
  ParseRequest.Output = &Output;
  if (Error E = runBuiltinLLVMAsmParser(ParseRequest)) {
    consumeError(std::move(E));
    return writerStatus(NEVERC_STATUS_VERIFICATION_FAILED, 510);
  }

  NevercByteView Bytes{
      reinterpret_cast<const uint8_t *>(ObjectBytes.data()),
      static_cast<uint64_t>(ObjectBytes.size())};
  Status = Request->Binary->Write(
      Request->Binary->Context, Request->Task, Request->Builder, Bytes);
  if (!neverc_status_is_ok(Status) && Status.Detail == 0)
    Status.Detail = 600;
  return Status;
}

} // namespace neverc::plugin
