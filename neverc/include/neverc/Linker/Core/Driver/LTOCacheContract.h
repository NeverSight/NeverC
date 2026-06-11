//===----------------------------------------------------------------------===//
//
//  Externally observable contract of the LTO link cache: the environment
//  variables users set and the on-disk naming scheme.  This is the single
//  definition point shared by the implementation (LTOCache.cpp) and the
//  black-box test suite, so it is deliberately free of LLVM dependencies.
//
//===----------------------------------------------------------------------===//

#ifndef LINKER_CORE_DRIVER_LTOCACHECONTRACT_H
#define LINKER_CORE_DRIVER_LTOCACHECONTRACT_H

namespace linker {

/// Set to ltoCacheDisableValue to disable the cache entirely.
inline constexpr char ltoCacheEnvVar[] = "NEVERC_LTO_CACHE";
inline constexpr char ltoCacheDisableValue[] = "0";

/// Set to ltoCacheDisableValue to disable only the per-partition object
/// cache layer; the full-link cache above is governed by ltoCacheEnvVar.
/// Partition entries share the cache directory and pruning policy.
inline constexpr char ltoPartitionCacheEnvVar[] = "NEVERC_LTO_PCACHE";

/// Overrides the cache directory
/// (default: <user cache dir>/ltoCacheDefaultDirName).
inline constexpr char ltoCacheDirEnvVar[] = "NEVERC_LTO_CACHE_DIR";
inline constexpr char ltoCacheDefaultDirName[] = "neverc-lto";

/// Pruning policy in llvm::parseCachePruningPolicy syntax
/// (default: prune_after=1w, cache_size=75%, interval-gated).
inline constexpr char ltoCachePolicyEnvVar[] = "NEVERC_LTO_CACHE_POLICY";

/// Entry filename prefix.  Must stay "llvmcache-": llvm::pruneCache only
/// ever deletes files carrying this prefix, which keeps pruning safe even
/// if the cache directory is pointed at a shared location.
inline constexpr char ltoCacheEntryPrefix[] = "llvmcache-";

/// Suffix of in-flight temporary files; never a valid entry.
inline constexpr char ltoCacheTmpSuffix[] = ".tmp";

} // namespace linker

#endif // LINKER_CORE_DRIVER_LTOCACHECONTRACT_H
