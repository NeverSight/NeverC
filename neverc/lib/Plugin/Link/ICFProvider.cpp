#include "ICFProvider.h"
#include "LivenessVerifier.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <map>
#include <tuple>

using namespace llvm;

namespace neverc::plugin {
namespace {

Error icfError(const Twine &Message) {
  return createStringError(errc::invalid_argument,
                           "link ICF: " + Message);
}

bool isLive(const PluginLinkAtom &Atom) {
  return (Atom.Flags & NEVERC_LINK_ATOM_LIVE) != 0;
}

bool hasSafeIdentity(const PluginLinkGraph &Graph,
                     uint64_t AtomID) {
  for (const PluginLinkSymbol &Symbol : Graph.symbols())
    if (Symbol.AtomID == AtomID &&
        (Symbol.IsRoot || Symbol.IsExported))
      return true;
  return false;
}

std::string atomSignature(const PluginLinkGraph &Graph,
                          const PluginLinkAtom &Atom) {
  std::string Result;
  raw_string_ostream OS(Result);
  OS << Atom.Alignment << ":" << Atom.ZeroFillSize << ":"
     << Atom.Content.size() << ":";
  for (uint8_t Byte : Atom.Content)
    OS << format_hex_no_prefix(Byte, 2);

  using EdgeKey =
      std::tuple<NevercLinkEdgeKind, uint64_t,
                 NevercObjectRelocationKind, uint32_t, int64_t, bool,
                 bool, std::string>;
  std::vector<EdgeKey> Edges;
  for (const PluginLinkEdge &Edge : Graph.edges()) {
    if (Edge.SourceAtomID != Atom.ID)
      continue;
    std::string Target;
    if (const PluginLinkSymbol *Symbol =
            Graph.findSymbol(Edge.TargetSymbolID))
      Target = "S:" + Symbol->Name + ":" + Symbol->Version;
    else if (const PluginLinkAtom *TargetAtom =
                 Graph.findAtom(Edge.TargetAtomID))
      Target = "A:" + TargetAtom->Name;
    Edges.emplace_back(
        Edge.Kind, Edge.Offset, Edge.RelocationKind, Edge.Width,
        Edge.Addend, Edge.IsPCRelative, Edge.IsSigned,
        std::move(Target));
  }
  llvm::sort(Edges);
  for (const EdgeKey &Edge : Edges) {
    OS << "|";
    std::apply([&](const auto &...Values) { ((OS << Values << ":"), ...); },
               Edge);
  }
  OS.flush();
  return Result;
}

} // namespace

Expected<std::vector<LinkFoldRecord>>
foldIdenticalLinkAtoms(PluginLinkGraph &Graph,
                       const LinkICFOptions &Options) {
  if (Graph.state() < NEVERC_LINK_STATE_GC_COMPLETE)
    return icfError("GC is not complete");
  if (Graph.state() > NEVERC_LINK_STATE_ICF_COMPLETE)
    return icfError("later phases must be invalidated before ICF");

  std::map<std::string, std::vector<PluginLinkAtom *>> Classes;
  std::map<uint64_t, LinkFoldRecord> Records;
  for (PluginLinkAtom &Atom : Graph.atoms()) {
    Atom.Flags &= ~NEVERC_LINK_ATOM_FOLDED;
    Atom.FoldLeaderID = 0;
    LinkFoldRecord &Record = Records[Atom.ID];
    Record.AtomID = Atom.ID;
    if (Options.Mode == LinkICFMode::None) {
      Record.Reason = "disabled";
      continue;
    }
    if (!isLive(Atom)) {
      Record.Reason = "dead";
      continue;
    }
    if ((Atom.Flags & NEVERC_LINK_ATOM_ADDRESS_SIGNIFICANT) != 0) {
      Record.Reason = "address-significant";
      continue;
    }
    if ((Atom.Flags & NEVERC_LINK_ATOM_TLS) != 0) {
      Record.Reason = "tls";
      continue;
    }
    if ((Atom.Flags & NEVERC_LINK_ATOM_UNWIND) != 0) {
      Record.Reason = "unwind";
      continue;
    }
    if (Options.Mode == LinkICFMode::Safe &&
        ((Atom.Flags & NEVERC_LINK_ATOM_ROOT) != 0 ||
         hasSafeIdentity(Graph, Atom.ID))) {
      Record.Reason = "safe-identity";
      continue;
    }
    Record.Eligible = true;
    Record.Reason = "unique";
    Classes[atomSignature(Graph, Atom)].push_back(&Atom);
  }

  for (auto &[Signature, Atoms] : Classes) {
    (void)Signature;
    llvm::sort(Atoms, [](const PluginLinkAtom *Left,
                         const PluginLinkAtom *Right) {
      return Left->ID < Right->ID;
    });
    PluginLinkAtom *Leader = Atoms.front();
    Records[Leader->ID].LeaderID = Leader->ID;
    Records[Leader->ID].Reason =
        Atoms.size() == 1 ? "unique" : "class-leader";
    for (PluginLinkAtom *Atom : llvm::drop_begin(Atoms)) {
      Atom->FoldLeaderID = Leader->ID;
      Atom->Flags |= NEVERC_LINK_ATOM_FOLDED;
      Records[Atom->ID].LeaderID = Leader->ID;
      Records[Atom->ID].Reason = "equivalent";
    }
  }

  std::vector<LinkFoldRecord> Result;
  Result.reserve(Records.size());
  for (auto &[AtomID, Record] : Records) {
    (void)AtomID;
    Result.push_back(std::move(Record));
  }
  Graph.advanceGeneration();
  Graph.setState(NEVERC_LINK_STATE_ICF_COMPLETE);
  if (Error E = verifyLinkFolding(Graph))
    return std::move(E);
  return Result;
}

} // namespace neverc::plugin
