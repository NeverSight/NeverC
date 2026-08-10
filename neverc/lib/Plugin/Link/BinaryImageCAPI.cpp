#include "BinaryImage.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include <algorithm>
#include <cstring>
#include <mutex>

namespace neverc::plugin {
namespace {

NevercStatus imageStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

bool sameHandle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

class ImageLease {
public:
  ImageLease() = default;
  ImageLease(std::shared_ptr<detail::BinaryImageAPIControl> ControlValue,
             std::unique_lock<std::recursive_mutex> LockValue,
             PluginBinaryImage *OwnerValue)
      : Control(std::move(ControlValue)), Lock(std::move(LockValue)),
        Owner(OwnerValue) {}
  explicit operator bool() const { return Owner != nullptr; }
  PluginBinaryImage &operator*() const { return *Owner; }
  PluginBinaryImage *operator->() const { return Owner; }

private:
  std::shared_ptr<detail::BinaryImageAPIControl> Control;
  std::unique_lock<std::recursive_mutex> Lock;
  PluginBinaryImage *Owner = nullptr;
};

ImageLease acquire(detail::BinaryImageAPIFacade &Facade,
                   NevercTaskHandle Task,
                   NevercBinaryImageHandle Handle,
                   NevercStatus &Status) {
  if (!sameHandle(Task, Facade.TaskHandle)) {
    Status = imageStatus(NEVERC_STATUS_WRONG_SCOPE);
    return {};
  }
  auto Control = Facade.Control;
  std::unique_lock<std::recursive_mutex> Lock(Control->Mutex);
  PluginBinaryImage *Owner = Control->Owner;
  if (!Owner) {
    Status = imageStatus(NEVERC_STATUS_STALE_HANDLE);
    return {};
  }
  void *Payload = nullptr;
  Status = Facade.Task->handles().resolve(
      Handle, PluginBinaryImageHandleKind, &Payload);
  if (!neverc_status_is_ok(Status))
    return {};
  if (Payload != Owner || !sameHandle(Handle, Owner->handle())) {
    Status = imageStatus(NEVERC_STATUS_STALE_HANDLE);
    return {};
  }
  Status = neverc_status_ok();
  return ImageLease(std::move(Control), std::move(Lock), Owner);
}

template <typename T>
NevercStatus writeRecord(T *OutValue, const T &Value) {
  if (!OutValue)
    return imageStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  const uint32_t Capacity = OutValue->Header.StructSize;
  if (Capacity < sizeof(NevercABITableHeader))
    return imageStatus(NEVERC_STATUS_ABI_MISMATCH);
  const size_t Writable = std::min<size_t>(Capacity, sizeof(Value));
  std::memset(OutValue, 0, Writable);
  std::memcpy(OutValue, &Value, Writable);
  return Capacity < sizeof(Value)
             ? imageStatus(NEVERC_STATUS_ABI_MISMATCH)
             : neverc_status_ok();
}

NevercStatus NEVERC_CALL getImageInfo(
    void *Context, NevercTaskHandle Task,
    NevercBinaryImageHandle Handle, NevercBinaryImageInfo *OutInfo) {
  if (!Context)
    return imageStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Facade = *static_cast<detail::BinaryImageAPIFacade *>(Context);
  NevercStatus Status;
  ImageLease Image = acquire(Facade, Task, Handle, Status);
  if (!Image)
    return Status;
  NevercBinaryImageInfo Value{};
  Value.Header = {sizeof(Value), NEVERC_LINK_API_MAJOR,
                  NEVERC_LINK_API_MINOR, 0};
  Value.Image = Image->handle();
  Value.State = Image->state();
  Value.OutputKind = Image->outputKind();
  Value.TargetID = Image->targetID();
  Value.FormatID = Image->formatID();
  Value.EntryAddress = Image->entryAddress();
  Value.ImageBase = Image->imageBase();
  Value.Size = Image->bytes().size();
  Value.SegmentCount = Image->segments().size();
  Value.SectionCount = Image->sections().size();
  Value.ImportCount = Image->importCount();
  Value.ExportCount = Image->exportCount();
  Value.DynamicRelocationCount = Image->dynamicRelocationCount();
  const auto Digest = Image->contentDigest();
  std::copy(Digest.begin(), Digest.end(), Value.ContentDigest);
  switch (Facade.Access) {
  case detail::BinaryImageAPIAccess::Unrestricted:
    Value.Binary = &Image->binaryAPI();
    break;
  case detail::BinaryImageAPIAccess::ReadOnly:
    Value.Binary = &Image->readOnlyBinaryAPI();
    break;
  case detail::BinaryImageAPIAccess::Capability:
    Value.Binary =
        &Image->capabilityBinaryAPI(Facade.MutationDomain, Facade.Token);
    break;
  }
  Value.Builder = Image->builderHandle();
  return writeRecord(OutInfo, Value);
}

NevercStatus validatePage(NevercLinkEntityPage *Page,
                          uint64_t RequiredStride) {
  if (!Page || Page->Header.StructSize < sizeof(*Page) ||
      (Page->ElementCapacity != 0 && !Page->Data) ||
      Page->ElementStride < RequiredStride)
    return imageStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Page->OutCount = 0;
  Page->NextCursor = 0;
  Page->HasMore = NEVERC_FALSE;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL getSegmentPage(
    void *Context, NevercTaskHandle Task,
    NevercBinaryImageHandle Handle, uint64_t Cursor,
    NevercLinkEntityPage *Page) {
  if (!Context)
    return imageStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Facade = *static_cast<detail::BinaryImageAPIFacade *>(Context);
  NevercStatus Status;
  ImageLease Image = acquire(Facade, Task, Handle, Status);
  if (!Image)
    return Status;
  Status = validatePage(Page, sizeof(NevercBinarySegmentInfo));
  if (!neverc_status_is_ok(Status))
    return Status;
  if (Cursor > Image->segments().size())
    return imageStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto *Bytes = static_cast<uint8_t *>(Page->Data);
  uint64_t Index = Cursor;
  while (Index != Image->segments().size() &&
         Page->OutCount != Page->ElementCapacity) {
    const PluginBinarySegment &Segment = Image->segments()[Index];
    NevercBinarySegmentInfo Value{};
    Value.Header = {sizeof(Value), NEVERC_LINK_API_MAJOR,
                    NEVERC_LINK_API_MINOR, 0};
    Value.Segment = Segment.Handle;
    Value.Name = {Segment.Name.data(), Segment.Name.size()};
    Value.Flags = Segment.Flags;
    Value.Address = Segment.Address;
    Value.MemorySize = Segment.MemorySize;
    Value.FileOffset = Segment.FileOffset;
    Value.FileSize = Segment.FileSize;
    Value.Alignment = Segment.Alignment;
    std::memcpy(Bytes + Page->OutCount * Page->ElementStride,
                &Value, sizeof(Value));
    ++Page->OutCount;
    ++Index;
  }
  Page->NextCursor = Index;
  Page->HasMore =
      Index != Image->segments().size() ? NEVERC_TRUE : NEVERC_FALSE;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL getSectionPage(
    void *Context, NevercTaskHandle Task,
    NevercBinaryImageHandle Handle, uint64_t Cursor,
    NevercLinkEntityPage *Page) {
  if (!Context)
    return imageStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Facade = *static_cast<detail::BinaryImageAPIFacade *>(Context);
  NevercStatus Status;
  ImageLease Image = acquire(Facade, Task, Handle, Status);
  if (!Image)
    return Status;
  Status = validatePage(Page, sizeof(NevercBinarySectionInfo));
  if (!neverc_status_is_ok(Status))
    return Status;
  if (Cursor > Image->sections().size())
    return imageStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto *Bytes = static_cast<uint8_t *>(Page->Data);
  uint64_t Index = Cursor;
  while (Index != Image->sections().size() &&
         Page->OutCount != Page->ElementCapacity) {
    const PluginBinarySection &Section = Image->sections()[Index];
    NevercBinarySectionInfo Value{};
    Value.Header = {sizeof(Value), NEVERC_LINK_API_MAJOR,
                    NEVERC_LINK_API_MINOR, 0};
    Value.Section = Section.Handle;
    Value.Segment = Section.Segment;
    Value.Name = {Section.Name.data(), Section.Name.size()};
    Value.Kind = Section.Kind;
    Value.Flags = Section.Flags;
    Value.Address = Section.Address;
    Value.MemorySize = Section.MemorySize;
    Value.FileOffset = Section.FileOffset;
    Value.FileSize = Section.FileSize;
    Value.Alignment = Section.Alignment;
    std::memcpy(Bytes + Page->OutCount * Page->ElementStride,
                &Value, sizeof(Value));
    ++Page->OutCount;
    ++Index;
  }
  Page->NextCursor = Index;
  Page->HasMore =
      Index != Image->sections().size() ? NEVERC_TRUE : NEVERC_FALSE;
  return neverc_status_ok();
}

} // namespace

void initializeBinaryImageAPI(detail::BinaryImageAPIFacade &Facade) {
  Facade.API = {};
  Facade.API.Header = {sizeof(Facade.API), NEVERC_LINK_API_MAJOR,
                       NEVERC_LINK_API_MINOR, 0};
  Facade.API.Context = &Facade;
  Facade.API.GetBinaryImageInfo = getImageInfo;
  Facade.API.GetBinarySegmentPage = getSegmentPage;
  Facade.API.GetBinarySectionPage = getSectionPage;
}

} // namespace neverc::plugin
