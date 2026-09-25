//===----------------------------------------------------------------------===//
//
//  FastLink utilities: errors, the worker pool, hashing and the lock-free
//  name table.
//
//===----------------------------------------------------------------------===//

#ifndef LINKER_ELF_FASTPATH_FASTLINKSUPPORT_H
#define LINKER_ELF_FASTPATH_FASTLINKSUPPORT_H

#include <elf.h>
#include <sys/mman.h>

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
      running_.store(n_ - 1, std::memory_order_relaxed);
      gen_.fetch_add(1, std::memory_order_release);
    }
    cv_.notify_all();
    invoke(fn, 0);
    // Phases are short, so wait briefly before sleeping.
    for (unsigned k = 0; k < SpinLimit; ++k) {
      if (running_.load(std::memory_order_acquire) == 0)
        break;
      pause();
    }
    if (running_.load(std::memory_order_acquire) != 0) {
      std::unique_lock<std::mutex> l(m_);
      done_.wait(l, [&] {
        return running_.load(std::memory_order_acquire) == 0;
      });
    }
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
  static void pause() {
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#endif
  }
  void loop(unsigned i) {
    tid_ = i;
    unsigned seen = 0;
    for (;;) {
      // The next phase usually starts within microseconds.
      for (unsigned k = 0; k < SpinLimit; ++k) {
        if (gen_.load(std::memory_order_acquire) != seen)
          break;
        pause();
      }
      const std::function<void(unsigned)> *j;
      {
        std::unique_lock<std::mutex> l(m_);
        cv_.wait(l, [&] {
          return stop_ || gen_.load(std::memory_order_acquire) != seen;
        });
        if (stop_)
          return;
        seen = gen_.load(std::memory_order_acquire);
        j = job_;
      }
      invoke(*j, i);
      if (running_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        std::lock_guard<std::mutex> l(m_);
        done_.notify_all();
      }
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
  static constexpr unsigned SpinLimit = 2000;
  const std::function<void(unsigned)> *job_ = nullptr;
  std::atomic<unsigned> gen_{0}, running_{0};
  bool stop_ = false;
  std::mutex errorLock_;
  std::exception_ptr error_;
  std::atomic<bool> failed_{false};
  static inline thread_local unsigned tid_ = 0;
};

// Faults in [p, p + n), each 2 MiB block by one worker: workers faulting in
// neighboring pages would contend for the lock of the page table they share,
// and populating a block at once avoids a fault per page.
inline void prefault(Pool &pool, const void *p, size_t n, bool write) {
  constexpr uintptr_t Block = 2 << 20;
  const uintptr_t pageSize = 4096;
  uintptr_t b = reinterpret_cast<uintptr_t>(p) & ~(pageSize - 1);
  uintptr_t e = reinterpret_cast<uintptr_t>(p) + n;
  if (b >= e)
    return;
  size_t first = b / Block, last = (e - 1) / Block;
  pool.forEach(last - first + 1, [&](size_t i) {
    uintptr_t lo = std::max(b, (first + i) * Block);
    uintptr_t hi = std::min(e, (first + i + 1) * Block);
    hi = (hi + pageSize - 1) & ~(pageSize - 1);
    // MADV_POPULATE_READ and MADV_POPULATE_WRITE, from Linux 5.14.
    if (madvise(reinterpret_cast<void *>(lo), hi - lo, write ? 23 : 22) == 0)
      return;
    for (uintptr_t q = lo; q < hi; q += pageSize) {
      volatile char *c = reinterpret_cast<volatile char *>(q);
      char v = *c;
      if (write)
        *c = v;
    }
  }, 1);
}

// Zeroed storage for `n` objects of trivially constructible type `T`, faulted
// in by the pool.
template <class T> T *bigArray(Pool &pool, size_t n) {
  T *p = static_cast<T *>(calloc(std::max<size_t>(n, 1), sizeof(T)));
  if (!p)
    throw Failure{"out of memory"};
  prefault(pool, p, n * sizeof(T), true);
  return p;
}

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

// Lock-free insert-only table from names to dense ids. A slot holds a hash
// tag and an id, published with a single compare-and-swap once the id's name
// is stored, so readers never wait for a writer. Ids of names that lost an
// insertion race stay unused.
class NameTable {
public:
  explicit NameTable(size_t expected) {
    size_t cap = size_t(1) << 16;
    while (cap < expected * 2)
      cap <<= 1;
    mask_ = cap - 1;
    maxIds_ = cap / 2;
    slots_ = static_cast<std::atomic<uint64_t> *>(
        calloc(cap, sizeof(std::atomic<uint64_t>)));
    names_ = static_cast<Name *>(
        calloc(maxIds_ + size_t(MaxWorkers) * Block, sizeof(Name)));
  }
  // Returns the name's id, or UINT32_MAX once the table is past half full;
  // callers then retry with a larger table. Names in different groups are
  // different.
  uint32_t intern(const char *p, size_t n, uint32_t group = 0) {
    const uint64_t h =
        hashBytes(p, n) ^ (uint64_t(group) * 0x9E3779B97F4A7C15ull);
    const uint64_t tag = h >> 32;
    uint32_t mine = UINT32_MAX;
    for (size_t i = h & mask_;; i = (i + 1) & mask_) {
      uint64_t cur = slots_[i].load(std::memory_order_acquire);
      if (cur == 0) {
        if (mine == UINT32_MAX) {
          mine = allocId(Pool::self());
          if (mine >= maxIds_)
            return UINT32_MAX;
          names_[mine] = {p, uint32_t(n), group};
        }
        if (slots_[i].compare_exchange_strong(cur, (tag << 32) | (mine + 1),
                                              std::memory_order_acq_rel))
          return mine;
      }
      if ((cur >> 32) == tag) {
        const uint32_t id = uint32_t(cur) - 1;
        const Name &e = names_[id];
        if (e.len == n && e.group == group && memcmp(e.ptr, p, n) == 0) {
          if (mine != UINT32_MAX)
            names_[mine] = {};
          return id;
        }
      }
    }
  }

  // Looks a name up without inserting it; returns UINT32_MAX if absent.
  uint32_t find(const char *p, size_t n) const {
    const uint64_t h = hashBytes(p, n);
    for (size_t i = h & mask_;; i = (i + 1) & mask_) {
      uint64_t cur = slots_[i].load(std::memory_order_acquire);
      if (cur == 0)
        return UINT32_MAX;
      if ((cur >> 32) == (h >> 32)) {
        const Name &e = names_[uint32_t(cur) - 1];
        if (e.len == n && memcmp(e.ptr, p, n) == 0)
          return uint32_t(cur) - 1;
      }
    }
  }
  // Faults the table in with the pool's workers.
  void prefault(Pool &pool) {
    fl::prefault(pool, slots_, (mask_ + 1) * sizeof(*slots_), true);
    fl::prefault(pool, names_, (maxIds_ + size_t(MaxWorkers) * Block) * sizeof(Name),
                 true);
  }
  // One past the highest id handed out; ids below it may be unused.
  uint32_t size() const { return next_.load(); }
  // An upper bound of the ids intern() returns.
  size_t idLimit() const { return maxIds_; }
  // Fills names[id] for every id; unused ids get empty names.
  void names(vector<string_view> &out) const {
    const uint32_t n = size();
    out.resize(n);
    for (uint32_t id = 0; id < n; ++id)
      out[id] = string_view(names_[id].ptr ? names_[id].ptr : "", names_[id].len);
  }

private:
  struct Name {
    const char *ptr;
    uint32_t len;
    uint32_t group;
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
  std::atomic<uint64_t> *slots_;
  Name *names_;
  size_t mask_, maxIds_;
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
