#include "Linker/Core/Driver/LTOCache.h"
#include "Linker/Core/Driver/CommonLTOConfig.h"
#include "Linker/Core/Driver/Dispatcher.h"
#include "Linker/Core/Runtime/Diagnostic.h"
#include "neverc/Foundation/Core/Version.h"
#include "llvm/Support/CachePruning.h"
#include "llvm/Support/Caching.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/xxhash.h"
#include <cstdlib>

using namespace llvm;

// ===----------------------------------------------------------------------===
// Key construction
// ===----------------------------------------------------------------------===

static void appendU64(SmallVectorImpl<char> &out, uint64_t v) {
  for (int i = 0; i < 8; ++i)
    out.push_back(char((v >> (i * 8)) & 0xff));
}

static void appendStr(SmallVectorImpl<char> &out, StringRef s) {
  appendU64(out, s.size());
  out.append(s.begin(), s.end());
}

linker::LTOCacheKey::LTOCacheKey() {
  // The compiler build is part of the key: any pipeline or codegen change
  // must invalidate old entries.
  appendStr(material, neverc::getNeverCFullVersion());
  appendStr(material, neverc::getNeverCFullRepositoryVersion());
  // The repository version only changes per commit.  Developers rebuilding
  // the compiler from a dirty tree would otherwise hit stale entries, so
  // bind the key to the running binary itself (size + mtime).
  static const int anchor = 0;
  std::string exe = sys::fs::getMainExecutable(nullptr, (void *)&anchor);
  sys::fs::file_status st;
  if (!exe.empty() && !sys::fs::status(exe, st)) {
    appendU64(material, st.getSize());
    appendU64(material, uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                            st.getLastModificationTime().time_since_epoch())
                            .count()));
  }
}

void linker::LTOCacheKey::addInput(MemoryBufferRef mb,
                                   ArrayRef<lto::SymbolResolution> resols) {
  // Content only: the buffer identifier is often a temporary object path
  // (single-command auto-LTO) and must not influence the key.
  appendU64(material, mb.getBufferSize());
  appendU64(material, xxh3_64bits(mb.getBuffer()));
  appendU64(material, resols.size());
  for (const lto::SymbolResolution &r : resols)
    material.push_back(char(r.Prevailing | r.FinalDefinitionInLinkageUnit << 1 |
                            r.VisibleToRegularObj << 2 | r.ExportDynamic << 3 |
                            r.LinkerRedefined << 4));
}

// Two independently salted 64-bit lanes give a 128-bit key.  Consumes the
// material (a lane salt is appended in place).
static std::string hexKeyFromMaterial(SmallVectorImpl<char> &material) {
  static constexpr char hiLaneSalt = '\x9e';
  uint64_t lo = xxh3_64bits(StringRef(material.data(), material.size()));
  material.push_back(hiLaneSalt);
  uint64_t hi = xxh3_64bits(StringRef(material.data(), material.size()));
  SmallString<33> hex;
  raw_svector_ostream os(hex);
  os << format_hex_no_prefix(hi, 16) << format_hex_no_prefix(lo, 16);
  return std::string(hex);
}

void linker::LTOCacheKey::appendConfig(const LinkerDriverConfig &cfg) {
  // Every LinkerDriverConfig field consumed by createLTOConfig() or by the
  // partitioning hooks.  Fields that only affect native-link output (map
  // files, strip level, build-id, ...) are deliberately excluded; fields
  // that trigger a cache bypass (remarks, save-temps, plugins) never reach
  // this point with interesting values.
  appendStr(material, cfg.cpu);
  appendU64(material, uint64_t(int64_t(cfg.ltoOptLevel)));
  appendU64(material, uint64_t(int64_t(cfg.ltoCGOLevel)));
  appendStr(material, cfg.ltoBasicBlockSections);
  appendU64(material, cfg.ltoUniqueBasicBlockSectionNames);
  appendU64(material, uint64_t(int64_t(cfg.debuggerTuning)));
  appendU64(material, cfg.splitMachineFunctions);
  appendU64(material, cfg.jmcInstrument);
  appendU64(material, cfg.emulatedTLS);
  appendU64(material, cfg.stackSizeSection);
  appendU64(material, cfg.mllvmOpts.size());
  for (const std::string &opt : cfg.mllvmOpts)
    appendStr(material, opt);
  appendU64(material, cfg.ltoPartitions);
  appendU64(material, cfg.threadCount);
  appendU64(material,
            uint64_t(cfg.shared) | uint64_t(cfg.bundle) << 1 |
                uint64_t(cfg.pie) << 2 | uint64_t(cfg.relocatable) << 3 |
                uint64_t(cfg.staticLink) << 4 |
                uint64_t(cfg.exportDynamic) << 5);
}

std::string linker::LTOCacheKey::finalize(const LinkerDriverConfig &cfg,
                                          unsigned maxTasks,
                                          StringRef backendTag,
                                          bool emitAddrsig) {
  appendConfig(cfg);
  appendStr(material, backendTag);
  appendU64(material, emitAddrsig);
  // Output shape: the task vector layout and the partition count chosen by
  // the parallel codegen hooks depend on these.
  appendU64(material, maxTasks);
  appendU64(material, llvm::thread::hardware_concurrency());
  return hexKeyFromMaterial(material);
}

std::string linker::ltoPartitionCacheSalt(const LinkerDriverConfig &cfg,
                                          bool emitAddrsig) {
  // No backendTag: the partition object is produced purely by TargetMachine
  // codegen from the partition module, and the module triple is part of the
  // hashed bitcode.  No maxTasks/hardware_concurrency either: a different
  // partitioning changes the partition contents, which the content hash
  // already covers.
  LTOCacheKey k;
  k.appendConfig(cfg);
  appendStr(k.material, "neverc-pcg-salt-v1");
  appendU64(k.material, emitAddrsig);
  return hexKeyFromMaterial(k.material);
}

// ===----------------------------------------------------------------------===
// Eligibility & directory resolution
// ===----------------------------------------------------------------------===

bool linker::ltoCacheUsable(const LinkerDriverConfig &cfg) {
  if (const char *env = getenv(ltoCacheEnvVar))
    if (StringRef(env) == ltoCacheDisableValue)
      return false;
  // Features with side effects (extra output files, loaded plugin code)
  // that a cache hit would silently skip.
  if (cfg.saveTemps || cfg.timeTraceEnabled || !cfg.optRemarksFilename.empty())
    return false;
  if (!cfg.nevercPluginPaths.empty())
    return false;
  if (cfg.pluginSession)
    return false;
  // A basic-block-sections list file is read by createLTOConfig; its
  // contents are not covered by the key.
  if (ltoBasicBlockSectionsIsListFile(cfg.ltoBasicBlockSections))
    return false;
  return true;
}

static bool ltoCacheDir(SmallVectorImpl<char> &dir) {
  if (const char *env = getenv(linker::ltoCacheDirEnvVar)) {
    dir.append(env, env + strlen(env));
    return !dir.empty();
  }
  if (!sys::path::cache_directory(dir))
    return false;
  sys::path::append(dir, linker::ltoCacheDefaultDirName);
  return true;
}

static void ltoCacheEntryPath(StringRef key, SmallVectorImpl<char> &path) {
  sys::path::append(path, Twine(linker::ltoCacheEntryPrefix) + key);
}

// ===----------------------------------------------------------------------===
// Pack format
// ===----------------------------------------------------------------------===
//
//   "NCLC0001"                     magic, 8 bytes
//   u64 numEntries
//   { u64 task, u64 size } * numEntries
//   payload bytes, concatenated in entry order
//   u64 xxh3 of payload
//
// All integers little-endian via appendU64/readU64.

static const char ltoCacheMagic[] = {'N', 'C', 'L', 'C', '0', '0', '0', '1'};

static constexpr uint64_t packU64Bytes = sizeof(uint64_t);
static constexpr uint64_t packMagicBytes = sizeof(ltoCacheMagic);
/// magic + u64 numEntries.
static constexpr uint64_t packHeaderBytes = packMagicBytes + packU64Bytes;
/// u64 task + u64 size.
static constexpr uint64_t packEntryBytes = 2 * packU64Bytes;
static constexpr uint64_t packChecksumBytes = packU64Bytes;

static uint64_t readU64(const char *p) {
  uint64_t v = 0;
  for (unsigned i = 0; i < packU64Bytes; ++i)
    v |= uint64_t(uint8_t(p[i])) << (i * 8);
  return v;
}

namespace {
struct PackEntry {
  uint64_t task;
  uint64_t size;
};
} // namespace

static PackEntry readPackEntry(StringRef data, uint64_t i) {
  const char *p = data.data() + packHeaderBytes + i * packEntryBytes;
  return {readU64(p), readU64(p + packU64Bytes)};
}

static bool ltoCacheLookup(StringRef key,
                           MutableArrayRef<SmallString<0>> bufs) {
  SmallString<128> path;
  if (!ltoCacheDir(path))
    return false;
  ltoCacheEntryPath(key, path);

  // OF_UpdateAtime keeps pruneCache's LRU ordering meaningful on volumes
  // that mount noatime (same pattern as llvm::localCache).
  Expected<sys::fs::file_t> fdOrErr =
      sys::fs::openNativeFileForRead(path, sys::fs::OF_UpdateAtime, nullptr);
  if (!fdOrErr) {
    consumeError(fdOrErr.takeError());
    return false;
  }
  ErrorOr<std::unique_ptr<MemoryBuffer>> mbOrErr =
      MemoryBuffer::getOpenFile(*fdOrErr, path, /*FileSize=*/-1,
                                /*RequiresNullTerminator=*/false);
  if (std::error_code ec = sys::fs::closeFile(*fdOrErr))
    (void)ec;
  if (!mbOrErr)
    return false;

  StringRef data = (*mbOrErr)->getBuffer();
  auto corrupt = [&] {
    if (std::error_code ec = sys::fs::remove(path))
      (void)ec;
    return false;
  };

  if (data.size() < packHeaderBytes ||
      memcmp(data.data(), ltoCacheMagic, packMagicBytes) != 0)
    return corrupt();
  uint64_t numEntries = readU64(data.data() + packMagicBytes);
  uint64_t tableBytes = packHeaderBytes + numEntries * packEntryBytes;
  if (numEntries > bufs.size() || data.size() < tableBytes + packChecksumBytes)
    return corrupt();

  // Sizes are validated against the file size before use; tasks must be
  // strictly increasing (the writer emits them in order), which also rules
  // out duplicate slots.
  uint64_t payloadSize = 0;
  uint64_t prevTask = UINT64_MAX;
  for (uint64_t i = 0; i < numEntries; ++i) {
    PackEntry e = readPackEntry(data, i);
    if (e.task >= bufs.size() ||
        (prevTask != UINT64_MAX && e.task <= prevTask))
      return corrupt();
    prevTask = e.task;
    if (e.size > data.size() || payloadSize + e.size < payloadSize)
      return corrupt();
    payloadSize += e.size;
  }
  if (data.size() != tableBytes + payloadSize + packChecksumBytes)
    return corrupt();

  StringRef payload = data.substr(tableBytes, payloadSize);
  if (readU64(data.data() + tableBytes + payloadSize) != xxh3_64bits(payload))
    return corrupt();

  uint64_t off = 0;
  for (uint64_t i = 0; i < numEntries; ++i) {
    PackEntry e = readPackEntry(data, i);
    bufs[e.task].assign(payload.substr(off, e.size));
    off += e.size;
  }
  return true;
}

static void ltoCacheStore(StringRef key, ArrayRef<SmallString<0>> bufs) {
  SmallString<128> dir;
  if (!ltoCacheDir(dir))
    return;
  if (sys::fs::create_directories(dir, /*IgnoreExisting=*/true))
    return;

  SmallString<0> pack;
  pack.append(ltoCacheMagic, ltoCacheMagic + packMagicBytes);
  uint64_t numEntries = 0;
  for (const auto &b : bufs)
    if (!b.empty())
      ++numEntries;
  appendU64(pack, numEntries);
  for (size_t task = 0; task < bufs.size(); ++task)
    if (!bufs[task].empty()) {
      appendU64(pack, task);
      appendU64(pack, bufs[task].size());
    }
  size_t payloadStart = pack.size();
  for (const auto &b : bufs)
    if (!b.empty())
      pack.append(b.begin(), b.end());
  appendU64(pack,
            xxh3_64bits(StringRef(pack).substr(payloadStart)));

  SmallString<128> entryPath(dir);
  ltoCacheEntryPath(key, entryPath);
  SmallString<128> tmpModel(dir);
  sys::path::append(tmpModel, Twine(linker::ltoCacheEntryPrefix) + "%%%%%%" +
                                  linker::ltoCacheTmpSuffix);
  auto tmp = sys::fs::TempFile::create(tmpModel,
                                       sys::fs::owner_read |
                                           sys::fs::owner_write);
  if (!tmp) {
    consumeError(tmp.takeError());
    return;
  }
  {
    raw_fd_ostream os(tmp->FD, /*shouldClose=*/false);
    os << pack;
    os.flush();
    if (os.has_error()) {
      consumeError(tmp->discard());
      return;
    }
  }
  // Concurrent stores of the same key write identical content; whichever
  // rename lands last wins and both are valid.
  if (Error e = tmp->keep(entryPath)) {
    consumeError(std::move(e));
    consumeError(tmp->discard());
    return;
  }

  CachePruningPolicy policy;
  if (const char *env = getenv(linker::ltoCachePolicyEnvVar)) {
    if (auto p = parseCachePruningPolicy(env))
      policy = *p;
    else
      consumeError(p.takeError());
  }
  pruneCache(dir, policy);
}

// ===----------------------------------------------------------------------===
// Per-partition object cache
// ===----------------------------------------------------------------------===
//
// Entries reuse the pack format above with a single task-0 buffer, so the
// directory, pruning policy and corruption handling are shared with the
// full-link cache.

bool linker::ltoPartitionCacheUsable(const LinkerDriverConfig &cfg) {
  if (const char *env = getenv(ltoPartitionCacheEnvVar))
    if (StringRef(env) == ltoCacheDisableValue)
      return false;
  return ltoCacheUsable(cfg);
}

bool linker::ltoPartitionCacheLookup(StringRef salt, StringRef pipeTag,
                                     StringRef bitcode, std::string &keyOut,
                                     SmallVectorImpl<char> &obj) {
  SmallString<64> material;
  appendStr(material, salt);
  appendStr(material, pipeTag);
  appendU64(material, bitcode.size());
  appendU64(material, xxh3_64bits(bitcode));
  keyOut = hexKeyFromMaterial(material);

  SmallString<0> buf[1];
  if (!ltoCacheLookup(keyOut, buf))
    return false;
  obj.assign(buf[0].begin(), buf[0].end());
  return true;
}

void linker::ltoPartitionCacheStore(StringRef key, ArrayRef<char> obj) {
  SmallString<0> buf[1];
  buf[0].assign(obj.begin(), obj.end());
  ltoCacheStore(key, buf);
}

// ===----------------------------------------------------------------------===
// Cached pipeline driver
// ===----------------------------------------------------------------------===

void linker::runLTOWithCache(lto::LTO &ltoObj, LTOCacheKey &cacheKey,
                             bool usable, const LinkerDriverConfig &cfg,
                             StringRef backendTag, bool emitAddrsig,
                             MutableArrayRef<SmallString<0>> bufs) {
  std::string key;
  if (usable) {
    key = cacheKey.finalize(cfg, bufs.size(), backendTag, emitAddrsig);
    if (ltoCacheLookup(key, bufs))
      return;
  }
  checkError(ltoObj.run([&](size_t task, const Twine &moduleName) {
    return std::make_unique<CachedFileStream>(
        std::make_unique<raw_svector_ostream>(bufs[task]));
  }));
  if (usable)
    ltoCacheStore(key, bufs);
}
