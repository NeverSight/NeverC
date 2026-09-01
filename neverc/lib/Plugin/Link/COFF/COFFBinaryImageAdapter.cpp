#include "COFFLinkGraphAdapter.h"
#include "Link/BinaryImage.h"
#include "Link/LinkOutputBundle.h"
#include "neverc/Linker/COFF/COFFLinkerContext.h"
#include "neverc/Linker/COFF/Config.h"
#include "neverc/Linker/COFF/Emit.h"
#include "neverc/Linker/COFF/Symbols.h"
#include "neverc/Plugin/Host/ObjectSectionRole.h"
#include "neverc/Plugin/Host/PluginIOBridge.h"
#include "neverc/Plugin/Host/PluginInterfaceRegistry.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/MemoryBuffer.h"
#include <algorithm>
#include <array>
#include <limits>

using namespace llvm;
using namespace llvm::COFF;
using namespace neverc::plugin;

namespace linker::coff {
namespace {

Error imageError(const Twine &Message) {
  return createStringError(errc::invalid_argument,
                           "COFF BinaryImage adapter: " + Message);
}

bool validRange(uint64_t Offset, uint64_t Size, uint64_t ImageSize) {
  return Offset <= ImageSize && Size <= ImageSize - Offset;
}

Error verifyPEImage(ArrayRef<uint8_t> Bytes, uint16_t ExpectedMachine) {
  if (Bytes.size() < 64 || support::endian::read16le(Bytes.data()) != 0x5a4d)
    return imageError("DOS header or magic is invalid");
  const uint32_t PEOffset = support::endian::read32le(Bytes.data() + 0x3c);
  if (!validRange(PEOffset, 24, Bytes.size()) ||
      support::endian::read32le(Bytes.data() + PEOffset) != 0x00004550)
    return imageError("PE signature or COFF header is invalid");
  const uint8_t *COFFHeader = Bytes.data() + PEOffset + 4;
  const uint16_t Machine = support::endian::read16le(COFFHeader);
  const uint16_t SectionCount = support::endian::read16le(COFFHeader + 2);
  const uint16_t OptionalSize = support::endian::read16le(COFFHeader + 16);
  const uint64_t OptionalOffset = PEOffset + 24;
  if (Machine != ExpectedMachine ||
      !validRange(OptionalOffset, OptionalSize, Bytes.size()) ||
      OptionalSize < 112 ||
      support::endian::read16le(Bytes.data() + OptionalOffset) !=
          PE32Header::PE32_PLUS)
    return imageError("PE machine or optional header is invalid");
  const uint64_t SectionTable = OptionalOffset + OptionalSize;
  if (SectionCount >
          (Bytes.size() - std::min<uint64_t>(SectionTable, Bytes.size())) /
              sizeof(llvm::object::coff_section) ||
      !validRange(SectionTable,
                  uint64_t(SectionCount) * sizeof(llvm::object::coff_section),
                  Bytes.size()))
    return imageError("PE section table is out of range");
  for (uint16_t Index = 0; Index != SectionCount; ++Index) {
    const uint8_t *Header =
        Bytes.data() + SectionTable +
        uint64_t(Index) * sizeof(llvm::object::coff_section);
    const uint32_t RawSize = support::endian::read32le(Header + 16);
    const uint32_t RawOffset = support::endian::read32le(Header + 20);
    if (RawSize != 0 && !validRange(RawOffset, RawSize, Bytes.size()))
      return imageError("PE section contents are out of range");
  }
  return Error::success();
}

Expected<const NevercIOAPI *> getIOAPI(PluginTaskContext &Task) {
  auto Query = Task.processServices().interfaces().query(
      ioPluginInterfaceID(), NEVERC_IO_API_MAJOR, NEVERC_IO_API_MINOR);
  if (!Query)
    return Query.takeError();
  return static_cast<const NevercIOAPI *>(Query->Table);
}

NevercObjectSectionKind binarySectionKind(const OutputSection &Section) {
  const uint32_t Characteristics = Section.header.Characteristics;
  if (isDebugSectionName(BuiltinObjectFormat::COFF, Section.name))
    return NEVERC_OBJECT_SECTION_KIND_DEBUG;
  if (isUnwindSectionName(BuiltinObjectFormat::COFF, Section.name))
    return NEVERC_OBJECT_SECTION_KIND_UNWIND;
  // The header has no thread-local bit the way ELF has SHF_TLS, so the name is
  // the only statement of it -- the same answer the link graph adapter and the
  // object reader give for a COFF section.
  if (isThreadLocalSectionName(BuiltinObjectFormat::COFF, Section.name))
    return (Characteristics & IMAGE_SCN_CNT_UNINITIALIZED_DATA) != 0
               ? NEVERC_OBJECT_SECTION_KIND_TLS_ZERO_FILL
               : NEVERC_OBJECT_SECTION_KIND_TLS_DATA;
  if ((Characteristics & IMAGE_SCN_CNT_CODE) != 0 ||
      (Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0)
    return NEVERC_OBJECT_SECTION_KIND_TEXT;
  if ((Characteristics & IMAGE_SCN_CNT_UNINITIALIZED_DATA) != 0)
    return NEVERC_OBJECT_SECTION_KIND_ZERO_FILL;
  if ((Characteristics & IMAGE_SCN_MEM_WRITE) == 0)
    return NEVERC_OBJECT_SECTION_KIND_READ_ONLY_DATA;
  return NEVERC_OBJECT_SECTION_KIND_DATA;
}

NevercObjectSectionFlags binarySectionFlags(const OutputSection &Section) {
  const uint32_t Characteristics = Section.header.Characteristics;
  NevercObjectSectionFlags Flags = NEVERC_OBJECT_SECTION_ALLOCATED;
  if ((Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0)
    Flags |= NEVERC_OBJECT_SECTION_EXECUTABLE;
  if ((Characteristics & IMAGE_SCN_MEM_WRITE) != 0)
    Flags |= NEVERC_OBJECT_SECTION_WRITABLE;
  if ((Characteristics & IMAGE_SCN_MEM_DISCARDABLE) != 0)
    Flags |= NEVERC_OBJECT_SECTION_DISCARDABLE;
  if (isDebugSectionName(BuiltinObjectFormat::COFF, Section.name))
    Flags |= NEVERC_OBJECT_SECTION_DEBUG;
  if (isUnwindSectionName(BuiltinObjectFormat::COFF, Section.name))
    Flags |= NEVERC_OBJECT_SECTION_UNWIND;
  if (isThreadLocalSectionName(BuiltinObjectFormat::COFF, Section.name))
    Flags |= NEVERC_OBJECT_SECTION_TLS;
  return Flags;
}

std::optional<uint64_t> rvaToFileOffset(const COFFLinkerContext &Context,
                                        uint32_t RVA, uint32_t Size) {
  for (OutputSection *Section : Context.outputSections) {
    if (!Section || RVA < Section->getRVA())
      continue;
    const uint64_t Delta = uint64_t(RVA) - Section->getRVA();
    if (Delta <= Section->getRawSize() && Size <= Section->getRawSize() - Delta)
      return Section->getFileOff() + Delta;
  }
  return std::nullopt;
}

void addFileSideOutput(std::vector<PluginLinkSideOutput> &Outputs,
                       StringRef Kind, StringRef Path) {
  if (Path.empty())
    return;
  auto Buffer = MemoryBuffer::getFile(Path);
  if (!Buffer)
    return;
  StringRef Contents = (*Buffer)->getBuffer();
  Outputs.push_back(
      {Kind.str(), Path.str(),
       std::vector<uint8_t>(Contents.bytes_begin(), Contents.bytes_end())});
}

} // namespace

Error COFFLinkGraphAdapter::publishImage(ArrayRef<uint8_t> Bytes) {
  if (!Graph || Graph->state() != NEVERC_LINK_STATE_IMAGE_EMITTED)
    return imageError("LinkGraph has not reached IMAGE_EMITTED");
  if (Bytes.empty())
    return imageError("native PE image is empty");

  auto IO = getIOAPI(Task);
  if (!IO)
    return joinErrors(imageError("I/O interface is unavailable"),
                      IO.takeError());

  const uint64_t Growth = std::min<uint64_t>(
      UINT64_C(64) * 1024 * 1024, std::max<uint64_t>(4096, Bytes.size() / 16));
  const uint64_t Budget =
      Bytes.size() > UINT64_MAX - Growth ? UINT64_MAX : Bytes.size() + Growth;
  const NevercTaskHandle Handle = Task.handle();
  const std::string SinkName = "coff-native-image-" +
                               std::to_string(Handle.Owner) + "-" +
                               std::to_string(Handle.Value);
  NevercOutputSinkHandle Sink{};
  NevercStatus Status = (*IO)->BeginMemoryOutput(
      (*IO)->Context, Handle,
      {SinkName.data(), static_cast<uint64_t>(SinkName.size())}, Budget, &Sink);
  if (!neverc_status_is_ok(Status))
    return imageError("could not create native image staging sink");

  PluginBinaryImageData Data;
  Data.OutputKind = Context.config.dll ? NEVERC_LINK_OUTPUT_SHARED_LIBRARY
                                       : NEVERC_LINK_OUTPUT_EXECUTABLE;
  const NevercTargetKey TargetKey = Graph->targetKey();
  Data.TargetID = TargetKey.TargetID;
  Data.FormatID = Graph->formatID();
  Data.ImageBase = Context.config.imageBase;
  if (auto *Entry = dyn_cast_or_null<Defined>(Context.config.entry))
    Data.EntryAddress = Context.config.imageBase + Entry->getRVA();

  uint64_t ImageEnd = Context.config.imageBase;
  bool HasExecutable = false;
  for (OutputSection *Native : Context.outputSections) {
    if (!Native)
      continue;
    ImageEnd = std::max(ImageEnd, Context.config.imageBase + Native->getRVA() +
                                      Native->getVirtualSize());
    HasExecutable |=
        (Native->header.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
  }

  PluginBinarySegment ImageSegment;
  ImageSegment.Name = "PE.image";
  ImageSegment.Flags = NEVERC_BINARY_SEGMENT_READ;
  if (HasExecutable || Data.EntryAddress != 0)
    ImageSegment.Flags |= NEVERC_BINARY_SEGMENT_EXECUTE;
  ImageSegment.Address = Data.ImageBase;
  ImageSegment.MemorySize =
      std::max<uint64_t>(Bytes.size(), ImageEnd - Data.ImageBase);
  ImageSegment.FileSize = Bytes.size();
  ImageSegment.Alignment = Context.config.align == 0 ? 1 : Context.config.align;
  Data.Segments.push_back(std::move(ImageSegment));

  for (OutputSection *Native : Context.outputSections) {
    if (!Native)
      continue;
    PluginBinarySection Section;
    Section.SegmentIndex = 0;
    Section.Name = Native->name.empty() ? "<coff-output>" : Native->name.str();
    Section.Kind = binarySectionKind(*Native);
    Section.Flags = binarySectionFlags(*Native);
    Section.Address = Data.ImageBase + Native->getRVA();
    Section.MemorySize = Native->getVirtualSize();
    Section.FileOffset = Native->getFileOff();
    Section.FileSize = std::min<uint64_t>(
        Native->getRawSize(), Native->getFileOff() <= Bytes.size()
                                  ? Bytes.size() - Native->getFileOff()
                                  : 0);
    Section.Alignment =
        Context.config.fileAlign == 0 ? 1 : Context.config.fileAlign;
    Data.Sections.push_back(std::move(Section));
  }

  if (Bytes.size() >= 64) {
    const uint32_t PEOffset = support::endian::read32le(Bytes.data() + 0x3c);
    if (validRange(PEOffset, 24, Bytes.size())) {
      const uint8_t *COFFHeader = Bytes.data() + PEOffset + 4;
      const uint16_t SectionCount = support::endian::read16le(COFFHeader + 2);
      const uint16_t OptionalSize = support::endian::read16le(COFFHeader + 16);
      const uint64_t OptionalOffset = PEOffset + 24;
      const uint64_t SectionTable = OptionalOffset + OptionalSize;
      if (validRange(SectionTable,
                     uint64_t(SectionCount) *
                         sizeof(llvm::object::coff_section),
                     Bytes.size()))
        Data.Directories.push_back(
            {"pe.section-table", SectionTable,
             uint64_t(SectionCount) * sizeof(llvm::object::coff_section)});

      if (OptionalSize >= 112 &&
          validRange(OptionalOffset, OptionalSize, Bytes.size())) {
        const uint32_t Count = std::min<uint32_t>(
            16, support::endian::read32le(Bytes.data() + OptionalOffset + 108));
        static constexpr std::array<const char *, 16> kPEDirectoryNames = {
            "exports",    "imports",       "resources",
            "exceptions", "certificates",  "base-relocations",
            "debug",      "architecture",  "global-pointer",
            "tls",        "load-config",   "bound-imports",
            "iat",        "delay-imports", "clr",
            "reserved"};
        for (uint32_t Index = 0; Index != Count; ++Index) {
          const uint64_t DirectoryOffset =
              OptionalOffset + 112 + uint64_t(Index) * 8;
          if (!validRange(DirectoryOffset, 8, Bytes.size()))
            break;
          const uint32_t RVA =
              support::endian::read32le(Bytes.data() + DirectoryOffset);
          const uint32_t Size =
              support::endian::read32le(Bytes.data() + DirectoryOffset + 4);
          if (RVA == 0 || Size == 0 || Index == 4)
            continue;
          if (std::optional<uint64_t> FileOffset =
                  rvaToFileOffset(Context, RVA, Size))
            Data.Directories.push_back(
                {("pe." + Twine(kPEDirectoryNames[Index])).str(), *FileOffset,
                 Size});
        }
      }
    }
  }

  Data.ImportCount = Graph->imports().size();
  Data.ExportCount = Graph->exports().size();
  Data.DynamicRelocationCount =
      llvm::count_if(Graph->edges(), [](const PluginLinkEdge &Edge) {
        return Edge.RelocationKind == NEVERC_OBJECT_RELOCATION_TARGET_EXTENSION;
      });
  Data.Bytes.assign(Bytes.begin(), Bytes.end());
  const uint16_t Machine = Context.config.machine;
  Data.FormatVerifier = [Machine](ArrayRef<uint8_t> Candidate) {
    return verifyPEImage(Candidate, Machine);
  };

  auto Image = PluginBinaryImage::import(Task, **IO, Sink, std::move(Data));
  if (!Image)
    return joinErrors(imageError("could not import native PE image"),
                      Image.takeError());

  std::vector<PluginLinkSideOutput> SideOutputs;
  addFileSideOutput(SideOutputs, "map", Context.config.mapFile);
  if (!Context.config.noimplib)
    addFileSideOutput(SideOutputs, "import-library", Context.config.implib);

  auto Pipeline = LinkOutputPipeline::create(
      Task, Task.processServices().outputCoordinator());
  if (!Pipeline)
    return joinErrors(imageError("could not create output pipeline"),
                      Pipeline.takeError());
  auto Published = (*Pipeline)->execute(std::move(*Image),
                                        Context.config.outputFile, SideOutputs);
  if (!Published)
    return joinErrors(imageError("transactional publication failed"),
                      Published.takeError());
  return Error::success();
}

} // namespace linker::coff
