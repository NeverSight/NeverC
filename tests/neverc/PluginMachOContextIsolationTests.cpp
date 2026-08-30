#include "Driver/Parallelism.h"
#include "Linker/Core/Driver/Dispatcher.h"
#include "Linker/Core/Runtime/LinkerExecutionContext.h"
#include "Linker/MachO/Driver.h"
#include "Linker/MachO/MachOLinkerContext.h"
#include "neverc/Invoke/InMemoryFileStore.h"
#include "neverc/Plugin/Host/BuiltinLLVMAsmParser.h"
#include "neverc/Plugin/Host/BuiltinTargetProvider.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Object/ArchiveWriter.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

using namespace linker;
using namespace linker::macho;

LINKER_HAS_DRIVER(macho)

namespace neverc::plugin::detail {
bool relocatableLTOEmitsAddrsig(
    llvm::Triple::ObjectFormatType ObjectFormat,
    const linker::LinkerDriverConfig &Config);
}

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

struct ArchiveMemberParseObservation {
  unsigned Calls = 0;
  unsigned SelectedThreads = 0;
};

void observeArchiveMemberParse(llvm::MemoryBufferRef, unsigned SelectedThreads,
                               void *Context) {
  auto &Observation = *static_cast<ArchiveMemberParseObservation *>(Context);
  ++Observation.Calls;
  Observation.SelectedThreads = SelectedThreads;
}

} // namespace

TEST(PluginMachOContextIsolationTest,
     RelocatableLTOSafeICFEmitsAddrsigLikeNativeBackend) {
  linker::LinkerDriverConfig Config;
  struct MachOCase {
    int ICFLevel;
    bool EmitAddrsig;
  };
  constexpr MachOCase Cases[] = {
      {0, false},
      {1, true},
      {2, false},
  };
  for (const MachOCase &Case : Cases) {
    SCOPED_TRACE(Case.ICFLevel);
    Config.icfLevel = Case.ICFLevel;
    EXPECT_EQ(neverc::plugin::detail::relocatableLTOEmitsAddrsig(
                  llvm::Triple::MachO, Config),
              Case.EmitAddrsig);
  }

  // The bridge has always requested addrsig for ELF and COFF relocatable LTO;
  // the Mach-O safe-ICF correction must not change those backend contracts.
  Config.icfLevel = 0;
  EXPECT_TRUE(neverc::plugin::detail::relocatableLTOEmitsAddrsig(
      llvm::Triple::ELF, Config));
  EXPECT_TRUE(neverc::plugin::detail::relocatableLTOEmitsAddrsig(
      llvm::Triple::COFF, Config));
}

TEST(PluginMachOContextIsolationTest, ConcurrentStateDoesNotCrossContexts) {
  std::atomic<unsigned> Ready{0};
  std::atomic<bool> Failed{false};

  auto Run = [&](uint32_t DylibCount, llvm::StringRef Warning) {
    {
      LinkerExecutionContext Execution;
      MachOLinkerContext &Context =
          Execution.createBackend<MachOLinkerContext>();
      machoLCDylibCount() = DylibCount;
      machoMissingAutolinkWarnings().push_back(Warning);
      detail::incrementalInputWorkload().recordNative(DylibCount * 1024ULL);

      Ready.fetch_add(1, std::memory_order_release);
      while (Ready.load(std::memory_order_acquire) != 2)
        std::this_thread::yield();

      if (currentLinkerContext() != &Context ||
          machoLCDylibCount() != DylibCount ||
          machoMissingAutolinkWarnings().size() != 1 ||
          machoMissingAutolinkWarnings().front() != Warning ||
          detail::incrementalInputWorkload().materializedNative().Bytes !=
              DylibCount * 1024ULL)
        Failed.store(true, std::memory_order_relaxed);
    }
    if (currentLinkerContext() != nullptr)
      Failed.store(true, std::memory_order_relaxed);
  };

  std::thread First([&] { Run(3, "first"); });
  std::thread Second([&] { Run(19, "second"); });
  First.join();
  Second.join();

  EXPECT_FALSE(Failed.load(std::memory_order_relaxed));
}

TEST(PluginMachOContextIsolationTest,
     IncrementalWorkloadTracksThousandsOfSmallArchiveMembers) {
  constexpr uint64_t KiB = 1024;
  constexpr unsigned MemberCount = 4096;
  constexpr uint64_t MemberSize = 2 * KiB;

  detail::IncrementalInputWorkload Workload;
  detail::LinkInputWorkload Snapshot;
  for (unsigned I = 0; I != MemberCount - 1; ++I)
    Snapshot = Workload.recordNative(MemberSize);

  EXPECT_EQ(Snapshot.Bytes, 8ULL * 1024ULL * 1024ULL - MemberSize);
  EXPECT_EQ(Snapshot.Files, MemberCount - 1);

  LinkThreadPolicy Policy;
  Policy.MinParallelBytes = 8ULL * 1024ULL * 1024ULL;
  Policy.BytesPerAdditionalThread = 0;
  Policy.MinAverageFileBytes = 0;
  EXPECT_EQ(selectAdaptiveLinkThreadCount(/*RequestedThreads=*/0,
                                          /*AvailableThreads=*/16,
                                          Snapshot.Bytes, Snapshot.Files,
                                          Policy),
            1U);

  Snapshot = Workload.recordNative(MemberSize);
  EXPECT_EQ(Snapshot.Bytes, 8ULL * 1024ULL * 1024ULL);
  EXPECT_EQ(Snapshot.Files, MemberCount);
  EXPECT_EQ(selectAdaptiveLinkThreadCount(/*RequestedThreads=*/0,
                                          /*AvailableThreads=*/16,
                                          Snapshot.Bytes, Snapshot.Files,
                                          Policy),
            16U);
}

TEST(PluginMachOContextIsolationTest,
     ArchiveMemberAccountingRetainsPendingInputsBeforeParsing) {
  if (llvm::thread::hardware_concurrency() < 2)
    GTEST_SKIP() << "automatic worker selection needs two available CPUs";

  constexpr uint64_t KiB = 1024;
  constexpr unsigned MemberCount = 4096;
  constexpr uint64_t MemberSize = 2 * KiB;
  std::string Member(MemberSize, '\0');
  llvm::support::endian::write32le(Member.data(), llvm::MachO::MH_MAGIC_64);
  llvm::support::endian::write32le(Member.data() + 12, llvm::MachO::MH_OBJECT);
  const llvm::MemoryBufferRef Buffer(Member, "pending-archive-member.o");

  LinkerExecutionContext Execution;
  MachOLinkerContext &Context = Execution.createBackend<MachOLinkerContext>();
  for (unsigned I = 0; I != MemberCount - 1; ++I)
    configureParallelismForMaterializedInput(Buffer);

  EXPECT_FALSE(Context.parallelConfigured());
  configureParallelismForMaterializedInput(Buffer);
  EXPECT_TRUE(Context.parallelConfigured());
  EXPECT_EQ(Context.parallelThreadCount(),
            std::min(16U, llvm::thread::hardware_concurrency()));
}

TEST(PluginMachOContextIsolationTest,
     LTONativeWorkloadReplacesSourceBitcodeAccounting) {
  constexpr uint64_t MiB = 1024ULL * 1024ULL;
  detail::IncrementalInputWorkload Workload;

  Workload.recordNative(2 * MiB);
  const detail::LinkInputWorkload BeforeLTO = Workload.recordBitcode(7 * MiB);
  EXPECT_EQ(BeforeLTO.Bytes, 9 * MiB);
  EXPECT_EQ(BeforeLTO.Files, 2U);

  const uint64_t NativeObjectSizes[] = {3 * MiB, 4 * MiB};
  const detail::LinkInputWorkload AfterLTO =
      Workload.replaceBitcodeWithNative(NativeObjectSizes);
  EXPECT_EQ(AfterLTO.Bytes, 9 * MiB);
  EXPECT_EQ(AfterLTO.Files, 3U);
  EXPECT_EQ(Workload.current().Bytes, AfterLTO.Bytes);
  EXPECT_EQ(Workload.current().Files, AfterLTO.Files);

  detail::IncrementalInputWorkload EmptyOutputWorkload;
  EmptyOutputWorkload.recordBitcode(9 * MiB);
  const detail::LinkInputWorkload EmptyOutput =
      EmptyOutputWorkload.replaceBitcodeWithNative({});
  EXPECT_EQ(EmptyOutput.Bytes, 0U);
  EXPECT_EQ(EmptyOutput.Files, 0U);
}

TEST(PluginMachOContextIsolationTest,
     AutomaticThreadsKeepSmallDirectLinksSerial) {
  if (llvm::thread::hardware_concurrency() < 2)
    GTEST_SKIP() << "automatic worker selection needs two available CPUs";

  initializeAssemblyTargets();
  const neverc::plugin::BuiltinTargetRoute *Route =
      neverc::plugin::findBuiltinTargetRoute("x86_64-apple-macosx13.0");
  ASSERT_NE(Route, nullptr);
  auto Target = neverc::plugin::lookupBuiltinLLVMTarget(*Route);
  ASSERT_TRUE(static_cast<bool>(Target))
      << llvm::toString(Target.takeError()).str().str();

  constexpr llvm::StringLiteral Assembly = R"(
.text
.globl _macho_tiny_entry
_macho_tiny_entry:
  ret
)";
  llvm::SmallVector<char, 0> Object;
  llvm::raw_svector_ostream ObjectStream(Object);
  neverc::plugin::BuiltinLLVMAsmParserRequest Request;
  Request.Target = *Target;
  Request.TargetTriple =
      llvm::Triple(llvm::Triple::normalize(Route->CanonicalTriple));
  Request.CPU = Route->DefaultCPU;
  Request.Input = llvm::MemoryBufferRef(Assembly, "macho-tiny-link.s");
  Request.Output = &ObjectStream;
  if (llvm::Error Error = neverc::plugin::runBuiltinLLVMAsmParser(Request))
    FAIL() << llvm::toString(std::move(Error)).str().str();
  ASSERT_LT(Object.size(), 16U * 1024U * 1024U);

  neverc::InMemoryFileStore &Store = neverc::InMemoryFileStore::instance();
  Store.clear();
  auto ClearStore = llvm::make_scope_exit([&] { Store.clear(); });
  constexpr llvm::StringLiteral ObjectPath = "/virtual/macho-tiny-link.o";
  llvm::SmallString<0> &ObjectBytes = Store.create(ObjectPath, Object.size());
  ObjectBytes.append(Object.begin(), Object.end());
  Store.freeze();

  llvm::SmallString<128> OutputPath;
  std::error_code EC = llvm::sys::fs::createTemporaryFile(
      "neverc-macho-tiny-link", "macho", OutputPath);
  ASSERT_FALSE(EC) << EC.message();
  llvm::FileRemover RemoveOutput(OutputPath);

  const char *DirectArgs[] = {"neverc-test-linker", "-e", "_macho_tiny_entry",
                              ObjectPath.data()};
  auto Link = [&](llvm::ArrayRef<const char *> Args, unsigned RequestedThreads,
                  unsigned &SelectedThreads, std::string &Image,
                  llvm::StringRef Sysroot) {
    LinkerExecutionContext Execution;
    LinkerDriverConfig Config;
    Config.executionContext = &Execution;
    Config.outputFile = OutputPath.str().str();
    Config.threadCount = RequestedThreads;
    Config.archName = "x86_64";
    Config.platformName = "macos";
    Config.platformMinVersion = "13.0";
    Config.platformSdkVersion = "13.0";
    Config.nostdlib = true;
    Config.sysroot = Sysroot.str();
    std::string Stdout;
    std::string Stderr;
    llvm::raw_string_ostream StdoutStream(Stdout);
    llvm::raw_string_ostream StderrStream(Stderr);
    if (!linker::macho::link(Args, StdoutStream, StderrStream,
                             /*exitEarly=*/false,
                             /*disableOutput=*/false, Config)) {
      ADD_FAILURE() << Stderr;
      return false;
    }
    if (!Execution.common()) {
      ADD_FAILURE() << "Mach-O execution did not retain its linker context";
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
  ASSERT_TRUE(Link(DirectArgs, /*RequestedThreads=*/0, AutoThreads, AutoImage,
                   /*Sysroot=*/{}));
  ASSERT_TRUE(Link(DirectArgs, /*RequestedThreads=*/2, ExplicitThreads,
                   ExplicitImage, /*Sysroot=*/{}));
  EXPECT_EQ(AutoThreads, 1U);
  EXPECT_EQ(ExplicitThreads, 2U);
  EXPECT_EQ(AutoImage, ExplicitImage)
      << "Mach-O output changed when the selected worker budget changed";

  llvm::SmallVector<llvm::NewArchiveMember, 1> Members;
  Members.emplace_back(llvm::MemoryBufferRef(
      llvm::StringRef(Object.data(), Object.size()), "tiny-member.o"));
  auto Archive = llvm::writeArchiveToBuffer(
      Members, llvm::SymtabWritingMode::NormalSymtab,
      llvm::object::Archive::K_DARWIN, /*Deterministic=*/true,
      /*Thin=*/false);
  ASSERT_TRUE(static_cast<bool>(Archive))
      << llvm::toString(Archive.takeError()).str().str();
  ASSERT_LT((*Archive)->getBufferSize(), 8U * 1024U * 1024U);

  llvm::SmallString<128> LibraryDirectory;
  ASSERT_FALSE(llvm::sys::fs::createUniqueDirectory(
      "neverc-macho-tiny-libraries", LibraryDirectory));
  auto RemoveLibraryDirectory = llvm::make_scope_exit(
      [&] { (void)llvm::sys::fs::remove_directories(LibraryDirectory); });
  std::vector<std::string> LibraryOptions;
  LibraryOptions.reserve(8);
  for (unsigned I = 0; I != 8; ++I) {
    llvm::SmallString<128> ArchivePath = LibraryDirectory;
    llvm::sys::path::append(ArchivePath, "libtiny" + std::to_string(I) + ".a");
    std::error_code WriteEC;
    llvm::raw_fd_ostream ArchiveStream(ArchivePath, WriteEC,
                                       llvm::sys::fs::OF_None);
    ASSERT_FALSE(WriteEC) << WriteEC.message();
    ArchiveStream << (*Archive)->getBuffer();
    ArchiveStream.close();
    LibraryOptions.push_back("-ltiny" + std::to_string(I));
  }

  const std::string LibrarySearch = "-L" + LibraryDirectory.str().str();
  llvm::SmallVector<const char *, 16> TinyLibraryArgs = {
      "neverc-test-linker", "-e", "_macho_tiny_entry", LibrarySearch.c_str(),
      ObjectPath.data()};
  for (const std::string &LibraryOption : LibraryOptions)
    TinyLibraryArgs.push_back(LibraryOption.c_str());

  unsigned TinyLibrariesThreads = 0;
  std::string TinyLibrariesImage;
  ASSERT_TRUE(Link(TinyLibraryArgs, /*RequestedThreads=*/0,
                   TinyLibrariesThreads, TinyLibrariesImage,
                   /*Sysroot=*/{}));
  EXPECT_EQ(TinyLibrariesThreads, 1U);
  EXPECT_EQ(TinyLibrariesImage, AutoImage)
      << "unextracted tiny libraries changed the linked image";

  llvm::SmallString<128> SysrootDirectory;
  ASSERT_FALSE(llvm::sys::fs::createUniqueDirectory(
      "neverc-macho-resolved-prefetch", SysrootDirectory));
  auto RemoveSysrootDirectory = llvm::make_scope_exit(
      [&] { (void)llvm::sys::fs::remove_directories(SysrootDirectory); });
  llvm::SmallString<128> RootedLibraryDirectory = SysrootDirectory;
  llvm::sys::path::append(RootedLibraryDirectory, "usr", "lib");
  ASSERT_FALSE(llvm::sys::fs::create_directories(RootedLibraryDirectory));
  llvm::SmallString<128> RootedArchivePath = RootedLibraryDirectory;
  llvm::sys::path::append(RootedArchivePath, "librooted.a");
  {
    std::error_code WriteEC;
    llvm::raw_fd_ostream ArchiveStream(RootedArchivePath, WriteEC,
                                       llvm::sys::fs::OF_None);
    ASSERT_FALSE(WriteEC) << WriteEC.message();
    ArchiveStream << (*Archive)->getBuffer();
  }

  // A resolved library path is already rooted. This deliberately creates a
  // large valid-Mach-O decoy at the location a second reroot would probe.
#ifndef _WIN32
  llvm::SmallString<256> DoubleRootedDecoyPath = SysrootDirectory.str();
  llvm::sys::path::append(DoubleRootedDecoyPath, RootedArchivePath);
  llvm::SmallString<256> DoubleRootedDecoyDirectory = DoubleRootedDecoyPath;
  llvm::sys::path::remove_filename(DoubleRootedDecoyDirectory);
  ASSERT_FALSE(llvm::sys::fs::create_directories(DoubleRootedDecoyDirectory));
  {
    std::error_code WriteEC;
    llvm::raw_fd_ostream DecoyStream(DoubleRootedDecoyPath, WriteEC,
                                     llvm::sys::fs::OF_None);
    ASSERT_FALSE(WriteEC) << WriteEC.message();
    DecoyStream.write(Object.data(), Object.size());
    std::string Padding(9U * 1024U * 1024U, '\0');
    DecoyStream << Padding;
  }
#endif

  const std::string RootLibrarySearch = "-L/usr/lib";
  llvm::SmallVector<const char *, 8> RootedLibraryArgs = {
      "neverc-test-linker",      "-e",
      "_macho_tiny_entry",       ObjectPath.data(),
      RootLibrarySearch.c_str(), "-lrooted"};
  unsigned RootedLibraryThreads = 0;
  std::string RootedLibraryImage;
  ASSERT_TRUE(Link(RootedLibraryArgs, /*RequestedThreads=*/0,
                   RootedLibraryThreads, RootedLibraryImage, SysrootDirectory));
  EXPECT_EQ(RootedLibraryThreads, 1U);
  EXPECT_EQ(RootedLibraryImage, AutoImage)
      << "resolved library prefetch rerooted an already rooted path";

  constexpr llvm::StringLiteral LargeAssembly = R"(
.text
.globl _macho_tiny_entry
_macho_tiny_entry:
  ret
.section __DATA,__large
.zero 17825792
)";
  llvm::SmallVector<char, 0> LargeObject;
  llvm::raw_svector_ostream LargeObjectStream(LargeObject);
  Request.Input = llvm::MemoryBufferRef(LargeAssembly, "macho-large-link.s");
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
  ASSERT_TRUE(Link(DirectArgs, /*RequestedThreads=*/0, LargeThreads, LargeImage,
                   /*Sysroot=*/{}));
  EXPECT_EQ(LargeThreads, std::min(16U, llvm::thread::hardware_concurrency()));
}

TEST(PluginMachOContextIsolationTest,
     AutomaticThreadsReconsiderNativeLTOOutputBeforeParsing) {
  if (llvm::thread::hardware_concurrency() < 2)
    GTEST_SKIP() << "automatic worker selection needs two available CPUs";

  constexpr uint64_t MiB = 1024ULL * 1024ULL;
  const uint64_t NativeObjectSizes[] = {4 * MiB, 4 * MiB};

  {
    LinkerExecutionContext Execution;
    MachOLinkerContext &Context = Execution.createBackend<MachOLinkerContext>();
    configureParallelismForLTONativeObjects(NativeObjectSizes);

    EXPECT_EQ(Context.parallelThreadCount(),
              std::min(16U, llvm::thread::hardware_concurrency()));
  }

  {
    LinkerExecutionContext Execution;
    MachOLinkerContext &Context = Execution.createBackend<MachOLinkerContext>();
    Context.configureParallel(/*RequestedThreads=*/1);
    configureParallelismForLTONativeObjects(NativeObjectSizes);

    EXPECT_EQ(Context.parallelThreadCount(), 1U);
  }
}

TEST(PluginMachOContextIsolationTest,
     AutomaticThreadsIncludeLateAutoLinkedAndCommandLineArchives) {
  if (llvm::thread::hardware_concurrency() < 2)
    GTEST_SKIP() << "automatic worker selection needs two available CPUs";

  initializeAssemblyTargets();
  const neverc::plugin::BuiltinTargetRoute *Route =
      neverc::plugin::findBuiltinTargetRoute("x86_64-apple-macosx13.0");
  ASSERT_NE(Route, nullptr);
  auto Target = neverc::plugin::lookupBuiltinLLVMTarget(*Route);
  ASSERT_TRUE(static_cast<bool>(Target))
      << llvm::toString(Target.takeError()).str().str();

  auto Assemble = [&](llvm::StringRef Source, llvm::StringRef Identifier) {
    llvm::SmallVector<char, 0> Object;
    llvm::raw_svector_ostream Stream(Object);
    neverc::plugin::BuiltinLLVMAsmParserRequest Request;
    Request.Target = *Target;
    Request.TargetTriple =
        llvm::Triple(llvm::Triple::normalize(Route->CanonicalTriple));
    Request.CPU = Route->DefaultCPU;
    Request.Input = llvm::MemoryBufferRef(Source, Identifier);
    Request.Output = &Stream;
    if (llvm::Error Error = neverc::plugin::runBuiltinLLVMAsmParser(Request))
      ADD_FAILURE() << llvm::toString(std::move(Error)).str().str();
    return Object;
  };

  constexpr llvm::StringLiteral DirectAssembly = R"(
.text
.globl _macho_lc_entry
_macho_lc_entry:
  callq _late_archive_symbol
  xorl %eax, %eax
  retq
.linker_option "-llateauto"
)";
  llvm::SmallVector<char, 0> DirectObject =
      Assemble(DirectAssembly, "macho-late-autolink.s");
  ASSERT_LT(DirectObject.size(), 8U * 1024U * 1024U);

  constexpr llvm::StringLiteral CommandLineAssembly = R"(
.text
.globl _macho_lc_entry
_macho_lc_entry:
  callq _late_archive_symbol
  xorl %eax, %eax
  retq
)";
  llvm::SmallVector<char, 0> CommandLineObject =
      Assemble(CommandLineAssembly, "macho-command-line-archive.s");
  ASSERT_LT(CommandLineObject.size(), 8U * 1024U * 1024U);

  constexpr llvm::StringLiteral MemberAssembly = R"(
.text
.globl _late_archive_symbol
_late_archive_symbol:
  retq
.section __DATA,__late
.zero 9437184
)";
  llvm::SmallVector<char, 0> MemberObject =
      Assemble(MemberAssembly, "macho-late-member.s");
  ASSERT_GT(MemberObject.size(), 8U * 1024U * 1024U);

  constexpr llvm::StringLiteral TinyMemberAssembly = R"(
.text
.globl _late_archive_symbol
_late_archive_symbol:
  retq
)";
  llvm::SmallVector<char, 0> TinyMemberObject =
      Assemble(TinyMemberAssembly, "macho-tiny-member.s");
  ASSERT_LT(TinyMemberObject.size(), 8U * 1024U * 1024U);

  llvm::SmallVector<llvm::NewArchiveMember, 1> Members;
  Members.emplace_back(llvm::MemoryBufferRef(
      llvm::StringRef(MemberObject.data(), MemberObject.size()),
      "late-auto-member.o"));
  auto Archive = llvm::writeArchiveToBuffer(
      Members, llvm::SymtabWritingMode::NormalSymtab,
      llvm::object::Archive::K_DARWIN, /*Deterministic=*/true,
      /*Thin=*/false);
  ASSERT_TRUE(static_cast<bool>(Archive))
      << llvm::toString(Archive.takeError()).str().str();

  llvm::SmallVector<llvm::NewArchiveMember, 1> TinyMembers;
  TinyMembers.emplace_back(llvm::MemoryBufferRef(
      llvm::StringRef(TinyMemberObject.data(), TinyMemberObject.size()),
      "tiny-auto-member.o"));
  auto TinyArchive = llvm::writeArchiveToBuffer(
      TinyMembers, llvm::SymtabWritingMode::NormalSymtab,
      llvm::object::Archive::K_DARWIN, /*Deterministic=*/true,
      /*Thin=*/false);
  ASSERT_TRUE(static_cast<bool>(TinyArchive))
      << llvm::toString(TinyArchive.takeError()).str().str();

  neverc::InMemoryFileStore &Store = neverc::InMemoryFileStore::instance();
  Store.clear();
  auto ClearStore = llvm::make_scope_exit([&] { Store.clear(); });
  constexpr llvm::StringLiteral ObjectPath = "/virtual/macho-autolink.o";
  constexpr llvm::StringLiteral CommandLineObjectPath =
      "/virtual/macho-command-line-archive.o";
  llvm::SmallString<0> &ObjectBytes =
      Store.create(ObjectPath, DirectObject.size());
  ObjectBytes.append(DirectObject.begin(), DirectObject.end());
  llvm::SmallString<0> &CommandLineObjectBytes =
      Store.create(CommandLineObjectPath, CommandLineObject.size());
  CommandLineObjectBytes.append(CommandLineObject.begin(),
                                CommandLineObject.end());
  Store.freeze();

  llvm::SmallString<128> LibraryDirectory;
  ASSERT_FALSE(llvm::sys::fs::createUniqueDirectory(
      "neverc-macho-late-autolink", LibraryDirectory));
  auto RemoveLibraryDirectory = llvm::make_scope_exit(
      [&] { (void)llvm::sys::fs::remove_directories(LibraryDirectory); });
  llvm::SmallString<128> ArchivePath = LibraryDirectory;
  llvm::sys::path::append(ArchivePath, "liblateauto.a");
  {
    std::error_code WriteEC;
    llvm::raw_fd_ostream ArchiveStream(ArchivePath, WriteEC,
                                       llvm::sys::fs::OF_None);
    ASSERT_FALSE(WriteEC) << WriteEC.message();
    ArchiveStream << (*Archive)->getBuffer();
  }
  llvm::SmallString<128> TinyArchivePath = LibraryDirectory;
  llvm::sys::path::append(TinyArchivePath, "libtinyparse.a");
  {
    std::error_code WriteEC;
    llvm::raw_fd_ostream ArchiveStream(TinyArchivePath, WriteEC,
                                       llvm::sys::fs::OF_None);
    ASSERT_FALSE(WriteEC) << WriteEC.message();
    ArchiveStream << (*TinyArchive)->getBuffer();
  }
  llvm::SmallString<128> ThinMemberPath = LibraryDirectory;
  llvm::sys::path::append(ThinMemberPath, "large-thin-member.o");
  {
    std::error_code WriteEC;
    llvm::raw_fd_ostream MemberStream(ThinMemberPath, WriteEC,
                                      llvm::sys::fs::OF_None);
    ASSERT_FALSE(WriteEC) << WriteEC.message();
    MemberStream.write(MemberObject.data(), MemberObject.size());
  }
  auto ThinMember =
      llvm::NewArchiveMember::getFile(ThinMemberPath, /*Deterministic=*/true);
  ASSERT_TRUE(static_cast<bool>(ThinMember))
      << llvm::toString(ThinMember.takeError()).str().str();
  llvm::SmallVector<llvm::NewArchiveMember, 1> ThinMembers;
  ThinMembers.push_back(std::move(*ThinMember));
  auto ThinArchive = llvm::writeArchiveToBuffer(
      ThinMembers, llvm::SymtabWritingMode::NormalSymtab,
      llvm::object::Archive::K_GNU, /*Deterministic=*/true,
      /*Thin=*/true);
  ASSERT_TRUE(static_cast<bool>(ThinArchive))
      << llvm::toString(ThinArchive.takeError()).str().str();
  llvm::SmallString<128> ThinArchivePath = LibraryDirectory;
  llvm::sys::path::append(ThinArchivePath, "libthinparse.a");
  {
    std::error_code WriteEC;
    llvm::raw_fd_ostream ArchiveStream(ThinArchivePath, WriteEC,
                                       llvm::sys::fs::OF_None);
    ASSERT_FALSE(WriteEC) << WriteEC.message();
    ArchiveStream << (*ThinArchive)->getBuffer();
  }

  llvm::SmallString<128> OutputPath;
  std::error_code EC = llvm::sys::fs::createTemporaryFile(
      "neverc-macho-late-autolink", "macho", OutputPath);
  ASSERT_FALSE(EC) << EC.message();
  llvm::FileRemover RemoveOutput(OutputPath);

  const std::string LibrarySearch = "-L" + LibraryDirectory.str().str();
  auto RunLink = [&](llvm::StringRef InputPath,
                     llvm::StringRef CommandLineLibrary,
                     unsigned RequestedThreads, unsigned &SelectedThreads,
                     unsigned &ArchiveMemberParseThreads, std::string &Image,
                     ArchiveMemberParseObservation *Observation) {
    llvm::SmallVector<const char *, 6> Args = {
        "neverc-test-linker", "-e", "_macho_lc_entry", LibrarySearch.c_str()};
    std::string CommandLineLibraryOption;
    if (!CommandLineLibrary.empty()) {
      CommandLineLibraryOption = "-l" + CommandLineLibrary.str();
      Args.push_back(CommandLineLibraryOption.c_str());
    }
    Args.push_back(InputPath.data());
    LinkerExecutionContext Execution;
    LinkerDriverConfig Config;
    Config.executionContext = &Execution;
    Config.outputFile = OutputPath.str().str();
    Config.threadCount = RequestedThreads;
    Config.archName = "x86_64";
    Config.platformName = "macos";
    Config.platformMinVersion = "13.0";
    Config.platformSdkVersion = "13.0";
    Config.nostdlib = true;
    std::string Stdout;
    std::string Stderr;
    llvm::raw_string_ostream StdoutStream(Stdout);
    llvm::raw_string_ostream StderrStream(Stderr);
    if (!linker::macho::link(Args, StdoutStream, StderrStream,
                             /*exitEarly=*/false,
                             /*disableOutput=*/false, Config)) {
      ADD_FAILURE() << Stderr;
      return false;
    }
    if (!Execution.common()) {
      ADD_FAILURE() << "Mach-O execution did not retain its linker context";
      return false;
    }
    SelectedThreads = Execution.common()->parallelThreadCount();
    if (Observation) {
      if (Observation->Calls != 1) {
        ADD_FAILURE() << "expected exactly one parsed archive member, observed "
                      << Observation->Calls;
        return false;
      }
      ArchiveMemberParseThreads = Observation->SelectedThreads;
    }
    auto Buffer = llvm::MemoryBuffer::getFile(OutputPath);
    if (!Buffer) {
      ADD_FAILURE() << Buffer.getError().message();
      return false;
    }
    Image = (*Buffer)->getBuffer().str();
    return true;
  };
  auto Link = [&](llvm::StringRef InputPath, llvm::StringRef CommandLineLibrary,
                  unsigned RequestedThreads, unsigned &SelectedThreads,
                  unsigned &ArchiveMemberParseThreads, std::string &Image) {
    ArchiveMemberParseObservation Observation;
    setArchiveMemberParseObserverForTesting(observeArchiveMemberParse,
                                            &Observation);
    auto ResetObserver = llvm::make_scope_exit(
        [] { setArchiveMemberParseObserverForTesting(nullptr); });
    return RunLink(InputPath, CommandLineLibrary, RequestedThreads,
                   SelectedThreads, ArchiveMemberParseThreads, Image,
                   &Observation);
  };

  unsigned AutoThreads = 0;
  unsigned SerialThreads = 0;
  unsigned AutoParseThreads = 0;
  unsigned SerialParseThreads = 0;
  std::string AutoImage;
  std::string SerialImage;
  ASSERT_TRUE(Link(ObjectPath, /*CommandLineLibrary=*/{},
                   /*RequestedThreads=*/0, AutoThreads, AutoParseThreads,
                   AutoImage));
  ASSERT_TRUE(Link(ObjectPath, /*CommandLineLibrary=*/{},
                   /*RequestedThreads=*/1, SerialThreads, SerialParseThreads,
                   SerialImage));
  EXPECT_EQ(AutoThreads, std::min(16U, llvm::thread::hardware_concurrency()));
  EXPECT_EQ(SerialThreads, 1U);
  EXPECT_GT(AutoParseThreads, 1U);
  EXPECT_EQ(SerialParseThreads, 1U);
  EXPECT_EQ(AutoImage, SerialImage)
      << "late auto-linked archive changed output across worker budgets";

  unsigned CommandLineAutoThreads = 0;
  unsigned CommandLineSerialThreads = 0;
  unsigned CommandLineAutoParseThreads = 0;
  unsigned CommandLineSerialParseThreads = 0;
  std::string CommandLineAutoImage;
  std::string CommandLineSerialImage;
  ASSERT_TRUE(Link(CommandLineObjectPath, /*CommandLineLibrary=*/"lateauto",
                   /*RequestedThreads=*/0, CommandLineAutoThreads,
                   CommandLineAutoParseThreads, CommandLineAutoImage));
  ASSERT_TRUE(Link(CommandLineObjectPath, /*CommandLineLibrary=*/"lateauto",
                   /*RequestedThreads=*/1, CommandLineSerialThreads,
                   CommandLineSerialParseThreads, CommandLineSerialImage));
  EXPECT_EQ(CommandLineAutoThreads,
            std::min(16U, llvm::thread::hardware_concurrency()));
  EXPECT_EQ(CommandLineSerialThreads, 1U);
  EXPECT_GT(CommandLineAutoParseThreads, 1U);
  EXPECT_EQ(CommandLineSerialParseThreads, 1U);
  EXPECT_EQ(CommandLineAutoImage, CommandLineSerialImage)
      << "command-line archive changed output across worker budgets";

  unsigned TinyAutoThreads = 0;
  unsigned TinySerialThreads = 0;
  unsigned TinyAutoParseThreads = 0;
  unsigned TinySerialParseThreads = 0;
  std::string TinyAutoImage;
  std::string TinySerialImage;
  ASSERT_TRUE(Link(CommandLineObjectPath,
                   /*CommandLineLibrary=*/"tinyparse",
                   /*RequestedThreads=*/0, TinyAutoThreads,
                   TinyAutoParseThreads, TinyAutoImage));
  ASSERT_TRUE(Link(CommandLineObjectPath,
                   /*CommandLineLibrary=*/"tinyparse",
                   /*RequestedThreads=*/1, TinySerialThreads,
                   TinySerialParseThreads, TinySerialImage));
  EXPECT_EQ(TinyAutoThreads, 1U);
  EXPECT_EQ(TinySerialThreads, 1U);
  EXPECT_EQ(TinyAutoParseThreads, 1U);
  EXPECT_EQ(TinySerialParseThreads, 1U);
  EXPECT_EQ(TinyAutoImage, TinySerialImage)
      << "tiny archive member changed output across worker budgets";

  unsigned ThinAutoThreads = 0;
  unsigned ThinSerialThreads = 0;
  unsigned ThinAutoParseThreads = 0;
  unsigned ThinSerialParseThreads = 0;
  std::string ThinAutoImage;
  std::string ThinSerialImage;
  ASSERT_TRUE(Link(CommandLineObjectPath,
                   /*CommandLineLibrary=*/"thinparse",
                   /*RequestedThreads=*/0, ThinAutoThreads,
                   ThinAutoParseThreads, ThinAutoImage));
  ASSERT_TRUE(Link(CommandLineObjectPath,
                   /*CommandLineLibrary=*/"thinparse",
                   /*RequestedThreads=*/1, ThinSerialThreads,
                   ThinSerialParseThreads, ThinSerialImage));
  EXPECT_EQ(ThinAutoThreads,
            std::min(16U, llvm::thread::hardware_concurrency()));
  EXPECT_EQ(ThinSerialThreads, 1U);
  EXPECT_GT(ThinAutoParseThreads, 1U);
  EXPECT_EQ(ThinSerialParseThreads, 1U);
  EXPECT_EQ(ThinAutoImage, ThinSerialImage)
      << "thin archive member changed output across worker budgets";

  // Keep the last observation alive across a subsequent unobserved link. If
  // the scope guard stops clearing the TLS hook, that link calls the stale
  // observer and increments this counter.
  ArchiveMemberParseObservation ClearedObservation;
  unsigned ObservedThreads = 0;
  unsigned ObservedParseThreads = 0;
  std::string ObservedImage;
  {
    setArchiveMemberParseObserverForTesting(observeArchiveMemberParse,
                                            &ClearedObservation);
    auto ResetObserver = llvm::make_scope_exit(
        [] { setArchiveMemberParseObserverForTesting(nullptr); });
    ASSERT_TRUE(
        RunLink(CommandLineObjectPath, /*CommandLineLibrary=*/"tinyparse",
                /*RequestedThreads=*/1, ObservedThreads, ObservedParseThreads,
                ObservedImage, &ClearedObservation));
  }
  ASSERT_EQ(ClearedObservation.Calls, 1U);
  const unsigned CallsAfterScope = ClearedObservation.Calls;
  unsigned UnobservedThreads = 0;
  unsigned UnobservedParseThreads = 0;
  std::string UnobservedImage;
  ASSERT_TRUE(RunLink(CommandLineObjectPath,
                      /*CommandLineLibrary=*/"tinyparse",
                      /*RequestedThreads=*/1, UnobservedThreads,
                      UnobservedParseThreads, UnobservedImage,
                      /*Observation=*/nullptr));
  EXPECT_EQ(ClearedObservation.Calls, CallsAfterScope);
}

TEST(PluginMachOContextIsolationTest,
     AutomaticThreadsIncludeLargeSectCreatePayloads) {
  if (llvm::thread::hardware_concurrency() < 2)
    GTEST_SKIP() << "automatic worker selection needs two available CPUs";

  initializeAssemblyTargets();
  const neverc::plugin::BuiltinTargetRoute *Route =
      neverc::plugin::findBuiltinTargetRoute("x86_64-apple-macosx13.0");
  ASSERT_NE(Route, nullptr);
  auto Target = neverc::plugin::lookupBuiltinLLVMTarget(*Route);
  ASSERT_TRUE(static_cast<bool>(Target))
      << llvm::toString(Target.takeError()).str().str();

  constexpr llvm::StringLiteral Assembly = R"(
.text
.globl _macho_sectcreate_entry
_macho_sectcreate_entry:
  retq
)";
  llvm::SmallVector<char, 0> Object;
  llvm::raw_svector_ostream ObjectStream(Object);
  neverc::plugin::BuiltinLLVMAsmParserRequest Request;
  Request.Target = *Target;
  Request.TargetTriple =
      llvm::Triple(llvm::Triple::normalize(Route->CanonicalTriple));
  Request.CPU = Route->DefaultCPU;
  Request.Input = llvm::MemoryBufferRef(Assembly, "macho-sectcreate.s");
  Request.Output = &ObjectStream;
  if (llvm::Error Error = neverc::plugin::runBuiltinLLVMAsmParser(Request))
    FAIL() << llvm::toString(std::move(Error)).str().str();

  neverc::InMemoryFileStore &Store = neverc::InMemoryFileStore::instance();
  Store.clear();
  auto ClearStore = llvm::make_scope_exit([&] { Store.clear(); });
  constexpr llvm::StringLiteral ObjectPath = "/virtual/macho-sectcreate.o";
  llvm::SmallString<0> &ObjectBytes = Store.create(ObjectPath, Object.size());
  ObjectBytes.append(Object.begin(), Object.end());
  Store.freeze();

  llvm::SmallString<128> PayloadPath;
  std::error_code EC = llvm::sys::fs::createTemporaryFile(
      "neverc-macho-sectcreate", "payload", PayloadPath);
  ASSERT_FALSE(EC) << EC.message();
  llvm::FileRemover RemovePayload(PayloadPath);
  {
    std::error_code WriteEC;
    llvm::raw_fd_ostream PayloadStream(PayloadPath, WriteEC,
                                       llvm::sys::fs::OF_None);
    ASSERT_FALSE(WriteEC) << WriteEC.message();
    std::string Payload(9U * 1024U * 1024U, '\x5a');
    PayloadStream << Payload;
  }

  llvm::SmallString<128> OutputPath;
  EC = llvm::sys::fs::createTemporaryFile("neverc-macho-sectcreate", "macho",
                                          OutputPath);
  ASSERT_FALSE(EC) << EC.message();
  llvm::FileRemover RemoveOutput(OutputPath);

  const std::string PayloadPathString = PayloadPath.str().str();
  const char *Args[] = {"neverc-test-linker",
                        "-e",
                        "_macho_sectcreate_entry",
                        "-sectcreate",
                        "__DATA",
                        "__payload",
                        PayloadPathString.c_str(),
                        ObjectPath.data()};
  auto Link = [&](unsigned RequestedThreads, unsigned &SelectedThreads,
                  std::string &Image) {
    LinkerExecutionContext Execution;
    LinkerDriverConfig Config;
    Config.executionContext = &Execution;
    Config.outputFile = OutputPath.str().str();
    Config.threadCount = RequestedThreads;
    Config.archName = "x86_64";
    Config.platformName = "macos";
    Config.platformMinVersion = "13.0";
    Config.platformSdkVersion = "13.0";
    Config.nostdlib = true;
    std::string Stdout;
    std::string Stderr;
    llvm::raw_string_ostream StdoutStream(Stdout);
    llvm::raw_string_ostream StderrStream(Stderr);
    if (!linker::macho::link(Args, StdoutStream, StderrStream,
                             /*exitEarly=*/false,
                             /*disableOutput=*/false, Config)) {
      ADD_FAILURE() << Stderr;
      return false;
    }
    if (!Execution.common()) {
      ADD_FAILURE() << "Mach-O execution did not retain its linker context";
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
  EXPECT_EQ(AutoImage, SerialImage)
      << "-sectcreate output changed across worker budgets";
}
