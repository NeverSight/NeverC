#include "neverc/Plugin/Host/BuiltinTargetSchema.h"
#include "neverc/Plugin/Host/TargetSchemaDigest.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"
#include <cstdlib>
#include <string>

using namespace llvm;
using namespace neverc::plugin;

namespace {

std::string errorText(Error ErrorValue) {
  return toString(std::move(ErrorValue)).str().str();
}

std::string schemaRoot() {
  if (const char *FromEnv = std::getenv("NEVERC_PLUGIN_TARGET_SCHEMA_ROOT"))
    return FromEnv;
#ifdef NEVERC_PLUGIN_TARGET_SCHEMA_ROOT
  return NEVERC_PLUGIN_TARGET_SCHEMA_ROOT;
#else
  SmallString<256> Path(NEVERC_SOURCE_DIR);
  sys::path::append(Path, "pluginsdk", "schemas", "targets");
  return std::string(Path.str());
#endif
}

} // namespace

TEST(PluginTargetSchemaTest, BuiltinX86AndAArch64SchemasAreLoadable) {
  auto X86 = loadBuiltinTargetSchema(schemaRoot(), "x86_64");
  ASSERT_TRUE(static_cast<bool>(X86)) << errorText(X86.takeError());
  EXPECT_EQ(X86->Architecture, "x86_64");
  EXPECT_FALSE(X86->Registers.empty());
  EXPECT_FALSE(X86->Instructions.empty());
  EXPECT_FALSE(X86->Features.empty());
  EXPECT_EQ(X86->Digest.size(), 64U);

  auto AArch64 = loadBuiltinTargetSchema(schemaRoot(), "aarch64");
  ASSERT_TRUE(static_cast<bool>(AArch64)) << errorText(AArch64.takeError());
  EXPECT_EQ(AArch64->Architecture, "aarch64");
  EXPECT_FALSE(AArch64->Registers.empty());
  EXPECT_FALSE(AArch64->Instructions.empty());
  EXPECT_FALSE(AArch64->Features.empty());
  EXPECT_EQ(AArch64->Digest.size(), 64U);
  EXPECT_NE(X86->Digest, AArch64->Digest);
}

TEST(PluginTargetSchemaTest, DigestMatchesCanonicalPayload) {
  auto Schema = loadBuiltinTargetSchema(schemaRoot(), "x86_64");
  ASSERT_TRUE(static_cast<bool>(Schema)) << errorText(Schema.takeError());
  EXPECT_EQ(computeTargetSchemaDigest(*Schema), Schema->Digest);
}

TEST(PluginTargetSchemaTest, LookupRejectsUnknownBackendValueAndForeignDigest) {
  auto Schema = loadBuiltinTargetSchema(schemaRoot(), "x86_64");
  ASSERT_TRUE(static_cast<bool>(Schema)) << errorText(Schema.takeError());

  const BuiltinTargetRegister *RAX = nullptr;
  for (const auto &Register : Schema->Registers) {
    if (Register.CanonicalName == "RAX" || Register.CanonicalName == "rax") {
      RAX = &Register;
      break;
    }
  }
  ASSERT_NE(RAX, nullptr);

  const BuiltinTargetRegister *Found =
      findRegisterByBackendValue(*Schema, RAX->BackendValue);
  ASSERT_NE(Found, nullptr);
  EXPECT_EQ(Found->StableID, RAX->StableID);

  EXPECT_EQ(findRegisterByBackendValue(*Schema, 0xFFFFFFFFu), nullptr);

  EXPECT_TRUE(bool(validateTargetSchemaToken(*Schema, std::string(64, '0'))));
  EXPECT_FALSE(bool(validateTargetSchemaToken(*Schema, Schema->Digest)));
}

TEST(PluginTargetSchemaTest, CheckScriptRejectsDrift) {
  SmallString<256> Script(NEVERC_SOURCE_DIR);
  sys::path::append(Script, "utils", "plugin-api", "check-target-schema.py");
  ASSERT_TRUE(sys::fs::exists(Script));

  auto Python = sys::findProgramByName("python3");
  ASSERT_TRUE(static_cast<bool>(Python)) << "python3 not found on PATH";
  std::vector<StringRef> Args = {
      *Python, Script.str(), "--check", "--generator",
      NEVERC_PLUGIN_TARGET_SCHEMA_GENERATOR};
  SmallString<256> ErrMsg;
  int RC = sys::ExecuteAndWait(*Python, Args, /*Env=*/{}, /*Redirects=*/{}, 0, 0,
                               &ErrMsg);
  ASSERT_EQ(RC, 0) << ErrMsg.str().str();
}

TEST(PluginTargetSchemaTest, CheckScriptRejectsValidButStaleGoldenFile) {
  SmallString<256> TemporaryDirectory;
  ASSERT_FALSE(sys::fs::createUniqueDirectory(
      "neverc-target-schema-test", TemporaryDirectory));
  auto Cleanup = make_scope_exit(
      [&] { (void)sys::fs::remove_directories(TemporaryDirectory); });

  for (StringRef Architecture : {"x86_64", "aarch64"}) {
    SmallString<256> Source(schemaRoot());
    sys::path::append(Source, Architecture + ".json");
    SmallString<256> Destination(TemporaryDirectory);
    sys::path::append(Destination, Architecture + ".json");
    ASSERT_FALSE(sys::fs::copy_file(Source, Destination));
  }

  SmallString<256> StaleSchema(TemporaryDirectory);
  sys::path::append(StaleSchema, "x86_64.json");
  std::error_code StreamError;
  raw_fd_ostream Stream(StaleSchema, StreamError, sys::fs::OF_Append);
  ASSERT_FALSE(StreamError);
  Stream << '\n';
  Stream.close();

  SmallString<256> Script(NEVERC_SOURCE_DIR);
  sys::path::append(Script, "utils", "plugin-api", "check-target-schema.py");
  auto Python = sys::findProgramByName("python3");
  ASSERT_TRUE(static_cast<bool>(Python)) << "python3 not found on PATH";
  std::vector<StringRef> Args = {
      *Python,
      Script.str(),
      "--check",
      "--generator",
      NEVERC_PLUGIN_TARGET_SCHEMA_GENERATOR,
      "--output-dir",
      TemporaryDirectory.str()};
  SmallString<256> ErrMsg;
  int RC = sys::ExecuteAndWait(*Python, Args, /*Env=*/{}, /*Redirects=*/{}, 0, 0,
                               &ErrMsg);
  EXPECT_NE(RC, 0);
}
