#include "PluginFrontendFuzzSupport.h"
#include "neverc/Plugin/Host/ObjectReaderProvider.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <memory>

using namespace llvm;
using namespace neverc::fuzz;
using namespace neverc::plugin;

namespace {

void consume(Error E) {
  if (E)
    consumeError(std::move(E));
}

Expected<OwnedTargetKey>
targetKey(NevercObjectFormatID FormatID) {
  TargetKeyBuilder Builder;
  return Builder
      .setTargetID({UINT64_C(0x66757a7a72656164), 1})
      .setTriple("x86_64-unknown-linux-gnu", "x86_64", "unknown",
                 "linux", "gnu")
      .setCPU("generic", "")
      .setFeatures({})
      .setABI({UINT64_C(0x66757a7a61626900), 2})
      .setCallingConvention({UINT64_C(0x66757a7a63630000), 2})
      .setObjectFormat(FormatID)
      .setCodeGeneration(NEVERC_TARGET_RELOCATION_STATIC,
                         NEVERC_TARGET_CODE_MODEL_SMALL)
      .setExecution(NEVERC_TARGET_EXECUTION_USER, 64,
                    NEVERC_TARGET_ENDIAN_LITTLE)
      .setSchemaDigest(
          "0123456789abcdef0123456789abcdef"
          "0123456789abcdef0123456789abcdef")
      .build();
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  auto CreatedTask =
      pluginFuzzRuntime().session().createTask(NEVERC_TASK_CODEGEN);
  if (!CreatedTask) {
    consume(CreatedTask.takeError());
    return 0;
  }
  std::unique_ptr<PluginTaskContext> Task = std::move(*CreatedTask);

  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  if (!Snapshot) {
    consume(Snapshot.takeError());
    consume(Task->end());
    return 0;
  }
  auto Reader = ObjectReaderProvider::create(*Snapshot);
  if (!Reader || (*Reader)->registry().formats().empty()) {
    if (!Reader)
      consume(Reader.takeError());
    consume(Task->end());
    return 0;
  }

  const auto Formats = (*Reader)->registry().formats();
  const size_t FormatIndex = Size == 0 ? 0 : Data[0] % Formats.size();
  auto Key = targetKey(Formats[FormatIndex].ID);
  if (!Key) {
    consume(Key.takeError());
    consume(Task->end());
    return 0;
  }
  ArrayRef<uint8_t> Bytes(Data, Size);
  auto Graph = (*Reader)->read(
      *Task, Bytes, "fuzz-object-input", *Key,
      Formats[FormatIndex].ID);
  if (!Graph)
    consume(Graph.takeError());
  consume(Task->end());
  return 0;
}
