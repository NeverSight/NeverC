#include "../../neverc/lib/Translate/Cpp/CppSdk.h"
#include "NeverCTestFixture.h"
#include "llvm/Support/JSON.h"
#include <algorithm>

namespace {
using namespace neverc::translate;
class TranslateCppSdkTest : public NeverCTest {
protected:
  static constexpr const char *Target = "x86_64-apple-macosx15.0.0";
  CppSdkContext Context;
  Diagnostics Errors;

  bool load(const std::string &Triple = Target) {
    Errors.clear();
    return loadBuiltinCppSdk(Triple, Context, Errors);
  }
  MappingEvidence mapping(const std::string &ID = "cpp.math.fabs.f64.v1") {
    MappingEvidence M;
    M.ID = ID;
    M.DeclarationID =
        "3b5582378dc6c99969ded4259e116878bc30c63d238676286b4729b7ab12a5ce";
    M.Result = {TypeKind::Double, {}};
    M.Parameters = {{TypeKind::Double, {}}};
    M.Origin = {
        "platform", "usr/include/math.h",
        "00606d7b27eb5db0a3b93c575a7084f432d1e783cfca2eedcdc234c01cb15134", 423,
        15};
    return M;
  }
};

TEST_F(TranslateCppSdkTest, BuiltinSdkLoadsBothTargetsWithoutExternalInputs) {
  for (const std::string &Triple :
       {"x86_64-apple-macosx15.0.0", "arm64-apple-macosx15.0.0"}) {
    SCOPED_TRACE(Triple);
    ASSERT_TRUE(load(Triple)) << (Errors.empty() ? "" : Errors.front().Reason);
    EXPECT_EQ(Context.DistributionID,
              "neverc-embedded-clang20.1.8-libcxx200100-macos15.5");
    EXPECT_EQ(Context.TargetTriple, Triple);
    EXPECT_EQ(Context.CatalogSHA256, approvedCppSdkCatalogSHA256());
    EXPECT_EQ(Context.ApprovedFiles.size(), 209u);
    EXPECT_TRUE(
        verifyCppSdkDependencies(Context, Context.ApprovedFiles, Errors));
    EXPECT_TRUE(Errors.empty());
  }
}

TEST_F(TranslateCppSdkTest, InvalidTargetClearsPreviouslyApprovedContext) {
  ASSERT_TRUE(load());
  ASSERT_FALSE(Context.ApprovedFiles.empty());
  EXPECT_FALSE(load("x86_64-unknown-linux-gnu"));
  ASSERT_FALSE(Errors.empty());
  EXPECT_EQ(Errors.front().Code, "TR0204");
  EXPECT_TRUE(Context.DistributionID.empty());
  EXPECT_TRUE(Context.CatalogSHA256.empty());
  EXPECT_TRUE(Context.TargetTriple.empty());
  EXPECT_TRUE(Context.ApprovedFiles.empty());
}

TEST_F(TranslateCppSdkTest, RequestContainsOnlyImmutableDistributionIdentity) {
  ASSERT_TRUE(load());
  const auto Request = cppSdkRequestJSON(Context);
  EXPECT_EQ(Request.size(), 2u);
  EXPECT_EQ(Request.getString("distribution_id"), CppMathSDKID);
  EXPECT_EQ(Request.getString("catalog_sha256"), approvedCppSdkCatalogSHA256());
  EXPECT_EQ(Request.getObject("roots"), nullptr);
  EXPECT_EQ(Request.getString("descriptor").data(), nullptr);
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
            "e9e2be353baded7be350900ae52d5c1a5f0fc7724e29c2dbe18c9c5fdef5dbe3");
  EXPECT_EQ(approvedCppSdkCatalog().find("/Users/"), llvm::StringRef::npos);
  EXPECT_EQ(approvedCppSdkCatalog().find("/Library/"), llvm::StringRef::npos);
  EXPECT_EQ(approvedCppSdkCatalog().find("/opt/"), llvm::StringRef::npos);
}

TEST_F(TranslateCppSdkTest, CompiledCatalogPreservesSourceBytesAndTrailingNul) {
  const auto Path = testDir().parent_path().parent_path() /
                    "neverc/lib/Translate/Cpp/SDK/catalog.json";
  ASSERT_TRUE(fs::is_regular_file(Path));
  const auto Source = readFile(Path);
  const auto Compiled = approvedCppSdkCatalog();
  ASSERT_FALSE(Source.empty());
  EXPECT_EQ(Compiled, llvm::StringRef(Source));
  EXPECT_EQ(Compiled.data()[Compiled.size()], '\0');
  EXPECT_EQ(approvedCppSdkCatalogSHA256(),
            "e9e2be353baded7be350900ae52d5c1a5f0fc7724e29c2dbe18c9c5fdef5dbe3");
}

TEST_F(TranslateCppSdkTest,
       ForgedMappingNamesDeclarationsTypesAndLocationsFailWithoutLibraryTrust) {
  ASSERT_TRUE(load());
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
  M = mapping();
  M.Origin.Column++;
  Invalid.push_back(M);
  M = mapping();
  M.Result.RecordID = std::string(64, '1');
  Invalid.push_back(M);
  M = mapping();
  M.Parameters[0].RecordID = std::string(64, '1');
  Invalid.push_back(M);
  M = mapping();
  M.DeclarationID =
      "f3ca0fb6a3bcfcc5dc17c1c23aa24f17ca350364ec3f88fb2ee305011ba67c3e";
  Invalid.push_back(M);
  for (const auto &Evidence : Invalid) {
    Errors.clear();
    EXPECT_FALSE(verifyCppSdkMappings(Context, {Evidence}, Errors));
    ASSERT_FALSE(Errors.empty());
    EXPECT_EQ(Errors.front().Code, "TR0103");
  }
}

TEST_F(TranslateCppSdkTest, BuiltinSdkApprovesOnlyCanonicalMathMappings) {
  ASSERT_TRUE(load());
  auto Floor = mapping("cpp.math.floor.f64.v1");
  Floor.DeclarationID =
      "60841fa6c87218b07cc6556af6e977abac151c2cc1ca59104bb25cb8006df30a";
  Floor.Origin.Line = 466;
  EXPECT_TRUE(verifyCppSdkMappings(Context, {mapping(), Floor}, Errors));
  EXPECT_TRUE(Errors.empty());
  EXPECT_FALSE(verifyCppSdkMappings(Context, {mapping(), mapping()}, Errors));
  ASSERT_FALSE(Errors.empty());
  EXPECT_EQ(Errors.back().Code, "TR0103");
}

TEST_F(TranslateCppSdkTest, CallerCannotChangeDistributionOrCatalogAuthority) {
  ASSERT_TRUE(load());
  const auto Approved = Context;
  for (const char *Field : {"distribution", "catalog"}) {
    SCOPED_TRACE(Field);
    Context = Approved;
    if (std::string(Field) == "distribution")
      Context.DistributionID = "clang20.1.8-libcxx200100-macos15.5";
    else
      Context.CatalogSHA256 = std::string(64, '0');
    Errors.clear();
    EXPECT_FALSE(verifyCppSdkDependencies(Context, {}, Errors));
    ASSERT_FALSE(Errors.empty());
    EXPECT_EQ(Errors.back().Code, "TR0103");
    Errors.clear();
    EXPECT_FALSE(verifyCppSdkMappings(Context, {mapping()}, Errors));
    ASSERT_FALSE(Errors.empty());
    EXPECT_EQ(Errors.back().Code, "TR0103");
  }
}

TEST_F(TranslateCppSdkTest, EvidenceRetainsTargetAdmission) {
  ASSERT_TRUE(load());
  Context.TargetTriple = "x86_64-apple-macosx14.0.0";
  EXPECT_FALSE(verifyCppSdkMappings(Context, {mapping()}, Errors));
  ASSERT_FALSE(Errors.empty());
  EXPECT_EQ(Errors.back().Code, "TR0204");
  Errors.clear();
  EXPECT_FALSE(verifyCppSdkDependencies(Context, {}, Errors));
  ASSERT_FALSE(Errors.empty());
  EXPECT_EQ(Errors.back().Code, "TR0204");
}

TEST_F(TranslateCppSdkTest,
       CallerOwnedListsCannotApproveForgedHashesOrAdditionalHeaders) {
  ASSERT_TRUE(load());
  const SDKDependency Header = Context.ApprovedFiles.front();
  auto Forged = Header;
  Forged.SHA256 = std::string(64, '0');
  Context.ApprovedFiles.push_back(Forged);
  EXPECT_FALSE(verifyCppSdkDependencies(Context, {Forged}, Errors));
  ASSERT_FALSE(Errors.empty());
  EXPECT_EQ(Errors.back().Code, "TR0103");
  Errors.clear();
  EXPECT_FALSE(verifyCppSdkDependencies(Context, {Header, Header}, Errors));
  ASSERT_FALSE(Errors.empty());
  EXPECT_EQ(Errors.back().Code, "TR0103");
  Errors.clear();
  Forged.Path = "vector";
  Context.ApprovedFiles.push_back(Forged);
  EXPECT_FALSE(verifyCppSdkDependencies(Context, {Forged}, Errors));
  ASSERT_FALSE(Errors.empty());
  EXPECT_EQ(Errors.back().Code, "TR0103");
  Context.ApprovedFiles.clear();
  Errors.clear();
  EXPECT_TRUE(verifyCppSdkDependencies(Context, {Header}, Errors));
  EXPECT_TRUE(Errors.empty());
}

TEST_F(TranslateCppSdkTest,
       DependencyEvidenceCannotChangeRootsPathsOrIncludePlatformMetadata) {
  ASSERT_TRUE(load());
  const SDKDependency Header = Context.ApprovedFiles.front();
  std::vector<SDKDependency> Invalid;
  for (const std::string &Path : {"../__config", "./__config", "/__config",
                                  "nested/../__config", "nested\\__config"}) {
    auto Forged = Header;
    Forged.Path = Path;
    Invalid.push_back(Forged);
  }
  auto Forged = Header;
  Forged.Root = "owned";
  Invalid.push_back(Forged);
  Invalid.push_back(
      {"platform", "SDKSettings.json",
       "58499bbeb3eb1aa9ca96358a097bc237a9beb14cfd3db9876d986534e59ea17e"});
  for (const auto &Evidence : Invalid) {
    SCOPED_TRACE(Evidence.Root + "/" + Evidence.Path);
    Errors.clear();
    Context.ApprovedFiles.push_back(Evidence);
    EXPECT_FALSE(verifyCppSdkDependencies(Context, {Evidence}, Errors));
    ASSERT_FALSE(Errors.empty());
    EXPECT_EQ(Errors.back().Code, "TR0103");
  }
}
} // namespace
