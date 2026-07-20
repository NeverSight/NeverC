#include "BinaryImage.h"
#include <algorithm>
#include <cstring>

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

NevercStatus validate(PluginBinaryImage *Image,
                      NevercTaskHandle Task,
                      NevercBinaryImageHandle Handle) {
  if (!Image || !sameHandle(Task, Image->taskHandle()))
    return imageStatus(NEVERC_STATUS_WRONG_SCOPE);
  if (!sameHandle(Handle, Image->handle()))
    return imageStatus(NEVERC_STATUS_STALE_HANDLE);
  return neverc_status_ok();
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
  auto *Image = static_cast<PluginBinaryImage *>(Context);
  NevercStatus Status = validate(Image, Task, Handle);
  if (!neverc_status_is_ok(Status))
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
  Value.Binary = &Image->binaryAPI();
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
  auto *Image = static_cast<PluginBinaryImage *>(Context);
  NevercStatus Status = validate(Image, Task, Handle);
  if (!neverc_status_is_ok(Status))
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
  auto *Image = static_cast<PluginBinaryImage *>(Context);
  NevercStatus Status = validate(Image, Task, Handle);
  if (!neverc_status_is_ok(Status))
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

void initializeBinaryImageAPI(NevercLinkAPI &API,
                              PluginBinaryImage &Image) {
  API = {};
  API.Header = {sizeof(API), NEVERC_LINK_API_MAJOR,
                NEVERC_LINK_API_MINOR, 0};
  API.Context = &Image;
  API.GetBinaryImageInfo = getImageInfo;
  API.GetBinarySegmentPage = getSegmentPage;
  API.GetBinarySectionPage = getSectionPage;
}

} // namespace neverc::plugin
