#include "MachOLinkGraphAdapter.h"

#include "Link/BinaryImage.h"
#include "Link/LinkOutputBundle.h"
#include "Linker/MachO/Config.h"
#include "neverc/Plugin/Host/PluginIOBridge.h"
#include "neverc/Plugin/Host/PluginInterfaceRegistry.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/MemoryBuffer.h"
#include <algorithm>
#include <string>

using namespace llvm;
using namespace neverc::plugin;

namespace linker::macho {
namespace {

Error imageError(const Twine &Message) {
  return createStringError(errc::invalid_argument,
                           "Mach-O BinaryImage adapter: " + Message);
}

Error verifyMachOImage(ArrayRef<uint8_t> Bytes) {
  if (Bytes.size() < sizeof(uint32_t))
    return imageError("Mach-O image is smaller than its magic");
  const uint8_t B0 = Bytes[0];
  const uint8_t B1 = Bytes[1];
  const uint8_t B2 = Bytes[2];
  const uint8_t B3 = Bytes[3];
  const bool Magic64 = B0 == 0xcf && B1 == 0xfa && B2 == 0xed && B3 == 0xfe;
  const bool Magic32 = B0 == 0xce && B1 == 0xfa && B2 == 0xed && B3 == 0xfe;
  if (!Magic64 && !Magic32)
    return imageError("Mach-O magic is invalid");
  return Error::success();
}

Expected<const NevercIOAPI *> getIOAPI(PluginTaskContext &Task) {
  auto Query = Task.processServices().interfaces().query(
      ioPluginInterfaceID(), NEVERC_IO_API_MAJOR, NEVERC_IO_API_MINOR);
  if (!Query)
    return Query.takeError();
  return static_cast<const NevercIOAPI *>(Query->Table);
}

NevercLinkOutputKind outputKindFor(llvm::MachO::HeaderFileType Type) {
  switch (Type) {
  case llvm::MachO::MH_EXECUTE:
    return NEVERC_LINK_OUTPUT_EXECUTABLE;
  case llvm::MachO::MH_DYLIB:
  case llvm::MachO::MH_BUNDLE:
    return NEVERC_LINK_OUTPUT_SHARED_LIBRARY;
  default:
    return NEVERC_LINK_OUTPUT_EXECUTABLE;
  }
}

} // namespace

Error MachOLinkGraphAdapter::publishImage(ArrayRef<uint8_t> Bytes) {
  if (!Graph || Graph->state() != NEVERC_LINK_STATE_IMAGE_EMITTED)
    return imageError("LinkGraph has not reached IMAGE_EMITTED");
  if (Bytes.empty())
    return imageError("native Mach-O image is empty");

  auto IO = getIOAPI(Task);
  if (!IO)
    return joinErrors(imageError("I/O interface is unavailable"),
                      IO.takeError());

  const uint64_t Growth = std::min<uint64_t>(
      UINT64_C(64) * 1024 * 1024, std::max<uint64_t>(4096, Bytes.size() / 16));
  const uint64_t Budget =
      Bytes.size() > UINT64_MAX - Growth ? UINT64_MAX : Bytes.size() + Growth;
  const NevercTaskHandle Handle = Task.handle();
  const std::string SinkName = "macho-native-image-" +
                               std::to_string(Handle.Owner) + "-" +
                               std::to_string(Handle.Value);
  NevercOutputSinkHandle Sink{};
  NevercStatus Status = (*IO)->BeginMemoryOutput(
      (*IO)->Context, Handle,
      {SinkName.data(), static_cast<uint64_t>(SinkName.size())}, Budget, &Sink);
  if (!neverc_status_is_ok(Status))
    return imageError("could not create native image staging sink");

  PluginBinaryImageData Data;
  Data.OutputKind = outputKindFor(machoConfig()->outputType);
  const NevercTargetKey TargetKey = Graph->targetKey();
  Data.TargetID = TargetKey.TargetID;
  Data.FormatID = Graph->formatID();
  // The Mach-O entry lives inside the emitted image; expose it as metadata only
  // by leaving EntryAddress at zero so the host verifier does not require an
  // executable-segment projection for this first-version single-segment model.
  Data.EntryAddress = 0;

  // Model the finished image as one file-spanning segment. This is sufficient
  // for byte-transparent publication: the written bytes are Data.Bytes, and the
  // segment metadata only needs to cover the image without a writable-executable
  // range. Segment/section-level projection can be enriched later.
  PluginBinarySegment FileSegment;
  FileSegment.Name = "MachO.file";
  FileSegment.Flags =
      NEVERC_BINARY_SEGMENT_READ | NEVERC_BINARY_SEGMENT_EXECUTE;
  FileSegment.Address = 0;
  FileSegment.MemorySize = Bytes.size();
  FileSegment.FileOffset = 0;
  FileSegment.FileSize = Bytes.size();
  FileSegment.Alignment = 1;
  Data.Segments.push_back(std::move(FileSegment));

  Data.ImportCount = Graph->imports().size();
  Data.ExportCount = Graph->exports().size();
  Data.Bytes.assign(Bytes.begin(), Bytes.end());
  Data.FormatVerifier = [](ArrayRef<uint8_t> Candidate) {
    return verifyMachOImage(Candidate);
  };

  auto Image = PluginBinaryImage::import(Task, **IO, Sink, std::move(Data));
  if (!Image)
    return joinErrors(imageError("could not import native Mach-O image"),
                      Image.takeError());

  std::vector<PluginLinkSideOutput> SideOutputs;
  if (!machoConfig()->mapFile.empty()) {
    auto Map = MemoryBuffer::getFile(machoConfig()->mapFile);
    if (Map) {
      const StringRef Contents = (*Map)->getBuffer();
      SideOutputs.push_back(
          {"map", machoConfig()->mapFile.str(),
           std::vector<uint8_t>(Contents.bytes_begin(), Contents.bytes_end())});
    }
  }

  auto Pipeline = LinkOutputPipeline::create(
      Task, Task.processServices().outputCoordinator());
  if (!Pipeline)
    return joinErrors(imageError("could not create output pipeline"),
                      Pipeline.takeError());
  auto Published = (*Pipeline)->execute(std::move(*Image),
                                        machoConfig()->outputFile, SideOutputs);
  if (!Published)
    return joinErrors(imageError("transactional publication failed"),
                      Published.takeError());
  return Error::success();
}

} // namespace linker::macho
