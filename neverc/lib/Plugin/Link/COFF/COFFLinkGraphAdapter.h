#ifndef NEVERC_PLUGIN_LINK_COFF_COFFLINKGRAPHADAPTER_H
#define NEVERC_PLUGIN_LINK_COFF_COFFLINKGRAPHADAPTER_H

#include "Link/LinkGraph.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <utility>

namespace neverc::plugin {
class PluginTaskContext;
}

namespace linker::coff {

class Chunk;
class COFFLinkerContext;
class InputFile;
class OutputSection;
class SectionChunk;
class Symbol;

/// Task-local projection between the native COFF linker and the public
/// NeverC LinkGraph. Native pointers stay in this host-side identity map.
class COFFLinkGraphAdapter {
public:
  static llvm::Expected<std::unique_ptr<COFFLinkGraphAdapter>>
  create(neverc::plugin::PluginTaskContext &Task, COFFLinkerContext &Context,
         llvm::StringRef TargetTriple, llvm::StringRef CPU,
         NevercTargetRelocationModel RelocationModel);

  ~COFFLinkGraphAdapter();
  COFFLinkGraphAdapter(const COFFLinkGraphAdapter &) = delete;
  COFFLinkGraphAdapter &operator=(const COFFLinkGraphAdapter &) = delete;

  llvm::Error advanceTo(NevercLinkState State);
  llvm::Error publishImage(llvm::ArrayRef<uint8_t> Bytes);

  const std::shared_ptr<neverc::plugin::PluginLinkGraph> &graph() const {
    return Graph;
  }

private:
  struct NativeRelocationRef {
    SectionChunk *Section = nullptr;
    size_t Index = 0;
  };

  COFFLinkGraphAdapter(neverc::plugin::PluginTaskContext &Task,
                       COFFLinkerContext &Context,
                       std::shared_ptr<neverc::plugin::PluginLinkGraph> Graph);

  llvm::Expected<std::shared_ptr<neverc::plugin::PluginLinkGraph>>
  capture(const neverc::plugin::PluginLinkGraph &Previous,
          NevercLinkState State);
  llvm::Error applyDelta(const neverc::plugin::PluginLinkGraph &Before,
                         const neverc::plugin::PluginLinkGraph &After,
                         NevercLinkState State);

  neverc::plugin::PluginTaskContext &Task;
  COFFLinkerContext &Context;
  std::shared_ptr<neverc::plugin::PluginLinkGraph> Graph;

  llvm::DenseMap<const InputFile *, uint64_t> InputIDs;
  llvm::DenseMap<const Chunk *, uint64_t> SectionIDs;
  llvm::DenseMap<const Chunk *, uint64_t> AtomIDs;
  llvm::DenseMap<const OutputSection *, uint64_t> OutputSectionIDs;
  llvm::DenseMap<const Symbol *, uint64_t> SymbolIDs;
  std::map<std::pair<const SectionChunk *, size_t>, uint64_t> RelocationIDs;

  llvm::DenseMap<uint64_t, InputFile *> NativeInputs;
  llvm::DenseMap<uint64_t, Chunk *> NativeSections;
  llvm::DenseMap<uint64_t, Chunk *> NativeAtoms;
  llvm::DenseMap<uint64_t, OutputSection *> NativeOutputSections;
  llvm::DenseMap<uint64_t, Symbol *> NativeSymbols;
  llvm::DenseMap<uint64_t, NativeRelocationRef> NativeRelocations;
};

} // namespace linker::coff

#endif
