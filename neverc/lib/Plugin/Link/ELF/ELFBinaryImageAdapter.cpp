#include "ELFLinkGraphAdapter.h"
#include "Link/BinaryImage.h"
#include "Link/LinkOutputBundle.h"
#include "neverc/Linker/ELF/Config.h"
#include "neverc/Linker/ELF/Emit.h"
#include "neverc/Linker/ELF/OutputSections.h"
#include "neverc/Linker/ELF/SymbolTable.h"
#include "neverc/Linker/ELF/Symbols.h"
#include "neverc/Linker/ELF/SyntheticSections.h"
#include "neverc/Plugin/Host/ObjectSectionRole.h"
#include "neverc/Plugin/Host/PluginIOBridge.h"
#include "neverc/Plugin/Host/PluginInterfaceRegistry.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/MemoryBuffer.h"
#include <algorithm>
#include <limits>

// NOTE: This adapter deliberately does NOT include ELFContextAccess.h. That
// header defines source-compatibility macros (`in`, `symtab`) for the
// incrementally-ported backend .cpp files; in particular `#define in elfIn()`
// rewrites the ubiquitous identifier `in`, colliding with std::ios_base::in
// whenever a standard-library <istream>/<ios> header is parsed in the same TU.
// New code uses the explicit accessors (elfSymtab(), etc.) instead.

using namespace llvm;
using namespace llvm::ELF;
using namespace neverc::plugin;

namespace linker::elf {
namespace {

Error imageError(const Twine &Message) {
  return createStringError(errc::invalid_argument,
                           "ELF BinaryImage adapter: " + Message);
}

template <typename T> T readEndian(const uint8_t *Data, bool LittleEndian);

template <>
uint16_t readEndian<uint16_t>(const uint8_t *Data, bool LittleEndian) {
  return LittleEndian ? support::endian::read16le(Data)
                      : support::endian::read16be(Data);
}

template <>
uint64_t readEndian<uint64_t>(const uint8_t *Data, bool LittleEndian) {
  return LittleEndian ? support::endian::read64le(Data)
                      : support::endian::read64be(Data);
}

bool validRange(uint64_t Offset, uint64_t Count, uint64_t ElementSize,
                uint64_t ImageSize) {
  return ElementSize == 0 ? Count == 0
                          : Count <= (ImageSize - std::min(Offset, ImageSize)) /
                                         ElementSize &&
                                Offset <= ImageSize &&
                                Offset + Count * ElementSize <= ImageSize;
}

Error verifyELF64Image(ArrayRef<uint8_t> Bytes, uint16_t ExpectedMachine) {
  if (Bytes.size() < 64 ||
      Bytes[EI_MAG0] != static_cast<uint8_t>(ElfMagic[EI_MAG0]) ||
      Bytes[EI_MAG1] != static_cast<uint8_t>(ElfMagic[EI_MAG1]) ||
      Bytes[EI_MAG2] != static_cast<uint8_t>(ElfMagic[EI_MAG2]) ||
      Bytes[EI_MAG3] != static_cast<uint8_t>(ElfMagic[EI_MAG3]))
    return imageError("ELF magic or header is invalid");
  if (Bytes[EI_CLASS] != ELFCLASS64)
    return imageError("only ELF64 native images are supported");
  if (Bytes[EI_DATA] != ELFDATA2LSB && Bytes[EI_DATA] != ELFDATA2MSB)
    return imageError("ELF data encoding is invalid");
  const bool IsLE = Bytes[EI_DATA] == ELFDATA2LSB;
  const uint16_t Machine = readEndian<uint16_t>(Bytes.data() + 18, IsLE);
  const uint16_t EHSize = readEndian<uint16_t>(Bytes.data() + 52, IsLE);
  const uint64_t PHOffset = readEndian<uint64_t>(Bytes.data() + 32, IsLE);
  const uint64_t SHOffset = readEndian<uint64_t>(Bytes.data() + 40, IsLE);
  const uint16_t PHEntSize = readEndian<uint16_t>(Bytes.data() + 54, IsLE);
  const uint16_t PHCount = readEndian<uint16_t>(Bytes.data() + 56, IsLE);
  const uint16_t SHEntSize = readEndian<uint16_t>(Bytes.data() + 58, IsLE);
  const uint16_t SHCount = readEndian<uint16_t>(Bytes.data() + 60, IsLE);
  if (EHSize != 64 || Machine != ExpectedMachine)
    return imageError("ELF class, machine, or header size changed");
  if ((PHCount != 0 && PHEntSize != 56) ||
      !validRange(PHOffset, PHCount, PHEntSize, Bytes.size()))
    return imageError("ELF program header table is out of range");
  if ((SHCount != 0 && SHEntSize != 64) ||
      !validRange(SHOffset, SHCount, SHEntSize, Bytes.size()))
    return imageError("ELF section header table is out of range");
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
  if (isDebugSectionName(BuiltinObjectFormat::ELF, Section.name))
    return NEVERC_OBJECT_SECTION_KIND_DEBUG;
  if (isUnwindSectionName(BuiltinObjectFormat::ELF, Section.name))
    return NEVERC_OBJECT_SECTION_KIND_UNWIND;
  if ((Section.flags & SHF_TLS) != 0)
    return Section.type == SHT_NOBITS ? NEVERC_OBJECT_SECTION_KIND_TLS_ZERO_FILL
                                      : NEVERC_OBJECT_SECTION_KIND_TLS_DATA;
  if (Section.type == SHT_NOBITS)
    return NEVERC_OBJECT_SECTION_KIND_ZERO_FILL;
  if ((Section.flags & SHF_EXECINSTR) != 0)
    return NEVERC_OBJECT_SECTION_KIND_TEXT;
  if ((Section.flags & SHF_WRITE) == 0)
    return NEVERC_OBJECT_SECTION_KIND_READ_ONLY_DATA;
  return NEVERC_OBJECT_SECTION_KIND_DATA;
}

NevercObjectSectionFlags binarySectionFlags(const OutputSection &Section) {
  NevercObjectSectionFlags Flags = 0;
  if ((Section.flags & SHF_ALLOC) != 0)
    Flags |= NEVERC_OBJECT_SECTION_ALLOCATED;
  if ((Section.flags & SHF_EXECINSTR) != 0)
    Flags |= NEVERC_OBJECT_SECTION_EXECUTABLE;
  if ((Section.flags & SHF_WRITE) != 0)
    Flags |= NEVERC_OBJECT_SECTION_WRITABLE;
  if ((Section.flags & SHF_MERGE) != 0)
    Flags |= NEVERC_OBJECT_SECTION_MERGEABLE;
  if ((Section.flags & SHF_STRINGS) != 0)
    Flags |= NEVERC_OBJECT_SECTION_STRINGS;
  if ((Section.flags & SHF_TLS) != 0)
    Flags |= NEVERC_OBJECT_SECTION_TLS;
  if (isDebugSectionName(BuiltinObjectFormat::ELF, Section.name))
    Flags |= NEVERC_OBJECT_SECTION_DEBUG;
  if (isUnwindSectionName(BuiltinObjectFormat::ELF, Section.name))
    Flags |= NEVERC_OBJECT_SECTION_UNWIND;
  return Flags;
}

} // namespace

Error ELFLinkGraphAdapter::publishImage(ArrayRef<uint8_t> Bytes) {
  if (!Graph || Graph->state() != NEVERC_LINK_STATE_IMAGE_EMITTED)
    return imageError("LinkGraph has not reached IMAGE_EMITTED");
  if (Bytes.empty())
    return imageError("native ELF image is empty");

  auto IO = getIOAPI(Task);
  if (!IO)
    return joinErrors(imageError("I/O interface is unavailable"),
                      IO.takeError());

  const uint64_t Growth = std::min<uint64_t>(
      UINT64_C(64) * 1024 * 1024, std::max<uint64_t>(4096, Bytes.size() / 16));
  const uint64_t Budget =
      Bytes.size() > UINT64_MAX - Growth ? UINT64_MAX : Bytes.size() + Growth;
  const NevercTaskHandle Handle = Task.handle();
  const std::string SinkName = "elf-native-image-" +
                               std::to_string(Handle.Owner) + "-" +
                               std::to_string(Handle.Value);
  NevercOutputSinkHandle Sink{};
  NevercStatus Status = (*IO)->BeginMemoryOutput(
      (*IO)->Context, Handle,
      {SinkName.data(), static_cast<uint64_t>(SinkName.size())}, Budget, &Sink);
  if (!neverc_status_is_ok(Status))
    return imageError("could not create native image staging sink");

  PluginBinaryImageData Data;
  Data.OutputKind = config->relocatable ? NEVERC_LINK_OUTPUT_RELOCATABLE
                    : config->shared    ? NEVERC_LINK_OUTPUT_SHARED_LIBRARY
                                        : NEVERC_LINK_OUTPUT_EXECUTABLE;
  const NevercTargetKey TargetKey = Graph->targetKey();
  Data.TargetID = TargetKey.TargetID;
  Data.FormatID = Graph->formatID();
  if (!config->entry.empty())
    if (Symbol *Entry = elfSymtab().find(config->entry))
      Data.EntryAddress = Entry->getVA();

  uint64_t ImageEnd = 0;
  bool HasExecutableLoad = false;
  for (PhdrEntry *ProgramHeader : mainPart->phdrs) {
    if (!ProgramHeader || ProgramHeader->p_type != PT_LOAD)
      continue;
    if (Data.ImageBase == 0 || ProgramHeader->p_vaddr < Data.ImageBase)
      Data.ImageBase = ProgramHeader->p_vaddr;
    ImageEnd =
        std::max(ImageEnd, ProgramHeader->p_vaddr + ProgramHeader->p_memsz);
    HasExecutableLoad |= (ProgramHeader->p_flags & PF_X) != 0;
  }

  PluginBinarySegment FileSegment;
  FileSegment.Name = "ELF.file";
  FileSegment.Flags = NEVERC_BINARY_SEGMENT_READ;
  if (HasExecutableLoad || Data.EntryAddress != 0)
    FileSegment.Flags |= NEVERC_BINARY_SEGMENT_EXECUTE;
  FileSegment.Address = Data.ImageBase;
  FileSegment.MemorySize = std::max<uint64_t>(
      Bytes.size(), ImageEnd >= Data.ImageBase ? ImageEnd - Data.ImageBase : 0);
  FileSegment.FileSize = Bytes.size();
  FileSegment.Alignment = config->maxPageSize == 0 ? 1 : config->maxPageSize;
  Data.Segments.push_back(std::move(FileSegment));

  for (OutputSection *Native : outputSections) {
    if (!Native)
      continue;
    PluginBinarySection Section;
    Section.SegmentIndex = 0;
    Section.Name = Native->name.empty() ? "<elf-output>" : Native->name.str();
    Section.Kind = binarySectionKind(*Native);
    Section.Flags = binarySectionFlags(*Native);
    Section.Address = Native->addr;
    Section.MemorySize = Native->size;
    Section.FileOffset = Native->offset;
    Section.FileSize =
        Native->type == SHT_NOBITS
            ? 0
            : std::min<uint64_t>(Native->size,
                                 Native->offset <= Bytes.size()
                                     ? Bytes.size() - Native->offset
                                     : 0);
    Section.Alignment =
        isPowerOf2_64(Native->addralign) && Native->addralign != 0
            ? Native->addralign
            : 1;
    Data.Sections.push_back(std::move(Section));
  }

  if (Bytes.size() >= 64) {
    const bool IsLE = Bytes[EI_DATA] == ELFDATA2LSB;
    const uint64_t PHOffset = readEndian<uint64_t>(Bytes.data() + 32, IsLE);
    const uint64_t SHOffset = readEndian<uint64_t>(Bytes.data() + 40, IsLE);
    const uint16_t PHEntSize = readEndian<uint16_t>(Bytes.data() + 54, IsLE);
    const uint16_t PHCount = readEndian<uint16_t>(Bytes.data() + 56, IsLE);
    const uint16_t SHEntSize = readEndian<uint16_t>(Bytes.data() + 58, IsLE);
    const uint16_t SHCount = readEndian<uint16_t>(Bytes.data() + 60, IsLE);
    if (validRange(PHOffset, PHCount, PHEntSize, Bytes.size()))
      Data.Directories.push_back({"elf.program-headers", PHOffset,
                                  static_cast<uint64_t>(PHCount) * PHEntSize});
    if (validRange(SHOffset, SHCount, SHEntSize, Bytes.size()))
      Data.Directories.push_back({"elf.section-headers", SHOffset,
                                  static_cast<uint64_t>(SHCount) * SHEntSize});
  }
  Data.ImportCount = Graph->imports().size();
  Data.ExportCount = Graph->exports().size();
  Data.DynamicRelocationCount =
      llvm::count_if(Graph->edges(), [](const PluginLinkEdge &Edge) {
        return Edge.RelocationKind == NEVERC_OBJECT_RELOCATION_TARGET_EXTENSION;
      });
  Data.Bytes.assign(Bytes.begin(), Bytes.end());
  const uint16_t Machine = config->emachine;
  Data.FormatVerifier = [Machine](ArrayRef<uint8_t> Candidate) {
    return verifyELF64Image(Candidate, Machine);
  };

  auto Image = PluginBinaryImage::import(Task, **IO, Sink, std::move(Data));
  if (!Image)
    return joinErrors(imageError("could not import native ELF image"),
                      Image.takeError());

  std::vector<PluginLinkSideOutput> SideOutputs;
  if (!config->mapFile.empty()) {
    auto Map = MemoryBuffer::getFile(config->mapFile);
    if (Map) {
      const StringRef Contents = (*Map)->getBuffer();
      SideOutputs.push_back(
          {"map", config->mapFile.str(),
           std::vector<uint8_t>(Contents.bytes_begin(), Contents.bytes_end())});
    }
  }

  auto Pipeline = LinkOutputPipeline::create(
      Task, Task.processServices().outputCoordinator());
  if (!Pipeline)
    return joinErrors(imageError("could not create output pipeline"),
                      Pipeline.takeError());
  auto Published =
      (*Pipeline)->execute(std::move(*Image), config->outputFile, SideOutputs);
  if (!Published)
    return joinErrors(imageError("transactional publication failed"),
                      Published.takeError());
  return Error::success();
}

} // namespace linker::elf
