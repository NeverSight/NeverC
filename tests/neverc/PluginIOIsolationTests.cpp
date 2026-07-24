#include "neverc/Plugin/Host/PluginIOBridge.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "gtest/gtest.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace neverc::plugin;

// This target deliberately links only nevercPluginCore/nevercPluginIO plus
// LLVMSupport to prove nevercPluginIO stands alone without the frontend or
// compiler libraries. In assertions / ABI-breaking-checks builds, LLVM headers
// emit out-of-line references to the header-inline llvm::dbgs() (reached through
// Error::fatalUncheckedError, Expected<>::fatalUncheckedExpected and the vfs
// FileSystem::dump() debug helper). dbgs() has no out-of-line home in this fork,
// so the higher-level LLVM libraries this target intentionally omits would
// normally be the ones carrying its weak definition. Force one copy here so the
// minimal link resolves without pulling in codegen/target libraries.
namespace {
[[maybe_unused, gnu::used]] llvm::raw_ostream &(*const ForceDbgsEmission)() =
    &llvm::dbgs;
} // namespace

namespace {

TEST(PluginIOIsolationTest, PublishesCompleteIndependentIOTable) {
  PluginProcessServices Services{"neverc-plugin-io-isolation",
                                 LLVM_VERSION_MAJOR};
  ASSERT_FALSE(registerPluginIOInterface(Services));
  ASSERT_FALSE(Services.interfaces().freeze());

  auto Query = Services.interfaces().query(
      ioPluginInterfaceID(), NEVERC_IO_API_MAJOR,
      NEVERC_IO_API_MINOR);
  ASSERT_TRUE(static_cast<bool>(Query));
  ASSERT_NE(Query->Table, nullptr);
  EXPECT_EQ(Query->StructSize, sizeof(NevercIOAPI));
  const auto &API = *static_cast<const NevercIOAPI *>(Query->Table);
  EXPECT_EQ(API.Header.StructSize, sizeof(API));
  EXPECT_NE(API.RegisterVFSProvider, nullptr);
  EXPECT_NE(API.Stat, nullptr);
  EXPECT_NE(API.OpenFileForRead, nullptr);
  EXPECT_NE(API.ReadFile, nullptr);
  EXPECT_NE(API.CloseFile, nullptr);
  EXPECT_NE(API.CopyBuffer, nullptr);
  EXPECT_NE(API.GetBufferView, nullptr);
  EXPECT_NE(API.ReleaseBuffer, nullptr);
  EXPECT_NE(API.Canonicalize, nullptr);
  EXPECT_NE(API.GetCurrentDirectory, nullptr);
  EXPECT_NE(API.SetCurrentDirectory, nullptr);
  EXPECT_NE(API.OpenDirectory, nullptr);
  EXPECT_NE(API.ReadDirectory, nullptr);
  EXPECT_NE(API.CloseDirectory, nullptr);
  EXPECT_NE(API.AddMemoryFile, nullptr);

  EXPECT_FALSE(Services.shutdown());
}

} // namespace
