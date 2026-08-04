#include "LinkGraph.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/raw_ostream.h"
#include <string>
#include <unordered_map>
#include <unordered_set>

using namespace llvm;

namespace neverc::plugin {
namespace {

Error verificationError(StringRef Kind, uint64_t ID,
                        const PluginLinkOriginData *Origin,
                        StringRef Problem, StringRef Remedy) {
  std::string Message;
  raw_string_ostream OS(Message);
  OS << "LinkGraph verification failed: entity=" << Kind << "#" << ID
     << "; problem=" << Problem << "; origin=";
  if (Origin)
    OS << canonicalizeLinkOrigin(*Origin);
  else
    OS << "host";
  OS << "; remedy=" << Remedy;
  OS.flush();
  return createStringError(errc::invalid_argument, Message);
}

bool isPowerOfTwo(uint64_t Value) {
  return Value != 0 && (Value & (Value - 1)) == 0;
}

template <typename Storage>
Error checkIDs(const Storage &Values, StringRef Kind,
               std::unordered_set<uint64_t> &AllIDs) {
  for (const auto &Value : Values) {
    if (Value.ID == 0)
      return verificationError(Kind, Value.ID, nullptr,
                               "entity ID is zero",
                               "allocate IDs through PluginLinkGraph");
    if (!AllIDs.insert(Value.ID).second)
      return verificationError(Kind, Value.ID, nullptr,
                               "entity ID is duplicated",
                               "allocate one graph-global ID per entity");
  }
  return Error::success();
}

template <typename Storage>
void collectIDs(const Storage &Values,
                std::unordered_set<uint64_t> &IDs) {
  IDs.reserve(Values.size());
  for (const auto &Value : Values)
    IDs.insert(Value.ID);
}

struct GraphIndex {
  std::unordered_set<uint64_t> Inputs;
  std::unordered_set<uint64_t> Archives;
  std::unordered_set<uint64_t> ArchiveMembers;
  std::unordered_set<uint64_t> SharedLibraries;
  std::unordered_set<uint64_t> BitcodeModules;
  std::unordered_set<uint64_t> Comdats;
  std::unordered_set<uint64_t> Sections;
  std::unordered_map<uint64_t, const PluginLinkAtom *> Atoms;
  std::unordered_set<uint64_t> Symbols;

  explicit GraphIndex(const PluginLinkGraph &Graph) {
    collectIDs(Graph.inputs(), Inputs);
    collectIDs(Graph.archives(), Archives);
    collectIDs(Graph.archiveMembers(), ArchiveMembers);
    collectIDs(Graph.sharedLibraries(), SharedLibraries);
    collectIDs(Graph.bitcodeModules(), BitcodeModules);
    collectIDs(Graph.comdats(), Comdats);
    collectIDs(Graph.sections(), Sections);
    Atoms.reserve(Graph.atoms().size());
    for (const PluginLinkAtom &Atom : Graph.atoms())
      Atoms.emplace(Atom.ID, &Atom);
    collectIDs(Graph.symbols(), Symbols);
  }

  const PluginLinkAtom *findAtom(uint64_t ID) const {
    auto It = Atoms.find(ID);
    return It == Atoms.end() ? nullptr : It->second;
  }
};

Error checkOrigin(const GraphIndex &Index,
                  const PluginLinkOriginData &Origin, StringRef Kind,
                  uint64_t ID) {
  if (Origin.InputID != 0 && !Index.Inputs.count(Origin.InputID))
    return verificationError(
        Kind, ID, &Origin, "origin refers to a missing input",
        "retain the originating input or clear the invalid reference");
  if (Origin.ArchiveMemberID != 0 &&
      !Index.ArchiveMembers.count(Origin.ArchiveMemberID))
    return verificationError(
        Kind, ID, &Origin, "origin refers to a missing archive member",
        "retain the archive member or clear the invalid reference");
  return Error::success();
}

template <typename Storage>
Error checkOrigins(const GraphIndex &Index, const Storage &Values,
                   StringRef Kind) {
  for (const auto &Value : Values)
    if (Error E = checkOrigin(Index, Value.Origin, Kind, Value.ID))
      return E;
  return Error::success();
}

bool exceedsPointerWidth(uint64_t Value, uint32_t PointerWidth) {
  if (PointerWidth == 0 || PointerWidth >= 64)
    return false;
  return Value > ((UINT64_C(1) << PointerWidth) - 1);
}

} // namespace

Error verifyPluginLinkGraph(const PluginLinkGraph &Graph) {
  std::unordered_set<uint64_t> IDs;
  if (Error E = checkIDs(Graph.inputs(), "input", IDs))
    return E;
  if (Error E = checkIDs(Graph.archives(), "archive", IDs))
    return E;
  if (Error E = checkIDs(Graph.archiveMembers(), "archive-member", IDs))
    return E;
  if (Error E =
          checkIDs(Graph.sharedLibraries(), "shared-library", IDs))
    return E;
  if (Error E =
          checkIDs(Graph.bitcodeModules(), "bitcode-module", IDs))
    return E;
  if (Error E = checkIDs(Graph.comdats(), "comdat", IDs))
    return E;
  if (Error E = checkIDs(Graph.sections(), "section", IDs))
    return E;
  if (Error E = checkIDs(Graph.atoms(), "atom", IDs))
    return E;
  if (Error E = checkIDs(Graph.symbols(), "symbol", IDs))
    return E;
  if (Error E = checkIDs(Graph.edges(), "edge", IDs))
    return E;
  if (Error E = checkIDs(Graph.imports(), "import", IDs))
    return E;
  if (Error E = checkIDs(Graph.exports(), "export", IDs))
    return E;
  if (Error E = checkIDs(Graph.unwindRecords(), "unwind", IDs))
    return E;
  if (Error E = checkIDs(Graph.synthetics(), "synthetic", IDs))
    return E;
  if (Error E = checkIDs(Graph.constraints(), "constraint", IDs))
    return E;

  const GraphIndex Index(Graph);

  for (const PluginLinkInput &Input : Graph.inputs()) {
    if (Input.LogicalURI.empty() && neverc_handle_is_null(Input.ObjectGraph))
      return verificationError(
          "input", Input.ID, nullptr,
          "input has neither a logical URI nor an ObjectGraph",
          "supply an authorized URI or a task-scoped ObjectGraph");
    if (Input.ArchiveID != 0 && !Index.Archives.count(Input.ArchiveID))
      return verificationError("input", Input.ID, nullptr,
                               "input refers to a missing archive",
                               "retain or clear the archive association");
    if (Input.SharedLibraryID != 0 &&
        !Index.SharedLibraries.count(Input.SharedLibraryID))
      return verificationError(
          "input", Input.ID, nullptr,
          "input refers to a missing shared library",
          "retain or clear the shared-library association");
    if (Input.BitcodeModuleID != 0 &&
        !Index.BitcodeModules.count(Input.BitcodeModuleID))
      return verificationError(
          "input", Input.ID, nullptr,
          "input refers to a missing bitcode module",
          "retain or clear the bitcode-module association");
  }

  for (const PluginLinkArchive &Archive : Graph.archives()) {
    if (!Index.Inputs.count(Archive.InputID))
      return verificationError("archive", Archive.ID, &Archive.Origin,
                               "archive has no owning input",
                               "bind the archive to a live input");
    if (Archive.Name.empty())
      return verificationError("archive", Archive.ID, &Archive.Origin,
                               "archive name is empty",
                               "provide a deterministic archive name");
  }
  for (const PluginLinkArchiveMember &Member : Graph.archiveMembers()) {
    if (!Index.Inputs.count(Member.InputID))
      return verificationError(
          "archive-member", Member.ID, &Member.Origin,
          "archive member has no owning input",
          "bind the member to the input that supplies its bytes");
    if (Member.ArchiveID != 0 && !Index.Archives.count(Member.ArchiveID))
      return verificationError(
          "archive-member", Member.ID, &Member.Origin,
          "archive member refers to a missing archive",
          "retain the archive until all members are removed");
    if (Member.Name.empty())
      return verificationError("archive-member", Member.ID,
                               &Member.Origin,
                               "archive member name is empty",
                               "provide a deterministic member name");
  }
  for (const PluginLinkSharedLibrary &Library :
       Graph.sharedLibraries()) {
    if (!Index.Inputs.count(Library.InputID))
      return verificationError(
          "shared-library", Library.ID, &Library.Origin,
          "shared library has no owning input",
          "bind the shared library to a live input");
  }
  for (const PluginLinkBitcodeModule &Module : Graph.bitcodeModules()) {
    if (!Index.Inputs.count(Module.InputID))
      return verificationError(
          "bitcode-module", Module.ID, &Module.Origin,
          "bitcode module has no owning input",
          "bind the module to a live bitcode input");
    for (const PluginLinkBitcodeSymbol &Symbol : Module.Symbols) {
      if (Symbol.Name.empty())
        return verificationError(
            "bitcode-module", Module.ID, &Module.Origin,
            "bitcode summary contains an empty symbol name",
            "rebuild the bitcode symbol summary");
      if (!Symbol.Common &&
          (Symbol.CommonSize != 0 || Symbol.CommonAlignment != 0))
        return verificationError(
            "bitcode-module", Module.ID, &Module.Origin,
            "non-common bitcode symbol carries common allocation data",
            "clear common size and alignment metadata");
    }
  }

  for (const PluginLinkComdat &Comdat : Graph.comdats()) {
    if (Comdat.Name.empty())
      return verificationError("comdat", Comdat.ID, &Comdat.Origin,
                               "COMDAT name is empty",
                               "provide the source COMDAT signature");
    if (Comdat.SelectedID != 0 &&
        !Index.Comdats.count(Comdat.SelectedID))
      return verificationError("comdat", Comdat.ID, &Comdat.Origin,
                               "selected COMDAT is missing",
                               "clear or replace the selected COMDAT");
  }

  const uint32_t PointerWidth = Graph.targetKey().PointerWidth;
  for (const PluginLinkSection &Section : Graph.sections()) {
    if (Section.Name.empty())
      return verificationError("section", Section.ID, &Section.Origin,
                               "section name is empty",
                               "provide a target-format section name");
    if (!isPowerOfTwo(Section.Alignment))
      return verificationError("section", Section.ID, &Section.Origin,
                               "section alignment is not a power of two",
                               "use a non-zero power-of-two alignment");
    if (Section.ComdatID != 0 &&
        !Index.Comdats.count(Section.ComdatID))
      return verificationError("section", Section.ID, &Section.Origin,
                               "section COMDAT is missing",
                               "retain or clear the COMDAT reference");
    if (Graph.state() >= NEVERC_LINK_STATE_LAYOUT_COMPLETE &&
        ((Section.Address & (Section.Alignment - 1)) != 0 ||
         exceedsPointerWidth(Section.Address, PointerWidth)))
      return verificationError(
          "section", Section.ID, &Section.Origin,
          "laid-out section address violates target bounds or alignment",
          "rerun layout and clear stale layout proof");
  }

  for (const PluginLinkAtom &Atom : Graph.atoms()) {
    if (!Index.Sections.count(Atom.SectionID))
      return verificationError("atom", Atom.ID, &Atom.Origin,
                               "atom section is missing",
                               "bind the atom to a live section");
    if (!isPowerOfTwo(Atom.Alignment))
      return verificationError("atom", Atom.ID, &Atom.Origin,
                               "atom alignment is not a power of two",
                               "use a non-zero power-of-two alignment");
    if (!Atom.Content.empty() && Atom.ZeroFillSize != 0)
      return verificationError(
          "atom", Atom.ID, &Atom.Origin,
          "atom mixes initialized bytes with zero-fill size",
          "represent initialized and zero-fill ranges as separate atoms");
    if (Atom.ComdatID != 0 && !Index.Comdats.count(Atom.ComdatID))
      return verificationError("atom", Atom.ID, &Atom.Origin,
                               "atom COMDAT is missing",
                               "retain or clear the COMDAT reference");
    if (Atom.FoldLeaderID != 0 &&
        (!Index.findAtom(Atom.FoldLeaderID) ||
         Atom.FoldLeaderID == Atom.ID))
      return verificationError("atom", Atom.ID, &Atom.Origin,
                               "fold leader is missing or self-referential",
                               "select another live atom or clear folding");
    if (Graph.state() >= NEVERC_LINK_STATE_LAYOUT_COMPLETE &&
        ((Atom.Address & (Atom.Alignment - 1)) != 0 ||
         exceedsPointerWidth(Atom.Address, PointerWidth)))
      return verificationError(
          "atom", Atom.ID, &Atom.Origin,
          "laid-out atom address violates target bounds or alignment",
          "rerun layout and clear stale layout proof");
  }

  std::unordered_map<std::string, uint64_t> Prevailing;
  for (const PluginLinkSymbol &Symbol : Graph.symbols()) {
    if (Symbol.Name.empty())
      return verificationError("symbol", Symbol.ID, &Symbol.Origin,
                               "symbol name is empty",
                               "provide the source or synthetic symbol name");
    if (Symbol.Definition == NEVERC_LINK_SYMBOL_DEFINED &&
        !Index.findAtom(Symbol.AtomID))
      return verificationError("symbol", Symbol.ID, &Symbol.Origin,
                               "defined symbol has no live atom",
                               "rebind it to a live atom or make it undefined");
    if (Symbol.Definition == NEVERC_LINK_SYMBOL_UNDEFINED &&
        Symbol.AtomID != 0)
      return verificationError(
          "symbol", Symbol.ID, &Symbol.Origin,
          "undefined symbol is bound to an atom",
          "clear the binding or mark the symbol as defined");
    if (Symbol.IsPrevailing) {
      std::string Key = Symbol.Name + "@" + Symbol.Version;
      if (Symbol.Binding == NEVERC_LINK_SYMBOL_BINDING_LOCAL)
        Key += "#local:" + std::to_string(Symbol.Origin.InputID) + ":" +
               std::to_string(Symbol.ID);
      auto [It, Inserted] = Prevailing.emplace(Key, Symbol.ID);
      if (!Inserted)
        return verificationError(
            "symbol", Symbol.ID, &Symbol.Origin,
            "definition set has multiple prevailing symbols",
            "choose exactly one prevailing definition");
    }
  }

  for (const PluginLinkEdge &Edge : Graph.edges()) {
    const PluginLinkAtom *Source = Index.findAtom(Edge.SourceAtomID);
    if (!Source)
      return verificationError("edge", Edge.ID, &Edge.Origin,
                               "edge source atom is missing",
                               "bind the edge to a live source atom");
    if ((Edge.TargetSymbolID == 0) == (Edge.TargetAtomID == 0))
      return verificationError(
          "edge", Edge.ID, &Edge.Origin,
          "edge must have exactly one symbol or atom target",
          "set one target and clear the other");
    if (Edge.TargetSymbolID != 0 &&
        !Index.Symbols.count(Edge.TargetSymbolID))
      return verificationError("edge", Edge.ID, &Edge.Origin,
                               "edge target symbol is missing",
                               "retarget the edge to a live symbol");
    if (Edge.TargetAtomID != 0 &&
        !Index.findAtom(Edge.TargetAtomID))
      return verificationError("edge", Edge.ID, &Edge.Origin,
                               "edge target atom is missing",
                               "retarget the edge to a live atom");
    const uint64_t SourceSize =
        Source->Content.size() + Source->ZeroFillSize;
    const uint64_t WidthBytes = (Edge.Width + 7) / 8;
    if (Edge.Width == 0 || Edge.Offset > SourceSize ||
        WidthBytes > SourceSize - Edge.Offset)
      return verificationError(
          "edge", Edge.ID, &Edge.Origin,
          "edge fixup is outside the source atom",
          "use a valid width and in-bounds offset");
  }

  for (const PluginLinkImport &Import : Graph.imports()) {
    if (Import.Name.empty() ||
        (Import.SymbolID != 0 && !Index.Symbols.count(Import.SymbolID)))
      return verificationError(
          "import", Import.ID, &Import.Origin,
          "import name or symbol reference is invalid",
          "bind a named import to a live symbol");
  }
  for (const PluginLinkExport &Export : Graph.exports()) {
    if (Export.Name.empty() || !Index.Symbols.count(Export.SymbolID))
      return verificationError(
          "export", Export.ID, &Export.Origin,
          "export name or symbol reference is invalid",
          "bind a named export to a live defined symbol");
  }
  for (const PluginLinkUnwindRecord &Unwind :
       Graph.unwindRecords()) {
    if (!Index.findAtom(Unwind.AtomID) ||
        (Unwind.PersonalitySymbolID != 0 &&
         !Index.Symbols.count(Unwind.PersonalitySymbolID)))
      return verificationError(
          "unwind", Unwind.ID, &Unwind.Origin,
          "unwind atom or personality symbol is missing",
          "retain the referenced entities or remove the unwind record");
  }
  for (const PluginLinkSynthetic &Synthetic : Graph.synthetics()) {
    if (Synthetic.Role.empty() ||
        (Synthetic.SectionID == 0 && Synthetic.AtomID == 0) ||
        (Synthetic.SectionID != 0 &&
         !Index.Sections.count(Synthetic.SectionID)) ||
        (Synthetic.AtomID != 0 && !Index.findAtom(Synthetic.AtomID)))
      return verificationError(
          "synthetic", Synthetic.ID, &Synthetic.Origin,
          "synthetic role or entity reference is invalid",
          "name the role and retain its section or atom");
  }
  for (const PluginLinkConstraint &Constraint : Graph.constraints()) {
    const bool GlobalConstraint =
        Constraint.Kind == "image-base" ||
        Constraint.Kind == "file-base" ||
        Constraint.Kind == "page-size" ||
        Constraint.Kind == "allow-wx";
    if (Constraint.Kind.empty() ||
        (Constraint.Required && Constraint.SubjectID == 0 &&
         !GlobalConstraint))
      return verificationError(
          "constraint", Constraint.ID, &Constraint.Origin,
          "constraint kind or required subject is missing",
          "provide a stable kind and subject for required constraints");
  }

  if (Error E = checkOrigins(Index, Graph.archives(), "archive"))
    return E;
  if (Error E =
          checkOrigins(Index, Graph.archiveMembers(), "archive-member"))
    return E;
  if (Error E = checkOrigins(Index, Graph.sharedLibraries(),
                             "shared-library"))
    return E;
  if (Error E =
          checkOrigins(Index, Graph.bitcodeModules(), "bitcode-module"))
    return E;
  if (Error E = checkOrigins(Index, Graph.comdats(), "comdat"))
    return E;
  if (Error E = checkOrigins(Index, Graph.sections(), "section"))
    return E;
  if (Error E = checkOrigins(Index, Graph.atoms(), "atom"))
    return E;
  if (Error E = checkOrigins(Index, Graph.symbols(), "symbol"))
    return E;
  if (Error E = checkOrigins(Index, Graph.edges(), "edge"))
    return E;
  if (Error E = checkOrigins(Index, Graph.imports(), "import"))
    return E;
  if (Error E = checkOrigins(Index, Graph.exports(), "export"))
    return E;
  if (Error E = checkOrigins(Index, Graph.unwindRecords(), "unwind"))
    return E;
  if (Error E = checkOrigins(Index, Graph.synthetics(), "synthetic"))
    return E;
  return checkOrigins(Index, Graph.constraints(), "constraint");
}

} // namespace neverc::plugin
