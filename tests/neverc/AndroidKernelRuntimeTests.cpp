#include "NeverCTestFixture.h"

#include "neverc/Foundation/AndroidKernelModuleReleaseNames.h"
#include "neverc/Linker/Core/Driver/LTOCacheContract.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/SHA256.h"

#include <cstdlib>
#include <limits>
#include <optional>
#include <utility>

namespace {

void setEnvironmentVariable(const char *Name, const char *Value) {
#ifdef _WIN32
  _putenv_s(Name, Value);
#else
  setenv(Name, Value, 1);
#endif
}

void unsetEnvironmentVariable(const char *Name) {
#ifdef _WIN32
  _putenv_s(Name, "");
#else
  unsetenv(Name);
#endif
}

class ScopedEnvironmentVariable {
  std::string Name;
  std::optional<std::string> OldValue;

public:
  ScopedEnvironmentVariable(const char *Name, const char *Value) : Name(Name) {
    if (const char *Old = std::getenv(Name))
      OldValue = Old;
    setEnvironmentVariable(Name, Value);
  }

  explicit ScopedEnvironmentVariable(const char *Name) : Name(Name) {
    if (const char *Old = std::getenv(Name))
      OldValue = Old;
    unsetEnvironmentVariable(Name);
  }

  ScopedEnvironmentVariable(const ScopedEnvironmentVariable &) = delete;
  ScopedEnvironmentVariable &
  operator=(const ScopedEnvironmentVariable &) = delete;

  ~ScopedEnvironmentVariable() {
    if (OldValue)
      setEnvironmentVariable(Name.c_str(), OldValue->c_str());
    else
      unsetEnvironmentVariable(Name.c_str());
  }
};

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

// Keep the embedded formatting runtime live so the final LTO stage has actual
// runtime-owned ciphertext to re-key.  The minimal fixture above intentionally
// dead-strips this path.
constexpr const char *kAndroidKernelXorStrModule =
    "#include <nvkmod.h>\n"
    "int neverc_krt_fmt_init(void);\n"
    "static int m_init(void) { return neverc_krt_fmt_init(); }\n"
    "static void m_exit(void) {}\n"
    "module_init(m_init);\n"
    "module_exit(m_exit);\n"
    "MODULE_LICENSE(\"GPL v2\");\n"
    "NEVERC_KRT_DEFINE_MODULE(\"neverc_test_xorstr\");\n";

bool isElfImage(const std::string &Bytes) {
  return Bytes.size() > 4 && static_cast<unsigned char>(Bytes[0]) == 0x7f &&
         Bytes[1] == 'E' && Bytes[2] == 'L' && Bytes[3] == 'F';
}

std::string sha256Text(llvm::StringRef Bytes) {
  const auto Digest = llvm::SHA256::hash(llvm::ArrayRef<uint8_t>(
      reinterpret_cast<const uint8_t *>(Bytes.data()), Bytes.size()));
  return llvm::toHex(Digest, /*LowerCase=*/true);
}

std::string renderError(llvm::Error Error) {
  return llvm::toString(std::move(Error)).str().str();
}

::testing::AssertionResult hasNoSymbolContaining(const std::string &Bytes,
                                                 llvm::StringRef Marker) {
  using namespace llvm;
  using namespace llvm::object;

  auto ObjectOrErr = ObjectFile::createObjectFile(
      MemoryBufferRef(StringRef(Bytes.data(), Bytes.size()), "module.ko"));
  if (!ObjectOrErr)
    return ::testing::AssertionFailure()
           << "cannot parse .ko: " << renderError(ObjectOrErr.takeError());

  for (const SymbolRef &Symbol : (*ObjectOrErr)->symbols()) {
    Expected<StringRef> NameOrErr = Symbol.getName();
    if (!NameOrErr)
      return ::testing::AssertionFailure()
             << "cannot read symbol name: "
             << renderError(NameOrErr.takeError());
    if (NameOrErr->contains(Marker))
      return ::testing::AssertionFailure()
             << "final artifact still contains shared xorstr symbol '"
             << NameOrErr->str() << "'";
  }
  return ::testing::AssertionSuccess();
}

::testing::AssertionResult
hasNoUndefinedSymbolNamed(const std::string &Bytes,
                          llvm::StringRef SymbolName) {
  using namespace llvm;
  using namespace llvm::object;

  auto ObjectOrErr = ObjectFile::createObjectFile(
      MemoryBufferRef(StringRef(Bytes.data(), Bytes.size()), "module.ko"));
  if (!ObjectOrErr)
    return ::testing::AssertionFailure()
           << "cannot parse .ko: " << renderError(ObjectOrErr.takeError());

  for (const SymbolRef &Symbol : (*ObjectOrErr)->symbols()) {
    llvm::Expected<StringRef> NameOrErr = Symbol.getName();
    if (!NameOrErr)
      return ::testing::AssertionFailure()
             << "cannot read symbol name: "
             << renderError(NameOrErr.takeError());
    if (*NameOrErr != SymbolName)
      continue;
    llvm::Expected<uint32_t> FlagsOrErr = Symbol.getFlags();
    if (!FlagsOrErr)
      return ::testing::AssertionFailure()
             << "cannot read symbol flags: "
             << renderError(FlagsOrErr.takeError());
    if (*FlagsOrErr & SymbolRef::SF_Undefined)
      return ::testing::AssertionFailure()
             << "embedded runtime symbol '" << SymbolName.str()
             << "' is still undefined";
  }
  return ::testing::AssertionSuccess();
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
  // Relocatable (`-r`) Android kernel-module link.  An empty PluginPath uses
  // the native driver directly; otherwise the plugin is loaded with -fplugin,
  // which routes the relocatable link through the plugin object-merge bridge.
  CmdResult linkKernelModule(const std::string &PluginPath,
                             const fs::path &Source, const fs::path &Output,
                             bool UseDeterministicXorStrKey = true,
                             bool EmitDebugInfo = false,
                             bool StripSymbols = false) {
    return linkKernelModule(PluginPath, std::vector<fs::path>{Source}, Output,
                            UseDeterministicXorStrKey, EmitDebugInfo,
                            StripSymbols);
  }

  CmdResult linkKernelModule(const std::string &PluginPath,
                             const std::vector<fs::path> &Sources,
                             const fs::path &Output,
                             bool UseDeterministicXorStrKey = true,
                             bool EmitDebugInfo = false,
                             bool StripSymbols = false) {
    std::vector<std::string> Args;
    if (!PluginPath.empty())
      Args.push_back("-fplugin=" + PluginPath);
    Args.push_back("--target=aarch64-linux-android");
    Args.push_back("-fandroid-kernel-driver-mode");
    Args.push_back("-DNVK_KERNEL=510");
    if (UseDeterministicXorStrKey)
      Args.push_back("-fstring-encrypt-key=1");
    if (EmitDebugInfo)
      Args.push_back("-g");
    if (StripSymbols) {
      Args.push_back("-O2");
      Args.push_back("--strip");
    }
    Args.push_back("-nostdlib");
    Args.push_back("-r");
    for (const fs::path &Source : Sources)
      Args.push_back(Source.string());
    Args.push_back("-o");
    Args.push_back(Output.string());
    return ncc(Args);
  }

  ::testing::AssertionResult
  hasMatchingReleaseSymbolMap(const fs::path &Output,
                              const std::string &ImageBytes) const {
    const fs::path Sidecar(Output.string() + ".symbols.json");
    if (!fs::exists(Sidecar))
      return ::testing::AssertionFailure()
             << "missing release symbol map " << Sidecar;

    const std::string MapBytes = readFile(Sidecar);
    auto Parsed = llvm::json::parse(MapBytes);
    if (!Parsed)
      return ::testing::AssertionFailure()
             << "invalid release symbol map JSON: "
             << renderError(Parsed.takeError());
    const llvm::json::Object *Root = Parsed->getAsObject();
    if (!Root)
      return ::testing::AssertionFailure()
             << "release symbol map root is not an object";
    int64_t Version = 0;
    if (Root->getString("format") != "neverc.android-kernel-symbol-map" ||
        !Root->getInteger("version", Version) || Version != 2)
      return ::testing::AssertionFailure()
             << "release symbol map has the wrong format or version";

    const auto Digest = llvm::SHA256::hash(llvm::ArrayRef<uint8_t>(
        reinterpret_cast<const uint8_t *>(ImageBytes.data()),
        ImageBytes.size()));
    const llvm::StringRef ImageDigest = Root->getString("image_sha256");
    if (ImageDigest.empty() ||
        ImageDigest != llvm::toHex(Digest, /*LowerCase=*/true))
      return ::testing::AssertionFailure()
             << "release symbol map digest does not match the .ko";

    const llvm::json::Array *Symbols = Root->getArray("symbols");
    if (!Symbols || Symbols->empty())
      return ::testing::AssertionFailure()
             << "release symbol map contains no renamed symbols";

    auto ObjectOrErr =
        llvm::object::ObjectFile::createObjectFile(llvm::MemoryBufferRef(
            llvm::StringRef(ImageBytes.data(), ImageBytes.size()),
            "module.ko"));
    if (!ObjectOrErr)
      return ::testing::AssertionFailure()
             << "cannot parse mapped .ko: "
             << renderError(ObjectOrErr.takeError());
    llvm::StringMap<unsigned> ImageSymbolCounts;
    for (const llvm::object::SymbolRef &Symbol : (*ObjectOrErr)->symbols()) {
      auto NameOrErr = Symbol.getName();
      if (!NameOrErr)
        return ::testing::AssertionFailure()
               << "cannot read mapped .ko symbol name: "
               << renderError(NameOrErr.takeError());
      ++ImageSymbolCounts[*NameOrErr];
    }

    for (const llvm::json::Value &Value : *Symbols) {
      const llvm::json::Object *Entry = Value.getAsObject();
      if (!Entry)
        return ::testing::AssertionFailure()
               << "release symbol map entry is not an object";
      const llvm::StringRef Original = Entry->getString("original");
      const llvm::StringRef Release = Entry->getString("release");
      if (Original.empty() || Release.empty() || Original == Release ||
          !neverc::hasCanonicalReleaseNameShape(Release))
        return ::testing::AssertionFailure()
               << "release symbol map contains an invalid rename";
      const auto Count = ImageSymbolCounts.find(Release);
      if (Count == ImageSymbolCounts.end() || Count->second != 1)
        return ::testing::AssertionFailure()
               << "mapped release symbol '" << Release.str()
               << "' does not identify exactly one final .ko symbol";
    }
    return ::testing::AssertionSuccess();
  }
};

TEST_F(AndroidKernelRuntimeTest,
       ReleasePublishesMatchingSymbolMapAndDebugRemovesIt) {
  const fs::path Source = tmpFile("nvk_release_symbol_map.c");
  writeFile(Source, kAndroidKernelModule);
  const fs::path Output = tmpFile("nvk_release_symbol_map.ko");

  const CmdResult Release = linkKernelModule("", Source, Output,
                                             /*UseDeterministicXorStrKey=*/true,
                                             /*EmitDebugInfo=*/false,
                                             /*StripSymbols=*/true);
  ASSERT_EQ(Release.exitCode, 0) << Release.err;
  const std::string ReleaseBytes = readFile(Output);
  EXPECT_TRUE(hasMatchingReleaseSymbolMap(Output, ReleaseBytes));
#ifndef _WIN32
  llvm::sys::fs::file_status MapStatus;
  ASSERT_FALSE(
      llvm::sys::fs::status(Output.string() + ".symbols.json", MapStatus));
  EXPECT_EQ(MapStatus.permissions() &
                (llvm::sys::fs::group_all | llvm::sys::fs::others_all),
            llvm::sys::fs::no_perms);
#endif

  const fs::path PluginOutput = tmpFile("nvk_release_symbol_map_plugin.ko");
  const CmdResult PluginRelease = linkKernelModule(
      NEVERC_TEST_OBJECT_POST_WRITE_PLUGIN, Source, PluginOutput,
      /*UseDeterministicXorStrKey=*/true,
      /*EmitDebugInfo=*/false,
      /*StripSymbols=*/true);
  ASSERT_EQ(PluginRelease.exitCode, 0) << PluginRelease.err;
  const std::string PluginReleaseBytes = readFile(PluginOutput);
  EXPECT_TRUE(hasMatchingReleaseSymbolMap(PluginOutput, PluginReleaseBytes));

  const CmdResult Debug = linkKernelModule("", Source, Output);
  ASSERT_EQ(Debug.exitCode, 0) << Debug.err;
  EXPECT_FALSE(fs::exists(Output.string() + ".symbols.json"));

  const CmdResult PluginDebug = linkKernelModule(
      NEVERC_TEST_OBJECT_POST_WRITE_PLUGIN, Source, PluginOutput);
  ASSERT_EQ(PluginDebug.exitCode, 0) << PluginDebug.err;
  EXPECT_FALSE(fs::exists(PluginOutput.string() + ".symbols.json"));
}

TEST_F(AndroidKernelRuntimeTest,
       ReleasePublishesRequestedBuildStateInTheOutputBundle) {
  ScopedEnvironmentVariable BuildID(
      "NEVERC_ANDROID_KERNEL_BUILD_ID",
      "KERNEL=510 PROFILE=release NEVERC=/opt/neverc");
  ScopedEnvironmentVariable BuildExtra("NEVERC_ANDROID_KERNEL_BUILD_EXTRA",
                                       "-DTEST_FIRST=1 -DTEST_SECOND=two");
  const fs::path Source = tmpFile("nvk_release_build_state.c");
  const fs::path Output = tmpFile("nvk_release_build_state.ko");
  writeFile(Source, kAndroidKernelModule);

  const CmdResult Release = linkKernelModule("", Source, Output,
                                             /*UseDeterministicXorStrKey=*/true,
                                             /*EmitDebugInfo=*/false,
                                             /*StripSymbols=*/true);
  ASSERT_EQ(Release.exitCode, 0) << Release.err;
  const std::string BuildState =
      "KERNEL=510 PROFILE=release NEVERC=/opt/neverc\n";
  const std::string BuildExtraState = "-DTEST_FIRST=1 -DTEST_SECOND=two\n";
  const std::string Image = readFile(Output);
  EXPECT_EQ(readFile(tmp() / ".nvk-build-flags"), BuildState);
  EXPECT_EQ(readFile(tmp() / ".nvk-build-extra"), BuildExtraState);
  EXPECT_EQ(readFile(tmp() / ".nvk-build-integrity"),
            "IMAGE_SHA256=" + sha256Text(Image) +
                " BUILD_ID_SHA256=" + sha256Text(BuildState) +
                " BUILD_EXTRA_SHA256=" + sha256Text(BuildExtraState) + "\n");
}

TEST_F(AndroidKernelRuntimeTest,
       ReleaseClearsStaleBuildExtraWhenRequestedExtraIsEmpty) {
  ScopedEnvironmentVariable BuildID(
      "NEVERC_ANDROID_KERNEL_BUILD_ID",
      "KERNEL=510 PROFILE=release NEVERC=/opt/neverc");
  ScopedEnvironmentVariable BuildExtra("NEVERC_ANDROID_KERNEL_BUILD_EXTRA", "");
  const fs::path Source = tmpFile("nvk_release_empty_build_extra.c");
  const fs::path Output = tmpFile("nvk_release_empty_build_extra.ko");
  const fs::path ExtraState = tmp() / ".nvk-build-extra";
  writeFile(Source, kAndroidKernelModule);
  writeFile(ExtraState, "-DSTALE_EXTRA=1\n");

  const CmdResult Release = linkKernelModule("", Source, Output,
                                             /*UseDeterministicXorStrKey=*/true,
                                             /*EmitDebugInfo=*/false,
                                             /*StripSymbols=*/true);
  ASSERT_EQ(Release.exitCode, 0) << Release.err;
  EXPECT_FALSE(fs::exists(ExtraState));
  const std::string BuildState =
      "KERNEL=510 PROFILE=release NEVERC=/opt/neverc\n";
  EXPECT_EQ(readFile(tmp() / ".nvk-build-integrity"),
            "IMAGE_SHA256=" + sha256Text(readFile(Output)) +
                " BUILD_ID_SHA256=" + sha256Text(BuildState) +
                " BUILD_EXTRA_SHA256=-\n");
}

TEST_F(AndroidKernelRuntimeTest,
       ReleaseClearsStaleBuildExtraWhenBuildExtraIsAbsent) {
  ScopedEnvironmentVariable BuildID(
      "NEVERC_ANDROID_KERNEL_BUILD_ID",
      "KERNEL=510 PROFILE=release NEVERC=/opt/neverc");
  ScopedEnvironmentVariable BuildExtra("NEVERC_ANDROID_KERNEL_BUILD_EXTRA");
  const fs::path Source = tmpFile("nvk_release_absent_build_extra.c");
  const fs::path Output = tmpFile("nvk_release_absent_build_extra.ko");
  const fs::path ExtraState = tmp() / ".nvk-build-extra";
  writeFile(Source, kAndroidKernelModule);
  writeFile(ExtraState, "-DSTALE_EXTRA=1\n");

  const CmdResult Release = linkKernelModule("", Source, Output,
                                             /*UseDeterministicXorStrKey=*/true,
                                             /*EmitDebugInfo=*/false,
                                             /*StripSymbols=*/true);
  ASSERT_EQ(Release.exitCode, 0) << Release.err;
  EXPECT_FALSE(fs::exists(ExtraState));
  const std::string BuildState =
      "KERNEL=510 PROFILE=release NEVERC=/opt/neverc\n";
  EXPECT_EQ(readFile(tmp() / ".nvk-build-integrity"),
            "IMAGE_SHA256=" + sha256Text(readFile(Output)) +
                " BUILD_ID_SHA256=" + sha256Text(BuildState) +
                " BUILD_EXTRA_SHA256=-\n");
}

TEST_F(AndroidKernelRuntimeTest,
       BuildStatePublicationFailurePreservesTheExistingBundle) {
  ScopedEnvironmentVariable BuildID(
      "NEVERC_ANDROID_KERNEL_BUILD_ID",
      "KERNEL=510 PROFILE=release NEVERC=/opt/neverc");
  const fs::path Source = tmpFile("nvk_release_build_state_failure.c");
  const fs::path Output = tmpFile("nvk_release_build_state_failure.ko");
  const fs::path Sidecar(Output.string() + ".symbols.json");
  writeFile(Source, kAndroidKernelModule);
  writeFile(Output, "preexisting-main");
  writeFile(Sidecar, "preexisting-map");
  ASSERT_TRUE(fs::create_directory(tmp() / ".nvk-build-flags"));

  const CmdResult Release = linkKernelModule("", Source, Output,
                                             /*UseDeterministicXorStrKey=*/true,
                                             /*EmitDebugInfo=*/false,
                                             /*StripSymbols=*/true);
  EXPECT_NE(Release.exitCode, 0) << Release.err;
  EXPECT_EQ(readFile(Output), "preexisting-main");
  EXPECT_EQ(readFile(Sidecar), "preexisting-map");
}

TEST_F(AndroidKernelRuntimeTest,
       ReleaseLinkFailurePreservesPreexistingTransactionalBundle) {
  const fs::path FirstSource = tmpFile("nvk_release_duplicate_first.c");
  const fs::path SecondSource = tmpFile("nvk_release_duplicate_second.c");
  writeFile(FirstSource,
            std::string(kAndroidKernelModule) +
                "int duplicate_release_symbol(void) { return 1; }\n");
  writeFile(SecondSource, "int duplicate_release_symbol(void) { return 2; }\n");
  const fs::path Output = tmpFile("nvk_release_duplicate.ko");
  const fs::path Sidecar(Output.string() + ".symbols.json");
  writeFile(Output, "preexisting-main");
  writeFile(Sidecar, "preexisting-map");

  const CmdResult Release = linkKernelModule(
      "", std::vector<fs::path>{FirstSource, SecondSource}, Output,
      /*UseDeterministicXorStrKey=*/true,
      /*EmitDebugInfo=*/false,
      /*StripSymbols=*/true);
  EXPECT_NE(Release.exitCode, 0) << Release.err;
  EXPECT_EQ(readFile(Output), "preexisting-main");
  EXPECT_EQ(readFile(Sidecar), "preexisting-map");
}

TEST_F(AndroidKernelRuntimeTest,
       ReleaseRejectsStreamOutputWithoutSymbolMapPath) {
  const fs::path Source = tmpFile("nvk_release_stdout.c");
  writeFile(Source, kAndroidKernelModule);
  const CmdResult Release = linkKernelModule("", Source, fs::path("-"),
                                             /*UseDeterministicXorStrKey=*/true,
                                             /*EmitDebugInfo=*/false,
                                             /*StripSymbols=*/true);
  EXPECT_NE(Release.exitCode, 0);
  EXPECT_NE(Release.err.find(
                "Android kernel release symbol maps require a file output"),
            std::string::npos)
      << Release.err;
}

TEST_F(AndroidKernelRuntimeTest, EmbeddedRuntimeXorStrRekeysFinalArtifact) {
  const fs::path Source = tmpFile("nvk_xorstr_rekey.c");
  writeFile(Source, kAndroidKernelXorStrModule);
  const fs::path FirstKo = tmpFile("nvk_xorstr_rekey_first.ko");
  const fs::path SecondKo = tmpFile("nvk_xorstr_rekey_second.ko");

  const CmdResult First = linkKernelModule(
      /*PluginPath=*/"", Source, FirstKo,
      /*UseDeterministicXorStrKey=*/false);
  ASSERT_EQ(First.exitCode, 0) << First.err;
  const CmdResult Second = linkKernelModule(
      /*PluginPath=*/"", Source, SecondKo,
      /*UseDeterministicXorStrKey=*/false);
  ASSERT_EQ(Second.exitCode, 0) << Second.err;

  const std::string FirstBytes = readFile(FirstKo);
  const std::string SecondBytes = readFile(SecondKo);
  const std::string Vsnprintf("vsnprintf\0", sizeof("vsnprintf"));
  const std::string Vsscanf("vsscanf\0", sizeof("vsscanf"));
  ASSERT_TRUE(isElfImage(FirstBytes));
  ASSERT_TRUE(isElfImage(SecondBytes));
  EXPECT_EQ(FirstBytes.find(Vsnprintf), std::string::npos);
  EXPECT_EQ(FirstBytes.find(Vsscanf), std::string::npos);
  EXPECT_EQ(SecondBytes.find(Vsnprintf), std::string::npos);
  EXPECT_EQ(SecondBytes.find(Vsscanf), std::string::npos);
  EXPECT_FALSE(FirstBytes == SecondBytes)
      << "embedded runtime xorstr must receive a fresh final-link key"
      << "\nfirst compiler stderr:\n"
      << First.err << "\nsecond compiler stderr:\n"
      << Second.err;
}

TEST_F(AndroidKernelRuntimeTest, EmbeddedRuntimeXorStrHasNoSharedDecoder) {
  const fs::path Source =
      testDir() / "../../examples/android-kernel-hello/main.c";
  ASSERT_TRUE(fs::exists(Source)) << Source;
  const fs::path Output = tmpFile("nvk_xorstr_no_decoder.ko");

  const CmdResult Result =
      linkKernelModule("", Source, Output, /*UseDeterministicXorStrKey=*/false,
                       /*EmitDebugInfo=*/true);
  ASSERT_EQ(Result.exitCode, 0) << Result.err;

  const std::string Bytes = readFile(Output);
  ASSERT_TRUE(isElfImage(Bytes));
  EXPECT_TRUE(hasNoSymbolContaining(Bytes, "__neverc_xorstr_decrypt"));
  EXPECT_TRUE(hasNoSymbolContaining(Bytes, "__atomic"))
      << "format-slot atomics must lower to native AArch64 instructions";
  EXPECT_TRUE(hasNoSymbolContaining(Bytes, "__aarch64_"))
      << "the freestanding kernel module must not depend on outlined atomic "
         "helpers";
  EXPECT_EQ(Bytes.find("__neverc_xorstr_"), std::string::npos)
      << "debug/string tables must not retain xorstr helper identities";
  EXPECT_EQ(Bytes.find(".rekey"), std::string::npos)
      << "per-call ciphertext must not expose a semantic symbol name";
  EXPECT_EQ(Bytes.find("vsnprintf"), std::string::npos)
      << "debug symbols must not reveal the protected lookup target";
  EXPECT_EQ(Bytes.find("vsscanf"), std::string::npos)
      << "debug symbols must not reveal the protected lookup target";
}

TEST_F(AndroidKernelRuntimeTest,
       EmbeddedRuntimeMaterializesWhenLLVMPassesAreDisabled) {
  const fs::path Source = tmpFile("nvk_runtime_disabled_passes.c");
  const fs::path Output = tmpFile("nvk_runtime_disabled_passes.ko");
  writeFile(Source, kAndroidKernelXorStrModule);

  const CmdResult Build = ncc({
      "--target=aarch64-linux-android",
      "-fandroid-kernel-driver-mode",
      "-DNVK_KERNEL=510",
      "-O0",
      "-disable-llvm-passes",
      "-fno-lto",
      "-fstring-encrypt-key=1",
      "-r",
      "-nostdlib",
      Source.string(),
      "-o",
      Output.string(),
  });
  ASSERT_EQ(Build.exitCode, 0) << Build.err;

  const std::string Bytes = readFile(Output);
  ASSERT_TRUE(isElfImage(Bytes));
  EXPECT_TRUE(hasNoUndefinedSymbolNamed(Bytes, "neverc_krt_fmt_init"));
  EXPECT_TRUE(hasNoSymbolContaining(Bytes, "__neverc_xorstr_"));
  EXPECT_EQ(Bytes.find("vsnprintf"), std::string::npos);
  EXPECT_EQ(Bytes.find("vsscanf"), std::string::npos);
}

TEST_F(AndroidKernelRuntimeTest,
       EmbeddedRuntimeXorStrProviderBypassIsSealedAcrossModes) {
  ScopedEnvironmentVariable DisableLTOCache(linker::ltoCacheEnvVar,
                                            linker::ltoCacheDisableValue);
  const fs::path Source = tmpFile("nvk_xorstr_provider.c");
  writeFile(Source, kAndroidKernelXorStrModule);

  for (const char *Mode : {"", "-flto=full", "-fno-lto"}) {
    SCOPED_TRACE(Mode[0] ? Mode : "auto-lto");
    const std::string ModeName =
        Mode[0] ? (std::string(Mode) == "-fno-lto" ? "no_lto" : "full_lto")
                : "auto_lto";
    auto Build = [&](const fs::path &Output, bool UseDeterministicKey) {
      std::vector<std::string> Args = {
          std::string("-fplugin=") +
              NEVERC_TEST_IR_OPTIMIZATION_PASSTHROUGH_PLUGIN,
          "--target=aarch64-linux-android",
          "-fandroid-kernel-driver-mode",
          "-DNVK_KERNEL=510",
          "-O2",
          "-g",
      };
      if (UseDeterministicKey)
        Args.push_back("-fstring-encrypt-key=0x12345678DEADBEEF");
      if (Mode[0])
        Args.push_back(Mode);
      Args.insert(Args.end(),
                  {"-r", "-nostdlib", Source.string(), "-o", Output.string()});
      return ncc(Args);
    };

    const fs::path FirstOutput =
        tmpFile("nvk_xorstr_provider_first_" + ModeName + ".ko");
    const fs::path SecondOutput =
        tmpFile("nvk_xorstr_provider_second_" + ModeName + ".ko");
    const fs::path FixedFirstOutput =
        tmpFile("nvk_xorstr_provider_fixed_first_" + ModeName + ".ko");
    const fs::path FixedSecondOutput =
        tmpFile("nvk_xorstr_provider_fixed_second_" + ModeName + ".ko");
    const CmdResult FirstBuild = Build(FirstOutput, false);
    ASSERT_EQ(FirstBuild.exitCode, 0) << FirstBuild.err;
    const CmdResult SecondBuild = Build(SecondOutput, false);
    ASSERT_EQ(SecondBuild.exitCode, 0) << SecondBuild.err;
    const CmdResult FixedFirstBuild = Build(FixedFirstOutput, true);
    ASSERT_EQ(FixedFirstBuild.exitCode, 0) << FixedFirstBuild.err;
    const CmdResult FixedSecondBuild = Build(FixedSecondOutput, true);
    ASSERT_EQ(FixedSecondBuild.exitCode, 0) << FixedSecondBuild.err;

    const std::string FirstBytes = readFile(FirstOutput);
    const std::string SecondBytes = readFile(SecondOutput);
    const std::string FixedFirstBytes = readFile(FixedFirstOutput);
    const std::string FixedSecondBytes = readFile(FixedSecondOutput);
    ASSERT_TRUE(isElfImage(FirstBytes));
    ASSERT_TRUE(isElfImage(SecondBytes));
    ASSERT_TRUE(isElfImage(FixedFirstBytes));
    ASSERT_TRUE(isElfImage(FixedSecondBytes));
    EXPECT_FALSE(FirstBytes == SecondBytes)
        << "a provider-owned pipeline must not make the default xorstr seed "
           "deterministic";
    EXPECT_TRUE(FixedFirstBytes == FixedSecondBytes)
        << "a provider-owned pipeline must preserve fixed-seed "
           "reproducibility";
    for (const std::string *Bytes :
         {&FirstBytes, &SecondBytes, &FixedFirstBytes, &FixedSecondBytes}) {
      EXPECT_TRUE(hasNoUndefinedSymbolNamed(*Bytes, "neverc_krt_fmt_init"));
      EXPECT_TRUE(hasNoSymbolContaining(*Bytes, "__neverc_xorstr_"));
      EXPECT_EQ(Bytes->find("__neverc_xorstr_"), std::string::npos)
          << "provider output must not retain dead helper identities";
      EXPECT_EQ(Bytes->find(std::string("vsnprintf\0", sizeof("vsnprintf"))),
                std::string::npos);
      EXPECT_EQ(Bytes->find(std::string("vsscanf\0", sizeof("vsscanf"))),
                std::string::npos);
    }
  }
}

TEST_F(AndroidKernelRuntimeTest,
       EmbeddedRuntimeXorStrProviderSurvivesFullLTOSaveTemps) {
  ScopedEnvironmentVariable DisableLTOCache(linker::ltoCacheEnvVar,
                                            linker::ltoCacheDisableValue);
  const fs::path Source = tmpFile("nvk_xorstr_provider_save_temps.c");
  const fs::path Output = tmpFile("nvk_xorstr_provider_save_temps.ko");
  writeFile(Source, kAndroidKernelXorStrModule);

  const CmdResult Build = ncc({
      std::string("-fplugin=") + NEVERC_TEST_IR_OPTIMIZATION_PASSTHROUGH_PLUGIN,
      "--target=aarch64-linux-android",
      "-fandroid-kernel-driver-mode",
      "-DNVK_KERNEL=510",
      "-O2",
      "-flto=full",
      "-save-temps=obj",
      "-fstring-encrypt-key=0x12345678DEADBEEF",
      "-r",
      "-nostdlib",
      Source.string(),
      "-o",
      Output.string(),
  });
  ASSERT_EQ(Build.exitCode, 0) << Build.err;

  const std::string Bytes = readFile(Output);
  ASSERT_TRUE(isElfImage(Bytes));
  EXPECT_TRUE(hasNoUndefinedSymbolNamed(Bytes, "neverc_krt_fmt_init"));
  EXPECT_TRUE(hasNoSymbolContaining(Bytes, "__neverc_xorstr_"));
  EXPECT_EQ(Bytes.find("vsnprintf"), std::string::npos);
  EXPECT_EQ(Bytes.find("vsscanf"), std::string::npos);
}

TEST_F(AndroidKernelRuntimeTest,
       LateIRPassRuntimeReferenceIsMaterializedAcrossModes) {
  const fs::path Source = testDir() / "Inputs/nvk_late_runtime_reference.c";
  ASSERT_TRUE(fs::exists(Source)) << Source;

  const fs::path LTOInput = tmpFile("nvk_late_runtime_input.bc");
  const CmdResult Compile = ncc({
      "--target=aarch64-linux-android",
      "-fandroid-kernel-driver-mode",
      "-DNVK_KERNEL=510",
      "-O2",
      "-g",
      "-flto=full",
      "-fstring-encrypt-key=1",
      "-c",
      Source.string(),
      "-o",
      LTOInput.string(),
  });
  ASSERT_EQ(Compile.exitCode, 0) << Compile.err;

  auto CheckOutput = [&](const fs::path &Output) {
    const std::string Bytes = readFile(Output);
    ASSERT_TRUE(isElfImage(Bytes));
    EXPECT_TRUE(hasNoUndefinedSymbolNamed(Bytes, "neverc_krt_fmt_init"));
    EXPECT_TRUE(hasNoSymbolContaining(Bytes, "__neverc_xorstr_"));
    EXPECT_EQ(Bytes.find("vsnprintf"), std::string::npos);
    EXPECT_EQ(Bytes.find("vsscanf"), std::string::npos);
  };

  // Feed pre-existing IR back through the driver.  This covers the frontend's
  // IR-input path as well as the final LTO boundary; the dedicated
  // optimization-provider test below isolates a reference introduced from
  // inside the LTO replacement hook itself.
  for (const char *Mode : {"", "-flto=full"}) {
    SCOPED_TRACE(Mode[0] ? Mode : "auto-lto");
    const fs::path Output = tmpFile(Mode[0] ? "nvk_late_runtime_full_lto.ko"
                                            : "nvk_late_runtime_auto_lto.ko");
    std::vector<std::string> Args = {
        std::string("-fplugin=") +
            NEVERC_TEST_IR_PASS_LATE_NVK_REFERENCE_PLUGIN,
        "--target=aarch64-linux-android",
        "-fandroid-kernel-driver-mode",
        "-DNVK_KERNEL=510",
        "-O2",
        "-g",
        "-fstring-encrypt-key=1",
    };
    if (Mode[0])
      Args.push_back(Mode);
    Args.insert(Args.end(),
                {"-r", "-nostdlib", LTOInput.string(), "-o", Output.string()});

    const CmdResult Build = ncc(Args);
    ASSERT_EQ(Build.exitCode, 0) << Build.err;
    CheckOutput(Output);
  }

  // A no-LTO frontend still has the same late-pass requirement at its native
  // code-generation boundary.
  const fs::path NativeOutput = tmpFile("nvk_late_runtime_no_lto.ko");
  const CmdResult NativeBuild = ncc({
      std::string("-fplugin=") + NEVERC_TEST_IR_PASS_LATE_NVK_REFERENCE_PLUGIN,
      "--target=aarch64-linux-android",
      "-fandroid-kernel-driver-mode",
      "-DNVK_KERNEL=510",
      "-O2",
      "-g",
      "-fno-lto",
      "-fstring-encrypt-key=1",
      "-r",
      "-nostdlib",
      Source.string(),
      "-o",
      NativeOutput.string(),
  });
  ASSERT_EQ(NativeBuild.exitCode, 0) << NativeBuild.err;
  CheckOutput(NativeOutput);
}

TEST_F(AndroidKernelRuntimeTest,
       LateLTOOptimizationProviderRuntimeReferenceIsMaterialized) {
  const fs::path Source = testDir() / "Inputs/nvk_late_runtime_reference.c";
  const fs::path Output = tmpFile("nvk_late_provider_runtime_full_lto.ko");
  ASSERT_TRUE(fs::exists(Source)) << Source;

  // This provider deliberately passes the frontend module through unchanged,
  // then renames the placeholder declaration only from its second invocation:
  // the CommonLTO optimization-replacement hook.  Runtime materialization
  // must therefore happen after the provider publishes its final module.
  const CmdResult Build = ncc({
      std::string("-fplugin=") +
          NEVERC_TEST_IR_OPTIMIZATION_LATE_NVK_REFERENCE_PLUGIN,
      "--target=aarch64-linux-android",
      "-fandroid-kernel-driver-mode",
      "-DNVK_KERNEL=510",
      "-O2",
      "-g",
      "-flto=full",
      "-fstring-encrypt-key=1",
      "-r",
      "-nostdlib",
      Source.string(),
      "-o",
      Output.string(),
  });
  ASSERT_EQ(Build.exitCode, 0) << Build.err;

  const std::string Bytes = readFile(Output);
  ASSERT_TRUE(isElfImage(Bytes));
  EXPECT_TRUE(hasNoUndefinedSymbolNamed(Bytes, "plugin_late_nvk_runtime"))
      << "the provider did not introduce the LTO-only runtime reference";
  EXPECT_TRUE(hasNoUndefinedSymbolNamed(Bytes, "neverc_krt_fmt_init"));
  EXPECT_TRUE(hasNoSymbolContaining(Bytes, "__neverc_xorstr_"));
  EXPECT_EQ(Bytes.find("vsnprintf"), std::string::npos);
  EXPECT_EQ(Bytes.find("vsscanf"), std::string::npos);
}

TEST_F(AndroidKernelRuntimeTest, LateLTOIRPassRuntimeReferenceIsMaterialized) {
  const fs::path Source = testDir() / "Inputs/nvk_late_runtime_reference.c";
  const fs::path Output = tmpFile("nvk_late_ir_pass_runtime_full_lto.ko");
  ASSERT_TRUE(fs::exists(Source)) << Source;
  ScopedEnvironmentVariable StrictPCG("NEVERC_PCG_STRICT", "1");
  ScopedEnvironmentVariable DebugPCG("NEVERC_PCG_DEBUG", "1");

  // The pass is a no-op in the frontend task and introduces the NVK symbol
  // only from the CommonLTO pre-codegen callback.  This exercises the
  // mandatory tail after ordinary plugin passes, independently of the
  // replacement-provider hook above.
  const CmdResult Build = ncc({
      std::string("-fplugin=") +
          NEVERC_TEST_IR_PASS_LTO_LATE_NVK_REFERENCE_PLUGIN,
      "--target=aarch64-linux-android",
      "-fandroid-kernel-driver-mode",
      "-DNVK_KERNEL=510",
      "-O2",
      "-g",
      "-flto=full",
      "-fstring-encrypt-key=1",
      "-mllvm",
      "-neverc-pcg-min-funcs=2",
      "-mllvm",
      "-neverc-pcg-weight-floor=0",
      "-r",
      "-nostdlib",
      Source.string(),
      "-o",
      Output.string(),
  });
  ASSERT_EQ(Build.exitCode, 0) << Build.err;

  const std::string Bytes = readFile(Output);
  ASSERT_TRUE(isElfImage(Bytes));
  EXPECT_TRUE(hasNoUndefinedSymbolNamed(Bytes, "plugin_late_nvk_runtime"));
  EXPECT_TRUE(hasNoUndefinedSymbolNamed(Bytes, "neverc_krt_fmt_init"));
  EXPECT_TRUE(hasNoSymbolContaining(Bytes, "__neverc_xorstr_"));
  EXPECT_EQ(Bytes.find("vsnprintf"), std::string::npos);
  EXPECT_EQ(Bytes.find("vsscanf"), std::string::npos);
}

TEST_F(AndroidKernelRuntimeTest, EmbeddedRuntimeXorStrHonorsDeterministicSeed) {
  ScopedEnvironmentVariable DisableLTOCache(linker::ltoCacheEnvVar,
                                            linker::ltoCacheDisableValue);
  const fs::path Source = tmpFile("nvk_xorstr_deterministic.c");
  writeFile(Source, kAndroidKernelXorStrModule);
  const fs::path FirstKo = tmpFile("nvk_xorstr_deterministic_first.ko");
  const fs::path SecondKo = tmpFile("nvk_xorstr_deterministic_second.ko");

  const CmdResult First = linkKernelModule("", Source, FirstKo);
  ASSERT_EQ(First.exitCode, 0) << First.err;
  const CmdResult Second = linkKernelModule("", Source, SecondKo);
  ASSERT_EQ(Second.exitCode, 0) << Second.err;

  const std::string FirstBytes = readFile(FirstKo);
  const std::string SecondBytes = readFile(SecondKo);
  ASSERT_TRUE(isElfImage(FirstBytes));
  ASSERT_TRUE(isElfImage(SecondBytes));
  EXPECT_EQ(FirstBytes, SecondBytes)
      << "an explicit xorstr key must keep builds reproducible";
}

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
// so the bridge must transparently defer the whole link to the native driver
// and produce a byte-identical .ko.
TEST_F(AndroidKernelRuntimeTest,
       RelocatablePluginLinkDefersToNativeByteForByte) {
  const fs::path Source = tmpFile("nvk_defer.c");
  writeFile(Source, kAndroidKernelModule);
  const fs::path NativeKo = tmpFile("nvk_defer_native.ko");
  const fs::path PluginKo = tmpFile("nvk_defer_plugin.ko");

  const CmdResult Native =
      linkKernelModule(/*PluginPath=*/"", Source, NativeKo);
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
TEST_F(AndroidKernelRuntimeTest,
       RelocatablePluginLinkLowersLTOBitcodeAndMerges) {
  const fs::path Source = tmpFile("nvk_merge.c");
  writeFile(Source, kAndroidKernelModule);
  const fs::path NativeKo = tmpFile("nvk_merge_native.ko");
  const fs::path PluginKo = tmpFile("nvk_merge_plugin.ko");

  const CmdResult Native =
      linkKernelModule(/*PluginPath=*/"", Source, NativeKo);
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
  writeFile(First, "#include <nvkmod.h>\n"
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
  const CmdResult Plugin =
      linkKernelModule(NEVERC_TEST_OBJECT_POST_WRITE_PLUGIN, Sources, PluginKo);
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

// Each consumer TU independently links a copy of the embedded nvk runtime
// under __neverc_nvk_local.*.  LTO must coalesce those hidden ODR copies;
// otherwise -O1+ leaves non-identical duplicates (name, name.1, ...) and
// bootstrap writes one instance while every other TU reads a zero one.
TEST_F(AndroidKernelRuntimeTest, EmbeddedRuntimeLocalsCoalesceAcrossTUs) {
  const fs::path First = tmpFile("nvk_coalesce_a.c");
  const fs::path Second = tmpFile("nvk_coalesce_b.c");
  writeFile(First, "#include <nvkmod.h>\n"
                   "extern int nvk_coalesce_helper(void);\n"
                   "static int m_init(void) { return nvk_coalesce_helper(); }\n"
                   "static void m_exit(void) {}\n"
                   "module_init(m_init);\n"
                   "module_exit(m_exit);\n"
                   "MODULE_LICENSE(\"GPL v2\");\n"
                   "NEVERC_KRT_DEFINE_MODULE(\"neverc_test_coalesce\");\n");
  writeFile(
      Second,
      "#include <nvk_mem.h>\n"
      "int nvk_coalesce_helper(void) { return neverc_krt_mem_init(); }\n");
  const fs::path Output = tmpFile("nvk_coalesce.ko");
  ScopedEnvironmentVariable StrictPCG("NEVERC_PCG_STRICT", "1");
  ScopedEnvironmentVariable DebugPCG("NEVERC_PCG_DEBUG", "1");

  const CmdResult Build = ncc({
      "--target=aarch64-linux-android",
      "-fandroid-kernel-driver-mode",
      "-DNVK_KERNEL=510",
      "-O2",
      "-fstring-encrypt-key=1",
      "-mllvm",
      "-neverc-pcg-min-funcs=2",
      "-mllvm",
      "-neverc-pcg-weight-floor=0",
      "-nostdlib",
      "-r",
      First.string(),
      Second.string(),
      "-o",
      Output.string(),
  });
  ASSERT_EQ(Build.exitCode, 0) << Build.err;
  ASSERT_TRUE(Build.stderrContains("[pcg] SUCCESS"))
      << "test did not exercise parallel full-LTO:\n"
      << Build.err;

  const std::string Bytes = readFile(Output);
  ASSERT_TRUE(isElfImage(Bytes));
  EXPECT_TRUE(hasAndroidLoaderContract(Bytes, /*RequireEmptyTags=*/true));

  auto ObjectOrErr =
      llvm::object::ObjectFile::createObjectFile(llvm::MemoryBufferRef(
          llvm::StringRef(Bytes.data(), Bytes.size()), "module.ko"));
  ASSERT_TRUE(static_cast<bool>(ObjectOrErr))
      << renderError(ObjectOrErr.takeError());

  llvm::StringMap<unsigned> NameCounts;
  llvm::StringMap<unsigned> CanonicalNameCounts;
  llvm::StringMap<std::string> FirstSpelling;
  for (const llvm::object::SymbolRef &Symbol : (*ObjectOrErr)->symbols()) {
    auto NameOrErr = Symbol.getName();
    ASSERT_TRUE(static_cast<bool>(NameOrErr))
        << renderError(NameOrErr.takeError());
    if (NameOrErr->starts_with("__neverc_nvk_local.")) {
      ++NameCounts[*NameOrErr];
      llvm::StringRef Canonical = *NameOrErr;
      if (const size_t Marker = Canonical.find(".__pcg");
          Marker != llvm::StringRef::npos) {
        Canonical = Canonical.take_front(Marker);
      } else if (const size_t Dot = Canonical.rfind('.');
                 Dot != llvm::StringRef::npos) {
        const llvm::StringRef Suffix = Canonical.drop_front(Dot + 1);
        if (!Suffix.empty() &&
            llvm::all_of(Suffix, [](char C) { return C >= '0' && C <= '9'; }))
          Canonical = Canonical.take_front(Dot);
      }
      ++CanonicalNameCounts[Canonical];
      FirstSpelling.try_emplace(Canonical, NameOrErr->str());
    }
  }
  ASSERT_FALSE(NameCounts.empty())
      << "expected unstripped nvk runtime locals in the .ko";

  for (const auto &Entry : NameCounts) {
    EXPECT_EQ(Entry.getValue(), 1U)
        << "duplicate nvk runtime private symbol: " << Entry.getKey().str();
  }
  for (const auto &Entry : CanonicalNameCounts) {
    EXPECT_EQ(Entry.getValue(), 1U) << "nvk runtime private was not coalesced: "
                                    << FirstSpelling.lookup(Entry.getKey());
  }
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
