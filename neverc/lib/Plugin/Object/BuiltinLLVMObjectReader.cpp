#include "BuiltinLLVMObjectWriter.h"
#include "neverc/Plugin/Host/BuiltinObjectExtension.h"
#include "neverc/Plugin/Host/BuiltinTargetProvider.h"
#include "neverc/Plugin/Host/NativeRelocationFacts.h"
#include "neverc/Plugin/Host/ObjectReaderProvider.h"
#include "neverc/Plugin/Host/ObjectSectionRole.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
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
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
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

// Each failure site has a stable number, with the index of the entity being
// processed in the low decimal digits. Adding an index straight onto a base
// would let distinct failures produce the same value once a graph held more
// than a few entities, which is every real translation unit.
enum DetailSite : uint64_t {
  DetailELFGroupSections = 1,
  DetailELFGroupContents,
  DetailELFGroupSymbolTable,
  DetailELFGroupSymbols,
  DetailELFGroupName,
  DetailCOFFComdatSymbolName,
  DetailCOFFComdatSectionName,
  DetailCOFFComdatAssociativeMissing,
  DetailCOFFComdatSelfAssociative,
  DetailCOFFComdatCycle,
  DetailSectionName,
  DetailSectionContents,
  DetailSectionSizeMismatch,
  DetailSectionCreate,
  DetailSymbolQuery,
  DetailSymbolOutsideSection,
  DetailSymbolCreate,
  DetailRelocatedSectionQuery,
  DetailRelocationSectionUnmapped,
  DetailRelocationUnsupportedTarget,
  DetailRelocationWidthInvalid,
  DetailRelocationOutsideSection,
  DetailRelocationTargetUnmapped,
  DetailRelocationCreate,
  DetailObjectParse,
  DetailObjectFormatMismatch,
  DetailObjectArchMismatch,
};

constexpr uint64_t DetailIndexScale = 1000000;

uint64_t detailAt(DetailSite Site, size_t Index) {
  return static_cast<uint64_t>(Site) * DetailIndexScale +
         std::min<uint64_t>(Index, DetailIndexScale - 1);
}

uint64_t detail(DetailSite Site) { return detailAt(Site, 0); }

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
  // COFF section headers carry no thread-local bit, so the name is all there
  // is to go on.
  return isThreadLocalSectionName(BuiltinObjectFormat::COFF, Name);
}

// ELFSectionRef exposes sh_flags and sh_offset but not sh_entsize, and a
// mergeable section cannot be re-emitted without its entry size, so reach the
// section header through the concrete ELF type.
template <typename ELFT>
bool elfEntrySizeFor(const ObjectFile &Object, SectionRef Section,
                     uint64_t &EntrySize) {
  const auto *Typed = dyn_cast<ELFObjectFile<ELFT>>(&Object);
  if (Typed == nullptr)
    return false;
  EntrySize = Typed->getSection(Section.getRawDataRefImpl())->sh_entsize;
  return true;
}

uint64_t elfEntrySize(const ObjectFile &Object, SectionRef Section) {
  uint64_t EntrySize = 0;
  if (elfEntrySizeFor<ELF64LE>(Object, Section, EntrySize) ||
      elfEntrySizeFor<ELF32LE>(Object, Section, EntrySize) ||
      elfEntrySizeFor<ELF64BE>(Object, Section, EntrySize) ||
      elfEntrySizeFor<ELF32BE>(Object, Section, EntrySize))
    return EntrySize;
  return 0;
}

void nativeSectionExtension(const ObjectFile &Object, SectionRef Section,
                            SmallVectorImpl<uint8_t> &Bytes) {
  using namespace builtinext;
  appendHeader(Bytes, SectionTag, SectionVersion);
  appendU64(Bytes, Section.getIndex());
  appendU64(Bytes, Section.getAddress());
  uint64_t Type = 0;
  uint64_t Flags = 0;
  uint64_t Offset = 0;
  uint64_t EntrySize = 0;
  if (isa<ELFObjectFileBase>(Object)) {
    ELFSectionRef ELFSection(Section);
    Type = ELFSection.getType();
    Flags = ELFSection.getFlags();
    Offset = ELFSection.getOffset();
    EntrySize = elfEntrySize(Object, Section);
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
  appendU64(Bytes, EntrySize);
}

void nativeSymbolExtension(const ObjectFile &Object, SymbolRef Symbol,
                           SmallVectorImpl<uint8_t> &Bytes) {
  using namespace builtinext;
  appendHeader(Bytes, SymbolTag, SymbolVersion);
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
  using namespace builtinext;
  appendHeader(Bytes, RelocationTag, RelocationVersion);
  appendU64(Bytes, Relocation.getType());
  appendU32(Bytes, static_cast<uint32_t>(TypeName.size()));
  appendBytes(Bytes, TypeName);
}

NevercObjectSectionKind
sectionKind(BuiltinObjectFormat Format, const ObjectFile &Object,
            SectionRef Section, StringRef Name, bool IsTLS) {
  if (Section.isDebugSection() || isDebugSectionName(Format, Name))
    return NEVERC_OBJECT_SECTION_KIND_DEBUG;
  if (isUnwindSectionName(Format, Name))
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

// ELF and COFF state a section's attributes in its header, so read them instead
// of inferring them. llvm::object's generic isData() is also true for read-only
// allocated sections such as .eh_frame and .rdata; calling those writable makes
// the writer re-emit them with flags the assembler then rejects as a change.
// Mach-O carries protection on the segment rather than the section, so it has
// nothing to read here and keeps the inferred answer.
bool nativeSectionFlags(const ObjectFile &Object, SectionRef Section,
                        NevercObjectSectionFlags &Flags) {
  if (isa<ELFObjectFileBase>(Object)) {
    const uint64_t Native = ELFSectionRef(Section).getFlags();
    if ((Native & ELF::SHF_ALLOC) != 0)
      Flags |= NEVERC_OBJECT_SECTION_ALLOCATED;
    if ((Native & ELF::SHF_WRITE) != 0)
      Flags |= NEVERC_OBJECT_SECTION_WRITABLE;
    if ((Native & ELF::SHF_EXECINSTR) != 0)
      Flags |= NEVERC_OBJECT_SECTION_EXECUTABLE;
    // Mergeability is part of a section's identity, not a hint: dropping it
    // turns .rodata.cstN back into a plain section and the assembler rejects
    // the changed flags and entry size.
    if ((Native & ELF::SHF_MERGE) != 0)
      Flags |= NEVERC_OBJECT_SECTION_MERGEABLE;
    if ((Native & ELF::SHF_STRINGS) != 0)
      Flags |= NEVERC_OBJECT_SECTION_STRINGS;
    return true;
  }
  if (const auto *COFFObject = dyn_cast<COFFObjectFile>(&Object)) {
    const coff_section *Native = COFFObject->getCOFFSection(Section);
    if (Native == nullptr)
      return false;
    const uint32_t Characteristics = Native->Characteristics;
    if ((Characteristics & COFF::IMAGE_SCN_MEM_READ) != 0)
      Flags |= NEVERC_OBJECT_SECTION_ALLOCATED;
    if ((Characteristics & COFF::IMAGE_SCN_MEM_WRITE) != 0)
      Flags |= NEVERC_OBJECT_SECTION_WRITABLE;
    if ((Characteristics & COFF::IMAGE_SCN_MEM_EXECUTE) != 0)
      Flags |= NEVERC_OBJECT_SECTION_EXECUTABLE;
    return true;
  }
  return false;
}

NevercObjectSectionFlags
sectionFlags(const ObjectFile &Object, SectionRef Section,
             NevercObjectSectionKind Kind) {
  NevercObjectSectionFlags Flags = 0;
  if (!nativeSectionFlags(Object, Section, Flags)) {
    if (Section.isBerkeleyText() || Section.isBerkeleyData() ||
        Section.isText() || Section.isData() || Section.isBSS())
      Flags |= NEVERC_OBJECT_SECTION_ALLOCATED;
    if (Section.isText())
      Flags |= NEVERC_OBJECT_SECTION_EXECUTABLE;
    if (Section.isData() || Section.isBSS())
      Flags |= NEVERC_OBJECT_SECTION_WRITABLE;
  }
  if (Kind == NEVERC_OBJECT_SECTION_KIND_TLS_DATA ||
      Kind == NEVERC_OBJECT_SECTION_KIND_TLS_ZERO_FILL)
    Flags |= NEVERC_OBJECT_SECTION_WRITABLE | NEVERC_OBJECT_SECTION_TLS;
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

// ELF mapping symbols mark where a section switches between code and data.
// AArch64 spells them $x and $d, each optionally carrying a ".<suffix>". Other
// architectures define more -- ARM adds $a and $t for its two instruction sets
// -- but this build targets only AArch64 and x86, and x86 has none, so listing
// the ARM pair here would be unreachable either way.
bool isELFMappingSymbol(Triple::ArchType Arch, StringRef Name) {
  if (Arch != Triple::aarch64 || Name.size() < 2 || Name.front() != '$')
    return false;
  return (Name[1] == 'x' || Name[1] == 'd') &&
         (Name.size() == 2 || Name[2] == '.');
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
  // Mach-O's assembler mints "ltmp<n>" for section starts. Match the minted
  // shape rather than the prefix, so a program's own "ltmpBuffer" survives.
  if (isa<MachOObjectFile>(Object) && Name.starts_with("ltmp")) {
    unsigned TemporaryIndex = 0;
    if (!Name.drop_front(4).getAsInteger(10, TemporaryIndex))
      return true;
  }
  if (isa<ELFObjectFileBase>(Object) &&
      isELFMappingSymbol(Object.getArch(), Name))
    return true;
  return false;
}

// What the graph records about a relocation -- how wide the patched field is,
// how it is addressed, which linker-level form it belongs to, and whether it
// sits inside an instruction -- all follow from its type number, and the
// tables that say so live in NativeRelocationFacts.h. They are shared because
// a type number means nothing on its own: the reader that records it, the
// writer that restates it and the linker that patches the bytes it covers all
// have to read it through the same table or they answer about different
// relocations.
using RelocationFacts = NativeRelocationFacts;

// The architecture and format come from the object rather than from the
// TargetKey the caller supplied, so that a mismatch between the two cannot
// send a type number through the wrong table.
std::optional<Triple> relocationTableTriple(const ObjectFile &Object) {
  Triple Result;
  if (isa<MachOObjectFile>(Object))
    Result.setObjectFormat(Triple::MachO);
  else if (const auto *COFFObject = dyn_cast<COFFObjectFile>(&Object)) {
    if (COFFObject->getMachine() != COFF::IMAGE_FILE_MACHINE_AMD64 &&
        COFFObject->getMachine() != COFF::IMAGE_FILE_MACHINE_ARM64)
      return std::nullopt;
    Result.setObjectFormat(Triple::COFF);
  } else if (const auto *ELFObject = dyn_cast<ELFObjectFileBase>(&Object)) {
    if (ELFObject->getEMachine() != ELF::EM_X86_64 &&
        ELFObject->getEMachine() != ELF::EM_AARCH64)
      return std::nullopt;
    Result.setObjectFormat(Triple::ELF);
  } else
    return std::nullopt;
  if (Object.getArch() != Triple::x86_64 &&
      Object.getArch() != Triple::aarch64)
    return std::nullopt;
  Result.setArch(Object.getArch());
  return Result;
}

std::optional<RelocationFacts>
relocationFacts(const ObjectFile &Object, RelocationRef Relocation) {
  const std::optional<Triple> Table = relocationTableTriple(Object);
  if (!Table)
    return std::nullopt;
  const uint64_t Type = Relocation.getType();
  // Mach-O records the field size and the addressing mode in the relocation
  // itself, so only the rest is looked up.
  if (const auto *MachObject = dyn_cast<MachOObjectFile>(&Object)) {
    const unsigned Length =
        MachObject->getRelocationLength(Relocation.getRawDataRefImpl());
    if (Length > 3)
      return std::nullopt;
    MachO::any_relocation_info Native =
        MachObject->getRelocation(Relocation.getRawDataRefImpl());
    return machOFacts(Table->getArch(), Type, (UINT32_C(1) << Length) * 8,
                      MachObject->getAnyRelocationPCRel(Native) != 0);
  }
  return nativeRelocationFacts(*Table, Type);
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
    return status(NEVERC_STATUS_VERIFICATION_FAILED,
                  detail(DetailELFGroupSections));
  }
  for (const typename ELFT::Shdr &Section : *Sections) {
    if (Section.sh_type != ELF::SHT_GROUP)
      continue;
    auto Words =
        File.template getSectionContentsAsArray<typename ELFT::Word>(
            Section);
    if (!Words) {
      consumeError(Words.takeError());
      return status(NEVERC_STATUS_VERIFICATION_FAILED,
                    detail(DetailELFGroupContents));
    }
    if (Words->empty() ||
        (static_cast<uint32_t>(Words->front()) & ELF::GRP_COMDAT) == 0)
      continue;
    auto SymbolTable = File.getSection(Section.sh_link);
    if (!SymbolTable) {
      consumeError(SymbolTable.takeError());
      return status(NEVERC_STATUS_VERIFICATION_FAILED,
                    detail(DetailELFGroupSymbolTable));
    }
    auto Symbols = File.symbols(*SymbolTable);
    auto StringTable = File.getStringTableForSymtab(**SymbolTable);
    if (!Symbols || !StringTable ||
        Section.sh_info >= Symbols->size()) {
      if (!Symbols)
        consumeError(Symbols.takeError());
      if (!StringTable)
        consumeError(StringTable.takeError());
      return status(NEVERC_STATUS_VERIFICATION_FAILED,
                    detail(DetailELFGroupSymbols));
    }
    auto Name = (*Symbols)[Section.sh_info].getName(*StringTable);
    if (!Name || Name->empty()) {
      if (!Name)
        consumeError(Name.takeError());
      return status(NEVERC_STATUS_VERIFICATION_FAILED,
                    detail(DetailELFGroupName));
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

struct COFFComdatPlan {
  uint64_t SectionIndex = 0;
  std::string Name;
  NevercObjectComdatSelection Selection = NEVERC_OBJECT_COMDAT_ANY;
  // 1-based COFF section number of the parent, for associative COMDATs only.
  int32_t AssociatedSectionNumber = 0;
};

NevercStatus createCOFFComdats(
    const COFFObjectFile &Object,
    const NevercObjectReadRequest &Request,
    std::vector<ComdatMapEntry> &Comdats) {
  std::vector<COFFComdatPlan> Plans;
  for (SectionRef Section : Object.sections()) {
    const coff_section *Native = Object.getCOFFSection(Section);
    if (!Native ||
        (Native->Characteristics & COFF::IMAGE_SCN_LNK_COMDAT) == 0)
      continue;
    const int32_t SectionNumber =
        static_cast<int32_t>(Section.getIndex() + 1);
    COFFComdatPlan Plan;
    Plan.SectionIndex = Section.getIndex();
    for (SymbolRef Symbol : Object.symbols()) {
      COFFSymbolRef NativeSymbol = Object.getCOFFSymbol(Symbol);
      if (NativeSymbol.getSectionNumber() != SectionNumber)
        continue;
      if (const coff_aux_section_definition *Definition =
              NativeSymbol.getSectionDefinition()) {
        const NevercObjectComdatSelection Mapped =
            coffComdatSelection(Definition->Selection);
        if (Mapped != 0)
          Plan.Selection = Mapped;
        // An associative COMDAT names the section it lives or dies with --
        // this is how .pdata and .xdata stay attached to the .text they
        // describe. The graph requires that parent, so read it rather than
        // leaving the association dangling.
        if (Mapped == NEVERC_OBJECT_COMDAT_ASSOCIATIVE)
          Plan.AssociatedSectionNumber =
              Definition->getNumber(NativeSymbol.isBigObj());
      }
      if (NativeSymbol.isExternal()) {
        auto SymbolName = Symbol.getName();
        if (!SymbolName) {
          consumeError(SymbolName.takeError());
          return status(NEVERC_STATUS_VERIFICATION_FAILED,
                        detail(DetailCOFFComdatSymbolName));
        }
        if (!SymbolName->empty())
          Plan.Name = SymbolName->str();
      }
    }
    if (Plan.Name.empty()) {
      auto SectionName = Section.getName();
      if (!SectionName) {
        consumeError(SectionName.takeError());
        return status(NEVERC_STATUS_VERIFICATION_FAILED,
                      detail(DetailCOFFComdatSectionName));
      }
      Plan.Name = SectionName->str();
    }
    Plans.push_back(std::move(Plan));
  }

  // A parent has to exist before the COMDAT that points at it, and section
  // order does not guarantee that, so create whatever is ready each round
  // until nothing new can be.
  std::vector<bool> Created(Plans.size(), false);
  size_t Remaining = Plans.size();
  while (Remaining != 0) {
    size_t CreatedThisRound = 0;
    for (size_t I = 0; I != Plans.size(); ++I) {
      if (Created[I])
        continue;
      const COFFComdatPlan &Plan = Plans[I];
      NevercObjectComdatHandle Associated{};
      if (Plan.Selection == NEVERC_OBJECT_COMDAT_ASSOCIATIVE) {
        if (Plan.AssociatedSectionNumber <= 0)
          return status(NEVERC_STATUS_VERIFICATION_FAILED,
                        detail(DetailCOFFComdatAssociativeMissing));
        const uint64_t ParentIndex =
            static_cast<uint64_t>(Plan.AssociatedSectionNumber - 1);
        if (ParentIndex == Plan.SectionIndex)
          return status(NEVERC_STATUS_VERIFICATION_FAILED,
                        detail(DetailCOFFComdatSelfAssociative));
        auto Parent = llvm::find_if(
            Comdats, [&](const ComdatMapEntry &Entry) {
              return Entry.SectionIndex == ParentIndex;
            });
        if (Parent == Comdats.end())
          continue; // Parent not created yet; try again next round.
        Associated = Parent->Handle;
      }
      NevercObjectComdatDescriptor Descriptor{};
      Descriptor.Header = {sizeof(Descriptor), NEVERC_OBJECT_API_MAJOR,
                           NEVERC_OBJECT_API_MINOR, 0};
      Descriptor.Name = stringView(Plan.Name);
      Descriptor.Selection = Plan.Selection;
      Descriptor.AssociatedComdat = Associated;
      NevercObjectComdatHandle Handle{};
      NevercStatus Result = Request.Object->CreateComdat(
          Request.Object->Context, Request.Task, Request.Mutation,
          &Descriptor, &Handle);
      if (!neverc_status_is_ok(Result))
        return Result;
      Comdats.push_back({Plan.SectionIndex, Handle});
      Created[I] = true;
      --Remaining;
      ++CreatedThisRound;
    }
    // Nothing moved: the remaining associations form a cycle or name a
    // section that is not a COMDAT at all.
    if (CreatedThisRound == 0)
      return status(NEVERC_STATUS_VERIFICATION_FAILED,
                    detail(DetailCOFFComdatCycle));
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

// Indexed by section index rather than scanned: createRelocations resolves a
// section per relocation and createSymbols per symbol, so a linear scan makes
// the reader quadratic in the size of the object.
using SectionIndexMap = DenseMap<uint64_t, SectionMapEntry *>;

SectionIndexMap indexSections(std::vector<SectionMapEntry> &Sections) {
  SectionIndexMap Index;
  Index.reserve(Sections.size());
  for (SectionMapEntry &Entry : Sections)
    Index.try_emplace(Entry.Section.getIndex(), &Entry);
  return Index;
}

SectionMapEntry *findSection(const SectionIndexMap &Index,
                             SectionRef Section) {
  const auto It = Index.find(Section.getIndex());
  return It == Index.end() ? nullptr : It->second;
}

// A relocation may name a symbol that createSymbols() dropped as a container
// artifact.  Those symbols label the start of a section, so the reference is
// really section-relative and the graph can say so directly.
//
// The test is the same predicate createSymbols() drops by, not a subset of it:
// anything dropped there has to be recoverable here, or a relocation naming it
// has nowhere left to point.  Mach-O's "ltmp<n>" section labels are the case
// that matters -- they are dropped without being SF_FormatSpecific, and any
// object with more than a handful of sections has relocations against them.
SectionMapEntry *sectionForDroppedSymbol(const ObjectFile &Object,
                                         SymbolRef Symbol,
                                         const SectionIndexMap &Sections) {
  Expected<uint32_t> Flags = Symbol.getFlags();
  Expected<StringRef> Name = Symbol.getName();
  if (!Flags || !Name) {
    if (!Flags)
      consumeError(Flags.takeError());
    if (!Name)
      consumeError(Name.takeError());
    return nullptr;
  }
  if (!isSyntheticAssemblerSymbol(Object, *Name, *Flags))
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

// A symbol has no stable index the way a section does, so its DataRefImpl --
// the opaque cursor llvm::object hands out -- is the key.
using SymbolIndexMap = DenseMap<std::pair<uint64_t, uint64_t>,
                                const SymbolMapEntry *>;

// Which member of DataRefImpl's union is live depends on the format, so the
// key is its bytes -- the same thing llvm::object itself compares when asked
// whether two symbols are the same symbol.
std::pair<uint64_t, uint64_t> symbolKey(SymbolRef Symbol) {
  const DataRefImpl Impl = Symbol.getRawDataRefImpl();
  uint64_t Words[2] = {0, 0};
  static_assert(sizeof(Impl) <= sizeof(Words),
                "DataRefImpl does not fit the symbol key");
  std::memcpy(Words, &Impl, sizeof(Impl));
  return {Words[0], Words[1]};
}

const SymbolMapEntry *findSymbol(const SymbolIndexMap &Index,
                                 SymbolRef Symbol) {
  const auto It = Index.find(symbolKey(Symbol));
  return It == Index.end() ? nullptr : It->second;
}

NevercStatus createSections(BuiltinObjectFormat Format,
                            const ObjectFile &Object,
                            const NevercObjectReadRequest &Request,
                            ArrayRef<ComdatMapEntry> Comdats,
                            std::vector<SectionMapEntry> &Sections) {
  // Indexed for the same reason the section and symbol maps are: C++ puts every
  // inline function, template instantiation and vtable in its own COMDAT, so
  // the list grows with the section list and scanning it once per section is
  // quadratic in the size of the object.
  DenseMap<uint64_t, NevercObjectComdatHandle> ComdatBySection;
  ComdatBySection.reserve(Comdats.size());
  for (const ComdatMapEntry &Entry : Comdats)
    ComdatBySection.try_emplace(Entry.SectionIndex, Entry.Handle);

  uint64_t AnonymousIndex = 0;
  for (SectionRef Section : Object.sections()) {
    if (!isLogicalObjectSection(Object, Section))
      continue;
    Expected<StringRef> NameOrError = Section.getName();
    if (!NameOrError) {
      consumeError(NameOrError.takeError());
      return status(NEVERC_STATUS_VERIFICATION_FAILED,
                    detailAt(DetailSectionName, Sections.size()));
    }
    std::string GeneratedName;
    StringRef Name = *NameOrError;
    if (Name.empty()) {
      GeneratedName = "$section." + std::to_string(++AnonymousIndex);
      Name = GeneratedName;
    }

    const bool IsTLS = isTLSSection(Object, Section, Name);
    const NevercObjectSectionKind Kind =
        sectionKind(Format, Object, Section, Name, IsTLS);
    SmallVector<uint8_t, 48> Extension;
    nativeSectionExtension(Object, Section, Extension);

    NevercObjectSectionDescriptor Descriptor{};
    Descriptor.Header = {sizeof(Descriptor), NEVERC_OBJECT_API_MAJOR,
                         NEVERC_OBJECT_API_MINOR, 0};
    Descriptor.Name = stringView(Name);
    Descriptor.Kind = Kind;
    Descriptor.Flags = sectionFlags(Object, Section, Kind);
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
        return status(NEVERC_STATUS_VERIFICATION_FAILED,
                      detailAt(DetailSectionContents, Sections.size()));
      }
      if (ContentsOrError->size() != Size)
        return status(NEVERC_STATUS_VERIFICATION_FAILED,
                      detailAt(DetailSectionSizeMismatch, Sections.size()));
      Descriptor.Data = {
          reinterpret_cast<const uint8_t *>(ContentsOrError->data()),
          static_cast<uint64_t>(ContentsOrError->size())};
    }
    Descriptor.ExtensionOwner = Request.Target.ObjectFormatID;
    Descriptor.ExtensionVersion = builtinext::SectionVersion;
    Descriptor.Extension = byteView(Extension);
    const auto Comdat = ComdatBySection.find(Section.getIndex());
    if (Comdat != ComdatBySection.end())
      Descriptor.Comdat = Comdat->second;

    NevercObjectSectionHandle Handle{};
    NevercStatus Result = Request.Object->CreateSection(
        Request.Object->Context, Request.Task, Request.Mutation,
        &Descriptor, &Handle);
    if (!neverc_status_is_ok(Result)) {
      if (Result.Detail == 0)
        Result.Detail = detailAt(DetailSectionCreate, Sections.size());
      return Result;
    }
    Sections.push_back({Section, Handle, Kind, Size,
                        Section.getAddress(), Descriptor.Comdat});
  }
  return neverc_status_ok();
}

NevercStatus createSymbols(const ObjectFile &Object,
                           const NevercObjectReadRequest &Request,
                           const SectionIndexMap &Sections,
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
      return status(NEVERC_STATUS_VERIFICATION_FAILED,
                    detailAt(DetailSymbolQuery, Symbols.size()));
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
        return status(NEVERC_STATUS_VERIFICATION_FAILED,
                      detailAt(DetailSymbolOutsideSection, Symbols.size()));
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
    Descriptor.ExtensionVersion = builtinext::SymbolVersion;
    Descriptor.Extension = byteView(Extension);

    NevercObjectSymbolHandle Handle{};
    NevercStatus Result = Request.Object->CreateSymbol(
        Request.Object->Context, Request.Task, Request.Mutation,
        &Descriptor, &Handle);
    if (!neverc_status_is_ok(Result)) {
      if (Result.Detail == 0)
        Result.Detail = detailAt(DetailSymbolCreate, Symbols.size());
      return Result;
    }
    Symbols.push_back({Symbol, Handle});
  }
  return neverc_status_ok();
}

// COFF and Mach-O keep a relocation's addend in the bytes it covers rather than
// in the relocation record. The writer restates such a relocation as a data
// directive, which overwrites those bytes, so the addend has to be lifted into
// the graph here or the rewrite silently drops it.
int64_t implicitAddend(StringRef Contents, uint64_t Offset, uint32_t Width) {
  // A field wider than the accumulator has no addend this can lift, and
  // shifting past the accumulator's width is undefined rather than merely
  // wrong.
  if (Width == 0 || Width > 64)
    return 0;
  uint64_t Raw = 0;
  for (uint32_t I = 0; I != Width / 8; ++I)
    Raw |= static_cast<uint64_t>(
               static_cast<uint8_t>(Contents[static_cast<size_t>(Offset) + I]))
           << (I * 8);
  if (Width < 64) {
    // An addend is a displacement, so a field with its top bit set is negative.
    const uint64_t SignBit = UINT64_C(1) << (Width - 1);
    if ((Raw & SignBit) != 0)
      Raw |= ~((UINT64_C(1) << Width) - 1);
  }
  return static_cast<int64_t>(Raw);
}

NevercStatus createRelocations(const ObjectFile &Object,
                               const NevercObjectReadRequest &Request,
                               const SectionIndexMap &Sections,
                               const SymbolIndexMap &Symbols) {
  const bool ELFAddends = isa<ELFObjectFileBase>(Object);
  for (SectionRef RelocationSection : Object.sections()) {
    SectionMapEntry *MappedSection =
        findSection(Sections, RelocationSection);
    Expected<section_iterator> RelocatedSection =
        RelocationSection.getRelocatedSection();
    if (!RelocatedSection) {
      consumeError(RelocatedSection.takeError());
      return status(NEVERC_STATUS_VERIFICATION_FAILED,
                    detail(DetailRelocatedSectionQuery));
    }
    // On ELF the relocations live in their own .rela section, which the graph
    // does not carry; elsewhere they hang off the section they patch.
    if (*RelocatedSection != Object.section_end())
      MappedSection = findSection(Sections, **RelocatedSection);
    if (!MappedSection) {
      if (RelocationSection.relocations().begin() ==
          RelocationSection.relocations().end())
        continue;
      return status(NEVERC_STATUS_VERIFICATION_FAILED,
                    detail(DetailRelocationSectionUnmapped));
    }

    // Read once per section: the addend of a COFF or Mach-O relocation is in
    // these bytes.
    StringRef Contents;
    if (!ELFAddends) {
      Expected<StringRef> ContentsOrError =
          MappedSection->Section.getContents();
      if (ContentsOrError)
        Contents = *ContentsOrError;
      else
        consumeError(ContentsOrError.takeError());
    }

    for (RelocationRef Relocation : RelocationSection.relocations()) {
      SmallString<64> TypeStorage;
      Relocation.getTypeName(TypeStorage);
      StringRef TypeName = TypeStorage;
      const std::optional<RelocationFacts> Facts =
          relocationFacts(Object, Relocation);
      if (!Facts)
        return status(NEVERC_STATUS_CAPABILITY_UNAVAILABLE,
                      detail(DetailRelocationUnsupportedTarget));
      if (Facts->IsNoOp)
        continue;
      // Capped at 64 rather than 128 because that is what implicitAddend can
      // lift, and no format below asks for more.
      const uint32_t Width = Facts->Width;
      if (Width == 0 || Width > 64 || (Width % 8) != 0)
        return status(NEVERC_STATUS_VERIFICATION_FAILED,
                      detail(DetailRelocationWidthInvalid));
      if (Relocation.getOffset() > MappedSection->Size ||
          Width / 8 > MappedSection->Size - Relocation.getOffset())
        return status(NEVERC_STATUS_VERIFICATION_FAILED,
                      detail(DetailRelocationOutsideSection));

      NevercObjectRelocationDescriptor Descriptor{};
      Descriptor.Header = {sizeof(Descriptor), NEVERC_OBJECT_API_MAJOR,
                           NEVERC_OBJECT_API_MINOR, 0};
      Descriptor.Section = MappedSection->Handle;
      Descriptor.Offset = Relocation.getOffset();
      Descriptor.Kind = Facts->Kind;
      Descriptor.Width = Width;
      Descriptor.IsPCRelative =
          Facts->IsPCRelative ? NEVERC_TRUE : NEVERC_FALSE;
      Descriptor.IsSigned = Facts->IsSigned ? NEVERC_TRUE : NEVERC_FALSE;
      if (ELFAddends) {
        Expected<int64_t> Addend =
            ELFRelocationRef(Relocation).getAddend();
        if (Addend)
          Descriptor.Addend = *Addend;
        else
          consumeError(Addend.takeError());
      } else if (!Facts->IsInstructionField &&
                 Relocation.getOffset() + Width / 8 <= Contents.size()) {
        Descriptor.Addend =
            implicitAddend(Contents, Relocation.getOffset(), Width);
      }

      SmallVector<uint8_t, 80> Extension;
      nativeRelocationExtension(Relocation, TypeName, Extension);
      Descriptor.ExtensionOwner = Request.Target.ObjectFormatID;
      Descriptor.ExtensionVersion = builtinext::RelocationVersion;
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
          return status(NEVERC_STATUS_VERIFICATION_FAILED,
                        detail(DetailRelocationTargetUnmapped));
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
          Result.Detail = detail(DetailRelocationCreate);
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
    return status(NEVERC_STATUS_VERIFICATION_FAILED, detail(DetailObjectParse));
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
    return status(NEVERC_STATUS_VERIFICATION_FAILED,
                  detail(DetailObjectFormatMismatch));

  // The graph says which architecture it is for, and the caller states that,
  // not the object. Reading on regardless produces a graph whose TargetKey
  // contradicts its own contents -- and the contents that matter most are the
  // relocation type numbers, which mean nothing until an architecture is
  // named. 4 is AMD64's REL32 and ARM64's PAGEBASE_REL21; 2 is x86's BRANCH
  // and ARM64's BRANCH26. A consumer that reads a native type back through the
  // TargetKey -- the object writer, the dyncode relocation provider -- then
  // reads it out of the wrong table and gets an answer that is wrong without
  // looking wrong. Refusing here is what keeps that pair consistent for
  // everyone downstream, rather than each consumer having to notice it.
  StringRef TripleText(Request->Target.RawTriple.Data
                           ? Request->Target.RawTriple.Data
                           : "",
                       static_cast<size_t>(Request->Target.RawTriple.Length));
  const Triple TargetTriple(Triple::normalize(TripleText));
  if (Object.getArch() != TargetTriple.getArch())
    return status(NEVERC_STATUS_VERIFICATION_FAILED,
                  detail(DetailObjectArchMismatch));

  std::vector<ComdatMapEntry> Comdats;
  NevercStatus Result = createComdats(Object, *Request, Comdats);
  if (!neverc_status_is_ok(Result))
    return Result;
  std::vector<SectionMapEntry> Sections;
  std::vector<SymbolMapEntry> Symbols;
  Result = createSections(Context->Format, Object, *Request, Comdats,
                          Sections);
  if (!neverc_status_is_ok(Result))
    return Result;
  const SectionIndexMap SectionIndex = indexSections(Sections);
  Result = createSymbols(Object, *Request, SectionIndex, Symbols);
  if (!neverc_status_is_ok(Result))
    return Result;
  SymbolIndexMap SymbolIndex;
  SymbolIndex.reserve(Symbols.size());
  for (const SymbolMapEntry &Entry : Symbols)
    SymbolIndex.try_emplace(symbolKey(Entry.Symbol), &Entry);
  return createRelocations(Object, *Request, SectionIndex, SymbolIndex);
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
