#include "ObjectGraphImporter.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

namespace neverc::plugin {
namespace {

struct AtomChunk {
  uint64_t Begin = 0;
  uint64_t Size = 0;
  uint64_t AtomID = 0;
};

using SectionChunks = std::map<uint64_t, std::vector<AtomChunk>>;

bool equalID(NevercInterfaceID Left, NevercInterfaceID Right) {
  return Left.High == Right.High && Left.Low == Right.Low;
}

PluginLinkOriginData origin(const ObjectGraphImportOptions &Options,
                            uint64_t EntityID) {
  PluginLinkOriginData Result;
  Result.InputID = Options.InputID;
  Result.ArchiveMemberID = Options.ArchiveMemberID;
  Result.ObjectGraph = Options.ObjectGraph;
  Result.ObjectEntityID = EntityID;
  return Result;
}

std::string digest(ArrayRef<uint8_t> Bytes) {
  const std::array<uint8_t, 32> Hash = SHA256::hash(Bytes);
  std::string Result;
  raw_string_ostream OS(Result);
  for (uint8_t Byte : Hash)
    OS << format_hex_no_prefix(Byte, 2);
  OS.flush();
  return Result;
}

PluginLinkExtensionSet
copyExtension(const PluginObjectExtension &Source) {
  PluginLinkExtensionSet Result;
  if (!Source.empty()) {
    PluginLinkExtensionData Extension;
    Extension.NamespaceID = Source.Owner;
    Extension.Version = Source.Version;
    Extension.Payload = Source.Bytes;
    Extension.Digest = digest(Extension.Payload);
    Result.values().push_back(std::move(Extension));
  }
  return Result;
}

NevercLinkComdatSelection mapComdatSelection(uint32_t Selection) {
  switch (Selection) {
  case NEVERC_OBJECT_COMDAT_ANY:
    return NEVERC_LINK_COMDAT_ANY;
  case NEVERC_OBJECT_COMDAT_EXACT_MATCH:
    return NEVERC_LINK_COMDAT_EXACT_MATCH;
  case NEVERC_OBJECT_COMDAT_SAME_SIZE:
    return NEVERC_LINK_COMDAT_SAME_SIZE;
  case NEVERC_OBJECT_COMDAT_NO_DUPLICATES:
    return NEVERC_LINK_COMDAT_NO_DUPLICATES;
  case NEVERC_OBJECT_COMDAT_LARGEST:
    return NEVERC_LINK_COMDAT_LARGEST;
  case NEVERC_OBJECT_COMDAT_ASSOCIATIVE:
    return NEVERC_LINK_COMDAT_ANY;
  default:
    return NEVERC_LINK_COMDAT_ANY;
  }
}

NevercLinkSymbolBinding mapBinding(uint32_t Binding) {
  switch (Binding) {
  case NEVERC_OBJECT_SYMBOL_BINDING_LOCAL:
    return NEVERC_LINK_SYMBOL_BINDING_LOCAL;
  case NEVERC_OBJECT_SYMBOL_BINDING_WEAK:
    return NEVERC_LINK_SYMBOL_BINDING_WEAK;
  case NEVERC_OBJECT_SYMBOL_BINDING_UNIQUE:
    return NEVERC_LINK_SYMBOL_BINDING_GLOBAL;
  case NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL:
  case NEVERC_OBJECT_SYMBOL_BINDING_FORMAT_EXTENSION:
  default:
    return NEVERC_LINK_SYMBOL_BINDING_GLOBAL;
  }
}

NevercLinkSymbolVisibility mapVisibility(uint32_t Visibility) {
  switch (Visibility) {
  case NEVERC_OBJECT_SYMBOL_VISIBILITY_HIDDEN:
    return NEVERC_LINK_SYMBOL_VISIBILITY_HIDDEN;
  case NEVERC_OBJECT_SYMBOL_VISIBILITY_PROTECTED:
    return NEVERC_LINK_SYMBOL_VISIBILITY_PROTECTED;
  case NEVERC_OBJECT_SYMBOL_VISIBILITY_INTERNAL:
    return NEVERC_LINK_SYMBOL_VISIBILITY_INTERNAL;
  case NEVERC_OBJECT_SYMBOL_VISIBILITY_DEFAULT:
  case NEVERC_OBJECT_SYMBOL_VISIBILITY_FORMAT_EXTENSION:
  default:
    return NEVERC_LINK_SYMBOL_VISIBILITY_DEFAULT;
  }
}

NevercLinkSymbolDefinition mapDefinition(uint32_t Definition) {
  switch (Definition) {
  case NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED:
    return NEVERC_LINK_SYMBOL_DEFINED;
  case NEVERC_OBJECT_SYMBOL_DEFINITION_COMMON:
    return NEVERC_LINK_SYMBOL_COMMON;
  case NEVERC_OBJECT_SYMBOL_DEFINITION_ABSOLUTE:
    return NEVERC_LINK_SYMBOL_ABSOLUTE;
  case NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED:
  default:
    return NEVERC_LINK_SYMBOL_UNDEFINED;
  }
}

NevercLinkAtomFlags atomFlags(const PluginObjectSection &Section,
                              bool ZeroFill) {
  NevercLinkAtomFlags Flags = 0;
  (void)ZeroFill;
  if (Section.Kind == NEVERC_OBJECT_SECTION_KIND_TLS_DATA ||
      Section.Kind == NEVERC_OBJECT_SECTION_KIND_TLS_ZERO_FILL)
    Flags |= NEVERC_LINK_ATOM_TLS;
  if (Section.Kind == NEVERC_OBJECT_SECTION_KIND_UNWIND)
    Flags |= NEVERC_LINK_ATOM_UNWIND;
  if ((Section.Flags & NEVERC_OBJECT_SECTION_RETAIN) != 0)
    Flags |= NEVERC_LINK_ATOM_ROOT | NEVERC_LINK_ATOM_LIVE;
  return Flags;
}

Expected<AtomChunk> chunkForOffset(const SectionChunks &Chunks,
                                   uint64_t SectionID, uint64_t Offset,
                                   StringRef EntityKind) {
  auto It = Chunks.find(SectionID);
  if (It == Chunks.end() || It->second.empty())
    return createStringError(errc::invalid_argument,
                             "ObjectGraph import: %s refers to section %llu "
                             "without a normalized atom",
                             EntityKind.str().c_str(),
                             static_cast<unsigned long long>(SectionID));
  for (const AtomChunk &Chunk : It->second) {
    if (Offset >= Chunk.Begin &&
        (Offset < Chunk.Begin + Chunk.Size ||
         (Chunk.Size == 0 && Offset == Chunk.Begin)))
      return Chunk;
  }
  const AtomChunk &Last = It->second.back();
  if (Offset == Last.Begin + Last.Size)
    return Last;
  return createStringError(errc::invalid_argument,
                           "ObjectGraph import: %s offset %llu is outside "
                           "section %llu",
                           EntityKind.str().c_str(),
                           static_cast<unsigned long long>(Offset),
                           static_cast<unsigned long long>(SectionID));
}

} // namespace

Expected<ObjectGraphImportResult>
importObjectGraph(PluginLinkGraph &Destination,
                  const PluginObjectGraph &Source,
                  const ObjectGraphImportOptions &Options) {
  if (Error E = verifyPluginObjectGraph(Source))
    return std::move(E);

  const NevercTargetKey Target = Destination.targetKey();
  if (!equalID(Target.TargetID, Source.targetKey().TargetID) ||
      !equalID(Target.ObjectFormatID, Source.formatID()))
    return createStringError(
        errc::invalid_argument,
        "ObjectGraph import: source target or object format does not match "
        "the LinkGraph");
  if (Options.InputID != 0 && !Destination.findInput(Options.InputID))
    return createStringError(errc::invalid_argument,
                             "ObjectGraph import: origin input is missing");
  if (Options.ArchiveMemberID != 0 &&
      !Destination.findArchiveMember(Options.ArchiveMemberID))
    return createStringError(
        errc::invalid_argument,
        "ObjectGraph import: origin archive member is missing");

  ObjectGraphImportResult Result;
  SectionChunks Chunks;

  for (const PluginObjectComdat &SourceComdat : Source.comdats()) {
    PluginLinkComdat Comdat;
    Comdat.Name = SourceComdat.Name;
    Comdat.Selection = mapComdatSelection(SourceComdat.Selection);
    Comdat.Origin = origin(Options, SourceComdat.ID);
    Comdat.Extensions = copyExtension(SourceComdat.Extension);
    PluginLinkComdat &Stored = Destination.addComdat(std::move(Comdat));
    Result.Comdats.emplace(SourceComdat.ID, Stored.ID);
  }
  for (const PluginObjectComdat &SourceComdat : Source.comdats()) {
    if (SourceComdat.AssociatedComdatID == 0)
      continue;
    PluginLinkComdat *Stored =
        Destination.findComdat(Result.Comdats.at(SourceComdat.ID));
    auto Associated = Result.Comdats.find(SourceComdat.AssociatedComdatID);
    if (Associated != Result.Comdats.end())
      Stored->SelectedID = Associated->second;
  }

  for (const PluginObjectSection &SourceSection : Source.sections()) {
    PluginLinkSection Section;
    Section.Name = SourceSection.Name;
    Section.Kind = SourceSection.Kind;
    Section.Alignment = std::max<uint64_t>(1, SourceSection.Alignment);
    Section.Size = SourceSection.Data.size() + SourceSection.ZeroFillSize;
    Section.Flags = SourceSection.Flags;
    if (SourceSection.ComdatID != 0)
      Section.ComdatID = Result.Comdats.at(SourceSection.ComdatID);
    Section.Origin = origin(Options, SourceSection.ID);
    Section.Extensions = copyExtension(SourceSection.Extension);
    PluginLinkSection &StoredSection =
        Destination.addSection(std::move(Section));
    Result.Sections.emplace(SourceSection.ID, StoredSection.ID);

    auto addAtom = [&](StringRef Suffix, uint64_t Begin, bool ZeroFill,
                       ArrayRef<uint8_t> Content, uint64_t ZeroSize) {
      PluginLinkAtom Atom;
      Atom.Name = SourceSection.Name;
      if (!Suffix.empty())
        Atom.Name += Suffix.str();
      Atom.SectionID = StoredSection.ID;
      Atom.Alignment =
          Begin == 0 ? std::max<uint64_t>(1, SourceSection.Alignment) : 1;
      Atom.Flags = atomFlags(SourceSection, ZeroFill);
      Atom.Content.assign(Content.begin(), Content.end());
      Atom.ZeroFillSize = ZeroSize;
      Atom.ComdatID = StoredSection.ComdatID;
      Atom.Origin = origin(Options, SourceSection.ID);
      Atom.Extensions = copyExtension(SourceSection.Extension);
      PluginLinkAtom &StoredAtom = Destination.addAtom(std::move(Atom));
      Chunks[SourceSection.ID].push_back(
          {Begin, Content.size() + ZeroSize, StoredAtom.ID});
      if (!Result.Atoms.count(SourceSection.ID))
        Result.Atoms.emplace(SourceSection.ID, StoredAtom.ID);
      if (SourceSection.Kind == NEVERC_OBJECT_SECTION_KIND_UNWIND) {
        PluginLinkUnwindRecord Unwind;
        Unwind.AtomID = StoredAtom.ID;
        Unwind.Origin = origin(Options, SourceSection.ID);
        Destination.addUnwind(std::move(Unwind));
      }
    };

    if (!SourceSection.Data.empty())
      addAtom(SourceSection.ZeroFillSize != 0 ? "$data" : "", 0, false,
              SourceSection.Data, 0);
    if (SourceSection.ZeroFillSize != 0)
      addAtom(!SourceSection.Data.empty() ? "$bss" : "",
              SourceSection.Data.size(), true, {}, SourceSection.ZeroFillSize);
    if (SourceSection.Data.empty() && SourceSection.ZeroFillSize == 0)
      addAtom("", 0, false, {}, 0);
  }

  for (const PluginObjectSymbol &SourceSymbol : Source.symbols()) {
    PluginLinkSymbol Symbol;
    Symbol.Name = SourceSymbol.Name;
    Symbol.Binding = mapBinding(SourceSymbol.Binding);
    Symbol.Visibility = mapVisibility(SourceSymbol.Visibility);
    Symbol.Definition = mapDefinition(SourceSymbol.Definition);
    Symbol.Type = SourceSymbol.Type;
    Symbol.Value = SourceSymbol.Value;
    Symbol.Size = SourceSymbol.Size;
    Symbol.IsImported =
        (SourceSymbol.Flags & NEVERC_OBJECT_SYMBOL_IMPORTED) != 0;
    Symbol.IsExported =
        (SourceSymbol.Flags & NEVERC_OBJECT_SYMBOL_EXPORTED) != 0;
    Symbol.Origin = origin(Options, SourceSymbol.ID);
    Symbol.Extensions = copyExtension(SourceSymbol.Extension);
    if (Symbol.Definition == NEVERC_LINK_SYMBOL_DEFINED) {
      auto Chunk = chunkForOffset(Chunks, SourceSymbol.SectionID,
                                  SourceSymbol.Value, "symbol");
      if (!Chunk)
        return Chunk.takeError();
      Symbol.AtomID = Chunk->AtomID;
      Symbol.Value -= Chunk->Begin;
    }
    PluginLinkSymbol &Stored = Destination.addSymbol(std::move(Symbol));
    Result.Symbols.emplace(SourceSymbol.ID, Stored.ID);

    if (Stored.IsImported) {
      PluginLinkImport Import;
      Import.Name = Stored.Name;
      Import.SymbolID = Stored.ID;
      Import.Origin = Stored.Origin;
      Destination.addImport(std::move(Import));
    }
    if (Stored.IsExported) {
      PluginLinkExport Export;
      Export.Name = Stored.Name;
      Export.SymbolID = Stored.ID;
      Export.Origin = Stored.Origin;
      Destination.addExport(std::move(Export));
    }
  }

  for (const PluginObjectRelocation &SourceReloc : Source.relocations()) {
    auto SourceChunk = chunkForOffset(Chunks, SourceReloc.SectionID,
                                      SourceReloc.Offset, "relocation");
    if (!SourceChunk)
      return SourceChunk.takeError();
    PluginLinkEdge Edge;
    Edge.Kind = NEVERC_LINK_EDGE_RELOCATION;
    Edge.SourceAtomID = SourceChunk->AtomID;
    Edge.Offset = SourceReloc.Offset - SourceChunk->Begin;
    Edge.Addend = SourceReloc.Addend;
    Edge.Width = SourceReloc.Width;
    Edge.RelocationKind = SourceReloc.Kind;
    Edge.IsPCRelative = SourceReloc.IsPCRelative;
    Edge.IsSigned = SourceReloc.IsSigned;
    Edge.Origin = origin(Options, SourceReloc.ID);
    Edge.Extensions = copyExtension(SourceReloc.Extension);

    if (SourceReloc.TargetKind == NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL) {
      auto Target = Result.Symbols.find(SourceReloc.TargetSymbolID);
      if (Target == Result.Symbols.end())
        return createStringError(
            errc::invalid_argument,
            "ObjectGraph import: relocation target symbol is missing");
      Edge.TargetSymbolID = Target->second;
    } else if (SourceReloc.TargetKind ==
               NEVERC_OBJECT_RELOCATION_TARGET_SECTION) {
      auto TargetChunk =
          chunkForOffset(Chunks, SourceReloc.TargetSectionID,
                         SourceReloc.TargetValue, "relocation target");
      if (!TargetChunk)
        return TargetChunk.takeError();
      Edge.TargetAtomID = TargetChunk->AtomID;
      if (SourceReloc.TargetValue - TargetChunk->Begin >
          static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
        return createStringError(
            errc::result_out_of_range,
            "ObjectGraph import: section-relative relocation addend "
            "does not fit int64");
      Edge.Addend += static_cast<int64_t>(SourceReloc.TargetValue -
                                         TargetChunk->Begin);
    } else {
      PluginLinkSymbol Absolute;
      Absolute.Name = "$object.absolute." + std::to_string(SourceReloc.ID);
      Absolute.Binding = NEVERC_LINK_SYMBOL_BINDING_LOCAL;
      Absolute.Definition = NEVERC_LINK_SYMBOL_ABSOLUTE;
      Absolute.Value = SourceReloc.TargetValue;
      Absolute.Origin = origin(Options, SourceReloc.ID);
      PluginLinkSymbol &StoredAbsolute =
          Destination.addSymbol(std::move(Absolute));
      Edge.TargetSymbolID = StoredAbsolute.ID;
    }
    PluginLinkEdge &Stored = Destination.addEdge(std::move(Edge));
    Result.Relocations.emplace(SourceReloc.ID, Stored.ID);
  }

  if (Error E = verifyPluginLinkGraph(Destination))
    return std::move(E);
  return Result;
}

} // namespace neverc::plugin
