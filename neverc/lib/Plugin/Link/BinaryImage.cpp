#include "BinaryImage.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginPhaseExecutor.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/SHA256.h"
#include <algorithm>
#include <limits>

using namespace llvm;

namespace neverc::plugin {
namespace {

Error imageError(const Twine &Message) {
  return createStringError(errc::invalid_argument, "binary image: " + Message);
}

bool checkedEnd(uint64_t Start, uint64_t Size, uint64_t &End) {
  if (Start > UINT64_MAX - Size)
    return false;
  End = Start + Size;
  return true;
}

NevercBinarySegmentFlags segmentFlags(NevercObjectSectionFlags Flags) {
  NevercBinarySegmentFlags Result = NEVERC_BINARY_SEGMENT_READ;
  if ((Flags & NEVERC_OBJECT_SECTION_WRITABLE) != 0)
    Result |= NEVERC_BINARY_SEGMENT_WRITE;
  if ((Flags & NEVERC_OBJECT_SECTION_EXECUTABLE) != 0)
    Result |= NEVERC_BINARY_SEGMENT_EXECUTE;
  return Result;
}

bool isDynamicRelocation(NevercObjectRelocationKind Kind) {
  return Kind == NEVERC_OBJECT_RELOCATION_GOT_RELATIVE ||
         Kind == NEVERC_OBJECT_RELOCATION_PLT_RELATIVE ||
         Kind == NEVERC_OBJECT_RELOCATION_TLS;
}

} // namespace

PluginBinaryImage::PluginBinaryImage(
    PluginTaskContext &TaskValue, NevercLinkOutputKind OutputKindValue,
    NevercTargetID TargetIDValue, NevercObjectFormatID FormatIDValue,
    uint64_t EntryAddressValue, uint64_t ImageBaseValue,
    uint64_t ImportCountValue, uint64_t ExportCountValue,
    uint64_t DynamicRelocationCountValue,
    std::vector<PluginBinarySegment> SegmentsValue,
    std::vector<PluginBinarySection> SectionsValue,
    std::vector<PluginBinaryDirectory> DirectoriesValue,
    std::function<Error(ArrayRef<uint8_t>)> FormatVerifierValue,
    std::unique_ptr<MutableBinaryBuilder> BuilderValue)
    : Task(TaskValue), OutputKind(OutputKindValue), TargetID(TargetIDValue),
      FormatID(FormatIDValue), EntryAddress(EntryAddressValue),
      ImageBase(ImageBaseValue), ImportCount(ImportCountValue),
      ExportCount(ExportCountValue),
      DynamicRelocationCount(DynamicRelocationCountValue),
      Segments(std::move(SegmentsValue)), Sections(std::move(SectionsValue)),
      Directories(std::move(DirectoriesValue)),
      FormatVerifier(std::move(FormatVerifierValue)),
      Builder(std::move(BuilderValue)) {
  LinkAPIControl = std::make_shared<detail::BinaryImageAPIControl>();
  LinkAPIControl->Owner = this;
  UnrestrictedLinkFacade =
      createLinkAPIFacade(detail::BinaryImageAPIAccess::Unrestricted);
  ReadOnlyLinkFacade =
      createLinkAPIFacade(detail::BinaryImageAPIAccess::ReadOnly);
}

std::shared_ptr<detail::BinaryImageAPIFacade>
PluginBinaryImage::createLinkAPIFacade(detail::BinaryImageAPIAccess Access,
                                       const void *MutationDomain,
                                       uint64_t Token) {
  auto Facade = std::make_shared<detail::BinaryImageAPIFacade>();
  Facade->Task = &Task;
  Facade->TaskHandle = Task.handle();
  Facade->Control = LinkAPIControl;
  Facade->MutationDomain = MutationDomain;
  Facade->Token = Token;
  Facade->Access = Access;
  initializeBinaryImageAPI(*Facade);
  Task.retainCallbackContext(Facade);
  return Facade;
}

const NevercLinkAPI &
PluginBinaryImage::capabilityLinkAPI(const PluginPhaseExecutor &Executor,
                                     uint64_t Token) {
  if (Token == 0)
    return ReadOnlyLinkFacade->API;
  std::lock_guard<std::recursive_mutex> Lock(LinkAPIControl->Mutex);
  auto Existing = std::find_if(
      CapabilityLinkFacades.begin(), CapabilityLinkFacades.end(),
      [&](const std::shared_ptr<detail::BinaryImageAPIFacade> &Facade) {
        return Facade->MutationDomain == &Executor && Facade->Token == Token;
      });
  if (Existing != CapabilityLinkFacades.end())
    return (*Existing)->API;
  auto Facade = createLinkAPIFacade(detail::BinaryImageAPIAccess::Capability,
                                    &Executor, Token);
  const NevercLinkAPI &Result = Facade->API;
  CapabilityLinkFacades.push_back(std::move(Facade));
  return Result;
}

Expected<std::shared_ptr<PluginBinaryImage>>
PluginBinaryImage::import(PluginTaskContext &Task, const NevercIOAPI &IO,
                          NevercOutputSinkHandle Sink,
                          PluginBinaryImageData Data) {
  if (Data.OutputKind != NEVERC_LINK_OUTPUT_EXECUTABLE &&
      Data.OutputKind != NEVERC_LINK_OUTPUT_SHARED_LIBRARY &&
      Data.OutputKind != NEVERC_LINK_OUTPUT_BUNDLE)
    return imageError("imported output kind is not a final image");
  auto Builder = MutableBinaryBuilder::create(Task, IO, Sink);
  if (!Builder)
    return Builder.takeError();
  if (!Data.Bytes.empty()) {
    NevercStatus Status = (*Builder)->api().Write(
        (*Builder)->api().Context, Task.handle(), (*Builder)->handle(),
        {Data.Bytes.data(), Data.Bytes.size()});
    if (!neverc_status_is_ok(Status)) {
      (void)(*Builder)->abort();
      return imageError("output budget rejected imported image");
    }
  }
  auto Image = std::shared_ptr<PluginBinaryImage>(new PluginBinaryImage(
      Task, Data.OutputKind, Data.TargetID, Data.FormatID, Data.EntryAddress,
      Data.ImageBase, Data.ImportCount, Data.ExportCount,
      Data.DynamicRelocationCount, std::move(Data.Segments),
      std::move(Data.Sections), std::move(Data.Directories),
      std::move(Data.FormatVerifier), std::move(*Builder)));
  if (Error E = Image->initializeHandles())
    return std::move(E);
  return Image;
}

Expected<std::shared_ptr<PluginBinaryImage>> PluginBinaryImage::emit(
    PluginTaskContext &Task, const NevercIOAPI &IO, NevercOutputSinkHandle Sink,
    const PluginLinkGraph &Graph, NevercLinkOutputKind OutputKind) {
  if (Graph.state() < NEVERC_LINK_STATE_RELOCATIONS_APPLIED)
    return imageError("relocations have not been applied");
  if (OutputKind != NEVERC_LINK_OUTPUT_EXECUTABLE &&
      OutputKind != NEVERC_LINK_OUTPUT_SHARED_LIBRARY &&
      OutputKind != NEVERC_LINK_OUTPUT_BUNDLE)
    return imageError("output kind is not a final image");

  std::vector<PluginBinarySegment> Segments;
  std::vector<PluginBinarySection> Sections;
  uint64_t ImageBase = UINT64_MAX;
  uint64_t ImageSize = 0;
  for (const PluginLinkSection &Section : Graph.sections()) {
    uint64_t FileSize = 0;
    for (const PluginLinkAtom &Atom : Graph.atoms()) {
      if (Atom.SectionID != Section.ID)
        continue;
      if (Atom.FileOffset < Section.FileOffset)
        return imageError("atom file offset precedes its section");
      const uint64_t Relative = Atom.FileOffset - Section.FileOffset;
      uint64_t End = 0;
      if (!checkedEnd(Relative, Atom.Content.size(), End))
        return imageError("section file range overflows");
      FileSize = std::max(FileSize, End);
    }
    uint64_t SectionEnd = 0;
    if (!checkedEnd(Section.FileOffset, FileSize, SectionEnd))
      return imageError("image file range overflows");
    ImageSize = std::max(ImageSize, SectionEnd);
    ImageBase = std::min(ImageBase, Section.Address);

    PluginBinarySegment Segment;
    Segment.Name = Section.Name;
    Segment.Flags = segmentFlags(Section.Flags);
    Segment.Address = Section.Address;
    Segment.MemorySize = Section.Size;
    Segment.FileOffset = Section.FileOffset;
    Segment.FileSize = FileSize;
    Segment.Alignment = Section.Alignment;
    Segments.push_back(std::move(Segment));

    PluginBinarySection BinarySection;
    BinarySection.Name = Section.Name;
    BinarySection.Kind = Section.Kind;
    BinarySection.Flags = Section.Flags;
    BinarySection.Address = Section.Address;
    BinarySection.MemorySize = Section.Size;
    BinarySection.FileOffset = Section.FileOffset;
    BinarySection.FileSize = FileSize;
    BinarySection.Alignment = Section.Alignment;
    BinarySection.SegmentIndex = Segments.size() - 1;
    Sections.push_back(std::move(BinarySection));
  }
  if (ImageBase == UINT64_MAX)
    ImageBase = 0;
  if (ImageSize > std::numeric_limits<size_t>::max())
    return imageError("image exceeds the host address space");
  std::vector<uint8_t> Bytes(static_cast<size_t>(ImageSize), 0);
  for (const PluginLinkAtom &Atom : Graph.atoms()) {
    if (Atom.Content.empty())
      continue;
    uint64_t End = 0;
    if (!checkedEnd(Atom.FileOffset, Atom.Content.size(), End) ||
        End > Bytes.size())
      return imageError("atom content is outside the image");
    std::copy(Atom.Content.begin(), Atom.Content.end(),
              Bytes.begin() + static_cast<size_t>(Atom.FileOffset));
  }

  uint64_t EntryAddress = 0;
  for (const PluginLinkSymbol &Symbol : Graph.symbols()) {
    if (!Symbol.IsRoot || Symbol.AtomID == 0)
      continue;
    const PluginLinkAtom *Atom = Graph.findAtom(Symbol.AtomID);
    if (!Atom || Atom->Address > UINT64_MAX - Symbol.Value)
      return imageError("entry address is invalid");
    EntryAddress = Atom->Address + Symbol.Value;
    if (Symbol.Name == "entry")
      break;
  }
  uint64_t DynamicRelocations = 0;
  for (const PluginLinkEdge &Edge : Graph.edges())
    DynamicRelocations += isDynamicRelocation(Edge.RelocationKind) ? 1 : 0;

  auto Builder = MutableBinaryBuilder::create(Task, IO, Sink);
  if (!Builder)
    return Builder.takeError();
  if (!Bytes.empty()) {
    NevercStatus Status = (*Builder)->api().Write(
        (*Builder)->api().Context, Task.handle(), (*Builder)->handle(),
        {Bytes.data(), Bytes.size()});
    if (!neverc_status_is_ok(Status)) {
      (void)(*Builder)->abort();
      return imageError("output budget rejected emitted image");
    }
  }
  NevercTargetKey Target = Graph.targetKey();
  auto Image = std::shared_ptr<PluginBinaryImage>(new PluginBinaryImage(
      Task, OutputKind, Target.TargetID, Graph.formatID(), EntryAddress,
      ImageBase, Graph.imports().size(), Graph.exports().size(),
      DynamicRelocations, std::move(Segments), std::move(Sections), {}, {},
      std::move(*Builder)));
  if (Error E = Image->initializeHandles())
    return std::move(E);
  return Image;
}

Error PluginBinaryImage::initializeHandles() {
  auto ImageHandle = Task.handles().create(PluginBinaryImageHandleKind, this);
  if (!ImageHandle)
    return ImageHandle.takeError();
  Handle = *ImageHandle;
  for (size_t Index = 0; Index != Segments.size(); ++Index) {
    auto SegmentHandle =
        Task.handles().create(PluginBinarySegmentHandleKind, &Segments[Index]);
    if (!SegmentHandle)
      return SegmentHandle.takeError();
    Segments[Index].Handle = *SegmentHandle;
  }
  for (PluginBinarySection &Section : Sections) {
    if (Section.SegmentIndex < Segments.size())
      Section.Segment = Segments[Section.SegmentIndex].Handle;
    auto SectionHandle =
        Task.handles().create(PluginBinarySectionHandleKind, &Section);
    if (!SectionHandle)
      return SectionHandle.takeError();
    Section.Handle = *SectionHandle;
  }
  return Error::success();
}

PluginBinaryImage::~PluginBinaryImage() {
  std::unique_lock<std::recursive_mutex> Lock(LinkAPIControl->Mutex);
  if (State == NEVERC_BINARY_IMAGE_CANDIDATE && Builder)
    (void)Builder->abort();
  for (PluginBinarySection &Section : Sections)
    if (!neverc_handle_is_null(Section.Handle))
      (void)Task.handles().release(Section.Handle,
                                   PluginBinarySectionHandleKind);
  for (PluginBinarySegment &Segment : Segments)
    if (!neverc_handle_is_null(Segment.Handle))
      (void)Task.handles().release(Segment.Handle,
                                   PluginBinarySegmentHandleKind);
  if (!neverc_handle_is_null(Handle))
    (void)Task.handles().release(Handle, PluginBinaryImageHandleKind);
  LinkAPIControl->Owner = nullptr;
}

NevercTaskHandle PluginBinaryImage::taskHandle() const { return Task.handle(); }

std::array<uint8_t, 32> PluginBinaryImage::contentDigest() const {
  return SHA256::hash(Builder->bytes());
}

Error verifyBinaryImage(const PluginBinaryImage &Image) {
  ArrayRef<uint8_t> Bytes = Image.bytes();
  uint64_t CoveredFileSize = 0;
  uint64_t PreviousFileEnd = 0;
  uint64_t PreviousAddressEnd = 0;
  std::vector<const PluginBinarySegment *> Ordered;
  Ordered.reserve(Image.segments().size());
  for (const PluginBinarySegment &Segment : Image.segments())
    Ordered.push_back(&Segment);
  std::sort(
      Ordered.begin(), Ordered.end(),
      [](const PluginBinarySegment *Left, const PluginBinarySegment *Right) {
        return Left->Address < Right->Address;
      });
  bool EntryCovered = Image.entryAddress() == 0;
  for (const PluginBinarySegment *Segment : Ordered) {
    if (!isPowerOf2_64(Segment->Alignment))
      return imageError("segment alignment is invalid");
    uint64_t FileEnd = 0;
    uint64_t AddressEnd = 0;
    if (!checkedEnd(Segment->FileOffset, Segment->FileSize, FileEnd) ||
        !checkedEnd(Segment->Address, Segment->MemorySize, AddressEnd) ||
        FileEnd > Bytes.size())
      return imageError("segment range is outside the image");
    if (Segment->FileSize != 0 && Segment->FileOffset < PreviousFileEnd)
      return imageError("segment file ranges overlap");
    if (Segment->MemorySize != 0 && Segment->Address < PreviousAddressEnd)
      return imageError("segment address ranges overlap");
    if ((Segment->Flags & NEVERC_BINARY_SEGMENT_WRITE) != 0 &&
        (Segment->Flags & NEVERC_BINARY_SEGMENT_EXECUTE) != 0)
      return imageError("writable-executable segment is forbidden");
    if ((Segment->Flags & NEVERC_BINARY_SEGMENT_EXECUTE) != 0 &&
        Image.entryAddress() >= Segment->Address &&
        Image.entryAddress() < AddressEnd)
      EntryCovered = true;
    PreviousFileEnd = std::max(PreviousFileEnd, FileEnd);
    PreviousAddressEnd = std::max(PreviousAddressEnd, AddressEnd);
    CoveredFileSize = std::max(CoveredFileSize, FileEnd);
  }
  if (!EntryCovered)
    return imageError("entry address is outside executable segments");
  if (CoveredFileSize != Bytes.size())
    return imageError("unclaimed bytes trail the declared image ranges");
  for (const PluginBinarySection &Section : Image.sections()) {
    uint64_t FileEnd = 0;
    if (!checkedEnd(Section.FileOffset, Section.FileSize, FileEnd) ||
        FileEnd > Bytes.size())
      return imageError("section range is outside the image");
    if (neverc_handle_is_null(Section.Segment))
      return imageError("section has no containing segment");
  }
  for (const PluginBinaryDirectory &Directory : Image.directories()) {
    uint64_t End = 0;
    if (Directory.Kind.empty() ||
        !checkedEnd(Directory.Offset, Directory.Size, End) ||
        End > Bytes.size())
      return imageError("format directory is outside the image");
  }
  return Error::success();
}

Error PluginBinaryImage::verify() {
  if (State == NEVERC_BINARY_IMAGE_VERIFIED ||
      State == NEVERC_BINARY_IMAGE_COMMITTED)
    return Error::success();
  if (State != NEVERC_BINARY_IMAGE_CANDIDATE)
    return imageError("only a candidate image can be verified");
  if (FormatVerifier)
    if (Error E = FormatVerifier(Builder->bytes()))
      return joinErrors(imageError("format verifier rejected image"),
                        std::move(E));
  if (Error E = verifyBinaryImage(*this))
    return E;
  auto Seal = Builder->finish();
  if (!Seal)
    return Seal.takeError();
  State = NEVERC_BINARY_IMAGE_VERIFIED;
  return Error::success();
}

void PluginBinaryImage::markCommitted() {
  if (State == NEVERC_BINARY_IMAGE_VERIFIED)
    State = NEVERC_BINARY_IMAGE_COMMITTED;
}

void PluginBinaryImage::markFailedPartial() {
  State = NEVERC_BINARY_IMAGE_FAILED_PARTIAL;
}

Error PluginBinaryImage::abort() {
  if (State == NEVERC_BINARY_IMAGE_ABORTED)
    return Error::success();
  if (State == NEVERC_BINARY_IMAGE_COMMITTED ||
      State == NEVERC_BINARY_IMAGE_FAILED_PARTIAL)
    return imageError("published image cannot be aborted");
  NevercStatus Status = Builder->abort();
  if (!neverc_status_is_ok(Status))
    return imageError("candidate image abort failed");
  State = NEVERC_BINARY_IMAGE_ABORTED;
  return Error::success();
}

} // namespace neverc::plugin
