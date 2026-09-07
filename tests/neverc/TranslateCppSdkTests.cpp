#include "../../neverc/lib/Translate/Cpp/CppSdk.h"
#include "../../neverc/lib/Translate/JSON.h"
#include "NeverCTestFixture.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cstdlib>

namespace {
using namespace neverc::translate;
class TranslateCppSdkTest : public NeverCTest {
protected:
  static constexpr const char *Target = "x86_64-apple-macosx15.0.0";
  fs::path Descriptor;
  CppSdkContext Context;
  Diagnostics Errors;

  void SetUp() override {
    NeverCTest::SetUp();
    Descriptor = tmpFile("sdk.json");
    for (const auto *Root : {"libcxx", "resource", "platform"})
      fs::create_directory(tmpFile(Root));
  }
  llvm::json::Object descriptor() {
    return llvm::json::Object{
        {"schema", "neverc.cpp.sdk"},
        {"version", 1},
        {"distribution_id", CppMathSDKID},
        {"roots", llvm::json::Object{{"libcxx", "libcxx"},
                                     {"resource", "resource"},
                                     {"platform", "platform"}}}};
  }
  void writeDescriptor(llvm::json::Object Value) {
    std::string Bytes;
    llvm::raw_string_ostream OS(Bytes);
    OS << llvm::json::Value(std::move(Value));
    writeFile(Descriptor, Bytes);
  }
  bool load(const std::string &Triple = Target) {
    Errors.clear();
    return loadCppSdk(Descriptor.string(), Triple, Context, Errors);
  }
  void rejected(const std::string &Text) {
    ASSERT_FALSE(load());
    ASSERT_FALSE(Errors.empty());
    EXPECT_EQ(Errors.front().Code, "TR0101");
    EXPECT_NE(Errors.front().Reason.find(Text), std::string::npos)
        << Errors.front().Reason;
    EXPECT_TRUE(Context.DistributionID.empty());
    EXPECT_TRUE(Context.Roots.empty());
  }
  bool externalSdk() {
    const char *Path = std::getenv("NEVERC_CPP_SDK");
    if (!Path || !*Path)
      return false;
    Errors.clear();
    EXPECT_TRUE(loadCppSdk(Path, Target, Context, Errors))
        << (Errors.empty() ? "" : Errors.front().Reason);
    return !Context.Roots.empty();
  }
  void isolatedSdkCopy() {
    auto Value = descriptor();
    auto Copy = [&](const SDKDependency &File) {
      const auto Root = std::find_if(
          Context.Roots.begin(), Context.Roots.end(),
          [&](const CppSdkRoot &R) { return R.Name == File.Root; });
      ASSERT_NE(Root, Context.Roots.end());
      const auto Destination = tmpFile(File.Root) / File.Path;
      fs::create_directories(Destination.parent_path());
      fs::copy_file(fs::path(Root->AbsolutePath) / File.Path, Destination,
                    fs::copy_options::overwrite_existing);
    };
    for (const auto &File : Context.ApprovedFiles)
      Copy(File);
    Copy({"platform", "SDKSettings.json", ""});
    writeDescriptor(std::move(Value));
    ASSERT_TRUE(load());
  }
  MappingEvidence mapping(const std::string &ID = "cpp.math.fabs.f64.v1") {
    MappingEvidence M;
    M.ID = ID;
    M.DeclarationID =
        "f3ca0fb6a3bcfcc5dc17c1c23aa24f17ca350364ec3f88fb2ee305011ba67c3e";
    M.Result = {TypeKind::Double, {}};
    M.Parameters = {{TypeKind::Double, {}}};
    M.Origin = {
        "platform", "usr/include/math.h",
        "00606d7b27eb5db0a3b93c575a7084f432d1e783cfca2eedcdc234c01cb15134", 423,
        15};
    return M;
  }
};

TEST_F(TranslateCppSdkTest, MissingInvalidAndOversizedDescriptorsAreRejected) {
  rejected("missing");
  writeFile(Descriptor, "{}");
  rejected("descriptor must select");
  writeFile(Descriptor, std::string(1, char(0xff)));
  rejected("invalid SDK descriptor JSON");
  writeFile(Descriptor, std::string(20, '[') + std::string(20, ']'));
  rejected("nesting limit");
  writeFile(Descriptor, std::string(64 * 1024 + 1, ' '));
  rejected("size limit");
}

TEST_F(TranslateCppSdkTest,
       DescriptorCannotApproveAnotherDistributionOrSupplyItsOwnHashes) {
  auto Value = descriptor();
  Value["distribution_id"] = "a-different-sdk";
  writeDescriptor(std::move(Value));
  rejected("descriptor must select");
  Value = descriptor();
  Value["headers"] = llvm::json::Array{};
  writeDescriptor(std::move(Value));
  rejected("descriptor must select");
  Value = descriptor();
  Value["version"] = 2;
  writeDescriptor(std::move(Value));
  rejected("descriptor must select");
}

TEST_F(TranslateCppSdkTest, RootsMustBeThreeDistinctExistingDirectories) {
  auto Value = descriptor();
  (*Value.getObject("roots"))["extra"] = "platform";
  writeDescriptor(std::move(Value));
  rejected("exactly libcxx");
  Value = descriptor();
  (*Value.getObject("roots"))["resource"] = "missing-resource";
  writeDescriptor(std::move(Value));
  rejected("accessible directory");
  Value = descriptor();
  (*Value.getObject("roots"))["resource"] = "libcxx";
  writeDescriptor(std::move(Value));
  rejected("overlap");
  Value = descriptor();
  (*Value.getObject("roots"))["resource"] = "libcxx/nested";
  fs::create_directory(tmpFile("libcxx/nested"));
  writeDescriptor(std::move(Value));
  rejected("overlap");
}

TEST_F(TranslateCppSdkTest, RootPresenceDoesNotProveApprovedFilesAreInstalled) {
  writeDescriptor(descriptor());
  rejected("SDK file is unavailable");
}

TEST_F(TranslateCppSdkTest,
       TargetAdmissionUsesTheTestedMacOSDeploymentVersion) {
  for (const std::string &Triple :
       {"x86_64-apple-macosx15.0.0", "arm64-apple-macosx15.0.0",
        "aarch64-apple-macosx15.0", "x86_64-apple-darwin24.0.0",
        "x86_64-apple-darwin24.6.0"}) {
    Errors.clear();
    EXPECT_TRUE(validateCppMathTarget(Triple, Errors)) << Triple;
  }
  for (const std::string &Triple :
       {"x86_64-apple-macosx15.1.0", "arm64-apple-macosx14.0.0",
        "x86_64-apple-darwin23.6.0", "x86_64-unknown-linux-gnu",
        "i386-apple-macosx15.0.0", "arm64-apple-ios15.0.0",
        "x86_64-pc-windows-msvc", "arm64-apple-macosx15.0.0-simulator"}) {
    Errors.clear();
    EXPECT_FALSE(validateCppMathTarget(Triple, Errors)) << Triple;
    ASSERT_FALSE(Errors.empty());
    EXPECT_EQ(Errors.front().Code, "TR0204");
  }
}

TEST_F(TranslateCppSdkTest,
       CatalogIsTheFixedSharedUnionAndDoesNotContainHostPaths) {
  auto Value = llvm::json::parse(approvedCppSdkCatalog());
  ASSERT_TRUE(bool(Value));
  const auto *Object = Value->getAsObject();
  ASSERT_NE(Object, nullptr);
  EXPECT_EQ(Object->getString("distribution_id"), CppMathSDKID);
  ASSERT_NE(Object->getArray("headers"), nullptr);
  EXPECT_EQ(Object->getArray("headers")->size(), 209u);
  EXPECT_EQ(approvedCppSdkCatalogSHA256(),
            "0c72f368ab38180821ca5c51fdab9f7fd2c3e9d6106d5c327fa09a8c8c795fa4");
  EXPECT_EQ(approvedCppSdkCatalog().find("/Users/"), llvm::StringRef::npos);
  EXPECT_EQ(approvedCppSdkCatalog().find("/Library/"), llvm::StringRef::npos);
  EXPECT_EQ(approvedCppSdkCatalog().find("/opt/"), llvm::StringRef::npos);
}

TEST_F(TranslateCppSdkTest, CompiledCatalogPreservesSourceBytesAndTrailingNul) {
  const auto Path = testDir().parent_path().parent_path() /
                    "utils/translate-frontends/cpp/sdk/approved-sdk.json";
  ASSERT_TRUE(fs::is_regular_file(Path));
  const auto Source = readFile(Path);
  const auto Compiled = approvedCppSdkCatalog();
  ASSERT_FALSE(Source.empty());
  EXPECT_EQ(Compiled, llvm::StringRef(Source));
  EXPECT_EQ(Compiled.data()[Compiled.size()], '\0');
  EXPECT_EQ(approvedCppSdkCatalogSHA256(),
            "0c72f368ab38180821ca5c51fdab9f7fd2c3e9d6106d5c327fa09a8c8c795fa4");
}

TEST_F(TranslateCppSdkTest,
       ForgedMappingNamesDeclarationsTypesAndLocationsFailWithoutLibraryTrust) {
  Context.DistributionID = CppMathSDKID;
  Context.CatalogSHA256 = approvedCppSdkCatalogSHA256();
  std::vector<MappingEvidence> Invalid;
  auto M = mapping();
  M.ID = "std.fabs";
  Invalid.push_back(M);
  M = mapping();
  M.DeclarationID = std::string(64, '0');
  Invalid.push_back(M);
  M = mapping();
  M.Result.Kind = TypeKind::Int;
  Invalid.push_back(M);
  M = mapping();
  M.Parameters.clear();
  Invalid.push_back(M);
  M = mapping();
  M.Origin.Root = "libcxx";
  Invalid.push_back(M);
  M = mapping();
  M.Origin.Path = "usr/include/../include/math.h";
  Invalid.push_back(M);
  M = mapping();
  M.Origin.SHA256 = std::string(64, '0');
  Invalid.push_back(M);
  M = mapping();
  M.Origin.Line++;
  Invalid.push_back(M);
  for (const auto &Evidence : Invalid) {
    Errors.clear();
    EXPECT_FALSE(verifyCppSdkMappings(Context, {Evidence}, Errors));
    ASSERT_FALSE(Errors.empty());
    EXPECT_EQ(Errors.front().Code, "TR0103");
  }
}

TEST_F(TranslateCppSdkTest,
       ActualApprovedSdkProducesCanonicalRequestAndVerifiableMappings) {
  if (!externalSdk())
    GTEST_SKIP() << "set NEVERC_CPP_SDK for actual external SDK checks";
  EXPECT_EQ(Context.DistributionID, CppMathSDKID);
  EXPECT_EQ(Context.ApprovedFiles.size(), 209u);
  const auto Request = cppSdkRequestJSON(Context);
  EXPECT_EQ(Request.getString("distribution_id"), CppMathSDKID);
  EXPECT_EQ(Request.getString("catalog_sha256"), approvedCppSdkCatalogSHA256());
  ASSERT_NE(Request.getObject("roots"), nullptr);
  EXPECT_EQ(Request.getObject("roots")->size(), 3u);
  EXPECT_TRUE(verifyCppSdkDependencies(Context, Context.ApprovedFiles, Errors));
  auto Floor = mapping("cpp.math.floor.f64.v1");
  Floor.DeclarationID =
      "6bf45a2f8a4b03ce7d791a0c8425efecaf9afa3f0cef99a9cc084d408fc7e6e9";
  Floor.Origin.Line = 466;
  EXPECT_TRUE(verifyCppSdkMappings(Context, {mapping(), Floor}, Errors));
}

TEST_F(TranslateCppSdkTest,
       IsolatedHeaderAndDescriptorMutationsAreDetectedBeforePublication) {
  if (!externalSdk())
    GTEST_SKIP() << "set NEVERC_CPP_SDK for actual external SDK checks";
  isolatedSdkCopy();
  const SDKDependency Header = Context.ApprovedFiles.front();
  writeFile(tmpFile(Header.Root) / Header.Path, "changed header\n");
  EXPECT_FALSE(verifyCppSdkDependencies(Context, {Header}, Errors));
  ASSERT_FALSE(Errors.empty());
  EXPECT_EQ(Errors.back().Code, "TR0101");
  writeFile(Descriptor, readFile(Descriptor) + "\n");
  Errors.clear();
  EXPECT_FALSE(verifyCppSdkDependencies(Context, {}, Errors));
  EXPECT_NE(Errors.back().Reason.find("descriptor changed"), std::string::npos);
}

TEST_F(TranslateCppSdkTest,
       HelperCannotForgeHashesDuplicatesOrAdditionalSdkHeaders) {
  if (!externalSdk())
    GTEST_SKIP() << "set NEVERC_CPP_SDK for actual external SDK checks";
  const SDKDependency Header = Context.ApprovedFiles.front();
  auto Forged = Header;
  Forged.SHA256 = std::string(64, '0');
  Context.ApprovedFiles.push_back(Forged);
  EXPECT_FALSE(verifyCppSdkDependencies(Context, {Forged}, Errors));
  EXPECT_EQ(Errors.back().Code, "TR0103");
  Errors.clear();
  EXPECT_FALSE(verifyCppSdkDependencies(Context, {Header, Header}, Errors));
  EXPECT_EQ(Errors.back().Code, "TR0103");
  Errors.clear();
  Forged.Path = "vector";
  EXPECT_FALSE(verifyCppSdkDependencies(Context, {Forged}, Errors));
  EXPECT_EQ(Errors.back().Code, "TR0103");
}

TEST_F(TranslateCppSdkTest, SymlinkRebindingCannotExpandAnApprovedRoot) {
  if (!externalSdk())
    GTEST_SKIP() << "set NEVERC_CPP_SDK for actual external SDK checks";
  isolatedSdkCopy();
  const SDKDependency Header = Context.ApprovedFiles.front();
  const auto Path = tmpFile(Header.Root) / Header.Path;
  const auto External = tmpFile("external-header");
  fs::rename(Path, External);
  std::error_code EC;
  fs::create_symlink(External, Path, EC);
  if (EC)
    GTEST_SKIP() << "symlink creation unavailable: " << EC.message();
  EXPECT_FALSE(verifyCppSdkDependencies(Context, {Header}, Errors));
  ASSERT_FALSE(Errors.empty());
  EXPECT_NE(Errors.back().Reason.find("canonical declared"), std::string::npos);
}
} // namespace
