#include "Linker/ELF/ELFLinkerContext.h"
#include "Linker/ELF/Config.h"
#include "Linker/ELF/LinkerScript.h"
#include "Linker/ELF/LTO.h"
#include "Linker/ELF/OutputSections.h"
#include "Linker/ELF/Relocations.h"
#include "Linker/ELF/SymbolTable.h"
#include "Linker/ELF/Symbols.h"
#include "Linker/ELF/SyntheticSections.h"
#include "Linker/ELF/Target.h"
#include "ELF/ELFLinkGraphAdapter.h"

namespace linker::elf {

struct ELFLinkerContext::Impl {
  ConfigWrapper Config;
  Ctx BackendState;
  SymbolTable Symbols;
  SmallVector<SymbolAux, 0> SymbolAuxRecords;
  SmallVector<OutputSection *, 0> OutputSectionList;
  std::unique_ptr<LinkerScript> Script;
  std::vector<Partition> Partitions;
  Partition *MainPartition = nullptr;
  InStruct SyntheticInputs;
  const TargetInfo *Target = nullptr;
  ElfSymbolState GeneratedSymbols;
  ElfOutputState Output;
  std::unique_ptr<ELFLinkGraphAdapter> PluginLinkAdapter;
  bool InputFileIsInGroup = false;
  uint32_t NextGroupId = 0;
  unsigned VernauxNum = 0;
  detail::ELFRelocationState Relocations;
};

ELFLinkerContext::ELFLinkerContext() : State(std::make_unique<Impl>()) {}
ELFLinkerContext::~ELFLinkerContext() { finalizeOwnedState(); }

ELFLinkerContext &elfContext() {
  return static_cast<ELFLinkerContext &>(commonContext());
}

ConfigWrapper &elfConfig() { return elfContext().state().Config; }
Ctx &elfState() { return elfContext().state().BackendState; }
SymbolTable &elfSymtab() { return elfContext().state().Symbols; }
SmallVector<SymbolAux, 0> &elfSymbolAux() {
  return elfContext().state().SymbolAuxRecords;
}
SmallVector<OutputSection *, 0> &elfOutputSections() {
  return elfContext().state().OutputSectionList;
}
std::unique_ptr<LinkerScript> &elfScript() {
  return elfContext().state().Script;
}
std::vector<Partition> &elfPartitions() {
  return elfContext().state().Partitions;
}
Partition *&elfMainPart() {
  return elfContext().state().MainPartition;
}
InStruct &elfIn() { return elfContext().state().SyntheticInputs; }
const TargetInfo *&elfTarget() { return elfContext().state().Target; }
ElfSymbolState &elfSym() {
  return elfContext().state().GeneratedSymbols;
}
ElfOutputState &elfOut() { return elfContext().state().Output; }
std::unique_ptr<ELFLinkGraphAdapter> &elfPluginLinkAdapter() {
  return elfContext().state().PluginLinkAdapter;
}
bool &elfInputFileIsInGroup() {
  return elfContext().state().InputFileIsInGroup;
}
uint32_t &elfNextGroupId() { return elfContext().state().NextGroupId; }
unsigned &elfVernauxNum() { return elfContext().state().VernauxNum; }

namespace detail {
ELFRelocationState &elfRelocationState() {
  return elfContext().state().Relocations;
}
} // namespace detail

} // namespace linker::elf
