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

#include "llvm/Config/llvm-config.h"
#include "llvm/Support/FileOutputBuffer.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Parallel.h"
#include "llvm/Support/TimeProfiler.h"

#if LLVM_ON_UNIX
#include <unistd.h>
#endif
#include <condition_variable>
#include <mutex>
#include <thread>

using namespace llvm;
using namespace linker;

//===----------------------------------------------------------------------===//
// Pre-fault mapped output buffer
//===----------------------------------------------------------------------===//

void linker::prefaultBuffer(uint8_t *buf, size_t size) {
#if defined(__unix__) || defined(__APPLE__)
  if (!buf || size == 0)
    return;
  static const size_t pageSize = [] {
    long p = ::sysconf(_SC_PAGESIZE);
    return p > 0 ? static_cast<size_t>(p) : size_t(4096);
  }();
  const size_t chunkSize = 32 * 1024 * 1024;
  const size_t numChunks = (size + chunkSize - 1) / chunkSize;
  parallelFor(0, numChunks, [&](size_t i) {
    size_t begin = i * chunkSize;
    size_t end = std::min(begin + chunkSize, size);
    for (size_t off = begin; off < end; off += pageSize)
      buf[off] = 0;
  });
#else
  (void)buf;
  (void)size;
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
void linker::unlinkAsync(StringRef path) {
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
  if (parallel::strategy.ThreadsRequested == 1)
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

  std::mutex m;
  std::condition_variable cv;
  bool started = false;
  std::thread([&, fd] {
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
