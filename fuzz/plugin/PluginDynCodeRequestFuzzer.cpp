// Fuzzes the dyncode request freeze path.
//
// `freezeDynCodeRequest` normalizes hostile driver options against a resolved
// TargetKey/object format into an immutable, digest-stamped request.  This
// fuzzer drives it with arbitrary option/target bytes and checks two
// invariants: it never crashes on malformed input, and identical inputs always
// produce an identical digest (the header's determinism contract).  It only
// exercises the host builder/verifier -- no native plugin code from fuzz bytes
// is loaded.

#include "PluginFrontendFuzzSupport.h"
#include "Plugin/DynCodeRequest.h"
#include "neverc/Plugin/Host/PluginTargetDescriptor.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <cstdlib>
#include <string>

using namespace llvm;
using namespace neverc::fuzz;
using namespace neverc::plugin;
using namespace neverc::dyncode;

namespace {

void consume(Error E) {
  if (E)
    consumeError(std::move(E));
}

std::string takeText(ByteCursor &Input, size_t Maximum) {
  ArrayRef<uint8_t> Bytes = Input.takeBytes(Maximum);
  if (Bytes.empty())
    return std::string();
  return std::string(reinterpret_cast<const char *>(Bytes.data()),
                     Bytes.size());
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  ByteCursor Input(Data, Size);

  DynCodeOptions Opts;
  Opts.Enabled = true;
  const uint8_t Bits = Input.takeByte();
  Opts.AllBlr = (Bits & 1) != 0;
  Opts.SyscallInlining = (Bits & 2) != 0;
  Opts.WindowsPEBImport = (Bits & 4) != 0;
  Opts.HeapArena = (Bits & 8) != 0;
  Opts.InlineAll = (Bits & 16) != 0;
  Opts.BadByteRewrite = (Bits & 32) != 0;
  Opts.Level =
      (Bits & 64) ? ExecutionLevel::Kernel : ExecutionLevel::User;
  Opts.Target.Level = Opts.Level;
  Opts.EntrySymbol = takeText(Input, 32);
  Opts.BadByteProfile = takeText(Input, 16);
  Opts.Charset = takeText(Input, 16);
  {
    ArrayRef<uint8_t> BadBytes = Input.takeBytes(16);
    Opts.BadBytes.assign(BadBytes.begin(), BadBytes.end());
  }
  Opts.Align = Input.takeU32();
  if (Input.takeByte() & 1)
    Opts.MaxLength = Input.takeU64();
  if (Input.takeByte() & 1)
    Opts.PadByte = static_cast<uint8_t>(Input.takeByte());

  const std::string Text = takeText(Input, 24);
  TargetKeyBuilder Builder;
  Builder.setTargetID({Input.takeU64(), Input.takeU64()})
      .setTriple(Text, Text, Text, Text, Text)
      .setCPU(Text, Text)
      .setFeatures({Text})
      .setABI({Input.takeU64(), Input.takeU64()})
      .setCallingConvention({Input.takeU64(), Input.takeU64()})
      .setObjectFormat({Input.takeU64(), Input.takeU64()})
      .setCodeGeneration(Input.takeU32(), Input.takeU32())
      .setExecution(Input.takeU32(), Input.takeU32(), Input.takeU32())
      .setSchemaDigest(Text);
  auto Key = Builder.build();
  if (!Key) {
    consume(Key.takeError());
    return 0;
  }

  NevercObjectFormatID Format{Input.takeU64(), Input.takeU64()};

  auto First = freezeDynCodeRequest(Opts, *Key, Format);
  if (!First) {
    consume(First.takeError());
    return 0;
  }

  // Determinism contract: the same options/target/format must re-freeze to the
  // same digest.  A mismatch (or a nondeterministic failure) is a real bug.
  auto Second = freezeDynCodeRequest(Opts, *Key, Format);
  if (!Second) {
    consumeError(Second.takeError());
    abort();
  }
  if (First->Digest != Second->Digest)
    abort();
  return 0;
}
