//===----------------------------------------------------------------------===//
//
//  FileIO — output-side filesystem helpers used by every linker backend.
//
//  Two responsibilities sit side by side because both deal with the path
//  the linker is about to materialise:
//
//    * Probing & opening output streams (`tryCreateFile`, `openFile`)
//      — a pre-flight write before the linker has anything useful to
//      commit.
//    * Asynchronous removal of an existing artefact at the destination
//      (`unlinkAsync`) — a perf hack hiding the slow `unlink(2)` syscall
//      on Unix.  Windows uses a rename-then-delete fallback so the
//      linker can proceed even while another process still holds an
//      open handle to the old file.
//
//===----------------------------------------------------------------------===//

#include "Linker/Core/Support/FileIO.h"
#include "Linker/Core/Runtime/Diagnostic.h"
#include "Linker/Core/Runtime/LinkerParallel.h"

#include "llvm/Config/llvm-config.h"
#include "llvm/Support/FileOutputBuffer.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/thread.h"

#if LLVM_ON_UNIX
#include <sys/mman.h>
#include <unistd.h>
#endif
#include <condition_variable>
#include <optional>
#include <string>
#include <mutex>

using namespace llvm;
using namespace linker;

//===----------------------------------------------------------------------===//
// Pre-fault mapped output buffer
//===----------------------------------------------------------------------===//

void linker::prefaultBuffer(uint8_t *buf, size_t size, bool fileBacked) {
#if LLVM_ON_UNIX
  if (!buf || size == 0)
    return;
#if defined(MADV_HUGEPAGE)
  if (fileBacked)
    (void)::madvise(buf, size, MADV_HUGEPAGE);
#else
  (void)fileBacked;
#endif
  const size_t pageSize = [] {
    long p = ::sysconf(_SC_PAGESIZE);
    return p > 0 ? static_cast<size_t>(p) : size_t(4096);
  }();
  // Small chunks let every worker share the page-allocation work.
  const size_t chunkSize = 4 * 1024 * 1024;
  const size_t numChunks = (size + chunkSize - 1) / chunkSize;
  parallelFor(0, numChunks, [&](size_t i) {
    size_t begin = i * chunkSize;
    size_t end = std::min(begin + chunkSize, size);
#if defined(__linux__)
    // Populating a range in one call avoids a page fault per page. Kernels
    // before 5.14 reject the advice; touch the pages instead.
    constexpr int populateWrite = 23; // MADV_POPULATE_WRITE
    uintptr_t first = reinterpret_cast<uintptr_t>(buf + begin);
    uintptr_t aligned = first / pageSize * pageSize;
    if (::madvise(reinterpret_cast<void *>(aligned),
                  reinterpret_cast<uintptr_t>(buf + end) - aligned,
                  populateWrite) == 0)
      return;
#endif
    for (size_t off = begin; off < end; off += pageSize)
      buf[off] = 0;
  });
#else
  (void)buf;
  (void)size;
  (void)fileBacked;
#endif
}

//===----------------------------------------------------------------------===//
// Output stream creation
//===----------------------------------------------------------------------===//

// Simulate file creation to see if `path` is writable.
//
// Determining whether a file is writable or not is amazingly hard, and the
// only truly reliable way is to actually create a file.  We refuse to do
// that here because the linker must not leave a partial artefact behind
// if the link itself later fails.  Re-implementing all the writability
// heuristics by hand would also be painful, so we defer the work to
// `FileOutputBuffer`: it does not touch the destination until `commit()`
// is called, so constructing one without committing is a cheap
// pre-flight.
std::error_code linker::tryCreateFile(StringRef path) {
  llvm::TimeTraceScope timeScope("Try create output file");
  if (path.empty())
    return std::error_code();
  if (path == "-")
    return std::error_code();
  return errorToErrorCode(FileOutputBuffer::create(path, 1).takeError());
}

// Create an empty file and return a write-only `raw_fd_ostream` for it.
std::unique_ptr<raw_fd_ostream> linker::openFile(StringRef file) {
  std::error_code ec;
  auto ret =
      std::make_unique<raw_fd_ostream>(file, ec, sys::fs::OpenFlags::OF_None);
  if (ec) {
    error("cannot open " + file + ": " + ec.message());
    return nullptr;
  }
  return ret;
}

//===----------------------------------------------------------------------===//
// Asynchronous unlink of the existing artefact at the destination path
//===----------------------------------------------------------------------===//

// Removes a given file asynchronously.  This is a performance hack, so
// remove it when operating systems are improved.
//
// On Linux (and probably on other Unix-like systems), `unlink(2)` is a
// noticeably slow system call.  As of 2016, `unlink` takes 250 ms to
// remove a 1 GB file on ext4.  Re-linking a 1 GB program in a regular
// compile-link-debug cycle therefore wastes 250 ms per iteration just
// to remove the previous output.  Since the linker can produce a 1 GB
// binary in about 5 s, that overhead counts.
//
// We spawn a background thread to remove the file; the calling thread
// returns almost immediately.
void linker::unlinkAsync(StringRef path, bool keepUntilExit) {
  if (!sys::fs::exists(path) || !sys::fs::is_regular_file(path))
    return;

#if defined(_WIN32)
  // On Windows, co-operative programs open the linker's output with
  // `FILE_SHARE_DELETE`.  That lets us delete the file (by moving it
  // aside to a temporary name and then deleting it) so the next link
  // can overwrite the existing path even while another process still
  // holds a handle to the old file.
  //
  // This is done on a best-effort basis — a failure here is not fatal;
  // the user merely gets an inconvenient workflow.
  //
  // The rename-and-delete dance keeps the linker working on every
  // Windows version the fork still supports.  Starting with Windows 10
  // 1903 a plain `remove(path)` would be enough; simplify this block
  // once support for older Windows releases is dropped.
  //
  // Warning: the WINVER and _WIN32_WINNT preprocessor defines affect
  // the behavior of the Windows calls we use here.  If this code stops
  // working, that is worth bearing in mind.
  SmallString<128> tmpName;
  if (!sys::fs::createUniqueFile(path + "%%%%%%%%.tmp", tmpName)) {
    if (!sys::fs::rename(path, tmpName))
      path = tmpName;
    else
      sys::fs::remove(tmpName);
  }
  sys::fs::remove(path);
#else
  if (!parallelEnabled())
    return;

  // We cannot just remove `path` from a different thread, because the
  // calling thread is about to create `path` as a new file.  Instead we
  // open the file on this thread and only close the fd on a helper
  // thread: the fd keeps a reference alive, so `remove` returns
  // immediately.
  int fd;
  std::error_code ec = sys::fs::openFileForRead(path, fd);
  sys::fs::remove(path);

  if (ec)
    return;
  // Releasing a large file's storage takes real CPU time; a process that
  // exits after the link leaves it to exit, off the link's critical path.
  if (keepUntilExit)
    return;

  std::mutex m;
  std::condition_variable cv;
  bool started = false;
  llvm::thread([&, fd] {
    {
      std::lock_guard<std::mutex> l(m);
      started = true;
      cv.notify_all();
    }
    ::close(fd);
  }).detach();

  // glibc ≤ 2.26 has a race that crashes the whole process when the
  // main thread calls `exit(2)` while another thread is starting up;
  // wait for the helper to signal it is running before returning.
  std::unique_lock<std::mutex> l(m);
  cv.wait(l, [&] { return started; });
#endif
}

//===----------------------------------------------------------------------===//
// Output file created ahead of its final size
//===----------------------------------------------------------------------===//

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

namespace {
constexpr size_t earlyChunkSize = 2 * 1024 * 1024;

#if defined(__linux__)
// The buffer finish() returns: an on-disk buffer over the early file's
// mapping, which may be larger than the output.
class EarlyOnDiskBuffer final : public FileOutputBuffer {
public:
  EarlyOnDiskBuffer(StringRef path, sys::fs::TempFile temp, uint8_t *base,
                    size_t mapped, size_t size, bool keepMappedAtCommit)
      : FileOutputBuffer(path), temp(std::move(temp)), base(base),
        mapped(mapped), size(size), keepMappedAtCommit(keepMappedAtCommit) {}
  uint8_t *getBufferStart() const override { return base; }
  uint8_t *getBufferEnd() const override { return base + size; }
  size_t getBufferSize() const override { return size; }
  Error commit() override {
    TimeTraceScope timeScope("Commit buffer to disk");
    // The shared mapping's pages already belong to the file, so the rename
    // does not wait for them. Tearing down a large mapping is slow; a process
    // about to exit leaves that to the kernel.
    if (keepMappedAtCommit)
      base = nullptr;
    else
      unmap();
    return temp.keep(FinalPath);
  }
  ~EarlyOnDiskBuffer() override {
    unmap();
    consumeError(temp.discard());
  }
  void discard() override { consumeError(temp.discard()); }

private:
  void unmap() {
    if (base)
      ::munmap(base, mapped);
    base = nullptr;
  }
  sys::fs::TempFile temp;
  uint8_t *base;
  size_t mapped;
  size_t size;
  bool keepMappedAtCommit;
};

void populate(uint8_t *begin, uint8_t *end, size_t pageSize) {
  constexpr int populateWrite = 23; // MADV_POPULATE_WRITE
  if (::madvise(begin, end - begin, populateWrite) == 0)
    return;
  for (uint8_t *p = begin; p < end; p += pageSize)
    *p = 0;
}
#endif
} // namespace

struct EarlyOutputFile::State {
  std::string path;
  std::optional<sys::fs::TempFile> temp;
  uint8_t *base = nullptr;
  size_t mapped = 0;
  size_t pageSize = 4096;
  size_t numChunks = 0;
  std::atomic<size_t> nextChunk{0};
  std::atomic<bool> stop{false};
  std::unique_ptr<std::atomic<uint8_t>[]> chunkDone;
  std::vector<std::thread> workers;
};

EarlyOutputFile::EarlyOutputFile(std::unique_ptr<State> state)
    : state(std::move(state)) {}

std::unique_ptr<EarlyOutputFile>
EarlyOutputFile::start(StringRef path, size_t estimatedSize, bool executable,
                       unsigned threads) {
#if defined(__linux__)
  if (path.empty() || path == "-" || estimatedSize == 0 || threads == 0)
    return nullptr;
  // Other file types keep FileOutputBuffer's handling.
  sys::fs::file_status status;
  sys::fs::status(path, status);
  if (status.type() != sys::fs::file_type::regular_file &&
      status.type() != sys::fs::file_type::file_not_found)
    return nullptr;

  unsigned mode = sys::fs::all_read | sys::fs::all_write;
  if (executable)
    mode |= sys::fs::all_exe;
  Expected<sys::fs::TempFile> temp =
      sys::fs::TempFile::create(path + ".tmp%%%%%%%", mode);
  if (!temp) {
    consumeError(temp.takeError());
    return nullptr;
  }
  auto state = std::make_unique<State>();
  state->path = path.str();
  state->temp.emplace(std::move(*temp));
  long page = ::sysconf(_SC_PAGESIZE);
  state->pageSize = page > 0 ? size_t(page) : 4096;
  const size_t size = alignTo(estimatedSize, state->pageSize);
  if (::ftruncate(state->temp->FD, size) != 0) {
    consumeError(state->temp->discard());
    return nullptr;
  }
  void *base = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED,
                      state->temp->FD, 0);
  if (base == MAP_FAILED) {
    consumeError(state->temp->discard());
    return nullptr;
  }
  state->base = static_cast<uint8_t *>(base);
  state->mapped = size;
  state->numChunks = (size + earlyChunkSize - 1) / earlyChunkSize;
  state->chunkDone.reset(new std::atomic<uint8_t>[state->numChunks]);
  for (size_t i = 0; i != state->numChunks; ++i)
    state->chunkDone[i].store(0, std::memory_order_relaxed);

  State *s = state.get();
  for (unsigned i = 0; i != threads; ++i)
    s->workers.emplace_back([s] {
      // Only use CPUs the link leaves idle.
      sched_param param{};
      pthread_setschedparam(pthread_self(), SCHED_IDLE, &param);
      while (!s->stop.load(std::memory_order_relaxed)) {
        const size_t i = s->nextChunk.fetch_add(1, std::memory_order_relaxed);
        if (i >= s->numChunks)
          return;
        uint8_t *begin = s->base + i * earlyChunkSize;
        populate(begin, std::min(begin + earlyChunkSize, s->base + s->mapped),
                 s->pageSize);
        s->chunkDone[i].store(1, std::memory_order_release);
      }
    });
  return std::unique_ptr<EarlyOutputFile>(
      new EarlyOutputFile(std::move(state)));
#else
  (void)path;
  (void)estimatedSize;
  (void)executable;
  (void)threads;
  return nullptr;
#endif
}

void EarlyOutputFile::stopWorkers() {
  state->stop.store(true, std::memory_order_relaxed);
  for (std::thread &t : state->workers)
    t.join();
  state->workers.clear();
}

EarlyOutputFile::~EarlyOutputFile() {
  if (!state)
    return;
  stopWorkers();
#if defined(__linux__)
  if (state->base)
    ::munmap(state->base, state->mapped);
  if (state->temp)
    consumeError(state->temp->discard());
#endif
}

Expected<std::unique_ptr<FileOutputBuffer>>
EarlyOutputFile::finish(size_t size, bool keepMappedAtCommit) {
#if defined(__linux__)
  stopWorkers();
  State &s = *state;
  if (::ftruncate(s.temp->FD, size) != 0)
    return errorCodeToError(std::error_code(errno, std::generic_category()));
  size_t populated = s.mapped;
  if (size > s.mapped) {
    void *base = ::mremap(s.base, s.mapped, size, MREMAP_MAYMOVE);
    if (base == MAP_FAILED)
      return errorCodeToError(std::error_code(errno, std::generic_category()));
    s.base = static_cast<uint8_t *>(base);
    s.mapped = size;
  }

  // Fault in what the background threads did not get to.
  SmallVector<std::pair<size_t, size_t>, 0> ranges;
  for (size_t i = 0; i != s.numChunks; ++i) {
    const size_t begin = i * earlyChunkSize;
    if (begin >= size)
      break;
    if (!s.chunkDone[i].load(std::memory_order_acquire))
      ranges.push_back({begin, std::min(begin + earlyChunkSize, size)});
  }
  for (size_t begin = populated; begin < size; begin += earlyChunkSize)
    ranges.push_back({begin, std::min(begin + earlyChunkSize, size)});
  {
    TimeTraceScope timeScope("Prefault output file");
    parallelFor(0, ranges.size(), [&](size_t i) {
      populate(s.base + ranges[i].first, s.base + ranges[i].second,
               s.pageSize);
    });
  }

  auto buffer = std::make_unique<EarlyOnDiskBuffer>(
      s.path, std::move(*s.temp), s.base, s.mapped, size, keepMappedAtCommit);
  s.temp.reset();
  s.base = nullptr;
  return std::unique_ptr<FileOutputBuffer>(std::move(buffer));
#else
  (void)size;
  (void)keepMappedAtCommit;
  llvm_unreachable("EarlyOutputFile is not supported on this host");
#endif
}
