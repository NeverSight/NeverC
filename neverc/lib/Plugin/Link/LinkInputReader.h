#ifndef NEVERC_PLUGIN_LINK_LINKINPUTREADER_H
#define NEVERC_PLUGIN_LINK_LINKINPUTREADER_H

#include "LinkGraph.h"
#include "LinkRequest.h"
#include "LinkerScriptProvider.h"
#include "neverc/Plugin/Host/ObjectReaderProvider.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace neverc::plugin {

class PluginObjectGraph;
class PluginTaskContext;
class LinkInputSetImpl;

struct LinkInputReaderOptions {
  uint64_t MaterializationBudgetBytes = UINT64_MAX;
};

/// Owns the immutable input blobs, normalized LinkGraph, lazy archive state,
/// and ObjectGraphs produced while reading one frozen LinkRequest.
class LinkInputSet {
public:
  ~LinkInputSet();
  LinkInputSet(LinkInputSet &&) noexcept;
  LinkInputSet &operator=(LinkInputSet &&) noexcept;
  LinkInputSet(const LinkInputSet &) = delete;
  LinkInputSet &operator=(const LinkInputSet &) = delete;

  PluginLinkGraph &graph();
  const PluginLinkGraph &graph() const;

  PluginObjectGraph *objectGraphForInput(uint64_t InputID);
  const PluginObjectGraph *objectGraphForInput(uint64_t InputID) const;
  std::vector<PluginObjectGraph *> objectGraphs();
  std::vector<const PluginObjectGraph *> objectGraphs() const;
  /// Immutable source bytes for each object, parallel to objectGraphs().  An
  /// entry is empty when the origin bytes are unavailable, in which case callers
  /// must fall back to re-serializing the graph.
  std::vector<llvm::ArrayRef<uint8_t>> objectGraphSourceBytes() const;
  llvm::Expected<llvm::MemoryBufferRef>
  bitcodeBufferForModule(uint64_t ModuleID);
  const LinkerScriptResult *scriptResultForInput(uint64_t InputID) const;

  llvm::Error materializeArchiveMember(uint64_t MemberID,
                                       llvm::StringRef Reason);
  llvm::Error materializeWholeArchives();
  llvm::Expected<size_t>
  materializeArchiveSymbols(llvm::ArrayRef<llvm::StringRef> UndefinedSymbols);

  uint64_t materializedBytes() const;

private:
  explicit LinkInputSet(std::unique_ptr<LinkInputSetImpl> ImplValue);
  std::unique_ptr<LinkInputSetImpl> Impl;

  friend class LinkInputReader;
};

/// Reads every input through an explicitly supplied task VFS, hashes the
/// immutable bytes, freezes the probe route, and normalizes input containers.
class LinkInputReader {
public:
  LinkInputReader(PluginTaskContext &Task, llvm::vfs::FileSystem &FileSystem,
                  const ObjectReaderProvider &Objects,
                  LinkInputReaderOptions Options = {},
                  const LinkerScriptProvider *Scripts = nullptr);

  llvm::Expected<std::unique_ptr<LinkInputSet>>
  read(const LinkRequest &Request) const;

private:
  PluginTaskContext &Task;
  llvm::vfs::FileSystem &FileSystem;
  const ObjectReaderProvider &Objects;
  LinkInputReaderOptions Options;
  const LinkerScriptProvider *Scripts;
};

} // namespace neverc::plugin

#endif
