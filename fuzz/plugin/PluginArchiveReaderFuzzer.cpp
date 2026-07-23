#include "PluginFrontendFuzzSupport.h"
#include "Link/LinkInputReader.h"
#include "Link/LinkRequest.h"
#include "Link/LinkerScriptProvider.h"
#include "neverc/Plugin/Host/ObjectReaderProvider.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <cstdint>
#include <memory>
#include <vector>

using namespace llvm;
using namespace neverc::fuzz;
using namespace neverc::plugin;

namespace {

void consume(Error E) {
  if (E)
    consumeError(std::move(E));
}

Expected<OwnedTargetKey> targetKey(NevercObjectFormatID FormatID) {
  TargetKeyBuilder Builder;
  return Builder
      .setTargetID({UINT64_C(0x66757a7a61726368), 1})
      .setTriple("x86_64-unknown-linux-gnu", "x86_64", "unknown", "linux",
                 "gnu")
      .setCPU("generic", "")
      .setFeatures({})
      .setABI({UINT64_C(0x66757a7a61626900), 1})
      .setCallingConvention({UINT64_C(0x66757a7a63630000), 1})
      .setObjectFormat(FormatID)
      .setCodeGeneration(NEVERC_TARGET_RELOCATION_STATIC,
                         NEVERC_TARGET_CODE_MODEL_SMALL)
      .setExecution(NEVERC_TARGET_EXECUTION_USER, 64,
                    NEVERC_TARGET_ENDIAN_LITTLE)
      .setSchemaDigest("0123456789abcdef0123456789abcdef"
                       "0123456789abcdef0123456789abcdef")
      .build();
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  auto CreatedTask =
      pluginFuzzRuntime().session().createTask(NEVERC_TASK_LINK);
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
  if (!Reader) {
    consume(Reader.takeError());
    consume(Task->end());
    return 0;
  }

  const auto Formats = (*Reader)->registry().formats();
  const NevercObjectFormatID FormatID =
      Formats.empty()
          ? NevercObjectFormatID{UINT64_C(0x66757a7a6f626a00), 1}
          : Formats.front().ID;
  auto Key = targetKey(FormatID);
  if (!Key) {
    consume(Key.takeError());
    consume(Task->end());
    return 0;
  }

  // Prefix the well-known archive magic so the input classifier routes the
  // fuzzed bytes into the archive index/member parser instead of rejecting
  // them on a format-probe mismatch.
  std::vector<uint8_t> Blob;
  static constexpr char Magic[] = "!<arch>\n";
  Blob.insert(Blob.end(), Magic, Magic + 8);
  Blob.insert(Blob.end(), Data, Data + Size);

  LinkRequestData RequestData;
  RequestData.Task = Task->handle();
  RequestData.Target = std::move(*Key);
  RequestData.InputFormat = FormatID;
  RequestData.OutputKind = NEVERC_LINK_OUTPUT_EXECUTABLE;
  OwnedRawLinkInput Input;
  Input.Kind = NEVERC_LINK_INPUT_ARCHIVE;
  Input.LogicalURI = "fuzz.a";
  Input.AuthorizedBlob = std::move(Blob);
  RequestData.Inputs.push_back(std::move(Input));

  auto Request = LinkRequest::create(std::move(RequestData));
  if (!Request) {
    consume(Request.takeError());
    consume(Task->end());
    return 0;
  }

  IntrusiveRefCntPtr<vfs::InMemoryFileSystem> FileSystem(
      new vfs::InMemoryFileSystem());
  LinkInputReader InputReader(*Task, *FileSystem, **Reader,
                              LinkInputReaderOptions{},
                              &builtinLinkerScriptProvider());
  auto InputSet = InputReader.read(**Request);
  if (!InputSet)
    consume(InputSet.takeError());
  consume(Task->end());
  return 0;
}
