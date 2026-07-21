#ifndef NEVERC_PLUGIN_LINK_BINARYIMAGE_H
#define NEVERC_PLUGIN_LINK_BINARYIMAGE_H

#include "LinkGraph.h"
#include "neverc/Plugin/Host/MutableBinaryBuilder.h"
#include "neverc/Plugin/PluginLink.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Error.h"
#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace neverc::plugin {

class PluginTaskContext;

struct PluginBinarySegment {
  NevercBinarySegmentHandle Handle{};
  std::string Name;
  NevercBinarySegmentFlags Flags = 0;
  uint64_t Address = 0;
  uint64_t MemorySize = 0;
  uint64_t FileOffset = 0;
  uint64_t FileSize = 0;
  uint64_t Alignment = 1;
};

struct PluginBinarySection {
  NevercBinarySectionHandle Handle{};
  NevercBinarySegmentHandle Segment{};
  size_t SegmentIndex = std::numeric_limits<size_t>::max();
  std::string Name;
  NevercObjectSectionKind Kind = NEVERC_OBJECT_SECTION_KIND_DATA;
  NevercObjectSectionFlags Flags = 0;
  uint64_t Address = 0;
  uint64_t MemorySize = 0;
  uint64_t FileOffset = 0;
  uint64_t FileSize = 0;
  uint64_t Alignment = 1;
};

struct PluginBinaryDirectory {
  std::string Kind;
  uint64_t Offset = 0;
  uint64_t Size = 0;
};

struct PluginBinaryImageData {
  NevercLinkOutputKind OutputKind = NEVERC_LINK_OUTPUT_EXECUTABLE;
  NevercTargetID TargetID{};
  NevercObjectFormatID FormatID{};
  uint64_t EntryAddress = 0;
  uint64_t ImageBase = 0;
  uint64_t ImportCount = 0;
  uint64_t ExportCount = 0;
  uint64_t DynamicRelocationCount = 0;
  std::vector<PluginBinarySegment> Segments;
  std::vector<PluginBinarySection> Sections;
  std::vector<PluginBinaryDirectory> Directories;
  std::vector<uint8_t> Bytes;
  std::function<llvm::Error(llvm::ArrayRef<uint8_t>)> FormatVerifier;
};

class PluginBinaryImage {
public:
  static llvm::Expected<std::shared_ptr<PluginBinaryImage>>
  import(PluginTaskContext &Task, const NevercIOAPI &IO,
         NevercOutputSinkHandle Sink, PluginBinaryImageData Data);

  static llvm::Expected<std::shared_ptr<PluginBinaryImage>>
  emit(PluginTaskContext &Task, const NevercIOAPI &IO,
       NevercOutputSinkHandle Sink, const PluginLinkGraph &Graph,
       NevercLinkOutputKind OutputKind =
           NEVERC_LINK_OUTPUT_EXECUTABLE);

  ~PluginBinaryImage();

  PluginBinaryImage(const PluginBinaryImage &) = delete;
  PluginBinaryImage &operator=(const PluginBinaryImage &) = delete;

  NevercBinaryImageHandle handle() const { return Handle; }
  NevercTaskHandle taskHandle() const;
  NevercBinaryImageState state() const { return State; }
  NevercLinkOutputKind outputKind() const { return OutputKind; }
  NevercTargetID targetID() const { return TargetID; }
  NevercObjectFormatID formatID() const { return FormatID; }
  uint64_t entryAddress() const { return EntryAddress; }
  uint64_t imageBase() const { return ImageBase; }
  uint64_t importCount() const { return ImportCount; }
  uint64_t exportCount() const { return ExportCount; }
  uint64_t dynamicRelocationCount() const {
    return DynamicRelocationCount;
  }
  llvm::ArrayRef<PluginBinarySegment> segments() const {
    return Segments;
  }
  llvm::ArrayRef<PluginBinarySection> sections() const {
    return Sections;
  }
  llvm::ArrayRef<PluginBinaryDirectory> directories() const {
    return Directories;
  }
  llvm::ArrayRef<uint8_t> bytes() const { return Builder->bytes(); }
  const NevercMutableBinaryAPI &binaryAPI() const {
    return Builder->api();
  }
  const NevercLinkAPI &linkAPI() const { return API; }
  NevercMutableBinaryBuilderHandle builderHandle() const {
    return Builder->handle();
  }
  std::array<uint8_t, 32> contentDigest() const;

  llvm::Error verify();
  void markCommitted();
  void markFailedPartial();
  llvm::Error abort();

private:
  PluginBinaryImage(PluginTaskContext &Task,
                    NevercLinkOutputKind OutputKind,
                    NevercTargetID TargetID,
                    NevercObjectFormatID FormatID,
                    uint64_t EntryAddress, uint64_t ImageBase,
                    uint64_t ImportCount, uint64_t ExportCount,
                    uint64_t DynamicRelocationCount,
                    std::vector<PluginBinarySegment> Segments,
                    std::vector<PluginBinarySection> Sections,
                    std::vector<PluginBinaryDirectory> Directories,
                    std::function<llvm::Error(
                        llvm::ArrayRef<uint8_t>)> FormatVerifier,
                    std::unique_ptr<MutableBinaryBuilder> Builder);
  llvm::Error initializeHandles();

  PluginTaskContext &Task;
  NevercLinkOutputKind OutputKind;
  NevercTargetID TargetID{};
  NevercObjectFormatID FormatID{};
  uint64_t EntryAddress = 0;
  uint64_t ImageBase = 0;
  uint64_t ImportCount = 0;
  uint64_t ExportCount = 0;
  uint64_t DynamicRelocationCount = 0;
  std::vector<PluginBinarySegment> Segments;
  std::vector<PluginBinarySection> Sections;
  std::vector<PluginBinaryDirectory> Directories;
  std::function<llvm::Error(llvm::ArrayRef<uint8_t>)> FormatVerifier;
  std::unique_ptr<MutableBinaryBuilder> Builder;
  NevercBinaryImageHandle Handle{};
  NevercLinkAPI API{};
  NevercBinaryImageState State = NEVERC_BINARY_IMAGE_CANDIDATE;
};

llvm::Error verifyBinaryImage(const PluginBinaryImage &Image);
void initializeBinaryImageAPI(NevercLinkAPI &API,
                              PluginBinaryImage &Image);

} // namespace neverc::plugin

#endif
