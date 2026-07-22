#include "LinkInputReaderInternal.h"
#include "ObjectGraphImporter.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SHA256.h"
#include <algorithm>
#include <cstring>

using namespace llvm;

namespace neverc::plugin {
namespace {

Error inputError(StringRef URI, const Twine &Message) {
  return createStringError(errc::invalid_argument,
                           "link input '" + URI + "': " + Message);
}

Expected<std::unique_ptr<MemoryBuffer>>
freezeInput(vfs::FileSystem &FileSystem, const OwnedRawLinkInput &Input) {
  if (!Input.AuthorizedBlob.empty()) {
    StringRef Bytes(
        reinterpret_cast<const char *>(Input.AuthorizedBlob.data()),
        Input.AuthorizedBlob.size());
    return MemoryBuffer::getMemBufferCopy(Bytes, Input.LogicalURI);
  }

  if (Input.LogicalURI.empty())
    return inputError("<anonymous>", "has no VFS URI or authorized blob");
  ErrorOr<std::unique_ptr<MemoryBuffer>> Buffer =
      FileSystem.getBufferForFile(Input.LogicalURI);
  if (!Buffer)
    return errorCodeToError(Buffer.getError());
  return std::move(*Buffer);
}

NevercLinkInputKind detectedKind(file_magic Magic) {
  switch (Magic) {
  case file_magic::archive:
  case file_magic::coff_import_library:
    return NEVERC_LINK_INPUT_ARCHIVE;
  case file_magic::bitcode:
    return NEVERC_LINK_INPUT_BITCODE;
  case file_magic::elf_relocatable:
  case file_magic::macho_object:
  case file_magic::coff_object:
  case file_magic::xcoff_object_32:
  case file_magic::xcoff_object_64:
    return NEVERC_LINK_INPUT_OBJECT;
  case file_magic::elf_shared_object:
  case file_magic::macho_dynamically_linked_shared_lib:
  case file_magic::macho_dynamically_linked_shared_lib_stub:
  case file_magic::tapi_file:
    return NEVERC_LINK_INPUT_SHARED_LIBRARY;
  default:
    return NEVERC_LINK_INPUT_UNKNOWN;
  }
}

Expected<NevercLinkInputKind>
classifyInput(const OwnedRawLinkInput &Requested, StringRef Bytes) {
  const file_magic Magic = identify_magic(Bytes);
  NevercLinkInputKind Detected = detectedKind(Magic);
  if ((Magic == file_magic::macho_universal_binary ||
       Magic == file_magic::pecoff_executable) &&
      Requested.Kind == NEVERC_LINK_INPUT_SHARED_LIBRARY)
    Detected = NEVERC_LINK_INPUT_SHARED_LIBRARY;
  if (Detected == NEVERC_LINK_INPUT_UNKNOWN) {
    if (Requested.Kind == NEVERC_LINK_INPUT_OBJECT ||
        Requested.Kind == NEVERC_LINK_INPUT_SCRIPT ||
        Requested.Kind == NEVERC_LINK_INPUT_BLOB)
      return Requested.Kind;
    return inputError(Requested.LogicalURI, "unrecognized input format");
  }

  if (Requested.Kind != NEVERC_LINK_INPUT_BLOB &&
      Requested.Kind != Detected)
    return inputError(Requested.LogicalURI,
                      "declared kind does not match probed content");
  return Detected;
}

StringRef displayName(StringRef URI) {
  StringRef Name = sys::path::filename(URI);
  return Name.empty() ? URI : Name;
}

Error checkCancellation(PluginTaskContext &Task) {
  NevercStatus Status = Task.checkCancelled();
  if (Status.Code == NEVERC_STATUS_OK)
    return Error::success();
  return createStringError(
      std::make_error_code(std::errc::operation_canceled),
                           "link input reading was cancelled");
}

} // namespace

LinkInputSetImpl::LinkInputSetImpl(
    PluginTaskContext &TaskValue, vfs::FileSystem &FileSystemValue,
    const ObjectReaderProvider &ObjectsValue, OwnedTargetKey TargetValue,
    NevercObjectFormatID InputFormatValue,
    LinkInputReaderOptions OptionsValue,
    const LinkerScriptProvider &ScriptsValue)
    : Task(TaskValue), FileSystem(FileSystemValue), Objects(ObjectsValue),
      Target(std::move(TargetValue)),
      InputFormat(InputFormatValue), Options(OptionsValue),
      Scripts(ScriptsValue),
      Graph(std::make_unique<PluginLinkGraph>(Target)) {}

Expected<NevercObjectGraphHandle>
LinkInputSetImpl::addObject(uint64_t OriginID,
                            std::unique_ptr<PluginObjectGraph> Object,
                            ArrayRef<uint8_t> SourceBytes) {
  LinkObjectStorage Storage;
  Storage.OriginID = OriginID;
  Storage.Graph = std::move(Object);
  Storage.SourceBytes = SourceBytes;
  Storage.Bridge =
      std::make_unique<ObjectPluginBridge>(Task, *Storage.Graph);
  auto Handle = Storage.Bridge->graph();
  if (!Handle)
    return Handle.takeError();
  ObjectGraphImportOptions ImportOptions;
  ImportOptions.ObjectGraph = *Handle;
  if (const PluginLinkInput *Input = Graph->findInput(OriginID)) {
    ImportOptions.InputID = Input->ID;
  } else if (const PluginLinkArchiveMember *Member =
                 Graph->findArchiveMember(OriginID)) {
    ImportOptions.InputID = Member->InputID;
    ImportOptions.ArchiveMemberID = Member->ID;
  } else {
    return createStringError(
        errc::invalid_argument,
        "object input has no normalized input or archive-member origin");
  }
  auto Imported =
      importObjectGraph(*Graph, *Storage.Graph, ImportOptions);
  if (!Imported)
    return Imported.takeError();
  PluginObjectGraph *GraphValue = Storage.Graph.get();
  ObjectStorage.push_back(std::move(Storage));
  ObjectsByInput[OriginID] = GraphValue;
  return *Handle;
}

LinkInputSet::LinkInputSet(std::unique_ptr<LinkInputSetImpl> ImplValue)
    : Impl(std::move(ImplValue)) {}
LinkInputSet::~LinkInputSet() = default;
LinkInputSet::LinkInputSet(LinkInputSet &&) noexcept = default;
LinkInputSet &LinkInputSet::operator=(LinkInputSet &&) noexcept = default;

PluginLinkGraph &LinkInputSet::graph() { return *Impl->Graph; }
const PluginLinkGraph &LinkInputSet::graph() const { return *Impl->Graph; }

PluginObjectGraph *LinkInputSet::objectGraphForInput(uint64_t InputID) {
  auto It = Impl->ObjectsByInput.find(InputID);
  return It == Impl->ObjectsByInput.end() ? nullptr : It->second;
}

const PluginObjectGraph *
LinkInputSet::objectGraphForInput(uint64_t InputID) const {
  auto It = Impl->ObjectsByInput.find(InputID);
  return It == Impl->ObjectsByInput.end() ? nullptr : It->second;
}

std::vector<PluginObjectGraph *> LinkInputSet::objectGraphs() {
  std::vector<PluginObjectGraph *> Result;
  Result.reserve(Impl->ObjectStorage.size());
  for (LinkObjectStorage &Storage : Impl->ObjectStorage)
    Result.push_back(Storage.Graph.get());
  return Result;
}

std::vector<const PluginObjectGraph *> LinkInputSet::objectGraphs() const {
  std::vector<const PluginObjectGraph *> Result;
  Result.reserve(Impl->ObjectStorage.size());
  for (const LinkObjectStorage &Storage : Impl->ObjectStorage)
    Result.push_back(Storage.Graph.get());
  return Result;
}

std::vector<ArrayRef<uint8_t>>
LinkInputSet::objectGraphSourceBytes() const {
  std::vector<ArrayRef<uint8_t>> Result;
  Result.reserve(Impl->ObjectStorage.size());
  for (const LinkObjectStorage &Storage : Impl->ObjectStorage)
    Result.push_back(Storage.SourceBytes);
  return Result;
}

Expected<MemoryBufferRef>
LinkInputSet::bitcodeBufferForModule(uint64_t ModuleID) {
  const PluginLinkBitcodeModule *Module =
      Impl->Graph->findBitcodeModule(ModuleID);
  if (!Module)
    return createStringError(errc::invalid_argument,
                             "unknown bitcode module ID " +
                                 std::to_string(ModuleID));
  if (Module->Origin.ArchiveMemberID != 0) {
    auto ArchiveIt =
        Impl->ArchiveIndexByMemberID.find(
            Module->Origin.ArchiveMemberID);
    if (ArchiveIt == Impl->ArchiveIndexByMemberID.end())
      return createStringError(
          errc::invalid_argument,
          "bitcode module archive origin is not materialized");
    LinkArchiveState &Archive = Impl->Archives[ArchiveIt->second];
    auto MemberIt = Archive.MemberIndexByID.find(
        Module->Origin.ArchiveMemberID);
    if (MemberIt == Archive.MemberIndexByID.end())
      return createStringError(
          errc::invalid_argument,
          "bitcode module archive member index is inconsistent");
    return Impl->archiveMemberBuffer(
        Archive, Archive.Members[MemberIt->second]);
  }
  auto BlobIt = llvm::find_if(
      Impl->Blobs, [&](const std::unique_ptr<LinkInputBlob> &Blob) {
        return Blob->InputID == Module->InputID;
      });
  if (BlobIt == Impl->Blobs.end() || !(*BlobIt)->Buffer)
    return createStringError(
        errc::invalid_argument,
        "bitcode module has no immutable input buffer");
  return (*BlobIt)->Buffer->getMemBufferRef();
}

const LinkerScriptResult *
LinkInputSet::scriptResultForInput(uint64_t InputID) const {
  auto It = Impl->ScriptResults.find(InputID);
  return It == Impl->ScriptResults.end() ? nullptr : &It->second;
}

Error LinkInputSet::materializeArchiveMember(uint64_t MemberID,
                                             StringRef Reason) {
  return Impl->materializeArchiveMember(MemberID, Reason);
}

Error LinkInputSet::materializeWholeArchives() {
  for (LinkArchiveState &Archive : Impl->Archives) {
    const PluginLinkInput *Input = Impl->Graph->findInput(Archive.InputID);
    if (!Input ||
        (Input->Flags & NEVERC_LINK_INPUT_FLAG_WHOLE_ARCHIVE) == 0)
      continue;
    for (const LinkArchiveMemberState &Member : Archive.Members)
      if (Error E =
              Impl->materializeArchiveMember(Member.MemberID, "whole-archive"))
        return E;
  }
  return Error::success();
}

Expected<size_t> LinkInputSet::materializeArchiveSymbols(
    ArrayRef<StringRef> UndefinedSymbols) {
  size_t Materialized = 0;
  bool Changed = false;
  do {
    Changed = false;
    for (LinkArchiveState &Archive : Impl->Archives) {
      for (StringRef Symbol : UndefinedSymbols) {
        auto It = Archive.SymbolMembers.find(Symbol);
        if (It == Archive.SymbolMembers.end())
          continue;
        for (uint64_t MemberID : It->second) {
          PluginLinkArchiveMember *Member =
              Impl->Graph->findArchiveMember(MemberID);
          if (!Member || Member->Materialized)
            continue;
          const std::string Reason =
              (Twine("undefined symbol ") + Symbol).str();
          if (Error E =
                  Impl->materializeArchiveMember(MemberID, Reason))
            return std::move(E);
          ++Materialized;
          Changed = true;
        }
      }
    }
  } while (Changed);
  return Materialized;
}

uint64_t LinkInputSet::materializedBytes() const {
  return Impl->MaterializedBytes;
}

LinkInputReader::LinkInputReader(PluginTaskContext &TaskValue,
                                 vfs::FileSystem &FileSystemValue,
                                 const ObjectReaderProvider &ObjectsValue,
                                 LinkInputReaderOptions OptionsValue,
                                 const LinkerScriptProvider *ScriptsValue)
    : Task(TaskValue), FileSystem(FileSystemValue), Objects(ObjectsValue),
      Options(OptionsValue),
      Scripts(ScriptsValue ? ScriptsValue : &builtinLinkerScriptProvider()) {}

Expected<std::unique_ptr<LinkInputSet>>
LinkInputReader::read(const LinkRequest &Request) const {
  auto Impl = std::make_unique<LinkInputSetImpl>(
      Task, FileSystem, Objects, Request.ownedTarget(),
      Request.inputFormat(), Options, *Scripts);
  Impl->Blobs.reserve(Request.inputs().size());

  for (const OwnedRawLinkInput &Requested : Request.inputs()) {
    if (Error E = checkCancellation(Task))
      return std::move(E);

    auto Buffer = freezeInput(FileSystem, Requested);
    if (!Buffer)
      return Buffer.takeError();
    StringRef Bytes = (*Buffer)->getBuffer();
    auto Kind = classifyInput(Requested, Bytes);
    if (!Kind)
      return Kind.takeError();

    auto Blob = std::make_unique<LinkInputBlob>();
    Blob->Flags = Requested.Flags;
    Blob->Ordinal = Requested.Ordinal;
    Blob->LogicalURI = Requested.LogicalURI;
    Blob->Kind = *Kind;
    Blob->Digest = SHA256::hash(ArrayRef<uint8_t>(
        reinterpret_cast<const uint8_t *>(Bytes.data()), Bytes.size()));
    Blob->Buffer = std::move(*Buffer);

    PluginLinkInput Input;
    Input.Kind = *Kind;
    Input.Flags = Requested.Flags;
    Input.Ordinal = Requested.Ordinal;
    Input.LogicalURI = Requested.LogicalURI;
    Input.ContentDigest = Blob->Digest;
    PluginLinkInput &Stored = Impl->Graph->addInput(std::move(Input));
    Blob->InputID = Stored.ID;
    LinkInputBlob *BlobValue = Blob.get();
    Impl->Blobs.push_back(std::move(Blob));

    switch (*Kind) {
    case NEVERC_LINK_INPUT_OBJECT: {
      auto Match = Objects.registry().probe(
          Task,
          ArrayRef<uint8_t>(
              reinterpret_cast<const uint8_t *>(Bytes.data()), Bytes.size()),
          Requested.LogicalURI, Request.target(), Request.inputFormat());
      if (!Match)
        return Match.takeError();
      if (!Match->Format)
        return inputError(Requested.LogicalURI,
                          "object probe selected no reader route");
      Stored.ReaderRoute =
          Match->Format->PluginID + ":" + Match->Format->CanonicalName;
      auto Object = Objects.read(
          Task,
          ArrayRef<uint8_t>(
              reinterpret_cast<const uint8_t *>(Bytes.data()), Bytes.size()),
          Requested.LogicalURI, Request.ownedTarget(),
          Request.inputFormat());
      if (!Object)
        return Object.takeError();
      auto Handle = Impl->addObject(
          Stored.ID, std::move(*Object),
          ArrayRef<uint8_t>(
              reinterpret_cast<const uint8_t *>(Bytes.data()), Bytes.size()));
      if (!Handle)
        return Handle.takeError();
      Stored.ObjectGraph = *Handle;
      break;
    }
    case NEVERC_LINK_INPUT_ARCHIVE:
      if (Error E = readArchiveInput(*Impl, *BlobValue, Stored))
        return std::move(E);
      break;
    case NEVERC_LINK_INPUT_SHARED_LIBRARY:
      if (Error E = readSharedLibraryInput(*Impl, *BlobValue, Stored))
        return std::move(E);
      break;
    case NEVERC_LINK_INPUT_BITCODE: {
      PluginLinkBitcodeModule Module;
      Module.InputID = Stored.ID;
      Module.Name = displayName(Stored.LogicalURI).str();
      Module.ContentDigest = BlobValue->Digest;
      Module.Origin.InputID = Stored.ID;
      PluginLinkBitcodeModule &StoredModule =
          Impl->Graph->addBitcodeModule(std::move(Module));
      Stored.BitcodeModuleID = StoredModule.ID;
      Stored.ReaderRoute = "llvm-bitcode";
      break;
    }
    case NEVERC_LINK_INPUT_SCRIPT:
      if (Error E = readLinkerScriptInput(*Impl, *BlobValue, Stored))
        return std::move(E);
      break;
    case NEVERC_LINK_INPUT_BLOB:
      Stored.ReaderRoute = "opaque-blob";
      break;
    default:
      return inputError(Requested.LogicalURI, "unsupported input kind");
    }
  }

  Impl->Graph->setState(NEVERC_LINK_STATE_INPUTS_READ);
  auto Result =
      std::unique_ptr<LinkInputSet>(new LinkInputSet(std::move(Impl)));
  if (Error E = Result->materializeWholeArchives())
    return std::move(E);
  if (Error E = verifyPluginLinkGraph(Result->graph()))
    return std::move(E);
  return Result;
}

} // namespace neverc::plugin
