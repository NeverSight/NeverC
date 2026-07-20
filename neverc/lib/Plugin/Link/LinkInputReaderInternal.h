#ifndef NEVERC_PLUGIN_LINK_LINKINPUTREADERINTERNAL_H
#define NEVERC_PLUGIN_LINK_LINKINPUTREADERINTERNAL_H

#include "LinkInputReader.h"
#include "neverc/Plugin/Host/ObjectPluginBridge.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Object/Archive.h"
#include "llvm/Support/MemoryBuffer.h"
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace neverc::plugin {

struct LinkInputBlob {
  uint64_t InputID = 0;
  NevercLinkInputKind Kind = NEVERC_LINK_INPUT_UNKNOWN;
  NevercLinkInputFlags Flags = NEVERC_LINK_INPUT_FLAG_NONE;
  uint64_t Ordinal = 0;
  std::string LogicalURI;
  std::array<uint8_t, 32> Digest{};
  std::unique_ptr<llvm::MemoryBuffer> Buffer;
};

struct LinkObjectStorage {
  uint64_t OriginID = 0;
  std::unique_ptr<PluginObjectGraph> Graph;
  std::unique_ptr<ObjectPluginBridge> Bridge;
};

struct LinkArchiveMemberState {
  LinkArchiveMemberState(uint64_t MemberIDValue,
                         llvm::object::Archive::Child ChildValue)
      : MemberID(MemberIDValue), Child(std::move(ChildValue)) {}

  uint64_t MemberID = 0;
  llvm::object::Archive::Child Child;
  std::unique_ptr<llvm::MemoryBuffer> ExternalBuffer;
};

struct LinkArchiveState {
  uint64_t ArchiveID = 0;
  uint64_t InputID = 0;
  LinkInputBlob *Blob = nullptr;
  std::unique_ptr<llvm::object::Archive> Archive;
  std::vector<LinkArchiveMemberState> Members;
  std::map<uint64_t, size_t> MemberIndexByID;
  llvm::StringMap<std::vector<uint64_t>> SymbolMembers;
};

class LinkInputSetImpl {
public:
  LinkInputSetImpl(PluginTaskContext &TaskValue,
                   llvm::vfs::FileSystem &FileSystemValue,
                   const ObjectReaderProvider &ObjectsValue,
                   OwnedTargetKey TargetValue,
                   NevercObjectFormatID InputFormatValue,
                   LinkInputReaderOptions OptionsValue,
                   const LinkerScriptProvider &ScriptsValue);

  llvm::Expected<NevercObjectGraphHandle>
  addObject(uint64_t OriginID, std::unique_ptr<PluginObjectGraph> Object);
  llvm::Expected<llvm::MemoryBufferRef>
  archiveMemberBuffer(LinkArchiveState &Archive,
                      LinkArchiveMemberState &Member);
  llvm::Error materializeArchiveMember(uint64_t MemberID,
                                       llvm::StringRef Reason);

  PluginTaskContext &Task;
  llvm::vfs::FileSystem &FileSystem;
  const ObjectReaderProvider &Objects;
  OwnedTargetKey Target;
  NevercObjectFormatID InputFormat{};
  LinkInputReaderOptions Options;
  const LinkerScriptProvider &Scripts;
  std::unique_ptr<PluginLinkGraph> Graph;
  std::vector<std::unique_ptr<LinkInputBlob>> Blobs;
  std::vector<LinkObjectStorage> ObjectStorage;
  std::map<uint64_t, PluginObjectGraph *> ObjectsByInput;
  std::map<uint64_t, LinkerScriptResult> ScriptResults;
  std::vector<LinkArchiveState> Archives;
  std::map<uint64_t, size_t> ArchiveIndexByMemberID;
  uint64_t MaterializedBytes = 0;
};

llvm::Error readArchiveInput(LinkInputSetImpl &Set, LinkInputBlob &Blob,
                             PluginLinkInput &Input);
llvm::Error readSharedLibraryInput(LinkInputSetImpl &Set,
                                   LinkInputBlob &Blob,
                                   PluginLinkInput &Input);
llvm::Error readLinkerScriptInput(LinkInputSetImpl &Set,
                                  LinkInputBlob &Blob,
                                  PluginLinkInput &Input);

} // namespace neverc::plugin

#endif
