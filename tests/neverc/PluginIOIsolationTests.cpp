#include "neverc/Plugin/Host/PluginIOBridge.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "gtest/gtest.h"
#include "llvm/Config/llvm-config.h"

using namespace neverc::plugin;

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
