#include "neverc/Plugin/Host/PluginABILowering.h"
#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "llvm/Support/Error.h"
#include "gtest/gtest.h"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;
using namespace neverc::plugin;

namespace {

std::string errorText(Error ErrorValue) {
  return toString(std::move(ErrorValue)).str().str();
}

NevercStatus NEVERC_CALL classifyFunction(
    void *UserData, const NevercABIFunctionQuery *Query,
    NevercABIArgumentClassification *ReturnValue,
    NevercABIArgumentClassificationArray *Arguments) {
  ++*static_cast<unsigned *>(UserData);
  if (!Query || !ReturnValue || !Arguments ||
      Query->Parameters.Count != Arguments->Count) {
    NevercStatus Status{};
    Status.Code = NEVERC_STATUS_INVALID_ARGUMENT;
    return Status;
  }

  ReturnValue->Kind = NEVERC_ABI_ARGUMENT_DIRECT;
  ReturnValue->Coercion = NEVERC_ABI_COERCE_INTEGER;
  ReturnValue->CoercionBitWidth = 64;
  auto *Argument = reinterpret_cast<NevercABIArgumentClassification *>(
      Arguments->Data);
  Argument->Kind = NEVERC_ABI_ARGUMENT_INDIRECT;
  Argument->Alignment = 16;
  Argument->Flags = NEVERC_ABI_ARGUMENT_BYVAL;
  return neverc_status_ok();
}

struct ClassifierPolicy {
  NevercABIArgumentKind Kind;
  bool SawVariadic = false;
  uint32_t RequiredArguments = UINT32_MAX;
};

NevercStatus NEVERC_CALL classifyWithPolicy(
    void *UserData, const NevercABIFunctionQuery *Query,
    NevercABIArgumentClassification *ReturnValue,
    NevercABIArgumentClassificationArray *Arguments) {
  auto &Policy = *static_cast<ClassifierPolicy *>(UserData);
  Policy.SawVariadic = Query->Variadic != NEVERC_FALSE;
  Policy.RequiredArguments = Query->RequiredArgumentCount;
  ReturnValue->Kind = NEVERC_ABI_ARGUMENT_DIRECT;
  for (uint64_t I = 0; I != Arguments->Count; ++I) {
    auto *Argument = reinterpret_cast<
        NevercABIArgumentClassification *>(
        reinterpret_cast<uint8_t *>(Arguments->Data) +
        I * Arguments->ElementStride);
    Argument->Kind = Policy.Kind;
    if (Policy.Kind == NEVERC_ABI_ARGUMENT_INDIRECT) {
      Argument->Alignment = 8;
      Argument->Flags = NEVERC_ABI_ARGUMENT_BYVAL;
    }
  }
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL classifyInvalidDirect(
    void *, const NevercABIFunctionQuery *,
    NevercABIArgumentClassification *ReturnValue,
    NevercABIArgumentClassificationArray *) {
  ReturnValue->Kind = NEVERC_ABI_ARGUMENT_DIRECT;
  ReturnValue->Flags = NEVERC_ABI_ARGUMENT_BYVAL;
  return neverc_status_ok();
}

NevercABITypeDescriptor type(NevercABITypeKind Kind,
                              uint32_t Width,
                              uint32_t Alignment) {
  NevercABITypeDescriptor Type{};
  Type.Header = {sizeof(Type), NEVERC_TARGET_ABI_API_MAJOR,
                 NEVERC_TARGET_ABI_API_MINOR, 0};
  Type.Kind = Kind;
  Type.BitWidth = Width;
  Type.Alignment = Alignment;
  return Type;
}

TEST(PluginABILoweringTest, InvokesTypedClassifierAndOwnsResult) {
  unsigned Calls = 0;
  PluginTargetSnapshot::NamedRecord ABI;
  ABI.CanonicalName = "test.abi";
  ABI.ClassifyFunction = classifyFunction;
  ABI.CallbackUserData = &Calls;
  ABI.VAArg.Header = {sizeof(ABI.VAArg),
                      NEVERC_TARGET_ABI_API_MAJOR,
                      NEVERC_TARGET_ABI_API_MINOR, 0};
  ABI.VAArg.Kind = NEVERC_ABI_VA_ARG_LLVM;
  PluginTargetSnapshot::NamedRecord CallingConvention;
  CallingConvention.LLVMCallingConvention = 102;

  const NevercABITypeDescriptor ReturnType =
      type(NEVERC_ABI_TYPE_INTEGER, 32, 4);
  const std::vector<NevercABITypeDescriptor> Parameters = {
      type(NEVERC_ABI_TYPE_RECORD, 128, 16)};
  PluginABILowering Lowering(ABI, &CallingConvention);
  auto Result = Lowering.classify(ReturnType, Parameters,
                                  /*Variadic=*/false,
                                  /*RequiredArguments=*/1);

  ASSERT_TRUE(static_cast<bool>(Result))
      << errorText(Result.takeError());
  EXPECT_EQ(Calls, 1U);
  EXPECT_EQ(Result->LLVMCallingConvention, 102U);
  EXPECT_EQ(Result->ReturnValue.Kind, NEVERC_ABI_ARGUMENT_DIRECT);
  EXPECT_EQ(Result->ReturnValue.CoercionBitWidth, 64U);
  ASSERT_EQ(Result->Arguments.size(), 1U);
  EXPECT_EQ(Result->Arguments[0].Kind,
            NEVERC_ABI_ARGUMENT_INDIRECT);
  EXPECT_EQ(Result->Arguments[0].Alignment, 16U);
  EXPECT_NE(Result->Arguments[0].Flags &
                NEVERC_ABI_ARGUMENT_BYVAL,
            0U);
}

TEST(PluginABILoweringTest, RejectsMissingClassifier) {
  PluginTargetSnapshot::NamedRecord ABI;
  ABI.CanonicalName = "test.missing";
  PluginABILowering Lowering(ABI, nullptr);
  auto Result = Lowering.classify(
      type(NEVERC_ABI_TYPE_VOID, 0, 1), {},
      /*Variadic=*/false, /*RequiredArguments=*/0);

  ASSERT_FALSE(static_cast<bool>(Result));
  EXPECT_NE(errorText(Result.takeError()).find("classifier"),
            std::string::npos);
}

TEST(PluginABILoweringTest,
     SameSignatureCanUseDifferentTargetABIClassifications) {
  ClassifierPolicy DirectPolicy{
      NEVERC_ABI_ARGUMENT_DIRECT};
  ClassifierPolicy IndirectPolicy{
      NEVERC_ABI_ARGUMENT_INDIRECT};
  PluginTargetSnapshot::NamedRecord DirectABI;
  DirectABI.CanonicalName = "target-a.abi";
  DirectABI.ClassifyFunction = classifyWithPolicy;
  DirectABI.CallbackUserData = &DirectPolicy;
  PluginTargetSnapshot::NamedRecord IndirectABI;
  IndirectABI.CanonicalName = "target-b.abi";
  IndirectABI.ClassifyFunction = classifyWithPolicy;
  IndirectABI.CallbackUserData = &IndirectPolicy;
  const NevercABITypeDescriptor ReturnType =
      type(NEVERC_ABI_TYPE_INTEGER, 32, 4);
  const std::vector<NevercABITypeDescriptor> Parameters = {
      type(NEVERC_ABI_TYPE_RECORD, 64, 8)};

  auto Direct = PluginABILowering(DirectABI, nullptr)
                    .classify(ReturnType, Parameters, false, 1);
  auto Indirect = PluginABILowering(IndirectABI, nullptr)
                      .classify(ReturnType, Parameters, false, 1);

  ASSERT_TRUE(static_cast<bool>(Direct))
      << errorText(Direct.takeError());
  ASSERT_TRUE(static_cast<bool>(Indirect))
      << errorText(Indirect.takeError());
  EXPECT_EQ(Direct->Arguments[0].Kind,
            NEVERC_ABI_ARGUMENT_DIRECT);
  EXPECT_EQ(Indirect->Arguments[0].Kind,
            NEVERC_ABI_ARGUMENT_INDIRECT);
}

TEST(PluginABILoweringTest, PreservesVariadicBoundaryInQuery) {
  ClassifierPolicy Policy{NEVERC_ABI_ARGUMENT_DIRECT};
  PluginTargetSnapshot::NamedRecord ABI;
  ABI.CanonicalName = "test.variadic";
  ABI.ClassifyFunction = classifyWithPolicy;
  ABI.CallbackUserData = &Policy;
  const std::vector<NevercABITypeDescriptor> Parameters = {
      type(NEVERC_ABI_TYPE_INTEGER, 32, 4),
      type(NEVERC_ABI_TYPE_FLOAT, 64, 8)};

  auto Result = PluginABILowering(ABI, nullptr)
                    .classify(type(NEVERC_ABI_TYPE_VOID, 0, 0),
                              Parameters, true, 1);

  ASSERT_TRUE(static_cast<bool>(Result))
      << errorText(Result.takeError());
  EXPECT_TRUE(Policy.SawVariadic);
  EXPECT_EQ(Policy.RequiredArguments, 1U);
  ASSERT_EQ(Result->Arguments.size(), 2U);
  EXPECT_EQ(Result->Arguments[1].Kind,
            NEVERC_ABI_ARGUMENT_DIRECT);
}

TEST(PluginABILoweringTest, RejectsInconsistentClassification) {
  PluginTargetSnapshot::NamedRecord ABI;
  ABI.CanonicalName = "test.invalid";
  ABI.ClassifyFunction = classifyInvalidDirect;

  auto Result = PluginABILowering(ABI, nullptr)
                    .classify(type(NEVERC_ABI_TYPE_INTEGER, 32, 4),
                              {}, false, 0);

  ASSERT_FALSE(static_cast<bool>(Result));
  EXPECT_NE(errorText(Result.takeError()).find("indirect flags"),
            std::string::npos);
}

} // namespace
