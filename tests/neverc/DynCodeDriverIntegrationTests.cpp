#include "neverc/DynCode/Pipeline/DriverIntegration.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

#include "gtest/gtest.h"

#include <string>

using namespace neverc::dyncode;

namespace {

bool containsArgument(llvm::ArrayRef<const char *> Args,
                      llvm::StringRef Expected) {
  for (const char *Arg : Args)
    if (Arg && llvm::StringRef(Arg) == Expected)
      return true;
  return false;
}

} // namespace

TEST(DynCodeDriverIntegrationTest,
     OrdinaryDriverArgumentsDoNotNeedDynCodePreparation) {
  llvm::SmallVector<const char *, 8> Args = {
      "neverc", "--target=x86_64-linux-gnu", "input.o", "-o", "output"};
  const auto OriginalArgs = Args;
  DynCodeDriverSetup Setup;

  EXPECT_EQ(prepareDriverDynCode(Args, Setup), 0);
  EXPECT_EQ(Args, OriginalArgs);
  EXPECT_FALSE(Setup.Enabled);
  EXPECT_FALSE(Setup.Opts.Enabled);
  EXPECT_TRUE(Setup.StringPool.empty());
}

TEST(DynCodeDriverIntegrationTest, NonEnableSpellingsLeavePreparationDisabled) {
  llvm::SmallVector<const char *, 8> Args = {
      "neverc", "-fno-dyncode", "-fdyncode=1", "--fdyncode", "path/-fdyncode"};
  const auto OriginalArgs = Args;
  DynCodeDriverSetup Setup;

  EXPECT_EQ(prepareDriverDynCode(Args, Setup), 0);
  EXPECT_EQ(Args, OriginalArgs);
  EXPECT_FALSE(Setup.Enabled);
  EXPECT_FALSE(Setup.Opts.Enabled);
  EXPECT_TRUE(Setup.StringPool.empty());
}

TEST(DynCodeDriverIntegrationTest,
     MalformedModifierWithoutEnableStillReportsItsDiagnostic) {
  llvm::SmallVector<const char *, 4> Args = {"neverc", "-fdyncode-bad-bytes=zz",
                                             "input.c"};
  DynCodeDriverSetup Setup;

  EXPECT_NE(prepareDriverDynCode(Args, Setup), 0);
  EXPECT_FALSE(Setup.Enabled);
  EXPECT_FALSE(Setup.Opts.Enabled);
}

TEST(DynCodeDriverIntegrationTest,
     ExplicitDynCodeTokenRunsPreparationAndInjectsFrontendMode) {
  llvm::SmallVector<const char *, 8> Args = {
      "neverc", "-fdyncode", "--target=x86_64-linux-gnu", "input.c"};
  DynCodeDriverSetup Setup;

  EXPECT_EQ(prepareDriverDynCode(Args, Setup), 0);
  EXPECT_TRUE(Setup.Enabled);
  EXPECT_TRUE(Setup.Opts.Enabled);
  EXPECT_EQ(Setup.Opts.TargetTriple, "x86_64-unknown-linux-gnu");
  EXPECT_TRUE(containsArgument(Args, "-fdyncode-mode"));
  EXPECT_FALSE(Setup.StringPool.empty());
}

TEST(DynCodeDriverIntegrationTest,
     LaterDisableTokenPreservesDriverOptionOrdering) {
  llvm::SmallVector<const char *, 8> Args = {
      "neverc", "-fdyncode", "-fno-dyncode", "--target=x86_64-linux-gnu",
      "input.c"};
  const auto OriginalArgs = Args;
  DynCodeDriverSetup Setup;

  // The enable token deliberately enters the full parser so hasFlag can honor
  // the later disable token exactly as before the fast path.
  EXPECT_EQ(prepareDriverDynCode(Args, Setup), 0);
  EXPECT_EQ(Args, OriginalArgs);
  EXPECT_FALSE(Setup.Enabled);
  EXPECT_FALSE(Setup.Opts.Enabled);
  EXPECT_TRUE(Setup.StringPool.empty());
}

TEST(DynCodeDriverIntegrationTest,
     ExpandedResponseFileTokenStillEnablesPreparation) {
  llvm::SmallVector<const char *, 4> UnexpandedArgs = {"neverc", "@driver.rsp"};
  const auto OriginalUnexpandedArgs = UnexpandedArgs;
  DynCodeDriverSetup UnexpandedSetup;
  EXPECT_EQ(prepareDriverDynCode(UnexpandedArgs, UnexpandedSetup), 0);
  EXPECT_EQ(UnexpandedArgs, OriginalUnexpandedArgs);
  EXPECT_FALSE(UnexpandedSetup.Enabled);

  // main expands response files before calling prepareDriverDynCode. Use
  // separately owned storage to prove the fast path compares token contents,
  // not the address of the string literal.
  std::string ExpandedEnableToken = "-fdyncode";
  llvm::SmallVector<const char *, 8> ExpandedArgs = {
      "neverc", ExpandedEnableToken.c_str(), "--target=x86_64-linux-gnu",
      "input.c"};
  DynCodeDriverSetup Setup;

  EXPECT_EQ(prepareDriverDynCode(ExpandedArgs, Setup), 0);
  EXPECT_TRUE(Setup.Enabled);
  EXPECT_TRUE(Setup.Opts.Enabled);
  EXPECT_TRUE(containsArgument(ExpandedArgs, "-fdyncode-mode"));
}
