#include "LinkGraph.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <limits>

using namespace llvm;

namespace neverc::plugin {
namespace {

template <typename Storage>
auto *findByID(Storage &Values, uint64_t ID) {
  auto It =
      std::find_if(Values.begin(), Values.end(),
                   [ID](const auto &Value) { return Value.ID == ID; });
  return It == Values.end() ? nullptr : &*It;
}

template <typename Storage, typename Value>
Value &appendEntity(Storage &Values, Value Entity, uint64_t ID) {
  Entity.ID = ID;
  Values.push_back(std::move(Entity));
  return Values.back();
}

void appendID(raw_ostream &OS, NevercInterfaceID ID) {
  OS << format_hex_no_prefix(ID.High, 16) << ":"
     << format_hex_no_prefix(ID.Low, 16);
}

void appendBytes(raw_ostream &OS, ArrayRef<uint8_t> Bytes) {
  OS << Bytes.size() << ":";
  for (uint8_t Byte : Bytes)
    OS << format_hex_no_prefix(Byte, 2);
}

void appendString(raw_ostream &OS, StringRef Value) {
  OS << Value.size() << ":" << Value;
}

void appendExtensions(raw_ostream &OS,
                      const PluginLinkExtensionSet &Extensions) {
  std::vector<std::string> Records;
  for (const PluginLinkExtensionData &Extension : Extensions.values()) {
    std::string Record;
    raw_string_ostream Stream(Record);
    appendID(Stream, Extension.NamespaceID);
    Stream << ":" << Extension.Version << ":" << Extension.Required << ":";
    appendBytes(Stream, Extension.Payload);
    Stream << ":";
    appendString(Stream, Extension.Digest);
    Stream.flush();
    Records.push_back(std::move(Record));
  }
  llvm::sort(Records);
  for (const std::string &Record : Records)
    appendString(OS, Record);
}

template <typename Storage, typename Encoder>
void appendSorted(raw_ostream &OS, const Storage &Values, Encoder Encode) {
  std::vector<std::string> Records;
  Records.reserve(Values.size());
  for (const auto &Value : Values) {
    std::string Record;
    raw_string_ostream Stream(Record);
    Encode(Stream, Value);
    Stream.flush();
    Records.push_back(std::move(Record));
  }
  llvm::sort(Records);
  OS << Records.size() << ";";
  for (const std::string &Record : Records)
    appendString(OS, Record);
}

std::string sectionKey(const PluginLinkGraph &Graph, uint64_t ID) {
  const PluginLinkSection *Section = Graph.findSection(ID);
  return Section ? Section->Name : std::string();
}

std::string atomKey(const PluginLinkGraph &Graph, uint64_t ID) {
  const PluginLinkAtom *Atom = Graph.findAtom(ID);
  if (!Atom)
    return {};
  return sectionKey(Graph, Atom->SectionID) + "/" + Atom->Name;
}

std::string symbolKey(const PluginLinkGraph &Graph, uint64_t ID) {
  const PluginLinkSymbol *Symbol = Graph.findSymbol(ID);
  if (!Symbol)
    return {};
  return Symbol->Name + "@" + Symbol->Version;
}

std::string comdatKey(const PluginLinkGraph &Graph, uint64_t ID) {
  const PluginLinkComdat *Comdat = Graph.findComdat(ID);
  return Comdat ? Comdat->Name : std::string();
}

std::string inputKey(const PluginLinkGraph &Graph, uint64_t ID) {
  const PluginLinkInput *Input = Graph.findInput(ID);
  return Input ? Input->LogicalURI : std::string();
}

} // namespace

NevercStructArrayView PluginLinkExtensionSet::view() const {
  Wire.clear();
  Wire.reserve(Values.size());
  for (const PluginLinkExtensionData &Value : Values) {
    NevercLinkExtension Extension{};
    Extension.Header = {sizeof(Extension), NEVERC_LINK_API_MAJOR,
                        NEVERC_LINK_API_MINOR, 0};
    Extension.NamespaceID = Value.NamespaceID;
    Extension.Version = Value.Version;
    Extension.Required = Value.Required ? NEVERC_TRUE : NEVERC_FALSE;
    Extension.Payload = {Value.Payload.data(), Value.Payload.size()};
    Extension.Digest = {Value.Digest.data(), Value.Digest.size()};
    Wire.push_back(Extension);
  }
  return {Wire.data(), Wire.size(), sizeof(NevercLinkExtension)};
}

PluginLinkGraph::PluginLinkGraph(OwnedTargetKey TargetValue,
                                 NevercLinkState StateValue)
    : Target(std::move(TargetValue)), State(StateValue) {}

uint64_t PluginLinkGraph::allocateEntityID() {
  if (NextEntityID == 0)
    return 0;
  uint64_t ID = NextEntityID++;
  if (NextEntityID == std::numeric_limits<uint64_t>::max())
    NextEntityID = 0;
  return ID;
}

void PluginLinkGraph::advanceGeneration() {
  Generation =
      Generation == std::numeric_limits<uint64_t>::max() ? 1 : Generation + 1;
}

PluginLinkInput &PluginLinkGraph::addInput(PluginLinkInput Value) {
  return appendEntity(Inputs, std::move(Value), allocateEntityID());
}

PluginLinkArchive &PluginLinkGraph::addArchive(PluginLinkArchive Value) {
  return appendEntity(Archives, std::move(Value), allocateEntityID());
}

PluginLinkArchiveMember &
PluginLinkGraph::addArchiveMember(PluginLinkArchiveMember Value) {
  return appendEntity(ArchiveMembers, std::move(Value), allocateEntityID());
}

PluginLinkSharedLibrary &
PluginLinkGraph::addSharedLibrary(PluginLinkSharedLibrary Value) {
  return appendEntity(SharedLibraries, std::move(Value), allocateEntityID());
}

PluginLinkBitcodeModule &
PluginLinkGraph::addBitcodeModule(PluginLinkBitcodeModule Value) {
  return appendEntity(BitcodeModules, std::move(Value), allocateEntityID());
}

PluginLinkSection &PluginLinkGraph::addSection(PluginLinkSection Value) {
  return appendEntity(Sections, std::move(Value), allocateEntityID());
}

PluginLinkAtom &PluginLinkGraph::addAtom(PluginLinkAtom Value) {
  return appendEntity(Atoms, std::move(Value), allocateEntityID());
}

PluginLinkSymbol &PluginLinkGraph::addSymbol(PluginLinkSymbol Value) {
  return appendEntity(Symbols, std::move(Value), allocateEntityID());
}

PluginLinkEdge &PluginLinkGraph::addEdge(PluginLinkEdge Value) {
  return appendEntity(Edges, std::move(Value), allocateEntityID());
}

PluginLinkComdat &PluginLinkGraph::addComdat(PluginLinkComdat Value) {
  return appendEntity(Comdats, std::move(Value), allocateEntityID());
}

PluginLinkImport &PluginLinkGraph::addImport(PluginLinkImport Value) {
  return appendEntity(Imports, std::move(Value), allocateEntityID());
}

PluginLinkExport &PluginLinkGraph::addExport(PluginLinkExport Value) {
  return appendEntity(Exports, std::move(Value), allocateEntityID());
}

PluginLinkUnwindRecord &
PluginLinkGraph::addUnwind(PluginLinkUnwindRecord Value) {
  return appendEntity(UnwindRecords, std::move(Value), allocateEntityID());
}

PluginLinkSynthetic &
PluginLinkGraph::addSynthetic(PluginLinkSynthetic Value) {
  return appendEntity(Synthetics, std::move(Value), allocateEntityID());
}

PluginLinkConstraint &
PluginLinkGraph::addConstraint(PluginLinkConstraint Value) {
  return appendEntity(Constraints, std::move(Value), allocateEntityID());
}

#define NEVERC_DEFINE_LINK_FIND(Name, StorageName)                            \
  PluginLink##Name *PluginLinkGraph::find##Name(uint64_t ID) {               \
    return findByID(StorageName, ID);                                         \
  }                                                                           \
  const PluginLink##Name *PluginLinkGraph::find##Name(uint64_t ID) const {    \
    return findByID(StorageName, ID);                                         \
  }

NEVERC_DEFINE_LINK_FIND(Input, Inputs)
NEVERC_DEFINE_LINK_FIND(Archive, Archives)
NEVERC_DEFINE_LINK_FIND(ArchiveMember, ArchiveMembers)
NEVERC_DEFINE_LINK_FIND(SharedLibrary, SharedLibraries)
NEVERC_DEFINE_LINK_FIND(BitcodeModule, BitcodeModules)
NEVERC_DEFINE_LINK_FIND(Section, Sections)
NEVERC_DEFINE_LINK_FIND(Atom, Atoms)
NEVERC_DEFINE_LINK_FIND(Symbol, Symbols)
NEVERC_DEFINE_LINK_FIND(Edge, Edges)
NEVERC_DEFINE_LINK_FIND(Comdat, Comdats)
NEVERC_DEFINE_LINK_FIND(Import, Imports)
NEVERC_DEFINE_LINK_FIND(Export, Exports)
NEVERC_DEFINE_LINK_FIND(Synthetic, Synthetics)
NEVERC_DEFINE_LINK_FIND(Constraint, Constraints)

#undef NEVERC_DEFINE_LINK_FIND

PluginLinkUnwindRecord *PluginLinkGraph::findUnwind(uint64_t ID) {
  return findByID(UnwindRecords, ID);
}

const PluginLinkUnwindRecord *
PluginLinkGraph::findUnwind(uint64_t ID) const {
  return findByID(UnwindRecords, ID);
}

std::array<uint8_t, 32> PluginLinkGraph::semanticDigest() const {
  std::string Canonical;
  raw_string_ostream OS(Canonical);
  const NevercTargetKey Key = targetKey();
  appendID(OS, Key.TargetID);
  OS << ";";
  appendID(OS, Key.ObjectFormatID);
  OS << ";";

  appendSorted(OS, Inputs, [](raw_ostream &Stream,
                              const PluginLinkInput &Input) {
    Stream << Input.Kind << ":" << Input.Flags << ":" << Input.Ordinal << ":";
    appendString(Stream, Input.LogicalURI);
    appendBytes(Stream, Input.ContentDigest);
    appendString(Stream, Input.ReaderRoute);
    appendExtensions(Stream, Input.Extensions);
  });
  appendSorted(OS, Archives, [&](raw_ostream &Stream,
                                 const PluginLinkArchive &Archive) {
    appendString(Stream, inputKey(*this, Archive.InputID));
    appendString(Stream, Archive.Name);
    Stream << ":" << Archive.Thin;
    appendExtensions(Stream, Archive.Extensions);
  });
  appendSorted(OS, ArchiveMembers,
               [&](raw_ostream &Stream,
                   const PluginLinkArchiveMember &Member) {
                 appendString(Stream, inputKey(*this, Member.InputID));
                 const PluginLinkArchive *Archive =
                     findArchive(Member.ArchiveID);
                 appendString(Stream,
                              Archive ? Archive->Name : std::string());
                 appendString(Stream, Member.Name);
                 Stream << ":" << Member.Ordinal << ":"
                        << Member.Materialized << ":";
                 appendBytes(Stream, Member.ContentDigest);
                 appendString(Stream, Member.MaterializationReason);
                 appendExtensions(Stream, Member.Extensions);
               });
  appendSorted(OS, SharedLibraries,
               [&](raw_ostream &Stream,
                   const PluginLinkSharedLibrary &Library) {
                 appendString(Stream, inputKey(*this, Library.InputID));
                 appendString(Stream, Library.Name);
                 appendString(Stream, Library.InstallName);
                 appendBytes(Stream, Library.ContentDigest);
                 std::vector<std::string> Needed =
                     Library.NeededLibraries;
                 llvm::sort(Needed);
                 for (StringRef Name : Needed)
                   appendString(Stream, Name);
                 appendExtensions(Stream, Library.Extensions);
               });
  appendSorted(OS, BitcodeModules,
               [&](raw_ostream &Stream,
                   const PluginLinkBitcodeModule &Module) {
                 appendString(Stream, inputKey(*this, Module.InputID));
                 appendString(Stream, Module.Name);
                 appendString(Stream, Module.ModuleIdentifier);
                 appendString(Stream, Module.TargetTriple);
                 appendString(Stream, Module.DataLayout);
                 appendString(Stream, Module.ProducerBuild);
                 appendBytes(Stream, Module.ContentDigest);
                 appendBytes(Stream, Module.SummaryDigest);
                 Stream << ":" << Module.HasSummary << ":";
                 std::vector<std::string> Symbols;
                 Symbols.reserve(Module.Symbols.size());
                 for (const PluginLinkBitcodeSymbol &Symbol :
                      Module.Symbols) {
                   std::string Record;
                   raw_string_ostream SymbolStream(Record);
                   appendString(SymbolStream, Symbol.Name);
                   appendString(SymbolStream, Symbol.ComdatName);
                   SymbolStream << ":" << Symbol.Visibility << ":"
                                << Symbol.Undefined << ":"
                                << Symbol.Weak << ":" << Symbol.Common
                                << ":" << Symbol.TLS << ":"
                                << Symbol.Executable << ":"
                                << Symbol.Used << ":"
                                << Symbol.CommonSize << ":"
                                << Symbol.CommonAlignment;
                   SymbolStream.flush();
                   Symbols.push_back(std::move(Record));
                 }
                 llvm::sort(Symbols);
                 for (StringRef Symbol : Symbols)
                   appendString(Stream, Symbol);
                 appendExtensions(Stream, Module.Extensions);
               });
  appendSorted(OS, Comdats, [&](raw_ostream &Stream,
                                const PluginLinkComdat &Comdat) {
    appendString(Stream, Comdat.Name);
    Stream << ":" << Comdat.Selection << ":"
           << comdatKey(*this, Comdat.SelectedID);
    appendExtensions(Stream, Comdat.Extensions);
  });
  appendSorted(OS, Sections, [&](raw_ostream &Stream,
                                 const PluginLinkSection &Section) {
    appendString(Stream, Section.Name);
    Stream << ":" << Section.Kind << ":" << Section.Flags << ":"
           << Section.Alignment << ":" << Section.Size << ":";
    appendString(Stream, comdatKey(*this, Section.ComdatID));
    appendExtensions(Stream, Section.Extensions);
  });
  appendSorted(OS, Atoms, [&](raw_ostream &Stream,
                              const PluginLinkAtom &Atom) {
    appendString(Stream, sectionKey(*this, Atom.SectionID));
    appendString(Stream, Atom.Name);
    Stream << ":" << Atom.Flags << ":" << Atom.Alignment << ":"
           << Atom.ZeroFillSize << ":";
    appendBytes(Stream, Atom.Content);
    appendString(Stream, comdatKey(*this, Atom.ComdatID));
    appendString(Stream, atomKey(*this, Atom.FoldLeaderID));
    appendExtensions(Stream, Atom.Extensions);
  });
  appendSorted(OS, Symbols, [&](raw_ostream &Stream,
                                const PluginLinkSymbol &Symbol) {
    appendString(Stream, Symbol.Name);
    appendString(Stream, Symbol.Version);
    Stream << ":" << Symbol.Binding << ":" << Symbol.Visibility << ":"
           << Symbol.Definition << ":" << Symbol.Type << ":"
           << Symbol.Value << ":" << Symbol.Size << ":"
           << Symbol.IsPrevailing << ":" << Symbol.IsExported << ":"
           << Symbol.IsImported << ":" << Symbol.IsRoot << ":";
    appendString(Stream, atomKey(*this, Symbol.AtomID));
    appendExtensions(Stream, Symbol.Extensions);
  });
  appendSorted(OS, Edges, [&](raw_ostream &Stream,
                              const PluginLinkEdge &Edge) {
    Stream << Edge.Kind << ":";
    appendString(Stream, atomKey(*this, Edge.SourceAtomID));
    Stream << ":" << Edge.Offset << ":" << Edge.RelocationKind << ":"
           << Edge.Width << ":" << Edge.Addend << ":" << Edge.IsPCRelative
           << ":" << Edge.IsSigned << ":";
    appendString(Stream, symbolKey(*this, Edge.TargetSymbolID));
    appendString(Stream, atomKey(*this, Edge.TargetAtomID));
    appendExtensions(Stream, Edge.Extensions);
  });
  appendSorted(OS, Imports, [&](raw_ostream &Stream,
                                const PluginLinkImport &Import) {
    appendString(Stream, Import.Name);
    appendString(Stream, Import.Library);
    appendString(Stream, symbolKey(*this, Import.SymbolID));
    appendExtensions(Stream, Import.Extensions);
  });
  appendSorted(OS, Exports, [&](raw_ostream &Stream,
                                const PluginLinkExport &Export) {
    appendString(Stream, Export.Name);
    appendString(Stream, symbolKey(*this, Export.SymbolID));
    appendExtensions(Stream, Export.Extensions);
  });
  appendSorted(OS, UnwindRecords,
               [&](raw_ostream &Stream,
                   const PluginLinkUnwindRecord &Unwind) {
                 appendString(Stream, atomKey(*this, Unwind.AtomID));
                 appendString(
                     Stream,
                     symbolKey(*this, Unwind.PersonalitySymbolID));
                 appendExtensions(Stream, Unwind.Extensions);
               });
  appendSorted(OS, Synthetics, [&](raw_ostream &Stream,
                                   const PluginLinkSynthetic &Synthetic) {
    appendString(Stream, Synthetic.Role);
    appendString(Stream, sectionKey(*this, Synthetic.SectionID));
    appendString(Stream, atomKey(*this, Synthetic.AtomID));
    appendExtensions(Stream, Synthetic.Extensions);
  });
  appendSorted(OS, Constraints,
               [&](raw_ostream &Stream,
                   const PluginLinkConstraint &Constraint) {
                 appendString(Stream, Constraint.Kind);
                 Stream << ":" << Constraint.SubjectID << ":"
                        << Constraint.Value << ":" << Constraint.Required;
                 appendExtensions(Stream, Constraint.Extensions);
               });
  OS.flush();
  return SHA256::hash(ArrayRef<uint8_t>(
      reinterpret_cast<const uint8_t *>(Canonical.data()),
      Canonical.size()));
}

} // namespace neverc::plugin
