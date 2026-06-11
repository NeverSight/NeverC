//===----------------------------------------------------------------------===//
//
//  Link-time LTO output cache shared by all linker backends.
//
//  Full/auto LTO recomputes the whole IPO + codegen pipeline on every
//  link even when nothing changed (touch rebuilds, comment-only edits,
//  CI re-runs).  This cache keys the complete external input set of an
//  lto::LTO instance -- the bitcode buffers and symbol resolutions in
//  add order, the driver configuration, and the partitioning
//  environment -- and stores the produced task object buffers.  On a
//  hit the entire LTO stage (serial IPO + parallel codegen) is skipped.
//
//  Environment controls and on-disk naming live in LTOCacheContract.h.
//
//===----------------------------------------------------------------------===//

#ifndef LINKER_CORE_DRIVER_LTOCACHE_H
#define LINKER_CORE_DRIVER_LTOCACHE_H

#include "Linker/Core/Driver/LTOCacheContract.h" // IWYU pragma: export
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Support/MemoryBufferRef.h"
#include <string>

namespace linker {

struct LinkerDriverConfig;

/// Accumulates the cache key.  Feed every bitcode input in add order,
/// then finalize with the configuration.  The key covers everything
/// lto::LTO sees; two links with equal keys produce identical task
/// outputs.
class LTOCacheKey {
public:
  LTOCacheKey();

  /// Record one bitcode input and its symbol resolutions.  Must be
  /// called in lto::LTO::add() order, before the resolutions are
  /// consumed.
  void addInput(llvm::MemoryBufferRef mb,
                llvm::ArrayRef<llvm::lto::SymbolResolution> resols);

  /// Mix in the remaining lto::LTO inputs and return the final hex key.
  /// backendTag names the object format (outputs differ per backend for
  /// identical bitcode); emitAddrsig must be the value the backend passed
  /// to createLTOConfig().
  std::string finalize(const LinkerDriverConfig &cfg, unsigned maxTasks,
                       llvm::StringRef backendTag, bool emitAddrsig);

private:
  void appendConfig(const LinkerDriverConfig &cfg);

  friend std::string ltoPartitionCacheSalt(const LinkerDriverConfig &cfg,
                                           bool emitAddrsig);

  llvm::SmallString<512> material;
};

/// Salt for per-partition object cache keys: compiler identity plus the
/// configuration digest, without any input contents (the partition bitcode
/// itself carries those).  Computed once per link and captured by the
/// partition-cache hooks.
std::string ltoPartitionCacheSalt(const LinkerDriverConfig &cfg,
                                  bool emitAddrsig);

/// True when the per-partition object cache layer may run: the link cache
/// is usable for this configuration and NEVERC_LTO_PCACHE does not disable
/// the partition layer.
bool ltoPartitionCacheUsable(const LinkerDriverConfig &cfg);

/// Computes the entry key for one codegen partition -- salt + pipeline tag
/// + partition bitcode content -- writes it to keyOut, and returns true on
/// a cache hit with the stored object copied into obj.
bool ltoPartitionCacheLookup(llvm::StringRef salt, llvm::StringRef pipeTag,
                             llvm::StringRef bitcode, std::string &keyOut,
                             llvm::SmallVectorImpl<char> &obj);

/// Stores one partition object under a key produced by
/// ltoPartitionCacheLookup.
void ltoPartitionCacheStore(llvm::StringRef key, llvm::ArrayRef<char> obj);

/// False when caching is disabled (NEVERC_LTO_CACHE=0) or the link uses
/// features whose side effects a cache hit cannot reproduce: save-temps,
/// optimization remarks, time-trace, neverc plugins, or a
/// basic-block-sections list file.
bool ltoCacheUsable(const LinkerDriverConfig &cfg);

/// Runs ltoObj with the link cache wrapped around it.  bufs must already
/// be sized to getMaxTasks(); task outputs land in bufs either way.  On a
/// key hit the LTO pipeline is skipped entirely; on a miss (or when usable
/// is false) it runs, and its outputs are stored on completion.
/// backendTag / emitAddrsig: see LTOCacheKey::finalize().
void runLTOWithCache(llvm::lto::LTO &ltoObj, LTOCacheKey &key, bool usable,
                     const LinkerDriverConfig &cfg,
                     llvm::StringRef backendTag, bool emitAddrsig,
                     llvm::MutableArrayRef<llvm::SmallString<0>> bufs);

} // namespace linker

#endif // LINKER_CORE_DRIVER_LTOCACHE_H
