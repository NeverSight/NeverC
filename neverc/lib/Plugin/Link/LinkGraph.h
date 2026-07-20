#ifndef NEVERC_PLUGIN_LINK_LINKGRAPH_H
#define NEVERC_PLUGIN_LINK_LINKGRAPH_H

#include "neverc/Plugin/Host/PluginTargetDescriptor.h"
#include "neverc/Plugin/PluginLink.h"
#include "llvm/Support/Error.h"
#include <array>
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace neverc::plugin {

class PluginTaskContext;
class LinkMutation;
struct PluginLinkProof;

struct PluginLinkExtensionData {
  NevercInterfaceID NamespaceID{};
  uint32_t Version = 0;
  bool Required = false;
  std::vector<uint8_t> Payload;
  std::string Digest;
};

class PluginLinkExtensionSet {
public:
  std::vector<PluginLinkExtensionData> &values() { return Values; }
  const std::vector<PluginLinkExtensionData> &values() const { return Values; }
  NevercStructArrayView view() const;

private:
  std::vector<PluginLinkExtensionData> Values;
  mutable std::vector<NevercLinkExtension> Wire;
};

struct PluginLinkOriginData {
  uint64_t InputID = 0;
  uint64_t ArchiveMemberID = 0;
  NevercObjectGraphHandle ObjectGraph{};
  uint64_t ObjectEntityID = 0;
  NevercInterfaceID CreatedByPhase{};
  std::string CreatedByProvider;
  NevercInterfaceID LastMutationPhase{};
  std::string LastMutationPlugin;
};

struct PluginLinkInput {
  uint64_t ID = 0;
  NevercLinkInputKind Kind = NEVERC_LINK_INPUT_UNKNOWN;
  NevercLinkInputFlags Flags = NEVERC_LINK_INPUT_FLAG_NONE;
  uint64_t Ordinal = 0;
  std::string LogicalURI;
  std::array<uint8_t, 32> ContentDigest{};
  std::string ReaderRoute;
  NevercObjectGraphHandle ObjectGraph{};
  uint64_t ArchiveID = 0;
  uint64_t SharedLibraryID = 0;
  uint64_t BitcodeModuleID = 0;
  PluginLinkExtensionSet Extensions;
};

struct PluginLinkArchive {
  uint64_t ID = 0;
  uint64_t InputID = 0;
  std::string Name;
  bool Thin = false;
  PluginLinkOriginData Origin;
  PluginLinkExtensionSet Extensions;
};

struct PluginLinkArchiveMember {
  uint64_t ID = 0;
  uint64_t InputID = 0;
  uint64_t ArchiveID = 0;
  std::string Name;
  uint64_t Ordinal = 0;
  std::array<uint8_t, 32> ContentDigest{};
  bool Materialized = false;
  std::string MaterializationReason;
  PluginLinkOriginData Origin;
  PluginLinkExtensionSet Extensions;
};

struct PluginLinkSharedLibrary {
  uint64_t ID = 0;
  uint64_t InputID = 0;
  std::string Name;
  std::string InstallName;
  std::array<uint8_t, 32> ContentDigest{};
  std::vector<std::string> NeededLibraries;
  std::vector<std::string> Exports;
  std::vector<std::string> Imports;
  PluginLinkOriginData Origin;
  PluginLinkExtensionSet Extensions;
};

struct PluginLinkBitcodeSymbol {
  std::string Name;
  std::string ComdatName;
  NevercLinkSymbolVisibility Visibility =
      NEVERC_LINK_SYMBOL_VISIBILITY_DEFAULT;
  bool Undefined = false;
  bool Weak = false;
  bool Common = false;
  bool TLS = false;
  bool Executable = false;
  bool Used = false;
  uint64_t CommonSize = 0;
  uint64_t CommonAlignment = 0;
};

struct PluginLinkBitcodeModule {
  uint64_t ID = 0;
  uint64_t InputID = 0;
  std::string Name;
  std::string ModuleIdentifier;
  std::string TargetTriple;
  std::string DataLayout;
  std::string ProducerBuild;
  std::array<uint8_t, 32> ContentDigest{};
  std::array<uint8_t, 32> SummaryDigest{};
  bool HasSummary = false;
  std::vector<PluginLinkBitcodeSymbol> Symbols;
  NevercHandle Summary{};
  PluginLinkOriginData Origin;
  PluginLinkExtensionSet Extensions;
};

struct PluginLinkSection {
  uint64_t ID = 0;
  std::string Name;
  NevercObjectSectionKind Kind = NEVERC_OBJECT_SECTION_KIND_DATA;
  NevercObjectSectionFlags Flags = 0;
  uint64_t Alignment = 1;
  uint64_t Address = 0;
  uint64_t FileOffset = 0;
  uint64_t Size = 0;
  uint64_t ComdatID = 0;
  PluginLinkOriginData Origin;
  PluginLinkExtensionSet Extensions;
};

struct PluginLinkAtom {
  uint64_t ID = 0;
  uint64_t SectionID = 0;
  std::string Name;
  NevercLinkAtomFlags Flags = 0;
  uint64_t Alignment = 1;
  uint64_t Address = 0;
  uint64_t FileOffset = 0;
  std::vector<uint8_t> Content;
  uint64_t ZeroFillSize = 0;
  uint64_t ComdatID = 0;
  uint64_t FoldLeaderID = 0;
  PluginLinkOriginData Origin;
  PluginLinkExtensionSet Extensions;
};

struct PluginLinkSymbol {
  uint64_t ID = 0;
  std::string Name;
  std::string Version;
  NevercLinkSymbolBinding Binding = NEVERC_LINK_SYMBOL_BINDING_LOCAL;
  NevercLinkSymbolVisibility Visibility =
      NEVERC_LINK_SYMBOL_VISIBILITY_DEFAULT;
  NevercLinkSymbolDefinition Definition = NEVERC_LINK_SYMBOL_UNDEFINED;
  NevercObjectSymbolType Type = NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE;
  uint64_t AtomID = 0;
  uint64_t Value = 0;
  uint64_t Size = 0;
  bool IsPrevailing = false;
  bool IsExported = false;
  bool IsImported = false;
  bool IsRoot = false;
  PluginLinkOriginData Origin;
  PluginLinkExtensionSet Extensions;
};

struct PluginLinkEdge {
  uint64_t ID = 0;
  NevercLinkEdgeKind Kind = NEVERC_LINK_EDGE_RELOCATION;
  uint64_t SourceAtomID = 0;
  uint64_t Offset = 0;
  NevercObjectRelocationKind RelocationKind =
      NEVERC_OBJECT_RELOCATION_ABSOLUTE;
  uint32_t Width = 0;
  int64_t Addend = 0;
  bool IsPCRelative = false;
  bool IsSigned = false;
  uint64_t TargetSymbolID = 0;
  uint64_t TargetAtomID = 0;
  PluginLinkOriginData Origin;
  PluginLinkExtensionSet Extensions;
};

struct PluginLinkComdat {
  uint64_t ID = 0;
  std::string Name;
  NevercLinkComdatSelection Selection = NEVERC_LINK_COMDAT_ANY;
  uint64_t SelectedID = 0;
  PluginLinkOriginData Origin;
  PluginLinkExtensionSet Extensions;
};

struct PluginLinkImport {
  uint64_t ID = 0;
  std::string Name;
  std::string Library;
  uint64_t SymbolID = 0;
  PluginLinkOriginData Origin;
  PluginLinkExtensionSet Extensions;
};

struct PluginLinkExport {
  uint64_t ID = 0;
  std::string Name;
  uint64_t SymbolID = 0;
  PluginLinkOriginData Origin;
  PluginLinkExtensionSet Extensions;
};

struct PluginLinkUnwindRecord {
  uint64_t ID = 0;
  uint64_t AtomID = 0;
  uint64_t PersonalitySymbolID = 0;
  PluginLinkOriginData Origin;
  PluginLinkExtensionSet Extensions;
};

struct PluginLinkSynthetic {
  uint64_t ID = 0;
  std::string Role;
  uint64_t SectionID = 0;
  uint64_t AtomID = 0;
  PluginLinkOriginData Origin;
  PluginLinkExtensionSet Extensions;
};

struct PluginLinkConstraint {
  uint64_t ID = 0;
  std::string Kind;
  uint64_t SubjectID = 0;
  uint64_t Value = 0;
  bool Required = false;
  PluginLinkOriginData Origin;
  PluginLinkExtensionSet Extensions;
};

class PluginLinkGraph {
public:
  using InputStorage = std::list<PluginLinkInput>;
  using ArchiveStorage = std::list<PluginLinkArchive>;
  using ArchiveMemberStorage = std::list<PluginLinkArchiveMember>;
  using SharedLibraryStorage = std::list<PluginLinkSharedLibrary>;
  using BitcodeModuleStorage = std::list<PluginLinkBitcodeModule>;
  using SectionStorage = std::list<PluginLinkSection>;
  using AtomStorage = std::list<PluginLinkAtom>;
  using SymbolStorage = std::list<PluginLinkSymbol>;
  using EdgeStorage = std::list<PluginLinkEdge>;
  using ComdatStorage = std::list<PluginLinkComdat>;
  using ImportStorage = std::list<PluginLinkImport>;
  using ExportStorage = std::list<PluginLinkExport>;
  using UnwindStorage = std::list<PluginLinkUnwindRecord>;
  using SyntheticStorage = std::list<PluginLinkSynthetic>;
  using ConstraintStorage = std::list<PluginLinkConstraint>;

  explicit PluginLinkGraph(OwnedTargetKey Target,
                           NevercLinkState State = NEVERC_LINK_STATE_INITIAL);

  NevercTargetKey targetKey() const { return Target.view(); }
  NevercObjectFormatID formatID() const {
    return Target.view().ObjectFormatID;
  }
  NevercLinkState state() const { return State; }
  void setState(NevercLinkState Value) { State = Value; }
  uint64_t generation() const { return Generation; }
  void advanceGeneration();
  uint64_t allocateEntityID();
  std::array<uint8_t, 32> semanticDigest() const;

  PluginLinkInput &addInput(PluginLinkInput Value);
  PluginLinkArchive &addArchive(PluginLinkArchive Value);
  PluginLinkArchiveMember &addArchiveMember(PluginLinkArchiveMember Value);
  PluginLinkSharedLibrary &addSharedLibrary(PluginLinkSharedLibrary Value);
  PluginLinkBitcodeModule &addBitcodeModule(PluginLinkBitcodeModule Value);
  PluginLinkSection &addSection(PluginLinkSection Value);
  PluginLinkAtom &addAtom(PluginLinkAtom Value);
  PluginLinkSymbol &addSymbol(PluginLinkSymbol Value);
  PluginLinkEdge &addEdge(PluginLinkEdge Value);
  PluginLinkComdat &addComdat(PluginLinkComdat Value);
  PluginLinkImport &addImport(PluginLinkImport Value);
  PluginLinkExport &addExport(PluginLinkExport Value);
  PluginLinkUnwindRecord &addUnwind(PluginLinkUnwindRecord Value);
  PluginLinkSynthetic &addSynthetic(PluginLinkSynthetic Value);
  PluginLinkConstraint &addConstraint(PluginLinkConstraint Value);

  PluginLinkInput *findInput(uint64_t ID);
  PluginLinkArchive *findArchive(uint64_t ID);
  PluginLinkArchiveMember *findArchiveMember(uint64_t ID);
  PluginLinkSharedLibrary *findSharedLibrary(uint64_t ID);
  PluginLinkBitcodeModule *findBitcodeModule(uint64_t ID);
  PluginLinkSection *findSection(uint64_t ID);
  PluginLinkAtom *findAtom(uint64_t ID);
  PluginLinkSymbol *findSymbol(uint64_t ID);
  PluginLinkEdge *findEdge(uint64_t ID);
  PluginLinkComdat *findComdat(uint64_t ID);
  PluginLinkImport *findImport(uint64_t ID);
  PluginLinkExport *findExport(uint64_t ID);
  PluginLinkUnwindRecord *findUnwind(uint64_t ID);
  PluginLinkSynthetic *findSynthetic(uint64_t ID);
  PluginLinkConstraint *findConstraint(uint64_t ID);
  const PluginLinkInput *findInput(uint64_t ID) const;
  const PluginLinkArchive *findArchive(uint64_t ID) const;
  const PluginLinkArchiveMember *findArchiveMember(uint64_t ID) const;
  const PluginLinkSharedLibrary *findSharedLibrary(uint64_t ID) const;
  const PluginLinkBitcodeModule *findBitcodeModule(uint64_t ID) const;
  const PluginLinkSection *findSection(uint64_t ID) const;
  const PluginLinkAtom *findAtom(uint64_t ID) const;
  const PluginLinkSymbol *findSymbol(uint64_t ID) const;
  const PluginLinkEdge *findEdge(uint64_t ID) const;
  const PluginLinkComdat *findComdat(uint64_t ID) const;
  const PluginLinkImport *findImport(uint64_t ID) const;
  const PluginLinkExport *findExport(uint64_t ID) const;
  const PluginLinkUnwindRecord *findUnwind(uint64_t ID) const;
  const PluginLinkSynthetic *findSynthetic(uint64_t ID) const;
  const PluginLinkConstraint *findConstraint(uint64_t ID) const;

  InputStorage &inputs() { return Inputs; }
  const InputStorage &inputs() const { return Inputs; }
  ArchiveStorage &archives() { return Archives; }
  const ArchiveStorage &archives() const { return Archives; }
  ArchiveMemberStorage &archiveMembers() { return ArchiveMembers; }
  const ArchiveMemberStorage &archiveMembers() const {
    return ArchiveMembers;
  }
  SharedLibraryStorage &sharedLibraries() { return SharedLibraries; }
  const SharedLibraryStorage &sharedLibraries() const {
    return SharedLibraries;
  }
  BitcodeModuleStorage &bitcodeModules() { return BitcodeModules; }
  const BitcodeModuleStorage &bitcodeModules() const {
    return BitcodeModules;
  }
  SectionStorage &sections() { return Sections; }
  const SectionStorage &sections() const { return Sections; }
  AtomStorage &atoms() { return Atoms; }
  const AtomStorage &atoms() const { return Atoms; }
  SymbolStorage &symbols() { return Symbols; }
  const SymbolStorage &symbols() const { return Symbols; }
  EdgeStorage &edges() { return Edges; }
  const EdgeStorage &edges() const { return Edges; }
  ComdatStorage &comdats() { return Comdats; }
  const ComdatStorage &comdats() const { return Comdats; }
  ImportStorage &imports() { return Imports; }
  const ImportStorage &imports() const { return Imports; }
  ExportStorage &exports() { return Exports; }
  const ExportStorage &exports() const { return Exports; }
  UnwindStorage &unwindRecords() { return UnwindRecords; }
  const UnwindStorage &unwindRecords() const { return UnwindRecords; }
  SyntheticStorage &synthetics() { return Synthetics; }
  const SyntheticStorage &synthetics() const { return Synthetics; }
  ConstraintStorage &constraints() { return Constraints; }
  const ConstraintStorage &constraints() const { return Constraints; }

private:
  OwnedTargetKey Target;
  NevercLinkState State;
  uint64_t Generation = 1;
  uint64_t NextEntityID = 1;
  InputStorage Inputs;
  ArchiveStorage Archives;
  ArchiveMemberStorage ArchiveMembers;
  SharedLibraryStorage SharedLibraries;
  BitcodeModuleStorage BitcodeModules;
  SectionStorage Sections;
  AtomStorage Atoms;
  SymbolStorage Symbols;
  EdgeStorage Edges;
  ComdatStorage Comdats;
  ImportStorage Imports;
  ExportStorage Exports;
  UnwindStorage UnwindRecords;
  SyntheticStorage Synthetics;
  ConstraintStorage Constraints;
};

std::string canonicalizeLinkOrigin(const PluginLinkOriginData &Origin);
llvm::Error verifyPluginLinkGraph(const PluginLinkGraph &Graph);

class LinkGraphPluginBridge {
public:
  LinkGraphPluginBridge(PluginTaskContext &Task, PluginLinkGraph &Graph,
                        bool AllowMutation = true);
  ~LinkGraphPluginBridge();
  LinkGraphPluginBridge(const LinkGraphPluginBridge &) = delete;
  LinkGraphPluginBridge &operator=(const LinkGraphPluginBridge &) = delete;

  const NevercLinkAPI &api() const { return API; }
  NevercTaskHandle taskHandle() const;
  PluginLinkGraph &graphValue() const { return activeGraph(); }
  PluginLinkGraph &committedGraph() const { return Graph; }
  PluginLinkGraph &activeGraph() const;
  llvm::Expected<NevercLinkGraphHandle> graph();
  bool mutationAllowed() const { return MutationAllowed; }
  bool hasActiveMutation() const;
  llvm::Expected<NevercLinkMutationHandle>
  beginMutation(std::string Capability = {});
  NevercStatus checkMutation(NevercLinkMutationHandle Mutation) const;
  LinkMutation *mutationValue(NevercLinkMutationHandle Mutation,
                              NevercStatus &Status) const;
  NevercStatus commitMutation(NevercLinkMutationHandle Mutation);
  NevercStatus abandonMutation(NevercLinkMutationHandle Mutation);
  NevercLinkState lastInvalidatedState() const {
    return LastInvalidatedState;
  }

  llvm::Expected<NevercLinkProofHandle>
  issueProof(NevercLinkState State,
             NevercInterfaceID OutputArtifact = {},
             std::array<uint8_t, 32> RouteDigest = {});
  NevercStatus resolveProof(NevercLinkProofHandle Handle,
                            const PluginLinkProof **OutProof) const;

  enum class EntityKind : uint8_t {
    Input,
    Archive,
    ArchiveMember,
    SharedLibrary,
    BitcodeModule,
    Section,
    Atom,
    Symbol,
    Edge,
    Comdat,
    Import,
    Export,
    Unwind,
    Synthetic,
    Constraint,
  };

  llvm::Expected<NevercHandle> wrapEntity(EntityKind Kind, uint64_t ID);
  NevercStatus resolveGraph(NevercLinkGraphHandle Handle,
                            PluginLinkGraph **OutGraph) const;
  NevercStatus resolveEntity(NevercHandle Handle, EntityKind Kind,
                             uint64_t *OutID) const;
  void invalidateEntityHandles();

private:
  struct EntityReference {
    LinkGraphPluginBridge *Bridge = nullptr;
    EntityKind Kind = EntityKind::Input;
    uint64_t ID = 0;
    uint64_t Generation = 0;
  };

  PluginTaskContext &Task;
  PluginLinkGraph &Graph;
  bool MutationAllowed = true;
  uint64_t BridgeGeneration = 1;
  NevercLinkAPI API{};
  NevercLinkGraphHandle GraphHandle{};
  std::unique_ptr<LinkMutation> Mutation;
  NevercLinkMutationHandle MutationHandle{};
  NevercLinkState LastInvalidatedState =
      NEVERC_LINK_STATE_IMAGE_EMITTED;
  std::vector<std::unique_ptr<PluginLinkProof>> Proofs;
  std::vector<NevercLinkProofHandle> ProofHandles;
  std::vector<std::pair<NevercHandle, uint16_t>> EntityHandles;

  void finishMutation();

  friend void initializeLinkGraphAPI(NevercLinkAPI &,
                                     LinkGraphPluginBridge &);
};

void initializeLinkGraphAPI(NevercLinkAPI &API,
                            LinkGraphPluginBridge &Bridge);

} // namespace neverc::plugin

#endif
