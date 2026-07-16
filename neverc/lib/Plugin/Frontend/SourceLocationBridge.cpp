#include "neverc/Plugin/Host/FrontendPluginBridge.h"
#include "neverc/Foundation/Core/FileManager.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "neverc/Scan/SourceScanner.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <new>

using namespace llvm;

namespace neverc::plugin {
namespace {

struct SourceLocationPayload {
  SourceManager *Manager = nullptr;
  SourceLocation Location;
};

struct SourceRangePayload {
  SourceManager *Manager = nullptr;
  CharSourceRange Range;
};

struct SourceFilePayload {
  SourceManager *Manager = nullptr;
  FileID File;
};

NevercStatus bridgeStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

bool sameHandle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

NevercStringView stringView(StringRef Value) {
  return {Value.data(), static_cast<uint64_t>(Value.size())};
}

NevercFileCharacteristic
fileCharacteristic(SrcMgr::CharacteristicKind Characteristic) {
  switch (Characteristic) {
  case SrcMgr::C_User:
    return NEVERC_FILE_CHARACTERISTIC_USER;
  case SrcMgr::C_System:
    return NEVERC_FILE_CHARACTERISTIC_SYSTEM;
  case SrcMgr::C_ExternCSystem:
    return NEVERC_FILE_CHARACTERISTIC_EXTERN_C_SYSTEM;
  }
  return NEVERC_FILE_CHARACTERISTIC_USER;
}

template <typename T>
NevercStatus writeCallerBuffer(T *OutValue, const T &Value) {
  if (!OutValue)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  const uint32_t Capacity = OutValue->Header.StructSize;
  if (Capacity < sizeof(NevercABITableHeader))
    return bridgeStatus(NEVERC_STATUS_ABI_MISMATCH);
  const size_t Writable = std::min<size_t>(Capacity, sizeof(Value));
  std::memset(OutValue, 0, Writable);
  std::memcpy(OutValue, &Value, Writable);
  if (Capacity < sizeof(Value))
    return bridgeStatus(NEVERC_STATUS_ABI_MISMATCH);
  return neverc_status_ok();
}

} // namespace

FrontendPluginBridge::FrontendPluginBridge(PluginTaskContext &TaskValue,
                                           SourceManager &SourceMgrValue)
    : Task(TaskValue), SourceMgr(SourceMgrValue) {
  SourceLocationAPI.Header = {sizeof(SourceLocationAPI),
                              NEVERC_SOURCE_LOCATION_API_MAJOR,
                              NEVERC_SOURCE_LOCATION_API_MINOR, 0};
  SourceLocationAPI.Context = this;
  SourceLocationAPI.GetLocationInfo = getLocationInfo;
  SourceLocationAPI.GetSpellingLocation = getSpellingLocation;
  SourceLocationAPI.GetExpansionLocation = getExpansionLocation;
  SourceLocationAPI.GetFileLocation = getFileLocation;
  SourceLocationAPI.GetRangeInfo = getRangeInfo;
  SourceLocationAPI.GetSourceText = getSourceText;
  SourceLocationAPI.GetPresumedLocation = getPresumedLocation;
  SourceLocationAPI.GetLocationFile = getLocationFile;
  SourceLocationAPI.GetIncludeLocation = getIncludeLocation;
  SourceLocationAPI.GetFileInfo = getFileInfo;
  SourceLocationAPI.GetCharacterData = getCharacterData;
  SourceLocationAPI.GetTokenEnd = getTokenEnd;
  SourceLocationAPI.GetLocationInfoBatch = getLocationInfoBatch;
}

FrontendPluginBridge::FrontendPluginBridge(PluginTaskContext &TaskValue,
                                           SourceManager &SourceMgrValue,
                                           const LangOptions &LangOpts)
    : FrontendPluginBridge(TaskValue, SourceMgrValue) {
  LanguageOptions = &LangOpts;
}

Expected<NevercSourceLocation>
FrontendPluginBridge::createLocation(SourceLocation Location) {
  if (Task.isEnded())
    return createStringError(inconvertibleErrorCode(),
                             "cannot create a source location after TaskEnd");
  if (Location.isInvalid())
    return createStringError(inconvertibleErrorCode(),
                             "cannot create an invalid source location");
  auto *Payload =
      new (std::nothrow) SourceLocationPayload{&SourceMgr, Location};
  if (!Payload)
    return createStringError(inconvertibleErrorCode(),
                             "cannot allocate source location payload");
  auto Handle = Task.handles().create(
      PluginSourceLocationHandleKind, Payload,
      [](void *Value) { delete static_cast<SourceLocationPayload *>(Value); });
  if (!Handle) {
    delete Payload;
    return Handle.takeError();
  }
  return *Handle;
}

Expected<NevercSourceRange>
FrontendPluginBridge::createRange(CharSourceRange Range) {
  if (Task.isEnded())
    return createStringError(inconvertibleErrorCode(),
                             "cannot create a source range after TaskEnd");
  if (Range.isInvalid())
    return createStringError(inconvertibleErrorCode(),
                             "cannot create an invalid source range");
  auto *Payload = new (std::nothrow) SourceRangePayload{&SourceMgr, Range};
  if (!Payload)
    return createStringError(inconvertibleErrorCode(),
                             "cannot allocate source range payload");
  auto Handle = Task.handles().create(
      PluginSourceRangeHandleKind, Payload,
      [](void *Value) { delete static_cast<SourceRangePayload *>(Value); });
  if (!Handle) {
    delete Payload;
    return Handle.takeError();
  }
  return *Handle;
}

Expected<NevercFileHandle>
FrontendPluginBridge::createFile(FileID File) {
  if (Task.isEnded())
    return createStringError(inconvertibleErrorCode(),
                             "cannot create a source file after TaskEnd");
  if (File.isInvalid() || !SourceMgr.getFileEntryRefForID(File))
    return createStringError(inconvertibleErrorCode(),
                             "source location has no physical file");
  auto *Payload = new (std::nothrow) SourceFilePayload{&SourceMgr, File};
  if (!Payload)
    return createStringError(inconvertibleErrorCode(),
                             "cannot allocate source file payload");
  auto Handle = Task.handles().create(
      PluginSourceFileHandleKind, Payload,
      [](void *Value) { delete static_cast<SourceFilePayload *>(Value); });
  if (!Handle) {
    delete Payload;
    return Handle.takeError();
  }
  return *Handle;
}

NevercStatus FrontendPluginBridge::resolveLocation(
    NevercTaskHandle TaskHandle, NevercSourceLocation Location,
    SourceLocation *OutLocation) {
  if (!OutLocation)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutLocation = {};
  if (!sameHandle(TaskHandle, Task.handle()))
    return bridgeStatus(NEVERC_STATUS_WRONG_SCOPE);
  if (Task.isEnded())
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  void *RawPayload = nullptr;
  NevercStatus Status = Task.handles().resolve(
      Location, PluginSourceLocationHandleKind, &RawPayload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto &Payload = *static_cast<SourceLocationPayload *>(RawPayload);
  if (Payload.Manager != &SourceMgr)
    return bridgeStatus(NEVERC_STATUS_WRONG_SCOPE);
  *OutLocation = Payload.Location;
  return neverc_status_ok();
}

NevercStatus FrontendPluginBridge::resolveRange(
    NevercTaskHandle TaskHandle, NevercSourceRange Range,
    CharSourceRange *OutRange) {
  if (!OutRange)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutRange = {};
  if (!sameHandle(TaskHandle, Task.handle()))
    return bridgeStatus(NEVERC_STATUS_WRONG_SCOPE);
  if (Task.isEnded())
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  void *RawPayload = nullptr;
  NevercStatus Status = Task.handles().resolve(
      Range, PluginSourceRangeHandleKind, &RawPayload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto &Payload = *static_cast<SourceRangePayload *>(RawPayload);
  if (Payload.Manager != &SourceMgr)
    return bridgeStatus(NEVERC_STATUS_WRONG_SCOPE);
  *OutRange = Payload.Range;
  return neverc_status_ok();
}

NevercStatus
FrontendPluginBridge::publishLocation(SourceLocation Location,
                                      NevercSourceLocation *OutLocation) {
  if (!OutLocation)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutLocation = {};
  auto Published = createLocation(Location);
  if (!Published) {
    consumeError(Published.takeError());
    return bridgeStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutLocation = *Published;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL FrontendPluginBridge::getLocationInfo(
    void *Context, NevercTaskHandle TaskHandle,
    NevercSourceLocation Location, NevercSourceLocationInfo *OutInfo) {
  if (!Context || !OutInfo)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<FrontendPluginBridge *>(Context);
  SourceLocation NativeLocation;
  NevercStatus Status =
      Bridge.resolveLocation(TaskHandle, Location, &NativeLocation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  SourceLocation FileLocation =
      Bridge.SourceMgr.getFileLoc(NativeLocation);
  if (FileLocation.isInvalid())
    return bridgeStatus(NEVERC_STATUS_VERIFICATION_FAILED);
  bool InvalidLine = false;
  bool InvalidColumn = false;
  uint32_t Line =
      Bridge.SourceMgr.getPresumedLineNumber(NativeLocation, &InvalidLine);
  uint32_t Column =
      Bridge.SourceMgr.getPresumedColumnNumber(NativeLocation, &InvalidColumn);
  if (InvalidLine || InvalidColumn)
    return bridgeStatus(NEVERC_STATUS_VERIFICATION_FAILED);

  NevercSourceLocationInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_SOURCE_LOCATION_API_MAJOR,
                 NEVERC_SOURCE_LOCATION_API_MINOR, 0};
  Info.Kind = NativeLocation.isMacroID() ? NEVERC_SOURCE_LOCATION_MACRO
                                        : NEVERC_SOURCE_LOCATION_FILE;
  Info.FileOffset = Bridge.SourceMgr.getFileOffset(FileLocation);
  Info.Line = Line;
  Info.Column = Column;
  return writeCallerBuffer(OutInfo, Info);
}

NevercStatus NEVERC_CALL FrontendPluginBridge::getSpellingLocation(
    void *Context, NevercTaskHandle TaskHandle,
    NevercSourceLocation Location, NevercSourceLocation *OutLocation) {
  if (!Context || !OutLocation)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<FrontendPluginBridge *>(Context);
  SourceLocation NativeLocation;
  NevercStatus Status =
      Bridge.resolveLocation(TaskHandle, Location, &NativeLocation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return Bridge.publishLocation(
      Bridge.SourceMgr.getSpellingLoc(NativeLocation), OutLocation);
}

NevercStatus NEVERC_CALL FrontendPluginBridge::getExpansionLocation(
    void *Context, NevercTaskHandle TaskHandle,
    NevercSourceLocation Location, NevercSourceLocation *OutLocation) {
  if (!Context || !OutLocation)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<FrontendPluginBridge *>(Context);
  SourceLocation NativeLocation;
  NevercStatus Status =
      Bridge.resolveLocation(TaskHandle, Location, &NativeLocation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return Bridge.publishLocation(
      Bridge.SourceMgr.getExpansionLoc(NativeLocation), OutLocation);
}

NevercStatus NEVERC_CALL FrontendPluginBridge::getFileLocation(
    void *Context, NevercTaskHandle TaskHandle,
    NevercSourceLocation Location, NevercSourceLocation *OutLocation) {
  if (!Context || !OutLocation)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<FrontendPluginBridge *>(Context);
  SourceLocation NativeLocation;
  NevercStatus Status =
      Bridge.resolveLocation(TaskHandle, Location, &NativeLocation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return Bridge.publishLocation(
      Bridge.SourceMgr.getFileLoc(NativeLocation), OutLocation);
}

NevercStatus NEVERC_CALL FrontendPluginBridge::getRangeInfo(
    void *Context, NevercTaskHandle TaskHandle, NevercSourceRange Range,
    NevercSourceRangeInfo *OutInfo) {
  if (!Context || !OutInfo)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<FrontendPluginBridge *>(Context);
  CharSourceRange NativeRange;
  NevercStatus Status =
      Bridge.resolveRange(TaskHandle, Range, &NativeRange);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  NevercSourceRangeInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_SOURCE_LOCATION_API_MAJOR,
                 NEVERC_SOURCE_LOCATION_API_MINOR, 0};
  Status = Bridge.publishLocation(NativeRange.getBegin(), &Info.Begin);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = Bridge.publishLocation(NativeRange.getEnd(), &Info.End);
  if (Status.Code != NEVERC_STATUS_OK) {
    (void)Bridge.Task.handles().release(
        Info.Begin, PluginSourceLocationHandleKind);
    return Status;
  }
  Info.Kind = NativeRange.isTokenRange() ? NEVERC_SOURCE_RANGE_TOKEN
                                        : NEVERC_SOURCE_RANGE_CHARACTER;
  return writeCallerBuffer(OutInfo, Info);
}

NevercStatus NEVERC_CALL FrontendPluginBridge::getSourceText(
    void *Context, NevercTaskHandle TaskHandle, NevercSourceRange Range,
    NevercBufferView *OutText) {
  if (!Context || !OutText)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<FrontendPluginBridge *>(Context);
  if (!Bridge.LanguageOptions)
    return bridgeStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  CharSourceRange NativeRange;
  NevercStatus Status =
      Bridge.resolveRange(TaskHandle, Range, &NativeRange);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  const auto Begin =
      Bridge.SourceMgr.getDecomposedSpellingLoc(NativeRange.getBegin());
  const auto End =
      Bridge.SourceMgr.getDecomposedSpellingLoc(NativeRange.getEnd());
  if (Begin.first.isInvalid() || End.first.isInvalid() ||
      Begin.first != End.first)
    return bridgeStatus(NEVERC_STATUS_VERIFICATION_FAILED);
  bool Invalid = false;
  StringRef Text = SourceScanner::getSourceText(
      NativeRange, Bridge.SourceMgr, *Bridge.LanguageOptions, &Invalid);
  if (Invalid)
    return bridgeStatus(NEVERC_STATUS_VERIFICATION_FAILED);

  NevercBufferView View{};
  View.Header = {sizeof(View), NEVERC_SOURCE_LOCATION_API_MAJOR,
                 NEVERC_SOURCE_LOCATION_API_MINOR, 0};
  View.Data = reinterpret_cast<const uint8_t *>(Text.data());
  View.Length = Text.size();
  View.NullTerminated = NEVERC_FALSE;
  return writeCallerBuffer(OutText, View);
}

NevercStatus NEVERC_CALL FrontendPluginBridge::getPresumedLocation(
    void *Context, NevercTaskHandle TaskHandle,
    NevercSourceLocation Location, NevercPresumedLocation *OutLocation) {
  if (!Context || !OutLocation)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<FrontendPluginBridge *>(Context);
  SourceLocation NativeLocation;
  NevercStatus Status =
      Bridge.resolveLocation(TaskHandle, Location, &NativeLocation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  PresumedLoc NativePresumed =
      Bridge.SourceMgr.getPresumedLoc(NativeLocation);
  if (NativePresumed.isInvalid())
    return bridgeStatus(NEVERC_STATUS_VERIFICATION_FAILED);

  NevercPresumedLocation Presumed{};
  Presumed.Header = {sizeof(Presumed), NEVERC_SOURCE_LOCATION_API_MAJOR,
                     NEVERC_SOURCE_LOCATION_API_MINOR, 0};
  Presumed.Filename =
      stringView(StringRef(NativePresumed.getFilename()));
  Presumed.Line = NativePresumed.getLine();
  Presumed.Column = NativePresumed.getColumn();
  if (NativePresumed.getIncludeLoc().isValid()) {
    Status = Bridge.publishLocation(NativePresumed.getIncludeLoc(),
                                    &Presumed.IncludeLocation);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
  }
  Status = writeCallerBuffer(OutLocation, Presumed);
  if (Status.Code != NEVERC_STATUS_OK &&
      !neverc_handle_is_null(Presumed.IncludeLocation))
    (void)Bridge.Task.handles().release(
        Presumed.IncludeLocation, PluginSourceLocationHandleKind);
  return Status;
}

NevercStatus NEVERC_CALL FrontendPluginBridge::getLocationFile(
    void *Context, NevercTaskHandle TaskHandle,
    NevercSourceLocation Location, NevercFileHandle *OutFile) {
  if (!Context || !OutFile)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutFile = {};
  auto &Bridge = *static_cast<FrontendPluginBridge *>(Context);
  SourceLocation NativeLocation;
  NevercStatus Status =
      Bridge.resolveLocation(TaskHandle, Location, &NativeLocation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  SourceLocation FileLocation =
      Bridge.SourceMgr.getFileLoc(NativeLocation);
  if (FileLocation.isInvalid())
    return bridgeStatus(NEVERC_STATUS_VERIFICATION_FAILED);
  auto File = Bridge.createFile(Bridge.SourceMgr.getFileID(FileLocation));
  if (!File) {
    consumeError(File.takeError());
    return bridgeStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  }
  *OutFile = *File;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL FrontendPluginBridge::getIncludeLocation(
    void *Context, NevercTaskHandle TaskHandle,
    NevercSourceLocation Location, NevercSourceLocation *OutLocation) {
  if (!Context || !OutLocation)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutLocation = {};
  auto &Bridge = *static_cast<FrontendPluginBridge *>(Context);
  SourceLocation NativeLocation;
  NevercStatus Status =
      Bridge.resolveLocation(TaskHandle, Location, &NativeLocation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PresumedLoc Presumed = Bridge.SourceMgr.getPresumedLoc(NativeLocation);
  if (Presumed.isInvalid())
    return bridgeStatus(NEVERC_STATUS_VERIFICATION_FAILED);
  if (Presumed.getIncludeLoc().isInvalid())
    return neverc_status_ok();
  return Bridge.publishLocation(Presumed.getIncludeLoc(), OutLocation);
}

NevercStatus NEVERC_CALL FrontendPluginBridge::getFileInfo(
    void *Context, NevercTaskHandle TaskHandle, NevercFileHandle File,
    NevercFileInfo *OutInfo) {
  if (!Context || !OutInfo)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<FrontendPluginBridge *>(Context);
  if (!sameHandle(TaskHandle, Bridge.Task.handle()))
    return bridgeStatus(NEVERC_STATUS_WRONG_SCOPE);
  if (Bridge.Task.isEnded())
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);

  void *RawPayload = nullptr;
  NevercStatus Status = Bridge.Task.handles().resolve(
      File, PluginSourceFileHandleKind, &RawPayload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto &Payload = *static_cast<SourceFilePayload *>(RawPayload);
  if (Payload.Manager != &Bridge.SourceMgr)
    return bridgeStatus(NEVERC_STATUS_WRONG_SCOPE);
  OptionalFileEntryRef FileEntry =
      Bridge.SourceMgr.getFileEntryRefForID(Payload.File);
  if (!FileEntry)
    return bridgeStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  if (FileEntry->getSize() < 0)
    return bridgeStatus(NEVERC_STATUS_VERIFICATION_FAILED);

  StringRef Canonical =
      Bridge.SourceMgr.getFileManager().getCanonicalName(*FileEntry);
  const sys::fs::UniqueID &UniqueID = FileEntry->getUniqueID();
  NevercFileInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_SOURCE_LOCATION_API_MAJOR,
                 NEVERC_SOURCE_LOCATION_API_MINOR, 0};
  Info.Path = stringView(FileEntry->getName());
  Info.CanonicalPath = stringView(Canonical);
  Info.Size = static_cast<uint64_t>(FileEntry->getSize());
  Info.ModificationTime =
      static_cast<int64_t>(FileEntry->getModificationTime());
  Info.UniqueID = {UniqueID.getDevice(), UniqueID.getFile()};
  Info.Characteristic = fileCharacteristic(
      Bridge.SourceMgr.getFileCharacteristic(
          Bridge.SourceMgr.getLocForStartOfFile(Payload.File)));
  Info.NamedPipe =
      FileEntry->isNamedPipe() ? NEVERC_TRUE : NEVERC_FALSE;
  return writeCallerBuffer(OutInfo, Info);
}

NevercStatus NEVERC_CALL FrontendPluginBridge::getCharacterData(
    void *Context, NevercTaskHandle TaskHandle,
    NevercSourceLocation Location, NevercBufferView *OutData) {
  if (!Context || !OutData)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<FrontendPluginBridge *>(Context);
  SourceLocation NativeLocation;
  NevercStatus Status =
      Bridge.resolveLocation(TaskHandle, Location, &NativeLocation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  const auto Decomposed =
      Bridge.SourceMgr.getDecomposedSpellingLoc(NativeLocation);
  if (Decomposed.first.isInvalid())
    return bridgeStatus(NEVERC_STATUS_VERIFICATION_FAILED);
  std::optional<StringRef> Buffer =
      Bridge.SourceMgr.getBufferDataOrNone(Decomposed.first);
  if (!Buffer || Decomposed.second > Buffer->size())
    return bridgeStatus(NEVERC_STATUS_VERIFICATION_FAILED);

  StringRef Remaining = Buffer->drop_front(Decomposed.second);
  NevercBufferView View{};
  View.Header = {sizeof(View), NEVERC_SOURCE_LOCATION_API_MAJOR,
                 NEVERC_SOURCE_LOCATION_API_MINOR, 0};
  View.Data = reinterpret_cast<const uint8_t *>(Remaining.data());
  View.Length = Remaining.size();
  View.NullTerminated = NEVERC_TRUE;
  return writeCallerBuffer(OutData, View);
}

NevercStatus NEVERC_CALL FrontendPluginBridge::getTokenEnd(
    void *Context, NevercTaskHandle TaskHandle,
    NevercSourceLocation Location, NevercSourceLocation *OutLocation) {
  if (!Context || !OutLocation)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutLocation = {};
  auto &Bridge = *static_cast<FrontendPluginBridge *>(Context);
  if (!Bridge.LanguageOptions)
    return bridgeStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  SourceLocation NativeLocation;
  NevercStatus Status =
      Bridge.resolveLocation(TaskHandle, Location, &NativeLocation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  SourceLocation End = SourceScanner::getLocForEndOfToken(
      NativeLocation, 0, Bridge.SourceMgr, *Bridge.LanguageOptions);
  if (End.isInvalid())
    return bridgeStatus(NEVERC_STATUS_VERIFICATION_FAILED);
  return Bridge.publishLocation(End, OutLocation);
}

NevercStatus NEVERC_CALL FrontendPluginBridge::getLocationInfoBatch(
    void *Context, NevercTaskHandle TaskHandle,
    const NevercSourceLocation *Locations, uint64_t LocationCount,
    NevercSourceLocationInfo *OutInfos, uint64_t OutInfoCapacity) {
  if (!Context || LocationCount > OutInfoCapacity)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  if (LocationCount == 0)
    return neverc_status_ok();
  if (!Locations || !OutInfos ||
      LocationCount > std::numeric_limits<size_t>::max())
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);

  const size_t Count = static_cast<size_t>(LocationCount);
  for (size_t Index = 0; Index != Count; ++Index) {
    if (OutInfos[Index].Header.StructSize <
        sizeof(NevercSourceLocationInfo))
      return bridgeStatus(NEVERC_STATUS_ABI_MISMATCH);
  }
  for (size_t Index = 0; Index != Count; ++Index) {
    NevercStatus Status = getLocationInfo(
        Context, TaskHandle, Locations[Index], &OutInfos[Index]);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
  }
  return neverc_status_ok();
}

} // namespace neverc::plugin
