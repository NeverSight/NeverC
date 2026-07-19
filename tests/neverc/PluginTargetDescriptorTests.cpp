#include "neverc/Plugin/Host/PluginTargetDescriptor.h"
#include "llvm/Support/Error.h"
#include "gtest/gtest.h"
#include <string>
#include <utility>
#include <vector>

using namespace neverc::plugin;

namespace {

std::string text(NevercStringView View) {
  return std::string(View.Data ? View.Data : "",
                     static_cast<size_t>(View.Length));
}

NevercStringView view(const char *Text) {
  return {Text, static_cast<uint64_t>(std::char_traits<char>::length(Text))};
}

std::string errorText(llvm::Error ErrorValue) {
  auto Message = llvm::toString(std::move(ErrorValue));
  return Message.str().str();
}

NevercTargetMachineDescriptor machineDescriptor() {
  NevercTargetMachineDescriptor Descriptor{};
  Descriptor.Header = {sizeof(Descriptor), NEVERC_TARGET_API_MAJOR,
                       NEVERC_TARGET_API_MINOR, 0};
  Descriptor.RawTriple = view("x86_64-pc-linux-gnu");
  Descriptor.Architecture = view("x86_64");
  Descriptor.Vendor = view("pc");
  Descriptor.OperatingSystem = view("linux");
  Descriptor.Environment = view("gnu");
  Descriptor.DataLayout = view("e-p:64:64-i64:64-n32:64-S128");
  Descriptor.DefaultCPU = view("x86-64");
  Descriptor.GlobalLabelPrefix = view("");
  Descriptor.SchemaDigest = view(
      "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
  Descriptor.SupportedRelocationModels =
      NEVERC_TARGET_RELOCATION_MASK_STATIC |
      NEVERC_TARGET_RELOCATION_MASK_PIC;
  Descriptor.SupportedCodeModels = NEVERC_TARGET_CODE_MODEL_MASK_SMALL;
  Descriptor.DefaultRelocationModel = NEVERC_TARGET_RELOCATION_PIC;
  Descriptor.DefaultCodeModel = NEVERC_TARGET_CODE_MODEL_SMALL;
  Descriptor.ExceptionModel = NEVERC_TARGET_EXCEPTION_DWARF;
  Descriptor.UnwindModel = NEVERC_TARGET_UNWIND_DWARF;
  Descriptor.Endianness = NEVERC_TARGET_ENDIAN_LITTLE;
  Descriptor.PointerWidth = 64;
  Descriptor.IntWidth = 32;
  Descriptor.LongWidth = 64;
  Descriptor.LongLongWidth = 64;
  Descriptor.StackAlignment = 128;
  Descriptor.MaximumAtomicWidth = 128;
  Descriptor.MaximumVectorAlignment = 128;
  Descriptor.BuiltinVaListKind =
      NEVERC_TARGET_VA_LIST_X86_64;
  Descriptor.ExecutionLevels = NEVERC_TARGET_EXECUTION_USER;
  Descriptor.DefaultExecutionLevel = NEVERC_TARGET_EXECUTION_USER;
  Descriptor.TLSSupported = NEVERC_TRUE;
  return Descriptor;
}

TEST(PluginTargetDescriptorTest, TargetKeyPreservesRawUnknownIdentity) {
  TargetKeyBuilder Builder;
  Builder.setTargetID({UINT64_C(0x5000), UINT64_C(1)})
      .setTriple("quantum-acme-neveros-sandbox", "quantum", "acme",
                 "neveros", "sandbox")
      .setCPU("q2", "q2-tuned")
      .setFeatures({"zeta", "alpha"})
      .setABI({UINT64_C(0x5001), UINT64_C(1)})
      .setCallingConvention({UINT64_C(0x5002), UINT64_C(1)})
      .setObjectFormat({UINT64_C(0x5003), UINT64_C(1)})
      .setCodeGeneration(NEVERC_TARGET_RELOCATION_PIC,
                         NEVERC_TARGET_CODE_MODEL_SMALL)
      .setExecution(NEVERC_TARGET_EXECUTION_KERNEL, 128,
                    NEVERC_TARGET_ENDIAN_BIG)
      .setSchemaDigest(
          "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");

  auto Built = Builder.build();
  ASSERT_TRUE(static_cast<bool>(Built));
  NevercTargetKey View = Built->view();
  EXPECT_EQ(text(View.RawTriple), "quantum-acme-neveros-sandbox");
  EXPECT_EQ(text(View.Architecture), "quantum");
  EXPECT_EQ(text(View.CPU), "q2");
  EXPECT_EQ(View.PointerWidth, 128U);
  EXPECT_EQ(View.Endianness, NEVERC_TARGET_ENDIAN_BIG);
  ASSERT_EQ(View.Features.Count, 2U);
  const auto *Features = View.Features.Data;
  EXPECT_EQ(text(Features[0]), "alpha");
  EXPECT_EQ(text(Features[1]), "zeta");
}

TEST(PluginTargetDescriptorTest, RejectsDataLayoutPointerWidthMismatch) {
  NevercTargetMachineDescriptor Descriptor = machineDescriptor();
  Descriptor.PointerWidth = 32;

  auto Verified = verifyTargetMachineDescriptor(Descriptor);
  ASSERT_FALSE(static_cast<bool>(Verified));
  EXPECT_NE(errorText(Verified.takeError()).find("pointer width"),
            std::string::npos);
}

TEST(PluginTargetDescriptorTest, RejectsPrimitiveWidthTooLargeForTargetInfo) {
  NevercTargetMachineDescriptor Descriptor = machineDescriptor();
  Descriptor.DataLayout =
      view("e-p:256:256-i64:64-n32:64-S128");
  Descriptor.PointerWidth = 256;

  auto Verified = verifyTargetMachineDescriptor(Descriptor);
  ASSERT_FALSE(static_cast<bool>(Verified));
  EXPECT_NE(errorText(Verified.takeError()).find("TargetInfo width"),
            std::string::npos);
}

TEST(PluginTargetDescriptorTest, RejectsFeatureImplicationCycle) {
  const NevercStringView FirstImplies[] = {view("beta")};
  const NevercStringView SecondImplies[] = {view("alpha")};
  NevercTargetFeatureDescriptor Features[2]{};
  Features[0].Header = {sizeof(Features[0]), NEVERC_TARGET_API_MAJOR,
                        NEVERC_TARGET_API_MINOR, 0};
  Features[0].Name = view("alpha");
  Features[0].Implies = {FirstImplies, 1, sizeof(FirstImplies[0])};
  Features[1].Header = {sizeof(Features[1]), NEVERC_TARGET_API_MAJOR,
                        NEVERC_TARGET_API_MINOR, 0};
  Features[1].Name = view("beta");
  Features[1].Implies = {SecondImplies, 1, sizeof(SecondImplies[0])};
  NevercTargetMachineDescriptor Descriptor = machineDescriptor();
  Descriptor.Features = {Features, 2, sizeof(Features[0])};

  auto Verified = verifyTargetMachineDescriptor(Descriptor);
  ASSERT_FALSE(static_cast<bool>(Verified));
  EXPECT_NE(errorText(Verified.takeError()).find("cycle"),
            std::string::npos);
}

TEST(PluginTargetDescriptorTest, RejectsUnknownStableDiscriminant) {
  NevercTargetMachineDescriptor Descriptor = machineDescriptor();
  Descriptor.Endianness = UINT32_C(99);

  auto Verified = verifyTargetMachineDescriptor(Descriptor);
  ASSERT_FALSE(static_cast<bool>(Verified));
  EXPECT_NE(errorText(Verified.takeError()).find("invalid scalars"),
            std::string::npos);
}

TEST(PluginTargetDescriptorTest, RejectsNonzeroReservedField) {
  NevercTargetMachineDescriptor Descriptor = machineDescriptor();
  Descriptor.Reserved = 1;

  auto Verified = verifyTargetMachineDescriptor(Descriptor);
  ASSERT_FALSE(static_cast<bool>(Verified));
  EXPECT_NE(errorText(Verified.takeError()).find("invalid scalars"),
            std::string::npos);
}

TEST(PluginTargetDescriptorTest, RejectsMalformedFeatureArrayStride) {
  NevercTargetFeatureDescriptor Feature{};
  Feature.Header = {sizeof(Feature), NEVERC_TARGET_API_MAJOR,
                    NEVERC_TARGET_API_MINOR, 0};
  Feature.Name = view("simd");
  NevercTargetMachineDescriptor Descriptor = machineDescriptor();
  Descriptor.Features = {&Feature, 1, sizeof(Feature) - 1};

  auto Verified = verifyTargetMachineDescriptor(Descriptor);
  ASSERT_FALSE(static_cast<bool>(Verified));
  EXPECT_NE(errorText(Verified.takeError()).find("feature array"),
            std::string::npos);
}

TEST(PluginTargetDescriptorTest, RejectsUnsupportedDefaultCodeModel) {
  NevercTargetMachineDescriptor Descriptor = machineDescriptor();
  Descriptor.DefaultCodeModel = NEVERC_TARGET_CODE_MODEL_LARGE;
  Descriptor.SupportedCodeModels = NEVERC_TARGET_CODE_MODEL_MASK_SMALL;

  auto Verified = verifyTargetMachineDescriptor(Descriptor);
  ASSERT_FALSE(static_cast<bool>(Verified));
  EXPECT_NE(errorText(Verified.takeError()).find("code model"),
            std::string::npos);
}

TEST(PluginTargetDescriptorTest, AcceptsConsistentMachineDescriptor) {
  NevercTargetMachineDescriptor Descriptor = machineDescriptor();

  auto Verified = verifyTargetMachineDescriptor(Descriptor);
  ASSERT_TRUE(static_cast<bool>(Verified));
  EXPECT_EQ(Verified->RawTriple, "x86_64-pc-linux-gnu");
  EXPECT_EQ(Verified->PointerWidth, 64U);
  EXPECT_EQ(Verified->StackAlignment, 128U);
  EXPECT_TRUE(Verified->TLSSupported);
}

} // namespace
