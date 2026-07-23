#include "PluginFrontendFuzzSupport.h"
#include "Link/BinaryImage.h"
#include "neverc/Plugin/Host/LinkPluginInterfaces.h"
#include "neverc/Plugin/Host/PluginIOBridge.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <memory>
#include <string>

using namespace llvm;
using namespace neverc::fuzz;
using namespace neverc::plugin;

namespace {

void consume(Error E) {
  if (E)
    consumeError(std::move(E));
}

// A self-contained process/session that registers the IO interface so the
// bounded MutableBinaryBuilder backing every BinaryImage has a real memory
// OutputSink to stage into.
struct BinaryImageFuzzEnv {
  std::unique_ptr<PluginProcessServices> Services;
  std::unique_ptr<PluginActivationPlan> Plan;
  std::unique_ptr<PluginSession> Session;
  const NevercIOAPI *IO = nullptr;

  ~BinaryImageFuzzEnv() {
    if (Session && !Session->isEnded())
      consume(Session->end());
    Session.reset();
    Plan.reset();
    if (Services)
      consume(Services->shutdown());
  }

  static Expected<std::unique_ptr<BinaryImageFuzzEnv>> create() {
    auto Env = std::make_unique<BinaryImageFuzzEnv>();
    Env->Services = std::make_unique<PluginProcessServices>(
        "neverc-plugin-binary-image-fuzzer", LLVM_VERSION_MAJOR);
    if (Error E = registerPluginIOInterface(*Env->Services))
      return std::move(E);
    if (Error E = registerPluginLinkInterfaces(*Env->Services))
      return std::move(E);
    if (Error E = Env->Services->interfaces().freeze())
      return std::move(E);
    auto CreatedPlan = makePluginActivationPlan(Env->Services->registry(),
                                                ArrayRef<StringRef>());
    if (!CreatedPlan)
      return CreatedPlan.takeError();
    Env->Plan =
        std::make_unique<PluginActivationPlan>(std::move(*CreatedPlan));
    auto CreatedSession =
        PluginSession::create(*Env->Services, *Env->Plan);
    if (!CreatedSession)
      return CreatedSession.takeError();
    Env->Session = std::move(*CreatedSession);
    auto Query = Env->Services->interfaces().query(
        ioPluginInterfaceID(), NEVERC_IO_API_MAJOR, NEVERC_IO_API_MINOR);
    if (!Query)
      return Query.takeError();
    Env->IO = static_cast<const NevercIOAPI *>(Query->Table);
    return Env;
  }
};

BinaryImageFuzzEnv &env() {
  static std::unique_ptr<BinaryImageFuzzEnv> Env = [] {
    auto Created = BinaryImageFuzzEnv::create();
    if (!Created) {
      auto Message = toString(Created.takeError());
      report_fatal_error(StringRef(Message));
    }
    return std::move(*Created);
  }();
  return *Env;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  BinaryImageFuzzEnv &E = env();
  auto CreatedTask = E.Session->createTask(NEVERC_TASK_LINK);
  if (!CreatedTask) {
    consume(CreatedTask.takeError());
    return 0;
  }
  std::unique_ptr<PluginTaskContext> Task = std::move(*CreatedTask);

  ByteCursor Input(Data, Size);
  NevercOutputSinkHandle Sink{};
  static constexpr char Name[] = "binary-image-fuzz";
  NevercStatus Status = E.IO->BeginMemoryOutput(
      E.IO->Context, Task->handle(), {Name, sizeof(Name) - 1},
      UINT64_C(1) << 20, &Sink);
  if (!neverc_status_is_ok(Status)) {
    consume(Task->end());
    return 0;
  }

  PluginBinaryImageData ImageData;
  ImageData.OutputKind = NEVERC_LINK_OUTPUT_EXECUTABLE;
  ImageData.EntryAddress = Input.takeU64();
  ImageData.ImageBase = Input.takeU64();
  ImageData.ImportCount = Input.takeU32();
  ImageData.ExportCount = Input.takeU32();
  ImageData.DynamicRelocationCount = Input.takeU32();

  const unsigned SegmentCount = Input.takeByte() % 6;
  for (unsigned I = 0; I != SegmentCount; ++I) {
    PluginBinarySegment Segment;
    Segment.Name = "seg." + std::to_string(Input.takeU32());
    Segment.Flags = Input.takeU32();
    Segment.Address = Input.takeU64();
    Segment.MemorySize = Input.takeU64();
    Segment.FileOffset = Input.takeU64();
    Segment.FileSize = Input.takeU64();
    Segment.Alignment = UINT64_C(1) << (Input.takeByte() % 16);
    ImageData.Segments.push_back(std::move(Segment));
  }

  const unsigned SectionCount = Input.takeByte() % 8;
  for (unsigned I = 0; I != SectionCount; ++I) {
    PluginBinarySection Section;
    Section.Name = "sec." + std::to_string(Input.takeU32());
    if (!ImageData.Segments.empty())
      Section.SegmentIndex = Input.takeByte() % ImageData.Segments.size();
    Section.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
    Section.Flags = Input.takeU32();
    Section.Address = Input.takeU64();
    Section.MemorySize = Input.takeU64();
    Section.FileOffset = Input.takeU64();
    Section.FileSize = Input.takeU64();
    Section.Alignment = UINT64_C(1) << (Input.takeByte() % 16);
    ImageData.Sections.push_back(std::move(Section));
  }

  const unsigned DirectoryCount = Input.takeByte() % 6;
  for (unsigned I = 0; I != DirectoryCount; ++I) {
    PluginBinaryDirectory Directory;
    Directory.Kind = "dir." + std::to_string(Input.takeU32());
    Directory.Offset = Input.takeU64();
    Directory.Size = Input.takeU64();
    ImageData.Directories.push_back(std::move(Directory));
  }

  ArrayRef<uint8_t> Bytes = Input.takeBytes(4096);
  ImageData.Bytes.assign(Bytes.begin(), Bytes.end());

  auto Image =
      PluginBinaryImage::import(*Task, *E.IO, Sink, std::move(ImageData));
  if (!Image) {
    consume(Image.takeError());
    consume(Task->end());
    return 0;
  }
  consume((*Image)->verify());
  consume((*Image)->abort());
  consume(Task->end());
  return 0;
}
