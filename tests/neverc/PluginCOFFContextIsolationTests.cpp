#include "Linker/COFF/COFFLinkerContext.h"
#include "Linker/Core/Driver/Dispatcher.h"
#include "Linker/Core/Runtime/LinkerExecutionContext.h"
#include "neverc/Invoke/InMemoryFileStore.h"
#include "neverc/Plugin/Host/BuiltinLLVMAsmParser.h"
#include "neverc/Plugin/Host/BuiltinTargetProvider.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"
#include <atomic>
#include <cstdint>
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

} // namespace

TEST(PluginCOFFContextIsolationTest, ConcurrentStateDoesNotCrossContexts) {
  std::atomic<unsigned> Ready{0};
  std::atomic<bool> Failed{false};

  auto Run = [&](llvm::COFF::MachineTypes Machine, llvm::StringRef Name) {
    {
      LinkerExecutionContext Execution;
      COFFLinkerContext &Context =
          Execution.createBackend<COFFLinkerContext>();
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
