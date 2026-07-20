#include "neverc/Plugin/Host/CodeGenArtifacts.h"
#include "neverc/Plugin/Host/CodeGenRoutePlanner.h"
#include "neverc/Plugin/Host/PluginCodeGenProvider.h"
#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "llvm/Support/Error.h"
#include "gtest/gtest.h"
#include <array>
#include <cstring>
#include <string>
#include <vector>

using namespace llvm;
using namespace neverc::plugin;

namespace {

constexpr NevercTargetID TargetID{UINT64_C(0x7000), UINT64_C(1)};
constexpr NevercInterfaceID IRProduct{UINT64_C(0x7100), UINT64_C(1)};
constexpr NevercInterfaceID MIRProduct{UINT64_C(0x7100), UINT64_C(2)};
constexpr NevercInterfaceID MCProduct{UINT64_C(0x7100), UINT64_C(3)};
constexpr NevercInterfaceID ObjectProduct{UINT64_C(0x7100), UINT64_C(4)};

std::string errorText(Error ErrorValue) {
  return toString(std::move(ErrorValue)).str().str();
}

PluginTargetSnapshot::CodeGenEdgeRecord
edge(uint64_t ID, const char *Name, NevercCodeGenProductKind Input,
     NevercCodeGenProductKind Output, NevercInterfaceID Product,
     const char *Provider) {
  PluginTargetSnapshot::CodeGenEdgeRecord Result;
  Result.ID = {UINT64_C(0x7200), ID};
  Result.TargetID = TargetID;
  Result.CanonicalName = Name;
  Result.InputKind = Input;
  Result.OutputKind = Output;
  Result.ProductID = Product;
  Result.ProviderID = Provider;
  Result.CompatibilityKey = "target-key-1";
  return Result;
}

NevercStatus failure(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

struct CallbackState {
  unsigned LowerCalls = 0;
  unsigned VerifyCalls = 0;
  NevercCodeGenProductKind Kind = NEVERC_CODEGEN_PRODUCT_OBJECT_IMAGE;
  NevercInterfaceID ProductID = ObjectProduct;
  NevercArtifactHandle Artifact{UINT64_C(0x9000), UINT64_C(1)};
  NevercProofHandle ForgedProof{};
  NevercStatusCode LowerStatus = NEVERC_STATUS_OK;
  NevercStatusCode VerifyStatus = NEVERC_STATUS_OK;
};

NevercStatus NEVERC_CALL
coarseLower(void *UserData, NevercTaskHandle,
            const NevercCodeGenRequest *,
            NevercCodeGenProductCandidate *OutCandidate) {
  auto &State = *static_cast<CallbackState *>(UserData);
  ++State.LowerCalls;
  if (State.LowerStatus != NEVERC_STATUS_OK)
    return failure(State.LowerStatus);
  if (!OutCandidate ||
      OutCandidate->Header.StructSize < sizeof(*OutCandidate))
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  const uint32_t Capacity = OutCandidate->Header.StructSize;
  NevercCodeGenProductCandidate Candidate{};
  Candidate.Header = {sizeof(Candidate), NEVERC_TARGET_API_MAJOR,
                      NEVERC_TARGET_API_MINOR, 0};
  Candidate.Kind = State.Kind;
  Candidate.Artifact = State.Artifact;
  Candidate.ProductID = State.ProductID;
  Candidate.Proof = State.ForgedProof;
  std::memcpy(OutCandidate, &Candidate,
              std::min<size_t>(Capacity, sizeof(Candidate)));
  OutCandidate->Header.StructSize = sizeof(Candidate);
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL
verifyProduct(void *UserData, NevercTaskHandle,
              const NevercCodeGenRequest *,
              const NevercCodeGenProductCandidate *,
              NevercCodeGenVerificationObligations Obligations) {
  auto &State = *static_cast<CallbackState *>(UserData);
  ++State.VerifyCalls;
  const NevercCodeGenVerificationObligations Required =
      NEVERC_CODEGEN_VERIFY_TARGET_KEY |
      NEVERC_CODEGEN_VERIFY_PRODUCT_KIND |
      NEVERC_CODEGEN_VERIFY_PRODUCT_ID |
      NEVERC_CODEGEN_VERIFY_STRUCTURE;
  if ((Obligations & Required) != Required)
    return failure(NEVERC_STATUS_PLUGIN_FAILURE);
  return State.VerifyStatus == NEVERC_STATUS_OK
             ? neverc_status_ok()
             : failure(State.VerifyStatus);
}

CodeGenRouteRequest request(NevercCodeGenProductKind Output) {
  CodeGenRouteRequest Request;
  Request.TargetID = TargetID;
  Request.InputKind = NEVERC_CODEGEN_PRODUCT_IR;
  Request.OutputKind = Output;
  Request.CompatibilityKey = "target-key-1";
  return Request;
}

CodeGenExecutionRequest executionRequest() {
  CodeGenExecutionRequest Request;
  Request.Task = {UINT64_C(0x8000), UINT64_C(1)};
  Request.Input = {UINT64_C(0x8100), UINT64_C(1)};
  Request.InputKind = NEVERC_CODEGEN_PRODUCT_IR;
  Request.OutputKind = NEVERC_CODEGEN_PRODUCT_OBJECT_IMAGE;
  Request.CompatibilityKey = "target-key-1";
  Request.HasFinalIRProof = true;
  return Request;
}

TEST(PluginCodeGenRouteTest, RegistersTypedCodeGenArtifacts) {
  PluginArtifactRegistry Registry;
  auto Types = registerCodeGenArtifactTypes(Registry);
  ASSERT_TRUE(static_cast<bool>(Types)) << errorText(Types.takeError());
  EXPECT_EQ(Registry.size(), 7U);
  EXPECT_EQ(Types->TargetSelection->name(), "target.selection");
  EXPECT_EQ(Types->Request->name(), "codegen.request");
  EXPECT_EQ(Types->IRModule->name(), "ir.module");
  EXPECT_EQ(Types->MIRModule->name(), "mir.module");
  EXPECT_EQ(Types->MCUnit->name(), "mc.unit");
  EXPECT_EQ(Types->ObjectGraph->name(), "object.graph");
  EXPECT_EQ(Types->ObjectImage->name(), "object.image");
}

TEST(PluginCodeGenRouteTest, RejectsMissingAndAmbiguousRoutes) {
  std::vector<PluginTargetSnapshot::CodeGenEdgeRecord> Edges;
  Edges.push_back(edge(1, "ir-to-mir", NEVERC_CODEGEN_PRODUCT_IR,
                       NEVERC_CODEGEN_PRODUCT_MIR, MIRProduct, "first"));

  auto Missing = CodeGenRoutePlanner::plan(Edges,
                                           request(NEVERC_CODEGEN_PRODUCT_MC));
  ASSERT_FALSE(static_cast<bool>(Missing));
  EXPECT_NE(errorText(Missing.takeError()).find("missing codegen edge"),
            std::string::npos);

  Edges.push_back(edge(2, "ir-to-mc-a", NEVERC_CODEGEN_PRODUCT_IR,
                       NEVERC_CODEGEN_PRODUCT_MC, MCProduct, "a"));
  Edges.push_back(edge(3, "ir-to-mc-b", NEVERC_CODEGEN_PRODUCT_IR,
                       NEVERC_CODEGEN_PRODUCT_MC, MCProduct, "b"));
  Edges[1].Flags = NEVERC_CODEGEN_EDGE_BUILTIN;
  Edges[2].Flags = NEVERC_CODEGEN_EDGE_BUILTIN;
  auto Ambiguous = CodeGenRoutePlanner::plan(
      Edges, request(NEVERC_CODEGEN_PRODUCT_MC));
  ASSERT_FALSE(static_cast<bool>(Ambiguous));
  EXPECT_NE(errorText(Ambiguous.takeError()).find("ambiguous codegen route"),
            std::string::npos);
}

TEST(PluginCodeGenRouteTest, ForcedProviderSelectsOneCompleteRoute) {
  std::array<PluginTargetSnapshot::CodeGenEdgeRecord, 2> Edges = {
      edge(1, "ir-to-object-a", NEVERC_CODEGEN_PRODUCT_IR,
           NEVERC_CODEGEN_PRODUCT_OBJECT_IMAGE, ObjectProduct, "a"),
      edge(2, "ir-to-object-b", NEVERC_CODEGEN_PRODUCT_IR,
           NEVERC_CODEGEN_PRODUCT_OBJECT_IMAGE, ObjectProduct, "b")};
  Edges[0].Flags = NEVERC_CODEGEN_EDGE_COARSE;
  Edges[1].Flags = NEVERC_CODEGEN_EDGE_COARSE;
  Edges[0].VerifyProduct = verifyProduct;
  Edges[1].VerifyProduct = verifyProduct;

  CodeGenRouteRequest Request =
      request(NEVERC_CODEGEN_PRODUCT_OBJECT_IMAGE);
  Request.ForcedProvider = "b";
  auto Plan = CodeGenRoutePlanner::plan(Edges, Request);
  ASSERT_TRUE(static_cast<bool>(Plan)) << errorText(Plan.takeError());
  ASSERT_EQ(Plan->edges().size(), 1U);
  EXPECT_EQ(Plan->edges().front()->ProviderID, "b");
}

TEST(PluginCodeGenRouteTest,
     CoarseProviderBypassesBuiltinAndRunsMandatoryVerifier) {
  CallbackState State;
  auto Edge = edge(1, "coarse-ir-to-object", NEVERC_CODEGEN_PRODUCT_IR,
                   NEVERC_CODEGEN_PRODUCT_OBJECT_IMAGE, ObjectProduct,
                   "coarse");
  Edge.Flags = NEVERC_CODEGEN_EDGE_COARSE;
  Edge.CoarseLower = coarseLower;
  Edge.VerifyProduct = verifyProduct;
  Edge.CallbackUserData = &State;
  auto Plan = CodeGenRoutePlanner::plan(
      ArrayRef<PluginTargetSnapshot::CodeGenEdgeRecord>(Edge),
      request(NEVERC_CODEGEN_PRODUCT_OBJECT_IMAGE));
  ASSERT_TRUE(static_cast<bool>(Plan)) << errorText(Plan.takeError());

  unsigned BuiltinCalls = 0;
  auto Result = PluginCodeGenProviderRuntime::execute(
      *Plan, executionRequest(),
      [&](const PluginTargetSnapshot::CodeGenEdgeRecord &,
          const CodeGenExecutionRequest &)
          -> Expected<NevercCodeGenProductCandidate> {
        ++BuiltinCalls;
        return createStringError(inconvertibleErrorCode(),
                                 "builtin must not run");
      });
  ASSERT_TRUE(static_cast<bool>(Result)) << errorText(Result.takeError());
  EXPECT_EQ(BuiltinCalls, 0U);
  EXPECT_EQ(State.LowerCalls, 1U);
  EXPECT_EQ(State.VerifyCalls, 1U);
  EXPECT_TRUE(Result->HostVerified);
  EXPECT_EQ(Result->Candidate.Kind,
            NEVERC_CODEGEN_PRODUCT_OBJECT_IMAGE);
}

TEST(PluginCodeGenRouteTest, RejectsMissingVerifierAndFinalIRProof) {
  CallbackState State;
  auto Edge = edge(1, "coarse-ir-to-object", NEVERC_CODEGEN_PRODUCT_IR,
                   NEVERC_CODEGEN_PRODUCT_OBJECT_IMAGE, ObjectProduct,
                   "coarse");
  Edge.Flags = NEVERC_CODEGEN_EDGE_COARSE;
  Edge.CoarseLower = coarseLower;
  Edge.CallbackUserData = &State;

  auto MissingVerifier = CodeGenRoutePlanner::plan(
      ArrayRef<PluginTargetSnapshot::CodeGenEdgeRecord>(Edge),
      request(NEVERC_CODEGEN_PRODUCT_OBJECT_IMAGE));
  ASSERT_FALSE(static_cast<bool>(MissingVerifier));
  EXPECT_NE(errorText(MissingVerifier.takeError()).find(
                "missing product verifier"),
            std::string::npos);

  Edge.VerifyProduct = verifyProduct;
  auto Plan = CodeGenRoutePlanner::plan(
      ArrayRef<PluginTargetSnapshot::CodeGenEdgeRecord>(Edge),
      request(NEVERC_CODEGEN_PRODUCT_OBJECT_IMAGE));
  ASSERT_TRUE(static_cast<bool>(Plan)) << errorText(Plan.takeError());
  CodeGenExecutionRequest Execution = executionRequest();
  Execution.HasFinalIRProof = false;
  auto MissingProof = PluginCodeGenProviderRuntime::execute(
      *Plan, Execution, {});
  ASSERT_FALSE(static_cast<bool>(MissingProof));
  EXPECT_NE(errorText(MissingProof.takeError()).find("final IR proof"),
            std::string::npos);
  EXPECT_EQ(State.LowerCalls, 0U);
}

TEST(PluginCodeGenRouteTest,
     RejectsWrongProductForeignCompatibilityAndForgedProof) {
  CallbackState State;
  auto Edge = edge(1, "coarse-ir-to-object", NEVERC_CODEGEN_PRODUCT_IR,
                   NEVERC_CODEGEN_PRODUCT_OBJECT_IMAGE, ObjectProduct,
                   "coarse");
  Edge.Flags = NEVERC_CODEGEN_EDGE_COARSE;
  Edge.CoarseLower = coarseLower;
  Edge.VerifyProduct = verifyProduct;
  Edge.CallbackUserData = &State;
  auto Plan = CodeGenRoutePlanner::plan(
      ArrayRef<PluginTargetSnapshot::CodeGenEdgeRecord>(Edge),
      request(NEVERC_CODEGEN_PRODUCT_OBJECT_IMAGE));
  ASSERT_TRUE(static_cast<bool>(Plan)) << errorText(Plan.takeError());

  State.ProductID = MCProduct;
  auto WrongProduct = PluginCodeGenProviderRuntime::execute(
      *Plan, executionRequest(), {});
  ASSERT_FALSE(static_cast<bool>(WrongProduct));
  EXPECT_NE(errorText(WrongProduct.takeError()).find("wrong product ID"),
            std::string::npos);

  State.ProductID = ObjectProduct;
  State.ForgedProof = {UINT64_C(0xdead), UINT64_C(0xbeef)};
  auto ForgedProof = PluginCodeGenProviderRuntime::execute(
      *Plan, executionRequest(), {});
  ASSERT_FALSE(static_cast<bool>(ForgedProof));
  EXPECT_NE(errorText(ForgedProof.takeError()).find("plugin-supplied proof"),
            std::string::npos);

  CodeGenExecutionRequest Foreign = executionRequest();
  Foreign.CompatibilityKey = "foreign-key";
  auto ForeignResult =
      PluginCodeGenProviderRuntime::execute(*Plan, Foreign, {});
  ASSERT_FALSE(static_cast<bool>(ForeignResult));
  EXPECT_NE(errorText(ForeignResult.takeError()).find("compatibility key"),
            std::string::npos);
}

} // namespace
