#include "NeverCTestFixture.h"

#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Error.h"

#include <limits>
#include <utility>

namespace {

// A minimal Android GKI kernel module.  `-fandroid-kernel-driver-mode` supplies
// <nvkmod.h> and the module scaffolding macros, so this compiles with no kernel
// source tree, on any host.
constexpr const char *kAndroidKernelModule =
    "#include <nvkmod.h>\n"
    "static int m_init(void) { return 0; }\n"
    "static void m_exit(void) {}\n"
    "module_init(m_init);\n"
    "module_exit(m_exit);\n"
    "MODULE_LICENSE(\"GPL v2\");\n"
    "NEVERC_KRT_DEFINE_MODULE(\"neverc_test_hello\");\n";

bool isElfImage(const std::string &Bytes) {
  return Bytes.size() > 4 && static_cast<unsigned char>(Bytes[0]) == 0x7f &&
         Bytes[1] == 'E' && Bytes[2] == 'L' && Bytes[3] == 'F';
}

std::string renderError(llvm::Error Error) {
  return llvm::toString(std::move(Error)).str().str();
}

::testing::AssertionResult
hasAndroidLoaderContract(const std::string &Bytes, bool RequireEmptyTags,
                         bool RequirePopulatedTags = false) {
  using namespace llvm;
  using namespace llvm::object;

  auto ObjectOrErr = ObjectFile::createObjectFile(
      MemoryBufferRef(StringRef(Bytes.data(), Bytes.size()), "module.ko"));
  if (!ObjectOrErr)
    return ::testing::AssertionFailure()
           << "cannot parse .ko: " << renderError(ObjectOrErr.takeError());
  const auto *Object = dyn_cast<ELF64LEObjectFile>(ObjectOrErr->get());
  if (!Object)
    return ::testing::AssertionFailure() << ".ko is not ELF64 little-endian";
  const auto *ELFObject = static_cast<const ELFObjectFileBase *>(Object);
  if (ELFObject->getEMachine() != ELF::EM_AARCH64 ||
      ELFObject->getEType() != ELF::ET_REL)
    return ::testing::AssertionFailure()
           << ".ko is not an AArch64 ET_REL object";

  uint64_t VersionsCount = 0;
  uint64_t AllocTagsCount = 0;
  uint64_t RawAllocTagsCount = 0;
  uint64_t AllocTagsIndex = std::numeric_limits<uint64_t>::max();
  uint64_t AllocTagsSize = 0;
  auto HasValidAlignment = [](uint64_t Alignment) {
    return Alignment >= 8 && (Alignment & (Alignment - 1)) == 0;
  };
  for (SectionRef Section : Object->sections()) {
    auto NameOrErr = Section.getName();
    if (!NameOrErr)
      return ::testing::AssertionFailure()
             << "cannot read section name: "
             << renderError(NameOrErr.takeError());
    const ELFSectionRef ELFSection(Section);
    const uint64_t Alignment = Section.getAlignment().value();
    if (*NameOrErr == "__versions") {
      ++VersionsCount;
      if (ELFSection.getType() != ELF::SHT_PROGBITS ||
          !(ELFSection.getFlags() & ELF::SHF_ALLOC) ||
          (ELFSection.getFlags() & ELF::SHF_COMPRESSED) ||
          !HasValidAlignment(Alignment) || Section.getSize() % 64 != 0)
        return ::testing::AssertionFailure()
               << "__versions has an invalid type/flags/alignment/size";
    } else if (*NameOrErr == ".codetag.alloc_tags") {
      ++AllocTagsCount;
      AllocTagsIndex = Section.getIndex();
      AllocTagsSize = Section.getSize();
      const uint64_t RequiredFlags = ELF::SHF_ALLOC | ELF::SHF_WRITE;
      if (ELFSection.getType() != ELF::SHT_PROGBITS ||
          (ELFSection.getFlags() & RequiredFlags) != RequiredFlags ||
          (ELFSection.getFlags() & ELF::SHF_COMPRESSED) ||
          !HasValidAlignment(Alignment))
        return ::testing::AssertionFailure()
               << ".codetag.alloc_tags has invalid type/flags/alignment";
    } else if (*NameOrErr == "alloc_tags") {
      ++RawAllocTagsCount;
    } else if (*NameOrErr == ".neverc.android.kernel.profile") {
      return ::testing::AssertionFailure()
             << "delivered .ko retained NeverC profile-contract section";
    }
  }
  if (VersionsCount != 1 || AllocTagsCount != 1 || RawAllocTagsCount != 0)
    return ::testing::AssertionFailure()
           << "loader sections: __versions=" << VersionsCount
           << " .codetag.alloc_tags=" << AllocTagsCount
           << " raw alloc_tags=" << RawAllocTagsCount;
  if (RequireEmptyTags && AllocTagsSize != 0)
    return ::testing::AssertionFailure()
           << "zero-import module unexpectedly contains alloc_tags";
  if (RequirePopulatedTags && AllocTagsSize == 0)
    return ::testing::AssertionFailure()
           << "module's compiler alloc_tags input was not collected";

  struct Boundary {
    unsigned Count = 0;
    uint8_t Binding = ELF::STB_LOCAL;
    uint8_t Type = ELF::STT_NOTYPE;
    uint64_t SectionIndex = std::numeric_limits<uint64_t>::max();
    uint64_t Value = 0;
  } Start, Stop;
  for (SymbolRef Symbol : Object->symbols()) {
    auto NameOrErr = Symbol.getName();
    if (!NameOrErr)
      return ::testing::AssertionFailure()
             << "cannot read symbol name: "
             << renderError(NameOrErr.takeError());
    if (*NameOrErr == "__neverc_android_kernel_profile_contract")
      return ::testing::AssertionFailure()
             << "delivered .ko retained NeverC profile-contract symbol";
    Boundary *Result = nullptr;
    if (*NameOrErr == "__start_alloc_tags")
      Result = &Start;
    else if (*NameOrErr == "__stop_alloc_tags")
      Result = &Stop;
    if (!Result)
      continue;

    ++Result->Count;
    const ELFSymbolRef ELFSymbol(Symbol);
    Result->Binding = ELFSymbol.getBinding();
    Result->Type = ELFSymbol.getELFType();
    auto SectionOrErr = Symbol.getSection();
    if (!SectionOrErr)
      return ::testing::AssertionFailure()
             << "cannot read boundary section: "
             << renderError(SectionOrErr.takeError());
    if (*SectionOrErr != Object->section_end())
      Result->SectionIndex = (*SectionOrErr)->getIndex();
    auto ValueOrErr = Symbol.getValue();
    if (!ValueOrErr)
      return ::testing::AssertionFailure()
             << "cannot read boundary value: "
             << renderError(ValueOrErr.takeError());
    Result->Value = *ValueOrErr;
  }
  if (Start.Count != 1 || Stop.Count != 1 || Start.Binding != ELF::STB_GLOBAL ||
      Stop.Binding != ELF::STB_GLOBAL || Start.Type != ELF::STT_NOTYPE ||
      Stop.Type != ELF::STT_NOTYPE || Start.SectionIndex != AllocTagsIndex ||
      Stop.SectionIndex != AllocTagsIndex || Start.Value != 0 ||
      Stop.Value != AllocTagsSize)
    return ::testing::AssertionFailure()
           << "alloc_tags boundary contract is invalid: start(count="
           << Start.Count << ", section=" << Start.SectionIndex
           << ", value=" << Start.Value << ") stop(count=" << Stop.Count
           << ", section=" << Stop.SectionIndex << ", value=" << Stop.Value
           << ") expected section=" << AllocTagsIndex
           << " size=" << AllocTagsSize;
  return ::testing::AssertionSuccess();
}

} // namespace

class AndroidKernelRuntimeTest : public NeverCTest {
protected:
  // Relocatable (`-r`) Android kernel-module link.  An empty PluginPath uses the
  // native driver directly; otherwise the plugin is loaded with -fplugin, which
  // routes the relocatable link through the plugin object-merge bridge.
  CmdResult linkKernelModule(const std::string &PluginPath,
                             const fs::path &Source, const fs::path &Output) {
    return linkKernelModule(PluginPath, std::vector<fs::path>{Source}, Output);
  }

  CmdResult linkKernelModule(const std::string &PluginPath,
                             const std::vector<fs::path> &Sources,
                             const fs::path &Output) {
    std::vector<std::string> Args;
    if (!PluginPath.empty())
      Args.push_back("-fplugin=" + PluginPath);
    Args.push_back("--target=aarch64-linux-android");
    Args.push_back("-fandroid-kernel-driver-mode");
    Args.push_back("-DNVK_KERNEL=510");
    Args.push_back("-nostdlib");
    Args.push_back("-r");
    for (const fs::path &Source : Sources)
      Args.push_back(Source.string());
    Args.push_back("-o");
    Args.push_back(Output.string());
    return ncc(Args);
  }
};

TEST_F(AndroidKernelRuntimeTest, EmbeddedRuntimeLinkage) {
  if (isWindows())
    GTEST_SKIP() << "NVK runtime linkage test requires a POSIX shell";

  const fs::path Script =
      testDir() / "../../runtime/android/kernel/tools/test-runtime-linkage.sh";
  ASSERT_TRUE(fs::exists(Script)) << Script;

  const CmdResult Result =
      exec("bash", {Script.string(), neverc().string(), "--smoke"});
  EXPECT_EQ(Result.exitCode, 0) << Result.out << Result.err;
}

TEST_F(AndroidKernelRuntimeTest, PublicSdkLayouts) {
  if (isWindows())
    GTEST_SKIP() << "NVK SDK layout test requires a POSIX shell";

  const fs::path Script =
      testDir() / "../../runtime/android/kernel/tools/test-sdk-layouts.sh";
  ASSERT_TRUE(fs::exists(Script)) << Script;

  const CmdResult Result = exec("sh", {Script.string(), neverc().string()});
  EXPECT_EQ(Result.exitCode, 0) << Result.out << Result.err;
}

// `-fandroid-kernel-driver-mode` implies `-flto=full`, so a relocatable (`-r`)
// link receives LLVM bitcode rather than native objects.  With a plugin loaded,
// the relocatable link is routed through the plugin object-merge bridge, which
// must first lower the bitcode to native objects before merging.  A plugin that
// binds no object phase has nothing to contribute to a built-in target's merge,
// so the bridge must transparently defer the whole link to the native driver and
// produce a byte-identical .ko.
TEST_F(AndroidKernelRuntimeTest, RelocatablePluginLinkDefersToNativeByteForByte) {
  const fs::path Source = tmpFile("nvk_defer.c");
  writeFile(Source, kAndroidKernelModule);
  const fs::path NativeKo = tmpFile("nvk_defer_native.ko");
  const fs::path PluginKo = tmpFile("nvk_defer_plugin.ko");

  const CmdResult Native = linkKernelModule(/*PluginPath=*/"", Source, NativeKo);
  ASSERT_EQ(Native.exitCode, 0) << Native.err;
  const CmdResult Plugin =
      linkKernelModule(NEVERC_TEST_EMPTY_PLUGIN, Source, PluginKo);
  ASSERT_EQ(Plugin.exitCode, 0) << Plugin.err;

  const std::string NativeBytes = readFile(NativeKo);
  const std::string PluginBytes = readFile(PluginKo);
  ASSERT_TRUE(isElfImage(NativeBytes)) << "native .ko is not an ELF image";
  EXPECT_TRUE(hasAndroidLoaderContract(NativeBytes, /*RequireEmptyTags=*/true));
  EXPECT_TRUE(hasAndroidLoaderContract(PluginBytes, /*RequireEmptyTags=*/true));
  EXPECT_EQ(NativeBytes, PluginBytes)
      << "a no-op plugin must not perturb the relocatable Android link";
}

// The same relocatable Android link, but through a plugin that binds an object
// post-write phase.  That binding forces the full plugin path: lower the LTO
// bitcode to native objects (runPluginRelocatableLTO), merge them via the
// built-in object merger, then run the object write pipeline.  The result must
// match the native .ko byte-for-byte except for the single marker byte the
// plugin deliberately writes -- proving the LTO-lowering and merge stages are
// faithful to the native `-r` link.
TEST_F(AndroidKernelRuntimeTest, RelocatablePluginLinkLowersLTOBitcodeAndMerges) {
  const fs::path Source = tmpFile("nvk_merge.c");
  writeFile(Source, kAndroidKernelModule);
  const fs::path NativeKo = tmpFile("nvk_merge_native.ko");
  const fs::path PluginKo = tmpFile("nvk_merge_plugin.ko");

  const CmdResult Native = linkKernelModule(/*PluginPath=*/"", Source, NativeKo);
  ASSERT_EQ(Native.exitCode, 0) << Native.err;
  const CmdResult Plugin =
      linkKernelModule(NEVERC_TEST_OBJECT_POST_WRITE_PLUGIN, Source, PluginKo);
  ASSERT_EQ(Plugin.exitCode, 0) << Plugin.err;

  const std::string NativeBytes = readFile(NativeKo);
  const std::string PluginBytes = readFile(PluginKo);
  ASSERT_TRUE(isElfImage(NativeBytes)) << "native .ko is not an ELF image";
  EXPECT_TRUE(hasAndroidLoaderContract(NativeBytes, /*RequireEmptyTags=*/true));
  EXPECT_TRUE(hasAndroidLoaderContract(PluginBytes, /*RequireEmptyTags=*/true));
  ASSERT_EQ(NativeBytes.size(), PluginBytes.size())
      << "plugin LTO merge changed the .ko size";

  size_t Differences = 0;
  size_t FirstDiff = 0;
  for (size_t I = 0; I != NativeBytes.size(); ++I) {
    if (NativeBytes[I] != PluginBytes[I]) {
      if (Differences == 0)
        FirstDiff = I;
      ++Differences;
    }
  }
  // Exactly one byte -- the plugin's marker (0x42 at offset 9) -- may differ;
  // any other divergence means the LTO-lower + merge path is not byte-faithful.
  EXPECT_EQ(Differences, 1U)
      << "plugin LTO-lower + merge diverged from the native relocatable link";
  EXPECT_EQ(FirstDiff, 9U);
  EXPECT_EQ(static_cast<unsigned char>(PluginBytes[FirstDiff]), 0x42U);
}

// The same path with more than one translation unit.
//
// Choosing which definition of a name prevails needs every input's symbols
// before any of them is handed to the LTO driver, so the winners are recorded
// in one pass and consulted in a second -- and handing an input over destroys
// it. With a single translation unit nothing is handed over before the last
// lookup, so the one-file test above never exercises the second pass against
// an input that is already gone; this one does.
//
// Both links must produce the same object apart from the plugin's marker
// byte: a prevailing definition that gets recorded as non-prevailing is one
// LTO drops.
TEST_F(AndroidKernelRuntimeTest, RelocatablePluginLinkHandlesSeveralInputs) {
  const fs::path First = tmpFile("nvk_multi_a.c");
  const fs::path Second = tmpFile("nvk_multi_b.c");
  writeFile(First,
            "#include <nvkmod.h>\n"
            "extern int nvk_multi_helper(int x);\n"
            "static int m_init(void) { return nvk_multi_helper(1); }\n"
            "static void m_exit(void) {}\n"
            "module_init(m_init);\n"
            "module_exit(m_exit);\n"
            "MODULE_LICENSE(\"GPL v2\");\n"
            "NEVERC_KRT_DEFINE_MODULE(\"neverc_test_multi\");\n");
  // A static helper of the same name in both units, so a lookup for the second
  // unit's copy hashes onto the entry recorded for the first one and compares
  // against it.
  writeFile(Second, "static int scale(int v) { return v * 3; }\n"
                    "int nvk_multi_helper(int x) { return scale(x) + 1; }\n");
  const fs::path NativeKo = tmpFile("nvk_multi_native.ko");
  const fs::path PluginKo = tmpFile("nvk_multi_plugin.ko");
  const std::vector<fs::path> Sources = {First, Second};

  const CmdResult Native =
      linkKernelModule(/*PluginPath=*/"", Sources, NativeKo);
  ASSERT_EQ(Native.exitCode, 0) << Native.err;
  const CmdResult Plugin = linkKernelModule(
      NEVERC_TEST_OBJECT_POST_WRITE_PLUGIN, Sources, PluginKo);
  ASSERT_EQ(Plugin.exitCode, 0) << Plugin.err;

  const std::string NativeBytes = readFile(NativeKo);
  const std::string PluginBytes = readFile(PluginKo);
  ASSERT_TRUE(isElfImage(NativeBytes)) << "native .ko is not an ELF image";
  EXPECT_TRUE(hasAndroidLoaderContract(NativeBytes, /*RequireEmptyTags=*/true));
  EXPECT_TRUE(hasAndroidLoaderContract(PluginBytes, /*RequireEmptyTags=*/true));
  ASSERT_EQ(NativeBytes.size(), PluginBytes.size())
      << "plugin LTO merge changed the .ko size";
  size_t Differences = 0;
  for (size_t I = 0; I != NativeBytes.size(); ++I)
    if (NativeBytes[I] != PluginBytes[I])
      ++Differences;
  EXPECT_EQ(Differences, 1U)
      << "plugin LTO-lower + merge over several inputs diverged from the "
         "native relocatable link";
}

TEST_F(AndroidKernelRuntimeTest, RelocatableLinkCollectsCompilerAllocTags) {
  const fs::path Source = tmpFile("nvk_alloc_tags.c");
  writeFile(Source,
            "#include <nvkmod.h>\n"
            "static unsigned long tag __attribute__((section(\"alloc_tags\"), "
            "used, aligned(8))) = 0x1234;\n"
            "static int m_init(void) { return tag == 0x1234 ? 0 : -1; }\n"
            "static void m_exit(void) {}\n"
            "module_init(m_init);\n"
            "module_exit(m_exit);\n"
            "MODULE_LICENSE(\"GPL v2\");\n"
            "NEVERC_KRT_DEFINE_MODULE(\"neverc_test_tags\");\n");
  const fs::path NativeKo = tmpFile("nvk_alloc_tags_native.ko");
  const fs::path PluginKo = tmpFile("nvk_alloc_tags_plugin.ko");

  const CmdResult Native =
      linkKernelModule(/*PluginPath=*/"", Source, NativeKo);
  ASSERT_EQ(Native.exitCode, 0) << Native.err;
  const CmdResult Plugin =
      linkKernelModule(NEVERC_TEST_OBJECT_POST_WRITE_PLUGIN, Source, PluginKo);
  ASSERT_EQ(Plugin.exitCode, 0) << Plugin.err;

  EXPECT_TRUE(hasAndroidLoaderContract(readFile(NativeKo),
                                       /*RequireEmptyTags=*/false,
                                       /*RequirePopulatedTags=*/true));
  EXPECT_TRUE(hasAndroidLoaderContract(readFile(PluginKo),
                                       /*RequireEmptyTags=*/false,
                                       /*RequirePopulatedTags=*/true));
}
