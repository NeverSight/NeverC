#include "neverc/Plugin/Host/PluginPhaseGraph.h"
#include "gtest/gtest.h"
#include "llvm/Support/Error.h"
#include <string>

using namespace llvm;
using namespace neverc::plugin;

namespace {

std::string takeErrorMessage(Error ErrorValue) {
  auto Message = toString(std::move(ErrorValue));
  return Message.str().str();
}

PluginPhaseDefinition phase(uint64_t ID, const char *Name,
                            uint64_t Input = 1, uint64_t Output = 1) {
  PluginPhaseDefinition Result;
  Result.ID = {0xabcdef, ID};
  Result.CanonicalName = Name;
  Result.Domain = "test";
  Result.Verifier = "test.verify";
  Result.InputArtifact = {0x1111, Input};
  Result.OutputArtifact = {0x1111, Output};
  Result.Policy = NEVERC_PHASE_OBSERVABLE |
                  NEVERC_PHASE_INTERCEPTABLE |
                  NEVERC_PHASE_REPLACEABLE;
  Result.ObserverPoints = NEVERC_OBSERVER_BEFORE | NEVERC_OBSERVER_AFTER;
  Result.HasBuiltinFallback = true;
  return Result;
}

TEST(PluginPhaseGraphTest, BuildsStableBuiltinDriverOrder) {
  auto Graph = PluginPhaseGraph::createBuiltinDriverGraph();
  ASSERT_TRUE(static_cast<bool>(Graph))
      << takeErrorMessage(Graph.takeError());
  ASSERT_EQ(Graph->size(), 6U);
  ASSERT_EQ(Graph->order().size(), 6U);
  EXPECT_EQ(Graph->phaseAt(Graph->order()[0]).CanonicalName,
            "neverc.driver.raw_arguments");
  EXPECT_EQ(Graph->phaseAt(Graph->order()[5]).CanonicalName,
            "neverc.driver.execute_job");
  EXPECT_NE(Graph->find("neverc.driver.select_toolchain"), nullptr);
  EXPECT_EQ(Graph->phaseAt(Graph->order()[0]).Domain, "driver");
  EXPECT_EQ(Graph->phaseAt(Graph->order()[0]).Verifier,
            "neverc.driver.verify_raw_arguments");
}

TEST(PluginPhaseGraphTest, IncludesParserProviderAndFineGrainedExtensionSlots) {
  auto Graph = PluginPhaseGraph::createBuiltinSourceGraph();
  ASSERT_TRUE(static_cast<bool>(Graph)) << takeErrorMessage(Graph.takeError());
  EXPECT_EQ(Graph->size(), 22U);
  EXPECT_NE(Graph->find("neverc.syntax.parse"), nullptr);
  EXPECT_NE(Graph->find("neverc.syntax.extension.declaration"), nullptr);
  EXPECT_NE(Graph->find("neverc.syntax.extension.statement"), nullptr);
  EXPECT_NE(Graph->find("neverc.syntax.extension.expression"), nullptr);
  EXPECT_NE(Graph->find("neverc.syntax.extension.type_name"), nullptr);
  EXPECT_NE(Graph->find("neverc.syntax.extension.attribute"), nullptr);
  EXPECT_NE(Graph->find("neverc.syntax.extension.keyword"), nullptr);
  EXPECT_NE(Graph->find("neverc.sema.extension.conversion"), nullptr);
}

TEST(PluginPhaseGraphTest, RejectsDuplicatesAndInvalidSealedPolicy) {
  PluginPhaseGraph Graph;
  ASSERT_FALSE(Graph.addPhase(phase(1, "test.phase.one")));
  Error DuplicateID = Graph.addPhase(phase(1, "test.phase.two"));
  ASSERT_TRUE(static_cast<bool>(DuplicateID));
  EXPECT_NE(takeErrorMessage(std::move(DuplicateID)).find("duplicate"),
            std::string::npos);

  PluginPhaseDefinition Sealed = phase(2, "test.phase.sealed");
  Sealed.Policy |= NEVERC_PHASE_SEALED_HOST_GATE;
  Sealed.Gate = PluginPhaseGateKind::SealedVerifier;
  ASSERT_FALSE(Graph.addPhase(std::move(Sealed)));
  Error Invalid = Graph.finalize();
  ASSERT_TRUE(static_cast<bool>(Invalid));
  EXPECT_NE(takeErrorMessage(std::move(Invalid)).find("sealed"),
            std::string::npos);
}

TEST(PluginPhaseGraphTest, DetectsCyclesAndArtifactMismatch) {
  PluginPhaseGraph Cycle;
  auto A = phase(1, "test.phase.a");
  auto B = phase(2, "test.phase.b");
  auto C = phase(3, "test.phase.c");
  ASSERT_FALSE(Cycle.addPhase(A));
  ASSERT_FALSE(Cycle.addPhase(B));
  ASSERT_FALSE(Cycle.addPhase(C));
  ASSERT_FALSE(Cycle.addEdge(A.ID, B.ID));
  ASSERT_FALSE(Cycle.addEdge(B.ID, C.ID));
  ASSERT_FALSE(Cycle.addEdge(C.ID, A.ID));
  Error CycleError = Cycle.finalize();
  ASSERT_TRUE(static_cast<bool>(CycleError));
  std::string Message = takeErrorMessage(std::move(CycleError));
  EXPECT_NE(Message.find("cycle"), std::string::npos);
  EXPECT_NE(Message.find("test.phase.a"), std::string::npos);
  EXPECT_NE(Message.find("test.phase.b"), std::string::npos);
  EXPECT_NE(Message.find("test.phase.c"), std::string::npos);

  PluginPhaseGraph Mismatch;
  auto Producer = phase(4, "test.phase.producer", 1, 2);
  auto Consumer = phase(5, "test.phase.consumer", 3, 4);
  ASSERT_FALSE(Mismatch.addPhase(Producer));
  ASSERT_FALSE(Mismatch.addPhase(Consumer));
  ASSERT_FALSE(
      Mismatch.addEdge(Producer.ID, Consumer.ID, true));
  Error TypeError = Mismatch.finalize();
  ASSERT_TRUE(static_cast<bool>(TypeError));
  EXPECT_NE(takeErrorMessage(std::move(TypeError)).find("incompatible"),
            std::string::npos);
}

TEST(PluginPhaseGraphTest, KeepsInsertionOrderForUnconstrainedNodes) {
  PluginPhaseGraph Graph;
  auto First = phase(10, "test.phase.first");
  auto Second = phase(11, "test.phase.second");
  auto Third = phase(12, "test.phase.third");
  ASSERT_FALSE(Graph.addPhase(First));
  ASSERT_FALSE(Graph.addPhase(Second));
  ASSERT_FALSE(Graph.addPhase(Third));
  ASSERT_FALSE(Graph.addEdge(First.ID, Third.ID));
  ASSERT_FALSE(Graph.finalize());
  ASSERT_EQ(Graph.order().size(), 3U);
  EXPECT_EQ(Graph.phaseAt(Graph.order()[0]).CanonicalName,
            "test.phase.first");
  EXPECT_EQ(Graph.phaseAt(Graph.order()[1]).CanonicalName,
            "test.phase.second");
  EXPECT_EQ(Graph.phaseAt(Graph.order()[2]).CanonicalName,
            "test.phase.third");
}

} // namespace
