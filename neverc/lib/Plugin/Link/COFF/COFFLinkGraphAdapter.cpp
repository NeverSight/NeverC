#include "COFFLinkGraphAdapter.h"
#include "neverc/Linker/COFF/COFFLinkerContext.h"
#include "neverc/Linker/COFF/Chunks.h"
#include "neverc/Linker/COFF/Config.h"
#include "neverc/Linker/COFF/Emit.h"
#include "neverc/Linker/COFF/InputFiles.h"
#include "neverc/Linker/COFF/Symbols.h"
#include "neverc/Plugin/Host/BuiltinTargetProvider.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/SHA256.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>

using namespace llvm;
using namespace llvm::COFF;
using namespace neverc::plugin;

namespace linker::coff {
namespace {

constexpr NevercInterfaceID COFFFileExtension{UINT64_C(0x4e43434f46464649),
                                              UINT64_C(1)};
constexpr NevercInterfaceID COFFSectionExtension{UINT64_C(0x4e43434f46465345),
                                                 UINT64_C(1)};
constexpr NevercInterfaceID COFFSymbolExtension{UINT64_C(0x4e43434f46465359),
                                                UINT64_C(1)};
constexpr NevercInterfaceID COFFRelocationExtension{
    UINT64_C(0x4e43434f46465245), UINT64_C(1)};

Error adapterError(const Twine &Message) {
  return createStringError(errc::invalid_argument,
                           "COFF LinkGraph adapter: " + Message);
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

template <typename Map, typename Key, typename Add>
uint64_t ensureID(Map &IDs, Key Native, Add AddEntity) {
  auto It = IDs.find(Native);
  if (It != IDs.end())
    return It->second;
  const uint64_t ID = AddEntity().ID;
  IDs[Native] = ID;
  return ID;
}

NevercObjectSectionFlags sectionFlags(uint32_t Characteristics) {
  NevercObjectSectionFlags Flags = 0;
  if ((Characteristics & IMAGE_SCN_MEM_READ) != 0)
    Flags |= NEVERC_OBJECT_SECTION_ALLOCATED;
  if ((Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0)
    Flags |= NEVERC_OBJECT_SECTION_EXECUTABLE;
  if ((Characteristics & IMAGE_SCN_MEM_WRITE) != 0)
    Flags |= NEVERC_OBJECT_SECTION_WRITABLE;
  if ((Characteristics & IMAGE_SCN_MEM_DISCARDABLE) != 0)
    Flags |= NEVERC_OBJECT_SECTION_DISCARDABLE;
  return Flags;
}

NevercObjectSectionKind sectionKind(uint32_t Characteristics, StringRef Name) {
  if (Name.starts_with(".debug"))
    return NEVERC_OBJECT_SECTION_KIND_DEBUG;
  if (Name == ".pdata" || Name == ".xdata")
    return NEVERC_OBJECT_SECTION_KIND_UNWIND;
  if ((Characteristics & IMAGE_SCN_CNT_CODE) != 0 ||
      (Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0)
    return NEVERC_OBJECT_SECTION_KIND_TEXT;
  if ((Characteristics & IMAGE_SCN_CNT_UNINITIALIZED_DATA) != 0)
    return NEVERC_OBJECT_SECTION_KIND_ZERO_FILL;
  if ((Characteristics & IMAGE_SCN_MEM_WRITE) == 0)
    return NEVERC_OBJECT_SECTION_KIND_READ_ONLY_DATA;
  return NEVERC_OBJECT_SECTION_KIND_DATA;
}

NevercLinkComdatSelection comdatSelection(COMDATType Selection) {
  switch (Selection) {
  case IMAGE_COMDAT_SELECT_NODUPLICATES:
    return NEVERC_LINK_COMDAT_NO_DUPLICATES;
  case IMAGE_COMDAT_SELECT_SAME_SIZE:
    return NEVERC_LINK_COMDAT_SAME_SIZE;
  case IMAGE_COMDAT_SELECT_EXACT_MATCH:
    return NEVERC_LINK_COMDAT_EXACT_MATCH;
  case IMAGE_COMDAT_SELECT_LARGEST:
    return NEVERC_LINK_COMDAT_LARGEST;
  case IMAGE_COMDAT_SELECT_NEWEST:
    return NEVERC_LINK_COMDAT_NEWEST;
  case IMAGE_COMDAT_SELECT_ANY:
  case IMAGE_COMDAT_SELECT_ASSOCIATIVE:
  default:
    return NEVERC_LINK_COMDAT_ANY;
  }
}

bool isPCRelativeRelocation(MachineTypes Machine, uint16_t Type) {
  if (Machine == AMD64)
    return Type >= IMAGE_REL_AMD64_REL32 && Type <= IMAGE_REL_AMD64_REL32_5;
  if (Machine == ARM64)
    return Type == IMAGE_REL_ARM64_BRANCH26 ||
           Type == IMAGE_REL_ARM64_PAGEBASE_REL21 ||
           Type == IMAGE_REL_ARM64_REL21;
  return false;
}

uint8_t relocationWidth(MachineTypes Machine, uint16_t Type) {
  // The LinkGraph edge width is expressed in bits ({0,8,16,32,64}), not bytes.
  if (Machine == AMD64 && Type == IMAGE_REL_AMD64_ADDR64)
    return 64;
  if ((Machine == AMD64 && Type == IMAGE_REL_AMD64_SECTION) ||
      (Machine == ARM64 && Type == IMAGE_REL_ARM64_SECTION))
    return 16;
  return 32;
}

PluginLinkOriginData originFor(const DenseMap<const InputFile *, uint64_t> &IDs,
                               const InputFile *File) {
  PluginLinkOriginData Origin;
  if (File) {
    auto It = IDs.find(File);
    if (It != IDs.end())
      Origin.InputID = It->second;
  }
  Origin.CreatedByProvider = "neverc.builtin.coff";
  return Origin;
}

InputFile *fileForChunk(Chunk *Native) {
  if (auto *Section = dyn_cast_or_null<SectionChunk>(Native))
    return Section->file;
  return nullptr;
}

bool chunkIsLive(Chunk *Native) {
  if (auto *Section = dyn_cast_or_null<SectionChunk>(Native))
    return Section->live && Section->repl == Section;
  return Native != nullptr;
}

StringRef chunkName(Chunk *Native, const COFFLinkerContext &Context) {
  if (!Native)
    return {};
  if (auto *Section = dyn_cast<SectionChunk>(Native))
    return Section->getSectionName();
  if (Native->getOutputSectionIdx() != 0)
    if (OutputSection *Output = Context.getOutputSection(Native))
      return Output->name;
  return Native->getDebugName();
}

} // namespace

COFFLinkGraphAdapter::COFFLinkGraphAdapter(
    PluginTaskContext &TaskValue, COFFLinkerContext &ContextValue,
    std::shared_ptr<PluginLinkGraph> GraphValue)
    : Task(TaskValue), Context(ContextValue), Graph(std::move(GraphValue)) {}

COFFLinkGraphAdapter::~COFFLinkGraphAdapter() = default;

Expected<std::unique_ptr<COFFLinkGraphAdapter>> COFFLinkGraphAdapter::create(
    PluginTaskContext &Task, COFFLinkerContext &Context, StringRef TargetTriple,
    StringRef CPU, NevercTargetRelocationModel RelocationModel) {
  if (TargetTriple.empty())
    return adapterError("target triple is required");
  const BuiltinTargetRoute *Route = findBuiltinTargetRoute(TargetTriple);
  if (!Route || Route->ObjectFormat != BuiltinObjectFormat::COFF)
    return adapterError("target triple has no built-in COFF route");
  auto TargetKey =
      createBuiltinTargetKey(*Route, TargetTriple, CPU, RelocationModel);
  if (!TargetKey)
    return joinErrors(adapterError("could not create the target key"),
                      TargetKey.takeError());
  auto Graph = std::make_shared<PluginLinkGraph>(std::move(*TargetKey),
                                                 NEVERC_LINK_STATE_INITIAL);
  return std::unique_ptr<COFFLinkGraphAdapter>(
      new COFFLinkGraphAdapter(Task, Context, std::move(Graph)));
}

Expected<std::shared_ptr<PluginLinkGraph>>
COFFLinkGraphAdapter::capture(const PluginLinkGraph &Previous,
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
    Input->ReaderRoute = "neverc.builtin.coff";
    if (File->lazy)
      Input->Flags |= NEVERC_LINK_INPUT_FLAG_LAZY;
    std::vector<uint8_t> Payload;
    appendU32(Payload, File->kind());
    // LTO takes ownership of a bitcode file's lto::InputFile when it adds it,
    // so BitcodeFile::getMachineType() dereferences null on every capture that
    // runs after LTO. Such a file can no longer state a machine type.
    auto *Bitcode = dyn_cast<BitcodeFile>(File);
    appendU32(Payload, Bitcode && !Bitcode->obj ? IMAGE_FILE_MACHINE_UNKNOWN
                                                : File->getMachineType());
    appendU32(Payload, File->lazy ? 1 : 0);
    appendU32(Payload, File->builtFromBitcode ? 1 : 0);
    setExtension(Input->Extensions, COFFFileExtension, 1, std::move(Payload));
    return ID;
  };

  for (ObjFile *File : Context.objFileInstances)
    CaptureInput(File, NEVERC_LINK_INPUT_OBJECT);
  for (ImportFile *File : Context.importFileInstances)
    CaptureInput(File, NEVERC_LINK_INPUT_SHARED_LIBRARY);
  for (BitcodeFile *File : Context.bitcodeFileInstances)
    CaptureInput(File, NEVERC_LINK_INPUT_BITCODE);

  const bool HasLayout = State >= NEVERC_LINK_STATE_LAYOUT_COMPLETE;
  if (HasLayout) {
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
    Section->Name = Native->name.empty() ? "<coff-output>" : Native->name.str();
    Section->Kind = sectionKind(Native->header.Characteristics, Native->name);
    Section->Flags = sectionFlags(Native->header.Characteristics);
    Section->Address =
        HasLayout ? Context.config.imageBase + Native->getRVA() : 0;
    Section->FileOffset = HasLayout ? Native->getFileOff() : 0;
    Section->Size = HasLayout ? Native->getVirtualSize() : 0;
    // PE uses distinct memory (SectionAlignment) and disk (FileAlignment)
    // alignments, so the generic verifier's requirement that the address and
    // file offset both divide the reported alignment only holds for the largest
    // power of two dividing both. Derive it from the frozen layout values.
    if (HasLayout) {
      const uint64_t AddrAlign =
          Section->Address == 0 ? (UINT64_C(1) << 32)
                                : (Section->Address & (0 - Section->Address));
      const uint64_t OffsetAlign =
          Section->FileOffset == 0
              ? (UINT64_C(1) << 32)
              : (Section->FileOffset & (0 - Section->FileOffset));
      Section->Alignment = std::min(AddrAlign, OffsetAlign);
    } else {
      Section->Alignment = Context.config.align == 0 ? 1 : Context.config.align;
    }
    Section->Origin.CreatedByProvider = "neverc.builtin.coff";
    std::vector<uint8_t> Payload;
    appendU32(Payload, Native->header.Characteristics);
    appendU32(Payload, Native->sectionIndex);
    appendU32(Payload, Native->header.SizeOfRawData);
    appendU32(Payload, Native->header.VirtualSize);
    setExtension(Section->Extensions, COFFSectionExtension, 1,
                 std::move(Payload));
    AllowWX |= (Section->Flags & NEVERC_OBJECT_SECTION_WRITABLE) != 0 &&
               (Section->Flags & NEVERC_OBJECT_SECTION_EXECUTABLE) != 0;
    return ID;
  };

  if (HasLayout)
    for (OutputSection *Section : Context.outputSections)
      CaptureOutputSection(Section);

  DenseSet<Chunk *> SeenChunks;
  SmallVector<Chunk *, 0> Chunks;
  const auto AddChunk = [&](Chunk *ChunkValue) {
    if (ChunkValue && SeenChunks.insert(ChunkValue).second)
      Chunks.push_back(ChunkValue);
  };
  for (ObjFile *File : Context.objFileInstances)
    for (Chunk *ChunkValue : File->getChunks())
      AddChunk(ChunkValue);
  for (Chunk *ChunkValue : Context.symtab.getChunks())
    AddChunk(ChunkValue);
  for (OutputSection *Section : Context.outputSections)
    if (Section)
      for (Chunk *ChunkValue : Section->chunks)
        AddChunk(ChunkValue);

  // COMDAT sections that share a leader name form one selection group. Every
  // same-named candidate must reference a single winning group ID, so collapse
  // them into one canonical PluginLinkComdat instead of self-selecting each.
  std::map<std::string, uint64_t> ComdatByName;
  const auto CaptureChunk = [&](Chunk *Native) -> uint64_t {
    if (!Native)
      return 0;
    const StringRef NativeName = chunkName(Native, Context);
    const uint32_t Characteristics = Native->getOutputCharacteristics();
    const uint64_t SectionID =
        ensureID(SectionIDs, Native, [&]() -> PluginLinkSection & {
          return Result->addSection(PluginLinkSection{});
        });
    NativeSections[SectionID] = Native;
    PluginLinkSection *Section = Result->findSection(SectionID);
    if (!Section)
      return 0;
    Section->Name = NativeName.empty() ? "<coff-chunk>" : NativeName.str();
    Section->Kind = sectionKind(Characteristics, NativeName);
    Section->Flags = sectionFlags(Characteristics);
    Section->Alignment = std::max<uint32_t>(1, Native->getAlignment());
    Section->Origin = originFor(InputIDs, fileForChunk(Native));
    Section->Address = 0;
    Section->FileOffset = 0;
    Section->Size = HasLayout ? 0 : Native->getSize();
    std::vector<uint8_t> SectionPayload;
    appendU32(SectionPayload, Characteristics);
    appendU32(SectionPayload, Native->kind());
    appendU32(SectionPayload, Native->getMachine());
    appendU32(SectionPayload, Native->getOutputSectionIdx());
    setExtension(Section->Extensions, COFFSectionExtension, 1,
                 std::move(SectionPayload));

    const uint64_t AtomID =
        ensureID(AtomIDs, Native, [&]() -> PluginLinkAtom & {
          return Result->addAtom(PluginLinkAtom{});
        });
    NativeAtoms[AtomID] = Native;
    PluginLinkAtom *Atom = Result->findAtom(AtomID);
    if (!Atom)
      return 0;
    Atom->Name = Section->Name;
    Atom->SectionID = SectionID;
    Atom->Flags = 0;
    if (chunkIsLive(Native))
      Atom->Flags |= NEVERC_LINK_ATOM_LIVE;
    if (auto *Input = dyn_cast<SectionChunk>(Native)) {
      if (Input->keepUnique)
        Atom->Flags |= NEVERC_LINK_ATOM_ADDRESS_SIGNIFICANT;
      if (Input->repl != Input) {
        Atom->FoldLeaderID = AtomIDs.lookup(Input->repl);
        if (Atom->FoldLeaderID != 0)
          Atom->Flags |= NEVERC_LINK_ATOM_FOLDED;
      } else {
        Atom->FoldLeaderID = 0;
      }
    } else {
      Atom->Flags |= NEVERC_LINK_ATOM_SYNTHETIC;
    }
    if (Section->Kind == NEVERC_OBJECT_SECTION_KIND_UNWIND)
      Atom->Flags |= NEVERC_LINK_ATOM_UNWIND;
    Atom->Alignment = Section->Alignment;
    Atom->Origin = Section->Origin;
    Atom->Address = 0;
    Atom->FileOffset = 0;
    Atom->Content.clear();
    Atom->ZeroFillSize = 0;
    if (auto *Input = dyn_cast<SectionChunk>(Native)) {
      if (Input->hasData) {
        ArrayRef<uint8_t> Content = Input->getContents();
        Atom->Content.assign(Content.begin(), Content.end());
      } else {
        Atom->ZeroFillSize = Input->getSize();
      }
    } else {
      // Native synthetic chunks materialize their bytes directly into the
      // output buffer. Preserve their size without exposing a private pointer.
      Atom->ZeroFillSize = Native->getSize();
    }

    if (auto *Input = dyn_cast<SectionChunk>(Native);
        Input && Input->isCOMDAT() && Atom->ComdatID == 0) {
      const std::string ComdatName =
          Input->sym && !Input->sym->getName().empty()
              ? Input->sym->getName().str()
              : Section->Name;
      auto Existing = ComdatByName.find(ComdatName);
      uint64_t ComdatID;
      if (Existing == ComdatByName.end()) {
        PluginLinkComdat Comdat;
        Comdat.Name = ComdatName;
        Comdat.Selection = comdatSelection(Input->selection);
        Comdat.Origin = Section->Origin;
        PluginLinkComdat &Stored = Result->addComdat(std::move(Comdat));
        Stored.SelectedID = Stored.ID;
        ComdatID = Stored.ID;
        ComdatByName.emplace(ComdatName, ComdatID);
      } else {
        ComdatID = Existing->second;
      }
      Atom->ComdatID = ComdatID;
      Section->ComdatID = ComdatID;
    }

    // Once layout is frozen the chunk-level section becomes a zero-sized
    // shell, so a live atom only has somewhere to live if it can be reparented
    // onto the output section that actually holds it. Chunks with no output
    // section (osidx 0, which getOutputSection reports as null) and chunks
    // whose RVA falls outside their output section have no address to report
    // and must not stay live.
    if (HasLayout && chunkIsLive(Native)) {
      OutputSection *Output = Context.getOutputSection(Native);
      const uint64_t OutputID = CaptureOutputSection(Output);
      const uint64_t RVA = Native->getRVA();
      if (OutputID != 0 && RVA >= Output->getRVA() &&
          Native->getSize() <= Output->getVirtualSize() -
                                   std::min<uint64_t>(Output->getVirtualSize(),
                                                      RVA - Output->getRVA())) {
        Atom->SectionID = OutputID;
        Atom->Address = Context.config.imageBase + RVA;
        Atom->FileOffset = Output->getFileOff() + (RVA - Output->getRVA());
      } else {
        Atom->Flags &= ~NEVERC_LINK_ATOM_LIVE;
      }
    }

    if (State >= NEVERC_LINK_STATE_SYNTHETICS_READY &&
        (Atom->Flags & NEVERC_LINK_ATOM_SYNTHETIC) != 0) {
      auto It = llvm::find_if(Result->synthetics(),
                              [&](const PluginLinkSynthetic &Value) {
                                return Value.AtomID == AtomID;
                              });
      if (It == Result->synthetics().end()) {
        PluginLinkSynthetic Synthetic;
        Synthetic.Role = NativeName.empty()
                             ? "coff.synthetic"
                             : ("coff.synthetic." + NativeName).str();
        Synthetic.SectionID = Atom->SectionID;
        Synthetic.AtomID = AtomID;
        Synthetic.Origin = Atom->Origin;
        Result->addSynthetic(std::move(Synthetic));
      } else {
        It->SectionID = Atom->SectionID;
      }
    }

    if (Section->Kind == NEVERC_OBJECT_SECTION_KIND_UNWIND) {
      auto It = llvm::find_if(Result->unwindRecords(),
                              [&](const PluginLinkUnwindRecord &Value) {
                                return Value.AtomID == AtomID;
                              });
      if (It == Result->unwindRecords().end()) {
        PluginLinkUnwindRecord Record;
        Record.AtomID = AtomID;
        Record.Origin = Atom->Origin;
        Result->addUnwind(std::move(Record));
      }
    }
    return AtomID;
  };

  for (size_t Index = 0; Index != Chunks.size(); ++Index)
    CaptureChunk(Chunks[Index]);

  const auto CaptureSymbol = [&](Symbol *Native) -> uint64_t {
    if (!Native || Native->isLazy() || Native->getName().empty())
      return 0;
    // LTO leaves behind undefined symbols that nothing outside bitcode ever
    // referenced: the definition was internalized (or renamed by parallel
    // codegen) and the reference it answered was optimized away with it.
    // resolveRemainingUndefines skips exactly these, so the native link
    // succeeds; projecting them anyway would make the graph report an
    // unresolved symbol that the link does not actually have.
    if (isa<Undefined>(Native) && !Native->isUsedInRegularObj)
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
    // Only the symbol the symbol table actually resolved to is the external
    // winner for this name. Non-external file-local definitions (reached
    // through relocations) share names across translation units, so binding
    // them GLOBAL would make several appear as prevailing definitions of one
    // name. Bind those as LOCAL, which the verifier keys per input/id.
    const bool IsExternalWinner =
        Context.symtab.find(Native->getName()) == Native;
    SymbolValue->Binding = !IsExternalWinner
                               ? NEVERC_LINK_SYMBOL_BINDING_LOCAL
                           : Native->isWeak ? NEVERC_LINK_SYMBOL_BINDING_WEAK
                                            : NEVERC_LINK_SYMBOL_BINDING_GLOBAL;
    SymbolValue->Visibility = NEVERC_LINK_SYMBOL_VISIBILITY_DEFAULT;
    SymbolValue->Type = NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE;
    SymbolValue->Definition = NEVERC_LINK_SYMBOL_UNDEFINED;
    SymbolValue->AtomID = 0;
    SymbolValue->Value = 0;
    SymbolValue->Size = 0;
    SymbolValue->IsImported = false;
    SymbolValue->IsPrevailing = false;
    SymbolValue->IsExported =
        llvm::any_of(Context.config.exports,
                     [&](const Export &Value) { return Value.sym == Native; });
    SymbolValue->IsRoot = Native == Context.config.entry || Native->isGCRoot;
    SymbolValue->Origin = originFor(InputIDs, Native->getFile());

    if (auto *DefinedValue = dyn_cast<Defined>(Native)) {
      if (isa<DefinedAbsolute>(DefinedValue)) {
        SymbolValue->Definition = NEVERC_LINK_SYMBOL_ABSOLUTE;
        SymbolValue->Value = cast<DefinedAbsolute>(DefinedValue)->getVA();
      } else if (isa<DefinedImportData>(DefinedValue) ||
                 isa<DefinedImportThunk>(DefinedValue)) {
        SymbolValue->Definition = NEVERC_LINK_SYMBOL_SHARED;
        SymbolValue->IsImported = true;
      } else if (isa<DefinedCommon>(DefinedValue)) {
        SymbolValue->Definition = NEVERC_LINK_SYMBOL_COMMON;
        SymbolValue->Binding = NEVERC_LINK_SYMBOL_BINDING_COMMON;
      } else {
        SymbolValue->Definition = NEVERC_LINK_SYMBOL_DEFINED;
      }
      if (Chunk *ChunkValue = DefinedValue->getChunk()) {
        SymbolValue->AtomID = CaptureChunk(ChunkValue);
        SymbolValue->Size = ChunkValue->getSize();
        if (auto *Regular = dyn_cast<DefinedRegular>(DefinedValue))
          SymbolValue->Value = Regular->getValue();
      }
      // Synthetic and local-import bodies are defined without an owning chunk,
      // so no atom was captured for them. Represent such chunkless definitions
      // as absolute values; otherwise the verifier rejects a defined symbol
      // whose atom is missing from the graph.
      if (SymbolValue->Definition == NEVERC_LINK_SYMBOL_DEFINED &&
          SymbolValue->AtomID == 0)
        SymbolValue->Definition = NEVERC_LINK_SYMBOL_ABSOLUTE;
      SymbolValue->IsPrevailing = true;
    }

    if (SymbolValue->AtomID != 0) {
      if (const PluginLinkAtom *Atom = Result->findAtom(SymbolValue->AtomID))
        if (const PluginLinkSection *Section =
                Result->findSection(Atom->SectionID))
          if (Section->Kind == NEVERC_OBJECT_SECTION_KIND_TEXT)
            SymbolValue->Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
      if (SymbolValue->IsRoot)
        if (PluginLinkAtom *Atom = Result->findAtom(SymbolValue->AtomID))
          Atom->Flags |= NEVERC_LINK_ATOM_ROOT | NEVERC_LINK_ATOM_LIVE;
    }

    std::vector<uint8_t> Payload;
    appendU32(Payload, Native->kind());
    appendU32(Payload, Native->isWeak ? 1 : 0);
    appendU32(Payload, Native->isCOMDAT ? 1 : 0);
    appendU32(Payload, Native->isGCRoot ? 1 : 0);
    setExtension(SymbolValue->Extensions, COFFSymbolExtension, 1,
                 std::move(Payload));

    if (SymbolValue->IsImported) {
      StringRef Library;
      if (auto *Import = dyn_cast<DefinedImportData>(Native))
        Library = Import->getDLLName();
      else if (auto *Thunk = dyn_cast<DefinedImportThunk>(Native))
        Library = Thunk->wrappedSym->getDLLName();
      auto It =
          llvm::find_if(Result->imports(), [&](const PluginLinkImport &Value) {
            return Value.SymbolID == ID;
          });
      if (It == Result->imports().end()) {
        PluginLinkImport Import;
        Import.Name = SymbolValue->Name;
        Import.Library = Library.str();
        Import.SymbolID = ID;
        Import.Origin = SymbolValue->Origin;
        Result->addImport(std::move(Import));
      } else {
        It->Library = Library.str();
      }
    }
    if (SymbolValue->IsExported) {
      auto It =
          llvm::find_if(Result->exports(), [&](const PluginLinkExport &Value) {
            return Value.SymbolID == ID;
          });
      if (It == Result->exports().end()) {
        PluginLinkExport ExportValue;
        ExportValue.Name = SymbolValue->Name;
        ExportValue.SymbolID = ID;
        ExportValue.Origin = SymbolValue->Origin;
        Result->addExport(std::move(ExportValue));
      }
    }
    return ID;
  };

  SmallVector<Symbol *, 0> Symbols;
  Context.symtab.forEachSymbol(
      [&](Symbol *SymbolValue) { Symbols.push_back(SymbolValue); });
  llvm::sort(Symbols, [](Symbol *Left, Symbol *Right) {
    return Left->getName() < Right->getName();
  });
  for (Symbol *SymbolValue : Symbols)
    CaptureSymbol(SymbolValue);

  for (ObjFile *File : Context.objFileInstances) {
    const ArrayRef<Symbol *> FileSymbols = File->getSymbols();
    for (Chunk *ChunkValue : File->getChunks()) {
      auto *Section = dyn_cast_or_null<SectionChunk>(ChunkValue);
      if (!Section)
        continue;
      const uint64_t SourceAtomID = AtomIDs.lookup(Section);
      if (SourceAtomID == 0)
        continue;
      const ArrayRef<llvm::object::coff_relocation> Relocations =
          Section->getRelocs();
      for (size_t Index = 0; Index != Relocations.size(); ++Index) {
        const llvm::object::coff_relocation &Reloc = Relocations[Index];
        if (Reloc.VirtualAddress >= Section->getSize() ||
            Reloc.SymbolTableIndex >= FileSymbols.size())
          continue;
        Symbol *Target = FileSymbols[Reloc.SymbolTableIndex];
        const uint64_t TargetSymbolID = CaptureSymbol(Target);
        uint64_t TargetAtomID = 0;
        if (TargetSymbolID == 0)
          if (auto *DefinedTarget = dyn_cast_or_null<Defined>(Target))
            if (Chunk *TargetChunk = DefinedTarget->getChunk())
              TargetAtomID = CaptureChunk(TargetChunk);
        if (TargetSymbolID == 0 && TargetAtomID == 0)
          continue;
        // Debug and metadata sections legitimately reference GC-eliminated
        // code in COFF; the native linker relaxes those relocations. Recording
        // such a live-source -> dead-target reference as a hard graph edge
        // would violate the liveness invariant, so drop it from the projection.
        uint64_t ResolvedTargetAtomID = TargetAtomID;
        if (ResolvedTargetAtomID == 0 && TargetSymbolID != 0)
          if (const PluginLinkSymbol *TargetSym =
                  Result->findSymbol(TargetSymbolID))
            ResolvedTargetAtomID = TargetSym->AtomID;
        if (ResolvedTargetAtomID != 0)
          if (const PluginLinkAtom *SourceAtom =
                  Result->findAtom(SourceAtomID))
            if (const PluginLinkAtom *TargetAtom =
                    Result->findAtom(ResolvedTargetAtomID))
              if ((SourceAtom->Flags & NEVERC_LINK_ATOM_LIVE) != 0 &&
                  (TargetAtom->Flags & NEVERC_LINK_ATOM_LIVE) == 0)
                continue;
        const auto Key =
            std::make_pair(static_cast<const SectionChunk *>(Section), Index);
        const uint64_t EdgeID =
            ensureID(RelocationIDs, Key, [&]() -> PluginLinkEdge & {
              return Result->addEdge(PluginLinkEdge{});
            });
        NativeRelocations[EdgeID] = {Section, Index};
        PluginLinkEdge *Edge = Result->findEdge(EdgeID);
        if (!Edge)
          continue;
        Edge->Kind = NEVERC_LINK_EDGE_RELOCATION;
        Edge->SourceAtomID = SourceAtomID;
        Edge->Offset = Reloc.VirtualAddress;
        Edge->RelocationKind = NEVERC_OBJECT_RELOCATION_TARGET_EXTENSION;
        Edge->Width = relocationWidth(Context.config.machine, Reloc.Type);
        Edge->Addend = 0;
        Edge->IsPCRelative =
            isPCRelativeRelocation(Context.config.machine, Reloc.Type);
        Edge->IsSigned = Edge->IsPCRelative;
        Edge->TargetSymbolID = TargetSymbolID;
        Edge->TargetAtomID = TargetAtomID;
        Edge->Origin = originFor(InputIDs, File);
        std::vector<uint8_t> Payload;
        appendU32(Payload, Context.config.machine);
        appendU32(Payload, Reloc.Type);
        setExtension(Edge->Extensions, COFFRelocationExtension, 1,
                     std::move(Payload));
      }
    }
  }

  if (State >= NEVERC_LINK_STATE_GC_COMPLETE)
    for (const auto &[Native, AtomID] : AtomIDs)
      if (PluginLinkAtom *Atom = Result->findAtom(AtomID)) {
        // Once a layout exists, CaptureChunk has already withheld liveness from
        // atoms that survive natively but occupy no output-section range. This
        // pass may only take liveness away, never restore it, or those atoms
        // come back with no address and fail layout verification.
        if (!chunkIsLive(const_cast<Chunk *>(Native)))
          Atom->Flags &= ~NEVERC_LINK_ATOM_LIVE;
        else if (!HasLayout)
          Atom->Flags |= NEVERC_LINK_ATOM_LIVE;
      }

  if (State >= NEVERC_LINK_STATE_ICF_COMPLETE)
    for (const auto &[Native, AtomID] : AtomIDs) {
      auto *Input = dyn_cast<SectionChunk>(const_cast<Chunk *>(Native));
      PluginLinkAtom *Atom = Result->findAtom(AtomID);
      if (!Input || !Atom)
        continue;
      const uint64_t LeaderID =
          Input->repl != Input ? AtomIDs.lookup(Input->repl) : 0;
      PluginLinkAtom *Leader =
          LeaderID != 0 ? Result->findAtom(LeaderID) : nullptr;
      if (Leader) {
        // ICF merged this follower into its leader. The canonical graph models
        // a fold as a live follower that is byte-equivalent to and co-located
        // with the leader, and is not an unwind/TLS/address-significant atom
        // (the leader owns those roles). The native repl pointer stays the
        // source of truth; this projection only has to satisfy the
        // fold/liveness/layout verifiers, which require follower ≡ leader.
        Leader->Flags |= NEVERC_LINK_ATOM_LIVE;
        Atom->FoldLeaderID = LeaderID;
        Atom->Flags |= NEVERC_LINK_ATOM_FOLDED | NEVERC_LINK_ATOM_LIVE;
        Atom->Flags &= ~(NEVERC_LINK_ATOM_UNWIND | NEVERC_LINK_ATOM_TLS |
                         NEVERC_LINK_ATOM_ADDRESS_SIGNIFICANT);
        Atom->SectionID = Leader->SectionID;
        Atom->Address = Leader->Address;
        Atom->FileOffset = Leader->FileOffset;
        Atom->Alignment = Leader->Alignment;
        Atom->Content = Leader->Content;
        Atom->ZeroFillSize = Leader->ZeroFillSize;
      } else {
        Atom->FoldLeaderID = 0;
        Atom->Flags &= ~NEVERC_LINK_ATOM_FOLDED;
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
      Constraint.Origin.CreatedByProvider = "neverc.builtin.coff";
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

Error COFFLinkGraphAdapter::applyDelta(const PluginLinkGraph &Before,
                                       const PluginLinkGraph &After,
                                       NevercLinkState State) {
  for (const auto &[AtomID, Native] : NativeAtoms) {
    const PluginLinkAtom *Old = Before.findAtom(AtomID);
    const PluginLinkAtom *Current = After.findAtom(AtomID);
    auto *Section = dyn_cast<SectionChunk>(Native);
    if (!Current) {
      if (Section)
        Section->live = false;
      continue;
    }
    // Push only the liveness change the plugin actually made. The captured
    // LIVE flag folds native selection/fold state (chunkIsLive), so writing it
    // back unconditionally would corrupt the native linker's own GC result on a
    // no-op replay and discard sections it kept live.
    if (Section && Old &&
        ((Old->Flags & NEVERC_LINK_ATOM_LIVE) !=
         (Current->Flags & NEVERC_LINK_ATOM_LIVE)))
      Section->live = (Current->Flags & NEVERC_LINK_ATOM_LIVE) != 0;
    if (Old && (Old->Content != Current->Content ||
                Old->ZeroFillSize != Current->ZeroFillSize))
      return adapterError(
          "COFF content replacement requires a native graph rebuild");
    if (Old && Old->Alignment != Current->Alignment) {
      if (State >= NEVERC_LINK_STATE_LAYOUT_COMPLETE)
        return adapterError("cannot change COFF alignment after layout");
      if (Current->Alignment == 0 || !isPowerOf2_64(Current->Alignment) ||
          Current->Alignment > (UINT64_C(1) << Log2MaxSectionAlignment))
        return adapterError("plugin requested an invalid COFF alignment");
      Native->setAlignment(static_cast<uint32_t>(Current->Alignment));
    }
    if (Section && Current->FoldLeaderID != 0 &&
        (!Old || Old->FoldLeaderID != Current->FoldLeaderID)) {
      auto It = NativeAtoms.find(Current->FoldLeaderID);
      auto *Leader = It == NativeAtoms.end()
                         ? nullptr
                         : dyn_cast<SectionChunk>(It->second);
      if (!Leader)
        return adapterError("plugin selected a non-native COFF fold leader");
      Section->repl = Leader;
      Section->live = false;
    }
    if (State >= NEVERC_LINK_STATE_LAYOUT_COMPLETE && Current->Address != 0) {
      if (Current->Address < Context.config.imageBase ||
          Current->Address - Context.config.imageBase > UINT32_MAX)
        return adapterError("plugin COFF atom address is outside the image");
      Native->setRVA(Current->Address - Context.config.imageBase);
    }
  }

  for (const auto &[SymbolID, Native] : NativeSymbols) {
    const PluginLinkSymbol *Old = Before.findSymbol(SymbolID);
    const PluginLinkSymbol *Current = After.findSymbol(SymbolID);
    if (!Current)
      return adapterError(
          "removing native COFF symbols requires a graph rebuild");
    if (!Old || Old->IsRoot != Current->IsRoot)
      Native->isGCRoot = Current->IsRoot;
    if (Old && (Old->Definition != Current->Definition ||
                Old->Binding != Current->Binding ||
                Old->AtomID != Current->AtomID || Old->Value != Current->Value))
      return adapterError(
          "COFF symbol replacement requires a native graph rebuild");
  }

  for (const auto &[EdgeID, NativeRef] : NativeRelocations) {
    const PluginLinkEdge *Old = Before.findEdge(EdgeID);
    const PluginLinkEdge *Current = After.findEdge(EdgeID);
    if (!Current)
      return adapterError(
          "removing native COFF relocations requires a graph rebuild");
    if (Old && (Old->Offset != Current->Offset ||
                Old->RelocationKind != Current->RelocationKind ||
                Old->TargetSymbolID != Current->TargetSymbolID ||
                Old->TargetAtomID != Current->TargetAtomID ||
                Old->Addend != Current->Addend))
      return adapterError(
          "COFF relocation replacement requires a native graph rebuild");
  }

  for (const PluginLinkAtom &Atom : After.atoms())
    if (!NativeAtoms.count(Atom.ID) &&
        (Atom.Flags & NEVERC_LINK_ATOM_SYNTHETIC) == 0)
      return adapterError(
          "replacement COFF graph requires an unsupported native rebuild");
  return Error::success();
}

} // namespace linker::coff
