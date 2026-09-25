//===----------------------------------------------------------------------===//
//
//  FastLink utilities: errors, the worker pool, hashing and the lock-free
//  name table.
//
//===----------------------------------------------------------------------===//

#ifndef LINKER_ELF_FASTPATH_FASTLINKSUPPORT_H
#define LINKER_ELF_FASTPATH_FASTLINKSUPPORT_H

#include <elf.h>

// Definitions missing from older <elf.h> versions.
#ifndef SHF_GNU_RETAIN
#define SHF_GNU_RETAIN (1 << 21)
#endif
#ifndef SHT_X86_64_UNWIND
#define SHT_X86_64_UNWIND 0x70000001
#endif
#ifndef DF_1_PIE
#define DF_1_PIE 0x08000000
#endif
#ifndef R_X86_64_GOTPCRELX
#define R_X86_64_GOTPCRELX 41
#endif
#ifndef R_X86_64_REX_GOTPCRELX
#define R_X86_64_REX_GOTPCRELX 42
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace fl {

using std::string;
using std::string_view;
using std::vector;

// Raised for any input or situation the pipeline does not handle; the link
// is then declined.
struct Failure {
  string message;
};

[[noreturn]] inline void fatal(const string &msg) { throw Failure{msg}; }

inline uint64_t alignTo(uint64_t v, uint64_t a) {
  return a <= 1 ? v : (v + a - 1) & ~(a - 1);
}

// ------------------------------------------------------------ thread pool
class Pool {
public:
  explicit Pool(unsigned n) : n_(n ? n : 1) {
    for (unsigned i = 1; i < n_; ++i)
      threads_.emplace_back([this, i] { loop(i); });
  }
  ~Pool() {
    {
      std::lock_guard<std::mutex> l(m_);
      stop_ = true;
    }
    cv_.notify_all();
    for (auto &t : threads_)
      t.join();
  }
  unsigned size() const { return n_; }
  static unsigned self() { return tid_; }

  // Runs fn(worker) on every worker including the caller. An exception from
  // any worker is rethrown here once all workers have finished.
  void run(const std::function<void(unsigned)> &fn) {
    if (n_ == 1) {
      fn(0);
      return;
    }
    {
      std::lock_guard<std::mutex> l(m_);
      job_ = &fn;
      running_ = n_ - 1;
      ++gen_;
    }
    cv_.notify_all();
    invoke(fn, 0);
    std::unique_lock<std::mutex> l(m_);
    done_.wait(l, [&] { return running_ == 0; });
    if (error_) {
      std::exception_ptr e = error_;
      error_ = nullptr;
      failed_ = false;
      std::rethrow_exception(e);
    }
  }
  // Whether a worker of the current run() has failed.
  bool failed() const { return failed_.load(std::memory_order_relaxed); }

  template <class F> void forEach(size_t count, F f, size_t grain = 0) {
    if (count == 0)
      return;
    if (grain == 0)
      grain = std::max<size_t>(1, count / (size_t(n_) * 16));
    if (n_ == 1 || count <= grain) {
      for (size_t i = 0; i < count; ++i)
        f(i);
      return;
    }
    std::atomic<size_t> next{0};
    run([&](unsigned) {
      for (;;) {
        size_t b = next.fetch_add(grain, std::memory_order_relaxed);
        if (b >= count || failed())
          return;
        size_t e = std::min(count, b + grain);
        for (size_t i = b; i < e; ++i)
          f(i);
      }
    });
  }

private:
  void loop(unsigned i) {
    tid_ = i;
    unsigned seen = 0;
    for (;;) {
      const std::function<void(unsigned)> *j;
      {
        std::unique_lock<std::mutex> l(m_);
        cv_.wait(l, [&] { return stop_ || gen_ != seen; });
        if (stop_)
          return;
        seen = gen_;
        j = job_;
      }
      invoke(*j, i);
      std::lock_guard<std::mutex> l(m_);
      if (--running_ == 0)
        done_.notify_all();
    }
  }
  void invoke(const std::function<void(unsigned)> &fn, unsigned i) {
    try {
      fn(i);
    } catch (...) {
      std::lock_guard<std::mutex> l(errorLock_);
      if (!error_)
        error_ = std::current_exception();
      failed_ = true;
    }
  }
  unsigned n_;
  vector<std::thread> threads_;
  std::mutex m_;
  std::condition_variable cv_, done_;
  const std::function<void(unsigned)> *job_ = nullptr;
  unsigned gen_ = 0, running_ = 0;
  bool stop_ = false;
  std::mutex errorLock_;
  std::exception_ptr error_;
  std::atomic<bool> failed_{false};
  static inline thread_local unsigned tid_ = 0;
};

// ------------------------------------------------------------ hashing
// A fast hash for bulk data (build ids): four independent lanes.
inline uint64_t hashBulk(const uint8_t *p, size_t n) {
  uint64_t a = 0x9E3779B97F4A7C15ull ^ n, b = 0xC2B2AE3D27D4EB4Full,
           c = 0x165667B19E3779F9ull, d = 0x27D4EB2F165667C5ull;
  size_t i = 0;
  for (; i + 32 <= n; i += 32) {
    uint64_t w[4];
    memcpy(w, p + i, 32);
    a = (a ^ w[0]) * 0xff51afd7ed558ccdull;
    b = (b ^ w[1]) * 0xc4ceb9fe1a85ec53ull;
    c = (c ^ w[2]) * 0xff51afd7ed558ccdull;
    d = (d ^ w[3]) * 0xc4ceb9fe1a85ec53ull;
    a ^= a >> 29;
    b ^= b >> 31;
    c ^= c >> 29;
    d ^= d >> 31;
  }
  for (; i < n; ++i)
    a = (a ^ p[i]) * 0x100000001b3ull;
  uint64_t h = a ^ (b * 3) ^ (c * 5) ^ (d * 7);
  h = (h ^ (h >> 32)) * 0xff51afd7ed558ccdull;
  return h ^ (h >> 29);
}

inline uint64_t hashBytes(const char *p, size_t n) {
  uint64_t h = 0x9E3779B97F4A7C15ull ^ n;
  size_t i = 0;
  for (; i + 8 <= n; i += 8) {
    uint64_t w;
    memcpy(&w, p + i, 8);
    h = (h ^ w) * 0xff51afd7ed558ccdull;
    h ^= h >> 32;
  }
  uint64_t tail = 0;
  memcpy(&tail, p + i, n - i);
  h = (h ^ tail) * 0xc4ceb9fe1a85ec53ull;
  h ^= h >> 29;
  return h;
}

// Lock-free insert-only table from names to dense ids.
class NameTable {
public:
  explicit NameTable(size_t expected) {
    size_t cap = 1024;
    while (cap < expected * 2)
      cap <<= 1;
    mask_ = cap - 1;
    entries_ = static_cast<Entry *>(calloc(cap, sizeof(Entry)));
  }
  uint32_t intern(const char *p, size_t n) {
    const uint64_t h = hashBytes(p, n) | 2;
    const unsigned worker = Pool::self();
    for (size_t i = h & mask_;; i = (i + 1) & mask_) {
      Entry &e = entries_[i];
      uint64_t cur = e.hash.load(std::memory_order_acquire);
      if (cur == 0) {
        uint64_t expect = 0;
        if (e.hash.compare_exchange_strong(expect, 1,
                                           std::memory_order_acq_rel)) {
          e.ptr = p;
          e.len = static_cast<uint32_t>(n);
          e.id = allocId(worker);
          e.hash.store(h, std::memory_order_release);
          // Past half full the table reports failure; callers retry with
          // a larger table.
          if (e.id >= mask_ / 2)
            return UINT32_MAX;
          return e.id;
        }
        cur = expect;
      }
      while (cur == 1)
        cur = e.hash.load(std::memory_order_acquire);
      if (cur == h && e.len == n && memcmp(e.ptr, p, n) == 0)
        return e.id;
    }
  }
  // Looks a name up without inserting it; returns UINT32_MAX if absent.
  uint32_t find(const char *p, size_t n) const {
    const uint64_t h = hashBytes(p, n) | 2;
    for (size_t i = h & mask_;; i = (i + 1) & mask_) {
      const Entry &e = entries_[i];
      uint64_t cur = e.hash.load(std::memory_order_acquire);
      if (cur == 0)
        return UINT32_MAX;
      if (cur == h && e.len == n && memcmp(e.ptr, p, n) == 0)
        return e.id;
    }
  }
  // One past the highest id handed out; ids below it may be unused.
  uint32_t size() const { return next_.load(); }
  // Fills names[id] for every entry.
  void names(vector<string_view> &out) const {
    out.assign(size(), string_view());
    for (size_t i = 0; i <= mask_; ++i) {
      const Entry &e = entries_[i];
      if (e.hash.load(std::memory_order_relaxed) > 1 && e.id < out.size())
        out[e.id] = string_view(e.ptr, e.len);
    }
  }

private:
  struct Entry {
    std::atomic<uint64_t> hash;
    const char *ptr;
    uint32_t len;
    uint32_t id;
  };
  // Workers take ids in blocks so they rarely touch the shared counter.
  static constexpr unsigned MaxWorkers = 256, Block = 64;
  uint32_t allocId(unsigned worker) {
    if (worker >= MaxWorkers)
      return next_.fetch_add(1, std::memory_order_relaxed);
    Cursor &c = cursors_[worker];
    if (c.next == c.end) {
      c.next = next_.fetch_add(Block, std::memory_order_relaxed);
      c.end = c.next + Block;
    }
    return c.next++;
  }
  struct alignas(64) Cursor {
    uint32_t next = 0, end = 0;
  };
  Entry *entries_;
  size_t mask_;
  std::atomic<uint32_t> next_{0};
  Cursor cursors_[MaxWorkers];
};

template <class T> inline void atomicMin(std::atomic<T> &a, T v) {
  T cur = a.load(std::memory_order_relaxed);
  while (v < cur &&
         !a.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {
  }
}
template <class T> inline void atomicOr(std::atomic<T> &a, T bits) {
  if ((a.load(std::memory_order_relaxed) & bits) != bits)
    a.fetch_or(bits, std::memory_order_relaxed);
}

using Clock = std::chrono::steady_clock;
inline double msSince(Clock::time_point t) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}

} // namespace fl

#endif // LINKER_ELF_FASTPATH_FASTLINKSUPPORT_H
