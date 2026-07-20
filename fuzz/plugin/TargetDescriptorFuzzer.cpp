#include "PluginFrontendFuzzSupport.h"
#include "neverc/Plugin/Host/PluginTargetDescriptor.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <string>

using namespace llvm;
using namespace neverc::fuzz;
using namespace neverc::plugin;

namespace {

NevercStringView view(const std::string &Value) {
  return {Value.data(), Value.size()};
}

void consume(Error E) {
  if (E)
    consumeError(std::move(E));
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  ByteCursor Input(Data, Size);
  ArrayRef<uint8_t> TextBytes = Input.takeBytes(128);
  std::string Text;
  if (!TextBytes.empty())
    Text.assign(reinterpret_cast<const char *>(TextBytes.data()),
                TextBytes.size());

  NevercTargetMachineDescriptor Descriptor{};
  Descriptor.Header = {
      sizeof(Descriptor), NEVERC_TARGET_API_MAJOR,
      static_cast<uint16_t>(Input.takeByte()), Input.takeU32()};
  Descriptor.RawTriple = view(Text);
  Descriptor.Architecture = view(Text);
  Descriptor.Vendor = view(Text);
  Descriptor.OperatingSystem = view(Text);
  Descriptor.Environment = view(Text);
  Descriptor.DataLayout = view(Text);
  Descriptor.DefaultCPU = view(Text);
  Descriptor.TuneCPU = view(Text);
  Descriptor.SchemaDigest = view(Text);
  Descriptor.SupportedRelocationModels = Input.takeU64();
  Descriptor.SupportedCodeModels = Input.takeU64();
  Descriptor.DefaultRelocationModel = Input.takeU32();
  Descriptor.DefaultCodeModel = Input.takeU32();
  Descriptor.ExceptionModel = Input.takeU32();
  Descriptor.UnwindModel = Input.takeU32();
  Descriptor.Endianness = Input.takeU32();
  Descriptor.PointerWidth = Input.takeU32();
  Descriptor.IntWidth = Input.takeU32();
  Descriptor.LongWidth = Input.takeU32();
  Descriptor.LongLongWidth = Input.takeU32();
  Descriptor.StackAlignment = Input.takeU32();
  Descriptor.MaximumAtomicWidth = Input.takeU32();
  Descriptor.MaximumVectorAlignment = Input.takeU32();
  Descriptor.BuiltinVaListKind = Input.takeU32();
  Descriptor.ExecutionLevels = Input.takeU32();
  Descriptor.DefaultExecutionLevel = Input.takeU32();
  Descriptor.TLSSupported =
      static_cast<NevercBool>(Input.takeByte());

  auto Verified = verifyTargetMachineDescriptor(Descriptor);
  if (!Verified)
    consume(Verified.takeError());

  TargetKeyBuilder Builder;
  Builder
      .setTargetID({Input.takeU64(), Input.takeU64()})
      .setTriple(Text, Text, Text, Text, Text)
      .setCPU(Text, Text)
      .setFeatures({Text})
      .setABI({Input.takeU64(), Input.takeU64()})
      .setCallingConvention({Input.takeU64(), Input.takeU64()})
      .setObjectFormat({Input.takeU64(), Input.takeU64()})
      .setCodeGeneration(Input.takeU32(), Input.takeU32())
      .setExecution(Input.takeU32(), Input.takeU32(),
                    Input.takeU32())
      .setSchemaDigest(Text);
  auto Key = Builder.build();
  if (!Key)
    consume(Key.takeError());
  return 0;
}
