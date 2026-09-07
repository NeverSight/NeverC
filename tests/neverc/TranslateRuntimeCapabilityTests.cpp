#include "../../neverc/lib/Translate/Cpp/LibraryMappings.h"
#include "NeverCTestFixture.h"
#include "neverc/Foundation/Std/BuiltinStd.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>

namespace {
using namespace neverc::translate;
class TranslateRuntimeCapabilityTest : public NeverCTest {
protected:
  static constexpr const char *Target = "x86_64-apple-macosx15.0.0";
  RuntimeCapabilityProviders Providers;
  MathRuntimeCapabilities Capabilities;
  Diagnostics Errors;
  std::vector<RuntimeModuleData> Modules;

  void SetUp() override {
    NeverCTest::SetUp();
    Providers.ReadHeader = [&](llvm::StringRef, std::string &Bytes,
                               std::string &) {
      Bytes = readFile(testDir().parent_path().parent_path() /
                       "std/include/neverc/std/math.h");
      return !Bytes.empty();
    };
    Providers.Modules = [&](llvm::StringRef) { return Modules; };
  }
  bool inspect(std::vector<std::string> IDs = {"cpp.math.fabs.f64.v1"},
               bool Enabled = true, const std::string &Triple = Target) {
    Errors.clear();
    return inspectMathRuntime(Triple, tmp().string(), IDs, Enabled,
                              Capabilities, Errors, &Providers);
  }
  void rejected(const std::string &Reason,
                std::vector<std::string> IDs = {"cpp.math.fabs.f64.v1"},
                bool Enabled = true, const std::string &Triple = Target) {
    ASSERT_FALSE(inspect(IDs, Enabled, Triple));
    ASSERT_FALSE(Errors.empty());
    EXPECT_EQ(Errors.front().Code, "TR0403");
    EXPECT_NE(Errors.front().Reason.find(Reason), std::string::npos)
        << Errors.front().Reason;
    EXPECT_TRUE(Capabilities.ID.empty());
    EXPECT_TRUE(Capabilities.Modules.empty());
  }
  std::string bitcode(llvm::Module &M) {
    std::string Bytes;
    llvm::raw_string_ostream OS(Bytes);
    llvm::WriteBitcodeToFile(M, OS);
    return Bytes;
  }
  enum class Variant {
    Ordinary,
    Declaration,
    WrongName,
    WrongType,
    WrongCallingConvention,
    WrongTarget,
    WrongLayout,
    ExternalDependency
  };
  RuntimeModuleData controlled(Variant V = Variant::Ordinary) {
    llvm::LLVMContext C;
    llvm::Module M("controlled test payload", C);
    M.setTargetTriple(V == Variant::WrongTarget ? "arm64-apple-macosx11.0.0"
                                                : "x86_64-apple-macosx11.0.0");
    M.setDataLayout(V == Variant::WrongLayout ? "E-p:64:64-i64:64-f64:64"
                                              : "e-p:64:64-i64:64-f64:64");
    auto *Double = llvm::Type::getDoubleTy(C);
    auto *Result = V == Variant::WrongType ? llvm::Type::getInt32Ty(C) : Double;
    auto *FT = llvm::FunctionType::get(Result, {Double}, false);
    auto *F = llvm::Function::Create(
        FT, llvm::GlobalValue::ExternalLinkage,
        V == Variant::WrongName ? "wrong_math_abs" : "neverc_math_abs", M);
    if (V == Variant::WrongCallingConvention)
      F->setCallingConv(llvm::CallingConv::Fast);
    if (V != Variant::Declaration) {
      auto *B = llvm::BasicBlock::Create(C, "entry", F);
      llvm::Value *Value = F->getArg(0);
      if (V == Variant::WrongType)
        Value = llvm::ConstantInt::get(Result, 0);
      if (V == Variant::ExternalDependency) {
        auto *Other = llvm::Function::Create(
            llvm::FunctionType::get(Double, {Double}, false),
            llvm::GlobalValue::ExternalLinkage, "neverc_math_modf", M);
        Value = llvm::CallInst::Create(Other->getFunctionType(), Other,
                                       {F->getArg(0)}, "value", B);
      }
      llvm::ReturnInst::Create(C, Value, B);
    }
    return {"math_abs", bitcode(M)};
  }
  std::vector<RuntimeModuleData> actual(const std::string &Triple) {
    std::vector<RuntimeModuleData> Result;
    llvm::Triple T(Triple);
    for (unsigned I = 0, Count = neverc::BuiltinStd::getEmbeddedModuleCount(T);
         I < Count; ++I) {
      auto [Name, Data] = neverc::BuiltinStd::getEmbeddedModule(T, I);
      if (Name == "math_abs" || Name == "math_floor")
        Result.push_back({Name.str(), Data.str()});
    }
    return Result;
  }
};

TEST_F(TranslateRuntimeCapabilityTest,
       MissingAndModifiedInstalledHeadersFailBeforePayloadLookup) {
  bool Queried = false;
  Providers.Modules = [&](llvm::StringRef) {
    Queried = true;
    return Modules;
  };
  Providers.ReadHeader = [](llvm::StringRef, std::string &, std::string &Why) {
    Why = "controlled missing file";
    return false;
  };
  rejected("header is unavailable");
  EXPECT_FALSE(Queried);
  Providers.ReadHeader = [](llvm::StringRef, std::string &Bytes,
                            std::string &) {
    Bytes = "double neverc_math_abs(double);\n";
    return true;
  };
  rejected("header identity");
  EXPECT_FALSE(Queried);
}

TEST_F(TranslateRuntimeCapabilityTest,
       DisabledRuntimeAndUnknownMappingNeverConsultProviders) {
  bool Queried = false;
  Providers.ReadHeader = [&](llvm::StringRef, std::string &, std::string &) {
    Queried = true;
    return false;
  };
  rejected("disabled", {"cpp.math.fabs.f64.v1"}, false);
  rejected("unknown mapping", {"unapproved.operation"});
  EXPECT_FALSE(Queried);
}

TEST_F(TranslateRuntimeCapabilityTest,
       MissingEmptyDuplicateAndMalformedPayloadsAreRejected) {
  rejected("payload is absent");
  Modules = {{"math_abs", ""}};
  rejected("empty");
  Modules = {{"math_abs", "invalid bitcode"}};
  rejected("malformed");
  Modules.push_back(Modules.front());
  rejected("duplicate");
}

TEST_F(TranslateRuntimeCapabilityTest,
       DefinitionSignatureCallingConventionAndTargetAreExact) {
  for (auto V : {Variant::Declaration, Variant::WrongName, Variant::WrongType,
                 Variant::WrongCallingConvention}) {
    Modules = {controlled(V)};
    rejected("required runtime symbol");
  }
  Modules = {controlled(Variant::WrongTarget)};
  rejected("target or binary64 data layout");
  Modules = {controlled(Variant::WrongLayout)};
  rejected("target or binary64 data layout");
}

TEST_F(TranslateRuntimeCapabilityTest,
       DependenciesAndUnapprovedImplementationsCannotPassBySignatureAlone) {
  Modules = {controlled(Variant::ExternalDependency)};
  rejected("unapproved external dependency");
  Modules = {controlled()};
  rejected("implementation fingerprint is unapproved");
  EXPECT_NE(Errors.front().Reason.find("expected "), std::string::npos);
  EXPECT_NE(Errors.front().Reason.find("observed "), std::string::npos);
}

TEST_F(TranslateRuntimeCapabilityTest,
       UnsupportedConsumerTargetsFailBeforeCapabilities) {
  ASSERT_FALSE(
      inspect({"cpp.math.fabs.f64.v1"}, true, "x86_64-unknown-linux-gnu"));
  ASSERT_FALSE(Errors.empty());
  EXPECT_EQ(Errors.front().Code, "TR0204");
  EXPECT_TRUE(Capabilities.ID.empty());
}

TEST_F(TranslateRuntimeCapabilityTest,
       MappingFreeMathDoesNotRequireUnneededRuntimePayloads) {
  Providers.ReadHeader = [](llvm::StringRef, std::string &, std::string &) {
    ADD_FAILURE();
    return false;
  };
  ASSERT_TRUE(inspect({}, false));
  EXPECT_EQ(Capabilities.ID.size(), 64u);
  EXPECT_TRUE(Capabilities.Modules.empty());
  EXPECT_TRUE(Capabilities.HeaderRelativePath.empty());
}

TEST_F(TranslateRuntimeCapabilityTest,
       BootstrappedMathPayloadsMatchBothApprovedArchitectureIdentities) {
  for (const std::string &Triple :
       {"x86_64-apple-macosx15.0.0", "arm64-apple-macosx15.0.0"}) {
    Modules = actual(Triple);
    if (Modules.size() != 2)
      GTEST_SKIP() << "math runtime not bootstrapped for " << Triple;
    ASSERT_TRUE(inspect({"cpp.math.floor.f64.v1", "cpp.math.fabs.f64.v1"}, true,
                        Triple))
        << (Errors.empty() ? "" : Errors.front().Reason);
    ASSERT_EQ(Capabilities.Modules.size(), 2u);
    EXPECT_EQ(Capabilities.HeaderSHA256,
              approvedMathRuntimeHeaderSHA256().str());
    for (const auto &Module : Capabilities.Modules) {
      EXPECT_EQ(Module.ImplementationIdentity,
                approvedMathRuntimeIdentity(Triple, Module.Name).str());
      EXPECT_EQ(Module.SHA256.size(), 64u);
      EXPECT_EQ(Module.DefinedSymbols.size(), 1u);
      EXPECT_TRUE(Module.RequiredSymbols.empty());
    }
  }
}

TEST_F(TranslateRuntimeCapabilityTest,
       FingerprintsIgnoreLocationMetadataButPreserveSemanticAttributes) {
  Modules = actual(Target);
  if (Modules.empty())
    GTEST_SKIP() << "math runtime not bootstrapped";
  const auto Original = Modules.front();
  std::string Expected;
  ASSERT_TRUE(
      mathRuntimeImplementationIdentity(Original.Bitcode, Expected, Errors));
  llvm::LLVMContext C;
  auto Buffer =
      llvm::MemoryBuffer::getMemBuffer(Original.Bitcode, "original", false);
  auto Parsed = llvm::parseBitcodeFile(Buffer->getMemBufferRef(), C);
  ASSERT_TRUE(bool(Parsed));
  auto &M = **Parsed;
  M.setModuleIdentifier("different checkout/module");
  M.setSourceFileName("/relocated/install/abs.c");
  auto *Ident = M.getOrInsertNamedMetadata("llvm.ident");
  Ident->clearOperands();
  Ident->addOperand(llvm::MDNode::get(
      C, llvm::MDString::get(C, "different compiler installation path")));
  const auto Relocated = bitcode(M);
  EXPECT_NE(Relocated, Original.Bitcode);
  std::string Observed;
  ASSERT_TRUE(mathRuntimeImplementationIdentity(Relocated, Observed, Errors));
  EXPECT_EQ(Observed, Expected);
  for (auto &F : M)
    if (!F.isDeclaration())
      F.addFnAttr("no-signed-zeros-fp-math", "true");
  const auto Changed = bitcode(M);
  ASSERT_TRUE(mathRuntimeImplementationIdentity(Changed, Observed, Errors));
  EXPECT_NE(Observed, Expected);
  Modules = {{Original.Name, Changed}};
  rejected("implementation fingerprint is unapproved");
}

TEST_F(TranslateRuntimeCapabilityTest,
       MissingSecondCapabilityReturnsNoPartialApproval) {
  Modules = actual(Target);
  if (Modules.empty())
    GTEST_SKIP() << "math runtime not bootstrapped";
  Modules.erase(std::remove_if(Modules.begin(), Modules.end(),
                               [](const RuntimeModuleData &M) {
                                 return M.Name == "math_floor";
                               }),
                Modules.end());
  rejected("payload is absent",
           {"cpp.math.fabs.f64.v1", "cpp.math.floor.f64.v1"});
}
} // namespace
