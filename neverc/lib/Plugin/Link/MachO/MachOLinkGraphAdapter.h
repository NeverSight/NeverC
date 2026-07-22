#ifndef NEVERC_PLUGIN_LINK_MACHO_MACHOLINKGRAPHADAPTER_H
#define NEVERC_PLUGIN_LINK_MACHO_MACHOLINKGRAPHADAPTER_H

#include "Link/LinkGraph.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <memory>

namespace neverc::plugin {
class PluginTaskContext;
}

namespace linker::macho {

class InputFile;

/// Task-local projection between the native Mach-O linker and the public
/// NeverC LinkGraph. Native pointers are retained only in this host-side
/// identity map and never cross the C ABI.
///
/// This first version captures the frozen input set and routes every standard
/// LinkGraph transition through the unified plugin phase executor so plugins
/// observe the built-in Mach-O link. With no user providers registered the
/// projection is a faithful no-op, so the emitted image is byte-identical to
/// the native path. Richer section/symbol/relocation capture mirrors ELF/COFF
/// and can be layered on without changing this contract.
class MachOLinkGraphAdapter {
public:
  static llvm::Expected<std::unique_ptr<MachOLinkGraphAdapter>>
  create(neverc::plugin::PluginTaskContext &Task, llvm::StringRef TargetTriple,
         llvm::StringRef CPU, NevercTargetRelocationModel RelocationModel);

  ~MachOLinkGraphAdapter();
  MachOLinkGraphAdapter(const MachOLinkGraphAdapter &) = delete;
  MachOLinkGraphAdapter &operator=(const MachOLinkGraphAdapter &) = delete;

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
  MachOLinkGraphAdapter(neverc::plugin::PluginTaskContext &Task,
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
};

/// The adapter is owned by MachOLinkerContext and therefore follows exactly one
/// native link invocation, including nested and parallel links.
std::unique_ptr<MachOLinkGraphAdapter> &machoPluginLinkAdapter();

} // namespace linker::macho

#endif
