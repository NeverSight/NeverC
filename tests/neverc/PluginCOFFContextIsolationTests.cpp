#include "Driver/InputWorkload.h"
#include "Linker/COFF/COFFLinkerContext.h"
#include "Linker/Core/Driver/Dispatcher.h"
#include "Linker/Core/Runtime/CrashRecovery.h"
#include "Linker/Core/Runtime/LinkerExecutionContext.h"
#include "ProcessResourceBrokerInternal.h"
#include "neverc/Foundation/Core/ProcessResourceBroker.h"
#include "neverc/Invoke/InMemoryFileStore.h"
#include "neverc/Plugin/Host/BuiltinLLVMAsmParser.h"
#include "neverc/Plugin/Host/BuiltinTargetProvider.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Object/WindowsResource.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

using namespace linker;
using namespace linker::coff;

LINKER_HAS_DRIVER(coff)

namespace {

void initializeAssemblyTargets() {
  static std::once_flag Once;
  std::call_once(Once, [] {
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
  });
}

bool successfulTraceBackend(llvm::ArrayRef<const char *>, llvm::raw_ostream &,
                            llvm::raw_ostream &, bool, bool,
                            const LinkerDriverConfig &) {
  return true;
}

bool fatalTraceBackend(llvm::ArrayRef<const char *>, llvm::raw_ostream &,
                       llvm::raw_ostream &, bool, bool,
                       const LinkerDriverConfig &Config) {
  linker::crash_recovery_detail::CrashRecoveryLocalOwner<LinkerExecutionContext>
      ExecutionOwner(Config.executionContext);
  ExecutionOwner.get().createBackend<CommonLinkerContext>();
  llvm::CrashRecoveryContext *Context =
      llvm::CrashRecoveryContext::GetCurrent();
  if (!Context)
    return false;
  Context->HandleExit(1);
}

std::optional<size_t> findPEChecksumOffset(llvm::StringRef Image) {
  const auto *Bytes = reinterpret_cast<const uint8_t *>(Image.data());
  if (Image.size() < 0x40 || Bytes[0] != 'M' || Bytes[1] != 'Z')
    return std::nullopt;

  const size_t PEOffset = llvm::support::endian::read32le(Bytes + 0x3c);
  if (PEOffset > Image.size() || Image.size() - PEOffset < 24 ||
      Bytes[PEOffset] != 'P' || Bytes[PEOffset + 1] != 'E' ||
      Bytes[PEOffset + 2] != 0 || Bytes[PEOffset + 3] != 0)
    return std::nullopt;

  const uint16_t OptionalHeaderSize =
      llvm::support::endian::read16le(Bytes + PEOffset + 20);
  const size_t OptionalHeaderOffset = PEOffset + 24;
  if (OptionalHeaderSize < 68 || OptionalHeaderOffset > Image.size() ||
      Image.size() - OptionalHeaderOffset < OptionalHeaderSize)
    return std::nullopt;

  const uint16_t Magic =
      llvm::support::endian::read16le(Bytes + OptionalHeaderOffset);
  if (Magic != 0x10b && Magic != 0x20b)
    return std::nullopt;
  return OptionalHeaderOffset + 64;
}

uint32_t recomputePEChecksum(llvm::StringRef Image, size_t ChecksumOffset) {
  const auto *Bytes = reinterpret_cast<const uint8_t *>(Image.data());
  uint64_t Sum = 0;
  for (size_t I = 0; I + 1 < Image.size(); I += 2) {
    if (I != ChecksumOffset && I != ChecksumOffset + 2)
      Sum += llvm::support::endian::read16le(Bytes + I);
    Sum = (Sum & 0xffff) + (Sum >> 16);
  }
  if ((Image.size() & 1) != 0) {
    Sum += Bytes[Image.size() - 1];
    Sum = (Sum & 0xffff) + (Sum >> 16);
  }
  Sum = (Sum & 0xffff) + (Sum >> 16);
  return static_cast<uint32_t>(Sum) + static_cast<uint32_t>(Image.size());
}

llvm::SmallVector<char, 0> createWindowsResource(size_t PayloadSize) {
  const size_t ResourceSize = llvm::alignTo(
      llvm::object::WIN_RES_MAGIC_SIZE + llvm::object::WIN_RES_NULL_ENTRY_SIZE +
          sizeof(llvm::object::WinResHeaderPrefix) +
          sizeof(llvm::object::WinResIDs) +
          sizeof(llvm::object::WinResHeaderSuffix) + PayloadSize,
      llvm::object::WIN_RES_DATA_ALIGNMENT);
  llvm::SmallVector<char, 0> Resource(ResourceSize, 0);
  char *Cursor = Resource.data();

  std::memcpy(Cursor, llvm::COFF::WinResMagic, sizeof(llvm::COFF::WinResMagic));
  Cursor += sizeof(llvm::COFF::WinResMagic);
  Cursor += llvm::object::WIN_RES_NULL_ENTRY_SIZE;

  auto *Prefix = reinterpret_cast<llvm::object::WinResHeaderPrefix *>(Cursor);
  Prefix->DataSize = PayloadSize;
  Prefix->HeaderSize = sizeof(llvm::object::WinResHeaderPrefix) +
                       sizeof(llvm::object::WinResIDs) +
                       sizeof(llvm::object::WinResHeaderSuffix);
  Cursor += sizeof(llvm::object::WinResHeaderPrefix);

  auto *IDs = reinterpret_cast<llvm::object::WinResIDs *>(Cursor);
  IDs->setType(/*RCDATA=*/10);
  IDs->setName(/*ID=*/1);
  Cursor += sizeof(llvm::object::WinResIDs);

  auto *Suffix = reinterpret_cast<llvm::object::WinResHeaderSuffix *>(Cursor);
  Suffix->MemoryFlags = llvm::object::WIN_RES_PURE_MOVEABLE;
  Suffix->Language = 0x0409;
  Cursor += sizeof(llvm::object::WinResHeaderSuffix);
  std::fill_n(Cursor, PayloadSize, '\x5a');
  return Resource;
}

} // namespace

TEST(PluginCOFFContextIsolationTest, ConcurrentStateDoesNotCrossContexts) {
  std::atomic<unsigned> Ready{0};
  std::atomic<bool> Failed{false};

  auto Run = [&](llvm::COFF::MachineTypes Machine, llvm::StringRef Name) {
    {
      LinkerExecutionContext Execution;
      COFFLinkerContext &Context = Execution.createBackend<COFFLinkerContext>();
      Context.config.machine = Machine;
      Context.config.outputFile = Name.str();
      Context.overrideSymbols.try_emplace(Name, nullptr);

      Ready.fetch_add(1, std::memory_order_release);
      while (Ready.load(std::memory_order_acquire) != 2)
        std::this_thread::yield();

      if (currentLinkerContext() != &Context ||
          Context.config.machine != Machine ||
          Context.config.outputFile != Name ||
          Context.overrideSymbols.size() != 1 ||
          Context.overrideSymbols.count(Name) != 1)
        Failed.store(true, std::memory_order_relaxed);
    }
    if (currentLinkerContext() != nullptr)
      Failed.store(true, std::memory_order_relaxed);
  };

  std::thread First(
      [&] { Run(llvm::COFF::IMAGE_FILE_MACHINE_AMD64, "first.exe"); });
  std::thread Second(
      [&] { Run(llvm::COFF::IMAGE_FILE_MACHINE_ARM64, "second.exe"); });
  First.join();
  Second.join();

  EXPECT_FALSE(Failed.load(std::memory_order_relaxed));
}

TEST(PluginCOFFContextIsolationTest,
     DispatcherFatalTimeTraceDoesNotPoisonNextLink) {
  neverc::ProcessResourceBrokerConfig BrokerConfig;
  BrokerConfig.Enabled = true;
  BrokerConfig.CpuTokens = 1;
  auto Broker = neverc::ProcessResourceBrokerTestAccess::create(BrokerConfig);
  neverc::ScopedProcessResourceBrokerOverride Override(*Broker);

  llvm::SmallString<128> OutputPath;
  std::error_code EC = llvm::sys::fs::createTemporaryFile(
      "neverc-dispatcher-crash-trace", "exe", OutputPath);
  ASSERT_FALSE(EC) << EC.message();
  llvm::FileRemover RemoveOutput(OutputPath);
  const std::string TracePath = OutputPath.str().str() + ".time-trace";
  llvm::FileRemover RemoveTrace(TracePath);

  LinkerDriverConfig Config;
  Config.outputFile = OutputPath.str().str();
  Config.timeTraceEnabled = true;
  Config.timeTraceGranularity = 0;
  auto ExecutionRequest = std::make_shared<LinkExecutionRequest>();
  Config.executionRequest = ExecutionRequest;
  const long StableRequestOwners = ExecutionRequest.use_count();
  ASSERT_EQ(StableRequestOwners, 2);
  const char *Args[] = {"neverc-test-linker"};
  std::string Stdout;
  std::string Stderr;
  llvm::raw_string_ostream StdoutStream(Stdout);
  llvm::raw_string_ostream StderrStream(Stderr);

  llvm::CrashRecoveryContext::Enable();
  auto DisableCrashRecovery =
      llvm::make_scope_exit([] { llvm::CrashRecoveryContext::Disable(); });
  int FatalResult = 0;
  {
    llvm::CrashRecoveryContext CRC;
    const linker::DriverDef FatalDrivers[] = {
        {linker::Flavor::WinLink, fatalTraceBackend}};
    EXPECT_FALSE(CRC.RunSafely([&] {
      FatalResult =
          linker::dispatchLink(FatalDrivers, linker::Flavor::WinLink, Args,
                               StdoutStream, StderrStream, Config);
    }));
    EXPECT_EQ(CRC.RetCode, 1);
  }
  EXPECT_EQ(FatalResult, 0);
  EXPECT_EQ(ExecutionRequest.use_count(), StableRequestOwners);
  EXPECT_FALSE(llvm::timeTraceProfilerEnabled());
  EXPECT_EQ(currentLinkerContext(), nullptr);
  EXPECT_FALSE(llvm::sys::fs::exists(TracePath));
  const neverc::ProcessResourceBrokerSnapshot Recovered =
      neverc::ProcessResourceBrokerTestAccess::snapshot(*Broker);
  ASSERT_EQ(Recovered.ActiveTokens, 0U);
  ASSERT_EQ(Recovered.ActiveSessions, 0U);
  EXPECT_EQ(Recovered.AvailableTokens, 1U);

  const linker::DriverDef SuccessfulDrivers[] = {
      {linker::Flavor::WinLink, successfulTraceBackend}};
  ASSERT_EQ(linker::dispatchLink(SuccessfulDrivers, linker::Flavor::WinLink,
                                 Args, StdoutStream, StderrStream, Config),
            0);
  EXPECT_FALSE(llvm::timeTraceProfilerEnabled());

  auto Trace = llvm::MemoryBuffer::getFile(TracePath);
  ASSERT_TRUE(static_cast<bool>(Trace)) << Trace.getError().message();
  ASSERT_FALSE((*Trace)->getBuffer().empty());
  auto Parsed = llvm::json::parse((*Trace)->getBuffer());
  ASSERT_TRUE(static_cast<bool>(Parsed))
      << llvm::toString(Parsed.takeError()).str().str();
  const llvm::json::Object *Root = Parsed->getAsObject();
  ASSERT_NE(Root, nullptr);
  const llvm::json::Array *Events = Root->getArray("traceEvents");
  ASSERT_NE(Events, nullptr);
  bool SawDispatch = false;
  bool SawBackend = false;
  for (const llvm::json::Value &Value : *Events) {
    const llvm::json::Object *Event = Value.getAsObject();
    if (!Event || Event->getString("ph") != "X")
      continue;
    SawDispatch |= Event->getString("name") == "neverc.link.dispatch";
    SawBackend |= Event->getString("name") == "neverc.link.backend";
  }
  EXPECT_TRUE(SawDispatch);
  EXPECT_TRUE(SawBackend);

  {
    LinkerExecutionContext ExternalExecution;
    LinkerDriverConfig ExternalConfig = Config;
    ExternalConfig.executionContext = &ExternalExecution;
    const long OwnersWithExternalConfig = StableRequestOwners + 1;
    ASSERT_EQ(ExecutionRequest.use_count(), OwnersWithExternalConfig);
    const neverc::ResourceSessionView ExpectedSession =
        ExternalExecution.resourceSession();
    ASSERT_TRUE(ExpectedSession);
    const neverc::ProcessResourceBrokerSnapshot BeforeFatal =
        neverc::ProcessResourceBrokerTestAccess::snapshot(*Broker);
    ASSERT_EQ(BeforeFatal.ActiveTokens, 1U);
    ASSERT_EQ(BeforeFatal.ActiveSessions, 1U);

    int ExternalFatalResult = 0;
    {
      llvm::CrashRecoveryContext CRC;
      const linker::DriverDef FatalDrivers[] = {
          {linker::Flavor::WinLink, fatalTraceBackend}};
      EXPECT_FALSE(CRC.RunSafely([&] {
        ExternalFatalResult =
            linker::dispatchLink(FatalDrivers, linker::Flavor::WinLink, Args,
                                 StdoutStream, StderrStream, ExternalConfig);
      }));
      EXPECT_EQ(CRC.RetCode, 1);
    }
    EXPECT_EQ(ExternalFatalResult, 0);
    EXPECT_EQ(ExecutionRequest.use_count(), OwnersWithExternalConfig);
    EXPECT_FALSE(llvm::timeTraceProfilerEnabled());
    EXPECT_EQ(currentLinkerContext(), nullptr);
    EXPECT_TRUE(ExternalExecution.resourceSession().refersToSameSession(
        ExpectedSession));
    const neverc::ProcessResourceBrokerSnapshot AfterFatal =
        neverc::ProcessResourceBrokerTestAccess::snapshot(*Broker);
    EXPECT_EQ(AfterFatal.ActiveTokens, 1U);
    EXPECT_EQ(AfterFatal.ActiveSessions, 1U);
    EXPECT_EQ(AfterFatal.AvailableTokens, 0U);
  }
  const neverc::ProcessResourceBrokerSnapshot Final =
      neverc::ProcessResourceBrokerTestAccess::snapshot(*Broker);
  EXPECT_EQ(Final.ActiveTokens, 0U);
  EXPECT_EQ(Final.ActiveSessions, 0U);
  EXPECT_EQ(Final.AvailableTokens, 1U);
  EXPECT_EQ(ExecutionRequest.use_count(), StableRequestOwners);
}

TEST(PluginCOFFContextIsolationTest,
     DirectFatalTimeTraceDoesNotPoisonNextLink) {
  initializeAssemblyTargets();
  const neverc::plugin::BuiltinTargetRoute *Route =
      neverc::plugin::findBuiltinTargetRoute("x86_64-pc-windows-msvc");
  ASSERT_NE(Route, nullptr);
  auto Target = neverc::plugin::lookupBuiltinLLVMTarget(*Route);
  ASSERT_TRUE(static_cast<bool>(Target))
      << llvm::toString(Target.takeError()).str().str();

  constexpr llvm::StringLiteral Assembly = R"(
.text
.globl coff_trace_entry
coff_trace_entry:
  ret
)";
  llvm::SmallVector<char, 0> Object;
  llvm::raw_svector_ostream ObjectStream(Object);
  neverc::plugin::BuiltinLLVMAsmParserRequest Request;
  Request.Target = *Target;
  Request.TargetTriple =
      llvm::Triple(llvm::Triple::normalize(Route->CanonicalTriple));
  Request.CPU = Route->DefaultCPU;
  Request.Input = llvm::MemoryBufferRef(Assembly, "coff-trace-recovery.s");
  Request.Output = &ObjectStream;
  if (llvm::Error Error = neverc::plugin::runBuiltinLLVMAsmParser(Request))
    FAIL() << llvm::toString(std::move(Error)).str().str();

  neverc::InMemoryFileStore &Store = neverc::InMemoryFileStore::instance();
  Store.clear();
  auto ClearStore = llvm::make_scope_exit([&] { Store.clear(); });
  constexpr llvm::StringLiteral ObjectPath = "/virtual/coff-trace-recovery.obj";
  llvm::SmallString<0> &ObjectBytes = Store.create(ObjectPath, Object.size());
  ObjectBytes.append(Object.begin(), Object.end());
  Store.freeze();

  llvm::SmallString<128> OutputPath;
  std::error_code EC = llvm::sys::fs::createTemporaryFile(
      "neverc-direct-crash-trace", "exe", OutputPath);
  ASSERT_FALSE(EC) << EC.message();
  llvm::FileRemover RemoveOutput(OutputPath);
  const std::string TracePath = OutputPath.str().str() + ".time-trace";
  llvm::FileRemover RemoveTrace(TracePath);

  LinkerDriverConfig Config;
  Config.outputFile = OutputPath.str().str();
  Config.threadCount = 1;
  Config.timeTraceEnabled = true;
  Config.timeTraceGranularity = 0;
  std::string Stdout;
  std::string Stderr;
  llvm::raw_string_ostream StdoutStream(Stdout);
  llvm::raw_string_ostream StderrStream(Stderr);
  auto CleanupProfiler = llvm::make_scope_exit([] {
    if (llvm::timeTraceProfilerEnabled())
      llvm::timeTraceProfilerCleanup();
  });

  llvm::CrashRecoveryContext::Enable();
  auto DisableCrashRecovery =
      llvm::make_scope_exit([] { llvm::CrashRecoveryContext::Disable(); });
  {
    llvm::CrashRecoveryContext CRC;
    const char *FatalArgs[] = {"neverc-test-linker", "--machine=x64"};
    EXPECT_FALSE(CRC.RunSafely([&] {
      linker::coff::link(FatalArgs, StdoutStream, StderrStream,
                         /*exitEarly=*/false, /*disableOutput=*/false, Config);
    }));
    EXPECT_EQ(CRC.RetCode, 1);
  }
  EXPECT_FALSE(llvm::timeTraceProfilerEnabled());
  EXPECT_EQ(currentLinkerContext(), nullptr);
  EXPECT_FALSE(llvm::sys::fs::exists(TracePath));

  const char *SuccessArgs[] = {
      "neverc-test-linker",  "--machine=x64",  "--entry=coff_trace_entry",
      "--subsystem=console", "--nodefaultlib", ObjectPath.data()};
  ASSERT_TRUE(linker::coff::link(SuccessArgs, StdoutStream, StderrStream,
                                 /*exitEarly=*/false,
                                 /*disableOutput=*/false, Config))
      << Stderr;
  EXPECT_FALSE(llvm::timeTraceProfilerEnabled());
  EXPECT_EQ(currentLinkerContext(), nullptr);

  auto Trace = llvm::MemoryBuffer::getFile(TracePath);
  ASSERT_TRUE(static_cast<bool>(Trace)) << Trace.getError().message();
  ASSERT_FALSE((*Trace)->getBuffer().empty());
  auto Parsed = llvm::json::parse((*Trace)->getBuffer());
  ASSERT_TRUE(static_cast<bool>(Parsed))
      << llvm::toString(Parsed.takeError()).str().str();
  const llvm::json::Object *Root = Parsed->getAsObject();
  ASSERT_NE(Root, nullptr);
  const llvm::json::Array *Events = Root->getArray("traceEvents");
  ASSERT_NE(Events, nullptr);
  unsigned CompleteMainScopes = 0;
  for (const llvm::json::Value &Value : *Events) {
    const llvm::json::Object *Event = Value.getAsObject();
    if (Event && Event->getString("ph") == "X" &&
        Event->getString("name") == "COFF link")
      ++CompleteMainScopes;
  }
  EXPECT_EQ(CompleteMainScopes, 1U);

  ASSERT_FALSE(llvm::sys::fs::remove(TracePath));
  {
    LinkerExecutionContext ExternalExecution;
    LinkerDriverConfig ExternalConfig = Config;
    ExternalConfig.executionContext = &ExternalExecution;
    const neverc::ResourceSessionView ExpectedSession =
        ExternalExecution.resourceSession();
    ASSERT_TRUE(ExpectedSession);

    {
      llvm::CrashRecoveryContext CRC;
      const char *FatalArgs[] = {"neverc-test-linker", "--machine=x64"};
      EXPECT_FALSE(CRC.RunSafely([&] {
        linker::coff::link(FatalArgs, StdoutStream, StderrStream,
                           /*exitEarly=*/false, /*disableOutput=*/false,
                           ExternalConfig);
      }));
      EXPECT_EQ(CRC.RetCode, 1);
    }
    EXPECT_FALSE(llvm::timeTraceProfilerEnabled());
    EXPECT_EQ(currentLinkerContext(), nullptr);
    EXPECT_EQ(ExternalExecution.common(), nullptr);
    EXPECT_TRUE(ExternalExecution.resourceSession().refersToSameSession(
        ExpectedSession));
    EXPECT_FALSE(llvm::sys::fs::exists(TracePath));
  }
}

TEST(PluginCOFFContextIsolationTest,
     AutomaticThreadsKeepSmallDirectLinksSerial) {
  if (llvm::thread::hardware_concurrency() < 2)
    GTEST_SKIP() << "automatic worker selection needs two available CPUs";

  initializeAssemblyTargets();
  const neverc::plugin::BuiltinTargetRoute *Route =
      neverc::plugin::findBuiltinTargetRoute("x86_64-pc-windows-msvc");
  ASSERT_NE(Route, nullptr);
  auto Target = neverc::plugin::lookupBuiltinLLVMTarget(*Route);
  ASSERT_TRUE(static_cast<bool>(Target))
      << llvm::toString(Target.takeError()).str().str();

  constexpr llvm::StringLiteral Assembly = R"(
.text
.globl coff_tiny_entry
coff_tiny_entry:
  ret
)";
  llvm::SmallVector<char, 0> Object;
  llvm::raw_svector_ostream ObjectStream(Object);
  neverc::plugin::BuiltinLLVMAsmParserRequest Request;
  Request.Target = *Target;
  Request.TargetTriple =
      llvm::Triple(llvm::Triple::normalize(Route->CanonicalTriple));
  Request.CPU = Route->DefaultCPU;
  Request.Input = llvm::MemoryBufferRef(Assembly, "coff-tiny-link.s");
  Request.Output = &ObjectStream;
  if (llvm::Error Error = neverc::plugin::runBuiltinLLVMAsmParser(Request))
    FAIL() << llvm::toString(std::move(Error)).str().str();
  ASSERT_LT(Object.size(), 16U * 1024U * 1024U);

  neverc::InMemoryFileStore &Store = neverc::InMemoryFileStore::instance();
  Store.clear();
  auto ClearStore = llvm::make_scope_exit([&] { Store.clear(); });
  constexpr llvm::StringLiteral ObjectPath = "/virtual/coff-tiny-link.obj";
  llvm::SmallString<0> &ObjectBytes = Store.create(ObjectPath, Object.size());
  ObjectBytes.append(Object.begin(), Object.end());
  Store.freeze();

  llvm::SmallString<128> OutputPath;
  std::error_code EC = llvm::sys::fs::createTemporaryFile(
      "neverc-coff-tiny-link", "exe", OutputPath);
  ASSERT_FALSE(EC) << EC.message();
  llvm::FileRemover RemoveOutput(OutputPath);
  const std::string TracePath = OutputPath.str().str() + ".time-trace";
  llvm::FileRemover RemoveTrace(TracePath);

  const char *Args[] = {"neverc-test-linker",      "--machine=x64",
                        "--entry=coff_tiny_entry", "--subsystem=console",
                        "--nodefaultlib",          ObjectPath.data()};
  auto Link = [&](unsigned RequestedThreads, unsigned &SelectedThreads,
                  std::string &Image) {
    LinkerExecutionContext Execution;
    LinkerDriverConfig Config;
    Config.executionContext = &Execution;
    Config.outputFile = OutputPath.str().str();
    Config.threadCount = RequestedThreads;
    Config.repro = true;
    Config.timeTraceEnabled = true;
    Config.timeTraceGranularity = 1;
    if (std::error_code RemoveEC = llvm::sys::fs::remove(TracePath)) {
      ADD_FAILURE() << RemoveEC.message();
      return false;
    }
    std::string Stdout;
    std::string Stderr;
    llvm::raw_string_ostream StdoutStream(Stdout);
    llvm::raw_string_ostream StderrStream(Stderr);
    if (!linker::coff::link(Args, StdoutStream, StderrStream,
                            /*exitEarly=*/false,
                            /*disableOutput=*/false, Config)) {
      ADD_FAILURE() << Stderr;
      return false;
    }
    if (!Execution.common()) {
      ADD_FAILURE() << "COFF execution did not retain its linker context";
      return false;
    }
    if (llvm::timeTraceProfilerEnabled()) {
      ADD_FAILURE() << "COFF direct link retained its time-trace profiler";
      return false;
    }
    auto Trace = llvm::MemoryBuffer::getFile(TracePath);
    if (!Trace || (*Trace)->getBuffer().empty()) {
      ADD_FAILURE() << (Trace ? "empty COFF time trace"
                              : Trace.getError().message());
      return false;
    }
    auto Parsed = llvm::json::parse((*Trace)->getBuffer());
    if (!Parsed) {
      ADD_FAILURE() << llvm::toString(Parsed.takeError()).str().str();
      return false;
    }
    const llvm::json::Object *Root = Parsed->getAsObject();
    const llvm::json::Array *Events =
        Root ? Root->getArray("traceEvents") : nullptr;
    if (!Events) {
      ADD_FAILURE() << "COFF time trace has no traceEvents array";
      return false;
    }
    unsigned CompleteMainScopes = 0;
    for (const llvm::json::Value &Value : *Events) {
      const llvm::json::Object *Event = Value.getAsObject();
      if (Event && Event->getString("ph") == "X" &&
          Event->getString("name") == "COFF link")
        ++CompleteMainScopes;
    }
    if (CompleteMainScopes != 1) {
      ADD_FAILURE() << "expected one complete COFF link scope, got "
                    << CompleteMainScopes;
      return false;
    }
    SelectedThreads = Execution.common()->parallelThreadCount();
    auto Buffer = llvm::MemoryBuffer::getFile(OutputPath);
    if (!Buffer) {
      ADD_FAILURE() << Buffer.getError().message();
      return false;
    }
    Image = (*Buffer)->getBuffer().str();
    return true;
  };

  unsigned AutoThreads = 0;
  unsigned ExplicitThreads = 0;
  std::string AutoImage;
  std::string ExplicitImage;
  ASSERT_TRUE(Link(/*RequestedThreads=*/0, AutoThreads, AutoImage));
  ASSERT_TRUE(Link(/*RequestedThreads=*/2, ExplicitThreads, ExplicitImage));
  EXPECT_EQ(AutoThreads, 1U);
  EXPECT_EQ(ExplicitThreads, 2U);
  EXPECT_EQ(AutoImage, ExplicitImage)
      << "COFF output changed when the selected worker budget changed";

  constexpr llvm::StringLiteral LargeAssembly = R"(
.text
.globl coff_tiny_entry
coff_tiny_entry:
  ret
.data
.zero 17825792
)";
  llvm::SmallVector<char, 0> LargeObject;
  llvm::raw_svector_ostream LargeObjectStream(LargeObject);
  Request.Input = llvm::MemoryBufferRef(LargeAssembly, "coff-large-link.s");
  Request.Output = &LargeObjectStream;
  if (llvm::Error Error = neverc::plugin::runBuiltinLLVMAsmParser(Request))
    FAIL() << llvm::toString(std::move(Error)).str().str();
  ASSERT_GT(LargeObject.size(), 16U * 1024U * 1024U);

  Store.clear();
  llvm::SmallString<0> &LargeObjectBytes =
      Store.create(ObjectPath, LargeObject.size());
  LargeObjectBytes.append(LargeObject.begin(), LargeObject.end());
  Store.freeze();

  unsigned LargeThreads = 0;
  std::string LargeImage;
  ASSERT_TRUE(Link(/*RequestedThreads=*/0, LargeThreads, LargeImage));
  EXPECT_EQ(LargeThreads, std::min(16U, llvm::thread::hardware_concurrency()));
}

TEST(PluginCOFFContextIsolationTest,
     ExplicitThreadsConfigureDefOnlyExecutionContext) {
  if (llvm::thread::hardware_concurrency() < 2)
    GTEST_SKIP() << "explicit two-thread configuration needs two CPUs";

  llvm::SmallString<128> DefPath;
  int DefFD = -1;
  std::error_code EC = llvm::sys::fs::createTemporaryFile(
      "neverc-coff-def-only", "def", DefFD, DefPath);
  ASSERT_FALSE(EC) << EC.message();
  llvm::FileRemover RemoveDef(DefPath);
  {
    llvm::raw_fd_ostream DefStream(DefFD, /*shouldClose=*/true);
    DefStream << "LIBRARY neverc_def_only\nEXPORTS\n  neverc_export\n";
  }

  LinkerExecutionContext Execution;
  LinkerDriverConfig Config;
  Config.executionContext = &Execution;
  Config.threadCount = 2;
  const std::string DefArg = (llvm::Twine("--def=") + DefPath).str();
  const char *Args[] = {"neverc-test-linker", "--machine=x64",
                        "--noimplib", DefArg.c_str()};
  std::string Stdout;
  std::string Stderr;
  llvm::raw_string_ostream StdoutStream(Stdout);
  llvm::raw_string_ostream StderrStream(Stderr);
  ASSERT_TRUE(linker::coff::link(Args, StdoutStream, StderrStream,
                                 /*exitEarly=*/false,
                                 /*disableOutput=*/true, Config))
      << Stderr;
  ASSERT_NE(Execution.common(), nullptr);
  EXPECT_TRUE(Execution.common()->parallelConfigured());
  EXPECT_EQ(Execution.common()->parallelThreadCount(), 2u);
}

TEST(PluginCOFFContextIsolationTest,
     AutomaticThreadsIncludeMaterializedResourcePayload) {
  if (llvm::thread::hardware_concurrency() < 2)
    GTEST_SKIP() << "automatic worker selection needs two available CPUs";

  initializeAssemblyTargets();
  const neverc::plugin::BuiltinTargetRoute *Route =
      neverc::plugin::findBuiltinTargetRoute("x86_64-pc-windows-msvc");
  ASSERT_NE(Route, nullptr);
  auto Target = neverc::plugin::lookupBuiltinLLVMTarget(*Route);
  ASSERT_TRUE(static_cast<bool>(Target))
      << llvm::toString(Target.takeError()).str().str();

  constexpr llvm::StringLiteral Assembly = R"(
.text
.globl coff_resource_entry
coff_resource_entry:
  ret
)";
  llvm::SmallVector<char, 0> Object;
  llvm::raw_svector_ostream ObjectStream(Object);
  neverc::plugin::BuiltinLLVMAsmParserRequest Request;
  Request.Target = *Target;
  Request.TargetTriple =
      llvm::Triple(llvm::Triple::normalize(Route->CanonicalTriple));
  Request.CPU = Route->DefaultCPU;
  Request.Input = llvm::MemoryBufferRef(Assembly, "coff-resource-link.s");
  Request.Output = &ObjectStream;
  if (llvm::Error Error = neverc::plugin::runBuiltinLLVMAsmParser(Request))
    FAIL() << llvm::toString(std::move(Error)).str().str();

  constexpr size_t ResourcePayloadSize = 9U * 1024U * 1024U;
  llvm::SmallVector<char, 0> Resource =
      createWindowsResource(ResourcePayloadSize);
  ASSERT_GT(Resource.size(), 8U * 1024U * 1024U);

  neverc::InMemoryFileStore &Store = neverc::InMemoryFileStore::instance();
  Store.clear();
  auto ClearStore = llvm::make_scope_exit([&] { Store.clear(); });
  constexpr llvm::StringLiteral ObjectPath = "/virtual/coff-resource-link.obj";
  constexpr llvm::StringLiteral ResourcePath =
      "/virtual/coff-resource-link.res";
  llvm::SmallString<0> &ObjectBytes = Store.create(ObjectPath, Object.size());
  ObjectBytes.append(Object.begin(), Object.end());
  llvm::SmallString<0> &ResourceBytes =
      Store.create(ResourcePath, Resource.size());
  ResourceBytes.append(Resource.begin(), Resource.end());
  Store.freeze();

  llvm::SmallString<128> OutputPath;
  std::error_code EC = llvm::sys::fs::createTemporaryFile(
      "neverc-coff-resource-link", "exe", OutputPath);
  ASSERT_FALSE(EC) << EC.message();
  llvm::FileRemover RemoveOutput(OutputPath);

  const char *Args[] = {
      "neverc-test-linker",  "--machine=x64",  "--entry=coff_resource_entry",
      "--subsystem=console", "--nodefaultlib", ObjectPath.data(),
      ResourcePath.data()};
  auto Link = [&](unsigned RequestedThreads, unsigned &SelectedThreads,
                  std::string &Image) {
    LinkerExecutionContext Execution;
    LinkerDriverConfig Config;
    Config.executionContext = &Execution;
    Config.outputFile = OutputPath.str().str();
    Config.threadCount = RequestedThreads;
    Config.repro = true;
    std::string Stdout;
    std::string Stderr;
    llvm::raw_string_ostream StdoutStream(Stdout);
    llvm::raw_string_ostream StderrStream(Stderr);
    if (!linker::coff::link(Args, StdoutStream, StderrStream,
                            /*exitEarly=*/false,
                            /*disableOutput=*/false, Config)) {
      ADD_FAILURE() << Stderr;
      return false;
    }
    if (!Execution.common()) {
      ADD_FAILURE() << "COFF execution did not retain its linker context";
      return false;
    }
    SelectedThreads = Execution.common()->parallelThreadCount();
    auto Buffer = llvm::MemoryBuffer::getFile(OutputPath);
    if (!Buffer) {
      ADD_FAILURE() << Buffer.getError().message();
      return false;
    }
    Image = (*Buffer)->getBuffer().str();
    return true;
  };

  unsigned AutoThreads = 0;
  unsigned SerialThreads = 0;
  std::string AutoImage;
  std::string SerialImage;
  ASSERT_TRUE(Link(/*RequestedThreads=*/0, AutoThreads, AutoImage));
  ASSERT_TRUE(Link(/*RequestedThreads=*/1, SerialThreads, SerialImage));
  EXPECT_EQ(AutoThreads, std::min(16U, llvm::thread::hardware_concurrency()));
  EXPECT_EQ(SerialThreads, 1U);
  ASSERT_GT(AutoImage.size(), ResourcePayloadSize)
      << "the materialized resource payload was not emitted";
  EXPECT_EQ(AutoImage, SerialImage)
      << "COFF resource conversion changed with the worker budget";

  // Embedded manifests are materialized only after LTO. Exercise that exact
  // late-resource path without invoking external mt.exe: manifest dependency
  // text is emitted directly into the default XML and deliberately follows
  // link.exe's no-validation behavior.
  std::string LargeDependency(ResourcePayloadSize, 'x');
  std::string ManifestDependencyArg =
      "--manifestdependency=" + LargeDependency;
  const char *ManifestArgs[] = {
      "neverc-test-linker",  "--machine=x64",
      "--entry=coff_resource_entry", "--subsystem=console",
      "--nodefaultlib",      "--manifest=embed",
      ManifestDependencyArg.c_str(), ObjectPath.data()};
  LinkerExecutionContext ManifestExecution;
  LinkerDriverConfig ManifestConfig;
  ManifestConfig.executionContext = &ManifestExecution;
  ManifestConfig.outputFile = OutputPath.str().str();
  ManifestConfig.threadCount = 0;
  ManifestConfig.repro = true;
  std::string ManifestStdout;
  std::string ManifestStderr;
  llvm::raw_string_ostream ManifestStdoutStream(ManifestStdout);
  llvm::raw_string_ostream ManifestStderrStream(ManifestStderr);
  ASSERT_TRUE(linker::coff::link(ManifestArgs, ManifestStdoutStream,
                                 ManifestStderrStream,
                                 /*exitEarly=*/false,
                                 /*disableOutput=*/false, ManifestConfig))
      << ManifestStderr;
  ASSERT_NE(ManifestExecution.common(), nullptr);
  EXPECT_EQ(ManifestExecution.common()->parallelThreadCount(),
            std::min(16U, llvm::thread::hardware_concurrency()));
}

TEST(PluginCOFFContextIsolationTest,
     PostLTOWorkloadExcludesBitcodeRepresentation) {
  constexpr uint64_t MiB = 1024ULL * 1024ULL;
  constexpr uint64_t KiB = 1024ULL;

  const auto Included = detail::mergeMaterializedInputWorkload(
      /*NativeBytes=*/7 * MiB, /*NativeFiles=*/1,
      /*BitcodeBytes=*/MiB, /*BitcodeFiles=*/1,
      /*ResourceBytes=*/512 * KiB, /*ResourceFiles=*/1,
      /*IncludeBitcode=*/true);
  const auto PostLTO = detail::mergeMaterializedInputWorkload(
      /*NativeBytes=*/7 * MiB, /*NativeFiles=*/1,
      /*BitcodeBytes=*/MiB, /*BitcodeFiles=*/1,
      /*ResourceBytes=*/512 * KiB, /*ResourceFiles=*/1,
      /*IncludeBitcode=*/false);

  LinkThreadPolicy Policy;
  Policy.MinParallelBytes = 8 * MiB;
  Policy.BytesPerAdditionalThread = 0;
  Policy.MinAverageFileBytes = 0;
  EXPECT_EQ(Included.first, 8 * MiB + 512 * KiB);
  EXPECT_EQ(Included.second, 3U);
  EXPECT_EQ(linker::selectAdaptiveLinkThreadCount(
                /*RequestedThreads=*/0, /*AvailableThreads=*/16, Included.first,
                Included.second, Policy),
            16U);
  EXPECT_EQ(PostLTO.first, 7 * MiB + 512 * KiB);
  EXPECT_EQ(PostLTO.second, 2U);
  EXPECT_EQ(linker::selectAdaptiveLinkThreadCount(
                /*RequestedThreads=*/0, /*AvailableThreads=*/16, PostLTO.first,
                PostLTO.second, Policy),
            1U);
}

TEST(PluginCOFFContextIsolationTest,
     ReproducibleBuildIdAndReleaseChecksumAreThreadIndependent) {
  initializeAssemblyTargets();
  constexpr llvm::StringLiteral Assembly = R"(
.text
.globl coff_hash_entry
coff_hash_entry:
  ret
.data
.globl coff_hash_payload
coff_hash_payload:
  .byte 1
  .zero 4194560
)";
  neverc::InMemoryFileStore &Store = neverc::InMemoryFileStore::instance();
  Store.clear();
  auto ClearStore = llvm::make_scope_exit([&] { Store.clear(); });

  struct TargetCase {
    const char *Triple;
    const char *Machine;
    const char *Arch;
  };
  for (const TargetCase &Case :
       {TargetCase{"x86_64-pc-windows-msvc", "--machine=x64", "x64"},
        TargetCase{"aarch64-pc-windows-msvc", "--machine=arm64", "arm64"}}) {
    SCOPED_TRACE(Case.Triple);
    const neverc::plugin::BuiltinTargetRoute *Route =
        neverc::plugin::findBuiltinTargetRoute(Case.Triple);
    ASSERT_NE(Route, nullptr);
    auto Target = neverc::plugin::lookupBuiltinLLVMTarget(*Route);
    ASSERT_TRUE(static_cast<bool>(Target))
        << llvm::toString(Target.takeError()).str().str();

    llvm::SmallVector<char, 0> Object;
    llvm::raw_svector_ostream ObjectStream(Object);
    neverc::plugin::BuiltinLLVMAsmParserRequest Request;
    Request.Target = *Target;
    Request.TargetTriple =
        llvm::Triple(llvm::Triple::normalize(Route->CanonicalTriple));
    Request.CPU = Route->DefaultCPU;
    Request.Input = llvm::MemoryBufferRef(Assembly, "coff-content-hash.s");
    Request.Output = &ObjectStream;
    if (llvm::Error Error = neverc::plugin::runBuiltinLLVMAsmParser(Request))
      FAIL() << llvm::toString(std::move(Error)).str().str();

    Store.clear();
    const std::string ObjectPath =
        std::string("/virtual/coff-content-hash-") + Case.Arch + ".obj";
    llvm::SmallString<0> &ObjectBytes = Store.create(ObjectPath, Object.size());
    ObjectBytes.append(Object.begin(), Object.end());
    Store.freeze();

    llvm::SmallString<128> OutputPath;
    std::error_code EC = llvm::sys::fs::createTemporaryFile(
        llvm::Twine("neverc-coff-content-hash-") + Case.Arch, "exe",
        OutputPath);
    ASSERT_FALSE(EC) << EC.message();
    llvm::FileRemover RemoveOutput(OutputPath);

    auto Link = [&](unsigned ThreadCount, std::string &Image) {
      LinkerExecutionContext Execution;
      LinkerDriverConfig Config;
      Config.executionContext = &Execution;
      Config.outputFile = OutputPath.str().str();
      Config.threadCount = ThreadCount;
      Config.repro = true;
      Config.buildId = "fast";
      const char *Args[] = {"neverc-test-linker",      Case.Machine,
                            "--entry=coff_hash_entry", "--subsystem=console",
                            "--nodefaultlib",          "--release",
                            ObjectPath.c_str()};
      std::string Stdout;
      std::string Stderr;
      llvm::raw_string_ostream StdoutStream(Stdout);
      llvm::raw_string_ostream StderrStream(Stderr);
      if (!linker::coff::link(Args, StdoutStream, StderrStream,
                              /*exitEarly=*/false, /*disableOutput=*/false,
                              Config)) {
        ADD_FAILURE() << Stderr;
        return false;
      }

      auto Buffer = llvm::MemoryBuffer::getFile(OutputPath);
      if (!Buffer) {
        ADD_FAILURE() << Buffer.getError().message();
        return false;
      }
      Image = (*Buffer)->getBuffer().str();
      return true;
    };

    std::string SerialImage;
    std::string ParallelImage;
    ASSERT_TRUE(Link(/*ThreadCount=*/1, SerialImage));
    ASSERT_TRUE(Link(/*ThreadCount=*/4, ParallelImage));
    ASSERT_GT(SerialImage.size(), 4U * 1024U * 1024U);
    ASSERT_TRUE(SerialImage == ParallelImage)
        << "COFF content hashing changed the complete PE image across thread "
           "counts";

    for (llvm::StringRef Image :
         {llvm::StringRef(SerialImage), llvm::StringRef(ParallelImage)}) {
      const std::optional<size_t> ChecksumOffset = findPEChecksumOffset(Image);
      ASSERT_TRUE(ChecksumOffset.has_value());
      ASSERT_LE(*ChecksumOffset + 4, Image.size());
      const auto *Bytes = reinterpret_cast<const uint8_t *>(Image.data());
      const uint32_t StoredChecksum =
          llvm::support::endian::read32le(Bytes + *ChecksumOffset);
      EXPECT_NE(StoredChecksum, 0U);
      EXPECT_EQ(StoredChecksum, recomputePEChecksum(Image, *ChecksumOffset));
    }
  }
}
