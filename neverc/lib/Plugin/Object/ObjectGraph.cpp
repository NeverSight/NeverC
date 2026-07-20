#include "neverc/Plugin/Host/ObjectGraph.h"
#include "llvm/ADT/STLExtras.h"
#include <limits>
#include <sstream>
#include <utility>

namespace neverc::plugin {
namespace {

template <typename Storage>
auto *findByID(Storage &Values, uint64_t ID) {
  auto It = llvm::find_if(
      Values, [ID](const auto &Value) { return Value.ID == ID; });
  return It == Values.end() ? nullptr : &*It;
}

} // namespace

PluginObjectGraph::PluginObjectGraph(OwnedTargetKey TargetValue)
    : Target(std::move(TargetValue)) {}

PluginObjectSection *PluginObjectGraph::findSection(uint64_t ID) {
  return findByID(Sections, ID);
}

const PluginObjectSection *
PluginObjectGraph::findSection(uint64_t ID) const {
  return findByID(Sections, ID);
}

PluginObjectSymbol *PluginObjectGraph::findSymbol(uint64_t ID) {
  return findByID(Symbols, ID);
}

const PluginObjectSymbol *
PluginObjectGraph::findSymbol(uint64_t ID) const {
  return findByID(Symbols, ID);
}

PluginObjectRelocation *
PluginObjectGraph::findRelocation(uint64_t ID) {
  return findByID(Relocations, ID);
}

const PluginObjectRelocation *
PluginObjectGraph::findRelocation(uint64_t ID) const {
  return findByID(Relocations, ID);
}

PluginObjectComdat *PluginObjectGraph::findComdat(uint64_t ID) {
  return findByID(Comdats, ID);
}

const PluginObjectComdat *
PluginObjectGraph::findComdat(uint64_t ID) const {
  return findByID(Comdats, ID);
}

uint64_t PluginObjectGraph::allocateEntityID() {
  if (NextEntityID == 0)
    return 0;
  const uint64_t ID = NextEntityID++;
  if (NextEntityID == std::numeric_limits<uint64_t>::max())
    NextEntityID = 0;
  return ID;
}

void PluginObjectGraph::advanceGeneration() {
  clearLayoutProof();
  if (Generation == std::numeric_limits<uint64_t>::max())
    Generation = 1;
  else
    ++Generation;
}

void PluginObjectGraph::issueLayoutProof() {
  NevercTargetKey Key = targetKey();
  LayoutProof =
      PluginObjectLayoutProof{Generation, Key.TargetID, Key.ObjectFormatID};
}

std::string dumpPluginObjectGraph(const PluginObjectGraph &Graph) {
  NevercTargetKey Target = Graph.targetKey();
  std::ostringstream Stream;
  Stream << "target=" << std::hex << Target.TargetID.High << ":"
         << Target.TargetID.Low << " format=" << Target.ObjectFormatID.High
         << ":" << Target.ObjectFormatID.Low << std::dec
         << " generation=" << Graph.generation() << "\n";
  for (const PluginObjectComdat &Comdat : Graph.comdats())
    Stream << "comdat " << Comdat.Name << " selection="
           << Comdat.Selection << "\n";
  for (const PluginObjectSection &Section : Graph.sections())
    Stream << "section " << Section.Name << " kind=" << Section.Kind
           << " align=" << Section.Alignment
           << " bytes=" << Section.Data.size()
           << " zerofill=" << Section.ZeroFillSize << "\n";
  for (const PluginObjectSymbol &Symbol : Graph.symbols())
    Stream << "symbol " << Symbol.Name << " definition="
           << Symbol.Definition << " section=" << Symbol.SectionID
           << " value=" << Symbol.Value << " size=" << Symbol.Size
           << "\n";
  for (const PluginObjectRelocation &Relocation : Graph.relocations())
    Stream << "relocation section=" << Relocation.SectionID
           << " offset=" << Relocation.Offset
           << " width=" << Relocation.Width
           << " kind=" << Relocation.Kind << "\n";
  return Stream.str();
}

} // namespace neverc::plugin
