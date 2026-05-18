//===----------------------------------------------------------------------===//
//
//  Stopwatch — scoped hierarchical timer tree.
//
//  Every backend creates a root `Timer` for its `link()` run and attaches
//  child timers for coarse-grained phases (parse, resolve, write, ...).
//  `ScopedTimer` accumulates the elapsed nanoseconds into its parent
//  `Timer` when it goes out of scope; `Timer::print()` renders the tree
//  on the `message()` stream when `--time-trace` is requested.
//
//===----------------------------------------------------------------------===//

#ifndef LINKER_CORE_RUNTIME_STOPWATCH_H
#define LINKER_CORE_RUNTIME_STOPWATCH_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include <assert.h>
#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <vector>

namespace linker {

class Timer;

struct ScopedTimer {
  explicit ScopedTimer(Timer &t);
  ~ScopedTimer();

  void stop();

  std::chrono::time_point<std::chrono::high_resolution_clock> startTime;
  Timer *t = nullptr;
};

class Timer {
public:
  // Attach a new timer under `parent`.
  Timer(llvm::StringRef name, Timer &parent);
  // Root timer.
  explicit Timer(llvm::StringRef name);

  void addToTotal(std::chrono::nanoseconds time) { total += time.count(); }
  void print();

  double millis() const;

private:
  void print(int depth, double totalDuration, bool recurse = true) const;

  std::atomic<std::chrono::nanoseconds::rep> total;
  std::vector<Timer *> children;
  std::string name;
};

} // namespace linker

#endif // LINKER_CORE_RUNTIME_STOPWATCH_H
