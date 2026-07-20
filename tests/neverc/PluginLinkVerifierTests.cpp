#include "PluginLinkTestSupport.h"
#include "Link/LinkProof.h"
#include "gtest/gtest.h"

using namespace llvm;
using namespace neverc::plugin;
using namespace neverc::plugin::test_support;

namespace {

TEST(PluginLinkVerifierTest, AcceptsWellFormedNormalizedGraph) {
  auto Target = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  PluginLinkGraph Graph(std::move(*Target));
  populateValidGraph(Graph);
  EXPECT_FALSE(verifyPluginLinkGraph(Graph));
}

TEST(PluginLinkVerifierTest, ReportsFirstEntityInStableVerificationOrder) {
  auto Target = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  PluginLinkGraph Graph(std::move(*Target));
  GraphEntities Entities = populateValidGraph(Graph);
  Graph.findSection(Entities.SectionID)->Alignment = 3;
  Graph.findSymbol(Entities.SymbolID)->AtomID = 0;

  Error Failure = verifyPluginLinkGraph(Graph);
  ASSERT_TRUE(static_cast<bool>(Failure));
  const std::string Message = errorText(std::move(Failure));
  EXPECT_NE(Message.find("entity=section#"), std::string::npos);
  EXPECT_NE(Message.find("origin="), std::string::npos);
  EXPECT_NE(Message.find("remedy="), std::string::npos);
  EXPECT_EQ(Message.find("entity=symbol#"), std::string::npos);
}

TEST(PluginLinkVerifierTest, RejectsStaleLayoutCoordinates) {
  auto Target = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  PluginLinkGraph Graph(std::move(*Target));
  GraphEntities Entities = populateValidGraph(Graph);
  Graph.setState(NEVERC_LINK_STATE_LAYOUT_COMPLETE);
  Graph.findSection(Entities.SectionID)->Address = 3;

  Error Failure = verifyPluginLinkGraph(Graph);
  ASSERT_TRUE(static_cast<bool>(Failure));
  EXPECT_NE(errorText(std::move(Failure))
                .find("rerun layout and clear stale layout proof"),
            std::string::npos);
}

TEST(PluginLinkVerifierTest, RejectsMultiplePrevailingDefinitions) {
  auto Target = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  PluginLinkGraph Graph(std::move(*Target));
  GraphEntities Entities = populateValidGraph(Graph);
  PluginLinkSymbol Duplicate =
      *Graph.findSymbol(Entities.SymbolID);
  Graph.addSymbol(std::move(Duplicate));

  Error Failure = verifyPluginLinkGraph(Graph);
  ASSERT_TRUE(static_cast<bool>(Failure));
  EXPECT_NE(errorText(std::move(Failure))
                .find("multiple prevailing symbols"),
            std::string::npos);
}

TEST(PluginLinkVerifierTest, ProofCannotCrossGraphEvenWithinOneTask) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto FirstTarget = makeTargetKey();
  auto SecondTarget = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(FirstTarget))
      << errorText(FirstTarget.takeError());
  ASSERT_TRUE(static_cast<bool>(SecondTarget))
      << errorText(SecondTarget.takeError());
  PluginLinkGraph First(std::move(*FirstTarget));
  PluginLinkGraph Second(std::move(*SecondTarget));
  populateValidGraph(First);
  populateValidGraph(Second);
  First.setState(NEVERC_LINK_STATE_INPUTS_READ);
  Second.setState(NEVERC_LINK_STATE_INPUTS_READ);
  LinkGraphPluginBridge FirstBridge(Scope.task(), First);
  LinkGraphPluginBridge SecondBridge(Scope.task(), Second);
  auto Proof = FirstBridge.issueProof(NEVERC_LINK_STATE_INPUTS_READ);
  ASSERT_TRUE(static_cast<bool>(Proof)) << errorText(Proof.takeError());

  const PluginLinkProof *Resolved = nullptr;
  EXPECT_EQ(SecondBridge.resolveProof(*Proof, &Resolved).Code,
            NEVERC_STATUS_WRONG_SCOPE);
  EXPECT_EQ(Resolved, nullptr);
}

} // namespace
