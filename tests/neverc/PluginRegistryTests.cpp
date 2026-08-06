#include "neverc/Plugin/Host/PluginInterfaceRegistry.h"
#include "neverc/Plugin/Host/PluginRegistry.h"
#include "gtest/gtest.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include <array>
#include <cstring>

using namespace llvm;
using namespace neverc::plugin;

namespace {

constexpr NevercInterfaceID RequiredInterfaceID{0x1111111111111111ULL,
                                                0x2222222222222222ULL};
struct RequiredInterfaceTable {
  NevercABITableHeader Header;
  uint64_t Value;
};
const RequiredInterfaceTable RequiredInterface{{sizeof(RequiredInterfaceTable),
                                                1, 2, 0},
                                               42};

std::string takeErrorMessage(Error ErrorValue) {
  auto Message = toString(std::move(ErrorValue));
  return Message.str().str();
}

std::string takeErrorMessage(
    Expected<std::shared_ptr<const PluginModule>> &Result) {
  return takeErrorMessage(Result.takeError());
}

PluginRegistry makeRegistry() {
  return PluginRegistry("neverc-plugin-registry-tests", LLVM_VERSION_MAJOR);
}

TEST(PluginRegistryTest, LoadsAndOwnsAValidDescriptor) {
  auto Registry = makeRegistry();
  auto Loaded = Registry.load(NEVERC_TEST_MINIMAL_PLUGIN);
  if (!Loaded)
    FAIL() << takeErrorMessage(Loaded);

  EXPECT_EQ((*Loaded)->descriptor().PluginID, "org.neverc.test.minimal");
  EXPECT_EQ((*Loaded)->descriptor().DisplayName,
            "NeverC Registry Test Plugin");
  EXPECT_EQ((*Loaded)->descriptor().Version.Major, 1u);
  EXPECT_EQ((*Loaded)->descriptor().Version.Minor, 2u);
  EXPECT_EQ((*Loaded)->descriptor().Version.Patch, 3u);
  EXPECT_EQ(Registry.moduleCount(), 1u);
  EXPECT_EQ(Registry.generation(), 1u);

  auto Snapshot = Registry.acquireSnapshot();
  ASSERT_TRUE(static_cast<bool>(Snapshot));
  EXPECT_EQ(Snapshot->generation(), 1u);
  EXPECT_EQ(Snapshot->modules().size(), 1u);
  EXPECT_EQ(Snapshot->findByID("org.neverc.test.minimal"), (*Loaded).get());
}

TEST(PluginRegistryTest, RejectsMissingEntryPoint) {
  auto Registry = makeRegistry();
  auto Loaded = Registry.load(NEVERC_TEST_MISSING_ENTRY_PLUGIN);
  ASSERT_FALSE(static_cast<bool>(Loaded));
  EXPECT_NE(takeErrorMessage(Loaded).find("has no 'neverc_plugin_entry'"),
            std::string::npos);
}

#if !NEVERC_TEST_PYTHON_PLUGINS_ENABLED
TEST(PluginRegistryTest, PythonPluginReportsDisabledBuildFeature) {
  auto Registry = makeRegistry();
  auto Loaded = Registry.load(NEVERC_TEST_PYTHON_MINIMAL_PLUGIN);
  ASSERT_FALSE(static_cast<bool>(Loaded));
  std::string Message = takeErrorMessage(Loaded);
  EXPECT_NE(Message.find("Python plugin support disabled"), std::string::npos);
  EXPECT_NE(Message.find("NEVERC_ENABLE_PYTHON_PLUGINS=ON"),
            std::string::npos);
}
#endif

TEST(PluginRegistryTest, RejectsRemovedPrototypeBinaryWithMigrationDiagnostic) {
  auto Registry = makeRegistry();
  auto Loaded = Registry.load(NEVERC_TEST_REMOVED_PROTOTYPE_PLUGIN);
  ASSERT_FALSE(static_cast<bool>(Loaded));
  std::string Message = takeErrorMessage(Loaded);
  EXPECT_NE(Message.find("removed 'nevercGetPluginInfo' prototype ABI"),
            std::string::npos);
  EXPECT_NE(Message.find("export 'neverc_plugin_entry'"), std::string::npos);
}

TEST(PluginRegistryTest, RejectsMissingFile) {
  auto Registry = makeRegistry();
  auto Loaded = Registry.load("/neverc/this/plugin/does/not/exist");
  ASSERT_FALSE(static_cast<bool>(Loaded));
  EXPECT_NE(takeErrorMessage(Loaded).find("cannot resolve plugin path"),
            std::string::npos);
}

TEST(PluginRegistryTest, RejectsWrongABIMajor) {
  auto Registry = makeRegistry();
  auto Loaded = Registry.load(NEVERC_TEST_WRONG_ABI_PLUGIN);
  ASSERT_FALSE(static_cast<bool>(Loaded));
  EXPECT_NE(takeErrorMessage(Loaded).find("ABI major"), std::string::npos);
}

TEST(PluginRegistryTest, RejectsShortDescriptor) {
  auto Registry = makeRegistry();
  auto Loaded = Registry.load(NEVERC_TEST_SHORT_DESCRIPTOR_PLUGIN);
  ASSERT_FALSE(static_cast<bool>(Loaded));
  EXPECT_NE(takeErrorMessage(Loaded).find("shorter than its required prefix"),
            std::string::npos);
}

TEST(PluginRegistryTest, AcceptsKnownPrefixAndIgnoresUnknownTail) {
  auto Registry = makeRegistry();
  auto Prefix = Registry.load(NEVERC_TEST_PREFIX_DESCRIPTOR_PLUGIN);
  if (!Prefix)
    FAIL() << takeErrorMessage(Prefix);
  EXPECT_EQ((*Prefix)->descriptor().PluginID, "org.neverc.test.prefix");
  EXPECT_EQ((*Prefix)->descriptor().SessionBegin, nullptr);
  EXPECT_EQ((*Prefix)->descriptor().Destroy, nullptr);

  auto Long = Registry.load(NEVERC_TEST_LONG_DESCRIPTOR_PLUGIN);
  if (!Long)
    FAIL() << takeErrorMessage(Long);
  EXPECT_EQ((*Long)->descriptor().PluginID, "org.neverc.test.long");
  EXPECT_EQ(Registry.moduleCount(), 2u);
}

TEST(PluginRegistryTest, EntryHonorsCallerBufferCapacity) {
  SmallString<256> LoadError;
  auto Library = sys::DynamicLibrary::getLibrary(NEVERC_TEST_MINIMAL_PLUGIN,
                                                 &LoadError);
  ASSERT_TRUE(Library.isValid()) << LoadError.c_str();
  void *Address = Library.getAddressOfSymbol(NEVERC_PLUGIN_ENTRY_POINT);
  ASSERT_NE(Address, nullptr);
  auto Entry = reinterpret_cast<NevercPluginEntryFn>(Address);

  constexpr size_t PrefixSize =
      offsetof(NevercPluginDescriptor, Register) +
      sizeof(NevercPluginDescriptor::Register);
  struct alignas(NevercPluginDescriptor) BoundedBuffer {
    std::array<unsigned char, sizeof(NevercPluginDescriptor) + 32> Bytes;
  } Buffer;
  Buffer.Bytes.fill(0xA5);
  auto *Descriptor =
      reinterpret_cast<NevercPluginDescriptor *>(Buffer.Bytes.data());
  std::memset(Descriptor, 0, PrefixSize);
  Descriptor->Header.StructSize = PrefixSize;

  NevercBootstrapAPI Bootstrap{};
  Bootstrap.Header = {sizeof(Bootstrap), NEVERC_PLUGIN_ABI_MAJOR,
                      NEVERC_PLUGIN_ABI_MINOR, 0};
  NevercStatus Status = Entry(&Bootstrap, Descriptor);
  EXPECT_EQ(Status.Code, NEVERC_STATUS_OK);
  EXPECT_EQ(Descriptor->Header.StructSize, sizeof(NevercPluginDescriptor));
  EXPECT_TRUE(llvm::all_of(
      ArrayRef<unsigned char>(Buffer.Bytes).drop_front(PrefixSize),
      [](unsigned char Byte) { return Byte == 0xA5; }));
  sys::DynamicLibrary::closeLibrary(Library);
}

TEST(PluginRegistryTest, RejectsNonCanonicalPluginID) {
  auto Registry = makeRegistry();
  auto Loaded = Registry.load(NEVERC_TEST_INVALID_ID_PLUGIN);
  ASSERT_FALSE(static_cast<bool>(Loaded));
  EXPECT_NE(takeErrorMessage(Loaded).find("plugin ID is not canonical"),
            std::string::npos);
}

TEST(PluginRegistryTest, RejectsDuplicateIDFromDifferentFiles) {
  auto Registry = makeRegistry();
  auto First = Registry.load(NEVERC_TEST_MINIMAL_PLUGIN);
  if (!First)
    FAIL() << takeErrorMessage(First);

  auto Duplicate = Registry.load(NEVERC_TEST_DUPLICATE_ID_PLUGIN);
  ASSERT_FALSE(static_cast<bool>(Duplicate));
  EXPECT_NE(takeErrorMessage(Duplicate).find("duplicate plugin ID"),
            std::string::npos);
  EXPECT_EQ(Registry.moduleCount(), 1u);
  EXPECT_EQ(Registry.generation(), 1u);
}

TEST(PluginRegistryTest, ValidatesRequiredInterfacesBeforePublishingPlugin) {
  PluginInterfaceRegistry Interfaces;
  EXPECT_FALSE(Interfaces.registerInterface(
      RequiredInterfaceID, NEVERC_INTERFACE_STABLE, &RequiredInterface, {}));
  EXPECT_FALSE(Interfaces.freeze());
  PluginRegistry Registry("neverc-plugin-registry-tests", LLVM_VERSION_MAJOR,
                          &Interfaces);

  auto Supported = Registry.load(NEVERC_TEST_REQUIRED_INTERFACE_PLUGIN);
  if (!Supported)
    FAIL() << takeErrorMessage(Supported);
  EXPECT_EQ(Registry.moduleCount(), 1u);

  auto Missing = Registry.load(NEVERC_TEST_MISSING_INTERFACE_PLUGIN);
  ASSERT_FALSE(static_cast<bool>(Missing));
  EXPECT_NE(takeErrorMessage(Missing).find("unsatisfied required interface"),
            std::string::npos);
  EXPECT_EQ(Registry.moduleCount(), 1u);
}

TEST(PluginRegistryTest, DeduplicatesHardLinkAliasesByFileIdentity) {
  auto Registry = makeRegistry();
  auto First = Registry.load(NEVERC_TEST_MINIMAL_PLUGIN);
  if (!First)
    FAIL() << takeErrorMessage(First);

  // A hard link cannot cross volumes and the system temporary directory does
  // not always share a drive with the build tree, so stage the alias beside
  // the plugin it aliases.
  SmallString<160> Prefix(NEVERC_TEST_MINIMAL_PLUGIN);
  sys::path::remove_filename(Prefix);
  sys::path::append(Prefix, "neverc-plugin-registry");
  SmallString<128> TemporaryDirectory;
  ASSERT_FALSE(
      sys::fs::createUniqueDirectory(Prefix, TemporaryDirectory));
  SmallString<160> Alias(TemporaryDirectory);
  // LoadLibrary appends ".dll" to a path that carries no extension, so an
  // extensionless alias is looked up under a name that does not exist.
  const std::string AliasName =
      ("minimal-plugin-alias" +
       sys::path::extension(NEVERC_TEST_MINIMAL_PLUGIN))
          .str();
  sys::path::append(Alias, AliasName);
  ASSERT_FALSE(
      sys::fs::create_hard_link(NEVERC_TEST_MINIMAL_PLUGIN, Alias));

  auto AliasLoad = Registry.load(Alias);
  if (!AliasLoad)
    FAIL() << takeErrorMessage(AliasLoad);
  EXPECT_EQ((*AliasLoad).get(), (*First).get());
  EXPECT_EQ(Registry.moduleCount(), 1u);
  EXPECT_EQ(Registry.generation(), 1u);

  EXPECT_FALSE(sys::fs::remove_directories(TemporaryDirectory));
}

TEST(PluginRegistryTest, SnapshotLeaseBlocksMutation) {
  auto Registry = makeRegistry();
  auto First = Registry.load(NEVERC_TEST_MINIMAL_PLUGIN);
  if (!First)
    FAIL() << takeErrorMessage(First);
  auto Snapshot = Registry.acquireSnapshot();
  EXPECT_EQ(Registry.activeSnapshotLeases(), 1u);

  auto Other = Registry.load(NEVERC_TEST_OTHER_PLUGIN);
  ASSERT_FALSE(static_cast<bool>(Other));
  EXPECT_NE(takeErrorMessage(Other).find("snapshot lease"), std::string::npos);

  Error Shutdown = Registry.shutdown();
  ASSERT_TRUE(static_cast<bool>(Shutdown));
  EXPECT_NE(takeErrorMessage(std::move(Shutdown)).find("snapshot lease"),
            std::string::npos);

  Snapshot.reset();
  EXPECT_EQ(Registry.activeSnapshotLeases(), 0u);
  Other = Registry.load(NEVERC_TEST_OTHER_PLUGIN);
  if (!Other)
    FAIL() << takeErrorMessage(Other);
  EXPECT_EQ(Registry.moduleCount(), 2u);
  EXPECT_EQ(Registry.generation(), 2u);
}

TEST(PluginRegistryTest, RepeatedLoadUnloadLeavesNoResidentModule) {
  auto Registry = makeRegistry();

  // A snapshot owns strong references to its modules, so a stale published
  // snapshot would silently keep an unloaded plugin -- and its process state
  // and dynamic library -- resident. Track each cycle's module weakly to prove
  // unload republishes and releases.
  std::weak_ptr<const PluginModule> Unloaded;
  for (uint64_t Cycle = 0; Cycle != 3; ++Cycle) {
    EXPECT_TRUE(Unloaded.expired())
        << "cycle " << Cycle << " began with the previous module resident";

    auto Loaded = Registry.load(NEVERC_TEST_MINIMAL_PLUGIN);
    if (!Loaded)
      FAIL() << takeErrorMessage(Loaded);
    EXPECT_EQ(Registry.moduleCount(), 1u);
    EXPECT_EQ(Registry.generation(), 2 * Cycle + 1);

    {
      auto Snapshot = Registry.acquireSnapshot();
      ASSERT_TRUE(static_cast<bool>(Snapshot));
      EXPECT_EQ(Snapshot->generation(), 2 * Cycle + 1);
      EXPECT_EQ(Snapshot->modules().size(), 1u);
      EXPECT_EQ(Snapshot->findByID("org.neverc.test.minimal"), Loaded->get());
    }
    EXPECT_EQ(Registry.activeSnapshotLeases(), 0u);

    Unloaded = *Loaded;
    *Loaded = std::shared_ptr<const PluginModule>();
    EXPECT_FALSE(Registry.unload("org.neverc.test.minimal"));
    EXPECT_EQ(Registry.moduleCount(), 0u);
    EXPECT_EQ(Registry.generation(), 2 * Cycle + 2);
    EXPECT_TRUE(Unloaded.expired())
        << "unload left the module reachable through a published snapshot";

    auto Empty = Registry.acquireSnapshot();
    ASSERT_TRUE(static_cast<bool>(Empty));
    EXPECT_EQ(Empty->generation(), 2 * Cycle + 2);
    EXPECT_TRUE(Empty->modules().empty());
    EXPECT_EQ(Empty->findByID("org.neverc.test.minimal"), nullptr);
  }
}

TEST(PluginRegistryTest, SessionAndCallbackActivityBlockMutation) {
  auto Registry = makeRegistry();

  auto Session = Registry.acquireSessionLease();
  EXPECT_EQ(Registry.activeSessions(), 1u);
  auto DuringSession = Registry.load(NEVERC_TEST_MINIMAL_PLUGIN);
  ASSERT_FALSE(static_cast<bool>(DuringSession));
  EXPECT_NE(takeErrorMessage(DuringSession).find("session"), std::string::npos);

  Session.reset();
  EXPECT_EQ(Registry.activeSessions(), 0u);
  auto Loaded = Registry.load(NEVERC_TEST_MINIMAL_PLUGIN);
  if (!Loaded)
    FAIL() << takeErrorMessage(Loaded);

  auto Callback = Registry.acquireCallbackLease();
  EXPECT_EQ(Registry.activeCallbacks(), 1u);
  Error DuringCallback = Registry.shutdown();
  ASSERT_TRUE(static_cast<bool>(DuringCallback));
  EXPECT_NE(takeErrorMessage(std::move(DuringCallback)).find("callback"),
            std::string::npos);

  Callback.reset();
  EXPECT_EQ(Registry.activeCallbacks(), 0u);
  *Loaded = std::shared_ptr<const PluginModule>();
  EXPECT_FALSE(Registry.shutdown());
}

TEST(PluginRegistryTest, ShutdownClearsModulesAndPreventsReload) {
  auto Registry = makeRegistry();
  auto Loaded = Registry.load(NEVERC_TEST_MINIMAL_PLUGIN);
  if (!Loaded)
    FAIL() << takeErrorMessage(Loaded);
  *Loaded = std::shared_ptr<const PluginModule>();

  ASSERT_FALSE(Registry.shutdown());
  EXPECT_EQ(Registry.moduleCount(), 0u);
  EXPECT_EQ(Registry.generation(), 2u);

  auto Reload = Registry.load(NEVERC_TEST_MINIMAL_PLUGIN);
  ASSERT_FALSE(static_cast<bool>(Reload));
  EXPECT_NE(takeErrorMessage(Reload).find("after registry shutdown"),
            std::string::npos);
}

TEST(PluginRegistryTest, ValidatesCanonicalPluginIDs) {
  EXPECT_TRUE(isCanonicalPluginID("a"));
  EXPECT_TRUE(isCanonicalPluginID("org.neverc.plugin-name_2"));
  EXPECT_FALSE(isCanonicalPluginID(""));
  EXPECT_FALSE(isCanonicalPluginID("Org.neverc.plugin"));
  EXPECT_FALSE(isCanonicalPluginID("org..plugin"));
  EXPECT_FALSE(isCanonicalPluginID("org.-plugin"));
  EXPECT_FALSE(isCanonicalPluginID("org.plugin-"));
  EXPECT_FALSE(isCanonicalPluginID("org.plug\xC3\xAFn"));
}

} // namespace
