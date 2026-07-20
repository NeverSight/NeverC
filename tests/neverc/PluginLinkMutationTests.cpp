#include "PluginLinkTestSupport.h"
#include "Link/LinkMutation.h"
#include "Link/LinkProof.h"
#include "gtest/gtest.h"

using namespace neverc::plugin;
using namespace neverc::plugin::test_support;

extern "C" NevercStatus neverc_test_submit_invalid_link_mutation(
    const NevercLinkAPI *, NevercTaskHandle, NevercLinkGraphHandle,
    NevercLinkSymbolHandle);

namespace {

TEST(PluginLinkMutationTest, AbandonKeepsCommittedGraphAndGeneration) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Target = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  PluginLinkGraph Graph(std::move(*Target));
  GraphEntities Entities = populateValidGraph(Graph);
  LinkGraphPluginBridge Bridge(Scope.task(), Graph);
  auto GraphHandle = Bridge.graph();
  auto Symbol = Bridge.wrapEntity(
      LinkGraphPluginBridge::EntityKind::Symbol, Entities.SymbolID);
  ASSERT_TRUE(static_cast<bool>(GraphHandle))
      << errorText(GraphHandle.takeError());
  ASSERT_TRUE(static_cast<bool>(Symbol)) << errorText(Symbol.takeError());

  NevercLinkMutationHandle Mutation{};
  ASSERT_EQ(Bridge.api()
                .BeginMutation(Bridge.api().Context, Scope.task().handle(),
                               *GraphHandle, &Mutation)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(Bridge.api()
                .SetSymbolRoot(Bridge.api().Context, Scope.task().handle(),
                               Mutation, *Symbol, NEVERC_FALSE)
                .Code,
            NEVERC_STATUS_OK);

  EXPECT_FALSE(Bridge.activeGraph().findSymbol(Entities.SymbolID)->IsRoot);
  EXPECT_TRUE(
      Bridge.committedGraph().findSymbol(Entities.SymbolID)->IsRoot);
  EXPECT_EQ(Bridge.committedGraph().generation(), 1U);

  EXPECT_EQ(Bridge.api()
                .AbandonMutation(Bridge.api().Context,
                                 Scope.task().handle(), Mutation)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_TRUE(Graph.findSymbol(Entities.SymbolID)->IsRoot);
  EXPECT_EQ(Graph.generation(), 1U);
}

TEST(PluginLinkMutationTest,
     CommitBumpsGenerationInvalidatesProofAndTracksEarliestPhase) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Target = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  PluginLinkGraph Graph(std::move(*Target));
  GraphEntities Entities = populateValidGraph(Graph);
  Graph.setState(NEVERC_LINK_STATE_IMAGE_EMITTED);
  LinkGraphPluginBridge Bridge(Scope.task(), Graph);
  auto GraphHandle = Bridge.graph();
  auto Atom = Bridge.wrapEntity(LinkGraphPluginBridge::EntityKind::Atom,
                                Entities.AtomID);
  auto Proof = Bridge.issueProof(NEVERC_LINK_STATE_IMAGE_EMITTED);
  ASSERT_TRUE(static_cast<bool>(GraphHandle))
      << errorText(GraphHandle.takeError());
  ASSERT_TRUE(static_cast<bool>(Atom)) << errorText(Atom.takeError());
  ASSERT_TRUE(static_cast<bool>(Proof)) << errorText(Proof.takeError());

  NevercLinkProofInfo ProofInfo{};
  ProofInfo.Header = {sizeof(ProofInfo), NEVERC_LINK_API_MAJOR,
                      NEVERC_LINK_API_MINOR, 0};
  ASSERT_EQ(Bridge.api()
                .GetProofInfo(Bridge.api().Context, Scope.task().handle(),
                              *Proof, &ProofInfo)
                .Code,
            NEVERC_STATUS_OK);

  NevercLinkMutationHandle Mutation{};
  ASSERT_EQ(Bridge.api()
                .BeginMutation(Bridge.api().Context, Scope.task().handle(),
                               *GraphHandle, &Mutation)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(Bridge.api()
                .SetAtomLive(Bridge.api().Context, Scope.task().handle(),
                             Mutation, *Atom, NEVERC_TRUE)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(Bridge.api()
                .CommitMutation(Bridge.api().Context,
                                Scope.task().handle(), Mutation)
                .Code,
            NEVERC_STATUS_OK);

  EXPECT_EQ(Graph.generation(), 2U);
  EXPECT_EQ(Graph.state(), NEVERC_LINK_STATE_GC_COMPLETE);
  EXPECT_EQ(Bridge.lastInvalidatedState(),
            NEVERC_LINK_STATE_ICF_COMPLETE);
  EXPECT_NE(Graph.findAtom(Entities.AtomID)->Flags &
                NEVERC_LINK_ATOM_LIVE,
            0U);
  EXPECT_EQ(Bridge.api()
                .GetProofInfo(Bridge.api().Context, Scope.task().handle(),
                              *Proof, &ProofInfo)
                .Code,
            NEVERC_STATUS_STALE_HANDLE);
}

TEST(PluginLinkMutationTest, VerificationFailureRollsBackWholeMutation) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Target = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  PluginLinkGraph Graph(std::move(*Target));
  GraphEntities Entities = populateValidGraph(Graph);
  LinkGraphPluginBridge Bridge(Scope.task(), Graph);
  auto GraphHandle = Bridge.graph();
  auto Symbol = Bridge.wrapEntity(
      LinkGraphPluginBridge::EntityKind::Symbol, Entities.SymbolID);
  ASSERT_TRUE(static_cast<bool>(GraphHandle))
      << errorText(GraphHandle.takeError());
  ASSERT_TRUE(static_cast<bool>(Symbol)) << errorText(Symbol.takeError());

  EXPECT_EQ(neverc_test_submit_invalid_link_mutation(
                &Bridge.api(), Scope.task().handle(), *GraphHandle, *Symbol)
                .Code,
            NEVERC_STATUS_VERIFICATION_FAILED);

  EXPECT_EQ(Graph.generation(), 1U);
  EXPECT_EQ(Graph.findSymbol(Entities.SymbolID)->AtomID,
            Entities.AtomID);
}

TEST(PluginLinkMutationTest,
     SyntheticAndConstraintChangesCommitAsOneTransaction) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Target = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  PluginLinkGraph Graph(std::move(*Target));
  GraphEntities Entities = populateValidGraph(Graph);
  Graph.setState(NEVERC_LINK_STATE_IMAGE_EMITTED);
  LinkGraphPluginBridge Bridge(Scope.task(), Graph);
  auto GraphHandle = Bridge.graph();
  auto Section = Bridge.wrapEntity(
      LinkGraphPluginBridge::EntityKind::Section, Entities.SectionID);
  auto Atom = Bridge.wrapEntity(LinkGraphPluginBridge::EntityKind::Atom,
                                Entities.AtomID);
  ASSERT_TRUE(static_cast<bool>(GraphHandle))
      << errorText(GraphHandle.takeError());
  ASSERT_TRUE(static_cast<bool>(Section)) << errorText(Section.takeError());
  ASSERT_TRUE(static_cast<bool>(Atom)) << errorText(Atom.takeError());

  NevercLinkMutationHandle Mutation{};
  ASSERT_EQ(Bridge.api()
                .BeginMutation(Bridge.api().Context, Scope.task().handle(),
                               *GraphHandle, &Mutation)
                .Code,
            NEVERC_STATUS_OK);
  NevercLinkSyntheticInfo Synthetic{};
  Synthetic.Header = {sizeof(Synthetic), NEVERC_LINK_API_MAJOR,
                      NEVERC_LINK_API_MINOR, 0};
  Synthetic.Role = {"entry-table", 11};
  Synthetic.Section = *Section;
  Synthetic.Atom = *Atom;
  NevercLinkSyntheticHandle SyntheticHandle{};
  ASSERT_EQ(Bridge.api()
                .CreateSynthetic(Bridge.api().Context,
                                 Scope.task().handle(), Mutation,
                                 &Synthetic, &SyntheticHandle)
                .Code,
            NEVERC_STATUS_OK);

  NevercLinkConstraintInfo Constraint{};
  Constraint.Header = {sizeof(Constraint), NEVERC_LINK_API_MAJOR,
                       NEVERC_LINK_API_MINOR, 0};
  Constraint.Kind = {"minimum-address", 15};
  Constraint.SubjectID = Entities.AtomID;
  Constraint.Value = 0x1000;
  Constraint.Required = NEVERC_TRUE;
  NevercLinkConstraintHandle ConstraintHandle{};
  ASSERT_EQ(Bridge.api()
                .CreateConstraint(Bridge.api().Context,
                                  Scope.task().handle(), Mutation,
                                  &Constraint, &ConstraintHandle)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(Bridge.api()
                .CommitMutation(Bridge.api().Context,
                                Scope.task().handle(), Mutation)
                .Code,
            NEVERC_STATUS_OK);

  EXPECT_EQ(Graph.synthetics().size(), 1U);
  EXPECT_EQ(Graph.constraints().size(), 1U);
  EXPECT_EQ(Graph.generation(), 2U);
  EXPECT_EQ(Bridge.lastInvalidatedState(),
            NEVERC_LINK_STATE_SYNTHETICS_READY);
}

} // namespace
