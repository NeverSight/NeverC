#ifndef NEVERC_PLUGIN_HOST_OBJECTGRAPH_H
#define NEVERC_PLUGIN_HOST_OBJECTGRAPH_H

#include "neverc/Plugin/Host/PluginTargetDescriptor.h"
#include "neverc/Plugin/PluginObject.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <list>
#include <optional>
#include <string>
#include <vector>

namespace neverc::plugin {

struct PluginObjectExtension {
  NevercObjectFormatID Owner{};
  uint32_t Version = 0;
  std::vector<uint8_t> Bytes;

  bool empty() const { return Bytes.empty(); }
};

struct PluginObjectSection {
  uint64_t ID = 0;
  std::string Name;
  NevercObjectSectionKind Kind = NEVERC_OBJECT_SECTION_KIND_DATA;
  NevercObjectSectionFlags Flags = 0;
  uint64_t Alignment = 1;
  std::vector<uint8_t> Data;
  uint64_t ZeroFillSize = 0;
  uint64_t ComdatID = 0;
  PluginObjectExtension Extension;
};

struct PluginObjectSymbol {
  uint64_t ID = 0;
  std::string Name;
  NevercObjectSymbolBinding Binding =
      NEVERC_OBJECT_SYMBOL_BINDING_LOCAL;
  NevercObjectSymbolVisibility Visibility =
      NEVERC_OBJECT_SYMBOL_VISIBILITY_DEFAULT;
  NevercObjectSymbolType Type = NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE;
  NevercObjectSymbolDefinition Definition =
      NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED;
  uint64_t SectionID = 0;
  uint64_t Value = 0;
  uint64_t Size = 0;
  uint64_t Alignment = 1;
  uint64_t ComdatID = 0;
  NevercObjectSymbolFlags Flags = 0;
  PluginObjectExtension Extension;
};

struct PluginObjectRelocation {
  uint64_t ID = 0;
  uint64_t SectionID = 0;
  uint64_t Offset = 0;
  NevercObjectRelocationKind Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
  NevercObjectRelocationTargetKind TargetKind =
      NEVERC_OBJECT_RELOCATION_TARGET_ABSOLUTE;
  uint32_t Width = 0;
  bool IsPCRelative = false;
  bool IsSigned = false;
  int64_t Addend = 0;
  uint64_t TargetSymbolID = 0;
  uint64_t TargetSectionID = 0;
  uint64_t TargetValue = 0;
  uint32_t TargetExtensionKind = 0;
  PluginObjectExtension Extension;
};

struct PluginObjectComdat {
  uint64_t ID = 0;
  std::string Name;
  NevercObjectComdatSelection Selection =
      NEVERC_OBJECT_COMDAT_ANY;
  uint64_t AssociatedComdatID = 0;
  PluginObjectExtension Extension;
};

struct PluginObjectLayoutProof {
  uint64_t GraphGeneration = 0;
  NevercTargetID TargetID{};
  NevercObjectFormatID FormatID{};
};

class PluginObjectGraph {
public:
  using SectionStorage = std::list<PluginObjectSection>;
  using SymbolStorage = std::list<PluginObjectSymbol>;
  using RelocationStorage = std::list<PluginObjectRelocation>;
  using ComdatStorage = std::list<PluginObjectComdat>;

  explicit PluginObjectGraph(OwnedTargetKey Target);

  NevercTargetKey targetKey() const { return Target.view(); }
  NevercObjectFormatID formatID() const {
    return Target.view().ObjectFormatID;
  }
  uint64_t generation() const { return Generation; }

  size_t sectionCount() const { return Sections.size(); }
  size_t symbolCount() const { return Symbols.size(); }
  size_t relocationCount() const { return Relocations.size(); }
  size_t comdatCount() const { return Comdats.size(); }

  SectionStorage &sections() { return Sections; }
  const SectionStorage &sections() const { return Sections; }
  SymbolStorage &symbols() { return Symbols; }
  const SymbolStorage &symbols() const { return Symbols; }
  RelocationStorage &relocations() { return Relocations; }
  const RelocationStorage &relocations() const { return Relocations; }
  ComdatStorage &comdats() { return Comdats; }
  const ComdatStorage &comdats() const { return Comdats; }

  PluginObjectSection *findSection(uint64_t ID);
  const PluginObjectSection *findSection(uint64_t ID) const;
  PluginObjectSymbol *findSymbol(uint64_t ID);
  const PluginObjectSymbol *findSymbol(uint64_t ID) const;
  PluginObjectRelocation *findRelocation(uint64_t ID);
  const PluginObjectRelocation *findRelocation(uint64_t ID) const;
  PluginObjectComdat *findComdat(uint64_t ID);
  const PluginObjectComdat *findComdat(uint64_t ID) const;

  uint64_t allocateEntityID();
  void advanceGeneration();

  void issueLayoutProof();
  void clearLayoutProof() { LayoutProof.reset(); }
  bool hasLayoutProof() const { return LayoutProof.has_value(); }
  const PluginObjectLayoutProof *layoutProof() const {
    return LayoutProof ? &*LayoutProof : nullptr;
  }

private:
  OwnedTargetKey Target;
  SectionStorage Sections;
  SymbolStorage Symbols;
  RelocationStorage Relocations;
  ComdatStorage Comdats;
  uint64_t Generation = 1;
  uint64_t NextEntityID = 1;
  std::optional<PluginObjectLayoutProof> LayoutProof;
};

llvm::Error verifyPluginObjectGraph(const PluginObjectGraph &Graph);
std::string dumpPluginObjectGraph(const PluginObjectGraph &Graph);

} // namespace neverc::plugin

#endif
