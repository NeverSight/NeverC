#include "Linker/MachO/DataConst.h"
#include "gtest/gtest.h"

using namespace llvm;
using namespace llvm::MachO;

TEST(MachODataConstTest, SpecialImageKindsHaveWritableDataByDefault) {
  for (const auto Kind : {MH_DYLINKER, MH_PRELOAD, MH_KEXT_BUNDLE})
    for (const bool IsPic : {false, true})
      for (const auto Platform : {PLATFORM_MACOS, PLATFORM_IOS})
        EXPECT_FALSE(linker::macho::defaultDataConst(
            Kind, IsPic, Platform, VersionTuple(17, 0)));
}

TEST(MachODataConstTest, ExistingExecutableAndLibraryDefaultsArePreserved) {
  for (const auto Platform : {PLATFORM_MACOS, PLATFORM_IOS}) {
    EXPECT_FALSE(linker::macho::defaultDataConst(
        MH_EXECUTE, false, Platform, VersionTuple(17, 0)));
    EXPECT_TRUE(linker::macho::defaultDataConst(
        MH_EXECUTE, true, Platform, VersionTuple(17, 0)));
    for (const bool IsPic : {false, true}) {
      EXPECT_TRUE(linker::macho::defaultDataConst(
          MH_DYLIB, IsPic, Platform, VersionTuple(17, 0)));
      EXPECT_TRUE(linker::macho::defaultDataConst(
          MH_BUNDLE, IsPic, Platform, VersionTuple(17, 0)));
      EXPECT_FALSE(linker::macho::defaultDataConst(
          MH_OBJECT, IsPic, Platform, VersionTuple(17, 0)));
    }
  }
}

TEST(MachODataConstTest, OlderMacosDeploymentKeepsDataWritable) {
  for (const auto Kind : {MH_EXECUTE, MH_DYLIB, MH_BUNDLE, MH_OBJECT,
                          MH_DYLINKER, MH_PRELOAD, MH_KEXT_BUNDLE})
    for (const bool IsPic : {false, true})
      EXPECT_FALSE(linker::macho::defaultDataConst(
          Kind, IsPic, PLATFORM_MACOS, VersionTuple(10, 14)));
}
