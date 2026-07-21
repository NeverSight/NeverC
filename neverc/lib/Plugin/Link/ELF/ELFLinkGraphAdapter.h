#ifndef NEVERC_PLUGIN_LINK_ELF_ELFLINKGRAPHADAPTER_H
#define NEVERC_PLUGIN_LINK_ELF_ELFLINKGRAPHADAPTER_H

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

namespace linker::elf {

class InputFile;
class InputSectionBase;
class OutputSection;
class Symbol;

/// Task-local projection between the native ELF linker and the public
/// NeverC LinkGraph. Native pointers are retained only in this host-side
/// identity map and never cross the C ABI.
class ELFLinkGraphAdapter {
public:
  static llvm::Expected<std::unique_ptr<ELFLinkGraphAdapter>>
  create(neverc::plugin::PluginTaskContext &Task, llvm::StringRef TargetTriple,
         llvm::StringRef CPU, NevercTargetRelocationModel RelocationModel);

  ~ELFLinkGraphAdapter();
  ELFLinkGraphAdapter(const ELFLinkGraphAdapter &) = delete;
  ELFLinkGraphAdapter &operator=(const ELFLinkGraphAdapter &) = delete;

  /// Synchronize one or more already-completed native transitions through the
  /// unified plugin phase executor.
  llvm::Error advanceTo(NevercLinkState State);

  /// Import the native image, run post-emit and sealed output gates, and
  /// transactionally publish the final output bundle.
  llvm::Error publishImage(llvm::ArrayRef<uint8_t> Bytes);

  const std::shared_ptr<neverc::plugin::PluginLinkGraph> &graph() const {
    return Graph;
  }

private:
  struct NativeRelocationRef {
    InputSectionBase *Section = nullptr;
    size_t Index = 0;
  };

  ELFLinkGraphAdapter(neverc::plugin::PluginTaskContext &Task,
                      std::shared_ptr<neverc::plugin::PluginLinkGraph> Graph);

  llvm::Expected<std::shared_ptr<neverc::plugin::PluginLinkGraph>>
  capture(const neverc::plugin::PluginLinkGraph &Previous,
          NevercLinkState State);
  llvm::Error applyDelta(const neverc::plugin::PluginLinkGraph &Before,
                         const neverc::plugin::PluginLinkGraph &After,
                         NevercLinkState State);

  neverc::plugin::PluginTaskContext &Task;
  std::shared_ptr<neverc::plugin::PluginLinkGraph> Graph;

  llvm::DenseMap<const InputFile *, uint64_t> InputIDs;
  llvm::DenseMap<const InputSectionBase *, uint64_t> SectionIDs;
  llvm::DenseMap<const InputSectionBase *, uint64_t> AtomIDs;
  llvm::DenseMap<const OutputSection *, uint64_t> OutputSectionIDs;
  llvm::DenseMap<const Symbol *, uint64_t> SymbolIDs;
  std::map<std::pair<const InputSectionBase *, size_t>, uint64_t> RelocationIDs;

  llvm::DenseMap<uint64_t, InputFile *> NativeInputs;
  llvm::DenseMap<uint64_t, InputSectionBase *> NativeSections;
  llvm::DenseMap<uint64_t, InputSectionBase *> NativeAtoms;
  llvm::DenseMap<uint64_t, OutputSection *> NativeOutputSections;
  llvm::DenseMap<uint64_t, Symbol *> NativeSymbols;
  llvm::DenseMap<uint64_t, NativeRelocationRef> NativeRelocations;
};

/// The adapter is owned by ELFLinkerContext and therefore follows exactly one
/// native link invocation, including nested and parallel links.
std::unique_ptr<ELFLinkGraphAdapter> &elfPluginLinkAdapter();

} // namespace linker::elf

#endif
