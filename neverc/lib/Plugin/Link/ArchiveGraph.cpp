#include "LinkInputReaderInternal.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SHA256.h"
#include <algorithm>
#include <limits>

using namespace llvm;
using namespace llvm::object;

namespace neverc::plugin {
namespace {

Error archiveError(const LinkInputBlob &Blob, const Twine &Message) {
  return createStringError(errc::invalid_argument,
                           "archive '" + Blob.LogicalURI + "': " + Message);
}

std::string archiveName(StringRef URI) {
  StringRef Name = sys::path::filename(URI);
  return (Name.empty() ? URI : Name).str();
}

std::array<uint8_t, 32> digest(MemoryBufferRef Buffer) {
  StringRef Bytes = Buffer.getBuffer();
  return SHA256::hash(ArrayRef<uint8_t>(
      reinterpret_cast<const uint8_t *>(Bytes.data()), Bytes.size()));
}

Error checkCancellation(PluginTaskContext &Task) {
  if (Task.checkCancelled().Code == NEVERC_STATUS_OK)
    return Error::success();
  return createStringError(
      std::make_error_code(std::errc::operation_canceled),
                           "archive materialization was cancelled");
}

Error materializeCOFFImport(LinkInputSetImpl &Set,
                            LinkArchiveState &ArchiveState,
                            PluginLinkArchiveMember &Member,
                            MemoryBufferRef Buffer) {
  StringRef Bytes = Buffer.getBuffer();
  if (Bytes.size() < sizeof(coff_import_header))
    return archiveError(*ArchiveState.Blob,
                        "truncated COFF short import member");
  const auto *Header =
      reinterpret_cast<const coff_import_header *>(Bytes.data());
  if (Header->Sig1 != 0 || Header->Sig2 != UINT16_MAX)
    return archiveError(*ArchiveState.Blob,
                        "invalid COFF short import signature");
  const uint64_t DataSize = Header->SizeOfData;
  if (DataSize > Bytes.size() - sizeof(*Header))
    return archiveError(*ArchiveState.Blob,
                        "COFF short import strings are out of range");
  StringRef Strings = Bytes.substr(sizeof(*Header), DataSize);
  const size_t SymbolEnd = Strings.find('\0');
  if (SymbolEnd == StringRef::npos)
    return archiveError(*ArchiveState.Blob,
                        "COFF short import symbol is not terminated");
  const size_t DLLEnd = Strings.find('\0', SymbolEnd + 1);
  if (DLLEnd == StringRef::npos)
    return archiveError(*ArchiveState.Blob,
                        "COFF short import DLL name is not terminated");
  const StringRef SymbolName = Strings.take_front(SymbolEnd);
  const StringRef DLLName =
      Strings.slice(SymbolEnd + 1, DLLEnd);
  if (SymbolName.empty() || DLLName.empty())
    return archiveError(*ArchiveState.Blob,
                        "COFF short import has an empty name");

  PluginLinkSharedLibrary *Library = nullptr;
  for (PluginLinkSharedLibrary &Candidate : Set.Graph->sharedLibraries())
    if (Candidate.InputID == ArchiveState.InputID &&
        Candidate.InstallName == DLLName) {
      Library = &Candidate;
      break;
    }
  if (!Library) {
    PluginLinkSharedLibrary Value;
    Value.InputID = ArchiveState.InputID;
    Value.Name = DLLName.str();
    Value.InstallName = DLLName.str();
    Value.ContentDigest = digest(Buffer);
    Value.Origin.InputID = ArchiveState.InputID;
    Library = &Set.Graph->addSharedLibrary(std::move(Value));
  }

  auto addDefinition = [&](StringRef Name) {
    if (Name.empty() ||
        llvm::any_of(Library->Exports,
                     [&](const std::string &Existing) {
                       return Existing == Name;
                     }))
      return;
    Library->Exports.push_back(Name.str());
    PluginLinkSymbol Symbol;
    Symbol.Name = Name.str();
    Symbol.Binding = NEVERC_LINK_SYMBOL_BINDING_GLOBAL;
    Symbol.Definition = NEVERC_LINK_SYMBOL_SHARED;
    Symbol.IsExported = true;
    Symbol.Origin.InputID = ArchiveState.InputID;
    Symbol.Origin.ArchiveMemberID = Member.ID;
    Set.Graph->addSymbol(std::move(Symbol));
  };
  addDefinition(SymbolName);
  if (Header->getType() == COFF::IMPORT_DATA)
    addDefinition((Twine("__imp_") + SymbolName).str());
  return Error::success();
}

} // namespace

Error readArchiveInput(LinkInputSetImpl &Set, LinkInputBlob &Blob,
                       PluginLinkInput &Input) {
  auto Parsed = Archive::create(Blob.Buffer->getMemBufferRef());
  if (!Parsed)
    return joinErrors(archiveError(Blob, "cannot parse archive"),
                      Parsed.takeError());

  LinkArchiveState State;
  State.InputID = Input.ID;
  State.Blob = &Blob;
  State.Archive = std::move(*Parsed);

  PluginLinkArchive GraphArchive;
  GraphArchive.InputID = Input.ID;
  GraphArchive.Name = archiveName(Blob.LogicalURI);
  GraphArchive.Thin = State.Archive->isThin();
  GraphArchive.Origin.InputID = Input.ID;
  PluginLinkArchive &StoredArchive =
      Set.Graph->addArchive(std::move(GraphArchive));
  State.ArchiveID = StoredArchive.ID;
  Input.ArchiveID = State.ArchiveID;
  Input.ReaderRoute =
      State.Archive->isThin() ? "llvm-thin-archive" : "llvm-archive";

  std::map<uint64_t, uint64_t> MemberIDByOffset;
  Error ChildError = Error::success();
  uint64_t Ordinal = 0;
  for (const Archive::Child &Child : State.Archive->children(ChildError)) {
    auto Name = Child.getName();
    if (!Name)
      return joinErrors(archiveError(Blob, "member has no valid name"),
                        Name.takeError());

    PluginLinkArchiveMember Member;
    Member.InputID = Input.ID;
    Member.ArchiveID = State.ArchiveID;
    Member.Name = Name->str();
    Member.Ordinal = Ordinal++;
    Member.Origin.InputID = Input.ID;

    if (!State.Archive->isThin()) {
      auto Buffer = Child.getMemoryBufferRef();
      if (!Buffer)
        return joinErrors(archiveError(Blob, "cannot read member"),
                          Buffer.takeError());
      Member.ContentDigest = digest(*Buffer);
    }

    PluginLinkArchiveMember &StoredMember =
        Set.Graph->addArchiveMember(std::move(Member));
    const uint64_t MemberID = StoredMember.ID;
    StoredMember.Origin.ArchiveMemberID = MemberID;
    MemberIDByOffset[Child.getChildOffset()] = MemberID;
    State.MemberIndexByID[MemberID] = State.Members.size();
    State.Members.emplace_back(MemberID, Child);
  }
  if (ChildError)
    return joinErrors(archiveError(Blob, "invalid member table"),
                      std::move(ChildError));

  for (const Archive::Symbol &Symbol : State.Archive->symbols()) {
    auto Child = Symbol.getMember();
    if (!Child)
      return joinErrors(archiveError(Blob, "invalid symbol index"),
                        Child.takeError());
    auto It = MemberIDByOffset.find(Child->getChildOffset());
    if (It == MemberIDByOffset.end())
      continue;
    std::vector<uint64_t> &Members = State.SymbolMembers[Symbol.getName()];
    if (!llvm::is_contained(Members, It->second))
      Members.push_back(It->second);
  }

  const size_t ArchiveIndex = Set.Archives.size();
  for (const LinkArchiveMemberState &Member : State.Members)
    Set.ArchiveIndexByMemberID[Member.MemberID] = ArchiveIndex;
  Set.Archives.push_back(std::move(State));
  return Error::success();
}

Expected<MemoryBufferRef> LinkInputSetImpl::archiveMemberBuffer(
    LinkArchiveState &ArchiveState, LinkArchiveMemberState &Member) {
  if (Error E = checkCancellation(Task))
    return std::move(E);
  if (!ArchiveState.Archive->isThin())
    return Member.Child.getMemoryBufferRef();
  if (Member.ExternalBuffer)
    return Member.ExternalBuffer->getMemBufferRef();

  auto Name = Member.Child.getName();
  if (!Name)
    return Name.takeError();
  SmallString<256> Path(*Name);
  if (!sys::path::is_absolute(Path)) {
    if (ArchiveState.Blob->LogicalURI.empty())
      return archiveError(*ArchiveState.Blob,
                          "thin member has no resolvable VFS base URI");
    SmallString<256> Base(ArchiveState.Blob->LogicalURI);
    sys::path::remove_filename(Base);
    sys::path::append(Base, Path);
    Path = Base;
  }

  ErrorOr<std::unique_ptr<MemoryBuffer>> Buffer =
      FileSystem.getBufferForFile(Path);
  if (!Buffer)
    return errorCodeToError(Buffer.getError());
  Member.ExternalBuffer = std::move(*Buffer);
  return Member.ExternalBuffer->getMemBufferRef();
}

Error LinkInputSetImpl::materializeArchiveMember(uint64_t MemberID,
                                                 StringRef Reason) {
  auto ArchiveIt = ArchiveIndexByMemberID.find(MemberID);
  if (ArchiveIt == ArchiveIndexByMemberID.end())
    return createStringError(errc::invalid_argument,
                             "unknown archive member ID " +
                                 std::to_string(MemberID));
  LinkArchiveState &ArchiveState = Archives[ArchiveIt->second];
  auto MemberIt = ArchiveState.MemberIndexByID.find(MemberID);
  if (MemberIt == ArchiveState.MemberIndexByID.end())
    return archiveError(*ArchiveState.Blob, "member index is inconsistent");
  LinkArchiveMemberState &MemberState =
      ArchiveState.Members[MemberIt->second];
  PluginLinkArchiveMember *Member = Graph->findArchiveMember(MemberID);
  if (!Member)
    return archiveError(*ArchiveState.Blob,
                        "normalized member record is missing");
  if (Member->Materialized)
    return Error::success();
  if (Reason.empty())
    return archiveError(*ArchiveState.Blob,
                        "materialization reason must not be empty");

  auto Buffer = archiveMemberBuffer(ArchiveState, MemberState);
  if (!Buffer)
    return Buffer.takeError();
  StringRef Bytes = Buffer->getBuffer();
  if (Bytes.size() > Options.MaterializationBudgetBytes ||
      MaterializedBytes >
          Options.MaterializationBudgetBytes - Bytes.size())
    return createStringError(errc::not_enough_memory,
                             "archive materialization budget exceeded");

  const file_magic Magic = identify_magic(Bytes);
  const std::string LogicalName =
      (Twine(ArchiveState.Blob->LogicalURI) + "(" + Member->Name + ")")
          .str();
  if (Magic == file_magic::coff_import_library) {
    if (Error E =
            materializeCOFFImport(*this, ArchiveState, *Member, *Buffer))
      return E;
  } else if (Magic == file_magic::bitcode) {
    PluginLinkBitcodeModule Module;
    Module.InputID = ArchiveState.InputID;
    Module.Name = LogicalName;
    Module.ContentDigest = digest(*Buffer);
    Module.Origin.InputID = ArchiveState.InputID;
    Module.Origin.ArchiveMemberID = MemberID;
    Graph->addBitcodeModule(std::move(Module));
  } else {
    auto Object = Objects.read(
        Task,
        ArrayRef<uint8_t>(
            reinterpret_cast<const uint8_t *>(Bytes.data()), Bytes.size()),
        LogicalName, Target, InputFormat);
    if (!Object)
      return joinErrors(
          archiveError(*ArchiveState.Blob,
                       "member is neither bitcode nor a readable object"),
          Object.takeError());
    auto Handle = addObject(
        MemberID, std::move(*Object),
        ArrayRef<uint8_t>(
            reinterpret_cast<const uint8_t *>(Bytes.data()), Bytes.size()));
    if (!Handle)
      return Handle.takeError();
    Member->Origin.ObjectGraph = *Handle;
  }

  Member->ContentDigest = digest(*Buffer);
  Member->Materialized = true;
  Member->MaterializationReason = Reason.str();
  MaterializedBytes += Bytes.size();
  return Error::success();
}

} // namespace neverc::plugin
