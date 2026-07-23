#include "LTO/LTOInputSet.h"
#include "Link/LinkGraph.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include <cstdint>
#include <memory>

using namespace llvm;
using namespace neverc::plugin;

namespace {

void consume(Error E) {
  if (E)
    consumeError(std::move(E));
}

Expected<OwnedTargetKey> targetKey() {
  TargetKeyBuilder Builder;
  return Builder
      .setTargetID({UINT64_C(0x66757a7a6c746f00), 1})
      .setTriple("x86_64-unknown-linux-gnu", "x86_64", "unknown", "linux",
                 "gnu")
      .setCPU("generic", "")
      .setFeatures({})
      .setABI({UINT64_C(0x66757a7a61626900), 1})
      .setCallingConvention({UINT64_C(0x66757a7a63630000), 1})
      .setObjectFormat({UINT64_C(0x66757a7a6f626a00), 1})
      .setCodeGeneration(NEVERC_TARGET_RELOCATION_PIC,
                         NEVERC_TARGET_CODE_MODEL_SMALL)
      .setExecution(NEVERC_TARGET_EXECUTION_USER, 64,
                    NEVERC_TARGET_ENDIAN_LITTLE)
      .setSchemaDigest("0123456789abcdef0123456789abcdef"
                       "0123456789abcdef0123456789abcdef")
      .build();
}

} // namespace

// Drives the LTO bitcode-module inspection path that produces the symbol and
// summary records the resolution table is later built from.  Arbitrary bytes
// exercise LLVM's bitcode reader and the host's provenance/target checks.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  auto Key = targetKey();
  if (!Key) {
    consume(Key.takeError());
    return 0;
  }

  PluginLinkBitcodeModule Module;
  Module.ID = 1;
  Module.InputID = 1;
  Module.Name = "fuzz.bc";
  Module.ModuleIdentifier = "fuzz";
  Module.Origin.InputID = 1;

  StringRef Bytes(reinterpret_cast<const char *>(Data), Size);
  std::unique_ptr<MemoryBuffer> Buffer = MemoryBuffer::getMemBuffer(
      Bytes, "fuzz.bc", /*RequiresNullTerminator=*/false);
  consume(inspectPluginBitcodeModule(Module, Buffer->getMemBufferRef(),
                                     Key->view()));
  return 0;
}
