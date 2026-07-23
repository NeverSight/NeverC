#include "PluginFrontendFuzzSupport.h"
#include "Link/LinkGraph.h"
#include "Link/LinkMutation.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <string>
#include <vector>

using namespace llvm;
using namespace neverc::fuzz;
using namespace neverc::plugin;

namespace {

void consume(Error E) {
  if (E)
    consumeError(std::move(E));
}

Expected<OwnedTargetKey> targetKey() {
  TargetKeyBuilder Builder;
  return Builder
      .setTargetID({UINT64_C(0x66757a7a6c696e6b), 1})
      .setTriple("x86_64-unknown-linux-gnu", "x86_64", "unknown", "linux",
                 "gnu")
      .setCPU("generic", "")
      .setFeatures({})
      .setABI({UINT64_C(0x66757a7a61626900), 1})
      .setCallingConvention({UINT64_C(0x66757a7a63630000), 1})
      .setObjectFormat({UINT64_C(0x66757a7a6f626a00), 1})
      .setCodeGeneration(NEVERC_TARGET_RELOCATION_STATIC,
                         NEVERC_TARGET_CODE_MODEL_SMALL)
      .setExecution(NEVERC_TARGET_EXECUTION_USER, 64,
                    NEVERC_TARGET_ENDIAN_LITTLE)
      .setSchemaDigest("0123456789abcdef0123456789abcdef"
                       "0123456789abcdef0123456789abcdef")
      .build();
}

uint64_t pick(const std::vector<uint64_t> &IDs, ByteCursor &Input) {
  if (IDs.empty())
    return Input.takeU64();
  return IDs[Input.takeByte() % IDs.size()];
}

// Populates a plausible-but-arbitrary link graph so the verifier and the
// copy-on-write mutation journal both see realistic entity cross-references.
void buildGraph(PluginLinkGraph &Graph, ByteCursor &Input,
                std::vector<uint64_t> &SectionIDs,
                std::vector<uint64_t> &AtomIDs,
                std::vector<uint64_t> &SymbolIDs) {
  const unsigned SectionCount = Input.takeByte() % 8;
  for (unsigned I = 0; I != SectionCount; ++I) {
    PluginLinkSection Section;
    Section.ID = Graph.allocateEntityID();
    Section.Name = "sec." + std::to_string(Input.takeU32());
    Section.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
    Section.Flags = Input.takeU64();
    Section.Alignment = 1U << (Input.takeByte() % 12);
    Section.Size = Input.takeU64();
    SectionIDs.push_back(Section.ID);
    Graph.addSection(std::move(Section));
  }

  const unsigned AtomCount = Input.takeByte() % 12;
  for (unsigned I = 0; I != AtomCount; ++I) {
    PluginLinkAtom Atom;
    Atom.ID = Graph.allocateEntityID();
    Atom.SectionID = pick(SectionIDs, Input);
    Atom.Name = "atom." + std::to_string(Input.takeU32());
    Atom.Flags = Input.takeU64();
    Atom.Alignment = 1U << (Input.takeByte() % 12);
    ArrayRef<uint8_t> Bytes = Input.takeBytes(64);
    Atom.Content.assign(Bytes.begin(), Bytes.end());
    Atom.ZeroFillSize = Input.takeU64();
    AtomIDs.push_back(Atom.ID);
    Graph.addAtom(std::move(Atom));
  }

  const unsigned SymbolCount = Input.takeByte() % 16;
  for (unsigned I = 0; I != SymbolCount; ++I) {
    PluginLinkSymbol Symbol;
    Symbol.ID = Graph.allocateEntityID();
    Symbol.Name = "sym." + std::to_string(Input.takeU32());
    Symbol.Binding =
        static_cast<NevercLinkSymbolBinding>(Input.takeU32());
    Symbol.Visibility =
        static_cast<NevercLinkSymbolVisibility>(Input.takeU32());
    Symbol.Definition =
        static_cast<NevercLinkSymbolDefinition>(Input.takeU32());
    Symbol.AtomID = pick(AtomIDs, Input);
    Symbol.Value = Input.takeU64();
    Symbol.Size = Input.takeU64();
    Symbol.IsRoot = (Input.takeByte() & 1U) != 0;
    SymbolIDs.push_back(Symbol.ID);
    Graph.addSymbol(std::move(Symbol));
  }

  const unsigned EdgeCount = Input.takeByte() % 16;
  for (unsigned I = 0; I != EdgeCount; ++I) {
    PluginLinkEdge Edge;
    Edge.ID = Graph.allocateEntityID();
    Edge.Kind = NEVERC_LINK_EDGE_RELOCATION;
    Edge.SourceAtomID = pick(AtomIDs, Input);
    Edge.Offset = Input.takeU64();
    Edge.RelocationKind =
        static_cast<NevercObjectRelocationKind>(Input.takeU32());
    Edge.Width = Input.takeU32();
    Edge.Addend = static_cast<int64_t>(Input.takeU64());
    Edge.TargetSymbolID = pick(SymbolIDs, Input);
    Edge.TargetAtomID = pick(AtomIDs, Input);
    Graph.addEdge(std::move(Edge));
  }
}

// Exercises the mutation journal so add/replace/erase/rebind paths and their
// invalidation bookkeeping run against fuzzed identifiers.
void driveMutation(PluginLinkGraph &Graph, ByteCursor &Input,
                   const std::vector<uint64_t> &SectionIDs,
                   const std::vector<uint64_t> &AtomIDs,
                   const std::vector<uint64_t> &SymbolIDs) {
  LinkMutation Mutation(Graph, "fuzz");
  const unsigned Operations = Input.takeByte() % 24;
  for (unsigned I = 0; I != Operations; ++I) {
    switch (Input.takeByte() % 12) {
    case 0: {
      PluginLinkSection Section;
      Section.ID = Mutation.graph().allocateEntityID();
      Section.Name = "msec." + std::to_string(Input.takeU32());
      Section.Alignment = 1U << (Input.takeByte() % 12);
      Mutation.addSection(std::move(Section));
      break;
    }
    case 1: {
      PluginLinkAtom Atom;
      Atom.ID = Mutation.graph().allocateEntityID();
      Atom.SectionID = pick(SectionIDs, Input);
      Atom.Alignment = 1U << (Input.takeByte() % 12);
      Mutation.addAtom(std::move(Atom));
      break;
    }
    case 2: {
      PluginLinkSymbol Symbol;
      Symbol.ID = Mutation.graph().allocateEntityID();
      Symbol.Name = "msym." + std::to_string(Input.takeU32());
      Mutation.addSymbol(std::move(Symbol));
      break;
    }
    case 3:
      consume(Mutation.eraseSection(pick(SectionIDs, Input)));
      break;
    case 4:
      consume(Mutation.eraseAtom(pick(AtomIDs, Input)));
      break;
    case 5:
      consume(Mutation.eraseSymbol(pick(SymbolIDs, Input)));
      break;
    case 6:
      consume(Mutation.rebindSymbol(pick(SymbolIDs, Input),
                                    pick(AtomIDs, Input)));
      break;
    case 7:
      consume(Mutation.setSymbolResolution(
          pick(SymbolIDs, Input),
          static_cast<NevercLinkSymbolBinding>(Input.takeU32()),
          static_cast<NevercLinkSymbolVisibility>(Input.takeU32()),
          static_cast<NevercLinkSymbolDefinition>(Input.takeU32()),
          (Input.takeByte() & 1U) != 0, (Input.takeByte() & 1U) != 0));
      break;
    case 8:
      consume(Mutation.setSymbolRoot(pick(SymbolIDs, Input),
                                     (Input.takeByte() & 1U) != 0));
      break;
    case 9:
      consume(Mutation.setAtomLive(pick(AtomIDs, Input),
                                   (Input.takeByte() & 1U) != 0));
      break;
    case 10:
      consume(Mutation.setFoldLeader(pick(AtomIDs, Input),
                                     pick(AtomIDs, Input)));
      break;
    default: {
      ArrayRef<uint8_t> Bytes = Input.takeBytes(64);
      consume(Mutation.replaceAtomContent(
          pick(AtomIDs, Input),
          std::vector<uint8_t>(Bytes.begin(), Bytes.end()),
          Input.takeU64()));
      break;
    }
    }
  }
  consume(verifyPluginLinkGraph(Mutation.graph()));
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  ByteCursor Input(Data, Size);
  auto Key = targetKey();
  if (!Key) {
    consume(Key.takeError());
    return 0;
  }

  const NevercLinkState State =
      static_cast<NevercLinkState>(Input.takeU32());
  PluginLinkGraph Graph(std::move(*Key), State);
  std::vector<uint64_t> SectionIDs;
  std::vector<uint64_t> AtomIDs;
  std::vector<uint64_t> SymbolIDs;
  buildGraph(Graph, Input, SectionIDs, AtomIDs, SymbolIDs);

  consume(verifyPluginLinkGraph(Graph));
  driveMutation(Graph, Input, SectionIDs, AtomIDs, SymbolIDs);
  return 0;
}
