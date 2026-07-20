#include "LinkGraph.h"
#include "LinkMutation.h"
#include "LinkProof.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/Support/Error.h"
#include <algorithm>
#include <cstring>
#include <iterator>
#include <new>

using namespace llvm;

namespace neverc::plugin {
namespace {

NevercStatus linkStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

bool sameHandle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

template <typename T> bool validRecord(const T *Value) {
  return Value && Value->Header.StructSize >= sizeof(T) &&
         Value->Header.Major == NEVERC_LINK_API_MAJOR &&
         Value->Header.Minor <= NEVERC_LINK_API_MINOR;
}

PluginHandleKind handleKind(LinkGraphPluginBridge::EntityKind Kind) {
  switch (Kind) {
  case LinkGraphPluginBridge::EntityKind::Input:
    return PluginLinkInputHandleKind;
  case LinkGraphPluginBridge::EntityKind::Archive:
    return PluginLinkArchiveHandleKind;
  case LinkGraphPluginBridge::EntityKind::ArchiveMember:
    return PluginLinkArchiveMemberHandleKind;
  case LinkGraphPluginBridge::EntityKind::SharedLibrary:
    return PluginLinkSharedLibraryHandleKind;
  case LinkGraphPluginBridge::EntityKind::BitcodeModule:
    return PluginLinkBitcodeModuleHandleKind;
  case LinkGraphPluginBridge::EntityKind::Section:
    return PluginLinkSectionHandleKind;
  case LinkGraphPluginBridge::EntityKind::Atom:
    return PluginLinkAtomHandleKind;
  case LinkGraphPluginBridge::EntityKind::Symbol:
    return PluginLinkSymbolHandleKind;
  case LinkGraphPluginBridge::EntityKind::Edge:
    return PluginLinkEdgeHandleKind;
  case LinkGraphPluginBridge::EntityKind::Comdat:
    return PluginLinkComdatHandleKind;
  case LinkGraphPluginBridge::EntityKind::Import:
    return PluginLinkImportHandleKind;
  case LinkGraphPluginBridge::EntityKind::Export:
    return PluginLinkExportHandleKind;
  case LinkGraphPluginBridge::EntityKind::Unwind:
    return PluginLinkUnwindHandleKind;
  case LinkGraphPluginBridge::EntityKind::Synthetic:
    return PluginLinkSyntheticHandleKind;
  case LinkGraphPluginBridge::EntityKind::Constraint:
    return PluginLinkConstraintHandleKind;
  }
  return 0;
}

LinkGraphPluginBridge *bridge(void *Context, NevercTaskHandle Task,
                              NevercStatus &Status) {
  if (!Context) {
    Status = linkStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return nullptr;
  }
  auto *Bridge = static_cast<LinkGraphPluginBridge *>(Context);
  if (!sameHandle(Bridge->taskHandle(), Task)) {
    Status = linkStatus(NEVERC_STATUS_WRONG_SCOPE);
    return nullptr;
  }
  Status = neverc_status_ok();
  return Bridge;
}

template <typename T>
Expected<T> castHandle(Expected<NevercHandle> Handle) {
  if (!Handle)
    return Handle.takeError();
  return T{Handle->Owner, Handle->Value};
}

NevercStatus consumeHandleError(Expected<NevercHandle> Handle,
                                NevercHandle *Out) {
  if (!Out) {
    if (!Handle)
      consumeError(Handle.takeError());
    return linkStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  }
  *Out = {};
  if (!Handle) {
    consumeError(Handle.takeError());
    return linkStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *Out = *Handle;
  return neverc_status_ok();
}

NevercStatus fillOrigin(LinkGraphPluginBridge &Bridge,
                        const PluginLinkOriginData &Origin,
                        NevercLinkOrigin &Out) {
  Out = {};
  Out.Header = {sizeof(Out), NEVERC_LINK_API_MAJOR,
                NEVERC_LINK_API_MINOR, 0};
  if (Origin.InputID != 0) {
    NevercHandle Handle{};
    NevercStatus Status = consumeHandleError(
        Bridge.wrapEntity(LinkGraphPluginBridge::EntityKind::Input,
                          Origin.InputID),
        &Handle);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    Out.Input = Handle;
  }
  if (Origin.ArchiveMemberID != 0) {
    NevercHandle Handle{};
    NevercStatus Status = consumeHandleError(
        Bridge.wrapEntity(LinkGraphPluginBridge::EntityKind::ArchiveMember,
                          Origin.ArchiveMemberID),
        &Handle);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    Out.ArchiveMember = Handle;
  }
  Out.ObjectGraph = Origin.ObjectGraph;
  Out.ObjectEntityID = Origin.ObjectEntityID;
  Out.CreatedByPhase = Origin.CreatedByPhase;
  Out.CreatedByProvider = {Origin.CreatedByProvider.data(),
                           Origin.CreatedByProvider.size()};
  Out.LastMutationPhase = Origin.LastMutationPhase;
  Out.LastMutationPlugin = {Origin.LastMutationPlugin.data(),
                            Origin.LastMutationPlugin.size()};
  return neverc_status_ok();
}

template <typename Record, typename Storage, typename Fill>
NevercStatus fillPage(LinkGraphPluginBridge &Bridge, const Storage &Values,
                       uint64_t Cursor, NevercLinkEntityPage *Page,
                       Fill FillRecord) {
  if (!validRecord(Page) || Page->ElementStride < sizeof(Record) ||
      (Page->ElementCapacity != 0 && !Page->Data))
    return linkStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  if (Cursor > Values.size())
    return linkStatus(NEVERC_STATUS_INVALID_ARGUMENT);

  Page->OutCount = 0;
  Page->NextCursor = Cursor;
  Page->HasMore = Cursor < Values.size() ? NEVERC_TRUE : NEVERC_FALSE;
  Page->Reserved = 0;
  auto It = Values.begin();
  std::advance(It, static_cast<typename Storage::difference_type>(Cursor));
  auto *Bytes = static_cast<uint8_t *>(Page->Data);
  while (It != Values.end() && Page->OutCount < Page->ElementCapacity) {
    auto *Out = reinterpret_cast<Record *>(
        Bytes + Page->OutCount * Page->ElementStride);
    std::memset(Out, 0, sizeof(*Out));
    Out->Header = {sizeof(*Out), NEVERC_LINK_API_MAJOR,
                   NEVERC_LINK_API_MINOR, 0};
    NevercStatus Status = FillRecord(Bridge, *It, *Out);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    ++It;
    ++Page->OutCount;
    ++Page->NextCursor;
  }
  Page->HasMore =
      Page->NextCursor < Values.size() ? NEVERC_TRUE : NEVERC_FALSE;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL GetGraphInfo(
    void *Context, NevercTaskHandle Task, NevercLinkGraphHandle Graph,
    NevercLinkGraphInfo *OutInfo) {
  NevercStatus Status;
  LinkGraphPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  if (!validRecord(OutInfo))
    return linkStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  PluginLinkGraph *Resolved = nullptr;
  Status = Bridge->resolveGraph(Graph, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  const auto Digest = Resolved->semanticDigest();
  OutInfo->Graph = Graph;
  OutInfo->Target = Resolved->targetKey();
  OutInfo->FormatID = Resolved->formatID();
  OutInfo->State = Resolved->state();
  OutInfo->Reserved = 0;
  OutInfo->Generation = Resolved->generation();
  OutInfo->InputCount = Resolved->inputs().size();
  OutInfo->ArchiveCount = Resolved->archives().size();
  OutInfo->ArchiveMemberCount = Resolved->archiveMembers().size();
  OutInfo->SharedLibraryCount = Resolved->sharedLibraries().size();
  OutInfo->BitcodeModuleCount = Resolved->bitcodeModules().size();
  OutInfo->SectionCount = Resolved->sections().size();
  OutInfo->AtomCount = Resolved->atoms().size();
  OutInfo->SymbolCount = Resolved->symbols().size();
  OutInfo->EdgeCount = Resolved->edges().size();
  OutInfo->ComdatCount = Resolved->comdats().size();
  OutInfo->ImportCount = Resolved->imports().size();
  OutInfo->ExportCount = Resolved->exports().size();
  OutInfo->UnwindCount = Resolved->unwindRecords().size();
  OutInfo->SyntheticCount = Resolved->synthetics().size();
  OutInfo->ConstraintCount = Resolved->constraints().size();
  std::copy(Digest.begin(), Digest.end(), OutInfo->SemanticDigest);
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL GetInputPage(
    void *Context, NevercTaskHandle Task, NevercLinkGraphHandle Graph,
    uint64_t Cursor, NevercLinkEntityPage *Page) {
  NevercStatus Status;
  LinkGraphPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  PluginLinkGraph *Resolved = nullptr;
  Status = Bridge->resolveGraph(Graph, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return fillPage<NevercLinkInputInfo>(
      *Bridge, Resolved->inputs(), Cursor, Page,
      [](LinkGraphPluginBridge &Bridge, const PluginLinkInput &Input,
         NevercLinkInputInfo &Out) {
        auto Handle = Bridge.wrapEntity(
            LinkGraphPluginBridge::EntityKind::Input, Input.ID);
        if (!Handle) {
          consumeError(Handle.takeError());
          return linkStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
        }
        Out.Input = *Handle;
        Out.Kind = Input.Kind;
        Out.Reserved = 0;
        Out.Flags = Input.Flags;
        Out.Ordinal = Input.Ordinal;
        Out.LogicalURI = {Input.LogicalURI.data(), Input.LogicalURI.size()};
        std::copy(Input.ContentDigest.begin(), Input.ContentDigest.end(),
                  Out.ContentDigest);
        Out.ReaderRoute = {Input.ReaderRoute.data(),
                           Input.ReaderRoute.size()};
        Out.ObjectGraph = Input.ObjectGraph;
        if (Input.ArchiveID != 0) {
          NevercStatus Status = consumeHandleError(
              Bridge.wrapEntity(
                  LinkGraphPluginBridge::EntityKind::Archive,
                  Input.ArchiveID),
              &Out.Archive);
          if (Status.Code != NEVERC_STATUS_OK)
            return Status;
        }
        if (Input.SharedLibraryID != 0) {
          NevercStatus Status = consumeHandleError(
              Bridge.wrapEntity(
                  LinkGraphPluginBridge::EntityKind::SharedLibrary,
                  Input.SharedLibraryID),
              &Out.SharedLibrary);
          if (Status.Code != NEVERC_STATUS_OK)
            return Status;
        }
        if (Input.BitcodeModuleID != 0) {
          NevercStatus Status = consumeHandleError(
              Bridge.wrapEntity(
                  LinkGraphPluginBridge::EntityKind::BitcodeModule,
                  Input.BitcodeModuleID),
              &Out.BitcodeModule);
          if (Status.Code != NEVERC_STATUS_OK)
            return Status;
        }
        Out.Extensions = Input.Extensions.view();
        return neverc_status_ok();
      });
}

template <typename Record, typename Access, typename Fill>
NevercStatus getGraphPage(void *Context, NevercTaskHandle Task,
                          NevercLinkGraphHandle Graph, uint64_t Cursor,
                          NevercLinkEntityPage *Page, Access Storage,
                          Fill FillRecord) {
  NevercStatus Status;
  LinkGraphPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  PluginLinkGraph *Resolved = nullptr;
  Status = Bridge->resolveGraph(Graph, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return fillPage<Record>(*Bridge, Storage(*Resolved), Cursor, Page,
                          FillRecord);
}

NevercStatus NEVERC_CALL GetArchivePage(
    void *Context, NevercTaskHandle Task, NevercLinkGraphHandle Graph,
    uint64_t Cursor, NevercLinkEntityPage *Page) {
  return getGraphPage<NevercLinkArchiveInfo>(
      Context, Task, Graph, Cursor, Page,
      [](PluginLinkGraph &Value) -> const auto & {
        return Value.archives();
      },
      [](LinkGraphPluginBridge &Bridge, const PluginLinkArchive &Archive,
         NevercLinkArchiveInfo &Out) {
        NevercStatus Status = consumeHandleError(
            Bridge.wrapEntity(LinkGraphPluginBridge::EntityKind::Archive,
                              Archive.ID),
            &Out.Archive);
        if (Status.Code != NEVERC_STATUS_OK)
          return Status;
        if (Archive.InputID != 0) {
          Status = consumeHandleError(
              Bridge.wrapEntity(LinkGraphPluginBridge::EntityKind::Input,
                                Archive.InputID),
              &Out.Input);
          if (Status.Code != NEVERC_STATUS_OK)
            return Status;
        }
        Out.Name = {Archive.Name.data(), Archive.Name.size()};
        Out.Thin = Archive.Thin ? NEVERC_TRUE : NEVERC_FALSE;
        Status = fillOrigin(Bridge, Archive.Origin, Out.Origin);
        if (Status.Code != NEVERC_STATUS_OK)
          return Status;
        Out.Extensions = Archive.Extensions.view();
        return neverc_status_ok();
      });
}

NevercStatus NEVERC_CALL GetArchiveMemberPage(
    void *Context, NevercTaskHandle Task, NevercLinkGraphHandle Graph,
    uint64_t Cursor, NevercLinkEntityPage *Page) {
  return getGraphPage<NevercLinkArchiveMemberInfo>(
      Context, Task, Graph, Cursor, Page,
      [](PluginLinkGraph &Value) -> const auto & {
        return Value.archiveMembers();
      },
      [](LinkGraphPluginBridge &Bridge,
         const PluginLinkArchiveMember &Member,
         NevercLinkArchiveMemberInfo &Out) {
        NevercStatus Status = consumeHandleError(
            Bridge.wrapEntity(
                LinkGraphPluginBridge::EntityKind::ArchiveMember,
                Member.ID),
            &Out.Member);
        if (Status.Code != NEVERC_STATUS_OK)
          return Status;
        if (Member.InputID != 0) {
          Status = consumeHandleError(
              Bridge.wrapEntity(LinkGraphPluginBridge::EntityKind::Input,
                                Member.InputID),
              &Out.Input);
          if (Status.Code != NEVERC_STATUS_OK)
            return Status;
        }
        if (Member.ArchiveID != 0) {
          Status = consumeHandleError(
              Bridge.wrapEntity(LinkGraphPluginBridge::EntityKind::Archive,
                                Member.ArchiveID),
              &Out.Archive);
          if (Status.Code != NEVERC_STATUS_OK)
            return Status;
        }
        Out.Name = {Member.Name.data(), Member.Name.size()};
        Out.Ordinal = Member.Ordinal;
        std::copy(Member.ContentDigest.begin(), Member.ContentDigest.end(),
                  Out.ContentDigest);
        Out.Materialized =
            Member.Materialized ? NEVERC_TRUE : NEVERC_FALSE;
        Out.MaterializationReason = {
            Member.MaterializationReason.data(),
            Member.MaterializationReason.size()};
        Status = fillOrigin(Bridge, Member.Origin, Out.Origin);
        if (Status.Code != NEVERC_STATUS_OK)
          return Status;
        Out.Extensions = Member.Extensions.view();
        return neverc_status_ok();
      });
}

NevercStatus NEVERC_CALL GetSharedLibraryPage(
    void *Context, NevercTaskHandle Task, NevercLinkGraphHandle Graph,
    uint64_t Cursor, NevercLinkEntityPage *Page) {
  return getGraphPage<NevercLinkSharedLibraryInfo>(
      Context, Task, Graph, Cursor, Page,
      [](PluginLinkGraph &Value) -> const auto & {
        return Value.sharedLibraries();
      },
      [](LinkGraphPluginBridge &Bridge,
         const PluginLinkSharedLibrary &Library,
         NevercLinkSharedLibraryInfo &Out) {
        NevercStatus Status = consumeHandleError(
            Bridge.wrapEntity(
                LinkGraphPluginBridge::EntityKind::SharedLibrary,
                Library.ID),
            &Out.SharedLibrary);
        if (Status.Code != NEVERC_STATUS_OK)
          return Status;
        if (Library.InputID != 0) {
          Status = consumeHandleError(
              Bridge.wrapEntity(LinkGraphPluginBridge::EntityKind::Input,
                                Library.InputID),
              &Out.Input);
          if (Status.Code != NEVERC_STATUS_OK)
            return Status;
        }
        Out.Name = {Library.Name.data(), Library.Name.size()};
        Out.InstallName = {Library.InstallName.data(),
                           Library.InstallName.size()};
        std::copy(Library.ContentDigest.begin(),
                  Library.ContentDigest.end(), Out.ContentDigest);
        Status = fillOrigin(Bridge, Library.Origin, Out.Origin);
        if (Status.Code != NEVERC_STATUS_OK)
          return Status;
        Out.Extensions = Library.Extensions.view();
        return neverc_status_ok();
      });
}

NevercStatus NEVERC_CALL GetBitcodeModulePage(
    void *Context, NevercTaskHandle Task, NevercLinkGraphHandle Graph,
    uint64_t Cursor, NevercLinkEntityPage *Page) {
  return getGraphPage<NevercLinkBitcodeModuleInfo>(
      Context, Task, Graph, Cursor, Page,
      [](PluginLinkGraph &Value) -> const auto & {
        return Value.bitcodeModules();
      },
      [](LinkGraphPluginBridge &Bridge,
         const PluginLinkBitcodeModule &Module,
         NevercLinkBitcodeModuleInfo &Out) {
        NevercStatus Status = consumeHandleError(
            Bridge.wrapEntity(
                LinkGraphPluginBridge::EntityKind::BitcodeModule,
                Module.ID),
            &Out.Module);
        if (Status.Code != NEVERC_STATUS_OK)
          return Status;
        if (Module.InputID != 0) {
          Status = consumeHandleError(
              Bridge.wrapEntity(LinkGraphPluginBridge::EntityKind::Input,
                                Module.InputID),
              &Out.Input);
          if (Status.Code != NEVERC_STATUS_OK)
            return Status;
        }
        Out.Name = {Module.Name.data(), Module.Name.size()};
        std::copy(Module.ContentDigest.begin(), Module.ContentDigest.end(),
                  Out.ContentDigest);
        Out.Summary = Module.Summary;
        Status = fillOrigin(Bridge, Module.Origin, Out.Origin);
        if (Status.Code != NEVERC_STATUS_OK)
          return Status;
        Out.Extensions = Module.Extensions.view();
        return neverc_status_ok();
      });
}

NevercStatus NEVERC_CALL GetSectionPage(
    void *Context, NevercTaskHandle Task, NevercLinkGraphHandle Graph,
    uint64_t Cursor, NevercLinkEntityPage *Page) {
  NevercStatus Status;
  LinkGraphPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  PluginLinkGraph *Resolved = nullptr;
  Status = Bridge->resolveGraph(Graph, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return fillPage<NevercLinkSectionInfo>(
      *Bridge, Resolved->sections(), Cursor, Page,
      [](LinkGraphPluginBridge &Bridge, const PluginLinkSection &Section,
         NevercLinkSectionInfo &Out) {
        auto Handle = Bridge.wrapEntity(
            LinkGraphPluginBridge::EntityKind::Section, Section.ID);
        if (!Handle) {
          consumeError(Handle.takeError());
          return linkStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
        }
        Out.Section = *Handle;
        Out.Name = {Section.Name.data(), Section.Name.size()};
        Out.Kind = Section.Kind;
        Out.Flags = Section.Flags;
        Out.Alignment = Section.Alignment;
        Out.Address = Section.Address;
        Out.FileOffset = Section.FileOffset;
        Out.Size = Section.Size;
        if (Section.ComdatID != 0) {
          auto Comdat = Bridge.wrapEntity(
              LinkGraphPluginBridge::EntityKind::Comdat,
              Section.ComdatID);
          if (!Comdat) {
            consumeError(Comdat.takeError());
            return linkStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
          }
          Out.Comdat = *Comdat;
        }
        NevercStatus Status = fillOrigin(Bridge, Section.Origin, Out.Origin);
        if (Status.Code != NEVERC_STATUS_OK)
          return Status;
        Out.Extensions = Section.Extensions.view();
        return neverc_status_ok();
      });
}

NevercStatus NEVERC_CALL GetAtomPage(
    void *Context, NevercTaskHandle Task, NevercLinkGraphHandle Graph,
    uint64_t Cursor, NevercLinkEntityPage *Page) {
  NevercStatus Status;
  LinkGraphPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  PluginLinkGraph *Resolved = nullptr;
  Status = Bridge->resolveGraph(Graph, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return fillPage<NevercLinkAtomInfo>(
      *Bridge, Resolved->atoms(), Cursor, Page,
      [](LinkGraphPluginBridge &Bridge, const PluginLinkAtom &Atom,
         NevercLinkAtomInfo &Out) {
        auto Handle = Bridge.wrapEntity(
            LinkGraphPluginBridge::EntityKind::Atom, Atom.ID);
        auto Section = Bridge.wrapEntity(
            LinkGraphPluginBridge::EntityKind::Section, Atom.SectionID);
        if (!Handle || !Section) {
          if (!Handle)
            consumeError(Handle.takeError());
          if (!Section)
            consumeError(Section.takeError());
          return linkStatus(NEVERC_STATUS_VERIFICATION_FAILED);
        }
        Out.Atom = *Handle;
        Out.Section = *Section;
        Out.Name = {Atom.Name.data(), Atom.Name.size()};
        Out.Flags = Atom.Flags;
        Out.Alignment = Atom.Alignment;
        Out.Address = Atom.Address;
        Out.FileOffset = Atom.FileOffset;
        Out.Content = {Atom.Content.data(), Atom.Content.size()};
        Out.ZeroFillSize = Atom.ZeroFillSize;
        if (Atom.ComdatID != 0) {
          auto Comdat = Bridge.wrapEntity(
              LinkGraphPluginBridge::EntityKind::Comdat, Atom.ComdatID);
          if (!Comdat) {
            consumeError(Comdat.takeError());
            return linkStatus(NEVERC_STATUS_VERIFICATION_FAILED);
          }
          Out.Comdat = *Comdat;
        }
        if (Atom.FoldLeaderID != 0) {
          auto Leader = Bridge.wrapEntity(
              LinkGraphPluginBridge::EntityKind::Atom,
              Atom.FoldLeaderID);
          if (!Leader) {
            consumeError(Leader.takeError());
            return linkStatus(NEVERC_STATUS_VERIFICATION_FAILED);
          }
          Out.FoldLeader = *Leader;
        }
        NevercStatus Status = fillOrigin(Bridge, Atom.Origin, Out.Origin);
        if (Status.Code != NEVERC_STATUS_OK)
          return Status;
        Out.Extensions = Atom.Extensions.view();
        return neverc_status_ok();
      });
}

NevercStatus NEVERC_CALL GetSymbolPage(
    void *Context, NevercTaskHandle Task, NevercLinkGraphHandle Graph,
    uint64_t Cursor, NevercLinkEntityPage *Page) {
  NevercStatus Status;
  LinkGraphPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  PluginLinkGraph *Resolved = nullptr;
  Status = Bridge->resolveGraph(Graph, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return fillPage<NevercLinkSymbolInfo>(
      *Bridge, Resolved->symbols(), Cursor, Page,
      [](LinkGraphPluginBridge &Bridge, const PluginLinkSymbol &Symbol,
         NevercLinkSymbolInfo &Out) {
        auto Handle = Bridge.wrapEntity(
            LinkGraphPluginBridge::EntityKind::Symbol, Symbol.ID);
        if (!Handle) {
          consumeError(Handle.takeError());
          return linkStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
        }
        Out.Symbol = *Handle;
        Out.Name = {Symbol.Name.data(), Symbol.Name.size()};
        Out.Version = {Symbol.Version.data(), Symbol.Version.size()};
        Out.Binding = Symbol.Binding;
        Out.Visibility = Symbol.Visibility;
        Out.Definition = Symbol.Definition;
        Out.Type = Symbol.Type;
        if (Symbol.AtomID != 0) {
          auto Atom = Bridge.wrapEntity(
              LinkGraphPluginBridge::EntityKind::Atom, Symbol.AtomID);
          if (!Atom) {
            consumeError(Atom.takeError());
            return linkStatus(NEVERC_STATUS_VERIFICATION_FAILED);
          }
          Out.Atom = *Atom;
        }
        Out.Value = Symbol.Value;
        Out.Size = Symbol.Size;
        Out.IsPrevailing =
            Symbol.IsPrevailing ? NEVERC_TRUE : NEVERC_FALSE;
        Out.IsExported = Symbol.IsExported ? NEVERC_TRUE : NEVERC_FALSE;
        Out.IsImported = Symbol.IsImported ? NEVERC_TRUE : NEVERC_FALSE;
        Out.IsRoot = Symbol.IsRoot ? NEVERC_TRUE : NEVERC_FALSE;
        NevercStatus Status = fillOrigin(Bridge, Symbol.Origin, Out.Origin);
        if (Status.Code != NEVERC_STATUS_OK)
          return Status;
        Out.Extensions = Symbol.Extensions.view();
        return neverc_status_ok();
      });
}

NevercStatus NEVERC_CALL GetEdgePage(
    void *Context, NevercTaskHandle Task, NevercLinkGraphHandle Graph,
    uint64_t Cursor, NevercLinkEntityPage *Page) {
  NevercStatus Status;
  LinkGraphPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  PluginLinkGraph *Resolved = nullptr;
  Status = Bridge->resolveGraph(Graph, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return fillPage<NevercLinkEdgeInfo>(
      *Bridge, Resolved->edges(), Cursor, Page,
      [](LinkGraphPluginBridge &Bridge, const PluginLinkEdge &Edge,
         NevercLinkEdgeInfo &Out) {
        auto Handle = Bridge.wrapEntity(
            LinkGraphPluginBridge::EntityKind::Edge, Edge.ID);
        auto Source = Bridge.wrapEntity(
            LinkGraphPluginBridge::EntityKind::Atom,
            Edge.SourceAtomID);
        if (!Handle || !Source) {
          if (!Handle)
            consumeError(Handle.takeError());
          if (!Source)
            consumeError(Source.takeError());
          return linkStatus(NEVERC_STATUS_VERIFICATION_FAILED);
        }
        Out.Edge = *Handle;
        Out.Kind = Edge.Kind;
        Out.Source = *Source;
        Out.Offset = Edge.Offset;
        Out.RelocationKind = Edge.RelocationKind;
        Out.Width = Edge.Width;
        Out.Addend = Edge.Addend;
        Out.IsPCRelative =
            Edge.IsPCRelative ? NEVERC_TRUE : NEVERC_FALSE;
        Out.IsSigned = Edge.IsSigned ? NEVERC_TRUE : NEVERC_FALSE;
        if (Edge.TargetSymbolID != 0) {
          auto Target = Bridge.wrapEntity(
              LinkGraphPluginBridge::EntityKind::Symbol,
              Edge.TargetSymbolID);
          if (!Target) {
            consumeError(Target.takeError());
            return linkStatus(NEVERC_STATUS_VERIFICATION_FAILED);
          }
          Out.TargetSymbol = *Target;
        }
        if (Edge.TargetAtomID != 0) {
          auto Target = Bridge.wrapEntity(
              LinkGraphPluginBridge::EntityKind::Atom,
              Edge.TargetAtomID);
          if (!Target) {
            consumeError(Target.takeError());
            return linkStatus(NEVERC_STATUS_VERIFICATION_FAILED);
          }
          Out.TargetAtom = *Target;
        }
        NevercStatus Status = fillOrigin(Bridge, Edge.Origin, Out.Origin);
        if (Status.Code != NEVERC_STATUS_OK)
          return Status;
        Out.Extensions = Edge.Extensions.view();
        return neverc_status_ok();
      });
}

NevercStatus NEVERC_CALL GetComdatPage(
    void *Context, NevercTaskHandle Task, NevercLinkGraphHandle Graph,
    uint64_t Cursor, NevercLinkEntityPage *Page) {
  NevercStatus Status;
  LinkGraphPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  PluginLinkGraph *Resolved = nullptr;
  Status = Bridge->resolveGraph(Graph, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return fillPage<NevercLinkComdatInfo>(
      *Bridge, Resolved->comdats(), Cursor, Page,
      [](LinkGraphPluginBridge &Bridge, const PluginLinkComdat &Comdat,
         NevercLinkComdatInfo &Out) {
        auto Handle = Bridge.wrapEntity(
            LinkGraphPluginBridge::EntityKind::Comdat, Comdat.ID);
        if (!Handle) {
          consumeError(Handle.takeError());
          return linkStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
        }
        Out.Comdat = *Handle;
        Out.Name = {Comdat.Name.data(), Comdat.Name.size()};
        Out.Selection = Comdat.Selection;
        if (Comdat.SelectedID != 0) {
          auto Selected = Bridge.wrapEntity(
              LinkGraphPluginBridge::EntityKind::Comdat,
              Comdat.SelectedID);
          if (!Selected) {
            consumeError(Selected.takeError());
            return linkStatus(NEVERC_STATUS_VERIFICATION_FAILED);
          }
          Out.Selected = *Selected;
        }
        NevercStatus Status = fillOrigin(Bridge, Comdat.Origin, Out.Origin);
        if (Status.Code != NEVERC_STATUS_OK)
          return Status;
        Out.Extensions = Comdat.Extensions.view();
        return neverc_status_ok();
      });
}

NevercStatus NEVERC_CALL GetImportPage(
    void *Context, NevercTaskHandle Task, NevercLinkGraphHandle Graph,
    uint64_t Cursor, NevercLinkEntityPage *Page) {
  return getGraphPage<NevercLinkImportInfo>(
      Context, Task, Graph, Cursor, Page,
      [](PluginLinkGraph &Value) -> const auto & {
        return Value.imports();
      },
      [](LinkGraphPluginBridge &Bridge, const PluginLinkImport &Import,
         NevercLinkImportInfo &Out) {
        NevercStatus Status = consumeHandleError(
            Bridge.wrapEntity(LinkGraphPluginBridge::EntityKind::Import,
                              Import.ID),
            &Out.Import);
        if (Status.Code != NEVERC_STATUS_OK)
          return Status;
        Out.Name = {Import.Name.data(), Import.Name.size()};
        Out.Library = {Import.Library.data(), Import.Library.size()};
        if (Import.SymbolID != 0) {
          Status = consumeHandleError(
              Bridge.wrapEntity(LinkGraphPluginBridge::EntityKind::Symbol,
                                Import.SymbolID),
              &Out.Symbol);
          if (Status.Code != NEVERC_STATUS_OK)
            return Status;
        }
        Status = fillOrigin(Bridge, Import.Origin, Out.Origin);
        if (Status.Code != NEVERC_STATUS_OK)
          return Status;
        Out.Extensions = Import.Extensions.view();
        return neverc_status_ok();
      });
}

NevercStatus NEVERC_CALL GetExportPage(
    void *Context, NevercTaskHandle Task, NevercLinkGraphHandle Graph,
    uint64_t Cursor, NevercLinkEntityPage *Page) {
  return getGraphPage<NevercLinkExportInfo>(
      Context, Task, Graph, Cursor, Page,
      [](PluginLinkGraph &Value) -> const auto & {
        return Value.exports();
      },
      [](LinkGraphPluginBridge &Bridge, const PluginLinkExport &Export,
         NevercLinkExportInfo &Out) {
        NevercStatus Status = consumeHandleError(
            Bridge.wrapEntity(LinkGraphPluginBridge::EntityKind::Export,
                              Export.ID),
            &Out.Export);
        if (Status.Code != NEVERC_STATUS_OK)
          return Status;
        Out.Name = {Export.Name.data(), Export.Name.size()};
        if (Export.SymbolID != 0) {
          Status = consumeHandleError(
              Bridge.wrapEntity(LinkGraphPluginBridge::EntityKind::Symbol,
                                Export.SymbolID),
              &Out.Symbol);
          if (Status.Code != NEVERC_STATUS_OK)
            return Status;
        }
        Status = fillOrigin(Bridge, Export.Origin, Out.Origin);
        if (Status.Code != NEVERC_STATUS_OK)
          return Status;
        Out.Extensions = Export.Extensions.view();
        return neverc_status_ok();
      });
}

NevercStatus NEVERC_CALL GetUnwindPage(
    void *Context, NevercTaskHandle Task, NevercLinkGraphHandle Graph,
    uint64_t Cursor, NevercLinkEntityPage *Page) {
  return getGraphPage<NevercLinkUnwindInfo>(
      Context, Task, Graph, Cursor, Page,
      [](PluginLinkGraph &Value) -> const auto & {
        return Value.unwindRecords();
      },
      [](LinkGraphPluginBridge &Bridge,
         const PluginLinkUnwindRecord &Unwind,
         NevercLinkUnwindInfo &Out) {
        NevercStatus Status = consumeHandleError(
            Bridge.wrapEntity(LinkGraphPluginBridge::EntityKind::Unwind,
                              Unwind.ID),
            &Out.Unwind);
        if (Status.Code != NEVERC_STATUS_OK)
          return Status;
        if (Unwind.AtomID != 0) {
          Status = consumeHandleError(
              Bridge.wrapEntity(LinkGraphPluginBridge::EntityKind::Atom,
                                Unwind.AtomID),
              &Out.Atom);
          if (Status.Code != NEVERC_STATUS_OK)
            return Status;
        }
        if (Unwind.PersonalitySymbolID != 0) {
          Status = consumeHandleError(
              Bridge.wrapEntity(LinkGraphPluginBridge::EntityKind::Symbol,
                                Unwind.PersonalitySymbolID),
              &Out.PersonalitySymbol);
          if (Status.Code != NEVERC_STATUS_OK)
            return Status;
        }
        Status = fillOrigin(Bridge, Unwind.Origin, Out.Origin);
        if (Status.Code != NEVERC_STATUS_OK)
          return Status;
        Out.Extensions = Unwind.Extensions.view();
        return neverc_status_ok();
      });
}

NevercStatus NEVERC_CALL GetSyntheticPage(
    void *Context, NevercTaskHandle Task, NevercLinkGraphHandle Graph,
    uint64_t Cursor, NevercLinkEntityPage *Page) {
  return getGraphPage<NevercLinkSyntheticInfo>(
      Context, Task, Graph, Cursor, Page,
      [](PluginLinkGraph &Value) -> const auto & {
        return Value.synthetics();
      },
      [](LinkGraphPluginBridge &Bridge,
         const PluginLinkSynthetic &Synthetic,
         NevercLinkSyntheticInfo &Out) {
        NevercStatus Status = consumeHandleError(
            Bridge.wrapEntity(LinkGraphPluginBridge::EntityKind::Synthetic,
                              Synthetic.ID),
            &Out.Synthetic);
        if (Status.Code != NEVERC_STATUS_OK)
          return Status;
        Out.Role = {Synthetic.Role.data(), Synthetic.Role.size()};
        if (Synthetic.SectionID != 0) {
          Status = consumeHandleError(
              Bridge.wrapEntity(LinkGraphPluginBridge::EntityKind::Section,
                                Synthetic.SectionID),
              &Out.Section);
          if (Status.Code != NEVERC_STATUS_OK)
            return Status;
        }
        if (Synthetic.AtomID != 0) {
          Status = consumeHandleError(
              Bridge.wrapEntity(LinkGraphPluginBridge::EntityKind::Atom,
                                Synthetic.AtomID),
              &Out.Atom);
          if (Status.Code != NEVERC_STATUS_OK)
            return Status;
        }
        Status = fillOrigin(Bridge, Synthetic.Origin, Out.Origin);
        if (Status.Code != NEVERC_STATUS_OK)
          return Status;
        Out.Extensions = Synthetic.Extensions.view();
        return neverc_status_ok();
      });
}

NevercStatus NEVERC_CALL GetConstraintPage(
    void *Context, NevercTaskHandle Task, NevercLinkGraphHandle Graph,
    uint64_t Cursor, NevercLinkEntityPage *Page) {
  return getGraphPage<NevercLinkConstraintInfo>(
      Context, Task, Graph, Cursor, Page,
      [](PluginLinkGraph &Value) -> const auto & {
        return Value.constraints();
      },
      [](LinkGraphPluginBridge &Bridge,
         const PluginLinkConstraint &Constraint,
         NevercLinkConstraintInfo &Out) {
        NevercStatus Status = consumeHandleError(
            Bridge.wrapEntity(LinkGraphPluginBridge::EntityKind::Constraint,
                              Constraint.ID),
            &Out.Constraint);
        if (Status.Code != NEVERC_STATUS_OK)
          return Status;
        Out.Kind = {Constraint.Kind.data(), Constraint.Kind.size()};
        Out.SubjectID = Constraint.SubjectID;
        Out.Value = Constraint.Value;
        Out.Required =
            Constraint.Required ? NEVERC_TRUE : NEVERC_FALSE;
        Status = fillOrigin(Bridge, Constraint.Origin, Out.Origin);
        if (Status.Code != NEVERC_STATUS_OK)
          return Status;
        Out.Extensions = Constraint.Extensions.view();
        return neverc_status_ok();
      });
}

template <typename Entity, typename Info, typename Finder, typename Fill>
NevercStatus getEntityInfo(void *Context, NevercTaskHandle Task,
                           NevercHandle Handle,
                           LinkGraphPluginBridge::EntityKind Kind,
                           Info *OutInfo, Finder Find, Fill FillInfo) {
  NevercStatus Status;
  LinkGraphPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  if (!validRecord(OutInfo))
    return linkStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  uint64_t ID = 0;
  Status = Bridge->resolveEntity(Handle, Kind, &ID);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Entity *Resolved = Find(Bridge->graphValue(), ID);
  if (!Resolved)
    return linkStatus(NEVERC_STATUS_STALE_HANDLE);
  std::memset(OutInfo, 0, sizeof(*OutInfo));
  OutInfo->Header = {sizeof(*OutInfo), NEVERC_LINK_API_MAJOR,
                     NEVERC_LINK_API_MINOR, 0};
  return FillInfo(*Bridge, *Resolved, *OutInfo);
}

NevercStatus NEVERC_CALL GetInputInfo(
    void *Context, NevercTaskHandle Task, NevercLinkInputHandle Input,
    NevercLinkInputInfo *OutInfo) {
  return getEntityInfo<PluginLinkInput>(
      Context, Task, Input, LinkGraphPluginBridge::EntityKind::Input,
      OutInfo,
      [](PluginLinkGraph &Graph, uint64_t ID) {
        return Graph.findInput(ID);
      },
      [](LinkGraphPluginBridge &Bridge, const PluginLinkInput &Value,
         NevercLinkInputInfo &Out) {
        auto GraphHandle = Bridge.graph();
        if (!GraphHandle) {
          consumeError(GraphHandle.takeError());
          return linkStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
        }
        uint64_t Cursor = 0;
        for (const auto &Candidate : Bridge.graphValue().inputs()) {
          if (Candidate.ID == Value.ID)
            break;
          ++Cursor;
        }
        NevercLinkEntityPage Page{{sizeof(Page), NEVERC_LINK_API_MAJOR,
                                   NEVERC_LINK_API_MINOR, 0},
                                  &Out, 1, sizeof(Out), 0, 0, 0, 0};
        return GetInputPage(Bridge.api().Context, Bridge.taskHandle(),
                            *GraphHandle, Cursor, &Page);
      });
}

#define NEVERC_DEFINE_LINK_GET_INFO(Name, EntityType, InfoType, KindName,       \
                                    FindName, PageName, StorageName)            \
  NevercStatus NEVERC_CALL Get##Name##Info(                                    \
      void *Context, NevercTaskHandle Task, NevercLink##Name##Handle Handle,   \
      InfoType *OutInfo) {                                                     \
    return getEntityInfo<EntityType>(                                          \
        Context, Task, Handle, LinkGraphPluginBridge::EntityKind::KindName,    \
        OutInfo,                                                               \
        [](PluginLinkGraph &Graph, uint64_t ID) { return Graph.FindName(ID); }, \
        [](LinkGraphPluginBridge &Bridge, const EntityType &Value,             \
           InfoType &Out) {                                                    \
          auto GraphHandle = Bridge.graph();                                   \
          if (!GraphHandle) {                                                  \
            consumeError(GraphHandle.takeError());                             \
            return linkStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);               \
          }                                                                    \
          uint64_t Cursor = 0;                                                 \
          for (const auto &Candidate : Bridge.graphValue().StorageName()) {     \
            if (Candidate.ID == Value.ID)                                      \
              break;                                                           \
            ++Cursor;                                                          \
          }                                                                    \
          NevercLinkEntityPage Page{                                           \
              {sizeof(Page), NEVERC_LINK_API_MAJOR,                            \
               NEVERC_LINK_API_MINOR, 0},                                      \
              &Out, 1, sizeof(Out), 0, 0, 0, 0};                               \
          return PageName(Bridge.api().Context, Bridge.taskHandle(),           \
                          *GraphHandle, Cursor, &Page);                         \
        });                                                                    \
  }

NEVERC_DEFINE_LINK_GET_INFO(Section, PluginLinkSection,
                            NevercLinkSectionInfo, Section, findSection,
                            GetSectionPage, sections)
NEVERC_DEFINE_LINK_GET_INFO(Archive, PluginLinkArchive,
                            NevercLinkArchiveInfo, Archive, findArchive,
                            GetArchivePage, archives)
NEVERC_DEFINE_LINK_GET_INFO(ArchiveMember, PluginLinkArchiveMember,
                            NevercLinkArchiveMemberInfo, ArchiveMember,
                            findArchiveMember, GetArchiveMemberPage,
                            archiveMembers)
NEVERC_DEFINE_LINK_GET_INFO(SharedLibrary, PluginLinkSharedLibrary,
                            NevercLinkSharedLibraryInfo, SharedLibrary,
                            findSharedLibrary, GetSharedLibraryPage,
                            sharedLibraries)
NEVERC_DEFINE_LINK_GET_INFO(BitcodeModule, PluginLinkBitcodeModule,
                            NevercLinkBitcodeModuleInfo, BitcodeModule,
                            findBitcodeModule, GetBitcodeModulePage,
                            bitcodeModules)
NEVERC_DEFINE_LINK_GET_INFO(Atom, PluginLinkAtom, NevercLinkAtomInfo, Atom,
                            findAtom, GetAtomPage, atoms)
NEVERC_DEFINE_LINK_GET_INFO(Symbol, PluginLinkSymbol,
                            NevercLinkSymbolInfo, Symbol, findSymbol,
                            GetSymbolPage, symbols)
NEVERC_DEFINE_LINK_GET_INFO(Edge, PluginLinkEdge, NevercLinkEdgeInfo, Edge,
                            findEdge, GetEdgePage, edges)
NEVERC_DEFINE_LINK_GET_INFO(Comdat, PluginLinkComdat,
                            NevercLinkComdatInfo, Comdat, findComdat,
                            GetComdatPage, comdats)
NEVERC_DEFINE_LINK_GET_INFO(Import, PluginLinkImport,
                            NevercLinkImportInfo, Import, findImport,
                            GetImportPage, imports)
NEVERC_DEFINE_LINK_GET_INFO(Export, PluginLinkExport,
                            NevercLinkExportInfo, Export, findExport,
                            GetExportPage, exports)
NEVERC_DEFINE_LINK_GET_INFO(Unwind, PluginLinkUnwindRecord,
                            NevercLinkUnwindInfo, Unwind, findUnwind,
                            GetUnwindPage, unwindRecords)
NEVERC_DEFINE_LINK_GET_INFO(Synthetic, PluginLinkSynthetic,
                            NevercLinkSyntheticInfo, Synthetic, findSynthetic,
                            GetSyntheticPage, synthetics)
NEVERC_DEFINE_LINK_GET_INFO(Constraint, PluginLinkConstraint,
                            NevercLinkConstraintInfo, Constraint,
                            findConstraint, GetConstraintPage, constraints)

#undef NEVERC_DEFINE_LINK_GET_INFO

NevercStatus NEVERC_CALL UnsupportedRequest(
    void *, NevercTaskHandle, NevercLinkRequestHandle, NevercLinkRequest *) {
  return linkStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
}

NevercStatus NEVERC_CALL GetProofInfo(
    void *Context, NevercTaskHandle Task, NevercLinkProofHandle Proof,
    NevercLinkProofInfo *OutInfo) {
  NevercStatus Status;
  LinkGraphPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  if (!validRecord(OutInfo))
    return linkStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  const PluginLinkProof *Resolved = nullptr;
  Status = Bridge->resolveProof(Proof, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto Graph = Bridge->graph();
  if (!Graph) {
    consumeError(Graph.takeError());
    return linkStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  std::memset(OutInfo, 0, sizeof(*OutInfo));
  OutInfo->Header = {sizeof(*OutInfo), NEVERC_LINK_API_MAJOR,
                     NEVERC_LINK_API_MINOR, 0};
  OutInfo->Proof = Proof;
  OutInfo->Graph = *Graph;
  OutInfo->State = Resolved->State;
  OutInfo->GraphGeneration = Resolved->GraphGeneration;
  OutInfo->TargetID = Resolved->TargetID;
  OutInfo->FormatID = Resolved->FormatID;
  OutInfo->OutputArtifact = Resolved->OutputArtifact;
  std::copy(Resolved->RouteDigest.begin(), Resolved->RouteDigest.end(),
            OutInfo->RouteDigest);
  std::copy(Resolved->SemanticDigest.begin(),
            Resolved->SemanticDigest.end(), OutInfo->SemanticDigest);
  OutInfo->ImageBase = Resolved->ImageBase;
  OutInfo->EntryAddress = Resolved->EntryAddress;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL UnsupportedImage(
    void *, NevercTaskHandle, NevercBinaryImageHandle,
    NevercBinaryImageInfo *) {
  return linkStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
}

NevercStatus NEVERC_CALL UnsupportedImagePage(
    void *, NevercTaskHandle, NevercBinaryImageHandle, uint64_t,
    NevercLinkEntityPage *) {
  return linkStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
}

} // namespace

LinkGraphPluginBridge::LinkGraphPluginBridge(PluginTaskContext &TaskValue,
                                             PluginLinkGraph &GraphValue,
                                             bool AllowMutationValue)
    : Task(TaskValue), Graph(GraphValue),
      MutationAllowed(AllowMutationValue) {
  API.Header = {sizeof(API), NEVERC_LINK_API_MAJOR,
                NEVERC_LINK_API_MINOR, 0};
  initializeLinkGraphAPI(API, *this);
}

LinkGraphPluginBridge::~LinkGraphPluginBridge() {
  if (hasActiveMutation())
    finishMutation();
  invalidateEntityHandles();
  for (NevercLinkProofHandle Handle : ProofHandles)
    (void)Task.handles().release(Handle, PluginLinkProofHandleKind);
  ProofHandles.clear();
  Proofs.clear();
  if (!neverc_handle_is_null(GraphHandle))
    (void)Task.handles().release(GraphHandle,
                                 PluginLinkGraphHandleKind);
}

NevercTaskHandle LinkGraphPluginBridge::taskHandle() const {
  return Task.handle();
}

Expected<NevercLinkGraphHandle> LinkGraphPluginBridge::graph() {
  if (!neverc_handle_is_null(GraphHandle))
    return GraphHandle;
  auto Handle =
      Task.handles().create(PluginLinkGraphHandleKind, &Graph);
  if (!Handle)
    return Handle.takeError();
  GraphHandle = *Handle;
  return GraphHandle;
}

Expected<NevercLinkProofHandle>
LinkGraphPluginBridge::issueProof(
    NevercLinkState State, NevercInterfaceID OutputArtifact,
    std::array<uint8_t, 32> RouteDigest) {
  if (hasActiveMutation())
    return createStringError(inconvertibleErrorCode(),
                             "cannot issue LinkGraph proof during mutation");
  if (State != Graph.state())
    return createStringError(
        inconvertibleErrorCode(),
        "LinkGraph proof state does not match committed graph state");
  if (Error E = verifyPluginLinkGraph(Graph))
    return std::move(E);
  auto Proof = std::make_unique<PluginLinkProof>();
  Proof->Graph = &Graph;
  Proof->GraphGeneration = Graph.generation();
  Proof->State = State;
  Proof->TargetID = Graph.targetKey().TargetID;
  Proof->FormatID = Graph.formatID();
  Proof->OutputArtifact = OutputArtifact;
  Proof->RouteDigest = RouteDigest;
  Proof->SemanticDigest = Graph.semanticDigest();
  if (State >= NEVERC_LINK_STATE_LAYOUT_COMPLETE) {
    Proof->ImageBase = UINT64_MAX;
    for (const PluginLinkSection &Section : Graph.sections())
      if ((Section.Flags & NEVERC_OBJECT_SECTION_ALLOCATED) != 0)
        Proof->ImageBase =
            std::min(Proof->ImageBase, Section.Address);
    if (Proof->ImageBase == UINT64_MAX)
      Proof->ImageBase = 0;
    for (const PluginLinkSymbol &Symbol : Graph.symbols()) {
      if (!Symbol.IsRoot || Symbol.AtomID == 0)
        continue;
      if (const PluginLinkAtom *Atom =
              Graph.findAtom(Symbol.AtomID)) {
        Proof->EntryAddress = Atom->Address + Symbol.Value;
        if (Symbol.Name == "entry")
          break;
      }
    }
  }
  auto Handle =
      Task.handles().create(PluginLinkProofHandleKind, Proof.get());
  if (!Handle)
    return Handle.takeError();
  ProofHandles.push_back(*Handle);
  Proofs.push_back(std::move(Proof));
  return *Handle;
}

NevercStatus LinkGraphPluginBridge::resolveProof(
    NevercLinkProofHandle Handle,
    const PluginLinkProof **OutProof) const {
  if (!OutProof)
    return linkStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutProof = nullptr;
  void *Payload = nullptr;
  NevercStatus Status =
      Task.handles().resolve(Handle, PluginLinkProofHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto It = std::find_if(
      Proofs.begin(), Proofs.end(),
      [Payload](const std::unique_ptr<PluginLinkProof> &Candidate) {
        return Candidate.get() == Payload;
      });
  if (It == Proofs.end() || (*It)->Graph != &Graph)
    return linkStatus(NEVERC_STATUS_WRONG_SCOPE);
  if ((*It)->GraphGeneration != Graph.generation())
    return linkStatus(NEVERC_STATUS_STALE_HANDLE);
  *OutProof = It->get();
  return neverc_status_ok();
}

Expected<NevercHandle>
LinkGraphPluginBridge::wrapEntity(EntityKind Kind, uint64_t ID) {
  bool Exists = false;
  PluginLinkGraph &Active = activeGraph();
  switch (Kind) {
  case EntityKind::Input:
    Exists = Active.findInput(ID) != nullptr;
    break;
  case EntityKind::Archive:
    Exists = Active.findArchive(ID) != nullptr;
    break;
  case EntityKind::ArchiveMember:
    Exists = Active.findArchiveMember(ID) != nullptr;
    break;
  case EntityKind::SharedLibrary:
    Exists = Active.findSharedLibrary(ID) != nullptr;
    break;
  case EntityKind::BitcodeModule:
    Exists = Active.findBitcodeModule(ID) != nullptr;
    break;
  case EntityKind::Section:
    Exists = Active.findSection(ID) != nullptr;
    break;
  case EntityKind::Atom:
    Exists = Active.findAtom(ID) != nullptr;
    break;
  case EntityKind::Symbol:
    Exists = Active.findSymbol(ID) != nullptr;
    break;
  case EntityKind::Edge:
    Exists = Active.findEdge(ID) != nullptr;
    break;
  case EntityKind::Comdat:
    Exists = Active.findComdat(ID) != nullptr;
    break;
  case EntityKind::Import:
    Exists = Active.findImport(ID) != nullptr;
    break;
  case EntityKind::Export:
    Exists = Active.findExport(ID) != nullptr;
    break;
  case EntityKind::Unwind:
    Exists = Active.findUnwind(ID) != nullptr;
    break;
  case EntityKind::Synthetic:
    Exists = Active.findSynthetic(ID) != nullptr;
    break;
  case EntityKind::Constraint:
    Exists = Active.findConstraint(ID) != nullptr;
    break;
  }
  if (!Exists)
    return createStringError(inconvertibleErrorCode(),
                             "LinkGraph entity does not exist");

  const PluginHandleKind HandleKind = handleKind(Kind);
  for (const auto &Entry : EntityHandles) {
    if (Entry.second != HandleKind)
      continue;
    void *Payload = nullptr;
    if (Task.handles().resolve(Entry.first, HandleKind, &Payload).Code !=
        NEVERC_STATUS_OK)
      continue;
    const auto *Reference = static_cast<EntityReference *>(Payload);
    if (Reference->Bridge == this && Reference->Kind == Kind &&
        Reference->ID == ID &&
        Reference->Generation == BridgeGeneration)
      return Entry.first;
  }

  auto *Reference = new (std::nothrow)
      EntityReference{this, Kind, ID, BridgeGeneration};
  if (!Reference)
    return createStringError(inconvertibleErrorCode(),
                             "failed to allocate LinkGraph reference");
  auto Handle = Task.handles().create(
      HandleKind, Reference,
      [](void *Value) { delete static_cast<EntityReference *>(Value); });
  if (!Handle) {
    delete Reference;
    return Handle.takeError();
  }
  EntityHandles.push_back({*Handle, HandleKind});
  return *Handle;
}

NevercStatus LinkGraphPluginBridge::resolveGraph(
    NevercLinkGraphHandle Handle, PluginLinkGraph **OutGraph) const {
  if (!OutGraph)
    return linkStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutGraph = nullptr;
  void *Payload = nullptr;
  NevercStatus Status = Task.handles().resolve(
      Handle, PluginLinkGraphHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Payload != &Graph || !sameHandle(Handle, GraphHandle))
    return linkStatus(NEVERC_STATUS_WRONG_SCOPE);
  *OutGraph = &activeGraph();
  return neverc_status_ok();
}

NevercStatus LinkGraphPluginBridge::resolveEntity(
    NevercHandle Handle, EntityKind Kind, uint64_t *OutID) const {
  if (!OutID)
    return linkStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutID = 0;
  void *Payload = nullptr;
  NevercStatus Status =
      Task.handles().resolve(Handle, handleKind(Kind), &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  const auto *Reference = static_cast<EntityReference *>(Payload);
  if (Reference->Bridge != this || Reference->Kind != Kind)
    return linkStatus(NEVERC_STATUS_WRONG_SCOPE);
  if (Reference->Generation != BridgeGeneration)
    return linkStatus(NEVERC_STATUS_STALE_HANDLE);
  *OutID = Reference->ID;
  return neverc_status_ok();
}

void LinkGraphPluginBridge::invalidateEntityHandles() {
  for (const auto &Entry : EntityHandles)
    (void)Task.handles().release(Entry.first, Entry.second);
  EntityHandles.clear();
  ++BridgeGeneration;
  if (BridgeGeneration == 0)
    BridgeGeneration = 1;
}

void initializeLinkGraphAPI(NevercLinkAPI &API,
                            LinkGraphPluginBridge &Bridge) {
  API.Context = &Bridge;
  API.GetRequest = UnsupportedRequest;
  API.GetGraphInfo = GetGraphInfo;
  API.GetInputPage = GetInputPage;
  API.GetArchivePage = GetArchivePage;
  API.GetArchiveMemberPage = GetArchiveMemberPage;
  API.GetSharedLibraryPage = GetSharedLibraryPage;
  API.GetBitcodeModulePage = GetBitcodeModulePage;
  API.GetSectionPage = GetSectionPage;
  API.GetAtomPage = GetAtomPage;
  API.GetSymbolPage = GetSymbolPage;
  API.GetEdgePage = GetEdgePage;
  API.GetComdatPage = GetComdatPage;
  API.GetImportPage = GetImportPage;
  API.GetExportPage = GetExportPage;
  API.GetUnwindPage = GetUnwindPage;
  API.GetSyntheticPage = GetSyntheticPage;
  API.GetConstraintPage = GetConstraintPage;
  API.GetInputInfo = GetInputInfo;
  API.GetArchiveInfo = GetArchiveInfo;
  API.GetArchiveMemberInfo = GetArchiveMemberInfo;
  API.GetSharedLibraryInfo = GetSharedLibraryInfo;
  API.GetBitcodeModuleInfo = GetBitcodeModuleInfo;
  API.GetSectionInfo = GetSectionInfo;
  API.GetAtomInfo = GetAtomInfo;
  API.GetSymbolInfo = GetSymbolInfo;
  API.GetEdgeInfo = GetEdgeInfo;
  API.GetComdatInfo = GetComdatInfo;
  API.GetImportInfo = GetImportInfo;
  API.GetExportInfo = GetExportInfo;
  API.GetUnwindInfo = GetUnwindInfo;
  API.GetSyntheticInfo = GetSyntheticInfo;
  API.GetConstraintInfo = GetConstraintInfo;
  API.GetProofInfo = GetProofInfo;
  API.GetBinaryImageInfo = UnsupportedImage;
  API.GetBinarySegmentPage = UnsupportedImagePage;
  API.GetBinarySectionPage = UnsupportedImagePage;
  initializeLinkMutationAPI(API, Bridge);
}

} // namespace neverc::plugin
