#include "ELFLinkGraphAdapter.h"
#include "llvm/Support/SHA256.h"

#include "neverc/Linker/ELF/Config.h"
#include "neverc/Linker/ELF/InputFiles.h"
#include "neverc/Linker/ELF/InputSection.h"
#include "neverc/Linker/ELF/OutputSections.h"
#include "neverc/Linker/ELF/Relocations.h"
#include "neverc/Linker/ELF/Symbols.h"
#include "neverc/Linker/ELF/SyntheticSections.h"
#include "neverc/Plugin/Host/BuiltinTargetProvider.h"
#include "neverc/Plugin/Host/ObjectSectionRole.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/MathExtras.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <set>

// Included last: ELFContextAccess.h still defines the transitional `in` and
// `symtab` spellings. They must not be active while standard-library or LLVM
// headers are parsed because `in` collides with identifiers such as
// std::ios_base::in. Linker state uses the explicit elfState() accessor.
#include "neverc/Linker/ELF/ELFContextAccess.h"

using namespace llvm;
using namespace llvm::ELF;
using namespace neverc::plugin;

namespace linker::elf {
namespace {

constexpr NevercInterfaceID ELFFileExtension{UINT64_C(0x4e43454c4646494c),
                                             UINT64_C(1)};
constexpr NevercInterfaceID ELFSectionExtension{UINT64_C(0x4e43454c46534543),
                                                UINT64_C(1)};
constexpr NevercInterfaceID ELFSymbolExtension{UINT64_C(0x4e43454c4653594d),
                                               UINT64_C(1)};
constexpr NevercInterfaceID ELFRelocationExtension{UINT64_C(0x4e43454c4652454c),
                                                   UINT64_C(1)};

Error adapterError(const Twine &Message) {
  return createStringError(errc::invalid_argument,
                           "ELF LinkGraph adapter: " + Message);
}

bool sameID(NevercInterfaceID Left, NevercInterfaceID Right) {
  return Left.High == Right.High && Left.Low == Right.Low;
}

void appendU32(std::vector<uint8_t> &Bytes, uint32_t Value) {
  const size_t Offset = Bytes.size();
  Bytes.resize(Offset + sizeof(Value));
  support::endian::write32le(Bytes.data() + Offset, Value);
}

void appendU64(std::vector<uint8_t> &Bytes, uint64_t Value) {
  const size_t Offset = Bytes.size();
  Bytes.resize(Offset + sizeof(Value));
  support::endian::write64le(Bytes.data() + Offset, Value);
}

std::string digest(ArrayRef<uint8_t> Bytes) {
  static constexpr char Hex[] = "0123456789abcdef";
  const auto Value = SHA256::hash(Bytes);
  std::string Result;
  Result.reserve(Value.size() * 2);
  for (uint8_t Byte : Value) {
    Result.push_back(Hex[Byte >> 4]);
    Result.push_back(Hex[Byte & 15]);
  }
  return Result;
}

void setExtension(PluginLinkExtensionSet &Extensions,
                  NevercInterfaceID Namespace, uint32_t Version,
                  std::vector<uint8_t> Payload) {
  llvm::erase_if(Extensions.values(),
                 [&](const PluginLinkExtensionData &Value) {
                   return sameID(Value.NamespaceID, Namespace);
                 });
  PluginLinkExtensionData Value;
  Value.NamespaceID = Namespace;
  Value.Version = Version;
  Value.Payload = std::move(Payload);
  Value.Digest = digest(Value.Payload);
  Extensions.values().push_back(std::move(Value));
}

// The LinkGraph edge width is expressed in bits ({8,16,32,64}), not bytes, and
// names the storage field the relocation writes -- not the narrower immediate
// an instruction encodes inside it, so AArch64's 26-bit branches report 32.
// The scanner drops R_*_NONE before relocations reach here, so every edge that
// gets this far does write a field.
uint32_t relocationWidth(uint16_t Machine, uint32_t Type) {
  if (Machine == EM_X86_64)
    switch (Type) {
    case R_X86_64_8:
    case R_X86_64_PC8:
      return 8;
    case R_X86_64_16:
    case R_X86_64_PC16:
      return 16;
    case R_X86_64_64:
    case R_X86_64_PC64:
    case R_X86_64_GOT64:
    case R_X86_64_GOTOFF64:
    case R_X86_64_GOTPC64:
    case R_X86_64_GOTPCREL64:
    case R_X86_64_GOTPLT64:
    case R_X86_64_PLTOFF64:
    case R_X86_64_SIZE64:
    case R_X86_64_DTPMOD64:
    case R_X86_64_DTPOFF64:
    case R_X86_64_TPOFF64:
      return 64;
    default:
      return 32;
    }
  if (Machine == EM_AARCH64)
    switch (Type) {
    case R_AARCH64_ABS16:
    case R_AARCH64_PREL16:
      return 16;
    case R_AARCH64_ABS64:
    case R_AARCH64_PREL64:
    case R_AARCH64_GOTREL64:
    case R_AARCH64_AUTH_ABS64:
    case R_AARCH64_TLS_DTPMOD64:
    case R_AARCH64_TLS_DTPREL64:
    case R_AARCH64_TLS_TPREL64:
      return 64;
    default:
      return 32;
    }
  return 32;
}

NevercObjectSectionFlags sectionFlags(const InputSectionBase &Section) {
  NevercObjectSectionFlags Flags = 0;
  if ((Section.flags & SHF_ALLOC) != 0)
    Flags |= NEVERC_OBJECT_SECTION_ALLOCATED;
  if ((Section.flags & SHF_EXECINSTR) != 0)
    Flags |= NEVERC_OBJECT_SECTION_EXECUTABLE;
  if ((Section.flags & SHF_WRITE) != 0)
    Flags |= NEVERC_OBJECT_SECTION_WRITABLE;
  if ((Section.flags & SHF_MERGE) != 0)
    Flags |= NEVERC_OBJECT_SECTION_MERGEABLE;
  if ((Section.flags & SHF_STRINGS) != 0)
    Flags |= NEVERC_OBJECT_SECTION_STRINGS;
  if ((Section.flags & SHF_TLS) != 0)
    Flags |= NEVERC_OBJECT_SECTION_TLS;
  if ((Section.flags & SHF_EXCLUDE) != 0)
    Flags |= NEVERC_OBJECT_SECTION_DISCARDABLE;
  if ((Section.flags & SHF_GNU_RETAIN) != 0)
    Flags |= NEVERC_OBJECT_SECTION_RETAIN;
  // Not the linker's own isDebugSection(): that one also requires the section
  // to be non-allocated and does not know the ".zdebug" spelling, while
  // sectionKind() below asks only about the name. A graph is required to state
  // the two consistently -- the object verifier rejects a debug kind without
  // the matching flag -- so a ".zdebug_info" answered here by the linker's rule
  // and there by the name would contradict itself.
  if (isDebugSectionName(BuiltinObjectFormat::ELF, Section.name))
    Flags |= NEVERC_OBJECT_SECTION_DEBUG;
  if (isUnwindSectionName(BuiltinObjectFormat::ELF, Section.name))
    Flags |= NEVERC_OBJECT_SECTION_UNWIND;
  return Flags;
}

NevercObjectSectionFlags sectionFlags(const OutputSection &Section) {
  NevercObjectSectionFlags Flags = 0;
  if ((Section.flags & SHF_ALLOC) != 0)
    Flags |= NEVERC_OBJECT_SECTION_ALLOCATED;
  if ((Section.flags & SHF_EXECINSTR) != 0)
    Flags |= NEVERC_OBJECT_SECTION_EXECUTABLE;
  if ((Section.flags & SHF_WRITE) != 0)
    Flags |= NEVERC_OBJECT_SECTION_WRITABLE;
  if ((Section.flags & SHF_MERGE) != 0)
    Flags |= NEVERC_OBJECT_SECTION_MERGEABLE;
  if ((Section.flags & SHF_STRINGS) != 0)
    Flags |= NEVERC_OBJECT_SECTION_STRINGS;
  if ((Section.flags & SHF_TLS) != 0)
    Flags |= NEVERC_OBJECT_SECTION_TLS;
  if (isDebugSectionName(BuiltinObjectFormat::ELF, Section.name))
    Flags |= NEVERC_OBJECT_SECTION_DEBUG;
  if (isUnwindSectionName(BuiltinObjectFormat::ELF, Section.name))
    Flags |= NEVERC_OBJECT_SECTION_UNWIND;
  return Flags;
}

NevercObjectSectionKind sectionKind(uint32_t Type, uint64_t Flags,
                                    StringRef Name) {
  if (isDebugSectionName(BuiltinObjectFormat::ELF, Name))
    return NEVERC_OBJECT_SECTION_KIND_DEBUG;
  if (isUnwindSectionName(BuiltinObjectFormat::ELF, Name))
    return NEVERC_OBJECT_SECTION_KIND_UNWIND;
  if ((Flags & SHF_TLS) != 0)
    return Type == SHT_NOBITS ? NEVERC_OBJECT_SECTION_KIND_TLS_ZERO_FILL
                              : NEVERC_OBJECT_SECTION_KIND_TLS_DATA;
  if (Type == SHT_NOBITS)
    return NEVERC_OBJECT_SECTION_KIND_ZERO_FILL;
  if ((Flags & SHF_EXECINSTR) != 0)
    return NEVERC_OBJECT_SECTION_KIND_TEXT;
  if ((Flags & SHF_WRITE) == 0)
    return NEVERC_OBJECT_SECTION_KIND_READ_ONLY_DATA;
  return NEVERC_OBJECT_SECTION_KIND_DATA;
}

NevercLinkSymbolBinding symbolBinding(const Symbol &SymbolValue) {
  if (SymbolValue.isCommon())
    return NEVERC_LINK_SYMBOL_BINDING_COMMON;
  switch (SymbolValue.binding) {
  case STB_LOCAL:
    return NEVERC_LINK_SYMBOL_BINDING_LOCAL;
  case STB_WEAK:
    return NEVERC_LINK_SYMBOL_BINDING_WEAK;
  default:
    return NEVERC_LINK_SYMBOL_BINDING_GLOBAL;
  }
}

NevercLinkSymbolVisibility symbolVisibility(const Symbol &SymbolValue) {
  switch (SymbolValue.visibility()) {
  case STV_HIDDEN:
    return NEVERC_LINK_SYMBOL_VISIBILITY_HIDDEN;
  case STV_PROTECTED:
    return NEVERC_LINK_SYMBOL_VISIBILITY_PROTECTED;
  case STV_INTERNAL:
    return NEVERC_LINK_SYMBOL_VISIBILITY_INTERNAL;
  default:
    return NEVERC_LINK_SYMBOL_VISIBILITY_DEFAULT;
  }
}

NevercObjectSymbolType symbolType(const Symbol &SymbolValue) {
  switch (SymbolValue.type) {
  case STT_OBJECT:
    return NEVERC_OBJECT_SYMBOL_TYPE_OBJECT;
  case STT_FUNC:
    return NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
  case STT_SECTION:
    return NEVERC_OBJECT_SYMBOL_TYPE_SECTION;
  case STT_TLS:
    return NEVERC_OBJECT_SYMBOL_TYPE_TLS;
  case STT_FILE:
    return NEVERC_OBJECT_SYMBOL_TYPE_FILE;
  case STT_GNU_IFUNC:
    return NEVERC_OBJECT_SYMBOL_TYPE_INDIRECT_FUNCTION;
  case STT_NOTYPE:
    return NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE;
  default:
    return NEVERC_OBJECT_SYMBOL_TYPE_FORMAT_EXTENSION;
  }
}

NevercObjectSectionFlags nativeSectionFlags(NevercObjectSectionFlags Flags) {
  uint64_t Native = 0;
  if ((Flags & NEVERC_OBJECT_SECTION_ALLOCATED) != 0)
    Native |= SHF_ALLOC;
  if ((Flags & NEVERC_OBJECT_SECTION_EXECUTABLE) != 0)
    Native |= SHF_EXECINSTR;
  if ((Flags & NEVERC_OBJECT_SECTION_WRITABLE) != 0)
    Native |= SHF_WRITE;
  if ((Flags & NEVERC_OBJECT_SECTION_MERGEABLE) != 0)
    Native |= SHF_MERGE;
  if ((Flags & NEVERC_OBJECT_SECTION_STRINGS) != 0)
    Native |= SHF_STRINGS;
  if ((Flags & NEVERC_OBJECT_SECTION_TLS) != 0)
    Native |= SHF_TLS;
  if ((Flags & NEVERC_OBJECT_SECTION_RETAIN) != 0)
    Native |= SHF_GNU_RETAIN;
  return Native;
}

uint32_t nativeSectionType(NevercObjectSectionKind Kind) {
  return Kind == NEVERC_OBJECT_SECTION_KIND_ZERO_FILL ||
                 Kind == NEVERC_OBJECT_SECTION_KIND_TLS_ZERO_FILL
             ? SHT_NOBITS
             : SHT_PROGBITS;
}

uint8_t nativeBinding(NevercLinkSymbolBinding Binding) {
  switch (Binding) {
  case NEVERC_LINK_SYMBOL_BINDING_LOCAL:
    return STB_LOCAL;
  case NEVERC_LINK_SYMBOL_BINDING_WEAK:
    return STB_WEAK;
  case NEVERC_LINK_SYMBOL_BINDING_COMMON:
    return STB_GLOBAL;
  default:
    return STB_GLOBAL;
  }
}

uint8_t nativeVisibility(NevercLinkSymbolVisibility Visibility) {
  switch (Visibility) {
  case NEVERC_LINK_SYMBOL_VISIBILITY_HIDDEN:
    return STV_HIDDEN;
  case NEVERC_LINK_SYMBOL_VISIBILITY_PROTECTED:
    return STV_PROTECTED;
  case NEVERC_LINK_SYMBOL_VISIBILITY_INTERNAL:
    return STV_INTERNAL;
  default:
    return STV_DEFAULT;
  }
}

uint8_t nativeSymbolType(NevercObjectSymbolType Type) {
  switch (Type) {
  case NEVERC_OBJECT_SYMBOL_TYPE_OBJECT:
    return STT_OBJECT;
  case NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION:
    return STT_FUNC;
  case NEVERC_OBJECT_SYMBOL_TYPE_SECTION:
    return STT_SECTION;
  case NEVERC_OBJECT_SYMBOL_TYPE_TLS:
    return STT_TLS;
  case NEVERC_OBJECT_SYMBOL_TYPE_FILE:
    return STT_FILE;
  case NEVERC_OBJECT_SYMBOL_TYPE_INDIRECT_FUNCTION:
    return STT_GNU_IFUNC;
  default:
    return STT_NOTYPE;
  }
}

template <typename Map, typename Key, typename Add>
uint64_t ensureID(Map &IDs, Key Native, Add AddEntity) {
  auto It = IDs.find(Native);
  if (It != IDs.end())
    return It->second;
  const uint64_t ID = AddEntity().ID;
  IDs[Native] = ID;
  return ID;
}

uint64_t sectionMemorySize(const InputSectionBase &Section) {
  if (const auto *Synthetic = dyn_cast<SyntheticSection>(&Section))
    return Synthetic->getSize();
  return Section.getSize();
}

ArrayRef<uint8_t> sectionContent(const InputSectionBase &Section) {
  if (Section.type == SHT_NOBITS || isa<SyntheticSection>(Section))
    return {};
  return Section.contentMaybeDecompress();
}

PluginLinkOriginData originFor(const DenseMap<const InputFile *, uint64_t> &IDs,
                               const InputFile *File) {
  PluginLinkOriginData Origin;
  if (File) {
    auto It = IDs.find(File);
    if (It != IDs.end())
      Origin.InputID = It->second;
  }
  Origin.CreatedByProvider = "neverc.builtin.elf";
  return Origin;
}

} // namespace

ELFLinkGraphAdapter::ELFLinkGraphAdapter(
    PluginTaskContext &TaskValue, std::shared_ptr<PluginLinkGraph> GraphValue)
    : Task(TaskValue), Graph(std::move(GraphValue)) {}

ELFLinkGraphAdapter::~ELFLinkGraphAdapter() = default;

Expected<std::unique_ptr<ELFLinkGraphAdapter>>
ELFLinkGraphAdapter::create(PluginTaskContext &Task, StringRef TargetTriple,
                            StringRef CPU,
                            NevercTargetRelocationModel RelocationModel) {
  if (TargetTriple.empty())
    return adapterError("target triple is required");
  const BuiltinTargetRoute *Route = findBuiltinTargetRoute(TargetTriple);
  if (!Route || Route->ObjectFormat != BuiltinObjectFormat::ELF)
    return adapterError("target triple has no built-in ELF route");
  auto TargetKey =
      createBuiltinTargetKey(*Route, TargetTriple, CPU, RelocationModel);
  if (!TargetKey)
    return joinErrors(adapterError("could not create the target key"),
                      TargetKey.takeError());
  auto Graph = std::make_shared<PluginLinkGraph>(std::move(*TargetKey),
                                                 NEVERC_LINK_STATE_INITIAL);
  return std::unique_ptr<ELFLinkGraphAdapter>(
      new ELFLinkGraphAdapter(Task, std::move(Graph)));
}

Expected<std::shared_ptr<PluginLinkGraph>>
ELFLinkGraphAdapter::capture(const PluginLinkGraph &Previous,
                             NevercLinkState State) {
  auto Result = std::make_shared<PluginLinkGraph>(Previous);

  uint64_t Ordinal = 0;
  const auto CaptureInput = [&](InputFile *File, NevercLinkInputKind Kind) {
    if (!File)
      return uint64_t(0);
    const uint64_t ID = ensureID(InputIDs, File, [&]() -> PluginLinkInput & {
      return Result->addInput(PluginLinkInput{});
    });
    NativeInputs[ID] = File;
    PluginLinkInput *Input = Result->findInput(ID);
    if (!Input)
      return uint64_t(0);
    Input->Kind = Kind;
    Input->Ordinal = Ordinal++;
    Input->LogicalURI =
        File->getName().empty() ? "<memory>" : File->getName().str();
    const StringRef Buffer = File->mb.getBuffer();
    Input->ContentDigest = SHA256::hash(ArrayRef<uint8_t>(
        reinterpret_cast<const uint8_t *>(Buffer.data()), Buffer.size()));
    Input->ReaderRoute = "neverc.builtin.elf";
    if (File->lazy)
      Input->Flags |= NEVERC_LINK_INPUT_FLAG_LAZY;
    std::vector<uint8_t> Payload;
    appendU32(Payload, File->kind());
    appendU32(Payload, File->ekind);
    appendU32(Payload, File->emachine);
    appendU32(Payload, File->osabi);
    appendU32(Payload, File->abiVersion);
    appendU32(Payload, File->groupId);
    setExtension(Input->Extensions, ELFFileExtension, 1, std::move(Payload));
    return ID;
  };

  for (ELFFileBase *File : elfState().objectFiles)
    CaptureInput(File, NEVERC_LINK_INPUT_OBJECT);
  for (SharedFile *File : elfState().sharedFiles)
    CaptureInput(File, NEVERC_LINK_INPUT_SHARED_LIBRARY);
  for (BinaryFile *File : elfState().binaryFiles)
    CaptureInput(File, NEVERC_LINK_INPUT_BLOB);
  for (BitcodeFile *File : elfState().bitcodeFiles)
    CaptureInput(File, NEVERC_LINK_INPUT_BITCODE);
  for (BitcodeFile *File : elfState().lazyBitcodeFiles)
    CaptureInput(File, NEVERC_LINK_INPUT_BITCODE);

  const bool HasLayout = State >= NEVERC_LINK_STATE_LAYOUT_COMPLETE;
  if (HasLayout) {
    // The graph is copied from the preceding phase. Clear every old layout
    // projection before repopulating native output sections; otherwise input
    // and synthetic sections that disappeared during GC retain zero-based
    // pre-layout ranges and falsely overlap the final ELF layout.
    for (PluginLinkSection &Section : Result->sections()) {
      Section.Address = 0;
      Section.FileOffset = 0;
      Section.Size = 0;
    }
    for (PluginLinkAtom &Atom : Result->atoms()) {
      Atom.Address = 0;
      Atom.FileOffset = 0;
      Atom.Flags &= ~NEVERC_LINK_ATOM_LIVE;
    }
  }
  bool AllowWX = false;
  const auto CaptureOutputSection = [&](OutputSection *Native) -> uint64_t {
    if (!Native)
      return 0;
    const uint64_t ID =
        ensureID(OutputSectionIDs, Native, [&]() -> PluginLinkSection & {
          return Result->addSection(PluginLinkSection{});
        });
    NativeOutputSections[ID] = Native;
    PluginLinkSection *Section = Result->findSection(ID);
    if (!Section)
      return 0;
    Section->Name = Native->name.empty() ? "<elf-output>" : Native->name.str();
    Section->Kind = sectionKind(Native->type, Native->flags, Native->name);
    Section->Flags = sectionFlags(*Native);
    Section->Alignment =
        isPowerOf2_64(Native->addralign) && Native->addralign != 0
            ? Native->addralign
            : 1;
    Section->Address = HasLayout ? Native->addr : 0;
    Section->FileOffset = HasLayout ? Native->offset : 0;
    Section->Size = HasLayout ? Native->size : 0;
    Section->Origin.CreatedByProvider = "neverc.builtin.elf";
    std::vector<uint8_t> Payload;
    appendU32(Payload, Native->type);
    appendU64(Payload, Native->flags);
    appendU32(Payload, Native->sectionIndex);
    appendU32(Payload, Native->partition);
    setExtension(Section->Extensions, ELFSectionExtension, 1,
                 std::move(Payload));
    AllowWX |= (Section->Flags & NEVERC_OBJECT_SECTION_WRITABLE) != 0 &&
               (Section->Flags & NEVERC_OBJECT_SECTION_EXECUTABLE) != 0;
    return ID;
  };

  if (HasLayout)
    for (OutputSection *Section : outputSections)
      CaptureOutputSection(Section);

  const auto CaptureSection = [&](InputSectionBase *Native) {
    if (!Native || Native == discardedInputSection())
      return;
    const auto *Synthetic = dyn_cast<SyntheticSection>(Native);
    // prepareLayout() removes synthetic sections whose isNeeded() predicate
    // became false, but their owning unique_ptr and this adapter's identity
    // map remain valid.  SectionBase::partition is not cleared by that removal,
    // so isLive() alone would resurrect the stale section in the canonical
    // layout (for example .symtab_shndx with a non-zero theoretical getSize()
    // inside a zero-sized orphan OutputSection).
    const bool NativeIsMaterialized =
        Native->isLive() && (!HasLayout || !Synthetic ||
                             (Synthetic->getParent() && Synthetic->isNeeded()));
    // Some synthetic getSize() implementations require a live partition.
    // Removed/dead synthetic sections have no size in this graph snapshot.
    const uint64_t NativeMemorySize =
        (!Synthetic || Native->isLive()) && (!HasLayout || NativeIsMaterialized)
            ? sectionMemorySize(*Native)
            : 0;
    const uint64_t SectionID =
        ensureID(SectionIDs, Native, [&]() -> PluginLinkSection & {
          return Result->addSection(PluginLinkSection{});
        });
    NativeSections[SectionID] = Native;
    PluginLinkSection *Section = Result->findSection(SectionID);
    if (!Section)
      return;
    Section->Name = Native->name.empty() ? "<elf-section>" : Native->name.str();
    Section->Kind = sectionKind(Native->type, Native->flags, Native->name);
    Section->Flags = sectionFlags(*Native);
    Section->Alignment =
        isPowerOf2_64(Native->addralign) && Native->addralign != 0
            ? Native->addralign
            : 1;
    Section->Origin = originFor(InputIDs, Native->file);
    Section->Address = 0;
    Section->FileOffset = 0;
    Section->Size = HasLayout ? 0 : NativeMemorySize;
    std::vector<uint8_t> SectionPayload;
    appendU32(SectionPayload, Native->type);
    appendU64(SectionPayload, Native->flags);
    appendU32(SectionPayload, Native->link);
    appendU32(SectionPayload, Native->info);
    appendU32(SectionPayload, Native->partition);
    appendU32(SectionPayload, Native->kind());
    setExtension(Section->Extensions, ELFSectionExtension, 1,
                 std::move(SectionPayload));

    const uint64_t AtomID =
        ensureID(AtomIDs, Native, [&]() -> PluginLinkAtom & {
          return Result->addAtom(PluginLinkAtom{});
        });
    NativeAtoms[AtomID] = Native;
    PluginLinkAtom *Atom = Result->findAtom(AtomID);
    if (!Atom)
      return;
    Atom->Name = Section->Name;
    Atom->SectionID = SectionID;
    Atom->Flags = 0;
    if (NativeIsMaterialized)
      Atom->Flags |= NEVERC_LINK_ATOM_LIVE;
    if (Native->keepUnique)
      Atom->Flags |= NEVERC_LINK_ATOM_ADDRESS_SIGNIFICANT;
    if ((Native->flags & SHF_TLS) != 0)
      Atom->Flags |= NEVERC_LINK_ATOM_TLS;
    if (isUnwindSectionName(BuiltinObjectFormat::ELF, Native->name))
      Atom->Flags |= NEVERC_LINK_ATOM_UNWIND;
    if (isa<SyntheticSection>(Native))
      Atom->Flags |= NEVERC_LINK_ATOM_SYNTHETIC;
    Atom->Alignment = Section->Alignment;
    Atom->Origin = Section->Origin;
    Atom->Address = 0;
    Atom->FileOffset = 0;
    const ArrayRef<uint8_t> Content = sectionContent(*Native);
    Atom->Content.assign(Content.begin(), Content.end());
    Atom->ZeroFillSize = Content.empty() ? NativeMemorySize : 0;

    if (HasLayout && NativeIsMaterialized) {
      OutputSection *Output = Native->getOutputSection();
      const uint64_t OutputID = CaptureOutputSection(Output);
      const uint64_t OutputOffset = Output ? Native->getOffset(0) : UINT64_MAX;
      const uint64_t NativeSize = NativeMemorySize;
      // SHF_MERGE and .eh_frame sections are split into pieces that are
      // deduplicated and repacked, so the input section spans no contiguous
      // run of the output: its size stops describing the output, and its
      // first piece can land at an offset the input's own alignment does not
      // divide. Neither can be expressed as one laid-out atom.
      const bool Fragmented = Native->kind() == SectionBase::Merge ||
                              Native->kind() == SectionBase::EHFrame;
      if (!Fragmented && OutputID != 0 && OutputOffset <= Output->size &&
          NativeSize <= Output->size - OutputOffset) {
        Atom->SectionID = OutputID;
        Atom->Address = Native->getVA();
        Atom->FileOffset = Output->offset + OutputOffset;
        if (Native->type == SHT_NOBITS)
          Atom->Content.clear();
      } else {
        // The atom covers no addressable range of any output section, either
        // because it is fragmented as above or because ELF keeps some
        // synthetic sections isLive() without selecting them into an output
        // section. Neither belongs in the laid-out graph.
        Atom->Flags &= ~NEVERC_LINK_ATOM_LIVE;
      }
    }
  };

  for (InputSectionBase *Section : elfState().inputSections)
    CaptureSection(Section);
  for (EhInputSection *Section : elfState().ehInputSections)
    CaptureSection(Section);

  // Native synthetic sections can also be reachable only through output
  // section commands. Capture them before symbols and relocations.
  for (OutputSection *Output : outputSections) {
    SmallVector<InputSection *, 0> Storage;
    for (InputSection *Section : getInputSections(*Output, Storage))
      CaptureSection(Section);
  }
  if (HasLayout) {
    // Some ELF synthetic sections are attached directly to an output section
    // and are not returned by getInputSections(). Revisit every previously
    // known native atom so those sections receive their final output-section
    // identity and layout coordinates as well.
    SmallVector<InputSectionBase *, 0> KnownSections;
    KnownSections.reserve(NativeAtoms.size());
    for (const auto &[ID, Native] : NativeAtoms)
      KnownSections.push_back(Native);
    for (InputSectionBase *Native : KnownSections)
      CaptureSection(Native);
  }

  const auto CaptureSymbol = [&](Symbol *Native) -> uint64_t {
    if (!Native || Native->isPlaceholder() || Native->isLazy() ||
        Native->getName().empty())
      return 0;
    const uint64_t ID =
        ensureID(SymbolIDs, Native, [&]() -> PluginLinkSymbol & {
          return Result->addSymbol(PluginLinkSymbol{});
        });
    NativeSymbols[ID] = Native;
    PluginLinkSymbol *SymbolValue = Result->findSymbol(ID);
    if (!SymbolValue)
      return 0;
    SymbolValue->Name = Native->getName().str();
    SymbolValue->Version.clear();
    if (Native->hasVersionSuffix) {
      StringRef Version(Native->getVersionSuffix());
      SymbolValue->Version = Version.ltrim("@").str();
    }
    SymbolValue->Binding = symbolBinding(*Native);
    SymbolValue->Visibility = symbolVisibility(*Native);
    SymbolValue->Type = symbolType(*Native);
    SymbolValue->AtomID = 0;
    SymbolValue->Value = 0;
    // Only defined, common and shared symbols carry a size; Symbol::getSize()
    // asserts on the other kinds, so each branch below fills it in.
    SymbolValue->Size = 0;
    SymbolValue->IsImported = false;
    SymbolValue->IsPrevailing = false;
    SymbolValue->IsExported = Native->includeInDynsym();
    SymbolValue->IsRoot =
        !config->entry.empty() && Native->getName() == config->entry;
    SymbolValue->Origin = originFor(InputIDs, Native->file);

    if (auto *DefinedValue = dyn_cast<Defined>(Native)) {
      SymbolValue->Definition = NEVERC_LINK_SYMBOL_DEFINED;
      SymbolValue->Value = DefinedValue->value;
      SymbolValue->Size = DefinedValue->size;
      if (auto *Input =
              dyn_cast_or_null<InputSectionBase>(DefinedValue->section)) {
        CaptureSection(Input);
        SymbolValue->AtomID = AtomIDs.lookup(Input);
      } else {
        SymbolValue->Definition = NEVERC_LINK_SYMBOL_ABSOLUTE;
      }
      SymbolValue->IsPrevailing = true;
    } else if (auto *Common = dyn_cast<CommonSymbol>(Native)) {
      SymbolValue->Definition = NEVERC_LINK_SYMBOL_COMMON;
      SymbolValue->Binding = NEVERC_LINK_SYMBOL_BINDING_COMMON;
      SymbolValue->Size = Common->size;
      SymbolValue->IsPrevailing = true;
    } else if (auto *Shared = dyn_cast<SharedSymbol>(Native)) {
      SymbolValue->Definition = NEVERC_LINK_SYMBOL_SHARED;
      SymbolValue->Size = Shared->size;
      SymbolValue->IsImported = true;
      SymbolValue->IsPrevailing = true;
    } else if (isa<Undefined>(Native)) {
      // A non-weak undefined that survives native resolution represents a
      // dynamic import. Model it as shared so the canonical resolution proof
      // reflects the native backend's accepted outcome.
      if (!Native->isWeak() && State >= NEVERC_LINK_STATE_SYMBOLS_RESOLVED) {
        SymbolValue->Definition = NEVERC_LINK_SYMBOL_SHARED;
        SymbolValue->IsImported = true;
        SymbolValue->IsPrevailing = true;
      } else {
        SymbolValue->Definition = NEVERC_LINK_SYMBOL_UNDEFINED;
      }
    }
    if (SymbolValue->IsRoot && SymbolValue->AtomID != 0)
      if (PluginLinkAtom *Atom = Result->findAtom(SymbolValue->AtomID))
        Atom->Flags |= NEVERC_LINK_ATOM_ROOT | NEVERC_LINK_ATOM_LIVE;
    std::vector<uint8_t> Payload;
    appendU32(Payload, Native->kind());
    appendU32(Payload, Native->binding);
    appendU32(Payload, Native->type);
    appendU32(Payload, Native->stOther);
    appendU32(Payload, Native->versionId);
    appendU32(Payload, Native->partition);
    appendU32(Payload, Native->flags.load(std::memory_order_relaxed));
    setExtension(SymbolValue->Extensions, ELFSymbolExtension, 1,
                 std::move(Payload));
    return ID;
  };

  for (Symbol *SymbolValue : symtab.getSymbols())
    CaptureSymbol(SymbolValue);

  for (InputSectionBase *Native : elfState().inputSections) {
    if (!Native || Native == discardedInputSection())
      continue;
    const uint64_t SourceAtomID = AtomIDs.lookup(Native);
    if (SourceAtomID == 0)
      continue;
    const uint64_t SourceSize = sectionMemorySize(*Native);
    for (size_t Index = 0; Index != Native->relocs().size(); ++Index) {
      Relocation &Reloc = Native->relocs()[Index];
      if (Reloc.offset >= SourceSize || !Reloc.sym)
        continue;
      uint64_t TargetSymbolID = CaptureSymbol(Reloc.sym);
      uint64_t TargetAtomID = 0;
      if (TargetSymbolID == 0)
        if (auto *DefinedValue = dyn_cast<Defined>(Reloc.sym))
          if (auto *TargetSection =
                  dyn_cast_or_null<InputSectionBase>(DefinedValue->section)) {
            CaptureSection(TargetSection);
            TargetAtomID = AtomIDs.lookup(TargetSection);
          }
      if (TargetSymbolID == 0 && TargetAtomID == 0)
        continue;
      const auto Key =
          std::make_pair(static_cast<const InputSectionBase *>(Native), Index);
      const uint64_t EdgeID =
          ensureID(RelocationIDs, Key, [&]() -> PluginLinkEdge & {
            return Result->addEdge(PluginLinkEdge{});
          });
      NativeRelocations[EdgeID] = {Native, Index};
      PluginLinkEdge *Edge = Result->findEdge(EdgeID);
      if (!Edge)
        continue;
      Edge->Kind = NEVERC_LINK_EDGE_RELOCATION;
      Edge->SourceAtomID = SourceAtomID;
      Edge->Offset = Reloc.offset;
      Edge->RelocationKind = NEVERC_OBJECT_RELOCATION_TARGET_EXTENSION;
      Edge->Width = relocationWidth(config->emachine, Reloc.type);
      Edge->Addend = Reloc.addend;
      Edge->IsPCRelative = Reloc.expr == R_PC || Reloc.expr == R_PLT_PC;
      Edge->IsSigned = Edge->IsPCRelative;
      Edge->TargetSymbolID = TargetSymbolID;
      Edge->TargetAtomID = TargetAtomID;
      Edge->Origin = originFor(InputIDs, Native->file);
      std::vector<uint8_t> Payload;
      appendU32(Payload, Reloc.type);
      appendU32(Payload, Reloc.expr);
      setExtension(Edge->Extensions, ELFRelocationExtension, 1,
                   std::move(Payload));
    }
  }

  if (State >= NEVERC_LINK_STATE_GC_COMPLETE) {
    for (auto &[Native, AtomID] : AtomIDs) {
      PluginLinkAtom *Atom = Result->findAtom(AtomID);
      if (!Atom)
        continue;
      const auto *Synthetic = dyn_cast<SyntheticSection>(Native);
      const bool NativeIsMaterialized =
          Native->isLive() &&
          (!HasLayout || !Synthetic ||
           (Synthetic->getParent() && Synthetic->isNeeded()));
      // Once a layout exists, CaptureSection has already withheld liveness from
      // atoms that survive natively but occupy no output-section range. This
      // pass may only take liveness away, never restore it, or those atoms come
      // back with no address and fail layout verification.
      if (!NativeIsMaterialized)
        Atom->Flags &= ~NEVERC_LINK_ATOM_LIVE;
      else if (!HasLayout)
        Atom->Flags |= NEVERC_LINK_ATOM_LIVE;
    }
  }

  if (State >= NEVERC_LINK_STATE_ICF_COMPLETE) {
    for (auto &[Native, AtomID] : AtomIDs) {
      auto *Input =
          dyn_cast<InputSection>(const_cast<InputSectionBase *>(Native));
      PluginLinkAtom *Atom = Result->findAtom(AtomID);
      if (!Input || !Atom)
        continue;
      if (Input->repl != Input) {
        CaptureSection(Input->repl);
        Atom->FoldLeaderID = AtomIDs.lookup(Input->repl);
        Atom->Flags |= NEVERC_LINK_ATOM_FOLDED;
      } else {
        Atom->FoldLeaderID = 0;
        Atom->Flags &= ~NEVERC_LINK_ATOM_FOLDED;
      }
    }
  }

  if (State >= NEVERC_LINK_STATE_SYNTHETICS_READY) {
    for (auto &[Native, AtomID] : AtomIDs) {
      if (!isa<SyntheticSection>(Native))
        continue;
      PluginLinkAtom *Atom = Result->findAtom(AtomID);
      if (!Atom)
        continue;
      auto It = llvm::find_if(Result->synthetics(),
                              [&](const PluginLinkSynthetic &Value) {
                                return Value.AtomID == AtomID;
                              });
      PluginLinkSynthetic *Synthetic = nullptr;
      if (It == Result->synthetics().end()) {
        PluginLinkSynthetic Value;
        Value.Role = Native->name.empty()
                         ? "elf.synthetic"
                         : ("elf.synthetic." + Native->name).str();
        Value.SectionID = Atom->SectionID;
        Value.AtomID = AtomID;
        Value.Origin = Atom->Origin;
        Synthetic = &Result->addSynthetic(std::move(Value));
      } else {
        Synthetic = &*It;
      }
      Synthetic->SectionID = Atom->SectionID;
      Synthetic->AtomID = AtomID;
      Atom->Flags |= NEVERC_LINK_ATOM_SYNTHETIC;
    }
  }

  if (HasLayout && AllowWX) {
    auto It = llvm::find_if(Result->constraints(),
                            [](const PluginLinkConstraint &Value) {
                              return Value.Kind == "allow-wx";
                            });
    if (It == Result->constraints().end()) {
      PluginLinkConstraint Constraint;
      Constraint.Kind = "allow-wx";
      Constraint.Value = 1;
      Constraint.Origin.CreatedByProvider = "neverc.builtin.elf";
      Result->addConstraint(std::move(Constraint));
    } else {
      It->Value = 1;
    }
  }

  Result->advanceGeneration();
  Result->setState(State);
  if (Error E = verifyPluginLinkGraph(*Result))
    return joinErrors(adapterError("native projection is invalid"),
                      std::move(E));
  return Result;
}

Error ELFLinkGraphAdapter::applyDelta(const PluginLinkGraph &Before,
                                      const PluginLinkGraph &After,
                                      NevercLinkState State) {
  for (const auto &[AtomID, Native] : NativeAtoms) {
    const PluginLinkAtom *Old = Before.findAtom(AtomID);
    const PluginLinkAtom *Current = After.findAtom(AtomID);
    if (!Current) {
      Native->markDead();
      continue;
    }
    if ((Current->Flags & NEVERC_LINK_ATOM_LIVE) != 0)
      Native->markLive();
    else
      Native->markDead();
    if (Old && (Old->Content != Current->Content ||
                Old->ZeroFillSize != Current->ZeroFillSize)) {
      if (State >= NEVERC_LINK_STATE_THUNKS_RELAXED)
        return adapterError(
            "atom content changed after native relaxation; rebuild is "
            "not safe at this phase");
      const uint64_t NewSize = Current->Content.size() + Current->ZeroFillSize;
      if (NewSize > std::numeric_limits<size_t>::max())
        return adapterError("plugin atom content exceeds host size limits");
      if (!Current->Content.empty()) {
        auto *Storage = static_cast<uint8_t *>(
            bAlloc().Allocate(Current->Content.size(), alignof(uint8_t)));
        std::memcpy(Storage, Current->Content.data(), Current->Content.size());
        Native->content_ = Storage;
      } else {
        Native->content_ = nullptr;
      }
      Native->size = NewSize;
      Native->bss = Current->Content.empty() && NewSize != 0;
      Native->type = Native->bss ? SHT_NOBITS : SHT_PROGBITS;
    }
    if (auto *Input = dyn_cast<InputSection>(Native)) {
      if (Current->FoldLeaderID == 0) {
        Input->repl = Input;
      } else {
        auto Leader = NativeAtoms.find(Current->FoldLeaderID);
        if (Leader == NativeAtoms.end() || !isa<InputSection>(Leader->second))
          return adapterError("plugin selected a non-native ELF fold leader");
        Input->replace(cast<InputSection>(Leader->second));
      }
    }
  }

  for (const auto &[SymbolID, Native] : NativeSymbols) {
    const PluginLinkSymbol *Old = Before.findSymbol(SymbolID);
    const PluginLinkSymbol *Current = After.findSymbol(SymbolID);
    if (!Current) {
      if (!Native->isUndefined())
        Undefined(Native->file, Native->getName(), Native->binding,
                  Native->stOther, Native->type)
            .overwrite(*Native);
      continue;
    }
    Native->binding = nativeBinding(Current->Binding);
    Native->setVisibility(nativeVisibility(Current->Visibility));
    Native->type = nativeSymbolType(Current->Type);
    Native->exportDynamic = Current->IsExported;
    if (Current->IsRoot) {
      Native->isUsedInRegularObj = true;
      if (auto *DefinedValue = dyn_cast<Defined>(Native))
        if (DefinedValue->section)
          DefinedValue->section->markLive();
    } else if (Old && Old->IsRoot && Native->getName() != config->entry) {
      Native->isUsedInRegularObj = false;
    }
    if (Current->AtomID != 0 && (!Old || Current->AtomID != Old->AtomID)) {
      auto It = NativeAtoms.find(Current->AtomID);
      auto *DefinedValue = dyn_cast<Defined>(Native);
      if (It == NativeAtoms.end() || !DefinedValue)
        return adapterError(
            "plugin symbol rebind cannot be represented in ELF");
      DefinedValue->section = It->second;
      DefinedValue->value = Current->Value;
    }
  }

  for (const auto &[EdgeID, Ref] : NativeRelocations) {
    const PluginLinkEdge *Old = Before.findEdge(EdgeID);
    const PluginLinkEdge *Current = After.findEdge(EdgeID);
    if (!Current)
      return adapterError(
          "removing a native ELF relocation requires a full rebuild");
    if (!Ref.Section || Ref.Index >= Ref.Section->relocs().size())
      return adapterError("native ELF relocation identity became stale");
    Relocation &Native = Ref.Section->relocs()[Ref.Index];
    Native.addend = Current->Addend;
    if (!Old || Current->TargetSymbolID != Old->TargetSymbolID) {
      auto It = NativeSymbols.find(Current->TargetSymbolID);
      if (It == NativeSymbols.end())
        return adapterError("ELF relocation retarget requires a native symbol");
      Native.sym = It->second;
    }
  }

  // Materialize plugin-added sections/atoms before native layout. This is the
  // deterministic rebuild path for graph entities with no native identity.
  if (State < NEVERC_LINK_STATE_THUNKS_RELAXED) {
    std::set<uint64_t> KnownAtoms;
    for (const auto &Entry : NativeAtoms)
      KnownAtoms.insert(Entry.first);
    for (const PluginLinkAtom &Atom : After.atoms()) {
      if (KnownAtoms.count(Atom.ID) != 0)
        continue;
      const PluginLinkSection *Section = After.findSection(Atom.SectionID);
      if (!Section)
        return adapterError("plugin atom has no section during ELF rebuild");
      if (Atom.Content.size() + Atom.ZeroFillSize >
          std::numeric_limits<size_t>::max())
        return adapterError("plugin ELF section exceeds host size limits");
      ArrayRef<uint8_t> Content;
      if (!Atom.Content.empty()) {
        auto *Storage = static_cast<uint8_t *>(
            bAlloc().Allocate(Atom.Content.size(), alignof(uint8_t)));
        std::memcpy(Storage, Atom.Content.data(), Atom.Content.size());
        Content = {Storage, Atom.Content.size()};
      }
      StringRef Name = saver().save(Section->Name);
      auto *Created = make<InputSection>(
          nullptr, nativeSectionFlags(Section->Flags),
          nativeSectionType(Section->Kind),
          static_cast<uint32_t>(std::max<uint64_t>(1, Section->Alignment)),
          Content, Name);
      if (Atom.Content.empty() && Atom.ZeroFillSize != 0) {
        Created->size = Atom.ZeroFillSize;
        Created->bss = true;
      }
      if ((Atom.Flags & NEVERC_LINK_ATOM_LIVE) == 0)
        Created->markDead();
      elfState().inputSections.push_back(Created);
      NativeSections[Section->ID] = Created;
      NativeAtoms[Atom.ID] = Created;
      SectionIDs[Created] = Section->ID;
      AtomIDs[Created] = Atom.ID;
    }
  } else {
    for (const PluginLinkAtom &Atom : After.atoms())
      if (!NativeAtoms.count(Atom.ID))
        return adapterError(
            "plugin added an ELF atom after native layout began");
  }

  for (const PluginLinkSymbol &SymbolValue : After.symbols()) {
    if (NativeSymbols.count(SymbolValue.ID))
      continue;
    StringRef Name = saver().save(SymbolValue.Name);
    Symbol *Created = nullptr;
    if (SymbolValue.Definition == NEVERC_LINK_SYMBOL_DEFINED &&
        SymbolValue.AtomID != 0) {
      auto It = NativeAtoms.find(SymbolValue.AtomID);
      if (It == NativeAtoms.end())
        return adapterError("plugin ELF symbol targets a non-native atom");
      Created = symtab.addSymbol(
          Defined{nullptr, Name, nativeBinding(SymbolValue.Binding),
                  nativeVisibility(SymbolValue.Visibility),
                  nativeSymbolType(SymbolValue.Type), SymbolValue.Value,
                  SymbolValue.Size, It->second});
    } else {
      Created = symtab.addSymbol(
          Undefined{nullptr, Name, nativeBinding(SymbolValue.Binding),
                    nativeVisibility(SymbolValue.Visibility),
                    nativeSymbolType(SymbolValue.Type)});
    }
    NativeSymbols[SymbolValue.ID] = Created;
    SymbolIDs[Created] = SymbolValue.ID;
  }

  for (const PluginLinkConstraint &Constraint : After.constraints()) {
    if (Constraint.Kind == "image-base") {
      config->imageBase = Constraint.Value;
    } else if (Constraint.Kind == "section-address") {
      auto It = NativeOutputSections.find(Constraint.SubjectID);
      if (It != NativeOutputSections.end()) {
        const uint64_t Address = Constraint.Value;
        It->second->addrExpr = [Address] { return Address; };
      } else {
        auto Input = NativeSections.find(Constraint.SubjectID);
        if (Input != NativeSections.end())
          config->sectionStartMap[Input->second->name] = Constraint.Value;
      }
    } else if (Constraint.Required && Constraint.Kind != "allow-wx" &&
               Constraint.Kind != "file-base" &&
               Constraint.Kind != "page-size") {
      return adapterError("unsupported required ELF layout constraint '" +
                          Constraint.Kind + "'");
    }
  }

  return Error::success();
}

} // namespace linker::elf
