#include "PluginLinkTestSupport.h"
#include "Link/LinkProof.h"
#include "gtest/gtest.h"
#include <chrono>
#include <string>
#include <vector>

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

TEST(PluginLinkVerifierTest, VerifiesLargeGraphWithoutQuadraticLookup) {
  constexpr size_t EntityCount = 20000;
  auto Target = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  PluginLinkGraph Graph(std::move(*Target));

  PluginLinkInput Input;
  Input.Kind = NEVERC_LINK_INPUT_OBJECT;
  Input.LogicalURI = "vfs:///large.o";
  const uint64_t InputID = Graph.addInput(std::move(Input)).ID;

  PluginLinkSection Section;
  Section.Name = ".text";
  Section.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Section.Alignment = 1;
  Section.Size = EntityCount;
  Section.Origin.InputID = InputID;
  const uint64_t SectionID = Graph.addSection(std::move(Section)).ID;

  std::vector<uint64_t> AtomIDs;
  AtomIDs.reserve(EntityCount);
  for (size_t Index = 0; Index != EntityCount; ++Index) {
    PluginLinkAtom Atom;
    Atom.SectionID = SectionID;
    Atom.Name = "atom-" + std::to_string(Index);
    Atom.Alignment = 1;
    Atom.Content = {0};
    Atom.Origin.InputID = InputID;
    AtomIDs.push_back(Graph.addAtom(std::move(Atom)).ID);
  }

  std::vector<uint64_t> SymbolIDs;
  SymbolIDs.reserve(EntityCount);
  for (size_t Index = 0; Index != EntityCount; ++Index) {
    PluginLinkSymbol Symbol;
    Symbol.Name = "symbol-" + std::to_string(Index);
    Symbol.Binding = NEVERC_LINK_SYMBOL_BINDING_GLOBAL;
    Symbol.Definition = NEVERC_LINK_SYMBOL_DEFINED;
    Symbol.AtomID = AtomIDs[Index];
    Symbol.Origin.InputID = InputID;
    SymbolIDs.push_back(Graph.addSymbol(std::move(Symbol)).ID);
  }

  for (size_t Index = 0; Index != EntityCount; ++Index) {
    PluginLinkEdge Edge;
    Edge.SourceAtomID = AtomIDs[Index];
    Edge.TargetSymbolID = SymbolIDs[Index];
    Edge.Width = 8;
    Edge.Origin.InputID = InputID;
    Graph.addEdge(std::move(Edge));
  }

  const auto Start = std::chrono::steady_clock::now();
  Error Failure = verifyPluginLinkGraph(Graph);
  const auto Elapsed = std::chrono::steady_clock::now() - Start;
  if (Failure)
    ADD_FAILURE() << errorText(std::move(Failure));
  const auto ElapsedMilliseconds =
      std::chrono::duration_cast<std::chrono::milliseconds>(Elapsed).count();
  EXPECT_LT(ElapsedMilliseconds, 2000);
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
