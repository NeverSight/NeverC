#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCComponentProvider.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "gtest/gtest.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace llvm;

namespace {

std::string errorText(Error E) {
  return toString(std::move(E)).str().str();
}

void initializeTargets() {
  static std::once_flag Once;
  std::call_once(Once, [] {
    InitializeAllTargetInfos();
    InitializeAllTargetMCs();
    InitializeAllTargets();
    InitializeAllAsmPrinters();
  });
}

struct ProviderStats {
  std::atomic<unsigned> Printers{0};
  std::atomic<unsigned> Emitters{0};
  std::atomic<unsigned> Backends{0};
  std::atomic<unsigned> Writers{0};
  std::atomic<bool> WrongContext{false};
};

class RecordingProvider final : public MCComponentProvider {
public:
  RecordingProvider(ProviderStats &StatsValue,
                    MCContext &ExpectedContextValue,
                    bool FailEmitterValue)
      : Stats(StatsValue), ExpectedContext(ExpectedContextValue),
        FailEmitter(FailEmitterValue) {}

  Expected<std::unique_ptr<MCInstPrinter>>
  provideInstPrinter(
      MCContext &Context,
      std::unique_ptr<MCInstPrinter> Fallback) override {
    observe(Context, Stats.Printers);
    return std::move(Fallback);
  }

  Expected<std::unique_ptr<MCCodeEmitter>>
  provideCodeEmitter(
      MCContext &Context,
      std::unique_ptr<MCCodeEmitter> Fallback) override {
    observe(Context, Stats.Emitters);
    if (FailEmitter)
      return createStringError(inconvertibleErrorCode(),
                               "test component emitter failure");
    return std::move(Fallback);
  }

  Expected<std::unique_ptr<MCAsmBackend>>
  provideAsmBackend(
      MCContext &Context,
      std::unique_ptr<MCAsmBackend> Fallback) override {
    observe(Context, Stats.Backends);
    return std::move(Fallback);
  }

  Expected<std::unique_ptr<MCObjectWriter>>
  provideObjectWriter(
      MCContext &Context,
      std::unique_ptr<MCObjectWriter> Fallback) override {
    observe(Context, Stats.Writers);
    return std::move(Fallback);
  }

private:
  void observe(MCContext &Context,
               std::atomic<unsigned> &Counter) {
    ++Counter;
    if (&Context != &ExpectedContext)
      Stats.WrongContext = true;
  }

  ProviderStats &Stats;
  MCContext &ExpectedContext;
  bool FailEmitter = false;
};

Expected<std::vector<char>>
emitTestObject(ProviderStats *Stats = nullptr,
               bool FailEmitter = false) {
  initializeTargets();
  Triple TargetTriple("x86_64-unknown-linux-gnu");
  std::string LookupError;
  const Target *TheTarget =
      TargetRegistry::lookupTarget(TargetTriple.str(), LookupError);
  if (!TheTarget)
    return createStringError(inconvertibleErrorCode(), LookupError);
  TargetOptions Options;
  std::unique_ptr<TargetMachine> TM(TheTarget->createTargetMachine(
      TargetTriple.str(), "generic", "", Options, std::nullopt,
      CodeGenOptLevel::None));
  if (!TM)
    return createStringError(inconvertibleErrorCode(),
                             "failed to create test target machine");
  auto *LLVMTarget = static_cast<LLVMTargetMachine *>(TM.get());
  MCContext Context(TargetTriple, TM->getMCAsmInfo(),
                    TM->getMCRegisterInfo(),
                    TM->getMCSubtargetInfo());
  MCObjectFileInfo ObjectInfo;
  ObjectInfo.initMCObjectFileInfo(Context);
  Context.setObjectFileInfo(&ObjectInfo);
  std::optional<RecordingProvider> Provider;
  if (Stats) {
    Provider.emplace(*Stats, Context, FailEmitter);
    Context.setComponentProvider(&*Provider);
  }

  SmallVector<char, 0> Storage;
  raw_svector_ostream Output(Storage);
  auto Stream = LLVMTarget->createMCStreamer(
      Output, nullptr, CodeGenFileType::ObjectFile, Context);
  if (!Stream)
    return Stream.takeError();
  (*Stream)->initSections(false, *TM->getMCSubtargetInfo());
  (*Stream)->emitBytes(StringRef("\x7f", 1));
  (*Stream)->finish();
  if (Context.hadError())
    return createStringError(inconvertibleErrorCode(),
                             "test MC context reported an error");
  return std::vector<char>(Storage.begin(), Storage.end());
}

TEST(PluginMCComponentProviderTest,
     PassThroughProviderPreservesBuiltinObjectBytes) {
  auto Builtin = emitTestObject();
  ASSERT_TRUE(static_cast<bool>(Builtin))
      << errorText(Builtin.takeError());
  ProviderStats Stats;
  auto Provided = emitTestObject(&Stats);
  ASSERT_TRUE(static_cast<bool>(Provided))
      << errorText(Provided.takeError());
  EXPECT_EQ(*Provided, *Builtin);
  EXPECT_EQ(Stats.Printers, 0U);
  EXPECT_EQ(Stats.Emitters, 1U);
  EXPECT_EQ(Stats.Backends, 1U);
  EXPECT_EQ(Stats.Writers, 1U);
  EXPECT_FALSE(Stats.WrongContext);
}

TEST(PluginMCComponentProviderTest,
     ProviderCreationFailureDoesNotFallBack) {
  ProviderStats Stats;
  auto Result = emitTestObject(&Stats, true);
  ASSERT_FALSE(static_cast<bool>(Result));
  EXPECT_NE(errorText(Result.takeError()).find(
                "test component emitter failure"),
            std::string::npos);
  EXPECT_EQ(Stats.Emitters, 1U);
  EXPECT_EQ(Stats.Backends, 0U);
  EXPECT_EQ(Stats.Writers, 0U);
}

TEST(PluginMCComponentProviderTest,
     ParallelContextsKeepProvidersIsolated) {
  ProviderStats FirstStats;
  ProviderStats SecondStats;
  std::optional<std::string> FirstError;
  std::optional<std::string> SecondError;
  std::thread First([&] {
    auto Result = emitTestObject(&FirstStats);
    if (!Result)
      FirstError = errorText(Result.takeError());
  });
  std::thread Second([&] {
    auto Result = emitTestObject(&SecondStats);
    if (!Result)
      SecondError = errorText(Result.takeError());
  });
  First.join();
  Second.join();

  EXPECT_FALSE(FirstError) << FirstError.value_or("");
  EXPECT_FALSE(SecondError) << SecondError.value_or("");
  EXPECT_EQ(FirstStats.Emitters, 1U);
  EXPECT_EQ(FirstStats.Backends, 1U);
  EXPECT_EQ(FirstStats.Writers, 1U);
  EXPECT_FALSE(FirstStats.WrongContext);
  EXPECT_EQ(SecondStats.Emitters, 1U);
  EXPECT_EQ(SecondStats.Backends, 1U);
  EXPECT_EQ(SecondStats.Writers, 1U);
  EXPECT_FALSE(SecondStats.WrongContext);
}

} // namespace
