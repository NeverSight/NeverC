#include "BuiltinLLVMObjectWriter.h"
#include "neverc/Plugin/Host/BuiltinTargetProvider.h"
#include "neverc/Plugin/Host/ObjectReaderProvider.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/MachO.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBufferRef.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;
using namespace llvm::object;

namespace neverc::plugin {
namespace {

struct BuiltinFormatContext {
  BuiltinObjectFormat Format;
};

constexpr BuiltinFormatContext ELFContext{BuiltinObjectFormat::ELF};
constexpr BuiltinFormatContext COFFContext{BuiltinObjectFormat::COFF};
constexpr BuiltinFormatContext MachOContext{BuiltinObjectFormat::MachO};

NevercStatus status(NevercStatusCode Code, uint64_t Detail = 0) {
  NevercStatus Result = neverc_status_ok();
  Result.Code = Code;
  Result.Detail = Detail;
  return Result;
}

bool sameID(NevercInterfaceID Left, NevercInterfaceID Right) {
  return Left.High == Right.High && Left.Low == Right.Low;
}

NevercStringView stringView(StringRef Value) {
  return {Value.data(), static_cast<uint64_t>(Value.size())};
}

NevercByteView byteView(ArrayRef<uint8_t> Value) {
  return {Value.data(), static_cast<uint64_t>(Value.size())};
}

void appendU32(SmallVectorImpl<uint8_t> &Bytes, uint32_t Value) {
  for (unsigned I = 0; I != 4; ++I)
    Bytes.push_back(static_cast<uint8_t>(Value >> (I * 8)));
}

void appendU64(SmallVectorImpl<uint8_t> &Bytes, uint64_t Value) {
  for (unsigned I = 0; I != 8; ++I)
    Bytes.push_back(static_cast<uint8_t>(Value >> (I * 8)));
}

void appendTag(SmallVectorImpl<uint8_t> &Bytes, const char (&Tag)[5]) {
  Bytes.append(reinterpret_cast<const uint8_t *>(Tag),
               reinterpret_cast<const uint8_t *>(Tag) + 4);
}

bool containsInsensitive(StringRef Value, StringRef Needle) {
  return Value.lower().find(Needle.lower()) != std::string::npos;
}

bool isUnwindName(StringRef Name) {
  return containsInsensitive(Name, "eh_frame") ||
         containsInsensitive(Name, "unwind") ||
         containsInsensitive(Name, ".pdata") ||
         containsInsensitive(Name, ".xdata") ||
         containsInsensitive(Name, "__compact_unwind");
}

bool isTLSName(StringRef Name) {
  return containsInsensitive(Name, ".tdata") ||
         containsInsensitive(Name, ".tbss") ||
         containsInsensitive(Name, "__thread") ||
         containsInsensitive(Name, "__tls");
}

bool isTLSSection(const ObjectFile &Object, SectionRef Section,
                  StringRef Name) {
  if (isa<ELFObjectFileBase>(Object))
    return (ELFSectionRef(Section).getFlags() & ELF::SHF_TLS) != 0;
  if (const auto *MachObject = dyn_cast<MachOObjectFile>(&Object)) {
    const uint32_t Type = MachObject->getSectionType(Section);
    return Type == MachO::S_THREAD_LOCAL_REGULAR ||
           Type == MachO::S_THREAD_LOCAL_ZEROFILL ||
           Type == MachO::S_THREAD_LOCAL_VARIABLES ||
           Type == MachO::S_THREAD_LOCAL_VARIABLE_POINTERS ||
           Type == MachO::S_THREAD_LOCAL_INIT_FUNCTION_POINTERS;
  }
  return isTLSName(Name);
}

void nativeSectionExtension(const ObjectFile &Object, SectionRef Section,
                            SmallVectorImpl<uint8_t> &Bytes) {
  appendTag(Bytes, "NCSE");
  appendU32(Bytes, 1);
  appendU64(Bytes, Section.getIndex());
  appendU64(Bytes, Section.getAddress());
  uint64_t Type = 0;
  uint64_t Flags = 0;
  uint64_t Offset = 0;
  if (isa<ELFObjectFileBase>(Object)) {
    ELFSectionRef ELFSection(Section);
    Type = ELFSection.getType();
    Flags = ELFSection.getFlags();
    Offset = ELFSection.getOffset();
  } else if (const auto *COFF = dyn_cast<COFFObjectFile>(&Object)) {
    if (const coff_section *Native = COFF->getCOFFSection(Section)) {
      Flags = Native->Characteristics;
      Offset = Native->PointerToRawData;
    }
  } else if (const auto *MachObject =
                 dyn_cast<MachOObjectFile>(&Object)) {
    if (MachObject->is64Bit()) {
      MachO::section_64 Native =
          MachObject->getSection64(Section.getRawDataRefImpl());
      Flags = Native.flags;
      Offset = Native.offset;
      Type = Native.flags & MachO::SECTION_TYPE;
    } else {
      MachO::section Native =
          MachObject->getSection(Section.getRawDataRefImpl());
      Flags = Native.flags;
      Offset = Native.offset;
      Type = Native.flags & MachO::SECTION_TYPE;
    }
  }
  appendU64(Bytes, Type);
  appendU64(Bytes, Flags);
  appendU64(Bytes, Offset);
}

void nativeSymbolExtension(const ObjectFile &Object, SymbolRef Symbol,
                           SmallVectorImpl<uint8_t> &Bytes) {
  appendTag(Bytes, "NCSY");
  appendU32(Bytes, 1);
  uint64_t Type = 0;
  uint64_t Binding = 0;
  uint64_t Other = 0;
  uint64_t Auxiliary = 0;
  if (isa<ELFObjectFileBase>(Object)) {
    ELFSymbolRef ELFSymbol(Symbol);
    Type = ELFSymbol.getELFType();
    Binding = ELFSymbol.getBinding();
    Other = ELFSymbol.getOther();
    Auxiliary = ELFSymbol.getSize();
  } else if (const auto *COFF = dyn_cast<COFFObjectFile>(&Object)) {
    COFFSymbolRef COFFSymbol = COFF->getCOFFSymbol(Symbol);
    Type = COFFSymbol.getType();
    Binding = COFFSymbol.getStorageClass();
    Other = static_cast<uint32_t>(COFFSymbol.getSectionNumber());
    Auxiliary = COFFSymbol.getNumberOfAuxSymbols();
  } else if (const auto *MachObject =
                 dyn_cast<MachOObjectFile>(&Object)) {
    if (MachObject->is64Bit()) {
      MachO::nlist_64 Native =
          MachObject->getSymbol64TableEntry(Symbol.getRawDataRefImpl());
      Type = Native.n_type;
      Binding = Native.n_sect;
      Other = Native.n_desc;
      Auxiliary = Native.n_value;
    } else {
      MachO::nlist Native =
          MachObject->getSymbolTableEntry(Symbol.getRawDataRefImpl());
      Type = Native.n_type;
      Binding = Native.n_sect;
      Other = Native.n_desc;
      Auxiliary = Native.n_value;
    }
  }
  appendU64(Bytes, Type);
  appendU64(Bytes, Binding);
  appendU64(Bytes, Other);
  appendU64(Bytes, Auxiliary);
}

void nativeRelocationExtension(
    RelocationRef Relocation, StringRef TypeName,
    SmallVectorImpl<uint8_t> &Bytes) {
  appendTag(Bytes, "NCRL");
  appendU32(Bytes, 1);
  appendU64(Bytes, Relocation.getType());
  appendU32(Bytes, static_cast<uint32_t>(TypeName.size()));
  Bytes.append(reinterpret_cast<const uint8_t *>(TypeName.data()),
               reinterpret_cast<const uint8_t *>(TypeName.data()) +
                   TypeName.size());
}

NevercObjectSectionKind
sectionKind(const ObjectFile &Object, SectionRef Section, StringRef Name,
            bool IsTLS) {
  if (Section.isDebugSection())
    return NEVERC_OBJECT_SECTION_KIND_DEBUG;
  if (isUnwindName(Name))
    return NEVERC_OBJECT_SECTION_KIND_UNWIND;
  if (IsTLS)
    return Section.isBSS() || Section.isVirtual()
               ? NEVERC_OBJECT_SECTION_KIND_TLS_ZERO_FILL
               : NEVERC_OBJECT_SECTION_KIND_TLS_DATA;
  if (Section.isText())
    return NEVERC_OBJECT_SECTION_KIND_TEXT;
  if (Section.isBSS() || Section.isVirtual())
    return NEVERC_OBJECT_SECTION_KIND_ZERO_FILL;
  if (Section.isData() || Section.isBerkeleyData())
    return NEVERC_OBJECT_SECTION_KIND_DATA;
  if (Section.isBerkeleyText())
    return NEVERC_OBJECT_SECTION_KIND_READ_ONLY_DATA;
  return NEVERC_OBJECT_SECTION_KIND_FORMAT_EXTENSION;
}

NevercObjectSectionFlags
sectionFlags(SectionRef Section, NevercObjectSectionKind Kind) {
  NevercObjectSectionFlags Flags = 0;
  if (Section.isBerkeleyText() || Section.isBerkeleyData() ||
      Section.isText() || Section.isData() || Section.isBSS())
    Flags |= NEVERC_OBJECT_SECTION_ALLOCATED;
  if (Section.isText())
    Flags |= NEVERC_OBJECT_SECTION_EXECUTABLE;
  if (Section.isData() || Section.isBSS() ||
      Kind == NEVERC_OBJECT_SECTION_KIND_TLS_DATA ||
      Kind == NEVERC_OBJECT_SECTION_KIND_TLS_ZERO_FILL)
    Flags |= NEVERC_OBJECT_SECTION_WRITABLE;
  if (Kind == NEVERC_OBJECT_SECTION_KIND_TLS_DATA ||
      Kind == NEVERC_OBJECT_SECTION_KIND_TLS_ZERO_FILL)
    Flags |= NEVERC_OBJECT_SECTION_TLS;
  if (Kind == NEVERC_OBJECT_SECTION_KIND_DEBUG)
    Flags |= NEVERC_OBJECT_SECTION_DEBUG;
  if (Kind == NEVERC_OBJECT_SECTION_KIND_UNWIND)
    Flags |= NEVERC_OBJECT_SECTION_UNWIND;
  return Flags;
}

NevercObjectSymbolType symbolType(SymbolRef::Type Type) {
  switch (Type) {
  case SymbolRef::ST_Data:
    return NEVERC_OBJECT_SYMBOL_TYPE_OBJECT;
  case SymbolRef::ST_Function:
    return NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
  case SymbolRef::ST_File:
    return NEVERC_OBJECT_SYMBOL_TYPE_FILE;
  case SymbolRef::ST_Debug:
  case SymbolRef::ST_Other:
  case SymbolRef::ST_Unknown:
    return NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE;
  }
  return NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE;
}

bool isSyntheticAssemblerSymbol(const ObjectFile &Object, StringRef Name,
                                uint32_t NativeFlags) {
  // Section, file and stab symbols encode the container rather than program
  // content, and the assembler recreates them from the directives the writer
  // emits.  Keeping them would re-declare names the assembler already owns --
  // an ELF section symbol collides with its own `.section` directive.
  if ((NativeFlags & SymbolRef::SF_FormatSpecific) != 0)
    return true;
  if ((NativeFlags & (SymbolRef::SF_Global | SymbolRef::SF_Weak)) != 0)
    return false;
  if (isa<MachOObjectFile>(Object) && Name.starts_with("ltmp")) {
    unsigned TemporaryIndex = 0;
    if (!Name.drop_front(4).getAsInteger(10, TemporaryIndex))
      return true;
  }
  if (isa<ELFObjectFileBase>(Object) &&
      Object.getArch() == Triple::aarch64 && Name.size() >= 2 &&
      Name.front() == '$' &&
      (Name[1] == 'x' || Name[1] == 'd' || Name[1] == 'a' ||
       Name[1] == 't') &&
      (Name.size() == 2 || Name[2] == '.'))
    return true;
  return false;
}

uint32_t relocationWidth(const ObjectFile &Object,
                         RelocationRef Relocation, StringRef TypeName,
                         uint32_t PointerWidth) {
  if (const auto *MachObject = dyn_cast<MachOObjectFile>(&Object)) {
    const unsigned Length =
        MachObject->getRelocationLength(Relocation.getRawDataRefImpl());
    if (Length <= 4)
      return (UINT32_C(1) << Length) * 8;
  }
  // AArch64 ELF relocation names embed operand sizes that are not the field
  // width: the architecture token "AARCH64" itself contains "64", and
  // instruction-form relocations (ADR/ADD/LDST64/MOVW/CALL26/JUMP26/...) patch a
  // fixed 32-bit instruction regardless of the size named in the mnemonic.  The
  // name-based heuristic below would overestimate these as 64-bit and then
  // spuriously reject a relocation that sits within 8 bytes of a section's end
  // (e.g. a tail-call branch emitted by LTO).  Derive the width from the
  // relocation type number instead; only ABS/PREL data relocations carry a raw
  // field wider than the instruction.
  if (const auto *ELFObject = dyn_cast<ELFObjectFileBase>(&Object)) {
    if (ELFObject->getEMachine() == ELF::EM_AARCH64) {
      switch (Relocation.getType()) {
      case ELF::R_AARCH64_ABS64:
      case ELF::R_AARCH64_PREL64:
        return 64;
      case ELF::R_AARCH64_ABS16:
      case ELF::R_AARCH64_PREL16:
        return 16;
      default:
        // ABS32/PREL32 and every instruction-form relocation are 32 bits wide.
        return 32;
      }
    }
  }
  const auto Lower = TypeName.lower();
  if (Lower.find("128") != std::string::npos)
    return 128;
  if (Lower.find("64") != std::string::npos)
    return 64;
  if (Lower.find("32") != std::string::npos ||
      Lower.find("26") != std::string::npos ||
      Lower.find("24") != std::string::npos)
    return 32;
  if (Lower.find("16") != std::string::npos)
    return 16;
  if (Lower.find("8") != std::string::npos)
    return 8;
  return PointerWidth >= 8 && PointerWidth <= 128 &&
                 (PointerWidth % 8) == 0
             ? PointerWidth
             : 32;
}

bool isPCRelative(const ObjectFile &Object, RelocationRef Relocation,
                  StringRef TypeName) {
  if (const auto *MachObject = dyn_cast<MachOObjectFile>(&Object)) {
    MachO::any_relocation_info Native =
        MachObject->getRelocation(Relocation.getRawDataRefImpl());
    return MachObject->getAnyRelocationPCRel(Native) != 0;
  }
  const auto Lower = TypeName.lower();
  return Lower.find("pcrel") != std::string::npos ||
         Lower.find("pc") != std::string::npos ||
         Lower.find("rel32") != std::string::npos ||
         Lower.find("branch") != std::string::npos ||
         Lower.find("call") != std::string::npos ||
         Lower.find("page") != std::string::npos;
}

NevercObjectRelocationKind relocationKind(StringRef TypeName,
                                          bool PCRelative) {
  const auto Lower = TypeName.lower();
  if (Lower.find("tls") != std::string::npos ||
      Lower.find("tpoff") != std::string::npos ||
      Lower.find("dtp") != std::string::npos)
    return NEVERC_OBJECT_RELOCATION_TLS;
  if (Lower.find("got") != std::string::npos)
    return NEVERC_OBJECT_RELOCATION_GOT_RELATIVE;
  if (Lower.find("plt") != std::string::npos)
    return NEVERC_OBJECT_RELOCATION_PLT_RELATIVE;
  if (Lower.find("secrel") != std::string::npos ||
      Lower.find("section") != std::string::npos)
    return NEVERC_OBJECT_RELOCATION_SECTION_RELATIVE;
  if (Lower.find("addr32nb") != std::string::npos)
    return NEVERC_OBJECT_RELOCATION_IMAGE_RELATIVE;
  if (PCRelative)
    return NEVERC_OBJECT_RELOCATION_PC_RELATIVE;
  return NEVERC_OBJECT_RELOCATION_ABSOLUTE;
}

struct SectionMapEntry {
  SectionRef Section;
  NevercObjectSectionHandle Handle{};
  NevercObjectSectionKind Kind = NEVERC_OBJECT_SECTION_KIND_FORMAT_EXTENSION;
  uint64_t Size = 0;
  uint64_t Address = 0;
  NevercObjectComdatHandle Comdat{};
};

struct SymbolMapEntry {
  SymbolRef Symbol;
  NevercObjectSymbolHandle Handle{};
};

struct ComdatMapEntry {
  uint64_t SectionIndex = 0;
  NevercObjectComdatHandle Handle{};
};

NevercObjectComdatSelection
coffComdatSelection(uint8_t Selection) {
  switch (Selection) {
  case COFF::IMAGE_COMDAT_SELECT_NODUPLICATES:
    return NEVERC_OBJECT_COMDAT_NO_DUPLICATES;
  case COFF::IMAGE_COMDAT_SELECT_ANY:
    return NEVERC_OBJECT_COMDAT_ANY;
  case COFF::IMAGE_COMDAT_SELECT_SAME_SIZE:
    return NEVERC_OBJECT_COMDAT_SAME_SIZE;
  case COFF::IMAGE_COMDAT_SELECT_EXACT_MATCH:
    return NEVERC_OBJECT_COMDAT_EXACT_MATCH;
  case COFF::IMAGE_COMDAT_SELECT_LARGEST:
    return NEVERC_OBJECT_COMDAT_LARGEST;
  case COFF::IMAGE_COMDAT_SELECT_ASSOCIATIVE:
    return NEVERC_OBJECT_COMDAT_ASSOCIATIVE;
  default:
    return 0;
  }
}

template <class ELFT>
NevercStatus createELFComdats(
    const ELFObjectFile<ELFT> &Object,
    const NevercObjectReadRequest &Request,
    std::vector<ComdatMapEntry> &Comdats) {
  const ELFFile<ELFT> &File = Object.getELFFile();
  auto Sections = File.sections();
  if (!Sections) {
    consumeError(Sections.takeError());
    return status(NEVERC_STATUS_VERIFICATION_FAILED, 50);
  }
  for (const typename ELFT::Shdr &Section : *Sections) {
    if (Section.sh_type != ELF::SHT_GROUP)
      continue;
    auto Words =
        File.template getSectionContentsAsArray<typename ELFT::Word>(
            Section);
    if (!Words) {
      consumeError(Words.takeError());
      return status(NEVERC_STATUS_VERIFICATION_FAILED, 51);
    }
    if (Words->empty() ||
        (static_cast<uint32_t>(Words->front()) & ELF::GRP_COMDAT) == 0)
      continue;
    auto SymbolTable = File.getSection(Section.sh_link);
    if (!SymbolTable) {
      consumeError(SymbolTable.takeError());
      return status(NEVERC_STATUS_VERIFICATION_FAILED, 52);
    }
    auto Symbols = File.symbols(*SymbolTable);
    auto StringTable = File.getStringTableForSymtab(**SymbolTable);
    if (!Symbols || !StringTable ||
        Section.sh_info >= Symbols->size()) {
      if (!Symbols)
        consumeError(Symbols.takeError());
      if (!StringTable)
        consumeError(StringTable.takeError());
      return status(NEVERC_STATUS_VERIFICATION_FAILED, 53);
    }
    auto Name = (*Symbols)[Section.sh_info].getName(*StringTable);
    if (!Name || Name->empty()) {
      if (!Name)
        consumeError(Name.takeError());
      return status(NEVERC_STATUS_VERIFICATION_FAILED, 54);
    }

    NevercObjectComdatDescriptor Descriptor{};
    Descriptor.Header = {sizeof(Descriptor), NEVERC_OBJECT_API_MAJOR,
                         NEVERC_OBJECT_API_MINOR, 0};
    Descriptor.Name = stringView(*Name);
    Descriptor.Selection = NEVERC_OBJECT_COMDAT_ANY;
    NevercObjectComdatHandle Handle{};
    NevercStatus Result = Request.Object->CreateComdat(
        Request.Object->Context, Request.Task, Request.Mutation,
        &Descriptor, &Handle);
    if (!neverc_status_is_ok(Result))
      return Result;
    for (typename ELFT::Word Member : Words->drop_front())
      Comdats.push_back(
          {static_cast<uint64_t>(Member), Handle});
  }
  return neverc_status_ok();
}

NevercStatus createCOFFComdats(
    const COFFObjectFile &Object,
    const NevercObjectReadRequest &Request,
    std::vector<ComdatMapEntry> &Comdats) {
  for (SectionRef Section : Object.sections()) {
    const coff_section *Native = Object.getCOFFSection(Section);
    if (!Native ||
        (Native->Characteristics & COFF::IMAGE_SCN_LNK_COMDAT) == 0)
      continue;
    const int32_t SectionNumber =
        static_cast<int32_t>(Section.getIndex() + 1);
    NevercObjectComdatSelection Selection = NEVERC_OBJECT_COMDAT_ANY;
    std::string Name;
    for (SymbolRef Symbol : Object.symbols()) {
      COFFSymbolRef NativeSymbol = Object.getCOFFSymbol(Symbol);
      if (NativeSymbol.getSectionNumber() != SectionNumber)
        continue;
      if (const coff_aux_section_definition *Definition =
              NativeSymbol.getSectionDefinition()) {
        const NevercObjectComdatSelection Mapped =
            coffComdatSelection(Definition->Selection);
        if (Mapped != 0)
          Selection = Mapped;
      }
      if (NativeSymbol.isExternal()) {
        auto SymbolName = Symbol.getName();
        if (!SymbolName) {
          consumeError(SymbolName.takeError());
          return status(NEVERC_STATUS_VERIFICATION_FAILED, 55);
        }
        if (!SymbolName->empty())
          Name = SymbolName->str();
      }
    }
    if (Name.empty()) {
      auto SectionName = Section.getName();
      if (!SectionName) {
        consumeError(SectionName.takeError());
        return status(NEVERC_STATUS_VERIFICATION_FAILED, 56);
      }
      Name = SectionName->str();
    }
    NevercObjectComdatDescriptor Descriptor{};
    Descriptor.Header = {sizeof(Descriptor), NEVERC_OBJECT_API_MAJOR,
                         NEVERC_OBJECT_API_MINOR, 0};
    Descriptor.Name = stringView(Name);
    Descriptor.Selection = Selection;
    NevercObjectComdatHandle Handle{};
    NevercStatus Result = Request.Object->CreateComdat(
        Request.Object->Context, Request.Task, Request.Mutation,
        &Descriptor, &Handle);
    if (!neverc_status_is_ok(Result))
      return Result;
    Comdats.push_back({Section.getIndex(), Handle});
  }
  return neverc_status_ok();
}

NevercStatus createComdats(
    const ObjectFile &Object, const NevercObjectReadRequest &Request,
    std::vector<ComdatMapEntry> &Comdats) {
  if (const auto *ELF = dyn_cast<ELF32LEObjectFile>(&Object))
    return createELFComdats(*ELF, Request, Comdats);
  if (const auto *ELF = dyn_cast<ELF64LEObjectFile>(&Object))
    return createELFComdats(*ELF, Request, Comdats);
  if (const auto *ELF = dyn_cast<ELF32BEObjectFile>(&Object))
    return createELFComdats(*ELF, Request, Comdats);
  if (const auto *ELF = dyn_cast<ELF64BEObjectFile>(&Object))
    return createELFComdats(*ELF, Request, Comdats);
  if (const auto *COFF = dyn_cast<COFFObjectFile>(&Object))
    return createCOFFComdats(*COFF, Request, Comdats);
  return neverc_status_ok();
}

bool isLogicalObjectSection(const ObjectFile &Object,
                            SectionRef Section) {
  if (!isa<ELFObjectFileBase>(Object))
    return true;
  switch (ELFSectionRef(Section).getType()) {
  case ELF::SHT_NULL:
  case ELF::SHT_SYMTAB:
  case ELF::SHT_DYNSYM:
  case ELF::SHT_STRTAB:
  case ELF::SHT_REL:
  case ELF::SHT_RELA:
  case ELF::SHT_SYMTAB_SHNDX:
  case ELF::SHT_GROUP:
    return false;
  default:
    return true;
  }
}

SectionMapEntry *findSection(std::vector<SectionMapEntry> &Sections,
                             SectionRef Section) {
  auto It = std::find_if(
      Sections.begin(), Sections.end(),
      [&](const SectionMapEntry &Entry) {
        return Entry.Section == Section;
      });
  return It == Sections.end() ? nullptr : &*It;
}

// A relocation may name a symbol that collectSymbols() dropped as a container
// artifact.  Those symbols label the start of a section, so the reference is
// really section-relative and the graph can say so directly.
SectionMapEntry *sectionForDroppedSymbol(
    const ObjectFile &Object, SymbolRef Symbol,
    std::vector<SectionMapEntry> &Sections) {
  Expected<uint32_t> Flags = Symbol.getFlags();
  if (!Flags) {
    consumeError(Flags.takeError());
    return nullptr;
  }
  if ((*Flags & SymbolRef::SF_FormatSpecific) == 0)
    return nullptr;
  Expected<section_iterator> Section = Symbol.getSection();
  if (!Section) {
    consumeError(Section.takeError());
    return nullptr;
  }
  if (*Section == Object.section_end())
    return nullptr;
  return findSection(Sections, **Section);
}

const SymbolMapEntry *findSymbol(
    const std::vector<SymbolMapEntry> &Symbols, SymbolRef Symbol) {
  auto It = std::find_if(
      Symbols.begin(), Symbols.end(),
      [&](const SymbolMapEntry &Entry) {
        return Entry.Symbol == Symbol;
      });
  return It == Symbols.end() ? nullptr : &*It;
}

NevercStatus createSections(const ObjectFile &Object,
                            const NevercObjectReadRequest &Request,
                            ArrayRef<ComdatMapEntry> Comdats,
                            std::vector<SectionMapEntry> &Sections) {
  uint64_t AnonymousIndex = 0;
  for (SectionRef Section : Object.sections()) {
    if (!isLogicalObjectSection(Object, Section))
      continue;
    Expected<StringRef> NameOrError = Section.getName();
    if (!NameOrError) {
      consumeError(NameOrError.takeError());
      return status(NEVERC_STATUS_VERIFICATION_FAILED, 101);
    }
    std::string GeneratedName;
    StringRef Name = *NameOrError;
    if (Name.empty()) {
      GeneratedName = "$section." + std::to_string(++AnonymousIndex);
      Name = GeneratedName;
    }

    const bool IsTLS = isTLSSection(Object, Section, Name);
    const NevercObjectSectionKind Kind =
        sectionKind(Object, Section, Name, IsTLS);
    SmallVector<uint8_t, 48> Extension;
    nativeSectionExtension(Object, Section, Extension);

    NevercObjectSectionDescriptor Descriptor{};
    Descriptor.Header = {sizeof(Descriptor), NEVERC_OBJECT_API_MAJOR,
                         NEVERC_OBJECT_API_MINOR, 0};
    Descriptor.Name = stringView(Name);
    Descriptor.Kind = Kind;
    Descriptor.Flags = sectionFlags(Section, Kind);
    Descriptor.Alignment =
        std::max<uint64_t>(UINT64_C(1), Section.getAlignment().value());
    const uint64_t Size = Section.getSize();
    if (Kind == NEVERC_OBJECT_SECTION_KIND_ZERO_FILL ||
        Kind == NEVERC_OBJECT_SECTION_KIND_TLS_ZERO_FILL) {
      Descriptor.ZeroFillSize = Size;
    } else {
      Expected<StringRef> ContentsOrError = Section.getContents();
      if (!ContentsOrError) {
        consumeError(ContentsOrError.takeError());
        return status(NEVERC_STATUS_VERIFICATION_FAILED, 102);
      }
      if (ContentsOrError->size() != Size)
        return status(NEVERC_STATUS_VERIFICATION_FAILED, 103);
      Descriptor.Data = {
          reinterpret_cast<const uint8_t *>(ContentsOrError->data()),
          static_cast<uint64_t>(ContentsOrError->size())};
    }
    Descriptor.ExtensionOwner = Request.Target.ObjectFormatID;
    Descriptor.ExtensionVersion = 1;
    Descriptor.Extension = byteView(Extension);
    auto Comdat = llvm::find_if(
        Comdats, [&](const ComdatMapEntry &Entry) {
          return Entry.SectionIndex == Section.getIndex();
        });
    if (Comdat != Comdats.end())
      Descriptor.Comdat = Comdat->Handle;

    NevercObjectSectionHandle Handle{};
    NevercStatus Result = Request.Object->CreateSection(
        Request.Object->Context, Request.Task, Request.Mutation,
        &Descriptor, &Handle);
    if (!neverc_status_is_ok(Result)) {
      if (Result.Detail == 0)
        Result.Detail = 110 + Sections.size();
      return Result;
    }
    Sections.push_back({Section, Handle, Kind, Size,
                        Section.getAddress(), Descriptor.Comdat});
  }
  return neverc_status_ok();
}

NevercStatus createSymbols(const ObjectFile &Object,
                           const NevercObjectReadRequest &Request,
                           std::vector<SectionMapEntry> &Sections,
                           std::vector<SymbolMapEntry> &Symbols) {
  uint64_t AnonymousIndex = 0;
  for (SymbolRef Symbol : Object.symbols()) {
    Expected<StringRef> NameOrError = Symbol.getName();
    Expected<uint32_t> FlagsOrError = Symbol.getFlags();
    Expected<SymbolRef::Type> TypeOrError = Symbol.getType();
    Expected<uint64_t> ValueOrError = Symbol.getValue();
    Expected<section_iterator> SectionOrError = Symbol.getSection();
    if (!NameOrError || !FlagsOrError || !TypeOrError || !ValueOrError ||
        !SectionOrError) {
      if (!NameOrError)
        consumeError(NameOrError.takeError());
      if (!FlagsOrError)
        consumeError(FlagsOrError.takeError());
      if (!TypeOrError)
        consumeError(TypeOrError.takeError());
      if (!ValueOrError)
        consumeError(ValueOrError.takeError());
      if (!SectionOrError)
        consumeError(SectionOrError.takeError());
      return status(NEVERC_STATUS_VERIFICATION_FAILED, 201);
    }

    const uint32_t NativeFlags = *FlagsOrError;
    std::string GeneratedName;
    StringRef Name = *NameOrError;
    if (Name.empty()) {
      GeneratedName = "$symbol." + std::to_string(++AnonymousIndex);
      Name = GeneratedName;
    }
    if (isSyntheticAssemblerSymbol(Object, Name, NativeFlags))
      continue;

    NevercObjectSymbolDescriptor Descriptor{};
    Descriptor.Header = {sizeof(Descriptor), NEVERC_OBJECT_API_MAJOR,
                         NEVERC_OBJECT_API_MINOR, 0};
    Descriptor.Name = stringView(Name);
    Descriptor.Binding =
        (NativeFlags & SymbolRef::SF_Weak)
            ? NEVERC_OBJECT_SYMBOL_BINDING_WEAK
            : ((NativeFlags & SymbolRef::SF_Global)
                   ? NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL
                   : NEVERC_OBJECT_SYMBOL_BINDING_LOCAL);
    Descriptor.Visibility =
        (NativeFlags & SymbolRef::SF_Hidden)
            ? NEVERC_OBJECT_SYMBOL_VISIBILITY_HIDDEN
            : NEVERC_OBJECT_SYMBOL_VISIBILITY_DEFAULT;
    Descriptor.Type = symbolType(*TypeOrError);
    Descriptor.Alignment =
        std::max<uint64_t>(UINT64_C(1), Symbol.getAlignment());

    SectionMapEntry *MappedSection = nullptr;
    if (*SectionOrError != Object.section_end())
      MappedSection = findSection(Sections, **SectionOrError);
    if ((NativeFlags & SymbolRef::SF_Undefined) != 0) {
      Descriptor.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED;
      Descriptor.Flags |= NEVERC_OBJECT_SYMBOL_IMPORTED;
    } else if ((NativeFlags & SymbolRef::SF_Common) != 0) {
      Descriptor.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_COMMON;
      Descriptor.Size = Symbol.getCommonSize();
    } else if ((NativeFlags & SymbolRef::SF_Absolute) != 0 ||
               !MappedSection) {
      Descriptor.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_ABSOLUTE;
      Descriptor.Value = *ValueOrError;
    } else {
      Descriptor.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
      Descriptor.Section = MappedSection->Handle;
      Descriptor.Comdat = MappedSection->Comdat;
      Descriptor.Value =
          *ValueOrError >= MappedSection->Address
              ? *ValueOrError - MappedSection->Address
              : *ValueOrError;
      if (isa<ELFObjectFileBase>(Object))
        Descriptor.Size = ELFSymbolRef(Symbol).getSize();
      if (Descriptor.Value > MappedSection->Size ||
          Descriptor.Size > MappedSection->Size - Descriptor.Value)
        return status(NEVERC_STATUS_VERIFICATION_FAILED, 202);
      if (MappedSection->Kind == NEVERC_OBJECT_SECTION_KIND_TLS_DATA ||
          MappedSection->Kind ==
              NEVERC_OBJECT_SECTION_KIND_TLS_ZERO_FILL)
        Descriptor.Type = NEVERC_OBJECT_SYMBOL_TYPE_TLS;
    }
    if ((NativeFlags & SymbolRef::SF_Exported) != 0 &&
        Descriptor.Definition != NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED)
      Descriptor.Flags |= NEVERC_OBJECT_SYMBOL_EXPORTED;

    SmallVector<uint8_t, 48> Extension;
    nativeSymbolExtension(Object, Symbol, Extension);
    Descriptor.ExtensionOwner = Request.Target.ObjectFormatID;
    Descriptor.ExtensionVersion = 1;
    Descriptor.Extension = byteView(Extension);

    NevercObjectSymbolHandle Handle{};
    NevercStatus Result = Request.Object->CreateSymbol(
        Request.Object->Context, Request.Task, Request.Mutation,
        &Descriptor, &Handle);
    if (!neverc_status_is_ok(Result)) {
      if (Result.Detail == 0)
        Result.Detail = 210 + Symbols.size();
      return Result;
    }
    Symbols.push_back({Symbol, Handle});
  }
  return neverc_status_ok();
}

NevercStatus createRelocations(
    const ObjectFile &Object, const NevercObjectReadRequest &Request,
    std::vector<SectionMapEntry> &Sections,
    const std::vector<SymbolMapEntry> &Symbols) {
  for (SectionRef RelocationSection : Object.sections()) {
    SectionMapEntry *MappedSection =
        findSection(Sections, RelocationSection);
    Expected<section_iterator> RelocatedSection =
        RelocationSection.getRelocatedSection();
    if (!RelocatedSection) {
      consumeError(RelocatedSection.takeError());
      return status(NEVERC_STATUS_VERIFICATION_FAILED, 300);
    }
    if (*RelocatedSection != Object.section_end()) {
      MappedSection = findSection(Sections, **RelocatedSection);
      if (!MappedSection)
        return status(NEVERC_STATUS_VERIFICATION_FAILED, 302);
    }
    for (RelocationRef Relocation : RelocationSection.relocations()) {
      if (!MappedSection)
        return status(NEVERC_STATUS_VERIFICATION_FAILED, 302);
      SmallString<64> TypeStorage;
      Relocation.getTypeName(TypeStorage);
      StringRef TypeName = TypeStorage;
      const uint32_t Width = relocationWidth(
          Object, Relocation, TypeName, Request.Target.PointerWidth);
      const bool PCRelative =
          isPCRelative(Object, Relocation, TypeName);
      if (Width == 0 || Width > 128 || (Width % 8) != 0)
        return status(NEVERC_STATUS_VERIFICATION_FAILED, 301);
      if (Relocation.getOffset() > MappedSection->Size)
        return status(NEVERC_STATUS_VERIFICATION_FAILED, 303);
      if (Width / 8 > MappedSection->Size - Relocation.getOffset())
        return status(NEVERC_STATUS_VERIFICATION_FAILED, 304);

      NevercObjectRelocationDescriptor Descriptor{};
      Descriptor.Header = {sizeof(Descriptor), NEVERC_OBJECT_API_MAJOR,
                           NEVERC_OBJECT_API_MINOR, 0};
      Descriptor.Section = MappedSection->Handle;
      Descriptor.Offset = Relocation.getOffset();
      Descriptor.Kind = relocationKind(TypeName, PCRelative);
      Descriptor.Width = Width;
      Descriptor.IsPCRelative =
          PCRelative ? NEVERC_TRUE : NEVERC_FALSE;
      Descriptor.IsSigned =
          PCRelative ? NEVERC_TRUE : NEVERC_FALSE;
      if (isa<ELFObjectFileBase>(Object)) {
        Expected<int64_t> Addend =
            ELFRelocationRef(Relocation).getAddend();
        if (Addend)
          Descriptor.Addend = *Addend;
        else
          consumeError(Addend.takeError());
      }

      SmallVector<uint8_t, 80> Extension;
      nativeRelocationExtension(Relocation, TypeName, Extension);
      Descriptor.ExtensionOwner = Request.Target.ObjectFormatID;
      Descriptor.ExtensionVersion = 1;
      Descriptor.Extension = byteView(Extension);

      symbol_iterator Symbol = Relocation.getSymbol();
      if (Symbol != Object.symbol_end()) {
        const SymbolMapEntry *MappedSymbol =
            findSymbol(Symbols, *Symbol);
        if (MappedSymbol) {
          Descriptor.TargetKind =
              NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
          Descriptor.TargetSymbol = MappedSymbol->Handle;
        } else if (const SectionMapEntry *DroppedTarget =
                       sectionForDroppedSymbol(Object, *Symbol, Sections)) {
          Descriptor.TargetKind =
              NEVERC_OBJECT_RELOCATION_TARGET_SECTION;
          Descriptor.TargetSection = DroppedTarget->Handle;
        } else {
          return status(NEVERC_STATUS_VERIFICATION_FAILED, 302);
        }
      } else if (const auto *MachObject =
                     dyn_cast<MachOObjectFile>(&Object)) {
        MachO::any_relocation_info Native =
            MachObject->getRelocation(Relocation.getRawDataRefImpl());
        SectionRef TargetSection =
            MachObject->getAnyRelocationSection(Native);
        SectionMapEntry *MappedTarget =
            findSection(Sections, TargetSection);
        if (MappedTarget) {
          Descriptor.TargetKind =
              NEVERC_OBJECT_RELOCATION_TARGET_SECTION;
          Descriptor.TargetSection = MappedTarget->Handle;
        } else {
          Descriptor.TargetKind =
              NEVERC_OBJECT_RELOCATION_TARGET_FORMAT_EXTENSION;
          Descriptor.TargetExtensionKind =
              static_cast<uint32_t>(Relocation.getType()) + 1;
        }
      } else {
        Descriptor.TargetKind =
            NEVERC_OBJECT_RELOCATION_TARGET_FORMAT_EXTENSION;
        Descriptor.TargetExtensionKind =
            static_cast<uint32_t>(Relocation.getType()) + 1;
      }

      NevercObjectRelocationHandle Handle{};
      NevercStatus Result = Request.Object->CreateRelocation(
          Request.Object->Context, Request.Task, Request.Mutation,
          &Descriptor, &Handle);
      if (!neverc_status_is_ok(Result)) {
        if (Result.Detail == 0)
          Result.Detail = 310;
        return Result;
      }
    }
  }
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL probeBuiltinObject(
    void *UserData, const NevercObjectProbeRequest *Request,
    NevercObjectProbeResult *Result) {
  if (!UserData || !Request || !Result ||
      Request->Input.Length >
          static_cast<uint64_t>(std::numeric_limits<size_t>::max()) ||
      (!Request->Input.Data && Request->Input.Length != 0))
    return status(NEVERC_STATUS_INVALID_ARGUMENT);

  const auto *Context =
      static_cast<const BuiltinFormatContext *>(UserData);
  StringRef Bytes(
      reinterpret_cast<const char *>(Request->Input.Data),
      static_cast<size_t>(Request->Input.Length));
  const file_magic Magic = identify_magic(Bytes);
  Result->ConsumedMinimum =
      Context->Format == BuiltinObjectFormat::COFF ? 20 : 16;
  switch (Context->Format) {
  case BuiltinObjectFormat::ELF:
    switch (Magic) {
    case file_magic::elf_relocatable:
      Result->ArtifactKind = NEVERC_OBJECT_ARTIFACT_RELOCATABLE;
      break;
    case file_magic::elf_executable:
      Result->ArtifactKind = NEVERC_OBJECT_ARTIFACT_EXECUTABLE_IMAGE;
      break;
    case file_magic::elf_shared_object:
      Result->ArtifactKind = NEVERC_OBJECT_ARTIFACT_SHARED_IMAGE;
      break;
    default:
      return neverc_status_ok();
    }
    break;
  case BuiltinObjectFormat::COFF:
    switch (Magic) {
    case file_magic::coff_object:
      Result->ArtifactKind = NEVERC_OBJECT_ARTIFACT_RELOCATABLE;
      break;
    case file_magic::coff_import_library:
      Result->ArtifactKind = NEVERC_OBJECT_ARTIFACT_ARCHIVE;
      break;
    case file_magic::pecoff_executable:
      Result->ArtifactKind = NEVERC_OBJECT_ARTIFACT_EXECUTABLE_IMAGE;
      break;
    default:
      return neverc_status_ok();
    }
    break;
  case BuiltinObjectFormat::MachO:
    switch (Magic) {
    case file_magic::macho_object:
      Result->ArtifactKind = NEVERC_OBJECT_ARTIFACT_RELOCATABLE;
      break;
    case file_magic::macho_executable:
    case file_magic::macho_bundle:
    case file_magic::macho_kext_bundle:
      Result->ArtifactKind = NEVERC_OBJECT_ARTIFACT_EXECUTABLE_IMAGE;
      break;
    case file_magic::macho_dynamically_linked_shared_lib:
    case file_magic::macho_dynamically_linked_shared_lib_stub:
      Result->ArtifactKind = NEVERC_OBJECT_ARTIFACT_SHARED_IMAGE;
      break;
    case file_magic::macho_universal_binary:
      Result->ArtifactKind = NEVERC_OBJECT_ARTIFACT_UNIVERSAL_BINARY;
      break;
    default:
      return neverc_status_ok();
    }
    break;
  }
  Result->Confidence = NEVERC_OBJECT_PROBE_MAX_CONFIDENCE;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL readBuiltinObject(
    void *UserData, const NevercObjectReadRequest *Request) {
  if (!UserData || !Request || !Request->Object ||
      Request->Input.Length >
          static_cast<uint64_t>(std::numeric_limits<size_t>::max()) ||
      (!Request->Input.Data && Request->Input.Length != 0))
    return status(NEVERC_STATUS_INVALID_ARGUMENT);

  const auto *Context =
      static_cast<const BuiltinFormatContext *>(UserData);
  StringRef Bytes(
      reinterpret_cast<const char *>(Request->Input.Data),
      static_cast<size_t>(Request->Input.Length));
  MemoryBufferRef Buffer(Bytes, StringRef(
                                    Request->LogicalPath.Data
                                        ? Request->LogicalPath.Data
                                        : "",
                                    static_cast<size_t>(
                                        Request->LogicalPath.Length)));
  auto ObjectOrError = ObjectFile::createObjectFile(Buffer);
  if (!ObjectOrError) {
    consumeError(ObjectOrError.takeError());
    return status(NEVERC_STATUS_VERIFICATION_FAILED, 401);
  }
  ObjectFile &Object = **ObjectOrError;
  const bool CorrectFormat =
      (Context->Format == BuiltinObjectFormat::ELF &&
       isa<ELFObjectFileBase>(Object)) ||
      (Context->Format == BuiltinObjectFormat::COFF &&
       isa<COFFObjectFile>(Object)) ||
      (Context->Format == BuiltinObjectFormat::MachO &&
       isa<MachOObjectFile>(Object));
  if (!CorrectFormat)
    return status(NEVERC_STATUS_VERIFICATION_FAILED, 402);

  std::vector<ComdatMapEntry> Comdats;
  NevercStatus Result = createComdats(Object, *Request, Comdats);
  if (!neverc_status_is_ok(Result))
    return Result;
  std::vector<SectionMapEntry> Sections;
  std::vector<SymbolMapEntry> Symbols;
  Result = createSections(Object, *Request, Comdats, Sections);
  if (!neverc_status_is_ok(Result))
    return Result;
  Result = createSymbols(Object, *Request, Sections, Symbols);
  if (!neverc_status_is_ok(Result))
    return Result;
  return createRelocations(Object, *Request, Sections, Symbols);
}

const BuiltinFormatContext *
contextFor(BuiltinObjectFormat Format) {
  switch (Format) {
  case BuiltinObjectFormat::ELF:
    return &ELFContext;
  case BuiltinObjectFormat::COFF:
    return &COFFContext;
  case BuiltinObjectFormat::MachO:
    return &MachOContext;
  }
  llvm_unreachable("unknown built-in object Format");
}

StringRef canonicalName(BuiltinObjectFormat Format) {
  switch (Format) {
  case BuiltinObjectFormat::ELF:
    return "elf";
  case BuiltinObjectFormat::COFF:
    return "coff";
  case BuiltinObjectFormat::MachO:
    return "mach-o";
  }
  llvm_unreachable("unknown built-in object Format");
}

StringRef defaultExtension(BuiltinObjectFormat Format) {
  return Format == BuiltinObjectFormat::COFF ? ".obj" : ".o";
}

} // namespace

void appendBuiltinLLVMObjectFormats(
    std::vector<PluginTargetSnapshot::ObjectFormatRecord> &Formats) {
  const std::array<BuiltinObjectFormat, 3> Builtins = {
      BuiltinObjectFormat::ELF, BuiltinObjectFormat::COFF,
      BuiltinObjectFormat::MachO};
  for (BuiltinObjectFormat Kind : Builtins) {
    PluginTargetSnapshot::ObjectFormatRecord Format;
    Format.PluginID = "neverc.builtin.llvm-object";
    Format.CanonicalName = canonicalName(Kind).str();
    Format.DefaultExtension = defaultExtension(Kind).str();
    Format.Flags = NEVERC_OBJECT_FORMAT_CAN_PROBE |
                   NEVERC_OBJECT_FORMAT_CAN_READ |
                   NEVERC_OBJECT_FORMAT_CAN_WRITE;
    Format.Probe = probeBuiltinObject;
    Format.Reader = readBuiltinObject;
    Format.Writer = writeBuiltinLLVMObject;
    Format.CallbackUserData =
        const_cast<BuiltinFormatContext *>(contextFor(Kind));
    for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
      if (Route.ObjectFormat != Kind)
        continue;
      if (Format.ID.High == 0 && Format.ID.Low == 0)
        Format.ID = Route.ObjectFormatID;
      else
        assert(sameID(Format.ID, Route.ObjectFormatID) &&
               "built-in routes disagree on object Format ID");
      Format.SupportedTargets.push_back(Route.TargetID);
    }
    assert((Format.ID.High != 0 || Format.ID.Low != 0) &&
           "built-in object Format has no route");
    if (Kind == BuiltinObjectFormat::MachO)
      Format.Aliases = {"macho", "mach"};
    Formats.push_back(std::move(Format));
  }
}

} // namespace neverc::plugin
