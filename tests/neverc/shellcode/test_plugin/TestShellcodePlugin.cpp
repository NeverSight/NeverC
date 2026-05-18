//===- TestShellcodePlugin.cpp -- e2e-only shellcode hook/plug-in driver
//---===//
//
// This file is intentionally compiled into `nevercShellcode` (see
// `neverc/lib/Shellcode/CMakeLists.txt`) so the standard `bin/neverc` test
// runner can observe the hook/plug-in APIs without any dynamic loader
// plumbing.
//
// IMPORTANT:
// - The plugin is INERT by default.
// - It only registers hooks/strategies when the environment variable
//   `NEVERC_ENABLE_SHELLCODE_TEST_PLUGIN=1` is present in the driver process.
//
// Keeping the implementation under `tests/` ensures it stays clearly
// test-only and can evolve alongside the shellcode test suite.
//
//===----------------------------------------------------------------------===//

#include "neverc/Shellcode/Pipeline/Pipeline.h"
#include "neverc/Shellcode/Pipeline/Plugin.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"

#include <cstdint>
#include <cstdlib>

using namespace neverc::shellcode;

namespace {

extern "C" void neverc_shellcode_test_plugin_link_anchor() {}

bool enabled() {
  const char *V = std::getenv("NEVERC_ENABLE_SHELLCODE_TEST_PLUGIN");
  return V && V[0] == '1' && V[1] == '\0';
}

void registerNoopCharset() {
  CharsetEncoderEntry E;
  E.Name = "noop";
  E.Description = "Test-only charset encoder: identity transform, empty stub.";
  E.Encode = [](llvm::ArrayRef<uint8_t> Raw, const TargetDesc &) {
    llvm::SmallVector<uint8_t, 256> Out;
    Out.append(Raw.begin(), Raw.end());
    return Out;
  };
  E.Stub = [](const TargetDesc &) { return llvm::SmallVector<uint8_t, 256>(); };
  E.IsCharsetMember = [](uint8_t) { return true; };
  registerCharsetEncoder(std::move(E));
}

void registerBadByteRewrite41() {
  registerBadByteRewriteStrategy([](BadByteRewriteContext &Ctx) {
    if (!Ctx.Bytes)
      return BadByteRewriteResult::Error;
    // Only do work when 0x41 is listed as forbidden.
    bool Wants = false;
    for (uint8_t B : Ctx.BadBytes)
      if (B == 0x41)
        Wants = true;
    if (!Wants)
      return BadByteRewriteResult::NotApplied;

    bool Changed = false;
    for (uint8_t &Byte : *Ctx.Bytes) {
      if (Byte == 0x41) {
        Byte = 0x43;
        Changed = true;
      }
    }
    return Changed ? BadByteRewriteResult::Applied
                   : BadByteRewriteResult::NotApplied;
  });
}

void mergeHooks(ObfuscationHooks &H) {
  // Pre-finalize byte hook: append one byte 0x41. The test suite uses
  // -fshellcode-bad-bytes=41 to prove the bad-byte rewriter runs AFTER this.
  auto OldPostExtract = std::move(H.RunPostExtract);
  H.RunPostExtract = [OldPostExtract](llvm::SmallVectorImpl<uint8_t> &Bytes,
                                      const ShellcodeOptions &Opts) {
    if (OldPostExtract)
      OldPostExtract(Bytes, Opts);
    Bytes.push_back(0x41);
  };

  // Post-finalize byte hook: append one byte 0x42. The test suite checks the
  // tail byte to prove this fires AFTER audit + sizing.
  auto OldPostFinalize = std::move(H.RunPostFinalize);
  H.RunPostFinalize = [OldPostFinalize](llvm::SmallVectorImpl<uint8_t> &Bytes,
                                        const ShellcodeOptions &Opts) {
    if (OldPostFinalize)
      OldPostFinalize(Bytes, Opts);
    Bytes.push_back(0x42);
  };
}

struct Init {
  Init() {
    if (!enabled())
      return;
    registerNoopCharset();
    registerBadByteRewrite41();

    ObfuscationHooks H = getShellcodeObfuscationHooks();
    mergeHooks(H);
    setShellcodeObfuscationHooks(std::move(H));
  }
} init;

} // namespace
